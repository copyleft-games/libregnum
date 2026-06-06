/* lrg-game-host.h - Host abstraction that drives a loaded game
 *
 * Copyright 2025 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * This file is part of Libregnum.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <graylib.h>
#include "../lrg-version.h"
#include "../lrg-types.h"

G_BEGIN_DECLS

#define LRG_TYPE_GAME_HOST (lrg_game_host_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_INTERFACE (LrgGameHost, lrg_game_host, LRG, GAME_HOST, GObject)

/**
 * LrgGameHostInterface:
 * @parent_iface: the parent interface
 * @get_engine: return the started #LrgEngine the game should use
 * @get_window: return the #LrgWindow the game should use (may be %NULL)
 * @get_owns_window: %TRUE if this host owns the window/engine lifecycle
 * @begin_frame: bind the render target (backbuffer or FBO) for a frame
 * @clear: clear the bound render target to @color
 * @end_frame: unbind/present the render target
 * @get_render_size: return the render target size in pixels
 * @get_frame_delta: return seconds elapsed since the previous frame
 * @get_input_source: return an injected input source, or %NULL
 *
 * A game host owns the window, the main loop, and the engine lifetime; a
 * loaded game borrows all three through this interface. The standalone shim
 * implements it with a real window and a blocking loop; an embedding host
 * (e.g. an editor or cmacs) implements it with its own GL context and an
 * offscreen framebuffer, driving the game frame-by-frame.
 *
 * Since: 1.0
 */
struct _LrgGameHostInterface
{
    GTypeInterface parent_iface;

    /*< public >*/

    /* Environment the game borrows from the host */
    LrgEngine *       (*get_engine)       (LrgGameHost *self);
    LrgWindow *       (*get_window)       (LrgGameHost *self);
    gboolean          (*get_owns_window)  (LrgGameHost *self);

    /* Render-target bracket: backbuffer (standalone) vs FBO (embedded) */
    void              (*begin_frame)      (LrgGameHost *self);
    void              (*clear)            (LrgGameHost *self,
                                           GrlColor    *color);
    void              (*end_frame)        (LrgGameHost *self);

    /* Render sizing + frame timing */
    void              (*get_render_size)  (LrgGameHost *self,
                                           gint        *width,
                                           gint        *height);
    gdouble           (*get_frame_delta)  (LrgGameHost *self);

    /* Optional input-injection source (NULL when the host pumps real input) */
    LrgInputSoftware *(*get_input_source) (LrgGameHost *self);

    /*< private >*/
    gpointer _reserved[7];
};

/**
 * lrg_game_host_get_engine:
 * @self: an #LrgGameHost
 *
 * Gets the started engine the game should use. The engine is owned by the
 * host (or is the process-wide singleton); the game must not shut it down.
 *
 * Returns: (transfer none): the #LrgEngine
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgEngine *
lrg_game_host_get_engine (LrgGameHost *self);

/**
 * lrg_game_host_get_window:
 * @self: an #LrgGameHost
 *
 * Gets the window the game should use, or %NULL when the host renders to an
 * offscreen target without an #LrgWindow.
 *
 * Returns: (transfer none) (nullable): the #LrgWindow
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgWindow *
lrg_game_host_get_window (LrgGameHost *self);

/**
 * lrg_game_host_get_owns_window:
 * @self: an #LrgGameHost
 *
 * Checks whether the host owns the window/engine lifecycle. When %TRUE the
 * host is responsible for associating the window with the engine.
 *
 * Returns: %TRUE if the host owns the window
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_game_host_get_owns_window (LrgGameHost *self);

/**
 * lrg_game_host_begin_frame:
 * @self: an #LrgGameHost
 *
 * Binds the render target for one frame. Call before
 * lrg_game_template_render().
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_game_host_begin_frame (LrgGameHost *self);

/**
 * lrg_game_host_clear:
 * @self: an #LrgGameHost
 * @color: (nullable): the clear color, or %NULL to skip clearing
 *
 * Clears the bound render target.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_game_host_clear (LrgGameHost *self,
                     GrlColor    *color);

/**
 * lrg_game_host_end_frame:
 * @self: an #LrgGameHost
 *
 * Unbinds and presents the render target. Call after
 * lrg_game_template_render().
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_game_host_end_frame (LrgGameHost *self);

/**
 * lrg_game_host_get_render_size:
 * @self: an #LrgGameHost
 * @width: (out) (optional): return location for width in pixels
 * @height: (out) (optional): return location for height in pixels
 *
 * Gets the size of the render target.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_game_host_get_render_size (LrgGameHost *self,
                               gint        *width,
                               gint        *height);

/**
 * lrg_game_host_get_frame_delta:
 * @self: an #LrgGameHost
 *
 * Gets the time elapsed since the previous frame, in seconds. Hosts supply
 * this because the raylib frame time is only valid when the backbuffer is
 * presented, which embedded (FBO) hosts skip.
 *
 * Returns: seconds since the previous frame
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gdouble
lrg_game_host_get_frame_delta (LrgGameHost *self);

/**
 * lrg_game_host_get_input_source:
 * @self: an #LrgGameHost
 *
 * Gets the host's injected input source, if any. Hosts that drive a game
 * without real OS input (e.g. an editor preview) return a software source so
 * the game's input map reads host-supplied events; hosts with live input
 * return %NULL.
 *
 * Returns: (transfer none) (nullable): the #LrgInputSoftware, or %NULL
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgInputSoftware *
lrg_game_host_get_input_source (LrgGameHost *self);

G_END_DECLS
