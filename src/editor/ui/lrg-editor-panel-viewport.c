/* lrg-editor-panel-viewport.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * The viewport panel's interaction and overlays. Rendering of the scene
 * itself happens in lrg-editor-render.c (only with a window); everything
 * here works headless, which is what the app tests drive.
 *
 * Mouse model (Godot/Unity-like):
 *   right drag        fly look; WASD/QE move, Shift faster, wheel = speed
 *   right click       context menu
 *   middle drag       pan
 *   Alt + left drag   orbit around the target
 *   wheel             zoom (dolly)
 *   left click        pick (Shift add, Ctrl toggle, click again to cycle
 *                     through overlapping nodes)
 *   left drag (empty) box select
 *   left drag (gizmo) move / rotate / scale; Ctrl inverts snapping;
 *                     Escape or right click cancels
 * In 2D mode right and middle drag pan, the wheel zooms.
 */

#include "lrg-editor-app-private.h"
#include <math.h>
#include <string.h>

#define DEG2RAD           ((gfloat) G_PI / 180.0f)
#define CLICK_SLOP        (4.0f)
#define FLY_FAST          (3.0f)

void
editor_view_reset (LrgEditorApp *self)
{
	gdouble fov = 50.0;
	gfloat  w = MAX (self->view.width, 1.0f);
	gfloat  h = MAX (self->view.height, 1.0f);

	if (self->settings != NULL)
		g_object_get (self->settings, "camera-fov", &fov, NULL);

	lrg_editor_view_init_default (&self->view, w, h);
	self->view.fovy = (gfloat) fov;
	if (self->view_2d)
	{
		/* Looking down -Z at the XY plane. */
		self->view.position[0] = 0.0f;
		self->view.position[1] = 0.0f;
		self->view.position[2] = 50.0f;
		self->view.orthographic = TRUE;
		self->view.ortho_height = 20.0f;
	}
}

/* Camera on an axis (direction = where the camera sits), orthographic;
 * the next orbit returns to perspective (Blender's auto-perspective). */
void
editor_view_align (LrgEditorApp *self,
                   gfloat        x,
                   gfloat        y,
                   gfloat        z)
{
	const gfloat dir[3] = { x, y, z };

	lrg_editor_view_align (&self->view, dir);
	if (!self->view_2d)
	{
		self->view.orthographic = TRUE;
		self->ortho_from_align = TRUE;
	}
}

/* Orbits around the target (radians); leaves an aligned ortho view. */
void
editor_view_orbit (LrgEditorApp *self,
                   gfloat        yaw,
                   gfloat        pitch)
{
	if (self->view_2d)
		return;
	if (self->ortho_from_align)
	{
		self->view.orthographic = FALSE;
		self->ortho_from_align = FALSE;
	}
	lrg_editor_view_orbit (&self->view, yaw, pitch);
}

/* ==========================================================================
 * Orientation gizmo (bottom left): drag to orbit, click an axis to look
 * along it. It works with any pointing device, including touchpads that
 * cannot right- or middle-drag.
 * ========================================================================== */

#define NAV_GIZMO_RADIUS  (30.0f)
#define NAV_GIZMO_MARGIN  (46.0f)
#define NAV_GIZMO_END_HIT (9.0f)

static void
nav_gizmo_center (const LrgGuiRect *rect,
                  gfloat           *cx,
                  gfloat           *cy)
{
	*cx = rect->x + NAV_GIZMO_MARGIN;
	*cy = rect->y + rect->h - NAV_GIZMO_MARGIN;
}

/* Screen offset of axis end @index: 0..2 = +X +Y +Z, 3..5 = -X -Y -Z. */
static void
nav_gizmo_end (LrgEditorApp *self,
               gint          index,
               gfloat       *dx,
               gfloat       *dy)
{
	gfloat view[16];
	gint   axis = index % 3;
	gfloat sign = (index < 3) ? 1.0f : -1.0f;

	/* Column @axis of the view rotation = that world axis in camera space. */
	lrg_editor_view_get_view_matrix (&self->view, view);
	*dx = view[axis * 4 + 0] * sign * NAV_GIZMO_RADIUS;
	*dy = -view[axis * 4 + 1] * sign * NAV_GIZMO_RADIUS;
}

