/* lrgeditorctl-mcp.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * The editor's MCP tools. Most tools map one-to-one onto an IPC method
 * and pass their arguments straight through (the schemas below document
 * the method's parameters for the model); a few compose methods
 * (create_node with a transform), pick a method from an argument (play,
 * run_game) or post-process the result (screenshot returns the image).
 */

#include "lrgeditorctl-mcp.h"
#include <glib/gstdio.h>
#include <string.h>

#define NODE_ARG \
	"\"node\":{\"type\":\"string\",\"description\":\"Node GUID or slash-separated name path from the top of the level, e.g. \\\"House/Door\\\"\"}"
#define VEC3(desc) \
	"{\"type\":\"array\",\"items\":{\"type\":\"number\"},\"minItems\":3,\"maxItems\":3,\"description\":\"" desc "\"}"

#define DATA_PATH_ARG \
	"\"path\":{\"type\":\"string\",\"description\":\"Data file, absolute or relative to the project; omit for the focused data editor\"}"
#define MAT_PATH_ARG \
	"\"path\":{\"type\":\"string\",\"description\":\"File, absolute or relative to the project\"}"
#define DATA_AT_ARG \
	"\"at\":{\"type\":\"string\",\"description\":\"YAML path, e.g. /actions/jump/bindings/0/key (default the root)\"}"

typedef enum
{
	TOOL_PASS,        /* arguments -> method params */
	TOOL_CREATE,      /* node.create, then node.set for a transform */
	TOOL_PLAY,        /* command -> play.<command> */
	TOOL_RUN,         /* stop -> run.stop, else run.start */
	TOOL_SCREENSHOT,  /* view.screenshot to a temp file, returned as an image */
	TOOL_SESSIONS,    /* running editors on this machine */
	TOOL_RAW          /* any method */
} ToolKind;

typedef struct
{
	const gchar *name;
	const gchar *method;
	ToolKind     kind;
	gboolean     read_only;
	gboolean     destructive;
	const gchar *description;
	const gchar *properties;   /* JSON object members of the input schema */
	const gchar *required;     /* JSON array, or NULL */
} ToolSpec;

