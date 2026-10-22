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
 * seeks.  When the file also carries an audio stream it decodes that too
 * (opt-in, see lrg_video_decoder_set_audio_enabled()) into a FIFO of
 * interleaved signed 16-bit PCM at the stream's native rate.
 *
 * Packets are demuxed once for both streams: a video packet met while audio
 * is being pulled waits in a bounded queue that the video path drains before
 * it reads the file again, and an audio packet met while video is being
 * decoded goes straight into the PCM FIFO (bounded too, oldest audio
 * dropped first).
 *
 * It performs no threading and touches no GObject signals, so it is
 * deterministic and safe to call from any single thread (the player drives
 * it from the engine/main thread; the reel video source can use it for
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
/* TRUE if the file has an audio stream whose decoder opened (audio is best
 * effort: a missing or broken audio codec never fails open()). */
gboolean lrg_video_decoder_has_audio (LrgVideoDecoder *self);
/* Native sample rate of the audio stream in Hz (0 without audio). */
guint    lrg_video_decoder_get_audio_sample_rate (LrgVideoDecoder *self);
/* Output channels: 1 for mono, 2 for anything else (more than two channels
 * are folded down to stereo).  0 without audio. */
guint    lrg_video_decoder_get_audio_channels (LrgVideoDecoder *self);

/* Enable or disable audio decoding.  While disabled (the default) audio
 * packets are dropped as they are demuxed, so a caller that never reads
 * audio pays nothing.  Disabling clears the PCM FIFO. */
void     lrg_video_decoder_set_audio_enabled (LrgVideoDecoder *self,
                                              gboolean         enabled);
gboolean lrg_video_decoder_get_audio_enabled (LrgVideoDecoder *self);

/* Demux until at least MIN_FRAMES PCM frames are buffered, the file ends,
 * or the video packet queue is full.  Returns the frames now buffered
 * (0 when audio is disabled or unavailable). */
guint    lrg_video_decoder_fill_audio (LrgVideoDecoder *self,
                                       guint            min_frames);

/* Presentation time in seconds of the next buffered PCM frame.  Returns
 * FALSE (and leaves *OUT_TIME alone) when nothing is buffered. */
gboolean lrg_video_decoder_get_audio_time (LrgVideoDecoder *self,
                                           gdouble         *out_time);

/* Take up to MAX_FRAMES frames from the PCM FIFO (demuxing more when it
 * holds fewer) into OUT, which holds MAX_FRAMES * channels samples.  OUT may
 * be NULL to discard the frames.  Returns the frames taken. */
guint    lrg_video_decoder_read_audio (LrgVideoDecoder *self,
                                       gint16          *out,
                                       guint            max_frames);

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
 * POSITION seconds (decoding forward from the preceding keyframe).  The
 * audio decoder is flushed, the PCM FIFO and the video packet queue are
 * cleared, and audio before POSITION is dropped, so audio resumes at the
 * target too.  Returns FALSE / sets ERROR on failure. */
gboolean lrg_video_decoder_seek (LrgVideoDecoder  *self,
                                 gdouble           position,
                                 GError          **error);

G_END_DECLS

#endif /* LRG_HAS_FFMPEG */
