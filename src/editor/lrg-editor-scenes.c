/* lrg-editor-scenes.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "config.h"
#include "lrg-editor-scenes.h"

struct _LrgEditorScenes
{
	GObject    parent_instance;
	GPtrArray *paths;
	gint       active;
};

G_DEFINE_FINAL_TYPE (LrgEditorScenes, lrg_editor_scenes, G_TYPE_OBJECT)

static void
lrg_editor_scenes_finalize (GObject *object)
{
	LrgEditorScenes *self = LRG_EDITOR_SCENES (object);

	g_clear_pointer (&self->paths, g_ptr_array_unref);
	G_OBJECT_CLASS (lrg_editor_scenes_parent_class)->finalize (object);
}

static void
lrg_editor_scenes_class_init (LrgEditorScenesClass *klass)
{
	G_OBJECT_CLASS (klass)->finalize = lrg_editor_scenes_finalize;
}

static void
lrg_editor_scenes_init (LrgEditorScenes *self)
{
	self->paths = g_ptr_array_new_with_free_func (g_free);
	self->active = -1;
}

LrgEditorScenes *
lrg_editor_scenes_new (void)
{
	return g_object_new (LRG_TYPE_EDITOR_SCENES, NULL);
}

guint
lrg_editor_scenes_add (LrgEditorScenes *self,
                       const gchar     *path)
{
	g_return_val_if_fail (LRG_IS_EDITOR_SCENES (self), 0);
	g_return_val_if_fail (path != NULL, 0);

	g_ptr_array_add (self->paths, g_strdup (path));
	self->active = (gint) self->paths->len - 1;
	return (guint) self->active;
}

gboolean
lrg_editor_scenes_remove (LrgEditorScenes *self,
                          guint            index)
{
	g_return_val_if_fail (LRG_IS_EDITOR_SCENES (self), FALSE);
	if (index >= self->paths->len)
		return FALSE;
	g_ptr_array_remove_index (self->paths, index);
	if (self->paths->len == 0)
		self->active = -1;
	else if (self->active >= (gint) self->paths->len)
		self->active = (gint) self->paths->len - 1;
	else if ((gint) index < self->active)
		self->active--;
	return TRUE;
}

void
lrg_editor_scenes_set_active (LrgEditorScenes *self,
                              guint            index)
{
	g_return_if_fail (LRG_IS_EDITOR_SCENES (self));
	g_return_if_fail (index < self->paths->len);
	self->active = (gint) index;
}

gint
lrg_editor_scenes_get_active (LrgEditorScenes *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_SCENES (self), -1);
	return self->active;
}

guint
lrg_editor_scenes_get_count (LrgEditorScenes *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_SCENES (self), 0);
	return self->paths->len;
}

const gchar *
lrg_editor_scenes_get_path (LrgEditorScenes *self,
                            guint            index)
{
	g_return_val_if_fail (LRG_IS_EDITOR_SCENES (self), NULL);
	if (index >= self->paths->len)
		return NULL;
	return g_ptr_array_index (self->paths, index);
}

#define LRG_EDITOR_SCENES_KEY "lrg-editor-scenes"

LrgEditorScenes *
lrg_editor_scenes_ensure (GObject *owner)
{
	LrgEditorScenes *scenes;

	g_return_val_if_fail (G_IS_OBJECT (owner), NULL);

	scenes = g_object_get_data (owner, LRG_EDITOR_SCENES_KEY);
	if (!LRG_IS_EDITOR_SCENES (scenes))
	{
		scenes = lrg_editor_scenes_new ();
		g_object_set_data_full (owner, LRG_EDITOR_SCENES_KEY, scenes, g_object_unref);
	}
	return scenes;
}
