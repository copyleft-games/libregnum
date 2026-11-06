/* lrg-list-view.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgListView: A virtualized, scrollable list of rows built from a GListModel.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "lrg-container.h"

G_BEGIN_DECLS

#define LRG_TYPE_LIST_VIEW (lrg_list_view_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgListView, lrg_list_view, LRG, LIST_VIEW, LrgContainer)

/**
 * lrg_list_view_new:
 *
 * Returns: (transfer full): a new #LrgListView
 */
LRG_AVAILABLE_IN_ALL
LrgListView * lrg_list_view_new (void);

G_END_DECLS
