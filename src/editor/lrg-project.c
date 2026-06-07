/* lrg-project.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * A libregnum editor project.
 */

#include "lrg-project.h"
#include "lrg-asset-database.h"
#include "../lrg-enums.h"
#include <yaml-glib.h>

struct _LrgProject
{
	GObject parent_instance;

	gchar     *root;
	gchar     *name;
	gchar     *default_level;
	gchar     *game_output;
	GPtrArray *asset_dirs;   /* gchar* */
};

G_DEFINE_FINAL_TYPE (LrgProject, lrg_project, G_TYPE_OBJECT)

static void
lrg_project_finalize (GObject *object)
{
	LrgProject *self = LRG_PROJECT (object);

	g_clear_pointer (&self->root, g_free);
	g_clear_pointer (&self->name, g_free);
	g_clear_pointer (&self->default_level, g_free);
	g_clear_pointer (&self->game_output, g_free);
	g_clear_pointer (&self->asset_dirs, g_ptr_array_unref);

	G_OBJECT_CLASS (lrg_project_parent_class)->finalize (object);
}

static void
lrg_project_class_init (LrgProjectClass *klass)
{
	G_OBJECT_CLASS (klass)->finalize = lrg_project_finalize;
}

static void
lrg_project_init (LrgProject *self)
{
	self->asset_dirs = g_ptr_array_new_with_free_func (g_free);
}

LrgProject *
lrg_project_new (const gchar *root,
                 const gchar *name)
{
	LrgProject *self = g_object_new (LRG_TYPE_PROJECT, NULL);

	self->root = g_strdup (root);
	self->name = g_strdup (name);

	return self;
}

LrgProject *
lrg_project_open (const gchar  *dir,
                  GError      **error)
{
	g_autofree gchar      *path = NULL;
	g_autoptr(YamlParser)  parser = NULL;
	YamlNode              *root;
	YamlMapping           *root_map;
	YamlMapping           *proj_map;
	YamlNode              *proj_node;
	LrgProject            *self;
	YamlNode              *dirs_node;

	g_return_val_if_fail (dir != NULL, NULL);

	path = g_build_filename (dir, LRG_PROJECT_MANIFEST, NULL);

	parser = yaml_parser_new ();
	if (!yaml_parser_load_from_file (parser, path, error))
		return NULL;

	root = yaml_parser_get_root (parser);
	if (root == NULL || yaml_node_get_node_type (root) != YAML_NODE_MAPPING)
	{
		g_set_error (error, LRG_LEVEL_ERROR, LRG_LEVEL_ERROR_PARSE,
		             "Project manifest is not a mapping");
		return NULL;
	}

	root_map = yaml_node_get_mapping (root);
	proj_node = yaml_mapping_get_member (root_map, "project");
	if (proj_node == NULL || yaml_node_get_node_type (proj_node) != YAML_NODE_MAPPING)
	{
		g_set_error (error, LRG_LEVEL_ERROR, LRG_LEVEL_ERROR_PARSE,
		             "Project manifest has no 'project' mapping");
		return NULL;
	}
	proj_map = yaml_node_get_mapping (proj_node);

	self = lrg_project_new (dir, yaml_mapping_get_string_member (proj_map, "name"));
	self->default_level = g_strdup (yaml_mapping_get_string_member (proj_map, "default_level"));
	self->game_output = g_strdup (yaml_mapping_get_string_member (proj_map, "game_output"));

	dirs_node = yaml_mapping_get_member (proj_map, "asset_dirs");
	if (dirs_node != NULL && yaml_node_get_node_type (dirs_node) == YAML_NODE_SEQUENCE)
	{
		YamlSequence *seq = yaml_node_get_sequence (dirs_node);
		guint         i, n = yaml_sequence_get_length (seq);

		for (i = 0; i < n; i++)
		{
			YamlNode *e = yaml_sequence_get_element (seq, i);

			if (e != NULL && yaml_node_get_node_type (e) == YAML_NODE_SCALAR)
				g_ptr_array_add (self->asset_dirs,
				                 g_strdup (yaml_node_get_scalar (e)));
		}
	}

	return self;
}

