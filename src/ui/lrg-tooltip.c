/* lrg-tooltip.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * The rich tooltip model: LrgTooltipLine (a boxed row) and LrgTooltip (an
 * ordered array of rows).  Pure data, no drawing, no graphics context.
 */

#include "lrg-tooltip.h"
#include <string.h>

/* ------------------------------------------------------------------ */
/* LrgTooltipLine                                                       */

struct _LrgTooltipLine
{
    LrgTooltipLineKind kind;
    gchar             *left;
    gchar             *right;
    gchar             *id;
    gchar             *icon;
    guint32            left_color;
    guint32            right_color;
    gboolean           wrap;
};

G_DEFINE_BOXED_TYPE (LrgTooltipLine, lrg_tooltip_line,
                     lrg_tooltip_line_copy, lrg_tooltip_line_free)

static const GEnumValue line_kind_values[] =
{
    { LRG_TOOLTIP_LINE_TEXT,      "LRG_TOOLTIP_LINE_TEXT",      "text" },
    { LRG_TOOLTIP_LINE_DOUBLE,    "LRG_TOOLTIP_LINE_DOUBLE",    "double" },
    { LRG_TOOLTIP_LINE_ICON,      "LRG_TOOLTIP_LINE_ICON",      "icon" },
    { LRG_TOOLTIP_LINE_SEPARATOR, "LRG_TOOLTIP_LINE_SEPARATOR", "separator" },
    { LRG_TOOLTIP_LINE_SPACER,    "LRG_TOOLTIP_LINE_SPACER",    "spacer" },
    { 0, NULL, NULL }
};

GType
lrg_tooltip_line_kind_get_type (void)
{
    static gsize type = 0;

    if (g_once_init_enter (&type))
        g_once_init_leave (&type, g_enum_register_static ("LrgTooltipLineKind", line_kind_values));
    return (GType) type;
}

LrgTooltipLine *
lrg_tooltip_line_new (LrgTooltipLineKind kind)
{
    LrgTooltipLine *self;

    self = g_new0 (LrgTooltipLine, 1);
    self->kind = kind;
    self->left_color = 0xFFFFFFFFu;
    self->right_color = 0xFFFFFFFFu;
    return self;
}

LrgTooltipLine *
lrg_tooltip_line_copy (const LrgTooltipLine *self)
{
    LrgTooltipLine *copy;

    g_return_val_if_fail (self != NULL, NULL);
    copy = g_new0 (LrgTooltipLine, 1);
    copy->kind = self->kind;
    copy->left = g_strdup (self->left);
    copy->right = g_strdup (self->right);
    copy->id = g_strdup (self->id);
    copy->icon = g_strdup (self->icon);
    copy->left_color = self->left_color;
    copy->right_color = self->right_color;
    copy->wrap = self->wrap;
    return copy;
}

void
lrg_tooltip_line_free (LrgTooltipLine *self)
{
    if (self == NULL)
        return;
    g_free (self->left);
    g_free (self->right);
    g_free (self->id);
    g_free (self->icon);
    g_free (self);
}

LrgTooltipLineKind
lrg_tooltip_line_get_kind (const LrgTooltipLine *self)
{
    g_return_val_if_fail (self != NULL, LRG_TOOLTIP_LINE_TEXT);
    return self->kind;
}

const gchar *
lrg_tooltip_line_get_left (const LrgTooltipLine *self)
{
    g_return_val_if_fail (self != NULL, NULL);
    return self->left;
}

void
lrg_tooltip_line_set_left (LrgTooltipLine *self, const gchar *text)
{
    g_return_if_fail (self != NULL);
    g_free (self->left);
    self->left = g_strdup (text);
}

const gchar *
lrg_tooltip_line_get_right (const LrgTooltipLine *self)
{
    g_return_val_if_fail (self != NULL, NULL);
    return self->right;
}

void
lrg_tooltip_line_set_right (LrgTooltipLine *self, const gchar *text)
{
    g_return_if_fail (self != NULL);
    g_free (self->right);
    self->right = g_strdup (text);
}

guint32
lrg_tooltip_line_get_left_color (const LrgTooltipLine *self)
{
    g_return_val_if_fail (self != NULL, 0);
    return self->left_color;
}

guint32
lrg_tooltip_line_get_right_color (const LrgTooltipLine *self)
{
    g_return_val_if_fail (self != NULL, 0);
    return self->right_color;
}

void
lrg_tooltip_line_set_colors (LrgTooltipLine *self, guint32 left, guint32 right)
{
    g_return_if_fail (self != NULL);
    self->left_color = left;
    self->right_color = right;
}

gboolean
lrg_tooltip_line_get_wrap (const LrgTooltipLine *self)
{
    g_return_val_if_fail (self != NULL, FALSE);
    return self->wrap;
}

void
lrg_tooltip_line_set_wrap (LrgTooltipLine *self, gboolean wrap)
{
    g_return_if_fail (self != NULL);
    self->wrap = wrap != FALSE;
}

