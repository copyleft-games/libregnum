/* lrg-cookbook-scenes.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * The image-quality cookbook: small reference scenes that show which
 * libregnum calls produce a polished still image, and in which order.
 * Each scene is one function that renders a #GrlImage of any size; the
 * `cookbook` program writes them as PNGs, tests/test-cookbook.c checks
 * them, and docs/guides/image-cookbook.org explains them.
 */

#pragma once

#include <glib.h>
#include <libregnum.h>

G_BEGIN_DECLS

/**
 * LrgCookbookSceneFunc:
 * @width: the image width in pixels (16 or more)
 * @height: the image height in pixels (16 or more)
 * @out_layout: (out) (optional) (nullable) (transfer full): where to
 *   store the scene's #LrgFrameLayout, or %NULL; scenes that place no
 *   named elements store %NULL
 *
 * Renders one cookbook scene.  Scenes are deterministic: the same size
 * gives the same pixels.  Everything runs on the CPU; no window or GL
 * context is needed.
 *
 * Returns: (transfer full) (nullable): a new opaque RGBA8 #GrlImage of
 *   @width x @height, or %NULL when the size is too small
 */
typedef GrlImage *(*LrgCookbookSceneFunc) (guint             width,
                                           guint             height,
                                           LrgFrameLayout  **out_layout);

/**
 * LrgCookbookScene:
 * @name: the scene's command-line name, e.g. "glow-title"
 * @summary: one line saying what the scene shows
 * @func: the renderer
 * @has_layout: %TRUE when the scene returns an #LrgFrameLayout
 *
 * One entry of the scene table (lrg_cookbook_get_scenes()).
 */
typedef struct
{
	const gchar          *name;
	const gchar          *summary;
	LrgCookbookSceneFunc  func;
	gboolean              has_layout;
} LrgCookbookScene;

/**
 * lrg_cookbook_get_scenes:
 * @n_scenes: (out): the number of scenes
 *
 * Gets the table of the twelve cookbook scenes, in the guide's order.
 *
 * Returns: (transfer none) (array length=n_scenes): the scenes
 */
const LrgCookbookScene *
lrg_cookbook_get_scenes (guint *n_scenes);

/**
 * lrg_cookbook_get_pitfalls:
 * @n_pitfalls: (out): the number of pitfall renders
 *
 * Gets the table of the "pitfall" renders used by the guide: side by
 * side comparisons of a common mistake and its fix (moire from noise
 * that is not band-limited, thin dark detail lost to tone mapping).
 * They are not part of the twelve scenes.
 *
 * Returns: (transfer none) (array length=n_pitfalls): the pitfalls
 */
const LrgCookbookScene *
lrg_cookbook_get_pitfalls (guint *n_pitfalls);

/**
 * lrg_cookbook_find_scene:
 * @name: a scene or pitfall name
 *
 * Looks a scene (or a pitfall render) up by name.
 *
 * Returns: (transfer none) (nullable): the entry, or %NULL
 */
const LrgCookbookScene *
lrg_cookbook_find_scene (const gchar *name);

/**
 * lrg_cookbook_load_font:
 * @bold: %TRUE for a bold face
 *
 * Loads the TrueType font the scenes draw text with: the file named by
 * the LRG_COOKBOOK_FONT environment variable when it is set, otherwise
 * the first of a few common system fonts (DejaVu Sans, Liberation
 * Sans, Cantarell, Noto Sans) that exists.  Scenes fall back to the
 * built-in bitmap font when none is found.
 *
 * Returns: (transfer full) (nullable): a new #GrlImageFont, or %NULL
 */
GrlImageFont *
lrg_cookbook_load_font (gboolean bold);

/* ==========================================================================
 * The scenes
 * ========================================================================== */

