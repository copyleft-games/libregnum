/* lrg-water-body.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgWaterBody: the `.rwater` water body asset.
 *
 * A water body is a shape (an axis-aligned plane, a lake polygon or a
 * river spline with a width per point) plus everything an
 * #LrgWaterSurface needs to shade it (waves, colours, optics).  It
 * builds the surface's geometry as grid patches clipped to the shape,
 * with a shore-foam factor that grows towards the shape's edge (and,
 * with a ground function such as a terrain's heights, towards shallow
 * water).  Loading, saving and building are headless; the
 * #LrgWaterComponent draws a body in a world.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <graylib.h>
#include "../lrg-version.h"
#include "../lrg-types.h"
#include "lrg-water-surface.h"

G_BEGIN_DECLS

/**
 * LRG_WATER_BODY_FORMAT_VERSION:
 *
 * The `.rwater` format version this build writes (and the newest it
 * reads).
 */
#define LRG_WATER_BODY_FORMAT_VERSION (1)

/**
 * LRG_WATER_BODY_MAX_POINTS:
 *
 * The most points of a lake polygon or river spline.
 */
#define LRG_WATER_BODY_MAX_POINTS (1024)

/**
 * LRG_WATER_BODY_ERROR:
 *
 * Error domain of #LrgWaterBody.
 */
#define LRG_WATER_BODY_ERROR (lrg_water_body_error_quark ())

LRG_AVAILABLE_IN_ALL
GQuark lrg_water_body_error_quark (void);

/**
 * LrgWaterBodyError:
 * @LRG_WATER_BODY_ERROR_PARSE: not valid YAML or a value of the wrong kind
 * @LRG_WATER_BODY_ERROR_INVALID: a value out of range, or a shape that
 *   cannot be built (a lake with fewer than three points)
 * @LRG_WATER_BODY_ERROR_IO: a file could not be read or written
 *
 * Errors of #LrgWaterBody.
 */
typedef enum
{
	LRG_WATER_BODY_ERROR_PARSE,
	LRG_WATER_BODY_ERROR_INVALID,
	LRG_WATER_BODY_ERROR_IO
} LrgWaterBodyError;

LRG_AVAILABLE_IN_ALL
GType lrg_water_body_error_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_WATER_BODY_ERROR (lrg_water_body_error_get_type ())

/**
 * LrgWaterBodyShape:
 * @LRG_WATER_BODY_PLANE: a `size[0]` x `size[1]` rectangle centred on the
 *   origin at the body's level
 * @LRG_WATER_BODY_LAKE: the polygon through the points (x, z) at the
 *   body's level
 * @LRG_WATER_BODY_RIVER: a Catmull-Rom spline through the points (x, y,
 *   z) with a width per point; the surface follows the spline's height
 *
 * The outline of a water body.
 */
typedef enum
{
	LRG_WATER_BODY_PLANE,
	LRG_WATER_BODY_LAKE,
	LRG_WATER_BODY_RIVER
} LrgWaterBodyShape;

LRG_AVAILABLE_IN_ALL
GType lrg_water_body_shape_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_WATER_BODY_SHAPE (lrg_water_body_shape_get_type ())

/**
 * LrgWaterGroundFunc:
 * @x: local X
 * @z: local Z
 * @out_height: (out): the ground height there
 * @user_data: the data given with the function
 *
 * The ground under the water, for depth-based shore foam.
 *
 * Returns: %TRUE when there is ground at (@x, @z)
 */
typedef gboolean (*LrgWaterGroundFunc) (gfloat    x,
                                        gfloat    z,
                                        gfloat   *out_height,
                                        gpointer  user_data);

#define LRG_TYPE_WATER_BODY (lrg_water_body_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgWaterBody, lrg_water_body, LRG, WATER_BODY, GObject)

/**
 * lrg_water_body_new:
 * @shape: the outline kind
 *
 * Creates a body at level 0: a 32 x 32 plane, no points, 1-unit cells, a
 * 3-unit foam band, depth foam over the first 0.6 units, the default
 * waves and colours of lrg_water_surface_new() and its default optics.
 *
 * Returns: (transfer full): a new #LrgWaterBody
 */
LRG_AVAILABLE_IN_ALL
LrgWaterBody * lrg_water_body_new (LrgWaterBodyShape shape);

/**
 * lrg_water_body_new_from_file:
 * @path: a `.rwater` file
 * @error: return location for a #GError
 *
 * Returns: (transfer full) (nullable): the body, or %NULL with @error set
 *   ("file: /key/path: problem")
 */
