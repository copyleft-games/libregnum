/* lrg-editor-app-actions.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Every editor command as a named action (menus, toolbar, shortcuts,
 * command palette, IPC "action.run"), the application's extra IPC methods,
 * The window turns every action into an "editor.<id>" GAction and every
 * accelerator into a canvas shortcut (lrg-editor-window.c); IPC methods
 * act on the headless view model (LrgEditorViewState / ViewportNav /
 * PlaySession), so view.*, play.*, timeline.* and tilemap.tool work with
 * --headless too.
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
#include "../../ui/lrg-menu-bar.h"
#include "../../ui/lrg-menu.h"
#include <math.h>
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
	       !editor_app_is_headless (APP (data));   /* the system clipboard may hold YAML */
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
	return lrg_editor_view_state_get_camera (APP (data)->view, -1)->view_2d;
}

static gboolean
is_ortho (gpointer data)
{
	return lrg_editor_view_state_get_camera (APP (data)->view, -1)->view.orthographic;
}

static gboolean
grid_on (gpointer data)
{
	return lrg_editor_view_state_has_overlay (APP (data)->view, -1, LRG_EDITOR_VIEW_OVERLAY_GRID);
}

static gboolean
icons_on (gpointer data)
{
	return lrg_editor_view_state_has_overlay (APP (data)->view, -1, LRG_EDITOR_VIEW_OVERLAY_ICONS);
}

static gboolean
wire_on (gpointer data)
{
	return lrg_editor_view_state_get_render_mode (APP (data)->view, -1) == LRG_EDITOR_RENDER_MODE_WIREFRAME;
}

static gboolean
stats_on (gpointer data)
{
	return lrg_editor_view_state_has_overlay (APP (data)->view, -1, LRG_EDITOR_VIEW_OVERLAY_STATS);
}

static gboolean
is_playing (gpointer data)
{
	return editor_play_is_playing (APP (data));
}

static gboolean
is_paused (gpointer data)
{
	return lrg_editor_play_session_is_paused (APP (data)->play);
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

static void
a_new_level (gpointer data)
{
	editor_app_request (APP (data), EDITOR_PENDING_NEW_LEVEL, NULL);
}

static void
a_open_level (gpointer data)
{
	editor_dialog_open_level (APP (data));
}

static void
a_save (gpointer data)
{
	editor_app_save (APP (data));
}

static void
a_save_as (gpointer data)
{
	editor_dialog_save_level_as (APP (data));
}

static void
a_reload (gpointer data)
{
	editor_app_request (APP (data), EDITOR_PENDING_RELOAD, NULL);
}

static void
a_new_project (gpointer data)
{
	editor_dialog_new_project (APP (data));
}

static void
a_open_project (gpointer data)
{
	editor_dialog_open_project (APP (data));
}

static void
a_project_settings (gpointer data)
{
	editor_dialog_project_settings (APP (data));
}

static void
a_import_scene (gpointer data)
{
	editor_dialog_import_scene (APP (data));
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
		editor_app_set_clipboard (self, yaml);
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
		editor_app_set_clipboard (self, yaml);
}

static void
a_paste (gpointer data)
{
	LrgEditorApp         *self = APP (data);
	g_autofree gchar     *text = editor_app_get_clipboard (self);
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
		editor_app_notify (self, EDITOR_NOTIFY_WARNING, "Paste: %s", error->message);
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
	/* The hierarchy's filter field arrives with the panel (Wave 4) */
	editor_app_show_panel (APP (data), "hierarchy");
}

static void
a_preferences (gpointer data)
{
	editor_app_notify (APP (data), EDITOR_NOTIFY_INFO, "Preferences: coming in Wave 4 (edit editor.yaml meanwhile)");
}

static void
a_rename (gpointer data)
{
	LrgEditorApp *self = APP (data);
	LrgNode      *n = primary (self);

	if (n == NULL)
		return;
	/* Inline rename arrives with the hierarchy panel (Wave 4) */
	editor_app_show_panel (self, "hierarchy");
	editor_app_set_status (self, "Rename %s: in the hierarchy (Wave 4) or with node.rename over IPC",
	                       lrg_node_get_name (n) != NULL ? lrg_node_get_name (n) : "node");
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
	editor_dialog_save_prefab (APP (data));
}

static void
a_instantiate_prefab (gpointer data)
{
	editor_dialog_instantiate_prefab (APP (data));
}

static void
a_attach_script (gpointer data)
{
	LrgEditorApp *self = APP (data);

	if (primary (self) == NULL)
		return;
	editor_dialog_new_script (self, lrg_node_get_guid (primary (self)));
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
	lrg_editor_view_state_focus_selection (APP (data)->view);
}

static void
a_frame_all (gpointer data)
{
	lrg_editor_view_state_frame_all (APP (data)->view);
}

/* Axis views look at the target from the given side (orthographic until
 * the next orbit); numpad orbiting in 15 degree steps; keyboard zoom.
 * All act on the active viewport's camera. */
#define VIEW(d) (APP (d)->view)
static void a_view_top (gpointer d)    { lrg_editor_view_state_set_axis_view (VIEW (d), LRG_EDITOR_AXIS_VIEW_TOP); }
static void a_view_bottom (gpointer d) { lrg_editor_view_state_set_axis_view (VIEW (d), LRG_EDITOR_AXIS_VIEW_BOTTOM); }
static void a_view_front (gpointer d)  { lrg_editor_view_state_set_axis_view (VIEW (d), LRG_EDITOR_AXIS_VIEW_FRONT); }
static void a_view_back (gpointer d)   { lrg_editor_view_state_set_axis_view (VIEW (d), LRG_EDITOR_AXIS_VIEW_BACK); }
static void a_view_right (gpointer d)  { lrg_editor_view_state_set_axis_view (VIEW (d), LRG_EDITOR_AXIS_VIEW_RIGHT); }
static void a_view_left (gpointer d)   { lrg_editor_view_state_set_axis_view (VIEW (d), LRG_EDITOR_AXIS_VIEW_LEFT); }
static void a_orbit_left (gpointer d)  { lrg_editor_view_state_orbit_step (VIEW (d), -1, 0); }
static void a_orbit_right (gpointer d) { lrg_editor_view_state_orbit_step (VIEW (d), 1, 0); }
static void a_orbit_up (gpointer d)    { lrg_editor_view_state_orbit_step (VIEW (d), 0, -1); }
static void a_orbit_down (gpointer d)  { lrg_editor_view_state_orbit_step (VIEW (d), 0, 1); }
static void a_zoom_in (gpointer d)     { lrg_editor_view_state_zoom_step (VIEW (d), TRUE); }
static void a_zoom_out (gpointer d)    { lrg_editor_view_state_zoom_step (VIEW (d), FALSE); }
static void a_view_persp (gpointer d)  { lrg_editor_view_state_toggle_ortho (VIEW (d)); }
static void a_toggle_2d (gpointer d)   { lrg_editor_view_state_toggle_2d (VIEW (d)); }
static void a_grid (gpointer d)  { lrg_editor_view_state_toggle_overlay (VIEW (d), -1, LRG_EDITOR_VIEW_OVERLAY_GRID); }
static void a_icons (gpointer d) { lrg_editor_view_state_toggle_overlay (VIEW (d), -1, LRG_EDITOR_VIEW_OVERLAY_ICONS); }
static void a_stats (gpointer d) { lrg_editor_view_state_toggle_overlay (VIEW (d), -1, LRG_EDITOR_VIEW_OVERLAY_STATS); }

static void
a_wireframe (gpointer data)
{
	LrgEditorViewState *view = APP (data)->view;

	lrg_editor_view_state_set_render_mode (view, -1,
	                                       lrg_editor_view_state_get_render_mode (view, -1)
	                                       == LRG_EDITOR_RENDER_MODE_WIREFRAME
	                                       ? LRG_EDITOR_RENDER_MODE_LIT : LRG_EDITOR_RENDER_MODE_WIREFRAME);
}

/* View > Layout and Next Viewport (split and quad viewports) */
static void a_layout_single (gpointer d) { lrg_editor_view_state_set_layout (VIEW (d), LRG_EDITOR_VIEWPORT_LAYOUT_SINGLE); }
static void a_layout_split_h (gpointer d) { lrg_editor_view_state_set_layout (VIEW (d), LRG_EDITOR_VIEWPORT_LAYOUT_SPLIT_H); }
static void a_layout_split_v (gpointer d) { lrg_editor_view_state_set_layout (VIEW (d), LRG_EDITOR_VIEWPORT_LAYOUT_SPLIT_V); }
static void a_layout_quad (gpointer d)   { lrg_editor_view_state_set_layout (VIEW (d), LRG_EDITOR_VIEWPORT_LAYOUT_QUAD); }
static void a_next_viewport (gpointer d) { lrg_editor_view_state_next_viewport (VIEW (d)); }

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
		editor_app_notify (APP (data), EDITOR_NOTIFY_ERROR, "Run failed: %s", error->message);
}

