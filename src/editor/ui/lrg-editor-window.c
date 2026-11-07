/* lrg-editor-window.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgEditorWindow: menu bar, toolbar, dock area, status bar and toasts of
 * the editor, on the retained UI toolkit.
 *
 * Actions. LrgEditorActions is a plain GActionGroup (no GActionMap), but
 * LrgMenu and LrgMenuBar look actions up with lrg_widget_lookup_action(),
 * which needs a map. The window therefore mirrors the registry into a
 * GSimpleActionGroup inserted as "editor": one GSimpleAction per id
 * (boolean-stateful for toggles), kept in step through the registry's
 * action-added / -removed / -enabled-changed / -state-changed signals,
 * and activating a mirror action runs the registry's action. A second
 * group, "win", holds what the registry has no action for: opening a
 * recent level or project (string target), creating a node of a kind
 * (string target) and showing or hiding each panel (boolean state).
 *
 * Shortcuts. Every accelerator of every action becomes a canvas shortcut
 * on "editor.<id>"; the set is rebuilt when an action's accelerators
 * change (::accels-changed) or actions come and go. The "editor" group is
 * inserted on the canvas too, so shortcuts resolve with no widget
 * focused; it is taken off the canvas while a modal dialog is open (see
 * editor_window_set_menus_enabled()), which is what keeps shortcuts from
 * firing behind dialogs.
 *
 * Refresh. lrg_editor_actions_refresh_state() runs once per frame from
 * editor_window_refresh(), which also syncs the toolbar's checked states,
 * the snap fields and the status bar texts.
 */

#include "config.h"

#include "lrg-editor-app-private.h"
#include "../../ui/lrg-button.h"
#include "../../ui/lrg-label.h"
#include "../../ui/lrg-hbox.h"
#include "../../ui/lrg-spin-box.h"
#include "../../ui/lrg-combo-box.h"
#include "../../ui/lrg-separator.h"
#include "../../ui/lrg-toast.h"
#include "../../paint/lrg-paint-list.h"
#include <string.h>

/* The editor's top-level menus, in menu-bar order (ui.state reports
 * these labels and their rectangles). */
static const gchar * const menu_titles[] = {
	"_File", "_Edit", "_Create", "_Node", "_View", "_Play", "_Tools", "_Help"
};

/* Panels listed in View > Panels (id, label). */
static const gchar * const panel_entries[][2] = {
	{ "hierarchy", "Hierarchy" }, { "inspector", "Inspector" }, { "viewport", "Viewport" },
	{ "assets", "Assets" }, { "console", "Console" }, { "history", "History" },
	{ "problems", "Problems" }, { "tiles", "Tiles" }, { "timeline", "Timeline" },
	{ "profiler", "Profiler" }, { "code", "Code" }, { "sources", "Sources" }
};

/* Toolbar toggle buttons: label and the action they run and reflect. */
static const gchar * const tool_entries[][2] = {
	{ "Select", "tool.select" }, { "Move", "tool.move" }, { "Rotate", "tool.rotate" },
	{ "Scale", "tool.scale" }, { "-", NULL }, { "World", "tool.toggle-space" },
	{ "Snap", "tool.toggle-snap" }
};

static const gchar * const play_entries[][2] = {
	{ "3D", "view.toggle-2d" }, { "-", NULL }, { "Play", "play.start" }, { "Pause", "play.pause" },
	{ "Step", "play.step" }, { "Stop", "play.stop" }, { "-", NULL }, { "Run Game", "run.start" }
};

static const gchar * const layout_names[] = { "single", "split-h", "split-v", "quad" };

struct _LrgEditorWindow
{
	LrgContainer          parent_instance;

	LrgEditorApp         *app;             /* not owned: the app owns the window */

	/* Actions */
	GSimpleActionGroup   *editor_group;    /* mirror of LrgEditorActions */
	GSimpleActionGroup   *win_group;
	gboolean              editor_on_canvas;
	gboolean              shortcuts_dirty;
	LrgCanvas            *shortcut_canvas; /* canvas our shortcuts are on (weak) */

	/* Chrome */
	GMenu                *menu_model;
	GMenu                *recent_menu;
	LrgMenuBar           *menu_bar;
	LrgHBox              *toolbar;
	GHashTable           *tool_buttons;    /* action id -> LrgButton* (borrowed) */
	LrgSpinBox           *snap_move;
	LrgSpinBox           *snap_rotate;
	LrgSpinBox           *snap_scale;
	LrgComboBox          *layout_combo;
	gboolean              syncing;         /* writing widget values from the model */
	LrgDockArea          *dock;
	LrgHBox              *statusbar;
	LrgLabel             *doc_label;
	LrgLabel             *play_label;
	LrgLabel             *disk_label;
	LrgButton            *reload_button;
	LrgButton            *keep_button;
	LrgLabel             *status_label;
	LrgButton            *counters;
	LrgToastQueue        *toasts;
};

G_DEFINE_FINAL_TYPE (LrgEditorWindow, lrg_editor_window, LRG_TYPE_CONTAINER)

/* ==========================================================================
 * The "editor" mirror group
 * ========================================================================== */

/* A mirror action was activated (menu, toolbar, shortcut): run the
 * registry action; the registry refreshes enabled / state itself. */
static void
on_mirror_activate (GSimpleAction *action,
                    GVariant      *parameter,
                    gpointer       user_data)
{
	LrgEditorWindow *self = LRG_EDITOR_WINDOW (user_data);

	lrg_editor_actions_activate (self->app->actions, g_action_get_name (G_ACTION (action)));
}

/* A toggle's state was set from outside (a check-item target): run the
 * action only when the value really differs, as GTK does. */
static void
on_mirror_change_state (GSimpleAction *action,
                        GVariant      *value,
                        gpointer       user_data)
{
	LrgEditorWindow     *self = LRG_EDITOR_WINDOW (user_data);
	g_autoptr(GVariant)  state = g_action_get_state (G_ACTION (action));

	if (state != NULL && value != NULL && !g_variant_equal (state, value))
		lrg_editor_actions_activate (self->app->actions, g_action_get_name (G_ACTION (action)));
}

