/* lrg-bt-leaf.c
 *
 * Copyright 2024 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Leaf nodes implementation.
 */

#include "lrg-bt-leaf.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_AI
#include "lrg-log.h"

/* ==========================================================================
 * LrgBTAction - Performs an action
 * ========================================================================== */

struct _LrgBTAction
{
    LrgBTNode        parent_instance;

    LrgBTActionFunc  func;
    gpointer         user_data;
    GDestroyNotify   destroy;
};

#pragma GCC visibility push(default)
G_DEFINE_FINAL_TYPE (LrgBTAction, lrg_bt_action, LRG_TYPE_BT_NODE)
#pragma GCC visibility pop

static void
lrg_bt_action_finalize (GObject *object)
{
    LrgBTAction *self = LRG_BT_ACTION (object);

    if (self->destroy && self->user_data)
        self->destroy (self->user_data);

    G_OBJECT_CLASS (lrg_bt_action_parent_class)->finalize (object);
}

static LrgBTStatus
lrg_bt_action_tick (LrgBTNode     *node,
                    LrgBlackboard *blackboard,
                    gfloat         delta_time)
{
    LrgBTAction *self = LRG_BT_ACTION (node);

    if (self->func == NULL)
        return LRG_BT_STATUS_FAILURE;

    return self->func (blackboard, delta_time, self->user_data);
}

static void
lrg_bt_action_class_init (LrgBTActionClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    LrgBTNodeClass *node_class = LRG_BT_NODE_CLASS (klass);

    object_class->finalize = lrg_bt_action_finalize;

    node_class->tick = lrg_bt_action_tick;
}

static void
lrg_bt_action_init (LrgBTAction *self)
{
}

/**
 * lrg_bt_action_new:
 * @func: (scope notified): The action function
 * @user_data: (closure): User data for the function
 * @destroy: (nullable): Destroy function for user data
 *
 * Creates a new action node.
 *
 * Returns: (transfer full): A new #LrgBTAction
 */
LrgBTAction *
lrg_bt_action_new (LrgBTActionFunc  func,
                   gpointer         user_data,
                   GDestroyNotify   destroy)
{
    LrgBTAction *self;

    g_return_val_if_fail (func != NULL, NULL);

    self = g_object_new (LRG_TYPE_BT_ACTION, NULL);
    self->func = func;
    self->user_data = user_data;
    self->destroy = destroy;

    return self;
}

/**
 * lrg_bt_action_new_simple:
 * @func: (scope notified): The action function
 *
 * Creates a new action node without user data.
 *
 * Returns: (transfer full): A new #LrgBTAction
 */
LrgBTAction *
lrg_bt_action_new_simple (LrgBTActionFunc func)
{
    return lrg_bt_action_new (func, NULL, NULL);
}

/* ==========================================================================
 * LrgBTCondition - Checks a condition
 * ========================================================================== */

struct _LrgBTCondition
{
    LrgBTNode           parent_instance;

    LrgBTConditionFunc  func;
    gpointer            user_data;
    GDestroyNotify      destroy;
};

#pragma GCC visibility push(default)
G_DEFINE_FINAL_TYPE (LrgBTCondition, lrg_bt_condition, LRG_TYPE_BT_NODE)
#pragma GCC visibility pop

static void
lrg_bt_condition_finalize (GObject *object)
{
    LrgBTCondition *self = LRG_BT_CONDITION (object);

    if (self->destroy && self->user_data)
        self->destroy (self->user_data);

    G_OBJECT_CLASS (lrg_bt_condition_parent_class)->finalize (object);
}

static LrgBTStatus
lrg_bt_condition_tick (LrgBTNode     *node,
                       LrgBlackboard *blackboard,
                       gfloat         delta_time)
{
    LrgBTCondition *self = LRG_BT_CONDITION (node);

    (void) delta_time;  /* Unused for conditions */

    if (self->func == NULL)
        return LRG_BT_STATUS_FAILURE;

    return self->func (blackboard, self->user_data)
               ? LRG_BT_STATUS_SUCCESS
               : LRG_BT_STATUS_FAILURE;
}

