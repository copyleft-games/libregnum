/* lrg-video-decoder.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * FFmpeg/libav implementation of the internal synchronous video decoder.
 * The entire translation unit is empty unless libregnum was built with
 * FFmpeg support (FFMPEG=1 -> -DLRG_HAS_FFMPEG=1).
 */

#include "config.h"

#ifdef LRG_HAS_FFMPEG

#include "lrg-video-decoder.h"

#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libavutil/avutil.h>
#include <libswscale/swscale.h>

struct _LrgVideoDecoder
{
    AVFormatContext *fmt;
    AVCodecContext  *vctx;
    struct SwsContext *sws;
    AVFrame         *frame;     /* decoded frame in the native pixel format */
    AVPacket        *pkt;       /* reusable packet                          */

    int              vstream;   /* video stream index, -1 when closed       */
    AVRational       time_base; /* video stream time base                   */

    guint8          *rgba;      /* RGBA8 staging buffer, width*height*4      */
    gint             rgba_linesize;

    gint             width;
    gint             height;
    gdouble          duration;  /* seconds */
    gdouble          fps;
    gboolean         has_audio;

    gboolean         opened;
    gboolean         draining;  /* a NULL flush packet has been sent        */

    /* A frame produced by seek() that the next next_frame() should return
     * verbatim (the rgba buffer already holds it). */
    gboolean         has_pending;
    gdouble          pending_pts;
};

G_DEFINE_QUARK (lrg-video-decoder-error-quark, lrg_video_decoder_error)

LrgVideoDecoder *
lrg_video_decoder_new (void)
{
    LrgVideoDecoder *self = g_new0 (LrgVideoDecoder, 1);
    self->vstream = -1;
    return self;
}

static void
decoder_reset_fields (LrgVideoDecoder *self)
{
    self->fmt = NULL;
    self->vctx = NULL;
    self->sws = NULL;
    self->frame = NULL;
    self->pkt = NULL;
    self->vstream = -1;
    self->width = 0;
    self->height = 0;
    self->duration = 0.0;
    self->fps = 0.0;
    self->has_audio = FALSE;
    self->opened = FALSE;
    self->draining = FALSE;
    self->has_pending = FALSE;
    self->pending_pts = 0.0;
    self->rgba_linesize = 0;
    g_clear_pointer (&self->rgba, g_free);
}

void
lrg_video_decoder_close (LrgVideoDecoder *self)
{
    g_return_if_fail (self != NULL);

    if (self->sws != NULL)
    {
        sws_freeContext (self->sws);
        self->sws = NULL;
    }
    if (self->frame != NULL)
        av_frame_free (&self->frame);
    if (self->pkt != NULL)
        av_packet_free (&self->pkt);
    if (self->vctx != NULL)
        avcodec_free_context (&self->vctx);
    if (self->fmt != NULL)
        avformat_close_input (&self->fmt);

    decoder_reset_fields (self);
}

void
lrg_video_decoder_free (LrgVideoDecoder *self)
{
    if (self == NULL)
        return;
    lrg_video_decoder_close (self);
    g_free (self);
}

