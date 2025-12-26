/* lrg-bt-composite.c
 *
 * Copyright 2024 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Composite nodes implementation.
 */

#include "lrg-bt-composite.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_AI
#include "lrg-log.h"

/* ==========================================================================
 * LrgBTComposite - Base composite class
 * ========================================================================== */

typedef struct
{
    GPtrArray *children;
    guint      current_child;
} LrgBTCompositePrivate;

#pragma GCC visibility push(default)
G_DEFINE_TYPE_WITH_PRIVATE (LrgBTComposite, lrg_bt_composite, LRG_TYPE_BT_NODE)
#pragma GCC visibility pop

static void
lrg_bt_composite_finalize (GObject *object)
{
    LrgBTComposite *self = LRG_BT_COMPOSITE (object);
    LrgBTCompositePrivate *priv = lrg_bt_composite_get_instance_private (self);

    g_clear_pointer (&priv->children, g_ptr_array_unref);

    G_OBJECT_CLASS (lrg_bt_composite_parent_class)->finalize (object);
}

static void
lrg_bt_composite_real_reset (LrgBTNode *node)
{
    LrgBTComposite *self = LRG_BT_COMPOSITE (node);
    LrgBTCompositePrivate *priv = lrg_bt_composite_get_instance_private (self);
    guint i;

    /* Reset current child index */
    priv->current_child = 0;

    /* Reset all children */
    for (i = 0; i < priv->children->len; i++)
    {
        LrgBTNode *child = g_ptr_array_index (priv->children, i);
        lrg_bt_node_reset (child);
    }

    /* Chain up */
    LRG_BT_NODE_CLASS (lrg_bt_composite_parent_class)->reset (node);
}

static void
lrg_bt_composite_real_abort (LrgBTNode *node)
{
    LrgBTComposite *self = LRG_BT_COMPOSITE (node);
    LrgBTCompositePrivate *priv = lrg_bt_composite_get_instance_private (self);
    guint i;

    /* Abort all running children */
    for (i = 0; i < priv->children->len; i++)
    {
        LrgBTNode *child = g_ptr_array_index (priv->children, i);
        if (lrg_bt_node_is_running (child))
            lrg_bt_node_abort (child);
    }

    /* Chain up */
    LRG_BT_NODE_CLASS (lrg_bt_composite_parent_class)->abort (node);
}

static void
lrg_bt_composite_class_init (LrgBTCompositeClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    LrgBTNodeClass *node_class = LRG_BT_NODE_CLASS (klass);

    object_class->finalize = lrg_bt_composite_finalize;

    node_class->reset = lrg_bt_composite_real_reset;
    node_class->abort = lrg_bt_composite_real_abort;
}

static void
lrg_bt_composite_init (LrgBTComposite *self)
{
    LrgBTCompositePrivate *priv = lrg_bt_composite_get_instance_private (self);

    priv->children = g_ptr_array_new_with_free_func (g_object_unref);
    priv->current_child = 0;
}

/**
 * lrg_bt_composite_add_child:
 * @self: an #LrgBTComposite
 * @child: (transfer none): The child node to add
 *
 * Adds a child node to the composite.
 */
void
lrg_bt_composite_add_child (LrgBTComposite *self,
                            LrgBTNode      *child)
{
    LrgBTCompositePrivate *priv;

    g_return_if_fail (LRG_IS_BT_COMPOSITE (self));
    g_return_if_fail (LRG_IS_BT_NODE (child));

    priv = lrg_bt_composite_get_instance_private (self);
    g_ptr_array_add (priv->children, g_object_ref (child));
}

/**
 * lrg_bt_composite_remove_child:
 * @self: an #LrgBTComposite
 * @child: The child node to remove
 *
 * Removes a child node from the composite.
 *
 * Returns: %TRUE if the child was removed
 */
gboolean
lrg_bt_composite_remove_child (LrgBTComposite *self,
                               LrgBTNode      *child)
{
    LrgBTCompositePrivate *priv;

    g_return_val_if_fail (LRG_IS_BT_COMPOSITE (self), FALSE);
    g_return_val_if_fail (LRG_IS_BT_NODE (child), FALSE);

    priv = lrg_bt_composite_get_instance_private (self);
    return g_ptr_array_remove (priv->children, child);
}

/**
 * lrg_bt_composite_get_children:
 * @self: an #LrgBTComposite
 *
 * Gets the list of child nodes.
 *
 * Returns: (transfer none) (element-type LrgBTNode): The child nodes
 */
GPtrArray *
lrg_bt_composite_get_children (LrgBTComposite *self)
{
    LrgBTCompositePrivate *priv;

    g_return_val_if_fail (LRG_IS_BT_COMPOSITE (self), NULL);

    priv = lrg_bt_composite_get_instance_private (self);
    return priv->children;
}

