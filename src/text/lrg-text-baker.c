/* lrg-text-baker.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgTextBaker - Headless rasterization of TTF/OTF text.
 */

#include "config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_TEXT

#include "lrg-text-baker.h"
#include "../lrg-log.h"

#include <string.h>

/* ==========================================================================
 * Private Struct
 * ========================================================================== */

struct _LrgTextBaker
{
    GObject       parent_instance;

    GrlImageFont *font;  /* GObject - use g_object_unref / g_autoptr */
};

G_DEFINE_TYPE (LrgTextBaker, lrg_text_baker, G_TYPE_OBJECT)

/* ==========================================================================
 * Error Quark
 * ========================================================================== */

/**
 * lrg_text_baker_error_quark:
 *
 * Gets the error quark for #LrgTextBaker errors.
 *
 * Returns: the error quark
 */
GQuark
lrg_text_baker_error_quark (void)
{
    return g_quark_from_static_string ("lrg-text-baker-error-quark");
}

/* ==========================================================================
 * GObject Implementation
 * ========================================================================== */

static void
lrg_text_baker_finalize (GObject *object)
{
    LrgTextBaker *self = LRG_TEXT_BAKER (object);

    g_clear_object (&self->font);

    G_OBJECT_CLASS (lrg_text_baker_parent_class)->finalize (object);
}

static void
lrg_text_baker_class_init (LrgTextBakerClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = lrg_text_baker_finalize;
}

static void
lrg_text_baker_init (LrgTextBaker *self)
{
    self->font = NULL;
}

/* ==========================================================================
 * Private Helpers
 * ========================================================================== */

/*
 * measure_multiline:
 * @self: an #LrgTextBaker
 * @text: UTF-8 text (may contain '\n')
 * @px_size: font size in pixels
 * @out_width: (out): pixel width of the widest line
 * @out_height: (out): total pixel height across all lines
 *
 * Computes the bounding box for multi-line text by splitting on '\n' and
 * calling grl_image_measure_text_ttf() on each segment.  The returned
 * height is the sum of per-line heights; the width is the maximum line
 * width.
 *
 * Returns: %TRUE on success, %FALSE if there are no printable lines.
 */
static gboolean
measure_multiline (LrgTextBaker *self,
                   const gchar  *text,
                   gfloat        px_size,
                   gint         *out_width,
                   gint         *out_height)
{
    gchar  **lines = NULL;
    guint    n_lines;
    gint     max_w = 0;
    gint     total_h = 0;
    guint    i;

    lines = g_strsplit (text, "\n", -1);
    n_lines = g_strv_length (lines);

    for (i = 0; i < n_lines; i++)
    {
        g_autoptr(GrlVector2) sz = NULL;

        /* Blank line: contribute line height only */
        if (lines[i][0] == '\0')
        {
            /* Measure a single space to get the line height */
            sz = grl_image_measure_text_ttf (self->font, " ", px_size);
        }
        else
        {
            sz = grl_image_measure_text_ttf (self->font, lines[i], px_size);
        }

        if (sz == NULL)
            continue;

        if ((gint) sz->x > max_w)
            max_w = (gint) sz->x;

        total_h += (gint) sz->y;
    }

    g_strfreev (lines);

    if (max_w <= 0 || total_h <= 0)
        return FALSE;

    if (out_width != NULL)
        *out_width = max_w;

    if (out_height != NULL)
        *out_height = total_h;

    return TRUE;
}

/* ==========================================================================
 * Public API - Construction
 * ========================================================================== */

/**
 * lrg_text_baker_new_from_file:
 * @font_path: path to a TTF or OTF font file
 * @error: (nullable): return location for a #GError
 *
 * Creates a new #LrgTextBaker by loading a font from @font_path.
 *
 * Returns: (transfer full) (nullable): a new #LrgTextBaker, or %NULL on error.
 *
 * Since: 1.0
 */
