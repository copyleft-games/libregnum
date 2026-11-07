/* lrg-probe-grid.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgProbeGrid - a box of baked irradiance probes (L2 spherical
 * harmonics) lighting dynamic and non-lightmapped objects.
 *
 * The grid spans an axis-aligned box with nx x ny x nz probes on the
 * lattice points (the box corners included; one probe on an axis sits at
 * the box centre).  Each probe holds 27 floats: the 9 L2 SH coefficients
 * x RGB of the irradiance / pi around it, in exactly the convention of
 * #LrgEnvironment (lrg_environment_sh_irradiance() evaluates one), plus
 * a validity flag (FALSE for a probe the baker found inside geometry).
 *
 * Invalid probes are filled from their valid neighbours when the grid is
 * resolved (sampled or uploaded), so trilinear interpolation never pulls
 * the black of a probe buried in a wall.  The CPU sample
 * (lrg_probe_grid_sample()) and the shader read the same resolved data.
 *
 * On disk a grid is a `.rprobes` YAML file: header keys plus the
 * coefficients as base64 little-endian float32 (lrg_probe_grid_save()).
 * The data side is headless; lrg_probe_grid_upload() needs a GL context
 * (one RGBA16F 3D texture of nx x ny x 7 nz texels).
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"

G_BEGIN_DECLS

/**
 * LRG_PROBE_GRID_SH_FLOATS:
 *
 * Floats of one probe: 9 coefficients x RGB, coefficient-major.
 */
#define LRG_PROBE_GRID_SH_FLOATS (27)

/**
 * LRG_PROBE_GRID_MAX_RESOLUTION:
 *
 * Most probes along one axis.
 */
#define LRG_PROBE_GRID_MAX_RESOLUTION (64)

/**
 * LRG_PROBE_GRID_FORMAT_VERSION:
 *
 * The `version:` written into `.rprobes` files; newer files are refused.
 */
#define LRG_PROBE_GRID_FORMAT_VERSION (1)

/**
 * LrgProbeGridError:
 * @LRG_PROBE_GRID_ERROR_FAILED: generic failure
 * @LRG_PROBE_GRID_ERROR_IO: the file could not be read or written
 * @LRG_PROBE_GRID_ERROR_FORMAT: the file is malformed
 * @LRG_PROBE_GRID_ERROR_VERSION: the file is of a newer format version
 * @LRG_PROBE_GRID_ERROR_GPU: no GL context or the upload failed
 *
 * Errors of #LrgProbeGrid.
 */
typedef enum
{
	LRG_PROBE_GRID_ERROR_FAILED,
	LRG_PROBE_GRID_ERROR_IO,
	LRG_PROBE_GRID_ERROR_FORMAT,
	LRG_PROBE_GRID_ERROR_VERSION,
	LRG_PROBE_GRID_ERROR_GPU
} LrgProbeGridError;

/**
 * LRG_PROBE_GRID_ERROR:
 *
 * Error domain of #LrgProbeGridError.
 */
#define LRG_PROBE_GRID_ERROR (lrg_probe_grid_error_quark ())

#define LRG_TYPE_PROBE_GRID_ERROR (lrg_probe_grid_error_get_type ())

/**
 * lrg_probe_grid_error_get_type:
 *
 * Returns: the #GType of #LrgProbeGridError
 */
LRG_AVAILABLE_IN_ALL
GType lrg_probe_grid_error_get_type (void) G_GNUC_CONST;

/**
 * lrg_probe_grid_error_quark:
 *
 * Returns: the error quark of %LRG_PROBE_GRID_ERROR
 */
LRG_AVAILABLE_IN_ALL
GQuark lrg_probe_grid_error_quark (void);

#define LRG_TYPE_PROBE_GRID (lrg_probe_grid_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgProbeGrid, lrg_probe_grid, LRG, PROBE_GRID, GObject)

