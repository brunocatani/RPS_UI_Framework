# Registry

Header: `RPS_UI_Framework/Registry.h`. Namespace: `rpsui::api::registry`. Version 2.0.

Read-only discovery of every registered consumer and panel, with the current runtime budgets. `getPage` can be called from a control thread, including before the host is ready.

## Paging

Start both cursors in `Query` at zero. After each call, pass `nextOwnerToken` and `nextPanelHandle` back as `afterOwnerToken` and `afterPanelHandle`. Stop when `moreConsumers` and `morePanels` are both zero.

Entries are ordered by monotonically increasing identity, not draw order. Removed identities are never reused.

## Consistency

Each page is an atomic advisory copy. `registryRevision` increases only on registration and removal, not on pose or input changes. If revisions differ between pages, a caller that needs one membership census restarts on a later control tick and never spins. Pose and input may change between pages even when the revisions match.

## Budgets

`consumerCapacity`, `panelCapacity` and `inputSubscriberCapacity` are the runtime budgets. `totalConsumers` and `totalPanels` are the current totals. `kPageConsumers` and `kPagePanels` are only the size of one page.
