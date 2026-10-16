/* lrg-billboard-batch.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Batched camera-facing textured quads (billboards / "flats").
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <graylib.h>
#include "../lrg-version.h"
#include "../lrg-types.h"
#include "../lrg-enums.h"
#include "lrg-light-set.h"

G_BEGIN_DECLS

/**
 * LRG_BILLBOARD_BATCH_DEFAULT_CUTOFF:
 *
 * Default alpha below which billboard texels are discarded.
 */
#define LRG_BILLBOARD_BATCH_DEFAULT_CUTOFF (0.5f)

#define LRG_TYPE_BILLBOARD_BATCH (lrg_billboard_batch_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgBillboardBatch, lrg_billboard_batch, LRG, BILLBOARD_BATCH, GObject)

/**
 * lrg_billboard_batch_new:
 *
 * Creates an empty batch in %LRG_BILLBOARD_MODE_AXIS_Y.
 *
 * Returns: (transfer full): a new #LrgBillboardBatch
 */
LRG_AVAILABLE_IN_ALL
LrgBillboardBatch *
lrg_billboard_batch_new (void);

/**
 * lrg_billboard_batch_set_mode:
 * @self: an #LrgBillboardBatch
 * @mode: orientation mode for every billboard of the batch
 */
LRG_AVAILABLE_IN_ALL
void
lrg_billboard_batch_set_mode (LrgBillboardBatch *self,
                              LrgBillboardMode   mode);

/**
 * lrg_billboard_batch_get_mode:
 * @self: an #LrgBillboardBatch
 *
 * Returns: the orientation mode
 */
LRG_AVAILABLE_IN_ALL
LrgBillboardMode
lrg_billboard_batch_get_mode (LrgBillboardBatch *self);

/**
 * lrg_billboard_batch_add:
 * @self: an #LrgBillboardBatch
 * @material: material id selecting the texture (see
 *   lrg_billboard_batch_set_material_texture())
 * @position: world position of the pivot
 * @width: quad width in world units (> 0)
 * @height: quad height in world units (> 0)
 *
 * Adds a visible billboard pivoted at its bottom centre (pivot 0.5, 0),
 * mapping the whole texture, untinted.  Headless.
 *
 * Returns: the billboard index (stable until lrg_billboard_batch_clear())
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_billboard_batch_add (LrgBillboardBatch *self,
                         guint              material,
                         const GrlVector3  *position,
                         gfloat             width,
                         gfloat             height);

/**
 * lrg_billboard_batch_clear:
 * @self: an #LrgBillboardBatch
 *
 * Removes every billboard.  Material textures are kept.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_billboard_batch_clear (LrgBillboardBatch *self);

/**
 * lrg_billboard_batch_get_count:
 * @self: an #LrgBillboardBatch
 *
 * Returns: the number of billboards, visible or not
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_billboard_batch_get_count (LrgBillboardBatch *self);

/**
 * lrg_billboard_batch_set_position:
 * @self: an #LrgBillboardBatch
 * @index: a billboard index
 * @position: new pivot position
 */
LRG_AVAILABLE_IN_ALL
void
lrg_billboard_batch_set_position (LrgBillboardBatch *self,
                                  guint              index,
                                  const GrlVector3  *position);

/**
 * lrg_billboard_batch_set_size:
 * @self: an #LrgBillboardBatch
 * @index: a billboard index
 * @width: width in world units (> 0)
 * @height: height in world units (> 0)
 */
LRG_AVAILABLE_IN_ALL
void
lrg_billboard_batch_set_size (LrgBillboardBatch *self,
                              guint              index,
                              gfloat             width,
                              gfloat             height);

