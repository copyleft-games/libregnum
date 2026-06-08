/* lrg-reel-video-exporter.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "config.h"
#include "lrg-reel-video-exporter.h"
#include "../audio/lrg-wave-data.h"
#include <gio/gio.h>
#include <glib/gstdio.h>

struct _LrgReelVideoExporter
{
    LrgReelExporter parent_instance;

    gchar             *path;
    LrgReelVideoCodec  codec;
    gint               crf;
    gint               bitrate_kbps;  /* > 0 selects target bitrate over CRF */
    gchar             *ffmpeg_path;   /* override, or NULL to auto-discover */
    LrgWaveData       *audio;         /* optional, ref */

    /* Active encode state (begin .. finish). */
    GSubprocess  *proc;
    GOutputStream *stdin_stream;      /* (transfer none), owned by proc */
    gchar        *temp_dir;           /* created when audio is muxed */
    gchar        *temp_audio_path;
};

G_DEFINE_FINAL_TYPE (LrgReelVideoExporter, lrg_reel_video_exporter, LRG_TYPE_REEL_EXPORTER)

/* Locate the ffmpeg executable: explicit override first, then PATH.  An
 * explicit override is validated so a bad path reports "not found" rather than
 * failing later at spawn time. */
static gchar *
reel_video_find_ffmpeg (LrgReelVideoExporter *self)
{
    if (self->ffmpeg_path != NULL)
    {
        if (g_path_is_absolute (self->ffmpeg_path))
        {
            if (g_file_test (self->ffmpeg_path, G_FILE_TEST_IS_EXECUTABLE))
                return g_strdup (self->ffmpeg_path);
            return NULL;
        }
        return g_find_program_in_path (self->ffmpeg_path);
    }

    return g_find_program_in_path ("ffmpeg");
}

