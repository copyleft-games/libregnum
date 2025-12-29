/* lrg-video-subtitle-track.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "config.h"
#include "lrg-video-subtitle-track.h"
#include <gio/gio.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/**
 * SECTION:lrg-video-subtitle-track
 * @Title: LrgVideoSubtitleTrack
 * @Short_description: Subtitle track with cue parsing
 *
 * #LrgVideoSubtitleTrack holds subtitle cues loaded from SRT or WebVTT files.
 * Each cue contains timing information and text content.
 */

/* LrgSubtitleCue boxed type */
struct _LrgSubtitleCue
{
    gdouble  start_time;
    gdouble  end_time;
    gchar   *text;
};

G_DEFINE_BOXED_TYPE (LrgSubtitleCue, lrg_subtitle_cue,
                     lrg_subtitle_cue_copy, lrg_subtitle_cue_free)

LrgSubtitleCue *
lrg_subtitle_cue_new (gdouble      start_time,
                      gdouble      end_time,
                      const gchar *text)
{
    LrgSubtitleCue *cue;

    cue = g_slice_new0 (LrgSubtitleCue);
    cue->start_time = start_time;
    cue->end_time = end_time;
    cue->text = g_strdup (text != NULL ? text : "");

    return cue;
}

LrgSubtitleCue *
lrg_subtitle_cue_copy (const LrgSubtitleCue *cue)
{
    g_return_val_if_fail (cue != NULL, NULL);
    return lrg_subtitle_cue_new (cue->start_time, cue->end_time, cue->text);
}

void
lrg_subtitle_cue_free (LrgSubtitleCue *cue)
{
    if (cue != NULL)
    {
        g_free (cue->text);
        g_slice_free (LrgSubtitleCue, cue);
    }
}

gdouble
lrg_subtitle_cue_get_start_time (const LrgSubtitleCue *cue)
{
    g_return_val_if_fail (cue != NULL, 0.0);
    return cue->start_time;
}

gdouble
lrg_subtitle_cue_get_end_time (const LrgSubtitleCue *cue)
{
    g_return_val_if_fail (cue != NULL, 0.0);
    return cue->end_time;
}

const gchar *
lrg_subtitle_cue_get_text (const LrgSubtitleCue *cue)
{
    g_return_val_if_fail (cue != NULL, NULL);
    return cue->text;
}

gboolean
lrg_subtitle_cue_contains_time (const LrgSubtitleCue *cue,
                                gdouble               time)
{
    g_return_val_if_fail (cue != NULL, FALSE);
    /* Range is [start, end) - end time is exclusive */
    return time >= cue->start_time && time < cue->end_time;
}

/* LrgVideoSubtitleTrack */
struct _LrgVideoSubtitleTrack
{
    GObject    parent_instance;

    GPtrArray *cues;
    gchar     *language;
};

G_DEFINE_TYPE (LrgVideoSubtitleTrack, lrg_video_subtitle_track, G_TYPE_OBJECT)