static const ToolSpec tools[] = {
	{ "editor_info", "editor.info", TOOL_PASS, TRUE, FALSE,
	  "Summary of the running editor: project, level file, dirty flag, node count, undo/redo depth, active tool, snapping and selection. Call this first.",
	  "", NULL },
	{ "list_sessions", NULL, TOOL_SESSIONS, TRUE, FALSE,
	  "Running libregnum editors on this machine (socket, pid, project, level). The bridge talks to the newest unless started with --session.",
	  "", NULL },
	{ "get_tree", "node.tree", TOOL_PASS, TRUE, FALSE,
	  "The level's node hierarchy: nested {guid, name, kind, visible, locked, children, nodes}. Optionally only the subtree under one node.",
	  NODE_ARG, NULL },
	{ "get_node", "node.get", TOOL_PASS, TRUE, FALSE,
	  "Everything about one node: transform (location, rotation in radians, scale), visual (kind, primitive, asset, material), components, scripts, flags.",
	  NODE_ARG ",\"recursive\":{\"type\":\"boolean\",\"description\":\"Include children\"}", "[\"node\"]" },
	{ "find_nodes", "node.find", TOOL_PASS, TRUE, FALSE,
	  "Nodes whose name contains the query (case-insensitive), or whose GUID matches.",
	  "\"query\":{\"type\":\"string\"}", "[\"query\"]" },
	{ "create_node", "node.create", TOOL_CREATE, FALSE, FALSE,
	  "Create a node (undoable). kind: none, primitive, mesh-asset, sprite, tilemap, light, camera, audio-emitter. primitive: cube, uv-sphere, ico-sphere, plane, cylinder, cone, torus, circle, grid, rectangle-2d, circle-2d. Primitives are unit-sized (1 m). Returns the new node.",
	  "\"name\":{\"type\":\"string\"},\"parent\":{\"type\":\"string\",\"description\":\"Parent GUID or path; omit for top level\"},"
	  "\"kind\":{\"type\":\"string\"},\"primitive\":{\"type\":\"string\"},\"index\":{\"type\":\"integer\"},"
	  "\"location\":" VEC3 ("metres, local to the parent") ",\"rotation_degrees\":" VEC3 ("Euler XYZ degrees") ","
	  "\"scale\":" VEC3 ("per-axis scale"), "[\"name\"]" },
	{ "update_node", "node.set", TOOL_PASS, FALSE, FALSE,
	  "Change a node (one undo step): name, location, rotation_degrees (or rotation in radians), scale, visible, locked, visual {kind, primitive, asset, color [r,g,b,a] 0..1, roughness, metallic} (color/roughness/metallic may also be nested under material). Only given fields change.",
	  NODE_ARG ",\"name\":{\"type\":\"string\"},\"location\":" VEC3 ("metres") ",\"rotation_degrees\":" VEC3 ("degrees")
	  ",\"rotation\":" VEC3 ("radians") ",\"scale\":" VEC3 ("scale") ",\"visible\":{\"type\":\"boolean\"},"
	  "\"locked\":{\"type\":\"boolean\"},\"visual\":{\"type\":\"object\"}", "[\"node\"]" },
	{ "delete_node", "node.delete", TOOL_PASS, FALSE, TRUE,
	  "Delete a node and its children (undoable with undo).",
	  NODE_ARG, "[\"node\"]" },
	{ "duplicate_node", "node.duplicate", TOOL_PASS, FALSE, FALSE,
	  "Duplicate a node and its children next to it; returns the copy.",
	  NODE_ARG, "[\"node\"]" },
	{ "move_node", "node.move", TOOL_PASS, FALSE, FALSE,
	  "Reparent and/or reorder a node. keep_world keeps its world transform.",
	  NODE_ARG ",\"parent\":{\"type\":\"string\",\"description\":\"New parent; omit for top level\"},"
	  "\"index\":{\"type\":\"integer\"},\"keep_world\":{\"type\":\"boolean\"}", "[\"node\"]" },
	{ "add_component", "component.add", TOOL_PASS, FALSE, FALSE,
	  "Attach a component description (a registered component type name) with optional properties.",
	  NODE_ARG ",\"type\":{\"type\":\"string\"},\"props\":{\"type\":\"object\"}", "[\"node\",\"type\"]" },
	{ "set_component", "component.set", TOOL_PASS, FALSE, FALSE,
	  "Set one property of the node's component at index.",
	  NODE_ARG ",\"index\":{\"type\":\"integer\"},\"key\":{\"type\":\"string\"},\"value\":{}", "[\"node\",\"index\",\"key\",\"value\"]" },
	{ "remove_component", "component.remove", TOOL_PASS, FALSE, TRUE,
	  "Remove the node's component at index.",
	  NODE_ARG ",\"index\":{\"type\":\"integer\"}", "[\"node\",\"index\"]" },
	{ "add_script", "script.add", TOOL_PASS, FALSE, FALSE,
	  "Attach a script file (path relative to the project) to a node. language: lua, python, gjs, crispy, elisp.",
	  NODE_ARG ",\"script\":{\"type\":\"string\"},\"language\":{\"type\":\"string\"},\"enabled\":{\"type\":\"boolean\"}", "[\"node\",\"script\"]" },
	{ "remove_script", "script.remove", TOOL_PASS, FALSE, TRUE,
	  "Detach the node's script at index (the file is kept).",
	  NODE_ARG ",\"index\":{\"type\":\"integer\"}", "[\"node\",\"index\"]" },
	{ "get_selection", "selection.get", TOOL_PASS, TRUE, FALSE,
	  "GUIDs of the selected nodes and the primary one.", "", NULL },
	{ "set_selection", "selection.set", TOOL_PASS, FALSE, FALSE,
	  "Select nodes (GUIDs or paths) in the editor; additive adds to the selection.",
	  "\"nodes\":{\"type\":\"array\",\"items\":{\"type\":\"string\"}},\"additive\":{\"type\":\"boolean\"}", "[\"nodes\"]" },
	{ "undo", "edit.undo", TOOL_PASS, FALSE, FALSE, "Undo the last edit.", "", NULL },
	{ "redo", "edit.redo", TOOL_PASS, FALSE, FALSE, "Redo the last undone edit.", "", NULL },
	{ "get_history", "edit.history", TOOL_PASS, TRUE, FALSE, "Undo and redo step labels.", "", NULL },
	{ "save_level", "level.save", TOOL_PASS, FALSE, FALSE,
	  "Save the level (optionally to a new path, which becomes the document's path).",
	  "\"path\":{\"type\":\"string\"}", NULL },
	{ "open_level", "level.open", TOOL_PASS, FALSE, TRUE,
	  "Open a level file. Fails if there are unsaved changes unless force is true (which discards them).",
	  "\"path\":{\"type\":\"string\"},\"force\":{\"type\":\"boolean\"}", "[\"path\"]" },
	{ "new_level", "level.new", TOOL_PASS, FALSE, TRUE,
	  "Start a new empty level (unsaved).", "\"name\":{\"type\":\"string\"}", NULL },
	{ "get_level_yaml", "level.serialize", TOOL_PASS, TRUE, FALSE,
	  "The whole level as .rlevel YAML text, exactly as it would be saved.", "", NULL },
	{ "validate_level", "level.validate", TOOL_PASS, TRUE, FALSE,
	  "Problems in the level: [{severity, code, node, path, message, file}]. Empty means clean.", "", NULL },
	{ "list_actions", "action.list", TOOL_PASS, TRUE, FALSE,
	  "Every editor action (menu command): {id, label, category, shortcut, enabled, checked}.", "", NULL },
	{ "run_action", "action.run", TOOL_PASS, FALSE, FALSE,
	  "Run an editor action by id, e.g. view.frame-all, view.top, edit.select-all, node.group, tool.rotate.",
	  "\"id\":{\"type\":\"string\"}", "[\"id\"]" },
	{ "play", NULL, TOOL_PLAY, FALSE, FALSE,
	  "Play-in-editor: start, pause (toggle), step one frame, stop (restores the level exactly), or state.",
	  "\"command\":{\"type\":\"string\",\"enum\":[\"start\",\"pause\",\"step\",\"stop\",\"state\"]}", "[\"command\"]" },
	{ "run_game", NULL, TOOL_RUN, FALSE, FALSE,
	  "Run the game as its own process with the project's run command (saves first), or stop it. Output goes to the editor log.",
	  "\"stop\":{\"type\":\"boolean\"}", NULL },
	{ "get_view", "view.get", TOOL_PASS, TRUE, FALSE,
	  "Viewport camera: position, target, fovy, orthographic, ortho_height, mode_2d.", "", NULL },
	{ "set_view", "view.set", TOOL_PASS, FALSE, FALSE,
	  "Move the viewport camera.",
	  "\"position\":" VEC3 ("camera position") ",\"target\":" VEC3 ("look-at point") ","
	  "\"orthographic\":{\"type\":\"boolean\"},\"ortho_height\":{\"type\":\"number\"},\"mode_2d\":{\"type\":\"boolean\"}", NULL },
	{ "focus_view", "view.focus", TOOL_PASS, FALSE, FALSE,
	  "Frame a node in the viewport (or the selection, or everything).", NODE_ARG, NULL },
	{ "screenshot", NULL, TOOL_SCREENSHOT, TRUE, FALSE,
	  "Look at the editor: returns a PNG of the 3D viewport, or of the whole window with window=true. Needs a windowed editor (not --headless).",
	  "\"window\":{\"type\":\"boolean\"}", NULL },
	{ "get_log", "log.tail", TOOL_PASS, TRUE, FALSE,
	  "Recent editor log records (console), including game output and errors.",
	  "\"count\":{\"type\":\"integer\"}", NULL },
	{ "list_assets", "asset.list", TOOL_PASS, TRUE, FALSE,
	  "Project assets {path, type}; type filters: level, prefab, texture, model, audio, script, font...",
	  "\"type\":{\"type\":\"string\"}", NULL },
	{ "project_info", "project.info", TOOL_PASS, TRUE, FALSE,
	  "Project manifest: root, name, default level, game module, run command, scripts dir, asset dirs.", "", NULL },
	{ "ui_state", "ui.state", TOOL_PASS, TRUE, FALSE,
	  "What the editor UI is doing: open menu, dialog, navigation mode, panel rects, last input seen.", "", NULL },
	{ "notify", "ui.notify", TOOL_PASS, FALSE, FALSE,
	  "Show a toast to the person using the editor. level: info (default), success, warning or error.",
	  "\"message\":{\"type\":\"string\"},\"level\":{\"type\":\"string\",\"enum\":[\"info\",\"success\",\"warning\",\"error\"]}",
	  "[\"message\"]" },
	{ "get_tilemap", "tilemap.get", TOOL_PASS, TRUE, FALSE,
	  "A tilemap node's grid: width, height, tile_width/height (tileset px), tile_size (world units), tileset, layers [{index, name, visible, count, rows}]. Tile ids are 1-based into the tileset; 0 is empty. Row 0 is the top row; cells grow right/down from the node origin.",
	  NODE_ARG ",\"layer\":{\"description\":\"Layer index or name (only that layer)\"},\"cells\":{\"type\":\"boolean\",\"description\":\"false omits the rows\"}",
	  "[\"node\"]" },
	{ "configure_tilemap", "tilemap.configure", TOOL_PASS, FALSE, FALSE,
	  "Set up a tilemap node's grid (one undo step): width/height in cells, tile_width/tile_height (tileset slicing in px), tile_size (world units per cell), tileset (image path), add_layer (name), remove_layer, layer + rename/visible.",
	  NODE_ARG ",\"width\":{\"type\":\"integer\"},\"height\":{\"type\":\"integer\"},\"tile_width\":{\"type\":\"integer\"},"
	  "\"tile_height\":{\"type\":\"integer\"},\"tile_size\":{\"type\":\"number\"},\"tileset\":{\"type\":\"string\"},"
	  "\"add_layer\":{\"type\":\"string\"},\"remove_layer\":{},\"layer\":{},\"rename\":{\"type\":\"string\"},"
	  "\"visible\":{\"type\":\"boolean\"}", "[\"node\"]" },
	{ "paint_tiles", "tilemap.paint", TOOL_PASS, FALSE, FALSE,
	  "Paint tiles (one undo step): cells [[x,y],...] sets exactly those cells; points [[x,y],...] draws a brush stroke through them with brush size. tile 0 or erase=true erases.",
	  NODE_ARG ",\"tile\":{\"type\":\"integer\"},\"erase\":{\"type\":\"boolean\"},\"layer\":{},\"brush\":{\"type\":\"integer\"},"
	  "\"cells\":{\"type\":\"array\",\"items\":{\"type\":\"array\",\"items\":{\"type\":\"integer\"}}},"
	  "\"points\":{\"type\":\"array\",\"items\":{\"type\":\"array\",\"items\":{\"type\":\"integer\"}}}", "[\"node\"]" },
	{ "fill_tiles", "tilemap.fill", TOOL_PASS, FALSE, FALSE,
	  "Flood-fill the 4-connected region holding the seed cell's tile id (one undo step).",
	  NODE_ARG ",\"x\":{\"type\":\"integer\"},\"y\":{\"type\":\"integer\"},\"tile\":{\"type\":\"integer\"},"
	  "\"erase\":{\"type\":\"boolean\"},\"layer\":{}", "[\"node\",\"x\",\"y\"]" },
	{ "paint_tile_rect", "tilemap.rect", TOOL_PASS, FALSE, FALSE,
	  "Fill a rectangle of cells, corners inclusive (one undo step).",
	  NODE_ARG ",\"x0\":{\"type\":\"integer\"},\"y0\":{\"type\":\"integer\"},\"x1\":{\"type\":\"integer\"},"
	  "\"y1\":{\"type\":\"integer\"},\"tile\":{\"type\":\"integer\"},\"erase\":{\"type\":\"boolean\"},\"layer\":{}",
	  "[\"node\",\"x0\",\"y0\",\"x1\",\"y1\"]" },
	{ "list_timelines", "timeline.list", TOOL_PASS, TRUE, FALSE,
	  "The level's animation timelines: [{name, length, loop, autoplay, tracks}].", "", NULL },
	{ "get_timeline", "timeline.get", TOOL_PASS, TRUE, FALSE,
	  "One timeline with its tracks [{node, node_name, channel, keys [{time, value, easing}]}].",
	  "\"name\":{\"type\":\"string\"}", "[\"name\"]" },
	{ "set_keyframe", "timeline.key", TOOL_PASS, FALSE, FALSE,
	  "Insert keyframes (undoable; creates the timeline if missing). With channel (location.x|y|z, rotation.x|y|z radians, scale.x|y|z, color.r|g|b|a, roughness, metallic, param.NAME) and optional value keys one channel; otherwise keys the node's current values of channels (default the 9 transform channels). easing: linear, ease-in-quad, ease-out-cubic, ease-in-out-sine, ease-out-bounce, ...",
	  "\"name\":{\"type\":\"string\"}," NODE_ARG ",\"time\":{\"type\":\"number\"},\"channel\":{\"type\":\"string\"},"
	  "\"value\":{\"type\":\"number\"},\"channels\":{\"type\":\"array\",\"items\":{\"type\":\"string\"}},\"easing\":{\"type\":\"string\"}",
	  "[\"name\",\"node\",\"time\"]" },
	{ "remove_keyframe", "timeline.unkey", TOOL_PASS, FALSE, TRUE,
	  "Remove the keys at time (1 ms tolerance) of one channel or every track of the node.",
	  "\"name\":{\"type\":\"string\"}," NODE_ARG ",\"time\":{\"type\":\"number\"},\"channel\":{\"type\":\"string\"}",
	  "[\"name\",\"node\",\"time\"]" },
	{ "configure_timeline", "timeline.set", TOOL_PASS, FALSE, FALSE,
	  "Change a timeline: rename, length (s), loop, autoplay (plays during play-in-editor).",
	  "\"name\":{\"type\":\"string\"},\"rename\":{\"type\":\"string\"},\"length\":{\"type\":\"number\"},"
	  "\"loop\":{\"type\":\"boolean\"},\"autoplay\":{\"type\":\"boolean\"}", "[\"name\"]" },
	{ "preview_timeline", "timeline.seek", TOOL_PASS, FALSE, FALSE,
	  "Show a timeline at a time in the editor (preview only; not saved, timeline.stop restores).",
	  "\"name\":{\"type\":\"string\"},\"time\":{\"type\":\"number\"}", "[\"time\"]" },
	{ "get_thumbnail", "asset.thumbnail", TOOL_PASS, TRUE, FALSE,
	  "Path of an asset's cached thumbnail PNG (textures, levels, prefabs; models need a windowed editor). wait=true generates it now.",
	  "\"path\":{\"type\":\"string\"},\"wait\":{\"type\":\"boolean\"}", "[\"path\"]" },
	/* 0.8.1: asset sidecars, git, linked prefabs */
	{ "get_asset_meta", "asset.meta.get", TOOL_PASS, TRUE, FALSE,
	  "An asset's .meta sidecar: stable guid, importer and its settings at their effective values (defaults included), plus exists. Without a sidecar, the one the editor would create (nothing is written).",
	  "\"path\":{\"type\":\"string\",\"description\":\"Asset path, absolute or relative to the project\"}", "[\"path\"]" },
	{ "set_asset_meta", "asset.meta.set", TOOL_PASS, FALSE, FALSE,
	  "Change importer settings (all or nothing; a null value resets a setting) and/or the guid of an asset's .meta sidecar. Texture settings include filter (point|bilinear|trilinear), mipmaps, max-size.",
	  "\"path\":{\"type\":\"string\"},\"settings\":{\"type\":\"object\"},\"guid\":{\"type\":\"string\"}", "[\"path\"]" },
	{ "vcs_status", "vcs.status", TOOL_PASS, TRUE, FALSE,
	  "Git status of the project: repository, branch and files [{path, status, letter}] that are not clean; with path, that file or folder's badge.",
	  "\"path\":{\"type\":\"string\"}", NULL },
	{ "prefab_overrides", "prefab.overrides", TOOL_PASS, TRUE, FALSE,
	  "A linked prefab instance: source prefab, linked flag and the override keys that differ from the prefab.",
	  NODE_ARG, "[\"node\"]" },
	{ "prefab_apply", "prefab.apply", TOOL_PASS, FALSE, TRUE,
	  "Write an instance's overrides (all, or keys) into its prefab file and sync every other instance (one undo group; overwrites the prefab).",
	  NODE_ARG ",\"keys\":{\"type\":\"array\",\"items\":{\"type\":\"string\"}}", "[\"node\"]" },
	{ "prefab_revert", "prefab.revert", TOOL_PASS, FALSE, TRUE,
	  "Discard an instance's overrides (all, or keys) so it matches its prefab again (undoable).",
	  NODE_ARG ",\"keys\":{\"type\":\"array\",\"items\":{\"type\":\"string\"}}", "[\"node\"]" },
	{ "prefab_sync", "prefab.sync", TOOL_PASS, FALSE, FALSE,
	  "Bring linked instances up to date with their prefab files (one prefab with path, else every prefab used in the level).",
	  "\"path\":{\"type\":\"string\"}", NULL },
	{ "prefab_relink", "prefab.relink", TOOL_PASS, FALSE, FALSE,
	  "Link an unlinked (0.8.0) instance to a prefab again (default: the prefab it was made from).",
	  NODE_ARG ",\"path\":{\"type\":\"string\"}", "[\"node\"]" },
	{ "prefab_unlink", "prefab.unlink", TOOL_PASS, FALSE, TRUE,
	  "Break an instance's prefab link (it becomes ordinary nodes; undoable).",
	  NODE_ARG, "[\"node\"]" },
	/* 0.8.1: viewports */
	{ "set_viewport_layout", "view.layout", TOOL_PASS, FALSE, FALSE,
	  "Viewport layout: single, split-h, split-v or quad; without layout, returns the current one.",
	  "\"layout\":{\"type\":\"string\",\"enum\":[\"single\",\"split-h\",\"split-v\",\"quad\"]}", NULL },
	{ "set_render_mode", "view.render_mode", TOOL_PASS, FALSE, FALSE,
	  "A viewport's render mode (lit, unlit, wireframe, normals, lighting-only, and the 0.9.0 debug views overdraw, uv, light-complexity, ao, reflections); without mode, returns it.",
	  "\"viewport\":{\"type\":\"integer\"},\"mode\":{\"type\":\"string\"}", NULL },
	{ "set_overlay", "view.overlay", TOOL_PASS, FALSE, FALSE,
	  "Toggle a viewport overlay (grid, gizmos, icons, bounds, stats, scene-lighting, shadows, sky, post); without name, lists them.",
	  "\"viewport\":{\"type\":\"integer\"},\"name\":{\"type\":\"string\"},\"enabled\":{\"type\":\"boolean\"}", NULL },
	/* 0.8.1: panels */
	{ "inspector_list", "inspector.list", TOOL_PASS, TRUE, FALSE,
	  "The inspector's sections for a node (or the selection): header, transform, visual, components, scripts; with section, its rows {name, label, group, hint, value, state}.",
	  NODE_ARG ",\"nodes\":{\"type\":\"array\",\"items\":{\"type\":\"string\"}},\"section\":{\"type\":\"string\"},\"component\":{\"type\":\"integer\"}", NULL },
	{ "inspector_get", "inspector.get", TOOL_PASS, TRUE, FALSE,
	  "One inspector property value (as the inspector shows it; mixed for multi-selection).",
	  NODE_ARG ",\"nodes\":{\"type\":\"array\",\"items\":{\"type\":\"string\"}},\"section\":{\"type\":\"string\"},\"component\":{\"type\":\"integer\"},\"name\":{\"type\":\"string\"}", "[\"name\"]" },
	{ "inspector_set", "inspector.set", TOOL_PASS, FALSE, FALSE,
	  "Set an inspector property on a node or several (one undo step), exactly as editing the row would.",
	  NODE_ARG ",\"nodes\":{\"type\":\"array\",\"items\":{\"type\":\"string\"}},\"section\":{\"type\":\"string\"},\"component\":{\"type\":\"integer\"},\"name\":{\"type\":\"string\"},\"value\":{}", "[\"name\",\"value\"]" },
	{ "inspector_reset", "inspector.reset", TOOL_PASS, FALSE, FALSE,
	  "Reset an inspector property to its default (undoable).",
	  NODE_ARG ",\"section\":{\"type\":\"string\"},\"component\":{\"type\":\"integer\"},\"name\":{\"type\":\"string\"}", "[\"name\"]" },
	{ "inspector_apply_override", "inspector.apply_override", TOOL_PASS, FALSE, TRUE,
	  "Apply one prefab override row of the inspector to the prefab file.",
	  NODE_ARG ",\"section\":{\"type\":\"string\"},\"name\":{\"type\":\"string\"}", "[\"name\"]" },
	{ "inspector_revert_override", "inspector.revert_override", TOOL_PASS, FALSE, TRUE,
	  "Revert one prefab override row of the inspector to the prefab's value.",
	  NODE_ARG ",\"section\":{\"type\":\"string\"},\"name\":{\"type\":\"string\"}", "[\"name\"]" },
	{ "inspector_state", "inspector.state", TOOL_PASS, TRUE, FALSE,
	  "The inspector panel's state: the nodes it shows, whether it is locked to them (and the lock mode), live play-mode inspection, component count and prefab link. Read-only; lock or unlock it with run_action id=inspector.toggle-lock.",
	  "", NULL },
	{ "hierarchy_reveal", "hierarchy.reveal", TOOL_PASS, FALSE, FALSE,
	  "Expand the hierarchy down to a node, scroll it into view and (select, default true) select it.",
	  NODE_ARG ",\"select\":{\"type\":\"boolean\"}", "[\"node\"]" },
	{ "hierarchy_rename", "hierarchy.rename", TOOL_PASS, FALSE, FALSE,
	  "Start inline rename of a node in the hierarchy, or with name rename it (undoable).",
	  NODE_ARG ",\"name\":{\"type\":\"string\"}", "[\"node\"]" },
	{ "hierarchy_filter", "hierarchy.filter", TOOL_PASS, FALSE, FALSE,
	  "Filter the hierarchy panel to nodes whose name matches text (ancestors stay visible); empty or absent text clears the filter. Returns the number of matching nodes.",
	  "\"text\":{\"type\":\"string\"}", NULL },
	{ "hierarchy_state", "hierarchy.state", TOOL_PASS, TRUE, FALSE,
	  "The hierarchy panel: page (scene/live), filter, matches, inline editing and the visible rows.", "", NULL },
	{ "asset_reveal", "asset.reveal", TOOL_PASS, FALSE, FALSE,
	  "Show and select an asset in the asset browser.", "\"path\":{\"type\":\"string\"}", "[\"path\"]" },
	{ "asset_rename", "asset.rename", TOOL_PASS, FALSE, FALSE,
	  "Rename an asset file (its .meta goes along, so the guid is kept).",
	  "\"path\":{\"type\":\"string\"},\"name\":{\"type\":\"string\"}", "[\"path\",\"name\"]" },
	{ "asset_move", "asset.move", TOOL_PASS, FALSE, FALSE,
	  "Move an asset into another project folder (with its .meta).",
	  "\"path\":{\"type\":\"string\"},\"dest\":{\"type\":\"string\"}", "[\"path\",\"dest\"]" },
	{ "asset_duplicate", "asset.duplicate", TOOL_PASS, FALSE, FALSE,
	  "Copy an asset next to itself (the copy gets a new guid).", "\"path\":{\"type\":\"string\"}", "[\"path\"]" },
	{ "asset_trash", "asset.trash", TOOL_PASS, FALSE, TRUE,
	  "Move an asset and its .meta to the trash.", "\"path\":{\"type\":\"string\"}", "[\"path\"]" },
	{ "asset_orphans", "asset.orphans", TOOL_PASS, FALSE, TRUE,
	  "List .meta sidecars whose asset is gone; trash=true moves them to the trash.",
	  "\"trash\":{\"type\":\"boolean\"}", NULL },
	{ "profiler_sample", "profiler.sample", TOOL_PASS, FALSE, FALSE,
	  "Editor profiler sections over the last frames: {name, last_ms, avg_ms, min_ms, max_ms, count}; enable switches profiling on or off.",
	  "\"frames\":{\"type\":\"integer\"},\"enable\":{\"type\":\"boolean\"}", NULL },
	/* 0.8.1: code editor */
	{ "code_open", "code.open", TOOL_PASS, FALSE, FALSE,
	  "Open a file in the editor's built-in code editor (a tab), optionally at a 1-based line; returns the tab {path, tab, dirty, line, lines, language}.",
	  "\"path\":{\"type\":\"string\"},\"line\":{\"type\":\"integer\"}", "[\"path\"]" },
	{ "code_list", "code.list", TOOL_PASS, TRUE, FALSE,
	  "The code editor's open tabs: [{path, tab, active, dirty, line, column, lines, language}].", "", NULL },
	{ "code_save", "code.save", TOOL_PASS, FALSE, FALSE,
	  "Save a code editor tab to its file (the active tab without path).", "\"path\":{\"type\":\"string\"}", NULL },
	{ "open_palette", "palette.open", TOOL_PASS, FALSE, FALSE,
	  "Open the command palette (mode commands, the default) or the quick-open file finder (mode files) for the person, optionally pre-filled with query; returns {open, results, scanning} (open is false while a dialog is up).",
	  "\"mode\":{\"type\":\"string\",\"enum\":[\"commands\",\"files\"]},\"query\":{\"type\":\"string\"}", NULL },
	{ "open_preferences", "preferences.open", TOOL_PASS, FALSE, FALSE,
	  "Open the preferences dialog, optionally on a page (e.g. keybindings); returns the page id.",
	  "\"page\":{\"type\":\"string\"}", NULL },
	/* 0.8.1: data editors (input maps, locales, sound banks, themes, HUD
	 * and UI layouts, dialog trees, dungeons). path defaults to the
	 * focused data editor; at is a YAML path such as /actions/jump. */
	{ "data_open", "data.open", TOOL_PASS, FALSE, FALSE,
	  "Open a data file (.yaml: input map, locale, sound bank, theme, HUD layout, UI layout, dialog tree, dungeon) in its data editor. Returns the editor status {path, panel, format, editor, dirty, read_only, focused, changed_on_disk, issues, errors, undo_label, redo_label, state}.",
	  DATA_PATH_ARG, "[\"path\"]" },
	{ "data_list", "data.list", TOOL_PASS, TRUE, FALSE,
	  "The open data editors, as an array of editor statuses (path, format, dirty, issues, errors, undo/redo labels).", "", NULL },
	{ "data_get", "data.get", TOOL_PASS, TRUE, FALSE,
	  "Read a node of a data document: {at, kind, value, keys, length, yaml, line} (value for scalars, keys for mappings, length for sequences, yaml for collections or with as=yaml).",
	  DATA_PATH_ARG "," DATA_AT_ARG ",\"as\":{\"type\":\"string\",\"enum\":[\"yaml\"]}", NULL },
	{ "data_set", "data.set", TOOL_PASS, FALSE, FALSE,
	  "Set a node of a data document (one undo step) to value (any JSON) or to yaml text; edits with the same gesture string merge into one undo step. Returns the status plus ok.",
	  DATA_PATH_ARG "," DATA_AT_ARG ",\"value\":{},\"yaml\":{\"type\":\"string\"},\"gesture\":{\"type\":\"string\"}", "[\"at\"]" },
	{ "data_edit", "data.edit", TOOL_PASS, FALSE, FALSE,
	  "Structural edit of a data document (one undo step) at a YAML path: op insert (index?, yaml) into a sequence, move (from, to) within a sequence, add_key (key, yaml, position?) to a mapping, rename_key (old, new), remove (the node at).",
	  DATA_PATH_ARG "," DATA_AT_ARG ",\"op\":{\"type\":\"string\",\"enum\":[\"insert\",\"move\",\"add_key\",\"rename_key\",\"remove\"]},"
	  "\"index\":{\"type\":\"integer\"},\"yaml\":{\"type\":\"string\"},\"from\":{\"type\":\"integer\"},\"to\":{\"type\":\"integer\"},"
	  "\"key\":{\"type\":\"string\"},\"position\":{\"type\":\"integer\"},\"old\":{\"type\":\"string\"},\"new\":{\"type\":\"string\"}",
	  "[\"op\",\"at\"]" },
	{ "data_undo", "data.undo", TOOL_PASS, FALSE, FALSE,
	  "Undo the last edit of a data document (each file has its own history); ok says whether anything changed.", DATA_PATH_ARG, NULL },
	{ "data_redo", "data.redo", TOOL_PASS, FALSE, FALSE,
	  "Redo the last undone edit of a data document.", DATA_PATH_ARG, NULL },
	{ "data_validate", "data.validate", TOOL_PASS, TRUE, FALSE,
	  "Validate a data document with the engine's own loader: {ok, errors, issues [{severity, path, line, column, message}]}.",
	  DATA_PATH_ARG, NULL },
	{ "data_save", "data.save", TOOL_PASS, FALSE, FALSE,
	  "Save a data document (keeps key order and comments); save_as writes it to a new file, which becomes its path.",
	  DATA_PATH_ARG ",\"save_as\":{\"type\":\"string\"}", NULL },
	{ "data_reload", "data.reload", TOOL_PASS, FALSE, TRUE,
	  "Reload a data document from disk, discarding unsaved edits.", DATA_PATH_ARG, NULL },
	{ "data_close", "data.close", TOOL_PASS, FALSE, TRUE,
	  "Close a data editor. With unsaved changes the person is asked (closed is false while the prompt is up) unless force=true, which discards them.",
	  DATA_PATH_ARG ",\"force\":{\"type\":\"boolean\"}", NULL },
	{ "data_reveal", "data.reveal", TOOL_PASS, FALSE, FALSE,
	  "Show a YAML path (or a line) in a data editor, exactly as clicking a validation issue does.",
	  DATA_PATH_ARG "," DATA_AT_ARG ",\"line\":{\"type\":\"integer\"}", NULL },
	/* 0.9.0: materials (.rmat), user shaders (.rshader) and shader graphs
	 * (.rshadergraph). path is absolute or relative to the project. */
	{ "material_open", "material.open", TOOL_PASS, FALSE, FALSE,
	  "Open a .rmat material in the material editor (live lit preview); returns {path, panel}.",
	  MAT_PATH_ARG, "[\"path\"]" },
	{ "material_create", "material.create", TOOL_PASS, FALSE, FALSE,
	  "Create a .rmat material file (optionally an instance of parent, a material path or guid:). shading_model: lit (default), unlit or custom (then shader names a .rshader). Returns {path}.",
	  MAT_PATH_ARG ",\"parent\":{\"type\":\"string\"},\"shading_model\":{\"type\":\"string\",\"enum\":[\"lit\",\"unlit\",\"custom\"]},"
	  "\"shader\":{\"type\":\"string\"}", "[\"path\"]" },
	{ "material_create_variant", "material.create_variant", TOOL_PASS, FALSE, FALSE,
	  "Create a material instance whose parent is path (unset parameters fall through to it), at dest or next to it. Returns {path}.",
	  MAT_PATH_ARG ",\"dest\":{\"type\":\"string\"}", "[\"path\"]" },
	{ "material_get", "material.get", TOOL_PASS, TRUE, FALSE,
	  "Everything about a material: name, parent, shading_model, shader, base_color [r,g,b,a], metallic, roughness, occlusion_strength, normal_scale, emission_color, emission_strength, alpha_cutoff, height_scale, blend_mode, cull_mode, depth_test, depth_write, double_sided, render_queue, textures {albedo, normal, orm, emission, height, metallic_gloss}, uniforms, overrides (keys set on an instance).",
	  MAT_PATH_ARG, "[\"path\"]" },
	{ "material_set", "material.set", TOOL_PASS, FALSE, FALSE,
	  "Set one .rmat key (undoable in the material editor): e.g. metallic, roughness, base_color [r,g,b,a], blend_mode, textures.albedo (a texture path or guid:), uniforms.tint. Returns {path, key, dirty}; call material_save to write the file.",
	  MAT_PATH_ARG ",\"key\":{\"type\":\"string\"},\"value\":{}", "[\"path\",\"key\",\"value\"]" },
	{ "material_revert", "material.revert", TOOL_PASS, FALSE, TRUE,
	  "Drop an instance's override of key so it falls through to the parent again. Returns {path, key, reverted}.",
	  MAT_PATH_ARG ",\"key\":{\"type\":\"string\"}", "[\"path\",\"key\"]" },
	{ "material_save", "material.save", TOOL_PASS, FALSE, FALSE,
	  "Write an open material to its .rmat file.", MAT_PATH_ARG, "[\"path\"]" },
	{ "material_preview", "material.preview", TOOL_PASS, FALSE, FALSE,
	  "Configure the material editor's live preview: shape sphere, cube or plane; rotate on/off.",
	  MAT_PATH_ARG ",\"shape\":{\"type\":\"string\",\"enum\":[\"sphere\",\"cube\",\"plane\"]},\"rotate\":{\"type\":\"boolean\"}",
	  "[\"path\"]" },
	{ "material_assign", "material.assign", TOOL_PASS, FALSE, FALSE,
	  "Assign a material to a mesh or primitive node (undoable; the same as dropping it on the node in the viewport), optionally to one submesh. Returns {node, material}.",
	  NODE_ARG "," MAT_PATH_ARG ",\"submesh\":{\"type\":\"integer\"}", "[\"node\",\"path\"]" },
	{ "material_list", "material.list", TOOL_PASS, TRUE, FALSE,
	  "The open material editors: {editors: [{path, dirty}]}.", "", NULL },
	{ "shader_create", "shader.create", TOOL_PASS, FALSE, FALSE,
	  "Create a .rshader user shader with a fragment stub; lit (default true) prepends the engine light library so lrg_light_pbr() is available. Returns {path, fragment}.",
	  MAT_PATH_ARG ",\"lit\":{\"type\":\"boolean\"}", "[\"path\"]" },
	{ "shader_compile", "shader.compile", TOOL_PASS, FALSE, FALSE,
	  "Compile a .rshader (needs a windowed editor for GL): {ok, diagnostics [{stage, source, line, column, error, message}]} with lines mapped to the user's files.",
	  MAT_PATH_ARG, "[\"path\"]" },
	{ "shader_diagnostics", "shader.diagnostics", TOOL_PASS, TRUE, FALSE,
	  "The diagnostics of the last compile of a .rshader (same shape as shader_compile).", MAT_PATH_ARG, "[\"path\"]" },
	{ "shader_uniforms", "shader.uniforms", TOOL_PASS, TRUE, FALSE,
	  "The uniforms a .rshader declares: {uniforms [{name, type, default, min, max, hint}]} (what the uniform inspector shows).",
	  MAT_PATH_ARG, "[\"path\"]" },
	{ "shader_reload", "shader.reload", TOOL_PASS, FALSE, FALSE,
	  "Reload a .rshader from disk and recompile (keeps the last good program on failure): {ok, diagnostics}.",
	  MAT_PATH_ARG, "[\"path\"]" },
	{ "shadergraph_open", "shadergraph.open", TOOL_PASS, FALSE, FALSE,
	  "Open a .rshadergraph in the shader graph editor; returns {path, panel}.", MAT_PATH_ARG, "[\"path\"]" },
	{ "shadergraph_create", "shadergraph.create", TOOL_PASS, FALSE, FALSE,
	  "Create a .rshadergraph with a master output node (master pbr, the default, or unlit). Returns {path}.",
	  MAT_PATH_ARG ",\"master\":{\"type\":\"string\",\"enum\":[\"pbr\",\"unlit\"]}", "[\"path\"]" },
	{ "shadergraph_node_types", "shadergraph.node_types", TOOL_PASS, TRUE, FALSE,
	  "The node library: {types [{type, label, category, inputs, outputs}]} (ports with their types).", "", NULL },
	{ "shadergraph_get", "shadergraph.get", TOOL_PASS, TRUE, FALSE,
	  "A graph: {name, nodes [{id, type, x, y, params}], links [{from, from_port, to, to_port}]}.", MAT_PATH_ARG, "[\"path\"]" },
	{ "shadergraph_add_node", "shadergraph.add_node", TOOL_PASS, FALSE, FALSE,
	  "Add a node of type (see shadergraph_node_types) at x, y; id is generated unless given. Returns {id}.",
	  MAT_PATH_ARG ",\"type\":{\"type\":\"string\"},\"id\":{\"type\":\"string\"},\"x\":{\"type\":\"number\"},\"y\":{\"type\":\"number\"}",
	  "[\"path\",\"type\"]" },
	{ "shadergraph_remove_node", "shadergraph.remove_node", TOOL_PASS, FALSE, TRUE,
	  "Remove a node and its links. Returns {removed}.", MAT_PATH_ARG ",\"id\":{\"type\":\"string\"}", "[\"path\",\"id\"]" },
	{ "shadergraph_connect", "shadergraph.connect", TOOL_PASS, FALSE, FALSE,
	  "Link output from_port of node from to input to_port of node to (replaces the input's old link). Returns {ok}.",
	  MAT_PATH_ARG ",\"from\":{\"type\":\"string\"},\"from_port\":{\"type\":\"string\"},\"to\":{\"type\":\"string\"},\"to_port\":{\"type\":\"string\"}",
	  "[\"path\",\"from\",\"from_port\",\"to\",\"to_port\"]" },
	{ "shadergraph_disconnect", "shadergraph.disconnect", TOOL_PASS, FALSE, TRUE,
	  "Remove the link into input to_port of node to. Returns {removed}.",
	  MAT_PATH_ARG ",\"to\":{\"type\":\"string\"},\"to_port\":{\"type\":\"string\"}", "[\"path\",\"to\",\"to_port\"]" },
	{ "shadergraph_set_param", "shadergraph.set_param", TOOL_PASS, FALSE, FALSE,
	  "Set a node parameter (e.g. a parameter node's name or default, a swizzle mask). Returns {ok}.",
	  MAT_PATH_ARG ",\"id\":{\"type\":\"string\"},\"key\":{\"type\":\"string\"},\"value\":{}", "[\"path\",\"id\",\"key\",\"value\"]" },
	{ "shadergraph_set_input", "shadergraph.set_input", TOOL_PASS, FALSE, FALSE,
	  "Set the constant of an unlinked input port (number or vector), or null to clear it. Returns {ok}.",
	  MAT_PATH_ARG ",\"id\":{\"type\":\"string\"},\"port\":{\"type\":\"string\"},\"value\":{}", "[\"path\",\"id\",\"port\"]" },
	{ "shadergraph_validate", "shadergraph.validate", TOOL_PASS, TRUE, FALSE,
	  "Validate a graph: {ok, problems [{code, node, port, message}]} (cycles, type mismatches, missing inputs).",
	  MAT_PATH_ARG, "[\"path\"]" },
	{ "shadergraph_glsl", "shadergraph.glsl", TOOL_PASS, TRUE, FALSE,
	  "The GLSL fragment source generated from a graph: {fragment}.", MAT_PATH_ARG, "[\"path\"]" },
	{ "shadergraph_save", "shadergraph.save", TOOL_PASS, FALSE, FALSE,
	  "Save a graph to its .rshadergraph and write the generated .rshader next to it. Returns {path}.",
	  MAT_PATH_ARG, "[\"path\"]" },
	/* 0.9.0: the level look (environment block, .rpost post stacks),
	 * LOD preview and per-viewport render layers. */
	{ "environment_get", "environment.get", TOOL_PASS, TRUE, FALSE,
	  "The level's environment block: {present, environment {sky {source, color, zenith, horizon, ground, path, faces, exposure, rotation}, ibl {enabled, diffuse_intensity, specular_intensity, resolution}, ambient {color, intensity}, fog {enabled, type, color, density, start, end, height_base, height_falloff}, volumetric {enabled, density, anisotropy, steps, max_distance, intensity}, post_stack}, baked, needs_bake}. Colours are sRGB 0..1.",
	  "", NULL },
	{ "environment_set", "environment.set", TOOL_PASS, FALSE, FALSE,
	  "Change the environment block (one undo step; creates the block when the level has none): key is a dotted path such as fog.density, sky.source (color, gradient, dome, cubemap, equirect), sky.zenith ([r,g,b]), ibl.enabled, volumetric.enabled or post_stack; or values {key: value, ...} for several at once. Returns the same as environment_get.",
	  "\"key\":{\"type\":\"string\"},\"value\":{},\"values\":{\"type\":\"object\"}", NULL },
	{ "environment_reset", "environment.reset", TOOL_PASS, FALSE, TRUE,
	  "Remove the level's environment block (undoable); the level falls back to the default look. Returns {present: false}.",
	  "", NULL },
	{ "environment_bake", "environment.bake", TOOL_PASS, FALSE, FALSE,
	  "Bake the environment's image based lighting and capture the reflection probes (windowed editors; runs over the next frames). Returns {started}; poll environment_progress.",
	  "", NULL },
	{ "environment_progress", "environment.progress", TOOL_PASS, TRUE, FALSE,
	  "State of the environment bake: {baking, phase, progress 0..1, error, baked}.", "", NULL },
	{ "environment_show", "environment.show", TOOL_PASS, FALSE, FALSE,
	  "Bring the Environment panel to the front for the person. Returns {panel}.", "", NULL },
	{ "post_effect_types", "post.effect_types", TOOL_PASS, TRUE, FALSE,
	  "The post effects a .rpost can use: {types [{type, params [{name, type, default, min, max}]}]}.", "", NULL },
	{ "post_open", "post.open", TOOL_PASS, FALSE, FALSE,
	  "Open a .rpost post stack in the post stack editor (live viewport preview). Returns {path, panel}.",
	  MAT_PATH_ARG, "[\"path\"]" },
	{ "post_create", "post.create", TOOL_PASS, FALSE, FALSE,
	  "Create a .rpost file: preset default (bloom, tonemap, vignette) or empty. Returns {path}.",
	  MAT_PATH_ARG ",\"preset\":{\"type\":\"string\",\"enum\":[\"default\",\"empty\"]}", "[\"path\"]" },
	{ "post_get", "post.get", TOOL_PASS, TRUE, FALSE,
	  "A post stack: {path, name, quality, hdr, dirty, effects [{index, type, enabled, weight, params}]} in run order.",
	  MAT_PATH_ARG, "[\"path\"]" },
	{ "post_add", "post.add", TOOL_PASS, FALSE, FALSE,
	  "Add an effect of type (see post_effect_types) at index (default the end). Undoable. Returns {index}.",
	  MAT_PATH_ARG ",\"type\":{\"type\":\"string\"},\"index\":{\"type\":\"integer\"}", "[\"path\",\"type\"]" },
	{ "post_remove", "post.remove", TOOL_PASS, FALSE, TRUE,
	  "Remove the effect at index. Undoable. Returns {removed}.",
	  MAT_PATH_ARG ",\"index\":{\"type\":\"integer\"}", "[\"path\",\"index\"]" },
	{ "post_move", "post.move", TOOL_PASS, FALSE, FALSE,
	  "Move the effect at from to index to (changes the run order). Returns {index}.",
	  MAT_PATH_ARG ",\"from\":{\"type\":\"integer\"},\"to\":{\"type\":\"integer\"}", "[\"path\",\"from\",\"to\"]" },
	{ "post_set", "post.set", TOOL_PASS, FALSE, FALSE,
	  "Set enabled, weight (0..1) or a parameter of the effect at index (e.g. intensity, threshold, exposure). Undoable. Returns {ok}.",
	  MAT_PATH_ARG ",\"index\":{\"type\":\"integer\"},\"key\":{\"type\":\"string\"},\"value\":{}", "[\"path\",\"index\",\"key\",\"value\"]" },
	{ "post_save", "post.save", TOOL_PASS, FALSE, FALSE,
	  "Write an open post stack to its .rpost file. Returns {path}.", MAT_PATH_ARG, "[\"path\"]" },
	{ "post_preview", "post.preview", TOOL_PASS, FALSE, FALSE,
	  "Preview a post stack live in the viewports (enabled true) or go back to the level's own look (enabled false). Returns {preview: path or null}.",
	  "\"path\":{\"type\":\"string\"},\"enabled\":{\"type\":\"boolean\"}", "[\"enabled\"]" },
	{ "post_save_look", "post.save_look", TOOL_PASS, FALSE, FALSE,
	  "Save what a viewport's post chain currently does as a .rpost at path (\"save current look\"). Returns {path, effects}.",
	  MAT_PATH_ARG ",\"viewport\":{\"type\":\"integer\"}", "[\"path\"]" },
	{ "post_assign", "post.assign", TOOL_PASS, FALSE, FALSE,
	  "Make a .rpost the level's post stack (environment.post_stack; undoable). Returns {post_stack}.",
	  MAT_PATH_ARG, "[\"path\"]" },
	{ "lod_get", "lod.get", TOOL_PASS, TRUE, FALSE,
	  "LOD preview of a mesh node whose model has LOD levels: {node, asset, levels [{level, triangles, screen_size, error}], current, forced}.",
	  NODE_ARG, "[\"node\"]" },
	{ "lod_force", "lod.force", TOOL_PASS, FALSE, FALSE,
	  "Force the viewport to draw one LOD level of a node's model (preview only, not saved); level -1 goes back to automatic. Returns {node, forced}.",
	  NODE_ARG ",\"level\":{\"type\":\"integer\"}", "[\"node\",\"level\"]" },
	{ "set_view_layers", "view.layers", TOOL_PASS, FALSE, FALSE,
	  "A viewport's render-layer cull mask (bit N = layer N; 0 default, 1 ui, 2 editor-only, 3 ignore-raycast, 4 transparent-fx, 5 water, 6 sky): set mask, or one layer (index or name) with enabled; without arguments returns {viewport, mask, layers [{index, name, enabled}]}.",
	  "\"viewport\":{\"type\":\"integer\"},\"mask\":{\"type\":\"integer\"},\"layer\":{},\"enabled\":{\"type\":\"boolean\"}", NULL },
	/* 0.9.0 M3/M4 editor wave: the lighting bake (bake.*), sequences
	 * (sequence.*), 2D skeletons (skel2d.*), animation graphs (anim.*),
	 * bone maps (bonemap.*) and glTF clips (clip.*) */
	{ "bake_get", "bake.get", TOOL_PASS, TRUE, FALSE,
	  "The level's lighting bake: settings (every LrgLightmapBakeSettings key with its value: texels_per_unit, samples, bounces, max_lightmap_size, padding, denoise, probe_spacing, probe_samples, auto_probe_volume, seed, threads, light_radius, sun_angle), set_keys (those written in lighting: settings:), lightmap and probes (the level's references), baking, phase, progress, auto_save, preview, result {present, unsaved, objects, lights, texels, entries, pages, grids, probes, seconds}.",
	  "", NULL },
	{ "bake_set", "bake.set", TOOL_PASS, FALSE, FALSE,
	  "Write lighting: settings: keys (undoable): key + value, or values {key: value} as one undo step; reset=true drops every key; auto_save=false keeps finished bakes in memory until bake_save. Booleans may be given as true/false. Returns the same as bake_get.",
	  "\"key\":{\"type\":\"string\",\"description\":\"A bake setting, e.g. samples\"},\"value\":{\"description\":\"Number or boolean\"},\"values\":{\"type\":\"object\",\"description\":\"Several settings at once\"},\"reset\":{\"type\":\"boolean\"},\"auto_save\":{\"type\":\"boolean\"}", NULL },
	{ "bake_start", "bake.start", TOOL_PASS, FALSE, FALSE,
	  "Bake the open level's lighting on the CPU (worker threads; works without a window): static_lighting meshes get lightmaps, probe-volume nodes probe grids, from lights whose bake_mode is mixed or baked plus the sky. Returns {started, objects, lights}; poll bake_progress. With auto save (default) and a saved level the .rlightmap / .rprobes are written next to the level and its lighting: references set (one undo step). save=false keeps the result unsaved.",
	  "\"save\":{\"type\":\"boolean\",\"description\":\"Save when done (default true)\"}", NULL },
	{ "bake_progress", "bake.progress", TOOL_PASS, TRUE, FALSE,
	  "State of the lighting bake: {baking, phase (idle, prepare, lightmaps, postprocess, probes, done, cancelled, failed), progress 0..1, error, unsaved}.",
	  "", NULL },
	{ "bake_cancel", "bake.cancel", TOOL_PASS, FALSE, FALSE,
	  "Cancel the running lighting bake (the level keeps its previous lighting). Returns {cancelled}.",
	  "", NULL },
	{ "bake_save", "bake.save", TOOL_PASS, FALSE, FALSE,
	  "Write the last bake: <level>.rlightmap with its .hdr pages and <level>-<n>.rprobes, into dir (default the level's directory; required for a level never saved), and point lighting: lightmap / probes at them (one undo step). Returns the same as bake_get.",
	  "\"dir\":{\"type\":\"string\",\"description\":\"Directory to write into\"}", NULL },
	{ "bake_clear", "bake.clear", TOOL_PASS, FALSE, TRUE,
	  "Drop the level's lightmap and probe references and the unsaved result (undoable; settings and files stay). Returns {cleared}.",
	  "", NULL },
	{ "bake_evaluate", "bake.evaluate", TOOL_PASS, TRUE, FALSE,
	  "Probe the baked light (linear irradiance / pi, as a lightmap texel holds it) at a world point on a surface with its normal; without position, the surface under the active viewport's centre. Returns {position, normal, rgb, luminance}. Uses the last bake's scene (or collects one; it does not bake).",
	  "\"position\":{\"type\":\"array\",\"items\":{\"type\":\"number\"},\"minItems\":3,\"maxItems\":3,\"description\":\"World point on a surface\"},\"normal\":{\"type\":\"array\",\"items\":{\"type\":\"number\"},\"minItems\":3,\"maxItems\":3,\"description\":\"Surface normal (default up)\"}", NULL },
	{ "bake_preview", "bake.preview", TOOL_PASS, FALSE, FALSE,
	  "Whether the authoring viewport applies the baked lighting (show), and which lightmap atlas page the bake panel shows (page). Returns {show, page, pages}.",
	  "\"show\":{\"type\":\"boolean\"},\"page\":{\"type\":\"integer\"}", NULL },
	{ "bake_show", "bake.show", TOOL_PASS, FALSE, FALSE,
	  "Show the Lighting (bake) panel. Returns {panel, shown}.",
	  "", NULL },
	{ "sequence_list", "sequence.list", TOOL_PASS, TRUE, FALSE,
	  "The sequence open in the sequencer panel: {open (path or null), dirty}.",
	  "", NULL },
	{ "sequence_open", "sequence.open", TOOL_PASS, FALSE, FALSE,
	  "Open a .rseq in the sequencer panel. Returns {path, panel}.",
	  "\"path\":{\"type\":\"string\",\"description\":\"The .rseq, absolute or relative to the project\"}", "[\"path\"]" },
	{ "sequence_create", "sequence.create", TOOL_PASS, FALSE, FALSE,
	  "Create a .rseq (fails if it exists) and open it. play_mode once, loop or ping-pong. Returns {path}.",
	  "\"path\":{\"type\":\"string\",\"description\":\"New .rseq path\"},\"name\":{\"type\":\"string\"},\"length\":{\"type\":\"number\",\"description\":\"Seconds (default 5)\"},\"fps\":{\"type\":\"number\",\"description\":\"Default 30\"},\"play_mode\":{\"type\":\"string\",\"enum\":[\"once\",\"loop\",\"ping-pong\"]}", "[\"path\"]" },
	{ "sequence_get", "sequence.get", TOOL_PASS, TRUE, FALSE,
	  "Everything about a sequence: name, length, fps, play_mode, dirty, time, playing, bindings [{guid, name}], tracks [{index, kind, name, binding, binding_name, muted, bone, component, property, channels {\"location.x\": [key]}, keys, sections [{index, start, duration, end, name, offset, rate, loop, weight, payload}]}]; key = {index, time, value, tangent, in, out}.",
	  "\"path\":{\"type\":\"string\",\"description\":\"The .rseq, absolute or relative to the project; omit for the sequence open in the sequencer\"}", NULL },
	{ "sequence_set", "sequence.set", TOOL_PASS, FALSE, FALSE,
	  "Change a sequence's name, length, fps or play_mode (one undo step). Returns the same as sequence_get.",
	  "\"path\":{\"type\":\"string\",\"description\":\"The .rseq, absolute or relative to the project; omit for the sequence open in the sequencer\"},\"name\":{\"type\":\"string\"},\"length\":{\"type\":\"number\"},\"fps\":{\"type\":\"number\"},\"play_mode\":{\"type\":\"string\",\"enum\":[\"once\",\"loop\",\"ping-pong\"]}", NULL },
	{ "sequence_add_track", "sequence.add_track", TOOL_PASS, FALSE, FALSE,
	  "Add a track: kind transform, property, camera-cut, audio, event, animation or subsequence; binding is a node GUID, node path or binding override name; bone / component / property for bone and property tracks. Returns {index}.",
	  "\"path\":{\"type\":\"string\",\"description\":\"The .rseq, absolute or relative to the project; omit for the sequence open in the sequencer\"},\"kind\":{\"type\":\"string\",\"enum\":[\"transform\",\"property\",\"camera-cut\",\"audio\",\"event\",\"animation\",\"subsequence\"]},\"name\":{\"type\":\"string\"},\"binding\":{\"type\":\"string\"},\"bone\":{\"type\":\"string\"},\"component\":{\"type\":\"string\"},\"property\":{\"type\":\"string\"}", "[\"kind\"]" },
	{ "sequence_add_key", "sequence.add_key", TOOL_PASS, FALSE, FALSE,
	  "Add a key to a track (channel such as location.x for transform tracks). tangent auto, user, broken, linear or constant; in / out tangent slopes. Returns {index}.",
	  "\"path\":{\"type\":\"string\",\"description\":\"The .rseq, absolute or relative to the project; omit for the sequence open in the sequencer\"},\"track\":{\"type\":\"integer\",\"description\":\"Track index\"},\"channel\":{\"type\":\"string\"},\"time\":{\"type\":\"number\",\"description\":\"Seconds\"},\"value\":{},\"tangent\":{\"type\":\"string\",\"enum\":[\"auto\",\"user\",\"broken\",\"linear\",\"constant\"]},\"in\":{\"type\":\"number\"},\"out\":{\"type\":\"number\"}", "[\"track\",\"time\"]" },
	{ "sequence_set_key", "sequence.set_key", TOOL_PASS, FALSE, FALSE,
	  "Change a key, picked by key (index) or at (time): move it (time), value, tangent, in, out. Returns {index}.",
	  "\"path\":{\"type\":\"string\",\"description\":\"The .rseq, absolute or relative to the project; omit for the sequence open in the sequencer\"},\"track\":{\"type\":\"integer\"},\"channel\":{\"type\":\"string\"},\"key\":{\"type\":\"integer\"},\"at\":{\"type\":\"number\"},\"time\":{\"type\":\"number\"},\"value\":{},\"tangent\":{\"type\":\"string\"},\"in\":{\"type\":\"number\"},\"out\":{\"type\":\"number\"}", "[\"track\"]" },
	{ "sequence_add_section", "sequence.add_section", TOOL_PASS, FALSE, FALSE,
	  "Add a section to a camera-cut, audio, animation or subsequence track: start, duration, name (camera / clip / sound / sequence), payload, offset, rate, loop, weight. Returns {index}.",
	  "\"path\":{\"type\":\"string\",\"description\":\"The .rseq, absolute or relative to the project; omit for the sequence open in the sequencer\"},\"track\":{\"type\":\"integer\"},\"start\":{\"type\":\"number\"},\"duration\":{\"type\":\"number\"},\"name\":{\"type\":\"string\"},\"payload\":{\"type\":\"string\"},\"offset\":{\"type\":\"number\"},\"rate\":{\"type\":\"number\"},\"loop\":{\"type\":\"boolean\"},\"weight\":{\"type\":\"number\"}", "[\"track\",\"start\",\"name\"]" },
	{ "sequence_remove", "sequence.remove", TOOL_PASS, FALSE, TRUE,
	  "Remove a track (track), a key (track + channel + key or at), a section (track + section) or a binding (binding). Returns {removed}.",
	  "\"path\":{\"type\":\"string\",\"description\":\"The .rseq, absolute or relative to the project; omit for the sequence open in the sequencer\"},\"track\":{\"type\":\"integer\"},\"channel\":{\"type\":\"string\"},\"key\":{\"type\":\"integer\"},\"at\":{\"type\":\"number\"},\"section\":{\"type\":\"integer\"},\"binding\":{\"type\":\"string\"}", NULL },
	{ "sequence_bind", "sequence.bind", TOOL_PASS, FALSE, FALSE,
	  "Bind a level node (GUID or name path) to the sequence, optionally under a name and onto a track. Returns {guid, name}.",
	  "\"path\":{\"type\":\"string\",\"description\":\"The .rseq, absolute or relative to the project; omit for the sequence open in the sequencer\"},\"node\":{\"type\":\"string\"},\"name\":{\"type\":\"string\"},\"track\":{\"type\":\"integer\"}", "[\"node\"]" },
	{ "sequence_play", "sequence.play", TOOL_PASS, FALSE, FALSE,
	  "Preview playback: playing (false pauses), stop (stop and rewind), mode once / loop / ping-pong. Returns {time, playing, mode}.",
	  "\"path\":{\"type\":\"string\",\"description\":\"The .rseq, absolute or relative to the project; omit for the sequence open in the sequencer\"},\"playing\":{\"type\":\"boolean\"},\"stop\":{\"type\":\"boolean\"},\"mode\":{\"type\":\"string\"}", NULL },
	{ "sequence_scrub", "sequence.scrub", TOOL_PASS, FALSE, FALSE,
	  "Scrub the preview to time (seconds) or frame. Returns {time, frame, events [{name, payload, time}]} (the events crossed).",
	  "\"path\":{\"type\":\"string\",\"description\":\"The .rseq, absolute or relative to the project; omit for the sequence open in the sequencer\"},\"time\":{\"type\":\"number\"},\"frame\":{\"type\":\"integer\"}", NULL },
	{ "sequence_save", "sequence.save", TOOL_PASS, FALSE, FALSE,
	  "Write a sequence to its file, or to save_as. Returns {path}.",
	  "\"path\":{\"type\":\"string\",\"description\":\"The .rseq, absolute or relative to the project; omit for the sequence open in the sequencer\"},\"save_as\":{\"type\":\"string\"}", NULL },
	{ "sequence_undo", "sequence.undo", TOOL_PASS, FALSE, FALSE,
	  "Undo (or with redo=true redo) the last edit of a sequence in the sequencer. Returns {ok, dirty}.",
	  "\"path\":{\"type\":\"string\",\"description\":\"The .rseq; omit for the open one\"},\"redo\":{\"type\":\"boolean\"}", NULL },
	{ "timeline_tangent", "timeline.tangent", TOOL_PASS, FALSE, FALSE,
	  "Set the tangent mode of a level timeline key (node and time, optional channel): eased, auto, user, broken, linear or constant; slope (or in / out) for user and broken. Returns {changed}.",
	  "\"name\":{\"type\":\"string\",\"description\":\"Timeline name (default the open one)\"}," NODE_ARG ",\"time\":{\"type\":\"number\"},\"channel\":{\"type\":\"string\"},\"tangent\":{\"type\":\"string\",\"enum\":[\"eased\",\"auto\",\"user\",\"broken\",\"linear\",\"constant\"]},\"slope\":{\"type\":\"number\"},\"in\":{\"type\":\"number\"},\"out\":{\"type\":\"number\"}",
	  "[\"node\",\"time\",\"tangent\"]" },
	{ "sequence_from_timeline", "sequence.from_timeline", TOOL_PASS, FALSE, FALSE,
	  "Convert a level timeline (name) into a new .rseq. Returns {path}.",
	  "\"timeline\":{\"type\":\"string\",\"description\":\"Level timeline name\"},\"path\":{\"type\":\"string\",\"description\":\"New .rseq\"},\"fps\":{\"type\":\"number\",\"description\":\"Default 30\"}", "[\"timeline\",\"path\"]" },
	{ "skel2d_create", "skel2d.create", TOOL_PASS, FALSE, FALSE,
	  "Create a .rskel2d 2D skeleton (fails if it exists) and open it. Returns {path}.",
	  "\"path\":{\"type\":\"string\",\"description\":\"The .rskel2d, absolute or relative to the project (opens its editor tab)\"},\"name\":{\"type\":\"string\"}", "[\"path\"]" },
	{ "skel2d_open", "skel2d.open", TOOL_PASS, FALSE, FALSE,
	  "Open a .rskel2d in the 2D skeleton editor. Returns {path, panel}.",
	  "\"path\":{\"type\":\"string\",\"description\":\"The .rskel2d, absolute or relative to the project (opens its editor tab)\"}", "[\"path\"]" },
	{ "skel2d_get", "skel2d.get", TOOL_PASS, TRUE, FALSE,
	  "Everything about a 2D skeleton: name, dirty, mode, tool, skin, clip, time, playing, loop, selected {bone, slot, attachment}, bones [{name, parent, x, y, rotation, scale, length}], slots [{name, bone, attachment, color}], draw_order, skins [{name, attachments [{slot, name, image, vertices, triangles, weighted}]}], clips [{name, duration, loop, bones, slots}].",
	  "\"path\":{\"type\":\"string\",\"description\":\"The .rskel2d, absolute or relative to the project (opens its editor tab)\"}", "[\"path\"]" },
	{ "skel2d_add_bone", "skel2d.add_bone", TOOL_PASS, FALSE, FALSE,
	  "Add a bone (setup pose; parent by name). Returns {bone, index}.",
	  "\"path\":{\"type\":\"string\",\"description\":\"The .rskel2d, absolute or relative to the project (opens its editor tab)\"},\"name\":{\"type\":\"string\"},\"parent\":{\"type\":\"string\"},\"x\":{\"type\":\"number\"},\"y\":{\"type\":\"number\"},\"rotation\":{\"type\":\"number\",\"description\":\"Degrees\"},\"length\":{\"type\":\"number\"}", "[\"path\",\"name\"]" },
	{ "skel2d_set_bone", "skel2d.set_bone", TOOL_PASS, FALSE, FALSE,
	  "Change a bone's setup pose or name. Returns {bone, x, y, rotation, scale, length}.",
	  "\"path\":{\"type\":\"string\",\"description\":\"The .rskel2d, absolute or relative to the project (opens its editor tab)\"},\"bone\":{\"type\":\"string\"},\"x\":{\"type\":\"number\"},\"y\":{\"type\":\"number\"},\"rotation\":{\"type\":\"number\"},\"scale_x\":{\"type\":\"number\"},\"scale_y\":{\"type\":\"number\"},\"length\":{\"type\":\"number\"},\"name\":{\"type\":\"string\"}", "[\"path\",\"bone\"]" },
	{ "skel2d_remove_bone", "skel2d.remove_bone", TOOL_PASS, FALSE, TRUE,
	  "Remove a bone and its children. Returns {removed [names]}.",
	  "\"path\":{\"type\":\"string\",\"description\":\"The .rskel2d, absolute or relative to the project (opens its editor tab)\"},\"bone\":{\"type\":\"string\"}", "[\"path\",\"bone\"]" },
	{ "skel2d_add_slot", "skel2d.add_slot", TOOL_PASS, FALSE, FALSE,
	  "Add a slot on a bone. Returns {slot, index}.",
	  "\"path\":{\"type\":\"string\",\"description\":\"The .rskel2d, absolute or relative to the project (opens its editor tab)\"},\"name\":{\"type\":\"string\"},\"bone\":{\"type\":\"string\"},\"attachment\":{\"type\":\"string\"}", "[\"path\",\"name\",\"bone\"]" },
	{ "skel2d_set_slot", "skel2d.set_slot", TOOL_PASS, FALSE, FALSE,
	  "Change a slot's attachment (null clears), colour [r,g,b,a] or name. Returns {slot, attachment, color}.",
	  "\"path\":{\"type\":\"string\",\"description\":\"The .rskel2d, absolute or relative to the project (opens its editor tab)\"},\"slot\":{\"type\":\"string\"},\"attachment\":{\"description\":\"Name or null\"},\"color\":{\"type\":\"array\"},\"name\":{\"type\":\"string\"}", "[\"path\",\"slot\"]" },
	{ "skel2d_remove_slot", "skel2d.remove_slot", TOOL_PASS, FALSE, TRUE,
	  "Remove a slot. Returns {removed}.",
	  "\"path\":{\"type\":\"string\",\"description\":\"The .rskel2d, absolute or relative to the project (opens its editor tab)\"},\"slot\":{\"type\":\"string\"}", "[\"path\",\"slot\"]" },
	{ "skel2d_draw_order", "skel2d.draw_order", TOOL_PASS, FALSE, FALSE,
	  "Set the draw order (every slot name, back to front). Returns {order}.",
	  "\"path\":{\"type\":\"string\",\"description\":\"The .rskel2d, absolute or relative to the project (opens its editor tab)\"},\"order\":{\"type\":\"array\",\"items\":{\"type\":\"string\"}}", "[\"path\",\"order\"]" },
	{ "skel2d_attach", "skel2d.attach", TOOL_PASS, FALSE, FALSE,
	  "Add an image attachment to a slot (a region or a columns x rows grid mesh). Returns {slot, name, vertices, triangles}.",
	  "\"path\":{\"type\":\"string\",\"description\":\"The .rskel2d, absolute or relative to the project (opens its editor tab)\"},\"skin\":{\"type\":\"string\",\"description\":\"Skin name (default the active skin)\"},\"slot\":{\"type\":\"string\"},\"name\":{\"type\":\"string\"},\"image\":{\"type\":\"string\"},\"x\":{\"type\":\"number\"},\"y\":{\"type\":\"number\"},\"width\":{\"type\":\"number\"},\"height\":{\"type\":\"number\"},\"columns\":{\"type\":\"integer\"},\"rows\":{\"type\":\"integer\"}", "[\"path\",\"slot\",\"name\"]" },
	{ "skel2d_detach", "skel2d.detach", TOOL_PASS, FALSE, TRUE,
	  "Remove an attachment from a slot. Returns {removed}.",
	  "\"path\":{\"type\":\"string\",\"description\":\"The .rskel2d, absolute or relative to the project (opens its editor tab)\"},\"skin\":{\"type\":\"string\",\"description\":\"Skin name (default the active skin)\"},\"slot\":{\"type\":\"string\"},\"name\":{\"type\":\"string\"}", "[\"path\",\"slot\",\"name\"]" },
	{ "skel2d_mesh", "skel2d.mesh", TOOL_PASS, FALSE, FALSE,
	  "Edit an attachment's mesh: add [[x,y],...], move {index, x, y}, remove index, triangulate. Returns {vertices, triangles}.",
	  "\"path\":{\"type\":\"string\",\"description\":\"The .rskel2d, absolute or relative to the project (opens its editor tab)\"},\"skin\":{\"type\":\"string\",\"description\":\"Skin name (default the active skin)\"},\"slot\":{\"type\":\"string\"},\"name\":{\"type\":\"string\"},\"add\":{\"type\":\"array\"},\"move\":{\"type\":\"object\"},\"remove\":{\"type\":\"integer\"},\"triangulate\":{\"type\":\"boolean\"}", "[\"path\",\"slot\",\"name\"]" },
	{ "skel2d_auto_weights", "skel2d.auto_weights", TOOL_PASS, FALSE, FALSE,
	  "Compute a mesh's bone weights automatically (bones limits them). Returns {vertices, weighted}.",
	  "\"path\":{\"type\":\"string\",\"description\":\"The .rskel2d, absolute or relative to the project (opens its editor tab)\"},\"skin\":{\"type\":\"string\",\"description\":\"Skin name (default the active skin)\"},\"slot\":{\"type\":\"string\"},\"name\":{\"type\":\"string\"},\"bones\":{\"type\":\"array\",\"items\":{\"type\":\"string\"}},\"falloff\":{\"type\":\"number\"},\"max_influences\":{\"type\":\"integer\"},\"smooth\":{\"type\":\"integer\"}", "[\"path\",\"slot\",\"name\"]" },
	{ "skel2d_paint_weights", "skel2d.paint_weights", TOOL_PASS, FALSE, FALSE,
	  "One weight-brush dab for a bone at x, y (radius, strength, erase). Returns {changed}.",
	  "\"path\":{\"type\":\"string\",\"description\":\"The .rskel2d, absolute or relative to the project (opens its editor tab)\"},\"skin\":{\"type\":\"string\",\"description\":\"Skin name (default the active skin)\"},\"slot\":{\"type\":\"string\"},\"name\":{\"type\":\"string\"},\"bone\":{\"type\":\"string\"},\"x\":{\"type\":\"number\"},\"y\":{\"type\":\"number\"},\"radius\":{\"type\":\"number\"},\"strength\":{\"type\":\"number\"},\"erase\":{\"type\":\"boolean\"}", "[\"path\",\"slot\",\"name\",\"bone\",\"x\",\"y\"]" },
	{ "skel2d_skin", "skel2d.skin", TOOL_PASS, FALSE, FALSE,
	  "Add, remove or activate a skin. Returns {skins, active}.",
	  "\"path\":{\"type\":\"string\",\"description\":\"The .rskel2d, absolute or relative to the project (opens its editor tab)\"},\"name\":{\"type\":\"string\"},\"op\":{\"type\":\"string\",\"enum\":[\"add\",\"remove\",\"activate\"]}", "[\"path\",\"name\",\"op\"]" },
	{ "skel2d_clip", "skel2d.clip", TOOL_PASS, FALSE, FALSE,
	  "Add, remove, select or set (duration, loop) an animation clip. Returns {clips, clip}.",
	  "\"path\":{\"type\":\"string\",\"description\":\"The .rskel2d, absolute or relative to the project (opens its editor tab)\"},\"name\":{\"type\":\"string\"},\"op\":{\"type\":\"string\",\"enum\":[\"add\",\"remove\",\"select\",\"set\"]},\"duration\":{\"type\":\"number\"},\"loop\":{\"type\":\"boolean\"}", "[\"path\",\"name\",\"op\"]" },
	{ "skel2d_key", "skel2d.key", TOOL_PASS, FALSE, FALSE,
	  "Key a bone or slot (target) channel translate, rotate, scale, color or attachment at time (values, attachment, curve linear / stepped / [x1,y1,x2,y2]); remove=true deletes the key. Returns {target, channel, keys}.",
	  "\"path\":{\"type\":\"string\",\"description\":\"The .rskel2d, absolute or relative to the project (opens its editor tab)\"},\"clip\":{\"type\":\"string\"},\"target\":{\"type\":\"string\"},\"channel\":{\"type\":\"string\",\"enum\":[\"translate\",\"rotate\",\"scale\",\"color\",\"attachment\"]},\"time\":{\"type\":\"number\"},\"values\":{\"type\":\"array\"},\"attachment\":{\"type\":\"string\"},\"curve\":{},\"remove\":{\"type\":\"boolean\"}", "[\"path\",\"target\",\"channel\",\"time\"]" },
	{ "skel2d_play", "skel2d.play", TOOL_PASS, FALSE, FALSE,
	  "Preview a clip: playing, loop, time. Returns {clip, time, playing, loop}.",
	  "\"path\":{\"type\":\"string\",\"description\":\"The .rskel2d, absolute or relative to the project (opens its editor tab)\"},\"clip\":{\"type\":\"string\"},\"playing\":{\"type\":\"boolean\"},\"loop\":{\"type\":\"boolean\"},\"time\":{\"type\":\"number\"}", "[\"path\"]" },
	{ "skel2d_mode", "skel2d.mode", TOOL_PASS, FALSE, FALSE,
	  "Switch setup / animate mode, the tool (select, bone, mesh, weights) and the selection. Returns {mode, tool, selected}.",
	  "\"path\":{\"type\":\"string\",\"description\":\"The .rskel2d, absolute or relative to the project (opens its editor tab)\"},\"mode\":{\"type\":\"string\",\"enum\":[\"setup\",\"animate\"]},\"tool\":{\"type\":\"string\",\"enum\":[\"select\",\"bone\",\"mesh\",\"weights\"]},\"bone\":{\"type\":\"string\"},\"slot\":{\"type\":\"string\"}", "[\"path\"]" },
	{ "skel2d_pick", "skel2d.pick", TOOL_PASS, TRUE, FALSE,
	  "The bone at a skeleton-space point (radius). Returns {bone or null}.",
	  "\"path\":{\"type\":\"string\",\"description\":\"The .rskel2d, absolute or relative to the project (opens its editor tab)\"},\"x\":{\"type\":\"number\"},\"y\":{\"type\":\"number\"},\"radius\":{\"type\":\"number\"}", "[\"path\",\"x\",\"y\"]" },
	{ "skel2d_save", "skel2d.save", TOOL_PASS, FALSE, FALSE,
	  "Write a 2D skeleton to its file. Returns {path}.",
	  "\"path\":{\"type\":\"string\",\"description\":\"The .rskel2d, absolute or relative to the project (opens its editor tab)\"}", "[\"path\"]" },
	{ "anim_open", "anim.open", TOOL_PASS, FALSE, FALSE,
	  "Open a .ranim animation graph in its editor. Returns {path, panel}.",
	  "\"path\":{\"type\":\"string\",\"description\":\"The .ranim, absolute or relative to the project (opens its editor tab)\"}", "[\"path\"]" },
	{ "anim_create", "anim.create", TOOL_PASS, FALSE, FALSE,
	  "Create a .ranim (optionally previewing a model). Returns {path}.",
	  "\"path\":{\"type\":\"string\",\"description\":\"The .ranim, absolute or relative to the project (opens its editor tab)\"},\"model\":{\"type\":\"string\",\"description\":\"Preview .glb / .gltf\"}", "[\"path\"]" },
	{ "anim_get", "anim.get", TOOL_PASS, TRUE, FALSE,
	  "An animation graph: name, machine, breadcrumbs, layers [{name, weight, blend, enabled}], states [{name, kind, clip, default, x, y, active}], transitions [{from, to, duration, exit_time, offset, priority, interruption, conditions}], parameters [{name, type, default, value}], clips [{name, source, animation, missing}], preview {model, playing, time, states}.",
	  "\"path\":{\"type\":\"string\",\"description\":\"The .ranim, absolute or relative to the project (opens its editor tab)\"},\"machine\":{\"type\":\"string\",\"description\":\"YAML path of the state machine, e.g. /layers/0 or /layers/0/states/2/machine (default the one on the canvas)\"}", "[\"path\"]" },
	{ "anim_enter", "anim.enter", TOOL_PASS, FALSE, FALSE,
	  "Show another machine on the canvas: a layer, a state's sub-machine, or up. Returns {machine, breadcrumbs}.",
	  "\"path\":{\"type\":\"string\",\"description\":\"The .ranim, absolute or relative to the project (opens its editor tab)\"},\"layer\":{\"description\":\"Layer name or index\"},\"state\":{\"type\":\"string\"},\"up\":{\"type\":\"boolean\"}", "[\"path\"]" },
	{ "anim_add_layer", "anim.add_layer", TOOL_PASS, FALSE, FALSE,
	  "Add a layer.",
	  "\"path\":{\"type\":\"string\",\"description\":\"The .ranim, absolute or relative to the project (opens its editor tab)\"},\"name\":{\"type\":\"string\"}", "[\"path\",\"name\"]" },
	{ "anim_remove_layer", "anim.remove_layer", TOOL_PASS, FALSE, TRUE,
	  "Remove a layer.",
	  "\"path\":{\"type\":\"string\",\"description\":\"The .ranim, absolute or relative to the project (opens its editor tab)\"},\"name\":{\"type\":\"string\"}", "[\"path\",\"name\"]" },
	{ "anim_add_state", "anim.add_state", TOOL_PASS, FALSE, FALSE,
	  "Add a state (kind clip, blend-tree or machine) at x, y.",
	  "\"path\":{\"type\":\"string\",\"description\":\"The .ranim, absolute or relative to the project (opens its editor tab)\"},\"machine\":{\"type\":\"string\",\"description\":\"YAML path of the state machine, e.g. /layers/0 or /layers/0/states/2/machine (default the one on the canvas)\"},\"name\":{\"type\":\"string\"},\"kind\":{\"type\":\"string\"},\"clip\":{\"type\":\"string\"},\"x\":{\"type\":\"number\"},\"y\":{\"type\":\"number\"}", "[\"path\",\"name\"]" },
	{ "anim_remove_state", "anim.remove_state", TOOL_PASS, FALSE, TRUE,
	  "Remove a state and its transitions.",
	  "\"path\":{\"type\":\"string\",\"description\":\"The .ranim, absolute or relative to the project (opens its editor tab)\"},\"machine\":{\"type\":\"string\",\"description\":\"YAML path of the state machine, e.g. /layers/0 or /layers/0/states/2/machine (default the one on the canvas)\"},\"name\":{\"type\":\"string\"}", "[\"path\",\"name\"]" },
	{ "anim_set_state", "anim.set_state", TOOL_PASS, FALSE, FALSE,
	  "Change a state: rename, clip, speed, speed_parameter, mirror, default, x, y.",
	  "\"path\":{\"type\":\"string\",\"description\":\"The .ranim, absolute or relative to the project (opens its editor tab)\"},\"machine\":{\"type\":\"string\",\"description\":\"YAML path of the state machine, e.g. /layers/0 or /layers/0/states/2/machine (default the one on the canvas)\"},\"name\":{\"type\":\"string\"},\"rename\":{\"type\":\"string\"},\"clip\":{\"type\":\"string\"},\"speed\":{\"type\":\"number\"},\"speed_parameter\":{\"type\":\"string\"},\"mirror\":{\"type\":\"boolean\"},\"default\":{\"type\":\"boolean\"},\"x\":{\"type\":\"number\"},\"y\":{\"type\":\"number\"}", "[\"path\",\"name\"]" },
	{ "anim_add_transition", "anim.add_transition", TOOL_PASS, FALSE, FALSE,
	  "Add a transition (from \"*\" = any state): duration, exit_time, offset, priority, interruption, conditions [{parameter, op, value}].",
	  "\"path\":{\"type\":\"string\",\"description\":\"The .ranim, absolute or relative to the project (opens its editor tab)\"},\"machine\":{\"type\":\"string\",\"description\":\"YAML path of the state machine, e.g. /layers/0 or /layers/0/states/2/machine (default the one on the canvas)\"},\"from\":{\"type\":\"string\"},\"to\":{\"type\":\"string\"},\"duration\":{\"type\":\"number\"},\"exit_time\":{\"type\":\"number\"},\"offset\":{\"type\":\"number\"},\"priority\":{\"type\":\"integer\"},\"interruption\":{\"type\":\"string\"},\"conditions\":{\"type\":\"array\"}", "[\"path\",\"from\",\"to\"]" },
	{ "anim_set_transition", "anim.set_transition", TOOL_PASS, FALSE, FALSE,
	  "Change a transition (exit_time null clears it; conditions replaces the list).",
	  "\"path\":{\"type\":\"string\",\"description\":\"The .ranim, absolute or relative to the project (opens its editor tab)\"},\"machine\":{\"type\":\"string\",\"description\":\"YAML path of the state machine, e.g. /layers/0 or /layers/0/states/2/machine (default the one on the canvas)\"},\"from\":{\"type\":\"string\"},\"to\":{\"type\":\"string\"},\"duration\":{\"type\":\"number\"},\"exit_time\":{},\"offset\":{\"type\":\"number\"},\"priority\":{\"type\":\"integer\"},\"interruption\":{\"type\":\"string\"},\"conditions\":{\"type\":\"array\"}", "[\"path\",\"from\",\"to\"]" },
	{ "anim_remove_transition", "anim.remove_transition", TOOL_PASS, FALSE, TRUE,
	  "Remove a transition.",
	  "\"path\":{\"type\":\"string\",\"description\":\"The .ranim, absolute or relative to the project (opens its editor tab)\"},\"machine\":{\"type\":\"string\",\"description\":\"YAML path of the state machine, e.g. /layers/0 or /layers/0/states/2/machine (default the one on the canvas)\"},\"from\":{\"type\":\"string\"},\"to\":{\"type\":\"string\"}", "[\"path\",\"from\",\"to\"]" },
	{ "anim_add_parameter", "anim.add_parameter", TOOL_PASS, FALSE, FALSE,
	  "Add a parameter: type float, int, bool or trigger, with a default.",
	  "\"path\":{\"type\":\"string\",\"description\":\"The .ranim, absolute or relative to the project (opens its editor tab)\"},\"name\":{\"type\":\"string\"},\"type\":{\"type\":\"string\",\"enum\":[\"float\",\"int\",\"bool\",\"trigger\"]},\"default\":{}", "[\"path\",\"name\",\"type\"]" },
	{ "anim_remove_parameter", "anim.remove_parameter", TOOL_PASS, FALSE, TRUE,
	  "Remove a parameter.",
	  "\"path\":{\"type\":\"string\",\"description\":\"The .ranim, absolute or relative to the project (opens its editor tab)\"},\"name\":{\"type\":\"string\"}", "[\"path\",\"name\"]" },
	{ "anim_set_parameter", "anim.set_parameter", TOOL_PASS, FALSE, FALSE,
	  "Set a parameter's live value in the preview (not saved).",
	  "\"path\":{\"type\":\"string\",\"description\":\"The .ranim, absolute or relative to the project (opens its editor tab)\"},\"name\":{\"type\":\"string\"},\"value\":{}", "[\"path\",\"name\",\"value\"]" },
	{ "anim_set_blend_tree", "anim.set_blend_tree", TOOL_PASS, FALSE, FALSE,
	  "Configure a blend-tree state: type, parameter, parameter_y, sync, children [{clip, threshold or position or parameter or weight, speed}].",
	  "\"path\":{\"type\":\"string\",\"description\":\"The .ranim, absolute or relative to the project (opens its editor tab)\"},\"machine\":{\"type\":\"string\",\"description\":\"YAML path of the state machine, e.g. /layers/0 or /layers/0/states/2/machine (default the one on the canvas)\"},\"state\":{\"type\":\"string\"},\"type\":{\"type\":\"string\"},\"parameter\":{\"type\":\"string\"},\"parameter_y\":{\"type\":\"string\"},\"sync\":{\"type\":\"boolean\"},\"children\":{\"type\":\"array\"}", "[\"path\",\"state\"]" },
	{ "anim_preview", "anim.preview", TOOL_PASS, FALSE, FALSE,
	  "Preview the graph on a model: model, restart, play, advance (seconds stepped now, also while paused), scrub (0..1 through the base layer's state; pauses). Returns {model, playing, time, states, transition}.",
	  "\"path\":{\"type\":\"string\",\"description\":\"The .ranim, absolute or relative to the project (opens its editor tab)\"},\"model\":{\"type\":\"string\"},\"restart\":{\"type\":\"boolean\"},\"play\":{\"type\":\"boolean\"},\"advance\":{\"type\":\"number\"},\"scrub\":{\"type\":\"number\"}", "[\"path\"]" },
	{ "anim_validate", "anim.validate", TOOL_PASS, TRUE, FALSE,
	  "Validate a graph. Returns {ok, issues [{severity, path, line, message}]}.",
	  "\"path\":{\"type\":\"string\",\"description\":\"The .ranim, absolute or relative to the project (opens its editor tab)\"}", "[\"path\"]" },
	{ "anim_save", "anim.save", TOOL_PASS, FALSE, FALSE,
	  "Write a graph to its file.",
	  "\"path\":{\"type\":\"string\",\"description\":\"The .ranim, absolute or relative to the project (opens its editor tab)\"}", "[\"path\"]" },
	{ "bonemap_open", "bonemap.open", TOOL_PASS, FALSE, FALSE,
	  "Open a .rbonemap in the bone map editor.",
	  "\"path\":{\"type\":\"string\",\"description\":\"The .rbonemap, absolute or relative to the project (opens its editor tab)\"}", "[\"path\"]" },
	{ "bonemap_create", "bonemap.create", TOOL_PASS, FALSE, FALSE,
	  "Create a .rbonemap from a source and target model (auto_fill pairs bones by the name rules). Returns {path, added}.",
	  "\"path\":{\"type\":\"string\",\"description\":\"The .rbonemap, absolute or relative to the project (opens its editor tab)\"},\"source\":{\"type\":\"string\",\"description\":\"Source model\"},\"target\":{\"type\":\"string\",\"description\":\"Target model\"},\"auto_fill\":{\"type\":\"boolean\"}", "[\"path\"]" },
	{ "bonemap_get", "bonemap.get", TOOL_PASS, TRUE, FALSE,
	  "A bone map: rules, pairs, source_model, target_model, mapping, unmapped_source, unmapped_target.",
	  "\"path\":{\"type\":\"string\",\"description\":\"The .rbonemap, absolute or relative to the project (opens its editor tab)\"}", "[\"path\"]" },
	{ "bonemap_set", "bonemap.set", TOOL_PASS, FALSE, FALSE,
	  "Map a source bone to a target bone (null removes the pair).",
	  "\"path\":{\"type\":\"string\",\"description\":\"The .rbonemap, absolute or relative to the project (opens its editor tab)\"},\"source\":{\"type\":\"string\"},\"target\":{\"description\":\"Bone name or null\"}", "[\"path\",\"source\",\"target\"]" },
	{ "bonemap_auto_fill", "bonemap.auto_fill", TOOL_PASS, FALSE, FALSE,
	  "Pair the unmapped bones by the name rules. Returns {added}.",
	  "\"path\":{\"type\":\"string\",\"description\":\"The .rbonemap, absolute or relative to the project (opens its editor tab)\"}", "[\"path\"]" },
	{ "bonemap_set_rules", "bonemap.set_rules", TOOL_PASS, FALSE, FALSE,
	  "Name rules: ignore_case, normalize_separators, normalize_sides, strip_prefixes.",
	  "\"path\":{\"type\":\"string\",\"description\":\"The .rbonemap, absolute or relative to the project (opens its editor tab)\"},\"ignore_case\":{\"type\":\"boolean\"},\"normalize_separators\":{\"type\":\"boolean\"},\"normalize_sides\":{\"type\":\"boolean\"},\"strip_prefixes\":{\"type\":\"array\",\"items\":{\"type\":\"string\"}}", "[\"path\"]" },
	{ "bonemap_set_models", "bonemap.set_models", TOOL_PASS, FALSE, FALSE,
	  "The preview's source and target models and clip.",
	  "\"path\":{\"type\":\"string\",\"description\":\"The .rbonemap, absolute or relative to the project (opens its editor tab)\"},\"source\":{\"type\":\"string\"},\"target\":{\"type\":\"string\"},\"clip\":{\"type\":\"string\"}", "[\"path\"]" },
	{ "bonemap_preview", "bonemap.preview", TOOL_PASS, FALSE, FALSE,
	  "The live retarget preview: clip, time, play.",
	  "\"path\":{\"type\":\"string\",\"description\":\"The .rbonemap, absolute or relative to the project (opens its editor tab)\"},\"clip\":{\"type\":\"string\"},\"time\":{\"type\":\"number\"},\"play\":{\"type\":\"boolean\"}", "[\"path\"]" },
	{ "bonemap_save", "bonemap.save", TOOL_PASS, FALSE, FALSE,
	  "Write a bone map to its file.",
	  "\"path\":{\"type\":\"string\",\"description\":\"The .rbonemap, absolute or relative to the project (opens its editor tab)\"}", "[\"path\"]" },
	{ "clip_list", "clip.list", TOOL_PASS, TRUE, FALSE,
	  "A glTF model's clips [{name, duration, tracks, loop}], bones and morph_targets [{name, group, weight}].",
	  "\"model\":{\"type\":\"string\",\"description\":\"A .glb / .gltf model, absolute or relative to the project\"}", "[\"model\"]" },
	{ "clip_info", "clip.info", TOOL_PASS, TRUE, FALSE,
	  "One clip of a model: {name, duration, loop, tracks, events}.",
	  "\"model\":{\"type\":\"string\",\"description\":\"A .glb / .gltf model, absolute or relative to the project\"},\"clip\":{\"type\":\"string\"}", "[\"model\",\"clip\"]" },
	{ "clip_preview", "clip.preview", TOOL_PASS, FALSE, FALSE,
	  "Preview a clip on the model's turntable thumbnail: clip, time, play.",
	  "\"model\":{\"type\":\"string\",\"description\":\"A .glb / .gltf model, absolute or relative to the project\"},\"clip\":{\"type\":\"string\"},\"time\":{\"type\":\"number\"},\"play\":{\"type\":\"boolean\"}", "[\"model\"]" },
	{ "clip_set_morph", "clip.set_morph", TOOL_PASS, FALSE, FALSE,
	  "Set a morph target weight on the preview.",
	  "\"model\":{\"type\":\"string\",\"description\":\"A .glb / .gltf model, absolute or relative to the project\"},\"target\":{\"type\":\"string\"},\"weight\":{\"type\":\"number\"}", "[\"model\",\"target\",\"weight\"]" },
	/* 0.9.0 M5 physics: node physics blocks (physics.*), project collision
	 * layers and world settings (physics.layers.*), ragdoll assets
	 * (physasset.*) */
	{ "physics_get", "physics.get", TOOL_PASS, TRUE, FALSE,
	  "A node's physics: block: {node, name, body: null or {body-kind, mass, layer, friction, restitution, linear-damping, angular-damping, gravity-factor, ccd}, shapes [{index, kind, half-extents, radius, half-height, offset, rotation, trigger, friction, restitution, layer, mesh}], joints [{index, kind, target, target-name, anchor, axis, limits-enabled, limit-min, limit-max, spring-frequency, spring-damping, motor-velocity, motor-max-force, break-force}], layers (the project layer names)}. node defaults to the primary selection.",
	  NODE_ARG, NULL },
	{ "physics_add_body", "physics.add_body", TOOL_PASS, FALSE, FALSE,
	  "Give a node a rigid body (one undo step): kind dynamic (default), static or kinematic; shape auto (fit to the mesh, default), a shape kind, or none. Returns the same as physics_get.",
	  NODE_ARG ",\"kind\":{\"type\":\"string\",\"enum\":[\"dynamic\",\"static\",\"kinematic\"]},\"shape\":{\"type\":\"string\",\"description\":\"auto, none, box, sphere, capsule, cylinder, convex, mesh\"}", NULL },
	{ "physics_remove_body", "physics.remove_body", TOOL_PASS, FALSE, TRUE,
	  "Remove a node's whole physics: block (body, shapes, joints; undoable). Returns the same as physics_get.",
	  NODE_ARG, NULL },
	{ "physics_set_body", "physics.set_body", TOOL_PASS, FALSE, FALSE,
	  "Set body fields in one undo step, e.g. values {body-kind: dynamic, mass: 20, layer: debris, friction: 0.6, ccd: true}. Returns the same as physics_get.",
	  NODE_ARG ",\"values\":{\"type\":\"object\",\"description\":\"field: value, names as in physics_get (kebab-case or snake_case); vectors [x,y,z]; enums by nick\"}", "[\"values\"]" },
	{ "physics_add_shape", "physics.add_shape", TOOL_PASS, FALSE, FALSE,
	  "Add a collision shape (kind box default, sphere, capsule, cylinder, convex, mesh) with optional values {half-extents, radius, half-height, offset, rotation, trigger, layer}; fit true sizes it to the node's mesh. Returns physics_get plus index.",
	  NODE_ARG ",\"kind\":{\"type\":\"string\"},\"fit\":{\"type\":\"boolean\"},\"values\":{\"type\":\"object\",\"description\":\"field: value, names as in physics_get (kebab-case or snake_case); vectors [x,y,z]; enums by nick\"}", NULL },
	{ "physics_remove_shape", "physics.remove_shape", TOOL_PASS, FALSE, TRUE,
	  "Remove the shape at index (undoable). Returns the same as physics_get.",
	  NODE_ARG ",\"index\":{\"type\":\"integer\"}", "[\"index\"]" },
	{ "physics_set_shape", "physics.set_shape", TOOL_PASS, FALSE, FALSE,
	  "Set fields of the shape at index in one undo step, e.g. values {kind: capsule, radius: 0.3, half-height: 0.6, offset: [0,0.9,0], trigger: true}. Returns the same as physics_get.",
	  NODE_ARG ",\"index\":{\"type\":\"integer\"},\"values\":{\"type\":\"object\",\"description\":\"field: value, names as in physics_get (kebab-case or snake_case); vectors [x,y,z]; enums by nick\"}", "[\"index\",\"values\"]" },
	{ "physics_move_shape", "physics.move_shape", TOOL_PASS, FALSE, FALSE,
	  "Reorder a shape: from index to position to. Returns the same as physics_get.",
	  NODE_ARG ",\"index\":{\"type\":\"integer\"},\"to\":{\"type\":\"integer\"}", "[\"index\",\"to\"]" },
	{ "physics_add_joint", "physics.add_joint", TOOL_PASS, FALSE, FALSE,
	  "Add a joint (kind fixed default, point, hinge, slider, distance, cone, swing-twist, six-dof) to target (a node GUID or path; omit for the world) with optional values {anchor, axis, limits-enabled, limit-min, limit-max, spring-frequency, spring-damping, motor-velocity, motor-max-force, break-force}. Returns physics_get plus index.",
	  NODE_ARG ",\"kind\":{\"type\":\"string\"},\"target\":{\"type\":\"string\"},\"values\":{\"type\":\"object\",\"description\":\"field: value, names as in physics_get (kebab-case or snake_case); vectors [x,y,z]; enums by nick\"}", NULL },
	{ "physics_remove_joint", "physics.remove_joint", TOOL_PASS, FALSE, TRUE,
	  "Remove the joint at index (undoable). Returns the same as physics_get.",
	  NODE_ARG ",\"index\":{\"type\":\"integer\"}", "[\"index\"]" },
	{ "physics_set_joint", "physics.set_joint", TOOL_PASS, FALSE, FALSE,
	  "Set fields of the joint at index in one undo step; values.target is a node GUID or path, \"\" for the world. Returns the same as physics_get.",
	  NODE_ARG ",\"index\":{\"type\":\"integer\"},\"values\":{\"type\":\"object\",\"description\":\"field: value, names as in physics_get (kebab-case or snake_case); vectors [x,y,z]; enums by nick\"}", "[\"index\",\"values\"]" },
	{ "physics_fit", "physics.fit", TOOL_PASS, FALSE, FALSE,
	  "Fit the shape at index (default 0) to the node's model bounds or points, optionally changing its kind (box, sphere, capsule, cylinder, convex). One undo step. Returns the same as physics_get.",
	  NODE_ARG ",\"index\":{\"type\":\"integer\"},\"kind\":{\"type\":\"string\"}", NULL },
	{ "physics_overlay", "physics.overlay", TOOL_PASS, FALSE, FALSE,
	  "A viewport's physics debug overlay: flags (the full set), enable or disable (names: shapes, aabbs, contacts, joints, sleeping, com, velocity, broadphase, all, none). Without arguments returns {flags}.",
	  "\"viewport\":{\"type\":\"integer\"},\"flags\":{\"type\":\"array\",\"items\":{\"type\":\"string\"}},\"enable\":{\"type\":\"array\",\"items\":{\"type\":\"string\"}},\"disable\":{\"type\":\"array\",\"items\":{\"type\":\"string\"}}", NULL },
	{ "physics_state", "physics.state", TOOL_PASS, TRUE, FALSE,
	  "Live body state while playing: {playing, has_body, body: null or {body-id, position, velocity, angular-velocity, sleeping, mass, contacts}}.",
	  NODE_ARG, NULL },
	{ "physics_show", "physics.show", TOOL_PASS, FALSE, FALSE,
	  "Bring the Physics panel (the selected node's body, shapes and joints) to the front. Returns {panel, shown}.",
	  "", NULL },
	{ "physics_layers_get", "physics.layers.get", TOOL_PASS, TRUE, FALSE,
	  "The project's collision layers: {layers (32 slot names or null), count, matrix (32 row masks), no_collide (pairs of layers that never collide), settings {gravity, fixed_dt, max_substeps, iterations, sleep_threshold, sleep_substeps}}.",
	  "", NULL },
	{ "physics_layers_set_name", "physics.layers.set_name", TOOL_PASS, FALSE, FALSE,
	  "Name collision layer slot index (0-31); null or \"\" unnames it. Names are unique. Saves project.ryaml. Returns the same as physics_layers_get.",
	  "\"index\":{\"type\":\"integer\"},\"name\":{\"type\":[\"string\",\"null\"]}", "[\"index\",\"name\"]" },
	{ "physics_layers_set_collides", "physics.layers.set_collides", TOOL_PASS, FALSE, FALSE,
	  "Set whether layers a and b collide (names or indices; the matrix stays symmetric). Saves project.ryaml. Returns the same as physics_layers_get.",
	  "\"a\":{\"description\":\"Layer name or index\"},\"b\":{\"description\":\"Layer name or index\"},\"collides\":{\"type\":\"boolean\"}", "[\"a\",\"b\",\"collides\"]" },
	{ "physics_layers_settings", "physics.layers.settings", TOOL_PASS, FALSE, FALSE,
	  "Read or write the world settings of project.ryaml's physics: block: gravity [x,y,z], fixed_dt (s, <= 0.1), max_substeps (1-8), iterations (1-32), sleep_threshold, sleep_substeps; reset true restores the defaults. Returns the settings.",
	  "\"gravity\":" VEC3 ("m/s^2") ",\"fixed_dt\":{\"type\":\"number\"},\"max_substeps\":{\"type\":\"integer\"},\"iterations\":{\"type\":\"integer\"},\"sleep_threshold\":{\"type\":\"number\"},\"sleep_substeps\":{\"type\":\"integer\"},\"reset\":{\"type\":\"boolean\"}", NULL },
	{ "physics_layers_reset", "physics.layers.reset", TOOL_PASS, FALSE, TRUE,
	  "Replace the layer table with the default layers (or an empty table with empty true). Saves project.ryaml. Returns the same as physics_layers_get.",
	  "\"empty\":{\"type\":\"boolean\"}", NULL },
	{ "physics_layers_open", "physics.layers.open", TOOL_PASS, FALSE, FALSE,
	  "Open Project Settings on its Physics page (layer names, collide matrix, world settings) in the editor UI.",
	  "", NULL },
	{ "physasset_open", "physasset.open", TOOL_PASS, FALSE, FALSE,
	  "Open a .rphysasset in the physics asset (ragdoll) editor. Returns {path, panel}.",
	  "\"path\":{\"type\":\"string\",\"description\":\"The .rphysasset, absolute or relative to the project (opens its editor tab)\"}", "[\"path\"]" },
	{ "physasset_create", "physasset.create", TOOL_PASS, FALSE, FALSE,
	  "Create a .rphysasset (fails if it exists): empty, or with model (a skinned .glb) one part per bone generated from its skeleton (total_mass kg, default 70; radius_ratio of the bone length, default 0.2) and the model remembered as the preview. Returns {path, parts}.",
	  "\"path\":{\"type\":\"string\",\"description\":\"The .rphysasset, absolute or relative to the project (opens its editor tab)\"},\"model\":{\"type\":\"string\"},\"total_mass\":{\"type\":\"number\"},\"radius_ratio\":{\"type\":\"number\"}", "[\"path\"]" },
	{ "physasset_get", "physasset.get", TOOL_PASS, TRUE, FALSE,
	  "A physics asset: {path, name, exclude_adjacent, total_mass, model, parts [{index, bone, shape, radius, half_height, half_extents, offset, rotation, mass, parent, joint {kind, position, rotation, twist [min,max], swing [y,z], friction, motor_max_torque, motor_frequency, motor_damping}}], exclusions (bone pairs), bones, free_bones (bones without a part), selected, simulating, load_error}. Angles are radians.",
	  "\"path\":{\"type\":\"string\",\"description\":\"The .rphysasset, absolute or relative to the project (opens its editor tab)\"}", "[\"path\"]" },
	{ "physasset_set_model", "physasset.set_model", TOOL_PASS, FALSE, FALSE,
	  "Pick the preview model (a skinned .glb / .gltf) whose skeleton the parts sit on; kept in the asset's .meta sidecar. Returns the same as physasset_get.",
	  "\"path\":{\"type\":\"string\",\"description\":\"The .rphysasset, absolute or relative to the project (opens its editor tab)\"},\"model\":{\"type\":\"string\"}", "[\"path\",\"model\"]" },
	{ "physasset_add_part", "physasset.add_part", TOOL_PASS, FALSE, FALSE,
	  "Add a part on a bone that has none (sized along the bone when a preview model is set), with optional values. One undo step. Returns physasset_get plus index.",
	  "\"path\":{\"type\":\"string\",\"description\":\"The .rphysasset, absolute or relative to the project (opens its editor tab)\"},\"bone\":{\"type\":\"string\"},\"values\":{\"type\":\"object\",\"description\":\"shape (capsule, box, sphere), radius, half_height, half_extents [x,y,z], offset [x,y,z], rotation [x,y,z,w] or rotation_degrees [x,y,z], mass, joint (swing-twist, cone, hinge, point, fixed), joint_position, joint_rotation or joint_rotation_degrees, twist [min,max] or twist_degrees, swing [y,z] or swing_degrees, friction, motor_max_torque, motor_frequency, motor_damping, bone\"}", "[\"path\",\"bone\"]" },
	{ "physasset_remove_part", "physasset.remove_part", TOOL_PASS, FALSE, TRUE,
	  "Remove a part (index or bone name) and the exclusions naming its bone. One undo step. Returns the same as physasset_get.",
	  "\"path\":{\"type\":\"string\",\"description\":\"The .rphysasset, absolute or relative to the project (opens its editor tab)\"},\"part\":{\"description\":\"Part index or bone name\"}", "[\"path\",\"part\"]" },
	{ "physasset_set_part", "physasset.set_part", TOOL_PASS, FALSE, FALSE,
	  "Change a part (index or bone name) in one undo step; invalid values (non-positive mass or size, twist min > max) are refused. Returns the part.",
	  "\"path\":{\"type\":\"string\",\"description\":\"The .rphysasset, absolute or relative to the project (opens its editor tab)\"},\"part\":{\"description\":\"Part index or bone name\"},\"values\":{\"type\":\"object\",\"description\":\"shape (capsule, box, sphere), radius, half_height, half_extents [x,y,z], offset [x,y,z], rotation [x,y,z,w] or rotation_degrees [x,y,z], mass, joint (swing-twist, cone, hinge, point, fixed), joint_position, joint_rotation or joint_rotation_degrees, twist [min,max] or twist_degrees, swing [y,z] or swing_degrees, friction, motor_max_torque, motor_frequency, motor_damping, bone\"}", "[\"path\",\"part\",\"values\"]" },
	{ "physasset_generate", "physasset.generate", TOOL_PASS, FALSE, TRUE,
	  "Replace every part with ones generated from the preview model's skeleton (total_mass kg, default 70; radius_ratio, default 0.2). One undo step. Returns the same as physasset_get.",
	  "\"path\":{\"type\":\"string\",\"description\":\"The .rphysasset, absolute or relative to the project (opens its editor tab)\"},\"total_mass\":{\"type\":\"number\"},\"radius_ratio\":{\"type\":\"number\"}", "[\"path\"]" },
	{ "physasset_stabilize", "physasset.stabilize", TOOL_PASS, FALSE, FALSE,
	  "Even out masses along each chain (a child weighs 0.8-1.2x its parent, total kept). One undo step. Returns the same as physasset_get.",
	  "\"path\":{\"type\":\"string\",\"description\":\"The .rphysasset, absolute or relative to the project (opens its editor tab)\"}", "[\"path\"]" },
	{ "physasset_exclude", "physasset.exclude", TOOL_PASS, FALSE, FALSE,
	  "Stop two parts (bone names) colliding, or with remove true let them collide again. One undo step. Returns the same as physasset_get.",
	  "\"path\":{\"type\":\"string\",\"description\":\"The .rphysasset, absolute or relative to the project (opens its editor tab)\"},\"a\":{\"type\":\"string\"},\"b\":{\"type\":\"string\"},\"remove\":{\"type\":\"boolean\"}", "[\"path\",\"a\",\"b\"]" },
	{ "physasset_set", "physasset.set", TOOL_PASS, FALSE, FALSE,
	  "Set the asset's name and exclude_adjacent (jointed parts never collide). One undo step. Returns the same as physasset_get.",
	  "\"path\":{\"type\":\"string\",\"description\":\"The .rphysasset, absolute or relative to the project (opens its editor tab)\"},\"name\":{\"type\":\"string\"},\"exclude_adjacent\":{\"type\":\"boolean\"}", "[\"path\"]" },
	{ "physasset_select", "physasset.select", TOOL_PASS, FALSE, FALSE,
	  "Select a part (index or bone name) in the editor: the grid shows it and the preview draws its joint limits. Returns {selected, bone}.",
	  "\"path\":{\"type\":\"string\",\"description\":\"The .rphysasset, absolute or relative to the project (opens its editor tab)\"},\"part\":{\"description\":\"Part index or bone name\"}", "[\"path\",\"part\"]" },
	{ "physasset_simulate", "physasset.simulate", TOOL_PASS, FALSE, FALSE,
	  "Drop the preview model as a passive ragdoll (project gravity) or stop: run (default true), reset (back to the bind pose first), advance (seconds stepped now). Returns {simulating, time, parts [{bone, position}]}.",
	  "\"path\":{\"type\":\"string\",\"description\":\"The .rphysasset, absolute or relative to the project (opens its editor tab)\"},\"run\":{\"type\":\"boolean\"},\"reset\":{\"type\":\"boolean\"},\"advance\":{\"type\":\"number\"}", "[\"path\"]" },
	{ "physasset_save", "physasset.save", TOOL_PASS, FALSE, FALSE,
	  "Write a physics asset to its file. Returns {path}.",
	  "\"path\":{\"type\":\"string\",\"description\":\"The .rphysasset, absolute or relative to the project (opens its editor tab)\"}", "[\"path\"]" },
	/* 0.8.3: asset sources (virtual asset trees registered by game modules) */
	{ "source_list", "source.list", TOOL_PASS, TRUE, FALSE,
	  "The asset sources registered in the editor (by game modules): [{id, title, search}]. Sources hold assets that are not project files (e.g. models in game archives).",
	  "", NULL },
	{ "source_ls", "source.ls", TOOL_PASS, TRUE, FALSE,
	  "List one virtual folder of an asset source (\"\" or omitted = root): {entries: [{path, name, folder, type, size, description}]}; type is what the entry exports to (model, texture...).",
	  "\"source\":{\"type\":\"string\"},\"folder\":{\"type\":\"string\"}", "[\"source\"]" },
	{ "source_search", "source.search", TOOL_PASS, TRUE, FALSE,
	  "Find entries anywhere in an asset source by a case-insensitive name pattern (limit defaults to 200).",
	  "\"source\":{\"type\":\"string\"},\"pattern\":{\"type\":\"string\"},\"limit\":{\"type\":\"integer\"}",
	  "[\"source\",\"pattern\"]" },
	{ "source_export", "source.export", TOOL_PASS, FALSE, FALSE,
	  "Convert an asset source entry into real files (cached; the first export can take seconds) and return the main file; with dest also copy the files into that folder.",
	  "\"source\":{\"type\":\"string\"},\"path\":{\"type\":\"string\"},\"dest\":{\"type\":\"string\"}",
	  "[\"source\",\"path\"]" },
	{ "source_import", "source.import", TOOL_PASS, FALSE, FALSE,
	  "Import an asset source entry into the open project (assets/<source>/<folder>/); returns the project-relative asset path to use in nodes.",
	  "\"source\":{\"type\":\"string\"},\"path\":{\"type\":\"string\"}", "[\"source\",\"path\"]" },
	{ "source_place", "source.place", TOOL_PASS, FALSE, FALSE,
	  "Import an asset source entry and place it in the level at the camera target (model: mesh node, texture: sprite), selected and undoable; returns {asset, node, name}.",
	  "\"source\":{\"type\":\"string\"},\"path\":{\"type\":\"string\"}", "[\"source\",\"path\"]" },
	{ "call_method", NULL, TOOL_RAW, FALSE, TRUE,
	  "Escape hatch: call any editor IPC method (see docs/editor/ipc-protocol.org or the methods list) with raw params.",
	  "\"method\":{\"type\":\"string\"},\"params\":{\"type\":\"object\"}", "[\"method\"]" },
};

