/* lrg-light-set.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgLightSet - forward lighting for static 3D geometry: an ambient
 * colour, one directional light and any number of point lights, of
 * which the ones nearest the camera (up to LRG_LIGHT_SET_MAX_ACTIVE) are
 * selected each frame and uploaded to a GLSL 330 shader usable with
 * LrgStaticBatch and LrgTerrain. Light bookkeeping and selection are
 * headless; only the shader needs GL.
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

#define LRG_TYPE_LIGHT_SET (lrg_light_set_get_type ())

/**
 * LRG_LIGHT_SET_MAX_ACTIVE:
 *
 * Point lights the shader evaluates per fragment (the size of its
 * uniform arrays).
 */
#define LRG_LIGHT_SET_MAX_ACTIVE (16)

/**
 * LRG_LIGHT_SET_INVALID_LIGHT:
 *
 * Returned for a light id that does not exist.
 */
#define LRG_LIGHT_SET_INVALID_LIGHT (G_MAXUINT)

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgLightSet, lrg_light_set, LRG, LIGHT_SET, GObject)

/**
 * lrg_light_set_new:
 *
 * Creates a light set with ambient (64, 64, 64), no directional light,
 * no point lights, %LRG_LIGHT_SET_MAX_ACTIVE active lights and the alpha
 * cutout off.
 *
 * Returns: (transfer full): a new #LrgLightSet
 */
LRG_AVAILABLE_IN_ALL
LrgLightSet *
lrg_light_set_new (void);

/**
 * lrg_light_set_set_ambient:
 * @self: an #LrgLightSet
 * @color: light reaching every surface (alpha ignored)
 */
LRG_AVAILABLE_IN_ALL
void
lrg_light_set_set_ambient (LrgLightSet    *self,
                           const GrlColor *color);

/**
 * lrg_light_set_set_directional:
 * @self: an #LrgLightSet
 * @direction: (nullable): direction the light travels (e.g. the negated
 *   sun direction of an #LrgSkyDome); normalized internally
 * @color: (nullable): light colour (alpha ignored); %NULL or a %NULL
 *   @direction turns the directional light off
 */
LRG_AVAILABLE_IN_ALL
void
lrg_light_set_set_directional (LrgLightSet      *self,
                               const GrlVector3 *direction,
                               const GrlColor   *color);

/**
 * lrg_light_set_set_origin:
 * @self: an #LrgLightSet
 * @x: origin X
 * @y: origin Y
 * @z: origin Z
 *
 * Floating origin: light positions are uploaded relative to it, matching
 * geometry drawn origin-relative (lrg_terrain_set_origin()). Selection
 * keeps using world coordinates.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_light_set_set_origin (LrgLightSet *self,
                          gdouble      x,
                          gdouble      y,
                          gdouble      z);

/**
 * lrg_light_set_add_point_light:
 * @self: an #LrgLightSet
 * @x: world X
 * @y: world Y
 * @z: world Z
 * @color: light colour (alpha ignored)
 * @radius: distance at which the light fades to nothing (> 0)
 *
 * Adds an enabled point light of intensity 1. Ids of removed lights are
 * reused.
 *
 * Returns: the light id
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_light_set_add_point_light (LrgLightSet    *self,
                               gdouble         x,
                               gdouble         y,
                               gdouble         z,
                               const GrlColor *color,
                               gfloat          radius);

/**
 * lrg_light_set_remove_point_light:
 * @self: an #LrgLightSet
 * @light: a light id
 *
 * Returns: %TRUE if the light existed
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_light_set_remove_point_light (LrgLightSet *self,
                                  guint        light);

/**
 * lrg_light_set_clear_point_lights:
 * @self: an #LrgLightSet
 */
LRG_AVAILABLE_IN_ALL
void
lrg_light_set_clear_point_lights (LrgLightSet *self);

/**
 * lrg_light_set_get_point_light_count:
 * @self: an #LrgLightSet
 *
 * Returns: the number of point lights, enabled or not
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_light_set_get_point_light_count (LrgLightSet *self);

/**
 * lrg_light_set_set_point_light_position:
 * @self: an #LrgLightSet
 * @light: a light id
 * @x: world X
 * @y: world Y
 * @z: world Z
 */
LRG_AVAILABLE_IN_ALL
void
lrg_light_set_set_point_light_position (LrgLightSet *self,
                                        guint        light,
                                        gdouble      x,
                                        gdouble      y,
                                        gdouble      z);

/**
 * lrg_light_set_set_point_light_color:
 * @self: an #LrgLightSet
 * @light: a light id
 * @color: light colour (alpha ignored)
 */
LRG_AVAILABLE_IN_ALL
void
lrg_light_set_set_point_light_color (LrgLightSet    *self,
                                     guint           light,
                                     const GrlColor *color);

/**
 * lrg_light_set_set_point_light_radius:
 * @self: an #LrgLightSet
 * @light: a light id
 * @radius: fade-out distance (> 0)
 */
LRG_AVAILABLE_IN_ALL
void
lrg_light_set_set_point_light_radius (LrgLightSet *self,
                                      guint        light,
                                      gfloat       radius);

/**
 * lrg_light_set_set_point_light_intensity:
 * @self: an #LrgLightSet
 * @light: a light id
 * @intensity: colour multiplier (>= 0; above 1 over-brightens, 0 is
 *   never selected), e.g. for flicker
 */
LRG_AVAILABLE_IN_ALL
void
lrg_light_set_set_point_light_intensity (LrgLightSet *self,
                                         guint        light,
                                         gfloat       intensity);

