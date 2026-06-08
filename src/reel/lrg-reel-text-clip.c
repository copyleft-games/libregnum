/* lrg-reel-text-clip.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "config.h"
#include "lrg-reel-text-clip.h"
#include "../graphics/lrg-image-canvas.h"
#include "lrg-reel-context.h"
#include <string.h>

/* ==========================================================================
 * Instance struct
 * ========================================================================== */

struct _LrgReelTextClip
{
    LrgReelClip   parent_instance;

    gchar        *text;
    GrlImageFont *font;       /* nullable; NULL -> bitmap font */
    gdouble       font_size;
    GrlColor      color;
    LrgReelTextAlign align;
    gint          text_x;
    gint          text_y;
    gint          max_width;  /* 0 = no wrapping */
    gdouble       line_height; /* multiplier */

    /* Drop shadow */
    gboolean      shadow;
    GrlColor      shadow_color;
    gint          shadow_dx;
    gint          shadow_dy;
};

G_DEFINE_FINAL_TYPE (LrgReelTextClip, lrg_reel_text_clip, LRG_TYPE_REEL_CLIP)

/* ==========================================================================
 * GObject property ids
 * ========================================================================== */

enum
{
    PROP_0,
    PROP_TEXT,
    PROP_FONT,
    PROP_FONT_SIZE,
    PROP_COLOR,
    PROP_ALIGN,
    PROP_TEXT_X,
    PROP_TEXT_Y,
    PROP_MAX_WIDTH,
    PROP_LINE_HEIGHT,
    PROP_SHADOW,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

/* ==========================================================================
 * Internal helpers
 * ========================================================================== */

/*
 * measure_line:
 * @self: the clip (provides font / font_size).
 * @line: a NUL-terminated line of text (no '\n').
 * @out_w: (out): measured pixel width.
 * @out_h: (out): measured pixel height.
 *
 * Measures a single line.  Frees the returned GrlVector2 internally.
 */
static void
measure_line (LrgReelTextClip *self,
              const gchar     *line,
              gint            *out_w,
              gint            *out_h)
{
    g_autoptr(GrlVector2) sz = NULL;

    if (self->font != NULL)
    {
        sz = grl_image_measure_text_ttf (self->font, line,
                                         (gfloat) self->font_size);
    }
    else
    {
        sz = grl_image_measure_text_bitmap (line, (gint) self->font_size);
    }

    if (sz != NULL)
    {
        if (out_w) *out_w = (gint) sz->x;
        if (out_h) *out_h = (gint) sz->y;
    }
    else
    {
        if (out_w) *out_w = 0;
        if (out_h) *out_h = 0;
    }
}

/*
 * natural_line_height:
 *
 * Returns the pixel distance to advance between lines.
 */
static gint
natural_line_height (LrgReelTextClip *self)
{
    gfloat ascent;
    gfloat descent;
    gfloat gap;
    gint   lh;

    if (self->font != NULL)
    {
        grl_image_font_get_v_metrics (self->font, (gfloat) self->font_size,
                                      &ascent, &descent, &gap);
        /* descent is <= 0; gap >= 0 */
        lh = (gint) ((ascent - descent + gap) * (gfloat) self->line_height + 0.5f);
        if (lh < 1)
            lh = (gint) (self->font_size * self->line_height + 0.5);
    }
    else
    {
        lh = (gint) (self->font_size * self->line_height + 0.5);
    }

    return lh;
}

/*
 * build_lines:
 * @self: the clip.
 * @out_count: (out): number of lines allocated.
 *
 * Splits self->text into an array of heap-allocated line strings, applying
 * greedy word-wrap when self->max_width > 0.  The caller must free each
 * element and then the array itself with g_free.
 *
 * Returns a NULL-terminated array of gchar* (never NULL; may be { NULL }).
 */
static gchar **
build_lines (LrgReelTextClip *self,
             gint            *out_count)
{
    GPtrArray   *result;
    const gchar *src;
    const gchar *seg_start;
    gchar       *seg;
    gchar      **parts;
    guint        i;

    result = g_ptr_array_new ();
    *out_count = 0;

    if (self->text == NULL || self->text[0] == '\0')
    {
        g_ptr_array_add (result, NULL);
        return (gchar **) g_ptr_array_free (result, FALSE);
    }

    /* Split text on '\n' first to get paragraphs. */
    parts = g_strsplit (self->text, "\n", -1);

    for (i = 0; parts[i] != NULL; i++)
    {
        const gchar *para;
        const gchar *word_end;
        const gchar *next_space;
        gchar       *candidate;
        gint         cw;
        gsize        seg_len;
        gsize        emit_len;

        para = parts[i];

        /* If no word-wrap, emit each paragraph line as-is. */
        if (self->max_width <= 0)
        {
            g_ptr_array_add (result, g_strdup (para));
            (*out_count)++;
            continue;
        }

        /* Greedy word-wrap within this paragraph. */
        src       = para;
        seg_start = para;

        while (*src != '\0')
        {
            /* Find the next word boundary (space or end of string). */
            next_space = src;
            while (*next_space != '\0' && *next_space != ' ')
                next_space++;

            /* Build candidate = everything from seg_start to next_space. */
            seg_len   = (gsize)(next_space - seg_start);
            candidate = g_strndup (seg_start, seg_len);
            measure_line (self, candidate, &cw, NULL);

            if (cw > self->max_width && seg_start < src)
            {
                /*
                 * This word would overflow.  Emit the current accumulation
                 * (which stops before the current word).
                 */
                emit_len = (gsize)(src - seg_start);

                /* Trim trailing space. */
                while (emit_len > 0 && seg_start[emit_len - 1] == ' ')
                    emit_len--;

                seg = g_strndup (seg_start, emit_len);
                g_ptr_array_add (result, seg);
                (*out_count)++;

                /* Advance past the space that preceded src. */
                seg_start = src;
                g_free (candidate);
                continue;
            }

            g_free (candidate);

            /* Move src past the word and any following space. */
            src = next_space;
            if (*src == ' ')
                src++;
        }

        /* Emit the remainder of this paragraph. */
        word_end = para + strlen (para);
        if (word_end > seg_start)
        {
            emit_len = (gsize)(word_end - seg_start);
            seg = g_strndup (seg_start, emit_len);
            g_ptr_array_add (result, seg);
            (*out_count)++;
        }
        else if (seg_start == para)
        {
            /* Empty paragraph: emit an empty line. */
            g_ptr_array_add (result, g_strdup (""));
            (*out_count)++;
        }
    }

    g_strfreev (parts);

    /* NULL-terminate. */
    g_ptr_array_add (result, NULL);
    return (gchar **) g_ptr_array_free (result, FALSE);
}

/*
 * draw_single_line:
 * @self: the clip.
 * @image: destination #GrlImage.
 * @line: NUL-terminated text line.
 * @x: pixel X.
 * @y: pixel Y.
 * @color: pointer to draw color.
 */
static void
draw_single_line (LrgReelTextClip *self,
                  GrlImage        *image,
                  const gchar     *line,
                  gint             x,
                  gint             y,
                  const GrlColor  *color)
{
    if (self->font != NULL)
    {
        grl_image_draw_text_ttf (image, self->font, line, x, y,
                                 (gfloat) self->font_size, color);
    }
    else
    {
        grl_image_draw_text_bitmap (image, line, x, y,
                                    (gint) self->font_size, color);
    }
}

/* ==========================================================================
 * Render vfunc
 * ========================================================================== */

static void
lrg_reel_text_clip_render (LrgReelClip    *base,
                            LrgReelContext *ctx,
                            LrgImageCanvas *canvas)
{
    LrgReelTextClip *self;
    GrlImage        *image;
    gchar          **lines;
    gint             n_lines;
    gint             lh;
    gint             i;
    gint             block_width;

    (void) ctx;

    self = LRG_REEL_TEXT_CLIP (base);

    if (self->text == NULL || self->text[0] == '\0')
        return;

    image = lrg_image_canvas_get_image (canvas);

    lines   = build_lines (self, &n_lines);
    lh      = natural_line_height (self);

    /* Determine block_width for alignment when max_width <= 0. */
    block_width = self->max_width;
    if (block_width <= 0 && self->align != LRG_REEL_TEXT_ALIGN_LEFT)
    {
        gint j;
        block_width = 0;
        for (j = 0; j < n_lines; j++)
        {
            gint lw;
            if (lines[j] == NULL)
                continue;
            measure_line (self, lines[j], &lw, NULL);
            if (lw > block_width)
                block_width = lw;
        }
    }

    for (i = 0; i < n_lines; i++)
    {
        gint draw_x;
        gint draw_y;

        if (lines[i] == NULL)
            continue;

        draw_y = self->text_y + i * lh;

        /* Compute draw_x based on alignment. */
        if (self->align == LRG_REEL_TEXT_ALIGN_CENTER ||
            self->align == LRG_REEL_TEXT_ALIGN_RIGHT)
        {
            gint lw;
            measure_line (self, lines[i], &lw, NULL);
            if (self->align == LRG_REEL_TEXT_ALIGN_CENTER)
                draw_x = self->text_x + (block_width - lw) / 2;
            else
                draw_x = self->text_x + block_width - lw;
        }
        else
        {
            draw_x = self->text_x;
        }

        /* Shadow pass. */
        if (self->shadow)
        {
            draw_single_line (self, image, lines[i],
                              draw_x + self->shadow_dx,
                              draw_y + self->shadow_dy,
                              &self->shadow_color);
        }

        /* Text pass. */
        draw_single_line (self, image, lines[i],
                          draw_x, draw_y, &self->color);
    }

    /* Free each line string. */
    for (i = 0; lines[i] != NULL; i++)
        g_free (lines[i]);
    g_free (lines);
}

/* ==========================================================================
 * GObject boilerplate
 * ========================================================================== */

static void
lrg_reel_text_clip_finalize (GObject *object)
{
    LrgReelTextClip *self = LRG_REEL_TEXT_CLIP (object);

    g_clear_pointer (&self->text, g_free);
    g_clear_object  (&self->font);

    G_OBJECT_CLASS (lrg_reel_text_clip_parent_class)->finalize (object);
}

static void
lrg_reel_text_clip_get_property (GObject    *object,
                                  guint       prop_id,
                                  GValue     *value,
                                  GParamSpec *pspec)
{
    LrgReelTextClip *self = LRG_REEL_TEXT_CLIP (object);
    GrlColor         tmp;

    switch (prop_id)
    {
    case PROP_TEXT:
        g_value_set_string (value, self->text);
        break;
    case PROP_FONT:
        g_value_set_object (value, self->font);
        break;
    case PROP_FONT_SIZE:
        g_value_set_double (value, self->font_size);
        break;
    case PROP_COLOR:
        tmp = self->color;
        g_value_set_boxed (value, &tmp);
        break;
    case PROP_ALIGN:
        g_value_set_enum (value, self->align);
        break;
    case PROP_TEXT_X:
        g_value_set_int (value, self->text_x);
        break;
    case PROP_TEXT_Y:
        g_value_set_int (value, self->text_y);
        break;
    case PROP_MAX_WIDTH:
        g_value_set_int (value, self->max_width);
        break;
    case PROP_LINE_HEIGHT:
        g_value_set_double (value, self->line_height);
        break;
    case PROP_SHADOW:
        g_value_set_boolean (value, self->shadow);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_reel_text_clip_set_property (GObject      *object,
                                  guint         prop_id,
                                  const GValue *value,
                                  GParamSpec   *pspec)
{
    LrgReelTextClip *self = LRG_REEL_TEXT_CLIP (object);

    switch (prop_id)
    {
    case PROP_TEXT:
        lrg_reel_text_clip_set_text (self, g_value_get_string (value));
        break;
    case PROP_FONT:
        lrg_reel_text_clip_set_font (self,
                                     GRL_IMAGE_FONT (g_value_get_object (value)));
        break;
    case PROP_FONT_SIZE:
        lrg_reel_text_clip_set_font_size (self, g_value_get_double (value));
        break;
    case PROP_COLOR:
        lrg_reel_text_clip_set_color (self,
                                      (const GrlColor *) g_value_get_boxed (value));
        break;
    case PROP_ALIGN:
        lrg_reel_text_clip_set_align (self, g_value_get_enum (value));
        break;
    case PROP_TEXT_X:
        lrg_reel_text_clip_set_text_x (self, g_value_get_int (value));
        break;
    case PROP_TEXT_Y:
        lrg_reel_text_clip_set_text_y (self, g_value_get_int (value));
        break;
    case PROP_MAX_WIDTH:
        lrg_reel_text_clip_set_max_width (self, g_value_get_int (value));
        break;
    case PROP_LINE_HEIGHT:
        lrg_reel_text_clip_set_line_height (self, g_value_get_double (value));
        break;
    case PROP_SHADOW:
        lrg_reel_text_clip_set_shadow (self, g_value_get_boolean (value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_reel_text_clip_class_init (LrgReelTextClipClass *klass)
{
    GObjectClass     *object_class = G_OBJECT_CLASS (klass);
    LrgReelClipClass *clip_class   = LRG_REEL_CLIP_CLASS (klass);

    object_class->finalize     = lrg_reel_text_clip_finalize;
    object_class->get_property = lrg_reel_text_clip_get_property;
    object_class->set_property = lrg_reel_text_clip_set_property;

    clip_class->render = lrg_reel_text_clip_render;

    /**
     * LrgReelTextClip:text:
     *
     * The UTF-8 text string to render.  Newlines produce line breaks;
     * additional word-wrap is controlled by #LrgReelTextClip:max-width.
     *
     * Since: 1.0
     */
    properties[PROP_TEXT] =
        g_param_spec_string ("text", "Text",
                             "UTF-8 text string",
                             NULL,
                             G_PARAM_READWRITE |
                             G_PARAM_STATIC_STRINGS |
                             G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgReelTextClip:font:
     *
     * The #GrlImageFont used for rendering, or %NULL to use the built-in
     * 8×8 bitmap font.
     *
     * Since: 1.0
     */
    properties[PROP_FONT] =
        g_param_spec_object ("font", "Font",
                             "GrlImageFont for text rendering (NULL = bitmap font)",
                             GRL_TYPE_IMAGE_FONT,
                             G_PARAM_READWRITE |
                             G_PARAM_STATIC_STRINGS |
                             G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgReelTextClip:font-size:
     *
     * Font size in pixels.  For the bitmap font this is rounded to the
     * nearest integer.
     *
     * Since: 1.0
     */
    properties[PROP_FONT_SIZE] =
        g_param_spec_double ("font-size", "Font Size",
                             "Font size in pixels",
                             1.0, G_MAXDOUBLE, 32.0,
                             G_PARAM_READWRITE |
                             G_PARAM_STATIC_STRINGS |
                             G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgReelTextClip:color:
     *
     * The foreground color of the rendered text.
     *
     * Since: 1.0
     */
    properties[PROP_COLOR] =
        g_param_spec_boxed ("color", "Color",
                            "Text foreground color",
                            GRL_TYPE_COLOR,
                            G_PARAM_READWRITE |
                            G_PARAM_STATIC_STRINGS |
                            G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgReelTextClip:align:
     *
     * Horizontal alignment of the text block.
     *
     * Since: 1.0
     */
    properties[PROP_ALIGN] =
        g_param_spec_enum ("align", "Align",
                           "Horizontal text alignment",
                           LRG_TYPE_REEL_TEXT_ALIGN,
                           LRG_REEL_TEXT_ALIGN_LEFT,
                           G_PARAM_READWRITE |
                           G_PARAM_STATIC_STRINGS |
                           G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgReelTextClip:text-x:
     *
     * X pixel coordinate of the text block's top-left corner within the frame.
     * Note: this is the text content position, distinct from the inherited
     * clip transform property "x".
     *
     * Since: 1.0
     */
    properties[PROP_TEXT_X] =
        g_param_spec_int ("text-x", "Text X",
                          "X coordinate of the text block in frame pixels",
                          G_MININT, G_MAXINT, 0,
                          G_PARAM_READWRITE |
                          G_PARAM_STATIC_STRINGS |
                          G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgReelTextClip:text-y:
     *
     * Y pixel coordinate of the text block's top edge within the frame.
     * Note: this is the text content position, distinct from the inherited
     * clip transform property "y".
     *
     * Since: 1.0
     */
    properties[PROP_TEXT_Y] =
        g_param_spec_int ("text-y", "Text Y",
                          "Y coordinate of the text block in frame pixels",
                          G_MININT, G_MAXINT, 0,
                          G_PARAM_READWRITE |
                          G_PARAM_STATIC_STRINGS |
                          G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgReelTextClip:max-width:
     *
     * Maximum line width in pixels for greedy word-wrap.  Set to 0 (the
     * default) to disable word-wrapping; only explicit '\n' characters
     * produce line breaks.
     *
     * Since: 1.0
     */
    properties[PROP_MAX_WIDTH] =
        g_param_spec_int ("max-width", "Max Width",
                          "Word-wrap column in pixels (0 = disabled)",
                          0, G_MAXINT, 0,
                          G_PARAM_READWRITE |
                          G_PARAM_STATIC_STRINGS |
                          G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgReelTextClip:line-height:
     *
     * Multiplier applied to the font's natural line height when computing
     * the vertical distance between successive lines.
     *
     * Since: 1.0
     */
    properties[PROP_LINE_HEIGHT] =
        g_param_spec_double ("line-height", "Line Height",
                             "Line-height multiplier (default 1.2)",
                             0.01, 100.0, 1.2,
                             G_PARAM_READWRITE |
                             G_PARAM_STATIC_STRINGS |
                             G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgReelTextClip:shadow:
     *
     * Whether a drop shadow is drawn behind each line of text.
     *
     * Since: 1.0
     */
    properties[PROP_SHADOW] =
        g_param_spec_boolean ("shadow", "Shadow",
                              "Enable drop-shadow rendering",
                              FALSE,
                              G_PARAM_READWRITE |
                              G_PARAM_STATIC_STRINGS |
                              G_PARAM_EXPLICIT_NOTIFY);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_reel_text_clip_init (LrgReelTextClip *self)
{
    self->text          = NULL;
    self->font          = NULL;
    self->font_size     = 32.0;
    self->color.r       = 255;
    self->color.g       = 255;
    self->color.b       = 255;
    self->color.a       = 255;
    self->align         = LRG_REEL_TEXT_ALIGN_LEFT;
    self->text_x        = 0;
    self->text_y        = 0;
    self->max_width     = 0;
    self->line_height   = 1.2;
    self->shadow        = FALSE;
    self->shadow_color.r = 0;
    self->shadow_color.g = 0;
    self->shadow_color.b = 0;
    self->shadow_color.a = 255;
    self->shadow_dx     = 2;
    self->shadow_dy     = 2;
}

/* ==========================================================================
 * Constructors
 * ========================================================================== */

/**
 * lrg_reel_text_clip_new:
 * @text: (nullable): UTF-8 text to display, or %NULL for an empty clip.
 *
 * Creates a new #LrgReelTextClip using the built-in 8×8 bitmap font.  The
 * clip works headless (no GL context / font file required).
 *
 * Returns: (transfer full): a new #LrgReelTextClip.
 *
 * Since: 1.0
 */
LrgReelTextClip *
lrg_reel_text_clip_new (const gchar *text)
{
    LrgReelTextClip *self;

    self = g_object_new (LRG_TYPE_REEL_TEXT_CLIP, NULL);

    if (text != NULL)
        self->text = g_strdup (text);

    return self;
}

/**
 * lrg_reel_text_clip_new_with_font:
 * @text: (nullable): UTF-8 text to display, or %NULL for an empty clip.
 * @font: (nullable): a #GrlImageFont to use for rendering, or %NULL to fall
 *   back to the bitmap font.
 *
 * Creates a new #LrgReelTextClip that renders with @font when non-%NULL.
 * The clip takes a reference to @font; the caller may unref it after this
 * call.
 *
 * Returns: (transfer full): a new #LrgReelTextClip.
 *
 * Since: 1.0
 */
LrgReelTextClip *
lrg_reel_text_clip_new_with_font (const gchar  *text,
                                   GrlImageFont *font)
{
    LrgReelTextClip *self;

    self = g_object_new (LRG_TYPE_REEL_TEXT_CLIP, NULL);

    if (text != NULL)
        self->text = g_strdup (text);

    if (font != NULL)
        self->font = g_object_ref (font);

    return self;
}

/**
 * lrg_reel_text_clip_set_font_from_file:
 * @self: an #LrgReelTextClip.
 * @path: (type filename): path to a TTF or OTF font file.
 * @error: (nullable): return location for error, or %NULL.
 *
 * Loads a font from @path and sets it as the active font for this clip,
 * replacing any previously set font.
 *
 * Returns: %TRUE on success; %FALSE on error (see @error).
 *
 * Since: 1.0
 */
gboolean
lrg_reel_text_clip_set_font_from_file (LrgReelTextClip  *self,
                                        const gchar      *path,
                                        GError          **error)
{
    GrlImageFont *font;

    g_return_val_if_fail (LRG_IS_REEL_TEXT_CLIP (self), FALSE);
    g_return_val_if_fail (path != NULL, FALSE);

    font = grl_image_font_new_from_file (path, error);
    if (font == NULL)
        return FALSE;

    g_clear_object (&self->font);
    self->font = font; /* already transfer-full */

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_FONT]);
    return TRUE;
}

/* ==========================================================================
 * Text accessors
 * ========================================================================== */

/**
 * lrg_reel_text_clip_get_text:
 * @self: an #LrgReelTextClip.
 *
 * Returns: (transfer none) (nullable): the current text string.
 *
 * Since: 1.0
 */
const gchar *
lrg_reel_text_clip_get_text (LrgReelTextClip *self)
{
    g_return_val_if_fail (LRG_IS_REEL_TEXT_CLIP (self), NULL);
    return self->text;
}

/**
 * lrg_reel_text_clip_set_text:
 * @self: an #LrgReelTextClip.
 * @text: (nullable): new UTF-8 text string, or %NULL to clear.
 *
 * Since: 1.0
 */
void
lrg_reel_text_clip_set_text (LrgReelTextClip *self,
                              const gchar     *text)
{
    g_return_if_fail (LRG_IS_REEL_TEXT_CLIP (self));

    if (g_strcmp0 (self->text, text) == 0)
        return;

    g_free (self->text);
    self->text = g_strdup (text);
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TEXT]);
}

/* ==========================================================================
 * Font accessors
 * ========================================================================== */

/**
 * lrg_reel_text_clip_get_font:
 * @self: an #LrgReelTextClip.
 *
 * Returns: (transfer none) (nullable): the current #GrlImageFont.
 *
 * Since: 1.0
 */
GrlImageFont *
lrg_reel_text_clip_get_font (LrgReelTextClip *self)
{
    g_return_val_if_fail (LRG_IS_REEL_TEXT_CLIP (self), NULL);
    return self->font;
}

/**
 * lrg_reel_text_clip_set_font:
 * @self: an #LrgReelTextClip.
 * @font: (nullable): a #GrlImageFont, or %NULL to switch back to the bitmap font.
 *
 * Since: 1.0
 */
void
lrg_reel_text_clip_set_font (LrgReelTextClip *self,
                              GrlImageFont    *font)
{
    g_return_if_fail (LRG_IS_REEL_TEXT_CLIP (self));
    g_return_if_fail (font == NULL || GRL_IS_IMAGE_FONT (font));

    if (self->font == font)
        return;

    g_clear_object (&self->font);
    if (font != NULL)
        self->font = g_object_ref (font);

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_FONT]);
}

/* ==========================================================================
 * Font size accessors
 * ========================================================================== */

/**
 * lrg_reel_text_clip_get_font_size:
 * @self: an #LrgReelTextClip.
 *
 * Returns: the font size in pixels.
 *
 * Since: 1.0
 */
gdouble
lrg_reel_text_clip_get_font_size (LrgReelTextClip *self)
{
    g_return_val_if_fail (LRG_IS_REEL_TEXT_CLIP (self), 32.0);
    return self->font_size;
}

/**
 * lrg_reel_text_clip_set_font_size:
 * @self: an #LrgReelTextClip.
 * @font_size: font size in pixels (must be > 0).
 *
 * Since: 1.0
 */
void
lrg_reel_text_clip_set_font_size (LrgReelTextClip *self,
                                   gdouble          font_size)
{
    g_return_if_fail (LRG_IS_REEL_TEXT_CLIP (self));
    g_return_if_fail (font_size > 0.0);

    if (self->font_size == font_size)
        return;

    self->font_size = font_size;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_FONT_SIZE]);
}

/* ==========================================================================
 * Color accessors
 * ========================================================================== */

/**
 * lrg_reel_text_clip_get_color:
 * @self: an #LrgReelTextClip.
 * @out_color: (out caller-allocates): return location for the text color.
 *
 * Since: 1.0
 */
void
lrg_reel_text_clip_get_color (LrgReelTextClip *self,
                               GrlColor        *out_color)
{
    g_return_if_fail (LRG_IS_REEL_TEXT_CLIP (self));
    g_return_if_fail (out_color != NULL);

    *out_color = self->color;
}

/**
 * lrg_reel_text_clip_set_color:
 * @self: an #LrgReelTextClip.
 * @color: new text color.
 *
 * Since: 1.0
 */
void
lrg_reel_text_clip_set_color (LrgReelTextClip *self,
                               const GrlColor  *color)
{
    g_return_if_fail (LRG_IS_REEL_TEXT_CLIP (self));
    g_return_if_fail (color != NULL);

    self->color = *color;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_COLOR]);
}

/* ==========================================================================
 * Alignment accessors
 * ========================================================================== */

/**
 * lrg_reel_text_clip_get_align:
 * @self: an #LrgReelTextClip.
 *
 * Returns: the current #LrgReelTextAlign value.
 *
 * Since: 1.0
 */
LrgReelTextAlign
lrg_reel_text_clip_get_align (LrgReelTextClip *self)
{
    g_return_val_if_fail (LRG_IS_REEL_TEXT_CLIP (self),
                          LRG_REEL_TEXT_ALIGN_LEFT);
    return self->align;
}

/**
 * lrg_reel_text_clip_set_align:
 * @self: an #LrgReelTextClip.
 * @align: the desired text alignment.
 *
 * Since: 1.0
 */
void
lrg_reel_text_clip_set_align (LrgReelTextClip  *self,
                               LrgReelTextAlign  align)
{
    g_return_if_fail (LRG_IS_REEL_TEXT_CLIP (self));

    if (self->align == align)
        return;

    self->align = align;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ALIGN]);
}

/* ==========================================================================
 * Position accessors
 * ========================================================================== */

/**
 * lrg_reel_text_clip_get_text_x:
 * @self: an #LrgReelTextClip.
 *
 * Returns: the X pixel coordinate of the text block's top-left corner.
 *
 * Since: 1.0
 */
gint
lrg_reel_text_clip_get_text_x (LrgReelTextClip *self)
{
    g_return_val_if_fail (LRG_IS_REEL_TEXT_CLIP (self), 0);
    return self->text_x;
}

/**
 * lrg_reel_text_clip_set_text_x:
 * @self: an #LrgReelTextClip.
 * @text_x: X coordinate in frame pixels.
 *
 * Since: 1.0
 */
void
lrg_reel_text_clip_set_text_x (LrgReelTextClip *self,
                                gint             text_x)
{
    g_return_if_fail (LRG_IS_REEL_TEXT_CLIP (self));

    if (self->text_x == text_x)
        return;

    self->text_x = text_x;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TEXT_X]);
}

/**
 * lrg_reel_text_clip_get_text_y:
 * @self: an #LrgReelTextClip.
 *
 * Returns: the Y pixel coordinate of the text block's top edge.
 *
 * Since: 1.0
 */
gint
lrg_reel_text_clip_get_text_y (LrgReelTextClip *self)
{
    g_return_val_if_fail (LRG_IS_REEL_TEXT_CLIP (self), 0);
    return self->text_y;
}

/**
 * lrg_reel_text_clip_set_text_y:
 * @self: an #LrgReelTextClip.
 * @text_y: Y coordinate in frame pixels.
 *
 * Since: 1.0
 */
void
lrg_reel_text_clip_set_text_y (LrgReelTextClip *self,
                                gint             text_y)
{
    g_return_if_fail (LRG_IS_REEL_TEXT_CLIP (self));

    if (self->text_y == text_y)
        return;

    self->text_y = text_y;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TEXT_Y]);
}

/* ==========================================================================
 * Max-width accessor
 * ========================================================================== */

/**
 * lrg_reel_text_clip_get_max_width:
 * @self: an #LrgReelTextClip.
 *
 * Returns: the word-wrap column in pixels, or 0 if wrapping is disabled.
 *
 * Since: 1.0
 */
gint
lrg_reel_text_clip_get_max_width (LrgReelTextClip *self)
{
    g_return_val_if_fail (LRG_IS_REEL_TEXT_CLIP (self), 0);
    return self->max_width;
}

/**
 * lrg_reel_text_clip_set_max_width:
 * @self: an #LrgReelTextClip.
 * @max_width: maximum line width in pixels, or 0 to disable word-wrapping.
 *
 * Since: 1.0
 */
void
lrg_reel_text_clip_set_max_width (LrgReelTextClip *self,
                                   gint             max_width)
{
    g_return_if_fail (LRG_IS_REEL_TEXT_CLIP (self));
    g_return_if_fail (max_width >= 0);

    if (self->max_width == max_width)
        return;

    self->max_width = max_width;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MAX_WIDTH]);
}

