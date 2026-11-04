# WorldDraw

Header: `RPS_UI_Framework/WorldDraw.h`. Namespace: `rpsui::api::world_draw`. Version 2.0.

World drawing for gameplay indicators and developer diagnostics. Every position is in game units in world space. Everything is drawn into both eyes after UI panels with the depth test off, so it stays visible through geometry. Text is anchored to a world point; there is no screen-positioned drawing.

It uses an owner token from the [Framework](Framework.md) interface. The host copies every array during the call and owns all GPU resources; nothing is borrowed.

## Publishing

`submit` takes a `Publication` of `Sphere`, `Line`, `Text` and `MeshInstance` arrays and replaces the owner's previous publication. It stays visible for `leaseFrames` rendered frames (1 to `kMaxLeaseFrames`) unless it is replaced or cleared. Republish every frame for continuous display. An empty publication clears, and so does `clear`.

A publication above any per-owner capacity (`kMaxSpheres`, `kMaxLines`, `kMaxTexts`, `kMaxMeshInstances`) is rejected whole.

## Text

Text uses 5x7 block glyphs: A-Z, 0-9 and `- + = . , : / ( )`. Lower case draws as upper case, and other characters leave a blank cell. Each eye projects the anchor and draws the glyphs at that point, offset and sized in eye-texture pixels and kept inside the eye.

## Meshes

`registerMesh` takes a `Mesh`: a triangle list in mesh-local game units, where `positions` holds `vertexCount` xyz triples and every index is below `vertexCount`. Registering an existing ID replaces its geometry. Meshes belong to the owner until released with `releaseMesh` or until the owner unregisters. Capacities are `kMaxMeshes`, `kMaxMeshVertices`, `kMaxMeshIndices` and `kMaxMeshBytes`.

A `MeshInstance` names a mesh by ID; an unknown ID is skipped. `transform` uses the row-vector convention (position * transform), with the translation in elements 12 to 14, as `DirectX::XMFLOAT4X4` stores it.

## Frame callback

A producer whose data is final only late in the frame, for example after the physics step, registers a callback with `setFrameCallback` and submits from it.

The callback runs on the render thread once per drawn frame, before the host takes its snapshot. It may call `submit`, `clear`, `registerMesh` and `releaseMesh` for its own owner and must not call other RPS UI functions. Submitting from it with `leaseFrames = 1` shows the data for exactly this frame. It must stay brief, because it delays the frame.

`setFrameCallback` replaces the owner's callback, and null removes it. After the call returns, the previous callback is not started again. `CallbackBusy` means it is still running: retry until `Ok` before releasing its context. An owner with a callback must also retry its unregister until `Ok`.
