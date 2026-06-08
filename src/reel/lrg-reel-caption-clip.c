/* lrg-reel-caption-clip.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "config.h"
#include "lrg-reel-caption-clip.h"
#include "lrg-reel-context.h"
#include "../graphics/lrg-image-canvas.h"
#include "../video/lrg-video-subtitle-track.h"
#include "../audio/lrg-wave-data.h"
#include <gio/gio.h>
#include <glib/gstdio.h>
#include <string.h>

/* ==========================================================================
 * Constants
 * ========================================================================== */

/* Default padding (pixels) added around the text when drawing the box. */
#define CAPTION_BOX_PAD_H 8
#define CAPTION_BOX_PAD_V 4

/* Horizontal margin used for left- and right-aligned text. */
#define CAPTION_SIDE_MARGIN 16

/* ==========================================================================
 * Instance struct
 * ========================================================================== */

struct _LrgReelCaptionClip
{
    LrgReelClip            parent_instance;

    LrgVideoSubtitleTrack *track;

    gint                   font_size;
    GrlColor               color;
    LrgReelTextAlign       align;
    gint                   margin_bottom;

    gboolean               box;
    GrlColor               box_color;
};

G_DEFINE_FINAL_TYPE (LrgReelCaptionClip, lrg_reel_caption_clip,
                     LRG_TYPE_REEL_CLIP)

/* ==========================================================================
 * GObject property ids
 * ========================================================================== */