/* ==========================================================================
 * Line height accessor
 * ========================================================================== */

/**
 * lrg_reel_text_clip_get_line_height:
 * @self: an #LrgReelTextClip.
 *
 * Returns: the line-height multiplier.
 *
 * Since: 1.0
 */
gdouble
lrg_reel_text_clip_get_line_height (LrgReelTextClip *self)
{
    g_return_val_if_fail (LRG_IS_REEL_TEXT_CLIP (self), 1.2);
    return self->line_height;
}

/**
 * lrg_reel_text_clip_set_line_height:
 * @self: an #LrgReelTextClip.
 * @line_height: line-height multiplier (must be > 0).
 *
 * Since: 1.0
 */
void
lrg_reel_text_clip_set_line_height (LrgReelTextClip *self,
                                     gdouble          line_height)
{
    g_return_if_fail (LRG_IS_REEL_TEXT_CLIP (self));
    g_return_if_fail (line_height > 0.0);

    if (self->line_height == line_height)
        return;

    self->line_height = line_height;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_LINE_HEIGHT]);
}

/* ==========================================================================
 * Shadow accessors
 * ========================================================================== */

/**
 * lrg_reel_text_clip_get_shadow:
 * @self: an #LrgReelTextClip.
 *
 * Returns: %TRUE if drop-shadow rendering is enabled.
 *
 * Since: 1.0
 */
