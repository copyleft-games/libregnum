/* lrg-project-template.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "config.h"
#include "lrg-project-template.h"

#include <string.h>

static const LrgProjectTemplateInfo templates[] = {
	{ "empty", "Empty", "A project with one level and an asset directory" },
	{ "mod", "Mod", "An empty project plus a script mod" }
};

GQuark
lrg_project_template_error_quark (void)
{
	return g_quark_from_static_string ("lrg-project-template-error");
}

guint
lrg_project_template_count (void)
{
	return G_N_ELEMENTS (templates);
}

const LrgProjectTemplateInfo *
lrg_project_template_get (guint index)
{
	if (index >= G_N_ELEMENTS (templates))
		return NULL;
	return &templates[index];
}

static gboolean
write_text (const gchar  *path,
            const gchar  *text,
            GError      **error)
{
	g_autofree gchar *parent = NULL;

	parent = g_path_get_dirname (path);
	if (g_mkdir_with_parents (parent, 0755) != 0)
	{
		g_set_error (error, LRG_PROJECT_TEMPLATE_ERROR, LRG_PROJECT_TEMPLATE_ERROR_FAILED,
		             "Could not create '%s'", parent);
		return FALSE;
	}
	return g_file_set_contents (path, text, -1, error);
}

static gchar *
yaml_name (const gchar *name)
{
	GString *s;
	const gchar *p;

	s = g_string_new ("");
	for (p = name; *p != '\0'; p++)
	{
		if (*p == '"' || *p == '\\')
			g_string_append_c (s, '\\');
		g_string_append_c (s, *p);
	}
	return g_string_free (s, FALSE);
}

gboolean
lrg_project_template_instantiate (const gchar  *id,
                                  const gchar  *dest,
                                  const gchar  *name,
                                  GError      **error)
{
	g_autofree gchar *manifest = NULL;
	g_autofree gchar *manifest_path = NULL;
	g_autofree gchar *level_path = NULL;
	g_autofree gchar *assets = NULL;
	g_autofree gchar *escaped = NULL;
	gboolean          mod;
	const gchar      *use_name;

	g_return_val_if_fail (dest != NULL, FALSE);
	if (id == NULL || (!g_str_equal (id, "empty") && !g_str_equal (id, "mod")))
	{
		g_set_error (error, LRG_PROJECT_TEMPLATE_ERROR, LRG_PROJECT_TEMPLATE_ERROR_UNKNOWN,
		             "Unknown template '%s'", (id != NULL) ? id : "(null)");
		return FALSE;
	}
	manifest_path = g_build_filename (dest, "project.ryaml", NULL);
	if (g_file_test (manifest_path, G_FILE_TEST_EXISTS))
	{
		g_set_error (error, LRG_PROJECT_TEMPLATE_ERROR, LRG_PROJECT_TEMPLATE_ERROR_EXISTS,
		             "'%s' already contains a project", dest);
		return FALSE;
	}
	use_name = (name != NULL && name[0] != '\0') ? name : "Game";
	escaped = yaml_name (use_name);
	manifest = g_strdup_printf (
		"project:\n"
		"  name: \"%s\"\n"
		"  default_level: levels/main.rlevel\n"
		"  game_output: build/game.so\n"
		"  scripts_dir: scripts\n"
		"  asset_dirs:\n"
		"    - assets\n"
		"autoloads: []\n"
		"settings:\n"
		"  width: \"1280\"\n"
		"  height: \"720\"\n"
		"platforms:\n"
		"  linux:\n"
		"    fullscreen: \"false\"\n"
		"steam:\n"
		"  app-id: \"0\"\n"
		"  achievements: \"false\"\n",
		escaped);
	if (!write_text (manifest_path, manifest, error))
		return FALSE;
	level_path = g_build_filename (dest, "levels", "main.rlevel", NULL);
	if (!write_text (level_path,
	                 "type: level\n"
	                 "name: main\n"
	                 "default_view: 3d\n"
	                 "nodes:\n"
	                 "  - name: Root\n"
	                 "    guid: 00000000-0000-4000-8000-000000000001\n",
	                 error))
		return FALSE;
	assets = g_build_filename (dest, "assets", NULL);
	if (g_mkdir_with_parents (assets, 0755) != 0)
	{
		g_set_error_literal (error, LRG_PROJECT_TEMPLATE_ERROR, LRG_PROJECT_TEMPLATE_ERROR_FAILED,
		                     "Could not create the asset directory");
		return FALSE;
	}
	mod = g_str_equal (id, "mod");
	if (mod)
	{
		g_autofree gchar *mod_yaml = NULL;
		g_autofree gchar *script = NULL;

		mod_yaml = g_build_filename (dest, "mods", "example", "mod.yaml", NULL);
		script = g_build_filename (dest, "mods", "example", "mod.lua", NULL);
		if (!write_text (mod_yaml,
		                 "id: example\n"
		                 "name: Example\n"
		                 "version: 1.0.0\n"
		                 "type: script\n"
		                 "priority: normal\n"
		                 "entry_point: mod.lua\n",
		                 error))
			return FALSE;
		if (!write_text (script, "-- example mod\nreturn true\n", error))
			return FALSE;
	}
	return TRUE;
}
