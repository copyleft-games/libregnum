/* lrg-audio-encoder.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Audio files out and in (see lrg-audio-encoder.h and
 * docs/modules/audio/encoder.org).
 *
 * Writing: WAV and AIFF natively (16/24-bit integer with optional TPDF
 * dither, 32-bit float WAV).  The other formats go to FFmpeg: the library
 * backend (lrg-audio-encoder-libav.c) when it was built in and has the
 * encoder, else the `ffmpeg` program, fed the samples as raw float on its
 * standard input (no temporary file).  Which backend serves a format is
 * worked out once and cached.
 *
 * Reading: WAV, Ogg Vorbis, MP3 and QOA through raylib's decoders
 * (LrgWaveData); anything else through libav or the `ffmpeg` program
 * (with `ffprobe` for the rate and channel count).
 */

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_AUDIO

#include "config.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <gio/gio.h>

#include "lrg-audio-encoder.h"
#include "lrg-audio-encoder-private.h"
#include "lrg-audio-dsp-private.h"

G_DEFINE_ENUM_TYPE (LrgAudioFileFormat, lrg_audio_file_format,
	G_DEFINE_ENUM_VALUE (LRG_AUDIO_FILE_FORMAT_WAV, "wav"),
	G_DEFINE_ENUM_VALUE (LRG_AUDIO_FILE_FORMAT_AIFF, "aiff"),
	G_DEFINE_ENUM_VALUE (LRG_AUDIO_FILE_FORMAT_FLAC, "flac"),
	G_DEFINE_ENUM_VALUE (LRG_AUDIO_FILE_FORMAT_MP3, "mp3"),
	G_DEFINE_ENUM_VALUE (LRG_AUDIO_FILE_FORMAT_OGG, "ogg"),
	G_DEFINE_ENUM_VALUE (LRG_AUDIO_FILE_FORMAT_OPUS, "opus"),
	G_DEFINE_ENUM_VALUE (LRG_AUDIO_FILE_FORMAT_AAC, "aac"),
	G_DEFINE_ENUM_VALUE (LRG_AUDIO_FILE_FORMAT_ALAC, "alac"),
	G_DEFINE_ENUM_VALUE (LRG_AUDIO_FILE_FORMAT_WAVPACK, "wavpack"))

G_DEFINE_ENUM_TYPE (LrgAudioEncoderBackend, lrg_audio_encoder_backend,
	G_DEFINE_ENUM_VALUE (LRG_AUDIO_ENCODER_BACKEND_NONE, "none"),
	G_DEFINE_ENUM_VALUE (LRG_AUDIO_ENCODER_BACKEND_NATIVE, "native"),
	G_DEFINE_ENUM_VALUE (LRG_AUDIO_ENCODER_BACKEND_LIBAV, "libav"),
	G_DEFINE_ENUM_VALUE (LRG_AUDIO_ENCODER_BACKEND_FFMPEG, "ffmpeg"))

G_DEFINE_BOXED_TYPE (LrgAudioEncodeOptions, lrg_audio_encode_options, lrg_audio_encode_options_copy,
                     lrg_audio_encode_options_free)

#define N_FORMATS (LRG_AUDIO_FILE_FORMAT_WAVPACK + 1)

/* the FFmpeg encoders (preferred first) and muxer of each format */
typedef struct
{
	const gchar *ext;
	const gchar *codecs[3];
	const gchar *container;
	gboolean     lossless;
	const gchar *package;     /* what to install when missing */
} FormatInfo;

