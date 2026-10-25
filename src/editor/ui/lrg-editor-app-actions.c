/* lrg-editor-app-actions.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Every editor command as a named action (menus, toolbar, shortcuts,
 * command palette, IPC "action.run"), the application's extra IPC methods,
 * and keyboard shortcut dispatch.
 *
 * Default shortcuts follow Godot 4 and Unity where they agree (Q/W/E/R
 * tools, F to focus, Ctrl+D duplicate, F2 rename) and common editor
 * conventions elsewhere (Ctrl+P quick open, Ctrl+Shift+P command palette).
 */

#include "lrg-editor-app-private.h"
#include "../../core/lrg-engine.h"
#include "../../core/lrg-registry.h"
#include "../lrg-asset-database.h"
#include "../lrg-scene-import.h"
#include <string.h>

#define APP(p) LRG_EDITOR_APP (p)

static LrgNode *
primary (LrgEditorApp *self)
{
	return lrg_editor_selection_get_primary (lrg_editor_get_selection (self->editor));
}

/* ==========================================================================
 * Enabled / checked queries
 * ========================================================================== */

static gboolean
has_selection (gpointer data)
{
	return lrg_editor_selection_get_count (lrg_editor_get_selection (APP (data)->editor)) > 0;
}

static gboolean
can_undo (gpointer data)
{
	return lrg_editor_can_undo (APP (data)->editor);
}

static gboolean
can_redo (gpointer data)
{
	return lrg_editor_can_redo (APP (data)->editor);
}

static gboolean
has_clipboard (gpointer data)
{
	return lrg_editor_get_clipboard (APP (data)->editor) != NULL ||
	       !APP (data)->headless;   /* the system clipboard may hold YAML */
}

static gboolean
has_path (gpointer data)
{
	return lrg_editor_get_path (APP (data)->editor) != NULL;
}

static gboolean
has_project (gpointer data)
{
	return APP (data)->project != NULL;
}

static gboolean
primary_has_scripts (gpointer data)
{
	LrgNode *n = primary (APP (data));

	return n != NULL && lrg_node_get_scripts (n)->len > 0;
}

static gboolean
tool_is_select (gpointer data)
{
	return lrg_editor_get_tool (APP (data)->editor) == LRG_EDITOR_TOOL_SELECT;
}

static gboolean
tool_is_move (gpointer data)
{
	return lrg_editor_get_tool (APP (data)->editor) == LRG_EDITOR_TOOL_TRANSLATE;
}

static gboolean
tool_is_rotate (gpointer data)
{
	return lrg_editor_get_tool (APP (data)->editor) == LRG_EDITOR_TOOL_ROTATE;
}

static gboolean
tool_is_scale (gpointer data)
{
	return lrg_editor_get_tool (APP (data)->editor) == LRG_EDITOR_TOOL_SCALE;
}

static gboolean
space_is_local (gpointer data)
{
	return lrg_editor_get_space (APP (data)->editor) == LRG_EDITOR_SPACE_LOCAL;
}

static gboolean
snap_on (gpointer data)
{
	return lrg_editor_get_snap_enabled (APP (data)->editor);
}

static gboolean
is_2d (gpointer data)
{
	return APP (data)->view_2d;
}

static gboolean
is_ortho (gpointer data)
{
	return APP (data)->view.orthographic;
}

static gboolean
grid_on (gpointer data)
{
	return APP (data)->show_grid;
}

static gboolean
icons_on (gpointer data)
{
	return APP (data)->show_icons;
}

static gboolean
wire_on (gpointer data)
{
	return APP (data)->wireframe;
}

static gboolean
stats_on (gpointer data)
{
	return APP (data)->show_stats;
}

static gboolean
is_playing (gpointer data)
{
	return APP (data)->playing;
}

static gboolean
is_paused (gpointer data)
{
	return APP (data)->playing && APP (data)->paused;
}

static gboolean
is_running (gpointer data)
{
	return APP (data)->run_proc != NULL;
}

static gboolean
not_running (gpointer data)
{
	return APP (data)->run_proc == NULL;
}

/* ==========================================================================
 * Activations
 * ========================================================================== */

static gchar *
levels_dir (LrgEditorApp *self)
{
	if (self->project_dir != NULL)
	{
		g_autofree gchar *levels = g_build_filename (self->project_dir, "levels", NULL);

		if (g_file_test (levels, G_FILE_TEST_IS_DIR))
			return g_steal_pointer (&levels);
		return g_strdup (self->project_dir);
	}
	if (lrg_editor_get_path (self->editor) != NULL)
		return g_path_get_dirname (lrg_editor_get_path (self->editor));
	return g_get_current_dir ();
}

static void
a_new_level (gpointer data)
{
	editor_app_request (APP (data), EDITOR_PENDING_NEW_LEVEL, NULL);
}

static void
a_open_level (gpointer data)
{
	g_autofree gchar *dir = levels_dir (APP (data));

	editor_file_dialog_open (APP (data), EDITOR_FILE_OPEN_LEVEL, dir, ".rlevel");
}

static void
a_save (gpointer data)
{
	editor_app_save (APP (data));
}

static void
a_save_as (gpointer data)
{
	g_autofree gchar *dir = levels_dir (APP (data));

	editor_file_dialog_open (APP (data), EDITOR_FILE_SAVE_LEVEL_AS, dir, ".rlevel");
}

static void
a_reload (gpointer data)
{
	editor_app_request (APP (data), EDITOR_PENDING_RELOAD, NULL);
}

static void
a_new_project (gpointer data)
{
	editor_dialog_open (APP (data), EDITOR_DIALOG_NEW_PROJECT);
}

static void
a_open_project (gpointer data)
{
	LrgEditorApp     *self = APP (data);
	g_autofree gchar *dir = (self->project_dir != NULL) ? g_path_get_dirname (self->project_dir)
	                                                    : g_get_current_dir ();

	editor_file_dialog_open (self, EDITOR_FILE_OPEN_PROJECT, dir, NULL);
}

static void
a_project_settings (gpointer data)
{
	editor_dialog_open (APP (data), EDITOR_DIALOG_PROJECT_SETTINGS);
}

static void
a_import_scene (gpointer data)
{
	g_autofree gchar *dir = levels_dir (APP (data));

	editor_file_dialog_open (APP (data), EDITOR_FILE_IMPORT_SCENE, dir, ".yaml");
}

static void
a_quit (gpointer data)
{
	lrg_editor_app_request_quit (APP (data));
}

static void
a_undo (gpointer data)
{
	lrg_editor_undo (APP (data)->editor);
}

static void
a_redo (gpointer data)
{
	lrg_editor_redo (APP (data)->editor);
}

