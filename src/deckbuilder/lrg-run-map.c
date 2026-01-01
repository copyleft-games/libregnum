/* lrg-run-map.c
 *
 * Copyright 2025 Libregnum Authors
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "lrg-run-map.h"
#include "../lrg-log.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_DECKBUILDER

/**
 * LrgRunMap:
 *
 * Represents the procedurally generated map for a single act.
 *
 * The map is structured as a grid of nodes organized by rows:
 * - Row 0: Starting nodes (player chooses one to begin)
 * - Rows 1 to N-2: Normal floors with various encounter types
 * - Row N-1: Boss floor (single boss node)
 *
 * Map generation follows Slay the Spire-style rules:
 * - Each node connects to 1-3 nodes in the next row
 * - Connections don't cross (maintains visual clarity)
 * - Encounter types are distributed following probability rules
 * - Elite encounters appear on certain floors
 * - Rest sites appear on certain floors
 * - Shop appears before boss
 *
 * Since: 1.0
 */
struct _LrgRunMap
{
    GObject parent_instance;

    gint     act;
    guint64  seed;
    GRand   *rng;

    /* All nodes in the map */
    GPtrArray *nodes;

    /* Nodes organized by row for quick lookup */
    GPtrArray *rows;  /* GPtrArray of GPtrArray of LrgMapNode */

    /* Quick access to special nodes */
    LrgMapNode *boss_node;

    gboolean generated;
};

G_DEFINE_FINAL_TYPE (LrgRunMap, lrg_run_map, G_TYPE_OBJECT)

