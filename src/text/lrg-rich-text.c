/* lrg-rich-text.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "config.h"
#include "lrg-rich-text.h"
#include "lrg-font-manager.h"
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdio.h>

/**
 * SECTION:lrg-rich-text
 * @Title: LrgRichText
 * @Short_description: Rich text with BBCode markup
 *
 * #LrgRichText parses BBCode-style markup and renders styled text
 * with support for colors, sizes, and animated effects.
 *
 * ## Supported BBCode Tags
 *
 * - `[b]bold[/b]` - Bold text
 * - `[i]italic[/i]` - Italic text
 * - `[u]underline[/u]` - Underlined text
 * - `[s]strikethrough[/s]` - Strikethrough text
 * - `[color=#RRGGBB]text[/color]` - Colored text (hex)
 * - `[color=red]text[/color]` - Named colors
 * - `[size=1.5]text[/size]` - Size multiplier
 * - `[shake]text[/shake]` - Shake effect
 * - `[wave]text[/wave]` - Wave effect
 * - `[rainbow]text[/rainbow]` - Rainbow effect
 * - `[typewriter speed=50]text[/typewriter]` - Typewriter reveal
 */

typedef struct
{
    GPtrArray        *spans;     /* LrgTextSpan* */
    GPtrArray        *effects;   /* LrgTextEffect* (per-span) */
    GString          *plain_text;
    gfloat            font_size;
    gfloat            line_spacing;
    gfloat            max_width;
    LrgTextAlignment  alignment;
    guint8            default_r;
    guint8            default_g;
    guint8            default_b;
    guint8            default_a;
} LrgRichTextPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (LrgRichText, lrg_rich_text, G_TYPE_OBJECT)

