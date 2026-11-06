/* ui-showcase.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * An app-style tour of the retained UI framework: every widget, the
 * theme presets and accent derivation, spring motion presets, elevation
 * shadows, gradients, staggered appear animations, toasts, a modal
 * dialog, a 10,000-row virtualized list and table, a tree, icon tiles,
 * pickers, glass materials and effect shaders, layout glide, the
 * dynamic island, Markdown with rich-text effects, a hot-reloaded YAML
 * screen and the UI inspector.
 *
 * Controls:
 *   Mouse                 everything
 *   Tab / Shift+Tab       move focus (focus ring appears)
 *   Arrows / gamepad      spatial focus navigation, list cursor, sliders
 *   Enter / Space / A     activate the focused widget
 *   Escape / B            close popups and dialogs, leave pick mode
 *   F12                   inspector pick mode (hover highlights, click selects)
 *
 * Run with --help for options (theme, motion, page, screenshots).
 */

#include <libregnum.h>
#include <graylib.h>
#include <raylib.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define WINDOW_WIDTH   (1280)
#define WINDOW_HEIGHT  (800)
#define SIDEBAR_WIDTH  (240.0f)
#define N_LIST_ITEMS   (10000)
#define N_TABLE_ROWS   (10000)
#define SETTINGS_FILE  "showcase-settings.yaml"

static const gchar *license_text =
	"ui-showcase - libregnum UI framework tour\n"
	"Copyright (C) 2026  Zach Podbielniak\n\n"
	"This program is free software: you can redistribute it and/or modify\n"
	"it under the terms of the GNU Affero General Public License as published by\n"
	"the Free Software Foundation, either version 3 of the License, or\n"
	"(at your option) any later version.\n\n"
	"This program is distributed in the hope that it will be useful,\n"
	"but WITHOUT ANY WARRANTY; without even the implied warranty of\n"
	"MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the\n"
	"GNU Affero General Public License for more details.\n\n"
	"You should have received a copy of the GNU Affero General Public License\n"
	"along with this program.  If not, see <https://www.gnu.org/licenses/>.\n";

/* Accent swatches offered in the header */
static const gchar *accents[] = {
	"#7c6cff", "#22d3ee", "#34d399", "#fbbf24", "#ff7a45", "#f472b6"
};

/* Motion presets, in combo-box order */
static const struct { const gchar *name; LrgMotion motion; } motions[] = {
	{ "Smooth", LRG_MOTION_SMOOTH },
	{ "Snappy", LRG_MOTION_SNAPPY },
	{ "Bouncy", LRG_MOTION_BOUNCY },
	{ "Wobbly", LRG_MOTION_WOBBLY },
	{ "None",   LRG_MOTION_NONE }
};

/* Sidebar pages, in order; --page matches these case-insensitively */
enum
{
	PAGE_CONTROLS,
	PAGE_CARDS,
	PAGE_FEEDBACK,
	PAGE_LIST,
	PAGE_DATA,
	PAGE_PICKERS,
	PAGE_GLASS,
	PAGE_MOTION,
	PAGE_MARKDOWN,
	PAGE_DECLARATIVE,
	PAGE_INSPECTOR,
	N_PAGES
};

static const gchar *page_names[N_PAGES] = {
	"Controls", "Cards", "Feedback", "List", "Data", "Pickers",
	"Glass", "Motion", "Markdown", "Declarative", "Inspector"
};

/* Effect palettes for the glass page, by built-in effect name. Effects
 * not in this table (user effects) use the theme's colours. */
static const struct { const gchar *name; const gchar *c1; const gchar *c2; const gchar *c3; } fx_palettes[] = {
	{ "shimmer",       "#4c1d95", "#2563eb", "#ffffffb0" },
	{ "aurora",        "#0b1026", "#22d3ee", "#a855f7" },
	{ "neon",          "#120a24", "#ff2bd6", "#22d3ee" },
	{ "spotlight",     "#1e1b4b", "#818cf8", "#f472b6" },
	{ "ripple",        "#0c4a6e", "#38bdf8", "#e0f2fe" },
	{ "grain",         "#f97316", "#be185d", "#ffffff" },
	{ "plasma",        "#7c3aed", "#06b6d4", "#f43f5e" },
	{ "mesh-gradient", "#db2777", "#2563eb", "#059669" }
};

/* ==========================================================================
 * A list item for the list page and the sidebar
 * ========================================================================== */

#define SHOWCASE_TYPE_ITEM (showcase_item_get_type ())
G_DECLARE_FINAL_TYPE (ShowcaseItem, showcase_item, SHOWCASE, ITEM, GObject)

struct _ShowcaseItem
{
	GObject  parent_instance;
	gchar   *label;
};

G_DEFINE_FINAL_TYPE (ShowcaseItem, showcase_item, G_TYPE_OBJECT)

enum
{
	ITEM_PROP_0,
	ITEM_PROP_LABEL,
	ITEM_N_PROPS
};

static void
showcase_item_finalize (GObject *object)
{
	g_free (SHOWCASE_ITEM (object)->label);
	G_OBJECT_CLASS (showcase_item_parent_class)->finalize (object);
}

static void
showcase_item_get_property (GObject    *object,
                            guint       prop_id,
                            GValue     *value,
                            GParamSpec *pspec)
{
	if (prop_id == ITEM_PROP_LABEL)
		g_value_set_string (value, SHOWCASE_ITEM (object)->label);
	else
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
}

static void
showcase_item_class_init (ShowcaseItemClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = showcase_item_finalize;
	object_class->get_property = showcase_item_get_property;

	/* The list view's default rows show a "label" property */
	g_object_class_install_property (object_class, ITEM_PROP_LABEL,
		g_param_spec_string ("label", NULL, NULL, NULL,
		                     G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));
}

static void
showcase_item_init (ShowcaseItem *self)
{
}

static ShowcaseItem *
showcase_item_new (guint index)
{
	static const gchar *kinds[] = { "Ember", "Frost", "Gale", "Stone", "Tide", "Volt" };
	ShowcaseItem       *self = g_object_new (SHOWCASE_TYPE_ITEM, NULL);

	self->label = g_strdup_printf ("#%05u  %s relic, tier %u", index,
	                               kinds[index % G_N_ELEMENTS (kinds)], 1 + index % 7);
	return self;
}

/* ==========================================================================
 * A tree / tile node: a name, a built-in icon and optional children
 * ========================================================================== */

#define SHOWCASE_TYPE_NODE (showcase_node_get_type ())
G_DECLARE_FINAL_TYPE (ShowcaseNode, showcase_node, SHOWCASE, NODE, GObject)

struct _ShowcaseNode
{
	GObject       parent_instance;
	gchar        *name;
	LrgIcon       icon;
	GListStore   *children;  /* NULL for leaves */
	ShowcaseNode *parent;    /* borrowed: the parent owns us through its store */
};

G_DEFINE_FINAL_TYPE (ShowcaseNode, showcase_node, G_TYPE_OBJECT)

static void
showcase_node_finalize (GObject *object)
{
	ShowcaseNode *self = SHOWCASE_NODE (object);

	g_free (self->name);
	g_clear_object (&self->children);
	G_OBJECT_CLASS (showcase_node_parent_class)->finalize (object);
}

static void
showcase_node_class_init (ShowcaseNodeClass *klass)
{
	G_OBJECT_CLASS (klass)->finalize = showcase_node_finalize;
}

static void
showcase_node_init (ShowcaseNode *self)
{
}

/* showcase_node_new: a node; folders get an empty child store */
static ShowcaseNode *
showcase_node_new (const gchar *name,
                   LrgIcon      icon,
                   gboolean     folder)
{
	ShowcaseNode *self = g_object_new (SHOWCASE_TYPE_NODE, NULL);

	self->name = g_strdup (name);
	self->icon = icon;
	if (folder)
		self->children = g_list_store_new (SHOWCASE_TYPE_NODE);
	return self;
}

/* showcase_node_add: append a child to a folder and return it (borrowed) */
static ShowcaseNode *
showcase_node_add (ShowcaseNode *parent,
                   const gchar  *name,
                   LrgIcon       icon,
                   gboolean      folder)
{
	g_autoptr(ShowcaseNode) child = showcase_node_new (name, icon, folder);

	child->parent = parent;
	g_list_store_append (parent->children, child);
	return child;
}

/* ==========================================================================
 * A table row: an adventurer with typed properties
 * ========================================================================== */

#define SHOWCASE_TYPE_ROW (showcase_row_get_type ())
G_DECLARE_FINAL_TYPE (ShowcaseRow, showcase_row, SHOWCASE, ROW, GObject)

struct _ShowcaseRow
{
	GObject  parent_instance;
	guint    id;
	gchar   *name;
	gchar   *role;
	guint    level;
	gdouble  power;
	gchar   *region;
};

G_DEFINE_FINAL_TYPE (ShowcaseRow, showcase_row, G_TYPE_OBJECT)

enum
{
	ROW_PROP_0,
	ROW_PROP_ID,
	ROW_PROP_NAME,
	ROW_PROP_ROLE,
	ROW_PROP_LEVEL,
	ROW_PROP_POWER,
	ROW_PROP_REGION,
	ROW_N_PROPS
};

static void
showcase_row_finalize (GObject *object)
{
	ShowcaseRow *self = SHOWCASE_ROW (object);

	g_free (self->name);
	g_free (self->role);
	g_free (self->region);
	G_OBJECT_CLASS (showcase_row_parent_class)->finalize (object);
}

