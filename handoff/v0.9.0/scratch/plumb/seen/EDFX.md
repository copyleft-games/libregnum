FINAL
# EDFX IPC methods (implemented and tested in tests/test-editor-app-vfx.c; full docs: docs/editor/vfx.org#vfx-ipc)

Registered by lrg_editor_particle_editor_install() / lrg_editor_weather_editor_install()
(called from the editors' constructed() and from the assets panel: patch applied).

Conventions (same as physasset.*):
- every method takes `path` (the .rparticles / .rweather file, absolute or project-relative);
  the file is opened in its editor tab when it is not open yet.
- `emitter`: an index (number) or an emitter name (string). `module`: index in that emitter's stack.
- `preset`: index or preset id. `layer`: index or layer id.
- values (`value`) are file syntax: a JSON number / bool, or a string exactly as written in the YAML
  file ("[4, 8]" ranges, "#ffc040" colours, "0 1; 1 0 ease-out-quad" curves, "0 #fff; 1 #0000" gradients),
  or a JSON array of numbers (ranges / vectors). null resets to the default (removes the key).
- every edit is ONE undo step of the document (undo/redo through the existing data.undo / data.redo).
- errors: invalid-params (missing/malformed params, engine validation message with the key path, a key the object does not have, a taken name),
  not-found (no such file / emitter / module / preset / layer), failed (does not load, IO, render without GL).

## particles.*
| method | params | result |
|---|---|---|
| particles.open | path | {path, panel} |
| particles.create | path, template? ("default" or "empty") | {path} ; existing file -> invalid-params |
| particles.get | path | {path, name, seed, duration, loop, prewarm, local_space, emitters:[{index, name, enabled, max_particles, start_delay, renderer:{kind, blend, texture,...all render keys}, modules:[{index, kind, stage, enabled, gpu_capable, values:{key: text}}]}], unknown_keys:[..], load_error, selected:{emitter, module}, dirty} |
| particles.kinds | (none) | {kinds:[{kind, stage (spawn/shape/init/force/collision/over-life/event), gpu_capable}]} |
| particles.set | path, key, value, emitter? , module? , renderer? (bool) | {ok:true} : asset key (no emitter), emitter key (emitter), renderer key (emitter + renderer:true), module key (emitter + module) |
| particles.add_emitter | path, name? (default "emitter", numbered when taken), index? | {index, name} |
| particles.remove_emitter | path, emitter | {ok} |
| particles.move_emitter | path, from, to | {ok} |
| particles.duplicate_emitter | path, emitter | {index, name} |
| particles.add_module | path, emitter, kind, index? (-1 / absent appends) | {index} |
| particles.remove_module | path, emitter, module | {ok} |
| particles.move_module | path, emitter, from, to | {ok} |
| particles.select | path, emitter, module? (-1 none) | {emitter, module} (the grid shows it) |
| particles.solo | path, emitter (null clears) | {solo} (preview only: draws that emitter + its sub-emitter targets; not saved) |
| particles.play | path, playing (bool) | preview state (see particles.preview) |
| particles.restart | path | preview state |
| particles.seek | path, time (s) | preview state (deterministic: restart + fixed 1/60 steps) |
| particles.preview | path, speed? (0.05..4), seed? (null = the asset's), ground? (bool), background? ("#rrggbb"), gpu? (bool) | {time, playing, speed, seed, ground, background, gpu, solo} |
| particles.stats | path | preview state + {active, bounds:[minx,miny,minz,maxx,maxy,maxz] or null, emitters:[{name, enabled, active, capacity, bounds, gpu_ok, gpu_message, simulation ("effect"/"gpu-emitter"), backend ("cpu"/"gpu"), drawn}]} |
| particles.save | path | {path} |
| particles.render | path, file (png), size? (default 512) | {file, width, height} ; FAILED without a GL context |

## weather.*
| method | params | result |
|---|---|---|
| weather.open | path | {path, panel} |
| weather.create | path, template? ("default" or "empty") | {path} ; existing file -> invalid-params |
| weather.get | path | {path, dirty, load_error, name, default_preset, default_transition, presets:[{index, id, name, ambient_color, ambient_brightness, layers:[{index, kind, id, intensity, enabled, params:{name: text}}]}], transitions:[{from, to, duration}], selected:{preset, layer}, preview} |
| weather.kinds | kind? | {kinds:[{kind, params:[{name, type (GType name), blurb, default, min?, max?, values? (enum nicks)}]}]} ; unknown kind -> not-found |
| weather.set | path, key ("name" / "default-preset" / "default-transition"), value | {ok} |
| weather.add_preset | path, id, name? | {index} |
| weather.remove_preset | path, preset | {ok} |
| weather.set_preset | path, preset, name?, ambient_color? ("#rrggbb"), ambient_brightness? | {ok} |
| weather.add_layer | path, preset, kind, id? | {index, id} |
| weather.remove_layer | path, preset, layer | {ok} |
| weather.move_layer | path, preset, from, to | {ok} |
| weather.set_layer | path, preset, layer, intensity?, enabled? | {ok} |
| weather.set_param | path, preset, layer, name, value (null unsets) | {ok} |
| weather.set_transition | path, from, to, duration | {ok} |
| weather.remove_transition | path, from, to | {ok} |
| weather.select | path, preset, layer? | {preset, layer} |
| weather.preview | path, preset? (switch instantly; "none"), transition_to? (crossfade with the resolved duration; "none"), advance? (seconds to step) | {active, transitioning, duration, remaining, wind_x, wind_y} |
| weather.save | path | {path} |
| weather.render | path, file, width? (640), height? (360) | {file, width, height} ; failed without GL |
