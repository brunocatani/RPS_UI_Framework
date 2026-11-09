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
| `pixelWidth`, `pixelHeight` | Render target size |
| `defaultPhysicalWidth`, `minimumPhysicalWidth`, `maximumPhysicalWidth` | World size in game units |
| `sortOrder` | Composition order |
| `flags` | `PanelFlag` bits; zero is a rectangular opaque panel |
| `layoutWidth`, `layoutHeight` | ImGui's display size. Both zero uses the pixel size; otherwise set both, at the pixel size's aspect |
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

## Presenting

`submitPanelPresentation` sends a `PanelPresentation` with a rising `sequence`, a `PanelPose` in game units and the `open` state. `getPanelState` returns the current pose, pointer hand and resize state. `resetPanelSize` returns the panel to its default width.

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

`setupCallback` receives a `PanelSetup` once per context, before its first frame: on the panel's first draw, after a device change, and after the host rebuilt a context whose draw failed. Add fonts, set the style and create textures there. Fonts and textures you add belong to that context; `contextGeneration` changes when it is replaced, and anything made for the old one must be made again. `defaultFont` is the host's text face (Consolas, or ImGui's built-in face when Windows has no Consolas), which is also `io.FontDefault`; the default size is 20 pixels.

`drawCallback` receives a `PanelDrawFrame` between the host's `NewFrame` and `Render`. Before calling it, the host has set the display size, fed the pointer position, the primary button and the scroll axes, and turned the software cursor on while the pointer is valid. You may change `io.MouseDrawCursor` during the frame. The frame's coordinates are in layout units, the same as ImGui's `DisplaySize` and `MousePos`. `backDown` is the gated physical grip of the pointing hand; derive edges from it locally.

`d3dDevice` is the `ID3D11Device` for your own textures (`ImTextureID` is an `ID3D11ShaderResourceView*`). There is no render target or device context: the host renders the frame into the panel's own texture. Rebuild device textures when `deviceGeneration` changes.

An ImGui context lives until the panel is unregistered or the device changes, so ImGui state such as scroll positions survives closing and reopening the panel. Unbalanced `Begin`/`End` calls are recovered without asserts.

### Desktop previews and tests

`support/RPS_UI_Framework/ImGuiPanelHost.h` is the code the host runs each panel frame with: context settings, the default font, input feeding and the frame. Its `LocalPanelHost` runs your setup and draw callbacks in a desktop preview or test exactly as the game does, with its own context and DX11 renderer. It includes Dear ImGui and its DX11 backend, so it is not part of the runtime API and only previews and tests use it; in the game, RPS UI Framework is the only host.

## Readiness

`isFrameworkReady` reports whether the host can serve panels. Calls made before it returns true may return `FrameworkNotReady`.
