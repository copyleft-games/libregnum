/* lrg-editor-panel-inspector.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * The Inspector panel: everything about the selected node(s) — name,
 * flags, transform, the visual payload (kind, primitive, asset, material,
 * kind-specific parameters), component descriptions with editors
 * reflected from the registered component's GObject properties, script
 * bindings (open in the external editor, attach, detach), and prefab
 * origin. With nothing selected it shows the level's own properties.
 *
 * Every edit goes through the editor's undoable operations. Drags merge
 * into one undo step: the first change of a drag starts a step, the rest
 * of the drag folds into it.
 */

#include "lrg-editor-app-private.h"
#include "../../core/lrg-engine.h"
#include "../../core/lrg-registry.h"
#include "../../ecs/lrg-component.h"
#include <math.h>
#include <string.h>

#define RAD2DEG (180.0f / (gfloat) G_PI)
#define DEG2RAD ((gfloat) G_PI / 180.0f)

/* ==========================================================================
 * Helpers
 * ========================================================================== */

/* Whether this edit continues the current drag (merge into one step). */
static gboolean
merging (LrgEditorApp *self)
{
	gboolean merge = self->inspector_dragging && lrg_gui_is_mouse_down (self->gui, LRG_GUI_MOUSE_LEFT);

	self->inspector_dragging = lrg_gui_is_mouse_down (self->gui, LRG_GUI_MOUSE_LEFT);
	return merge;
}

static void
section_gap (LrgEditorApp *self)
{
	lrg_gui_spacing (self->gui);
}

static LrgRegistry *
registry (void)
{
	LrgEngine *engine = lrg_engine_get_default ();

	return (engine != NULL) ? lrg_engine_get_registry (engine) : NULL;
}

/*
 * edit_value:
 *
 * Draws an editor for a value of @type (current value @current, which may
 * be NULL = unset) and stores a new value in @out when it changes.
 * @pspec (nullable) supplies ranges and enum types.
 */
static gboolean
edit_value (LrgEditorApp *self,
            const gchar  *label,
            GType         type,
            const GValue *current,
            GParamSpec   *pspec,
            GValue       *out)
{
	GType fundamental = G_TYPE_FUNDAMENTAL (type);
	GValue cur = G_VALUE_INIT;
	gboolean changed = FALSE;
	g_autofree gchar *id = g_strdup_printf ("##%s", label);

	/* Start from the current value, the pspec default, or zero. */
	g_value_init (&cur, type);
	if (current != NULL && !g_value_transform (current, &cur) && G_VALUE_TYPE (current) == type)
		g_value_copy (current, &cur);
	else if (current == NULL && pspec != NULL)
		g_value_copy (g_param_spec_get_default_value (pspec), &cur);

	lrg_gui_property_label (self->gui, label);

	switch (fundamental)
	{
	case G_TYPE_BOOLEAN:
		{
			gboolean b = g_value_get_boolean (&cur);

			if (lrg_gui_checkbox (self->gui, id, &b))
			{
				g_value_init (out, G_TYPE_BOOLEAN);
				g_value_set_boolean (out, b);
				changed = TRUE;
			}
		}
		break;

	case G_TYPE_INT:
	case G_TYPE_UINT:
	case G_TYPE_INT64:
	case G_TYPE_UINT64:
	case G_TYPE_LONG:
	case G_TYPE_ULONG:
		{
			GValue as_int = G_VALUE_INIT;
			gint   v, lo = 0, hi = 0;

			g_value_init (&as_int, G_TYPE_INT);
			g_value_transform (&cur, &as_int);
			v = g_value_get_int (&as_int);
			g_value_unset (&as_int);
			if (pspec != NULL && G_IS_PARAM_SPEC_INT (pspec))
			{
				lo = G_PARAM_SPEC_INT (pspec)->minimum;
				hi = G_PARAM_SPEC_INT (pspec)->maximum;
			}
			else if (pspec != NULL && G_IS_PARAM_SPEC_UINT (pspec))
			{
				lo = 0;
				hi = (gint) MIN (G_PARAM_SPEC_UINT (pspec)->maximum, (guint) G_MAXINT);
			}
			if (lrg_gui_drag_int (self->gui, id, &v, 0.2f, lo, hi))
			{
				GValue tmp = G_VALUE_INIT;

				g_value_init (&tmp, G_TYPE_INT);
				g_value_set_int (&tmp, v);
				g_value_init (out, (current != NULL && G_VALUE_TYPE (current) == G_TYPE_INT64 && pspec == NULL)
				                   ? G_TYPE_INT64 : type);
				g_value_transform (&tmp, out);
				g_value_unset (&tmp);
				changed = TRUE;
			}
		}
		break;

	case G_TYPE_FLOAT:
	case G_TYPE_DOUBLE:
		{
			GValue as_d = G_VALUE_INIT;
			gfloat v, lo = 0.0f, hi = 0.0f;

			g_value_init (&as_d, G_TYPE_DOUBLE);
			g_value_transform (&cur, &as_d);
			v = (gfloat) g_value_get_double (&as_d);
			g_value_unset (&as_d);
			if (pspec != NULL && G_IS_PARAM_SPEC_DOUBLE (pspec) &&
			    G_PARAM_SPEC_DOUBLE (pspec)->maximum - G_PARAM_SPEC_DOUBLE (pspec)->minimum < 1e9)
			{
				lo = (gfloat) G_PARAM_SPEC_DOUBLE (pspec)->minimum;
				hi = (gfloat) G_PARAM_SPEC_DOUBLE (pspec)->maximum;
			}
			else if (pspec != NULL && G_IS_PARAM_SPEC_FLOAT (pspec) &&
			         G_PARAM_SPEC_FLOAT (pspec)->maximum - G_PARAM_SPEC_FLOAT (pspec)->minimum < 1e9f)
			{
				lo = G_PARAM_SPEC_FLOAT (pspec)->minimum;
				hi = G_PARAM_SPEC_FLOAT (pspec)->maximum;
			}
			if (lrg_gui_drag_float (self->gui, id, &v, 0.01f, lo, hi, "%.3f"))
			{
				GValue tmp = G_VALUE_INIT;

				g_value_init (&tmp, G_TYPE_DOUBLE);
				g_value_set_double (&tmp, v);
				g_value_init (out, type);
				g_value_transform (&tmp, out);
				g_value_unset (&tmp);
				changed = TRUE;
			}
		}
		break;

	case G_TYPE_ENUM:
		{
			g_autoptr(GEnumClass) klass = g_type_class_ref (type);
			const gchar         **names = g_new0 (const gchar *, klass->n_values + 1);
			gint                  current_index = 0;
			guint                 i;

			for (i = 0; i < klass->n_values; i++)
			{
				names[i] = klass->values[i].value_nick;
				if (klass->values[i].value == g_value_get_enum (&cur))
					current_index = (gint) i;
			}
			if (lrg_gui_combo (self->gui, id, &current_index, names, (gint) klass->n_values))
			{
				g_value_init (out, type);
				g_value_set_enum (out, klass->values[current_index].value);
				changed = TRUE;
			}
			g_free (names);
		}
		break;

	case G_TYPE_STRING:
		{
			g_autoptr(GString) buf = g_string_new (g_value_get_string (&cur));

			if (lrg_gui_input_text (self->gui, id, NULL, buf, LRG_GUI_INPUT_TEXT_ENTER_RETURNS_TRUE))
			{
				g_value_init (out, G_TYPE_STRING);
				g_value_set_string (out, buf->str);
				changed = TRUE;
			}
		}
		break;

	default:
		{
			g_autofree gchar *contents = g_strdup_value_contents (&cur);

			lrg_gui_text_disabled (self->gui, contents);
		}
		break;
	}

	g_value_unset (&cur);
	return changed;
}