/* Adds the mirror of registry action @id. */
static void
mirror_add (LrgEditorWindow *self,
            const gchar     *id)
{
	GActionGroup          *group = G_ACTION_GROUP (self->app->actions);
	g_autoptr(GSimpleAction) action = NULL;
	g_autoptr(GVariant)    state = NULL;

	if (!g_action_name_is_valid (id))
		return;

	state = g_action_group_get_action_state (group, id);
	if (state != NULL)
		action = g_simple_action_new_stateful (id, NULL, state); /* takes its own reference */
	else
		action = g_simple_action_new (id, NULL);
	g_simple_action_set_enabled (action, g_action_group_get_action_enabled (group, id));
	g_signal_connect (action, "activate", G_CALLBACK (on_mirror_activate), self);
	if (state != NULL)
		g_signal_connect (action, "change-state", G_CALLBACK (on_mirror_change_state), self);
	g_action_map_add_action (G_ACTION_MAP (self->editor_group), G_ACTION (action));
}

static void
on_registry_added (GActionGroup *group,
                   const gchar  *id,
                   gpointer      user_data)
{
	LrgEditorWindow *self = LRG_EDITOR_WINDOW (user_data);

	mirror_add (self, id);
	self->shortcuts_dirty = TRUE;
}

static void
on_registry_removed (GActionGroup *group,
                     const gchar  *id,
                     gpointer      user_data)
{
	LrgEditorWindow *self = LRG_EDITOR_WINDOW (user_data);

	g_action_map_remove_action (G_ACTION_MAP (self->editor_group), id);
	self->shortcuts_dirty = TRUE;
}

static void
on_registry_enabled (GActionGroup *group,
                     const gchar  *id,
                     gboolean      enabled,
                     gpointer      user_data)
{
	LrgEditorWindow *self = LRG_EDITOR_WINDOW (user_data);
	GAction         *action = g_action_map_lookup_action (G_ACTION_MAP (self->editor_group), id);

	if (action != NULL)
		g_simple_action_set_enabled (G_SIMPLE_ACTION (action), enabled);
}

static void
on_registry_state (GActionGroup *group,
                   const gchar  *id,
                   GVariant     *state,
                   gpointer      user_data)
{
	LrgEditorWindow *self = LRG_EDITOR_WINDOW (user_data);
	GAction         *action = g_action_map_lookup_action (G_ACTION_MAP (self->editor_group), id);

	if (action != NULL && g_action_get_state_type (action) != NULL)
		g_simple_action_set_state (G_SIMPLE_ACTION (action), state);
}

static void
on_accels_changed (LrgEditorActions *actions,
                   const gchar      *id,
                   gpointer          user_data)
{
	LRG_EDITOR_WINDOW (user_data)->shortcuts_dirty = TRUE;
}

/* ==========================================================================
 * The "win" group: recent files, node creation, panel toggles
 * ========================================================================== */

static void
on_open_recent_level (GSimpleAction *action,
                      GVariant      *parameter,
                      gpointer       user_data)
{
	LrgEditorWindow *self = LRG_EDITOR_WINDOW (user_data);

	editor_app_request (self->app, EDITOR_PENDING_OPEN_LEVEL, g_variant_get_string (parameter, NULL));
}

static void
on_open_recent_project (GSimpleAction *action,
                        GVariant      *parameter,
                        gpointer       user_data)
{
	LrgEditorWindow *self = LRG_EDITOR_WINDOW (user_data);

	editor_app_request (self->app, EDITOR_PENDING_OPEN_PROJECT, g_variant_get_string (parameter, NULL));
}

static void
on_create (GSimpleAction *action,
           GVariant      *parameter,
           gpointer       user_data)
{
	LrgEditorWindow *self = LRG_EDITOR_WINDOW (user_data);
	LrgNode         *node = editor_app_create_node_kind (self->app, g_variant_get_string (parameter, NULL), NULL);

	if (node != NULL)
		editor_app_set_status (self->app, "Created %s", lrg_node_get_name (node));
}

/* View > Panels: a boolean action per panel; activating it toggles. */
static void
on_panel_activate (GSimpleAction *action,
                   GVariant      *parameter,
                   gpointer       user_data)
{
	LrgEditorWindow *self = LRG_EDITOR_WINDOW (user_data);
	const gchar     *name = g_action_get_name (G_ACTION (action));

	/* "panel-<id>" */
	editor_app_toggle_panel (self->app, name + strlen ("panel-"));
}

static void
add_win_actions (LrgEditorWindow *self)
{
	static const GActionEntry entries[] = {
		{ "open-recent-level", on_open_recent_level, "s", NULL, NULL, { 0 } },
		{ "open-recent-project", on_open_recent_project, "s", NULL, NULL, { 0 } },
		{ "create", on_create, "s", NULL, NULL, { 0 } },
	};
	gsize i;

	g_action_map_add_action_entries (G_ACTION_MAP (self->win_group), entries, G_N_ELEMENTS (entries), self);
	for (i = 0; i < G_N_ELEMENTS (panel_entries); i++)
	{
		g_autofree gchar         *name = g_strconcat ("panel-", panel_entries[i][0], NULL);
		g_autoptr(GSimpleAction)  action = g_simple_action_new_stateful (name, NULL,
		                                                                 g_variant_new_boolean (FALSE));

		g_signal_connect (action, "activate", G_CALLBACK (on_panel_activate), self);
		g_action_map_add_action (G_ACTION_MAP (self->win_group), G_ACTION (action));
	}
}

/* ==========================================================================
 * Menu model
 * ========================================================================== */

/* Appends the "editor.<id>" item of each id; "-" starts a new section
 * (drawn as a separator). */
static void
append_items (GMenu               *menu,
              LrgEditorActions    *actions,
              const gchar * const *ids)
{
	g_autoptr(GMenu) section = g_menu_new ();
	guint            i;

	for (i = 0; ids[i] != NULL; i++)
	{
		if (ids[i][0] == '-')
		{
			g_menu_append_section (menu, NULL, G_MENU_MODEL (section));
			g_clear_object (&section);
			section = g_menu_new ();
			continue;
		}
		if (lrg_editor_actions_has (actions, ids[i]))
		{
			g_autofree gchar *detailed = g_strconcat ("editor.", ids[i], NULL);

			g_menu_append (section, lrg_editor_actions_get_label (actions, ids[i]), detailed);
		}
	}
	if (g_menu_model_get_n_items (G_MENU_MODEL (section)) > 0)
		g_menu_append_section (menu, NULL, G_MENU_MODEL (section));
}