static const FormatInfo formats[N_FORMATS] = {
	{ ".wav", { NULL }, NULL, TRUE, NULL },
	{ ".aiff", { NULL }, NULL, TRUE, NULL },
	{ ".flac", { "flac", NULL }, "flac", TRUE, "ffmpeg-free" },
	{ ".mp3", { "libmp3lame", NULL }, "mp3", FALSE, "ffmpeg-free (with libmp3lame)" },
	{ ".ogg", { "libvorbis", "vorbis", NULL }, "ogg", FALSE, "ffmpeg-free (with libvorbis)" },
	{ ".opus", { "libopus", "opus", NULL }, "opus", FALSE, "ffmpeg-free (with libopus)" },
	{ ".m4a", { "aac", "libfdk_aac", NULL }, "ipod", FALSE, "ffmpeg-free" },
	{ ".alac.m4a", { "alac", NULL }, "ipod", TRUE, "ffmpeg-free" },
	{ ".wv", { "wavpack", NULL }, "wv", TRUE, "ffmpeg-free" },
};

/* ==========================================================================
 * Options and format names
 * ========================================================================== */

LrgAudioEncodeOptions *
lrg_audio_encode_options_new (void)
{
	LrgAudioEncodeOptions *o = g_new0 (LrgAudioEncodeOptions, 1);

	o->quality = -1.0;
	o->dither = TRUE;
	return o;
}

LrgAudioEncodeOptions *
lrg_audio_encode_options_copy (const LrgAudioEncodeOptions *options)
{
	LrgAudioEncodeOptions *o;

	g_return_val_if_fail (options != NULL, NULL);
	o = g_new0 (LrgAudioEncodeOptions, 1);
	*o = *options;
	o->title = g_strdup (options->title);
	o->artist = g_strdup (options->artist);
	o->album = g_strdup (options->album);
	o->comment = g_strdup (options->comment);
	o->date = g_strdup (options->date);
	return o;
}

void
lrg_audio_encode_options_free (LrgAudioEncodeOptions *options)
{
	if (options == NULL)
		return;
	g_free (options->title);
	g_free (options->artist);
	g_free (options->album);
	g_free (options->comment);
	g_free (options->date);
	g_free (options);
}

gboolean
lrg_audio_file_format_from_path (const gchar        *path,
                                 LrgAudioFileFormat *out_format)
{
	g_autofree gchar *lower = NULL;
	const gchar      *dot;

	g_return_val_if_fail (out_format != NULL, FALSE);
	if (path == NULL)
		return FALSE;
	lower = g_ascii_strdown (path, -1);
	if (g_str_has_suffix (lower, ".alac.m4a") || g_str_has_suffix (lower, ".alac"))
	{
		*out_format = LRG_AUDIO_FILE_FORMAT_ALAC;
		return TRUE;
	}
	dot = strrchr (lower, '.');
	if (dot == NULL)
		return FALSE;
	return lrg_audio_file_format_from_name (dot + 1, out_format);
}

gboolean
lrg_audio_file_format_from_name (const gchar        *name,
                                 LrgAudioFileFormat *out_format)
{
	static const struct { const gchar *name; LrgAudioFileFormat format; } names[] = {
		{ "wav", LRG_AUDIO_FILE_FORMAT_WAV }, { "wave", LRG_AUDIO_FILE_FORMAT_WAV },
		{ "aiff", LRG_AUDIO_FILE_FORMAT_AIFF }, { "aif", LRG_AUDIO_FILE_FORMAT_AIFF },
		{ "flac", LRG_AUDIO_FILE_FORMAT_FLAC }, { "mp3", LRG_AUDIO_FILE_FORMAT_MP3 },
		{ "ogg", LRG_AUDIO_FILE_FORMAT_OGG }, { "oga", LRG_AUDIO_FILE_FORMAT_OGG },
		{ "vorbis", LRG_AUDIO_FILE_FORMAT_OGG }, { "opus", LRG_AUDIO_FILE_FORMAT_OPUS },
		{ "aac", LRG_AUDIO_FILE_FORMAT_AAC }, { "m4a", LRG_AUDIO_FILE_FORMAT_AAC },
		{ "alac", LRG_AUDIO_FILE_FORMAT_ALAC }, { "wavpack", LRG_AUDIO_FILE_FORMAT_WAVPACK },
		{ "wv", LRG_AUDIO_FILE_FORMAT_WAVPACK },
	};
	guint i;

	g_return_val_if_fail (out_format != NULL, FALSE);
	for (i = 0; name != NULL && i < G_N_ELEMENTS (names); i++)
		if (g_ascii_strcasecmp (name, names[i].name) == 0)
		{
			*out_format = names[i].format;
			return TRUE;
		}
	return FALSE;
}

