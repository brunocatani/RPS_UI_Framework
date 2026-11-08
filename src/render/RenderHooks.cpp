#include "PCH.h"

#include "Logger.h"

#include "render/EngineStereoSubmissionPolicy.h"
#include "render/HostRenderer.h"
#include "render/RenderHooks.h"
#include "render/SceneDepthCapture.h"

#include <RPS/Runtime/HookChain.h>
#include <intrin.h>

namespace rpsui::render::RenderHooks
{
    namespace
    {
        using SubmitStereoTexture = void (*)(ID3D11Texture2D* texture);

        std::mutex g_installMutex;
        std::atomic_bool g_installed = false;
        // What each submit call reached: the native submit or another renderer's
        // detour. The shared hook picks its call site's by its return address.
        SubmitStereoTexture g_fullCallee = nullptr;
        SubmitStereoTexture g_specialCallee = nullptr;
        std::uintptr_t g_fullReturn = 0;

        [[nodiscard]] bool IsExactRuntime() noexcept
        {
            return REL::Module::IsVR() &&
                   REL::Module::get().version() == F4SE::RUNTIME_VR_1_2_72;
        }

        [[nodiscard]] bool IsReadable(const void* address, std::size_t size) noexcept
        {
            if (!address || size == 0) {
                return false;
            }
            auto* cursor = static_cast<const std::byte*>(address);
            const auto* end = cursor + size;
            while (cursor < end) {
                MEMORY_BASIC_INFORMATION memory{};
                if (VirtualQuery(cursor, &memory, sizeof(memory)) != sizeof(memory) ||
                    memory.State != MEM_COMMIT ||
                    (memory.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0) {
                    return false;
                }
                const auto* regionEnd =
                    static_cast<const std::byte*>(memory.BaseAddress) + memory.RegionSize;
                if (regionEnd <= cursor) {
                    return false;
                }
                cursor = (std::min)(regionEnd, end);
            }
            return true;
        }

        template <std::size_t Size>
        [[nodiscard]] bool Matches(
            const void* address,
            const std::array<std::uint8_t, Size>& expected) noexcept
        {
            return IsReadable(address, expected.size()) &&
                   std::memcmp(address, expected.data(), expected.size()) == 0;
        }

        void RestoreCall(
            std::uintptr_t callsite,
            const std::array<std::uint8_t, 5>& bytes) noexcept
        {
            if (callsite == 0) {
                return;
            }
            try {
                REL::safe_write(callsite, bytes.data(), bytes.size());
            } catch (...) {
                log::critical("RPS UI Framework could not roll back a failed stereo-hook transaction");
            }
        }

        void HookSubmitStereoTexture(ID3D11Texture2D* texture) noexcept
        {
            class FrameEpoch final
            {
            public:
                ~FrameEpoch() noexcept
                {
                    SceneDepthCapture::AdvanceSubmittedFrame();
                }
            } frameEpoch;

            try {
                if (texture) {
                    RenderSubmittedTexture(texture);
                }
            } catch (...) {
                try {
                    log::error("RPS UI Framework contained an exception at the stereo submission boundary");
                } catch (...) {
                }
            }

            const auto through = reinterpret_cast<std::uintptr_t>(_ReturnAddress()) == g_fullReturn ? g_fullCallee :
                                                                                                   g_specialCallee;
            if (through) {
                through(texture);
            }
        }
    }

