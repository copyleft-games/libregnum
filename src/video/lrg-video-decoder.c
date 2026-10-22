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
#include <libavutil/channel_layout.h>
#include <libavutil/samplefmt.h>
#include <libswscale/swscale.h>

#include <math.h>
#include <string.h>

/* Video packets demuxed ahead while audio is pulled.  An interleaved file
 * keeps audio and video within a fraction of a second of each other, so this
 * is far more than needed; it only stops a pathological file (all audio
 * first) from growing memory without limit. */
#define LRG_VIDEO_DECODER_MAX_VIDEO_QUEUE (512)

/* Upper bound on buffered PCM, in seconds.  Audio decoded by the video path
 * while the host does not pull is dropped oldest first beyond this. */
#define LRG_VIDEO_DECODER_MAX_PCM_SECONDS (4)

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

    /* Demuxing shared by both streams. */
    GQueue           video_queue; /* AVPacket *, demuxed ahead of the video path */
    gboolean         demux_eof;   /* av_read_frame() reported end/error       */

    /* Audio (actx is NULL when the file has no decodable audio). */
    int              astream;     /* audio stream index, -1 without audio     */
    AVCodecContext  *actx;
    AVFrame         *aframe;
    AVRational       atime_base;
    guint            a_rate;      /* native sample rate (Hz)                  */
    guint            a_channels;  /* output channels: 1 or 2                  */
    gboolean         audio_enabled;
    gboolean         audio_drained; /* the NULL flush packet has been sent    */
    gdouble          audio_floor; /* audio before this time is dropped (seek) */

    /* PCM FIFO: interleaved s16, a_channels per frame.  Frames
     * [pcm_head, pcm_head + pcm_len) are buffered; pcm_time is the time of
     * the frame at pcm_head (valid only while pcm_len > 0). */
    gint16          *pcm;
    gsize            pcm_head;
    gsize            pcm_len;
    gsize            pcm_cap;
    gdouble          pcm_time;

    /* Conversion scratch for one decoded audio frame. */
    gint16          *conv;
    gsize            conv_cap;    /* in frames */
};

G_DEFINE_QUARK (lrg-video-decoder-error-quark, lrg_video_decoder_error)

LrgVideoDecoder *
lrg_video_decoder_new (void)
{
    LrgVideoDecoder *self = g_new0 (LrgVideoDecoder, 1);
    self->vstream = -1;
    self->astream = -1;
    g_queue_init (&self->video_queue);
    self->audio_floor = -G_MAXDOUBLE;
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

    self->demux_eof = FALSE;
    self->astream = -1;
    self->actx = NULL;
    self->aframe = NULL;
    self->a_rate = 0;
    self->a_channels = 0;
    self->audio_drained = FALSE;
    self->audio_floor = -G_MAXDOUBLE;
    self->pcm_head = 0;
    self->pcm_len = 0;
    self->pcm_cap = 0;
    self->pcm_time = 0.0;
    self->conv_cap = 0;
    g_clear_pointer (&self->pcm, g_free);
    g_clear_pointer (&self->conv, g_free);
    /* audio_enabled is a caller setting: it survives close/open. */
}

/* Free every queued video packet. */
static void
decoder_clear_video_queue (LrgVideoDecoder *self)
{
    AVPacket *pkt;

    while ((pkt = g_queue_pop_head (&self->video_queue)) != NULL)
        av_packet_free (&pkt);
}