/* File > Recent: recent levels, then recent projects. */
void
editor_window_rebuild_recent (LrgEditorWindow *self)
{
	const gchar * const *levels = lrg_editor_settings_get_recent_levels (self->app->settings);
	const gchar * const *projects = lrg_editor_settings_get_recent_projects (self->app->settings);
	g_autoptr(GMenu)     lsec = g_menu_new ();
	g_autoptr(GMenu)     psec = g_menu_new ();
	guint                i;

	g_menu_remove_all (self->recent_menu);
	for (i = 0; levels != NULL && levels[i] != NULL; i++)
	{
		g_autoptr(GMenuItem) item = g_menu_item_new (levels[i], NULL);

		g_menu_item_set_action_and_target_value (item, "win.open-recent-level", g_variant_new_string (levels[i]));
		g_menu_append_item (lsec, item);
	}
	for (i = 0; projects != NULL && projects[i] != NULL; i++)
	{
		g_autoptr(GMenuItem) item = g_menu_item_new (projects[i], NULL);

		g_menu_item_set_action_and_target_value (item, "win.open-recent-project",
		                                         g_variant_new_string (projects[i]));
		g_menu_append_item (psec, item);
	}
	if (g_menu_model_get_n_items (G_MENU_MODEL (lsec)) > 0)
		g_menu_append_section (self->recent_menu, NULL, G_MENU_MODEL (lsec));
	if (g_menu_model_get_n_items (G_MENU_MODEL (psec)) > 0)
		g_menu_append_section (self->recent_menu, NULL, G_MENU_MODEL (psec));
	if (g_menu_model_get_n_items (G_MENU_MODEL (self->recent_menu)) == 0)
	{
		g_autoptr(GMenuItem) none = g_menu_item_new ("(none)", "win.no-recent");

		g_menu_append_item (self->recent_menu, none);
	}
}

static void
on_recent_changed (GObject    *settings,
                   GParamSpec *pspec,
                   gpointer    user_data)
{
	editor_window_rebuild_recent (LRG_EDITOR_WINDOW (user_data));
}

/*
 * build_menu_model:
 *
 * The same menus, in the same order and with the same contents, as the
 * 0.8.0 immediate-mode menu bar (File incl. Recent, Edit, Create, Node,
 * View incl. Panels, Play, Tools, Help). Titles carry mnemonics, so
 * Alt+F etc. open them from the keyboard.
 */
static void
build_menu_model (LrgEditorWindow *self)
{
	static const gchar *file_items[] = {
		"file.new-level", "file.open-level", "-", "file.save", "file.save-as", "file.reload", "-",
		"file.new-project", "file.open-project", "file.project-settings", "-",
		"file.import-scene", "-", "file.quit", NULL
	};
	static const gchar *edit_items[] = {
		"edit.undo", "edit.redo", "-", "edit.cut", "edit.copy", "edit.paste", "edit.duplicate",
		"edit.delete", "-", "edit.select-all", "edit.deselect", "edit.find", "-",
		"edit.preferences", NULL
	};
	static const gchar *node_items[] = {
		"node.rename", "node.toggle-visible", "node.toggle-lock", "node.group", "node.ungroup", "-",
		"node.save-prefab", "node.instantiate-prefab", "-", "node.attach-script",
		"node.open-scripts", NULL
	};
	static const gchar *view_items[] = {
		"view.focus", "view.frame-all", "-", "view.top", "view.front", "view.right",
		"view.perspective", "view.toggle-2d", "-", "view.grid", "view.icons", "view.wireframe",
		"view.stats", NULL
	};
	static const gchar *play_items[] = {
		"play.start", "play.pause", "play.step", "play.stop", "-", "run.start", "run.stop", NULL
	};
	static const gchar *tool_items[] = {
		"tool.select", "tool.move", "tool.rotate", "tool.scale", "-", "tool.toggle-space",
		"tool.toggle-snap", "-", "tools.validate", "tools.palette", "tools.quick-open",
		"tools.open-level-yaml", "tools.reveal-socket", "-", "tilemap.paint-mode", "tilemap.show-panel",
		"timeline.key", "timeline.play", "timeline.show-panel", "tools.rebuild-thumbnails", NULL
	};
	static const gchar *help_items[] = { "help.shortcuts", "help.docs", "help.about", NULL };
	LrgEditorActions *actions = self->app->actions;
	GMenu            *menus[G_N_ELEMENTS (menu_titles)];
	guint             i;
	guint             n_create;
	const EditorCreateItem *create = editor_create_items (&n_create);

	self->menu_model = g_menu_new ();
	for (i = 0; i < G_N_ELEMENTS (menu_titles); i++)
		menus[i] = g_menu_new ();

	/* File (with a Recent submenu after the actions) */
	append_items (menus[0], actions, file_items);
	{
		g_autoptr(GMenu) section = g_menu_new ();

		self->recent_menu = g_menu_new ();
		g_menu_append_submenu (section, "Recent", G_MENU_MODEL (self->recent_menu));
		g_menu_append_section (menus[0], NULL, G_MENU_MODEL (section));
		editor_window_rebuild_recent (self);
	}

	append_items (menus[1], actions, edit_items);

	/* Create: one item per node kind, then Instantiate Prefab */
	{
		g_autoptr(GMenu) section = g_menu_new ();

		for (i = 0; i < n_create; i++)
		{
			g_autoptr(GMenuItem) item = NULL;

			if (create[i].id[0] == '-')
			{
				g_menu_append_section (menus[2], NULL, G_MENU_MODEL (section));
				g_clear_object (&section);
				section = g_menu_new ();
				continue;
			}
			item = g_menu_item_new (create[i].label, NULL);
			g_menu_item_set_action_and_target_value (item, "win.create", g_variant_new_string (create[i].id));
			g_menu_append_item (section, item);
		}
		g_menu_append_section (menus[2], NULL, G_MENU_MODEL (section));
		g_clear_object (&section);
		section = g_menu_new ();
		g_menu_append (section, lrg_editor_actions_get_label (actions, "node.instantiate-prefab"),
		               "editor.node.instantiate-prefab");
		g_menu_append_section (menus[2], NULL, G_MENU_MODEL (section));
	}

	append_items (menus[3], actions, node_items);

	/* View (with Panels and Reset Layout at the end) */
	append_items (menus[4], actions, view_items);
	{
		g_autoptr(GMenu) section = g_menu_new ();
		g_autoptr(GMenu) panels = g_menu_new ();

		for (i = 0; i < G_N_ELEMENTS (panel_entries); i++)
		{
			g_autofree gchar *detailed = g_strconcat ("win.panel-", panel_entries[i][0], NULL);

			g_menu_append (panels, panel_entries[i][1], detailed);
		}
		g_menu_append_submenu (section, "Panels", G_MENU_MODEL (panels));
		g_menu_append (section, lrg_editor_actions_get_label (actions, "view.reset-layout"),
		               "editor.view.reset-layout");
		g_menu_append_section (menus[4], NULL, G_MENU_MODEL (section));
	}

	append_items (menus[5], actions, play_items);
	append_items (menus[6], actions, tool_items);
	append_items (menus[7], actions, help_items);

	for (i = 0; i < G_N_ELEMENTS (menu_titles); i++)
	{
		g_menu_append_submenu (self->menu_model, menu_titles[i], G_MENU_MODEL (menus[i]));
		g_object_unref (menus[i]);
	}
}

