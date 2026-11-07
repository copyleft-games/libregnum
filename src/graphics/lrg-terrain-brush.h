/* lrg-terrain-brush.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgTerrainBrush: the engine side of terrain sculpting and painting.
 *
 * A brush is an operation (raise, lower, flatten, smooth, noise, paint a
 * surface layer, punch or fill holes, paint or erase foliage density)
 * with a radius, a strength and a falloff.  lrg_terrain_brush_apply()
 * runs one dab on an #LrgTerrainAsset at a local position and returns
 * the touched region before and after as an #LrgTerrainEdit; a stroke
 * (begin, any number of dabs, end) returns one edit for the whole
 * stroke, which is what an editor puts on its undo stack.  All of it is
 * headless and deterministic.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-types.h"
#include "../tween/lrg-keyframe-curve.h"
#include "lrg-terrain-asset.h"

G_BEGIN_DECLS

/**
 * LrgTerrainBrushOp:
 * @LRG_TERRAIN_BRUSH_RAISE: heights += strength x amount x falloff
 * @LRG_TERRAIN_BRUSH_LOWER: heights -= strength x amount x falloff
 * @LRG_TERRAIN_BRUSH_FLATTEN: heights move towards the target height by
 *   min (1, strength x amount x falloff) of the gap
 * @LRG_TERRAIN_BRUSH_SMOOTH: heights move towards the mean of their 3 x 3
 *   neighbourhood (taken before the dab) by min (1, strength x amount x
 *   falloff)
 * @LRG_TERRAIN_BRUSH_NOISE: heights += strength x amount x falloff x
 *   (2 n - 1), n a smooth value noise of the brush seed at the noise
 *   scale
 * @LRG_TERRAIN_BRUSH_PAINT: the brush layer's weight moves towards 1 by
 *   min (1, strength x amount x falloff) (the other layers scale down so
 *   the sum stays 1); in material-ids mode, cells whose centre has a
 *   falloff of at least 0.5 take the brush layer
 * @LRG_TERRAIN_BRUSH_HOLE: cells whose centre has a falloff of at least
 *   0.5 become holes
 * @LRG_TERRAIN_BRUSH_FILL: such cells stop being holes
 * @LRG_TERRAIN_BRUSH_FOLIAGE_PAINT: the brush layer's foliage density
 *   moves towards 1 by min (1, strength x amount x falloff) at every cell
 *   centre
 * @LRG_TERRAIN_BRUSH_FOLIAGE_ERASE: the foliage density moves towards 0
 *
 * What a brush dab does.
 */
typedef enum
{
	LRG_TERRAIN_BRUSH_RAISE,
	LRG_TERRAIN_BRUSH_LOWER,
	LRG_TERRAIN_BRUSH_FLATTEN,
	LRG_TERRAIN_BRUSH_SMOOTH,
	LRG_TERRAIN_BRUSH_NOISE,
	LRG_TERRAIN_BRUSH_PAINT,
	LRG_TERRAIN_BRUSH_HOLE,
	LRG_TERRAIN_BRUSH_FILL,
	LRG_TERRAIN_BRUSH_FOLIAGE_PAINT,
	LRG_TERRAIN_BRUSH_FOLIAGE_ERASE
} LrgTerrainBrushOp;

LRG_AVAILABLE_IN_ALL
GType lrg_terrain_brush_op_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_TERRAIN_BRUSH_OP (lrg_terrain_brush_op_get_type ())

/**
 * LrgTerrainBrushFalloff:
 * @LRG_TERRAIN_BRUSH_FALLOFF_LINEAR: 1 - t
 * @LRG_TERRAIN_BRUSH_FALLOFF_SMOOTH: 1 - t² (3 - 2 t) (smoothstep)
 * @LRG_TERRAIN_BRUSH_FALLOFF_SPHERE: sqrt (1 - t²)
 * @LRG_TERRAIN_BRUSH_FALLOFF_SHARP: the square of 1 - t
 * @LRG_TERRAIN_BRUSH_FALLOFF_CONSTANT: 1 inside the radius
 * @LRG_TERRAIN_BRUSH_FALLOFF_CURVE: the falloff curve sampled at t
 *   (clamped to 0 .. 1); linear without a curve
 *
 * The brush weight at t = distance / radius (0 outside the radius).
 */
