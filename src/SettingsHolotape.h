#pragma once

/*
 * The "RPS UI Settings" holotape (RPS_UI_Framework.esp, local form 0x800): the settings
 * menu's way in without a wheel. The player gets one whenever a session starts without it;
 * playing it in the Pip-Boy opens the menu in front of the headset as soon as gameplay input
 * is back.
 *
 * Playing a holotape equips it, so the engine's TESEquipEvent reports it. FO4VR registers
 * equip sinks through the function at RVA 0x41E090 (it fetches the equip event source and
 * adds the sink); the VR address library has no entry for CommonLib's event-source ID. Its
 * bytes are checked before the call, and a mismatch leaves the holotape inert and logged.
 */
namespace rpsui::settings_holotape
{
    // kGameDataReady: finds the holotape and registers the equip sink. Main thread.
    void onGameDataReady() noexcept;
    // kNewGame and kPostLoadGame: gives the player a holotape when they have none. Main thread.
    void onSessionReady() noexcept;
    // Every gameplay input frame (game thread): true once after the holotape was played.
    [[nodiscard]] bool takePlayed() noexcept;
}