const gchar *
lrg_audio_file_format_get_extension (LrgAudioFileFormat format)
{
	if ((guint) format >= N_FORMATS)
		return "";
	return formats[format].ext;
}

gboolean
lrg_audio_file_format_is_lossless (LrgAudioFileFormat format)
{
	return (guint) format < N_FORMATS && formats[format].lossless;
}

/* ==========================================================================
 * Backends
 * ========================================================================== */

/* the encoders the `ffmpeg` program lists (computed once) */
static GHashTable *
cli_encoders (void)
{
	static gsize       once = 0;
	static GHashTable *encoders = NULL;

	if (g_once_init_enter (&once))
	{
		g_autofree gchar *ffmpeg = g_find_program_in_path ("ffmpeg");
		GHashTable       *table = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);

		if (ffmpeg != NULL)
		{
			const gchar      *argv[] = { ffmpeg, "-hide_banner", "-encoders", NULL };
			g_autofree gchar *out = NULL;

			if (g_spawn_sync (NULL, (gchar **) argv, NULL, G_SPAWN_STDERR_TO_DEV_NULL, NULL, NULL, &out, NULL,
			                  NULL, NULL) && out != NULL)
			{
				g_auto(GStrv) lines = g_strsplit (out, "\n", -1);
				guint         i;

				/* " A....D flac   FLAC (Free Lossless Audio Codec)" */
				for (i = 0; lines[i] != NULL; i++)
				{
					g_auto(GStrv) f = g_strsplit_set (g_strstrip (lines[i]), " \t", 3);

					if (g_strv_length (f) >= 2 && f[0][0] == 'A')
						g_hash_table_add (table, g_strdup (f[1]));
				}
			}
		}
		encoders = table;
		g_once_init_leave (&once, 1);
	}
	return encoders;
}

/* the encoder name serving @format on @backend, or NULL */
static const gchar *
codec_for (LrgAudioFileFormat     format,
           LrgAudioEncoderBackend backend)
{
	guint i;

	for (i = 0; formats[format].codecs[i] != NULL; i++)
	{
		const gchar *name = formats[format].codecs[i];

		if (backend == LRG_AUDIO_ENCODER_BACKEND_LIBAV && lrg_audio_libav_has_encoder (name))
			return name;
		if (backend == LRG_AUDIO_ENCODER_BACKEND_FFMPEG && g_hash_table_contains (cli_encoders (), name))
			return name;
	}
	return NULL;
}

LrgAudioEncoderBackend
lrg_audio_encoder_get_backend (LrgAudioFileFormat format)
{
	if ((guint) format >= N_FORMATS)
		return LRG_AUDIO_ENCODER_BACKEND_NONE;
	if (format == LRG_AUDIO_FILE_FORMAT_WAV || format == LRG_AUDIO_FILE_FORMAT_AIFF)
		return LRG_AUDIO_ENCODER_BACKEND_NATIVE;
	if (lrg_audio_libav_compiled () && codec_for (format, LRG_AUDIO_ENCODER_BACKEND_LIBAV) != NULL)
		return LRG_AUDIO_ENCODER_BACKEND_LIBAV;
	if (codec_for (format, LRG_AUDIO_ENCODER_BACKEND_FFMPEG) != NULL)
		return LRG_AUDIO_ENCODER_BACKEND_FFMPEG;
	return LRG_AUDIO_ENCODER_BACKEND_NONE;
}

