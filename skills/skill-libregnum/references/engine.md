# Working on libregnum itself

Authoritative guide: `AGENTS.md` at the root of the checkout (read it before changing code), plus `docs/` (org-mode). This page is the short version and the traps that have actually bitten.

## Layout

| Path | What |
|------|------|
| `src/core/` | `LrgEngine` singleton, `LrgRegistry` (type names -> GTypes; engine types pre-registered at startup), data loader (YAML), asset manager/packs, `lrg-math3d` (shared Euler/quaternion/matrix math, TRS compose/decompose, look-at, projections; plain float-array functions) |
| `src/ecs/` | `LrgWorld`, `LrgGameObject`, `LrgComponent` and built-in components |
| `src/graphics/`, `src/scene/`, `src/shapes/`, `src/lighting/`, `src/postprocess/` | Rendering on graylib/raylib |
| `src/ui/` | Retained widgets (games); `src/gui/` immediate-mode GUI (tools; the editor is drawn with it) |
| `src/level/` | Runtime level module, always built (0.9.0+): `LrgLevel`, `LrgNode` (+ `lrg_node_get_world_matrix`), `LrgNodeVisual`, `LrgNodeTilemap`, `LrgTimeline`, `LrgComponentDesc`, `LrgScriptBinding`, `.rlevel`/`.rprefab` serializer, prefabs (`lrg_prefab_load_resolved`, `instantiate_linked`), `lrg_level_instantiate`. Works with `BUILD_EDITOR=0` |
| `src/editor/` | `BUILD_EDITOR=1` authoring layer: `LrgEditor` (commands, undo, selection), view math/picking (wraps `lrg-math3d`), gizmo, settings, external editor, IPC server, actions, validation, projects, asset database/`.meta`, scene import. The old `src/editor/lrg-level.h` etc. are forwarders to `src/level/` |
| `src/editor/ui/` | `LrgEditorApp`: the editor application (panels, viewport render, dialogs, play mode) |
| `src/editorctl/` | `lrgeditorctl` and the MCP tool table shared with `lrgeditor --mcp` |
| `src/mcp/` | In-game MCP server (`MCP=1`): input injection and `lrg_input_get_state`, ECS, save, debug (profiler, overlay, console, inspector) tools; `libregnum://engine/{info,config,registry}` resources |
| `src/launcher/` | `lrgldr` (game module runner), `lrgeditor` |
| `src/scripting/` | Lua, Python, GJS, crispy (C) backends |
| `src/template/` | Genre game templates (FPS, third person, racing, tycoon...) |
| `src/mmo/`, `src/net/` | Networking and MMO services |
| `editors/emacs/` | `libregnum.el` (+ ERT tests) |
| `deps/` | Submodules: graylib, yaml-glib, mcp-glib, crispy, cad-glib |
| `tests/` | GTest suites (`test-*.c`) and smoke scripts |

## Build and test

```bash
git submodule update --init --recursive
make -j"$(nproc)"                 # library, GIR, lrgldr, lrgeditor, lrgeditorctl
make test                         # all GTest suites
make SANITIZE=1 deps && make SANITIZE=1 lib && make -C tests SANITIZE=1 run-sanitize
make test-editorctl               # lrgeditorctl + MCP against a headless editor
make test-emacs                   # libregnum.el byte-compile (warnings = errors) + ERT
make test-editor-x11              # real window + real X input (Xvfb, xdotool, socat, yq)
make install-skills               # this skill into ~/.claude/skills (SKILLS_LINK=1 to symlink)
```

Options: `DEBUG=1`, `BUILD_GIR=0` (faster), `BUILD_EDITOR=0`, `BUILD_EDITOR_UI=0`, `EDITOR_MCP=0`, `MCP=1`, `CRISPY=0`, `STEAM=1`, `WINDOWS=1`.

## Conventions

- C `-std=gnu89`, gcc, `-Werror` with `-Wnull-dereference -Wformat=2 -Wmissing-prototypes -Wdeclaration-after-statement`: declarations at the top of blocks, prototypes for non-static functions, literal format strings. Fix warnings properly; never silence them.
- Tabs for indentation, GNU-style spacing `func (args)`, `/* */` comments only.
- GObject everywhere: `G_DECLARE_FINAL_TYPE`/`DERIVABLE`, `g_autoptr`, `g_steal_pointer`, properties in kebab-case, `LRG_AVAILABLE_IN_ALL` before public declarations.
- gtk-doc/GIR comment blocks in headers, once per identifier (duplicates make the GIR scanner warn, and the GIR build must be warning-free too).
- graylib boxed types (`GrlColor`, `GrlVector3`...) are freed, not unreffed.
- Docs in org-mode under `docs/`, updated in the same change; Conventional Commits; feature work in a git worktree under `./trees/`.

## Traps

