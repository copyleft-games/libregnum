/* lrg-grass-field.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgGrassField - instanced grass: deterministic placement of blades
 * (crossed cards) per ground cell, then one instanced draw of a small
 * card mesh with wind sway, a base-to-tip colour, the bound
 * #LrgLightSet's light and fog, and a distance fade. Placement is
 * headless; only lrg_grass_field_draw() needs GL.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <graylib.h>
#include "../lrg-version.h"
#include "../lrg-types.h"

G_BEGIN_DECLS

/**
 * LRG_GRASS_FIELD_MAX_BLADES_PER_CELL:
 *
 * The most blades one cell can hold.
 */
#define LRG_GRASS_FIELD_MAX_BLADES_PER_CELL (256)

/**
 * LRG_GRASS_FIELD_CARD_VERTICES:
 *
 * Vertices of the card mesh every blade instance draws: two crossed
 * quads of two segments each.
 */
#define LRG_GRASS_FIELD_CARD_VERTICES (12)

/**
 * LRG_GRASS_FIELD_CARD_TRIANGLES:
 *
 * Triangles of the card mesh.
 */
#define LRG_GRASS_FIELD_CARD_TRIANGLES (8)

/**
 * LrgGrassBlade:
 * @position: the blade's foot in draw space
 * @yaw: rotation about +Y in radians
 * @height: the blade's height
 * @width: the card's width
 * @shade: brightness factor (about 0.85 .. 1.15)
 *
 * One placed blade (a pair of crossed cards).
 */
typedef struct
{
    gfloat position[3];
    gfloat yaw;
    gfloat height;
    gfloat width;
    gfloat shade;
} LrgGrassBlade;

#define LRG_TYPE_GRASS_BLADE (lrg_grass_blade_get_type ())

LRG_AVAILABLE_IN_ALL
GType lrg_grass_blade_get_type (void) G_GNUC_CONST;

/**
 * lrg_grass_blade_copy:
 * @blade: a #LrgGrassBlade
 *
 * Returns: (transfer full): a copy of @blade
 */
LRG_AVAILABLE_IN_ALL
LrgGrassBlade *
lrg_grass_blade_copy (const LrgGrassBlade *blade);

/**
 * lrg_grass_blade_free:
 * @blade: a #LrgGrassBlade
 */
LRG_AVAILABLE_IN_ALL
void
lrg_grass_blade_free (LrgGrassBlade *blade);

/**
 * LrgGrassHeightFunc:
 * @x: draw-space X of a blade's foot
 * @z: draw-space Z
 * @out_y: (out): the ground height there
 * @user_data: the data given with the function
 *
 * Returns: %TRUE when there is ground at (@x, @z) (else no blade)
 */
typedef gboolean (*LrgGrassHeightFunc) (gfloat    x,
                                        gfloat    z,
                                        gfloat   *out_y,
                                        gpointer  user_data);

#define LRG_TYPE_GRASS_FIELD (lrg_grass_field_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgGrassField, lrg_grass_field, LRG, GRASS_FIELD, GObject)

/**
 * lrg_grass_field_new:
 * @seed: the placement seed
 *
 * Creates a field with 2-unit cells, 24 blades per full cell, blades
 * 0.35 .. 0.7 high and 0.3 .. 0.5 wide, a fade from 30 to 45 units,
 * a light breeze and green colours.
 *
 * Returns: (transfer full): a new #LrgGrassField
 */
LRG_AVAILABLE_IN_ALL
LrgGrassField *
lrg_grass_field_new (guint32 seed);

/**
 * lrg_grass_field_set_cell_size:
 * @self: an #LrgGrassField
 * @size: the side of a placement cell in world units (> 0)
 */
LRG_AVAILABLE_IN_ALL
void
lrg_grass_field_set_cell_size (LrgGrassField *self,
                               gfloat         size);

/**
 * lrg_grass_field_get_cell_size:
 * @self: an #LrgGrassField
 *
 * Returns: the cell side
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_grass_field_get_cell_size (LrgGrassField *self);

/**
 * lrg_grass_field_set_blades_per_cell:
 * @self: an #LrgGrassField
 * @blades: blades of a cell at density 1, 0 .. %LRG_GRASS_FIELD_MAX_BLADES_PER_CELL
 */
LRG_AVAILABLE_IN_ALL
void
lrg_grass_field_set_blades_per_cell (LrgGrassField *self,
                                     guint          blades);

/**
 * lrg_grass_field_get_blades_per_cell:
 * @self: an #LrgGrassField
 *
 * Returns: blades of a full cell
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_grass_field_get_blades_per_cell (LrgGrassField *self);

/**
 * lrg_grass_field_set_blade_size:
 * @self: an #LrgGrassField
 * @min_height: the shortest blade
 * @max_height: the tallest blade (>= @min_height)
 * @min_width: the narrowest card
 * @max_width: the widest card (>= @min_width)
 */
