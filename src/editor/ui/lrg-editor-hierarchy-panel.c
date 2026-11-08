/* lrg-editor-hierarchy-panel.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * The Hierarchy (outliner) panel on the retained UI toolkit.
 *
 *   +----------------------------------+
 *   | [ Filter nodes (name/GUID) ] [+] |   search field, create menu
 *   | (  Scene  |  Live  )             |   only while playing
 *   | Level name - 12 nodes            |   info line
 *   | > Lights                     o L |   LrgTreeView over an
 *   |     * Sun                    o L |   LrgEditorNodeModel, rows are
 *   |   # Ground                   o L |   HierarchyRow widgets
 *   +----------------------------------+
 *
 * Pieces:
 *   HierarchyRow     one row: kind glyph, an LrgLabel with the name (the
 *                    tree's inline rename covers it), script / prefab /
 *                    override markers and the eye and lock toggles. It
 *                    follows its node's name and reads the rest live
 *                    while painting, and it is the drag source.
 *   selection        the tree's selection-changed is pushed into the
 *                    editor selection; the selection's changed signal is
 *                    pulled back into the tree (revealing the primary);
 *                    a guard flag breaks the loop.
 *   drop target      on the tree view: nodes reparent (INTO) or reorder
 *                    (BEFORE / AFTER) as one undo step, Alt (the LINK
 *                    action) keeps world transforms; assets create nodes
 *                    or attach scripts.
 *   context menu     an LrgMenu over a GMenuModel built per popup from the
 *                    editor's actions ("editor.*") and the panel's own
 *                    group ("hierarchy.*", acting on the menu's node).
 *   live tab         an LrgTreeView over the play session's live tree:
 *                    the game objects of a 2D world (flat), the entity
 *                    hierarchy of a 3D world (children through
 *                    lrg_editor_play_session_get_live_children()).
 *   actions / IPC    node.rename and edit.find are replaced; the
 *                    hierarchy.* methods are registered. Both look the
 *                    panel up through the app, so they never outlive it.
 */

#include "config.h"

#include <math.h>
#include <string.h>
#include <json-glib/json-glib.h>

#include "lrg-editor-hierarchy-panel.h"
#include "lrg-editor-app-private.h"
#include "../lrg-editor-node-model.h"
#include "../../ui/lrg-label.h"
#include "../../ui/lrg-button.h"
#include "../../ui/lrg-drag.h"
#include "../../ui/lrg-drop-target.h"
#include "../../ui/lrg-ui-event.h"
#include "../../paint/lrg-paint-list.h"
#include "../../paint/lrg-paint-icons.h"
#include "../../ecs/lrg-game-object.h"
#include "../../world3d/lrg-entity3d.h"

#define PANEL_KEY       "lrg-editor-hierarchy-panel"
#define NODE_TYPE       "application/x-lrg-node"
#define ASSET_TYPE      "application/x-lrg-asset"
#define PREFAB_TYPE     "application/x-lrg-prefab"

struct _LrgEditorHierarchyPanel
{
	LrgContainer          parent_instance;

	LrgEditorApp         *app;          /* weak */
	LrgEditor            *editor;       /* strong: signals are disconnected from it */
	LrgEditorPlaySession *play;         /* strong */
	LrgEditorNodeModel   *model;

	/* Children (borrowed: owned by the container) */
	LrgTextInput         *search;
	LrgButton            *add_button;
	LrgTreeView          *tree;
	LrgTreeView          *live;

	LrgMenu              *menu;         /* owned, sunk */
	GSimpleActionGroup   *group;        /* "hierarchy" actions */
	LrgNode              *menu_node;    /* the node the open menu acts on */

	/* Tabs and the info line (laid out by layout_children) */
	gboolean              playing;
	gboolean              show_live;
	gfloat                tabs_x, tabs_y, tabs_w, tabs_h;
	gfloat                info_y, info_h;

	/* Selection mirroring */
	gboolean              syncing;
	LrgInputModifiers     gesture_mods;  /* modifiers of the last row press */
	gboolean              gesture_valid;
	gboolean              reveal_pending;

	LrgNode              *pending_rename;
	GObject              *live_selection;

	gulong                selection_id;
	gulong                started_id;
	gulong                stopped_id;
	gulong                replaced_id;
};

G_DEFINE_FINAL_TYPE (LrgEditorHierarchyPanel, lrg_editor_hierarchy_panel, LRG_TYPE_CONTAINER)

enum
{
	SIGNAL_LIVE_SELECTED,
	N_SIGNALS
};

static guint signals[N_SIGNALS];

static void sync_tree_from_editor (LrgEditorHierarchyPanel *self, gboolean reveal);

/* ==========================================================================
 * Small helpers
 * ========================================================================== */

static GrlColor
with_alpha (const GrlColor *c,
            gfloat          a)
{
	GrlColor out = *c;

	out.a = (guint8) CLAMP ((gfloat) c->a * a, 0.0f, 255.0f);
	return out;
}

static const GrlColor *
theme_color (LrgWidget     *widget,
             LrgThemeColor  which)
{
	return lrg_theme_get_color (lrg_widget_get_theme (widget), which);
}

static gfloat
theme_metric (LrgWidget      *widget,
              LrgThemeMetric  which)
{
	return lrg_theme_get_metric (lrg_widget_get_theme (widget), which);
}

static LrgEditorSelection *
selection_of (LrgEditorHierarchyPanel *self)
{
	return lrg_editor_get_selection (self->editor);
}

/* The prefab source path of a 0.8.0 (unlinked) instance root, or NULL. */
static const gchar *
legacy_prefab_source (LrgNode *node)
{
	LrgNodeVisual *v = lrg_node_get_visual (node);
	const GValue  *param;

	if (v == NULL)
		return NULL;
	if (lrg_node_visual_get_kind (v) == LRG_NODE_VISUAL_PREFAB_INSTANCE && lrg_node_visual_get_asset (v) != NULL)
		return lrg_node_visual_get_asset (v);
	param = lrg_node_visual_get_param (v, "prefab:source");
	if (param != NULL && G_VALUE_HOLDS_STRING (param))
		return g_value_get_string (param);
	return NULL;
}

/* The project-absolute form of a project-relative path. */
static gchar *
absolute_path (LrgEditorHierarchyPanel *self,
               const gchar             *path)
{
	return editor_app_project_path (self->app, path);
}

/* ==========================================================================
 * HierarchyRow: one row of the tree
 * ========================================================================== */

#define HIERARCHY_TYPE_ROW (hierarchy_row_get_type ())
G_DECLARE_FINAL_TYPE (HierarchyRow, hierarchy_row, HIERARCHY, ROW, LrgContainer)

struct _HierarchyRow
{
	LrgContainer             parent_instance;

	LrgEditorHierarchyPanel *panel;       /* borrowed: the panel owns the tree owning us */
	LrgNode                 *node;        /* bound item */
	gulong                   name_id;
	LrgLabel                *label;       /* borrowed */
	gfloat                   pointer_x;
	gfloat                   pointer_y;
	gboolean                 hovered;
	gboolean                 narrow_pending;  /* plain press on a row of a multi-selection */
};

G_DEFINE_FINAL_TYPE (HierarchyRow, hierarchy_row, LRG_TYPE_CONTAINER)

/* Toggle and glyph geometry: everything is a square of the row height. */
static gfloat
row_box (HierarchyRow *self)
{
	return lrg_widget_get_height (LRG_WIDGET (self));
}

/* Eye toggle (index 0) and lock toggle (index 1) at the trailing edge. */
static void
row_toggle_rect (HierarchyRow *self,
                 guint         index,
                 gfloat       *x,
                 gfloat       *y,
                 gfloat       *s)
{
	gfloat h = row_box (self);
	gfloat right = lrg_widget_get_world_x (LRG_WIDGET (self)) + lrg_widget_get_width (LRG_WIDGET (self));

	*s = h;
	*y = lrg_widget_get_world_y (LRG_WIDGET (self));
	*x = right - h * (gfloat) (2 - index) - 4.0f;
}

static gboolean
row_toggle_hit (HierarchyRow *self,
                guint         index,
                gfloat        px,
                gfloat        py)
{
	gfloat x, y, s;

	row_toggle_rect (self, index, &x, &y, &s);
	return px >= x && px < x + s && py >= y && py < y + s;
}

static void
on_row_name (GObject    *object,
             GParamSpec *pspec,
             gpointer    user_data)
{
	HierarchyRow *self = HIERARCHY_ROW (user_data);
	const gchar  *name = lrg_node_get_name (LRG_NODE (object));

	lrg_label_set_text (self->label, (name != NULL && *name != '\0') ? name : "(unnamed)");
}

static void
hierarchy_row_set_node (HierarchyRow *self,
                        LrgNode      *node)
{
	if (self->node == node)
		return;
	if (self->node != NULL)
		g_clear_signal_handler (&self->name_id, self->node);
	g_set_object (&self->node, node);
	if (node != NULL)
	{
		self->name_id = g_signal_connect (node, "notify::name", G_CALLBACK (on_row_name), self);
		on_row_name (G_OBJECT (node), NULL, self);
	}
}

static void
hierarchy_row_layout_children (LrgContainer *container)
{
	HierarchyRow *self = HIERARCHY_ROW (container);
	gfloat        h = row_box (self);
	gfloat        w = lrg_widget_get_width (LRG_WIDGET (self));
	gfloat        lx = h + 2.0f;

	/* Label between the glyph and the markers + toggles */
	lrg_widget_place_in_slot (LRG_WIDGET (self->label), lx, 0.0f, MAX (10.0f, w - lx - h * 2.0f - 24.0f), h);
}

/*
 * paint_kind_glyph:
 *
 * A small glyph for what the node is, drawn in a box of @s at (@x, @y)
 * from paint primitives (the built-in icon set has no light or camera).
 */
