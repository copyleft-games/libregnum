/* lrg-uv-unwrap.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgUvUnwrap: (stub, being written)
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"


G_BEGIN_DECLS

#define LRG_TYPE_UV_UNWRAP (lrg_uv_unwrap_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgUvUnwrap, lrg_uv_unwrap, LRG, UV_UNWRAP, GObject)

/**
 * lrg_uv_unwrap_new:
 *
 * Returns: (transfer full): a new #LrgUvUnwrap
 */
LRG_AVAILABLE_IN_ALL
LrgUvUnwrap * lrg_uv_unwrap_new (void);

G_END_DECLS
