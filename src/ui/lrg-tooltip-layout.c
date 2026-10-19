/* lrg-tooltip-layout.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Tooltip sizing, wrapping, placement and drawing.  Pure arithmetic over
 * a caller supplied text measure: no graphics context is needed.
 */

#include "lrg-tooltip-layout.h"
#include <gio/gio.h>
#include <string.h>

G_DEFINE_BOXED_TYPE (LrgTooltipStyle, lrg_tooltip_style,
                     lrg_tooltip_style_copy, lrg_tooltip_style_free)

LrgTooltipStyle *
lrg_tooltip_style_copy (const LrgTooltipStyle *self)
{
    g_return_val_if_fail (self != NULL, NULL);
    return g_memdup2 (self, sizeof (LrgTooltipStyle));
}

void
lrg_tooltip_style_free (LrgTooltipStyle *self)
{
    g_free (self);
}

void
lrg_tooltip_style_init_default (LrgTooltipStyle *style)
{
    g_return_if_fail (style != NULL);
    style->padding = 4.0f;
    style->line_spacing = 1.0f;
    style->separator_height = 5.0f;
    style->spacer_height = 4.0f;
    style->column_gap = 12.0f;
    style->icon_size = 12.0f;
    style->min_width = 60.0f;
    style->max_width = 220.0f;
    style->border = 1.0f;
    style->background = 0x0A0A14F0u;
    style->border_color = 0x7F7F9FFFu;
    style->separator_color = 0x5F5F6FFFu;
}

/* ------------------------------------------------------------------ */
/* The layout                                                           */

struct _LrgTooltipLayout
{
    gint            ref_count;
    LrgTooltipStyle style;
    gfloat          width;
    gfloat          height;
    GArray         *rows;       /* LrgTooltipRow */
    GPtrArray      *strings;    /* gchar *, owned: the rows' texts */
};

G_DEFINE_BOXED_TYPE (LrgTooltipLayout, lrg_tooltip_layout,
                     lrg_tooltip_layout_ref, lrg_tooltip_layout_unref)

LrgTooltipLayout *
lrg_tooltip_layout_ref (LrgTooltipLayout *self)
{
    g_return_val_if_fail (self != NULL, NULL);
    g_atomic_int_inc (&self->ref_count);
    return self;
}

void
lrg_tooltip_layout_unref (LrgTooltipLayout *self)
{
    if (self == NULL)
        return;
    if (g_atomic_int_dec_and_test (&self->ref_count))
    {
        g_array_unref (self->rows);
        g_ptr_array_unref (self->strings);
        g_free (self);
    }
}

/* the measuring context */
typedef struct
{
    LrgTooltipMeasureFunc measure;
    gpointer              user_data;
} Measure;

static gfloat
text_width (const Measure *m, const gchar *text)
{
    gfloat w = 0.0f;
    gfloat h = 0.0f;

    if (text == NULL || text[0] == '\0')
        return 0.0f;
    m->measure (text, &w, &h, m->user_data);
    return w > 0.0f ? w : 0.0f;
}

/*
 * wrap_text:
 * Greedy word wrap of @text into pieces no wider than @avail: '\n' breaks
 * a paragraph, words split on spaces, a word wider than @avail is cut
 * between characters.  Every piece is appended to @out (owned strings).
 * Always yields at least one piece.
 */
