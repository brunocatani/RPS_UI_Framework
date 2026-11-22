# NumberOverlay

Header: `RPS_UI_Framework/NumberOverlay.h`. Namespace: `rpsui::api::number_overlay`. Version 2.1.

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
| `label` | 2.1: a short UTF-8 text drawn with the numbers, terminated inside its 32 bytes; empty for none |

`ChamberMarker` values: `None` draws nothing, `Empty` a hollow dim cartridge, `Chambered` a solid one. It takes the digit color and opacity; the digits move aside for it and keep their size where they fit.

`Style` values:

| Style | Layout |
| --- | --- |
| `Plain` | One value in the default face, centered |
| `Hud` | The Fallout HUD ammo counter: `value` over a divider over `secondaryValue`, right aligned, in a condensed bold face |
| `HudBracketed` | `Hud` inside the HUD's bracket pair |

## Label (2.1)

`label` is drawn in the numbers' face, color and opacity, its digits `kLabelScale` of the numbers' digit height:

- `Hud` and `HudBracketed`: the label shares the column right of the rows with the cartridge. The cartridge stands beside the first row and the label beside the second, on its baseline. The bracket pair encloses both.
- `Plain`: the number shrinks to `kPlainLabelNumberFill` of the panel height and moves to the panel's left edge; the cartridge and then the label follow it on its baseline.

The numbers keep their size; a label wider than the panel's remaining width shrinks alone. A 2.0 record (`size` of `kPresentationSize20`) has no label and draws as before. A consumer that needs labels requests minor 1; one that also runs with RPS UI 2.0 requests minor 0 and sends labels only when the returned descriptor reports minor 1 or later.