static void a_run_stop (gpointer d) { editor_run_stop (APP (d)); }

static void
a_validate (gpointer data)
{
	LrgEditorApp *self = APP (data);

	editor_app_validate_now (self);
	editor_app_show_panel (self, "problems");
	editor_app_notify (self, self->problems->len > 0 ? EDITOR_NOTIFY_WARNING : EDITOR_NOTIFY_SUCCESS,
	                   "Validation: %u problem(s)", self->problems->len);
}

/* The command palette and quick open are Wave 4 (P6) */
static void
a_palette (gpointer d)
{
	editor_app_notify (APP (d), EDITOR_NOTIFY_INFO, "Command palette: coming in Wave 4 (use the menus or action.run)");
}

static void
a_quick_open (gpointer d)
{
	editor_app_notify (APP (d), EDITOR_NOTIFY_INFO, "Quick open: coming in Wave 4");
}

/* The level is plain YAML: open it in the external editor (Emacs edits
 * reload live when there are no unsaved changes here). */
static void
a_open_level_yaml (gpointer data)
{
	LrgEditorApp *self = APP (data);
	const gchar  *path = lrg_editor_get_path (self->editor);

	if (path == NULL)
	{
		editor_app_notify (self, EDITOR_NOTIFY_WARNING, "Save the level first");
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
			editor_app_notify (self, EDITOR_NOTIFY_ERROR, "IPC server: %s", error->message);
			return;
		}
		sock = lrg_editor_server_get_socket_path (self->server);
	}
	editor_app_set_clipboard (self, sock);
	editor_app_notify (self, EDITOR_NOTIFY_INFO, "IPC socket (copied): %s", sock);
}

static void
a_refresh_assets (gpointer data)
{
	LrgEditorApp     *self = APP (data);
	g_autoptr(GError) error = NULL;

	if (self->assets != NULL && !lrg_asset_database_scan (self->assets, &error))
		editor_app_notify (self, EDITOR_NOTIFY_WARNING, "Refresh assets: %s", error->message);
}

static void a_shortcuts (gpointer d) { editor_dialog_shortcuts (APP (d)); }
static void a_about (gpointer d)     { editor_dialog_about (APP (d)); }

/* The editor docs: next to the binary in a source tree
 * (build/<type>/lrgeditor -> docs/editor), or installed. */
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
		editor_app_notify (self, EDITOR_NOTIFY_WARNING, "Editor documentation not found (docs/editor/ in the source tree)");
}

/* ==========================================================================
 * Tilemap painting, timeline and thumbnail actions
 * ========================================================================== */

static gboolean
has_tile_target (gpointer data)
{
	return editor_tiles_target (APP (data)) != NULL;
}

static gboolean
tile_mode_on (gpointer data)
{
	return lrg_editor_view_state_get_tile_mode (APP (data)->view) && editor_tiles_target (APP (data)) != NULL;
}

