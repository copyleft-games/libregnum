/* lrg-bt-node.c
 *
 * Copyright 2024 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Base class for behavior tree nodes.
 */

#include "lrg-bt-node.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_AI
#include "lrg-log.h"

typedef struct
{
    gchar       *name;
    LrgBTStatus  status;
} LrgBTNodePrivate;

#pragma GCC visibility push(default)
G_DEFINE_TYPE_WITH_PRIVATE (LrgBTNode, lrg_bt_node, G_TYPE_OBJECT)
#pragma GCC visibility pop

enum
{
    PROP_0,
    PROP_NAME,
    PROP_STATUS,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

static void
lrg_bt_node_finalize (GObject *object)
{
    LrgBTNode *self = LRG_BT_NODE (object);
    LrgBTNodePrivate *priv = lrg_bt_node_get_instance_private (self);

    g_clear_pointer (&priv->name, g_free);

    G_OBJECT_CLASS (lrg_bt_node_parent_class)->finalize (object);
}

static void
lrg_bt_node_get_property (GObject    *object,
                          guint       prop_id,
                          GValue     *value,
                          GParamSpec *pspec)
{
    LrgBTNode *self = LRG_BT_NODE (object);
    LrgBTNodePrivate *priv = lrg_bt_node_get_instance_private (self);

    switch (prop_id)
    {
    case PROP_NAME:
        g_value_set_string (value, priv->name);
        break;

    case PROP_STATUS:
        g_value_set_enum (value, priv->status);
        break;

    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_bt_node_set_property (GObject      *object,
                          guint         prop_id,
                          const GValue *value,
                          GParamSpec   *pspec)
{
    LrgBTNode *self = LRG_BT_NODE (object);
    LrgBTNodePrivate *priv = lrg_bt_node_get_instance_private (self);

    switch (prop_id)
    {
    case PROP_NAME:
        g_free (priv->name);
        priv->name = g_value_dup_string (value);
        break;

    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

/*
 * Default tick implementation - returns FAILURE.
 */
static LrgBTStatus
lrg_bt_node_real_tick (LrgBTNode     *self,
                       LrgBlackboard *blackboard,
                       gfloat         delta_time)
{
    return LRG_BT_STATUS_FAILURE;
}

/*
 * Default reset implementation - resets status to INVALID.
 */
static void
lrg_bt_node_real_reset (LrgBTNode *self)
{
    LrgBTNodePrivate *priv = lrg_bt_node_get_instance_private (self);

    priv->status = LRG_BT_STATUS_INVALID;
}

/*
 * Default abort implementation - resets status to INVALID.
 */
static void
lrg_bt_node_real_abort (LrgBTNode *self)
{
    LrgBTNodePrivate *priv = lrg_bt_node_get_instance_private (self);

    if (priv->status == LRG_BT_STATUS_RUNNING)
    {
        lrg_debug (LRG_LOG_DOMAIN_AI, "Aborting node: %s",
                   priv->name ? priv->name : "(unnamed)");
        priv->status = LRG_BT_STATUS_INVALID;
    }
}

static void
lrg_bt_node_class_init (LrgBTNodeClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = lrg_bt_node_finalize;
    object_class->get_property = lrg_bt_node_get_property;
    object_class->set_property = lrg_bt_node_set_property;

    /* Virtual methods */
    klass->tick = lrg_bt_node_real_tick;
    klass->reset = lrg_bt_node_real_reset;
    klass->abort = lrg_bt_node_real_abort;

    /**
     * LrgBTNode:name:
     *
     * The name of the node (for debugging).
     */
    properties[PROP_NAME] =
        g_param_spec_string ("name",
                             "Name",
                             "Node name for debugging",
                             NULL,
                             G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgBTNode:status:
     *
     * The current status of the node.
     */
    properties[PROP_STATUS] =
        g_param_spec_enum ("status",
                           "Status",
                           "Current node status",
                           LRG_TYPE_BT_STATUS,
                           LRG_BT_STATUS_INVALID,
                           G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_bt_node_init (LrgBTNode *self)
{
    LrgBTNodePrivate *priv = lrg_bt_node_get_instance_private (self);

    priv->status = LRG_BT_STATUS_INVALID;
    priv->name = NULL;
}

/**
 * lrg_bt_node_get_name:
 * @self: an #LrgBTNode
 *
 * Gets the node name.
 *
 * Returns: (transfer none) (nullable): The node name
 */
const gchar *
lrg_bt_node_get_name (LrgBTNode *self)
{
    LrgBTNodePrivate *priv;

    g_return_val_if_fail (LRG_IS_BT_NODE (self), NULL);

    priv = lrg_bt_node_get_instance_private (self);
    return priv->name;
}

/**
 * lrg_bt_node_set_name:
 * @self: an #LrgBTNode
 * @name: (nullable): The node name
 *
 * Sets the node name.
 */
void
lrg_bt_node_set_name (LrgBTNode   *self,
                      const gchar *name)
{
    LrgBTNodePrivate *priv;

    g_return_if_fail (LRG_IS_BT_NODE (self));

    priv = lrg_bt_node_get_instance_private (self);
    g_free (priv->name);
    priv->name = g_strdup (name);

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_NAME]);
}

/**
 * lrg_bt_node_get_status:
 * @self: an #LrgBTNode
 *
 * Gets the current status of the node.
 *
 * Returns: The node status
 */
LrgBTStatus
lrg_bt_node_get_status (LrgBTNode *self)
{
    LrgBTNodePrivate *priv;

    g_return_val_if_fail (LRG_IS_BT_NODE (self), LRG_BT_STATUS_INVALID);

    priv = lrg_bt_node_get_instance_private (self);
    return priv->status;
}

/**
 * lrg_bt_node_tick:
 * @self: an #LrgBTNode
 * @blackboard: The shared blackboard
 * @delta_time: Time since last tick
 *
 * Executes one tick of the node.
 *
 * Returns: The result status
 */
LrgBTStatus
lrg_bt_node_tick (LrgBTNode     *self,
                  LrgBlackboard *blackboard,
                  gfloat         delta_time)
{
    LrgBTNodePrivate *priv;
    LrgBTNodeClass *klass;

    g_return_val_if_fail (LRG_IS_BT_NODE (self), LRG_BT_STATUS_FAILURE);
    g_return_val_if_fail (LRG_IS_BLACKBOARD (blackboard), LRG_BT_STATUS_FAILURE);

    priv = lrg_bt_node_get_instance_private (self);
    klass = LRG_BT_NODE_GET_CLASS (self);

    priv->status = klass->tick (self, blackboard, delta_time);

    return priv->status;
}

/**
 * lrg_bt_node_reset:
 * @self: an #LrgBTNode
 *
 * Resets the node to its initial state.
 */
void
lrg_bt_node_reset (LrgBTNode *self)
{
    LrgBTNodeClass *klass;

    g_return_if_fail (LRG_IS_BT_NODE (self));

    klass = LRG_BT_NODE_GET_CLASS (self);
    klass->reset (self);
}

/**
 * lrg_bt_node_abort:
 * @self: an #LrgBTNode
 *
 * Aborts a running node.
 */
void
lrg_bt_node_abort (LrgBTNode *self)
{
    LrgBTNodeClass *klass;

    g_return_if_fail (LRG_IS_BT_NODE (self));

    klass = LRG_BT_NODE_GET_CLASS (self);
    klass->abort (self);
}

/**
 * lrg_bt_node_is_running:
 * @self: an #LrgBTNode
 *
 * Checks if the node is currently running.
 *
 * Returns: %TRUE if running
 */
gboolean
lrg_bt_node_is_running (LrgBTNode *self)
{
    LrgBTNodePrivate *priv;

    g_return_val_if_fail (LRG_IS_BT_NODE (self), FALSE);

    priv = lrg_bt_node_get_instance_private (self);
    return priv->status == LRG_BT_STATUS_RUNNING;
}
