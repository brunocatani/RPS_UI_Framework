/*
 * RPSUISettingsPreview.exe: RPS UI's settings menu on the desktop, without the game. The
 * menu's own panel callbacks run in a LocalPanelHost with RPS UI's default theme, exactly
 * as RPS UI runs them in the headset.
 *
 * Settings live in "Settings sandbox" beside the exe, filled from the mods' example files
 * the first time and again whenever a mod's example changes. --settings <folder> copies that folder's files over the sandbox's first (a
 * Mods_Config folder works: each mod's file is looked up in its own subfolder); the folder
 * itself is only read. The preview reads and writes the sandbox as the game does its files.
 * Without ROCK running, ROCK's pages read ROCK's files.
 *
 * Mouse: the pointer and trigger. Right click: Back (a grip tap). Wheel: the stick's scroll.
 * Keyboard: typing for the Console and the Spawner's searches.
 * --capture <folder> writes the main pages to PNG instead of opening the window.
 */
#include <Windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <shellapi.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_internal.h>

#include <RPS_UI_Framework/ImGuiPanelHost.h>
#include <RPS_UI_Framework/ThemeDefaults.h>

#include "ConfiguratorRuntime.h"
#include "render/FrameworkPanelRenderer.h"
#include "render/MenuPanel.h"
#include "render/NativeRenderer.h"

#include <ShlObj.h>
#include <array>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using Microsoft::WRL::ComPtr;

// The game's panel registration is not part of the preview; the menu asks for it only to
// show and place its panels, which the preview window does itself.
namespace devui::render
{
    void PrepareFonts() noexcept {}
    bool SetPanelOpen(bool, const PanelPose*) noexcept { return true; }
    void SyncKeyboardPanel(bool) noexcept {}
    void Shutdown() noexcept {}
    bool InstallFrameworkPanel() noexcept { return false; }
    bool ResetFrameworkPanelLayout() noexcept { return true; }
}

namespace
{
    namespace menu = rpsui::settings_menu;
    namespace fw = rpsui::api::framework;

    ComPtr<ID3D11Device> g_device;
    ComPtr<ID3D11DeviceContext> g_context;

    void check(HRESULT result, const char* what)
    {
        if (FAILED(result)) throw std::runtime_error(std::string(what) + " failed");
    }

    [[nodiscard]] fs::path exeFolder()
    {
        std::array<wchar_t, 32768> path{};
        GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
        return fs::path(path.data()).parent_path();
    }

    [[nodiscard]] std::vector<char> readFile(const fs::path& path)
    {
        std::ifstream file(path, std::ios::binary);
        if (!file) return {};
        return { std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>() };
    }

    [[nodiscard]] fs::path knownFolder(const KNOWNFOLDERID& id)
    {
        PWSTR raw = nullptr;
        fs::path result;
        if (SUCCEEDED(SHGetKnownFolderPath(id, 0, nullptr, &raw))) result = raw;
        CoTaskMemFree(raw);
        return result;
    }

    // RPS UI's faces, looked up as RPS UI looks them up: its Fonts folder, then Windows' fonts.
    struct Faces
    {
        std::array<std::vector<char>, fw::kFontRoleCount> bytes;

        void load()
        {
            const auto own = knownFolder(FOLDERID_Documents) / "My Games" / "Fallout4VR" / "Mods_Config" / "RPS_UI_Framework" / "Fonts";
            const auto windows = knownFolder(FOLDERID_Fonts);
            for (std::size_t role = 0; role < bytes.size(); ++role) {
                const fs::path file{ std::string(rpsui::theme_defaults::kFonts[role].file) };
                bytes[role] = readFile(own / file);
                if (bytes[role].empty()) bytes[role] = readFile(windows / file);
            }
        }

        [[nodiscard]] rpsui::imgui_host::FaceSet set() const
        {
            rpsui::imgui_host::FaceSet faces{};
            for (std::size_t role = 0; role < bytes.size(); ++role) faces[role] = { bytes[role].data(), bytes[role].size() };
            return faces;
        }
    };

