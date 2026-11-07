# Data formats (YAML)

## Composite documents (LrgDataLoader, 0.9.0): base of .rmat/.ranim/.rbt/.rnav/.rparticles/.rterrain/.rmixer

- Load: `lrg_data_loader_load_composite_file (loader, path, ROOT_TYPE or G_TYPE_INVALID, &err)`; `_data (text, len, root_type, source, &err)`. Save: `lrg_data_loader_save_to_file` / `_to_data`.
- Object property (GObject class/interface) = nested mapping; `type: <registry name>` is optional when the property type is concrete.
- GListModel property, or GPtrArray with `lrg_data_loader_set_param_item_type (pspec, T)` in class_init = sequence of mappings.
- LrgDataContainer objects take `children:` (loader property `children-key`); `get_child_type()` = default child type.
- `{ref: x}` anywhere an object goes, resolved after the build (forward refs OK): `#` root, `#/a/0` JSON pointer, a unique guid/id in the document, else the resolver (`lrg_data_loader_set_resolver`). Resolved objects are tagged (`lrg_data_loader_get_object_ref`) and saved back as `ref:`.
- Strict by default (`lrg_data_loader_set_strict`): one GError lists every problem as `file:line:col: /path: message`.
- Reserved keys: `type`, `ref`, `children` (on containers). Saving writes `type:` only where not implied (root always), skips values equal to a fresh `g_object_new (type)`, writes floats in shortest exact form; output is byte-stable on reload.
- Traps: GPtrArray without an item-type hint is ignored; duplicate ids make ref-by-id ambiguous; construct-only props cannot hold refs/lists; types written with `type:` must be registered.

## Engine formats with writers (all round-trip byte-stable)

| Format | Load | Save | Notes |
|---|---|---|---|
| locale | `lrg_locale_new_from_file` / `_from_data` | `lrg_locale_save_to_file` / `_data` | code, name, plural_rule (nick, optional), strings (text or plural-form map); a nested map is always plural forms |
| dialog tree | `lrg_dialog_tree_new_from_file` / `_from_data` | `lrg_dialog_tree_save_to_file` / `_data` | id, title, description, start, nodes; a node without next/responses auto-chains, `next: null` = terminal |
| sound bank | `lrg_sound_bank_new_from_file` / `_from_data (…, base_dir, manifest_only)` | `lrg_sound_bank_save_to_file` / `_data` | name, base_path, volume, sounds; manifest_only = no audio device/files needed |
| input map | `lrg_input_map_load_from_file` / `_from_data` | `lrg_input_map_save_to_file` / `_data` | actions keep order; unknown root/action/binding keys kept |
| HUD layout | `lrg_hud_layout_load_from_file` / `_from_data` | `lrg_hud_layout_save_to_file` / `_data` | version, pieces (sorted) |

Editor data editors validate with these from-data loaders (no temp files) but save through the document model (keeps comments). Guide: docs/guides/writing-a-data-format.org.

## Post stacks (.rpost, 0.9.0): LrgPostStack
- `version: 1`, `name`, `quality: low|medium|high|ultra`, `hdr: true`, `effects:` (ordered list). Entry: `type:` (bloom, tonemap, vignette, color-grade, fxaa, film-grain, screen-shake, heat-haze, colorblind-filter, sharpen, palettize, or a registered/GType name), `enabled`, `weight` (0..1 = base intensity), `priority` (default index*10), `params:` = effect GObject properties (kebab or snake case).
- `[r, g, b]` expands to `name-r/g/b` (bloom `tint`, vignette `color`, color-grade `lift`/`gamma`/`gain`). Special: palettize `palette: ["#rrggbb", ...]`; color-grade `lut: file.cube` (relative to the .rpost) + `lut_strength`.
- Load `lrg_post_stack_new_from_file` / `_from_data (text, len, base_dir, &err)`; save `lrg_post_stack_save_to_file` / `_to_data` (byte-stable; unknown top-level and per-effect keys kept). Strict: unknown effect type / param, bad value, version > 1 are errors (LRG_POST_STACK_ERROR_*). Not an LrgDataLoader document (own reader: per-effect param validation).
- `lrg_post_stack_build (stack, processor, &err)` reuses same-type effects in order, resets unset params to defaults (except screen-shake trauma), removes unlisted effects: cheap per frame. `lrg_post_stack_blend (a, b, t)` for post volumes. Custom effects: `lrg_post_stack_register_effect_type ("my-sepia", TYPE)`.
- Order: bloom (wants HDR) -> tonemap -> grade/colorblind/sharpen/fxaa/palettize/film-grain/vignette. Docs: docs/modules/postprocess/post-stack.org.

## Model `.meta` LOD keys (0.9.0)
- Under `settings:` of `importer: model`: `lod-levels` (1..8, 1 = none), `lod-ratios` (list for levels 1.., default 0.5^i), `lod-screen-sizes` (list for levels 0.., default 0.5^(i+1) with last 0; a positive last value culls), `lod-max-error` (0.05), `lod-lock-border` (true), `lod-hysteresis` (0.1), `lod-fade-duration` (0.25 s).
- Lists may be `"0.5, 0.25"` or `[0.5, 0.25]`; `asset.meta.set` / `lrgeditorctl meta set` validate them.
- `lrg_asset_manager_load_model` ignores these keys; `LrgLodComponent` and `lrg_lod_group_new_from_model_with_meta` read them.
