/* lrg-audio-encoder-libav.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * The in-process FFmpeg backend of lrg-audio-encoder.c (see
 * lrg-audio-encoder-private.h).  With LRG_HAS_FFMPEG (FFMPEG=1) it
 * encodes with libavformat + libavcodec, converting the interleaved float
 * input with libswresample to whatever sample format, rate and channel
 * layout the encoder wants, in the encoder's frame size; and decodes the
 * first audio stream of a file the same way back to interleaved float.
 * Without it every function reports that the backend is absent.
 */

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_AUDIO

#include "config.h"

#include <math.h>
#include <string.h>

#include "lrg-audio-encoder-private.h"

#ifdef LRG_HAS_FFMPEG

#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/opt.h>
#include <libavutil/channel_layout.h>
#include <libavutil/samplefmt.h>
#include <libavutil/audio_fifo.h>
#include <libswresample/swresample.h>

gboolean
lrg_audio_libav_compiled (void)
{
	return TRUE;
}

gboolean
lrg_audio_libav_has_encoder (const gchar *name)
{
	return name != NULL && avcodec_find_encoder_by_name (name) != NULL;
}

static gboolean
av_fail (GError      **error,
         gint          code,
         const gchar  *what,
         const gchar  *path)
{
	gchar buf[AV_ERROR_MAX_STRING_SIZE] = { 0 };

	av_strerror (code, buf, sizeof buf);
	g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED, "%s: %s: %s", path, what, buf);
	return FALSE;
}

/* the encoder's preferred sample format for @bit_depth (lossless) */
static enum AVSampleFormat
pick_format (AVCodecContext *ctx,
             const AVCodec  *codec,
             guint           bit_depth)
{
	const enum AVSampleFormat *fmts = NULL;
	gint                       n = 0, i;
	enum AVSampleFormat        want[4];

	if (avcodec_get_supported_config (ctx, codec, AV_CODEC_CONFIG_SAMPLE_FORMAT, 0, (const void **) &fmts, &n) < 0
	    || fmts == NULL || n == 0)
		return AV_SAMPLE_FMT_FLTP;
	if (bit_depth >= 24)
	{
		want[0] = AV_SAMPLE_FMT_S32;
		want[1] = AV_SAMPLE_FMT_S32P;
	}
	else
	{
		want[0] = AV_SAMPLE_FMT_S16;
		want[1] = AV_SAMPLE_FMT_S16P;
	}
	want[2] = AV_SAMPLE_FMT_FLTP;
	want[3] = AV_SAMPLE_FMT_FLT;
	{
		guint w;

		for (w = 0; w < G_N_ELEMENTS (want); w++)
			for (i = 0; i < n; i++)
				if (fmts[i] == want[w])
					return want[w];
	}
	return fmts[0];
}

/* the supported rate nearest @rate (equal, else the next above, else the highest) */
static gint
pick_rate (AVCodecContext *ctx,
           const AVCodec  *codec,
           guint           rate)
{
	const gint *rates = NULL;
	gint        n = 0, i, best = 0;

	if (avcodec_get_supported_config (ctx, codec, AV_CODEC_CONFIG_SAMPLE_RATE, 0, (const void **) &rates, &n) < 0
	    || rates == NULL || n == 0)
		return (gint) rate;
	for (i = 0; i < n; i++)
	{
		if (rates[i] == (gint) rate)
			return rates[i];
		if (rates[i] > (gint) rate && (best == 0 || rates[i] < best))
			best = rates[i];
	}
	if (best != 0)
		return best;
	for (i = 0; i < n; i++)
		best = MAX (best, rates[i]);
	return best;
}

