#pragma once

/*
 * RPS UI Framework's client of Mods Config FO4 VR's native API. ModsConfigFO4Api.h beside it
 * is Mods Config's own client header, copied as is: refresh it from Mods Config's src/api/
 * when its API grows, never edit it.
 *
 * The client connects under "RPS_UI_Framework", the modName of the page in
 * data/mod/ModsConfigFO4/Config/RPS_UI_Framework/spec.json, which edits RPS_UI.toml.
 * It gives the Theme choice its options, the theme files in the Themes folder, and
 * turns each change of a live setting into a preview (UiSettings.h): shown before a
 * Save, ended by its Discard, a Save or the menu closing. Without Mods Config nothing
 * is called and RPS UI reads its file as before.
 *
 * Threads: Mods Config calls on the game's main thread. The client never touches the
 * settings store, which only the settings watcher thread reads; it hands each preview
 * to UiSettings, which keeps it under a lock and wakes the watcher to publish the next
 * snapshot.
 */
namespace rpsui::mods_config_client
{
    // Game thread, from F4SE's kPostLoad on, after settings::start: adds the page's options
    // and handlers and connects. Logs and returns when Mods Config is not installed.
    void connect() noexcept;
}
