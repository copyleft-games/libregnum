/* lrg-video-subtitles.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "config.h"
#include "lrg-video-subtitles.h"
#include <string.h>

/**
 * SECTION:lrg-video-subtitles
 * @Title: LrgVideoSubtitles
 * @Short_description: Subtitle rendering
 *
 * #LrgVideoSubtitles renders subtitle text on screen with customizable
 * appearance including font size, color, position, and background.
 */

struct _LrgVideoSubtitles
{
    GObject                parent_instance;

    LrgVideoSubtitleTrack *track;
    gboolean               visible;
    LrgSubtitlePosition    position;
    gfloat                 font_size;
    guint8                 color_r;
    guint8                 color_g;
    guint8                 color_b;
    guint8                 color_a;
    gboolean               background;
    gfloat                 margin;
    gchar                 *current_text;
};

G_DEFINE_TYPE (LrgVideoSubtitles, lrg_video_subtitles, G_TYPE_OBJECT)

enum
{
    PROP_0,
    PROP_TRACK,
    PROP_VISIBLE,
    PROP_POSITION,
    PROP_FONT_SIZE,
    PROP_BACKGROUND,
    PROP_MARGIN,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

static void
lrg_video_subtitles_dispose (GObject *object)
{
    LrgVideoSubtitles *self = LRG_VIDEO_SUBTITLES (object);

    g_clear_object (&self->track);

    G_OBJECT_CLASS (lrg_video_subtitles_parent_class)->dispose (object);
}

static void
lrg_video_subtitles_finalize (GObject *object)
{
    LrgVideoSubtitles *self = LRG_VIDEO_SUBTITLES (object);

    g_free (self->current_text);

    G_OBJECT_CLASS (lrg_video_subtitles_parent_class)->finalize (object);
}

static void
lrg_video_subtitles_get_property (GObject    *object,
                                  guint       prop_id,
                                  GValue     *value,
                                  GParamSpec *pspec)
{
    LrgVideoSubtitles *self = LRG_VIDEO_SUBTITLES (object);

    switch (prop_id)
    {
        case PROP_TRACK:
            g_value_set_object (value, self->track);
            break;
        case PROP_VISIBLE:
            g_value_set_boolean (value, self->visible);
            break;
        case PROP_POSITION:
            g_value_set_enum (value, self->position);
            break;
        case PROP_FONT_SIZE:
            g_value_set_float (value, self->font_size);
            break;
        case PROP_BACKGROUND:
            g_value_set_boolean (value, self->background);
            break;
        case PROP_MARGIN:
            g_value_set_float (value, self->margin);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_video_subtitles_set_property (GObject      *object,
                                  guint         prop_id,
                                  const GValue *value,
                                  GParamSpec   *pspec)
{
    LrgVideoSubtitles *self = LRG_VIDEO_SUBTITLES (object);

    switch (prop_id)
    {
        case PROP_TRACK:
            lrg_video_subtitles_set_track (self, g_value_get_object (value));
            break;
        case PROP_VISIBLE:
            lrg_video_subtitles_set_visible (self, g_value_get_boolean (value));
            break;
        case PROP_POSITION:
            lrg_video_subtitles_set_position (self, g_value_get_enum (value));
            break;
        case PROP_FONT_SIZE:
            lrg_video_subtitles_set_font_size (self, g_value_get_float (value));
            break;
        case PROP_BACKGROUND:
            lrg_video_subtitles_set_background (self, g_value_get_boolean (value));
            break;
        case PROP_MARGIN:
            lrg_video_subtitles_set_margin (self, g_value_get_float (value));
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_video_subtitles_class_init (LrgVideoSubtitlesClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->dispose = lrg_video_subtitles_dispose;
    object_class->finalize = lrg_video_subtitles_finalize;
    object_class->get_property = lrg_video_subtitles_get_property;
    object_class->set_property = lrg_video_subtitles_set_property;

    properties[PROP_TRACK] =
        g_param_spec_object ("track",
                             "Track",
                             "Subtitle track to render",
                             LRG_TYPE_VIDEO_SUBTITLE_TRACK,
                             G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_VISIBLE] =
        g_param_spec_boolean ("visible",
                              "Visible",
                              "Whether subtitles are visible",
                              TRUE,
                              G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_POSITION] =
        g_param_spec_enum ("position",
                           "Position",
                           "Subtitle position on screen",
                           LRG_TYPE_SUBTITLE_POSITION,
                           LRG_SUBTITLE_POSITION_BOTTOM,
                           G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_FONT_SIZE] =
        g_param_spec_float ("font-size",
                            "Font Size",
                            "Subtitle font size in pixels",
                            8.0f, 200.0f, 24.0f,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_BACKGROUND] =
        g_param_spec_boolean ("background",
                              "Background",
                              "Show background behind subtitles",
                              TRUE,
                              G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_MARGIN] =
        g_param_spec_float ("margin",
                            "Margin",
                            "Margin from screen edge",
                            0.0f, 500.0f, 50.0f,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_video_subtitles_init (LrgVideoSubtitles *self)
{
    self->track = NULL;
    self->visible = TRUE;
    self->position = LRG_SUBTITLE_POSITION_BOTTOM;
    self->font_size = 24.0f;
    self->color_r = 255;
    self->color_g = 255;
    self->color_b = 255;
    self->color_a = 255;
    self->background = TRUE;
    self->margin = 50.0f;
    self->current_text = NULL;
}

LrgVideoSubtitles *
lrg_video_subtitles_new (void)
{
    return g_object_new (LRG_TYPE_VIDEO_SUBTITLES, NULL);
}

void
lrg_video_subtitles_set_track (LrgVideoSubtitles     *subtitles,
                               LrgVideoSubtitleTrack *track)
{
    g_return_if_fail (LRG_IS_VIDEO_SUBTITLES (subtitles));
    g_return_if_fail (track == NULL || LRG_IS_VIDEO_SUBTITLE_TRACK (track));

    if (g_set_object (&subtitles->track, track))
    {
        g_clear_pointer (&subtitles->current_text, g_free);
        g_object_notify_by_pspec (G_OBJECT (subtitles), properties[PROP_TRACK]);
    }
}

LrgVideoSubtitleTrack *
lrg_video_subtitles_get_track (LrgVideoSubtitles *subtitles)
{
    g_return_val_if_fail (LRG_IS_VIDEO_SUBTITLES (subtitles), NULL);
    return subtitles->track;
}

void
lrg_video_subtitles_set_visible (LrgVideoSubtitles *subtitles,
                                 gboolean           visible)
{
    g_return_if_fail (LRG_IS_VIDEO_SUBTITLES (subtitles));

    if (subtitles->visible != visible)
    {
        subtitles->visible = visible;
        g_object_notify_by_pspec (G_OBJECT (subtitles), properties[PROP_VISIBLE]);
    }
}

gboolean
lrg_video_subtitles_get_visible (LrgVideoSubtitles *subtitles)
{
    g_return_val_if_fail (LRG_IS_VIDEO_SUBTITLES (subtitles), FALSE);
    return subtitles->visible;
}

void
lrg_video_subtitles_set_position (LrgVideoSubtitles   *subtitles,
                                  LrgSubtitlePosition  position)
{
    g_return_if_fail (LRG_IS_VIDEO_SUBTITLES (subtitles));

    if (subtitles->position != position)
    {
        subtitles->position = position;
        g_object_notify_by_pspec (G_OBJECT (subtitles), properties[PROP_POSITION]);
    }
}

LrgSubtitlePosition
lrg_video_subtitles_get_position (LrgVideoSubtitles *subtitles)
{
    g_return_val_if_fail (LRG_IS_VIDEO_SUBTITLES (subtitles), LRG_SUBTITLE_POSITION_BOTTOM);
    return subtitles->position;
}

void
lrg_video_subtitles_set_font_size (LrgVideoSubtitles *subtitles,
                                   gfloat             size)
{
    g_return_if_fail (LRG_IS_VIDEO_SUBTITLES (subtitles));

    size = CLAMP (size, 8.0f, 200.0f);

    if (subtitles->font_size != size)
    {
        subtitles->font_size = size;
        g_object_notify_by_pspec (G_OBJECT (subtitles), properties[PROP_FONT_SIZE]);
    }
}

gfloat
lrg_video_subtitles_get_font_size (LrgVideoSubtitles *subtitles)
{
    g_return_val_if_fail (LRG_IS_VIDEO_SUBTITLES (subtitles), 24.0f);
    return subtitles->font_size;
}

void
lrg_video_subtitles_set_color (LrgVideoSubtitles *subtitles,
                               guint8             r,
                               guint8             g,
                               guint8             b,
                               guint8             a)
{
    g_return_if_fail (LRG_IS_VIDEO_SUBTITLES (subtitles));

    subtitles->color_r = r;
    subtitles->color_g = g;
    subtitles->color_b = b;
    subtitles->color_a = a;
}

void
lrg_video_subtitles_get_color (LrgVideoSubtitles *subtitles,
                               guint8            *r,
                               guint8            *g,
                               guint8            *b,
                               guint8            *a)
{
    g_return_if_fail (LRG_IS_VIDEO_SUBTITLES (subtitles));

    if (r != NULL)
        *r = subtitles->color_r;
    if (g != NULL)
        *g = subtitles->color_g;
    if (b != NULL)
        *b = subtitles->color_b;
    if (a != NULL)
        *a = subtitles->color_a;
}

void
lrg_video_subtitles_set_background (LrgVideoSubtitles *subtitles,
                                    gboolean           enabled)
{
    g_return_if_fail (LRG_IS_VIDEO_SUBTITLES (subtitles));

    if (subtitles->background != enabled)
    {
        subtitles->background = enabled;
        g_object_notify_by_pspec (G_OBJECT (subtitles), properties[PROP_BACKGROUND]);
    }
}

gboolean
lrg_video_subtitles_get_background (LrgVideoSubtitles *subtitles)
{
    g_return_val_if_fail (LRG_IS_VIDEO_SUBTITLES (subtitles), TRUE);
    return subtitles->background;
}

void
lrg_video_subtitles_set_margin (LrgVideoSubtitles *subtitles,
                                gfloat             margin)
{
    g_return_if_fail (LRG_IS_VIDEO_SUBTITLES (subtitles));

    margin = CLAMP (margin, 0.0f, 500.0f);

    if (subtitles->margin != margin)
    {
        subtitles->margin = margin;
        g_object_notify_by_pspec (G_OBJECT (subtitles), properties[PROP_MARGIN]);
    }
}

gfloat
lrg_video_subtitles_get_margin (LrgVideoSubtitles *subtitles)
{
    g_return_val_if_fail (LRG_IS_VIDEO_SUBTITLES (subtitles), 50.0f);
    return subtitles->margin;
}

void
lrg_video_subtitles_update (LrgVideoSubtitles *subtitles,
                            gdouble            time)
{
    gchar *new_text;

    g_return_if_fail (LRG_IS_VIDEO_SUBTITLES (subtitles));

    if (subtitles->track == NULL)
    {
        g_clear_pointer (&subtitles->current_text, g_free);
        return;
    }

    new_text = lrg_video_subtitle_track_get_text_at (subtitles->track, time);

    /* Only update if text changed */
    if (g_strcmp0 (new_text, subtitles->current_text) != 0)
    {
        g_free (subtitles->current_text);
        subtitles->current_text = new_text;
    }
    else
    {
        g_free (new_text);
    }
}

const gchar *
lrg_video_subtitles_get_current_text (LrgVideoSubtitles *subtitles)
{
    g_return_val_if_fail (LRG_IS_VIDEO_SUBTITLES (subtitles), NULL);
    return subtitles->current_text;
}

void
lrg_video_subtitles_draw (LrgVideoSubtitles *subtitles,
                          gint               screen_width,
                          gint               screen_height)
{
    g_return_if_fail (LRG_IS_VIDEO_SUBTITLES (subtitles));

    /* Only draw if visible and we have text */
    if (!subtitles->visible || subtitles->current_text == NULL)
        return;

    /*
     * In a real implementation, this would:
     * 1. Calculate text width/height using the font
     * 2. Draw background box if enabled
     * 3. Draw text centered at appropriate position
     *
     * The actual rendering depends on the graphics backend (raylib, etc.)
     */

    /* For now, this is a placeholder that just logs the text */
    (void) screen_width;
    (void) screen_height;
}
