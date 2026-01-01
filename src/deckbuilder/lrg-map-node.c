/* lrg-map-node.c
 *
 * Copyright 2025 Libregnum Authors
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "lrg-map-node.h"
#include "../lrg-log.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_DECKBUILDER

/**
 * LrgMapNode:
 *
 * Represents a single node on the run map.
 *
 * Each node has:
 * - A type (combat, elite, boss, event, shop, rest, etc.)
 * - A position (row and column on the map)
 * - Connections to nodes in the next row
 * - Optional encounter ID for the specific content
 * - Rendering position (x, y) for display
 *
 * The map is structured as a grid where:
 * - Row 0 is the starting floor
 * - Each row can have multiple nodes
 * - Nodes connect to nodes in the next row
 * - The player moves forward by selecting connected nodes
 *
 * Since: 1.0
 */
struct _LrgMapNode
{
    GObject parent_instance;

    gchar          *id;
    LrgMapNodeType  node_type;
    gint            row;
    gint            column;

    /* Connections to nodes in the next row */
    GPtrArray      *connections;

    /* State */
    gboolean        visited;
    gchar          *encounter_id;

    /* Render position */
    gfloat          x;
    gfloat          y;
};

G_DEFINE_FINAL_TYPE (LrgMapNode, lrg_map_node, G_TYPE_OBJECT)