gboolean
lrg_video_decoder_open (LrgVideoDecoder  *self,
                        const char       *path,
                        GError          **error)
{
    const AVCodec *codec = NULL;
    AVStream *stream = NULL;
    AVRational fr;
    int ret;
    int astream;

    g_return_val_if_fail (self != NULL, FALSE);
    g_return_val_if_fail (path != NULL, FALSE);

    lrg_video_decoder_close (self);

    ret = avformat_open_input (&self->fmt, path, NULL, NULL);
    if (ret < 0)
    {
        g_set_error (error, LRG_VIDEO_DECODER_ERROR,
                     LRG_VIDEO_DECODER_ERROR_OPEN,
                     "could not open '%s'", path);
        lrg_video_decoder_close (self);
        return FALSE;
    }

    ret = avformat_find_stream_info (self->fmt, NULL);
    if (ret < 0)
    {
        g_set_error (error, LRG_VIDEO_DECODER_ERROR,
                     LRG_VIDEO_DECODER_ERROR_STREAM,
                     "could not read stream info for '%s'", path);
        lrg_video_decoder_close (self);
        return FALSE;
    }

    ret = av_find_best_stream (self->fmt, AVMEDIA_TYPE_VIDEO, -1, -1,
                               &codec, 0);
    if (ret < 0 || codec == NULL)
    {
        g_set_error (error, LRG_VIDEO_DECODER_ERROR,
                     LRG_VIDEO_DECODER_ERROR_STREAM,
                     "no decodable video stream in '%s'", path);
        lrg_video_decoder_close (self);
        return FALSE;
    }
    self->vstream = ret;
    stream = self->fmt->streams[self->vstream];

    astream = av_find_best_stream (self->fmt, AVMEDIA_TYPE_AUDIO, -1, -1,
                                   NULL, 0);
    self->has_audio = (astream >= 0);

    self->vctx = avcodec_alloc_context3 (codec);
    if (self->vctx == NULL)
    {
        g_set_error (error, LRG_VIDEO_DECODER_ERROR,
                     LRG_VIDEO_DECODER_ERROR_CODEC,
                     "out of memory allocating decoder context");
        lrg_video_decoder_close (self);
        return FALSE;
    }

    ret = avcodec_parameters_to_context (self->vctx, stream->codecpar);
    if (ret < 0)
    {
        g_set_error (error, LRG_VIDEO_DECODER_ERROR,
                     LRG_VIDEO_DECODER_ERROR_CODEC,
                     "could not copy codec parameters");
        lrg_video_decoder_close (self);
        return FALSE;
    }

    ret = avcodec_open2 (self->vctx, codec, NULL);
    if (ret < 0)
    {
        g_set_error (error, LRG_VIDEO_DECODER_ERROR,
                     LRG_VIDEO_DECODER_ERROR_CODEC,
                     "could not open codec '%s'", codec->name);
        lrg_video_decoder_close (self);
        return FALSE;
    }

    self->width = self->vctx->width;
    self->height = self->vctx->height;
    self->time_base = stream->time_base;

    if (self->width <= 0 || self->height <= 0)
    {
        g_set_error (error, LRG_VIDEO_DECODER_ERROR,
                     LRG_VIDEO_DECODER_ERROR_CODEC,
                     "video stream reports invalid dimensions %dx%d",
                     self->width, self->height);
        lrg_video_decoder_close (self);
        return FALSE;
    }

    /* Duration: prefer the container, fall back to the stream. */
    if (self->fmt->duration != AV_NOPTS_VALUE && self->fmt->duration > 0)
        self->duration = (gdouble) self->fmt->duration / (gdouble) AV_TIME_BASE;
    else if (stream->duration != AV_NOPTS_VALUE && stream->duration > 0)
        self->duration = (gdouble) stream->duration * av_q2d (stream->time_base);
    else
        self->duration = 0.0;

    fr = av_guess_frame_rate (self->fmt, stream, NULL);
    self->fps = (fr.num > 0 && fr.den > 0) ? av_q2d (fr) : 0.0;

    self->frame = av_frame_alloc ();
    self->pkt = av_packet_alloc ();
    if (self->frame == NULL || self->pkt == NULL)
    {
        g_set_error (error, LRG_VIDEO_DECODER_ERROR,
                     LRG_VIDEO_DECODER_ERROR_CODEC,
                     "out of memory allocating frame/packet");
        lrg_video_decoder_close (self);
        return FALSE;
    }

    self->sws = sws_getContext (self->width, self->height, self->vctx->pix_fmt,
                                self->width, self->height, AV_PIX_FMT_RGBA,
                                SWS_BILINEAR, NULL, NULL, NULL);
    if (self->sws == NULL)
    {
        g_set_error (error, LRG_VIDEO_DECODER_ERROR,
                     LRG_VIDEO_DECODER_ERROR_CODEC,
                     "could not create RGBA conversion context");
        lrg_video_decoder_close (self);
        return FALSE;
    }

    self->rgba_linesize = self->width * 4;
    self->rgba = g_malloc0 ((gsize) self->rgba_linesize * self->height);
    self->opened = TRUE;
    self->draining = FALSE;
    self->has_pending = FALSE;

    return TRUE;
}