#define TILE_TOOL(d) lrg_editor_view_state_get_tile_tool (APP (d)->view)
static gboolean tile_tool_brush (gpointer d)  { return TILE_TOOL (d) == LRG_EDITOR_TILE_TOOL_BRUSH; }
static gboolean tile_tool_eraser (gpointer d) { return TILE_TOOL (d) == LRG_EDITOR_TILE_TOOL_ERASER; }
static gboolean tile_tool_rect (gpointer d)   { return TILE_TOOL (d) == LRG_EDITOR_TILE_TOOL_RECT; }
static gboolean tile_tool_fill (gpointer d)   { return TILE_TOOL (d) == LRG_EDITOR_TILE_TOOL_FILL; }
static gboolean tile_tool_pick (gpointer d)   { return TILE_TOOL (d) == LRG_EDITOR_TILE_TOOL_PICK; }

static void
a_tile_mode (gpointer data)
{
	LrgEditorApp *self = APP (data);

	gboolean      on;

	if (editor_tiles_target (self) == NULL)
	{
		lrg_editor_view_state_set_tile_mode (self->view, FALSE);
		editor_app_set_status (self, "Select a tilemap node to paint");
		return;
	}
	on = !lrg_editor_view_state_get_tile_mode (self->view);
	lrg_editor_view_state_set_tile_mode (self->view, on);
	if (on)
		editor_app_show_panel (self, "tiles");
	editor_app_set_status (self, on ? "Tile painting on" : "Tile painting off");
}

static void
set_tile_tool (LrgEditorApp      *self,
               LrgEditorTileTool  tool)
{
	lrg_editor_view_state_set_tile_tool (self->view, tool);
	lrg_editor_view_state_set_tile_mode (self->view, editor_tiles_target (self) != NULL);
}

static void a_tile_brush (gpointer d)  { set_tile_tool (APP (d), LRG_EDITOR_TILE_TOOL_BRUSH); }
static void a_tile_eraser (gpointer d) { set_tile_tool (APP (d), LRG_EDITOR_TILE_TOOL_ERASER); }
static void a_tile_rect (gpointer d)   { set_tile_tool (APP (d), LRG_EDITOR_TILE_TOOL_RECT); }
static void a_tile_fill (gpointer d)   { set_tile_tool (APP (d), LRG_EDITOR_TILE_TOOL_FILL); }
static void a_tile_pick (gpointer d)   { set_tile_tool (APP (d), LRG_EDITOR_TILE_TOOL_PICK); }

static void
a_tile_brush_grow (gpointer data)
{
	LrgEditorTilePainter *p = APP (data)->tile_painter;

	lrg_editor_tile_painter_set_brush_size (p, lrg_editor_tile_painter_get_brush_size (p) + 1);
}

static void
a_tile_brush_shrink (gpointer data)
{
	LrgEditorTilePainter *p = APP (data)->tile_painter;
	guint                 b = lrg_editor_tile_painter_get_brush_size (p);

	lrg_editor_tile_painter_set_brush_size (p, b > 1 ? b - 1 : 1);
}

/* Adds or removes a layer of the target's grid as one undo step. */
static void
edit_tile_layers (LrgEditorApp *self,
                  gboolean      add)
{
	LrgNode            *node = editor_tiles_target (self);
	LrgNodeTilemap     *grid;
	g_autoptr(LrgNode)  before = NULL;
	guint               layer;

	if (node == NULL)
		return;
	lrg_editor_begin_group (self->editor, add ? "Add Tile Layer" : "Remove Tile Layer");
	grid = lrg_editor_tile_painter_ensure_tilemap (self->tile_painter, node);
	before = lrg_editor_snapshot_node (node);
	layer = lrg_editor_tile_painter_get_layer (self->tile_painter);
	if (add)
		layer = lrg_node_tilemap_add_layer (grid, NULL);
	else if (lrg_node_tilemap_remove_layer (grid, MIN (layer, lrg_node_tilemap_get_n_layers (grid) - 1)))
		layer = (layer > 0) ? layer - 1 : 0;
	lrg_editor_commit_node_state (self->editor, node, before, add ? "Add Tile Layer" : "Remove Tile Layer", FALSE);
	lrg_editor_end_group (self->editor);
	lrg_editor_tile_painter_set_layer (self->tile_painter, layer);
}

static void a_tile_add_layer (gpointer d)    { edit_tile_layers (APP (d), TRUE); }
static void a_tile_remove_layer (gpointer d) { edit_tile_layers (APP (d), FALSE); }

static void
a_tile_panel (gpointer data)
{
	editor_app_show_panel (APP (data), "tiles");
}

static gboolean
has_timeline (gpointer data)
{
	return editor_timeline_current (APP (data)) != NULL;
}

static gboolean
timeline_playing (gpointer data)
{
	return lrg_editor_view_state_get_timeline_playing (APP (data)->view);
}

static gboolean
timeline_key_selected (gpointer data)
{
	return APP (data)->timeline_key_valid && editor_timeline_current (APP (data)) != NULL;
}

static void
a_timeline_panel (gpointer data)
{
	editor_app_show_panel (APP (data), "timeline");
}

static void
a_timeline_key (gpointer data)
{
	LrgEditorApp *self = APP (data);

	if (editor_timeline_key_selection (self) > 0)
		editor_app_show_panel (self, "timeline");
}

static void
a_timeline_play (gpointer data)
{
	LrgEditorApp *self = APP (data);

	editor_timeline_set_playing (self, !lrg_editor_view_state_get_timeline_playing (self->view));
}

static void
a_timeline_stop (gpointer data)
{
	LrgEditorApp *self = APP (data);

	editor_timeline_end_preview (self);
	lrg_editor_view_state_set_timeline_time (self->view, 0.0f);
}

static void
a_timeline_new (gpointer data)
{
	LrgEditorApp     *self = APP (data);
	LrgLevel         *level = lrg_editor_get_level (self->editor);
	g_autofree gchar *name = g_strdup ("Timeline");
	guint             n = 2;

	while (lrg_level_find_timeline (level, name) >= 0)
	{
		g_free (name);
		name = g_strdup_printf ("Timeline %u", n++);
	}
	if (lrg_editor_timeline_add (self->editor, name, 2.0f, FALSE))
	{
		editor_timeline_end_preview (self);
		lrg_editor_view_state_set_timeline_name (self->view, name);
		lrg_editor_view_state_set_timeline_time (self->view, 0.0f);
		editor_app_show_panel (self, "timeline");
	}
}

