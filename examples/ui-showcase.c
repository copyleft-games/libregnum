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
 * screen, the UI inspector, a menu bar driven by GIO actions and
 * shortcuts, card context menus, right-to-left layout, an anchor-layout
 * HUD, a switcher and a size box, a live 3D viewport and a custom
 * widget, a code editor with a console, a node graph, a playing
 * timeline, the five canvas looks, a block editor running its scripts,
 * a dock area, a file browser with the file dialogs, a header bar,
 * pagination, toast buttons and desktop notifications.
 *
 * Controls:
 *   Mouse                 everything (right-click a card for its menu)
 *   Tab / Shift+Tab       move focus (focus ring appears)
 *   Arrows / gamepad      spatial focus navigation, list cursor, sliders
 *   Enter / Space / A     activate the focused widget
 *   Escape / B            close popups and dialogs, leave pick mode
 *   F12                   inspector pick mode (hover highlights, click selects)
 *   Ctrl+Q                quit (app.quit)
 *   Ctrl+1 .. Ctrl+9      open pages (show.page)
 *   Ctrl+Shift+R          right-to-left layout (show.rtl)
 *   F1                    list the shortcuts
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
#define TABLE_PAGE     (250)    /* rows per page of the data page's pager */
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
	PAGE_LAYOUT,
	PAGE_VIEWPORT,
	PAGE_EDITOR,
	PAGE_GRAPH,
	PAGE_TIMELINE,
	PAGE_LOOKS,
	PAGE_BLOCKS,
	PAGE_DOCK,
	PAGE_FILES,
	PAGE_MARKDOWN,
	PAGE_DECLARATIVE,
	PAGE_INSPECTOR,
	N_PAGES
};

static const gchar *page_names[N_PAGES] = {
	"Controls", "Cards", "Feedback", "List", "Data", "Pickers",
	"Glass", "Motion", "Layout", "Viewport", "Editor", "Graph", "Timeline",
	"Looks", "Blocks", "Dock", "Files", "Markdown", "Declarative", "Inspector"
};

/* Looks page combos: LrgCanvasLook and LrgCanvasLinkShape, in enum order */
static const gchar *look_names[] = { "Auto (theme)", "Modern", "Blueprint", "Blender", "Neon" };
static const gchar *link_shape_names[] = { "Look's links", "Bezier", "Straight", "Step", "Angled" };
static const gchar *key_shape_names[] = { "Look's keys", "Diamond", "Circle", "Square", "Bar" };

/* Blocks page: "set color" choices and their colours */
static const struct { const gchar *name; const gchar *hex; } sprite_colors[] = {
	{ "violet", "#7c6cff" },
	{ "cyan",   "#22d3ee" },
	{ "mint",   "#34d399" },
	{ "amber",  "#fbbf24" },
	{ "coral",  "#ff7a45" }
};

/* A "repeat" block being run: the block and the iterations still to go */
typedef struct
{
	LrgBlock *loop;
	gint      left;
} BlockLoop;

#define SPRITE_TRAIL   (48)
#define BLOCK_STEP     (0.25)   /* seconds per block on the Blocks page */

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

	/* Menu bar, actions ("show.*") and shortcuts */
	LrgMenuBar         *menubar;
	GSimpleActionGroup *actions;
	LrgComboBox        *theme_combo;
	LrgComboBox        *motion_combo;
	gboolean            syncing_menu;  /* guards combo <-> action echo */
	gboolean            quit;          /* set by app.quit (quit-requested) */
	gchar              *shot_request;  /* File > Save screenshot, taken after the next render */

	/* Layout page */
	LrgSwitcher      *switcher;
	LrgComboBox      *transition_combo;
	LrgSizeBox       *size_box;
	LrgLabel         *size_label;

	/* Viewport page */
	LrgViewport      *viewport;
	GrlCamera3D      *camera;
	gfloat            yaw;
	gfloat            pitch;
	gfloat            distance;
	gfloat            scene_time;
	Model             cube;
	gboolean          cube_loaded;
	LrgLabel         *viewport_status;
	gfloat            spectrum_phase;

	/* Editor page */
	LrgTextEditor    *code;
	LrgComboBox      *language_combo;
	LrgTextEditor    *console_log;
	LrgTextEditor    *console_input;

	/* Graph, timeline and dock pages */
	LrgNodeGraph     *graph;
	LrgTimelineView  *timeline;
	LrgTimelineViewTrack *track_x;
	LrgTimelineViewTrack *track_y;
	LrgTimelineViewTrack *track_size;
	LrgTimelineViewTrack *track_hue;
	LrgDockArea      *dock;

	/* Looks page */
	LrgNodeGraph     *look_graph;
	LrgTimelineView  *look_timeline;
	LrgComboBox      *look_combo;

	/* Blocks page: the editor, its interpreter and the sprite it drives */
	LrgBlockEditor   *blocks;
	LrgLabel         *blocks_status;
	gboolean          running;
	LrgBlock         *pc;
	GArray           *loops;        /* BlockLoop */
	gdouble           run_delay;
	guint             run_steps;
	gfloat            sprite_x;     /* target position, stage units from the centre */
	gfloat            sprite_y;
	gfloat            sprite_heading;  /* degrees, 0 = right, 90 = up */
	gfloat            shown_x;      /* what is drawn: eases towards the target */
	gfloat            shown_y;
	gfloat            shown_heading;
	gchar            *sprite_color;
	gchar            *sprite_say;
	gfloat            say_left;     /* seconds the speech bubble stays */
	gfloat            trail[SPRITE_TRAIL * 2];
	guint             trail_len;

	/* Files page */
	LrgFileBrowser   *files;
	LrgLabel         *files_selection;
	LrgLabel         *files_result;

	/* Feedback and cards pages: notification, header bar */
	LrgHeaderBar     *header_bar;
	LrgLabel         *header_hint;
	gboolean          real_window;  /* --header-bar-window */
	gint              fake_x;       /* the fake window the header bar moves */
	gint              fake_y;
	gboolean          fake_maximized;
	guint             deleted_batches;
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

/* The header combo boxes go through the same actions as the menus, so
 * the radio marks follow them (the actions set the combos back, guarded
 * by syncing_menu) */
static void
on_theme_changed (LrgComboBox *combo,
                  gpointer     user_data)
{
	Showcase    *app = user_data;
	const gchar *name = lrg_combo_box_get_selected_text (combo);

	if (app->syncing_menu || name == NULL)
		return;
	app->syncing_menu = TRUE;
	g_action_group_activate_action (G_ACTION_GROUP (app->actions), "theme", g_variant_new_string (name));
	app->syncing_menu = FALSE;
}

static void
on_motion_changed (LrgComboBox *combo,
                   gpointer     user_data)
{
	Showcase    *app = user_data;
	const gchar *name = lrg_combo_box_get_selected_text (combo);

	if (app->syncing_menu || name == NULL)
		return;
	app->syncing_menu = TRUE;
	g_action_group_activate_action (G_ACTION_GROUP (app->actions), "motion", g_variant_new_string (name));
	app->syncing_menu = FALSE;
}

