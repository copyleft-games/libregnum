DRAFT (TOOLSCRIPT; editor-time tool scripts). Registered from src/editor/ui/lrg-editor-console-panel.c on the app server.
Languages: "lua", "python", "pygobject", "gjs" (only those built; others -> invalid-params). Every run is ONE undo step.

- script.run          params {code?: string, path?: string, language?: string (default from path ext, else "lua"), label?: string (undo label)} -> {result: any|null, output: string, language: string}; errors invalid-params (no code/path, unknown or unavailable language), not-found (path missing), failed (script error; message "file:line: msg"). code runs in the persistent console context of that language (REPL state kept); path runs in a fresh context.
- script.languages    params {} -> [{id: string, name: string, available: bool}]
- script.tools        params {} -> [{id: string (action id "script.tool.<name>"), label: string, menu: string ("Tools/..."), script: string (path), language: string, function: string, shortcut: string|null}]
- script.run_tool     params {id: string} -> true; errors not-found (no such tool), failed (callback raised)
- script.reload       params {} -> {dir: string|null, loaded: int, failed: [{path: string, message: string}]}  (re-runs every <project>/editor_scripts/* file)
- script.run_in_editor params {node: string (guid or path), index?: int (script binding index, default 0), enabled: bool} -> true; one undo step; errors not-found (node or binding), invalid-params
- script.editor_running params {} -> [{node: string (guid), script: string, language: string, error: string|null}]  (bindings ticking while authoring)
