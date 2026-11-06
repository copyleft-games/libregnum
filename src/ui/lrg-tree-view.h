/* lrg-tree-view.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgTreeView: A virtualized expandable tree of rows.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "lrg-container.h"

G_BEGIN_DECLS

#define LRG_TYPE_TREE_VIEW (lrg_tree_view_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgTreeView, lrg_tree_view, LRG, TREE_VIEW, LrgContainer)

/**
 * lrg_tree_view_new:
 *
 * Returns: (transfer full): a new #LrgTreeView
 */
LRG_AVAILABLE_IN_ALL
LrgTreeView * lrg_tree_view_new (void);

G_END_DECLS