static void
a_timeline_delete_key (gpointer data)
{
	LrgEditorApp *self = APP (data);
	LrgTimeline  *tl = editor_timeline_current (self);
	LrgNode      *node = self->timeline_key_valid ? lrg_editor_find_node (self->editor, self->timeline_key_guid) : NULL;

	if (tl == NULL || node == NULL)
		return;
	lrg_editor_timeline_remove_key (self->editor, lrg_timeline_get_name (tl), node, self->timeline_key_channel,
	                                self->timeline_key_time);
	self->timeline_key_valid = FALSE;
}

/* Moves the playhead to the next (@dir > 0) or previous key of any track. */
static void
jump_key (LrgEditorApp *self,
          gint          dir)
{
	LrgTimeline *tl = editor_timeline_current (self);
	gfloat       best = dir > 0 ? G_MAXFLOAT : -G_MAXFLOAT;
	gfloat       now = lrg_editor_view_state_get_timeline_time (self->view);
	guint        i, k;

	if (tl == NULL)
		return;
	for (i = 0; i < lrg_timeline_get_n_tracks (tl); i++)
	{
		LrgTimelineTrack *t = lrg_timeline_get_track (tl, i);

		for (k = 0; k < lrg_timeline_track_get_n_keys (t); k++)
		{
			gfloat kt = 0.0f;

			lrg_timeline_track_get_key (t, k, &kt, NULL, NULL);
			if (dir > 0 && kt > now + 0.0005f && kt < best)
				best = kt;
			if (dir < 0 && kt < now - 0.0005f && kt > best)
				best = kt;
		}
	}
	if (best != G_MAXFLOAT && best != -G_MAXFLOAT)
		editor_timeline_seek (self, best);
}

static void a_timeline_next_key (gpointer d) { jump_key (APP (d), 1); }
static void a_timeline_prev_key (gpointer d) { jump_key (APP (d), -1); }

/* The asset grid view belongs to the assets panel (Wave 4); the toggle
 * is kept so bindings and scripts that name it keep working. */
static gboolean asset_grid_on (gpointer d) { return GPOINTER_TO_INT (g_object_get_data (G_OBJECT (d), "lrg-asset-grid")); }
static void     a_asset_grid (gpointer d)
{
	g_object_set_data (G_OBJECT (d), "lrg-asset-grid", GINT_TO_POINTER (!asset_grid_on (d)));
}

static void
a_rebuild_thumbnails (gpointer data)
{
	editor_thumbnails_clear (APP (data));
	editor_app_set_status (APP (data), "Rebuilding thumbnails");
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
		{ "view.orbit-left", "Orbit Left", "View", "Num4", a_orbit_left, NULL, NULL },
		{ "view.orbit-right", "Orbit Right", "View", "Num6", a_orbit_right, NULL, NULL },
		{ "view.orbit-up", "Orbit Up", "View", "Num8", a_orbit_up, NULL, NULL },
		{ "view.orbit-down", "Orbit Down", "View", "Num2", a_orbit_down, NULL, NULL },
		{ "view.zoom-in", "Zoom In", "View", "=", a_zoom_in, NULL, NULL },
		{ "view.zoom-out", "Zoom Out", "View", "-", a_zoom_out, NULL, NULL },
		{ "view.toggle-2d", "2D Mode", "View", "Ctrl+Shift+2", a_toggle_2d, NULL, is_2d },
		{ "view.grid", "Show Grid", "View", "Shift+G", a_grid, NULL, grid_on },
		{ "view.icons", "Show Icons", "View", NULL, a_icons, NULL, icons_on },
		{ "view.wireframe", "Wireframe", "View", "Shift+Z", a_wireframe, NULL, wire_on },
		{ "view.stats", "Show Stats", "View", NULL, a_stats, NULL, stats_on },
		{ "view.reset-layout", "Reset Layout", "View", NULL, a_reset_layout, NULL, NULL },
		{ "view.layout-single", "Single Viewport", "View", NULL, a_layout_single, NULL, NULL },
		{ "view.layout-split-h", "Two Viewports Side by Side", "View", NULL, a_layout_split_h, NULL, NULL },
		{ "view.layout-split-v", "Two Viewports Stacked", "View", NULL, a_layout_split_v, NULL, NULL },
		{ "view.layout-quad", "Four Viewports", "View", NULL, a_layout_quad, NULL, NULL },
		{ "view.next-viewport", "Next Viewport", "View", NULL, a_next_viewport, NULL, NULL },

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
		{ "tools.rebuild-thumbnails", "Rebuild Thumbnails", "Tools", NULL, a_rebuild_thumbnails, NULL, NULL },
		{ "view.asset-grid", "Asset Thumbnails (Grid View)", "View", NULL, a_asset_grid, NULL, asset_grid_on },

		{ "tilemap.paint-mode", "Paint Tiles", "Tilemap", "B", a_tile_mode, has_tile_target, tile_mode_on },
		{ "tilemap.tool-brush", "Tile Brush", "Tilemap", NULL, a_tile_brush, has_tile_target, tile_tool_brush },
		{ "tilemap.tool-eraser", "Tile Eraser", "Tilemap", NULL, a_tile_eraser, has_tile_target, tile_tool_eraser },
		{ "tilemap.tool-rect", "Tile Rectangle", "Tilemap", NULL, a_tile_rect, has_tile_target, tile_tool_rect },
		{ "tilemap.tool-fill", "Tile Fill", "Tilemap", NULL, a_tile_fill, has_tile_target, tile_tool_fill },
		{ "tilemap.tool-pick", "Tile Eyedropper", "Tilemap", NULL, a_tile_pick, has_tile_target, tile_tool_pick },
		{ "tilemap.brush-grow", "Bigger Tile Brush", "Tilemap", "]", a_tile_brush_grow, has_tile_target, NULL },
		{ "tilemap.brush-shrink", "Smaller Tile Brush", "Tilemap", "[", a_tile_brush_shrink, has_tile_target, NULL },
		{ "tilemap.add-layer", "Add Tile Layer", "Tilemap", NULL, a_tile_add_layer, has_tile_target, NULL },
		{ "tilemap.remove-layer", "Remove Tile Layer", "Tilemap", NULL, a_tile_remove_layer, has_tile_target, NULL },
		{ "tilemap.show-panel", "Tiles Panel", "Tilemap", NULL, a_tile_panel, NULL, NULL },

		{ "timeline.show-panel", "Timeline Panel", "Animation", NULL, a_timeline_panel, NULL, NULL },
		{ "timeline.new", "New Timeline", "Animation", NULL, a_timeline_new, NULL, NULL },
		{ "timeline.key", "Insert Keyframe", "Animation", "I", a_timeline_key, has_selection, NULL },
		{ "timeline.play", "Play Timeline", "Animation", "K", a_timeline_play, has_timeline, timeline_playing },
		{ "timeline.stop", "Stop Timeline", "Animation", NULL, a_timeline_stop, has_timeline, NULL },
		{ "timeline.next-key", "Next Keyframe", "Animation", "Alt+Right", a_timeline_next_key, has_timeline, NULL },
		{ "timeline.prev-key", "Previous Keyframe", "Animation", "Alt+Left", a_timeline_prev_key, has_timeline, NULL },
		{ "timeline.delete-key", "Delete Keyframe", "Animation", NULL, a_timeline_delete_key, timeline_key_selected,
		  NULL },

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

/* The camera of the "viewport" parameter (an index; omitted or -1 is the
 * active viewport). */
static gboolean
p_viewport (LrgEditorApp  *self,
            JsonObject    *params,
            gint          *index,
            GError       **error)
{
	JsonNode *n;
	gint64    v;

	*index = -1;
	if (params == NULL || !json_object_has_member (params, "viewport"))
		return TRUE;
	n = json_object_get_member (params, "viewport");
	if (!JSON_NODE_HOLDS_VALUE (n) || json_node_get_value_type (n) != G_TYPE_INT64)
	{
		g_set_error_literal (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS,
		                     "'viewport' must be an integer");
		return FALSE;
	}
	v = json_node_get_int (n);
	if (v < -1 || v >= LRG_EDITOR_VIEW_MAX_VIEWPORTS)
	{
		g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS,
		             "'viewport' must be -1..%d", LRG_EDITOR_VIEW_MAX_VIEWPORTS - 1);
		return FALSE;
	}
	*index = (gint) v;
	return TRUE;
}

