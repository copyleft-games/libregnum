/* lrg-level-stream.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "config.h"
#include "lrg-level-stream.h"
#include "lrg-level.h"
#include "lrg-level-serializer.h"
#include "lrg-node.h"

typedef struct
{
	gchar   *id;
	gchar   *path;
	gfloat   min_x, min_y, min_z;
	gfloat   max_x, max_y, max_z;
	gboolean loaded;
} Cell;

struct _LrgLevelStream
{
	GObject    parent_instance;
	GPtrArray *cells;
};

G_DEFINE_FINAL_TYPE (LrgLevelStream, lrg_level_stream, G_TYPE_OBJECT)

static void
cell_free (gpointer data)
{
	Cell *cell = data;

	g_free (cell->id);
	g_free (cell->path);
	g_free (cell);
}

static void
lrg_level_stream_finalize (GObject *object)
{
	LrgLevelStream *self = LRG_LEVEL_STREAM (object);

	g_clear_pointer (&self->cells, g_ptr_array_unref);
	G_OBJECT_CLASS (lrg_level_stream_parent_class)->finalize (object);
}

static void
lrg_level_stream_class_init (LrgLevelStreamClass *klass)
{
	G_OBJECT_CLASS (klass)->finalize = lrg_level_stream_finalize;
}

static void
lrg_level_stream_init (LrgLevelStream *self)
{
	self->cells = g_ptr_array_new_with_free_func (cell_free);
}

LrgLevelStream *
lrg_level_stream_new (void)
{
	return g_object_new (LRG_TYPE_LEVEL_STREAM, NULL);
}

gboolean
lrg_level_stream_add_cell (LrgLevelStream *self,
                           const gchar    *id,
                           const gchar    *path,
                           gfloat          min_x,
                           gfloat          min_y,
                           gfloat          min_z,
                           gfloat          max_x,
                           gfloat          max_y,
                           gfloat          max_z)
{
	Cell  *cell;
	guint  i;

	g_return_val_if_fail (LRG_IS_LEVEL_STREAM (self), FALSE);
	g_return_val_if_fail (id != NULL && path != NULL, FALSE);

	for (i = 0; i < self->cells->len; i++)
	{
		cell = g_ptr_array_index (self->cells, i);
		if (g_strcmp0 (cell->id, id) == 0)
		{
			g_free (cell->path);
			cell->path = g_strdup (path);
			cell->min_x = min_x;
			cell->min_y = min_y;
			cell->min_z = min_z;
			cell->max_x = max_x;
			cell->max_y = max_y;
			cell->max_z = max_z;
			return TRUE;
		}
	}
	cell = g_new0 (Cell, 1);
	cell->id = g_strdup (id);
	cell->path = g_strdup (path);
	cell->min_x = min_x;
	cell->min_y = min_y;
	cell->min_z = min_z;
	cell->max_x = max_x;
	cell->max_y = max_y;
	cell->max_z = max_z;
	g_ptr_array_add (self->cells, cell);
	return TRUE;
}

static gboolean
contains (const Cell *cell,
          gfloat      x,
          gfloat      y,
          gfloat      z,
          gfloat      margin)
{
	return x >= cell->min_x - margin && x <= cell->max_x + margin &&
	       y >= cell->min_y - margin && y <= cell->max_y + margin &&
	       z >= cell->min_z - margin && z <= cell->max_z + margin;
}

static GStrv
steal_ids (GPtrArray *ids)
{
	g_ptr_array_add (ids, NULL);
	return (GStrv) g_ptr_array_free (ids, FALSE);
}

gboolean
lrg_level_stream_update (LrgLevelStream  *self,
                         gfloat           x,
                         gfloat           y,
                         gfloat           z,
                         gfloat           margin,
                         GStrv           *load,
                         GStrv           *unload)
{
	GPtrArray *loads;
	GPtrArray *unloads;
	guint      i;

	g_return_val_if_fail (LRG_IS_LEVEL_STREAM (self), FALSE);

	loads = g_ptr_array_new_with_free_func (g_free);
	unloads = g_ptr_array_new_with_free_func (g_free);
	for (i = 0; i < self->cells->len; i++)
	{
		Cell    *cell = g_ptr_array_index (self->cells, i);
		gboolean want = contains (cell, x, y, z, margin);

		if (want && !cell->loaded)
		{
			cell->loaded = TRUE;
			g_ptr_array_add (loads, g_strdup (cell->id));
		}
		else if (!want && cell->loaded)
		{
			cell->loaded = FALSE;
			g_ptr_array_add (unloads, g_strdup (cell->id));
		}
	}
	if (load != NULL)
		*load = steal_ids (loads);
	else
		g_ptr_array_unref (loads);
	if (unload != NULL)
		*unload = steal_ids (unloads);
	else
		g_ptr_array_unref (unloads);
	return TRUE;
}

static void
load_thread (GTask        *task,
             gpointer      source,
             gpointer      task_data,
             GCancellable *cancellable)
{
	const gchar *path = task_data;
	g_autoptr(LrgLevelSerializer) ser = NULL;
	g_autoptr(GError) error = NULL;
	LrgLevel *level;

	(void) source;
	(void) cancellable;
	ser = lrg_level_serializer_new ();
	level = lrg_level_serializer_load_from_file (ser, path, &error);
	if (level == NULL)
	{
		if (error == NULL)
			g_set_error (&error, G_IO_ERROR, G_IO_ERROR_FAILED,
			             "could not load %s", path);
		g_task_return_error (task, g_steal_pointer (&error));
	}
	else
		g_task_return_pointer (task, level, g_object_unref);
}

void
lrg_level_stream_load_async (LrgLevelStream      *self,
                             const gchar         *path,
                             GAsyncReadyCallback  callback,
                             gpointer             user_data)
{
	GTask *task;

	g_return_if_fail (LRG_IS_LEVEL_STREAM (self));
	g_return_if_fail (path != NULL);

	task = g_task_new (self, NULL, callback, user_data);
	g_task_set_task_data (task, g_strdup (path), g_free);
	g_task_run_in_thread (task, load_thread);
	g_object_unref (task);
}

LrgLevel *
lrg_level_stream_load_finish (LrgLevelStream  *self,
                              GAsyncResult    *result,
                              GError         **error)
{
	g_return_val_if_fail (LRG_IS_LEVEL_STREAM (self), NULL);
	g_return_val_if_fail (g_task_is_valid (result, self), NULL);
	return g_task_propagate_pointer (G_TASK (result), error);
}

static void
clone_children (LrgNode *dest,
                LrgNode *src)
{
	GPtrArray *children;
	guint      i;

	children = lrg_node_get_children (src);
	for (i = 0; children != NULL && i < children->len; i++)
	{
		LrgNode *child = g_ptr_array_index (children, i);
		LrgNode *copy = lrg_node_new (NULL);

		lrg_node_set_guid (copy, lrg_node_get_guid (child));
		lrg_node_copy_state (copy, child);
		lrg_node_add_child (dest, copy);
		clone_children (copy, child);
		g_object_unref (copy);
	}
}

guint
lrg_level_instance_mount (LrgNode      *host,
                          const gchar  *path,
                          GError      **error)
{
	g_autoptr(LrgLevelSerializer) ser = NULL;
	g_autoptr(LrgLevel) level = NULL;
	LrgNode  *root;
	GPtrArray *children;

	g_return_val_if_fail (LRG_IS_NODE (host), 0);
	g_return_val_if_fail (path != NULL, 0);

	ser = lrg_level_serializer_new ();
	level = lrg_level_serializer_load_from_file (ser, path, error);
	if (level == NULL)
		return 0;
	root = lrg_level_get_root (level);
	clone_children (host, root);
	lrg_node_set_level_path (host, path);
	children = lrg_node_get_children (root);
	if (children == NULL)
		return 0;
	return children->len;
}
