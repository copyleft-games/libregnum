/* lrg-image-document.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "config.h"
#include "lrg-image-document.h"
#include <gio/gio.h>
#include <string.h>

#define UNDO_LIMIT 64

typedef struct
{
    guint     layer;   /* index of the snapshotted layer */
    GrlImage *image;   /* owned copy of that layer's pixels */
} UndoEntry;

struct _LrgImageDocument
{
    GObject     parent_instance;

    gint        width;
    gint        height;
    GPtrArray  *layers;      /* of LrgImageLayer*, bottom..top */
    guint       active;
    GrlImage   *flattened;   /* cached RGBA8 composite */
    gboolean    dirty;

    GQueue     *undo;        /* of UndoEntry* (tail = most recent) */
    GQueue     *redo;
};

G_DEFINE_TYPE (LrgImageDocument, lrg_image_document, G_TYPE_OBJECT)

static void
undo_entry_free (gpointer p)
{
    UndoEntry *e = p;
    if (e == NULL)
        return;
    g_clear_object (&e->image);
    g_free (e);
}

static void
clear_queue (GQueue *q)
{
    gpointer p;
    while ((p = g_queue_pop_head (q)) != NULL)
        undo_entry_free (p);
}

static void
lrg_image_document_finalize (GObject *object)
{
    LrgImageDocument *self = LRG_IMAGE_DOCUMENT (object);

    g_clear_pointer (&self->layers, g_ptr_array_unref);
    g_clear_object (&self->flattened);
    if (self->undo != NULL)
    {
        clear_queue (self->undo);
        g_queue_free (self->undo);
    }
    if (self->redo != NULL)
    {
        clear_queue (self->redo);
        g_queue_free (self->redo);
    }

    G_OBJECT_CLASS (lrg_image_document_parent_class)->finalize (object);
}

static void
lrg_image_document_class_init (LrgImageDocumentClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    object_class->finalize = lrg_image_document_finalize;
}

static void
lrg_image_document_init (LrgImageDocument *self)
{
    self->layers = g_ptr_array_new_with_free_func (g_object_unref);
    self->active = 0;
    self->flattened = NULL;
    self->dirty = TRUE;
    self->undo = g_queue_new ();
    self->redo = g_queue_new ();
}

LrgImageDocument *
lrg_image_document_new (gint width, gint height)
{
    LrgImageDocument *self;

    g_return_val_if_fail (width > 0, NULL);
    g_return_val_if_fail (height > 0, NULL);

    self = g_object_new (LRG_TYPE_IMAGE_DOCUMENT, NULL);
    self->width = width;
    self->height = height;
    g_ptr_array_add (self->layers, lrg_image_layer_new (width, height,
                                                        "Layer 1"));
    return self;
}

LrgImageDocument *
lrg_image_document_new_from_image (GrlImage *image)
{
    LrgImageDocument *self;
    gint w, h;

    g_return_val_if_fail (image != NULL, NULL);

    w = grl_image_get_width (image);
    h = grl_image_get_height (image);
    g_return_val_if_fail (w > 0 && h > 0, NULL);

    self = g_object_new (LRG_TYPE_IMAGE_DOCUMENT, NULL);
    self->width = w;
    self->height = h;
    g_ptr_array_add (self->layers,
                     lrg_image_layer_new_for_image (image, "Layer 1"));
    return self;
}

gint
lrg_image_document_get_width (LrgImageDocument *self)
{
    g_return_val_if_fail (LRG_IS_IMAGE_DOCUMENT (self), 0);
    return self->width;
}

gint
lrg_image_document_get_height (LrgImageDocument *self)
{
    g_return_val_if_fail (LRG_IS_IMAGE_DOCUMENT (self), 0);
    return self->height;
}

guint
lrg_image_document_get_n_layers (LrgImageDocument *self)
{
    g_return_val_if_fail (LRG_IS_IMAGE_DOCUMENT (self), 0);
    return self->layers->len;
}

LrgImageLayer *
lrg_image_document_get_layer (LrgImageDocument *self, guint index)
{
    g_return_val_if_fail (LRG_IS_IMAGE_DOCUMENT (self), NULL);
    if (index >= self->layers->len)
        return NULL;
    return g_ptr_array_index (self->layers, index);
}

