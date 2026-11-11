# Themes

Every world panel drawn through RPS UI Framework (RobCo PALM's wheel and Config, dialogue, the backpack, and any other mod's panels) takes its colours, fonts and sizes from one theme. Change the theme and every panel follows, while the game runs.

## Choosing a theme

Themes are files in `Documents\My Games\Fallout4VR\Mods_Config\RPS_UI_Framework\Themes\`. `Theme` in `RPS_UI.toml`, in the folder above, names the one in use, without `.toml`:

```toml
[Display]
Theme = "Pip-Boy"
```

Three themes are built in, in the colours of Fallout's own screens, with rounded corners and slightly see-through panels:

| Theme | Look |
| --- | --- |
| `Pip-Boy` (default) | Fallout 4's HUD and Pip-Boy green, `#12FF15`, on dark green |
| `New Vegas` | Fallout: New Vegas's HUD amber, `#FFB642`, on dark brown |
| `Vault-Tec` | Vault-Tec yellow on navy, with white text |

RPS UI writes each into `Themes` and puts back any key a file lacks. RobCo PALM's Config (RPS UI page, Theme) switches between them and any theme you add.

## Making a theme

1. Copy one of the built-in files under a new name, for example `Amber.toml`.
2. Set `Theme = "Amber"` in `RPS_UI.toml`.
3. Edit `Amber.toml`. Each save shows in the headset within a second.

A name may hold letters, digits, spaces, `-`, `_` and `.`. Naming a theme that has no file yet creates it as a copy of `Pip-Boy`.

### Colours

Colours are `"#RRGGBB"` or `"#RRGGBBAA"`, where `AA` is opacity (`FF` solid, `00` invisible). Each role has its help line above it in the file:

| Key | Used for |
| --- | --- |
| `Background` | A panel's body |
| `Surface` | Idle controls, headers and rails |
| `Hover` | Under the pointer |
| `Pressed` | While pressed or dragged |
| `Selected` | The open page and the current choice |
| `Border`, `Divider` | Control outlines; rules and separators |
| `Text`, `TextMuted`, `TextDisabled` | Text; help and secondary text; text of things that cannot be used now |
| `Accent`, `OnAccent` | Chosen things (as a tint, outline and text) and the solid fill of a page's main action; text and marks on that fill |
| `Focus` | The control the thumbstick adjusts |
| `Pending` | A change that is not saved yet |
| `Invalid` | A refused value and errors |
| `Success` | A finished action |
| `Scrim` | Behind a dialog |

`Surface`, `Hover`, `Pressed` and `Selected` read best as a see-through tint of `Accent` over `Background`, as the built-in themes do.

When text would be hard to read, RPS UI writes a warning to `RPS_UI_Framework.log` with the pair and its contrast: `Text` needs 4.5:1 against `Background`, `TextMuted`, `Pending` and `Invalid` 3:1, and `OnAccent` 4.5:1 against `Accent`. A see-through `Background` is checked against both a black and a white world behind it.

### Fonts

| Key | Used for |
| --- | --- |
| `Interface` | Labels and controls |
| `Strong` | Headings and emphasis |
| `Mono` | Numbers, code and console text |
| `Prose` | Long reading text such as dialogue |

Each names a `.ttf`, `.otf` or `.ttc` file: first in `Mods_Config\RPS_UI_Framework\Fonts\`, then among the fonts Windows has. A face that cannot be found falls back to the `Interface` face, and that to a built-in face.

### Sizes

In the panel's layout units: `FontSize` (body text), `TitleFontSize`, `SmallFontSize`, `TargetHeight` (the least height of anything you press), `Padding`, `Spacing`, `Rounding` (corner radius) and `BorderWidth`. The file gives each one's range; a value outside it is clamped and logged.

## Sharing a theme

A theme is one file. Share its `.toml`, and any font files it names that Windows does not have; the player puts them in `Themes` and `Fonts` and sets `Theme`.

## Sharpness

`Sharpness` in `RPS_UI.toml`, from 1.0 to 3.0, sets how many texture pixels each panel gets. Higher is crisper text and uses more video memory. A panel you enlarge in game gets more pixels, so it stays as sharp.