static void
wrap_text (const Measure *m, const gchar *text, gfloat avail, GPtrArray *out)
{
    gchar **paragraphs;
    guint p;

    paragraphs = g_strsplit (text != NULL ? text : "", "\n", -1);
    for (p = 0; paragraphs[p] != NULL; p++)
    {
        gchar **words = g_strsplit (paragraphs[p], " ", -1);
        GString *current = g_string_new (NULL);
        guint w;

        for (w = 0; words[w] != NULL; w++)
        {
            const gchar *word = words[w];
            const gchar *c;

            if (word[0] == '\0')
                continue;
            if (current->len > 0)
            {
                g_autofree gchar *candidate = g_strconcat (current->str, " ", word, NULL);

                if (text_width (m, candidate) <= avail)
                {
                    g_string_assign (current, candidate);
                    continue;
                }
                /* the word does not fit after what is there: start a new piece */
                g_ptr_array_add (out, g_string_free (current, FALSE));
                current = g_string_new (NULL);
            }
            if (text_width (m, word) <= avail)
            {
                g_string_assign (current, word);
                continue;
            }
            /* a word wider than the box: cut it between characters */
            for (c = word; *c != '\0';)
            {
                const gchar *next = g_utf8_next_char (c);
                gsize before = current->len;

                g_string_append_len (current, c, (gssize) (next - c));
                if (before > 0 && text_width (m, current->str) > avail)
                {
                    /* the piece is full: it ends before this character */
                    g_string_truncate (current, before);
                    g_ptr_array_add (out, g_string_free (current, FALSE));
                    current = g_string_new (NULL);
                    g_string_append_len (current, c, (gssize) (next - c));
                }
                c = next;
            }
        }
        g_ptr_array_add (out, g_string_free (current, FALSE));
        g_strfreev (words);
    }
    g_strfreev (paragraphs);
}

/* appends a row; strings are copied into the layout */
static void
add_row (LrgTooltipLayout *self,
         LrgTooltipLineKind kind,
         guint              line_index,
         gfloat             y,
         gfloat             height,
         const gchar       *left,
         const gchar       *right,
         guint32            left_color,
         guint32            right_color,
         gfloat             left_x,
         gfloat             right_x,
         const gchar       *icon)
{
    LrgTooltipRow row;

    memset (&row, 0, sizeof row);
    row.kind = kind;
    row.line_index = line_index;
    row.y = y;
    row.height = height;
    row.left_color = left_color;
    row.right_color = right_color;
    row.left_x = left_x;
    row.right_x = right_x;
    if (left != NULL)
    {
        gchar *copy = g_strdup (left);

        g_ptr_array_add (self->strings, copy);
        row.left = copy;
    }
    if (right != NULL)
    {
        gchar *copy = g_strdup (right);

        g_ptr_array_add (self->strings, copy);
        row.right = copy;
    }
    if (icon != NULL)
    {
        gchar *copy = g_strdup (icon);

        g_ptr_array_add (self->strings, copy);
        row.icon = copy;
    }
    g_array_append_val (self->rows, row);
}

static gboolean
style_valid (const LrgTooltipStyle *s)
{
    return s->padding >= 0.0f && s->line_spacing >= 0.0f && s->separator_height >= 0.0f &&
           s->spacer_height >= 0.0f && s->column_gap >= 0.0f && s->icon_size >= 0.0f &&
           s->min_width >= 0.0f && s->border >= 0.0f &&
           s->max_width >= s->min_width &&
           s->max_width > 2.0f * (s->padding + s->border) + 1.0f;
}

