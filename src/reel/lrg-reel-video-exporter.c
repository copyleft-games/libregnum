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
#include <math.h>
#include <string.h>

struct _LrgReelVideoExporter
{
    LrgReelExporter parent_instance;

    gchar             *path;
    LrgReelVideoCodec  codec;
    gint               crf;
    gint               bitrate_kbps;  /* > 0 selects target bitrate over CRF */
    gchar             *preset;        /* x264/x265 -preset word; NULL=default */
    gboolean           show_progress; /* emit ffmpeg -stats to stderr */
    gchar             *ffmpeg_path;   /* override, or NULL to auto-discover */
    gchar             *encoder;       /* -c:v override, or NULL to choose */
    gchar             *resolved;      /* encoder chosen by the last begin() */
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

/* ==========================================================================
 * Encoder discovery
 * ==========================================================================
 *
 * A codec names a format (H.264, VP9, ...); ffmpeg needs an encoder (libx264,
 * libopenh264, h264_nvenc, ...).  Which encoders exist depends on how the
 * ffmpeg on this machine was built: Fedora's packaged ffmpeg has libopenh264
 * and the hardware H.264 encoders but no libx264.  So begin() asks the ffmpeg
 * it is about to run which video encoders it has (`ffmpeg -hide_banner
 * -encoders`, once per ffmpeg path, cached for the life of the process) and
 * takes the first usable entry of the codec's candidate list.  Listed is not
 * the same as usable: hardware encoders are listed whenever ffmpeg was built
 * with them, even on machines without the hardware, and Fedora's ffmpeg
 * lists libopenh264 while the library installed by default is "noopenh264",
 * a stub that refuses to create an encoder (the real one is the openh264
 * package from the fedora-cisco-openh264 repository).  So every candidate
 * must also pass a one-frame trial encode (cached per ffmpeg and encoder)
 * before it is chosen.  An encoder named with set_encoder() is used as
 * named, without a trial.
 */

/* What rate-control and pixel-format flags an encoder takes. */
typedef enum
{
    ENC_X26X,      /* libx264 / libx265: -preset word, -crf */
    ENC_OPENH264,  /* libopenh264: bitrate only */
    ENC_NVENC,     /* *_nvenc: -preset pN, -cq */
    ENC_QSV,       /* *_qsv: -global_quality, nv12 input */
    ENC_VAAPI,     /* *_vaapi: device + hwupload, -qp */
    ENC_VPX,       /* libvpx(-vp9): -b:v 0 -crf */
    ENC_SVTAV1,    /* libsvtav1: numeric -preset, -crf */
    ENC_PRORES,    /* prores*: -profile:v */
    ENC_OTHER      /* anything else named by set_encoder(): bitrate only */
} EncoderKind;

/* The render node VA-API encoders open; the first GPU on most systems. */
#define REEL_VAAPI_DEVICE "/dev/dri/renderD128"

static const gchar * const h264_candidates[] =
    { "libx264", "libopenh264", "h264_nvenc", "h264_qsv", "h264_vaapi", NULL };
static const gchar * const h265_candidates[]  = { "libx265", NULL };
static const gchar * const vp9_candidates[]   = { "libvpx-vp9", NULL };
static const gchar * const av1_candidates[]   = { "libsvtav1", NULL };
static const gchar * const prores_candidates[] = { "prores_ks", NULL };

/* Probe caches, shared by every exporter: ffmpeg path -> set of video
 * encoder names, and "path\nencoder" -> trial result (1 works, 2 fails). */
static GMutex      probe_lock;
static GHashTable *probe_encoders;
static GHashTable *probe_trials;

static EncoderKind
reel_video_encoder_kind (const gchar *name)
{
    if (g_str_equal (name, "libx264") || g_str_equal (name, "libx265"))
        return ENC_X26X;
    if (g_str_equal (name, "libopenh264"))
        return ENC_OPENH264;
    if (g_str_has_suffix (name, "_nvenc"))
        return ENC_NVENC;
    if (g_str_has_suffix (name, "_qsv"))
        return ENC_QSV;
    if (g_str_has_suffix (name, "_vaapi"))
        return ENC_VAAPI;
    if (g_str_has_prefix (name, "libvpx"))
        return ENC_VPX;
    if (g_str_equal (name, "libsvtav1"))
        return ENC_SVTAV1;
    if (g_str_has_prefix (name, "prores"))
        return ENC_PRORES;
    return ENC_OTHER;
}

static const gchar * const *
reel_video_codec_candidates (LrgReelVideoCodec codec)
{
    switch (codec)
    {
    case LRG_REEL_VIDEO_CODEC_VP9:
    case LRG_REEL_VIDEO_CODEC_VP9_ALPHA:
        return vp9_candidates;
    case LRG_REEL_VIDEO_CODEC_H265:
        return h265_candidates;
    case LRG_REEL_VIDEO_CODEC_PRORES:
    case LRG_REEL_VIDEO_CODEC_PRORES_ALPHA:
        return prores_candidates;
    case LRG_REEL_VIDEO_CODEC_AV1:
        return av1_candidates;
    case LRG_REEL_VIDEO_CODEC_H264:
    default:
        return h264_candidates;
    }
}

static const gchar *
reel_video_codec_label (LrgReelVideoCodec codec)
{
    switch (codec)
    {
    case LRG_REEL_VIDEO_CODEC_VP9:
    case LRG_REEL_VIDEO_CODEC_VP9_ALPHA:
        return "VP9";
    case LRG_REEL_VIDEO_CODEC_H265:
        return "H.265";
    case LRG_REEL_VIDEO_CODEC_PRORES:
    case LRG_REEL_VIDEO_CODEC_PRORES_ALPHA:
        return "ProRes";
    case LRG_REEL_VIDEO_CODEC_AV1:
        return "AV1";
    case LRG_REEL_VIDEO_CODEC_H264:
    default:
        return "H.264";
    }
}

/*
 * reel_video_parse_encoders:
 * @listing: the stdout of `ffmpeg -hide_banner -encoders`.
 *
 * Returns a new set (name -> name) of the video encoders in @listing.  Each
 * encoder line is " V....D name   description": a six-character capability
 * field whose first letter is V for video, then the name.  The legend above
 * the "------" line uses the same shape, so parsing starts after it.
 */
static GHashTable *
reel_video_parse_encoders (const gchar *listing)
{
    GHashTable *set = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
    g_auto(GStrv) lines = g_strsplit (listing != NULL ? listing : "", "\n", -1);
    gboolean in_list = FALSE;
    guint i;

    for (i = 0; lines[i] != NULL; i++)
    {
        const gchar *p = lines[i];
        const gchar *name;
        const gchar *name_end;

        while (*p == ' ')
            p++;
        if (!in_list)
        {
            in_list = g_str_has_prefix (p, "------");
            continue;
        }
        /* Capability field: six non-space characters, video first. */
        if (p[0] != 'V' || strlen (p) < 8 || p[6] != ' ')
            continue;
        name = p + 7;
        while (*name == ' ')
            name++;
        name_end = name;
        while (*name_end != '\0' && *name_end != ' ')
            name_end++;
        if (name_end > name)
        {
            gchar *n = g_strndup (name, (gsize) (name_end - name));

            g_hash_table_add (set, n);
        }
    }
    return set;
}

/*
 * reel_video_encoders:
 * @ffmpeg: absolute path of the ffmpeg to ask.
 *
 * Returns: (transfer none): the set of video encoders @ffmpeg was built with.
 * The set is empty when the listing could not be read; callers then fall back
 * to the codec's first candidate, as before encoder discovery existed.
 */
static GHashTable *
reel_video_encoders (const gchar *ffmpeg)
{
    GHashTable *set;

    g_mutex_lock (&probe_lock);
    if (probe_encoders == NULL)
        probe_encoders = g_hash_table_new_full (g_str_hash, g_str_equal, g_free,
                                                (GDestroyNotify) g_hash_table_unref);
    set = g_hash_table_lookup (probe_encoders, ffmpeg);
    if (set == NULL)
    {
        const gchar *argv[] = { ffmpeg, "-hide_banner", "-encoders", NULL };
        g_autofree gchar *out = NULL;
        gint status = 0;

        if (!g_spawn_sync (NULL, (gchar **) argv, NULL, G_SPAWN_STDERR_TO_DEV_NULL,
                           NULL, NULL, &out, NULL, &status, NULL)
            || !g_spawn_check_wait_status (status, NULL))
            g_clear_pointer (&out, g_free);
        set = reel_video_parse_encoders (out);
        g_hash_table_insert (probe_encoders, g_strdup (ffmpeg), set);
    }
    g_mutex_unlock (&probe_lock);
    return set;
}

/*
 * reel_video_encoder_works:
 *
 * Runs a one-frame trial encode with @encoder (64x64 black from lavfi into
 * the null muxer) and reports whether it succeeded.  The answer is cached
 * per ffmpeg path and encoder, so the trial runs once per process.
 */
static gboolean
reel_video_encoder_works (const gchar *ffmpeg,
                          const gchar *encoder)
{
    g_autofree gchar *key = g_strdup_printf ("%s\n%s", ffmpeg, encoder);
    gpointer cached;
    gboolean ok;

    g_mutex_lock (&probe_lock);
    if (probe_trials == NULL)
        probe_trials = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
    cached = g_hash_table_lookup (probe_trials, key);
    g_mutex_unlock (&probe_lock);
    if (cached != NULL)
        return GPOINTER_TO_INT (cached) == 1;

    {
        GPtrArray *argv = g_ptr_array_new ();
        gint status = 0;
        gboolean vaapi = reel_video_encoder_kind (encoder) == ENC_VAAPI;

        g_ptr_array_add (argv, (gpointer) ffmpeg);
        g_ptr_array_add (argv, (gpointer) "-hide_banner");
        g_ptr_array_add (argv, (gpointer) "-loglevel");
        g_ptr_array_add (argv, (gpointer) "error");
        if (vaapi)
        {
            g_ptr_array_add (argv, (gpointer) "-vaapi_device");
            g_ptr_array_add (argv, (gpointer) REEL_VAAPI_DEVICE);
        }
        g_ptr_array_add (argv, (gpointer) "-f");
        g_ptr_array_add (argv, (gpointer) "lavfi");
        g_ptr_array_add (argv, (gpointer) "-i");
        g_ptr_array_add (argv, (gpointer) "color=c=black:s=256x256:r=24:d=0.1");
        g_ptr_array_add (argv, (gpointer) "-frames:v");
        g_ptr_array_add (argv, (gpointer) "1");
        if (vaapi)
        {
            g_ptr_array_add (argv, (gpointer) "-vf");
            g_ptr_array_add (argv, (gpointer) "format=nv12,hwupload");
        }
        g_ptr_array_add (argv, (gpointer) "-c:v");
        g_ptr_array_add (argv, (gpointer) encoder);
        g_ptr_array_add (argv, (gpointer) "-f");
        g_ptr_array_add (argv, (gpointer) "null");
        g_ptr_array_add (argv, (gpointer) "-");
        g_ptr_array_add (argv, NULL);

        ok = g_spawn_sync (NULL, (gchar **) argv->pdata, NULL,
                           G_SPAWN_STDOUT_TO_DEV_NULL | G_SPAWN_STDERR_TO_DEV_NULL,
                           NULL, NULL, NULL, NULL, &status, NULL)
             && g_spawn_check_wait_status (status, NULL);
        g_ptr_array_free (argv, TRUE);
    }

    g_mutex_lock (&probe_lock);
    g_hash_table_insert (probe_trials, g_strdup (key), GINT_TO_POINTER (ok ? 1 : 2));
    g_mutex_unlock (&probe_lock);
    return ok;
}

/*
 * reel_video_resolve_encoder:
 *
 * Picks the encoder for this export: the set_encoder() override when there
 * is one (it must exist in @ffmpeg), otherwise the first candidate of the
 * codec that @ffmpeg lists and that passes a trial encode.  Fails with %LRG_REEL_EXPORTER_ERROR_ENCODER_NOT_FOUND, naming
 * every candidate tried, when none is usable.
 */
static gchar *
reel_video_resolve_encoder (LrgReelVideoExporter *self,
                            const gchar          *ffmpeg,
                            GError              **error)
{
    GHashTable *have = reel_video_encoders (ffmpeg);
    const gchar * const *cands = reel_video_codec_candidates (self->codec);
    g_autofree gchar *tried = NULL;
    guint i;

    /* An empty listing means the probe itself failed (a wrapper script, a
     * very old ffmpeg): keep the pre-discovery behaviour and let ffmpeg
     * report a missing encoder itself. */
    if (g_hash_table_size (have) == 0)
        return g_strdup (self->encoder != NULL ? self->encoder : cands[0]);

    if (self->encoder != NULL)
    {
        if (g_hash_table_contains (have, self->encoder))
            return g_strdup (self->encoder);
        g_set_error (error, LRG_REEL_EXPORTER_ERROR,
                     LRG_REEL_EXPORTER_ERROR_ENCODER_NOT_FOUND,
                     "%s has no video encoder named '%s' "
                     "(see `ffmpeg -hide_banner -encoders`)",
                     ffmpeg, self->encoder);
        return NULL;
    }

    for (i = 0; cands[i] != NULL; i++)
    {
        if (!g_hash_table_contains (have, cands[i]))
            continue;
        if (!reel_video_encoder_works (ffmpeg, cands[i]))
            continue;
        return g_strdup (cands[i]);
    }

    tried = g_strjoinv (", ", (gchar **) cands);
    g_set_error (error, LRG_REEL_EXPORTER_ERROR,
                 LRG_REEL_EXPORTER_ERROR_ENCODER_NOT_FOUND,
                 "%s has no usable %s encoder (tried %s); install an ffmpeg "
                 "built with one, pick another codec, or name an encoder with "
                 "lrg_reel_video_exporter_set_encoder()",
                 ffmpeg, reel_video_codec_label (self->codec), tried);
    return NULL;
}

/*
 * reel_video_openh264_kbps:
 *
 * libopenh264 has no constant-quality mode, so a CRF request is turned into
 * a bitrate: 0.1 bits per pixel per frame at CRF 23, doubling for every 6
 * CRF steps down (and halving for every 6 up), the same slope x264's CRF
 * scale has.  1280x720 at 24 fps and CRF 23 comes to about 2200 kbit/s.
 */
static gint
reel_video_openh264_kbps (gint    width,
                          gint    height,
                          gdouble fps,
                          gint    crf)
{
    gdouble bits;

    bits = (gdouble) width * (gdouble) height * fps * 0.1
           * pow (2.0, (23.0 - (gdouble) crf) / 6.0);
    return MAX (100, (gint) (bits / 1000.0 + 0.5));
}

/* Build the ffmpeg argv. Returns a NULL-terminated, owned string vector. */
static gchar **
reel_video_build_argv (LrgReelVideoExporter *self,
                       const gchar          *ffmpeg,
                       const gchar          *encoder,
                       gint                  width,
                       gint                  height,
                       gdouble               fps)
{
    GPtrArray   *args = g_ptr_array_new ();
    EncoderKind  kind = reel_video_encoder_kind (encoder);

    g_ptr_array_add (args, g_strdup (ffmpeg));
    g_ptr_array_add (args, g_strdup ("-y"));
    /* Keep real errors + the banner suppressed; per-frame stats are opt-in
       (a caller monitoring progress enables them). */
    g_ptr_array_add (args, g_strdup ("-loglevel"));
    g_ptr_array_add (args, g_strdup ("error"));
    g_ptr_array_add (args, g_strdup (self->show_progress ? "-stats"
                                                        : "-nostats"));

    /* VA-API encoders need the device opened before the inputs. */
    if (kind == ENC_VAAPI)
    {
        g_ptr_array_add (args, g_strdup ("-vaapi_device"));
        g_ptr_array_add (args, g_strdup (REEL_VAAPI_DEVICE));
    }

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

    /* Video encoder, pixel format and rate control. */
    {
        const gchar *pix_fmt = "yuv420p";
        gboolean     is_webm = FALSE;
        gint         prores_profile = 3;

        switch (self->codec)
        {
        case LRG_REEL_VIDEO_CODEC_VP9:
            is_webm = TRUE;
            break;
        case LRG_REEL_VIDEO_CODEC_VP9_ALPHA:
            pix_fmt = "yuva420p"; is_webm = TRUE;
            break;
        case LRG_REEL_VIDEO_CODEC_PRORES:
            pix_fmt = "yuv422p10le"; prores_profile = 3;
            break;
        case LRG_REEL_VIDEO_CODEC_PRORES_ALPHA:
            pix_fmt = "yuva444p10le"; prores_profile = 4;
            break;
        case LRG_REEL_VIDEO_CODEC_H265:
        case LRG_REEL_VIDEO_CODEC_AV1:
        case LRG_REEL_VIDEO_CODEC_H264:
        default:
            break;
        }

        g_ptr_array_add (args, g_strdup ("-c:v"));
        g_ptr_array_add (args, g_strdup (encoder));

        /* Hardware encoders take frames in their own formats: QSV wants
         * NV12, VA-API wants NV12 uploaded to a GPU surface. */
        if (kind == ENC_VAAPI)
        {
            g_ptr_array_add (args, g_strdup ("-vf"));
            g_ptr_array_add (args, g_strdup ("format=nv12,hwupload"));
        }
        else
        {
            g_ptr_array_add (args, g_strdup ("-pix_fmt"));
            g_ptr_array_add (args, g_strdup (kind == ENC_QSV ? "nv12" : pix_fmt));
        }

        if (kind == ENC_PRORES)
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
            /* Constant quality, expressed in each encoder's own terms. */
            switch (kind)
            {
            case ENC_X26X:
                g_ptr_array_add (args, g_strdup ("-preset"));
                g_ptr_array_add (args, g_strdup (self->preset ? self->preset
                                                             : "veryfast"));
                g_ptr_array_add (args, g_strdup ("-crf"));
                g_ptr_array_add (args, g_strdup_printf ("%d", self->crf));
                break;
            case ENC_VPX:
                g_ptr_array_add (args, g_strdup ("-b:v"));
                g_ptr_array_add (args, g_strdup ("0"));
                g_ptr_array_add (args, g_strdup ("-crf"));
                g_ptr_array_add (args, g_strdup_printf ("%d", self->crf));
                break;
            case ENC_SVTAV1:
                /* SVT-AV1 presets are numeric (0 slowest .. 13 fastest). */
                g_ptr_array_add (args, g_strdup ("-preset"));
                g_ptr_array_add (args, g_strdup ("8"));
                g_ptr_array_add (args, g_strdup ("-crf"));
                g_ptr_array_add (args, g_strdup_printf ("%d", self->crf));
                break;
            case ENC_OPENH264:
                g_ptr_array_add (args, g_strdup ("-b:v"));
                g_ptr_array_add (args, g_strdup_printf (
                    "%dk", reel_video_openh264_kbps (width, height, fps, self->crf)));
                break;
            case ENC_NVENC:
                g_ptr_array_add (args, g_strdup ("-preset"));
                g_ptr_array_add (args, g_strdup ("p4"));
                g_ptr_array_add (args, g_strdup ("-rc"));
                g_ptr_array_add (args, g_strdup ("vbr"));
                g_ptr_array_add (args, g_strdup ("-cq"));
                g_ptr_array_add (args, g_strdup_printf ("%d", self->crf));
                g_ptr_array_add (args, g_strdup ("-b:v"));
                g_ptr_array_add (args, g_strdup ("0"));
                break;
            case ENC_QSV:
                g_ptr_array_add (args, g_strdup ("-global_quality"));
                g_ptr_array_add (args, g_strdup_printf ("%d", self->crf));
                break;
            case ENC_VAAPI:
                g_ptr_array_add (args, g_strdup ("-rc_mode"));
                g_ptr_array_add (args, g_strdup ("CQP"));
                g_ptr_array_add (args, g_strdup ("-qp"));
                g_ptr_array_add (args, g_strdup_printf ("%d", self->crf));
                break;
            case ENC_PRORES:
            case ENC_OTHER:
            default:
                /* Unknown encoder named by the caller: its own defaults. */
                break;
            }
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

    /* Choose the encoder this ffmpeg can actually run (see above). */
    g_clear_pointer (&self->resolved, g_free);
    self->resolved = reel_video_resolve_encoder (self, ffmpeg, error);
    if (self->resolved == NULL)
        return FALSE;

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

    argv = reel_video_build_argv (self, ffmpeg, self->resolved,
                                  width, height, fps);

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
    g_clear_pointer (&self->preset, g_free);
    g_clear_pointer (&self->encoder, g_free);
    g_clear_pointer (&self->resolved, g_free);

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
lrg_reel_video_exporter_set_preset (LrgReelVideoExporter *self,
                                    const gchar          *preset)
{
    g_return_if_fail (LRG_IS_REEL_VIDEO_EXPORTER (self));
    g_clear_pointer (&self->preset, g_free);
    self->preset = (preset && preset[0]) ? g_strdup (preset) : NULL;
}

void
lrg_reel_video_exporter_set_show_progress (LrgReelVideoExporter *self,
                                           gboolean              show)
{
    g_return_if_fail (LRG_IS_REEL_VIDEO_EXPORTER (self));
    self->show_progress = show;
}

void
lrg_reel_video_exporter_set_ffmpeg_path (LrgReelVideoExporter *self,
                                         const gchar          *path)
{
    g_return_if_fail (LRG_IS_REEL_VIDEO_EXPORTER (self));

    g_free (self->ffmpeg_path);
    self->ffmpeg_path = g_strdup (path);
}

/**
 * lrg_reel_video_exporter_set_encoder:
 * @self: a #LrgReelVideoExporter
 * @encoder: (nullable): an ffmpeg video encoder name ("libopenh264",
 *   "h264_nvenc", ...), or %NULL to choose one from the codec.
 *
 * Names the ffmpeg encoder to use instead of letting the exporter choose.
 * The name must be in `ffmpeg -hide_banner -encoders` of the ffmpeg that
 * runs, or lrg_reel_exporter_begin() fails with
 * %LRG_REEL_EXPORTER_ERROR_ENCODER_NOT_FOUND.  Rate control follows the
 * encoder's family (see the exporters documentation); an encoder the
 * exporter does not know gets only a bitrate when one is set.
 *
 * Since: 0.13.7
 */
void
lrg_reel_video_exporter_set_encoder (LrgReelVideoExporter *self,
                                     const gchar          *encoder)
{
    g_return_if_fail (LRG_IS_REEL_VIDEO_EXPORTER (self));

    g_clear_pointer (&self->encoder, g_free);
    self->encoder = (encoder != NULL && encoder[0] != '\0') ? g_strdup (encoder) : NULL;
}

/**
 * lrg_reel_video_exporter_get_encoder:
 * @self: a #LrgReelVideoExporter
 *
 * Returns the encoder the last lrg_reel_exporter_begin() chose, or, before
 * any begin, the one set with lrg_reel_video_exporter_set_encoder().
 *
 * Returns: (nullable): the encoder name, or %NULL when nothing was set and
 *   no export has begun.
 *
 * Since: 0.13.7
 */
const gchar *
lrg_reel_video_exporter_get_encoder (LrgReelVideoExporter *self)
{
    g_return_val_if_fail (LRG_IS_REEL_VIDEO_EXPORTER (self), NULL);

    return self->resolved != NULL ? self->resolved : self->encoder;
}

gboolean
lrg_reel_video_exporter_is_ffmpeg_available (void)
{
    g_autofree gchar *p = g_find_program_in_path ("ffmpeg");

    return p != NULL;
}