/* Applies a boolean property to every selected node in one step. */
static void
set_flag_all (LrgEditorApp *self,
              const gchar  *prop,
              gboolean      value)
{
	GPtrArray *nodes = lrg_editor_selection_get_nodes (lrg_editor_get_selection (self->editor));
	guint      i;

	lrg_editor_begin_group (self->editor, NULL);
	for (i = 0; i < nodes->len; i++)
	{
		GValue v = G_VALUE_INIT;

		g_value_init (&v, G_TYPE_BOOLEAN);
		g_value_set_boolean (&v, value);
		lrg_editor_set_object_property (self->editor, g_ptr_array_index (nodes, i), prop, &v, FALSE);
		g_value_unset (&v);
	}
	lrg_editor_end_group (self->editor);
}

/* ==========================================================================
 * Sections
 * ========================================================================== */

static void
header_section (LrgEditorApp *self,
                LrgNode      *node,
                guint         count)
{
	LrgGuiStyle        *style = lrg_gui_context_get_style (self->gui);
	g_autoptr(GString)  name = g_string_new (lrg_node_get_name (node));
	gboolean            visible = lrg_node_get_visible (node);
	gboolean            locked = lrg_node_get_locked (node);
	gboolean            is_2d = lrg_node_get_is_2d (node);
	g_autofree gchar   *path = lrg_node_get_path (node);

	if (count > 1)
	{
		g_autofree gchar *multi = g_strdup_printf ("%u nodes selected; flag and transform edits apply to all",
		                                           count);

		lrg_gui_text_colored (self->gui, style->colors[LRG_GUI_COL_WARNING], multi);
	}

	lrg_gui_property_label (self->gui, "Name");
	if (lrg_gui_input_text (self->gui, "##name", "name", name, LRG_GUI_INPUT_TEXT_ENTER_RETURNS_TRUE))
		lrg_editor_rename_node (self->editor, node, name->str);

	lrg_gui_property_label (self->gui, "Flags");
	if (lrg_gui_checkbox (self->gui, "Visible", &visible))
		set_flag_all (self, "visible", visible);
	lrg_gui_same_line (self->gui, -1.0f);
	if (lrg_gui_checkbox (self->gui, "Locked", &locked))
		set_flag_all (self, "locked", locked);
	lrg_gui_same_line (self->gui, -1.0f);
	if (lrg_gui_checkbox (self->gui, "2D", &is_2d))
		set_flag_all (self, "is-2d", is_2d);

	lrg_gui_property_label (self->gui, "Path");
	lrg_gui_text_disabled (self->gui, path);

	lrg_gui_property_label (self->gui, "GUID");
	lrg_gui_text_mono (self->gui, style->colors[LRG_GUI_COL_TEXT_DISABLED], lrg_node_get_guid (node));
	if (lrg_gui_is_item_double_clicked (self->gui))
	{
		lrg_gui_context_set_clipboard (self->gui, lrg_node_get_guid (node));
		editor_app_set_status (self, "Copied GUID");
	}
	lrg_gui_set_item_tooltip (self->gui, "Double-click to copy");
}

/* Transform rows; edits of one channel apply that channel to every
 * selected node. */
