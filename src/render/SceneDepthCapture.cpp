#include "PCH.h"

#include "Logger.h"

#include "render/SceneDepthCapture.h"
#include "render/NativeSceneTargets.h"
#include "render/SceneDepthSelector.h"

#include <array>

namespace rpsui::render::SceneDepthCapture
{
    namespace
    {
        constexpr std::uintptr_t kCommitGraphicsStateRva =
            0x1D9B5D0;
        constexpr std::uintptr_t kCaptureCallsiteRva =
            0x1D8E84A;

        constexpr std::array<std::uint8_t, 5>
            kOriginalCall{
                0xE8, 0x81, 0xCD, 0x00, 0x00
            };
        constexpr std::array<std::uint8_t, 19>
            kCommitPrologue{
                0x88, 0x54, 0x24, 0x10,
                0x88, 0x4C, 0x24, 0x08,
                0x55, 0x56, 0x57, 0x41, 0x56, 0x41, 0x57,
                0x48, 0x83, 0xEC, 0x50
            };
        constexpr std::array<std::uint8_t, 13>
            kCallsiteBoundary{
                0x33, 0xD2, 0x33, 0xC9,
                0xE8, 0x81, 0xCD, 0x00, 0x00,
                0x80, 0x7B, 0x2A, 0x00
            };

        using CommitGraphicsState =
            void (*)(std::uint8_t, std::uint8_t);

        struct Capture
        {
            Microsoft::WRL::ComPtr<ID3D11DepthStencilView>
                originalView;
            Microsoft::WRL::ComPtr<ID3D11DepthStencilView>
                readOnlyView;
            Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
            D3D11_TEXTURE2D_DESC textureDescription{};
            D3D11_DEPTH_STENCIL_VIEW_DESC viewDescription{};
            D3D11_COMPARISON_FUNC sourceComparison = D3D11_COMPARISON_NEVER;
            std::uint64_t frameEpoch = 0;
            std::uint32_t logicalDepth = 0;
            D3D11_DEPTH_WRITE_MASK writeMask = D3D11_DEPTH_WRITE_MASK_ZERO;
            bool stencilEnabled = false;
            std::optional<CaptureDiagnostic> diagnostic;
        };

        std::mutex g_lifecycleMutex;
        std::mutex g_captureMutex;
        std::atomic<bool> g_installed = false;
        std::atomic<bool> g_requested = false;
        std::atomic<std::uint64_t> g_frameEpoch = 1;
        std::atomic<std::uint64_t> g_capturedEpoch = 0;
        enum class CaptureStage : std::size_t
        {
            Attempt, AlreadyCaptured, NoContext, NoDepthView,
            NoDepthTexture, NativeTargetsUnavailable, NonSceneDepth,
            NoDepthState, DepthDisabled, ReadOnlyViewFailed, EpochChanged,
            Captured, Count
        };
        // Hook and submit callbacks may run on different threads. Counters are
        // relaxed diagnostics only; capture ownership and validity are unchanged.
        std::array<std::atomic<std::uint64_t>, static_cast<std::size_t>(CaptureStage::Count)> g_stageCounts{};
        std::atomic<HRESULT> g_readOnlyViewError{ S_OK };
        std::atomic<bool> g_reportedDepthMatch = false;
        std::atomic<const char*> g_nativeFailureStage{ "none" };
        std::uintptr_t g_callsite = 0;
        CommitGraphicsState g_original = nullptr;
        using SceneSelector = std::uint8_t (*)(void*);
        SceneSelector g_sceneSelector = nullptr;
        bool g_trueScopesSelector = false;
        Capture g_capture;
        // Protected by g_captureMutex. Sampling reads state only; it never
        // changes depth selection or supplies a projection to the renderer.
        std::chrono::steady_clock::time_point g_nextDiagnostic{};

