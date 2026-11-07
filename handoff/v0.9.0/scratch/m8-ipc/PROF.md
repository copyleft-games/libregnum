# PROF IPC methods (DRAFT: names stable, result fields may grow)

Docs: docs/editor/profiling.org (#editor-profiling-ipc has the same tables with more words).

Common: frame `index` = the profiler's monotonic frame number (not a ring slot). Times: `*_us` integer
microseconds relative to the frame start, `*_ms` doubles. `source` = "live" (the profiler's ring) or
"capture" (the panel's current capture: the last profiler.capture or profiler.load), default "capture"
when one is held, else "live". `path` = absolute or relative to the project. Errors: invalid-params for
missing/bad params, not-found for no such frame / call / texture / no capture yet, failed for I/O,
parse or GL errors (no GPU, read-back failed).

## profiler.* (registered by src/editor/ui/lrg-editor-profiler-panel.c)
- profiler.sample {frames?:int=60, enable?:bool} -> {enabled, pinned, paused, gpu, frame_ms, frames:[ms...], sections:[{name, last_ms, avg_ms, min_ms, max_ms, count}]}   (unchanged from 0.8.1, + `gpu`)
- profiler.set {enable?:bool, gpu?:bool, paused?:bool, ring?:int frames kept (8..4096)} -> {enabled, pinned, paused, gpu, gpu_supported, ring}
- profiler.capture {frames?:int=120, path?} -> {frames, first, last, events, threads:[name...], path|null}   (snapshots the last N ring frames into the panel's capture; saves it to path (.rprof) when given)
- profiler.save {path} -> {path, frames, events}   (saves the panel's capture; not-found without one)
- profiler.load {path} -> {path, frames, first, last, events, threads:[name...]}   (becomes the panel's capture; the panel shows it)
- profiler.frames {source?, count?:int=60} -> [{index, start_us, duration_ms, gpu_ms|null, events, counters}]   (newest last)
- profiler.frame {source?, index?:int (default newest)} -> {index, start_us, duration_ms, gpu_ms|null, threads:[{id, name}], events:[{name, thread, depth, parent:int (-1 root), start_us, duration_us}], gpu:[{name, depth, start_us, duration_us}], counters:[{name, value}]}
- profiler.compare {source?, a:int, b:int} -> {a:{index, duration_ms}, b:{index, duration_ms}, sections:[{name, a_ms, b_ms, delta_ms, a_calls, b_calls}]}   (sections by name, inclusive time summed per frame, sorted by |delta| desc)
- profiler.view {mode?:"timeline"|"flame"|"table"|"compare", frame?:int, compare?:int, show?:bool} -> {mode, frame|null, compare|null, source}   (drives the panel; show presents it)

## frame.* (registered by src/editor/ui/lrg-editor-frame-debugger.c)
- frame.capture {} -> {armed:true}   (arms LrgFrameCapture on the viewport's LrgWorld3DRenderer; the next viewport render is recorded; poll frame.status)
- frame.status {} -> {armed, captured, calls, width, height, scrub:int (-1 = full frame), passes:[{pass, calls}]}
- frame.calls {pass?:nick, offset?:int=0, limit?:int=200} -> {total, calls:[{index, pass, kind, entity|null, guid|null, mesh|null, material|null, shader, vertices, triangles}]}
- frame.call {index} -> {index, pass, kind, entity, guid, mesh, material, shader, shader_id, texture_id, vertices, triangles, instances, color:[r,g,b,a], transform:[16], state:{blend, blend_mode, depth_test, depth_write, cull, wireframe, framebuffer}}
- frame.scrub {index:int (-1 = full frame)} -> {scrub, calls}   (the viewport and the replay image draw calls 0..index only)
- frame.texture {id?:int GL texture id, target?:"replay"|"shadow"|"depth"|"normal"|"ssao", path} -> {path, width, height}   (saves a texture/RT as PNG; replay = the partial frame at the scrub)
- frame.save {path} -> {path, calls}   (.rframe YAML)
- frame.load {path} -> frame.status   (offline view; scrub/replay need the live renderer)
- frame.show {} -> {}   (presents the Frame Debugger panel)