/**
 * lrg_probe_grid_new:
 *
 * Creates a 2 x 2 x 2 grid over the box (-1, -1, -1) .. (1, 1, 1) with
 * every probe invalid (black), intensity 1.
 *
 * Returns: (transfer full): a new #LrgProbeGrid
 */
LRG_AVAILABLE_IN_ALL
LrgProbeGrid * lrg_probe_grid_new (void);

/**
 * lrg_probe_grid_new_with_bounds:
 * @min: (array fixed-size=3): the box's lower corner
 * @max: (array fixed-size=3): the box's upper corner
 * @nx: probes along X (1 .. %LRG_PROBE_GRID_MAX_RESOLUTION)
 * @ny: probes along Y
 * @nz: probes along Z
 *
 * Returns: (transfer full): a new grid, every probe invalid
 */
LRG_AVAILABLE_IN_ALL
LrgProbeGrid * lrg_probe_grid_new_with_bounds (const gfloat *min,
                                               const gfloat *max,
                                               guint         nx,
                                               guint         ny,
                                               guint         nz);

/**
 * lrg_probe_grid_new_from_file:
 * @path: a `.rprobes` file
 * @error: return location for a #GError
 *
 * Returns: (transfer full) (nullable): the grid, %NULL with @error
 */
LRG_AVAILABLE_IN_ALL
LrgProbeGrid * lrg_probe_grid_new_from_file (const gchar  *path,
                                             GError      **error);

/**
 * lrg_probe_grid_save:
 * @self: an #LrgProbeGrid
 * @path: the `.rprobes` file to write
 * @error: return location for a #GError
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_probe_grid_save (LrgProbeGrid  *self,
                              const gchar   *path,
                              GError       **error);

/**
 * lrg_probe_grid_get_path:
 * @self: an #LrgProbeGrid
 *
 * Returns: (transfer none) (nullable): the file last loaded or saved
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_probe_grid_get_path (LrgProbeGrid *self);

/* --- layout ------------------------------------------------------------- */

/**
 * lrg_probe_grid_set_bounds:
 * @self: an #LrgProbeGrid
 * @min: (array fixed-size=3): lower corner
 * @max: (array fixed-size=3): upper corner (components below @min are
 *   swapped with it)
 *
 * Moves the box; the probes keep their data.  Emits
 * #LrgProbeGrid::changed.
 */
LRG_AVAILABLE_IN_ALL
void lrg_probe_grid_set_bounds (LrgProbeGrid *self,
                                const gfloat *min,
                                const gfloat *max);

/**
 * lrg_probe_grid_get_bounds:
 * @self: an #LrgProbeGrid
 * @out_min: (out caller-allocates) (array fixed-size=3) (optional): lower corner
 * @out_max: (out caller-allocates) (array fixed-size=3) (optional): upper corner
 */
LRG_AVAILABLE_IN_ALL
void lrg_probe_grid_get_bounds (LrgProbeGrid *self,
                                gfloat       *out_min,
                                gfloat       *out_max);

/**
 * lrg_probe_grid_set_resolution:
 * @self: an #LrgProbeGrid
 * @nx: probes along X (clamped into 1 .. %LRG_PROBE_GRID_MAX_RESOLUTION)
 * @ny: probes along Y
 * @nz: probes along Z
 *
 * Changes the probe count; every probe becomes invalid and black.
 * Emits #LrgProbeGrid::changed.
 */
LRG_AVAILABLE_IN_ALL
void lrg_probe_grid_set_resolution (LrgProbeGrid *self,
                                    guint         nx,
                                    guint         ny,
                                    guint         nz);

/**
 * lrg_probe_grid_get_resolution:
 * @self: an #LrgProbeGrid
 * @out_nx: (out) (optional): probes along X
 * @out_ny: (out) (optional): probes along Y
 * @out_nz: (out) (optional): probes along Z
 */
LRG_AVAILABLE_IN_ALL
void lrg_probe_grid_get_resolution (LrgProbeGrid *self,
                                    guint        *out_nx,
                                    guint        *out_ny,
                                    guint        *out_nz);