enum
{
    PROP_0,
    PROP_FONT_SIZE,
    PROP_COLOR,
    PROP_ALIGN,
    PROP_MARGIN_BOTTOM,
    PROP_BOX,
    PROP_BOX_COLOR,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

/* ==========================================================================
 * Render vfunc
 * ========================================================================== */

static void
lrg_reel_caption_clip_render (LrgReelClip    *base,
                               LrgReelContext *ctx,
                               LrgImageCanvas *canvas)
{
    LrgReelCaptionClip *self;
    gdouble             t;
    gchar              *text;
    GrlVector2         *size;
    gint                text_w;
    gint                text_h;
    gint                frame_w;
    gint                frame_h;
    gint                text_x;
    gint                text_y;
    gint                box_x;
    gint                box_y;
    gint                box_w;
    gint                box_h;
    GrlImage           *img;

    self = LRG_REEL_CAPTION_CLIP (base);

    if (self->track == NULL)
        return;

    t    = lrg_reel_context_get_seconds (ctx);
    text = lrg_video_subtitle_track_get_text_at (self->track, t);

    if (text == NULL || text[0] == '\0')
    {
        g_free (text);
        return;
    }

    /* Measure the text using the embedded bitmap font. */
    size = grl_image_measure_text_bitmap (text, self->font_size);
    if (size == NULL)
    {
        g_free (text);
        return;
    }

    text_w   = (gint) size->x;
    text_h   = (gint) size->y;
    frame_w  = lrg_reel_context_get_width (ctx);
    frame_h  = lrg_reel_context_get_height (ctx);

    grl_vector2_free (size);

    /* Compute horizontal position based on alignment. */
    switch (self->align)
    {
    case LRG_REEL_TEXT_ALIGN_LEFT:
        text_x = CAPTION_SIDE_MARGIN;
        break;
    case LRG_REEL_TEXT_ALIGN_RIGHT:
        text_x = frame_w - CAPTION_SIDE_MARGIN - text_w;
        break;
    case LRG_REEL_TEXT_ALIGN_CENTER:
    default:
        text_x = (frame_w - text_w) / 2;
        break;
    }

    /* Compute vertical position measured from the bottom. */
    text_y = frame_h - self->margin_bottom - text_h;

    /* Draw the background box first if requested. */
    if (self->box)
    {
        box_x = text_x - CAPTION_BOX_PAD_H;
        box_y = text_y - CAPTION_BOX_PAD_V;
        box_w = text_w + CAPTION_BOX_PAD_H * 2;
        box_h = text_h + CAPTION_BOX_PAD_V * 2;

        /* Clamp box to the frame boundaries. */
        if (box_x < 0)
        {
            box_w += box_x;
            box_x  = 0;
        }
        if (box_y < 0)
        {
            box_h += box_y;
            box_y  = 0;
        }
        if (box_x + box_w > frame_w)
            box_w = frame_w - box_x;
        if (box_y + box_h > frame_h)
            box_h = frame_h - box_y;

        if (box_w > 0 && box_h > 0)
            lrg_image_canvas_fill_rect (canvas, box_x, box_y, box_w, box_h,
                                        &self->box_color);
    }

    /* Draw the text. */
    img = lrg_image_canvas_get_image (canvas);
    grl_image_draw_text_bitmap (img, text, text_x, text_y, self->font_size,
                                &self->color);

    g_free (text);
}

/* ==========================================================================
 * GObject boilerplate
 * ========================================================================== */

static void
lrg_reel_caption_clip_finalize (GObject *object)
{
    LrgReelCaptionClip *self = LRG_REEL_CAPTION_CLIP (object);

    g_clear_object (&self->track);

    G_OBJECT_CLASS (lrg_reel_caption_clip_parent_class)->finalize (object);
}

static void
lrg_reel_caption_clip_get_property (GObject    *object,
                                     guint       prop_id,
                                     GValue     *value,
                                     GParamSpec *pspec)
{
    LrgReelCaptionClip *self = LRG_REEL_CAPTION_CLIP (object);
    GrlColor            tmp;

    switch (prop_id)
    {
    case PROP_FONT_SIZE:
        g_value_set_int (value, self->font_size);
        break;
    case PROP_COLOR:
        memcpy (&tmp, &self->color, sizeof (GrlColor));
        g_value_set_boxed (value, &tmp);
        break;
    case PROP_ALIGN:
        g_value_set_enum (value, self->align);
        break;
    case PROP_MARGIN_BOTTOM:
        g_value_set_int (value, self->margin_bottom);
        break;
    case PROP_BOX:
        g_value_set_boolean (value, self->box);
        break;
    case PROP_BOX_COLOR:
        memcpy (&tmp, &self->box_color, sizeof (GrlColor));
        g_value_set_boxed (value, &tmp);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
lrg_reel_caption_clip_set_property (GObject      *object,
                                     guint         prop_id,
                                     const GValue *value,
                                     GParamSpec   *pspec)
{
    LrgReelCaptionClip *self  = LRG_REEL_CAPTION_CLIP (object);
    const GrlColor     *color = NULL;

    switch (prop_id)
    {
    case PROP_FONT_SIZE:
        lrg_reel_caption_clip_set_font_size (self, g_value_get_int (value));
        break;
    case PROP_COLOR:
        color = (const GrlColor *) g_value_get_boxed (value);
        if (color != NULL)
            lrg_reel_caption_clip_set_color (self, color);
        break;
    case PROP_ALIGN:
        lrg_reel_caption_clip_set_align (self,
                                         (LrgReelTextAlign) g_value_get_enum (value));
        break;
    case PROP_MARGIN_BOTTOM:
        lrg_reel_caption_clip_set_margin_bottom (self, g_value_get_int (value));
        break;
    case PROP_BOX:
        lrg_reel_caption_clip_set_box (self, g_value_get_boolean (value));
        break;
    case PROP_BOX_COLOR:
        color = (const GrlColor *) g_value_get_boxed (value);
        if (color != NULL)
            lrg_reel_caption_clip_set_box_color (self, color);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
lrg_reel_caption_clip_class_init (LrgReelCaptionClipClass *klass)
{
    GObjectClass     *object_class = G_OBJECT_CLASS (klass);
    LrgReelClipClass *clip_class   = LRG_REEL_CLIP_CLASS (klass);

    object_class->finalize     = lrg_reel_caption_clip_finalize;
    object_class->get_property = lrg_reel_caption_clip_get_property;
    object_class->set_property = lrg_reel_caption_clip_set_property;

    clip_class->render = lrg_reel_caption_clip_render;

    /**
     * LrgReelCaptionClip:font-size:
     *
     * Bitmap font size in pixels used to render caption text.  Default: 28.
     */
    properties[PROP_FONT_SIZE] =
        g_param_spec_int ("font-size", "Font Size",
                          "Bitmap font size in pixels",
                          1, G_MAXINT, 28,
                          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
                          G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgReelCaptionClip:color:
     *
     * Foreground color used to draw caption text.  Default: opaque white.
     */
    properties[PROP_COLOR] =
        g_param_spec_boxed ("color", "Color",
                            "Text foreground color",
                            GRL_TYPE_COLOR,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
                            G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgReelCaptionClip:align:
     *
     * Horizontal alignment of the caption text within the frame.
     * Default: %LRG_REEL_TEXT_ALIGN_CENTER.
     */
    properties[PROP_ALIGN] =
        g_param_spec_enum ("align", "Align",
                           "Horizontal text alignment",
                           LRG_TYPE_REEL_TEXT_ALIGN,
                           LRG_REEL_TEXT_ALIGN_CENTER,
                           G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
                           G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgReelCaptionClip:margin-bottom:
     *
     * Distance in pixels from the bottom edge of the frame to the bottom of
     * the caption text.  Default: 40.
     */
    properties[PROP_MARGIN_BOTTOM] =
        g_param_spec_int ("margin-bottom", "Margin Bottom",
                          "Distance in pixels from the frame bottom",
                          0, G_MAXINT, 40,
                          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
                          G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgReelCaptionClip:box:
     *
     * Whether to draw a semi-transparent background rectangle behind the
     * caption text to improve readability.  Default: %TRUE.
     */
    properties[PROP_BOX] =
        g_param_spec_boolean ("box", "Box",
                              "Draw a background box behind the text",
                              TRUE,
                              G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
                              G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgReelCaptionClip:box-color:
     *
     * Fill color for the background box.  The alpha channel controls the box
     * opacity.  Default: black at approximately 50 % opacity.
     */
    properties[PROP_BOX_COLOR] =
        g_param_spec_boxed ("box-color", "Box Color",
                            "Background box fill color",
                            GRL_TYPE_COLOR,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
                            G_PARAM_EXPLICIT_NOTIFY);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_reel_caption_clip_init (LrgReelCaptionClip *self)
{
    self->track         = NULL;
    self->font_size     = 28;
    self->color.r       = 255;
    self->color.g       = 255;
    self->color.b       = 255;
    self->color.a       = 255;
    self->align         = LRG_REEL_TEXT_ALIGN_CENTER;
    self->margin_bottom = 40;
    self->box           = TRUE;
    self->box_color.r   = 0;
    self->box_color.g   = 0;
    self->box_color.b   = 0;
    self->box_color.a   = 128; /* ~50 % opacity */
}

/* ==========================================================================
 * Constructors
 * ========================================================================== */

/**
 * lrg_reel_caption_clip_new:
 *
 * Creates a new #LrgReelCaptionClip with an empty subtitle track.
 *
 * Returns: (transfer full): a new #LrgReelCaptionClip
 *
 * Since: 1.0
 */
LrgReelCaptionClip *
lrg_reel_caption_clip_new (void)
{
    return g_object_new (LRG_TYPE_REEL_CAPTION_CLIP, NULL);
}

/**
 * lrg_reel_caption_clip_new_from_srt:
 * @path: (type filename): path to an SRT subtitle file.
 * @error: (nullable): return location for a #GError, or %NULL.
 *
 * Creates a new #LrgReelCaptionClip pre-loaded with cues from @path.
 *
 * Returns: (transfer full) (nullable): a new #LrgReelCaptionClip, or %NULL on
 *   error.
 *
 * Since: 1.0
 */
LrgReelCaptionClip *
lrg_reel_caption_clip_new_from_srt (const gchar  *path,
                                     GError      **error)
{
    LrgReelCaptionClip    *self;
    LrgVideoSubtitleTrack *track;

    g_return_val_if_fail (path != NULL, NULL);

    track = lrg_video_subtitle_track_new ();

    if (!lrg_video_subtitle_track_load_srt (track, path, error))
    {
        g_object_unref (track);
        return NULL;
    }

    self = lrg_reel_caption_clip_new ();
    lrg_reel_caption_clip_set_track (self, track);
    g_object_unref (track);

    return self;
}

/**
 * lrg_reel_caption_clip_new_from_vtt:
 * @path: (type filename): path to a WebVTT subtitle file.
 * @error: (nullable): return location for a #GError, or %NULL.
 *
 * Creates a new #LrgReelCaptionClip pre-loaded with cues from @path.
 *
 * Returns: (transfer full) (nullable): a new #LrgReelCaptionClip, or %NULL on
 *   error.
 *
 * Since: 1.0
 */
LrgReelCaptionClip *
lrg_reel_caption_clip_new_from_vtt (const gchar  *path,
                                     GError      **error)
{
    LrgReelCaptionClip    *self;
    LrgVideoSubtitleTrack *track;

    g_return_val_if_fail (path != NULL, NULL);

    track = lrg_video_subtitle_track_new ();

    if (!lrg_video_subtitle_track_load_vtt (track, path, error))
    {
        g_object_unref (track);
        return NULL;
    }

    self = lrg_reel_caption_clip_new ();
    lrg_reel_caption_clip_set_track (self, track);
    g_object_unref (track);

    return self;
}

/* ==========================================================================
 * Track accessor
 * ========================================================================== */

/**
 * lrg_reel_caption_clip_set_track:
 * @self: an #LrgReelCaptionClip
 * @track: (transfer none) (nullable): the #LrgVideoSubtitleTrack to use, or
 *   %NULL to clear.
 *
 * Replaces the clip's subtitle track.  The clip takes a reference on @track.
 *
 * Since: 1.0
 */
void
lrg_reel_caption_clip_set_track (LrgReelCaptionClip    *self,
                                  LrgVideoSubtitleTrack *track)
{
    g_return_if_fail (LRG_IS_REEL_CAPTION_CLIP (self));
    g_return_if_fail (track == NULL || LRG_IS_VIDEO_SUBTITLE_TRACK (track));

    if (self->track == track)
        return;

    g_clear_object (&self->track);

    if (track != NULL)
        self->track = g_object_ref (track);
}

/**
 * lrg_reel_caption_clip_get_track:
 * @self: an #LrgReelCaptionClip
 *
 * Returns: (transfer none) (nullable): the current #LrgVideoSubtitleTrack, or
 *   %NULL if none is set.
 *
 * Since: 1.0
 */
LrgVideoSubtitleTrack *
lrg_reel_caption_clip_get_track (LrgReelCaptionClip *self)
{
    g_return_val_if_fail (LRG_IS_REEL_CAPTION_CLIP (self), NULL);

    return self->track;
}

/* ==========================================================================
 * Style accessors
 * ========================================================================== */

/**
 * lrg_reel_caption_clip_set_font_size:
 * @self: an #LrgReelCaptionClip
 * @font_size: font size in pixels (> 0).
 *
 * Sets the bitmap font size used to render caption text.
 *
 * Since: 1.0
 */
void
lrg_reel_caption_clip_set_font_size (LrgReelCaptionClip *self,
                                      gint                font_size)
{
    g_return_if_fail (LRG_IS_REEL_CAPTION_CLIP (self));
    g_return_if_fail (font_size > 0);

    if (self->font_size == font_size)
        return;

    self->font_size = font_size;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_FONT_SIZE]);
}

/**
 * lrg_reel_caption_clip_get_font_size:
 * @self: an #LrgReelCaptionClip
 *
 * Returns: the current font size in pixels.
 *
 * Since: 1.0
 */
gint
lrg_reel_caption_clip_get_font_size (LrgReelCaptionClip *self)
{
    g_return_val_if_fail (LRG_IS_REEL_CAPTION_CLIP (self), 28);

    return self->font_size;
}

/**
 * lrg_reel_caption_clip_set_color:
 * @self: an #LrgReelCaptionClip
 * @color: the text color.
 *
 * Sets the foreground color used to draw caption text.
 *
 * Since: 1.0
 */
void
lrg_reel_caption_clip_set_color (LrgReelCaptionClip *self,
                                  const GrlColor     *color)
{
    g_return_if_fail (LRG_IS_REEL_CAPTION_CLIP (self));
    g_return_if_fail (color != NULL);

    if (memcmp (&self->color, color, sizeof (GrlColor)) == 0)
        return;

    memcpy (&self->color, color, sizeof (GrlColor));
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_COLOR]);
}

/**
 * lrg_reel_caption_clip_get_color:
 * @self: an #LrgReelCaptionClip
 * @out_color: (out caller-allocates): return location for the color.
 *
 * Copies the current text color into @out_color.
 *
 * Since: 1.0
 */
void
lrg_reel_caption_clip_get_color (LrgReelCaptionClip *self,
                                  GrlColor           *out_color)
{
    g_return_if_fail (LRG_IS_REEL_CAPTION_CLIP (self));
    g_return_if_fail (out_color != NULL);

    memcpy (out_color, &self->color, sizeof (GrlColor));
}

/**
 * lrg_reel_caption_clip_set_align:
 * @self: an #LrgReelCaptionClip
 * @align: the horizontal text alignment.
 *
 * Sets how caption text is aligned horizontally.
 *
 * Since: 1.0
 */
void
lrg_reel_caption_clip_set_align (LrgReelCaptionClip *self,
                                  LrgReelTextAlign    align)
{
    g_return_if_fail (LRG_IS_REEL_CAPTION_CLIP (self));

    if (self->align == align)
        return;

    self->align = align;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ALIGN]);
}

/**
 * lrg_reel_caption_clip_get_align:
 * @self: an #LrgReelCaptionClip
 *
 * Returns: the current #LrgReelTextAlign value.
 *
 * Since: 1.0
 */
LrgReelTextAlign
lrg_reel_caption_clip_get_align (LrgReelCaptionClip *self)
{
    g_return_val_if_fail (LRG_IS_REEL_CAPTION_CLIP (self),
                          LRG_REEL_TEXT_ALIGN_CENTER);

    return self->align;
}

/**
 * lrg_reel_caption_clip_set_margin_bottom:
 * @self: an #LrgReelCaptionClip
 * @margin_bottom: distance in pixels from the bottom edge of the frame.
 *
 * Sets the vertical position of the caption by specifying how far above the
 * bottom of the frame the text baseline sits.
 *
 * Since: 1.0
 */
void
lrg_reel_caption_clip_set_margin_bottom (LrgReelCaptionClip *self,
                                          gint                margin_bottom)
{
    g_return_if_fail (LRG_IS_REEL_CAPTION_CLIP (self));
    g_return_if_fail (margin_bottom >= 0);

    if (self->margin_bottom == margin_bottom)
        return;

    self->margin_bottom = margin_bottom;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MARGIN_BOTTOM]);
}

/**
 * lrg_reel_caption_clip_get_margin_bottom:
 * @self: an #LrgReelCaptionClip
 *
 * Returns: the bottom margin in pixels.
 *
 * Since: 1.0
 */
gint
lrg_reel_caption_clip_get_margin_bottom (LrgReelCaptionClip *self)
{
    g_return_val_if_fail (LRG_IS_REEL_CAPTION_CLIP (self), 40);

    return self->margin_bottom;
}

/**
 * lrg_reel_caption_clip_set_box:
 * @self: an #LrgReelCaptionClip
 * @box: %TRUE to draw a background box behind the caption text.
 *
 * Enables or disables the semi-transparent background rectangle drawn behind
 * caption text to improve readability against complex backgrounds.
 *
 * Since: 1.0
 */
void
lrg_reel_caption_clip_set_box (LrgReelCaptionClip *self,
                                gboolean            box)
{
    g_return_if_fail (LRG_IS_REEL_CAPTION_CLIP (self));

    box = !!box;
    if (self->box == box)
        return;

    self->box = box;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_BOX]);
}

/**
 * lrg_reel_caption_clip_get_box:
 * @self: an #LrgReelCaptionClip
 *
 * Returns: %TRUE if the background box is enabled.
 *
 * Since: 1.0
 */
gboolean
lrg_reel_caption_clip_get_box (LrgReelCaptionClip *self)
{
    g_return_val_if_fail (LRG_IS_REEL_CAPTION_CLIP (self), TRUE);

    return self->box;
}

/**
 * lrg_reel_caption_clip_set_box_color:
 * @self: an #LrgReelCaptionClip
 * @color: the background box fill color (alpha controls opacity).
 *
 * Sets the color of the background rectangle drawn behind the caption text.
 *
 * Since: 1.0
 */
void
lrg_reel_caption_clip_set_box_color (LrgReelCaptionClip *self,
                                      const GrlColor     *color)
{
    g_return_if_fail (LRG_IS_REEL_CAPTION_CLIP (self));
    g_return_if_fail (color != NULL);

    if (memcmp (&self->box_color, color, sizeof (GrlColor)) == 0)
        return;

    memcpy (&self->box_color, color, sizeof (GrlColor));
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_BOX_COLOR]);
}

