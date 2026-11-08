/* lrg-lightmap-baker.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgLightmapBaker - bakes static lighting into an #LrgLightmap3D and
 * #LrgProbeGrid volumes with a multithreaded CPU path tracer.
 *
 * Inputs are static meshes (world transform, uv2 or none, linear albedo
 * and emission from the material, lightmap scale), lights (#LrgLight3DDesc
 * with spot cones and a bake mode), the sky (#LrgEnvironment or a flat
 * colour) and probe volumes.  lrg_lightmap_baker_add_world() collects
 * them from an #LrgWorld3D.  A bake:
 *
 *   1. prepare      unwraps meshes without uv2 (#LrgUvUnwrap), sizes each
 *                   object by #LrgLightmapBakeSettings:texels-per-unit x
 *                   its lightmap scale, packs the objects into atlas
 *                   pages, rasterizes their texels and builds the
 *                   #LrgBakeBvh;
 *   2. lightmaps    per texel: the direct light of %LRG_LIGHT_MODE_BAKED
 *                   lights (shadow rays to a jittered point of the
 *                   light's source radius, spot cones and falloff exactly
 *                   as the lit shader) plus N-bounce indirect light
 *                   (cosine-weighted paths with next-event estimation of
 *                   baked and mixed lights, emissive surfaces and the
 *                   sky), on a #GThreadPool;
 *   3. postprocess  an edge-preserving a-trous denoise guided by normals,
 *                   positions and the per-texel variance, dilation into
 *                   the gutters, and seam stitching across uv2 seams;
 *   4. probes       L2 spherical harmonics (the #LrgEnvironment
 *                   convention) of the radiance reaching each probe of
 *                   every volume, from the same tracer.
 *
 * Texels store irradiance / pi (linear), the term the lit shader
 * multiplies by the albedo.  A bake is deterministic for a given seed:
 * every texel and probe draws its random numbers from a hash of the seed
 * and its own index, so the thread count does not change the result.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <gio/gio.h>
#include "../lrg-version.h"
#include "../graphics/lrg-uv-unwrap.h"
#include "../world3d/lrg-world3d.h"
#include "lrg-environment.h"
#include "lrg-lightmap3d.h"
#include "lrg-probe-grid.h"

G_BEGIN_DECLS

/* ==========================================================================
 * Enums
 * ========================================================================== */

/**
 * LrgLightmapBakePhase:
 * @LRG_LIGHTMAP_BAKE_PHASE_PREPARE: unwrapping, packing, rasterizing,
 *   building the ray tracing hierarchy
 * @LRG_LIGHTMAP_BAKE_PHASE_LIGHTMAPS: tracing the lightmap texels
 * @LRG_LIGHTMAP_BAKE_PHASE_POSTPROCESS: denoise, dilation, seams
 * @LRG_LIGHTMAP_BAKE_PHASE_PROBES: tracing the probe volumes
 * @LRG_LIGHTMAP_BAKE_PHASE_DONE: the bake finished (fraction 1)
 *
 * The phase reported by #LrgLightmapBaker::progress.
 */
typedef enum
{
	LRG_LIGHTMAP_BAKE_PHASE_PREPARE,
	LRG_LIGHTMAP_BAKE_PHASE_LIGHTMAPS,
	LRG_LIGHTMAP_BAKE_PHASE_POSTPROCESS,
	LRG_LIGHTMAP_BAKE_PHASE_PROBES,
	LRG_LIGHTMAP_BAKE_PHASE_DONE
} LrgLightmapBakePhase;

LRG_AVAILABLE_IN_ALL
GType lrg_lightmap_bake_phase_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_LIGHTMAP_BAKE_PHASE (lrg_lightmap_bake_phase_get_type ())

/**
 * LrgLightmapBakeObjectFlags:
 * @LRG_LIGHTMAP_BAKE_OBJECT_NONE: no flag
 * @LRG_LIGHTMAP_BAKE_OBJECT_RECEIVE: the object gets a lightmap entry
 *   (needs a GUID)
 * @LRG_LIGHTMAP_BAKE_OBJECT_CONTRIBUTE: the object blocks and bounces
 *   light for the others
 * @LRG_LIGHTMAP_BAKE_OBJECT_DOUBLE_SIDED: rays hitting the back of the
 *   object see its front material (otherwise the back is black and marks
 *   texels inside geometry)
 *
 * How a mesh takes part in a bake.
 */