static void
transform_row (LrgEditorApp *self,
               LrgNode      *node,
               const gchar  *label,
               gint          channel)   /* 0 loc, 1 rot (deg), 2 scale */
{
	GrlVector3 *src = (channel == 0) ? lrg_node_get_location (node)
	                 : (channel == 1) ? lrg_node_get_rotation (node) : lrg_node_get_scale (node);
	gfloat      v[3] = { src->x, src->y, src->z };
	gfloat      before[3];
	GPtrArray  *nodes = lrg_editor_selection_get_nodes (lrg_editor_get_selection (self->editor));
	g_autofree gchar *id = g_strdup_printf ("##%s", label);

	if (channel == 1)
	{
		v[0] *= RAD2DEG;
		v[1] *= RAD2DEG;
		v[2] *= RAD2DEG;
	}
	memcpy (before, v, sizeof before);

	lrg_gui_property_label (self->gui, label);
	if (lrg_gui_drag_float3 (self->gui, id, v, channel == 1 ? 0.5f : 0.02f,
	                         channel == 1 ? "%.1f" : "%.3f"))
	{
		gboolean merge = merging (self);
		guint    i;
		gint     k;

		lrg_editor_begin_group (self->editor, NULL);
		for (i = 0; i < nodes->len; i++)
		{
			LrgNode    *n = g_ptr_array_index (nodes, i);
			GrlVector3 *cur = (channel == 0) ? lrg_node_get_location (n)
			                 : (channel == 1) ? lrg_node_get_rotation (n) : lrg_node_get_scale (n);
			gfloat      out[3] = { cur->x, cur->y, cur->z };

			/* Only the edited components change on the other nodes. */
			for (k = 0; k < 3; k++)
				if (v[k] != before[k] || n == node)
					out[k] = (channel == 1) ? v[k] * DEG2RAD : v[k];

			lrg_editor_transform_node (self->editor, n,
			                           channel == 0 ? out : NULL,
			                           channel == 1 ? out : NULL,
			                           channel == 2 ? out : NULL,
			                           merge && nodes->len == 1);
		}
		lrg_editor_end_group (self->editor);
	}

	/* Right-click: reset this channel. */
	if (lrg_gui_begin_popup_context_item (self->gui, NULL))
	{
		g_autofree gchar *reset = g_strdup_printf ("Reset %s", label);

		if (lrg_gui_menu_item (self->gui, reset, NULL, FALSE, TRUE))
		{
			const gfloat zero[3] = { 0, 0, 0 }, one[3] = { 1, 1, 1 };
			guint        i;

			lrg_editor_begin_group (self->editor, reset);
			for (i = 0; i < nodes->len; i++)
				lrg_editor_transform_node (self->editor, g_ptr_array_index (nodes, i),
				                           channel == 0 ? zero : NULL, channel == 1 ? zero : NULL,
				                           channel == 2 ? one : NULL, FALSE);
			lrg_editor_end_group (self->editor);
		}
		if (lrg_gui_menu_item (self->gui, "Copy", NULL, FALSE, TRUE))
		{
			g_autofree gchar *txt = g_strdup_printf ("%g, %g, %g", v[0], v[1], v[2]);

			lrg_gui_context_set_clipboard (self->gui, txt);
		}
		lrg_gui_end_popup (self->gui);
	}
}

static void
transform_section (LrgEditorApp *self,
                   LrgNode      *node)
{
	if (!lrg_gui_collapsing_header (self->gui, "Transform", TRUE))
		return;

	transform_row (self, node, "Location", 0);
	transform_row (self, node, "Rotation", 1);
	transform_row (self, node, "Scale", 2);

	/* World position for reference. */
	{
		gfloat           w[3];
		g_autofree gchar *txt = NULL;

		lrg_editor_node_get_world_position (node, w);
		txt = g_strdup_printf ("%.3f, %.3f, %.3f", w[0], w[1], w[2]);
		lrg_gui_property_label (self->gui, "World");
		lrg_gui_text_disabled (self->gui, txt);
	}
	section_gap (self);
}

static const gchar *kind_names[] = {
	"None (group)", "Primitive", "Mesh asset", "Sprite", "Tilemap", "Light", "Camera",
	"Audio emitter", "Prefab instance", "CAD part"
};

static const gchar *primitive_names[] = {
	"Plane", "Cube", "Circle", "UV sphere", "Ico sphere", "Cylinder", "Cone", "Torus", "Grid",
	"Mesh", "Rectangle 2D", "Circle 2D"
};

/* Suggested parameters per kind: name, default. */
static const struct
{
	LrgNodeVisualKind kind;
	const gchar      *param;
	gdouble           value;
} suggested[] = {
	{ LRG_NODE_VISUAL_LIGHT, "intensity", 1.0 },
	{ LRG_NODE_VISUAL_LIGHT, "range", 10.0 },
	{ LRG_NODE_VISUAL_LIGHT, "spot_angle", 0.0 },
	{ LRG_NODE_VISUAL_CAMERA, "fov", 60.0 },
	{ LRG_NODE_VISUAL_CAMERA, "near", 0.1 },
	{ LRG_NODE_VISUAL_CAMERA, "far", 1000.0 },
	{ LRG_NODE_VISUAL_AUDIO_EMITTER, "volume", 1.0 },
	{ LRG_NODE_VISUAL_AUDIO_EMITTER, "radius", 10.0 },
	{ LRG_NODE_VISUAL_SPRITE, "frame", 0.0 },
	{ LRG_NODE_VISUAL_SPRITE, "pixels_per_unit", 32.0 },
	{ LRG_NODE_VISUAL_TILEMAP, "tile_size", 1.0 },
};

static gboolean
kind_has_asset (LrgNodeVisualKind kind)
{
	return kind == LRG_NODE_VISUAL_MESH_ASSET || kind == LRG_NODE_VISUAL_SPRITE ||
	       kind == LRG_NODE_VISUAL_TILEMAP || kind == LRG_NODE_VISUAL_PREFAB_INSTANCE ||
	       kind == LRG_NODE_VISUAL_CAD_PART || kind == LRG_NODE_VISUAL_AUDIO_EMITTER;
}

