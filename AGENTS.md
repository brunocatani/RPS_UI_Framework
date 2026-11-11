# RPS UI Framework

Shared world-space UI and VR controller interaction for Fallout 4 VR. It hosts world panels and draws them with Dear ImGui, owns all world drawing (gameplay indicators and debug shapes) and publishes controller input. It has no menus of its own; individual mods supply them.

## Layout

- `include/RPS_UI_Framework/`: the public API, the only copy of these headers. They include only the C++ standard library and each other.
- `support/RPS_UI_Framework/`: code shared by the host and consumers' desktop previews and tests, kept out of `include/` because it is not runtime API. `ImGuiPanelHost.h` runs a panel's Dear ImGui frame, `ThemeStyle.h` turns a theme into ImGui's style (both include Dear ImGui), `ThemeDefaults.h` holds the built-in themes (Pip-Boy, the default, New Vegas and Vault-Tec; standard library only), and `ThemeStyle.h` also has `pushChosen`, the shared look of a chosen choice, and `BindingText.h` (standard library only) and `BindingIcons.h` show controller bindings as sentences and icons.
- `docs/api/`: one page per interface, starting at `Overview.md`. `docs/api/Migration.md` updates a mod built against RPS UI Framework 1.1 (the RPS SDK's `RPSUI*Api.h` headers); ROCK's `skills/rock-1.0-migration/SKILL.md` covers it with ROCK, PAPER and RobCo PALM. `docs/Themes.md` is the players' and theme makers' guide.
- `src/`: the plugin. `Api.cpp` holds the tables, discovery and retired exports. `FrameworkRuntime.cpp` hosts panels, `render/PanelImGui.cpp` owns their Dear ImGui contexts and `InputService.cpp` publishes input. `UiSettings.cpp` reads `RPS_UI.toml` and `ThemeStore.cpp` the theme files, on a watcher thread that publishes immutable snapshots. `WorldDrawStore.cpp` owns world drawing.
- `tests/`: targeted tests. `ConsumerExample.cpp` is the minimal consumer, compiled with the public header test.

## Build and tests

Requires Windows x64, Visual Studio 2022, CMake, vcpkg with `VCPKG_ROOT` set, and checkouts of CommonLibF4VR, F4VR-CommonFramework and Dear ImGui at the revision `docs/api/Framework.md` names, and RPS Framework's staged package (built from RPS Framework). Point `RPSFramework_DIR` at the package's CMake directory and `IMGUI_DIR` at the Dear ImGui checkout when they are not where `CMakeLists.txt` looks by default.

- Build with `cmake --preset custom-fast`, then `cmake --build build-fast --config Release`.
- Build tests with `cmake --build build-fast --config Release --target build_tests`, then run `ctest --test-dir build-fast -C Release --output-on-failure`.

## Runtime files

The plugin is `F4SE/Plugins/RPS_UI_Framework.dll`, deployed under the mod folder set by `COPY_PLUGIN_BASE_PATH` in `CMakeUserPresets.json`. It logs to `RPS_UI_Framework.log` in the game's F4SE log folder. Its settings are `RPS_UI.toml` in `Mods_Config\RPS_UI_Framework` under the player's Documents game folder, with themes in `Themes\` and theme fonts in `Fonts\` beside it.

## What it does not own

Menus, settings pages and their persistence belong to the mods that show them. RobCo PALM owns the wheel and persists the shared pointer aim. Consumers own their panel contents (their ImGui calls, fonts, styles and textures) and keep their callback data alive until a safe unregister returns `Ok`.

## Invariants

- A consumer binds at runtime through `RPSUI_QueryInterfaceV1` and never links the plugin or copies its headers.
- Each interface header declares `kMajor` and `kMinor`, and each interface has one page in `docs/api/`.
- Every version refusal and retired-export call is logged with the calling DLL and stops the game at the main menu with an error box (`src/RequiredUpdates.cpp`).
- Native controller input works without ROCK or a skeleton provider.
- Input yields while Mods Config FO4 VR's menu is open (`src/ModsConfigMenu.cpp`, read through its public `getMenuState`). Both mods hide controller buttons from every other reader of the shared IVRSystem vtable, so only one of them may take input at a time. Its hooks of the same call sites (main tick, scene-depth commit, controller polls, window procedure) chain with ours in either load order; keep every hook here chaining onto a foreign detour rather than refusing it.
- Keyboard capture (`src/KeyboardCapture.cpp`) changes no engine state: it withholds raw key presses and `WM_CHAR` from the game window while held. Key releases always pass, or a key would stay held in the game's key table.
- Screen-positioned drawing does not exist. World drawing is anchored to world points, and text is projected per eye.
- The host owns every panel's Dear ImGui context, renderer, input feeding, frame, texture resolution and theme; a consumer never receives a render target or device context, and declares only its layout size. A consumer's Dear ImGui build must match the host's (`ImGuiBuild.h`), or its panel is refused and the game stops at the main menu.