static void
showcase_row_get_property (GObject    *object,
                           guint       prop_id,
                           GValue     *value,
                           GParamSpec *pspec)
{
	ShowcaseRow *self = SHOWCASE_ROW (object);

	switch (prop_id)
	{
	case ROW_PROP_ID:     g_value_set_uint (value, self->id); break;
	case ROW_PROP_NAME:   g_value_set_string (value, self->name); break;
	case ROW_PROP_ROLE:   g_value_set_string (value, self->role); break;
	case ROW_PROP_LEVEL:  g_value_set_uint (value, self->level); break;
	case ROW_PROP_POWER:  g_value_set_double (value, self->power); break;
	case ROW_PROP_REGION: g_value_set_string (value, self->region); break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
showcase_row_class_init (ShowcaseRowClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	GParamFlags   flags = G_PARAM_READABLE | G_PARAM_STATIC_STRINGS;

	object_class->finalize = showcase_row_finalize;
	object_class->get_property = showcase_row_get_property;

	/* The table's property columns read these by name, and sort
	 * numbers numerically and strings with g_utf8_collate() */
	g_object_class_install_property (object_class, ROW_PROP_ID,
		g_param_spec_uint ("id", NULL, NULL, 0, G_MAXUINT, 0, flags));
	g_object_class_install_property (object_class, ROW_PROP_NAME,
		g_param_spec_string ("name", NULL, NULL, NULL, flags));
	g_object_class_install_property (object_class, ROW_PROP_ROLE,
		g_param_spec_string ("role", NULL, NULL, NULL, flags));
	g_object_class_install_property (object_class, ROW_PROP_LEVEL,
		g_param_spec_uint ("level", NULL, NULL, 0, G_MAXUINT, 0, flags));
	g_object_class_install_property (object_class, ROW_PROP_POWER,
		g_param_spec_double ("power", NULL, NULL, 0.0, G_MAXDOUBLE, 0.0, flags));
	g_object_class_install_property (object_class, ROW_PROP_REGION,
		g_param_spec_string ("region", NULL, NULL, NULL, flags));
}

static void
showcase_row_init (ShowcaseRow *self)
{
}

/* showcase_row_new: a deterministic pseudo-random adventurer */
static ShowcaseRow *
showcase_row_new (guint index)
{
	static const gchar *first[] = { "Ka", "Mor", "El", "Thra", "Vi", "Sor", "Ny", "Bal", "Ise", "Cor" };
	static const gchar *last[] = { "dric", "wen", "thas", "mira", "gorn", "lia", "vex", "dun", "ra", "nox" };
	static const gchar *roles[] = { "Warden", "Mystic", "Ranger", "Duelist", "Alchemist", "Bard" };
	static const gchar *regions[] = { "Ashfall Reach", "Glimmerdeep", "The Verdant March",
	                                  "Stormcrown Isles", "Hollow Wastes" };
	ShowcaseRow *self = g_object_new (SHOWCASE_TYPE_ROW, NULL);
	guint        h = index * 2654435761u;

	self->id = index + 1;
	self->name = g_strdup_printf ("%s%s", first[(h >> 3) % G_N_ELEMENTS (first)],
	                              last[(h >> 11) % G_N_ELEMENTS (last)]);
	self->role = g_strdup (roles[(h >> 17) % G_N_ELEMENTS (roles)]);
	self->level = 1 + (h >> 7) % 60;
	self->power = (gdouble) self->level * 37.5 + (gdouble) ((h >> 13) % 500) / 10.0;
	self->region = g_strdup (regions[(h >> 21) % G_N_ELEMENTS (regions)]);
	return self;
}

/* ==========================================================================
 * ShowcaseIcon: a widget that paints one built-in vector icon
 * ========================================================================== */

#define SHOWCASE_TYPE_ICON (showcase_icon_get_type ())
G_DECLARE_FINAL_TYPE (ShowcaseIcon, showcase_icon, SHOWCASE, ICON, LrgWidget)

struct _ShowcaseIcon
{
	LrgWidget     parent_instance;
	LrgIcon       icon;
	gfloat        size;
	LrgThemeColor role;   /* colour role the icon is painted with */
};

G_DEFINE_FINAL_TYPE (ShowcaseIcon, showcase_icon, LRG_TYPE_WIDGET)

static void
showcase_icon_measure (LrgWidget *widget,
                       gfloat    *preferred_width,
                       gfloat    *preferred_height)
{
	ShowcaseIcon *self = SHOWCASE_ICON (widget);

	*preferred_width = self->size;
	*preferred_height = self->size;
}

/* showcase_icon_paint: the icon, centred in the allocation, in the
 * theme colour of its role (so it follows theme and accent changes) */
static void
showcase_icon_paint (LrgWidget    *widget,
                     LrgPaintList *list)
{
	ShowcaseIcon   *self = SHOWCASE_ICON (widget);
	LrgTheme       *theme = lrg_widget_get_theme (widget);
	gfloat          w = lrg_widget_get_width (widget);
	gfloat          h = lrg_widget_get_height (widget);
	gfloat          s = MIN (self->size, MIN (w, h));
	const GrlColor *color = lrg_theme_get_color (theme, self->role);

	lrg_paint_list_icon (list, self->icon,
	                     lrg_widget_get_world_x (widget) + (w - s) * 0.5f,
	                     lrg_widget_get_world_y (widget) + (h - s) * 0.5f,
	                     s, 0.0f, color);
}

static void
showcase_icon_class_init (ShowcaseIconClass *klass)
{
	LrgWidgetClass *widget_class = LRG_WIDGET_CLASS (klass);

	widget_class->measure = showcase_icon_measure;
	widget_class->paint = showcase_icon_paint;
}

static void
showcase_icon_init (ShowcaseIcon *self)
{
	self->size = 16.0f;
	self->role = LRG_THEME_COLOR_TEXT;
	lrg_widget_set_pointer_transparent (LRG_WIDGET (self), TRUE);
}

static ShowcaseIcon *
showcase_icon_new (LrgIcon       icon,
                   gfloat        size,
                   LrgThemeColor role)
{
	ShowcaseIcon *self = g_object_new (SHOWCASE_TYPE_ICON, NULL);

	self->icon = icon;
	self->size = size;
	self->role = role;
	lrg_widget_set_valign (LRG_WIDGET (self), LRG_ALIGN_CENTER);
	return self;
}

static void
showcase_icon_set (ShowcaseIcon  *self,
                   LrgIcon        icon,
                   LrgThemeColor  role)
{
	self->icon = icon;
	self->role = role;
}

/* ==========================================================================
 * ShowcaseBackdrop: vivid gradient blobs and pulsing curves that drift
 * with the canvas clock (lrg_paint_list_get_time()), so glass has
 * something to frost
 * ========================================================================== */

#define SHOWCASE_TYPE_BACKDROP (showcase_backdrop_get_type ())
G_DECLARE_FINAL_TYPE (ShowcaseBackdrop, showcase_backdrop, SHOWCASE, BACKDROP, LrgWidget)

struct _ShowcaseBackdrop
{
	LrgWidget parent_instance;
};

G_DEFINE_FINAL_TYPE (ShowcaseBackdrop, showcase_backdrop, LRG_TYPE_WIDGET)

static void
color_hex (const gchar *hex,
           gfloat       alpha,
           GrlColor    *out)
{
	GrlColor c;

	lrg_color_parse (hex, &c);
	lrg_paint_color_with_alpha (&c, alpha, out);
}

/* showcase_backdrop_paint: a pure function of the list time; nothing
 * here reads a clock, so captures are reproducible */
static void
showcase_backdrop_paint (LrgWidget    *widget,
                         LrgPaintList *list)
{
	static const struct { const gchar *hex; gfloat bx; gfloat by; gfloat ax; gfloat ay;
	                      gfloat speed; gfloat phase; gfloat radius; } blobs[] = {
		{ "#7c3aed", 0.18f, 0.25f, 0.12f, 0.10f, 0.31f, 0.0f, 0.34f },
		{ "#06b6d4", 0.80f, 0.20f, 0.10f, 0.14f, 0.23f, 1.7f, 0.30f },
		{ "#f43f5e", 0.55f, 0.78f, 0.16f, 0.08f, 0.27f, 3.1f, 0.32f },
		{ "#f59e0b", 0.12f, 0.85f, 0.08f, 0.10f, 0.37f, 4.4f, 0.24f },
		{ "#22c55e", 0.92f, 0.82f, 0.06f, 0.12f, 0.19f, 2.2f, 0.22f },
		{ "#3b82f6", 0.48f, 0.38f, 0.20f, 0.12f, 0.17f, 5.0f, 0.28f }
	};
	LrgTheme           *theme = lrg_widget_get_theme (widget);
	gboolean            dark = lrg_theme_get_dark (theme);
	gfloat              x = lrg_widget_get_world_x (widget);
	gfloat              y = lrg_widget_get_world_y (widget);
	gfloat              w = lrg_widget_get_width (widget);
	gfloat              h = lrg_widget_get_height (widget);
	gdouble             t = lrg_paint_list_get_time (list);
	LrgCornerRadii      radii;
	LrgPaintGradient    base;
	LrgPaintCurveStyle  wire;
	GrlColor            a;
	GrlColor            b;
	guint               i;

	if (w <= 0.0f || h <= 0.0f)
		return;

	/* Base: a deep (or pale) diagonal gradient in a rounded frame */
	lrg_corner_radii_init_uniform (&radii, 18.0f);
	color_hex (dark ? "#0b1026" : "#eef2ff", 1.0f, &a);
	color_hex (dark ? "#2a1458" : "#fdf2f8", 1.0f, &b);
	lrg_paint_gradient_init_linear (&base, &a, &b, 35.0f);
	lrg_paint_list_rect_ex (list, x, y, w, h, &radii, NULL, &base, 0.0f, NULL);

	lrg_paint_list_push_clip (list, x, y, w, h);

	/* Blobs: radial gradients from a saturated centre to transparent,
	 * each orbiting its home point on a Lissajous path */
	for (i = 0; i < G_N_ELEMENTS (blobs); i++)
	{
		LrgPaintGradient g;
		GrlColor         centre;
		GrlColor         edge;
		gfloat           r = blobs[i].radius * MAX (w, h);
		gfloat           cx = x + w * (blobs[i].bx + blobs[i].ax * (gfloat) sin (t * blobs[i].speed + blobs[i].phase));
		gfloat           cy = y + h * (blobs[i].by + blobs[i].ay * (gfloat) cos (t * blobs[i].speed * 1.3 + blobs[i].phase));

		color_hex (blobs[i].hex, dark ? 0.85f : 0.70f, &centre);
		color_hex (blobs[i].hex, 0.0f, &edge);
		lrg_paint_gradient_init_radial (&g, &centre, &edge, 0.5f, 0.5f, r);
		lrg_paint_list_rect_ex (list, cx - r, cy - r, r * 2.0f, r * 2.0f, NULL, NULL, &g, 0.0f, NULL);
	}

	/* Sweeping wires with pulses across the whole backdrop */
	for (i = 0; i < 3; i++)
	{
		gfloat fy = y + h * (0.22f + 0.28f * (gfloat) i);
		gfloat sway = h * 0.18f * (gfloat) sin (t * 0.4 + (gdouble) i * 2.1);

		lrg_paint_curve_style_init (&wire, &a);
		color_hex (i == 1 ? "#22d3ee" : "#a855f7", dark ? 0.55f : 0.45f, &wire.color_start);
		color_hex (i == 1 ? "#f472b6" : "#22d3ee", dark ? 0.55f : 0.45f, &wire.color_end);
		color_hex ("#ffffff", dark ? 0.12f : 0.25f, &wire.glow);
		wire.glow_size = 6.0f;
		color_hex ("#ffffff", 0.95f, &wire.pulse_color);
		wire.pulse_count = 2 + i;
		wire.pulse_speed = 0.08f + 0.03f * (gfloat) i;
		wire.pulse_phase = 0.3f * (gfloat) i;
		lrg_paint_list_curve (list, x - 20.0f, fy,
		                      x + w * 0.35f, fy - sway,
		                      x + w * 0.65f, fy + sway,
		                      x + w + 20.0f, fy, 2.5f, &wire);
	}

	lrg_paint_list_pop_clip (list);
}

/* showcase_backdrop_measure: no preferred size of its own; it fills
 * whatever the overlay gives it (the default measure would report the
 * current size and make the page grow with every layout) */
static void
showcase_backdrop_measure (LrgWidget *widget,
                           gfloat    *preferred_width,
                           gfloat    *preferred_height)
{
	*preferred_width = 0.0f;
	*preferred_height = 0.0f;
}

static void
showcase_backdrop_class_init (ShowcaseBackdropClass *klass)
{
	LrgWidgetClass *widget_class = LRG_WIDGET_CLASS (klass);

	widget_class->measure = showcase_backdrop_measure;
	widget_class->paint = showcase_backdrop_paint;
}

static void
showcase_backdrop_init (ShowcaseBackdrop *self)
{
	lrg_widget_set_pointer_transparent (LRG_WIDGET (self), TRUE);
}

/* ==========================================================================
 * ShowcaseWires: a small node graph with gradient wires and pulses
 * ========================================================================== */

#define SHOWCASE_TYPE_WIRES (showcase_wires_get_type ())
G_DECLARE_FINAL_TYPE (ShowcaseWires, showcase_wires, SHOWCASE, WIRES, LrgWidget)

struct _ShowcaseWires
{
	LrgWidget parent_instance;
};

G_DEFINE_FINAL_TYPE (ShowcaseWires, showcase_wires, LRG_TYPE_WIDGET)

static void
showcase_wires_measure (LrgWidget *widget,
                        gfloat    *preferred_width,
                        gfloat    *preferred_height)
{
	*preferred_width = 480.0f;
	*preferred_height = 150.0f;
}

/* Node layout as fractions of the widget: x, y of the centre */
static const struct { const gchar *name; gfloat fx; gfloat fy; LrgIcon icon; } wire_nodes[] = {
	{ "Input",  0.08f, 0.50f, LRG_ICON_DOWNLOAD },
	{ "Noise",  0.36f, 0.18f, LRG_ICON_REFRESH },
	{ "Blend",  0.36f, 0.82f, LRG_ICON_EDIT },
	{ "Glow",   0.64f, 0.50f, LRG_ICON_STAR },
	{ "Output", 0.92f, 0.50f, LRG_ICON_UPLOAD }
};

/* Edges: from node, to node, two gradient colours */
static const struct { guint from; guint to; const gchar *c0; const gchar *c1; } wire_edges[] = {
	{ 0, 1, "#22d3ee", "#a855f7" },
	{ 0, 2, "#22d3ee", "#f472b6" },
	{ 1, 3, "#a855f7", "#fbbf24" },
	{ 2, 3, "#f472b6", "#fbbf24" },
	{ 3, 4, "#fbbf24", "#34d399" }
};

#define WIRE_NODE_W (92.0f)
#define WIRE_NODE_H (34.0f)

/* showcase_wires_paint: wires first (cubic Beziers leaving and entering
 * horizontally, like a shader graph), then the node chips on top */
static void
showcase_wires_paint (LrgWidget    *widget,
                      LrgPaintList *list)
{
	LrgTheme  *theme = lrg_widget_get_theme (widget);
	GrlFont   *font = lrg_theme_get_ui_font (theme);
	gfloat     fs = lrg_theme_get_metric (theme, LRG_THEME_METRIC_FONT_SIZE_SMALL);
	gfloat     x = lrg_widget_get_world_x (widget);
	gfloat     y = lrg_widget_get_world_y (widget);
	gfloat     w = lrg_widget_get_width (widget) - WIRE_NODE_W;
	gfloat     h = lrg_widget_get_height (widget) - WIRE_NODE_H;
	guint      i;

	x += WIRE_NODE_W * 0.5f;
	y += WIRE_NODE_H * 0.5f;

	for (i = 0; i < G_N_ELEMENTS (wire_edges); i++)
	{
		LrgPaintCurveStyle style;
		GrlColor           c0;
		gfloat             x0 = x + w * wire_nodes[wire_edges[i].from].fx + WIRE_NODE_W * 0.5f;
		gfloat             y0 = y + h * wire_nodes[wire_edges[i].from].fy;
		gfloat             x3 = x + w * wire_nodes[wire_edges[i].to].fx - WIRE_NODE_W * 0.5f;
		gfloat             y3 = y + h * wire_nodes[wire_edges[i].to].fy;
		gfloat             dx = (x3 - x0) * 0.5f;

		color_hex (wire_edges[i].c0, 1.0f, &c0);
		lrg_paint_curve_style_init (&style, &c0);
		color_hex (wire_edges[i].c1, 1.0f, &style.color_end);
		color_hex (wire_edges[i].c0, 0.35f, &style.glow);
		style.glow_size = 7.0f;
		color_hex ("#ffffff", 1.0f, &style.pulse_color);
		style.pulse_count = 2;
		style.pulse_speed = 0.35f;
		style.pulse_phase = 0.17f * (gfloat) i;
		lrg_paint_list_curve (list, x0, y0, x0 + dx, y0, x3 - dx, y3, x3, y3, 3.0f, &style);
	}

	for (i = 0; i < G_N_ELEMENTS (wire_nodes); i++)
	{
		gfloat cx = x + w * wire_nodes[i].fx;
		gfloat cy = y + h * wire_nodes[i].fy;
		gfloat nx = cx - WIRE_NODE_W * 0.5f;
		gfloat ny = cy - WIRE_NODE_H * 0.5f;
		gfloat tw = 0.0f;
		gfloat th = 0.0f;

		lrg_paint_list_shadow (list, nx, ny, WIRE_NODE_W, WIRE_NODE_H, 10.0f, 10.0f, 0.0f, 0.0f, 3.0f,
		                       lrg_theme_get_color (theme, LRG_THEME_COLOR_SHADOW));
		lrg_paint_list_rect (list, nx, ny, WIRE_NODE_W, WIRE_NODE_H, 10.0f,
		                     lrg_theme_get_color (theme, LRG_THEME_COLOR_SURFACE));
		lrg_paint_list_border (list, nx, ny, WIRE_NODE_W, WIRE_NODE_H, 10.0f, 1.0f,
		                       lrg_theme_get_color (theme, LRG_THEME_COLOR_BORDER));
		lrg_paint_list_icon (list, wire_nodes[i].icon, nx + 8.0f, cy - 8.0f, 16.0f, 0.0f,
		                     lrg_theme_get_color (theme, LRG_THEME_COLOR_ACCENT));
		lrg_paint_measure_text (font, wire_nodes[i].name, fs, 0.0f, &tw, &th);
		lrg_paint_list_text (list, font, wire_nodes[i].name, nx + 30.0f, cy - th * 0.5f, fs, 0.0f,
		                     lrg_theme_get_color (theme, LRG_THEME_COLOR_TEXT));
	}
}

static void
showcase_wires_class_init (ShowcaseWiresClass *klass)
{
	LrgWidgetClass *widget_class = LRG_WIDGET_CLASS (klass);

	widget_class->measure = showcase_wires_measure;
	widget_class->paint = showcase_wires_paint;
}

static void
showcase_wires_init (ShowcaseWires *self)
{
}

/* ==========================================================================
 * App state
 * ========================================================================== */

typedef struct
{
	LrgCanvas        *canvas;
	LrgHBox          *root;
	LrgListView      *nav;
	LrgOverlay       *pages;
	LrgWidget        *page[N_PAGES];
	gint              current_page;
	GPtrArray        *cards;       /* LrgPanel, for replaying their appear */
	LrgToastQueue    *toasts;
	LrgComboBox      *toast_mode;
	LrgProgressBar   *progress;
	LrgSlider        *volume;
	LrgLabel         *selection;
	LrgDialog        *dialog;
	gchar            *accent;

	/* Data and pickers */
	ShowcaseNode     *project;     /* tree root, owned */
	LrgTreeView      *tree;
	LrgTable         *table;
	LrgLabel         *table_status;
	LrgBreadcrumb    *crumbs[2];   /* data page and pickers page */
	LrgColorPicker   *pickers[2];  /* inline and popup, both drive the accent */
	gboolean          syncing;     /* guards picker <-> accent echo */
	LrgLabel         *picker_status;

	/* Motion */
	LrgIsland        *island;
	LrgWrapBox       *chips;
	guint             next_chip;
	guint32           shuffle_seed;
	GPtrArray        *appear_tiles;  /* LrgPanel, replayed by a button */

	/* Markdown */
	LrgMarkdownLabel *markdown;

	/* Declarative */
	LrgUILoader      *loader;
	LrgPanel         *yaml_host;
	LrgLabel         *yaml_status;
	gchar            *yaml_path;

	/* Inspector */
	LrgUIInspector   *inspector;
	LrgLabel         *inspect_mode;
	LrgLabel         *inspect_selected;
	LrgLabel         *inspect_timings;
	LrgWidget        *inspect_target;
	LrgMarkdownLabel *inspect_json;
} Showcase;

static void show_toast (Showcase *app, const gchar *title, const gchar *message, LrgToastKind kind);
static void sync_pickers_to_accent (Showcase *app);

/* ==========================================================================
 * Theming
 * ========================================================================== */

/* add_card_rules: gradient styles for the cards page, per theme */
static void
add_card_rules (LrgTheme *theme)
{
	static const struct { const gchar *a; const gchar *b; gfloat angle; } grads[] = {
		{ "#7c6cff", "#22d3ee", 30.0f },
		{ "#ff7a45", "#ff3d7f", 60.0f },
		{ "#34d399", "#0ea5e9", 90.0f },
		{ "#f59e0b", "#ef4444", 120.0f },
		{ "#a855f7", "#ec4899", 150.0f },
		{ "#14b8a6", "#6366f1", 0.0f }
	};
	guint i;

	for (i = 0; i < G_N_ELEMENTS (grads); i++)
	{
		g_autofree gchar        *klass = g_strdup_printf ("card-%u", i);
		g_autoptr(LrgStyleRule)  rule = lrg_style_rule_new ("LrgPanel", klass, LRG_UI_PART_BOX,
		                                                    LRG_WIDGET_STATE_NONE);
		g_autoptr(LrgStyleRule)  hover = lrg_style_rule_new ("LrgPanel", klass, LRG_UI_PART_BOX,
		                                                     LRG_WIDGET_STATE_HOVERED);
		LrgPaintGradient         g;
		GrlColor                 a;
		GrlColor                 b;
		GrlColor                 glow;

		lrg_color_parse (grads[i].a, &a);
		lrg_color_parse (grads[i].b, &b);
		lrg_paint_gradient_init_linear (&g, &a, &b, grads[i].angle);
		lrg_part_look_set_gradient (lrg_style_rule_get_look (rule), &g);
		lrg_theme_add_style_rule (theme, rule);

		/* Hovered cards glow in their own colour */
		lrg_paint_color_with_alpha (&a, 0.55f, &glow);
		lrg_part_look_set_glow (lrg_style_rule_get_look (hover), &glow, 28.0f);
		lrg_theme_add_style_rule (theme, hover);
	}
}

/* add_glass_rules: materials and effects for the glass page, all
 * through theme style rules (the "theme route"): the widgets only
 * carry a style class */
static void
add_glass_rules (LrgTheme *theme)
{
	gboolean                dark = lrg_theme_get_dark (theme);
	g_autoptr(LrgStyleRule) clear = lrg_style_rule_new ("LrgPanel", "glass-clear", LRG_UI_PART_BOX,
	                                                    LRG_WIDGET_STATE_NONE);
	g_autoptr(LrgStyleRule) frosted = lrg_style_rule_new ("LrgPanel", "glass-frosted", LRG_UI_PART_BOX,
	                                                      LRG_WIDGET_STATE_NONE);
	g_autoptr(LrgStyleRule) tinted = lrg_style_rule_new ("LrgPanel", "glass-tinted", LRG_UI_PART_BOX,
	                                                     LRG_WIDGET_STATE_NONE);
	g_auto(GStrv)           names = lrg_paint_effect_list_names ();
	LrgPaintGlass           glass;
	GrlColor                tint;
	GrlColor                white;
	guint                   i;

	/* Clear glass: light frost, a strong refracting rim and dispersion */
	lrg_paint_glass_init_default (&glass);
	glass.frost = 0.15f;
	glass.refraction = 14.0f;
	glass.dispersion = 0.5f;
	glass.rim_light = 0.9f;
	color_hex ("#ffffff", dark ? 0.06f : 0.22f, &glass.tint);
	lrg_part_look_set_material (lrg_style_rule_get_look (clear), LRG_PAINT_MATERIAL_GLASS);
	lrg_part_look_set_glass (lrg_style_rule_get_look (clear), &glass);
	lrg_theme_add_style_rule (theme, clear);

	/* Frosted: full frost with a wide blur and a faint white veil */
	lrg_paint_glass_init_default (&glass);
	glass.blur_radius = 22.0f;
	glass.saturation = 1.4f;
	color_hex (dark ? "#ffffff" : "#ffffff", dark ? 0.08f : 0.40f, &glass.tint);
	lrg_part_look_set_material (lrg_style_rule_get_look (frosted), LRG_PAINT_MATERIAL_FROSTED);
	lrg_part_look_set_glass (lrg_style_rule_get_look (frosted), &glass);
	lrg_theme_add_style_rule (theme, frosted);

	/* Tinted: frosted glass coloured by the theme accent (the background
	 * alpha is the tint strength) */
	lrg_paint_color_with_alpha (lrg_theme_get_color (theme, LRG_THEME_COLOR_ACCENT), 0.35f, &tint);
	lrg_part_look_set_material (lrg_style_rule_get_look (tinted), LRG_PAINT_MATERIAL_TINTED);
	lrg_part_look_set_background (lrg_style_rule_get_look (tinted), &tint);
	lrg_theme_add_style_rule (theme, tinted);

	/* One button class per effect: "fx-aurora", "fx-neon", ... with a
	 * fixed palette and white labels so they read on every theme */
	color_hex ("#ffffff", 1.0f, &white);
	for (i = 0; names != NULL && names[i] != NULL; i++)
	{
		g_autofree gchar        *klass = g_strconcat ("fx-", names[i], NULL);
		g_autoptr(LrgStyleRule)  box = lrg_style_rule_new ("LrgButton", klass, LRG_UI_PART_BOX,
		                                                   LRG_WIDGET_STATE_NONE);
		g_autoptr(LrgStyleRule)  label = lrg_style_rule_new ("LrgButton", klass, LRG_UI_PART_LABEL,
		                                                     LRG_WIDGET_STATE_NONE);
		LrgPartLook             *look = lrg_style_rule_get_look (box);
		guint                    j;

		lrg_part_look_set_effect (look, names[i]);
		lrg_part_look_set_radius (look, 14.0f);
		for (j = 0; j < G_N_ELEMENTS (fx_palettes); j++)
		{
			if (g_strcmp0 (fx_palettes[j].name, names[i]) == 0)
			{
				GrlColor c1;
				GrlColor c2;
				GrlColor c3;

				lrg_color_parse (fx_palettes[j].c1, &c1);
				lrg_color_parse (fx_palettes[j].c2, &c2);
				lrg_color_parse (fx_palettes[j].c3, &c3);
				lrg_part_look_set_effect_colors (look, &c1, &c2, &c3);
			}
		}
		lrg_theme_add_style_rule (theme, box);

		lrg_part_look_set_foreground (lrg_style_rule_get_look (label), &white);
		lrg_theme_add_style_rule (theme, label);
	}
}

/* apply_theme: switch the whole UI to a preset, keeping the accent */
static void
apply_theme (Showcase    *app,
             const gchar *preset)
{
	g_autoptr(LrgTheme) theme = lrg_theme_new_preset (preset);
	GrlColor            accent;

	if (theme == NULL)
	{
		g_printerr ("ui-showcase: unknown theme '%s'\n", preset);
		return;
	}

	if (app->accent != NULL && lrg_color_parse (app->accent, &accent))
		lrg_theme_set_accent (theme, &accent);
	add_card_rules (theme);
	add_glass_rules (theme);
	lrg_theme_set_default (theme);
}

/* set_accent: re-derive the accent family of the running theme; the
 * tinted glass rule follows the accent, so the rules are rebuilt */
static void
set_accent (Showcase       *app,
            const GrlColor *accent)
{
	LrgTheme *theme = lrg_theme_get_default ();

	g_free (app->accent);
	app->accent = lrg_color_to_string (accent);
	lrg_theme_set_accent (theme, accent);
	lrg_theme_clear_style_rules (theme);
	add_card_rules (theme);
	add_glass_rules (theme);
}

static void
on_theme_changed (LrgComboBox *combo,
                  gpointer     user_data)
{
	apply_theme (user_data, lrg_combo_box_get_selected_text (combo));
}

static void
on_motion_changed (LrgComboBox *combo,
                   gpointer     user_data)
{
	gint i = lrg_combo_box_get_selected (combo);

	if (i >= 0 && i < (gint) G_N_ELEMENTS (motions))
		lrg_theme_set_motion (lrg_theme_get_default (), motions[i].motion);
}

static void
on_accent_clicked (LrgButton *button,
                   gpointer   user_data)
{
	Showcase    *app = user_data;
	const gchar *hex = g_object_get_data (G_OBJECT (button), "accent");
	GrlColor     accent;

	if (lrg_color_parse (hex, &accent))
	{
		set_accent (app, &accent);
		sync_pickers_to_accent (app);
	}
}

/* ==========================================================================
 * Small builders
 * ========================================================================== */

static LrgLabel *
label_with_class (const gchar *text,
                  const gchar *style_class)
{
	LrgLabel *label = lrg_label_new (text);

	if (style_class != NULL)
		lrg_widget_add_class (LRG_WIDGET (label), style_class);
	return label;
}

/* add: append a child and drop our reference */
static void
add (gpointer container,
     gpointer child)
{
	lrg_container_add_child (LRG_CONTAINER (container), LRG_WIDGET (child));
	g_object_unref (child);
}

/* card: a titled card panel with a vertical box inside; the panel is
 * returned through @panel_out when the caller wants to style it */
static LrgVBox *
card (LrgContainer  *parent,
      const gchar   *title,
      LrgPanel     **panel_out)
{
	LrgPanel *panel = lrg_panel_new ();
	LrgVBox  *box = lrg_vbox_new ();

	lrg_panel_set_elevation (panel, 1);
	lrg_panel_set_fill_children (panel, TRUE);
	lrg_container_set_padding (LRG_CONTAINER (panel), 18.0f);
	lrg_container_set_padding (LRG_CONTAINER (box), 0.0f);
	lrg_container_set_spacing (LRG_CONTAINER (box), 12.0f);
	if (title != NULL)
		add (box, label_with_class (title, "heading"));
	lrg_container_add_child (LRG_CONTAINER (panel), LRG_WIDGET (box));
	g_object_unref (box);
	if (panel_out != NULL)
		*panel_out = panel;
	add (parent, panel);
	return box;
}

/* section: a titled card panel with a vertical box inside */
static LrgVBox *
section (LrgContainer *parent,
         const gchar  *title)
{
	return card (parent, title, NULL);
}

static LrgHBox *
hrow (gfloat spacing)
{
	LrgHBox *row = lrg_hbox_new ();

	lrg_container_set_padding (LRG_CONTAINER (row), 0.0f);
	lrg_container_set_spacing (LRG_CONTAINER (row), spacing);
	return row;
}

static LrgVBox *
vcol (gfloat spacing)
{
	LrgVBox *col = lrg_vbox_new ();

	lrg_container_set_padding (LRG_CONTAINER (col), 0.0f);
	lrg_container_set_spacing (LRG_CONTAINER (col), spacing);
	return col;
}

/* ==========================================================================
 * Controls, cards, feedback and list pages
 * ========================================================================== */

static void
on_volume_changed (LrgSlider *slider,
                   gpointer   user_data)
{
	Showcase *app = user_data;

	lrg_progress_bar_set_value (app->progress, lrg_slider_get_value (slider));
}

static LrgWidget *
build_controls_page (Showcase *app)
{
	LrgGrid      *grid = lrg_grid_new (2);
	LrgVBox      *box;
	LrgHBox      *row;
	LrgButton    *btn;
	LrgTabView   *tabs;
	LrgSpinBox   *spin;

	lrg_grid_set_column_spacing (grid, 20.0f);
	lrg_grid_set_row_spacing (grid, 20.0f);

	/* Buttons in every flavour */
	box = section (LRG_CONTAINER (grid), "Buttons");
	row = lrg_hbox_new ();
	lrg_container_set_spacing (LRG_CONTAINER (row), 10.0f);
	add (row, lrg_button_new ("Default"));
	btn = lrg_button_new ("Primary");
	lrg_widget_add_class (LRG_WIDGET (btn), "primary");
	add (row, btn);
	btn = lrg_button_new ("Flat");
	lrg_widget_add_class (LRG_WIDGET (btn), "flat");
	add (row, btn);
	btn = lrg_button_new ("Delete");
	lrg_widget_add_class (LRG_WIDGET (btn), "danger");
	add (row, btn);
	btn = lrg_button_new ("Disabled");
	lrg_widget_set_enabled (LRG_WIDGET (btn), FALSE);
	add (row, btn);
	add (box, row);

	/* Toggles */
	box = section (LRG_CONTAINER (grid), "Toggles");
	add (box, lrg_checkbox_new_with_style ("Enable bloom", LRG_CHECKBOX_STYLE_SWITCH));
	add (box, lrg_checkbox_new ("Show subtitles"));
	add (box, lrg_checkbox_new_with_style ("Hard mode", LRG_CHECKBOX_STYLE_RADIO));

	/* Ranges */
	box = section (LRG_CONTAINER (grid), "Ranges");
	app->volume = lrg_slider_new_with_range (0.0, 100.0, 1.0);
	lrg_slider_set_value (app->volume, 65.0);
	g_signal_connect (app->volume, "value-changed", G_CALLBACK (on_volume_changed), app);
	add (box, app->volume);
	app->progress = lrg_progress_bar_new ();
	lrg_progress_bar_set_max (app->progress, 100.0);
	lrg_progress_bar_set_value (app->progress, 65.0);
	add (box, app->progress);
	{
		LrgProgressBar *busy = lrg_progress_bar_new ();

		lrg_progress_bar_set_indeterminate (busy, TRUE);
		add (box, busy);
	}

	/* Text and numbers */
	box = section (LRG_CONTAINER (grid), "Input");
	add (box, lrg_text_input_new_with_placeholder ("Hero name..."));
	spin = lrg_spin_box_new_with_range (0.0, 10.0, 0.5);
	lrg_spin_box_set_digits (spin, 1);
	lrg_spin_box_set_value (spin, 2.5);
	add (box, spin);

	/* Tabs */
	box = section (LRG_CONTAINER (grid), "Tabs");
	tabs = lrg_tab_view_new ();
	lrg_widget_set_min_size (LRG_WIDGET (tabs), 0.0f, 110.0f);
	lrg_tab_view_add_tab (tabs, "Graphics", LRG_WIDGET (label_with_class ("Resolution, quality, effects.", "muted")));
	lrg_tab_view_add_tab (tabs, "Audio", LRG_WIDGET (label_with_class ("Volumes and output device.", "muted")));
	lrg_tab_view_add_tab (tabs, "Controls", LRG_WIDGET (label_with_class ("Bindings for keyboard and pad.", "muted")));
	add (box, tabs);

	/* A separator demo */
	box = section (LRG_CONTAINER (grid), "Separators");
	add (box, label_with_class ("Above the line", NULL));
	add (box, lrg_separator_new ());
	add (box, label_with_class ("Below the line", "muted"));

	return LRG_WIDGET (grid);
}

static LrgWidget *
build_cards_page (Showcase *app)
{
	LrgGrid *grid = lrg_grid_new (3);
	guint    i;

	lrg_grid_set_column_spacing (grid, 24.0f);
	lrg_grid_set_row_spacing (grid, 24.0f);

	/* Gradient cards (theme rules card-N) with increasing elevation,
	 * popping in one after another */
	for (i = 0; i < 6; i++)
	{
		g_autofree gchar *klass = g_strdup_printf ("card-%u", i);
		g_autofree gchar *title = g_strdup_printf ("Elevation %u", i % 4);
		LrgPanel         *panel = lrg_panel_new ();
		LrgVBox          *box = lrg_vbox_new ();

		lrg_widget_add_class (LRG_WIDGET (panel), klass);
		lrg_panel_set_elevation (panel, i % 4);
		lrg_panel_set_fill_children (panel, TRUE);
		lrg_container_set_padding (LRG_CONTAINER (panel), 22.0f);
		lrg_widget_set_appear (LRG_WIDGET (panel), LRG_APPEAR_POP, 0.06f * (gfloat) i);
		lrg_container_set_spacing (LRG_CONTAINER (box), 8.0f);
		add (box, label_with_class (title, "heading"));
		add (box, label_with_class ("Gradient fill from a theme\nstyle rule; hover to glow.", NULL));
		lrg_container_add_child (LRG_CONTAINER (panel), LRG_WIDGET (box));
		g_object_unref (box);
		g_ptr_array_add (app->cards, panel);
		lrg_container_add_child (LRG_CONTAINER (grid), LRG_WIDGET (panel));
	}

	return LRG_WIDGET (grid);
}

static void
show_toast (Showcase     *app,
            const gchar  *title,
            const gchar  *message,
            LrgToastKind  kind)
{
	g_autoptr(LrgToast) toast = lrg_toast_new (title, message);

	lrg_toast_set_kind (toast, kind);
	lrg_toast_queue_show (app->toasts, toast);
}

static void
on_toast_clicked (LrgButton *button,
                  gpointer   user_data)
{
	Showcase *app = user_data;
	gint      kind = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (button), "kind"));

	static const gchar *titles[] = { "Saved", "Quest complete", "Low health", "Connection lost" };
	static const gchar *messages[] = {
		"Your progress was stored in slot 2.",
		"The Ember Relic is yours. +250 XP",
		"Drink a potion or retreat!",
		"Retrying in 5 seconds..."
	};
	static const LrgToastKind kinds[] = {
		LRG_TOAST_KIND_INFO, LRG_TOAST_KIND_SUCCESS, LRG_TOAST_KIND_WARNING, LRG_TOAST_KIND_DANGER
	};

	show_toast (app, titles[kind], messages[kind], kinds[kind]);
}

