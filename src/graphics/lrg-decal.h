/* lrg-decal.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgDecal: a box projector that paints albedo, normal and
 * roughness/metallic onto lit surfaces inside it.  LrgDecalSet: the
 * decals of a frame, sorted, binned into a clustered grid like the light
 * set's lights and packed into data textures plus a texture atlas that
 * the lit shader walks through lrg_decals_apply().
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <graylib.h>
#include "../lrg-version.h"

G_BEGIN_DECLS

/* ==========================================================================
 * Enumerations
 * ========================================================================== */

/**
 * LrgDecalBlend:
 * @LRG_DECAL_BLEND_ALPHA: the decal colour replaces the surface albedo by
 *   its alpha (stickers, posters, paint)
 * @LRG_DECAL_BLEND_MULTIPLY: the albedo is multiplied by the decal colour
 *   (dirt, grime, scorch marks, puddle darkening)
 * @LRG_DECAL_BLEND_ADD: the decal colour is added to the albedo (glow
 *   paint, frost highlights)
 *
 * How a decal combines its colour with the surface albedo.  Normal and
 * roughness/metallic always blend by the decal's coverage.
 */
typedef enum
{
	LRG_DECAL_BLEND_ALPHA,
	LRG_DECAL_BLEND_MULTIPLY,
	LRG_DECAL_BLEND_ADD
} LrgDecalBlend;

LRG_AVAILABLE_IN_ALL
GType lrg_decal_blend_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_DECAL_BLEND (lrg_decal_blend_get_type ())

/**
 * LrgDecalChannels:
 * @LRG_DECAL_CHANNEL_NONE: affects nothing
 * @LRG_DECAL_CHANNEL_ALBEDO: changes the albedo (colour times the albedo
 *   texture)
 * @LRG_DECAL_CHANNEL_NORMAL: bends the normal with the normal texture
 *   (no effect without one)
 * @LRG_DECAL_CHANNEL_ROUGHNESS: replaces roughness with the ORM texture's
 *   green channel, or the decal's roughness without a texture
 * @LRG_DECAL_CHANNEL_METALLIC: replaces metallic with the ORM texture's
 *   blue channel, or the decal's metallic without a texture
 * @LRG_DECAL_CHANNEL_ALL: every channel
 *
 * The surface properties a decal writes.
 */
typedef enum /*< flags >*/
{
	LRG_DECAL_CHANNEL_NONE      = 0,
	LRG_DECAL_CHANNEL_ALBEDO    = 1 << 0,
	LRG_DECAL_CHANNEL_NORMAL    = 1 << 1,
	LRG_DECAL_CHANNEL_ROUGHNESS = 1 << 2,
	LRG_DECAL_CHANNEL_METALLIC  = 1 << 3,
	LRG_DECAL_CHANNEL_ALL       = 0xF
} LrgDecalChannels;

LRG_AVAILABLE_IN_ALL
GType lrg_decal_channels_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_DECAL_CHANNELS (lrg_decal_channels_get_type ())

/* ==========================================================================
 * LrgDecal
 * ========================================================================== */

#define LRG_TYPE_DECAL (lrg_decal_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgDecal, lrg_decal, LRG, DECAL, GObject)

/**
 * lrg_decal_new:
 *
 * Creates a 1 x 1 x 1 decal at the origin projecting down (along local
 * -Y): opaque white, alpha blend, albedo and normal channels, fade angle
 * 60 degrees over 20, sort order 0, every layer.
 *
 * Returns: (transfer full): a new #LrgDecal
 */
LRG_AVAILABLE_IN_ALL
LrgDecal * lrg_decal_new (void);

/**
 * lrg_decal_set_transform:
 * @self: an #LrgDecal
 * @matrix: (array fixed-size=16): column-major world matrix mapping the
 *   unit box (-0.5 .. 0.5 on every axis) onto the projector volume
 *
 * Places the decal.  The box projects along its local -Y axis (local +Y
 * is the "up" the fade angle measures against); the texture's U runs
 * along local +X and V along local +Z (the image's top edge at local
 * -Z).  Scale is the box size.
 */
