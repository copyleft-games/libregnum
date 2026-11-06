/* lrg-label.c
 *
 * Copyright 2025-2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Text display widget.
 *
 * A label records its text through paint() with the theme's UI font.
 * Its LABEL part takes the theme's TEXT colour (TEXT_MUTED for the
 * "muted" class) and the size comes from the theme too: FONT_SIZE, or
 * FONT_SIZE_TITLE / _LARGE / _SMALL for the "title", "heading" and
 * "caption" classes. An explicitly set colour (applied in
 * style_overrides(), after the theme's style rules) or font size wins
 * over the theme. Text may span several lines; each line is aligned on its
 * own.
 */

#include "config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_UI

#include "lrg-label.h"
#include "lrg-theme.h"
#include "../lrg-log.h"
#include "../graphics/lrg-draw-cull-private.h"

/* ==========================================================================
 * Private Data
 * ========================================================================== */

/* Distance between baselines of consecutive lines, in font sizes (the
 * same factor lrg_paint_list_text() and lrg_paint_measure_text() use) */
#define LABEL_LINE_HEIGHT (1.2f)

struct _LrgLabel
{
    LrgWidget         parent_instance;

    gchar            *text;
    GrlFont          *font;
    gfloat            font_size;
    gboolean          font_size_set;
    GrlColor          color;
    gboolean          color_set;
    LrgTextAlignment  alignment;

    /* Measured text size, reused while text, font and size are unchanged
     * (labels are drawn every frame; their text rarely changes) */
    GrlFont          *measured_font;   /* font measured with (owned ref) */
    gfloat            measured_size;
    gfloat            measured_width;
    gfloat            measured_height;
    gboolean          measured_valid;
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

/* Legacy default colour, reported by the getter until a colour is set */
static const GrlColor DEFAULT_COLOR = { 255, 255, 255, 255 };

/* ==========================================================================
 * Helpers
 * ========================================================================== */

/* label_font: the label's own font, else the theme's UI font */
static GrlFont *
label_font (LrgLabel *self)
{
    if (self->font != NULL)
        return self->font;
    return lrg_theme_get_ui_font (lrg_widget_get_theme (LRG_WIDGET (self)));
}

/*
 * label_effective_size:
 *
 * The font size the label draws with: the explicitly set one, or the
 * theme size picked by the style class.
 */
static gfloat
label_effective_size (LrgLabel *self)
{
    LrgWidget *widget = LRG_WIDGET (self);
    LrgTheme  *theme;

    if (self->font_size_set)
        return self->font_size;

    theme = lrg_widget_get_theme (widget);
    if (lrg_widget_has_class (widget, "title"))
        return lrg_theme_get_metric (theme, LRG_THEME_METRIC_FONT_SIZE_TITLE);
    if (lrg_widget_has_class (widget, "heading"))
        return lrg_theme_get_metric (theme, LRG_THEME_METRIC_FONT_SIZE_LARGE);
    if (lrg_widget_has_class (widget, "caption"))
        return lrg_theme_get_metric (theme, LRG_THEME_METRIC_FONT_SIZE_SMALL);
    return lrg_theme_get_metric (theme, LRG_THEME_METRIC_FONT_SIZE);
}

/*
 * label_text_size:
 *
 * Size of the label's whole text in @font at @size, measured once and
 * cached until the text, font or size changes.
 */
static void
label_text_size (LrgLabel *self,
                 GrlFont  *font,
                 gfloat    size,
                 gfloat   *width,
                 gfloat   *height)
{
    if (!self->measured_valid || self->measured_font != font || self->measured_size != size)
    {
        lrg_paint_measure_text (font, self->text, size, 1.0f,
                                &self->measured_width, &self->measured_height);
        g_set_object (&self->measured_font, font);
        self->measured_size = size;
        self->measured_valid = TRUE;
    }

    *width = self->measured_width;
    *height = self->measured_height;
}

/*
 * label_offscreen:
 *
 * Whether the label, drawn immediately at its world position, cannot
 * touch the current render target. Only used on the legacy
 * lrg_widget_draw() path, where many labels are drawn every frame and
 * long lists often sit far outside the screen. The measured box is
 * widened by two ems on every side to cover glyph overhang; the
 * visible rectangle comes from rlgl's live matrices, so cameras and
 * render textures are respected and culling never changes a pixel.
 */
static gboolean
label_offscreen (LrgLabel *self)
{
    LrgWidget    *widget = LRG_WIDGET (self);
    GrlRectangle  view;
    gfloat        size = label_effective_size (self);
    gfloat        width;
    gfloat        height;
    gfloat        margin;
    gfloat        x;
    gfloat        y;

    if (!lrg_draw_cull_visible_rect_2d (&view))
        return FALSE;

    label_text_size (self, label_font (self), size, &width, &height);
    width = MAX (width, lrg_widget_get_width (widget));
    height = MAX (height, lrg_widget_get_height (widget));
    x = lrg_widget_get_world_x (widget);
    y = lrg_widget_get_world_y (widget);

    margin = MAX (size, 1.0f) * 2.0f;
    return x + width + margin < view.x ||
           y + height + margin < view.y ||
           x - margin > view.x + view.width ||
           y - margin > view.y + view.height;
}

/* ==========================================================================
 * Looks
 * ========================================================================== */

/*
 * lrg_label_style_defaults:
 *
 * LABEL: TEXT (TEXT_MUTED for "muted", DISABLED when insensitive).
 * An explicit colour is applied later, in style_overrides().
 */
static void
lrg_label_style_defaults (LrgWidget      *widget,
                          LrgUIPart       part,
                          LrgWidgetState  state,
                          LrgPartLook    *look)
{
    LrgTheme *theme = lrg_widget_get_theme (widget);

    if (part != LRG_UI_PART_LABEL)
    {
        LRG_WIDGET_CLASS (lrg_label_parent_class)->style_defaults (widget, part, state, look);
        return;
    }

    if (state & LRG_WIDGET_STATE_DISABLED)
        lrg_part_look_set_foreground (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_DISABLED));
    else if (lrg_widget_has_class (widget, "muted"))
        lrg_part_look_set_foreground (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_TEXT_MUTED));
    else
        lrg_part_look_set_foreground (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_TEXT));
}

