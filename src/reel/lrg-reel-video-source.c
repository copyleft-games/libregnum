/* lrg-reel-video-source.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "config.h"
#include "lrg-reel-video-source.h"
#include "../audio/lrg-wave-data.h"
#include <gio/gio.h>
#include <glib/gstdio.h>
#include <string.h>

typedef struct
{
    gsize offset;  /* into png_data */
    gsize length;
} ReelFrameEntry;

struct _LrgReelVideoSource
{
    GObject parent_instance;

    gchar   *path;
    gint     width;
    gint     height;
    gdouble  fps;
    gdouble  duration;
    gboolean has_audio;

    gboolean   decoded;
    GBytes    *png_data;       /* concatenated PNG frames */
    GArray    *frame_index;    /* of ReelFrameEntry */

    GrlImage *current_frame;
    gint      current_index;

    /* Async decode support: `decoded'/`decoding'/`png_data'/`frame_index'
     * are guarded by `lock'; a worker thread (holding a strong ref) runs
     * the ffmpeg decode and commits under the lock, then broadcasts. */
    GMutex    lock;
    GCond     cond;
    gboolean  async_decode;
    gboolean  decoding;
    GrlImage *placeholder;     /* returned while an async decode runs */
};

G_DEFINE_FINAL_TYPE (LrgReelVideoSource, lrg_reel_video_source, G_TYPE_OBJECT)

#define LRG_REEL_VIDEO_SOURCE_ERROR (g_quark_from_static_string ("lrg-reel-video-source"))

static void
lrg_reel_video_source_finalize (GObject *object)
{
    LrgReelVideoSource *self = LRG_REEL_VIDEO_SOURCE (object);

    /* A running decode worker holds a strong ref, so finalize cannot race
     * an in-flight decode. */
    g_clear_object (&self->current_frame);
    g_clear_object (&self->placeholder);
    g_clear_pointer (&self->png_data, g_bytes_unref);
    g_clear_pointer (&self->frame_index, g_array_unref);
    g_clear_pointer (&self->path, g_free);
    g_mutex_clear (&self->lock);
    g_cond_clear (&self->cond);

    G_OBJECT_CLASS (lrg_reel_video_source_parent_class)->finalize (object);
}

static void
lrg_reel_video_source_class_init (LrgReelVideoSourceClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = lrg_reel_video_source_finalize;
}

static void
lrg_reel_video_source_init (LrgReelVideoSource *self)
{
    self->fps = 30.0;
    self->current_index = -1;
    self->frame_index = g_array_new (FALSE, FALSE, sizeof (ReelFrameEntry));
    g_mutex_init (&self->lock);
    g_cond_init (&self->cond);
}

gboolean
lrg_reel_video_source_is_ffmpeg_available (void)
{
    g_autofree gchar *ff = g_find_program_in_path ("ffmpeg");
    g_autofree gchar *fp = g_find_program_in_path ("ffprobe");

    return ff != NULL && fp != NULL;
}

/* Run a program to completion, capturing stdout as bytes. */
static GBytes *
reel_video_run_capture (const gchar * const *argv,
                        GError             **error)
{
    g_autoptr(GSubprocessLauncher) launcher = NULL;
    g_autoptr(GSubprocess) proc = NULL;
    GBytes *out = NULL;

    launcher = g_subprocess_launcher_new (G_SUBPROCESS_FLAGS_STDOUT_PIPE |
                                          G_SUBPROCESS_FLAGS_STDERR_SILENCE);
    proc = g_subprocess_launcher_spawnv (launcher, argv, error);
    if (proc == NULL)
        return NULL;

    if (!g_subprocess_communicate (proc, NULL, NULL, &out, NULL, error))
    {
        g_clear_pointer (&out, g_bytes_unref);
        return NULL;
    }

    return out;
}

/* Parse a "key=value" block from ffprobe output. */
static gchar *
reel_video_probe_value (const gchar *text,
                        const gchar *key)
{
    g_autofree gchar *needle = g_strconcat (key, "=", NULL);
    const gchar *p = strstr (text, needle);
    const gchar *end;

    if (p == NULL)
        return NULL;

    p += strlen (needle);
    end = strchr (p, '\n');
    if (end == NULL)
        end = p + strlen (p);

    return g_strndup (p, (gsize) (end - p));
}

