# Image quality: polished still images on the CPU (0.14)

The full guide is `docs/guides/image-cookbook.org`; the twelve reference scenes are `examples/cookbook/lrg-cookbook-scenes.c` (one function each, checked by `tests/test-cookbook.c`). This page is what an agent needs to generate a picture with libregnum that looks finished on the first try: smooth edges, soft light, no overlaps, no hand-written shading beyond the scene itself.

## Never

- `lrg_image_canvas_new()` or bare `grl_image_draw_*` for a picture. Their default is `GRL_IMAGE_BLEND_REPLACE` (translucent shapes punch holes), gamma-space mixing and aliased edges.
- `grl_image_resize()` to downsample a supersampled image (filters sRGB bytes, darkens detail). Use `lrg_image_canvas_resolve()` / `lrg_image_downsample()`.
- Noise without a footprint (`footprint = 0`) on anything that recedes or is zoomed out: moire.
- Guessed pixel coordinates for titles, bubbles and characters. Use `LrgFrameLayout` and validate.
- Thin dark detail (outlines, mustaches, small text) inside HDR shading that is tone-mapped at high exposure: it vanishes. Draw it after the tone map.

## Recipe A: 8-bit drawing (shapes, line art, text, posters)

```c
g_autoptr(LrgImageCanvas) canvas = lrg_image_canvas_new_supersampled (w, h, 3, &bg); /* 4 for hairlines */
gint f = (gint) lrg_image_canvas_get_supersample (canvas);

/* gradients and text ignore the transform: large pixels (x f) */
lrg_image_canvas_draw_gradient_rect (canvas, 0, 0, w * f, h * f, &top, &bottom, GRL_GRADIENT_AXIS_VERTICAL);
lrg_image_canvas_draw_text (canvas, font, "Title", x * f, y * f, size * f, &ink);

/* geometry: one scale, then final-size coordinates; thickness 1 = 1 final pixel */
lrg_image_canvas_scale (canvas, (gfloat) f, (gfloat) f);
lrg_image_canvas_fill_circle (canvas, cx, cy, r, &translucent);
lrg_image_canvas_fill_path (canvas, path, GRL_FILL_RULE_NONZERO, &c);
lrg_image_canvas_draw_line (canvas, x1, y1, x2, y2, 1, &c);

/* custom smooth shapes: fill_sdf_rect gets LARGE pixels; divide p by f, return d * f */
lrg_image_canvas_fill_sdf_rect (canvas, bx * f, by * f, bw * f, bh * f, my_sdf, &shape, &c);

image = lrg_image_canvas_resolve (canvas, LRG_RESAMPLE_FILTER_LANCZOS3); /* MITCHELL for soft content */
lrg_post_processor_apply_to_image (post, image, 0.0f, &error);        /* optional finish */
```

Feathered (soft) SDF fills: return `d / feather` from the field; `feather = 1` is a crisp edge, 20 a 20-pixel glow.

## Recipe B: HDR shading (glows, suns, lit 3D shapes, textures)

```c
g_autoptr(LrgHdrCanvas) hdr = lrg_hdr_canvas_new (w, h);

lrg_hdr_canvas_shade (hdr, shade_fn, &scene, 2);   /* 2x2 stratified; 3 for product shots; 1 while iterating */
/* optional: bloom / blur on the linear floats (opaque canvas: premultiplied == straight) */
lrg_post_processor_apply_to_buffer (post, lrg_hdr_canvas_get_data (hdr, NULL), w, h, 0.0f, &error);
image = lrg_hdr_canvas_to_image (hdr, LRG_TONEMAP_OPERATOR_ACES, 0.0f, TRUE); /* exposure in stops, dither on */
/* optional: vignette + grain on the 8-bit image; labels via lrg_image_canvas_new_for_image + set_quality */
```

Inside `shade_fn (x, y, out_rgba, data)`: return linear radiance with `out_rgba[3] = 1`. Values above 1 are normal (sun disc 30..60).

- 2D: `lrg_sdf2_*` shapes, `lrg_sdf2_coverage (d, 1.0f)` for edges, mix layers by coverage.
- 3D: camera ray -> `lrg_sdf3_raymarch (field, data, eye, rd, tmax, 200, &t)` -> `lrg_sdf3_normal (field, data, p, 1e-3f, n)` -> key `ndl * lrg_sdf3_soft_shadow (field, data, p + n * 0.01, L, 0.02, 4.0, 8.0)` + fill `sky * lrg_sdf3_ambient_occlusion (field, data, p, n)` + rim `pow (1 - dot (n, -rd), 3)` + Blinn highlight + fog.
- Build shapes with `lrg_sdf3_ellipsoid/capsule/round_cone/round_box/sphere/torus`, join with `lrg_sdf_smooth_union (a, b, k)` (k = blend radius), carve with `lrg_sdf_smooth_subtract (keep, cut, k)`. Material = the nearest part.
- Multiply non-exact fields by 0.75..0.8 (ellipsoids, blends, displacement), 0.5 for heightfields. No bounding-volume shortcut that jumps at a threshold.
- Textures: `lrg_noise_fbm2/fbm3 (..., octaves, 2.0, 0.5, footprint, seed)`, `footprint = frequency * distance * pixel_angle` (divide by `|rd.y|` on a grazing plane). Hand-made periodic patterns: `contrast *= 1 - smoothstep (0.25, 0.5, cycles * footprint)`.