gboolean
lrg_audio_libav_encode (const gfloat              *samples,
                        gsize                      frames,
                        guint                      channels,
                        guint                      rate,
                        const LrgAudioEncodePlan  *plan,
                        const gchar               *path,
                        GCancellable              *cancellable,
                        GError                   **error)
{
	const AVCodec     *codec = avcodec_find_encoder_by_name (plan->codec);
	AVFormatContext   *fmt = NULL;
	AVCodecContext    *ctx = NULL;
	AVStream          *stream;
	SwrContext        *swr = NULL;
	AVFrame           *frame = NULL;
	AVPacket          *pkt = NULL;
	AVAudioFifo       *fifo = NULL;
	AVChannelLayout    in_layout, out_layout;
	gboolean           ok = FALSE;
	gint               ret, frame_size;
	gsize              done = 0;
	gint64             pts = 0;
	gboolean           flushed = FALSE;

	if (codec == NULL)
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED, "libavcodec has no %s encoder", plan->codec);
		return FALSE;
	}
	av_channel_layout_default (&in_layout, (gint) channels);
	av_channel_layout_default (&out_layout, (gint) plan->channels);

	/* the container and the stream */
	ret = avformat_alloc_output_context2 (&fmt, NULL, plan->container, path);
	if (ret < 0 || fmt == NULL)
		return av_fail (error, ret, "cannot create the container", path);
	stream = avformat_new_stream (fmt, NULL);
	ctx = avcodec_alloc_context3 (codec);
	if (stream == NULL || ctx == NULL)
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED, "%s: out of memory", path);
		goto out;
	}
	ctx->sample_fmt = pick_format (ctx, codec, plan->bit_depth);
	ctx->sample_rate = pick_rate (ctx, codec, plan->rate);
	av_channel_layout_copy (&ctx->ch_layout, &out_layout);
	ctx->time_base = (AVRational) { 1, ctx->sample_rate };
	if (plan->bit_depth >= 24 && (ctx->sample_fmt == AV_SAMPLE_FMT_S32 || ctx->sample_fmt == AV_SAMPLE_FMT_S32P))
		ctx->bits_per_raw_sample = 24;
	if (plan->quality >= 0.0 && (g_str_equal (plan->codec, "libmp3lame") || g_str_has_suffix (plan->codec, "vorbis")))
	{
		ctx->flags |= AV_CODEC_FLAG_QSCALE;
		ctx->global_quality = FF_QP2LAMBDA
		                      * (g_str_equal (plan->codec, "libmp3lame") ? (gint) CLAMP (round (9.0 - plan->quality * 0.9), 0, 9)
		                                                                 : (gint) CLAMP (round (plan->quality), 0, 10));
	}
	else if (!g_str_equal (plan->codec, "flac") && !g_str_equal (plan->codec, "alac")
	         && !g_str_equal (plan->codec, "wavpack"))
		ctx->bit_rate = (gint64) plan->bitrate_kbps * 1000;
	if (g_str_equal (plan->codec, "vorbis") || g_str_equal (plan->codec, "opus"))
		ctx->strict_std_compliance = FF_COMPLIANCE_EXPERIMENTAL;
	if (fmt->oformat->flags & AVFMT_GLOBALHEADER)
		ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
	ret = avcodec_open2 (ctx, codec, NULL);
	if (ret < 0)
	{
		av_fail (error, ret, "cannot open the encoder", path);
		goto out;
	}
	ret = avcodec_parameters_from_context (stream->codecpar, ctx);
	if (ret < 0)
	{
		av_fail (error, ret, "cannot set the stream", path);
		goto out;
	}
	stream->time_base = ctx->time_base;

	/* tags */
	if (plan->title != NULL)
		av_dict_set (&fmt->metadata, "title", plan->title, 0);
	if (plan->artist != NULL)
		av_dict_set (&fmt->metadata, "artist", plan->artist, 0);
	if (plan->album != NULL)
		av_dict_set (&fmt->metadata, "album", plan->album, 0);
	if (plan->comment != NULL)
		av_dict_set (&fmt->metadata, "comment", plan->comment, 0);
	if (plan->date != NULL)
		av_dict_set (&fmt->metadata, "date", plan->date, 0);

	if (!(fmt->oformat->flags & AVFMT_NOFILE))
	{
		ret = avio_open (&fmt->pb, path, AVIO_FLAG_WRITE);
		if (ret < 0)
		{
			av_fail (error, ret, "cannot open the file", path);
			goto out;
		}
	}
	ret = avformat_write_header (fmt, NULL);
	if (ret < 0)
	{
		av_fail (error, ret, "cannot write the header", path);
		goto out;
	}

	/* the converter: interleaved float in, the encoder's layout out */
	ret = swr_alloc_set_opts2 (&swr, &ctx->ch_layout, ctx->sample_fmt, ctx->sample_rate, &in_layout,
	                           AV_SAMPLE_FMT_FLT, (gint) rate, 0, NULL);
	if (ret < 0 || swr_init (swr) < 0)
	{
		av_fail (error, ret, "cannot set up the resampler", path);
		goto out;
	}
	frame_size = ctx->frame_size > 0 ? ctx->frame_size : 4096;
	frame = av_frame_alloc ();
	pkt = av_packet_alloc ();
	frame->format = ctx->sample_fmt;
	frame->sample_rate = ctx->sample_rate;
	av_channel_layout_copy (&frame->ch_layout, &ctx->ch_layout);
	frame->nb_samples = frame_size;
	if (av_frame_get_buffer (frame, 0) < 0)
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED, "%s: out of memory", path);
		goto out;
	}

	/* convert chunks into a FIFO, encode it in whole encoder frames; at
	 * the end flush the resampler, pad the last frame when the encoder
	 * needs fixed-size frames, then drain the encoder */
	fifo = av_audio_fifo_alloc (ctx->sample_fmt, ctx->ch_layout.nb_channels, frame_size * 4);
	if (fifo == NULL)
		goto out;
	while (!flushed)
	{
		gboolean last = FALSE;

		if (cancellable != NULL && g_cancellable_is_cancelled (cancellable))
		{
			g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_CANCELLED, "the encode was cancelled");
			goto out;
		}

		/* 1. more converted samples into the FIFO */
		if (av_audio_fifo_size (fifo) < frame_size)
		{
			gint          room;
			guint8      **conv = NULL;
			gint          got;

			if (done < frames)
			{
				gsize         n = MIN (frames - done, (gsize) 8192);
				const guint8 *in[1];

				in[0] = (const guint8 *) (samples + done * channels);
				room = swr_get_out_samples (swr, (gint) n);
				if (av_samples_alloc_array_and_samples (&conv, NULL, ctx->ch_layout.nb_channels, MAX (room, 1),
				                                        ctx->sample_fmt, 0) < 0)
					goto out;
				got = swr_convert (swr, conv, room, in, (gint) n);
				done += n;
			}
			else
			{
				room = swr_get_out_samples (swr, 0) + 64;
				if (av_samples_alloc_array_and_samples (&conv, NULL, ctx->ch_layout.nb_channels, MAX (room, 1),
				                                        ctx->sample_fmt, 0) < 0)
					goto out;
				got = swr_convert (swr, conv, room, NULL, 0);   /* the resampler's tail */
				last = TRUE;
			}
			if (got > 0)
				av_audio_fifo_write (fifo, (void **) conv, got);
			av_freep (&conv[0]);
			av_freep (&conv);
			if (got < 0)
			{
				g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED, "%s: resampling failed", path);
				goto out;
			}
			if (!last)
				continue;
		}

		/* 2. one frame out of the FIFO (the last one may be short) */
		if (av_audio_fifo_size (fifo) > 0)
		{
			gint n = MIN (av_audio_fifo_size (fifo), frame_size);

			if (av_frame_make_writable (frame) < 0)
				goto out;
			av_audio_fifo_read (fifo, (void **) frame->data, n);
			if (n < frame_size && !(codec->capabilities & (AV_CODEC_CAP_VARIABLE_FRAME_SIZE
			                                              | AV_CODEC_CAP_SMALL_LAST_FRAME)))
			{
				av_samples_set_silence (frame->data, n, frame_size - n, ctx->ch_layout.nb_channels,
				                        ctx->sample_fmt);
				n = frame_size;
			}
			frame->nb_samples = n;
			frame->pts = pts;
			pts += n;
			ret = avcodec_send_frame (ctx, frame);
			frame->nb_samples = frame_size;
		}
		else if (done >= frames)
		{
			flushed = TRUE;
			ret = avcodec_send_frame (ctx, NULL);
		}
		else
			continue;
		if (ret < 0)
		{
			av_fail (error, ret, "encoding failed", path);
			goto out;
		}

		/* 3. whatever packets are ready */
		while ((ret = avcodec_receive_packet (ctx, pkt)) >= 0)
		{
			av_packet_rescale_ts (pkt, ctx->time_base, stream->time_base);
			pkt->stream_index = stream->index;
			ret = av_interleaved_write_frame (fmt, pkt);
			if (ret < 0)
			{
				av_fail (error, ret, "writing failed", path);
				goto out;
			}
		}
		if (ret != AVERROR (EAGAIN) && ret != AVERROR_EOF)
		{
			av_fail (error, ret, "encoding failed", path);
			goto out;
		}
	}
	ret = av_write_trailer (fmt);
	if (ret < 0)
	{
		av_fail (error, ret, "cannot finish the file", path);
		goto out;
	}
	ok = TRUE;

