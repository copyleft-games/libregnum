/* lrg-behavior-tree.c
 *
 * Copyright 2024 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Behavior tree implementation.
 */

#include "lrg-behavior-tree.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_AI
#include "lrg-log.h"

struct _LrgBehaviorTree
{
    GObject        parent_instance;

    LrgBTNode     *root;
    LrgBlackboard *blackboard;
    LrgBTStatus    status;
};

#pragma GCC visibility push(default)
G_DEFINE_FINAL_TYPE (LrgBehaviorTree, lrg_behavior_tree, G_TYPE_OBJECT)
#pragma GCC visibility pop

enum
{
    PROP_0,
    PROP_ROOT,
    PROP_BLACKBOARD,
    PROP_STATUS,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

enum
{
    SIGNAL_COMPLETED,
    N_SIGNALS
};

static guint signals[N_SIGNALS];

static void
lrg_behavior_tree_finalize (GObject *object)
{
    LrgBehaviorTree *self = LRG_BEHAVIOR_TREE (object);

    g_clear_object (&self->root);
    g_clear_object (&self->blackboard);

    G_OBJECT_CLASS (lrg_behavior_tree_parent_class)->finalize (object);
}

static void
lrg_behavior_tree_get_property (GObject    *object,
                                guint       prop_id,
                                GValue     *value,
                                GParamSpec *pspec)
{
    LrgBehaviorTree *self = LRG_BEHAVIOR_TREE (object);

    switch (prop_id)
    {
    case PROP_ROOT:
        g_value_set_object (value, self->root);
        break;

    case PROP_BLACKBOARD:
        g_value_set_object (value, self->blackboard);
        break;

    case PROP_STATUS:
        g_value_set_enum (value, self->status);
        break;

    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_behavior_tree_set_property (GObject      *object,
                                guint         prop_id,
                                const GValue *value,
                                GParamSpec   *pspec)
{
    LrgBehaviorTree *self = LRG_BEHAVIOR_TREE (object);

    switch (prop_id)
    {
    case PROP_ROOT:
        lrg_behavior_tree_set_root (self, g_value_get_object (value));
        break;

    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_behavior_tree_class_init (LrgBehaviorTreeClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = lrg_behavior_tree_finalize;
    object_class->get_property = lrg_behavior_tree_get_property;
    object_class->set_property = lrg_behavior_tree_set_property;

    /**
     * LrgBehaviorTree:root:
     *
     * The root node of the behavior tree.
     */
    properties[PROP_ROOT] =
        g_param_spec_object ("root",
                             "Root",
                             "Root node of the tree",
                             LRG_TYPE_BT_NODE,
                             G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgBehaviorTree:blackboard:
     *
     * The blackboard for sharing data between nodes.
     */
    properties[PROP_BLACKBOARD] =
        g_param_spec_object ("blackboard",
                             "Blackboard",
                             "Data blackboard for the tree",
                             LRG_TYPE_BLACKBOARD,
                             G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgBehaviorTree:status:
     *
     * The current status of the tree.
     */
    properties[PROP_STATUS] =
        g_param_spec_enum ("status",
                           "Status",
                           "Current tree status",
                           LRG_TYPE_BT_STATUS,
                           LRG_BT_STATUS_INVALID,
                           G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);

    /**
     * LrgBehaviorTree::completed:
     * @self: The tree that completed
     * @status: The final status
     *
     * Emitted when the behavior tree completes execution.
     */
    signals[SIGNAL_COMPLETED] =
        g_signal_new ("completed",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0,
                      NULL, NULL, NULL,
                      G_TYPE_NONE, 1, LRG_TYPE_BT_STATUS);
}

static void
lrg_behavior_tree_init (LrgBehaviorTree *self)
{
    self->blackboard = lrg_blackboard_new ();
    self->status = LRG_BT_STATUS_INVALID;
}

/**
 * lrg_behavior_tree_new:
 *
 * Creates a new empty behavior tree.
 *
 * Returns: (transfer full): A new #LrgBehaviorTree
 */
LrgBehaviorTree *
lrg_behavior_tree_new (void)
{
    return g_object_new (LRG_TYPE_BEHAVIOR_TREE, NULL);
}

/**
 * lrg_behavior_tree_new_with_root:
 * @root: (transfer none): The root node
 *
 * Creates a new behavior tree with a root node.
 *
 * Returns: (transfer full): A new #LrgBehaviorTree
 */
LrgBehaviorTree *
lrg_behavior_tree_new_with_root (LrgBTNode *root)
{
    g_return_val_if_fail (LRG_IS_BT_NODE (root), NULL);

    return g_object_new (LRG_TYPE_BEHAVIOR_TREE,
                         "root", root,
                         NULL);
}

/**
 * lrg_behavior_tree_get_root:
 * @self: an #LrgBehaviorTree
 *
 * Gets the root node of the tree.
 *
 * Returns: (transfer none) (nullable): The root node
 */
LrgBTNode *
lrg_behavior_tree_get_root (LrgBehaviorTree *self)
{
    g_return_val_if_fail (LRG_IS_BEHAVIOR_TREE (self), NULL);

    return self->root;
}

/**
 * lrg_behavior_tree_set_root:
 * @self: an #LrgBehaviorTree
 * @root: (nullable) (transfer none): The root node
 *
 * Sets the root node of the tree.
 */
void
lrg_behavior_tree_set_root (LrgBehaviorTree *self,
                            LrgBTNode       *root)
{
    g_return_if_fail (LRG_IS_BEHAVIOR_TREE (self));
    g_return_if_fail (root == NULL || LRG_IS_BT_NODE (root));

    if (self->root != root)
    {
        g_clear_object (&self->root);
        self->root = root ? g_object_ref (root) : NULL;
        self->status = LRG_BT_STATUS_INVALID;

        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ROOT]);
    }
}

/**
 * lrg_behavior_tree_get_blackboard:
 * @self: an #LrgBehaviorTree
 *
 * Gets the blackboard for this tree.
 *
 * Returns: (transfer none): The blackboard
 */
LrgBlackboard *
lrg_behavior_tree_get_blackboard (LrgBehaviorTree *self)
{
    g_return_val_if_fail (LRG_IS_BEHAVIOR_TREE (self), NULL);

    return self->blackboard;
}

/**
 * lrg_behavior_tree_tick:
 * @self: an #LrgBehaviorTree
 * @delta_time: Time since last tick
 *
 * Executes one tick of the behavior tree.
 *
 * Returns: The result status
 */
LrgBTStatus
lrg_behavior_tree_tick (LrgBehaviorTree *self,
                        gfloat           delta_time)
{
    LrgBTStatus prev_status;

    g_return_val_if_fail (LRG_IS_BEHAVIOR_TREE (self), LRG_BT_STATUS_FAILURE);

    if (self->root == NULL)
        return LRG_BT_STATUS_FAILURE;

    prev_status = self->status;
    self->status = lrg_bt_node_tick (self->root, self->blackboard, delta_time);

    /* Emit completed signal when tree finishes */
    if (prev_status == LRG_BT_STATUS_RUNNING &&
        self->status != LRG_BT_STATUS_RUNNING)
    {
        g_signal_emit (self, signals[SIGNAL_COMPLETED], 0, self->status);
    }

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_STATUS]);

    return self->status;
}

/**
 * lrg_behavior_tree_reset:
 * @self: an #LrgBehaviorTree
 *
 * Resets the tree to its initial state.
 */
void
lrg_behavior_tree_reset (LrgBehaviorTree *self)
{
    g_return_if_fail (LRG_IS_BEHAVIOR_TREE (self));

    if (self->root)
        lrg_bt_node_reset (self->root);

    self->status = LRG_BT_STATUS_INVALID;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_STATUS]);
}

/**
 * lrg_behavior_tree_abort:
 * @self: an #LrgBehaviorTree
 *
 * Aborts any running nodes in the tree.
 */
void
lrg_behavior_tree_abort (LrgBehaviorTree *self)
{
    g_return_if_fail (LRG_IS_BEHAVIOR_TREE (self));

    if (self->root && lrg_bt_node_is_running (self->root))
    {
        lrg_bt_node_abort (self->root);
        self->status = LRG_BT_STATUS_INVALID;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_STATUS]);
    }
}

/**
 * lrg_behavior_tree_get_status:
 * @self: an #LrgBehaviorTree
 *
 * Gets the current status of the tree.
 *
 * Returns: The tree status
 */
LrgBTStatus
lrg_behavior_tree_get_status (LrgBehaviorTree *self)
{
    g_return_val_if_fail (LRG_IS_BEHAVIOR_TREE (self), LRG_BT_STATUS_INVALID);

    return self->status;
}

/**
 * lrg_behavior_tree_is_running:
 * @self: an #LrgBehaviorTree
 *
 * Checks if the tree is currently running.
 *
 * Returns: %TRUE if running
 */
gboolean
lrg_behavior_tree_is_running (LrgBehaviorTree *self)
{
    g_return_val_if_fail (LRG_IS_BEHAVIOR_TREE (self), FALSE);

    return self->status == LRG_BT_STATUS_RUNNING;
}
