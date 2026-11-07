/* lrg-environment.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgEnvironment - the sky, image-based lighting (IBL), ambient light,
 * fog and volumetric settings of a 3D scene.
 *
 * An environment describes where the light that does not come from a
 * light source comes from: a sky source (a flat colour, a three-colour
 * gradient, an #LrgSkyDome, a cubemap from six images or a cross, or an
 * equirectangular #LrgHdrImage), its exposure and rotation, and the
 * flat ambient colour used when image-based lighting is off.  Baking it
 * (lrg_environment_bake(), needs GL) derives the GPU maps the lit
 * shader samples through the LRG_ENV_GLSL hook of #LrgLightSet:
 *
 * - a radiance cubemap (the sky itself, drawn by
 *   lrg_environment_draw_sky()),
 * - the diffuse irradiance as 9 L2 spherical-harmonic coefficients per
 *   colour channel (uniforms, no texture),
 * - a prefiltered specular cubemap whose mip levels hold the radiance
 *   convolved with the GGX lobe of growing roughness,
 * - a 2D BRDF lookup table (scale and bias of F0 for the split-sum).
 *
 * The environment also carries the scene's fog (applied to an
 * #LrgLightSet by lrg_environment_apply_to_light_set()), the parameters
 * of the screen-space #LrgVolumetricFog, the post-processing stack
 * reference of the level and a set of #LrgReflectionProbe objects; the
 * two probes nearest to the reference point are bound with it.
 *
 * Every setting is headless.  Environments read and write the
 * `environment:` block of `.rlevel` files (lrg_environment_load_from_data(),
 * lrg_environment_to_data()); unknown keys are kept.
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
#include "../graphics/lrg-hdr-image.h"
#include "../graphics/lrg-sky-dome.h"
#include "../graphics/lrg-light-set.h"
#include "lrg-reflection-probe.h"

G_BEGIN_DECLS

/**
 * LRG_ENVIRONMENT_DEFAULT_RESOLUTION:
 *
 * Default edge length in pixels of the radiance and prefiltered
 * specular cubemaps.
 */
#define LRG_ENVIRONMENT_DEFAULT_RESOLUTION (128)

/**
 * LRG_ENVIRONMENT_MAX_RESOLUTION:
 *
 * Largest cubemap edge accepted by lrg_environment_set_resolution().
 */
#define LRG_ENVIRONMENT_MAX_RESOLUTION (1024)

/**
 * LRG_ENVIRONMENT_BRDF_LUT_SIZE:
 *
 * Edge length of the BRDF lookup table a bake uploads.
 */
#define LRG_ENVIRONMENT_BRDF_LUT_SIZE (64)

/**
 * LRG_ENVIRONMENT_SH_FLOATS:
 *
 * Number of floats of the irradiance spherical harmonics: 9 L2
 * coefficients times 3 colour channels, coefficient-major
 * (c0.rgb, c1.rgb, ... c8.rgb).
 */
#define LRG_ENVIRONMENT_SH_FLOATS (27)

/**
 * LRG_ENVIRONMENT_TEXTURE_UNITS:
 *
 * Most texture units the lit-shader binder claims from the unit it is
 * handed (the prefiltered map, the BRDF table and two probe cubemaps).
 * It claims fewer when the light set's hook window is short: on a
 * driver with only 16 units just the prefiltered map (an analytic BRDF
 * fit replaces the table and probes are not bound).
 */
#define LRG_ENVIRONMENT_TEXTURE_UNITS (4)

/**
 * LRG_ENVIRONMENT_MAX_BOUND_PROBES:
 *
 * Reflection probes blended per draw.
 */
#define LRG_ENVIRONMENT_MAX_BOUND_PROBES (2)

/**
 * LrgEnvironmentSkySource:
 * @LRG_ENVIRONMENT_SKY_COLOR: one flat colour in every direction
 * @LRG_ENVIRONMENT_SKY_GRADIENT: ground / horizon / zenith colours
 *   blended by the height of the direction
 * @LRG_ENVIRONMENT_SKY_DOME: an #LrgSkyDome (captured on the GPU, so its
 *   sun and band show in reflections); the rotation is ignored
 * @LRG_ENVIRONMENT_SKY_CUBEMAP: six face images (+X, -X, +Y, -Y, +Z, -Z)
 *   or one cross image
 * @LRG_ENVIRONMENT_SKY_EQUIRECT: an equirectangular (latitude/longitude)
 *   panorama, usually a Radiance .hdr
 *
 * Where the environment's light comes from.
 */
typedef enum
{
	LRG_ENVIRONMENT_SKY_COLOR,
	LRG_ENVIRONMENT_SKY_GRADIENT,
	LRG_ENVIRONMENT_SKY_DOME,
	LRG_ENVIRONMENT_SKY_CUBEMAP,
	LRG_ENVIRONMENT_SKY_EQUIRECT
} LrgEnvironmentSkySource;

#define LRG_TYPE_ENVIRONMENT_SKY_SOURCE (lrg_environment_sky_source_get_type ())

/**
 * lrg_environment_sky_source_get_type:
 *
 * Returns: the #GType of #LrgEnvironmentSkySource
 */
LRG_AVAILABLE_IN_ALL
GType lrg_environment_sky_source_get_type (void) G_GNUC_CONST;

