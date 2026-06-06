/* lrg-standalone-host.c - LrgGameHost that owns a real window + main loop
 *
 * Copyright 2025 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * This file is part of Libregnum.
 */

#include "config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_GAMEMODULE

#include <graylib.h>

#include "lrg-standalone-host.h"
#include "lrg-game-host.h"
#include "../lrg-log.h"
#include "../lrg-enums.h"
#include "../core/lrg-engine.h"
#include "../graphics/lrg-window.h"
#include "../graphics/lrg-grl-window.h"
#include "../template/lrg-game-template.h"

struct _LrgStandaloneHost
{
    GObject    parent_instance;

    LrgEngine *engine;   /* Borrowed singleton (startup is refcounted) */
    LrgWindow *window;   /* Owned */
    gboolean   started;  /* TRUE while we hold a matching engine startup */
};

static void lrg_standalone_host_game_host_init (LrgGameHostInterface *iface);

G_DEFINE_TYPE_WITH_CODE (LrgStandaloneHost, lrg_standalone_host, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (LRG_TYPE_GAME_HOST,
                                                lrg_standalone_host_game_host_init))

/* ==========================================================================
 * LrgGameHost implementation
 * ========================================================================== */

static LrgEngine *
lrg_standalone_host_get_engine (LrgGameHost *host)
{
    return LRG_STANDALONE_HOST (host)->engine;
}

static LrgWindow *
lrg_standalone_host_get_window (LrgGameHost *host)
{
    return LRG_STANDALONE_HOST (host)->window;
}

static gboolean
lrg_standalone_host_get_owns_window (LrgGameHost *host)
{
    (void) host;
    return TRUE;
}

static void
lrg_standalone_host_begin_frame (LrgGameHost *host)
{
    lrg_window_begin_frame (LRG_STANDALONE_HOST (host)->window);
}

static void
lrg_standalone_host_clear (LrgGameHost *host,
                           GrlColor    *color)
{
    LrgStandaloneHost *self = LRG_STANDALONE_HOST (host);

    if (color != NULL)
        lrg_window_clear (self->window, color);
}

static void
lrg_standalone_host_end_frame (LrgGameHost *host)
{
    lrg_window_end_frame (LRG_STANDALONE_HOST (host)->window);
}

static void
lrg_standalone_host_get_render_size (LrgGameHost *host,
                                     gint        *width,
                                     gint        *height)
{
    LrgStandaloneHost *self = LRG_STANDALONE_HOST (host);

    if (width != NULL)
        *width = lrg_window_get_width (self->window);
    if (height != NULL)
        *height = lrg_window_get_height (self->window);
}

static gdouble
lrg_standalone_host_get_frame_delta (LrgGameHost *host)
{
    return (gdouble) lrg_window_get_frame_time (LRG_STANDALONE_HOST (host)->window);
}

static void
lrg_standalone_host_game_host_init (LrgGameHostInterface *iface)
{
    iface->get_engine = lrg_standalone_host_get_engine;
    iface->get_window = lrg_standalone_host_get_window;
    iface->get_owns_window = lrg_standalone_host_get_owns_window;
    iface->begin_frame = lrg_standalone_host_begin_frame;
    iface->clear = lrg_standalone_host_clear;
    iface->end_frame = lrg_standalone_host_end_frame;
    iface->get_render_size = lrg_standalone_host_get_render_size;
    iface->get_frame_delta = lrg_standalone_host_get_frame_delta;
    /* get_input_source left NULL: standalone uses live OS input. */
}

/* ==========================================================================
 * Lifecycle
 * ========================================================================== */

static void
lrg_standalone_host_cleanup (LrgStandaloneHost *self)
{
    /* Disconnect the window from the engine before releasing it. */
    if (self->engine != NULL && self->window != NULL)
        lrg_engine_set_window (self->engine, NULL);

    g_clear_object (&self->window);

    if (self->started && self->engine != NULL)
    {
        lrg_engine_shutdown (self->engine);
        self->started = FALSE;
    }
}

static void
lrg_standalone_host_dispose (GObject *object)
{
    lrg_standalone_host_cleanup (LRG_STANDALONE_HOST (object));

    G_OBJECT_CLASS (lrg_standalone_host_parent_class)->dispose (object);
}

static void
lrg_standalone_host_class_init (LrgStandaloneHostClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->dispose = lrg_standalone_host_dispose;
}

static void
lrg_standalone_host_init (LrgStandaloneHost *self)
{
    self->engine = NULL;
    self->window = NULL;
    self->started = FALSE;
}

LrgStandaloneHost *
lrg_standalone_host_new (LrgGameTemplate  *game,
                         GError          **error)
{
    LrgStandaloneHost *self;
    LrgGrlWindow      *grl_win;
    GrlWindow         *raw_window;
    const gchar       *title;
    gint               window_width = 0;
    gint               window_height = 0;
    gint               min_width = 0;
    gint               min_height = 0;
    gint               target_fps = 0;
    gboolean           vsync = FALSE;
    gboolean           allow_resize = FALSE;
    LrgFullscreenMode  fullscreen_mode = LRG_FULLSCREEN_WINDOWED;

    g_return_val_if_fail (LRG_IS_GAME_TEMPLATE (game), NULL);

    self = g_object_new (LRG_TYPE_STANDALONE_HOST, NULL);

    /* Bring the engine up. Startup is refcounted, so this is safe even if
     * something else already started it. */
    self->engine = lrg_engine_get_default ();
    if (!lrg_engine_startup (self->engine, error))
    {
        g_object_unref (self);
        return NULL;
    }
    self->started = TRUE;

    /* Read window configuration from the game template's properties. */
    title = lrg_game_template_get_title (game);
    lrg_game_template_get_window_size (game, &window_width, &window_height);
    g_object_get (game,
                  "min-width", &min_width,
                  "min-height", &min_height,
                  "fullscreen-mode", &fullscreen_mode,
                  "vsync", &vsync,
                  "target-fps", &target_fps,
                  "allow-resize", &allow_resize,
                  NULL);

    /* Create + configure the window (migrated from lrg_game_template_run). */
    grl_win = lrg_grl_window_new (window_width, window_height, title);
    if (grl_win == NULL)
    {
        g_set_error (error, LRG_ENGINE_ERROR, LRG_ENGINE_ERROR_STATE,
                     "Failed to create window");
        lrg_standalone_host_cleanup (self);
        g_object_unref (self);
        return NULL;
    }

    self->window = LRG_WINDOW (grl_win);

    /* Register window with engine so game states can find it. */
    lrg_engine_set_window (self->engine, self->window);

    raw_window = lrg_grl_window_get_grl_window (grl_win);

    lrg_window_set_target_fps (self->window, target_fps);

    if (vsync)
        lrg_grl_window_set_vsync (grl_win, TRUE);

    if (allow_resize)
        grl_window_set_state (raw_window, GRL_FLAG_WINDOW_RESIZABLE);

    grl_window_set_min_size (raw_window, min_width, min_height);

    if (fullscreen_mode == LRG_FULLSCREEN_FULLSCREEN)
        lrg_grl_window_toggle_fullscreen (grl_win);
    else if (fullscreen_mode == LRG_FULLSCREEN_BORDERLESS)
        grl_window_toggle_borderless (raw_window);

    return self;
}

void
lrg_standalone_host_teardown (LrgStandaloneHost *self)
{
    g_return_if_fail (LRG_IS_STANDALONE_HOST (self));

    lrg_standalone_host_cleanup (self);
}
