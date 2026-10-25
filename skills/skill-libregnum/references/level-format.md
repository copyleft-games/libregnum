# Levels, prefabs and projects on disk

Full references: `docs/editor/yaml-format.org`, `docs/editor/level-model.org`, `docs/editor/projects-and-assets.org`.

## Project layout

```text
mygame/
├── project.ryaml        # manifest
├── levels/main.rlevel   # levels (YAML)
├── prefabs/*.rprefab    # reusable node subtrees (same format, type: prefab)
├── scripts/             # lua, py, js (gjs), c (crispy), el
├── assets/              # textures, models (glTF/OBJ), audio, fonts
└── .lrg-editor/         # editor state: autosave/ (not part of the game)
```

`lrgeditor --new-project DIR --name NAME` creates this with a starter level (Sun light, Camera, Ground plane). `project.ryaml` keys (under a top-level `project:` mapping): `name`, `default_level`, `game_output` (the game module `.so`), `run_command` (template with `{project}` `{level}` `{module}`; default `lrgldr {module} -- --level {level}`), `scripts_dir`, `asset_dirs`.

```yaml
project:
  name: Keep
  default_level: levels/main.rlevel
  game_output: build/libkeep.so
  asset_dirs: [assets, levels, prefabs, scripts]
```

## .rlevel

```yaml
type: level
name: arena
default_view: 3d          # or 2d
nodes:
  - name: Floor
    guid: a1b2c3d4-e5f6-7890-abcd-ef1234567890   # stable id, generated
    visible: true
    locked: false
    is_2d: false
    transform:
      location: [0.0, 0.0, 0.0]
      rotation: [0.0, 0.0, 0.0]      # radians, Euler XYZ
      scale: [20.0, 1.0, 20.0]
    visual:
      kind: primitive                # none primitive mesh-asset sprite tilemap light camera audio-emitter prefab-instance cad-part
      primitive: plane               # cube uv-sphere ico-sphere plane cylinder cone torus circle grid rectangle-2d circle-2d
      material:
        color: [0.35, 0.45, 0.35, 1.0]
        roughness: 0.5
        metallic: 0.0
    components:
      - type: health                 # registered component type
        props: { max_hp: 100 }
    scripts:
      - language: lua
        script: scripts/floor.lua
        enabled: true
    children: []
```

- Transforms are local to the parent; the implicit root is not serialized.
- Primitives are unit-sized (1 m); `scale` sizes them.
- Hand edits are fine: the editor reloads a level changed on disk when it has no unsaved changes, and offers Reload / Keep mine otherwise. `libregnum.el` gives these files `libregnum-rlevel-mode` in Emacs.
- `lrgeditor --validate FILE.rlevel` checks a level without a window (exit 1 on errors). Problem codes: missing-guid, duplicate-guid, duplicate-name, unnamed, slash-in-name, non-finite-transform, zero-scale, missing-asset-ref, missing-asset, component-no-type, unknown-component, script-no-path, script-no-language, script-disabled, missing-script.
- Blender scenes exported with the scene YAML serializer import with File > Import Scene or `lrgeditorctl call level.import path=FILE`.

## Runtime

`lrg_level_instantiate()` turns a level into an `LrgWorld` of `LrgGameObject`s (components via the engine registry, scripts as `LrgScriptComponent`, each object tagged with its node GUID); `lrg_level_to_scene()` bakes renderable nodes into an `LrgScene`. Games are shared modules run by `lrgldr MODULE.so -- ARGS`; `lrgeditor --module MODULE.so` loads one so its component types can be edited and played in the editor.