/*
 * lrg_label_style_overrides:
 *
 * LABEL: an explicit colour beats the defaults and the theme's style
 * rules in every state but the disabled one.
 */
static void
lrg_label_style_overrides (LrgWidget      *widget,
                           LrgUIPart       part,
                           LrgWidgetState  state,
                           LrgPartLook    *look)
{
    LrgLabel       *self = LRG_LABEL (widget);
    LrgWidgetClass *parent = LRG_WIDGET_CLASS (lrg_label_parent_class);

    if (parent->style_overrides != NULL)
        parent->style_overrides (widget, part, state, look);

    if (part == LRG_UI_PART_LABEL && self->color_set && !(state & LRG_WIDGET_STATE_DISABLED))
        lrg_part_look_set_foreground (look, &self->color);
}

/* ==========================================================================
 * Virtual Method Implementations
 * ========================================================================== */

/*
 * lrg_label_paint:
 *
 * Records the text line by line so each line can be aligned within the
 * label's width. When the label is taller than its text (e.g. a row
 * stretched it next to a button), the block is centred vertically;
 * otherwise it starts at the top edge as it always has.
 */
static void
lrg_label_paint (LrgWidget    *widget,
                 LrgPaintList *list)
{
    LrgLabel     *self = LRG_LABEL (widget);
    LrgPartLook   look;
    GrlFont      *font;
    GrlColor      fg;
    gfloat        size;
    gfloat        x;
    gfloat        y;
    gfloat        w;
    gfloat        h;
    gfloat        tw;
    gfloat        th;
    gchar       **lines;
    guint         i;
    LrgTextAlignment align;

    if (self->text == NULL || self->text[0] == '\0')
        return;

    /* Right-to-left: LEFT means the leading edge, so left and right
     * swap (the default LEFT alignment hugs the right edge) */
    align = self->alignment;
    if (lrg_widget_is_rtl (widget) && align != LRG_TEXT_ALIGN_CENTER)
        align = align == LRG_TEXT_ALIGN_LEFT ? LRG_TEXT_ALIGN_RIGHT : LRG_TEXT_ALIGN_LEFT;

    font = label_font (self);
    size = label_effective_size (self);
    x = lrg_widget_get_world_x (widget);
    y = lrg_widget_get_world_y (widget);
    w = lrg_widget_get_width (widget);
    h = lrg_widget_get_height (widget);

    lrg_widget_resolve_look (widget, LRG_UI_PART_LABEL, &look);
    lrg_paint_color_with_alpha (&look.foreground,
                                ((gfloat) look.foreground.a / 255.0f) * look.opacity, &fg);

    /* Vertical placement of the whole block */
    label_text_size (self, font, size, &tw, &th);
    if (h > th)
        y += (h - th) * 0.5f;

    /* Single line, left aligned: one call, no splitting */
    if (align == LRG_TEXT_ALIGN_LEFT && strchr (self->text, '\n') == NULL)
    {
        lrg_paint_list_text (list, font, self->text, x, y, size, 1.0f, &fg);
        return;
    }

    /* Several lines (or centred / right aligned): place each line */
    lines = g_strsplit (self->text, "\n", -1);
    for (i = 0; lines[i] != NULL; i++)
    {
        gfloat lx = x;
        gfloat lw = 0.0f;

        if (lines[i][0] != '\0')
        {
            if (align != LRG_TEXT_ALIGN_LEFT)
            {
                lrg_paint_measure_text (font, lines[i], size, 1.0f, &lw, NULL);
                if (align == LRG_TEXT_ALIGN_CENTER)
                    lx += (w - lw) * 0.5f;
                else if (align == LRG_TEXT_ALIGN_RIGHT)
                    lx += w - lw;
            }
            lrg_paint_list_text (list, font, lines[i], lx, y, size, 1.0f, &fg);
        }
        y += size * LABEL_LINE_HEIGHT;
    }
    g_strfreev (lines);
}

/*
 * lrg_label_draw:
 *
 * Legacy immediate drawing: skip labels that cannot reach the render
 * target, otherwise render paint() on the spot (parent default).
 */