static void
a_copy (gpointer data)
{
	LrgEditorApp     *self = APP (data);
	g_autofree gchar *yaml = lrg_editor_copy_selection (self->editor);

	/* Also on the system clipboard: paste into a .rlevel in Emacs. */
	if (yaml != NULL)
	{
		lrg_gui_context_set_clipboard (self->gui, yaml);
		editor_app_set_status (self, "Copied %u node(s)",
		                       lrg_editor_selection_get_count (lrg_editor_get_selection (self->editor)));
	}
}

static void
a_cut (gpointer data)
{
	LrgEditorApp     *self = APP (data);
	g_autofree gchar *yaml = lrg_editor_cut_selection (self->editor);

	if (yaml != NULL)
		lrg_gui_context_set_clipboard (self->gui, yaml);
}

static void
a_paste (gpointer data)
{
	LrgEditorApp         *self = APP (data);
	g_autofree gchar     *text = lrg_gui_context_get_clipboard (self->gui);
	g_autoptr(GPtrArray)  created = NULL;
	g_autoptr(GError)     error = NULL;
	LrgNode              *parent = NULL;

	/* Prefer the system clipboard when it holds a level. */
	if (text == NULL || strstr (text, "nodes:") == NULL)
		g_clear_pointer (&text, g_free);

	/* Paste as siblings of the selection. */
	if (primary (self) != NULL && lrg_node_get_parent (primary (self)) != NULL)
		parent = lrg_node_get_parent (primary (self));

	created = lrg_editor_paste (self->editor, text, parent, &error);
	if (created == NULL)
		editor_app_notify (self, 2, "Paste: %s", error->message);
	else
		editor_app_set_status (self, "Pasted %u node(s)", created->len);
}

static void
a_duplicate (gpointer data)
{
	g_autoptr(GPtrArray) created = lrg_editor_duplicate_selection (APP (data)->editor);

	editor_app_set_status (APP (data), "Duplicated %u node(s)", created->len);
}

static void
a_delete (gpointer data)
{
	guint n = lrg_editor_delete_selection (APP (data)->editor);

	editor_app_set_status (APP (data), "Deleted %u node(s)", n);
}

static void
a_select_all (gpointer data)
{
	lrg_editor_select_all (APP (data)->editor);
}

static void
a_deselect (gpointer data)
{
	lrg_editor_select (APP (data)->editor, NULL, FALSE);
}

static void
a_find (gpointer data)
{
	LrgEditorApp *self = APP (data);

	lrg_gui_dock_open_panel (self->dock, "hierarchy");
	g_free (self->rename_guid);
	self->rename_guid = g_strdup ("##filter");   /* hierarchy focuses its filter */
}

static void
a_preferences (gpointer data)
{
	editor_dialog_open (APP (data), EDITOR_DIALOG_PREFERENCES);
}

static void
a_rename (gpointer data)
{
	LrgEditorApp *self = APP (data);
	LrgNode      *n = primary (self);

	if (n == NULL)
		return;
	lrg_gui_dock_open_panel (self->dock, "hierarchy");
	g_free (self->rename_guid);
	self->rename_guid = g_strdup (lrg_node_get_guid (n));
	g_string_assign (self->rename_buf, lrg_node_get_name (n) != NULL ? lrg_node_get_name (n) : "");
	self->rename_focus = TRUE;
	self->reveal_pending = TRUE;
}

static void
a_toggle_visible (gpointer data)
{
	LrgEditorApp        *self = APP (data);
	g_autoptr(GPtrArray) roots = lrg_editor_get_selected_roots (self->editor);
	gboolean             target;
	guint                i;

	if (roots->len == 0)
		return;
	target = !lrg_node_get_visible (g_ptr_array_index (roots, 0));
	lrg_editor_begin_group (self->editor, target ? "Show" : "Hide");
	for (i = 0; i < roots->len; i++)
	{
		GValue v = G_VALUE_INIT;

		g_value_init (&v, G_TYPE_BOOLEAN);
		g_value_set_boolean (&v, target);
		lrg_editor_set_object_property (self->editor, g_ptr_array_index (roots, i), "visible", &v, FALSE);
		g_value_unset (&v);
	}
	lrg_editor_end_group (self->editor);
}

static void
a_toggle_lock (gpointer data)
{
	LrgEditorApp *self = APP (data);
	GPtrArray    *nodes = lrg_editor_selection_get_nodes (lrg_editor_get_selection (self->editor));
	gboolean      target;
	guint         i;

	if (nodes->len == 0)
		return;
	target = !lrg_node_get_locked (g_ptr_array_index (nodes, 0));
	lrg_editor_begin_group (self->editor, target ? "Lock" : "Unlock");
	for (i = 0; i < nodes->len; i++)
	{
		GValue v = G_VALUE_INIT;

		g_value_init (&v, G_TYPE_BOOLEAN);
		g_value_set_boolean (&v, target);
		lrg_editor_set_object_property (self->editor, g_ptr_array_index (nodes, i), "locked", &v, FALSE);
		g_value_unset (&v);
	}
	lrg_editor_end_group (self->editor);
}

/* Group: a new empty parent at the selection's centre, keeping world
 * transforms (Ctrl+G). */
static void
a_group (gpointer data)
{
	LrgEditorApp        *self = APP (data);
	g_autoptr(GPtrArray) roots = lrg_editor_get_selected_roots (self->editor);
	g_autoptr(LrgNode)   group = NULL;
	LrgNode             *parent;
	gfloat               center[3] = { 0, 0, 0 };
	guint                i;
	gint                 index;

	if (roots->len == 0)
		return;

	for (i = 0; i < roots->len; i++)
	{
		gfloat p[3];

		lrg_editor_node_get_world_position (g_ptr_array_index (roots, i), p);
		center[0] += p[0] / roots->len;
		center[1] += p[1] / roots->len;
		center[2] += p[2] / roots->len;
	}

	parent = lrg_node_get_parent (g_ptr_array_index (roots, 0));
	index = lrg_node_get_child_index (parent, g_ptr_array_index (roots, 0));
	group = lrg_node_new ("Group");

	lrg_editor_begin_group (self->editor, "Group");
	{
		gfloat world[16], loc[3], rot[3], scl[3];
		const gfloat zero[3] = { 0, 0, 0 }, one[3] = { 1, 1, 1 };

		/* Place the group at the centre in world space. */
		lrg_editor_math_compose (center, zero, one, world);
		lrg_editor_insert_node (self->editor, group,
		                        lrg_node_get_parent (parent) != NULL ? parent : NULL, index);
		if (lrg_editor_world_to_local (lrg_node_get_parent (group), world, loc, rot, scl))
			lrg_editor_transform_node (self->editor, group, loc, rot, scl, FALSE);
	}
	for (i = 0; i < roots->len; i++)
		lrg_editor_move_node (self->editor, g_ptr_array_index (roots, i), group, -1, TRUE);
	lrg_editor_end_group (self->editor);

	lrg_editor_select (self->editor, group, FALSE);
	self->reveal_pending = TRUE;
}