gboolean
lrg_reel_text_clip_get_shadow (LrgReelTextClip *self)
{
    g_return_val_if_fail (LRG_IS_REEL_TEXT_CLIP (self), FALSE);
    return self->shadow;
}

/**
 * lrg_reel_text_clip_set_shadow:
 * @self: an #LrgReelTextClip.
 * @shadow: %TRUE to enable drop-shadow rendering.
 *
 * Since: 1.0
 */
void
lrg_reel_text_clip_set_shadow (LrgReelTextClip *self,
                                gboolean         shadow)
{
    g_return_if_fail (LRG_IS_REEL_TEXT_CLIP (self));

    shadow = !!shadow;
    if (self->shadow == shadow)
        return;

    self->shadow = shadow;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SHADOW]);
}

/**
 * lrg_reel_text_clip_get_shadow_color:
 * @self: an #LrgReelTextClip.
 * @out_color: (out caller-allocates): return location for the shadow color.
 *
 * Since: 1.0
 */
void
lrg_reel_text_clip_get_shadow_color (LrgReelTextClip *self,
                                      GrlColor        *out_color)
{
    g_return_if_fail (LRG_IS_REEL_TEXT_CLIP (self));
    g_return_if_fail (out_color != NULL);

    *out_color = self->shadow_color;
}