static void
paint_kind_glyph (LrgWidget    *widget,
                  LrgPaintList *list,
                  LrgNode      *node,
                  gfloat        x,
                  gfloat        y,
                  gfloat        s)
{
	LrgNodeVisual    *v = lrg_node_get_visual (node);
	LrgNodeVisualKind kind = (v != NULL) ? lrg_node_visual_get_kind (v) : LRG_NODE_VISUAL_NONE;
	gfloat            cx = x + s * 0.5f;
	gfloat            cy = y + s * 0.5f;
	gfloat            r = s * 0.30f;
	gfloat            t = MAX (1.2f, s * 0.08f);
	GrlColor          c;

	if (lrg_node_get_prefab_link (node) != NULL || kind == LRG_NODE_VISUAL_PREFAB_INSTANCE)
	{
		/* Prefab instance root: an accent diamond */
		c = *theme_color (widget, LRG_THEME_COLOR_ACCENT);
		lrg_paint_list_triangle (list, cx, cy - r * 1.15f, cx + r * 1.05f, cy, cx - r * 1.05f, cy, &c);
		lrg_paint_list_triangle (list, cx, cy + r * 1.15f, cx - r * 1.05f, cy, cx + r * 1.05f, cy, &c);
		return;
	}

	c = *theme_color (widget, LRG_THEME_COLOR_TEXT_MUTED);
	switch (kind)
	{
	case LRG_NODE_VISUAL_LIGHT:
		{
			const GValue *lt = lrg_node_visual_get_param (v, "light_type");
			const gchar  *type = (lt != NULL && G_VALUE_HOLDS_STRING (lt)) ? g_value_get_string (lt) : NULL;
			gint          i;

			c = *theme_color (widget, LRG_THEME_COLOR_WARNING);
			if (g_strcmp0 (type, "spot") == 0 ||
			    (type == NULL && lrg_node_visual_get_param_double (v, "spot_angle", 0.0) > 0.0))
			{
				/* spot: a lamp head and its widening beam */
				GrlColor beam = c;

				beam.a = (guint8) (beam.a / 3);
				lrg_paint_list_triangle (list, cx, cy - r * 0.55f, cx + r * 1.2f, cy + r * 1.15f,
				                         cx - r * 1.2f, cy + r * 1.15f, &beam);
				lrg_paint_list_circle (list, cx, cy - r * 0.6f, r * 0.5f, &c);
				lrg_paint_list_line (list, cx, cy - r * 0.55f, cx + r * 1.2f, cy + r * 1.15f, t, &c);
				lrg_paint_list_line (list, cx, cy - r * 0.55f, cx - r * 1.2f, cy + r * 1.15f, t, &c);
			}
			else if (g_strcmp0 (type, "directional") == 0)
			{
				/* sun: a disc with parallel rays falling down-left */
				lrg_paint_list_circle (list, cx + r * 0.35f, cy - r * 0.35f, r * 0.6f, &c);
				for (i = 0; i < 3; i++)
				{
					gfloat o = ((gfloat) i - 1.0f) * r * 0.55f;

					lrg_paint_list_line (list, cx - r * 0.2f + o, cy + r * 0.1f - o * 0.2f,
					                     cx - r * 1.0f + o, cy + r * 1.1f - o * 0.2f, t, &c);
				}
			}
			else
			{
				lrg_paint_list_circle (list, cx, cy, r * 0.55f, &c);
				for (i = 0; i < 8; i++)
				{
					gfloat a = (gfloat) i * (gfloat) G_PI / 4.0f;

					lrg_paint_list_line (list, cx + cosf (a) * r * 0.85f, cy + sinf (a) * r * 0.85f,
					                     cx + cosf (a) * r * 1.25f, cy + sinf (a) * r * 1.25f, t, &c);
				}
			}
		}
		break;
	case LRG_NODE_VISUAL_DECAL:
		{
			/* a projector: a square stamp with an arrow pressing into it */
			GrlColor fill;

			c.r = 0xf5; c.g = 0xc2; c.b = 0xe7; c.a = 0xff;
			fill = c;
			fill.a = 0x50;
			lrg_paint_list_rect (list, cx - r * 1.1f, cy + r * 0.2f, r * 2.2f, r * 0.9f, r * 0.2f, &fill);
			lrg_paint_list_border (list, cx - r * 1.1f, cy + r * 0.2f, r * 2.2f, r * 0.9f, r * 0.2f, t, &c);
			lrg_paint_list_line (list, cx, cy - r * 1.15f, cx, cy + r * 0.05f, t, &c);
			lrg_paint_list_triangle (list, cx - r * 0.45f, cy - r * 0.35f, cx + r * 0.45f, cy - r * 0.35f,
			                         cx, cy + r * 0.15f, &c);
		}
		break;
	case LRG_NODE_VISUAL_REFLECTION_PROBE:
		{
			/* a mirror ball in a box */
			GrlColor hi;

			c.r = 0x89; c.g = 0xdc; c.b = 0xeb; c.a = 0xff;
			hi.r = 0xff; hi.g = 0xff; hi.b = 0xff; hi.a = 0xd0;
			lrg_paint_list_border (list, cx - r * 1.15f, cy - r * 1.15f, r * 2.3f, r * 2.3f, r * 0.25f, t * 0.8f, &c);
			lrg_paint_list_circle (list, cx, cy, r * 0.75f, &c);
			lrg_paint_list_circle (list, cx - r * 0.25f, cy - r * 0.25f, r * 0.22f, &hi);
		}
		break;
	case LRG_NODE_VISUAL_PROBE_VOLUME:
		{
			/* light probes (0.9.0 M3): a 3 x 3 lattice of dots in a box */
			gint ix, iy;

			c.r = 0xf9; c.g = 0xe2; c.b = 0xaf; c.a = 0xff;
			lrg_paint_list_border (list, cx - r * 1.15f, cy - r * 1.15f, r * 2.3f, r * 2.3f, r * 0.25f, t * 0.8f, &c);
			for (iy = -1; iy <= 1; iy++)
				for (ix = -1; ix <= 1; ix++)
					lrg_paint_list_circle (list, cx + (gfloat) ix * r * 0.6f, cy + (gfloat) iy * r * 0.6f,
					                       r * (ix == 0 && iy == 0 ? 0.28f : 0.2f), &c);
		}
		break;
	case LRG_NODE_VISUAL_CAMERA:
		c = *theme_color (widget, LRG_THEME_COLOR_LINK);
		lrg_paint_list_rect (list, cx - r * 1.1f, cy - r * 0.6f, r * 1.4f, r * 1.2f, r * 0.2f, &c);
		lrg_paint_list_triangle (list, cx + r * 0.35f, cy, cx + r * 1.15f, cy - r * 0.6f, cx + r * 1.15f, cy + r * 0.6f, &c);
		break;
	case LRG_NODE_VISUAL_AUDIO_EMITTER:
		c = *theme_color (widget, LRG_THEME_COLOR_SUCCESS);
		lrg_paint_list_rect (list, cx - r * 1.0f, cy - r * 0.35f, r * 0.6f, r * 0.7f, 1.0f, &c);
		lrg_paint_list_triangle (list, cx - r * 0.5f, cy - r * 0.35f, cx + r * 0.4f, cy - r * 1.0f, cx + r * 0.4f, cy + r * 1.0f, &c);
		lrg_paint_list_triangle (list, cx - r * 0.5f, cy - r * 0.35f, cx + r * 0.4f, cy + r * 1.0f, cx - r * 0.5f, cy + r * 0.35f, &c);
		lrg_paint_list_line (list, cx + r * 0.8f, cy - r * 0.5f, cx + r * 0.8f, cy + r * 0.5f, t, &c);
		break;
	case LRG_NODE_VISUAL_MESH_ASSET:
		lrg_paint_list_triangle (list, cx, cy - r * 1.1f, cx + r * 1.1f, cy + r * 0.9f, cx - r * 1.1f, cy + r * 0.9f, &c);
		break;
	case LRG_NODE_VISUAL_SPRITE:
		lrg_paint_list_border (list, cx - r * 1.1f, cy - r * 1.0f, r * 2.2f, r * 2.0f, r * 0.25f, t, &c);
		lrg_paint_list_triangle (list, cx - r * 0.8f, cy + r * 0.7f, cx - r * 0.1f, cy - r * 0.2f, cx + r * 0.6f, cy + r * 0.7f, &c);
		lrg_paint_list_circle (list, cx + r * 0.5f, cy - r * 0.45f, r * 0.22f, &c);
		break;
	case LRG_NODE_VISUAL_TILEMAP:
		{
			gfloat q = r * 0.95f;

			lrg_paint_list_rect (list, cx - q - 0.5f, cy - q - 0.5f, q, q, 1.0f, &c);
			lrg_paint_list_rect (list, cx + 0.5f, cy - q - 0.5f, q, q, 1.0f, &c);
			lrg_paint_list_rect (list, cx - q - 0.5f, cy + 0.5f, q, q, 1.0f, &c);
			lrg_paint_list_rect (list, cx + 0.5f, cy + 0.5f, q, q, 1.0f, &c);
		}
		break;
	case LRG_NODE_VISUAL_CAD_PART:
		lrg_paint_list_icon (list, LRG_ICON_GEAR, x, y, s, 0.0f, &c);
		break;
	case LRG_NODE_VISUAL_PRIMITIVE:
		lrg_paint_list_border (list, cx - r, cy - r, r * 2.0f, r * 2.0f, r * 0.3f, t, &c);
		lrg_paint_list_line (list, cx - r, cy - r * 0.25f, cx + r, cy - r * 0.25f, t * 0.8f, &c);
		break;
	default:
		if (lrg_node_get_n_children (node) > 0)
			lrg_paint_list_icon (list, LRG_ICON_FOLDER, x + s * 0.08f, y + s * 0.08f, s * 0.84f, 0.0f, &c);
		else
			lrg_paint_list_border (list, cx - r * 0.7f, cy - r * 0.7f, r * 1.4f, r * 1.4f, r * 0.7f, t, &c);
		break;
	}
}

static void
hierarchy_row_paint (LrgWidget    *widget,
                     LrgPaintList *list)
{
	HierarchyRow  *self = HIERARCHY_ROW (widget);
	LrgNode       *node = self->node;
	gfloat         x = lrg_widget_get_world_x (widget);
	gfloat         y = lrg_widget_get_world_y (widget);
	gfloat         h = row_box (self);
	gboolean       in_prefab;
	gboolean       visible;
	gboolean       locked;
	gboolean       match;
	GrlColor       name_color;
	gfloat         tx, ty, ts;
	gfloat         mx;

	if (node == NULL)
		return;

	visible = lrg_node_get_visible (node);
	locked = lrg_node_get_locked (node);
	in_prefab = lrg_node_get_prefab_source_guid (node) != NULL || lrg_node_get_prefab_link (node) != NULL;
	match = self->panel == NULL || self->panel->model == NULL ||
	        lrg_editor_node_model_is_match (self->panel->model, node);

	/* Name colour: prefab instances in the accent colour, hidden nodes and
	 * ancestors shown only for a filter match muted */
	if (in_prefab)
		name_color = *theme_color (widget, LRG_THEME_COLOR_ACCENT);
	else
		name_color = *theme_color (widget, LRG_THEME_COLOR_TEXT);
	if (!visible || !match)
		name_color = with_alpha (&name_color, 0.5f);
	lrg_label_set_color (self->label, &name_color);

	/* Glyph */
	if (!visible)
		lrg_paint_list_push_opacity (list, 0.5f);
	paint_kind_glyph (widget, list, node, x + h * 0.12f, y + h * 0.12f, h * 0.76f);
	if (!visible)
		lrg_paint_list_pop_opacity (list);

	/* The label (child) */
	LRG_WIDGET_CLASS (hierarchy_row_parent_class)->paint (widget, list);

	/* Markers before the toggles: scripts (green dot), prefab link (chain),
	 * overrides on a linked root (warning dot), the physics body and the nav-agent trail */
	row_toggle_rect (self, 0, &tx, &ty, &ts);
	mx = tx - 6.0f;
	if (lrg_node_get_prefab_link (node) != NULL)
	{
		const GrlColor *accent = theme_color (widget, LRG_THEME_COLOR_ACCENT);
		gfloat          cy = y + h * 0.5f;
		gfloat          lw = MAX (1.2f, h * 0.07f);

		/* Two linked rings */
		mx -= h * 0.6f;
		lrg_paint_list_border (list, mx, cy - h * 0.13f, h * 0.32f, h * 0.26f, h * 0.13f, lw, accent);
		lrg_paint_list_border (list, mx + h * 0.2f, cy - h * 0.13f, h * 0.32f, h * 0.26f, h * 0.13f, lw, accent);
		if (lrg_prefab_link_get_n_overrides (lrg_node_get_prefab_link (node)) > 0)
		{
			mx -= h * 0.3f;
			lrg_paint_list_circle (list, mx + h * 0.1f, cy, h * 0.1f, theme_color (widget, LRG_THEME_COLOR_WARNING));
		}
		mx -= 4.0f;
	}
	if (lrg_node_get_scripts (node)->len > 0)
	{
		mx -= h * 0.3f;
		lrg_paint_list_circle (list, mx + h * 0.1f, y + h * 0.5f, h * 0.1f, theme_color (widget, LRG_THEME_COLOR_SUCCESS));
	}
	if (lrg_node_get_physics (node) != NULL)
	{
		/* Physics body (0.9.0 M5): a small ball resting on a ground
		 * line, coloured like the physics debug draw (green dynamic,
		 * blue kinematic, grey static) */
		LrgNodePhysics *phys = lrg_node_get_physics (node);
		GrlColor        c;
		gfloat          cy = y + h * 0.5f;
		gfloat          r = h * 0.14f;
		gfloat          lw = MAX (1.0f, h * 0.06f);

		switch (lrg_node_physics_get_body_kind (phys))
		{
		case LRG_DYNAMICS_BODY_STATIC:
			c.r = 150; c.g = 150; c.b = 150; c.a = 255;
			break;
		case LRG_DYNAMICS_BODY_KINEMATIC:
			c.r = 80; c.g = 140; c.b = 255; c.a = 255;
			break;
		case LRG_DYNAMICS_BODY_DYNAMIC:
		default:
			c.r = 80; c.g = 220; c.b = 110; c.a = 255;
			break;
		}
		mx -= h * 0.5f;
		lrg_paint_list_circle (list, mx + h * 0.25f, cy - r * 0.6f, r, &c);
		lrg_paint_list_line (list, mx + h * 0.02f, cy + r * 0.9f, mx + h * 0.48f, cy + r * 0.9f, lw, &c);
		if (lrg_node_physics_get_joints (phys)->len > 0)
		{
			/* a joint: a short link to the left */
			lrg_paint_list_line (list, mx - h * 0.12f, cy - r * 0.6f, mx + h * 0.25f - r, cy - r * 0.6f, lw, &c);
			lrg_paint_list_circle (list, mx - h * 0.12f, cy - r * 0.6f, r * 0.5f, &c);
			mx -= h * 0.2f;
		}
	}
	if (lrg_node_find_component (node, "nav-agent") != NULL)
	{
		/* Navigation agent (0.9.0 M6): a dotted trail ending in an
		 * arrow head, cyan like the navigation overlay's links */
		GrlColor c = { 60, 210, 230, 255 };
		gfloat   cy = y + h * 0.5f;
		gfloat   r = MAX (1.0f, h * 0.05f);
		gfloat   lw = MAX (1.2f, h * 0.07f);
		gfloat   tip;

		mx -= h * 0.55f;
		tip = mx + h * 0.48f;
		lrg_paint_list_circle (list, mx + h * 0.04f, cy + h * 0.1f, r, &c);
		lrg_paint_list_circle (list, mx + h * 0.16f, cy + h * 0.02f, r, &c);
		lrg_paint_list_line (list, mx + h * 0.26f, cy - h * 0.04f, tip, cy - h * 0.04f, lw, &c);
		lrg_paint_list_line (list, tip, cy - h * 0.04f, tip - h * 0.12f, cy - h * 0.16f, lw, &c);
		lrg_paint_list_line (list, tip, cy - h * 0.04f, tip - h * 0.12f, cy + h * 0.08f, lw, &c);
	}

	/* Eye toggle: filled pupil when visible, ring when hidden; brighter
	 * under the pointer */
	{
		gboolean        hot = self->hovered && row_toggle_hit (self, 0, self->pointer_x, self->pointer_y);
		const GrlColor *muted = theme_color (widget, LRG_THEME_COLOR_TEXT_MUTED);
		GrlColor        c = hot ? *theme_color (widget, LRG_THEME_COLOR_TEXT)
		                        : with_alpha (muted, visible ? 0.9f : 0.6f);

		if (visible)
			lrg_paint_list_icon (list, LRG_ICON_EYE, tx + ts * 0.18f, ty + ts * 0.18f, ts * 0.64f, 0.0f, &c);
		else
			lrg_paint_list_border (list, tx + ts * 0.36f, ty + ts * 0.36f, ts * 0.28f, ts * 0.28f, ts * 0.14f,
			                       MAX (1.0f, ts * 0.06f), &c);
	}

	/* Lock toggle: only drawn when locked or hovered (like 0.8.0) */
	row_toggle_rect (self, 1, &tx, &ty, &ts);
	if (locked || self->hovered)
	{
		gboolean hot = self->hovered && row_toggle_hit (self, 1, self->pointer_x, self->pointer_y);
		GrlColor c = locked ? *theme_color (widget, LRG_THEME_COLOR_WARNING)
		                    : with_alpha (theme_color (widget, LRG_THEME_COLOR_TEXT_MUTED), hot ? 1.0f : 0.55f);

		lrg_paint_list_icon (list, LRG_ICON_LOCK, tx + ts * 0.2f, ty + ts * 0.2f, ts * 0.6f, 0.0f, &c);
	}
}

