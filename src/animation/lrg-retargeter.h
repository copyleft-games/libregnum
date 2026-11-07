/* lrg-retargeter.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgRetargeter: (stub, being written)
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"


G_BEGIN_DECLS

#define LRG_TYPE_RETARGETER (lrg_retargeter_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgRetargeter, lrg_retargeter, LRG, RETARGETER, GObject)

/**
 * lrg_retargeter_new:
 *
 * Returns: (transfer full): a new #LrgRetargeter
 */
LRG_AVAILABLE_IN_ALL
LrgRetargeter * lrg_retargeter_new (void);

G_END_DECLS