    InstallResult Install() noexcept
    {
        std::scoped_lock lock(g_installMutex);
        if (g_installed.load(std::memory_order_acquire)) {
            return InstallResult::Installed;
        }
        if (!IsExactRuntime()) {
            log::critical("RPS UI Framework stereo composition supports only Fallout 4 VR 1.2.72");
            return InstallResult::Unavailable;
        }

        using namespace EngineStereoSubmissionPolicy;
        std::uintptr_t fullCallsite = 0;
        std::uintptr_t specialCallsite = 0;
        bool fullPatched = false;
        bool specialPatched = false;
        // Each call site's bytes before RPS UI patched it, for rollback.
        std::array<std::uint8_t, 5> fullBefore = kFullSubmitOriginalCall;
        std::array<std::uint8_t, 5> specialBefore = kSpecialSubmitOriginalCall;
        try {
            const REL::Relocation<std::uintptr_t> fullCall{ REL::Offset(kFullSubmitCallsiteRva) };
            const REL::Relocation<std::uintptr_t> specialCall{ REL::Offset(kSpecialSubmitCallsiteRva) };
            const REL::Relocation<std::uintptr_t> submit{ REL::Offset(kSubmitStereoTextureRva) };
            fullCallsite = fullCall.address();
            specialCallsite = specialCall.address();
            const auto submitAddress = submit.address();

            // Another renderer that already hooked a submit call keeps it: RPS UI
            // composes first and calls through its detour.
            const auto full = RPS::Runtime::HookChain::resolveCall(fullCallsite, submitAddress);
            const auto special = RPS::Runtime::HookChain::resolveCall(specialCallsite, submitAddress);
            const bool guarded =
                fullCallsite >= kCallsitePrefixSize &&
                specialCallsite >= kCallsitePrefixSize &&
                Matches(reinterpret_cast<const void*>(fullCallsite - kCallsitePrefixSize), kFullSubmitBoundary) &&
                Matches(reinterpret_cast<const void*>(specialCallsite - kCallsitePrefixSize), kSpecialSubmitBoundary) &&
                RPS::Runtime::HookChain::resolveEntry(submitAddress, kSubmitStereoTexturePrologue) &&
                full && special;
            if (!guarded) {
                log::critical(
                    "RPS UI Framework stereo guards do not match FO4VR 1.2.72 and no other renderer's detour is "
                    "recognisable there");
                return InstallResult::Conflict;
            }
            std::memcpy(fullBefore.data(), reinterpret_cast<const void*>(fullCallsite), fullBefore.size());
            std::memcpy(specialBefore.data(), reinterpret_cast<const void*>(specialCallsite), specialBefore.size());

            g_fullCallee = reinterpret_cast<SubmitStereoTexture>(full.callee);
            g_specialCallee = reinterpret_cast<SubmitStereoTexture>(special.callee);
            g_fullReturn = fullCallsite + 5;
            auto& trampoline = F4SE::GetTrampoline();
            const auto fullOriginal = trampoline.write_call<5>(fullCallsite, HookSubmitStereoTexture);
            fullPatched = true;
            if (fullOriginal != full.callee) {
                RestoreCall(fullCallsite, fullBefore);
                fullPatched = false;
                g_fullCallee = g_specialCallee = nullptr;
                return InstallResult::Unavailable;
            }

            const auto specialOriginal = trampoline.write_call<5>(specialCallsite, HookSubmitStereoTexture);
            specialPatched = true;
            if (specialOriginal != special.callee) {
                RestoreCall(specialCallsite, specialBefore);
                specialPatched = false;
                RestoreCall(fullCallsite, fullBefore);
                fullPatched = false;
                g_fullCallee = g_specialCallee = nullptr;
                return InstallResult::Unavailable;
            }

            g_installed.store(true, std::memory_order_release);
            if (full.foreign() || special.foreign()) {
                log::info("RPS UI Framework installed guarded FO4VR stereo composition chained behind {}",
                    full.foreign() ? full.owner : special.owner);
            } else {
                log::info("RPS UI Framework installed guarded FO4VR stereo composition");
            }
            return InstallResult::Installed;
        } catch (...) {
            if (specialPatched) {
                RestoreCall(specialCallsite, specialBefore);
            }
            if (fullPatched) {
                RestoreCall(fullCallsite, fullBefore);
            }
            g_fullCallee = g_specialCallee = nullptr;
            log::critical("RPS UI Framework stereo-hook installation failed");
            return InstallResult::Unavailable;
        }
    }

    bool IsInstalled() noexcept
    {
        return g_installed.load(std::memory_order_acquire);
    }
}