/**
 * lrg_bt_composite_get_child_count:
 * @self: an #LrgBTComposite
 *
 * Gets the number of child nodes.
 *
 * Returns: Number of children
 */
guint
lrg_bt_composite_get_child_count (LrgBTComposite *self)
{
    LrgBTCompositePrivate *priv;

    g_return_val_if_fail (LRG_IS_BT_COMPOSITE (self), 0);

    priv = lrg_bt_composite_get_instance_private (self);
    return priv->children->len;
}

/**
 * lrg_bt_composite_clear_children:
 * @self: an #LrgBTComposite
 *
 * Removes all child nodes.
 */
void
lrg_bt_composite_clear_children (LrgBTComposite *self)
{
    LrgBTCompositePrivate *priv;

    g_return_if_fail (LRG_IS_BT_COMPOSITE (self));

    priv = lrg_bt_composite_get_instance_private (self);
    g_ptr_array_set_size (priv->children, 0);
    priv->current_child = 0;
}

/* ==========================================================================
 * LrgBTSequence - Runs children until one fails
 * ========================================================================== */

struct _LrgBTSequence
{
    LrgBTComposite parent_instance;
};

#pragma GCC visibility push(default)
G_DEFINE_FINAL_TYPE (LrgBTSequence, lrg_bt_sequence, LRG_TYPE_BT_COMPOSITE)
#pragma GCC visibility pop

static LrgBTStatus
lrg_bt_sequence_tick (LrgBTNode     *node,
                      LrgBlackboard *blackboard,
                      gfloat         delta_time)
{
    LrgBTComposite *composite = LRG_BT_COMPOSITE (node);
    LrgBTCompositePrivate *priv = lrg_bt_composite_get_instance_private (composite);
    GPtrArray *children = priv->children;

    while (priv->current_child < children->len)
    {
        LrgBTNode *child = g_ptr_array_index (children, priv->current_child);
        LrgBTStatus status = lrg_bt_node_tick (child, blackboard, delta_time);

        switch (status)
        {
        case LRG_BT_STATUS_RUNNING:
            return LRG_BT_STATUS_RUNNING;

        case LRG_BT_STATUS_FAILURE:
            priv->current_child = 0;
            return LRG_BT_STATUS_FAILURE;

        case LRG_BT_STATUS_SUCCESS:
            priv->current_child++;
            break;

        default:
            break;
        }
    }

    /* All children succeeded */
    priv->current_child = 0;
    return LRG_BT_STATUS_SUCCESS;
}

static void
lrg_bt_sequence_class_init (LrgBTSequenceClass *klass)
{
    LrgBTNodeClass *node_class = LRG_BT_NODE_CLASS (klass);

    node_class->tick = lrg_bt_sequence_tick;
}

static void
lrg_bt_sequence_init (LrgBTSequence *self)
{
}

/**
 * lrg_bt_sequence_new:
 *
 * Creates a new sequence node.
 * A sequence runs children in order until one fails.
 *
 * Returns: (transfer full): A new #LrgBTSequence
 */
LrgBTSequence *
lrg_bt_sequence_new (void)
{
    return g_object_new (LRG_TYPE_BT_SEQUENCE, NULL);
}

/* ==========================================================================
 * LrgBTSelector - Runs children until one succeeds
 * ========================================================================== */

struct _LrgBTSelector
{
    LrgBTComposite parent_instance;
};

#pragma GCC visibility push(default)
G_DEFINE_FINAL_TYPE (LrgBTSelector, lrg_bt_selector, LRG_TYPE_BT_COMPOSITE)
#pragma GCC visibility pop

static LrgBTStatus
lrg_bt_selector_tick (LrgBTNode     *node,
                      LrgBlackboard *blackboard,
                      gfloat         delta_time)
{
    LrgBTComposite *composite = LRG_BT_COMPOSITE (node);
    LrgBTCompositePrivate *priv = lrg_bt_composite_get_instance_private (composite);
    GPtrArray *children = priv->children;

    while (priv->current_child < children->len)
    {
        LrgBTNode *child = g_ptr_array_index (children, priv->current_child);
        LrgBTStatus status = lrg_bt_node_tick (child, blackboard, delta_time);

        switch (status)
        {
        case LRG_BT_STATUS_RUNNING:
            return LRG_BT_STATUS_RUNNING;

        case LRG_BT_STATUS_SUCCESS:
            priv->current_child = 0;
            return LRG_BT_STATUS_SUCCESS;

        case LRG_BT_STATUS_FAILURE:
            priv->current_child++;
            break;

        default:
            break;
        }
    }

    /* All children failed */
    priv->current_child = 0;
    return LRG_BT_STATUS_FAILURE;
}

