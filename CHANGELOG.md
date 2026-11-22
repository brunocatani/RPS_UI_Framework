# Changelog

## Unreleased

### Added

- **For mod authors: NumberOverlay 2.1.** A `label` drawn with the numbers in their
  face and color: beside the second row in the chamber column of the HUD styles,
  after the number in `Plain`. ROCK's ammo counters use it for the caliber. Mods
  built for 2.0 keep working.

### Changed

- The settings menu is now the **ROCK Family Configurator**, and so is its holotape
  (formerly **RPS UI Settings**). The holotape works with or without RobCo PALM;
  PALM's Config opens the same menu.

### Fixed

- The holotape is back. Since `RPS_UI_Framework.esp` became ESL-flagged in 2.0.1,
  RPS UI could not find it among light plugins, so no holotape was given and playing
  an old one did nothing.

## 2.0.1

### Changed

- A mod built for another RPS UI Framework version no longer closes the game. RPS UI
  refuses it and names it in `RPS_UI_Framework.log`; the game keeps running without
  that mod's RPS UI features. No error box appears at the main menu.
- `RPS_UI_Framework.esp` is ESL-flagged and no longer takes a load-order slot. Its
  file name and form IDs are unchanged.

### Fixed

- RPS UI hands the controllers back to the game while the dialogue menu or the
  lockpicking menu is open. Before, those two menus were looked up under the wrong
  names, so PALM and RPS UI panels stayed live during conversations and lockpicking.

## 2.0.0

Built for ROCK 1.0.0, RobCo PALM 2.0.0 and PAPER 2.0.0.
Mods built for RPS UI Framework 1.1 must be updated: at the main menu an error
box names each one, and the game closes when the box is closed.

### Added

- **Settings menu.** Play the **RPS UI Settings** holotape in the Pip-Boy (from
  `RPS_UI_Framework.esp`, given whenever a game starts or loads without one) to
  open every RPS mod's settings, the Spawner and the Console, without the wheel.
  RobCo PALM's Config opens the same menu. Each settings page starts with its
  choices, named and explained in one line; numbers are under Fine-tuning, with a
  Default button for values changed from ROCK's defaults. The ROCK Dev page shows
  while ROCK's Developer Settings is on. The Console's shortcut buttons moved to
  `Mods_Config\RPS_UI_Framework\ConsoleCommands.ini`; shortcuts you wrote for
  RobCo PALM are copied there the first time.
- **Settings menu preview** for developers: `RPSUISettingsPreview.exe` shows the
  menu on the desktop from a sandbox of settings files, and `--capture` saves its
  pages as pictures.
- **Themes.** Every mod's panels draw with one theme you choose in
  `Documents\My Games\Fallout4VR\Mods_Config\RPS_UI_Framework\RPS_UI.toml`.
  Themes are files you can copy, edit and share. Built in: Pip-Boy (the
  game's HUD green, the default), New Vegas (amber) and Vault-Tec (blue and
  yellow), with rounded corners and slightly see-through panels; RobCo PALM's
  Config picks one. See `docs/Themes.md`.
- **Sharp panels at any size.** Each panel's resolution follows its size in the
  world and the `Sharpness` setting, with mipmaps, so enlarged panels are no
  longer blurry and small ones no longer shimmer.
- **Movable panels.** On panels that allow it, hold grip on the panel to carry
  it, push or pull it with the stick, and release grip to place it. A short grip
  press is Back.
- **Controller feedback.** Pressing something on a panel ticks the controller;
  the strength is a setting.
- **World drawing** for every mod: grab indicators, ammo counters and
  diagnostic shapes from ROCK, PAPER and SCISSORS now draw through RPS UI.
- **Ammo counter styles:** HUD colour, Fallout HUD stacked and bracketed
  styles, and a cartridge marker beside the number that shows a chambered round.
- **Keyboard text entry** for panels such as PALM's Console tab: while a panel
  holds the keyboard, key presses go to it and not to the game.
- Panels that show guides without taking input, and number overlays, for mods
  that need them.
- **A ring pointer.** Every panel's pointer is a small ring with a centre dot in
  the theme's colour instead of an arrow.

### Fixed

- RPS UI and Mods Config FO4 VR no longer fight over the controllers: while that
  menu is open, the controllers belong to it and RPS UI panels take no input.
- A press that closes a panel (Cancel, Config, grip Back) no longer reaches the
  game after the panel closes.
- Works beside another renderer that hooks the same frame submit (True Scopes
  and others): RPS UI chains behind it instead of replacing it.
- Lower per-frame cost: rendering reads no longer make a kernel call per pointer
  hop on every frame.

### For mod authors

- Provider API **2.0**: nine interfaces (Framework, Cooperation, FocusedPanel,
  Input, NumberOverlay, PassivePanel, Registry, SettingsMenu, WorldDraw) in
  `include/RPS_UI_Framework/`, discovered through `RPSUI_QueryInterfaceV1`, one
  page each in `docs/api/`. The SDK folder is gone.
- RPS UI now owns every panel's Dear ImGui context, renderer, fonts, input and
  frame. A panel registers its layout size and a draw callback; the host passes
  the player's theme and font faces. Consumers must build with the Dear ImGui
  configuration in `ImGuiBuild.h`; another build is refused.
- Up to 256 consumers, 1024 panels and 256 input subscriptions.
