# Migrating a mod from RPS UI Framework 1.1 to 2.0

RPS UI Framework 1.1 published three tables through the RPS SDK: the panel
framework (`RPSUIFrameworkApi.h`, export `RPSUI_RequestApi`), cooperation
(`RPSUICooperationApi.h`, `RPSUI_RequestCooperationApi`) and input
(`RPSUIInputApi.h`, `RPSUI_RequestInputApi`). Version 2.0 publishes eight
interfaces at major 2 from this repository's `include/RPS_UI_Framework/`,
found through `RPSUI_QueryInterfaceV1`. The old exports stay only to refuse:
they return no table, and RPS UI stops the game at the main menu with an error
box naming the mod that called them. A mod built against the SDK headers has
to be rebuilt with the changes on this page.

The largest change is drawing. In 1.1 your mod ran its own Dear ImGui context
and DX11 backend and drew into a render target the host passed you. In 2.0 the
host owns every panel's ImGui context, renderer, fonts, input and frame, and
your mod only issues ImGui calls (section 4).

A mod that also binds ROCK, PAPER or RobCo PALM moves all of them at once;
each has its own `docs/api/Migration.md`. Coding agents can follow ROCK's
`skills/rock-1.0-migration/SKILL.md`, which covers the four together.

## 1. Build

- Remove the RPS SDK from your build: `find_package(RPS_SDK)`, include paths
  ending in `SDK/UI/include`, and any copy of the `RPSUI*Api.h` headers.
- Add this repository's `include/` directory through a CMake cache variable,
  for example `RPSUI_INCLUDE_DIR`, and include `<RPS_UI_Framework/...>`.
  Nothing is linked.
- If you draw panels, build Dear ImGui as section 4 describes and drop your
  DX11 ImGui backend.
- The headers need C++20 and a 64-bit build.

## 2. Discovery

| 1.1 | 2.0 |
| --- | --- |
| `rpsui::sdk::RequestApiV1()`, export `RPSUI_RequestApi` | `rpsui::api::acquire(query, framework)` with `const framework::Api*` |
| `RequestCooperationApiV1()`, `RPSUI_RequestCooperationApi` | `acquire(query, cooperation)` with `const cooperation::Api*` |
| Input request, `RPSUI_RequestInputApi` | `acquire(query, input)` with `const input::Api*` |

```cpp
#include <RPS_UI_Framework/Client.h>
#include <RPS_UI_Framework/Framework.h>

const auto module = GetModuleHandleW(rpsui::api::kModuleName);
const auto query = module ? reinterpret_cast<rpsui::api::QueryInterfaceV1>(
    GetProcAddress(module, rpsui::api::kQueryExportName)) : nullptr;
const rpsui::api::framework::Api* framework{};
if (rpsui::api::acquire(query, framework) != rpsui::api::Result::Ok) {
    // RPS UI absent or refused: run without panels.
}
```

`acquire` requests exactly the major, minor and table size your headers
declare. Never probe with trial queries: every refused query stops the game.
Feature negotiation is gone: `FeatureV1`, `requestedFeatures`,
`grantedFeatures`, the table's `apiFlavor` and `featureBits` are removed.
Every table function is set.

## 3. Names and records

- The namespace `rpsui::sdk` becomes `rpsui::api` with one namespace per
  interface (`framework`, `cooperation`, `input`, ...). `V1` is gone from every
  type: `ResultV1` is `rpsui::api::Result` (same values, plus
  `ImGuiBuildMismatch` and the discovery results), `PanelFlagV1` is
  `framework::PanelFlag`, `PhysicalHandV1` is `PhysicalHand`.
- Every record's `structSize` and `apiVersion` are `size` and `version`.
  Value-initialize records and the defaults are right; a short record returns
  `InvalidArgument` and another version `VersionMismatch` (`InvalidArgument` on
  the Framework registration and presentation calls). Reserved fields are gone.
- Capacity grew from 16 consumers and 8 panels to 256 consumers, 1024 panels
  and 256 input subscriptions.

## 4. Framework: panels drawn by the host

| 1.1 | 2.0 |
| --- | --- |
| `ConsumerRegistrationV1`, `ConsumerHandleV1` | `ConsumerRegistration` (`consumerId`, `displayName`), `ConsumerHandle` (`ownerToken`) |
| `PanelRegistrationV1.pixelWidth`, `pixelHeight` | `layoutWidth`, `layoutHeight`: your drawing size in ImGui units and the panel's default shape. The host picks the texture size from the panel's size in the world and the player's `Sharpness` setting |
| `PanelRegistrationV1.renderCallback(PanelRenderFrameV1)` | `drawCallback(PanelDrawFrame)`, plus an optional `setupCallback(PanelSetup)` and `imgui = RPSUI_IMGUI_BUILD()` |
| `PanelRenderFrameV1.d3dContext`, `renderTargetView` | Removed: the host renders into the panel's texture. `d3dDevice` stays for your own textures |
| `pointerPixelX`, `pointerPixelY` | `pointerX`, `pointerY` in layout units; the host has already fed them to ImGui |
| `backDown` (the grip level) | `backPressed`: 1 on exactly one frame per Back on a `ConfigNavigation` panel, delivered on the grip's release |
| `resetPanelSize` | `resetPanelLayout` (size and placement) |
| `submitPanelPresentation`, `getPanelState`, `unregisterPanel`, `isFrameworkReady` | Same names; `PanelPose` keeps its fields, and its height may be any positive value |

