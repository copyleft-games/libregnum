/* lrg-color-gradient.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgColorGradient: (stub, being written)
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"


G_BEGIN_DECLS

#define LRG_TYPE_COLOR_GRADIENT (lrg_color_gradient_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgColorGradient, lrg_color_gradient, LRG, COLOR_GRADIENT, GObject)

/**
 * lrg_color_gradient_new:
 *
 * Returns: (transfer full): a new #LrgColorGradient
 */
LRG_AVAILABLE_IN_ALL
LrgColorGradient * lrg_color_gradient_new (void);

G_END_DECLS