/**
 * lrg_reel_text_clip_set_shadow_color:
 * @self: an #LrgReelTextClip.
 * @color: the new shadow color.
 *
 * Since: 1.0
 */
void
lrg_reel_text_clip_set_shadow_color (LrgReelTextClip *self,
                                      const GrlColor  *color)
{
    g_return_if_fail (LRG_IS_REEL_TEXT_CLIP (self));
    g_return_if_fail (color != NULL);

    self->shadow_color = *color;
}

/**
 * lrg_reel_text_clip_get_shadow_offset:
 * @self: an #LrgReelTextClip.
 * @out_dx: (out) (optional): return location for the horizontal offset.
 * @out_dy: (out) (optional): return location for the vertical offset.
 *
 * Since: 1.0
 */
void
lrg_reel_text_clip_get_shadow_offset (LrgReelTextClip *self,
                                       gint            *out_dx,
                                       gint            *out_dy)
{
    g_return_if_fail (LRG_IS_REEL_TEXT_CLIP (self));

    if (out_dx) *out_dx = self->shadow_dx;
    if (out_dy) *out_dy = self->shadow_dy;
}

/**
 * lrg_reel_text_clip_set_shadow_offset:
 * @self: an #LrgReelTextClip.
 * @dx: horizontal shadow offset in pixels (positive = right).
 * @dy: vertical shadow offset in pixels (positive = down).
 *
 * Since: 1.0
 */