/* ==========================================================================
 * Toolbar and status bar
 * ========================================================================== */

static void
on_tool_clicked (LrgButton *button,
                 gpointer   user_data)
{
	LrgEditorWindow *self = LRG_EDITOR_WINDOW (user_data);
	const gchar     *id = g_object_get_data (G_OBJECT (button), "lrg-editor-action");

	if (id != NULL && lrg_editor_actions_is_enabled (self->app->actions, id))
		lrg_editor_actions_activate (self->app->actions, id);
}

static void
add_separator (LrgHBox *box)
{
	g_autoptr(LrgSeparator) sep = lrg_separator_new_with_orientation (LRG_ORIENTATION_VERTICAL);

	{
		const LrgInsets ins = { 4.0f, 2.0f, 4.0f, 2.0f };

		lrg_widget_set_margin (LRG_WIDGET (sep), &ins);
	}
	lrg_container_add_child (LRG_CONTAINER (box), LRG_WIDGET (sep));
}

static void
add_tool_buttons (LrgEditorWindow     *self,
                  const gchar * const  entries[][2],
                  gsize                n)
{
	gsize i;

	for (i = 0; i < n; i++)
	{
		g_autoptr(LrgButton) button = NULL;

		if (entries[i][1] == NULL)
		{
			add_separator (self->toolbar);
			continue;
		}
		button = lrg_button_new (entries[i][0]);
		g_object_set_data_full (G_OBJECT (button), "lrg-editor-action", g_strdup (entries[i][1]), g_free);
		g_signal_connect (button, "clicked", G_CALLBACK (on_tool_clicked), self);
		lrg_widget_add_class (LRG_WIDGET (button), "flat");
		lrg_container_add_child (LRG_CONTAINER (self->toolbar), LRG_WIDGET (button));
		g_hash_table_insert (self->tool_buttons, (gpointer) entries[i][1], button);
	}
}

/* A snap increment was edited in the toolbar. */
static void
on_snap_changed (LrgSpinBox *spin,
                 gdouble     value,
                 gpointer    user_data)
{
	LrgEditorWindow *self = LRG_EDITOR_WINDOW (user_data);
	gdouble          st, sr, ss;

	if (self->syncing)
		return;
	lrg_editor_get_snap (self->app->editor, &st, &sr, &ss);
	if (spin == self->snap_move)
		st = lrg_spin_box_get_value (spin);
	else if (spin == self->snap_rotate)
		sr = lrg_spin_box_get_value (spin) * G_PI / 180.0;
	else
		ss = lrg_spin_box_get_value (spin);
	lrg_editor_set_snap (self->app->editor, st, sr, ss);
}

static LrgSpinBox *
add_spin (LrgEditorWindow *self,
          gdouble          min,
          gdouble          max,
          gdouble          step,
          guint            digits,
          const gchar     *name)
{
	g_autoptr(LrgSpinBox) spin = lrg_spin_box_new_with_range (min, max, step);

	lrg_spin_box_set_digits (spin, digits);
	lrg_widget_set_name (LRG_WIDGET (spin), name);
	lrg_widget_set_min_size (LRG_WIDGET (spin), 84.0f, 0.0f);
	g_signal_connect (spin, "value-changed", G_CALLBACK (on_snap_changed), self);
	lrg_container_add_child (LRG_CONTAINER (self->toolbar), LRG_WIDGET (spin));
	return spin;
}

static void
on_layout_changed (LrgComboBox *combo,
                   gpointer     user_data)
{
	LrgEditorWindow *self = LRG_EDITOR_WINDOW (user_data);
	gint             i = lrg_combo_box_get_selected (combo);

	if (self->syncing || i < 0 || i >= (gint) G_N_ELEMENTS (layout_names))
		return;
	lrg_editor_view_state_set_layout_name (self->app->view, layout_names[i]);
}

