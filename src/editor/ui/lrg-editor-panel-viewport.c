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
		                                          lrg_level_count_nodes (lrg_editor_get_level (self->editor)),
		                                          lrg_editor_selection_get_count (lrg_editor_get_selection (self->editor)),
		                                          self->fps);
		gfloat            tw = lrg_gui_calc_text_width (self->gui, text);

		lrg_gui_draw_list_text (dl, rect->x + rect->w - tw - pad * 2.0f, rect->y + pad * 1.5f, text, -1,
		                        style->font_size, style->colors[LRG_GUI_COL_TEXT_DISABLED], FALSE);
	}

	/* Orientation axes, bottom left. */
	{
		gfloat       cx = rect->x + 40.0f, cy = rect->y + rect->h - 40.0f, len = 26.0f;
		gfloat       view[16];
		const gchar *names[3] = { "X", "Y", "Z" };
		gint         i;

		lrg_editor_view_get_view_matrix (&self->view, view);
		for (i = 0; i < 3; i++)
		{
			/* Column i of the rotation = world axis i in camera space. */
			gfloat ax = view[i * 4 + 0];
			gfloat ay = view[i * 4 + 1];

			lrg_gui_draw_list_line (dl, cx, cy, cx + ax * len, cy - ay * len,
			                        style->colors[LRG_GUI_COL_AXIS_X + i], 2.0f);
			lrg_gui_draw_list_text (dl, cx + ax * (len + 6.0f) - 4.0f, cy - ay * (len + 6.0f) - 7.0f,
			                        names[i], -1, style->font_size * 0.85f,
			                        style->colors[LRG_GUI_COL_AXIS_X + i], FALSE);
		}
	}

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
	if (lrg_level_count_nodes (lrg_editor_get_level (self->editor)) == 0)
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
	LrgGuiStyle *style = lrg_gui_context_get_style (self->gui);
	LrgGuiRect   rect;
	gfloat       mx, my, lx, ly, dx, dy;
	gboolean     hovered;
	gdouble      orbit_speed = 0.35;
	gboolean     invert_y = FALSE;
	const gchar *dropped;

	lrg_gui_get_region_rect (self->gui, &rect);
	self->viewport_rect = rect;
	self->viewport_visible = TRUE;
	self->view.width = MAX (rect.w, 1.0f);
	self->view.height = MAX (rect.h, 1.0f);

	g_object_get (self->settings, "camera-orbit-speed", &orbit_speed, "camera-invert-y", &invert_y, NULL);

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

	/* --- Navigation start ------------------------------------------------ */
	if (hovered && self->nav == EDITOR_NAV_NONE && !lrg_editor_gizmo_is_dragging (self->gizmo))
	{
		if (lrg_gui_is_mouse_clicked (self->gui, LRG_GUI_MOUSE_RIGHT))
		{
			self->nav = self->view_2d ? EDITOR_NAV_PAN : EDITOR_NAV_FLY;
			self->press_x = mx;
			self->press_y = my;
		}
		else if (lrg_gui_is_mouse_clicked (self->gui, LRG_GUI_MOUSE_MIDDLE))
			self->nav = EDITOR_NAV_PAN;
		else if (lrg_gui_is_mouse_clicked (self->gui, LRG_GUI_MOUSE_LEFT) &&
		         (lrg_gui_get_mods (self->gui) & LRG_GUI_MOD_ALT) && !self->view_2d)
			self->nav = EDITOR_NAV_ORBIT;
	}

	/* --- Navigation update ---------------------------------------------- */
	switch (self->nav)
	{
	case EDITOR_NAV_FLY:
		if (!lrg_gui_is_mouse_down (self->gui, LRG_GUI_MOUSE_RIGHT))
		{
			/* A right click without motion opens the context menu. */
			if (fabsf (mx - self->press_x) < CLICK_SLOP && fabsf (my - self->press_y) < CLICK_SLOP)
				lrg_gui_open_popup (self->gui, "##viewport-menu");
			self->nav = EDITOR_NAV_NONE;
			break;
		}
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

			lrg_editor_view_fly (&self->view, fwd, right, up, dx * sens, (invert_y ? -dy : dy) * sens);

			/* The wheel tunes the fly speed while flying. */
			if (lrg_gui_get_mouse_wheel (self->gui) != 0.0f)
				self->fly_speed = CLAMP (self->fly_speed * powf (1.2f, lrg_gui_get_mouse_wheel (self->gui)),
				                         0.1f, 500.0f);
			lrg_gui_set_cursor_shape (self->gui, LRG_GUI_CURSOR_RESIZE_ALL);
		}
		break;

	case EDITOR_NAV_ORBIT:
		if (!lrg_gui_is_mouse_down (self->gui, LRG_GUI_MOUSE_LEFT))
		{
			self->nav = EDITOR_NAV_NONE;
			break;
		}
		lrg_editor_view_orbit (&self->view, -dx * (gfloat) orbit_speed * DEG2RAD,
		                       (invert_y ? -dy : dy) * (gfloat) orbit_speed * DEG2RAD);
		break;

	case EDITOR_NAV_PAN:
		if (!lrg_gui_is_mouse_down (self->gui, LRG_GUI_MOUSE_MIDDLE) &&
		    !lrg_gui_is_mouse_down (self->gui, LRG_GUI_MOUSE_RIGHT))
		{
			if (self->view_2d && fabsf (mx - self->press_x) < CLICK_SLOP &&
			    fabsf (my - self->press_y) < CLICK_SLOP)
				lrg_gui_open_popup (self->gui, "##viewport-menu");
			self->nav = EDITOR_NAV_NONE;
			break;
		}
		lrg_editor_view_pan (&self->view, dx, dy);
		break;

	default:
		break;
	}

	/* Zoom. */
	if (hovered && self->nav != EDITOR_NAV_FLY && lrg_gui_get_mouse_wheel (self->gui) != 0.0f)
		lrg_editor_view_dolly (&self->view, powf (0.87f, lrg_gui_get_mouse_wheel (self->gui)));

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