void
lrg_reel_text_clip_set_shadow_offset (LrgReelTextClip *self,
                                       gint             dx,
                                       gint             dy)
{
    g_return_if_fail (LRG_IS_REEL_TEXT_CLIP (self));

    self->shadow_dx = dx;
    self->shadow_dy = dy;
}

/* ==========================================================================
 * Layout utility: lrg_reel_measure_text
 * ========================================================================== */

/**
 * lrg_reel_measure_text:
 * @font: (nullable): a #GrlImageFont, or %NULL for the bitmap font.
 * @text: (nullable): UTF-8 text to measure.
 * @font_size: font size in pixels.
 * @out_width: (out) (optional): return location for the total width.
 * @out_height: (out) (optional): return location for the total height.
 *
 * Measures the bounding box of @text rendered at @font_size with @font.
 * Multi-line text (newline-separated) returns the maximum line width and
 * n_lines × natural line height.
 *
 * Since: 1.0
 */
void
lrg_reel_measure_text (GrlImageFont *font,
                       const gchar  *text,
                       gdouble       font_size,
                       gint         *out_width,
                       gint         *out_height)
{
    gchar             **lines;
    gint                n_lines;
    gint                i;
    gint                max_w;
    gint                lh;
    gfloat              ascent;
    gfloat              descent;
    gfloat              gap;

    if (out_width)  *out_width  = 0;
    if (out_height) *out_height = 0;

    if (text == NULL || text[0] == '\0' || font_size <= 0.0)
        return;

    lines   = g_strsplit (text, "\n", -1);
    n_lines = 0;
    max_w   = 0;

    for (i = 0; lines[i] != NULL; i++)
    {
        GrlVector2 *sz;
        gint        lw;

        n_lines++;

        if (font != NULL)
            sz = grl_image_measure_text_ttf (font, lines[i], (gfloat) font_size);
        else
            sz = grl_image_measure_text_bitmap (lines[i], (gint) font_size);

        lw = (sz != NULL) ? (gint) sz->x : 0;
        if (sz != NULL)
            grl_vector2_free (sz);
        if (lw > max_w)
            max_w = lw;
    }

    g_strfreev (lines);

    if (n_lines == 0)
        return;

    /* Compute line height. */
    if (font != NULL)
    {
        grl_image_font_get_v_metrics (font, (gfloat) font_size,
                                      &ascent, &descent, &gap);
        lh = (gint) (ascent - descent + gap + 0.5f);
        if (lh < 1)
            lh = (gint) (font_size + 0.5);
    }
    else
    {
        lh = (gint) (font_size + 0.5);
    }

    if (out_width)  *out_width  = max_w;
    if (out_height) *out_height = n_lines * lh;
}

