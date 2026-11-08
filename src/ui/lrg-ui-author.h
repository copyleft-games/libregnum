/* lrg-ui-author.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Authoring helpers for the UI layout document: place a named widget
 * at a canvas point (the WYSIWYG drag writes x and y through this) and
 * sample a UI animation track.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"

G_BEGIN_DECLS

#define LRG_TYPE_UI_ANIMATION (lrg_ui_animation_get_type ())

/**
 * lrg_ui_layout_place:
 * @yaml: a UI layout document
 * @widget: the widget's `name:`
 * @x: new x
 * @y: new y
 * @error: (nullable): return location for a #GError
 *
 * Sets `x` and `y` on the mapping whose `name` is @widget.
 *
 * Returns: (transfer full) (nullable): the rewritten YAML
 */
LRG_AVAILABLE_IN_ALL
gchar * lrg_ui_layout_place (const gchar  *yaml,
                             const gchar  *widget,
                             gfloat        x,
                             gfloat        y,
                             GError      **error);

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgUiAnimation, lrg_ui_animation, LRG, UI_ANIMATION, GObject)

/**
 * lrg_ui_animation_new:
 *
 * Returns: (transfer full): an empty UI animation
 */
LRG_AVAILABLE_IN_ALL
LrgUiAnimation * lrg_ui_animation_new (void);

/**
 * lrg_ui_animation_add_key:
 * @self: an animation
 * @widget: widget name
 * @prop: property name ("x", "opacity", ...)
 * @time: key time in seconds
 * @value: property value at @time
 */
LRG_AVAILABLE_IN_ALL
void lrg_ui_animation_add_key (LrgUiAnimation *self,
                               const gchar    *widget,
                               const gchar    *prop,
                               gfloat          time,
                               gfloat          value);

/**
 * lrg_ui_animation_sample:
 * @self: an animation
 * @widget: widget name
 * @prop: property name
 * @time: sample time in seconds
 * @out: (out): interpolated value
 *
 * Linear between the surrounding keys. Before the first key the value
 * is the first key; after the last key it is the last key.
 *
 * Returns: %FALSE when the track has no keys
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_ui_animation_sample (LrgUiAnimation *self,
                                  const gchar    *widget,
                                  const gchar    *prop,
                                  gfloat          time,
                                  gfloat         *out);

/**
 * lrg_ui_animation_save:
 * @self: an animation
 *
 * Returns: (transfer full): YAML with a `keys:` list of widget, prop, t, v
 */
LRG_AVAILABLE_IN_ALL
gchar * lrg_ui_animation_save (LrgUiAnimation *self);

/**
 * lrg_ui_animation_load:
 * @yaml: YAML written by lrg_ui_animation_save()
 * @error: (nullable): return location for a #GError
 *
 * Returns: (transfer full) (nullable): the animation
 */
LRG_AVAILABLE_IN_ALL
LrgUiAnimation * lrg_ui_animation_load (const gchar  *yaml,
                                        GError      **error);

G_END_DECLS
