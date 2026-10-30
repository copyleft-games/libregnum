/* lrg-procgen-bsp.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Procedural generator: binary space partition rooms and corridors.
 */

#include "config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_PROCGEN

#include "lrg-procgen-bsp.h"
#include "../lrg-log.h"

struct _LrgProcgenBsp
{
	LrgProcgenGenerator parent_instance;

	guint    min_room_size;
	guint    max_room_size;
	guint    min_leaf_size;
	guint    max_leaf_size;
	gboolean doors;
};

G_DEFINE_FINAL_TYPE (LrgProcgenBsp, lrg_procgen_bsp, LRG_TYPE_PROCGEN_GENERATOR)

enum
{
	PROP_0,
	PROP_MIN_ROOM_SIZE,
	PROP_MAX_ROOM_SIZE,
	PROP_MIN_LEAF_SIZE,
	PROP_MAX_LEAF_SIZE,
	PROP_DOORS,
	N_PROPS
};

static GParamSpec *properties[N_PROPS];

/* one node of the partition tree */
typedef struct
{
	gint x;
	gint y;
	gint w;
	gint h;
	gint left;   /* child node indices, -1 for a leaf */
	gint right;
	gint room;   /* room index of a leaf, -1 otherwise */
} BspNode;

/* turns a wall cell into corridor; rooms and earlier corridors stay */
static void
carve (LrgProcgenGrid *grid,
       gint            x,
       gint            y)
{
	if (lrg_procgen_grid_get_cell (grid, x, y) == LRG_PROCGEN_CELL_WALL)
		lrg_procgen_grid_set_cell (grid, x, y, LRG_PROCGEN_CELL_CORRIDOR);
}

/* an L-shaped corridor between two cells, elbow chosen by @horizontal_first */
static void
carve_corridor (LrgProcgenGrid *grid,
                gint            ax,
                gint            ay,
                gint            bx,
                gint            by,
                gboolean        horizontal_first)
{
	gint i;
	gint ex = horizontal_first ? bx : ax;
	gint ey = horizontal_first ? ay : by;

	/* first leg: from a to the elbow */
	for (i = MIN (ax, ex); i <= MAX (ax, ex); i++)
		carve (grid, i, ay);
	for (i = MIN (ay, ey); i <= MAX (ay, ey); i++)
		carve (grid, ax, i);
	/* second leg: from the elbow to b */
	for (i = MIN (ex, bx); i <= MAX (ex, bx); i++)
		carve (grid, i, by);
	for (i = MIN (ey, by); i <= MAX (ey, by); i++)
		carve (grid, bx, i);
}

/* splits a rectangle recursively; returns the new node's index */
static gint
bsp_split (LrgProcgenBsp   *self,
           GArray          *nodes,
           LrgRandomStream *rng,
           gint             x,
           gint             y,
           gint             w,
           gint             h)
{
	BspNode node;
	gint min_leaf = (gint) self->min_leaf_size;
	gint max_leaf = (gint) self->max_leaf_size;
	gboolean can_x = w >= 2 * min_leaf;
	gboolean can_y = h >= 2 * min_leaf;
	gboolean split_x;
	gint index;
	gint left;
	gint right;
	gint at;

	node.x = x;
	node.y = y;
	node.w = w;
	node.h = h;
	node.left = -1;
	node.right = -1;
	node.room = -1;
	index = (gint) nodes->len;
	g_array_append_val (nodes, node);

	/* a leaf when it cannot split, or is already small enough */
	if ((!can_x && !can_y) || (w <= max_leaf && h <= max_leaf))
		return index;

	/* split across the long side; pick at random when nearly square */
	if (can_x && can_y)
	{
		if (w * 4 > h * 5)
			split_x = TRUE;
		else if (h * 4 > w * 5)
			split_x = FALSE;
		else
			split_x = lrg_random_stream_bounded (rng, 2) == 0;
	}
	else
		split_x = can_x;

	if (split_x)
	{
		at = min_leaf + (gint) lrg_random_stream_bounded (rng, (guint32) (w - 2 * min_leaf + 1));
		left = bsp_split (self, nodes, rng, x, y, at, h);
		right = bsp_split (self, nodes, rng, x + at, y, w - at, h);
	}
	else
	{
		at = min_leaf + (gint) lrg_random_stream_bounded (rng, (guint32) (h - 2 * min_leaf + 1));
		left = bsp_split (self, nodes, rng, x, y, w, at);
		right = bsp_split (self, nodes, rng, x, y + at, w, h - at);
	}
	g_array_index (nodes, BspNode, index).left = left;
	g_array_index (nodes, BspNode, index).right = right;
	return index;
}

