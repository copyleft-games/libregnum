/* lrg-reel-audio-exporter.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "config.h"
#include "lrg-reel-audio-exporter.h"
#include "../audio/lrg-wave-data.h"
#include "../audio/lrg-audio-encoder.h"
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

/* the encoder format of each reel format */
static LrgAudioFileFormat
reel_audio_file_format (LrgReelAudioFormat format)
{
    switch (format)
    {
    case LRG_REEL_AUDIO_FORMAT_MP3:
        return LRG_AUDIO_FILE_FORMAT_MP3;
    case LRG_REEL_AUDIO_FORMAT_AAC:
        return LRG_AUDIO_FILE_FORMAT_AAC;
    case LRG_REEL_AUDIO_FORMAT_FLAC:
        return LRG_AUDIO_FILE_FORMAT_FLAC;
    case LRG_REEL_AUDIO_FORMAT_OGG:
        return LRG_AUDIO_FILE_FORMAT_OGG;
    case LRG_REEL_AUDIO_FORMAT_OPUS:
        return LRG_AUDIO_FILE_FORMAT_OPUS;
    case LRG_REEL_AUDIO_FORMAT_WAV:
    default:
        return LRG_AUDIO_FILE_FORMAT_WAV;
    }
}

gboolean
lrg_reel_audio_exporter_export (LrgReelAudioExporter *self,
                                LrgWaveData          *wave,
                                GError              **error)
{
    g_autoptr(LrgAudioEncodeOptions) options = NULL;
    g_autoptr(GError)                local = NULL;
    LrgAudioFileFormat               format;

    g_return_val_if_fail (LRG_IS_REEL_AUDIO_EXPORTER (self), FALSE);
    g_return_val_if_fail (LRG_IS_WAVE_DATA (wave), FALSE);
    g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

    /* WAV is still written by the wave itself (raylib's ExportWave), as
     * before 0.10.0; everything else goes through LrgAudioEncoder, which
     * picks libav or the ffmpeg program */
    if (self->format == LRG_REEL_AUDIO_FORMAT_WAV)
        return lrg_wave_data_export_wav (wave, self->path, error);

    format = reel_audio_file_format (self->format);
    options = lrg_audio_encode_options_new ();
    options->bitrate_kbps = self->bitrate_kbps > 0 ? (guint) self->bitrate_kbps : 0;
    if (lrg_audio_encode_wave (wave, format, options, self->path, NULL, &local))
        return TRUE;

    /* keep the error 0.9 callers check for when no encoder exists */
    if (g_error_matches (local, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED))
        g_set_error (error, LRG_REEL_AUDIO_EXPORTER_ERROR, 1, "%s", local->message);
    else
        g_propagate_error (error, g_steal_pointer (&local));
    return FALSE;
}
