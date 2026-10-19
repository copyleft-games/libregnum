/* lrg-water-surface.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgWaterSurface - a shaded water surface: Gerstner waves displacing
 * a grid in the vertex shader, per-pixel wave normals, a Fresnel blend
 * of reflection and refraction, a sun glint, shoreline foam from a
 * per-vertex shore factor and crest foam. With a captured copy of the
 * frame drawn so far (lrg_water_surface_capture_scene()) the
 * reflection is screen space (the reflected ray's point at a fixed
 * distance, projected and sampled; the sky colours where it leaves the
 * screen or lands below the water) and the refraction samples the
 * frame through the wave normals; without one the water blends over
 * the frame with its own colours. Lit and fogged by the bound
 * #LrgLightSet. lrg_water_surface_evaluate() is the vertex shader's
 * wave sum on the CPU (for floating objects and tests).
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
 * LRG_WATER_SURFACE_MAX_WAVES:
 *
 * The most Gerstner waves a surface sums.
 */
#define LRG_WATER_SURFACE_MAX_WAVES (4)

/**
 * LRG_WATER_SURFACE_MAX_PATCH_VERTICES:
 *
 * The most vertices one grid patch may have (16-bit indices).
 */
#define LRG_WATER_SURFACE_MAX_PATCH_VERTICES (65535)

/**
 * LrgWaterWave:
 * @direction: the travel direction on the XZ plane (normalised when set)
 * @amplitude: the crest height in world units (>= 0)
 * @wavelength: crest to crest in world units (> 0)
 * @speed: the phase speed in world units per second
 * @steepness: 0 (sine swell) .. 1 (sharp crests); shared out so the
 *   sum never loops over itself
 *
 * One Gerstner wave.
 */
typedef struct
{
    gfloat direction[2];
    gfloat amplitude;
    gfloat wavelength;
    gfloat speed;
    gfloat steepness;
} LrgWaterWave;

/**
 * LrgWaterFoamFunc:
 * @x: draw-space X of a grid vertex
 * @z: draw-space Z
 * @user_data: the data given with the function
 *
 * Returns: the shore factor at the vertex: 0 open water .. 1 at the shore
 */
typedef gfloat (*LrgWaterFoamFunc) (gfloat   x,
                                    gfloat   z,
                                    gpointer user_data);

#define LRG_TYPE_WATER_SURFACE (lrg_water_surface_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgWaterSurface, lrg_water_surface, LRG, WATER_SURFACE, GObject)

/**
 * lrg_water_surface_new:
 *
 * Creates a surface with three gentle default waves (0.12, 0.07 and
 * 0.04 high), lake colours, a pale sky and no geometry.
 *
 * Returns: (transfer full): a new #LrgWaterSurface
 */
LRG_AVAILABLE_IN_ALL
LrgWaterSurface *
lrg_water_surface_new (void);

/**
 * lrg_water_surface_set_wave:
 * @self: an #LrgWaterSurface
 * @index: the wave, 0 .. %LRG_WATER_SURFACE_MAX_WAVES - 1
 * @wave: the wave
 *
 * Sets one wave (the count grows to include it). Invalid values are
 * clamped: a zero direction becomes +X, the wavelength at least 0.01.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_water_surface_set_wave (LrgWaterSurface    *self,
                            guint               index,
                            const LrgWaterWave *wave);

/**
 * lrg_water_surface_get_wave:
 * @self: an #LrgWaterSurface
 * @index: the wave
 * @out_wave: (out caller-allocates): the wave
 *
 * Returns: %TRUE when @index is below the wave count
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_water_surface_get_wave (LrgWaterSurface *self,
                            guint            index,
                            LrgWaterWave    *out_wave);

/**
 * lrg_water_surface_set_wave_count:
 * @self: an #LrgWaterSurface
 * @count: 0 (a flat mirror) .. %LRG_WATER_SURFACE_MAX_WAVES
 */
LRG_AVAILABLE_IN_ALL
void
lrg_water_surface_set_wave_count (LrgWaterSurface *self,
                                  guint            count);

/**
 * lrg_water_surface_get_wave_count:
 * @self: an #LrgWaterSurface
 *
 * Returns: the number of summed waves
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_water_surface_get_wave_count (LrgWaterSurface *self);

/**
 * lrg_water_surface_set_wave_scale:
 * @self: an #LrgWaterSurface
 * @scale: multiplies every amplitude (0 = flat), for calm dungeon pools
 *   or stormy weather
 */
LRG_AVAILABLE_IN_ALL
void
lrg_water_surface_set_wave_scale (LrgWaterSurface *self,
                                  gfloat           scale);