static void
visual_section (LrgEditorApp *self,
                LrgNode      *node)
{
	LrgNodeVisual     *visual = lrg_node_get_visual (node);
	gint               kind = (visual != NULL) ? (gint) lrg_node_visual_get_kind (visual) : 0;
	g_autoptr(LrgNode) before = NULL;
	gboolean           changed = FALSE;
	const gchar       *label = "Change Visual";

	if (!lrg_gui_collapsing_header (self->gui, "Visual", TRUE))
		return;

	before = lrg_editor_snapshot_node (node);

	lrg_gui_property_label (self->gui, "Kind");
	if (lrg_gui_combo (self->gui, "##kind", &kind, kind_names, G_N_ELEMENTS (kind_names)))
	{
		if (kind == LRG_NODE_VISUAL_NONE)
			lrg_node_set_visual (node, NULL);
		else
		{
			g_autoptr(LrgNodeVisual) v = (visual != NULL) ? lrg_node_visual_copy (visual)
			                                               : lrg_node_visual_new (kind);

			lrg_node_visual_set_kind (v, kind);
			if (kind == LRG_NODE_VISUAL_PRIMITIVE && lrg_node_visual_get_material (v) == NULL)
			{
				g_autoptr(LrgMaterial3D) m = lrg_material3d_new_with_color (0.75f, 0.75f, 0.78f, 1.0f);

				lrg_node_visual_set_material (v, m);
			}
			lrg_node_set_visual (node, v);
		}
		changed = TRUE;
		label = "Change Visual Kind";
		visual = lrg_node_get_visual (node);
	}

	if (visual != NULL && !changed)
	{
		LrgNodeVisualKind vk = lrg_node_visual_get_kind (visual);
		gboolean          merge = FALSE;

		/* Work on a copy so a cancelled edit leaves nothing behind. */
		g_autoptr(LrgNodeVisual) v = lrg_node_visual_copy (visual);

		if (vk == LRG_NODE_VISUAL_PRIMITIVE)
		{
			gint prim = (gint) lrg_node_visual_get_primitive (v);

			lrg_gui_property_label (self->gui, "Primitive");
			if (lrg_gui_combo (self->gui, "##primitive", &prim, primitive_names, G_N_ELEMENTS (primitive_names)))
			{
				lrg_node_visual_set_primitive (v, prim);
				changed = TRUE;
				label = "Change Primitive";
			}
		}

		if (kind_has_asset (vk))
		{
			g_autoptr(GString) asset = g_string_new (lrg_node_visual_get_asset (v));
			gfloat             w;
			const gchar       *dropped;

			lrg_gui_property_label (self->gui, "Asset");
			lrg_gui_get_content_avail (self->gui, &w, NULL);
			lrg_gui_set_next_item_width (self->gui, w - lrg_gui_get_frame_height (self->gui) * 2.0f -
			                             lrg_gui_context_get_style (self->gui)->item_spacing_x);
			if (lrg_gui_input_text (self->gui, "##asset", "path relative to the project", asset,
			                        LRG_GUI_INPUT_TEXT_ENTER_RETURNS_TRUE))
			{
				lrg_node_visual_set_asset (v, asset->len > 0 ? asset->str : NULL);
				changed = TRUE;
				label = "Set Asset";
			}
			if ((dropped = lrg_gui_accept_drop (self->gui, "asset")) != NULL)
			{
				lrg_node_visual_set_asset (v, dropped);
				changed = TRUE;
				label = "Set Asset";
			}
			lrg_gui_same_line (self->gui, -1.0f);
			if (lrg_gui_button_sized (self->gui, "...##pick-asset", lrg_gui_get_frame_height (self->gui), 0.0f))
			{
				g_autofree gchar *dir = (editor_app_base_dir (self) != NULL)
				                        ? g_strdup (editor_app_base_dir (self)) : g_get_current_dir ();

				editor_file_dialog_open (self, EDITOR_FILE_PICK_ASSET, dir, NULL);
				g_free (self->file_target_guid);
				self->file_target_guid = g_strdup (lrg_node_get_guid (node));
				self->file_target_slot = -1;
			}
			lrg_gui_set_item_tooltip (self->gui, "Pick a file");
			lrg_gui_same_line (self->gui, -1.0f);
			if (lrg_gui_button_sized (self->gui, ">##open-asset", lrg_gui_get_frame_height (self->gui), 0.0f) &&
			    lrg_node_visual_get_asset (v) != NULL)
				editor_app_open_asset (self, lrg_node_visual_get_asset (v));
			lrg_gui_set_item_tooltip (self->gui, "Open the asset");
		}

		/* Material for drawable kinds. */
		if (vk == LRG_NODE_VISUAL_PRIMITIVE || vk == LRG_NODE_VISUAL_MESH_ASSET ||
		    vk == LRG_NODE_VISUAL_SPRITE || vk == LRG_NODE_VISUAL_LIGHT)
		{
			LrgMaterial3D *m = lrg_node_visual_get_material (v);
			gfloat         rgba[4] = { 1, 1, 1, 1 };
			gfloat         rough, metal, estr;

			if (m == NULL)
			{
				g_autoptr(LrgMaterial3D) fresh = lrg_material3d_new ();

				lrg_node_visual_set_material (v, fresh);
				m = fresh;
			}
			lrg_material3d_get_color (m, &rgba[0], &rgba[1], &rgba[2], &rgba[3]);
			rough = lrg_material3d_get_roughness (m);
			metal = lrg_material3d_get_metallic (m);
			estr = lrg_material3d_get_emission_strength (m);

			lrg_gui_property_label (self->gui, vk == LRG_NODE_VISUAL_LIGHT ? "Light color" : "Color");
			if (lrg_gui_color_edit (self->gui, "##color", rgba, TRUE))
			{
				lrg_material3d_set_color (m, rgba[0], rgba[1], rgba[2], rgba[3]);
				changed = TRUE;
				merge = TRUE;
				label = "Set Color";
			}
			if (vk != LRG_NODE_VISUAL_LIGHT)
			{
				lrg_gui_property_label (self->gui, "Roughness");
				if (lrg_gui_slider_float (self->gui, "##rough", &rough, 0.0f, 1.0f, "%.2f"))
				{
					lrg_material3d_set_roughness (m, rough);
					changed = TRUE;
					merge = TRUE;
					label = "Set Roughness";
				}
				lrg_gui_property_label (self->gui, "Metallic");
				if (lrg_gui_slider_float (self->gui, "##metal", &metal, 0.0f, 1.0f, "%.2f"))
				{
					lrg_material3d_set_metallic (m, metal);
					changed = TRUE;
					merge = TRUE;
					label = "Set Metallic";
				}
				lrg_gui_property_label (self->gui, "Emission");
				if (lrg_gui_drag_float (self->gui, "##emission", &estr, 0.01f, 0.0f, 100.0f, "%.2f"))
				{
					lrg_material3d_set_emission_strength (m, estr);
					changed = TRUE;
					merge = TRUE;
					label = "Set Emission";
				}
			}
		}

		/* Parameter bag. */
		{
			GList *names = g_list_sort (lrg_node_visual_get_param_names (v), (GCompareFunc) g_strcmp0);
			GList *l;
			gsize  i;

			if (names != NULL)
				lrg_gui_separator_text (self->gui, "Parameters");
			for (l = names; l != NULL; l = l->next)
			{
				const gchar  *key = l->data;
				const GValue *val = lrg_node_visual_get_param (v, key);
				GValue        out = G_VALUE_INIT;

				lrg_gui_push_id (self->gui, key);
				if (val != NULL && edit_value (self, key, G_VALUE_TYPE (val), val, NULL, &out))
				{
					lrg_node_visual_set_param (v, key, &out);
					g_value_unset (&out);
					changed = TRUE;
					merge = TRUE;
					label = "Set Parameter";
				}
				if (lrg_gui_begin_popup_context_item (self->gui, NULL))
				{
					if (lrg_gui_menu_item (self->gui, "Remove parameter", NULL, FALSE, TRUE))
					{
						lrg_node_visual_remove_param (v, key);
						changed = TRUE;
						label = "Remove Parameter";
					}
					lrg_gui_end_popup (self->gui);
				}
				lrg_gui_pop_id (self->gui);
			}
			g_list_free (names);

			/* Quick-add the usual parameters for this kind. */
			for (i = 0; i < G_N_ELEMENTS (suggested); i++)
			{
				if (suggested[i].kind != vk || lrg_node_visual_get_param (v, suggested[i].param) != NULL)
					continue;
				{
					g_autofree gchar *btn = g_strdup_printf ("+ %s", suggested[i].param);

					if (lrg_gui_button (self->gui, btn))
					{
						lrg_node_visual_set_param_double (v, suggested[i].param, suggested[i].value);
						changed = TRUE;
						label = "Add Parameter";
					}
					lrg_gui_same_line (self->gui, -1.0f);
				}
			}
			lrg_gui_dummy (self->gui, 0.0f, 0.0f);

			/* Custom parameter: name + type + add. */
			{
				static const gchar *types[] = { "number", "integer", "boolean", "text" };
				gfloat              w;

				lrg_gui_get_content_avail (self->gui, &w, NULL);
				lrg_gui_set_next_item_width (self->gui, w * 0.45f);
				lrg_gui_input_text (self->gui, "##new-param", "new parameter", self->param_key, 0);
				lrg_gui_same_line (self->gui, -1.0f);
				lrg_gui_set_next_item_width (self->gui, w * 0.3f);
				lrg_gui_combo (self->gui, "##new-param-type", &self->new_param_type, types, 4);
				lrg_gui_same_line (self->gui, -1.0f);
				if (lrg_gui_button (self->gui, "Add##param") && self->param_key->len > 0)
				{
					GValue nv = G_VALUE_INIT;

					switch (self->new_param_type)
					{
					case 1:
						g_value_init (&nv, G_TYPE_INT64);
						break;
					case 2:
						g_value_init (&nv, G_TYPE_BOOLEAN);
						break;
					case 3:
						g_value_init (&nv, G_TYPE_STRING);
						g_value_set_string (&nv, "");
						break;
					default:
						g_value_init (&nv, G_TYPE_DOUBLE);
						break;
					}
					lrg_node_visual_set_param (v, self->param_key->str, &nv);
					g_value_unset (&nv);
					g_string_truncate (self->param_key, 0);
					changed = TRUE;
					label = "Add Parameter";
				}
			}
		}

		if (changed)
		{
			lrg_node_set_visual (node, v);
			merge = merge && merging (self);
			lrg_editor_commit_node_state (self->editor, node, before, label, merge);
			return;
		}
	}

	if (changed)
		lrg_editor_commit_node_state (self->editor, node, before, label, FALSE);
	section_gap (self);
}

