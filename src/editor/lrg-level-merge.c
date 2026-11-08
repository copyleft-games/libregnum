/* lrg-level-merge.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "config.h"
#include "lrg-level-merge.h"
#include "../level/lrg-level.h"
#include "../level/lrg-level-serializer.h"
#include "../level/lrg-node.h"

#include <glib/gstdio.h>
#include <string.h>

typedef struct
{
	GHashTable *base;
	GHashTable *ours;
	GHashTable *theirs;
	GHashTable *result;
	GHashTable *conflicts;
	LrgNode    *result_root; /* synthetic root; its GUID is not stable */
} Merge;

static gboolean
index_node (LrgNode  *node,
            gpointer  user_data)
{
	GHashTable  *table = user_data;
	const gchar *guid = lrg_node_get_guid (node);

	if (guid != NULL)
		g_hash_table_insert (table, (gpointer) guid, node);
	return TRUE;
}

static GHashTable *
index_level (LrgLevel *level)
{
	GHashTable *table = g_hash_table_new (g_str_hash, g_str_equal);

	lrg_node_foreach (lrg_level_get_root (level), index_node, table);
	return table;
}

static LrgNode *
index_root (GHashTable *table)
{
	GHashTableIter iter;
	gpointer       value;

	g_hash_table_iter_init (&iter, table);
	while (g_hash_table_iter_next (&iter, NULL, &value))
	{
		LrgNode *node = value;

		if (lrg_node_get_parent (node) == NULL)
			return node;
	}
	return NULL;
}

static void
note_conflict (Merge       *merge,
               const gchar *guid)
{
	if (guid == NULL)
		return;
	if (g_hash_table_contains (merge->conflicts, guid))
		return;
	g_hash_table_add (merge->conflicts, g_strdup (guid));
}

static void
add_clone (Merge   *merge,
           LrgNode *theirs_node)
{
	LrgNode     *parent_t;
	LrgNode     *parent_r;
	LrgNode     *created;
	const gchar *guid;
	const gchar *parent_guid;

	parent_t = lrg_node_get_parent (theirs_node);
	guid = lrg_node_get_guid (theirs_node);
	/* The level's synthetic root is rewritten on every load, so its
	 * GUID never matches across the three sides. It is not content. */
	if (parent_t == NULL || guid == NULL)
		return;
	parent_guid = lrg_node_get_guid (parent_t);
	parent_r = g_hash_table_lookup (merge->result, parent_guid);
	/* A node added under the synthetic root attaches to the result
	 * root. The two roots do not share a GUID. */
	if (parent_r == NULL && lrg_node_get_parent (parent_t) == NULL)
		parent_r = merge->result_root;
	if (parent_r == NULL)
	{
		note_conflict (merge, guid);
		return;
	}
	created = lrg_node_new (NULL);
	lrg_node_set_guid (created, guid);
	lrg_node_copy_state (created, theirs_node);
	lrg_node_add_child (parent_r, created);
	g_hash_table_insert (merge->result, (gpointer) lrg_node_get_guid (created), created);
	g_object_unref (created);
}

static gboolean
merge_theirs (LrgNode  *node,
              gpointer  user_data)
{
	Merge       *merge = user_data;
	const gchar *guid;
	LrgNode     *base_n;
	LrgNode     *ours_n;
	LrgNode     *result_n;

	guid = lrg_node_get_guid (node);
	if (guid == NULL)
		return TRUE;
	base_n = g_hash_table_lookup (merge->base, guid);
	ours_n = g_hash_table_lookup (merge->ours, guid);
	result_n = g_hash_table_lookup (merge->result, guid);

	if (result_n == NULL)
	{
		/* Not in ours. Take theirs when ours did not delete a
		 * node theirs still changed, and when theirs added it. */
		if (base_n == NULL)
			add_clone (merge, node);
		else if (ours_n == NULL && !lrg_node_state_equal (base_n, node))
			note_conflict (merge, guid);
		return TRUE;
	}
	if (base_n == NULL)
	{
		if (!lrg_node_state_equal (ours_n, node))
			note_conflict (merge, guid);
		return TRUE;
	}
	if (!lrg_node_state_equal (ours_n, base_n) &&
	    !lrg_node_state_equal (node, base_n) &&
	    !lrg_node_state_equal (ours_n, node))
		note_conflict (merge, guid);
	else if (lrg_node_state_equal (ours_n, base_n) &&
	         !lrg_node_state_equal (node, base_n))
		lrg_node_copy_state (result_n, node);
	return TRUE;
}

