# Settings menu

`SettingsMenu.h`, interface `SettingsMenu`, version 2.0.

RPS UI's settings menu, shown to players as the **ROCK Family Configurator**, holds the Spawner, the Console and the pages mods add on one panel. Mods' settings are not in it: each mod has its own page in Mods Config FO4 VR. Players open it by playing the **ROCK Family Configurator** holotape in the Pip-Boy (`RPS_UI_Framework.esp` gives the player one whenever a game starts or loads without it). A mod can open it as well, as RobCo PALM's wheel does, and add pages of its own to the menu's top bar.

The table needs an owner token from `framework::Api::registerConsumer`.

## Opening and closing

```cpp
const rpsui::api::settings_menu::Api* menu{};
rpsui::api::acquire(query, menu);

rpsui::api::settings_menu::OpenRequest request{};
request.pose = wheelPose;   // or leave hasPose 0: in front of the headset
request.hasPose = 1;
menu->open(owner, &request);
```

| Function | Thread | Result |
| --- | --- | --- |
| `open(owner, request)` | Game | `Ok`; `FrameworkNotReady` outside gameplay or before the host is ready; `ResourceConflict` while the menu is open or opening; `InvalidPose` for a pose RPS UI cannot place a panel at; `OwnerNotRegistered` for an unknown owner |
| `close(owner)` | Game | `Ok` |
| `isOpen()` | Any | The menu is open or opening |
| `closedCount()` | Any | A count that increases each time the menu closes; compare it with the last value you saw to refresh what the Spawner or the Console may have changed |

The menu's size is the player's last menu size; a pose sets only where it appears. It opens the first time on its home tab, the first page a mod added or else the Spawner, and afterwards on the tab it was closed on.

## Pages

A page is a tab after Spawner and Console. It draws with the mod's own Dear ImGui copy, exactly like a [Framework](Framework.md) panel: the registration carries the mod's `ImGuiBuild`, and each frame passes the host's context through `imgui`, which the page binds with `RPSUI_IMGUI_BIND` before any ImGui call.

```cpp
bool RPSUI_CALL drawPage(const rpsui::api::settings_menu::PageDrawFrame* frame, void* user) noexcept
{
    RPSUI_IMGUI_BIND(frame->imgui);
    ImGui::TextUnformatted("My page");
    return false;   // Back not used: the menu goes back
}

rpsui::api::settings_menu::PageRegistration page{};
std::snprintf(page.pageId, sizeof(page.pageId), "mymod.page");
std::snprintf(page.title, sizeof(page.title), "My page");
page.sortOrder = 10;
page.imgui = RPSUI_IMGUI_BUILD();
page.drawCallback = &drawPage;
std::uint64_t handle{};
menu->registerPage(owner, &page, &handle);
```

- `drawCallback` runs on the render thread inside a child window that fills the page area. `width` and `height` are that area in the menu's layout units; `fonts` are the theme's faces by `framework::FontRole`. A page adds no fonts of its own; `d3dDevice` is for the page's own textures.
- `backPressed` is 1 on the frame after a Back press while the page is shown. Return true when the page used it (closing a sub-view of its own); otherwise the menu goes back: to its home tab, or closed when the page is the home tab.
- Pages are ordered by `sortOrder`, then by registration. At most `kMaxPages` (8) pages exist at once; one more returns `CapacityReached`. A `pageId` already registered returns `ResourceConflict`.
- `unregisterPage(owner, handle)` returns `CallbackBusy` while the page's callback is running; call it again. After `Ok` the callback is never called again and `userData` may be freed. Unregistering the consumer removes its pages.
