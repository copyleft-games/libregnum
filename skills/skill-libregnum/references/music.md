# Composing music and sound effects in lrgeditor (0.10.0)

How an agent writes songs (`.rsong`) and sound effects (`.rfoley`) through the editor: discovery, building in batches, mixing by numbers, export. The same JSON works as IPC (`song.*`), MCP tools (`song_*`) and `lrgeditorctl song ...`. Full method tables: `ipc-protocol.md` (song.*, foley.*, audio.formats); the editor itself: `docs/editor/music.org`.

## Rules that matter

- Every edit is one undo step; `song.apply` runs a list of ops as ONE step and changes nothing when any op fails (the error names `ops[i] (name)`). Build whole sections with `song.apply`.
- An edit whose YAML would not load back is refused ("The edit would not load back: ..."). The usual cause: a duck whose target bus feeds its trigger (or a send loop). Duck from a dedicated `kick` bus into a `synths` bus, both children of `master`.
- Keys: note names (`A2`, `C#4`; C4 = 60) or MIDI numbers. Notes: `[beat, key, length, velocity]` (velocity 0..1, default 0.8), `{beat, key, length, velocity}` or `"beat key length velocity"`. Beats are quarter notes from 0.
- Drum lanes: `{"kick": "x... x... x... x..."}`, one char per step (16 steps per pattern by default): `.`/`-` rest, `g` 0.3, `o` 0.5, `x` 0.8, `X` 1.0; spaces and `|` ignored. Lane names are drum pieces (`kick snare clap rim closed-hat pedal-hat open-hat low-tom mid-tom high-tom crash ride cowbell`) or notes.
- A pattern clip loops its pattern over the clip length. Clips on a track are sorted by start; `add_clip` returns the new clip's `index`.
- You cannot hear: measure with `song.render` (`tracks=true` for every track alone) and listen with numbers.
- The audio device opens only on `song.play` / `foley.play`. Never call them in CI or headless runs.

## 1. Discover

```json
{"method": "song.instruments"}
```
Returns `instrument_types` (synth presets with descriptions: `init synthwave-bass synthwave-pad synthwave-lead arp-pluck brass-stab sub-bass acid-bass bell strings noise-riser organ`; drum kits `808 909 linn` with pieces and keys; sampler, soundfont, foley; every parameter with min/max/default), `effects` (compressor, limiter, equalizer, distortion, filter, gate, stereo-widener, reverb, echo, chorus, low-pass, high-pass, pitch-shift with parameters), `foley_kinds`, the step and note syntax, `automation_targets` and `ops`. `audio.formats` says which export formats this build can write.

## 2. Create

```json
{"method": "song.new", "params": {"path": "music/night.rsong", "empty": true}}
{"method": "song.set", "params": {"title": "Night Drive", "artist": "Agent", "bpm": 108, "time_signature": "4/4"}}
```

## 3. Build one section per batch

Drums, bass and pad over 8 bars (32 beats) in A minor, the kick pumping the synths:

