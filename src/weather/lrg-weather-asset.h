/* lrg-weather-asset.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgWeatherAsset: the structured `.rweather` format. An asset holds
 * presets (one #LrgWeather each), every preset a stack of layers (rain,
 * snow, fog, lightning, wind, GPU particles or a registered effect type)
 * with an intensity and parameters, plus the crossfade durations between
 * presets. It loads and saves YAML (byte-stable round trip, unknown keys
 * kept), builds and live-syncs #LrgWeather objects on an
 * #LrgWeatherManager, and exposes a typed parameter schema for the
 * editor's weather inspector.
 *
 * Two effect types live here because only the format needs them:
 * LrgWeatherWind (a wind layer with gusts that fades with its weather)
 * and LrgWeatherParticles (a camera-following #LrgGpuParticles emitter).
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <graylib.h>
#include "../lrg-version.h"
#include "lrg-weather.h"
#include "lrg-weather-effect.h"
#include "lrg-weather-manager.h"
#include "../particles/lrg-gpu-particles.h"

G_BEGIN_DECLS

/* ==========================================================================
 * LrgWeatherWind
 * ========================================================================== */

#define LRG_TYPE_WEATHER_WIND (lrg_weather_wind_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgWeatherWind, lrg_weather_wind, LRG, WEATHER_WIND, LrgWeatherEffect)

/**
 * lrg_weather_wind_new:
 *
 * Creates a wind layer: a steady wind (#LrgWeatherWind:speed-x,
 * #LrgWeatherWind:speed-y, in the units of the weather's space) plus
 * gusts along it, scaled by the layer's intensity. An #LrgWeather adds
 * the current wind of its wind layers to its own wind and hands the sum
 * to its other effects every update, so a storm's wind rises and falls
 * with the storm.
 *
 * Returns: (transfer full): a new #LrgWeatherWind
 */
LRG_AVAILABLE_IN_ALL
LrgWeatherWind * lrg_weather_wind_new (void);

/**
 * lrg_weather_wind_get_current:
 * @self: an #LrgWeatherWind
 * @wind_x: (out) (optional): current wind X (gusts and intensity applied)
 * @wind_y: (out) (optional): current wind Y
 *
 * Gets the wind this layer contributes now (zero while inactive).
 */
LRG_AVAILABLE_IN_ALL
void lrg_weather_wind_get_current (LrgWeatherWind *self,
                                   gfloat         *wind_x,
                                   gfloat         *wind_y);

/* ==========================================================================
 * LrgWeatherParticles
 * ========================================================================== */

#define LRG_TYPE_WEATHER_PARTICLES (lrg_weather_particles_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgWeatherParticles, lrg_weather_particles, LRG, WEATHER_PARTICLES, LrgWeatherEffect)

/**
 * lrg_weather_particles_new:
 *
 * Creates a particle layer: an #LrgGpuParticles emitter (GPU when
 * available, the same simulation on the CPU otherwise) that follows the
 * camera at #LrgWeatherParticles:offset-x/y/z, emits its
 * #LrgGpuParticles:emission-rate times the layer intensity, takes the
 * weather wind as its air velocity (X/Y of the wind on world X/Z) and
 * draws in the weather's world pass (lrg_weather_effect_render_world()).
 * World space only: dust, embers, ash, pollen, dense GPU rain or snow.
 *
 * Returns: (transfer full): a new #LrgWeatherParticles
 */
LRG_AVAILABLE_IN_ALL
LrgWeatherParticles * lrg_weather_particles_new (void);

/**
 * lrg_weather_particles_get_emitter:
 * @self: an #LrgWeatherParticles
 *
 * The emitter; set its description parameters directly (or through the
 * layer's `params` in a `.rweather`). Replaced when
 * #LrgWeatherParticles:capacity changes (parameters are carried over).
 *
 * Returns: (transfer none): the emitter
 */
