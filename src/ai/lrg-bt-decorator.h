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
#include "lrg-blackboard.h"

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

/* ==========================================================================
 * Cooldown - Refuses to run its child again for a while (0.9.0)
 * ========================================================================== */

#define LRG_TYPE_BT_COOLDOWN (lrg_bt_cooldown_get_type ())

/**
 * LrgBTCooldown:
 *
 * After its child completes (success or failure) the cooldown fails
 * without ticking the child until @seconds of tick time have passed. Tick
 * time is the delta of the ticks that reach the node, so a cooldown under
 * a branch that is not visited does not count down. `.rbt` spelling:
 * `type: cooldown` with `seconds:`.
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgBTCooldown, lrg_bt_cooldown, LRG, BT_COOLDOWN, LrgBTDecorator)

/**
 * lrg_bt_cooldown_new:
 * @child: (nullable) (transfer none): The child node
 * @seconds: the cooldown length
 *
 * Returns: (transfer full): A new #LrgBTCooldown
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
LrgBTCooldown *     lrg_bt_cooldown_new              (LrgBTNode *child,
                                                      gfloat     seconds);

/**
 * lrg_bt_cooldown_get_seconds:
 * @self: an #LrgBTCooldown
 *
 * Returns: the cooldown length in seconds
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
gfloat              lrg_bt_cooldown_get_seconds      (LrgBTCooldown *self);

/**
 * lrg_bt_cooldown_set_seconds:
 * @self: an #LrgBTCooldown
 * @seconds: the cooldown length (negative is clamped to 0)
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
void                lrg_bt_cooldown_set_seconds      (LrgBTCooldown *self,
                                                      gfloat         seconds);

/**
 * lrg_bt_cooldown_get_remaining:
 * @self: an #LrgBTCooldown
 *
 * Returns: the seconds left before the child may run again (0 when ready)
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
gfloat              lrg_bt_cooldown_get_remaining    (LrgBTCooldown *self);

/* ==========================================================================
 * TimeLimit - Fails a child that runs too long (0.9.0)
 * ========================================================================== */

#define LRG_TYPE_BT_TIME_LIMIT (lrg_bt_time_limit_get_type ())

/**
 * LrgBTTimeLimit:
 *
 * Ticks its child; when the child has been running for more than
 * @seconds of tick time the child is aborted and the node fails. `.rbt`
 * spelling: `type: time_limit` with `seconds:`.
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgBTTimeLimit, lrg_bt_time_limit, LRG, BT_TIME_LIMIT, LrgBTDecorator)

/**
 * lrg_bt_time_limit_new:
 * @child: (nullable) (transfer none): The child node
 * @seconds: the limit
 *
 * Returns: (transfer full): A new #LrgBTTimeLimit
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
LrgBTTimeLimit *    lrg_bt_time_limit_new            (LrgBTNode *child,
                                                      gfloat     seconds);

/**
 * lrg_bt_time_limit_get_seconds:
 * @self: an #LrgBTTimeLimit
 *
 * Returns: the limit in seconds
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
gfloat              lrg_bt_time_limit_get_seconds    (LrgBTTimeLimit *self);

/**
 * lrg_bt_time_limit_set_seconds:
 * @self: an #LrgBTTimeLimit
 * @seconds: the limit (negative is clamped to 0)
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
void                lrg_bt_time_limit_set_seconds    (LrgBTTimeLimit *self,
                                                      gfloat          seconds);

/* ==========================================================================
 * Guard - Runs its child only while a blackboard test holds (0.9.0)
 * ========================================================================== */

#define LRG_TYPE_BT_GUARD (lrg_bt_guard_get_type ())

/**
 * LrgBTGuard:
 *
 * Tests a blackboard key on every tick (lrg_blackboard_test()): while the
 * test holds the child is ticked and its status returned; when it fails a
 * running child is aborted and the guard fails. This makes a branch
 * reactive: it stops the moment its precondition changes. `.rbt`
 * spelling: `type: guard` with `key:`, `op:` and `value:`.
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgBTGuard, lrg_bt_guard, LRG, BT_GUARD, LrgBTDecorator)

/**
 * lrg_bt_guard_new:
 * @child: (nullable) (transfer none): The child node
 * @key: the blackboard key tested
 * @op: the comparison
 * @operand: (nullable): what the value is compared with (a floating
 *   reference is sunk)
 *
 * Returns: (transfer full): A new #LrgBTGuard
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
LrgBTGuard *        lrg_bt_guard_new                 (LrgBTNode       *child,
                                                      const gchar     *key,
                                                      LrgBlackboardOp  op,
                                                      GVariant        *operand);

/**
 * lrg_bt_guard_get_key:
 * @self: an #LrgBTGuard
 *
 * Returns: (transfer none): the tested key
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
const gchar *       lrg_bt_guard_get_key             (LrgBTGuard *self);

/**
 * lrg_bt_guard_get_op:
 * @self: an #LrgBTGuard
 *
 * Returns: the comparison
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
LrgBlackboardOp     lrg_bt_guard_get_op              (LrgBTGuard *self);

/**
 * lrg_bt_guard_get_operand:
 * @self: an #LrgBTGuard
 *
 * Returns: (transfer none) (nullable): the operand
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
GVariant *          lrg_bt_guard_get_operand         (LrgBTGuard *self);

G_END_DECLS

#endif /* LRG_BT_DECORATOR_H */