static void
on_accent_clicked (LrgButton *button,
                   gpointer   user_data)
{
	Showcase    *app = user_data;
	const gchar *hex = g_object_get_data (G_OBJECT (button), "accent");
	GrlColor     accent;

	if (lrg_color_parse (hex, &accent))
		g_action_group_activate_action (G_ACTION_GROUP (app->actions), "accent", g_variant_new_string (hex));
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

static const gchar *accent_names[] = { "Violet", "Cyan", "Mint", "Amber", "Coral", "Pink" };

/* append_item: a menu item with an optional icon and a display-only
 * accelerator (the real binding is a canvas shortcut; without an accel
 * attribute the menu would look the shortcut up itself) */
static void
append_item (GMenu       *menu,
             const gchar *label,
             const gchar *detailed_action,
             const gchar *icon,
             const gchar *accel)
{
	g_autoptr(GMenuItem) item = g_menu_item_new (label, detailed_action);

	if (icon != NULL)
		g_menu_item_set_attribute (item, "icon", "s", icon);
	if (accel != NULL)
		g_menu_item_set_attribute (item, "accel", "s", accel);
	g_menu_append_item (menu, item);
}

/* ==========================================================================
 * Card context menus: a "card" action group on each card, so the same
 * menu model acts on whichever card was right-clicked
 * ========================================================================== */

/* card_sync: title text and which elevation actions are enabled (the
 * menu greys out disabled items) */
static void
card_sync (LrgPanel *panel)
{
	GActionGroup     *group = lrg_widget_get_action_group (LRG_WIDGET (panel), "card");
	LrgLabel         *title = g_object_get_data (G_OBJECT (panel), "title");
	guint             elevation = lrg_panel_get_elevation (panel);
	g_autofree gchar *text = g_strdup_printf ("Elevation %u", elevation);

	lrg_label_set_text (title, text);
	g_simple_action_set_enabled (G_SIMPLE_ACTION (g_action_map_lookup_action (G_ACTION_MAP (group), "raise")),
	                             elevation < 3);
	g_simple_action_set_enabled (G_SIMPLE_ACTION (g_action_map_lookup_action (G_ACTION_MAP (group), "lower")),
	                             elevation > 0);
}

static void
on_card_raise (GSimpleAction *action,
               GVariant      *parameter,
               gpointer       user_data)
{
	LrgPanel *panel = user_data;

	lrg_panel_set_elevation (panel, MIN (lrg_panel_get_elevation (panel) + 1, 3));
	card_sync (panel);
}

static void
on_card_lower (GSimpleAction *action,
               GVariant      *parameter,
               gpointer       user_data)
{
	LrgPanel *panel = user_data;
	guint     elevation = lrg_panel_get_elevation (panel);

	lrg_panel_set_elevation (panel, elevation > 0 ? elevation - 1 : 0);
	card_sync (panel);
}

static void
on_card_replay (GSimpleAction *action,
                GVariant      *parameter,
                gpointer       user_data)
{
	lrg_widget_play_appear (LRG_WIDGET (user_data));
}

/* on_card_inspect: select the card in the inspector and go there */
static void
on_card_inspect (GSimpleAction *action,
                 GVariant      *parameter,
                 gpointer       user_data)
{
	Showcase *app = g_object_get_data (G_OBJECT (user_data), "showcase");

	app->inspect_target = LRG_WIDGET (user_data);
	lrg_ui_inspector_set_selected (app->inspector, LRG_WIDGET (user_data));
	g_action_group_activate_action (G_ACTION_GROUP (app->actions), "page", g_variant_new_string ("inspector"));
}

/* card_menu_model: card.* items, then the canvas-wide accent radio,
 * which resolves from the card up to the canvas's "show" group */
static GMenuModel *
card_menu_model (void)
{
	GMenu            *menu = g_menu_new ();
	g_autoptr(GMenu)  card_items = g_menu_new ();
	g_autoptr(GMenu)  accent = g_menu_new ();
	guint             i;

	append_item (card_items, "_Raise elevation", "card.raise", "chevron-up", NULL);
	append_item (card_items, "_Lower elevation", "card.lower", "chevron-down", NULL);
	append_item (card_items, "Re_play the pop", "card.replay", "refresh", NULL);
	append_item (card_items, "_Inspect", "card.inspect", "search", NULL);
	g_menu_append_section (menu, NULL, G_MENU_MODEL (card_items));
	for (i = 0; i < G_N_ELEMENTS (accents); i++)
	{
		g_autoptr(GMenuItem) item = g_menu_item_new (accent_names[i], NULL);

		g_menu_item_set_action_and_target_value (item, "show.accent", g_variant_new_string (accents[i]));
		g_menu_append_item (accent, item);
	}
	g_menu_append_section (menu, "Accent", G_MENU_MODEL (accent));
	return G_MENU_MODEL (menu);
}

/* attach_card_menu: the shared model plus this card's own group */
static void
attach_card_menu (Showcase   *app,
                  LrgPanel   *panel,
                  LrgLabel   *title,
                  GMenuModel *model)
{
	const GActionEntry entries[] = {
		{ "raise",   on_card_raise,   NULL, NULL, NULL, { 0 } },
		{ "lower",   on_card_lower,   NULL, NULL, NULL, { 0 } },
		{ "replay",  on_card_replay,  NULL, NULL, NULL, { 0 } },
		{ "inspect", on_card_inspect, NULL, NULL, NULL, { 0 } }
	};
	g_autoptr(GSimpleActionGroup) group = g_simple_action_group_new ();

	g_object_set_data (G_OBJECT (panel), "showcase", app);
	g_object_set_data (G_OBJECT (panel), "title", title);
	g_action_map_add_action_entries (G_ACTION_MAP (group), entries, G_N_ELEMENTS (entries), panel);
	lrg_widget_insert_action_group (LRG_WIDGET (panel), "card", G_ACTION_GROUP (group));
	lrg_widget_set_context_menu (LRG_WIDGET (panel), model);
	card_sync (panel);
}

/* ==========================================================================
 * Header bar (cards page): client-side window decoration. The window
 * buttons and dragging go through an LrgHeaderBarWindowFuncs table: the
 * default one drives the real raylib window (--header-bar-window); the
 * showcase otherwise installs this fake one, which only moves numbers
 * and raises toasts, so trying the buttons does not iconify the tour.
 * ========================================================================== */

static void
fake_window_caption (Showcase *app)
{
	g_autofree gchar *text = g_strdup_printf ("A fake window at %d, %d%s: drag the empty bar to move it, "
	                                          "double-click it to maximise (--header-bar-window: the real one).",
	                                          app->fake_x, app->fake_y,
	                                          app->fake_maximized ? ", maximised" : "");

	if (app->header_hint != NULL)
		lrg_label_set_text (app->header_hint, text);
}

static gboolean
fake_can_move (gpointer user_data)
{
	return TRUE;
}

static void
fake_get_position (gpointer user_data,
                   gint    *x,
                   gint    *y)
{
	Showcase *app = user_data;

	*x = app->fake_x;
	*y = app->fake_y;
}

static void
fake_set_position (gpointer user_data,
                   gint     x,
                   gint     y)
{
	Showcase *app = user_data;

	app->fake_x = x;
	app->fake_y = y;
	fake_window_caption (app);
}

static void
fake_minimize (gpointer user_data)
{
	show_toast (user_data, "Minimize", "A fake window: run with --header-bar-window to use the real one.",
	            LRG_TOAST_KIND_INFO);
}

static gboolean
fake_is_maximized (gpointer user_data)
{
	return ((Showcase *) user_data)->fake_maximized;
}

static void
fake_maximize (gpointer user_data)
{
	Showcase *app = user_data;

	app->fake_maximized = TRUE;
	fake_window_caption (app);
	show_toast (app, "Maximize", "The maximise glyph is now a restore glyph.", LRG_TOAST_KIND_INFO);
}

static void
fake_restore (gpointer user_data)
{
	Showcase *app = user_data;

	app->fake_maximized = FALSE;
	fake_window_caption (app);
}

static const LrgHeaderBarWindowFuncs fake_window_funcs = {
	fake_can_move, fake_get_position, fake_set_position,
	fake_minimize, fake_is_maximized, fake_maximize, fake_restore
};

/* on_close_requested: with the real window the close button goes on to
 * app.quit; with the fake one it only toasts */
static gboolean
on_close_requested (LrgHeaderBar *bar,
                    gpointer      user_data)
{
	Showcase *app = user_data;

	if (app->real_window)
		return FALSE;
	show_toast (app, "Close requested", "Returning TRUE from ::close-requested kept the app open.",
	            LRG_TOAST_KIND_WARNING);
	return TRUE;
}

static void
on_header_button (LrgButton *button,
                  gpointer   user_data)
{
	show_toast (user_data, lrg_button_get_text (button), "A header bar child: packed with pack_start / pack_end.",
	            LRG_TOAST_KIND_INFO);
}

/* build_header_bar: Back / New at the start, a breadcrumb as the title
 * widget, Share at the end, then the window buttons */
static LrgWidget *
build_header_bar (Showcase *app)
{
	LrgHeaderBar  *bar = lrg_header_bar_new ();
	LrgBreadcrumb *crumbs = lrg_breadcrumb_new ();
	LrgButton     *btn;

	app->header_bar = bar;
	lrg_header_bar_set_title (bar, "Cards");
	btn = lrg_button_new ("Back");
	lrg_widget_add_class (LRG_WIDGET (btn), "flat");
	g_signal_connect (btn, "clicked", G_CALLBACK (on_header_button), app);
	lrg_header_bar_pack_start (bar, LRG_WIDGET (btn));
	g_object_unref (btn);
	btn = lrg_button_new ("New");
	g_signal_connect (btn, "clicked", G_CALLBACK (on_header_button), app);
	lrg_header_bar_pack_start (bar, LRG_WIDGET (btn));
	g_object_unref (btn);
	btn = lrg_button_new ("Share");
	lrg_widget_add_class (LRG_WIDGET (btn), "primary");
	g_signal_connect (btn, "clicked", G_CALLBACK (on_header_button), app);
	lrg_header_bar_pack_end (bar, LRG_WIDGET (btn));
	g_object_unref (btn);

	/* A breadcrumb replaces the title text */
	lrg_breadcrumb_append (crumbs, "Showcase");
	lrg_breadcrumb_append (crumbs, "Cards");
	lrg_breadcrumb_append (crumbs, "Elevation");
	lrg_header_bar_set_title_widget (bar, LRG_WIDGET (crumbs));
	g_object_unref (crumbs);

	/* The window: the real one only on request */
	if (!app->real_window)
	{
		app->fake_x = 120;
		app->fake_y = 80;
		lrg_header_bar_set_window_funcs (bar, &fake_window_funcs, app, NULL);
		fake_window_caption (app);
	}
	g_signal_connect (bar, "close-requested", G_CALLBACK (on_close_requested), app);
	return LRG_WIDGET (bar);
}

static LrgWidget *
build_cards_page (Showcase *app)
{
	LrgVBox               *page = vcol (16.0f);
	LrgGrid               *grid = lrg_grid_new (3);
	g_autoptr(GMenuModel)  menu = card_menu_model ();
	guint                  i;

	/* A header bar over the cards, as a window would wear it */
	{
		LrgVBox  *box;
		LrgPanel *panel;
		LrgLabel *hint = label_with_class ("Drag the empty bar to move the window, double-click it to maximise.",
		                                   "muted");

		box = card (LRG_CONTAINER (page), NULL, &panel);
		lrg_container_set_padding (LRG_CONTAINER (panel), 0.0f);

		lrg_widget_set_margin_all (LRG_WIDGET (hint), 12.0f);
		app->header_hint = hint;
		add (box, build_header_bar (app));
		add (box, hint);
	}

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
		{
			LrgLabel *heading = label_with_class (title, "heading");

			lrg_container_add_child (LRG_CONTAINER (box), LRG_WIDGET (heading));
			attach_card_menu (app, panel, heading, menu);
			g_object_unref (heading);
		}
		add (box, label_with_class ("Gradient fill from a theme style\nrule; hover to glow, right-click\nfor its context menu.", NULL));
		lrg_container_add_child (LRG_CONTAINER (panel), LRG_WIDGET (box));
		g_object_unref (box);
		g_ptr_array_add (app->cards, panel);
		lrg_container_add_child (LRG_CONTAINER (grid), LRG_WIDGET (panel));
	}

	add (page, grid);
	return LRG_WIDGET (page);
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

/* on_delete_clicked: a toast with an Undo button. The button is a
 * detailed action ("show.undo-delete") activated on the toast, so the
 * "show" group on the canvas answers it like a menu item */
static void
on_delete_clicked (LrgButton *button,
                   gpointer   user_data)
{
	Showcase           *app = user_data;
	g_autoptr(LrgToast) toast = lrg_toast_new ("Deleted 3 items", "crate.png, barrel.png, torch.png");

	app->deleted_batches++;
	lrg_toast_add_button (toast, "Undo", "show.undo-delete");
	lrg_toast_set_duration (toast, 6.0);
	lrg_toast_queue_show (app->toasts, toast);
}

/* on_notify_clicked: a desktop notification (org.freedesktop.Notifications)
 * whose click and button open the Files page; without a notification
 * server, or with "Force in-app" on, it is a toast in the showcase's
 * queue with the same button */
static void
on_notify_clicked (LrgButton *button,
                   gpointer   user_data)
{
	Showcase                  *app = user_data;
	g_autoptr(LrgNotification) note = lrg_notification_new ("Download finished");
	g_autoptr(GError)          error = NULL;

	lrg_notification_set_body (note, "level-pack.zip (24 MB) is ready.");
	lrg_notification_set_icon_name (note, "folder-download");
	lrg_notification_set_default_action (note, "show.page::files");
	lrg_notification_add_button (note, "Show files", "show.page::files");
	lrg_notification_set_toast_queue (note, app->toasts);
	/* Actions resolve from the anchor up to the canvas */
	if (!lrg_notification_show (note, button != NULL ? LRG_WIDGET (button) : LRG_WIDGET (app->canvas), &error))
		show_toast (app, "Notification failed", error->message, LRG_TOAST_KIND_DANGER);
}

static void
on_force_in_app_toggled (LrgCheckbox *check,
                         gboolean     checked,
                         gpointer     user_data)
{
	lrg_notification_set_force_in_app (checked);
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

	box = section (LRG_CONTAINER (page), "Toast buttons and notifications");
	add (box, label_with_class ("Toast buttons run GIO actions (Undo is show.undo-delete). A notification goes "
	                            "to the desktop,\nor falls back to a toast here when there is no notification "
	                            "server.", "muted"));
	row = hrow (10.0f);
	{
		LrgButton   *btn = lrg_button_new ("Delete 3 items");
		LrgCheckbox *force;

		lrg_widget_add_class (LRG_WIDGET (btn), "danger");
		g_signal_connect (btn, "clicked", G_CALLBACK (on_delete_clicked), app);
		add (row, btn);
		btn = lrg_button_new ("Send notification");
		g_signal_connect (btn, "clicked", G_CALLBACK (on_notify_clicked), app);
		add (row, btn);
		force = lrg_checkbox_new_with_style ("Force in-app", LRG_CHECKBOX_STYLE_SWITCH);
		lrg_widget_set_valign (LRG_WIDGET (force), LRG_ALIGN_CENTER);
		lrg_checkbox_set_checked (force, lrg_notification_get_force_in_app ());
		g_signal_connect (force, "toggled", G_CALLBACK (on_force_in_app_toggled), app);
		add (row, force);
	}
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

	text = g_strdup_printf ("%u adventurers by %s (%s)",
	                        g_list_model_get_n_items (lrg_table_get_model (table)),
	                        column >= 0 ? lrg_table_get_column_title (table, (guint) column) : "nothing",
	                        ascending ? "ascending" : "descending");
	lrg_label_set_text (app->table_status, text);
}

/* on_table_page: the pager under the table jumps TABLE_PAGE rows at a
 * time. lrg_table_scroll_to() scrolls by the least amount that shows a
 * row, so showing the page's last row first and then its first row
 * leaves the first row at the top. */
static void
on_table_page (LrgPagination *pager,
               guint          page,
               gpointer       user_data)
{
	Showcase *app = user_data;
	guint     first = page * TABLE_PAGE;

	lrg_table_scroll_to (app->table, MIN (first + TABLE_PAGE - 1, N_TABLE_ROWS - 1), FALSE);
	lrg_table_scroll_to (app->table, first, FALSE);
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
	{
		/* The status line, with a pager over the 10,000 rows beside it */
		LrgHBox       *status_row = hrow (10.0f);
		LrgPagination *pager = lrg_pagination_new (N_TABLE_ROWS / TABLE_PAGE);

		lrg_widget_set_fill_weight (LRG_WIDGET (app->table_status), 1.0f);
		lrg_widget_set_valign (LRG_WIDGET (app->table_status), LRG_ALIGN_CENTER);
		lrg_container_add_child (LRG_CONTAINER (status_row), LRG_WIDGET (app->table_status));
		g_signal_connect (pager, "page-changed", G_CALLBACK (on_table_page), app);
		add (status_row, pager);
		add (right, status_row);
	}
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

	/* View > Inspector pick mode shows a check mark from this state */
	if (app->actions != NULL)
		g_simple_action_set_state (G_SIMPLE_ACTION (g_action_map_lookup_action (G_ACTION_MAP (app->actions), "pick")),
		                           g_variant_new_boolean (lrg_ui_inspector_get_pick_mode (app->inspector)));
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

/* ==========================================================================
 * Layout page: anchor layout HUD, switcher, size box
 * ========================================================================== */

/* paint_radar: a minimap for the HUD, a radar sweep that turns with the
 * list time (a pure function of it, so captures are reproducible) */
static void
paint_radar (LrgCustomWidget *widget,
             LrgPaintList    *list,
             gfloat           x,
             gfloat           y,
             gfloat           width,
             gfloat           height,
             gpointer         user_data)
{
	LrgTheme *theme = lrg_widget_get_theme (LRG_WIDGET (widget));
	gfloat    r = MIN (width, height) * 0.5f;
	gfloat    cx = x + width * 0.5f;
	gfloat    cy = y + height * 0.5f;
	gdouble   t = lrg_paint_list_get_time (list);
	gfloat    a = (gfloat) fmod (t * 1.6, G_PI * 2.0);
	GrlColor  ring;
	GrlColor  dim;
	GrlColor  sweep;
	GrlColor  blip;
	guint     i;

	color_hex ("#04121f", 0.72f, &dim);
	color_hex ("#22d3ee", 0.85f, &ring);
	lrg_paint_list_shadow (list, x, y, width, height, r, 18.0f, 0.0f, 0.0f, 4.0f,
	                       lrg_theme_get_color (theme, LRG_THEME_COLOR_SHADOW));
	lrg_paint_list_circle (list, cx, cy, r, &dim);
	lrg_paint_list_border (list, x, y, width, height, r, 2.0f, &ring);
	lrg_paint_list_border (list, cx - r * 0.55f, cy - r * 0.55f, r * 1.1f, r * 1.1f, r * 0.55f, 1.0f, &ring);

	/* The sweep: a fan of fading lines trailing the beam */
	for (i = 0; i < 14; i++)
	{
		gfloat aa = a - (gfloat) i * 0.05f;

		color_hex ("#22d3ee", 0.9f * (1.0f - (gfloat) i / 14.0f), &sweep);
		lrg_paint_list_line (list, cx, cy, cx + cosf (aa) * r * 0.95f, cy + sinf (aa) * r * 0.95f,
		                     2.0f, &sweep);
	}

	/* Two contacts, the player in the middle */
	color_hex ("#f43f5e", 1.0f, &blip);
	lrg_paint_list_circle (list, cx + r * 0.42f, cy - r * 0.30f, 4.0f, &blip);
	color_hex ("#fbbf24", 1.0f, &blip);
	lrg_paint_list_circle (list, cx - r * 0.25f, cy + r * 0.50f, 4.0f, &blip);
	lrg_paint_list_circle (list, cx, cy, 3.0f, &ring);
}

/* paint_crosshair: four ticks and a dot that breathe with the list time */
static void
paint_crosshair (LrgCustomWidget *widget,
                 LrgPaintList    *list,
                 gfloat           x,
                 gfloat           y,
                 gfloat           width,
                 gfloat           height,
                 gpointer         user_data)
{
	gfloat   cx = x + width * 0.5f;
	gfloat   cy = y + height * 0.5f;
	gfloat   gap = 7.0f + 2.0f * (gfloat) sin (lrg_paint_list_get_time (list) * 3.0);
	GrlColor c;

	color_hex ("#ffffff", 0.92f, &c);
	lrg_paint_list_line (list, cx - gap - 9.0f, cy, cx - gap, cy, 2.0f, &c);
	lrg_paint_list_line (list, cx + gap, cy, cx + gap + 9.0f, cy, 2.0f, &c);
	lrg_paint_list_line (list, cx, cy - gap - 9.0f, cx, cy - gap, 2.0f, &c);
	lrg_paint_list_line (list, cx, cy + gap, cx, cy + gap + 9.0f, 2.0f, &c);
	lrg_paint_list_circle (list, cx, cy, 2.0f, &c);
}

/* custom_painter: a fixed-size LrgCustomWidget with a paint callback */
static LrgCustomWidget *
custom_painter (gfloat width,
                gfloat height,
                void (*paint) (LrgCustomWidget *, LrgPaintList *, gfloat, gfloat, gfloat, gfloat, gpointer),
                gpointer user_data)
{
	LrgCustomWidget      *w = lrg_custom_widget_new ();
	LrgCustomWidgetFuncs  funcs = { 0 };

	funcs.paint = paint;
	lrg_custom_widget_set_funcs (w, &funcs, user_data, NULL);
	lrg_custom_widget_set_preferred_width (w, width);
	lrg_custom_widget_set_preferred_height (w, height);
	return w;
}

/* hud_chip: a frosted glass pill holding a row of widgets */
static LrgHBox *
hud_chip (LrgPanel **panel_out)
{
	LrgPanel *panel = lrg_panel_new ();
	LrgHBox  *row = hrow (10.0f);

	lrg_widget_add_class (LRG_WIDGET (panel), "glass-frosted");
	lrg_panel_set_elevation (panel, 2);
	lrg_panel_set_fill_children (panel, TRUE);
	lrg_container_set_padding (LRG_CONTAINER (panel), 12.0f);
	lrg_container_add_child (LRG_CONTAINER (panel), LRG_WIDGET (row));
	g_object_unref (row);
	*panel_out = panel;
	return row;
}

static const struct { const gchar *name; LrgSwitcherTransition transition; } transitions[] = {
	{ "Crossfade",   LRG_SWITCHER_TRANSITION_CROSSFADE },
	{ "Slide",       LRG_SWITCHER_TRANSITION_SLIDE },
	{ "Slide up",    LRG_SWITCHER_TRANSITION_SLIDE_UP },
	{ "Slide down",  LRG_SWITCHER_TRANSITION_SLIDE_DOWN },
	{ "Zoom",        LRG_SWITCHER_TRANSITION_ZOOM },
	{ "None",        LRG_SWITCHER_TRANSITION_NONE }
};

static void
on_transition_changed (LrgComboBox *combo,
                       gpointer     user_data)
{
	Showcase *app = user_data;
	gint      i = lrg_combo_box_get_selected (combo);

	if (i >= 0 && i < (gint) G_N_ELEMENTS (transitions))
		lrg_switcher_set_transition (app->switcher, transitions[i].transition);
}

/* on_switch_step: Previous / Next buttons, wrapping around */
static void
on_switch_step (LrgButton *button,
                gpointer   user_data)
{
	Showcase *app = user_data;
	gint      step = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (button), "step"));
	gint      n = (gint) lrg_container_get_child_count (LRG_CONTAINER (app->switcher));
	gint      i = lrg_switcher_get_visible_index (app->switcher);

	if (n > 0)
		lrg_switcher_set_visible_index (app->switcher, ((i + step) % n + n) % n);
}

static LrgWidget *
build_layout_page (Showcase *app)
{
	static const struct { const gchar *title; const gchar *text; } slides[] = {
		{ "Play",     "Continue where you left off:\nchapter 3, the flooded vault." },
		{ "Options",  "Video, audio and controls.\nPages keep their own state." },
		{ "Credits",  "Made with libregnum.\nTransitions run on springs." }
	};
	LrgVBox          *page = vcol (20.0f);
	LrgHBox          *row = hrow (20.0f);
	LrgOverlay       *stage = lrg_overlay_new ();
	LrgAnchorLayout  *hud = lrg_anchor_layout_new ();
	LrgPanel         *panel;
	LrgHBox          *chip;
	LrgVBox          *box;
	LrgHBox          *controls;
	LrgButton        *btn;
	LrgProgressBar   *hp;
	guint             i;

	/* An anchor layout over the glass page's backdrop: each HUD element
	 * pinned to an edge or corner, so resizing the window keeps them put */
	box = card (LRG_CONTAINER (page), "LrgAnchorLayout: a game HUD pinned to the corners", NULL);
	lrg_widget_set_min_size (LRG_WIDGET (stage), 0.0f, 280.0f);
	add (stage, g_object_new (SHOWCASE_TYPE_BACKDROP, NULL));

	chip = hud_chip (&panel);
	add (chip, showcase_icon_new (LRG_ICON_HEART, 18.0f, LRG_THEME_COLOR_DANGER));
	hp = lrg_progress_bar_new ();
	lrg_progress_bar_set_value (hp, 82.0);
	lrg_widget_set_min_size (LRG_WIDGET (hp), 150.0f, 0.0f);
	lrg_widget_set_valign (LRG_WIDGET (hp), LRG_ALIGN_CENTER);
	add (chip, hp);
	add (chip, label_with_class ("82", NULL));
	lrg_anchor_layout_add (hud, LRG_WIDGET (panel), LRG_ANCHOR_PRESET_TOP_LEFT, 16.0f, 16.0f);
	g_object_unref (panel);

	lrg_anchor_layout_add (hud, LRG_WIDGET (custom_painter (96.0f, 96.0f, paint_radar, NULL)),
	                       LRG_ANCHOR_PRESET_TOP_RIGHT, 16.0f, 16.0f);
	lrg_anchor_layout_add (hud, LRG_WIDGET (custom_painter (48.0f, 48.0f, paint_crosshair, NULL)),
	                       LRG_ANCHOR_PRESET_CENTER, 0.0f, 0.0f);

	chip = hud_chip (&panel);
	add (chip, showcase_icon_new (LRG_ICON_STAR, 16.0f, LRG_THEME_COLOR_WARNING));
	add (chip, label_with_class ("Objective: reach the flooded vault", NULL));
	lrg_anchor_layout_add (hud, LRG_WIDGET (panel), LRG_ANCHOR_PRESET_TOP, 0.0f, 16.0f);
	g_object_unref (panel);

	chip = hud_chip (&panel);
	add (chip, label_with_class ("E", "heading"));
	add (chip, label_with_class ("Open the vault door", NULL));
	lrg_anchor_layout_add (hud, LRG_WIDGET (panel), LRG_ANCHOR_PRESET_BOTTOM, 0.0f, 16.0f);
	g_object_unref (panel);

	chip = hud_chip (&panel);
	add (chip, label_with_class ("24", "title"));
	add (chip, label_with_class ("/ 120", "muted"));
	lrg_anchor_layout_add (hud, LRG_WIDGET (panel), LRG_ANCHOR_PRESET_BOTTOM_RIGHT, 16.0f, 16.0f);
	g_object_unref (panel);

	add (stage, hud);
	add (box, stage);

	/* A switcher with a transition picker */
	box = card (LRG_CONTAINER (row), "LrgSwitcher: one page at a time", &panel);
	lrg_widget_set_fill_weight (LRG_WIDGET (panel), 1.0f);
	controls = hrow (10.0f);
	app->transition_combo = lrg_combo_box_new ();
	for (i = 0; i < G_N_ELEMENTS (transitions); i++)
		lrg_combo_box_append (app->transition_combo, transitions[i].name);
	lrg_combo_box_set_selected (app->transition_combo, 1);
	lrg_widget_set_fill_weight (LRG_WIDGET (app->transition_combo), 1.0f);
	g_signal_connect (app->transition_combo, "changed", G_CALLBACK (on_transition_changed), app);
	lrg_container_add_child (LRG_CONTAINER (controls), LRG_WIDGET (app->transition_combo));
	btn = lrg_button_new ("Previous");
	g_object_set_data (G_OBJECT (btn), "step", GINT_TO_POINTER (-1));
	g_signal_connect (btn, "clicked", G_CALLBACK (on_switch_step), app);
	add (controls, btn);
	btn = lrg_button_new ("Next");
	lrg_widget_add_class (LRG_WIDGET (btn), "primary");
	g_object_set_data (G_OBJECT (btn), "step", GINT_TO_POINTER (1));
	g_signal_connect (btn, "clicked", G_CALLBACK (on_switch_step), app);
	add (controls, btn);
	add (box, controls);

	app->switcher = lrg_switcher_new ();
	lrg_switcher_set_transition (app->switcher, LRG_SWITCHER_TRANSITION_SLIDE);
	lrg_widget_set_min_size (LRG_WIDGET (app->switcher), 0.0f, 150.0f);
	for (i = 0; i < G_N_ELEMENTS (slides); i++)
	{
		g_autofree gchar *klass = g_strdup_printf ("card-%u", (i * 2) % 6);
		LrgPanel         *slide = lrg_panel_new ();
		LrgVBox          *inner = vcol (8.0f);

		lrg_widget_add_class (LRG_WIDGET (slide), klass);
		lrg_panel_set_fill_children (slide, TRUE);
		lrg_container_set_padding (LRG_CONTAINER (slide), 20.0f);
		add (inner, label_with_class (slides[i].title, "title"));
		add (inner, label_with_class (slides[i].text, NULL));
		add (slide, inner);
		lrg_switcher_add_named (app->switcher, LRG_WIDGET (slide), slides[i].title);
		g_object_unref (slide);
	}
	lrg_container_add_child (LRG_CONTAINER (box), LRG_WIDGET (app->switcher));

	/* A size box holding its child at 16:9 inside whatever it gets */
	box = card (LRG_CONTAINER (row), "LrgSizeBox: 16:9 at any size", &panel);
	lrg_widget_set_fill_weight (LRG_WIDGET (panel), 1.0f);
	app->size_label = label_with_class ("", "muted");
	lrg_container_add_child (LRG_CONTAINER (box), LRG_WIDGET (app->size_label));
	app->size_box = lrg_size_box_new ();
	lrg_size_box_set_aspect_ratio (app->size_box, 16.0f / 9.0f);
	lrg_widget_set_min_size (LRG_WIDGET (app->size_box), 0.0f, 196.0f);
	{
		LrgPanel *frame = lrg_panel_new ();
		LrgLabel *ratio = label_with_class ("16 : 9", "title");

		lrg_widget_add_class (LRG_WIDGET (frame), "card-5");
		lrg_panel_set_elevation (frame, 2);
		lrg_panel_set_fill_children (frame, TRUE);
		lrg_widget_set_halign (LRG_WIDGET (ratio), LRG_ALIGN_CENTER);
		lrg_widget_set_valign (LRG_WIDGET (ratio), LRG_ALIGN_CENTER);
		add (frame, ratio);
		lrg_size_box_set_child (app->size_box, LRG_WIDGET (frame));
		g_object_unref (frame);
	}
	lrg_container_add_child (LRG_CONTAINER (box), LRG_WIDGET (app->size_box));

	add (page, row);
	return LRG_WIDGET (page);
}

