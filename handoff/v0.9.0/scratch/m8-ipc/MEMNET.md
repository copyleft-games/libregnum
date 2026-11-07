# MEMNET IPC methods: DRAFT (being implemented; shapes may still gain fields)

Registered from src/editor/ui/lrg-editor-memory-panel.c (memory.*) and
src/editor/ui/lrg-editor-network-panel.c (network.*), at panel construction.
Errors: INVALID_PARAMS (missing/mistyped), NOT_FOUND (unknown snapshot id / peer).

Type row: {type, count, instance_size, bytes, source:"instance-count"|"tracked"}
Resource row: {name, kind:"texture"|"model"|"mesh"|"sound"|"music"|"font"|"material"|"animation"|"object"|"other", refs, cpu_bytes, gpu_bytes, details}
Totals: {instances, types, type_bytes, resources, cpu_bytes, gpu_bytes}

| method | params | result |
|---|---|---|
| memory.status | {} | {instance_counts:bool, tracked:int, asset_manager:bool, hint} |
| memory.types | {filter?:substring, limit?:int (def 200), sort?:"count"\|"bytes"\|"name"} | {source, types:[type row], totals} (live) |
| memory.resources | {kind?} | {resources:[resource row], totals} |
| memory.snapshot | {label?} | {id, label, time, totals} (stored in the panel's list) |
| memory.snapshots | {} | {snapshots:[{id, label, time, totals}]} |
| memory.diff | {from:id, to?:id (default: a fresh live snapshot), limit?:int} | {from, to, types:[{type, before, after, delta, bytes_delta}], resources:[{name, kind, change:"added"\|"removed"\|"changed", bytes_delta}]} |
| memory.clear_snapshots | {} | {removed:int} |
| memory.history | {type?, samples?:int} | {interval, instances:[n...], cpu_bytes:[n...], gpu_bytes:[n...], type?:[counts]} |
| memory.show | {} | {panel:"memory", shown:true} |
| network.stats | {peer?, window?:seconds (def 10)} | {enabled, window, totals:{packets_in, packets_out, bytes_in, bytes_out, lost}, rates:{bytes_in, bytes_out, packets_in, packets_out} (per second over window), peers:[{peer, connected, rtt_ms, rtt_avg_ms, rtt_min_ms, rtt_max_ms, loss, packets_in, packets_out, bytes_in, bytes_out, lost}], messages:[{type, packets_in, packets_out, bytes_in, bytes_out}]} |
| network.history | {peer?, type?, metric:"bytes_in"\|"bytes_out"\|"packets_in"\|"packets_out"\|"rtt"\|"lost", seconds?:int} | {metric, interval:1, values:[n...]} (oldest first) |
| network.enable | {enabled:bool} | {enabled} (the default LrgNetStats every LrgNetServer/LrgNetClient/LrgMmoDatagram feeds) |
| network.reset | {} | {reset:true} |
| network.show | {} | {panel:"network", shown:true} |

Actions: memory.show-panel, memory.snapshot, network.show-panel, network.toggle, network.reset.
Read-only: memory.status, memory.types, memory.resources, memory.snapshots, memory.diff, memory.history, network.stats, network.history.