/**
 * lrg_water_surface_set_origin:
 * @self: an #LrgWaterSurface
 * @x: placement X of the draw-space origin
 * @z: placement Z of the draw-space origin
 *
 * The waves run on draw position + origin, so a floating origin that
 * moves the draw space keeps the waves in place.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_water_surface_set_origin (LrgWaterSurface *self,
                              gdouble          x,
                              gdouble          z);

/**
 * lrg_water_surface_evaluate:
 * @self: an #LrgWaterSurface
 * @x: draw-space X on the rest plane
 * @z: draw-space Z
 * @time: seconds
 * @out_offset: (out caller-allocates) (array fixed-size=3) (optional):
 *   the displacement of the rest point (y is the height above the plane)
 * @out_normal: (out caller-allocates) (array fixed-size=3) (optional):
 *   the unit surface normal
 *
 * The wave sum the vertex shader applies (Gerstner: horizontal pull
 * towards the crests, sine height), with the wave scale.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_water_surface_evaluate (LrgWaterSurface *self,
                            gfloat           x,
                            gfloat           z,
                            gfloat           time,
                            gfloat          *out_offset,
                            gfloat          *out_normal);

/**
 * lrg_water_surface_get_max_height:
 * @self: an #LrgWaterSurface
 *
 * Returns: the highest a crest can rise above the rest plane (the sum
 *   of the scaled amplitudes)
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_water_surface_get_max_height (LrgWaterSurface *self);

/**
 * lrg_water_surface_set_colors:
 * @self: an #LrgWaterSurface
 * @shallow: the water's colour where it is shallow (near the shore)
 * @deep: its colour in open water
 * @foam: the foam colour
 */
LRG_AVAILABLE_IN_ALL
void
lrg_water_surface_set_colors (LrgWaterSurface *self,
                              const GrlColor  *shallow,
                              const GrlColor  *deep,
                              const GrlColor  *foam);

/**
 * lrg_water_surface_set_sky:
 * @self: an #LrgWaterSurface
 * @horizon: the sky colour the surface reflects at grazing angles
 * @zenith: the colour it reflects looking straight up
 */
LRG_AVAILABLE_IN_ALL
void
lrg_water_surface_set_sky (LrgWaterSurface *self,
                           const GrlColor  *horizon,
                           const GrlColor  *zenith);

/**
 * lrg_water_surface_set_sun:
 * @self: an #LrgWaterSurface
 * @direction: (array fixed-size=3): the direction the sunlight travels
 *   (pointing down for a sun above the horizon)
 * @color: the glint colour (black for none, e.g. at night)
 */
LRG_AVAILABLE_IN_ALL
void
lrg_water_surface_set_sun (LrgWaterSurface *self,
                           const gfloat    *direction,
                           const GrlColor  *color);

/**
 * lrg_water_surface_set_optics:
 * @self: an #LrgWaterSurface
 * @transparency: 0 opaque .. 1 clear, the share of the frame seen
 *   through flat water without a captured scene
 * @reflection: 0 .. 1, how much of the captured scene the reflection shows
 * @refraction: the shift of the refracted frame in screen units per
 *   unit of normal tilt (0 = none)
 * @reflect_distance: how far along the reflected ray the screen-space
 *   reflection looks (world units)
 */
LRG_AVAILABLE_IN_ALL
void
lrg_water_surface_set_optics (LrgWaterSurface *self,
                              gfloat           transparency,
                              gfloat           reflection,
                              gfloat           refraction,
                              gfloat           reflect_distance);

/**
 * lrg_water_surface_clear:
 * @self: an #LrgWaterSurface
 *
 * Removes every patch (and frees their GPU meshes when a context lives).
 */
LRG_AVAILABLE_IN_ALL
void
lrg_water_surface_clear (LrgWaterSurface *self);