static gboolean
reel_video_probe (LrgReelVideoSource *self,
                  GError            **error)
{
    g_autofree gchar *ffprobe = g_find_program_in_path ("ffprobe");
    g_autoptr(GBytes) out = NULL;
    g_autofree gchar *text = NULL;
    g_autofree gchar *w = NULL;
    g_autofree gchar *h = NULL;
    g_autofree gchar *rate = NULL;
    g_autofree gchar *dur = NULL;
    const gchar *argv[14];
    gsize len = 0;

    if (ffprobe == NULL)
    {
        g_set_error_literal (error, LRG_REEL_VIDEO_SOURCE_ERROR, 1,
                             "ffprobe was not found on the PATH");
        return FALSE;
    }

    argv[0] = ffprobe;
    argv[1] = "-v";          argv[2] = "error";
    argv[3] = "-select_streams"; argv[4] = "v:0";
    argv[5] = "-show_entries";
    argv[6] = "stream=width,height,r_frame_rate,duration";
    argv[7] = "-of";         argv[8] = "default=noprint_wrappers=1";
    argv[9] = self->path;
    argv[10] = NULL;

    out = reel_video_run_capture ((const gchar * const *) argv, error);
    if (out == NULL)
        return FALSE;

    {
        const gchar *data = g_bytes_get_data (out, &len);
        text = g_strndup (data, len);
    }

    w = reel_video_probe_value (text, "width");
    h = reel_video_probe_value (text, "height");
    rate = reel_video_probe_value (text, "r_frame_rate");
    dur = reel_video_probe_value (text, "duration");

    if (w == NULL || h == NULL)
    {
        g_set_error_literal (error, LRG_REEL_VIDEO_SOURCE_ERROR, 2,
                             "could not determine video dimensions");
        return FALSE;
    }

    self->width = (gint) g_ascii_strtoll (w, NULL, 10);
    self->height = (gint) g_ascii_strtoll (h, NULL, 10);

    /* r_frame_rate is "num/den". */
    if (rate != NULL)
    {
        gchar *slash = strchr (rate, '/');

        if (slash != NULL)
        {
            gdouble num = g_ascii_strtod (rate, NULL);
            gdouble den = g_ascii_strtod (slash + 1, NULL);

            if (den > 0.0)
                self->fps = num / den;
        }
    }
    if (dur != NULL)
        self->duration = g_ascii_strtod (dur, NULL);

    /* Probe for an audio stream. */
    {
        g_autoptr(GBytes) aout = NULL;
        const gchar *aargv[12];

        aargv[0] = ffprobe;
        aargv[1] = "-v";          aargv[2] = "error";
        aargv[3] = "-select_streams"; aargv[4] = "a:0";
        aargv[5] = "-show_entries"; aargv[6] = "stream=codec_type";
        aargv[7] = "-of";         aargv[8] = "default=noprint_wrappers=1:nokey=1";
        aargv[9] = self->path;
        aargv[10] = NULL;

        aout = reel_video_run_capture ((const gchar * const *) aargv, NULL);
        if (aout != NULL)
        {
            gsize alen = 0;
            const gchar *atext = g_bytes_get_data (aout, &alen);

            self->has_audio = (alen > 0 && strstr (atext, "audio") != NULL);
        }
    }

    return TRUE;
}

