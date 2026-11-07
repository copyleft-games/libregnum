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
     *
     * Called by lrg_lighting_manager_render() while the light target is
     * bound, with a 2D camera that maps world units onto it and additive
     * blending (source x alpha + destination) active: draw the light's
     * contribution in world coordinates with graylib, using the colour
     * alpha as its weight.
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
 * Calculates the light's shadow geometry from the casters (the
 * #LrgLightingManager calls it every frame for shadow-casting lights).
 *
 * The base implementation projects the casters' edges away from the light
 * into shadow triangles: from the light's position to its reach (point
 * and spot lights: their radius) or along its direction by its shadow
 * length (#LrgDirectionalLight2D).  #LrgShadowOccluder2D casters project
 * only the edges facing away from the light unless they self-shadow.
 * With #LrgLight2D:shadow-softness above 0 the geometry is built once per
 * sample (#LrgLight2D:shadow-samples) from points spread over the light's
 * area (or directions spread over an angle), which renders as a soft
 * penumbra.  Read the result with lrg_light2d_get_shadow_triangles() and
 * lrg_light2d_get_shadow_factor().
 */
LRG_AVAILABLE_IN_ALL
void lrg_light2d_calculate_shadows (LrgLight2D *self,
                                    GPtrArray  *casters);

/**
 * lrg_light2d_get_shadow_samples:
 * @self: an #LrgLight2D
 *
 * Returns: how many shadow samples a soft light uses
 */
LRG_AVAILABLE_IN_ALL
guint lrg_light2d_get_shadow_samples (LrgLight2D *self);

/**
 * lrg_light2d_set_shadow_samples:
 * @self: an #LrgLight2D
 * @samples: shadow samples for soft shadows (1 to 32; each costs one
 *   extra light draw per frame)
 */
LRG_AVAILABLE_IN_ALL
void lrg_light2d_set_shadow_samples (LrgLight2D *self,
                                     guint       samples);

/**
 * lrg_light2d_get_shadow_sample_count:
 * @self: an #LrgLight2D
 *
 * Returns: the number of samples the last lrg_light2d_calculate_shadows()
 *   produced (0: no shadow geometry; 1: hard shadows)
 */
LRG_AVAILABLE_IN_ALL
guint lrg_light2d_get_shadow_sample_count (LrgLight2D *self);

/**
 * lrg_light2d_get_shadow_triangles:
 * @self: an #LrgLight2D
 * @sample: the sample index
 * @n_coords: (out): number of floats (6 per triangle)
 *
 * Gets the shadow triangles of one sample in world pixels.
 *
 * Returns: (array length=n_coords) (transfer none) (nullable): x, y
 *   triples of the triangles, or %NULL
 */
LRG_AVAILABLE_IN_ALL
const gfloat * lrg_light2d_get_shadow_triangles (LrgLight2D *self,
                                                 guint       sample,
                                                 guint      *n_coords);

/**
 * lrg_light2d_get_shadow_opacities:
 * @self: an #LrgLight2D
 * @sample: the sample index
 * @n_triangles: (out): number of triangles
 *
 * Returns: (array length=n_triangles) (transfer none) (nullable): the
 *   opacity of each triangle of the sample (its caster's opacity)
 */
LRG_AVAILABLE_IN_ALL
const gfloat * lrg_light2d_get_shadow_opacities (LrgLight2D *self,
                                                 guint       sample,
                                                 guint      *n_triangles);

/**
 * lrg_light2d_get_shadow_factor:
 * @self: an #LrgLight2D
 * @x: world x
 * @y: world y
 *
 * Gets how much of the light a point loses to shadows, from the last
 * lrg_light2d_calculate_shadows(): the average over the samples of the
 * strongest shadow covering the point.  Useful beyond rendering (stealth:
 * is the player in the dark?).
 *
 * Returns: 0 (fully lit) to 1 (fully shadowed)
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_light2d_get_shadow_factor (LrgLight2D *self,
                                      gfloat      x,
                                      gfloat      y);

/**
 * lrg_light2d_clear_shadows:
 * @self: an #LrgLight2D
 *
 * Drops the shadow geometry (the light renders unshadowed until the next
 * lrg_light2d_calculate_shadows()).
 */
LRG_AVAILABLE_IN_ALL
void lrg_light2d_clear_shadows (LrgLight2D *self);

G_END_DECLS
