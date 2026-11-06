/* lrg-editor-viewport-split.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgEditorViewportSplit: one to four viewport panels in nested
 * splitters, following LrgEditorViewState:layout.
 *
 * The split owns all four panels and three splitters for the whole of
 * its life and only re-parents them on a layout change, so cameras,
 * render modes, overlays and splitter positions survive switching
 * layouts. Its single child ("root") is either one panel or a splitter:
 *
 *   single   panel 0 (or the maximised panel)
 *   split-h  h_top:  panel 0 | panel 1
 *   split-v  v_root: panel 0 / panel 1
 *   quad     v_root: (h_top: panel 0 | panel 1) / (h_bottom: panel 2 | panel 3)
 *
 * The file also holds the viewport IPC methods (view.layout,
 * view.render_mode, view.overlay): they act on the view state only, so
 * they work headless and with any number of splits.
 */

#include "config.h"

#include "lrg-editor-app-private.h"
#include "lrg-editor-viewport-split.h"
#include "../../ui/lrg-splitter.h"
#include <json-glib/json-glib.h>

struct _LrgEditorViewportSplit
{
	LrgContainer            parent_instance;

	LrgEditorApp           *app;          /* weak */
	LrgEditorViewState     *view;         /* held: its notify::layout */
	LrgEditorViewportPanel *panels[LRG_EDITOR_VIEW_MAX_VIEWPORTS];
	LrgSplitter            *v_root;
	LrgSplitter            *h_top;
	LrgSplitter            *h_bottom;
	LrgWidget              *root;         /* the current child, not owned */
	gint                    maximized;
	LrgEditorViewportLayout shown;        /* what the tree shows now */
	gint                    shown_max;
	gboolean                built;
};

G_DEFINE_FINAL_TYPE (LrgEditorViewportSplit, lrg_editor_viewport_split, LRG_TYPE_CONTAINER)

/* ==========================================================================
 * Layout
 * ========================================================================== */

/* Takes @w out of whatever holds it (a splitter pane or the split). */
static void
detach (LrgEditorViewportSplit *self,
        LrgWidget              *w)
{
	LrgWidget *parent = LRG_WIDGET (lrg_widget_get_parent (w));

	if (parent == NULL)
		return;
	if (LRG_IS_SPLITTER (parent))
	{
		LrgSplitter *s = LRG_SPLITTER (parent);

		if (lrg_splitter_get_start_child (s) == w)
			lrg_splitter_set_start_child (s, NULL);
		else if (lrg_splitter_get_end_child (s) == w)
			lrg_splitter_set_end_child (s, NULL);
	}
	else if (LRG_IS_CONTAINER (parent))
		lrg_container_remove_child (LRG_CONTAINER (parent), w);
}

/* Rebuilds the widget tree for the view state's layout (or the
 * maximised panel). Every panel and splitter is detached first. */
static void
rebuild (LrgEditorViewportSplit *self)
{
	LrgEditorViewportLayout layout;
	LrgWidget              *root;
	guint                   i;

	if (self->app == NULL)
		return;
	layout = lrg_editor_view_state_get_layout (self->app->view);
	if (self->built && layout == self->shown && self->maximized == self->shown_max)
		return;

	/* 1. Take everything apart */
	if (self->root != NULL)
		lrg_container_remove_child (LRG_CONTAINER (self), self->root);
	self->root = NULL;
	for (i = 0; i < LRG_EDITOR_VIEW_MAX_VIEWPORTS; i++)
		detach (self, LRG_WIDGET (self->panels[i]));
	detach (self, LRG_WIDGET (self->h_top));
	detach (self, LRG_WIDGET (self->h_bottom));

	/* 2. Put the layout together */
	if (self->maximized >= 0)
		root = LRG_WIDGET (self->panels[self->maximized]);
	else
	{
		switch (layout)
		{
		case LRG_EDITOR_VIEWPORT_LAYOUT_SPLIT_H:
			lrg_splitter_set_start_child (self->h_top, LRG_WIDGET (self->panels[0]));
			lrg_splitter_set_end_child (self->h_top, LRG_WIDGET (self->panels[1]));
			root = LRG_WIDGET (self->h_top);
			break;
		case LRG_EDITOR_VIEWPORT_LAYOUT_SPLIT_V:
			lrg_splitter_set_start_child (self->v_root, LRG_WIDGET (self->panels[0]));
			lrg_splitter_set_end_child (self->v_root, LRG_WIDGET (self->panels[1]));
			root = LRG_WIDGET (self->v_root);
			break;
		case LRG_EDITOR_VIEWPORT_LAYOUT_QUAD:
			lrg_splitter_set_start_child (self->h_top, LRG_WIDGET (self->panels[0]));
			lrg_splitter_set_end_child (self->h_top, LRG_WIDGET (self->panels[1]));
			lrg_splitter_set_start_child (self->h_bottom, LRG_WIDGET (self->panels[2]));
			lrg_splitter_set_end_child (self->h_bottom, LRG_WIDGET (self->panels[3]));
			lrg_splitter_set_start_child (self->v_root, LRG_WIDGET (self->h_top));
			lrg_splitter_set_end_child (self->v_root, LRG_WIDGET (self->h_bottom));
			root = LRG_WIDGET (self->v_root);
			break;
		case LRG_EDITOR_VIEWPORT_LAYOUT_SINGLE:
		default:
			root = LRG_WIDGET (self->panels[0]);
			break;
		}
	}
	detach (self, root);
	lrg_container_add_child (LRG_CONTAINER (self), root);
	self->root = root;
	self->shown = layout;
	self->shown_max = self->maximized;
	self->built = TRUE;
	lrg_widget_queue_layout (LRG_WIDGET (self));
}