/**
 * LRG_ENVIRONMENT_ERROR:
 *
 * Error domain of #LrgEnvironment.  Errors carry an #LrgEnvironmentError
 * code.
 */
#define LRG_ENVIRONMENT_ERROR (lrg_environment_error_quark ())

/**
 * LrgEnvironmentError:
 * @LRG_ENVIRONMENT_ERROR_FAILED: generic failure
 * @LRG_ENVIRONMENT_ERROR_IO: a file could not be read or written
 * @LRG_ENVIRONMENT_ERROR_PARSE: the YAML is malformed
 * @LRG_ENVIRONMENT_ERROR_SOURCE: a sky image is missing, unreadable or
 *   of the wrong shape, or a `guid:` does not resolve
 * @LRG_ENVIRONMENT_ERROR_GPU: no GL context, or a GPU resource could not
 *   be created
 *
 * Error codes of %LRG_ENVIRONMENT_ERROR.
 */
typedef enum
{
	LRG_ENVIRONMENT_ERROR_FAILED,
	LRG_ENVIRONMENT_ERROR_IO,
	LRG_ENVIRONMENT_ERROR_PARSE,
	LRG_ENVIRONMENT_ERROR_SOURCE,
	LRG_ENVIRONMENT_ERROR_GPU
} LrgEnvironmentError;

#define LRG_TYPE_ENVIRONMENT_ERROR (lrg_environment_error_get_type ())

/**
 * lrg_environment_error_get_type:
 *
 * Returns: the #GType of #LrgEnvironmentError
 */
LRG_AVAILABLE_IN_ALL
GType lrg_environment_error_get_type (void) G_GNUC_CONST;

/**
 * lrg_environment_error_quark:
 *
 * Returns: the error quark of %LRG_ENVIRONMENT_ERROR
 */
LRG_AVAILABLE_IN_ALL
GQuark lrg_environment_error_quark (void);

#define LRG_TYPE_ENVIRONMENT (lrg_environment_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgEnvironment, lrg_environment, LRG, ENVIRONMENT, GObject)

/**
 * lrg_environment_new:
 *
 * Creates an environment with a mid-grey gradient sky, exposure 0,
 * image-based lighting on, a dim grey ambient, no fog and volumetrics
 * off.
 *
 * Returns: (transfer full): a new #LrgEnvironment
 */
LRG_AVAILABLE_IN_ALL
LrgEnvironment *
lrg_environment_new (void);

/* ==========================================================================
 * Format
 * ========================================================================== */

/**
 * lrg_environment_new_from_file:
 * @path: a YAML file whose root mapping is an environment block (or a
 *   mapping with an `environment:` member)
 * @error: return location for a #GError
 *
 * Returns: (transfer full) (nullable): the environment, or %NULL
 */
LRG_AVAILABLE_IN_ALL
LrgEnvironment *
lrg_environment_new_from_file (const gchar  *path,
                               GError      **error);

/**
 * lrg_environment_load_from_data:
 * @self: an #LrgEnvironment
 * @data: YAML text of an environment block (or of a mapping with an
 *   `environment:` member)
 * @length: length of @data, or -1 when NUL-terminated
 * @error: return location for a #GError
 *
 * Replaces every setting of @self with the block's; keys that are not
 * given take their defaults and unknown keys are kept for
 * lrg_environment_to_data().  Sky images are not loaded: call
 * lrg_environment_load_sources().
 *
 * Returns: %TRUE on success, else %FALSE with %LRG_ENVIRONMENT_ERROR_PARSE
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_environment_load_from_data (LrgEnvironment  *self,
                                const gchar     *data,
                                gssize           length,
                                GError         **error);

/**
 * lrg_environment_to_data:
 * @self: an #LrgEnvironment
 *
 * Serializes the settings as the YAML mapping of an `environment:`
 * block (the mapping itself, without the key).
 *
 * Returns: (transfer full): the YAML text
 */
LRG_AVAILABLE_IN_ALL
gchar *
lrg_environment_to_data (LrgEnvironment *self);

/**
 * lrg_environment_save_to_file:
 * @self: an #LrgEnvironment
 * @path: destination YAML file
 * @error: return location for a #GError
 *
 * Writes lrg_environment_to_data() to @path.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_environment_save_to_file (LrgEnvironment  *self,
                              const gchar     *path,
                              GError         **error);

/* ==========================================================================
 * Sky source
 * ========================================================================== */

/**
 * lrg_environment_set_sky_source:
 * @self: an #LrgEnvironment
 * @source: where the light comes from
 */
LRG_AVAILABLE_IN_ALL
void
lrg_environment_set_sky_source (LrgEnvironment          *self,
                                LrgEnvironmentSkySource  source);

/**
 * lrg_environment_get_sky_source:
 * @self: an #LrgEnvironment
 *
 * Returns: the sky source
 */
LRG_AVAILABLE_IN_ALL
LrgEnvironmentSkySource
lrg_environment_get_sky_source (LrgEnvironment *self);

/**
 * lrg_environment_set_sky_color:
 * @self: an #LrgEnvironment
 * @rgb: (array fixed-size=3): the colour of %LRG_ENVIRONMENT_SKY_COLOR,
 *   sRGB-encoded like every other engine colour (values above 1 are
 *   allowed and brighter)
 */