LRG_AVAILABLE_IN_ALL
LrgWaterBody * lrg_water_body_new_from_file (const gchar  *path,
                                             GError      **error);

/**
 * lrg_water_body_new_from_data:
 * @data: the YAML text
 * @length: its length, or -1 when nul-terminated
 * @error: return location for a #GError
 *
 * Returns: (transfer full) (nullable): the body, or %NULL with @error set
 */
LRG_AVAILABLE_IN_ALL
LrgWaterBody * lrg_water_body_new_from_data (const gchar  *data,
                                             gssize        length,
                                             GError      **error);

/**
 * lrg_water_body_save_to_file:
 * @self: an #LrgWaterBody
 * @path: where to write
 * @error: return location for a #GError
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_water_body_save_to_file (LrgWaterBody  *self,
                                      const gchar   *path,
                                      GError       **error);

/**
 * lrg_water_body_to_data:
 * @self: an #LrgWaterBody
 * @length: (out) (optional): the text length
 * @error: return location for a #GError
 *
 * Returns: (transfer full) (nullable): the `.rwater` document (same body,
 *   same bytes; unknown keys of the last load are kept)
 */
LRG_AVAILABLE_IN_ALL
gchar * lrg_water_body_to_data (LrgWaterBody  *self,
                                gsize         *length,
                                GError       **error);

/**
 * lrg_water_body_get_name:
 * @self: an #LrgWaterBody
 *
 * Returns: (nullable): the display name
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_water_body_get_name (LrgWaterBody *self);

/**
 * lrg_water_body_set_name:
 * @self: an #LrgWaterBody
 * @name: (nullable): the display name
 */
LRG_AVAILABLE_IN_ALL
void lrg_water_body_set_name (LrgWaterBody *self,
                              const gchar  *name);

/**
 * lrg_water_body_get_shape:
 * @self: an #LrgWaterBody
 *
 * Returns: the outline kind
 */
LRG_AVAILABLE_IN_ALL
LrgWaterBodyShape lrg_water_body_get_shape (LrgWaterBody *self);

/**
 * lrg_water_body_set_shape:
 * @self: an #LrgWaterBody
 * @shape: the outline kind (the points are kept)
 */
LRG_AVAILABLE_IN_ALL
void lrg_water_body_set_shape (LrgWaterBody      *self,
                               LrgWaterBodyShape  shape);

/**
 * lrg_water_body_get_level:
 * @self: an #LrgWaterBody
 *
 * Returns: the rest height of a plane or lake (local)
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_water_body_get_level (LrgWaterBody *self);

/**
 * lrg_water_body_set_level:
 * @self: an #LrgWaterBody
 * @level: the rest height
 */
LRG_AVAILABLE_IN_ALL
void lrg_water_body_set_level (LrgWaterBody *self,
                               gfloat        level);

/**
 * lrg_water_body_get_size:
 * @self: an #LrgWaterBody
 * @width: (out) (optional): plane extent along X
 * @length: (out) (optional): plane extent along Z
 */
LRG_AVAILABLE_IN_ALL
void lrg_water_body_get_size (LrgWaterBody *self,
                              gfloat       *width,
                              gfloat       *length);

/**
 * lrg_water_body_set_size:
 * @self: an #LrgWaterBody
 * @width: plane extent along X (> 0)
 * @length: plane extent along Z (> 0)
 */
LRG_AVAILABLE_IN_ALL
void lrg_water_body_set_size (LrgWaterBody *self,
                              gfloat        width,
                              gfloat        length);

/**
 * lrg_water_body_get_n_points:
 * @self: an #LrgWaterBody
 *
 * Returns: the number of outline points
 */
LRG_AVAILABLE_IN_ALL
guint lrg_water_body_get_n_points (LrgWaterBody *self);

/**
 * lrg_water_body_get_point:
 * @self: an #LrgWaterBody
 * @index: the point
 * @out: (out caller-allocates) (array fixed-size=4): x, y, z, width (y
 *   and width are only used by rivers)
 *
 * Returns: %TRUE when @index exists
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_water_body_get_point (LrgWaterBody *self,
                                   guint         index,
                                   gfloat       *out);

/**
 * lrg_water_body_add_point:
 * @self: an #LrgWaterBody
 * @x: local X
 * @y: local height (rivers)
 * @z: local Z
 * @width: river width at the point (> 0; rivers)
 *
 * Returns: the new point's index, or -1 at %LRG_WATER_BODY_MAX_POINTS
 */
