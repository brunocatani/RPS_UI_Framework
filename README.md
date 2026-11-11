# RPS UI Framework

Shared world-space UI and VR controller interaction for Fallout 4 VR mods.

RPS UI Framework powers RobCo PALM and the settings menu of every RPS mod. Install
it when a mod lists it as a requirement.

It works alongside [Mods Config FO4 VR](https://github.com/ArthurHub/F4VR-Mods-Config):
while that menu is open, the controllers belong to it and RPS UI panels take no
input until it closes.

## Settings menu

Play the **RPS UI Settings** holotape in your Pip-Boy (enable `RPS_UI_Framework.esp`;
you get the holotape whenever a game starts or loads without one). When you close the
Pip-Boy, the menu opens in front of you with:

- **Settings** for ROCK, PAPER and the other RPS mods you have loaded. Each page starts
  with the main choices, named and explained in one line, as in ROCK's setup wizard;
  numbers are under **Fine-tuning**. Turn on ROCK's *Developer Settings* to add the
  ROCK Dev page.
- **Spawner**: add any item from your load order to your inventory.
- **Console**: run console commands with the on-screen keyboard or a real one. Its
  shortcut buttons are in `Mods_Config\RPS_UI_Framework\ConsoleCommands.ini`.

RobCo PALM's Config opens the same menu from the wheel.

## Settings and themes

`Documents\My Games\Fallout4VR\Mods_Config\RPS_UI_Framework\RPS_UI.toml` holds the
panels' sharpness, the theme every mod's panels draw with, how strongly the controller
ticks when you press something on a panel, and how long to hold grip on a panel before
it moves with your hand. Themes are files you can copy, edit and share; see
[Themes](docs/Themes.md).

On panels that allow it, hold grip on the panel to carry it, push or pull it with the
stick, and release grip to place it. A short grip press is Back.

## For developers

The public UI API headers are in this repository's [include/](include/RPS_UI_Framework/),
with one page per interface in [docs/api/](docs/api/Overview.md). Add `include/`
to your include path through a CMake cache variable such as `RPSUI_INCLUDE_DIR`;
never copy the headers into your project. A mod built against RPS UI Framework 1.1
is updated with [Migration](docs/api/Migration.md). Read the
[DevArtificial documentation](https://devartificial.pro/docs/apis/ui)
for setup and integration guides.

## Credits

- [CommonLibF4VR](https://github.com/ArthurHub/CommonLibF4VR) and its contributors.
- [Arthur](https://github.com/ArthurHub) for [F4VR-CommonFramework](https://github.com/ArthurHub/F4VR-CommonFramework).

## License

GNU General Public License version 3 or later (`GPL-3.0-or-later`).
See [COPYRIGHT](COPYRIGHT) and [LICENSE](LICENSE).
Required dependency notices are in [THIRD_PARTY_NOTICES.txt](THIRD_PARTY_NOTICES.txt).

## Developer services

The host publishes nine interfaces at major 2, discovered through `RPSUI_QueryInterfaceV1`; see [docs/api/](docs/api/Overview.md). Native controller input requires neither ROCK nor a skeleton provider. Consumers retain callback data until the appropriate safe unregister or input unsubscribe handshake succeeds.
