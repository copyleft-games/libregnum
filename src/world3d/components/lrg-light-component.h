/* lrg-light-component.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgLightComponent: a directional, point or spot light that registers an
 * #LrgLight3DDesc with its #LrgWorld3D from the owning entity's world
 * transform (on world entry, every late update and on property changes).
 * Colour, intensity, range, spot cone, shadows (with their own depth
 * bias) and the render layers it lights map onto #LrgLightSet
 * (lrg_light_component_add_to_light_set()).
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../../lrg-version.h"
#include "../../lrg-types.h"
#include "../../ecs/lrg-component.h"
#include "../lrg-world3d.h"
#include "../../graphics/lrg-light-set.h"

G_BEGIN_DECLS

#define LRG_TYPE_LIGHT_COMPONENT (lrg_light_component_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgLightComponent, lrg_light_component, LRG, LIGHT_COMPONENT, LrgComponent)

/**
 * lrg_light_component_new:
 *
 * Creates a white point light, intensity 1, range 10, no shadows.
 *
 * Returns: (transfer full): a new #LrgLightComponent
 */
LRG_AVAILABLE_IN_ALL
LrgLightComponent * lrg_light_component_new (void);

/**
 * lrg_light_component_new_with_kind:
 * @kind: directional, point or spot
 *
 * Creates a white light of @kind.
 *
 * Returns: (transfer full): a new #LrgLightComponent
 */
LRG_AVAILABLE_IN_ALL
LrgLightComponent * lrg_light_component_new_with_kind (LrgLight3DKind kind);

/**
 * lrg_light_component_get_kind:
 * @self: an #LrgLightComponent
 *
 * Returns: the light kind
 */
LRG_AVAILABLE_IN_ALL
LrgLight3DKind lrg_light_component_get_kind (LrgLightComponent *self);

/**
 * lrg_light_component_set_kind:
 * @self: an #LrgLightComponent
 * @kind: directional, point or spot
 *
 * Sets the light kind.
 */
LRG_AVAILABLE_IN_ALL
void lrg_light_component_set_kind (LrgLightComponent *self,
                                   LrgLight3DKind     kind);

/**
 * lrg_light_component_get_color:
 * @self: an #LrgLightComponent
 * @out: (out caller-allocates) (array fixed-size=3): linear RGB, 0 .. 1
 *
 * Gets the light colour.
 */
LRG_AVAILABLE_IN_ALL
void lrg_light_component_get_color (LrgLightComponent *self,
                                    gfloat            *out);

/**
 * lrg_light_component_set_color:
 * @self: an #LrgLightComponent
 * @r: red, 0 .. 1
 * @g: green, 0 .. 1
 * @b: blue, 0 .. 1
 *
 * Sets the light colour (#LrgLightComponent:color-r .. color-b).
 */
LRG_AVAILABLE_IN_ALL
void lrg_light_component_set_color (LrgLightComponent *self,
                                    gfloat             r,
                                    gfloat             g,
                                    gfloat             b);

/**
 * lrg_light_component_get_intensity:
 * @self: an #LrgLightComponent
 *
 * Returns: the intensity multiplier
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_light_component_get_intensity (LrgLightComponent *self);

/**
 * lrg_light_component_set_intensity:
 * @self: an #LrgLightComponent
 * @intensity: multiplier on the colour (>= 0)
 *
 * Sets the intensity.
 */
LRG_AVAILABLE_IN_ALL
void lrg_light_component_set_intensity (LrgLightComponent *self,
                                        gfloat             intensity);

/**
 * lrg_light_component_get_range:
 * @self: an #LrgLightComponent
 *
 * Returns: the attenuation range of point and spot lights
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_light_component_get_range (LrgLightComponent *self);

/**
 * lrg_light_component_set_range:
 * @self: an #LrgLightComponent
 * @range: attenuation range in world units (> 0)
 *
 * Sets the reach of a point or spot light.
 */
LRG_AVAILABLE_IN_ALL
void lrg_light_component_set_range (LrgLightComponent *self,
                                    gfloat             range);

/**
 * lrg_light_component_set_spot_angles:
 * @self: an #LrgLightComponent
 * @inner_degrees: half-angle of full intensity, degrees
 * @outer_degrees: half-angle where the light reaches zero, degrees
 *   (clamped to at least @inner_degrees and at most 89)
 *
 * Sets the spot cone (#LrgLightComponent:inner-angle and
 * #LrgLightComponent:outer-angle).
 */
LRG_AVAILABLE_IN_ALL
void lrg_light_component_set_spot_angles (LrgLightComponent *self,
                                          gfloat             inner_degrees,
                                          gfloat             outer_degrees);