static void
lrg_bt_selector_class_init (LrgBTSelectorClass *klass)
{
    LrgBTNodeClass *node_class = LRG_BT_NODE_CLASS (klass);

    node_class->tick = lrg_bt_selector_tick;
}

static void
lrg_bt_selector_init (LrgBTSelector *self)
{
}

/**
 * lrg_bt_selector_new:
 *
 * Creates a new selector node.
 * A selector runs children in order until one succeeds.
 *
 * Returns: (transfer full): A new #LrgBTSelector
 */
LrgBTSelector *
lrg_bt_selector_new (void)
{
    return g_object_new (LRG_TYPE_BT_SELECTOR, NULL);
}

/* ==========================================================================
 * LrgBTParallel - Runs all children simultaneously
 * ========================================================================== */

struct _LrgBTParallel
{
    LrgBTComposite       parent_instance;

    LrgBTParallelPolicy  policy;
};

#pragma GCC visibility push(default)
G_DEFINE_FINAL_TYPE (LrgBTParallel, lrg_bt_parallel, LRG_TYPE_BT_COMPOSITE)
#pragma GCC visibility pop

static LrgBTStatus
lrg_bt_parallel_tick (LrgBTNode     *node,
                      LrgBlackboard *blackboard,
                      gfloat         delta_time)
{
    LrgBTParallel *self = LRG_BT_PARALLEL (node);
    LrgBTComposite *composite = LRG_BT_COMPOSITE (node);
    LrgBTCompositePrivate *priv = lrg_bt_composite_get_instance_private (composite);
    GPtrArray *children = priv->children;
    guint i;
    guint success_count = 0;
    guint failure_count = 0;
    guint running_count = 0;

    if (children->len == 0)
        return LRG_BT_STATUS_SUCCESS;

    /* Tick all children */
    for (i = 0; i < children->len; i++)
    {
        LrgBTNode *child = g_ptr_array_index (children, i);
        LrgBTStatus status = lrg_bt_node_tick (child, blackboard, delta_time);

        switch (status)
        {
        case LRG_BT_STATUS_SUCCESS:
            success_count++;
            break;

        case LRG_BT_STATUS_FAILURE:
            failure_count++;
            break;

        case LRG_BT_STATUS_RUNNING:
            running_count++;
            break;

        default:
            break;
        }
    }

    /* Apply policy */
    switch (self->policy)
    {
    case LRG_BT_PARALLEL_REQUIRE_ONE:
        if (success_count > 0)
            return LRG_BT_STATUS_SUCCESS;
        if (failure_count == children->len)
            return LRG_BT_STATUS_FAILURE;
        break;

    case LRG_BT_PARALLEL_REQUIRE_ALL:
        if (failure_count > 0)
            return LRG_BT_STATUS_FAILURE;
        if (success_count == children->len)
            return LRG_BT_STATUS_SUCCESS;
        break;
    }

    return LRG_BT_STATUS_RUNNING;
}

static void
lrg_bt_parallel_class_init (LrgBTParallelClass *klass)
{
    LrgBTNodeClass *node_class = LRG_BT_NODE_CLASS (klass);

    node_class->tick = lrg_bt_parallel_tick;
}

static void
lrg_bt_parallel_init (LrgBTParallel *self)
{
    self->policy = LRG_BT_PARALLEL_REQUIRE_ALL;
}

/**
 * lrg_bt_parallel_new:
 * @policy: The success policy
 *
 * Creates a new parallel node.
 * A parallel runs all children simultaneously.
 *
 * Returns: (transfer full): A new #LrgBTParallel
 */
LrgBTParallel *
lrg_bt_parallel_new (LrgBTParallelPolicy policy)
{
    LrgBTParallel *self = g_object_new (LRG_TYPE_BT_PARALLEL, NULL);
    self->policy = policy;
    return self;
}

/**
 * lrg_bt_parallel_get_policy:
 * @self: an #LrgBTParallel
 *
 * Gets the success policy.
 *
 * Returns: The policy
 */
LrgBTParallelPolicy
lrg_bt_parallel_get_policy (LrgBTParallel *self)
{
    g_return_val_if_fail (LRG_IS_BT_PARALLEL (self), LRG_BT_PARALLEL_REQUIRE_ALL);

    return self->policy;
}

/**
 * lrg_bt_parallel_set_policy:
 * @self: an #LrgBTParallel
 * @policy: The success policy
 *
 * Sets the success policy.
 */
void
lrg_bt_parallel_set_policy (LrgBTParallel       *self,
                            LrgBTParallelPolicy  policy)
{
    g_return_if_fail (LRG_IS_BT_PARALLEL (self));

    self->policy = policy;
}