static void
on_layout_notify (GObject    *object,
                  GParamSpec *pspec,
                  gpointer    user_data)
{
	LrgEditorViewportSplit *self = LRG_EDITOR_VIEWPORT_SPLIT (user_data);

	/* A layout change ends a temporary maximise */
	self->maximized = -1;
	rebuild (self);
}

static void
lrg_editor_viewport_split_layout_children (LrgContainer *container)
{
	LrgEditorViewportSplit *self = LRG_EDITOR_VIEWPORT_SPLIT (container);

	if (self->root != NULL)
		lrg_widget_place_in_slot (self->root, 0.0f, 0.0f, lrg_widget_get_width (LRG_WIDGET (self)),
		                          lrg_widget_get_height (LRG_WIDGET (self)));
}

/* ==========================================================================
 * IPC: view.layout, view.render_mode, view.overlay
 * ========================================================================== */

static JsonNode *
obj_node (JsonObject *o)
{
	JsonNode *n = json_node_new (JSON_NODE_OBJECT);

	json_node_take_object (n, o);
	return n;
}

static const gchar *
param_string (JsonObject  *params,
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

/* The "viewport" parameter: omitted or -1 is the active viewport. */
static gboolean
param_viewport (LrgEditorApp  *app,
                JsonObject    *params,
                gint          *index,
                GError       **error)
{
	JsonNode *n;
	gint64    v;

	*index = (gint) lrg_editor_view_state_get_active_viewport (app->view);
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
	if (v >= 0)
		*index = (gint) v;
	return TRUE;
}

/* {"layout": "quad", "viewports": 4, "active_viewport": 0} */
static JsonNode *
m_view_layout (LrgEditorServer  *server,
               JsonObject       *params,
               gpointer          user_data,
               GError          **error)
{
	LrgEditorApp *app = LRG_EDITOR_APP (user_data);
	const gchar  *layout = param_string (params, "layout");
	JsonObject   *o;

	if (layout != NULL && !lrg_editor_view_state_set_layout_name (app->view, layout))
	{
		g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS,
		             "Unknown layout '%s' (single, split-h, split-v, quad)", layout);
		return NULL;
	}
	o = json_object_new ();
	json_object_set_string_member (o, "layout", lrg_editor_view_state_get_layout_name (app->view));
	json_object_set_int_member (o, "viewports", lrg_editor_view_state_get_viewport_count (app->view));
	json_object_set_int_member (o, "active_viewport", lrg_editor_view_state_get_active_viewport (app->view));
	return obj_node (o);
}

/* {"viewport": 1, "mode": "wireframe"} */
static JsonNode *
m_view_render_mode (LrgEditorServer  *server,
                    JsonObject       *params,
                    gpointer          user_data,
                    GError          **error)
{
	LrgEditorApp          *app = LRG_EDITOR_APP (user_data);
	const gchar           *mode = param_string (params, "mode");
	g_autoptr(GEnumClass)  modes = g_type_class_ref (LRG_TYPE_EDITOR_RENDER_MODE);
	GEnumValue            *value;
	JsonObject            *o;
	gint                   index;

	if (!param_viewport (app, params, &index, error))
		return NULL;
	if (mode != NULL)
	{
		value = g_enum_get_value_by_nick (modes, mode);
		if (value == NULL)
		{
			g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS,
			             "Unknown render mode '%s' (lit, unlit, wireframe, normals, lighting-only)", mode);
			return NULL;
		}
		lrg_editor_view_state_set_render_mode (app->view, index, (LrgEditorRenderMode) value->value);
	}
	value = g_enum_get_value (modes, lrg_editor_view_state_get_render_mode (app->view, index));
	o = json_object_new ();
	json_object_set_int_member (o, "viewport", index);
	json_object_set_string_member (o, "mode", value != NULL ? value->value_nick : "lit");
	return obj_node (o);
}