/**
 * lrg_cookbook_aa_shapes:
 * @width: image width
 * @height: image height
 * @out_layout: (out) (optional) (nullable) (transfer full): always %NULL
 *
 * Translucent overlapping shapes, thin lines and a path, drawn with
 * nothing but the canvas primitives on a 3x supersampled
 * quality-preset canvas (lrg_image_canvas_new_supersampled(),
 * lrg_image_canvas_resolve()).
 *
 * Returns: (transfer full) (nullable): the image
 */
GrlImage *lrg_cookbook_aa_shapes (guint width, guint height, LrgFrameLayout **out_layout);

/**
 * lrg_cookbook_glow_title:
 * @width: image width
 * @height: image height
 * @out_layout: (out) (optional) (nullable) (transfer full): the layout
 *   (title, subtitle)
 *
 * A neon title: TrueType text on a supersampled canvas, then the CPU
 * post stack (bloom, ACES tonemap, vignette, grain) on the resolved
 * image (lrg_post_processor_apply_to_image()).
 *
 * Returns: (transfer full) (nullable): the image
 */
GrlImage *lrg_cookbook_glow_title (guint width, guint height, LrgFrameLayout **out_layout);

/**
 * lrg_cookbook_sdf_character_bust:
 * @width: image width
 * @height: image height
 * @out_layout: (out) (optional) (nullable) (transfer full): always %NULL
 *
 * A character bust built from 3D signed distance primitives with
 * smooth unions, raymarched into an #LrgHdrCanvas with soft shadow,
 * ambient occlusion and a rim light, ACES tone-mapped.
 *
 * Returns: (transfer full) (nullable): the image
 */
GrlImage *lrg_cookbook_sdf_character_bust (guint width, guint height, LrgFrameLayout **out_layout);

/**
 * lrg_cookbook_product_shot:
 * @width: image width
 * @height: image height
 * @out_layout: (out) (optional) (nullable) (transfer full): always %NULL
 *
 * A studio product shot: an SDF bottle on a floor, soft contact shadow,
 * 3x3 stratified samples per pixel, filmic tone map.
 *
 * Returns: (transfer full) (nullable): the image
 */
GrlImage *lrg_cookbook_product_shot (guint width, guint height, LrgFrameLayout **out_layout);

/**
 * lrg_cookbook_band_limited_texture:
 * @width: image width
 * @height: image height
 * @out_layout: (out) (optional) (nullable) (transfer full): always %NULL
 *
 * A marble floor receding to the horizon, textured with band-limited
 * fBm (lrg_noise_fbm2() with the pixel footprint), so the far floor
 * fades to its average instead of turning into moire.
 *
 * Returns: (transfer full) (nullable): the image
 */
GrlImage *lrg_cookbook_band_limited_texture (guint width, guint height, LrgFrameLayout **out_layout);

/**
 * lrg_cookbook_sky_gradient_vignette:
 * @width: image width
 * @height: image height
 * @out_layout: (out) (optional) (nullable) (transfer full): always %NULL
 *
 * A dusk sky in linear light with a bright sun and hills, bloomed on
 * the float buffer, ACES tone-mapped, then vignette and grain on the
 * 8-bit result.
 *
 * Returns: (transfer full) (nullable): the image
 */
GrlImage *lrg_cookbook_sky_gradient_vignette (guint width, guint height, LrgFrameLayout **out_layout);

/**
 * lrg_cookbook_speech_bubble_layout:
 * @width: image width
 * @height: image height
 * @out_layout: (out) (optional) (nullable) (transfer full): the layout
 *   (backdrop, title, character, bubble)
 *
 * A character, a speech bubble and a title placed with #LrgFrameLayout
 * against a safe area, validated, then drawn into their boxes with SDF
 * fills on a supersampled canvas.
 *
 * Returns: (transfer full) (nullable): the image
 */
GrlImage *lrg_cookbook_speech_bubble_layout (guint width, guint height, LrgFrameLayout **out_layout);