static void
build_toolbar (LrgEditorWindow *self)
{
	g_autoptr(LrgLabel) label = NULL;
	gsize               i;

	self->toolbar = lrg_hbox_new ();
	lrg_widget_set_name (LRG_WIDGET (self->toolbar), "toolbar");
	lrg_container_set_spacing (LRG_CONTAINER (self->toolbar), 4.0f);
	{
		const LrgInsets ins = { 6.0f, 3.0f, 6.0f, 3.0f };

		lrg_widget_set_margin (LRG_WIDGET (self->toolbar), &ins);
	}

	add_tool_buttons (self, tool_entries, G_N_ELEMENTS (tool_entries));

	/* Snap increments, editable in place */
	self->snap_move = add_spin (self, 0.0, 1000.0, 0.25, 2, "snap-move");
	self->snap_rotate = add_spin (self, 0.0, 360.0, 5.0, 0, "snap-rotate");
	self->snap_scale = add_spin (self, 0.0, 100.0, 0.05, 2, "snap-scale");

	add_separator (self->toolbar);
	add_tool_buttons (self, play_entries, G_N_ELEMENTS (play_entries));
	add_separator (self->toolbar);

	/* Viewport layout picker */
	label = lrg_label_new ("Layout");
	lrg_container_add_child (LRG_CONTAINER (self->toolbar), LRG_WIDGET (label));
	self->layout_combo = lrg_combo_box_new ();
	for (i = 0; i < G_N_ELEMENTS (layout_names); i++)
		lrg_combo_box_append (self->layout_combo, layout_names[i]);
	lrg_widget_set_name (LRG_WIDGET (self->layout_combo), "viewport-layout");
	lrg_widget_set_min_size (LRG_WIDGET (self->layout_combo), 96.0f, 0.0f);
	g_signal_connect (self->layout_combo, "changed", G_CALLBACK (on_layout_changed), self);
	lrg_container_add_child (LRG_CONTAINER (self->toolbar), LRG_WIDGET (self->layout_combo));
	g_object_unref (self->layout_combo);
}

static void
on_reload_clicked (LrgButton *button,
                   gpointer   user_data)
{
	LrgEditorWindow *self = LRG_EDITOR_WINDOW (user_data);

	editor_app_perform_pending (self->app, EDITOR_PENDING_RELOAD, NULL);
}

static void
on_keep_clicked (LrgButton *button,
                 gpointer   user_data)
{
	LRG_EDITOR_WINDOW (user_data)->app->level_changed_on_disk = FALSE;
}

static void
on_counters_clicked (LrgButton *button,
                     gpointer   user_data)
{
	editor_app_show_panel (LRG_EDITOR_WINDOW (user_data)->app, "console");
}

static LrgLabel *
add_label (LrgHBox     *box,
           const gchar *name)
{
	g_autoptr(LrgLabel) label = lrg_label_new ("");

	lrg_widget_set_name (LRG_WIDGET (label), name);
	lrg_widget_set_valign (LRG_WIDGET (label), LRG_ALIGN_CENTER);
	lrg_container_add_child (LRG_CONTAINER (box), LRG_WIDGET (label));
	return label;
}

static LrgButton *
add_status_button (LrgEditorWindow *self,
                   const gchar     *text,
                   const gchar     *name,
                   GCallback        callback)
{
	g_autoptr(LrgButton) button = lrg_button_new (text);

	lrg_widget_set_name (LRG_WIDGET (button), name);
	lrg_widget_add_class (LRG_WIDGET (button), "flat");
	g_signal_connect (button, "clicked", callback, self);
	lrg_container_add_child (LRG_CONTAINER (self->statusbar), LRG_WIDGET (button));
	return button;
}

static void
build_statusbar (LrgEditorWindow *self)
{
	g_autoptr(LrgLabel) spacer = NULL;

	self->statusbar = lrg_hbox_new ();
	lrg_widget_set_name (LRG_WIDGET (self->statusbar), "statusbar");
	lrg_container_set_spacing (LRG_CONTAINER (self->statusbar), 10.0f);
	{
		const LrgInsets ins = { 8.0f, 2.0f, 8.0f, 2.0f };

		lrg_widget_set_margin (LRG_WIDGET (self->statusbar), &ins);
	}

	self->doc_label = add_label (self->statusbar, "status-document");
	self->play_label = add_label (self->statusbar, "status-play");
	self->disk_label = add_label (self->statusbar, "status-disk");
	lrg_label_set_text (self->disk_label, "changed on disk:");
	self->reload_button = add_status_button (self, "Reload", "status-reload", G_CALLBACK (on_reload_clicked));
	self->keep_button = add_status_button (self, "Keep mine", "status-keep", G_CALLBACK (on_keep_clicked));
	self->status_label = add_label (self->statusbar, "status-message");

	/* Push the counters to the right edge */
	spacer = lrg_label_new ("");
	lrg_widget_set_fill_weight (LRG_WIDGET (spacer), 1.0f);
	lrg_container_add_child (LRG_CONTAINER (self->statusbar), LRG_WIDGET (spacer));

	self->counters = add_status_button (self, "", "status-counters", G_CALLBACK (on_counters_clicked));
}

/* ==========================================================================
 * Shortcuts and per-frame refresh
 * ========================================================================== */

/*
 * editor_window_sync_shortcuts:
 *
 * (Re)registers every accelerator of every registry action as a canvas
 * shortcut on "editor.<id>". Cheap enough to redo whenever a binding
 * changes; the canvas keeps no other shortcuts of ours.
 */
void
editor_window_sync_shortcuts (LrgEditorWindow *self)
{
	LrgCanvas *canvas = self->app->canvas;
	GList     *ids;
	GList     *l;

	if (canvas == NULL)
		return;
	g_set_weak_pointer (&self->shortcut_canvas, canvas);
	lrg_canvas_remove_all_shortcuts (canvas);

	ids = lrg_editor_actions_list (self->app->actions);
	for (l = ids; l != NULL; l = l->next)
	{
		g_auto(GStrv)     accels = lrg_editor_actions_get_accels (self->app->actions, l->data);
		g_autofree gchar *action = g_strconcat ("editor.", (const gchar *) l->data, NULL);
		guint             i;

		for (i = 0; accels != NULL && accels[i] != NULL; i++)
			if (accels[i][0] != '\0')
				lrg_canvas_add_shortcut (canvas, accels[i], action, NULL);
	}
	g_list_free (ids);
	self->shortcuts_dirty = FALSE;
}

/*
 * editor_window_set_menus_enabled:
 *
 * A modal dialog opened (@enabled FALSE) or closed: the "editor" group
 * leaves the canvas so shortcuts stop resolving behind the dialog, and
 * the menu bar closes.
 */
void
editor_window_set_menus_enabled (LrgEditorWindow *self,
                                 gboolean         enabled)
{
	LrgCanvas *canvas = self->app->canvas;

	if (canvas == NULL || enabled == self->editor_on_canvas)
		return;
	self->editor_on_canvas = enabled;
	lrg_widget_insert_action_group (LRG_WIDGET (canvas), "editor",
	                                enabled ? G_ACTION_GROUP (self->editor_group) : NULL);
	if (!enabled)
		lrg_menu_bar_close (self->menu_bar);
}