static void
merge_deletions (Merge *merge)
{
	GHashTableIter  iter;
	gpointer        key;
	gpointer        value;

	g_hash_table_iter_init (&iter, merge->base);
	while (g_hash_table_iter_next (&iter, &key, &value))
	{
		const gchar *guid = key;
		LrgNode     *base_n = value;
		LrgNode     *ours_n;
		LrgNode     *result_n;
		LrgNode     *parent;

		if (g_hash_table_contains (merge->theirs, guid))
			continue;
		ours_n = g_hash_table_lookup (merge->ours, guid);
		if (ours_n == NULL)
			continue;
		if (!lrg_node_state_equal (ours_n, base_n))
		{
			note_conflict (merge, guid);
			continue;
		}
		result_n = g_hash_table_lookup (merge->result, guid);
		if (result_n == NULL)
			continue;
		parent = lrg_node_get_parent (result_n);
		if (parent == NULL)
			continue;
		lrg_node_remove_child (parent, result_n);
		g_hash_table_remove (merge->result, guid);
	}
}

static GStrv
conflict_list (GHashTable *conflicts)
{
	GStrv   out;
	GList  *keys;
	GList  *l;
	guint   i;

	keys = g_hash_table_get_keys (conflicts);
	out = g_new0 (gchar *, g_list_length (keys) + 1);
	i = 0;
	for (l = keys; l != NULL; l = l->next)
		out[i++] = g_strdup (l->data);
	g_list_free (keys);
	return out;
}

LrgLevel *
lrg_level_merge (LrgLevel  *base,
                 LrgLevel  *ours,
                 LrgLevel  *theirs,
                 GStrv     *conflicts,
                 GError   **error)
{
	g_autoptr(LrgLevelSerializer) ser = NULL;
	g_autofree gchar             *data = NULL;
	g_autoptr(LrgLevel)           result = NULL;
	Merge                         merge;

	g_return_val_if_fail (LRG_IS_LEVEL (base), NULL);
	g_return_val_if_fail (LRG_IS_LEVEL (ours), NULL);
	g_return_val_if_fail (LRG_IS_LEVEL (theirs), NULL);

	ser = lrg_level_serializer_new ();
	data = lrg_level_serializer_save_to_data (ser, ours, NULL);
	if (data == NULL)
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED, "could not copy our level");
		return NULL;
	}
	result = lrg_level_serializer_load_from_data (ser, data, -1, error);
	if (result == NULL)
		return NULL;

	memset (&merge, 0, sizeof (merge));
	merge.base = index_level (base);
	merge.ours = index_level (ours);
	merge.theirs = index_level (theirs);
	merge.result = index_level (result);
	merge.result_root = index_root (merge.result);
	merge.conflicts = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);

	lrg_node_foreach (lrg_level_get_root (theirs), merge_theirs, &merge);
	merge_deletions (&merge);
	if (conflicts != NULL)
		*conflicts = conflict_list (merge.conflicts);

	g_hash_table_unref (merge.base);
	g_hash_table_unref (merge.ours);
	g_hash_table_unref (merge.theirs);
	g_hash_table_unref (merge.result);
	g_hash_table_unref (merge.conflicts);
	return g_steal_pointer (&result);
}

