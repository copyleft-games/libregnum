/* lrg-ssao.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgSsao: (stub, being written)
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"


G_BEGIN_DECLS

#define LRG_TYPE_SSAO (lrg_ssao_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgSsao, lrg_ssao, LRG, SSAO, GObject)

/**
 * lrg_ssao_new:
 *
 * Returns: (transfer full): a new #LrgSsao
 */
LRG_AVAILABLE_IN_ALL
LrgSsao * lrg_ssao_new (void);

G_END_DECLS