enum
{
    PROP_0,
    PROP_ACT,
    PROP_SEED,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

static void
lrg_run_map_finalize (GObject *object)
{
    LrgRunMap *self = LRG_RUN_MAP (object);
    guint i;

    /* Free row arrays */
    for (i = 0; i < self->rows->len; i++)
    {
        GPtrArray *row = g_ptr_array_index (self->rows, i);
        g_ptr_array_unref (row);
    }
    g_ptr_array_unref (self->rows);

    g_ptr_array_unref (self->nodes);
    g_rand_free (self->rng);

    G_OBJECT_CLASS (lrg_run_map_parent_class)->finalize (object);
}

static void
lrg_run_map_get_property (GObject    *object,
                          guint       prop_id,
                          GValue     *value,
                          GParamSpec *pspec)
{
    LrgRunMap *self = LRG_RUN_MAP (object);

    switch (prop_id)
    {
    case PROP_ACT:
        g_value_set_int (value, self->act);
        break;
    case PROP_SEED:
        g_value_set_uint64 (value, self->seed);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
lrg_run_map_set_property (GObject      *object,
                          guint         prop_id,
                          const GValue *value,
                          GParamSpec   *pspec)
{
    LrgRunMap *self = LRG_RUN_MAP (object);

    switch (prop_id)
    {
    case PROP_ACT:
        self->act = g_value_get_int (value);
        break;
    case PROP_SEED:
        self->seed = g_value_get_uint64 (value);
        g_rand_set_seed (self->rng, (guint32)self->seed);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
lrg_run_map_class_init (LrgRunMapClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = lrg_run_map_finalize;
    object_class->get_property = lrg_run_map_get_property;
    object_class->set_property = lrg_run_map_set_property;

    /**
     * LrgRunMap:act:
     *
     * The act number (1-based).
     *
     * Since: 1.0
     */
    properties[PROP_ACT] =
        g_param_spec_int ("act",
                          "Act",
                          "Act number",
                          1, G_MAXINT, 1,
                          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

    /**
     * LrgRunMap:seed:
     *
     * Random seed for map generation.
     *
     * Since: 1.0
     */
    properties[PROP_SEED] =
        g_param_spec_uint64 ("seed",
                             "Seed",
                             "Random seed for generation",
                             0, G_MAXUINT64, 0,
                             G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_run_map_init (LrgRunMap *self)
{
    self->nodes = g_ptr_array_new_with_free_func (g_object_unref);
    self->rows = g_ptr_array_new ();
    self->rng = g_rand_new ();
    self->generated = FALSE;
    self->boss_node = NULL;
}

LrgRunMap *
lrg_run_map_new (gint    act,
                 guint64 seed)
{
    return g_object_new (LRG_TYPE_RUN_MAP,
                         "act", act,
                         "seed", seed,
                         NULL);
}

gint
lrg_run_map_get_act (LrgRunMap *self)
{
    g_return_val_if_fail (LRG_IS_RUN_MAP (self), 0);

    return self->act;
}

guint64
lrg_run_map_get_seed (LrgRunMap *self)
{
    g_return_val_if_fail (LRG_IS_RUN_MAP (self), 0);

    return self->seed;
}

/*
 * Determines the node type for a given position based on Slay the Spire rules.
 * This is a simplified version - real games would have more complex logic.
 */
static LrgMapNodeType
determine_node_type (LrgRunMap *self,
                     gint       row,
                     gint       num_rows)
{
    gdouble roll;
    gint floor_num = row + 1;  /* 1-indexed floor */

    /* Boss floor is always a boss */
    if (row == num_rows - 1)
        return LRG_MAP_NODE_BOSS;

    /* First few floors are always combat */
    if (floor_num <= 3)
        return LRG_MAP_NODE_COMBAT;

    /* Floor before boss is usually shop or rest */
    if (row == num_rows - 2)
    {
        roll = g_rand_double (self->rng);
        if (roll < 0.5)
            return LRG_MAP_NODE_REST;
        else
            return LRG_MAP_NODE_SHOP;
    }

    /* Mid-act has elite encounters on certain floors */
    if (floor_num == 6 || floor_num == 10)
    {
        roll = g_rand_double (self->rng);
        if (roll < 0.4)
            return LRG_MAP_NODE_ELITE;
    }

    /* General floor type distribution */
    roll = g_rand_double (self->rng);

    if (roll < 0.45)
        return LRG_MAP_NODE_COMBAT;
    else if (roll < 0.65)
        return LRG_MAP_NODE_EVENT;
    else if (roll < 0.75)
        return LRG_MAP_NODE_ELITE;
    else if (roll < 0.85)
        return LRG_MAP_NODE_REST;
    else if (roll < 0.93)
        return LRG_MAP_NODE_SHOP;
    else if (roll < 0.97)
        return LRG_MAP_NODE_TREASURE;
    else
        return LRG_MAP_NODE_MYSTERY;
}

void
lrg_run_map_generate (LrgRunMap *self,
                      gint       num_rows,
                      gint       min_columns,
                      gint       max_columns)
{
    gint row;
    gint col;
    gint num_nodes;
    guint node_idx = 0;

    g_return_if_fail (LRG_IS_RUN_MAP (self));
    g_return_if_fail (num_rows > 0);
    g_return_if_fail (min_columns > 0);
    g_return_if_fail (max_columns >= min_columns);

    /* Clear existing map */
    lrg_run_map_clear (self);

    lrg_debug (LRG_LOG_DOMAIN,
               "Generating map for act %d with %d rows, seed %" G_GUINT64_FORMAT,
               self->act, num_rows, self->seed);

    /* Create nodes for each row */
    for (row = 0; row < num_rows; row++)
    {
        GPtrArray *row_nodes = g_ptr_array_new ();

        /* Boss row has only one node */
        if (row == num_rows - 1)
        {
            num_nodes = 1;
        }
        else
        {
            num_nodes = g_rand_int_range (self->rng, min_columns, max_columns + 1);
        }

        for (col = 0; col < num_nodes; col++)
        {
            g_autofree gchar *node_id = NULL;
            LrgMapNodeType node_type;
            LrgMapNode *node;

            node_id = g_strdup_printf ("node_%d_%d_%d", self->act, row, col);
            node_type = determine_node_type (self, row, num_rows);

            node = lrg_map_node_new (node_id, node_type, row, col);
            g_ptr_array_add (self->nodes, node);
            g_ptr_array_add (row_nodes, node);

            /* Track boss node */
            if (node_type == LRG_MAP_NODE_BOSS)
                self->boss_node = node;

            node_idx++;
        }

        g_ptr_array_add (self->rows, row_nodes);
    }

    /*
     * Create connections between adjacent rows.
     * Each node connects to 1-3 nodes in the next row.
     * Connections cannot cross (to maintain visual clarity).
     */
    for (row = 0; row < num_rows - 1; row++)
    {
        GPtrArray *current_row = g_ptr_array_index (self->rows, row);
        GPtrArray *next_row = g_ptr_array_index (self->rows, row + 1);
        guint current_count = current_row->len;
        guint next_count = next_row->len;
        gint last_connection = -1;
        guint i;

        for (i = 0; i < current_count; i++)
        {
            LrgMapNode *current_node = g_ptr_array_index (current_row, i);
            gint num_connections;
            gint min_target;
            gint max_target;
            gint j;

            /* Determine valid connection range (no crossing) */
            min_target = last_connection + 1;
            if (min_target >= (gint)next_count)
                min_target = next_count - 1;

            /* Calculate proportional position for max target */
            max_target = (gint)(((gdouble)(i + 1) / current_count) * next_count);
            if (max_target >= (gint)next_count)
                max_target = next_count - 1;

            /* Ensure at least one connection */
            if (max_target < min_target)
                max_target = min_target;

            /* 1-3 connections (or fewer if near edge) */
            num_connections = g_rand_int_range (self->rng, 1, 4);
            if (num_connections > max_target - min_target + 1)
                num_connections = max_target - min_target + 1;

            /* Create connections */
            for (j = 0; j < num_connections && min_target + j <= max_target; j++)
            {
                LrgMapNode *target = g_ptr_array_index (next_row, min_target + j);
                lrg_map_node_add_connection (current_node, target);
                last_connection = min_target + j;
            }

            /* Ensure last node in row connects to last nodes in next row */
            if (i == current_count - 1 && last_connection < (gint)next_count - 1)
            {
                for (j = last_connection + 1; j < (gint)next_count; j++)
                {
                    LrgMapNode *target = g_ptr_array_index (next_row, j);
                    lrg_map_node_add_connection (current_node, target);
                }
            }
        }
    }

    self->generated = TRUE;

    lrg_debug (LRG_LOG_DOMAIN,
               "Generated map with %u nodes across %d rows",
               self->nodes->len, num_rows);
}

gint
lrg_run_map_get_row_count (LrgRunMap *self)
{
    g_return_val_if_fail (LRG_IS_RUN_MAP (self), 0);

    return self->rows->len;
}

GPtrArray *
lrg_run_map_get_nodes_in_row (LrgRunMap *self,
                              gint       row)
{
    g_return_val_if_fail (LRG_IS_RUN_MAP (self), NULL);

    if (row < 0 || row >= (gint)self->rows->len)
        return NULL;

    return g_ptr_array_index (self->rows, row);
}

GPtrArray *
lrg_run_map_get_all_nodes (LrgRunMap *self)
{
    g_return_val_if_fail (LRG_IS_RUN_MAP (self), NULL);

    return self->nodes;
}

guint
lrg_run_map_get_node_count (LrgRunMap *self)
{
    g_return_val_if_fail (LRG_IS_RUN_MAP (self), 0);

    return self->nodes->len;
}

LrgMapNode *
lrg_run_map_get_node_by_id (LrgRunMap   *self,
                            const gchar *id)
{
    guint i;

    g_return_val_if_fail (LRG_IS_RUN_MAP (self), NULL);
    g_return_val_if_fail (id != NULL, NULL);

    for (i = 0; i < self->nodes->len; i++)
    {
        LrgMapNode *node = g_ptr_array_index (self->nodes, i);
        if (g_strcmp0 (lrg_map_node_get_id (node), id) == 0)
            return node;
    }

    return NULL;
}

GPtrArray *
lrg_run_map_get_starting_nodes (LrgRunMap *self)
{
    g_return_val_if_fail (LRG_IS_RUN_MAP (self), NULL);

    if (self->rows->len == 0)
        return NULL;

    return g_ptr_array_index (self->rows, 0);
}

LrgMapNode *
lrg_run_map_get_boss_node (LrgRunMap *self)
{
    g_return_val_if_fail (LRG_IS_RUN_MAP (self), NULL);

    return self->boss_node;
}

gboolean
lrg_run_map_is_generated (LrgRunMap *self)
{
    g_return_val_if_fail (LRG_IS_RUN_MAP (self), FALSE);

    return self->generated;
}

void
lrg_run_map_add_node (LrgRunMap  *self,
                      LrgMapNode *node)
{
    gint row;
    GPtrArray *row_array;

    g_return_if_fail (LRG_IS_RUN_MAP (self));
    g_return_if_fail (LRG_IS_MAP_NODE (node));

    row = lrg_map_node_get_row (node);

    /* Ensure we have enough row arrays */
    while ((gint)self->rows->len <= row)
    {
        g_ptr_array_add (self->rows, g_ptr_array_new ());
    }

    row_array = g_ptr_array_index (self->rows, row);

    /* Takes ownership */
    g_ptr_array_add (self->nodes, node);
    g_ptr_array_add (row_array, node);

    /* Track boss node */
    if (lrg_map_node_get_node_type (node) == LRG_MAP_NODE_BOSS)
        self->boss_node = node;
}

void
lrg_run_map_clear (LrgRunMap *self)
{
    guint i;

    g_return_if_fail (LRG_IS_RUN_MAP (self));

    /* Clear row arrays */
    for (i = 0; i < self->rows->len; i++)
    {
        GPtrArray *row = g_ptr_array_index (self->rows, i);
        g_ptr_array_unref (row);
    }
    g_ptr_array_set_size (self->rows, 0);

    /* Clear nodes (this frees them) */
    g_ptr_array_set_size (self->nodes, 0);

    self->boss_node = NULL;
    self->generated = FALSE;
}

void
lrg_run_map_calculate_positions (LrgRunMap *self,
                                 gfloat     width,
                                 gfloat     height,
                                 gfloat     padding)
{
    guint row_count;
    gfloat row_height;
    guint i;

    g_return_if_fail (LRG_IS_RUN_MAP (self));

    row_count = self->rows->len;
    if (row_count == 0)
        return;

    /* Height per row (from bottom to top) */
    row_height = (height - 2 * padding) / (row_count - 1);
    if (row_count == 1)
        row_height = 0;

    for (i = 0; i < row_count; i++)
    {
        GPtrArray *row_nodes = g_ptr_array_index (self->rows, i);
        guint node_count = row_nodes->len;
        gfloat col_width;
        gfloat y;
        guint j;

        if (node_count == 0)
            continue;

        /* Y position (row 0 at bottom, higher rows go up) */
        y = height - padding - (i * row_height);

        /* Width per node in this row */
        col_width = (width - 2 * padding) / (node_count + 1);

        for (j = 0; j < node_count; j++)
        {
            LrgMapNode *node = g_ptr_array_index (row_nodes, j);
            gfloat x;

            /* Center nodes horizontally with even spacing */
            x = padding + col_width * (j + 1);

            /* Add slight random offset for visual interest */
            x += (g_rand_double (self->rng) - 0.5) * col_width * 0.3f;

            lrg_map_node_set_x (node, x);
            lrg_map_node_set_y (node, y);
        }
    }
}
