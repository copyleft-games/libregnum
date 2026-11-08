/* lrg-audio-encoder-private.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * The bridge between lrg-audio-encoder.c and the in-process FFmpeg
 * backend (lrg-audio-encoder-libav.c, which compiles to stubs without
 * LRG_HAS_FFMPEG).  Not installed.
 */

#pragma once

#include <glib.h>
#include <gio/gio.h>
#include "lrg-wave-data.h"

G_BEGIN_DECLS

typedef struct
{
	const gchar *codec;        /* encoder name ("flac", "libmp3lame", ...) */
	const gchar *container;    /* muxer name ("flac", "mp3", "ogg", "ipod", ...) */
	guint        rate;         /* output rate */
	guint        channels;     /* output channels */
	guint        bit_depth;    /* lossless: 16 or 24 */
	guint        bitrate_kbps; /* lossy */
	gdouble      quality;      /* lossy VBR 0 .. 10, < 0: bitrate */
	const gchar *title, *artist, *album, *comment, *date;
} LrgAudioEncodePlan;

/* whether the library backend was compiled in */
G_GNUC_INTERNAL gboolean lrg_audio_libav_compiled (void);

/* whether libavcodec has the encoder @name */
G_GNUC_INTERNAL gboolean lrg_audio_libav_has_encoder (const gchar *name);

/* writes interleaved float @samples at @rate x @channels as @plan says */
G_GNUC_INTERNAL gboolean lrg_audio_libav_encode (const gfloat              *samples,
                                                 gsize                      frames,
                                                 guint                      channels,
                                                 guint                      rate,
                                                 const LrgAudioEncodePlan  *plan,
                                                 const gchar               *path,
                                                 GCancellable              *cancellable,
                                                 GError                   **error);

/* decodes the first audio stream as float, at most stereo */
G_GNUC_INTERNAL LrgWaveData *lrg_audio_libav_decode (const gchar  *path,
                                                     GError      **error);

G_END_DECLS
