/* lrg-horizon-ring.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgHorizonRing - a distant-terrain impostor: an annulus of
 * vertex-coloured, lit-normal terrain from an inner radius (where the
 * streamed terrain ends) to an outer one, sampled from a coarse height
 * and colour callback around a centre and merged into an
 * #LrgStaticBatch. Drawn with a lit, fogged shader (an #LrgLightSet's),
 * its far edge melts into the fog: aerial perspective. Building is
 * headless; only drawing needs GL.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <graylib.h>
#include "../lrg-version.h"
#include "../lrg-types.h"
#include "lrg-static-batch.h"

G_BEGIN_DECLS

/**
 * LrgHorizonSampleFunc:
 * @x: placement X of a ring vertex
 * @z: placement Z
 * @out_height: (out): the ground height there (draw-space Y + offset)
 * @out_rgba: (out caller-allocates) (array fixed-size=4): its colour
 * @user_data: the data given with the function
 *
 * Returns: %TRUE when there is ground at (@x, @z); %FALSE leaves the
 *   vertex at height 0 in the last colour (e.g. off the map)
 */
typedef gboolean (*LrgHorizonSampleFunc) (gdouble   x,
                                          gdouble   z,
                                          gfloat   *out_height,
                                          guint8   *out_rgba,
                                          gpointer  user_data);

#define LRG_TYPE_HORIZON_RING (lrg_horizon_ring_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgHorizonRing, lrg_horizon_ring, LRG, HORIZON_RING, GObject)

/**
 * lrg_horizon_ring_new:
 *
 * Creates an empty ring with radii 1000 .. 8000, 96 segments, 12 rings
 * and an inner sink of 0.
 *
 * Returns: (transfer full): a new #LrgHorizonRing
 */
LRG_AVAILABLE_IN_ALL
LrgHorizonRing *
lrg_horizon_ring_new (void);

/**
 * lrg_horizon_ring_set_radii:
 * @self: an #LrgHorizonRing
 * @inner: where the ring starts (> 0)
 * @outer: where it ends (> @inner)
 *
 * The rings between are spaced geometrically (denser near the inner edge).
 */
LRG_AVAILABLE_IN_ALL
void
lrg_horizon_ring_set_radii (LrgHorizonRing *self,
                            gfloat          inner,
                            gfloat          outer);

/**
 * lrg_horizon_ring_get_radii:
 * @self: an #LrgHorizonRing
 * @out_inner: (out) (optional): the inner radius
 * @out_outer: (out) (optional): the outer radius
 */
LRG_AVAILABLE_IN_ALL
void
lrg_horizon_ring_get_radii (LrgHorizonRing *self,
                            gfloat         *out_inner,
                            gfloat         *out_outer);

/**
 * lrg_horizon_ring_set_resolution:
 * @self: an #LrgHorizonRing
 * @segments: vertices around, 8 .. 1024
 * @rings: radial steps, 1 .. 64
 */
LRG_AVAILABLE_IN_ALL
void
lrg_horizon_ring_set_resolution (LrgHorizonRing *self,
                                 guint           segments,
                                 guint           rings);

/**
 * lrg_horizon_ring_set_sink:
 * @self: an #LrgHorizonRing
 * @sink: how far the inner edge is lowered (fading to none half way
 *   out), so the detailed terrain covers the seam
 */
LRG_AVAILABLE_IN_ALL
void
lrg_horizon_ring_set_sink (LrgHorizonRing *self,
                           gfloat          sink);

/**
 * lrg_horizon_ring_build:
 * @self: an #LrgHorizonRing
 * @centre_x: placement X of the centre
 * @centre_z: placement Z of the centre
 * @offset_x: draw X = placement X - @offset_x
 * @offset_y: draw Y = sampled height - @offset_y
 * @offset_z: draw Z = placement Z - @offset_z
 * @func: (scope call) (closure user_data): the height and colour sampler
 * @user_data: data for @func
 * @error: return location for a #GError
 *
 * Samples (segments) x (rings + 1) vertices and rebuilds the batch
 * (its GPU copy is dropped and uploads on the next draw).
 *
 * Returns: %TRUE when the ring was built
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_horizon_ring_build (LrgHorizonRing        *self,
                        gdouble                centre_x,
                        gdouble                centre_z,
                        gdouble                offset_x,
                        gdouble                offset_y,
                        gdouble                offset_z,
                        LrgHorizonSampleFunc   func,
                        gpointer               user_data,
                        GError               **error);

/**
 * lrg_horizon_ring_is_built:
 * @self: an #LrgHorizonRing
 *
 * Returns: %TRUE after a successful build
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_horizon_ring_is_built (LrgHorizonRing *self);

/**
 * lrg_horizon_ring_get_centre:
 * @self: an #LrgHorizonRing
 * @out_x: (out) (optional): placement X of the last build's centre
 * @out_z: (out) (optional): placement Z
 */
LRG_AVAILABLE_IN_ALL
void
lrg_horizon_ring_get_centre (LrgHorizonRing *self,
                             gdouble        *out_x,
                             gdouble        *out_z);

/**
 * lrg_horizon_ring_get_vertex_count:
 * @self: an #LrgHorizonRing
 *
 * Returns: the vertices of the last build (0 before one)
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_horizon_ring_get_vertex_count (LrgHorizonRing *self);

/**
 * lrg_horizon_ring_get_triangle_count:
 * @self: an #LrgHorizonRing
 *
 * Returns: the triangles of the last build
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_horizon_ring_get_triangle_count (LrgHorizonRing *self);

/**
 * lrg_horizon_ring_get_batch:
 * @self: an #LrgHorizonRing
 *
 * Returns: (transfer none): the ring's batch (material 0, layer 0)
 */
LRG_AVAILABLE_IN_ALL
LrgStaticBatch *
lrg_horizon_ring_get_batch (LrgHorizonRing *self);

/**
 * lrg_horizon_ring_draw:
 * @self: an #LrgHorizonRing
 * @shader: (nullable): the shader to draw with (an #LrgLightSet's for
 *   light and fog), or %NULL for raylib's default
 *
 * Uploads the batch when needed and draws it. Needs GL in 3D mode.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_horizon_ring_draw (LrgHorizonRing *self,
                       GrlShader      *shader);

/**
 * lrg_horizon_ring_release_gpu:
 * @self: an #LrgHorizonRing
 *
 * Unloads the batch while the context lives; the next draw uploads it.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_horizon_ring_release_gpu (LrgHorizonRing *self);

G_END_DECLS