LRG_AVAILABLE_IN_ALL
void
lrg_grass_field_set_blade_size (LrgGrassField *self,
                                gfloat         min_height,
                                gfloat         max_height,
                                gfloat         min_width,
                                gfloat         max_width);

/**
 * lrg_grass_field_set_fade:
 * @self: an #LrgGrassField
 * @start: distance from the eye where blades start to thin out
 * @end: distance where the last blade is gone (> @start)
 */
LRG_AVAILABLE_IN_ALL
void
lrg_grass_field_set_fade (LrgGrassField *self,
                          gfloat         start,
                          gfloat         end);

/**
 * lrg_grass_field_get_fade_end:
 * @self: an #LrgGrassField
 *
 * Returns: the distance past which nothing draws (place no farther)
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_grass_field_get_fade_end (LrgGrassField *self);

/**
 * lrg_grass_field_set_wind:
 * @self: an #LrgGrassField
 * @strength: sway of a blade's tip in world units (0 = still)
 * @frequency: sway speed in radians per second
 */
LRG_AVAILABLE_IN_ALL
void
lrg_grass_field_set_wind (LrgGrassField *self,
                          gfloat         strength,
                          gfloat         frequency);

/**
 * lrg_grass_field_set_colors:
 * @self: an #LrgGrassField
 * @base: the colour at a blade's foot
 * @tip: the colour at its tip
 */
LRG_AVAILABLE_IN_ALL
void
lrg_grass_field_set_colors (LrgGrassField  *self,
                            const GrlColor *base,
                            const GrlColor *tip);

/**
 * lrg_grass_field_place_cell:
 * @self: an #LrgGrassField
 * @cell_x: the cell column in placement space (absolute, continuous
 *   across a floating origin)
 * @cell_z: the cell row
 * @density: 0 (bare) .. 1 (full)
 * @draw_offset_x: draw X = placement X - @draw_offset_x
 * @draw_offset_z: draw Z = placement Z - @draw_offset_z
 * @height_func: (scope call) (closure user_data) (nullable): the ground height, or %NULL for y = 0
 * @user_data: data for @height_func
 * @out_blades: (element-type LrgGrassBlade): where the blades are appended
 *
 * Places the cell's blades. Every blade slot of the cell has its own
 * hash: a slot is used when its hash is below @density, so the same
 * cell always gets the same blades and a higher density only adds
 * blades. Blades lie inside the cell.
 *
 * Returns: the number of blades appended
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_grass_field_place_cell (LrgGrassField      *self,
                            gint64              cell_x,
                            gint64              cell_z,
                            gfloat              density,
                            gdouble             draw_offset_x,
                            gdouble             draw_offset_z,
                            LrgGrassHeightFunc  height_func,
                            gpointer            user_data,
                            GArray             *out_blades);

/**
 * lrg_grass_field_set_blades:
 * @self: an #LrgGrassField
 * @blades: (array length=n_blades) (nullable): the blades to draw
 * @n_blades: the number of blades
 *
 * Replaces the drawn instances (one transform each, built here on the
 * CPU).
 */
LRG_AVAILABLE_IN_ALL
void
lrg_grass_field_set_blades (LrgGrassField       *self,
                            const LrgGrassBlade *blades,
                            guint                n_blades);

/**
 * lrg_grass_field_get_instance_count:
 * @self: an #LrgGrassField
 *
 * Returns: the number of blade instances lrg_grass_field_draw() draws
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_grass_field_get_instance_count (LrgGrassField *self);

/**
 * lrg_grass_field_get_card:
 * @self: an #LrgGrassField
 * @out_positions: (out caller-allocates) (array fixed-size=36): the
 *   %LRG_GRASS_FIELD_CARD_VERTICES positions of the unit card (1 wide,
 *   1 high, foot at the origin)
 *
 * The geometry every instance scales, turns and places (tests and
 * picking).
 */
LRG_AVAILABLE_IN_ALL
void
lrg_grass_field_get_card (LrgGrassField *self,
                          gfloat        *out_positions);

/**
 * lrg_grass_field_draw:
 * @self: an #LrgGrassField
 * @eye: (array fixed-size=3): the camera in draw space (for the fade)
 * @time: seconds (for the wind)
 *
 * Draws every instance with one instanced call, two-sided, lit by the
 * bound #LrgLightSet (lrg_light_set_bind()) and fogged by its fog
 * (unlit and clear without one). Needs a GL context and 3D mode; does
 * nothing without instances.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_grass_field_draw (LrgGrassField *self,
                      const gfloat  *eye,
                      gfloat         time);

/**
 * lrg_grass_field_release_gpu:
 * @self: an #LrgGrassField
 *
 * Frees the card mesh and the shader while the GL context lives; the
 * next draw makes them again.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_grass_field_release_gpu (LrgGrassField *self);

G_END_DECLS