/* update_size_label: report the size box child's allocation */
static void
update_size_label (Showcase *app)
{
	LrgWidget        *child = lrg_size_box_get_child (app->size_box);
	g_autofree gchar *text = NULL;

	if (child == NULL)
		return;
	text = g_strdup_printf ("Child %.0f x %.0f in a %.0f x %.0f box",
	                        lrg_widget_get_width (child), lrg_widget_get_height (child),
	                        lrg_widget_get_width (LRG_WIDGET (app->size_box)),
	                        lrg_widget_get_height (LRG_WIDGET (app->size_box)));
	lrg_label_set_text (app->size_label, text);
}

/* ==========================================================================
 * Viewport page: a live 3D scene through LrgViewport and GrlCamera3D,
 * and an animated LrgCustomWidget
 * ========================================================================== */

/* load_shaded_cube: a unit cube mesh with per-face grey vertex colours,
 * so a flat tint still reads as a lit solid (raylib's default shader
 * multiplies the vertex colour by the tint) */
static Model
load_shaded_cube (void)
{
	Mesh   src = GenMeshCube (1.0f, 1.0f, 1.0f);
	Mesh   mesh = { 0 };
	gint   i;

	mesh.vertexCount = src.vertexCount;
	mesh.triangleCount = src.triangleCount;
	mesh.vertices = MemAlloc ((guint) src.vertexCount * 3 * sizeof (gfloat));
	mesh.normals = MemAlloc ((guint) src.vertexCount * 3 * sizeof (gfloat));
	mesh.texcoords = MemAlloc ((guint) src.vertexCount * 2 * sizeof (gfloat));
	mesh.colors = MemAlloc ((guint) src.vertexCount * 4);
	mesh.indices = MemAlloc ((guint) src.triangleCount * 3 * sizeof (gushort));
	memcpy (mesh.vertices, src.vertices, (gsize) src.vertexCount * 3 * sizeof (gfloat));
	memcpy (mesh.normals, src.normals, (gsize) src.vertexCount * 3 * sizeof (gfloat));
	memcpy (mesh.texcoords, src.texcoords, (gsize) src.vertexCount * 2 * sizeof (gfloat));
	memcpy (mesh.indices, src.indices, (gsize) src.triangleCount * 3 * sizeof (gushort));

	/* Light from above-front-right: brightness = ambient + diffuse */
	for (i = 0; i < src.vertexCount; i++)
	{
		gfloat nx = src.normals[i * 3 + 0];
		gfloat ny = src.normals[i * 3 + 1];
		gfloat nz = src.normals[i * 3 + 2];
		gfloat d = MAX (0.0f, nx * 0.40f + ny * 0.80f + nz * 0.45f);
		guint8 v = (guint8) CLAMP (95.0f + 160.0f * d, 0.0f, 255.0f);

		mesh.colors[i * 4 + 0] = v;
		mesh.colors[i * 4 + 1] = v;
		mesh.colors[i * 4 + 2] = v;
		mesh.colors[i * 4 + 3] = 255;
	}
	UnloadMesh (src);
	UploadMesh (&mesh, false);
	return LoadModelFromMesh (mesh);
}

/* orbit_apply: place the camera on a sphere around the scene centre */
static void
orbit_apply (Showcase *app)
{
	gfloat cp = cosf (app->pitch);

	grl_camera3d_set_position_xyz (app->camera,
	                               app->distance * cp * sinf (app->yaw),
	                               0.6f + app->distance * sinf (app->pitch),
	                               app->distance * cp * cosf (app->yaw));
}

/* draw_scene: the viewport's render function. Texture mode is active on
 * the viewport's own render texture; this runs from the canvas step,
 * outside the UI paint pass, so 3D mode is free to use */
static void
draw_scene (LrgViewport      *viewport,
            GrlRenderTexture *target,
            gfloat            width,
            gfloat            height,
            gpointer          user_data)
{
	static const gchar *hues[] = { "#7c6cff", "#22d3ee", "#34d399", "#fbbf24", "#ff7a45", "#f472b6" };
	Showcase *app = user_data;
	gfloat    t = app->scene_time;
	guint     i;

	grl_camera3d_begin (app->camera);
	DrawGrid (16, 0.5f);

	/* A ring of cubes bobbing and spinning around a large centre cube */
	for (i = 0; i < G_N_ELEMENTS (hues); i++)
	{
		gfloat   a = t * 0.5f + (gfloat) i * (gfloat) G_PI / 3.0f;
		Vector3  pos = { cosf (a) * 2.6f, 0.45f + 0.25f * sinf (t * 2.0f + (gfloat) i), sinf (a) * 2.6f };
		Vector3  axis = { 0.3f, 1.0f, 0.2f };
		Vector3  scale = { 0.6f, 0.6f, 0.6f };
		GrlColor c;
		Color    tint;

		lrg_color_parse (hues[i], &c);
		tint.r = c.r;
		tint.g = c.g;
		tint.b = c.b;
		tint.a = 255;
		if (app->cube_loaded)
		{
			DrawModelEx (app->cube, pos, axis, t * 90.0f + (gfloat) i * 40.0f, scale, tint);
		}
	}
	if (app->cube_loaded)
	{
		Vector3 pos = { 0.0f, 0.8f, 0.0f };
		Vector3 axis = { 0.0f, 1.0f, 0.0f };
		Vector3 scale = { 1.4f, 1.4f, 1.4f };

		DrawModelEx (app->cube, pos, axis, t * 30.0f, scale, (Color){ 180, 170, 255, 255 });
	}
	DrawSphere ((Vector3){ cosf (t) * 1.2f, 2.2f + 0.2f * sinf (t * 3.0f), sinf (t) * 1.2f },
	            0.18f, (Color){ 251, 191, 36, 255 });
	grl_camera3d_end (app->camera);
}

/* on_viewport_move: left-drag orbits; the status line shows the pointer
 * in texture coordinates */
static void
on_viewport_move (LrgViewport *viewport,
                  gfloat       u,
                  gfloat       v,
                  gfloat       dx,
                  gfloat       dy,
                  gpointer     user_data)
{
	Showcase         *app = user_data;
	g_autofree gchar *text = NULL;

	if (lrg_viewport_get_dragging (viewport))
	{
		app->yaw -= dx * 0.01f;
		app->pitch = CLAMP (app->pitch + dy * 0.01f, -0.2f, 1.4f);
		orbit_apply (app);
	}
	text = g_strdup_printf ("Pointer at texel %.0f, %.0f  -  yaw %.2f  pitch %.2f  distance %.1f",
	                        u, v, app->yaw, app->pitch, app->distance);
	lrg_label_set_text (app->viewport_status, text);
}

static void
on_viewport_scroll (LrgViewport *viewport,
                    gfloat       u,
                    gfloat       v,
                    gfloat       scroll_x,
                    gfloat       scroll_y,
                    gpointer     user_data)
{
	Showcase *app = user_data;

	app->distance = CLAMP (app->distance * (1.0f - scroll_y * 0.1f), 3.0f, 20.0f);
	orbit_apply (app);
}

static void
on_resolution_changed (LrgSlider *slider,
                       gpointer   user_data)
{
	Showcase *app = user_data;

	lrg_viewport_set_resolution_scale (app->viewport, (gfloat) lrg_slider_get_value (slider));
}

/* spectrum_step: the custom widget's own clock (the canvas calls it
 * every frame with the frame time); TRUE keeps the canvas animating */
static gboolean
spectrum_step (LrgCustomWidget *widget,
               gfloat           delta_time,
               gpointer         user_data)
{
	Showcase *app = user_data;

	app->spectrum_phase += delta_time;
	return TRUE;
}

/* spectrum_paint: a fake audio spectrum, rounded bars with an accent
 * gradient and a glowing peak cap, purely from spectrum_phase */
static void
spectrum_paint (LrgCustomWidget *widget,
                LrgPaintList    *list,
                gfloat           x,
                gfloat           y,
                gfloat           width,
                gfloat           height,
                gpointer         user_data)
{
	Showcase       *app = user_data;
	LrgTheme       *theme = lrg_widget_get_theme (LRG_WIDGET (widget));
	const GrlColor *accent = lrg_theme_get_color (theme, LRG_THEME_COLOR_ACCENT);
	const GrlColor *track = lrg_theme_get_color (theme, LRG_THEME_COLOR_TRACK);
	GrlColor        top;
	GrlColor        cap;
	LrgCornerRadii  radii;
	guint           n = 24;
	gfloat          gap = 4.0f;
	gfloat          bw = (width - gap * (gfloat) (n - 1)) / (gfloat) n;
	gfloat          p = app->spectrum_phase;
	guint           i;

	color_hex ("#f472b6", 1.0f, &top);
	lrg_corner_radii_init_uniform (&radii, bw * 0.5f);
	for (i = 0; i < n; i++)
	{
		gfloat           f = (gfloat) i / (gfloat) n;
		gfloat           level = 0.18f + 0.38f * (0.5f + 0.5f * sinf (p * (2.0f + f * 3.0f) + f * 9.0f))
		                         + 0.30f * (0.5f + 0.5f * sinf (p * 5.3f + f * 21.0f)) * (1.0f - f);
		gfloat           bh = MIN (1.0f, level) * height;
		gfloat           bx = x + (gfloat) i * (bw + gap);
		LrgPaintGradient g;

		lrg_paint_list_rect (list, bx, y, bw, height, bw * 0.5f, track);
		lrg_paint_gradient_init_linear (&g, accent, &top, 90.0f);
		lrg_paint_list_rect_ex (list, bx, y + height - bh, bw, bh, &radii, NULL, &g, 0.0f, NULL);
		lrg_paint_color_with_alpha (&top, 0.9f, &cap);
		lrg_paint_list_shadow (list, bx, y + height - bh, bw, 4.0f, 2.0f, 10.0f, 0.0f, 0.0f, 0.0f, &cap);
	}
}

static LrgWidget *
build_viewport_page (Showcase *app)
{
	LrgHBox              *page = hrow (20.0f);
	LrgVBox              *side = vcol (20.0f);
	LrgVBox              *box;
	LrgPanel             *panel;
	LrgSlider            *res;
	LrgCustomWidget      *spectrum;
	LrgCustomWidgetFuncs  funcs = { 0 };
	GrlColor              clear;

	/* The camera orbits the scene centre; the cube model needs a GL
	 * context, which exists because the window opens before build_ui */
	app->camera = grl_camera3d_new ();
	grl_camera3d_set_target_xyz (app->camera, 0.0f, 0.6f, 0.0f);
	grl_camera3d_set_fovy (app->camera, 45.0f);
	app->yaw = 0.7f;
	app->pitch = 0.45f;
	app->distance = 8.0f;
	orbit_apply (app);
	if (IsWindowReady ())
	{
		app->cube = load_shaded_cube ();
		app->cube_loaded = TRUE;
	}

	box = card (LRG_CONTAINER (page), "LrgViewport: a live 3D scene", &panel);
	lrg_widget_set_fill_weight (LRG_WIDGET (panel), 2.0f);
	app->viewport = lrg_viewport_new ();
	color_hex ("#0b1026", 1.0f, &clear);
	lrg_viewport_set_clear_color (app->viewport, &clear);
	lrg_viewport_set_render_func (app->viewport, draw_scene, app, NULL);
	lrg_widget_set_fill_weight (LRG_WIDGET (app->viewport), 1.0f);
	g_signal_connect (app->viewport, "pointer-move", G_CALLBACK (on_viewport_move), app);
	g_signal_connect (app->viewport, "pointer-scroll", G_CALLBACK (on_viewport_scroll), app);
	lrg_container_add_child (LRG_CONTAINER (box), LRG_WIDGET (app->viewport));
	app->viewport_status = label_with_class ("Drag to orbit, wheel to zoom", "muted");
	lrg_container_add_child (LRG_CONTAINER (box), LRG_WIDGET (app->viewport_status));

	/* Render resolution: the texture follows the widget times this */
	box = card (LRG_CONTAINER (side), "Resolution scale", NULL);
	add (box, label_with_class ("0.25 renders 1/16 of the pixels\nand upscales; 2 supersamples.", "muted"));
	res = lrg_slider_new_with_range (0.25, 2.0, 0.25);
	lrg_slider_set_value (res, 1.0);
	g_signal_connect (res, "value-changed", G_CALLBACK (on_resolution_changed), app);
	add (box, res);

	/* An LrgCustomWidget: paint + step callbacks, no subclass */
	box = card (LRG_CONTAINER (side), "LrgCustomWidget", &panel);
	lrg_widget_set_fill_weight (LRG_WIDGET (panel), 1.0f);
	spectrum = lrg_custom_widget_new ();
	funcs.paint = spectrum_paint;
	funcs.step = spectrum_step;
	lrg_custom_widget_set_funcs (spectrum, &funcs, app, NULL);
	lrg_custom_widget_set_preferred_height (spectrum, 140.0f);
	lrg_widget_set_fill_weight (LRG_WIDGET (spectrum), 1.0f);
	add (box, spectrum);
	add (box, label_with_class ("24 bars from one paint callback;\nthe phase advances in step().", "muted"));

	lrg_widget_set_fill_weight (LRG_WIDGET (side), 1.0f);
	add (page, side);
	return LRG_WIDGET (page);
}

/* ==========================================================================
 * Editor page: LrgTextEditor with syntax highlighting, and a console
 * ========================================================================== */

static const struct { const gchar *language; const gchar *text; } code_samples[] = {
	{ "c",
	  "/* player.c - a GObject with a spring-driven health bar */\n"
	  "#include <libregnum.h>\n"
	  "\n"
	  "#define MAX_HEALTH (100)\n"
	  "\n"
	  "struct _GamePlayer\n"
	  "{\n"
	  "\tGObject    parent_instance;\n"
	  "\tgint       health;\n"
	  "\tLrgSpring  bar;\n"
	  "};\n"
	  "\n"
	  "G_DEFINE_FINAL_TYPE (GamePlayer, game_player, G_TYPE_OBJECT)\n"
	  "\n"
	  "void\n"
	  "game_player_damage (GamePlayer *self,\n"
	  "                    gint        amount)\n"
	  "{\n"
	  "\tg_return_if_fail (GAME_IS_PLAYER (self));\n"
	  "\n"
	  "\tself->health = CLAMP (self->health - amount, 0, MAX_HEALTH);\n"
	  "\tlrg_spring_set_target (&self->bar, (gfloat) self->health / MAX_HEALTH);\n"
	  "\tif (self->health == 0)\n"
	  "\t\tg_signal_emit_by_name (self, \"died\");\n"
	  "}\n" },
	{ "yaml",
	  "# studio.yaml - a docked tool window, loaded by LrgUILoader\n"
	  "shortcuts:\n"
	  "  - {accel: \"<Ctrl>q\", action: app.quit}\n"
	  "  - {accel: space, action: studio.play}\n"
	  "root:\n"
	  "  type: vbox\n"
	  "  children:\n"
	  "    - type: menu-bar\n"
	  "      id: menubar\n"
	  "    - type: dock-area\n"
	  "      id: dock\n"
	  "      fill-weight: 1\n"
	  "      panels:\n"
	  "        - {id: scene, title: Scene, icon: eye}\n"
	  "        - {id: graph, title: Graph, position: right}\n"
	  "        - &console {id: console, title: Console, closable: true}\n"
	  "theme: 'midnight'   # or daylight, catppuccin-latte ...\n"
	  "rate: 1.5\n"
	  "enabled: true\n" },
	{ "glsl",
	  "#version 330\n"
	  "\n"
	  "uniform sampler2D albedo;\n"
	  "uniform float     time;\n"
	  "in vec2           uv;\n"
	  "out vec4          frag;\n"
	  "\n"
	  "void main ()\n"
	  "{\n"
	  "    /* Scroll the texture and pulse its brightness */\n"
	  "    vec4 c = texture (albedo, uv + vec2 (time * 0.1, 0.0));\n"
	  "    frag = vec4 (c.rgb * (0.8 + 0.2 * sin (time * 3.0)), c.a);\n"
	  "}\n" },
	{ "lua",
	  "-- door.lua: open the vault when the player has the key\n"
	  "local Door = {}\n"
	  "\n"
	  "function Door.on_interact (door, player)\n"
	  "  if player:has_item (\"vault-key\") then\n"
	  "    door:play (\"open\", { speed = 1.5 })\n"
	  "    return true\n"
	  "  end\n"
	  "  ui.toast ([[It is locked.]])\n"
	  "  return false\n"
	  "end\n"
	  "\n"
	  "return Door\n" }
};

static void
on_language_changed (LrgComboBox *combo,
                     gpointer     user_data)
{
	Showcase    *app = user_data;
	const gchar *language = lrg_combo_box_get_selected_text (combo);
	guint        i;

	if (language == NULL)
		return;
	lrg_text_editor_set_language (app->code, language);

	/* Languages with a sample get it; the rest re-highlight the text */
	for (i = 0; i < G_N_ELEMENTS (code_samples); i++)
		if (g_strcmp0 (code_samples[i].language, language) == 0)
			lrg_text_editor_set_text (app->code, code_samples[i].text);
}

static void console_print (Showcase *app, const gchar *format, ...) G_GNUC_PRINTF (2, 3);

