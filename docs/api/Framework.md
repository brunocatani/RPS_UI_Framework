# Framework

Header: `RPS_UI_Framework/Framework.h`. Namespace: `rpsui::api::framework`. Version 2.0.

Registers consumers and world panels, submits panel poses and reads panel state. Every other interface takes the owner token issued here.

## Consumers

`registerConsumer` takes a `ConsumerRegistration` with a stable `consumerId` and a `displayName`, and returns a `ConsumerHandle` holding `ownerToken`. `unregisterConsumer` removes the consumer. If its callbacks may still be running, use the Cooperation interface's safe unregister instead.

## Panels

`registerPanel` takes a `PanelRegistration` and returns a panel handle.

| Field | Meaning |
| --- | --- |
| `panelId`, `displayName` | Stable identity and label |
| `layoutWidth`, `layoutHeight` | Your drawing size in ImGui units (`DisplaySize`) and the panel's default shape. Any positive size |
| `defaultPhysicalWidth`, `minimumPhysicalWidth`, `maximumPhysicalWidth` | World size in game units |
| `sortOrder` | Composition order |
| `flags` | `PanelFlag` bits; zero is a rectangular opaque panel |
| `imgui` | Your Dear ImGui build, `RPSUI_IMGUI_BUILD()` |
| `setupCallback` | Optional. Called once per ImGui context before its first frame |
| `drawCallback`, `userData` | Called every frame the panel is open; keep both alive until the panel is unregistered |

`PanelFlag` values:

| Flag | Meaning |
| --- | --- |
| `Transparent` | The panel has no opaque background |
| `FixedSize` | The user cannot resize the panel |
| `CircularInput` | Hit testing uses the inscribed circle (`panelContains`) |
| `ConfigNavigation` | Opts in to raw stick handoff and grip-back; hold-based wheels leave this unset |
| `Movable` | The player can carry the panel: grip held on it moves it with the hand, the stick pushes and pulls it, and releasing the grip places it |

## Presenting

`submitPanelPresentation` sends a `PanelPresentation` with a rising `sequence`, a `PanelPose` in game units and the `open` state. The pose's width must be within the registered limits; its height may be anything positive. The layout is scaled onto the panel on each axis, so a height off the layout's shape stretches the drawing. `getPanelState` returns the current pose, pointer hand and resize state; the pose includes the player's resizing and carrying, and `sequence` rises when either changes it. `resetPanelLayout` returns the panel to the pose it was last presented with, at its default width.

### Grip and haptics

On a `ConfigNavigation` or `Movable` panel the grip belongs to the UI while the pointer is on the panel. A grip press released before the player's hold time (`GripMoveSeconds` in `RPS_UI.toml`) is Back, delivered on the release, so no part of the press reaches the game after Back closes the panel. On a `Movable` panel a grip held past the hold time carries the panel instead, and is not Back.

The host pulses the pointing controller when a press makes an ImGui control active, and when a carry starts. `pulseHaptic` sends your own pulse, for presses ImGui does not see such as hand-drawn controls: strength 0 to 1, scaled by the player's `HapticStrength`, from any thread. A pulse is sent on the next input tick while the UI has the controllers, and dropped otherwise.

## Resolution

You never choose a texture size. The host gives each panel `Sharpness` texture pixels per layout unit at its default width (`RPS_UI.toml`, 1.5 unless the player changes it) and scales that with the panel's size in the world, so text stays equally sharp when a player enlarges a panel. Each texture has a full mip chain and is sampled anisotropically, so a small or distant panel is filtered rather than shimmering. `PanelDrawFrame::pixelWidth` and `pixelHeight` report the texture's size this frame. Dear ImGui rasterizes fonts at the texture's density on its own.

## Drawing