LRG_AVAILABLE_IN_ALL
gint lrg_water_body_add_point (LrgWaterBody *self,
                               gfloat        x,
                               gfloat        y,
                               gfloat        z,
                               gfloat        width);

/**
 * lrg_water_body_set_point:
 * @self: an #LrgWaterBody
 * @index: an existing point
 * @x: local X
 * @y: local height
 * @z: local Z
 * @width: river width (> 0)
 *
 * Returns: %TRUE when @index exists
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_water_body_set_point (LrgWaterBody *self,
                                   guint         index,
                                   gfloat        x,
                                   gfloat        y,
                                   gfloat        z,
                                   gfloat        width);

/**
 * lrg_water_body_remove_point:
 * @self: an #LrgWaterBody
 * @index: the point
 *
 * Returns: %TRUE when a point was removed
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_water_body_remove_point (LrgWaterBody *self,
                                      guint         index);

/**
 * lrg_water_body_clear_points:
 * @self: an #LrgWaterBody
 */
LRG_AVAILABLE_IN_ALL
void lrg_water_body_clear_points (LrgWaterBody *self);

/**
 * lrg_water_body_get_cell_size:
 * @self: an #LrgWaterBody
 *
 * Returns: the world size of a surface grid cell
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_water_body_get_cell_size (LrgWaterBody *self);

/**
 * lrg_water_body_set_cell_size:
 * @self: an #LrgWaterBody
 * @cell_size: grid cell size (0.05 .. 64)
 */
LRG_AVAILABLE_IN_ALL
void lrg_water_body_set_cell_size (LrgWaterBody *self,
                                   gfloat        cell_size);

/**
 * lrg_water_body_get_foam:
 * @self: an #LrgWaterBody
 * @width: (out) (optional): the shore foam band along the outline
 * @depth: (out) (optional): the water depth below which the ground
 *   foams (with a ground function)
 */
LRG_AVAILABLE_IN_ALL
void lrg_water_body_get_foam (LrgWaterBody *self,
                              gfloat       *width,
                              gfloat       *depth);

/**
 * lrg_water_body_set_foam:
 * @self: an #LrgWaterBody
 * @width: shore band width (>= 0; 0 = none)
 * @depth: shallow depth (>= 0; 0 = none)
 */
LRG_AVAILABLE_IN_ALL
void lrg_water_body_set_foam (LrgWaterBody *self,
                              gfloat        width,
                              gfloat        depth);

/**
 * lrg_water_body_get_wave_count:
 * @self: an #LrgWaterBody
 *
 * Returns: the number of waves (0 .. %LRG_WATER_SURFACE_MAX_WAVES)
 */
LRG_AVAILABLE_IN_ALL
guint lrg_water_body_get_wave_count (LrgWaterBody *self);

/**
 * lrg_water_body_set_wave_count:
 * @self: an #LrgWaterBody
 * @count: 0 .. %LRG_WATER_SURFACE_MAX_WAVES (clamped)
 */
LRG_AVAILABLE_IN_ALL
void lrg_water_body_set_wave_count (LrgWaterBody *self,
                                    guint         count);

/**
 * lrg_water_body_get_wave:
 * @self: an #LrgWaterBody
 * @index: the wave
 * @out: (out caller-allocates): the wave
 *
 * Returns: %TRUE when @index is below the wave count
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_water_body_get_wave (LrgWaterBody *self,
                                  guint         index,
                                  LrgWaterWave *out);

/**
 * lrg_water_body_set_wave:
 * @self: an #LrgWaterBody
 * @index: 0 .. %LRG_WATER_SURFACE_MAX_WAVES - 1 (the count grows to
 *   include it)
 * @wave: the wave (direction normalised; amplitude >= 0, wavelength > 0,
 *   steepness 0 .. 1 are clamped)
 */
LRG_AVAILABLE_IN_ALL
void lrg_water_body_set_wave (LrgWaterBody       *self,
                              guint               index,
                              const LrgWaterWave *wave);

/**
 * lrg_water_body_get_wave_scale:
 * @self: an #LrgWaterBody
 *
 * Returns: the multiplier on every wave amplitude
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_water_body_get_wave_scale (LrgWaterBody *self);

/**
 * lrg_water_body_set_wave_scale:
 * @self: an #LrgWaterBody
 * @scale: amplitude multiplier (>= 0)
 */
LRG_AVAILABLE_IN_ALL
void lrg_water_body_set_wave_scale (LrgWaterBody *self,
                                    gfloat        scale);

