/* lrg-text-span.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "config.h"
#include "lrg-text-span.h"
#include <string.h>
#include <stdio.h>

/**
 * SECTION:lrg-text-span
 * @Title: LrgTextSpan
 * @Short_description: Styled text span for rich text
 *
 * #LrgTextSpan represents a styled run of text within a rich text block.
 * Each span contains text content along with styling attributes like
 * font size, color, style flags (bold, italic, etc.), and optional
 * animated effects.
 */

struct _LrgTextSpan
{
    gchar            *text;
    LrgTextStyle      style;
    gfloat            font_size;
    guint8            color_r;
    guint8            color_g;
    guint8            color_b;
    guint8            color_a;
    LrgTextEffectType effect_type;
    gfloat            effect_speed;
    gchar            *custom_font;
};

G_DEFINE_BOXED_TYPE (LrgTextSpan, lrg_text_span,
                     lrg_text_span_copy, lrg_text_span_free)

LrgTextSpan *
lrg_text_span_new (const gchar *text)
{
    LrgTextSpan *span;

    span = g_slice_new0 (LrgTextSpan);
    span->text = g_strdup (text != NULL ? text : "");
    span->style = LRG_TEXT_STYLE_NONE;
    span->font_size = 1.0f;
    span->color_r = 255;
    span->color_g = 255;
    span->color_b = 255;
    span->color_a = 255;
    span->effect_type = LRG_TEXT_EFFECT_NONE;
    span->effect_speed = 1.0f;
    span->custom_font = NULL;

    return span;
}

LrgTextSpan *
lrg_text_span_copy (const LrgTextSpan *span)
{
    LrgTextSpan *copy;

    g_return_val_if_fail (span != NULL, NULL);

    copy = g_slice_new0 (LrgTextSpan);
    copy->text = g_strdup (span->text);
    copy->style = span->style;
    copy->font_size = span->font_size;
    copy->color_r = span->color_r;
    copy->color_g = span->color_g;
    copy->color_b = span->color_b;
    copy->color_a = span->color_a;
    copy->effect_type = span->effect_type;
    copy->effect_speed = span->effect_speed;
    copy->custom_font = g_strdup (span->custom_font);

    return copy;
}

void
lrg_text_span_free (LrgTextSpan *span)
{
    if (span == NULL)
        return;

    g_free (span->text);
    g_free (span->custom_font);
    g_slice_free (LrgTextSpan, span);
}

const gchar *
lrg_text_span_get_text (const LrgTextSpan *span)
{
    g_return_val_if_fail (span != NULL, NULL);
    return span->text;
}

void
lrg_text_span_set_text (LrgTextSpan *span,
                        const gchar *text)
{
    g_return_if_fail (span != NULL);

    g_free (span->text);
    span->text = g_strdup (text != NULL ? text : "");
}

LrgTextStyle
lrg_text_span_get_style (const LrgTextSpan *span)
{
    g_return_val_if_fail (span != NULL, LRG_TEXT_STYLE_NONE);
    return span->style;
}

void
lrg_text_span_set_style (LrgTextSpan  *span,
                         LrgTextStyle  style)
{
    g_return_if_fail (span != NULL);
    span->style = style;
}

gfloat
lrg_text_span_get_font_size (const LrgTextSpan *span)
{
    g_return_val_if_fail (span != NULL, 1.0f);
    return span->font_size;
}

void
lrg_text_span_set_font_size (LrgTextSpan *span,
                             gfloat       size)
{
    g_return_if_fail (span != NULL);
    span->font_size = size > 0.0f ? size : 1.0f;
}

void
lrg_text_span_get_color (const LrgTextSpan *span,
                         guint8            *r,
                         guint8            *g,
                         guint8            *b,
                         guint8            *a)
{
    g_return_if_fail (span != NULL);

    if (r != NULL)
        *r = span->color_r;
    if (g != NULL)
        *g = span->color_g;
    if (b != NULL)
        *b = span->color_b;
    if (a != NULL)
        *a = span->color_a;
}

void
lrg_text_span_set_color (LrgTextSpan *span,
                         guint8       r,
                         guint8       g,
                         guint8       b,
                         guint8       a)
{
    g_return_if_fail (span != NULL);

    span->color_r = r;
    span->color_g = g;
    span->color_b = b;
    span->color_a = a;
}

gboolean
lrg_text_span_set_color_hex (LrgTextSpan *span,
                             const gchar *hex)
{
    const gchar *p;
    guint r, g, b;

    g_return_val_if_fail (span != NULL, FALSE);
    g_return_val_if_fail (hex != NULL, FALSE);

    p = hex;

    /* Skip leading # */
    if (*p == '#')
        p++;

    /* Parse 6-digit hex color */
    if (strlen (p) == 6)
    {
        if (sscanf (p, "%2x%2x%2x", &r, &g, &b) == 3)
        {
            span->color_r = (guint8) r;
            span->color_g = (guint8) g;
            span->color_b = (guint8) b;
            return TRUE;
        }
    }
    /* Parse 3-digit shorthand */
    else if (strlen (p) == 3)
    {
        if (sscanf (p, "%1x%1x%1x", &r, &g, &b) == 3)
        {
            span->color_r = (guint8) (r * 17);
            span->color_g = (guint8) (g * 17);
            span->color_b = (guint8) (b * 17);
            return TRUE;
        }
    }

    return FALSE;
}

LrgTextEffectType
lrg_text_span_get_effect_type (const LrgTextSpan *span)
{
    g_return_val_if_fail (span != NULL, LRG_TEXT_EFFECT_NONE);
    return span->effect_type;
}

void
lrg_text_span_set_effect_type (LrgTextSpan       *span,
                               LrgTextEffectType  effect)
{
    g_return_if_fail (span != NULL);
    span->effect_type = effect;
}

gfloat
lrg_text_span_get_effect_speed (const LrgTextSpan *span)
{
    g_return_val_if_fail (span != NULL, 1.0f);
    return span->effect_speed;
}

void
lrg_text_span_set_effect_speed (LrgTextSpan *span,
                                gfloat       speed)
{
    g_return_if_fail (span != NULL);
    span->effect_speed = speed > 0.0f ? speed : 1.0f;
}

const gchar *
lrg_text_span_get_custom_font (const LrgTextSpan *span)
{
    g_return_val_if_fail (span != NULL, NULL);
    return span->custom_font;
}

void
lrg_text_span_set_custom_font (LrgTextSpan *span,
                               const gchar *font_name)
{
    g_return_if_fail (span != NULL);

    g_free (span->custom_font);
    span->custom_font = g_strdup (font_name);
}