static const gchar *instructions =
	"Tools for the libregnum level/scene/world editor. Start with editor_info and get_tree. "
	"Nodes are addressed by GUID or by slash path from the top of the level (\"House/Door\"). "
	"Every edit is undoable (undo/redo) and marks the level dirty; call save_level to write it. "
	"Transforms: location in metres, rotation_degrees as Euler XYZ degrees, scale per axis; primitives "
	"are 1 m unit shapes. Use validate_level after bulk edits, screenshot to look at the result (windowed "
	"editors only), play to test in the editor (stop restores the level), and run_action for any menu "
	"command (list_actions). Lighting: mark geometry static_lighting, set lights' bake_mode, add probe-volume "
	"nodes, then bake_start and poll bake_progress.";

typedef struct
{
	const ToolSpec *spec;
	LrgCtlCallFunc  call;
	gpointer        call_data;
} ToolContext;

guint
lrg_ctl_mcp_n_tools (void)
{
	return G_N_ELEMENTS (tools);
}

const gchar *
lrg_ctl_mcp_tool_name (guint index)
{
	return (index < G_N_ELEMENTS (tools)) ? tools[index].name : NULL;
}

const gchar *
lrg_ctl_mcp_tool_description (guint index)
{
	return (index < G_N_ELEMENTS (tools)) ? tools[index].description : NULL;
}