/**
 * lrg_cookbook_hdr_sunset:
 * @width: image width
 * @height: image height
 * @out_layout: (out) (optional) (nullable) (transfer full): the layout
 *   (one panel and one label per operator)
 *
 * One HDR sunset shaded once and tone-mapped three ways (clamp,
 * Reinhard, ACES) side by side.
 *
 * Returns: (transfer full) (nullable): the image
 */
GrlImage *lrg_cookbook_hdr_sunset (guint width, guint height, LrgFrameLayout **out_layout);

/**
 * lrg_cookbook_supersampled_line_art:
 * @width: image width
 * @height: image height
 * @out_layout: (out) (optional) (nullable) (transfer full): always %NULL
 *
 * Hairline geometry (a spirograph, concentric rings, a Bezier fan) on a
 * 4x supersampled canvas, resolved with Lanczos-3.
 *
 * Returns: (transfer full) (nullable): the image
 */
GrlImage *lrg_cookbook_supersampled_line_art (guint width, guint height, LrgFrameLayout **out_layout);

/**
 * lrg_cookbook_poster_typography:
 * @width: image width
 * @height: image height
 * @out_layout: (out) (optional) (nullable) (transfer full): the layout
 *   (backdrop, kicker, title, subtitle, art, footer)
 *
 * A typographic poster: every text block gets an #LrgFrameLayout box
 * inside a 6% safe area, its font size is fitted to the box, and the
 * layout is validated before drawing.
 *
 * Returns: (transfer full) (nullable): the image
 */
GrlImage *lrg_cookbook_poster_typography (guint width, guint height, LrgFrameLayout **out_layout);

/**
 * lrg_cookbook_noise_terrain:
 * @width: image width
 * @height: image height
 * @out_layout: (out) (optional) (nullable) (transfer full): always %NULL
 *
 * An fBm heightfield raymarched as a distance field, with soft shadows,
 * height- and slope-based materials, distance haze and a footprint
 * that grows with distance (so far ridges do not shimmer).
 *
 * Returns: (transfer full) (nullable): the image
 */
GrlImage *lrg_cookbook_noise_terrain (guint width, guint height, LrgFrameLayout **out_layout);

/**
 * lrg_cookbook_particles_glow:
 * @width: image width
 * @height: image height
 * @out_layout: (out) (optional) (nullable) (transfer full): always %NULL
 *
 * Thousands of additive light dots in an #LrgHdrCanvas: a far layer
 * softened with #LrgBlur, a near layer kept sharp, bloom on the float
 * buffer and an ACES tone map.
 *
 * Returns: (transfer full) (nullable): the image
 */
GrlImage *lrg_cookbook_particles_glow (guint width, guint height, LrgFrameLayout **out_layout);

/* ==========================================================================
 * Pitfalls (comparisons for the guide)
 * ========================================================================== */

/**
 * lrg_cookbook_pitfall_moire:
 * @width: image width
 * @height: image height
 * @out_layout: (out) (optional) (nullable) (transfer full): always %NULL
 *
 * The band-limited floor twice: left with a footprint of 0 (aliasing
 * into moire near the horizon), right with the pixel footprint.
 *
 * Returns: (transfer full) (nullable): the image
 */
GrlImage *lrg_cookbook_pitfall_moire (guint width, guint height, LrgFrameLayout **out_layout);

/**
 * lrg_cookbook_pitfall_thin_detail:
 * @width: image width
 * @height: image height
 * @out_layout: (out) (optional) (nullable) (transfer full): always %NULL
 *
 * A face with a thin dark mustache in front of a bright backlight,
 * three ways: ACES at +1.5 stops (the mustache vanishes), Reinhard at
 * 0 stops (it survives), and the detail drawn after the tone map on
 * the 8-bit image (it is exact).
 *
 * Returns: (transfer full) (nullable): the image
 */
GrlImage *lrg_cookbook_pitfall_thin_detail (guint width, guint height, LrgFrameLayout **out_layout);

G_END_DECLS