/* console_print: append one line to the console log; the log follows
 * new output while its caret sits at the end */
static void
console_print (Showcase    *app,
               const gchar *format,
               ...)
{
	g_autofree gchar *line = NULL;
	g_autofree gchar *with_nl = NULL;
	va_list           args;

	va_start (args, format);
	line = g_strdup_vprintf (format, args);
	va_end (args);
	with_nl = g_strconcat ("\n", line, NULL);
	lrg_text_editor_append (app->console_log, with_nl);
}

static void run_console_command (Showcase *app, const gchar *command);

/* on_console_submit: Enter in the input runs its text as a command */
static void
on_console_submit (LrgTextEditor *input,
                   gpointer       user_data)
{
	Showcase         *app = user_data;
	g_autofree gchar *text = lrg_text_editor_get_text (input);

	g_strstrip (text);
	if (text[0] != '\0')
		run_console_command (app, text);
	lrg_text_editor_set_text (input, "");
}

static LrgWidget *
build_editor_page (Showcase *app)
{
	LrgHBox            *page = hrow (20.0f);
	LrgVBox            *box;
	LrgHBox            *bar;
	LrgPanel           *panel;
	LrgLabel           *spacer;
	g_auto(GStrv)       languages = lrg_syntax_list_languages ();
	guint               i;

	/* The code editor with a language picker */
	box = card (LRG_CONTAINER (page), NULL, &panel);
	lrg_widget_set_fill_weight (LRG_WIDGET (panel), 3.0f);
	bar = hrow (10.0f);
	add (bar, label_with_class ("LrgTextEditor", "heading"));
	spacer = label_with_class ("", NULL);
	lrg_widget_set_fill_weight (LRG_WIDGET (spacer), 1.0f);
	lrg_widget_set_valign (LRG_WIDGET (spacer), LRG_ALIGN_CENTER);
	add (bar, spacer);
	app->language_combo = lrg_combo_box_new ();
	for (i = 0; languages != NULL && languages[i] != NULL; i++)
	{
		lrg_combo_box_append (app->language_combo, languages[i]);
		if (g_strcmp0 (languages[i], "c") == 0)
			lrg_combo_box_set_selected (app->language_combo, (gint) i);
	}
	lrg_widget_set_min_size (LRG_WIDGET (app->language_combo), 150.0f, 0.0f);
	g_signal_connect (app->language_combo, "changed", G_CALLBACK (on_language_changed), app);
	lrg_container_add_child (LRG_CONTAINER (bar), LRG_WIDGET (app->language_combo));
	add (box, bar);

	app->code = lrg_text_editor_new ();
	lrg_text_editor_set_language (app->code, "c");
	lrg_text_editor_set_show_line_numbers (app->code, TRUE);
	lrg_text_editor_set_text (app->code, code_samples[0].text);
	lrg_widget_set_fill_weight (LRG_WIDGET (app->code), 1.0f);
	lrg_container_add_child (LRG_CONTAINER (box), LRG_WIDGET (app->code));
	add (box, label_with_class ("Ctrl+F find, Ctrl+Z undo, Tab indents, Ctrl+C / V clipboard", "muted"));

	/* The console: a read-only log and a submit-on-enter input */
	box = card (LRG_CONTAINER (page), "Console", &panel);
	lrg_widget_set_fill_weight (LRG_WIDGET (panel), 1.0f);
	lrg_widget_set_max_size (LRG_WIDGET (panel), 380.0f, 0.0f);
	app->console_log = lrg_text_editor_new ();
	lrg_text_editor_set_read_only (app->console_log, TRUE);
	lrg_text_editor_set_max_lines (app->console_log, 500);
	lrg_text_editor_set_highlight_current_line (app->console_log, FALSE);
	lrg_text_editor_set_wrap (app->console_log, TRUE);
	lrg_text_editor_set_text (app->console_log,
	                          "libregnum console. Type 'help' and press Enter.");
	lrg_widget_set_fill_weight (LRG_WIDGET (app->console_log), 1.0f);
	lrg_container_add_child (LRG_CONTAINER (box), LRG_WIDGET (app->console_log));
	app->console_input = lrg_text_editor_new ();
	lrg_text_editor_set_submit_on_enter (app->console_input, TRUE);
	lrg_text_editor_set_placeholder (app->console_input, "Type a command, e.g. help");
	lrg_text_editor_set_highlight_current_line (app->console_input, FALSE);
	lrg_widget_set_min_size (LRG_WIDGET (app->console_input), 0.0f, 40.0f);
	lrg_widget_set_max_size (LRG_WIDGET (app->console_input), 0.0f, 40.0f);
	g_signal_connect (app->console_input, "submit", G_CALLBACK (on_console_submit), app);
	lrg_container_add_child (LRG_CONTAINER (box), LRG_WIDGET (app->console_input));
	console_print (app, "> echo hello");
	console_print (app, "hello");

	return LRG_WIDGET (page);
}

/* ==========================================================================
 * Graph page: a shader graph in LrgNodeGraph
 * ========================================================================== */

static LrgGraphNode *
graph_node (LrgNodeGraph *graph,
            const gchar  *id,
            const gchar  *title,
            const gchar  *category,
            gfloat        x,
            gfloat        y)
{
	LrgGraphNode *node = lrg_node_graph_add_node (graph, id, title, x, y);

	lrg_graph_node_set_category (node, category);
	return node;
}

static LrgWidget *
build_graph_page (Showcase *app)
{
	LrgVBox      *box;
	LrgPanel     *panel;
	LrgVBox      *page = vcol (0.0f);
	LrgGraphNode *n;
	LrgSlider    *speed;

	box = card (LRG_CONTAINER (page),
	            "LrgNodeGraph: drag cards, drag from a port to wire, wheel to zoom, F frames all", &panel);
	lrg_widget_set_fill_weight (LRG_WIDGET (panel), 1.0f);
	app->graph = lrg_node_graph_new ();
	lrg_widget_set_fill_weight (LRG_WIDGET (app->graph), 1.0f);

	n = graph_node (app->graph, "uv", "UV", "input", 0.0f, 0.0f);
	lrg_graph_node_add_port (n, LRG_GRAPH_PORT_OUTPUT, "uv", "vec2");
	n = graph_node (app->graph, "time", "Time", "input", 0.0f, 140.0f);
	lrg_graph_node_add_port (n, LRG_GRAPH_PORT_OUTPUT, "seconds", "float");
	speed = lrg_slider_new_with_range (0.0, 4.0, 0.1);
	lrg_slider_set_value (speed, 1.5);
	lrg_widget_set_min_size (LRG_WIDGET (speed), 120.0f, 24.0f);
	lrg_graph_node_set_body (n, LRG_WIDGET (speed));
	g_object_unref (speed);

	n = graph_node (app->graph, "noise", "Noise", "texture", 260.0f, -40.0f);
	lrg_graph_node_add_port (n, LRG_GRAPH_PORT_INPUT, "uv", "vec2");
	lrg_graph_node_add_port (n, LRG_GRAPH_PORT_INPUT, "offset", "float");
	lrg_graph_node_add_port (n, LRG_GRAPH_PORT_OUTPUT, "value", "float");
	n = graph_node (app->graph, "albedo", "Albedo texture", "texture", 260.0f, 140.0f);
	lrg_graph_node_add_port (n, LRG_GRAPH_PORT_INPUT, "uv", "vec2");
	lrg_graph_node_add_port (n, LRG_GRAPH_PORT_OUTPUT, "color", "vec4");
	lrg_graph_node_add_port (n, LRG_GRAPH_PORT_OUTPUT, "alpha", "float");

	n = graph_node (app->graph, "mix", "Mix", "math", 520.0f, 30.0f);
	lrg_graph_node_add_port (n, LRG_GRAPH_PORT_INPUT, "a", "vec4");
	lrg_graph_node_add_port (n, LRG_GRAPH_PORT_INPUT, "b", "vec4");
	lrg_graph_node_add_port (n, LRG_GRAPH_PORT_INPUT, "factor", "float");
	lrg_graph_node_add_port (n, LRG_GRAPH_PORT_OUTPUT, "out", "vec4");
	n = graph_node (app->graph, "tint", "Tint colour", "input", 260.0f, 300.0f);
	lrg_graph_node_add_port (n, LRG_GRAPH_PORT_OUTPUT, "color", "vec4");
	n = graph_node (app->graph, "fresnel", "Fresnel", "lighting", 520.0f, 230.0f);
	lrg_graph_node_add_port (n, LRG_GRAPH_PORT_INPUT, "power", "float");
	lrg_graph_node_add_port (n, LRG_GRAPH_PORT_OUTPUT, "rim", "float");

	n = graph_node (app->graph, "out", "Material output", "output", 780.0f, 100.0f);
	lrg_graph_node_add_port (n, LRG_GRAPH_PORT_INPUT, "albedo", "vec4");
	lrg_graph_node_add_port (n, LRG_GRAPH_PORT_INPUT, "emission", "float");
	lrg_graph_node_add_port (n, LRG_GRAPH_PORT_INPUT, "alpha", "float");

	lrg_node_graph_connect_by_name (app->graph, "uv", "uv", "noise", "uv", NULL);
	lrg_node_graph_connect_by_name (app->graph, "uv", "uv", "albedo", "uv", NULL);
	lrg_node_graph_connect_by_name (app->graph, "time", "seconds", "noise", "offset", NULL);
	lrg_node_graph_connect_by_name (app->graph, "albedo", "color", "mix", "a", NULL);
	lrg_node_graph_connect_by_name (app->graph, "tint", "color", "mix", "b", NULL);
	lrg_node_graph_connect_by_name (app->graph, "noise", "value", "mix", "factor", NULL);
	lrg_node_graph_connect_by_name (app->graph, "mix", "out", "out", "albedo", NULL);
	lrg_node_graph_connect_by_name (app->graph, "fresnel", "rim", "out", "emission", NULL);
	lrg_node_graph_connect_by_name (app->graph, "albedo", "alpha", "out", "alpha", NULL);

	lrg_node_graph_set_show_minimap (app->graph, TRUE);
	lrg_node_graph_set_animate_links (app->graph, TRUE);
	lrg_node_graph_set_snap_to_grid (app->graph, TRUE);
	lrg_container_add_child (LRG_CONTAINER (box), LRG_WIDGET (app->graph));
	return LRG_WIDGET (page);
}

/* ==========================================================================
 * Timeline page: LrgTimelineView playing, its curves driving a stage
 * ========================================================================== */

/* stage_paint: a ball whose position, size and hue are sampled from the
 * timeline's curves at the playhead with lrg_timeline_view_evaluate() */
static void
stage_paint (LrgCustomWidget *widget,
             LrgPaintList    *list,
             gfloat           x,
             gfloat           y,
             gfloat           width,
             gfloat           height,
             gpointer         user_data)
{
	Showcase       *app = user_data;
	LrgTheme       *theme = lrg_widget_get_theme (LRG_WIDGET (widget));
	gdouble         t = lrg_timeline_view_get_time (app->timeline);
	gfloat          fx = (gfloat) lrg_timeline_view_evaluate (app->timeline, app->track_x, t);
	gfloat          fy = (gfloat) lrg_timeline_view_evaluate (app->timeline, app->track_y, t);
	gfloat          size = (gfloat) lrg_timeline_view_evaluate (app->timeline, app->track_size, t);
	gfloat          hue = (gfloat) lrg_timeline_view_evaluate (app->timeline, app->track_hue, t);
	Color           c = ColorFromHSV (hue, 0.75f, 1.0f);
	GrlColor        ball = { c.r, c.g, c.b, 255 };
	GrlColor        glow;
	GrlColor        floor_line;
	gfloat          bx = x + 40.0f + fx * (width - 80.0f);
	gfloat          by = y + height - 24.0f - fy * (height - 60.0f);
	gfloat          r = 10.0f + size * 18.0f;

	lrg_paint_list_rect (list, x, y, width, height, 12.0f, lrg_theme_get_color (theme, LRG_THEME_COLOR_INPUT));
	lrg_paint_color_with_alpha (lrg_theme_get_color (theme, LRG_THEME_COLOR_BORDER), 1.0f, &floor_line);
	lrg_paint_list_line (list, x + 20.0f, y + height - 24.0f + r * 0.0f, x + width - 20.0f,
	                     y + height - 24.0f, 1.0f, &floor_line);
	/* The whole path as a dotted trail: the same curves sampled over the
	 * sequence, brighter near the playhead */
	{
		gdouble duration = lrg_timeline_view_get_duration (app->timeline);
		guint   i;

		for (i = 0; i <= 96; i++)
		{
			gdouble  st = duration * (gdouble) i / 96.0;
			gfloat   px = (gfloat) lrg_timeline_view_evaluate (app->timeline, app->track_x, st);
			gfloat   py = (gfloat) lrg_timeline_view_evaluate (app->timeline, app->track_y, st);
			gfloat   near = 1.0f - MIN (1.0f, (gfloat) fabs (st - t) / 1.5f);
			GrlColor dot;

			lrg_paint_color_with_alpha (lrg_theme_get_color (theme, LRG_THEME_COLOR_TEXT_MUTED),
			                            0.25f + 0.6f * near, &dot);
			lrg_paint_list_circle (list, x + 40.0f + px * (width - 80.0f),
			                       y + height - 24.0f - py * (height - 60.0f), 1.5f + 1.5f * near, &dot);
		}
	}
	lrg_paint_color_with_alpha (&ball, 0.55f, &glow);
	lrg_paint_list_shadow (list, bx - r, by - r, r * 2.0f, r * 2.0f, r, 26.0f, 0.0f, 0.0f, 0.0f, &glow);
	lrg_paint_list_circle (list, bx, by, r, &ball);
}

static LrgTimelineViewTrack *
curve_track (Showcase             *app,
             const gchar          *name,
             const gchar          *hex,
             LrgTimelineViewTrack *parent,
             gdouble               min,
             gdouble               max)
{
	LrgTimelineViewTrack *track = lrg_timeline_view_add_track (app->timeline, name, parent);
	GrlColor              c;

	lrg_color_parse (hex, &c);
	lrg_timeline_view_track_set_color (track, &c);
	lrg_timeline_view_track_set_curve_lane (track, TRUE);
	lrg_timeline_view_track_set_value_range (track, min, max);
	lrg_timeline_view_track_set_curve_height (track, 56.0f);
	return track;
}

static LrgWidget *
build_timeline_page (Showcase *app)
{
	LrgVBox              *page = vcol (20.0f);
	LrgVBox              *box;
	LrgPanel             *panel;
	LrgTimelineViewTrack *group;
	LrgTimelineViewTrack *track;
	LrgCustomWidget      *stage;
	LrgCustomWidgetFuncs  funcs = { 0 };
	GrlColor              c;
	guint                 id;

	app->timeline = lrg_timeline_view_new ();
	lrg_timeline_view_set_duration (app->timeline, 8.0);
	lrg_timeline_view_set_fps (app->timeline, 30.0);
	lrg_timeline_view_set_loop (app->timeline, TRUE);

	/* Clips: a camera track and an audio track */
	track = lrg_timeline_view_add_track (app->timeline, "Camera", NULL);
	lrg_timeline_view_add_clip (app->timeline, track, 0.0, 2.5, "Wide shot");
	lrg_timeline_view_add_clip (app->timeline, track, 2.5, 3.0, "Close-up");
	id = lrg_timeline_view_add_clip (app->timeline, track, 5.5, 2.5, "Crane");
	color_hex ("#f472b6", 1.0f, &c);
	lrg_timeline_view_set_clip_color (app->timeline, id, &c);
	track = lrg_timeline_view_add_track (app->timeline, "Music", NULL);
	color_hex ("#34d399", 1.0f, &c);
	lrg_timeline_view_track_set_color (track, &c);
	lrg_timeline_view_add_clip (app->timeline, track, 0.0, 6.0, "theme.ogg");
	lrg_timeline_view_add_clip (app->timeline, track, 6.2, 1.8, "sting.ogg");

	/* Curves under a "Ball" group: they drive the stage above */
	group = lrg_timeline_view_add_track (app->timeline, "Ball", NULL);
	app->track_x = curve_track (app, "Position X", "#22d3ee", group, 0.0, 1.0);
	lrg_timeline_view_add_keyframe (app->timeline, app->track_x, 0.0, 0.0, LRG_TIMELINE_VIEW_INTERPOLATION_EASE);
	lrg_timeline_view_add_keyframe (app->timeline, app->track_x, 4.0, 1.0, LRG_TIMELINE_VIEW_INTERPOLATION_EASE);
	lrg_timeline_view_add_keyframe (app->timeline, app->track_x, 8.0, 0.0, LRG_TIMELINE_VIEW_INTERPOLATION_EASE);
	app->track_y = curve_track (app, "Height", "#fbbf24", group, 0.0, 1.0);
	lrg_timeline_view_add_keyframe (app->timeline, app->track_y, 0.0, 0.0, LRG_TIMELINE_VIEW_INTERPOLATION_EASE);
	lrg_timeline_view_add_keyframe (app->timeline, app->track_y, 1.0, 1.0, LRG_TIMELINE_VIEW_INTERPOLATION_EASE);
	lrg_timeline_view_add_keyframe (app->timeline, app->track_y, 2.0, 0.0, LRG_TIMELINE_VIEW_INTERPOLATION_EASE);
	lrg_timeline_view_add_keyframe (app->timeline, app->track_y, 3.0, 0.7, LRG_TIMELINE_VIEW_INTERPOLATION_EASE);
	lrg_timeline_view_add_keyframe (app->timeline, app->track_y, 4.0, 0.0, LRG_TIMELINE_VIEW_INTERPOLATION_EASE);
	lrg_timeline_view_add_keyframe (app->timeline, app->track_y, 5.0, 0.45, LRG_TIMELINE_VIEW_INTERPOLATION_EASE);
	lrg_timeline_view_add_keyframe (app->timeline, app->track_y, 6.0, 0.0, LRG_TIMELINE_VIEW_INTERPOLATION_EASE);
	lrg_timeline_view_add_keyframe (app->timeline, app->track_y, 7.0, 0.25, LRG_TIMELINE_VIEW_INTERPOLATION_EASE);
	lrg_timeline_view_add_keyframe (app->timeline, app->track_y, 8.0, 0.0, LRG_TIMELINE_VIEW_INTERPOLATION_LINEAR);
	app->track_size = curve_track (app, "Size", "#a855f7", group, 0.0, 1.0);
	lrg_timeline_view_add_keyframe (app->timeline, app->track_size, 0.0, 0.3, LRG_TIMELINE_VIEW_INTERPOLATION_STEP);
	lrg_timeline_view_add_keyframe (app->timeline, app->track_size, 2.5, 0.8, LRG_TIMELINE_VIEW_INTERPOLATION_LINEAR);
	lrg_timeline_view_add_keyframe (app->timeline, app->track_size, 5.5, 0.5, LRG_TIMELINE_VIEW_INTERPOLATION_EASE);
	lrg_timeline_view_add_keyframe (app->timeline, app->track_size, 8.0, 0.3, LRG_TIMELINE_VIEW_INTERPOLATION_EASE);
	app->track_hue = curve_track (app, "Hue", "#ff7a45", group, 0.0, 360.0);
	lrg_timeline_view_add_keyframe (app->timeline, app->track_hue, 0.0, 190.0, LRG_TIMELINE_VIEW_INTERPOLATION_LINEAR);
	lrg_timeline_view_add_keyframe (app->timeline, app->track_hue, 8.0, 330.0, LRG_TIMELINE_VIEW_INTERPOLATION_LINEAR);

	lrg_timeline_view_add_marker (app->timeline, 2.5, "Cut");
	lrg_timeline_view_add_marker (app->timeline, 5.5, "Crane");
	lrg_timeline_view_set_playing (app->timeline, TRUE);

	/* The stage: an LrgCustomWidget reading the curves every frame */
	box = card (LRG_CONTAINER (page), "Curves driving a stage: lrg_timeline_view_evaluate() at the playhead", NULL);
	stage = lrg_custom_widget_new ();
	funcs.paint = stage_paint;
	lrg_custom_widget_set_funcs (stage, &funcs, app, NULL);
	lrg_custom_widget_set_preferred_height (stage, 150.0f);
	add (box, stage);

	box = card (LRG_CONTAINER (page),
	            "LrgTimelineView: scrub the ruler, drag clips and keys, Ctrl+wheel zooms, Space plays", &panel);
	lrg_widget_set_fill_weight (LRG_WIDGET (panel), 1.0f);
	lrg_widget_set_fill_weight (LRG_WIDGET (app->timeline), 1.0f);
	lrg_container_add_child (LRG_CONTAINER (box), LRG_WIDGET (app->timeline));
	return LRG_WIDGET (page);
}