LrgTooltipLayout *
lrg_tooltip_layout_new (LrgTooltip            *tooltip,
                        const LrgTooltipStyle *style,
                        LrgTooltipMeasureFunc  measure,
                        gpointer               user_data,
                        GError               **error)
{
    LrgTooltipLayout *self;
    LrgTooltipStyle def;
    Measure m;
    gfloat inset;
    gfloat inner_max;
    gfloat inner_natural;
    gfloat inner;
    gfloat line_h;
    gfloat y;
    guint n;
    guint i;
    gboolean first_row;
    gfloat dummy_w;

    g_return_val_if_fail (error == NULL || *error == NULL, NULL);
    if (!LRG_IS_TOOLTIP (tooltip) || measure == NULL)
    {
        g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                             "a tooltip layout needs a tooltip and a measure function");
        return NULL;
    }
    n = lrg_tooltip_get_n_lines (tooltip);
    if (n == 0)
    {
        g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                             "a tooltip layout needs at least one line");
        return NULL;
    }
    if (style == NULL)
    {
        lrg_tooltip_style_init_default (&def);
        style = &def;
    }
    if (!style_valid (style))
    {
        g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                             "the tooltip style has a negative size or a maximum width too small for its padding");
        return NULL;
    }

    self = g_new0 (LrgTooltipLayout, 1);
    self->ref_count = 1;
    self->style = *style;
    self->rows = g_array_new (FALSE, TRUE, sizeof (LrgTooltipRow));
    self->strings = g_ptr_array_new_with_free_func (g_free);
    m.measure = measure;
    m.user_data = user_data;

    inset = style->padding + style->border;
    inner_max = style->max_width - 2.0f * inset;

    /* the text height of this font */
    dummy_w = 0.0f;
    line_h = 0.0f;
    measure ("Ag", &dummy_w, &line_h, user_data);
    if (line_h <= 0.0f)
        line_h = 8.0f;

    /* pass one: the width the non-wrapping lines ask for */
    inner_natural = 0.0f;
    for (i = 0; i < n; i++)
    {
        LrgTooltipLine *line = lrg_tooltip_get_line (tooltip, i);
        gfloat w = 0.0f;

        switch (lrg_tooltip_line_get_kind (line))
        {
        case LRG_TOOLTIP_LINE_TEXT:
            if (!lrg_tooltip_line_get_wrap (line))
                w = text_width (&m, lrg_tooltip_line_get_left (line));
            break;
        case LRG_TOOLTIP_LINE_ICON:
            w = style->icon_size + 2.0f + text_width (&m, lrg_tooltip_line_get_left (line));
            break;
        case LRG_TOOLTIP_LINE_DOUBLE:
            w = text_width (&m, lrg_tooltip_line_get_left (line)) + style->column_gap +
                text_width (&m, lrg_tooltip_line_get_right (line));
            break;
        default:
            break;
        }
        inner_natural = MAX (inner_natural, MIN (w, inner_max));
    }
    inner = MAX (inner_natural, MAX (style->min_width - 2.0f * inset, 0.0f));
    inner = MIN (inner, inner_max);
    self->width = inner + 2.0f * inset;

    /* pass two: rows, wrapping where a text is wider than the box */
    y = inset;
    first_row = TRUE;
    for (i = 0; i < n; i++)
    {
        LrgTooltipLine *line = lrg_tooltip_get_line (tooltip, i);
        LrgTooltipLineKind kind = lrg_tooltip_line_get_kind (line);
        const gchar *left = lrg_tooltip_line_get_left (line);
        const gchar *right = lrg_tooltip_line_get_right (line);
        guint32 lc = lrg_tooltip_line_get_left_color (line);
        guint32 rc = lrg_tooltip_line_get_right_color (line);
        gfloat height;

        if (!first_row)
            y += style->line_spacing;
        first_row = FALSE;

        if (kind == LRG_TOOLTIP_LINE_SEPARATOR || kind == LRG_TOOLTIP_LINE_SPACER)
        {
            height = kind == LRG_TOOLTIP_LINE_SEPARATOR ? style->separator_height : style->spacer_height;
            add_row (self, kind, i, y, height, NULL, NULL, 0, 0, inset, inset, NULL);
            y += height;
        }
        else if (kind == LRG_TOOLTIP_LINE_DOUBLE)
        {
            gfloat rw = text_width (&m, right);
            gfloat avail = inner - rw - style->column_gap;
            g_autoptr(GPtrArray) pieces = g_ptr_array_new_with_free_func (g_free);
            guint k;

            /* only a wrap-flagged left text wraps; it leaves room for the right text */
            if (lrg_tooltip_line_get_wrap (line) && avail > 0.0f && text_width (&m, left) > avail)
                wrap_text (&m, left, avail, pieces);
            else
                g_ptr_array_add (pieces, g_strdup (left != NULL ? left : ""));
            for (k = 0; k < pieces->len; k++)
            {
                if (k > 0)
                    y += style->line_spacing;
                add_row (self, kind, i, y, line_h, g_ptr_array_index (pieces, k),
                         k == 0 ? right : NULL, lc, rc, inset,
                         self->width - inset - rw, NULL);
                y += line_h;
            }
        }
        else
        {
            /* text and icon lines */
            gfloat indent = kind == LRG_TOOLTIP_LINE_ICON ? style->icon_size + 2.0f : 0.0f;
            gfloat avail = inner - indent;
            g_autoptr(GPtrArray) pieces = g_ptr_array_new_with_free_func (g_free);
            guint k;

            if (text_width (&m, left) > avail || (left != NULL && strchr (left, '\n') != NULL))
                wrap_text (&m, left, avail, pieces);
            else
                g_ptr_array_add (pieces, g_strdup (left != NULL ? left : ""));
            for (k = 0; k < pieces->len; k++)
            {
                gfloat row_h = line_h;

                /* an icon row is at least as tall as its icon */
                if (kind == LRG_TOOLTIP_LINE_ICON && k == 0)
                    row_h = MAX (line_h, style->icon_size);
                if (k > 0)
                    y += style->line_spacing;
                add_row (self, kind, i, y, row_h, g_ptr_array_index (pieces, k), NULL, lc, rc,
                         inset + indent, inset,
                         k == 0 ? lrg_tooltip_line_get_icon (line) : NULL);
                y += row_h;
            }
        }
    }
    self->height = y + inset;
    return self;
}

