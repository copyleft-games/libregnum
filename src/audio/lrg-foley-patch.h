/* lrg-foley-patch.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * An editable recipe for a synthesized sound effect: global settings and
 * an ordered list of layers (struck bodies, noise grains and washes, a
 * rain bed, oscillator tones and swept noise bursts), saved as a .rfoley
 * YAML file.  lrg_foley_synthesize() renders the built-in kinds through a
 * patch, so lrg_foley_patch_new_from_kind() starts from any of them.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-types.h"
#include "lrg-foley.h"
#include "lrg-wave-data.h"

G_BEGIN_DECLS

/**
 * LRG_FOLEY_PATCH_FORMAT_VERSION:
 *
 * The `version` written in .rfoley files.
 */
#define LRG_FOLEY_PATCH_FORMAT_VERSION (1)

/**
 * LRG_FOLEY_PATCH_MAX_LAYERS:
 *
 * The most layers a patch holds.
 */
#define LRG_FOLEY_PATCH_MAX_LAYERS (256)

/**
 * LRG_FOLEY_PATCH_MAX_DURATION:
 *
 * The longest patch in seconds.
 */
#define LRG_FOLEY_PATCH_MAX_DURATION (60.0)

/**
 * LrgFoleyLayerType:
 * @LRG_FOLEY_LAYER_BODY: a damped sine whose pitch sweeps over its decay
 *   (thuds, knocks, rings, bubbles)
 * @LRG_FOLEY_LAYER_GRAIN: a raised-cosine burst of band-limited noise
 *   (scrapes, crunches, ticks)
 * @LRG_FOLEY_LAYER_WASH: low-passed noise under an exponential decay
 *   (soft impacts, splashes)
 * @LRG_FOLEY_LAYER_BED: a drifting band of noise with droplet ticks over
 *   the whole clip (rain, static, wind)
 * @LRG_FOLEY_LAYER_TONE: an oscillator with a pitch sweep, envelope and
 *   vibrato (lasers, beeps, power-ups, alarms)
 * @LRG_FOLEY_LAYER_NOISE: a noise burst through a sweeping low-pass and a
 *   high-pass (whooshes, explosions, risers)
 *
 * The kinds of layer a #LrgFoleyPatch mixes.
 */
typedef enum
{
	LRG_FOLEY_LAYER_BODY,
	LRG_FOLEY_LAYER_GRAIN,
	LRG_FOLEY_LAYER_WASH,
	LRG_FOLEY_LAYER_BED,
	LRG_FOLEY_LAYER_TONE,
	LRG_FOLEY_LAYER_NOISE
} LrgFoleyLayerType;

LRG_AVAILABLE_IN_ALL
GType lrg_foley_layer_type_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_FOLEY_LAYER_TYPE (lrg_foley_layer_type_get_type ())

/**
 * LrgFoleyPatchError:
 * @LRG_FOLEY_PATCH_ERROR_PARSE: the text is not YAML
 * @LRG_FOLEY_PATCH_ERROR_INVALID_VALUE: a key holds a wrong or out-of-range value
 * @LRG_FOLEY_PATCH_ERROR_IO: the file could not be read or written
 * @LRG_FOLEY_PATCH_ERROR_RENDER: the patch cannot be rendered
 *
 * Errors of #LrgFoleyPatch loading, saving and rendering.
 */
typedef enum
{
	LRG_FOLEY_PATCH_ERROR_PARSE,
	LRG_FOLEY_PATCH_ERROR_INVALID_VALUE,
	LRG_FOLEY_PATCH_ERROR_IO,
	LRG_FOLEY_PATCH_ERROR_RENDER
} LrgFoleyPatchError;

#define LRG_FOLEY_PATCH_ERROR (lrg_foley_patch_error_quark ())

LRG_AVAILABLE_IN_ALL
GQuark lrg_foley_patch_error_quark (void);

#define LRG_TYPE_FOLEY_PATCH (lrg_foley_patch_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgFoleyPatch, lrg_foley_patch, LRG, FOLEY_PATCH, GObject)

/* ==========================================================================
 * Construction, files
 * ========================================================================== */

/**
 * lrg_foley_patch_new:
 *
 * An empty patch: half a second at 44100 Hz, no layers, peak-normalised.
 * The global settings are GObject properties: #LrgFoleyPatch:name,
 * #LrgFoleyPatch:duration, #LrgFoleyPatch:sample-rate, #LrgFoleyPatch:seed,
 * #LrgFoleyPatch:loop, #LrgFoleyPatch:loop-tail, #LrgFoleyPatch:fade,
 * #LrgFoleyPatch:peak and #LrgFoleyPatch:normalize.
 *
 * Returns: (transfer full): a new #LrgFoleyPatch
 */
LRG_AVAILABLE_IN_ALL
LrgFoleyPatch * lrg_foley_patch_new (void);