/* Toggles a boolean node property as one undo step. */
static void
toggle_node_flag (LrgEditorHierarchyPanel *self,
                  LrgNode                 *node,
                  const gchar             *prop,
                  gboolean                 now)
{
	GValue v = G_VALUE_INIT;

	g_value_init (&v, G_TYPE_BOOLEAN);
	g_value_set_boolean (&v, !now);
	lrg_editor_set_object_property (self->editor, G_OBJECT (node), prop, &v, FALSE);
	g_value_unset (&v);
}

static gboolean
hierarchy_row_handle_event (LrgWidget        *widget,
                            const LrgUIEvent *event)
{
	HierarchyRow *self = HIERARCHY_ROW (widget);
	gfloat        px = lrg_ui_event_get_x (event);
	gfloat        py = lrg_ui_event_get_y (event);

	switch (lrg_ui_event_get_event_type (event))
	{
	case LRG_UI_EVENT_POINTER_ENTER:
	case LRG_UI_EVENT_MOUSE_MOVE:
		self->hovered = TRUE;
		self->pointer_x = px;
		self->pointer_y = py;
		return FALSE;

	case LRG_UI_EVENT_POINTER_LEAVE:
		self->hovered = FALSE;
		return FALSE;

	case LRG_UI_EVENT_MOUSE_BUTTON_DOWN:
		if (self->node == NULL || self->panel == NULL || lrg_ui_event_get_button (event) != GRL_MOUSE_BUTTON_LEFT)
			return FALSE;
		if (row_toggle_hit (self, 0, px, py))
		{
			toggle_node_flag (self->panel, self->node, "visible", lrg_node_get_visible (self->node));
			return TRUE;
		}
		if (row_toggle_hit (self, 1, px, py))
		{
			toggle_node_flag (self->panel, self->node, "locked", lrg_node_get_locked (self->node));
			return TRUE;
		}
		/* A plain press on a row that is part of a multi-selection keeps the
		 * selection, so the whole selection can be dragged; a release
		 * without a drag narrows it to this row (as in 0.8.0) */
		if ((lrg_ui_event_get_modifiers (event) & (LRG_INPUT_MODIFIER_CTRL | LRG_INPUT_MODIFIER_SHIFT)) == 0 &&
		    lrg_editor_selection_contains (selection_of (self->panel), self->node) &&
		    lrg_editor_selection_get_count (selection_of (self->panel)) > 1)
		{
			self->narrow_pending = TRUE;
			return TRUE;
		}
		/* Remember the gesture's modifiers: the tree selects next and the
		 * panel needs to know whether it was a range / toggle */
		self->panel->gesture_mods = lrg_ui_event_get_modifiers (event);
		self->panel->gesture_valid = TRUE;
		return FALSE;

	case LRG_UI_EVENT_MOUSE_BUTTON_UP:
		if (!self->narrow_pending)
			return FALSE;
		self->narrow_pending = FALSE;
		/* A drag took over: the release arrives far outside the row */
		if (self->node != NULL && self->panel != NULL && lrg_widget_contains_point (widget, px, py))
			lrg_editor_select (self->panel->editor, self->node, FALSE);
		return TRUE;

	default:
		return FALSE;
	}
}

/* Drag source: the bound node, or "N nodes" when it is part of a larger
 * selection (the drop moves the whole selection). */
static LrgDragPayload *
on_row_prepare (LrgDragSource *source,
                gfloat         x,
                gfloat         y,
                gpointer       user_data)
{
	HierarchyRow   *self = HIERARCHY_ROW (user_data);
	LrgDragPayload *payload;
	guint           n;

	if (self->node == NULL || self->panel == NULL || lrg_tree_view_get_editing_item (self->panel->tree) != NULL)
		return NULL;
	/* No drags from the eye / lock toggles */
	if (row_toggle_hit (self, 0, x, y) || row_toggle_hit (self, 1, x, y))
		return NULL;

	payload = lrg_drag_payload_new_object (NODE_TYPE, self->node, LRG_DRAG_ACTION_MOVE | LRG_DRAG_ACTION_LINK);
	n = lrg_editor_selection_contains (selection_of (self->panel), self->node)
	    ? lrg_editor_selection_get_count (selection_of (self->panel)) : 1;
	if (n > 1)
	{
		g_autofree gchar *label = g_strdup_printf ("%u nodes", n);

		lrg_drag_payload_set_label (payload, label);
	}
	else
		lrg_drag_payload_set_label (payload, lrg_node_get_name (self->node));
	lrg_drag_payload_set_icon (payload, LRG_ICON_FILE);
	return payload;
}

static void
hierarchy_row_dispose (GObject *object)
{
	HierarchyRow *self = HIERARCHY_ROW (object);

	hierarchy_row_set_node (self, NULL);
	G_OBJECT_CLASS (hierarchy_row_parent_class)->dispose (object);
}

static void
hierarchy_row_class_init (HierarchyRowClass *klass)
{
	G_OBJECT_CLASS (klass)->dispose = hierarchy_row_dispose;
	LRG_WIDGET_CLASS (klass)->paint = hierarchy_row_paint;
	LRG_WIDGET_CLASS (klass)->handle_event = hierarchy_row_handle_event;
	LRG_CONTAINER_CLASS (klass)->layout_children = hierarchy_row_layout_children;
}

static void
hierarchy_row_init (HierarchyRow *self)
{
	g_autoptr(LrgLabel)      label = lrg_label_new ("");
	g_autoptr(LrgDragSource) source = lrg_drag_source_new ();

	lrg_widget_set_valign (LRG_WIDGET (label), LRG_ALIGN_CENTER);
	lrg_widget_set_name (LRG_WIDGET (label), "name");
	self->label = label;
	lrg_container_add_child (LRG_CONTAINER (self), LRG_WIDGET (label));

	g_signal_connect (source, "prepare", G_CALLBACK (on_row_prepare), self);
	lrg_widget_add_drag_source (LRG_WIDGET (self), g_steal_pointer (&source));
}

/* ==========================================================================
 * Tree factory
 * ========================================================================== */

static LrgWidget *
tree_create_row (LrgTreeView *tree,
                 gpointer     user_data)
{
	HierarchyRow *row = g_object_new (HIERARCHY_TYPE_ROW, NULL);

	row->panel = LRG_EDITOR_HIERARCHY_PANEL (user_data);
	lrg_widget_set_name (LRG_WIDGET (row), "hierarchy-row");
	return LRG_WIDGET (row);
}

static void
tree_bind_row (LrgTreeView *tree,
               LrgWidget   *row,
               GObject     *item,
               guint        depth,
               gpointer     user_data)
{
	hierarchy_row_set_node (HIERARCHY_ROW (row), LRG_NODE (item));
}

/* Live tab rows: a label with the authoring node's name (an entity the
 * simulation spawned shows its own name). */
static LrgWidget *
live_create_row (LrgTreeView *tree,
                 gpointer     user_data)
{
	LrgLabel *label = lrg_label_new ("");

	lrg_widget_set_valign (LRG_WIDGET (label), LRG_ALIGN_CENTER);
	return LRG_WIDGET (label);
}

/* Live tab hierarchy: entity children in 3D, a flat list in 2D. */
static GListModel *
live_children (LrgTreeView *tree,
               GObject     *item,
               gpointer     user_data)
{
	LrgEditorHierarchyPanel *self = LRG_EDITOR_HIERARCHY_PANEL (user_data);

	return lrg_editor_play_session_get_live_children (self->play, item);
}

static void
live_bind_row (LrgTreeView *tree,
               LrgWidget   *row,
               GObject     *item,
               guint        depth,
               gpointer     user_data)
{
	LrgEditorHierarchyPanel *self = LRG_EDITOR_HIERARCHY_PANEL (user_data);
	LrgNode                 *node = lrg_editor_play_session_get_node_for_live (self->play, item);
	g_autofree gchar        *text = NULL;

	if (node != NULL && lrg_node_get_name (node) != NULL)
		text = g_strdup (lrg_node_get_name (node));
	else if (LRG_IS_ENTITY3D (item) && lrg_entity3d_get_name (LRG_ENTITY3D (item)) != NULL)
		text = g_strdup (lrg_entity3d_get_name (LRG_ENTITY3D (item)));
	else
		text = g_strdup_printf ("%s %p", G_OBJECT_TYPE_NAME (item), (gpointer) item);
	lrg_label_set_text (LRG_LABEL (row), text);
}

/* ==========================================================================
 * Selection mirroring
 * ========================================================================== */

/* Tree -> editor: the tree's set becomes the editor selection, the
 * cursor item primary. A Ctrl / Shift gesture keeps selected nodes that
 * have no row (inside collapsed branches). */
static void
on_tree_selection_changed (LrgTreeView *tree,
                           gpointer     user_data)
{
	LrgEditorHierarchyPanel *self = LRG_EDITOR_HIERARCHY_PANEL (user_data);
	g_autoptr(GPtrArray)     items = NULL;
	g_autoptr(GPtrArray)     nodes = g_ptr_array_new ();
	GObject                 *cursor;
	gboolean                 keep_hidden;
	guint                    i;

	if (self->syncing)
		return;

	keep_hidden = self->gesture_valid &&
	              (self->gesture_mods & (LRG_INPUT_MODIFIER_CTRL | LRG_INPUT_MODIFIER_SHIFT)) != 0;
	self->gesture_valid = FALSE;

	items = lrg_tree_view_get_selected_items (tree);
	cursor = lrg_tree_view_get_cursor_item (tree);
	for (i = 0; i < items->len; i++)
		if (g_ptr_array_index (items, i) != cursor)
			g_ptr_array_add (nodes, g_ptr_array_index (items, i));
	if (keep_hidden)
	{
		GPtrArray *sel = lrg_editor_selection_get_nodes (selection_of (self));

		for (i = 0; i < sel->len; i++)
		{
			GObject *n = g_ptr_array_index (sel, i);

			if (lrg_tree_view_get_position (tree, n) == LRG_TREE_VIEW_INVALID_POSITION &&
			    !g_ptr_array_find (nodes, n, NULL))
				g_ptr_array_insert (nodes, 0, n);
		}
	}
	if (cursor != NULL && lrg_tree_view_is_item_selected (tree, cursor))
		g_ptr_array_add (nodes, cursor);

	self->syncing = TRUE;
	if (nodes->len == 0)
		lrg_editor_select (self->editor, NULL, FALSE);
	else
		lrg_editor_select_nodes (self->editor, nodes, FALSE);
	self->syncing = FALSE;
}

/*
 * sync_tree_from_editor:
 *
 * Editor -> tree: reveals the primary node (when asked) and makes the
 * tree's selection the selected nodes that have a row, primary last so
 * it is the cursor and range anchor.
 */
static void
sync_tree_from_editor (LrgEditorHierarchyPanel *self,
                       gboolean                 reveal)
{
	LrgEditorSelection  *sel = selection_of (self);
	LrgNode             *primary = lrg_editor_selection_get_primary (sel);
	GPtrArray           *nodes = lrg_editor_selection_get_nodes (sel);
	g_autoptr(GPtrArray) current = NULL;
	gboolean             same = TRUE;
	guint                n_rowed = 0;
	guint                i;

	self->syncing = TRUE;

	if (reveal && primary != NULL && lrg_editor_node_model_is_shown (self->model, primary))
	{
		lrg_tree_view_reveal (self->tree, G_OBJECT (primary));
		if (lrg_widget_get_height (LRG_WIDGET (self->tree)) <= 0.0f)
			self->reveal_pending = TRUE;
	}

	/* Compare with the tree's set first: nothing to do is the common case */
	current = lrg_tree_view_get_selected_items (self->tree);
	for (i = 0; i < nodes->len; i++)
	{
		GObject *n = g_ptr_array_index (nodes, i);

		if (lrg_tree_view_get_position (self->tree, n) == LRG_TREE_VIEW_INVALID_POSITION)
			continue;
		n_rowed++;
		if (!lrg_tree_view_is_item_selected (self->tree, n))
			same = FALSE;
	}
	if (n_rowed != current->len)
		same = FALSE;
	if (same && (primary == NULL || lrg_tree_view_get_cursor_item (self->tree) == G_OBJECT (primary) ||
	             lrg_tree_view_get_position (self->tree, G_OBJECT (primary)) == LRG_TREE_VIEW_INVALID_POSITION))
	{
		self->syncing = FALSE;
		return;
	}

	lrg_tree_view_unselect_all (self->tree);
	for (i = 0; i < nodes->len; i++)
	{
		GObject *n = g_ptr_array_index (nodes, i);

		if (n != G_OBJECT (primary) && lrg_tree_view_get_position (self->tree, n) != LRG_TREE_VIEW_INVALID_POSITION)
			lrg_tree_view_select_item_full (self->tree, n, LRG_SELECT_MODE_TOGGLE);
	}
	if (primary != NULL && lrg_tree_view_get_position (self->tree, G_OBJECT (primary)) != LRG_TREE_VIEW_INVALID_POSITION)
		lrg_tree_view_select_item_full (self->tree, G_OBJECT (primary), LRG_SELECT_MODE_TOGGLE);

	self->syncing = FALSE;
}

static void
on_editor_selection_changed (LrgEditorSelection *sel,
                             gpointer            user_data)
{
	LrgEditorHierarchyPanel *self = LRG_EDITOR_HIERARCHY_PANEL (user_data);

	if (self->syncing)
		return;
	sync_tree_from_editor (self, TRUE);
}