LRG_AVAILABLE_IN_ALL
void lrg_decal_set_transform (LrgDecal     *self,
                              const gfloat *matrix);

/**
 * lrg_decal_get_transform:
 * @self: an #LrgDecal
 * @out_matrix: (out caller-allocates) (array fixed-size=16): the
 *   column-major world matrix of the unit box
 */
LRG_AVAILABLE_IN_ALL
void lrg_decal_get_transform (LrgDecal *self,
                              gfloat   *out_matrix);

/**
 * lrg_decal_set_trs:
 * @self: an #LrgDecal
 * @position: (array fixed-size=3): box centre in world space
 * @rotation: (nullable) (array fixed-size=4): unit quaternion (x, y, z,
 *   w), %NULL for none (projecting straight down)
 * @size: (array fixed-size=3): box extents (width along X, projection
 *   depth along Y, height along Z)
 *
 * lrg_decal_set_transform() from translation, rotation and size.
 */
LRG_AVAILABLE_IN_ALL
void lrg_decal_set_trs (LrgDecal     *self,
                        const gfloat *position,
                        const gfloat *rotation,
                        const gfloat *size);

/**
 * lrg_decal_get_world_bounds:
 * @self: an #LrgDecal
 * @out_min: (out caller-allocates) (array fixed-size=3): AABB minimum
 * @out_max: (out caller-allocates) (array fixed-size=3): AABB maximum
 *
 * The world axis-aligned box around the projector volume.
 */
LRG_AVAILABLE_IN_ALL
void lrg_decal_get_world_bounds (LrgDecal *self,
                                 gfloat   *out_min,
                                 gfloat   *out_max);

/**
 * lrg_decal_project_point:
 * @self: an #LrgDecal
 * @point: (array fixed-size=3): a world position
 * @out_local: (out caller-allocates) (optional) (array fixed-size=3): the
 *   position in the unit box (-0.5 .. 0.5 inside)
 *
 * Maps a world point into the projector, as the shader does.
 *
 * Returns: %TRUE when @point is inside the box
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_decal_project_point (LrgDecal     *self,
                                  const gfloat *point,
                                  gfloat       *out_local);

/**
 * lrg_decal_get_coverage:
 * @self: an #LrgDecal
 * @point: (array fixed-size=3): a world position on a surface
 * @normal: (nullable) (array fixed-size=3): the surface's geometric
 *   normal (any length), or %NULL to skip the angle fade
 *
 * The CPU model of the shader's blend weight before textures: opacity
 * times colour alpha, times the angle fade and the depth fade; 0 outside
 * the box or when the decal is disabled.
 *
 * Returns: the coverage in 0 .. 1
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_decal_get_coverage (LrgDecal     *self,
                               const gfloat *point,
                               const gfloat *normal);

/**
 * lrg_decal_set_color:
 * @self: an #LrgDecal
 * @r: red, 0 .. 1
 * @g: green, 0 .. 1
 * @b: blue, 0 .. 1
 * @a: alpha, 0 .. 1 (scales the coverage)
 *
 * Tints the albedo texture (or is the decal colour without one).  The
 * colour is in the same (sRGB-encoded) space as material colours.
 */
LRG_AVAILABLE_IN_ALL
void lrg_decal_set_color (LrgDecal *self,
                          gfloat    r,
                          gfloat    g,
                          gfloat    b,
                          gfloat    a);

/**
 * lrg_decal_get_color:
 * @self: an #LrgDecal
 * @out_rgba: (out caller-allocates) (array fixed-size=4): the colour
 */
LRG_AVAILABLE_IN_ALL
void lrg_decal_get_color (LrgDecal *self,
                          gfloat   *out_rgba);

/**
 * lrg_decal_set_opacity:
 * @self: an #LrgDecal
 * @opacity: 0 .. 1, scales every channel (fade decals in and out with it)
 */