enum
{
    PROP_0,
    PROP_LANGUAGE,
    PROP_CUE_COUNT,
    PROP_DURATION,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

static void
lrg_video_subtitle_track_finalize (GObject *object)
{
    LrgVideoSubtitleTrack *self = LRG_VIDEO_SUBTITLE_TRACK (object);

    g_ptr_array_unref (self->cues);
    g_free (self->language);

    G_OBJECT_CLASS (lrg_video_subtitle_track_parent_class)->finalize (object);
}

static void
lrg_video_subtitle_track_get_property (GObject    *object,
                                       guint       prop_id,
                                       GValue     *value,
                                       GParamSpec *pspec)
{
    LrgVideoSubtitleTrack *self = LRG_VIDEO_SUBTITLE_TRACK (object);

    switch (prop_id)
    {
        case PROP_LANGUAGE:
            g_value_set_string (value, self->language);
            break;
        case PROP_CUE_COUNT:
            g_value_set_uint (value, self->cues->len);
            break;
        case PROP_DURATION:
            g_value_set_double (value, lrg_video_subtitle_track_get_duration (self));
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_video_subtitle_track_set_property (GObject      *object,
                                       guint         prop_id,
                                       const GValue *value,
                                       GParamSpec   *pspec)
{
    LrgVideoSubtitleTrack *self = LRG_VIDEO_SUBTITLE_TRACK (object);

    switch (prop_id)
    {
        case PROP_LANGUAGE:
            g_free (self->language);
            self->language = g_value_dup_string (value);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_video_subtitle_track_class_init (LrgVideoSubtitleTrackClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = lrg_video_subtitle_track_finalize;
    object_class->get_property = lrg_video_subtitle_track_get_property;
    object_class->set_property = lrg_video_subtitle_track_set_property;

    properties[PROP_LANGUAGE] =
        g_param_spec_string ("language",
                             "Language",
                             "Language code (e.g., 'en', 'es')",
                             NULL,
                             G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_CUE_COUNT] =
        g_param_spec_uint ("cue-count",
                           "Cue Count",
                           "Number of cues in the track",
                           0, G_MAXUINT, 0,
                           G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

    properties[PROP_DURATION] =
        g_param_spec_double ("duration",
                             "Duration",
                             "Total duration based on last cue",
                             0.0, G_MAXDOUBLE, 0.0,
                             G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_video_subtitle_track_init (LrgVideoSubtitleTrack *self)
{
    self->cues = g_ptr_array_new_with_free_func ((GDestroyNotify) lrg_subtitle_cue_free);
    self->language = NULL;
}

LrgVideoSubtitleTrack *
lrg_video_subtitle_track_new (void)
{
    return g_object_new (LRG_TYPE_VIDEO_SUBTITLE_TRACK, NULL);
}

/*
 * parse_srt_time:
 * Parses SRT time format: HH:MM:SS,mmm
 */
static gdouble
parse_srt_time (const gchar *str)
{
    guint hours, minutes, seconds, millis;

    hours = 0;
    minutes = 0;
    seconds = 0;
    millis = 0;

    if (sscanf (str, "%u:%u:%u,%u", &hours, &minutes, &seconds, &millis) == 4 ||
        sscanf (str, "%u:%u:%u.%u", &hours, &minutes, &seconds, &millis) == 4)
    {
        return (gdouble) hours * 3600.0 +
               (gdouble) minutes * 60.0 +
               (gdouble) seconds +
               (gdouble) millis / 1000.0;
    }

    return 0.0;
}

/*
 * parse_vtt_time:
 * Parses VTT time format: HH:MM:SS.mmm or MM:SS.mmm
 */
static gdouble
parse_vtt_time (const gchar *str)
{
    guint hours, minutes, seconds, millis;
    gint result;

    hours = 0;
    minutes = 0;
    seconds = 0;
    millis = 0;

    /* Try HH:MM:SS.mmm format */
    result = sscanf (str, "%u:%u:%u.%u", &hours, &minutes, &seconds, &millis);
    if (result == 4)
    {
        return (gdouble) hours * 3600.0 +
               (gdouble) minutes * 60.0 +
               (gdouble) seconds +
               (gdouble) millis / 1000.0;
    }

    /* Try MM:SS.mmm format */
    result = sscanf (str, "%u:%u.%u", &minutes, &seconds, &millis);
    if (result == 3)
    {
        return (gdouble) minutes * 60.0 +
               (gdouble) seconds +
               (gdouble) millis / 1000.0;
    }

    return 0.0;
}

/*
 * strip_html_tags:
 * Removes basic HTML tags from subtitle text.
 */
static gchar *
strip_html_tags (const gchar *text)
{
    GString *result;
    const gchar *p;
    gboolean in_tag;

    result = g_string_new (NULL);
    in_tag = FALSE;

    for (p = text; *p != '\0'; p++)
    {
        if (*p == '<')
        {
            in_tag = TRUE;
        }
        else if (*p == '>')
        {
            in_tag = FALSE;
        }
        else if (!in_tag)
        {
            g_string_append_c (result, *p);
        }
    }

    return g_string_free (result, FALSE);
}

gboolean
lrg_video_subtitle_track_load_srt (LrgVideoSubtitleTrack  *track,
                                   const gchar            *path,
                                   GError                **error)
{
    gchar *contents;
    gboolean result;

    g_return_val_if_fail (LRG_IS_VIDEO_SUBTITLE_TRACK (track), FALSE);
    g_return_val_if_fail (path != NULL, FALSE);

    if (!g_file_get_contents (path, &contents, NULL, error))
        return FALSE;

    result = lrg_video_subtitle_track_load_data (track, contents, "srt", error);
    g_free (contents);

    return result;
}

gboolean
lrg_video_subtitle_track_load_vtt (LrgVideoSubtitleTrack  *track,
                                   const gchar            *path,
                                   GError                **error)
{
    gchar *contents;
    gboolean result;

    g_return_val_if_fail (LRG_IS_VIDEO_SUBTITLE_TRACK (track), FALSE);
    g_return_val_if_fail (path != NULL, FALSE);

    if (!g_file_get_contents (path, &contents, NULL, error))
        return FALSE;

    result = lrg_video_subtitle_track_load_data (track, contents, "vtt", error);
    g_free (contents);

    return result;
}

gboolean
lrg_video_subtitle_track_load_data (LrgVideoSubtitleTrack  *track,
                                    const gchar            *data,
                                    const gchar            *format,
                                    GError                **error)
{
    gchar **lines;
    gboolean is_vtt;
    guint i;
    gdouble start_time;
    gdouble end_time;
    GString *text_buffer;
    gboolean in_cue;

    g_return_val_if_fail (LRG_IS_VIDEO_SUBTITLE_TRACK (track), FALSE);
    g_return_val_if_fail (data != NULL, FALSE);
    g_return_val_if_fail (format != NULL, FALSE);

    /* Clear existing cues */
    lrg_video_subtitle_track_clear (track);

    is_vtt = (g_strcmp0 (format, "vtt") == 0);
    lines = g_strsplit (data, "\n", -1);
    text_buffer = g_string_new (NULL);
    in_cue = FALSE;
    start_time = 0.0;
    end_time = 0.0;

    for (i = 0; lines[i] != NULL; i++)
    {
        gchar *line;
        gchar *stripped;

        line = lines[i];

        /* Remove carriage return if present */
        stripped = g_strchomp (g_strdup (line));

        /* Skip WEBVTT header */
        if (is_vtt && i == 0 && g_str_has_prefix (stripped, "WEBVTT"))
        {
            g_free (stripped);
            continue;
        }

        /* Check for timing line */
        if (strstr (stripped, "-->") != NULL)
        {
            gchar **parts;
            gchar *arrow_pos;

            /* Save previous cue if any */
            if (in_cue && text_buffer->len > 0)
            {
                gchar *clean_text;

                clean_text = strip_html_tags (text_buffer->str);
                g_strstrip (clean_text);
                if (clean_text[0] != '\0')
                {
                    LrgSubtitleCue *cue;

                    cue = lrg_subtitle_cue_new (start_time, end_time, clean_text);
                    g_ptr_array_add (track->cues, cue);
                }
                g_free (clean_text);
                g_string_truncate (text_buffer, 0);
            }

            /* Parse timing */
            arrow_pos = strstr (stripped, "-->");
            if (arrow_pos != NULL)
            {
                gchar *start_str;
                gchar *end_str;

                *arrow_pos = '\0';
                start_str = g_strstrip (g_strdup (stripped));
                end_str = g_strstrip (g_strdup (arrow_pos + 3));

                /* Remove any position/styling info after end time (VTT) */
                parts = g_strsplit (end_str, " ", 2);
                g_free (end_str);
                end_str = g_strdup (parts[0]);
                g_strfreev (parts);

                if (is_vtt)
                {
                    start_time = parse_vtt_time (start_str);
                    end_time = parse_vtt_time (end_str);
                }
                else
                {
                    start_time = parse_srt_time (start_str);
                    end_time = parse_srt_time (end_str);
                }

                g_free (start_str);
                g_free (end_str);
            }

            in_cue = TRUE;
        }
        /* Empty line ends a cue */
        else if (stripped[0] == '\0')
        {
            if (in_cue && text_buffer->len > 0)
            {
                gchar *clean_text;

                clean_text = strip_html_tags (text_buffer->str);
                g_strstrip (clean_text);
                if (clean_text[0] != '\0')
                {
                    LrgSubtitleCue *cue;

                    cue = lrg_subtitle_cue_new (start_time, end_time, clean_text);
                    g_ptr_array_add (track->cues, cue);
                }
                g_free (clean_text);
                g_string_truncate (text_buffer, 0);
            }
            in_cue = FALSE;
        }
        /* Text content */
        else if (in_cue)
        {
            /* Skip cue numbers in SRT */
            if (!is_vtt)
            {
                gchar *endptr;
                g_ascii_strtoll (stripped, &endptr, 10);
                if (*endptr == '\0')
                {
                    g_free (stripped);
                    continue;
                }
            }

            if (text_buffer->len > 0)
                g_string_append_c (text_buffer, '\n');
            g_string_append (text_buffer, stripped);
        }

        g_free (stripped);
    }

    /* Handle final cue */
    if (in_cue && text_buffer->len > 0)
    {
        gchar *clean_text;

        clean_text = strip_html_tags (text_buffer->str);
        g_strstrip (clean_text);
        if (clean_text[0] != '\0')
        {
            LrgSubtitleCue *cue;

            cue = lrg_subtitle_cue_new (start_time, end_time, clean_text);
            g_ptr_array_add (track->cues, cue);
        }
        g_free (clean_text);
    }

    g_string_free (text_buffer, TRUE);
    g_strfreev (lines);

    return TRUE;
}

void
lrg_video_subtitle_track_add_cue (LrgVideoSubtitleTrack *track,
                                  LrgSubtitleCue        *cue)
{
    g_return_if_fail (LRG_IS_VIDEO_SUBTITLE_TRACK (track));
    g_return_if_fail (cue != NULL);

    g_ptr_array_add (track->cues, cue);
    g_object_notify_by_pspec (G_OBJECT (track), properties[PROP_CUE_COUNT]);
    g_object_notify_by_pspec (G_OBJECT (track), properties[PROP_DURATION]);
}

void
lrg_video_subtitle_track_clear (LrgVideoSubtitleTrack *track)
{
    g_return_if_fail (LRG_IS_VIDEO_SUBTITLE_TRACK (track));

    g_ptr_array_set_size (track->cues, 0);
    g_object_notify_by_pspec (G_OBJECT (track), properties[PROP_CUE_COUNT]);
    g_object_notify_by_pspec (G_OBJECT (track), properties[PROP_DURATION]);
}

guint
lrg_video_subtitle_track_get_cue_count (LrgVideoSubtitleTrack *track)
{
    g_return_val_if_fail (LRG_IS_VIDEO_SUBTITLE_TRACK (track), 0);
    return track->cues->len;
}

const LrgSubtitleCue *
lrg_video_subtitle_track_get_cue (LrgVideoSubtitleTrack *track,
                                  guint                  index)
{
    g_return_val_if_fail (LRG_IS_VIDEO_SUBTITLE_TRACK (track), NULL);

    if (index >= track->cues->len)
        return NULL;

    return g_ptr_array_index (track->cues, index);
}

gchar *
lrg_video_subtitle_track_get_text_at (LrgVideoSubtitleTrack *track,
                                      gdouble                time)
{
    GString *result;
    guint i;

    g_return_val_if_fail (LRG_IS_VIDEO_SUBTITLE_TRACK (track), NULL);

    result = g_string_new (NULL);

    for (i = 0; i < track->cues->len; i++)
    {
        const LrgSubtitleCue *cue;

        cue = g_ptr_array_index (track->cues, i);

        if (lrg_subtitle_cue_contains_time (cue, time))
        {
            if (result->len > 0)
                g_string_append_c (result, '\n');
            g_string_append (result, cue->text);
        }
        /* Optimization: if we've passed all active cues, stop */
        else if (cue->start_time > time)
        {
            break;
        }
    }

    if (result->len == 0)
    {
        g_string_free (result, TRUE);
        return NULL;
    }

    return g_string_free (result, FALSE);
}

GPtrArray *
lrg_video_subtitle_track_get_cues_at (LrgVideoSubtitleTrack *track,
                                      gdouble                time)
{
    GPtrArray *result;
    guint i;

    g_return_val_if_fail (LRG_IS_VIDEO_SUBTITLE_TRACK (track), NULL);

    result = g_ptr_array_new ();

    for (i = 0; i < track->cues->len; i++)
    {
        const LrgSubtitleCue *cue;

        cue = g_ptr_array_index (track->cues, i);

        if (lrg_subtitle_cue_contains_time (cue, time))
        {
            g_ptr_array_add (result, (gpointer) cue);
        }
        else if (cue->start_time > time)
        {
            break;
        }
    }

    return result;
}

gdouble
lrg_video_subtitle_track_get_duration (LrgVideoSubtitleTrack *track)
{
    const LrgSubtitleCue *last_cue;

    g_return_val_if_fail (LRG_IS_VIDEO_SUBTITLE_TRACK (track), 0.0);

    if (track->cues->len == 0)
        return 0.0;

    last_cue = g_ptr_array_index (track->cues, track->cues->len - 1);
    return last_cue->end_time;
}

const gchar *
lrg_video_subtitle_track_get_language (LrgVideoSubtitleTrack *track)
{
    g_return_val_if_fail (LRG_IS_VIDEO_SUBTITLE_TRACK (track), NULL);
    return track->language;
}

void
lrg_video_subtitle_track_set_language (LrgVideoSubtitleTrack *track,
                                       const gchar           *language)
{
    g_return_if_fail (LRG_IS_VIDEO_SUBTITLE_TRACK (track));

    if (g_strcmp0 (track->language, language) != 0)
    {
        g_free (track->language);
        track->language = g_strdup (language);
        g_object_notify_by_pspec (G_OBJECT (track), properties[PROP_LANGUAGE]);
    }
}