## Tone map choice

| Operator | Use |
|----------|-----|
| `ACES` | default: contrast, soft highlight roll-off; desaturates the brightest colours |
| `FILMIC` | product/studio shots (white point 4) |
| `REINHARD` | when mid-tones and dark detail matter more than punch |
| `NONE` | values already display-referred; clamps everything above 1 |

Exposure: start at 0 EV; lower it (-0.5..-1.5) when bright surfaces carry detail.

## Layout: boxes first, pixels second

```c
layout = lrg_frame_layout_new (w, h);
lrg_frame_layout_set_safe_area_percent (layout, 0.05f);
lrg_frame_layout_set_box_rect (layout, "backdrop", &full);            /* full bleed */
lrg_frame_layout_set_backdrop (layout, "backdrop", TRUE);
lrg_frame_layout_add_box (layout, "title", LRG_HUD_ANCHOR_TOP, 0, 0, w * 0.7f, h * 0.13f);
lrg_frame_layout_add_box (layout, "character", LRG_HUD_ANCHOR_BOTTOM_LEFT, w * 0.04f, 0, w * 0.3f, h * 0.6f);
lrg_frame_layout_add_box_relative (layout, "bubble", "character", LRG_FRAME_SIDE_RIGHT,
                                   w * 0.03f, w * 0.5f, h * 0.36f, LRG_FRAME_ALIGN_START);
if (!lrg_frame_layout_validate (layout, &error)) g_printerr ("%s\n", error->message);
lrg_frame_layout_get_box (layout, "bubble", &box);   /* draw inside it; keep tails inside too */
lrg_frame_layout_draw_debug (layout, canvas);         /* while iterating */
```

Fit text: measure at 100 px (`grl_image_measure_text_ttf`), `size = min (100 * box_w / w, 100 * box_h / h)`.
Intended overlaps (badge on a card): `lrg_frame_layout_allow_overlap (layout, "a", "b")`.

## Checklist before showing an image

- [ ] Canvas is `new_quality` / `new_supersampled`, or the image came from `LrgHdrCanvas` + `to_image (..., dither TRUE)`.
- [ ] Every element has a layout box; `validate` passes.
- [ ] No noise call with footprint 0 on receding or minified surfaces.
- [ ] Tone map and exposure picked on purpose; thin dark detail drawn after it.
- [ ] Opaque output (no alpha < 255 unless intended); thousands of distinct colours (a flat aliased render has a few dozen).
- [ ] A GTest renders it at 256x160, counts colours, asserts opacity and `g_assert_no_error` on `validate` (copy `tests/test-cookbook.c`).

## Running and checking

```bash
make -j8 && make -C examples build/release/examples/cookbook
examples/build/release/examples/cookbook --list
examples/build/release/examples/cookbook --scene sdf-character-bust --size 1280x800 --out shots
examples/build/release/examples/cookbook --size 320x200 --sheet shots/sheet.png   # contact sheet: look at it
make -C tests build/release/tests/test-cookbook && env -u DISPLAY -u WAYLAND_DISPLAY tests/build/release/tests/test-cookbook
```

Standalone references: `examples/image-quality-showcase.c` (defaults vs quality side by side), `examples/sdf-portrait.c` (detailed raymarched bust), `examples/frame-layout-poster.c` (`--debug-boxes`, `--broken`).

## Headless GPU

CPU paths (`LrgImageCanvas`, `LrgHdrCanvas`, SDF, noise, `apply_to_image`/`apply_to_buffer`, `LrgFrameLayout`) need no display at all. GLSL post shaders and render textures need a GL context: run them through `build-aux/xrun.sh` (private Xvfb, or Xwayland on a headless compositor; Mesa llvmpipe when there is no GPU). Never on the user's session.

## Cost

`shade` calls the function `w * h * samples^2` times; a raymarched sample calls the field ~100 times. 640x400 at 2x2: 3..6 s for a raymarched scene, < 1 s for 2D shading, ~0.1 s for 8-bit drawing. Iterate at samples 1 and quarter size.