- **Render layers vs LrgRenderLayer.** `LRG_RENDER_LAYERS_*` / `lrg_render_layers_*` (graphics/lrg-camera.h) are the 32 visibility layers filtered by a camera's `cull-mask`; `LrgRenderLayer` (`LRG_RENDER_LAYER_UI`, lrg-enums.h) is LrgRenderer's draw order. Drawables (static batch chunks, billboard batches, terrain chunks, particle systems) test the *active mask*, which only `lrg_camera_begin()` / `lrg_shadow_renderer_begin_pass()` install: render code using raw `BeginMode3D()` must bracket with `lrg_render_layers_set_active_mask()`, and hand-drawn meshes must test `lrg_render_layers_is_visible()` themselves.
- **Debug draw needs a flush every frame.** Record into `lrg_debug_draw_get_default()` (or a world/viewport instance), call `lrg_debug_draw_tick (dd, delta)` in update and `lrg_debug_draw_flush (dd, camera)` (or `_flush_matrices`) in render; one-frame primitives otherwise pile up to a 262144 cap. Call `draw_3d()` at the top level of the 3D mode, never inside `rlPushMatrix()`. Use `set_thickness()`, not `rlSetLineWidth()` (GL 3.3 core only guarantees 1-px lines). Subsystems record on their `LrgDebugCategory` and test `lrg_debug_draw_is_category_enabled()` first.
- **raylib `bool` under gnu89.** `<raylib.h>` without `<stdbool.h>` makes `bool` an int-sized enum while raylib returns a one-byte `_Bool`, so callers read garbage (the editor's mouse buttons read as always held; `RayCollision.hit` read padding). The build force-includes `stdbool.h` and `libregnum.h` includes it first. If you see `typedef enum bool` in effect anywhere, that file is broken.
- **raylib `Matrix` layout.** Fields are named by column-major storage index but declared in row order: convert by field name, never `memcpy` a `float[16]`. `raymath.h` is not gnu89-clean; do not include it.
- **`g_ptr_array_copy (src, NULL, NULL)`** inherits `src`'s free function: freeing the copy unrefs live objects. Pass a ref-copy function.
- **Unix socket paths** over 107 bytes are rejected; keep test sockets in `$XDG_RUNTIME_DIR`.
- **Input bugs** do not show in headless GUI tests (they inject input below raylib/GLFW): use `make test-editor-x11`. Under Xvfb by hand, point `XDG_RUNTIME_DIR` at an empty short directory or GLFW opens the window on your Wayland session.
- **Level types are not editor code any more.** Put runtime level/prefab/serializer changes in `src/level/` (never in the `src/editor/` forwarders, which are excluded from the GIR scan); `src/level/` must not include editor headers. `tests/check-level-noeditor.sh` builds `BUILD_EDITOR=0` into `build/noeditor` and proves the level API is exported and runnable without the editor.
- **MCP over stdio owns stdout.** `lrgeditor --mcp` duplicates fd 1 for the protocol and points fd 1 at stderr; never `printf` to stdout in code paths that run under it.

## Registry built-ins

`lrg_registry_register_builtin()` (run by engine startup) maps the engine's concrete, headlessly constructible types to kebab-case names: the C prefix without `lrg_` (`rigid-body`, `point-light2d`, `particle-emitter`, `item-def`), components without `-component` (`transform`, `sprite`, `collider`, `animator`). YAML `type:` fields and `lrg_ecs_spawn_object` resolve them without app registration; app registrations of the same name win. To add a type, append a row to `builtin_catalog` in `src/core/lrg-registry.c` and to the table in `docs/modules/core/registry.org`; `tests/test-registry.c` builds every entry from YAML.

## Extending the editor

- New user-facing command: add an action in `src/editor/ui/lrg-editor-app-actions.c` (id, label, category, default shortcut, callbacks). It appears in menus via the palette, gets a rebindable shortcut, and is callable as `lrgeditorctl run ID` and the `run_action` MCP tool.
- New IPC method: `lrg_editor_server_register_method()` (document methods in `lrg-editor-server.c`, app methods in `lrg-editor-app-actions.c`); document it in `docs/editor/ipc-protocol.org`; if agents should use it directly, add a row to the tool table in `src/editorctl/lrgeditorctl-mcp.c`.
- Any edit to the document goes through `LrgEditor` (commands, or `lrg_editor_snapshot_node()` + `lrg_editor_commit_node_state()`), never by poking nodes directly, so it is undoable and emits `changed`.

## Debug and visibility

- `src/debug/lrg-debug-draw.*`: immediate-mode debug geometry (lines, boxes, spheres, capsules, cylinders, cones, frustums, grids, axes, labels, 2D shapes) with durations, depth-tested/on-top lists and 32 toggleable categories; headless-inspectable. Docs: docs/modules/debug/debug-draw.org. Render layers: docs/modules/graphics/render-layers.org.