typedef enum /*< flags >*/
{
	LRG_LIGHTMAP_BAKE_OBJECT_NONE = 0,
	LRG_LIGHTMAP_BAKE_OBJECT_RECEIVE = 1 << 0,
	LRG_LIGHTMAP_BAKE_OBJECT_CONTRIBUTE = 1 << 1,
	LRG_LIGHTMAP_BAKE_OBJECT_DOUBLE_SIDED = 1 << 2
} LrgLightmapBakeObjectFlags;

LRG_AVAILABLE_IN_ALL
GType lrg_lightmap_bake_object_flags_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_LIGHTMAP_BAKE_OBJECT_FLAGS (lrg_lightmap_bake_object_flags_get_type ())

/**
 * LRG_LIGHTMAP_BAKE_OBJECT_DEFAULT:
 *
 * %LRG_LIGHTMAP_BAKE_OBJECT_RECEIVE | %LRG_LIGHTMAP_BAKE_OBJECT_CONTRIBUTE.
 */
#define LRG_LIGHTMAP_BAKE_OBJECT_DEFAULT (LRG_LIGHTMAP_BAKE_OBJECT_RECEIVE | LRG_LIGHTMAP_BAKE_OBJECT_CONTRIBUTE)

/* ==========================================================================
 * LrgLightmapBakeSettings
 * ========================================================================== */

#define LRG_TYPE_LIGHTMAP_BAKE_SETTINGS (lrg_lightmap_bake_settings_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgLightmapBakeSettings, lrg_lightmap_bake_settings, LRG, LIGHTMAP_BAKE_SETTINGS, GObject)

/**
 * lrg_lightmap_bake_settings_new:
 *
 * Creates settings with the defaults: 8 texels per unit, 64 samples per
 * texel, 2 bounces, 1024 texel pages, 2 texels of padding, denoise on,
 * probes every 2 units with 256 rays, seed 1, one thread per processor,
 * a 0.05 unit light radius and a 0.5 degree sun.  Every setting is a
 * property (set them with g_object_set(); the names match the level
 * format's `lighting: settings:` keys with `_` for `-`).
 *
 * Returns: (transfer full): new settings
 */
LRG_AVAILABLE_IN_ALL
LrgLightmapBakeSettings * lrg_lightmap_bake_settings_new (void);

/**
 * lrg_lightmap_bake_settings_copy:
 * @self: settings
 *
 * Returns: (transfer full): a copy of @self
 */
LRG_AVAILABLE_IN_ALL
LrgLightmapBakeSettings * lrg_lightmap_bake_settings_copy (LrgLightmapBakeSettings *self);

/**
 * lrg_lightmap_bake_settings_write_to_lightmap:
 * @self: settings
 * @lightmap: the lightmap to record them in
 *
 * Records every setting with lrg_lightmap3d_set_setting() (keys with
 * `_`, booleans as 0 / 1), as a bake does on its result.
 */
LRG_AVAILABLE_IN_ALL
void lrg_lightmap_bake_settings_write_to_lightmap (LrgLightmapBakeSettings *self,
                                                   LrgLightmap3D           *lightmap);

/* ==========================================================================
 * LrgLightmapBaker
 * ========================================================================== */

#define LRG_TYPE_LIGHTMAP_BAKER (lrg_lightmap_baker_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgLightmapBaker, lrg_lightmap_baker, LRG, LIGHTMAP_BAKER, GObject)

/**
 * lrg_lightmap_baker_new:
 *
 * Creates an empty baker with default settings and a black sky.
 *
 * Returns: (transfer full): a new #LrgLightmapBaker
 */
LRG_AVAILABLE_IN_ALL
LrgLightmapBaker * lrg_lightmap_baker_new (void);

/**
 * lrg_lightmap_baker_get_settings:
 * @self: an #LrgLightmapBaker
 *
 * The settings the next bake reads (when it starts; changes during a bake
 * apply to the next one).
 *
 * Returns: (transfer none): the settings
 */
LRG_AVAILABLE_IN_ALL
LrgLightmapBakeSettings * lrg_lightmap_baker_get_settings (LrgLightmapBaker *self);