/* joins the two subtrees of every node; returns the subtree's representative room */
static gint
bsp_connect (GArray          *nodes,
             gint             index,
             LrgProcgenGrid  *grid,
             LrgRandomStream *rng)
{
	BspNode *node = &g_array_index (nodes, BspNode, index);
	const LrgProcgenRoom *a;
	const LrgProcgenRoom *b;
	gint left;
	gint right;
	gint ax;
	gint ay;
	gint bx;
	gint by;

	if (node->left < 0)
		return node->room;

	left = bsp_connect (nodes, node->left, grid, rng);
	right = bsp_connect (nodes, g_array_index (nodes, BspNode, index).right, grid, rng);
	a = lrg_procgen_grid_get_room (grid, (guint) left);
	b = lrg_procgen_grid_get_room (grid, (guint) right);
	lrg_procgen_room_get_center (a, &ax, &ay);
	lrg_procgen_room_get_center (b, &bx, &by);
	carve_corridor (grid, ax, ay, bx, by, lrg_random_stream_bounded (rng, 2) == 0);
	return lrg_random_stream_bounded (rng, 2) == 0 ? left : right;
}

/* corridor cells on a room's wall ring, flanked by walls, become doors */
static void
place_doors (LrgProcgenGrid       *grid,
             const LrgProcgenRoom *room)
{
	gint x;
	gint y;
	gint rows[2];
	gint cols[2];
	guint i;

	rows[0] = room->y - 1;
	rows[1] = room->y + room->height;
	cols[0] = room->x - 1;
	cols[1] = room->x + room->width;

	for (i = 0; i < 2; i++)
	{
		for (x = room->x; x < room->x + room->width; x++)
			if (lrg_procgen_grid_get_cell (grid, x, rows[i]) == LRG_PROCGEN_CELL_CORRIDOR &&
			    lrg_procgen_grid_get_cell (grid, x - 1, rows[i]) == LRG_PROCGEN_CELL_WALL &&
			    lrg_procgen_grid_get_cell (grid, x + 1, rows[i]) == LRG_PROCGEN_CELL_WALL)
				lrg_procgen_grid_set_cell (grid, x, rows[i], LRG_PROCGEN_CELL_DOOR);
		for (y = room->y; y < room->y + room->height; y++)
			if (lrg_procgen_grid_get_cell (grid, cols[i], y) == LRG_PROCGEN_CELL_CORRIDOR &&
			    lrg_procgen_grid_get_cell (grid, cols[i], y - 1) == LRG_PROCGEN_CELL_WALL &&
			    lrg_procgen_grid_get_cell (grid, cols[i], y + 1) == LRG_PROCGEN_CELL_WALL)
				lrg_procgen_grid_set_cell (grid, cols[i], y, LRG_PROCGEN_CELL_DOOR);
	}
}