/* ==========================================================================
 * Inline rename
 * ========================================================================== */

static gboolean
on_edit_label (LrgTreeView *tree,
               GObject     *item,
               const gchar *text,
               gpointer     user_data)
{
	LrgEditorHierarchyPanel *self = LRG_EDITOR_HIERARCHY_PANEL (user_data);
	g_autofree gchar        *name = g_strstrip (g_strdup (text != NULL ? text : ""));

	if (*name == '\0')
		return FALSE;   /* refused: the field stays open after Enter */
	if (g_strcmp0 (name, lrg_node_get_name (LRG_NODE (item))) != 0)
	{
		lrg_editor_rename_node (self->editor, LRG_NODE (item), name);
		editor_app_set_status (self->app, "Renamed to %s", name);
	}
	return TRUE;
}

static void
on_row_activated (LrgTreeView *tree,
                  GObject     *item,
                  gpointer     user_data)
{
	/* Double click (or Enter on the cursor row) renames */
	lrg_tree_view_start_editing (tree, item);
}

/* Clears a filter that hides @node. */
static void
unfilter_for (LrgEditorHierarchyPanel *self,
              LrgNode                 *node)
{
	if (!lrg_editor_node_model_is_shown (self->model, node))
		lrg_editor_hierarchy_panel_set_filter (self, NULL);
}

gboolean
lrg_editor_hierarchy_panel_start_rename (LrgEditorHierarchyPanel *self,
                                         LrgNode                 *node)
{
	g_return_val_if_fail (LRG_IS_EDITOR_HIERARCHY_PANEL (self), FALSE);
	g_return_val_if_fail (LRG_IS_NODE (node), FALSE);

	lrg_editor_hierarchy_panel_set_page (self, "scene");
	unfilter_for (self, node);
	if (!lrg_tree_view_reveal (self->tree, G_OBJECT (node)))
		return FALSE;
	if (lrg_widget_get_height (LRG_WIDGET (self->tree)) > 0.0f &&
	    lrg_tree_view_start_editing (self->tree, G_OBJECT (node)))
	{
		g_clear_object (&self->pending_rename);
		return TRUE;
	}
	/* Not laid out yet (the panel was just shown): next frame */
	g_set_object (&self->pending_rename, node);
	return TRUE;
}

/* ==========================================================================
 * Filter
 * ========================================================================== */

/* Expands every shown node with shown children, top down. */
static void
expand_shown (LrgEditorHierarchyPanel *self,
              LrgNode                 *node)
{
	GPtrArray *children = lrg_node_get_children (node);
	guint      i;

	for (i = 0; i < children->len; i++)
	{
		LrgNode *c = g_ptr_array_index (children, i);

		if (!lrg_editor_node_model_is_shown (self->model, c))
			continue;
		if (g_list_model_get_n_items (lrg_editor_node_model_get_children (self->model, c)) > 0)
			lrg_tree_view_expand (self->tree, G_OBJECT (c));
		expand_shown (self, c);
	}
}

static void
apply_filter (LrgEditorHierarchyPanel *self,
              const gchar             *text)
{
	lrg_editor_node_model_set_filter (self->model, text);
	if (lrg_editor_node_model_get_filter (self->model) != NULL)
		expand_shown (self, lrg_level_get_root (lrg_editor_get_level (self->editor)));
	sync_tree_from_editor (self, lrg_editor_node_model_get_filter (self->model) == NULL);
}

static void
on_search_changed (LrgTextInput *input,
                   gpointer      user_data)
{
	LrgEditorHierarchyPanel *self = LRG_EDITOR_HIERARCHY_PANEL (user_data);

	apply_filter (self, lrg_text_input_get_text (input));
}

void
lrg_editor_hierarchy_panel_set_filter (LrgEditorHierarchyPanel *self,
                                       const gchar             *filter)
{
	g_return_if_fail (LRG_IS_EDITOR_HIERARCHY_PANEL (self));

	if (g_strcmp0 (lrg_text_input_get_text (self->search), filter != NULL ? filter : "") != 0)
		lrg_text_input_set_text (self->search, filter != NULL ? filter : "");
	apply_filter (self, filter);
}

gboolean
lrg_editor_hierarchy_panel_focus_search (LrgEditorHierarchyPanel *self)
{
	LrgWidget *root;

	g_return_val_if_fail (LRG_IS_EDITOR_HIERARCHY_PANEL (self), FALSE);

	lrg_editor_hierarchy_panel_set_page (self, "scene");
	root = lrg_widget_get_root (LRG_WIDGET (self));
	if (!LRG_IS_CANVAS (root))
		return FALSE;
	lrg_canvas_set_focused_widget (LRG_CANVAS (root), LRG_WIDGET (self->search));
	lrg_text_input_select_all (self->search);
	return lrg_canvas_get_focused_widget (LRG_CANVAS (root)) == LRG_WIDGET (self->search);
}

/* ==========================================================================
 * Drag and drop
 * ========================================================================== */

/* What a node drop moves: the selected roots when the grabbed node is
 * selected, else the node alone. */
static GPtrArray *
moving_set (LrgEditorHierarchyPanel *self,
            LrgNode                 *dragged)
{
	GPtrArray *moving;

	if (lrg_editor_selection_contains (selection_of (self), dragged))
		return lrg_editor_get_selected_roots (self->editor);
	moving = g_ptr_array_new ();
	g_ptr_array_add (moving, dragged);
	return moving;
}

/* The parent and index a drop at (@item, @pos) inserts at; @item NULL
 * is the root. *index is -1 to append. */
static LrgNode *
drop_parent (LrgEditorHierarchyPanel *self,
             LrgNode                 *item,
             LrgDropPosition          pos,
             gint                    *index)
{
	LrgNode *root = lrg_level_get_root (lrg_editor_get_level (self->editor));
	LrgNode *parent;

	*index = -1;
	if (item == NULL)
		return root;
	if (pos == LRG_DROP_POSITION_INTO)
		return item;
	parent = lrg_node_get_parent (item);
	if (parent == NULL)
		parent = root;
	*index = lrg_node_get_child_index (parent, item) + (pos == LRG_DROP_POSITION_AFTER ? 1 : 0);
	return parent;
}

/* Would moving @moving under @parent create a cycle? */
static gboolean
drop_is_cycle (GPtrArray *moving,
               LrgNode   *parent)
{
	guint i;

	for (i = 0; i < moving->len; i++)
	{
		LrgNode *m = g_ptr_array_index (moving, i);

		if (m == parent || lrg_node_is_ancestor_of (m, parent))
			return TRUE;
	}
	return FALSE;
}

static LrgDragAction
on_drop_motion (LrgDropTarget  *target,
                LrgDragPayload *payload,
                gfloat          x,
                gfloat          y,
                gpointer        user_data)
{
	LrgEditorHierarchyPanel *self = LRG_EDITOR_HIERARCHY_PANEL (user_data);
	GObject                 *item = NULL;
	LrgDropPosition          pos = LRG_DROP_POSITION_INTO;

	if (!lrg_tree_view_get_drop_position (self->tree, y, &item, &pos))
	{
		lrg_tree_view_clear_drop_indicator (self->tree);
		return LRG_DRAG_ACTION_NONE;
	}

	if (lrg_drag_payload_matches (payload, NODE_TYPE))
	{
		LrgNode             *dragged = lrg_drag_payload_get_object (payload);
		g_autoptr(GPtrArray) moving = NULL;
		LrgNode             *parent;
		gint                 index;

		if (!LRG_IS_NODE (dragged))
			return LRG_DRAG_ACTION_NONE;
		moving = moving_set (self, dragged);
		parent = drop_parent (self, LRG_NODE (item), pos, &index);
		if (drop_is_cycle (moving, parent))
		{
			/* Into its own subtree: refused, no indicator */
			lrg_tree_view_clear_drop_indicator (self->tree);
			return LRG_DRAG_ACTION_NONE;
		}
	}

	if (item != NULL)
		lrg_tree_view_set_drop_indicator (self->tree, item, pos);
	else
		lrg_tree_view_clear_drop_indicator (self->tree);
	return lrg_drop_target_get_proposed_action (target);
}

static void
on_drop_leave (LrgDropTarget *target,
               gpointer       user_data)
{
	lrg_tree_view_clear_drop_indicator (LRG_EDITOR_HIERARCHY_PANEL (user_data)->tree);
}

/* Moves the dragged node (or the selection containing it) as one undo
 * step. */
static gboolean
drop_nodes (LrgEditorHierarchyPanel *self,
            LrgNode                 *dragged,
            LrgNode                 *item,
            LrgDropPosition          pos,
            gboolean                 keep_world)
{
	g_autoptr(GPtrArray) moving = moving_set (self, dragged);
	LrgNode             *parent;
	gint                 index;
	guint                moved = 0;
	guint                i;

	parent = drop_parent (self, item, pos, &index);
	if (drop_is_cycle (moving, parent))
	{
		editor_app_set_status (self->app, "A node cannot be moved into itself");
		return FALSE;
	}

	lrg_editor_begin_group (self->editor, moving->len > 1 ? "Move Nodes" : "Move Node");
	for (i = 0; i < moving->len; i++)
	{
		LrgNode *n = g_ptr_array_index (moving, i);
		gint     at = index;

		/* Moving down within the same parent shifts the target index */
		if (at >= 0 && lrg_node_get_parent (n) == parent && lrg_node_get_child_index (parent, n) < at)
			at--;
		if (lrg_editor_move_node (self->editor, n, parent, at >= 0 ? at + (gint) i : -1, keep_world))
			moved++;
	}
	lrg_editor_end_group (self->editor);

	if (moved > 0)
	{
		if (pos == LRG_DROP_POSITION_INTO && item != NULL)
			lrg_tree_view_expand (self->tree, G_OBJECT (item));
		editor_app_set_status (self->app, "Moved %u node(s)%s", moved, keep_world ? " (kept world transform)" : "");
	}
	return moved > 0;
}

/* Assets dropped on the tree: prefabs instance, models / textures make
 * nodes, scripts attach to the row's node. */
static gboolean
drop_asset (LrgEditorHierarchyPanel *self,
            const gchar             *path,
            LrgNode                 *item,
            LrgDropPosition          pos)
{
	LrgAssetType      type = lrg_asset_database_classify (path);
	g_autoptr(GError) error = NULL;
	g_autofree gchar *base = g_path_get_basename (path);
	LrgNode          *root = lrg_level_get_root (lrg_editor_get_level (self->editor));
	LrgNode          *parent;
	LrgNode          *made = NULL;
	gint              index;

	parent = drop_parent (self, item, pos, &index);
	if (parent == root)
		parent = NULL;

	switch (type)
	{
	case LRG_ASSET_TYPE_PREFAB:
		{
			g_autofree gchar *abs = absolute_path (self, path);

			made = lrg_editor_instantiate_prefab (self->editor, abs, parent, &error);
			if (made == NULL)
			{
				editor_app_notify (self->app, EDITOR_NOTIFY_ERROR, "Prefab: %s", error->message);
				return FALSE;
			}
		}
		break;
	case LRG_ASSET_TYPE_SCRIPT:
		{
			g_autoptr(LrgScriptBinding) binding = NULL;

			if (item == NULL)
			{
				editor_app_set_status (self->app, "Drop %s onto a node to attach it", base);
				return FALSE;
			}
			binding = lrg_script_binding_new (editor_script_language_for_path (path), path);
			lrg_editor_add_script (self->editor, item, binding);
			editor_app_set_status (self->app, "Attached %s to %s", base, lrg_node_get_name (item));
			return TRUE;
		}
	case LRG_ASSET_TYPE_MODEL:
	case LRG_ASSET_TYPE_TEXTURE:
		{
			g_autoptr(LrgNode) before = NULL;

			lrg_editor_begin_group (self->editor, "Create from Asset");
			made = editor_app_create_node (self->app, base,
			                               type == LRG_ASSET_TYPE_MODEL ? LRG_NODE_VISUAL_MESH_ASSET
			                                                            : LRG_NODE_VISUAL_SPRITE,
			                               0, parent);
			before = lrg_editor_snapshot_node (made);
			lrg_node_visual_set_asset (lrg_node_get_visual (made), path);
			lrg_editor_commit_node_state (self->editor, made, before, "Set Asset", TRUE);
			lrg_editor_end_group (self->editor);
		}
		break;
	default:
		editor_app_set_status (self->app, "%s cannot be dropped here", base);
		return FALSE;
	}

	if (made != NULL)
	{
		/* Between rows: put it where the line was */
		if (index >= 0 && lrg_node_get_parent (made) != NULL)
		{
			LrgNode *p = lrg_node_get_parent (made);

			if (lrg_node_get_child_index (p, made) != index)
				lrg_editor_move_node (self->editor, made, p, index, FALSE);
		}
		lrg_editor_select (self->editor, made, FALSE);
		editor_app_set_status (self->app, "Created %s", lrg_node_get_name (made));
	}
	return made != NULL;
}

