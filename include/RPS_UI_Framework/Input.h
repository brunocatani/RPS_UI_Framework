#pragma once
#include "Abi.h"

// Value-only game-thread input. Physical hands are left=0, right=1. No skeleton
// provider is required. Callbacks must not block or retain the borrowed frame.
#pragma pack(push, 8)
namespace rpsui::api::input
{
    inline constexpr InterfaceId kInterfaceId = InterfaceId::Input;
    inline constexpr std::uint32_t kMajor = 2;
    inline constexpr std::uint32_t kMinor = 0;

    struct HandInput
    {
        float position[3]{};
        float forward[3]{};
        std::uint64_t pressed{};
        float stick[2]{};
        std::uint8_t valid{};
        std::uint8_t reservedBytes[7]{};
    };

    struct Frame
    {
        std::uint32_t size{ sizeof(Frame) };
        std::uint32_t version{ kRecordVersion };
        std::uint64_t sequence{};
        double seconds{};
        HandInput hands[2]{};
        std::uint8_t ready{}, leftHanded{};
        std::uint8_t reservedBytes[6]{};
    };

    using Callback = void(RPSUI_CALL*)(const Frame* frame, void* context) noexcept;

    struct Capture
    {
        std::uint32_t size{ sizeof(Capture) };
        std::uint32_t version{ kRecordVersion };
        // Capture is game-callback-thread only; renew every callback. Expires after
        // three callbacks. Supported buttons: B/Menu (1), Grip (2), A (7),
        // thumbstick click (32), Trigger (33).
        std::uint64_t buttons[2]{};
        // An idle chord reserves its members only while the complete chord is down.
        // Once captured, publish buttons until all chord members have been released.
        std::uint64_t chord[2]{};
    };

    struct PointerAim
    {
        std::uint32_t size{ sizeof(PointerAim) };
        std::uint32_t version{ kRecordVersion };
        // Physical hands: left=0, right=1. Degrees relative to native controller
        // forward: positive pitch aims up, positive yaw aims right.
        // Pitch range [-180,180] permits consumer forward calibration plus adjustment;
        // yaw range [-90,90].
        float pitchDegrees[2]{}, yawDegrees[2]{};
    };

    struct Api
    {
        static constexpr InterfaceId interfaceId = kInterfaceId;
        static constexpr std::uint32_t majorVersion = kMajor;
        static constexpr std::uint32_t minorVersion = kMinor;

        std::uint64_t(RPSUI_CALL* subscribe)(Callback callback, void* context) noexcept {};
        // False means a callback is in flight; caller retains callback/context and retries.
        bool(RPSUI_CALL* unsubscribe)(std::uint64_t token) noexcept {};
        bool(RPSUI_CALL* capture)(std::uint64_t token, const Capture* request) noexcept {};
        // Optional cooperation for other input hooks. True only on the calling
        // thread while the UI samples physical OpenVR state; leave that read raw.
        bool(RPSUI_CALL* rawInputReadActive)() noexcept {};
        // Read-only, lock-free query from any input thread. Physical hand: 0=left,
        // 1=right. Supply current physical button levels to resolve pending chords.
        // Returns zero outside gameplay or after capture has been released/expired.
        std::uint64_t(RPSUI_CALL* capturedButtons)(unsigned hand, std::uint64_t leftPressed, std::uint64_t rightPressed) noexcept {};
        // Game thread only. Sets shared UI aim; the settings host (PALM) owns
        // persistence. Changes apply on the next input frame to every UI panel.
        bool(RPSUI_CALL* setPointerAim)(const PointerAim* aim) noexcept {};
    };

    static_assert(std::is_standard_layout_v<HandInput> && sizeof(HandInput) == 48);
    static_assert(std::is_standard_layout_v<Frame> && sizeof(Frame) == 128);
    static_assert(std::is_standard_layout_v<Capture> && sizeof(Capture) == 40);
    static_assert(std::is_standard_layout_v<PointerAim> && sizeof(PointerAim) == 24);
    static_assert(std::is_standard_layout_v<Api> && sizeof(Api) == 48);
}
#pragma pack(pop)