/* ==========================================================================
 * Results
 * ========================================================================== */

static McpToolResult *
error_result (const gchar *message)
{
	McpToolResult *r = mcp_tool_result_new (TRUE);

	mcp_tool_result_add_text (r, message);
	return r;
}

static McpToolResult *
json_result (JsonNode *node)
{
	g_autoptr(JsonGenerator) gen = json_generator_new ();
	g_autofree gchar        *text = NULL;
	McpToolResult           *r = mcp_tool_result_new (FALSE);

	/* level.serialize returns YAML text: hand it over as-is. */
	if (node != NULL && JSON_NODE_HOLDS_VALUE (node) && json_node_get_value_type (node) == G_TYPE_STRING)
	{
		mcp_tool_result_add_text (r, json_node_get_string (node));
		return r;
	}

	json_generator_set_root (gen, node);
	json_generator_set_pretty (gen, TRUE);
	text = json_generator_to_data (gen, NULL);
	mcp_tool_result_add_text (r, text);
	return r;
}

static JsonObject *
copy_object (JsonObject *src)
{
	JsonObject *out = json_object_new ();
	GList      *members, *l;

	if (src == NULL)
		return out;
	members = json_object_get_members (src);
	for (l = members; l != NULL; l = l->next)
		json_object_set_member (out, l->data, json_node_copy (json_object_get_member (src, l->data)));
	g_list_free (members);
	return out;
}