static JsonNode *
view_json (LrgEditorApp *self,
           gint          index)
{
	LrgEditorViewCamera *camera = lrg_editor_view_state_get_camera (self->view, index);
	JsonObject          *o = json_object_new ();

	vec3_member (o, "position", camera->view.position);
	vec3_member (o, "target", camera->view.target);
	json_object_set_double_member (o, "fovy", camera->view.fovy);
	json_object_set_boolean_member (o, "orthographic", camera->view.orthographic);
	json_object_set_double_member (o, "ortho_height", camera->view.ortho_height);
	json_object_set_boolean_member (o, "mode_2d", camera->view_2d);
	json_object_set_int_member (o, "viewport", index >= 0 ? index
	                            : (gint64) lrg_editor_view_state_get_active_viewport (self->view));
	return obj_node (o);
}

static JsonNode *
m_view_get (LrgEditorServer  *server,
            JsonObject       *params,
            gpointer          user_data,
            GError          **error)
{
	gint index;

	if (!p_viewport (APP (user_data), params, &index, error))
		return NULL;
	return view_json (APP (user_data), index);
}

static JsonNode *
m_view_set (LrgEditorServer  *server,
            JsonObject       *params,
            gpointer          user_data,
            GError          **error)
{
	LrgEditorApp        *self = APP (user_data);
	LrgEditorViewCamera *camera;
	gfloat               v[3];
	gint                 index;

	if (!p_viewport (self, params, &index, error))
		return NULL;
	camera = lrg_editor_view_state_get_camera (self->view, index);

	/* 2D first: it resets the camera, explicit values then apply */
	if (params != NULL && json_object_has_member (params, "mode_2d"))
	{
		gdouble fov = lrg_editor_view_state_get_fov (self->view);

		lrg_editor_view_camera_set_2d (camera, p_bool (params, "mode_2d", FALSE), (gfloat) fov);
	}
	if (read_vec3 (params, "position", v))
		memcpy (camera->view.position, v, sizeof v);
	if (read_vec3 (params, "target", v))
		memcpy (camera->view.target, v, sizeof v);
	if (params != NULL && json_object_has_member (params, "orthographic"))
	{
		camera->view.orthographic = p_bool (params, "orthographic", FALSE);
		camera->ortho_from_align = FALSE;
	}
	if (params != NULL && json_object_has_member (params, "ortho_height"))
		camera->view.ortho_height = MAX (0.01f, (gfloat) json_object_get_double_member (params, "ortho_height"));
	lrg_editor_view_state_camera_changed (self->view, index);
	return view_json (self, index);
}

static JsonNode *
m_view_focus (LrgEditorServer  *server,
              JsonObject       *params,
              gpointer          user_data,
              GError          **error)
{
	LrgEditorApp *self = APP (user_data);
	const gchar  *ref = p_string (params, "node");
	gint          index;
	guint         active;

	if (!p_viewport (self, params, &index, error))
		return NULL;
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

	/* Focus acts on the active viewport: make @index active for it */
	active = lrg_editor_view_state_get_active_viewport (self->view);
	if (index >= 0)
		lrg_editor_view_state_set_active_viewport (self->view, (guint) index);
	lrg_editor_view_state_focus_selection (self->view);
	if (index >= 0)
		lrg_editor_view_state_set_active_viewport (self->view, active);
	return view_json (self, index);
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

	/* The whole window is captured after the next frame is drawn */
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
	if (!editor_viewport_panel_screenshot (self->viewport_panel, abs, error))
		return NULL;
	return str_node (abs);
}

