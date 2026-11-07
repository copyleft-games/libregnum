# EDAUDIO IPC methods: FINAL (implemented and tested in tests/test-editor-app-audio.c)

Registered from src/editor/ui/data/lrg-editor-mixer-editor.c (mixer.*, via
lrg_editor_mixer_editor_install(app), called by the Mixer panel's constructor
and by every mixer editor) and src/editor/ui/lrg-editor-inspector-panel.c (audio.*).

Common: every mixer.* method takes optional "path" (a .rmixer, project-relative
or absolute). Without it: the project's default mixer (project.ryaml
audio.mixer); error NOT_FOUND "No mixer: pass \"path\" or set the project's
default mixer". Edits go through the LrgEditorMixerEditor of that file (opened
as a tab when needed), so each call is ONE undo step in that document and leaves
it dirty until mixer.save. Levels are dB (-144 .. 12.04). Errors: INVALID_PARAMS
(missing/mistyped), NOT_FOUND (bus/effect/snapshot/file), FAILED (engine refused:
message is the LrgMixerAsset error, e.g. routing loop).

Layout JSON ("mixer" object below): {path, name, default_snapshot|null, dirty,
 buses:[{name, parent|null, volume, mute, solo,
         sends:[{target, level}], effects:[{id, type, enabled, params:{name: value}}]}],
 snapshots:[{name, blend, volumes:{bus: dB}, sends:[{source,target,level}], params:{"bus/effect/param": n}}],
 ducking:[{trigger, target, threshold, depth, attack, release}]}

| method | params | result |
|---|---|---|
| mixer.show | {} | {panel:"mixer", shown} (docks/raises the Mixer panel) |
| mixer.open | {path?} | {path, panel} opens the editor tab |
| mixer.create | {path, buses?:[names], set_default?:bool} | mixer (writes a new .rmixer: master + the named buses; set_default writes project.ryaml audio.mixer) |
| mixer.set_default | {path\|null} | {mixer: path\|null} (project.ryaml audio.mixer, saved) |
| mixer.get | {path?} | mixer |
| mixer.effect_types | {} | {types:[{name, gtype, params:[{name, type, default, min?, max?, values?(enum nicks)}]}]} |
| mixer.add_bus | {path?, name, parent?} | mixer |
| mixer.remove_bus | {path?, name} | mixer |
| mixer.set_bus | {path?, name, volume?, mute?, solo?, parent?} | mixer |
| mixer.set_send | {path?, source, target, level} | mixer |
| mixer.remove_send | {path?, source, target} | mixer |
| mixer.add_effect | {path?, bus, type, id?} | mixer |
| mixer.remove_effect | {path?, bus, id} | mixer |
| mixer.move_effect | {path?, bus, id, index} | mixer |
| mixer.set_effect | {path?, bus, id, enabled?, params?:{name: value}} (value null unsets) | mixer |
| mixer.add_snapshot | {path?, name, blend?, capture?:bool} | mixer (capture: copy current bus volumes + send levels into it) |
| mixer.remove_snapshot | {path?, name} | mixer |
| mixer.set_snapshot | {path?, name, blend?, default?:bool, volumes?:{bus: dB\|null}, sends?:[{source,target,level\|null}], params?:{"bus/effect/param": n\|null}} | mixer |
| mixer.capture_snapshot | {path?, name} | mixer (overwrites its volumes/sends with the current base faders) |
| mixer.set_duck | {path?, trigger, target, threshold?, depth?, attack?, release?} | mixer |
| mixer.remove_duck | {path?, trigger, target} | mixer |
| mixer.preview | {path?, playing?:bool, snapshot?:name\|null, snapshots?:[names], weights?:[n], blend?:seconds} | {playing, snapshot\|null, source} (preview = offline LrgAudioMixer with a test signal on every leaf bus, rendered per frame) |
| mixer.meters | {} | {source:"play"\|"preview"\|"none", buses:[{name, peak, rms, peak_db, rms_db, duck_gain, audible}]} |
| mixer.save | {path?} | {path, saved:true} |
| audio.get | {node} (GUID or path) | {node, kind:"audio-emitter", params:{min_distance, max_distance (=radius), rolloff_mode, rolloff, rolloff_curve_keys, cone_inner_angle, cone_outer_angle, cone_outer_gain, doppler_factor, speed_of_sound, occlusion, occlusion_gain, occlusion_cutoff, spatial_blend, bus, volume}} |
| audio.set | {node, params:{...same keys...}} | same as audio.get (ONE undo step) |
| audio.curve | {node, samples?:int(2..256, def 32)} | {min, max, mode, points:[[distance, gain]...]} (engine's lrg_audio_source_component_get_distance_gain) |
| audio.buses | {} | {mixer: path\|null, buses:[names]} (the bus picker's list) |

Notes (final):
- mixer.meters takes optional "path"; without it the project mixer's editor (only when open) supplies the bus list.
- mixer.set_snapshot "sends" entries: level omitted or null removes the override.
- audio.* node param accepts a GUID or a node path; audio.get on a non-emitter -> INVALID_PARAMS "'X' is not an audio emitter".
- audio.set unknown key -> INVALID_PARAMS "Unknown audio param 'k'"; values use the component property types (rolloff_mode by nick: linear|inverse|logarithmic|custom).
- Actions (for action.run / MCP run_action): mixer.show, mixer.new, mixer.open.
- Suggested lrgeditorctl verbs: `mixer get|open|create|save|preview|meters|effect-types`, `mixer bus add|remove|set`, `mixer send set|remove`, `mixer effect add|remove|move|set`, `mixer snapshot add|remove|set|capture`, `mixer duck set|remove`, `mixer default PATH|none`, `audio get|set|curve|buses NODE`. Read-only: mixer.get, mixer.effect_types, mixer.meters, audio.get, audio.curve, audio.buses. Destructive: mixer.remove_*, mixer.set_default (null).
