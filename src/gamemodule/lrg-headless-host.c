/* lrg-headless-host.c - LrgGameHost with no window, for headless runs
 *
 * Copyright 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * This file is part of Libregnum.
 */

#include "config.h"

#include "lrg-headless-host.h"
#include "lrg-game-host.h"
#include "../core/lrg-engine.h"

struct _LrgHeadlessHost
{
    GObject    parent_instance;

    LrgEngine *engine;       /* not owned: the engine is a singleton */
    gdouble    frame_delta;
};

static void lrg_headless_host_game_host_init (LrgGameHostInterface *iface);

G_DEFINE_FINAL_TYPE_WITH_CODE (LrgHeadlessHost, lrg_headless_host, G_TYPE_OBJECT,
                               G_IMPLEMENT_INTERFACE (LRG_TYPE_GAME_HOST,
                                                      lrg_headless_host_game_host_init))

static LrgEngine *
lrg_headless_host_get_engine (LrgGameHost *host)
{
    return LRG_HEADLESS_HOST (host)->engine;
}

static gboolean
lrg_headless_host_get_owns_window (LrgGameHost *host)
{
    /* There is no window: the template must not try to manage one */
    return FALSE;
}

static gdouble
lrg_headless_host_get_frame_delta (LrgGameHost *host)
{
    return LRG_HEADLESS_HOST (host)->frame_delta;
}

static void
lrg_headless_host_frame_noop (LrgGameHost *host)
{
    /* No render target to bind or present */
}

static void
lrg_headless_host_game_host_init (LrgGameHostInterface *iface)
{
    /* get_window and clear stay unset (no window, nothing to clear) */
    iface->get_engine = lrg_headless_host_get_engine;
    iface->begin_frame = lrg_headless_host_frame_noop;
    iface->end_frame = lrg_headless_host_frame_noop;
    iface->get_owns_window = lrg_headless_host_get_owns_window;
    iface->get_frame_delta = lrg_headless_host_get_frame_delta;
}

static void
lrg_headless_host_class_init (LrgHeadlessHostClass *klass)
{
}

static void
lrg_headless_host_init (LrgHeadlessHost *self)
{
    self->frame_delta = 1.0 / 60.0;
}

LrgHeadlessHost *
lrg_headless_host_new (LrgEngine *engine,
                       gdouble    frame_delta)
{
    LrgHeadlessHost *self;

    g_return_val_if_fail (LRG_IS_ENGINE (engine), NULL);

    self = g_object_new (LRG_TYPE_HEADLESS_HOST, NULL);
    self->engine = engine;
    self->frame_delta = frame_delta;
    return self;
}
