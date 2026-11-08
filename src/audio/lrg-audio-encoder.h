/* lrg-audio-encoder.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Writing and reading audio files in many formats.  WAV and AIFF are
 * written natively; FLAC, MP3, Ogg Vorbis, Opus, AAC, ALAC and WavPack go
 * through FFmpeg: in-process with libavformat, libavcodec and
 * libswresample when libregnum is built with FFMPEG=1, otherwise through
 * the `ffmpeg` program when it is installed.  lrg_audio_decode_file()
 * reads WAV, Ogg, MP3 and QOA itself and everything else the same two
 * ways.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <gio/gio.h>
#include "../lrg-version.h"
#include "../lrg-types.h"
#include "lrg-wave-data.h"

G_BEGIN_DECLS

/**
 * LrgAudioFileFormat:
 * @LRG_AUDIO_FILE_FORMAT_WAV: RIFF WAVE (.wav), 16/24-bit integer or 32-bit float
 * @LRG_AUDIO_FILE_FORMAT_AIFF: AIFF (.aiff), 16/24-bit integer
 * @LRG_AUDIO_FILE_FORMAT_FLAC: FLAC lossless (.flac), 16/24-bit
 * @LRG_AUDIO_FILE_FORMAT_MP3: MPEG-1 Layer III (.mp3)
 * @LRG_AUDIO_FILE_FORMAT_OGG: Ogg Vorbis (.ogg)
 * @LRG_AUDIO_FILE_FORMAT_OPUS: Ogg Opus (.opus), resampled to 48 kHz
 * @LRG_AUDIO_FILE_FORMAT_AAC: AAC in MPEG-4 (.m4a)
 * @LRG_AUDIO_FILE_FORMAT_ALAC: Apple Lossless in MPEG-4 (.alac.m4a)
 * @LRG_AUDIO_FILE_FORMAT_WAVPACK: WavPack lossless (.wv)
 *
 * The audio file formats lrg_audio_encode_samples() writes.
 */
typedef enum
{
	LRG_AUDIO_FILE_FORMAT_WAV,
	LRG_AUDIO_FILE_FORMAT_AIFF,
	LRG_AUDIO_FILE_FORMAT_FLAC,
	LRG_AUDIO_FILE_FORMAT_MP3,
	LRG_AUDIO_FILE_FORMAT_OGG,
	LRG_AUDIO_FILE_FORMAT_OPUS,
	LRG_AUDIO_FILE_FORMAT_AAC,
	LRG_AUDIO_FILE_FORMAT_ALAC,
	LRG_AUDIO_FILE_FORMAT_WAVPACK
} LrgAudioFileFormat;

LRG_AVAILABLE_IN_ALL
GType lrg_audio_file_format_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_AUDIO_FILE_FORMAT (lrg_audio_file_format_get_type ())

/**
 * LrgAudioEncoderBackend:
 * @LRG_AUDIO_ENCODER_BACKEND_NONE: the format cannot be written here
 * @LRG_AUDIO_ENCODER_BACKEND_NATIVE: libregnum writes it itself
 * @LRG_AUDIO_ENCODER_BACKEND_LIBAV: in-process FFmpeg libraries
 * @LRG_AUDIO_ENCODER_BACKEND_FFMPEG: the `ffmpeg` program
 *
 * What writes a format.
 */
typedef enum
{
	LRG_AUDIO_ENCODER_BACKEND_NONE,
	LRG_AUDIO_ENCODER_BACKEND_NATIVE,
	LRG_AUDIO_ENCODER_BACKEND_LIBAV,
	LRG_AUDIO_ENCODER_BACKEND_FFMPEG
} LrgAudioEncoderBackend;

LRG_AVAILABLE_IN_ALL
GType lrg_audio_encoder_backend_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_AUDIO_ENCODER_BACKEND (lrg_audio_encoder_backend_get_type ())

/**
 * LrgAudioEncodeOptions:
 * @sample_rate: the file's rate (0: the input's; Opus always uses 48000)
 * @channels: 1 or 2 (0: the input's)
 * @bit_depth: 16 or 24 for integer formats, 32 for float WAV (0: 16)
 * @bitrate_kbps: lossy formats' bitrate (0: 192, Opus 128)
 * @quality: lossy VBR quality 0 .. 10 (higher is better; below 0 uses
 *   the bitrate)
 * @dither: add triangular dither when reducing to 16 bits (on by default)
 * @title: (nullable): the title tag
 * @artist: (nullable): the artist tag
 * @album: (nullable): the album tag
 * @comment: (nullable): the comment tag
 * @date: (nullable): the date or year tag
 *
 * How lrg_audio_encode_samples() writes a file;
 * lrg_audio_encode_options_new() fills the defaults.
 */
typedef struct
{
	guint    sample_rate;
	guint    channels;
	guint    bit_depth;
	guint    bitrate_kbps;
	gdouble  quality;
	gboolean dither;
	gchar   *title;
	gchar   *artist;
	gchar   *album;
	gchar   *comment;
	gchar   *date;

	/*< private >*/
	gpointer _reserved[4];
} LrgAudioEncodeOptions;

