/* lrg-reel-clip.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgReelClip - a timed, layered, renderable element of a reel.
 *
 * A clip draws itself for the current frame onto a CPU #LrgImageCanvas.  Two
 * authoring styles are supported:
 *
 * 1. Subclass #LrgReelClip and override the LrgReelClipClass.render vfunc.
 * 2. Construct a base #LrgReelClip with lrg_reel_clip_new_with_func() and a
 *    render callback (ideal for scripting and one-off clips).
 *
 * Clips carry their own timing (from-frame, duration), opacity and visibility,
 * and are stacked in z-order by the #LrgReel that owns them.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-types.h"

G_BEGIN_DECLS

#define LRG_TYPE_REEL_CLIP (lrg_reel_clip_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_DERIVABLE_TYPE (LrgReelClip, lrg_reel_clip, LRG, REEL_CLIP, GObject)

/**
 * LrgReelRenderFunc:
 * @clip: the #LrgReelClip being rendered.
 * @ctx: the #LrgReelContext for the current frame.
 * @canvas: the #LrgImageCanvas to draw onto.
 * @user_data: user data supplied to lrg_reel_clip_new_with_func().
 *
 * Render callback for a functional clip.  Draw the clip's appearance for the
 * frame given by lrg_reel_context_get_frame() onto @canvas.
 *
 * Since: 1.0
 */
typedef void (*LrgReelRenderFunc) (LrgReelClip    *clip,
                                   LrgReelContext *ctx,
                                   LrgImageCanvas *canvas,
                                   gpointer        user_data);

/**
 * LrgReelClipClass:
 * @parent_class: the parent class.
 * @render: draw the clip onto the canvas for the current frame.
 *
 * Class structure for #LrgReelClip.  Subclasses override @render to draw.
 *
 * Since: 1.0
 */
struct _LrgReelClipClass
{
    GObjectClass parent_class;

    /* Virtual methods */
    void (*render) (LrgReelClip    *self,
                    LrgReelContext *ctx,
                    LrgImageCanvas *canvas);

    /*< private >*/
    gpointer _reserved[8];
};

/**
 * lrg_reel_clip_new_with_func:
 * @func: (scope notified): the render callback.
 * @user_data: (closure func): user data for @func.
 * @destroy: (nullable): destroy notifier for @user_data.
 *
 * Creates a base #LrgReelClip that renders by invoking @func each frame.
 *
 * Returns: (transfer full): a new #LrgReelClip
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgReelClip *
lrg_reel_clip_new_with_func (LrgReelRenderFunc func,
                             gpointer          user_data,
                             GDestroyNotify    destroy);

/**
 * lrg_reel_clip_render:
 * @self: a #LrgReelClip
 * @ctx: the #LrgReelContext for the current frame.
 * @canvas: the #LrgImageCanvas to draw onto.
 *
 * Invokes the clip's render vfunc.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_reel_clip_render (LrgReelClip    *self,
                      LrgReelContext *ctx,
                      LrgImageCanvas *canvas);

/**
 * lrg_reel_clip_is_active_at:
 * @self: a #LrgReelClip
 * @frame: a parent-relative frame.
 *
 * Tests whether @frame lies within the clip's [from, from + duration) window.
 * A duration of %LRG_REEL_DURATION_INFINITE (or negative) means "until the end".
 *
 * Returns: %TRUE if the clip is active at @frame
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_reel_clip_is_active_at (LrgReelClip *self,
                            gint         frame);

LRG_AVAILABLE_IN_ALL
gint
lrg_reel_clip_get_from_frame (LrgReelClip *self);

LRG_AVAILABLE_IN_ALL
void
lrg_reel_clip_set_from_frame (LrgReelClip *self,
                              gint         from_frame);

LRG_AVAILABLE_IN_ALL
gint
lrg_reel_clip_get_duration_in_frames (LrgReelClip *self);

LRG_AVAILABLE_IN_ALL
void
lrg_reel_clip_set_duration_in_frames (LrgReelClip *self,
                                      gint         duration_in_frames);

LRG_AVAILABLE_IN_ALL
gdouble
lrg_reel_clip_get_opacity (LrgReelClip *self);

LRG_AVAILABLE_IN_ALL
void
lrg_reel_clip_set_opacity (LrgReelClip *self,
                           gdouble      opacity);

LRG_AVAILABLE_IN_ALL
gboolean
lrg_reel_clip_get_visible (LrgReelClip *self);

LRG_AVAILABLE_IN_ALL
void
lrg_reel_clip_set_visible (LrgReelClip *self,
                           gboolean     visible);

LRG_AVAILABLE_IN_ALL
const gchar *
lrg_reel_clip_get_name (LrgReelClip *self);

LRG_AVAILABLE_IN_ALL
void
lrg_reel_clip_set_name (LrgReelClip *self,
                        const gchar *name);

G_END_DECLS
