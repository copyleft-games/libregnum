/* lrg-lod-group.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgLodGroup: a level-of-detail chain for one object. Each level is a
 * model (or several single-mesh models) shown while the object's screen
 * size stays at or above the level's threshold. Selection applies a bias,
 * a hysteresis band and an optional forced level; LrgLodState carries the
 * per-view selection and the dithered cross-fade between two levels. The
 * chain can be authored by hand or generated from a model with
 * LrgMeshSimplify, optionally driven by the model's `.meta` LOD keys.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <graylib.h>
#include "../lrg-version.h"
#include "../core/lrg-asset-meta.h"
#include "lrg-mesh-simplify.h"

G_BEGIN_DECLS

/**
 * LRG_LOD_MAX_LEVELS:
 *
 * Most levels an #LrgLodGroup holds (the original included).
 */
#define LRG_LOD_MAX_LEVELS (8)

/**
 * LRG_LOD_LEVEL_CULLED:
 *
 * Level index meaning "draw nothing": the object is smaller than the last
 * level's screen-size threshold, or the group is empty.
 */
#define LRG_LOD_LEVEL_CULLED (-1)

/**
 * LRG_LOD_LEVEL_NONE:
 *
 * "No level selected yet": pass it as the current level of
 * lrg_lod_group_select_level() to skip the hysteresis.
 */
#define LRG_LOD_LEVEL_NONE (-2)

/**
 * LRG_LOD_FADE_UNIFORM:
 *
 * Name of the float uniform the cross-fade GLSL reads (see
 * lrg_lod_get_glsl()).
 */
#define LRG_LOD_FADE_UNIFORM "lodFade"

/* ==========================================================================
 * LrgLodState
 * ========================================================================== */

typedef struct _LrgLodState LrgLodState;

/**
 * LrgLodState:
 * @level: the selected level, or %LRG_LOD_LEVEL_CULLED
 * @previous_level: the level fading out while @fade < 1 (may be
 *   %LRG_LOD_LEVEL_CULLED when fading in from nothing)
 * @fade: cross-fade progress, 0 (just switched) .. 1 (done)
 * @screen_size: the screen size of the last update, after bias
 * @initialized: %FALSE until the first lrg_lod_group_update_state()
 *
 * Per-view LOD selection, updated by lrg_lod_group_update_state(). Keep
 * one per object and camera (selection is stateful because of the
 * hysteresis and the fade).
 */
struct _LrgLodState
{
	gint     level;
	gint     previous_level;
	gfloat   fade;
	gfloat   screen_size;
	gboolean initialized;
};

LRG_AVAILABLE_IN_ALL
GType lrg_lod_state_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_LOD_STATE (lrg_lod_state_get_type ())

/**
 * lrg_lod_state_new:
 *
 * Returns: (transfer full): a new, uninitialised #LrgLodState
 */
LRG_AVAILABLE_IN_ALL
LrgLodState * lrg_lod_state_new (void);

/**
 * lrg_lod_state_copy:
 * @state: an #LrgLodState
 *
 * Returns: (transfer full): a copy of @state
 */
LRG_AVAILABLE_IN_ALL
LrgLodState * lrg_lod_state_copy (const LrgLodState *state);

/**
 * lrg_lod_state_free:
 * @state: (nullable): an #LrgLodState
 *
 * Frees a state from lrg_lod_state_new() or lrg_lod_state_copy().
 */
LRG_AVAILABLE_IN_ALL
void lrg_lod_state_free (LrgLodState *state);

/**
 * lrg_lod_state_init:
 * @state: (out caller-allocates): a state to reset
 *
 * Resets @state to "never updated" (stack or embedded states).
 */
LRG_AVAILABLE_IN_ALL
void lrg_lod_state_init (LrgLodState *state);

/**
 * lrg_lod_state_is_fading:
 * @state: an #LrgLodState
 *
 * Returns: %TRUE while two levels cross-fade
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_lod_state_is_fading (const LrgLodState *state);

/**
 * lrg_lod_state_get_level_fade:
 * @state: an #LrgLodState
 * @level: a level index
 * @out_fade: (out) (optional): the `lodFade` value to draw @level with:
 *   0 = fully visible, 0 < f < 1 = fading in (pixels whose dither value
 *   is below f are drawn), -1 < f < 0 = fading out (pixels whose dither
 *   value is at least -f are drawn); the two values of a cross-fade are
 *   exact complements, so every pixel is drawn exactly once
 *
 * Tells whether @level is drawn this frame and with which fade.
 *
 * Returns: %TRUE if @level is visible
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_lod_state_get_level_fade (const LrgLodState *state,
                                       gint               level,
                                       gfloat            *out_fade);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (LrgLodState, lrg_lod_state_free)

/* ==========================================================================
 * Screen size, global bias and the shader side
 * ========================================================================== */

