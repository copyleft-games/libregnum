/* lrg-editor-viewport-panel.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * The editor's scene viewport on the retained UI toolkit.
 *
 * The panel wraps an LrgViewport (render mode: it owns a render texture
 * sized to its allocation and calls our render function, which hands the
 * texture to LrgEditorSceneRenderer). Everything else is input routing,
 * in the order docs/editor/view-model.org prescribes:
 *
 *   press    tile painting (paint mode on a tilemap) ->
 *            LrgEditorViewportNav (fly / orbit / pan / zoom / axis gizmo) ->
 *            the transform gizmo (left button) ->
 *            lrg_editor_viewport_nav_press_empty() (arms click / box select)
 *   move     navigation, then the tile stroke or the gizmo drag, else the
 *            gizmo hover highlight
 *   release  ends the tile stroke / gizmo drag, then navigation, which
 *            emits ::click (pick), ::box-select or ::context-menu
 *   keys     Escape cancels a gizmo drag or leaves tile paint mode; the
 *            rest go to the navigator (W A S D Q E while flying), which
 *            returns TRUE only for keys it owns so shortcuts still fire;
 *            key releases arrive through the panel's handle_event (they
 *            bubble from the viewport) and focus loss releases held keys.
 *
 * Everything works headless: LrgViewport maps pointer positions with the
 * size its texture is heading for, so tests drive the viewport through
 * lrg_canvas_process_input() without a GL context.
 */

#include "config.h"

#include "lrg-editor-app-private.h"
#include "../../ui/lrg-menu.h"
#include <math.h>
#include <string.h>

#define CLICK_SLOP (4.0f)

struct _LrgEditorViewportPanel
{
	LrgContainer       parent_instance;

	LrgEditorApp      *app;          /* not owned */
	LrgViewport       *viewport;
	LrgMenu           *context_menu;

	LrgInputModifiers  mods;         /* of the last pointer event with modifiers */
	gfloat             pointer_u;
	gfloat             pointer_v;
	gboolean           hovered;
	gboolean           gizmo_drag;   /* the left button drags the transform gizmo */

	/* Tile painting (paint mode, left button) */
	gboolean           tile_drag;
	gboolean           tile_rect;
	gint               tile_rect_x0;
	gint               tile_rect_y0;
	gboolean           tile_erase;

	/* Click cycling through overlapping nodes */
	gfloat             last_pick_x;
	gfloat             last_pick_y;
	gfloat             size_w;
	gfloat             size_h;
};

G_DEFINE_FINAL_TYPE (LrgEditorViewportPanel, lrg_editor_viewport_panel, LRG_TYPE_CONTAINER)

/* The active camera as an LrgEditorView (picking and gizmo math). */
static void
current_view (LrgEditorViewportPanel *self,
              LrgEditorView          *view)
{
	lrg_editor_view_camera_to_view (lrg_editor_view_state_get_camera (self->app->view, -1), view);
}

/* ==========================================================================
 * Rendering
 * ========================================================================== */

static void
render_scene (LrgViewport      *viewport,
              GrlRenderTexture *target,
              gfloat            width,
              gfloat            height,
              gpointer          user_data)
{
	LrgEditorViewportPanel *self = LRG_EDITOR_VIEWPORT_PANEL (user_data);

	if (self->app == NULL || self->app->renderer == NULL)
		return;
	lrg_editor_view_state_set_viewport_size (self->app->view, 0, width, height);
	lrg_editor_scene_renderer_set_playing (self->app->renderer, lrg_editor_play_session_is_playing (self->app->play));
	lrg_editor_scene_renderer_render (self->app->renderer, self->app->view, 0, self->app->nav, TRUE, width, height);
}

/* ==========================================================================
 * Tile painting
 * ========================================================================== */