/* Walk a concatenated-PNG buffer, recording one entry per frame into OUT. */
static void
reel_video_build_index_for (GBytes *png_data,
                            GArray *out)
{
    const guint8 *data;
    gsize         size = 0;
    gsize         pos = 0;
    static const guint8 sig[8] = { 0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A };

    data = g_bytes_get_data (png_data, &size);

    while (pos + 8 <= size)
    {
        gsize    start = pos;
        gboolean iend = FALSE;

        if (memcmp (data + pos, sig, 8) != 0)
            break;
        pos += 8;

        while (pos + 8 <= size && !iend)
        {
            guint32  clen = ((guint32) data[pos] << 24) |
                            ((guint32) data[pos + 1] << 16) |
                            ((guint32) data[pos + 2] << 8) |
                            (guint32) data[pos + 3];
            gboolean is_iend = (data[pos + 4] == 'I' && data[pos + 5] == 'E' &&
                                data[pos + 6] == 'N' && data[pos + 7] == 'D');

            pos += 8 + (gsize) clen + 4; /* len+type, data, crc */
            if (is_iend)
                iend = TRUE;
        }

        if (iend && pos <= size)
        {
            ReelFrameEntry e;

            e.offset = start;
            e.length = pos - start;
            g_array_append_val (out, e);
        }
        else
        {
            break;
        }
    }
}

/* Run the whole-clip ffmpeg decode.  Touches no mutable object state (only
 * the immutable `path'), so it is safe on a worker thread.  On success the
 * caller owns *OUT_BYTES and *OUT_INDEX. */
static gboolean
reel_video_run_decode (LrgReelVideoSource *self,
                       GBytes            **out_bytes,
                       GArray            **out_index,
                       GError            **error)
{
    g_autofree gchar *ffmpeg = NULL;
    GBytes *bytes;
    GArray *index;
    const gchar *argv[14];

    *out_bytes = NULL;
    *out_index = NULL;

    ffmpeg = g_find_program_in_path ("ffmpeg");
    if (ffmpeg == NULL)
    {
        g_set_error_literal (error, LRG_REEL_VIDEO_SOURCE_ERROR, 1,
                             "ffmpeg was not found on the PATH");
        return FALSE;
    }

    argv[0] = ffmpeg;
    argv[1] = "-v";       argv[2] = "error";
    argv[3] = "-i";       argv[4] = self->path;
    argv[5] = "-f";       argv[6] = "image2pipe";
    argv[7] = "-vcodec";  argv[8] = "png";
    argv[9] = "-";
    argv[10] = NULL;

    bytes = reel_video_run_capture ((const gchar * const *) argv, error);
    if (bytes == NULL)
        return FALSE;

    index = g_array_new (FALSE, FALSE, sizeof (ReelFrameEntry));
    reel_video_build_index_for (bytes, index);

    if (index->len == 0)
    {
        g_bytes_unref (bytes);
        g_array_unref (index);
        g_set_error_literal (error, LRG_REEL_VIDEO_SOURCE_ERROR, 3,
                             "ffmpeg produced no frames");
        return FALSE;
    }

    *out_bytes = bytes;
    *out_index = index;
    return TRUE;
}

/* Commit a decode result (or failure: NULL/NULL) and wake waiters. */
static void
reel_video_commit_decode (LrgReelVideoSource *self,
                          GBytes             *bytes,
                          GArray             *index)
{
    g_mutex_lock (&self->lock);
    self->png_data = bytes;
    if (index != NULL)
    {
        g_array_unref (self->frame_index);
        self->frame_index = index;
    }
    self->decoded = TRUE;
    self->decoding = FALSE;
    g_cond_broadcast (&self->cond);
    g_mutex_unlock (&self->lock);
}

/* Synchronous path: waits out any in-flight worker, else decodes here. */
static gboolean
reel_video_ensure_decoded (LrgReelVideoSource *self,
                           GError            **error)
{
    GBytes *bytes = NULL;
    GArray *index = NULL;
    gboolean ok;

    g_mutex_lock (&self->lock);
    while (self->decoding)
        g_cond_wait (&self->cond, &self->lock);
    if (self->decoded)
    {
        ok = self->png_data != NULL;
        g_mutex_unlock (&self->lock);
        if (!ok)
            g_set_error_literal (error, LRG_REEL_VIDEO_SOURCE_ERROR, 3,
                                 "video decode failed");
        return ok;
    }
    self->decoding = TRUE;
    g_mutex_unlock (&self->lock);

    ok = reel_video_run_decode (self, &bytes, &index, error);
    reel_video_commit_decode (self, bytes, index);
    return ok;
}

