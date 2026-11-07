# M5 editor wave: physics tools in lrgeditor (read with brief.md and v081/brief-w4.md)

The M5 engine is COMMITTED (`git log --oneline 8992a0c..HEAD`): LrgPhysicsShape + lrg-collide,
LrgBroadphase + LrgPhysicsLayers (project.ryaml `physics:` block, `lrg_project_get_physics_layers()`),
the LrgDynamicsWorld solver (debug_draw, queries, signals), LrgJoint, LrgPhysicsAsset (.rphysasset) +
LrgRagdoll, the components (rigid-body3d, collider3d, joint3d, cloth3d, destructible3d), the node
`physics:` block (LrgNodePhysics: body, shapes, joints; lrg_node_get/set_physics), LrgCloth, LrgFracture,
2D LrgPhysicsWorld debug draw. Read docs/guides/physics.org, docs/modules/physics/*.org,
docs/modules/level/node-physics.org and the M2-M4 editor tools for patterns (environment panel,
material editor, gizmo-paint volume handles, inspector sections, editorctl/MCP rows).
Screenshots: AGENTS.md > Testing > GPU tests, editor runs and screenshots (build-aux/xrun.sh,
1920x1080, dark + light). Shell files (lrg-editor-app.c, -window.c, -app-actions.c) stay the LEAD's:
send exact patches.

Two agents: EDPHYS (in-level physics editing) and EDPHYSASSET (layers + physics assets + CLI).

## EDPHYS
Own: src/editor/ui/lrg-editor-physics-panel.{c,h} (stub; docked panel for the selected node's
`physics:` block: body kind/mass/material/damping/layer picker; shapes list (add/remove/reorder,
kind, dims, offset/rotation, trigger, per-shape layer); joints list (kind, target picker by
hierarchy/GUID, anchor/axis, limits, spring, motor, break force); "Fit shape to mesh" (box/sphere/
capsule/convex from the node's model bounds/points); every edit one undoable level command; play
mode shows live body state (velocity, sleeping, contacts)), src/editor/ui/lrg-editor-gizmo-paint.{c,h}
+ src/editor/lrg-editor-gizmo.c (collider gizmos for every shape kind with drag handles for half
extents/radius/half height, joint gizmos (anchor, axis, limit arcs/cones), 2D collider gizmos for the
2D `collider` component desc (box/circle) in 2D levels), src/editor/ui/lrg-editor-viewport-panel.{c,h}
(Physics overlay menu: shapes, contacts, AABBs, joints, sleeping, centre of mass while playing via
lrg_world3d_debug_draw_physics / 2D lrg_physics_world_debug_draw; also while authoring: shapes from
the physics blocks), src/editor/ui/lrg-editor-inspector-panel.c (a compact Physics summary section
linking to the panel), src/editor/lrg-editor-node-source.c (if physics rows belong there),
src/editor/ui/lrg-editor-hierarchy-panel.c (physics badge on nodes with a body),
tests/test-editor-app-physics.c. IPC `physics.*` (get, set body fields, add/remove/set shape, add/
remove/set joint, fit, debug overlay flags). Docs: docs/editor/physics.org (new), viewport/inspector
sections, ipc-protocol.org section. Send the lead your IPC list early (EDPHYSASSET writes the CLI).

## EDPHYSASSET
Own: src/editor/ui/data/lrg-editor-physics-asset-editor.{c,h} (stub; data editor for `.rphysasset`:
skeleton/model picker (glTF skin), per-bone part list with shape kind/dims/offset/mass, joint kind
and limits with in-preview gizmos (twist arcs, swing cones), "Generate from skeleton" (total mass,
radius ratio), "Stabilize masses", exclusions, Simulate button (passive ragdoll drop in the preview
with reset), live 3D preview using LrgRagdoll + debug draw; undoable edits through the document),
src/editor/lrg-editor-document.{c,h} + src/editor/ui/data/lrg-editor-data-editor.c (append
PHYSICS_ASSET ".rphysasset" format + builtin), src/editor/ui/lrg-editor-dialogs.c (Project Settings
gets a "Physics" page: layer names (32 slots), the symmetric collide matrix grid (click to toggle,
row/column highlight), gravity and solver settings kept in the `physics:` block; writes through
LrgProject with undo or an explicit Apply), src/editor/ui/lrg-editor-assets-panel.{c,h} (New
Physics Asset entry + "Physics Asset from Model" on .glb), src/editorctl/** (lrgeditorctl verbs +
MCP tools for your methods AND EDPHYS's `physics.*` list, relayed by the lead), tests/editorctl-smoke.sh,
tests/test-editor-app-physasset.c. IPC `physasset.*`, `physics.layers.*` (get, set name, set
collides, settings). Docs: docs/editor/physics-asset-editor.org (new), project settings section,
ipc-protocol.org + lrgeditorctl.org sections.

## Gate (each agent)
`make all` 0 warnings (GIR), your suites + test-editor-app* you touch + test-editor-app green (GPU
under build-aux/xrun.sh), `make test-editorctl` and `make test-emacs` (EDPHYSASSET), docs-check clean
for your headers, dark+light 1920x1080 screenshots looked at, kept in
`/tmp/claude-1000/-var-home-zach-source-projects-libregnum/c474e1e8-b6e5-4b4b-bd42-fe50c0433c3f/scratchpad/v090/shots/m5e-<agent>-*.png`. Skill text (SKILL.md "Edit physics in the
editor", references/ipc-protocol.md, lrgeditorctl.md, mcp.md) in your report.
