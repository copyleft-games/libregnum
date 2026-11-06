/* lrg-separator.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgSeparator: A thin horizontal or vertical divider line.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "lrg-widget.h"

G_BEGIN_DECLS

#define LRG_TYPE_SEPARATOR (lrg_separator_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgSeparator, lrg_separator, LRG, SEPARATOR, LrgWidget)

/**
 * lrg_separator_new:
 *
 * Creates a horizontal separator. In a vertical box it stretches across
 * the box's width.
 *
 * Returns: (transfer full): a new #LrgSeparator
 */
LRG_AVAILABLE_IN_ALL
LrgSeparator * lrg_separator_new (void);

/**
 * lrg_separator_new_with_orientation:
 * @orientation: direction of the line
 *
 * Returns: (transfer full): a new #LrgSeparator
 */
LRG_AVAILABLE_IN_ALL
LrgSeparator * lrg_separator_new_with_orientation (LrgOrientation orientation);

/**
 * lrg_separator_get_orientation:
 * @self: an #LrgSeparator
 *
 * Returns: the direction of the line
 */
LRG_AVAILABLE_IN_ALL
LrgOrientation lrg_separator_get_orientation (LrgSeparator *self);

/**
 * lrg_separator_set_orientation:
 * @self: an #LrgSeparator
 * @orientation: %LRG_ORIENTATION_HORIZONTAL (a line across, the
 *   default) or %LRG_ORIENTATION_VERTICAL (a line down)
 */
LRG_AVAILABLE_IN_ALL
void lrg_separator_set_orientation (LrgSeparator   *self,
                                    LrgOrientation  orientation);

G_END_DECLS
