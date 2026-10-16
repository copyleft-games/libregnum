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
 * LrgStaticBatch and LrgTerrain, plus optional distance fog (linear,
 * exponential, uniform or height) the same shader applies. With a light
 * grid (lrg_light_set_set_grid()) the shader instead finds the lights of
 * each fragment in a world-space grid of cells around the camera, so any
 * number of lights each light their own surroundings. A soft light
 * limit (lrg_light_set_set_light_limit()) and an emission term for
 * materials with an emission map (lrg_static_batch_set_material_emission())
 * complete the model. Light bookkeeping, selection, the grid and the
 * fog model are headless; only the shader needs GL.
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
 * LRG_LIGHT_SET_GRID_SLOTS:
 *
 * Point lights one grid cell holds (the lights a fragment evaluates
 * with a light grid).
 */
#define LRG_LIGHT_SET_GRID_SLOTS (16)

/**
 * LRG_LIGHT_SET_GRID_MAX_CELLS:
 *
 * Largest grid side (cells per row and per column).
 */
#define LRG_LIGHT_SET_GRID_MAX_CELLS (128)

/**
 * LRG_LIGHT_SET_GRID_MAX_LIGHTS:
 *
 * Point lights one grid upload holds; lights beyond it (the farthest
 * from the camera) light nothing that frame.
 */
#define LRG_LIGHT_SET_GRID_MAX_LIGHTS (1024)

/**
 * LRG_LIGHT_SET_EMISSION_UNIT:
 *
 * The texture unit (raylib's %MATERIAL_MAP_EMISSION) the shader reads
 * a material's emission map from.
 */
#define LRG_LIGHT_SET_EMISSION_UNIT (5)

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
 * lrg_light_set_set_grid:
 * @self: an #LrgLightSet
 * @cell_size: cell edge in world units (> 0)
 * @cells: cells per side (2 .. %LRG_LIGHT_SET_GRID_MAX_CELLS), or 0 to
 *   turn the grid off
 *
 * Switches point lighting from the %LRG_LIGHT_SET_MAX_ACTIVE lights
 * nearest the camera to a light grid: a square of @cells x @cells cells
 * in the XZ plane, centred on the camera's cell, each listing up to
 * %LRG_LIGHT_SET_GRID_SLOTS lights whose radius reaches it (closest
 * first). Every fragment is lit by the lights of its own cell, so lights
 * far from the camera still light their surroundings and nothing pops
 * when the camera moves. Fragments outside the square get no point
 * light. Built by lrg_light_set_select(); needs float textures on the
 * GPU (GLSL 330 has them).
 */
LRG_AVAILABLE_IN_ALL
void
lrg_light_set_set_grid (LrgLightSet *self,
                        gfloat       cell_size,
                        guint        cells);