```json
{"method": "song.apply", "params": {"label": "Verse", "ops": [
  {"op": "set_instrument", "id": "drums", "type": "drums", "source": "808"},
  {"op": "set_instrument", "id": "bass", "type": "synth", "source": "synthwave-bass"},
  {"op": "set_instrument", "id": "pad", "type": "synth", "source": "synthwave-pad", "params": {"volume": 0.6}},

  {"op": "add_pattern", "id": "beat", "length": 4, "lanes": {
    "kick":       "x... x... x... x...",
    "snare":      ".... X... .... X...",
    "closed-hat": "..x. ..x. ..x. ..xo",
    "open-hat":   ".... .... .... ...x"}},
  {"op": "add_pattern", "id": "bassline", "length": 16, "notes": [
    [0, "A1", 0.5, 0.9], [0.5, "A2", 0.5, 0.6], [1, "A1", 0.5, 0.9], [1.5, "A2", 0.5, 0.6],
    [2, "A1", 0.5, 0.9], [2.5, "A2", 0.5, 0.6], [3, "A1", 0.5, 0.9], [3.5, "A2", 0.5, 0.6],
    [4, "F1", 0.5, 0.9], [4.5, "F2", 0.5, 0.6], [5, "F1", 0.5, 0.9], [5.5, "F2", 0.5, 0.6],
    [6, "F1", 0.5, 0.9], [6.5, "F2", 0.5, 0.6], [7, "F1", 0.5, 0.9], [7.5, "F2", 0.5, 0.6],
    [8, "C2", 0.5, 0.9], [8.5, "C3", 0.5, 0.6], [9, "C2", 0.5, 0.9], [9.5, "C3", 0.5, 0.6],
    [10, "C2", 0.5, 0.9], [10.5, "C3", 0.5, 0.6], [11, "C2", 0.5, 0.9], [11.5, "C3", 0.5, 0.6],
    [12, "G1", 0.5, 0.9], [12.5, "G2", 0.5, 0.6], [13, "G1", 0.5, 0.9], [13.5, "G2", 0.5, 0.6],
    [14, "G1", 0.5, 0.9], [14.5, "G2", 0.5, 0.6], [15, "G1", 0.5, 0.9], [15.5, "G2", 0.5, 0.6]]},
  {"op": "add_pattern", "id": "chords", "length": 16, "notes": [
    [0, "A3", 4, 0.6], [0, "C4", 4, 0.6], [0, "E4", 4, 0.6],
    [4, "F3", 4, 0.6], [4, "A3", 4, 0.6], [4, "C4", 4, 0.6],
    [8, "C3", 4, 0.6], [8, "E3", 4, 0.6], [8, "G3", 4, 0.6],
    [12, "G3", 4, 0.6], [12, "B3", 4, 0.6], [12, "D4", 4, 0.6]]},

  {"op": "set_bus", "name": "kick"},
  {"op": "set_bus", "name": "synths"},
  {"op": "set_bus", "name": "verb"},
  {"op": "set_effect", "bus": "verb", "type": "reverb", "id": "hall"},
  {"op": "set_send", "source": "synths", "target": "verb", "level": -14},
  {"op": "set_effect", "bus": "master", "type": "compressor", "id": "glue"},
  {"op": "set_effect", "bus": "master", "type": "limiter", "id": "limit"},

  {"op": "add_track", "id": "drums", "instrument": "drums", "bus": "kick"},
  {"op": "add_track", "id": "bass", "instrument": "bass", "bus": "synths", "volume": -4},
  {"op": "add_track", "id": "pad", "instrument": "pad", "bus": "synths", "volume": -10},

  {"op": "add_clip", "track": "drums", "source": "beat", "start": 0, "length": 32},
  {"op": "add_clip", "track": "bass", "source": "bassline", "start": 0, "length": 32},
  {"op": "add_clip", "track": "pad", "source": "chords", "start": 0, "length": 32, "fade_in": 2},

  {"op": "set_duck", "trigger": "kick", "target": "synths", "depth": -6, "attack": 0.005, "release": 0.18},
  {"op": "set_marker", "name": "Verse", "beat": 0},
  {"op": "set_automation", "track": "pad", "target": "param.cutoff", "points": [[0, 600], [32, 4000]]}
]}}
```

Automation targets: on a track `volume` (dB), `pan`, `param.<instrument parameter>` (names from `song.instruments`); on the song (no `track`) `bus.<bus>.volume`, `effect.<bus>.<id>.<property>`. If a parameter name is refused, look it up in the catalogue and retry; the failing batch changed nothing.

The next section is another `song.apply` with clips starting at beat 32 (reuse patterns, add new ones, a `set_marker` for "Chorus").

## 4. Mix by numbers

```json
{"method": "song.render", "params": {"end": 32, "tracks": true, "bins": 32}}
```
Read `integrated_lufs`, `true_peak_db`, `clipped_samples`, `envelope` for the mix and `tracks.<id>.*` for each track alone. Rules of thumb for synthwave: kick and bass carry the mix (their stems the loudest, bass 2-4 dB under the kick), pad 8-12 dB under the bass, lead 2-4 dB over the pad; no clipped samples before the master limiter; an envelope with no zero runs where music should play. Fix with `song.set_track {"id": "pad", "volume": -12}` or a batch, render again, repeat. `song.validate` must report `valid: true`.

## 5. Export