static void
on_toast_mode_changed (LrgComboBox *combo,
                       gpointer     user_data)
{
	Showcase *app = user_data;
	static const LrgToastMode modes[] = {
		LRG_TOAST_MODE_STACK, LRG_TOAST_MODE_SEQUENCE, LRG_TOAST_MODE_REPLACE
	};
	gint i = lrg_combo_box_get_selected (combo);

	if (i >= 0 && i < (gint) G_N_ELEMENTS (modes))
		lrg_toast_queue_set_mode (app->toasts, modes[i]);
}

static void
on_dialog_response (LrgDialog *dialog,
                    gint       response,
                    gpointer   user_data)
{
	Showcase *app = user_data;

	if (response == 1)
		show_toast (app, "Ascended", "Your character was reborn.", LRG_TOAST_KIND_SUCCESS);
	else
		show_toast (app, "Cancelled", "Nothing changed.", LRG_TOAST_KIND_INFO);
}

static void
on_dialog_clicked (LrgButton *button,
                   gpointer   user_data)
{
	Showcase *app = user_data;

	lrg_dialog_present (app->dialog, app->canvas);
}

static LrgWidget *
build_feedback_page (Showcase *app)
{
	LrgVBox     *page = lrg_vbox_new ();
	LrgVBox     *box;
	LrgHBox     *row;
	guint        i;

	static const gchar *labels[] = { "Info", "Success", "Warning", "Danger" };

	lrg_container_set_spacing (LRG_CONTAINER (page), 20.0f);

	box = section (LRG_CONTAINER (page), "Toasts");
	row = lrg_hbox_new ();
	lrg_container_set_spacing (LRG_CONTAINER (row), 10.0f);
	for (i = 0; i < G_N_ELEMENTS (labels); i++)
	{
		LrgButton *btn = lrg_button_new (labels[i]);

		g_object_set_data (G_OBJECT (btn), "kind", GINT_TO_POINTER (i));
		g_signal_connect (btn, "clicked", G_CALLBACK (on_toast_clicked), app);
		add (row, btn);
	}
	app->toast_mode = lrg_combo_box_new ();
	lrg_combo_box_append (app->toast_mode, "Stack");
	lrg_combo_box_append (app->toast_mode, "Sequence");
	lrg_combo_box_append (app->toast_mode, "Replace");
	lrg_combo_box_set_selected (app->toast_mode, 0);
	g_signal_connect (app->toast_mode, "changed", G_CALLBACK (on_toast_mode_changed), app);
	lrg_container_add_child (LRG_CONTAINER (row), LRG_WIDGET (app->toast_mode));
	add (box, row);

	box = section (LRG_CONTAINER (page), "Dialog");
	add (box, label_with_class ("A modal sheet: the veil dims everything, focus is trapped,\n"
	                            "Escape or gamepad B cancels.", "muted"));
	{
		LrgButton *btn = lrg_button_new ("Ascend...");

		lrg_widget_add_class (LRG_WIDGET (btn), "primary");
		lrg_widget_set_halign (LRG_WIDGET (btn), LRG_ALIGN_START);
		g_signal_connect (btn, "clicked", G_CALLBACK (on_dialog_clicked), app);
		add (box, btn);
	}

	app->dialog = lrg_dialog_new ("Ascend this character?",
	                              "You keep your relics but restart at level 1.\n"
	                              "Ascension grants a permanent +10% to all stats.");
	lrg_dialog_add_button (app->dialog, "Not yet", LRG_DIALOG_RESPONSE_CANCEL, FALSE);
	lrg_dialog_add_button (app->dialog, "Ascend", 1, TRUE);
	g_signal_connect (app->dialog, "response", G_CALLBACK (on_dialog_response), app);

	return LRG_WIDGET (page);
}

