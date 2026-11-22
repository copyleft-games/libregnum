/* lrg-nameplate.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgNameplate: a small one-line label pinned to an entity, with an
 * optional coloured status dot.  Long names are shortened ("Kudu the
 * Gre...") and shown in full while the pointer is over the plate.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <graylib.h>
#include "../lrg-version.h"
#include "lrg-world-widget.h"

G_BEGIN_DECLS

#define LRG_TYPE_NAMEPLATE (lrg_nameplate_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgNameplate, lrg_nameplate, LRG, NAMEPLATE, LrgWorldWidget)

/**
 * lrg_nameplate_new:
 * @text: (nullable): the full name
 *
 * Creates a nameplate: 12 px white text on a translucent dark pill,
 * shortened past 14 characters, 4 px above its anchor.
 *
 * Returns: (transfer full): a new #LrgNameplate
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
LrgNameplate * lrg_nameplate_new (const gchar *text);

/**
 * lrg_nameplate_set_text:
 * @self: an #LrgNameplate
 * @text: (nullable): the full name
 *
 * Replaces the name.
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_nameplate_set_text (LrgNameplate *self,
                             const gchar  *text);

/**
 * lrg_nameplate_get_text:
 * @self: an #LrgNameplate
 *
 * Returns: (transfer none): the full name
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_nameplate_get_text (LrgNameplate *self);

/**
 * lrg_nameplate_set_max_chars:
 * @self: an #LrgNameplate
 * @max_chars: characters shown before the name is shortened (0: never)
 *
 * Sets how long a name may be before it is shortened with "...".
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_nameplate_set_max_chars (LrgNameplate *self,
                                  guint         max_chars);

/**
 * lrg_nameplate_get_display_text:
 * @self: an #LrgNameplate
 *
 * The text the plate draws now: the full name while hovered, the short
 * form otherwise.
 *
 * Returns: (transfer none): the text
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_nameplate_get_display_text (LrgNameplate *self);

/**
 * lrg_nameplate_set_hovered:
 * @self: an #LrgNameplate
 * @hovered: whether the pointer is over the plate
 *
 * Shows the full name (and raises the plate's opacity) while hovered.
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_nameplate_set_hovered (LrgNameplate *self,
                                gboolean      hovered);

/**
 * lrg_nameplate_get_hovered:
 * @self: an #LrgNameplate
 *
 * Returns: %TRUE while hovered
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_nameplate_get_hovered (LrgNameplate *self);

/**
 * lrg_nameplate_update_hover:
 * @self: an #LrgNameplate
 * @pointer_x: pointer x in screen pixels
 * @pointer_y: pointer y in screen pixels
 *
 * Sets the hovered state from the pointer and the box last drawn.  The
 * plate is tested at its short width, so it does not grow under the
 * pointer and flicker.
 *
 * Returns: the new hovered state
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_nameplate_update_hover (LrgNameplate *self,
                                     gfloat        pointer_x,
                                     gfloat        pointer_y);

/**
 * lrg_nameplate_set_badge_color:
 * @self: an #LrgNameplate
 * @color: (nullable): colour of the status dot left of the name; %NULL
 *   removes it
 *
 * Shows a status dot (busy, idle, away) beside the name.
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_nameplate_set_badge_color (LrgNameplate   *self,
                                    const GrlColor *color);

/**
 * lrg_nameplate_set_style:
 * @self: an #LrgNameplate
 * @background: (nullable): pill colour (%NULL keeps it)
 * @foreground: (nullable): text colour (%NULL keeps it)
 * @font_size: text size in pixels at scale 1 (> 0)
 *
 * Sets the plate's look.
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_nameplate_set_style (LrgNameplate   *self,
                              const GrlColor *background,
                              const GrlColor *foreground,
                              gfloat          font_size);

G_END_DECLS
