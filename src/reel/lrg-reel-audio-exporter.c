/* lrg-reel-audio-exporter.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "config.h"
#include "lrg-reel-audio-exporter.h"
#include "../audio/lrg-wave-data.h"
#include <gio/gio.h>
#include <glib/gstdio.h>

struct _LrgReelAudioExporter
{
    GObject            parent_instance;

    gchar             *path;
    LrgReelAudioFormat format;
    gint               bitrate_kbps;
};

G_DEFINE_FINAL_TYPE (LrgReelAudioExporter, lrg_reel_audio_exporter, G_TYPE_OBJECT)

#define LRG_REEL_AUDIO_EXPORTER_ERROR (g_quark_from_static_string ("lrg-reel-audio-exporter"))

static void
lrg_reel_audio_exporter_finalize (GObject *object)
{
    LrgReelAudioExporter *self = LRG_REEL_AUDIO_EXPORTER (object);

    g_clear_pointer (&self->path, g_free);

    G_OBJECT_CLASS (lrg_reel_audio_exporter_parent_class)->finalize (object);
}

static void
lrg_reel_audio_exporter_class_init (LrgReelAudioExporterClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = lrg_reel_audio_exporter_finalize;
}

static void
lrg_reel_audio_exporter_init (LrgReelAudioExporter *self)
{
    self->format = LRG_REEL_AUDIO_FORMAT_WAV;
    self->bitrate_kbps = 192;
}

LrgReelAudioExporter *
lrg_reel_audio_exporter_new (const gchar        *path,
                             LrgReelAudioFormat  format)
{
    LrgReelAudioExporter *self;

    g_return_val_if_fail (path != NULL, NULL);

    self = g_object_new (LRG_TYPE_REEL_AUDIO_EXPORTER, NULL);
    self->path = g_strdup (path);
    self->format = format;

    return self;
}

void
lrg_reel_audio_exporter_set_bitrate (LrgReelAudioExporter *self,
                                     gint                  kbps)
{
    g_return_if_fail (LRG_IS_REEL_AUDIO_EXPORTER (self));

    self->bitrate_kbps = kbps;
}

/* Map the lossy/lossless format to the ffmpeg encoder name. */
static const gchar *
reel_audio_codec_name (LrgReelAudioFormat format)
{
    switch (format)
    {
    case LRG_REEL_AUDIO_FORMAT_MP3:
        return "libmp3lame";
    case LRG_REEL_AUDIO_FORMAT_AAC:
        return "aac";
    case LRG_REEL_AUDIO_FORMAT_FLAC:
        return "flac";
    case LRG_REEL_AUDIO_FORMAT_WAV:
    default:
        return "pcm_s16le";
    }
}

gboolean
lrg_reel_audio_exporter_export (LrgReelAudioExporter *self,
                                LrgWaveData          *wave,
                                GError              **error)
{
    g_autofree gchar *ffmpeg = NULL;
    g_autofree gchar *tmp_dir = NULL;
    g_autofree gchar *tmp_wav = NULL;
    g_autoptr(GSubprocess) proc = NULL;
    g_autoptr(GSubprocessLauncher) launcher = NULL;
    const gchar *argv[16];
    gboolean ok;

    g_return_val_if_fail (LRG_IS_REEL_AUDIO_EXPORTER (self), FALSE);
    g_return_val_if_fail (LRG_IS_WAVE_DATA (wave), FALSE);
    g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

    /* WAV is written directly with no external dependency. */
    if (self->format == LRG_REEL_AUDIO_FORMAT_WAV)
        return lrg_wave_data_export_wav (wave, self->path, error);

    ffmpeg = g_find_program_in_path ("ffmpeg");
    if (ffmpeg == NULL)
    {
        g_set_error_literal (error, LRG_REEL_AUDIO_EXPORTER_ERROR, 1,
                             "ffmpeg is required to encode this audio format");
        return FALSE;
    }

    /* Stage a temp WAV, then transcode to the requested format. */
    tmp_dir = g_dir_make_tmp ("lrg-reel-aexp-XXXXXX", error);
    if (tmp_dir == NULL)
        return FALSE;
    tmp_wav = g_build_filename (tmp_dir, "mix.wav", NULL);

    if (!lrg_wave_data_export_wav (wave, tmp_wav, error))
    {
        g_unlink (tmp_wav);
        g_rmdir (tmp_dir);
        return FALSE;
    }

    argv[0] = ffmpeg;
    argv[1] = "-y";
    argv[2] = "-loglevel"; argv[3] = "error";
    argv[4] = "-i";        argv[5] = tmp_wav;
    argv[6] = "-c:a";      argv[7] = reel_audio_codec_name (self->format);
    argv[8] = "-b:a";      argv[9] = g_strdup_printf ("%dk", self->bitrate_kbps);
    argv[10] = self->path;
    argv[11] = NULL;

    launcher = g_subprocess_launcher_new (G_SUBPROCESS_FLAGS_STDOUT_SILENCE |
                                          G_SUBPROCESS_FLAGS_STDERR_SILENCE);
    proc = g_subprocess_launcher_spawnv (launcher, argv, error);

    ok = (proc != NULL) && g_subprocess_wait_check (proc, NULL, error);

    g_free ((gchar *) argv[9]);
    g_unlink (tmp_wav);
    g_rmdir (tmp_dir);

    return ok;
}
