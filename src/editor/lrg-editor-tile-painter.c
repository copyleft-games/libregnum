/* lrg-editor-tile-painter.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Headless tile painting tools. Every gesture becomes one or more
 * LRG_EDITOR_CMD_TILES commands executed through the editor, so undo,
 * redo, dirty tracking and IPC change events work like any other edit.
 * A stroke is wrapped in an editor group, so it undoes as one step.
 */

#include "lrg-editor-tile-painter.h"
#include "lrg-editor.h"
#include "lrg-editor-command.h"
#include "lrg-editor-math.h"
#include "lrg-node.h"
#include "lrg-node-visual.h"
#include "lrg-node-tilemap.h"
#include "../lrg-enums.h"
#include <stdlib.h>

#define MAX_BRUSH (64)

struct _LrgEditorTilePainter
{
	GObject parent_instance;

	LrgEditor *editor;
	guint      tile;
	guint      layer;
	guint      brush;

	/* Stroke state. */
	LrgNode   *stroke_node;   /* ref while stroking */
	gboolean   stroke_erase;
	gboolean   has_last;
	gint       last_x;
	gint       last_y;
	guint      stroke_changes;
};

G_DEFINE_FINAL_TYPE (LrgEditorTilePainter, lrg_editor_tile_painter, G_TYPE_OBJECT)

static void
lrg_editor_tile_painter_dispose (GObject *object)
{
	LrgEditorTilePainter *self = LRG_EDITOR_TILE_PAINTER (object);

	/* Never leave the editor with a group open. */
	if (self->stroke_node != NULL)
		lrg_editor_tile_painter_end_stroke (self);
	g_clear_object (&self->editor);

	G_OBJECT_CLASS (lrg_editor_tile_painter_parent_class)->dispose (object);
}

static void
lrg_editor_tile_painter_class_init (LrgEditorTilePainterClass *klass)
{
	G_OBJECT_CLASS (klass)->dispose = lrg_editor_tile_painter_dispose;
}

static void
lrg_editor_tile_painter_init (LrgEditorTilePainter *self)
{
	self->tile = 1;
	self->brush = 1;
}

LrgEditorTilePainter *
lrg_editor_tile_painter_new (LrgEditor *editor)
{
	LrgEditorTilePainter *self;

	g_return_val_if_fail (LRG_IS_EDITOR (editor), NULL);

	self = g_object_new (LRG_TYPE_EDITOR_TILE_PAINTER, NULL);
	self->editor = g_object_ref (editor);
	return self;
}

guint
lrg_editor_tile_painter_get_tile (LrgEditorTilePainter *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_TILE_PAINTER (self), 0);
	return self->tile;
}

void
lrg_editor_tile_painter_set_tile (LrgEditorTilePainter *self,
                                  guint                 tile)
{
	g_return_if_fail (LRG_IS_EDITOR_TILE_PAINTER (self));
	self->tile = tile;
}

guint
lrg_editor_tile_painter_get_layer (LrgEditorTilePainter *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_TILE_PAINTER (self), 0);
	return self->layer;
}

void
lrg_editor_tile_painter_set_layer (LrgEditorTilePainter *self,
                                   guint                 layer)
{
	g_return_if_fail (LRG_IS_EDITOR_TILE_PAINTER (self));
	self->layer = layer;
}

guint
lrg_editor_tile_painter_get_brush_size (LrgEditorTilePainter *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_TILE_PAINTER (self), 1);
	return self->brush;
}

void
lrg_editor_tile_painter_set_brush_size (LrgEditorTilePainter *self,
                                        guint                 size)
{
	g_return_if_fail (LRG_IS_EDITOR_TILE_PAINTER (self));
	self->brush = CLAMP (size, 1, MAX_BRUSH);
}

/* ==========================================================================
 * Helpers
 * ========================================================================== */

static LrgNodeTilemap *
node_tilemap (LrgNode *node)
{
	LrgNodeVisual *visual = lrg_node_get_visual (node);

	if (visual == NULL || lrg_node_visual_get_kind (visual) != LRG_NODE_VISUAL_TILEMAP)
		return NULL;
	return lrg_node_visual_get_tilemap (visual);
}

static gboolean
is_tilemap_node (LrgNode *node)
{
	LrgNodeVisual *visual = LRG_IS_NODE (node) ? lrg_node_get_visual (node) : NULL;

	return visual != NULL && lrg_node_visual_get_kind (visual) == LRG_NODE_VISUAL_TILEMAP;
}