/**
 * lrg_billboard_batch_set_pivot:
 * @self: an #LrgBillboardBatch
 * @index: a billboard index
 * @pivot_x: horizontal pivot, 0 = left edge, 1 = right edge
 * @pivot_y: vertical pivot, 0 = bottom edge, 1 = top edge
 *
 * Moves the point of the quad that sits on the billboard position.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_billboard_batch_set_pivot (LrgBillboardBatch *self,
                               guint              index,
                               gfloat             pivot_x,
                               gfloat             pivot_y);

/**
 * lrg_billboard_batch_set_uv:
 * @self: an #LrgBillboardBatch
 * @index: a billboard index
 * @uv: (nullable): normalized texture rectangle (x, y = top-left), or
 *   %NULL for the whole texture.  A negative width mirrors horizontally.
 *
 * Selects the texture region drawn, e.g. one frame of an atlas.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_billboard_batch_set_uv (LrgBillboardBatch  *self,
                            guint               index,
                            const GrlRectangle *uv);

/**
 * lrg_billboard_batch_set_material:
 * @self: an #LrgBillboardBatch
 * @index: a billboard index
 * @material: new material id (e.g. the next animation frame)
 */
LRG_AVAILABLE_IN_ALL
void
lrg_billboard_batch_set_material (LrgBillboardBatch *self,
                                  guint              index,
                                  guint              material);

/**
 * lrg_billboard_batch_set_tint:
 * @self: an #LrgBillboardBatch
 * @index: a billboard index
 * @tint: (nullable): multiplied colour, %NULL for white
 */
LRG_AVAILABLE_IN_ALL
void
lrg_billboard_batch_set_tint (LrgBillboardBatch *self,
                              guint              index,
                              const GrlColor    *tint);

/**
 * lrg_billboard_batch_set_visible:
 * @self: an #LrgBillboardBatch
 * @index: a billboard index
 * @visible: whether the billboard is drawn
 */
LRG_AVAILABLE_IN_ALL
void
lrg_billboard_batch_set_visible (LrgBillboardBatch *self,
                                 guint              index,
                                 gboolean           visible);

/**
 * lrg_billboard_batch_get_corners:
 * @self: an #LrgBillboardBatch
 * @index: a billboard index
 * @camera_position: the eye position
 * @camera_right: (nullable): camera right vector, required for
 *   %LRG_BILLBOARD_MODE_SPHERICAL
 * @camera_up: (nullable): camera up vector, required for
 *   %LRG_BILLBOARD_MODE_SPHERICAL
 * @corners: (out caller-allocates) (array fixed-size=4): bottom-left,
 *   bottom-right, top-right, top-left world positions as seen from the
 *   camera
 *
 * Computes the quad exactly as lrg_billboard_batch_draw() would.
 * Headless; useful for picking and tests.
 *
 * Returns: %TRUE when @index exists
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_billboard_batch_get_corners (LrgBillboardBatch *self,
                                 guint              index,
                                 const GrlVector3  *camera_position,
                                 const GrlVector3  *camera_right,
                                 const GrlVector3  *camera_up,
                                 GrlVector3        *corners);

/**
 * lrg_billboard_batch_set_material_texture:
 * @self: an #LrgBillboardBatch
 * @material: material id
 * @texture: (nullable): texture for @material, %NULL to unset
 *
 * The batch keeps a reference.  Materials without a texture are not
 * drawn.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_billboard_batch_set_material_texture (LrgBillboardBatch *self,
                                          guint              material,
                                          GrlTexture        *texture);

/**
 * lrg_billboard_batch_set_alpha_cutoff:
 * @self: an #LrgBillboardBatch
 * @cutoff: texels with alpha below this are discarded (0 disables the
 *   cutout shader and draws with plain alpha blending)
 *
 * Cutout drawing keeps transparent texels out of the depth buffer, so
 * overlapping billboards sort correctly without depth ordering.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_billboard_batch_set_alpha_cutoff (LrgBillboardBatch *self,
                                      gfloat             cutoff);

/**
 * lrg_billboard_batch_set_light_set:
 * @self: an #LrgBillboardBatch
 * @light_set: (nullable): lights for this batch, %NULL to follow the
 *   bound light set (lrg_light_set_bind())
 *
 * Lit billboards are tinted per quad by the light reaching their centre
 * (lrg_light_set_sample() without a normal, after the current rlgl
 * transform), multiplied with their own tint.  When the light set has
 * fog (lrg_light_set_set_fog()) every quad, emissive ones included, is
 * fogged per fragment like the static geometry.  The batch keeps a
 * reference.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_billboard_batch_set_light_set (LrgBillboardBatch *self,
                                   LrgLightSet       *light_set);

/**
 * lrg_billboard_batch_get_light_set:
 * @self: an #LrgBillboardBatch
 *
 * Returns: (transfer none) (nullable): the batch's own light set
 */
