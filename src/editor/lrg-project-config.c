/* lrg-project-config.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "config.h"
#include "lrg-project-config.h"

#include <yaml-glib.h>
#include <string.h>

struct _LrgProjectConfig
{
	GObject     parent_instance;
	GPtrArray  *autoloads;     /* gchar * */
	GHashTable *settings;      /* key -> value, default layer */
	GHashTable *platforms;     /* "platform\x1fkey" -> value */
	gint64      steam_app_id;
	gboolean    steam_achievements;
};

G_DEFINE_FINAL_TYPE (LrgProjectConfig, lrg_project_config, G_TYPE_OBJECT)

typedef struct
{
	GHashTable  *dest;
	const gchar *prefix;
} CopyCtx;

static void
copy_member (YamlMapping *mapping,
             const gchar *name,
             YamlNode    *node,
             gpointer     user_data)
{
	CopyCtx     *ctx = (CopyCtx *) user_data;
	gchar       *key;
	const gchar *scalar;

	(void) mapping;
	if (node == NULL || yaml_node_get_node_type (node) != YAML_NODE_SCALAR)
		return;
	scalar = yaml_node_get_scalar (node);
	if (ctx->prefix != NULL)
		key = g_strdup_printf ("%s\x1f%s", ctx->prefix, name);
	else
		key = g_strdup (name);
	g_hash_table_insert (ctx->dest, key, g_strdup (scalar));
}

static void
copy_map (YamlMapping *map,
          GHashTable  *dest,
          const gchar *prefix)
{
	CopyCtx ctx;

	if (map == NULL)
		return;
	ctx.dest = dest;
	ctx.prefix = prefix;
	yaml_mapping_foreach_member (map, copy_member, &ctx);
}

typedef struct
{
	LrgProjectConfig *self;
} PlatCtx;

static void
platform_member (YamlMapping *mapping,
                 const gchar *name,
                 YamlNode    *node,
                 gpointer     user_data)
{
	PlatCtx *ctx = (PlatCtx *) user_data;

	(void) mapping;
	if (node == NULL || yaml_node_get_node_type (node) != YAML_NODE_MAPPING)
		return;
	copy_map (yaml_node_get_mapping (node), ctx->self->platforms, name);
}

static void
lrg_project_config_dispose (GObject *object)
{
	LrgProjectConfig *self = LRG_PROJECT_CONFIG (object);

	g_clear_pointer (&self->autoloads, g_ptr_array_unref);
	g_clear_pointer (&self->settings, g_hash_table_unref);
	g_clear_pointer (&self->platforms, g_hash_table_unref);
	G_OBJECT_CLASS (lrg_project_config_parent_class)->dispose (object);
}

static void
lrg_project_config_class_init (LrgProjectConfigClass *klass)
{
	G_OBJECT_CLASS (klass)->dispose = lrg_project_config_dispose;
}

static void
lrg_project_config_init (LrgProjectConfig *self)
{
	self->autoloads = g_ptr_array_new_with_free_func (g_free);
	self->settings = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
	self->platforms = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
}

GQuark
lrg_project_config_error_quark (void)
{
	return g_quark_from_static_string ("lrg-project-config-error");
}

LrgProjectConfig *
lrg_project_config_new_from_file (const gchar  *path,
                                  GError      **error)
{
	g_autoptr(YamlParser) parser = NULL;
	LrgProjectConfig     *self;
	YamlNode             *root;
	YamlMapping          *root_map;
	YamlNode             *node;

	g_return_val_if_fail (path != NULL, NULL);

	parser = yaml_parser_new ();
	if (!yaml_parser_load_from_file (parser, path, error))
		return NULL;
	root = yaml_parser_get_root (parser);
	if (root == NULL || yaml_node_get_node_type (root) != YAML_NODE_MAPPING)
	{
		g_set_error_literal (error, LRG_PROJECT_CONFIG_ERROR, LRG_PROJECT_CONFIG_ERROR_FAILED,
		                     "Project manifest is not a mapping");
		return NULL;
	}
	self = g_object_new (LRG_TYPE_PROJECT_CONFIG, NULL);
	root_map = yaml_node_get_mapping (root);

	node = yaml_mapping_get_member (root_map, "autoloads");
	if (node != NULL && yaml_node_get_node_type (node) == YAML_NODE_SEQUENCE)
	{
		YamlSequence *seq = yaml_node_get_sequence (node);
		guint         i;
		guint         n = yaml_sequence_get_length (seq);

		for (i = 0; i < n; i++)
		{
			YamlNode *el = yaml_sequence_get_element (seq, i);

			if (el != NULL && yaml_node_get_node_type (el) == YAML_NODE_SCALAR)
				g_ptr_array_add (self->autoloads, g_strdup (yaml_node_get_scalar (el)));
		}
	}

	node = yaml_mapping_get_member (root_map, "settings");
	if (node != NULL && yaml_node_get_node_type (node) == YAML_NODE_MAPPING)
		copy_map (yaml_node_get_mapping (node), self->settings, NULL);

	node = yaml_mapping_get_member (root_map, "platforms");
	if (node != NULL && yaml_node_get_node_type (node) == YAML_NODE_MAPPING)
	{
		PlatCtx ctx;

		ctx.self = self;
		yaml_mapping_foreach_member (yaml_node_get_mapping (node), platform_member, &ctx);
	}

	node = yaml_mapping_get_member (root_map, "steam");
	if (node != NULL && yaml_node_get_node_type (node) == YAML_NODE_MAPPING)
	{
		YamlMapping *steam = yaml_node_get_mapping (node);
		const gchar *app_id;
		const gchar *flag;

		app_id = yaml_mapping_get_string_member (steam, "app-id");
		if (app_id != NULL)
			self->steam_app_id = g_ascii_strtoll (app_id, NULL, 10);
		flag = yaml_mapping_get_string_member (steam, "achievements");
		self->steam_achievements = flag != NULL &&
		    (g_str_equal (flag, "true") || g_str_equal (flag, "yes") || g_str_equal (flag, "1"));
	}
	return self;
}

GStrv
lrg_project_config_get_autoloads (LrgProjectConfig *self)
{
	GStrv out;
	guint i;

	g_return_val_if_fail (LRG_IS_PROJECT_CONFIG (self), NULL);
	out = g_new0 (gchar *, self->autoloads->len + 1);
	for (i = 0; i < self->autoloads->len; i++)
		out[i] = g_strdup ((const gchar *) g_ptr_array_index (self->autoloads, i));
	return out;
}

const gchar *
lrg_project_config_get_platform_setting (LrgProjectConfig *self,
                                         const gchar      *platform,
                                         const gchar      *key)
{
	const gchar *value;

	g_return_val_if_fail (LRG_IS_PROJECT_CONFIG (self), NULL);
	g_return_val_if_fail (key != NULL, NULL);

	if (platform != NULL)
	{
		g_autofree gchar *composed = NULL;

		composed = g_strdup_printf ("%s\x1f%s", platform, key);
		value = g_hash_table_lookup (self->platforms, composed);
		if (value != NULL)
			return value;
	}
	return g_hash_table_lookup (self->settings, key);
}

gint64
lrg_project_config_get_steam_app_id (LrgProjectConfig *self)
{
	g_return_val_if_fail (LRG_IS_PROJECT_CONFIG (self), 0);
	return self->steam_app_id;
}

gboolean
lrg_project_config_get_steam_achievements (LrgProjectConfig *self)
{
	g_return_val_if_fail (LRG_IS_PROJECT_CONFIG (self), FALSE);
	return self->steam_achievements;
}
