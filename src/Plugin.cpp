#include "PCH.h"

#include "FrameworkRuntime.h"
#include "Logger.h"
#include "render/RenderHooks.h"
#include "render/SceneDepthCapture.h"

#include <Windows.h>

namespace
{
    std::atomic_bool g_runtimeInstalled = false;

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

        if (!rpsui::render::SceneDepthCapture::Install()) {
            rpsui::log::critical(
                "RPS UI Framework requires guarded same-frame scene depth");
            return;
        }
        if (!rpsui::render::RenderHooks::Install()) {
            rpsui::log::critical(
                "RPS UI Framework could not claim the exclusive FO4VR stereo "
                "callsites; disable PrismaUI and standalone native renderers");
            return;
        }

        auto& runtime = rpsui::FrameworkRuntime::get();
        runtime.setRendererReady(true);
        runtime.start();
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
