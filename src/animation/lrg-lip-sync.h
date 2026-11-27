/* lrg-lip-sync.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgLipSync: turns timed phonemes (the alignment a forced aligner or a
 * TTS engine produces) into viseme weight keys of an
 * #LrgSkeleton2DClip, with short cross-fades between mouth shapes.  An
 * audio-only fallback drives the "A" shape from loudness.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-types.h"
#include "../lrg-enums.h"
#include "lrg-skeleton2d.h"

G_BEGIN_DECLS

/**
 * LRG_LIP_SYNC_ERROR:
 *
 * Error domain of the #LrgLipSync loaders.  Errors are #LrgLipSyncError
 * values.
 *
 * Since: 0.14
 */
#define LRG_LIP_SYNC_ERROR (lrg_lip_sync_error_quark ())

/**
 * lrg_lip_sync_error_quark:
 *
 * The error quark of %LRG_LIP_SYNC_ERROR.
 *
 * Returns: the quark
 *
 * Since: 0.14
 */
LRG_AVAILABLE_IN_ALL
GQuark lrg_lip_sync_error_quark (void);

/**
 * LRG_LIP_SYNC_DEFAULT_CROSSFADE:
 *
 * A cross-fade between mouth shapes that reads as speech at film and game
 * frame rates (seconds).
 *
 * Since: 0.14
 */
#define LRG_LIP_SYNC_DEFAULT_CROSSFADE (0.06f)

#define LRG_TYPE_LIP_SYNC (lrg_lip_sync_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgLipSync, lrg_lip_sync, LRG, LIP_SYNC, GObject)

/**
 * lrg_lip_sync_new:
 *
 * Creates an empty phoneme track.
 *
 * Returns: (transfer full): a new #LrgLipSync
 *
 * Since: 0.14
 */
LRG_AVAILABLE_IN_ALL
LrgLipSync * lrg_lip_sync_new (void);

/**
 * lrg_lip_sync_new_from_wave:
 * @wave: the spoken line
 * @fps: analysis windows per second (24 to 60 is typical)
 * @threshold: windowed RMS loudness (0 .. 1 of full scale) at or above
 *   which the mouth is open; 0.02 to 0.05 suits normalised speech
 *
 * The audio-only fallback: downmixes @wave to mono, measures the RMS
 * loudness of consecutive 1 / @fps second windows and turns every run of
 * loud windows into an "AA" phoneme (the %LRG_VISEME_A shape) and every
 * run of quiet ones into "sil" (%LRG_VISEME_REST).  It does not tell
 * vowels apart; use a phoneme alignment for that.
 *
 * Returns: (transfer full) (nullable): the track, or %NULL if @wave is
 *   empty or invalid or @fps is not positive
 *
 * Since: 0.14
 */
LRG_AVAILABLE_IN_ALL
LrgLipSync * lrg_lip_sync_new_from_wave (LrgWaveData *wave,
                                         gdouble      fps,
                                         gfloat       threshold);

/**
 * lrg_lip_sync_phoneme_to_viseme:
 * @phoneme: an ARPAbet phoneme as forced aligners write it (`"AH0"`,
 *   `"hh"`, `"sil"`)
 *
 * Maps a phoneme of the CMU (ARPAbet) phone set — the one Montreal
 * Forced Aligner's English models and Gentle use — to a viseme of the
 * standard shape sheet.  Matching ignores case, a trailing stress digit
 * (`AH0`, `IY1`) and a trailing word-position tag (`_B`, `_I`, `_E`,
 * `_S`, as Gentle writes them).  Silence and noise labels (`sil`, `sp`,
 * `spn`, an empty label) are %LRG_VISEME_REST; an unknown phoneme is
 * %LRG_VISEME_REST too and logs a debug message.
 *
 * Returns: the viseme
 *
 * Since: 0.14
 */
LRG_AVAILABLE_IN_ALL
LrgViseme lrg_lip_sync_phoneme_to_viseme (const gchar *phoneme);

/**
 * lrg_lip_sync_add_phoneme:
 * @self: an #LrgLipSync
 * @start: start time in seconds (0 or more)
 * @end: end time in seconds (@start or more)
 * @phoneme: the phoneme label (see lrg_lip_sync_phoneme_to_viseme())
 *
 * Adds a timed phoneme.  Phonemes are kept sorted by start time (equal
 * starts keep their insertion order).  Overlaps are allowed; when
 * writing keys a later phoneme cuts the earlier one short.
 *
 * Returns: %TRUE on success, %FALSE for a negative or non-finite time or
 *   an end before the start
 *
 * Since: 0.14
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_lip_sync_add_phoneme (LrgLipSync  *self,
                                   gdouble      start,
                                   gdouble      end,
                                   const gchar *phoneme);

/**
 * lrg_lip_sync_get_phoneme_count:
 * @self: an #LrgLipSync
 *
 * Returns: the number of phonemes
 *
 * Since: 0.14
 */