gboolean
lrg_video_decoder_is_open (LrgVideoDecoder *self)
{
    return self != NULL && self->opened;
}

guint
lrg_video_decoder_get_width (LrgVideoDecoder *self)
{
    g_return_val_if_fail (self != NULL, 0);
    return (guint) self->width;
}

guint
lrg_video_decoder_get_height (LrgVideoDecoder *self)
{
    g_return_val_if_fail (self != NULL, 0);
    return (guint) self->height;
}

gdouble
lrg_video_decoder_get_duration (LrgVideoDecoder *self)
{
    g_return_val_if_fail (self != NULL, 0.0);
    return self->duration;
}

gdouble
lrg_video_decoder_get_frame_rate (LrgVideoDecoder *self)
{
    g_return_val_if_fail (self != NULL, 0.0);
    return self->fps;
}

gboolean
lrg_video_decoder_has_audio (LrgVideoDecoder *self)
{
    g_return_val_if_fail (self != NULL, FALSE);
    return self->has_audio;
}

/* Convert the currently-held native frame into the RGBA staging buffer and
 * compute its presentation time in seconds. */
static gdouble
decoder_convert_current (LrgVideoDecoder *self)
{
    guint8 *dst[4];
    int dst_stride[4];
    gint64 ts;

    dst[0] = self->rgba;
    dst[1] = NULL;
    dst[2] = NULL;
    dst[3] = NULL;
    dst_stride[0] = self->rgba_linesize;
    dst_stride[1] = 0;
    dst_stride[2] = 0;
    dst_stride[3] = 0;

    sws_scale (self->sws,
               (const guint8 * const *) self->frame->data,
               self->frame->linesize, 0, self->height,
               dst, dst_stride);

    ts = self->frame->best_effort_timestamp;
    if (ts == AV_NOPTS_VALUE)
        ts = self->frame->pts;
    if (ts == AV_NOPTS_VALUE)
        return 0.0;
    return (gdouble) ts * av_q2d (self->time_base);
}

/* Decode exactly one video frame into the staging buffer.
 * Returns TRUE on a decoded frame (sets *PTS), FALSE on EOF (sets *EOF) or
 * error (sets ERROR). */
static gboolean
decoder_decode_one (LrgVideoDecoder  *self,
                    gdouble          *pts,
                    gboolean         *eof,
                    GError          **error)
{
    int ret;

    if (eof != NULL)
        *eof = FALSE;

    for (;;)
    {
        ret = avcodec_receive_frame (self->vctx, self->frame);
        if (ret == 0)
        {
            gdouble t = decoder_convert_current (self);
            av_frame_unref (self->frame);
            if (pts != NULL)
                *pts = t;
            return TRUE;
        }
        if (ret == AVERROR_EOF)
        {
            if (eof != NULL)
                *eof = TRUE;
            return FALSE;
        }
        if (ret != AVERROR (EAGAIN))
        {
            g_set_error (error, LRG_VIDEO_DECODER_ERROR,
                         LRG_VIDEO_DECODER_ERROR_DECODE,
                         "frame decode failed (%d)", ret);
            return FALSE;
        }

        /* EAGAIN: the decoder wants more input.  Feed one video packet, or
         * enter draining mode at end of file. */
        if (self->draining)
        {
            /* We already flushed; receive returning EAGAIN after a drain
             * means there is genuinely nothing left. */
            if (eof != NULL)
                *eof = TRUE;
            return FALSE;
        }

        for (;;)
        {
            ret = av_read_frame (self->fmt, self->pkt);
            if (ret < 0)
            {
                /* EOF or read error: send a NULL flush packet once, then
                 * keep receiving the buffered frames. */
                avcodec_send_packet (self->vctx, NULL);
                self->draining = TRUE;
                break;
            }
            if (self->pkt->stream_index != self->vstream)
            {
                av_packet_unref (self->pkt);
                continue;
            }
            ret = avcodec_send_packet (self->vctx, self->pkt);
            av_packet_unref (self->pkt);
            if (ret < 0 && ret != AVERROR (EAGAIN))
            {
                g_set_error (error, LRG_VIDEO_DECODER_ERROR,
                             LRG_VIDEO_DECODER_ERROR_DECODE,
                             "send packet failed (%d)", ret);
                return FALSE;
            }
            break;
        }
        /* Loop back to receive. */
    }
}

