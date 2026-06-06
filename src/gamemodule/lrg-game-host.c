/* lrg-game-host.c - Host abstraction that drives a loaded game
 *
 * Copyright 2025 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * This file is part of Libregnum.
 */

#include "config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_GAMEMODULE

#include "lrg-game-host.h"
#include "../lrg-log.h"

G_DEFINE_INTERFACE (LrgGameHost, lrg_game_host, G_TYPE_OBJECT)

static void
lrg_game_host_default_init (LrgGameHostInterface *iface)
{
    (void) iface;
}

/* ==========================================================================
 * Public API (dispatch wrappers)
 * ========================================================================== */

LrgEngine *
lrg_game_host_get_engine (LrgGameHost *self)
{
    LrgGameHostInterface *iface;

    g_return_val_if_fail (LRG_IS_GAME_HOST (self), NULL);

    iface = LRG_GAME_HOST_GET_IFACE (self);
    g_return_val_if_fail (iface->get_engine != NULL, NULL);

    return iface->get_engine (self);
}

LrgWindow *
lrg_game_host_get_window (LrgGameHost *self)
{
    LrgGameHostInterface *iface;

    g_return_val_if_fail (LRG_IS_GAME_HOST (self), NULL);

    iface = LRG_GAME_HOST_GET_IFACE (self);
    if (iface->get_window == NULL)
        return NULL;

    return iface->get_window (self);
}

gboolean
lrg_game_host_get_owns_window (LrgGameHost *self)
{
    LrgGameHostInterface *iface;

    g_return_val_if_fail (LRG_IS_GAME_HOST (self), FALSE);

    iface = LRG_GAME_HOST_GET_IFACE (self);
    if (iface->get_owns_window == NULL)
        return FALSE;

    return iface->get_owns_window (self);
}

void
lrg_game_host_begin_frame (LrgGameHost *self)
{
    LrgGameHostInterface *iface;

    g_return_if_fail (LRG_IS_GAME_HOST (self));

    iface = LRG_GAME_HOST_GET_IFACE (self);
    g_return_if_fail (iface->begin_frame != NULL);

    iface->begin_frame (self);
}

void
lrg_game_host_clear (LrgGameHost *self,
                     GrlColor    *color)
{
    LrgGameHostInterface *iface;

    g_return_if_fail (LRG_IS_GAME_HOST (self));

    iface = LRG_GAME_HOST_GET_IFACE (self);
    if (iface->clear != NULL)
        iface->clear (self, color);
}

void
lrg_game_host_end_frame (LrgGameHost *self)
{
    LrgGameHostInterface *iface;

    g_return_if_fail (LRG_IS_GAME_HOST (self));

    iface = LRG_GAME_HOST_GET_IFACE (self);
    g_return_if_fail (iface->end_frame != NULL);

    iface->end_frame (self);
}

void
lrg_game_host_get_render_size (LrgGameHost *self,
                               gint        *width,
                               gint        *height)
{
    LrgGameHostInterface *iface;

    if (width != NULL)
        *width = 0;
    if (height != NULL)
        *height = 0;

    g_return_if_fail (LRG_IS_GAME_HOST (self));

    iface = LRG_GAME_HOST_GET_IFACE (self);
    if (iface->get_render_size != NULL)
        iface->get_render_size (self, width, height);
}

gdouble
lrg_game_host_get_frame_delta (LrgGameHost *self)
{
    LrgGameHostInterface *iface;

    g_return_val_if_fail (LRG_IS_GAME_HOST (self), 0.0);

    iface = LRG_GAME_HOST_GET_IFACE (self);
    if (iface->get_frame_delta == NULL)
        return 0.0;

    return iface->get_frame_delta (self);
}

LrgInputSoftware *
lrg_game_host_get_input_source (LrgGameHost *self)
{
    LrgGameHostInterface *iface;

    g_return_val_if_fail (LRG_IS_GAME_HOST (self), NULL);

    iface = LRG_GAME_HOST_GET_IFACE (self);
    if (iface->get_input_source == NULL)
        return NULL;

    return iface->get_input_source (self);
}