static gpointer
node_ref_copy (gconstpointer src,
               gpointer      user_data)
{
	return g_object_ref ((gpointer) src);
}

static void
a_ungroup (gpointer data)
{
	LrgEditorApp *self = APP (data);
	LrgNode      *group = primary (self);
	LrgNode      *parent;
	gint          index;
	g_autoptr(GPtrArray) kids = NULL;
	guint         i;

	if (group == NULL || lrg_node_get_n_children (group) == 0)
		return;

	parent = lrg_node_get_parent (group);
	index = lrg_node_get_child_index (parent, group);
	/* A snapshot (the moves change the child list); it must hold its own
	 * references because copies inherit the source's free function. */
	kids = g_ptr_array_copy (lrg_node_get_children (group), node_ref_copy, NULL);

	lrg_editor_begin_group (self->editor, "Ungroup");
	for (i = 0; i < kids->len; i++)
		lrg_editor_move_node (self->editor, g_ptr_array_index (kids, i), parent, index + 1 + (gint) i, TRUE);
	if (lrg_node_get_visual (group) == NULL && lrg_node_get_components (group)->len == 0 &&
	    lrg_node_get_scripts (group)->len == 0)
		lrg_editor_delete_node (self->editor, group);
	lrg_editor_end_group (self->editor);

	lrg_editor_select_nodes (self->editor, kids, FALSE);
}

static void
a_move_up (gpointer data)
{
	LrgEditorApp *self = APP (data);
	LrgNode      *n = primary (self);
	gint          index;

	if (n == NULL || lrg_node_get_parent (n) == NULL)
		return;
	index = lrg_node_get_child_index (lrg_node_get_parent (n), n);
	if (index > 0)
		lrg_editor_move_node (self->editor, n, lrg_node_get_parent (n), index - 1, FALSE);
}

static void
a_move_down (gpointer data)
{
	LrgEditorApp *self = APP (data);
	LrgNode      *n = primary (self);
	gint          index;

	if (n == NULL || lrg_node_get_parent (n) == NULL)
		return;
	index = lrg_node_get_child_index (lrg_node_get_parent (n), n);
	if (index + 1 < (gint) lrg_node_get_n_children (lrg_node_get_parent (n)))
		lrg_editor_move_node (self->editor, n, lrg_node_get_parent (n), index + 1, FALSE);
}

static void
a_save_prefab (gpointer data)
{
	LrgEditorApp     *self = APP (data);
	g_autofree gchar *dir = (self->project_dir != NULL)
	                        ? g_build_filename (self->project_dir, "prefabs", NULL)
	                        : g_get_current_dir ();

	if (primary (self) == NULL)
		return;
	editor_file_dialog_open (self, EDITOR_FILE_SAVE_PREFAB, dir, ".rprefab");
	g_string_assign (self->file_name, lrg_node_get_name (primary (self)) != NULL
	                                  ? lrg_node_get_name (primary (self)) : "prefab");
	g_string_append (self->file_name, ".rprefab");
}

static void
a_instantiate_prefab (gpointer data)
{
	LrgEditorApp     *self = APP (data);
	g_autofree gchar *dir = (self->project_dir != NULL)
	                        ? g_build_filename (self->project_dir, "prefabs", NULL)
	                        : g_get_current_dir ();

	editor_file_dialog_open (self, EDITOR_FILE_PICK_ASSET, dir, ".rprefab");
	g_clear_pointer (&self->file_target_guid, g_free);
	self->file_target_slot = -2;   /* instantiate */
}

static void
a_attach_script (gpointer data)
{
	LrgEditorApp *self = APP (data);

	if (primary (self) == NULL)
		return;
	g_free (self->file_target_guid);
	self->file_target_guid = g_strdup (lrg_node_get_guid (primary (self)));
	editor_dialog_open (self, EDITOR_DIALOG_NEW_SCRIPT);
}

static void
a_open_scripts (gpointer data)
{
	LrgEditorApp *self = APP (data);
	LrgNode      *n = primary (self);
	GPtrArray    *scripts;
	guint         i;

	if (n == NULL)
		return;
	scripts = lrg_node_get_scripts (n);
	for (i = 0; i < scripts->len; i++)
	{
		const gchar *path = lrg_script_binding_get_script (g_ptr_array_index (scripts, i));

		if (path != NULL)
			editor_app_open_file (self, path, 1, 1);
	}
}

static void
a_focus (gpointer data)
{
	editor_app_frame_selection (APP (data));
}

static void
a_frame_all (gpointer data)
{
	editor_app_frame_all (APP (data));
}

static void
align_view (LrgEditorApp *self,
            gfloat        x,
            gfloat        y,
            gfloat        z,
            gboolean      ortho)
{
	const gfloat dir[3] = { x, y, z };

	lrg_editor_view_align (&self->view, dir);
	if (ortho)
		self->view.orthographic = TRUE;
}

static void a_view_top (gpointer d)    { align_view (APP (d), 0, 1, 0, TRUE); }
static void a_view_bottom (gpointer d) { align_view (APP (d), 0, -1, 0, TRUE); }
static void a_view_front (gpointer d)  { align_view (APP (d), 0, 0, 1, TRUE); }
static void a_view_back (gpointer d)   { align_view (APP (d), 0, 0, -1, TRUE); }
static void a_view_right (gpointer d)  { align_view (APP (d), 1, 0, 0, TRUE); }
static void a_view_left (gpointer d)   { align_view (APP (d), -1, 0, 0, TRUE); }

static void
a_view_persp (gpointer data)
{
	LrgEditorApp *self = APP (data);

	if (self->view_2d)
		return;
	self->view.orthographic = !self->view.orthographic;
}

static void
a_toggle_2d (gpointer data)
{
	LrgEditorApp *self = APP (data);

	self->view_2d = !self->view_2d;
	editor_view_reset (self);
}

static void a_grid (gpointer d)      { APP (d)->show_grid = !APP (d)->show_grid; }
static void a_icons (gpointer d)     { APP (d)->show_icons = !APP (d)->show_icons; }
static void a_wireframe (gpointer d) { APP (d)->wireframe = !APP (d)->wireframe; }
static void a_stats (gpointer d)     { APP (d)->show_stats = !APP (d)->show_stats; }

static void
a_reset_layout (gpointer data)
{
	editor_app_reset_layout (APP (data));
}

static void
set_tool (LrgEditorApp  *self,
          LrgEditorTool  tool)
{
	lrg_editor_set_tool (self->editor, tool);
}