LRG_AVAILABLE_IN_ALL
LrgGpuParticles * lrg_weather_particles_get_emitter (LrgWeatherParticles *self);

/**
 * lrg_weather_particles_set_asset:
 * @self: an #LrgWeatherParticles
 * @asset: (nullable): an `.rparticles` asset, or %NULL to go back to the
 *   layer's own emitter description
 * @error: return location for the reason the GPU path was not taken, or
 *   %NULL
 *
 * Plays an `.rparticles` asset as the layer. A one-emitter asset inside
 * the GPU subset (lrg_gpu_particles_check_emitter()) is copied into the
 * layer's #LrgGpuParticles and keeps the intensity-scaled emission; any
 * other asset plays on the CPU as an #LrgParticleEffect at the emitter
 * position (its own rates, drawn while the intensity is above 0.01).
 * @error is set when the asset fell back to the CPU; the call itself
 * never fails.
 *
 * Returns: %TRUE when the asset runs on the GPU emitter
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_weather_particles_set_asset (LrgWeatherParticles  *self,
                                          LrgParticleAsset     *asset,
                                          GError              **error);

/**
 * lrg_weather_particles_get_effect:
 * @self: an #LrgWeatherParticles
 *
 * Returns: (transfer none) (nullable): the CPU effect playing an asset
 *   that fell back to the CPU, or %NULL
 */
LRG_AVAILABLE_IN_ALL
LrgParticleEffect * lrg_weather_particles_get_effect (LrgWeatherParticles *self);

/* ==========================================================================
 * LrgWeatherAsset
 * ========================================================================== */

/**
 * LRG_WEATHER_ASSET_FORMAT_VERSION:
 *
 * The `.rweather` format version this build writes (and the newest it
 * reads).
 */
#define LRG_WEATHER_ASSET_FORMAT_VERSION (1)

/**
 * LRG_WEATHER_ASSET_ERROR:
 *
 * Error domain of #LrgWeatherAsset.
 */
#define LRG_WEATHER_ASSET_ERROR (lrg_weather_asset_error_quark ())

/**
 * lrg_weather_asset_error_quark:
 *
 * Returns: the #GQuark of %LRG_WEATHER_ASSET_ERROR
 */
LRG_AVAILABLE_IN_ALL
GQuark lrg_weather_asset_error_quark (void);

/**
 * LrgWeatherAssetError:
 * @LRG_WEATHER_ASSET_ERROR_PARSE: not YAML, or the wrong structure (the
 *   message names the key path)
 * @LRG_WEATHER_ASSET_ERROR_INVALID_VALUE: a value of the wrong type or
 *   out of range
 * @LRG_WEATHER_ASSET_ERROR_UNKNOWN_KIND: a layer kind nobody registered
 * @LRG_WEATHER_ASSET_ERROR_UNKNOWN_PARAM: a parameter the layer kind has not
 * @LRG_WEATHER_ASSET_ERROR_DUPLICATE: two presets with one id, or two
 *   layers of a preset with one id
 * @LRG_WEATHER_ASSET_ERROR_NOT_FOUND: a preset id or index that does not
 *   exist
 * @LRG_WEATHER_ASSET_ERROR_IO: reading or writing the file failed
 *
 * Error codes of %LRG_WEATHER_ASSET_ERROR.
 */
typedef enum
{
	LRG_WEATHER_ASSET_ERROR_PARSE,
	LRG_WEATHER_ASSET_ERROR_INVALID_VALUE,
	LRG_WEATHER_ASSET_ERROR_UNKNOWN_KIND,
	LRG_WEATHER_ASSET_ERROR_UNKNOWN_PARAM,
	LRG_WEATHER_ASSET_ERROR_DUPLICATE,
	LRG_WEATHER_ASSET_ERROR_NOT_FOUND,
	LRG_WEATHER_ASSET_ERROR_IO
} LrgWeatherAssetError;

/**
 * lrg_weather_asset_error_get_type:
 *
 * Returns: the #GType of #LrgWeatherAssetError
 */
