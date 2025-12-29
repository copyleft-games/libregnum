/* lrg-video-texture.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "config.h"
#include "lrg-video-texture.h"
#include <string.h>

/**
 * SECTION:lrg-video-texture
 * @Title: LrgVideoTexture
 * @Short_description: Video frame texture
 *
 * #LrgVideoTexture holds decoded video frame data for rendering.
 * The texture stores RGBA pixel data that can be uploaded to the GPU.
 */

struct _LrgVideoTexture
{
    GObject parent_instance;

    guint   width;
    guint   height;
    guint8 *data;
    gsize   data_size;
    gboolean valid;
};

G_DEFINE_TYPE (LrgVideoTexture, lrg_video_texture, G_TYPE_OBJECT)

enum
{
    PROP_0,
    PROP_WIDTH,
    PROP_HEIGHT,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

static void
lrg_video_texture_finalize (GObject *object)
{
    LrgVideoTexture *self = LRG_VIDEO_TEXTURE (object);

    g_free (self->data);

    G_OBJECT_CLASS (lrg_video_texture_parent_class)->finalize (object);
}

static void
lrg_video_texture_get_property (GObject    *object,
                                guint       prop_id,
                                GValue     *value,
                                GParamSpec *pspec)
{
    LrgVideoTexture *self = LRG_VIDEO_TEXTURE (object);

    switch (prop_id)
    {
        case PROP_WIDTH:
            g_value_set_uint (value, self->width);
            break;
        case PROP_HEIGHT:
            g_value_set_uint (value, self->height);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_video_texture_set_property (GObject      *object,
                                guint         prop_id,
                                const GValue *value,
                                GParamSpec   *pspec)
{
    LrgVideoTexture *self = LRG_VIDEO_TEXTURE (object);

    switch (prop_id)
    {
        case PROP_WIDTH:
            self->width = g_value_get_uint (value);
            break;
        case PROP_HEIGHT:
            self->height = g_value_get_uint (value);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_video_texture_constructed (GObject *object)
{
    LrgVideoTexture *self = LRG_VIDEO_TEXTURE (object);

    G_OBJECT_CLASS (lrg_video_texture_parent_class)->constructed (object);

    /* Allocate pixel data buffer */
    if (self->width > 0 && self->height > 0)
    {
        self->data_size = (gsize) self->width * self->height * 4;
        self->data = g_malloc0 (self->data_size);
        self->valid = TRUE;
    }
}

static void
lrg_video_texture_class_init (LrgVideoTextureClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = lrg_video_texture_finalize;
    object_class->get_property = lrg_video_texture_get_property;
    object_class->set_property = lrg_video_texture_set_property;
    object_class->constructed = lrg_video_texture_constructed;

    properties[PROP_WIDTH] =
        g_param_spec_uint ("width",
                           "Width",
                           "Texture width in pixels",
                           0, G_MAXUINT, 0,
                           G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

    properties[PROP_HEIGHT] =
        g_param_spec_uint ("height",
                           "Height",
                           "Texture height in pixels",
                           0, G_MAXUINT, 0,
                           G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_video_texture_init (LrgVideoTexture *self)
{
    self->data = NULL;
    self->data_size = 0;
    self->valid = FALSE;
}

LrgVideoTexture *
lrg_video_texture_new (guint width,
                       guint height)
{
    return g_object_new (LRG_TYPE_VIDEO_TEXTURE,
                         "width", width,
                         "height", height,
                         NULL);
}

guint
lrg_video_texture_get_width (LrgVideoTexture *texture)
{
    g_return_val_if_fail (LRG_IS_VIDEO_TEXTURE (texture), 0);
    return texture->width;
}

guint
lrg_video_texture_get_height (LrgVideoTexture *texture)
{
    g_return_val_if_fail (LRG_IS_VIDEO_TEXTURE (texture), 0);
    return texture->height;
}

void
lrg_video_texture_update (LrgVideoTexture *texture,
                          const guint8    *data,
                          gsize            size)
{
    g_return_if_fail (LRG_IS_VIDEO_TEXTURE (texture));
    g_return_if_fail (data != NULL);

    if (texture->data == NULL || texture->data_size == 0)
        return;

    /* Copy pixel data, clamping to buffer size */
    if (size > texture->data_size)
        size = texture->data_size;

    memcpy (texture->data, data, size);
    texture->valid = TRUE;
}

void
lrg_video_texture_clear (LrgVideoTexture *texture)
{
    g_return_if_fail (LRG_IS_VIDEO_TEXTURE (texture));

    if (texture->data != NULL && texture->data_size > 0)
    {
        memset (texture->data, 0, texture->data_size);
    }
}

gboolean
lrg_video_texture_is_valid (LrgVideoTexture *texture)
{
    g_return_val_if_fail (LRG_IS_VIDEO_TEXTURE (texture), FALSE);
    return texture->valid && texture->data != NULL && texture->data_size > 0;
}

const guint8 *
lrg_video_texture_get_data (LrgVideoTexture *texture,
                            gsize           *size)
{
    g_return_val_if_fail (LRG_IS_VIDEO_TEXTURE (texture), NULL);

    if (size != NULL)
        *size = texture->data_size;

    return texture->data;
}
