#pragma once
#include <RPS_UI_Framework/Framework.h>

// The settings menu's two panels (the menu and its keyboard) as RPS UI runs them: their
// setup and draw callbacks. The game registers them with RPS UI's Framework table
// (FrameworkPanelRenderer.cpp); the desktop preview runs them in a LocalPanelHost.
namespace devui::render
{
    struct PanelCallbacks
    {
        rpsui::api::framework::PanelSetupCallback setup{ nullptr };
        rpsui::api::framework::PanelDrawCallback draw{ nullptr };
        void* userData{ nullptr };
    };
    [[nodiscard]] PanelCallbacks panelCallbacks(bool keyboard) noexcept;
    // Forgets both panels' contexts after their registrations ended.
    void resetPanelStates() noexcept;
}
