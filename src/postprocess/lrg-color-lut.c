/* lrg-color-lut.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * 3D colour lookup tables. Entries are floats, red index fastest
 * (index (r + g N + b N^2) x 3), the .cube order, over the unit cube;
 * a .cube domain other than 0 .. 1 is resampled into it at load, so
 * every table answers for inputs 0 .. 1. Sampling is trilinear, the
 * same as the GPU strip lookup (two bilinear taps in neighbouring blue
 * slices) in LrgColorGrade's shader.
 */

#include "config.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#ifndef LIBREGNUM_COMPILATION
#define LIBREGNUM_COMPILATION
#endif
#include "postprocess/lrg-color-lut.h"

/* a 1D table becomes a 3D table of this side */
#define ONE_D_EXPAND_SIZE (33)
#define ONE_D_MAX_SIZE    (65536)

struct _LrgColorLut
{
    gint     ref_count;
    guint    size;
    gfloat  *data;      /* size^3 x 3 */
    gchar   *title;
};

G_DEFINE_BOXED_TYPE (LrgColorLut, lrg_color_lut, lrg_color_lut_ref, lrg_color_lut_unref)

G_DEFINE_QUARK (lrg-color-lut-error-quark, lrg_color_lut_error)

static LrgColorLut *
lut_alloc (guint        size,
           const gchar *title)
{
    LrgColorLut *self = g_new0 (LrgColorLut, 1);

    self->ref_count = 1;
    self->size = size;
    self->data = g_new0 (gfloat, (gsize)size * size * size * 3);
    self->title = g_strdup (title);
    return self;
}

LrgColorLut *
lrg_color_lut_ref (LrgColorLut *self)
{
    g_return_val_if_fail (self != NULL, NULL);

    g_atomic_int_inc (&self->ref_count);
    return self;
}

void
lrg_color_lut_unref (LrgColorLut *self)
{
    g_return_if_fail (self != NULL);

    if (!g_atomic_int_dec_and_test (&self->ref_count))
        return;
    g_free (self->data);
    g_free (self->title);
    g_free (self);
}

LrgColorLut *
lrg_color_lut_new_identity (guint size)
{
    LrgColorLut *self;
    guint r;
    guint g;
    guint b;

    g_return_val_if_fail (size >= LRG_COLOR_LUT_MIN_SIZE && size <= LRG_COLOR_LUT_MAX_SIZE, NULL);

    self = lut_alloc (size, NULL);
    for (b = 0; b < size; b++)
        for (g = 0; g < size; g++)
            for (r = 0; r < size; r++)
            {
                gfloat *e = self->data + ((gsize)r + (gsize)g * size + (gsize)b * size * size) * 3;

                e[0] = (gfloat)r / (gfloat)(size - 1);
                e[1] = (gfloat)g / (gfloat)(size - 1);
                e[2] = (gfloat)b / (gfloat)(size - 1);
            }
    return self;
}

LrgColorLut *
lrg_color_lut_new_from_data (guint         size,
                             const gfloat *rgb,
                             const gchar  *title)
{
    LrgColorLut *self;
    gsize n;
    gsize i;

    g_return_val_if_fail (size >= LRG_COLOR_LUT_MIN_SIZE && size <= LRG_COLOR_LUT_MAX_SIZE, NULL);
    g_return_val_if_fail (rgb != NULL, NULL);

    self = lut_alloc (size, title);
    n = (gsize)size * size * size * 3;
    for (i = 0; i < n; i++)
        self->data[i] = isfinite (rgb[i]) ? rgb[i] : 0.0f;
    return self;
}

guint
lrg_color_lut_get_size (LrgColorLut *self)
{
    g_return_val_if_fail (self != NULL, 0);

    return self->size;
}

const gchar *
lrg_color_lut_get_title (LrgColorLut *self)
{
    g_return_val_if_fail (self != NULL, NULL);

    return self->title;
}

gboolean
lrg_color_lut_get_entry (LrgColorLut *self,
                         guint        r,
                         guint        g,
                         guint        b,
                         gfloat      *out_rgb)
{
    const gfloat *e;

    g_return_val_if_fail (self != NULL, FALSE);
    g_return_val_if_fail (out_rgb != NULL, FALSE);

    if (r >= self->size || g >= self->size || b >= self->size)
        return FALSE;
    e = self->data + ((gsize)r + (gsize)g * self->size + (gsize)b * self->size * self->size) * 3;
    memcpy (out_rgb, e, 3 * sizeof (gfloat));
    return TRUE;
}