/* {"viewport": 0, "name": "shadows", "enabled": false} -> every flag */
static JsonNode *
m_view_overlay (LrgEditorServer  *server,
                JsonObject       *params,
                gpointer          user_data,
                GError          **error)
{
	LrgEditorApp           *app = LRG_EDITOR_APP (user_data);
	const gchar            *name = param_string (params, "name");
	g_autoptr(GFlagsClass)  flags = g_type_class_ref (LRG_TYPE_EDITOR_VIEW_OVERLAYS);
	LrgEditorViewOverlays   overlays;
	JsonObject             *o;
	JsonObject             *map;
	gint                    index;
	guint                   i;

	if (!param_viewport (app, params, &index, error))
		return NULL;
	if (name != NULL)
	{
		GFlagsValue *flag = g_flags_get_value_by_nick (flags, name);

		if (flag == NULL || flag->value == 0)
		{
			g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS,
			             "Unknown overlay '%s'", name);
			return NULL;
		}
		overlays = lrg_editor_view_state_get_overlays (app->view, index);
		if (params != NULL && json_object_has_member (params, "enabled"))
		{
			if (json_object_get_boolean_member (params, "enabled"))
				overlays |= (LrgEditorViewOverlays) flag->value;
			else
				overlays &= ~(LrgEditorViewOverlays) flag->value;
			lrg_editor_view_state_set_overlays (app->view, index, overlays);
		}
		else
			lrg_editor_view_state_toggle_overlay (app->view, index, (LrgEditorViewOverlays) flag->value);
	}

	overlays = lrg_editor_view_state_get_overlays (app->view, index);
	o = json_object_new ();
	map = json_object_new ();
	json_object_set_int_member (o, "viewport", index);
	for (i = 0; i < flags->n_values; i++)
		if (flags->values[i].value != 0)
			json_object_set_boolean_member (map, flags->values[i].value_nick,
			                                (overlays & flags->values[i].value) != 0);
	json_object_set_object_member (o, "overlays", map);
	return obj_node (o);
}

void
lrg_editor_viewport_split_register_methods (LrgEditorApp *app)
{
	g_return_if_fail (LRG_IS_EDITOR_APP (app));

	if (app->server == NULL)
		return;
	lrg_editor_server_register_method (app->server, "view.layout", m_view_layout, app, NULL);
	lrg_editor_server_register_method (app->server, "view.render_mode", m_view_render_mode, app, NULL);
	lrg_editor_server_register_method (app->server, "view.overlay", m_view_overlay, app, NULL);
}

/* ==========================================================================
 * GObject
 * ========================================================================== */

static void
lrg_editor_viewport_split_dispose (GObject *object)
{
	LrgEditorViewportSplit *self = LRG_EDITOR_VIEWPORT_SPLIT (object);
	guint                   i;

	if (self->view != NULL)
		g_signal_handlers_disconnect_by_data (self->view, self);
	g_clear_object (&self->view);
	if (self->app != NULL)
	{
		g_object_remove_weak_pointer (G_OBJECT (self->app), (gpointer *) &self->app);
		self->app = NULL;
	}
	if (self->root != NULL)
		lrg_container_remove_child (LRG_CONTAINER (self), self->root);
	self->root = NULL;
	for (i = 0; i < LRG_EDITOR_VIEW_MAX_VIEWPORTS; i++)
		if (self->panels[i] != NULL)
			detach (self, LRG_WIDGET (self->panels[i]));
	if (self->h_top != NULL)
		detach (self, LRG_WIDGET (self->h_top));
	if (self->h_bottom != NULL)
		detach (self, LRG_WIDGET (self->h_bottom));
	for (i = 0; i < LRG_EDITOR_VIEW_MAX_VIEWPORTS; i++)
		g_clear_object (&self->panels[i]);
	g_clear_object (&self->v_root);
	g_clear_object (&self->h_top);
	g_clear_object (&self->h_bottom);

	G_OBJECT_CLASS (lrg_editor_viewport_split_parent_class)->dispose (object);
}

static void
lrg_editor_viewport_split_class_init (LrgEditorViewportSplitClass *klass)
{
	GObjectClass      *object_class = G_OBJECT_CLASS (klass);
	LrgContainerClass *container_class = LRG_CONTAINER_CLASS (klass);

	object_class->dispose = lrg_editor_viewport_split_dispose;
	container_class->layout_children = lrg_editor_viewport_split_layout_children;
}