static void
on_list_selection (LrgListView *list,
                   gpointer     user_data)
{
	Showcase         *app = user_data;
	guint             pos = lrg_list_view_get_selected (list);
	g_autofree gchar *text = NULL;

	if (pos == LRG_LIST_VIEW_INVALID_POSITION)
		text = g_strdup ("Nothing selected");
	else
		text = g_strdup_printf ("Selected row %u of %u", pos, (guint) N_LIST_ITEMS);
	lrg_label_set_text (app->selection, text);
}

static void
on_list_activated (LrgListView *list,
                   guint        position,
                   gpointer     user_data)
{
	g_autofree gchar *msg = g_strdup_printf ("Row %u was activated.", position);

	show_toast (user_data, "Opened", msg, LRG_TOAST_KIND_INFO);
}

static LrgWidget *
build_list_page (Showcase *app)
{
	LrgVBox             *page = lrg_vbox_new ();
	LrgListView         *list = lrg_list_view_new ();
	g_autoptr(GListStore) store = g_list_store_new (SHOWCASE_TYPE_ITEM);
	guint                i;

	lrg_container_set_spacing (LRG_CONTAINER (page), 12.0f);
	add (page, label_with_class ("10,000 rows, only the visible ones exist as widgets", "heading"));
	app->selection = label_with_class ("Nothing selected", "muted");
	lrg_container_add_child (LRG_CONTAINER (page), LRG_WIDGET (app->selection));

	for (i = 0; i < N_LIST_ITEMS; i++)
	{
		g_autoptr(ShowcaseItem) item = showcase_item_new (i);

		g_list_store_append (store, item);
	}
	lrg_list_view_set_model (list, G_LIST_MODEL (store));
	lrg_list_view_set_selection_mode (list, LRG_SELECTION_MODE_MULTIPLE);
	lrg_widget_set_fill_weight (LRG_WIDGET (list), 1.0f);
	g_signal_connect (list, "selection-changed", G_CALLBACK (on_list_selection), app);
	g_signal_connect (list, "row-activated", G_CALLBACK (on_list_activated), app);
	add (page, list);

	return LRG_WIDGET (page);
}

/* ==========================================================================
 * Data page: tree | table in a splitter, icon tiles underneath
 * ========================================================================== */

/* build_project: a fake game project for the tree */
static ShowcaseNode *
build_project (void)
{
	ShowcaseNode *root = showcase_node_new ("ember-quest", LRG_ICON_HOME, TRUE);
	ShowcaseNode *dir;
	ShowcaseNode *sub;

	dir = showcase_node_add (root, "assets", LRG_ICON_FOLDER, TRUE);
	sub = showcase_node_add (dir, "sprites", LRG_ICON_FOLDER, TRUE);
	showcase_node_add (sub, "hero.png", LRG_ICON_FILE, FALSE);
	showcase_node_add (sub, "slime.png", LRG_ICON_FILE, FALSE);
	showcase_node_add (sub, "tiles.png", LRG_ICON_FILE, FALSE);
	sub = showcase_node_add (dir, "audio", LRG_ICON_FOLDER, TRUE);
	showcase_node_add (sub, "theme.ogg", LRG_ICON_PLAY, FALSE);
	showcase_node_add (sub, "jump.wav", LRG_ICON_PLAY, FALSE);
	sub = showcase_node_add (dir, "fonts", LRG_ICON_FOLDER, TRUE);
	showcase_node_add (sub, "inter.ttf", LRG_ICON_FILE, FALSE);

	dir = showcase_node_add (root, "levels", LRG_ICON_FOLDER, TRUE);
	showcase_node_add (dir, "forest.rlevel", LRG_ICON_STAR, FALSE);
	showcase_node_add (dir, "caves.rlevel", LRG_ICON_STAR, FALSE);
	sub = showcase_node_add (dir, "bosses", LRG_ICON_FOLDER, TRUE);
	showcase_node_add (sub, "warden.rlevel", LRG_ICON_WARNING, FALSE);

	dir = showcase_node_add (root, "scripts", LRG_ICON_FOLDER, TRUE);
	showcase_node_add (dir, "main.lua", LRG_ICON_EDIT, FALSE);
	showcase_node_add (dir, "ai.lua", LRG_ICON_EDIT, FALSE);

	dir = showcase_node_add (root, "ui", LRG_ICON_FOLDER, TRUE);
	showcase_node_add (dir, "menu.yaml", LRG_ICON_MENU, FALSE);
	showcase_node_add (dir, "settings.yaml", LRG_ICON_GEAR, FALSE);

	showcase_node_add (root, "project.ryaml", LRG_ICON_GEAR, FALSE);
	showcase_node_add (root, "README.md", LRG_ICON_INFO, FALSE);
	return root;
}

static GListModel *
tree_children (LrgTreeView *tree,
               GObject     *item,
               gpointer     user_data)
{
	ShowcaseNode *node = SHOWCASE_NODE (item);

	/* transfer full; NULL for leaves */
	return node->children != NULL ? G_LIST_MODEL (g_object_ref (node->children)) : NULL;
}

static LrgWidget *
tree_create_row (LrgTreeView *tree,
                 gpointer     user_data)
{
	LrgHBox      *row = hrow (8.0f);
	ShowcaseIcon *icon = showcase_icon_new (LRG_ICON_FILE, 16.0f, LRG_THEME_COLOR_TEXT_MUTED);
	LrgLabel     *name = lrg_label_new ("");

	lrg_widget_set_name (LRG_WIDGET (icon), "icon");
	lrg_widget_set_name (LRG_WIDGET (name), "name");
	lrg_widget_set_valign (LRG_WIDGET (name), LRG_ALIGN_CENTER);
	add (row, icon);
	add (row, name);
	return LRG_WIDGET (row);
}

static void
tree_bind_row (LrgTreeView *tree,
               LrgWidget   *row,
               GObject     *item,
               guint        depth,
               gpointer     user_data)
{
	ShowcaseNode *node = SHOWCASE_NODE (item);

	/* Folders in the accent colour, files muted */
	showcase_icon_set (SHOWCASE_ICON (lrg_widget_find (row, "icon")), node->icon,
	                   node->children != NULL ? LRG_THEME_COLOR_ACCENT : LRG_THEME_COLOR_TEXT_MUTED);
	lrg_label_set_text (LRG_LABEL (lrg_widget_find (row, "name")), node->name);
}

/* update_crumbs: rebuild both breadcrumbs from the tree selection, root
 * first; each segment keeps a reference on its node */
static void
update_crumbs (Showcase     *app,
               ShowcaseNode *node)
{
	g_autoptr(GPtrArray) path = g_ptr_array_new ();
	ShowcaseNode        *n;
	guint                c;
	guint                i;

	for (n = node != NULL ? node : app->project; n != NULL; n = n->parent)
		g_ptr_array_insert (path, 0, n);

	for (c = 0; c < G_N_ELEMENTS (app->crumbs); c++)
	{
		if (app->crumbs[c] == NULL)
			continue;
		lrg_breadcrumb_remove_all (app->crumbs[c]);
		for (i = 0; i < path->len; i++)
		{
			ShowcaseNode *p = g_ptr_array_index (path, i);

			lrg_breadcrumb_append_with_data (app->crumbs[c], p->name, g_object_ref (p), g_object_unref);
		}
	}
}

static void
on_tree_selection (LrgTreeView *tree,
                   gpointer     user_data)
{
	Showcase *app = user_data;
	GObject  *item = lrg_tree_view_get_selected_item (tree);

	update_crumbs (app, item != NULL ? SHOWCASE_NODE (item) : NULL);
}

static void
on_tree_activated (LrgTreeView *tree,
                   GObject     *item,
                   gpointer     user_data)
{
	ShowcaseNode     *node = SHOWCASE_NODE (item);
	g_autofree gchar *msg = g_strdup_printf ("%s would open in the editor.", node->name);

	if (node->children == NULL)
		show_toast (user_data, "Open", msg, LRG_TOAST_KIND_INFO);
}

/* on_crumb_activated: clicking a breadcrumb selects that folder */
static void
on_crumb_activated (LrgBreadcrumb *crumbs,
                    guint          index,
                    gpointer       user_data)
{
	Showcase *app = user_data;
	gpointer  node = lrg_breadcrumb_get_segment_data (crumbs, index);

	if (node == NULL)
		return;
	if (node == app->project)
		lrg_tree_view_unselect_all (app->tree);
	else
		lrg_tree_view_select_item (app->tree, G_OBJECT (node));
}

static LrgBreadcrumb *
new_crumbs (Showcase *app)
{
	LrgBreadcrumb *crumbs = lrg_breadcrumb_new ();

	g_signal_connect (crumbs, "activated", G_CALLBACK (on_crumb_activated), app);
	return crumbs;
}

static void
on_table_sort (LrgTable *table,
               gint      column,
               gboolean  ascending,
               gpointer  user_data)
{
	Showcase         *app = user_data;
	g_autofree gchar *text = NULL;

	text = g_strdup_printf ("%u adventurers, sorted by %s (%s); drag header edges to resize",
	                        g_list_model_get_n_items (lrg_table_get_model (table)),
	                        column >= 0 ? lrg_table_get_column_title (table, (guint) column) : "nothing",
	                        ascending ? "ascending" : "descending");
	lrg_label_set_text (app->table_status, text);
}

static void
on_table_activated (LrgTable *table,
                    guint     position,
                    gpointer  user_data)
{
	g_autoptr(GObject) item = g_list_model_get_item (lrg_table_get_model (table), position);
	g_autofree gchar  *msg = g_strdup_printf ("%s the %s, level %u.", SHOWCASE_ROW (item)->name,
	                                          SHOWCASE_ROW (item)->role, SHOWCASE_ROW (item)->level);

	show_toast (user_data, "Adventurer", msg, LRG_TOAST_KIND_INFO);
}

/* power_cell: a formatted column (one decimal) instead of the raw
 * double the property column would print */
static gchar *
power_cell (LrgTable  *table,
            LrgWidget *cell,
            GObject   *item,
            guint      position,
            gpointer   user_data)
{
	return g_strdup_printf ("%.1f", SHOWCASE_ROW (item)->power);
}

static gint
by_power (gconstpointer a,
          gconstpointer b,
          gpointer      user_data)
{
	gdouble pa = SHOWCASE_ROW ((gpointer) a)->power;
	gdouble pb = SHOWCASE_ROW ((gpointer) b)->power;

	return (pa > pb) - (pa < pb);
}

static LrgWidget *
tile_create (LrgTileView *tiles,
             gpointer     user_data)
{
	LrgVBox      *tile = vcol (6.0f);
	ShowcaseIcon *icon = showcase_icon_new (LRG_ICON_STAR, 30.0f, LRG_THEME_COLOR_ACCENT);
	LrgLabel     *name = label_with_class ("", "muted");

	lrg_container_set_padding (LRG_CONTAINER (tile), 6.0f);
	lrg_widget_set_name (LRG_WIDGET (icon), "icon");
	lrg_widget_set_name (LRG_WIDGET (name), "name");
	lrg_widget_set_halign (LRG_WIDGET (icon), LRG_ALIGN_CENTER);
	lrg_widget_set_halign (LRG_WIDGET (name), LRG_ALIGN_CENTER);
	add (tile, icon);
	add (tile, name);
	return LRG_WIDGET (tile);
}

static void
tile_bind (LrgTileView *tiles,
           LrgWidget   *tile,
           GObject     *item,
           guint        position,
           gpointer     user_data)
{
	ShowcaseNode *node = SHOWCASE_NODE (item);

	showcase_icon_set (SHOWCASE_ICON (lrg_widget_find (tile, "icon")), node->icon, LRG_THEME_COLOR_ACCENT);
	lrg_label_set_text (LRG_LABEL (lrg_widget_find (tile, "name")), node->name);
}

static void
on_tile_activated (LrgTileView *tiles,
                   guint        position,
                   gpointer     user_data)
{
	g_autoptr(GObject) item = g_list_model_get_item (lrg_tile_view_get_model (tiles), position);
	g_autofree gchar  *upper = g_ascii_strup (SHOWCASE_NODE (item)->name, -1);
	g_autofree gchar  *msg = NULL;
	gchar             *p;

	/* "chevron-down" -> "CHEVRON_DOWN" */
	for (p = upper; *p != '\0'; p++)
		if (*p == '-')
			*p = '_';
	msg = g_strdup_printf ("lrg_paint_list_icon (list, LRG_ICON_%s, ...)", upper);
	show_toast (user_data, "Icon", msg, LRG_TOAST_KIND_INFO);
}