/**
 * lrg_light_component_get_cast_shadows:
 * @self: an #LrgLightComponent
 *
 * Returns: %TRUE if the renderer should render a shadow map for it
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_light_component_get_cast_shadows (LrgLightComponent *self);

/**
 * lrg_light_component_set_cast_shadows:
 * @self: an #LrgLightComponent
 * @cast_shadows: whether the light casts shadows
 *
 * Sets #LrgLightComponent:cast-shadows.
 */
LRG_AVAILABLE_IN_ALL
void lrg_light_component_set_cast_shadows (LrgLightComponent *self,
                                           gboolean           cast_shadows);

/**
 * lrg_light_component_get_spot_angles:
 * @self: an #LrgLightComponent
 * @out_inner_degrees: (out) (optional): the inner half-angle (never
 *   above the outer one)
 * @out_outer_degrees: (out) (optional): the outer half-angle
 *
 * Gets the spot cone.
 */
LRG_AVAILABLE_IN_ALL
void lrg_light_component_get_spot_angles (LrgLightComponent *self,
                                          gfloat            *out_inner_degrees,
                                          gfloat            *out_outer_degrees);

/**
 * lrg_light_component_get_shadow_bias:
 * @self: an #LrgLightComponent
 *
 * Returns: the shadow depth bias in world units, 0 for the renderer's
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_light_component_get_shadow_bias (LrgLightComponent *self);

/**
 * lrg_light_component_set_shadow_bias:
 * @self: an #LrgLightComponent
 * @bias: depth bias in world units (0 .. 100), 0 for the shadow
 *   renderer's
 *
 * Sets #LrgLightComponent:shadow-bias.
 */
LRG_AVAILABLE_IN_ALL
void lrg_light_component_set_shadow_bias (LrgLightComponent *self,
                                          gfloat             bias);

/**
 * lrg_light_component_get_layer_mask:
 * @self: an #LrgLightComponent
 *
 * Returns: the render layers the light illuminates
 */
LRG_AVAILABLE_IN_ALL
guint32 lrg_light_component_get_layer_mask (LrgLightComponent *self);

/**
 * lrg_light_component_set_layer_mask:
 * @self: an #LrgLightComponent
 * @mask: bit N lights render layer N
 *
 * Sets #LrgLightComponent:layer-mask.
 */
LRG_AVAILABLE_IN_ALL
void lrg_light_component_set_layer_mask (LrgLightComponent *self,
                                         guint32            mask);

/**
 * lrg_light_component_add_to_light_set:
 * @self: an #LrgLightComponent
 * @lights: the light set to add the light to
 *
 * Adds the light as it is now (lrg_light_component_fill_desc() then
 * lrg_light_set_add_light_desc()): a directional light sets @lights'
 * directional light, a point or spot light becomes a new light with its
 * cone, layers, shadow priority 1 when it casts shadows and its shadow
 * bias.
 *
 * Returns: the light id, or %LRG_LIGHT_SET_INVALID_LIGHT for a
 *   directional or disabled light
 */
LRG_AVAILABLE_IN_ALL
guint lrg_light_component_add_to_light_set (LrgLightComponent *self,
                                            LrgLightSet       *lights);

/**
 * lrg_light_component_fill_desc:
 * @self: an #LrgLightComponent
 * @out: (out caller-allocates): the description to fill
 *
 * Builds the renderer-facing description from the properties and, when
 * the component has an entity, its world position and forward (-Z)
 * direction.  @out->enabled follows the component's enabled state and
 * the entity's active-in-hierarchy state.
 */
LRG_AVAILABLE_IN_ALL
void lrg_light_component_fill_desc (LrgLightComponent *self,
                                    LrgLight3DDesc    *out);

/**
 * lrg_light_component_sync:
 * @self: an #LrgLightComponent
 *
 * Re-registers the light with the entity's world now (late_update() does
 * this every frame).  Does nothing outside a world.
 */
LRG_AVAILABLE_IN_ALL
void lrg_light_component_sync (LrgLightComponent *self);

/**
 * lrg_light_component_get_bake_mode:
 * @self: an #LrgLightComponent
 *
 * Returns: #LrgLightComponent:bake-mode
 */
LRG_AVAILABLE_IN_ALL
LrgLightMode lrg_light_component_get_bake_mode (LrgLightComponent *self);

/**
 * lrg_light_component_set_bake_mode:
 * @self: an #LrgLightComponent
 * @mode: the bake mode
 *
 * Sets #LrgLightComponent:bake-mode.
 */
LRG_AVAILABLE_IN_ALL
void lrg_light_component_set_bake_mode (LrgLightComponent *self,
                                        LrgLightMode       mode);

G_END_DECLS