/* ==========================================================================
 * Native WAV and AIFF
 * ========================================================================== */

static void
put_le (GByteArray *b,
        guint32     v,
        guint       n)
{
	guint8 bytes[4];
	guint  i;

	for (i = 0; i < n; i++)
		bytes[i] = (guint8) (v >> (8 * i));
	g_byte_array_append (b, bytes, n);
}

static void
put_be (GByteArray *b,
        guint32     v,
        guint       n)
{
	guint8 bytes[4];
	guint  i;

	for (i = 0; i < n; i++)
		bytes[i] = (guint8) (v >> (8 * (n - 1 - i)));
	g_byte_array_append (b, bytes, n);
}

/* IEEE 754 80-bit extended, for the AIFF sample rate */
static void
put_extended (GByteArray *b,
              gdouble     value)
{
	guint8  bytes[10] = { 0 };
	gint    exponent;
	gdouble mantissa = frexp (value, &exponent);
	guint64 m;
	guint   i;

	exponent += 16382;
	m = (guint64) ldexp (mantissa, 64);
	bytes[0] = (guint8) ((exponent >> 8) & 0x7F);
	bytes[1] = (guint8) (exponent & 0xFF);
	for (i = 0; i < 8; i++)
		bytes[2 + i] = (guint8) (m >> (56 - 8 * i));
	g_byte_array_append (b, bytes, 10);
}

/* one output sample as an integer of @bits, with TPDF dither when asked */
static gint32
quantize (gdouble  x,
          guint    bits,
          gboolean dither,
          guint64 *rng)
{
	gdouble scale = (gdouble) ((1u << (bits - 1)) - 1);
	gdouble v = CLAMP (x, -1.0, 1.0) * scale;

	if (dither)
		v += lrg_dsp_rng_unit (rng) - lrg_dsp_rng_unit (rng);   /* +-1 LSB triangular */
	return (gint32) CLAMP (llround (v), -(gint64) scale - 1, (gint64) scale);
}

static gboolean
write_native (const gfloat                *samples,
              gsize                        frames,
              guint                        channels,
              guint                        rate,
              LrgAudioFileFormat           format,
              guint                        bits,
              gboolean                     dither,
              const gchar                 *path,
              GError                     **error)
{
	g_autoptr(GByteArray) b = g_byte_array_new ();
	gboolean              is_float = format == LRG_AUDIO_FILE_FORMAT_WAV && bits == 32;
	guint                 bytes = bits / 8;
	gsize                 data_size = frames * channels * bytes;
	guint64               rng = lrg_dsp_rng_seed (0xD17E);
	gsize                 i;

	if (data_size > G_MAXUINT32 - 64)
	{
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "the audio is too long for WAV/AIFF");
		return FALSE;
	}
	if (format == LRG_AUDIO_FILE_FORMAT_WAV)
	{
		g_byte_array_append (b, (const guint8 *) "RIFF", 4);
		put_le (b, (guint32) (36 + data_size), 4);
		g_byte_array_append (b, (const guint8 *) "WAVEfmt ", 8);
		put_le (b, 16, 4);
		put_le (b, is_float ? 3 : 1, 2);          /* IEEE float or PCM */
		put_le (b, channels, 2);
		put_le (b, rate, 4);
		put_le (b, rate * channels * bytes, 4);
		put_le (b, channels * bytes, 2);
		put_le (b, bits, 2);
		g_byte_array_append (b, (const guint8 *) "data", 4);
		put_le (b, (guint32) data_size, 4);
		for (i = 0; i < frames * channels; i++)
		{
			if (is_float)
			{
				union { gfloat f; guint32 u; } cv;

				cv.f = isfinite (samples[i]) ? CLAMP (samples[i], -1.0f, 1.0f) : 0.0f;
				put_le (b, cv.u, 4);
			}
			else
				put_le (b, (guint32) quantize (samples[i], bits, dither && bits == 16, &rng), bytes);
		}
	}
	else
	{
		g_byte_array_append (b, (const guint8 *) "FORM", 4);
		put_be (b, (guint32) (4 + 26 + 16 + data_size), 4);
		g_byte_array_append (b, (const guint8 *) "AIFFCOMM", 8);
		put_be (b, 18, 4);
		put_be (b, channels, 2);
		put_be (b, (guint32) frames, 4);
		put_be (b, bits, 2);
		put_extended (b, (gdouble) rate);
		g_byte_array_append (b, (const guint8 *) "SSND", 4);
		put_be (b, (guint32) (8 + data_size), 4);
		put_be (b, 0, 4);
		put_be (b, 0, 4);
		for (i = 0; i < frames * channels; i++)
			put_be (b, (guint32) quantize (samples[i], bits, dither && bits == 16, &rng), bytes);
	}
	return g_file_set_contents (path, (const gchar *) b->data, (gssize) b->len, error);
}

