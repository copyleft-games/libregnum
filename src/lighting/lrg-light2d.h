/* lrg-light2d.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Base 2D light class.
 *
 * Abstract base class for all 2D light types.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-enums.h"

G_BEGIN_DECLS

#define LRG_TYPE_LIGHT2D (lrg_light2d_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_DERIVABLE_TYPE (LrgLight2D, lrg_light2d, LRG, LIGHT2D, GObject)

/**
 * LrgLight2DClass:
 * @parent_class: parent class
 * @render: render the light to a target
 * @is_visible: check if light is visible in viewport
 * @update: update light state
 * @calculate_shadows: calculate shadow geometry
 *
 * Virtual methods for #LrgLight2D.
 */
struct _LrgLight2DClass
{
    GObjectClass parent_class;

    /**
     * LrgLight2DClass::render:
     * @self: an #LrgLight2D
     * @target_id: render target texture ID
     * @width: target width
     * @height: target height
     *
     * Renders the light to a render target.
     */
    void (*render) (LrgLight2D *self,
                    guint       target_id,
                    guint       width,
                    guint       height);

    /**
     * LrgLight2DClass::is_visible:
     * @self: an #LrgLight2D
     * @viewport_x: viewport X
     * @viewport_y: viewport Y
     * @viewport_width: viewport width
     * @viewport_height: viewport height
     *
     * Checks if the light is visible in the given viewport.
     *
     * Returns: %TRUE if visible
     */
    gboolean (*is_visible) (LrgLight2D *self,
                            gfloat      viewport_x,
                            gfloat      viewport_y,
                            gfloat      viewport_width,
                            gfloat      viewport_height);

    /**
     * LrgLight2DClass::update:
     * @self: an #LrgLight2D
     * @delta_time: time elapsed in seconds
     *
     * Updates the light state.
     */
    void (*update) (LrgLight2D *self,
                    gfloat      delta_time);

    /**
     * LrgLight2DClass::calculate_shadows:
     * @self: an #LrgLight2D
     * @casters: (element-type LrgShadowCaster): array of shadow casters
     *
     * Calculates shadow geometry for the given casters.
     */
    void (*calculate_shadows) (LrgLight2D *self,
                               GPtrArray  *casters);

    gpointer _reserved[8];
};

/* Position */

/**
 * lrg_light2d_get_position:
 * @self: an #LrgLight2D
 * @x: (out) (nullable): X position
 * @y: (out) (nullable): Y position
 *
 * Gets the light position.
 */
LRG_AVAILABLE_IN_ALL
void lrg_light2d_get_position (LrgLight2D *self,
                               gfloat     *x,
                               gfloat     *y);

/**
 * lrg_light2d_set_position:
 * @self: an #LrgLight2D
 * @x: X position
 * @y: Y position
 *
 * Sets the light position.
 */
LRG_AVAILABLE_IN_ALL
void lrg_light2d_set_position (LrgLight2D *self,
                               gfloat      x,
                               gfloat      y);

/* Color */

/**
 * lrg_light2d_get_color:
 * @self: an #LrgLight2D
 * @r: (out) (nullable): red component
 * @g: (out) (nullable): green component
 * @b: (out) (nullable): blue component
 *
 * Gets the light color.
 */
LRG_AVAILABLE_IN_ALL
void lrg_light2d_get_color (LrgLight2D *self,
                            guint8     *r,
                            guint8     *g,
                            guint8     *b);

/**
 * lrg_light2d_set_color:
 * @self: an #LrgLight2D
 * @r: red component
 * @g: green component
 * @b: blue component
 *
 * Sets the light color.
 */
LRG_AVAILABLE_IN_ALL
void lrg_light2d_set_color (LrgLight2D *self,
                            guint8      r,
                            guint8      g,
                            guint8      b);

/* Intensity */

/**
 * lrg_light2d_get_intensity:
 * @self: an #LrgLight2D
 *
 * Gets the light intensity.
 *
 * Returns: Light intensity (0.0 to unlimited)
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_light2d_get_intensity (LrgLight2D *self);

/**
 * lrg_light2d_set_intensity:
 * @self: an #LrgLight2D
 * @intensity: light intensity
 *
 * Sets the light intensity.
 */
LRG_AVAILABLE_IN_ALL
void lrg_light2d_set_intensity (LrgLight2D *self,
                                gfloat      intensity);

/* Enable/disable */

/**
 * lrg_light2d_get_enabled:
 * @self: an #LrgLight2D
 *
 * Gets whether the light is enabled.
 *
 * Returns: %TRUE if enabled
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_light2d_get_enabled (LrgLight2D *self);

/**
 * lrg_light2d_set_enabled:
 * @self: an #LrgLight2D
 * @enabled: whether to enable the light
 *
 * Enables or disables the light.
 */
LRG_AVAILABLE_IN_ALL
void lrg_light2d_set_enabled (LrgLight2D *self,
                              gboolean    enabled);

/* Shadow settings */