/**
 * lrg_reel_caption_clip_get_box_color:
 * @self: an #LrgReelCaptionClip
 * @out_color: (out caller-allocates): return location for the box color.
 *
 * Copies the current box color into @out_color.
 *
 * Since: 1.0
 */
void
lrg_reel_caption_clip_get_box_color (LrgReelCaptionClip *self,
                                      GrlColor           *out_color)
{
    g_return_if_fail (LRG_IS_REEL_CAPTION_CLIP (self));
    g_return_if_fail (out_color != NULL);

    memcpy (out_color, &self->box_color, sizeof (GrlColor));
}

/* ==========================================================================
 * Whisper-detection helper
 * ========================================================================== */

/*
 * Candidates probed in order.  "main" is the legacy whisper.cpp binary name;
 * "whisper" is the upstream Python-based CLI.
 */
static const gchar * const WHISPER_CANDIDATES[] = {
    "whisper-cli",
    "whisper-cpp",
    "main",
    "whisper",
    NULL
};

/*
 * find_whisper_exe:
 *
 * Returns the full path of the first candidate found on PATH, or %NULL.
 * Caller must free the returned string with g_free().
 */
static gchar *
find_whisper_exe (void)
{
    guint i;

    for (i = 0; WHISPER_CANDIDATES[i] != NULL; i++)
    {
        gchar *path = g_find_program_in_path (WHISPER_CANDIDATES[i]);
        if (path != NULL)
            return path;
    }

    return NULL;
}