LrgTextBaker *
lrg_text_baker_new_from_file (const gchar  *font_path,
                               GError      **error)
{
    LrgTextBaker *baker;
    GrlImageFont *font;

    g_return_val_if_fail (font_path != NULL, NULL);

    font = grl_image_font_new_from_file (font_path, error);
    if (font == NULL)
    {
        /* Ensure a useful error code when the graylib call doesn't set one */
        if (error != NULL && *error == NULL)
        {
            g_set_error (error,
                         LRG_TEXT_BAKER_ERROR,
                         LRG_TEXT_BAKER_ERROR_FONT_LOAD,
                         "Failed to load font from: %s", font_path);
        }

        lrg_debug (LRG_LOG_DOMAIN_TEXT,
                   "Failed to load image font from: %s", font_path);

        return NULL;
    }

    baker = g_object_new (LRG_TYPE_TEXT_BAKER, NULL);
    baker->font = font; /* takes ownership of the ref returned by graylib */

    lrg_debug (LRG_LOG_DOMAIN_TEXT, "Loaded image font: %s", font_path);

    return baker;
}

/**
 * lrg_text_baker_new_for_font:
 * @font: a #GrlImageFont to use
 *
 * Creates a new #LrgTextBaker that holds an additional reference to @font.
 *
 * Returns: (transfer full): a new #LrgTextBaker.
 *
 * Since: 1.0
 */
LrgTextBaker *
lrg_text_baker_new_for_font (GrlImageFont *font)
{
    LrgTextBaker *baker;

    g_return_val_if_fail (GRL_IS_IMAGE_FONT (font), NULL);

    baker = g_object_new (LRG_TYPE_TEXT_BAKER, NULL);
    baker->font = g_object_ref (font);

    return baker;
}

/* ==========================================================================
 * Public API - Font Access
 * ========================================================================== */

/**
 * lrg_text_baker_get_font:
 * @self: an #LrgTextBaker
 *
 * Returns the #GrlImageFont held by this baker.
 *
 * Returns: (transfer none): the underlying #GrlImageFont.
 *
 * Since: 1.0
 */
GrlImageFont *
lrg_text_baker_get_font (LrgTextBaker *self)
{
    g_return_val_if_fail (LRG_IS_TEXT_BAKER (self), NULL);

    return self->font;
}

/* ==========================================================================
 * Public API - Measurement
 * ========================================================================== */

/**
 * lrg_text_baker_measure:
 * @self: an #LrgTextBaker
 * @text: the UTF-8 text to measure
 * @px_size: the desired font size in pixels (must be > 0)
 * @out_width: (out) (nullable): return location for pixel width
 * @out_height: (out) (nullable): return location for pixel height
 *
 * Measures the bounding box of @text rendered at @px_size.
 *
 * Returns: %TRUE on success.
 *
 * Since: 1.0
 */
gboolean
lrg_text_baker_measure (LrgTextBaker *self,
                        const gchar  *text,
                        gfloat        px_size,
                        gint         *out_width,
                        gint         *out_height)
{
    g_return_val_if_fail (LRG_IS_TEXT_BAKER (self), FALSE);

    if (text == NULL || text[0] == '\0')
    {
        lrg_debug (LRG_LOG_DOMAIN_TEXT, "measure: text is NULL or empty");
        return FALSE;
    }

    if (px_size <= 0.0f)
    {
        lrg_debug (LRG_LOG_DOMAIN_TEXT, "measure: px_size must be > 0");
        return FALSE;
    }

    return measure_multiline (self, text, px_size, out_width, out_height);
}

/* ==========================================================================
 * Public API - Rendering
 * ========================================================================== */

