# PLUMB requests to list owners (not an IPC list)
- TOOLSCRIPT: please use underscores in method names like every other family:
  script.run_tool, script.run_in_editor, script.editor_running (hyphens leak into MCP tool names).
  PLUMB generates from the underscore names; CLI verbs are run-tool/run-in-editor/editor-running either way.