/**
 * lrg_probe_grid_get_n_probes:
 * @self: an #LrgProbeGrid
 *
 * Returns: nx x ny x nz
 */
LRG_AVAILABLE_IN_ALL
guint lrg_probe_grid_get_n_probes (LrgProbeGrid *self);

/**
 * lrg_probe_grid_get_index:
 * @self: an #LrgProbeGrid
 * @x: probe column (0 .. nx - 1)
 * @y: probe row (0 .. ny - 1)
 * @z: probe layer (0 .. nz - 1)
 *
 * Returns: the linear index x + nx (y + ny z), %G_MAXUINT when out of range
 */
LRG_AVAILABLE_IN_ALL
guint lrg_probe_grid_get_index (LrgProbeGrid *self,
                                guint         x,
                                guint         y,
                                guint         z);

/**
 * lrg_probe_grid_get_probe_position:
 * @self: an #LrgProbeGrid
 * @index: a linear probe index
 * @out_position: (out caller-allocates) (array fixed-size=3): its world
 *   position
 *
 * Returns: %FALSE when @index is out of range
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_probe_grid_get_probe_position (LrgProbeGrid *self,
                                            guint         index,
                                            gfloat       *out_position);

/* --- probe data --------------------------------------------------------- */

/**
 * lrg_probe_grid_set_probe:
 * @self: an #LrgProbeGrid
 * @index: a linear probe index
 * @sh: (array fixed-size=27): the probe's coefficients
 * @valid: whether the probe is usable
 *
 * Emits #LrgProbeGrid::changed.  A baker filling many probes may write
 * through lrg_probe_grid_get_data() and call
 * lrg_probe_grid_notify_changed() once instead.
 *
 * Returns: %FALSE when @index is out of range
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_probe_grid_set_probe (LrgProbeGrid *self,
                                   guint         index,
                                   const gfloat *sh,
                                   gboolean      valid);

/**
 * lrg_probe_grid_get_probe:
 * @self: an #LrgProbeGrid
 * @index: a linear probe index
 * @out_sh: (out caller-allocates) (array fixed-size=27) (optional): the
 *   stored (unresolved) coefficients
 *
 * Returns: whether the probe is valid (%FALSE out of range)
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_probe_grid_get_probe (LrgProbeGrid *self,
                                   guint         index,
                                   gfloat       *out_sh);

/**
 * lrg_probe_grid_set_valid:
 * @self: an #LrgProbeGrid
 * @index: a linear probe index
 * @valid: the new validity
 *
 * Emits #LrgProbeGrid::changed.
 */
LRG_AVAILABLE_IN_ALL
void lrg_probe_grid_set_valid (LrgProbeGrid *self,
                               guint         index,
                               gboolean      valid);

/**
 * lrg_probe_grid_get_n_valid:
 * @self: an #LrgProbeGrid
 *
 * Returns: the number of valid probes
 */
LRG_AVAILABLE_IN_ALL
guint lrg_probe_grid_get_n_valid (LrgProbeGrid *self);

/**
 * lrg_probe_grid_get_data:
 * @self: an #LrgProbeGrid
 * @n_floats: (out) (optional): 27 x the probe count
 *
 * The coefficient storage, probe after probe in linear index order.
 * Writes are allowed; call lrg_probe_grid_notify_changed() afterwards.
 *
 * Returns: (transfer none) (array length=n_floats): the coefficients
 */
LRG_AVAILABLE_IN_ALL
gfloat * lrg_probe_grid_get_data (LrgProbeGrid *self,
                                  gsize        *n_floats);

/**
 * lrg_probe_grid_get_validity:
 * @self: an #LrgProbeGrid
 * @n_probes: (out) (optional): the probe count
 *
 * The validity flags (0 / 1), one per probe; writable like
 * lrg_probe_grid_get_data().
 *
 * Returns: (transfer none) (array length=n_probes): the flags
 */
