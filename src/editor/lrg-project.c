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
#include "../physics/lrg-physics-layers.h"
#include <yaml-glib.h>
#include <string.h>

struct _LrgProject
{
	GObject parent_instance;

	gchar     *root;
	gchar     *name;
	gchar     *default_level;
	gchar     *game_output;
	gchar     *run_command;
	gchar     *scripts_dir;
	GPtrArray *asset_dirs;   /* gchar* */

	/* physics: block: the layer table, plus the parsed block itself so
	 * keys other than layers/no_collide (world settings) survive a save */
	LrgPhysicsLayers   *physics_layers;
	LrgPhysicsSettings *physics_settings;  /* world settings of the block */
	YamlNode           *physics_node;

	/* Keys this type does not model (e.g. 0.9.0 runtime settings such as
	 * store_prefab_instances, or another tool's data), kept with their
	 * parsed values so lrg_project_save() writes them back unchanged:
	 * parallel arrays of key (gchar*) and value (YamlNode*) */
	GPtrArray *extra_keys;        /* inside project: */
	GPtrArray *extra_nodes;
	GPtrArray *extra_root_keys;   /* next to project: */
	GPtrArray *extra_root_nodes;
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
	g_clear_pointer (&self->run_command, g_free);
	g_clear_pointer (&self->scripts_dir, g_free);
	g_clear_pointer (&self->asset_dirs, g_ptr_array_unref);
	g_clear_object (&self->physics_layers);
	g_clear_pointer (&self->physics_settings, lrg_physics_settings_free);
	g_clear_pointer (&self->physics_node, yaml_node_unref);
	g_clear_pointer (&self->extra_keys, g_ptr_array_unref);
	g_clear_pointer (&self->extra_nodes, g_ptr_array_unref);
	g_clear_pointer (&self->extra_root_keys, g_ptr_array_unref);
	g_clear_pointer (&self->extra_root_nodes, g_ptr_array_unref);

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
	self->physics_layers = lrg_physics_layers_new_default ();
	self->physics_settings = lrg_physics_settings_new ();
	self->extra_keys = g_ptr_array_new_with_free_func (g_free);
	self->extra_nodes = g_ptr_array_new_with_free_func ((GDestroyNotify) yaml_node_unref);
	self->extra_root_keys = g_ptr_array_new_with_free_func (g_free);
	self->extra_root_nodes = g_ptr_array_new_with_free_func ((GDestroyNotify) yaml_node_unref);
}

/* keep_unknown_members: remembers every member of @map whose key is not
 * in @known (NULL-terminated), in file order */
static void
keep_unknown_members (YamlMapping        *map,
                      const gchar * const *known,
                      GPtrArray          *keys,
                      GPtrArray          *nodes)
{
	guint i;
	guint n = yaml_mapping_get_size (map);

	for (i = 0; i < n; i++)
	{
		const gchar *key = yaml_mapping_get_key (map, i);
		YamlNode    *value = yaml_mapping_get_value (map, i);

		if (key == NULL || value == NULL || g_strv_contains (known, key))
			continue;
		g_ptr_array_add (keys, g_strdup (key));
		g_ptr_array_add (nodes, yaml_node_ref (value));
	}
}

