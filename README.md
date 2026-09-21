# RPS UI Framework

Shared world-space UI and VR controller interaction for Fallout 4 VR mods.

RPS UI Framework powers RobCo PALM and its configuration panels. Install it when
a mod lists it as a requirement; individual mods supply their menus.

## For developers

The public UI API is available in the
[RPS SDK](https://github.com/brunocatani/RPS_SDK/tree/main/SDK/UI).
Read the [DevArtificial documentation](https://devartificial.pro/docs/rps-sdk/ui)
for setup, API reference, and integration guides.

## Credits

- [CommonLibF4VR](https://github.com/ArthurHub/CommonLibF4VR) and its contributors.
- [Arthur](https://github.com/ArthurHub) for [F4VR-CommonFramework](https://github.com/ArthurHub/F4VR-CommonFramework).

## License

GNU General Public License version 3 or later (`GPL-3.0-or-later`).
See [COPYRIGHT](COPYRIGHT) and [LICENSE](LICENSE).
Required dependency notices are in [THIRD_PARTY_NOTICES.txt](THIRD_PARTY_NOTICES.txt).

## Developer services

The host exposes three independent V1 tables: eight panel calls, six input/capture/calibration calls, and four cooperation/directory/safe-retirement calls. Native controller input requires neither ROCK nor a skeleton provider. Consumers retain callback data until the appropriate safe unregister or input unsubscribe handshake succeeds.