LRG_AVAILABLE_IN_ALL
void
lrg_environment_set_sky_color (LrgEnvironment *self,
                               const gfloat   *rgb);

/**
 * lrg_environment_get_sky_color:
 * @self: an #LrgEnvironment
 * @out_rgb: (out caller-allocates) (array fixed-size=3): the colour
 */
LRG_AVAILABLE_IN_ALL
void
lrg_environment_get_sky_color (LrgEnvironment *self,
                               gfloat         *out_rgb);

/**
 * lrg_environment_set_gradient:
 * @self: an #LrgEnvironment
 * @zenith: (array fixed-size=3): colour straight up (sRGB)
 * @horizon: (array fixed-size=3): colour at the horizon (sRGB)
 * @ground: (array fixed-size=3): colour straight down (sRGB)
 *
 * Colours of %LRG_ENVIRONMENT_SKY_GRADIENT: horizon to zenith above the
 * horizon and horizon to ground below it, by sqrt (|y|) of the unit
 * direction so the horizon band stays narrow.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_environment_set_gradient (LrgEnvironment *self,
                              const gfloat   *zenith,
                              const gfloat   *horizon,
                              const gfloat   *ground);

/**
 * lrg_environment_get_gradient:
 * @self: an #LrgEnvironment
 * @out_zenith: (out caller-allocates) (optional) (array fixed-size=3): zenith
 * @out_horizon: (out caller-allocates) (optional) (array fixed-size=3): horizon
 * @out_ground: (out caller-allocates) (optional) (array fixed-size=3): ground
 */
LRG_AVAILABLE_IN_ALL
void
lrg_environment_get_gradient (LrgEnvironment *self,
                              gfloat         *out_zenith,
                              gfloat         *out_horizon,
                              gfloat         *out_ground);

/**
 * lrg_environment_set_sky_dome:
 * @self: an #LrgEnvironment
 * @dome: (nullable) (transfer none): the dome of %LRG_ENVIRONMENT_SKY_DOME
 *
 * Without a dome, the source behaves like %LRG_ENVIRONMENT_SKY_GRADIENT
 * with the environment's gradient colours.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_environment_set_sky_dome (LrgEnvironment *self,
                              LrgSkyDome     *dome);

/**
 * lrg_environment_get_sky_dome:
 * @self: an #LrgEnvironment
 *
 * Returns: (transfer none) (nullable): the sky dome
 */
LRG_AVAILABLE_IN_ALL
LrgSkyDome *
lrg_environment_get_sky_dome (LrgEnvironment *self);

/**
 * lrg_environment_set_equirect:
 * @self: an #LrgEnvironment
 * @image: (nullable) (transfer none): the linear panorama of
 *   %LRG_ENVIRONMENT_SKY_EQUIRECT (see lrg_hdr_image_direction_to_uv()
 *   for the mapping)
 */
LRG_AVAILABLE_IN_ALL
void
lrg_environment_set_equirect (LrgEnvironment *self,
                              LrgHdrImage    *image);

/**
 * lrg_environment_get_equirect:
 * @self: an #LrgEnvironment
 *
 * Returns: (transfer none) (nullable): the panorama
 */
LRG_AVAILABLE_IN_ALL
LrgHdrImage *
lrg_environment_get_equirect (LrgEnvironment *self);

/**
 * lrg_environment_set_cubemap_face:
 * @self: an #LrgEnvironment
 * @face: 0 .. 5 for +X, -X, +Y, -Y, +Z, -Z
 * @image: (nullable) (transfer none): the linear face image, square,
 *   in the OpenGL cubemap orientation (the one a camera at the centre
 *   sees looking down the axis with -Y up, or +Z up / -Z up for the
 *   +Y / -Y faces); all faces must have the same size
 */
LRG_AVAILABLE_IN_ALL
void
lrg_environment_set_cubemap_face (LrgEnvironment *self,
                                  guint           face,
                                  LrgHdrImage    *image);

/**
 * lrg_environment_get_cubemap_face:
 * @self: an #LrgEnvironment
 * @face: 0 .. 5
 *
 * Returns: (transfer none) (nullable): the face image
 */
LRG_AVAILABLE_IN_ALL
LrgHdrImage *
lrg_environment_get_cubemap_face (LrgEnvironment *self,
                                  guint           face);

/**
 * lrg_environment_set_cubemap_cross:
 * @self: an #LrgEnvironment
 * @cross: a horizontal (4 x 3 faces) or vertical (3 x 4 faces) cross
 * @error: return location for a #GError
 *
 * Cuts @cross into the six faces.  Horizontal crosses hold
 * -X, +Z, +X, -Z in their middle row with +Y above and -Y below the
 * +Z face; vertical crosses hold +Y, +Z, -Y, -Z down their middle column
 * with -X and +X beside +Z (-Z is rotated half a turn), the common
 * layouts of skybox tools.
 *
 * Returns: %TRUE on success, else %FALSE with
 *   %LRG_ENVIRONMENT_ERROR_SOURCE (not a cross shape)
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_environment_set_cubemap_cross (LrgEnvironment  *self,
                                   LrgHdrImage     *cross,
                                   GError         **error);

/**
 * lrg_environment_set_sky_path:
 * @self: an #LrgEnvironment
 * @path: (nullable): the authored reference of the equirect panorama or
 *   cubemap cross: a path (relative to the directory handed to
 *   lrg_environment_load_sources()) or `guid:<uuid>`
 */