/**
 * lrg_text_baker_render_to_image:
 * @self: an #LrgTextBaker
 * @text: the UTF-8 text to render (may contain '\n')
 * @px_size: the desired font size in pixels (must be > 0)
 * @color: the foreground text colour
 * @bg_or_null: (nullable): background colour, or %NULL for transparent
 *
 * Rasterizes @text into a tightly-sized RGBA #GrlImage.
 *
 * Returns: (transfer full) (nullable): a new #GrlImage, or %NULL on failure.
 *
 * Since: 1.0
 */
GrlImage *
lrg_text_baker_render_to_image (LrgTextBaker    *self,
                                const gchar     *text,
                                gfloat           px_size,
                                const GrlColor  *color,
                                const GrlColor  *bg_or_null)
{
    GrlImage    *image = NULL;
    gchar      **lines = NULL;
    guint        n_lines;
    gint         img_w;
    gint         img_h;
    gint         cursor_y;
    guint        i;

    g_return_val_if_fail (LRG_IS_TEXT_BAKER (self), NULL);
    g_return_val_if_fail (color != NULL, NULL);

    if (text == NULL || text[0] == '\0')
    {
        lrg_debug (LRG_LOG_DOMAIN_TEXT, "render_to_image: text is NULL or empty");
        return NULL;
    }

    if (px_size <= 0.0f)
    {
        lrg_debug (LRG_LOG_DOMAIN_TEXT, "render_to_image: px_size must be > 0");
        return NULL;
    }

    if (!measure_multiline (self, text, px_size, &img_w, &img_h))
    {
        lrg_debug (LRG_LOG_DOMAIN_TEXT, "render_to_image: measure returned empty bounds");
        return NULL;
    }

    /* Allocate the image: transparent background when bg_or_null is NULL */
    if (bg_or_null != NULL)
    {
        image = grl_image_new_color (img_w, img_h, bg_or_null);
    }
    else
    {
        g_autoptr(GrlColor) transparent = grl_color_new (0, 0, 0, 0);
        image = grl_image_new_color (img_w, img_h, transparent);
    }

    if (image == NULL)
    {
        lrg_debug (LRG_LOG_DOMAIN_TEXT, "render_to_image: failed to allocate image");
        return NULL;
    }

    grl_image_set_antialias (image, TRUE);

    /* Draw each line in turn, advancing the cursor vertically */
    lines = g_strsplit (text, "\n", -1);
    n_lines = g_strv_length (lines);
    cursor_y = 0;

    for (i = 0; i < n_lines; i++)
    {
        g_autoptr(GrlVector2) line_sz = NULL;
        const gchar          *line = lines[i];

        /* Measure line height (use a space for blank lines) */
        line_sz = grl_image_measure_text_ttf (self->font,
                                              line[0] != '\0' ? line : " ",
                                              px_size);

        if (line_sz == NULL)
            continue;

        if (line[0] != '\0')
        {
            grl_image_draw_text_ttf (image, self->font, line,
                                     0, cursor_y, px_size, color);
        }

        cursor_y += (gint) line_sz->y;
    }

    g_strfreev (lines);

    lrg_debug (LRG_LOG_DOMAIN_TEXT,
               "render_to_image: rendered \"%s\" at %.0f px -> %dx%d",
               text, (gdouble) px_size, img_w, img_h);

    return image;
}

/**
 * lrg_text_baker_render_to_texture:
 * @self: an #LrgTextBaker
 * @text: the UTF-8 text to render
 * @px_size: the desired font size in pixels (must be > 0)
 * @color: the foreground text colour
 * @bg_or_null: (nullable): background colour, or %NULL for transparent
 *
 * Convenience wrapper: renders to a #GrlImage then uploads it to GPU.
 *
 * Returns: (transfer full) (nullable): a new #GrlTexture, or %NULL on
 *   failure.
 *
 * Since: 1.0
 */
