/* lrg-hbox.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Horizontal box layout container.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "lrg-container.h"

G_BEGIN_DECLS

#define LRG_TYPE_HBOX (lrg_hbox_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgHBox, lrg_hbox, LRG, HBOX, LrgContainer)

/**
 * lrg_hbox_new:
 *
 * Creates a new horizontal box container.
 *
 * Returns: (transfer full): A new #LrgHBox
 */
LRG_AVAILABLE_IN_ALL
LrgHBox * lrg_hbox_new (void);

/**
 * lrg_hbox_get_homogeneous:
 * @self: an #LrgHBox
 *
 * Gets whether children are given equal widths.
 *
 * Returns: %TRUE if homogeneous
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_hbox_get_homogeneous (LrgHBox *self);

/**
 * lrg_hbox_set_homogeneous:
 * @self: an #LrgHBox
 * @homogeneous: whether to use equal widths
 *
 * Sets whether children are given equal widths.
 */
LRG_AVAILABLE_IN_ALL
void lrg_hbox_set_homogeneous (LrgHBox  *self,
                               gboolean  homogeneous);

G_END_DECLS