GActionGroup *
editor_window_get_editor_group (LrgEditorWindow *self)
{
	return G_ACTION_GROUP (self->editor_group);
}

static void
set_checked (LrgWidget *widget,
             gboolean   checked)
{
	if (checked)
		lrg_widget_set_state_flags (widget, LRG_WIDGET_STATE_CHECKED);
	else
		lrg_widget_unset_state_flags (widget, LRG_WIDGET_STATE_CHECKED);
}

static void
set_label (LrgLabel    *label,
           const gchar *text)
{
	if (g_strcmp0 (lrg_label_get_text (label), text) != 0)
		lrg_label_set_text (label, text);
}

/* Spin boxes follow the editor unless the user is editing them. */
static void
sync_spin (LrgEditorWindow *self,
           LrgSpinBox      *spin,
           gdouble          value)
{
	LrgWidget *focus = (self->app->canvas != NULL) ? lrg_canvas_get_focused_widget (self->app->canvas) : NULL;

	if (focus != NULL && (focus == LRG_WIDGET (spin) || lrg_widget_is_ancestor (focus, LRG_WIDGET (spin))))
		return;
	if (ABS (lrg_spin_box_get_value (spin) - value) > 1e-6)
		lrg_spin_box_set_value (spin, value);
}

/*
 * editor_window_refresh:
 *
 * Once per frame: polls the registry's enabled / checked callbacks
 * (which updates the mirror, hence menus), re-registers shortcuts when a
 * binding changed, and copies the model into the toolbar and status bar.
 */
void
editor_window_refresh (LrgEditorWindow *self)
{
	LrgEditorApp     *app = self->app;
	GHashTableIter    iter;
	gpointer          key, value;
	gdouble           st, sr, ss;
	const gchar      *path = lrg_editor_get_path (app->editor);
	g_autofree gchar *base = NULL;
	g_autofree gchar *doc = NULL;
	g_autofree gchar *counts = NULL;
	guint             warnings, errors, clients, i;
	const gchar      *socket = lrg_editor_server_get_socket_path (app->server);
	gboolean          playing = lrg_editor_play_session_is_playing (app->play);

	lrg_editor_actions_refresh_state (app->actions);
	if (self->shortcuts_dirty || (app->canvas != NULL && self->shortcut_canvas != app->canvas))
		editor_window_sync_shortcuts (self);

	/* Panel toggles show whether each panel is docked */
	for (i = 0; i < G_N_ELEMENTS (panel_entries); i++)
	{
		g_autofree gchar *name = g_strconcat ("panel-", panel_entries[i][0], NULL);
		GAction          *action = g_action_map_lookup_action (G_ACTION_MAP (self->win_group), name);
		gboolean          shown = editor_app_panel_is_shown (app, panel_entries[i][0]);
		g_autoptr(GVariant) state = g_action_get_state (action);

		if (g_variant_get_boolean (state) != shown)
			g_simple_action_set_state (G_SIMPLE_ACTION (action), g_variant_new_boolean (shown));
		g_simple_action_set_enabled (G_SIMPLE_ACTION (action),
		                             g_hash_table_contains (app->panels, panel_entries[i][0]));
	}

	/* Toolbar: checked / enabled from the actions, labels that flip */
	self->syncing = TRUE;
	g_hash_table_iter_init (&iter, self->tool_buttons);
	while (g_hash_table_iter_next (&iter, &key, &value))
	{
		set_checked (LRG_WIDGET (value), lrg_editor_actions_is_checked (app->actions, key));
		lrg_widget_set_enabled (LRG_WIDGET (value), lrg_editor_actions_is_enabled (app->actions, key));
	}
	{
		LrgButton *space = g_hash_table_lookup (self->tool_buttons, "tool.toggle-space");
		LrgButton *dim = g_hash_table_lookup (self->tool_buttons, "view.toggle-2d");
		LrgButton *play = g_hash_table_lookup (self->tool_buttons, "play.start");
		LrgButton *run = g_hash_table_lookup (self->tool_buttons, "run.start");
		const gchar *t;

		t = lrg_editor_get_space (app->editor) == LRG_EDITOR_SPACE_LOCAL ? "Local" : "World";
		if (g_strcmp0 (lrg_button_get_text (space), t) != 0)
			lrg_button_set_text (space, t);
		t = lrg_editor_view_state_get_camera (app->view, -1)->view_2d ? "2D" : "3D";
		if (g_strcmp0 (lrg_button_get_text (dim), t) != 0)
			lrg_button_set_text (dim, t);
		t = playing ? "Playing" : "Play";
		if (g_strcmp0 (lrg_button_get_text (play), t) != 0)
			lrg_button_set_text (play, t);
		t = app->run_proc != NULL ? "Running" : "Run Game";
		if (g_strcmp0 (lrg_button_get_text (run), t) != 0)
			lrg_button_set_text (run, t);
	}
	lrg_editor_get_snap (app->editor, &st, &sr, &ss);
	sync_spin (self, self->snap_move, st);
	sync_spin (self, self->snap_rotate, sr * 180.0 / G_PI);
	sync_spin (self, self->snap_scale, ss);
	{
		const gchar *layout = lrg_editor_view_state_get_layout_name (app->view);

		for (i = 0; i < G_N_ELEMENTS (layout_names); i++)
			if (g_strcmp0 (layout, layout_names[i]) == 0 && lrg_combo_box_get_selected (self->layout_combo) != (gint) i)
				lrg_combo_box_set_selected (self->layout_combo, (gint) i);
	}
	self->syncing = FALSE;

	/* Status bar: document, play state, disk prompt, message, counters */
	base = (path != NULL) ? g_path_get_basename (path) : g_strdup ("untitled");
	doc = g_strdup_printf ("%s%s", base, lrg_editor_is_dirty (app->editor) ? " *" : "");
	set_label (self->doc_label, doc);
	set_label (self->play_label, playing ? (lrg_editor_play_session_is_paused (app->play) ? "PAUSED" : "PLAYING")
	                                     : "");
	lrg_widget_set_visible (LRG_WIDGET (self->play_label), playing);
	lrg_widget_set_visible (LRG_WIDGET (self->disk_label), app->level_changed_on_disk);
	lrg_widget_set_visible (LRG_WIDGET (self->reload_button), app->level_changed_on_disk);
	lrg_widget_set_visible (LRG_WIDGET (self->keep_button), app->level_changed_on_disk);
	set_label (self->status_label, lrg_editor_view_state_get_status (app->view) != NULL
	                               ? lrg_editor_view_state_get_status (app->view) : "");

	warnings = lrg_editor_log_count_level (app->log, G_LOG_LEVEL_WARNING);
	errors = lrg_editor_log_count_level (app->log, G_LOG_LEVEL_ERROR | G_LOG_LEVEL_CRITICAL);
	clients = lrg_editor_server_get_n_clients (app->server);
	counts = g_strdup_printf ("%u selected   %u nodes   %u errors  %u warnings   %s%s   %.0f fps",
	                          lrg_editor_selection_get_count (lrg_editor_get_selection (app->editor)),
	                          editor_app_count_nodes (app), errors, warnings,
	                          socket != NULL ? "IPC " : "",
	                          socket != NULL ? (lrg_editor_server_has_capability (app->server, "open-file")
	                                            ? "emacs" : (clients > 0 ? "client" : "idle")) : "",
	                          app->fps);
	if (g_strcmp0 (lrg_button_get_text (self->counters), counts) != 0)
		lrg_button_set_text (self->counters, counts);
}