static void a_tool_select (gpointer d) { set_tool (APP (d), LRG_EDITOR_TOOL_SELECT); }
static void a_tool_move (gpointer d)   { set_tool (APP (d), LRG_EDITOR_TOOL_TRANSLATE); }
static void a_tool_rotate (gpointer d) { set_tool (APP (d), LRG_EDITOR_TOOL_ROTATE); }
static void a_tool_scale (gpointer d)  { set_tool (APP (d), LRG_EDITOR_TOOL_SCALE); }

static void
a_toggle_space (gpointer data)
{
	LrgEditor *ed = APP (data)->editor;

	lrg_editor_set_space (ed, lrg_editor_get_space (ed) == LRG_EDITOR_SPACE_LOCAL
	                          ? LRG_EDITOR_SPACE_WORLD : LRG_EDITOR_SPACE_LOCAL);
}

static void
a_toggle_snap (gpointer data)
{
	LrgEditor *ed = APP (data)->editor;

	lrg_editor_set_snap_enabled (ed, !lrg_editor_get_snap_enabled (ed));
}

static void
a_play (gpointer data)
{
	if (!APP (data)->playing)
		editor_play_start (APP (data));
}

static void a_pause (gpointer d) { editor_play_toggle_pause (APP (d)); }
static void a_step (gpointer d)  { editor_play_step (APP (d)); }
static void a_stop (gpointer d)  { editor_play_stop (APP (d)); }

static void
a_run (gpointer data)
{
	g_autoptr(GError) error = NULL;

	if (!editor_run_start (APP (data), &error))
		editor_app_notify (APP (data), 3, "Run failed: %s", error->message);
}

static void a_run_stop (gpointer d) { editor_run_stop (APP (d)); }

static void
a_validate (gpointer data)
{
	LrgEditorApp *self = APP (data);

	self->problems_dirty = TRUE;
	self->problems_time = -1000.0;
	lrg_gui_dock_open_panel (self->dock, "problems");
}

static void a_palette (gpointer d)    { editor_palette_open (APP (d), FALSE); }
static void a_quick_open (gpointer d) { editor_palette_open (APP (d), TRUE); }

/* The level is plain YAML: open it in the external editor (Emacs edits
 * reload live when there are no unsaved changes here). */
static void
a_open_level_yaml (gpointer data)
{
	LrgEditorApp *self = APP (data);
	const gchar  *path = lrg_editor_get_path (self->editor);

	if (path == NULL)
	{
		editor_app_notify (self, 2, "Save the level first");
		return;
	}
	if (lrg_editor_is_dirty (self->editor))
		editor_app_save (self);

	/* Jump to the selected node's entry when there is one. */
	{
		LrgNode          *n = primary (self);
		g_autofree gchar *text = NULL;
		gint              line = 1;

		if (n != NULL && g_file_get_contents (path, &text, NULL, NULL))
		{
			const gchar *hit = strstr (text, lrg_node_get_guid (n));
			const gchar *p;

			for (p = text; hit != NULL && p < hit; p++)
				if (*p == '\n')
					line++;
		}
		editor_app_open_file (self, path, line, 1);
	}
}

static void
a_reveal_socket (gpointer data)
{
	LrgEditorApp *self = APP (data);
	const gchar  *sock = lrg_editor_server_get_socket_path (self->server);

	if (sock == NULL)
	{
		g_autoptr(GError) error = NULL;

		if (!lrg_editor_app_start_server (self, NULL, &error))
		{
			editor_app_notify (self, 3, "IPC server: %s", error->message);
			return;
		}
		sock = lrg_editor_server_get_socket_path (self->server);
	}
	lrg_gui_context_set_clipboard (self->gui, sock);
	editor_app_notify (self, 0, "IPC socket (copied): %s", sock);
}

static void
a_refresh_assets (gpointer data)
{
	APP (data)->assets_dirty = TRUE;
	g_ptr_array_set_size (APP (data)->quick_files, 0);
}

static void a_shortcuts (gpointer d) { editor_dialog_open (APP (d), EDITOR_DIALOG_SHORTCUTS); }
static void a_about (gpointer d)     { editor_dialog_open (APP (d), EDITOR_DIALOG_ABOUT); }

/* The editor docs: next to the binary in a source tree
 * (build/<type>/lrg-editor -> docs/editor), or installed. */
static void
a_docs (gpointer data)
{
	LrgEditorApp     *self = APP (data);
	g_autofree gchar *exe = g_file_read_link ("/proc/self/exe", NULL);
	g_autofree gchar *exe_dir = (exe != NULL) ? g_path_get_dirname (exe) : NULL;
	g_autofree gchar *tree = (exe_dir != NULL)
	                         ? g_build_filename (exe_dir, "..", "..", "docs", "editor", "overview.org", NULL)
	                         : NULL;
	const gchar      *installed = "/usr/share/doc/libregnum/editor/overview.org";

	if (tree != NULL && g_file_test (tree, G_FILE_TEST_EXISTS))
		editor_app_open_file (self, tree, 1, 1);
	else if (g_file_test (installed, G_FILE_TEST_EXISTS))
		editor_app_open_file (self, installed, 1, 1);
	else
		editor_app_notify (self, 2, "Editor documentation not found (docs/editor/ in the source tree)");
}

/* ==========================================================================
 * Registration
 * ========================================================================== */

