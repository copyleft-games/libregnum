/* lrg-node-private.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Library-internal helpers of #LrgNode.
 */

#pragma once

#include <glib.h>

G_BEGIN_DECLS

/*
 * lrg_node_get_tree_serial:
 *
 * A process-wide counter bumped whenever any node is created, finalized,
 * gains or loses a child, or is renamed or given a new GUID. Callers that
 * derive data from a node tree's shape or names (the editor's node count
 * and hierarchy filter) cache it together with this value and recompute
 * when it differs. It is never 0, so 0 can mean "nothing cached".
 *
 * Returns: the current serial
 */
G_GNUC_INTERNAL guint lrg_node_get_tree_serial (void);

G_END_DECLS