/**
 * lrg_water_body_get_colors:
 * @self: an #LrgWaterBody
 * @shallow: (out caller-allocates) (optional): shallow colour
 * @deep: (out caller-allocates) (optional): deep colour
 * @foam: (out caller-allocates) (optional): foam colour
 */
LRG_AVAILABLE_IN_ALL
void lrg_water_body_get_colors (LrgWaterBody *self,
                                GrlColor     *shallow,
                                GrlColor     *deep,
                                GrlColor     *foam);

/**
 * lrg_water_body_set_colors:
 * @self: an #LrgWaterBody
 * @shallow: shallow colour
 * @deep: deep colour
 * @foam: foam colour
 */
LRG_AVAILABLE_IN_ALL
void lrg_water_body_set_colors (LrgWaterBody   *self,
                                const GrlColor *shallow,
                                const GrlColor *deep,
                                const GrlColor *foam);

/**
 * lrg_water_body_get_optics:
 * @self: an #LrgWaterBody
 * @transparency: (out) (optional): see lrg_water_surface_set_optics()
 * @reflection: (out) (optional): reflection share
 * @refraction: (out) (optional): refraction shift
 * @reflect_distance: (out) (optional): reflection ray length
 */
LRG_AVAILABLE_IN_ALL
void lrg_water_body_get_optics (LrgWaterBody *self,
                                gfloat       *transparency,
                                gfloat       *reflection,
                                gfloat       *refraction,
                                gfloat       *reflect_distance);

/**
 * lrg_water_body_set_optics:
 * @self: an #LrgWaterBody
 * @transparency: 0 .. 1
 * @reflection: 0 .. 1
 * @refraction: >= 0
 * @reflect_distance: > 0
 */
LRG_AVAILABLE_IN_ALL
void lrg_water_body_set_optics (LrgWaterBody *self,
                                gfloat        transparency,
                                gfloat        reflection,
                                gfloat        refraction,
                                gfloat        reflect_distance);

/**
 * lrg_water_body_contains:
 * @self: an #LrgWaterBody
 * @x: local X
 * @z: local Z
 * @out_level: (out) (optional): the rest height of the water there (a
 *   river's spline height at the nearest point)
 *
 * Whether a point lies inside the outline (buoyancy, swimming, editor
 * picking).
 *
 * Returns: %TRUE inside
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_water_body_contains (LrgWaterBody *self,
                                  gfloat        x,
                                  gfloat        z,
                                  gfloat       *out_level);

/**
 * lrg_water_body_get_bounds:
 * @self: an #LrgWaterBody
 * @out_min: (out caller-allocates) (array fixed-size=3): local minimum
 *   (the rest surface, without waves)
 * @out_max: (out caller-allocates) (array fixed-size=3): local maximum
 *
 * Returns: %FALSE for an outline that builds nothing
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_water_body_get_bounds (LrgWaterBody *self,
                                    gfloat       *out_min,
                                    gfloat       *out_max);

/**
 * lrg_water_body_apply_to_surface:
 * @self: an #LrgWaterBody
 * @surface: the surface to configure
 *
 * Copies the waves, wave scale, colours and optics onto @surface.
 */
LRG_AVAILABLE_IN_ALL
void lrg_water_body_apply_to_surface (LrgWaterBody    *self,
                                      LrgWaterSurface *surface);

/**
 * lrg_water_body_build_surface:
 * @self: an #LrgWaterBody
 * @surface: the surface whose patches are replaced
 * @offset_x: added to every local X (the draw-space position of the
 *   body's origin)
 * @offset_y: added to every height
 * @offset_z: added to every local Z
 * @ground_func: (scope call) (closure user_data) (nullable): the ground
 *   under the water (local coordinates) for depth foam, or %NULL
 * @user_data: data for @ground_func
 * @error: return location for a #GError
 *
 * Clears @surface and adds the body's grid patches: the outline's
 * bounding box cut into tiles of at most
 * %LRG_WATER_SURFACE_MAX_PATCH_VERTICES vertices, each keeping only the
 * cells whose centre lies inside the outline.  The shore factor is the
 * larger of the band (1 at the outline, 0 at the foam width inside) and
 * the depth (1 at depth 0, 0 at the foam depth).  Also applies
 * lrg_water_body_apply_to_surface().
 *
 * Returns: %TRUE when at least one patch was added
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_water_body_build_surface (LrgWaterBody        *self,
                                       LrgWaterSurface     *surface,
                                       gfloat               offset_x,
                                       gfloat               offset_y,
                                       gfloat               offset_z,
                                       LrgWaterGroundFunc   ground_func,
                                       gpointer             user_data,
                                       GError             **error);

G_END_DECLS
