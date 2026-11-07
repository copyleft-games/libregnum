# Wave 4 brief: the lrgeditor panels (read AFTER brief.md, which still applies)

Read `/tmp/claude-1000/-var-home-zach-source-projects-libregnum/c474e1e8-b6e5-4b4b-bd42-fe50c0433c3f/scratchpad/v081/brief.md` (rules, lock, style), the plan
`/var/home/zach/.claude/plans/take-a-look-at-hidden-iverson.md`, and `git log --stat master..HEAD`.
Everything before you is COMMITTED: the toolkit pieces (DnD, property grid, view helpers, menu hooks),
the headless editor core (view state, viewport nav, play session, asset meta, VCS, linked prefabs,
editor documents, node property source, actions bridge, settings + migration) and the NEW SHELL
(commit "feat(editor)!: lrgeditor shell on the retained UI toolkit"): read
`src/editor/ui/lrg-editor-app.h`, `lrg-editor-app-private.h`, `lrg-editor-window.h`,
`lrg-editor-viewport-panel.h`, `lrg-editor-scene-renderer.h`, `tests/editor-app-fixture.h`, and
`docs/editor/editor-app.org` (Architecture + Embedding sections) and `docs/editor/view-model.org`.
The old immediate-mode panels you are replacing were deleted; read them from master for behaviour
parity: `git show master:src/editor/ui/lrg-editor-panel-<name>.c` (hierarchy, inspector, viewport,
assets, console, tiles, timeline), `master:src/editor/ui/lrg-editor-render.c`,
`master:src/editor/ui/lrg-editor-dialogs.c`, `master:src/editor/ui/lrg-editor-app.c`. Match or exceed
every 0.8.0 feature listed in docs/editor/*.org for your panel (do not drop features).

## Rules specific to Wave 4
- Six agents run in parallel. You own ONLY your files (table below). The shell files
  (lrg-editor-app.{c,h}, app-private.h, app-actions.c, lrg-editor-window.{c,h}) are the LEAD's:
  do not edit them. Your panel's constructor `lrg_editor_X_panel_new (LrgEditorApp *app)` is called by
  the window; register everything your panel needs FROM YOUR OWN FILES at construction time:
  - actions: `lrg_editor_actions_add` on `lrg_editor_app_get_actions (app)` (re-adding an existing id
    REPLACES it — use this to give real callbacks to placeholder actions such as tools.palette,
    tools.quick-open, edit.preferences, node.rename, edit.find);
  - IPC methods: `lrg_editor_server_register_method` on the app's server (see lrg-editor-server.h; if
    the server can be NULL at panel construction, also handle late registration or report it);
  - signals on the editor/level/selection/view state/play session.
  If you truly need a shell change (a getter, a hook, a menu entry), write the exact patch in your
  report instead of editing; the lead applies it.
- Panels are final LrgContainer subclasses that fill their LrgDockPanel. Flashy but clean: theme
  colours/metrics, springs, icons (lrg_paint_list_icon), the new toolkit widgets. Every panel must look
  right in all 5 presets plus catppuccin-macchiato/frappe (data/editor/themes).
- Tests: your own `tests/test-editor-app-<x>.c` (stubs exist and are registered) built on
  tests/editor-app-fixture.h (read-only for you; if you need a helper, copy it into your test file).
  Drive through the canvas (clicks on widget rects, keys, drags via the DnD session) and IPC.
  Also re-run tests/test-editor-app (shell suite) — it must stay green; its /editor-app/palette case
  is skipped until P6 lands.
- Docs: update/write the docs/editor page sections for your panel (editor-app.org has a section per
  panel? if not, add a section "Panels > <Name>" — coordinate: append-only edits in editor-app.org,
  one contiguous section per agent, headed `** <Panel name> panel` with a :CUSTOM_ID: editor-panel-<x>).
- Visual check: run lrgeditor under the isolated Xvfb on a sample project (create one with
  `build/release/lrgeditor --new-project DIR --name Demo` in the scratchpad, add nodes via lrgeditorctl
  or IPC), screenshot with `lrgeditorctl screenshot PATH --window` (see docs/editor/lrgeditorctl.org),
  LOOK at the PNGs, fix ugliness. Keep 2-3 final screenshots in
  `/tmp/claude-1000/-var-home-zach-source-projects-libregnum/c474e1e8-b6e5-4b4b-bd42-fe50c0433c3f/scratchpad/v081/shots/<panel>-*.png`. Run the editor with
  `env -u WAYLAND_DISPLAY XDG_SESSION_TYPE=x11 XDG_RUNTIME_DIR=<short empty scratch dir>` under xrun.sh
  (GLFW otherwise grabs the Wayland session), never in the user's session.

## Ownership
| Agent | Owns |
|---|---|
| P1 hierarchy | src/editor/ui/lrg-editor-hierarchy-panel.{h,c}, src/editor/lrg-editor-node-model.{h,c}, tests/test-editor-node-model.c, tests/test-editor-app-hierarchy.c |
| P2 inspector | src/editor/ui/lrg-editor-inspector-panel.{h,c}, src/editor/lrg-level-serializer.c + src/editor/lrg-level-instantiate.c (enum/flags nick fix only), src/ui/lrg-spin-box.{h,c}, src/ui/lrg-checkbox.{h,c} (additive), tests/test-editor-app-inspector.c, + an `inspector.*` IPC method set registered from your panel file |
| P3 viewport | src/editor/ui/lrg-editor-viewport-panel.{h,c}, lrg-editor-viewport-split.{h,c}, lrg-editor-scene-renderer.{h,c}, lrg-editor-gizmo-paint.{h,c}, data/editor/shaders/ (new), tests/test-editor-app-viewport.c |
| P4 assets | src/editor/ui/lrg-editor-assets-panel.{h,c}, lrg-editor-thumbnails.c, tests/test-editor-app-assets.c |
| P5 utility | src/editor/ui/lrg-editor-console-panel.{h,c}, lrg-editor-profiler-panel.{h,c}, lrg-editor-timeline-panel.{h,c}, lrg-editor-tiles-panel.{h,c}, src/ui/lrg-canvas-input.{h,c} (input latch), tests/test-editor-app-panels.c |
| P6 dialogs+code | src/editor/ui/lrg-editor-dialogs.c, lrg-editor-preferences.{h,c}, lrg-editor-palette.{h,c}, lrg-editor-code-panel.{h,c}, src/editorctl/** (verbs + MCP tools for the new IPC methods), skills references are NOT yours, tests/test-editor-app-dialogs.c, tests/editorctl-smoke.sh |