/* -1: outside; -2: on the gizmo background; 0..5: an axis end. */
static gint
nav_gizmo_hit (LrgEditorApp     *self,
               const LrgGuiRect *rect,
               gfloat            mx,
               gfloat            my)
{
	gfloat cx, cy, best = NAV_GIZMO_END_HIT;
	gint   hit = -1, i;

	if (self->view_2d)
		return -1;
	nav_gizmo_center (rect, &cx, &cy);
	for (i = 0; i < 6; i++)
	{
		gfloat dx, dy, d;

		nav_gizmo_end (self, i, &dx, &dy);
		d = hypotf (mx - (cx + dx), my - (cy + dy));
		if (d < best)
		{
			best = d;
			hit = i;
		}
	}
	if (hit >= 0)
		return hit;
	return (hypotf (mx - cx, my - cy) <= NAV_GIZMO_RADIUS + NAV_GIZMO_END_HIT) ? -2 : -1;
}

static void
nav_gizmo_draw (LrgEditorApp     *self,
                const LrgGuiRect *rect)
{
	static const gchar *names[3] = { "X", "Y", "Z" };
	LrgGuiStyle        *style = lrg_gui_context_get_style (self->gui);
	LrgGuiDrawList     *dl = lrg_gui_get_draw_list (self->gui);
	gfloat              cx, cy, depth[6];
	gint                order[6], i, j;
	gfloat              view[16];

	if (self->view_2d)
		return;
	nav_gizmo_center (rect, &cx, &cy);

	/* A faint disc shows the drag area. */
	lrg_gui_draw_list_circle (dl, cx, cy, NAV_GIZMO_RADIUS + NAV_GIZMO_END_HIT,
	                          (style->colors[LRG_GUI_COL_PANEL_BG] & 0xffffff00u) |
	                          (self->nav == EDITOR_NAV_GIZMO || self->axis_hot != -1 ? 0xb0u : 0x60u), TRUE);

	/* Far ends first so near ones draw on top. */
	lrg_editor_view_get_view_matrix (&self->view, view);
	for (i = 0; i < 6; i++)
	{
		order[i] = i;
		depth[i] = view[(i % 3) * 4 + 2] * (i < 3 ? 1.0f : -1.0f);
	}
	for (i = 0; i < 6; i++)
		for (j = i + 1; j < 6; j++)
			if (depth[order[j]] < depth[order[i]])
			{
				gint t = order[i];

				order[i] = order[j];
				order[j] = t;
			}

	for (i = 0; i < 6; i++)
	{
		gint        e = order[i];
		gfloat      dx, dy;
		LrgGuiColor col = style->colors[LRG_GUI_COL_AXIS_X + e % 3];
		gboolean    hot = (self->axis_hot == e);

		nav_gizmo_end (self, e, &dx, &dy);
		if (e < 3)
		{
			lrg_gui_draw_list_line (dl, cx, cy, cx + dx, cy + dy, col, 2.0f);
			lrg_gui_draw_list_circle (dl, cx + dx, cy + dy, hot ? 9.0f : 7.5f, col, TRUE);
			lrg_gui_draw_list_text (dl, cx + dx - 4.0f, cy + dy - style->font_size * 0.45f, names[e], -1,
			                        style->font_size * 0.8f, LRG_GUI_RGBA (17, 17, 27, 255), FALSE);
		}
		else
		{
			lrg_gui_draw_list_circle (dl, cx + dx, cy + dy, hot ? 7.5f : 6.0f, (col & 0xffffff00u) | 0x90u, TRUE);
		}
	}
}

/* World point on the ground (y = 0, or z = 0 in 2D) under a pixel. */
static gboolean
ground_point (LrgEditorApp *self,
              gfloat        lx,
              gfloat        ly,
              gfloat       *out)
{
	gfloat       o[3], d[3], t;
	const gfloat p0[3] = { 0.0f, 0.0f, 0.0f };
	const gfloat n3d[3] = { 0.0f, 1.0f, 0.0f };
	const gfloat n2d[3] = { 0.0f, 0.0f, 1.0f };

	lrg_editor_view_get_ray (&self->view, lx, ly, o, d);
	if (!lrg_editor_ray_plane (o, d, p0, self->view_2d ? n2d : n3d, &t))
	{
		/* Fall back to a point in front of the camera. */
		t = 10.0f;
	}
	out[0] = o[0] + d[0] * t;
	out[1] = o[1] + d[1] * t;
	out[2] = o[2] + d[2] * t;
	return TRUE;
}