LRG_AVAILABLE_IN_ALL
void
lrg_environment_set_sky_path (LrgEnvironment *self,
                              const gchar    *path);

/**
 * lrg_environment_get_sky_path:
 * @self: an #LrgEnvironment
 *
 * Returns: (nullable): the panorama or cross reference
 */
LRG_AVAILABLE_IN_ALL
const gchar *
lrg_environment_get_sky_path (LrgEnvironment *self);

/**
 * lrg_environment_set_sky_face_path:
 * @self: an #LrgEnvironment
 * @face: 0 .. 5 for +X, -X, +Y, -Y, +Z, -Z
 * @path: (nullable): path or `guid:` of that face image
 */
LRG_AVAILABLE_IN_ALL
void
lrg_environment_set_sky_face_path (LrgEnvironment *self,
                                   guint           face,
                                   const gchar    *path);

/**
 * lrg_environment_get_sky_face_path:
 * @self: an #LrgEnvironment
 * @face: 0 .. 5
 *
 * Returns: (nullable): the face reference
 */
LRG_AVAILABLE_IN_ALL
const gchar *
lrg_environment_get_sky_face_path (LrgEnvironment *self,
                                   guint           face);

/**
 * lrg_environment_load_sources:
 * @self: an #LrgEnvironment
 * @manager: (nullable): resolves `guid:` references
 *   (lrg_asset_manager_resolve_guid())
 * @base_dir: (nullable): directory relative paths are resolved against
 *   (usually the level file's directory); %NULL for the working directory
 * @error: return location for a #GError
 *
 * Loads the images the sky source references: for
 * %LRG_ENVIRONMENT_SKY_EQUIRECT the panorama of the sky path, for
 * %LRG_ENVIRONMENT_SKY_CUBEMAP the six face paths or the cross of the
 * sky path.  `.hdr` files are read with #LrgHdrImage (linear); other
 * images (PNG, JPG...) through raylib and decoded from sRGB to linear.
 * Other sources need nothing and succeed.
 *
 * Returns: %TRUE on success, else %FALSE with %LRG_ENVIRONMENT_ERROR_SOURCE
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_environment_load_sources (LrgEnvironment   *self,
                              LrgAssetManager  *manager,
                              const gchar      *base_dir,
                              GError          **error);

/* ==========================================================================
 * Intensity and orientation
 * ========================================================================== */

/**
 * lrg_environment_set_exposure:
 * @self: an #LrgEnvironment
 * @ev: exposure compensation in stops: the sky and its lighting are
 *   multiplied by 2^@ev (0 = as authored)
 */
LRG_AVAILABLE_IN_ALL
void
lrg_environment_set_exposure (LrgEnvironment *self,
                              gfloat          ev);

/**
 * lrg_environment_get_exposure:
 * @self: an #LrgEnvironment
 *
 * Returns: the exposure in stops
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_environment_get_exposure (LrgEnvironment *self);

/**
 * lrg_environment_set_rotation:
 * @self: an #LrgEnvironment
 * @degrees: turn of the sky about +Y (counter-clockwise seen from
 *   above); no re-bake is needed
 */
LRG_AVAILABLE_IN_ALL
void
lrg_environment_set_rotation (LrgEnvironment *self,
                              gfloat          degrees);

/**
 * lrg_environment_get_rotation:
 * @self: an #LrgEnvironment
 *
 * Returns: the rotation in degrees
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_environment_get_rotation (LrgEnvironment *self);

/**
 * lrg_environment_set_diffuse_intensity:
 * @self: an #LrgEnvironment
 * @intensity: multiplier of the diffuse (irradiance) light, >= 0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_environment_set_diffuse_intensity (LrgEnvironment *self,
                                       gfloat          intensity);

/**
 * lrg_environment_get_diffuse_intensity:
 * @self: an #LrgEnvironment
 *
 * Returns: the diffuse multiplier
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_environment_get_diffuse_intensity (LrgEnvironment *self);

/**
 * lrg_environment_set_specular_intensity:
 * @self: an #LrgEnvironment
 * @intensity: multiplier of the specular (reflection) light, >= 0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_environment_set_specular_intensity (LrgEnvironment *self,
                                        gfloat          intensity);

/**
 * lrg_environment_get_specular_intensity:
 * @self: an #LrgEnvironment
 *
 * Returns: the specular multiplier
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_environment_get_specular_intensity (LrgEnvironment *self);

/**
 * lrg_environment_set_ibl_enabled:
 * @self: an #LrgEnvironment
 * @enabled: %FALSE makes the lit shader fall back to the flat ambient
 *   colour even after a bake
 */
LRG_AVAILABLE_IN_ALL
void
lrg_environment_set_ibl_enabled (LrgEnvironment *self,
                                 gboolean        enabled);