static gboolean
on_drop (LrgDropTarget  *target,
         LrgDragPayload *payload,
         gfloat          x,
         gfloat          y,
         gpointer        user_data)
{
	LrgEditorHierarchyPanel *self = LRG_EDITOR_HIERARCHY_PANEL (user_data);
	GObject                 *item = NULL;
	LrgDropPosition          pos = LRG_DROP_POSITION_INTO;
	LrgDragAction            action = lrg_drop_target_get_current_action (target);

	lrg_tree_view_get_drop_position (self->tree, y, &item, &pos);
	lrg_tree_view_clear_drop_indicator (self->tree);

	if (lrg_drag_payload_matches (payload, NODE_TYPE))
	{
		LrgNode *dragged = lrg_drag_payload_get_object (payload);

		if (!LRG_IS_NODE (dragged))
			return FALSE;
		/* Alt (the LINK action) keeps world transforms, like node.move --keep-world */
		return drop_nodes (self, dragged, LRG_NODE (item), pos, action == LRG_DRAG_ACTION_LINK);
	}
	if (lrg_drag_payload_matches (payload, ASSET_TYPE) || lrg_drag_payload_matches (payload, PREFAB_TYPE))
	{
		const gchar *path = lrg_drag_payload_get_string (payload);

		return path != NULL && drop_asset (self, path, LRG_NODE (item), pos);
	}
	return FALSE;
}

/* ==========================================================================
 * Context menu
 * ========================================================================== */

static void
append_action (GMenu       *menu,
               const gchar *label,
               const gchar *action)
{
	g_menu_append (menu, label, action);
}

/* "Create" items with @action (a string parameter, the kind id). */
static GMenuModel *
create_submenu (const gchar *action)
{
	GMenu                  *menu = g_menu_new ();
	g_autoptr(GMenu)        section = g_menu_new ();
	const EditorCreateItem *kinds;
	guint                   n, i;

	kinds = editor_create_items (&n);
	for (i = 0; i < n; i++)
	{
		g_autoptr(GMenuItem) item = NULL;

		if (kinds[i].id[0] == '-')
		{
			g_menu_append_section (menu, NULL, G_MENU_MODEL (section));
			g_clear_object (&section);
			section = g_menu_new ();
			continue;
		}
		item = g_menu_item_new (kinds[i].label, NULL);
		g_menu_item_set_action_and_target_value (item, action, g_variant_new_string (kinds[i].id));
		g_menu_append_item (section, item);
	}
	g_menu_append_section (menu, NULL, G_MENU_MODEL (section));
	return G_MENU_MODEL (menu);
}

/* The menu of a row: everything acts on the selection (which includes
 * @node) or on @node itself (hierarchy.* actions). */
static GMenuModel *
build_node_menu (LrgNode *node)
{
	GMenu               *menu = g_menu_new ();
	g_autoptr(GMenu)     edit = g_menu_new ();
	g_autoptr(GMenu)     clip = g_menu_new ();
	g_autoptr(GMenu)     structure = g_menu_new ();
	g_autoptr(GMenu)     view = g_menu_new ();
	g_autoptr(GMenu)     copy = g_menu_new ();
	g_autoptr(GMenuModel) create = create_submenu ("hierarchy.create-child");
	LrgNode             *owner = lrg_prefab_find_owner (node);

	append_action (edit, "Rename", "hierarchy.rename");
	append_action (edit, "Duplicate", "editor.edit.duplicate");
	append_action (edit, "Delete", "editor.edit.delete");
	g_menu_append_section (menu, NULL, G_MENU_MODEL (edit));

	append_action (clip, "Cut", "editor.edit.cut");
	append_action (clip, "Copy", "editor.edit.copy");
	append_action (clip, "Paste as Child", "hierarchy.paste-child");
	g_menu_append_section (menu, NULL, G_MENU_MODEL (clip));

	g_menu_append_submenu (structure, "Create Child", create);
	append_action (structure, "Group Selection", "editor.node.group");
	append_action (structure, "Ungroup", "editor.node.ungroup");
	append_action (structure, "Save as Prefab...", "editor.node.save-prefab");
	g_menu_append_section (menu, NULL, G_MENU_MODEL (structure));

	/* Prefab instances (linked) or 0.8.0 instances (unlinked markers) */
	if (owner != NULL)
	{
		g_autoptr(GMenu) prefab = g_menu_new ();

		append_action (prefab, "Select Source Prefab", "hierarchy.prefab-select-source");
		if (owner != node)
			append_action (prefab, "Select Instance Root", "hierarchy.prefab-select-root");
		append_action (prefab, "Revert All Overrides", "hierarchy.prefab-revert-all");
		append_action (prefab, "Apply All Overrides", "hierarchy.prefab-apply-all");
		append_action (prefab, "Unlink", "hierarchy.prefab-unlink");
		g_menu_append_section (menu, "Prefab", G_MENU_MODEL (prefab));
	}
	else if (legacy_prefab_source (node) != NULL)
	{
		g_autoptr(GMenu) prefab = g_menu_new ();

		append_action (prefab, "Select Source Prefab", "hierarchy.prefab-select-source");
		append_action (prefab, "Relink", "hierarchy.prefab-relink");
		g_menu_append_section (menu, "Prefab", G_MENU_MODEL (prefab));
	}

	append_action (view, "Focus Selection", "editor.view.focus");
	append_action (view, "Toggle Visibility", "editor.node.toggle-visible");
	append_action (view, "Toggle Lock", "editor.node.toggle-lock");
	append_action (view, "Attach Script...", "editor.node.attach-script");
	append_action (view, "Open Scripts in Editor", "editor.node.open-scripts");
	g_menu_append_section (menu, NULL, G_MENU_MODEL (view));

	append_action (copy, "Copy GUID", "hierarchy.copy-guid");
	append_action (copy, "Copy Path", "hierarchy.copy-path");
	g_menu_append_section (menu, NULL, G_MENU_MODEL (copy));
	return G_MENU_MODEL (menu);
}

/* The menu of the empty area: create at the root, paste, select all. */
static GMenuModel *
build_empty_menu (void)
{
	GMenu                *menu = g_menu_new ();
	g_autoptr(GMenu)      section = g_menu_new ();
	g_autoptr(GMenuModel) create = create_submenu ("hierarchy.create");

	g_menu_append_submenu (section, "Create", create);
	append_action (section, "Paste", "editor.edit.paste");
	append_action (section, "Select All", "editor.edit.select-all");
	g_menu_append_section (menu, NULL, G_MENU_MODEL (section));
	return G_MENU_MODEL (menu);
}

static void
refresh_group_state (LrgEditorHierarchyPanel *self)
{
	LrgNode  *node = self->menu_node;
	LrgNode  *owner = (node != NULL) ? lrg_prefab_find_owner (node) : NULL;
	gboolean  linked = owner != NULL;
	gboolean  overrides = linked && lrg_prefab_link_get_n_overrides (lrg_node_get_prefab_link (owner)) > 0;
	static const struct { const gchar *name; gint need; } table[] = {
		{ "rename", 1 }, { "paste-child", 1 }, { "create-child", 1 }, { "copy-guid", 1 }, { "copy-path", 1 },
		{ "prefab-select-source", 1 }, { "prefab-select-root", 2 }, { "prefab-revert-all", 3 },
		{ "prefab-apply-all", 3 }, { "prefab-unlink", 2 }, { "prefab-relink", 1 },
	};
	gsize i;

	for (i = 0; i < G_N_ELEMENTS (table); i++)
	{
		GAction *a = g_action_map_lookup_action (G_ACTION_MAP (self->group), table[i].name);
		gboolean on = node != NULL;

		if (table[i].need == 2)
			on = linked;
		else if (table[i].need == 3)
			on = overrides;
		g_simple_action_set_enabled (G_SIMPLE_ACTION (a), on);
	}
}

void
lrg_editor_hierarchy_panel_popup_context_menu (LrgEditorHierarchyPanel *self,
                                               LrgNode                 *node,
                                               gfloat                   x,
                                               gfloat                   y)
{
	g_autoptr(GMenuModel) model = NULL;

	g_return_if_fail (LRG_IS_EDITOR_HIERARCHY_PANEL (self));
	g_return_if_fail (node == NULL || LRG_IS_NODE (node));

	if (node != NULL && !lrg_editor_selection_contains (selection_of (self), node))
		lrg_editor_select (self->editor, node, FALSE);
	g_set_object (&self->menu_node, node);
	refresh_group_state (self);

	/* The editor's own actions have enabled states; refresh them so the
	 * menu shows Ungroup etc. correctly for this selection */
	lrg_editor_actions_refresh_state (lrg_editor_app_get_actions (self->app));

	model = (node != NULL) ? build_node_menu (node) : build_empty_menu ();
	if (self->menu == NULL)
	{
		self->menu = lrg_menu_new ();
		lrg_widget_set_name (LRG_WIDGET (self->menu), "hierarchy-menu");
	}
	lrg_menu_set_model (self->menu, model);
	lrg_menu_popup_at_point (self->menu, LRG_WIDGET (self), x, y);
}

LrgMenu *
lrg_editor_hierarchy_panel_get_context_menu (LrgEditorHierarchyPanel *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_HIERARCHY_PANEL (self), NULL);
	return self->menu;
}

/* --- the "hierarchy" action group (acts on menu_node) --- */

#define PANEL(d) LRG_EDITOR_HIERARCHY_PANEL (d)

static void
act_rename (GSimpleAction *a, GVariant *p, gpointer d)
{
	if (PANEL (d)->menu_node != NULL)
		lrg_editor_hierarchy_panel_start_rename (PANEL (d), PANEL (d)->menu_node);
}

static void
act_paste_child (GSimpleAction *a, GVariant *p, gpointer d)
{
	LrgEditorHierarchyPanel *self = PANEL (d);
	g_autofree gchar        *text = editor_app_get_clipboard (self->app);
	g_autoptr(GError)        error = NULL;
	g_autoptr(GPtrArray)     created = NULL;

	if (self->menu_node == NULL)
		return;
	/* System clipboard text that is not a node list: use the editor's */
	if (text == NULL || strstr (text, "nodes:") == NULL)
		g_clear_pointer (&text, g_free);
	created = lrg_editor_paste (self->editor, text, self->menu_node, &error);
	if (created == NULL)
		editor_app_notify (self->app, EDITOR_NOTIFY_WARNING, "Paste: %s",
		                   error != NULL ? error->message : "nothing to paste");
	else
		lrg_tree_view_expand (self->tree, G_OBJECT (self->menu_node));
}

static void
create_under (LrgEditorHierarchyPanel *self,
              const gchar             *kind,
              LrgNode                 *parent)
{
	LrgNode *node = editor_app_create_node_kind (self->app, kind, parent);

	if (node == NULL)
		return;
	lrg_editor_select (self->editor, node, FALSE);
	editor_app_set_status (self->app, "Created %s", lrg_node_get_name (node));
}

static void
act_create (GSimpleAction *a, GVariant *p, gpointer d)
{
	create_under (PANEL (d), g_variant_get_string (p, NULL), NULL);
}

static void
act_create_child (GSimpleAction *a, GVariant *p, gpointer d)
{
	create_under (PANEL (d), g_variant_get_string (p, NULL), PANEL (d)->menu_node);
}

static void
act_copy_guid (GSimpleAction *a, GVariant *p, gpointer d)
{
	LrgEditorHierarchyPanel *self = PANEL (d);

	if (self->menu_node == NULL)
		return;
	editor_app_set_clipboard (self->app, lrg_node_get_guid (self->menu_node));
	editor_app_set_status (self->app, "Copied GUID %s", lrg_node_get_guid (self->menu_node));
}

static void
act_copy_path (GSimpleAction *a, GVariant *p, gpointer d)
{
	LrgEditorHierarchyPanel *self = PANEL (d);
	g_autofree gchar        *path = NULL;

	if (self->menu_node == NULL)
		return;
	path = lrg_node_get_path (self->menu_node);
	editor_app_set_clipboard (self->app, path);
	editor_app_set_status (self->app, "Copied path %s", path);
}

/* The source prefab path of the menu node's instance (linked or not). */
static const gchar *
menu_prefab_path (LrgEditorHierarchyPanel *self)
{
	LrgNode *owner;

	if (self->menu_node == NULL)
		return NULL;
	owner = lrg_prefab_find_owner (self->menu_node);
	if (owner != NULL)
		return lrg_prefab_link_get_source_path (lrg_node_get_prefab_link (owner));
	return legacy_prefab_source (self->menu_node);
}

static void
act_prefab_select_source (GSimpleAction *a, GVariant *p, gpointer d)
{
	LrgEditorHierarchyPanel *self = PANEL (d);
	const gchar             *path = menu_prefab_path (self);
	g_autofree gchar        *rel = NULL;
	g_autoptr(GList)         methods = NULL;

	if (path == NULL)
		return;
	rel = editor_app_relative_path (self->app, path);

	/* The asset browser reveals it when it offers the method; the path
	 * goes to the clipboard either way */
	methods = lrg_editor_server_list_methods (lrg_editor_app_get_server (self->app));
	if (g_list_find_custom (methods, "asset.reveal", (GCompareFunc) g_strcmp0) != NULL)
	{
		g_autoptr(JsonBuilder) b = json_builder_new ();
		g_autoptr(JsonNode)    root = NULL;
		g_autofree gchar      *line = NULL;
		g_autofree gchar      *reply = NULL;

		json_builder_begin_object (b);
		json_builder_set_member_name (b, "id");
		json_builder_add_int_value (b, 0);
		json_builder_set_member_name (b, "method");
		json_builder_add_string_value (b, "asset.reveal");
		json_builder_set_member_name (b, "params");
		json_builder_begin_object (b);
		json_builder_set_member_name (b, "path");
		json_builder_add_string_value (b, rel);
		json_builder_end_object (b);
		json_builder_end_object (b);
		root = json_builder_get_root (b);
		line = json_to_string (root, FALSE);
		reply = lrg_editor_server_dispatch (lrg_editor_app_get_server (self->app), line);
	}
	editor_app_set_clipboard (self->app, rel);
	editor_app_set_status (self->app, "Source prefab: %s (path copied)", rel);
}

