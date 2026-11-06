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
 * Returns: (transfer full): a new #LrgSeparator
 */
LRG_AVAILABLE_IN_ALL
LrgSeparator * lrg_separator_new (void);

G_END_DECLS
