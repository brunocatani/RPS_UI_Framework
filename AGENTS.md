# RPS UI Framework

Shared world-space UI and VR controller interaction for Fallout 4 VR. It hosts world panels, owns all world drawing (gameplay indicators and debug shapes) and publishes controller input. It has no menus of its own; individual mods supply them.

## Layout

- `include/RPS_UI_Framework/`: the public API, the only copy of these headers. They include only the C++ standard library and each other.
- `docs/api/`: one page per interface, starting at `Overview.md`.
- `src/`: the plugin. `Api.cpp` holds the tables, discovery and retired exports. `FrameworkRuntime.cpp` hosts panels and `InputService.cpp` publishes input. `WorldDrawStore.cpp` owns world drawing.
- `tests/`: targeted tests. `ConsumerExample.cpp` is the minimal consumer, compiled with the public header test.

## Build and tests

Requires Windows x64, Visual Studio 2022, CMake, vcpkg with `VCPKG_ROOT` set, and checkouts of CommonLibF4VR, F4VR-CommonFramework and RPS Framework. Point `RPS_FRAMEWORK_INCLUDE_DIR` at RPS Framework's `include/` when it is not where `CMakeLists.txt` looks by default.

- Build with `cmake --preset custom-fast`, then `cmake --build build-fast --config Release`.
- Build tests with `cmake --build build-fast --config Release --target build_tests`, then run `ctest --test-dir build-fast -C Release --output-on-failure`.

## Runtime files

The plugin is `F4SE/Plugins/RPS_UI_Framework.dll`, deployed under the mod folder set by `COPY_PLUGIN_BASE_PATH` in `CMakeUserPresets.json`. It logs to `RPS_UI_Framework.log` in the game's F4SE log folder.

## What it does not own

Menus, settings pages and their persistence belong to the mods that show them. RobCo PALM owns the wheel and persists the shared pointer aim. Consumers own their panel contents and keep their callback data alive until a safe unregister returns `Ok`.

## Invariants

- A consumer binds at runtime through `RPSUI_QueryInterfaceV1` and never links the plugin or copies its headers.
- Each interface header declares `kMajor` and `kMinor`, and each interface has one page in `docs/api/`, updated in the same commit as the header.
- Released contracts never change. Retired export names, interface IDs, slot meanings, enum values and flag bits are never reused.
- Every version refusal and retired-export call is logged with the calling DLL and stops the game at the main menu with an error box (`src/RequiredUpdates.cpp`).
- Native controller input works without ROCK or a skeleton provider.
- Keyboard capture (`src/KeyboardCapture.cpp`) changes no engine state: it withholds raw key presses and `WM_CHAR` from the game window while held. Key releases always pass, or a key would stay held in the game's key table.
- Screen-positioned drawing does not exist. World drawing is anchored to world points, and text is projected per eye.