LRG_AVAILABLE_IN_ALL
guint lrg_lip_sync_get_phoneme_count (LrgLipSync *self);

/**
 * lrg_lip_sync_get_phoneme:
 * @self: an #LrgLipSync
 * @index: a phoneme index in start-time order
 * @start: (out) (optional): the start time
 * @end: (out) (optional): the end time
 * @phoneme: (out) (optional) (transfer none): the label
 * @viseme: (out) (optional): the viseme it maps to
 *
 * Reads a phoneme.
 *
 * Returns: %TRUE if @index is valid
 *
 * Since: 0.14
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_lip_sync_get_phoneme (LrgLipSync   *self,
                                   guint         index,
                                   gdouble      *start,
                                   gdouble      *end,
                                   const gchar **phoneme,
                                   LrgViseme    *viseme);

/**
 * lrg_lip_sync_get_duration:
 * @self: an #LrgLipSync
 *
 * Returns: the latest phoneme end time (0 when empty)
 *
 * Since: 0.14
 */
LRG_AVAILABLE_IN_ALL
gdouble lrg_lip_sync_get_duration (LrgLipSync *self);

/**
 * lrg_lip_sync_clear:
 * @self: an #LrgLipSync
 *
 * Removes every phoneme.
 *
 * Since: 0.14
 */
LRG_AVAILABLE_IN_ALL
void lrg_lip_sync_clear (LrgLipSync *self);

/**
 * lrg_lip_sync_load_yaml:
 * @self: an #LrgLipSync
 * @data: YAML text: a list of `{start, end, phoneme}` mappings
 * @length: length of @data, or -1 if it is NUL-terminated
 * @error: return location for a #GError
 *
 * Replaces the phonemes with those of a YAML alignment:
 *
 * |[
 * - {start: 0.00, end: 0.12, phoneme: HH}
 * - {start: 0.12, end: 0.21, phoneme: AH0}
 * ]|
 *
 * Loading is strict (an unknown key is an error) and messages name the
 * node: `<data>: /1/end: expected a number`.  On error the track is left
 * unchanged.
 *
 * Returns: %TRUE on success
 *
 * Since: 0.14
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_lip_sync_load_yaml (LrgLipSync   *self,
                                 const gchar  *data,
                                 gssize        length,
                                 GError      **error);

/**
 * lrg_lip_sync_load_csv:
 * @self: an #LrgLipSync
 * @data: CSV text: `start,end,phoneme` rows
 * @length: length of @data, or -1 if it is NUL-terminated
 * @error: return location for a #GError
 *
 * Replaces the phonemes with those of a CSV alignment.  A first row whose
 * start field is not a number is a header and skipped; blank lines and
 * lines starting with `#` are skipped; fields are trimmed.  Messages name
 * the line: `<data>: line 3: expected 3 fields, got 2`.  On error the
 * track is left unchanged.
 *
 * Returns: %TRUE on success
 *
 * Since: 0.14
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_lip_sync_load_csv (LrgLipSync   *self,
                                const gchar  *data,
                                gssize        length,
                                GError      **error);

/**
 * lrg_lip_sync_load_file:
 * @self: an #LrgLipSync
 * @path: a `.csv` file, or a YAML file (any other extension)
 * @error: return location for a #GError
 *
 * Reads @path with lrg_lip_sync_load_csv() or lrg_lip_sync_load_yaml()
 * by its extension; messages name @path instead of `<data>`.
 *
 * Returns: %TRUE on success
 *
 * Since: 0.14
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_lip_sync_load_file (LrgLipSync   *self,
                                 const gchar  *path,
                                 GError      **error);

/**
 * lrg_lip_sync_write_clip:
 * @self: an #LrgLipSync
 * @clip: the clip to key
 * @slot: the mouth slot's name
 * @crossfade: seconds each change of mouth shape blends over (centred
 *   on the phoneme boundary and shortened so it never takes more than
 *   half of either neighbour); 0 switches shapes with stepped keys
 *
 * Writes viseme weight keys for @slot into @clip: the time line is cut
 * into runs of one viseme (gaps and silences are %LRG_VISEME_REST, runs
 * of the same viseme merge), and each run's shape goes from 0 to 1 and
 * back across its boundaries.  Only the shapes that occur are keyed.
 * Existing keys of those shapes at the same times are replaced; the
 * clip's duration is extended to the end of the track.
 *
 * Returns: the number of keys written
 *
 * Since: 0.14
 */
LRG_AVAILABLE_IN_ALL
guint lrg_lip_sync_write_clip (LrgLipSync        *self,
                               LrgSkeleton2DClip *clip,
                               const gchar       *slot,
                               gfloat             crossfade);

G_END_DECLS
