#include "PCH.h"

#include "ConsoleService.h"

#include <ShlObj.h>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iterator>

namespace rpsui::settings_menu::console
{
    namespace
    {
        constexpr std::size_t kPendingLineLimit = 4000;

        std::mutex s_mutex;
        std::deque<std::string> s_pending;
        std::atomic_bool s_installed{ false };

        void store(std::string line)
        {
            std::scoped_lock lock(s_mutex);
            s_pending.push_back(std::move(line));
            if (s_pending.size() > kPendingLineLimit) {
                s_pending.pop_front();
            }
        }

#ifndef RPSUI_SETTINGS_MENU_PREVIEW
        // ConsoleLog::AddString's only caller in the game is the console's print
        // function 0x1412E3E80, which formats one message and calls it here, so every
        // line passes this call whether or not the Console menu has ever been opened.
        // Reading ConsoleLog's buffer instead would lose lines: once the menu exists it
        // empties the buffer on its own schedule. AddString also drops text past its
        // 64 KB limit; this sees that text too.
        constexpr std::uintptr_t kPrintContextRva = 0x12E3ED0;
        constexpr std::uintptr_t kAddStringCallRva = 0x12E3EDF;
        constexpr std::uintptr_t kAfterCallRva = 0x12E3EE4;
        // Up to and including the CALL opcode; its target may be another mod's chain.
        constexpr std::array<std::uint8_t, 16> kPrintContext{
            0x48, 0x8D, 0x54, 0x24, 0x20,                    // lea rdx,[rsp+20h]
            0x48, 0x8B, 0xCE,                                // mov rcx,rsi
            0x66, 0xC7, 0x44, 0x04, 0x20, 0x0A, 0x00,        // mov word ptr [rsp+rax+20h],0Ah
            0xE8,                                            // call
        };
        constexpr std::array<std::uint8_t, 8> kAfterCall{ 0x4C, 0x8D, 0x9C, 0x24, 0x20, 0x10, 0x00, 0x00 }; // lea r11,[rsp+1020h]

        using AddString = void (*)(void*, const char*);
        AddString s_addString{ nullptr };

        [[nodiscard]] bool bytesMatch(std::uintptr_t rva, const std::uint8_t* expected, std::size_t count) noexcept
        {
            __try {
                return std::memcmp(reinterpret_cast<const void*>(REL::Offset(rva).address()), expected, count) == 0;
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                return false;
            }
        }

        [[nodiscard]] std::wstring widen(std::string_view text, UINT codePage, DWORD flags)
        {
            const auto length = static_cast<int>(text.size());
            const auto needed = MultiByteToWideChar(codePage, flags, text.data(), length, nullptr, 0);
            std::wstring wide(static_cast<std::size_t>((std::max)(needed, 0)), L'\0');
            if (needed > 0) {
                MultiByteToWideChar(codePage, flags, text.data(), length, wide.data(), needed);
            }
            return wide;
        }

        [[nodiscard]] std::string narrow(std::wstring_view text, UINT codePage)
        {
            const auto length = static_cast<int>(text.size());
            const auto needed = WideCharToMultiByte(codePage, 0, text.data(), length, nullptr, 0, nullptr, nullptr);
            std::string bytes(static_cast<std::size_t>((std::max)(needed, 0)), '\0');
            if (needed > 0) {
                WideCharToMultiByte(codePage, 0, text.data(), length, bytes.data(), needed, nullptr, nullptr);
            }
            return bytes;
        }

        // Game text is in the system code page, but a mod may print UTF-8.
        [[nodiscard]] std::string toUtf8(std::string_view line)
        {
            if (std::all_of(line.begin(), line.end(), [](char c) { return static_cast<unsigned char>(c) < 0x80; }) ||
                !widen(line, CP_UTF8, MB_ERR_INVALID_CHARS).empty()) {
                return std::string(line);
            }
            return narrow(widen(line, CP_ACP, 0), CP_UTF8);
        }

        void capture(const char* text) noexcept
        {
            if (!text) {
                return;
            }
            try {
                std::string_view rest(text);
                while (!rest.empty()) {
                    const auto end = rest.find('\n');
                    auto line = rest.substr(0, end);
                    if (!line.empty() && line.back() == '\r') {
                        line.remove_suffix(1);
                    }
                    store(toUtf8(line));
                    if (end == std::string_view::npos) {
                        break;
                    }
                    rest.remove_prefix(end + 1);
                }
            } catch (...) {
                static std::atomic_bool logged{ false };
                if (!logged.exchange(true)) {
                    logger::error("Settings menu console capture dropped a line after an allocation failure");
                }
            }
        }

