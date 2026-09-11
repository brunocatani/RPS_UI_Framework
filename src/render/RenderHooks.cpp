#include "PCH.h"

#include "Logger.h"

#include "render/EngineStereoSubmissionPolicy.h"
#include "render/HostRenderer.h"
#include "render/RenderHooks.h"
#include "render/SceneDepthCapture.h"

namespace rpsui::render::RenderHooks
{
    namespace
    {
        using SubmitStereoTexture = void (*)(ID3D11Texture2D* texture);

        std::mutex g_installMutex;
        std::atomic_bool g_installed = false;
        SubmitStereoTexture g_originalSubmit = nullptr;

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

        [[nodiscard]] bool CallTargets(
            std::uintptr_t callsite,
            std::uintptr_t target) noexcept
        {
            if (!IsReadable(reinterpret_cast<const void*>(callsite), 5)) {
                return false;
            }
            const auto* instruction = reinterpret_cast<const std::uint8_t*>(callsite);
            if (instruction[0] != 0xE8) {
                return false;
            }
            std::int32_t displacement = 0;
            std::memcpy(&displacement, instruction + 1, sizeof(displacement));
            return EngineStereoSubmissionPolicy::RelativeCallTarget(callsite, displacement) == target;
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

            if (g_originalSubmit) {
                g_originalSubmit(texture);
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
        try {
            const REL::Relocation<std::uintptr_t> fullCall{ REL::Offset(kFullSubmitCallsiteRva) };
            const REL::Relocation<std::uintptr_t> specialCall{ REL::Offset(kSpecialSubmitCallsiteRva) };
            const REL::Relocation<std::uintptr_t> submit{ REL::Offset(kSubmitStereoTextureRva) };
            fullCallsite = fullCall.address();
            specialCallsite = specialCall.address();
            const auto submitAddress = submit.address();

            const bool guarded =
                fullCallsite >= kCallsitePrefixSize &&
                specialCallsite >= kCallsitePrefixSize &&
                Matches(reinterpret_cast<const void*>(fullCallsite - kCallsitePrefixSize), kFullSubmitBoundary) &&
                Matches(reinterpret_cast<const void*>(specialCallsite - kCallsitePrefixSize), kSpecialSubmitBoundary) &&
                Matches(reinterpret_cast<const void*>(submitAddress), kSubmitStereoTexturePrologue) &&
                CallTargets(fullCallsite, submitAddress) &&
                CallTargets(specialCallsite, submitAddress);
            if (!guarded) {
                log::critical(
                    "RPS UI Framework stereo guards do not match the pristine FO4VR 1.2.72 callsites; "
                    "another renderer may already own them");
                return InstallResult::Conflict;
            }

            g_originalSubmit = reinterpret_cast<SubmitStereoTexture>(submitAddress);
            auto& trampoline = F4SE::GetTrampoline();
            const auto fullOriginal = trampoline.write_call<5>(fullCallsite, HookSubmitStereoTexture);
            fullPatched = true;
            if (fullOriginal != submitAddress) {
                RestoreCall(fullCallsite, kFullSubmitOriginalCall);
                fullPatched = false;
                g_originalSubmit = nullptr;
                return InstallResult::Unavailable;
            }

            const auto specialOriginal = trampoline.write_call<5>(specialCallsite, HookSubmitStereoTexture);
            specialPatched = true;
            if (specialOriginal != submitAddress) {
                RestoreCall(specialCallsite, kSpecialSubmitOriginalCall);
                specialPatched = false;
                RestoreCall(fullCallsite, kFullSubmitOriginalCall);
                fullPatched = false;
                g_originalSubmit = nullptr;
                return InstallResult::Unavailable;
            }

            g_installed.store(true, std::memory_order_release);
            log::info("RPS UI Framework installed exclusive guarded FO4VR stereo composition");
            return InstallResult::Installed;
        } catch (...) {
            if (specialPatched) {
                RestoreCall(specialCallsite, kSpecialSubmitOriginalCall);
            }
            if (fullPatched) {
                RestoreCall(fullCallsite, kFullSubmitOriginalCall);
            }
            g_originalSubmit = nullptr;
            log::critical("RPS UI Framework stereo-hook installation failed");
            return InstallResult::Unavailable;
        }
    }

    bool IsInstalled() noexcept
    {
        return g_installed.load(std::memory_order_acquire);
    }
}