/* ==========================================================================
 * Components
 * ========================================================================== */

static void
component_body (LrgEditorApp     *self,
                LrgNode          *node,
                guint             index,
                LrgComponentDesc *desc)
{
	LrgRegistry *reg = registry ();
	const gchar *type_name = lrg_component_desc_get_type_name (desc);
	GType        type = (reg != NULL && type_name != NULL) ? lrg_registry_lookup (reg, type_name) : G_TYPE_INVALID;
	g_autoptr(GHashTable) shown = g_hash_table_new (g_str_hash, g_str_equal);
	GList       *keys, *l;

	/* Reflected properties of the registered type. */
	if (type != G_TYPE_INVALID && G_TYPE_IS_OBJECT (type))
	{
		GObjectClass *klass = g_type_class_ref (type);
		guint         n = 0, i;
		GParamSpec  **pspecs = g_object_class_list_properties (klass, &n);

		for (i = 0; i < n; i++)
		{
			GParamSpec   *pspec = pspecs[i];
			const GValue *cur;
			GValue        out = G_VALUE_INIT;

			if (!(pspec->flags & G_PARAM_WRITABLE) || (pspec->flags & G_PARAM_CONSTRUCT_ONLY) ||
			    G_TYPE_IS_OBJECT (pspec->value_type) || pspec->owner_type == G_TYPE_OBJECT)
				continue;

			g_hash_table_add (shown, (gpointer) pspec->name);
			cur = lrg_component_desc_get_value (desc, pspec->name);
			lrg_gui_push_id (self->gui, pspec->name);
			if (edit_value (self, pspec->name, pspec->value_type, cur, pspec, &out))
			{
				lrg_editor_set_component_value (self->editor, node, index, pspec->name, &out, merging (self));
				g_value_unset (&out);
			}
			if (cur == NULL)
				lrg_gui_set_item_tooltip (self->gui, g_param_spec_get_blurb (pspec) != NULL
				                                     ? g_param_spec_get_blurb (pspec) : "default value");
			else if (lrg_gui_begin_popup_context_item (self->gui, NULL))
			{
				if (lrg_gui_menu_item (self->gui, "Reset to default", NULL, FALSE, TRUE))
					lrg_editor_set_component_value (self->editor, node, index, pspec->name, NULL, FALSE);
				lrg_gui_end_popup (self->gui);
			}
			lrg_gui_pop_id (self->gui);
		}
		g_free (pspecs);
		g_type_class_unref (klass);
	}
	else if (type_name != NULL && reg != NULL)
	{
		lrg_gui_text_colored (self->gui, lrg_gui_context_get_style (self->gui)->colors[LRG_GUI_COL_WARNING],
		                      "Type not registered in this editor; editing raw values");
	}

	/* Stored keys not covered by the type's properties. */
	keys = lrg_component_desc_get_sorted_keys (desc);
	for (l = keys; l != NULL; l = l->next)
	{
		const GValue *cur = lrg_component_desc_get_value (desc, l->data);
		GValue        out = G_VALUE_INIT;

		if (g_hash_table_contains (shown, l->data) || cur == NULL)
			continue;
		lrg_gui_push_id (self->gui, l->data);
		if (edit_value (self, l->data, G_VALUE_TYPE (cur), cur, NULL, &out))
		{
			lrg_editor_set_component_value (self->editor, node, index, l->data, &out, merging (self));
			g_value_unset (&out);
		}
		if (lrg_gui_begin_popup_context_item (self->gui, NULL))
		{
			if (lrg_gui_menu_item (self->gui, "Remove value", NULL, FALSE, TRUE))
				lrg_editor_set_component_value (self->editor, node, index, l->data, NULL, FALSE);
			lrg_gui_end_popup (self->gui);
		}
		lrg_gui_pop_id (self->gui);
	}
	g_list_free (keys);

	/* Add a raw value. */
	{
		static const gchar *types[] = { "number", "integer", "boolean", "text" };
		gfloat              w;

		lrg_gui_get_content_avail (self->gui, &w, NULL);
		lrg_gui_set_next_item_width (self->gui, w * 0.45f);
		lrg_gui_input_text (self->gui, "##new-key", "new value", self->scratch, 0);
		lrg_gui_same_line (self->gui, -1.0f);
		lrg_gui_set_next_item_width (self->gui, w * 0.3f);
		lrg_gui_combo (self->gui, "##new-key-type", &self->new_param_type, types, 4);
		lrg_gui_same_line (self->gui, -1.0f);
		if (lrg_gui_button (self->gui, "Add##key") && self->scratch->len > 0)
		{
			GValue nv = G_VALUE_INIT;

			switch (self->new_param_type)
			{
			case 1:
				g_value_init (&nv, G_TYPE_INT64);
				break;
			case 2:
				g_value_init (&nv, G_TYPE_BOOLEAN);
				break;
			case 3:
				g_value_init (&nv, G_TYPE_STRING);
				g_value_set_string (&nv, "");
				break;
			default:
				g_value_init (&nv, G_TYPE_DOUBLE);
				break;
			}
			lrg_editor_set_component_value (self->editor, node, index, self->scratch->str, &nv, FALSE);
			g_value_unset (&nv);
			g_string_truncate (self->scratch, 0);
		}
	}
}