static gboolean
lrg_procgen_bsp_generate (LrgProcgenGenerator  *generator,
                          LrgProcgenGrid       *grid,
                          LrgRandomStream      *rng,
                          GError              **error)
{
	LrgProcgenBsp *self = LRG_PROCGEN_BSP (generator);
	g_autoptr(GArray) nodes = NULL;
	gint w = (gint) lrg_procgen_grid_get_width (grid);
	gint h = (gint) lrg_procgen_grid_get_height (grid);
	guint i;

	/* --- parameters --- */
	if (self->min_room_size > self->max_room_size ||
	    self->min_leaf_size < self->min_room_size + 2 ||
	    self->max_leaf_size < self->min_leaf_size)
	{
		g_set_error (error, LRG_PROCGEN_ERROR, LRG_PROCGEN_ERROR_INVALID_CONFIG,
		             "bsp: need min-room-size <= max-room-size, "
		             "min-leaf-size >= min-room-size + 2 and max-leaf-size >= min-leaf-size");
		return FALSE;
	}
	if (w < (gint) self->min_leaf_size || h < (gint) self->min_leaf_size)
	{
		g_set_error (error, LRG_PROCGEN_ERROR, LRG_PROCGEN_ERROR_TOO_SMALL,
		             "bsp: a %dx%d grid is smaller than min-leaf-size %u", w, h,
		             self->min_leaf_size);
		return FALSE;
	}

	/* --- partition, then one room per leaf (inset one cell for walls) --- */
	nodes = g_array_new (FALSE, FALSE, sizeof (BspNode));
	bsp_split (self, nodes, rng, 0, 0, w, h);
	for (i = 0; i < nodes->len; i++)
	{
		BspNode *node = &g_array_index (nodes, BspNode, i);
		LrgProcgenRoom room;
		gint max_w;
		gint max_h;

		if (node->left >= 0)
			continue;
		max_w = MIN ((gint) self->max_room_size, node->w - 2);
		max_h = MIN ((gint) self->max_room_size, node->h - 2);
		room.width = (gint) self->min_room_size +
			(gint) lrg_random_stream_bounded (rng, (guint32) (max_w - (gint) self->min_room_size + 1));
		room.height = (gint) self->min_room_size +
			(gint) lrg_random_stream_bounded (rng, (guint32) (max_h - (gint) self->min_room_size + 1));
		room.x = node->x + 1 + (gint) lrg_random_stream_bounded (rng, (guint32) (node->w - 2 - room.width + 1));
		room.y = node->y + 1 + (gint) lrg_random_stream_bounded (rng, (guint32) (node->h - 2 - room.height + 1));
		lrg_procgen_grid_fill_rect (grid, room.x, room.y, room.width, room.height,
		                            LRG_PROCGEN_CELL_FLOOR);
		node->room = (gint) lrg_procgen_grid_add_room (grid, &room);
	}

	/* --- corridors along the tree, then doors --- */
	bsp_connect (nodes, 0, grid, rng);
	if (self->doors)
		for (i = 0; i < lrg_procgen_grid_get_room_count (grid); i++)
			place_doors (grid, lrg_procgen_grid_get_room (grid, i));
	return TRUE;
}