LRG_AVAILABLE_IN_ALL
void lrg_decal_set_opacity (LrgDecal *self,
                            gfloat    opacity);

/**
 * lrg_decal_get_opacity:
 * @self: an #LrgDecal
 *
 * Returns: the opacity
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_decal_get_opacity (LrgDecal *self);

/**
 * lrg_decal_set_fade_angle:
 * @self: an #LrgDecal
 * @degrees: 0 .. 180: surfaces whose normal is within this angle of the
 *   box's local +Y get the full decal
 *
 * Beyond the angle the decal fades out over lrg_decal_set_fade_width()
 * degrees, which stops it smearing down walls next to the floor it was
 * aimed at.
 */
LRG_AVAILABLE_IN_ALL
void lrg_decal_set_fade_angle (LrgDecal *self,
                               gfloat    degrees);

/**
 * lrg_decal_get_fade_angle:
 * @self: an #LrgDecal
 *
 * Returns: the full-strength angle in degrees
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_decal_get_fade_angle (LrgDecal *self);

/**
 * lrg_decal_set_fade_width:
 * @self: an #LrgDecal
 * @degrees: 0 .. 180, the angle over which the decal fades out past the
 *   fade angle (0: a hard cut)
 */
LRG_AVAILABLE_IN_ALL
void lrg_decal_set_fade_width (LrgDecal *self,
                               gfloat    degrees);

/**
 * lrg_decal_get_fade_width:
 * @self: an #LrgDecal
 *
 * Returns: the fade width in degrees
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_decal_get_fade_width (LrgDecal *self);

/**
 * lrg_decal_set_depth_fade:
 * @self: an #LrgDecal
 * @fade: 0 .. 1, the share of the box's half depth (local Y) over which
 *   the decal fades towards the top and bottom faces (0: hard cut)
 */
LRG_AVAILABLE_IN_ALL
void lrg_decal_set_depth_fade (LrgDecal *self,
                               gfloat    fade);

/**
 * lrg_decal_get_depth_fade:
 * @self: an #LrgDecal
 *
 * Returns: the depth fade share
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_decal_get_depth_fade (LrgDecal *self);

/**
 * lrg_decal_set_sort_order:
 * @self: an #LrgDecal
 * @order: draw order: higher orders apply after (on top of) lower ones;
 *   equal orders keep the set's insertion order
 */
LRG_AVAILABLE_IN_ALL
void lrg_decal_set_sort_order (LrgDecal *self,
                               gint      order);

/**
 * lrg_decal_get_sort_order:
 * @self: an #LrgDecal
 *
 * Returns: the sort order
 */
LRG_AVAILABLE_IN_ALL
gint lrg_decal_get_sort_order (LrgDecal *self);

/**
 * lrg_decal_set_layer_mask:
 * @self: an #LrgDecal
 * @mask: render layers (bit n = layer n) whose surfaces receive the decal
 *
 * Matched against the receiver layers of the decal set
 * (lrg_decal_set_set_receiver_layers()) at draw time.
 */
LRG_AVAILABLE_IN_ALL
void lrg_decal_set_layer_mask (LrgDecal *self,
                               guint32   mask);

/**
 * lrg_decal_get_layer_mask:
 * @self: an #LrgDecal
 *
 * Returns: the receiving layers
 */
LRG_AVAILABLE_IN_ALL
guint32 lrg_decal_get_layer_mask (LrgDecal *self);

/**
 * lrg_decal_set_blend:
 * @self: an #LrgDecal
 * @blend: how the colour combines with the albedo
 */
LRG_AVAILABLE_IN_ALL
void lrg_decal_set_blend (LrgDecal      *self,
                          LrgDecalBlend  blend);

/**
 * lrg_decal_get_blend:
 * @self: an #LrgDecal
 *
 * Returns: the blend mode
 */
LRG_AVAILABLE_IN_ALL
LrgDecalBlend lrg_decal_get_blend (LrgDecal *self);