/* ==========================================================================
 * The `ffmpeg` program
 * ========================================================================== */

static gboolean
encode_cli (const gfloat              *samples,
            gsize                      frames,
            guint                      channels,
            guint                      rate,
            const LrgAudioEncodePlan  *plan,
            const gchar               *path,
            GCancellable              *cancellable,
            GError                   **error)
{
	g_autofree gchar               *ffmpeg = g_find_program_in_path ("ffmpeg");
	g_autoptr(GPtrArray)            argv = g_ptr_array_new_with_free_func (g_free);
	g_autoptr(GSubprocessLauncher)  launcher = NULL;
	g_autoptr(GSubprocess)          proc = NULL;
	g_autoptr(GBytes)               input = NULL;
	g_autoptr(GBytes)               err_out = NULL;
	g_autofree gfloat              *clean = NULL;
	gsize                           i;

	if (ffmpeg == NULL)
	{
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED, "the ffmpeg program is not installed");
		return FALSE;
	}

#define ARG(s) g_ptr_array_add (argv, g_strdup (s))
#define ARGF(...) g_ptr_array_add (argv, g_strdup_printf (__VA_ARGS__))
	ARG (ffmpeg);
	ARG ("-hide_banner");
	ARG ("-nostdin");
	ARG ("-loglevel");
	ARG ("error");
	ARG ("-y");
	ARG ("-f");
	ARG ("f32le");
	ARGF ("%u", rate);
	g_ptr_array_insert (argv, argv->len - 1, g_strdup ("-ar"));
	ARG ("-ac");
	ARGF ("%u", channels);
	ARG ("-i");
	ARG ("pipe:0");
	ARG ("-ar");
	ARGF ("%u", plan->rate);
	ARG ("-ac");
	ARGF ("%u", plan->channels);
	ARG ("-c:a");
	ARG (plan->codec);
	if (g_str_equal (plan->codec, "vorbis") || g_str_equal (plan->codec, "opus"))
	{
		ARG ("-strict");
		ARG ("-2");
	}
	if (g_str_equal (plan->codec, "flac") || g_str_equal (plan->codec, "alac") || g_str_equal (plan->codec, "wavpack"))
	{
		ARG ("-sample_fmt");
		if (g_str_equal (plan->codec, "flac"))
			ARG (plan->bit_depth >= 24 ? "s32" : "s16");
		else
			ARG (plan->bit_depth >= 24 ? "s32p" : "s16p");
		if (plan->bit_depth >= 24 && !g_str_equal (plan->codec, "alac"))
		{
			ARG ("-bits_per_raw_sample");
			ARG ("24");
		}
	}
	else if (plan->quality >= 0.0 && (g_str_equal (plan->codec, "libmp3lame") || g_str_has_suffix (plan->codec, "vorbis")))
	{
		/* VBR: LAME's 0 is best, Vorbis' 10 is best */
		ARG ("-q:a");
		if (g_str_equal (plan->codec, "libmp3lame"))
			ARGF ("%d", (gint) CLAMP (round (9.0 - plan->quality * 0.9), 0, 9));
		else
			ARGF ("%g", CLAMP (plan->quality, 0.0, 10.0));
	}
	else
	{
		ARG ("-b:a");
		ARGF ("%uk", plan->bitrate_kbps);
	}
