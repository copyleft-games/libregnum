/* lrg-bt-decorator.h
 *
 * Copyright 2024 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Decorator nodes for behavior trees.
 */

#ifndef LRG_BT_DECORATOR_H
#define LRG_BT_DECORATOR_H

#include <glib-object.h>
#include "lrg-version.h"
#include "lrg-bt-node.h"

G_BEGIN_DECLS

#define LRG_TYPE_BT_DECORATOR (lrg_bt_decorator_get_type ())

G_DECLARE_DERIVABLE_TYPE (LrgBTDecorator, lrg_bt_decorator, LRG, BT_DECORATOR, LrgBTNode)

/**
 * LrgBTDecoratorClass:
 *
 * The class structure for #LrgBTDecorator.
 */
struct _LrgBTDecoratorClass
{
    LrgBTNodeClass parent_class;

    /* Reserved for future expansion */
    gpointer _reserved[8];
};

/**
 * lrg_bt_decorator_get_child:
 * @self: an #LrgBTDecorator
 *
 * Gets the child node.
 *
 * Returns: (transfer none) (nullable): The child node
 */
LRG_AVAILABLE_IN_ALL
LrgBTNode *         lrg_bt_decorator_get_child       (LrgBTDecorator *self);

/**
 * lrg_bt_decorator_set_child:
 * @self: an #LrgBTDecorator
 * @child: (nullable) (transfer none): The child node
 *
 * Sets the child node.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_bt_decorator_set_child       (LrgBTDecorator *self,
                                                      LrgBTNode      *child);

/* ==========================================================================
 * Inverter - Inverts child result
 * ========================================================================== */

#define LRG_TYPE_BT_INVERTER (lrg_bt_inverter_get_type ())

G_DECLARE_FINAL_TYPE (LrgBTInverter, lrg_bt_inverter, LRG, BT_INVERTER, LrgBTDecorator)

/**
 * lrg_bt_inverter_new:
 * @child: (nullable) (transfer none): The child node
 *
 * Creates a new inverter decorator.
 * An inverter inverts SUCCESS to FAILURE and vice versa.
 *
 * Returns: (transfer full): A new #LrgBTInverter
 */
LRG_AVAILABLE_IN_ALL
LrgBTInverter *     lrg_bt_inverter_new              (LrgBTNode *child);

/* ==========================================================================
 * Repeater - Repeats child N times
 * ========================================================================== */

#define LRG_TYPE_BT_REPEATER (lrg_bt_repeater_get_type ())

G_DECLARE_FINAL_TYPE (LrgBTRepeater, lrg_bt_repeater, LRG, BT_REPEATER, LrgBTDecorator)

/**
 * lrg_bt_repeater_new:
 * @child: (nullable) (transfer none): The child node
 * @count: Number of times to repeat (0 = infinite)
 *
 * Creates a new repeater decorator.
 *
 * Returns: (transfer full): A new #LrgBTRepeater
 */
LRG_AVAILABLE_IN_ALL
LrgBTRepeater *     lrg_bt_repeater_new              (LrgBTNode *child,
                                                      guint      count);

/**
 * lrg_bt_repeater_get_count:
 * @self: an #LrgBTRepeater
 *
 * Gets the repeat count.
 *
 * Returns: Repeat count (0 = infinite)
 */
LRG_AVAILABLE_IN_ALL
guint               lrg_bt_repeater_get_count        (LrgBTRepeater *self);

/**
 * lrg_bt_repeater_set_count:
 * @self: an #LrgBTRepeater
 * @count: Repeat count (0 = infinite)
 *
 * Sets the repeat count.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_bt_repeater_set_count        (LrgBTRepeater *self,
                                                      guint          count);

/* ==========================================================================
 * Succeeder - Always returns SUCCESS
 * ========================================================================== */

#define LRG_TYPE_BT_SUCCEEDER (lrg_bt_succeeder_get_type ())

G_DECLARE_FINAL_TYPE (LrgBTSucceeder, lrg_bt_succeeder, LRG, BT_SUCCEEDER, LrgBTDecorator)

/**
 * lrg_bt_succeeder_new:
 * @child: (nullable) (transfer none): The child node
 *
 * Creates a new succeeder decorator.
 * A succeeder always returns SUCCESS regardless of child result.
 *
 * Returns: (transfer full): A new #LrgBTSucceeder
 */
LRG_AVAILABLE_IN_ALL
LrgBTSucceeder *    lrg_bt_succeeder_new             (LrgBTNode *child);

/* ==========================================================================
 * Failer - Always returns FAILURE
 * ========================================================================== */

#define LRG_TYPE_BT_FAILER (lrg_bt_failer_get_type ())

G_DECLARE_FINAL_TYPE (LrgBTFailer, lrg_bt_failer, LRG, BT_FAILER, LrgBTDecorator)

/**
 * lrg_bt_failer_new:
 * @child: (nullable) (transfer none): The child node
 *
 * Creates a new failer decorator.
 * A failer always returns FAILURE regardless of child result.
 *
 * Returns: (transfer full): A new #LrgBTFailer
 */
LRG_AVAILABLE_IN_ALL
LrgBTFailer *       lrg_bt_failer_new                (LrgBTNode *child);

G_END_DECLS

#endif /* LRG_BT_DECORATOR_H */
