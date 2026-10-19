/* lrg-photo-host.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "config.h"

#include "lrg-photo-host.h"

/**
 * SECTION:lrg-photo-host
 * @Title: LrgPhotoHost
 * @Short_description: The game's side of photo mode
 *
 * Photo mode freezes the world, hides the HUD and may override the time
 * of day and the weather; it knows nothing about the game, so the game
 * implements this interface.  Every method is optional and the wrappers
 * here are safe to call on a host that leaves it out.
 */

G_DEFINE_INTERFACE (LrgPhotoHost, lrg_photo_host, G_TYPE_OBJECT)

static void
lrg_photo_host_default_init (LrgPhotoHostInterface *iface)
{
    (void)iface;
}

gboolean
lrg_photo_host_freeze_world (LrgPhotoHost *self,
                             gboolean      freeze)
{
    LrgPhotoHostInterface *iface;

    g_return_val_if_fail (LRG_IS_PHOTO_HOST (self), FALSE);

    iface = LRG_PHOTO_HOST_GET_IFACE (self);
    return iface->freeze_world != NULL ? iface->freeze_world (self, freeze) : FALSE;
}

void
lrg_photo_host_set_hud_visible (LrgPhotoHost *self,
                                gboolean      visible)
{
    LrgPhotoHostInterface *iface;

    g_return_if_fail (LRG_IS_PHOTO_HOST (self));

    iface = LRG_PHOTO_HOST_GET_IFACE (self);
    if (iface->set_hud_visible != NULL)
        iface->set_hud_visible (self, visible);
}

gboolean
lrg_photo_host_get_time_of_day (LrgPhotoHost *self,
                                gfloat       *hour)
{
    LrgPhotoHostInterface *iface;

    g_return_val_if_fail (LRG_IS_PHOTO_HOST (self), FALSE);
    g_return_val_if_fail (hour != NULL, FALSE);

    iface = LRG_PHOTO_HOST_GET_IFACE (self);
    return iface->get_time_of_day != NULL ? iface->get_time_of_day (self, hour) : FALSE;
}

void
lrg_photo_host_set_time_of_day (LrgPhotoHost *self,
                                gfloat        hour)
{
    LrgPhotoHostInterface *iface;

    g_return_if_fail (LRG_IS_PHOTO_HOST (self));

    iface = LRG_PHOTO_HOST_GET_IFACE (self);
    if (iface->set_time_of_day != NULL)
        iface->set_time_of_day (self, hour);
}

void
lrg_photo_host_clear_time_of_day (LrgPhotoHost *self)
{
    LrgPhotoHostInterface *iface;

    g_return_if_fail (LRG_IS_PHOTO_HOST (self));

    iface = LRG_PHOTO_HOST_GET_IFACE (self);
    if (iface->clear_time_of_day != NULL)
        iface->clear_time_of_day (self);
}

const gchar *
lrg_photo_host_get_weather (LrgPhotoHost *self)
{
    LrgPhotoHostInterface *iface;

    g_return_val_if_fail (LRG_IS_PHOTO_HOST (self), NULL);

    iface = LRG_PHOTO_HOST_GET_IFACE (self);
    return iface->get_weather != NULL ? iface->get_weather (self) : NULL;
}

const gchar * const *
lrg_photo_host_list_weathers (LrgPhotoHost *self)
{
    LrgPhotoHostInterface *iface;

    g_return_val_if_fail (LRG_IS_PHOTO_HOST (self), NULL);

    iface = LRG_PHOTO_HOST_GET_IFACE (self);
    return iface->list_weathers != NULL ? iface->list_weathers (self) : NULL;
}

void
lrg_photo_host_set_weather (LrgPhotoHost *self,
                            const gchar  *weather)
{
    LrgPhotoHostInterface *iface;

    g_return_if_fail (LRG_IS_PHOTO_HOST (self));
    g_return_if_fail (weather != NULL);

    iface = LRG_PHOTO_HOST_GET_IFACE (self);
    if (iface->set_weather != NULL)
        iface->set_weather (self, weather);
}

void
lrg_photo_host_clear_weather (LrgPhotoHost *self)
{
    LrgPhotoHostInterface *iface;

    g_return_if_fail (LRG_IS_PHOTO_HOST (self));

    iface = LRG_PHOTO_HOST_GET_IFACE (self);
    if (iface->clear_weather != NULL)
        iface->clear_weather (self);
}

gboolean
lrg_photo_host_get_anchor (LrgPhotoHost *self,
                           gfloat       *xyz)
{
    LrgPhotoHostInterface *iface;

    g_return_val_if_fail (LRG_IS_PHOTO_HOST (self), FALSE);
    g_return_val_if_fail (xyz != NULL, FALSE);

    iface = LRG_PHOTO_HOST_GET_IFACE (self);
    return iface->get_anchor != NULL ? iface->get_anchor (self, xyz) : FALSE;
}

void
lrg_photo_host_describe (LrgPhotoHost *self,
                         GHashTable   *metadata)
{
    LrgPhotoHostInterface *iface;

    g_return_if_fail (LRG_IS_PHOTO_HOST (self));
    g_return_if_fail (metadata != NULL);

    iface = LRG_PHOTO_HOST_GET_IFACE (self);
    if (iface->describe != NULL)
        iface->describe (self, metadata);
}