/* ==========================================================================
 * Dock page: LrgDockArea with tabbed, splittable, floatable panels
 * ========================================================================== */

static LrgDockPanel *
dock_panel (LrgDockArea     *dock,
            const gchar     *id,
            const gchar     *title,
            const gchar     *icon,
            LrgWidget       *child,
            LrgDockPanel    *relative,
            LrgDockPosition  position)
{
	LrgDockPanel *panel = lrg_dock_panel_new (id, title);

	lrg_dock_panel_set_icon (panel, icon);
	lrg_dock_panel_set_child (panel, child);
	g_object_unref (child);
	lrg_dock_area_add_panel (dock, panel, relative, position);
	g_object_unref (panel);
	return panel;
}

static LrgWidget *
build_dock_page (Showcase *app)
{
	static const gchar *outline[] = {
		"World", "  Camera", "  Sun", "  Player", "    Weapon", "  Vault door", "  Enemies (12)"
	};
	LrgVBox      *page = vcol (12.0f);
	LrgVBox      *col;
	LrgWrapBox   *assets;
	LrgTextEditor *log;
	LrgDockPanel *scene;
	LrgDockPanel *hierarchy;
	LrgWidget    *backdrop;
	guint         i;

	add (page, label_with_class ("Drag a tab onto another stack's edge to split it, onto a tab strip to "
	                             "join it, or out of the area to float it. Drag dividers to resize.",
	                             "muted"));
	app->dock = lrg_dock_area_new ();
	lrg_widget_set_fill_weight (LRG_WIDGET (app->dock), 1.0f);

	backdrop = g_object_new (SHOWCASE_TYPE_BACKDROP, NULL);
	scene = dock_panel (app->dock, "scene", "Scene", "eye", backdrop, NULL, LRG_DOCK_POSITION_CENTER);
	lrg_dock_panel_set_closable (scene, FALSE);

	col = vcol (6.0f);
	lrg_container_set_padding (LRG_CONTAINER (col), 12.0f);
	for (i = 0; i < G_N_ELEMENTS (outline); i++)
		add (col, label_with_class (outline[i], i == 3 ? NULL : "muted"));
	hierarchy = dock_panel (app->dock, "hierarchy", "Hierarchy", "menu", LRG_WIDGET (col), NULL,
	                        LRG_DOCK_POSITION_LEFT);

	col = vcol (10.0f);
	lrg_container_set_padding (LRG_CONTAINER (col), 12.0f);
	add (col, label_with_class ("Player", "heading"));
	{
		LrgSlider *speed = lrg_slider_new_with_range (0.0, 10.0, 0.5);

		/* Label above control: the panel can be narrow */
		lrg_slider_set_value (speed, 6.5);
		add (col, label_with_class ("Speed", "muted"));
		add (col, speed);
	}
	add (col, lrg_checkbox_new ("Visible in game"));
	add (col, label_with_class ("Team", "muted"));
	add (col, lrg_spin_box_new_with_range (1.0, 4.0, 1.0));
	dock_panel (app->dock, "inspector", "Inspector", "gear", LRG_WIDGET (col), NULL, LRG_DOCK_POSITION_RIGHT);

	log = lrg_text_editor_new ();
	lrg_text_editor_set_read_only (log, TRUE);
	lrg_text_editor_set_highlight_current_line (log, FALSE);
	lrg_text_editor_set_text (log, "[info] level loaded in 84 ms\n[info] 12 enemies spawned\n"
	                               "[warn] texture 'vault_door.png' is not a power of two\n"
	                               "[info] autosave complete");
	dock_panel (app->dock, "log", "Log", "file", LRG_WIDGET (log), scene, LRG_DOCK_POSITION_BOTTOM);

	assets = lrg_wrap_box_new ();
	lrg_container_set_padding (LRG_CONTAINER (assets), 12.0f);
	lrg_container_set_spacing (LRG_CONTAINER (assets), 14.0f);
	for (i = LRG_ICON_NONE + 1; i < LRG_ICON_NONE + 13; i++)
		add (assets, showcase_icon_new ((LrgIcon) i, 28.0f, LRG_THEME_COLOR_ACCENT));
	dock_panel (app->dock, "assets", "Assets", "folder", LRG_WIDGET (assets), hierarchy,
	            LRG_DOCK_POSITION_CENTER);
	lrg_dock_area_present_panel (app->dock, hierarchy);
	lrg_dock_area_set_panel_share (app->dock, scene, 0.62f);
	lrg_dock_area_set_panel_share (app->dock, hierarchy, 0.38f);

	lrg_container_add_child (LRG_CONTAINER (page), LRG_WIDGET (app->dock));
	return LRG_WIDGET (page);
}

/* ==========================================================================
 * Looks page: one LrgCanvasLook for a node graph and a timeline (one
 * above the other), with the link shape, key shape and glow on top of it
 * ========================================================================== */

static void
on_look_changed (LrgComboBox *combo,
                 gpointer     user_data)
{
	Showcase *app = user_data;
	gint      i = lrg_combo_box_get_selected (combo);

	if (i < 0 || i >= (gint) G_N_ELEMENTS (look_names))
		return;
	lrg_node_graph_set_look (app->look_graph, (LrgCanvasLook) i);
	lrg_timeline_view_set_look (app->look_timeline, (LrgCanvasLook) i);
}

static void
on_link_shape_changed (LrgComboBox *combo,
                       gpointer     user_data)
{
	Showcase *app = user_data;
	gint      i = lrg_combo_box_get_selected (combo);

	if (i >= 0 && i < (gint) G_N_ELEMENTS (link_shape_names))
		lrg_node_graph_set_link_shape (app->look_graph, (LrgCanvasLinkShape) i);
}

static void
on_key_shape_changed (LrgComboBox *combo,
                      gpointer     user_data)
{
	Showcase *app = user_data;
	gint      i = lrg_combo_box_get_selected (combo);

	if (i >= 0 && i < (gint) G_N_ELEMENTS (key_shape_names))
		lrg_timeline_view_set_key_shape (app->look_timeline, (LrgCanvasKeyShape) i);
}

/* on_glow_toggled: glow off is 0; on follows the look (negative) */
static void
on_glow_toggled (LrgCheckbox *check,
                 gboolean     checked,
                 gpointer     user_data)
{
	Showcase *app = user_data;

	lrg_node_graph_set_glow (app->look_graph, checked ? -1.0f : 0.0f);
	lrg_timeline_view_set_glow (app->look_timeline, checked ? -1.0f : 0.0f);
}

/* look_combo: a labelled combo box over @names */
static LrgComboBox *
look_combo (LrgHBox      *row,
            const gchar  *caption,
            const gchar **names,
            guint         n_names,
            GCallback     callback,
            Showcase     *app)
{
	LrgComboBox *combo = lrg_combo_box_new ();
	LrgLabel    *label = label_with_class (caption, "muted");
	guint        i;

	for (i = 0; i < n_names; i++)
		lrg_combo_box_append (combo, names[i]);
	lrg_combo_box_set_selected (combo, 0);
	g_signal_connect (combo, "changed", callback, app);
	lrg_widget_set_valign (LRG_WIDGET (label), LRG_ALIGN_CENTER);
	add (row, label);
	lrg_container_add_child (LRG_CONTAINER (row), LRG_WIDGET (combo));
	g_object_unref (combo);
	return combo;
}

static LrgWidget *
build_looks_page (Showcase *app)
{
	LrgVBox              *page = vcol (16.0f);
	LrgHBox              *row = hrow (10.0f);
	LrgSplitter          *split = lrg_splitter_new (LRG_ORIENTATION_VERTICAL);
	LrgVBox              *left = vcol (0.0f);
	LrgVBox              *right = vcol (0.0f);
	LrgVBox              *box;
	LrgPanel             *panel;
	LrgCheckbox          *glow;
	LrgGraphNode         *n;
	LrgTimelineViewTrack *track;
	GrlColor              c;

	/* The controls: look, link shape, key shape, glow */
	app->look_combo = look_combo (row, "Look", look_names, G_N_ELEMENTS (look_names),
	                              G_CALLBACK (on_look_changed), app);
	look_combo (row, "Links", link_shape_names, G_N_ELEMENTS (link_shape_names),
	            G_CALLBACK (on_link_shape_changed), app);
	look_combo (row, "Keys", key_shape_names, G_N_ELEMENTS (key_shape_names),
	            G_CALLBACK (on_key_shape_changed), app);
	glow = lrg_checkbox_new_with_style ("Glow", LRG_CHECKBOX_STYLE_SWITCH);
	lrg_checkbox_set_checked (glow, TRUE);
	lrg_widget_set_valign (LRG_WIDGET (glow), LRG_ALIGN_CENTER);
	g_signal_connect (glow, "toggled", G_CALLBACK (on_glow_toggled), app);
	add (row, glow);
	add (page, row);

	/* Node graph: a small material, one "exec"-style flow */
	box = card (LRG_CONTAINER (left), NULL, &panel);
	lrg_container_set_padding (LRG_CONTAINER (panel), 8.0f);
	lrg_widget_set_fill_weight (LRG_WIDGET (panel), 1.0f);
	app->look_graph = lrg_node_graph_new ();
	lrg_widget_set_fill_weight (LRG_WIDGET (app->look_graph), 1.0f);
	n = graph_node (app->look_graph, "time", "Time", "input", 0.0f, 0.0f);
	lrg_graph_node_add_port (n, LRG_GRAPH_PORT_OUTPUT, "t", "float");
	n = graph_node (app->look_graph, "tex", "Texture", "texture", 0.0f, 120.0f);
	lrg_graph_node_add_port (n, LRG_GRAPH_PORT_OUTPUT, "color", "vec4");
	n = graph_node (app->look_graph, "wave", "Sine wave", "math", 220.0f, 0.0f);
	lrg_graph_node_add_port (n, LRG_GRAPH_PORT_INPUT, "x", "float");
	lrg_graph_node_add_port (n, LRG_GRAPH_PORT_OUTPUT, "y", "float");
	n = graph_node (app->look_graph, "mix", "Mix", "math", 220.0f, 120.0f);
	lrg_graph_node_add_port (n, LRG_GRAPH_PORT_INPUT, "a", "vec4");
	lrg_graph_node_add_port (n, LRG_GRAPH_PORT_INPUT, "factor", "float");
	lrg_graph_node_add_port (n, LRG_GRAPH_PORT_OUTPUT, "out", "vec4");
	n = graph_node (app->look_graph, "out", "Output", "output", 440.0f, 60.0f);
	color_hex ("#f472b6", 1.0f, &c);
	lrg_graph_node_set_accent (n, &c);
	lrg_graph_node_add_port (n, LRG_GRAPH_PORT_INPUT, "albedo", "vec4");
	lrg_graph_node_add_port (n, LRG_GRAPH_PORT_INPUT, "emission", "float");
	lrg_node_graph_connect_by_name (app->look_graph, "time", "t", "wave", "x", NULL);
	lrg_node_graph_connect_by_name (app->look_graph, "tex", "color", "mix", "a", NULL);
	lrg_node_graph_connect_by_name (app->look_graph, "wave", "y", "mix", "factor", NULL);
	lrg_node_graph_connect_by_name (app->look_graph, "mix", "out", "out", "albedo", NULL);
	lrg_node_graph_connect_by_name (app->look_graph, "wave", "y", "out", "emission", NULL);
	lrg_node_graph_set_animate_links (app->look_graph, TRUE);
	lrg_container_add_child (LRG_CONTAINER (box), LRG_WIDGET (app->look_graph));

	/* Timeline: clips, a curve and markers, playing */
	box = card (LRG_CONTAINER (right), NULL, &panel);
	lrg_container_set_padding (LRG_CONTAINER (panel), 8.0f);
	lrg_widget_set_fill_weight (LRG_WIDGET (panel), 1.0f);
	app->look_timeline = lrg_timeline_view_new ();
	lrg_widget_set_fill_weight (LRG_WIDGET (app->look_timeline), 1.0f);
	lrg_timeline_view_set_duration (app->look_timeline, 6.0);
	lrg_timeline_view_set_loop (app->look_timeline, TRUE);
	track = lrg_timeline_view_add_track (app->look_timeline, "Shots", NULL);
	lrg_timeline_view_add_clip (app->look_timeline, track, 0.0, 2.0, "Intro");
	lrg_timeline_view_add_clip (app->look_timeline, track, 2.2, 3.4, "Chase");
	track = lrg_timeline_view_add_track (app->look_timeline, "Music", NULL);
	color_hex ("#34d399", 1.0f, &c);
	lrg_timeline_view_track_set_color (track, &c);
	lrg_timeline_view_add_clip (app->look_timeline, track, 0.0, 6.0, "theme.ogg");
	track = lrg_timeline_view_add_track (app->look_timeline, "Glow", NULL);
	color_hex ("#fbbf24", 1.0f, &c);
	lrg_timeline_view_track_set_color (track, &c);
	lrg_timeline_view_track_set_curve_lane (track, TRUE);
	lrg_timeline_view_track_set_curve_height (track, 48.0f);
	lrg_timeline_view_track_set_value_range (track, 0.0, 1.0);
	lrg_timeline_view_add_keyframe (app->look_timeline, track, 0.0, 0.1, LRG_TIMELINE_VIEW_INTERPOLATION_EASE);
	lrg_timeline_view_add_keyframe (app->look_timeline, track, 2.0, 0.9, LRG_TIMELINE_VIEW_INTERPOLATION_EASE);
	lrg_timeline_view_add_keyframe (app->look_timeline, track, 4.0, 0.3, LRG_TIMELINE_VIEW_INTERPOLATION_LINEAR);
	lrg_timeline_view_add_keyframe (app->look_timeline, track, 6.0, 0.8, LRG_TIMELINE_VIEW_INTERPOLATION_EASE);
	lrg_timeline_view_add_marker (app->look_timeline, 2.2, "Go");
	lrg_timeline_view_add_marker (app->look_timeline, 5.6, "End");
	lrg_timeline_view_set_playing (app->look_timeline, TRUE);
	lrg_container_add_child (LRG_CONTAINER (box), LRG_WIDGET (app->look_timeline));

	/* One above the other in a splitter. (Side by side they would not
	 * fit: a timeline's natural width is its header plus 480, a graph's
	 * 480, and boxes never shrink children below their natural size.) */
	lrg_splitter_set_start_child (split, LRG_WIDGET (left));
	lrg_splitter_set_end_child (split, LRG_WIDGET (right));
	g_object_unref (left);
	g_object_unref (right);
	lrg_splitter_set_min_sizes (split, 160.0f, 160.0f);
	lrg_splitter_set_fraction (split, 0.55f, FALSE);
	lrg_splitter_set_default_fraction (split, 0.55f);
	lrg_widget_set_fill_weight (LRG_WIDGET (split), 1.0f);
	add (page, split);
	return LRG_WIDGET (page);
}

/* ==========================================================================
 * Blocks page: LrgBlockEditor with a tiny interpreter driving a sprite.
 * The interpreter runs from the stage's step callback, one block per
 * BLOCK_STEP seconds of canvas time, so a running script never blocks a
 * frame. Its state is a program counter (the next block) and a stack of
 * the "repeat" blocks it is inside. Editing the program stops a run.
 * ========================================================================== */