gboolean
lrg_video_decoder_next_frame (LrgVideoDecoder  *self,
                              const guint8    **out_rgba,
                              gdouble          *out_pts,
                              gboolean         *out_eof,
                              GError          **error)
{
    gdouble pts = 0.0;
    gboolean eof = FALSE;

    g_return_val_if_fail (self != NULL, FALSE);
    g_return_val_if_fail (self->opened, FALSE);

    if (out_eof != NULL)
        *out_eof = FALSE;

    /* A seek() left the target frame ready in the staging buffer. */
    if (self->has_pending)
    {
        self->has_pending = FALSE;
        if (out_rgba != NULL)
            *out_rgba = self->rgba;
        if (out_pts != NULL)
            *out_pts = self->pending_pts;
        return TRUE;
    }

    if (!decoder_decode_one (self, &pts, &eof, error))
    {
        if (out_eof != NULL)
            *out_eof = eof;
        return FALSE;
    }

    if (out_rgba != NULL)
        *out_rgba = self->rgba;
    if (out_pts != NULL)
        *out_pts = pts;
    return TRUE;
}

gboolean
lrg_video_decoder_seek (LrgVideoDecoder  *self,
                        gdouble           position,
                        GError          **error)
{
    gint64 ts;
    int ret;

    g_return_val_if_fail (self != NULL, FALSE);
    g_return_val_if_fail (self->opened, FALSE);

    if (position < 0.0)
        position = 0.0;

    ts = (gint64) (position / av_q2d (self->time_base));

    ret = av_seek_frame (self->fmt, self->vstream, ts, AVSEEK_FLAG_BACKWARD);
    if (ret < 0)
    {
        g_set_error (error, LRG_VIDEO_DECODER_ERROR,
                     LRG_VIDEO_DECODER_ERROR_SEEK,
                     "seek to %.3fs failed", position);
        return FALSE;
    }

    avcodec_flush_buffers (self->vctx);
    self->draining = FALSE;
    self->has_pending = FALSE;

    /* Decode forward from the preceding keyframe to the first frame at or
     * after the requested position; hold it as the pending frame. */
    for (;;)
    {
        gdouble pts = 0.0;
        gboolean eof = FALSE;

        if (!decoder_decode_one (self, &pts, &eof, error))
        {
            if (eof)
                return TRUE;  /* seeked at/after end: next_frame yields EOF */
            return FALSE;
        }

        /* Small epsilon so a target landing exactly on a frame boundary is
         * accepted rather than overshooting by one frame. */
        if (pts + 1e-6 >= position)
        {
            self->has_pending = TRUE;
            self->pending_pts = pts;
            return TRUE;
        }
    }
}

#else /* !LRG_HAS_FFMPEG */

/* Keep this translation unit non-empty when FFmpeg is disabled. */
typedef int lrg_video_decoder_disabled_placeholder;

#endif /* LRG_HAS_FFMPEG */