LRG_AVAILABLE_IN_ALL
GType lrg_weather_asset_error_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_WEATHER_ASSET_ERROR (lrg_weather_asset_error_get_type ())

#define LRG_TYPE_WEATHER_ASSET (lrg_weather_asset_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgWeatherAsset, lrg_weather_asset, LRG, WEATHER_ASSET, GObject)

/* --------------------------------------------------------------------------
 * Layer kinds
 * -------------------------------------------------------------------------- */

/**
 * lrg_weather_asset_register_kind:
 * @kind: the `kind:` name used in files (lowercase, dashes)
 * @effect_type: an instantiatable subtype of #LrgWeatherEffect
 *
 * Registers a layer kind for a game's own effect type. Its parameters
 * are the writable properties of @effect_type (minus the ones the format
 * manages: id, active, intensity, target-intensity, level, wind-x/y).
 * The built-in kinds are rain, snow, fog, lightning, wind and particles.
 * Registering an existing name replaces it.
 */
LRG_AVAILABLE_IN_ALL
void lrg_weather_asset_register_kind (const gchar *kind,
                                      GType        effect_type);

/**
 * lrg_weather_asset_lookup_kind:
 * @kind: a layer kind
 *
 * Returns: the effect type of @kind, or %G_TYPE_INVALID
 */
LRG_AVAILABLE_IN_ALL
GType lrg_weather_asset_lookup_kind (const gchar *kind);

/**
 * lrg_weather_asset_list_kinds:
 *
 * Returns: (transfer full) (array zero-terminated=1): the registered
 *   kinds, sorted
 */
LRG_AVAILABLE_IN_ALL
gchar ** lrg_weather_asset_list_kinds (void);

/**
 * lrg_weather_asset_list_kind_params:
 * @kind: a layer kind
 * @n_params: (out) (optional): the number of parameters
 *
 * The parameter schema of a kind, for an inspector: one #GParamSpec per
 * parameter (its name, value type, range, default and blurb), the
 * effect's properties first, then the format-only parameters (colours,
 * areas, volumes, bolt shape, seeds and, for `particles`, every
 * #LrgGpuParticles description parameter).
 *
 * Returns: (transfer container) (array length=n_params) (nullable): the
 *   specs, or %NULL for an unknown kind
 */
LRG_AVAILABLE_IN_ALL
GParamSpec ** lrg_weather_asset_list_kind_params (const gchar *kind,
                                                  guint       *n_params);

/* --------------------------------------------------------------------------
 * Construction, loading and saving
 * -------------------------------------------------------------------------- */

/**
 * lrg_weather_asset_new:
 *
 * Creates an empty asset (no presets).
 *
 * Returns: (transfer full): a new #LrgWeatherAsset
 */
LRG_AVAILABLE_IN_ALL
LrgWeatherAsset * lrg_weather_asset_new (void);

/**
 * lrg_weather_asset_new_from_data:
 * @data: `.rweather` YAML text
 * @length: length of @data in bytes, or -1 when nul-terminated
 * @base_dir: (nullable): directory relative paths resolve against
 * @error: return location for a #GError, or %NULL
 *
 * Parses an asset. Unknown keys at any mapping level except `params` are
 * kept and written back; an unknown kind, an unknown parameter or a bad
 * value is an error whose message starts with the key path (for example
 * `presets[1].layers[0].params.drop-count`).
 *
 * Returns: (transfer full) (nullable): the asset, or %NULL with @error
 */
LRG_AVAILABLE_IN_ALL
LrgWeatherAsset * lrg_weather_asset_new_from_data (const gchar  *data,
                                                   gssize        length,
                                                   const gchar  *base_dir,
                                                   GError      **error);

/**
 * lrg_weather_asset_new_from_file:
 * @path: a `.rweather` file
 * @error: return location for a #GError, or %NULL
 *
 * Loads an asset; relative paths in it resolve against the file's
 * directory.
 *
 * Returns: (transfer full) (nullable): the asset, or %NULL with @error
 */
