/* lrg-expander.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgExpander: A header that reveals or hides its content with a spring.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "lrg-container.h"

G_BEGIN_DECLS

#define LRG_TYPE_EXPANDER (lrg_expander_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgExpander, lrg_expander, LRG, EXPANDER, LrgContainer)

/**
 * lrg_expander_new:
 * @title: (nullable): header text
 *
 * Creates a collapsed expander without content.
 *
 * Returns: (transfer full): a new #LrgExpander
 */
LRG_AVAILABLE_IN_ALL
LrgExpander * lrg_expander_new (const gchar *title);

/**
 * lrg_expander_get_title:
 * @self: an #LrgExpander
 *
 * Returns: (transfer none): the header text
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_expander_get_title (LrgExpander *self);

/**
 * lrg_expander_set_title:
 * @self: an #LrgExpander
 * @title: (nullable): header text
 */
LRG_AVAILABLE_IN_ALL
void lrg_expander_set_title (LrgExpander *self,
                             const gchar *title);

/**
 * lrg_expander_set_child:
 * @self: an #LrgExpander
 * @child: (nullable): the content revealed when expanded
 *
 * Replaces the content. The expander shows and hides @child itself
 * (it is hidden while fully collapsed).
 */
LRG_AVAILABLE_IN_ALL
void lrg_expander_set_child (LrgExpander *self,
                             LrgWidget   *child);

/**
 * lrg_expander_get_child:
 * @self: an #LrgExpander
 *
 * Returns: (transfer none) (nullable): the content
 */
LRG_AVAILABLE_IN_ALL
LrgWidget * lrg_expander_get_child (LrgExpander *self);

/**
 * lrg_expander_get_expanded:
 * @self: an #LrgExpander
 *
 * Returns: %TRUE when expanded (or expanding)
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_expander_get_expanded (LrgExpander *self);

/**
 * lrg_expander_set_expanded:
 * @self: an #LrgExpander
 * @expanded: reveal the content
 *
 * Opens or closes the expander. The revealed height and the arrow
 * follow with a spring (instantly before the first frame, or with
 * %LRG_MOTION_NONE).
 */
LRG_AVAILABLE_IN_ALL
void lrg_expander_set_expanded (LrgExpander *self,
                                gboolean     expanded);

/**
 * lrg_expander_toggle:
 * @self: an #LrgExpander
 *
 * Flips #LrgExpander:expanded.
 */
LRG_AVAILABLE_IN_ALL
void lrg_expander_toggle (LrgExpander *self);

/**
 * lrg_expander_get_reveal:
 * @self: an #LrgExpander
 *
 * Returns: the revealed share of the content, 0 (closed) .. 1 (open);
 *   may overshoot slightly with bouncy motion
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_expander_get_reveal (LrgExpander *self);

/**
 * lrg_expander_get_header:
 * @self: an #LrgExpander
 *
 * The header is a focusable child widget: keyboard and gamepad focus
 * land on it, and Enter / Space / gamepad A toggle the expander.
 *
 * Returns: (transfer none): the header widget
 */
LRG_AVAILABLE_IN_ALL
LrgWidget * lrg_expander_get_header (LrgExpander *self);

G_END_DECLS