static JsonNode *
play_state (LrgEditorApp *self)
{
	JsonObject *o = json_object_new ();

	json_object_set_boolean_member (o, "playing", lrg_editor_play_session_is_playing (self->play));
	json_object_set_boolean_member (o, "paused", lrg_editor_play_session_is_paused (self->play));
	json_object_set_double_member (o, "time", lrg_editor_play_session_get_time (self->play));
	json_object_set_boolean_member (o, "running", self->run_proc != NULL);
	return obj_node (o);
}

static JsonNode *
m_play_start (LrgEditorServer *s, JsonObject *p, gpointer d, GError **e)
{
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
	/* A read-only scan: asset.list never writes .meta sidecars */
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

static JsonArray *
rect_array (gfloat x,
            gfloat y,
            gfloat w,
            gfloat h)
{
	JsonArray *r = json_array_new ();

	json_array_add_double_element (r, x);
	json_array_add_double_element (r, y);
	json_array_add_double_element (r, w);
	json_array_add_double_element (r, h);
	return r;
}

/*
 * ui.state: what the UI is doing and the input it last saw, for tools,
 * tests (tests/editor-x11-smoke.sh clicks the menubar rectangles) and
 * diagnosing "my clicks do nothing". The 0.8.0 fields keep their names
 * and meaning; 0.8.1 adds "viewports", "active_viewport", "layout",
 * "focus" and "panels".
 */
static JsonNode *
m_ui_state (LrgEditorServer  *server,
            JsonObject       *params,
            gpointer          user_data,
            GError          **error)
{
	LrgEditorApp *self = APP (user_data);
	LrgMenuBar   *bar = lrg_editor_window_get_menu_bar (self->window);
	JsonObject   *o = json_object_new ();
	JsonObject   *in = json_object_new ();
	JsonObject   *menubar = json_object_new ();
	JsonArray    *buttons = json_array_new ();
	JsonArray    *viewports = json_array_new ();
	JsonArray    *panels = json_array_new ();
	LrgMenu      *first;
	LrgWidget    *focus;
	GList        *docked, *l;
	gfloat        x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f;
	guint         i;

	/* Menus: the open one, every title's rectangle, the row height */
	json_object_set_member (o, "menu", str_node (lrg_menu_bar_get_open_title (bar)));
	for (i = 0; i < lrg_menu_bar_get_n_titles (bar); i++)
	{
		x = y = w = h = 0.0f;
		lrg_menu_bar_get_title_rect (bar, i, &x, &y, &w, &h);
		json_object_set_array_member (menubar, lrg_menu_bar_get_title (bar, i), rect_array (x, y, w, h));
	}
	json_object_set_object_member (o, "menubar", menubar);
	first = lrg_menu_bar_get_n_titles (bar) > 0 ? lrg_menu_bar_get_menu (bar, 0) : NULL;
	json_object_set_double_member (o, "frame_height", first != NULL ? lrg_menu_get_row_height (first) : 0.0);

	/* Dialog, palette, navigation, window */
	json_object_set_member (o, "dialog", str_node (lrg_editor_view_state_get_dialog (self->view)));
	json_object_set_boolean_member (o, "palette", lrg_editor_view_state_get_palette_open (self->view));
	json_object_set_string_member (o, "nav", lrg_editor_nav_mode_to_string (lrg_editor_view_state_get_nav_mode (self->view)));
	json_object_set_boolean_member (o, "viewport_hovered", editor_viewport_panel_is_hovered (self->viewport_panel));
	json_object_set_boolean_member (o, "window", self->window_open);
	json_object_set_double_member (o, "fps", self->fps);

	/* The viewport: the 0.8.0 "viewport" rect is the first (and in Wave 3
	 * the only drawn) viewport */
	x = y = w = h = 0.0f;
	editor_viewport_panel_get_rect (self->viewport_panel, &x, &y, &w, &h);
	json_object_set_array_member (o, "viewport", rect_array (x, y, w, h));
	for (i = 0; i < lrg_editor_view_state_get_viewport_count (self->view); i++)
	{
		JsonObject *vp = json_object_new ();
		g_autoptr(GEnumClass) modes = g_type_class_ref (LRG_TYPE_EDITOR_RENDER_MODE);
		GEnumValue *mode = g_enum_get_value (modes, lrg_editor_view_state_get_render_mode (self->view, (gint) i));

		json_object_set_int_member (vp, "index", i);
		{
			gfloat vx = 0.0f, vy = 0.0f, vw = 0.0f, vh = 0.0f;

			/* Each viewport of the split; hidden ones report zeros */
			editor_viewport_panel_get_rect (lrg_editor_viewport_split_get_panel (self->viewport_split, i),
			                                &vx, &vy, &vw, &vh);
			json_object_set_array_member (vp, "rect", rect_array (vx, vy, vw, vh));
		}
		json_object_set_string_member (vp, "render_mode", mode != NULL ? mode->value_nick : "lit");
		json_object_set_boolean_member (vp, "mode_2d", lrg_editor_view_state_get_camera (self->view, (gint) i)->view_2d);
		json_array_add_object_element (viewports, vp);
	}
	json_object_set_array_member (o, "viewports", viewports);
	json_object_set_int_member (o, "active_viewport", lrg_editor_view_state_get_active_viewport (self->view));
	json_object_set_string_member (o, "layout", lrg_editor_view_state_get_layout_name (self->view));

	/* Docked panels and keyboard focus */
	docked = lrg_dock_area_get_panels (lrg_editor_window_get_dock (self->window));
	for (l = docked; l != NULL; l = l->next)
		json_array_add_string_element (panels, lrg_dock_panel_get_id (l->data));
	g_list_free (docked);
	json_object_set_array_member (o, "panels", panels);
	focus = lrg_canvas_get_focused_widget (self->canvas);
	json_object_set_member (o, "focus", str_node (focus != NULL ? (lrg_widget_get_name (focus) != NULL
	                                                                ? lrg_widget_get_name (focus)
	                                                                : G_OBJECT_TYPE_NAME (focus)) : NULL));

	/* The last input frame, as the canvas saw it */
	json_object_set_double_member (in, "x", self->last_input.pointer_x);
	json_object_set_double_member (in, "y", self->last_input.pointer_y);
	json_array_add_boolean_element (buttons, (self->last_input.buttons_down & LRG_CANVAS_BUTTON_MASK (GRL_MOUSE_BUTTON_LEFT)) != 0);
	json_array_add_boolean_element (buttons, (self->last_input.buttons_down & LRG_CANVAS_BUTTON_MASK (GRL_MOUSE_BUTTON_RIGHT)) != 0);
	json_array_add_boolean_element (buttons, (self->last_input.buttons_down & LRG_CANVAS_BUTTON_MASK (GRL_MOUSE_BUTTON_MIDDLE)) != 0);
	json_object_set_array_member (in, "buttons", buttons);
	json_object_set_int_member (in, "mods", self->last_input.modifiers);
	json_object_set_object_member (o, "input", in);
	return obj_node (o);
}

static JsonNode *
m_ui_notify (LrgEditorServer  *server,
             JsonObject       *params,
             gpointer          user_data,
             GError          **error)
{
	static const gchar * const names[] = { "info", "success", "warning", "error" };
	const gchar               *msg = p_string (params, "message");
	const gchar               *lvl = p_string (params, "level");
	JsonNode                  *n;
	gint                       level = 0;
	gsize                      i;

	if (msg == NULL)
	{
		g_set_error_literal (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS,
		                     "Missing string parameter 'message'");
		return NULL;
	}

	/* The level is a name (info, success, warning, error); for clients
	 * from 0.8.0, whose lrgeditorctl sent a number, 0..3 map onto the
	 * same names. Anything else falls back to info rather than failing:
	 * a toast with the wrong colour beats a lost message. */
	for (i = 0; lvl != NULL && i < G_N_ELEMENTS (names); i++)
		if (g_ascii_strcasecmp (lvl, names[i]) == 0)
			level = (gint) i;
	n = (params != NULL && json_object_has_member (params, "level"))
	    ? json_object_get_member (params, "level") : NULL;
	if (lvl == NULL && n != NULL && JSON_NODE_HOLDS_VALUE (n) &&
	    (json_node_get_value_type (n) == G_TYPE_INT64 || json_node_get_value_type (n) == G_TYPE_DOUBLE))
	{
		gint64 v = (json_node_get_value_type (n) == G_TYPE_INT64)
		           ? json_node_get_int (n) : (gint64) json_node_get_double (n);

		level = (gint) CLAMP (v, 0, (gint64) G_N_ELEMENTS (names) - 1);
	}
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

/* ---- editor tools: tile tool state, timeline playback, thumbnails ---- */

static gboolean
p_number (JsonObject  *params,
          const gchar *key,
          gdouble     *out)
{
	JsonNode *n;

	if (params == NULL || !json_object_has_member (params, key))
		return FALSE;
	n = json_object_get_member (params, key);
	if (!JSON_NODE_HOLDS_VALUE (n) ||
	    (json_node_get_value_type (n) != G_TYPE_INT64 && json_node_get_value_type (n) != G_TYPE_DOUBLE))
		return FALSE;
	*out = json_node_get_double (n);
	return isfinite (*out);
}

static JsonNode *
tile_tool_json (LrgEditorApp *self)
{
	JsonObject *o = json_object_new ();
	LrgNode    *target = editor_tiles_target (self);

	json_object_set_string_member (o, "tool", lrg_editor_tile_tool_to_string (lrg_editor_view_state_get_tile_tool (self->view)));
	json_object_set_boolean_member (o, "mode", lrg_editor_view_state_get_tile_mode (self->view));
	json_object_set_int_member (o, "tile", lrg_editor_tile_painter_get_tile (self->tile_painter));
	json_object_set_int_member (o, "layer", lrg_editor_tile_painter_get_layer (self->tile_painter));
	json_object_set_int_member (o, "brush", lrg_editor_tile_painter_get_brush_size (self->tile_painter));
	json_object_set_member (o, "target", str_node (target != NULL ? lrg_node_get_guid (target) : NULL));
	if (self->tile_hover_valid && lrg_editor_view_state_get_tile_mode (self->view))
	{
		JsonArray *hover = json_array_new ();

		json_array_add_int_element (hover, self->tile_hover_x);
		json_array_add_int_element (hover, self->tile_hover_y);
		json_object_set_array_member (o, "hover", hover);
	}
	else
		json_object_set_null_member (o, "hover");
	return obj_node (o);
}

static JsonNode *
m_tilemap_tool (LrgEditorServer  *server,
                JsonObject       *params,
                gpointer          user_data,
                GError          **error)
{
	LrgEditorApp      *self = APP (user_data);
	const gchar       *tool = p_string (params, "tool");
	gdouble            v;
	LrgEditorTileTool  t;

	if (tool != NULL)
	{
		if (!lrg_editor_tile_tool_from_string (tool, &t))
		{
			g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS,
			             "Unknown tile tool '%s' (brush eraser rect fill pick)", tool);
			return NULL;
		}
		lrg_editor_view_state_set_tile_tool (self->view, t);
	}
	if (p_number (params, "tile", &v))
		lrg_editor_tile_painter_set_tile (self->tile_painter, (guint) MAX (v, 0.0));
	if (p_number (params, "layer", &v))
		lrg_editor_tile_painter_set_layer (self->tile_painter, (guint) MAX (v, 0.0));
	if (p_number (params, "brush", &v))
		lrg_editor_tile_painter_set_brush_size (self->tile_painter, (guint) MAX (v, 1.0));
	if (params != NULL && json_object_has_member (params, "mode"))
		lrg_editor_view_state_set_tile_mode (self->view, p_bool (params, "mode", FALSE));
	return tile_tool_json (self);
}

static JsonNode *
timeline_state_json (LrgEditorApp *self)
{
	JsonObject  *o = json_object_new ();
	LrgTimeline *tl = editor_timeline_current (self);

	json_object_set_member (o, "name", str_node (tl != NULL ? lrg_timeline_get_name (tl) : NULL));
	json_object_set_double_member (o, "time", lrg_editor_view_state_get_timeline_time (self->view));
	json_object_set_boolean_member (o, "playing", lrg_editor_view_state_get_timeline_playing (self->view));
	json_object_set_boolean_member (o, "previewing", lrg_editor_view_state_get_timeline_previewing (self->view));
	return obj_node (o);
}

/* Makes "name" (if given) the panel's timeline. */
static gboolean
choose_timeline (LrgEditorApp  *self,
                 JsonObject    *params,
                 GError       **error)
{
	const gchar *name = p_string (params, "name");

	if (name == NULL)
	{
		if (editor_timeline_current (self) != NULL)
			return TRUE;
		g_set_error_literal (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_NOT_FOUND,
		                     "The level has no timeline");
		return FALSE;
	}
	if (lrg_level_find_timeline (lrg_editor_get_level (self->editor), name) < 0)
	{
		g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_NOT_FOUND, "No timeline '%s'", name);
		return FALSE;
	}
	if (g_strcmp0 (name, lrg_editor_view_state_get_timeline_name (self->view)) != 0)
	{
		editor_timeline_end_preview (self);
		lrg_editor_view_state_set_timeline_name (self->view, name);
		lrg_editor_view_state_set_timeline_time (self->view, 0.0f);
	}
	return TRUE;
}