/**
 * lrg_lod_compute_screen_size:
 * @radius: bounding-sphere radius in world units
 * @distance: distance from the camera to the sphere centre
 * @fovy: vertical field of view in degrees (perspective)
 *
 * The fraction of the screen height the sphere's diameter covers:
 * radius / (distance * tan (fovy / 2)). Camera inside the sphere gives a
 * large value.
 *
 * Returns: the screen size (1 = as tall as the screen)
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_lod_compute_screen_size (gfloat radius,
                                    gfloat distance,
                                    gfloat fovy);

/**
 * lrg_lod_screen_size_from_view_projection:
 * @view_projection: (array fixed-size=16): column-major view-projection
 *   matrix (perspective or orthographic, as lrg_render_list_get_view_projection()
 *   returns)
 * @center: (array fixed-size=3): world-space sphere centre
 * @radius: world-space sphere radius
 *
 * Screen size from a view-projection matrix: radius times the projection's
 * vertical scale over the clip w of the centre. Works for orthographic
 * cameras too (w = 1).
 *
 * Returns: the screen size
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_lod_screen_size_from_view_projection (const gfloat *view_projection,
                                                 const gfloat *center,
                                                 gfloat        radius);

/**
 * lrg_lod_get_global_bias:
 *
 * Returns: the bias every group's screen size is multiplied by
 *   (default 1)
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_lod_get_global_bias (void);

/**
 * lrg_lod_set_global_bias:
 * @bias: multiplier (> 0); above 1 keeps detailed levels longer, below 1
 *   switches to coarse levels earlier (a quality setting)
 *
 * Sets the process-wide LOD bias.
 */
LRG_AVAILABLE_IN_ALL
void lrg_lod_set_global_bias (gfloat bias);

/**
 * lrg_lod_get_glsl:
 *
 * GLSL (330) for the dithered cross-fade, to paste into a fragment
 * shader before `main`: `uniform float lodFade;` and
 * `void lrg_lod_fade_discard ()`, which discards the pixels a fading
 * level must not draw (interleaved gradient noise on gl_FragCoord).
 * Call `lrg_lod_fade_discard ();` first thing in `main`. With lodFade = 0
 * (the default value of an unset uniform) nothing is discarded.
 *
 * Returns: (transfer none): the GLSL source
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_lod_get_glsl (void);

/**
 * lrg_lod_bind_fade:
 * @shader_id: a linked GL program id
 * @fade: the `lodFade` value (see lrg_lod_state_get_level_fade())
 *
 * Sets the %LRG_LOD_FADE_UNIFORM uniform of @shader_id (locations are
 * cached per program; programs without the uniform are skipped). The
 * renderer calls it before each LOD draw and with 0 for everything else.
 * Needs a graphics context.
 */
LRG_AVAILABLE_IN_ALL
void lrg_lod_bind_fade (guint  shader_id,
                        gfloat fade);

/* ==========================================================================
 * LrgLodGroup
 * ========================================================================== */

#define LRG_TYPE_LOD_GROUP (lrg_lod_group_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgLodGroup, lrg_lod_group, LRG, LOD_GROUP, GObject)

/**
 * lrg_lod_group_new:
 *
 * Creates an empty group: hysteresis 0.1, bias 1, no forced level, fade
 * 0.25 s.
 *
 * Returns: (transfer full): a new #LrgLodGroup
 */
LRG_AVAILABLE_IN_ALL
LrgLodGroup * lrg_lod_group_new (void);