typedef enum
{
	LRG_TERRAIN_BRUSH_FALLOFF_LINEAR,
	LRG_TERRAIN_BRUSH_FALLOFF_SMOOTH,
	LRG_TERRAIN_BRUSH_FALLOFF_SPHERE,
	LRG_TERRAIN_BRUSH_FALLOFF_SHARP,
	LRG_TERRAIN_BRUSH_FALLOFF_CONSTANT,
	LRG_TERRAIN_BRUSH_FALLOFF_CURVE
} LrgTerrainBrushFalloff;

LRG_AVAILABLE_IN_ALL
GType lrg_terrain_brush_falloff_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_TERRAIN_BRUSH_FALLOFF (lrg_terrain_brush_falloff_get_type ())

#define LRG_TYPE_TERRAIN_BRUSH (lrg_terrain_brush_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgTerrainBrush, lrg_terrain_brush, LRG, TERRAIN_BRUSH, GObject)

/**
 * lrg_terrain_brush_new:
 * @op: the operation
 *
 * Creates a brush of radius 4, strength 1, smooth falloff, target height
 * 0, layer 0, noise scale 8 and seed 0.
 *
 * Returns: (transfer full): a new #LrgTerrainBrush
 */
LRG_AVAILABLE_IN_ALL
LrgTerrainBrush * lrg_terrain_brush_new (LrgTerrainBrushOp op);

/**
 * lrg_terrain_brush_get_op:
 * @self: an #LrgTerrainBrush
 *
 * Returns: the operation
 */
LRG_AVAILABLE_IN_ALL
LrgTerrainBrushOp lrg_terrain_brush_get_op (LrgTerrainBrush *self);

/**
 * lrg_terrain_brush_set_op:
 * @self: an #LrgTerrainBrush
 * @op: the operation
 */
LRG_AVAILABLE_IN_ALL
void lrg_terrain_brush_set_op (LrgTerrainBrush   *self,
                               LrgTerrainBrushOp  op);

/**
 * lrg_terrain_brush_get_radius:
 * @self: an #LrgTerrainBrush
 *
 * Returns: the radius in local world units
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_terrain_brush_get_radius (LrgTerrainBrush *self);

/**
 * lrg_terrain_brush_set_radius:
 * @self: an #LrgTerrainBrush
 * @radius: the radius (> 0)
 */
LRG_AVAILABLE_IN_ALL
void lrg_terrain_brush_set_radius (LrgTerrainBrush *self,
                                   gfloat           radius);

/**
 * lrg_terrain_brush_get_strength:
 * @self: an #LrgTerrainBrush
 *
 * Returns: the strength (world units per dab for raise / lower / noise, a
 *   blend rate otherwise)
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_terrain_brush_get_strength (LrgTerrainBrush *self);

/**
 * lrg_terrain_brush_set_strength:
 * @self: an #LrgTerrainBrush
 * @strength: the strength (>= 0)
 */
LRG_AVAILABLE_IN_ALL
void lrg_terrain_brush_set_strength (LrgTerrainBrush *self,
                                     gfloat           strength);

/**
 * lrg_terrain_brush_get_falloff:
 * @self: an #LrgTerrainBrush
 *
 * Returns: the falloff shape
 */
LRG_AVAILABLE_IN_ALL
LrgTerrainBrushFalloff lrg_terrain_brush_get_falloff (LrgTerrainBrush *self);

/**
 * lrg_terrain_brush_set_falloff:
 * @self: an #LrgTerrainBrush
 * @falloff: the falloff shape
 */
