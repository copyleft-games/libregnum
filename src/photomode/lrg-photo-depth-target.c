/* lrg-photo-depth-target.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "config.h"

#include <raylib.h>
#include <rlgl.h>

#include "lrg-photo-depth-target.h"
#include "../lrg-enums.h"
#include "../lrg-log.h"

/**
 * SECTION:lrg-photo-depth-target
 * @Title: LrgPhotoDepthTarget
 * @Short_description: A render target with a readable depth texture
 *
 * The scene is drawn into the target between
 * lrg_photo_depth_target_begin() and lrg_photo_depth_target_end(); the
 * #LrgPhotoDof effect then reads its colour and depth.
 */

struct _LrgPhotoDepthTarget
{
    GObject       parent_instance;

    RenderTexture target;
    gint          width;
    gint          height;
};

G_DEFINE_TYPE (LrgPhotoDepthTarget, lrg_photo_depth_target, G_TYPE_OBJECT)

static void
lrg_photo_depth_target_finalize (GObject *object)
{
    LrgPhotoDepthTarget *self = LRG_PHOTO_DEPTH_TARGET (object);

    /* the textures go with the context; without one there is nothing to free */
    if (self->target.id != 0 && IsWindowReady ())
    {
        rlFramebufferAttach (self->target.id, 0, RL_ATTACHMENT_COLOR_CHANNEL0,
                             RL_ATTACHMENT_TEXTURE2D, 0);
        rlFramebufferAttach (self->target.id, 0, RL_ATTACHMENT_DEPTH,
                             RL_ATTACHMENT_TEXTURE2D, 0);
        rlUnloadTexture (self->target.texture.id);
        rlUnloadTexture (self->target.depth.id);
        rlUnloadFramebuffer (self->target.id);
    }

    G_OBJECT_CLASS (lrg_photo_depth_target_parent_class)->finalize (object);
}

static void
lrg_photo_depth_target_class_init (LrgPhotoDepthTargetClass *klass)
{
    G_OBJECT_CLASS (klass)->finalize = lrg_photo_depth_target_finalize;
}

static void
lrg_photo_depth_target_init (LrgPhotoDepthTarget *self)
{
    (void)self;
}

LrgPhotoDepthTarget *
lrg_photo_depth_target_new (gint     width,
                            gint     height,
                            GError **error)
{
    g_autoptr(LrgPhotoDepthTarget) self = NULL;
    RenderTexture *rt;

    g_return_val_if_fail (width >= 1 && height >= 1, NULL);

    if (!IsWindowReady ())
    {
        g_set_error_literal (error, LRG_PHOTO_MODE_ERROR, LRG_PHOTO_MODE_ERROR_FAILED,
                             "no window: a depth target needs a GL context");
        return NULL;
    }

    self = g_object_new (LRG_TYPE_PHOTO_DEPTH_TARGET, NULL);
    self->width = width;
    self->height = height;
    rt = &self->target;

    /* colour: RGBA8 texture; depth: a texture, not a renderbuffer */
    rt->id = rlLoadFramebuffer ();
    rt->texture.id = rlLoadTexture (NULL, width, height, RL_PIXELFORMAT_UNCOMPRESSED_R8G8B8A8, 1);
    rt->texture.width = width;
    rt->texture.height = height;
    rt->texture.mipmaps = 1;
    rt->texture.format = RL_PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;
    rt->depth.id = rlLoadTextureDepth (width, height, false);
    rt->depth.width = width;
    rt->depth.height = height;
    rt->depth.mipmaps = 1;
    rt->depth.format = 19;
    if (rt->id == 0 || rt->texture.id == 0 || rt->depth.id == 0)
    {
        g_set_error_literal (error, LRG_PHOTO_MODE_ERROR, LRG_PHOTO_MODE_ERROR_FAILED,
                             "could not create the framebuffer or its textures");
        return NULL;
    }
    rlTextureParameters (rt->texture.id, RL_TEXTURE_MIN_FILTER, RL_TEXTURE_FILTER_LINEAR);
    rlTextureParameters (rt->texture.id, RL_TEXTURE_MAG_FILTER, RL_TEXTURE_FILTER_LINEAR);
    rlTextureParameters (rt->depth.id, RL_TEXTURE_MIN_FILTER, RL_TEXTURE_FILTER_NEAREST);
    rlTextureParameters (rt->depth.id, RL_TEXTURE_MAG_FILTER, RL_TEXTURE_FILTER_NEAREST);
    rlFramebufferAttach (rt->id, rt->texture.id, RL_ATTACHMENT_COLOR_CHANNEL0,
                         RL_ATTACHMENT_TEXTURE2D, 0);
    rlFramebufferAttach (rt->id, rt->depth.id, RL_ATTACHMENT_DEPTH, RL_ATTACHMENT_TEXTURE2D, 0);
    if (!rlFramebufferComplete (rt->id))
    {
        g_set_error_literal (error, LRG_PHOTO_MODE_ERROR, LRG_PHOTO_MODE_ERROR_FAILED,
                             "the depth framebuffer is incomplete");
        return NULL;
    }
    return (LrgPhotoDepthTarget *)g_steal_pointer (&self);
}

gint
lrg_photo_depth_target_get_width (LrgPhotoDepthTarget *self)
{
    g_return_val_if_fail (LRG_IS_PHOTO_DEPTH_TARGET (self), 0);

    return self->width;
}

gint
lrg_photo_depth_target_get_height (LrgPhotoDepthTarget *self)
{
    g_return_val_if_fail (LRG_IS_PHOTO_DEPTH_TARGET (self), 0);

    return self->height;
}

void
lrg_photo_depth_target_begin (LrgPhotoDepthTarget *self)
{
    g_return_if_fail (LRG_IS_PHOTO_DEPTH_TARGET (self));

    BeginTextureMode (self->target);
}

void
lrg_photo_depth_target_end (LrgPhotoDepthTarget *self)
{
    g_return_if_fail (LRG_IS_PHOTO_DEPTH_TARGET (self));

    EndTextureMode ();
}

guint
lrg_photo_depth_target_get_color_id (LrgPhotoDepthTarget *self)
{
    g_return_val_if_fail (LRG_IS_PHOTO_DEPTH_TARGET (self), 0);

    return self->target.texture.id;
}

guint
lrg_photo_depth_target_get_depth_id (LrgPhotoDepthTarget *self)
{
    g_return_val_if_fail (LRG_IS_PHOTO_DEPTH_TARGET (self), 0);

    return self->target.depth.id;
}