static void
lrg_editor_viewport_split_init (LrgEditorViewportSplit *self)
{
	self->maximized = -1;
}

LrgEditorViewportSplit *
lrg_editor_viewport_split_new (LrgEditorApp *app)
{
	LrgEditorViewportSplit *self;
	guint                   i;

	g_return_val_if_fail (LRG_IS_EDITOR_APP (app), NULL);

	self = g_object_new (LRG_TYPE_EDITOR_VIEWPORT_SPLIT, NULL);
	self->app = app;
	g_object_add_weak_pointer (G_OBJECT (app), (gpointer *) &self->app);
	lrg_widget_set_name (LRG_WIDGET (self), "viewport-split");

	/* Four panels and three splitters, kept for the split's lifetime */
	for (i = 0; i < LRG_EDITOR_VIEW_MAX_VIEWPORTS; i++)
		self->panels[i] = lrg_editor_viewport_panel_new_for_index (app, i);
	self->v_root = lrg_splitter_new (LRG_ORIENTATION_VERTICAL);
	self->h_top = lrg_splitter_new (LRG_ORIENTATION_HORIZONTAL);
	self->h_bottom = lrg_splitter_new (LRG_ORIENTATION_HORIZONTAL);
	lrg_widget_set_name (LRG_WIDGET (self->v_root), "viewport-split-v");
	lrg_widget_set_name (LRG_WIDGET (self->h_top), "viewport-split-top");
	lrg_widget_set_name (LRG_WIDGET (self->h_bottom), "viewport-split-bottom");
	lrg_splitter_set_fraction (self->v_root, 0.5f, FALSE);
	lrg_splitter_set_fraction (self->h_top, 0.5f, FALSE);
	lrg_splitter_set_fraction (self->h_bottom, 0.5f, FALSE);

	self->view = g_object_ref (app->view);
	g_signal_connect (app->view, "notify::layout", G_CALLBACK (on_layout_notify), self);
	lrg_editor_viewport_split_register_methods (app);
	rebuild (self);
	return self;
}

LrgEditorViewportPanel *
lrg_editor_viewport_split_get_panel (LrgEditorViewportSplit *self,
                                     guint                   index)
{
	g_return_val_if_fail (LRG_IS_EDITOR_VIEWPORT_SPLIT (self), NULL);

	if (index >= LRG_EDITOR_VIEW_MAX_VIEWPORTS)
		return NULL;
	return self->panels[index];
}

guint
lrg_editor_viewport_split_get_n_visible (LrgEditorViewportSplit *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_VIEWPORT_SPLIT (self), 1);

	if (self->maximized >= 0 || self->app == NULL)
		return 1;
	return lrg_editor_view_state_get_viewport_count (self->app->view);
}

LrgEditorViewportPanel *
lrg_editor_viewport_split_get_active_panel (LrgEditorViewportSplit *self)
{
	guint i;

	g_return_val_if_fail (LRG_IS_EDITOR_VIEWPORT_SPLIT (self), NULL);

	i = (self->app != NULL) ? lrg_editor_view_state_get_active_viewport (self->app->view) : 0;
	return self->panels[MIN (i, LRG_EDITOR_VIEW_MAX_VIEWPORTS - 1)];
}

void
lrg_editor_viewport_split_set_maximized (LrgEditorViewportSplit *self,
                                         gint                    index)
{
	g_return_if_fail (LRG_IS_EDITOR_VIEWPORT_SPLIT (self));
	g_return_if_fail (index >= -1 && index < LRG_EDITOR_VIEW_MAX_VIEWPORTS);

	if (self->app == NULL || index == self->maximized)
		return;
	/* Only a viewport the layout shows can be maximised */
	if (index >= (gint) lrg_editor_view_state_get_viewport_count (self->app->view))
		return;
	self->maximized = index;
	if (index >= 0)
		lrg_editor_view_state_set_active_viewport (self->app->view, (guint) index);
	rebuild (self);
}

gint
lrg_editor_viewport_split_get_maximized (LrgEditorViewportSplit *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_VIEWPORT_SPLIT (self), -1);

	return self->maximized;
}

void
lrg_editor_viewport_split_set_gpu_enabled (LrgEditorViewportSplit *self,
                                           gboolean                enabled)
{
	guint i;

	g_return_if_fail (LRG_IS_EDITOR_VIEWPORT_SPLIT (self));

	for (i = 0; i < LRG_EDITOR_VIEW_MAX_VIEWPORTS; i++)
		lrg_editor_viewport_panel_set_gpu_enabled (self->panels[i], enabled);
}
