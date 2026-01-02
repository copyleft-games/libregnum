/* lrg-label.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Simple text display widget.
 */

#include "config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_UI

#include "lrg-label.h"
#include "lrg-theme.h"
#include "../lrg-log.h"

/* ==========================================================================
 * Private Data
 * ========================================================================== */

struct _LrgLabel
{
    LrgWidget         parent_instance;

    gchar            *text;
    GrlFont          *font;
    gfloat            font_size;
    GrlColor          color;
    LrgTextAlignment  alignment;
};

G_DEFINE_TYPE (LrgLabel, lrg_label, LRG_TYPE_WIDGET)

enum
{
    PROP_0,
    PROP_TEXT,
    PROP_FONT,
    PROP_FONT_SIZE,
    PROP_COLOR,
    PROP_ALIGNMENT,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

/* Default color (white) */
static const GrlColor DEFAULT_COLOR = { 255, 255, 255, 255 };

/* ==========================================================================
 * Virtual Method Implementations
 * ========================================================================== */

static void
lrg_label_draw (LrgWidget *widget)
{
    LrgLabel              *self = LRG_LABEL (widget);
    g_autoptr(GrlVector2)  pos = NULL;
    GrlFont               *font_to_use;
    gfloat                 text_width;

    if (self->text == NULL || self->text[0] == '\0')
    {
        return;
    }

    pos = grl_vector2_new (lrg_widget_get_world_x (widget),
                           lrg_widget_get_world_y (widget));

    /* Determine which font to use: widget font -> theme font -> raylib default */
    if (self->font != NULL)
    {
        font_to_use = self->font;
    }
    else
    {
        LrgTheme *theme = lrg_theme_get_default ();
        font_to_use = lrg_theme_get_default_font (theme);
    }

    /* Handle text alignment */
    if (self->alignment != LRG_TEXT_ALIGN_LEFT)
    {
        if (font_to_use != NULL)
        {
            g_autoptr(GrlVector2) measured = NULL;
            measured = grl_font_measure_text (font_to_use, self->text,
                                              self->font_size, 1.0f);
            text_width = measured->x;
        }
        else
        {
            /* Estimate for raylib default font */
            text_width = (gfloat)g_utf8_strlen (self->text, -1) * (self->font_size * 0.6f);
        }

        if (self->alignment == LRG_TEXT_ALIGN_CENTER)
        {
            pos->x += (lrg_widget_get_width (widget) - text_width) / 2.0f;
        }
        else if (self->alignment == LRG_TEXT_ALIGN_RIGHT)
        {
            pos->x += lrg_widget_get_width (widget) - text_width;
        }
    }

    /* Draw with font or fallback to raylib default */
    if (font_to_use != NULL)
    {
        grl_draw_text_ex (font_to_use, self->text, pos,
                          self->font_size, 1.0f, &self->color);
    }
    else
    {
        /* Last resort: raylib default bitmap font */
        grl_draw_text (self->text, (gint)pos->x, (gint)pos->y,
                       (gint)self->font_size, &self->color);
    }
}

static void
lrg_label_measure (LrgWidget *widget,
                   gfloat    *preferred_width,
                   gfloat    *preferred_height)
{
    LrgLabel              *self = LRG_LABEL (widget);
    g_autoptr(GrlVector2)  size = NULL;
    GrlFont               *font_to_use;

    if (self->text == NULL || self->text[0] == '\0')
    {
        if (preferred_width != NULL)
        {
            *preferred_width = 0.0f;
        }
        if (preferred_height != NULL)
        {
            *preferred_height = self->font_size;
        }
        return;
    }

    /* Determine which font to use: widget font -> theme font -> fallback estimate */
    if (self->font != NULL)
    {
        font_to_use = self->font;
    }
    else
    {
        LrgTheme *theme = lrg_theme_get_default ();
        font_to_use = lrg_theme_get_default_font (theme);
    }

    if (font_to_use != NULL)
    {
        size = grl_font_measure_text (font_to_use, self->text,
                                      self->font_size, 1.0f);
    }
    else
    {
        /* Estimate for raylib default font */
        size = grl_vector2_new ((gfloat)g_utf8_strlen (self->text, -1) * (self->font_size * 0.6f),
                                self->font_size);
    }

    if (preferred_width != NULL)
    {
        *preferred_width = size->x;
    }
    if (preferred_height != NULL)
    {
        *preferred_height = size->y;
    }
}

/* ==========================================================================
 * GObject Implementation
 * ========================================================================== */

static void
lrg_label_finalize (GObject *object)
{
    LrgLabel *self = LRG_LABEL (object);

    g_free (self->text);
    g_clear_object (&self->font);

    G_OBJECT_CLASS (lrg_label_parent_class)->finalize (object);
}

static void
lrg_label_get_property (GObject    *object,
                        guint       prop_id,
                        GValue     *value,
                        GParamSpec *pspec)
{
    LrgLabel *self = LRG_LABEL (object);

    switch (prop_id)
    {
    case PROP_TEXT:
        g_value_set_string (value, self->text);
        break;
    case PROP_FONT:
        g_value_set_object (value, self->font);
        break;
    case PROP_FONT_SIZE:
        g_value_set_float (value, self->font_size);
        break;
    case PROP_COLOR:
        g_value_set_boxed (value, &self->color);
        break;
    case PROP_ALIGNMENT:
        g_value_set_enum (value, self->alignment);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_label_set_property (GObject      *object,
                        guint         prop_id,
                        const GValue *value,
                        GParamSpec   *pspec)
{
    LrgLabel *self = LRG_LABEL (object);

    switch (prop_id)
    {
    case PROP_TEXT:
        lrg_label_set_text (self, g_value_get_string (value));
        break;
    case PROP_FONT:
        lrg_label_set_font (self, g_value_get_object (value));
        break;
    case PROP_FONT_SIZE:
        lrg_label_set_font_size (self, g_value_get_float (value));
        break;
    case PROP_COLOR:
        lrg_label_set_color (self, g_value_get_boxed (value));
        break;
    case PROP_ALIGNMENT:
        lrg_label_set_alignment (self, g_value_get_enum (value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_label_class_init (LrgLabelClass *klass)
{
    GObjectClass   *object_class = G_OBJECT_CLASS (klass);
    LrgWidgetClass *widget_class = LRG_WIDGET_CLASS (klass);

    object_class->finalize = lrg_label_finalize;
    object_class->get_property = lrg_label_get_property;
    object_class->set_property = lrg_label_set_property;

    widget_class->draw = lrg_label_draw;
    widget_class->measure = lrg_label_measure;

    /**
     * LrgLabel:text:
     *
     * The text to display.
     */
    properties[PROP_TEXT] =
        g_param_spec_string ("text",
                             "Text",
                             "The text to display",
                             NULL,
                             G_PARAM_READWRITE |
                             G_PARAM_STATIC_STRINGS |
                             G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgLabel:font:
     *
     * The font to use for rendering.
     * If %NULL, the default font is used.
     */
    properties[PROP_FONT] =
        g_param_spec_object ("font",
                             "Font",
                             "The font to use",
                             GRL_TYPE_FONT,
                             G_PARAM_READWRITE |
                             G_PARAM_STATIC_STRINGS |
                             G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgLabel:font-size:
     *
     * The font size in pixels.
     */
    properties[PROP_FONT_SIZE] =
        g_param_spec_float ("font-size",
                            "Font Size",
                            "The font size in pixels",
                            1.0f, 256.0f, 20.0f,
                            G_PARAM_READWRITE |
                            G_PARAM_STATIC_STRINGS |
                            G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgLabel:color:
     *
     * The text color.
     */
    properties[PROP_COLOR] =
        g_param_spec_boxed ("color",
                            "Color",
                            "The text color",
                            GRL_TYPE_COLOR,
                            G_PARAM_READWRITE |
                            G_PARAM_STATIC_STRINGS |
                            G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgLabel:alignment:
     *
     * The text alignment within the widget's bounds.
     */
    properties[PROP_ALIGNMENT] =
        g_param_spec_enum ("alignment",
                           "Alignment",
                           "Text alignment",
                           LRG_TYPE_TEXT_ALIGNMENT,
                           LRG_TEXT_ALIGN_LEFT,
                           G_PARAM_READWRITE |
                           G_PARAM_STATIC_STRINGS |
                           G_PARAM_EXPLICIT_NOTIFY);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_label_init (LrgLabel *self)
{
    self->text = NULL;
    self->font = NULL;
    self->font_size = 20.0f;
    self->color = DEFAULT_COLOR;
    self->alignment = LRG_TEXT_ALIGN_LEFT;
}

/* ==========================================================================
 * Public API - Construction
 * ========================================================================== */

/**
 * lrg_label_new:
 * @text: (nullable): the label text
 *
 * Creates a new label widget.
 *
 * Returns: (transfer full): A new #LrgLabel
 */
LrgLabel *
lrg_label_new (const gchar *text)
{
    return g_object_new (LRG_TYPE_LABEL,
                         "text", text,
                         NULL);
}

/* ==========================================================================
 * Public API - Text
 * ========================================================================== */

/**
 * lrg_label_get_text:
 * @self: an #LrgLabel
 *
 * Gets the label's text.
 *
 * Returns: (transfer none) (nullable): The text
 */
const gchar *
lrg_label_get_text (LrgLabel *self)
{
    g_return_val_if_fail (LRG_IS_LABEL (self), NULL);

    return self->text;
}

/**
 * lrg_label_set_text:
 * @self: an #LrgLabel
 * @text: (nullable): the text to display
 *
 * Sets the label's text.
 */
void
lrg_label_set_text (LrgLabel    *self,
                    const gchar *text)
{
    g_return_if_fail (LRG_IS_LABEL (self));

    if (g_strcmp0 (self->text, text) != 0)
    {
        g_free (self->text);
        self->text = g_strdup (text);
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TEXT]);
    }
}

/* ==========================================================================
 * Public API - Font
 * ========================================================================== */

/**
 * lrg_label_get_font:
 * @self: an #LrgLabel
 *
 * Gets the label's font.
 *
 * Returns: (transfer none) (nullable): The font, or %NULL for default
 */
GrlFont *
lrg_label_get_font (LrgLabel *self)
{
    g_return_val_if_fail (LRG_IS_LABEL (self), NULL);

    return self->font;
}

/**
 * lrg_label_set_font:
 * @self: an #LrgLabel
 * @font: (nullable): the font to use, or %NULL for default
 *
 * Sets the label's font.
 */
void
lrg_label_set_font (LrgLabel *self,
                    GrlFont  *font)
{
    g_return_if_fail (LRG_IS_LABEL (self));

    if (g_set_object (&self->font, font))
    {
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_FONT]);
    }
}

/**
 * lrg_label_get_font_size:
 * @self: an #LrgLabel
 *
 * Gets the label's font size.
 *
 * Returns: The font size in pixels
 */
gfloat
lrg_label_get_font_size (LrgLabel *self)
{
    g_return_val_if_fail (LRG_IS_LABEL (self), 20.0f);

    return self->font_size;
}

/**
 * lrg_label_set_font_size:
 * @self: an #LrgLabel
 * @size: the font size in pixels
 *
 * Sets the label's font size.
 */
void
lrg_label_set_font_size (LrgLabel *self,
                         gfloat    size)
{
    g_return_if_fail (LRG_IS_LABEL (self));
    g_return_if_fail (size >= 1.0f);

    if (self->font_size != size)
    {
        self->font_size = size;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_FONT_SIZE]);
    }
}

/* ==========================================================================
 * Public API - Appearance
 * ========================================================================== */

/**
 * lrg_label_get_color:
 * @self: an #LrgLabel
 *
 * Gets the label's text color.
 *
 * Returns: (transfer none): The color
 */
const GrlColor *
lrg_label_get_color (LrgLabel *self)
{
    g_return_val_if_fail (LRG_IS_LABEL (self), &DEFAULT_COLOR);

    return &self->color;
}

/**
 * lrg_label_set_color:
 * @self: an #LrgLabel
 * @color: the text color
 *
 * Sets the label's text color.
 */
void
lrg_label_set_color (LrgLabel       *self,
                     const GrlColor *color)
{
    g_return_if_fail (LRG_IS_LABEL (self));
    g_return_if_fail (color != NULL);

    if (self->color.r != color->r ||
        self->color.g != color->g ||
        self->color.b != color->b ||
        self->color.a != color->a)
    {
        self->color = *color;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_COLOR]);
    }
}

/**
 * lrg_label_get_alignment:
 * @self: an #LrgLabel
 *
 * Gets the label's text alignment.
 *
 * Returns: The alignment
 */
LrgTextAlignment
lrg_label_get_alignment (LrgLabel *self)
{
    g_return_val_if_fail (LRG_IS_LABEL (self), LRG_TEXT_ALIGN_LEFT);

    return self->alignment;
}

/**
 * lrg_label_set_alignment:
 * @self: an #LrgLabel
 * @alignment: the text alignment
 *
 * Sets the label's text alignment.
 */
void
lrg_label_set_alignment (LrgLabel         *self,
                         LrgTextAlignment  alignment)
{
    g_return_if_fail (LRG_IS_LABEL (self));

    if (self->alignment != alignment)
    {
        self->alignment = alignment;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ALIGNMENT]);
    }
}
