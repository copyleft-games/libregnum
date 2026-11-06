/* lrg-splitter.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgSplitter: Two panes separated by a draggable divider.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "lrg-container.h"

G_BEGIN_DECLS

#define LRG_TYPE_SPLITTER (lrg_splitter_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgSplitter, lrg_splitter, LRG, SPLITTER, LrgContainer)

/**
 * lrg_splitter_new:
 *
 * Returns: (transfer full): a new #LrgSplitter
 */
LRG_AVAILABLE_IN_ALL
LrgSplitter * lrg_splitter_new (void);

G_END_DECLS