/* Cell under a viewport pixel: the ray meets the node's local XY plane. */
static gboolean
cell_under (LrgEditorViewportPanel *self,
            LrgNode                *node,
            gfloat                  u,
            gfloat                  v,
            gint                   *x,
            gint                   *y)
{
	LrgEditorView view;
	gfloat        world[16], o[3], d[3], n[3], p[3], t, len;
	const gfloat  z[3] = { 0.0f, 0.0f, 1.0f };

	*x = *y = -1;
	if (lrg_node_visual_get_tilemap (lrg_node_get_visual (node)) == NULL)
		return FALSE;

	lrg_editor_node_get_world_matrix (node, world);
	lrg_editor_math_transform_direction (world, z, n);
	len = sqrtf (n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
	if (len < 1e-6f)
		return FALSE;
	n[0] /= len;
	n[1] /= len;
	n[2] /= len;

	current_view (self, &view);
	lrg_editor_view_get_ray (&view, u, v, o, d);
	if (!lrg_editor_ray_plane (o, d, world + 12, n, &t) || t < 0.0f)
		return FALSE;
	p[0] = o[0] + d[0] * t;
	p[1] = o[1] + d[1] * t;
	p[2] = o[2] + d[2] * t;
	lrg_editor_tile_painter_cell_at (node, p, x, y);
	return TRUE;
}

/* Updates the hovered cell (tilemap.tool "hover"). */
static void
update_tile_hover (LrgEditorViewportPanel *self,
                   LrgNode                *node)
{
	LrgEditorApp   *app = self->app;
	LrgNodeTilemap *grid;
	gboolean        in;

	in = cell_under (self, node, self->pointer_u, self->pointer_v, &app->tile_hover_x, &app->tile_hover_y);
	grid = lrg_node_visual_get_tilemap (lrg_node_get_visual (node));
	app->tile_hover_valid = in && grid != NULL && self->hovered &&
	                        lrg_node_tilemap_in_bounds (grid, app->tile_hover_x, app->tile_hover_y);
}

/* The node tile painting applies to, or NULL when paint mode is off. */
static LrgNode *
tile_target (LrgEditorViewportPanel *self)
{
	if (!lrg_editor_view_state_get_tile_mode (self->app->view))
		return NULL;
	return editor_tiles_target (self->app);
}

static void
tile_finish (LrgEditorViewportPanel *self,
             LrgNode                *node,
             gboolean                commit)
{
	LrgEditorApp *app = self->app;

	if (lrg_editor_tile_painter_is_stroking (app->tile_painter))
	{
		guint n = lrg_editor_tile_painter_end_stroke (app->tile_painter);

		editor_app_set_status (app, "%s %u tile(s)", self->tile_erase ? "Erased" : "Painted", n);
	}
	if (self->tile_rect && commit && node != NULL)
	{
		guint n = lrg_editor_tile_painter_paint_rect (app->tile_painter, node, self->tile_rect_x0,
		                                              self->tile_rect_y0, app->tile_hover_x,
		                                              app->tile_hover_y, self->tile_erase);

		editor_app_set_status (app, "Rectangle: %u tile(s)", n);
	}
	self->tile_rect = FALSE;
	self->tile_drag = FALSE;
}

/* Left press in paint mode: start the tool (Ctrl + click picks, Shift
 * erases). Returns TRUE when the tile tool took the press. */
static gboolean
tile_press (LrgEditorViewportPanel *self,
            LrgInputModifiers       mods)
{
	LrgEditorApp      *app = self->app;
	LrgNode           *node = tile_target (self);
	LrgEditorTileTool  tool;

	if (node == NULL || (mods & LRG_INPUT_MODIFIER_ALT))
		return FALSE;

	tool = (mods & LRG_INPUT_MODIFIER_CTRL) ? LRG_EDITOR_TILE_TOOL_PICK
	                                        : lrg_editor_view_state_get_tile_tool (app->view);
	self->tile_erase = tool == LRG_EDITOR_TILE_TOOL_ERASER || (mods & LRG_INPUT_MODIFIER_SHIFT);

	/* A node that was never painted gets its grid first */
	if (lrg_node_visual_get_tilemap (lrg_node_get_visual (node)) == NULL && tool != LRG_EDITOR_TILE_TOOL_PICK)
		lrg_editor_tile_painter_ensure_tilemap (app->tile_painter, node);
	update_tile_hover (self, node);

	switch (tool)
	{
	case LRG_EDITOR_TILE_TOOL_BRUSH:
	case LRG_EDITOR_TILE_TOOL_ERASER:
		if (lrg_editor_tile_painter_begin_stroke (app->tile_painter, node, self->tile_erase))
		{
			update_tile_hover (self, node);
			lrg_editor_tile_painter_stroke_to (app->tile_painter, app->tile_hover_x, app->tile_hover_y);
			self->tile_drag = TRUE;
		}
		break;
	case LRG_EDITOR_TILE_TOOL_RECT:
		self->tile_rect = TRUE;
		self->tile_rect_x0 = app->tile_hover_x;
		self->tile_rect_y0 = app->tile_hover_y;
		self->tile_drag = TRUE;
		break;
	case LRG_EDITOR_TILE_TOOL_FILL:
		editor_app_set_status (app, "Filled %u tile(s)",
		                       lrg_editor_tile_painter_fill (app->tile_painter, node, app->tile_hover_x,
		                                                     app->tile_hover_y, self->tile_erase));
		break;
	case LRG_EDITOR_TILE_TOOL_PICK:
	default:
		{
			guint tile = lrg_editor_tile_painter_pick (app->tile_painter, node, app->tile_hover_x,
			                                           app->tile_hover_y);

			if (tile != 0)
				editor_app_set_status (app, "Picked tile %u", tile);
		}
		break;
	}
	return TRUE;
}

/* ==========================================================================
 * Picking and the navigator's gestures
 * ========================================================================== */

/* Click picking with cycling through overlapping nodes. */
static void
on_nav_click (LrgEditorViewportNav *nav,
              gfloat                x,
              gfloat                y,
              LrgInputModifiers     mods,
              gpointer              user_data)
{
	LrgEditorViewportPanel *self = LRG_EDITOR_VIEWPORT_PANEL (user_data);
	LrgEditor              *editor = self->app->editor;
	g_autoptr(GPtrArray)    hits = NULL;
	LrgEditorView           view;
	gfloat                  o[3], d[3];
	LrgNode                *current = lrg_editor_selection_get_primary (lrg_editor_get_selection (editor));
	LrgNode                *pick;
	gboolean                same_spot;
	guint                   i;

	current_view (self, &view);
	lrg_editor_view_get_ray (&view, x, y, o, d);
	hits = lrg_editor_pick_all (lrg_editor_get_level (editor), o, d, FALSE);
	if (hits->len == 0)
	{
		if (!(mods & (LRG_INPUT_MODIFIER_SHIFT | LRG_INPUT_MODIFIER_CTRL)))
			lrg_editor_select (editor, NULL, FALSE);
		return;
	}

	/* Clicking the same spot again walks to the next node behind */
	same_spot = fabsf (x - self->last_pick_x) < CLICK_SLOP && fabsf (y - self->last_pick_y) < CLICK_SLOP;
	pick = g_ptr_array_index (hits, 0);
	if (same_spot && current != NULL && !(mods & (LRG_INPUT_MODIFIER_SHIFT | LRG_INPUT_MODIFIER_CTRL)))
	{
		for (i = 0; i < hits->len; i++)
			if (g_ptr_array_index (hits, i) == current)
			{
				pick = g_ptr_array_index (hits, (i + 1) % hits->len);
				break;
			}
	}
	self->last_pick_x = x;
	self->last_pick_y = y;

	if (mods & LRG_INPUT_MODIFIER_CTRL)
		lrg_editor_toggle_selected (editor, pick);
	else
		lrg_editor_select (editor, pick, (mods & LRG_INPUT_MODIFIER_SHIFT) != 0);
	editor_app_set_status (self->app, "%s%s", lrg_node_get_name (pick) != NULL ? lrg_node_get_name (pick) : "node",
	                       hits->len > 1 ? "  (click again to cycle overlapping nodes)" : "");
}

static void
on_nav_box_select (LrgEditorViewportNav *nav,
                   gfloat                x0,
                   gfloat                y0,
                   gfloat                x1,
                   gfloat                y1,
                   LrgInputModifiers     mods,
                   gpointer              user_data)
{
	LrgEditorViewportPanel *self = LRG_EDITOR_VIEWPORT_PANEL (user_data);
	g_autoptr(GPtrArray)    boxed = NULL;
	LrgEditorView           view;

	current_view (self, &view);
	boxed = lrg_editor_box_select (lrg_editor_get_level (self->app->editor), &view, x0, y0, x1, y1, FALSE);
	lrg_editor_select_nodes (self->app->editor, boxed, (mods & LRG_INPUT_MODIFIER_SHIFT) != 0);
	editor_app_set_status (self->app, "Box selected %u node(s)", boxed->len);
}

/* The viewport context menu: create, paste, duplicate, delete, group,
 * focus, frame all, select all. */
static GMenuModel *
build_context_menu (void)
{
	static const gchar *items[] = {
		"edit.paste", "-", "edit.duplicate", "edit.delete", "node.group", "-",
		"view.focus", "view.frame-all", "edit.select-all", NULL
	};
	static const gchar *labels[] = {
		"Paste", "-", "Duplicate", "Delete", "Group Selection", "-",
		"Focus Selection", "Frame All", "Select All", NULL
	};
	GMenu                  *menu = g_menu_new ();
	g_autoptr(GMenu)        create = g_menu_new ();
	g_autoptr(GMenu)        section = g_menu_new ();
	const EditorCreateItem *kinds;
	guint                   n, i;

	kinds = editor_create_items (&n);
	for (i = 0; i < n; i++)
	{
		g_autoptr(GMenuItem) item = NULL;

		if (kinds[i].id[0] == '-')
			continue;
		item = g_menu_item_new (kinds[i].label, NULL);
		g_menu_item_set_action_and_target_value (item, "win.create", g_variant_new_string (kinds[i].id));
		g_menu_append_item (create, item);
	}
	g_menu_append_submenu (section, "Create", G_MENU_MODEL (create));
	for (i = 0; items[i] != NULL; i++)
	{
		g_autofree gchar *detailed = NULL;

		if (items[i][0] == '-')
		{
			g_menu_append_section (menu, NULL, G_MENU_MODEL (section));
			g_clear_object (&section);
			section = g_menu_new ();
			continue;
		}
		detailed = g_strconcat ("editor.", items[i], NULL);
		g_menu_append (section, labels[i], detailed);
	}
	g_menu_append_section (menu, NULL, G_MENU_MODEL (section));
	return G_MENU_MODEL (menu);
}

static void
on_nav_context_menu (LrgEditorViewportNav *nav,
                     gfloat                x,
                     gfloat                y,
                     gpointer              user_data)
{
	LrgEditorViewportPanel *self = LRG_EDITOR_VIEWPORT_PANEL (user_data);
	gfloat                  wx = 0.0f, wy = 0.0f;

	if (self->context_menu == NULL)
	{
		g_autoptr(GMenuModel) model = build_context_menu ();

		self->context_menu = g_object_ref_sink (lrg_menu_new_from_model (model));
	}
	lrg_viewport_texture_to_widget (self->viewport, x, y, &wx, &wy);
	lrg_menu_popup_at_point (self->context_menu, LRG_WIDGET (self),
	                         lrg_widget_get_world_x (LRG_WIDGET (self->viewport)) + wx,
	                         lrg_widget_get_world_y (LRG_WIDGET (self->viewport)) + wy);
}

/* ==========================================================================
 * Pointer and key routing
 * ========================================================================== */

static void
take_focus (LrgEditorViewportPanel *self)
{
	LrgWidget *root = lrg_widget_get_root (LRG_WIDGET (self));

	if (LRG_IS_CANVAS (root) && lrg_canvas_get_focused_widget (LRG_CANVAS (root)) != LRG_WIDGET (self->viewport))
		lrg_canvas_set_focused_widget (LRG_CANVAS (root), LRG_WIDGET (self->viewport));
}

static void
on_pointer_press (LrgViewport       *viewport,
                  gint               button,
                  gfloat             u,
                  gfloat             v,
                  LrgInputModifiers  mods,
                  gpointer           user_data)
{
	LrgEditorViewportPanel *self = LRG_EDITOR_VIEWPORT_PANEL (user_data);
	LrgEditorApp           *app = self->app;
	LrgEditorView           view;

	self->mods = mods;
	self->pointer_u = u;
	self->pointer_v = v;
	self->hovered = TRUE;
	take_focus (self);

	/* A right click (or any other button) cancels a gizmo drag */
	if (self->gizmo_drag)
	{
		if (button != (gint) GRL_MOUSE_BUTTON_LEFT)
		{
			lrg_editor_gizmo_cancel_drag (app->gizmo);
			self->gizmo_drag = FALSE;
			editor_app_set_status (app, "Transform cancelled");
		}
		return;
	}
	if (self->tile_drag)
		return;

	if (button == (gint) GRL_MOUSE_BUTTON_LEFT && tile_press (self, mods))
		return;
	if (lrg_editor_viewport_nav_pointer_press (app->nav, (GrlMouseButton) button, u, v, mods))
		return;
	if (button != (gint) GRL_MOUSE_BUTTON_LEFT || (mods & LRG_INPUT_MODIFIER_ALT))
		return;

	current_view (self, &view);
	if (lrg_editor_gizmo_begin_drag (app->gizmo, &view, u, v))
		self->gizmo_drag = TRUE;
	else
		lrg_editor_viewport_nav_press_empty (app->nav, u, v);
}

static void
on_pointer_move (LrgViewport *viewport,
                 gfloat       u,
                 gfloat       v,
                 gfloat       dx,
                 gfloat       dy,
                 gpointer     user_data)
{
	LrgEditorViewportPanel *self = LRG_EDITOR_VIEWPORT_PANEL (user_data);
	LrgEditorApp           *app = self->app;
	LrgEditorView           view;
	LrgNode                *node;

	self->pointer_u = u;
	self->pointer_v = v;
	self->hovered = lrg_viewport_widget_to_texture (viewport, u, v, NULL, NULL) || TRUE;

	if (lrg_editor_viewport_nav_pointer_move (app->nav, u, v, dx, dy, self->mods))
		return;

	node = tile_target (self);
	if (node != NULL)
	{
		update_tile_hover (self, node);
		if (self->tile_drag && lrg_editor_tile_painter_is_stroking (app->tile_painter))
			lrg_editor_tile_painter_stroke_to (app->tile_painter, app->tile_hover_x, app->tile_hover_y);
	}
	else
		app->tile_hover_valid = FALSE;

	current_view (self, &view);
	if (self->gizmo_drag)
		lrg_editor_gizmo_drag (app->gizmo, &view, u, v, (self->mods & LRG_INPUT_MODIFIER_CTRL) != 0);
	else if (lrg_editor_viewport_nav_get_mode (app->nav) == LRG_EDITOR_NAV_MODE_NONE && !self->tile_drag)
		lrg_editor_gizmo_update_hover (app->gizmo, &view, u, v);
}

static void
on_pointer_release (LrgViewport       *viewport,
                    gint               button,
                    gfloat             u,
                    gfloat             v,
                    LrgInputModifiers  mods,
                    gpointer           user_data)
{
	LrgEditorViewportPanel *self = LRG_EDITOR_VIEWPORT_PANEL (user_data);
	LrgEditorApp           *app = self->app;

	self->mods = mods;
	self->pointer_u = u;
	self->pointer_v = v;

	if (button == (gint) GRL_MOUSE_BUTTON_LEFT && self->tile_drag)
	{
		LrgNode *node = tile_target (self);

		/* The release carries the last motion: paint up to it first */
		if (node != NULL)
		{
			update_tile_hover (self, node);
			if (lrg_editor_tile_painter_is_stroking (app->tile_painter))
				lrg_editor_tile_painter_stroke_to (app->tile_painter, app->tile_hover_x, app->tile_hover_y);
		}
		tile_finish (self, node, TRUE);
		return;
	}
	if (button == (gint) GRL_MOUSE_BUTTON_LEFT && self->gizmo_drag)
	{
		LrgEditorView view;

		current_view (self, &view);
		lrg_editor_gizmo_drag (app->gizmo, &view, u, v, (mods & LRG_INPUT_MODIFIER_CTRL) != 0);
		lrg_editor_gizmo_end_drag (app->gizmo);
		self->gizmo_drag = FALSE;
		return;
	}
	lrg_editor_viewport_nav_pointer_release (app->nav, (GrlMouseButton) button, u, v, mods);
}

static void
on_pointer_scroll (LrgViewport *viewport,
                   gfloat       u,
                   gfloat       v,
                   gfloat       scroll_x,
                   gfloat       scroll_y,
                   gpointer     user_data)
{
	LrgEditorViewportPanel *self = LRG_EDITOR_VIEWPORT_PANEL (user_data);
	LrgWidget              *root = lrg_widget_get_root (LRG_WIDGET (self));
	LrgInputModifiers       mods = self->mods;

	/* The scroll signal has no modifiers: the app remembers this frame's */
	if (LRG_IS_CANVAS (root) && self->app != NULL)
		mods = self->app->last_input.modifiers;
	lrg_editor_viewport_nav_scroll (self->app->nav, scroll_x, scroll_y, mods);
}

static gboolean
on_key (LrgViewport       *viewport,
        GrlKey             key,
        LrgInputModifiers  mods,
        gpointer           user_data)
{
	LrgEditorViewportPanel *self = LRG_EDITOR_VIEWPORT_PANEL (user_data);
	LrgEditorApp           *app = self->app;

	self->mods = mods;
	if (key == GRL_KEY_ESCAPE)
	{
		/* A gizmo drag owns Escape */
		if (self->gizmo_drag)
		{
			lrg_editor_gizmo_cancel_drag (app->gizmo);
			self->gizmo_drag = FALSE;
			editor_app_set_status (app, "Transform cancelled");
			return TRUE;
		}
		/* Escape cancels a rectangle, otherwise leaves paint mode */
		if (lrg_editor_view_state_get_tile_mode (app->view))
		{
			gboolean was_rect = self->tile_rect;

			tile_finish (self, NULL, FALSE);
			if (!was_rect)
			{
				lrg_editor_view_state_set_tile_mode (app->view, FALSE);
				app->tile_hover_valid = FALSE;
				editor_app_set_status (app, "Tile painting off");
			}
			return TRUE;
		}
	}
	return lrg_editor_viewport_nav_key_down (app->nav, key, mods);
}

/* Key releases bubble here from the focused viewport; focus loss drops
 * held keys so the camera cannot keep flying. */
static gboolean
lrg_editor_viewport_panel_handle_event (LrgWidget        *widget,
                                        const LrgUIEvent *event)
{
	LrgEditorViewportPanel *self = LRG_EDITOR_VIEWPORT_PANEL (widget);

	if (self->app == NULL)
		return FALSE;
	switch (lrg_ui_event_get_event_type (event))
	{
	case LRG_UI_EVENT_KEY_UP:
		return lrg_editor_viewport_nav_key_up (self->app->nav, lrg_ui_event_get_key (event),
		                                       lrg_ui_event_get_modifiers (event));
	case LRG_UI_EVENT_FOCUS_OUT:
		lrg_editor_viewport_nav_focus_lost (self->app->nav);
		return FALSE;
	case LRG_UI_EVENT_POINTER_LEAVE:
		self->hovered = FALSE;
		self->app->tile_hover_valid = FALSE;
		return FALSE;
	default:
		return FALSE;
	}
}

/* ==========================================================================
 * Internal API
 * ========================================================================== */

/* Per frame: keeps the camera's viewport size in step with the widget
 * (also headless, where no render texture exists) and ticks the held
 * fly keys. */
void
editor_viewport_panel_tick (LrgEditorViewportPanel *self,
                            gfloat                  dt)
{
	gint w = 0, h = 0;

	lrg_viewport_get_content_size (self->viewport, &w, &h);
	if (w > 0 && h > 0 && ((gfloat) w != self->size_w || (gfloat) h != self->size_h))
	{
		self->size_w = (gfloat) w;
		self->size_h = (gfloat) h;
		lrg_editor_view_state_set_viewport_size (self->app->view, 0, (gfloat) w, (gfloat) h);
	}
	if (lrg_editor_viewport_nav_tick (self->app->nav, dt))
		lrg_viewport_queue_render (self->viewport);
}

/* The viewport content rectangle in canvas coordinates (ui.state). */
gboolean
editor_viewport_panel_get_rect (LrgEditorViewportPanel *self,
                                gfloat                 *x,
                                gfloat                 *y,
                                gfloat                 *w,
                                gfloat                 *h)
{
	GrlRectangle r = { 0 };
	gboolean     ok;

	ok = lrg_viewport_get_display_rect (self->viewport, &r) && lrg_widget_get_visible (LRG_WIDGET (self)) &&
	     lrg_widget_get_parent (LRG_WIDGET (self)) != NULL;
	*x = lrg_widget_get_world_x (LRG_WIDGET (self->viewport)) + r.x;
	*y = lrg_widget_get_world_y (LRG_WIDGET (self->viewport)) + r.y;
	*w = r.width;
	*h = r.height;
	return ok;
}

gboolean
editor_viewport_panel_is_hovered (LrgEditorViewportPanel *self)
{
	return (lrg_widget_get_state_flags (LRG_WIDGET (self->viewport)) & LRG_WIDGET_STATE_HOVERED) != 0;
}

/* Reads the last rendered viewport image back and writes it as PNG. */
gboolean
editor_viewport_panel_screenshot (LrgEditorViewportPanel  *self,
                                  const gchar             *path,
                                  GError                 **error)
{
	GrlRenderTexture   *target = lrg_viewport_get_render_texture (self->viewport);
	g_autoptr(GrlImage) image = NULL;

	if (target == NULL)
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
		             "No viewport image (the editor is headless or the viewport is hidden)");
		return FALSE;
	}
	image = grl_texture_to_image (grl_render_texture_get_texture (target));
	if (image == NULL)
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED, "Could not read the viewport image back");
		return FALSE;
	}
	grl_image_flip_vertical (image);
	if (!grl_image_export (image, path))
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED, "Could not write %s", path);
		return FALSE;
	}
	return TRUE;
}

