# RPS UI Framework

Shared world-space UI and VR controller interaction for Fallout 4 VR. It hosts world panels and draws them with Dear ImGui, owns world overlay drawing (gameplay indicators and debug shapes, drawn over the lit scene; it does not paint scene surfaces) and publishes controller input. It owns one menu, the settings menu: the Spawner, the Console and the pages other mods add through the SettingsMenu interface, opened from its holotape or by a mod such as RobCo PALM.

## Layout

- `include/RPS_UI_Framework/`: the public API, the only copy of these headers. They include only the C++ standard library and each other.
- `support/RPS_UI_Framework/`: code shared by the host and consumers' desktop previews and tests, kept out of `include/` because it is not runtime API. `ImGuiPanelHost.h` runs a panel's Dear ImGui frame, `ThemeStyle.h` turns a theme into ImGui's style (both include Dear ImGui), `ThemeDefaults.h` holds the built-in themes (Pip-Boy, the default, New Vegas and Vault-Tec; standard library only), and `ThemeStyle.h` also has `pushChosen`, the shared look of a chosen choice, and `BindingText.h` (standard library only) and `BindingIcons.h` show controller bindings as sentences and icons.
- `docs/api/`: one page per interface, starting at `Overview.md`. `docs/api/Migration.md` updates a mod built against RPS UI Framework 1.1 (the RPS SDK's `RPSUI*Api.h` headers); ROCK's `skills/rock-1.0-migration/SKILL.md` covers it with ROCK, PAPER and RobCo PALM. `docs/Themes.md` is the players' and theme makers' guide.
- `src/`: the plugin. `Api.cpp` holds the tables, discovery and retired exports. `FrameworkRuntime.cpp` hosts panels, `render/PanelImGui.cpp` owns their Dear ImGui contexts and `InputService.cpp` publishes input. `UiSettings.cpp` reads `RPS_UI.toml` and `ThemeStore.cpp` the theme files, on a watcher thread that publishes immutable snapshots. `ModsConfigClient.cpp` connects RPS UI's page in Mods Config FO4 VR; `ModsConfigFO4Api.h` beside it is Mods Config's own client header, copied as is: refresh it from Mods Config's `src/api/`, never edit it. `WorldDrawStore.cpp` owns world drawing.
- `src/settings_menu/`: the settings menu, built into a game library and a desktop-preview library. `ConfiguratorRuntime.cpp` is its state and layout, `SpawnBrowser.cpp` the Spawner, `ConsoleTab.cpp` and `ConsoleService.cpp` the Console, `ExternalPages.cpp` holds mods' pages. `src/SettingsMenuHost.cpp` ties it to the plugin's lifecycle and `src/SettingsHolotape.cpp` to the holotape.
- `data/plugin-source/`: the Spriggit source of `RPS_UI_Framework.esp` (the settings holotape); `data/mod/` holds the game data that ships: the built plugin and RPS UI's Mods Config FO4 VR page, `ModsConfigFO4/Config/RPS_UI_Framework/spec.json`. Edit the YAML and rebuild the plugin with Spriggit's `deserialize`.
- `preview/`: `RPSUISettingsPreview.exe`, the settings menu's Spawner (with sample items) and Console on the desktop; `--capture <folder>` saves both as PNG.
- `tests/`: targeted tests. `ConsumerExample.cpp` is the minimal consumer, compiled with the public header test. `tests/settings_menu/` covers the menu's pure policies.

## Build and tests

Requires Windows x64, Visual Studio 2022, CMake, vcpkg with `VCPKG_ROOT` set, and checkouts of CommonLibF4VR, F4VR-CommonFramework and Dear ImGui at the revision `docs/api/Framework.md` names, and RPS Framework's staged package (built from RPS Framework). Point `RPSFramework_DIR` at the package's CMake directory and `IMGUI_DIR` at the Dear ImGui checkout when they are not where `CMakeLists.txt` looks by default.

- Build with `cmake --preset custom-fast`, then `cmake --build build-fast --config Release`.
- Build tests with `cmake --build build-fast --config Release --target build_tests`, then run `ctest --test-dir build-fast -C Release --output-on-failure`.

## Runtime files

The plugin is `F4SE/Plugins/RPS_UI_Framework.dll`, `RPS_UI_Framework.esp` at the root of the mod folder and its Mods Config page under `ModsConfigFO4/Config/RPS_UI_Framework/`, all deployed under the mod folder set by `COPY_PLUGIN_BASE_PATH` in `CMakeUserPresets.json`. It logs to `RPS_UI_Framework.log` in the game's F4SE log folder. Its settings are `RPS_UI.toml` in `Mods_Config\RPS_UI_Framework` under the player's Documents game folder, with themes in `Themes\` and theme fonts in `Fonts\` beside it, and the Console's shortcut buttons in `ConsoleCommands.ini`.

## What it does not own

Each mod owns its settings, their files and its own page in Mods Config FO4 VR; the settings menu edits no mod's settings. Other menus belong to the mods that show them. RobCo PALM owns the wheel and persists the shared pointer aim. Consumers own their panel contents (their ImGui calls, fonts, styles and textures) and keep their callback data alive until a safe unregister returns `Ok`.

## Settings menu

- Its tabs are the Spawner, the Console, then the pages mods add (`src/settings_menu/ExternalPages.cpp`). It opens the first time on its home tab, the first page a mod added (RobCo PALM's Wheel items) or else the Spawner, and afterwards on the tab it was closed on. Back on another tab goes home; Back on the home tab closes the menu.
- The Console hooks the call to `ConsoleLog::AddString` at plugin load (`src/settings_menu/ConsoleService.cpp`), after checking the live bytes, and runs commands through `Console::ExecuteCommand`, so the native Console menu is never needed. Commands containing `%` are refused: the engine echoes a command through its print function as the format string.
- The on-screen keyboard is its own panel (`rpsui.settings.keyboard`, `src/settings_menu/OnScreenKeyboard.cpp`) under the menu. It is up on the Console tab, and on the Spawner while one of its searches is open. Both keyboards feed a `ConsoleLine`, not an ImGui text field, because clicking a key would take focus from that field. The real keyboard is captured only while the keyboard panel is up.
- Console shortcut buttons come from `ConsoleCommands.ini` (`src/settings_menu/ConsoleShortcuts.h` parses it), read only on entering the Console tab and on Reload, never per frame.
- With `Flying_BRICKS.esp` loaded, the Spawner's All ammo source lists only the playable rounds that plugin defines or overrides (`src/settings_menu/SpawnBrowser.cpp`); otherwise every named ammo form.

## Invariants

- A consumer binds at runtime through `RPSUI_QueryInterfaceV1` and never links the plugin or copies its headers.
- Each interface header declares `kMajor` and `kMinor`, and each interface has one page in `docs/api/`.
- Every version refusal and retired-export call is logged once with the calling DLL; the game keeps running (`src/RequiredUpdates.cpp`).
- Native controller input works without ROCK or a skeleton provider.
- Input yields while Mods Config FO4 VR's menu is open (`src/ModsConfigMenu.cpp`, read through its public `getMenuState`). Both mods hide controller buttons from every other reader of the shared IVRSystem vtable, so only one of them may take input at a time. Its hooks of the same call sites (main tick, scene-depth commit, controller polls, window procedure) chain with ours in either load order; keep every hook here chaining onto a foreign detour rather than refusing it.
- `RPS_UI.toml` is edited on RPS UI's Mods Config FO4 VR page. Its sliders' ranges are the clamps `src/UiSettings.cpp` applies, so a range changes in both together; the Theme choice takes its options from the DLL (the theme files, and the theme in use without one). Live previews reach `UiSettings` on the game thread, which keeps them under a lock and wakes the watcher; only the watcher reads the settings store and lays the previews over the file's values. A preview's Discard, a Save and the menu closing end it. Without Mods Config nothing is called and the file alone applies.
- Keyboard capture (`src/KeyboardCapture.cpp`) changes no engine state: it withholds raw key presses and `WM_CHAR` from the game window while held. Key releases always pass, or a key would stay held in the game's key table.
- The settings holotape opens the menu through `TESEquipEvent`, registered through FO4VR's equip-sink function at RVA 0x41E090 after a live byte check; a mismatch leaves the holotape inert and logged.
- `RPS_UI_Framework.esp` is ESL-flagged. The holotape's runtime ID is built from the plugin file's own compile indices, never CommonLib's `LookupForm`, which cannot see light plugins on FO4VR's light-plugin loaders.
- Players see the settings menu, its header and its holotape as the ROCK Family Configurator. The holotape is given and works whether or not RobCo PALM is loaded.
- Screen-positioned drawing does not exist. World drawing is anchored to world points, and text is projected per eye.
- The host owns every panel's Dear ImGui context, renderer, input feeding, frame, texture resolution and theme; a consumer never receives a render target or device context, and declares only its layout size. A consumer's Dear ImGui build must match the host's (`ImGuiBuild.h`), or its panel is refused and the refusal is logged.