gfloat
lrg_tooltip_layout_get_width (const LrgTooltipLayout *self)
{
    g_return_val_if_fail (self != NULL, 0.0f);
    return self->width;
}

gfloat
lrg_tooltip_layout_get_height (const LrgTooltipLayout *self)
{
    g_return_val_if_fail (self != NULL, 0.0f);
    return self->height;
}

guint
lrg_tooltip_layout_get_n_rows (const LrgTooltipLayout *self)
{
    g_return_val_if_fail (self != NULL, 0);
    return self->rows->len;
}

gboolean
lrg_tooltip_layout_get_row (const LrgTooltipLayout *self, guint index, LrgTooltipRow *out_row)
{
    g_return_val_if_fail (self != NULL, FALSE);
    g_return_val_if_fail (out_row != NULL, FALSE);
    if (index >= self->rows->len)
        return FALSE;
    *out_row = g_array_index (self->rows, LrgTooltipRow, index);
    return TRUE;
}

void
lrg_tooltip_layout_draw (const LrgTooltipLayout    *self,
                         gfloat                     x,
                         gfloat                     y,
                         const LrgTooltipDrawFuncs *funcs,
                         gpointer                   user_data)
{
    guint i;

    g_return_if_fail (self != NULL);
    g_return_if_fail (funcs != NULL);
    /* frame: the border colour under a background inset by the border */
    if (funcs->fill_rect != NULL)
    {
        if (self->style.border > 0.0f)
            funcs->fill_rect (x, y, self->width, self->height, self->style.border_color, user_data);
        funcs->fill_rect (x + self->style.border, y + self->style.border,
                          self->width - 2.0f * self->style.border,
                          self->height - 2.0f * self->style.border,
                          self->style.background, user_data);
    }
    for (i = 0; i < self->rows->len; i++)
    {
        const LrgTooltipRow *row = &g_array_index (self->rows, LrgTooltipRow, i);

        switch (row->kind)
        {
        case LRG_TOOLTIP_LINE_SEPARATOR:
            if (funcs->fill_rect != NULL)
                funcs->fill_rect (x + self->style.padding + self->style.border,
                                  y + row->y + row->height / 2.0f,
                                  self->width - 2.0f * (self->style.padding + self->style.border),
                                  1.0f, self->style.separator_color, user_data);
            break;
        case LRG_TOOLTIP_LINE_SPACER:
            break;
        default:
            if (row->icon != NULL && row->icon[0] != '\0' && funcs->draw_icon != NULL)
                funcs->draw_icon (row->icon, x + row->left_x - self->style.icon_size - 2.0f,
                                  y + row->y, self->style.icon_size, user_data);
            if (row->left != NULL && row->left[0] != '\0' && funcs->draw_text != NULL)
                funcs->draw_text (row->left, x + row->left_x, y + row->y, row->left_color, user_data);
            if (row->right != NULL && row->right[0] != '\0' && funcs->draw_text != NULL)
                funcs->draw_text (row->right, x + row->right_x, y + row->y, row->right_color, user_data);
            break;
        }
    }
}

