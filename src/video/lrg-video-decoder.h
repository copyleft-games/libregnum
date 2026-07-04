/* lrg-video-decoder.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgVideoDecoder - internal FFmpeg/libav video decode helper.
 *
 * A small, SYNCHRONOUS libav wrapper that opens a media file, reports its
 * video stream info, decodes frames sequentially into an RGBA8 buffer, and
 * seeks.  It performs no threading and touches no GObject signals, so it is
 * deterministic and safe to call from any single thread (the player drives it
 * from the engine/main thread; the reel video source can use it for
 * decode-on-demand).
 *
 * This is an INTERNAL header: it is NOT part of the public <libregnum.h>
 * umbrella and is only compiled/used when libregnum is built with FFmpeg
 * support (FFMPEG=1 -> -DLRG_HAS_FFMPEG=1).  Without that macro the whole
 * module is empty.
 */

#pragma once

#ifndef LIBREGNUM_COMPILATION
#error "lrg-video-decoder.h is a libregnum-internal header."
#endif

#ifdef LRG_HAS_FFMPEG

#include <glib.h>

G_BEGIN_DECLS

/* Opaque synchronous decoder (a plain struct, not a GObject -- this is an
 * internal helper and is never introspected). */
typedef struct _LrgVideoDecoder LrgVideoDecoder;

/* Quark for decoder errors. */
#define LRG_VIDEO_DECODER_ERROR (lrg_video_decoder_error_quark ())
GQuark lrg_video_decoder_error_quark (void);

typedef enum
{
    LRG_VIDEO_DECODER_ERROR_OPEN,    /* could not open / probe the file      */
    LRG_VIDEO_DECODER_ERROR_STREAM,  /* no decodable video stream            */
    LRG_VIDEO_DECODER_ERROR_CODEC,   /* decoder unavailable / failed to open */
    LRG_VIDEO_DECODER_ERROR_DECODE,  /* a decode/scale operation failed      */
    LRG_VIDEO_DECODER_ERROR_SEEK     /* a seek operation failed              */
} LrgVideoDecoderError;

/* Create an empty (unopened) decoder. */
LrgVideoDecoder *lrg_video_decoder_new (void);

/* Free a decoder (closing any open file). */
void lrg_video_decoder_free (LrgVideoDecoder *self);

/* Open PATH.  On success fills the info getters and leaves the decoder
 * positioned before the first frame.  On failure returns FALSE and sets
 * ERROR. */
gboolean lrg_video_decoder_open (LrgVideoDecoder  *self,
                                 const char       *path,
                                 GError          **error);

/* Close the currently open file (idempotent). */
void lrg_video_decoder_close (LrgVideoDecoder *self);

gboolean lrg_video_decoder_is_open    (LrgVideoDecoder *self);
guint    lrg_video_decoder_get_width  (LrgVideoDecoder *self);
guint    lrg_video_decoder_get_height (LrgVideoDecoder *self);
/* Duration in seconds (0 if unknown). */
gdouble  lrg_video_decoder_get_duration (LrgVideoDecoder *self);
/* Average frame rate in FPS (0 if unknown). */
gdouble  lrg_video_decoder_get_frame_rate (LrgVideoDecoder *self);
/* TRUE if the container has at least one audio stream. */
gboolean lrg_video_decoder_has_audio (LrgVideoDecoder *self);

/* Decode the next video frame.
 *
 * On a decoded frame: returns TRUE, sets *OUT_RGBA to an internal RGBA8 buffer
 * of width*height*4 bytes (owned by the decoder, valid only until the next
 * next_frame/seek/close), and *OUT_PTS to the frame presentation time in
 * seconds.
 *
 * At end of stream: returns FALSE with *OUT_EOF set TRUE and no error.
 * On a real error: returns FALSE with *OUT_EOF FALSE and ERROR set.
 *
 * OUT_RGBA / OUT_PTS / OUT_EOF may each be NULL if not wanted. */
gboolean lrg_video_decoder_next_frame (LrgVideoDecoder  *self,
                                       const guint8    **out_rgba,
                                       gdouble          *out_pts,
                                       gboolean         *out_eof,
                                       GError          **error);

/* Seek so that the next next_frame() returns the first frame at or after
 * POSITION seconds (decoding forward from the preceding keyframe).  Returns
 * FALSE / sets ERROR on failure. */
gboolean lrg_video_decoder_seek (LrgVideoDecoder  *self,
                                 gdouble           position,
                                 GError          **error);

G_END_DECLS

#endif /* LRG_HAS_FFMPEG */
