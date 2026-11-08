# libregnum editor MCP server

The editor exposes its operations as MCP (Model Context Protocol) tools, implemented with mcp-glib. The same tool set is served two ways:

| Command | When |
|---------|------|
| `lrgeditorctl mcp [-s SESSION]` | Bridge to an editor that is already running (a person's windowed session, or a headless one). Each call finds the session again, so it survives editor restarts and can be configured before any editor runs. |
| `lrgeditor --mcp [PROJECT]` | The agent starts its own editor. MCP runs inside the editor process; it is headless when there is no display. A headless editor exits when the client disconnects, unless the level has unsaved changes (then it keeps running for `lrgeditorctl save`/`quit`). The normal IPC socket stays available too, so `lrgeditorctl` works alongside. |

## Registering with clients

```bash
# Claude Code
claude mcp add libregnum -- lrgeditorctl mcp
claude mcp add libregnum-keep -- lrgeditor --mcp /home/me/games/keep

# Any client with an mcpServers JSON config
{
  "mcpServers": {
    "libregnum": { "command": "lrgeditorctl", "args": ["mcp"] }
  }
}
```

Use absolute paths for project directories in configs. With several editors running, pin one: `lrgeditorctl mcp -s keep`.

## Tools

| Tool | IPC method | Notes |
|------|-----------|-------|
| `editor_info` | editor.info | Call first: level, dirty, counts, tool, selection |
| `list_sessions` | (socket scan) | Running editors |
| `get_tree` | node.tree | `{node?}` |
| `get_node` | node.get | `{node, recursive?}` |
| `find_nodes` | node.find | `{query}` (name substring or GUID) |
| `create_node` | node.create (+ node.set) | `{name, parent?, kind?, primitive?, index?, location?, rotation_degrees?, scale?}` in one undo step |
| `update_node` | node.set | `{node, name?, location?, rotation_degrees?, rotation?, scale?, visible?, locked?, visual?}` |
| `delete_node` | node.delete | destructive (undoable) |
| `duplicate_node` | node.duplicate | |
| `move_node` | node.move | `{node, parent?, index?, keep_world?}` |
| `add_component` / `set_component` / `remove_component` | component.* | registered component type names |
| `add_script` / `remove_script` | script.* | `{node, script, language?}` |
| `get_selection` / `set_selection` | selection.* | |
| `undo` / `redo` / `get_history` | edit.* | |
| `save_level` | level.save | `{path?}` |
| `open_level` / `new_level` | level.open / level.new | destructive with `force` |
| `get_level_yaml` | level.serialize | the whole `.rlevel` as text |
| `validate_level` | level.validate | `[]` means clean |
| `list_actions` / `run_action` | action.* | any menu command by id |
| `play` | play.* | `{command: start\|pause\|step\|stop\|state}` |
| `run_game` | run.start / run.stop | `{stop?}` |
| `get_view` / `set_view` / `focus_view` | view.* | camera |
| `screenshot` | view.screenshot | returns a PNG image; `{window?}`; windowed editors only |
| `get_log` | log.tail | `{count?}` |
| `list_assets` | asset.list | `{type?}` |
| `project_info` | project.info | |
| `ui_state` | ui.state | |
| `notify` | ui.notify | toast for the human; `{message, level?: info\|success\|warning\|error}` |
| `get_tilemap` / `configure_tilemap` | tilemap.get / tilemap.configure | grid of a `tilemap` node; width/height, tile px, tile_size, tileset, layers |
| `paint_tiles` / `paint_tile_rect` / `fill_tiles` | tilemap.paint / rect / fill | `cells` or brush `points`; tile ids 1-based, 0 erases; one undo step each |
| `list_timelines` / `get_timeline` | timeline.list / get | keyframe clips stored in the level |
| `set_keyframe` / `remove_keyframe` / `configure_timeline` | timeline.key / unkey / set | channel + value, or the node's transform; easing nicks |
| `preview_timeline` | timeline.seek | pose the level at a time (not saved) |
| `get_thumbnail` | asset.thumbnail | cached PNG path of a texture/model/level/prefab |
| `get_asset_meta` / `set_asset_meta` | asset.meta.get / set | `.meta` sidecar: guid, importer, settings; set is all or nothing |
| `vcs_status` | vcs.status | git branch and files that are not clean; `{path?}` |
| `prefab_overrides` | prefab.overrides | read-only: source, linked, override keys |
| `prefab_apply` / `prefab_revert` | prefab.apply / revert | destructive: `{node, keys?}`; apply overwrites the prefab file and syncs other instances |
| `prefab_sync` / `prefab_relink` / `prefab_unlink` | prefab.sync / relink / unlink | unlink is destructive (undoable) |
| `set_viewport_layout` / `set_render_mode` / `set_overlay` | view.layout / render_mode / overlay | without the value they report the current state |
| `inspector_list` / `inspector_get` | inspector.list / get | read-only: sections and rows `{name, label, group, hint, value, mixed, state}` |
| `inspector_set` / `inspector_reset` | inspector.set / reset | `{node\|nodes, section, component?, name, value}`; several nodes = one undo step |
| `inspector_apply_override` / `inspector_revert_override` | inspector.apply_override / revert_override | destructive: one prefab override row |
| `inspector_state` | inspector.state | read-only, no parameters: lock, shown nodes, live object, prefab (lock it with `run_action` `inspector.toggle-lock`) |
| `hierarchy_reveal` / `hierarchy_rename` / `hierarchy_state` | hierarchy.* | reveal and select, inline or direct rename, visible rows |
| `hierarchy_filter` | hierarchy.filter | `{text?}`: filter the panel (empty clears); returns the match count |
| `asset_reveal` / `asset_rename` / `asset_move` / `asset_duplicate` | asset.* | the `.meta` follows its asset; duplicates get a new guid |
| `asset_trash` / `asset_orphans` | asset.trash / orphans | destructive: to the desktop trash |
| `profiler_sample` | profiler.sample | `{frames?, enable?}` |
| `code_open` / `code_list` / `code_save` | code.* | the built-in code editor panel |
| `open_palette` / `open_preferences` | palette.open / preferences.open | `{mode?: commands\|files, query?}` / `{page?}`: open them for the human |
| `data_open` / `data_list` | data.open / data.list | open a data file (input map, locale, sound bank, theme, HUD/UI layout, dialog tree, dungeon) in its editor; list is read-only |
| `data_get` / `data_validate` | data.get / data.validate | read-only: `{path?, at?, as?}` node `{at, kind, value, keys, length, yaml, line}`; `{ok, errors, issues}` |
| `data_set` / `data_edit` | data.set / data.edit | `{path?, at, value\|yaml, gesture?}`; `{path?, op, at, ...}` with op insert, move, add_key, rename_key, remove; one undo step each |
| `data_undo` / `data_redo` / `data_save` / `data_reveal` | data.* | per-file history; `save_as`; reveal shows a YAML path or line |
| `data_reload` / `data_close` | data.reload / data.close | destructive: reload and `close {force: true}` discard unsaved edits |
| `call_method` | any | `{method, params}` escape hatch for any other IPC method |

Results are JSON text (YAML text for `get_level_yaml`); failures come back as tool errors with the editor's message, e.g. `node.get: No node 'Hall/Tabel' (not-found)`.

## Working well with it

- Start with `editor_info` and `get_tree`; address nodes by path, switch to GUIDs when renaming/moving many.
- Size primitives with `scale` (they are 1 m); `rotation_degrees` is easier to reason about than radians.
- After a batch of edits: `validate_level`, then `screenshot` if the editor is windowed, then report and ask before `save_level` unless the user asked to save.
- `play` start/stop is safe: stop restores the level and keeps edits made during play on the redo stack.
- Never pass `force` to `open_level`/`new_level` without the user's explicit OK.

## 0.9.0 tools (args pass through; path absolute or project-relative)
Materials: material_open, material_create, material_create_variant, material_get (ro), material_set, material_revert (destructive), material_save, material_preview, material_assign, material_list (ro).
Shaders: shader_create, shader_compile, shader_diagnostics (ro), shader_uniforms (ro), shader_reload.
Shader graphs: shadergraph_open, _create, _node_types (ro), _get (ro), _add_node, _remove_node (destructive), _connect, _disconnect (destructive), _set_param, _set_input, _validate (ro), _glsl (ro), _save.
Level look: environment_get (ro), environment_set{key,value|values}, environment_reset (destructive), environment_bake then poll environment_progress (ro), environment_show.
Post stacks: post_effect_types (ro), post_open, post_create, post_get (ro), post_add, post_remove (destructive), post_move, post_set, post_save, post_preview, post_save_look, post_assign.
LOD and views: lod_get (ro), lod_force{node,level (-1 auto)}, set_view_layers{viewport?,mask?,layer?,enabled?}; set_render_mode also takes overdraw, uv, light-complexity, ao, reflections.

## 0.9.0 tools (M3/M4)
Lighting bake: bake_get, bake_progress, bake_evaluate{position?,normal?} (ro); bake_set{key,value|values|reset|auto_save}; bake_start{save?} then poll bake_progress; bake_cancel; bake_save{dir?}; bake_clear (destructive); bake_preview{show?,page?}; bake_show.
Sequences: sequence_list/sequence_get (ro), sequence_open, sequence_create, sequence_set, sequence_add_track, sequence_add_key, sequence_set_key, sequence_add_section, sequence_remove (destructive), sequence_bind, sequence_play, sequence_scrub, sequence_save, sequence_undo, sequence_from_timeline; timeline_tangent.
2D skeletons: skel2d_get/skel2d_pick (ro), skel2d_create/open/add_bone/set_bone/remove_bone (destructive)/add_slot/set_slot/remove_slot (destructive)/draw_order/attach/detach (destructive)/mesh/auto_weights/paint_weights/skin/clip/key/play/mode/save.
Animation: anim_get/anim_validate (ro), anim_open/create/enter/add_layer/remove_layer (destructive)/add_state/remove_state (destructive)/set_state/add_transition/set_transition/remove_transition (destructive)/add_parameter/remove_parameter (destructive)/set_parameter/set_blend_tree/preview/save; bonemap_get (ro), bonemap_open/create/set/auto_fill/set_rules/set_models/preview/save; clip_list/clip_info (ro), clip_preview, clip_set_morph.
anim_preview takes {path, model?, restart?, play?, advance?, scrub?}.

## Physics tools (0.9.0)
Node physics: physics_get, physics_state (ro), physics_add_body, physics_remove_body (destructive), physics_set_body, physics_add_shape, physics_remove_shape (destructive), physics_set_shape, physics_move_shape, physics_add_joint, physics_remove_joint (destructive), physics_set_joint, physics_fit, physics_overlay, physics_show. Project layers: physics_layers_get (ro), physics_layers_set_name, physics_layers_set_collides, physics_layers_settings, physics_layers_reset (destructive), physics_layers_open. Ragdolls: physasset_get (ro), physasset_open, physasset_create, physasset_set_model, physasset_add_part, physasset_remove_part (destructive), physasset_set_part, physasset_generate (destructive), physasset_stabilize, physasset_exclude, physasset_set, physasset_select, physasset_simulate, physasset_save. Arguments pass through; field maps go in a `values` object.

## 0.9.0 navigation, AI, effects, terrain, audio, 2D, scripting, debugging and profiling tools

<!-- m67e-generated: begin -->
Navigation: nav_get (ro), nav_set, nav_new, nav_bake, nav_cancel, nav_save, nav_clear (destructive), nav_area_add, nav_area_remove (destructive), nav_area_set, nav_area_box_add, nav_area_box_remove (destructive), nav_area_box_set, nav_link_add, nav_link_remove (destructive), nav_link_set, nav_select, nav_overlay, nav_agents (ro), nav_debug_agent, nav_show.
Behaviour tree: bt_open, bt_create, bt_get (ro), bt_palette (ro), bt_add_node, bt_remove_node (destructive), bt_move_node, bt_set_node, bt_set_position, bt_layout, bt_select, bt_show, bt_add_key, bt_remove_key (destructive), bt_set_subtree, bt_remove_subtree (destructive), bt_open_subtree, bt_validate (ro), bt_save, bt_live (ro).
State machine: fsm_open, fsm_create, fsm_get (ro), fsm_enter, fsm_up, fsm_add_state, fsm_remove_state (destructive), fsm_rename_state, fsm_set_initial, fsm_add_transition, fsm_set_transition, fsm_remove_transition (destructive), fsm_set_actions, fsm_set_position, fsm_select, fsm_add_key, fsm_remove_key (destructive), fsm_validate (ro), fsm_save, fsm_live (ro), fsm_send_event.
AI debug: ai_show, ai_entities (ro), ai_select, ai_state (ro), ai_set_key, ai_send_event, ai_brains (ro), ai_topic (ro), ai_log (ro).
Particle effect: particles_open, particles_create, particles_get (ro), particles_kinds (ro), particles_set, particles_add_emitter, particles_remove_emitter (destructive), particles_move_emitter, particles_duplicate_emitter, particles_add_module, particles_remove_module (destructive), particles_move_module, particles_select, particles_solo, particles_play, particles_restart, particles_seek, particles_preview, particles_stats (ro), particles_save, particles_render.
Weather: weather_open, weather_create, weather_get (ro), weather_kinds (ro), weather_set, weather_add_preset, weather_remove_preset (destructive), weather_set_preset, weather_add_layer, weather_remove_layer (destructive), weather_move_layer, weather_set_layer, weather_set_param, weather_set_transition, weather_remove_transition (destructive), weather_select, weather_preview, weather_save, weather_render.
Terrain: terrain_get (ro), terrain_brush, terrain_stroke, terrain_sample (ro), terrain_add_layer, terrain_remove_layer (destructive), terrain_set_layer, terrain_add_foliage, terrain_remove_foliage (destructive), terrain_set_foliage, terrain_rebake, terrain_save, terrain_create, terrain_show.
Water: water_get (ro), water_set, water_set_wave, water_add_point, water_set_point, water_remove_point (destructive), water_save.
Mixer: mixer_show, mixer_open, mixer_create, mixer_set_default (destructive), mixer_get (ro), mixer_effect_types (ro), mixer_add_bus, mixer_remove_bus (destructive), mixer_set_bus, mixer_set_send, mixer_remove_send (destructive), mixer_add_effect, mixer_remove_effect (destructive), mixer_move_effect, mixer_set_effect, mixer_add_snapshot, mixer_remove_snapshot (destructive), mixer_set_snapshot, mixer_capture_snapshot, mixer_set_duck, mixer_remove_duck (destructive), mixer_preview, mixer_meters (ro), mixer_save.
Audio emitter: audio_get (ro), audio_set, audio_curve (ro), audio_buses (ro).
Tile painting: tiles_get (ro), tiles_grid, tiles_brush, tiles_paint, tiles_refresh, tiles_cell_at (ro), tiles_tile_info (ro).
Tileset: tileset_create, tileset_open, tileset_get (ro), tileset_set, tileset_select, tileset_tile, tileset_collision, tileset_rule_tile, tileset_rule, tileset_preview (ro), tileset_save.
Sprite sheet: sprite_create, sprite_open, sprite_get (ro), sprite_slice, sprite_frame, sprite_clip, sprite_clip_frame, sprite_event, sprite_play, sprite_save.
Asset creation: asset_new.
Script debugger: debug_state (ro), debug_show, debug_breakpoints (ro), debug_set_breakpoint, debug_remove_breakpoint (destructive), debug_toggle_breakpoint, debug_clear_breakpoints (destructive), debug_continue, debug_pause, debug_step_over, debug_step_in, debug_step_out, debug_stop (destructive), debug_stack (ro), debug_scopes (ro), debug_variables (ro), debug_evaluate, debug_watches (ro), debug_add_watch, debug_remove_watch, debug_dap_start, debug_dap_stop.
Language server: lsp_servers (ro), lsp_status (ro), lsp_diagnostics (ro), lsp_complete (ro), lsp_hover (ro), lsp_definition, lsp_references (ro), lsp_rename, lsp_format, lsp_restart.
Profiler capture: profiler_set, profiler_capture, profiler_save, profiler_load, profiler_frames (ro), profiler_frame (ro), profiler_compare (ro), profiler_view.
Frame debugger: frame_capture, frame_status (ro), frame_calls (ro), frame_call (ro), frame_scrub, frame_texture, frame_save, frame_load, frame_show.
Tool script: script_run, script_languages (ro), script_tools (ro), script_run_tool, script_reload, script_run_in_editor, script_editor_running (ro).
Visual script: vscript_create, vscript_open, vscript_get (ro), vscript_node_types (ro), vscript_gi_search (ro), vscript_add_node, vscript_remove_node (destructive), vscript_set_node, vscript_connect, vscript_disconnect, vscript_set_variable, vscript_add_subgraph, vscript_remove_subgraph (destructive), vscript_add_comment, vscript_remove_comment (destructive), vscript_set_program, vscript_select, vscript_frame, vscript_palette, vscript_validate (ro), vscript_run (ro), vscript_live (ro), vscript_breakpoint, vscript_continue, vscript_save.
Memory profiler: memory_status (ro), memory_types (ro), memory_resources (ro), memory_snapshot, memory_snapshots (ro), memory_diff (ro), memory_clear_snapshots (destructive), memory_history (ro), memory_show.
Network stats: network_stats (ro), network_history (ro), network_enable, network_reset (destructive), network_show.
<!-- m67e-generated: end -->
