# RPS UI Framework API

RPS UI Framework publishes nine interfaces from `include/RPS_UI_Framework/`. The headers include only the C++ standard library and each other. They contain no CommonLibF4VR, F4SE or engine types. `ImGuiBuild.h` holds the Dear ImGui configuration panel consumers build with and two macros that expand to Dear ImGui calls in your own code; see [Drawing](Framework.md#drawing).

| Interface | Header | Purpose | Page |
| --- | --- | --- | --- |
| Framework | `Framework.h` | Consumers, world panels drawn with Dear ImGui, poses and panel state | [Framework](Framework.md) |
| Cooperation | `Cooperation.h` | Render-resource agreements, the host directory, safe retirement | [Cooperation](Cooperation.md) |
| Focused panels | `FocusedPanel.h` | Fixed-size panels that take input only while pointed at | [FocusedPanel](FocusedPanel.md) |
| Passive panels | `PassivePanel.h` | Visual-only panels | [PassivePanel](PassivePanel.md) |
| Input | `Input.h` | Controller frames, button capture and pointer aim | [Input](Input.md) |
| Number overlays | `NumberOverlay.h` | Numbers the host draws itself | [NumberOverlay](NumberOverlay.md) |
| Registry | `Registry.h` | Paged read-only census of consumers and panels | [Registry](Registry.md) |
| Settings menu | `SettingsMenu.h` | Opening the settings menu and adding pages to it | [SettingsMenu](SettingsMenu.md) |
| World drawing | `WorldDraw.h` | World-space spheres, lines, text and meshes | [WorldDraw](WorldDraw.md) |

Each interface header declares its own `kMajor` and `kMinor`. Every interface is at major 2.

## Building against the headers

Add this repository's `include/` directory through a CMake cache variable, for example `RPSUI_INCLUDE_DIR`, and include `<RPS_UI_Framework/...>`. Do not copy the headers into your project. Do not link RPS UI Framework; bind it at runtime.

## Binding at runtime

Resolve `RPSUI_QueryInterfaceV1` from the loaded `RPS_UI_Framework.dll`, then acquire each table you use. `Client.h` is a header-only helper that requests exactly the version and table size your headers declare.

```cpp
#include <RPS_UI_Framework/Client.h>
#include <RPS_UI_Framework/WorldDraw.h>

const auto module = GetModuleHandleW(rpsui::api::kModuleName);
const auto query = module ? reinterpret_cast<rpsui::api::QueryInterfaceV1>(
    GetProcAddress(module, rpsui::api::kQueryExportName)) : nullptr;

const rpsui::api::world_draw::Api* draw{};
if (rpsui::api::acquire(query, draw) == rpsui::api::Result::Ok) {
    // Every function in the table is set.
}
```

Tables and descriptors stay valid for the life of the process. Discovery can be called from any thread.

`tests/ConsumerExample.cpp` is a minimal consumer that is compiled with the tests.

## Version negotiation

`RPSUI_QueryInterfaceV1(id, exactMajor, minimumMinor, minimumTableBytes, &descriptor)` serves an interface when the major matches exactly, the minor is at least the one requested and the table is at least the size requested.

| Result | Meaning |
| --- | --- |
| `Ok` | `descriptor` holds the table and its version |
| `UnknownInterface` | This build has no interface with that ID |
| `UnsupportedMajor` | The interface exists at another major; no table is returned |
| `UnsupportedMinor` | Your headers are newer than this build |
| `TableTooSmall` | Your table is larger than this build's |

Major 1 is refused. RPS UI Framework logs every version refusal with the calling DLL, then stops the game at the main menu with an error box that names the mod to update.

The pre-2.0 exports (`RPSUI_RequestApi` and the other `RPSUI_Request*` functions) stay exported, return no table and report the same stop. Their names are never reused. A mod built against 1.1 is updated with [Migration](Migration.md).

## Records and results

Every record a consumer passes in starts with `size` and `version`. Both are initialized by the record's default member initializers, so start from a value-initialized record and set only the fields you need. A record that is too small is rejected with `InvalidArgument`. A record with another `version` is rejected with `VersionMismatch`, or with `InvalidArgument` on the Framework interface's registration and presentation calls.

Functions return `rpsui::api::Result`, listed in `Abi.h`, unless the table documents a `bool` or integer return.