/* the entry at integer grid (r, g, b) */
static const gfloat *
entry (LrgColorLut *self,
       guint        r,
       guint        g,
       guint        b)
{
    return self->data + ((gsize)r + (gsize)g * self->size + (gsize)b * self->size * self->size) * 3;
}

void
lrg_color_lut_sample (LrgColorLut  *self,
                      const gfloat *rgb,
                      gfloat       *out_rgb)
{
    gfloat s[3];
    guint  i0[3];
    guint  i1[3];
    gfloat f[3];
    gfloat res[3];
    guint  k;

    g_return_if_fail (self != NULL);
    g_return_if_fail (rgb != NULL && out_rgb != NULL);

    for (k = 0; k < 3; k++)
    {
        gfloat c = isfinite (rgb[k]) ? CLAMP (rgb[k], 0.0f, 1.0f) : 0.0f;

        s[k] = c * (gfloat)(self->size - 1);
        i0[k] = (guint)floorf (s[k]);
        if (i0[k] > self->size - 1)
            i0[k] = self->size - 1;
        i1[k] = MIN (i0[k] + 1, self->size - 1);
        f[k] = s[k] - (gfloat)i0[k];
    }

    /* trilinear: blend along red, then green, then blue */
    for (k = 0; k < 3; k++)
    {
        gfloat c00 = entry (self, i0[0], i0[1], i0[2])[k] * (1.0f - f[0])
                     + entry (self, i1[0], i0[1], i0[2])[k] * f[0];
        gfloat c10 = entry (self, i0[0], i1[1], i0[2])[k] * (1.0f - f[0])
                     + entry (self, i1[0], i1[1], i0[2])[k] * f[0];
        gfloat c01 = entry (self, i0[0], i0[1], i1[2])[k] * (1.0f - f[0])
                     + entry (self, i1[0], i0[1], i1[2])[k] * f[0];
        gfloat c11 = entry (self, i0[0], i1[1], i1[2])[k] * (1.0f - f[0])
                     + entry (self, i1[0], i1[1], i1[2])[k] * f[0];
        gfloat c0 = c00 * (1.0f - f[1]) + c10 * f[1];
        gfloat c1 = c01 * (1.0f - f[1]) + c11 * f[1];

        res[k] = c0 * (1.0f - f[2]) + c1 * f[2];
    }
    memcpy (out_rgb, res, sizeof res);
}

void
lrg_color_lut_apply_rgb (LrgColorLut  *self,
                         const gfloat *rgb,
                         gfloat        strength,
                         gfloat       *out_rgb)
{
    gfloat looked[3];
    gfloat in[3];
    guint  k;

    g_return_if_fail (self != NULL);
    g_return_if_fail (rgb != NULL && out_rgb != NULL);

    memcpy (in, rgb, sizeof in);
    strength = CLAMP (isfinite (strength) ? strength : 0.0f, 0.0f, 1.0f);
    lrg_color_lut_sample (self, in, looked);
    for (k = 0; k < 3; k++)
        out_rgb[k] = in[k] + (looked[k] - in[k]) * strength;
}

gboolean
lrg_color_lut_is_identity (LrgColorLut *self,
                           gfloat       tolerance)
{
    guint r;
    guint g;
    guint b;

    g_return_val_if_fail (self != NULL, FALSE);

    for (b = 0; b < self->size; b++)
        for (g = 0; g < self->size; g++)
            for (r = 0; r < self->size; r++)
            {
                const gfloat *e = entry (self, r, g, b);
                gfloat d = (gfloat)(self->size - 1);

                if (fabsf (e[0] - (gfloat)r / d) > tolerance
                    || fabsf (e[1] - (gfloat)g / d) > tolerance
                    || fabsf (e[2] - (gfloat)b / d) > tolerance)
                    return FALSE;
            }
    return TRUE;
}

LrgColorLut *
lrg_color_lut_new_resampled (LrgColorLut *self,
                             guint        size)
{
    LrgColorLut *out;
    guint r;
    guint g;
    guint b;

    g_return_val_if_fail (self != NULL, NULL);
    g_return_val_if_fail (size >= LRG_COLOR_LUT_MIN_SIZE && size <= LRG_COLOR_LUT_MAX_SIZE, NULL);

    out = lut_alloc (size, self->title);
    for (b = 0; b < size; b++)
        for (g = 0; g < size; g++)
            for (r = 0; r < size; r++)
            {
                gfloat in[3];

                in[0] = (gfloat)r / (gfloat)(size - 1);
                in[1] = (gfloat)g / (gfloat)(size - 1);
                in[2] = (gfloat)b / (gfloat)(size - 1);
                lrg_color_lut_sample (self, in, out->data + ((gsize)r + (gsize)g * size
                                                             + (gsize)b * size * size) * 3);
            }
    return out;
}

