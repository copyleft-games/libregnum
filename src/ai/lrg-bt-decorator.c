/* lrg-bt-decorator.c
 *
 * Copyright 2024 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Decorator nodes implementation.
 */

#include "lrg-bt-decorator.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_AI
#include "lrg-log.h"

/* ==========================================================================
 * LrgBTDecorator - Base decorator class
 * ========================================================================== */

typedef struct
{
    LrgBTNode *child;
} LrgBTDecoratorPrivate;

#pragma GCC visibility push(default)
G_DEFINE_TYPE_WITH_PRIVATE (LrgBTDecorator, lrg_bt_decorator, LRG_TYPE_BT_NODE)
#pragma GCC visibility pop

static void
lrg_bt_decorator_finalize (GObject *object)
{
    LrgBTDecorator *self = LRG_BT_DECORATOR (object);
    LrgBTDecoratorPrivate *priv = lrg_bt_decorator_get_instance_private (self);

    g_clear_object (&priv->child);

    G_OBJECT_CLASS (lrg_bt_decorator_parent_class)->finalize (object);
}

static void
lrg_bt_decorator_real_reset (LrgBTNode *node)
{
    LrgBTDecorator *self = LRG_BT_DECORATOR (node);
    LrgBTDecoratorPrivate *priv = lrg_bt_decorator_get_instance_private (self);

    if (priv->child)
        lrg_bt_node_reset (priv->child);

    LRG_BT_NODE_CLASS (lrg_bt_decorator_parent_class)->reset (node);
}

static void
lrg_bt_decorator_real_abort (LrgBTNode *node)
{
    LrgBTDecorator *self = LRG_BT_DECORATOR (node);
    LrgBTDecoratorPrivate *priv = lrg_bt_decorator_get_instance_private (self);

    if (priv->child && lrg_bt_node_is_running (priv->child))
        lrg_bt_node_abort (priv->child);

    LRG_BT_NODE_CLASS (lrg_bt_decorator_parent_class)->abort (node);
}

static void
lrg_bt_decorator_class_init (LrgBTDecoratorClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    LrgBTNodeClass *node_class = LRG_BT_NODE_CLASS (klass);

    object_class->finalize = lrg_bt_decorator_finalize;

    node_class->reset = lrg_bt_decorator_real_reset;
    node_class->abort = lrg_bt_decorator_real_abort;
}

static void
lrg_bt_decorator_init (LrgBTDecorator *self)
{
}

/**
 * lrg_bt_decorator_get_child:
 * @self: an #LrgBTDecorator
 *
 * Gets the child node.
 *
 * Returns: (transfer none) (nullable): The child node
 */
LrgBTNode *
lrg_bt_decorator_get_child (LrgBTDecorator *self)
{
    LrgBTDecoratorPrivate *priv;

    g_return_val_if_fail (LRG_IS_BT_DECORATOR (self), NULL);

    priv = lrg_bt_decorator_get_instance_private (self);
    return priv->child;
}

/**
 * lrg_bt_decorator_set_child:
 * @self: an #LrgBTDecorator
 * @child: (nullable) (transfer none): The child node
 *
 * Sets the child node.
 */
void
lrg_bt_decorator_set_child (LrgBTDecorator *self,
                            LrgBTNode      *child)
{
    LrgBTDecoratorPrivate *priv;

    g_return_if_fail (LRG_IS_BT_DECORATOR (self));
    g_return_if_fail (child == NULL || LRG_IS_BT_NODE (child));

    priv = lrg_bt_decorator_get_instance_private (self);

    if (priv->child != child)
    {
        g_clear_object (&priv->child);
        priv->child = child ? g_object_ref (child) : NULL;
    }
}

/* ==========================================================================
 * LrgBTInverter - Inverts child result
 * ========================================================================== */

struct _LrgBTInverter
{
    LrgBTDecorator parent_instance;
};

#pragma GCC visibility push(default)
G_DEFINE_FINAL_TYPE (LrgBTInverter, lrg_bt_inverter, LRG_TYPE_BT_DECORATOR)
#pragma GCC visibility pop