/**
 * lrg_reel_is_whisper_available:
 *
 * Tests whether a supported speech-recognition command-line tool is installed
 * and accessible on the current %PATH.  The following executables are probed
 * in order: @whisper-cli, @whisper-cpp, @main (whisper.cpp legacy name),
 * @whisper (upstream).
 *
 * Returns: %TRUE if at least one supported tool was found.
 *
 * Since: 1.0
 */
gboolean
lrg_reel_is_whisper_available (void)
{
    gchar    *exe;
    gboolean  found;

    exe   = find_whisper_exe ();
    found = (exe != NULL);
    g_free (exe);

    return found;
}

/* ==========================================================================
 * Transcription
 * ========================================================================== */

/*
 * is_cpp_cli:
 * @exe_basename: base name of the executable (not a full path).
 *
 * Returns TRUE if @exe_basename matches a known whisper.cpp CLI name, i.e.
 * whisper-cli, whisper-cpp, or main.  The upstream Python binary ("whisper")
 * uses a different argument layout.
 */
static gboolean
is_cpp_cli (const gchar *exe_basename)
{
    return (g_strcmp0 (exe_basename, "whisper-cli") == 0 ||
            g_strcmp0 (exe_basename, "whisper-cpp") == 0 ||
            g_strcmp0 (exe_basename, "main")        == 0);
}