LRG_AVAILABLE_IN_ALL
guint8 * lrg_probe_grid_get_validity (LrgProbeGrid *self,
                                      guint        *n_probes);

/**
 * lrg_probe_grid_notify_changed:
 * @self: an #LrgProbeGrid
 *
 * Marks the data as written in place: re-resolves on the next sample,
 * re-uploads on the next upload, emits #LrgProbeGrid::changed.
 */
LRG_AVAILABLE_IN_ALL
void lrg_probe_grid_notify_changed (LrgProbeGrid *self);

/* --- sampling ----------------------------------------------------------- */

/**
 * lrg_probe_grid_contains:
 * @self: an #LrgProbeGrid
 * @position: (array fixed-size=3): a world position
 *
 * Returns: whether @position is inside the box (borders included)
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_probe_grid_contains (LrgProbeGrid *self,
                                  const gfloat *position);

/**
 * lrg_probe_grid_sample_sh:
 * @self: an #LrgProbeGrid
 * @position: (array fixed-size=3): a world position (clamped into the box)
 * @out_sh: (out caller-allocates) (array fixed-size=27): the trilinear
 *   blend of the resolved coefficients of the eight surrounding probes,
 *   times the intensity
 *
 * Returns: %FALSE (zeros) when the grid has no valid probe
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_probe_grid_sample_sh (LrgProbeGrid *self,
                                   const gfloat *position,
                                   gfloat       *out_sh);

/**
 * lrg_probe_grid_sample:
 * @self: an #LrgProbeGrid
 * @position: (array fixed-size=3): a world position (clamped into the box)
 * @normal: (array fixed-size=3): a unit normal
 * @out_rgb: (out caller-allocates) (array fixed-size=3): irradiance / pi
 *   at @position for a surface facing @normal (linear), as the shader
 *   computes it
 *
 * lrg_probe_grid_sample_sh() evaluated with
 * lrg_environment_sh_irradiance().
 *
 * Returns: %FALSE (black) when the grid has no valid probe
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_probe_grid_sample (LrgProbeGrid *self,
                                const gfloat *position,
                                const gfloat *normal,
                                gfloat       *out_rgb);

/**
 * lrg_probe_grid_set_intensity:
 * @self: an #LrgProbeGrid
 * @intensity: multiplier on every probe (>= 0)
 *
 * Sets #LrgProbeGrid:intensity.
 */
LRG_AVAILABLE_IN_ALL
void lrg_probe_grid_set_intensity (LrgProbeGrid *self,
                                   gfloat        intensity);

/**
 * lrg_probe_grid_get_intensity:
 * @self: an #LrgProbeGrid
 *
 * Returns: #LrgProbeGrid:intensity
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_probe_grid_get_intensity (LrgProbeGrid *self);

/* --- GPU ---------------------------------------------------------------- */

/**
 * lrg_probe_grid_upload:
 * @self: an #LrgProbeGrid
 * @error: return location for a #GError
 *
 * Uploads the resolved coefficients (when changed) into an RGBA16F 3D
 * texture of nx x ny x (7 nz) texels: group g (coefficients 4g .. 4g+3
 * of the 28-float padded probe, the 28th float its validity) is the
 * slab of layers g nz .. g nz + nz - 1.  Needs a GL context.
 *
 * Returns: %TRUE when the texture exists
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_probe_grid_upload (LrgProbeGrid  *self,
                                GError       **error);

/**
 * lrg_probe_grid_unload_gpu:
 * @self: an #LrgProbeGrid
 *
 * Frees the GPU texture (re-made by the next upload).
 */
LRG_AVAILABLE_IN_ALL
void lrg_probe_grid_unload_gpu (LrgProbeGrid *self);

/**
 * lrg_probe_grid_get_texture:
 * @self: an #LrgProbeGrid
 *
 * Returns: the GL id of the 3D texture, 0 when not uploaded (or stale)
 */
LRG_AVAILABLE_IN_ALL
guint lrg_probe_grid_get_texture (LrgProbeGrid *self);

G_END_DECLS