/**
 * lrg_light2d_get_casts_shadows:
 * @self: an #LrgLight2D
 *
 * Gets whether the light casts shadows.
 *
 * Returns: %TRUE if casting shadows
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_light2d_get_casts_shadows (LrgLight2D *self);

/**
 * lrg_light2d_set_casts_shadows:
 * @self: an #LrgLight2D
 * @casts_shadows: whether to cast shadows
 *
 * Sets whether the light casts shadows.
 */
LRG_AVAILABLE_IN_ALL
void lrg_light2d_set_casts_shadows (LrgLight2D *self,
                                    gboolean    casts_shadows);

/**
 * lrg_light2d_get_shadow_method:
 * @self: an #LrgLight2D
 *
 * Gets the shadow calculation method.
 *
 * Returns: Shadow method
 */
LRG_AVAILABLE_IN_ALL
LrgShadowMethod lrg_light2d_get_shadow_method (LrgLight2D *self);

/**
 * lrg_light2d_set_shadow_method:
 * @self: an #LrgLight2D
 * @method: shadow method
 *
 * Sets the shadow calculation method.
 */
LRG_AVAILABLE_IN_ALL
void lrg_light2d_set_shadow_method (LrgLight2D      *self,
                                    LrgShadowMethod  method);

/**
 * lrg_light2d_get_shadow_softness:
 * @self: an #LrgLight2D
 *
 * Gets the shadow softness (blur amount).
 *
 * Returns: Shadow softness (0.0 to 1.0)
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_light2d_get_shadow_softness (LrgLight2D *self);

/**
 * lrg_light2d_set_shadow_softness:
 * @self: an #LrgLight2D
 * @softness: shadow softness
 *
 * Sets the shadow softness.
 */
LRG_AVAILABLE_IN_ALL
void lrg_light2d_set_shadow_softness (LrgLight2D *self,
                                      gfloat      softness);

/* Falloff */

/**
 * lrg_light2d_get_falloff:
 * @self: an #LrgLight2D
 *
 * Gets the light falloff type.
 *
 * Returns: Falloff type
 */
LRG_AVAILABLE_IN_ALL
LrgLightFalloff lrg_light2d_get_falloff (LrgLight2D *self);

/**
 * lrg_light2d_set_falloff:
 * @self: an #LrgLight2D
 * @falloff: falloff type
 *
 * Sets the light falloff type.
 */
LRG_AVAILABLE_IN_ALL
void lrg_light2d_set_falloff (LrgLight2D      *self,
                              LrgLightFalloff  falloff);

/* Blend mode */

/**
 * lrg_light2d_get_blend_mode:
 * @self: an #LrgLight2D
 *
 * Gets the light blend mode.
 *
 * Returns: Blend mode
 */
LRG_AVAILABLE_IN_ALL
LrgLightBlendMode lrg_light2d_get_blend_mode (LrgLight2D *self);

/**
 * lrg_light2d_set_blend_mode:
 * @self: an #LrgLight2D
 * @mode: blend mode
 *
 * Sets the light blend mode.
 */
LRG_AVAILABLE_IN_ALL
void lrg_light2d_set_blend_mode (LrgLight2D        *self,
                                 LrgLightBlendMode  mode);

/* Layer */

/**
 * lrg_light2d_get_layer:
 * @self: an #LrgLight2D
 *
 * Gets the rendering layer.
 *
 * Returns: Render layer
 */
LRG_AVAILABLE_IN_ALL
gint lrg_light2d_get_layer (LrgLight2D *self);

/**
 * lrg_light2d_set_layer:
 * @self: an #LrgLight2D
 * @layer: render layer
 *
 * Sets the rendering layer.
 */
LRG_AVAILABLE_IN_ALL
void lrg_light2d_set_layer (LrgLight2D *self,
                            gint        layer);

/* Virtual method wrappers */

/**
 * lrg_light2d_render:
 * @self: an #LrgLight2D
 * @target_id: render target texture ID
 * @width: target width
 * @height: target height
 *
 * Renders the light to a render target.
 */
LRG_AVAILABLE_IN_ALL
void lrg_light2d_render (LrgLight2D *self,
                         guint       target_id,
                         guint       width,
                         guint       height);

/**
 * lrg_light2d_is_visible:
 * @self: an #LrgLight2D
 * @viewport_x: viewport X
 * @viewport_y: viewport Y
 * @viewport_width: viewport width
 * @viewport_height: viewport height
 *
 * Checks if the light is visible.
 *
 * Returns: %TRUE if visible
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_light2d_is_visible (LrgLight2D *self,
                                 gfloat      viewport_x,
                                 gfloat      viewport_y,
                                 gfloat      viewport_width,
                                 gfloat      viewport_height);

/**
 * lrg_light2d_update:
 * @self: an #LrgLight2D
 * @delta_time: time elapsed
 *
 * Updates the light state.
 */
LRG_AVAILABLE_IN_ALL
void lrg_light2d_update (LrgLight2D *self,
                         gfloat      delta_time);

/**
 * lrg_light2d_calculate_shadows:
 * @self: an #LrgLight2D
 * @casters: (element-type LrgShadowCaster): array of shadow casters
 *
 * Calculates shadow geometry.
 */
LRG_AVAILABLE_IN_ALL
void lrg_light2d_calculate_shadows (LrgLight2D *self,
                                    GPtrArray  *casters);

G_END_DECLS