/* ==========================================================================
 * GObject
 * ========================================================================== */

static void
lrg_editor_viewport_panel_layout_children (LrgContainer *container)
{
	LrgEditorViewportPanel *self = LRG_EDITOR_VIEWPORT_PANEL (container);

	lrg_widget_place_in_slot (LRG_WIDGET (self->viewport), 0.0f, 0.0f,
	                          lrg_widget_get_width (LRG_WIDGET (self)),
	                          lrg_widget_get_height (LRG_WIDGET (self)));
}

static void
lrg_editor_viewport_panel_dispose (GObject *object)
{
	LrgEditorViewportPanel *self = LRG_EDITOR_VIEWPORT_PANEL (object);

	if (self->app != NULL)
	{
		g_signal_handlers_disconnect_by_data (self->app->nav, self);
		self->app = NULL;
	}
	if (self->context_menu != NULL)
		lrg_menu_close (self->context_menu);
	g_clear_object (&self->context_menu);

	G_OBJECT_CLASS (lrg_editor_viewport_panel_parent_class)->dispose (object);
}

static void
lrg_editor_viewport_panel_class_init (LrgEditorViewportPanelClass *klass)
{
	GObjectClass      *object_class = G_OBJECT_CLASS (klass);
	LrgWidgetClass    *widget_class = LRG_WIDGET_CLASS (klass);
	LrgContainerClass *container_class = LRG_CONTAINER_CLASS (klass);

	object_class->dispose = lrg_editor_viewport_panel_dispose;
	widget_class->handle_event = lrg_editor_viewport_panel_handle_event;
	container_class->layout_children = lrg_editor_viewport_panel_layout_children;
}