/* write_kept_members: appends the remembered members to @map */
static void
write_kept_members (YamlMapping *map,
                    GPtrArray   *keys,
                    GPtrArray   *nodes)
{
	guint i;

	for (i = 0; i < keys->len; i++)
		if (!yaml_mapping_has_member (map, g_ptr_array_index (keys, i)))
			yaml_mapping_set_member (map, g_ptr_array_index (keys, i), g_ptr_array_index (nodes, i));
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
	if (yaml_mapping_has_member (proj_map, "run_command"))
		self->run_command = g_strdup (yaml_mapping_get_string_member (proj_map, "run_command"));
	if (yaml_mapping_has_member (proj_map, "scripts_dir"))
		self->scripts_dir = g_strdup (yaml_mapping_get_string_member (proj_map, "scripts_dir"));

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

	/* The physics: block (root level, or inside project:). A broken
	 * block does not stop the project from opening: it is reported,
	 * the table is left empty (everything collides), and the block is
	 * kept so a save does not lose it */
	{
		YamlNode          *block = lrg_physics_layers_find_block (root);
		g_autoptr(GError)  physics_error = NULL;

		if (!lrg_physics_layers_load_yaml (self->physics_layers, block, &physics_error))
		{
			g_warning ("%s: physics block ignored: %s", path, physics_error->message);
			lrg_physics_layers_reset (self->physics_layers);
		}
		g_clear_error (&physics_error);
		if (!lrg_physics_settings_load_yaml (self->physics_settings, block, &physics_error))
		{
			g_warning ("%s: physics settings ignored: %s", path, physics_error->message);
			lrg_physics_settings_init_default (self->physics_settings);
		}
		if (block != NULL)
			self->physics_node = yaml_node_ref (block);
	}

	/* Everything else survives a save */
	{
		static const gchar * const project_keys[] = {
			"name", "default_level", "game_output", "run_command", "scripts_dir", "asset_dirs",
			"physics", NULL
		};
		static const gchar * const root_keys[] = { "project", "physics", NULL };

		keep_unknown_members (proj_map, project_keys, self->extra_keys, self->extra_nodes);
		keep_unknown_members (root_map, root_keys, self->extra_root_keys, self->extra_root_nodes);
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
	if (self->run_command != NULL)
		yaml_mapping_set_string_member (proj_map, "run_command", self->run_command);
	if (self->scripts_dir != NULL)
		yaml_mapping_set_string_member (proj_map, "scripts_dir", self->scripts_dir);

	for (i = 0; i < self->asset_dirs->len; i++)
		yaml_sequence_add_string_element (dirs_seq, g_ptr_array_index (self->asset_dirs, i));
	yaml_mapping_set_sequence_member (proj_map, "asset_dirs", dirs_seq);
	write_kept_members (proj_map, self->extra_keys, self->extra_nodes);

	yaml_mapping_set_mapping_member (root_map, "project", proj_map);

	/* physics: is always written at the root, next to project: (an old
	 * block nested in project: moves there); its other keys are kept */
	{
		g_autoptr(YamlNode)    physics_copy = NULL;
		g_autoptr(YamlMapping) physics_map = NULL;

		if (self->physics_node != NULL
		    && yaml_node_get_node_type (self->physics_node) == YAML_NODE_MAPPING)
		{
			physics_copy = yaml_node_copy (self->physics_node);
			physics_map = yaml_mapping_ref (yaml_node_get_mapping (physics_copy));
		}
		else
			physics_map = yaml_mapping_new ();
		lrg_physics_layers_write_yaml (self->physics_layers, physics_map);
		lrg_physics_settings_write_yaml (self->physics_settings, physics_map);
		yaml_mapping_set_mapping_member (root_map, "physics", physics_map);
	}
	write_kept_members (root_map, self->extra_root_keys, self->extra_root_nodes);
	/* yaml_node_new_mapping refs the mapping (transfer none); let the autoptr
	 * drop our reference. g_steal_pointer would leak it and the whole tree. */
	root_node = yaml_node_new_mapping (root_map);

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

const gchar *
lrg_project_get_run_command (LrgProject *self)
{
	g_return_val_if_fail (LRG_IS_PROJECT (self), NULL);
	return self->run_command;
}

void
lrg_project_set_run_command (LrgProject  *self,
                             const gchar *command)
{
	g_return_if_fail (LRG_IS_PROJECT (self));
	g_clear_pointer (&self->run_command, g_free);
	if (command != NULL && *command != '\0')
		self->run_command = g_strdup (command);
}

const gchar *
lrg_project_get_scripts_dir (LrgProject *self)
{
	g_return_val_if_fail (LRG_IS_PROJECT (self), NULL);
	return (self->scripts_dir != NULL) ? self->scripts_dir : "scripts";
}

void
lrg_project_set_scripts_dir (LrgProject  *self,
                             const gchar *dir)
{
	g_return_if_fail (LRG_IS_PROJECT (self));
	g_clear_pointer (&self->scripts_dir, g_free);
	if (dir != NULL && *dir != '\0')
		self->scripts_dir = g_strdup (dir);
}

void
lrg_project_clear_asset_dirs (LrgProject *self)
{
	g_return_if_fail (LRG_IS_PROJECT (self));
	g_ptr_array_set_size (self->asset_dirs, 0);
}

gchar *
lrg_project_resolve (LrgProject  *self,
                     const gchar *path)
{
	g_return_val_if_fail (LRG_IS_PROJECT (self), NULL);
	g_return_val_if_fail (path != NULL, NULL);

	if (g_path_is_absolute (path) || self->root == NULL)
		return g_canonicalize_filename (path, NULL);
	return g_canonicalize_filename (path, self->root);
}

gchar *
lrg_project_relativize (LrgProject  *self,
                        const gchar *path)
{
	g_autofree gchar *root = NULL;
	g_autofree gchar *abs = NULL;
	gsize             len;

	g_return_val_if_fail (LRG_IS_PROJECT (self), NULL);
	g_return_val_if_fail (path != NULL, NULL);

	if (self->root == NULL)
		return g_strdup (path);

	root = g_canonicalize_filename (self->root, NULL);
	abs = g_canonicalize_filename (path, root);
	len = strlen (root);

	/* Inside the root: strip "root/" */
	if (g_str_has_prefix (abs, root) && abs[len] == G_DIR_SEPARATOR)
		return g_strdup (abs + len + 1);
	if (g_strcmp0 (abs, root) == 0)
		return g_strdup (".");
	return g_strdup (path);
}

LrgPhysicsLayers *
lrg_project_get_physics_layers (LrgProject *self)
{
	g_return_val_if_fail (LRG_IS_PROJECT (self), NULL);
	return self->physics_layers;
}

LrgPhysicsSettings *
lrg_project_get_physics_settings (LrgProject *self)
{
	g_return_val_if_fail (LRG_IS_PROJECT (self), NULL);
	return self->physics_settings;
}

gboolean
lrg_project_set_physics_settings (LrgProject                *self,
                                  const LrgPhysicsSettings  *settings,
                                  GError                   **error)
{
	g_return_val_if_fail (LRG_IS_PROJECT (self), FALSE);
	g_return_val_if_fail (settings != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	if (!lrg_physics_settings_validate (settings, error))
		return FALSE;
	*self->physics_settings = *settings;
	return TRUE;
}