/* ==========================================================================
 * Special tools
 * ========================================================================== */

static McpToolResult *
tool_create (ToolContext *ctx,
             JsonObject  *args)
{
	static const gchar *transform[] = { "location", "rotation_degrees", "rotation", "scale" };
	g_autoptr(JsonObject) create = copy_object (args);
	g_autoptr(JsonObject) set = json_object_new ();
	g_autoptr(JsonNode)   created = NULL;
	g_autoptr(GError)     error = NULL;
	gsize                 i;

	/* node.create takes name/parent/kind/primitive; the transform follows. */
	for (i = 0; i < G_N_ELEMENTS (transform); i++)
		if (json_object_has_member (create, transform[i]))
		{
			json_object_set_member (set, transform[i], json_node_copy (json_object_get_member (create, transform[i])));
			json_object_remove_member (create, transform[i]);
		}

	created = ctx->call ("node.create", create, ctx->call_data, &error);
	if (created == NULL)
		return error_result (error->message);
	if (json_object_get_size (set) == 0 || !JSON_NODE_HOLDS_OBJECT (created))
		return json_result (created);

	json_object_set_string_member (set, "node", json_object_get_string_member (json_node_get_object (created), "guid"));
	json_object_set_boolean_member (set, "merge", TRUE);
	{
		g_autoptr(JsonNode) updated = ctx->call ("node.set", set, ctx->call_data, &error);

		if (updated == NULL)
			return error_result (error->message);
		return json_result (updated);
	}
}