/**
 * lrg_environment_get_ibl_enabled:
 * @self: an #LrgEnvironment
 *
 * Returns: whether image-based lighting is wanted
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_environment_get_ibl_enabled (LrgEnvironment *self);

/**
 * lrg_environment_set_resolution:
 * @self: an #LrgEnvironment
 * @size: cubemap edge in pixels, rounded down to a power of two in
 *   16 .. %LRG_ENVIRONMENT_MAX_RESOLUTION; the next bake uses it
 */
LRG_AVAILABLE_IN_ALL
void
lrg_environment_set_resolution (LrgEnvironment *self,
                                guint           size);

/**
 * lrg_environment_get_resolution:
 * @self: an #LrgEnvironment
 *
 * Returns: the cubemap edge in pixels
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_environment_get_resolution (LrgEnvironment *self);

/* ==========================================================================
 * Ambient, fog, volumetrics, post stack
 * ========================================================================== */

/**
 * lrg_environment_set_ambient:
 * @self: an #LrgEnvironment
 * @rgb: (array fixed-size=3): flat ambient colour (sRGB, 0 .. 1)
 * @intensity: its multiplier (>= 0)
 *
 * The light of the lit shader's `ambientColor`, used when image-based
 * lighting is off or not baked (lrg_environment_apply_to_light_set()).
 */
LRG_AVAILABLE_IN_ALL
void
lrg_environment_set_ambient (LrgEnvironment *self,
                             const gfloat   *rgb,
                             gfloat          intensity);

/**
 * lrg_environment_get_ambient:
 * @self: an #LrgEnvironment
 * @out_rgb: (out caller-allocates) (optional) (array fixed-size=3): colour
 * @out_intensity: (out) (optional): multiplier
 */
LRG_AVAILABLE_IN_ALL
void
lrg_environment_get_ambient (LrgEnvironment *self,
                             gfloat         *out_rgb,
                             gfloat         *out_intensity);

/**
 * lrg_environment_set_fog:
 * @self: an #LrgEnvironment
 * @enabled: whether the scene has distance fog
 * @type: the fog model
 * @rgba: (array fixed-size=4): fog colour (sRGB) and the cap of the fog
 *   amount in alpha (0 .. 1)
 * @density: exponential / uniform / height density
 * @start: linear fog start distance
 * @end: linear fog end distance
 *
 * Distance fog, handed to lrg_light_set_set_fog() (or
 * lrg_light_set_clear_fog() when disabled) by
 * lrg_environment_apply_to_light_set().
 */
LRG_AVAILABLE_IN_ALL
void
lrg_environment_set_fog (LrgEnvironment *self,
                         gboolean        enabled,
                         LrgFogType      type,
                         const gfloat   *rgba,
                         gfloat          density,
                         gfloat          start,
                         gfloat          end);

/**
 * lrg_environment_get_fog:
 * @self: an #LrgEnvironment
 * @out_type: (out) (optional): the fog model
 * @out_rgba: (out caller-allocates) (optional) (array fixed-size=4): colour
 * @out_density: (out) (optional): density
 * @out_start: (out) (optional): linear start
 * @out_end: (out) (optional): linear end
 *
 * The outputs are filled whether the fog is on or not.
 *
 * Returns: whether the fog is enabled
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_environment_get_fog (LrgEnvironment *self,
                         LrgFogType     *out_type,
                         gfloat         *out_rgba,
                         gfloat         *out_density,
                         gfloat         *out_start,
                         gfloat         *out_end);

/**
 * lrg_environment_set_fog_height:
 * @self: an #LrgEnvironment
 * @base_height: world height below which height fog is full density
 * @falloff: how fast it thins above, per world unit (> 0)
 *
 * Shapes %LRG_FOG_TYPE_HEIGHT fog and the volumetric fog's density
 * (defaults 0 and 0.1).
 */
LRG_AVAILABLE_IN_ALL
void
lrg_environment_set_fog_height (LrgEnvironment *self,
                                gfloat          base_height,
                                gfloat          falloff);

/**
 * lrg_environment_get_fog_height:
 * @self: an #LrgEnvironment
 * @out_base_height: (out) (optional): base height
 * @out_falloff: (out) (optional): falloff
 */
LRG_AVAILABLE_IN_ALL
void
lrg_environment_get_fog_height (LrgEnvironment *self,
                                gfloat         *out_base_height,
                                gfloat         *out_falloff);

/**
 * lrg_environment_set_volumetric:
 * @self: an #LrgEnvironment
 * @enabled: whether the scene wants the volumetric fog pass
 * @density: extinction per world unit at the base height (> 0)
 * @anisotropy: Henyey-Greenstein g, -0.95 .. 0.95
 * @steps: ray-march steps per pixel, 4 .. 128
 * @max_distance: how far the march reaches (> 0)
 * @intensity: multiplier of the in-scattered light (>= 0)
 *
 * Parameters of #LrgVolumetricFog (lrg_volumetric_fog_set_from_environment()).
 */
LRG_AVAILABLE_IN_ALL
void
lrg_environment_set_volumetric (LrgEnvironment *self,
                                gboolean        enabled,
                                gfloat          density,
                                gfloat          anisotropy,
                                guint           steps,
                                gfloat          max_distance,
                                gfloat          intensity);