static void
act_prefab_select_root (GSimpleAction *a, GVariant *p, gpointer d)
{
	LrgEditorHierarchyPanel *self = PANEL (d);
	LrgNode                 *owner = (self->menu_node != NULL) ? lrg_prefab_find_owner (self->menu_node) : NULL;

	if (owner != NULL)
		lrg_editor_select (self->editor, owner, FALSE);
}

static void
act_prefab_revert_all (GSimpleAction *a, GVariant *p, gpointer d)
{
	LrgEditorHierarchyPanel *self = PANEL (d);
	LrgNode                 *owner = (self->menu_node != NULL) ? lrg_prefab_find_owner (self->menu_node) : NULL;
	g_autoptr(GError)        error = NULL;

	if (owner == NULL)
		return;
	if (!lrg_editor_prefab_revert (self->editor, owner, NULL, &error))
		editor_app_notify (self->app, EDITOR_NOTIFY_ERROR, "Revert: %s", error->message);
	else
		editor_app_set_status (self->app, "Reverted %s to its prefab", lrg_node_get_name (owner));
}

static void
act_prefab_apply_all (GSimpleAction *a, GVariant *p, gpointer d)
{
	LrgEditorHierarchyPanel *self = PANEL (d);
	LrgNode                 *owner = (self->menu_node != NULL) ? lrg_prefab_find_owner (self->menu_node) : NULL;
	g_autoptr(GError)        error = NULL;

	if (owner == NULL)
		return;
	if (!lrg_editor_prefab_apply (self->editor, owner, NULL, &error))
		editor_app_notify (self->app, EDITOR_NOTIFY_ERROR, "Apply: %s", error->message);
	else
		editor_app_notify (self->app, EDITOR_NOTIFY_SUCCESS, "Applied overrides of %s to its prefab",
		                   lrg_node_get_name (owner));
}

static void
act_prefab_unlink (GSimpleAction *a, GVariant *p, gpointer d)
{
	LrgEditorHierarchyPanel *self = PANEL (d);
	LrgNode                 *owner = (self->menu_node != NULL) ? lrg_prefab_find_owner (self->menu_node) : NULL;

	if (owner != NULL && lrg_editor_prefab_unlink (self->editor, owner))
		editor_app_set_status (self->app, "Unlinked %s from its prefab", lrg_node_get_name (owner));
}

static void
act_prefab_relink (GSimpleAction *a, GVariant *p, gpointer d)
{
	LrgEditorHierarchyPanel *self = PANEL (d);
	const gchar             *src = (self->menu_node != NULL) ? legacy_prefab_source (self->menu_node) : NULL;
	g_autofree gchar        *abs = (src != NULL) ? absolute_path (self, src) : NULL;
	g_autoptr(GError)        error = NULL;

	if (self->menu_node == NULL)
		return;
	if (!lrg_editor_prefab_relink (self->editor, self->menu_node, abs, &error))
		editor_app_notify (self->app, EDITOR_NOTIFY_ERROR, "Relink: %s", error->message);
	else
		editor_app_set_status (self->app, "Relinked %s", lrg_node_get_name (self->menu_node));
}

static void
add_group_actions (LrgEditorHierarchyPanel *self)
{
	static const GActionEntry entries[] = {
		{ "rename", act_rename, NULL, NULL, NULL, { 0 } },
		{ "paste-child", act_paste_child, NULL, NULL, NULL, { 0 } },
		{ "create", act_create, "s", NULL, NULL, { 0 } },
		{ "create-child", act_create_child, "s", NULL, NULL, { 0 } },
		{ "copy-guid", act_copy_guid, NULL, NULL, NULL, { 0 } },
		{ "copy-path", act_copy_path, NULL, NULL, NULL, { 0 } },
		{ "prefab-select-source", act_prefab_select_source, NULL, NULL, NULL, { 0 } },
		{ "prefab-select-root", act_prefab_select_root, NULL, NULL, NULL, { 0 } },
		{ "prefab-revert-all", act_prefab_revert_all, NULL, NULL, NULL, { 0 } },
		{ "prefab-apply-all", act_prefab_apply_all, NULL, NULL, NULL, { 0 } },
		{ "prefab-unlink", act_prefab_unlink, NULL, NULL, NULL, { 0 } },
		{ "prefab-relink", act_prefab_relink, NULL, NULL, NULL, { 0 } },
	};

	self->group = g_simple_action_group_new ();
	g_action_map_add_action_entries (G_ACTION_MAP (self->group), entries, G_N_ELEMENTS (entries), self);
	lrg_widget_insert_action_group (LRG_WIDGET (self), "hierarchy", G_ACTION_GROUP (self->group));
}

/* The "+" button: the create menu for the top level. */
static void
on_add_clicked (LrgButton *button,
                gpointer   user_data)
{
	LrgEditorHierarchyPanel *self = LRG_EDITOR_HIERARCHY_PANEL (user_data);
	g_autoptr(GMenuModel)    model = create_submenu ("hierarchy.create");

	g_clear_object (&self->menu_node);
	if (self->menu == NULL)
	{
		self->menu = lrg_menu_new ();
		lrg_widget_set_name (LRG_WIDGET (self->menu), "hierarchy-menu");
	}
	lrg_menu_set_model (self->menu, model);
	lrg_menu_popup_at_widget (self->menu, LRG_WIDGET (button), LRG_POPUP_PLACEMENT_BELOW);
}

/* ==========================================================================
 * Live tab
 * ========================================================================== */

static void
set_live_selection (LrgEditorHierarchyPanel *self,
                    GObject                 *object)
{
	if (self->live_selection == object)
		return;
	g_set_object (&self->live_selection, object);
	g_signal_emit (self, signals[SIGNAL_LIVE_SELECTED], 0, object);
}

static void
on_live_selection_changed (LrgTreeView *view,
                           gpointer     user_data)
{
	set_live_selection (LRG_EDITOR_HIERARCHY_PANEL (user_data), lrg_tree_view_get_selected_item (view));
}

static void
update_page_visibility (LrgEditorHierarchyPanel *self)
{
	lrg_widget_set_visible (LRG_WIDGET (self->tree), !self->show_live);
	lrg_widget_set_visible (LRG_WIDGET (self->live), self->show_live);
	lrg_widget_queue_layout (LRG_WIDGET (self));
}

static void
on_play_started (LrgEditorPlaySession *play,
                 gpointer              user_data)
{
	LrgEditorHierarchyPanel *self = LRG_EDITOR_HIERARCHY_PANEL (user_data);

	self->playing = TRUE;
	lrg_widget_queue_layout (LRG_WIDGET (self));
}

static void
on_play_stopped (LrgEditorPlaySession *play,
                 gpointer              user_data)
{
	LrgEditorHierarchyPanel *self = LRG_EDITOR_HIERARCHY_PANEL (user_data);

	self->playing = FALSE;
	self->show_live = FALSE;
	lrg_tree_view_unselect_all (self->live);
	set_live_selection (self, NULL);
	update_page_visibility (self);
}

const gchar *
lrg_editor_hierarchy_panel_get_page (LrgEditorHierarchyPanel *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_HIERARCHY_PANEL (self), "scene");
	return self->show_live ? "live" : "scene";
}

gboolean
lrg_editor_hierarchy_panel_set_page (LrgEditorHierarchyPanel *self,
                                     const gchar             *page)
{
	gboolean live;

	g_return_val_if_fail (LRG_IS_EDITOR_HIERARCHY_PANEL (self), FALSE);
	g_return_val_if_fail (page != NULL, FALSE);

	live = g_strcmp0 (page, "live") == 0;
	if (live && !self->playing)
		return FALSE;
	if (live != self->show_live)
	{
		self->show_live = live;
		update_page_visibility (self);
	}
	return TRUE;
}

GObject *
lrg_editor_hierarchy_panel_get_live_selection (LrgEditorHierarchyPanel *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_HIERARCHY_PANEL (self), NULL);
	return self->live_selection;
}

/* ==========================================================================
 * Registry actions (node.rename, edit.find)
 * ========================================================================== */

LrgEditorHierarchyPanel *
lrg_editor_hierarchy_panel_find (LrgEditorApp *app)
{
	g_return_val_if_fail (LRG_IS_EDITOR_APP (app), NULL);
	return g_object_get_data (G_OBJECT (app), PANEL_KEY);
}

static void
a_rename (gpointer data)
{
	LrgEditorApp            *app = LRG_EDITOR_APP (data);
	LrgEditorHierarchyPanel *self = lrg_editor_hierarchy_panel_find (app);
	LrgNode                 *n = lrg_editor_selection_get_primary (lrg_editor_get_selection (lrg_editor_app_get_editor (app)));

	if (self == NULL || n == NULL)
		return;
	editor_app_show_panel (app, "hierarchy");
	if (!lrg_editor_hierarchy_panel_start_rename (self, n))
		editor_app_set_status (app, "Cannot rename %s here", lrg_node_get_name (n));
}

static gboolean
a_rename_enabled (gpointer data)
{
	LrgEditorApp *app = LRG_EDITOR_APP (data);

	return lrg_editor_selection_get_primary (lrg_editor_get_selection (lrg_editor_app_get_editor (app))) != NULL;
}

static void
a_find (gpointer data)
{
	LrgEditorApp            *app = LRG_EDITOR_APP (data);
	LrgEditorHierarchyPanel *self = lrg_editor_hierarchy_panel_find (app);

	editor_app_show_panel (app, "hierarchy");
	if (self != NULL)
		lrg_editor_hierarchy_panel_focus_search (self);
}

/* ==========================================================================
 * IPC: hierarchy.reveal, hierarchy.rename, hierarchy.filter, hierarchy.state
 * ========================================================================== */

static LrgEditorHierarchyPanel *
ipc_panel (gpointer   user_data,
           GError   **error)
{
	LrgEditorHierarchyPanel *self = lrg_editor_hierarchy_panel_find (LRG_EDITOR_APP (user_data));

	if (self == NULL)
		g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_FAILED, "no hierarchy panel");
	return self;
}

/* The node named by "guid" (or "node", a GUID or name path). */
static LrgNode *
ipc_node (LrgEditorHierarchyPanel  *self,
          JsonObject               *params,
          GError                  **error)
{
	const gchar *ref = NULL;
	LrgNode     *node;

	if (params != NULL && json_object_has_member (params, "guid"))
		ref = json_object_get_string_member (params, "guid");
	else if (params != NULL && json_object_has_member (params, "node"))
		ref = json_object_get_string_member (params, "node");
	if (ref == NULL)
	{
		g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS, "missing \"guid\"");
		return NULL;
	}
	node = lrg_editor_find_node (self->editor, ref);
	if (node == NULL)
		g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_NOT_FOUND, "no node \"%s\"", ref);
	return node;
}

static void
add_rect (JsonBuilder *b,
          const gchar *name,
          gfloat       x,
          gfloat       y,
          gfloat       w,
          gfloat       h)
{
	json_builder_set_member_name (b, name);
	json_builder_begin_array (b);
	json_builder_add_double_value (b, x);
	json_builder_add_double_value (b, y);
	json_builder_add_double_value (b, w);
	json_builder_add_double_value (b, h);
	json_builder_end_array (b);
}

static void
add_row_state (LrgEditorHierarchyPanel *self,
               JsonBuilder             *b,
               LrgNode                 *node)
{
	gfloat x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f;
	guint  pos = lrg_tree_view_get_position (self->tree, G_OBJECT (node));

	json_builder_set_member_name (b, "guid");
	json_builder_add_string_value (b, lrg_node_get_guid (node));
	json_builder_set_member_name (b, "row");
	if (pos == LRG_TREE_VIEW_INVALID_POSITION)
		json_builder_add_null_value (b);
	else
		json_builder_add_int_value (b, pos);
	if (lrg_tree_view_get_row_rect (self->tree, G_OBJECT (node), &x, &y, &w, &h))
		add_rect (b, "rect", x, y, w, h);
}

static JsonNode *
m_reveal (LrgEditorServer  *server,
          JsonObject       *params,
          gpointer          user_data,
          GError          **error)
{
	LrgEditorHierarchyPanel *self = ipc_panel (user_data, error);
	g_autoptr(JsonBuilder)   b = NULL;
	LrgNode                 *node;
	gboolean                 select = TRUE;
	gboolean                 shown;

	if (self == NULL || (node = ipc_node (self, params, error)) == NULL)
		return NULL;
	if (json_object_has_member (params, "select"))
		select = json_object_get_boolean_member (params, "select");

	editor_app_show_panel (self->app, "hierarchy");
	shown = lrg_editor_hierarchy_panel_reveal (self, node, select);

	b = json_builder_new ();
	json_builder_begin_object (b);
	add_row_state (self, b, node);
	json_builder_set_member_name (b, "revealed");
	json_builder_add_boolean_value (b, shown);
	json_builder_end_object (b);
	return json_builder_get_root (b);
}

static JsonNode *
m_rename (LrgEditorServer  *server,
          JsonObject       *params,
          gpointer          user_data,
          GError          **error)
{
	LrgEditorHierarchyPanel *self = ipc_panel (user_data, error);
	g_autoptr(JsonBuilder)   b = NULL;
	LrgNode                 *node;
	gboolean                 editing;

	if (self == NULL || (node = ipc_node (self, params, error)) == NULL)
		return NULL;

	/* With a name: rename now (undoable), no field */
	if (json_object_has_member (params, "name"))
	{
		const gchar *name = json_object_get_string_member (params, "name");

		if (name == NULL || *name == '\0')
		{
			g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS, "empty name");
			return NULL;
		}
		lrg_editor_rename_node (self->editor, node, name);
		editing = FALSE;
	}
	else
	{
		editor_app_show_panel (self->app, "hierarchy");
		lrg_editor_select (self->editor, node, FALSE);
		editing = lrg_editor_hierarchy_panel_start_rename (self, node);
	}

	b = json_builder_new ();
	json_builder_begin_object (b);
	json_builder_set_member_name (b, "guid");
	json_builder_add_string_value (b, lrg_node_get_guid (node));
	json_builder_set_member_name (b, "name");
	json_builder_add_string_value (b, lrg_node_get_name (node));
	json_builder_set_member_name (b, "editing");
	json_builder_add_boolean_value (b, editing);
	json_builder_end_object (b);
	return json_builder_get_root (b);
}