static LrgWidget *
build_data_page (Showcase *app)
{
	LrgVBox              *page = vcol (12.0f);
	LrgSplitter          *split = lrg_splitter_new (LRG_ORIENTATION_HORIZONTAL);
	LrgVBox              *left = vcol (8.0f);
	LrgVBox              *right = vcol (8.0f);
	LrgTileView          *tiles = lrg_tile_view_new ();
	g_autoptr(GListStore) roots = g_list_store_new (SHOWCASE_TYPE_NODE);
	g_autoptr(GListStore) rows = g_list_store_new (SHOWCASE_TYPE_ROW);
	g_autoptr(GListStore) icons = g_list_store_new (SHOWCASE_TYPE_NODE);
	guint                 col;
	guint                 i;

	/* Breadcrumb over the splitter, following the tree selection */
	app->crumbs[0] = new_crumbs (app);
	lrg_container_add_child (LRG_CONTAINER (page), LRG_WIDGET (app->crumbs[0]));

	/* Tree: a fake project; the root folder starts expanded */
	app->project = build_project ();
	g_list_store_append (roots, app->project);
	app->tree = lrg_tree_view_new ();
	lrg_tree_view_set_children_func (app->tree, tree_children, NULL, NULL);
	lrg_tree_view_set_factory (app->tree, tree_create_row, tree_bind_row, NULL, NULL);
	lrg_tree_view_set_model (app->tree, G_LIST_MODEL (roots));
	lrg_tree_view_set_indent (app->tree, 18.0f);
	lrg_tree_view_expand (app->tree, G_OBJECT (app->project));
	{
		g_autoptr(GObject) assets = g_list_model_get_item (G_LIST_MODEL (app->project->children), 0);

		g_autoptr(GObject) sprites = g_list_model_get_item (G_LIST_MODEL (SHOWCASE_NODE (assets)->children), 0);
		g_autoptr(GObject) hero = g_list_model_get_item (G_LIST_MODEL (SHOWCASE_NODE (sprites)->children), 0);

		/* Open assets/sprites and select a file, so the breadcrumbs
		 * start with a real path */
		lrg_tree_view_expand (app->tree, assets);
		lrg_tree_view_expand (app->tree, sprites);
		lrg_tree_view_select_item (app->tree, hero);
	}
	lrg_widget_set_fill_weight (LRG_WIDGET (app->tree), 1.0f);
	g_signal_connect (app->tree, "selection-changed", G_CALLBACK (on_tree_selection), app);
	g_signal_connect (app->tree, "row-activated", G_CALLBACK (on_tree_activated), app);
	add (left, label_with_class ("Project", "heading"));
	lrg_container_add_child (LRG_CONTAINER (left), LRG_WIDGET (app->tree));

	/* Table: 10,000 rows, sortable and resizable columns, striped */
	for (i = 0; i < N_TABLE_ROWS; i++)
	{
		g_autoptr(ShowcaseRow) row = showcase_row_new (i);

		g_list_store_append (rows, row);
	}
	app->table = lrg_table_new ();
	lrg_table_add_property_column (app->table, "#", "id", 64.0f, LRG_TABLE_COLUMN_SORTABLE);
	lrg_table_add_property_column (app->table, "Name", "name", 130.0f,
	                               LRG_TABLE_COLUMN_RESIZABLE | LRG_TABLE_COLUMN_SORTABLE);
	lrg_table_add_property_column (app->table, "Role", "role", 110.0f,
	                               LRG_TABLE_COLUMN_RESIZABLE | LRG_TABLE_COLUMN_SORTABLE);
	lrg_table_add_property_column (app->table, "Level", "level", 70.0f,
	                               LRG_TABLE_COLUMN_RESIZABLE | LRG_TABLE_COLUMN_SORTABLE);
	col = lrg_table_add_column (app->table, "Power", power_cell, NULL, NULL, 80.0f,
	                            LRG_TABLE_COLUMN_RESIZABLE | LRG_TABLE_COLUMN_SORTABLE);
	lrg_table_set_sort_func (app->table, col, by_power, NULL, NULL);
	lrg_table_add_property_column (app->table, "Region", "region", 150.0f,
	                               LRG_TABLE_COLUMN_RESIZABLE | LRG_TABLE_COLUMN_SORTABLE |
	                               LRG_TABLE_COLUMN_EXPAND);
	lrg_table_set_model (app->table, G_LIST_MODEL (rows));
	lrg_table_set_striped (app->table, TRUE);
	lrg_table_set_selection_mode (app->table, LRG_SELECTION_MODE_MULTIPLE);
	lrg_widget_set_fill_weight (LRG_WIDGET (app->table), 1.0f);
	app->table_status = label_with_class ("", "muted");
	g_signal_connect (app->table, "sort-changed", G_CALLBACK (on_table_sort), app);
	g_signal_connect (app->table, "row-activated", G_CALLBACK (on_table_activated), app);
	lrg_table_set_sort (app->table, 3, FALSE);   /* highest level first */
	lrg_container_add_child (LRG_CONTAINER (right), LRG_WIDGET (app->table_status));
	lrg_container_add_child (LRG_CONTAINER (right), LRG_WIDGET (app->table));

	/* Splitter: drag the handle, double-click resets */
	lrg_splitter_set_start_child (split, LRG_WIDGET (left));
	lrg_splitter_set_end_child (split, LRG_WIDGET (right));
	g_object_unref (left);
	g_object_unref (right);
	lrg_splitter_set_min_sizes (split, 180.0f, 360.0f);
	lrg_splitter_set_fraction (split, 0.27f, FALSE);
	lrg_splitter_set_default_fraction (split, 0.27f);
	lrg_widget_set_fill_weight (LRG_WIDGET (split), 1.0f);
	add (page, split);

	/* Tiles: every built-in vector icon (LrgIcon), by its nick */
	for (i = LRG_ICON_NONE + 1; lrg_paint_icon_get_name ((LrgIcon) i) != NULL; i++)
	{
		g_autoptr(ShowcaseNode) node = showcase_node_new (lrg_paint_icon_get_name ((LrgIcon) i),
		                                                  (LrgIcon) i, FALSE);

		g_list_store_append (icons, node);
	}
	lrg_tile_view_set_factory (tiles, tile_create, tile_bind, NULL, NULL);
	lrg_tile_view_set_tile_size (tiles, 124.0f, 76.0f);
	lrg_tile_view_set_tile_spacing (tiles, 8.0f);
	lrg_tile_view_set_model (tiles, G_LIST_MODEL (icons));
	lrg_tile_view_set_selection_mode (tiles, LRG_SELECTION_MODE_SINGLE);
	lrg_tile_view_select (tiles, 0);
	lrg_widget_set_min_size (LRG_WIDGET (tiles), 0.0f, 176.0f);
	lrg_widget_set_max_size (LRG_WIDGET (tiles), 0.0f, 176.0f);
	g_signal_connect (tiles, "tile-activated", G_CALLBACK (on_tile_activated), app);
	{
		g_autofree gchar *title = g_strdup_printf ("%u built-in vector icons",
		                                           g_list_model_get_n_items (G_LIST_MODEL (icons)));

		add (page, label_with_class (title, "heading"));
	}
	add (page, tiles);

	return LRG_WIDGET (page);
}

/* ==========================================================================
 * Pickers page
 * ========================================================================== */

/* sync_pickers_to_accent: show the current accent in both pickers
 * without feeding their color-changed back into the theme */
static void
sync_pickers_to_accent (Showcase *app)
{
	GrlColor accent;
	guint    i;

	if (app->accent == NULL || !lrg_color_parse (app->accent, &accent))
		accent = *lrg_theme_get_color (lrg_theme_get_default (), LRG_THEME_COLOR_ACCENT);

	app->syncing = TRUE;
	for (i = 0; i < G_N_ELEMENTS (app->pickers); i++)
		if (app->pickers[i] != NULL)
			lrg_color_picker_set_color (app->pickers[i], &accent);
	app->syncing = FALSE;
}

/* on_picker_changed: either picker drives the live theme accent */
static void
on_picker_changed (LrgColorPicker *picker,
                   gpointer        user_data)
{
	Showcase         *app = user_data;
	g_autofree gchar *hex = NULL;
	g_autofree gchar *text = NULL;
	GrlColor          c;

	if (app->syncing)
		return;

	lrg_color_picker_get_color (picker, &c);
	c.a = 255;
	set_accent (app, &c);
	sync_pickers_to_accent (app);
	hex = lrg_color_picker_get_hex (picker);
	text = g_strdup_printf ("Accent %s\nre-derived on every page:\nhover, pressed and\nfocus colours follow.", hex);
	lrg_label_set_text (app->picker_status, text);
}

static void
on_rating_changed (LrgRating *rating,
                   gpointer   user_data)
{
	g_autofree gchar *msg = g_strdup_printf ("%.1f / %u", lrg_rating_get_value (rating),
	                                         lrg_rating_get_max (rating));

	show_toast (user_data, "Rated", msg, LRG_TOAST_KIND_SUCCESS);
}

static void
on_date_changed (LrgDatePicker *picker,
                 gpointer       user_data)
{
	g_autofree gchar *text = lrg_date_picker_get_text (picker);

	show_toast (user_data, "Date", text, LRG_TOAST_KIND_INFO);
}

static LrgHBox *
labelled (const gchar *text,
          LrgWidget   *widget)
{
	LrgHBox  *row = hrow (12.0f);
	LrgLabel *label = label_with_class (text, "muted");

	lrg_widget_set_min_size (LRG_WIDGET (label), 110.0f, 0.0f);
	lrg_widget_set_valign (LRG_WIDGET (label), LRG_ALIGN_CENTER);
	lrg_widget_set_valign (widget, LRG_ALIGN_CENTER);
	add (row, label);
	add (row, widget);
	return row;
}

static LrgWidget *
build_pickers_page (Showcase *app)
{
	static const GrlColor palette[] = {
		{ 124, 108, 255, 255 }, { 34, 211, 238, 255 }, { 52, 211, 153, 255 },
		{ 251, 191, 36, 255 }, { 255, 122, 69, 255 }, { 244, 114, 182, 255 }
	};
	LrgHBox       *page = hrow (20.0f);
	LrgVBox       *left = vcol (20.0f);
	LrgVBox       *right = vcol (20.0f);
	LrgVBox       *box;
	LrgHBox       *split;
	LrgVBox       *side;
	LrgRating     *rating;
	LrgDatePicker *date;
	LrgTimePicker *time;

	lrg_widget_set_fill_weight (LRG_WIDGET (left), 1.0f);
	lrg_widget_set_fill_weight (LRG_WIDGET (right), 1.0f);

	/* Inline colour picker, wired to the live accent */
	box = section (LRG_CONTAINER (left), "Accent colour");
	split = hrow (20.0f);
	app->pickers[0] = lrg_color_picker_new ();
	lrg_color_picker_set_palette (app->pickers[0], palette, G_N_ELEMENTS (palette));
	lrg_color_picker_set_show_alpha (app->pickers[0], FALSE);
	g_signal_connect (app->pickers[0], "color-changed", G_CALLBACK (on_picker_changed), app);

	/* The picker is laid out for its preferred width (its height grows
	 * with the width), so keep it at that width */
	lrg_widget_set_halign (LRG_WIDGET (app->pickers[0]), LRG_ALIGN_START);
	lrg_widget_set_valign (LRG_WIDGET (app->pickers[0]), LRG_ALIGN_START);
	lrg_container_add_child (LRG_CONTAINER (split), LRG_WIDGET (app->pickers[0]));

	/* The same in popup mode: a swatch button that opens a popup */
	side = vcol (10.0f);
	lrg_widget_set_fill_weight (LRG_WIDGET (side), 1.0f);
	add (side, label_with_class ("Popup mode", NULL));
	app->pickers[1] = lrg_color_picker_new ();
	lrg_color_picker_set_popup_mode (app->pickers[1], TRUE);
	lrg_color_picker_set_show_alpha (app->pickers[1], FALSE);
	lrg_color_picker_set_palette (app->pickers[1], palette, G_N_ELEMENTS (palette));
	lrg_widget_set_halign (LRG_WIDGET (app->pickers[1]), LRG_ALIGN_START);
	g_signal_connect (app->pickers[1], "color-changed", G_CALLBACK (on_picker_changed), app);
	g_object_ref (app->pickers[1]);
	add (side, app->pickers[1]);
	app->picker_status = label_with_class ("Both pickers drive the\ntheme accent live:\nhover, pressed and\nfocus colours follow.",
	                                       "muted");
	add (side, app->picker_status);
	add (split, side);
	add (box, split);
	sync_pickers_to_accent (app);

	/* Accent preview: widgets whose looks derive from the accent */
	box = section (LRG_CONTAINER (left), "Accent preview");
	split = hrow (12.0f);
	{
		LrgButton      *btn = lrg_button_new ("Primary");
		LrgCheckbox    *sw = lrg_checkbox_new_with_style ("Switch", LRG_CHECKBOX_STYLE_SWITCH);
		LrgSlider      *slider = lrg_slider_new_with_range (0.0, 100.0, 1.0);
		LrgProgressBar *bar = lrg_progress_bar_new ();

		lrg_widget_add_class (LRG_WIDGET (btn), "primary");
		lrg_checkbox_set_checked (sw, TRUE);
		lrg_widget_set_valign (LRG_WIDGET (sw), LRG_ALIGN_CENTER);
		lrg_slider_set_value (slider, 70.0);
		lrg_widget_set_fill_weight (LRG_WIDGET (slider), 1.0f);
		lrg_widget_set_valign (LRG_WIDGET (slider), LRG_ALIGN_CENTER);
		add (split, btn);
		add (split, sw);
		add (split, slider);
		add (box, split);
		lrg_progress_bar_set_max (bar, 100.0);
		lrg_progress_bar_set_value (bar, 70.0);
		add (box, bar);
	}

	/* Ratings */
	box = section (LRG_CONTAINER (right), "Rating");
	rating = lrg_rating_new ();
	lrg_rating_set_allow_half (rating, TRUE);
	lrg_rating_set_value (rating, 3.5);
	lrg_rating_set_symbol_size (rating, 28.0f);
	g_signal_connect (rating, "value-changed", G_CALLBACK (on_rating_changed), app);
	add (box, labelled ("Half stars", LRG_WIDGET (rating)));
	rating = lrg_rating_new ();
	lrg_rating_set_shape (rating, LRG_RATING_SHAPE_HEART);
	lrg_rating_set_max (rating, 6);
	lrg_rating_set_value (rating, 4.0);
	lrg_rating_set_symbol_size (rating, 24.0f);
	g_signal_connect (rating, "value-changed", G_CALLBACK (on_rating_changed), app);
	add (box, labelled ("Hearts", LRG_WIDGET (rating)));
	rating = lrg_rating_new ();
	lrg_rating_set_shape (rating, LRG_RATING_SHAPE_CIRCLE);
	lrg_rating_set_max (rating, 4);
	lrg_rating_set_value (rating, 2.0);
	lrg_rating_set_read_only (rating, TRUE);
	add (box, labelled ("Read-only", LRG_WIDGET (rating)));

	/* Date and time */
	box = section (LRG_CONTAINER (right), "Date and time");
	date = lrg_date_picker_new ();
	lrg_date_picker_set_format (date, "%a %d %b %Y");
	lrg_date_picker_set_date_ymd (date, 2026, 11, 6);
	g_signal_connect (date, "date-changed", G_CALLBACK (on_date_changed), app);
	lrg_widget_set_min_size (LRG_WIDGET (date), 200.0f, 0.0f);
	add (box, labelled ("Release", LRG_WIDGET (date)));
	time = lrg_time_picker_new ();
	lrg_time_picker_set_time (time, 21, 30, 0);
	add (box, labelled ("Raid (24h)", LRG_WIDGET (time)));
	time = lrg_time_picker_new ();
	lrg_time_picker_set_use_24h (time, FALSE);
	lrg_time_picker_set_show_seconds (time, TRUE);
	lrg_time_picker_set_time (time, 7, 5, 42);
	add (box, labelled ("Alarm (12h)", LRG_WIDGET (time)));

	/* Breadcrumb mirroring the data page's tree selection */
	box = section (LRG_CONTAINER (right), "Breadcrumb");
	app->crumbs[1] = new_crumbs (app);
	g_object_ref (app->crumbs[1]);
	add (box, app->crumbs[1]);
	add (box, label_with_class ("Follows the tree selection on the Data page;\n"
	                            "click a segment to jump back up.", "muted"));

	add (page, left);
	add (page, right);
	return LRG_WIDGET (page);
}

/* ==========================================================================
 * Glass and effects page
 * ========================================================================== */

static void
on_fx_clicked (LrgButton *button,
               gpointer   user_data)
{
	g_autofree gchar *msg = g_strdup_printf ("lrg_part_look_set_effect (look, \"%s\")",
	                                         lrg_button_get_text (button));

	show_toast (user_data, "Effect", msg, LRG_TOAST_KIND_INFO);
}

