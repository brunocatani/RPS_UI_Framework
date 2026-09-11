#include "PCH.h"

#include "FrameworkRuntime.h"
#include "InputService.h"
#include "Logger.h"
#include "render/RenderHooks.h"
#include "render/SceneDepthCapture.h"

#include <Windows.h>

namespace
{
    std::atomic_bool g_runtimeInstalled = false;

    void reportRuntimeModules() noexcept
    {
        // Startup only: version-resource reads must never run on a render or input callback.
        // Report observed modules and metadata, not an inferred runtime classification.
        for (const auto* name : { "openvr_api.dll", "vrclient_x64.dll", "openxr_loader.dll", "LibOVRRT64_1.dll" }) {
            try {
                const auto module = GetModuleHandleA(name);
                if (!module) {
                    rpsui::log::info("RPS UI runtime module: {} loaded=no", name);
                    continue;
                }
                std::array<char, 32768> path{};
                const auto length = GetModuleFileNameA(module, path.data(), static_cast<DWORD>(path.size()));
                if (!length || length >= path.size()) {
                    rpsui::log::warn("RPS UI runtime module: {} loaded=yes path unavailable error={}", name, GetLastError());
                    continue;
                }
                std::string version = "unavailable", description = "unavailable", product = "unavailable";
                const auto size = GetFileVersionInfoSizeA(path.data(), nullptr);
                if (size && size <= 1024 * 1024) {
                    std::vector<std::byte> data(size);
                    if (GetFileVersionInfoA(path.data(), 0, size, data.data())) {
                        void* value = nullptr;
                        UINT bytes = 0;
                        if (VerQueryValueA(data.data(), "\\", &value, &bytes) && value && bytes >= sizeof(VS_FIXEDFILEINFO)) {
                            const auto& info = *static_cast<const VS_FIXEDFILEINFO*>(value);
                            version = std::format("{}.{}.{}.{}", HIWORD(info.dwFileVersionMS), LOWORD(info.dwFileVersionMS),
                                HIWORD(info.dwFileVersionLS), LOWORD(info.dwFileVersionLS));
                        }
                        struct Translation { WORD language, codePage; };
                        if (VerQueryValueA(data.data(), "\\VarFileInfo\\Translation", &value, &bytes) && value && bytes >= sizeof(Translation)) {
                            const auto translation = *static_cast<const Translation*>(value);
                            const auto readString = [&](const char* key) {
                                char query[96]{};
                                std::snprintf(query, sizeof(query), "\\StringFileInfo\\%04x%04x\\%s",
                                    translation.language, translation.codePage, key);
                                void* text = nullptr; UINT characters = 0;
                                return VerQueryValueA(data.data(), query, &text, &characters) && text && characters ?
                                    std::string(static_cast<const char*>(text), strnlen_s(static_cast<const char*>(text), characters)) :
                                    std::string("unavailable");
                            };
                            description = readString("FileDescription");
                            product = readString("ProductName");
                        }
                    }
                }
                rpsui::log::info("RPS UI runtime module: {} path='{}' version={} description='{}' product='{}'",
                    name, path.data(), version, description, product);
            } catch (...) {
                rpsui::log::warn("RPS UI runtime module diagnostics failed for {}", name);
            }
        }
    }

    void reportBoundaryFailure(
        const char* boundary,
        const char* detail) noexcept
    {
        char message[512]{};
        const int written = std::snprintf(
            message,
            sizeof(message),
            "RPS UI Framework: unhandled exception in %s%s%s\n",
            boundary ? boundary : "plugin boundary",
            detail ? ": " : "",
            detail ? detail : "");
        if (written > 0) {
            ::OutputDebugStringA(message);
        }
    }

