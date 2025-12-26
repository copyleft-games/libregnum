/* lrg-bt-composite.h
 *
 * Copyright 2024 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Composite nodes for behavior trees (sequence, selector, parallel).
 */

#ifndef LRG_BT_COMPOSITE_H
#define LRG_BT_COMPOSITE_H

#include <glib-object.h>
#include "lrg-version.h"
#include "lrg-bt-node.h"
#include "lrg-enums.h"

G_BEGIN_DECLS

#define LRG_TYPE_BT_COMPOSITE (lrg_bt_composite_get_type ())

G_DECLARE_DERIVABLE_TYPE (LrgBTComposite, lrg_bt_composite, LRG, BT_COMPOSITE, LrgBTNode)

/**
 * LrgBTCompositeClass:
 *
 * The class structure for #LrgBTComposite.
 */
struct _LrgBTCompositeClass
{
    LrgBTNodeClass parent_class;

    /* Reserved for future expansion */
    gpointer _reserved[8];
};

/**
 * lrg_bt_composite_add_child:
 * @self: an #LrgBTComposite
 * @child: (transfer none): The child node to add
 *
 * Adds a child node to the composite.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_bt_composite_add_child       (LrgBTComposite *self,
                                                      LrgBTNode      *child);

/**
 * lrg_bt_composite_remove_child:
 * @self: an #LrgBTComposite
 * @child: The child node to remove
 *
 * Removes a child node from the composite.
 *
 * Returns: %TRUE if the child was removed
 */
LRG_AVAILABLE_IN_ALL
gboolean            lrg_bt_composite_remove_child    (LrgBTComposite *self,
                                                      LrgBTNode      *child);

/**
 * lrg_bt_composite_get_children:
 * @self: an #LrgBTComposite
 *
 * Gets the list of child nodes.
 *
 * Returns: (transfer none) (element-type LrgBTNode): The child nodes
 */
LRG_AVAILABLE_IN_ALL
GPtrArray *         lrg_bt_composite_get_children    (LrgBTComposite *self);

/**
 * lrg_bt_composite_get_child_count:
 * @self: an #LrgBTComposite
 *
 * Gets the number of child nodes.
 *
 * Returns: Number of children
 */
LRG_AVAILABLE_IN_ALL
guint               lrg_bt_composite_get_child_count (LrgBTComposite *self);

/**
 * lrg_bt_composite_clear_children:
 * @self: an #LrgBTComposite
 *
 * Removes all child nodes.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_bt_composite_clear_children  (LrgBTComposite *self);

/* ==========================================================================
 * Sequence Node
 * ========================================================================== */

#define LRG_TYPE_BT_SEQUENCE (lrg_bt_sequence_get_type ())

G_DECLARE_FINAL_TYPE (LrgBTSequence, lrg_bt_sequence, LRG, BT_SEQUENCE, LrgBTComposite)

/**
 * lrg_bt_sequence_new:
 *
 * Creates a new sequence node.
 * A sequence runs children in order until one fails.
 *
 * Returns: (transfer full): A new #LrgBTSequence
 */
LRG_AVAILABLE_IN_ALL
LrgBTSequence *     lrg_bt_sequence_new              (void);

/* ==========================================================================
 * Selector Node
 * ========================================================================== */

#define LRG_TYPE_BT_SELECTOR (lrg_bt_selector_get_type ())

G_DECLARE_FINAL_TYPE (LrgBTSelector, lrg_bt_selector, LRG, BT_SELECTOR, LrgBTComposite)

/**
 * lrg_bt_selector_new:
 *
 * Creates a new selector node.
 * A selector runs children in order until one succeeds.
 *
 * Returns: (transfer full): A new #LrgBTSelector
 */
LRG_AVAILABLE_IN_ALL
LrgBTSelector *     lrg_bt_selector_new              (void);

/* ==========================================================================
 * Parallel Node
 * ========================================================================== */

#define LRG_TYPE_BT_PARALLEL (lrg_bt_parallel_get_type ())

G_DECLARE_FINAL_TYPE (LrgBTParallel, lrg_bt_parallel, LRG, BT_PARALLEL, LrgBTComposite)

/**
 * lrg_bt_parallel_new:
 * @policy: The success policy
 *
 * Creates a new parallel node.
 * A parallel runs all children simultaneously.
 *
 * Returns: (transfer full): A new #LrgBTParallel
 */
LRG_AVAILABLE_IN_ALL
LrgBTParallel *     lrg_bt_parallel_new              (LrgBTParallelPolicy policy);

/**
 * lrg_bt_parallel_get_policy:
 * @self: an #LrgBTParallel
 *
 * Gets the success policy.
 *
 * Returns: The policy
 */
LRG_AVAILABLE_IN_ALL
LrgBTParallelPolicy lrg_bt_parallel_get_policy       (LrgBTParallel *self);

/**
 * lrg_bt_parallel_set_policy:
 * @self: an #LrgBTParallel
 * @policy: The success policy
 *
 * Sets the success policy.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_bt_parallel_set_policy       (LrgBTParallel       *self,
                                                      LrgBTParallelPolicy  policy);

G_END_DECLS

#endif /* LRG_BT_COMPOSITE_H */