static void
add_component_popup (LrgEditorApp *self,
                     LrgNode      *node)
{
	LrgRegistry *reg = registry ();
	GList       *names = (reg != NULL) ? g_list_sort (lrg_registry_get_names (reg), (GCompareFunc) g_strcmp0) : NULL;
	GList       *l;
	guint        shown = 0;

	lrg_gui_set_next_popup_size (self->gui, 280.0f, 360.0f);
	if (!lrg_gui_begin_popup (self->gui, "##add-component"))
	{
		g_list_free (names);
		return;
	}

	if (lrg_gui_context_get_frame (self->gui) > 0 && self->component_search->len == 0)
		lrg_gui_set_keyboard_focus_here (self->gui);
	lrg_gui_set_next_item_width (self->gui, 0.0f);
	lrg_gui_input_text (self->gui, "##component-search", "search or type a name", self->component_search, 0);

	for (l = names; l != NULL; l = l->next)
	{
		GType type = lrg_registry_lookup (reg, l->data);

		/* Only component types belong here. */
		if (!g_type_is_a (type, LRG_TYPE_COMPONENT))
			continue;
		if (self->component_search->len > 0 && lrg_editor_fuzzy_score (self->component_search->str, l->data) == 0)
			continue;
		shown++;
		if (lrg_gui_selectable (self->gui, l->data, FALSE, 0))
		{
			g_autoptr(LrgComponentDesc) desc = lrg_component_desc_new (l->data);

			lrg_editor_add_component (self->editor, node, desc);
			g_string_truncate (self->component_search, 0);
		}
	}
	if (shown == 0)
		lrg_gui_text_disabled (self->gui, "No registered component types match.");

	lrg_gui_separator (self->gui);
	if (self->component_search->len > 0)
	{
		g_autofree gchar *custom = g_strdup_printf ("Add \"%s\" (custom type)", self->component_search->str);

		if (lrg_gui_selectable (self->gui, custom, FALSE, 0))
		{
			g_autoptr(LrgComponentDesc) desc = lrg_component_desc_new (self->component_search->str);

			lrg_editor_add_component (self->editor, node, desc);
			g_string_truncate (self->component_search, 0);
		}
	}

	lrg_gui_end_popup (self->gui);
	g_list_free (names);
}

static void
components_section (LrgEditorApp *self,
                    LrgNode      *node)
{
	GPtrArray *comps = lrg_node_get_components (node);
	guint      i;
	gint       remove = -1;

	{
		g_autofree gchar *title = g_strdup_printf ("Components (%u)###components", comps->len);

		if (!lrg_gui_collapsing_header (self->gui, title, TRUE))
			return;
	}

	for (i = 0; i < comps->len; i++)
	{
		LrgComponentDesc *desc = g_ptr_array_index (comps, i);
		const gchar      *type = lrg_component_desc_get_type_name (desc);
		g_autofree gchar *head = g_strdup_printf ("%s###component-%u", type != NULL ? type : "(no type)", i);
		gfloat            w;

		lrg_gui_push_id_int (self->gui, i);
		lrg_gui_get_content_avail (self->gui, &w, NULL);
		lrg_gui_set_next_item_width (self->gui, w - lrg_gui_get_frame_height (self->gui) -
		                             lrg_gui_context_get_style (self->gui)->item_spacing_x);
		{
			gboolean open = lrg_gui_tree_node (self->gui, head, LRG_GUI_TREE_FRAMED | LRG_GUI_TREE_DEFAULT_OPEN);

			lrg_gui_same_line (self->gui, -1.0f);
			if (lrg_gui_button_sized (self->gui, "x##remove-component", lrg_gui_get_frame_height (self->gui), 0.0f))
				remove = (gint) i;
			lrg_gui_set_item_tooltip (self->gui, "Remove component");
			if (open)
			{
				component_body (self, node, i, desc);
				lrg_gui_tree_pop (self->gui);
			}
		}
		lrg_gui_pop_id (self->gui);
	}

	if (remove >= 0)
		lrg_editor_remove_component (self->editor, node, (guint) remove);

	if (lrg_gui_button (self->gui, "Add Component..."))
	{
		g_string_truncate (self->component_search, 0);
		lrg_gui_open_popup (self->gui, "##add-component");
	}
	add_component_popup (self, node);
	section_gap (self);
}