RPS UI draws every panel with [Dear ImGui](https://github.com/ocornut/imgui). The host owns each panel's ImGui context: the DX11 renderer, the font atlas, the input it feeds and the frame. Your mod only issues ImGui calls.

### Building Dear ImGui

Compile your own copy of Dear ImGui from the same revision as the host, `1.93.0 WIP` (`IMGUI_VERSION_NUM` 19294, upstream commit `f562ab8e`), with `RPS_UI_Framework/ImGuiBuild.h` as its user configuration:

```cmake
add_library(my_imgui STATIC "${IMGUI_DIR}/imgui.cpp" "${IMGUI_DIR}/imgui_draw.cpp"
  "${IMGUI_DIR}/imgui_tables.cpp" "${IMGUI_DIR}/imgui_widgets.cpp")
target_include_directories(my_imgui PUBLIC "${IMGUI_DIR}" "${RPSUI_INCLUDE_DIR}")
target_compile_definitions(my_imgui PUBLIC IMGUI_USER_CONFIG="RPS_UI_Framework/ImGuiBuild.h")
```

Leave out the DX11 backend: the host renders. If you need more configuration, include `RPS_UI_Framework/ImGuiBuild.h` first from your own user configuration file. Nothing that changes ImGui's data layout may differ.

`registerPanel` compares `PanelRegistration::imgui` with the host's build and refuses another one with `ImGuiBuildMismatch`. The refusal is logged with your DLL and stops the game at the main menu, like an outdated consumer.

### Callbacks

Both callbacks run on the render thread. Start each with `RPSUI_IMGUI_BIND(setup->imgui)` or `RPSUI_IMGUI_BIND(frame->imgui)`: it makes the host's context current in your copy of ImGui and switches your copy to the host's allocator, so memory crosses the DLL boundary safely. Include `<imgui_internal.h>` beside `<imgui.h>` where you expand `RPSUI_IMGUI_BUILD()`.

```cpp
void RPSUI_CALL draw(const rpsui::api::framework::PanelDrawFrame* frame, void* userData) noexcept
{
    RPSUI_IMGUI_BIND(frame->imgui);
    ImGui::SetNextWindowPos({ 0, 0 });
    ImGui::SetNextWindowSize({ frame->layoutWidth, frame->layoutHeight });
    ImGui::Begin("Example", nullptr, ImGuiWindowFlags_NoDecoration);
    ImGui::TextUnformatted("Hello");
    ImGui::End();
}
```

`setupCallback` receives a `PanelSetup` once per context, before its first frame: on the panel's first draw, after a device change, after a theme change, and after the host rebuilt a context whose draw failed. The theme's style and faces are already in place (see [Themes](#themes)); add your own fonts, adjust the style and create textures there. Fonts and textures you add belong to that context; `contextGeneration` changes when it is replaced, and anything made for the old one must be made again. `style.FontSizeBase` is left unset, so the first frame takes the size of whatever font is `io.FontDefault` then: the theme's Interface face at its body size, or a font your setup callback installs.

`drawCallback` receives a `PanelDrawFrame` between the host's `NewFrame` and `Render`. Before calling it, the host has set the display size, fed the pointer position, the primary button and the scroll axes, and turned the software cursor on while the pointer is valid. You may change `io.MouseDrawCursor` during the frame. The frame's coordinates are in layout units, the same as ImGui's `DisplaySize` and `MousePos`. `backPressed` is 1 on exactly one frame after each Back press on a `ConfigNavigation` panel (see [Grip and haptics](#grip-and-haptics)).

`d3dDevice` is the `ID3D11Device` for your own textures (`ImTextureID` is an `ID3D11ShaderResourceView*`). There is no render target or device context: the host renders the frame into the panel's own texture. Rebuild device textures when `deviceGeneration` changes.

An ImGui context lives until the panel is unregistered or the device changes, so ImGui state such as scroll positions survives closing and reopening the panel. Unbalanced `Begin`/`End` calls are recovered without asserts.

### Themes

Players choose how every panel looks: `Theme` in `RPS_UI.toml` names a file in `Mods_Config\RPS_UI_Framework\Themes\`, and the built-in `RobCo` theme is written there as the template ([guide](../Themes.md)). Before your setup callback, the host sets the context's ImGui style from the theme, so plain ImGui widgets follow it without any code of yours.

`PanelSetup::theme` and `PanelDrawFrame::theme` point at the `Theme` record for the callback. Draw your own shapes with its roles instead of fixed colours:

| Role | Use |
| --- | --- |
| `background` | A panel's body |
| `surface`, `hover`, `pressed`, `selected` | A control idle, under the pointer, while pressed, and chosen |
| `border`, `divider` | Outlines; rules and separators |
| `text`, `textMuted`, `textDisabled` | Text; help and secondary text; text of things that cannot be used now |
| `accent`, `onAccent` | Solid fills of chosen things; text and marks on them |
| `focus` | The control the thumbstick adjusts |
| `pending`, `invalid`, `success` | A change not yet saved; a refused value or error; a finished action |
| `scrim` | Behind a dialog |

Colours have straight alpha. Sizes are in layout units: `fontSize`, `titleFontSize` and `smallFontSize` for text, `targetHeight` as the least height of anything pressable, and `padding`, `spacing`, `rounding` and `borderWidth`. Scale your own text sizes by `fontSize` when you want a player's larger body text to reach your panel.

`fonts` holds the theme's four faces by `FontRole`: `Interface` (also `io.FontDefault`), `Strong`, `Mono` and `Prose`, for long reading text. Each renders at any size with `ImGui::PushFont(font, size)`.

A theme change rebuilds every panel's context, so your setup callback runs again with the new theme. Keep the theme pointer only for the callback that received it.

`support/RPS_UI_Framework/ThemeDefaults.h` holds the built-in theme as a `Theme` record, and `support/RPS_UI_Framework/ThemeStyle.h` holds the host's theme-to-style mapping, for previews and tests that style their own contexts the way the game does.

### Showing controller bindings

Two support headers show a controller binding the same way in every mod. `BindingText.h` (standard library only) names controls the way ROCK's settings do, A and B on the right controller and X and Y on the left, reads ROCK's button-layout values with `parseControl`, and turns a `Binding` (hand, control, gesture, optional held modifier) into a sentence such as "Hold right grip" or "Double-tap left X while holding left grip". `BindingIcons.h` draws each control as a procedural icon at any size in any colour, marks the gesture (tap, double tap, hold, release) and the hand, and `chip` puts an icon beside its sentence on the theme's surface.

### Desktop previews and tests

`support/RPS_UI_Framework/ImGuiPanelHost.h` is the code the host runs each panel frame with: context settings, the theme's style and faces, input feeding and the frame. Its `LocalPanelHost` runs your setup and draw callbacks in a desktop preview or test exactly as the game does, with its own context and DX11 renderer. It includes Dear ImGui and its DX11 backend, so it is not part of the runtime API and only previews and tests use it; in the game, RPS UI Framework is the only host.

## Readiness

`isFrameworkReady` reports whether the host can serve panels. Calls made before it returns true may return `FrameworkNotReady`.