/**
 * lrg_lod_group_new_from_model:
 * @model: the full-detail model (level 0; its meshes need CPU data)
 * @simplify: (nullable): the simplifier settings (error limit, locks), or
 *   %NULL for target error 0.05 with locked borders
 * @n_levels: level count including @model (1 .. %LRG_LOD_MAX_LEVELS)
 * @ratios: (array length=n_ratios) (nullable): triangle ratio of levels
 *   1, 2, ...; missing entries default to 0.5 to the power of the level
 * @n_ratios: entries in @ratios
 * @screen_sizes: (array length=n_screen_sizes) (nullable): threshold of
 *   levels 0, 1, ...; missing entries default to 0.5^(level + 1), except
 *   the last level which defaults to 0 (never culled)
 * @n_screen_sizes: entries in @screen_sizes
 * @error: return location for a #GError
 *
 * Generates a LOD chain: level 0 is @model itself, every other level is
 * @model simplified from the original to its ratio, as one single-mesh
 * #GrlModel per source mesh with the source material and transform
 * (textures and shaders are borrowed: @model stays referenced by the
 * group). Needs a graphics context (the levels are uploaded).
 *
 * Returns: (transfer full) (nullable): the group, or %NULL with @error
 */
LRG_AVAILABLE_IN_ALL
LrgLodGroup * lrg_lod_group_new_from_model (GrlModel         *model,
                                            LrgMeshSimplify  *simplify,
                                            guint             n_levels,
                                            const gfloat     *ratios,
                                            guint             n_ratios,
                                            const gfloat     *screen_sizes,
                                            guint             n_screen_sizes,
                                            GError          **error);

/**
 * lrg_lod_group_new_from_model_with_meta:
 * @model: the full-detail model
 * @meta: (nullable): the model's `.meta` sidecar; %NULL or `lod-levels: 1`
 *   gives a single-level group
 * @error: return location for a #GError
 *
 * lrg_lod_group_new_from_model() configured by the sidecar's `lod-*`
 * import keys: `lod-levels`, `lod-ratios`, `lod-screen-sizes`,
 * `lod-max-error`, `lod-lock-border`, `lod-hysteresis` and
 * `lod-fade-duration`.
 *
 * Returns: (transfer full) (nullable): the group, or %NULL with @error
 */
LRG_AVAILABLE_IN_ALL
LrgLodGroup * lrg_lod_group_new_from_model_with_meta (GrlModel      *model,
                                                      LrgAssetMeta  *meta,
                                                      GError       **error);

/**
 * lrg_lod_group_apply_meta:
 * @self: an #LrgLodGroup
 * @meta: a model sidecar
 *
 * Copies the selection settings of @meta (`lod-screen-sizes`,
 * `lod-hysteresis`, `lod-fade-duration`) onto existing levels without
 * regenerating them (the editor's import settings use it for live
 * preview of thresholds).
 */
LRG_AVAILABLE_IN_ALL
void lrg_lod_group_apply_meta (LrgLodGroup  *self,
                               LrgAssetMeta *meta);

/* --------------------------------------------------------------------------
 * Levels
 * -------------------------------------------------------------------------- */

/**
 * lrg_lod_group_add_level:
 * @self: an #LrgLodGroup
 * @model: (nullable): the model of the level; %NULL draws nothing (an
 *   explicit "culled" level, or a placeholder in headless tests)
 * @screen_size: smallest screen size at which the level is still used
 *
 * Appends a level (coarser than the previous ones). The first non-%NULL
 * model also sets the group bounds unless lrg_lod_group_set_bounds() was
 * called.
 *
 * Returns: the new level index, or -1 when %LRG_LOD_MAX_LEVELS is reached
 */
LRG_AVAILABLE_IN_ALL
gint lrg_lod_group_add_level (LrgLodGroup *self,
                              GrlModel    *model,
                              gfloat       screen_size);

/**
 * lrg_lod_group_add_level_models:
 * @self: an #LrgLodGroup
 * @models: (element-type GrlModel): the models drawn together for the
 *   level (one render item each)
 * @screen_size: smallest screen size at which the level is still used
 *
 * Like lrg_lod_group_add_level() for a level made of several models (a
 * simplified multi-material model is one model per material).
 *
 * Returns: the new level index, or -1 when full
 */
LRG_AVAILABLE_IN_ALL
gint lrg_lod_group_add_level_models (LrgLodGroup *self,
                                     GPtrArray   *models,
                                     gfloat       screen_size);

/**
 * lrg_lod_group_remove_all_levels:
 * @self: an #LrgLodGroup
 *
 * Drops every level (the bounds stay).
 */
LRG_AVAILABLE_IN_ALL
void lrg_lod_group_remove_all_levels (LrgLodGroup *self);