        void onAddString(void* log, const char* text)
        {
            capture(text);
            s_addString(log, text);
        }
#endif
    }

    bool installCapture() noexcept
    {
#ifdef RPSUI_SETTINGS_MENU_PREVIEW
        return true;
#else
        try {
            if (!bytesMatch(kPrintContextRva, kPrintContext.data(), kPrintContext.size()) ||
                !bytesMatch(kAfterCallRva, kAfterCall.data(), kAfterCall.size())) {
                logger::error("Settings menu console output unavailable: the console print function is not the expected code");
                return false;
            }
            F4SE::AllocTrampoline(64);
            s_addString = reinterpret_cast<AddString>(
                F4SE::GetTrampoline().write_call<5>(REL::Offset(kAddStringCallRva).address(), &onAddString));
            s_installed = s_addString != nullptr;
            logger::info("Settings menu console output capture {}", s_installed ? "installed" : "failed");
            return s_installed;
        } catch (const std::exception& error) {
            logger::error("Settings menu console output unavailable: {}", error.what());
            return false;
        }
#endif
    }

    bool captureInstalled() noexcept
    {
#ifdef RPSUI_SETTINGS_MENU_PREVIEW
        return true;
#else
        return s_installed.load();
#endif
    }

    void takeLines(std::vector<std::string>& output)
    {
        std::scoped_lock lock(s_mutex);
#ifdef RPSUI_SETTINGS_MENU_PREVIEW
        static bool seeded = false;
        if (!seeded) {
            seeded = true;
            for (const auto* line : { "player.getpos x", "GetPos: X >> -89.48", "help lockpick 0",
                     "Unknown command. This line is a long preview fixture that shows how a message wider than the panel wraps onto the next row without running past the edge.",
                     "coc qasmoke" }) {
                s_pending.emplace_back(line);
            }
        }
#endif
        output.insert(output.end(), std::make_move_iterator(s_pending.begin()), std::make_move_iterator(s_pending.end()));
        s_pending.clear();
    }

    void run(const std::string& command)
    {
#ifdef RPSUI_SETTINGS_MENU_PREVIEW
        store(command);
#else
        RE::Console::ExecuteCommand(narrow(widen(command, CP_UTF8, 0), CP_ACP).c_str());
#endif
    }

    ShortcutFile loadShortcuts()
    {
#ifdef RPSUI_SETTINGS_MENU_PREVIEW
        return { parseConsoleShortcuts(kDefaultConsoleShortcuts), {} };
#else
        PWSTR documents = nullptr;
        if (FAILED(SHGetKnownFolderPath(FOLDERID_Documents, KF_FLAG_DEFAULT, nullptr, &documents)) || !documents) {
            CoTaskMemFree(documents);
            logger::warn("Settings menu console shortcuts unavailable: the Documents folder could not be resolved");
            return { {}, "Console shortcuts unavailable: Documents folder not found" };
        }
        const auto path = std::filesystem::path(documents) / "My Games" / "Fallout4VR" / "Mods_Config" / "RPS_UI_Framework" /
                          "ConsoleCommands.ini";
        CoTaskMemFree(documents);

        std::error_code error;
        if (!std::filesystem::exists(path, error)) {
            std::filesystem::create_directories(path.parent_path(), error);
            std::ofstream created(path, std::ios::binary);
            created.write(kDefaultConsoleShortcuts.data(), static_cast<std::streamsize>(kDefaultConsoleShortcuts.size()));
            if (!created) {
                logger::warn("RPS UI could not create {}", path.string());
                return { {}, "Could not create ConsoleCommands.ini; see RPS_UI_Framework.log" };
            }
            logger::info("RPS UI created {} with the default console shortcuts", path.string());
        }

        std::ifstream file(path, std::ios::binary);
        if (!file) {
            logger::warn("RPS UI could not read {}", path.string());
            return { {}, "Could not read ConsoleCommands.ini; see RPS_UI_Framework.log" };
        }
        const std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        auto shortcuts = parseConsoleShortcuts(text);
        logger::info("RPS UI loaded {} console shortcut(s) from {}", shortcuts.size(), path.string());
        if (shortcuts.empty()) {
            return { {}, "No shortcuts: add [Label] sections with commands to Mods_Config/RPS_UI_Framework/ConsoleCommands.ini" };
        }
        return { std::move(shortcuts), {} };
#endif
    }

    std::optional<std::uint32_t> selectedReference()
    {
#ifdef RPSUI_SETTINGS_MENU_PREVIEW
        return std::uint32_t{ 0x0001A2B3 };
#else
        const auto reference = RE::Console::GetPickRef().get();
        return reference ? std::optional<std::uint32_t>(reference->GetFormID()) : std::nullopt;
#endif
    }
}