New: `PanelFlag::Movable` (the player carries the panel by holding grip on it),
`pulseHaptic`, and themes.

What to change in your drawing code:

1. Delete your ImGui context creation, `ImGui_ImplDX11_*` calls, font atlas
   building, `NewFrame` and `Render`, and the render target handling. The host
   does all of it.
2. Compile Dear ImGui yourself from the revision named in
   [Framework](Framework.md#building-dear-imgui), with
   `IMGUI_USER_CONFIG="RPS_UI_Framework/ImGuiBuild.h"` and without the DX11
   backend. `registerPanel` refuses another build with `ImGuiBuildMismatch`,
   which stops the game like an outdated mod.
3. Expand `RPSUI_IMGUI_BUILD()` where you fill `PanelRegistration::imgui`, with
   `<imgui.h>` and `<imgui_internal.h>` included there.
4. Start both callbacks with `RPSUI_IMGUI_BIND(frame->imgui)` (or
   `setup->imgui`). It makes the host's context current in your copy of ImGui
   and switches it to the host's allocator. Then issue ImGui calls in layout
   units: `frame->layoutWidth` by `frame->layoutHeight` is the whole panel.
5. Load extra fonts, adjust the style and create textures in `setupCallback`,
   which runs once per context. Remake them when `contextGeneration` or
   `deviceGeneration` changes.
6. Draw with the player's theme: `frame->theme` holds colour roles and sizes,
   and `frame->fonts` the theme's four faces. Plain ImGui widgets follow the
   theme without code.

## 5. Cooperation

The calls keep their names: `registerPanel`, `getSnapshot`,
`unregisterPanelSafely`, `unregisterConsumerSafely`. Records lose `V1`:
`CooperativePanelRegistrationV1` is `cooperation::PanelRegistration` and embeds
the 2.0 Framework `PanelRegistration` (so it carries `layoutWidth`,
`layoutHeight`, `imgui`, `setupCallback` and `drawCallback`), and
`PanelAgreementV1`, `ConsumerDirectoryEntryV1`, `PanelDirectoryEntryV1` and
`CooperationSnapshotV1` are `PanelAgreement`, `ConsumerDirectoryEntry`,
`PanelDirectoryEntry` and `Snapshot`.

## 6. Input

The calls keep their names: `subscribe`, `unsubscribe`, `capture`,
`rawInputReadActive`, `capturedButtons`, `setPointerAim`. New:
`captureKeyboard`, for text entry. Records lose `V1`: `InputFrameV1` is
`input::Frame`, `HandInputV1` is `HandInput`, `InputCaptureV1` is `Capture`,
`PointerAimV1` is `PointerAim`, `InputCallbackV1` is `Callback`.

- `Capture` and `PointerAim` gain `version`.
- `HandInput.valid`, `Frame.ready` and `Frame.leftHanded` are `uint8` instead of
  `bool`: compare them with 0.

## 7. New interfaces

These did not exist in 1.1. See their pages if you need them:

- [WorldDraw](WorldDraw.md): spheres, lines, world-anchored text and meshes.
  ROCK 1.0 no longer draws for other mods; this replaces ROCK's Diagnostics
  drawing.
- [NumberOverlay](NumberOverlay.md): numbers the host draws, such as ammo
  counters.
- [PassivePanel](PassivePanel.md): panels that show and take no input.
- [FocusedPanel](FocusedPanel.md): fixed-size panels that take input only
  while pointed at.
- [Registry](Registry.md): a paged census of consumers and panels.

## 8. Check the result

1. Build against `include/RPS_UI_Framework/` with no RPS SDK path left. Fix
   every compile error with the tables above.
2. Search your source for leftovers: `rpsui::sdk`, `RPSUI_Request`,
   `structSize`, `apiVersion`, `renderCallback`, `renderTargetView`,
   `ImGui_ImplDX11`, `ImGui::CreateContext`, `backDown`.
3. Start the game with RPS UI Framework 2.0. No error box appears at the main
   menu, `RPS_UI_Framework.log` names no refusal for your DLL, and your panels
   draw with the player's theme.
