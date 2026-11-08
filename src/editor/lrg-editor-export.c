/* lrg-editor-export.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "config.h"
#include "lrg-editor-export.h"
#include "../core/lrg-pack-writer.h"

#include <json-glib/json-glib.h>
#include <string.h>

static const gchar *
param_string (JsonObject  *params,
              const gchar *key)
{
	JsonNode *node;

	if (params == NULL || !json_object_has_member (params, key))
		return NULL;
	node = json_object_get_member (params, key);
	if (node == NULL || !JSON_NODE_HOLDS_VALUE (node))
		return NULL;
	return json_node_get_string (node);
}

static JsonNode *
m_profiles (LrgEditorServer *server,
            JsonObject      *params,
            gpointer         user_data,
            GError         **error)
{
	JsonArray          *arr;
	const gchar * const *ids;
	guint                i;

	(void) server;
	(void) params;
	(void) user_data;
	(void) error;
	arr = json_array_new ();
	ids = lrg_pack_writer_profile_ids ();
	for (i = 0; ids[i] != NULL; i++)
		json_array_add_string_element (arr, ids[i]);
	return json_node_init_array (json_node_new (JSON_NODE_ARRAY), arr);
}

static JsonNode *
m_run (LrgEditorServer *server,
       JsonObject      *params,
       gpointer         user_data,
       GError         **error)
{
	const gchar *fallback = (const gchar *) user_data;
	const gchar *project;
	const gchar *profile;
	const gchar *output;
	g_autoptr(LrgPackWriter) writer = NULL;
	JsonObject *obj;

	(void) server;
	project = param_string (params, "project");
	if (project == NULL)
		project = fallback;
	profile = param_string (params, "profile");
	output = param_string (params, "output");
	if (project == NULL || profile == NULL || output == NULL)
	{
		g_set_error_literal (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS,
		                     "export.run needs profile, output and a project");
		return NULL;
	}
	writer = lrg_pack_writer_new ();
	lrg_pack_writer_set_project (writer, project);
	lrg_pack_writer_set_cook (writer, FALSE);
	if (!lrg_pack_writer_scan (writer, error))
		return NULL;
	if (!lrg_pack_writer_export (writer, profile, output, error))
		return NULL;
	obj = json_object_new ();
	json_object_set_boolean_member (obj, "ok", TRUE);
	json_object_set_string_member (obj, "profile", profile);
	json_object_set_string_member (obj, "output", output);
	json_object_set_string_member (obj, "log", lrg_pack_writer_get_log (writer));
	return json_node_init_object (json_node_new (JSON_NODE_OBJECT), obj);
}

void
lrg_editor_export_register (LrgEditorServer *server,
                            const gchar     *project_dir)
{
	gchar *copy;

	g_return_if_fail (LRG_IS_EDITOR_SERVER (server));
	copy = g_strdup (project_dir);
	lrg_editor_server_register_method (server, "export.profiles", m_profiles, NULL, NULL);
	lrg_editor_server_register_method (server, "export.run", m_run, copy, g_free);
}