LRG_AVAILABLE_IN_ALL
LrgWeatherAsset * lrg_weather_asset_new_from_file (const gchar  *path,
                                                   GError      **error);

/**
 * lrg_weather_asset_to_data:
 * @self: an #LrgWeatherAsset
 * @error: return location for a #GError, or %NULL
 *
 * Writes the asset as YAML. Saving a loaded asset and loading the result
 * gives the same text again (byte-stable round trip).
 *
 * Returns: (transfer full) (nullable): the YAML text
 */
LRG_AVAILABLE_IN_ALL
gchar * lrg_weather_asset_to_data (LrgWeatherAsset  *self,
                                   GError          **error);

/**
 * lrg_weather_asset_save_to_file:
 * @self: an #LrgWeatherAsset
 * @path: destination file
 * @error: return location for a #GError, or %NULL
 *
 * Writes lrg_weather_asset_to_data() to @path.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_weather_asset_save_to_file (LrgWeatherAsset  *self,
                                         const gchar      *path,
                                         GError          **error);

/**
 * lrg_weather_asset_copy:
 * @self: an #LrgWeatherAsset
 *
 * Returns: (transfer full): a deep copy (for undo snapshots)
 */
LRG_AVAILABLE_IN_ALL
LrgWeatherAsset * lrg_weather_asset_copy (LrgWeatherAsset *self);

/**
 * lrg_weather_asset_get_base_dir:
 * @self: an #LrgWeatherAsset
 *
 * Returns: (nullable): the directory relative paths resolve against
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_weather_asset_get_base_dir (LrgWeatherAsset *self);

/* --------------------------------------------------------------------------
 * Asset settings
 * -------------------------------------------------------------------------- */

/**
 * lrg_weather_asset_get_name:
 * @self: an #LrgWeatherAsset
 *
 * Returns: (nullable): the display name
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_weather_asset_get_name (LrgWeatherAsset *self);

/**
 * lrg_weather_asset_set_name:
 * @self: an #LrgWeatherAsset
 * @name: (nullable): the display name
 *
 * Sets the display name.
 */
LRG_AVAILABLE_IN_ALL
void lrg_weather_asset_set_name (LrgWeatherAsset *self,
                                 const gchar     *name);

/**
 * lrg_weather_asset_get_default_preset:
 * @self: an #LrgWeatherAsset
 *
 * Returns: (nullable): the preset lrg_weather_asset_apply() starts with
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_weather_asset_get_default_preset (LrgWeatherAsset *self);

/**
 * lrg_weather_asset_set_default_preset:
 * @self: an #LrgWeatherAsset
 * @preset_id: (nullable): a preset id, or %NULL for none (clear sky)
 *
 * Sets the starting preset.
 */
LRG_AVAILABLE_IN_ALL
void lrg_weather_asset_set_default_preset (LrgWeatherAsset *self,
                                           const gchar     *preset_id);

/**
 * lrg_weather_asset_get_default_transition:
 * @self: an #LrgWeatherAsset
 *
 * Returns: the crossfade in seconds when no transition rule matches
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_weather_asset_get_default_transition (LrgWeatherAsset *self);

/**
 * lrg_weather_asset_set_default_transition:
 * @self: an #LrgWeatherAsset
 * @seconds: crossfade in seconds (0 switches instantly)
 *
 * Sets the fallback crossfade.
 */
LRG_AVAILABLE_IN_ALL
void lrg_weather_asset_set_default_transition (LrgWeatherAsset *self,
                                               gfloat           seconds);

/* --------------------------------------------------------------------------
 * Presets
 * -------------------------------------------------------------------------- */

/**
 * lrg_weather_asset_get_preset_count:
 * @self: an #LrgWeatherAsset
 *
 * Returns: the number of presets
 */