/**
 * lrg_decal_set_channels:
 * @self: an #LrgDecal
 * @channels: the surface properties to write
 */
LRG_AVAILABLE_IN_ALL
void lrg_decal_set_channels (LrgDecal         *self,
                             LrgDecalChannels  channels);

/**
 * lrg_decal_get_channels:
 * @self: an #LrgDecal
 *
 * Returns: the written channels
 */
LRG_AVAILABLE_IN_ALL
LrgDecalChannels lrg_decal_get_channels (LrgDecal *self);

/**
 * lrg_decal_set_normal_strength:
 * @self: an #LrgDecal
 * @strength: 0 .. 1, how far the normal turns to the decal's normal map
 */
LRG_AVAILABLE_IN_ALL
void lrg_decal_set_normal_strength (LrgDecal *self,
                                    gfloat    strength);

/**
 * lrg_decal_get_normal_strength:
 * @self: an #LrgDecal
 *
 * Returns: the normal strength
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_decal_get_normal_strength (LrgDecal *self);

/**
 * lrg_decal_set_roughness:
 * @self: an #LrgDecal
 * @roughness: 0 .. 1, written with %LRG_DECAL_CHANNEL_ROUGHNESS when there
 *   is no ORM texture (wet patches: low roughness)
 */
LRG_AVAILABLE_IN_ALL
void lrg_decal_set_roughness (LrgDecal *self,
                              gfloat    roughness);

/**
 * lrg_decal_get_roughness:
 * @self: an #LrgDecal
 *
 * Returns: the constant roughness
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_decal_get_roughness (LrgDecal *self);

/**
 * lrg_decal_set_metallic:
 * @self: an #LrgDecal
 * @metallic: 0 .. 1, written with %LRG_DECAL_CHANNEL_METALLIC when there
 *   is no ORM texture
 */
LRG_AVAILABLE_IN_ALL
void lrg_decal_set_metallic (LrgDecal *self,
                             gfloat    metallic);

/**
 * lrg_decal_get_metallic:
 * @self: an #LrgDecal
 *
 * Returns: the constant metallic
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_decal_get_metallic (LrgDecal *self);

/**
 * lrg_decal_set_enabled:
 * @self: an #LrgDecal
 * @enabled: whether the decal draws (disabled decals stay in their set)
 */
LRG_AVAILABLE_IN_ALL
void lrg_decal_set_enabled (LrgDecal *self,
                            gboolean  enabled);

/**
 * lrg_decal_get_enabled:
 * @self: an #LrgDecal
 *
 * Returns: whether the decal draws
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_decal_get_enabled (LrgDecal *self);

/**
 * lrg_decal_set_albedo_texture:
 * @self: an #LrgDecal
 * @texture: (nullable): albedo (RGB) and coverage (A), sRGB-encoded like
 *   material albedo maps; %NULL for a plain colour decal
 */
LRG_AVAILABLE_IN_ALL
void lrg_decal_set_albedo_texture (LrgDecal   *self,
                                   GrlTexture *texture);

/**
 * lrg_decal_get_albedo_texture:
 * @self: an #LrgDecal
 *
 * Returns: (transfer none) (nullable): the albedo texture
 */
LRG_AVAILABLE_IN_ALL
GrlTexture * lrg_decal_get_albedo_texture (LrgDecal *self);

/**
 * lrg_decal_set_normal_texture:
 * @self: an #LrgDecal
 * @texture: (nullable): tangent-space normal map (OpenGL convention,
 *   green up the image)
 */
LRG_AVAILABLE_IN_ALL
void lrg_decal_set_normal_texture (LrgDecal   *self,
                                   GrlTexture *texture);

/**
 * lrg_decal_get_normal_texture:
 * @self: an #LrgDecal
 *
 * Returns: (transfer none) (nullable): the normal map
 */
LRG_AVAILABLE_IN_ALL
GrlTexture * lrg_decal_get_normal_texture (LrgDecal *self);