void
editor_app_register_actions (LrgEditorApp *self)
{
	static const struct
	{
		const gchar              *id;
		const gchar              *label;
		const gchar              *category;
		const gchar              *shortcut;
		LrgEditorActionFunc       activate;
		LrgEditorActionStateFunc  enabled;
		LrgEditorActionStateFunc  checked;
	} table[] = {
		{ "file.new-level", "New Level", "File", "Ctrl+N", a_new_level, NULL, NULL },
		{ "file.open-level", "Open Level...", "File", "Ctrl+O", a_open_level, NULL, NULL },
		{ "file.save", "Save", "File", "Ctrl+S", a_save, NULL, NULL },
		{ "file.save-as", "Save As...", "File", "Ctrl+Shift+S", a_save_as, NULL, NULL },
		{ "file.reload", "Reload from Disk", "File", NULL, a_reload, has_path, NULL },
		{ "file.new-project", "New Project...", "File", NULL, a_new_project, NULL, NULL },
		{ "file.open-project", "Open Project...", "File", "Ctrl+Shift+O", a_open_project, NULL, NULL },
		{ "file.project-settings", "Project Settings...", "File", NULL, a_project_settings, has_project, NULL },
		{ "file.import-scene", "Import Scene (Blender YAML)...", "File", NULL, a_import_scene, NULL, NULL },
		{ "file.quit", "Quit", "File", "Ctrl+Q", a_quit, NULL, NULL },

		{ "edit.undo", "Undo", "Edit", "Ctrl+Z", a_undo, can_undo, NULL },
		{ "edit.redo", "Redo", "Edit", "Ctrl+Shift+Z|Ctrl+Y", a_redo, can_redo, NULL },
		{ "edit.cut", "Cut", "Edit", "Ctrl+X", a_cut, has_selection, NULL },
		{ "edit.copy", "Copy", "Edit", "Ctrl+C", a_copy, has_selection, NULL },
		{ "edit.paste", "Paste", "Edit", "Ctrl+V", a_paste, has_clipboard, NULL },
		{ "edit.duplicate", "Duplicate", "Edit", "Ctrl+D", a_duplicate, has_selection, NULL },
		{ "edit.delete", "Delete", "Edit", "Delete", a_delete, has_selection, NULL },
		{ "edit.select-all", "Select All", "Edit", "Ctrl+A", a_select_all, NULL, NULL },
		{ "edit.deselect", "Deselect", "Edit", "Ctrl+Shift+A", a_deselect, has_selection, NULL },
		{ "edit.find", "Find in Hierarchy", "Edit", "Ctrl+F", a_find, NULL, NULL },
		{ "edit.preferences", "Preferences...", "Edit", "Ctrl+,", a_preferences, NULL, NULL },

		{ "node.rename", "Rename", "Node", "F2", a_rename, has_selection, NULL },
		{ "node.toggle-visible", "Toggle Visibility", "Node", "H", a_toggle_visible, has_selection, NULL },
		{ "node.toggle-lock", "Toggle Lock", "Node", "Ctrl+L", a_toggle_lock, has_selection, NULL },
		{ "node.group", "Group Selection", "Node", "Ctrl+G", a_group, has_selection, NULL },
		{ "node.ungroup", "Ungroup", "Node", "Ctrl+Shift+G", a_ungroup, has_selection, NULL },
		{ "node.move-up", "Move Up", "Node", "Alt+Up", a_move_up, has_selection, NULL },
		{ "node.move-down", "Move Down", "Node", "Alt+Down", a_move_down, has_selection, NULL },
		{ "node.save-prefab", "Save as Prefab...", "Node", NULL, a_save_prefab, has_selection, NULL },
		{ "node.instantiate-prefab", "Instantiate Prefab...", "Node", "Ctrl+Shift+I", a_instantiate_prefab, NULL, NULL },
		{ "node.attach-script", "Attach Script...", "Node", NULL, a_attach_script, has_selection, NULL },
		{ "node.open-scripts", "Open Scripts in Editor", "Node", "Ctrl+E", a_open_scripts, primary_has_scripts, NULL },

		{ "view.focus", "Focus Selection", "View", "F", a_focus, NULL, NULL },
		{ "view.frame-all", "Frame All", "View", "Home", a_frame_all, NULL, NULL },
		{ "view.top", "Top View", "View", "Num7", a_view_top, NULL, NULL },
		{ "view.bottom", "Bottom View", "View", "Ctrl+Num7", a_view_bottom, NULL, NULL },
		{ "view.front", "Front View", "View", "Num1", a_view_front, NULL, NULL },
		{ "view.back", "Back View", "View", "Ctrl+Num1", a_view_back, NULL, NULL },
		{ "view.right", "Right View", "View", "Num3", a_view_right, NULL, NULL },
		{ "view.left", "Left View", "View", "Ctrl+Num3", a_view_left, NULL, NULL },
		{ "view.perspective", "Orthographic", "View", "Num5", a_view_persp, NULL, is_ortho },
		{ "view.toggle-2d", "2D Mode", "View", "Ctrl+Shift+2", a_toggle_2d, NULL, is_2d },
		{ "view.grid", "Show Grid", "View", "Shift+G", a_grid, NULL, grid_on },
		{ "view.icons", "Show Icons", "View", NULL, a_icons, NULL, icons_on },
		{ "view.wireframe", "Wireframe", "View", "Shift+Z", a_wireframe, NULL, wire_on },
		{ "view.stats", "Show Stats", "View", NULL, a_stats, NULL, stats_on },
		{ "view.reset-layout", "Reset Layout", "View", NULL, a_reset_layout, NULL, NULL },

		{ "tool.select", "Select Tool", "Tool", "Q", a_tool_select, NULL, tool_is_select },
		{ "tool.move", "Move Tool", "Tool", "W", a_tool_move, NULL, tool_is_move },
		{ "tool.rotate", "Rotate Tool", "Tool", "E", a_tool_rotate, NULL, tool_is_rotate },
		{ "tool.scale", "Scale Tool", "Tool", "R", a_tool_scale, NULL, tool_is_scale },
		{ "tool.toggle-space", "Local Space", "Tool", "T", a_toggle_space, NULL, space_is_local },
		{ "tool.toggle-snap", "Snap", "Tool", "Y", a_toggle_snap, NULL, snap_on },

		{ "play.start", "Play in Editor", "Play", "F5", a_play, NULL, is_playing },
		{ "play.pause", "Pause", "Play", "F7", a_pause, is_playing, is_paused },
		{ "play.step", "Step Frame", "Play", "F10", a_step, is_playing, NULL },
		{ "play.stop", "Stop", "Play", "Shift+F5|F8", a_stop, is_playing, NULL },
		{ "run.start", "Run Game", "Play", "F6", a_run, not_running, is_running },
		{ "run.stop", "Stop Game", "Play", "Shift+F6", a_run_stop, is_running, NULL },

		{ "tools.validate", "Validate Level", "Tools", NULL, a_validate, NULL, NULL },
		{ "tools.palette", "Command Palette", "Tools", "Ctrl+Shift+P|F1", a_palette, NULL, NULL },
		{ "tools.quick-open", "Quick Open File", "Tools", "Ctrl+P", a_quick_open, NULL, NULL },
		{ "tools.open-level-yaml", "Edit Level YAML in External Editor", "Tools", "Ctrl+Alt+E",
		  a_open_level_yaml, has_path, NULL },
		{ "tools.reveal-socket", "Copy IPC Socket Path", "Tools", NULL, a_reveal_socket, NULL, NULL },
		{ "tools.refresh-assets", "Refresh Assets", "Tools", NULL, a_refresh_assets, NULL, NULL },

		{ "help.shortcuts", "Keyboard Shortcuts", "Help", "Ctrl+/", a_shortcuts, NULL, NULL },
		{ "help.docs", "Editor Documentation", "Help", NULL, a_docs, NULL, NULL },
		{ "help.about", "About", "Help", NULL, a_about, NULL, NULL },
	};
	gsize i;

	for (i = 0; i < G_N_ELEMENTS (table); i++)
		lrg_editor_actions_add (self->actions, table[i].id, table[i].label, table[i].category,
		                        table[i].shortcut, table[i].activate, table[i].enabled,
		                        table[i].checked, self);
}

/* ==========================================================================
 * Shortcut dispatch
 * ========================================================================== */