static LrgBTStatus
lrg_bt_inverter_tick (LrgBTNode     *node,
                      LrgBlackboard *blackboard,
                      gfloat         delta_time)
{
    LrgBTDecorator *decorator = LRG_BT_DECORATOR (node);
    LrgBTDecoratorPrivate *priv = lrg_bt_decorator_get_instance_private (decorator);
    LrgBTStatus status;

    if (priv->child == NULL)
        return LRG_BT_STATUS_FAILURE;

    status = lrg_bt_node_tick (priv->child, blackboard, delta_time);

    switch (status)
    {
    case LRG_BT_STATUS_SUCCESS:
        return LRG_BT_STATUS_FAILURE;

    case LRG_BT_STATUS_FAILURE:
        return LRG_BT_STATUS_SUCCESS;

    default:
        return status;
    }
}

static void
lrg_bt_inverter_class_init (LrgBTInverterClass *klass)
{
    LrgBTNodeClass *node_class = LRG_BT_NODE_CLASS (klass);

    node_class->tick = lrg_bt_inverter_tick;
}

static void
lrg_bt_inverter_init (LrgBTInverter *self)
{
}

/**
 * lrg_bt_inverter_new:
 * @child: (nullable) (transfer none): The child node
 *
 * Creates a new inverter decorator.
 *
 * Returns: (transfer full): A new #LrgBTInverter
 */
LrgBTInverter *
lrg_bt_inverter_new (LrgBTNode *child)
{
    LrgBTInverter *self = g_object_new (LRG_TYPE_BT_INVERTER, NULL);

    if (child)
        lrg_bt_decorator_set_child (LRG_BT_DECORATOR (self), child);

    return self;
}

/* ==========================================================================
 * LrgBTRepeater - Repeats child N times
 * ========================================================================== */

struct _LrgBTRepeater
{
    LrgBTDecorator parent_instance;

    guint count;
    guint current;
};

#pragma GCC visibility push(default)
G_DEFINE_FINAL_TYPE (LrgBTRepeater, lrg_bt_repeater, LRG_TYPE_BT_DECORATOR)
#pragma GCC visibility pop

static LrgBTStatus
lrg_bt_repeater_tick (LrgBTNode     *node,
                      LrgBlackboard *blackboard,
                      gfloat         delta_time)
{
    LrgBTRepeater *self = LRG_BT_REPEATER (node);
    LrgBTDecorator *decorator = LRG_BT_DECORATOR (node);
    LrgBTDecoratorPrivate *priv = lrg_bt_decorator_get_instance_private (decorator);
    LrgBTStatus status;

    if (priv->child == NULL)
        return LRG_BT_STATUS_FAILURE;

    status = lrg_bt_node_tick (priv->child, blackboard, delta_time);

    if (status == LRG_BT_STATUS_RUNNING)
        return LRG_BT_STATUS_RUNNING;

    /* Child completed (success or failure) */
    self->current++;

    /* Check if we should repeat */
    if (self->count == 0 || self->current < self->count)
    {
        lrg_bt_node_reset (priv->child);
        return LRG_BT_STATUS_RUNNING;
    }

    /* Finished all repetitions */
    self->current = 0;
    return status;
}

static void
lrg_bt_repeater_real_reset (LrgBTNode *node)
{
    LrgBTRepeater *self = LRG_BT_REPEATER (node);

    self->current = 0;

    LRG_BT_NODE_CLASS (lrg_bt_repeater_parent_class)->reset (node);
}

static void
lrg_bt_repeater_class_init (LrgBTRepeaterClass *klass)
{
    LrgBTNodeClass *node_class = LRG_BT_NODE_CLASS (klass);

    node_class->tick = lrg_bt_repeater_tick;
    node_class->reset = lrg_bt_repeater_real_reset;
}

static void
lrg_bt_repeater_init (LrgBTRepeater *self)
{
    self->count = 1;
    self->current = 0;
}

/**
 * lrg_bt_repeater_new:
 * @child: (nullable) (transfer none): The child node
 * @count: Number of times to repeat (0 = infinite)
 *
 * Creates a new repeater decorator.
 *
 * Returns: (transfer full): A new #LrgBTRepeater
 */
LrgBTRepeater *
lrg_bt_repeater_new (LrgBTNode *child,
                     guint      count)
{
    LrgBTRepeater *self = g_object_new (LRG_TYPE_BT_REPEATER, NULL);

    self->count = count;

    if (child)
        lrg_bt_decorator_set_child (LRG_BT_DECORATOR (self), child);

    return self;
}

/**
 * lrg_bt_repeater_get_count:
 * @self: an #LrgBTRepeater
 *
 * Gets the repeat count.
 *
 * Returns: Repeat count (0 = infinite)
 */
guint
lrg_bt_repeater_get_count (LrgBTRepeater *self)
{
    g_return_val_if_fail (LRG_IS_BT_REPEATER (self), 0);

    return self->count;
}