gboolean
lrg_project_save (LrgProject  *self,
                  GError     **error)
{
	g_autoptr(YamlMapping)   root_map = NULL;
	g_autoptr(YamlMapping)   proj_map = NULL;
	g_autoptr(YamlSequence)  dirs_seq = NULL;
	g_autoptr(YamlNode)      root_node = NULL;
	g_autoptr(YamlDocument)  doc = NULL;
	g_autoptr(YamlGenerator) gen = NULL;
	g_autofree gchar        *data = NULL;
	g_autofree gchar        *path = NULL;
	guint                    i;

	g_return_val_if_fail (LRG_IS_PROJECT (self), FALSE);
	g_return_val_if_fail (self->root != NULL, FALSE);

	root_map = yaml_mapping_new ();
	proj_map = yaml_mapping_new ();
	dirs_seq = yaml_sequence_new ();

	if (self->name != NULL)
		yaml_mapping_set_string_member (proj_map, "name", self->name);
	if (self->default_level != NULL)
		yaml_mapping_set_string_member (proj_map, "default_level", self->default_level);
	if (self->game_output != NULL)
		yaml_mapping_set_string_member (proj_map, "game_output", self->game_output);

	for (i = 0; i < self->asset_dirs->len; i++)
		yaml_sequence_add_string_element (dirs_seq, g_ptr_array_index (self->asset_dirs, i));
	yaml_mapping_set_sequence_member (proj_map, "asset_dirs", dirs_seq);

	yaml_mapping_set_mapping_member (root_map, "project", proj_map);
	root_node = yaml_node_new_mapping (g_steal_pointer (&root_map));

	doc = yaml_document_new_with_root (root_node);
	gen = yaml_generator_new ();
	yaml_generator_set_document (gen, doc);
	data = yaml_generator_to_data (gen, NULL, error);
	if (data == NULL)
		return FALSE;

	path = g_build_filename (self->root, LRG_PROJECT_MANIFEST, NULL);
	return g_file_set_contents (path, data, -1, error);
}

const gchar *
lrg_project_get_root (LrgProject *self)
{
	g_return_val_if_fail (LRG_IS_PROJECT (self), NULL);
	return self->root;
}

const gchar *
lrg_project_get_name (LrgProject *self)
{
	g_return_val_if_fail (LRG_IS_PROJECT (self), NULL);
	return self->name;
}

void
lrg_project_set_name (LrgProject  *self,
                      const gchar *name)
{
	g_return_if_fail (LRG_IS_PROJECT (self));
	g_clear_pointer (&self->name, g_free);
	self->name = g_strdup (name);
}

const gchar *
lrg_project_get_default_level (LrgProject *self)
{
	g_return_val_if_fail (LRG_IS_PROJECT (self), NULL);
	return self->default_level;
}

void
lrg_project_set_default_level (LrgProject  *self,
                               const gchar *level)
{
	g_return_if_fail (LRG_IS_PROJECT (self));
	g_clear_pointer (&self->default_level, g_free);
	self->default_level = g_strdup (level);
}

const gchar *
lrg_project_get_game_output (LrgProject *self)
{
	g_return_val_if_fail (LRG_IS_PROJECT (self), NULL);
	return self->game_output;
}

void
lrg_project_set_game_output (LrgProject  *self,
                             const gchar *output)
{
	g_return_if_fail (LRG_IS_PROJECT (self));
	g_clear_pointer (&self->game_output, g_free);
	self->game_output = g_strdup (output);
}

void
lrg_project_add_asset_dir (LrgProject  *self,
                           const gchar *dir)
{
	g_return_if_fail (LRG_IS_PROJECT (self));
	g_return_if_fail (dir != NULL);

	g_ptr_array_add (self->asset_dirs, g_strdup (dir));
}

GPtrArray *
lrg_project_get_asset_dirs (LrgProject *self)
{
	g_return_val_if_fail (LRG_IS_PROJECT (self), NULL);
	return self->asset_dirs;
}

LrgAssetDatabase *
lrg_project_create_asset_database (LrgProject *self)
{
	LrgAssetDatabase *db;
	guint             i;

	g_return_val_if_fail (LRG_IS_PROJECT (self), NULL);

	db = lrg_asset_database_new ();
	for (i = 0; i < self->asset_dirs->len; i++)
	{
		const gchar      *rel = g_ptr_array_index (self->asset_dirs, i);
		g_autofree gchar *full = g_build_filename (self->root, rel, NULL);

		lrg_asset_database_add_search_dir (db, full);
	}

	return db;
}
