/* lrg-sound-bed.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * A sound bed: named ambient layers mixed into one stereo stream, each
 * with its own level that fades over time.
 *
 * A layer is a clip that either loops seamlessly (a wind bed, rain, a
 * crowd) or is scattered: played once, then silent for a random gap
 * between two bounds, then played again (birdsong, dungeon drips).  The
 * game sets each layer's level as the place and the weather change and
 * the bed fades (lrg_sound_bed_set_level()) or crossfades
 * (lrg_sound_bed_crossfade()); silent layers cost nothing.  An optional
 * #LrgAudioEffectChain runs on the bed's own mix (a low-pass for the
 * rain heard from indoors).
 *
 * Rendering is headless (lrg_sound_bed_render(), what the tests use);
 * lrg_sound_bed_start_stream() plays the bed through a graylib audio
 * stream that lrg_sound_bed_update() feeds every frame.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-types.h"
#include "lrg-audio-effect.h"
#include "lrg-wave-data.h"

G_BEGIN_DECLS

/**
 * LRG_SOUND_BED_STREAM_FRAMES:
 *
 * Frames rendered per stream buffer (double buffered).
 */
#define LRG_SOUND_BED_STREAM_FRAMES (4096)

#define LRG_TYPE_SOUND_BED (lrg_sound_bed_get_type ())
#define LRG_SOUND_BED_ERROR (lrg_sound_bed_error_quark ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgSoundBed, lrg_sound_bed, LRG, SOUND_BED, GObject)

/**
 * LrgSoundBedError:
 * @LRG_SOUND_BED_ERROR_INVALID: the layer's samples are unusable
 *   (empty, more than two channels, a bad rate)
 * @LRG_SOUND_BED_ERROR_EXISTS: a layer of that name exists
 * @LRG_SOUND_BED_ERROR_NO_DEVICE: the audio device is not open
 *
 * Error codes of #LrgSoundBed.
 */
typedef enum
{
    LRG_SOUND_BED_ERROR_INVALID,
    LRG_SOUND_BED_ERROR_EXISTS,
    LRG_SOUND_BED_ERROR_NO_DEVICE
} LrgSoundBedError;

LRG_AVAILABLE_IN_ALL
GQuark lrg_sound_bed_error_quark (void);

/**
 * lrg_sound_bed_new:
 * @sample_rate: the bed's output rate (8000 .. 192000)
 *
 * Returns: (transfer full): an empty bed at @sample_rate
 */
LRG_AVAILABLE_IN_ALL
LrgSoundBed * lrg_sound_bed_new (guint sample_rate);

/**
 * lrg_sound_bed_get_sample_rate:
 * @self: an #LrgSoundBed
 *
 * Returns: the output rate in Hz
 */
LRG_AVAILABLE_IN_ALL
guint lrg_sound_bed_get_sample_rate (LrgSoundBed *self);

/**
 * lrg_sound_bed_add_layer_samples:
 * @self: an #LrgSoundBed
 * @name: the layer's name (unique in the bed)
 * @samples: (array length=n_samples): interleaved floats, -1 .. 1
 * @n_samples: the number of floats (frames x @channels)
 * @channels: 1 or 2
 * @sample_rate: the rate of @samples (resampled linearly to the bed's)
 * @error: return location for a #GError
 *
 * Adds a looping layer at level 0 (silent until lrg_sound_bed_set_level()).
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_sound_bed_add_layer_samples (LrgSoundBed   *self,
                                          const gchar   *name,
                                          const gfloat  *samples,
                                          gsize          n_samples,
                                          guint          channels,
                                          guint          sample_rate,
                                          GError       **error);

/**
 * lrg_sound_bed_add_layer:
 * @self: an #LrgSoundBed
 * @name: the layer's name
 * @wave: the clip (mono or stereo, any rate)
 * @error: return location for a #GError
 *
 * lrg_sound_bed_add_layer_samples() with @wave's samples.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_sound_bed_add_layer (LrgSoundBed  *self,
                                  const gchar  *name,
                                  LrgWaveData  *wave,
                                  GError      **error);

/**
 * lrg_sound_bed_remove_layer:
 * @self: an #LrgSoundBed
 * @name: a layer
 *
 * Returns: %TRUE when the layer existed
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_sound_bed_remove_layer (LrgSoundBed *self,
                                     const gchar *name);

/**
 * lrg_sound_bed_has_layer:
 * @self: an #LrgSoundBed
 * @name: a layer name
 *
 * Returns: %TRUE when the bed has the layer
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_sound_bed_has_layer (LrgSoundBed *self,
                                  const gchar *name);

/**
 * lrg_sound_bed_get_layer_names:
 * @self: an #LrgSoundBed
 *
 * Returns: (transfer full): the layer names in the order they were added
 */
LRG_AVAILABLE_IN_ALL
GStrv lrg_sound_bed_get_layer_names (LrgSoundBed *self);