LRG_AVAILABLE_IN_ALL
guint lrg_weather_asset_get_preset_count (LrgWeatherAsset *self);

/**
 * lrg_weather_asset_find_preset:
 * @self: an #LrgWeatherAsset
 * @preset_id: a preset id
 *
 * Returns: the preset's index, or -1
 */
LRG_AVAILABLE_IN_ALL
gint lrg_weather_asset_find_preset (LrgWeatherAsset *self,
                                    const gchar     *preset_id);

/**
 * lrg_weather_asset_add_preset:
 * @self: an #LrgWeatherAsset
 * @preset_id: a new, unique id
 * @name: (nullable): display name
 * @error: return location for a #GError, or %NULL
 *
 * Appends an empty preset.
 *
 * Returns: its index, or -1 with %LRG_WEATHER_ASSET_ERROR_DUPLICATE
 */
LRG_AVAILABLE_IN_ALL
gint lrg_weather_asset_add_preset (LrgWeatherAsset  *self,
                                   const gchar      *preset_id,
                                   const gchar      *name,
                                   GError          **error);

/**
 * lrg_weather_asset_remove_preset:
 * @self: an #LrgWeatherAsset
 * @preset: a preset index
 *
 * Removes a preset.
 *
 * Returns: %FALSE when @preset is out of range
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_weather_asset_remove_preset (LrgWeatherAsset *self,
                                          guint            preset);

/**
 * lrg_weather_asset_get_preset_id:
 * @self: an #LrgWeatherAsset
 * @preset: a preset index
 *
 * Returns: (nullable): the preset id
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_weather_asset_get_preset_id (LrgWeatherAsset *self,
                                               guint            preset);

/**
 * lrg_weather_asset_get_preset_name:
 * @self: an #LrgWeatherAsset
 * @preset: a preset index
 *
 * Returns: (nullable): the display name
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_weather_asset_get_preset_name (LrgWeatherAsset *self,
                                                 guint            preset);

/**
 * lrg_weather_asset_set_preset_name:
 * @self: an #LrgWeatherAsset
 * @preset: a preset index
 * @name: (nullable): display name
 *
 * Sets a preset's display name.
 */
LRG_AVAILABLE_IN_ALL
void lrg_weather_asset_set_preset_name (LrgWeatherAsset *self,
                                        guint            preset,
                                        const gchar     *name);

/**
 * lrg_weather_asset_get_preset_ambient:
 * @self: an #LrgWeatherAsset
 * @preset: a preset index
 * @color: (out caller-allocates) (optional): ambient tint (alpha 255)
 * @brightness: (out) (optional): ambient brightness
 *
 * Gets the preset's ambient light (lrg_weather_set_ambient_color() and
 * lrg_weather_set_ambient_brightness() of the built weather).
 */
LRG_AVAILABLE_IN_ALL
void lrg_weather_asset_get_preset_ambient (LrgWeatherAsset *self,
                                           guint            preset,
                                           GrlColor        *color,
                                           gfloat          *brightness);

/**
 * lrg_weather_asset_set_preset_ambient:
 * @self: an #LrgWeatherAsset
 * @preset: a preset index
 * @color: ambient tint (alpha ignored)
 * @brightness: ambient brightness (0 .. 2)
 *
 * Sets the preset's ambient light.
 */
LRG_AVAILABLE_IN_ALL
void lrg_weather_asset_set_preset_ambient (LrgWeatherAsset *self,
                                           guint            preset,
                                           const GrlColor  *color,
                                           gfloat           brightness);

/* --------------------------------------------------------------------------
 * Layers
 * -------------------------------------------------------------------------- */

/**
 * lrg_weather_asset_get_layer_count:
 * @self: an #LrgWeatherAsset
 * @preset: a preset index
 *
 * Returns: the number of layers of the preset (0 when out of range)
 */
LRG_AVAILABLE_IN_ALL
guint lrg_weather_asset_get_layer_count (LrgWeatherAsset *self,
                                         guint            preset);

