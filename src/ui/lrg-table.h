/* lrg-table.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgTable: A virtualized data table with resizable, sortable columns.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "lrg-container.h"

G_BEGIN_DECLS

#define LRG_TYPE_TABLE (lrg_table_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgTable, lrg_table, LRG, TABLE, LrgContainer)

/**
 * lrg_table_new:
 *
 * Returns: (transfer full): a new #LrgTable
 */
LRG_AVAILABLE_IN_ALL
LrgTable * lrg_table_new (void);

G_END_DECLS