/**
 * lrg_environment_get_volumetric:
 * @self: an #LrgEnvironment
 * @out_density: (out) (optional): density
 * @out_anisotropy: (out) (optional): anisotropy
 * @out_steps: (out) (optional): march steps
 * @out_max_distance: (out) (optional): march distance
 * @out_intensity: (out) (optional): scattering multiplier
 *
 * Returns: whether the volumetric fog is enabled
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_environment_get_volumetric (LrgEnvironment *self,
                                gfloat         *out_density,
                                gfloat         *out_anisotropy,
                                guint          *out_steps,
                                gfloat         *out_max_distance,
                                gfloat         *out_intensity);

/**
 * lrg_environment_set_post_stack:
 * @self: an #LrgEnvironment
 * @path: (nullable): path or `guid:` of the `.rpost` post-processing
 *   stack the scene uses (loaded by the caller through #LrgPostStack)
 */
LRG_AVAILABLE_IN_ALL
void
lrg_environment_set_post_stack (LrgEnvironment *self,
                                const gchar    *path);

/**
 * lrg_environment_get_post_stack:
 * @self: an #LrgEnvironment
 *
 * Returns: (nullable): the post stack reference
 */
LRG_AVAILABLE_IN_ALL
const gchar *
lrg_environment_get_post_stack (LrgEnvironment *self);

/**
 * lrg_environment_apply_to_light_set:
 * @self: an #LrgEnvironment
 * @light_set: the light set the scene draws with
 *
 * Copies the ambient colour (times its intensity and the exposure
 * factor) and the fog into @light_set.  The light set's GPU hookup of
 * the image-based lighting is separate (it binds the environment it was
 * given for that).
 */
LRG_AVAILABLE_IN_ALL
void
lrg_environment_apply_to_light_set (LrgEnvironment *self,
                                    LrgLightSet    *light_set);

/* ==========================================================================
 * Reflection probes
 * ========================================================================== */

/**
 * lrg_environment_add_probe:
 * @self: an #LrgEnvironment
 * @probe: (transfer none): a probe (a reference is kept); adding it twice
 *   does nothing
 */
LRG_AVAILABLE_IN_ALL
void
lrg_environment_add_probe (LrgEnvironment     *self,
                           LrgReflectionProbe *probe);

/**
 * lrg_environment_remove_probe:
 * @self: an #LrgEnvironment
 * @probe: a probe added before
 *
 * Returns: %TRUE when it was there
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_environment_remove_probe (LrgEnvironment     *self,
                              LrgReflectionProbe *probe);

/**
 * lrg_environment_clear_probes:
 * @self: an #LrgEnvironment
 */
LRG_AVAILABLE_IN_ALL
void
lrg_environment_clear_probes (LrgEnvironment *self);

/**
 * lrg_environment_get_n_probes:
 * @self: an #LrgEnvironment
 *
 * Returns: the number of probes
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_environment_get_n_probes (LrgEnvironment *self);

/**
 * lrg_environment_get_probe:
 * @self: an #LrgEnvironment
 * @index: 0 .. n - 1
 *
 * Returns: (transfer none) (nullable): the probe
 */
LRG_AVAILABLE_IN_ALL
LrgReflectionProbe *
lrg_environment_get_probe (LrgEnvironment *self,
                           guint           index);

/**
 * lrg_environment_set_reference_point:
 * @self: an #LrgEnvironment
 * @x: world X
 * @y: world Y
 * @z: world Z
 *
 * The point the binder selects probes for: the renderer sets the centre
 * of each object before it draws (or the camera position once per
 * frame, the cheaper and coarser choice).
 */
LRG_AVAILABLE_IN_ALL
void
lrg_environment_set_reference_point (LrgEnvironment *self,
                                     gfloat          x,
                                     gfloat          y,
                                     gfloat          z);

/**
 * lrg_environment_set_origin:
 * @self: an #LrgEnvironment
 * @x: origin X
 * @y: origin Y
 * @z: origin Z
 *
 * Floating origin: probe boxes are uploaded relative to it so they match
 * geometry drawn origin-relative (use the light set's
 * lrg_light_set_get_origin()).  Default 0.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_environment_set_origin (LrgEnvironment *self,
                            gfloat          x,
                            gfloat          y,
                            gfloat          z);

/**
 * lrg_environment_select_probes:
 * @self: an #LrgEnvironment
 * @point: (array fixed-size=3): a world point
 * @out_first: (out) (optional) (transfer none) (nullable): the strongest probe
 * @out_second: (out) (optional) (transfer none) (nullable): the runner-up
 *
 * The selection the binder makes: among the ready probes whose
 * influence box contains @point, the two with the largest influence
 * (lrg_reflection_probe_get_influence()), ties broken by the smaller
 * distance to the probe centre.
 *
 * Returns: how many were selected (0 .. 2)
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_environment_select_probes (LrgEnvironment      *self,
                               const gfloat        *point,
                               LrgReflectionProbe **out_first,
                               LrgReflectionProbe **out_second);

/* ==========================================================================
 * CPU evaluation
 * ========================================================================== */

/**
 * lrg_environment_sample_sky:
 * @self: an #LrgEnvironment
 * @direction: (array fixed-size=3): a world direction
 * @out_rgb: (out caller-allocates) (array fixed-size=3): linear radiance
 *
 * The sky's radiance seen along @direction with the exposure and
 * rotation applied, computed on the CPU (a sky dome contributes its
 * gradient, without the sun and band).
 */
