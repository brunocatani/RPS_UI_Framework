# Cooperation

Header: `RPS_UI_Framework/Cooperation.h`. Namespace: `rpsui::api::cooperation`. Version 2.0.

Render-resource agreements, a directory of the host's state and safe retirement of panels and consumers whose callbacks may still be running. It uses the owner token from the Framework interface.

## Render agreements

`registerPanel` takes a `PanelRegistration`, which wraps a Framework panel registration with a `RenderStage` and the `requestedAccess` bits, and fills a `PanelAgreement` with the panel handle, `grantedAccess`, `conflictingAccess` and `conflictOwnerId`.

The access bits are claim scopes, not a D3D sandbox. Scene access and direct hook ownership (`kHostExclusiveAccess`) cannot be granted to a panel callback, which uses only its private color target (`kPanelAccess`).

## Snapshot

`getSnapshot` fills a `Snapshot` with the hook status, whether the framework is ready, which hands are suppressed (left bit 0, right bit 1), the focused and resizing panels, and fixed-size lists of consumers (`kSnapshotConsumers`) and panels (`kSnapshotPanels`). Panels are sorted by `(sortOrder, panelHandle)`, matching composition order.

These are snapshot dimensions, not runtime budgets. Read the budgets and the complete lists from the [Registry](Registry.md) interface.

## Safe retirement

`unregisterPanelSafely` and `unregisterConsumerSafely` are nonblocking. `CallbackBusy` closes admission immediately: retry on a later control tick, free callback data only after `Ok`, and never spin.