#define META(key, value) G_STMT_START { if ((value) != NULL) { ARG ("-metadata"); ARGF ("%s=%s", key, value); } } G_STMT_END
	META ("title", plan->title);
	META ("artist", plan->artist);
	META ("album", plan->album);
	META ("comment", plan->comment);
	META ("date", plan->date);
#undef META
	ARG ("-f");
	ARG (plan->container);
	ARG (path);
	g_ptr_array_add (argv, NULL);
#undef ARG
#undef ARGF

	/* the samples, clamped and finite, as raw little-endian float */
	clean = g_new (gfloat, frames * channels);
	for (i = 0; i < frames * channels; i++)
		clean[i] = isfinite (samples[i]) ? CLAMP (samples[i], -1.0f, 1.0f) : 0.0f;
	if (G_BYTE_ORDER == G_BIG_ENDIAN)
		for (i = 0; i < frames * channels; i++)
		{
			union { gfloat f; guint32 u; } cv;

			cv.f = clean[i];
			cv.u = GUINT32_SWAP_LE_BE (cv.u);
			clean[i] = cv.f;
		}
	input = g_bytes_new_take (g_steal_pointer (&clean), frames * channels * sizeof (gfloat));

	launcher = g_subprocess_launcher_new (G_SUBPROCESS_FLAGS_STDIN_PIPE | G_SUBPROCESS_FLAGS_STDOUT_SILENCE
	                                      | G_SUBPROCESS_FLAGS_STDERR_PIPE);
	proc = g_subprocess_launcher_spawnv (launcher, (const gchar * const *) argv->pdata, error);
	if (proc == NULL)
		return FALSE;
	if (!g_subprocess_communicate (proc, input, cancellable, NULL, &err_out, error))
	{
		g_subprocess_force_exit (proc);
		return FALSE;
	}
	if (!g_subprocess_get_successful (proc))
	{
		gsize             n = 0;
		const gchar      *text = err_out != NULL ? g_bytes_get_data (err_out, &n) : NULL;
		g_autofree gchar *msg = text != NULL ? g_strndup (text, n) : g_strdup ("");

		g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED, "ffmpeg could not write %s: %s", path, g_strstrip (msg));
		return FALSE;
	}
	return TRUE;
}

/* ==========================================================================
 * Encoding
 * ========================================================================== */