/* blocks_palette: the page's palette and sample program, built in C */
static void
blocks_palette (LrgBlockEditor *editor)
{
	static const struct { const gchar *id; LrgBlockShape shape; const gchar *category; const gchar *label; } kinds[] = {
		{ "when-run",  LRG_BLOCK_SHAPE_HAT,       "events",  "when run clicked" },
		{ "move",      LRG_BLOCK_SHAPE_STATEMENT, "motion",  "move {steps:number=40} steps" },
		{ "turn",      LRG_BLOCK_SHAPE_STATEMENT, "motion",  "turn {deg:number=90} degrees" },
		{ "go-home",   LRG_BLOCK_SHAPE_STATEMENT, "motion",  "go home" },
		{ "repeat",    LRG_BLOCK_SHAPE_C,         "control", "repeat {times:number=4}" },
		{ "wait",      LRG_BLOCK_SHAPE_STATEMENT, "control", "wait {s:number=0.5} s" },
		{ "say",       LRG_BLOCK_SHAPE_STATEMENT, "looks",   "say {msg:text=Hello!}" },
		{ "set-color", LRG_BLOCK_SHAPE_STATEMENT, "looks",   "set color {c:dropdown=violet|cyan|mint|amber|coral}" }
	};
	static const struct { const gchar *kind; const gchar *slot; const gchar *value; gboolean into; } program[] = {
		{ "set-color", "c",     "cyan",      FALSE },
		{ "repeat",    "times", "4",         FALSE },
		{ "move",      "steps", "70",        TRUE },   /* into the repeat's mouth */
		{ "turn",      "deg",   "90",        FALSE },
		{ "say",       "msg",   "A square!", FALSE }   /* after the repeat (see below) */
	};
	LrgBlock *last;
	LrgBlock *loop = NULL;
	guint     i;

	for (i = 0; i < G_N_ELEMENTS (kinds); i++)
	{
		g_autoptr(GError) error = NULL;

		if (lrg_block_editor_define_kind (editor, kinds[i].id, kinds[i].shape, kinds[i].category,
		                                  kinds[i].label, &error) == NULL)
			g_printerr ("ui-showcase: block kind %s: %s\n", kinds[i].id, error->message);
	}

	/* when run clicked / set color / repeat 4 { move 70; turn 90 } / say */
	last = lrg_block_editor_create_block (editor, "when-run", 20.0f, 20.0f);
	for (i = 0; i < G_N_ELEMENTS (program); i++)
	{
		LrgBlock *b = lrg_block_editor_create_block (editor, program[i].kind, 0.0f, 0.0f);
		LrgBlock *target = last;
		LrgBlockPlace place = LRG_BLOCK_PLACE_NEXT;

		lrg_block_set_value_by_name (b, program[i].slot, program[i].value);
		if (program[i].into)
		{
			target = loop;
			place = LRG_BLOCK_PLACE_MOUTH;
		}
		else if (g_strcmp0 (program[i].kind, "say") == 0 && loop != NULL)
			target = loop;
		lrg_block_editor_attach (editor, b, target, place, 0);
		if (g_strcmp0 (program[i].kind, "repeat") == 0)
			loop = b;
		last = b;
	}

	/* A second, loose script to play with */
	last = lrg_block_editor_create_block (editor, "when-run", 20.0f, 330.0f);
	{
		LrgBlock *b = lrg_block_editor_create_block (editor, "go-home", 0.0f, 0.0f);

		lrg_block_editor_attach (editor, b, last, LRG_BLOCK_PLACE_NEXT, 0);
	}
	lrg_block_editor_clear_history (editor);
}

static void
blocks_light (Showcase *app,
              LrgBlock *block)
{
	lrg_block_editor_clear_running (app->blocks);
	if (block != NULL)
		lrg_block_set_running (block, TRUE);
}

static void
blocks_status (Showcase    *app,
               const gchar *text)
{
	lrg_label_set_text (app->blocks_status, text);
}

static void
blocks_finish (Showcase    *app,
               const gchar *why)
{
	g_autofree gchar *text = NULL;

	if (!app->running)
		return;
	app->running = FALSE;
	app->pc = NULL;
	g_array_set_size (app->loops, 0);
	blocks_light (app, NULL);
	text = g_strdup_printf ("%s after %u blocks", why, app->run_steps);
	blocks_status (app, text);
}

static void
blocks_run (Showcase *app,
            LrgBlock *top)
{
	blocks_finish (app, "Interrupted");
	app->running = TRUE;
	app->pc = top;
	app->run_delay = 0.0;
	app->run_steps = 0;
	blocks_status (app, "Running...");
}

/* blocks_run_first: the Run button runs the first "when run clicked" */
static void
blocks_run_first (Showcase *app)
{
	guint i;

	for (i = 0; i < lrg_block_editor_get_n_scripts (app->blocks); i++)
	{
		LrgBlock *top = lrg_block_editor_get_nth_script (app->blocks, i);

		if (g_strcmp0 (lrg_block_get_kind_id (top), "when-run") == 0)
		{
			blocks_run (app, top);
			return;
		}
	}
	blocks_status (app, "No \"when run clicked\" script");
}

static void
sprite_trail_push (Showcase *app)
{
	if (app->trail_len == SPRITE_TRAIL)
	{
		memmove (app->trail, app->trail + 2, sizeof (gfloat) * 2 * (SPRITE_TRAIL - 1));
		app->trail_len--;
	}
	app->trail[app->trail_len * 2] = app->sprite_x;
	app->trail[app->trail_len * 2 + 1] = app->sprite_y;
	app->trail_len++;
}

/* blocks_step: run one block; returns its duration in seconds, or a
 * negative value when the script ended */
static gdouble
blocks_step (Showcase *app)
{
	LrgBlock    *block;
	const gchar *id;
	gdouble      cost = BLOCK_STEP;

	/* At the end of a stack: finish, loop again, or leave the loop */
	while (app->pc == NULL)
	{
		BlockLoop *frame;

		if (app->loops->len == 0)
		{
			blocks_finish (app, "Finished");
			return -1.0;
		}
		frame = &g_array_index (app->loops, BlockLoop, app->loops->len - 1);
		if (--frame->left > 0)
			app->pc = lrg_block_get_mouth (frame->loop, 0);
		else
		{
			app->pc = lrg_block_get_next (frame->loop);
			g_array_set_size (app->loops, app->loops->len - 1);
		}
	}

	block = app->pc;
	id = lrg_block_get_kind_id (block);
	app->pc = lrg_block_get_next (block);
	app->run_steps++;
	blocks_light (app, block);

	if (g_strcmp0 (id, "move") == 0)
	{
		gfloat rad = app->sprite_heading * (gfloat) G_PI / 180.0f;
		gfloat steps = (gfloat) lrg_block_get_number (block, "steps");

		/* The stage is 300 x 240 units around the centre: stay on it */
		app->sprite_x += cosf (rad) * steps;
		app->sprite_y += sinf (rad) * steps;
		if (fabsf (app->sprite_x) > 150.0f || fabsf (app->sprite_y) > 120.0f)
		{
			app->sprite_x = CLAMP (app->sprite_x, -150.0f, 150.0f);
			app->sprite_y = CLAMP (app->sprite_y, -120.0f, 120.0f);
		}
		sprite_trail_push (app);
	}
	else if (g_strcmp0 (id, "turn") == 0)
		app->sprite_heading += (gfloat) lrg_block_get_number (block, "deg");
	else if (g_strcmp0 (id, "go-home") == 0)
	{
		app->sprite_x = 0.0f;
		app->sprite_y = 0.0f;
		app->sprite_heading = 0.0f;
		app->trail_len = 0;
		sprite_trail_push (app);
	}
	else if (g_strcmp0 (id, "repeat") == 0)
	{
		gint      times = (gint) lrg_block_get_number (block, "times");
		LrgBlock *mouth = lrg_block_get_mouth (block, 0);

		if (times > 0 && mouth != NULL)
		{
			BlockLoop frame = { block, MIN (times, 10000) };

			g_array_append_val (app->loops, frame);
			app->pc = mouth;
		}
		cost = BLOCK_STEP * 0.4;
	}
	else if (g_strcmp0 (id, "wait") == 0)
		cost = CLAMP (lrg_block_get_number (block, "s"), 0.0, 30.0);
	else if (g_strcmp0 (id, "say") == 0)
	{
		g_free (app->sprite_say);
		app->sprite_say = g_strdup (lrg_block_get_value_by_name (block, "msg"));
		app->say_left = 2.5f;
	}
	else if (g_strcmp0 (id, "set-color") == 0)
	{
		g_free (app->sprite_color);
		app->sprite_color = g_strdup (lrg_block_get_value_by_name (block, "c"));
	}
	else
		cost = BLOCK_STEP * 0.4;   /* the hat */
	return cost;
}

/* stage_step: the canvas clock. Eases the drawn sprite towards its
 * target and runs the interpreter */
static gboolean
blocks_stage_step (LrgCustomWidget *widget,
                   gfloat           dt,
                   gpointer         user_data)
{
	Showcase *app = user_data;
	gfloat    k = MIN (1.0f, dt * 9.0f);

	app->shown_x += (app->sprite_x - app->shown_x) * k;
	app->shown_y += (app->sprite_y - app->shown_y) * k;
	app->shown_heading += (app->sprite_heading - app->shown_heading) * k;
	if (app->say_left > 0.0f)
		app->say_left -= dt;

	if (app->running)
	{
		app->run_delay -= dt;
		while (app->running && app->run_delay <= 0.0)
		{
			gdouble cost = blocks_step (app);

			if (cost < 0.0)
				break;
			app->run_delay += cost;
		}
	}
	return TRUE;
}

/* sprite_rgb: the sprite's colour from its "set color" choice */
static void
sprite_rgb (Showcase *app,
            GrlColor *out)
{
	guint i;

	color_hex ("#7c6cff", 1.0f, out);
	for (i = 0; i < G_N_ELEMENTS (sprite_colors); i++)
		if (g_strcmp0 (app->sprite_color, sprite_colors[i].name) == 0)
			color_hex (sprite_colors[i].hex, 1.0f, out);
}

/* blocks_stage_paint: a dotted floor, the path walked so far, the
 * sprite with a heading needle, and its speech bubble */
static void
blocks_stage_paint (LrgCustomWidget *widget,
                    LrgPaintList    *list,
                    gfloat           x,
                    gfloat           y,
                    gfloat           width,
                    gfloat           height,
                    gpointer         user_data)
{
	Showcase *app = user_data;
	LrgTheme *theme = lrg_widget_get_theme (LRG_WIDGET (widget));
	GrlFont  *font = lrg_theme_get_ui_font (theme);
	gfloat    cx = x + width * 0.5f;
	gfloat    cy = y + height * 0.5f;
	gfloat    scale = MIN (width / 340.0f, height / 280.0f);
	gfloat    sx = cx + app->shown_x * scale;
	gfloat    sy = cy - app->shown_y * scale;
	gfloat    rad = app->shown_heading * (gfloat) G_PI / 180.0f;
	GrlColor  body;
	GrlColor  glow;
	GrlColor  dot;
	GrlColor  path;
	gfloat    gx;
	gfloat    gy;
	guint     i;

	lrg_paint_list_rect (list, x, y, width, height, 12.0f, lrg_theme_get_color (theme, LRG_THEME_COLOR_INPUT));
	lrg_paint_color_with_alpha (lrg_theme_get_color (theme, LRG_THEME_COLOR_TEXT_MUTED), 0.25f, &dot);
	for (gx = x + 14.0f; gx < x + width - 6.0f; gx += 22.0f)
		for (gy = y + 14.0f; gy < y + height - 6.0f; gy += 22.0f)
			lrg_paint_list_circle (list, gx, gy, 1.2f, &dot);

	/* The path walked */
	sprite_rgb (app, &body);
	lrg_paint_color_with_alpha (&body, 0.6f, &path);
	for (i = 1; i < app->trail_len; i++)
		lrg_paint_list_line (list, cx + app->trail[(i - 1) * 2] * scale, cy - app->trail[(i - 1) * 2 + 1] * scale,
		                     cx + app->trail[i * 2] * scale, cy - app->trail[i * 2 + 1] * scale, 3.0f, &path);

	/* The sprite */
	lrg_paint_color_with_alpha (&body, 0.5f, &glow);
	lrg_paint_list_shadow (list, sx - 16.0f, sy - 16.0f, 32.0f, 32.0f, 16.0f, 22.0f, 0.0f, 0.0f, 0.0f, &glow);
	lrg_paint_list_circle (list, sx, sy, 16.0f, &body);
	lrg_paint_list_line (list, sx, sy, sx + cosf (rad) * 24.0f, sy - sinf (rad) * 24.0f, 4.0f,
	                     lrg_theme_get_color (theme, LRG_THEME_COLOR_TEXT));

	/* Speech bubble */
	if (app->say_left > 0.0f && app->sprite_say != NULL && app->sprite_say[0] != '\0')
	{
		gfloat size = lrg_theme_get_metric (theme, LRG_THEME_METRIC_FONT_SIZE);
		gfloat tw = 0.0f;
		gfloat th = 0.0f;
		gfloat bx;
		gfloat by;

		lrg_paint_measure_text (font, app->sprite_say, size, 0.0f, &tw, &th);
		bx = CLAMP (sx - tw * 0.5f - 10.0f, x + 4.0f, x + width - tw - 24.0f);
		by = MAX (y + 4.0f, sy - 34.0f - th - 12.0f);
		lrg_paint_list_push_opacity (list, MIN (1.0f, app->say_left * 3.0f));
		lrg_paint_list_rect (list, bx, by, tw + 20.0f, th + 12.0f, 10.0f,
		                     lrg_theme_get_color (theme, LRG_THEME_COLOR_POPUP));
		lrg_paint_list_text (list, font, app->sprite_say, bx + 10.0f, by + 6.0f, size, 0.0f,
		                     lrg_theme_get_color (theme, LRG_THEME_COLOR_TEXT));
		lrg_paint_list_pop_opacity (list);
	}
}

static void
on_blocks_activated (LrgBlockEditor *editor,
                     LrgBlock       *top,
                     gpointer        user_data)
{
	blocks_run (user_data, top);
}

static void
on_blocks_changed (LrgBlockEditor *editor,
                   gpointer        user_data)
{
	blocks_finish (user_data, "Stopped: the program was edited");
}

static void
on_blocks_run (LrgButton *button,
               gpointer   user_data)
{
	blocks_run_first (user_data);
}

static void
on_blocks_stop (LrgButton *button,
                gpointer   user_data)
{
	blocks_finish (user_data, "Stopped");
}

static void
on_blocks_reset (LrgButton *button,
                 gpointer   user_data)
{
	Showcase *app = user_data;

	blocks_finish (app, "Reset");
	app->sprite_x = app->sprite_y = app->sprite_heading = 0.0f;
	app->trail_len = 0;
	sprite_trail_push (app);
	g_clear_pointer (&app->sprite_say, g_free);
}

static void
on_blocks_clean_up (LrgButton *button,
                    gpointer   user_data)
{
	lrg_block_editor_clean_up (((Showcase *) user_data)->blocks);
}

static LrgWidget *
build_blocks_page (Showcase *app)
{
	LrgVBox              *page = vcol (16.0f);
	LrgHBox              *bottom = hrow (16.0f);
	LrgVBox              *side;
	LrgVBox              *box;
	LrgPanel             *panel;
	LrgHBox              *row;
	LrgCustomWidget      *stage;
	LrgCustomWidgetFuncs  funcs = { 0 };
	LrgButton            *btn;

	/* The editor fills the page; the stage and controls sit under it
	 * (side by side would exceed the page: an editor's natural width is
	 * 520 plus its palette) */
	box = card (LRG_CONTAINER (page), NULL, &panel);
	lrg_widget_set_fill_weight (LRG_WIDGET (panel), 1.0f);
	lrg_container_set_padding (LRG_CONTAINER (panel), 10.0f);
	app->blocks = lrg_block_editor_new ();
	lrg_block_editor_set_palette_width (app->blocks, 190.0f);
	lrg_widget_set_fill_weight (LRG_WIDGET (app->blocks), 1.0f);
	blocks_palette (app->blocks);
	app->loops = g_array_new (FALSE, FALSE, sizeof (BlockLoop));
	g_signal_connect (app->blocks, "block-activated", G_CALLBACK (on_blocks_activated), app);
	g_signal_connect (app->blocks, "changed", G_CALLBACK (on_blocks_changed), app);
	lrg_container_add_child (LRG_CONTAINER (box), LRG_WIDGET (app->blocks));
	g_object_unref (app->blocks);

	/* The stage */
	box = card (LRG_CONTAINER (bottom), NULL, &panel);
	lrg_container_set_padding (LRG_CONTAINER (panel), 10.0f);
	stage = lrg_custom_widget_new ();
	funcs.paint = blocks_stage_paint;
	funcs.step = blocks_stage_step;
	lrg_custom_widget_set_funcs (stage, &funcs, app, NULL);
	lrg_custom_widget_set_preferred_width (stage, 360.0f);
	lrg_custom_widget_set_preferred_height (stage, 210.0f);
	add (box, stage);
	sprite_trail_push (app);

	/* The run controls */
	side = card (LRG_CONTAINER (bottom), "Run the program", &panel);
	lrg_widget_set_fill_weight (LRG_WIDGET (panel), 1.0f);
	row = hrow (8.0f);
	btn = lrg_button_new ("Run");
	lrg_widget_add_class (LRG_WIDGET (btn), "primary");
	g_signal_connect (btn, "clicked", G_CALLBACK (on_blocks_run), app);
	add (row, btn);
	btn = lrg_button_new ("Stop");
	g_signal_connect (btn, "clicked", G_CALLBACK (on_blocks_stop), app);
	add (row, btn);
	btn = lrg_button_new ("Reset");
	g_signal_connect (btn, "clicked", G_CALLBACK (on_blocks_reset), app);
	add (row, btn);
	btn = lrg_button_new ("Clean up");
	lrg_widget_add_class (LRG_WIDGET (btn), "flat");
	g_signal_connect (btn, "clicked", G_CALLBACK (on_blocks_clean_up), app);
	add (row, btn);
	add (side, row);
	app->blocks_status = label_with_class ("Press Run, or double-click a script", NULL);
	add (side, app->blocks_status);
	add (side, label_with_class ("Drag blocks out of the palette and snap them together. One block\n"
	                             "runs per tick of the canvas clock; the running block glows\n"
	                             "(lrg_block_set_running). Ctrl+Z undoes; drop on the palette deletes.",
	                             "muted"));
	add (page, bottom);
	return LRG_WIDGET (page);
}

/* ==========================================================================
 * Files page: an embedded LrgFileBrowser and the four LrgFileDialog
 * operations, in-app (the dialogs are modal sheets on this canvas)
 * ========================================================================== */

/* files_list: "a, b, c" of a GListModel of GFile */
static gchar *
files_list (GListModel *files)
{
	GString *out = g_string_new (NULL);
	guint    n = g_list_model_get_n_items (files);
	guint    i;

	for (i = 0; i < n && i < 4; i++)
	{
		g_autoptr(GFile) file = g_list_model_get_item (files, i);
		g_autofree gchar *name = g_file_get_basename (file);

		g_string_append_printf (out, "%s%s", i > 0 ? ", " : "", name);
	}
	if (n > 4)
		g_string_append_printf (out, " and %u more", n - 4);
	return g_string_free (out, FALSE);
}

static void
on_files_selection (LrgFileBrowser *browser,
                    gpointer        user_data)
{
	Showcase              *app = user_data;
	g_autoptr(GListModel)  files = lrg_file_browser_get_selected_files (browser);
	g_autofree gchar      *names = files_list (files);
	g_autofree gchar      *text = NULL;

	if (g_list_model_get_n_items (files) == 0)
		text = g_strdup ("Nothing selected (Ctrl / Shift + click selects several)");
	else
		text = g_strdup_printf ("%u selected: %s", g_list_model_get_n_items (files), names);
	lrg_label_set_text (app->files_selection, text);
}

