# RPS UI Framework

Shared world-space UI and VR controller interaction for Fallout 4 VR mods.

RPS UI Framework powers RobCo PALM and its configuration panels. Install it when
a mod lists it as a requirement; individual mods supply their menus.

It works alongside [Mods Config FO4 VR](https://github.com/ArthurHub/F4VR-Mods-Config):
while that menu is open, the controllers belong to it and RPS UI panels take no
input until it closes.

## Settings and themes

`Documents\My Games\Fallout4VR\Mods_Config\RPS_UI_Framework\RPS_UI.toml` holds the
panels' sharpness and the theme every mod's panels draw with. Themes are files you
can copy, edit and share; see [Themes](docs/Themes.md).

## For developers

The public UI API headers are in this repository's [include/](include/RPS_UI_Framework/),
with one page per interface in [docs/api/](docs/api/Overview.md). Add `include/`
to your include path through a CMake cache variable such as `RPSUI_INCLUDE_DIR`;
never copy the headers into your project. Read the
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

The host publishes eight interfaces at major 2, discovered through `RPSUI_QueryInterfaceV1`; see [docs/api/](docs/api/Overview.md). Native controller input requires neither ROCK nor a skeleton provider. Consumers retain callback data until the appropriate safe unregister or input unsubscribe handshake succeeds.