void
editor_viewport_drop_asset (LrgEditorApp *self,
                            const gchar  *path,
                            gfloat        lx,
                            gfloat        ly)
{
	LrgAssetType      type = lrg_asset_database_classify (path);
	g_autofree gchar *base = g_path_get_basename (path);
	gfloat            at[3];
	LrgNode          *node = NULL;
	g_autoptr(GError) error = NULL;
	gdouble           snap_t = 0.0;

	ground_point (self, lx, ly, at);
	lrg_editor_get_snap (self->editor, &snap_t, NULL, NULL);
	if (lrg_editor_get_snap_enabled (self->editor))
	{
		at[0] = lrg_editor_snap_value (at[0], (gfloat) snap_t);
		at[1] = lrg_editor_snap_value (at[1], (gfloat) snap_t);
		at[2] = lrg_editor_snap_value (at[2], (gfloat) snap_t);
	}

	lrg_editor_begin_group (self->editor, "Place Asset");
	switch (type)
	{
	case LRG_ASSET_TYPE_PREFAB:
		{
			g_autofree gchar *abs = editor_app_project_path (self, path);

			node = lrg_editor_instantiate_prefab (self->editor, abs, NULL, &error);
		}
		break;
	case LRG_ASSET_TYPE_MODEL:
	case LRG_ASSET_TYPE_TEXTURE:
	case LRG_ASSET_TYPE_CAD:
		{
			LrgNodeVisualKind  kind = (type == LRG_ASSET_TYPE_MODEL) ? LRG_NODE_VISUAL_MESH_ASSET
			                        : (type == LRG_ASSET_TYPE_CAD) ? LRG_NODE_VISUAL_CAD_PART
			                                                        : LRG_NODE_VISUAL_SPRITE;
			g_autoptr(LrgNode) before = NULL;
			g_autofree gchar  *stem = g_strdup (base);
			gchar             *dot = strrchr (stem, '.');

			if (dot != NULL)
				*dot = '\0';
			node = editor_app_create_node (self, stem, kind, 0, NULL);
			before = lrg_editor_snapshot_node (node);
			lrg_node_visual_set_asset (lrg_node_get_visual (node), path);
			lrg_editor_commit_node_state (self->editor, node, before, "Set Asset", FALSE);
		}
		break;
	case LRG_ASSET_TYPE_SCRIPT:
		{
			/* Scripts attach to the node under the pointer. */
			gfloat   o[3], d[3];
			LrgNode *hit;

			lrg_editor_view_get_ray (&self->view, lx, ly, o, d);
			hit = lrg_editor_pick (lrg_editor_get_level (self->editor), o, d, FALSE, NULL);
			if (hit != NULL)
			{
				g_autoptr(LrgScriptBinding) b = lrg_script_binding_new (editor_script_language_for_path (path),
				                                                        path);

				lrg_editor_add_script (self->editor, hit, b);
				lrg_editor_select (self->editor, hit, FALSE);
				editor_app_set_status (self, "Attached %s to %s", base, lrg_node_get_name (hit));
			}
		}
		break;
	case LRG_ASSET_TYPE_LEVEL:
		editor_app_request (self, EDITOR_PENDING_OPEN_LEVEL, path);
		break;
	default:
		editor_app_set_status (self, "%s cannot be placed in the scene", base);
		break;
	}

	if (node != NULL && type != LRG_ASSET_TYPE_SCRIPT)
		lrg_editor_transform_node (self->editor, node, at, NULL, NULL, FALSE);
	lrg_editor_end_group (self->editor);

	if (error != NULL)
		editor_app_notify (self, 3, "%s: %s", base, error->message);
}

/* Click picking with cycling through overlapping nodes. */
static void
click_pick (LrgEditorApp *self,
            gfloat        lx,
            gfloat        ly)
{
	g_autoptr(GPtrArray) hits = NULL;
	gfloat               o[3], d[3];
	LrgGuiModifiers      mods = lrg_gui_get_mods (self->gui);
	LrgNode             *current = lrg_editor_selection_get_primary (lrg_editor_get_selection (self->editor));
	LrgNode             *pick;
	gboolean             same_spot;
	guint                i;

	lrg_editor_view_get_ray (&self->view, lx, ly, o, d);
	hits = lrg_editor_pick_all (lrg_editor_get_level (self->editor), o, d, FALSE);

	if (hits->len == 0)
	{
		if (!(mods & (LRG_GUI_MOD_SHIFT | LRG_GUI_MOD_CTRL)))
			lrg_editor_select (self->editor, NULL, FALSE);
		return;
	}

	/* Clicking the same spot again walks to the next node behind. */
	same_spot = fabsf (lx - self->last_pick_x) < CLICK_SLOP && fabsf (ly - self->last_pick_y) < CLICK_SLOP;
	pick = g_ptr_array_index (hits, 0);
	if (same_spot && current != NULL && !(mods & (LRG_GUI_MOD_SHIFT | LRG_GUI_MOD_CTRL)))
	{
		for (i = 0; i < hits->len; i++)
			if (g_ptr_array_index (hits, i) == current)
			{
				pick = g_ptr_array_index (hits, (i + 1) % hits->len);
				break;
			}
	}
	self->last_pick_x = lx;
	self->last_pick_y = ly;

	if (mods & LRG_GUI_MOD_CTRL)
		lrg_editor_toggle_selected (self->editor, pick);
	else
		lrg_editor_select (self->editor, pick, (mods & LRG_GUI_MOD_SHIFT) != 0);

	editor_app_set_status (self, "%s%s", lrg_node_get_name (pick) != NULL ? lrg_node_get_name (pick) : "node",
	                       hits->len > 1 ? "  (click again to cycle overlapping nodes)" : "");
}