/* Build the ffmpeg argv. Returns a NULL-terminated, owned string vector. */
static gchar **
reel_video_build_argv (LrgReelVideoExporter *self,
                       const gchar          *ffmpeg,
                       gdouble               fps)
{
    GPtrArray *args = g_ptr_array_new ();

    g_ptr_array_add (args, g_strdup (ffmpeg));
    g_ptr_array_add (args, g_strdup ("-y"));
    /* Keep real errors but suppress the banner and per-frame progress. */
    g_ptr_array_add (args, g_strdup ("-loglevel"));
    g_ptr_array_add (args, g_strdup ("error"));
    g_ptr_array_add (args, g_strdup ("-nostats"));

    /* Video input: a stream of PNG frames at the composition frame rate. */
    g_ptr_array_add (args, g_strdup ("-framerate"));
    g_ptr_array_add (args, g_strdup_printf ("%.6g", fps));
    g_ptr_array_add (args, g_strdup ("-f"));
    g_ptr_array_add (args, g_strdup ("image2pipe"));
    g_ptr_array_add (args, g_strdup ("-vcodec"));
    g_ptr_array_add (args, g_strdup ("png"));
    g_ptr_array_add (args, g_strdup ("-i"));
    g_ptr_array_add (args, g_strdup ("pipe:0"));

    /* Optional audio input from a temp WAV. */
    if (self->temp_audio_path != NULL)
    {
        g_ptr_array_add (args, g_strdup ("-i"));
        g_ptr_array_add (args, g_strdup (self->temp_audio_path));
    }

    /* Video codec selection. */
    {
        const gchar *vcodec = "libx264";
        const gchar *pix_fmt = "yuv420p";
        gboolean     is_webm = FALSE;
        gboolean     is_prores = FALSE;
        gint         prores_profile = 3;

        switch (self->codec)
        {
        case LRG_REEL_VIDEO_CODEC_VP9:
            vcodec = "libvpx-vp9"; pix_fmt = "yuv420p"; is_webm = TRUE;
            break;
        case LRG_REEL_VIDEO_CODEC_VP9_ALPHA:
            vcodec = "libvpx-vp9"; pix_fmt = "yuva420p"; is_webm = TRUE;
            break;
        case LRG_REEL_VIDEO_CODEC_H265:
            vcodec = "libx265"; pix_fmt = "yuv420p";
            break;
        case LRG_REEL_VIDEO_CODEC_PRORES:
            vcodec = "prores_ks"; pix_fmt = "yuv422p10le";
            is_prores = TRUE; prores_profile = 3;
            break;
        case LRG_REEL_VIDEO_CODEC_PRORES_ALPHA:
            vcodec = "prores_ks"; pix_fmt = "yuva444p10le";
            is_prores = TRUE; prores_profile = 4;
            break;
        case LRG_REEL_VIDEO_CODEC_H264:
        default:
            vcodec = "libx264"; pix_fmt = "yuv420p";
            break;
        }

        g_ptr_array_add (args, g_strdup ("-c:v"));
        g_ptr_array_add (args, g_strdup (vcodec));
        g_ptr_array_add (args, g_strdup ("-pix_fmt"));
        g_ptr_array_add (args, g_strdup (pix_fmt));

        if (is_prores)
        {
            g_ptr_array_add (args, g_strdup ("-profile:v"));
            g_ptr_array_add (args, g_strdup_printf ("%d", prores_profile));
        }
        else if (self->bitrate_kbps > 0)
        {
            g_ptr_array_add (args, g_strdup ("-b:v"));
            g_ptr_array_add (args, g_strdup_printf ("%dk", self->bitrate_kbps));
        }
        else
        {
            if (is_webm)
            {
                g_ptr_array_add (args, g_strdup ("-b:v"));
                g_ptr_array_add (args, g_strdup ("0"));
            }
            else if (self->codec == LRG_REEL_VIDEO_CODEC_H264)
            {
                g_ptr_array_add (args, g_strdup ("-preset"));
                g_ptr_array_add (args, g_strdup ("medium"));
            }
            g_ptr_array_add (args, g_strdup ("-crf"));
            g_ptr_array_add (args, g_strdup_printf ("%d", self->crf));
        }

        /* H.265 in MP4 wants the hvc1 tag for QuickTime/Apple compatibility. */
        if (self->codec == LRG_REEL_VIDEO_CODEC_H265)
        {
            g_ptr_array_add (args, g_strdup ("-tag:v"));
            g_ptr_array_add (args, g_strdup ("hvc1"));
        }

        /* Audio codec + trim to the shorter stream when audio is present. */
        if (self->temp_audio_path != NULL)
        {
            g_ptr_array_add (args, g_strdup ("-c:a"));
            g_ptr_array_add (args, g_strdup (is_webm ? "libopus" : "aac"));
            g_ptr_array_add (args, g_strdup ("-b:a"));
            g_ptr_array_add (args, g_strdup ("192k"));
            g_ptr_array_add (args, g_strdup ("-shortest"));
        }

        /* faststart relocates the moov atom for streaming (MP4/MOV only). */
        if (!is_webm)
        {
            g_ptr_array_add (args, g_strdup ("-movflags"));
            g_ptr_array_add (args, g_strdup ("+faststart"));
        }
    }

    g_ptr_array_add (args, g_strdup (self->path));
    g_ptr_array_add (args, NULL);

    return (gchar **) g_ptr_array_free (args, FALSE);
}