static LrgWidget *
build_glass_page (Showcase *app)
{
	static const struct { const gchar *klass; const gchar *title; const gchar *text; } glasses[] = {
		{ "glass-clear",   "Clear glass", "material: glass\nthe rim refracts and\nsplits colour" },
		{ "glass-frosted", "Frosted",     "material: frosted\na 22 unit blur under\na white veil" },
		{ "glass-tinted",  "Tinted",      "material: tinted\nfrosted, tinted by\nthe theme accent" }
	};
	LrgOverlay       *page = lrg_overlay_new ();
	LrgVBox          *content = vcol(16.0f);
	LrgHBox          *row = hrow (16.0f);
	LrgGrid          *grid = lrg_grid_new (4);
	LrgPanel         *panel;
	LrgVBox          *box;
	g_auto(GStrv)     names = lrg_paint_effect_list_names ();
	guint             i;

	/* The animated backdrop fills the page; content floats over it */
	add (page, g_object_new (SHOWCASE_TYPE_BACKDROP, NULL));
	lrg_container_set_padding (LRG_CONTAINER (content), 22.0f);

	/* Three materials, chosen by style class through theme rules */
	for (i = 0; i < G_N_ELEMENTS (glasses); i++)
	{
		box = card (LRG_CONTAINER (row), glasses[i].title, &panel);
		lrg_widget_add_class (LRG_WIDGET (panel), glasses[i].klass);
		lrg_panel_set_elevation (panel, 2);
		lrg_widget_set_fill_weight (LRG_WIDGET (panel), 1.0f);
		add (box, label_with_class (glasses[i].text, NULL));
	}
	add (content, row);

	/* One button per effect shader, styled by an fx-NAME rule */
	add (content, label_with_class ("Effect shaders: hover and press them", "heading"));
	lrg_grid_set_column_spacing (grid, 12.0f);
	lrg_grid_set_row_spacing (grid, 12.0f);
	for (i = 0; names != NULL && names[i] != NULL; i++)
	{
		g_autofree gchar *klass = g_strconcat ("fx-", names[i], NULL);
		LrgButton        *btn = lrg_button_new (names[i]);

		lrg_widget_add_class (LRG_WIDGET (btn), klass);
		lrg_widget_set_min_size (LRG_WIDGET (btn), 0.0f, 64.0f);
		g_signal_connect (btn, "clicked", G_CALLBACK (on_fx_clicked), app);
		add (grid, btn);
	}
	add (content, grid);

	/* A node graph whose wires carry gradient pulses, on frosted glass */
	box = card (LRG_CONTAINER (content), "Curves: gradient wires with travelling pulses", &panel);
	lrg_widget_add_class (LRG_WIDGET (panel), "glass-frosted");
	lrg_panel_set_elevation (panel, 2);
	lrg_widget_set_fill_weight (LRG_WIDGET (panel), 1.0f);
	{
		LrgWidget *wires = g_object_new (SHOWCASE_TYPE_WIRES, NULL);

		lrg_widget_set_fill_weight (wires, 1.0f);
		add (box, wires);
	}

	add (page, content);
	return LRG_WIDGET (page);
}

/* ==========================================================================
 * Layout and motion page
 * ========================================================================== */

static const gchar *chip_names[] = {
	"Fire", "Ice", "Lightning", "Poison", "Holy", "Shadow", "Wind", "Earth",
	"Arcane", "Blood", "Nature", "Metal", "Sound", "Light", "Void", "Time"
};

/* new_chip: a pill button that glides when the wrap box reflows and
 * pops in when it is added */
static LrgButton *
new_chip (Showcase *app)
{
	LrgButton *chip = lrg_button_new (chip_names[app->next_chip % G_N_ELEMENTS (chip_names)]);

	app->next_chip++;
	lrg_button_set_corner_radius (chip, 999.0f);
	lrg_widget_set_animate_layout (LRG_WIDGET (chip), TRUE);
	lrg_widget_set_appear (LRG_WIDGET (chip), LRG_APPEAR_POP, 0.0f);
	if (app->next_chip % 3 == 0)
		lrg_widget_add_class (LRG_WIDGET (chip), "primary");
	return chip;
}

static void
on_chip_add (LrgButton *button,
             gpointer   user_data)
{
	Showcase  *app = user_data;
	LrgButton *chip = new_chip (app);

	add (app->chips, chip);
}

static void
on_chip_remove (LrgButton *button,
                gpointer   user_data)
{
	Showcase *app = user_data;
	guint     n = lrg_container_get_child_count (LRG_CONTAINER (app->chips));

	/* Remove from the front: everything after it glides left/up */
	if (n > 0)
		lrg_container_remove_child (LRG_CONTAINER (app->chips),
		                            lrg_container_get_child (LRG_CONTAINER (app->chips), 0));
}

/* on_chip_shuffle: re-insert the same widgets in a new order; they keep
 * their glide springs, so each one flies to its new slot */
static void
on_chip_shuffle (LrgButton *button,
                 gpointer   user_data)
{
	Showcase             *app = user_data;
	g_autoptr(GPtrArray)  kids = g_ptr_array_new_with_free_func (g_object_unref);
	GList                *children = lrg_container_get_children (LRG_CONTAINER (app->chips));
	GList                *l;
	guint                 i;

	for (l = children; l != NULL; l = l->next)
		g_ptr_array_add (kids, g_object_ref (l->data));
	g_list_free (children);

	/* Fisher-Yates with a small LCG, so runs are reproducible */
	for (i = kids->len; i > 1; i--)
	{
		guint    j;
		gpointer tmp;

		app->shuffle_seed = app->shuffle_seed * 1664525u + 1013904223u;
		j = (app->shuffle_seed >> 8) % i;
		tmp = kids->pdata[i - 1];
		kids->pdata[i - 1] = kids->pdata[j];
		kids->pdata[j] = tmp;
	}

	lrg_container_remove_all (LRG_CONTAINER (app->chips));
	for (i = 0; i < kids->len; i++)
		lrg_container_add_child (LRG_CONTAINER (app->chips), g_ptr_array_index (kids, i));
}

static void
on_replay_appear (LrgButton *button,
                  gpointer   user_data)
{
	Showcase *app = user_data;
	guint     i;

	for (i = 0; i < app->appear_tiles->len; i++)
		lrg_widget_play_appear (g_ptr_array_index (app->appear_tiles, i));
}

static void
on_island_toggled (LrgIsland *island,
                   gboolean   expanded,
                   gpointer   user_data)
{
	/* Nothing to do: the island morphs by itself. A real app would
	 * pause or resume a timer here. */
}

static LrgWidget *
build_island (Showcase *app)
{
	LrgHBox   *compact = hrow (10.0f);
	LrgVBox   *expanded = vcol (10.0f);
	LrgHBox   *row;
	LrgButton *btn;
	LrgSlider *seek;

	app->island = lrg_island_new ();

	/* Compact: an icon and a timer */
	add (compact, showcase_icon_new (LRG_ICON_PLAY, 16.0f, LRG_THEME_COLOR_ACCENT));
	add (compact, label_with_class ("Ember Overture  3:42", NULL));

	/* Expanded: track info and transport controls */
	row = hrow (12.0f);
	add (row, showcase_icon_new (LRG_ICON_STAR, 28.0f, LRG_THEME_COLOR_ACCENT));
	{
		LrgVBox *meta = vcol (2.0f);

		add (meta, label_with_class ("Ember Overture", "heading"));
		add (meta, label_with_class ("Ashfall Reach OST  -  track 3 of 12", "muted"));
		add (row, meta);
	}
	add (expanded, row);
	seek = lrg_slider_new_with_range (0.0, 222.0, 1.0);
	lrg_slider_set_value (seek, 81.0);
	lrg_widget_set_min_size (LRG_WIDGET (seek), 300.0f, 0.0f);
	add (expanded, seek);
	row = hrow (8.0f);
	lrg_widget_set_halign (LRG_WIDGET (row), LRG_ALIGN_CENTER);
	add (row, lrg_button_new ("Prev"));
	btn = lrg_button_new ("Pause");
	lrg_widget_add_class (LRG_WIDGET (btn), "primary");
	add (row, btn);
	add (row, lrg_button_new ("Next"));
	add (expanded, row);

	lrg_island_set_compact_child (app->island, LRG_WIDGET (compact));
	lrg_island_set_expanded_child (app->island, LRG_WIDGET (expanded));
	g_object_unref (compact);
	g_object_unref (expanded);
	lrg_widget_set_halign (LRG_WIDGET (app->island), LRG_ALIGN_CENTER);
	g_signal_connect (app->island, "toggled", G_CALLBACK (on_island_toggled), app);
	return LRG_WIDGET (app->island);
}

static LrgWidget *
build_motion_page (Showcase *app)
{
	static const struct { const gchar *title; const gchar *body; gboolean open; } expanders[] = {
		{ "What is layout glide?",
		  "animate-layout: the painted\n"
		  "position follows a spring while\n"
		  "layout and hit-testing jump.", TRUE },
		{ "Why is the island dark?",
		  "On light themes it paints\n"
		  "with a dark twin of the theme.", FALSE },
		{ "Can expanders nest?",
		  "Yes: the reveal is a spring\n"
		  "on the height.", FALSE }
	};
	static const struct { const gchar *name; const gchar *klass; LrgAppear appear; } appears[] = {
		{ "fade",   "card-0", LRG_APPEAR_FADE },
		{ "pop",    "card-1", LRG_APPEAR_POP },
		{ "zoom",   "card-2", LRG_APPEAR_ZOOM },
		{ "fly up", "card-3", LRG_APPEAR_FLY_UP },
		{ "drop",   "card-4", LRG_APPEAR_DROP }
	};
	LrgVBox   *page = vcol (16.0f);
	LrgHBox   *row = hrow (20.0f);
	LrgVBox   *box;
	LrgVBox   *col;
	LrgHBox   *buttons;
	LrgButton *btn;
	guint      i;

	/* The island sits at the top, centred; click it to expand */
	add (page, build_island (app));
	{
		LrgLabel *hint = label_with_class ("Click the island: size, radius and content morph on springs", "muted");

		lrg_widget_set_halign (LRG_WIDGET (hint), LRG_ALIGN_CENTER);
		add (page, hint);
	}

	/* Wrap box of chips with glide */
	col = vcol (20.0f);
	lrg_widget_set_fill_weight (LRG_WIDGET (col), 3.0f);
	box = section (LRG_CONTAINER (col), "Wrap box with layout glide");
	buttons = hrow (8.0f);
	btn = lrg_button_new ("Add");
	lrg_widget_add_class (LRG_WIDGET (btn), "primary");
	g_signal_connect (btn, "clicked", G_CALLBACK (on_chip_add), app);
	add (buttons, btn);
	btn = lrg_button_new ("Remove first");
	g_signal_connect (btn, "clicked", G_CALLBACK (on_chip_remove), app);
	add (buttons, btn);
	btn = lrg_button_new ("Shuffle");
	g_signal_connect (btn, "clicked", G_CALLBACK (on_chip_shuffle), app);
	add (buttons, btn);
	add (box, buttons);
	app->chips = lrg_wrap_box_new ();
	lrg_container_set_spacing (LRG_CONTAINER (app->chips), 8.0f);
	lrg_wrap_box_set_line_spacing (app->chips, 8.0f);
	/* A wrap box's preferred width is its single-line length; cap it so
	 * the row beside the expanders cannot push the page wider */
	lrg_widget_set_min_size (LRG_WIDGET (app->chips), 0.0f, 100.0f);
	lrg_widget_set_max_size (LRG_WIDGET (app->chips), 560.0f, 0.0f);
	app->shuffle_seed = 7u;
	for (i = 0; i < 11; i++)
		add (app->chips, new_chip (app));
	g_object_ref (app->chips);
	add (box, app->chips);

	/* Appear transitions, replayable */
	box = section (LRG_CONTAINER (col), "Appear transitions");
	buttons = hrow (10.0f);
	for (i = 0; i < G_N_ELEMENTS (appears); i++)
	{
		LrgPanel *tile = lrg_panel_new ();
		LrgLabel *name = label_with_class (appears[i].name, NULL);

		lrg_panel_set_elevation (tile, 1);
		lrg_panel_set_fill_children (tile, TRUE);
		lrg_container_set_padding (LRG_CONTAINER (tile), 12.0f);
		lrg_widget_add_class (LRG_WIDGET (tile), appears[i].klass);
		lrg_widget_set_fill_weight (LRG_WIDGET (tile), 1.0f);
		lrg_widget_set_halign (LRG_WIDGET (name), LRG_ALIGN_CENTER);
		lrg_widget_set_appear (LRG_WIDGET (tile), appears[i].appear, 0.08f * (gfloat) i);
		add (tile, name);
		g_ptr_array_add (app->appear_tiles, g_object_ref (tile));
		add (buttons, tile);
	}
	btn = lrg_button_new ("Replay");
	lrg_widget_add_class (LRG_WIDGET (btn), "flat");
	g_signal_connect (btn, "clicked", G_CALLBACK (on_replay_appear), app);
	add (buttons, btn);
	add (box, buttons);
	add (row, col);

	/* Expanders */
	col = vcol (20.0f);
	lrg_widget_set_fill_weight (LRG_WIDGET (col), 2.0f);
	box = section (LRG_CONTAINER (col), "Expanders");
	for (i = 0; i < G_N_ELEMENTS (expanders); i++)
	{
		LrgExpander *exp = lrg_expander_new (expanders[i].title);

		lrg_expander_set_child (exp, LRG_WIDGET (label_with_class (expanders[i].body, "muted")));
		g_object_unref (lrg_expander_get_child (exp));
		lrg_expander_set_expanded (exp, expanders[i].open);
		add (box, exp);
	}

	/* Touch note */
	box = section (LRG_CONTAINER (col), "Touch");
	add (box, label_with_class ("Scroll views, lists, tables,\n"
	                            "trees and tiles claim a one-\n"
	                            "finger pan and flick with\n"
	                            "momentum and a rubber band.\n"
	                            "Taps click, long presses\n"
	                            "right-click. Run with --touch.",
	                            "muted"));

	add (row, col);

	add (page, row);
	return LRG_WIDGET (page);
}

/* ==========================================================================
 * Markdown page
 * ========================================================================== */

static const gchar *markdown_text =
	"# Patch notes 1.4\n"
	"\n"
	"[typewriter=60]Welcome to the **Ember** update. This whole page is one `LrgMarkdownLabel`.[/typewriter]\n"
	"Rich-text tags pass straight through: text can [wave]wave[/wave], go [rainbow]full rainbow[/rainbow],\n"
	"[shake]shake with fear[/shake] or [pulse]pulse[/pulse].\n"
	"\n"
	"## What's new\n"
	"\n"
	"- **Glass** materials: *clear*, *frosted* and ***tinted***\n"
	"- Eight effect shaders, from `aurora` to `mesh-gradient`\n"
	"  - every one reacts to hover and press springs\n"
	"  - `spotlight` and `ripple` follow the pointer\n"
	"- Read the [paint module](docs://paint) or the [styling guide](docs://styling)\n"
	"\n"
	"## Try it\n"
	"\n"
	"1. Open the **Glass** page and hover the effect buttons\n"
	"2. Press `F12`, then click any widget to inspect it\n"
	"3. Edit `showcase-settings.yaml` while the app runs\n"
	"\n"
	"> Links raise a toast through the *link-activated* signal.\n"
	"\n"
	"```\n"
	"g_signal_connect (label, \"link-activated\", G_CALLBACK (on_link), app);\n"
	"```\n"
	"\n"
	"## Known issues\n"
	"\n"
	"- [s]The Warden ignores parries[/s] fixed in 1.4.1\n"
	"- [color=#f472b6]Pink[/color] slimes drop [b]double[/b] loot on [u]weekends[/u]\n"
	"- [fade]Fog fades in[/fade] a little too slowly at dusk\n";

static void
on_markdown_link (LrgMarkdownLabel *label,
                  const gchar      *url,
                  gpointer          user_data)
{
	show_toast (user_data, "Link activated", url, LRG_TOAST_KIND_INFO);
}