/* screenshot is a deferred tool: when the MCP server runs inside the
 * editor, its handler runs on the editor's main loop, and a window
 * capture is only written after the next frame. Blocking here would
 * stop that frame from ever being drawn, so the reply is sent from a
 * timeout once the file exists. */
typedef struct
{
	McpToolCall *call;
	gchar       *dir;
	gchar       *path;
	gint         tries;
	goffset      last_size;
} ShotWait;

static void
shot_wait_free (ShotWait *w)
{
	g_clear_object (&w->call);
	if (w->path != NULL)
		g_unlink (w->path);
	if (w->dir != NULL)
		g_rmdir (w->dir);
	g_free (w->path);
	g_free (w->dir);
	g_free (w);
}

static void
shot_reply (ShotWait      *w,
            McpToolResult *r)
{
	if (!mcp_tool_call_is_completed (w->call) && !mcp_tool_call_is_cancelled (w->call))
		mcp_tool_call_return_result (w->call, r);
	mcp_tool_result_unref (r);
}

static gboolean
shot_poll (gpointer user_data)
{
	ShotWait          *w = user_data;
	GStatBuf           st;
	g_autofree gchar  *data = NULL;
	g_autofree gchar  *b64 = NULL;
	g_autoptr(GError)  error = NULL;
	gsize              len = 0;
	McpToolResult     *r;

	/* Wait for the file, then for its size to settle. */
	if (g_stat (w->path, &st) != 0 || st.st_size == 0 || st.st_size != w->last_size)
	{
		w->last_size = (g_stat (w->path, &st) == 0) ? st.st_size : -1;
		if (++w->tries < 80)
			return G_SOURCE_CONTINUE;
		shot_reply (w, error_result ("The screenshot was not written (is the editor windowed and drawing?)"));
		shot_wait_free (w);
		return G_SOURCE_REMOVE;
	}

	if (!g_file_get_contents (w->path, &data, &len, &error))
		r = error_result (error->message);
	else
	{
		b64 = g_base64_encode ((const guchar *) data, len);
		r = mcp_tool_result_new (FALSE);
		mcp_tool_result_add_image (r, b64, "image/png");
	}
	shot_reply (w, r);
	shot_wait_free (w);
	return G_SOURCE_REMOVE;
}

