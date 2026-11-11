# Changelog

## 2.0.0

Built for ROCK 1.0.0, RobCo PALM 2.0.0 and PAPER 2.0.0.
Mods built for RPS UI Framework 1.1 must be updated: at the main menu an error
box names each one, and the game closes when the box is closed.

### Added

- **Themes.** Every mod's panels draw with one theme you choose in
  `Documents\My Games\Fallout4VR\Mods_Config\RPS_UI_Framework\RPS_UI.toml`.
  Themes are files you can copy, edit and share; RobCo is the built-in
  template. See `docs/Themes.md`.
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

- Provider API **2.0**: eight interfaces (Framework, Cooperation, FocusedPanel,
  Input, NumberOverlay, PassivePanel, Registry, WorldDraw) in
  `include/RPS_UI_Framework/`, discovered through `RPSUI_QueryInterfaceV1`, one
  page each in `docs/api/`. The SDK folder is gone.
- RPS UI now owns every panel's Dear ImGui context, renderer, fonts, input and
  frame. A panel registers its layout size and a draw callback; the host passes
  the player's theme and font faces. Consumers must build with the Dear ImGui
  configuration in `ImGuiBuild.h`; another build is refused.
- Up to 256 consumers, 1024 panels and 256 input subscriptions.