guint
lrg_image_document_add_layer (LrgImageDocument *self, const gchar *name)
{
    LrgImageLayer *layer;

    g_return_val_if_fail (LRG_IS_IMAGE_DOCUMENT (self), 0);

    layer = lrg_image_layer_new (self->width, self->height, name);
    g_ptr_array_add (self->layers, layer);
    self->active = self->layers->len - 1;
    self->dirty = TRUE;
    return self->active;
}

guint
lrg_image_document_add_layer_for_image (LrgImageDocument *self,
                                        GrlImage         *image,
                                        const gchar      *name)
{
    LrgImageLayer *layer;

    g_return_val_if_fail (LRG_IS_IMAGE_DOCUMENT (self), 0);
    g_return_val_if_fail (image != NULL, 0);

    layer = lrg_image_layer_new_for_image (image, name);
    g_ptr_array_add (self->layers, layer);
    self->active = self->layers->len - 1;
    self->dirty = TRUE;
    return self->active;
}

gboolean
lrg_image_document_remove_layer (LrgImageDocument *self, guint index)
{
    g_return_val_if_fail (LRG_IS_IMAGE_DOCUMENT (self), FALSE);
    if (index >= self->layers->len)
        return FALSE;
    if (self->layers->len <= 1)
        return FALSE;  /* always keep at least one layer */

    g_ptr_array_remove_index (self->layers, index);
    if (self->active >= self->layers->len)
        self->active = self->layers->len - 1;
    self->dirty = TRUE;
    return TRUE;
}

gboolean
lrg_image_document_move_layer (LrgImageDocument *self, guint from, guint to)
{
    gpointer item;

    g_return_val_if_fail (LRG_IS_IMAGE_DOCUMENT (self), FALSE);
    if (from >= self->layers->len || to >= self->layers->len || from == to)
        return FALSE;

    item = g_ptr_array_index (self->layers, from);
    g_object_ref (item);
    /* g_ptr_array_remove_index preserves order; re-insert at TO. */
    g_ptr_array_remove_index (self->layers, from);
    g_ptr_array_insert (self->layers, (gint) to, item);
    self->active = to;
    self->dirty = TRUE;
    return TRUE;
}

gint
lrg_image_document_duplicate_layer (LrgImageDocument *self, guint index)
{
    LrgImageLayer *src;
    LrgImageLayer *dup;
    gint ox, oy;

    g_return_val_if_fail (LRG_IS_IMAGE_DOCUMENT (self), -1);
    if (index >= self->layers->len)
        return -1;

    src = g_ptr_array_index (self->layers, index);
    dup = lrg_image_layer_new_for_image (lrg_image_layer_get_image (src),
                                         lrg_image_layer_get_name (src));
    lrg_image_layer_set_opacity (dup, lrg_image_layer_get_opacity (src));
    lrg_image_layer_set_blend_mode (dup, lrg_image_layer_get_blend_mode (src));
    lrg_image_layer_set_visible (dup, lrg_image_layer_get_visible (src));
    lrg_image_layer_get_offset (src, &ox, &oy);
    lrg_image_layer_set_offset (dup, ox, oy);

    g_ptr_array_insert (self->layers, (gint) index + 1, dup);
    self->active = index + 1;
    self->dirty = TRUE;
    return (gint) self->active;
}

guint
lrg_image_document_get_active_index (LrgImageDocument *self)
{
    g_return_val_if_fail (LRG_IS_IMAGE_DOCUMENT (self), 0);
    return self->active;
}

void
lrg_image_document_set_active_index (LrgImageDocument *self, guint index)
{
    g_return_if_fail (LRG_IS_IMAGE_DOCUMENT (self));
    if (index < self->layers->len)
        self->active = index;
}

LrgImageLayer *
lrg_image_document_get_active_layer (LrgImageDocument *self)
{
    g_return_val_if_fail (LRG_IS_IMAGE_DOCUMENT (self), NULL);
    if (self->active >= self->layers->len)
        return NULL;
    return g_ptr_array_index (self->layers, self->active);
}