/* The painter's layer, clamped to what the grid has. */
static guint
effective_layer (LrgEditorTilePainter *self,
                 LrgNodeTilemap       *tilemap)
{
	guint n = lrg_node_tilemap_get_n_layers (tilemap);

	return MIN (self->layer, n > 0 ? n - 1 : 0);
}

/*
 * Accumulates cell changes into one TILES command, skipping cells that
 * already hold the value and cells already recorded (a brush line
 * revisits cells). Returns the command, or NULL if nothing changes.
 */
typedef struct
{
	LrgNodeTilemap   *tilemap;
	guint             layer;
	guint             value;
	LrgEditorCommand *cmd;
	GHashTable       *seen;   /* cell index set */
} ChangeSet;

static void
change_set_init (ChangeSet      *cs,
                 LrgNode        *node,
                 LrgNodeTilemap *tilemap,
                 guint           layer,
                 guint           value,
                 const gchar    *label)
{
	cs->tilemap = tilemap;
	cs->layer = layer;
	cs->value = value;
	cs->cmd = lrg_editor_command_new_tiles (node, layer, label);
	cs->seen = g_hash_table_new (g_direct_hash, g_direct_equal);
}

static void
change_set_add (ChangeSet *cs,
                gint       x,
                gint       y)
{
	guint    before;
	gpointer key;

	if (!lrg_node_tilemap_in_bounds (cs->tilemap, x, y))
		return;
	before = lrg_node_tilemap_get_tile (cs->tilemap, cs->layer, x, y);
	if (before == cs->value)
		return;
	key = GUINT_TO_POINTER ((guint) y * lrg_node_tilemap_get_width (cs->tilemap) + (guint) x + 1);
	if (!g_hash_table_add (cs->seen, key))
		return;
	lrg_editor_command_tiles_add (cs->cmd, x, y, before, cs->value);
}

/* Executes the command if it has changes; returns how many. */
static guint
change_set_commit (LrgEditorTilePainter *self,
                   ChangeSet            *cs)
{
	guint n = lrg_editor_command_tiles_get_n_changes (cs->cmd);

	g_hash_table_unref (cs->seen);
	if (n == 0)
	{
		g_object_unref (cs->cmd);
		return 0;
	}
	lrg_editor_execute (self->editor, cs->cmd);   /* transfer full */
	return n;
}