/**
 * lrg_water_surface_add_grid:
 * @self: an #LrgWaterSurface
 * @x0: draw-space X of one corner
 * @z0: draw-space Z of that corner
 * @x1: X of the opposite corner
 * @z1: Z of the opposite corner
 * @y: the rest plane's height
 * @columns: grid cells along X (>= 1)
 * @rows: grid cells along Z (>= 1)
 * @foam_func: (scope call) (closure user_data) (nullable): the shore factor per vertex, or %NULL for 0
 * @user_data: data for @foam_func
 * @error: return location for a #GError
 *
 * Adds one rectangular patch of (@columns + 1) x (@rows + 1) vertices
 * (at most %LRG_WATER_SURFACE_MAX_PATCH_VERTICES), facing up.
 *
 * Returns: %TRUE when the patch was added
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_water_surface_add_grid (LrgWaterSurface   *self,
                            gfloat             x0,
                            gfloat             z0,
                            gfloat             x1,
                            gfloat             z1,
                            gfloat             y,
                            guint              columns,
                            guint              rows,
                            LrgWaterFoamFunc   foam_func,
                            gpointer           user_data,
                            GError           **error);

/**
 * lrg_water_surface_add_heightfield:
 * @self: an #LrgWaterSurface
 * @x0: draw-space X of one corner
 * @z0: draw-space Z of that corner
 * @x1: X of the opposite corner
 * @z1: Z of the opposite corner
 * @columns: grid cells along X (>= 1)
 * @rows: grid cells along Z (>= 1)
 * @heights: (array): the rest height of each of the (@columns + 1) x
 *   (@rows + 1) vertices, row by row from (@x0, @z0)
 * @cells: (array) (nullable): @columns x @rows flags, non-zero for the
 *   cells that hold water; %NULL for all
 * @foam_func: (scope call) (closure user_data) (nullable): the shore factor per vertex, or %NULL for 0
 * @user_data: data for @foam_func
 * @error: return location for a #GError
 *
 * Adds a patch like lrg_water_surface_add_grid() whose rest surface
 * follows @heights (a stream running down a valley) and that draws only
 * its wet cells.  The waves ride on each vertex's own height.  Refused
 * (%G_IO_ERROR_INVALID_ARGUMENT) with a non-finite height or no wet cell.
 *
 * Returns: %TRUE when the patch was added
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_water_surface_add_heightfield (LrgWaterSurface   *self,
                                   gfloat             x0,
                                   gfloat             z0,
                                   gfloat             x1,
                                   gfloat             z1,
                                   guint              columns,
                                   guint              rows,
                                   const gfloat      *heights,
                                   const guint8      *cells,
                                   LrgWaterFoamFunc   foam_func,
                                   gpointer           user_data,
                                   GError           **error);

/**
 * lrg_water_surface_get_patch_count:
 * @self: an #LrgWaterSurface
 *
 * Returns: the number of patches
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_water_surface_get_patch_count (LrgWaterSurface *self);

/**
 * lrg_water_surface_get_vertex_count:
 * @self: an #LrgWaterSurface
 *
 * Returns: the vertices of every patch
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_water_surface_get_vertex_count (LrgWaterSurface *self);

/**
 * lrg_water_surface_get_triangle_count:
 * @self: an #LrgWaterSurface
 *
 * Returns: the triangles of every patch
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_water_surface_get_triangle_count (LrgWaterSurface *self);

/**
 * lrg_water_surface_get_patch_foam:
 * @self: an #LrgWaterSurface
 * @patch: the patch
 * @n_values: (out) (optional): the number of values (one per vertex)
 *
 * Returns: (array length=n_values) (transfer none) (nullable): the
 *   patch's shore factors, row by row, or %NULL past the last patch
 */
LRG_AVAILABLE_IN_ALL
const gfloat *
lrg_water_surface_get_patch_foam (LrgWaterSurface *self,
                                  guint            patch,
                                  guint           *n_values);

/**
 * lrg_water_surface_capture_scene:
 * @self: an #LrgWaterSurface
 *
 * Copies the colour of the framebuffer being drawn (the screen or the
 * active render texture) into the surface's own texture, for the next
 * lrg_water_surface_draw(): call it after the opaque world and before
 * the water. Flushes rlgl's batch first. Needs GL 3.3.
 *
 * Returns: %TRUE when the frame was captured
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_water_surface_capture_scene (LrgWaterSurface *self);

/**
 * lrg_water_surface_drop_scene:
 * @self: an #LrgWaterSurface
 *
 * Forgets the captured frame: the next draw blends instead.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_water_surface_drop_scene (LrgWaterSurface *self);

/**
 * lrg_water_surface_draw:
 * @self: an #LrgWaterSurface
 * @eye: (array fixed-size=3): the camera in draw space
 * @time: seconds (for the waves and foam)
 *
 * Uploads new patches, then draws them (two-sided, depth tested, not
 * writing depth) with the water shader. Needs a GL context in 3D mode;
 * returns %FALSE (drawing nothing) when the shader is unavailable.
 *
 * Returns: %TRUE when the water was drawn with the shader
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_water_surface_draw (LrgWaterSurface *self,
                        const gfloat    *eye,
                        gfloat           time);

/**
 * lrg_water_surface_release_gpu:
 * @self: an #LrgWaterSurface
 *
 * Frees the meshes, the shader and the scene copy while the context
 * lives; the patches stay and upload again on the next draw.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_water_surface_release_gpu (LrgWaterSurface *self);

G_END_DECLS
