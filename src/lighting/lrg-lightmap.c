/* lrg-lightmap.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Baked lightmap implementation.
 */

#include "lrg-lightmap.h"
#include "../lrg-log.h"

struct _LrgLightmap
{
    GObject parent_instance;

    guint   width;
    guint   height;
    guint   texture_id;
    guint8 *data;  /* RGB data */
    gboolean dirty;
};

G_DEFINE_FINAL_TYPE (LrgLightmap, lrg_lightmap, G_TYPE_OBJECT)

enum
{
    PROP_0,
    PROP_WIDTH,
    PROP_HEIGHT,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

static void
lrg_lightmap_finalize (GObject *object)
{
    LrgLightmap *self = LRG_LIGHTMAP (object);

    g_clear_pointer (&self->data, g_free);

    G_OBJECT_CLASS (lrg_lightmap_parent_class)->finalize (object);
}

static void
lrg_lightmap_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
    LrgLightmap *self = LRG_LIGHTMAP (object);
    switch (prop_id)
    {
    case PROP_WIDTH: g_value_set_uint (value, self->width); break;
    case PROP_HEIGHT: g_value_set_uint (value, self->height); break;
    default: G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_lightmap_class_init (LrgLightmapClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = lrg_lightmap_finalize;
    object_class->get_property = lrg_lightmap_get_property;

    properties[PROP_WIDTH] = g_param_spec_uint ("width", "Width", "Lightmap width", 1, 8192, 256, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
    properties[PROP_HEIGHT] = g_param_spec_uint ("height", "Height", "Lightmap height", 1, 8192, 256, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_lightmap_init (LrgLightmap *self)
{
    self->width = 256;
    self->height = 256;
    self->texture_id = 0;
    self->data = NULL;
    self->dirty = FALSE;
}

/* Public API */

LrgLightmap *
lrg_lightmap_new (guint width, guint height)
{
    LrgLightmap *self = g_object_new (LRG_TYPE_LIGHTMAP, NULL);
    self->width = width;
    self->height = height;
    self->data = g_new0 (guint8, width * height * 3);
    return self;
}

LrgLightmap *
lrg_lightmap_load (const gchar *path, GError **error)
{
    /* Load from file - implementation would use image loading */
    (void)path;
    (void)error;
    return lrg_lightmap_new (256, 256);
}

gboolean
lrg_lightmap_save (LrgLightmap *self, const gchar *path, GError **error)
{
    g_return_val_if_fail (LRG_IS_LIGHTMAP (self), FALSE);
    g_return_val_if_fail (path != NULL, FALSE);

    /* Save to file - implementation would use image writing */
    (void)path;
    (void)error;
    return TRUE;
}

guint lrg_lightmap_get_width (LrgLightmap *self) { g_return_val_if_fail (LRG_IS_LIGHTMAP (self), 0); return self->width; }
guint lrg_lightmap_get_height (LrgLightmap *self) { g_return_val_if_fail (LRG_IS_LIGHTMAP (self), 0); return self->height; }
guint lrg_lightmap_get_texture_id (LrgLightmap *self) { g_return_val_if_fail (LRG_IS_LIGHTMAP (self), 0); return self->texture_id; }

void
lrg_lightmap_set_pixel (LrgLightmap *self,
                        guint        x,
                        guint        y,
                        guint8       r,
                        guint8       g,
                        guint8       b)
{
    guint index;

    g_return_if_fail (LRG_IS_LIGHTMAP (self));
    g_return_if_fail (x < self->width);
    g_return_if_fail (y < self->height);

    index = (y * self->width + x) * 3;
    self->data[index] = r;
    self->data[index + 1] = g;
    self->data[index + 2] = b;
    self->dirty = TRUE;
}

void
lrg_lightmap_get_pixel (LrgLightmap *self,
                        guint        x,
                        guint        y,
                        guint8      *r,
                        guint8      *g,
                        guint8      *b)
{
    guint index;

    g_return_if_fail (LRG_IS_LIGHTMAP (self));
    g_return_if_fail (x < self->width);
    g_return_if_fail (y < self->height);

    index = (y * self->width + x) * 3;
    if (r) *r = self->data[index];
    if (g) *g = self->data[index + 1];
    if (b) *b = self->data[index + 2];
}

void
lrg_lightmap_clear (LrgLightmap *self,
                    guint8       r,
                    guint8       g,
                    guint8       b)
{
    guint i;

    g_return_if_fail (LRG_IS_LIGHTMAP (self));

    for (i = 0; i < self->width * self->height; i++)
    {
        self->data[i * 3] = r;
        self->data[i * 3 + 1] = g;
        self->data[i * 3 + 2] = b;
    }
    self->dirty = TRUE;
}

void
lrg_lightmap_upload (LrgLightmap *self)
{
    g_return_if_fail (LRG_IS_LIGHTMAP (self));

    if (!self->dirty)
        return;

    /* Upload to GPU texture - implementation would use graylib */
    self->dirty = FALSE;
}