const gchar *
lrg_tooltip_line_get_id (const LrgTooltipLine *self)
{
    g_return_val_if_fail (self != NULL, NULL);
    return self->id;
}

void
lrg_tooltip_line_set_id (LrgTooltipLine *self, const gchar *id)
{
    g_return_if_fail (self != NULL);
    g_free (self->id);
    self->id = g_strdup (id);
}

const gchar *
lrg_tooltip_line_get_icon (const LrgTooltipLine *self)
{
    g_return_val_if_fail (self != NULL, NULL);
    return self->icon;
}

void
lrg_tooltip_line_set_icon (LrgTooltipLine *self, const gchar *icon)
{
    g_return_if_fail (self != NULL);
    g_free (self->icon);
    self->icon = g_strdup (icon);
}

/* one hex digit, or -1 */
static gint
hex_digit (gchar c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

gboolean
lrg_tooltip_color_parse (const gchar *spec, guint32 *out_color)
{
    gsize len;
    guint32 value;
    gsize i;

    if (spec == NULL || out_color == NULL)
        return FALSE;
    if (spec[0] == '#')
        spec++;
    len = strlen (spec);
    if (len != 6 && len != 8)
        return FALSE;
    value = 0;
    for (i = 0; i < len; i++)
    {
        gint digit = hex_digit (spec[i]);

        if (digit < 0)
            return FALSE;
        value = (value << 4) | (guint32) digit;
    }
    if (len == 6)
        value = (value << 8) | 0xFFu;
    *out_color = value;
    return TRUE;
}

/* ------------------------------------------------------------------ */
/* LrgTooltip                                                           */

struct _LrgTooltip
{
    GObject parent_instance;

    GPtrArray *lines;   /* LrgTooltipLine *, owned */
};

G_DEFINE_FINAL_TYPE (LrgTooltip, lrg_tooltip, G_TYPE_OBJECT)

static void
lrg_tooltip_finalize (GObject *object)
{
    LrgTooltip *self = LRG_TOOLTIP (object);

    g_ptr_array_unref (self->lines);
    G_OBJECT_CLASS (lrg_tooltip_parent_class)->finalize (object);
}

static void
lrg_tooltip_class_init (LrgTooltipClass *klass)
{
    G_OBJECT_CLASS (klass)->finalize = lrg_tooltip_finalize;
}

static void
lrg_tooltip_init (LrgTooltip *self)
{
    self->lines = g_ptr_array_new_with_free_func ((GDestroyNotify) lrg_tooltip_line_free);
}

LrgTooltip *
lrg_tooltip_new (void)
{
    return g_object_new (LRG_TYPE_TOOLTIP, NULL);
}

LrgTooltip *
lrg_tooltip_copy (LrgTooltip *self)
{
    LrgTooltip *copy;
    guint i;

    g_return_val_if_fail (LRG_IS_TOOLTIP (self), NULL);
    copy = lrg_tooltip_new ();
    for (i = 0; i < self->lines->len; i++)
        g_ptr_array_add (copy->lines, lrg_tooltip_line_copy (g_ptr_array_index (self->lines, i)));
    return copy;
}

guint
lrg_tooltip_get_n_lines (LrgTooltip *self)
{
    g_return_val_if_fail (LRG_IS_TOOLTIP (self), 0);
    return self->lines->len;
}

LrgTooltipLine *
lrg_tooltip_get_line (LrgTooltip *self, guint index)
{
    g_return_val_if_fail (LRG_IS_TOOLTIP (self), NULL);
    return index < self->lines->len ? g_ptr_array_index (self->lines, index) : NULL;
}

/* appends a new line of @kind and returns it */
static LrgTooltipLine *
append_line (LrgTooltip *self, LrgTooltipLineKind kind)
{
    LrgTooltipLine *line = lrg_tooltip_line_new (kind);

    g_ptr_array_add (self->lines, line);
    return line;
}

LrgTooltipLine *
lrg_tooltip_add_line (LrgTooltip *self, const gchar *text, guint32 color)
{
    LrgTooltipLine *line;

    g_return_val_if_fail (LRG_IS_TOOLTIP (self), NULL);
    line = append_line (self, LRG_TOOLTIP_LINE_TEXT);
    line->left = g_strdup (text != NULL ? text : "");
    line->left_color = color;
    return line;
}

LrgTooltipLine *
lrg_tooltip_add_double_line (LrgTooltip  *self,
                             const gchar *left,
                             const gchar *right,
                             guint32      left_color,
                             guint32      right_color)
{
    LrgTooltipLine *line;

    g_return_val_if_fail (LRG_IS_TOOLTIP (self), NULL);
    line = append_line (self, LRG_TOOLTIP_LINE_DOUBLE);
    line->left = g_strdup (left != NULL ? left : "");
    line->right = g_strdup (right != NULL ? right : "");
    line->left_color = left_color;
    line->right_color = right_color;
    return line;
}

LrgTooltipLine *
lrg_tooltip_add_wrapped_line (LrgTooltip *self, const gchar *text, guint32 color)
{
    LrgTooltipLine *line = lrg_tooltip_add_line (self, text, color);

    if (line != NULL)
        line->wrap = TRUE;
    return line;
}

LrgTooltipLine *
lrg_tooltip_add_icon_line (LrgTooltip  *self,
                           const gchar *icon,
                           const gchar *text,
                           guint32      color)
{
    LrgTooltipLine *line;

    g_return_val_if_fail (LRG_IS_TOOLTIP (self), NULL);
    line = append_line (self, LRG_TOOLTIP_LINE_ICON);
    line->icon = g_strdup (icon != NULL ? icon : "");
    line->left = g_strdup (text != NULL ? text : "");
    line->left_color = color;
    return line;
}

LrgTooltipLine *
lrg_tooltip_add_separator (LrgTooltip *self)
{
    g_return_val_if_fail (LRG_IS_TOOLTIP (self), NULL);
    return append_line (self, LRG_TOOLTIP_LINE_SEPARATOR);
}

LrgTooltipLine *
lrg_tooltip_add_spacer (LrgTooltip *self)
{
    g_return_val_if_fail (LRG_IS_TOOLTIP (self), NULL);
    return append_line (self, LRG_TOOLTIP_LINE_SPACER);
}

LrgTooltipLine *
lrg_tooltip_insert_line (LrgTooltip *self, guint index, const LrgTooltipLine *line)
{
    LrgTooltipLine *copy;

    g_return_val_if_fail (LRG_IS_TOOLTIP (self), NULL);
    g_return_val_if_fail (line != NULL, NULL);
    copy = lrg_tooltip_line_copy (line);
    if (index >= self->lines->len)
        g_ptr_array_add (self->lines, copy);
    else
        g_ptr_array_insert (self->lines, (gint) index, copy);
    return copy;
}

gboolean
lrg_tooltip_remove_line (LrgTooltip *self, guint index)
{
    g_return_val_if_fail (LRG_IS_TOOLTIP (self), FALSE);
    if (index >= self->lines->len)
        return FALSE;
    g_ptr_array_remove_index (self->lines, index);
    return TRUE;
}

gint
lrg_tooltip_find_line (LrgTooltip *self, const gchar *id)
{
    guint i;

    g_return_val_if_fail (LRG_IS_TOOLTIP (self), -1);
    if (id == NULL || id[0] == '\0')
        return -1;
    for (i = 0; i < self->lines->len; i++)
    {
        LrgTooltipLine *line = g_ptr_array_index (self->lines, i);

        if (g_strcmp0 (line->id, id) == 0)
            return (gint) i;
    }
    return -1;
}

guint
lrg_tooltip_remove_by_id (LrgTooltip *self, const gchar *id)
{
    guint removed = 0;
    gint index;

    g_return_val_if_fail (LRG_IS_TOOLTIP (self), 0);
    while ((index = lrg_tooltip_find_line (self, id)) >= 0)
    {
        g_ptr_array_remove_index (self->lines, (guint) index);
        removed++;
    }
    return removed;
}

gboolean
lrg_tooltip_replace_line (LrgTooltip *self, guint index, const LrgTooltipLine *line)
{
    g_return_val_if_fail (LRG_IS_TOOLTIP (self), FALSE);
    g_return_val_if_fail (line != NULL, FALSE);
    if (index >= self->lines->len)
        return FALSE;
    /* free the old line and put the copy in its slot (the order is kept) */
    lrg_tooltip_line_free (g_ptr_array_index (self->lines, index));
    g_ptr_array_index (self->lines, index) = lrg_tooltip_line_copy (line);
    return TRUE;
}

void
lrg_tooltip_clear (LrgTooltip *self)
{
    g_return_if_fail (LRG_IS_TOOLTIP (self));
    g_ptr_array_set_size (self->lines, 0);
}

gchar *
lrg_tooltip_to_string (LrgTooltip *self)
{
    GString *out;
    guint i;

    g_return_val_if_fail (LRG_IS_TOOLTIP (self), NULL);
    out = g_string_new (NULL);
    for (i = 0; i < self->lines->len; i++)
    {
        LrgTooltipLine *line = g_ptr_array_index (self->lines, i);

        if (i > 0)
            g_string_append_c (out, '\n');
        switch (line->kind)
        {
        case LRG_TOOLTIP_LINE_TEXT:
            g_string_append (out, line->left != NULL ? line->left : "");
            break;
        case LRG_TOOLTIP_LINE_DOUBLE:
            g_string_append_printf (out, "%s | %s", line->left != NULL ? line->left : "",
                                    line->right != NULL ? line->right : "");
            break;
        case LRG_TOOLTIP_LINE_ICON:
            g_string_append_printf (out, "[%s] %s", line->icon != NULL ? line->icon : "",
                                    line->left != NULL ? line->left : "");
            break;
        case LRG_TOOLTIP_LINE_SEPARATOR:
            g_string_append (out, "---");
            break;
        case LRG_TOOLTIP_LINE_SPACER:
        default:
            break;
        }
    }
    return g_string_free (out, FALSE);
}