static void
lrg_bt_condition_class_init (LrgBTConditionClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    LrgBTNodeClass *node_class = LRG_BT_NODE_CLASS (klass);

    object_class->finalize = lrg_bt_condition_finalize;

    node_class->tick = lrg_bt_condition_tick;
}

static void
lrg_bt_condition_init (LrgBTCondition *self)
{
}

/**
 * lrg_bt_condition_new:
 * @func: (scope notified): The condition function
 * @user_data: (closure): User data for the function
 * @destroy: (nullable): Destroy function for user data
 *
 * Creates a new condition node.
 *
 * Returns: (transfer full): A new #LrgBTCondition
 */
LrgBTCondition *
lrg_bt_condition_new (LrgBTConditionFunc  func,
                      gpointer            user_data,
                      GDestroyNotify      destroy)
{
    LrgBTCondition *self;

    g_return_val_if_fail (func != NULL, NULL);

    self = g_object_new (LRG_TYPE_BT_CONDITION, NULL);
    self->func = func;
    self->user_data = user_data;
    self->destroy = destroy;

    return self;
}

/**
 * lrg_bt_condition_new_simple:
 * @func: (scope notified): The condition function
 *
 * Creates a new condition node without user data.
 *
 * Returns: (transfer full): A new #LrgBTCondition
 */
LrgBTCondition *
lrg_bt_condition_new_simple (LrgBTConditionFunc func)
{
    return lrg_bt_condition_new (func, NULL, NULL);
}

/* ==========================================================================
 * LrgBTWait - Waits for a duration
 * ========================================================================== */

struct _LrgBTWait
{
    LrgBTNode parent_instance;

    gfloat    duration;
    gfloat    elapsed;
};

#pragma GCC visibility push(default)
G_DEFINE_FINAL_TYPE (LrgBTWait, lrg_bt_wait, LRG_TYPE_BT_NODE)
#pragma GCC visibility pop

static LrgBTStatus
lrg_bt_wait_tick (LrgBTNode     *node,
                  LrgBlackboard *blackboard,
                  gfloat         delta_time)
{
    LrgBTWait *self = LRG_BT_WAIT (node);

    (void) blackboard;  /* Unused */

    self->elapsed += delta_time;

    if (self->elapsed >= self->duration)
    {
        self->elapsed = 0.0f;
        return LRG_BT_STATUS_SUCCESS;
    }

    return LRG_BT_STATUS_RUNNING;
}

static void
lrg_bt_wait_real_reset (LrgBTNode *node)
{
    LrgBTWait *self = LRG_BT_WAIT (node);

    self->elapsed = 0.0f;

    LRG_BT_NODE_CLASS (lrg_bt_wait_parent_class)->reset (node);
}

static void
lrg_bt_wait_class_init (LrgBTWaitClass *klass)
{
    LrgBTNodeClass *node_class = LRG_BT_NODE_CLASS (klass);

    node_class->tick = lrg_bt_wait_tick;
    node_class->reset = lrg_bt_wait_real_reset;
}

static void
lrg_bt_wait_init (LrgBTWait *self)
{
    self->duration = 1.0f;
    self->elapsed = 0.0f;
}

/**
 * lrg_bt_wait_new:
 * @duration: Duration to wait in seconds
 *
 * Creates a new wait node.
 *
 * Returns: (transfer full): A new #LrgBTWait
 */
LrgBTWait *
lrg_bt_wait_new (gfloat duration)
{
    LrgBTWait *self = g_object_new (LRG_TYPE_BT_WAIT, NULL);
    self->duration = duration;
    return self;
}

/**
 * lrg_bt_wait_get_duration:
 * @self: an #LrgBTWait
 *
 * Gets the wait duration.
 *
 * Returns: Duration in seconds
 */
gfloat
lrg_bt_wait_get_duration (LrgBTWait *self)
{
    g_return_val_if_fail (LRG_IS_BT_WAIT (self), 0.0f);

    return self->duration;
}

/**
 * lrg_bt_wait_set_duration:
 * @self: an #LrgBTWait
 * @duration: Duration in seconds
 *
 * Sets the wait duration.
 */
void
lrg_bt_wait_set_duration (LrgBTWait *self,
                          gfloat     duration)
{
    g_return_if_fail (LRG_IS_BT_WAIT (self));

    self->duration = duration;
}