/**
 * lrg_decal_set_orm_texture:
 * @self: an #LrgDecal
 * @texture: (nullable): glTF occlusion/roughness/metallic map (R unused,
 *   G roughness, B metallic)
 */
LRG_AVAILABLE_IN_ALL
void lrg_decal_set_orm_texture (LrgDecal   *self,
                                GrlTexture *texture);

/**
 * lrg_decal_get_orm_texture:
 * @self: an #LrgDecal
 *
 * Returns: (transfer none) (nullable): the ORM map
 */
LRG_AVAILABLE_IN_ALL
GrlTexture * lrg_decal_get_orm_texture (LrgDecal *self);

/**
 * lrg_decal_set_uv_rect:
 * @self: an #LrgDecal
 * @x: left of the used region, 0 .. 1 of the texture width
 * @y: top of the used region, 0 .. 1 of the texture height
 * @width: region width, 0 .. 1
 * @height: region height, 0 .. 1
 *
 * The part of the textures the box shows (one cell of a decal sheet);
 * (0, 0, 1, 1) by default.
 */
LRG_AVAILABLE_IN_ALL
void lrg_decal_set_uv_rect (LrgDecal *self,
                            gfloat    x,
                            gfloat    y,
                            gfloat    width,
                            gfloat    height);

/**
 * lrg_decal_get_uv_rect:
 * @self: an #LrgDecal
 * @out_rect: (out caller-allocates) (array fixed-size=4): x, y, width,
 *   height
 */
LRG_AVAILABLE_IN_ALL
void lrg_decal_get_uv_rect (LrgDecal *self,
                            gfloat   *out_rect);

/**
 * lrg_decal_get_version:
 * @self: an #LrgDecal
 *
 * A counter bumped by every change (sets compare it to skip uploads).
 *
 * Returns: the change counter
 */
LRG_AVAILABLE_IN_ALL
guint lrg_decal_get_version (LrgDecal *self);

/* ==========================================================================
 * LrgDecalSet
 * ========================================================================== */

/**
 * LRG_DECAL_SET_MAX_DECALS:
 *
 * Most decals one selection uploads (the data texture's rows).
 */
#define LRG_DECAL_SET_MAX_DECALS (1024)

/**
 * LRG_DECAL_SET_MAX_UNGRIDDED:
 *
 * Most decals a set without a grid selects (every fragment tests them
 * all, so keep scenes without a grid small).
 */
#define LRG_DECAL_SET_MAX_UNGRIDDED (64)

/**
 * LRG_DECAL_SET_MAX_SLOTS:
 *
 * Most decals one grid cell lists (lrg_decal_set_set_grid_slots()).
 */
#define LRG_DECAL_SET_MAX_SLOTS (32)

/**
 * LRG_DECAL_SET_MAX_CELLS:
 *
 * Most cells per side of the decal grid (lrg_decal_set_set_grid()).
 */
#define LRG_DECAL_SET_MAX_CELLS (128)

/**
 * LRG_DECAL_SET_MAX_LAYERS:
 *
 * Most vertical layers of the decal grid
 * (lrg_decal_set_set_grid_layers()).
 */
#define LRG_DECAL_SET_MAX_LAYERS (16)

#define LRG_TYPE_DECAL_SET (lrg_decal_set_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgDecalSet, lrg_decal_set, LRG, DECAL_SET, GObject)

/**
 * lrg_decal_set_new:
 *
 * Creates an empty set without a grid, a 1024-pixel atlas of 4 x 4
 * tiles, every receiver layer and no distance limit.
 *
 * Returns: (transfer full): a new #LrgDecalSet
 */
LRG_AVAILABLE_IN_ALL
LrgDecalSet * lrg_decal_set_new (void);

/**
 * lrg_decal_set_add:
 * @self: an #LrgDecalSet
 * @decal: a decal (a reference is taken; adding twice is a no-op)
 */
LRG_AVAILABLE_IN_ALL
void lrg_decal_set_add (LrgDecalSet *self,
                        LrgDecal    *decal);