static JsonNode *
m_filter (LrgEditorServer  *server,
          JsonObject       *params,
          gpointer          user_data,
          GError          **error)
{
	LrgEditorHierarchyPanel *self = ipc_panel (user_data, error);
	JsonNode                *n;

	if (self == NULL)
		return NULL;
	lrg_editor_hierarchy_panel_set_filter (self, (params != NULL && json_object_has_member (params, "text"))
	                                             ? json_object_get_string_member (params, "text") : NULL);
	n = json_node_new (JSON_NODE_VALUE);
	json_node_set_int (n, lrg_editor_node_model_get_n_matches (self->model));
	return n;
}

static JsonNode *
m_state (LrgEditorServer  *server,
         JsonObject       *params,
         gpointer          user_data,
         GError          **error)
{
	LrgEditorHierarchyPanel *self = ipc_panel (user_data, error);
	g_autoptr(JsonBuilder)   b = NULL;
	GObject                 *editing;
	guint                    n, i;

	if (self == NULL)
		return NULL;

	b = json_builder_new ();
	json_builder_begin_object (b);
	json_builder_set_member_name (b, "page");
	json_builder_add_string_value (b, lrg_editor_hierarchy_panel_get_page (self));
	json_builder_set_member_name (b, "live");
	json_builder_add_boolean_value (b, self->playing);
	json_builder_set_member_name (b, "filter");
	json_builder_add_string_value (b, lrg_editor_node_model_get_filter (self->model));
	json_builder_set_member_name (b, "matches");
	json_builder_add_int_value (b, lrg_editor_node_model_get_n_matches (self->model));
	editing = lrg_tree_view_get_editing_item (self->tree);
	json_builder_set_member_name (b, "editing");
	if (editing != NULL)
		json_builder_add_string_value (b, lrg_node_get_guid (LRG_NODE (editing)));
	else
		json_builder_add_null_value (b);
	add_rect (b, "tree", lrg_widget_get_world_x (LRG_WIDGET (self->tree)), lrg_widget_get_world_y (LRG_WIDGET (self->tree)),
	          lrg_widget_get_width (LRG_WIDGET (self->tree)), lrg_widget_get_height (LRG_WIDGET (self->tree)));
	if (self->playing && self->tabs_w > 0.0f)
	{
		gfloat wx = lrg_widget_get_world_x (LRG_WIDGET (self));
		gfloat wy = lrg_widget_get_world_y (LRG_WIDGET (self));

		/* Two halves: Scene, Live */
		add_rect (b, "tab-scene", wx + self->tabs_x, wy + self->tabs_y, self->tabs_w * 0.5f, self->tabs_h);
		add_rect (b, "tab-live", wx + self->tabs_x + self->tabs_w * 0.5f, wy + self->tabs_y, self->tabs_w * 0.5f,
		          self->tabs_h);
	}

	/* The flattened rows: what a client can click */
	json_builder_set_member_name (b, "rows");
	json_builder_begin_array (b);
	n = lrg_tree_view_get_n_visible (self->tree);
	for (i = 0; i < n; i++)
	{
		LrgNode *node = LRG_NODE (lrg_tree_view_get_item (self->tree, i));

		json_builder_begin_object (b);
		add_row_state (self, b, node);
		json_builder_set_member_name (b, "name");
		json_builder_add_string_value (b, lrg_node_get_name (node));
		json_builder_set_member_name (b, "depth");
		json_builder_add_int_value (b, lrg_tree_view_get_depth (self->tree, i));
		/* the physics badge: body kind nick, null without a body */
		json_builder_set_member_name (b, "physics");
		if (lrg_node_get_physics (node) != NULL)
		{
			g_autoptr(GEnumClass) kinds = g_type_class_ref (LRG_TYPE_DYNAMICS_BODY_KIND);
			GEnumValue           *ev = g_enum_get_value (kinds,
			                                             (gint) lrg_node_physics_get_body_kind (lrg_node_get_physics (node)));

			json_builder_add_string_value (b, ev != NULL ? ev->value_nick : "unknown");
		}
		else
			json_builder_add_null_value (b);
		/* the navigation badge: a nav-agent component (0.9.0 M6) */
		json_builder_set_member_name (b, "nav_agent");
		json_builder_add_boolean_value (b, lrg_node_find_component (node, "nav-agent") != NULL);
		json_builder_set_member_name (b, "expanded");
		json_builder_add_boolean_value (b, lrg_tree_view_is_expanded (self->tree, G_OBJECT (node)));
		json_builder_set_member_name (b, "selected");
		json_builder_add_boolean_value (b, lrg_tree_view_is_item_selected (self->tree, G_OBJECT (node)));
		json_builder_end_object (b);
	}
	json_builder_end_array (b);
	json_builder_end_object (b);
	return json_builder_get_root (b);
}

static void
register_methods (LrgEditorHierarchyPanel *self)
{
	LrgEditorServer *server = lrg_editor_app_get_server (self->app);

	if (server == NULL)
		return;
	/* user_data is the app (it owns the server): the panel is looked up
	 * per call, so the methods fail cleanly once the panel is gone */
	lrg_editor_server_register_method (server, "hierarchy.reveal", m_reveal, self->app, NULL);
	lrg_editor_server_register_method (server, "hierarchy.rename", m_rename, self->app, NULL);
	lrg_editor_server_register_method (server, "hierarchy.filter", m_filter, self->app, NULL);
	lrg_editor_server_register_method (server, "hierarchy.state", m_state, self->app, NULL);
}

gboolean
lrg_editor_hierarchy_panel_reveal (LrgEditorHierarchyPanel *self,
                                   LrgNode                 *node,
                                   gboolean                 select)
{
	gboolean shown;

	g_return_val_if_fail (LRG_IS_EDITOR_HIERARCHY_PANEL (self), FALSE);
	g_return_val_if_fail (LRG_IS_NODE (node), FALSE);

	lrg_editor_hierarchy_panel_set_page (self, "scene");
	unfilter_for (self, node);
	if (select)
		lrg_editor_select (self->editor, node, FALSE);
	shown = lrg_tree_view_reveal (self->tree, G_OBJECT (node));
	if (lrg_widget_get_height (LRG_WIDGET (self->tree)) <= 0.0f)
		self->reveal_pending = TRUE;
	return shown;
}

/* ==========================================================================
 * Layout, paint, events
 * ========================================================================== */

static void
lrg_editor_hierarchy_panel_layout_children (LrgContainer *container)
{
	LrgEditorHierarchyPanel *self = LRG_EDITOR_HIERARCHY_PANEL (container);
	LrgWidget               *widget = LRG_WIDGET (self);
	gfloat                   w = lrg_widget_get_width (widget);
	gfloat                   h = lrg_widget_get_height (widget);
	gfloat                   pad = theme_metric (widget, LRG_THEME_METRIC_PADDING_Y) + 2.0f;
	gfloat                   gap = theme_metric (widget, LRG_THEME_METRIC_SPACING);
	gfloat                   ch = theme_metric (widget, LRG_THEME_METRIC_CONTROL_HEIGHT);
	gfloat                   y = pad;

	/* Children are added one by one in _new(): wait for all of them */
	if (self->search == NULL || self->add_button == NULL || self->tree == NULL || self->live == NULL)
		return;

	/* Search field and the "+" button */
	lrg_widget_place_in_slot (LRG_WIDGET (self->search), pad, y, MAX (20.0f, w - pad * 2.0f - ch - gap), ch);
	lrg_widget_place_in_slot (LRG_WIDGET (self->add_button), w - pad - ch, y, ch, ch);
	y += ch + gap;

	/* Scene | Live tabs while playing */
	if (self->playing)
	{
		self->tabs_x = pad;
		self->tabs_y = y;
		self->tabs_w = MAX (20.0f, w - pad * 2.0f);
		self->tabs_h = ch * 0.9f;
		y += self->tabs_h + gap;
	}
	else
		self->tabs_w = self->tabs_h = 0.0f;

	/* Info line */
	self->info_y = y;
	self->info_h = theme_metric (widget, LRG_THEME_METRIC_FONT_SIZE_SMALL) + 6.0f;
	y += self->info_h + gap * 0.5f;

	lrg_widget_place_in_slot (LRG_WIDGET (self->tree), 0.0f, y, w, MAX (0.0f, h - y));
	lrg_widget_place_in_slot (LRG_WIDGET (self->live), 0.0f, y, w, MAX (0.0f, h - y));
}

static void
paint_tabs (LrgEditorHierarchyPanel *self,
            LrgPaintList            *list)
{
	LrgWidget      *widget = LRG_WIDGET (self);
	LrgTheme       *theme = lrg_widget_get_theme (widget);
	GrlFont        *font = lrg_theme_get_ui_font (theme);
	gfloat          wx = lrg_widget_get_world_x (widget);
	gfloat          wy = lrg_widget_get_world_y (widget);
	gfloat          x = wx + self->tabs_x;
	gfloat          y = wy + self->tabs_y;
	gfloat          half = self->tabs_w * 0.5f;
	gfloat          fs = lrg_theme_get_metric (theme, LRG_THEME_METRIC_FONT_SIZE_SMALL);
	gfloat          radius = self->tabs_h * 0.5f;
	static const gchar *names[] = { "Scene", "Live" };
	guint           i;

	/* A pill track with the active half raised in the accent colour */
	lrg_paint_list_rect (list, x, y, self->tabs_w, self->tabs_h, radius, theme_color (widget, LRG_THEME_COLOR_TRACK));
	lrg_paint_list_rect (list, x + (self->show_live ? half : 0.0f) + 2.0f, y + 2.0f, half - 4.0f, self->tabs_h - 4.0f,
	                     radius - 2.0f, theme_color (widget, LRG_THEME_COLOR_ACCENT));
	for (i = 0; i < 2; i++)
	{
		gboolean        active = (i == 1) == self->show_live;
		const GrlColor *fg = theme_color (widget, active ? LRG_THEME_COLOR_TEXT_ON_ACCENT : LRG_THEME_COLOR_TEXT_MUTED);
		gfloat          tw = 0.0f;

		lrg_paint_measure_text (font, names[i], fs, 1.0f, &tw, NULL);
		lrg_paint_list_text (list, font, names[i], x + half * (gfloat) i + (half - tw) * 0.5f,
		                     y + (self->tabs_h - fs) * 0.5f, fs, 1.0f, fg);
	}
	/* A pulsing dot on "Live": the simulation runs */
	lrg_paint_list_circle (list, x + half + 10.0f, y + self->tabs_h * 0.5f, 3.0f,
	                       theme_color (widget, self->show_live ? LRG_THEME_COLOR_TEXT_ON_ACCENT : LRG_THEME_COLOR_DANGER));
}

static void
lrg_editor_hierarchy_panel_paint (LrgWidget    *widget,
                                  LrgPaintList *list)
{
	LrgEditorHierarchyPanel *self = LRG_EDITOR_HIERARCHY_PANEL (widget);
	LrgTheme                *theme = lrg_widget_get_theme (widget);
	GrlFont                 *font = lrg_theme_get_ui_font (theme);
	gfloat                   wx = lrg_widget_get_world_x (widget);
	gfloat                   wy = lrg_widget_get_world_y (widget);
	gfloat                   pad = theme_metric (widget, LRG_THEME_METRIC_PADDING_Y) + 2.0f;
	gfloat                   fs = lrg_theme_get_metric (theme, LRG_THEME_METRIC_FONT_SIZE_SMALL);
	const GrlColor          *muted = theme_color (widget, LRG_THEME_COLOR_TEXT_MUTED);
	g_autofree gchar        *info = NULL;
	LrgLevel                *level = lrg_editor_get_level (self->editor);
	const gchar             *lname = lrg_level_get_name (level);

	if (self->playing && self->tabs_w > 0.0f)
		paint_tabs (self, list);

	/* Info line: level name and count, or matches while filtering, or the
	 * live object count */
	if (self->show_live)
		info = g_strdup_printf ("Live objects: %u", lrg_editor_play_session_get_n_objects (self->play));
	else if (lrg_editor_node_model_get_filter (self->model) != NULL)
		info = g_strdup_printf ("%u match%s of %u nodes", lrg_editor_node_model_get_n_matches (self->model),
		                        lrg_editor_node_model_get_n_matches (self->model) == 1 ? "" : "es",
		                        lrg_level_count_nodes (level));
	else
		info = g_strdup_printf ("%s  \xc2\xb7  %u node%s", (lname != NULL && *lname != '\0') ? lname : "Level",
		                        lrg_editor_node_model_get_n_matches (self->model),
		                        lrg_editor_node_model_get_n_matches (self->model) == 1 ? "" : "s");
	lrg_paint_list_text (list, font, info, wx + pad + 2.0f, wy + self->info_y + (self->info_h - fs) * 0.5f, fs, 1.0f,
	                     muted);
	lrg_paint_list_rect (list, wx, wy + self->info_y + self->info_h + 1.0f, lrg_widget_get_width (widget), 1.0f, 0.0f,
	                     theme_color (widget, LRG_THEME_COLOR_BORDER));

	LRG_WIDGET_CLASS (lrg_editor_hierarchy_panel_parent_class)->paint (widget, list);

	/* Empty states over the tree */
	if (!self->show_live && lrg_tree_view_get_n_visible (self->tree) == 0)
	{
		const gchar *hint = (lrg_editor_node_model_get_filter (self->model) != NULL)
		                    ? "No node matches the filter"
		                    : "Empty level. Use + or right click to create nodes.";
		gfloat       tw = 0.0f;

		lrg_paint_measure_text (font, hint, fs, 1.0f, &tw, NULL);
		lrg_paint_list_text (list, font, hint,
		                     wx + MAX (pad, (lrg_widget_get_width (widget) - tw) * 0.5f),
		                     lrg_widget_get_world_y (LRG_WIDGET (self->tree)) + fs * 2.0f, fs, 1.0f, muted);
	}
}