/**
 * lrg_foley_patch_new_from_kind:
 * @kind: a built-in #LrgFoleyKind
 * @variant: which take (any number; each gives other random details)
 * @sample_rate: the rate in Hz (0 for %LRG_FOLEY_DEFAULT_RATE)
 * @error: (nullable): return location for an error
 *
 * The recipe lrg_foley_synthesize() renders for @kind and @variant, as an
 * editable patch.  At the same rate it renders to exactly the same samples
 * (lrg_foley_patch_render_wav() returns the bytes lrg_foley_synthesize()
 * does).  Every layer keeps the noise seed it was given, so changing one
 * layer does not change the others.
 *
 * Returns: (transfer full) (nullable): a new patch, or %NULL for an
 *   unknown kind or a rate outside 8000 .. 96000
 */
LRG_AVAILABLE_IN_ALL
LrgFoleyPatch * lrg_foley_patch_new_from_kind (LrgFoleyKind   kind,
                                               guint          variant,
                                               guint          sample_rate,
                                               GError       **error);

/**
 * lrg_foley_patch_new_from_data:
 * @data: .rfoley YAML text
 * @length: length of @data, or -1 when NUL-terminated
 * @error: (nullable): return location for an error
 *
 * Returns: (transfer full) (nullable): the patch, or %NULL with @error set
 *   (%LRG_FOLEY_PATCH_ERROR) naming the first wrong key by its YAML path
 */
LRG_AVAILABLE_IN_ALL
LrgFoleyPatch * lrg_foley_patch_new_from_data (const gchar  *data,
                                               gssize        length,
                                               GError      **error);

/**
 * lrg_foley_patch_new_from_file:
 * @path: a .rfoley file
 * @error: (nullable): return location for an error
 *
 * Returns: (transfer full) (nullable): the patch, or %NULL with @error set
 */
LRG_AVAILABLE_IN_ALL
LrgFoleyPatch * lrg_foley_patch_new_from_file (const gchar  *path,
                                               GError      **error);

/**
 * lrg_foley_patch_to_data:
 * @self: an #LrgFoleyPatch
 * @length: (out) (optional): the length of the text
 * @error: (nullable): return location for an error
 *
 * Writes the patch as .rfoley YAML.  Layer parameters that hold their
 * default are left out; the text of a patch read back is the same.
 *
 * Returns: (transfer full) (nullable): the YAML text
 */
LRG_AVAILABLE_IN_ALL
gchar * lrg_foley_patch_to_data (LrgFoleyPatch  *self,
                                 gsize          *length,
                                 GError        **error);

/**
 * lrg_foley_patch_save_to_file:
 * @self: an #LrgFoleyPatch
 * @path: the file to write
 * @error: (nullable): return location for an error
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_foley_patch_save_to_file (LrgFoleyPatch  *self,
                                       const gchar    *path,
                                       GError        **error);

/**
 * lrg_foley_patch_copy:
 * @self: an #LrgFoleyPatch
 *
 * Returns: (transfer full): a deep copy
 */
LRG_AVAILABLE_IN_ALL
LrgFoleyPatch * lrg_foley_patch_copy (LrgFoleyPatch *self);

/* ==========================================================================
 * Global settings (also GObject properties)
 * ========================================================================== */

/**
 * lrg_foley_patch_get_duration:
 * @self: an #LrgFoleyPatch
 *
 * Returns: the length of the rendered clip in seconds
 */
LRG_AVAILABLE_IN_ALL
gdouble lrg_foley_patch_get_duration (LrgFoleyPatch *self);

/**
 * lrg_foley_patch_get_sample_rate:
 * @self: an #LrgFoleyPatch
 *
 * Returns: the render rate in Hz
 */
LRG_AVAILABLE_IN_ALL
guint lrg_foley_patch_get_sample_rate (LrgFoleyPatch *self);

/**
 * lrg_foley_patch_get_name:
 * @self: an #LrgFoleyPatch
 *
 * Returns: (transfer none) (nullable): the display name
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_foley_patch_get_name (LrgFoleyPatch *self);

/**
 * lrg_foley_patch_set_global:
 * @self: an #LrgFoleyPatch
 * @name: a numeric or boolean global (`duration`, `sample-rate`, `seed`,
 *   `variation`, `loop`, `loop-tail`, `fade`, `peak`, `normalize`)
 * @value: the value, clamped to the setting's range (booleans: non-zero
 *   is true)
 *
 * Returns: %TRUE when @name is a known setting and @value is finite
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_foley_patch_set_global (LrgFoleyPatch *self,
                                     const gchar   *name,
                                     gdouble        value);

/* ==========================================================================
 * Layers
 * ========================================================================== */

/**
 * lrg_foley_patch_get_n_layers:
 * @self: an #LrgFoleyPatch
 *
 * Returns: the number of layers
 */
LRG_AVAILABLE_IN_ALL
guint lrg_foley_patch_get_n_layers (LrgFoleyPatch *self);

/**
 * lrg_foley_patch_add_layer:
 * @self: an #LrgFoleyPatch
 * @type: the kind of layer
 *
 * Appends a layer of @type with every parameter at its default.
 *
 * Returns: the new layer's index, or -1 when the patch is full
 */