gboolean
lrg_audio_encode_samples (const gfloat                *samples,
                          gsize                        n_samples,
                          guint                        channels,
                          guint                        sample_rate,
                          LrgAudioFileFormat           format,
                          const LrgAudioEncodeOptions *options,
                          const gchar                 *path,
                          GCancellable                *cancellable,
                          GError                     **error)
{
	g_autoptr(LrgAudioEncodeOptions) defaults = NULL;
	LrgAudioEncoderBackend           backend;
	LrgAudioEncodePlan               plan;
	gsize                            frames;
	guint                            bits;

	g_return_val_if_fail (samples != NULL || n_samples == 0, FALSE);
	g_return_val_if_fail (path != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	if (options == NULL)
		options = defaults = lrg_audio_encode_options_new ();
	if ((guint) format >= N_FORMATS || channels == 0 || channels > 2 || sample_rate < 8000 || sample_rate > 384000
	    || n_samples % channels != 0)
	{
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "bad audio layout or format");
		return FALSE;
	}
	frames = n_samples / channels;
	bits = options->bit_depth != 0 ? options->bit_depth : 16;
	if (bits != 16 && bits != 24 && bits != 32)
		bits = 16;

	backend = lrg_audio_encoder_get_backend (format);
	if (backend == LRG_AUDIO_ENCODER_BACKEND_NONE)
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
		             "no encoder for %s here: install %s, or build libregnum with FFMPEG=1",
		             formats[format].ext, formats[format].package);
		return FALSE;
	}
	if (backend == LRG_AUDIO_ENCODER_BACKEND_NATIVE)
	{
		/* resampling and channel changes are done by the caller's mixer;
		 * native writing keeps the layout */
		if (format == LRG_AUDIO_FILE_FORMAT_AIFF && bits == 32)
			bits = 24;
		return write_native (samples, frames, channels, sample_rate, format, bits, options->dither, path, error);
	}

	memset (&plan, 0, sizeof plan);
	plan.codec = codec_for (format, backend);
	plan.container = formats[format].container;
	plan.rate = options->sample_rate != 0 ? options->sample_rate : sample_rate;
	plan.channels = options->channels != 0 ? MIN (options->channels, 2u) : channels;
	plan.bit_depth = bits >= 24 ? 24 : 16;
	plan.bitrate_kbps = options->bitrate_kbps != 0 ? options->bitrate_kbps
	                    : format == LRG_AUDIO_FILE_FORMAT_OPUS ? 128 : 192;
	plan.quality = options->quality;
	plan.title = options->title;
	plan.artist = options->artist;
	plan.album = options->album;
	plan.comment = options->comment;
	plan.date = options->date;
	if (format == LRG_AUDIO_FILE_FORMAT_OPUS)
		plan.rate = 48000;
	if (format == LRG_AUDIO_FILE_FORMAT_MP3 && plan.rate > 48000)
		plan.rate = 48000;
	if (backend == LRG_AUDIO_ENCODER_BACKEND_LIBAV)
		return lrg_audio_libav_encode (samples, frames, channels, sample_rate, &plan, path, cancellable, error);
	return encode_cli (samples, frames, channels, sample_rate, &plan, path, cancellable, error);
}

gboolean
lrg_audio_encode_wave (LrgWaveData                 *wave,
                       LrgAudioFileFormat           format,
                       const LrgAudioEncodeOptions *options,
                       const gchar                 *path,
                       GCancellable                *cancellable,
                       GError                     **error)
{
	g_autofree gfloat *samples = NULL;
	gsize              count = 0;
	guint              channels;

	g_return_val_if_fail (LRG_IS_WAVE_DATA (wave), FALSE);

	channels = lrg_wave_data_get_channels (wave);
	if (channels > 2)
	{
		g_autoptr(LrgWaveData) stereo = lrg_wave_data_convert (wave, lrg_wave_data_get_sample_rate (wave), 32, 2);

		if (stereo == NULL)
		{
			g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "cannot mix the wave down");
			return FALSE;
		}
		return lrg_audio_encode_wave (stereo, format, options, path, cancellable, error);
	}
	samples = lrg_wave_data_get_samples (wave, &count);
	if (samples == NULL)
	{
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "the wave is empty");
		return FALSE;
	}
	return lrg_audio_encode_samples (samples, count, channels, lrg_wave_data_get_sample_rate (wave), format,
	                                 options, path, cancellable, error);
}

/* ==========================================================================
 * Decoding
 * ========================================================================== */

