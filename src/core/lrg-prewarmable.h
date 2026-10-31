/* lrg-prewarmable.h - Interface for objects that build GPU resources ahead of use
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-types.h"

G_BEGIN_DECLS

#define LRG_TYPE_PREWARMABLE (lrg_prewarmable_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_INTERFACE (LrgPrewarmable, lrg_prewarmable, LRG, PREWARMABLE, GObject)

/**
 * LrgPrewarmableInterface:
 * @parent_iface: the parent interface
 * @prewarm: builds the object's GPU resources (compiles its shaders,
 *   uploads its buffers) now instead of on its first draw; returns %TRUE
 *   when nothing is left to do, %FALSE to be called again later (work
 *   split over several calls)
 *
 * The interface of objects whose first draw would otherwise compile
 * shaders or upload geometry. An implementation must be idempotent: a
 * second call, or a draw after the call, does no extra GPU work. It is
 * only called on the thread that owns the GL context, with a window open
 * (lrg_prewarmable_prewarm() checks the window).
 */
struct _LrgPrewarmableInterface
{
    GTypeInterface parent_iface;

    /*< public >*/
    gboolean (*prewarm) (LrgPrewarmable *self);

    /*< private >*/
    gpointer _reserved[4];
};

/**
 * lrg_prewarmable_prewarm:
 * @self: an #LrgPrewarmable
 *
 * Builds @self's GPU resources now: the shaders, buffers and textures its
 * first draw would otherwise create. Call it on the GL thread, typically
 * behind a loading screen (or let an #LrgPrewarm queue call it). Without
 * an open window it does nothing and returns %TRUE.
 *
 * Returns: %TRUE when @self is warm (or cannot be warmed), %FALSE when
 *   work remains and it should be called again
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_prewarmable_prewarm (LrgPrewarmable *self);

G_END_DECLS