static void
stamp (LrgEditorTilePainter *self,
       ChangeSet            *cs,
       gint                  cx,
       gint                  cy)
{
	gint b = (gint) self->brush;
	gint ox = cx - (b - 1) / 2;
	gint oy = cy - (b - 1) / 2;
	gint i, j;

	for (j = 0; j < b; j++)
		for (i = 0; i < b; i++)
			change_set_add (cs, ox + i, oy + j);
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

LrgNodeTilemap *
lrg_editor_tile_painter_ensure_tilemap (LrgEditorTilePainter *self,
                                        LrgNode              *node)
{
	g_autoptr(LrgNode)        before = NULL;
	g_autoptr(LrgNodeTilemap) fresh = NULL;

	g_return_val_if_fail (LRG_IS_EDITOR_TILE_PAINTER (self), NULL);

	if (!is_tilemap_node (node))
		return NULL;
	if (node_tilemap (node) != NULL)
		return node_tilemap (node);

	/* Give the node an empty grid as an ordinary node-state edit. */
	before = lrg_editor_snapshot_node (node);
	fresh = lrg_node_tilemap_new (LRG_EDITOR_TILEMAP_DEFAULT_SIZE, LRG_EDITOR_TILEMAP_DEFAULT_SIZE);
	lrg_node_visual_set_tilemap (lrg_node_get_visual (node), fresh);
	lrg_editor_commit_node_state (self->editor, node, before, "Create Tilemap", FALSE);
	return node_tilemap (node);
}

gboolean
lrg_editor_tile_painter_begin_stroke (LrgEditorTilePainter *self,
                                      LrgNode              *node,
                                      gboolean              erase)
{
	g_return_val_if_fail (LRG_IS_EDITOR_TILE_PAINTER (self), FALSE);

	if (self->stroke_node != NULL || !is_tilemap_node (node))
		return FALSE;

	lrg_editor_begin_group (self->editor, erase ? "Erase Tiles" : "Paint Tiles");
	if (lrg_editor_tile_painter_ensure_tilemap (self, node) == NULL)
	{
		lrg_editor_end_group (self->editor);
		return FALSE;
	}
	self->stroke_node = g_object_ref (node);
	self->stroke_erase = erase;
	self->has_last = FALSE;
	self->stroke_changes = 0;
	return TRUE;
}

guint
lrg_editor_tile_painter_stroke_to (LrgEditorTilePainter *self,
                                   gint                  x,
                                   gint                  y)
{
	LrgNodeTilemap *tilemap;
	ChangeSet       cs;
	gint            x0, y0, dx, dy, sx, sy, err;
	guint           n;

	g_return_val_if_fail (LRG_IS_EDITOR_TILE_PAINTER (self), 0);

	if (self->stroke_node == NULL || (tilemap = node_tilemap (self->stroke_node)) == NULL)
		return 0;

	change_set_init (&cs, self->stroke_node, tilemap, effective_layer (self, tilemap),
	                 self->stroke_erase ? 0 : self->tile, self->stroke_erase ? "Erase Tiles" : "Paint Tiles");

	/* Bresenham from the previous sample so fast drags stay continuous. */
	x0 = self->has_last ? self->last_x : x;
	y0 = self->has_last ? self->last_y : y;
	dx = abs (x - x0);
	dy = -abs (y - y0);
	sx = (x0 < x) ? 1 : -1;
	sy = (y0 < y) ? 1 : -1;
	err = dx + dy;
	for (;;)
	{
		gint e2;

		stamp (self, &cs, x0, y0);
		if (x0 == x && y0 == y)
			break;
		e2 = 2 * err;
		if (e2 >= dy)
		{
			err += dy;
			x0 += sx;
		}
		if (e2 <= dx)
		{
			err += dx;
			y0 += sy;
		}
	}

	self->has_last = TRUE;
	self->last_x = x;
	self->last_y = y;
	n = change_set_commit (self, &cs);
	self->stroke_changes += n;
	return n;
}

guint
lrg_editor_tile_painter_end_stroke (LrgEditorTilePainter *self)
{
	guint changes;

	g_return_val_if_fail (LRG_IS_EDITOR_TILE_PAINTER (self), 0);

	if (self->stroke_node == NULL)
		return 0;
	g_clear_object (&self->stroke_node);
	changes = self->stroke_changes;
	self->stroke_changes = 0;
	self->has_last = FALSE;
	lrg_editor_end_group (self->editor);
	return changes;
}

gboolean
lrg_editor_tile_painter_is_stroking (LrgEditorTilePainter *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_TILE_PAINTER (self), FALSE);
	return self->stroke_node != NULL;
}

/* Wraps a one-shot edit (cells, rect, fill) in a group so the grid
 * creation and the edit undo together. */
static LrgNodeTilemap *
begin_one_shot (LrgEditorTilePainter *self,
                LrgNode              *node,
                const gchar          *label)
{
	LrgNodeTilemap *tilemap;

	if (!is_tilemap_node (node))
		return NULL;
	lrg_editor_begin_group (self->editor, label);
	tilemap = lrg_editor_tile_painter_ensure_tilemap (self, node);
	if (tilemap == NULL)
		lrg_editor_end_group (self->editor);
	return tilemap;
}

guint
lrg_editor_tile_painter_paint_cells (LrgEditorTilePainter *self,
                                     LrgNode              *node,
                                     const gint           *cells,
                                     gsize                 n_values,
                                     gboolean              erase)
{
	const gchar    *label = erase ? "Erase Tiles" : "Paint Tiles";
	LrgNodeTilemap *tilemap;
	ChangeSet       cs;
	gsize           i;
	guint           n;

	g_return_val_if_fail (LRG_IS_EDITOR_TILE_PAINTER (self), 0);
	g_return_val_if_fail (cells != NULL || n_values == 0, 0);

	if ((tilemap = begin_one_shot (self, node, label)) == NULL)
		return 0;
	change_set_init (&cs, node, tilemap, effective_layer (self, tilemap), erase ? 0 : self->tile, label);
	for (i = 0; i + 1 < n_values; i += 2)
		change_set_add (&cs, cells[i], cells[i + 1]);
	n = change_set_commit (self, &cs);
	lrg_editor_end_group (self->editor);
	return n;
}

