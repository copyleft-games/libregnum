# CODE IPC methods (DRAFT: names stable, result fields may grow)

Docs: docs/editor/debugging.org (#editor-debug-ipc) and docs/editor/code-intelligence.org (#editor-lsp-ipc).

Common: `path` = a script file, absolute or relative to the project (default: the code panel's active tab).
`line`/`column` are 1-based everywhere. Errors: invalid-params for missing/bad params, not-found for no
such breakpoint/frame/variable reference/file/server, failed for engine errors (no debug session, a
paused-only call while running, server crashed, request timed out).

## debug.* (registered by src/editor/ui/lrg-editor-debug-panel.c)
- debug.state {} -> {state:"idle"|"running"|"paused", reason:"breakpoint"|"step"|"pause"|"exception"|"entry"|null, path|null, line|null, language|null, message|null, dap:{listening:bool, port|null, clients:int}}
- debug.show {} -> {}   (presents the Debug panel)
- debug.breakpoints {path?} -> [{id, path, line, condition|null, hit_condition|null, log_message|null, enabled, verified, hits}]
- debug.set_breakpoint {path, line, condition?, hit_condition?, log_message?, enabled?:bool} -> breakpoint   (replaces one at the same path:line)
- debug.remove_breakpoint {path, line} -> {removed:bool}
- debug.toggle_breakpoint {path?, line} -> {set:bool, breakpoint|null}
- debug.clear_breakpoints {path?} -> {removed:int}
- debug.continue {} -> debug.state
- debug.pause {} -> debug.state
- debug.step_over {} -> debug.state
- debug.step_in {} -> debug.state
- debug.step_out {} -> debug.state
- debug.stop {} -> debug.state   (detaches: clears the stop, play keeps running)
- debug.stack {} -> [{id, name, path|null, line, column, language}]   (paused only)
- debug.scopes {frame?:id (default top)} -> [{name, reference, expensive}]
- debug.variables {reference} -> [{name, value, type, reference (0 = leaf)}]
- debug.evaluate {expression, frame?:id} -> {result, type, reference}
- debug.watches {} -> [{expression, value|null, type|null, error|null}]
- debug.add_watch {expression} -> debug.watches
- debug.remove_watch {expression} -> debug.watches
- debug.dap_start {port?:int (0 = pick a free one, default setting debug.dap-port = 4711), host?:"127.0.0.1"} -> {port}
- debug.dap_stop {} -> {}

## lsp.* (registered by src/editor/ui/lrg-editor-code-panel.c)
- lsp.servers {} -> [{language, command, available:bool, running:bool, initialized:bool, binary|null}]
- lsp.status {path?} -> {path, language, server|null, running, initialized, version:int, diagnostics:int}
- lsp.diagnostics {path?} -> [{line, column, end_line, end_column, severity:"error"|"warning"|"info"|"hint", message, source|null, code|null}]
- lsp.complete {path?, line?, column? (default caret)} -> [{label, kind:int, detail|null, insert_text}]
- lsp.hover {path?, line?, column?} -> {contents|null}
- lsp.definition {path?, line?, column?, jump?:bool (default false)} -> [{path, line, column}]
- lsp.references {path?, line?, column?} -> [{path, line, column}]
- lsp.rename {path?, line?, column?, new_name} -> {files:int, edits:int}   (edits open files' buffers; one undo step per buffer)
- lsp.format {path?} -> {edits:int}
- lsp.restart {language?} -> lsp.servers