static gboolean
lrg_editor_hierarchy_panel_handle_event (LrgWidget        *widget,
                                         const LrgUIEvent *event)
{
	LrgEditorHierarchyPanel *self = LRG_EDITOR_HIERARCHY_PANEL (widget);
	gfloat                   x = lrg_ui_event_get_x (event);
	gfloat                   y = lrg_ui_event_get_y (event);
	gfloat                   wx = lrg_widget_get_world_x (widget);
	gfloat                   wy = lrg_widget_get_world_y (widget);
	gint                     button;

	if (lrg_ui_event_get_event_type (event) != LRG_UI_EVENT_MOUSE_BUTTON_DOWN)
		return FALSE;
	button = lrg_ui_event_get_button (event);

	/* Tabs */
	if (self->playing && self->tabs_w > 0.0f && button == GRL_MOUSE_BUTTON_LEFT &&
	    x >= wx + self->tabs_x && x < wx + self->tabs_x + self->tabs_w &&
	    y >= wy + self->tabs_y && y < wy + self->tabs_y + self->tabs_h)
	{
		lrg_editor_hierarchy_panel_set_page (self, x >= wx + self->tabs_x + self->tabs_w * 0.5f ? "live" : "scene");
		return TRUE;
	}

	if (self->show_live)
		return FALSE;

	/* Presses the tree did not take: below the rows, or right clicks */
	if (y >= lrg_widget_get_world_y (LRG_WIDGET (self->tree)))
	{
		GObject *item = lrg_tree_view_get_item_at (self->tree, x, y);

		if (button == GRL_MOUSE_BUTTON_RIGHT)
		{
			lrg_editor_hierarchy_panel_popup_context_menu (self, item != NULL ? LRG_NODE (item) : NULL, x, y);
			return TRUE;
		}
		if (button == GRL_MOUSE_BUTTON_LEFT && item == NULL)
		{
			lrg_editor_select (self->editor, NULL, FALSE);
			return TRUE;
		}
	}
	return FALSE;
}

static gboolean
lrg_editor_hierarchy_panel_step (LrgWidget *widget,
                                 gfloat     delta_time)
{
	LrgEditorHierarchyPanel *self = LRG_EDITOR_HIERARCHY_PANEL (widget);
	gboolean                 busy = FALSE;

	if (LRG_WIDGET_CLASS (lrg_editor_hierarchy_panel_parent_class)->step != NULL)
		busy = LRG_WIDGET_CLASS (lrg_editor_hierarchy_panel_parent_class)->step (widget, delta_time);

	/* Work that needed a laid-out tree */
	if (lrg_widget_get_height (LRG_WIDGET (self->tree)) > 0.0f)
	{
		if (self->reveal_pending)
		{
			LrgNode *primary = lrg_editor_selection_get_primary (selection_of (self));

			self->reveal_pending = FALSE;
			if (primary != NULL && lrg_editor_node_model_is_shown (self->model, primary))
				lrg_tree_view_reveal (self->tree, G_OBJECT (primary));
		}
		if (self->pending_rename != NULL)
		{
			g_autoptr(LrgNode) node = g_steal_pointer (&self->pending_rename);

			if (lrg_node_get_parent (node) != NULL && lrg_tree_view_reveal (self->tree, G_OBJECT (node)))
				lrg_tree_view_start_editing (self->tree, G_OBJECT (node));
		}
	}
	return busy;
}

/* ==========================================================================
 * GObject
 * ========================================================================== */

static void
lrg_editor_hierarchy_panel_dispose (GObject *object)
{
	LrgEditorHierarchyPanel *self = LRG_EDITOR_HIERARCHY_PANEL (object);

	if (self->app != NULL && g_object_get_data (G_OBJECT (self->app), PANEL_KEY) == self)
		g_object_set_data (G_OBJECT (self->app), PANEL_KEY, NULL);
	g_clear_weak_pointer (&self->app);

	if (self->editor != NULL)
	{
		g_clear_signal_handler (&self->selection_id, lrg_editor_get_selection (self->editor));
		g_clear_signal_handler (&self->replaced_id, self->editor);
		g_clear_object (&self->editor);
	}
	if (self->play != NULL)
	{
		g_clear_signal_handler (&self->started_id, self->play);
		g_clear_signal_handler (&self->stopped_id, self->play);
		g_clear_object (&self->play);
	}
	if (self->menu != NULL)
	{
		lrg_menu_close (self->menu);
		g_clear_object (&self->menu);
	}
	g_clear_object (&self->menu_node);
	g_clear_object (&self->pending_rename);
	g_clear_object (&self->live_selection);
	g_clear_object (&self->group);
	g_clear_object (&self->model);

	G_OBJECT_CLASS (lrg_editor_hierarchy_panel_parent_class)->dispose (object);
}

static void
lrg_editor_hierarchy_panel_class_init (LrgEditorHierarchyPanelClass *klass)
{
	GObjectClass   *object_class = G_OBJECT_CLASS (klass);
	LrgWidgetClass *widget_class = LRG_WIDGET_CLASS (klass);

	object_class->dispose = lrg_editor_hierarchy_panel_dispose;
	widget_class->paint = lrg_editor_hierarchy_panel_paint;
	widget_class->handle_event = lrg_editor_hierarchy_panel_handle_event;
	widget_class->step = lrg_editor_hierarchy_panel_step;
	LRG_CONTAINER_CLASS (klass)->layout_children = lrg_editor_hierarchy_panel_layout_children;

	/**
	 * LrgEditorHierarchyPanel::live-selected:
	 * @self: the panel
	 * @object: (nullable): the selected live object (an #LrgGameObject
	 *   of a 2D play world or an #LrgEntity3D of a 3D one), or %NULL
	 *
	 * Emitted when the selection on the Live tab changes, and with %NULL
	 * when play stops. The inspector shows the object while it is set
	 * (lrg_editor_hierarchy_panel_get_live_selection() reads it later).
	 */
	signals[SIGNAL_LIVE_SELECTED] =
		g_signal_new ("live-selected",
		              G_TYPE_FROM_CLASS (klass),
		              G_SIGNAL_RUN_LAST,
		              0, NULL, NULL, NULL,
		              G_TYPE_NONE, 1, G_TYPE_OBJECT);
}

static void
lrg_editor_hierarchy_panel_init (LrgEditorHierarchyPanel *self)
{
	lrg_widget_set_name (LRG_WIDGET (self), "hierarchy-panel");
}

static void
on_level_replaced (LrgEditor *editor,
                   LrgLevel  *level,
                   gpointer   user_data)
{
	LrgEditorHierarchyPanel *self = LRG_EDITOR_HIERARCHY_PANEL (user_data);

	/* The model already swapped its root; nodes from the old level are
	 * gone, and the filter carries over */
	g_clear_object (&self->menu_node);
	g_clear_object (&self->pending_rename);
	if (lrg_editor_node_model_get_filter (self->model) != NULL)
		expand_shown (self, lrg_level_get_root (level));
	sync_tree_from_editor (self, TRUE);
}

LrgEditorHierarchyPanel *
lrg_editor_hierarchy_panel_new (LrgEditorApp *app)
{
	LrgEditorHierarchyPanel *self;
	const gchar             *types[] = { NODE_TYPE, ASSET_TYPE, PREFAB_TYPE, NULL };
	LrgDropTarget           *target;

	g_return_val_if_fail (LRG_IS_EDITOR_APP (app), NULL);

	self = g_object_new (LRG_TYPE_EDITOR_HIERARCHY_PANEL, NULL);
	g_set_weak_pointer (&self->app, app);
	self->editor = g_object_ref (lrg_editor_app_get_editor (app));
	self->play = g_object_ref (lrg_editor_app_get_play_session (app));
	self->model = lrg_editor_node_model_new (self->editor);
	g_object_set_data (G_OBJECT (app), PANEL_KEY, self);

	/* Search field and the create button */
	self->search = lrg_text_input_new_with_placeholder ("Filter nodes");
	lrg_widget_set_name (LRG_WIDGET (self->search), "hierarchy-search");
	lrg_container_add_child (LRG_CONTAINER (self), LRG_WIDGET (self->search));
	g_object_unref (self->search);
	g_signal_connect (self->search, "text-changed", G_CALLBACK (on_search_changed), self);

	self->add_button = lrg_button_new ("+");
	lrg_widget_set_name (LRG_WIDGET (self->add_button), "hierarchy-add");
	lrg_container_add_child (LRG_CONTAINER (self), LRG_WIDGET (self->add_button));
	g_object_unref (self->add_button);
	g_signal_connect (self->add_button, "clicked", G_CALLBACK (on_add_clicked), self);

	/* The tree */
	self->tree = lrg_tree_view_new ();
	lrg_widget_set_name (LRG_WIDGET (self->tree), "hierarchy-tree");
	lrg_tree_view_set_selection_mode (self->tree, LRG_SELECTION_MODE_MULTIPLE);
	lrg_tree_view_set_indent (self->tree, 16.0f);
	lrg_tree_view_set_factory (self->tree, tree_create_row, tree_bind_row, self, NULL);
	lrg_editor_node_model_bind_tree_view (self->model, self->tree);
	lrg_container_add_child (LRG_CONTAINER (self), LRG_WIDGET (self->tree));
	g_object_unref (self->tree);
	g_signal_connect (self->tree, "selection-changed", G_CALLBACK (on_tree_selection_changed), self);
	g_signal_connect (self->tree, "edit-label", G_CALLBACK (on_edit_label), self);
	g_signal_connect (self->tree, "row-activated", G_CALLBACK (on_row_activated), self);

	target = lrg_drop_target_new (types, LRG_DRAG_ACTION_ALL);
	lrg_drop_target_set_paint_highlight (target, FALSE);
	g_signal_connect (target, "motion", G_CALLBACK (on_drop_motion), self);
	g_signal_connect (target, "leave", G_CALLBACK (on_drop_leave), self);
	g_signal_connect (target, "drop", G_CALLBACK (on_drop), self);
	lrg_widget_add_drop_target (LRG_WIDGET (self->tree), target);

	/* The live list (shown on the Live tab while playing) */
	self->live = lrg_tree_view_new ();
	lrg_widget_set_name (LRG_WIDGET (self->live), "hierarchy-live");
	lrg_tree_view_set_factory (self->live, live_create_row, live_bind_row, self, NULL);
	lrg_tree_view_set_children_func (self->live, live_children, self, NULL);
	lrg_tree_view_set_model (self->live, lrg_editor_play_session_get_live_model (self->play));
	lrg_container_add_child (LRG_CONTAINER (self), LRG_WIDGET (self->live));
	g_object_unref (self->live);
	g_signal_connect (self->live, "selection-changed", G_CALLBACK (on_live_selection_changed), self);
	update_page_visibility (self);

	/* Editor and play session */
	self->selection_id = g_signal_connect (lrg_editor_get_selection (self->editor), "changed",
	                                       G_CALLBACK (on_editor_selection_changed), self);
	self->replaced_id = g_signal_connect_after (self->editor, "level-replaced", G_CALLBACK (on_level_replaced), self);
	self->started_id = g_signal_connect (self->play, "started", G_CALLBACK (on_play_started), self);
	self->stopped_id = g_signal_connect (self->play, "stopped", G_CALLBACK (on_play_stopped), self);
	self->playing = lrg_editor_play_session_is_playing (self->play);

	/* Actions: the panel's own group, and the placeholders it replaces */
	add_group_actions (self);
	lrg_editor_actions_add (lrg_editor_app_get_actions (app), "node.rename", "Rename", "Node", "F2",
	                        a_rename, a_rename_enabled, NULL, app);
	lrg_editor_actions_add (lrg_editor_app_get_actions (app), "edit.find", "Find in Hierarchy", "Edit", "Ctrl+F",
	                        a_find, NULL, NULL, app);
	register_methods (self);

	sync_tree_from_editor (self, TRUE);
	return self;
}

LrgEditorApp *
lrg_editor_hierarchy_panel_get_app (LrgEditorHierarchyPanel *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_HIERARCHY_PANEL (self), NULL);
	return self->app;
}

LrgTreeView *
lrg_editor_hierarchy_panel_get_tree_view (LrgEditorHierarchyPanel *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_HIERARCHY_PANEL (self), NULL);
	return self->tree;
}

LrgTreeView *
lrg_editor_hierarchy_panel_get_live_view (LrgEditorHierarchyPanel *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_HIERARCHY_PANEL (self), NULL);
	return self->live;
}

LrgEditorNodeModel *
lrg_editor_hierarchy_panel_get_node_model (LrgEditorHierarchyPanel *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_HIERARCHY_PANEL (self), NULL);
	return self->model;
}

LrgTextInput *
lrg_editor_hierarchy_panel_get_search_input (LrgEditorHierarchyPanel *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_HIERARCHY_PANEL (self), NULL);
	return self->search;
}