guint
lrg_editor_tile_painter_paint_rect (LrgEditorTilePainter *self,
                                    LrgNode              *node,
                                    gint                  x0,
                                    gint                  y0,
                                    gint                  x1,
                                    gint                  y1,
                                    gboolean              erase)
{
	const gchar    *label = erase ? "Erase Rectangle" : "Paint Rectangle";
	LrgNodeTilemap *tilemap;
	ChangeSet       cs;
	gint            x, y, minx, maxx, miny, maxy;
	guint           n;

	g_return_val_if_fail (LRG_IS_EDITOR_TILE_PAINTER (self), 0);

	if ((tilemap = begin_one_shot (self, node, label)) == NULL)
		return 0;

	/* Clip to the grid first so a huge rectangle costs only its overlap. */
	minx = MAX (MIN (x0, x1), 0);
	maxx = MIN (MAX (x0, x1), (gint) lrg_node_tilemap_get_width (tilemap) - 1);
	miny = MAX (MIN (y0, y1), 0);
	maxy = MIN (MAX (y0, y1), (gint) lrg_node_tilemap_get_height (tilemap) - 1);

	change_set_init (&cs, node, tilemap, effective_layer (self, tilemap), erase ? 0 : self->tile, label);
	for (y = miny; y <= maxy; y++)
		for (x = minx; x <= maxx; x++)
			change_set_add (&cs, x, y);
	n = change_set_commit (self, &cs);
	lrg_editor_end_group (self->editor);
	return n;
}

guint
lrg_editor_tile_painter_fill (LrgEditorTilePainter *self,
                              LrgNode              *node,
                              gint                  x,
                              gint                  y,
                              gboolean              erase)
{
	const gchar      *label = erase ? "Erase Fill" : "Fill Tiles";
	LrgNodeTilemap   *tilemap;
	g_autoptr(GArray) region = NULL;
	ChangeSet         cs;
	guint             i, w, n;

	g_return_val_if_fail (LRG_IS_EDITOR_TILE_PAINTER (self), 0);

	if ((tilemap = begin_one_shot (self, node, label)) == NULL)
		return 0;

	w = lrg_node_tilemap_get_width (tilemap);
	region = lrg_node_tilemap_flood_region (tilemap, effective_layer (self, tilemap), x, y);
	change_set_init (&cs, node, tilemap, effective_layer (self, tilemap), erase ? 0 : self->tile, label);
	for (i = 0; i < region->len; i++)
	{
		guint idx = g_array_index (region, guint, i);

		change_set_add (&cs, (gint) (idx % w), (gint) (idx / w));
	}
	n = change_set_commit (self, &cs);
	lrg_editor_end_group (self->editor);
	return n;
}

guint
lrg_editor_tile_painter_pick (LrgEditorTilePainter *self,
                              LrgNode              *node,
                              gint                  x,
                              gint                  y)
{
	LrgNodeTilemap *tilemap;
	guint           tile;

	g_return_val_if_fail (LRG_IS_EDITOR_TILE_PAINTER (self), 0);

	if (!is_tilemap_node (node) || (tilemap = node_tilemap (node)) == NULL)
		return 0;
	tile = lrg_node_tilemap_get_tile (tilemap, effective_layer (self, tilemap), x, y);
	if (tile != 0)
		self->tile = tile;
	return tile;
}

gfloat
lrg_editor_tile_painter_get_tile_size (LrgNode *node)
{
	LrgNodeVisual *visual;
	gdouble        size;

	g_return_val_if_fail (LRG_IS_NODE (node), 1.0f);

	visual = lrg_node_get_visual (node);
	size = (visual != NULL) ? lrg_node_visual_get_param_double (visual, "tile_size", 1.0) : 1.0;
	return (size > 0.0) ? (gfloat) size : 1.0f;
}

gboolean
lrg_editor_tile_painter_cell_at (LrgNode      *node,
                                 const gfloat *world_point,
                                 gint         *out_x,
                                 gint         *out_y)
{
	LrgNodeTilemap *tilemap;
	gfloat          world[16], inv[16], local[3];

	g_return_val_if_fail (LRG_IS_NODE (node), FALSE);
	g_return_val_if_fail (world_point != NULL && out_x != NULL && out_y != NULL, FALSE);

	*out_x = *out_y = -1;
	if ((tilemap = node_tilemap (node)) == NULL)
		return FALSE;

	lrg_editor_node_get_world_matrix (node, world);
	if (!lrg_editor_math_invert (world, inv))
		return FALSE;
	lrg_editor_math_transform_point (inv, world_point, local);
	return lrg_node_tilemap_local_to_cell (tilemap, lrg_editor_tile_painter_get_tile_size (node),
	                                       local[0], local[1], out_x, out_y);
}