static void
viewport_context_menu (LrgEditorApp *self)
{
	if (lrg_gui_begin_menu (self->gui, "Create", TRUE))
	{
		editor_app_menu_create (self, NULL);
		lrg_gui_end_menu (self->gui);
	}
	editor_app_menu_item_action (self, "edit.paste");
	lrg_gui_separator (self->gui);
	editor_app_menu_item_action (self, "edit.duplicate");
	editor_app_menu_item_action (self, "edit.delete");
	editor_app_menu_item_action (self, "node.group");
	lrg_gui_separator (self->gui);
	editor_app_menu_item_action (self, "view.focus");
	editor_app_menu_item_action (self, "view.frame-all");
	editor_app_menu_item_action (self, "edit.select-all");
}

/* Top-left view menu. Submitted before the viewport surface so its
 * clicks win. */
static void
overlay_controls (LrgEditorApp     *self,
                  const LrgGuiRect *rect)
{
	gfloat pad = 6.0f;

	/* View menu button. */
	lrg_gui_set_cursor (self->gui, rect->x + pad, rect->y + pad);
	{
		const gchar      *mode = self->view_2d ? "2D" : (self->view.orthographic ? "Orthographic" : "Perspective");
		g_autofree gchar *btn = g_strdup_printf ("%s%s###view-menu", mode, self->wireframe ? " | Wire" : "");

		if (lrg_gui_button (self->gui, btn))
			lrg_gui_open_popup (self->gui, "##view-popup");
	}
	if (lrg_gui_begin_popup (self->gui, "##view-popup"))
	{
		gfloat fov = self->view.fovy;

		editor_app_menu_item_action (self, "view.perspective");
		editor_app_menu_item_action (self, "view.toggle-2d");
		lrg_gui_separator (self->gui);
		editor_app_menu_item_action (self, "view.top");
		editor_app_menu_item_action (self, "view.front");
		editor_app_menu_item_action (self, "view.right");
		editor_app_menu_item_action (self, "view.bottom");
		editor_app_menu_item_action (self, "view.back");
		editor_app_menu_item_action (self, "view.left");
		lrg_gui_separator (self->gui);
		editor_app_menu_item_action (self, "view.grid");
		editor_app_menu_item_action (self, "view.icons");
		editor_app_menu_item_action (self, "view.wireframe");
		editor_app_menu_item_action (self, "view.stats");
		lrg_gui_separator (self->gui);
		lrg_gui_property_label (self->gui, "Fly speed");
		lrg_gui_drag_float (self->gui, "##fly", &self->fly_speed, 0.1f, 0.1f, 500.0f, "%.1f");
		lrg_gui_property_label (self->gui, "Field of view");
		if (lrg_gui_drag_float (self->gui, "##fov", &fov, 0.2f, 10.0f, 150.0f, "%.0f deg"))
			self->view.fovy = fov;
		if (lrg_gui_menu_item (self->gui, "Reset Camera", NULL, FALSE, TRUE))
			editor_view_reset (self);
		lrg_gui_end_popup (self->gui);
	}
}