/* Shows a notification. @level: EDITOR_NOTIFY_*. */
void
editor_window_toast (LrgEditorWindow *self,
                     gint             level,
                     const gchar     *text)
{
	static const LrgToastKind kinds[] = {
		LRG_TOAST_KIND_INFO, LRG_TOAST_KIND_SUCCESS, LRG_TOAST_KIND_WARNING, LRG_TOAST_KIND_DANGER
	};
	g_autoptr(LrgToast) toast = lrg_toast_new (NULL, text);

	lrg_toast_set_kind (toast, kinds[CLAMP (level, 0, 3)]);
	lrg_toast_set_duration (toast, level >= EDITOR_NOTIFY_ERROR ? 7.0 : 4.0);
	lrg_toast_queue_show (self->toasts, toast);
}

/* ==========================================================================
 * Layout and painting
 * ========================================================================== */

/* Menu bar, toolbar, dock, status bar from top to bottom. */
static void
lrg_editor_window_layout_children (LrgContainer *container)
{
	LrgEditorWindow *self = LRG_EDITOR_WINDOW (container);
	gfloat           w = lrg_widget_get_width (LRG_WIDGET (self));
	gfloat           h = lrg_widget_get_height (LRG_WIDGET (self));
	gfloat           mw, mh, tw, th, sw, sh;
	gfloat           top;
	LrgInsets        m;

	/* Children are added one by one during construction */
	if (self->menu_bar == NULL || self->toolbar == NULL || self->dock == NULL || self->statusbar == NULL)
		return;

	lrg_widget_get_preferred_size (LRG_WIDGET (self->menu_bar), &mw, &mh);
	lrg_widget_get_preferred_size (LRG_WIDGET (self->toolbar), &tw, &th);
	lrg_widget_get_preferred_size (LRG_WIDGET (self->statusbar), &sw, &sh);

	lrg_widget_place_in_slot (LRG_WIDGET (self->menu_bar), 0.0f, 0.0f, w, mh);
	top = mh;

	lrg_widget_get_margin (LRG_WIDGET (self->toolbar), &m);
	lrg_widget_place_in_slot (LRG_WIDGET (self->toolbar), m.left, top + m.top, MAX (0.0f, w - m.left - m.right), th);
	top += th + m.top + m.bottom;

	lrg_widget_get_margin (LRG_WIDGET (self->statusbar), &m);
	lrg_widget_place_in_slot (LRG_WIDGET (self->statusbar), m.left, h - sh - m.bottom,
	                          MAX (0.0f, w - m.left - m.right), sh);

	lrg_widget_place_in_slot (LRG_WIDGET (self->dock), 0.0f, top, w,
	                          MAX (50.0f, h - top - sh - m.top - m.bottom));
}

/* Bar backgrounds behind the toolbar and status bar, then the children. */
static void
lrg_editor_window_paint (LrgWidget    *widget,
                         LrgPaintList *list)
{
	LrgEditorWindow *self = LRG_EDITOR_WINDOW (widget);
	LrgTheme        *theme = lrg_widget_get_theme (widget);
	gfloat           x = lrg_widget_get_world_x (widget);
	gfloat           y = lrg_widget_get_world_y (widget);
	gfloat           w = lrg_widget_get_width (widget);
	gfloat           dy = (self->dock != NULL) ? lrg_widget_get_world_y (LRG_WIDGET (self->dock)) : 0.0f;
	gfloat           sy;

	if (self->dock == NULL || self->statusbar == NULL)
		return;
	sy = lrg_widget_get_world_y (LRG_WIDGET (self->statusbar));

	lrg_paint_list_rect (list, x, y, w, dy - y, 0.0f, lrg_theme_get_color (theme, LRG_THEME_COLOR_PANEL));
	lrg_paint_list_rect (list, x, sy - 2.0f, w, y + lrg_widget_get_height (widget) - sy + 2.0f, 0.0f,
	                     lrg_theme_get_color (theme, LRG_THEME_COLOR_PANEL));
	lrg_paint_list_rect (list, x, sy - 3.0f, w, 1.0f, 0.0f, lrg_theme_get_color (theme, LRG_THEME_COLOR_BORDER));

	LRG_WIDGET_CLASS (lrg_editor_window_parent_class)->paint (widget, list);
}

/* ==========================================================================
 * GObject
 * ========================================================================== */

static void
lrg_editor_window_dispose (GObject *object)
{
	LrgEditorWindow *self = LRG_EDITOR_WINDOW (object);

	if (self->app != NULL)
	{
		g_signal_handlers_disconnect_by_data (self->app->actions, self);
		g_signal_handlers_disconnect_by_data (self->app->settings, self);
		self->app = NULL;
	}
	g_clear_weak_pointer (&self->shortcut_canvas);
	g_clear_object (&self->toasts);
	g_clear_object (&self->editor_group);
	g_clear_object (&self->win_group);
	g_clear_object (&self->recent_menu);
	g_clear_object (&self->menu_model);
	g_clear_pointer (&self->tool_buttons, g_hash_table_unref);

	G_OBJECT_CLASS (lrg_editor_window_parent_class)->dispose (object);
}