/**
 * lrg_weather_asset_add_layer:
 * @self: an #LrgWeatherAsset
 * @preset: a preset index
 * @kind: a registered layer kind
 * @layer_id: (nullable): a unique id in the preset, or %NULL for the kind
 *   (numbered when taken)
 * @error: return location for a #GError, or %NULL
 *
 * Appends a layer with intensity 1, enabled, no parameters.
 *
 * Returns: the layer index, or -1 with @error
 */
LRG_AVAILABLE_IN_ALL
gint lrg_weather_asset_add_layer (LrgWeatherAsset  *self,
                                  guint             preset,
                                  const gchar      *kind,
                                  const gchar      *layer_id,
                                  GError          **error);

/**
 * lrg_weather_asset_remove_layer:
 * @self: an #LrgWeatherAsset
 * @preset: a preset index
 * @layer: a layer index
 *
 * Removes a layer.
 *
 * Returns: %FALSE when out of range
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_weather_asset_remove_layer (LrgWeatherAsset *self,
                                         guint            preset,
                                         guint            layer);

/**
 * lrg_weather_asset_move_layer:
 * @self: an #LrgWeatherAsset
 * @preset: a preset index
 * @from: a layer index
 * @to: its new index
 *
 * Reorders layers (the effects of a weather keep this order within a
 * render layer).
 *
 * Returns: %FALSE when out of range
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_weather_asset_move_layer (LrgWeatherAsset *self,
                                       guint            preset,
                                       guint            from,
                                       guint            to);

/**
 * lrg_weather_asset_get_layer_kind:
 * @self: an #LrgWeatherAsset
 * @preset: a preset index
 * @layer: a layer index
 *
 * Returns: (nullable): the layer kind
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_weather_asset_get_layer_kind (LrgWeatherAsset *self,
                                                guint            preset,
                                                guint            layer);

/**
 * lrg_weather_asset_get_layer_id:
 * @self: an #LrgWeatherAsset
 * @preset: a preset index
 * @layer: a layer index
 *
 * Returns: (nullable): the layer id (the built effect's id)
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_weather_asset_get_layer_id (LrgWeatherAsset *self,
                                              guint            preset,
                                              guint            layer);

/**
 * lrg_weather_asset_find_layer:
 * @self: an #LrgWeatherAsset
 * @preset: a preset index
 * @layer_id: a layer id
 *
 * Returns: the layer index, or -1
 */
LRG_AVAILABLE_IN_ALL
gint lrg_weather_asset_find_layer (LrgWeatherAsset *self,
                                   guint            preset,
                                   const gchar     *layer_id);

/**
 * lrg_weather_asset_get_layer_intensity:
 * @self: an #LrgWeatherAsset
 * @preset: a preset index
 * @layer: a layer index
 *
 * Returns: the layer's full-strength intensity (its effect's
 *   #LrgWeatherEffect:level)
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_weather_asset_get_layer_intensity (LrgWeatherAsset *self,
                                              guint            preset,
                                              guint            layer);

/**
 * lrg_weather_asset_set_layer_intensity:
 * @self: an #LrgWeatherAsset
 * @preset: a preset index
 * @layer: a layer index
 * @intensity: 0 .. 1
 *
 * Sets the layer's full-strength intensity.
 */
LRG_AVAILABLE_IN_ALL
void lrg_weather_asset_set_layer_intensity (LrgWeatherAsset *self,
                                            guint            preset,
                                            guint            layer,
                                            gfloat           intensity);

/**
 * lrg_weather_asset_get_layer_enabled:
 * @self: an #LrgWeatherAsset
 * @preset: a preset index
 * @layer: a layer index
 *
 * Returns: whether the layer is built (disabled layers stay in the file)
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_weather_asset_get_layer_enabled (LrgWeatherAsset *self,
                                              guint            preset,
                                              guint            layer);

/**
 * lrg_weather_asset_set_layer_enabled:
 * @self: an #LrgWeatherAsset
 * @preset: a preset index
 * @layer: a layer index
 * @enabled: whether to build the layer
 *
 * Enables or disables a layer.
 */