LRG_AVAILABLE_IN_ALL
void lrg_terrain_brush_set_falloff (LrgTerrainBrush        *self,
                                    LrgTerrainBrushFalloff  falloff);

/**
 * lrg_terrain_brush_get_falloff_curve:
 * @self: an #LrgTerrainBrush
 *
 * Returns: (transfer none) (nullable): the curve of
 *   %LRG_TERRAIN_BRUSH_FALLOFF_CURVE
 */
LRG_AVAILABLE_IN_ALL
LrgKeyframeCurve * lrg_terrain_brush_get_falloff_curve (LrgTerrainBrush *self);

/**
 * lrg_terrain_brush_set_falloff_curve:
 * @self: an #LrgTerrainBrush
 * @curve: (nullable): a curve over t = 0 (centre) .. 1 (rim)
 */
LRG_AVAILABLE_IN_ALL
void lrg_terrain_brush_set_falloff_curve (LrgTerrainBrush  *self,
                                          LrgKeyframeCurve *curve);

/**
 * lrg_terrain_brush_get_target_height:
 * @self: an #LrgTerrainBrush
 *
 * Returns: the flatten target height
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_terrain_brush_get_target_height (LrgTerrainBrush *self);

/**
 * lrg_terrain_brush_set_target_height:
 * @self: an #LrgTerrainBrush
 * @height: the flatten target height
 */
LRG_AVAILABLE_IN_ALL
void lrg_terrain_brush_set_target_height (LrgTerrainBrush *self,
                                          gfloat           height);

/**
 * lrg_terrain_brush_get_sample_target:
 * @self: an #LrgTerrainBrush
 *
 * Returns: whether lrg_terrain_brush_begin_stroke() samples the flatten
 *   target under the brush
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_terrain_brush_get_sample_target (LrgTerrainBrush *self);

/**
 * lrg_terrain_brush_set_sample_target:
 * @self: an #LrgTerrainBrush
 * @sample: %TRUE to take the flatten target from the terrain height at
 *   the stroke's first position
 */
LRG_AVAILABLE_IN_ALL
void lrg_terrain_brush_set_sample_target (LrgTerrainBrush *self,
                                          gboolean         sample);

/**
 * lrg_terrain_brush_get_layer:
 * @self: an #LrgTerrainBrush
 *
 * Returns: the surface layer (paint) or foliage layer (foliage ops)
 */
LRG_AVAILABLE_IN_ALL
guint lrg_terrain_brush_get_layer (LrgTerrainBrush *self);

/**
 * lrg_terrain_brush_set_layer:
 * @self: an #LrgTerrainBrush
 * @layer: the layer index
 */
LRG_AVAILABLE_IN_ALL
void lrg_terrain_brush_set_layer (LrgTerrainBrush *self,
                                  guint            layer);

/**
 * lrg_terrain_brush_get_noise_scale:
 * @self: an #LrgTerrainBrush
 *
 * Returns: the noise feature size in world units
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_terrain_brush_get_noise_scale (LrgTerrainBrush *self);

/**
 * lrg_terrain_brush_set_noise_scale:
 * @self: an #LrgTerrainBrush
 * @scale: the noise feature size (> 0)
 */
LRG_AVAILABLE_IN_ALL
void lrg_terrain_brush_set_noise_scale (LrgTerrainBrush *self,
                                        gfloat           scale);

/**
 * lrg_terrain_brush_get_seed:
 * @self: an #LrgTerrainBrush
 *
 * Returns: the noise seed
 */
LRG_AVAILABLE_IN_ALL
guint32 lrg_terrain_brush_get_seed (LrgTerrainBrush *self);

/**
 * lrg_terrain_brush_set_seed:
 * @self: an #LrgTerrainBrush
 * @seed: the noise seed
 */
LRG_AVAILABLE_IN_ALL
void lrg_terrain_brush_set_seed (LrgTerrainBrush *self,
                                 guint32          seed);