LrgColorLut *
lrg_color_lut_new_mix (LrgColorLut *a,
                       LrgColorLut *b,
                       gfloat       t)
{
    g_autoptr(LrgColorLut) other = NULL;
    LrgColorLut *out;
    gsize n;
    gsize i;

    g_return_val_if_fail (a != NULL && b != NULL, NULL);

    t = CLAMP (isfinite (t) ? t : 0.0f, 0.0f, 1.0f);
    other = b->size == a->size ? lrg_color_lut_ref (b) : lrg_color_lut_new_resampled (b, a->size);
    out = lut_alloc (a->size, t < 0.5f ? a->title : b->title);
    n = (gsize)a->size * a->size * a->size * 3;
    for (i = 0; i < n; i++)
        out->data[i] = a->data[i] + (other->data[i] - a->data[i]) * t;
    return out;
}

/* ------------------------------------------------------------------------ */
/* .cube                                                                    */
/* ------------------------------------------------------------------------ */

/* parse_floats:
 * Up to @max floats from @line; returns how many, -1 on junk. */
static gint
parse_floats (const gchar *line,
              gfloat      *out,
              gint         max)
{
    const gchar *p = line;
    gint n = 0;

    while (*p != '\0')
    {
        gchar  *end;
        gdouble v;

        while (*p == ' ' || *p == '\t')
            p++;
        if (*p == '\0')
            break;
        if (n == max)
            return -1;
        v = g_ascii_strtod (p, &end);
        if (end == p || !isfinite (v))
            return -1;
        out[n++] = (gfloat)v;
        p = end;
    }
    return n;
}

/* one_d_curve:
 * The 1D table @curve (@n entries x 3) at channel @k and input @x. */
static gfloat
one_d_curve (const gfloat *curve,
             guint         n,
             guint         k,
             gfloat        x)
{
    gfloat s = CLAMP (x, 0.0f, 1.0f) * (gfloat)(n - 1);
    guint  i0 = MIN ((guint)floorf (s), n - 1);
    guint  i1 = MIN (i0 + 1, n - 1);
    gfloat f = s - (gfloat)i0;

    return curve[i0 * 3 + k] * (1.0f - f) + curve[i1 * 3 + k] * f;
}