static LrgWidget *
build_markdown_page (Showcase *app)
{
	LrgScrollBox *scroll = lrg_scroll_box_new ();
	LrgPanel     *panel = lrg_panel_new ();

	app->markdown = lrg_markdown_label_new (markdown_text);
	g_signal_connect (app->markdown, "link-activated", G_CALLBACK (on_markdown_link), app);
	lrg_panel_set_elevation (panel, 1);
	lrg_panel_set_fill_children (panel, TRUE);
	lrg_container_set_padding (LRG_CONTAINER (panel), 26.0f);
	lrg_container_add_child (LRG_CONTAINER (panel), LRG_WIDGET (app->markdown));
	lrg_scroll_box_set_child (scroll, LRG_WIDGET (panel));
	g_object_unref (panel);
	lrg_scroll_box_set_kinetic (scroll, TRUE);
	lrg_scroll_box_set_hscroll (scroll, FALSE);   /* wrap to the viewport width */
	return LRG_WIDGET (scroll);
}

/* ==========================================================================
 * Declarative page: a hot-reloaded YAML document
 * ========================================================================== */

/* find_settings_file: the document next to the sources, whether the
 * example runs from the repository root, examples/ or its build dir */
static gchar *
find_settings_file (void)
{
	static const gchar *candidates[] = {
		"data/ui/" SETTINGS_FILE,
		"examples/data/ui/" SETTINGS_FILE,
		"../../../data/ui/" SETTINGS_FILE
	};
	g_autofree gchar *exe = g_file_read_link ("/proc/self/exe", NULL);
	guint             i;

	for (i = 0; i < G_N_ELEMENTS (candidates); i++)
		if (g_file_test (candidates[i], G_FILE_TEST_IS_REGULAR))
			return g_canonicalize_filename (candidates[i], NULL);

	/* build/release/examples/ui-showcase -> examples/data/ui/ */
	if (exe != NULL)
	{
		g_autofree gchar *dir = g_path_get_dirname (exe);
		g_autofree gchar *path = g_build_filename (dir, "..", "..", "..", "data", "ui", SETTINGS_FILE, NULL);

		if (g_file_test (path, G_FILE_TEST_IS_REGULAR))
			return g_canonicalize_filename (path, NULL);
	}
	return NULL;
}

static void
on_settings_apply (LrgButton *button,
                   gpointer   user_data)
{
	show_toast (user_data, "Applied", "Handler settings_apply from the YAML.", LRG_TOAST_KIND_SUCCESS);
}

static void
on_settings_reset (LrgButton *button,
                   gpointer   user_data)
{
	show_toast (user_data, "Reset", "Handler settings_reset from the YAML.", LRG_TOAST_KIND_INFO);
}

/* on_settings_slider: the document names sliders "slider-NAME" and their
 * value labels "value-NAME"; keep the label in step */
static void
on_settings_slider (LrgSlider *slider,
                    gpointer   user_data)
{
	Showcase         *app = user_data;
	const gchar      *id = lrg_widget_get_name (LRG_WIDGET (slider));
	g_autofree gchar *label_id = NULL;
	g_autofree gchar *text = NULL;
	LrgWidget        *label;

	if (id == NULL || !g_str_has_prefix (id, "slider-"))
		return;
	label_id = g_strconcat ("value-", id + strlen ("slider-"), NULL);
	label = lrg_ui_loader_get_widget (app->loader, label_id);
	if (label == NULL || !LRG_IS_LABEL (label))
		return;
	text = g_strdup_printf ("%.0f%s", lrg_slider_get_value (slider),
	                        lrg_slider_get_max (slider) == 100.0 ? "%" : "");
	lrg_label_set_text (LRG_LABEL (label), text);
}

static void
on_yaml_reloaded (LrgUILoader *loader,
                  const gchar *path,
                  LrgWidget   *root,
                  gpointer     user_data)
{
	Showcase         *app = user_data;
	g_autofree gchar *base = g_path_get_basename (path);

	show_toast (app, "Reloaded", base, LRG_TOAST_KIND_SUCCESS);
}

static void
on_yaml_reload_failed (LrgUILoader  *loader,
                       const gchar  *path,
                       const GError *error,
                       gpointer      user_data)
{
	show_toast (user_data, "YAML error", error->message, LRG_TOAST_KIND_DANGER);
}

static LrgWidget *
build_declarative_page (Showcase *app)
{
	LrgVBox           *page = vcol (12.0f);
	g_autoptr(GError)  error = NULL;
	g_autofree gchar  *note = NULL;

	app->yaml_path = find_settings_file ();
	app->yaml_host = lrg_panel_new ();
	lrg_panel_set_elevation (app->yaml_host, 1);
	lrg_panel_set_fill_children (app->yaml_host, TRUE);
	lrg_widget_set_fill_weight (LRG_WIDGET (app->yaml_host), 1.0f);

	app->loader = lrg_ui_loader_new ();
	lrg_ui_loader_add_handler (app->loader, "settings_apply", G_CALLBACK (on_settings_apply), app);
	lrg_ui_loader_add_handler (app->loader, "settings_reset", G_CALLBACK (on_settings_reset), app);
	lrg_ui_loader_add_handler (app->loader, "settings_slider_changed", G_CALLBACK (on_settings_slider), app);
	g_signal_connect (app->loader, "reloaded", G_CALLBACK (on_yaml_reloaded), app);
	g_signal_connect (app->loader, "reload-failed", G_CALLBACK (on_yaml_reload_failed), app);

	/* Watch the file: lrg_ui_loader_poll() in the frame loop notices
	 * saves and diffs the new document into the live widgets */
	if (app->yaml_path == NULL)
	{
		note = g_strdup ("examples/data/ui/" SETTINGS_FILE " was not found; run from the repository.");
	}
	else if (lrg_ui_loader_watch_file (app->loader, app->yaml_path,
	                                   LRG_CONTAINER (app->yaml_host), &error) == NULL)
	{
		note = g_strdup_printf ("Could not load %s: %s", app->yaml_path, error->message);
	}
	else
	{
		g_autofree gchar *base = g_path_get_basename (app->yaml_path);

		note = g_strdup_printf ("Built by LrgUILoader from %s, watched with lrg_ui_loader_poll () every frame.\n"
		                        "Save the file while the app runs: the new document is diffed into the live widgets\n"
		                        "(state survives; a broken document keeps the old tree and raises a toast).", base);
	}

	add (page, label_with_class ("Declarative UI (YAML, hot reload)", "heading"));
	app->yaml_status = label_with_class (note, "muted");
	lrg_container_add_child (LRG_CONTAINER (page), LRG_WIDGET (app->yaml_status));
	lrg_container_add_child (LRG_CONTAINER (page), LRG_WIDGET (app->yaml_host));
	return LRG_WIDGET (page);
}

/* ==========================================================================
 * Inspector page
 * ========================================================================== */

/* short_path: the last three segments of a widget path, so the label
 * stays narrow ("…/LrgVBox[1]/LrgPanel#sample-card") */
static gchar *
short_path (const gchar *path)
{
	g_auto(GStrv) parts = NULL;
	guint         n;

	if (path == NULL)
		return g_strdup ("(internal part)");
	parts = g_strsplit (path, "/", -1);
	n = g_strv_length (parts);
	if (n <= 3)
		return g_strdup (path);
	return g_strdup_printf (".../%s/%s/%s", parts[n - 3], parts[n - 2], parts[n - 1]);
}

/* update_inspector_labels: pick mode, the selection's path and the
 * first lines of lrg_ui_inspector_describe_widget() as a code block */
static void
update_inspector_labels (Showcase *app)
{
	LrgWidget        *selected = lrg_ui_inspector_get_selected (app->inspector);
	g_autofree gchar *path = selected != NULL ? lrg_ui_inspector_get_path (app->inspector, selected) : NULL;
	g_autofree gchar *shortened = short_path (path);
	g_autofree gchar *sel = NULL;
	g_autofree gchar *json = NULL;
	g_autoptr(GString) md = g_string_new ("```\n");

	lrg_label_set_text (app->inspect_mode, lrg_ui_inspector_get_pick_mode (app->inspector)
	                    ? "Pick mode: ON  (hover highlights, click selects, Escape leaves)"
	                    : "Pick mode: off  (press F12)");
	if (selected == NULL)
		sel = g_strdup ("Selected: nothing");
	else
		sel = g_strdup_printf ("Selected: %s\n%s", G_OBJECT_TYPE_NAME (selected), shortened);
	lrg_label_set_text (app->inspect_selected, sel);

	json = selected != NULL ? lrg_ui_inspector_describe_widget (app->inspector, selected, TRUE) : NULL;
	if (json == NULL)
	{
		g_string_append (md, "select a widget (F12, then click)\n");
	}
	else
	{
		g_auto(GStrv) lines = g_strsplit (json, "\n", -1);
		guint         i;

		/* The node's own fields come first; the property dump follows */
		for (i = 0; lines[i] != NULL && i < 6; i++)
			g_string_append_printf (md, "%s\n", lines[i]);
		g_string_append (md, "  ...\n");
	}
	g_string_append (md, "```\n");
	if (g_strcmp0 (lrg_markdown_label_get_markdown (app->inspect_json), md->str) != 0)
		lrg_markdown_label_set_markdown (app->inspect_json, md->str);
}

static void
update_inspector_timings (Showcase *app)
{
	LrgUIInspectorTimings t;
	g_autofree gchar     *text = NULL;

	lrg_ui_inspector_get_timings (app->inspector, &t);
	text = g_strdup_printf ("Frame: step %ld us, layout %ld us, record %ld us\n"
	                        "%u instances, %u commands, %u draw calls",
	                        (long) t.step_us, (long) t.layout_us, (long) t.record_us,
	                        t.n_instances, t.n_commands, t.draw_calls);
	lrg_label_set_text (app->inspect_timings, text);
}

static void
on_picked (LrgUIInspector *inspector,
           LrgWidget      *widget,
           gpointer        user_data)
{
	Showcase         *app = user_data;
	g_autofree gchar *path = widget != NULL ? lrg_ui_inspector_get_path (inspector, widget) : NULL;

	update_inspector_labels (app);
	show_toast (app, "Picked", path != NULL ? path : G_OBJECT_TYPE_NAME (widget), LRG_TOAST_KIND_INFO);
}

static void
toggle_pick_mode (Showcase *app)
{
	lrg_ui_inspector_set_pick_mode (app->inspector, !lrg_ui_inspector_get_pick_mode (app->inspector));
	update_inspector_labels (app);
}

static void
on_pick_clicked (LrgButton *button,
                 gpointer   user_data)
{
	toggle_pick_mode (user_data);
}

static void
on_select_target (LrgButton *button,
                  gpointer   user_data)
{
	Showcase *app = user_data;

	lrg_ui_inspector_set_selected (app->inspector, app->inspect_target);
	update_inspector_labels (app);
}

static void
on_clear_selection (LrgButton *button,
                    gpointer   user_data)
{
	Showcase *app = user_data;

	lrg_ui_inspector_set_selected (app->inspector, NULL);
	update_inspector_labels (app);
}

static LrgWidget *
build_inspector_page (Showcase *app)
{
	LrgVBox   *page = vcol (20.0f);
	LrgHBox   *row = hrow (20.0f);
	LrgVBox   *box;
	LrgVBox   *col;
	LrgHBox   *buttons;
	LrgButton *btn;
	LrgPanel  *panel;

	app->inspector = lrg_ui_inspector_get_default ();
	g_signal_connect (app->inspector, "picked", G_CALLBACK (on_picked), app);

	box = section (LRG_CONTAINER (page), "UI inspector");
	add (box, label_with_class ("lrg_ui_inspector_register_canvas () + lrg_ui_inspector_handle_input () in the frame loop.\n"
	                            "F12 toggles pick mode: hover outlines, a click selects (the app never sees it).\n"
	                            "The MCP tools lrg_ui_tree, lrg_ui_pick, lrg_ui_set_property ... see the same tree.",
	                            "muted"));
	buttons = hrow (8.0f);
	btn = lrg_button_new ("Pick mode (F12)");
	lrg_widget_add_class (LRG_WIDGET (btn), "primary");
	g_signal_connect (btn, "clicked", G_CALLBACK (on_pick_clicked), app);
	add (buttons, btn);
	btn = lrg_button_new ("Select the sample card");
	g_signal_connect (btn, "clicked", G_CALLBACK (on_select_target), app);
	add (buttons, btn);
	btn = lrg_button_new ("Clear");
	lrg_widget_add_class (LRG_WIDGET (btn), "flat");
	g_signal_connect (btn, "clicked", G_CALLBACK (on_clear_selection), app);
	add (buttons, btn);
	add (box, buttons);

	/* Live status */
	col = vcol (20.0f);
	lrg_widget_set_fill_weight (LRG_WIDGET (col), 1.0f);
	box = section (LRG_CONTAINER (col), "State");
	app->inspect_mode = label_with_class ("", NULL);
	g_object_ref (app->inspect_mode);
	add (box, app->inspect_mode);
	app->inspect_selected = label_with_class ("", "muted");
	g_object_ref (app->inspect_selected);
	add (box, app->inspect_selected);
	app->inspect_timings = label_with_class ("", "muted");
	g_object_ref (app->inspect_timings);
	add (box, app->inspect_timings);
	add (row, col);

	/* A named sample to inspect: its path reads
	 * ".../LrgPanel#sample-card/LrgVBox[0]/LrgButton#ok" */
	col = vcol (0.0f);
	lrg_widget_set_fill_weight (LRG_WIDGET (col), 1.0f);
	box = card (LRG_CONTAINER (col), "Sample card", &panel);
	lrg_widget_set_name (LRG_WIDGET (panel), "sample-card");
	lrg_panel_set_elevation (panel, 2);
	app->inspect_target = LRG_WIDGET (panel);
	add (box, label_with_class ("Named widgets get stable paths.", "muted"));
	add (box, lrg_checkbox_new_with_style ("Fullscreen", LRG_CHECKBOX_STYLE_SWITCH));
	buttons = hrow (8.0f);
	btn = lrg_button_new ("Cancel");
	lrg_widget_set_name (LRG_WIDGET (btn), "cancel");
	add (buttons, btn);
	btn = lrg_button_new ("OK");
	lrg_widget_set_name (LRG_WIDGET (btn), "ok");
	lrg_widget_add_class (LRG_WIDGET (btn), "primary");
	add (buttons, btn);
	add (box, buttons);
	add (row, col);
	add (page, row);

	/* The selection as the MCP tool lrg_ui_get_widget returns it */
	box = section (LRG_CONTAINER (page), "lrg_ui_inspector_describe_widget ()");
	app->inspect_json = lrg_markdown_label_new ("");
	g_object_ref (app->inspect_json);
	add (box, app->inspect_json);
	return LRG_WIDGET (page);
}

/* ==========================================================================
 * Navigation
 * ========================================================================== */

static void
show_page (Showcase *app,
           guint     index)
{
	guint i;

	for (i = 0; i < N_PAGES; i++)
		lrg_widget_set_visible (app->page[i], i == index);
	app->current_page = (gint) index;

	switch (index)
	{
	case PAGE_CARDS:
		/* Cards pop in again each time their page opens */
		for (i = 0; i < app->cards->len; i++)
			lrg_widget_play_appear (g_ptr_array_index (app->cards, i));
		break;

	case PAGE_MOTION:
		for (i = 0; i < app->appear_tiles->len; i++)
			lrg_widget_play_appear (g_ptr_array_index (app->appear_tiles, i));
		break;

	case PAGE_MARKDOWN:
		/* Replay the typewriter */
		lrg_markdown_label_reset_effects (app->markdown);
		break;

	case PAGE_INSPECTOR:
		/* Show the highlight right away on the sample card */
		if (lrg_ui_inspector_get_selected (app->inspector) == NULL)
			lrg_ui_inspector_set_selected (app->inspector, app->inspect_target);
		update_inspector_labels (app);
		break;

	default:
		break;
	}
}

static void
on_nav_selection (LrgListView *list,
                  gpointer     user_data)
{
	guint pos = lrg_list_view_get_selected (list);

	if (pos != LRG_LIST_VIEW_INVALID_POSITION)
		show_page (user_data, pos);
}