/**
 * lrg_decal_set_remove:
 * @self: an #LrgDecalSet
 * @decal: a decal of the set
 *
 * Returns: %TRUE when @decal was in the set
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_decal_set_remove (LrgDecalSet *self,
                               LrgDecal    *decal);

/**
 * lrg_decal_set_clear:
 * @self: an #LrgDecalSet
 *
 * Removes every decal.
 */
LRG_AVAILABLE_IN_ALL
void lrg_decal_set_clear (LrgDecalSet *self);

/**
 * lrg_decal_set_get_count:
 * @self: an #LrgDecalSet
 *
 * Returns: the decals in the set
 */
LRG_AVAILABLE_IN_ALL
guint lrg_decal_set_get_count (LrgDecalSet *self);

/**
 * lrg_decal_set_get_decal:
 * @self: an #LrgDecalSet
 * @index: 0 .. count - 1, in insertion order
 *
 * Returns: (transfer none) (nullable): the decal, %NULL out of range
 */
LRG_AVAILABLE_IN_ALL
LrgDecal * lrg_decal_set_get_decal (LrgDecalSet *self,
                                    guint        index);

/**
 * lrg_decal_set_set_origin:
 * @self: an #LrgDecalSet
 * @x: origin X
 * @y: origin Y
 * @z: origin Z
 *
 * Floating origin: decals are uploaded relative to it so they meet
 * geometry drawn origin-relative.  Use the light set's origin
 * (lrg_light_set_set_origin()).
 */
LRG_AVAILABLE_IN_ALL
void lrg_decal_set_set_origin (LrgDecalSet *self,
                               gdouble      x,
                               gdouble      y,
                               gdouble      z);

/**
 * lrg_decal_set_get_origin:
 * @self: an #LrgDecalSet
 * @out_x: (out) (optional): origin X
 * @out_y: (out) (optional): origin Y
 * @out_z: (out) (optional): origin Z
 */
LRG_AVAILABLE_IN_ALL
void lrg_decal_set_get_origin (LrgDecalSet *self,
                               gdouble     *out_x,
                               gdouble     *out_y,
                               gdouble     *out_z);

/**
 * lrg_decal_set_set_grid:
 * @self: an #LrgDecalSet
 * @cells: cells per side (2 .. %LRG_DECAL_SET_MAX_CELLS), or 0 for no
 *   grid (every fragment tests every selected decal)
 * @cell_size: world size of a cell (> 0)
 *
 * The clustered grid: a square of cells centred on the camera's cell
 * each listing the decals whose bounds touch it, the same layout as the
 * light set's light grid (lrg_light_set_set_grid()).  Decals outside the
 * grid are not selected.
 */
LRG_AVAILABLE_IN_ALL
void lrg_decal_set_set_grid (LrgDecalSet *self,
                             guint        cells,
                             gfloat       cell_size);

/**
 * lrg_decal_set_get_grid_cells:
 * @self: an #LrgDecalSet
 *
 * Returns: the cells per side (0: no grid)
 */
LRG_AVAILABLE_IN_ALL
guint lrg_decal_set_get_grid_cells (LrgDecalSet *self);

/**
 * lrg_decal_set_set_grid_layers:
 * @self: an #LrgDecalSet
 * @layers: 1 .. %LRG_DECAL_SET_MAX_LAYERS vertical layers
 * @layer_height: world height of a layer (> 0)
 *
 * Splits the grid vertically around the camera (the lowest and highest
 * layers reach on without end), so stacked floors list their own decals.
 */
LRG_AVAILABLE_IN_ALL
void lrg_decal_set_set_grid_layers (LrgDecalSet *self,
                                    guint        layers,
                                    gfloat       layer_height);

/**
 * lrg_decal_set_set_grid_slots:
 * @self: an #LrgDecalSet
 * @slots: decals per cell, a multiple of 4 from 4 to
 *   %LRG_DECAL_SET_MAX_SLOTS (default 8)
 *
 * When more decals touch a cell, the ones with the highest sort order
 * are kept.
 */
