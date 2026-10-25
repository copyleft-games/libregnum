# Working on libregnum itself

Authoritative guide: `AGENTS.md` at the root of the checkout (read it before changing code), plus `docs/` (org-mode). This page is the short version and the traps that have actually bitten.

## Layout

| Path | What |
|------|------|
| `src/core/` | `LrgEngine` singleton, `LrgRegistry` (type names -> GTypes), data loader (YAML), asset manager/packs |
| `src/ecs/` | `LrgWorld`, `LrgGameObject`, `LrgComponent` and built-in components |
| `src/graphics/`, `src/scene/`, `src/shapes/`, `src/lighting/`, `src/postprocess/` | Rendering on graylib/raylib |
| `src/ui/` | Retained widgets (games); `src/gui/` immediate-mode GUI (tools; the editor is drawn with it) |
| `src/editor/` | `LrgLevel`/`LrgNode` authoring model, `LrgEditor` (commands, undo, selection), math, gizmo, settings, external editor, IPC server, actions, validation, projects, prefabs |
| `src/editor/ui/` | `LrgEditorApp`: the editor application (panels, viewport render, dialogs, play mode) |
| `src/editorctl/` | `lrgeditorctl` and the MCP tool table shared with `lrgeditor --mcp` |
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

- **raylib `bool` under gnu89.** `<raylib.h>` without `<stdbool.h>` makes `bool` an int-sized enum while raylib returns a one-byte `_Bool`, so callers read garbage (the editor's mouse buttons read as always held; `RayCollision.hit` read padding). The build force-includes `stdbool.h` and `libregnum.h` includes it first. If you see `typedef enum bool` in effect anywhere, that file is broken.
- **raylib `Matrix` layout.** Fields are named by column-major storage index but declared in row order: convert by field name, never `memcpy` a `float[16]`. `raymath.h` is not gnu89-clean; do not include it.
- **`g_ptr_array_copy (src, NULL, NULL)`** inherits `src`'s free function: freeing the copy unrefs live objects. Pass a ref-copy function.
- **Unix socket paths** over 107 bytes are rejected; keep test sockets in `$XDG_RUNTIME_DIR`.
- **Input bugs** do not show in headless GUI tests (they inject input below raylib/GLFW): use `make test-editor-x11`. Under Xvfb by hand, point `XDG_RUNTIME_DIR` at an empty short directory or GLFW opens the window on your Wayland session.
- **MCP over stdio owns stdout.** `lrgeditor --mcp` duplicates fd 1 for the protocol and points fd 1 at stderr; never `printf` to stdout in code paths that run under it.

## Extending the editor

- New user-facing command: add an action in `src/editor/ui/lrg-editor-app-actions.c` (id, label, category, default shortcut, callbacks). It appears in menus via the palette, gets a rebindable shortcut, and is callable as `lrgeditorctl run ID` and the `run_action` MCP tool.
- New IPC method: `lrg_editor_server_register_method()` (document methods in `lrg-editor-server.c`, app methods in `lrg-editor-app-actions.c`); document it in `docs/editor/ipc-protocol.org`; if agents should use it directly, add a row to the tool table in `src/editorctl/lrgeditorctl-mcp.c`.
- Any edit to the document goes through `LrgEditor` (commands, or `lrg_editor_snapshot_node()` + `lrg_editor_commit_node_state()`), never by poking nodes directly, so it is undoable and emits `changed`.