static void
lrg_label_draw (LrgWidget *widget)
{
    LrgLabel *self = LRG_LABEL (widget);

    if (self->text == NULL || self->text[0] == '\0')
        return;
    if (label_offscreen (self))
        return;

    LRG_WIDGET_CLASS (lrg_label_parent_class)->draw (widget);
}

/*
 * lrg_label_measure:
 *
 * The text's size: the widest line, and one font size plus a line
 * height per extra line. An empty label is one font size tall.
 */
static void
lrg_label_measure (LrgWidget *widget,
                   gfloat    *preferred_width,
                   gfloat    *preferred_height)
{
    LrgLabel *self = LRG_LABEL (widget);
    gfloat    size = label_effective_size (self);
    gfloat    width = 0.0f;
    gfloat    height = size;

    if (self->text != NULL && self->text[0] != '\0')
        label_text_size (self, label_font (self), size, &width, &height);

    if (preferred_width != NULL)
        *preferred_width = width;
    if (preferred_height != NULL)
        *preferred_height = height;
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
    g_clear_object (&self->measured_font);

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
        break;
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
        if (g_value_get_boxed (value) != NULL)
            lrg_label_set_color (self, g_value_get_boxed (value));
        break;
    case PROP_ALIGNMENT:
        lrg_label_set_alignment (self, g_value_get_enum (value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
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

    widget_class->paint = lrg_label_paint;
    widget_class->draw = lrg_label_draw;
    widget_class->style_defaults = lrg_label_style_defaults;
    widget_class->style_overrides = lrg_label_style_overrides;
    widget_class->measure = lrg_label_measure;

    /**
     * LrgLabel:text:
     *
     * The text to display. May contain newlines. Changing it queues a
     * layout.
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
     * The font to use for rendering. If %NULL, the theme's UI font is
     * used.
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
     * The font size in pixels. The default (20) is only reported:
     * until a size is set the label uses the theme's FONT_SIZE, or the
     * size its "title" / "heading" / "caption" class selects.
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
     * The text colour. Until it is set the label uses the theme's TEXT
     * (or TEXT_MUTED for the "muted" class); once set it wins.
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
     * The horizontal alignment of each line within the label's width.
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
 * Public API
 * ========================================================================== */

LrgLabel *
lrg_label_new (const gchar *text)
{
    return g_object_new (LRG_TYPE_LABEL,
                         "text", text,
                         NULL);
}

const gchar *
lrg_label_get_text (LrgLabel *self)
{
    g_return_val_if_fail (LRG_IS_LABEL (self), NULL);
    return self->text;
}

void
lrg_label_set_text (LrgLabel    *self,
                    const gchar *text)
{
    g_return_if_fail (LRG_IS_LABEL (self));

    if (g_strcmp0 (self->text, text) != 0)
    {
        g_free (self->text);
        self->text = g_strdup (text);
        self->measured_valid = FALSE;
        lrg_widget_queue_layout (LRG_WIDGET (self));
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TEXT]);
    }
}

GrlFont *
lrg_label_get_font (LrgLabel *self)
{
    g_return_val_if_fail (LRG_IS_LABEL (self), NULL);
    return self->font;
}

void
lrg_label_set_font (LrgLabel *self,
                    GrlFont  *font)
{
    g_return_if_fail (LRG_IS_LABEL (self));

    if (g_set_object (&self->font, font))
    {
        self->measured_valid = FALSE;
        lrg_widget_queue_layout (LRG_WIDGET (self));
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_FONT]);
    }
}

gfloat
lrg_label_get_font_size (LrgLabel *self)
{
    g_return_val_if_fail (LRG_IS_LABEL (self), 20.0f);
    return self->font_size;
}

void
lrg_label_set_font_size (LrgLabel *self,
                         gfloat    size)
{
    g_return_if_fail (LRG_IS_LABEL (self));
    g_return_if_fail (size >= 1.0f);

    if (!self->font_size_set || self->font_size != size)
    {
        self->font_size_set = TRUE;
        self->font_size = size;
        self->measured_valid = FALSE;
        lrg_widget_queue_layout (LRG_WIDGET (self));
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_FONT_SIZE]);
    }
}

const GrlColor *
lrg_label_get_color (LrgLabel *self)
{
    g_return_val_if_fail (LRG_IS_LABEL (self), &DEFAULT_COLOR);
    return &self->color;
}

void
lrg_label_set_color (LrgLabel       *self,
                     const GrlColor *color)
{
    g_return_if_fail (LRG_IS_LABEL (self));
    g_return_if_fail (color != NULL);

    if (!self->color_set ||
        self->color.r != color->r ||
        self->color.g != color->g ||
        self->color.b != color->b ||
        self->color.a != color->a)
    {
        self->color_set = TRUE;
        self->color = *color;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_COLOR]);
    }
}

LrgTextAlignment
lrg_label_get_alignment (LrgLabel *self)
{
    g_return_val_if_fail (LRG_IS_LABEL (self), LRG_TEXT_ALIGN_LEFT);
    return self->alignment;
}

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