static JsonNode *
m_timeline_play (LrgEditorServer  *server,
                 JsonObject       *params,
                 gpointer          user_data,
                 GError          **error)
{
	LrgEditorApp *self = APP (user_data);
	gdouble       from;

	if (!choose_timeline (self, params, error))
		return NULL;
	if (p_number (params, "from", &from))
		lrg_editor_view_state_set_timeline_time (self->view, (gfloat) MAX (from, 0.0));
	editor_timeline_set_playing (self, p_bool (params, "playing", TRUE));
	return timeline_state_json (self);
}

static JsonNode *
m_timeline_stop (LrgEditorServer  *server,
                 JsonObject       *params,
                 gpointer          user_data,
                 GError          **error)
{
	LrgEditorApp *self = APP (user_data);

	editor_timeline_end_preview (self);
	lrg_editor_view_state_set_timeline_time (self->view, 0.0f);
	return timeline_state_json (self);
}

static JsonNode *
m_timeline_seek (LrgEditorServer  *server,
                 JsonObject       *params,
                 gpointer          user_data,
                 GError          **error)
{
	LrgEditorApp *self = APP (user_data);
	gdouble       time;

	if (!p_number (params, "time", &time))
	{
		g_set_error_literal (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS,
		                     "Missing number parameter 'time'");
		return NULL;
	}
	if (!choose_timeline (self, params, error))
		return NULL;
	lrg_editor_view_state_set_timeline_playing (self->view, FALSE);
	editor_timeline_seek (self, (gfloat) time);
	return timeline_state_json (self);
}

