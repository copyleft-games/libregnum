/* lrg-editor-app.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Standalone engine-drawn editor application.
 */

#include "lrg-editor-app.h"
#include "../lrg-editor.h"
#include "../lrg-level.h"
#include "../lrg-node.h"
#include "../../ui/lrg-canvas.h"
#include "../../ui/lrg-vbox.h"
#include "../../ui/lrg-label.h"
#include "../../ui/lrg-container.h"
#include "../../ui/lrg-widget.h"

struct _LrgEditorApp
{
	GObject parent_instance;

	LrgEditor *editor;     /* owned */
	LrgCanvas *canvas;     /* owned */
	LrgVBox   *outliner;   /* borrowed (child of canvas) */
};

G_DEFINE_FINAL_TYPE (LrgEditorApp, lrg_editor_app, G_TYPE_OBJECT)

static void
add_node_rows (LrgEditorApp *self,
               LrgNode      *node,
               gint          depth)
{
	GPtrArray *children;
	guint      i;
	GString   *text = g_string_new (NULL);
	LrgLabel  *label;

	for (i = 0; i < (guint) depth; i++)
		g_string_append (text, "  ");
	g_string_append (text, lrg_node_get_name (node) != NULL
	                       ? lrg_node_get_name (node) : "(node)");

	label = lrg_label_new (text->str);
	lrg_container_add_child (LRG_CONTAINER (self->outliner), LRG_WIDGET (label));
	g_string_free (text, TRUE);

	children = lrg_node_get_children (node);
	for (i = 0; children != NULL && i < children->len; i++)
		add_node_rows (self, g_ptr_array_index (children, i), depth + 1);
}

static void
on_editor_changed (LrgEditor *editor,
                   gpointer   user_data)
{
	lrg_editor_app_refresh (LRG_EDITOR_APP (user_data));
}

static void
lrg_editor_app_dispose (GObject *object)
{
	LrgEditorApp *self = LRG_EDITOR_APP (object);

	if (self->editor != NULL)
		g_signal_handlers_disconnect_by_data (self->editor, self);

	g_clear_object (&self->editor);
	g_clear_object (&self->canvas);
	self->outliner = NULL;

	G_OBJECT_CLASS (lrg_editor_app_parent_class)->dispose (object);
}

static void
lrg_editor_app_class_init (LrgEditorAppClass *klass)
{
	G_OBJECT_CLASS (klass)->dispose = lrg_editor_app_dispose;
}

static void
lrg_editor_app_init (LrgEditorApp *self)
{
}

LrgEditorApp *
lrg_editor_app_new (LrgEditor *editor)
{
	LrgEditorApp *self = g_object_new (LRG_TYPE_EDITOR_APP, NULL);
	LrgLabel     *title;

	self->editor = (editor != NULL) ? g_object_ref (editor) : lrg_editor_new ();
	self->canvas = lrg_canvas_new ();
	self->outliner = lrg_vbox_new ();

	lrg_widget_set_position (LRG_WIDGET (self->outliner), 0.0f, 0.0f);
	lrg_widget_set_size (LRG_WIDGET (self->outliner), 240.0f, 600.0f);

	title = lrg_label_new ("Outliner");
	lrg_container_add_child (LRG_CONTAINER (self->outliner), LRG_WIDGET (title));

	lrg_container_add_child (LRG_CONTAINER (self->canvas), LRG_WIDGET (self->outliner));

	g_signal_connect (self->editor, "changed",
	                  G_CALLBACK (on_editor_changed), self);

	lrg_editor_app_refresh (self);

	return self;
}

LrgEditor *
lrg_editor_app_get_editor (LrgEditorApp *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_APP (self), NULL);
	return self->editor;
}

LrgCanvas *
lrg_editor_app_get_canvas (LrgEditorApp *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_APP (self), NULL);
	return self->canvas;
}

void
lrg_editor_app_refresh (LrgEditorApp *self)
{
	LrgLevel  *level;
	LrgNode   *root;
	GPtrArray *children;
	guint      i;
	LrgLabel  *title;

	g_return_if_fail (LRG_IS_EDITOR_APP (self));

	lrg_container_remove_all (LRG_CONTAINER (self->outliner));

	title = lrg_label_new ("Outliner");
	lrg_container_add_child (LRG_CONTAINER (self->outliner), LRG_WIDGET (title));

	level = lrg_editor_get_level (self->editor);
	if (level == NULL)
		return;

	root = lrg_level_get_root (level);
	children = lrg_node_get_children (root);
	for (i = 0; children != NULL && i < children->len; i++)
		add_node_rows (self, g_ptr_array_index (children, i), 0);
}

void
lrg_editor_app_handle_input (LrgEditorApp *self)
{
	g_return_if_fail (LRG_IS_EDITOR_APP (self));

	lrg_canvas_handle_input (self->canvas);
}

void
lrg_editor_app_render (LrgEditorApp *self)
{
	g_return_if_fail (LRG_IS_EDITOR_APP (self));

	lrg_canvas_render (self->canvas);
}