    // The sandbox, laid out as Mods_Config holds the files. A file from `source` (read only)
    // replaces the sandbox's; a file still missing comes from the build's "examples". The
    // preview cannot reorganize a file as its mod does at startup, so when a mod's example
    // has changed since the sandbox file was filled from it (its layout or options moved),
    // the sandbox file is filled again; "examples" inside the sandbox keeps what it was
    // filled from.
    [[nodiscard]] fs::path prepareSandbox(const fs::path& folder, const fs::path& source = {})
    {
        const auto examples = exeFolder() / "examples";
        const auto filledFrom = folder / "examples";
        std::error_code error;
        fs::create_directories(filledFrom, error);
        for (const auto& [example, mod, file] : { std::array<const char*, 3>{ "ROCK_example.toml", "ROCK", "ROCK.toml" },
                 std::array<const char*, 3>{ "ROCK_Developer_example.toml", "ROCK", "ROCK_Developer.toml" },
                 std::array<const char*, 3>{ "PAPER_example.toml", "PAPER", "PAPER.toml" } }) {
            const auto target = folder / mod / file;
            const auto current = readFile(examples / example);
            if (!current.empty() && readFile(filledFrom / example) != current) {
                fs::remove(target, error);
                fs::copy_file(examples / example, filledFrom / example, fs::copy_options::overwrite_existing, error);
            }
            if (!source.empty()) {
                for (const auto& from : { source / mod / file, source / file }) {
                    if (!fs::is_regular_file(from, error)) continue;
                    fs::create_directories(target.parent_path(), error);
                    fs::copy_file(from, target, fs::copy_options::overwrite_existing, error);
                    break;
                }
            }
            if (fs::exists(target, error) || !fs::exists(examples / example, error)) continue;
            fs::create_directories(target.parent_path(), error);
            fs::copy_file(examples / example, target, error);
        }
        return folder;
    }

    [[nodiscard]] fw::PanelRegistration registration()
    {
        fw::PanelRegistration panel{};
        panel.layoutWidth = static_cast<float>(devui::render::kPanelLayoutWidth);
        panel.layoutHeight = static_cast<float>(devui::render::kPanelLayoutHeight);
        const auto callbacks = devui::render::panelCallbacks(false);
        panel.setupCallback = callbacks.setup;
        panel.drawCallback = callbacks.draw;
        panel.userData = callbacks.userData;
        return panel;
    }

    struct Target
    {
        ComPtr<ID3D11Texture2D> texture;
        ComPtr<ID3D11RenderTargetView> view;
        UINT width{ 0 }, height{ 0 };

        void create(UINT w, UINT h)
        {
            D3D11_TEXTURE2D_DESC desc{};
            desc.Width = width = w;
            desc.Height = height = h;
            desc.MipLevels = desc.ArraySize = 1;
            desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
            desc.SampleDesc.Count = 1;
            desc.BindFlags = D3D11_BIND_RENDER_TARGET;
            check(g_device->CreateTexture2D(&desc, nullptr, &texture), "Panel texture");
            check(g_device->CreateRenderTargetView(texture.Get(), nullptr, &view), "Panel target");
        }
    };