/* Empty the PCM FIFO (keeps its storage). */
static void
decoder_clear_pcm (LrgVideoDecoder *self)
{
    self->pcm_head = 0;
    self->pcm_len = 0;
    self->pcm_time = 0.0;
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
    if (self->aframe != NULL)
        av_frame_free (&self->aframe);
    if (self->actx != NULL)
        avcodec_free_context (&self->actx);
    decoder_clear_video_queue (self);
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

/* Open the decoder for audio stream INDEX.  On any failure the decoder is
 * left without audio (actx NULL); open() never fails because of audio. */
static void
decoder_open_audio (LrgVideoDecoder *self,
                    int              index)
{
    AVStream *stream = self->fmt->streams[index];
    const AVCodec *codec;
    AVCodecContext *ctx;
    int channels;

    codec = avcodec_find_decoder (stream->codecpar->codec_id);
    if (codec == NULL)
        return;

    ctx = avcodec_alloc_context3 (codec);
    if (ctx == NULL)
        return;

    if (avcodec_parameters_to_context (ctx, stream->codecpar) < 0 ||
        avcodec_open2 (ctx, codec, NULL) < 0)
    {
        avcodec_free_context (&ctx);
        return;
    }

    channels = ctx->ch_layout.nb_channels;
    if (ctx->sample_rate <= 0 || channels <= 0)
    {
        avcodec_free_context (&ctx);
        return;
    }

    self->aframe = av_frame_alloc ();
    if (self->aframe == NULL)
    {
        avcodec_free_context (&ctx);
        return;
    }

    self->actx = ctx;
    self->astream = index;
    self->atime_base = stream->time_base;
    self->a_rate = (guint) ctx->sample_rate;
    self->a_channels = (channels == 1) ? 1 : 2;
    self->has_audio = TRUE;
    self->audio_drained = FALSE;
    self->audio_floor = -G_MAXDOUBLE;
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

    astream = av_find_best_stream (self->fmt, AVMEDIA_TYPE_AUDIO, -1,
                                   self->vstream, NULL, 0);

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

    /* Audio is best effort: any failure here just leaves the file silent. */
    if (astream >= 0)
        decoder_open_audio (self, astream);

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

guint
lrg_video_decoder_get_audio_sample_rate (LrgVideoDecoder *self)
{
    g_return_val_if_fail (self != NULL, 0);
    return self->has_audio ? self->a_rate : 0;
}

guint
lrg_video_decoder_get_audio_channels (LrgVideoDecoder *self)
{
    g_return_val_if_fail (self != NULL, 0);
    return self->has_audio ? self->a_channels : 0;
}

void
lrg_video_decoder_set_audio_enabled (LrgVideoDecoder *self,
                                     gboolean         enabled)
{
    g_return_if_fail (self != NULL);

    enabled = !!enabled;
    if (self->audio_enabled == enabled)
        return;
    self->audio_enabled = enabled;

    /* Buffered audio belongs to the stretch that was enabled; drop it so a
     * later enable starts from the current demux position. */
    if (!enabled)
        decoder_clear_pcm (self);
}

gboolean
lrg_video_decoder_get_audio_enabled (LrgVideoDecoder *self)
{
    g_return_val_if_fail (self != NULL, FALSE);
    return self->audio_enabled;
}

/* Read sample I of channel C from FRAME as a double in [-1, 1] (floats are
 * passed through and clamped later).  Handles every libav sample format,
 * packed and planar. */
static gdouble
decoder_sample_at (const AVFrame *frame,
                   gint           channels,
                   gint           c,
                   gint           i)
{
    enum AVSampleFormat fmt = (enum AVSampleFormat) frame->format;
    gboolean planar = av_sample_fmt_is_planar (fmt);
    const guint8 *base;
    gsize index;

    /* Planar formats keep one plane per channel; packed ones interleave. */
    if (planar)
    {
        base = frame->extended_data[c];
        index = (gsize) i;
    }
    else
    {
        base = frame->extended_data[0];
        index = (gsize) i * (gsize) channels + (gsize) c;
    }

    switch (av_get_packed_sample_fmt (fmt))
    {
        case AV_SAMPLE_FMT_U8:
            return ((gdouble) base[index] - 128.0) / 128.0;
        case AV_SAMPLE_FMT_S16:
            return (gdouble) ((const gint16 *) base)[index] / 32768.0;
        case AV_SAMPLE_FMT_S32:
            return (gdouble) ((const gint32 *) base)[index] / 2147483648.0;
        case AV_SAMPLE_FMT_S64:
            return (gdouble) ((const gint64 *) base)[index] /
                   9223372036854775808.0;
        case AV_SAMPLE_FMT_FLT:
            return (gdouble) ((const gfloat *) base)[index];
        case AV_SAMPLE_FMT_DBL:
            return ((const gdouble *) base)[index];
        default:
            return 0.0;
    }
}

/* Convert a [-1, 1] sample to s16, clamping out-of-range floats. */
static gint16
decoder_to_s16 (gdouble v)
{
    gdouble s;

    if (isnan (v))
        return 0;
    s = floor (v * 32768.0 + 0.5);
    if (s > 32767.0)
        s = 32767.0;
    if (s < -32768.0)
        s = -32768.0;
    return (gint16) s;
}

/* Weight of input channel C (of CHANNELS, in LAYOUT) on the left and right
 * outputs when folding more than two channels down to stereo: left-side
 * channels go left, right-side ones right, centre/unknown ones to both at
 * -3 dB, LFE is dropped. */
static void
decoder_fold_weights (const AVChannelLayout *layout,
                      gint                   c,
                      gdouble               *wl,
                      gdouble               *wr)
{
    enum AVChannel ch = av_channel_layout_channel_from_index (layout,
                                                              (unsigned) c);

    switch (ch)
    {
        case AV_CHAN_FRONT_LEFT:
            *wl = 1.0; *wr = 0.0;
            return;
        case AV_CHAN_FRONT_RIGHT:
            *wl = 0.0; *wr = 1.0;
            return;
        case AV_CHAN_LOW_FREQUENCY:
        case AV_CHAN_LOW_FREQUENCY_2:
            *wl = 0.0; *wr = 0.0;
            return;
        case AV_CHAN_BACK_LEFT:
        case AV_CHAN_SIDE_LEFT:
        case AV_CHAN_FRONT_LEFT_OF_CENTER:
        case AV_CHAN_TOP_FRONT_LEFT:
        case AV_CHAN_TOP_BACK_LEFT:
        case AV_CHAN_WIDE_LEFT:
        case AV_CHAN_STEREO_LEFT:
            *wl = M_SQRT1_2; *wr = 0.0;
            return;
        case AV_CHAN_BACK_RIGHT:
        case AV_CHAN_SIDE_RIGHT:
        case AV_CHAN_FRONT_RIGHT_OF_CENTER:
        case AV_CHAN_TOP_FRONT_RIGHT:
        case AV_CHAN_TOP_BACK_RIGHT:
        case AV_CHAN_WIDE_RIGHT:
        case AV_CHAN_STEREO_RIGHT:
            *wl = 0.0; *wr = M_SQRT1_2;
            return;
        default:
            break;
    }

    /* Unordered/unknown layouts: the first two channels are left and right,
     * everything else is spread to both. */
    if (layout->order != AV_CHANNEL_ORDER_NATIVE && c < 2)
    {
        *wl = (c == 0) ? 1.0 : 0.0;
        *wr = (c == 1) ? 1.0 : 0.0;
        return;
    }
    *wl = M_SQRT1_2;
    *wr = M_SQRT1_2;
}

/* Convert the decoded audio frame into self->conv (interleaved s16 with
 * a_channels channels).  Mono stays mono, stereo stays stereo, more than two
 * channels fold down to stereo; a stream whose channel count changes
 * mid-file is mapped onto the channel count chosen at open.  Returns the
 * number of frames converted. */
static gsize
decoder_convert_audio (LrgVideoDecoder *self)
{
    const AVFrame *frame = self->aframe;
    gint in_ch = frame->ch_layout.nb_channels;
    gint n = frame->nb_samples;
    gsize out_ch = self->a_channels;
    gint i;
    gint c;

    if (n <= 0 || in_ch <= 0 || frame->extended_data == NULL)
        return 0;

    if (self->conv_cap < (gsize) n)
    {
        self->conv = g_renew (gint16, self->conv, (gsize) n * 2);
        self->conv_cap = (gsize) n;
    }

    for (i = 0; i < n; i++)
    {
        gint16 *dst = self->conv + (gsize) i * out_ch;

        if (in_ch == 1)
        {
            /* Mono in: one output channel, or the same sample on both. */
            dst[0] = decoder_to_s16 (decoder_sample_at (frame, 1, 0, i));
            if (out_ch == 2)
                dst[1] = dst[0];
        }
        else if (out_ch == 1)
        {
            /* Several channels in but mono out (layout changed mid-file):
             * average them. */
            gdouble sum = 0.0;

            for (c = 0; c < in_ch; c++)
                sum += decoder_sample_at (frame, in_ch, c, i);
            dst[0] = decoder_to_s16 (sum / (gdouble) in_ch);
        }
        else if (in_ch == 2)
        {
            dst[0] = decoder_to_s16 (decoder_sample_at (frame, 2, 0, i));
            dst[1] = decoder_to_s16 (decoder_sample_at (frame, 2, 1, i));
        }
        else
        {
            gdouble l = 0.0;
            gdouble r = 0.0;
            gdouble sum_l = 0.0;
            gdouble sum_r = 0.0;

            for (c = 0; c < in_ch; c++)
            {
                gdouble wl;
                gdouble wr;
                gdouble v = decoder_sample_at (frame, in_ch, c, i);

                decoder_fold_weights (&frame->ch_layout, c, &wl, &wr);
                l += v * wl;
                r += v * wr;
                sum_l += wl;
                sum_r += wr;
            }
            /* Normalise by the weights so the fold never clips a signal
             * that did not clip on its own channels. */
            dst[0] = decoder_to_s16 (sum_l > 0.0 ? l / sum_l : 0.0);
            dst[1] = decoder_to_s16 (sum_r > 0.0 ? r / sum_r : 0.0);
        }
    }

    return (gsize) n;
}

/* Append N frames of self->conv starting at time T (seconds; NAN when the
 * frame had no timestamp) to the PCM FIFO.  Audio before audio_floor (a seek
 * target) is dropped; beyond LRG_VIDEO_DECODER_MAX_PCM_SECONDS the oldest
 * buffered audio is dropped. */
static void
decoder_push_pcm (LrgVideoDecoder *self,
                  gsize            n,
                  gdouble          t)
{
    const gint16 *src = self->conv;
    gsize ch = self->a_channels;
    gsize max_frames;
    gsize skip;

    /* A frame without a timestamp continues the buffered audio. */
    if (isnan (t))
        t = (self->pcm_len > 0)
            ? self->pcm_time + (gdouble) self->pcm_len / (gdouble) self->a_rate
            : 0.0;

    /* Drop what lies before the seek target. */
    if (t < self->audio_floor)
    {
        skip = (gsize) ((self->audio_floor - t) * (gdouble) self->a_rate + 0.5);
        if (skip >= n)
            return;
        src += skip * ch;
        n -= skip;
        t += (gdouble) skip / (gdouble) self->a_rate;
    }

    /* The buffered audio is assumed contiguous: a new frame only sets the
     * FIFO's clock when the FIFO is empty. */
    if (self->pcm_len == 0)
    {
        self->pcm_head = 0;
        self->pcm_time = t;
    }

    max_frames = (gsize) self->a_rate * LRG_VIDEO_DECODER_MAX_PCM_SECONDS;
    if (n > max_frames)
    {
        /* A single frame larger than the whole budget: keep its tail. */
        skip = n - max_frames;
        decoder_clear_pcm (self);
        src += skip * ch;
        n = max_frames;
        self->pcm_time = t + (gdouble) skip / (gdouble) self->a_rate;
    }
    if (self->pcm_len + n > max_frames)
    {
        /* Over budget: drop the oldest buffered frames. */
        skip = self->pcm_len + n - max_frames;
        self->pcm_head += skip;
        self->pcm_len -= skip;
        self->pcm_time += (gdouble) skip / (gdouble) self->a_rate;
        if (self->pcm_len == 0)
            self->pcm_time = t;
    }

    /* Make room at the tail: compact first, then grow. */
    if (self->pcm_head + self->pcm_len + n > self->pcm_cap)
    {
        if (self->pcm_head > 0)
        {
            memmove (self->pcm, self->pcm + self->pcm_head * ch,
                     self->pcm_len * ch * sizeof (gint16));
            self->pcm_head = 0;
        }
        if (self->pcm_len + n > self->pcm_cap)
        {
            gsize cap = MAX (self->pcm_cap * 2, self->pcm_len + n);

            cap = MAX (cap, (gsize) 4096);
            cap = MIN (cap, max_frames);
            self->pcm = g_renew (gint16, self->pcm, cap * ch);
            self->pcm_cap = cap;
        }
    }

    memcpy (self->pcm + (self->pcm_head + self->pcm_len) * ch, src,
            n * ch * sizeof (gint16));
    self->pcm_len += n;
}

/* Receive every frame the audio decoder has ready into the FIFO. */
static void
decoder_receive_audio (LrgVideoDecoder *self)
{
    for (;;)
    {
        gint64 ts;
        gdouble t;
        gsize n;

        if (avcodec_receive_frame (self->actx, self->aframe) != 0)
            return;

        ts = self->aframe->best_effort_timestamp;
        if (ts == AV_NOPTS_VALUE)
            ts = self->aframe->pts;
        t = (ts == AV_NOPTS_VALUE) ? NAN
                                   : (gdouble) ts * av_q2d (self->atime_base);

        n = decoder_convert_audio (self);
        if (n > 0)
            decoder_push_pcm (self, n, t);
        av_frame_unref (self->aframe);
    }
}

/* Feed PKT (NULL to drain) to the audio decoder and collect its output.
 * Decode errors are ignored: a corrupt audio packet costs a gap, never the
 * video. */
static void
decoder_decode_audio (LrgVideoDecoder *self,
                      const AVPacket  *pkt)
{
    int ret;

    ret = avcodec_send_packet (self->actx, pkt);
    if (ret == AVERROR (EAGAIN))
    {
        /* The decoder is full: empty it, then the packet fits. */
        decoder_receive_audio (self);
        ret = avcodec_send_packet (self->actx, pkt);
    }
    (void) ret;
    decoder_receive_audio (self);
}

/* Read one packet from the file and route it: video packets are queued for
 * the video path, audio packets decoded into the FIFO while audio is
 * enabled, anything else dropped.  Returns FALSE at end of file (the audio
 * decoder is drained then). */
static gboolean
decoder_demux (LrgVideoDecoder *self)
{
    if (self->demux_eof)
        return FALSE;

    if (av_read_frame (self->fmt, self->pkt) < 0)
    {
        /* End of file or a read error: no more packets either way. */
        self->demux_eof = TRUE;
        if (self->actx != NULL && self->audio_enabled && !self->audio_drained)
        {
            decoder_decode_audio (self, NULL);
            self->audio_drained = TRUE;
        }
        return FALSE;
    }

    if (self->pkt->stream_index == self->vstream)
    {
        AVPacket *queued = av_packet_alloc ();

        if (queued != NULL)
        {
            av_packet_move_ref (queued, self->pkt);
            g_queue_push_tail (&self->video_queue, queued);
        }
    }
    else if (self->pkt->stream_index == self->astream &&
             self->actx != NULL && self->audio_enabled)
    {
        decoder_decode_audio (self, self->pkt);
    }
    av_packet_unref (self->pkt);
    return TRUE;
}

guint
lrg_video_decoder_fill_audio (LrgVideoDecoder *self,
                              guint            min_frames)
{
    g_return_val_if_fail (self != NULL, 0);

    if (!self->opened || self->actx == NULL || !self->audio_enabled)
        return 0;

    while (self->pcm_len < (gsize) min_frames &&
           g_queue_get_length (&self->video_queue) <
               LRG_VIDEO_DECODER_MAX_VIDEO_QUEUE)
    {
        if (!decoder_demux (self))
            break;
    }

    return (guint) MIN (self->pcm_len, (gsize) G_MAXUINT);
}

gboolean
lrg_video_decoder_get_audio_time (LrgVideoDecoder *self,
                                  gdouble         *out_time)
{
    g_return_val_if_fail (self != NULL, FALSE);

    if (self->pcm_len == 0)
        return FALSE;
    if (out_time != NULL)
        *out_time = self->pcm_time;
    return TRUE;
}

guint
lrg_video_decoder_read_audio (LrgVideoDecoder *self,
                              gint16          *out,
                              guint            max_frames)
{
    gsize n;
    gsize ch;

    g_return_val_if_fail (self != NULL, 0);

    if (max_frames == 0)
        return 0;
    if (lrg_video_decoder_fill_audio (self, max_frames) == 0)
        return 0;

    ch = self->a_channels;
    n = MIN (self->pcm_len, (gsize) max_frames);
    if (out != NULL)
        memcpy (out, self->pcm + self->pcm_head * ch,
                n * ch * sizeof (gint16));

    self->pcm_head += n;
    self->pcm_len -= n;
    self->pcm_time += (gdouble) n / (gdouble) self->a_rate;
    if (self->pcm_len == 0)
        self->pcm_head = 0;

    return (guint) n;
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
            AVPacket *vpkt = g_queue_pop_head (&self->video_queue);

            if (vpkt == NULL)
            {
                /* Nothing queued: demux (routing audio packets to the PCM
                 * FIFO on the way) until a video packet arrives. */
                if (decoder_demux (self))
                    continue;

                /* EOF or read error: send a NULL flush packet once, then
                 * keep receiving the buffered frames. */
                avcodec_send_packet (self->vctx, NULL);
                self->draining = TRUE;
                break;
            }
            ret = avcodec_send_packet (self->vctx, vpkt);
            av_packet_free (&vpkt);
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

    /* Everything demuxed or decoded before the seek is stale. */
    decoder_clear_video_queue (self);
    self->demux_eof = FALSE;
    if (self->actx != NULL)
        avcodec_flush_buffers (self->actx);
    self->audio_drained = FALSE;
    decoder_clear_pcm (self);
    /* The demuxer lands on the keyframe before POSITION: audio decoded on
     * the way to the target frame (and after) starts at POSITION. */
    self->audio_floor = position;

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