static void
handle_screenshot (McpServer   *server,
                   McpToolCall *call,
                   gpointer     user_data)
{
	ToolContext          *ctx = user_data;
	JsonObject           *args = mcp_tool_call_get_arguments (call);
	g_autoptr(JsonObject) params = json_object_new ();
	g_autoptr(JsonNode)   res = NULL;
	g_autoptr(GError)     error = NULL;
	ShotWait             *w = g_new0 (ShotWait, 1);

	w->call = g_object_ref (call);
	w->last_size = -1;
	w->dir = g_dir_make_tmp ("lrgeditor-shot-XXXXXX", NULL);
	if (w->dir == NULL)
	{
		shot_reply (w, error_result ("Cannot create a temporary directory"));
		shot_wait_free (w);
		return;
	}
	w->path = g_build_filename (w->dir, "shot.png", NULL);
	json_object_set_string_member (params, "path", w->path);
	json_object_set_boolean_member (params, "window",
	                                args != NULL && json_object_has_member (args, "window") &&
	                                json_object_get_boolean_member (args, "window"));

	res = ctx->call ("view.screenshot", params, ctx->call_data, &error);
	if (res == NULL)
	{
		shot_reply (w, error_result (error->message));
		shot_wait_free (w);
		return;
	}
	g_timeout_add (25, shot_poll, w);
}