enum
{
    PROP_0,
    PROP_FONT_SIZE,
    PROP_LINE_SPACING,
    PROP_MAX_WIDTH,
    PROP_ALIGNMENT,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

/*
 * Named color lookup
 */

typedef struct
{
    const gchar *name;
    guint8       r, g, b;
} NamedColor;

static const NamedColor named_colors[] = {
    { "red",     255, 0,   0   },
    { "green",   0,   255, 0   },
    { "blue",    0,   0,   255 },
    { "yellow",  255, 255, 0   },
    { "cyan",    0,   255, 255 },
    { "magenta", 255, 0,   255 },
    { "white",   255, 255, 255 },
    { "black",   0,   0,   0   },
    { "gray",    128, 128, 128 },
    { "grey",    128, 128, 128 },
    { "orange",  255, 165, 0   },
    { "purple",  128, 0,   128 },
    { "pink",    255, 192, 203 },
    { NULL,      0,   0,   0   }
};

static gboolean
lookup_named_color (const gchar *name,
                    guint8      *r,
                    guint8      *g,
                    guint8      *b)
{
    const NamedColor *nc;

    for (nc = named_colors; nc->name != NULL; nc++)
    {
        if (g_ascii_strcasecmp (name, nc->name) == 0)
        {
            *r = nc->r;
            *g = nc->g;
            *b = nc->b;
            return TRUE;
        }
    }
    return FALSE;
}

/*
 * BBCode parser
 */

typedef struct
{
    LrgTextStyle      style;
    gfloat            font_size;
    guint8            r, g, b, a;
    LrgTextEffectType effect;
    gfloat            effect_speed;
} ParseState;

/*
 * Custom free function for effects array that handles NULL entries
 */
static void
effect_free_func (gpointer data)
{
    if (data != NULL)
        g_object_unref (data);
}

static gboolean
parse_hex_color (const gchar *hex, guint8 *r, guint8 *g, guint8 *b)
{
    const gchar *p = hex;
    guint rv, gv, bv;

    if (*p == '#')
        p++;

    if (strlen (p) == 6)
    {
        if (sscanf (p, "%2x%2x%2x", &rv, &gv, &bv) == 3)
        {
            *r = (guint8) rv;
            *g = (guint8) gv;
            *b = (guint8) bv;
            return TRUE;
        }
    }
    return FALSE;
}

static void
flush_span (LrgRichTextPrivate *priv,
            GString            *text_buffer,
            const ParseState   *state)
{
    LrgTextSpan *span;
    LrgTextEffect *effect;

    if (text_buffer->len == 0)
        return;

    span = lrg_text_span_new (text_buffer->str);
    lrg_text_span_set_style (span, state->style);
    lrg_text_span_set_font_size (span, state->font_size);
    lrg_text_span_set_color (span, state->r, state->g, state->b, state->a);
    lrg_text_span_set_effect_type (span, state->effect);
    lrg_text_span_set_effect_speed (span, state->effect_speed);

    g_ptr_array_add (priv->spans, span);
    g_string_append (priv->plain_text, text_buffer->str);

    /* Create effect object if needed */
    if (state->effect != LRG_TEXT_EFFECT_NONE)
    {
        effect = lrg_text_effect_new (state->effect);
        lrg_text_effect_set_speed (effect, state->effect_speed);
        lrg_text_effect_set_char_count (effect, (guint) g_utf8_strlen (text_buffer->str, -1));
    }
    else
    {
        effect = NULL;
    }
    g_ptr_array_add (priv->effects, effect);

    g_string_truncate (text_buffer, 0);
}

static void
lrg_rich_text_real_parse (LrgRichText *text,
                          const gchar *markup)
{
    LrgRichTextPrivate *priv;
    ParseState state;
    GString *buffer;
    const gchar *p;
    GQueue *state_stack;

    priv = lrg_rich_text_get_instance_private (text);

    /* Clear existing spans */
    g_ptr_array_set_size (priv->spans, 0);
    g_ptr_array_set_size (priv->effects, 0);
    g_string_truncate (priv->plain_text, 0);

    if (markup == NULL || *markup == '\0')
        return;

    /* Initialize state */
    state.style = LRG_TEXT_STYLE_NONE;
    state.font_size = 1.0f;
    state.r = priv->default_r;
    state.g = priv->default_g;
    state.b = priv->default_b;
    state.a = priv->default_a;
    state.effect = LRG_TEXT_EFFECT_NONE;
    state.effect_speed = 1.0f;

    state_stack = g_queue_new ();
    buffer = g_string_new (NULL);
    p = markup;

    while (*p != '\0')
    {
        if (*p == '[')
        {
            const gchar *tag_start = p + 1;
            const gchar *tag_end;
            gchar *tag;
            gboolean closing;

            tag_end = strchr (tag_start, ']');
            if (tag_end == NULL)
            {
                /* Not a valid tag, treat as text */
                g_string_append_c (buffer, *p);
                p++;
                continue;
            }

            tag = g_strndup (tag_start, (gsize) (tag_end - tag_start));
            closing = (*tag == '/');

            if (closing)
            {
                /* Closing tag */
                const gchar *tag_name = tag + 1;

                flush_span (priv, buffer, &state);

                /* Restore previous state if we have one */
                if (!g_queue_is_empty (state_stack))
                {
                    ParseState *old;

                    old = g_queue_pop_head (state_stack);
                    state = *old;
                    g_slice_free (ParseState, old);
                }

                (void) tag_name;
            }
            else
            {
                /* Opening tag */
                gchar *eq_pos;
                gchar *tag_name;
                gchar *tag_value = NULL;
                ParseState *saved;

                eq_pos = strchr (tag, '=');
                if (eq_pos != NULL)
                {
                    tag_name = g_strndup (tag, (gsize) (eq_pos - tag));
                    tag_value = g_strdup (eq_pos + 1);
                }
                else
                {
                    tag_name = g_strdup (tag);
                }

                /* Flush current text */
                flush_span (priv, buffer, &state);

                /* Save state */
                saved = g_slice_new (ParseState);
                *saved = state;
                g_queue_push_head (state_stack, saved);

                /* Apply tag */
                if (g_str_equal (tag_name, "b"))
                {
                    state.style |= LRG_TEXT_STYLE_BOLD;
                }
                else if (g_str_equal (tag_name, "i"))
                {
                    state.style |= LRG_TEXT_STYLE_ITALIC;
                }
                else if (g_str_equal (tag_name, "u"))
                {
                    state.style |= LRG_TEXT_STYLE_UNDERLINE;
                }
                else if (g_str_equal (tag_name, "s"))
                {
                    state.style |= LRG_TEXT_STYLE_STRIKETHROUGH;
                }
                else if (g_str_equal (tag_name, "color") && tag_value != NULL)
                {
                    if (tag_value[0] == '#' || isxdigit ((unsigned char) tag_value[0]))
                        parse_hex_color (tag_value, &state.r, &state.g, &state.b);
                    else
                        lookup_named_color (tag_value, &state.r, &state.g, &state.b);
                }
                else if (g_str_equal (tag_name, "size") && tag_value != NULL)
                {
                    state.font_size = (gfloat) g_ascii_strtod (tag_value, NULL);
                    if (state.font_size <= 0.0f)
                        state.font_size = 1.0f;
                }
                else if (g_str_equal (tag_name, "shake"))
                {
                    state.effect = LRG_TEXT_EFFECT_SHAKE;
                }
                else if (g_str_equal (tag_name, "wave"))
                {
                    state.effect = LRG_TEXT_EFFECT_WAVE;
                }
                else if (g_str_equal (tag_name, "rainbow"))
                {
                    state.effect = LRG_TEXT_EFFECT_RAINBOW;
                }
                else if (g_str_equal (tag_name, "typewriter"))
                {
                    state.effect = LRG_TEXT_EFFECT_TYPEWRITER;
                    if (tag_value != NULL)
                    {
                        /* Parse speed=XX */
                        if (g_str_has_prefix (tag_value, "speed"))
                        {
                            const gchar *sp;

                            sp = strchr (tag_value, '=');
                            if (sp != NULL)
                                state.effect_speed = (gfloat) g_ascii_strtod (sp + 1, NULL) / 50.0f;
                        }
                        else
                        {
                            state.effect_speed = (gfloat) g_ascii_strtod (tag_value, NULL) / 50.0f;
                        }
                    }
                }
                else if (g_str_equal (tag_name, "pulse"))
                {
                    state.effect = LRG_TEXT_EFFECT_PULSE;
                }
                else if (g_str_equal (tag_name, "fade"))
                {
                    state.effect = LRG_TEXT_EFFECT_FADE_IN;
                }

                g_free (tag_name);
                g_free (tag_value);
            }

            g_free (tag);
            p = tag_end + 1;
        }
        else
        {
            g_string_append_c (buffer, *p);
            p++;
        }
    }

    /* Flush remaining text */
    flush_span (priv, buffer, &state);

    /* Clean up */
    while (!g_queue_is_empty (state_stack))
    {
        ParseState *old;

        old = g_queue_pop_head (state_stack);
        g_slice_free (ParseState, old);
    }
    g_queue_free (state_stack);
    g_string_free (buffer, TRUE);
}

static void
lrg_rich_text_real_update (LrgRichText *text,
                           gfloat       delta_time)
{
    LrgRichTextPrivate *priv;
    guint i;

    priv = lrg_rich_text_get_instance_private (text);

    for (i = 0; i < priv->effects->len; i++)
    {
        LrgTextEffect *effect;

        effect = g_ptr_array_index (priv->effects, i);
        if (effect != NULL)
            lrg_text_effect_update (effect, delta_time);
    }
}

static void
lrg_rich_text_real_draw (LrgRichText *text,
                         gfloat       x,
                         gfloat       y)
{
    LrgRichTextPrivate *priv;
    LrgFontManager *font_mgr;
    gfloat cursor_x, cursor_y;
    guint i;
    guint global_char_index;

    priv = lrg_rich_text_get_instance_private (text);
    font_mgr = lrg_font_manager_get_default ();

    cursor_x = x;
    cursor_y = y;
    global_char_index = 0;

    for (i = 0; i < priv->spans->len; i++)
    {
        const LrgTextSpan *span;
        LrgTextEffect *effect;
        const gchar *span_text;
        gfloat span_font_size;
        guint8 r, g, b, a;
        const gchar *ch;
        guint char_idx;

        span = g_ptr_array_index (priv->spans, i);
        effect = g_ptr_array_index (priv->effects, i);
        span_text = lrg_text_span_get_text (span);
        span_font_size = lrg_text_span_get_font_size (span) * priv->font_size;
        lrg_text_span_get_color (span, &r, &g, &b, &a);

        char_idx = 0;
        for (ch = span_text; *ch != '\0'; ch = g_utf8_next_char (ch))
        {
            gunichar uc;
            gchar char_buf[7];
            gint char_len;
            gfloat offset_x, offset_y;
            guint8 chr, chg, chb, cha;
            gfloat char_width;

            uc = g_utf8_get_char (ch);

            /* Handle newlines */
            if (uc == '\n')
            {
                cursor_x = x;
                cursor_y += span_font_size * priv->line_spacing;
                global_char_index++;
                char_idx++;
                continue;
            }

            char_len = g_unichar_to_utf8 (uc, char_buf);
            char_buf[char_len] = '\0';

            /* Apply effects */
            offset_x = 0.0f;
            offset_y = 0.0f;
            chr = r;
            chg = g;
            chb = b;
            cha = a;

            if (effect != NULL)
            {
                lrg_text_effect_apply (effect, global_char_index,
                                       &offset_x, &offset_y,
                                       &chr, &chg, &chb, &cha);
            }

            /* Draw character */
            if (cha > 0)
            {
                lrg_font_manager_draw_text (font_mgr, NULL, char_buf,
                                            cursor_x + offset_x,
                                            cursor_y + offset_y,
                                            span_font_size,
                                            chr, chg, chb, cha);
            }

            /* Advance cursor */
            lrg_font_manager_measure_text (font_mgr, NULL, char_buf, span_font_size,
                                           &char_width, NULL);
            cursor_x += char_width;

            /* Handle word wrap */
            if (priv->max_width > 0.0f && cursor_x - x > priv->max_width)
            {
                cursor_x = x;
                cursor_y += span_font_size * priv->line_spacing;
            }

            global_char_index++;
            char_idx++;
        }
    }
}

static void
lrg_rich_text_finalize (GObject *object)
{
    LrgRichText *self = LRG_RICH_TEXT (object);
    LrgRichTextPrivate *priv;

    priv = lrg_rich_text_get_instance_private (self);

    g_ptr_array_unref (priv->spans);
    g_ptr_array_unref (priv->effects);
    g_string_free (priv->plain_text, TRUE);

    G_OBJECT_CLASS (lrg_rich_text_parent_class)->finalize (object);
}

static void
lrg_rich_text_get_property (GObject    *object,
                            guint       prop_id,
                            GValue     *value,
                            GParamSpec *pspec)
{
    LrgRichText *self = LRG_RICH_TEXT (object);
    LrgRichTextPrivate *priv;

    priv = lrg_rich_text_get_instance_private (self);

    switch (prop_id)
    {
    case PROP_FONT_SIZE:
        g_value_set_float (value, priv->font_size);
        break;
    case PROP_LINE_SPACING:
        g_value_set_float (value, priv->line_spacing);
        break;
    case PROP_MAX_WIDTH:
        g_value_set_float (value, priv->max_width);
        break;
    case PROP_ALIGNMENT:
        g_value_set_enum (value, priv->alignment);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_rich_text_set_property (GObject      *object,
                            guint         prop_id,
                            const GValue *value,
                            GParamSpec   *pspec)
{
    LrgRichText *self = LRG_RICH_TEXT (object);
    LrgRichTextPrivate *priv;

    priv = lrg_rich_text_get_instance_private (self);

    switch (prop_id)
    {
    case PROP_FONT_SIZE:
        priv->font_size = g_value_get_float (value);
        break;
    case PROP_LINE_SPACING:
        priv->line_spacing = g_value_get_float (value);
        break;
    case PROP_MAX_WIDTH:
        priv->max_width = g_value_get_float (value);
        break;
    case PROP_ALIGNMENT:
        priv->alignment = g_value_get_enum (value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_rich_text_class_init (LrgRichTextClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = lrg_rich_text_finalize;
    object_class->get_property = lrg_rich_text_get_property;
    object_class->set_property = lrg_rich_text_set_property;

    klass->parse = lrg_rich_text_real_parse;
    klass->update = lrg_rich_text_real_update;
    klass->draw = lrg_rich_text_real_draw;

    properties[PROP_FONT_SIZE] =
        g_param_spec_float ("font-size", "Font Size", "Base font size",
                            1.0f, 1000.0f, 16.0f,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_LINE_SPACING] =
        g_param_spec_float ("line-spacing", "Line Spacing", "Line spacing multiplier",
                            0.5f, 5.0f, 1.2f,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_MAX_WIDTH] =
        g_param_spec_float ("max-width", "Max Width", "Max width for wrapping (0 = no wrap)",
                            0.0f, G_MAXFLOAT, 0.0f,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_ALIGNMENT] =
        g_param_spec_enum ("alignment", "Alignment", "Text alignment",
                           LRG_TYPE_TEXT_ALIGNMENT, LRG_TEXT_ALIGN_LEFT,
                           G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_rich_text_init (LrgRichText *self)
{
    LrgRichTextPrivate *priv;

    priv = lrg_rich_text_get_instance_private (self);
    priv->spans = g_ptr_array_new_with_free_func ((GDestroyNotify) lrg_text_span_free);
    priv->effects = g_ptr_array_new_with_free_func (effect_free_func);
    priv->plain_text = g_string_new (NULL);
    priv->font_size = 16.0f;
    priv->line_spacing = 1.2f;
    priv->max_width = 0.0f;
    priv->alignment = LRG_TEXT_ALIGN_LEFT;
    priv->default_r = 255;
    priv->default_g = 255;
    priv->default_b = 255;
    priv->default_a = 255;
}

/*
 * Public API
 */

LrgRichText *
lrg_rich_text_new (void)
{
    return g_object_new (LRG_TYPE_RICH_TEXT, NULL);
}

LrgRichText *
lrg_rich_text_new_from_markup (const gchar *markup)
{
    LrgRichText *text;

    text = lrg_rich_text_new ();
    lrg_rich_text_set_markup (text, markup);

    return text;
}

void
lrg_rich_text_set_markup (LrgRichText *text,
                          const gchar *markup)
{
    LrgRichTextClass *klass;

    g_return_if_fail (LRG_IS_RICH_TEXT (text));

    klass = LRG_RICH_TEXT_GET_CLASS (text);
    if (klass->parse != NULL)
        klass->parse (text, markup);
}

const gchar *
lrg_rich_text_get_plain_text (LrgRichText *text)
{
    LrgRichTextPrivate *priv;

    g_return_val_if_fail (LRG_IS_RICH_TEXT (text), NULL);

    priv = lrg_rich_text_get_instance_private (text);
    return priv->plain_text->str;
}

guint
lrg_rich_text_get_span_count (LrgRichText *text)
{
    LrgRichTextPrivate *priv;

    g_return_val_if_fail (LRG_IS_RICH_TEXT (text), 0);

    priv = lrg_rich_text_get_instance_private (text);
    return priv->spans->len;
}

const LrgTextSpan *
lrg_rich_text_get_span (LrgRichText *text,
                        guint        index)
{
    LrgRichTextPrivate *priv;

    g_return_val_if_fail (LRG_IS_RICH_TEXT (text), NULL);

    priv = lrg_rich_text_get_instance_private (text);

    if (index >= priv->spans->len)
        return NULL;

    return g_ptr_array_index (priv->spans, index);
}

gfloat
lrg_rich_text_get_font_size (LrgRichText *text)
{
    LrgRichTextPrivate *priv;

    g_return_val_if_fail (LRG_IS_RICH_TEXT (text), 16.0f);

    priv = lrg_rich_text_get_instance_private (text);
    return priv->font_size;
}

void
lrg_rich_text_set_font_size (LrgRichText *text,
                             gfloat       size)
{
    LrgRichTextPrivate *priv;

    g_return_if_fail (LRG_IS_RICH_TEXT (text));

    priv = lrg_rich_text_get_instance_private (text);
    priv->font_size = size > 0.0f ? size : 16.0f;
}

gfloat
lrg_rich_text_get_line_spacing (LrgRichText *text)
{
    LrgRichTextPrivate *priv;

    g_return_val_if_fail (LRG_IS_RICH_TEXT (text), 1.2f);

    priv = lrg_rich_text_get_instance_private (text);
    return priv->line_spacing;
}

void
lrg_rich_text_set_line_spacing (LrgRichText *text,
                                gfloat       spacing)
{
    LrgRichTextPrivate *priv;

    g_return_if_fail (LRG_IS_RICH_TEXT (text));

    priv = lrg_rich_text_get_instance_private (text);
    priv->line_spacing = spacing;
}

gfloat
lrg_rich_text_get_max_width (LrgRichText *text)
{
    LrgRichTextPrivate *priv;

    g_return_val_if_fail (LRG_IS_RICH_TEXT (text), 0.0f);

    priv = lrg_rich_text_get_instance_private (text);
    return priv->max_width;
}

void
lrg_rich_text_set_max_width (LrgRichText *text,
                             gfloat       width)
{
    LrgRichTextPrivate *priv;

    g_return_if_fail (LRG_IS_RICH_TEXT (text));

    priv = lrg_rich_text_get_instance_private (text);
    priv->max_width = width >= 0.0f ? width : 0.0f;
}

LrgTextAlignment
lrg_rich_text_get_alignment (LrgRichText *text)
{
    LrgRichTextPrivate *priv;

    g_return_val_if_fail (LRG_IS_RICH_TEXT (text), LRG_TEXT_ALIGN_LEFT);

    priv = lrg_rich_text_get_instance_private (text);
    return priv->alignment;
}

void
lrg_rich_text_set_alignment (LrgRichText      *text,
                             LrgTextAlignment  alignment)
{
    LrgRichTextPrivate *priv;

    g_return_if_fail (LRG_IS_RICH_TEXT (text));

    priv = lrg_rich_text_get_instance_private (text);
    priv->alignment = alignment;
}

void
lrg_rich_text_get_default_color (LrgRichText *text,
                                 guint8      *r,
                                 guint8      *g,
                                 guint8      *b,
                                 guint8      *a)
{
    LrgRichTextPrivate *priv;

    g_return_if_fail (LRG_IS_RICH_TEXT (text));

    priv = lrg_rich_text_get_instance_private (text);

    if (r != NULL) *r = priv->default_r;
    if (g != NULL) *g = priv->default_g;
    if (b != NULL) *b = priv->default_b;
    if (a != NULL) *a = priv->default_a;
}

void
lrg_rich_text_set_default_color (LrgRichText *text,
                                 guint8       r,
                                 guint8       g,
                                 guint8       b,
                                 guint8       a)
{
    LrgRichTextPrivate *priv;

    g_return_if_fail (LRG_IS_RICH_TEXT (text));

    priv = lrg_rich_text_get_instance_private (text);
    priv->default_r = r;
    priv->default_g = g;
    priv->default_b = b;
    priv->default_a = a;
}

void
lrg_rich_text_update (LrgRichText *text,
                      gfloat       delta_time)
{
    LrgRichTextClass *klass;

    g_return_if_fail (LRG_IS_RICH_TEXT (text));

    klass = LRG_RICH_TEXT_GET_CLASS (text);
    if (klass->update != NULL)
        klass->update (text, delta_time);
}

void
lrg_rich_text_draw (LrgRichText *text,
                    gfloat       x,
                    gfloat       y)
{
    LrgRichTextClass *klass;

    g_return_if_fail (LRG_IS_RICH_TEXT (text));

    klass = LRG_RICH_TEXT_GET_CLASS (text);
    if (klass->draw != NULL)
        klass->draw (text, x, y);
}

void
lrg_rich_text_measure (LrgRichText *text,
                       gfloat      *width,
                       gfloat      *height)
{
    LrgRichTextPrivate *priv;
    LrgFontManager *font_mgr;
    gfloat w, h;

    g_return_if_fail (LRG_IS_RICH_TEXT (text));

    priv = lrg_rich_text_get_instance_private (text);
    font_mgr = lrg_font_manager_get_default ();

    lrg_font_manager_measure_text (font_mgr, NULL, priv->plain_text->str,
                                   priv->font_size, &w, &h);

    if (width != NULL)
        *width = w;
    if (height != NULL)
        *height = h;
}

void
lrg_rich_text_reset_effects (LrgRichText *text)
{
    LrgRichTextPrivate *priv;
    guint i;

    g_return_if_fail (LRG_IS_RICH_TEXT (text));

    priv = lrg_rich_text_get_instance_private (text);

    for (i = 0; i < priv->effects->len; i++)
    {
        LrgTextEffect *effect;

        effect = g_ptr_array_index (priv->effects, i);
        if (effect != NULL)
            lrg_text_effect_reset (effect);
    }
}

gboolean
lrg_rich_text_effects_complete (LrgRichText *text)
{
    LrgRichTextPrivate *priv;
    guint i;

    g_return_val_if_fail (LRG_IS_RICH_TEXT (text), TRUE);

    priv = lrg_rich_text_get_instance_private (text);

    for (i = 0; i < priv->effects->len; i++)
    {
        LrgTextEffect *effect;

        effect = g_ptr_array_index (priv->effects, i);
        if (effect != NULL && !lrg_text_effect_is_complete (effect))
            return FALSE;
    }

    return TRUE;
}
