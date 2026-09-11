#pragma once
#include "RPSUIFrameworkApi.h"

namespace rpsui::sdk {
// Value-only game-thread input. Physical hands are left=0, right=1. No skeleton
// provider is required. Callbacks must not block or retain the borrowed frame.
struct HandInputV1 {
 float position[3]{};
 float forward[3]{};
 std::uint64_t pressed{};
 float stick[2]{};
 bool valid{};
};
struct InputFrameV1 {
 std::uint32_t structSize{sizeof(InputFrameV1)};
 std::uint32_t version{1};
 std::uint64_t sequence{};
 double seconds{};
 HandInputV1 hands[2]{};
 bool ready{},leftHanded{};
};
using InputCallbackV1=void(RPSUI_CALL*)(const InputFrameV1*,void*) noexcept;
struct InputCaptureV1 {
 std::uint32_t structSize{sizeof(InputCaptureV1)};
 // Capture is game-callback-thread only; renew every callback. Expires after
 // three callbacks. Supported buttons: B/Menu (1), Grip (2), A (7),
 // thumbstick click (32), Trigger (33).
 std::uint64_t buttons[2]{};
 // An idle chord reserves its members only while the complete chord is down.
 // Once captured, publish buttons until all chord members have been released.
 std::uint64_t chord[2]{};
};
struct InputApiV1 {
 std::uint32_t structSize{sizeof(InputApiV1)},version{1};
 std::uint64_t(RPSUI_CALL* subscribe)(InputCallbackV1,void*) noexcept{};
 // False means a callback is in flight; caller retains callback/context and retries.
 bool(RPSUI_CALL* unsubscribe)(std::uint64_t) noexcept{};
 bool(RPSUI_CALL* capture)(std::uint64_t,const InputCaptureV1*) noexcept{};
 // Optional cooperation for other input hooks. True only on the calling
 // thread while the UI samples physical OpenVR state; leave that read raw.
 bool(RPSUI_CALL* rawInputReadActive)() noexcept{};
 // Read-only, lock-free query from any input thread. Physical hand: 0=left,
 // 1=right. Supply current physical button levels to resolve pending chords.
 // Returns zero outside gameplay or after capture has been released/expired.
 std::uint64_t(RPSUI_CALL* capturedButtons)(unsigned hand,std::uint64_t leftPressed,std::uint64_t rightPressed) noexcept{};
};
inline const InputApiV1* RequestInputApiV1() noexcept {
 const auto module=GetModuleHandleW(L"RPS_UI_Framework.dll");
 const auto function=module?GetProcAddress(module,"RPSUI_RequestInputApi"):nullptr;
 if(!function)return nullptr;
 const auto* api=reinterpret_cast<const InputApiV1*(RPSUI_CALL*)() noexcept>(function)();
 return api && api->version==1 && api->structSize>=sizeof(InputApiV1)?api:nullptr;
}
}