/**
 * lrg_lightmap_baker_set_settings:
 * @self: an #LrgLightmapBaker
 * @settings: the settings (a reference is kept)
 *
 * Sets #LrgLightmapBaker:settings.
 */
LRG_AVAILABLE_IN_ALL
void lrg_lightmap_baker_set_settings (LrgLightmapBaker        *self,
                                      LrgLightmapBakeSettings *settings);

/**
 * lrg_lightmap_baker_clear:
 * @self: an #LrgLightmapBaker
 *
 * Drops every mesh, light, probe volume and the sky (the results of the
 * last bake stay until the next one).  Not allowed while baking.
 */
LRG_AVAILABLE_IN_ALL
void lrg_lightmap_baker_clear (LrgLightmapBaker *self);

/**
 * lrg_lightmap_baker_add_mesh:
 * @self: an #LrgLightmapBaker
 * @guid: (nullable): the node / entity GUID the lightmap entry is keyed
 *   by; required with %LRG_LIGHTMAP_BAKE_OBJECT_RECEIVE
 * @mesh: the object-space geometry; its uv2 is used when present,
 *   otherwise the baker unwraps it with the #LrgUvUnwrap defaults (see
 *   lrg_lightmap_baker_get_object_mesh()); normals are computed when it
 *   has none
 * @matrix: (array fixed-size=16) (nullable): column-major object to world
 *   transform, %NULL for identity
 * @albedo: (array fixed-size=3) (nullable): base colour in display
 *   (sRGB) space, as a material holds it; %NULL for white
 * @emission: (array fixed-size=3) (nullable): emitted colour in display
 *   space already multiplied by the emission strength (what the lit
 *   shader's emissionOwn holds); %NULL for none
 * @lightmap_scale: multiplier on the texel density (1 = the settings'
 *   #LrgLightmapBakeSettings:texels-per-unit); 0 drops
 *   %LRG_LIGHTMAP_BAKE_OBJECT_RECEIVE (the object is probe lit)
 * @flags: how the mesh takes part
 *
 * Adds a static mesh.  Not allowed while baking.
 *
 * Returns: %FALSE when the input is unusable (no triangles, receive
 *   without a GUID, a GUID added twice)
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_lightmap_baker_add_mesh (LrgLightmapBaker           *self,
                                      const gchar                *guid,
                                      LrgUvMesh                  *mesh,
                                      const gfloat               *matrix,
                                      const gfloat               *albedo,
                                      const gfloat               *emission,
                                      gfloat                      lightmap_scale,
                                      LrgLightmapBakeObjectFlags  flags);

/**
 * lrg_lightmap_baker_add_light:
 * @self: an #LrgLightmapBaker
 * @desc: the light (its #LrgLight3DDesc.bake_mode decides its part:
 *   realtime lights are ignored, mixed lights only bounce, baked lights
 *   also store their direct light in the lightmaps)
 * @source_radius: radius of the light's emitting sphere in world units
 *   for soft shadows (for a directional light: the angular radius in
 *   degrees); negative for the settings' default
 *   (#LrgLightmapBakeSettings:light-radius / :sun-angle)
 *
 * Adds a light (copied).  Disabled and realtime lights are skipped.
 *
 * Returns: %TRUE when the light takes part in the bake
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_lightmap_baker_add_light (LrgLightmapBaker     *self,
                                       const LrgLight3DDesc *desc,
                                       gfloat                source_radius);

/**
 * lrg_lightmap_baker_set_environment:
 * @self: an #LrgLightmapBaker
 * @environment: (nullable): the sky, sampled with
 *   lrg_environment_sample_sky() when the bake starts; %NULL for the
 *   flat sky colour
 *
 * Sets the sky that rays escaping the scene see.
 */
LRG_AVAILABLE_IN_ALL
void lrg_lightmap_baker_set_environment (LrgLightmapBaker *self,
                                         LrgEnvironment   *environment);

/**
 * lrg_lightmap_baker_set_sky_color:
 * @self: an #LrgLightmapBaker
 * @r: linear red radiance
 * @g: linear green radiance
 * @b: linear blue radiance
 *
 * The flat sky used without an environment (black by default: an
 * interior bake).
 */
LRG_AVAILABLE_IN_ALL
void lrg_lightmap_baker_set_sky_color (LrgLightmapBaker *self,
                                       gfloat            r,
                                       gfloat            g,
                                       gfloat            b);