enum
{
    PROP_0,
    PROP_ID,
    PROP_NODE_TYPE,
    PROP_ROW,
    PROP_COLUMN,
    PROP_VISITED,
    PROP_ENCOUNTER_ID,
    PROP_X,
    PROP_Y,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

static void
lrg_map_node_finalize (GObject *object)
{
    LrgMapNode *self = LRG_MAP_NODE (object);

    g_free (self->id);
    g_free (self->encounter_id);
    g_ptr_array_unref (self->connections);

    G_OBJECT_CLASS (lrg_map_node_parent_class)->finalize (object);
}

static void
lrg_map_node_get_property (GObject    *object,
                           guint       prop_id,
                           GValue     *value,
                           GParamSpec *pspec)
{
    LrgMapNode *self = LRG_MAP_NODE (object);

    switch (prop_id)
    {
    case PROP_ID:
        g_value_set_string (value, self->id);
        break;
    case PROP_NODE_TYPE:
        g_value_set_enum (value, self->node_type);
        break;
    case PROP_ROW:
        g_value_set_int (value, self->row);
        break;
    case PROP_COLUMN:
        g_value_set_int (value, self->column);
        break;
    case PROP_VISITED:
        g_value_set_boolean (value, self->visited);
        break;
    case PROP_ENCOUNTER_ID:
        g_value_set_string (value, self->encounter_id);
        break;
    case PROP_X:
        g_value_set_float (value, self->x);
        break;
    case PROP_Y:
        g_value_set_float (value, self->y);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
lrg_map_node_set_property (GObject      *object,
                           guint         prop_id,
                           const GValue *value,
                           GParamSpec   *pspec)
{
    LrgMapNode *self = LRG_MAP_NODE (object);

    switch (prop_id)
    {
    case PROP_ID:
        g_free (self->id);
        self->id = g_value_dup_string (value);
        break;
    case PROP_NODE_TYPE:
        self->node_type = g_value_get_enum (value);
        break;
    case PROP_ROW:
        self->row = g_value_get_int (value);
        break;
    case PROP_COLUMN:
        self->column = g_value_get_int (value);
        break;
    case PROP_VISITED:
        self->visited = g_value_get_boolean (value);
        break;
    case PROP_ENCOUNTER_ID:
        g_free (self->encounter_id);
        self->encounter_id = g_value_dup_string (value);
        break;
    case PROP_X:
        self->x = g_value_get_float (value);
        break;
    case PROP_Y:
        self->y = g_value_get_float (value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
lrg_map_node_class_init (LrgMapNodeClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = lrg_map_node_finalize;
    object_class->get_property = lrg_map_node_get_property;
    object_class->set_property = lrg_map_node_set_property;

    /**
     * LrgMapNode:id:
     *
     * Unique identifier for this node.
     *
     * Since: 1.0
     */
    properties[PROP_ID] =
        g_param_spec_string ("id",
                             "ID",
                             "Unique node identifier",
                             NULL,
                             G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

    /**
     * LrgMapNode:node-type:
     *
     * The type of encounter at this node.
     *
     * Since: 1.0
     */
    properties[PROP_NODE_TYPE] =
        g_param_spec_enum ("node-type",
                           "Node Type",
                           "Type of encounter at this node",
                           LRG_TYPE_MAP_NODE_TYPE,
                           LRG_MAP_NODE_COMBAT,
                           G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

    /**
     * LrgMapNode:row:
     *
     * The row (floor) of this node on the map.
     *
     * Since: 1.0
     */
    properties[PROP_ROW] =
        g_param_spec_int ("row",
                          "Row",
                          "Row (floor) of this node",
                          0, G_MAXINT, 0,
                          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

    /**
     * LrgMapNode:column:
     *
     * The column position within the row.
     *
     * Since: 1.0
     */
    properties[PROP_COLUMN] =
        g_param_spec_int ("column",
                          "Column",
                          "Column position within the row",
                          0, G_MAXINT, 0,
                          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

    /**
     * LrgMapNode:visited:
     *
     * Whether this node has been visited.
     *
     * Since: 1.0
     */
    properties[PROP_VISITED] =
        g_param_spec_boolean ("visited",
                              "Visited",
                              "Whether this node has been visited",
                              FALSE,
                              G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgMapNode:encounter-id:
     *
     * The encounter ID for looking up specific content.
     *
     * Since: 1.0
     */
    properties[PROP_ENCOUNTER_ID] =
        g_param_spec_string ("encounter-id",
                             "Encounter ID",
                             "ID of the specific encounter",
                             NULL,
                             G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgMapNode:x:
     *
     * X coordinate for rendering.
     *
     * Since: 1.0
     */
    properties[PROP_X] =
        g_param_spec_float ("x",
                            "X",
                            "X coordinate for rendering",
                            -G_MAXFLOAT, G_MAXFLOAT, 0.0f,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgMapNode:y:
     *
     * Y coordinate for rendering.
     *
     * Since: 1.0
     */
    properties[PROP_Y] =
        g_param_spec_float ("y",
                            "Y",
                            "Y coordinate for rendering",
                            -G_MAXFLOAT, G_MAXFLOAT, 0.0f,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_map_node_init (LrgMapNode *self)
{
    /* Connections do not hold refs to avoid circular references */
    self->connections = g_ptr_array_new ();
    self->visited = FALSE;
    self->x = 0.0f;
    self->y = 0.0f;
}

LrgMapNode *
lrg_map_node_new (const gchar   *id,
                  LrgMapNodeType node_type,
                  gint           row,
                  gint           column)
{
    g_return_val_if_fail (id != NULL, NULL);

    return g_object_new (LRG_TYPE_MAP_NODE,
                         "id", id,
                         "node-type", node_type,
                         "row", row,
                         "column", column,
                         NULL);
}

const gchar *
lrg_map_node_get_id (LrgMapNode *self)
{
    g_return_val_if_fail (LRG_IS_MAP_NODE (self), NULL);

    return self->id;
}

LrgMapNodeType
lrg_map_node_get_node_type (LrgMapNode *self)
{
    g_return_val_if_fail (LRG_IS_MAP_NODE (self), LRG_MAP_NODE_COMBAT);

    return self->node_type;
}

gint
lrg_map_node_get_row (LrgMapNode *self)
{
    g_return_val_if_fail (LRG_IS_MAP_NODE (self), 0);

    return self->row;
}

gint
lrg_map_node_get_column (LrgMapNode *self)
{
    g_return_val_if_fail (LRG_IS_MAP_NODE (self), 0);

    return self->column;
}

void
lrg_map_node_add_connection (LrgMapNode *self,
                             LrgMapNode *target)
{
    g_return_if_fail (LRG_IS_MAP_NODE (self));
    g_return_if_fail (LRG_IS_MAP_NODE (target));

    /* Check if already connected */
    if (lrg_map_node_is_connected_to (self, target))
    {
        lrg_debug (LRG_LOG_DOMAIN,
                   "Node %s already connected to %s",
                   self->id, target->id);
        return;
    }

    /* Add connection (no ref to avoid circular refs) */
    g_ptr_array_add (self->connections, target);

    lrg_debug (LRG_LOG_DOMAIN,
               "Connected node %s -> %s",
               self->id, target->id);
}

gboolean
lrg_map_node_remove_connection (LrgMapNode *self,
                                LrgMapNode *target)
{
    g_return_val_if_fail (LRG_IS_MAP_NODE (self), FALSE);
    g_return_val_if_fail (LRG_IS_MAP_NODE (target), FALSE);

    return g_ptr_array_remove (self->connections, target);
}

GPtrArray *
lrg_map_node_get_connections (LrgMapNode *self)
{
    g_return_val_if_fail (LRG_IS_MAP_NODE (self), NULL);

    return self->connections;
}

guint
lrg_map_node_get_connection_count (LrgMapNode *self)
{
    g_return_val_if_fail (LRG_IS_MAP_NODE (self), 0);

    return self->connections->len;
}

gboolean
lrg_map_node_is_connected_to (LrgMapNode *self,
                              LrgMapNode *target)
{
    guint i;

    g_return_val_if_fail (LRG_IS_MAP_NODE (self), FALSE);
    g_return_val_if_fail (LRG_IS_MAP_NODE (target), FALSE);

    for (i = 0; i < self->connections->len; i++)
    {
        if (g_ptr_array_index (self->connections, i) == target)
            return TRUE;
    }

    return FALSE;
}

gboolean
lrg_map_node_get_visited (LrgMapNode *self)
{
    g_return_val_if_fail (LRG_IS_MAP_NODE (self), FALSE);

    return self->visited;
}

void
lrg_map_node_set_visited (LrgMapNode *self,
                          gboolean    visited)
{
    g_return_if_fail (LRG_IS_MAP_NODE (self));

    if (self->visited != visited)
    {
        self->visited = visited;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_VISITED]);
    }
}

const gchar *
lrg_map_node_get_encounter_id (LrgMapNode *self)
{
    g_return_val_if_fail (LRG_IS_MAP_NODE (self), NULL);

    return self->encounter_id;
}

void
lrg_map_node_set_encounter_id (LrgMapNode  *self,
                               const gchar *encounter_id)
{
    g_return_if_fail (LRG_IS_MAP_NODE (self));

    if (g_strcmp0 (self->encounter_id, encounter_id) != 0)
    {
        g_free (self->encounter_id);
        self->encounter_id = g_strdup (encounter_id);
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ENCOUNTER_ID]);
    }
}

gfloat
lrg_map_node_get_x (LrgMapNode *self)
{
    g_return_val_if_fail (LRG_IS_MAP_NODE (self), 0.0f);

    return self->x;
}

void
lrg_map_node_set_x (LrgMapNode *self,
                    gfloat      x)
{
    g_return_if_fail (LRG_IS_MAP_NODE (self));

    if (self->x != x)
    {
        self->x = x;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_X]);
    }
}

gfloat
lrg_map_node_get_y (LrgMapNode *self)
{
    g_return_val_if_fail (LRG_IS_MAP_NODE (self), 0.0f);

    return self->y;
}

void
lrg_map_node_set_y (LrgMapNode *self,
                    gfloat      y)
{
    g_return_if_fail (LRG_IS_MAP_NODE (self));

    if (self->y != y)
    {
        self->y = y;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_Y]);
    }
}