LRG_AVAILABLE_IN_ALL
GType lrg_audio_encode_options_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_AUDIO_ENCODE_OPTIONS (lrg_audio_encode_options_get_type ())

/**
 * lrg_audio_encode_options_new:
 *
 * Returns: (transfer full): the default options
 */
LRG_AVAILABLE_IN_ALL
LrgAudioEncodeOptions * lrg_audio_encode_options_new (void);

/**
 * lrg_audio_encode_options_copy:
 * @options: options
 *
 * Returns: (transfer full): a deep copy
 */
LRG_AVAILABLE_IN_ALL
LrgAudioEncodeOptions * lrg_audio_encode_options_copy (const LrgAudioEncodeOptions *options);

/**
 * lrg_audio_encode_options_free:
 * @options: options
 */
LRG_AVAILABLE_IN_ALL
void lrg_audio_encode_options_free (LrgAudioEncodeOptions *options);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (LrgAudioEncodeOptions, lrg_audio_encode_options_free)

/**
 * lrg_audio_file_format_from_path:
 * @path: a file name
 * @out_format: (out): its format
 *
 * Picks the format from the extension (.wav, .aif/.aiff, .flac, .mp3,
 * .ogg/.oga, .opus, .m4a/.aac, .alac, .wv).  ".alac.m4a" is ALAC; any
 * other ".m4a" is AAC.
 *
 * Returns: %TRUE for a known extension
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_audio_file_format_from_path (const gchar        *path,
                                          LrgAudioFileFormat *out_format);

/**
 * lrg_audio_file_format_from_name:
 * @name: a format name: wav, aiff, flac, mp3, ogg, opus, aac (or m4a),
 *   alac, wavpack (or wv)
 * @out_format: (out): the format
 *
 * Returns: %TRUE for a known name
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_audio_file_format_from_name (const gchar        *name,
                                          LrgAudioFileFormat *out_format);

/**
 * lrg_audio_file_format_get_extension:
 * @format: a format
 *
 * Returns: (transfer none): its usual extension, with the dot
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_audio_file_format_get_extension (LrgAudioFileFormat format);

/**
 * lrg_audio_file_format_is_lossless:
 * @format: a format
 *
 * Returns: whether the format keeps every sample
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_audio_file_format_is_lossless (LrgAudioFileFormat format);

/**
 * lrg_audio_encoder_get_backend:
 * @format: a format
 *
 * What would write @format here: native for WAV and AIFF; for the others
 * libav when built in and its encoder exists, else the `ffmpeg` program
 * when it is on the PATH and lists the encoder, else none.  The answer is
 * computed once.
 *
 * Returns: the backend
 */
LRG_AVAILABLE_IN_ALL
LrgAudioEncoderBackend lrg_audio_encoder_get_backend (LrgAudioFileFormat format);

/**
 * lrg_audio_encode_samples:
 * @samples: (array length=n_samples): interleaved float samples
 * @n_samples: frames x @channels
 * @channels: 1 or 2
 * @sample_rate: their rate
 * @format: the file format
 * @options: (nullable): how (%NULL for the defaults)
 * @path: the file to write
 * @cancellable: (nullable): cancels the encode
 * @error: (nullable): return location for an error
 *
 * Writes the samples (clamped to [-1, 1]) as a file.  A format with no
 * backend fails with %G_IO_ERROR_NOT_SUPPORTED naming what to install.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_audio_encode_samples (const gfloat                *samples,
                                   gsize                        n_samples,
                                   guint                        channels,
                                   guint                        sample_rate,
                                   LrgAudioFileFormat           format,
                                   const LrgAudioEncodeOptions *options,
                                   const gchar                 *path,
                                   GCancellable                *cancellable,
                                   GError                     **error);

/**
 * lrg_audio_encode_wave:
 * @wave: a recording
 * @format: the file format
 * @options: (nullable): how
 * @path: the file to write
 * @cancellable: (nullable): cancels the encode
 * @error: (nullable): return location for an error
 *
 * lrg_audio_encode_samples() of a wave (mono and stereo).
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_audio_encode_wave (LrgWaveData                 *wave,
                                LrgAudioFileFormat           format,
                                const LrgAudioEncodeOptions *options,
                                const gchar                 *path,
                                GCancellable                *cancellable,
                                GError                     **error);

/**
 * lrg_audio_decode_file:
 * @path: an audio file
 * @error: (nullable): return location for an error
 *
 * Reads an audio file as 32-bit float at its own rate (more than two
 * channels are mixed down to stereo).  WAV, Ogg Vorbis, MP3 and QOA are
 * read in-process; other formats (FLAC, Opus, AAC, ...) through libav or
 * the `ffmpeg` program.
 *
 * Returns: (transfer full) (nullable): the samples
 */
LRG_AVAILABLE_IN_ALL
LrgWaveData * lrg_audio_decode_file (const gchar  *path,
                                     GError      **error);

G_END_DECLS