        template <std::size_t Size>
        [[nodiscard]] bool MatchesReadable(
            const char* name,
            const void* address,
            const std::array<std::uint8_t, Size>& expected) noexcept
        {
            // Installation only: snapshot guarded bytes safely and report every
            // failed boundary without dereferencing unreadable game memory.
            MEMORY_BASIC_INFORMATION memory{};
            std::array<std::uint8_t, Size> actual{};
            SIZE_T copied = 0;
            DWORD error = ERROR_SUCCESS;
            const char* reason = "null address";
            const auto start = reinterpret_cast<std::uintptr_t>(address);
            if (address) {
                if (VirtualQuery(address, &memory, sizeof(memory)) != sizeof(memory)) {
                    error = GetLastError();
                    reason = "VirtualQuery failed";
                } else if (memory.State != MEM_COMMIT ||
                           (memory.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0) {
                    reason = "memory unavailable";
                } else {
                    const auto regionStart = reinterpret_cast<std::uintptr_t>(memory.BaseAddress);
                    if (start < regionStart || start - regionStart > memory.RegionSize ||
                        memory.RegionSize - (start - regionStart) < Size) {
                        reason = "guard crosses memory region";
                    } else {
                        const auto read = ReadProcessMemory(GetCurrentProcess(), address,
                            actual.data(), actual.size(), &copied);
                        error = read ? ERROR_SUCCESS : GetLastError();
                        if (read && copied == Size && actual == expected) {
                            return true;
                        }
                        reason = !read ? "ReadProcessMemory failed" :
                            copied != Size ? "incomplete read" : "byte mismatch";
                    }
                }
            }
            const auto hex = [](const auto& bytes, std::size_t count) {
                constexpr char digits[] = "0123456789ABCDEF";
                std::array<char, Size * 3 + 1> text{};
                for (std::size_t i = 0; i < count; ++i) {
                    text[i * 3] = digits[bytes[i] >> 4];
                    text[i * 3 + 1] = digits[bytes[i] & 0x0F];
                    text[i * 3 + 2] = i + 1 < count ? ' ' : '\0';
                }
                return text;
            };
            const auto expectedText = hex(expected, Size);
            const auto actualText = hex(actual, (std::min)(copied, actual.size()));
            log::critical(
                "RPS UI scene-depth guard '{}' failed: RVA=0x{:X}, reason={}, "
                "expected=[{}], actual=[{}], read={}/{}, Win32 error={}, state=0x{:X}, protection=0x{:X}",
                name, start - REL::Module::get().base(), reason, expectedText.data(),
                copied ? actualText.data() : "unavailable", copied, Size, error, memory.State, memory.Protect);
            return false;
        }

        [[nodiscard]] Microsoft::WRL::ComPtr<IUnknown>
            Identity(IUnknown* value) noexcept
        {
            Microsoft::WRL::ComPtr<IUnknown> result;
            if (value) {
                (void)value->QueryInterface(
                    IID_PPV_ARGS(result.GetAddressOf()));
            }
            return result;
        }

        [[nodiscard]] bool SameIdentity(
            IUnknown* left,
            IUnknown* right) noexcept
        {
            const auto leftIdentity = Identity(left);
            const auto rightIdentity = Identity(right);
            return leftIdentity &&
                   rightIdentity &&
                   leftIdentity.Get() ==
                       rightIdentity.Get();
        }

        bool ReadNative(std::uintptr_t address, void* destination, std::size_t size) noexcept
        {
            SIZE_T copied = 0;
            return ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(address),
                destination, size, &copied) && copied == size;
        }

