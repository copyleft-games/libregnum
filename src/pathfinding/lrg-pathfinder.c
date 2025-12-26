/* lrg-pathfinder.c
 *
 * Copyright 2024 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * A* pathfinding implementation.
 */

#include <math.h>
#include "lrg-pathfinder.h"
#include "lrg-nav-cell.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_PATHFIND
#include "lrg-log.h"

/* Diagonal movement cost: sqrt(2) */
#define DIAGONAL_COST 1.41421356f

struct _LrgPathfinder
{
    GObject               parent_instance;

    LrgNavGrid           *grid;
    LrgPathSmoothingMode  smoothing;
    guint                 max_iterations;

    LrgHeuristicFunc      heuristic;
    gpointer              heuristic_data;
    GDestroyNotify        heuristic_destroy;

    guint                 last_nodes_explored;
};

#pragma GCC visibility push(default)
G_DEFINE_FINAL_TYPE (LrgPathfinder, lrg_pathfinder, G_TYPE_OBJECT)
#pragma GCC visibility pop

enum
{
    PROP_0,
    PROP_GRID,
    PROP_SMOOTHING,
    PROP_MAX_ITERATIONS,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

/*
 * AStarNode:
 *
 * Internal node structure for A* algorithm.
 */
typedef struct
{
    gint    x;
    gint    y;
    gfloat  g_cost;      /* Cost from start */
    gfloat  h_cost;      /* Heuristic cost to goal */
    gfloat  f_cost;      /* g + h */
    gint    parent_x;
    gint    parent_y;
    gboolean in_closed;
} AStarNode;

/*
 * node_hash:
 *
 * Hash function for node coordinates.
 */
static guint
node_hash (gconstpointer key)
{
    const AStarNode *node = key;
    return g_int_hash (&node->x) ^ (g_int_hash (&node->y) << 16);
}

/*
 * node_equal:
 *
 * Equality function for node coordinates.
 */
static gboolean
node_equal (gconstpointer a,
            gconstpointer b)
{
    const AStarNode *na = a;
    const AStarNode *nb = b;
    return na->x == nb->x && na->y == nb->y;
}

/*
 * node_compare:
 *
 * Comparison function for priority queue (min-heap by f_cost).
 */
static gint
node_compare (gconstpointer a,
              gconstpointer b,
              gpointer      user_data)
{
    const AStarNode *na = a;
    const AStarNode *nb = b;

    (void)user_data;

    if (na->f_cost < nb->f_cost)
        return -1;
    if (na->f_cost > nb->f_cost)
        return 1;
    return 0;
}

/*
 * smooth_path_simple:
 *
 * Simple path smoothing - removes redundant waypoints on straight lines.
 */
static void
smooth_path_simple (LrgPath *path)
{
    guint len;
    guint i;
    gint prev_dx = 0;
    gint prev_dy = 0;
    GArray *keep_indices;

    len = lrg_path_get_length (path);
    if (len <= 2)
        return;

    keep_indices = g_array_new (FALSE, FALSE, sizeof (guint));

    /* Always keep first point */
    i = 0;
    g_array_append_val (keep_indices, i);

    for (i = 1; i < len; i++)
    {
        gint x1, y1, x2, y2;
        gint dx, dy;

        lrg_path_get_point (path, i - 1, &x1, &y1);
        lrg_path_get_point (path, i, &x2, &y2);

        dx = x2 - x1;
        dy = y2 - y1;

        /* If direction changed, keep the previous point */
        if (dx != prev_dx || dy != prev_dy)
        {
            if (i > 1)
            {
                guint prev_idx = i - 1;
                g_array_append_val (keep_indices, prev_idx);
            }
            prev_dx = dx;
            prev_dy = dy;
        }
    }

    /* Always keep last point */
    i = len - 1;
    if (keep_indices->len == 0 ||
        g_array_index (keep_indices, guint, keep_indices->len - 1) != i)
    {
        g_array_append_val (keep_indices, i);
    }

    /* Rebuild path with only kept points */
    if (keep_indices->len < len)
    {
        g_autoptr(LrgPath) new_path = lrg_path_new ();

        for (i = 0; i < keep_indices->len; i++)
        {
            guint idx = g_array_index (keep_indices, guint, i);
            gint x, y;
            lrg_path_get_point (path, idx, &x, &y);
            lrg_path_append (new_path, x, y);
        }

        lrg_path_clear (path);
        for (i = 0; i < lrg_path_get_length (new_path); i++)
        {
            gint x, y;
            lrg_path_get_point (new_path, i, &x, &y);
            lrg_path_append (path, x, y);
        }
    }

    g_array_free (keep_indices, TRUE);
}

static void
lrg_pathfinder_get_property (GObject    *object,
                             guint       prop_id,
                             GValue     *value,
                             GParamSpec *pspec)
{
    LrgPathfinder *self = LRG_PATHFINDER (object);

    switch (prop_id)
    {
    case PROP_GRID:
        g_value_set_object (value, self->grid);
        break;
    case PROP_SMOOTHING:
        g_value_set_enum (value, self->smoothing);
        break;
    case PROP_MAX_ITERATIONS:
        g_value_set_uint (value, self->max_iterations);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_pathfinder_set_property (GObject      *object,
                             guint         prop_id,
                             const GValue *value,
                             GParamSpec   *pspec)
{
    LrgPathfinder *self = LRG_PATHFINDER (object);

    switch (prop_id)
    {
    case PROP_GRID:
        lrg_pathfinder_set_grid (self, g_value_get_object (value));
        break;
    case PROP_SMOOTHING:
        self->smoothing = g_value_get_enum (value);
        break;
    case PROP_MAX_ITERATIONS:
        self->max_iterations = g_value_get_uint (value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_pathfinder_dispose (GObject *object)
{
    LrgPathfinder *self = LRG_PATHFINDER (object);

    g_clear_object (&self->grid);

    if (self->heuristic_data != NULL && self->heuristic_destroy != NULL)
    {
        self->heuristic_destroy (self->heuristic_data);
        self->heuristic_data = NULL;
    }

    G_OBJECT_CLASS (lrg_pathfinder_parent_class)->dispose (object);
}

static void
lrg_pathfinder_class_init (LrgPathfinderClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->get_property = lrg_pathfinder_get_property;
    object_class->set_property = lrg_pathfinder_set_property;
    object_class->dispose = lrg_pathfinder_dispose;

    /**
     * LrgPathfinder:grid:
     *
     * The navigation grid to use for pathfinding.
     */
    properties[PROP_GRID] =
        g_param_spec_object ("grid",
                             "Grid",
                             "Navigation grid",
                             LRG_TYPE_NAV_GRID,
                             G_PARAM_READWRITE |
                             G_PARAM_STATIC_STRINGS);

    /**
     * LrgPathfinder:smoothing:
     *
     * Path smoothing mode.
     */
    properties[PROP_SMOOTHING] =
        g_param_spec_enum ("smoothing",
                           "Smoothing",
                           "Path smoothing mode",
                           LRG_TYPE_PATH_SMOOTHING_MODE,
                           LRG_PATH_SMOOTHING_NONE,
                           G_PARAM_READWRITE |
                           G_PARAM_STATIC_STRINGS);

    /**
     * LrgPathfinder:max-iterations:
     *
     * Maximum iterations before giving up (0 = unlimited).
     */
    properties[PROP_MAX_ITERATIONS] =
        g_param_spec_uint ("max-iterations",
                           "Max Iterations",
                           "Maximum search iterations",
                           0, G_MAXUINT, 0,
                           G_PARAM_READWRITE |
                           G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_pathfinder_init (LrgPathfinder *self)
{
    self->grid = NULL;
    self->smoothing = LRG_PATH_SMOOTHING_NONE;
    self->max_iterations = 0;
    self->heuristic = lrg_heuristic_manhattan;
    self->heuristic_data = NULL;
    self->heuristic_destroy = NULL;
    self->last_nodes_explored = 0;
}

/**
 * lrg_pathfinder_new:
 * @grid: The navigation grid to use
 *
 * Creates a new pathfinder for the given grid.
 *
 * Returns: (transfer full): A new #LrgPathfinder
 */
LrgPathfinder *
lrg_pathfinder_new (LrgNavGrid *grid)
{
    return g_object_new (LRG_TYPE_PATHFINDER,
                         "grid", grid,
                         NULL);
}

/**
 * lrg_pathfinder_get_grid:
 * @self: an #LrgPathfinder
 *
 * Gets the navigation grid.
 *
 * Returns: (transfer none): The navigation grid
 */
LrgNavGrid *
lrg_pathfinder_get_grid (LrgPathfinder *self)
{
    g_return_val_if_fail (LRG_IS_PATHFINDER (self), NULL);
    return self->grid;
}

/**
 * lrg_pathfinder_set_grid:
 * @self: an #LrgPathfinder
 * @grid: The navigation grid
 *
 * Sets the navigation grid.
 */
void
lrg_pathfinder_set_grid (LrgPathfinder *self,
                         LrgNavGrid    *grid)
{
    g_return_if_fail (LRG_IS_PATHFINDER (self));

    if (g_set_object (&self->grid, grid))
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_GRID]);
}

/**
 * lrg_pathfinder_find_path:
 * @self: an #LrgPathfinder
 * @start_x: Start X coordinate
 * @start_y: Start Y coordinate
 * @end_x: End X coordinate
 * @end_y: End Y coordinate
 * @error: (nullable): Return location for error
 *
 * Finds a path from start to end using A*.
 *
 * Returns: (transfer full) (nullable): The path, or %NULL on error
 */
LrgPath *
lrg_pathfinder_find_path (LrgPathfinder  *self,
                          gint            start_x,
                          gint            start_y,
                          gint            end_x,
                          gint            end_y,
                          GError        **error)
{
    GHashTable *all_nodes = NULL;
    GQueue *open_list = NULL;
    LrgPath *path = NULL;
    AStarNode *start_node = NULL;
    AStarNode *current = NULL;
    guint iterations = 0;
    gboolean found = FALSE;

    g_return_val_if_fail (LRG_IS_PATHFINDER (self), NULL);
    g_return_val_if_fail (error == NULL || *error == NULL, NULL);

    self->last_nodes_explored = 0;

    /* Validate grid */
    if (self->grid == NULL)
    {
        g_set_error (error, LRG_PATHFINDING_ERROR,
                     LRG_PATHFINDING_ERROR_NO_GRID,
                     "No navigation grid set");
        return NULL;
    }

    /* Validate coordinates */
    if (!lrg_nav_grid_is_valid (self->grid, start_x, start_y))
    {
        g_set_error (error, LRG_PATHFINDING_ERROR,
                     LRG_PATHFINDING_ERROR_INVALID_START,
                     "Invalid start position (%d, %d)", start_x, start_y);
        return NULL;
    }

    if (!lrg_nav_grid_is_valid (self->grid, end_x, end_y))
    {
        g_set_error (error, LRG_PATHFINDING_ERROR,
                     LRG_PATHFINDING_ERROR_INVALID_GOAL,
                     "Invalid end position (%d, %d)", end_x, end_y);
        return NULL;
    }

    /* Check if start/end are walkable */
    if (!lrg_nav_grid_is_walkable (self->grid, start_x, start_y))
    {
        g_set_error (error, LRG_PATHFINDING_ERROR,
                     LRG_PATHFINDING_ERROR_INVALID_START,
                     "Start position (%d, %d) is not walkable", start_x, start_y);
        return NULL;
    }

    if (!lrg_nav_grid_is_walkable (self->grid, end_x, end_y))
    {
        g_set_error (error, LRG_PATHFINDING_ERROR,
                     LRG_PATHFINDING_ERROR_INVALID_GOAL,
                     "End position (%d, %d) is not walkable", end_x, end_y);
        return NULL;
    }

    /* Same start and end */
    if (start_x == end_x && start_y == end_y)
    {
        path = lrg_path_new ();
        lrg_path_append (path, start_x, start_y);
        lrg_path_set_total_cost (path, 0.0f);
        return path;
    }

    /* Initialize data structures */
    all_nodes = g_hash_table_new_full (node_hash, node_equal, NULL, g_free);
    open_list = g_queue_new ();

    /* Create start node */
    start_node = g_new0 (AStarNode, 1);
    start_node->x = start_x;
    start_node->y = start_y;
    start_node->g_cost = 0.0f;
    start_node->h_cost = self->heuristic (start_x, start_y, end_x, end_y,
                                          self->heuristic_data);
    start_node->f_cost = start_node->h_cost;
    start_node->parent_x = -1;
    start_node->parent_y = -1;
    start_node->in_closed = FALSE;

    g_hash_table_insert (all_nodes, start_node, start_node);
    g_queue_insert_sorted (open_list, start_node, node_compare, NULL);

    /* A* main loop */
    while (!g_queue_is_empty (open_list))
    {
        GList *neighbors = NULL;
        GList *iter = NULL;

        iterations++;
        if (self->max_iterations > 0 && iterations > self->max_iterations)
        {
            lrg_log_debug ("Pathfinding exceeded max iterations (%u)",
                           self->max_iterations);
            break;
        }

        /* Get node with lowest f_cost */
        current = g_queue_pop_head (open_list);
        current->in_closed = TRUE;
        self->last_nodes_explored++;

        /* Check if we reached the goal */
        if (current->x == end_x && current->y == end_y)
        {
            found = TRUE;
            break;
        }

        /* Process neighbors */
        neighbors = lrg_nav_grid_get_neighbors (self->grid, current->x, current->y);

        for (iter = neighbors; iter != NULL; iter = iter->next)
        {
            LrgNavCell *neighbor_cell = iter->data;
            AStarNode lookup;
            AStarNode *neighbor;
            gfloat move_cost;
            gfloat new_g;
            gint nx;
            gint ny;

            nx = lrg_nav_cell_get_x (neighbor_cell);
            ny = lrg_nav_cell_get_y (neighbor_cell);

            lookup.x = nx;
            lookup.y = ny;
            neighbor = g_hash_table_lookup (all_nodes, &lookup);

            /* Calculate movement cost */
            move_cost = lrg_nav_cell_get_cost (neighbor_cell);
            if (nx != current->x && ny != current->y)
                move_cost *= DIAGONAL_COST;

            new_g = current->g_cost + move_cost;

            if (neighbor == NULL)
            {
                /* New node */
                neighbor = g_new0 (AStarNode, 1);
                neighbor->x = nx;
                neighbor->y = ny;
                neighbor->g_cost = new_g;
                neighbor->h_cost = self->heuristic (nx, ny, end_x, end_y,
                                                    self->heuristic_data);
                neighbor->f_cost = neighbor->g_cost + neighbor->h_cost;
                neighbor->parent_x = current->x;
                neighbor->parent_y = current->y;
                neighbor->in_closed = FALSE;

                g_hash_table_insert (all_nodes, neighbor, neighbor);
                g_queue_insert_sorted (open_list, neighbor, node_compare, NULL);
            }
            else if (!neighbor->in_closed && new_g < neighbor->g_cost)
            {
                /* Better path found */
                neighbor->g_cost = new_g;
                neighbor->f_cost = neighbor->g_cost + neighbor->h_cost;
                neighbor->parent_x = current->x;
                neighbor->parent_y = current->y;

                /* Re-sort in open list */
                g_queue_remove (open_list, neighbor);
                g_queue_insert_sorted (open_list, neighbor, node_compare, NULL);
            }
        }

        g_list_free_full (neighbors, (GDestroyNotify)lrg_nav_cell_free);
    }

    /* Build path if found */
    if (found)
    {
        AStarNode *node = current;

        path = lrg_path_new ();
        lrg_path_set_total_cost (path, current->g_cost);

        while (node != NULL)
        {
            AStarNode lookup;

            lrg_path_prepend (path, node->x, node->y);

            if (node->parent_x < 0)
                break;

            lookup.x = node->parent_x;
            lookup.y = node->parent_y;
            node = g_hash_table_lookup (all_nodes, &lookup);
        }

        /* Apply smoothing */
        if (self->smoothing == LRG_PATH_SMOOTHING_SIMPLE)
        {
            smooth_path_simple (path);
        }

        lrg_log_debug ("Found path with %u points, cost %.2f, explored %u nodes",
                       lrg_path_get_length (path),
                       lrg_path_get_total_cost (path),
                       self->last_nodes_explored);
    }
    else
    {
        g_set_error (error, LRG_PATHFINDING_ERROR,
                     LRG_PATHFINDING_ERROR_NO_PATH,
                     "No path found from (%d, %d) to (%d, %d)",
                     start_x, start_y, end_x, end_y);
    }

    /* Cleanup */
    g_queue_free (open_list);
    g_hash_table_destroy (all_nodes);

    return path;
}

/**
 * lrg_pathfinder_get_smoothing:
 * @self: an #LrgPathfinder
 *
 * Gets the path smoothing mode.
 *
 * Returns: The smoothing mode
 */
LrgPathSmoothingMode
lrg_pathfinder_get_smoothing (LrgPathfinder *self)
{
    g_return_val_if_fail (LRG_IS_PATHFINDER (self), LRG_PATH_SMOOTHING_NONE);
    return self->smoothing;
}

/**
 * lrg_pathfinder_set_smoothing:
 * @self: an #LrgPathfinder
 * @mode: Smoothing mode
 *
 * Sets the path smoothing mode.
 */
void
lrg_pathfinder_set_smoothing (LrgPathfinder        *self,
                              LrgPathSmoothingMode  mode)
{
    g_return_if_fail (LRG_IS_PATHFINDER (self));
    self->smoothing = mode;
}

/**
 * lrg_pathfinder_get_max_iterations:
 * @self: an #LrgPathfinder
 *
 * Gets the maximum number of iterations before giving up.
 *
 * Returns: Maximum iterations (0 = unlimited)
 */
guint
lrg_pathfinder_get_max_iterations (LrgPathfinder *self)
{
    g_return_val_if_fail (LRG_IS_PATHFINDER (self), 0);
    return self->max_iterations;
}

/**
 * lrg_pathfinder_set_max_iterations:
 * @self: an #LrgPathfinder
 * @max_iterations: Maximum iterations (0 = unlimited)
 *
 * Sets the maximum number of iterations.
 */
void
lrg_pathfinder_set_max_iterations (LrgPathfinder *self,
                                   guint          max_iterations)
{
    g_return_if_fail (LRG_IS_PATHFINDER (self));
    self->max_iterations = max_iterations;
}

/**
 * lrg_pathfinder_set_heuristic:
 * @self: an #LrgPathfinder
 * @func: (nullable) (scope notified): Heuristic function
 * @user_data: (closure): User data for function
 * @destroy: (nullable): Destroy function for user data
 *
 * Sets a custom heuristic function. If NULL, uses Manhattan distance.
 */
void
lrg_pathfinder_set_heuristic (LrgPathfinder    *self,
                              LrgHeuristicFunc  func,
                              gpointer          user_data,
                              GDestroyNotify    destroy)
{
    g_return_if_fail (LRG_IS_PATHFINDER (self));

    if (self->heuristic_data != NULL && self->heuristic_destroy != NULL)
        self->heuristic_destroy (self->heuristic_data);

    if (func != NULL)
    {
        self->heuristic = func;
        self->heuristic_data = user_data;
        self->heuristic_destroy = destroy;
    }
    else
    {
        self->heuristic = lrg_heuristic_manhattan;
        self->heuristic_data = NULL;
        self->heuristic_destroy = NULL;
    }
}

/**
 * lrg_pathfinder_get_last_nodes_explored:
 * @self: an #LrgPathfinder
 *
 * Gets the number of nodes explored in the last pathfinding operation.
 *
 * Returns: Number of nodes explored
 */
guint
lrg_pathfinder_get_last_nodes_explored (LrgPathfinder *self)
{
    g_return_val_if_fail (LRG_IS_PATHFINDER (self), 0);
    return self->last_nodes_explored;
}

/**
 * lrg_pathfinder_is_reachable:
 * @self: an #LrgPathfinder
 * @start_x: Start X coordinate
 * @start_y: Start Y coordinate
 * @end_x: End X coordinate
 * @end_y: End Y coordinate
 *
 * Checks if a path exists between two points.
 *
 * Returns: %TRUE if a path exists
 */
gboolean
lrg_pathfinder_is_reachable (LrgPathfinder *self,
                             gint           start_x,
                             gint           start_y,
                             gint           end_x,
                             gint           end_y)
{
    g_autoptr(LrgPath) path = NULL;

    g_return_val_if_fail (LRG_IS_PATHFINDER (self), FALSE);

    path = lrg_pathfinder_find_path (self, start_x, start_y, end_x, end_y, NULL);
    return path != NULL;
}

/* Built-in heuristic functions */

/**
 * lrg_heuristic_manhattan:
 * @x1: Start X
 * @y1: Start Y
 * @x2: End X
 * @y2: End Y
 * @user_data: (nullable): Unused
 *
 * Manhattan distance heuristic. Best for 4-directional movement.
 *
 * Returns: Manhattan distance
 */
gfloat
lrg_heuristic_manhattan (gint     x1,
                         gint     y1,
                         gint     x2,
                         gint     y2,
                         gpointer user_data)
{
    (void)user_data;
    return (gfloat)(abs (x2 - x1) + abs (y2 - y1));
}

/**
 * lrg_heuristic_euclidean:
 * @x1: Start X
 * @y1: Start Y
 * @x2: End X
 * @y2: End Y
 * @user_data: (nullable): Unused
 *
 * Euclidean distance heuristic.
 *
 * Returns: Euclidean distance
 */
gfloat
lrg_heuristic_euclidean (gint     x1,
                         gint     y1,
                         gint     x2,
                         gint     y2,
                         gpointer user_data)
{
    gint dx;
    gint dy;

    (void)user_data;

    dx = x2 - x1;
    dy = y2 - y1;
    return sqrtf ((gfloat)(dx * dx + dy * dy));
}

/**
 * lrg_heuristic_chebyshev:
 * @x1: Start X
 * @y1: Start Y
 * @x2: End X
 * @y2: End Y
 * @user_data: (nullable): Unused
 *
 * Chebyshev distance heuristic (diagonal cost = 1).
 *
 * Returns: Chebyshev distance
 */
gfloat
lrg_heuristic_chebyshev (gint     x1,
                         gint     y1,
                         gint     x2,
                         gint     y2,
                         gpointer user_data)
{
    gint dx;
    gint dy;

    (void)user_data;

    dx = abs (x2 - x1);
    dy = abs (y2 - y1);
    return (gfloat)MAX (dx, dy);
}

/**
 * lrg_heuristic_octile:
 * @x1: Start X
 * @y1: Start Y
 * @x2: End X
 * @y2: End Y
 * @user_data: (nullable): Unused
 *
 * Octile distance heuristic (diagonal cost = sqrt(2)).
 * Best for 8-directional movement.
 *
 * Returns: Octile distance
 */
gfloat
lrg_heuristic_octile (gint     x1,
                      gint     y1,
                      gint     x2,
                      gint     y2,
                      gpointer user_data)
{
    gint dx;
    gint dy;
    gint min_d;
    gint max_d;

    (void)user_data;

    dx = abs (x2 - x1);
    dy = abs (y2 - y1);
    min_d = MIN (dx, dy);
    max_d = MAX (dx, dy);

    return (gfloat)max_d + (DIAGONAL_COST - 1.0f) * (gfloat)min_d;
}