static void
lrg_editor_window_class_init (LrgEditorWindowClass *klass)
{
	GObjectClass      *object_class = G_OBJECT_CLASS (klass);
	LrgWidgetClass    *widget_class = LRG_WIDGET_CLASS (klass);
	LrgContainerClass *container_class = LRG_CONTAINER_CLASS (klass);

	object_class->dispose = lrg_editor_window_dispose;
	widget_class->paint = lrg_editor_window_paint;
	container_class->layout_children = lrg_editor_window_layout_children;
}

static void
lrg_editor_window_init (LrgEditorWindow *self)
{
	self->tool_buttons = g_hash_table_new (g_str_hash, g_str_equal);
	self->editor_group = g_simple_action_group_new ();
	self->win_group = g_simple_action_group_new ();
}

LrgEditorWindow *
lrg_editor_window_new (LrgEditorApp *app)
{
	LrgEditorWindow *self;
	gchar          **ids;
	guint            i;

	g_return_val_if_fail (LRG_IS_EDITOR_APP (app), NULL);

	self = g_object_new (LRG_TYPE_EDITOR_WINDOW, NULL);
	self->app = app;
	lrg_widget_set_name (LRG_WIDGET (self), "editor-window");

	/* Actions: mirror the registry, then the window's own group */
	ids = g_action_group_list_actions (G_ACTION_GROUP (app->actions));
	for (i = 0; ids[i] != NULL; i++)
		mirror_add (self, ids[i]);
	g_strfreev (ids);
	g_signal_connect (app->actions, "action-added", G_CALLBACK (on_registry_added), self);
	g_signal_connect (app->actions, "action-removed", G_CALLBACK (on_registry_removed), self);
	g_signal_connect (app->actions, "action-enabled-changed", G_CALLBACK (on_registry_enabled), self);
	g_signal_connect (app->actions, "action-state-changed", G_CALLBACK (on_registry_state), self);
	g_signal_connect (app->actions, "accels-changed", G_CALLBACK (on_accels_changed), self);
	add_win_actions (self);
	lrg_widget_insert_action_group (LRG_WIDGET (self), "editor", G_ACTION_GROUP (self->editor_group));
	lrg_widget_insert_action_group (LRG_WIDGET (self), "win", G_ACTION_GROUP (self->win_group));
	self->shortcuts_dirty = TRUE;

	/* Menu bar */
	build_menu_model (self);
	g_signal_connect (app->settings, "notify::recent-levels", G_CALLBACK (on_recent_changed), self);
	g_signal_connect (app->settings, "notify::recent-projects", G_CALLBACK (on_recent_changed), self);
	self->menu_bar = lrg_menu_bar_new_from_model (G_MENU_MODEL (self->menu_model));
	lrg_widget_set_name (LRG_WIDGET (self->menu_bar), "menubar");
	lrg_container_add_child (LRG_CONTAINER (self), LRG_WIDGET (self->menu_bar));
	g_object_unref (self->menu_bar);

	build_toolbar (self);
	lrg_container_add_child (LRG_CONTAINER (self), LRG_WIDGET (self->toolbar));
	g_object_unref (self->toolbar);

	self->dock = lrg_dock_area_new ();
	lrg_widget_set_name (LRG_WIDGET (self->dock), "dock");
	lrg_container_add_child (LRG_CONTAINER (self), LRG_WIDGET (self->dock));
	g_object_unref (self->dock);

	build_statusbar (self);
	lrg_container_add_child (LRG_CONTAINER (self), LRG_WIDGET (self->statusbar));
	g_object_unref (self->statusbar);

	/* Toasts float above everything (the app adds them as an overlay) */
	self->toasts = lrg_toast_queue_new ();
	lrg_toast_queue_set_max_visible (self->toasts, 5);

	return self;
}

LrgEditorApp *
lrg_editor_window_get_app (LrgEditorWindow *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_WINDOW (self), NULL);
	return self->app;
}

LrgMenuBar *
lrg_editor_window_get_menu_bar (LrgEditorWindow *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_WINDOW (self), NULL);
	return self->menu_bar;
}

GMenuModel *
lrg_editor_window_get_menu_model (LrgEditorWindow *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_WINDOW (self), NULL);
	return G_MENU_MODEL (self->menu_model);
}

LrgDockArea *
lrg_editor_window_get_dock (LrgEditorWindow *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_WINDOW (self), NULL);
	return self->dock;
}

LrgToastQueue *
lrg_editor_window_get_toasts (LrgEditorWindow *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_WINDOW (self), NULL);
	return self->toasts;
}

LrgWidget *
lrg_editor_window_get_widget (LrgEditorWindow *self,
                              const gchar     *name)
{
	g_return_val_if_fail (LRG_IS_EDITOR_WINDOW (self), NULL);
	g_return_val_if_fail (name != NULL, NULL);

	if (g_str_has_prefix (name, "tool:"))
		return g_hash_table_lookup (self->tool_buttons, name + 5);
	if (g_strcmp0 (name, "toolbar") == 0)
		return LRG_WIDGET (self->toolbar);
	if (g_strcmp0 (name, "statusbar") == 0)
		return LRG_WIDGET (self->statusbar);
	if (g_strcmp0 (name, "status-reload") == 0)
		return LRG_WIDGET (self->reload_button);
	if (g_strcmp0 (name, "status-keep") == 0)
		return LRG_WIDGET (self->keep_button);
	if (g_strcmp0 (name, "status-counters") == 0)
		return LRG_WIDGET (self->counters);
	if (g_strcmp0 (name, "snap-move") == 0)
		return LRG_WIDGET (self->snap_move);
	if (g_strcmp0 (name, "snap-rotate") == 0)
		return LRG_WIDGET (self->snap_rotate);
	if (g_strcmp0 (name, "snap-scale") == 0)
		return LRG_WIDGET (self->snap_scale);
	if (g_strcmp0 (name, "viewport-layout") == 0)
		return LRG_WIDGET (self->layout_combo);
	return NULL;
}