void
editor_app_process_shortcuts (LrgEditorApp *self)
{
	gboolean typing = lrg_gui_context_wants_keyboard (self->gui);
	guint    i;

	if (self->cur_input == NULL || self->dialog != EDITOR_DIALOG_NONE || self->palette_open)
		return;

	for (i = 0; i < self->cur_input->n_keys; i++)
	{
		const LrgGuiKeyEvent *ev = &self->cur_input->keys[i];
		LrgGuiModifiers       mods = ev->mods & (LRG_GUI_MOD_CTRL | LRG_GUI_MOD_SHIFT |
		                                         LRG_GUI_MOD_ALT | LRG_GUI_MOD_SUPER);
		const gchar          *id;

		if (ev->repeat && ev->key != LRG_GUI_KEY_Z && ev->key != LRG_GUI_KEY_Y)
			continue;

		/* While typing, only Ctrl/Alt chords that are not text editing
		 * fire (Ctrl+S saves from a text field; Ctrl+A selects text). */
		if (typing)
		{
			if (!(mods & (LRG_GUI_MOD_CTRL | LRG_GUI_MOD_ALT)))
				continue;
			if ((mods & LRG_GUI_MOD_CTRL) &&
			    (ev->key == LRG_GUI_KEY_A || ev->key == LRG_GUI_KEY_C || ev->key == LRG_GUI_KEY_V ||
			     ev->key == LRG_GUI_KEY_X || ev->key == LRG_GUI_KEY_Z || ev->key == LRG_GUI_KEY_Y ||
			     ev->key == LRG_GUI_KEY_LEFT || ev->key == LRG_GUI_KEY_RIGHT ||
			     ev->key == LRG_GUI_KEY_BACKSPACE || ev->key == LRG_GUI_KEY_DELETE))
				continue;
		}

		/* A gizmo drag owns Escape. */
		if (ev->key == LRG_GUI_KEY_ESCAPE && lrg_editor_gizmo_is_dragging (self->gizmo))
			continue;

		id = lrg_editor_actions_find_by_shortcut (self->actions, mods, ev->key);
		if (id != NULL)
			lrg_editor_actions_activate (self->actions, id);
	}
}

/* ==========================================================================
 * IPC methods
 * ========================================================================== */

static JsonNode *
str_node (const gchar *s)
{
	JsonNode *n = json_node_new (JSON_NODE_VALUE);

	if (s == NULL)
		json_node_init_null (n);
	else
		json_node_set_string (n, s);
	return n;
}

static JsonNode *
bool_node (gboolean b)
{
	JsonNode *n = json_node_new (JSON_NODE_VALUE);

	json_node_set_boolean (n, b);
	return n;
}

static JsonNode *
obj_node (JsonObject *o)
{
	JsonNode *n = json_node_new (JSON_NODE_OBJECT);

	json_node_take_object (n, o);
	return n;
}

static const gchar *
p_string (JsonObject  *params,
          const gchar *key)
{
	JsonNode *n;

	if (params == NULL || !json_object_has_member (params, key))
		return NULL;
	n = json_object_get_member (params, key);
	if (!JSON_NODE_HOLDS_VALUE (n) || json_node_get_value_type (n) != G_TYPE_STRING)
		return NULL;
	return json_node_get_string (n);
}

static gboolean
p_bool (JsonObject  *params,
        const gchar *key,
        gboolean     def)
{
	JsonNode *n;

	if (params == NULL || !json_object_has_member (params, key))
		return def;
	n = json_object_get_member (params, key);
	if (!JSON_NODE_HOLDS_VALUE (n) || json_node_get_value_type (n) != G_TYPE_BOOLEAN)
		return def;
	return json_node_get_boolean (n);
}

static JsonNode *
m_action_run (LrgEditorServer  *server,
              JsonObject       *params,
              gpointer          user_data,
              GError          **error)
{
	LrgEditorApp *self = APP (user_data);
	const gchar  *id = p_string (params, "id");

	if (id == NULL)
	{
		g_set_error_literal (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS,
		                     "Missing string parameter 'id'");
		return NULL;
	}
	if (!lrg_editor_actions_has (self->actions, id))
	{
		g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_NOT_FOUND,
		             "No action '%s'", id);
		return NULL;
	}
	return bool_node (lrg_editor_actions_activate (self->actions, id));
}

static JsonNode *
m_action_list (LrgEditorServer  *server,
               JsonObject       *params,
               gpointer          user_data,
               GError          **error)
{
	LrgEditorApp *self = APP (user_data);
	JsonArray    *out = json_array_new ();
	GList        *ids = lrg_editor_actions_list (self->actions);
	GList        *l;
	JsonNode     *n;

	for (l = ids; l != NULL; l = l->next)
	{
		JsonObject *o = json_object_new ();

		json_object_set_string_member (o, "id", l->data);
		json_object_set_string_member (o, "label", lrg_editor_actions_get_label (self->actions, l->data));
		json_object_set_string_member (o, "category", lrg_editor_actions_get_category (self->actions, l->data));
		json_object_set_member (o, "shortcut", str_node (lrg_editor_actions_get_shortcut (self->actions, l->data)));
		json_object_set_boolean_member (o, "enabled", lrg_editor_actions_is_enabled (self->actions, l->data));
		json_object_set_boolean_member (o, "checked", lrg_editor_actions_is_checked (self->actions, l->data));
		json_array_add_object_element (out, o);
	}
	g_list_free (ids);

	n = json_node_new (JSON_NODE_ARRAY);
	json_node_take_array (n, out);
	return n;
}

static void
vec3_member (JsonObject   *o,
             const gchar  *key,
             const gfloat *v)
{
	JsonArray *a = json_array_new ();

	json_array_add_double_element (a, v[0]);
	json_array_add_double_element (a, v[1]);
	json_array_add_double_element (a, v[2]);
	json_object_set_array_member (o, key, a);
}

static gboolean
read_vec3 (JsonObject  *params,
           const gchar *key,
           gfloat      *out)
{
	JsonNode  *n;
	JsonArray *a;
	guint      i;

	if (params == NULL || !json_object_has_member (params, key))
		return FALSE;
	n = json_object_get_member (params, key);
	if (!JSON_NODE_HOLDS_ARRAY (n) || json_array_get_length (a = json_node_get_array (n)) != 3)
		return FALSE;
	for (i = 0; i < 3; i++)
		out[i] = (gfloat) json_array_get_double_element (a, i);
	return TRUE;
}

static JsonNode *
view_json (LrgEditorApp *self)
{
	JsonObject *o = json_object_new ();

	vec3_member (o, "position", self->view.position);
	vec3_member (o, "target", self->view.target);
	json_object_set_double_member (o, "fovy", self->view.fovy);
	json_object_set_boolean_member (o, "orthographic", self->view.orthographic);
	json_object_set_double_member (o, "ortho_height", self->view.ortho_height);
	json_object_set_boolean_member (o, "mode_2d", self->view_2d);
	return obj_node (o);
}