/**
 * lrg_terrain_brush_get_weight:
 * @self: an #LrgTerrainBrush
 * @distance: distance from the brush centre
 *
 * The falloff at a distance (what a brush cursor draws as its rings).
 *
 * Returns: 0 .. 1 (0 at and past the radius)
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_terrain_brush_get_weight (LrgTerrainBrush *self,
                                     gfloat           distance);

/**
 * lrg_terrain_brush_get_region:
 * @self: an #LrgTerrainBrush
 * @asset: the terrain
 * @x: local X of the brush centre
 * @z: local Z
 * @out_x: (out) (optional): first column
 * @out_z: (out) (optional): first row
 * @out_width: (out) (optional): columns
 * @out_height: (out) (optional): rows
 *
 * The region a dab at (@x, @z) would touch, in samples for height and
 * paint (weights) ops and in cells otherwise, clipped to the grid.
 *
 * Returns: %TRUE when the region is not empty
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_terrain_brush_get_region (LrgTerrainBrush *self,
                                       LrgTerrainAsset *asset,
                                       gfloat           x,
                                       gfloat           z,
                                       guint           *out_x,
                                       guint           *out_z,
                                       guint           *out_width,
                                       guint           *out_height);

/**
 * lrg_terrain_brush_get_edit_kind:
 * @self: an #LrgTerrainBrush
 * @asset: the terrain (its splat mode decides what painting edits)
 *
 * Returns: the data set the brush's dabs change on @asset
 */
LRG_AVAILABLE_IN_ALL
LrgTerrainEditKind lrg_terrain_brush_get_edit_kind (LrgTerrainBrush *self,
                                                    LrgTerrainAsset *asset);

/**
 * lrg_terrain_brush_apply:
 * @self: an #LrgTerrainBrush
 * @asset: the terrain to edit
 * @x: local X of the brush centre
 * @z: local Z
 * @amount: the dab's share of the strength (1 for a single click, the
 *   frame time for a held brush)
 *
 * Runs one dab and emits #LrgTerrainAsset::region-changed once.  During
 * a stroke the dab is also folded into the stroke's edit.
 *
 * Returns: (transfer full) (nullable): the dab's region before and after,
 *   %NULL when nothing changed
 */
LRG_AVAILABLE_IN_ALL
LrgTerrainEdit * lrg_terrain_brush_apply (LrgTerrainBrush *self,
                                          LrgTerrainAsset *asset,
                                          gfloat           x,
                                          gfloat           z,
                                          gfloat           amount);

/**
 * lrg_terrain_brush_begin_stroke:
 * @self: an #LrgTerrainBrush
 * @asset: the terrain the stroke edits
 * @x: local X of the first position
 * @z: local Z
 *
 * Starts a stroke: snapshots the data set the brush edits (and samples
 * the flatten target when #LrgTerrainBrush:sample-target is set).  A
 * stroke already running is ended first and its edit dropped.
 */
LRG_AVAILABLE_IN_ALL
void lrg_terrain_brush_begin_stroke (LrgTerrainBrush *self,
                                     LrgTerrainAsset *asset,
                                     gfloat           x,
                                     gfloat           z);

/**
 * lrg_terrain_brush_is_stroking:
 * @self: an #LrgTerrainBrush
 *
 * Returns: %TRUE between begin and end of a stroke
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_terrain_brush_is_stroking (LrgTerrainBrush *self);

/**
 * lrg_terrain_brush_end_stroke:
 * @self: an #LrgTerrainBrush
 *
 * Ends the stroke.
 *
 * Returns: (transfer full) (nullable): one edit covering every dab of the
 *   stroke (the union of their regions, before = at begin, after = now),
 *   %NULL when the stroke changed nothing
 */
LRG_AVAILABLE_IN_ALL
LrgTerrainEdit * lrg_terrain_brush_end_stroke (LrgTerrainBrush *self);

G_END_DECLS
