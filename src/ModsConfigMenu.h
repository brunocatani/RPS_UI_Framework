#pragma once

// Mods Config FO4 VR (ModsConfigFO4VR.dll) is another world-space settings menu that
// reads the controllers through the shared IVRSystem vtable. While its ray is on its
// menu it hides that whole wand from every other reader, and RPS UI hides the buttons
// its pointer uses the same way, so with both taking input a press can reach neither
// menu or the wrong one. Its native API reports the menu state to any DLL, with or
// without a client, so the input service yields while that menu is open and takes
// input back when it closes or is minimized for a mod's own UI.
namespace rpsui::mods_config_menu {
// Game thread only, as the Mods Config API is. Binds on the first call, after every
// F4SE plugin is loaded; false while Mods Config is absent or its API failed.
[[nodiscard]] bool isOpen() noexcept;
}