/**
 * lrg_bt_repeater_set_count:
 * @self: an #LrgBTRepeater
 * @count: Repeat count (0 = infinite)
 *
 * Sets the repeat count.
 */
void
lrg_bt_repeater_set_count (LrgBTRepeater *self,
                           guint          count)
{
    g_return_if_fail (LRG_IS_BT_REPEATER (self));

    self->count = count;
}

/* ==========================================================================
 * LrgBTSucceeder - Always returns SUCCESS
 * ========================================================================== */

struct _LrgBTSucceeder
{
    LrgBTDecorator parent_instance;
};

#pragma GCC visibility push(default)
G_DEFINE_FINAL_TYPE (LrgBTSucceeder, lrg_bt_succeeder, LRG_TYPE_BT_DECORATOR)
#pragma GCC visibility pop

static LrgBTStatus
lrg_bt_succeeder_tick (LrgBTNode     *node,
                       LrgBlackboard *blackboard,
                       gfloat         delta_time)
{
    LrgBTDecorator *decorator = LRG_BT_DECORATOR (node);
    LrgBTDecoratorPrivate *priv = lrg_bt_decorator_get_instance_private (decorator);
    LrgBTStatus status;

    if (priv->child == NULL)
        return LRG_BT_STATUS_SUCCESS;

    status = lrg_bt_node_tick (priv->child, blackboard, delta_time);

    if (status == LRG_BT_STATUS_RUNNING)
        return LRG_BT_STATUS_RUNNING;

    return LRG_BT_STATUS_SUCCESS;
}

static void
lrg_bt_succeeder_class_init (LrgBTSucceederClass *klass)
{
    LrgBTNodeClass *node_class = LRG_BT_NODE_CLASS (klass);

    node_class->tick = lrg_bt_succeeder_tick;
}

static void
lrg_bt_succeeder_init (LrgBTSucceeder *self)
{
}

/**
 * lrg_bt_succeeder_new:
 * @child: (nullable) (transfer none): The child node
 *
 * Creates a new succeeder decorator.
 *
 * Returns: (transfer full): A new #LrgBTSucceeder
 */
LrgBTSucceeder *
lrg_bt_succeeder_new (LrgBTNode *child)
{
    LrgBTSucceeder *self = g_object_new (LRG_TYPE_BT_SUCCEEDER, NULL);

    if (child)
        lrg_bt_decorator_set_child (LRG_BT_DECORATOR (self), child);

    return self;
}

/* ==========================================================================
 * LrgBTFailer - Always returns FAILURE
 * ========================================================================== */

struct _LrgBTFailer
{
    LrgBTDecorator parent_instance;
};

#pragma GCC visibility push(default)
G_DEFINE_FINAL_TYPE (LrgBTFailer, lrg_bt_failer, LRG_TYPE_BT_DECORATOR)
#pragma GCC visibility pop

static LrgBTStatus
lrg_bt_failer_tick (LrgBTNode     *node,
                    LrgBlackboard *blackboard,
                    gfloat         delta_time)
{
    LrgBTDecorator *decorator = LRG_BT_DECORATOR (node);
    LrgBTDecoratorPrivate *priv = lrg_bt_decorator_get_instance_private (decorator);
    LrgBTStatus status;

    if (priv->child == NULL)
        return LRG_BT_STATUS_FAILURE;

    status = lrg_bt_node_tick (priv->child, blackboard, delta_time);

    if (status == LRG_BT_STATUS_RUNNING)
        return LRG_BT_STATUS_RUNNING;

    return LRG_BT_STATUS_FAILURE;
}

static void
lrg_bt_failer_class_init (LrgBTFailerClass *klass)
{
    LrgBTNodeClass *node_class = LRG_BT_NODE_CLASS (klass);

    node_class->tick = lrg_bt_failer_tick;
}

static void
lrg_bt_failer_init (LrgBTFailer *self)
{
}

/**
 * lrg_bt_failer_new:
 * @child: (nullable) (transfer none): The child node
 *
 * Creates a new failer decorator.
 *
 * Returns: (transfer full): A new #LrgBTFailer
 */
LrgBTFailer *
lrg_bt_failer_new (LrgBTNode *child)
{
    LrgBTFailer *self = g_object_new (LRG_TYPE_BT_FAILER, NULL);

    if (child)
        lrg_bt_decorator_set_child (LRG_BT_DECORATOR (self), child);

    return self;
}