/**
 * lrg_lod_group_get_n_levels:
 * @self: an #LrgLodGroup
 *
 * Returns: the number of levels
 */
LRG_AVAILABLE_IN_ALL
guint lrg_lod_group_get_n_levels (LrgLodGroup *self);

/**
 * lrg_lod_group_get_level_model:
 * @self: an #LrgLodGroup
 * @level: a level index
 *
 * Returns: (transfer none) (nullable): the first model of @level
 */
LRG_AVAILABLE_IN_ALL
GrlModel * lrg_lod_group_get_level_model (LrgLodGroup *self,
                                          guint        level);

/**
 * lrg_lod_group_get_level_models:
 * @self: an #LrgLodGroup
 * @level: a level index
 *
 * Returns: (transfer none) (element-type GrlModel) (nullable): every
 *   model of @level (empty for a %NULL level), or %NULL when out of range
 */
LRG_AVAILABLE_IN_ALL
GPtrArray * lrg_lod_group_get_level_models (LrgLodGroup *self,
                                            guint        level);

/**
 * lrg_lod_group_find_level_of_model:
 * @self: an #LrgLodGroup
 * @model: a model
 *
 * Returns: the level @model belongs to, or -1
 */
LRG_AVAILABLE_IN_ALL
gint lrg_lod_group_find_level_of_model (LrgLodGroup *self,
                                        GrlModel    *model);

/**
 * lrg_lod_group_get_level_screen_size:
 * @self: an #LrgLodGroup
 * @level: a level index
 *
 * Returns: the level's screen-size threshold
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_lod_group_get_level_screen_size (LrgLodGroup *self,
                                            guint        level);

/**
 * lrg_lod_group_set_level_screen_size:
 * @self: an #LrgLodGroup
 * @level: a level index
 * @screen_size: smallest screen size at which the level is still used
 *   (>= 0; keep thresholds decreasing with the level)
 *
 * Changes a threshold. Emits #LrgLodGroup::changed.
 */
LRG_AVAILABLE_IN_ALL
void lrg_lod_group_set_level_screen_size (LrgLodGroup *self,
                                          guint        level,
                                          gfloat       screen_size);

/**
 * lrg_lod_group_get_level_triangle_count:
 * @self: an #LrgLodGroup
 * @level: a level index
 *
 * Returns: the triangles of every mesh of the level's models
 */
LRG_AVAILABLE_IN_ALL
gint lrg_lod_group_get_level_triangle_count (LrgLodGroup *self,
                                            guint        level);

/**
 * lrg_lod_group_get_level_error:
 * @self: an #LrgLodGroup
 * @level: a level index
 *
 * Returns: the relative simplification error of a generated level (0 for
 *   level 0 and hand-made levels)
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_lod_group_get_level_error (LrgLodGroup *self,
                                      guint        level);

/* --------------------------------------------------------------------------
 * Bounds
 * -------------------------------------------------------------------------- */

/**
 * lrg_lod_group_set_bounds:
 * @self: an #LrgLodGroup
 * @min: (array fixed-size=3): local-space box minimum
 * @max: (array fixed-size=3): local-space box maximum
 *
 * Sets the local bounds whose sphere drives the screen size (and the
 * component's culling bounds). Overrides the bounds taken from level 0.
 */
LRG_AVAILABLE_IN_ALL
void lrg_lod_group_set_bounds (LrgLodGroup  *self,
                               const gfloat *min,
                               const gfloat *max);

/**
 * lrg_lod_group_get_bounds:
 * @self: an #LrgLodGroup
 * @out_min: (out caller-allocates) (array fixed-size=3): box minimum
 * @out_max: (out caller-allocates) (array fixed-size=3): box maximum
 *
 * Returns: %TRUE if the group has bounds (set, or from a level-0 model)
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_lod_group_get_bounds (LrgLodGroup *self,
                                   gfloat      *out_min,
                                   gfloat      *out_max);

/**
 * lrg_lod_group_get_bounding_sphere:
 * @self: an #LrgLodGroup
 * @out_center: (out caller-allocates) (array fixed-size=3): local centre
 * @out_radius: (out): radius (half the box diagonal)
 *
 * Returns: %TRUE if the group has bounds
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_lod_group_get_bounding_sphere (LrgLodGroup *self,
                                            gfloat      *out_center,
                                            gfloat      *out_radius);

/* --------------------------------------------------------------------------
 * Selection settings
 * -------------------------------------------------------------------------- */