LrgColorLut *
lrg_color_lut_new_from_cube (const gchar  *text,
                             gssize        length,
                             GError      **error)
{
    g_autofree gchar *copy = NULL;
    g_auto(GStrv) lines = NULL;
    g_autofree gchar *title = NULL;
    g_autoptr(GArray) values = NULL;
    LrgColorLut *raw;
    gfloat dmin[3] = { 0.0f, 0.0f, 0.0f };
    gfloat dmax[3] = { 1.0f, 1.0f, 1.0f };
    guint  size3 = 0;
    guint  size1 = 0;
    guint  expected;
    guint  i;

    g_return_val_if_fail (text != NULL, NULL);
    g_return_val_if_fail (error == NULL || *error == NULL, NULL);

    copy = length < 0 ? g_strdup (text) : g_strndup (text, (gsize)length);
    lines = g_strsplit_set (copy, "\r\n", -1);
    values = g_array_new (FALSE, FALSE, sizeof (gfloat));

    for (i = 0; lines[i] != NULL; i++)
    {
        gchar *line = g_strstrip (lines[i]);
        gfloat v[3];
        gint   n;

        if (line[0] == '\0' || line[0] == '#')
            continue;
        if (g_ascii_isalpha (line[0]))
        {
            /* a keyword line */
            if (g_str_has_prefix (line, "TITLE"))
            {
                gchar *q = strchr (line, '"');
                gchar *e = q != NULL ? strrchr (q + 1, '"') : NULL;

                g_free (title);
                title = (q != NULL && e != NULL) ? g_strndup (q + 1, (gsize)(e - q - 1))
                                                 : g_strdup (g_strstrip (line + 5));
            }
            else if (g_str_has_prefix (line, "LUT_3D_SIZE") || g_str_has_prefix (line, "LUT_1D_SIZE"))
            {
                gboolean three = line[4] == '3';
                gchar   *end;
                gint64   s = g_ascii_strtoll (line + 11, &end, 10);

                if (end == line + 11)
                {
                    g_set_error (error, LRG_COLOR_LUT_ERROR, LRG_COLOR_LUT_ERROR_SIZE,
                                 "line %u: no size after %.11s", i + 1, line);
                    return NULL;
                }
                if (three && (s < LRG_COLOR_LUT_MIN_SIZE || s > LRG_COLOR_LUT_MAX_SIZE))
                {
                    g_set_error (error, LRG_COLOR_LUT_ERROR, LRG_COLOR_LUT_ERROR_SIZE,
                                 "line %u: LUT_3D_SIZE %" G_GINT64_FORMAT " is outside %d .. %d",
                                 i + 1, s, LRG_COLOR_LUT_MIN_SIZE, LRG_COLOR_LUT_MAX_SIZE);
                    return NULL;
                }
                if (!three && (s < 2 || s > ONE_D_MAX_SIZE))
                {
                    g_set_error (error, LRG_COLOR_LUT_ERROR, LRG_COLOR_LUT_ERROR_SIZE,
                                 "line %u: LUT_1D_SIZE %" G_GINT64_FORMAT " is outside 2 .. %d",
                                 i + 1, s, ONE_D_MAX_SIZE);
                    return NULL;
                }
                if (three)
                    size3 = (guint)s;
                else
                    size1 = (guint)s;
            }
            else if (g_str_has_prefix (line, "DOMAIN_MIN") || g_str_has_prefix (line, "DOMAIN_MAX"))
            {
                gboolean is_min = line[8] == 'I';

                if (parse_floats (line + 10, v, 3) != 3)
                {
                    g_set_error (error, LRG_COLOR_LUT_ERROR, LRG_COLOR_LUT_ERROR_PARSE,
                                 "line %u: %.10s needs three numbers", i + 1, line);
                    return NULL;
                }
                memcpy (is_min ? dmin : dmax, v, sizeof v);
            }
            else if (g_str_has_prefix (line, "LUT_3D_INPUT_RANGE")
                     || g_str_has_prefix (line, "LUT_1D_INPUT_RANGE"))
            {
                if (parse_floats (line + 18, v, 2) != 2)
                {
                    g_set_error (error, LRG_COLOR_LUT_ERROR, LRG_COLOR_LUT_ERROR_PARSE,
                                 "line %u: %.18s needs two numbers", i + 1, line);
                    return NULL;
                }
                dmin[0] = dmin[1] = dmin[2] = v[0];
                dmax[0] = dmax[1] = dmax[2] = v[1];
            }
            /* other keywords (LUT_IN_VIDEO_RANGE, ...) are skipped */
            continue;
        }

        n = parse_floats (line, v, 3);
        if (n != 3)
        {
            g_set_error (error, LRG_COLOR_LUT_ERROR, LRG_COLOR_LUT_ERROR_PARSE,
                         "line %u: expected three numbers: \"%s\"", i + 1, line);
            return NULL;
        }
        g_array_append_vals (values, v, 3);
    }

    if (size3 == 0 && size1 == 0)
    {
        g_set_error_literal (error, LRG_COLOR_LUT_ERROR, LRG_COLOR_LUT_ERROR_SIZE,
                             "no LUT_3D_SIZE or LUT_1D_SIZE");
        return NULL;
    }
    for (i = 0; i < 3; i++)
    {
        if (!(dmax[i] > dmin[i]))
        {
            g_set_error (error, LRG_COLOR_LUT_ERROR, LRG_COLOR_LUT_ERROR_DOMAIN,
                         "domain of channel %u: max %g is not above min %g", i,
                         (gdouble)dmax[i], (gdouble)dmin[i]);
            return NULL;
        }
    }
    expected = size3 != 0 ? size3 * size3 * size3 : size1;
    if (values->len != expected * 3)
    {
        g_set_error (error, LRG_COLOR_LUT_ERROR, LRG_COLOR_LUT_ERROR_SIZE,
                     "%u entries for a size of %u (%u expected)", values->len / 3,
                     size3 != 0 ? size3 : size1, expected);
        return NULL;
    }

    /* the table over its own domain */
    if (size3 != 0)
    {
        raw = lrg_color_lut_new_from_data (size3, (const gfloat *)(gpointer)values->data, title);
    }
    else
    {
        guint r;
        guint g;
        guint b;
        guint s = ONE_D_EXPAND_SIZE;

        raw = lut_alloc (s, title);
        for (b = 0; b < s; b++)
            for (g = 0; g < s; g++)
                for (r = 0; r < s; r++)
                {
                    gfloat *e = raw->data + ((gsize)r + (gsize)g * s + (gsize)b * s * s) * 3;
                    const gfloat *curve = (const gfloat *)(gpointer)values->data;

                    e[0] = one_d_curve (curve, size1, 0, (gfloat)r / (gfloat)(s - 1));
                    e[1] = one_d_curve (curve, size1, 1, (gfloat)g / (gfloat)(s - 1));
                    e[2] = one_d_curve (curve, size1, 2, (gfloat)b / (gfloat)(s - 1));
                }
    }

    /* a domain other than 0 .. 1: sample it at the unit cube's grid */
    if (dmin[0] != 0.0f || dmin[1] != 0.0f || dmin[2] != 0.0f
        || dmax[0] != 1.0f || dmax[1] != 1.0f || dmax[2] != 1.0f)
    {
        LrgColorLut *unit;
        guint s = raw->size;
        guint r;
        guint g;
        guint b;

        unit = lut_alloc (s, title);
        for (b = 0; b < s; b++)
            for (g = 0; g < s; g++)
                for (r = 0; r < s; r++)
                {
                    gfloat in[3];
                    guint  k;

                    in[0] = (gfloat)r / (gfloat)(s - 1);
                    in[1] = (gfloat)g / (gfloat)(s - 1);
                    in[2] = (gfloat)b / (gfloat)(s - 1);
                    for (k = 0; k < 3; k++)
                        in[k] = (in[k] - dmin[k]) / (dmax[k] - dmin[k]);
                    lrg_color_lut_sample (raw, in, unit->data + ((gsize)r + (gsize)g * s
                                                                 + (gsize)b * s * s) * 3);
                }
        lrg_color_lut_unref (raw);
        raw = unit;
    }
    return raw;
}