/**
 * lrg_lightmap_baker_add_probe_volume:
 * @self: an #LrgLightmapBaker
 * @min: (array fixed-size=3): smallest corner
 * @max: (array fixed-size=3): largest corner
 * @nx: probes along X (0 = from #LrgLightmapBakeSettings:probe-spacing)
 * @ny: probes along Y (0 = from the spacing)
 * @nz: probes along Z (0 = from the spacing)
 *
 * Adds a box of probes, baked into one #LrgProbeGrid.  Without any
 * volume and with #LrgLightmapBakeSettings:auto-probe-volume set, the
 * bake covers the bounds of the static geometry.
 */
LRG_AVAILABLE_IN_ALL
void lrg_lightmap_baker_add_probe_volume (LrgLightmapBaker *self,
                                          const gfloat     *min,
                                          const gfloat     *max,
                                          guint             nx,
                                          guint             ny,
                                          guint             nz);

/**
 * lrg_lightmap_baker_add_world:
 * @self: an #LrgLightmapBaker
 * @world: the world to collect from
 *
 * Collects every active mesh renderer flagged for static lighting (the
 * component's `static-lighting` property, or the entity's static flag
 * when the component has none) with its geometry (the loaded model, a
 * glTF read headless from the mesh asset path, or the built-in
 * primitive), world matrix, material colours, `lightmap-scale` and GUID;
 * every registered light that is not realtime (soft shadow radius from a
 * `source-radius` property of the light's source when it has one); the
 * world's environment as the sky; and the world's probe grids as probe
 * volumes.
 *
 * Returns: the number of meshes added
 */
LRG_AVAILABLE_IN_ALL
guint lrg_lightmap_baker_add_world (LrgLightmapBaker *self,
                                    LrgWorld3D       *world);

/**
 * lrg_lightmap_baker_get_n_objects:
 * @self: an #LrgLightmapBaker
 *
 * Returns: the meshes added
 */
LRG_AVAILABLE_IN_ALL
guint lrg_lightmap_baker_get_n_objects (LrgLightmapBaker *self);

/**
 * lrg_lightmap_baker_get_n_lights:
 * @self: an #LrgLightmapBaker
 *
 * Returns: the lights taking part
 */
LRG_AVAILABLE_IN_ALL
guint lrg_lightmap_baker_get_n_lights (LrgLightmapBaker *self);

/**
 * lrg_lightmap_baker_bake:
 * @self: an #LrgLightmapBaker
 * @cancellable: (nullable): a #GCancellable
 * @error: (nullable): return location for a #GError
 *
 * Bakes on the calling thread (the tracing itself still runs on the
 * thread pool); #LrgLightmapBaker::progress is emitted on the calling
 * thread.  Prefer lrg_lightmap_baker_bake_async() in an interactive
 * application.
 *
 * Returns: %TRUE on success; %FALSE with %G_IO_ERROR_CANCELLED when
 *   cancelled, %G_IO_ERROR_PENDING when a bake runs, or the error of a
 *   failed step
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_lightmap_baker_bake (LrgLightmapBaker  *self,
                                  GCancellable      *cancellable,
                                  GError           **error);

/**
 * lrg_lightmap_baker_bake_async:
 * @self: an #LrgLightmapBaker
 * @cancellable: (nullable): a #GCancellable
 * @callback: (scope async): called when the bake finishes
 * @user_data: data for @callback
 *
 * Bakes on a worker thread.  The inputs are snapshotted (the sky is
 * sampled) before this returns, so the world may change during the bake.
 * #LrgLightmapBaker::progress is emitted on the thread-default main
 * context of the caller.  Results are available from
 * lrg_lightmap_baker_get_lightmap() after lrg_lightmap_baker_bake_finish().
 */
LRG_AVAILABLE_IN_ALL
void lrg_lightmap_baker_bake_async (LrgLightmapBaker    *self,
                                    GCancellable        *cancellable,
                                    GAsyncReadyCallback  callback,
                                    gpointer             user_data);

