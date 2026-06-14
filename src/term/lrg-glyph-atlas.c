/* lrg-glyph-atlas.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "config.h"
#include "lrg-glyph-atlas.h"

/* Padding between packed glyphs, in pixels, to stop bilinear bleed. */
#define LRG_ATLAS_PAD 1

typedef struct
{
    GrlTexture *texture;   /* NULL until first upload to this page */
    gint        shelf_x;
    gint        shelf_y;
    gint        shelf_h;
} AtlasPage;

struct _LrgGlyphAtlas
{
    GObject     parent_instance;
    gint        page_w;
    gint        page_h;
    GPtrArray  *pages;     /* AtlasPage* */
    GHashTable *glyphs;    /* LrgGlyphKey* -> LrgGlyphMetrics* */
};

G_DEFINE_TYPE (LrgGlyphAtlas, lrg_glyph_atlas, G_TYPE_OBJECT)

enum
{
    PROP_0,
    PROP_PAGE_WIDTH,
    PROP_PAGE_HEIGHT,
    N_PROPS
};

enum
{
    SIGNAL_PAGE_ADDED,
    N_SIGNALS
};

static GParamSpec *properties[N_PROPS];
static guint signals[N_SIGNALS];

static void
atlas_page_free (gpointer data)
{
    AtlasPage *page = data;

    if (page == NULL)
        return;

    g_clear_object (&page->texture);
    g_free (page);
}

static guint
atlas_add_page (LrgGlyphAtlas *self)
{
    AtlasPage *page = g_new0 (AtlasPage, 1);
    guint idx;

    page->texture = NULL;
    page->shelf_x = LRG_ATLAS_PAD;
    page->shelf_y = LRG_ATLAS_PAD;
    page->shelf_h = 0;

    g_ptr_array_add (self->pages, page);
    idx = self->pages->len - 1;

    g_signal_emit (self, signals[SIGNAL_PAGE_ADDED], 0, idx);

    return idx;
}

static gboolean
page_try_place (AtlasPage *page,
                gint       pw,
                gint       ph,
                gint       w,
                gint       h,
                gint      *ox,
                gint      *oy)
{
    /* Zero-size glyphs (e.g. space) take no atlas room. */
    if (w <= 0 || h <= 0)
    {
        *ox = page->shelf_x;
        *oy = page->shelf_y;
        return TRUE;
    }

    /* Wrap to a new shelf if the glyph won't fit on the current one. */
    if (page->shelf_x + w + LRG_ATLAS_PAD > pw)
    {
        page->shelf_y += page->shelf_h + LRG_ATLAS_PAD;
        page->shelf_x = LRG_ATLAS_PAD;
        page->shelf_h = 0;
    }

    if (page->shelf_y + h + LRG_ATLAS_PAD > ph)
        return FALSE;

    *ox = page->shelf_x;
    *oy = page->shelf_y;
    page->shelf_x += w + LRG_ATLAS_PAD;
    if (h > page->shelf_h)
        page->shelf_h = h;

    return TRUE;
}

