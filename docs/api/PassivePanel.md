# PassivePanel

Header: `RPS_UI_Framework/PassivePanel.h`. Namespace: `rpsui::api::passive_panel`. Version 2.0.

Registers a visual-only panel. The host never hit-tests it, captures input for it, resizes it or separates its pose from other panels. `registerPanel` takes a Framework `PanelRegistration` and an owner token, and returns a panel handle. The registration must set `Transparent` and `FixedSize` and provide a render callback.

Submit poses through the [Framework](Framework.md) interface and retire callbacks through the [Cooperation](Cooperation.md) interface's safe unregister calls.
