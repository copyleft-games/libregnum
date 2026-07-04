/* lrg-image-layer.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "config.h"
#include "lrg-image-layer.h"

struct _LrgImageLayer
{
    GObject            parent_instance;

    GrlImage          *image;     /* RGBA8 */
    gchar             *name;
    gfloat             opacity;    /* 0..1 */
    GrlImageBlendMode  blend;
    gboolean           visible;
    gboolean           locked;
    gint               offset_x;
    gint               offset_y;
};

G_DEFINE_TYPE (LrgImageLayer, lrg_image_layer, G_TYPE_OBJECT)

static void
lrg_image_layer_finalize (GObject *object)
{
    LrgImageLayer *self = LRG_IMAGE_LAYER (object);

    g_clear_object (&self->image);
    g_clear_pointer (&self->name, g_free);

    G_OBJECT_CLASS (lrg_image_layer_parent_class)->finalize (object);
}

static void
lrg_image_layer_class_init (LrgImageLayerClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = lrg_image_layer_finalize;
}

static void
lrg_image_layer_init (LrgImageLayer *self)
{
    self->image = NULL;
    self->name = NULL;
    self->opacity = 1.0f;
    self->blend = GRL_IMAGE_BLEND_OVER;
    self->visible = TRUE;
    self->locked = FALSE;
    self->offset_x = 0;
    self->offset_y = 0;
}

/* Ensure IMAGE is RGBA8 (required for non-REPLACE blending). */
static void
ensure_rgba8 (GrlImage *image)
{
    if (image != NULL
        && grl_image_get_format (image) != GRL_PIXELFORMAT_UNCOMPRESSED_R8G8B8A8)
        grl_image_set_format (image, GRL_PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
}

LrgImageLayer *
lrg_image_layer_new (gint         width,
                     gint         height,
                     const gchar *name)
{
    LrgImageLayer *self;
    GrlColor transparent = { 0, 0, 0, 0 };

    g_return_val_if_fail (width > 0, NULL);
    g_return_val_if_fail (height > 0, NULL);

    self = g_object_new (LRG_TYPE_IMAGE_LAYER, NULL);
    self->image = grl_image_new_color (width, height, &transparent);
    ensure_rgba8 (self->image);
    self->name = g_strdup (name != NULL ? name : "Layer");
    return self;
}

LrgImageLayer *
lrg_image_layer_new_for_image (GrlImage    *image,
                               const gchar *name)
{
    LrgImageLayer *self;

    g_return_val_if_fail (image != NULL, NULL);

    self = g_object_new (LRG_TYPE_IMAGE_LAYER, NULL);
    self->image = grl_image_copy (image);
    ensure_rgba8 (self->image);
    self->name = g_strdup (name != NULL ? name : "Layer");
    return self;
}

GrlImage *
lrg_image_layer_get_image (LrgImageLayer *self)
{
    g_return_val_if_fail (LRG_IS_IMAGE_LAYER (self), NULL);
    return self->image;
}

void
lrg_image_layer_set_image (LrgImageLayer *self,
                           GrlImage      *image)
{
    g_return_if_fail (LRG_IS_IMAGE_LAYER (self));
    g_return_if_fail (image != NULL);

    g_clear_object (&self->image);
    self->image = grl_image_copy (image);
    ensure_rgba8 (self->image);
}

const gchar *
lrg_image_layer_get_name (LrgImageLayer *self)
{
    g_return_val_if_fail (LRG_IS_IMAGE_LAYER (self), NULL);
    return self->name;
}

void
lrg_image_layer_set_name (LrgImageLayer *self,
                          const gchar   *name)
{
    g_return_if_fail (LRG_IS_IMAGE_LAYER (self));
    g_free (self->name);
    self->name = g_strdup (name != NULL ? name : "Layer");
}

gfloat
lrg_image_layer_get_opacity (LrgImageLayer *self)
{
    g_return_val_if_fail (LRG_IS_IMAGE_LAYER (self), 1.0f);
    return self->opacity;
}

void
lrg_image_layer_set_opacity (LrgImageLayer *self,
                             gfloat         opacity)
{
    g_return_if_fail (LRG_IS_IMAGE_LAYER (self));
    self->opacity = CLAMP (opacity, 0.0f, 1.0f);
}

GrlImageBlendMode
lrg_image_layer_get_blend_mode (LrgImageLayer *self)
{
    g_return_val_if_fail (LRG_IS_IMAGE_LAYER (self), GRL_IMAGE_BLEND_OVER);
    return self->blend;
}

void
lrg_image_layer_set_blend_mode (LrgImageLayer     *self,
                                GrlImageBlendMode  mode)
{
    g_return_if_fail (LRG_IS_IMAGE_LAYER (self));
    self->blend = mode;
}

gboolean
lrg_image_layer_get_visible (LrgImageLayer *self)
{
    g_return_val_if_fail (LRG_IS_IMAGE_LAYER (self), FALSE);
    return self->visible;
}

void
lrg_image_layer_set_visible (LrgImageLayer *self,
                             gboolean       visible)
{
    g_return_if_fail (LRG_IS_IMAGE_LAYER (self));
    self->visible = visible;
}

gboolean
lrg_image_layer_get_locked (LrgImageLayer *self)
{
    g_return_val_if_fail (LRG_IS_IMAGE_LAYER (self), FALSE);
    return self->locked;
}

void
lrg_image_layer_set_locked (LrgImageLayer *self,
                            gboolean       locked)
{
    g_return_if_fail (LRG_IS_IMAGE_LAYER (self));
    self->locked = locked;
}

void
lrg_image_layer_get_offset (LrgImageLayer *self,
                            gint          *x,
                            gint          *y)
{
    g_return_if_fail (LRG_IS_IMAGE_LAYER (self));
    if (x != NULL)
        *x = self->offset_x;
    if (y != NULL)
        *y = self->offset_y;
}

void
lrg_image_layer_set_offset (LrgImageLayer *self,
                            gint           x,
                            gint           y)
{
    g_return_if_fail (LRG_IS_IMAGE_LAYER (self));
    self->offset_x = x;
    self->offset_y = y;
}

gint
lrg_image_layer_get_width (LrgImageLayer *self)
{
    g_return_val_if_fail (LRG_IS_IMAGE_LAYER (self), 0);
    return self->image != NULL ? grl_image_get_width (self->image) : 0;
}

gint
lrg_image_layer_get_height (LrgImageLayer *self)
{
    g_return_val_if_fail (LRG_IS_IMAGE_LAYER (self), 0);
    return self->image != NULL ? grl_image_get_height (self->image) : 0;
}