        bool IsTrueScopesCode(std::uintptr_t address) noexcept
        {
            const auto module = GetModuleHandleW(L"truescopes_vr.dll");
            HMODULE owner{};
            MEMORY_BASIC_INFORMATION memory{};
            return module && GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCWSTR>(address), &owner) &&
                owner == module && VirtualQuery(reinterpret_cast<void*>(address), &memory, sizeof(memory)) == sizeof(memory) &&
                memory.State == MEM_COMMIT && !(memory.Protect & (PAGE_GUARD | PAGE_NOACCESS)) &&
                (memory.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY));
        }

        bool ReadSceneSelector(std::uint8_t& value) noexcept
        {
            if (!g_sceneSelector) return false;
            // The selected code belongs to the game or a validated loaded
            // plugin. Neither is unloaded during the framework's lifetime.
            __try {
                value = g_sceneSelector(reinterpret_cast<void*>(REL::Module::get().base() + scene_depth_selector::kRendererRva));
                return scene_depth_selector::isMainView(g_trueScopesSelector, value);
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                return false;
            }
        }

        bool ValidateNativeSceneLayout() noexcept
        {
            const auto base = REL::Module::get().base();
            bool valid = true;
            const auto matches = [base, &valid](const char* name, std::uintptr_t rva, const auto& bytes) {
                if (!MatchesReadable(name, reinterpret_cast<const void*>(base + rva), bytes)) valid = false;
            };
            // Guard the exact map readers, scene-depth selector, data-array
            // extents and renderer-data root before any new native pointer walk.
            matches("color target map", 0x1DBB4D0, std::array<std::uint8_t,11>{
                0x48,0x63,0xC2,0x8B,0x84,0x81,0xBC,0x13,0x00,0x00,0xC3});
            matches("depth target map", 0x1DBB4F0, std::array<std::uint8_t,11>{
                0x48,0x63,0xC2,0x8B,0x84,0x81,0xFC,0x15,0x00,0x00,0xC3});
            const auto selector = scene_depth_selector::resolve(base + scene_depth_selector::kFunctionRva,
                ReadNative, IsTrueScopesCode);
            if (!selector.target) {
                log::critical("RPS UI scene-depth selector rejected: stage={}; expected native reader or True Scopes-owned detour", selector.stage);
                valid = false;
            }
            matches("scene depth slot", 0x291B30F, std::array<std::uint8_t,52>{
                0x48,0x8D,0x0D,0x2A,0xE0,0x91,0x03,0xE8,0xB5,0x94,0x47,0xFF,0x41,0xB8,0x01,0x00,
                0x00,0x00,0x48,0x8D,0x4D,0xC0,0x84,0xC0,0xB8,0x0C,0x00,0x00,0x00,0x89,0x74,0x24,
                0x28,0x44,0x0F,0x45,0xC0,0x8D,0x50,0xFB,0x45,0x33,0xC9,0x89,0x74,0x24,0x20,0xE8,
                0x7D,0x48,0x01,0x00});
            matches("target array extents", 0x1DA2A49, std::array<std::uint8_t,40>{
                0x48,0x8D,0x8B,0x58,0x0A,0x00,0x00,0x33,0xD2,0x41,0xB8,0x30,0x1B,0x00,0x00,0xE8,
                0xC9,0xEB,0xBE,0x00,0x48,0x8D,0x8B,0x88,0x25,0x00,0x00,0x33,0xD2,0x41,0xB8,0xB0,
                0x0A,0x00,0x00,0xE8,0xB5,0xEB,0xBE,0x00});
            matches("renderer data root", 0x2B23EE6, std::array<std::uint8_t,29>{
                0x48,0x8D,0x1D,0x63,0x54,0x71,0x03,0x48,0x8B,0xCB,0xE8,0xEB,0xEA,0x27,0xFF,0x48,
                0x8B,0x05,0xCC,0x1B,0x71,0x03,0x48,0x89,0x1D,0xE5,0xFD,0x5C,0x03});
            if (valid) {
                g_sceneSelector = reinterpret_cast<SceneSelector>(selector.target);
                g_trueScopesSelector = selector.trueScopes;
                log::info("RPS UI scene-depth selector: {} target=0x{:X}; engine selector used for capture and submission", selector.stage, selector.target);
            }
            return valid;
        }

        void Count(CaptureStage stage) noexcept
        {
            g_stageCounts[static_cast<std::size_t>(stage)].fetch_add(1, std::memory_order_relaxed);
        }

        [[nodiscard]] bool DepthExtent(
            const D3D11_TEXTURE2D_DESC& texture,
            const D3D11_DEPTH_STENCIL_VIEW_DESC& view,
            std::uint32_t& width,
            std::uint32_t& height) noexcept
        {
            std::uint32_t mip = 0;
            switch (view.ViewDimension) {
            case D3D11_DSV_DIMENSION_TEXTURE2D:
                mip = view.Texture2D.MipSlice;
                break;
            case D3D11_DSV_DIMENSION_TEXTURE2DARRAY:
                if (view.Texture2DArray.ArraySize != 1) {
                    return false;
                }
                mip = view.Texture2DArray.MipSlice;
                break;
            case D3D11_DSV_DIMENSION_TEXTURE2DMS:
                break;
            case D3D11_DSV_DIMENSION_TEXTURE2DMSARRAY:
                if (view.Texture2DMSArray.ArraySize != 1) {
                    return false;
                }
                break;
            default:
                return false;
            }
            if (mip >= texture.MipLevels || mip >= 32) {
                return false;
            }
            width = (std::max)(1u, texture.Width >> mip);
            height = (std::max)(1u, texture.Height >> mip);
            return true;
        }

        [[nodiscard]] bool LayoutMatches(
            const D3D11_TEXTURE2D_DESC& depthTexture,
            const D3D11_DEPTH_STENCIL_VIEW_DESC& depthView,
            const D3D11_TEXTURE2D_DESC& color) noexcept
        {
            std::uint32_t width = 0;
            std::uint32_t height = 0;
            return DepthExtent(
                       depthTexture,
                       depthView,
                       width,
                       height) &&
                   width == color.Width &&
                   height == color.Height &&
                   depthTexture.SampleDesc.Count ==
                       color.SampleDesc.Count &&
                   depthTexture.SampleDesc.Quality ==
                       color.SampleDesc.Quality;
        }

        [[nodiscard]] Microsoft::WRL::ComPtr<
            ID3D11DepthStencilView>
            MakeReadOnlyView(
                ID3D11Texture2D* texture,
                D3D11_DEPTH_STENCIL_VIEW_DESC description) noexcept
        {
            Microsoft::WRL::ComPtr<ID3D11DepthStencilView> view;
            if (!texture) {
                return view;
            }
            Microsoft::WRL::ComPtr<ID3D11Device> device;
            texture->GetDevice(device.GetAddressOf());
            if (!device) {
                return view;
            }

            description.Flags |=
                D3D11_DSV_READ_ONLY_DEPTH;
            if (description.Format ==
                    DXGI_FORMAT_D24_UNORM_S8_UINT ||
                description.Format ==
                    DXGI_FORMAT_D32_FLOAT_S8X24_UINT) {
                description.Flags |=
                    D3D11_DSV_READ_ONLY_STENCIL;
            }
            const auto result = device->CreateDepthStencilView(
                    texture,
                    &description,
                    view.GetAddressOf());
            if (FAILED(result)) {
                g_readOnlyViewError.store(result, std::memory_order_relaxed);
                view.Reset();
            }
            return view;
        }

        void RestoreOriginalCall() noexcept
        {
            if (g_callsite == 0) {
                return;
            }
            try {
                REL::safe_write(
                    g_callsite,
                    kOriginalCall.data(),
                    kOriginalCall.size());
            } catch (...) {
                log::critical(
                    "RPS UI Framework could not roll back a failed scene-depth hook transaction");
            }
        }

        void CaptureBoundDepth() noexcept
        {
            if (!g_requested.load(std::memory_order_acquire)) {
                return;
            }
            Count(CaptureStage::Attempt);
            const auto epoch = g_frameEpoch.load(
                std::memory_order_acquire);
            if (g_capturedEpoch.load(
                    std::memory_order_acquire) == epoch) {
                Count(CaptureStage::AlreadyCaptured);
                return;
            }

            const auto rendererData =
                RE::BSGraphics::RendererData::GetSingleton();
            const auto context = rendererData ?
                reinterpret_cast<ID3D11DeviceContext*>(
                    rendererData->context) :
                nullptr;
            if (!context) {
                Count(CaptureStage::NoContext);
                return;
            }

            Microsoft::WRL::ComPtr<ID3D11DepthStencilView>
                depthView;
            context->OMGetRenderTargets(
                0,
                nullptr,
                depthView.GetAddressOf());
            if (!depthView) {
                Count(CaptureStage::NoDepthView);
                return;
            }

            Microsoft::WRL::ComPtr<ID3D11Resource>
                depthResource;
            depthView->GetResource(
                depthResource.GetAddressOf());
            Microsoft::WRL::ComPtr<ID3D11Texture2D>
                depthTexture;
            if (!depthResource ||
                FAILED(depthResource.As(&depthTexture)) ||
                !depthTexture) {
                Count(CaptureStage::NoDepthTexture);
                return;
            }

            native_scene_targets::Snapshot scene;
            if (!native_scene_targets::readDepth(REL::Module::get().base(), ReadNative, ReadSceneSelector, scene)) {
                Count(CaptureStage::NativeTargetsUnavailable);
                g_nativeFailureStage.store(scene.stage, std::memory_order_relaxed);
                return;
            }
            // The final VR color target may never be bound with depth (SteamVR).
            // Select by the engine's scene-depth role, not color identity or size.
            if (reinterpret_cast<std::uintptr_t>(depthTexture.Get()) != scene.depthTexture) {
                Count(CaptureStage::NonSceneDepth);
                return;
            }
            D3D11_TEXTURE2D_DESC depthDescription{};
            D3D11_DEPTH_STENCIL_VIEW_DESC viewDescription{};
            depthTexture->GetDesc(&depthDescription);
            depthView->GetDesc(&viewDescription);
            Microsoft::WRL::ComPtr<ID3D11DepthStencilState>
                depthState;
            UINT stencilReference = 0;
            context->OMGetDepthStencilState(
                depthState.GetAddressOf(),
                &stencilReference);
            if (!depthState) {
                Count(CaptureStage::NoDepthState);
                return;
            }
            D3D11_DEPTH_STENCIL_DESC stateDescription{};
            depthState->GetDesc(&stateDescription);
            const auto comparison = stateDescription.DepthFunc;
            const auto usableComparison =
                comparison == D3D11_COMPARISON_LESS ||
                comparison == D3D11_COMPARISON_LESS_EQUAL ||
                comparison == D3D11_COMPARISON_GREATER ||
                comparison == D3D11_COMPARISON_GREATER_EQUAL;
            if (stateDescription.DepthEnable == FALSE ||
                !usableComparison) {
                Count(CaptureStage::DepthDisabled);
                return;
            }

            Microsoft::WRL::ComPtr<ID3D11DepthStencilView>
                readOnly;
            bool sampleDiagnostic = false;
            {
                std::lock_guard lock(g_captureMutex);
                if (g_capture.originalView.Get() ==
                    depthView.Get()) {
                    readOnly = g_capture.readOnlyView;
                }
                const auto now = std::chrono::steady_clock::now();
                if (now >= g_nextDiagnostic) {
                    sampleDiagnostic = true;
                    g_nextDiagnostic = now + std::chrono::seconds(1);
                }
            }
            if (!readOnly) {
                readOnly = MakeReadOnlyView(
                    depthTexture.Get(),
                    viewDescription);
            }
            if (!readOnly) {
                Count(CaptureStage::ReadOnlyViewFailed);
                return;
            }

            std::optional<CaptureDiagnostic> diagnostic;
            if (sampleDiagnostic) {
                auto& sample = diagnostic.emplace();
                sample.projectionValid = StereoProjection::ReadSnapshot(
                    sample.projection, sample.projectionFailure);
                sample.viewportCount = static_cast<UINT>(sample.viewports.size());
                context->RSGetViewports(&sample.viewportCount, sample.viewports.data());
                Microsoft::WRL::ComPtr<ID3D11VertexShader> vertexShader;
                context->VSGetShader(vertexShader.GetAddressOf(), nullptr, nullptr);
                sample.vertexShader = reinterpret_cast<std::uintptr_t>(vertexShader.Get());
                Microsoft::WRL::ComPtr<ID3D11RenderTargetView> colorView;
                context->OMGetRenderTargets(1, colorView.GetAddressOf(), nullptr);
                if (colorView) {
                    Microsoft::WRL::ComPtr<ID3D11Resource> colorResource;
                    colorView->GetResource(colorResource.GetAddressOf());
                    Microsoft::WRL::ComPtr<ID3D11Texture2D> colorTexture;
                    if (colorResource && SUCCEEDED(colorResource.As(&colorTexture)) && colorTexture) {
                        sample.colorTexture = reinterpret_cast<std::uintptr_t>(colorTexture.Get());
                        colorTexture->GetDesc(&sample.colorDescription);
                    }
                }
            }

            {
                std::lock_guard lock(g_captureMutex);
                if (!g_requested.load(
                        std::memory_order_acquire) ||
                    g_frameEpoch.load(
                        std::memory_order_acquire) != epoch) {
                    Count(CaptureStage::EpochChanged);
                    return;
                }
                g_capture.originalView = depthView;
                g_capture.readOnlyView = readOnly;
                g_capture.texture = depthTexture;
                g_capture.textureDescription =
                    depthDescription;
                g_capture.viewDescription =
                    viewDescription;
                g_capture.sourceComparison = comparison;
                g_capture.frameEpoch = epoch;
                g_capture.logicalDepth = scene.logicalDepth;
                g_capture.writeMask = stateDescription.DepthWriteMask;
                g_capture.stencilEnabled = stateDescription.StencilEnable != FALSE;
                g_capture.diagnostic = diagnostic;
            }
            g_capturedEpoch.store(
                epoch,
                std::memory_order_release);
            Count(CaptureStage::Captured);
        }

        __declspec(noinline) void HookCommit(
            std::uint8_t firstMode,
            std::uint8_t secondMode) noexcept
        {
            if (g_original) {
                g_original(firstMode, secondMode);
            }
            CaptureBoundDepth();
        }
    }

    bool Install() noexcept
    {
        std::lock_guard lock(g_lifecycleMutex);
        if (g_installed.load(std::memory_order_acquire)) {
            return true;
        }
        if (!REL::Module::IsVR() ||
            REL::Module::get().version() !=
                F4SE::RUNTIME_VR_1_2_72) {
            return false;
        }

        bool patched = false;
        try {
            if (!ValidateNativeSceneLayout()) {
                log::critical("RPS UI native scene-depth layout byte guards failed; capture disabled");
                return false;
            }
            const REL::Relocation<std::uintptr_t> callsite{
                REL::Offset(kCaptureCallsiteRva)
            };
            const REL::Relocation<std::uintptr_t> target{
                REL::Offset(kCommitGraphicsStateRva)
            };
            g_callsite = callsite.address();
            const bool targetMatches = MatchesReadable(
                    "graphics state commit",
                    reinterpret_cast<const void*>(
                        target.address()),
                    kCommitPrologue);
            const bool callsiteMatches = g_callsite >= 4 && MatchesReadable(
                    "depth capture callsite",
                    reinterpret_cast<const void*>(
                        g_callsite - 4),
                    kCallsiteBoundary);
            if (!targetMatches || !callsiteMatches) {
                log::error(
                    "RPS UI Framework scene-depth callsite failed its FO4VR 1.2.72 byte guard");
                g_callsite = 0;
                return false;
            }

            g_original = reinterpret_cast<
                CommitGraphicsState>(target.address());
            const auto original =
                F4SE::GetTrampoline().write_call<5>(
                    g_callsite,
                    HookCommit);
            patched = true;
            if (original != target.address()) {
                RestoreOriginalCall();
                patched = false;
                g_callsite = 0;
                g_original = nullptr;
                return false;
            }

            g_installed.store(true, std::memory_order_release);
            log::info(
                "RPS UI Framework scene-depth capture installed for FO4VR 1.2.72");
            return true;
        } catch (...) {
            if (patched && g_callsite != 0) {
                RestoreOriginalCall();
            }
            g_callsite = 0;
            g_original = nullptr;
            return false;
        }
    }

    bool IsInstalled() noexcept
    {
        return g_installed.load(std::memory_order_acquire);
    }

    void Uninstall() noexcept
    {
        // F4SE plugins are process-lifetime modules. Rewriting a hot callsite
        // during shutdown would be less safe than leaving the guarded wrapper
        // resident; disabling capture makes it a single atomic branch.
        Reset();
    }

    void SetCaptureRequested(bool requested) noexcept
    {
        const auto previous = g_requested.exchange(
            requested,
            std::memory_order_acq_rel);
        if (previous && !requested) {
            g_capturedEpoch.store(
                0,
                std::memory_order_release);
            std::lock_guard lock(g_captureMutex);
            g_capture = {};
        }
    }

    FrameDepth AcquireForSubmittedTarget(
        ID3D11Texture2D* colorTexture,
        const D3D11_TEXTURE2D_DESC& colorDescription) noexcept
    {
        FrameDepth result;
        if (!g_requested.load(std::memory_order_acquire) ||
            !colorTexture) {
            return result;
        }

        Capture snapshot;
        {
            std::lock_guard lock(g_captureMutex);
            snapshot = g_capture;
        }
        result.frameEpoch = snapshot.frameEpoch;
        result.submittedTexture = reinterpret_cast<std::uintptr_t>(colorTexture);
        result.capturedDepth = reinterpret_cast<std::uintptr_t>(snapshot.texture.Get());
        result.requestedFrameEpoch = g_frameEpoch.load(std::memory_order_acquire);
        if (!snapshot.readOnlyView ||
            !snapshot.texture) {
            result.failureReason = "no-capture";
            return result;
        }
        if (snapshot.frameEpoch != result.requestedFrameEpoch) {
            result.failureReason = "stale-capture";
            return result;
        }

        native_scene_targets::Snapshot scene;
        const auto base = REL::Module::get().base();
        if (!native_scene_targets::readDepth(base, ReadNative, ReadSceneSelector, scene) ||
            !native_scene_targets::readColor(base, ReadNative, scene)) {
            result.failureReason = scene.stage;
            return result;
        }
        result.expectedColor = scene.colorTexture;
        result.expectedDepth = scene.depthTexture;
        result.logicalDepth = scene.logicalDepth;
        if (result.submittedTexture != scene.colorTexture) {
            result.failureReason = "not-native-vr-color";
            return result;
        }
        if (result.capturedDepth != scene.depthTexture || snapshot.logicalDepth != scene.logicalDepth) {
            result.failureReason = "scene-depth-changed";
            return result;
        }
        if (!LayoutMatches(
                snapshot.textureDescription,
                snapshot.viewDescription,
                colorDescription)) {
            result.failureReason = "submitted-layout-mismatch";
            return result;
        }

        Microsoft::WRL::ComPtr<ID3D11Device> colorDevice;
        Microsoft::WRL::ComPtr<ID3D11Device> depthDevice;
        colorTexture->GetDevice(colorDevice.GetAddressOf());
        snapshot.texture->GetDevice(
            depthDevice.GetAddressOf());
        if (!SameIdentity(
                colorDevice.Get(),
                depthDevice.Get())) {
            result.failureReason = "different-device";
            return result;
        }

        result.view = snapshot.readOnlyView;
        result.sourceComparison = snapshot.sourceComparison;
        result.frameEpoch = snapshot.frameEpoch;
        result.failureReason = nullptr;
        result.sourceWriteMask = snapshot.writeMask;
        result.sourceViewFlags = snapshot.viewDescription.Flags;
        result.sourceStencilEnabled = snapshot.stencilEnabled;
        result.diagnostic = snapshot.diagnostic;
        if (!g_reportedDepthMatch.exchange(true, std::memory_order_relaxed)) {
            log::info("RPS UI first depth match: epoch={} color=0x{:X} sceneDepthLogical={} depth=0x{:X} submitted={}x{} format={} samples={}/{} "
                "depth={}x{} format={} samples={}/{} viewDimension={} sourceDepthFunc={}",
                result.frameEpoch, result.submittedTexture, scene.logicalDepth, result.capturedDepth,
                colorDescription.Width, colorDescription.Height, static_cast<unsigned>(colorDescription.Format),
                colorDescription.SampleDesc.Count, colorDescription.SampleDesc.Quality,
                snapshot.textureDescription.Width, snapshot.textureDescription.Height,
                static_cast<unsigned>(snapshot.textureDescription.Format), snapshot.textureDescription.SampleDesc.Count,
                snapshot.textureDescription.SampleDesc.Quality, static_cast<unsigned>(snapshot.viewDescription.ViewDimension),
                static_cast<unsigned>(snapshot.sourceComparison));
        }
        return result;
    }

    void ReportUnavailable(const FrameDepth& depth,
        const D3D11_TEXTURE2D_DESC& submittedDescription) noexcept
    {
        std::array<std::uint64_t, static_cast<std::size_t>(CaptureStage::Count)> counts{};
        for (std::size_t i = 0; i < counts.size(); ++i)
            counts[i] = g_stageCounts[i].load(std::memory_order_relaxed);
        const auto count = [&](CaptureStage stage) { return counts[static_cast<std::size_t>(stage)]; };
        log::warn("RPS UI panels withheld: scene depth unavailable reason={} submitEpoch={} captureEpoch={} "
            "submittedColor=0x{:X} submitted={}x{} format={} samples={}/{}",
            depth.failureReason, depth.requestedFrameEpoch, depth.frameEpoch,
            depth.submittedTexture,
            submittedDescription.Width, submittedDescription.Height, static_cast<unsigned>(submittedDescription.Format),
            submittedDescription.SampleDesc.Count, submittedDescription.SampleDesc.Quality);
        log::warn("RPS UI depth capture totals: attempts={} alreadyCaptured={} noContext={} "
            "noDepthView={} noDepthTexture={} nativeTargetsUnavailable={} nonSceneDepth={} noDepthState={} "
            "depthDisabled={} readOnlyViewFailed={} epochChanged={} captured={} readOnlyHRESULT=0x{:08X}",
            count(CaptureStage::Attempt), count(CaptureStage::AlreadyCaptured), count(CaptureStage::NoContext),
            count(CaptureStage::NoDepthView), count(CaptureStage::NoDepthTexture),
            count(CaptureStage::NativeTargetsUnavailable), count(CaptureStage::NonSceneDepth),
            count(CaptureStage::NoDepthState), count(CaptureStage::DepthDisabled), count(CaptureStage::ReadOnlyViewFailed),
            count(CaptureStage::EpochChanged), count(CaptureStage::Captured),
            static_cast<std::uint32_t>(g_readOnlyViewError.load(std::memory_order_relaxed)));
        log::warn("RPS UI native scene depth: captureStage={} logical={} captured=0x{:X} expected=0x{:X} expectedColor=0x{:X}",
            g_nativeFailureStage.load(std::memory_order_relaxed), depth.logicalDepth,
            depth.capturedDepth, depth.expectedDepth, depth.expectedColor);
    }

    void AdvanceSubmittedFrame() noexcept
    {
        g_frameEpoch.fetch_add(1, std::memory_order_acq_rel);
    }

    void Reset() noexcept
    {
        g_requested.store(false, std::memory_order_release);
        g_frameEpoch.fetch_add(1, std::memory_order_acq_rel);
        g_capturedEpoch.store(0, std::memory_order_release);
        std::lock_guard lock(g_captureMutex);
        g_capture = {};
        g_nextDiagnostic = {};
    }
}