static void
lrg_editor_viewport_panel_init (LrgEditorViewportPanel *self)
{
	self->last_pick_x = -1000.0f;
	self->last_pick_y = -1000.0f;
}

LrgEditorViewportPanel *
lrg_editor_viewport_panel_new (LrgEditorApp *app)
{
	LrgEditorViewportPanel *self;

	g_return_val_if_fail (LRG_IS_EDITOR_APP (app), NULL);

	self = g_object_new (LRG_TYPE_EDITOR_VIEWPORT_PANEL, NULL);
	self->app = app;
	lrg_widget_set_name (LRG_WIDGET (self), "viewport-panel");

	self->viewport = lrg_viewport_new ();
	lrg_widget_set_name (LRG_WIDGET (self->viewport), "viewport");
	lrg_viewport_set_mode (self->viewport, LRG_VIEWPORT_MODE_RENDER);
	lrg_viewport_set_continuous (self->viewport, TRUE);
	lrg_viewport_set_auto_clear (self->viewport, FALSE);
	lrg_viewport_set_render_func (self->viewport, render_scene, self, NULL);
	g_signal_connect (self->viewport, "pointer-press", G_CALLBACK (on_pointer_press), self);
	g_signal_connect (self->viewport, "pointer-move", G_CALLBACK (on_pointer_move), self);
	g_signal_connect (self->viewport, "pointer-release", G_CALLBACK (on_pointer_release), self);
	g_signal_connect (self->viewport, "pointer-scroll", G_CALLBACK (on_pointer_scroll), self);
	g_signal_connect (self->viewport, "key", G_CALLBACK (on_key), self);
	lrg_container_add_child (LRG_CONTAINER (self), LRG_WIDGET (self->viewport));
	g_object_unref (self->viewport);

	g_signal_connect (app->nav, "click", G_CALLBACK (on_nav_click), self);
	g_signal_connect (app->nav, "box-select", G_CALLBACK (on_nav_box_select), self);
	g_signal_connect (app->nav, "context-menu", G_CALLBACK (on_nav_context_menu), self);
	return self;
}

LrgViewport *
lrg_editor_viewport_panel_get_viewport (LrgEditorViewportPanel *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_VIEWPORT_PANEL (self), NULL);
	return self->viewport;
}