/**
 * lrg_lightmap_baker_bake_finish:
 * @self: an #LrgLightmapBaker
 * @result: the #GAsyncResult
 * @error: (nullable): return location for a #GError
 *
 * Returns: %TRUE when the bake succeeded
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_lightmap_baker_bake_finish (LrgLightmapBaker  *self,
                                         GAsyncResult      *result,
                                         GError           **error);

/**
 * lrg_lightmap_baker_is_running:
 * @self: an #LrgLightmapBaker
 *
 * Returns: %TRUE while a bake runs (#LrgLightmapBaker:running)
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_lightmap_baker_is_running (LrgLightmapBaker *self);

/**
 * lrg_lightmap_baker_get_lightmap:
 * @self: an #LrgLightmapBaker
 *
 * The lightmap of the last successful bake: one page per atlas page, one
 * entry per receiving object, the settings recorded.  Hand it to
 * lrg_world3d_set_lightmap() or save it with lrg_lightmap3d_save().
 *
 * Returns: (transfer none) (nullable): the lightmap
 */
LRG_AVAILABLE_IN_ALL
LrgLightmap3D * lrg_lightmap_baker_get_lightmap (LrgLightmapBaker *self);

/**
 * lrg_lightmap_baker_get_probe_grids:
 * @self: an #LrgLightmapBaker
 *
 * Returns: (transfer none) (element-type LrgProbeGrid): the probe grids
 *   of the last successful bake, one per volume
 */
LRG_AVAILABLE_IN_ALL
GPtrArray * lrg_lightmap_baker_get_probe_grids (LrgLightmapBaker *self);

/**
 * lrg_lightmap_baker_get_object_mesh:
 * @self: an #LrgLightmapBaker
 * @guid: an object GUID
 *
 * The mesh the last bake used for an object: the input mesh when it had
 * uv2, else its unwrapped copy.  A renderer drawing a lightmapped object
 * whose mesh has no uv2 uploads this one (or regenerates it, see
 * lrg_uv_unwrap_new_lightmap_primitive()).
 *
 * Returns: (transfer none) (nullable): the mesh
 */
LRG_AVAILABLE_IN_ALL
LrgUvMesh * lrg_lightmap_baker_get_object_mesh (LrgLightmapBaker *self,
                                                const gchar      *guid);

/**
 * lrg_lightmap_baker_get_page_data:
 * @self: an #LrgLightmapBaker
 * @page: atlas page index
 * @out_width: (out) (optional): page width in texels
 * @out_height: (out) (optional): page height in texels
 *
 * The raw result of the last bake: linear RGB floats, rows from the top
 * (atlas v = 0).
 *
 * Returns: (transfer none) (nullable): the texels, or %NULL for a page
 *   that does not exist
 */
LRG_AVAILABLE_IN_ALL
const gfloat * lrg_lightmap_baker_get_page_data (LrgLightmapBaker *self,
                                                 guint             page,
                                                 guint            *out_width,
                                                 guint            *out_height);

/**
 * lrg_lightmap_baker_evaluate:
 * @self: an #LrgLightmapBaker
 * @position: (array fixed-size=3): a world point on (or just above) a
 *   surface
 * @normal: (array fixed-size=3): the surface normal there
 * @out_rgb: (out caller-allocates) (array fixed-size=3): irradiance / pi
 *   as a texel there would store it (baked direct + indirect), before
 *   denoising
 *
 * Runs the texel estimator at one point with the current settings, over
 * the scene of the last bake (or a scene built now when there was none
 * or the inputs changed).  For tools and tests: an editor's "probe the
 * light here" or a check against an analytic value.
 *
 * Returns: %FALSE when there is nothing to trace or a bake is running
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_lightmap_baker_evaluate (LrgLightmapBaker *self,
                                      const gfloat     *position,
                                      const gfloat     *normal,
                                      gfloat           *out_rgb);

/**
 * lrg_lightmap_baker_get_bake_time:
 * @self: an #LrgLightmapBaker
 *
 * Returns: wall-clock seconds the last bake took (0 before one)
 */
LRG_AVAILABLE_IN_ALL
gdouble lrg_lightmap_baker_get_bake_time (LrgLightmapBaker *self);

/**
 * lrg_lightmap_baker_get_n_texels:
 * @self: an #LrgLightmapBaker
 *
 * Returns: the texels the last bake traced (covered texels only)
 */
LRG_AVAILABLE_IN_ALL
guint lrg_lightmap_baker_get_n_texels (LrgLightmapBaker *self);

G_END_DECLS