static JsonNode *
m_timeline_state (LrgEditorServer  *server,
                  JsonObject       *params,
                  gpointer          user_data,
                  GError          **error)
{
	return timeline_state_json (APP (user_data));
}

static JsonNode *
m_asset_thumbnail (LrgEditorServer  *server,
                   JsonObject       *params,
                   gpointer          user_data,
                   GError          **error)
{
	LrgEditorApp      *self = APP (user_data);
	const gchar       *path = p_string (params, "path");
	LrgThumbnailCache *cache;
	g_autofree gchar  *abs = NULL;
	g_autofree gchar  *thumb = NULL;
	JsonObject        *o;

	if (path == NULL)
	{
		g_set_error_literal (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS,
		                     "Missing string parameter 'path'");
		return NULL;
	}
	abs = editor_app_project_path (self, path);
	cache = editor_thumbnails_cache (self);
	if (!g_file_test (abs, G_FILE_TEST_IS_REGULAR))
	{
		g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_NOT_FOUND, "No file '%s'", abs);
		return NULL;
	}
	if (!lrg_thumbnail_cache_is_supported (cache, abs))
	{
		g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_FAILED,
		             "No thumbnail for '%s' (textures, levels, prefabs; models need a window)", abs);
		return NULL;
	}

	/* wait: generate now (GPU kinds render on this, the GL, thread);
	 * otherwise report the cache state and queue a request. */
	if (p_bool (params, "wait", FALSE))
	{
		thumb = lrg_thumbnail_cache_generate (cache, abs, error);
		if (thumb == NULL)
			return NULL;
	}
	else if ((thumb = lrg_thumbnail_cache_lookup (cache, abs)) == NULL)
		lrg_thumbnail_cache_request (cache, abs);

	o = json_object_new ();
	json_object_set_string_member (o, "path", abs);
	json_object_set_member (o, "thumbnail", str_node (thumb));
	json_object_set_boolean_member (o, "pending", thumb == NULL);
	json_object_set_string_member (o, "cache", lrg_thumbnail_cache_get_dir (cache));
	return obj_node (o);
}

/* level.save, overriding the document method so a timeline preview is
 * put back before writing (the pose is not part of the document). */
static JsonNode *
m_level_save_app (LrgEditorServer  *server,
                  JsonObject       *params,
                  gpointer          user_data,
                  GError          **error)
{
	LrgEditorApp *self = APP (user_data);
	const gchar  *path = p_string (params, "path");
	gboolean      ok;

	editor_timeline_end_preview (self);
	if (path != NULL)
	{
		g_autofree gchar *full = editor_app_project_path (self, path);

		ok = lrg_editor_save_level (self->editor, full, error);
	}
	else
		ok = lrg_editor_save (self->editor, error);
	return ok ? str_node (lrg_editor_get_path (self->editor)) : NULL;
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
		{ "ui.state", m_ui_state },
		{ "editor.quit", m_editor_quit },
		{ "tilemap.tool", m_tilemap_tool },
		{ "timeline.play", m_timeline_play },
		{ "timeline.stop", m_timeline_stop },
		{ "timeline.seek", m_timeline_seek },
		{ "timeline.state", m_timeline_state },
		{ "asset.thumbnail", m_asset_thumbnail },
		{ "level.save", m_level_save_app },
	};
	gsize i;

	for (i = 0; i < G_N_ELEMENTS (table); i++)
		lrg_editor_server_register_method (self->server, table[i].name, table[i].func, self, NULL);
}
