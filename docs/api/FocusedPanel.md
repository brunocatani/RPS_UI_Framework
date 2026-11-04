# FocusedPanel

Header: `RPS_UI_Framework/FocusedPanel.h`. Namespace: `rpsui::api::focused_panel`. Version 2.0.

Registers a panel that keeps its submitted world pose and takes input only while the selected visible pointer is inside it. `registerPanel` takes a Framework `PanelRegistration` and an owner token, and returns a panel handle.

- The panel must set `FixedSize`. The host does not separate it automatically from other panels.
- Only the pointing hand's stick is captured.
- A UI button press is drained on exit until release, without capturing the stick or dispatching a click to a different panel.
- Opening and focus changes require the button to return to neutral before a click.

Submit poses and read state through the [Framework](Framework.md) interface, and retire the panel through the [Cooperation](Cooperation.md) interface. Contexts must survive `CallbackBusy`.