static JsonNode *
m_view_get (LrgEditorServer  *server,
            JsonObject       *params,
            gpointer          user_data,
            GError          **error)
{
	return view_json (APP (user_data));
}

static JsonNode *
m_view_set (LrgEditorServer  *server,
            JsonObject       *params,
            gpointer          user_data,
            GError          **error)
{
	LrgEditorApp *self = APP (user_data);
	gfloat        v[3];

	if (read_vec3 (params, "position", v))
		memcpy (self->view.position, v, sizeof v);
	if (read_vec3 (params, "target", v))
		memcpy (self->view.target, v, sizeof v);
	if (params != NULL && json_object_has_member (params, "orthographic"))
		self->view.orthographic = p_bool (params, "orthographic", FALSE);
	if (params != NULL && json_object_has_member (params, "ortho_height"))
		self->view.ortho_height = MAX (0.01f, (gfloat) json_object_get_double_member (params, "ortho_height"));
	if (params != NULL && json_object_has_member (params, "mode_2d"))
	{
		self->view_2d = p_bool (params, "mode_2d", FALSE);
		editor_view_reset (self);
	}
	return view_json (self);
}

static JsonNode *
m_view_focus (LrgEditorServer  *server,
              JsonObject       *params,
              gpointer          user_data,
              GError          **error)
{
	LrgEditorApp *self = APP (user_data);
	const gchar  *ref = p_string (params, "node");

	if (ref != NULL)
	{
		LrgNode *n = lrg_editor_find_node (self->editor, ref);

		if (n == NULL)
		{
			g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_NOT_FOUND,
			             "No node '%s'", ref);
			return NULL;
		}
		lrg_editor_select (self->editor, n, FALSE);
	}
	editor_app_frame_selection (self);
	return view_json (self);
}

static JsonNode *
m_view_screenshot (LrgEditorServer  *server,
                   JsonObject       *params,
                   gpointer          user_data,
                   GError          **error)
{
	LrgEditorApp     *self = APP (user_data);
	const gchar      *path = p_string (params, "path");
	g_autofree gchar *abs = NULL;

	if (path == NULL)
	{
		g_set_error_literal (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS,
		                     "Missing string parameter 'path' (a .png file)");
		return NULL;
	}
	abs = editor_app_project_path (self, path);

	/* The whole window is captured after the next frame is drawn. */
	if (p_bool (params, "window", FALSE))
	{
		if (!self->window_open)
		{
			g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED, "The editor has no window");
			return NULL;
		}
		g_free (self->window_shot);
		self->window_shot = g_strdup (abs);
		return str_node (abs);
	}
	if (!editor_render_screenshot (self, abs, error))
		return NULL;
	return str_node (abs);
}

static JsonNode *
play_state (LrgEditorApp *self)
{
	JsonObject *o = json_object_new ();

	json_object_set_boolean_member (o, "playing", self->playing);
	json_object_set_boolean_member (o, "paused", self->paused);
	json_object_set_double_member (o, "time", self->play_time);
	json_object_set_boolean_member (o, "running", self->run_proc != NULL);
	return obj_node (o);
}

static JsonNode *
m_play_start (LrgEditorServer *s, JsonObject *p, gpointer d, GError **e)
{
	if (!APP (d)->playing)
		editor_play_start (APP (d));
	return play_state (APP (d));
}

static JsonNode *
m_play_stop (LrgEditorServer *s, JsonObject *p, gpointer d, GError **e)
{
	editor_play_stop (APP (d));
	return play_state (APP (d));
}

static JsonNode *
m_play_pause (LrgEditorServer *s, JsonObject *p, gpointer d, GError **e)
{
	editor_play_toggle_pause (APP (d));
	return play_state (APP (d));
}

static JsonNode *
m_play_step (LrgEditorServer *s, JsonObject *p, gpointer d, GError **e)
{
	editor_play_step (APP (d));
	return play_state (APP (d));
}

static JsonNode *
m_play_state (LrgEditorServer *s, JsonObject *p, gpointer d, GError **e)
{
	return play_state (APP (d));
}

static JsonNode *
m_run_start (LrgEditorServer *s, JsonObject *p, gpointer d, GError **error)
{
	if (!editor_run_start (APP (d), error))
		return NULL;
	return play_state (APP (d));
}

static JsonNode *
m_run_stop (LrgEditorServer *s, JsonObject *p, gpointer d, GError **e)
{
	editor_run_stop (APP (d));
	return play_state (APP (d));
}

static JsonNode *
m_project_info (LrgEditorServer  *server,
                JsonObject       *params,
                gpointer          user_data,
                GError          **error)
{
	LrgEditorApp *self = APP (user_data);
	JsonObject   *o = json_object_new ();

	json_object_set_member (o, "root", str_node (self->project_dir));
	if (self->project != NULL)
	{
		JsonArray *dirs = json_array_new ();
		GPtrArray *arr = lrg_project_get_asset_dirs (self->project);
		guint      i;

		json_object_set_member (o, "name", str_node (lrg_project_get_name (self->project)));
		json_object_set_member (o, "default_level", str_node (lrg_project_get_default_level (self->project)));
		json_object_set_member (o, "game_output", str_node (lrg_project_get_game_output (self->project)));
		json_object_set_member (o, "run_command", str_node (lrg_project_get_run_command (self->project)));
		json_object_set_string_member (o, "scripts_dir", lrg_project_get_scripts_dir (self->project));
		for (i = 0; i < arr->len; i++)
			json_array_add_string_element (dirs, g_ptr_array_index (arr, i));
		json_object_set_array_member (o, "asset_dirs", dirs);
	}
	return obj_node (o);
}

static JsonNode *
m_project_open (LrgEditorServer  *server,
                JsonObject       *params,
                gpointer          user_data,
                GError          **error)
{
	LrgEditorApp *self = APP (user_data);
	const gchar  *path = p_string (params, "path");

	if (path == NULL)
	{
		g_set_error_literal (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS,
		                     "Missing string parameter 'path'");
		return NULL;
	}
	if (lrg_editor_is_dirty (self->editor) && !p_bool (params, "force", FALSE))
	{
		g_set_error_literal (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_FAILED,
		                     "The current level has unsaved changes; pass \"force\": true to discard them");
		return NULL;
	}
	lrg_editor_new_level (self->editor, "untitled");
	g_clear_object (&self->project);
	g_clear_pointer (&self->project_dir, g_free);
	if (!lrg_editor_app_open_project (self, path, error))
		return NULL;
	return m_project_info (server, NULL, user_data, error);
}