LrgColorLut *
lrg_color_lut_new_from_file (const gchar  *path,
                             GError      **error)
{
    g_autofree gchar *text = NULL;
    g_autoptr(GError) local = NULL;
    gsize length;
    LrgColorLut *lut;

    g_return_val_if_fail (path != NULL, NULL);
    g_return_val_if_fail (error == NULL || *error == NULL, NULL);

    if (!g_file_get_contents (path, &text, &length, &local))
    {
        g_set_error (error, LRG_COLOR_LUT_ERROR, LRG_COLOR_LUT_ERROR_IO, "%s", local->message);
        return NULL;
    }
    lut = lrg_color_lut_new_from_cube (text, (gssize)length, &local);
    if (lut == NULL)
    {
        g_set_error (error, LRG_COLOR_LUT_ERROR, local->code, "%s: %s", path, local->message);
        return NULL;
    }
    return lut;
}

gchar *
lrg_color_lut_to_cube (LrgColorLut *self)
{
    GString *out;
    gsize n;
    gsize i;

    g_return_val_if_fail (self != NULL, NULL);

    out = g_string_new (NULL);
    if (self->title != NULL)
        g_string_append_printf (out, "TITLE \"%s\"\n", self->title);
    g_string_append_printf (out, "LUT_3D_SIZE %u\n", self->size);
    n = (gsize)self->size * self->size * self->size;
    for (i = 0; i < n; i++)
    {
        gchar a[G_ASCII_DTOSTR_BUF_SIZE];
        gchar b[G_ASCII_DTOSTR_BUF_SIZE];
        gchar c[G_ASCII_DTOSTR_BUF_SIZE];

        g_ascii_formatd (a, sizeof a, "%.6f", (gdouble)self->data[i * 3]);
        g_ascii_formatd (b, sizeof b, "%.6f", (gdouble)self->data[i * 3 + 1]);
        g_ascii_formatd (c, sizeof c, "%.6f", (gdouble)self->data[i * 3 + 2]);
        g_string_append_printf (out, "%s %s %s\n", a, b, c);
    }
    return g_string_free (out, FALSE);
}

GBytes *
lrg_color_lut_to_strip (LrgColorLut *self,
                        guint       *out_width,
                        guint       *out_height)
{
    guint8 *pixels;
    guint   n;
    guint   r;
    guint   g;
    guint   b;

    g_return_val_if_fail (self != NULL, NULL);
    g_return_val_if_fail (out_width != NULL && out_height != NULL, NULL);

    n = self->size;
    pixels = g_malloc ((gsize)n * n * n * 4);
    for (g = 0; g < n; g++)
        for (b = 0; b < n; b++)
            for (r = 0; r < n; r++)
            {
                const gfloat *e = entry (self, r, g, b);
                guint8 *px = pixels + ((gsize)g * n * n + (gsize)b * n + r) * 4;
                guint   k;

                for (k = 0; k < 3; k++)
                    px[k] = (guint8)(CLAMP (e[k], 0.0f, 1.0f) * 255.0f + 0.5f);
                px[3] = 255;
            }
    *out_width = n * n;
    *out_height = n;
    return g_bytes_new_take (pixels, (gsize)n * n * n * 4);
}