/* rate and channels of the first audio stream, from ffprobe */
static gboolean
probe_cli (const gchar  *path,
           guint        *rate,
           guint        *channels,
           GError      **error)
{
	g_autofree gchar *ffprobe = g_find_program_in_path ("ffprobe");
	g_autofree gchar *out = NULL;
	g_autofree gchar *err = NULL;
	gint              status = 0;
	const gchar      *argv[] = { NULL, "-v", "error", "-select_streams", "a:0", "-show_entries",
	                             "stream=sample_rate,channels", "-of", "csv=p=0", NULL, NULL };

	if (ffprobe == NULL)
	{
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED, "ffprobe is not installed");
		return FALSE;
	}
	argv[0] = ffprobe;
	argv[9] = path;
	if (!g_spawn_sync (NULL, (gchar **) argv, NULL, G_SPAWN_DEFAULT, NULL, NULL, &out, &err, &status, error))
		return FALSE;
	if (!g_spawn_check_wait_status (status, NULL) || out == NULL || sscanf (out, "%u,%u", rate, channels) != 2
	    || *rate < 8000 || *channels == 0)
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "%s has no audio stream ffmpeg can read", path);
		return FALSE;
	}
	return TRUE;
}

static LrgWaveData *
decode_cli (const gchar  *path,
            GError      **error)
{
	g_autofree gchar               *ffmpeg = g_find_program_in_path ("ffmpeg");
	g_autoptr(GSubprocessLauncher)  launcher = NULL;
	g_autoptr(GSubprocess)          proc = NULL;
	g_autoptr(GBytes)               out = NULL;
	g_autofree gchar               *ac = NULL;
	guint                           rate = 0, channels = 0;
	gsize                           size = 0;
	const guint8                   *data;

	if (ffmpeg == NULL)
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
		             "%s: this format needs ffmpeg (not installed) or a libregnum built with FFMPEG=1", path);
		return NULL;
	}
	if (!probe_cli (path, &rate, &channels, error))
		return NULL;
	channels = MIN (channels, 2u);
	ac = g_strdup_printf ("%u", channels);
	launcher = g_subprocess_launcher_new (G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_SILENCE);
	proc = g_subprocess_launcher_spawn (launcher, error, ffmpeg, "-hide_banner", "-nostdin", "-loglevel", "error",
	                                    "-i", path, "-vn", "-ac", ac, "-f", "f32le", "pipe:1", NULL);
	if (proc == NULL || !g_subprocess_communicate (proc, NULL, NULL, &out, NULL, error))
		return NULL;
	if (!g_subprocess_get_successful (proc) || out == NULL)
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "ffmpeg could not decode %s", path);
		return NULL;
	}
	data = g_bytes_get_data (out, &size);
	if (size < sizeof (gfloat) * channels)
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "%s has no samples", path);
		return NULL;
	}
	return lrg_wave_data_new_from_samples (rate, 32, channels, data, size - size % (sizeof (gfloat) * channels));
}

LrgWaveData *
lrg_audio_decode_file (const gchar  *path,
                       GError      **error)
{
	g_autofree gchar *lower = NULL;

	g_return_val_if_fail (path != NULL, NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	if (!g_file_test (path, G_FILE_TEST_IS_REGULAR))
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "%s: no such file", path);
		return NULL;
	}
	lower = g_ascii_strdown (path, -1);
	if (g_str_has_suffix (lower, ".wav") || g_str_has_suffix (lower, ".ogg") || g_str_has_suffix (lower, ".mp3")
	    || g_str_has_suffix (lower, ".qoa"))
	{
		g_autoptr(GError)      local = NULL;
		g_autoptr(LrgWaveData) wave = lrg_wave_data_new_from_file (path, &local);

		if (wave != NULL && lrg_wave_data_is_valid (wave) && lrg_wave_data_get_frame_count (wave) > 0)
		{
			guint ch = MIN (lrg_wave_data_get_channels (wave), 2u);

			if (lrg_wave_data_get_sample_size (wave) == 32 && lrg_wave_data_get_channels (wave) == ch)
				return g_steal_pointer (&wave);
			return lrg_wave_data_convert (wave, lrg_wave_data_get_sample_rate (wave), 32, ch);
		}
		/* raylib could not: fall through to FFmpeg (an unusual WAV codec) */
	}
	if (lrg_audio_libav_compiled ())
		return lrg_audio_libav_decode (path, error);
	return decode_cli (path, error);
}