gboolean
lrg_level_merge_files (const gchar  *base_path,
                       const gchar  *ours_path,
                       const gchar  *theirs_path,
                       const gchar  *out_path,
                       GStrv        *conflicts,
                       GError      **error)
{
	g_autoptr(LrgLevelSerializer) ser = NULL;
	g_autoptr(LrgLevel)           base = NULL;
	g_autoptr(LrgLevel)           ours = NULL;
	g_autoptr(LrgLevel)           theirs = NULL;
	g_autoptr(LrgLevel)           merged = NULL;

	g_return_val_if_fail (base_path != NULL, FALSE);
	g_return_val_if_fail (ours_path != NULL, FALSE);
	g_return_val_if_fail (theirs_path != NULL, FALSE);
	g_return_val_if_fail (out_path != NULL, FALSE);

	ser = lrg_level_serializer_new ();
	base = lrg_level_serializer_load_from_file (ser, base_path, error);
	if (base == NULL)
		return FALSE;
	ours = lrg_level_serializer_load_from_file (ser, ours_path, error);
	if (ours == NULL)
		return FALSE;
	theirs = lrg_level_serializer_load_from_file (ser, theirs_path, error);
	if (theirs == NULL)
		return FALSE;

	merged = lrg_level_merge (base, ours, theirs, conflicts, error);
	if (merged == NULL)
		return FALSE;
	return lrg_level_serializer_save_to_file (ser, merged, out_path, error);
}

static gboolean
run_git (const gchar  *repo,
         gchar       **argv,
         gchar       **stdout_text,
         GError      **error)
{
	g_autofree gchar *git = NULL;
	g_autofree gchar *sink = NULL;
	g_autoptr(GError) local = NULL;
	gint              status = 1;

	git = g_find_program_in_path ("git");
	if (git == NULL)
	{
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "git is not installed");
		return FALSE;
	}
	/* git rev-parse --is-inside-work-tree prints "true". A caller that
	 * does not want that text still must not inherit the child's stdout. */
	if (stdout_text == NULL)
		stdout_text = &sink;
	if (!g_spawn_sync (repo, argv, NULL, G_SPAWN_SEARCH_PATH,
	                   NULL, NULL, stdout_text, NULL, &status, &local))
	{
		g_propagate_error (error, g_steal_pointer (&local));
		return FALSE;
	}
	if (!g_spawn_check_wait_status (status, &local))
	{
		g_propagate_error (error, g_steal_pointer (&local));
		return FALSE;
	}
	return TRUE;
}

gboolean
lrg_level_merge_install_driver (const gchar  *repo,
                                const gchar  *driver_command,
                                GError      **error)
{
	const gchar      *command;
	gchar            *check_argv[4];
	gchar            *name_argv[6];
	gchar            *driver_argv[6];
	g_autofree gchar *attrs = NULL;
	g_autofree gchar *existing = NULL;
	g_autofree gchar *next = NULL;

	g_return_val_if_fail (repo != NULL, FALSE);

	command = (driver_command != NULL && driver_command[0] != '\0')
	          ? driver_command
	          : "lrgeditor --merge %O %A %B";

	check_argv[0] = (gchar *) "git";
	check_argv[1] = (gchar *) "rev-parse";
	check_argv[2] = (gchar *) "--is-inside-work-tree";
	check_argv[3] = NULL;
	if (!run_git (repo, check_argv, NULL, error))
		return FALSE;

	name_argv[0] = (gchar *) "git";
	name_argv[1] = (gchar *) "config";
	name_argv[2] = (gchar *) "merge.rlevel.name";
	name_argv[3] = (gchar *) "libregnum rlevel merge";
	name_argv[4] = NULL;
	if (!run_git (repo, name_argv, NULL, error))
		return FALSE;

	driver_argv[0] = (gchar *) "git";
	driver_argv[1] = (gchar *) "config";
	driver_argv[2] = (gchar *) "merge.rlevel.driver";
	driver_argv[3] = (gchar *) command;
	driver_argv[4] = NULL;
	if (!run_git (repo, driver_argv, NULL, error))
		return FALSE;

	attrs = g_build_filename (repo, ".gitattributes", NULL);
	if (!g_file_get_contents (attrs, &existing, NULL, NULL))
		existing = g_strdup ("");
	if (strstr (existing, "*.rlevel merge=rlevel") != NULL)
		return TRUE;

	if (existing[0] != '\0' && !g_str_has_suffix (existing, "\n"))
		next = g_strconcat (existing, "\n*.rlevel merge=rlevel\n", NULL);
	else
		next = g_strconcat (existing, "*.rlevel merge=rlevel\n", NULL);
	if (!g_file_set_contents (attrs, next, -1, error))
		return FALSE;
	return TRUE;
}