static JsonNode *
m_asset_list (LrgEditorServer  *server,
              JsonObject       *params,
              gpointer          user_data,
              GError          **error)
{
	LrgEditorApp                *self = APP (user_data);
	g_autoptr(LrgAssetDatabase)  db = NULL;
	JsonArray                   *out = json_array_new ();
	GPtrArray                   *entries;
	const gchar                 *type = p_string (params, "type");
	gint                         want = -1;
	guint                        i;
	JsonNode                    *n;

	if (type != NULL)
	{
		g_autoptr(GEnumClass) k = g_type_class_ref (LRG_TYPE_ASSET_TYPE);
		GEnumValue           *v = g_enum_get_value_by_nick (k, type);

		if (v == NULL)
		{
			json_array_unref (out);
			g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS,
			             "Unknown asset type '%s'", type);
			return NULL;
		}
		want = v->value;
	}

	/* The whole project (or the level's directory). */
	db = lrg_asset_database_new ();
	if (editor_app_base_dir (self) != NULL)
		lrg_asset_database_add_search_dir (db, editor_app_base_dir (self));
	if (!lrg_asset_database_scan (db, error))
	{
		json_array_unref (out);
		return NULL;
	}

	entries = lrg_asset_database_get_entries (db);
	for (i = 0; i < entries->len; i++)
	{
		LrgAssetEntry    *e = g_ptr_array_index (entries, i);
		JsonObject       *o;
		g_autofree gchar *rel = NULL;
		g_autoptr(GEnumClass) k = NULL;
		GEnumValue       *v;

		if (want >= 0 && (gint) lrg_asset_entry_get_asset_type (e) != want)
			continue;
		if (strstr (lrg_asset_entry_get_path (e), "/.lrg-editor/") != NULL)
			continue;

		k = g_type_class_ref (LRG_TYPE_ASSET_TYPE);
		v = g_enum_get_value (k, lrg_asset_entry_get_asset_type (e));
		rel = editor_app_relative_path (self, lrg_asset_entry_get_path (e));
		o = json_object_new ();
		json_object_set_string_member (o, "path", rel);
		json_object_set_string_member (o, "type", v != NULL ? v->value_nick : "unknown");
		json_array_add_object_element (out, o);
	}

	n = json_node_new (JSON_NODE_ARRAY);
	json_node_take_array (n, out);
	return n;
}

static JsonNode *
m_level_validate (LrgEditorServer  *server,
                  JsonObject       *params,
                  gpointer          user_data,
                  GError          **error)
{
	LrgEditorApp        *self = APP (user_data);
	LrgEngine           *engine = lrg_engine_get_default ();
	g_autoptr(GPtrArray) problems = NULL;
	JsonArray           *out = json_array_new ();
	JsonNode            *n;
	guint                i;

	problems = lrg_editor_validate_level (lrg_editor_get_level (self->editor), editor_app_base_dir (self),
	                                      engine != NULL ? lrg_engine_get_registry (engine) : NULL);
	for (i = 0; i < problems->len; i++)
	{
		LrgEditorProblem *p = g_ptr_array_index (problems, i);
		JsonObject       *o = json_object_new ();

		json_object_set_string_member (o, "severity", lrg_editor_problem_severity_to_string (p->severity));
		json_object_set_string_member (o, "code", p->code);
		json_object_set_member (o, "node", str_node (p->node_guid));
		json_object_set_member (o, "path", str_node (p->node_path));
		json_object_set_string_member (o, "message", p->message);
		json_object_set_member (o, "file", str_node (p->file));
		json_array_add_object_element (out, o);
	}

	n = json_node_new (JSON_NODE_ARRAY);
	json_node_take_array (n, out);
	return n;
}

static JsonNode *
m_ui_notify (LrgEditorServer  *server,
             JsonObject       *params,
             gpointer          user_data,
             GError          **error)
{
	const gchar *msg = p_string (params, "message");
	const gchar *lvl = p_string (params, "level");
	gint         level = 0;

	if (msg == NULL)
	{
		g_set_error_literal (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS,
		                     "Missing string parameter 'message'");
		return NULL;
	}
	if (g_strcmp0 (lvl, "success") == 0)
		level = 1;
	else if (g_strcmp0 (lvl, "warning") == 0)
		level = 2;
	else if (g_strcmp0 (lvl, "error") == 0)
		level = 3;
	editor_app_notify (APP (user_data), level, "%s", msg);
	return bool_node (TRUE);
}

static JsonNode *
m_editor_quit (LrgEditorServer  *server,
               JsonObject       *params,
               gpointer          user_data,
               GError          **error)
{
	LrgEditorApp *self = APP (user_data);

	if (lrg_editor_is_dirty (self->editor) && !p_bool (params, "force", FALSE))
	{
		g_set_error_literal (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_FAILED,
		                     "Unsaved changes; pass \"force\": true to quit anyway");
		return NULL;
	}
	self->quit_now = TRUE;
	return bool_node (TRUE);
}

static JsonNode *
m_level_import (LrgEditorServer  *server,
                JsonObject       *params,
                gpointer          user_data,
                GError          **error)
{
	LrgEditorApp      *self = APP (user_data);
	const gchar       *path = p_string (params, "path");
	g_autofree gchar  *abs = NULL;
	g_autoptr(LrgLevel) level = NULL;

	if (path == NULL)
	{
		g_set_error_literal (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS,
		                     "Missing string parameter 'path'");
		return NULL;
	}
	abs = editor_app_project_path (self, path);
	level = lrg_level_import_scene_file (abs, error);
	if (level == NULL)
		return NULL;
	lrg_editor_set_level (self->editor, level);
	lrg_editor_set_path (self->editor, NULL);
	return bool_node (TRUE);
}

void
editor_app_register_methods (LrgEditorApp *self)
{
	static const struct
	{
		const gchar           *name;
		LrgEditorServerMethod  func;
	} table[] = {
		{ "action.run", m_action_run },
		{ "action.list", m_action_list },
		{ "view.get", m_view_get },
		{ "view.set", m_view_set },
		{ "view.focus", m_view_focus },
		{ "view.screenshot", m_view_screenshot },
		{ "play.start", m_play_start },
		{ "play.stop", m_play_stop },
		{ "play.pause", m_play_pause },
		{ "play.step", m_play_step },
		{ "play.state", m_play_state },
		{ "run.start", m_run_start },
		{ "run.stop", m_run_stop },
		{ "project.info", m_project_info },
		{ "project.open", m_project_open },
		{ "asset.list", m_asset_list },
		{ "level.validate", m_level_validate },
		{ "level.import", m_level_import },
		{ "ui.notify", m_ui_notify },
		{ "editor.quit", m_editor_quit },
	};
	gsize i;

	for (i = 0; i < G_N_ELEMENTS (table); i++)
		lrg_editor_server_register_method (self->server, table[i].name, table[i].func, self, NULL);
}
