# M8: scripting, debugging, profiling (+ dock tab overflow) (read with brief.md and ../v081/brief-w4.md)

M0-M7 are committed (`git log --oneline origin/master..HEAD | head -80`). Read the plan's M8 section
(/var/home/zach/.claude/plans/take-a-look-at-hidden-iverson.md), the gap-analysis rows in
docs/editor/gap-analysis.org "Scripting and visual scripting" and "Debugging and profiling" (and the
engine-gap rows: visual scripting runtime, script hot reload, debugger hooks, memory profiler, GPU
timings), AGENTS.md (Testing > GPU tests, editor runs and screenshots: build-aux/xrun.sh, 1920x1080,
dark + light, LOOK at them; keep shots in
`/tmp/claude-1000/-var-home-zach-source-projects-libregnum/c474e1e8-b6e5-4b4b-bd42-fe50c0433c3f/scratchpad/v090/shots/m8-<agent>-*.png`) and skills/skill-libregnum/references/engine.md.
Existing pieces: src/scripting/ (LrgScripting base with backends lua, python, pygobject, gjs, crispy,
native, gi; LrgScriptHost, LrgScriptingManager, scheduler/tasks), src/debug/ (LrgProfiler sections,
LrgDebugConsole, LrgDebugOverlay, LrgInspector), editor code panel (LrgTextEditor + LrgSyntax),
profiler panel, console panel, src/ui LrgNodeGraph / LrgBlockEditor (UI only), src/net + src/mmo.
Patterns: the M6/M7 editor tools (data editors via LrgEditorDataEditor + LrgEditorDocument formats,
docked panels, IPC from the tool's own file, every edit one undo step).

Stubs for every new file below exist and are registered (Makefile, libregnum.h, tests/Makefile, the
new non-editor headers in DOCS_CHECK_HEADERS); replace them wholesale (change parents as needed).

## Shared-file protocol (as in M6/M7)
- `.../scratchpad/v090/m8-ipc/<AGENT>.md`: your IPC method list (name, params, result, errors; one line
  each), written EARLY, `FINAL` on the first line when done. PLUMB turns them into lrgeditorctl verbs,
  MCP tools, smoke checks and IPC docs (generator: .../scratchpad/plumb/gen.py, spec.py).
- `.../scratchpad/v090/m8-patches/<target-basename>.<AGENT>.patch`: small hook patches for files another
  agent owns; owners apply them whenever they rebuild and rename them `*.applied`.
- Shell files (src/editor/ui/lrg-editor-app.c, -window.c, -app-actions.c, -app-private.h) are the
  LEAD's: docking, menus, default layout go in your report as exact patches.
- API contracts others consume: SCRIPTRT publishes its debugger + reload API header first and writes
  `m8-ipc/SCRIPTRT-API-READY` (CODE and TOOLSCRIPT build on it).

## SCRIPTRT (script runtime: hot reload + debugger core)
Own: src/scripting/lrg-scripting*.{c,h} (every backend + base), lrg-script-host.{c,h},
lrg-scripting-manager.{c,h}, lrg-scriptable.{c,h}, lrg-lua-*.{c,h}, lrg-python-*.{c,h}, new
src/scripting/lrg-script-reloader.{c,h} (GFileMonitor-based watcher for script files and the
scripting search paths; reload changed Lua/Python/GJS/crispy modules in running worlds without
restarting play; a state hand-over hook (`on_reload(old_state)` / `lrg_scriptable` save/restore
convention per language), failure keeps the old code running and reports file:line; signals
`reloaded`, `reload-failed`), new src/scripting/lrg-script-debugger.{c,h} (backend-neutral debugger:
breakpoints (file:line, conditions, hit counts, log points), pause/continue/step in/over/out, stack
frames, scopes and variables with lazy expansion (tables/dicts/GObjects), evaluate in frame,
exceptions as stops; Lua via lua_sethook, Python via sys.monitoring (3.12+), GJS via its Debugger API
if reachable from C (else document the limit), crispy: report unsupported cleanly; thread-safe stop
model: the game thread blocks in a nested main loop or condition while the debugger serves requests).
Publish the header EARLY (API-READY marker). Tests: tests/test-script-reload.c (each backend present:
edit file -> reload -> new behaviour, state kept through the hook, syntax error keeps old code),
tests/test-script-debugger.c (breakpoints, stepping, variables, evaluate, conditions, per backend;
skip cleanly when a backend is not built). Docs: docs/modules/scripting/{hot-reload,script-debugger}.org,
guides/scripting.org section. Editor: play-in-editor reloads on save (patch to code panel via the
protocol, CODE owns it), a console line per reload.

## CODE (code panel: debugger UI, DAP server, LSP client)
Own: src/scripting/lrg-dap-server.{c,h} (Debug Adapter Protocol server over a TCP port or stdio on
top of LrgScriptDebugger: initialize, launch/attach, setBreakpoints, configurationDone, threads,
stackTrace, scopes, variables, evaluate, continue/next/stepIn/stepOut/pause, stopped/output/terminated
events; json-glib), src/editor/lrg-lsp-client.{c,h} (LSP client over stdio: spawn per-language servers
from editor settings (clangd for crispy C, lua-language-server, pylsp/pyright, typescript-language-server
for GJS when installed), initialize, didOpen/didChange/didSave, publishDiagnostics, completion, hover,
definition, references, rename, formatting; graceful when no server is installed),
src/ui/lrg-text-editor.{c,h} (gutter markers API (breakpoints, current line, diagnostics), squiggle
underlines, completion popup, hover tooltip, go-to-definition hooks: generic, GIR-documented),
src/editor/ui/lrg-editor-code-panel.{c,h} (breakpoint gutter clicks, debug toolbar (continue, step
over/in/out, stop), current-line highlight, LSP integration, reload-on-save hook from SCRIPTRT),
src/editor/ui/lrg-editor-debug-panel.{c,h} (docked "Debug" panel: call stack, variables tree, watches,
breakpoints list, debug console/evaluate), editors/emacs/libregnum-dape.el (new: dape config attaching
to the editor's DAP port; byte-compile clean; give the lead the one-line Makefile test-emacs change),
tests/test-dap-server.c (protocol round trips against a scripted Lua session), tests/test-lsp-client.c
(a fake LSP server script in tests/data speaking JSON-RPC), tests/test-editor-app-debug.c. Docs:
docs/editor/debugging.org (DAP, Emacs dape, the debug panel), docs/editor/code-intelligence.org (LSP),
docs/modules/scripting/dap-server.org, docs/modules/ui/text-editor.org sections. IPC `debug.*`, `lsp.*`.

## VSCRIPT (visual scripting runtime + editors)
Own: src/scripting/lrg-vscript-program.{c,h} (`.rvscript` program format: graph of event nodes
(update, fixed update, signals of the owner/GObjects, input actions, custom events), flow (branch,
sequence, loops, for-each, delay/wait as coroutine), data nodes (literals, variables (graph + blackboard),
math, compare, string), GI call nodes (any introspected function/method/constructor, property get/set,
signal connect/emit, found through GIRepository) and subgraph/macro references; YAML load/save
(byte-stable, unknown keys kept, strict errors with key paths), validation with node ids; one program
format serves both the node-graph editor and the block editor (blocks map onto the same nodes)),
src/scripting/lrg-scripting-graph.{c,h} (an LrgScripting backend that runs `.rvscript` programs so
existing hosts/components can use them; interpreter with a step budget, deterministic, debugger
hooks through LrgScriptDebugger if SCRIPTRT's API allows (node-level breakpoints)),
src/editor/ui/data/lrg-editor-vscript-editor.{c,h} (node-graph editor on LrgNodeGraph: palette from
GIRepository search, typed pins, wiring, comments/groups, live execution highlight while playing),
src/editor/ui/data/lrg-editor-blocks-editor.{c,h} (block editor on LrgBlockEditor for the same programs,
for beginners), src/editor/lrg-editor-document.{c,h} + src/editor/ui/data/lrg-editor-data-editor.c +
src/editor/ui/lrg-editor-assets-panel.{c,h} THIS WAVE (formats VSCRIPT ".rvscript" (and ".rblocks" if
you keep a separate block layout file), builtins, New entries), an example
examples/visual-script.c (+ data; send the examples/Makefile rows to the lead), tests/test-vscript.c
(format round trip, every node family, GI calls, signals, determinism, step budget),
tests/test-editor-app-vscript.c. Docs: docs/modules/scripting/visual-scripting.org,
docs/editor/visual-scripting.org, docs/guides/visual-scripting.org. IPC `vscript.*`.

## TOOLSCRIPT (editor-time tool scripts)
Own: src/editor/lrg-editor-script-api.{c,h} (GI-friendly `LrgEditorScriptApi`: level/nodes
(find, create, set, delete, reparent), selection, undo groups (begin/end = one step), assets
(list, import, create), notify/log, run IPC methods, register menu items and actions with script
callbacks, register custom inspector rows/panels if cheap; exposed to Lua/Python/GJS scripting as a
global `editor`), src/editor/ui/lrg-editor-console-panel.{c,h} (script REPL: language picker, history,
multi-line, output capture), "Run Script..." and a per-project `editor_scripts/` folder whose scripts
register their menu items on project open (actions from your own file; Tools menu rows as a shell patch),
run-in-editor scripts for components (`@tool` equivalent: a `run-in-editor` property on the script
component(s) in src/ecs or src/world3d (find them; own those files this wave) so they tick while
authoring), tests/test-editor-app-toolscript.c (Lua and Python when built; create nodes as one undo
step, menu registration, REPL). Docs: docs/editor/tool-scripts.org. IPC `script.*` (run file/string,
list registered tools). Builds on SCRIPTRT's reloader for `editor_scripts/` hot reload.

## PROF (CPU/GPU profiler, captures, frame debugger)
Own: src/debug/lrg-profiler.{c,h} (hierarchical nested sections with parent/child, per-thread lanes,
frame markers, counters, capture to a `.rprof` file and load it back, ring buffer of N frames, cheap
when disabled), new src/debug/lrg-gpu-timer.{c,h} (GL timer queries (GL_TIME_ELAPSED / glQueryCounter
through rlGetProcAddress as lrg-gpu-particles.c does), double-buffered so it never stalls, a GPU lane
fed by profiler sections), new src/debug/lrg-frame-capture.{c,h} (records one frame's draw calls:
pass, entity/node, mesh, material, shader, state, counts, and can replay up to call N into a render
target for the frame debugger), src/world3d/lrg-world3d-renderer.c THIS WAVE (profiler sections per
pass + capture hooks), src/editor/ui/lrg-editor-profiler-panel.{c,h} (timeline with thread + GPU lanes,
flame graph, section table, capture/save/load .rprof, compare two frames), new
src/editor/ui/lrg-editor-frame-debugger.{c,h} (docked panel: draw-call list by pass, state details,
scrub to call N with the viewport showing the partial frame, texture/RT viewer), tests/test-profiler-capture.c,
tests/test-frame-capture.c (under xrun), tests/test-editor-app-frame.c. Docs: docs/modules/debug/{profiler,gpu-timer,frame-capture}.org,
docs/editor/profiling.org (CPU/GPU/frame debugger sections). Measured overhead numbers (taskset) in
docs. IPC `profiler.*`, `frame.*`.

## MEMNET (memory and network profilers)
Own: new src/debug/lrg-memory-profiler.{c,h} (live GObject instance counts by type (find a working
approach: g_type_get_instance_count needs GOBJECT_DEBUG=instance-count before GObject initialises;
the editor/launcher can set it at process start, or track through a toggle-ref/registry for engine
types; document what you chose), LrgAssetManager residency (what is loaded, ref counts, bytes),
texture/mesh/audio memory estimates (GPU VRAM estimates from formats and sizes), snapshots and diffs),
new src/net/lrg-net-stats.{c,h} (per-message-type counters, bytes in/out, packets, RTT, loss, per peer,
rolling windows) with hooks in src/net/** and src/mmo/** (own those files this wave for the hooks only),
src/editor/ui/lrg-editor-memory-panel.{c,h} and lrg-editor-network-panel.{c,h} (docked panels: tables
by type with sparkline trends, snapshot/diff, asset residency; network graphs per peer/message type),
tests/test-memory-profiler.c, tests/test-net-stats.c (loopback server/client), tests/test-editor-app-memnet.c.
Docs: docs/modules/debug/memory-profiler.org, docs/modules/net/net-stats.org,
docs/editor/memory-and-network.org. IPC `memory.*`, `network.*`.

## DOCKTAB (dock tab overflow + multi-shortcut canvas)
Own: src/ui/lrg-dock-area.{c,h}, src/ui/lrg-tab-view.{c,h} and any src/ui dock/tab helpers, src/ui/lrg-canvas.{c,h}.
1. Tab overflow: when a dock group's tabs do not fit, keep readable tabs (min width with ellipsis,
   icon + label), add scroll arrows and an overflow button listing every tab (click to present),
   keep the active tab visible, drag-reorder/drag-out still work; wheel scrolls the strip. Look at the
   current editor (8 tabs on the right dock collapse to bare icons; see
   `.../scratchpad/v090/shots/m67e-lead-mixer-docked.png`).
2. Propose a better default layout for the editor's many panels (e.g. a second right group under
   the Inspector for tool panels, or moving some to the bottom) as an exact shell patch for the lead.
3. Canvas shortcuts: several actions may share a key; the first ENABLED one fires (registration order
   or an explicit priority). Then give the Tiles panel's vertical flip `Y` back (src/editor/ui/
   lrg-editor-tiles-panel.c is yours for that one change) while `tool.toggle-snap` keeps Y elsewhere.
Tests: tests/test-ui-dock-overflow.c (+ existing test-ui-dock, test-ui-actions, test-editor-app,
x11 smoke). Docs: docs/modules/ui/dock-area.org + canvas shortcuts section. Screenshots of the
editor's right dock before/after.

## PLUMB (CLI/MCP/Emacs/docs for everything above)
Own: src/editorctl/**, tests/editorctl-smoke.sh, editors/emacs/libregnum.el + libregnum-tests.el (new
commands where useful: run tool script, profiler capture, debug attach helpers; CODE owns
libregnum-dape.el), docs/editor/ipc-protocol.org, docs/editor/lrgeditorctl.org,
skills/skill-libregnum/references/{ipc-protocol,lrgeditorctl,mcp}.md. Reuse
.../scratchpad/plumb/gen.py + spec.py (extend spec.py with the m8-ipc lists; keep the generated
markers). Wait for lists; build against what exists; finish when every list says FINAL (or the lead
tells you). Gate: make test-editorctl, make test-emacs.

## Gate (each agent)
`make all` 0 warnings (GIR), your suites + every suite you could affect (name them) green (GPU under
build-aux/xrun.sh), docs-check clean for your headers, dark+light 1920x1080 screenshots for UI looked
at. Skill text (SKILL.md section) in your report. Report: files, public API (one line each), formats,
IPC list, tests + results, shell patches, patches applied for others, screenshots, third-party code,
anything not done.