GrlTexture *
lrg_text_baker_render_to_texture (LrgTextBaker    *self,
                                  const gchar     *text,
                                  gfloat           px_size,
                                  const GrlColor  *color,
                                  const GrlColor  *bg_or_null)
{
    g_autoptr(GrlImage) image = NULL;

    g_return_val_if_fail (LRG_IS_TEXT_BAKER (self), NULL);
    g_return_val_if_fail (color != NULL, NULL);

    image = lrg_text_baker_render_to_image (self, text, px_size, color, bg_or_null);
    if (image == NULL)
        return NULL;

    return grl_texture_new_from_image (image);
}

/* ==========================================================================
 * Public API - Glyph Atlas
 * ========================================================================== */

/**
 * lrg_text_baker_bake_atlas:
 * @self: an #LrgTextBaker
 * @codepoints_utf8: UTF-8 string defining the glyph set to bake
 * @px_size: font size in pixels (must be > 0)
 * @color: foreground glyph colour
 * @padding: transparent pixel margin between glyphs
 * @out_regions: (out) (array length=out_n) (transfer full) (nullable):
 *   array of per-glyph rects in atlas space; free with g_free()
 * @out_n: (out) (nullable): number of entries in @out_regions
 *
 * Renders unique codepoints from @codepoints_utf8 into a shelf-packed
 * atlas image.
 *
 * Returns: (transfer full) (nullable): a new #GrlImage atlas, or %NULL.
 *
 * Since: 1.0
 */