LRG_AVAILABLE_IN_ALL
void
lrg_environment_sample_sky (LrgEnvironment *self,
                            const gfloat   *direction,
                            gfloat         *out_rgb);

/**
 * lrg_environment_compute_sh:
 * @self: an #LrgEnvironment
 * @face_size: cube face resolution to integrate over (4 .. 256)
 * @out_sh: (out caller-allocates) (array fixed-size=27): the irradiance
 *   coefficients (see lrg_environment_sh_project_cubemap())
 *
 * Projects lrg_environment_sample_sky() without the exposure and
 * rotation (the shader applies those) onto the L2 basis on the CPU and
 * stores the result as the environment's irradiance, like a bake does
 * for every source except the sky dome.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_environment_compute_sh (LrgEnvironment *self,
                            guint           face_size,
                            gfloat         *out_sh);

/**
 * lrg_environment_get_sh:
 * @self: an #LrgEnvironment
 * @out_sh: (out caller-allocates) (array fixed-size=27): the irradiance
 *   coefficients of the last bake or lrg_environment_compute_sh() (all
 *   zero before either)
 */
LRG_AVAILABLE_IN_ALL
void
lrg_environment_get_sh (LrgEnvironment *self,
                        gfloat         *out_sh);

/**
 * lrg_environment_sample_diffuse:
 * @self: an #LrgEnvironment
 * @normal: (array fixed-size=3): a world normal
 * @out_rgb: (out caller-allocates) (array fixed-size=3): the value of the
 *   shader's lrg_env_diffuse() for @normal
 *
 * The diffuse light the lit shader gets: E (n) / pi from the stored
 * coefficients, with the rotation, exposure and diffuse intensity.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_environment_sample_diffuse (LrgEnvironment *self,
                                const gfloat   *normal,
                                gfloat         *out_rgb);

/**
 * lrg_environment_sh_project_cubemap:
 * @faces: (array) (element-type gfloat): six faces of @size x @size
 *   linear RGB floats, face-major in the order +X, -X, +Y, -Y, +Z, -Z,
 *   rows in increasing t (see lrg_environment_cube_face_direction())
 * @size: face edge in pixels
 * @out_sh: (out caller-allocates) (array fixed-size=27): coefficients
 *
 * Projects the radiance onto the 9 real L2 spherical harmonics (each
 * texel weighted by its solid angle, the total normalized to 4 pi) and
 * convolves with the clamped cosine (Ramamoorthi and Hanrahan, "An
 * Efficient Representation for Irradiance Environment Maps"), divided
 * by pi.  Evaluating the basis with these coefficients
 * (lrg_environment_sh_irradiance()) gives E (n) / pi, which is the sky
 * colour itself for a constant sky.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_environment_sh_project_cubemap (const gfloat *faces,
                                    guint         size,
                                    gfloat       *out_sh);

/**
 * lrg_environment_sh_irradiance:
 * @sh: (array fixed-size=27): coefficients from
 *   lrg_environment_sh_project_cubemap()
 * @normal: (array fixed-size=3): a unit normal
 * @out_rgb: (out caller-allocates) (array fixed-size=3): E (n) / pi
 *
 * Evaluates the basis exactly as the shader does (negative results
 * clamped to 0).
 */
LRG_AVAILABLE_IN_ALL
void
lrg_environment_sh_irradiance (const gfloat *sh,
                               const gfloat *normal,
                               gfloat       *out_rgb);

/**
 * lrg_environment_cube_face_direction:
 * @face: 0 .. 5 for +X, -X, +Y, -Y, +Z, -Z
 * @s: horizontal texture coordinate on the face, 0 .. 1
 * @t: vertical texture coordinate (first row = 0), 0 .. 1
 * @out_direction: (out caller-allocates) (array fixed-size=3): the unit
 *   direction the OpenGL cubemap maps to (@face, @s, @t)
 */
LRG_AVAILABLE_IN_ALL
void
lrg_environment_cube_face_direction (guint   face,
                                     gfloat  s,
                                     gfloat  t,
                                     gfloat *out_direction);

/**
 * lrg_environment_integrate_brdf:
 * @n_dot_v: cosine between the normal and the view direction, 0 .. 1
 * @roughness: perceptual roughness, 0 .. 1 (alpha = roughness^2)
 * @samples: importance samples (>= 1; 256 is plenty)
 * @out_scale: (out): the factor of F0
 * @out_bias: (out): the term added to it
 *
 * The split-sum environment BRDF (Karis, "Real Shading in Unreal
 * Engine 4"): the GGX / Smith (k = alpha^2 / 2) specular response to a
 * white environment is F0 x @out_scale + @out_bias.  Deterministic
 * (Hammersley points).
 */
LRG_AVAILABLE_IN_ALL
void
lrg_environment_integrate_brdf (gfloat  n_dot_v,
                                gfloat  roughness,
                                guint   samples,
                                gfloat *out_scale,
                                gfloat *out_bias);