/**
 * lrg_light_set_get_grid_light_count:
 * @self: an #LrgLightSet
 * @x: world X
 * @z: world Z
 *
 * Returns: the lights of the grid cell holding (@x, @z) after the last
 *   selection, 0 outside the grid or without one
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_light_set_get_grid_light_count (LrgLightSet *self,
                                    gdouble      x,
                                    gdouble      z);

/**
 * lrg_light_set_get_grid_light:
 * @self: an #LrgLightSet
 * @x: world X
 * @z: world Z
 * @index: 0 .. lrg_light_set_get_grid_light_count() - 1, closest first
 *
 * Returns: the light id, or %LRG_LIGHT_SET_INVALID_LIGHT
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_light_set_get_grid_light (LrgLightSet *self,
                              gdouble      x,
                              gdouble      z,
                              guint        index);

/**
 * lrg_light_set_set_light_limit:
 * @self: an #LrgLightSet
 * @limit: the most light a surface can receive per channel (> 0), or 0
 *   (the default) for no limit
 *
 * A soft tonemap of the summed light: values up to 0.75 x @limit pass
 * unchanged, larger ones roll off exponentially towards @limit, so a
 * bright light near a wall saturates gracefully instead of turning the
 * texture white. lrg_light_set_sample() applies it too.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_light_set_set_light_limit (LrgLightSet *self,
                               gfloat       limit);

/**
 * lrg_light_set_get_light_limit:
 * @self: an #LrgLightSet
 *
 * Returns: the light limit, 0 when off
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_light_set_get_light_limit (LrgLightSet *self);

/**
 * lrg_light_set_limit_light:
 * @limit: a light limit (0: none)
 * @value: a summed light value of one channel
 *
 * The light limit's curve (see lrg_light_set_set_light_limit()).
 *
 * Returns: @value after the soft limit
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_light_set_limit_light (gfloat limit,
                           gfloat value);

/**
 * lrg_light_set_set_emission:
 * @self: an #LrgLightSet
 * @color: (nullable): colour of emissive texels, %NULL for none
 * @intensity: multiplier of @color (>= 0)
 *
 * Materials with an emission map (lrg_static_batch_set_material_emission())
 * add map texel x @color x @intensity to their lit colour, unaffected by
 * the lights (lit windows at night). The default is no emission.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_light_set_set_emission (LrgLightSet    *self,
                            const GrlColor *color,
                            gfloat          intensity);

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
 * ambient, directional, point light, cutout and fog uniforms to the
 * shader.
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
 * lrg_light_set_set_fog:
 * @self: an #LrgLightSet
 * @type: the fog model (see #LrgFogType)
 * @color: fog colour; its alpha caps the fog (255 = surfaces vanish
 *   into the colour, 128 = at most half fogged)
 * @density: exponential/height density per world unit, or the constant
 *   amount (0 .. 1) of %LRG_FOG_TYPE_UNIFORM
 * @start: linear fog: distance where the fog begins
 * @end: linear fog: distance where it is full (end <= start: a hard
 *   edge at @end)
 *
 * Turns on distance fog. Fragments drawn with lrg_light_set_get_shader()
 * (and billboards lit by this set, see #LrgBillboardBatch) blend toward
 * @color by the fog amount at their distance from the camera of the
 * last lrg_light_set_apply():
 *
 * - %LRG_FOG_TYPE_LINEAR: clamp ((d - @start) / (@end - @start))
 * - %LRG_FOG_TYPE_EXPONENTIAL: 1 - exp (-@density * d)
 * - %LRG_FOG_TYPE_UNIFORM: @density, at every distance
 * - %LRG_FOG_TYPE_HEIGHT: exponential, thinned above the base height
 *   (lrg_light_set_set_fog_height())
 *
 * The amount is then multiplied by @color's alpha. Takes effect on the
 * next apply. #LrgFog (lrg_fog_apply()) and
 * lrg_weather_manager_apply_fog() call this from weather.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_light_set_set_fog (LrgLightSet    *self,
                       LrgFogType      type,
                       const GrlColor *color,
                       gfloat          density,
                       gfloat          start,
                       gfloat          end);

/**
 * lrg_light_set_set_fog_height:
 * @self: an #LrgLightSet
 * @base_height: world height below which %LRG_FOG_TYPE_HEIGHT fog has
 *   its full density
 * @falloff: how fast it thins above @base_height, per world unit
 *
 * Shapes %LRG_FOG_TYPE_HEIGHT fog (defaults 0 and 1). The base is a
 * world height; it is moved by the origin like everything else.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_light_set_set_fog_height (LrgLightSet *self,
                              gfloat       base_height,
                              gfloat       falloff);

/**
 * lrg_light_set_clear_fog:
 * @self: an #LrgLightSet
 *
 * Turns the fog off (the default).
 */
LRG_AVAILABLE_IN_ALL
void
lrg_light_set_clear_fog (LrgLightSet *self);

/**
 * lrg_light_set_get_fog:
 * @self: an #LrgLightSet
 * @out_type: (out) (optional): the fog model
 * @out_color: (out caller-allocates) (optional): the fog colour
 * @out_density: (out) (optional): the density
 * @out_start: (out) (optional): the linear start
 * @out_end: (out) (optional): the linear end
 *
 * Reads the fog set by lrg_light_set_set_fog(). The outputs are left
 * alone when the fog is off.
 *
 * Returns: %TRUE when fog is on
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_light_set_get_fog (LrgLightSet *self,
                       LrgFogType  *out_type,
                       GrlColor    *out_color,
                       gfloat      *out_density,
                       gfloat      *out_start,
                       gfloat      *out_end);

/**
 * lrg_light_set_sample_fog:
 * @self: an #LrgLightSet
 * @position: a point in draw space (world minus the origin)
 *
 * The fog amount the shader gives a fragment at @position, seen from
 * the camera of the last lrg_light_set_select() or
 * lrg_light_set_apply(): 0 (clear, or fog off) .. the colour's alpha
 * as a fraction. Headless.
 *
 * Returns: the share of the fog colour, 0 .. 1
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_light_set_sample_fog (LrgLightSet      *self,
                          const GrlVector3 *position);

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