out:
	if (fifo != NULL)
		av_audio_fifo_free (fifo);
	av_packet_free (&pkt);
	av_frame_free (&frame);
	swr_free (&swr);
	avcodec_free_context (&ctx);
	if (fmt != NULL)
	{
		if (!(fmt->oformat->flags & AVFMT_NOFILE) && fmt->pb != NULL)
			avio_closep (&fmt->pb);
		avformat_free_context (fmt);
	}
	av_channel_layout_uninit (&in_layout);
	av_channel_layout_uninit (&out_layout);
	if (!ok && error != NULL && *error == NULL)
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED, "%s: encoding failed", path);
	return ok;
}

LrgWaveData *
lrg_audio_libav_decode (const gchar  *path,
                        GError      **error)
{
	AVFormatContext   *fmt = NULL;
	AVCodecContext    *ctx = NULL;
	const AVCodec     *codec = NULL;
	SwrContext        *swr = NULL;
	AVFrame           *frame = NULL;
	AVPacket          *pkt = NULL;
	AVChannelLayout    out_layout;
	GByteArray        *pcm = NULL;
	LrgWaveData       *wave = NULL;
	gint               ret, stream;
	guint              channels = 0;

	ret = avformat_open_input (&fmt, path, NULL, NULL);
	if (ret < 0)
	{
		av_fail (error, ret, "cannot open", path);
		return NULL;
	}
	if (avformat_find_stream_info (fmt, NULL) < 0
	    || (stream = av_find_best_stream (fmt, AVMEDIA_TYPE_AUDIO, -1, -1, &codec, 0)) < 0 || codec == NULL)
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "%s has no audio stream", path);
		avformat_close_input (&fmt);
		return NULL;
	}
	ctx = avcodec_alloc_context3 (codec);
	if (ctx == NULL || avcodec_parameters_to_context (ctx, fmt->streams[stream]->codecpar) < 0
	    || avcodec_open2 (ctx, codec, NULL) < 0)
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "%s: cannot open the decoder", path);
		goto out;
	}
	channels = (guint) MIN (MAX (ctx->ch_layout.nb_channels, 1), 2);
	av_channel_layout_default (&out_layout, (gint) channels);
	if (swr_alloc_set_opts2 (&swr, &out_layout, AV_SAMPLE_FMT_FLT, ctx->sample_rate, &ctx->ch_layout, ctx->sample_fmt,
	                         ctx->sample_rate, 0, NULL) < 0 || swr_init (swr) < 0)
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED, "%s: cannot set up the resampler", path);
		av_channel_layout_uninit (&out_layout);
		goto out;
	}
	av_channel_layout_uninit (&out_layout);
	frame = av_frame_alloc ();
	pkt = av_packet_alloc ();
	pcm = g_byte_array_new ();

	/* every packet of the stream, then the decoder's tail */
	for (;;)
	{
		gboolean eof = av_read_frame (fmt, pkt) < 0;

		if (!eof && pkt->stream_index != stream)
		{
			av_packet_unref (pkt);
			continue;
		}
		avcodec_send_packet (ctx, eof ? NULL : pkt);
		av_packet_unref (pkt);
		while (avcodec_receive_frame (ctx, frame) >= 0)
		{
			gint    max_out = swr_get_out_samples (swr, frame->nb_samples);
			guint8 *buf;
			gint    got;
			guint   old = pcm->len;

			if (max_out <= 0)
				continue;
			g_byte_array_set_size (pcm, old + (guint) max_out * channels * sizeof (gfloat));
			buf = pcm->data + old;
			got = swr_convert (swr, &buf, max_out, (const guint8 **) frame->extended_data, frame->nb_samples);
			g_byte_array_set_size (pcm, old + (guint) MAX (got, 0) * channels * sizeof (gfloat));
		}
		if (eof)
			break;
	}
	if (pcm->len == 0)
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "%s has no samples", path);
	else
		wave = lrg_wave_data_new_from_samples ((guint) ctx->sample_rate, 32, channels, pcm->data, pcm->len);