    void writePng(const Target& target, const fs::path& file)
    {
        D3D11_TEXTURE2D_DESC desc{};
        target.texture->GetDesc(&desc);
        desc.BindFlags = 0;
        desc.Usage = D3D11_USAGE_STAGING;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        ComPtr<ID3D11Texture2D> staging;
        check(g_device->CreateTexture2D(&desc, nullptr, &staging), "Staging texture");
        g_context->CopyResource(staging.Get(), target.texture.Get());
        D3D11_MAPPED_SUBRESOURCE mapped{};
        check(g_context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped), "Texture map");
        ComPtr<IWICImagingFactory> factory;
        check(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)), "WIC");
        ComPtr<IWICStream> stream;
        check(factory->CreateStream(&stream), "WIC stream");
        check(stream->InitializeFromFilename(file.c_str(), GENERIC_WRITE), "PNG file");
        ComPtr<IWICBitmapEncoder> encoder;
        check(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder), "PNG encoder");
        check(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache), "PNG encoder");
        ComPtr<IWICBitmapFrameEncode> frame;
        check(encoder->CreateNewFrame(&frame, nullptr), "PNG frame");
        check(frame->Initialize(nullptr), "PNG frame");
        check(frame->SetSize(target.width, target.height), "PNG size");
        WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
        check(frame->SetPixelFormat(&format), "PNG format");
        check(frame->WritePixels(target.height, mapped.RowPitch, mapped.RowPitch * target.height,
            static_cast<BYTE*>(mapped.pData)), "PNG pixels");
        check(frame->Commit(), "PNG frame");
        check(encoder->Commit(), "PNG file");
        g_context->Unmap(staging.Get(), 0);
    }

    // Each main page, from a sandbox of its own inside the output folder.
    int capture(const fs::path& folder, const Faces& faces)
    {
        fs::create_directories(folder);
        auto sandbox = folder / "sandbox";
        std::error_code error;
        fs::remove_all(sandbox, error);
        menu::initializeFilePreview(prepareSandbox(sandbox));
        menu::setPreviewOpen(true);
        Target target;
        target.create(devui::render::kPanelLayoutWidth, devui::render::kPanelLayoutHeight);
        rpsui::imgui_host::LocalPanelHost host(g_device.Get(), g_context.Get(), registration(), faces.set());
        const auto settle = [&] {
            for (int i = 0; i < 8; ++i) {
                const float clear[4]{ 0, 0, 0, 0 };
                g_context->ClearRenderTargetView(target.view.Get(), clear);
                rpsui::imgui_host::PanelInput input{};
                input.pointerValid = false;
                host.frame(target.view.Get(), input);
                menu::drainPreviewActions();
            }
        };
        const auto save = [&](const char* name) {
            settle();
            writePng(target, folder / name);
        };
        // A trigger pull at a point of the menu, as a player's pointer makes it: hover, press, release.
        const auto click = [&](float x, float y) {
            for (const bool down : { false, true, false }) {
                rpsui::imgui_host::PanelInput input{};
                input.pointerValid = true;
                input.pointerX = x;
                input.pointerY = y;
                input.primaryDown = down;
                host.frame(target.view.Get(), input);
                menu::drainPreviewActions();
            }
        };
        menu::previewShow("ROCK", "Grabbing");
        save("01-rock-grabbing.png");
        click(1140.0f, 400.0f); // Fingers (Alyx Style)
        save("01b-rock-grabbing-fingers-picked.png");
        menu::previewShow("ROCK", "Prompts");
        save("02-rock-prompts.png");
        menu::previewShow("ROCK", "Weapon Handling");
        save("03-rock-weapon-handling.png");
        click(570.0f, 378.0f); // Gunstock Mode: On
        save("03b-rock-gunstock-on-picked.png");
        menu::previewOpenFineTuning(true);
        save("04-rock-weapon-handling-fine-tuning.png");
        menu::previewOpenFineTuning(false);
        menu::previewShow("ROCK", "Recoil");
        save("05-rock-recoil.png");
        click(850.0f, 477.0f); // the Fine-tuning fold
        save("05b-rock-recoil-fine-tuning-opened.png");
        menu::previewShow("ROCK", "Developer");
        save("06-rock-developer-switch.png");
        menu::previewShow("PAPER", "Bolt");
        save("07-paper-bolt-appreciation.png");
        click(570.0f, 370.0f); // Bolt Appreciation: On
        save("07b-paper-bolt-appreciation-on-picked.png");
        menu::previewShow("PAPER", "Reload");
        save("07c-paper-reload.png");
        // With ROCK.toml's Developer Settings on, the ROCK Dev page joins the pages.
        {
            const auto rock = sandbox / "ROCK" / "ROCK.toml";
            const auto bytes = readFile(rock);
            std::string text(bytes.begin(), bytes.end());
            const std::string off = "EnableDeveloperSettings = false", on = "EnableDeveloperSettings = true";
            if (const auto at = text.find(off); at != std::string::npos) text.replace(at, off.size(), on);
            std::ofstream(rock, std::ios::binary | std::ios::trunc) << text;
            menu::initializeFilePreview(sandbox);
            menu::setPreviewOpen(true);
            menu::previewShow("ROCK Dev", "");
            save("08-rock-dev.png");
        }
        menu::previewShowTab("Spawner");
        save("09-spawner.png");
        menu::previewShowTab("Console");
        save("10-console.png");
        return 0;
    }

    // ---- the window ------------------------------------------------------------------------

    std::vector<menu::ConsoleKeyEvent> g_keys;
    float g_wheel = 0.0f;
    bool g_back = false;
    UINT g_resizeWidth = 0, g_resizeHeight = 0;

    LRESULT CALLBACK windowProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam)
    {
        switch (message) {
        case WM_CHAR:
            if (wparam >= 32) g_keys.push_back({ true, static_cast<std::uint32_t>(wparam) });
            return 0;
        case WM_KEYDOWN:
            g_keys.push_back({ false, static_cast<std::uint32_t>(wparam) });
            return 0;
        case WM_MOUSEWHEEL:
            g_wheel += static_cast<float>(GET_WHEEL_DELTA_WPARAM(wparam)) / WHEEL_DELTA;
            return 0;
        case WM_RBUTTONUP:
            g_back = true;
            return 0;
        case WM_SIZE:
            if (wparam != SIZE_MINIMIZED) {
                g_resizeWidth = LOWORD(lparam);
                g_resizeHeight = HIWORD(lparam);
            }
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        default:
            return DefWindowProcW(hwnd, message, wparam, lparam);
        }
    }

    [[nodiscard]] std::wstring argument(int argc, wchar_t** argv, std::wstring_view name)
    {
        for (int i = 1; i + 1 < argc; ++i)
            if (name == argv[i]) return argv[i + 1];
        return {};
    }

    int run(HINSTANCE instance, const fs::path& settings, const Faces& faces)
    {
        menu::initializeFilePreview(settings);
        menu::setPreviewOpen(true);

        WNDCLASSEXW windowClass{ sizeof(windowClass), CS_CLASSDC, windowProc, 0, 0, instance };
        windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        windowClass.lpszClassName = L"RPSUISettingsPreview";
        RegisterClassExW(&windowClass);
        RECT frame{ 0, 0, static_cast<LONG>(devui::render::kPanelLayoutWidth), static_cast<LONG>(devui::render::kPanelLayoutHeight) };
        AdjustWindowRect(&frame, WS_OVERLAPPEDWINDOW, FALSE);
        const auto title = L"RPS UI settings menu preview - " + settings.wstring();
        HWND hwnd = CreateWindowW(windowClass.lpszClassName, title.c_str(), WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
            frame.right - frame.left, frame.bottom - frame.top, nullptr, nullptr, instance, nullptr);

        DXGI_SWAP_CHAIN_DESC desc{};
        desc.BufferCount = 2;
        desc.BufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        desc.OutputWindow = hwnd;
        desc.SampleDesc.Count = 1;
        desc.Windowed = TRUE;
        desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
        ComPtr<IDXGISwapChain> swap;
        g_device.Reset();
        g_context.Reset();
        check(D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &desc,
            &swap, &g_device, nullptr, &g_context), "Direct3D 11 device");
        ComPtr<ID3D11RenderTargetView> backBuffer;
        const auto createBackBuffer = [&] {
            ComPtr<ID3D11Texture2D> buffer;
            check(swap->GetBuffer(0, IID_PPV_ARGS(&buffer)), "Back buffer");
            check(g_device->CreateRenderTargetView(buffer.Get(), nullptr, &backBuffer), "Back buffer target");
        };
        createBackBuffer();
        std::optional<rpsui::imgui_host::LocalPanelHost> host;
        host.emplace(g_device.Get(), g_context.Get(), registration(), faces.set());
        ShowWindow(hwnd, SW_SHOWDEFAULT);

        LARGE_INTEGER frequency{}, last{};
        QueryPerformanceFrequency(&frequency);
        QueryPerformanceCounter(&last);
        for (bool done = false; !done;) {
            MSG message{};
            while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
                TranslateMessage(&message);
                DispatchMessageW(&message);
                if (message.message == WM_QUIT) done = true;
            }
            if (done) break;
            if (g_resizeWidth && g_resizeHeight) {
                backBuffer.Reset();
                check(swap->ResizeBuffers(0, g_resizeWidth, g_resizeHeight, DXGI_FORMAT_UNKNOWN, 0), "Resize");
                createBackBuffer();
                g_resizeWidth = g_resizeHeight = 0;
            }
            RECT client{};
            GetClientRect(hwnd, &client);
            const auto width = static_cast<float>((std::max)(client.right, 1L));
            const auto height = static_cast<float>((std::max)(client.bottom, 1L));
            LARGE_INTEGER now{};
            QueryPerformanceCounter(&now);
            const float delta = static_cast<float>(now.QuadPart - last.QuadPart) / static_cast<float>(frequency.QuadPart);
            last = now;

            POINT cursor{};
            GetCursorPos(&cursor);
            ScreenToClient(hwnd, &cursor);
            rpsui::imgui_host::PanelInput input{};
            input.pixelWidth = static_cast<std::uint32_t>(width);
            input.pixelHeight = static_cast<std::uint32_t>(height);
            input.deltaSeconds = delta;
            input.pointerValid = GetForegroundWindow() == hwnd && cursor.x >= 0 && cursor.y >= 0 &&
                                 cursor.x < client.right && cursor.y < client.bottom;
            input.pointerX = static_cast<float>(cursor.x) * devui::render::kPanelLayoutWidth / width;
            input.pointerY = static_cast<float>(cursor.y) * devui::render::kPanelLayoutHeight / height;
            input.primaryDown = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
            // The host turns a stick's deflection into scroll over the frame; one wheel notch is one step.
            const float clampedDelta = std::clamp(delta, 1.0f / 240.0f, 0.1f);
            input.scrollAxisY = g_wheel / (rpsui::imgui_host::kWheelUnitsPerSecond * clampedDelta);
            g_wheel = 0.0f;

            const float clear[4]{ 0.02f, 0.03f, 0.02f, 1.0f };
            g_context->ClearRenderTargetView(backBuffer.Get(), clear);
            if (!menu::isOpen()) menu::setPreviewOpen(true); // Closing in the preview reopens the menu.
            host->frame(backBuffer.Get(), input, fw::PhysicalHand::Right, std::exchange(g_back, false));
            if (!g_keys.empty()) {
                menu::previewKeys(g_keys);
                g_keys.clear();
            }
            menu::drainPreviewActions();
            swap->Present(1, 0);
        }
        host.reset();
        DestroyWindow(hwnd);
        return 0;
    }
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int)
{
    int argc = 0;
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    try {
        check(CoInitializeEx(nullptr, COINIT_MULTITHREADED), "COM");
        Faces faces;
        faces.load();
        if (const auto folder = argument(argc, argv, L"--capture"); !folder.empty()) {
            check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &g_device, nullptr,
                &g_context), "Direct3D 11 WARP device");
            return capture(fs::absolute(folder), faces);
        }
        const auto given = argument(argc, argv, L"--settings");
        const auto settings = prepareSandbox(exeFolder() / "Settings sandbox", given.empty() ? fs::path{} : fs::path(given));
        return run(instance, settings, faces);
    } catch (const std::exception& error) {
        MessageBoxA(nullptr, error.what(), "RPS UI settings menu preview", MB_ICONERROR);
        return 1;
    }
}
