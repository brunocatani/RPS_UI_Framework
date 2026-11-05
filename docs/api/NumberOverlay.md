# NumberOverlay

Header: `RPS_UI_Framework/NumberOverlay.h`. Namespace: `rpsui::api::number_overlay`. Version 2.0.

Numbers the host draws itself, with no consumer render callback and no borrowed payload. The host owns all resources; a render snapshot it already copied may finish after removal. It uses an owner token from the [Framework](Framework.md) interface.

`registerOverlay` takes a `Registration` with `overlayId` and `displayName` and returns a handle. `unregisterOverlay` removes it. `submit` sends a `Presentation`:

| Field | Meaning |
| --- | --- |
| `sequence` | Rising submission counter |
| `pose` | World pose in game units; width and height must match `kAspect` |
| `value` | Any `uint32`, including zero, drawn with no leading padding |
| `opacity` | Premultiplied, 0 to 1, on a transparent background |
| `open` | Whether the overlay is shown |
| `color` | Digit and frame color, 0 to 1 per channel |
| `secondaryValue` | Hud styles only: the number below the divider |
| `style` | `Style` value |
| `chamber` | `ChamberMarker` value: a cartridge beside the number, in any style |

`ChamberMarker` values: `None` draws nothing, `Empty` a hollow dim cartridge, `Chambered` a solid one. It takes the digit color and opacity; the digits move aside for it and keep their size where they fit.

`Style` values:

| Style | Layout |
| --- | --- |
| `Plain` | One value in the default face, centered |
| `Hud` | The Fallout HUD ammo counter: `value` over a divider over `secondaryValue`, right aligned, in a condensed bold face |
| `HudBracketed` | `Hud` inside the HUD's bracket pair |