LRG_AVAILABLE_IN_ALL
void lrg_decal_set_set_grid_slots (LrgDecalSet *self,
                                   guint        slots);

/**
 * lrg_decal_set_get_grid_slots:
 * @self: an #LrgDecalSet
 *
 * Returns: the decals per cell
 */
LRG_AVAILABLE_IN_ALL
guint lrg_decal_set_get_grid_slots (LrgDecalSet *self);

/**
 * lrg_decal_set_set_max_distance:
 * @self: an #LrgDecalSet
 * @distance: decals whose bounds are farther than this from the camera
 *   are not selected (0: no limit)
 */
LRG_AVAILABLE_IN_ALL
void lrg_decal_set_set_max_distance (LrgDecalSet *self,
                                     gfloat       distance);

/**
 * lrg_decal_set_get_max_distance:
 * @self: an #LrgDecalSet
 *
 * Returns: the distance limit (0: none)
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_decal_set_get_max_distance (LrgDecalSet *self);

/**
 * lrg_decal_set_set_receiver_layers:
 * @self: an #LrgDecalSet
 * @mask: render layers of the geometry about to be drawn
 *
 * A decal applies to a fragment only when its layer mask and this mask
 * share a bit.  Bound with the set, so change it between draws and bind
 * again (the default, every layer, suits most scenes).
 */
LRG_AVAILABLE_IN_ALL
void lrg_decal_set_set_receiver_layers (LrgDecalSet *self,
                                        guint32      mask);

/**
 * lrg_decal_set_get_receiver_layers:
 * @self: an #LrgDecalSet
 *
 * Returns: the receiver mask
 */
LRG_AVAILABLE_IN_ALL
guint32 lrg_decal_set_get_receiver_layers (LrgDecalSet *self);

/**
 * lrg_decal_set_set_atlas:
 * @self: an #LrgDecalSet
 * @size: atlas side in pixels (64 .. 4096)
 * @tiles: tiles per side (1 .. 16); each distinct texture combination of
 *   the selected decals takes one tile of size / tiles pixels
 *
 * The texture atlas the decals' albedo, normal and ORM textures are
 * copied into (three bands of @size x @size, one per channel).  Textures
 * are resampled to the tile size.  Decals beyond the tile count draw
 * with their colour only (a warning is logged once).
 */
LRG_AVAILABLE_IN_ALL
void lrg_decal_set_set_atlas (LrgDecalSet *self,
                              guint        size,
                              guint        tiles);

/**
 * lrg_decal_set_get_atlas_size:
 * @self: an #LrgDecalSet
 *
 * Returns: the atlas side in pixels
 */
LRG_AVAILABLE_IN_ALL
guint lrg_decal_set_get_atlas_size (LrgDecalSet *self);

/**
 * lrg_decal_set_get_atlas_tiles:
 * @self: an #LrgDecalSet
 *
 * Returns: the tiles per atlas side
 */
LRG_AVAILABLE_IN_ALL
guint lrg_decal_set_get_atlas_tiles (LrgDecalSet *self);

/**
 * lrg_decal_set_select:
 * @self: an #LrgDecalSet
 * @camera_x: camera world X
 * @camera_y: camera world Y
 * @camera_z: camera world Z
 *
 * Chooses the decals to draw: enabled, visible, inside the grid (or the
 * distance limit), at most %LRG_DECAL_SET_MAX_DECALS (with a grid) or
 * %LRG_DECAL_SET_MAX_UNGRIDDED (without, the nearest first); orders them
 * by sort order then insertion and fills the grid cells.  Headless.
 * Call once per frame (per camera) before drawing.
 *
 * Returns: the selected decal count
 */
LRG_AVAILABLE_IN_ALL
guint lrg_decal_set_select (LrgDecalSet *self,
                            gdouble      camera_x,
                            gdouble      camera_y,
                            gdouble      camera_z);