/* ==========================================================================
 * Layout utility: lrg_reel_fit_text
 * ========================================================================== */

/**
 * lrg_reel_fit_text:
 * @font: (nullable): a #GrlImageFont, or %NULL for the bitmap font.
 * @text: (nullable): UTF-8 text (single logical line).
 * @box_width: available width in pixels (must be > 0).
 * @box_height: available height in pixels (must be > 0).
 * @max_font_size: upper bound for the returned size (must be >= 1).
 *
 * Returns the largest font size ≤ @max_font_size at which @text fits within
 * @box_width × @box_height.  The search step is 0.5 pixels.  Returns 1.0 if
 * even size 1 does not fit.
 *
 * Returns: the largest fitting font size.
 *
 * Since: 1.0
 */
gdouble
lrg_reel_fit_text (GrlImageFont *font,
                   const gchar  *text,
                   gint          box_width,
                   gint          box_height,
                   gdouble       max_font_size)
{
    gdouble size;

    g_return_val_if_fail (box_width > 0, 1.0);
    g_return_val_if_fail (box_height > 0, 1.0);
    g_return_val_if_fail (max_font_size >= 1.0, 1.0);

    if (text == NULL || text[0] == '\0')
        return max_font_size;

    /* Linear descent from max_font_size in 0.5-pixel steps. */
    size = max_font_size;
    while (size >= 1.0)
    {
        gint tw;
        gint th;

        lrg_reel_measure_text (font, text, size, &tw, &th);

        if (tw <= box_width && th <= box_height)
            return size;

        size -= 0.5;
    }

    return 1.0;
}