    void f4seMessageHandler(
        F4SE::MessagingInterface::Message* message) noexcept
    {
        if (message) {
            if (message->type == F4SE::MessagingInterface::kPreLoadGame) rpsui::input::sessionReady(false);
            if (message->type == F4SE::MessagingInterface::kNewGame) rpsui::input::sessionReady(true);
            if (message->type == F4SE::MessagingInterface::kPostLoadGame) rpsui::input::sessionReady(message->data != nullptr);
        }
        if (!message ||
            message->type !=
                F4SE::MessagingInterface::kGameDataReady) {
            return;
        }
        if (g_runtimeInstalled.exchange(
                true,
                std::memory_order_acq_rel)) {
            return;
        }

        reportRuntimeModules();

        auto& runtime = rpsui::FrameworkRuntime::get();
        if (!rpsui::render::SceneDepthCapture::Install()) {
            runtime.setHookStatus(rpsui::sdk::HookStatusV1::Unavailable);
            rpsui::log::critical(
                "RPS UI Framework requires guarded same-frame scene depth");
            return;
        }
        const auto hooks = rpsui::render::RenderHooks::Install();
        if (hooks != rpsui::render::RenderHooks::InstallResult::Installed) {
            runtime.setHookStatus(hooks == rpsui::render::RenderHooks::InstallResult::Conflict ?
                rpsui::sdk::HookStatusV1::Conflict : rpsui::sdk::HookStatusV1::Unavailable);
            rpsui::log::critical(
                "RPS UI Framework stereo submission unavailable; cooperating UI mods must use "
                "the panel callback API instead of replacing the same submission callsites");
            return;
        }

        runtime.setHookStatus(rpsui::sdk::HookStatusV1::Installed);
        runtime.setRendererReady(true);
        if (!runtime.start()) {
            rpsui::log::critical(
                "RPS UI Framework host unavailable: input initialization failed");
            return;
        }
        rpsui::log::info(
            "RPS UI Framework host is ready for UI consumers");
    }
}

extern "C" DLLEXPORT bool F4SEAPI F4SEPlugin_Query(
    const F4SE::QueryInterface* f4se,
    F4SE::PluginInfo* info) noexcept
{
    try {
        if (!f4se || !info || !rpsui::log::initialize()) {
            return false;
        }
        info->infoVersion = F4SE::PluginInfo::kVersion;
        info->name = "RPS_UI_Framework";
        info->version = 1;

        if (f4se->IsEditor() ||
            !REL::Module::IsVR() ||
            REL::Module::get().version() !=
                F4SE::RUNTIME_VR_1_2_72) {
            rpsui::log::critical(
                "RPS UI Framework requires Fallout 4 VR 1.2.72");
            return false;
        }

        const auto requiredRuntime = F4SE::RUNTIME_1_10_138;
        if (f4se->RuntimeVersion() != requiredRuntime) {
            rpsui::log::critical(
                "Unsupported F4SE compatibility runtime {}",
                f4se->RuntimeVersion().string());
            return false;
        }
        rpsui::log::info(
            "RPS UI Framework query passed for Fallout4VR.exe {}",
            REL::Module::get().version().string());
        return true;
    } catch (const std::exception& error) {
        reportBoundaryFailure("F4SEPlugin_Query", error.what());
        return false;
    } catch (...) {
        reportBoundaryFailure("F4SEPlugin_Query", nullptr);
        return false;
    }
}

extern "C" DLLEXPORT bool F4SEAPI F4SEPlugin_Load(
    const F4SE::LoadInterface* f4se) noexcept
{
    try {
        if (!f4se) {
            return false;
        }
        F4SE::Init(f4se, false);
        if (!REL::Module::IsVR() ||
            REL::Module::get().version() !=
                F4SE::RUNTIME_VR_1_2_72) {
            return false;
        }
        F4SE::AllocTrampoline(4096);
        const auto* messaging = F4SE::GetMessagingInterface();
        if (!messaging ||
            !messaging->RegisterListener(f4seMessageHandler)) {
            rpsui::log::critical(
                "F4SE messaging registration failed");
            return false;
        }
        rpsui::log::info(
            "RPS UI Framework load complete; API is available");
        return true;
    } catch (const std::exception& error) {
        reportBoundaryFailure("F4SEPlugin_Load", error.what());
        return false;
    } catch (...) {
        reportBoundaryFailure("F4SEPlugin_Load", nullptr);
        return false;
    }
}