LRG_AVAILABLE_IN_ALL
void lrg_weather_asset_set_layer_enabled (LrgWeatherAsset *self,
                                          guint            preset,
                                          guint            layer,
                                          gboolean         enabled);

/**
 * lrg_weather_asset_set_layer_param:
 * @self: an #LrgWeatherAsset
 * @preset: a preset index
 * @layer: a layer index
 * @name: a parameter of the layer's kind
 * @value: the value: of the parameter's type, transformable to it, or a
 *   string in the file syntax (numbers, `true`, enum nicks,
 *   `"#rrggbbaa"` colours, gradient strings)
 * @error: return location for a #GError, or %NULL
 *
 * Sets (or replaces) a parameter. Numbers outside the range clamp.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_weather_asset_set_layer_param (LrgWeatherAsset  *self,
                                            guint             preset,
                                            guint             layer,
                                            const gchar      *name,
                                            const GValue     *value,
                                            GError          **error);

/**
 * lrg_weather_asset_get_layer_param:
 * @self: an #LrgWeatherAsset
 * @preset: a preset index
 * @layer: a layer index
 * @name: a parameter name
 * @value: (out caller-allocates): an uninitialised #GValue, set to the
 *   parameter's type when found
 *
 * Gets a parameter set on the layer (unset parameters keep the effect's
 * default: see the #GParamSpec from
 * lrg_weather_asset_list_kind_params()).
 *
 * Returns: %TRUE when the layer sets @name
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_weather_asset_get_layer_param (LrgWeatherAsset *self,
                                            guint            preset,
                                            guint            layer,
                                            const gchar     *name,
                                            GValue          *value);

/**
 * lrg_weather_asset_unset_layer_param:
 * @self: an #LrgWeatherAsset
 * @preset: a preset index
 * @layer: a layer index
 * @name: a parameter name
 *
 * Removes a parameter (the effect default applies again on the next
 * build; a live sync rebuilds the effect).
 *
 * Returns: %TRUE when it was set
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_weather_asset_unset_layer_param (LrgWeatherAsset *self,
                                              guint            preset,
                                              guint            layer,
                                              const gchar     *name);

/**
 * lrg_weather_asset_get_layer_param_names:
 * @self: an #LrgWeatherAsset
 * @preset: a preset index
 * @layer: a layer index
 *
 * Returns: (transfer full) (array zero-terminated=1): the parameters the
 *   layer sets, in file order
 */
LRG_AVAILABLE_IN_ALL
gchar ** lrg_weather_asset_get_layer_param_names (LrgWeatherAsset *self,
                                                  guint            preset,
                                                  guint            layer);

/* --------------------------------------------------------------------------
 * Transitions
 * -------------------------------------------------------------------------- */

/**
 * lrg_weather_asset_set_transition:
 * @self: an #LrgWeatherAsset
 * @from: a preset id, `*` for any, or `none` for no weather
 * @to: a preset id, `*` for any, or `none` for no weather
 * @seconds: the crossfade
 *
 * Adds or replaces the rule for @from to @to.
 */
LRG_AVAILABLE_IN_ALL
void lrg_weather_asset_set_transition (LrgWeatherAsset *self,
                                       const gchar     *from,
                                       const gchar     *to,
                                       gfloat           seconds);

/**
 * lrg_weather_asset_remove_transition:
 * @self: an #LrgWeatherAsset
 * @from: the rule's from
 * @to: the rule's to
 *
 * Removes a rule.
 *
 * Returns: %TRUE when it existed
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_weather_asset_remove_transition (LrgWeatherAsset *self,
                                              const gchar     *from,
                                              const gchar     *to);

/**
 * lrg_weather_asset_get_transition_count:
 * @self: an #LrgWeatherAsset
 *
 * Returns: the number of transition rules
 */