static McpToolResult *
tool_sessions (void)
{
	g_autoptr(GPtrArray) sessions = lrg_ctl_list_sessions ();
	g_autoptr(JsonNode)  node = json_node_new (JSON_NODE_ARRAY);
	JsonArray           *arr = json_array_new ();
	guint                i;

	for (i = 0; i < sessions->len; i++)
	{
		LrgCtlSession *s = g_ptr_array_index (sessions, i);
		JsonObject    *o = json_object_new ();

		json_object_set_string_member (o, "socket", s->socket);
		json_object_set_int_member (o, "pid", s->pid);
		json_object_set_string_member (o, "project", s->project != NULL ? s->project : "");
		json_object_set_string_member (o, "level", s->level != NULL ? s->level : "");
		json_array_add_object_element (arr, o);
	}
	json_node_take_array (node, arr);
	return json_result (node);
}

/* ==========================================================================
 * Dispatch
 * ========================================================================== */

static McpToolResult *
handle_tool (McpServer   *server,
             const gchar *name,
             JsonObject  *arguments,
             gpointer     user_data)
{
	ToolContext          *ctx = user_data;
	const ToolSpec       *spec = ctx->spec;
	g_autoptr(GError)     error = NULL;
	g_autoptr(JsonNode)   result = NULL;
	g_autoptr(JsonObject) params = NULL;
	g_autofree gchar     *method = NULL;

	switch (spec->kind)
	{
	case TOOL_CREATE:
		return tool_create (ctx, arguments);
	case TOOL_SESSIONS:
		return tool_sessions ();
	case TOOL_PLAY:
		{
			const gchar *cmd = (arguments != NULL && json_object_has_member (arguments, "command"))
			                   ? json_object_get_string_member (arguments, "command") : "state";

			if (g_strcmp0 (cmd, "start") != 0 && g_strcmp0 (cmd, "stop") != 0 && g_strcmp0 (cmd, "pause") != 0 &&
			    g_strcmp0 (cmd, "step") != 0 && g_strcmp0 (cmd, "state") != 0)
				return error_result ("command must be start, pause, step, stop or state");
			method = g_strconcat ("play.", cmd, NULL);
			params = json_object_new ();
		}
		break;
	case TOOL_RUN:
		method = g_strdup ((arguments != NULL && json_object_has_member (arguments, "stop") &&
		                    json_object_get_boolean_member (arguments, "stop")) ? "run.stop" : "run.start");
		params = json_object_new ();
		break;
	case TOOL_RAW:
		if (arguments == NULL || !json_object_has_member (arguments, "method"))
			return error_result ("method is required");
		method = g_strdup (json_object_get_string_member (arguments, "method"));
		params = (json_object_has_member (arguments, "params") &&
		          JSON_NODE_HOLDS_OBJECT (json_object_get_member (arguments, "params")))
		         ? copy_object (json_object_get_object_member (arguments, "params")) : json_object_new ();
		break;
	default:
		method = g_strdup (spec->method);
		params = copy_object (arguments);
		break;
	}

	result = ctx->call (method, params, ctx->call_data, &error);
	if (result == NULL)
		return error_result (error != NULL ? error->message : "the call failed");
	return json_result (result);
}

static JsonNode *
build_schema (const ToolSpec *spec)
{
	g_autofree gchar     *text = g_strdup_printf ("{\"type\":\"object\",\"properties\":{%s}%s%s}",
	                                              spec->properties,
	                                              spec->required != NULL ? ",\"required\":" : "",
	                                              spec->required != NULL ? spec->required : "");
	g_autoptr(JsonParser) parser = json_parser_new ();
	g_autoptr(GError)     error = NULL;

	if (!json_parser_load_from_data (parser, text, -1, &error))
		g_error ("bad schema for %s: %s", spec->name, error->message);
	return json_node_copy (json_parser_get_root (parser));
}

McpServer *
lrg_ctl_mcp_server_new (LrgCtlCallFunc call,
                        gpointer       call_data)
{
	McpServer *server = mcp_server_new (LRG_CTL_MCP_SERVER_NAME, LRG_EDITORCTL_VERSION);
	gsize      i;

	mcp_server_set_instructions (server, instructions);
	for (i = 0; i < G_N_ELEMENTS (tools); i++)
	{
		g_autoptr(JsonNode) schema = build_schema (&tools[i]);
		g_autoptr(McpTool)  tool = mcp_tool_new (tools[i].name, tools[i].description);
		ToolContext        *ctx = g_new0 (ToolContext, 1);

		mcp_tool_set_input_schema (tool, schema);
		mcp_tool_set_read_only_hint (tool, tools[i].read_only);
		mcp_tool_set_destructive_hint (tool, tools[i].destructive);
		mcp_tool_set_open_world_hint (tool, FALSE);
		ctx->spec = &tools[i];
		ctx->call = call;
		ctx->call_data = call_data;
		if (tools[i].kind == TOOL_SCREENSHOT)
			mcp_server_add_deferred_tool (server, tool, handle_screenshot, ctx, g_free);
		else
			mcp_server_add_tool (server, tool, handle_tool, ctx, g_free);
	}
	return server;
}