/* Composite one source pixel S onto destination pixel D.  SA is the source
 * alpha already pre-multiplied by the layer opacity (0..255).  All channels
 * are 8-bit; OVER uses straight-alpha source-over. */
static void
blend_pixel (guint8                 *d,
             const guint8           *s,
             GrlImageBlendMode       mode,
             guint                   sa)
{
    guint sr = s[0], sg = s[1], sb = s[2];
    guint dr = d[0], dg = d[1], db = d[2], da = d[3];

    switch (mode)
    {
    case GRL_IMAGE_BLEND_REPLACE:
        d[0] = (guint8) sr;
        d[1] = (guint8) sg;
        d[2] = (guint8) sb;
        d[3] = (guint8) sa;
        return;

    case GRL_IMAGE_BLEND_ADD:
        d[0] = (guint8) MIN (255u, dr + sr * sa / 255u);
        d[1] = (guint8) MIN (255u, dg + sg * sa / 255u);
        d[2] = (guint8) MIN (255u, db + sb * sa / 255u);
        d[3] = (guint8) MIN (255u, da + sa);
        return;

    case GRL_IMAGE_BLEND_MULTIPLY:
    {
        guint mr = dr * sr / 255u;
        guint mg = dg * sg / 255u;
        guint mb = db * sb / 255u;
        d[0] = (guint8) ((dr * (255u - sa) + mr * sa) / 255u);
        d[1] = (guint8) ((dg * (255u - sa) + mg * sa) / 255u);
        d[2] = (guint8) ((db * (255u - sa) + mb * sa) / 255u);
        return;  /* alpha unchanged */
    }

    case GRL_IMAGE_BLEND_SUBTRACT:
        d[0] = (guint8) (dr > sr * sa / 255u ? dr - sr * sa / 255u : 0u);
        d[1] = (guint8) (dg > sg * sa / 255u ? dg - sg * sa / 255u : 0u);
        d[2] = (guint8) (db > sb * sa / 255u ? db - sb * sa / 255u : 0u);
        return;  /* alpha unchanged */

    case GRL_IMAGE_BLEND_OVER:
    default:
    {
        guint inv = 255u - sa;
        guint out_a = sa + da * inv / 255u;
        if (out_a == 0u)
        {
            d[0] = d[1] = d[2] = d[3] = 0;
            return;
        }
        d[0] = (guint8) ((sr * sa + dr * da * inv / 255u) / out_a);
        d[1] = (guint8) ((sg * sa + dg * da * inv / 255u) / out_a);
        d[2] = (guint8) ((sb * sa + db * da * inv / 255u) / out_a);
        d[3] = (guint8) out_a;
        return;
    }
    }
}

GrlImage *
lrg_image_document_flatten (LrgImageDocument *self)
{
    GrlColor transparent = { 0, 0, 0, 0 };
    guint8 *dst;
    gsize dst_size = 0;
    guint i;

    g_return_val_if_fail (LRG_IS_IMAGE_DOCUMENT (self), NULL);

    if (!self->dirty && self->flattened != NULL)
        return self->flattened;

    if (self->flattened == NULL)
        self->flattened = grl_image_new_color (self->width, self->height,
                                               &transparent);

    dst = grl_image_get_pixels (self->flattened, &dst_size);
    if (dst == NULL)
        return self->flattened;
    memset (dst, 0, dst_size);

    for (i = 0; i < self->layers->len; i++)
    {
        LrgImageLayer *layer = g_ptr_array_index (self->layers, i);
        GrlImage *img;
        const guint8 *src;
        gsize src_size = 0;
        GrlImageBlendMode mode;
        gint lw, lh, ox, oy, ly, lx;
        guint op255;

        if (!lrg_image_layer_get_visible (layer))
            continue;
        img = lrg_image_layer_get_image (layer);
        if (img == NULL)
            continue;

        src = grl_image_get_pixels (img, &src_size);
        if (src == NULL)
            continue;
        lw = grl_image_get_width (img);
        lh = grl_image_get_height (img);
        lrg_image_layer_get_offset (layer, &ox, &oy);
        mode = lrg_image_layer_get_blend_mode (layer);
        op255 = (guint) (lrg_image_layer_get_opacity (layer) * 255.0f + 0.5f);

        for (ly = 0; ly < lh; ly++)
        {
            gint dy = ly + oy;
            if (dy < 0 || dy >= self->height)
                continue;
            for (lx = 0; lx < lw; lx++)
            {
                gint dx = lx + ox;
                gsize sidx, didx;
                guint sa;

                if (dx < 0 || dx >= self->width)
                    continue;
                sidx = ((gsize) ly * lw + lx) * 4;
                didx = ((gsize) dy * self->width + dx) * 4;
                sa = (guint) src[sidx + 3] * op255 / 255u;
                if (sa == 0u && mode != GRL_IMAGE_BLEND_REPLACE)
                    continue;
                blend_pixel (dst + didx, src + sidx, mode, sa);
            }
        }
    }

    self->dirty = FALSE;
    return self->flattened;
}