static void
on_files_activated (LrgFileBrowser *browser,
                    GFile          *file,
                    gpointer        user_data)
{
	g_autofree gchar *name = g_file_get_basename (file);

	show_toast (user_data, "Opened", name, LRG_TOAST_KIND_SUCCESS);
}

/* files_report: show a dialog's result under the buttons and as a toast */
static void
files_report (Showcase    *app,
              const gchar *what,
              const gchar *result,
              GError      *error)
{
	g_autofree gchar *text = NULL;

	if (result == NULL)
	{
		gboolean dismissed = g_error_matches (error, LRG_FILE_DIALOG_ERROR, LRG_FILE_DIALOG_ERROR_DISMISSED);

		text = g_strdup_printf ("%s: %s", what, dismissed ? "cancelled" : error->message);
		lrg_label_set_text (app->files_result, text);
		if (!dismissed)
			show_toast (app, what, error->message, LRG_TOAST_KIND_DANGER);
		return;
	}
	text = g_strdup_printf ("%s: %s", what, result);
	lrg_label_set_text (app->files_result, text);
	show_toast (app, what, result, LRG_TOAST_KIND_SUCCESS);
}

static void
on_dialog_file (GObject      *source,
                GAsyncResult *result,
                gpointer      user_data)
{
	Showcase         *app = user_data;
	LrgFileDialog    *dialog = LRG_FILE_DIALOG (source);
	g_autoptr(GError) error = NULL;
	g_autoptr(GFile)  file = NULL;
	const gchar      *what = g_object_get_data (source, "what");
	g_autofree gchar *path = NULL;

	if (g_strcmp0 (what, "Save") == 0)
		file = lrg_file_dialog_save_finish (dialog, result, &error);
	else if (g_strcmp0 (what, "Select folder") == 0)
		file = lrg_file_dialog_select_folder_finish (dialog, result, &error);
	else
		file = lrg_file_dialog_open_finish (dialog, result, &error);
	path = file != NULL ? g_file_get_path (file) : NULL;
	files_report (app, what, path, error);
}

static void
on_dialog_files (GObject      *source,
                 GAsyncResult *result,
                 gpointer      user_data)
{
	g_autoptr(GError)     error = NULL;
	g_autoptr(GListModel) files = lrg_file_dialog_open_multiple_finish (LRG_FILE_DIALOG (source), result, &error);
	g_autofree gchar     *names = files != NULL ? files_list (files) : NULL;

	files_report (user_data, "Open multiple", names, error);
}

/* run_file_dialog: one LrgFileDialog operation by name ("Open", "Open
 * multiple", "Save", "Select folder"), shown on @anchor's canvas. Each
 * run makes a new dialog description; its filters match the embedded
 * browser's. The buttons pass their "what" data (on_dialog_mode). */
static void
run_file_dialog (Showcase    *app,
                 const gchar *what,
                 LrgWidget   *anchor)
{
	g_autoptr(LrgFileDialog)  dialog = lrg_file_dialog_new ();
	g_autoptr(GFile)          home = g_file_new_for_path (g_get_home_dir ());

	lrg_file_dialog_set_filters (dialog, lrg_file_browser_get_filters (app->files));
	lrg_file_dialog_set_initial_folder (dialog, home);
	lrg_file_dialog_set_show_system_places (dialog, TRUE);
	/* The callback reads the operation back from the dialog object */
	g_object_set_data_full (G_OBJECT (dialog), "what", g_strdup (what), g_free);

	if (g_strcmp0 (what, "Open") == 0)
		lrg_file_dialog_open (dialog, anchor, NULL, on_dialog_file, app);
	else if (g_strcmp0 (what, "Open multiple") == 0)
		lrg_file_dialog_open_multiple (dialog, anchor, NULL, on_dialog_files, app);
	else if (g_strcmp0 (what, "Save") == 0)
	{
		lrg_file_dialog_set_initial_name (dialog, "untitled.txt");
		lrg_file_dialog_save (dialog, anchor, NULL, on_dialog_file, app);
	}
	else
		lrg_file_dialog_select_folder (dialog, anchor, NULL, on_dialog_file, app);
}

static void
on_dialog_mode (LrgButton *button,
                gpointer   user_data)
{
	run_file_dialog (user_data, g_object_get_data (G_OBJECT (button), "what"), LRG_WIDGET (button));
}

static LrgWidget *
build_files_page (Showcase *app)
{
	static const gchar *modes[] = { "Open", "Open multiple", "Save", "Select folder" };
	LrgVBox                 *page = vcol (14.0f);
	LrgHBox                 *row = hrow (10.0f);
	LrgVBox                 *box;
	LrgPanel                *panel;
	g_autoptr(GFile)         home = g_file_new_for_path (g_get_home_dir ());
	g_autoptr(GFile)         docs = g_file_get_child (home, "Documents");
	g_autoptr(LrgFileFilter) all = lrg_file_filter_new ("All files");
	g_autoptr(LrgFileFilter) images = lrg_file_filter_new ("Images");
	g_autoptr(LrgFileFilter) text = lrg_file_filter_new ("Text and data");
	guint                    i;

	/* The four dialog operations */
	for (i = 0; i < G_N_ELEMENTS (modes); i++)
	{
		g_autofree gchar *label = g_strconcat (modes[i], "...", NULL);
		LrgButton        *btn = lrg_button_new (label);

		if (i == 0)
			lrg_widget_add_class (LRG_WIDGET (btn), "primary");
		g_object_set_data (G_OBJECT (btn), "what", (gpointer) modes[i]);
		g_signal_connect (btn, "clicked", G_CALLBACK (on_dialog_mode), app);
		add (row, btn);
	}
	app->files_result = label_with_class ("LrgFileDialog: in-app sheets on this canvas", "muted");
	lrg_widget_set_valign (LRG_WIDGET (app->files_result), LRG_ALIGN_CENTER);
	add (row, app->files_result);
	add (page, row);

	/* The browser: several files at once, from the home folder */
	lrg_file_filter_add_pattern (all, "*");
	lrg_file_filter_add_mime_type (images, "image/*");
	lrg_file_filter_add_suffix (images, "png");
	lrg_file_filter_add_suffix (images, "jpg");
	lrg_file_filter_add_suffix (text, "txt");
	lrg_file_filter_add_suffix (text, "md");
	lrg_file_filter_add_suffix (text, "org");
	lrg_file_filter_add_suffix (text, "yaml");
	box = card (LRG_CONTAINER (page), NULL, &panel);
	lrg_widget_set_fill_weight (LRG_WIDGET (panel), 1.0f);
	app->files = g_object_new (LRG_TYPE_FILE_BROWSER,
	                           "mode", LRG_FILE_BROWSER_MODE_OPEN_MULTIPLE,
	                           "location", home,
	                           NULL);
	lrg_file_browser_add_filter (app->files, all);
	lrg_file_browser_add_filter (app->files, images);
	lrg_file_browser_add_filter (app->files, text);
	lrg_file_browser_add_place (app->files, "Documents", docs, LRG_ICON_STAR);
	lrg_widget_set_fill_weight (LRG_WIDGET (app->files), 1.0f);
	g_signal_connect (app->files, "selection-changed", G_CALLBACK (on_files_selection), app);
	g_signal_connect (app->files, "file-activated", G_CALLBACK (on_files_activated), app);
	lrg_container_add_child (LRG_CONTAINER (box), LRG_WIDGET (app->files));
	g_object_unref (app->files);
	app->files_selection = label_with_class ("", "muted");
	add (box, app->files_selection);
	on_files_selection (app->files, app);
	return LRG_WIDGET (page);
}