/**
 * lrg_light_set_set_point_light_enabled:
 * @self: an #LrgLightSet
 * @light: a light id
 * @enabled: whether the light can be selected
 */
LRG_AVAILABLE_IN_ALL
void
lrg_light_set_set_point_light_enabled (LrgLightSet *self,
                                       guint        light,
                                       gboolean     enabled);

/**
 * lrg_light_set_set_max_active:
 * @self: an #LrgLightSet
 * @count: lights selected per frame, 0 .. %LRG_LIGHT_SET_MAX_ACTIVE
 */
LRG_AVAILABLE_IN_ALL
void
lrg_light_set_set_max_active (LrgLightSet *self,
                              guint        count);

/**
 * lrg_light_set_set_max_distance:
 * @self: an #LrgLightSet
 * @distance: lights whose sphere lies further than this from the camera
 *   are never selected; 0 (default) for no limit
 */
LRG_AVAILABLE_IN_ALL
void
lrg_light_set_set_max_distance (LrgLightSet *self,
                                gfloat       distance);

/**
 * lrg_light_set_select:
 * @self: an #LrgLightSet
 * @camera_x: camera world X
 * @camera_y: camera world Y
 * @camera_z: camera world Z
 *
 * Picks the enabled point lights (intensity > 0) closest to the camera,
 * ordered by the distance from the camera to the light's sphere
 * (distance to the centre minus the radius, so big lights win near
 * ties), then by id. Headless.
 *
 * Returns: the number of selected lights
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_light_set_select (LrgLightSet *self,
                      gdouble      camera_x,
                      gdouble      camera_y,
                      gdouble      camera_z);

/**
 * lrg_light_set_get_active_count:
 * @self: an #LrgLightSet
 *
 * Returns: lights picked by the last selection
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_light_set_get_active_count (LrgLightSet *self);

/**
 * lrg_light_set_get_active_light:
 * @self: an #LrgLightSet
 * @index: 0 .. active count - 1, nearest first
 *
 * Returns: the light id, or %LRG_LIGHT_SET_INVALID_LIGHT
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_light_set_get_active_light (LrgLightSet *self,
                                guint        index);

/**
 * lrg_light_set_set_alpha_cutoff:
 * @self: an #LrgLightSet
 * @cutoff: fragments whose alpha falls below this are discarded (0, the
 *   default, keeps every fragment)
 *
 * For cutout textures (foliage, fences) drawn as static geometry.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_light_set_set_alpha_cutoff (LrgLightSet *self,
                                gfloat       cutoff);

/**
 * lrg_light_set_get_shader:
 * @self: an #LrgLightSet
 *
 * The lighting shader, created on first call with a graphics context.
 * Pass it to lrg_static_batch_set_shader() / lrg_terrain_set_shader().
 * Release every reference before the window closes.
 *
 * Returns: (transfer none) (nullable): the shader, or %NULL without a
 *   context or if GLSL 330 is unavailable (keep raylib's default shader
 *   then)
 */
LRG_AVAILABLE_IN_ALL
GrlShader *
lrg_light_set_get_shader (LrgLightSet *self);

/**
 * lrg_light_set_apply:
 * @self: an #LrgLightSet
 * @camera_x: camera world X
 * @camera_y: camera world Y
 * @camera_z: camera world Z
 *
 * Selects lights for the camera (lrg_light_set_select()) and uploads
 * ambient, directional, point light and cutout uniforms to the shader.
 * Call once per frame (and after changing lights) before drawing with
 * the shader.
 *
 * Returns: %TRUE when the shader exists and was updated
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_light_set_apply (LrgLightSet *self,
                     gdouble      camera_x,
                     gdouble      camera_y,
                     gdouble      camera_z);

/**
 * lrg_light_set_sample:
 * @self: an #LrgLightSet
 * @position: a point in draw space, the space the shader lights (world
 *   minus the origin, see lrg_light_set_set_origin())
 * @normal: (nullable): the surface normal (any length > 0), or %NULL
 *   for a surface that faces every light (no Lambert term; billboards)
 * @out_rgb: (out caller-allocates) (array fixed-size=3): the light
 *   reaching @position, the shader's light term before it multiplies
 *   the texel (not clamped)
 *
 * Evaluates the shader's lighting model on the CPU with the ambient
 * colour, the directional light and the point lights of the last
 * selection (lrg_light_set_select() or lrg_light_set_apply()).
 * Headless; used to light geometry the shader cannot reach, such as
 * #LrgBillboardBatch quads.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_light_set_sample (LrgLightSet      *self,
                      const GrlVector3 *position,
                      const GrlVector3 *normal,
                      gfloat           *out_rgb);

/**
 * lrg_light_set_bind:
 * @self: an #LrgLightSet
 *
 * Makes @self the bound light set (one per process, like raylib's
 * current shader): billboard batches without a light set of their own
 * (lrg_billboard_batch_set_light_set()) are lit by it while it stays
 * bound.  Keeps a reference until lrg_light_set_unbind() or the next
 * bind.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_light_set_bind (LrgLightSet *self);

/**
 * lrg_light_set_unbind:
 *
 * Drops the bound light set; billboards draw unlit again.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_light_set_unbind (void);

/**
 * lrg_light_set_get_bound:
 *
 * Returns: (transfer none) (nullable): the bound light set
 */
LRG_AVAILABLE_IN_ALL
LrgLightSet *
lrg_light_set_get_bound (void);

G_END_DECLS