GrlImage *
lrg_text_baker_bake_atlas (LrgTextBaker   *self,
                           const gchar    *codepoints_utf8,
                           gfloat          px_size,
                           const GrlColor *color,
                           gint            padding,
                           GrlRectangle  **out_regions,
                           guint          *out_n)
{
    /* Pass 1: collect unique codepoints that the font supports */
    GArray       *codepoints   = NULL; /* gunichar */
    GArray       *glyph_widths = NULL; /* gint */
    GArray       *glyph_heights= NULL; /* gint */
    GrlImage     *atlas        = NULL;
    GrlRectangle *regions      = NULL;
    const gchar  *p;
    gint          atlas_w;
    gint          atlas_h;
    gint          cursor_x;
    gint          cursor_y;
    gint          row_height;
    guint         i;
    guint         n;

    g_return_val_if_fail (LRG_IS_TEXT_BAKER (self), NULL);
    g_return_val_if_fail (color != NULL, NULL);

    if (codepoints_utf8 == NULL || codepoints_utf8[0] == '\0')
    {
        lrg_debug (LRG_LOG_DOMAIN_TEXT, "bake_atlas: codepoints_utf8 is NULL or empty");
        return NULL;
    }

    if (px_size <= 0.0f)
    {
        lrg_debug (LRG_LOG_DOMAIN_TEXT, "bake_atlas: px_size must be > 0");
        return NULL;
    }

    if (padding < 0)
        padding = 0;

    codepoints    = g_array_new (FALSE, FALSE, sizeof (gunichar));
    glyph_widths  = g_array_new (FALSE, FALSE, sizeof (gint));
    glyph_heights = g_array_new (FALSE, FALSE, sizeof (gint));

    /* Walk the UTF-8 string, collecting unique supported codepoints */
    p = codepoints_utf8;
    while (p != NULL && *p != '\0')
    {
        gunichar cp = g_utf8_get_char_validated (p, -1);
        gboolean already_seen = FALSE;
        guint    j;

        if (cp == (gunichar)-1 || cp == (gunichar)-2)
        {
            /* Invalid UTF-8 byte – skip */
            p++;
            continue;
        }

        /* Deduplicate */
        for (j = 0; j < codepoints->len; j++)
        {
            if (g_array_index (codepoints, gunichar, j) == cp)
            {
                already_seen = TRUE;
                break;
            }
        }

        if (!already_seen && grl_image_font_has_glyph (self->font, cp))
        {
            gchar    glyph_buf[8];
            gint     glyph_bytes;
            gint     gw = 0;
            gint     gh = 0;

            glyph_bytes = g_unichar_to_utf8 (cp, glyph_buf);
            glyph_buf[glyph_bytes] = '\0';

            if (measure_multiline (self, glyph_buf, px_size, &gw, &gh) && gw > 0 && gh > 0)
            {
                g_array_append_val (codepoints, cp);
                g_array_append_val (glyph_widths, gw);
                g_array_append_val (glyph_heights, gh);
            }
        }

        p = g_utf8_next_char (p);
    }

    n = codepoints->len;

    if (n == 0)
    {
        lrg_debug (LRG_LOG_DOMAIN_TEXT, "bake_atlas: no renderable codepoints found");
        goto done;
    }

    /* Pass 2: compute atlas dimensions via shelf packing */
    atlas_w = LRG_TEXT_BAKER_ATLAS_MAX_WIDTH;
    atlas_h = 0;
    cursor_x = 0;
    cursor_y = 0;
    row_height = 0;

    for (i = 0; i < n; i++)
    {
        gint gw = g_array_index (glyph_widths,  gint, i) + padding;
        gint gh = g_array_index (glyph_heights, gint, i);

        if (cursor_x + gw > atlas_w && cursor_x > 0)
        {
            /* Wrap to next shelf */
            cursor_y   += row_height + padding;
            cursor_x    = 0;
            row_height  = 0;
        }

        cursor_x   += gw;
        if (gh > row_height)
            row_height = gh;
    }

    atlas_h = cursor_y + row_height;

    if (atlas_h <= 0)
    {
        lrg_debug (LRG_LOG_DOMAIN_TEXT, "bake_atlas: atlas height is zero");
        goto done;
    }

    /* Allocate transparent atlas image */
    {
        g_autoptr(GrlColor) transparent = grl_color_new (0, 0, 0, 0);
        atlas = grl_image_new_color (atlas_w, atlas_h, transparent);
    }

    if (atlas == NULL)
    {
        lrg_debug (LRG_LOG_DOMAIN_TEXT, "bake_atlas: failed to allocate atlas image");
        goto done;
    }

    grl_image_set_antialias (atlas, TRUE);

    /* Allocate region array (one GrlRectangle per glyph) */
    regions = g_new0 (GrlRectangle, n);

    /* Pass 3: draw glyphs into atlas and record their rects */
    cursor_x   = 0;
    cursor_y   = 0;
    row_height = 0;

    for (i = 0; i < n; i++)
    {
        gunichar  cp = g_array_index (codepoints, gunichar, i);
        gint      gw = g_array_index (glyph_widths,  gint, i);
        gint      gh = g_array_index (glyph_heights, gint, i);
        gchar     glyph_buf[8];
        gint      glyph_bytes;

        if (cursor_x + gw + padding > atlas_w && cursor_x > 0)
        {
            cursor_y   += row_height + padding;
            cursor_x    = 0;
            row_height  = 0;
        }

        glyph_bytes = g_unichar_to_utf8 (cp, glyph_buf);
        glyph_buf[glyph_bytes] = '\0';

        grl_image_draw_text_ttf (atlas, self->font, glyph_buf,
                                 cursor_x, cursor_y, px_size, color);

        /* Record the pixel rect in atlas-local coordinates */
        regions[i].x      = (gfloat) cursor_x;
        regions[i].y      = (gfloat) cursor_y;
        regions[i].width  = (gfloat) gw;
        regions[i].height = (gfloat) gh;

        cursor_x += gw + padding;
        if (gh > row_height)
            row_height = gh;
    }

    lrg_debug (LRG_LOG_DOMAIN_TEXT,
               "bake_atlas: baked %u glyphs into %dx%d atlas",
               n, atlas_w, atlas_h);

    if (out_regions != NULL)
        *out_regions = regions;
    else
        g_free (regions);

    if (out_n != NULL)
        *out_n = n;

done:
    g_array_unref (codepoints);
    g_array_unref (glyph_widths);
    g_array_unref (glyph_heights);

    return atlas;
}
