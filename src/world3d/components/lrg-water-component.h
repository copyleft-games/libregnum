/* lrg-water-component.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgWaterComponent: draws an #LrgWaterBody in a world.
 *
 * The body's surface is built in world space from the entity's position
 * (translation only: water stays level) and drawn with an
 * #LrgWaterSurface in the renderer's transparent pass, after a copy of
 * the frame drawn so far is captured for screen-space reflection and
 * refraction, lit by the renderer's light set and sun.  Waves run on the
 * component's clock (advanced by update).  The level node kind `water`
 * instantiates it with the `.rwater` as its asset.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../../lrg-version.h"
#include "../../lrg-types.h"
#include "../../ecs/lrg-component.h"
#include "../../graphics/lrg-water-body.h"
#include "../../graphics/lrg-water-surface.h"

G_BEGIN_DECLS

#define LRG_TYPE_WATER_COMPONENT (lrg_water_component_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgWaterComponent, lrg_water_component, LRG, WATER_COMPONENT, LrgComponent)

/**
 * lrg_water_component_new:
 *
 * Creates a component with a default plane body (see
 * lrg_water_body_new()).
 *
 * Returns: (transfer full): a new #LrgWaterComponent
 */
LRG_AVAILABLE_IN_ALL
LrgWaterComponent * lrg_water_component_new (void);

/**
 * lrg_water_component_get_body:
 * @self: an #LrgWaterComponent
 *
 * Returns: (transfer none): the water body (the explicit one, the one
 *   loaded from #LrgWaterComponent:body-path, or the default plane)
 */
LRG_AVAILABLE_IN_ALL
LrgWaterBody * lrg_water_component_get_body (LrgWaterComponent *self);

/**
 * lrg_water_component_set_body:
 * @self: an #LrgWaterComponent
 * @body: (nullable): the body, %NULL for the default plane
 */
LRG_AVAILABLE_IN_ALL
void lrg_water_component_set_body (LrgWaterComponent *self,
                                   LrgWaterBody      *body);

/**
 * lrg_water_component_get_body_path:
 * @self: an #LrgWaterComponent
 *
 * Returns: (nullable): the `.rwater` path
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_water_component_get_body_path (LrgWaterComponent *self);

/**
 * lrg_water_component_set_body_path:
 * @self: an #LrgWaterComponent
 * @path: (nullable): a `.rwater` file, loaded the first time the body is
 *   needed (a failure is logged once and the default plane stays)
 */
LRG_AVAILABLE_IN_ALL
void lrg_water_component_set_body_path (LrgWaterComponent *self,
                                        const gchar       *path);

/**
 * lrg_water_component_get_surface:
 * @self: an #LrgWaterComponent
 *
 * Returns: (transfer none): the surface the component draws (built from
 *   the body when it or the entity's position changes)
 */
LRG_AVAILABLE_IN_ALL
LrgWaterSurface * lrg_water_component_get_surface (LrgWaterComponent *self);

/**
 * lrg_water_component_rebuild:
 * @self: an #LrgWaterComponent
 *
 * Rebuilds the surface from the body now (call after editing the body
 * in place; setters on the component do it themselves).
 *
 * Returns: %TRUE when the body built at least one patch
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_water_component_rebuild (LrgWaterComponent *self);

/**
 * lrg_water_component_get_time:
 * @self: an #LrgWaterComponent
 *
 * Returns: the wave clock in seconds
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_water_component_get_time (LrgWaterComponent *self);

/**
 * lrg_water_component_set_time:
 * @self: an #LrgWaterComponent
 * @time: the wave clock in seconds
 */
LRG_AVAILABLE_IN_ALL
void lrg_water_component_set_time (LrgWaterComponent *self,
                                   gfloat             time);

/**
 * lrg_water_component_get_capture:
 * @self: an #LrgWaterComponent
 *
 * Returns: whether the draw captures the frame for screen-space
 *   reflection and refraction
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_water_component_get_capture (LrgWaterComponent *self);

/**
 * lrg_water_component_set_capture:
 * @self: an #LrgWaterComponent
 * @capture: %TRUE (default) to capture the frame; %FALSE blends with the
 *   body's own colours only (cheaper)
 */
LRG_AVAILABLE_IN_ALL
void lrg_water_component_set_capture (LrgWaterComponent *self,
                                      gboolean           capture);

/**
 * lrg_water_component_level_at:
 * @self: an #LrgWaterComponent
 * @x: world X
 * @z: world Z
 * @out_height: (out) (optional): the world height of the water surface
 *   there, waves included
 *
 * Returns: %TRUE inside the body's outline
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_water_component_level_at (LrgWaterComponent *self,
                                       gfloat             x,
                                       gfloat             z,
                                       gfloat            *out_height);

G_END_DECLS