static gpointer
reel_video_decode_thread (gpointer user_data)
{
    LrgReelVideoSource *self = user_data;
    GBytes *bytes = NULL;
    GArray *index = NULL;

    reel_video_run_decode (self, &bytes, &index, NULL);
    reel_video_commit_decode (self, bytes, index);
    g_object_unref (self);
    return NULL;
}

/* Start the worker if no decode has happened or is in flight. */
static void
reel_video_kick_async (LrgReelVideoSource *self)
{
    GThread *thread;

    g_mutex_lock (&self->lock);
    if (self->decoded || self->decoding)
    {
        g_mutex_unlock (&self->lock);
        return;
    }
    self->decoding = TRUE;
    g_mutex_unlock (&self->lock);

    thread = g_thread_new ("lrg-reel-decode", reel_video_decode_thread,
                           g_object_ref (self));
    g_thread_unref (thread);
}

/* Solid stand-in frame shown while the worker decodes (main thread only). */
static GrlImage *
reel_video_placeholder (LrgReelVideoSource *self)
{
    if (self->placeholder == NULL)
    {
        GrlColor col = { 24, 24, 24, 255 };

        self->placeholder = grl_image_new_color (MAX (self->width, 1),
                                                 MAX (self->height, 1), &col);
    }
    return self->placeholder;
}

LrgReelVideoSource *
lrg_reel_video_source_new_from_file (const gchar  *path,
                                     GError      **error)
{
    LrgReelVideoSource *self;

    g_return_val_if_fail (path != NULL, NULL);

    self = g_object_new (LRG_TYPE_REEL_VIDEO_SOURCE, NULL);
    self->path = g_strdup (path);

    if (!reel_video_probe (self, error))
    {
        g_object_unref (self);
        return NULL;
    }

    return self;
}

gint
lrg_reel_video_source_get_width (LrgReelVideoSource *self)
{
    g_return_val_if_fail (LRG_IS_REEL_VIDEO_SOURCE (self), 0);
    return self->width;
}

gint
lrg_reel_video_source_get_height (LrgReelVideoSource *self)
{
    g_return_val_if_fail (LRG_IS_REEL_VIDEO_SOURCE (self), 0);
    return self->height;
}

gdouble
lrg_reel_video_source_get_fps (LrgReelVideoSource *self)
{
    g_return_val_if_fail (LRG_IS_REEL_VIDEO_SOURCE (self), 0.0);
    return self->fps;
}

gdouble
lrg_reel_video_source_get_duration (LrgReelVideoSource *self)
{
    g_return_val_if_fail (LRG_IS_REEL_VIDEO_SOURCE (self), 0.0);
    return self->duration;
}

gboolean
lrg_reel_video_source_get_has_audio (LrgReelVideoSource *self)
{
    g_return_val_if_fail (LRG_IS_REEL_VIDEO_SOURCE (self), FALSE);
    return self->has_audio;
}

gint
lrg_reel_video_source_get_frame_count (LrgReelVideoSource *self)
{
    gboolean ready;

    g_return_val_if_fail (LRG_IS_REEL_VIDEO_SOURCE (self), 0);

    g_mutex_lock (&self->lock);
    ready = self->decoded;
    g_mutex_unlock (&self->lock);

    if (!ready && self->async_decode)
    {
        gint est = (gint) (self->duration * self->fps + 0.5);

        /* Kick the worker and answer from the probe so callers never
         * block; the estimate is replaced by the real count once done. */
        reel_video_kick_async (self);
        return MAX (est, 1);
    }

    if (!reel_video_ensure_decoded (self, NULL))
        return 0;

    return (gint) self->frame_index->len;
}

