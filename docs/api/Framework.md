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
| `renderCallback`, `userData` | Called to draw the panel; keep both alive until the panel is unregistered |

`PanelFlag` values:

| Flag | Meaning |
| --- | --- |
| `Transparent` | The panel has no opaque background |
| `FixedSize` | The user cannot resize the panel |
| `CircularInput` | Hit testing uses the inscribed circle (`panelContains`) |
| `ConfigNavigation` | Opts in to raw stick handoff and grip-back; hold-based wheels leave this unset |

## Presenting

`submitPanelPresentation` sends a `PanelPresentation` with a rising `sequence`, a `PanelPose` in game units and the `open` state. `getPanelState` returns the current pose, pointer hand and resize state. `resetPanelSize` returns the panel to its default width.

## Rendering

The `PanelRenderCallback` receives a `PanelRenderFrame` holding the D3D11 device, context and render target view, as `void*`, plus the pixel size, the pointer position and the button state. The frame is borrowed for the call. `backDown` is the gated physical grip of the pointing hand; derive edges from it locally. `deviceGeneration` changes when device resources must be recreated.

## Readiness

`isFrameworkReady` reports whether the host can serve panels. Calls made before it returns true may return `FrameworkNotReady`.