/* Stats, orientation axes, play frame, hints. */
static void
overlays (LrgEditorApp     *self,
          const LrgGuiRect *rect)
{
	LrgGuiStyle    *style = lrg_gui_context_get_style (self->gui);
	LrgGuiDrawList *dl = lrg_gui_get_draw_list (self->gui);
	gfloat          pad = 6.0f;

	/* Stats. */
	if (self->show_stats)
	{
		g_autofree gchar *text = g_strdup_printf ("%u nodes  %u selected  %.0f fps",
		                                          editor_app_count_nodes (self),
		                                          lrg_editor_selection_get_count (lrg_editor_get_selection (self->editor)),
		                                          self->fps);
		gfloat            tw = lrg_gui_calc_text_width (self->gui, text);

		lrg_gui_draw_list_text (dl, rect->x + rect->w - tw - pad * 2.0f, rect->y + pad * 1.5f, text, -1,
		                        style->font_size, style->colors[LRG_GUI_COL_TEXT_DISABLED], FALSE);

		/* Input as the editor sees it, for diagnosing devices. */
		if (self->cur_input != NULL)
		{
			const LrgGuiInput *in = self->cur_input;
			g_autofree gchar  *line = g_strdup_printf ("pointer %.0f,%.0f  buttons %c%c%c  mods %c%c%c",
			                                           in->mouse_x, in->mouse_y,
			                                           in->mouse_down[LRG_GUI_MOUSE_LEFT] ? 'L' : '-',
			                                           in->mouse_down[LRG_GUI_MOUSE_MIDDLE] ? 'M' : '-',
			                                           in->mouse_down[LRG_GUI_MOUSE_RIGHT] ? 'R' : '-',
			                                           (in->mods & LRG_GUI_MOD_CTRL) ? 'C' : '-',
			                                           (in->mods & LRG_GUI_MOD_SHIFT) ? 'S' : '-',
			                                           (in->mods & LRG_GUI_MOD_ALT) ? 'A' : '-');
			gfloat             lw = lrg_gui_calc_text_width (self->gui, line);

			lrg_gui_draw_list_text (dl, rect->x + rect->w - lw - pad * 2.0f,
			                        rect->y + pad * 1.5f + style->font_size + 4.0f, line, -1,
			                        style->font_size, style->colors[LRG_GUI_COL_TEXT_DISABLED], FALSE);
		}
	}

	/* Orientation gizmo, bottom left. */
	nav_gizmo_draw (self, rect);

	/* Play mode frame. */
	if (self->playing)
	{
		lrg_gui_draw_list_rect_outline (dl, rect, style->colors[LRG_GUI_COL_SUCCESS], 2.0f, 0.0f);
		lrg_gui_draw_list_text (dl, rect->x + rect->w * 0.5f - 160.0f, rect->y + rect->h - 26.0f,
		                        self->paused ? "PAUSED - F7 resume, F10 step, Shift+F5 stop"
		                                     : "PLAYING - changes are discarded on stop (Shift+F5)",
		                        -1, style->font_size, style->colors[LRG_GUI_COL_SUCCESS], FALSE);
	}

	/* Empty-level hint. */
	if (editor_app_count_nodes (self) == 0)
	{
		const gchar *hint = "Empty level: right-click or use Create to add nodes, drag assets here";
		gfloat       tw = lrg_gui_calc_text_width (self->gui, hint);

		lrg_gui_draw_list_text (dl, rect->x + (rect->w - tw) * 0.5f, rect->y + rect->h * 0.5f, hint, -1,
		                        style->font_size, style->colors[LRG_GUI_COL_TEXT_DISABLED], FALSE);
	}
}