/**
 * lrg_sound_bed_set_scatter:
 * @self: an #LrgSoundBed
 * @name: a layer
 * @min_gap: the shortest silence between two plays (seconds, >= 0)
 * @max_gap: the longest (>= @min_gap)
 *
 * Makes the layer scattered: it plays once, then waits a random gap
 * (uniform between the bounds, from the bed's seeded generator), and so
 * on.  The first play of a scattered layer waits a gap as well.  A gap
 * of 0 .. 0 is a plain loop again.
 *
 * Returns: %TRUE when the layer exists
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_sound_bed_set_scatter (LrgSoundBed *self,
                                    const gchar *name,
                                    gdouble      min_gap,
                                    gdouble      max_gap);

/**
 * lrg_sound_bed_set_level:
 * @self: an #LrgSoundBed
 * @name: a layer
 * @level: the layer's target gain, 0 .. 1
 * @fade: seconds to reach it linearly (0: at once)
 *
 * Returns: %TRUE when the layer exists
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_sound_bed_set_level (LrgSoundBed *self,
                                  const gchar *name,
                                  gfloat       level,
                                  gdouble      fade);

/**
 * lrg_sound_bed_get_level:
 * @self: an #LrgSoundBed
 * @name: a layer
 *
 * Returns: the layer's target gain, 0 for an unknown layer
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_sound_bed_get_level (LrgSoundBed *self,
                                const gchar *name);

/**
 * lrg_sound_bed_get_gain:
 * @self: an #LrgSoundBed
 * @name: a layer
 *
 * Returns: the layer's gain now (where its fade is), 0 for an unknown
 *   layer
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_sound_bed_get_gain (LrgSoundBed *self,
                               const gchar *name);

/**
 * lrg_sound_bed_get_play_count:
 * @self: an #LrgSoundBed
 * @name: a layer
 *
 * Returns: how many times the layer's clip started from its beginning
 *   while audible (loops count each pass)
 */
LRG_AVAILABLE_IN_ALL
guint lrg_sound_bed_get_play_count (LrgSoundBed *self,
                                    const gchar *name);

/**
 * lrg_sound_bed_crossfade:
 * @self: an #LrgSoundBed
 * @from: (nullable): the layer fading out
 * @to: (nullable): the layer fading in (to level 1)
 * @seconds: the crossfade's length
 *
 * Both fades start together and end together.
 */
LRG_AVAILABLE_IN_ALL
void lrg_sound_bed_crossfade (LrgSoundBed *self,
                              const gchar *from,
                              const gchar *to,
                              gdouble      seconds);

/**
 * lrg_sound_bed_set_volume:
 * @self: an #LrgSoundBed
 * @volume: the bed's master gain, 0 .. 1 (applied at once)
 */
LRG_AVAILABLE_IN_ALL
void lrg_sound_bed_set_volume (LrgSoundBed *self,
                               gfloat       volume);

/**
 * lrg_sound_bed_get_volume:
 * @self: an #LrgSoundBed
 *
 * Returns: the master gain
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_sound_bed_get_volume (LrgSoundBed *self);

/**
 * lrg_sound_bed_set_seed:
 * @self: an #LrgSoundBed
 * @seed: the seed of the scatter gaps
 */
LRG_AVAILABLE_IN_ALL
void lrg_sound_bed_set_seed (LrgSoundBed *self,
                             guint32      seed);

/**
 * lrg_sound_bed_get_effects:
 * @self: an #LrgSoundBed
 *
 * Returns: (transfer none): the effects run on the bed's mix after the
 *   master gain (empty at first)
 */
LRG_AVAILABLE_IN_ALL
LrgAudioEffectChain * lrg_sound_bed_get_effects (LrgSoundBed *self);

/**
 * lrg_sound_bed_render:
 * @self: an #LrgSoundBed
 * @frames: (array) (out caller-allocates): 2 x @n_frames floats
 * @n_frames: frames to render
 *
 * Mixes every audible layer into interleaved stereo (mono layers go to
 * both sides), advances their fades and positions, then runs the
 * effects.
 *
 * Returns: @n_frames
 */
LRG_AVAILABLE_IN_ALL
guint lrg_sound_bed_render (LrgSoundBed *self,
                            gfloat      *frames,
                            guint        n_frames);

/**
 * lrg_sound_bed_start_stream:
 * @self: an #LrgSoundBed
 * @error: return location for a #GError
 *
 * Opens a graylib stream on the open audio device.
 *
 * Returns: %TRUE when the bed streams (or already did)
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_sound_bed_start_stream (LrgSoundBed  *self,
                                     GError      **error);

/**
 * lrg_sound_bed_update:
 * @self: an #LrgSoundBed
 *
 * Refills the stream's free buffers (call every frame); no-op without
 * a stream.
 */
LRG_AVAILABLE_IN_ALL
void lrg_sound_bed_update (LrgSoundBed *self);

/**
 * lrg_sound_bed_stop_stream:
 * @self: an #LrgSoundBed
 *
 * Closes the stream (the layers keep their state).
 */
LRG_AVAILABLE_IN_ALL
void lrg_sound_bed_stop_stream (LrgSoundBed *self);

/**
 * lrg_sound_bed_has_stream:
 * @self: an #LrgSoundBed
 *
 * Returns: %TRUE while the bed streams to the device
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_sound_bed_has_stream (LrgSoundBed *self);

G_END_DECLS