/**
 * lrg_lod_group_get_hysteresis:
 * @self: an #LrgLodGroup
 *
 * Returns: the relative hysteresis band
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_lod_group_get_hysteresis (LrgLodGroup *self);

/**
 * lrg_lod_group_set_hysteresis:
 * @self: an #LrgLodGroup
 * @hysteresis: relative band (0 .. 0.9): the level only gets coarser
 *   below threshold * (1 - h) and finer above threshold * (1 + h)
 *
 * Sets #LrgLodGroup:hysteresis (default 0.1). Stops flickering when an
 * object hovers at a threshold.
 */
LRG_AVAILABLE_IN_ALL
void lrg_lod_group_set_hysteresis (LrgLodGroup *self,
                                   gfloat       hysteresis);

/**
 * lrg_lod_group_get_bias:
 * @self: an #LrgLodGroup
 *
 * Returns: the group's screen-size multiplier
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_lod_group_get_bias (LrgLodGroup *self);

/**
 * lrg_lod_group_set_bias:
 * @self: an #LrgLodGroup
 * @bias: screen-size multiplier (> 0), applied with the global bias
 *
 * Sets #LrgLodGroup:bias (default 1): 2 keeps every level until the
 * object is half as big on screen.
 */
LRG_AVAILABLE_IN_ALL
void lrg_lod_group_set_bias (LrgLodGroup *self,
                             gfloat       bias);

/**
 * lrg_lod_group_get_force_level:
 * @self: an #LrgLodGroup
 *
 * Returns: the forced level, or -1
 */
LRG_AVAILABLE_IN_ALL
gint lrg_lod_group_get_force_level (LrgLodGroup *self);

/**
 * lrg_lod_group_set_force_level:
 * @self: an #LrgLodGroup
 * @level: level to always use (clamped to the last level), or -1 for
 *   screen-size selection
 *
 * Sets #LrgLodGroup:force-level (debugging, editor LOD preview).
 */
LRG_AVAILABLE_IN_ALL
void lrg_lod_group_set_force_level (LrgLodGroup *self,
                                    gint         level);

/**
 * lrg_lod_group_get_fade_duration:
 * @self: an #LrgLodGroup
 *
 * Returns: the cross-fade time in seconds
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_lod_group_get_fade_duration (LrgLodGroup *self);

/**
 * lrg_lod_group_set_fade_duration:
 * @self: an #LrgLodGroup
 * @seconds: cross-fade time (0 = switch instantly)
 *
 * Sets #LrgLodGroup:fade-duration (default 0.25).
 */
LRG_AVAILABLE_IN_ALL
void lrg_lod_group_set_fade_duration (LrgLodGroup *self,
                                      gfloat       seconds);

/* --------------------------------------------------------------------------
 * Selection
 * -------------------------------------------------------------------------- */

/**
 * lrg_lod_group_select_level:
 * @self: an #LrgLodGroup
 * @screen_size: the object's screen size before any bias
 * @current_level: the level shown now (for the hysteresis), or
 *   %LRG_LOD_LEVEL_CULLED, or %LRG_LOD_LEVEL_NONE
 *
 * Pure selection: the forced level if set, else the first level whose
 * threshold the biased screen size reaches, moved only when the size
 * leaves the hysteresis band around @current_level.
 *
 * Returns: the level, or %LRG_LOD_LEVEL_CULLED
 */
LRG_AVAILABLE_IN_ALL
gint lrg_lod_group_select_level (LrgLodGroup *self,
                                 gfloat       screen_size,
                                 gint         current_level);

/**
 * lrg_lod_group_update_state:
 * @self: an #LrgLodGroup
 * @state: the per-view state to advance
 * @screen_size: the object's screen size this frame, before any bias
 * @delta: seconds since the last update (advances the fade)
 *
 * Selects the level and runs the cross-fade: when the level changes and
 * #LrgLodGroup:fade-duration is positive, the old level becomes
 * @state->previous_level and both are drawn with complementary dither
 * until @state->fade reaches 1.
 *
 * Returns: %TRUE if the level changed
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_lod_group_update_state (LrgLodGroup *self,
                                     LrgLodState *state,
                                     gfloat       screen_size,
                                     gfloat       delta);

G_END_DECLS