out:
	if (pcm != NULL)
		g_byte_array_unref (pcm);
	av_packet_free (&pkt);
	av_frame_free (&frame);
	swr_free (&swr);
	avcodec_free_context (&ctx);
	avformat_close_input (&fmt);
	return wave;
}

#else /* !LRG_HAS_FFMPEG */

gboolean
lrg_audio_libav_compiled (void)
{
	return FALSE;
}

gboolean
lrg_audio_libav_has_encoder (const gchar *name)
{
	(void) name;
	return FALSE;
}

gboolean
lrg_audio_libav_encode (const gfloat              *samples,
                        gsize                      frames,
                        guint                      channels,
                        guint                      rate,
                        const LrgAudioEncodePlan  *plan,
                        const gchar               *path,
                        GCancellable              *cancellable,
                        GError                   **error)
{
	(void) samples;
	(void) frames;
	(void) channels;
	(void) rate;
	(void) plan;
	(void) cancellable;
	g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED, "%s: libregnum was built without FFMPEG=1", path);
	return FALSE;
}

LrgWaveData *
lrg_audio_libav_decode (const gchar  *path,
                        GError      **error)
{
	g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED, "%s: libregnum was built without FFMPEG=1", path);
	return NULL;
}

#endif /* LRG_HAS_FFMPEG */