/**
 * lrg_environment_compute_brdf_lut:
 * @size: table edge (>= 2)
 * @samples: importance samples per entry
 * @n_floats: (out) (optional): number of floats returned (size x size x 2)
 *
 * The lookup table a bake uploads: entry (x, y) holds
 * lrg_environment_integrate_brdf() at n_dot_v = (x + 0.5) / @size and
 * roughness = (y + 0.5) / @size, as (scale, bias) pairs, rows of
 * increasing roughness.
 *
 * Returns: (transfer full) (array length=n_floats): the table
 */
LRG_AVAILABLE_IN_ALL
gfloat *
lrg_environment_compute_brdf_lut (guint  size,
                                  guint  samples,
                                  gsize *n_floats);

/* ==========================================================================
 * GPU
 * ========================================================================== */

/**
 * lrg_environment_bake:
 * @self: an #LrgEnvironment
 * @error: return location for a #GError
 *
 * Builds the GPU maps (needs a window, outside BeginDrawing() /
 * BeginTextureMode()): the radiance cubemap from the sky source (the
 * CPU evaluates colour, gradient, cubemap and equirect skies; a sky
 * dome is drawn into the six faces), the irradiance harmonics (on the
 * CPU, or read back from the GPU for a dome), the prefiltered specular
 * mip chain (GGX importance sampling rendered into each face of each
 * mip) and the BRDF table.  Missing source images fall back to the
 * gradient with a warning.  Baking again replaces the maps.
 *
 * Returns: %TRUE on success, else %FALSE with %LRG_ENVIRONMENT_ERROR_GPU
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_environment_bake (LrgEnvironment  *self,
                      GError         **error);

/**
 * lrg_environment_is_baked:
 * @self: an #LrgEnvironment
 *
 * Returns: whether GPU maps exist
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_environment_is_baked (LrgEnvironment *self);

/**
 * lrg_environment_needs_bake:
 * @self: an #LrgEnvironment
 *
 * Returns: %TRUE when a setting that changes the maps (sky source,
 *   colours, images, dome, resolution) changed since the last bake, or
 *   nothing was baked yet
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_environment_needs_bake (LrgEnvironment *self);

/**
 * lrg_environment_release_gpu:
 * @self: an #LrgEnvironment
 *
 * Frees the GPU maps (call before CloseWindow()); settings stay.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_environment_release_gpu (LrgEnvironment *self);

/**
 * lrg_environment_get_radiance_texture_id:
 * @self: an #LrgEnvironment
 *
 * Returns: the GL id of the radiance cubemap (RGBA16F), 0 before a bake
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_environment_get_radiance_texture_id (LrgEnvironment *self);

/**
 * lrg_environment_get_specular_texture_id:
 * @self: an #LrgEnvironment
 *
 * Returns: the GL id of the prefiltered specular cubemap (RGBA16F, mip
 *   m holds roughness m / (mips - 1)), 0 before a bake
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_environment_get_specular_texture_id (LrgEnvironment *self);

/**
 * lrg_environment_get_specular_mip_count:
 * @self: an #LrgEnvironment
 *
 * Returns: mip levels of the specular cubemap (down to 1 x 1), 0
 *   before a bake
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_environment_get_specular_mip_count (LrgEnvironment *self);

/**
 * lrg_environment_get_brdf_texture_id:
 * @self: an #LrgEnvironment
 *
 * Returns: the GL id of the BRDF lookup table (RGB32F, R = scale,
 *   G = bias), 0 before a bake
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_environment_get_brdf_texture_id (LrgEnvironment *self);

/**
 * lrg_environment_read_specular_face:
 * @self: a baked #LrgEnvironment
 * @mip: mip level of the specular cubemap
 * @face: 0 .. 5
 * @out_size: (out) (optional): the face edge at @mip
 *
 * Reads one face of one mip back from the GPU (tests, previews,
 * debugging; slow).
 *
 * Returns: (transfer full) (nullable) (array) (element-type gfloat): size x
 *   size linear RGB floats, rows of increasing t; %NULL when not baked or
 *   out of range
 */
LRG_AVAILABLE_IN_ALL
gfloat *
lrg_environment_read_specular_face (LrgEnvironment *self,
                                    guint           mip,
                                    guint           face,
                                    guint          *out_size);

/**
 * lrg_environment_draw_sky:
 * @self: an #LrgEnvironment
 * @camera_position: the eye, in draw space
 *
 * Draws the sky behind everything, inside BeginMode3D(), before the
 * world (depth test and writes off): a sky dome source draws its dome,
 * other sources draw the baked radiance cubemap (rotated and exposed,
 * encoded to sRGB like the lit shader's output).  Before a bake the
 * CPU-describable sources draw a flat clear in their mean colour.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_environment_draw_sky (LrgEnvironment   *self,
                          const GrlVector3 *camera_position);

/**
 * lrg_environment_fill_probe:
 * @self: an #LrgEnvironment
 * @probe: a reflection probe
 * @error: return location for a #GError
 *
 * Captures the environment's sky alone into @probe (no scene), a cheap
 * placeholder before a real capture and what an empty level reflects.
 * Needs GL like lrg_reflection_probe_capture().
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_environment_fill_probe (LrgEnvironment      *self,
                            LrgReflectionProbe  *probe,
                            GError             **error);

G_END_DECLS