void
lrg_image_document_mark_dirty (LrgImageDocument *self)
{
    g_return_if_fail (LRG_IS_IMAGE_DOCUMENT (self));
    self->dirty = TRUE;
}

/* ── Whole-document geometric transforms ───────────────────────────────
 * Each transforms every layer's backing image and updates the canvas size.
 * Layer offsets are assumed 0 (the common full-canvas case in imgedit). */

void
lrg_image_document_resize (LrgImageDocument *self, gint w, gint h,
                           gboolean nearest)
{
    guint i;
    g_return_if_fail (LRG_IS_IMAGE_DOCUMENT (self));
    if (w <= 0 || h <= 0)
        return;
    for (i = 0; i < self->layers->len; i++)
    {
        LrgImageLayer *l = g_ptr_array_index (self->layers, i);
        GrlImage *img = lrg_image_layer_get_image (l);
        if (img == NULL)
            continue;
        if (nearest)
            grl_image_resize_nearest (img, w, h);
        else
            grl_image_resize (img, w, h);
    }
    self->width = w;
    self->height = h;
    self->dirty = TRUE;
}

void
lrg_image_document_crop (LrgImageDocument *self, gint x, gint y,
                         gint w, gint h)
{
    guint i;
    g_autoptr (GrlRectangle) rect = NULL;
    g_return_if_fail (LRG_IS_IMAGE_DOCUMENT (self));
    if (w <= 0 || h <= 0)
        return;
    rect = grl_rectangle_new ((gfloat) x, (gfloat) y, (gfloat) w, (gfloat) h);
    for (i = 0; i < self->layers->len; i++)
    {
        LrgImageLayer *l = g_ptr_array_index (self->layers, i);
        GrlImage *img = lrg_image_layer_get_image (l);
        if (img != NULL)
            grl_image_crop (img, rect);
    }
    self->width = w;
    self->height = h;
    self->dirty = TRUE;
}

void
lrg_image_document_rotate (LrgImageDocument *self, gboolean clockwise)
{
    guint i;
    gint t;
    g_return_if_fail (LRG_IS_IMAGE_DOCUMENT (self));
    for (i = 0; i < self->layers->len; i++)
    {
        LrgImageLayer *l = g_ptr_array_index (self->layers, i);
        GrlImage *img = lrg_image_layer_get_image (l);
        if (img == NULL)
            continue;
        if (clockwise)
            grl_image_rotate_cw (img);
        else
            grl_image_rotate_ccw (img);
    }
    t = self->width; self->width = self->height; self->height = t;
    self->dirty = TRUE;
}

gboolean
lrg_image_document_get_pixel (LrgImageDocument *self,
                              gint              x,
                              gint              y,
                              GrlColor         *out_color)
{
    GrlImage *flat;
    const guint8 *px;
    gsize n = 0;
    gsize idx;

    g_return_val_if_fail (LRG_IS_IMAGE_DOCUMENT (self), FALSE);
    if (x < 0 || y < 0 || x >= self->width || y >= self->height)
        return FALSE;

    flat = lrg_image_document_flatten (self);
    px = grl_image_get_pixels (flat, &n);
    if (px == NULL)
        return FALSE;
    idx = ((gsize) y * self->width + x) * 4;
    if (out_color != NULL)
    {
        out_color->r = px[idx + 0];
        out_color->g = px[idx + 1];
        out_color->b = px[idx + 2];
        out_color->a = px[idx + 3];
    }
    return TRUE;
}