static void
lrg_procgen_bsp_get_property (GObject    *object,
                              guint       prop_id,
                              GValue     *value,
                              GParamSpec *pspec)
{
	LrgProcgenBsp *self = LRG_PROCGEN_BSP (object);

	switch (prop_id)
	{
	case PROP_MIN_ROOM_SIZE:
		g_value_set_uint (value, self->min_room_size);
		break;
	case PROP_MAX_ROOM_SIZE:
		g_value_set_uint (value, self->max_room_size);
		break;
	case PROP_MIN_LEAF_SIZE:
		g_value_set_uint (value, self->min_leaf_size);
		break;
	case PROP_MAX_LEAF_SIZE:
		g_value_set_uint (value, self->max_leaf_size);
		break;
	case PROP_DOORS:
		g_value_set_boolean (value, self->doors);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_procgen_bsp_set_property (GObject      *object,
                              guint         prop_id,
                              const GValue *value,
                              GParamSpec   *pspec)
{
	LrgProcgenBsp *self = LRG_PROCGEN_BSP (object);

	switch (prop_id)
	{
	case PROP_MIN_ROOM_SIZE:
		lrg_procgen_bsp_set_min_room_size (self, g_value_get_uint (value));
		break;
	case PROP_MAX_ROOM_SIZE:
		lrg_procgen_bsp_set_max_room_size (self, g_value_get_uint (value));
		break;
	case PROP_MIN_LEAF_SIZE:
		lrg_procgen_bsp_set_min_leaf_size (self, g_value_get_uint (value));
		break;
	case PROP_MAX_LEAF_SIZE:
		lrg_procgen_bsp_set_max_leaf_size (self, g_value_get_uint (value));
		break;
	case PROP_DOORS:
		lrg_procgen_bsp_set_doors (self, g_value_get_boolean (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_procgen_bsp_class_init (LrgProcgenBspClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	LrgProcgenGeneratorClass *generator_class = LRG_PROCGEN_GENERATOR_CLASS (klass);

	object_class->get_property = lrg_procgen_bsp_get_property;
	object_class->set_property = lrg_procgen_bsp_set_property;
	generator_class->generate = lrg_procgen_bsp_generate;

	/**
	 * LrgProcgenBsp:min-room-size:
	 *
	 * Smallest room side, in cells.
	 */
	properties[PROP_MIN_ROOM_SIZE] =
		g_param_spec_uint ("min-room-size", NULL, NULL,
		                   1, 256, 3,
		                   G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

	/**
	 * LrgProcgenBsp:max-room-size:
	 *
	 * Largest room side, in cells.
	 */
	properties[PROP_MAX_ROOM_SIZE] =
		g_param_spec_uint ("max-room-size", NULL, NULL,
		                   1, 256, 12,
		                   G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

	/**
	 * LrgProcgenBsp:min-leaf-size:
	 *
	 * Smallest partition side; at least min-room-size + 2.
	 */
	properties[PROP_MIN_LEAF_SIZE] =
		g_param_spec_uint ("min-leaf-size", NULL, NULL,
		                   3, 1024, 8,
		                   G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

	/**
	 * LrgProcgenBsp:max-leaf-size:
	 *
	 * Partitions larger than this on either side are split again.
	 */
	properties[PROP_MAX_LEAF_SIZE] =
		g_param_spec_uint ("max-leaf-size", NULL, NULL,
		                   3, 4096, 20,
		                   G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

	/**
	 * LrgProcgenBsp:doors:
	 *
	 * Whether corridor cells entering a room become doors.
	 */
	properties[PROP_DOORS] =
		g_param_spec_boolean ("doors", NULL, NULL,
		                      TRUE,
		                      G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

	g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_procgen_bsp_init (LrgProcgenBsp *self)
{
	self->min_room_size = 3;
	self->max_room_size = 12;
	self->min_leaf_size = 8;
	self->max_leaf_size = 20;
	self->doors = TRUE;
}

LrgProcgenBsp *
lrg_procgen_bsp_new (guint width,
                     guint height)
{
	return g_object_new (LRG_TYPE_PROCGEN_BSP, "width", width, "height", height, NULL);
}

guint
lrg_procgen_bsp_get_min_room_size (LrgProcgenBsp *self)
{
	g_return_val_if_fail (LRG_IS_PROCGEN_BSP (self), 0);
	return self->min_room_size;
}

void
lrg_procgen_bsp_set_min_room_size (LrgProcgenBsp *self,
                                   guint value)
{
	g_return_if_fail (LRG_IS_PROCGEN_BSP (self));
	g_return_if_fail (value >= 1 && value <= 256);
	if (self->min_room_size == value)
		return;
	self->min_room_size = value;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MIN_ROOM_SIZE]);
}

guint
lrg_procgen_bsp_get_max_room_size (LrgProcgenBsp *self)
{
	g_return_val_if_fail (LRG_IS_PROCGEN_BSP (self), 0);
	return self->max_room_size;
}

void
lrg_procgen_bsp_set_max_room_size (LrgProcgenBsp *self,
                                   guint value)
{
	g_return_if_fail (LRG_IS_PROCGEN_BSP (self));
	g_return_if_fail (value >= 1 && value <= 256);
	if (self->max_room_size == value)
		return;
	self->max_room_size = value;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MAX_ROOM_SIZE]);
}

guint
lrg_procgen_bsp_get_min_leaf_size (LrgProcgenBsp *self)
{
	g_return_val_if_fail (LRG_IS_PROCGEN_BSP (self), 0);
	return self->min_leaf_size;
}

void
lrg_procgen_bsp_set_min_leaf_size (LrgProcgenBsp *self,
                                   guint value)
{
	g_return_if_fail (LRG_IS_PROCGEN_BSP (self));
	g_return_if_fail (value >= 3 && value <= 1024);
	if (self->min_leaf_size == value)
		return;
	self->min_leaf_size = value;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MIN_LEAF_SIZE]);
}

guint
lrg_procgen_bsp_get_max_leaf_size (LrgProcgenBsp *self)
{
	g_return_val_if_fail (LRG_IS_PROCGEN_BSP (self), 0);
	return self->max_leaf_size;
}

void
lrg_procgen_bsp_set_max_leaf_size (LrgProcgenBsp *self,
                                   guint value)
{
	g_return_if_fail (LRG_IS_PROCGEN_BSP (self));
	g_return_if_fail (value >= 3 && value <= 4096);
	if (self->max_leaf_size == value)
		return;
	self->max_leaf_size = value;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MAX_LEAF_SIZE]);
}

gboolean
lrg_procgen_bsp_get_doors (LrgProcgenBsp *self)
{
	g_return_val_if_fail (LRG_IS_PROCGEN_BSP (self), FALSE);
	return self->doors;
}

void
lrg_procgen_bsp_set_doors (LrgProcgenBsp *self,
                           gboolean value)
{
	g_return_if_fail (LRG_IS_PROCGEN_BSP (self));
	if (self->doors == (value ? TRUE : FALSE))
		return;
	self->doors = (value ? TRUE : FALSE);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_DOORS]);
}