static GrlTexture *
ensure_page_texture (LrgGlyphAtlas *self,
                     AtlasPage     *page)
{
    if (page->texture == NULL)
    {
        g_autoptr(GrlColor) clear = grl_color_new (0, 0, 0, 0);
        g_autoptr(GrlImage) img =
            grl_image_new_color (self->page_w, self->page_h, clear);

        grl_image_set_format (img, GRL_PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
        page->texture = grl_texture_new_from_image (img);
        if (page->texture != NULL)
            grl_texture_set_filter (page->texture, GRL_TEXTURE_FILTER_BILINEAR);
    }

    return page->texture;
}

LrgGlyphAtlas *
lrg_glyph_atlas_new (gint page_width,
                     gint page_height)
{
    g_return_val_if_fail (page_width > 0, NULL);
    g_return_val_if_fail (page_height > 0, NULL);

    return g_object_new (LRG_TYPE_GLYPH_ATLAS,
                         "page-width", page_width,
                         "page-height", page_height,
                         NULL);
}

gboolean
lrg_glyph_atlas_reserve (LrgGlyphAtlas *self,
                         gint           width,
                         gint           height,
                         guint         *out_page,
                         gint          *out_x,
                         gint          *out_y)
{
    AtlasPage *page;
    guint idx;
    gint x = 0;
    gint y = 0;

    g_return_val_if_fail (LRG_IS_GLYPH_ATLAS (self), FALSE);

    /* A non-empty glyph that cannot fit a whole page is unplaceable. */
    if (width > 0 && height > 0
        && (width + 2 * LRG_ATLAS_PAD > self->page_w
            || height + 2 * LRG_ATLAS_PAD > self->page_h))
        return FALSE;

    if (self->pages->len == 0)
        atlas_add_page (self);

    idx = self->pages->len - 1;
    page = g_ptr_array_index (self->pages, idx);

    if (!page_try_place (page, self->page_w, self->page_h, width, height, &x, &y))
    {
        idx = atlas_add_page (self);
        page = g_ptr_array_index (self->pages, idx);
        if (!page_try_place (page, self->page_w, self->page_h,
                             width, height, &x, &y))
            return FALSE;
    }

    if (out_page != NULL)
        *out_page = idx;
    if (out_x != NULL)
        *out_x = x;
    if (out_y != NULL)
        *out_y = y;

    return TRUE;
}

LrgGlyphMetrics *
lrg_glyph_atlas_upload (LrgGlyphAtlas     *self,
                        const LrgGlyphKey *key,
                        const guint8      *pixels,
                        gint               width,
                        gint               height,
                        gint               bearing_x,
                        gint               bearing_y,
                        gint               advance,
                        gboolean           is_color)
{
    LrgGlyphMetrics *metrics;
    guint page_idx = 0;
    gint x = 0;
    gint y = 0;

    g_return_val_if_fail (LRG_IS_GLYPH_ATLAS (self), NULL);
    g_return_val_if_fail (key != NULL, NULL);

    metrics = g_hash_table_lookup (self->glyphs, key);
    if (metrics != NULL)
        return metrics;

    if (!lrg_glyph_atlas_reserve (self, width, height, &page_idx, &x, &y))
        return NULL;

    if (pixels != NULL && width > 0 && height > 0)
    {
        AtlasPage *page = g_ptr_array_index (self->pages, page_idx);
        GrlTexture *tex = ensure_page_texture (self, page);
        g_autoptr(GrlRectangle) rect = grl_rectangle_new (x, y, width, height);

        if (tex != NULL)
            grl_texture_update_rec (tex, rect, pixels);
    }

    metrics = lrg_glyph_metrics_new (page_idx, x, y, width, height,
                                     bearing_x, bearing_y, advance, is_color);
    if (width > 0 && height > 0)
        lrg_glyph_metrics_set_uv (metrics,
                                  (gfloat) x / (gfloat) self->page_w,
                                  (gfloat) y / (gfloat) self->page_h,
                                  (gfloat) (x + width) / (gfloat) self->page_w,
                                  (gfloat) (y + height) / (gfloat) self->page_h);

    g_hash_table_insert (self->glyphs, lrg_glyph_key_copy (key), metrics);

    return metrics;
}

LrgGlyphMetrics *
lrg_glyph_atlas_lookup (LrgGlyphAtlas     *self,
                        const LrgGlyphKey *key)
{
    g_return_val_if_fail (LRG_IS_GLYPH_ATLAS (self), NULL);
    g_return_val_if_fail (key != NULL, NULL);

    return g_hash_table_lookup (self->glyphs, key);
}

GrlTexture *
lrg_glyph_atlas_get_page_texture (LrgGlyphAtlas *self,
                                  guint          page)
{
    AtlasPage *p;

    g_return_val_if_fail (LRG_IS_GLYPH_ATLAS (self), NULL);

    if (page >= self->pages->len)
        return NULL;

    p = g_ptr_array_index (self->pages, page);
    return p->texture;
}

guint
lrg_glyph_atlas_get_page_count (LrgGlyphAtlas *self)
{
    g_return_val_if_fail (LRG_IS_GLYPH_ATLAS (self), 0);
    return self->pages->len;
}

guint
lrg_glyph_atlas_get_glyph_count (LrgGlyphAtlas *self)
{
    g_return_val_if_fail (LRG_IS_GLYPH_ATLAS (self), 0);
    return g_hash_table_size (self->glyphs);
}

gint
lrg_glyph_atlas_get_page_width (LrgGlyphAtlas *self)
{
    g_return_val_if_fail (LRG_IS_GLYPH_ATLAS (self), 0);
    return self->page_w;
}

gint
lrg_glyph_atlas_get_page_height (LrgGlyphAtlas *self)
{
    g_return_val_if_fail (LRG_IS_GLYPH_ATLAS (self), 0);
    return self->page_h;
}

guint
lrg_glyph_atlas_evict_font (LrgGlyphAtlas *self,
                            guint64        font_id)
{
    GHashTableIter iter;
    gpointer k;
    gpointer v;
    guint removed = 0;

    g_return_val_if_fail (LRG_IS_GLYPH_ATLAS (self), 0);

    g_hash_table_iter_init (&iter, self->glyphs);
    while (g_hash_table_iter_next (&iter, &k, &v))
    {
        const LrgGlyphKey *key = k;

        if (lrg_glyph_key_get_font_id (key) == font_id)
        {
            g_hash_table_iter_remove (&iter);
            removed++;
        }
    }

    return removed;
}

static void
lrg_glyph_atlas_get_property (GObject    *object,
                              guint       prop_id,
                              GValue     *value,
                              GParamSpec *pspec)
{
    LrgGlyphAtlas *self = LRG_GLYPH_ATLAS (object);

    switch (prop_id)
    {
    case PROP_PAGE_WIDTH:
        g_value_set_int (value, self->page_w);
        break;
    case PROP_PAGE_HEIGHT:
        g_value_set_int (value, self->page_h);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_glyph_atlas_set_property (GObject      *object,
                             guint          prop_id,
                             const GValue *value,
                             GParamSpec   *pspec)
{
    LrgGlyphAtlas *self = LRG_GLYPH_ATLAS (object);

    switch (prop_id)
    {
    case PROP_PAGE_WIDTH:
        self->page_w = g_value_get_int (value);
        break;
    case PROP_PAGE_HEIGHT:
        self->page_h = g_value_get_int (value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_glyph_atlas_finalize (GObject *object)
{
    LrgGlyphAtlas *self = LRG_GLYPH_ATLAS (object);

    g_clear_pointer (&self->glyphs, g_hash_table_unref);
    g_clear_pointer (&self->pages, g_ptr_array_unref);

    G_OBJECT_CLASS (lrg_glyph_atlas_parent_class)->finalize (object);
}

static void
lrg_glyph_atlas_class_init (LrgGlyphAtlasClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = lrg_glyph_atlas_finalize;
    object_class->get_property = lrg_glyph_atlas_get_property;
    object_class->set_property = lrg_glyph_atlas_set_property;

    /**
     * LrgGlyphAtlas:page-width:
     *
     * Width of each atlas page texture in pixels.
     */
    properties[PROP_PAGE_WIDTH] =
        g_param_spec_int ("page-width", "Page width",
                          "Width of each atlas page in pixels",
                          1, G_MAXINT, 1024,
                          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY
                          | G_PARAM_STATIC_STRINGS);

    /**
     * LrgGlyphAtlas:page-height:
     *
     * Height of each atlas page texture in pixels.
     */
    properties[PROP_PAGE_HEIGHT] =
        g_param_spec_int ("page-height", "Page height",
                          "Height of each atlas page in pixels",
                          1, G_MAXINT, 1024,
                          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY
                          | G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);

    /**
     * LrgGlyphAtlas::page-added:
     * @self: the atlas
     * @page: the index of the newly added page
     *
     * Emitted when the packer grows a new atlas page.
     */
    signals[SIGNAL_PAGE_ADDED] =
        g_signal_new ("page-added",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_FIRST,
                      0, NULL, NULL, NULL,
                      G_TYPE_NONE, 1, G_TYPE_UINT);
}

static void
lrg_glyph_atlas_init (LrgGlyphAtlas *self)
{
    self->page_w = 1024;
    self->page_h = 1024;
    self->pages = g_ptr_array_new_with_free_func (atlas_page_free);
    self->glyphs = g_hash_table_new_full (lrg_glyph_key_hash,
                                          lrg_glyph_key_equal,
                                          (GDestroyNotify) lrg_glyph_key_free,
                                          (GDestroyNotify) lrg_glyph_metrics_free);
}