/* ==========================================================================
 * Scripts
 * ========================================================================== */

static const gchar *language_names[] = {
	"none", "lua", "python", "gjs", "crispy (C)", "elisp", "native (.so)"
};

static void
scripts_section (LrgEditorApp *self,
                 LrgNode      *node)
{
	GPtrArray *scripts = lrg_node_get_scripts (node);
	guint      i;
	gint       remove = -1;

	{
		g_autofree gchar *title = g_strdup_printf ("Scripts (%u)###scripts", scripts->len);

		if (!lrg_gui_collapsing_header (self->gui, title, TRUE))
			return;
	}

	for (i = 0; i < scripts->len; i++)
	{
		LrgScriptBinding   *s = g_ptr_array_index (scripts, i);
		g_autoptr(GString)  path = g_string_new (lrg_script_binding_get_script (s));
		gboolean            enabled = lrg_script_binding_get_enabled (s);
		gint                lang = (gint) lrg_script_binding_get_language (s);
		gfloat              w;

		lrg_gui_push_id_int (self->gui, i);

		if (lrg_gui_checkbox (self->gui, "##enabled", &enabled))
		{
			g_autoptr(LrgNode) before = lrg_editor_snapshot_node (node);

			lrg_script_binding_set_enabled (g_ptr_array_index (lrg_node_get_scripts (node), i), enabled);
			lrg_editor_commit_node_state (self->editor, node, before, enabled ? "Enable Script" : "Disable Script",
			                              FALSE);
		}
		lrg_gui_set_item_tooltip (self->gui, "Enabled");
		lrg_gui_same_line (self->gui, -1.0f);
		lrg_gui_get_content_avail (self->gui, &w, NULL);
		lrg_gui_set_next_item_width (self->gui, w - 3.0f * (lrg_gui_get_frame_height (self->gui) +
		                                                    lrg_gui_context_get_style (self->gui)->item_spacing_x) - 90.0f);
		if (lrg_gui_input_text (self->gui, "##script-path", "script file", path,
		                        LRG_GUI_INPUT_TEXT_ENTER_RETURNS_TRUE))
		{
			g_autoptr(LrgNode) before = lrg_editor_snapshot_node (node);

			lrg_script_binding_set_script (g_ptr_array_index (lrg_node_get_scripts (node), i), path->str);
			lrg_editor_commit_node_state (self->editor, node, before, "Change Script", FALSE);
		}
		{
			const gchar *dropped = lrg_gui_accept_drop (self->gui, "asset");

			if (dropped != NULL)
			{
				g_autoptr(LrgNode) before = lrg_editor_snapshot_node (node);

				lrg_script_binding_set_script (g_ptr_array_index (lrg_node_get_scripts (node), i), dropped);
				lrg_editor_commit_node_state (self->editor, node, before, "Change Script", FALSE);
			}
		}
		lrg_gui_same_line (self->gui, -1.0f);
		lrg_gui_set_next_item_width (self->gui, 90.0f);
		if (lrg_gui_combo (self->gui, "##lang", &lang, language_names, G_N_ELEMENTS (language_names)))
		{
			g_autoptr(LrgNode) before = lrg_editor_snapshot_node (node);

			lrg_script_binding_set_language (g_ptr_array_index (lrg_node_get_scripts (node), i), lang);
			lrg_editor_commit_node_state (self->editor, node, before, "Change Script Language", FALSE);
		}
		lrg_gui_same_line (self->gui, -1.0f);
		if (lrg_gui_button_sized (self->gui, "Edit##open-script", 0.0f, 0.0f) &&
		    lrg_script_binding_get_script (s) != NULL)
			editor_app_open_file (self, lrg_script_binding_get_script (s), 1, 1);
		lrg_gui_set_item_tooltip (self->gui, "Open in the external editor ($EDITOR / Emacs)");
		lrg_gui_same_line (self->gui, -1.0f);
		if (lrg_gui_button_sized (self->gui, "x##remove-script", lrg_gui_get_frame_height (self->gui), 0.0f))
			remove = (gint) i;
		lrg_gui_set_item_tooltip (self->gui, "Detach script");

		lrg_gui_pop_id (self->gui);
	}

	if (remove >= 0)
		lrg_editor_remove_script (self->editor, node, (guint) remove);

	if (lrg_gui_button (self->gui, "New Script..."))
	{
		g_free (self->file_target_guid);
		self->file_target_guid = g_strdup (lrg_node_get_guid (node));
		editor_dialog_open (self, EDITOR_DIALOG_NEW_SCRIPT);
	}
	lrg_gui_same_line (self->gui, -1.0f);
	if (lrg_gui_button (self->gui, "Attach Existing..."))
	{
		g_autofree gchar *dir = (editor_app_base_dir (self) != NULL)
		                        ? g_strdup (editor_app_base_dir (self)) : g_get_current_dir ();

		editor_file_dialog_open (self, EDITOR_FILE_PICK_SCRIPT, dir, NULL);
		g_free (self->file_target_guid);
		self->file_target_guid = g_strdup (lrg_node_get_guid (node));
	}
	section_gap (self);
}