LRG_AVAILABLE_IN_ALL
guint lrg_weather_asset_get_transition_count (LrgWeatherAsset *self);

/**
 * lrg_weather_asset_get_transition:
 * @self: an #LrgWeatherAsset
 * @index: a rule index
 * @from: (out) (optional) (transfer none): the rule's from
 * @to: (out) (optional) (transfer none): the rule's to
 * @seconds: (out) (optional): the crossfade
 *
 * Returns: %FALSE when @index is out of range
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_weather_asset_get_transition (LrgWeatherAsset  *self,
                                           guint             index,
                                           const gchar     **from,
                                           const gchar     **to,
                                           gfloat           *seconds);

/**
 * lrg_weather_asset_resolve_transition:
 * @self: an #LrgWeatherAsset
 * @from: (nullable): the current preset id, %NULL for none
 * @to: (nullable): the next preset id, %NULL for none
 *
 * Picks the crossfade: an exact from/to rule first, then from/`*`, then
 * `*`/to, then `*`/`*`, then the default transition.
 *
 * Returns: seconds
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_weather_asset_resolve_transition (LrgWeatherAsset *self,
                                             const gchar     *from,
                                             const gchar     *to);

/* --------------------------------------------------------------------------
 * Runtime
 * -------------------------------------------------------------------------- */

/**
 * lrg_weather_asset_build_weather:
 * @self: an #LrgWeatherAsset
 * @preset: a preset index
 * @error: return location for a #GError, or %NULL
 *
 * Builds the preset's #LrgWeather: one effect per enabled layer (its id
 * the layer id, its #LrgWeatherEffect:level the layer intensity, its
 * parameters applied with `space` first), ambient light set.
 *
 * Returns: (transfer full) (nullable): the weather, or %NULL with @error
 */
LRG_AVAILABLE_IN_ALL
LrgWeather * lrg_weather_asset_build_weather (LrgWeatherAsset  *self,
                                              guint             preset,
                                              GError          **error);

/**
 * lrg_weather_asset_sync_weather:
 * @self: an #LrgWeatherAsset
 * @preset: a preset index
 * @weather: a weather built from that preset (possibly playing)
 * @error: return location for a #GError, or %NULL
 *
 * Brings a live weather in line with the preset without restarting it:
 * effects are matched by layer id and kind; matching effects get the
 * parameters and level re-applied (their particles keep going), missing
 * ones are built and added (active when @weather is), stale ones are
 * removed, and effects follow the layer order. The hook a weather
 * inspector calls after each edit.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_weather_asset_sync_weather (LrgWeatherAsset  *self,
                                         guint             preset,
                                         LrgWeather       *weather,
                                         GError          **error);

/**
 * lrg_weather_asset_apply:
 * @self: an #LrgWeatherAsset
 * @manager: an #LrgWeatherManager
 * @error: return location for a #GError, or %NULL
 *
 * Registers every preset on @manager (presets already registered under
 * the same id are synced in place) and, when nothing is active yet,
 * switches instantly to the default preset.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_weather_asset_apply (LrgWeatherAsset    *self,
                                  LrgWeatherManager  *manager,
                                  GError            **error);

/**
 * lrg_weather_asset_transition:
 * @self: an #LrgWeatherAsset
 * @manager: an #LrgWeatherManager the asset was applied to
 * @preset_id: (nullable): the preset to change to, %NULL for clear sky
 * @error: return location for a #GError, or %NULL
 *
 * Crossfades @manager to a preset over lrg_weather_asset_resolve_transition()
 * seconds from the active weather.
 *
 * Returns: %TRUE on success (%LRG_WEATHER_ASSET_ERROR_NOT_FOUND for an
 *   unknown preset)
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_weather_asset_transition (LrgWeatherAsset    *self,
                                       LrgWeatherManager  *manager,
                                       const gchar        *preset_id,
                                       GError            **error);

G_END_DECLS