static gboolean
lrg_reel_video_exporter_real_begin (LrgReelExporter *base,
                                    gint             width,
                                    gint             height,
                                    gdouble          fps,
                                    GError         **error)
{
    LrgReelVideoExporter *self = LRG_REEL_VIDEO_EXPORTER (base);
    g_autofree gchar     *ffmpeg = NULL;
    g_auto(GStrv)         argv = NULL;
    GSubprocessLauncher  *launcher;
    GError               *local = NULL;

    ffmpeg = reel_video_find_ffmpeg (self);
    if (ffmpeg == NULL)
    {
        g_set_error_literal (error, LRG_REEL_EXPORTER_ERROR,
                             LRG_REEL_EXPORTER_ERROR_FFMPEG_NOT_FOUND,
                             "ffmpeg was not found on the PATH; install ffmpeg "
                             "or call lrg_reel_video_exporter_set_ffmpeg_path()");
        return FALSE;
    }

    /* Write the mixed audio to a temp WAV that ffmpeg reads as a 2nd input. */
    if (self->audio != NULL)
    {
        self->temp_dir = g_dir_make_tmp ("lrg-reel-XXXXXX", &local);
        if (self->temp_dir == NULL)
        {
            g_propagate_error (error, local);
            return FALSE;
        }
        self->temp_audio_path = g_build_filename (self->temp_dir, "audio.wav", NULL);
        if (!lrg_wave_data_export_wav (self->audio, self->temp_audio_path, &local))
        {
            g_propagate_error (error, local);
            return FALSE;
        }
    }

    argv = reel_video_build_argv (self, ffmpeg, fps);

    launcher = g_subprocess_launcher_new (G_SUBPROCESS_FLAGS_STDIN_PIPE);
    self->proc = g_subprocess_launcher_spawnv (launcher,
                                               (const gchar * const *) argv,
                                               &local);
    g_object_unref (launcher);

    if (self->proc == NULL)
    {
        g_set_error (error, LRG_REEL_EXPORTER_ERROR,
                     LRG_REEL_EXPORTER_ERROR_SPAWN,
                     "failed to spawn ffmpeg: %s",
                     local != NULL ? local->message : "unknown error");
        g_clear_error (&local);
        return FALSE;
    }

    self->stdin_stream = g_subprocess_get_stdin_pipe (self->proc);

    /* width/height are implied by the PNG frames themselves. */
    (void) width;
    (void) height;

    return TRUE;
}

static gboolean
lrg_reel_video_exporter_real_add_frame (LrgReelExporter *base,
                                        GrlImage        *frame,
                                        GError         **error)
{
    LrgReelVideoExporter *self = LRG_REEL_VIDEO_EXPORTER (base);
    guint8               *png = NULL;
    gsize                 size = 0;
    gsize                 written = 0;
    GError               *local = NULL;
    gboolean              ok;

    if (self->stdin_stream == NULL)
    {
        g_set_error_literal (error, LRG_REEL_EXPORTER_ERROR,
                             LRG_REEL_EXPORTER_ERROR_FAILED,
                             "add_frame called before begin");
        return FALSE;
    }

    png = grl_image_export_to_memory (frame, ".png", &size);
    if (png == NULL || size == 0)
    {
        g_free (png);
        g_set_error_literal (error, LRG_REEL_EXPORTER_ERROR,
                             LRG_REEL_EXPORTER_ERROR_WRITE,
                             "failed to encode frame to PNG");
        return FALSE;
    }

    ok = g_output_stream_write_all (self->stdin_stream, png, size,
                                    &written, NULL, &local);
    g_free (png);

    if (!ok)
    {
        g_set_error (error, LRG_REEL_EXPORTER_ERROR,
                     LRG_REEL_EXPORTER_ERROR_WRITE,
                     "failed to write frame to ffmpeg: %s",
                     local != NULL ? local->message : "broken pipe");
        g_clear_error (&local);
        return FALSE;
    }

    return TRUE;
}

/* Remove the temp audio file and directory, if any. */
static void
reel_video_cleanup_temp (LrgReelVideoExporter *self)
{
    if (self->temp_audio_path != NULL)
    {
        g_unlink (self->temp_audio_path);
        g_clear_pointer (&self->temp_audio_path, g_free);
    }
    if (self->temp_dir != NULL)
    {
        g_rmdir (self->temp_dir);
        g_clear_pointer (&self->temp_dir, g_free);
    }
}

static gboolean
lrg_reel_video_exporter_real_finish (LrgReelExporter *base,
                                     GError         **error)
{
    LrgReelVideoExporter *self = LRG_REEL_VIDEO_EXPORTER (base);
    GError               *local = NULL;
    gboolean              ok = TRUE;

    if (self->proc == NULL)
    {
        g_set_error_literal (error, LRG_REEL_EXPORTER_ERROR,
                             LRG_REEL_EXPORTER_ERROR_FAILED,
                             "finish called before begin");
        return FALSE;
    }

    /* Closing stdin signals EOF so ffmpeg flushes and exits. */
    if (self->stdin_stream != NULL)
    {
        g_output_stream_close (self->stdin_stream, NULL, NULL);
        self->stdin_stream = NULL;
    }

    if (!g_subprocess_wait_check (self->proc, NULL, &local))
    {
        g_set_error (error, LRG_REEL_EXPORTER_ERROR,
                     LRG_REEL_EXPORTER_ERROR_SPAWN,
                     "ffmpeg exited with an error: %s",
                     local != NULL ? local->message : "non-zero exit");
        g_clear_error (&local);
        ok = FALSE;
    }

    g_clear_object (&self->proc);
    reel_video_cleanup_temp (self);

    return ok;
}