void
lrg_image_document_fill_active (LrgImageDocument *self,
                                const GrlColor   *color)
{
    GrlColor transparent = { 0, 0, 0, 0 };
    LrgImageLayer *layer;
    GrlImage *img;

    g_return_if_fail (LRG_IS_IMAGE_DOCUMENT (self));
    layer = lrg_image_document_get_active_layer (self);
    if (layer == NULL || lrg_image_layer_get_locked (layer))
        return;
    img = lrg_image_layer_get_image (layer);
    if (img == NULL)
        return;
    grl_image_clear_background (img, color != NULL ? color : &transparent);
    self->dirty = TRUE;
}

gboolean
lrg_image_document_export (LrgImageDocument *self,
                           const gchar      *path,
                           GError          **error)
{
    GrlImage *flat;

    g_return_val_if_fail (LRG_IS_IMAGE_DOCUMENT (self), FALSE);
    g_return_val_if_fail (path != NULL, FALSE);

    flat = lrg_image_document_flatten (self);
    if (flat == NULL || !grl_image_export (flat, path))
    {
        g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                     "could not export image to '%s'", path);
        return FALSE;
    }
    return TRUE;
}

void
lrg_image_document_push_undo (LrgImageDocument *self)
{
    LrgImageLayer *layer;
    UndoEntry *e;

    g_return_if_fail (LRG_IS_IMAGE_DOCUMENT (self));
    layer = lrg_image_document_get_active_layer (self);
    if (layer == NULL)
        return;

    e = g_new0 (UndoEntry, 1);
    e->layer = self->active;
    e->image = grl_image_copy (lrg_image_layer_get_image (layer));
    g_queue_push_tail (self->undo, e);

    clear_queue (self->redo);

    /* Cap memory: drop the oldest snapshots. */
    while (g_queue_get_length (self->undo) > UNDO_LIMIT)
        undo_entry_free (g_queue_pop_head (self->undo));
}

static gboolean
swap_layer_image (LrgImageDocument *self, GQueue *from, GQueue *to)
{
    UndoEntry *e;
    LrgImageLayer *layer;
    UndoEntry *cur;

    e = g_queue_pop_tail (from);
    if (e == NULL)
        return FALSE;
    if (e->layer >= self->layers->len)
    {
        undo_entry_free (e);
        return FALSE;
    }
    layer = g_ptr_array_index (self->layers, e->layer);

    /* Save the current image onto the opposite stack, then restore the
     * snapshot.  grl_image_copy keeps the layer's image independent. */
    cur = g_new0 (UndoEntry, 1);
    cur->layer = e->layer;
    cur->image = grl_image_copy (lrg_image_layer_get_image (layer));
    g_queue_push_tail (to, cur);

    lrg_image_layer_set_image (layer, e->image);
    self->active = e->layer;
    self->dirty = TRUE;

    undo_entry_free (e);
    return TRUE;
}

gboolean
lrg_image_document_undo (LrgImageDocument *self)
{
    g_return_val_if_fail (LRG_IS_IMAGE_DOCUMENT (self), FALSE);
    return swap_layer_image (self, self->undo, self->redo);
}

gboolean
lrg_image_document_redo (LrgImageDocument *self)
{
    g_return_val_if_fail (LRG_IS_IMAGE_DOCUMENT (self), FALSE);
    return swap_layer_image (self, self->redo, self->undo);
}

gboolean
lrg_image_document_can_undo (LrgImageDocument *self)
{
    g_return_val_if_fail (LRG_IS_IMAGE_DOCUMENT (self), FALSE);
    return !g_queue_is_empty (self->undo);
}

gboolean
lrg_image_document_can_redo (LrgImageDocument *self)
{
    g_return_val_if_fail (LRG_IS_IMAGE_DOCUMENT (self), FALSE);
    return !g_queue_is_empty (self->redo);
}