void
editor_panel_viewport (LrgEditorApp *self)
{
	LrgGuiStyle      *style = lrg_gui_context_get_style (self->gui);
	LrgGuiRect        rect;
	gfloat            mx, my, lx, ly, dx, dy;
	gboolean          hovered;
	gdouble           orbit_speed = 0.35;
	gboolean          invert_y = FALSE;
	const gchar      *dropped;
	g_autofree gchar *navigation = NULL;

	lrg_gui_get_region_rect (self->gui, &rect);
	self->viewport_rect = rect;
	self->viewport_visible = TRUE;
	self->view.width = MAX (rect.w, 1.0f);
	self->view.height = MAX (rect.h, 1.0f);

	g_object_get (self->settings, "camera-orbit-speed", &orbit_speed, "camera-invert-y", &invert_y,
	              "camera-navigation", &navigation, NULL);

	/* The 3D image (rendered before the UI pass) goes under everything. */
	if (!self->headless)
		lrg_gui_draw_callback (self->gui, &rect, editor_render_blit, self);

	overlay_controls (self, &rect);

	/* One interactive surface covering the panel. */
	lrg_gui_set_cursor (self->gui, rect.x, rect.y);
	lrg_gui_invisible_button (self->gui, "##viewport", rect.w, rect.h);
	hovered = lrg_gui_is_item_hovered (self->gui) || lrg_gui_is_item_active (self->gui);
	self->viewport_hovered = hovered;

	lrg_gui_get_mouse (self->gui, &mx, &my);
	lrg_gui_get_mouse_delta (self->gui, &dx, &dy);
	lx = mx - rect.x;
	ly = my - rect.y;

	/* --- Orientation gizmo -------------------------------------------- */
	if (self->nav == EDITOR_NAV_NONE && !lrg_editor_gizmo_is_dragging (self->gizmo))
	{
		gint hit = hovered ? nav_gizmo_hit (self, &rect, mx, my) : -1;

		self->axis_hot = (hit >= 0) ? hit : (hit == -2 ? 6 : -1);
		if (hit != -1 && lrg_gui_is_mouse_clicked (self->gui, LRG_GUI_MOUSE_LEFT))
		{
			self->nav = EDITOR_NAV_GIZMO;
			self->nav_button = LRG_GUI_MOUSE_LEFT;
			self->nav_moved = FALSE;
			self->nav_axis = hit;
			self->press_x = mx;
			self->press_y = my;
			dx = dy = 0.0f;
		}
	}

	/* --- Navigation start ------------------------------------------------ */
	if (hovered && self->nav == EDITOR_NAV_NONE && !lrg_editor_gizmo_is_dragging (self->gizmo))
	{
		LrgGuiModifiers   mods = lrg_gui_get_mods (self->gui);
		EditorNav         start = EDITOR_NAV_NONE;
		LrgGuiMouseButton button = LRG_GUI_MOUSE_LEFT;

		if (lrg_gui_is_mouse_clicked (self->gui, LRG_GUI_MOUSE_RIGHT))
		{
			/* Fly look (pan in 2D); a click without motion opens the menu. */
			start = self->view_2d ? EDITOR_NAV_PAN : EDITOR_NAV_FLY;
			button = LRG_GUI_MOUSE_RIGHT;
		}
		else if (lrg_gui_is_mouse_clicked (self->gui, LRG_GUI_MOUSE_MIDDLE))
		{
			button = LRG_GUI_MOUSE_MIDDLE;
			if (g_strcmp0 (navigation, "blender") == 0 && !self->view_2d)
				start = (mods & LRG_GUI_MOD_SHIFT) ? EDITOR_NAV_PAN
				        : (mods & LRG_GUI_MOD_CTRL) ? EDITOR_NAV_ZOOM : EDITOR_NAV_ORBIT;
			else
				start = EDITOR_NAV_PAN;
		}
		else if (lrg_gui_is_mouse_clicked (self->gui, LRG_GUI_MOUSE_LEFT) && (mods & LRG_GUI_MOD_ALT))
		{
			/* Alt + left: orbit (pan in 2D); Alt + Shift + left pans. */
			start = (self->view_2d || (mods & LRG_GUI_MOD_SHIFT)) ? EDITOR_NAV_PAN : EDITOR_NAV_ORBIT;
			button = LRG_GUI_MOUSE_LEFT;
		}

		if (start != EDITOR_NAV_NONE)
		{
			self->nav = start;
			self->nav_button = button;
			self->nav_moved = FALSE;
			self->press_x = mx;
			self->press_y = my;
			/* Motion before the press is not part of the drag. */
			dx = dy = 0.0f;
		}
	}

	/* --- Navigation update ---------------------------------------------- */
	if (self->nav != EDITOR_NAV_NONE && self->nav != EDITOR_NAV_BOX)
	{
		if (fabsf (mx - self->press_x) >= CLICK_SLOP || fabsf (my - self->press_y) >= CLICK_SLOP)
			self->nav_moved = TRUE;

		if (!lrg_gui_is_mouse_down (self->gui, self->nav_button))
		{
			/* Released: a right click that never moved opens the menu, a
			 * click on a gizmo axis looks along it. */
			if (!self->nav_moved && self->nav_button == LRG_GUI_MOUSE_RIGHT)
				lrg_gui_open_popup (self->gui, "##viewport-menu");
			if (!self->nav_moved && self->nav == EDITOR_NAV_GIZMO && self->nav_axis >= 0)
			{
				gfloat sign = (self->nav_axis < 3) ? 1.0f : -1.0f;
				gint   axis = self->nav_axis % 3;

				editor_view_align (self, axis == 0 ? sign : 0.0f, axis == 1 ? sign : 0.0f,
				                   axis == 2 ? sign : 0.0f);
			}
			self->nav = EDITOR_NAV_NONE;
			self->nav_axis = -1;
		}
	}

	switch (self->nav)
	{
	case EDITOR_NAV_FLY:
		{
			gfloat dt = lrg_gui_get_delta_time (self->gui);
			gfloat speed = self->fly_speed * dt * ((lrg_gui_get_mods (self->gui) & LRG_GUI_MOD_SHIFT) ? FLY_FAST : 1.0f);
			gfloat fwd = 0.0f, right = 0.0f, up = 0.0f;
			gfloat sens = (gfloat) orbit_speed * DEG2RAD;

			if (lrg_gui_is_key_down (self->gui, LRG_GUI_KEY_W)) fwd += speed;
			if (lrg_gui_is_key_down (self->gui, LRG_GUI_KEY_S)) fwd -= speed;
			if (lrg_gui_is_key_down (self->gui, LRG_GUI_KEY_D)) right += speed;
			if (lrg_gui_is_key_down (self->gui, LRG_GUI_KEY_A)) right -= speed;
			if (lrg_gui_is_key_down (self->gui, LRG_GUI_KEY_E)) up += speed;
			if (lrg_gui_is_key_down (self->gui, LRG_GUI_KEY_Q)) up -= speed;

			if (self->ortho_from_align && (dx != 0.0f || dy != 0.0f || fwd != 0.0f || right != 0.0f))
			{
				self->view.orthographic = FALSE;
				self->ortho_from_align = FALSE;
			}
			lrg_editor_view_fly (&self->view, fwd, right, up, dx * sens, (invert_y ? -dy : dy) * sens);

			/* The wheel tunes the fly speed while flying. */
			if (lrg_gui_get_mouse_wheel (self->gui) != 0.0f)
				self->fly_speed = CLAMP (self->fly_speed * powf (1.2f, lrg_gui_get_mouse_wheel (self->gui)),
				                         0.1f, 500.0f);
			lrg_gui_set_cursor_shape (self->gui, LRG_GUI_CURSOR_RESIZE_ALL);
		}
		break;

	case EDITOR_NAV_ORBIT:
	case EDITOR_NAV_GIZMO:
		if (self->nav_moved)
			editor_view_orbit (self, -dx * (gfloat) orbit_speed * DEG2RAD,
			                   (invert_y ? -dy : dy) * (gfloat) orbit_speed * DEG2RAD);
		lrg_gui_set_cursor_shape (self->gui, LRG_GUI_CURSOR_RESIZE_ALL);
		break;

	case EDITOR_NAV_PAN:
		lrg_editor_view_pan (&self->view, dx, dy);
		lrg_gui_set_cursor_shape (self->gui, LRG_GUI_CURSOR_RESIZE_ALL);
		break;

	case EDITOR_NAV_ZOOM:
		if (dy != 0.0f)
			lrg_editor_view_dolly (&self->view, powf (1.01f, dy));
		lrg_gui_set_cursor_shape (self->gui, LRG_GUI_CURSOR_RESIZE_NS);
		break;

	default:
		break;
	}

	/* Wheel: zoom; in the trackpad scheme two-finger scroll orbits, Shift
	 * pans and Ctrl zooms. */
	if (hovered && self->nav != EDITOR_NAV_FLY && self->cur_input != NULL &&
	    (self->cur_input->wheel_x != 0.0f || self->cur_input->wheel_y != 0.0f))
	{
		gfloat          wx = self->cur_input->wheel_x;
		gfloat          wy = self->cur_input->wheel_y;
		LrgGuiModifiers mods = lrg_gui_get_mods (self->gui);

		if (g_strcmp0 (navigation, "trackpad") == 0 && !(mods & LRG_GUI_MOD_CTRL))
		{
			if ((mods & LRG_GUI_MOD_SHIFT) || self->view_2d)
				lrg_editor_view_pan (&self->view, wx * 12.0f, wy * 12.0f);
			else
				editor_view_orbit (self, wx * 4.0f * DEG2RAD, wy * 4.0f * DEG2RAD);
		}
		else if (wy != 0.0f)
			lrg_editor_view_dolly (&self->view, powf (0.87f, wy));
	}

	/* --- Gizmo and selection -------------------------------------------- */
	if (self->nav == EDITOR_NAV_NONE || self->nav == EDITOR_NAV_BOX)
	{
		if (!lrg_editor_gizmo_is_dragging (self->gizmo) && hovered)
			lrg_editor_gizmo_update_hover (self->gizmo, &self->view, lx, ly);

		if (hovered && lrg_gui_is_mouse_clicked (self->gui, LRG_GUI_MOUSE_LEFT) &&
		    !(lrg_gui_get_mods (self->gui) & LRG_GUI_MOD_ALT))
		{
			if (!lrg_editor_gizmo_begin_drag (self->gizmo, &self->view, lx, ly))
			{
				self->press_on_empty = TRUE;
				self->press_x = mx;
				self->press_y = my;
			}
		}

		if (lrg_editor_gizmo_is_dragging (self->gizmo))
		{
			if (lrg_gui_is_key_pressed (self->gui, LRG_GUI_KEY_ESCAPE, FALSE) ||
			    lrg_gui_is_mouse_clicked (self->gui, LRG_GUI_MOUSE_RIGHT))
			{
				lrg_editor_gizmo_cancel_drag (self->gizmo);
				editor_app_set_status (self, "Transform cancelled");
			}
			else if (lrg_gui_is_mouse_down (self->gui, LRG_GUI_MOUSE_LEFT))
				lrg_editor_gizmo_drag (self->gizmo, &self->view, lx, ly,
				                       (lrg_gui_get_mods (self->gui) & LRG_GUI_MOD_CTRL) != 0);
			else
				lrg_editor_gizmo_end_drag (self->gizmo);
		}
		else if (self->press_on_empty)
		{
			if (lrg_gui_is_mouse_down (self->gui, LRG_GUI_MOUSE_LEFT))
			{
				if (fabsf (mx - self->press_x) > CLICK_SLOP || fabsf (my - self->press_y) > CLICK_SLOP)
				{
					self->nav = EDITOR_NAV_BOX;
					self->box_x0 = self->press_x - rect.x;
					self->box_y0 = self->press_y - rect.y;
				}
			}
			else
			{
				if (self->nav == EDITOR_NAV_BOX)
				{
					g_autoptr(GPtrArray) boxed = lrg_editor_box_select (lrg_editor_get_level (self->editor),
					                                                    &self->view, self->box_x0, self->box_y0,
					                                                    lx, ly, FALSE);

					lrg_editor_select_nodes (self->editor, boxed,
					                         (lrg_gui_get_mods (self->gui) & LRG_GUI_MOD_SHIFT) != 0);
					editor_app_set_status (self, "Box selected %u node(s)", boxed->len);
					self->nav = EDITOR_NAV_NONE;
				}
				else
				{
					click_pick (self, lx, ly);
				}
				self->press_on_empty = FALSE;
			}
		}
	}

	/* Box selection rectangle. */
	if (self->nav == EDITOR_NAV_BOX)
	{
		LrgGuiRect box = { rect.x + MIN (self->box_x0, lx), rect.y + MIN (self->box_y0, ly),
		                   fabsf (lx - self->box_x0), fabsf (ly - self->box_y0) };

		lrg_gui_draw_list_rect_filled (lrg_gui_get_draw_list (self->gui), &box,
		                               (style->colors[LRG_GUI_COL_ACCENT] & 0xffffff00u) | 0x30u, 0.0f);
		lrg_gui_draw_list_rect_outline (lrg_gui_get_draw_list (self->gui), &box,
		                                style->colors[LRG_GUI_COL_ACCENT], 1.0f, 0.0f);
	}

	/* Gizmo readout while dragging. */
	if (lrg_editor_gizmo_is_dragging (self->gizmo))
	{
		gfloat           d[3];
		g_autofree gchar *text = NULL;

		lrg_editor_gizmo_get_drag_delta (self->gizmo, d);
		switch (lrg_editor_get_tool (self->editor))
		{
		case LRG_EDITOR_TOOL_ROTATE:
			text = g_strdup_printf ("%.1f deg", d[0] / DEG2RAD);
			break;
		case LRG_EDITOR_TOOL_SCALE:
			text = g_strdup_printf ("x%.3f  y%.3f  z%.3f", d[0], d[1], d[2]);
			break;
		default:
			text = g_strdup_printf ("dx %.3f  dy %.3f  dz %.3f", d[0], d[1], d[2]);
			break;
		}
		lrg_gui_draw_list_text (lrg_gui_get_draw_list (self->gui), mx + 16.0f, my + 12.0f, text, -1,
		                        style->font_size, style->colors[LRG_GUI_COL_TEXT], TRUE);
	}

	/* Assets dropped into the scene. */
	lrg_gui_set_cursor (self->gui, rect.x, rect.y);
	lrg_gui_dummy (self->gui, rect.w, rect.h);
	if ((dropped = lrg_gui_accept_drop (self->gui, "asset")) != NULL)
	{
		g_autofree gchar *copy = g_strdup (dropped);

		editor_viewport_drop_asset (self, copy, lx, ly);
	}

	if (lrg_gui_begin_popup (self->gui, "##viewport-menu"))
	{
		viewport_context_menu (self);
		lrg_gui_end_popup (self->gui);
	}

	overlays (self, &rect);
}