static void
lrg_reel_video_exporter_finalize (GObject *object)
{
    LrgReelVideoExporter *self = LRG_REEL_VIDEO_EXPORTER (object);

    if (self->stdin_stream != NULL)
    {
        g_output_stream_close (self->stdin_stream, NULL, NULL);
        self->stdin_stream = NULL;
    }
    g_clear_object (&self->proc);
    reel_video_cleanup_temp (self);

    g_clear_object (&self->audio);
    g_clear_pointer (&self->path, g_free);
    g_clear_pointer (&self->ffmpeg_path, g_free);

    G_OBJECT_CLASS (lrg_reel_video_exporter_parent_class)->finalize (object);
}

static void
lrg_reel_video_exporter_class_init (LrgReelVideoExporterClass *klass)
{
    GObjectClass        *object_class = G_OBJECT_CLASS (klass);
    LrgReelExporterClass *exporter_class = LRG_REEL_EXPORTER_CLASS (klass);

    object_class->finalize = lrg_reel_video_exporter_finalize;

    exporter_class->begin = lrg_reel_video_exporter_real_begin;
    exporter_class->add_frame = lrg_reel_video_exporter_real_add_frame;
    exporter_class->finish = lrg_reel_video_exporter_real_finish;
}

static void
lrg_reel_video_exporter_init (LrgReelVideoExporter *self)
{
    self->crf = 23;
}

LrgReelVideoExporter *
lrg_reel_video_exporter_new (const gchar       *path,
                             LrgReelVideoCodec  codec)
{
    LrgReelVideoExporter *self;

    g_return_val_if_fail (path != NULL, NULL);

    self = g_object_new (LRG_TYPE_REEL_VIDEO_EXPORTER, NULL);
    self->path = g_strdup (path);
    self->codec = codec;

    return self;
}

void
lrg_reel_video_exporter_set_audio (LrgReelVideoExporter *self,
                                   LrgWaveData          *audio)
{
    g_return_if_fail (LRG_IS_REEL_VIDEO_EXPORTER (self));
    g_return_if_fail (audio == NULL || LRG_IS_WAVE_DATA (audio));

    g_set_object (&self->audio, audio);
}

void
lrg_reel_video_exporter_set_crf (LrgReelVideoExporter *self,
                                 gint                  crf)
{
    g_return_if_fail (LRG_IS_REEL_VIDEO_EXPORTER (self));

    self->crf = crf;
}

/**
 * lrg_reel_video_exporter_set_bitrate:
 * @self: an #LrgReelVideoExporter
 * @kbps: target video bitrate in kbit/s, or 0 to use CRF (the default).
 *
 * When non-zero, the exporter targets this average bitrate instead of using a
 * constant quality (CRF).  Ignored for ProRes (which is profile-driven).
 *
 * Since: 1.0
 */
void
lrg_reel_video_exporter_set_bitrate (LrgReelVideoExporter *self,
                                     gint                  kbps)
{
    g_return_if_fail (LRG_IS_REEL_VIDEO_EXPORTER (self));

    self->bitrate_kbps = kbps;
}

void
lrg_reel_video_exporter_set_ffmpeg_path (LrgReelVideoExporter *self,
                                         const gchar          *path)
{
    g_return_if_fail (LRG_IS_REEL_VIDEO_EXPORTER (self));

    g_free (self->ffmpeg_path);
    self->ffmpeg_path = g_strdup (path);
}

gboolean
lrg_reel_video_exporter_is_ffmpeg_available (void)
{
    g_autofree gchar *p = g_find_program_in_path ("ffmpeg");

    return p != NULL;
}