static void
build_ui (Showcase *app)
{
	LrgPanel            *sidebar = lrg_panel_new ();
	LrgVBox             *side_box = lrg_vbox_new ();
	LrgVBox             *main_box = lrg_vbox_new ();
	LrgHBox             *header = lrg_hbox_new ();
	LrgComboBox         *theme_combo = lrg_combo_box_new ();
	LrgComboBox         *motion_combo = lrg_combo_box_new ();
	g_autoptr(GListStore) nav_store = g_list_store_new (SHOWCASE_TYPE_ITEM);
	const gchar * const *presets = lrg_theme_list_presets ();
	guint                i;

	app->canvas = lrg_canvas_new ();
	lrg_canvas_set_paint_background (app->canvas, TRUE);
	app->cards = g_ptr_array_new_with_free_func (g_object_unref);
	app->appear_tiles = g_ptr_array_new_with_free_func (g_object_unref);

	app->root = lrg_hbox_new ();
	lrg_container_set_padding (LRG_CONTAINER (app->root), 20.0f);
	lrg_container_set_spacing (LRG_CONTAINER (app->root), 20.0f);

	/* Sidebar: title + page list */
	lrg_widget_set_min_size (LRG_WIDGET (sidebar), SIDEBAR_WIDTH, 0.0f);
	lrg_widget_set_max_size (LRG_WIDGET (sidebar), SIDEBAR_WIDTH, 0.0f);
	lrg_panel_set_elevation (sidebar, 2);
	lrg_panel_set_fill_children (sidebar, TRUE);
	lrg_container_set_padding (LRG_CONTAINER (sidebar), 16.0f);
	lrg_container_set_spacing (LRG_CONTAINER (side_box), 14.0f);
	add (side_box, label_with_class ("libregnum", "title"));
	add (side_box, label_with_class ("UI showcase", "muted"));

	for (i = 0; i < N_PAGES; i++)
	{
		g_autoptr(ShowcaseItem) item = g_object_new (SHOWCASE_TYPE_ITEM, NULL);

		item->label = g_strdup (page_names[i]);
		g_list_store_append (nav_store, item);
	}
	app->nav = lrg_list_view_new ();
	lrg_list_view_set_model (app->nav, G_LIST_MODEL (nav_store));
	lrg_widget_set_fill_weight (LRG_WIDGET (app->nav), 1.0f);
	g_signal_connect (app->nav, "selection-changed", G_CALLBACK (on_nav_selection), app);
	lrg_container_add_child (LRG_CONTAINER (side_box), LRG_WIDGET (app->nav));
	add (side_box, label_with_class ("F12: inspector pick mode", "muted"));
	lrg_container_add_child (LRG_CONTAINER (sidebar), LRG_WIDGET (side_box));
	g_object_unref (side_box);
	add (app->root, sidebar);

	/* Header: page title area + theme, motion and accent pickers */
	lrg_container_set_spacing (LRG_CONTAINER (header), 10.0f);
	{
		LrgLabel *title = label_with_class ("Make it flashy", "title");

		lrg_widget_set_fill_weight (LRG_WIDGET (title), 1.0f);
		add (header, title);
	}
	for (i = 0; i < G_N_ELEMENTS (accents); i++)
	{
		LrgButton *swatch = lrg_button_new ("");
		GrlColor   c;

		lrg_color_parse (accents[i], &c);
		lrg_button_set_normal_color (swatch, &c);
		lrg_button_set_corner_radius (swatch, 18.0f);
		lrg_widget_set_min_size (LRG_WIDGET (swatch), 36.0f, 36.0f);
		lrg_widget_set_max_size (LRG_WIDGET (swatch), 36.0f, 36.0f);
		lrg_widget_set_valign (LRG_WIDGET (swatch), LRG_ALIGN_CENTER);
		g_object_set_data (G_OBJECT (swatch), "accent", (gpointer) accents[i]);
		g_signal_connect (swatch, "clicked", G_CALLBACK (on_accent_clicked), app);
		add (header, swatch);
	}
	/* Show the theme that is actually active */
	for (i = 0; presets[i] != NULL; i++)
	{
		lrg_combo_box_append (theme_combo, presets[i]);
		if (g_strcmp0 (presets[i], lrg_theme_get_name (lrg_theme_get_default ())) == 0)
			lrg_combo_box_set_selected (theme_combo, (gint) i);
	}
	g_signal_connect (theme_combo, "changed", G_CALLBACK (on_theme_changed), app);
	add (header, theme_combo);
	for (i = 0; i < G_N_ELEMENTS (motions); i++)
		lrg_combo_box_append (motion_combo, motions[i].name);
	lrg_combo_box_set_selected (motion_combo, 0);
	g_signal_connect (motion_combo, "changed", G_CALLBACK (on_motion_changed), app);
	add (header, motion_combo);

	lrg_container_set_spacing (LRG_CONTAINER (main_box), 20.0f);
	lrg_widget_set_fill_weight (LRG_WIDGET (main_box), 1.0f);
	add (main_box, header);

	/* Pages stacked in an overlay, one visible at a time */
	app->pages = lrg_overlay_new ();
	lrg_widget_set_fill_weight (LRG_WIDGET (app->pages), 1.0f);
	app->page[PAGE_CONTROLS] = build_controls_page (app);
	app->page[PAGE_CARDS] = build_cards_page (app);
	app->page[PAGE_FEEDBACK] = build_feedback_page (app);
	app->page[PAGE_LIST] = build_list_page (app);
	app->page[PAGE_DATA] = build_data_page (app);
	app->page[PAGE_PICKERS] = build_pickers_page (app);
	app->page[PAGE_GLASS] = build_glass_page (app);
	app->page[PAGE_MOTION] = build_motion_page (app);
	app->page[PAGE_MARKDOWN] = build_markdown_page (app);
	app->page[PAGE_DECLARATIVE] = build_declarative_page (app);
	app->page[PAGE_INSPECTOR] = build_inspector_page (app);
	for (i = 0; i < N_PAGES; i++)
	{
		gboolean fill = (i == PAGE_LIST || i == PAGE_DATA || i == PAGE_GLASS ||
		                 i == PAGE_MARKDOWN || i == PAGE_DECLARATIVE);

		lrg_widget_set_appear (app->page[i], LRG_APPEAR_FLY_UP, 0.0f);
		lrg_widget_set_valign (app->page[i], fill ? LRG_ALIGN_FILL : LRG_ALIGN_START);
		add (app->pages, app->page[i]);
	}
	lrg_container_add_child (LRG_CONTAINER (main_box), LRG_WIDGET (app->pages));
	add (app->root, main_box);

	lrg_container_add_child (LRG_CONTAINER (app->canvas), LRG_WIDGET (app->root));

	/* Toasts live in the overlay layer: above everything, even a
	 * modal dialog's veil, bottom-right */
	app->toasts = lrg_toast_queue_new ();
	lrg_canvas_add_overlay (app->canvas, LRG_WIDGET (app->toasts));

	/* The breadcrumbs start at the tree's initial selection */
	on_tree_selection (app->tree, app);

	/* Make the UI inspectable (MCP tools, pick mode, highlight) */
	lrg_ui_inspector_register_canvas (app->canvas);

	lrg_list_view_select (app->nav, 0);
	show_page (app, 0);
}

/* ==========================================================================
 * Main
 * ========================================================================== */

int
main (int   argc,
      char *argv[])
{
	g_autoptr(GOptionContext) ctx = NULL;
	g_autoptr(GError)         error = NULL;
	g_autoptr(GrlWindow)      window = NULL;
	g_autofree gchar         *opt_theme = NULL;
	g_autofree gchar         *opt_motion = NULL;
	g_autofree gchar         *opt_page = NULL;
	g_autofree gchar         *opt_accent = NULL;
	g_autofree gchar         *opt_screenshot = NULL;
	gint                      opt_frames = 90;
	gboolean                  opt_license = FALSE;
	gboolean                  opt_toasts = FALSE;
	gboolean                  opt_dialog = FALSE;
	gboolean                  opt_island = FALSE;
	gboolean                  opt_pick = FALSE;
	gboolean                  opt_touch = FALSE;
	gboolean                  opt_dump = FALSE;
	Showcase                  app = { 0 };
	gint                      frame = 0;
	gboolean                  was_picking = FALSE;

	GOptionEntry entries[] = {
		{ "theme", 't', 0, G_OPTION_ARG_STRING, &opt_theme,
		  "Theme preset (midnight, daylight, catppuccin-mocha, catppuccin-latte, high-contrast)", "NAME" },
		{ "accent", 'a', 0, G_OPTION_ARG_STRING, &opt_accent, "Accent colour, e.g. '#22d3ee'", "HEX" },
		{ "motion", 'm', 0, G_OPTION_ARG_STRING, &opt_motion,
		  "Motion preset (smooth, snappy, bouncy, wobbly, none)", "NAME" },
		{ "page", 'p', 0, G_OPTION_ARG_STRING, &opt_page,
		  "Start page (controls, cards, feedback, list, data, pickers, glass, motion, markdown, "
		  "declarative, inspector)", "NAME" },
		{ "toasts", 0, 0, G_OPTION_ARG_NONE, &opt_toasts, "Show a few toasts at start-up", NULL },
		{ "dialog", 0, 0, G_OPTION_ARG_NONE, &opt_dialog, "Open the dialog at start-up", NULL },
		{ "island", 0, 0, G_OPTION_ARG_NONE, &opt_island, "Expand the island (motion page) at start-up", NULL },
		{ "pick", 0, 0, G_OPTION_ARG_NONE, &opt_pick, "Start in inspector pick mode", NULL },
		{ "touch", 0, 0, G_OPTION_ARG_NONE, &opt_touch, "Poll touch points (touch screens)", NULL },
		{ "dump-tree", 0, 0, G_OPTION_ARG_NONE, &opt_dump,
		  "With --screenshot: print the inspector's JSON snapshot of the tree too", NULL },
		{ "screenshot", 's', 0, G_OPTION_ARG_FILENAME, &opt_screenshot,
		  "Save a PNG after --frames frames and exit", "PATH" },
		{ "frames", 'f', 0, G_OPTION_ARG_INT, &opt_frames, "Frames to run before --screenshot (default 90)", "N" },
		{ "license", 0, 0, G_OPTION_ARG_NONE, &opt_license, "Show the license and exit", NULL },
		{ NULL }
	};

	ctx = g_option_context_new ("- tour the libregnum UI framework");
	g_option_context_add_main_entries (ctx, entries, NULL);
	g_option_context_set_description (ctx,
		"Examples:\n"
		"  ui-showcase\n"
		"  ui-showcase --theme catppuccin-latte --motion bouncy\n"
		"  ui-showcase --page cards --accent '#ff7a45'\n"
		"  ui-showcase --page glass --theme catppuccin-mocha\n"
		"  ui-showcase --page motion --island --motion bouncy\n"
		"  ui-showcase --page declarative     (then edit examples/data/ui/showcase-settings.yaml)\n"
		"  ui-showcase --page inspector --pick\n"
		"  ui-showcase --page feedback --toasts --dialog --screenshot dialog.png --frames 120\n"
		"\n"
		"Keys: Tab/arrows move focus, Enter activates, Escape cancels, F12 toggles inspector pick mode.\n");
	if (!g_option_context_parse (ctx, &argc, &argv, &error))
	{
		g_printerr ("ui-showcase: %s\n", error->message);
		return 2;
	}
	if (opt_license)
	{
		g_print ("%s", license_text);
		return 0;
	}

	SetConfigFlags (FLAG_WINDOW_RESIZABLE | FLAG_MSAA_4X_HINT | FLAG_WINDOW_HIGHDPI);
	window = grl_window_new (WINDOW_WIDTH, WINDOW_HEIGHT, "libregnum UI showcase");
	grl_window_set_target_fps (window, 60);
	if (opt_touch)
		lrg_canvas_input_set_poll_touch (TRUE);

	app.accent = g_strdup (opt_accent);
	apply_theme (&app, opt_theme != NULL ? opt_theme : "midnight");
	build_ui (&app);

	if (opt_motion != NULL)
	{
		guint i;

		for (i = 0; i < G_N_ELEMENTS (motions); i++)
			if (g_ascii_strcasecmp (opt_motion, motions[i].name) == 0)
				lrg_theme_set_motion (lrg_theme_get_default (), motions[i].motion);
	}
	if (opt_page != NULL)
	{
		guint i;
		gboolean found = FALSE;

		for (i = 0; i < N_PAGES; i++)
		{
			if (g_ascii_strcasecmp (opt_page, page_names[i]) == 0)
			{
				lrg_list_view_select (app.nav, i);
				found = TRUE;
			}
		}
		if (!found)
			g_printerr ("ui-showcase: unknown page '%s'\n", opt_page);
	}
	if (opt_toasts)
	{
		show_toast (&app, "Welcome back", "Last played 2 hours ago.", LRG_TOAST_KIND_INFO);
		show_toast (&app, "Achievement unlocked", "Pixel perfect: no damage taken.", LRG_TOAST_KIND_SUCCESS);
	}
	if (opt_pick)
		toggle_pick_mode (&app);

	while (!grl_window_should_close (window))
	{
		gfloat w = (gfloat) GetScreenWidth ();
		gfloat h = (gfloat) GetScreenHeight ();

		/* The canvas and the root box follow the window */
		lrg_widget_set_size (LRG_WIDGET (app.canvas), w, h);
		lrg_widget_set_size (LRG_WIDGET (app.root), w, h);
		lrg_widget_set_size (LRG_WIDGET (app.toasts), w, h);

		/* F12 toggles pick mode; input then goes through the inspector,
		 * which swallows clicks while picking and passes them otherwise */
		if (IsKeyPressed (KEY_F12))
			toggle_pick_mode (&app);
		lrg_ui_inspector_handle_input (app.inspector);
		if (lrg_ui_inspector_get_pick_mode (app.inspector) != was_picking)
		{
			/* Escape inside pick mode leaves it without telling us */
			was_picking = lrg_ui_inspector_get_pick_mode (app.inspector);
			update_inspector_labels (&app);
		}
		if (app.current_page == PAGE_INSPECTOR && frame % 15 == 0)
			update_inspector_timings (&app);

		/* Hot reload: a stat() of the watched YAML per frame */
		lrg_ui_loader_poll (app.loader);

		if (opt_dialog && frame == 10)
			lrg_dialog_present (app.dialog, app.canvas);
		if (opt_island && frame == 10)
			lrg_island_set_expanded (app.island, TRUE);

		grl_window_begin_drawing (window);
		lrg_canvas_render (app.canvas);

		if (opt_screenshot != NULL && frame >= opt_frames)
		{
			Image shot = LoadImageFromScreen ();

			ExportImage (shot, opt_screenshot);
			UnloadImage (shot);
			if (opt_dump)
			{
				g_autofree gchar *json = lrg_ui_inspector_snapshot_json (app.inspector, TRUE);

				g_print ("%s\n", json);
			}
			grl_window_end_drawing (window);
			break;
		}
		grl_window_end_drawing (window);
		frame++;
	}

	lrg_ui_inspector_register_canvas (NULL);
	g_clear_object (&app.loader);
	g_clear_object (&app.dialog);
	g_clear_object (&app.toast_mode);
	g_clear_object (&app.selection);
	g_clear_object (&app.nav);
	g_clear_object (&app.pages);
	g_clear_object (&app.root);
	g_clear_object (&app.toasts);
	g_clear_object (&app.canvas);
	g_clear_object (&app.pickers[0]);
	g_clear_object (&app.pickers[1]);
	g_clear_object (&app.crumbs[0]);
	g_clear_object (&app.crumbs[1]);
	g_clear_object (&app.tree);
	g_clear_object (&app.table);
	g_clear_object (&app.table_status);
	g_clear_object (&app.markdown);
	g_clear_object (&app.yaml_host);
	g_clear_object (&app.yaml_status);
	g_clear_object (&app.chips);
	g_clear_object (&app.inspect_mode);
	g_clear_object (&app.inspect_selected);
	g_clear_object (&app.inspect_timings);
	g_clear_object (&app.inspect_json);
	g_clear_object (&app.project);
	g_ptr_array_unref (app.cards);
	g_ptr_array_unref (app.appear_tiles);
	g_free (app.yaml_path);
	g_free (app.accent);
	return 0;
}