/* ------------------------------------------------------------------ */
/* Placement                                                            */

/* keeps [pos, pos + size] inside [0, limit]; the start wins when too big */
static gfloat
clamp_span (gfloat pos, gfloat size, gfloat limit)
{
    if (pos + size > limit)
        pos = limit - size;
    if (pos < 0.0f)
        pos = 0.0f;
    return pos;
}

void
lrg_tooltip_place (gfloat  width,
                   gfloat  height,
                   gfloat  anchor_x,
                   gfloat  anchor_y,
                   gfloat  offset,
                   gfloat  screen_width,
                   gfloat  screen_height,
                   gfloat *out_x,
                   gfloat *out_y)
{
    gfloat x;
    gfloat y;

    x = anchor_x + offset;
    y = anchor_y + offset;
    /* flip to the other side of the anchor when it does not fit */
    if (x + width > screen_width && anchor_x - offset - width >= 0.0f)
        x = anchor_x - offset - width;
    if (y + height > screen_height && anchor_y - offset - height >= 0.0f)
        y = anchor_y - offset - height;
    x = clamp_span (x, width, screen_width);
    y = clamp_span (y, height, screen_height);
    if (out_x != NULL)
        *out_x = x;
    if (out_y != NULL)
        *out_y = y;
}

gboolean
lrg_tooltip_place_pair (gfloat  width,
                        gfloat  height,
                        gfloat  other_width,
                        gfloat  other_height,
                        gfloat  anchor_x,
                        gfloat  anchor_y,
                        gfloat  offset,
                        gfloat  screen_width,
                        gfloat  screen_height,
                        gfloat *out_x,
                        gfloat *out_y,
                        gfloat *out_other_x,
                        gfloat *out_other_y)
{
    gfloat total;
    gfloat x;
    gfloat y;
    gfloat ox;
    gfloat oy;
    gboolean side;

    total = width + offset + other_width;
    side = total <= screen_width;
    y = anchor_y + offset;
    if (y + MAX (height, other_height) > screen_height && anchor_y - offset - MAX (height, other_height) >= 0.0f)
        y = anchor_y - offset - MAX (height, other_height);
    if (side)
    {
        x = anchor_x + offset;
        /* not enough room on the right: the pair goes left of the anchor */
        if (x + total > screen_width && anchor_x - offset - total >= 0.0f)
            x = anchor_x - offset - total;
        x = clamp_span (x, total, screen_width);
        y = clamp_span (y, MAX (height, other_height), screen_height);
        ox = x + width + offset;
        oy = y;
    }
    else
    {
        /* wider than the screen: stack the comparison under the main one */
        gfloat both = height + offset + other_height;

        x = clamp_span (anchor_x + offset, MAX (width, other_width), screen_width);
        y = clamp_span (y, both, screen_height);
        ox = x;
        oy = y + height + offset;
    }
    if (out_x != NULL)
        *out_x = x;
    if (out_y != NULL)
        *out_y = y;
    if (out_other_x != NULL)
        *out_other_x = ox;
    if (out_other_y != NULL)
        *out_other_y = oy;
    return side;
}