/**
 * lrg_reel_transcribe_audio:
 * @audio: an #LrgWaveData containing the audio to transcribe.
 * @language: (nullable): BCP-47 language tag (e.g. "en", "es"), or %NULL to
 *   use automatic language detection.
 * @error: (nullable): return location for a #GError, or %NULL.
 *
 * Transcribes @audio to subtitle cues using a locally installed
 * speech-recognition tool.  The function exports @audio to a temporary WAV
 * file, invokes the tool, parses the resulting SRT output, and returns a
 * populated #LrgVideoSubtitleTrack.
 *
 * The temporary files are removed before this function returns.
 *
 * Assumptions: the speech-recognition tool is the whisper.cpp command-line
 * interface (whisper-cli / whisper-cpp / main).  The upstream Python
 * distribution (@whisper) is also supported with a different argument set.
 * The function selects argument order based on which executable was found;
 * if the installed binary uses a non-standard interface the transcription may
 * fail and @error will be set.
 *
 * Returns: (transfer full) (nullable): a new #LrgVideoSubtitleTrack, or %NULL
 *   on error.
 *
 * Since: 1.0
 */
LrgVideoSubtitleTrack *
lrg_reel_transcribe_audio (LrgWaveData  *audio,
                            const gchar  *language,
                            GError      **error)
{
    gchar                 *exe          = NULL;
    gchar                 *tmpdir       = NULL;
    gchar                 *wav_path     = NULL;
    gchar                 *srt_path     = NULL;
    gchar                 *out_basename = NULL;
    LrgVideoSubtitleTrack *track        = NULL;
    gboolean               ok           = FALSE;
    GSubprocess           *proc         = NULL;
    const gchar           *lang_str;
    gchar                 *exe_basename;

    g_return_val_if_fail (LRG_IS_WAVE_DATA (audio), NULL);

    /* --- 1. Locate the tool ------------------------------------------------ */
    exe = find_whisper_exe ();
    if (exe == NULL)
    {
        g_set_error (error,
                     G_IO_ERROR,
                     G_IO_ERROR_NOT_SUPPORTED,
                     "No speech-recognition tool found on PATH "
                     "(tried: whisper-cli, whisper-cpp, main, whisper)");
        return NULL;
    }

    /* --- 2. Create a temporary directory ----------------------------------- */
    tmpdir = g_dir_make_tmp ("lrg-transcribe-XXXXXX", error);
    if (tmpdir == NULL)
    {
        g_free (exe);
        return NULL;
    }

    /* --- 3. Export the audio to a WAV file --------------------------------- */
    wav_path = g_build_filename (tmpdir, "audio.wav", NULL);

    if (!lrg_wave_data_export_wav (audio, wav_path, error))
        goto cleanup;

    /* --- 4. Build the argv for the chosen tool ----------------------------- */

    /*
     * out_basename is the output path stem; whisper.cpp appends ".srt" to it,
     * giving us out_basename.srt.  The upstream Python CLI writes the SRT into
     * a directory named by --output_dir.
     */
    out_basename = g_build_filename (tmpdir, "audio", NULL);
    srt_path     = g_strconcat (out_basename, ".srt", NULL);

    exe_basename = g_path_get_basename (exe);
    lang_str     = (language != NULL && language[0] != '\0') ? language : "auto";

    if (is_cpp_cli (exe_basename))
    {
        /*
         * whisper.cpp CLI (whisper-cli / whisper-cpp / main):
         *   -f <wav>  -osrt  -of <basename>  -l <lang>
         *
         * Output file: <basename>.srt (created by the tool).
         */
        const gchar *argv[] = {
            exe, "-f", wav_path, "-osrt", "-of", out_basename,
            "-l", lang_str, NULL
        };

        proc = g_subprocess_newv (argv,
                                   G_SUBPROCESS_FLAGS_STDOUT_SILENCE |
                                   G_SUBPROCESS_FLAGS_STDERR_SILENCE,
                                   error);
    }
    else
    {
        /*
         * Upstream Python CLI (whisper):
         *   <wav>  --model base  --output_format srt
         *   --output_dir <dir>  --language <lang>
         *
         * Output file: <dir>/audio.srt  (stem taken from the WAV filename).
         *
         * Note: "auto" is not accepted by the Python CLI; replace with "auto"
         * language by omitting --language; the Python CLI defaults to auto.
         */
        if (g_strcmp0 (lang_str, "auto") == 0)
        {
            const gchar *argv[] = {
                exe, wav_path,
                "--model", "base",
                "--output_format", "srt",
                "--output_dir", tmpdir,
                NULL
            };
            proc = g_subprocess_newv (argv,
                                       G_SUBPROCESS_FLAGS_STDOUT_SILENCE |
                                       G_SUBPROCESS_FLAGS_STDERR_SILENCE,
                                       error);
        }
        else
        {
            const gchar *argv[] = {
                exe, wav_path,
                "--model", "base",
                "--output_format", "srt",
                "--output_dir", tmpdir,
                "--language", lang_str,
                NULL
            };
            proc = g_subprocess_newv (argv,
                                       G_SUBPROCESS_FLAGS_STDOUT_SILENCE |
                                       G_SUBPROCESS_FLAGS_STDERR_SILENCE,
                                       error);
        }
    }

    g_free (exe_basename);

    if (proc == NULL)
        goto cleanup;

    /* --- 5. Wait for the process to finish --------------------------------- */
    ok = g_subprocess_wait_check (proc, NULL, error);
    g_clear_object (&proc);

    if (!ok)
        goto cleanup;

    /* --- 6. Load the produced SRT ----------------------------------------- */
    if (!g_file_test (srt_path, G_FILE_TEST_EXISTS))
    {
        g_set_error (error,
                     G_IO_ERROR,
                     G_IO_ERROR_NOT_FOUND,
                     "Speech-recognition tool did not produce expected output: %s",
                     srt_path);
        goto cleanup;
    }

    track = lrg_video_subtitle_track_new ();

    if (!lrg_video_subtitle_track_load_srt (track, srt_path, error))
    {
        g_clear_object (&track);
        goto cleanup;
    }

    if (language != NULL && language[0] != '\0')
        lrg_video_subtitle_track_set_language (track, language);

cleanup:
    /* Remove temporary files; ignore errors on cleanup. */
    if (srt_path != NULL)
        g_remove (srt_path);
    if (wav_path != NULL)
        g_remove (wav_path);
    if (tmpdir != NULL)
        g_rmdir (tmpdir);

    g_free (srt_path);
    g_free (out_basename);
    g_free (wav_path);
    g_free (tmpdir);
    g_free (exe);

    return track;
}