/**
 * lrg_decal_set_get_active_count:
 * @self: an #LrgDecalSet
 *
 * Returns: the decals of the last selection
 */
LRG_AVAILABLE_IN_ALL
guint lrg_decal_set_get_active_count (LrgDecalSet *self);

/**
 * lrg_decal_set_get_active_decal:
 * @self: an #LrgDecalSet
 * @index: 0 .. active count - 1, in application order
 *
 * Returns: (transfer none) (nullable): the selected decal
 */
LRG_AVAILABLE_IN_ALL
LrgDecal * lrg_decal_set_get_active_decal (LrgDecalSet *self,
                                           guint        index);

/**
 * lrg_decal_set_get_cluster_decal_count:
 * @self: an #LrgDecalSet
 * @x: world X
 * @y: world Y
 * @z: world Z
 *
 * The decals the grid cell at (@x, @y, @z) lists, as the shader walks
 * them (0 outside the grid or without one).
 *
 * Returns: the cell's decal count
 */
LRG_AVAILABLE_IN_ALL
guint lrg_decal_set_get_cluster_decal_count (LrgDecalSet *self,
                                             gdouble      x,
                                             gdouble      y,
                                             gdouble      z);

/**
 * lrg_decal_set_get_cluster_decal:
 * @self: an #LrgDecalSet
 * @x: world X
 * @y: world Y
 * @z: world Z
 * @index: 0 .. cell count - 1, in application order
 *
 * Returns: (transfer none) (nullable): the cell's decal
 */
LRG_AVAILABLE_IN_ALL
LrgDecal * lrg_decal_set_get_cluster_decal (LrgDecalSet *self,
                                            gdouble      x,
                                            gdouble      y,
                                            gdouble      z,
                                            guint        index);

/**
 * lrg_decal_set_upload:
 * @self: an #LrgDecalSet
 *
 * Uploads the selection (decal data, grid, atlas tiles of new textures)
 * when it changed.  The binder calls it, so calling it yourself only
 * moves the work (atlas copies read textures back from the GPU) to a
 * time of your choosing.
 *
 * Returns: %TRUE when the GPU data is current
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_decal_set_upload (LrgDecalSet *self);

/**
 * lrg_decal_set_bind:
 * @self: an #LrgDecalSet
 * @shader_id: a GL program that contains lrg_decal_set_get_glsl()
 * @first_unit: first free texture unit
 *
 * Public form of the lit-shader binder: uploads if needed, binds the
 * data, grid and atlas textures from @first_unit on and turns
 * lrg_decals_apply() on.  Use it for your own shaders; the light set
 * calls the private binder itself.
 *
 * Returns: the next free texture unit
 */
LRG_AVAILABLE_IN_ALL
gint lrg_decal_set_bind (LrgDecalSet *self,
                         guint        shader_id,
                         gint         first_unit);

/**
 * lrg_decal_set_bind_inactive:
 * @shader_id: a GL program that contains lrg_decal_set_get_glsl()
 *
 * Turns lrg_decals_apply() off in @shader_id.
 */
LRG_AVAILABLE_IN_ALL
void lrg_decal_set_bind_inactive (guint shader_id);

/**
 * lrg_decal_set_get_glsl:
 *
 * The GLSL 330 hook pasted into the lit shader: its uniforms and
 * `void lrg_decals_apply(vec3 p, vec3 ng, inout vec4 albedo, inout vec3
 * n, inout float rough, inout float metal)`, which paints the decals
 * covering draw-space position @p (geometric normal @ng) over the
 * material's albedo, shading normal, roughness and metallic.
 *
 * Returns: (transfer none): the GLSL source
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_decal_set_get_glsl (void);

/**
 * lrg_decal_set_release:
 * @self: an #LrgDecalSet
 *
 * Frees the GPU textures (call before CloseWindow()); the next upload
 * makes new ones.
 */
LRG_AVAILABLE_IN_ALL
void lrg_decal_set_release (LrgDecalSet *self);

G_END_DECLS