```json
{"method": "song.export", "params": {"file": "out/night.wav", "formats": ["wav", "flac", "mp3", "ogg", "opus"], "stems": true}}
```
Default normalisation: -14 LUFS integrated, -1 dBTP ceiling (`normalize`, `target`, `ceiling` change it). `formats` replaces the file's extension; each entry reports `bytes` or `error` (a format with no backend says what to install). Stems are `<name>.<track>.wav`, normalised with the mix's gain so they sum to it. `song.export_midi` writes the notes as MIDI; `song.import_midi` starts a song from a `.mid`.

## Importing a recording (0.10.2)

`song.import_audio` turns a recording (wav, flac, mp3, ogg, opus, m4a) into a NEW `.rsong` (refused when the file exists), saves it and opens it. The audio is resampled, aligned to its detected tempo and bars (silence in front so a bar starts at beat 0) and written to `audio/<name>.wav` beside the `.rsong`, played by one clip on the audio track `original`; sections become markers; the key is estimated. `transcribe: true` adds four MUTED estimate tracks on the bus `remake`: `drums` (808), `bass` (synthwave-bass), `melody` (synthwave-lead), `chords` (synthwave-pad). They are estimates; dense mixes transcribe worse than sparse ones.

```json
{"method": "song.import_audio", "params": {"file": "/home/me/track.flac", "path": "music/track.rsong", "transcribe": true}}
{"method": "song.get", "params": {"path": "music/track.rsong", "section": "tracks"}}
{"method": "song.set_track", "params": {"id": "drums", "mute": false}}
{"method": "song.export", "params": {"file": "out/track.wav", "formats": ["wav", "mp3"]}}
```
The reply: `path, audio_file, bpm, bpm_detected, confidence, alternative_bpm, first_beat, lead_in, duration, source_rate, key, sections, drum_hits, bass_notes, melody_notes, chords`. Other params: `bpm` (skip detection), `min_bpm` (70), `max_bpm` (180), `beats_per_bar` (4), `sample_rate` (48000), `sections` (true), `section_bars` (4), `title`, `artist`. If `confidence` < ~0.3 (the grid is a guess) or `alternative_bpm` is non-zero (half/double time is ambiguous), import again with `bpm` given, to a new `path`. Then edit as any song: unmute or fix the estimates, mute or cut `original`, render, export. Without the editor: `examples/song-import` (`--bpm`, `--transcribe`, `--no-sections`, `-o DIR`) and `examples/song-render`; C API in `engine.md`.

## Sound effects (Foley)

```json
{"method": "foley.layer_types"}
{"method": "foley.new", "params": {"path": "sfx/door.rfoley", "kind": "footstep-wood"}}
{"method": "foley.add_layer", "params": {"type": "tone", "params": {"freq": 180, "freq-end": 90, "at": 0.02}}}
{"method": "foley.set", "params": {"settings": {"duration": 0.8, "variation": 1}}}
{"method": "foley.render", "params": {"bins": 24}}
{"method": "foley.export", "params": {"file": "sfx/door.ogg"}}
```
Layer types: `body` (a resonant thump), `grain` (crackles and crunch), `wash` (filtered noise), `bed` (a whole-clip noise bed), `tone` (a swept oscillator), `noise` (a swept filter). `variation` gives another take of the same design (round robin). A song can play a patch as an instrument: `set_instrument {"type": "foley", "source": "sfx/door.rfoley"}`.

## lrgeditorctl equivalents

```sh
lrgeditorctl song instruments -o json
lrgeditorctl song new music/night.rsong empty=true
lrgeditorctl song apply "ops=$(cat verse.json)"
lrgeditorctl song render end=32 tracks=true -o json
lrgeditorctl song export out/night.wav 'formats=["wav","mp3"]' stems=true
lrgeditorctl song import-audio ~/music/track.flac music/track.rsong transcribe=true -o json
lrgeditorctl foley new sfx/door.rfoley footstep-wood
lrgeditorctl audio formats            # which export formats this build can write
lrgeditorctl asset new song name=night  # starter loop (808 + synthwave-bass), like Assets > New Song
```

In a game (C): load the `.rsong` with `lrg_song_new_from_file()` and play it with `LrgSongPlayer` (adaptive sections, layers, stingers), or render and encode it offline; see `engine.md`, "Music, Foley patches and audio export".
