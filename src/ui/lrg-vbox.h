/* lrg-vbox.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Vertical box layout container.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "lrg-container.h"

G_BEGIN_DECLS

#define LRG_TYPE_VBOX (lrg_vbox_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgVBox, lrg_vbox, LRG, VBOX, LrgContainer)

/**
 * lrg_vbox_new:
 *
 * Creates a new vertical box container.
 *
 * Returns: (transfer full): A new #LrgVBox
 */
LRG_AVAILABLE_IN_ALL
LrgVBox * lrg_vbox_new (void);

/**
 * lrg_vbox_get_homogeneous:
 * @self: an #LrgVBox
 *
 * Gets whether children are given equal heights.
 *
 * Returns: %TRUE if homogeneous
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_vbox_get_homogeneous (LrgVBox *self);

/**
 * lrg_vbox_set_homogeneous:
 * @self: an #LrgVBox
 * @homogeneous: whether to use equal heights
 *
 * Sets whether children are given equal heights.
 */
LRG_AVAILABLE_IN_ALL
void lrg_vbox_set_homogeneous (LrgVBox  *self,
                               gboolean  homogeneous);

G_END_DECLS