static void
prefab_section (LrgEditorApp *self,
                LrgNode      *node)
{
	LrgNodeVisual *v = lrg_node_get_visual (node);
	const gchar   *source = NULL;

	if (v == NULL)
		return;
	if (lrg_node_visual_get_kind (v) == LRG_NODE_VISUAL_PREFAB_INSTANCE)
		source = lrg_node_visual_get_asset (v);
	else if (lrg_node_visual_get_param (v, "prefab:source") != NULL &&
	         G_VALUE_HOLDS_STRING (lrg_node_visual_get_param (v, "prefab:source")))
		source = g_value_get_string (lrg_node_visual_get_param (v, "prefab:source"));
	if (source == NULL)
		return;

	if (!lrg_gui_collapsing_header (self->gui, "Prefab", TRUE))
		return;
	lrg_gui_property_label (self->gui, "Source");
	lrg_gui_text_disabled (self->gui, source);
	if (lrg_gui_button (self->gui, "Open Prefab"))
		editor_app_request (self, EDITOR_PENDING_OPEN_LEVEL, source);
	lrg_gui_same_line (self->gui, -1.0f);
	if (lrg_gui_button (self->gui, "Revert to Prefab"))
	{
		/* Replace the instance with a fresh one, same place and parent. */
		g_autoptr(GError) error = NULL;
		g_autofree gchar *src = g_strdup (source);
		LrgNode          *parent = lrg_node_get_parent (node);
		gint              index = lrg_node_get_child_index (parent, node);
		GrlVector3       *l = lrg_node_get_location (node), *r = lrg_node_get_rotation (node), *s = lrg_node_get_scale (node);
		gfloat            loc[3] = { l->x, l->y, l->z }, rot[3] = { r->x, r->y, r->z }, scl[3] = { s->x, s->y, s->z };
		LrgNode          *fresh;

		lrg_editor_begin_group (self->editor, "Revert to Prefab");
		lrg_editor_delete_node (self->editor, node);
		fresh = lrg_editor_instantiate_prefab (self->editor, src,
		                                       lrg_node_get_parent (parent) != NULL ? parent : NULL, &error);
		if (fresh != NULL)
		{
			lrg_editor_transform_node (self->editor, fresh, loc, rot, scl, FALSE);
			lrg_editor_move_node (self->editor, fresh, parent, index, FALSE);
		}
		lrg_editor_end_group (self->editor);
		if (fresh == NULL)
		{
			lrg_editor_undo (self->editor);
			editor_app_notify (self, 3, "Revert failed: %s", error->message);
		}
	}
	section_gap (self);
}

/* ==========================================================================
 * Level
 * ========================================================================== */

static void
level_section (LrgEditorApp *self)
{
	LrgLevel           *level = lrg_editor_get_level (self->editor);
	g_autoptr(GString)  name = g_string_new (lrg_level_get_name (level));
	gboolean            def2d = lrg_level_get_default_2d (level);
	const gchar        *path = lrg_editor_get_path (self->editor);

	lrg_gui_separator_text (self->gui, "Level");
	lrg_gui_property_label (self->gui, "Name");
	if (lrg_gui_input_text (self->gui, "##level-name", NULL, name, LRG_GUI_INPUT_TEXT_ENTER_RETURNS_TRUE))
	{
		GValue v = G_VALUE_INIT;

		g_value_init (&v, G_TYPE_STRING);
		g_value_set_string (&v, name->str);
		lrg_editor_set_object_property (self->editor, G_OBJECT (level), "name", &v, FALSE);
		g_value_unset (&v);
	}
	lrg_gui_property_label (self->gui, "Default view");
	if (lrg_gui_checkbox (self->gui, "2D##level-2d", &def2d))
	{
		GValue v = G_VALUE_INIT;

		g_value_init (&v, G_TYPE_BOOLEAN);
		g_value_set_boolean (&v, def2d);
		lrg_editor_set_object_property (self->editor, G_OBJECT (level), "default-2d", &v, FALSE);
		g_value_unset (&v);
	}
	lrg_gui_property_label (self->gui, "File");
	lrg_gui_text_disabled (self->gui, path != NULL ? path : "(not saved)");
	{
		g_autofree gchar *count = g_strdup_printf ("%u", lrg_level_count_nodes (level));

		lrg_gui_property_label (self->gui, "Nodes");
		lrg_gui_text (self->gui, count);
	}

	if (self->project != NULL)
	{
		lrg_gui_separator_text (self->gui, "Project");
		lrg_gui_property_label (self->gui, "Name");
		lrg_gui_text (self->gui, lrg_project_get_name (self->project));
		lrg_gui_property_label (self->gui, "Root");
		lrg_gui_text_disabled (self->gui, self->project_dir);
		if (lrg_gui_button (self->gui, "Project Settings..."))
			editor_dialog_open (self, EDITOR_DIALOG_PROJECT_SETTINGS);
	}

	lrg_gui_spacing (self->gui);
	lrg_gui_text_wrapped (self->gui, "Select a node in the viewport or hierarchy to edit it. "
	                                 "Ctrl+Shift+P opens the command palette.");
}

void
editor_panel_inspector (LrgEditorApp *self)
{
	LrgEditorSelection *sel = lrg_editor_get_selection (self->editor);
	LrgNode            *node = lrg_editor_selection_get_primary (sel);

	if (!lrg_gui_is_mouse_down (self->gui, LRG_GUI_MOUSE_LEFT))
		self->inspector_dragging = FALSE;

	if (node == NULL)
	{
		level_section (self);
		return;
	}

	lrg_gui_push_id_ptr (self->gui, node);
	header_section (self, node, lrg_editor_selection_get_count (sel));
	lrg_gui_spacing (self->gui);
	transform_section (self, node);

	/* The sections below may replace the selection (prefab revert). */
	if (lrg_editor_selection_get_primary (sel) == node)
		visual_section (self, node);
	if (lrg_editor_selection_get_primary (sel) == node)
		components_section (self, node);
	if (lrg_editor_selection_get_primary (sel) == node)
		scripts_section (self, node);
	if (lrg_editor_selection_get_primary (sel) == node)
		prefab_section (self, node);
	lrg_gui_pop_id (self->gui);
}