GrlImage *
lrg_reel_video_source_get_frame (LrgReelVideoSource *self,
                                 gint                index,
                                 GError            **error)
{
    ReelFrameEntry e;
    const guint8  *data;
    GrlImage      *img;
    gboolean       ready;

    g_return_val_if_fail (LRG_IS_REEL_VIDEO_SOURCE (self), NULL);

    g_mutex_lock (&self->lock);
    ready = self->decoded;
    g_mutex_unlock (&self->lock);

    if (!ready && self->async_decode)
    {
        /* Non-blocking preview path: hand back a stand-in frame while the
         * worker decodes.  Callers needing real frames (export) go through
         * lrg_reel_video_source_wait_decoded(). */
        reel_video_kick_async (self);
        return reel_video_placeholder (self);
    }

    if (!reel_video_ensure_decoded (self, error))
        return NULL;

    if (index < 0)
        index = 0;
    if (index >= (gint) self->frame_index->len)
        index = (gint) self->frame_index->len - 1;

    if (self->current_frame != NULL && self->current_index == index)
        return self->current_frame;

    e = g_array_index (self->frame_index, ReelFrameEntry, index);
    data = g_bytes_get_data (self->png_data, NULL);
    img = grl_image_new_from_memory (".png", data + e.offset, e.length);
    if (img == NULL)
    {
        g_set_error (error, LRG_REEL_VIDEO_SOURCE_ERROR, 4,
                     "failed to decode frame %d", index);
        return NULL;
    }

    g_clear_object (&self->current_frame);
    self->current_frame = img;
    self->current_index = index;

    return self->current_frame;
}

void
lrg_reel_video_source_set_async_decode (LrgReelVideoSource *self,
                                        gboolean            async_decode)
{
    g_return_if_fail (LRG_IS_REEL_VIDEO_SOURCE (self));
    self->async_decode = async_decode;
}

gboolean
lrg_reel_video_source_get_async_decode (LrgReelVideoSource *self)
{
    g_return_val_if_fail (LRG_IS_REEL_VIDEO_SOURCE (self), FALSE);
    return self->async_decode;
}

gboolean
lrg_reel_video_source_is_decoded (LrgReelVideoSource *self)
{
    gboolean ok;

    g_return_val_if_fail (LRG_IS_REEL_VIDEO_SOURCE (self), FALSE);

    g_mutex_lock (&self->lock);
    ok = self->decoded && self->png_data != NULL;
    g_mutex_unlock (&self->lock);
    return ok;
}

gboolean
lrg_reel_video_source_wait_decoded (LrgReelVideoSource *self,
                                    GError            **error)
{
    g_return_val_if_fail (LRG_IS_REEL_VIDEO_SOURCE (self), FALSE);
    return reel_video_ensure_decoded (self, error);
}

LrgWaveData *
lrg_reel_video_source_extract_audio (LrgReelVideoSource *self,
                                     GError            **error)
{
    g_autofree gchar *ffmpeg = NULL;
    g_autofree gchar *tmp_dir = NULL;
    g_autofree gchar *wav_path = NULL;
    g_autoptr(GBytes) out = NULL;
    LrgWaveData *wave;
    const gchar *argv[16];

    g_return_val_if_fail (LRG_IS_REEL_VIDEO_SOURCE (self), NULL);

    if (!self->has_audio)
        return NULL;

    ffmpeg = g_find_program_in_path ("ffmpeg");
    if (ffmpeg == NULL)
    {
        g_set_error_literal (error, LRG_REEL_VIDEO_SOURCE_ERROR, 1,
                             "ffmpeg was not found on the PATH");
        return NULL;
    }

    tmp_dir = g_dir_make_tmp ("lrg-reel-aud-XXXXXX", error);
    if (tmp_dir == NULL)
        return NULL;
    wav_path = g_build_filename (tmp_dir, "audio.wav", NULL);

    argv[0] = ffmpeg;
    argv[1] = "-v";   argv[2] = "error";
    argv[3] = "-y";
    argv[4] = "-i";   argv[5] = self->path;
    argv[6] = "-vn";
    argv[7] = "-acodec"; argv[8] = "pcm_s16le";
    argv[9] = wav_path;
    argv[10] = NULL;

    out = reel_video_run_capture ((const gchar * const *) argv, error);
    if (out == NULL)
    {
        g_rmdir (tmp_dir);
        return NULL;
    }

    wave = lrg_wave_data_new_from_file (wav_path, error);
    g_unlink (wav_path);
    g_rmdir (tmp_dir);

    return wave;
}