static void
show_page (Showcase *app,
           guint     index)
{
	guint i;

	for (i = 0; i < N_PAGES; i++)
		lrg_widget_set_visible (app->page[i], i == index);
	app->current_page = (gint) index;

	/* View > Go to page shows the radio dot on this page */
	if (app->actions != NULL)
	{
		g_autofree gchar *lower = g_ascii_strdown (page_names[index], -1);

		g_simple_action_set_state (G_SIMPLE_ACTION (g_action_map_lookup_action (G_ACTION_MAP (app->actions), "page")),
		                           g_variant_new_string (lower));
	}

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

/* ==========================================================================
 * Menu bar, actions and shortcuts
 *
 * Every menu item is a GIO action in the "show" group on the canvas (or
 * a built-in "app." / "ui." action). Stateful actions drive the marks:
 * a string state draws a radio dot on the item whose target matches
 * (theme, motion, accent, page), a boolean state a check mark (RTL,
 * pick mode). The header combo boxes and the menus stay in sync because
 * both go through the same actions.
 * ========================================================================== */

static GSimpleAction *
show_action (Showcase    *app,
             const gchar *name)
{
	return G_SIMPLE_ACTION (g_action_map_lookup_action (G_ACTION_MAP (app->actions), name));
}

/* on_theme_action: show.theme(s), a radio over the presets. The
 * built-in ui.theme would put a fresh preset on the canvas; the
 * showcase also needs its card and glass style rules, so it rebuilds
 * the default theme through apply_theme() instead */
static void
on_theme_action (GSimpleAction *action,
                 GVariant      *parameter,
                 gpointer       user_data)
{
	Showcase            *app = user_data;
	const gchar         *name = g_variant_get_string (parameter, NULL);
	const gchar * const *presets = lrg_theme_list_presets ();
	guint                i;

	for (i = 0; presets[i] != NULL; i++)
	{
		if (g_strcmp0 (presets[i], name) != 0)
			continue;
		g_simple_action_set_state (action, g_variant_new_string (name));
		apply_theme (app, name);
		if (!app->syncing_menu && app->theme_combo != NULL)
		{
			app->syncing_menu = TRUE;
			lrg_combo_box_set_selected (app->theme_combo, (gint) i);
			app->syncing_menu = FALSE;
		}
		return;
	}
	show_toast (app, "Unknown theme", name, LRG_TOAST_KIND_WARNING);
}

/* on_motion_action: show.motion(s), a radio over the motion presets */
static void
on_motion_action (GSimpleAction *action,
                  GVariant      *parameter,
                  gpointer       user_data)
{
	Showcase    *app = user_data;
	const gchar *name = g_variant_get_string (parameter, NULL);
	guint        i;

	for (i = 0; i < G_N_ELEMENTS (motions); i++)
	{
		if (g_ascii_strcasecmp (motions[i].name, name) != 0)
			continue;
		g_simple_action_set_state (action, parameter);
		lrg_theme_set_motion (lrg_theme_get_default (), motions[i].motion);
		if (!app->syncing_menu && app->motion_combo != NULL)
		{
			app->syncing_menu = TRUE;
			lrg_combo_box_set_selected (app->motion_combo, (gint) i);
			app->syncing_menu = FALSE;
		}
		return;
	}
}

/* on_accent_action: show.accent(s), a radio over the swatch colours */
static void
on_accent_action (GSimpleAction *action,
                  GVariant      *parameter,
                  gpointer       user_data)
{
	Showcase *app = user_data;
	GrlColor  accent;

	if (lrg_color_parse (g_variant_get_string (parameter, NULL), &accent))
	{
		g_simple_action_set_state (action, parameter);
		set_accent (app, &accent);
		sync_pickers_to_accent (app);
	}
}

/* on_rtl_action: show.rtl, a check item. Flips the canvas default
 * direction through the built-in ui.direction action */
static void
on_rtl_action (GSimpleAction *action,
               GVariant      *parameter,
               gpointer       user_data)
{
	Showcase          *app = user_data;
	g_autoptr(GVariant) state = g_action_get_state (G_ACTION (action));
	gboolean           rtl = !g_variant_get_boolean (state);

	g_simple_action_set_state (action, g_variant_new_boolean (rtl));
	lrg_widget_activate_action (LRG_WIDGET (app->canvas), "ui.direction",
	                            g_variant_new_string (rtl ? "rtl" : "ltr"));
}

/* on_page_action: show.page(s), a radio over the sidebar pages */
static void
on_page_action (GSimpleAction *action,
                GVariant      *parameter,
                gpointer       user_data)
{
	Showcase    *app = user_data;
	const gchar *name = g_variant_get_string (parameter, NULL);
	guint        i;

	for (i = 0; i < N_PAGES; i++)
		if (g_ascii_strcasecmp (page_names[i], name) == 0)
			lrg_list_view_select (app->nav, i);
}

/* on_pick_action: show.pick, a check item mirroring inspector pick mode */
static void
on_pick_action (GSimpleAction *action,
                GVariant      *parameter,
                gpointer       user_data)
{
	toggle_pick_mode (user_data);
}

static void
on_toasts_action (GSimpleAction *action,
                  GVariant      *parameter,
                  gpointer       user_data)
{
	show_toast (user_data, "Welcome back", "Last played 2 hours ago.", LRG_TOAST_KIND_INFO);
	show_toast (user_data, "Achievement unlocked", "Pixel perfect: no damage taken.", LRG_TOAST_KIND_SUCCESS);
}

static void
on_dialog_action (GSimpleAction *action,
                  GVariant      *parameter,
                  gpointer       user_data)
{
	Showcase *app = user_data;

	lrg_dialog_present (app->dialog, app->canvas);
}

/* on_screenshot_action: the capture happens after the next render (see
 * the main loop); the file goes to the user's cache directory */
static void
on_screenshot_action (GSimpleAction *action,
                      GVariant      *parameter,
                      gpointer       user_data)
{
	Showcase         *app = user_data;
	g_autofree gchar *dir = g_build_filename (g_get_user_cache_dir (), "libregnum", NULL);
	g_autofree gchar *name = g_strdup_printf ("ui-showcase-%s.png", page_names[MAX (app->current_page, 0)]);

	g_mkdir_with_parents (dir, 0755);
	g_free (app->shot_request);
	app->shot_request = g_build_filename (dir, name, NULL);
}

static void
on_reload_action (GSimpleAction *action,
                  GVariant      *parameter,
                  gpointer       user_data)
{
	Showcase          *app = user_data;
	g_autoptr(GError)  error = NULL;

	if (lrg_ui_loader_reload (app->loader, &error))
		show_toast (app, "Reloaded", "The declarative page was rebuilt.", LRG_TOAST_KIND_SUCCESS);
	else
		show_toast (app, "Reload failed", error != NULL ? error->message : "nothing is watched",
		            LRG_TOAST_KIND_DANGER);
}

/* on_undo_delete_action: show.undo-delete, the Undo button of the
 * "Deleted 3 items" toast */
static void
on_undo_delete_action (GSimpleAction *action,
                       GVariant      *parameter,
                       gpointer       user_data)
{
	show_toast (user_data, "Restored 3 items", "crate.png, barrel.png and torch.png are back.",
	            LRG_TOAST_KIND_SUCCESS);
}

static void
on_shortcuts_action (GSimpleAction *action,
                     GVariant      *parameter,
                     gpointer       user_data)
{
	show_toast (user_data, "Keyboard shortcuts",
	            "Ctrl+Q quit, Ctrl+1..9 pages, Ctrl+Shift+R right-to-left, Ctrl+D dialog, "
	            "Ctrl+Shift+S screenshot, F5 reload YAML, F12 pick mode, F1 this help.",
	            LRG_TOAST_KIND_INFO);
}

static void
on_about_action (GSimpleAction *action,
                 GVariant      *parameter,
                 gpointer       user_data)
{
	show_toast (user_data, "libregnum UI showcase",
	            "A GObject game engine on raylib. AGPL-3.0-or-later.", LRG_TOAST_KIND_INFO);
}

/* build_menu_model: File / View / Theme / Motion / Help */
static GMenuModel *
build_menu_model (void)
{
	GMenu               *bar = g_menu_new ();
	g_autoptr(GMenu)     file = g_menu_new ();
	g_autoptr(GMenu)     file_demo = g_menu_new ();
	g_autoptr(GMenu)     file_io = g_menu_new ();
	g_autoptr(GMenu)     file_quit = g_menu_new ();
	g_autoptr(GMenu)     view = g_menu_new ();
	g_autoptr(GMenu)     pages = g_menu_new ();
	g_autoptr(GMenu)     view_opts = g_menu_new ();
	g_autoptr(GMenu)     theme = g_menu_new ();
	g_autoptr(GMenu)     presets = g_menu_new ();
	g_autoptr(GMenu)     accent = g_menu_new ();
	g_autoptr(GMenu)     motion = g_menu_new ();
	g_autoptr(GMenu)     help = g_menu_new ();
	const gchar * const *names = lrg_theme_list_presets ();
	guint                i;

	/* File */
	append_item (file_demo, "Show _toasts", "show.toasts", "info", NULL);
	append_item (file_demo, "Open _dialog", "show.dialog", "edit", NULL);
	g_menu_append_section (file, NULL, G_MENU_MODEL (file_demo));
	append_item (file_io, "Save _screenshot", "show.screenshot", "download", NULL);
	append_item (file_io, "_Reload YAML page", "show.reload", "refresh", NULL);
	g_menu_append_section (file, NULL, G_MENU_MODEL (file_io));
	append_item (file_quit, "_Quit", "app.quit", "close", NULL);
	g_menu_append_section (file, NULL, G_MENU_MODEL (file_quit));
	g_menu_append_submenu (bar, "_File", G_MENU_MODEL (file));

	/* View: a page submenu (radio), RTL and pick mode (checks) */
	for (i = 0; i < N_PAGES; i++)
	{
		g_autofree gchar *lower = g_ascii_strdown (page_names[i], -1);
		g_autofree gchar *detailed = g_strdup_printf ("show.page::%s", lower);

		append_item (pages, page_names[i], detailed, NULL, NULL);
	}
	g_menu_append_submenu (view, "_Go to page", G_MENU_MODEL (pages));
	append_item (view_opts, "_Right-to-left layout", "show.rtl", NULL, NULL);
	append_item (view_opts, "Inspector _pick mode", "show.pick", NULL, "F12");
	g_menu_append_section (view, NULL, G_MENU_MODEL (view_opts));
	g_menu_append_submenu (bar, "_View", G_MENU_MODEL (view));

	/* Theme: presets and accents, both radio groups */
	for (i = 0; names[i] != NULL; i++)
	{
		g_autoptr(GMenuItem) item = g_menu_item_new (names[i], NULL);

		g_menu_item_set_action_and_target_value (item, "show.theme", g_variant_new_string (names[i]));
		g_menu_append_item (presets, item);
	}
	g_menu_append_section (theme, "Preset", G_MENU_MODEL (presets));
	for (i = 0; i < G_N_ELEMENTS (accents); i++)
	{
		g_autoptr(GMenuItem) item = g_menu_item_new (accent_names[i], NULL);

		g_menu_item_set_action_and_target_value (item, "show.accent", g_variant_new_string (accents[i]));
		g_menu_append_item (accent, item);
	}
	g_menu_append_section (theme, "Accent", G_MENU_MODEL (accent));
	g_menu_append_submenu (bar, "_Theme", G_MENU_MODEL (theme));

	/* Motion */
	for (i = 0; i < G_N_ELEMENTS (motions); i++)
	{
		g_autoptr(GMenuItem) item = g_menu_item_new (motions[i].name, NULL);

		g_menu_item_set_action_and_target_value (item, "show.motion", g_variant_new_string (motions[i].name));
		g_menu_append_item (motion, item);
	}
	g_menu_append_submenu (bar, "_Motion", G_MENU_MODEL (motion));

	/* Help */
	append_item (help, "_Keyboard shortcuts", "show.shortcuts", "search", NULL);
	append_item (help, "_About", "show.about", "info", NULL);
	g_menu_append_submenu (bar, "_Help", G_MENU_MODEL (help));

	return G_MENU_MODEL (bar);
}

/* install_actions: the "show" group, the shortcuts, and app.quit */
static void
install_actions (Showcase *app)
{
	const GActionEntry entries[] = {
		{ "theme",      on_theme_action,      "s",  "'midnight'", NULL, { 0 } },
		{ "motion",     on_motion_action,     "s",  "'Smooth'",   NULL, { 0 } },
		{ "accent",     on_accent_action,     "s",  "''",         NULL, { 0 } },
		{ "page",       on_page_action,       "s",  "'controls'", NULL, { 0 } },
		{ "rtl",        on_rtl_action,        NULL, "false",      NULL, { 0 } },
		{ "pick",       on_pick_action,       NULL, "false",      NULL, { 0 } },
		{ "toasts",     on_toasts_action,     NULL, NULL,         NULL, { 0 } },
		{ "dialog",     on_dialog_action,     NULL, NULL,         NULL, { 0 } },
		{ "screenshot", on_screenshot_action, NULL, NULL,         NULL, { 0 } },
		{ "reload",     on_reload_action,     NULL, NULL,         NULL, { 0 } },
		{ "shortcuts",  on_shortcuts_action,  NULL, NULL,         NULL, { 0 } },
		{ "about",      on_about_action,      NULL, NULL,         NULL, { 0 } },
		{ "undo-delete", on_undo_delete_action, NULL, NULL,       NULL, { 0 } }
	};
	guint i;

	app->actions = g_simple_action_group_new ();
	g_action_map_add_action_entries (G_ACTION_MAP (app->actions), entries, G_N_ELEMENTS (entries), app);
	lrg_widget_insert_action_group (LRG_WIDGET (app->canvas), "show", G_ACTION_GROUP (app->actions));

	/* The theme radio starts on the theme that is actually active */
	g_simple_action_set_state (show_action (app, "theme"),
	                           g_variant_new_string (lrg_theme_get_name (lrg_theme_get_default ())));

	/* Shortcuts: the menus show them next to their items */
	lrg_canvas_add_shortcut (app->canvas, "<Ctrl>q", "app.quit", NULL);
	lrg_canvas_add_shortcut (app->canvas, "<Ctrl>d", "show.dialog", NULL);
	lrg_canvas_add_shortcut (app->canvas, "<Ctrl><Shift>s", "show.screenshot", NULL);
	lrg_canvas_add_shortcut (app->canvas, "F5", "show.reload", NULL);
	lrg_canvas_add_shortcut (app->canvas, "<Ctrl><Shift>r", "show.rtl", NULL);
	lrg_canvas_add_shortcut (app->canvas, "F1", "show.shortcuts", NULL);
	for (i = 0; i < 9 && i < N_PAGES; i++)
	{
		g_autofree gchar *accel = g_strdup_printf ("<Ctrl>%u", i + 1);
		g_autofree gchar *lower = g_ascii_strdown (page_names[i], -1);

		lrg_canvas_add_shortcut (app->canvas, accel, "show.page", g_variant_new_string (lower));
	}
}

/* ==========================================================================
 * Console commands (Editor page)
 * ========================================================================== */

static void
run_console_command (Showcase    *app,
                     const gchar *command)
{
	g_auto(GStrv) argv = g_strsplit (command, " ", 2);
	const gchar  *verb = argv[0];
	const gchar  *arg = argv[1] != NULL ? g_strstrip (argv[1]) : NULL;

	console_print (app, "> %s", command);
	if (g_strcmp0 (verb, "help") == 0)
	{
		console_print (app, "help                 this list");
		console_print (app, "echo TEXT            print TEXT");
		console_print (app, "theme NAME           switch theme (show.theme)");
		console_print (app, "page NAME            open a sidebar page (show.page)");
		console_print (app, "rtl                  toggle right-to-left (show.rtl)");
		console_print (app, "toast TEXT           raise a toast (ui.toast)");
		console_print (app, "clear                empty the log");
		console_print (app, "quit                 leave (app.quit)");
	}
	else if (g_strcmp0 (verb, "echo") == 0)
		console_print (app, "%s", arg != NULL ? arg : "");
	else if (g_strcmp0 (verb, "clear") == 0)
		lrg_text_editor_set_text (app->console_log, "");
	else if (g_strcmp0 (verb, "theme") == 0 && arg != NULL)
		g_action_group_activate_action (G_ACTION_GROUP (app->actions), "theme", g_variant_new_string (arg));
	else if (g_strcmp0 (verb, "page") == 0 && arg != NULL)
		g_action_group_activate_action (G_ACTION_GROUP (app->actions), "page", g_variant_new_string (arg));
	else if (g_strcmp0 (verb, "rtl") == 0)
		g_action_group_activate_action (G_ACTION_GROUP (app->actions), "rtl", NULL);
	else if (g_strcmp0 (verb, "toast") == 0 && arg != NULL)
		lrg_widget_activate_action (LRG_WIDGET (app->console_log), "ui.toast", g_variant_new_string (arg));
	else if (g_strcmp0 (verb, "quit") == 0)
		lrg_widget_activate_action (LRG_WIDGET (app->console_log), "app.quit", NULL);
	else
		console_print (app, "unknown command '%s' (try help)", verb);
}

static void
on_quit_requested (LrgCanvas *canvas,
                   gpointer   user_data)
{
	Showcase *app = user_data;

	app->quit = TRUE;
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

	/* Actions first: the menu bar and combo boxes route through them */
	install_actions (app);
	g_signal_connect (app->canvas, "quit-requested", G_CALLBACK (on_quit_requested), app);

	/* Header: the menu bar + theme, motion and accent pickers */
	lrg_container_set_spacing (LRG_CONTAINER (header), 10.0f);
	{
		g_autoptr(GMenuModel) model = build_menu_model ();

		LrgLabel             *spacer = label_with_class ("", NULL);

		app->menubar = lrg_menu_bar_new_from_model (model);
		lrg_widget_set_valign (LRG_WIDGET (app->menubar), LRG_ALIGN_CENTER);
		lrg_container_add_child (LRG_CONTAINER (header), LRG_WIDGET (app->menubar));
		lrg_widget_set_fill_weight (LRG_WIDGET (spacer), 1.0f);
		add (header, spacer);
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
	app->theme_combo = theme_combo;
	add (header, theme_combo);
	for (i = 0; i < G_N_ELEMENTS (motions); i++)
		lrg_combo_box_append (motion_combo, motions[i].name);
	lrg_combo_box_set_selected (motion_combo, 0);
	g_signal_connect (motion_combo, "changed", G_CALLBACK (on_motion_changed), app);
	app->motion_combo = motion_combo;
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
	app->page[PAGE_LAYOUT] = build_layout_page (app);
	app->page[PAGE_VIEWPORT] = build_viewport_page (app);
	app->page[PAGE_EDITOR] = build_editor_page (app);
	app->page[PAGE_GRAPH] = build_graph_page (app);
	app->page[PAGE_TIMELINE] = build_timeline_page (app);
	app->page[PAGE_LOOKS] = build_looks_page (app);
	app->page[PAGE_BLOCKS] = build_blocks_page (app);
	app->page[PAGE_DOCK] = build_dock_page (app);
	app->page[PAGE_FILES] = build_files_page (app);
	app->page[PAGE_MARKDOWN] = build_markdown_page (app);
	app->page[PAGE_DECLARATIVE] = build_declarative_page (app);
	app->page[PAGE_INSPECTOR] = build_inspector_page (app);
	for (i = 0; i < N_PAGES; i++)
	{
		gboolean fill = (i == PAGE_LIST || i == PAGE_DATA || i == PAGE_GLASS ||
		                 i == PAGE_MARKDOWN || i == PAGE_DECLARATIVE || i == PAGE_VIEWPORT ||
		                 i == PAGE_EDITOR || i == PAGE_GRAPH || i == PAGE_TIMELINE || i == PAGE_DOCK ||
		                 i == PAGE_LOOKS || i == PAGE_BLOCKS || i == PAGE_FILES);

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
	gboolean                  opt_rtl = FALSE;
	gint                      opt_menu = 0;
	gboolean                  opt_context = FALSE;
	g_autofree gchar         *opt_look = NULL;
	gboolean                  opt_run_blocks = FALSE;
	gboolean                  opt_real_window = FALSE;
	gboolean                  opt_in_app = FALSE;
	gboolean                  opt_notify = FALSE;
	g_autofree gchar         *opt_file_dialog = NULL;
	g_autoptr(LrgMenu)        context = NULL;
	Showcase                  app = { 0 };
	gint                      frame = 0;
	gboolean                  was_picking = FALSE;
	gboolean                  graph_framed = FALSE;
	gboolean                  looks_framed = FALSE;

	GOptionEntry entries[] = {
		{ "theme", 't', 0, G_OPTION_ARG_STRING, &opt_theme,
		  "Theme preset (midnight, daylight, catppuccin-mocha, catppuccin-latte, high-contrast)", "NAME" },
		{ "accent", 'a', 0, G_OPTION_ARG_STRING, &opt_accent, "Accent colour, e.g. '#22d3ee'", "HEX" },
		{ "motion", 'm', 0, G_OPTION_ARG_STRING, &opt_motion,
		  "Motion preset (smooth, snappy, bouncy, wobbly, none)", "NAME" },
		{ "page", 'p', 0, G_OPTION_ARG_STRING, &opt_page,
		  "Start page (controls, cards, feedback, list, data, pickers, glass, motion, layout, viewport, "
		  "editor, graph, timeline, looks, blocks, dock, files, markdown, declarative, inspector)", "NAME" },
		{ "look", 'l', 0, G_OPTION_ARG_STRING, &opt_look,
		  "Looks page: start with this canvas look (auto, modern, blueprint, blender, neon)", "NICK" },
		{ "run-blocks", 0, 0, G_OPTION_ARG_NONE, &opt_run_blocks, "Blocks page: run the script at start-up", NULL },
		{ "header-bar-window", 0, 0, G_OPTION_ARG_NONE, &opt_real_window,
		  "Cards page: the header bar's buttons and dragging drive the real window", NULL },
		{ "in-app-notifications", 0, 0, G_OPTION_ARG_NONE, &opt_in_app,
		  "Show notifications as in-app toasts, never on the desktop", NULL },
		{ "file-dialog", 0, 0, G_OPTION_ARG_STRING, &opt_file_dialog,
		  "Open a file dialog on frame 10 (open, open-multiple, save, select-folder)", "MODE" },
		{ "notify", 0, 0, G_OPTION_ARG_NONE, &opt_notify,
		  "Send the Feedback page's notification and the Undo toast at start-up", NULL },
		{ "toasts", 0, 0, G_OPTION_ARG_NONE, &opt_toasts, "Show a few toasts at start-up", NULL },
		{ "dialog", 0, 0, G_OPTION_ARG_NONE, &opt_dialog, "Open the dialog at start-up", NULL },
		{ "island", 0, 0, G_OPTION_ARG_NONE, &opt_island, "Expand the island (motion page) at start-up", NULL },
		{ "pick", 0, 0, G_OPTION_ARG_NONE, &opt_pick, "Start in inspector pick mode", NULL },
		{ "touch", 0, 0, G_OPTION_ARG_NONE, &opt_touch, "Poll touch points (touch screens)", NULL },
		{ "rtl", 0, 0, G_OPTION_ARG_NONE, &opt_rtl, "Start in a right-to-left layout", NULL },
		{ "menu", 0, 0, G_OPTION_ARG_INT, &opt_menu,
		  "Open menu N of the menu bar (1 = File) on frame 10, keyboard style", "N" },
		{ "context-menu", 0, 0, G_OPTION_ARG_NONE, &opt_context,
		  "Open the first card's context menu on frame 30 (cards page)", NULL },
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
		"  ui-showcase --page viewport          (drag the 3D scene to orbit)\n"
		"  ui-showcase --page timeline --theme daylight\n"
		"  ui-showcase --page dock --rtl\n"
		"  ui-showcase --page feedback --toasts --dialog --screenshot dialog.png --frames 120\n"
		"  ui-showcase --page looks --look neon\n"
		"  ui-showcase --page blocks --run-blocks\n"
		"  ui-showcase --page files            (LrgFileBrowser + the four LrgFileDialog modes)\n"
		"  ui-showcase --page files --file-dialog open-multiple\n"
		"  ui-showcase --page cards --header-bar-window\n"
		"  ui-showcase --page feedback --notify --in-app-notifications\n"
		"\n"
		"Keys: Tab/arrows move focus, Enter activates, Escape cancels, F12 toggles inspector pick mode,\n"
		"Ctrl+Q quits, Ctrl+1..9 open pages, Ctrl+Shift+R toggles right-to-left, F1 lists shortcuts.\n");
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
	app.real_window = opt_real_window;
	if (opt_in_app)
		lrg_notification_set_force_in_app (TRUE);
	apply_theme (&app, opt_theme != NULL ? opt_theme : "midnight");
	build_ui (&app);
	lrg_notification_set_default_toast_queue (app.toasts);

	if (opt_look != NULL)
	{
		g_autoptr(GEnumClass) klass = g_type_class_ref (LRG_TYPE_CANVAS_LOOK);
		GEnumValue           *value = g_enum_get_value_by_nick (klass, opt_look);

		if (value != NULL)
			lrg_combo_box_set_selected (app.look_combo, value->value);
		else
			g_printerr ("ui-showcase: unknown look '%s'\n", opt_look);
	}
	if (opt_run_blocks)
		blocks_run_first (&app);
	if (opt_notify)
	{
		on_delete_clicked (NULL, &app);
		on_notify_clicked (NULL, &app);
	}

	if (opt_motion != NULL)
		g_action_group_activate_action (G_ACTION_GROUP (app.actions), "motion", g_variant_new_string (opt_motion));
	if (opt_rtl)
		g_action_group_activate_action (G_ACTION_GROUP (app.actions), "rtl", NULL);
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

	while (!grl_window_should_close (window) && !app.quit)
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
		if (app.current_page == PAGE_LAYOUT)
			update_size_label (&app);
		if (app.current_page == PAGE_GRAPH && !graph_framed &&
		    lrg_widget_get_width (LRG_WIDGET (app.graph)) > 0.0f)
		{
			/* Framing needs the graph's size: once, after its first layout */
			lrg_node_graph_frame_all (app.graph, FALSE);
			graph_framed = TRUE;
		}
		if (app.current_page == PAGE_LOOKS && !looks_framed &&
		    lrg_widget_get_width (LRG_WIDGET (app.look_graph)) > 0.0f)
		{
			lrg_node_graph_frame_all (app.look_graph, FALSE);
			looks_framed = TRUE;
		}
		app.scene_time += GetFrameTime ();

		/* Hot reload: a stat() of the watched YAML per frame */
		lrg_ui_loader_poll (app.loader);

		/* GIO work: file browser listings, file dialog and notification
		 * D-Bus replies complete on the default main context */
		g_main_context_iteration (NULL, FALSE);

		if (opt_dialog && frame == 10)
			lrg_dialog_present (app.dialog, app.canvas);
		if (opt_file_dialog != NULL && frame == 10)
		{
			static const gchar *nicks[] = { "open", "open-multiple", "save", "select-folder" };
			static const gchar *whats[] = { "Open", "Open multiple", "Save", "Select folder" };
			guint i;

			for (i = 0; i < G_N_ELEMENTS (nicks); i++)
				if (g_strcmp0 (opt_file_dialog, nicks[i]) == 0)
					run_file_dialog (&app, whats[i], LRG_WIDGET (app.canvas));
		}
		if (opt_island && frame == 10)
			lrg_island_set_expanded (app.island, TRUE);
		if (opt_menu > 0 && frame == 10)
			lrg_menu_bar_open_menu (app.menubar, (guint) (opt_menu - 1), TRUE);
		if (opt_context && frame == 30 && app.cards->len > 0)
		{
			/* What the canvas does on a right click, done by hand */
			LrgWidget *first = g_ptr_array_index (app.cards, 0);

			context = lrg_menu_new_from_model (lrg_widget_get_context_menu (first));
			lrg_menu_popup_at_point (context, first, lrg_widget_get_world_x (first) + 120.0f,
			                         lrg_widget_get_world_y (first) + 60.0f);
		}

		grl_window_begin_drawing (window);
		lrg_canvas_render (app.canvas);

		/* File > Save screenshot: capture the frame just rendered */
		if (app.shot_request != NULL)
		{
			Image shot = LoadImageFromScreen ();

			if (ExportImage (shot, app.shot_request))
				show_toast (&app, "Screenshot saved", app.shot_request, LRG_TOAST_KIND_SUCCESS);
			UnloadImage (shot);
			g_clear_pointer (&app.shot_request, g_free);
		}

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
	if (app.cube_loaded)
		UnloadModel (app.cube);
	g_clear_object (&app.camera);
	g_clear_object (&app.actions);
	g_clear_object (&app.menubar);
	g_clear_object (&app.transition_combo);
	g_clear_object (&app.switcher);
	g_clear_object (&app.size_box);
	g_clear_object (&app.size_label);
	g_clear_object (&app.viewport);
	g_clear_object (&app.viewport_status);
	g_clear_object (&app.code);
	g_clear_object (&app.language_combo);
	g_clear_object (&app.console_log);
	g_clear_object (&app.console_input);
	g_clear_object (&app.graph);
	g_clear_object (&app.timeline);
	g_clear_object (&app.dock);
	g_free (app.shot_request);
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
	g_clear_pointer (&app.loops, g_array_unref);
	g_free (app.sprite_color);
	g_free (app.sprite_say);
	return 0;
}