LRG_AVAILABLE_IN_ALL
gint lrg_foley_patch_add_layer (LrgFoleyPatch     *self,
                                LrgFoleyLayerType  type);

/**
 * lrg_foley_patch_remove_layer:
 * @self: an #LrgFoleyPatch
 * @index: a layer index
 *
 * Returns: %TRUE when the layer existed
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_foley_patch_remove_layer (LrgFoleyPatch *self,
                                       guint          index);

/**
 * lrg_foley_patch_move_layer:
 * @self: an #LrgFoleyPatch
 * @from: the index of the layer to move
 * @to: its new index
 *
 * Layers mix in any order to the same sound; the order is how lists show
 * them.
 *
 * Returns: %TRUE when both indices are valid
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_foley_patch_move_layer (LrgFoleyPatch *self,
                                     guint          from,
                                     guint          to);

/**
 * lrg_foley_patch_get_layer_type:
 * @self: an #LrgFoleyPatch
 * @index: a layer index
 *
 * Returns: the layer's type (%LRG_FOLEY_LAYER_BODY for a bad index)
 */
LRG_AVAILABLE_IN_ALL
LrgFoleyLayerType lrg_foley_patch_get_layer_type (LrgFoleyPatch *self,
                                                  guint          index);

/**
 * lrg_foley_patch_set_layer_param:
 * @self: an #LrgFoleyPatch
 * @index: a layer index
 * @name: a parameter of the layer's type (lrg_foley_layer_type_list_params())
 * @value: the value, clamped to the parameter's range
 *
 * Returns: %TRUE when the layer and parameter exist and @value is finite
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_foley_patch_set_layer_param (LrgFoleyPatch *self,
                                          guint          index,
                                          const gchar   *name,
                                          gdouble        value);

/**
 * lrg_foley_patch_get_layer_param:
 * @self: an #LrgFoleyPatch
 * @index: a layer index
 * @name: a parameter name
 *
 * Returns: the value, or NaN for an unknown layer or parameter
 */
LRG_AVAILABLE_IN_ALL
gdouble lrg_foley_patch_get_layer_param (LrgFoleyPatch *self,
                                         guint          index,
                                         const gchar   *name);

/**
 * lrg_foley_layer_type_list_params:
 * @type: a layer type
 *
 * The parameters of @type in display order.  Every type has `at`
 * (seconds), `amp`, `mute`, `seed` (0: derived from the patch seed),
 * `count`, `spread` (seconds) and `jitter` (0 .. 1), then its own.
 *
 * Returns: (transfer full) (array zero-terminated=1): the names
 */
LRG_AVAILABLE_IN_ALL
gchar ** lrg_foley_layer_type_list_params (LrgFoleyLayerType type);

/**
 * lrg_foley_layer_type_get_param_info:
 * @type: a layer type
 * @name: a parameter name
 * @out_min: (out) (optional): the lowest value
 * @out_max: (out) (optional): the highest value
 * @out_default: (out) (optional): the default
 * @out_blurb: (out) (optional) (transfer none): a one-line description
 *
 * Returns: %TRUE when @type has the parameter
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_foley_layer_type_get_param_info (LrgFoleyLayerType   type,
                                              const gchar        *name,
                                              gdouble            *out_min,
                                              gdouble            *out_max,
                                              gdouble            *out_default,
                                              const gchar       **out_blurb);

/* ==========================================================================
 * Rendering
 * ========================================================================== */

/**
 * lrg_foley_patch_render:
 * @self: an #LrgFoleyPatch
 * @n_frames: (out): the number of frames (mono samples)
 * @error: (nullable): return location for an error
 *
 * Renders the patch to mono float samples at its rate: every layer mixed,
 * the fades applied (or, for a loop, the tail folded onto the head), then
 * peak-normalised to #LrgFoleyPatch:peak when #LrgFoleyPatch:normalize is
 * set, and clamped to [-1, 1].  The same patch always renders the same
 * samples.
 *
 * Returns: (transfer full) (array length=n_frames) (nullable): the samples
 */
LRG_AVAILABLE_IN_ALL
gfloat * lrg_foley_patch_render (LrgFoleyPatch  *self,
                                 gsize          *n_frames,
                                 GError        **error);

/**
 * lrg_foley_patch_render_wave:
 * @self: an #LrgFoleyPatch
 * @error: (nullable): return location for an error
 *
 * Returns: (transfer full) (nullable): the render as 32-bit float mono
 *   #LrgWaveData
 */
LRG_AVAILABLE_IN_ALL
LrgWaveData * lrg_foley_patch_render_wave (LrgFoleyPatch  *self,
                                           GError        **error);

/**
 * lrg_foley_patch_render_wav:
 * @self: an #LrgFoleyPatch
 * @error: (nullable): return location for an error
 *
 * Returns: (transfer full) (nullable): the render as a complete 16-bit
 *   mono PCM WAV file image
 */
LRG_AVAILABLE_IN_ALL
GBytes * lrg_foley_patch_render_wav (LrgFoleyPatch  *self,
                                     GError        **error);

G_END_DECLS