LRG_AVAILABLE_IN_ALL
LrgLightSet *
lrg_billboard_batch_get_light_set (LrgBillboardBatch *self);

/**
 * lrg_billboard_batch_set_lit:
 * @self: an #LrgBillboardBatch
 * @lit: %FALSE to never light this batch (UI, sky, effects)
 *
 * Batches are lit by default whenever a light set applies.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_billboard_batch_set_lit (LrgBillboardBatch *self,
                             gboolean           lit);

/**
 * lrg_billboard_batch_get_lit:
 * @self: an #LrgBillboardBatch
 *
 * Returns: whether the batch may be lit
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_billboard_batch_get_lit (LrgBillboardBatch *self);

/**
 * lrg_billboard_batch_set_emissive:
 * @self: an #LrgBillboardBatch
 * @index: a billboard index
 * @emissive: %TRUE for a billboard that shines by itself (flames,
 *   lamps): it keeps its own tint under any light
 */
LRG_AVAILABLE_IN_ALL
void
lrg_billboard_batch_set_emissive (LrgBillboardBatch *self,
                                  guint              index,
                                  gboolean           emissive);

/**
 * lrg_billboard_batch_get_emissive:
 * @self: an #LrgBillboardBatch
 * @index: a billboard index
 *
 * Returns: whether the billboard is emissive (%FALSE for a bad index)
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_billboard_batch_get_emissive (LrgBillboardBatch *self,
                                  guint              index);

/**
 * lrg_billboard_batch_get_lit_tint:
 * @self: an #LrgBillboardBatch
 * @index: a billboard index
 * @out_tint: (out caller-allocates): the colour the next draw gives the
 *   quad, with an identity rlgl transform
 *
 * Headless: the billboard's tint times the light of the applicable
 * light set (the batch's own, else the bound one) at its centre,
 * clamped to 255; the plain tint when the batch is unlit, the billboard
 * emissive or no light set applies.
 *
 * Returns: %TRUE when @index exists
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_billboard_batch_get_lit_tint (LrgBillboardBatch *self,
                                  guint              index,
                                  GrlColor          *out_tint);

/**
 * lrg_billboard_batch_draw:
 * @self: an #LrgBillboardBatch
 * @camera_position: the eye position of the active 3D camera
 *
 * Draws every visible billboard with a texture, one texture bind per
 * material.  Call between BeginMode3D()/EndMode3D() (or
 * grl_camera3d_begin()/end()).  Spherical mode reads the camera basis
 * from the current modelview matrix.  With a light set (own or bound)
 * every non-emissive quad is tinted by the light at its centre, and the
 * light set's fog applies to every quad (the cutout shader is used for
 * it, with the cutoff at 0 if none was set).
 *
 * A batch of at least 64 billboards that has been drawn twice without
 * additions keeps its unchanged billboards in GPU vertex buffers and
 * turns and lights them in a vertex shader (same pixels, one draw call
 * per material); billboards changed afterwards are drawn the immediate
 * way.  See docs/modules/graphics/billboard-batch.org.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_billboard_batch_draw (LrgBillboardBatch *self,
                          const GrlVector3  *camera_position);

G_END_DECLS
