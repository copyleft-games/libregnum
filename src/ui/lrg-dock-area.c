/* lrg-dock-area.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgDockArea: a retained dock of titled panels.
 *
 * The arrangement is a binary tree of private DockNode structs (the
 * same shape as the immediate-mode LrgGuiDock): split nodes divide
 * their rectangle between two children by a ratio, with a divider of
 * "divider-size" units between them; leaves are tab stacks of
 * LrgDockPanel widgets. Every docked panel is a container child of
 * the area (so the canvas hit-tests and steps it) but only the active
 * tab of each stack is visible. The tab strips, dividers and drag
 * feedback are not widgets: the area paints them and hit-tests them
 * itself, so clicks on them target the area.
 *
 * Removing the last tab of a stack collapses it: its parent split is
 * replaced by the other side, so the tree never holds empty stacks
 * (except an empty root).
 *
 * Dragging a tab past a small threshold lifts it: a ghost chip with a
 * shadow trails the pointer on springs, a 5-way compass appears over
 * the hovered stack and four edge targets over the whole area, and a
 * translucent glass preview springs to the region the panel would
 * take. Dropping on a target rearranges the tree, dropping on a tab
 * strip inserts (or reorders) the tab, dropping anywhere else tears
 * the panel off into a floating frame in the canvas overlay layer,
 * and Escape cancels. The feedback is painted by a private
 * pointer-transparent overlay widget, so it is drawn above floating
 * frames too.
 *
 * Floating frames (the private LrgDockFrame) hold one panel under a
 * title bar: drag the title to move them (the same drop targets show,
 * so they can be docked back), the right / bottom edges and the corner
 * grip to resize them, double-click the title to dock them again.
 *
 * Layouts persist as YAML (lrg_dock_area_save_layout()) naming panels
 * by id; loading matches panels by id and skips unknown ones.
 */

#include "config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_UI

#include <math.h>
#include <string.h>
#include <gio/gio.h>
#include <yaml-glib.h>

#include "lrg-dock-area.h"
#include "lrg-canvas.h"
#include "../paint/lrg-paint-icons.h"
#include "../tween/lrg-spring.h"
#include "../lrg-log.h"

/* Pointer travel (units) before a pressed tab or title lifts */
#define DRAG_THRESHOLD (6.0f)

/* Two presses within this time (canvas clock) are a double click */
#define DOUBLE_CLICK_TIME (0.4)

/* Share of the area a panel docked against an area edge takes */
#define EDGE_SHARE (0.25f)

/* Drop compass: button size and the gap between buttons */
#define COMPASS_BUTTON (32.0f)
#define COMPASS_GAP (4.0f)

/* Distance of the area edge targets from the edges */
#define EDGE_MARGIN (8.0f)

/* Tab strip geometry */
#define TAB_MIN_W (56.0f)
#define TAB_MAX_W (220.0f)
#define TAB_GAP (2.0f)
#define STRIP_PAD (4.0f)
#define TAB_INSET (3.0f)
#define CLOSE_SIZE (16.0f)
#define ICON_SIZE (14.0f)
#define ICON_GAP (6.0f)
#define INDICATOR_THICKNESS (3.0f)

/* Tab strip overflow: tabs shrink no further than this (titles end in
 * an ellipsis); past it the strip scrolls and gets three buttons
 * (scroll back, scroll forward, tab list) of this width each */
#define TAB_READABLE_W (96.0f)
#define STRIP_BUTTON_W (22.0f)
#define STRIP_BUTTON_ICON (12.0f)
#define N_STRIP_BUTTONS (3)

/* Units one wheel notch scrolls an overflowing strip */
#define WHEEL_STEP (48.0f)

/* While a tab is dragged near the end of an overflowing strip's
 * visible region (or over its buttons) the strip scrolls this fast */
#define AUTOSCROLL_ZONE (28.0f)
#define AUTOSCROLL_SPEED (480.0f)

/* U+2026 HORIZONTAL ELLIPSIS, ending titles cut short */
#define ELLIPSIS "\xe2\x80\xa6"

/* Prefix of the area's private action group (the tab list menu) */
#define ACTION_PREFIX "lrg-dock"

/* Length of the grip pill in the middle of a divider */
#define GRIP_LENGTH (28.0f)

/* Floating frames: smallest size, default size, resize zones */
#define FRAME_MIN_W (140.0f)
#define FRAME_MIN_H (90.0f)
#define FRAME_DEFAULT_W (320.0f)
#define FRAME_DEFAULT_H (240.0f)
#define FRAME_GRIP (14.0f)
#define FRAME_EDGE (5.0f)

/* Deepest tree lrg_dock_area_load_layout() accepts */
#define MAX_DEPTH (64)

/* ==========================================================================
 * LrgDockPosition
 * ========================================================================== */

GType
lrg_dock_position_get_type (void)
{
	static gsize type_id = 0;

	if (g_once_init_enter (&type_id))
	{
		static const GEnumValue values[] = {
			{ LRG_DOCK_POSITION_LEFT, "LRG_DOCK_POSITION_LEFT", "left" },
			{ LRG_DOCK_POSITION_RIGHT, "LRG_DOCK_POSITION_RIGHT", "right" },
			{ LRG_DOCK_POSITION_TOP, "LRG_DOCK_POSITION_TOP", "top" },
			{ LRG_DOCK_POSITION_BOTTOM, "LRG_DOCK_POSITION_BOTTOM", "bottom" },
			{ LRG_DOCK_POSITION_CENTER, "LRG_DOCK_POSITION_CENTER", "center" },
			{ 0, NULL, NULL }
		};
		GType id = g_enum_register_static (g_intern_static_string ("LrgDockPosition"), values);

		g_once_init_leave (&type_id, id);
	}
	return (GType) type_id;
}

GType
lrg_dock_strip_button_get_type (void)
{
	static gsize type_id = 0;

	if (g_once_init_enter (&type_id))
	{
		static const GEnumValue values[] = {
			{ LRG_DOCK_STRIP_BUTTON_SCROLL_BACK, "LRG_DOCK_STRIP_BUTTON_SCROLL_BACK", "scroll-back" },
			{ LRG_DOCK_STRIP_BUTTON_SCROLL_FORWARD, "LRG_DOCK_STRIP_BUTTON_SCROLL_FORWARD", "scroll-forward" },
			{ LRG_DOCK_STRIP_BUTTON_LIST, "LRG_DOCK_STRIP_BUTTON_LIST", "list" },
			{ 0, NULL, NULL }
		};
		GType id = g_enum_register_static (g_intern_static_string ("LrgDockStripButton"), values);

		g_once_init_leave (&type_id, id);
	}
	return (GType) type_id;
}

/* ==========================================================================
 * Private types
 * ========================================================================== */

typedef struct _DockNode DockNode;

/*
 * DockNode:
 *
 * A split (is_split, two children a / b, ratio = a's share of the
 * space left after the divider) or a tab stack (panels, active tab and
 * the indicator springs). Rectangles are relative to the area and are
 * recomputed by every layout.
 */
struct _DockNode
{
	DockNode       *parent;
	gboolean        is_split;

	/* Split */
	LrgOrientation  orientation;    /* HORIZONTAL: a | b side by side */
	gfloat          ratio;
	DockNode       *a;
	DockNode       *b;
	LrgSpring       hover;          /* divider highlight */
	LrgSpring       press;
	gfloat          dx;             /* divider rectangle */
	gfloat          dy;
	gfloat          dw;
	gfloat          dh;

	/* Stack */
	GPtrArray      *panels;         /* LrgDockPanel*, not owned */
	guint           active;
	LrgSpring       ind_x;          /* indicator, relative to x, unscrolled */
	LrgSpring       ind_w;
	gboolean        ind_ready;
	LrgSpring       scroll;         /* strip scroll offset (logical units) */
	LrgSpring       button_hover[N_STRIP_BUTTONS];
	guint           reveal_active;  /* what the last reveal saw: active tab, */
	guint           reveal_len;     /* tab count and width; a change */
	gfloat          reveal_w;       /* scrolls the active tab into view */

	gfloat          x;
	gfloat          y;
	gfloat          w;
	gfloat          h;
};

typedef enum
{
	DROP_NONE,
	DROP_STACK,     /* a compass button of a stack */
	DROP_TABS,      /* a place in a tab strip */
	DROP_EDGE,      /* an edge of the whole area */
	DROP_EMPTY      /* the area has no panel at all */
} DropKind;

typedef struct
{
	DropKind         kind;
	DockNode        *stack;
	LrgDockPosition  position;
	gint             index;
	gfloat           x;             /* preview, relative to the area */
	gfloat           y;
	gfloat           w;
	gfloat           h;
} Drop;

/* Private widgets, defined below */
#define LRG_TYPE_DOCK_FRAME (lrg_dock_frame_get_type ())
G_DECLARE_FINAL_TYPE (LrgDockFrame, lrg_dock_frame, LRG, DOCK_FRAME, LrgContainer)

#define LRG_TYPE_DOCK_DRAG_LAYER (lrg_dock_drag_layer_get_type ())
G_DECLARE_FINAL_TYPE (LrgDockDragLayer, lrg_dock_drag_layer, LRG, DOCK_DRAG_LAYER, LrgWidget)

/* ==========================================================================
 * Instance structures
 * ========================================================================== */

struct _LrgDockPanel
{
	LrgContainer  parent_instance;

	gchar        *id;
	gchar        *title;
	gchar        *icon;
	gboolean      closable;
	LrgDockArea  *area;             /* back pointer, not owned */

	LrgSpring     tab_hover;        /* tab look, stepped by the area */
	LrgSpring     close_hover;
};

struct _LrgDockArea
{
	LrgContainer   parent_instance;

	DockNode      *root;
	GPtrArray     *frames;          /* LrgDockFrame*, owned */
	LrgDockPanel  *active_panel;    /* not owned */

	gfloat         divider_size;
	gfloat         tab_height;
	gfloat         min_panel_size;
	gboolean       allow_floating;
	gboolean       in_layout;

	/* Hover */
	LrgDockPanel  *hot_panel;
	gboolean       hot_close;
	DockNode      *hot_split;
	DockNode      *hot_button_stack; /* stack whose strip button is hovered */
	gint           hot_button;       /* LrgDockStripButton, -1 for none */

	/* Tab list menu (lrg_dock_area_popup_tab_list) */
	LrgMenu            *tab_menu;      /* owned, created on first use */
	GPtrArray          *menu_panels;   /* LrgDockPanel*, owned refs */
	GSimpleActionGroup *actions;       /* ACTION_PREFIX group */
	gboolean            swallow_list_press;

	/* Divider drag */
	DockNode      *drag_split;
	gfloat         split_grab;
	gfloat         split_start_ratio;
	DockNode      *click_split;
	gdouble        click_time;

	/* Tab presses */
	LrgDockPanel  *press_panel;
	gfloat         press_x;
	gfloat         press_y;
	LrgDockPanel  *close_press;
	LrgDockPanel  *middle_press;

	/* Drag and drop */
	gboolean       dragging;
	LrgDockPanel  *drag_panel;
	LrgDockFrame  *drag_frame;      /* a floating frame being moved */
	Drop           drop;
	DockNode      *hover_stack;     /* the stack showing the compass */
	gfloat         pointer_x;
	gfloat         pointer_y;
	LrgSpring      preview_cx;
	LrgSpring      preview_cy;
	LrgSpring      preview_w;
	LrgSpring      preview_h;
	LrgSpring      preview_shown;
	LrgSpring      compass_shown;
	LrgSpring      ghost_x;
	LrgSpring      ghost_y;
	LrgSpring      lift;
	LrgDockPanel  *ghost_panel;     /* owned while the ghost fades */
	LrgWidget     *layer;           /* owned; in the overlay while set */

	gdouble        clock;
};

G_DEFINE_FINAL_TYPE (LrgDockPanel, lrg_dock_panel, LRG_TYPE_CONTAINER)
G_DEFINE_FINAL_TYPE (LrgDockArea, lrg_dock_area, LRG_TYPE_CONTAINER)

/* Forward declarations */
static void tree_changed (LrgDockArea *self);
static void end_drag (LrgDockArea *self);
static void drag_cancel (LrgDockArea *self);
static void drag_update (LrgDockArea *self, gfloat px, gfloat py);
static void drag_finish (LrgDockArea *self, gfloat px, gfloat py);
static void begin_drag (LrgDockArea *self, LrgDockPanel *panel, LrgDockFrame *frame, gfloat px, gfloat py);
static void move_internal (LrgDockArea *self, LrgDockPanel *panel, DockNode *target,
                           LrgDockPosition position, gint index, gfloat share);
static gfloat strip_height (LrgDockArea *self);
static void paint_title (LrgWidget *widget, LrgPaintList *list, LrgDockPanel *panel,
                         gfloat x, gfloat y, gfloat w, gfloat h, const GrlColor *fg, gfloat reserve);
static LrgDockFrame *frame_new (LrgDockArea *area);
static void raise_frame (LrgDockArea *self, LrgDockFrame *frame);
static void set_active_panel (LrgDockArea *self, LrgDockPanel *panel);

/* ==========================================================================
 * LrgDockPanel
 * ========================================================================== */

enum
{
	PANEL_PROP_0,
	PANEL_PROP_ID,
	PANEL_PROP_TITLE,
	PANEL_PROP_ICON,
	PANEL_PROP_CLOSABLE,
	PANEL_PROP_CHILD,
	PANEL_N_PROPS
};

static GParamSpec *panel_properties[PANEL_N_PROPS];

/* The content fills the panel (honouring its margin and alignment) */
static void
lrg_dock_panel_layout_children (LrgContainer *container)
{
	LrgWidget *w = LRG_WIDGET (container);
	GList     *l;

	for (l = lrg_container_get_children (container); l != NULL; l = l->next)
		lrg_widget_place_in_slot (LRG_WIDGET (l->data), 0.0f, 0.0f,
		                          lrg_widget_get_width (w), lrg_widget_get_height (w));
}

/* The largest child's preferred size plus its margin */
static void
lrg_dock_panel_measure (LrgWidget *widget,
                        gfloat    *preferred_width,
                        gfloat    *preferred_height)
{
	GList  *l;
	gfloat  mw = 0.0f;
	gfloat  mh = 0.0f;

	for (l = lrg_container_get_children (LRG_CONTAINER (widget)); l != NULL; l = l->next)
	{
		LrgWidget *c = LRG_WIDGET (l->data);
		LrgInsets  m;
		gfloat     cw;
		gfloat     ch;

		if (!lrg_widget_get_visible (c))
			continue;
		lrg_widget_get_preferred_size (c, &cw, &ch);
		lrg_widget_get_margin (c, &m);
		mw = MAX (mw, cw + m.left + m.right);
		mh = MAX (mh, ch + m.top + m.bottom);
	}
	*preferred_width = mw;
	*preferred_height = mh;
}

static void
lrg_dock_panel_finalize (GObject *object)
{
	LrgDockPanel *self = LRG_DOCK_PANEL (object);

	g_free (self->id);
	g_free (self->title);
	g_free (self->icon);

	G_OBJECT_CLASS (lrg_dock_panel_parent_class)->finalize (object);
}

static void
lrg_dock_panel_get_property (GObject    *object,
                             guint       prop_id,
                             GValue     *value,
                             GParamSpec *pspec)
{
	LrgDockPanel *self = LRG_DOCK_PANEL (object);

	switch (prop_id)
	{
	case PANEL_PROP_ID:
		g_value_set_string (value, self->id);
		break;
	case PANEL_PROP_TITLE:
		g_value_set_string (value, self->title);
		break;
	case PANEL_PROP_ICON:
		g_value_set_string (value, self->icon);
		break;
	case PANEL_PROP_CLOSABLE:
		g_value_set_boolean (value, self->closable);
		break;
	case PANEL_PROP_CHILD:
		g_value_set_object (value, lrg_dock_panel_get_child (self));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_dock_panel_set_property (GObject      *object,
                             guint         prop_id,
                             const GValue *value,
                             GParamSpec   *pspec)
{
	LrgDockPanel *self = LRG_DOCK_PANEL (object);

	switch (prop_id)
	{
	case PANEL_PROP_ID:
		lrg_dock_panel_set_id (self, g_value_get_string (value));
		break;
	case PANEL_PROP_TITLE:
		lrg_dock_panel_set_title (self, g_value_get_string (value));
		break;
	case PANEL_PROP_ICON:
		lrg_dock_panel_set_icon (self, g_value_get_string (value));
		break;
	case PANEL_PROP_CLOSABLE:
		lrg_dock_panel_set_closable (self, g_value_get_boolean (value));
		break;
	case PANEL_PROP_CHILD:
		lrg_dock_panel_set_child (self, g_value_get_object (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_dock_panel_class_init (LrgDockPanelClass *klass)
{
	GObjectClass      *object_class = G_OBJECT_CLASS (klass);
	LrgWidgetClass    *widget_class = LRG_WIDGET_CLASS (klass);
	LrgContainerClass *container_class = LRG_CONTAINER_CLASS (klass);

	object_class->finalize = lrg_dock_panel_finalize;
	object_class->get_property = lrg_dock_panel_get_property;
	object_class->set_property = lrg_dock_panel_set_property;

	widget_class->measure = lrg_dock_panel_measure;
	container_class->layout_children = lrg_dock_panel_layout_children;

	/**
	 * LrgDockPanel:id:
	 *
	 * Stable identifier, unique within one dock area; layouts refer
	 * to panels by it.
	 */
	panel_properties[PANEL_PROP_ID] =
		g_param_spec_string ("id", "Id", "Stable identifier", NULL,
		                     G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgDockPanel:title:
	 *
	 * Text on the panel's tab and floating title bar.
	 */
	panel_properties[PANEL_PROP_TITLE] =
		g_param_spec_string ("title", "Title", "Tab text", NULL,
		                     G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgDockPanel:icon:
	 *
	 * Nick of a vector icon (lrg_paint_icon_from_name()) drawn before
	 * the title, or %NULL.
	 */
	panel_properties[PANEL_PROP_ICON] =
		g_param_spec_string ("icon", "Icon", "Vector icon nick", NULL,
		                     G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgDockPanel:closable:
	 *
	 * Whether the tab has a close button and closes on a middle click.
	 */
	panel_properties[PANEL_PROP_CLOSABLE] =
		g_param_spec_boolean ("closable", "Closable", "User can close the panel", TRUE,
		                      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgDockPanel:child:
	 *
	 * The content widget.
	 */
	panel_properties[PANEL_PROP_CHILD] =
		g_param_spec_object ("child", "Child", "Content widget", LRG_TYPE_WIDGET,
		                     G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	g_object_class_install_properties (object_class, PANEL_N_PROPS, panel_properties);
}

static void
lrg_dock_panel_init (LrgDockPanel *self)
{
	self->closable = TRUE;
	lrg_spring_init (&self->tab_hover, 0.0f, LRG_MOTION_SMOOTH);
	lrg_spring_init (&self->close_hover, 0.0f, LRG_MOTION_SNAPPY);
}

LrgDockPanel *
lrg_dock_panel_new (const gchar *id,
                    const gchar *title)
{
	return g_object_new (LRG_TYPE_DOCK_PANEL, "id", id, "title", title, NULL);
}

const gchar *
lrg_dock_panel_get_id (LrgDockPanel *self)
{
	g_return_val_if_fail (LRG_IS_DOCK_PANEL (self), NULL);
	return self->id;
}

void
lrg_dock_panel_set_id (LrgDockPanel *self,
                       const gchar  *id)
{
	g_return_if_fail (LRG_IS_DOCK_PANEL (self));

	if (g_strcmp0 (self->id, id) == 0)
		return;
	g_free (self->id);
	self->id = g_strdup (id);
	g_object_notify_by_pspec (G_OBJECT (self), panel_properties[PANEL_PROP_ID]);
}

const gchar *
lrg_dock_panel_get_title (LrgDockPanel *self)
{
	g_return_val_if_fail (LRG_IS_DOCK_PANEL (self), NULL);
	return self->title;
}

void
lrg_dock_panel_set_title (LrgDockPanel *self,
                          const gchar  *title)
{
	g_return_if_fail (LRG_IS_DOCK_PANEL (self));

	if (g_strcmp0 (self->title, title) == 0)
		return;
	g_free (self->title);
	self->title = g_strdup (title);
	g_object_notify_by_pspec (G_OBJECT (self), panel_properties[PANEL_PROP_TITLE]);
}

const gchar *
lrg_dock_panel_get_icon (LrgDockPanel *self)
{
	g_return_val_if_fail (LRG_IS_DOCK_PANEL (self), NULL);
	return self->icon;
}

void
lrg_dock_panel_set_icon (LrgDockPanel *self,
                         const gchar  *icon)
{
	g_return_if_fail (LRG_IS_DOCK_PANEL (self));

	if (g_strcmp0 (self->icon, icon) == 0)
		return;
	g_free (self->icon);
	self->icon = g_strdup (icon);
	g_object_notify_by_pspec (G_OBJECT (self), panel_properties[PANEL_PROP_ICON]);
}

gboolean
lrg_dock_panel_get_closable (LrgDockPanel *self)
{
	g_return_val_if_fail (LRG_IS_DOCK_PANEL (self), FALSE);
	return self->closable;
}

void
lrg_dock_panel_set_closable (LrgDockPanel *self,
                             gboolean      closable)
{
	g_return_if_fail (LRG_IS_DOCK_PANEL (self));

	closable = !!closable;
	if (self->closable == closable)
		return;
	self->closable = closable;
	g_object_notify_by_pspec (G_OBJECT (self), panel_properties[PANEL_PROP_CLOSABLE]);
}

LrgWidget *
lrg_dock_panel_get_child (LrgDockPanel *self)
{
	GList *children;

	g_return_val_if_fail (LRG_IS_DOCK_PANEL (self), NULL);

	children = lrg_container_get_children (LRG_CONTAINER (self));
	return children != NULL ? LRG_WIDGET (children->data) : NULL;
}

void
lrg_dock_panel_set_child (LrgDockPanel *self,
                          LrgWidget    *child)
{
	g_return_if_fail (LRG_IS_DOCK_PANEL (self));
	g_return_if_fail (child == NULL || LRG_IS_WIDGET (child));

	if (child != NULL && lrg_dock_panel_get_child (self) == child)
		return;

	lrg_container_remove_all (LRG_CONTAINER (self));
	if (child != NULL)
		lrg_container_add_child (LRG_CONTAINER (self), child);
	lrg_widget_queue_layout (LRG_WIDGET (self));
	g_object_notify_by_pspec (G_OBJECT (self), panel_properties[PANEL_PROP_CHILD]);
}

LrgDockArea *
lrg_dock_panel_get_dock_area (LrgDockPanel *self)
{
	g_return_val_if_fail (LRG_IS_DOCK_PANEL (self), NULL);
	return self->area;
}

/* ==========================================================================
 * Tree helpers
 * ========================================================================== */

static DockNode *
stack_new (LrgDockPanel *panel)
{
	DockNode *n = g_new0 (DockNode, 1);

	n->panels = g_ptr_array_new ();
	if (panel != NULL)
		g_ptr_array_add (n->panels, panel);
	lrg_spring_init (&n->ind_x, 0.0f, LRG_MOTION_BOUNCY);
	lrg_spring_init (&n->ind_w, 0.0f, LRG_MOTION_BOUNCY);
	lrg_spring_init (&n->scroll, 0.0f, LRG_MOTION_SMOOTH);
	lrg_spring_init (&n->button_hover[0], 0.0f, LRG_MOTION_SMOOTH);
	lrg_spring_init (&n->button_hover[1], 0.0f, LRG_MOTION_SMOOTH);
	lrg_spring_init (&n->button_hover[2], 0.0f, LRG_MOTION_SMOOTH);
	n->reveal_active = G_MAXUINT;
	return n;
}

static DockNode *
split_new (LrgOrientation orientation,
           gfloat         ratio,
           DockNode      *a,
           DockNode      *b)
{
	DockNode *n = g_new0 (DockNode, 1);

	n->is_split = TRUE;
	n->orientation = orientation;
	n->ratio = CLAMP (ratio, 0.0f, 1.0f);
	n->a = a;
	n->b = b;
	a->parent = n;
	b->parent = n;
	lrg_spring_init (&n->hover, 0.0f, LRG_MOTION_SMOOTH);
	lrg_spring_init (&n->press, 0.0f, LRG_MOTION_SNAPPY);
	return n;
}

/* node_free: one node only (children are unlinked first by callers) */
static void
node_free (DockNode *n)
{
	if (n == NULL)
		return;
	if (n->panels != NULL)
		g_ptr_array_unref (n->panels);
	g_free (n);
}

/* tree_free: a whole subtree (panels are not owned by the tree) */
static void
tree_free (DockNode *n)
{
	if (n == NULL)
		return;
	tree_free (n->a);
	tree_free (n->b);
	node_free (n);
}

/* find_stack: the stack holding @panel, and its index in it */
static DockNode *
find_stack (DockNode     *n,
            LrgDockPanel *panel,
            guint        *index)
{
	DockNode *found;

	if (n == NULL)
		return NULL;
	if (n->is_split)
	{
		found = find_stack (n->a, panel, index);
		return found != NULL ? found : find_stack (n->b, panel, index);
	}
	return g_ptr_array_find (n->panels, panel, index) ? n : NULL;
}

static DockNode *
first_stack (DockNode *n)
{
	while (n != NULL && n->is_split)
		n = n->a;
	return n;
}

static gboolean
node_in_tree (DockNode *tree,
              DockNode *n)
{
	if (tree == NULL)
		return FALSE;
	if (tree == n)
		return TRUE;
	return tree->is_split && (node_in_tree (tree->a, n) || node_in_tree (tree->b, n));
}

/* stack_active_panel: the visible tab of a stack */
static LrgDockPanel *
stack_active_panel (DockNode *s)
{
	if (s == NULL || s->panels->len == 0)
		return NULL;
	return g_ptr_array_index (s->panels, MIN (s->active, s->panels->len - 1));
}

/* replace_node: put @with where @old hangs (its parent's slot or the root) */
static void
replace_node (LrgDockArea *self,
              DockNode    *old,
              DockNode    *with)
{
	DockNode *p = old->parent;

	with->parent = p;
	if (p == NULL)
		self->root = with;
	else if (p->a == old)
		p->a = with;
	else
		p->b = with;
}

/* stack_insert: add @panel at @index (-1 appends) and make it active */
static void
stack_insert (DockNode     *s,
              LrgDockPanel *panel,
              gint          index)
{
	if (index < 0 || (guint) index > s->panels->len)
		index = (gint) s->panels->len;
	g_ptr_array_insert (s->panels, index, panel);
	s->active = (guint) index;
}

/*
 * collapse:
 *
 * Frees an empty stack. Its parent split is replaced by the other
 * side, which is relinked (not copied), so pointers to every other
 * node stay valid.
 */
static void
collapse (LrgDockArea *self,
          DockNode    *s)
{
	DockNode *split = s->parent;
	DockNode *sibling;

	if (split == NULL)
	{
		self->root = NULL;
		node_free (s);
		return;
	}

	sibling = split->a == s ? split->b : split->a;
	replace_node (self, split, sibling);
	split->a = NULL;
	split->b = NULL;
	node_free (split);
	node_free (s);
}

/* detach_from_stack: take @panel out of @s, collapsing @s when emptied */
static void
detach_from_stack (LrgDockArea  *self,
                   DockNode     *s,
                   LrgDockPanel *panel)
{
	guint i;

	if (!g_ptr_array_find (s->panels, panel, &i))
		return;
	g_ptr_array_remove_index (s->panels, i);

	/* Keep the same tab active when one before it went away */
	if (i < s->active)
		s->active--;
	if (s->panels->len > 0 && s->active >= s->panels->len)
		s->active = s->panels->len - 1;

	if (s->panels->len == 0)
		collapse (self, s);
}

/*
 * split_node:
 *
 * Replaces @target with a split of @target and @fresh on the side
 * @position says; @fresh takes @share of the space.
 */
static void
split_node (LrgDockArea     *self,
            DockNode        *target,
            DockNode        *fresh,
            LrgDockPosition  position,
            gfloat           share)
{
	gboolean        fresh_first = position == LRG_DOCK_POSITION_LEFT || position == LRG_DOCK_POSITION_TOP;
	LrgOrientation  o = (position == LRG_DOCK_POSITION_LEFT || position == LRG_DOCK_POSITION_RIGHT)
	                    ? LRG_ORIENTATION_HORIZONTAL : LRG_ORIENTATION_VERTICAL;
	DockNode       *parent = target->parent;
	DockNode       *split;

	share = CLAMP (share, 0.05f, 0.95f);
	split = fresh_first ? split_new (o, share, fresh, target)
	                    : split_new (o, 1.0f - share, target, fresh);

	/* split_new() reparented @target; hang the split where it was */
	split->parent = parent;
	if (parent == NULL)
		self->root = split;
	else if (parent->a == target)
		parent->a = split;
	else
		parent->b = split;
}

/*
 * insert_at:
 *
 * Puts a detached @panel into the tree. @target NULL means the whole
 * area: CENTER then picks the active panel's stack or the first one.
 */
static void
insert_at (LrgDockArea     *self,
           LrgDockPanel    *panel,
           DockNode        *target,
           LrgDockPosition  position,
           gint             index,
           gfloat           share)
{
	if (self->root == NULL)
	{
		self->root = stack_new (panel);
		return;
	}

	if (position == LRG_DOCK_POSITION_CENTER)
	{
		if (target == NULL && self->active_panel != NULL)
			target = find_stack (self->root, self->active_panel, NULL);
		if (target == NULL)
			target = first_stack (self->root);
		stack_insert (target, panel, index);
		return;
	}

	split_node (self, target != NULL ? target : self->root, stack_new (panel), position, share);
}

/* frame_of: the floating frame holding @panel */
static LrgDockFrame *frame_get_panel_frame (LrgDockArea *self, LrgDockPanel *panel);

/* canvas_of: the canvas the area is on, or NULL */
static LrgCanvas *
canvas_of (LrgDockArea *self)
{
	LrgWidget *root = lrg_widget_get_root (LRG_WIDGET (self));

	return LRG_IS_CANVAS (root) ? LRG_CANVAS (root) : NULL;
}

/* ==========================================================================
 * Geometry
 * ========================================================================== */

static LrgTheme *
theme_of (LrgDockArea *self)
{
	return lrg_widget_get_theme (LRG_WIDGET (self));
}

static gfloat
metric (LrgWidget        *widget,
        LrgThemeMetric    m)
{
	return lrg_theme_get_metric (lrg_widget_get_theme (widget), m);
}

/* strip_height: the tab strip (and floating title bar) height */
static gfloat
strip_height (LrgDockArea *self)
{
	if (self->tab_height > 0.0f)
		return self->tab_height;
	return roundf (MAX (24.0f, metric (LRG_WIDGET (self), LRG_THEME_METRIC_CONTROL_HEIGHT)));
}

static gfloat
border_width (LrgWidget *widget)
{
	return MAX (0.0f, metric (widget, LRG_THEME_METRIC_BORDER_WIDTH));
}

/* paint_close_glyph: the cross of a close button whose box is at (x, y) */
static void
paint_close_glyph (LrgPaintList   *list,
                   gfloat          x,
                   gfloat          y,
                   const GrlColor *color)
{
	gfloat s = roundf (CLOSE_SIZE * 0.7f);

	lrg_paint_list_icon (list, LRG_ICON_CROSS, x + (CLOSE_SIZE - s) * 0.5f, y + (CLOSE_SIZE - s) * 0.5f,
	                     s, 1.5f, color);
}

/*
 * is_rtl:
 *
 * Tab strips start from the right in right-to-left layouts (the tree
 * itself is physical: LEFT stays left).
 */
static gboolean
is_rtl (LrgDockArea *self)
{
	return lrg_widget_is_rtl (LRG_WIDGET (self));
}

static LrgIcon
panel_icon (LrgDockPanel *panel)
{
	if (panel->icon == NULL || panel->icon[0] == '\0')
		return LRG_ICON_NONE;
	return lrg_paint_icon_from_name (panel->icon);
}

/* tab_natural_width: padding, icon, title and close button, clamped */
static gfloat
tab_natural_width (LrgDockArea  *self,
                   LrgDockPanel *panel)
{
	LrgWidget *w = LRG_WIDGET (self);
	gfloat     pad = metric (w, LRG_THEME_METRIC_PADDING_X);
	gfloat     tw = 0.0f;
	gfloat     th = 0.0f;
	gfloat     total;

	lrg_paint_measure_text (lrg_theme_get_ui_font (theme_of (self)),
	                        panel->title != NULL ? panel->title : "",
	                        metric (w, LRG_THEME_METRIC_FONT_SIZE), 1.0f, &tw, &th);
	total = pad * 2.0f + tw;
	if (panel_icon (panel) != LRG_ICON_NONE)
		total += ICON_SIZE + ICON_GAP;
	if (panel->closable)
		total += CLOSE_SIZE + 4.0f;
	return roundf (CLAMP (total, TAB_MIN_W, TAB_MAX_W));
}

static DockNode *stack_at (DockNode *n, gfloat px, gfloat py);

/*
 * StripInfo:
 *
 * How a stack's tab strip is laid out (stack_tabs_info()): the visible
 * tab region (physical, relative to the area), the logical length of
 * the tab row and how far it can scroll, and, when it overflows, the
 * left edges of the three strip buttons (indexed by LrgDockStripButton).
 */
typedef struct
{
	gboolean overflow;
	gfloat   view_x;
	gfloat   view_w;
	gfloat   content_w;
	gfloat   max_scroll;
	gfloat   scroll;
	gfloat   button_x[N_STRIP_BUTTONS];
} StripInfo;

/*
 * stack_tabs_info:
 * @xs: (out): left edges, relative to the area
 * @ws: (out): widths
 * @info: (out) (optional): the strip geometry
 *
 * Lays the tabs of @s out along its strip, from the left (from the
 * right in RTL), in three regimes:
 *
 * 1. Everything fits at natural width: natural widths.
 * 2. It fits once tabs shrink to their readable minimum
 *    (MIN (natural, TAB_READABLE_W)): every tab gives up the same
 *    fraction of what it has above that minimum, so short titles keep
 *    their width and long ones end in an ellipsis.
 * 3. Otherwise the strip overflows: tabs keep the readable minimum,
 *    the three strip buttons take the trailing end, and the tab row
 *    scrolls inside the remaining region by the stack's scroll spring
 *    (clamped, so a bouncy overshoot never shows a gap).
 *
 * Returns: the number of tabs
 */
static guint
stack_tabs_info (LrgDockArea *self,
                 DockNode    *s,
                 gfloat      *xs,
                 gfloat      *ws,
                 StripInfo   *info)
{
	StripInfo local;
	guint     n = s->panels->len;
	gboolean  rtl = is_rtl (self);
	gfloat    gaps = TAB_GAP * (gfloat) (n > 0 ? n - 1 : 0);
	gfloat    avail = MAX (0.0f, s->w - STRIP_PAD * 2.0f);
	gfloat    total = 0.0f;
	gfloat    total_min = 0.0f;
	gfloat    cursor = 0.0f;
	guint     i;

	if (info == NULL)
		info = &local;
	memset (info, 0, sizeof (StripInfo));
	info->view_x = s->x + STRIP_PAD;
	info->view_w = avail;

	/* Natural widths and their readable minimums */
	for (i = 0; i < n; i++)
	{
		ws[i] = tab_natural_width (self, g_ptr_array_index (s->panels, i));
		total += ws[i];
		total_min += MIN (ws[i], TAB_READABLE_W);
	}

	if (total + gaps > avail && total > 0.0f)
	{
		if (total_min + gaps <= avail)
		{
			/* 2. Shrink towards the readable minimum, in proportion
			 * to what each tab has above it */
			gfloat extra = total - total_min;
			gfloat f = extra > 0.0f ? (avail - gaps - total_min) / extra : 0.0f;

			for (i = 0; i < n; i++)
			{
				gfloat mn = MIN (ws[i], TAB_READABLE_W);

				ws[i] = floorf (mn + (ws[i] - mn) * f);
			}
		}
		else
		{
			/* 3. Overflow: buttons at the trailing end, tabs scroll */
			gfloat reserve = STRIP_BUTTON_W * (gfloat) N_STRIP_BUTTONS + STRIP_PAD;

			info->overflow = TRUE;
			info->view_w = MAX (0.0f, avail - reserve);
			if (rtl)
				info->view_x = s->x + STRIP_PAD + reserve;
			for (i = 0; i < N_STRIP_BUTTONS; i++)
			{
				if (rtl)
					info->button_x[i] = s->x + STRIP_PAD + STRIP_BUTTON_W * (gfloat) (N_STRIP_BUTTONS - 1 - i);
				else
					info->button_x[i] = s->x + s->w - STRIP_PAD - STRIP_BUTTON_W * (gfloat) (N_STRIP_BUTTONS - i);
			}
			for (i = 0; i < n; i++)
				ws[i] = MIN (ws[i], TAB_READABLE_W);
		}
	}

	/* Row length, scroll range and the offset drawn */
	for (i = 0; i < n; i++)
		info->content_w += ws[i];
	info->content_w += gaps;
	if (info->overflow)
	{
		info->max_scroll = MAX (0.0f, info->content_w - info->view_w);
		info->scroll = CLAMP (s->scroll.value, 0.0f, info->max_scroll);
	}

	for (i = 0; i < n; i++)
	{
		if (rtl)
			xs[i] = info->view_x + info->view_w - cursor - ws[i] + info->scroll;
		else
			xs[i] = info->view_x + cursor - info->scroll;
		cursor += ws[i] + TAB_GAP;
	}
	return n;
}

/* stack_tabs: stack_tabs_info() without the strip geometry */
static guint
stack_tabs (LrgDockArea *self,
            DockNode    *s,
            gfloat      *xs,
            gfloat      *ws)
{
	return stack_tabs_info (self, s, xs, ws, NULL);
}

/*
 * tab_logical_left:
 *
 * Where tab @index starts along the row, from the row's start (the
 * left in LTR, the right in RTL), ignoring the scroll offset.
 */
static gfloat
tab_logical_left (const gfloat *ws,
                  guint         index)
{
	gfloat x = 0.0f;
	guint  i;

	for (i = 0; i < index; i++)
		x += ws[i] + TAB_GAP;
	return x;
}

/* strip_info: the strip geometry of @s alone */
static void
strip_info (LrgDockArea *self,
            DockNode    *s,
            StripInfo   *info)
{
	g_autofree gfloat *xs = g_new (gfloat, MAX (1u, s->panels->len));
	g_autofree gfloat *ws = g_new (gfloat, MAX (1u, s->panels->len));

	stack_tabs_info (self, s, xs, ws, info);
}

/*
 * stack_scroll_to:
 * @snap: jump there instead of gliding
 *
 * Sets the strip's scroll target to @offset, clamped to its range (0
 * when the strip does not overflow).
 */
static void
stack_scroll_to (LrgDockArea *self,
                 DockNode    *s,
                 gfloat       offset,
                 gboolean     snap)
{
	StripInfo info;

	strip_info (self, s, &info);
	offset = info.overflow ? CLAMP (offset, 0.0f, info.max_scroll) : 0.0f;
	if (snap || lrg_widget_get_motion (LRG_WIDGET (self)) == LRG_MOTION_NONE)
		lrg_spring_snap (&s->scroll, offset);
	else
	{
		/* Start from the drawn (clamped) offset, not an overshoot */
		if (s->scroll.value < 0.0f || s->scroll.value > info.max_scroll)
			lrg_spring_snap (&s->scroll, CLAMP (s->scroll.value, 0.0f, info.max_scroll));
		lrg_spring_set_target (&s->scroll, offset);
	}
}

/*
 * stack_reveal:
 *
 * Scrolls the strip the least distance that shows tab @index in full
 * (the start of the tab wins when the region is narrower than it).
 */
static void
stack_reveal (LrgDockArea *self,
              DockNode    *s,
              guint        index,
              gboolean     snap)
{
	g_autofree gfloat *xs = NULL;
	g_autofree gfloat *ws = NULL;
	StripInfo          info;
	gfloat             target;
	gfloat             left;
	gfloat             right;

	if (index >= s->panels->len)
		return;
	xs = g_new (gfloat, s->panels->len);
	ws = g_new (gfloat, s->panels->len);
	stack_tabs_info (self, s, xs, ws, &info);
	if (!info.overflow)
	{
		stack_scroll_to (self, s, 0.0f, TRUE);
		return;
	}

	target = s->scroll.target;
	left = tab_logical_left (ws, index);
	right = left + ws[index];
	if (right > target + info.view_w)
		target = right - info.view_w;
	if (left < target)
		target = left;
	stack_scroll_to (self, s, target, snap);
}

/*
 * strip_button_at:
 * @stack: (out): the stack whose strip has the button
 *
 * Returns: the LrgDockStripButton under the point, or -1
 */
static gint
strip_button_at (LrgDockArea  *self,
                 gfloat        px,
                 gfloat        py,
                 DockNode    **stack)
{
	DockNode  *s = stack_at (self->root, px, py);
	StripInfo  info;
	gint       i;

	*stack = NULL;
	if (s == NULL || py >= s->y + strip_height (self) || s->panels->len == 0)
		return -1;
	strip_info (self, s, &info);
	if (!info.overflow)
		return -1;
	for (i = 0; i < N_STRIP_BUTTONS; i++)
	{
		if (px >= info.button_x[i] && px < info.button_x[i] + STRIP_BUTTON_W)
		{
			*stack = s;
			return i;
		}
	}
	return -1;
}

/*
 * stack_step_scroll:
 * @forward: towards the last tab
 *
 * What the scroll arrows do: line the next tab boundary up with the
 * edge of the visible region (one tab per click).
 */
static void
stack_step_scroll (LrgDockArea *self,
                   DockNode    *s,
                   gboolean     forward)
{
	g_autofree gfloat *xs = g_new (gfloat, MAX (1u, s->panels->len));
	g_autofree gfloat *ws = g_new (gfloat, MAX (1u, s->panels->len));
	StripInfo          info;
	gfloat             current;
	gfloat             target;
	guint              n;
	guint              i;

	n = stack_tabs_info (self, s, xs, ws, &info);
	if (!info.overflow)
		return;
	current = CLAMP (s->scroll.target, 0.0f, info.max_scroll);
	target = forward ? info.max_scroll : 0.0f;
	for (i = 0; i < n; i++)
	{
		gfloat left = tab_logical_left (ws, i);
		gfloat right = left + ws[i];

		if (forward && right > current + info.view_w + 0.5f)
		{
			target = right - info.view_w;
			break;
		}
		if (!forward && left < current - 0.5f)
			target = left;
	}
	stack_scroll_to (self, s, target, FALSE);
}

/* close_rect: the close button of a tab (relative to the area) */
static void
close_rect (LrgDockArea *self,
            gfloat       tx,
            gfloat       ty,
            gfloat       tw,
            gfloat       th,
            gfloat      *cx,
            gfloat      *cy)
{
	gfloat pad = metric (LRG_WIDGET (self), LRG_THEME_METRIC_PADDING_X);

	*cx = is_rtl (self) ? tx + pad * 0.5f : tx + tw - pad * 0.5f - CLOSE_SIZE;
	*cy = ty + (th - CLOSE_SIZE) * 0.5f;
}

/* node_min: the smallest size a subtree can be laid out in */
static void
node_min (LrgDockArea *self,
          DockNode    *n,
          gfloat      *mw,
          gfloat      *mh)
{
	gfloat aw;
	gfloat ah;
	gfloat bw;
	gfloat bh;
	guint  i;

	if (!n->is_split)
	{
		gfloat w = self->min_panel_size;
		gfloat h = self->min_panel_size;

		for (i = 0; i < n->panels->len; i++)
		{
			gfloat pw;
			gfloat ph;

			lrg_widget_get_min_size (g_ptr_array_index (n->panels, i), &pw, &ph);
			w = MAX (w, pw);
			h = MAX (h, ph);
		}
		*mw = w;
		*mh = h + strip_height (self);
		return;
	}

	node_min (self, n->a, &aw, &ah);
	node_min (self, n->b, &bw, &bh);
	if (n->orientation == LRG_ORIENTATION_HORIZONTAL)
	{
		*mw = aw + self->divider_size + bw;
		*mh = MAX (ah, bh);
	}
	else
	{
		*mw = MAX (aw, bw);
		*mh = ah + self->divider_size + bh;
	}
}

/*
 * split_first:
 *
 * The size of a split's first child for @first wanted units: clamped
 * so both sides keep their minimum (the first side winning when the
 * space cannot hold both).
 */
static gfloat
split_first (LrgDockArea *self,
             DockNode    *n,
             gfloat       first)
{
	gfloat aw;
	gfloat ah;
	gfloat bw;
	gfloat bh;
	gfloat len = n->orientation == LRG_ORIENTATION_HORIZONTAL ? n->w : n->h;
	gfloat avail = MAX (0.0f, len - self->divider_size);
	gfloat lo;
	gfloat hi;

	node_min (self, n->a, &aw, &ah);
	node_min (self, n->b, &bw, &bh);
	if (n->orientation == LRG_ORIENTATION_HORIZONTAL)
	{
		lo = MIN (aw, avail);
		hi = MAX (lo, avail - bw);
	}
	else
	{
		lo = MIN (ah, avail);
		hi = MAX (lo, avail - bh);
	}
	return roundf (CLAMP (first, lo, hi));
}

/* layout_node: assign rectangles (relative to the area) to a subtree */
static void
layout_node (LrgDockArea *self,
             DockNode    *n,
             gfloat       x,
             gfloat       y,
             gfloat       w,
             gfloat       h)
{
	gfloat d = self->divider_size;
	gfloat first;

	n->x = x;
	n->y = y;
	n->w = MAX (0.0f, w);
	n->h = MAX (0.0f, h);
	if (!n->is_split)
		return;

	if (n->orientation == LRG_ORIENTATION_HORIZONTAL)
	{
		first = split_first (self, n, n->ratio * MAX (0.0f, n->w - d));
		layout_node (self, n->a, x, y, first, n->h);
		n->dx = x + first;
		n->dy = y;
		n->dw = d;
		n->dh = n->h;
		layout_node (self, n->b, x + first + d, y, MAX (0.0f, n->w - first - d), n->h);
	}
	else
	{
		first = split_first (self, n, n->ratio * MAX (0.0f, n->h - d));
		layout_node (self, n->a, x, y, n->w, first);
		n->dx = x;
		n->dy = y + first;
		n->dw = n->w;
		n->dh = d;
		layout_node (self, n->b, x, y + first + d, n->w, MAX (0.0f, n->h - first - d));
	}
}

/* place_panels: allocate every tab's content rect, show active tabs only */
static void
place_panels (LrgDockArea *self,
              DockNode    *n)
{
	gfloat bw;
	gfloat strip;
	guint  i;

	if (n == NULL)
		return;
	if (n->is_split)
	{
		place_panels (self, n->a);
		place_panels (self, n->b);
		return;
	}

	bw = border_width (LRG_WIDGET (self));
	strip = strip_height (self);
	for (i = 0; i < n->panels->len; i++)
	{
		LrgWidget *p = g_ptr_array_index (n->panels, i);
		gboolean   show = i == n->active;

		lrg_widget_allocate (p, n->x + bw, n->y + strip,
		                     MAX (0.0f, n->w - bw * 2.0f), MAX (0.0f, n->h - strip - bw));
		if (lrg_widget_get_visible (p) != show)
			lrg_widget_set_visible (p, show);
	}
}

/* ==========================================================================
 * Hit testing (coordinates relative to the area)
 * ========================================================================== */

static gboolean
in_rect (gfloat px,
         gfloat py,
         gfloat x,
         gfloat y,
         gfloat w,
         gfloat h)
{
	return px >= x && py >= y && px < x + w && py < y + h;
}

static DockNode *
split_at (DockNode *n,
          gfloat    px,
          gfloat    py)
{
	DockNode *found;

	if (n == NULL || !n->is_split)
		return NULL;
	if (in_rect (px, py, n->dx, n->dy, n->dw, n->dh))
		return n;
	found = split_at (n->a, px, py);
	return found != NULL ? found : split_at (n->b, px, py);
}

static DockNode *
stack_at (DockNode *n,
          gfloat    px,
          gfloat    py)
{
	DockNode *found;

	if (n == NULL)
		return NULL;
	if (n->is_split)
	{
		found = stack_at (n->a, px, py);
		return found != NULL ? found : stack_at (n->b, px, py);
	}
	return in_rect (px, py, n->x, n->y, n->w, n->h) ? n : NULL;
}

/*
 * tab_at:
 * @on_close: (out): whether the point is on the tab's close button
 *
 * Returns: (transfer none) (nullable): the panel whose tab is at the
 *   point
 */
static LrgDockPanel *
tab_at (LrgDockArea *self,
        gfloat       px,
        gfloat       py,
        gboolean    *on_close)
{
	DockNode         *s = stack_at (self->root, px, py);
	gfloat            strip = strip_height (self);
	g_autofree gfloat *xs = NULL;
	g_autofree gfloat *ws = NULL;
	StripInfo         info;
	guint             n;
	guint             i;

	*on_close = FALSE;
	if (s == NULL || py >= s->y + strip || s->panels->len == 0)
		return NULL;

	xs = g_new (gfloat, s->panels->len);
	ws = g_new (gfloat, s->panels->len);
	n = stack_tabs_info (self, s, xs, ws, &info);

	/* Tabs scrolled out of an overflowing strip, and its buttons, are
	 * not hit */
	if (info.overflow && (px < info.view_x || px >= info.view_x + info.view_w))
		return NULL;
	for (i = 0; i < n; i++)
	{
		LrgDockPanel *p = g_ptr_array_index (s->panels, i);

		if (px >= xs[i] && px < xs[i] + ws[i])
		{
			if (p->closable)
			{
				gfloat cx;
				gfloat cy;

				close_rect (self, xs[i], s->y + TAB_INSET, ws[i], strip - TAB_INSET * 2.0f, &cx, &cy);
				*on_close = in_rect (px, py, cx - 2.0f, cy - 2.0f, CLOSE_SIZE + 4.0f, CLOSE_SIZE + 4.0f);
			}
			return p;
		}
	}
	return NULL;
}

/* ==========================================================================
 * Active panel and tree changes
 * ========================================================================== */

enum
{
	PROP_0,
	PROP_DIVIDER_SIZE,
	PROP_TAB_HEIGHT,
	PROP_MIN_PANEL_SIZE,
	PROP_ALLOW_FLOATING,
	PROP_ACTIVE_PANEL,
	N_PROPS
};

enum
{
	SIGNAL_CLOSE_REQUEST,
	SIGNAL_PANEL_CLOSED,
	SIGNAL_PANEL_ACTIVATED,
	SIGNAL_LAYOUT_CHANGED,
	N_SIGNALS
};

static GParamSpec *properties[N_PROPS];
static guint       signals[N_SIGNALS];

/* set_active_panel: record the area's active panel, emitting on change */
static void
set_active_panel (LrgDockArea  *self,
                  LrgDockPanel *panel)
{
	if (self->active_panel == panel)
		return;
	self->active_panel = panel;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ACTIVE_PANEL]);
	if (panel != NULL)
		g_signal_emit (self, signals[SIGNAL_PANEL_ACTIVATED], 0, panel);
}

/*
 * tree_changed:
 *
 * After every rearrangement: forget node pointers held for hover and
 * divider drags (nodes may have been freed), lay out at once and tell
 * listeners.
 */
static void
tree_changed (LrgDockArea *self)
{
	self->hot_split = NULL;
	self->hot_button_stack = NULL;
	self->hot_button = -1;
	self->drag_split = NULL;
	self->click_split = NULL;
	self->hover_stack = NULL;
	self->drop.stack = NULL;
	self->drop.kind = DROP_NONE;

	lrg_widget_queue_layout (LRG_WIDGET (self));
	if (!self->in_layout)
		lrg_container_layout_children (LRG_CONTAINER (self));
	g_signal_emit (self, signals[SIGNAL_LAYOUT_CHANGED], 0);
}

/* forget_panel: the panel left the area; drop every reference to it */
static void
forget_panel (LrgDockArea  *self,
              LrgDockPanel *panel)
{
	panel->area = NULL;
	if (self->hot_panel == panel)
		self->hot_panel = NULL;
	if (self->press_panel == panel)
		self->press_panel = NULL;
	if (self->close_press == panel)
		self->close_press = NULL;
	if (self->middle_press == panel)
		self->middle_press = NULL;
	if (self->active_panel == panel)
	{
		self->active_panel = NULL;
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ACTIVE_PANEL]);
	}
}

/* ==========================================================================
 * Floating frames (private widget)
 * ========================================================================== */

typedef enum
{
	FRAME_IDLE,
	FRAME_MOVE,
	FRAME_RESIZE,
	FRAME_CLOSE
} FrameMode;

typedef enum
{
	FRAME_HOT_NONE,
	FRAME_HOT_TITLE,
	FRAME_HOT_CLOSE,
	FRAME_HOT_RESIZE
} FrameHot;

#define EDGE_RIGHT (1u << 0)
#define EDGE_BOTTOM (1u << 1)

struct _LrgDockFrame
{
	LrgContainer  parent_instance;

	LrgDockArea  *area;             /* not owned: the area owns frames */
	LrgDockPanel *panel;            /* the child */

	FrameMode     mode;
	FrameHot      hot;
	guint         edges;
	gboolean      moved;
	gfloat        press_px;
	gfloat        press_py;
	gfloat        start_x;
	gfloat        start_y;
	gfloat        start_w;
	gfloat        start_h;
	LrgSpring     close_hover;

	gdouble       clock;
	gboolean      click_armed;
	gdouble       click_time;
};

G_DEFINE_FINAL_TYPE (LrgDockFrame, lrg_dock_frame, LRG_TYPE_CONTAINER)

static gfloat
frame_title_height (LrgDockFrame *self)
{
	return self->area != NULL ? strip_height (self->area) : 28.0f;
}

/* frame_close_rect: close button of the title bar (world coordinates) */
static gboolean
frame_close_rect (LrgDockFrame *self,
                  gfloat       *cx,
                  gfloat       *cy)
{
	LrgWidget *w = LRG_WIDGET (self);
	gfloat     pad = metric (w, LRG_THEME_METRIC_PADDING_X);
	gfloat     th = frame_title_height (self);

	if (self->panel == NULL || !self->panel->closable)
		return FALSE;
	if (lrg_widget_is_rtl (w))
		*cx = lrg_widget_get_world_x (w) + pad * 0.5f;
	else
		*cx = lrg_widget_get_world_x (w) + lrg_widget_get_width (w) - pad * 0.5f - CLOSE_SIZE;
	*cy = lrg_widget_get_world_y (w) + (th - CLOSE_SIZE) * 0.5f;
	return TRUE;
}

/* frame_hit: which zone of the frame a world point is on */
static FrameHot
frame_hit (LrgDockFrame *self,
           gfloat        px,
           gfloat        py,
           guint        *edges)
{
	LrgWidget *w = LRG_WIDGET (self);
	gfloat     x = lrg_widget_get_world_x (w);
	gfloat     y = lrg_widget_get_world_y (w);
	gfloat     fw = lrg_widget_get_width (w);
	gfloat     fh = lrg_widget_get_height (w);
	gfloat     cx;
	gfloat     cy;

	*edges = 0;
	if (!in_rect (px, py, x, y, fw, fh))
		return FRAME_HOT_NONE;

	/* Corner grip, then the right and bottom edges */
	if (px >= x + fw - FRAME_GRIP && py >= y + fh - FRAME_GRIP)
		*edges = EDGE_RIGHT | EDGE_BOTTOM;
	else if (px >= x + fw - FRAME_EDGE)
		*edges = EDGE_RIGHT;
	else if (py >= y + fh - FRAME_EDGE)
		*edges = EDGE_BOTTOM;
	if (*edges != 0)
		return FRAME_HOT_RESIZE;

	if (py < y + frame_title_height (self))
	{
		if (frame_close_rect (self, &cx, &cy) &&
		    in_rect (px, py, cx - 2.0f, cy - 2.0f, CLOSE_SIZE + 4.0f, CLOSE_SIZE + 4.0f))
			return FRAME_HOT_CLOSE;
		return FRAME_HOT_TITLE;
	}
	return FRAME_HOT_NONE;
}

static void
lrg_dock_frame_layout_children (LrgContainer *container)
{
	LrgDockFrame *self = LRG_DOCK_FRAME (container);
	LrgWidget    *w = LRG_WIDGET (self);
	gfloat        bw = border_width (w);
	gfloat        th = frame_title_height (self);

	/* The right and bottom edges stay uncovered: they are the frame's
	 * own resize zones (the panel would take the hits otherwise) */
	if (self->panel == NULL)
		return;
	lrg_widget_allocate (LRG_WIDGET (self->panel), bw, th,
	                     MAX (0.0f, lrg_widget_get_width (w) - bw - FRAME_EDGE),
	                     MAX (0.0f, lrg_widget_get_height (w) - th - FRAME_EDGE));
}

/*
 * lrg_dock_frame_style_defaults:
 *
 * POPUP: the frame surface (PANEL, BORDER, popup radius and a deep
 * shadow). HEADER: the title bar (SURFACE, TEXT). ITEM: the close
 * button's hover disc.
 */
static void
lrg_dock_frame_style_defaults (LrgWidget      *widget,
                               LrgUIPart       part,
                               LrgWidgetState  state,
                               LrgPartLook    *look)
{
	LrgTheme *theme = lrg_widget_get_theme (widget);

	switch (part)
	{
	case LRG_UI_PART_POPUP:
		lrg_part_look_set_background (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_PANEL));
		lrg_part_look_set_border (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_BORDER),
		                          lrg_theme_get_metric (theme, LRG_THEME_METRIC_BORDER_WIDTH));
		lrg_part_look_set_radius (look, lrg_theme_get_metric (theme, LRG_THEME_METRIC_POPUP_RADIUS));
		lrg_part_look_set_shadow (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_SHADOW),
		                          lrg_theme_get_metric (theme, LRG_THEME_METRIC_SHADOW_BLUR) * 1.5f,
		                          lrg_theme_get_metric (theme, LRG_THEME_METRIC_SHADOW_OFFSET_Y) * 1.5f);
		break;
	case LRG_UI_PART_HEADER:
		lrg_part_look_set_background (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_SURFACE));
		lrg_part_look_set_foreground (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_TEXT));
		lrg_part_look_set_radius (look, lrg_theme_get_metric (theme, LRG_THEME_METRIC_POPUP_RADIUS));
		break;
	case LRG_UI_PART_ITEM:
		lrg_part_look_set_background (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_CONTROL_PRESSED));
		lrg_part_look_set_foreground (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_TEXT));
		lrg_part_look_set_radius (look, G_MAXFLOAT);
		break;
	default:
		LRG_WIDGET_CLASS (lrg_dock_frame_parent_class)->style_defaults (widget, part, state, look);
		return;
	}
	if (state & LRG_WIDGET_STATE_DISABLED)
		lrg_part_look_set_opacity (look, 0.45f);
}

/*
 * lrg_dock_frame_paint:
 *
 * Surface with shadow, title bar (icon, title, close button), the
 * panel clipped below it and a dotted resize grip in the corner.
 */
static void
lrg_dock_frame_paint (LrgWidget    *widget,
                      LrgPaintList *list)
{
	LrgDockFrame *self = LRG_DOCK_FRAME (widget);
	LrgTheme     *theme = lrg_widget_get_theme (widget);
	gfloat        x = lrg_widget_get_world_x (widget);
	gfloat        y = lrg_widget_get_world_y (widget);
	gfloat        w = lrg_widget_get_width (widget);
	gfloat        h = lrg_widget_get_height (widget);
	gfloat        th = frame_title_height (self);
	gfloat        bw = border_width (widget);
	gfloat        cx;
	gfloat        cy;
	LrgPartLook   look;
	LrgPartLook   header;
	GrlColor      c;
	guint         i;

	/* 1. Surface */
	lrg_widget_resolve_look (widget, LRG_UI_PART_POPUP, &look);
	lrg_widget_paint_box (widget, list, &look, x, y, w, h);

	/* 2. Title bar: rounded on top only, with a hairline under it */
	lrg_widget_resolve_look (widget, LRG_UI_PART_HEADER, &header);
	header.radii.top_left = look.radii.top_left;
	header.radii.top_right = look.radii.top_right;
	header.radii.bottom_left = 0.0f;
	header.radii.bottom_right = 0.0f;
	lrg_widget_paint_box (widget, list, &header, x + bw, y + bw, MAX (0.0f, w - bw * 2.0f), th - bw);
	lrg_paint_list_rect (list, x + bw, y + th - 1.0f, MAX (0.0f, w - bw * 2.0f), 1.0f, 0.0f,
	                     lrg_theme_get_color (theme, LRG_THEME_COLOR_BORDER));

	if (self->panel != NULL)
	{
		gboolean closable = frame_close_rect (self, &cx, &cy);
		gfloat   pad = metric (widget, LRG_THEME_METRIC_PADDING_X);

		paint_title (widget, list, self->panel, x, y, w, th, &header.foreground,
		             closable ? CLOSE_SIZE + pad : 0.0f);
		if (closable)
		{
			gfloat hv = CLAMP (self->close_hover.value, 0.0f, 1.0f);

			if (hv > 0.01f)
			{
				LrgPartLook item;

				lrg_widget_resolve_look (widget, LRG_UI_PART_ITEM, &item);
				lrg_paint_list_push_opacity (list, hv);
				lrg_widget_paint_box (widget, list, &item, cx, cy, CLOSE_SIZE, CLOSE_SIZE);
				lrg_paint_list_pop_opacity (list);
			}
			lrg_paint_color_with_alpha (&header.foreground, 0.6f + 0.4f * hv, &c);
			paint_close_glyph (list, cx, cy, &c);
		}

		/* 3. The panel */
		lrg_paint_list_push_clip (list, x + bw, y + th, MAX (0.0f, w - bw - FRAME_EDGE),
		                          MAX (0.0f, h - th - FRAME_EDGE));
		lrg_widget_paint (LRG_WIDGET (self->panel), list);
		lrg_paint_list_pop_clip (list);
	}

	/* 4. Resize grip: three dots along the corner diagonal */
	lrg_paint_color_with_alpha (lrg_theme_get_color (theme, LRG_THEME_COLOR_TEXT_MUTED), 0.7f, &c);
	for (i = 0; i < 3; i++)
	{
		gfloat o = 4.0f + (gfloat) i * 4.0f;

		lrg_paint_list_circle (list, x + w - o, y + h - 4.0f, 1.2f, &c);
		if (i > 0)
			lrg_paint_list_circle (list, x + w - 4.0f, y + h - o, 1.2f, &c);
	}
	lrg_paint_list_circle (list, x + w - 8.0f, y + h - 8.0f, 1.2f, &c);
}

static gboolean
lrg_dock_frame_step (LrgWidget *widget,
                     gfloat     delta_time)
{
	LrgDockFrame *self = LRG_DOCK_FRAME (widget);

	if (delta_time > 0.0f)
		self->clock += (gdouble) delta_time;
	lrg_spring_set_motion (&self->close_hover, lrg_widget_get_motion (widget) == LRG_MOTION_NONE
	                                           ? LRG_MOTION_NONE : LRG_MOTION_SNAPPY);
	lrg_spring_set_target (&self->close_hover, self->hot == FRAME_HOT_CLOSE ? 1.0f : 0.0f);
	return lrg_spring_step (&self->close_hover, delta_time);
}

/* frame_move_to: place the frame's top-left at a world position */
static void
frame_move_to (LrgDockFrame *self,
               gfloat        x,
               gfloat        y,
               gfloat        w,
               gfloat        h)
{
	LrgWidget    *widget = LRG_WIDGET (self);
	LrgContainer *parent = lrg_widget_get_parent (widget);
	gfloat        ox = parent != NULL ? lrg_widget_get_world_x (LRG_WIDGET (parent)) : 0.0f;
	gfloat        oy = parent != NULL ? lrg_widget_get_world_y (LRG_WIDGET (parent)) : 0.0f;

	lrg_widget_allocate (widget, roundf (x - ox), roundf (y - oy),
	                     roundf (MAX (FRAME_MIN_W, w)), roundf (MAX (FRAME_MIN_H, h)));
}

/*
 * lrg_dock_frame_handle_event:
 *
 * Title bar drags move the frame and show the area's drop targets
 * (dropping on one docks the panel again), the edges and corner grip
 * resize it, the close button closes the panel, a double click on the
 * title docks it back and Escape cancels a move. Events from inside
 * the panel only raise the frame on release.
 */
static gboolean
lrg_dock_frame_handle_event (LrgWidget        *widget,
                             const LrgUIEvent *event)
{
	LrgDockFrame         *self = LRG_DOCK_FRAME (widget);
	g_autoptr(LrgDockFrame) keep = g_object_ref (self);
	LrgDockArea          *area = self->area;
	LrgWidget            *target = lrg_ui_event_get_target (event);
	gboolean              own = target == widget;
	gfloat                px = lrg_ui_event_get_x (event);
	gfloat                py = lrg_ui_event_get_y (event);
	guint                 edges;
	FrameHot              hot;

	if (area == NULL || self->panel == NULL)
		return FALSE;

	switch (lrg_ui_event_get_event_type (event))
	{
	case LRG_UI_EVENT_MOUSE_BUTTON_DOWN:
		if (!own || lrg_ui_event_get_button (event) != GRL_MOUSE_BUTTON_LEFT)
			return FALSE;
		hot = frame_hit (self, px, py, &edges);
		self->press_px = px;
		self->press_py = py;
		self->start_x = lrg_widget_get_world_x (widget);
		self->start_y = lrg_widget_get_world_y (widget);
		self->start_w = lrg_widget_get_width (widget);
		self->start_h = lrg_widget_get_height (widget);
		self->moved = FALSE;
		self->edges = edges;
		if (hot == FRAME_HOT_CLOSE)
			self->mode = FRAME_CLOSE;
		else if (hot == FRAME_HOT_RESIZE)
			self->mode = FRAME_RESIZE;
		else if (hot == FRAME_HOT_TITLE)
		{
			/* A second press on the title docks the panel back */
			if (self->click_armed && self->clock - self->click_time <= DOUBLE_CLICK_TIME)
			{
				self->click_armed = FALSE;
				self->mode = FRAME_IDLE;
				move_internal (area, self->panel, NULL, LRG_DOCK_POSITION_CENTER, -1, 0.5f);
				return TRUE;
			}
			self->click_armed = TRUE;
			self->click_time = self->clock;
			self->mode = FRAME_MOVE;
		}
		else
			self->mode = FRAME_IDLE;
		return self->mode != FRAME_IDLE;

	case LRG_UI_EVENT_MOUSE_MOVE:
		if (self->mode == FRAME_MOVE)
		{
			if (!self->moved && hypotf (px - self->press_px, py - self->press_py) > DRAG_THRESHOLD)
			{
				self->moved = TRUE;
				self->click_armed = FALSE;
				begin_drag (area, self->panel, self, px, py);
			}
			if (self->moved && area->drag_frame == self)
			{
				frame_move_to (self, self->start_x + px - self->press_px, self->start_y + py - self->press_py,
				               self->start_w, self->start_h);
				drag_update (area, px, py);
			}
			return TRUE;
		}
		if (self->mode == FRAME_RESIZE)
		{
			gfloat w = self->start_w;
			gfloat h = self->start_h;

			if (self->edges & EDGE_RIGHT)
				w += px - self->press_px;
			if (self->edges & EDGE_BOTTOM)
				h += py - self->press_py;
			frame_move_to (self, self->start_x, self->start_y, w, h);
			return TRUE;
		}
		self->hot = own ? frame_hit (self, px, py, &edges) : FRAME_HOT_NONE;
		return FALSE;

	case LRG_UI_EVENT_POINTER_LEAVE:
		if (own)
			self->hot = FRAME_HOT_NONE;
		return FALSE;

	case LRG_UI_EVENT_MOUSE_BUTTON_UP:
		if (lrg_ui_event_get_button (event) != GRL_MOUSE_BUTTON_LEFT)
			return FALSE;
		if (self->mode == FRAME_CLOSE)
		{
			gfloat cx;
			gfloat cy;

			self->mode = FRAME_IDLE;
			if (frame_close_rect (self, &cx, &cy) &&
			    in_rect (px, py, cx - 2.0f, cy - 2.0f, CLOSE_SIZE + 4.0f, CLOSE_SIZE + 4.0f))
				lrg_dock_area_close_panel (area, self->panel);
			return TRUE;
		}
		if (self->mode == FRAME_MOVE && self->moved && area->drag_frame == self)
		{
			self->mode = FRAME_IDLE;
			drag_finish (area, px, py);
		}
		self->mode = FRAME_IDLE;

		/* Still floating: bring it to the front */
		if (self->area != NULL && self->panel != NULL)
			raise_frame (self->area, self);
		return own;

	case LRG_UI_EVENT_KEY_DOWN:
		if (lrg_ui_event_get_key (event) != GRL_KEY_ESCAPE)
			return FALSE;
		/* fall through */
	case LRG_UI_EVENT_CANCEL:
		if (self->mode == FRAME_MOVE && area->drag_frame == self)
		{
			self->mode = FRAME_IDLE;
			drag_cancel (area);
			return TRUE;
		}
		if (self->mode == FRAME_RESIZE)
		{
			self->mode = FRAME_IDLE;
			frame_move_to (self, self->start_x, self->start_y, self->start_w, self->start_h);
			return TRUE;
		}
		return FALSE;

	default:
		return FALSE;
	}
}

static void
lrg_dock_frame_measure (LrgWidget *widget,
                        gfloat    *preferred_width,
                        gfloat    *preferred_height)
{
	*preferred_width = MAX (FRAME_MIN_W, lrg_widget_get_width (widget));
	*preferred_height = MAX (FRAME_MIN_H, lrg_widget_get_height (widget));
}

static void
lrg_dock_frame_class_init (LrgDockFrameClass *klass)
{
	LrgWidgetClass    *widget_class = LRG_WIDGET_CLASS (klass);
	LrgContainerClass *container_class = LRG_CONTAINER_CLASS (klass);

	widget_class->paint = lrg_dock_frame_paint;
	widget_class->step = lrg_dock_frame_step;
	widget_class->measure = lrg_dock_frame_measure;
	widget_class->style_defaults = lrg_dock_frame_style_defaults;
	widget_class->handle_event = lrg_dock_frame_handle_event;
	container_class->layout_children = lrg_dock_frame_layout_children;

	/* The panel sits at a fixed place under the title bar */
	lrg_widget_class_set_handles_direction (widget_class, TRUE);
}

static void
lrg_dock_frame_init (LrgDockFrame *self)
{
	lrg_widget_set_focusable (LRG_WIDGET (self), TRUE);
	lrg_spring_init (&self->close_hover, 0.0f, LRG_MOTION_SNAPPY);
}

static LrgDockFrame *
frame_new (LrgDockArea *area)
{
	LrgDockFrame *frame = g_object_new (LRG_TYPE_DOCK_FRAME, NULL);

	frame->area = area;
	lrg_widget_set_name (LRG_WIDGET (frame), "dock-frame");
	return frame;
}

static LrgDockFrame *
frame_get_panel_frame (LrgDockArea  *self,
                       LrgDockPanel *panel)
{
	guint i;

	for (i = 0; i < self->frames->len; i++)
	{
		LrgDockFrame *f = g_ptr_array_index (self->frames, i);

		if (f->panel == panel)
			return f;
	}
	return NULL;
}

/* raise_frame: move a frame to the top of the overlay layer */
static void
raise_frame (LrgDockArea  *self,
             LrgDockFrame *frame)
{
	LrgContainer *parent = lrg_widget_get_parent (LRG_WIDGET (frame));
	g_autoptr(LrgDockFrame) keep = NULL;

	if (!LRG_IS_CANVAS (parent) || !g_ptr_array_find (self->frames, frame, NULL))
		return;
	keep = g_object_ref (frame);
	lrg_canvas_remove_overlay (LRG_CANVAS (parent), LRG_WIDGET (frame));
	lrg_canvas_add_overlay (LRG_CANVAS (parent), LRG_WIDGET (frame));
}

/*
 * unfloat:
 *
 * Moves a floating panel back under the area (as a container child,
 * not yet in the tree) and destroys its frame.
 */
static void
unfloat (LrgDockArea  *self,
         LrgDockFrame *frame)
{
	g_autoptr(LrgDockPanel) panel = g_object_ref (frame->panel);
	LrgContainer           *parent = lrg_widget_get_parent (LRG_WIDGET (frame));

	if (self->drag_frame == frame)
		self->drag_frame = NULL;

	frame->panel = NULL;
	lrg_container_remove_child (LRG_CONTAINER (frame), LRG_WIDGET (panel));
	if (LRG_IS_CANVAS (parent))
		lrg_canvas_remove_overlay (LRG_CANVAS (parent), LRG_WIDGET (frame));
	frame->area = NULL;
	g_ptr_array_remove (self->frames, frame);

	lrg_container_add_child (LRG_CONTAINER (self), LRG_WIDGET (panel));
}

/*
 * float_detached:
 *
 * Frames a panel that is a container child of the area but not in the
 * tree, and puts the frame in the canvas overlay layer.
 */
static void
float_detached (LrgDockArea  *self,
                LrgCanvas    *canvas,
                LrgDockPanel *panel,
                gfloat        x,
                gfloat        y,
                gfloat        w,
                gfloat        h)
{
	g_autoptr(LrgDockPanel) keep = g_object_ref (panel);
	LrgDockFrame           *frame = frame_new (self);

	lrg_container_remove_child (LRG_CONTAINER (self), LRG_WIDGET (panel));
	frame->panel = panel;
	lrg_container_add_child (LRG_CONTAINER (frame), LRG_WIDGET (panel));
	lrg_widget_set_visible (LRG_WIDGET (panel), TRUE);

	g_ptr_array_add (self->frames, frame);
	lrg_canvas_add_overlay (canvas, LRG_WIDGET (frame));
	frame_move_to (frame, x, y, w, h);
}

/* ==========================================================================
 * Moving panels
 * ========================================================================== */

/*
 * move_internal:
 * @target: (nullable): a stack of the tree, or %NULL for the whole area
 * @index: tab index for CENTER (-1 appends)
 * @share: share of the space for side positions
 *
 * Detaches @panel from wherever it is (its stack, which may collapse,
 * or its floating frame) and inserts it at the new place. Moves that
 * would change nothing (a panel beside itself when alone, onto its own
 * stack) are ignored. @target survives the detach: collapsing only
 * frees the emptied stack and its parent split, and @target cannot be
 * that stack.
 */
static void
move_internal (LrgDockArea     *self,
               LrgDockPanel    *panel,
               DockNode        *target,
               LrgDockPosition  position,
               gint             index,
               gfloat           share)
{
	LrgDockFrame *frame = NULL;
	DockNode     *source;
	guint         own = 0;

	if (target != NULL && !node_in_tree (self->root, target))
		return;

	source = find_stack (self->root, panel, &own);
	if (source == NULL)
	{
		frame = frame_get_panel_frame (self, panel);
		if (frame == NULL)
			return;
	}

	/* Onto its own stack: reorder (CENTER) or split it */
	if (source != NULL && target == source)
	{
		if (position == LRG_DOCK_POSITION_CENTER)
		{
			if (index < 0)
				return;
			if ((guint) index > own)
				index--;
			if ((guint) index == own || (guint) index >= source->panels->len)
				return;
			g_ptr_array_remove_index (source->panels, own);
			g_ptr_array_insert (source->panels, index, panel);
			source->active = (guint) index;
			tree_changed (self);
			return;
		}
		if (source->panels->len == 1)
			return;
	}

	/* The only panel against the area edge or its own (root) stack */
	if (source != NULL && target == NULL && source == self->root && source->panels->len == 1)
		return;

	if (source != NULL)
		detach_from_stack (self, source, panel);
	else
		unfloat (self, frame);

	insert_at (self, panel, target, position, index, share);
	tree_changed (self);
	set_active_panel (self, panel);
}

/* ==========================================================================
 * Drag and drop
 * ========================================================================== */

/* The private overlay widget painting the drag feedback */
struct _LrgDockDragLayer
{
	LrgWidget    parent_instance;
	LrgDockArea *area;              /* not owned */
};

G_DEFINE_FINAL_TYPE (LrgDockDragLayer, lrg_dock_drag_layer, LRG_TYPE_WIDGET)

static void paint_drag (LrgDockArea *self, LrgPaintList *list);

static void
lrg_dock_drag_layer_paint (LrgWidget    *widget,
                           LrgPaintList *list)
{
	LrgDockDragLayer *self = LRG_DOCK_DRAG_LAYER (widget);

	if (self->area != NULL)
		paint_drag (self->area, list);
}

static void
lrg_dock_drag_layer_class_init (LrgDockDragLayerClass *klass)
{
	LRG_WIDGET_CLASS (klass)->paint = lrg_dock_drag_layer_paint;
}

static void
lrg_dock_drag_layer_init (LrgDockDragLayer *self)
{
	lrg_widget_set_pointer_transparent (LRG_WIDGET (self), TRUE);
}

/* install_layer: put the feedback layer on top of the overlay layer */
static void
install_layer (LrgDockArea *self)
{
	LrgCanvas *canvas = canvas_of (self);

	if (canvas == NULL)
		return;
	if (self->layer == NULL)
	{
		self->layer = g_object_new (LRG_TYPE_DOCK_DRAG_LAYER, NULL);
		LRG_DOCK_DRAG_LAYER (self->layer)->area = self;
		lrg_widget_set_name (self->layer, "dock-drag-layer");
	}
	if (lrg_widget_get_parent (self->layer) != NULL)
		lrg_canvas_remove_overlay (canvas, self->layer);
	lrg_canvas_add_overlay (canvas, self->layer);
}

static void
uninstall_layer (LrgDockArea *self)
{
	LrgContainer *parent;

	if (self->layer == NULL)
		return;
	parent = lrg_widget_get_parent (self->layer);
	if (LRG_IS_CANVAS (parent))
		lrg_canvas_remove_overlay (LRG_CANVAS (parent), self->layer);
	LRG_DOCK_DRAG_LAYER (self->layer)->area = NULL;
	g_clear_object (&self->layer);
	g_clear_object (&self->ghost_panel);
}

/* compass_rect: one button of the compass over a stack (area-relative) */
static void
compass_rect (LrgDockArea     *self,
              DockNode        *s,
              LrgDockPosition  pos,
              gfloat          *x,
              gfloat          *y)
{
	gfloat strip = s != NULL ? strip_height (self) : 0.0f;
	gfloat cx;
	gfloat cy;
	gfloat step = COMPASS_BUTTON + COMPASS_GAP;

	if (s != NULL)
	{
		cx = s->x + s->w * 0.5f;
		cy = s->y + strip + (s->h - strip) * 0.5f;
	}
	else
	{
		cx = lrg_widget_get_width (LRG_WIDGET (self)) * 0.5f;
		cy = lrg_widget_get_height (LRG_WIDGET (self)) * 0.5f;
	}

	*x = cx - COMPASS_BUTTON * 0.5f;
	*y = cy - COMPASS_BUTTON * 0.5f;
	switch (pos)
	{
	case LRG_DOCK_POSITION_LEFT:
		*x -= step;
		break;
	case LRG_DOCK_POSITION_RIGHT:
		*x += step;
		break;
	case LRG_DOCK_POSITION_TOP:
		*y -= step;
		break;
	case LRG_DOCK_POSITION_BOTTOM:
		*y += step;
		break;
	case LRG_DOCK_POSITION_CENTER:
	default:
		break;
	}
}

/* edge_rect: the target for docking against one edge of the area */
static void
edge_rect (LrgDockArea     *self,
           LrgDockPosition  pos,
           gfloat          *x,
           gfloat          *y)
{
	gfloat w = lrg_widget_get_width (LRG_WIDGET (self));
	gfloat h = lrg_widget_get_height (LRG_WIDGET (self));
	gfloat top = strip_height (self) + EDGE_MARGIN;

	switch (pos)
	{
	case LRG_DOCK_POSITION_LEFT:
		*x = EDGE_MARGIN;
		*y = (h - COMPASS_BUTTON) * 0.5f;
		break;
	case LRG_DOCK_POSITION_RIGHT:
		*x = w - EDGE_MARGIN - COMPASS_BUTTON;
		*y = (h - COMPASS_BUTTON) * 0.5f;
		break;
	case LRG_DOCK_POSITION_TOP:
		*x = (w - COMPASS_BUTTON) * 0.5f;
		*y = top;
		break;
	case LRG_DOCK_POSITION_BOTTOM:
	default:
		*x = (w - COMPASS_BUTTON) * 0.5f;
		*y = h - EDGE_MARGIN - COMPASS_BUTTON;
		break;
	}
}

/* drag_source: the dragged panel's stack (NULL when floating) */
static DockNode *
drag_source (LrgDockArea *self)
{
	return self->drag_panel != NULL ? find_stack (self->root, self->drag_panel, NULL) : NULL;
}

/* edge_valid: docking against the area edges would change something */
static gboolean
edge_valid (LrgDockArea *self)
{
	DockNode *src = drag_source (self);

	return self->root != NULL && !(src != NULL && src == self->root && src->panels->len == 1);
}

/* compass_valid: a compass button of @s would change something */
static gboolean
compass_valid (LrgDockArea     *self,
               DockNode        *s,
               LrgDockPosition  pos)
{
	DockNode *src = drag_source (self);

	if (pos == LRG_DOCK_POSITION_CENTER)
		return s != src;
	return !(s == src && s->panels->len == 1);
}

static gboolean
any_compass_valid (LrgDockArea *self,
                   DockNode    *s)
{
	guint p;

	for (p = LRG_DOCK_POSITION_LEFT; p <= LRG_DOCK_POSITION_CENTER; p++)
		if (compass_valid (self, s, (LrgDockPosition) p))
			return TRUE;
	return FALSE;
}

/*
 * compute_drop:
 *
 * Where the dragged panel would land if dropped at the world point:
 * an empty area takes it whole; then the four area edge targets; then,
 * over a stack, its tab strip (an insertion index) or its compass
 * buttons. Anything else is no target (a drop there floats the panel).
 */
static void
compute_drop (LrgDockArea *self,
              gfloat       wx,
              gfloat       wy)
{
	LrgWidget *widget = LRG_WIDGET (self);
	gfloat     aw = lrg_widget_get_width (widget);
	gfloat     ah = lrg_widget_get_height (widget);
	gfloat     px = wx - lrg_widget_get_world_x (widget);
	gfloat     py = wy - lrg_widget_get_world_y (widget);
	gfloat     strip = strip_height (self);
	DropKind   previous = self->drop.kind;
	DockNode  *s;
	gfloat     bx;
	gfloat     by;
	guint      p;
	Drop      *d = &self->drop;

	d->kind = DROP_NONE;
	d->stack = NULL;
	d->index = -1;

	if (!in_rect (px, py, 0.0f, 0.0f, aw, ah))
	{
		self->hover_stack = NULL;
		return;
	}

	/* 1. An empty area: anywhere */
	if (self->root == NULL)
	{
		self->hover_stack = NULL;
		d->kind = DROP_EMPTY;
		d->position = LRG_DOCK_POSITION_CENTER;
		d->x = 0.0f;
		d->y = 0.0f;
		d->w = aw;
		d->h = ah;
		goto done;
	}

	/* 2. Area edges */
	if (edge_valid (self))
	{
		for (p = LRG_DOCK_POSITION_LEFT; p <= LRG_DOCK_POSITION_BOTTOM; p++)
		{
			edge_rect (self, (LrgDockPosition) p, &bx, &by);
			if (!in_rect (px, py, bx, by, COMPASS_BUTTON, COMPASS_BUTTON))
				continue;
			d->kind = DROP_EDGE;
			d->position = (LrgDockPosition) p;
			d->x = p == LRG_DOCK_POSITION_RIGHT ? aw * (1.0f - EDGE_SHARE) : 0.0f;
			d->y = p == LRG_DOCK_POSITION_BOTTOM ? ah * (1.0f - EDGE_SHARE) : 0.0f;
			d->w = (p == LRG_DOCK_POSITION_LEFT || p == LRG_DOCK_POSITION_RIGHT) ? aw * EDGE_SHARE : aw;
			d->h = (p == LRG_DOCK_POSITION_TOP || p == LRG_DOCK_POSITION_BOTTOM) ? ah * EDGE_SHARE : ah;
			break;
		}
	}

	/* 3. The stack under the pointer: compass or tab strip */
	s = stack_at (self->root, px, py);
	if (s != self->hover_stack)
	{
		self->hover_stack = s;
		lrg_spring_snap (&self->compass_shown, 0.0f);
	}
	if (d->kind != DROP_NONE || s == NULL)
		goto done;

	if (py < s->y + strip)
	{
		g_autofree gfloat *xs = g_new (gfloat, s->panels->len + 1);
		g_autofree gfloat *ws = g_new (gfloat, s->panels->len + 1);
		gboolean           rtl = is_rtl (self);
		guint              n = stack_tabs (self, s, xs, ws);
		guint              index = n;
		guint              own;
		gfloat             boundary;
		gfloat             width;

		for (p = 0; p < n; p++)
		{
			gfloat mid = xs[p] + ws[p] * 0.5f;

			if (rtl ? px > mid : px < mid)
			{
				index = p;
				break;
			}
		}

		/* Next to itself: nothing would move */
		if (g_ptr_array_find (s->panels, self->drag_panel, &own) && (index == own || index == own + 1))
			goto done;

		if (index < n)
			boundary = rtl ? xs[index] + ws[index] + TAB_GAP * 0.5f : xs[index] - TAB_GAP * 0.5f;
		else if (n > 0)
			boundary = rtl ? xs[n - 1] - TAB_GAP * 0.5f : xs[n - 1] + ws[n - 1] + TAB_GAP * 0.5f;
		else
			boundary = rtl ? s->x + s->w - STRIP_PAD : s->x + STRIP_PAD;

		width = MIN (tab_natural_width (self, self->drag_panel), s->w * 0.5f);
		d->kind = DROP_TABS;
		d->stack = s;
		d->index = (gint) index;
		d->position = LRG_DOCK_POSITION_CENTER;
		d->x = CLAMP (boundary - width * 0.5f, s->x, MAX (s->x, s->x + s->w - width));
		d->y = s->y + TAB_INSET;
		d->w = width;
		d->h = strip - TAB_INSET * 2.0f;
		goto done;
	}

	for (p = LRG_DOCK_POSITION_LEFT; p <= LRG_DOCK_POSITION_CENTER; p++)
	{
		compass_rect (self, s, (LrgDockPosition) p, &bx, &by);
		if (!in_rect (px, py, bx, by, COMPASS_BUTTON, COMPASS_BUTTON) ||
		    !compass_valid (self, s, (LrgDockPosition) p))
			continue;
		d->kind = DROP_STACK;
		d->stack = s;
		d->position = (LrgDockPosition) p;
		d->x = p == LRG_DOCK_POSITION_RIGHT ? s->x + s->w * 0.5f : s->x;
		d->y = p == LRG_DOCK_POSITION_BOTTOM ? s->y + s->h * 0.5f : s->y;
		d->w = (p == LRG_DOCK_POSITION_LEFT || p == LRG_DOCK_POSITION_RIGHT) ? s->w * 0.5f : s->w;
		d->h = (p == LRG_DOCK_POSITION_TOP || p == LRG_DOCK_POSITION_BOTTOM) ? s->h * 0.5f : s->h;
		break;
	}

done:
	if (d->kind == DROP_NONE)
		return;

	/* The preview appears at its target (from 60 %), then glides from
	 * target to target */
	if (previous == DROP_NONE && self->preview_shown.value < 0.05f)
	{
		lrg_spring_snap (&self->preview_cx, d->x + d->w * 0.5f);
		lrg_spring_snap (&self->preview_cy, d->y + d->h * 0.5f);
		lrg_spring_snap (&self->preview_w, d->w * 0.6f);
		lrg_spring_snap (&self->preview_h, d->h * 0.6f);
	}
	lrg_spring_set_target (&self->preview_cx, d->x + d->w * 0.5f);
	lrg_spring_set_target (&self->preview_cy, d->y + d->h * 0.5f);
	lrg_spring_set_target (&self->preview_w, d->w);
	lrg_spring_set_target (&self->preview_h, d->h);
}

/*
 * begin_drag:
 * @frame: (nullable): the floating frame being moved, %NULL for a tab
 *
 * Lifts @panel: the feedback layer goes on top of the overlay layer
 * and the ghost starts at the pointer.
 */
static void
begin_drag (LrgDockArea  *self,
            LrgDockPanel *panel,
            LrgDockFrame *frame,
            gfloat        px,
            gfloat        py)
{
	self->dragging = TRUE;
	self->drag_panel = panel;
	self->drag_frame = frame;
	self->drop.kind = DROP_NONE;
	self->hover_stack = NULL;
	self->hot_panel = NULL;
	self->hot_split = NULL;

	g_clear_object (&self->ghost_panel);
	if (frame == NULL)
		self->ghost_panel = g_object_ref (panel);
	lrg_spring_snap (&self->ghost_x, px);
	lrg_spring_snap (&self->ghost_y, py);
	if (self->lift.value < 0.02f)
		lrg_spring_snap (&self->lift, 0.0f);
	install_layer (self);
	drag_update (self, px, py);
}

static void
drag_update (LrgDockArea *self,
             gfloat       px,
             gfloat       py)
{
	self->pointer_x = px;
	self->pointer_y = py;
	compute_drop (self, px, py);
}

/* end_drag: back to rest; the feedback fades out before the layer goes */
static void
end_drag (LrgDockArea *self)
{
	self->dragging = FALSE;
	self->drag_panel = NULL;
	self->drag_frame = NULL;
	self->press_panel = NULL;
	self->drop.kind = DROP_NONE;
	self->drop.stack = NULL;
	self->hover_stack = NULL;
}

/* drag_cancel: Escape; a moved floating frame returns to where it was */
static void
drag_cancel (LrgDockArea *self)
{
	LrgDockFrame *frame = self->drag_frame;

	if (frame != NULL)
		frame_move_to (frame, frame->start_x, frame->start_y, frame->start_w, frame->start_h);
	end_drag (self);
}

/*
 * drag_finish:
 *
 * Applies the drop at the pointer. Without a target a dragged tab
 * floats (centred under the pointer at its docked size) when floating
 * is allowed; a dragged frame just stays where it was moved.
 */
static void
drag_finish (LrgDockArea *self,
             gfloat       px,
             gfloat       py)
{
	LrgDockPanel *panel = self->drag_panel;
	LrgDockFrame *frame = self->drag_frame;
	Drop          d;

	drag_update (self, px, py);
	d = self->drop;
	end_drag (self);
	if (panel == NULL)
		return;

	switch (d.kind)
	{
	case DROP_EMPTY:
		move_internal (self, panel, NULL, LRG_DOCK_POSITION_CENTER, -1, 0.5f);
		break;
	case DROP_EDGE:
		move_internal (self, panel, NULL, d.position, -1, EDGE_SHARE);
		break;
	case DROP_STACK:
		move_internal (self, panel, d.stack, d.position, -1, 0.5f);
		break;
	case DROP_TABS:
		move_internal (self, panel, d.stack, LRG_DOCK_POSITION_CENTER, d.index, 0.5f);
		break;
	case DROP_NONE:
	default:
		if (frame == NULL)
		{
			gfloat w = MAX (FRAME_DEFAULT_W, lrg_widget_get_width (LRG_WIDGET (panel)));
			gfloat h = MAX (FRAME_DEFAULT_H, lrg_widget_get_height (LRG_WIDGET (panel)) + strip_height (self));

			lrg_dock_area_float_panel (self, panel, px - w * 0.5f, py - strip_height (self) * 0.5f, w, h);
		}
		break;
	}
}

/* ==========================================================================
 * Looks
 * ========================================================================== */

/*
 * lrg_dock_area_style_defaults:
 *
 * BOX: each stack's surface. HEADER: the tab strip. TAB: one tab
 * (transparent at rest, CONTROL_HOVER hovered, an accent wash when
 * SELECTED). INDICATOR: the accent pill under the active tab. HANDLE:
 * the dividers. FILL: the drop preview (accent-tinted glass). MARK:
 * compass buttons (SELECTED for the target under the pointer). POPUP:
 * the dragged tab's ghost. ITEM: a close button's hover disc.
 */
static void
lrg_dock_area_style_defaults (LrgWidget      *widget,
                              LrgUIPart       part,
                              LrgWidgetState  state,
                              LrgPartLook    *look)
{
	LrgTheme      *theme = lrg_widget_get_theme (widget);
	gboolean       hovered = (state & LRG_WIDGET_STATE_HOVERED) != 0;
	gboolean       pressed = (state & LRG_WIDGET_STATE_PRESSED) != 0;
	gboolean       selected = (state & LRG_WIDGET_STATE_SELECTED) != 0;
	const GrlColor *accent = lrg_theme_get_color (theme, LRG_THEME_COLOR_ACCENT);
	gfloat         radius = lrg_theme_get_metric (theme, LRG_THEME_METRIC_CORNER_RADIUS);
	GrlColor       c;

	switch (part)
	{
	case LRG_UI_PART_BOX:
		lrg_part_look_set_background (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_PANEL));
		lrg_part_look_set_border (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_BORDER),
		                          lrg_theme_get_metric (theme, LRG_THEME_METRIC_BORDER_WIDTH));
		lrg_part_look_set_radius (look, radius);
		break;

	case LRG_UI_PART_HEADER:
		lrg_part_look_set_background (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_SURFACE));
		lrg_part_look_set_radius (look, radius);
		break;

	case LRG_UI_PART_TAB:
		if (hovered)
			c = *lrg_theme_get_color (theme, LRG_THEME_COLOR_CONTROL_HOVER);
		else if (selected)
			lrg_paint_color_with_alpha (accent, 0.12f, &c);
		else
			lrg_paint_color_with_alpha (lrg_theme_get_color (theme, LRG_THEME_COLOR_CONTROL_HOVER), 0.0f, &c);
		lrg_part_look_set_background (look, &c);
		lrg_part_look_set_foreground (look, lrg_theme_get_color (theme, (hovered || selected)
		                                                                ? LRG_THEME_COLOR_TEXT
		                                                                : LRG_THEME_COLOR_TEXT_MUTED));
		lrg_part_look_set_radius (look, radius);
		break;

	case LRG_UI_PART_INDICATOR:
		lrg_part_look_set_background (look, accent);
		lrg_part_look_set_radius (look, G_MAXFLOAT);
		lrg_part_look_set_size (look, INDICATOR_THICKNESS);
		lrg_paint_color_with_alpha (accent, 0.35f, &c);
		lrg_part_look_set_glow (look, &c, 6.0f);
		break;

	case LRG_UI_PART_HANDLE:
		/* A hairline at rest, a thicker accent line when hovered, a
		 * glowing solid accent while dragged */
		if (pressed)
		{
			lrg_part_look_set_background (look, accent);
			lrg_part_look_set_size (look, 4.0f);
			lrg_paint_color_with_alpha (accent, 0.35f, &c);
			lrg_part_look_set_glow (look, &c, 6.0f);
		}
		else if (hovered)
		{
			lrg_paint_color_with_alpha (accent, 0.7f, &c);
			lrg_part_look_set_background (look, &c);
			lrg_part_look_set_size (look, 4.0f);
		}
		else
		{
			lrg_paint_color_with_alpha (lrg_theme_get_color (theme, LRG_THEME_COLOR_BORDER), 0.0f, &c);
			lrg_part_look_set_background (look, &c);
			lrg_part_look_set_size (look, 1.0f);
		}
		lrg_part_look_set_foreground (look, lrg_theme_get_color (theme, (hovered || pressed)
		                                                                ? LRG_THEME_COLOR_ACCENT
		                                                                : LRG_THEME_COLOR_TEXT_MUTED));
		lrg_part_look_set_radius (look, G_MAXFLOAT);
		break;

	case LRG_UI_PART_FILL:
		{
			LrgPaintGlass glass;

			lrg_paint_glass_init_default (&glass);
			glass.frost = 0.55f;
			lrg_paint_color_with_alpha (accent, 0.30f, &glass.tint);
			lrg_part_look_set_glass (look, &glass);
			lrg_part_look_set_material (look, LRG_PAINT_MATERIAL_GLASS);
			lrg_paint_color_with_alpha (accent, 0.22f, &c);
			lrg_part_look_set_background (look, &c);
			lrg_paint_color_with_alpha (accent, 0.9f, &c);
			lrg_part_look_set_border (look, &c, 2.0f);
			lrg_part_look_set_radius (look, radius * 1.5f);
			lrg_paint_color_with_alpha (accent, 0.25f, &c);
			lrg_part_look_set_glow (look, &c, 10.0f);
		}
		break;

	case LRG_UI_PART_MARK:
		lrg_part_look_set_background (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_POPUP));
		lrg_part_look_set_border (look, selected ? accent : lrg_theme_get_color (theme, LRG_THEME_COLOR_BORDER),
		                          selected ? 2.0f : MAX (1.0f, lrg_theme_get_metric (theme,
		                                                            LRG_THEME_METRIC_BORDER_WIDTH)));
		lrg_part_look_set_foreground (look, selected ? accent
		                                             : lrg_theme_get_color (theme, LRG_THEME_COLOR_TEXT_MUTED));
		lrg_part_look_set_radius (look, 7.0f);
		lrg_part_look_set_shadow (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_SHADOW),
		                          lrg_theme_get_metric (theme, LRG_THEME_METRIC_SHADOW_BLUR),
		                          lrg_theme_get_metric (theme, LRG_THEME_METRIC_SHADOW_OFFSET_Y));
		if (selected)
		{
			lrg_paint_color_with_alpha (accent, 0.45f, &c);
			lrg_part_look_set_glow (look, &c, 8.0f);
		}
		break;

	case LRG_UI_PART_POPUP:
		lrg_part_look_set_background (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_POPUP));
		lrg_part_look_set_foreground (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_TEXT));
		lrg_part_look_set_border (look, accent, 1.0f);
		lrg_part_look_set_radius (look, radius + 2.0f);
		lrg_part_look_set_shadow (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_SHADOW),
		                          lrg_theme_get_metric (theme, LRG_THEME_METRIC_SHADOW_BLUR) * 1.5f,
		                          lrg_theme_get_metric (theme, LRG_THEME_METRIC_SHADOW_OFFSET_Y) + 4.0f);
		break;

	case LRG_UI_PART_ITEM:
		lrg_part_look_set_background (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_CONTROL_PRESSED));
		lrg_part_look_set_foreground (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_TEXT));
		lrg_part_look_set_radius (look, G_MAXFLOAT);
		break;

	default:
		LRG_WIDGET_CLASS (lrg_dock_area_parent_class)->style_defaults (widget, part, state, look);
		return;
	}

	if (state & LRG_WIDGET_STATE_DISABLED)
		lrg_part_look_set_opacity (look, 0.45f);
}

/* ==========================================================================
 * Painting
 * ========================================================================== */

/*
 * ellipsize:
 *
 * The longest prefix of @text that, followed by an ellipsis, measures
 * at most @avail (a binary search over the character count), or the
 * ellipsis alone.
 *
 * Returns: (transfer full): the shortened text
 */
static gchar *
ellipsize (GrlFont     *font,
           const gchar *text,
           gfloat       size,
           gfloat       avail)
{
	glong lo = 0;
	glong hi = g_utf8_strlen (text, -1);

	/* Invariant: a prefix of lo characters fits */
	while (lo < hi)
	{
		glong             mid = (lo + hi + 1) / 2;
		g_autofree gchar *prefix = g_utf8_substring (text, 0, mid);
		g_autofree gchar *candidate = g_strconcat (g_strchomp (prefix), ELLIPSIS, NULL);
		gfloat            tw = 0.0f;
		gfloat            th = 0.0f;

		lrg_paint_measure_text (font, candidate, size, 1.0f, &tw, &th);
		if (tw <= avail)
			lo = mid;
		else
			hi = mid - 1;
	}
	if (lo == 0)
		return g_strdup (ELLIPSIS);
	{
		g_autofree gchar *prefix = g_utf8_substring (text, 0, lo);

		return g_strconcat (g_strchomp (prefix), ELLIPSIS, NULL);
	}
}

/*
 * paint_title:
 * @reserve: units kept free at the trailing end (close button)
 *
 * Icon and title of @panel, vertically centred in the box, clipped.
 * A title too long for the box ends in an ellipsis. In RTL the content
 * is right-aligned and the reserve is on the left.
 */
static void
paint_title (LrgWidget      *widget,
             LrgPaintList   *list,
             LrgDockPanel   *panel,
             gfloat          x,
             gfloat          y,
             gfloat          w,
             gfloat          h,
             const GrlColor *fg,
             gfloat          reserve)
{
	LrgTheme *theme = lrg_widget_get_theme (widget);
	gfloat    pad = lrg_theme_get_metric (theme, LRG_THEME_METRIC_PADDING_X);
	gfloat    size = lrg_theme_get_metric (theme, LRG_THEME_METRIC_FONT_SIZE);
	LrgIcon   icon = panel_icon (panel);
	gboolean  rtl = lrg_widget_is_rtl (widget);
	gfloat    tw = 0.0f;
	gfloat    th = 0.0f;
	gfloat    content;
	gfloat    cx;
	gfloat    room;
	g_autofree gchar *shortened = NULL;
	const gchar *title = panel->title;

	if (title != NULL)
		lrg_paint_measure_text (lrg_theme_get_ui_font (theme), title, size, 1.0f, &tw, &th);

	/* Too long for the box: end it in an ellipsis */
	room = w - reserve - pad * (reserve > 0.0f ? 1.5f : 2.0f) - (icon != LRG_ICON_NONE ? ICON_SIZE + ICON_GAP : 0.0f);
	if (title != NULL && title[0] != '\0' && tw > room + 0.5f)
	{
		gfloat sh = 0.0f;

		shortened = ellipsize (lrg_theme_get_ui_font (theme), title, size, MAX (0.0f, room));
		title = shortened;
		lrg_paint_measure_text (lrg_theme_get_ui_font (theme), title, size, 1.0f, &tw, &sh);
	}
	content = tw + (icon != LRG_ICON_NONE ? ICON_SIZE + ICON_GAP : 0.0f);
	cx = rtl ? x + w - pad - content : x + pad;

	lrg_paint_list_push_clip (list, rtl ? x + reserve : x, y, MAX (0.0f, w - reserve), h);
	if (icon != LRG_ICON_NONE)
	{
		lrg_paint_list_icon (list, icon, roundf (cx), roundf (y + (h - ICON_SIZE) * 0.5f), ICON_SIZE, 0.0f, fg);
		cx += ICON_SIZE + ICON_GAP;
	}
	if (title != NULL && title[0] != '\0')
		lrg_paint_list_text (list, lrg_theme_get_ui_font (theme), title, roundf (cx),
		                     roundf (y + (h - th) * 0.5f), size, 1.0f, fg);
	lrg_paint_list_pop_clip (list);
}

/*
 * tab_look:
 *
 * The TAB look of one tab: the area's state without its own hover /
 * press, SELECTED for the active tab, cross-faded to the hovered
 * variant by the panel's tab hover spring.
 */
static void
tab_look (LrgDockArea  *self,
          LrgDockPanel *panel,
          gboolean      active,
          LrgPartLook  *look)
{
	LrgWidget      *widget = LRG_WIDGET (self);
	LrgWidgetState  base;
	gfloat          h = CLAMP (panel->tab_hover.value, 0.0f, 1.0f);

	base = lrg_widget_get_state_flags (widget) &
	       ~(LRG_WIDGET_STATE_HOVERED | LRG_WIDGET_STATE_PRESSED | LRG_WIDGET_STATE_SELECTED);
	if (active)
		base |= LRG_WIDGET_STATE_SELECTED;

	lrg_widget_resolve_look_for_state (widget, LRG_UI_PART_TAB, base, look);
	if (h > 0.001f)
	{
		LrgPartLook hovered;

		lrg_widget_resolve_look_for_state (widget, LRG_UI_PART_TAB, base | LRG_WIDGET_STATE_HOVERED, &hovered);
		lrg_part_look_lerp (look, &hovered, h, look);
	}
}

/*
 * paint_strip_buttons:
 *
 * The three buttons of an overflowing strip: a hairline separating
 * them from the tab region, then for each an ITEM box faded in by its
 * hover spring and a chevron glyph. An arrow that cannot scroll any
 * further is dimmed.
 */
static void
paint_strip_buttons (LrgDockArea     *self,
                     DockNode        *s,
                     const StripInfo *info,
                     LrgPaintList    *list)
{
	LrgWidget   *widget = LRG_WIDGET (self);
	LrgTheme    *theme = theme_of (self);
	gfloat       ox = lrg_widget_get_world_x (widget);
	gfloat       oy = lrg_widget_get_world_y (widget);
	gfloat       strip = strip_height (self);
	gfloat       bh = strip - TAB_INSET * 2.0f;
	gboolean     rtl = is_rtl (self);
	LrgPartLook  tab;
	LrgPartLook  item;
	gfloat       sep_x;
	guint        i;

	/* Glyphs take the plain (muted) tab foreground */
	lrg_widget_resolve_look_for_state (widget, LRG_UI_PART_TAB,
	                                   lrg_widget_get_state_flags (widget) &
	                                   ~(LRG_WIDGET_STATE_HOVERED | LRG_WIDGET_STATE_PRESSED |
	                                     LRG_WIDGET_STATE_SELECTED),
	                                   &tab);
	lrg_widget_resolve_look (widget, LRG_UI_PART_ITEM, &item);

	/* Hairline between the tab region and the buttons */
	sep_x = rtl ? info->view_x - STRIP_PAD * 0.5f : info->view_x + info->view_w + STRIP_PAD * 0.5f;
	lrg_paint_list_rect (list, roundf (ox + sep_x), oy + s->y + TAB_INSET + 3.0f, 1.0f, MAX (0.0f, bh - 6.0f), 0.0f,
	                     lrg_theme_get_color (theme, LRG_THEME_COLOR_BORDER));

	for (i = 0; i < N_STRIP_BUTTONS; i++)
	{
		gfloat   bx = ox + info->button_x[i];
		gfloat   by = oy + s->y + TAB_INSET;
		gfloat   hv = CLAMP (s->button_hover[i].value, 0.0f, 1.0f);
		gboolean usable = TRUE;
		LrgIcon  icon;
		GrlColor fg;

		if (i == LRG_DOCK_STRIP_BUTTON_SCROLL_BACK)
		{
			icon = rtl ? LRG_ICON_CHEVRON_RIGHT : LRG_ICON_CHEVRON_LEFT;
			usable = info->scroll > 0.5f;
		}
		else if (i == LRG_DOCK_STRIP_BUTTON_SCROLL_FORWARD)
		{
			icon = rtl ? LRG_ICON_CHEVRON_LEFT : LRG_ICON_CHEVRON_RIGHT;
			usable = info->scroll < info->max_scroll - 0.5f;
		}
		else
			icon = LRG_ICON_CHEVRON_DOWN;

		if (hv > 0.01f && usable)
		{
			lrg_paint_list_push_opacity (list, hv);
			lrg_widget_paint_box (widget, list, &item, bx, by, STRIP_BUTTON_W, bh);
			lrg_paint_list_pop_opacity (list);
		}
		lrg_paint_color_with_alpha (&tab.foreground,
		                            ((gfloat) tab.foreground.a / 255.0f) * (usable ? 0.75f + 0.25f * hv : 0.3f), &fg);
		lrg_paint_list_icon (list, icon, roundf (bx + (STRIP_BUTTON_W - STRIP_BUTTON_ICON) * 0.5f),
		                     roundf (by + (bh - STRIP_BUTTON_ICON) * 0.5f), STRIP_BUTTON_ICON, 1.5f, &fg);
	}
}

/* paint_stack: surface, tab strip, tabs, indicator, active panel */
static void
paint_stack (LrgDockArea  *self,
             DockNode     *s,
             LrgPaintList *list)
{
	LrgWidget         *widget = LRG_WIDGET (self);
	LrgTheme          *theme = theme_of (self);
	gfloat             ox = lrg_widget_get_world_x (widget);
	gfloat             oy = lrg_widget_get_world_y (widget);
	gfloat             x = ox + s->x;
	gfloat             y = oy + s->y;
	gfloat             strip = strip_height (self);
	gfloat             bw = border_width (widget);
	g_autofree gfloat *xs = g_new (gfloat, MAX (1u, s->panels->len));
	g_autofree gfloat *ws = g_new (gfloat, MAX (1u, s->panels->len));
	LrgPartLook        look;
	LrgPartLook        header;
	LrgDockPanel      *active = stack_active_panel (s);
	StripInfo          info;
	guint              n;
	guint              i;

	if (s->w < 2.0f || s->h < 2.0f)
		return;

	/* 1. Surface and strip (rounded on top only) with a hairline */
	lrg_widget_resolve_look (widget, LRG_UI_PART_BOX, &look);
	lrg_widget_paint_box (widget, list, &look, x, y, s->w, s->h);
	lrg_widget_resolve_look (widget, LRG_UI_PART_HEADER, &header);
	header.radii.top_left = MIN (header.radii.top_left, look.radii.top_left);
	header.radii.top_right = MIN (header.radii.top_right, look.radii.top_right);
	header.radii.bottom_left = 0.0f;
	header.radii.bottom_right = 0.0f;
	lrg_widget_paint_box (widget, list, &header, x + bw, y + bw, MAX (0.0f, s->w - bw * 2.0f), strip - bw);
	lrg_paint_list_rect (list, x + bw, y + strip - 1.0f, MAX (0.0f, s->w - bw * 2.0f), 1.0f, 0.0f,
	                     lrg_theme_get_color (theme, LRG_THEME_COLOR_BORDER));

	/* 2. Tabs, clipped to the visible tab region when they overflow */
	n = stack_tabs_info (self, s, xs, ws, &info);
	if (info.overflow)
		lrg_paint_list_push_clip (list, ox + info.view_x, y, info.view_w, strip);
	else
		lrg_paint_list_push_clip (list, x + bw, y, MAX (0.0f, s->w - bw * 2.0f), strip);
	for (i = 0; i < n; i++)
	{
		LrgDockPanel *p = g_ptr_array_index (s->panels, i);
		gboolean      is_active = p == active;
		gfloat        tx = ox + xs[i];
		gfloat        ty = y + TAB_INSET;
		gfloat        th = strip - TAB_INSET * 2.0f;
		gfloat        hv = CLAMP (p->tab_hover.value, 0.0f, 1.0f);
		gboolean      lifted = self->dragging && self->drag_frame == NULL && p == self->drag_panel;
		gfloat        reserve = 0.0f;
		GrlColor      fg;

		/* The dragged tab is in the user's hand, not here */
		if (lifted)
			lrg_paint_list_push_opacity (list, 0.35f);

		tab_look (self, p, is_active, &look);
		lrg_widget_paint_box (widget, list, &look, tx, ty, ws[i], th);
		lrg_paint_color_with_alpha (&look.foreground, ((gfloat) look.foreground.a / 255.0f) * look.opacity, &fg);

		/* Close button: shown on the active and the hovered tab */
		if (p->closable)
		{
			gfloat cx;
			gfloat cy;
			gfloat show = MAX (is_active ? 1.0f : 0.0f, hv);

			reserve = CLOSE_SIZE + metric (widget, LRG_THEME_METRIC_PADDING_X) * 0.5f;
			close_rect (self, xs[i], s->y + TAB_INSET, ws[i], th, &cx, &cy);
			cx += ox;
			cy += oy;
			if (p->close_hover.value > 0.01f)
			{
				LrgPartLook item;

				lrg_widget_resolve_look (widget, LRG_UI_PART_ITEM, &item);
				lrg_paint_list_push_opacity (list, CLAMP (p->close_hover.value, 0.0f, 1.0f));
				lrg_widget_paint_box (widget, list, &item, cx, cy, CLOSE_SIZE, CLOSE_SIZE);
				lrg_paint_list_pop_opacity (list);
			}
			if (show > 0.01f)
			{
				GrlColor cc;

				lrg_paint_color_with_alpha (&fg, ((gfloat) fg.a / 255.0f) * show *
				                            (0.6f + 0.4f * CLAMP (p->close_hover.value, 0.0f, 1.0f)), &cc);
				paint_close_glyph (list, cx, cy, &cc);
			}
		}

		paint_title (widget, list, p, tx, ty, ws[i], th, &fg, reserve);
		if (lifted)
			lrg_paint_list_pop_opacity (list);
	}

	/* 3. Indicator, sliding on springs to the active tab */
	if (n > 0)
	{
		gfloat thick;

		lrg_widget_resolve_look (widget, LRG_UI_PART_INDICATOR, &look);
		thick = look.size > 0.0f ? look.size : INDICATOR_THICKNESS;
		if (s->ind_w.value > 1.0f)
			lrg_widget_paint_box (widget, list, &look,
			                      x + s->ind_x.value + (is_rtl (self) ? info.scroll : -info.scroll),
			                      y + strip - thick - 1.0f, s->ind_w.value, thick);
	}
	lrg_paint_list_pop_clip (list);

	/* 3b. Overflow: scroll arrows and the tab list button */
	if (info.overflow)
		paint_strip_buttons (self, s, &info, list);

	/* 4. The active panel, clipped to the content area */
	if (active != NULL)
	{
		lrg_paint_list_push_clip (list, x + bw, y + strip, MAX (0.0f, s->w - bw * 2.0f),
		                          MAX (0.0f, s->h - strip - bw));
		lrg_widget_paint (LRG_WIDGET (active), list);
		lrg_paint_list_pop_clip (list);
	}
}

/* paint_divider: the HANDLE look blended by the split's springs */
static void
paint_divider (LrgDockArea  *self,
               DockNode     *n,
               LrgPaintList *list)
{
	LrgWidget   *widget = LRG_WIDGET (self);
	gfloat       ox = lrg_widget_get_world_x (widget);
	gfloat       oy = lrg_widget_get_world_y (widget);
	gfloat       hover = CLAMP (n->hover.value, 0.0f, 1.0f);
	gfloat       press = CLAMP (n->press.value, 0.0f, 1.0f);
	gboolean     horizontal = n->orientation == LRG_ORIENTATION_HORIZONTAL;
	LrgPartLook  look;
	LrgPartLook  variant;
	gfloat       t;
	gfloat       gl;

	hover = MAX (hover, press);
	lrg_widget_resolve_look_for_state (widget, LRG_UI_PART_HANDLE, LRG_WIDGET_STATE_NONE, &look);
	if (hover > 0.001f)
	{
		lrg_widget_resolve_look_for_state (widget, LRG_UI_PART_HANDLE, LRG_WIDGET_STATE_HOVERED, &variant);
		lrg_part_look_lerp (&look, &variant, hover, &look);
	}
	if (press > 0.001f)
	{
		lrg_widget_resolve_look_for_state (widget, LRG_UI_PART_HANDLE,
		                                   LRG_WIDGET_STATE_HOVERED | LRG_WIDGET_STATE_PRESSED, &variant);
		lrg_part_look_lerp (&look, &variant, press, &look);
	}

	/* A line along the divider (invisible at rest: the gap between the
	 * stacks is the divider) and a grip pill in its middle */
	t = MAX (1.0f, look.size);
	if (horizontal)
	{
		gfloat cx = ox + n->dx + n->dw * 0.5f;

		gl = MIN (GRIP_LENGTH, n->dh);
		lrg_widget_paint_box (widget, list, &look, roundf (cx - t * 0.5f), oy + n->dy, t, n->dh);
		lrg_paint_list_rect (list, roundf (cx - 1.0f), oy + n->dy + (n->dh - gl) * 0.5f, 2.0f, gl,
		                     G_MAXFLOAT, &look.foreground);
	}
	else
	{
		gfloat cy = oy + n->dy + n->dh * 0.5f;

		gl = MIN (GRIP_LENGTH, n->dw);
		lrg_widget_paint_box (widget, list, &look, ox + n->dx, roundf (cy - t * 0.5f), n->dw, t);
		lrg_paint_list_rect (list, ox + n->dx + (n->dw - gl) * 0.5f, roundf (cy - 1.0f), gl, 2.0f,
		                     G_MAXFLOAT, &look.foreground);
	}
}

static void
paint_node (LrgDockArea  *self,
            DockNode     *n,
            LrgPaintList *list)
{
	if (n->is_split)
	{
		paint_node (self, n->a, list);
		paint_node (self, n->b, list);
		paint_divider (self, n, list);
	}
	else
		paint_stack (self, n, list);
}

static void
lrg_dock_area_paint (LrgWidget    *widget,
                     LrgPaintList *list)
{
	LrgDockArea *self = LRG_DOCK_AREA (widget);

	if (self->root == NULL)
	{
		LrgTheme       *theme = theme_of (self);
		const gchar    *text = "No panels";
		gfloat          size = lrg_theme_get_metric (theme, LRG_THEME_METRIC_FONT_SIZE);
		gfloat          tw = 0.0f;
		gfloat          th = 0.0f;
		GrlColor        c;

		/* Empty: a muted hint in the middle */
		lrg_paint_measure_text (lrg_theme_get_ui_font (theme), text, size, 1.0f, &tw, &th);
		lrg_paint_color_with_alpha (lrg_theme_get_color (theme, LRG_THEME_COLOR_TEXT_MUTED), 0.6f, &c);
		lrg_paint_list_text (list, lrg_theme_get_ui_font (theme), text,
		                     roundf (lrg_widget_get_world_x (widget) + (lrg_widget_get_width (widget) - tw) * 0.5f),
		                     roundf (lrg_widget_get_world_y (widget) + (lrg_widget_get_height (widget) - th) * 0.5f),
		                     size, 1.0f, &c);
		return;
	}
	paint_node (self, self->root, list);
}

/*
 * paint_compass_button:
 *
 * One drop target: a small card (MARK look, SELECTED when it is the
 * current target) with a glyph showing where the panel would go: a
 * frame with the side it would take filled in, the whole frame for
 * CENTER. @edge glyphs use a slimmer fill for the area edges.
 */
static void
paint_compass_button (LrgDockArea     *self,
                      LrgPaintList    *list,
                      gfloat           x,
                      gfloat           y,
                      LrgDockPosition  pos,
                      gboolean         selected,
                      gboolean         edge)
{
	LrgWidget   *widget = LRG_WIDGET (self);
	LrgPartLook  look;
	gfloat       g = 8.0f;
	gfloat       gx = x + g;
	gfloat       gy = y + g;
	gfloat       gs = COMPASS_BUTTON - g * 2.0f;
	gfloat       part = edge ? 0.3f : 0.5f;
	GrlColor     fill;

	lrg_widget_resolve_look_for_state (widget, LRG_UI_PART_MARK,
	                                   selected ? LRG_WIDGET_STATE_SELECTED : LRG_WIDGET_STATE_NONE, &look);
	lrg_widget_paint_box (widget, list, &look, x, y, COMPASS_BUTTON, COMPASS_BUTTON);

	lrg_paint_list_border (list, gx, gy, gs, gs, 2.0f, 1.5f, &look.foreground);
	lrg_paint_color_with_alpha (&look.foreground, ((gfloat) look.foreground.a / 255.0f) * 0.75f, &fill);
	switch (pos)
	{
	case LRG_DOCK_POSITION_LEFT:
		lrg_paint_list_rect (list, gx, gy, gs * part, gs, 2.0f, &fill);
		break;
	case LRG_DOCK_POSITION_RIGHT:
		lrg_paint_list_rect (list, gx + gs * (1.0f - part), gy, gs * part, gs, 2.0f, &fill);
		break;
	case LRG_DOCK_POSITION_TOP:
		lrg_paint_list_rect (list, gx, gy, gs, gs * part, 2.0f, &fill);
		break;
	case LRG_DOCK_POSITION_BOTTOM:
		lrg_paint_list_rect (list, gx, gy + gs * (1.0f - part), gs, gs * part, 2.0f, &fill);
		break;
	case LRG_DOCK_POSITION_CENTER:
	default:
		lrg_paint_list_rect (list, gx + 2.0f, gy + 2.0f, gs - 4.0f, gs - 4.0f, 1.0f, &fill);
		break;
	}
}

/*
 * paint_drag:
 *
 * The drag feedback, recorded by the overlay layer above everything:
 * the glass preview of the target region (springing between targets
 * and fading in and out), the edge targets, the compass over the
 * hovered stack (popping in on a spring) and the lifted tab's ghost.
 */
static void
paint_drag (LrgDockArea  *self,
            LrgPaintList *list)
{
	LrgWidget   *widget = LRG_WIDGET (self);
	gfloat       ox = lrg_widget_get_world_x (widget);
	gfloat       oy = lrg_widget_get_world_y (widget);
	gfloat       shown = CLAMP (self->preview_shown.value, 0.0f, 1.0f);
	gfloat       lift = CLAMP (self->lift.value, 0.0f, 1.2f);
	LrgPartLook  look;
	gfloat       bx;
	gfloat       by;
	guint        p;

	/* 1. Preview: centre and size on springs, slightly shrunk while it
	 * fades so it pops into place */
	if (shown > 0.01f)
	{
		gfloat inset = 4.0f;
		gfloat w = MAX (4.0f, self->preview_w.value * (0.9f + 0.1f * shown) - inset * 2.0f);
		gfloat h = MAX (4.0f, self->preview_h.value * (0.9f + 0.1f * shown) - inset * 2.0f);

		lrg_widget_resolve_look (widget, LRG_UI_PART_FILL, &look);
		lrg_paint_list_push_opacity (list, shown);
		lrg_widget_paint_box (widget, list, &look, ox + self->preview_cx.value - w * 0.5f,
		                      oy + self->preview_cy.value - h * 0.5f, w, h);
		lrg_paint_list_pop_opacity (list);
	}

	if (self->dragging)
	{
		gfloat c = CLAMP (self->compass_shown.value, 0.0f, 1.2f);

		/* 2. Area edge targets */
		if (edge_valid (self))
			for (p = LRG_DOCK_POSITION_LEFT; p <= LRG_DOCK_POSITION_BOTTOM; p++)
			{
				edge_rect (self, (LrgDockPosition) p, &bx, &by);
				paint_compass_button (self, list, ox + bx, oy + by, (LrgDockPosition) p,
				                      self->drop.kind == DROP_EDGE && self->drop.position == (LrgDockPosition) p,
				                      TRUE);
			}

		/* 3. Compass over the hovered stack (or the empty area) */
		if (self->root == NULL)
		{
			compass_rect (self, NULL, LRG_DOCK_POSITION_CENTER, &bx, &by);
			paint_compass_button (self, list, ox + bx, oy + by, LRG_DOCK_POSITION_CENTER,
			                      self->drop.kind == DROP_EMPTY, FALSE);
		}
		else if (self->hover_stack != NULL && any_compass_valid (self, self->hover_stack) && c > 0.01f)
		{
			guint  mark = lrg_paint_list_begin_group (list);
			gfloat cx;
			gfloat cy;

			for (p = LRG_DOCK_POSITION_LEFT; p <= LRG_DOCK_POSITION_CENTER; p++)
			{
				if (!compass_valid (self, self->hover_stack, (LrgDockPosition) p))
					continue;
				compass_rect (self, self->hover_stack, (LrgDockPosition) p, &bx, &by);
				paint_compass_button (self, list, ox + bx, oy + by, (LrgDockPosition) p,
				                      self->drop.kind == DROP_STACK && self->drop.stack == self->hover_stack &&
				                      self->drop.position == (LrgDockPosition) p, FALSE);
			}
			compass_rect (self, self->hover_stack, LRG_DOCK_POSITION_CENTER, &cx, &cy);
			lrg_paint_list_transform_group (list, mark, 0.7f + 0.3f * c,
			                                ox + cx + COMPASS_BUTTON * 0.5f, oy + cy + COMPASS_BUTTON * 0.5f,
			                                0.0f, 0.0f, MIN (1.0f, c));
		}
	}

	/* 4. Ghost of the lifted tab, trailing the pointer */
	if (self->ghost_panel != NULL && lift > 0.02f)
	{
		gfloat   w = tab_natural_width (self, self->ghost_panel) + 8.0f;
		gfloat   h = strip_height (self) - 2.0f;
		gfloat   gx = self->ghost_x.value;
		gfloat   gy = self->ghost_y.value;
		gfloat   speed = MIN (0.12f, (fabsf (self->ghost_x.velocity) + fabsf (self->ghost_y.velocity)) * 0.00008f);
		guint    mark = lrg_paint_list_begin_group (list);
		GrlColor fg;

		/* Hangs below-right of the pointer so the target under it
		 * stays visible; scales around the pointer as it lifts */
		lrg_widget_resolve_look (widget, LRG_UI_PART_POPUP, &look);
		lrg_widget_paint_box (widget, list, &look, gx + 10.0f, gy + 14.0f, w, h);
		lrg_paint_color_with_alpha (&look.foreground, ((gfloat) look.foreground.a / 255.0f), &fg);
		paint_title (widget, list, self->ghost_panel, gx + 10.0f, gy + 14.0f, w, h, &fg, 0.0f);
		lrg_paint_list_transform_group (list, mark, (0.6f + 0.4f * lift) * (1.0f + speed), gx, gy,
		                                0.0f, 0.0f, CLAMP (lift * 1.5f, 0.0f, 1.0f));
	}
}

/* ==========================================================================
 * Animation
 * ========================================================================== */

/* spring_go: aim, step and report whether a spring still moves */
static gboolean
spring_go (LrgSpring *s,
           gfloat     target,
           LrgMotion  motion,
           gfloat     dt)
{
	lrg_spring_set_motion (s, motion);
	lrg_spring_set_target (s, target);
	if (motion == LRG_MOTION_NONE)
	{
		lrg_spring_snap (s, target);
		return FALSE;
	}
	return lrg_spring_step (s, dt);
}

/* step_node: indicator, tab hover and divider springs of a subtree */
static gboolean
step_node (LrgDockArea *self,
           DockNode    *n,
           LrgMotion    motion,
           gfloat       dt)
{
	gboolean busy = FALSE;
	guint    i;

	if (n == NULL)
		return FALSE;
	if (n->is_split)
	{
		busy |= step_node (self, n->a, motion, dt);
		busy |= step_node (self, n->b, motion, dt);
		busy |= spring_go (&n->hover, (self->hot_split == n || self->drag_split == n) ? 1.0f : 0.0f, motion, dt);
		busy |= spring_go (&n->press, self->drag_split == n ? 1.0f : 0.0f,
		                   motion == LRG_MOTION_NONE ? motion : LRG_MOTION_SNAPPY, dt);
		return busy;
	}

	/* Indicator under the active tab; snaps until the stack is laid out */
	if (n->panels->len > 0)
	{
		g_autofree gfloat *xs = g_new (gfloat, n->panels->len);
		g_autofree gfloat *ws = g_new (gfloat, n->panels->len);
		guint              a = MIN (n->active, n->panels->len - 1);
		StripInfo          info;
		gfloat             inset;
		gfloat             tx;
		gfloat             tw;

		/* The strip scrolls on its own spring first; the indicator is
		 * kept unscrolled, so it rides along with the tabs rigidly */
		busy |= spring_go (&n->scroll, n->scroll.target,
		                   motion == LRG_MOTION_NONE ? motion : LRG_MOTION_SMOOTH, dt);
		stack_tabs_info (self, n, xs, ws, &info);
		inset = MIN (ws[a] * 0.15f, 10.0f);
		tx = xs[a] - n->x + (is_rtl (self) ? -info.scroll : info.scroll) + inset;
		tw = MAX (0.0f, ws[a] - inset * 2.0f);
		if (!n->ind_ready || motion == LRG_MOTION_NONE)
		{
			lrg_spring_snap (&n->ind_x, tx);
			lrg_spring_snap (&n->ind_w, tw);
			n->ind_ready = n->w > 0.0f;
		}
		else
		{
			busy |= spring_go (&n->ind_x, tx, motion == LRG_MOTION_NONE ? motion : LRG_MOTION_BOUNCY, dt);
			busy |= spring_go (&n->ind_w, tw, motion == LRG_MOTION_NONE ? motion : LRG_MOTION_BOUNCY, dt);
		}
	}

	/* Overflow buttons: hover highlight */
	for (i = 0; i < N_STRIP_BUTTONS; i++)
		busy |= spring_go (&n->button_hover[i],
		                   (!self->dragging && self->hot_button_stack == n && self->hot_button == (gint) i) ? 1.0f : 0.0f,
		                   motion, dt);

	for (i = 0; i < n->panels->len; i++)
	{
		LrgDockPanel *p = g_ptr_array_index (n->panels, i);
		gboolean      hot = !self->dragging && self->hot_panel == p;

		busy |= spring_go (&p->tab_hover, hot ? 1.0f : 0.0f, motion, dt);
		busy |= spring_go (&p->close_hover, hot && self->hot_close ? 1.0f : 0.0f,
		                   motion == LRG_MOTION_NONE ? motion : LRG_MOTION_SNAPPY, dt);
	}
	return busy;
}

/*
 * drag_autoscroll:
 *
 * While a tab is dragged with the pointer near either end of an
 * overflowing strip's visible region (or over its buttons), the strip
 * scrolls that way, so a tab can be dropped between tabs that were
 * out of view; the drop target is recomputed as the row moves.
 *
 * Returns: %TRUE while it scrolls
 */
static gboolean
drag_autoscroll (LrgDockArea *self,
                 gfloat       dt)
{
	gfloat    px = self->pointer_x - lrg_widget_get_world_x (LRG_WIDGET (self));
	gfloat    py = self->pointer_y - lrg_widget_get_world_y (LRG_WIDGET (self));
	DockNode *s = stack_at (self->root, px, py);
	StripInfo info;
	gfloat    dir = 0.0f;
	gfloat    target;

	if (s == NULL || py >= s->y + strip_height (self) || s->panels->len == 0 || dt <= 0.0f)
		return FALSE;
	strip_info (self, s, &info);
	if (!info.overflow)
		return FALSE;

	/* Near the physical left / right end of the region */
	if (px < info.view_x + AUTOSCROLL_ZONE)
		dir = -1.0f;
	else if (px > info.view_x + info.view_w - AUTOSCROLL_ZONE)
		dir = 1.0f;
	if (is_rtl (self))
		dir = -dir;
	if (dir == 0.0f)
		return FALSE;

	target = CLAMP (s->scroll.target + dir * AUTOSCROLL_SPEED * dt, 0.0f, info.max_scroll);
	if (target == s->scroll.target)
		return FALSE;
	lrg_spring_snap (&s->scroll, target);
	compute_drop (self, self->pointer_x, self->pointer_y);
	return TRUE;
}

/*
 * lrg_dock_area_step:
 *
 * Advances the canvas clock (double clicks), the tree's springs and
 * the drag feedback. Once a drag has ended and its feedback has faded,
 * the overlay layer is taken down.
 *
 * Returns: %TRUE while anything still moves
 */
static gboolean
lrg_dock_area_step (LrgWidget *widget,
                    gfloat     delta_time)
{
	LrgDockArea *self = LRG_DOCK_AREA (widget);
	LrgMotion    motion = lrg_widget_get_motion (widget);
	LrgMotion    lively = motion == LRG_MOTION_NONE ? LRG_MOTION_NONE : LRG_MOTION_BOUNCY;
	gboolean     previewing = self->dragging && self->drop.kind != DROP_NONE;
	gboolean     busy;

	if (delta_time > 0.0f)
		self->clock += (gdouble) delta_time;

	busy = step_node (self, self->root, motion, delta_time);

	if (self->layer == NULL)
		return busy;

	busy |= spring_go (&self->preview_shown, previewing ? 1.0f : 0.0f,
	                   previewing ? lively : (motion == LRG_MOTION_NONE ? motion : LRG_MOTION_SNAPPY), delta_time);
	if (previewing || self->preview_shown.value > 0.01f)
	{
		lrg_spring_set_motion (&self->preview_cx, lively);
		lrg_spring_set_motion (&self->preview_cy, lively);
		lrg_spring_set_motion (&self->preview_w, lively);
		lrg_spring_set_motion (&self->preview_h, lively);
		if (lively == LRG_MOTION_NONE)
		{
			lrg_spring_snap (&self->preview_cx, self->preview_cx.target);
			lrg_spring_snap (&self->preview_cy, self->preview_cy.target);
			lrg_spring_snap (&self->preview_w, self->preview_w.target);
			lrg_spring_snap (&self->preview_h, self->preview_h.target);
		}
		else
		{
			busy |= lrg_spring_step (&self->preview_cx, delta_time);
			busy |= lrg_spring_step (&self->preview_cy, delta_time);
			busy |= lrg_spring_step (&self->preview_w, delta_time);
			busy |= lrg_spring_step (&self->preview_h, delta_time);
		}
	}
	busy |= spring_go (&self->compass_shown, self->dragging && self->hover_stack != NULL ? 1.0f : 0.0f,
	                   lively, delta_time);
	busy |= spring_go (&self->lift, self->dragging ? 1.0f : 0.0f,
	                   self->dragging ? lively : (motion == LRG_MOTION_NONE ? motion : LRG_MOTION_SNAPPY),
	                   delta_time);
	if (self->dragging && self->drag_frame == NULL)
		busy |= drag_autoscroll (self, delta_time);
	if (self->dragging)
	{
		busy |= spring_go (&self->ghost_x, self->pointer_x, lively, delta_time);
		busy |= spring_go (&self->ghost_y, self->pointer_y, lively, delta_time);
	}

	/* Faded out: remove the feedback layer */
	if (!self->dragging && self->preview_shown.value <= 0.01f && self->lift.value <= 0.02f)
	{
		uninstall_layer (self);
		return busy;
	}
	return TRUE;
}

/* ==========================================================================
 * Layout
 * ========================================================================== */

/*
 * adopt_strays:
 *
 * Panels added with plain lrg_container_add_child() (e.g. by a UI
 * loader) are not in the tree yet: dock them as tabs of the active
 * stack.
 */
static void
adopt_strays (LrgDockArea *self)
{
	GList *l;

	for (l = lrg_container_get_children (LRG_CONTAINER (self)); l != NULL; l = l->next)
	{
		LrgDockPanel *p;

		if (!LRG_IS_DOCK_PANEL (l->data))
			continue;
		p = LRG_DOCK_PANEL (l->data);
		if (p->area == self)
			continue;
		p->area = self;
		insert_at (self, p, NULL, LRG_DOCK_POSITION_CENTER, -1, 0.5f);
		if (self->active_panel == NULL)
			set_active_panel (self, p);
		g_signal_emit (self, signals[SIGNAL_LAYOUT_CHANGED], 0);
	}
}

/*
 * reveal_pass:
 *
 * Keeps every strip's active tab in view: whenever a stack's active
 * tab, tab count or width changed since the last layout, its strip
 * scrolls the active tab into view (snapping on a stack's first
 * layout, gliding afterwards). Otherwise the scroll target is only
 * clamped to the current range, so wheel scrolling is left alone.
 */
static void
reveal_pass (LrgDockArea *self,
             DockNode    *n)
{
	if (n == NULL)
		return;
	if (n->is_split)
	{
		reveal_pass (self, n->a);
		reveal_pass (self, n->b);
		return;
	}
	if (n->panels->len == 0 || n->w <= 0.0f)
		return;

	if (n->active != n->reveal_active || n->panels->len != n->reveal_len || n->w != n->reveal_w)
	{
		gboolean first = n->reveal_w <= 0.0f;

		n->reveal_active = n->active;
		n->reveal_len = n->panels->len;
		n->reveal_w = n->w;
		stack_reveal (self, n, MIN (n->active, n->panels->len - 1), first);
	}
	else
	{
		StripInfo info;

		strip_info (self, n, &info);
		if (n->scroll.target > info.max_scroll || n->scroll.target < 0.0f)
			stack_scroll_to (self, n, n->scroll.target, FALSE);
	}
}

static void
lrg_dock_area_layout_children (LrgContainer *container)
{
	LrgDockArea *self = LRG_DOCK_AREA (container);
	LrgWidget   *w = LRG_WIDGET (self);

	if (self->in_layout || self->frames == NULL)
		return;
	self->in_layout = TRUE;

	adopt_strays (self);
	if (self->root != NULL)
	{
		layout_node (self, self->root, 0.0f, 0.0f, lrg_widget_get_width (w), lrg_widget_get_height (w));
		place_panels (self, self->root);
		reveal_pass (self, self->root);
	}

	self->in_layout = FALSE;
}

static void
lrg_dock_area_measure (LrgWidget *widget,
                       gfloat    *preferred_width,
                       gfloat    *preferred_height)
{
	LrgDockArea *self = LRG_DOCK_AREA (widget);

	*preferred_width = 0.0f;
	*preferred_height = 0.0f;
	if (self->root != NULL)
		node_min (self, self->root, preferred_width, preferred_height);
}

/* ==========================================================================
 * Events
 * ========================================================================== */

/* update_hot: hovered tab / close button / divider at a world point */
static void
update_hot (LrgDockArea *self,
            gfloat       wx,
            gfloat       wy)
{
	gfloat px = wx - lrg_widget_get_world_x (LRG_WIDGET (self));
	gfloat py = wy - lrg_widget_get_world_y (LRG_WIDGET (self));

	self->hot_split = split_at (self->root, px, py);
	self->hot_panel = self->hot_split == NULL ? tab_at (self, px, py, &self->hot_close) : NULL;
	if (self->hot_panel == NULL)
		self->hot_close = FALSE;
	self->hot_button = self->hot_split == NULL ? strip_button_at (self, px, py, &self->hot_button_stack) : -1;
	if (self->hot_button < 0)
		self->hot_button_stack = NULL;
}

/* clear_hot: nothing of the area is hovered */
static void
clear_hot (LrgDockArea *self)
{
	self->hot_panel = NULL;
	self->hot_split = NULL;
	self->hot_button = -1;
	self->hot_button_stack = NULL;
}

/* drag_split_to: live resize while a divider is dragged */
static void
drag_split_to (LrgDockArea *self,
               gfloat       wx,
               gfloat       wy)
{
	DockNode *n = self->drag_split;
	gboolean  horizontal = n->orientation == LRG_ORIENTATION_HORIZONTAL;
	gfloat    pos = horizontal ? wx - lrg_widget_get_world_x (LRG_WIDGET (self))
	                           : wy - lrg_widget_get_world_y (LRG_WIDGET (self));
	gfloat    avail = MAX (0.0f, (horizontal ? n->w : n->h) - self->divider_size);
	gfloat    first;

	if (avail <= 0.0f)
		return;
	first = split_first (self, n, pos - self->split_grab - (horizontal ? n->x : n->y));
	n->ratio = CLAMP (first / avail, 0.0f, 1.0f);
	lrg_widget_queue_layout (LRG_WIDGET (self));
	lrg_container_layout_children (LRG_CONTAINER (self));
}

/* switch_tab: Left / Right on the focused area cycle the active stack */
static gboolean
switch_tab (LrgDockArea *self,
            gint         delta)
{
	DockNode *s = self->active_panel != NULL ? find_stack (self->root, self->active_panel, NULL) : NULL;
	gint      next;

	if (s == NULL)
		s = first_stack (self->root);
	if (s == NULL || s->panels->len < 2)
		return FALSE;
	if (is_rtl (self))
		delta = -delta;
	next = (gint) s->active + delta;
	if (next < 0 || next >= (gint) s->panels->len)
		return TRUE;
	lrg_dock_area_present_panel (self, g_ptr_array_index (s->panels, next));
	return TRUE;
}

/*
 * lrg_dock_area_handle_event:
 *
 * On the area itself (tab strips and dividers; events bubbling up from
 * panel content are left alone): a press on a divider starts a live
 * resize (a double click resets it to equal halves); a press on a tab
 * activates it and arms a drag that lifts past DRAG_THRESHOLD; the
 * close button closes on release over it; a middle click closes a
 * closable tab. While dragging, moves update the drop target, release
 * drops, Escape / CANCEL cancel. Left / Right on the focused area
 * switch tabs.
 */
static gboolean
lrg_dock_area_handle_event (LrgWidget        *widget,
                            const LrgUIEvent *event)
{
	LrgDockArea *self = LRG_DOCK_AREA (widget);
	LrgWidget   *target = lrg_ui_event_get_target (event);
	gboolean     own = target == widget;
	gfloat       wx = lrg_ui_event_get_x (event);
	gfloat       wy = lrg_ui_event_get_y (event);
	gfloat       px = wx - lrg_widget_get_world_x (widget);
	gfloat       py = wy - lrg_widget_get_world_y (widget);
	gboolean     on_close = FALSE;
	LrgDockPanel *p;
	DockNode    *split;

	switch (lrg_ui_event_get_event_type (event))
	{
	case LRG_UI_EVENT_MOUSE_BUTTON_DOWN:
		if (!own)
			return FALSE;

		if (lrg_ui_event_get_button (event) == GRL_MOUSE_BUTTON_MIDDLE)
		{
			p = tab_at (self, px, py, &on_close);
			self->middle_press = p != NULL && p->closable ? p : NULL;
			return p != NULL;
		}
		if (lrg_ui_event_get_button (event) != GRL_MOUSE_BUTTON_LEFT)
			return FALSE;

		/* Divider: double click resets, otherwise a live drag */
		split = split_at (self->root, px, py);
		if (split != NULL)
		{
			if (self->click_split == split && self->clock - self->click_time <= DOUBLE_CLICK_TIME)
			{
				self->click_split = NULL;
				if (split->ratio != 0.5f)
				{
					split->ratio = 0.5f;
					tree_changed (self);
				}
				return TRUE;
			}
			self->click_split = split;
			self->click_time = self->clock;
			self->drag_split = split;
			self->split_start_ratio = split->ratio;
			self->split_grab = split->orientation == LRG_ORIENTATION_HORIZONTAL ? px - split->dx : py - split->dy;
			return TRUE;
		}

		/* Overflow buttons: scroll one tab, or the tab list */
		{
			DockNode *bs = NULL;
			gint      b = strip_button_at (self, px, py, &bs);

			if (b == LRG_DOCK_STRIP_BUTTON_LIST)
			{
				/* The press that just closed the list (the canvas
				 * closes popups first) must not reopen it */
				if (self->swallow_list_press)
					self->swallow_list_press = FALSE;
				else
					lrg_dock_area_popup_tab_list (self, stack_active_panel (bs));
				return TRUE;
			}
			if (b >= 0)
			{
				stack_step_scroll (self, bs, b == LRG_DOCK_STRIP_BUTTON_SCROLL_FORWARD);
				return TRUE;
			}
		}

		/* Tab: close button or activation (and a possible drag) */
		p = tab_at (self, px, py, &on_close);
		if (p == NULL)
			return TRUE;
		if (on_close && p->closable)
		{
			self->close_press = p;
			return TRUE;
		}
		lrg_dock_area_present_panel (self, p);
		self->press_panel = p;
		self->press_x = wx;
		self->press_y = wy;
		return TRUE;

	case LRG_UI_EVENT_MOUSE_MOVE:
		if (self->drag_split != NULL)
		{
			drag_split_to (self, wx, wy);
			return TRUE;
		}
		if (self->press_panel != NULL && !self->dragging &&
		    hypotf (wx - self->press_x, wy - self->press_y) > DRAG_THRESHOLD)
			begin_drag (self, self->press_panel, NULL, wx, wy);
		if (self->dragging && self->drag_frame == NULL)
		{
			drag_update (self, wx, wy);
			return TRUE;
		}
		self->swallow_list_press = FALSE;
		if (own)
			update_hot (self, wx, wy);
		else
			clear_hot (self);
		return FALSE;

	case LRG_UI_EVENT_POINTER_LEAVE:
		if (own)
			clear_hot (self);
		return FALSE;

	case LRG_UI_EVENT_SCROLL:
		/* The wheel over an overflowing strip scrolls its tabs */
		if (!own)
			return FALSE;
		{
			DockNode  *s = stack_at (self->root, px, py);
			StripInfo  info;
			gfloat     sx = lrg_ui_event_get_scroll_x (event);
			gfloat     sy = lrg_ui_event_get_scroll_y (event);
			gfloat     delta;

			if (s == NULL || py >= s->y + strip_height (self) || s->panels->len == 0)
				return FALSE;
			strip_info (self, s, &info);
			if (!info.overflow)
				return FALSE;

			/* Wheel down / right goes towards the last tab */
			delta = (-sy + (is_rtl (self) ? -sx : sx)) * WHEEL_STEP;
			if (delta != 0.0f)
				stack_scroll_to (self, s, s->scroll.target + delta, FALSE);
			return TRUE;
		}

	case LRG_UI_EVENT_MOUSE_BUTTON_UP:
		if (lrg_ui_event_get_button (event) == GRL_MOUSE_BUTTON_MIDDLE)
		{
			LrgDockPanel *pressed = self->middle_press;

			self->middle_press = NULL;
			p = tab_at (self, px, py, &on_close);
			if (pressed != NULL && p == pressed)
				lrg_dock_area_close_panel (self, p);
			return pressed != NULL;
		}
		if (lrg_ui_event_get_button (event) != GRL_MOUSE_BUTTON_LEFT)
			return FALSE;

		if (self->drag_split != NULL)
		{
			gboolean changed = self->drag_split->ratio != self->split_start_ratio;

			self->drag_split = NULL;
			if (changed)
				g_signal_emit (self, signals[SIGNAL_LAYOUT_CHANGED], 0);
			return TRUE;
		}
		if (self->close_press != NULL)
		{
			LrgDockPanel *pressed = self->close_press;

			self->close_press = NULL;
			p = tab_at (self, px, py, &on_close);
			if (p == pressed && on_close)
				lrg_dock_area_close_panel (self, p);
			return TRUE;
		}
		if (self->dragging && self->drag_frame == NULL)
		{
			drag_finish (self, wx, wy);
			return TRUE;
		}
		if (self->press_panel != NULL)
		{
			self->press_panel = NULL;
			return TRUE;
		}
		return FALSE;

	case LRG_UI_EVENT_KEY_DOWN:
		if (lrg_ui_event_get_key (event) == GRL_KEY_ESCAPE && self->dragging)
		{
			drag_cancel (self);
			return TRUE;
		}
		if (!own)
			return FALSE;
		if (lrg_ui_event_get_key (event) == GRL_KEY_LEFT)
			return switch_tab (self, -1);
		if (lrg_ui_event_get_key (event) == GRL_KEY_RIGHT)
			return switch_tab (self, 1);
		return FALSE;

	case LRG_UI_EVENT_CANCEL:
		if (self->dragging)
		{
			drag_cancel (self);
			return TRUE;
		}
		return FALSE;

	default:
		return FALSE;
	}
}

/* ==========================================================================
 * GObject
 * ========================================================================== */

/*
 * on_present_activate:
 *
 * A row of the tab list was picked: present the panel it listed (if
 * it is still docked here) and scroll its tab into view.
 */
static void
on_present_activate (GSimpleAction *action,
                     GVariant      *parameter,
                     gpointer       user_data)
{
	LrgDockArea  *self = LRG_DOCK_AREA (user_data);
	guint         index = g_variant_get_uint32 (parameter);
	LrgDockPanel *panel;
	DockNode     *s;
	guint         i;

	if (index >= self->menu_panels->len)
		return;
	panel = g_ptr_array_index (self->menu_panels, index);
	if (panel->area != self)
		return;
	lrg_dock_area_present_panel (self, panel);
	s = find_stack (self->root, panel, &i);
	if (s != NULL)
		stack_reveal (self, s, i, FALSE);
}

/*
 * on_area_state_changed:
 *
 * The canvas closes popups on a press outside them before delivering
 * that press. When the tab list closes (OPEN cleared on its owner, the
 * area) while the pointer is on a list button, that press must not
 * reopen it: remember to swallow it.
 */
static void
on_area_state_changed (LrgWidget      *widget,
                       LrgWidgetState  previous,
                       gpointer        user_data)
{
	LrgDockArea    *self = LRG_DOCK_AREA (widget);
	LrgWidgetState  now = lrg_widget_get_state_flags (widget);

	if ((previous & LRG_WIDGET_STATE_OPEN) && !(now & LRG_WIDGET_STATE_OPEN) &&
	    self->hot_button == LRG_DOCK_STRIP_BUTTON_LIST)
		self->swallow_list_press = TRUE;
}

static void
lrg_dock_area_dispose (GObject *object)
{
	LrgDockArea *self = LRG_DOCK_AREA (object);

	end_drag (self);
	uninstall_layer (self);

	/* Floating frames leave the overlay layer with their panels */
	if (self->frames != NULL)
	{
		while (self->frames->len > 0)
		{
			LrgDockFrame *f = g_ptr_array_index (self->frames, self->frames->len - 1);
			LrgContainer *parent = lrg_widget_get_parent (LRG_WIDGET (f));

			if (f->panel != NULL)
				f->panel->area = NULL;
			f->area = NULL;
			if (LRG_IS_CANVAS (parent))
				lrg_canvas_remove_overlay (LRG_CANVAS (parent), LRG_WIDGET (f));
			g_ptr_array_remove_index (self->frames, self->frames->len - 1);
		}
	}

	tree_free (self->root);
	self->root = NULL;
	self->active_panel = NULL;
	self->hot_button_stack = NULL;

	/* Tab list menu and its action group */
	if (self->tab_menu != NULL && lrg_menu_get_open (self->tab_menu))
		lrg_menu_close (self->tab_menu);
	g_clear_object (&self->tab_menu);
	if (self->menu_panels != NULL)
		g_ptr_array_set_size (self->menu_panels, 0);
	g_clear_object (&self->actions);

	G_OBJECT_CLASS (lrg_dock_area_parent_class)->dispose (object);
}

static void
lrg_dock_area_finalize (GObject *object)
{
	LrgDockArea *self = LRG_DOCK_AREA (object);

	g_clear_pointer (&self->frames, g_ptr_array_unref);
	g_clear_pointer (&self->menu_panels, g_ptr_array_unref);

	G_OBJECT_CLASS (lrg_dock_area_parent_class)->finalize (object);
}

static void
lrg_dock_area_get_property (GObject    *object,
                            guint       prop_id,
                            GValue     *value,
                            GParamSpec *pspec)
{
	LrgDockArea *self = LRG_DOCK_AREA (object);

	switch (prop_id)
	{
	case PROP_DIVIDER_SIZE:
		g_value_set_float (value, self->divider_size);
		break;
	case PROP_TAB_HEIGHT:
		g_value_set_float (value, self->tab_height);
		break;
	case PROP_MIN_PANEL_SIZE:
		g_value_set_float (value, self->min_panel_size);
		break;
	case PROP_ALLOW_FLOATING:
		g_value_set_boolean (value, self->allow_floating);
		break;
	case PROP_ACTIVE_PANEL:
		g_value_set_object (value, self->active_panel);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_dock_area_set_property (GObject      *object,
                            guint         prop_id,
                            const GValue *value,
                            GParamSpec   *pspec)
{
	LrgDockArea *self = LRG_DOCK_AREA (object);

	switch (prop_id)
	{
	case PROP_DIVIDER_SIZE:
		lrg_dock_area_set_divider_size (self, g_value_get_float (value));
		break;
	case PROP_TAB_HEIGHT:
		lrg_dock_area_set_tab_height (self, g_value_get_float (value));
		break;
	case PROP_MIN_PANEL_SIZE:
		lrg_dock_area_set_min_panel_size (self, g_value_get_float (value));
		break;
	case PROP_ALLOW_FLOATING:
		lrg_dock_area_set_allow_floating (self, g_value_get_boolean (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_dock_area_class_init (LrgDockAreaClass *klass)
{
	GObjectClass      *object_class = G_OBJECT_CLASS (klass);
	LrgWidgetClass    *widget_class = LRG_WIDGET_CLASS (klass);
	LrgContainerClass *container_class = LRG_CONTAINER_CLASS (klass);

	object_class->dispose = lrg_dock_area_dispose;
	object_class->finalize = lrg_dock_area_finalize;
	object_class->get_property = lrg_dock_area_get_property;
	object_class->set_property = lrg_dock_area_set_property;

	widget_class->measure = lrg_dock_area_measure;
	widget_class->paint = lrg_dock_area_paint;
	widget_class->step = lrg_dock_area_step;
	widget_class->style_defaults = lrg_dock_area_style_defaults;
	widget_class->handle_event = lrg_dock_area_handle_event;

	container_class->layout_children = lrg_dock_area_layout_children;

	/* The tree is physical (LEFT stays left); only the tab strips
	 * mirror, which the area does itself */
	lrg_widget_class_set_handles_direction (widget_class, TRUE);

	/**
	 * LrgDockArea:divider-size:
	 *
	 * Gap between neighbouring stacks, which is also the divider's grab
	 * zone.
	 */
	properties[PROP_DIVIDER_SIZE] =
		g_param_spec_float ("divider-size", "Divider size", "Gap between stacks",
		                    1.0f, G_MAXFLOAT, 6.0f,
		                    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgDockArea:tab-height:
	 *
	 * Height of every tab strip and floating title bar; 0 follows the
	 * theme's control height.
	 */
	properties[PROP_TAB_HEIGHT] =
		g_param_spec_float ("tab-height", "Tab height", "Tab strip height (0 = theme)",
		                    0.0f, G_MAXFLOAT, 0.0f,
		                    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgDockArea:min-panel-size:
	 *
	 * Smallest width and content height of a stack; dividers stop
	 * there. A panel's own minimum size raises it for its stack.
	 */
	properties[PROP_MIN_PANEL_SIZE] =
		g_param_spec_float ("min-panel-size", "Min panel size", "Smallest stack size",
		                    0.0f, G_MAXFLOAT, 48.0f,
		                    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgDockArea:allow-floating:
	 *
	 * Whether a tab dropped away from every target floats; without it
	 * such a drop is cancelled.
	 */
	properties[PROP_ALLOW_FLOATING] =
		g_param_spec_boolean ("allow-floating", "Allow floating", "Tabs can be torn off", TRUE,
		                      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgDockArea:active-panel:
	 *
	 * The most recently activated panel.
	 */
	properties[PROP_ACTIVE_PANEL] =
		g_param_spec_object ("active-panel", "Active panel", "Most recently activated panel",
		                     LRG_TYPE_DOCK_PANEL,
		                     G_PARAM_READABLE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	g_object_class_install_properties (object_class, N_PROPS, properties);

	/**
	 * LrgDockArea::close-request:
	 * @self: the dock area
	 * @panel: the panel about to close
	 *
	 * Emitted before a panel is closed by its close button, a middle
	 * click or lrg_dock_area_close_panel(). Return %TRUE to keep it
	 * open (e.g. to ask about unsaved changes first).
	 *
	 * Returns: %TRUE to veto the close
	 */
	signals[SIGNAL_CLOSE_REQUEST] =
		g_signal_new ("close-request",
		              G_TYPE_FROM_CLASS (klass),
		              G_SIGNAL_RUN_LAST,
		              0, g_signal_accumulator_true_handled, NULL, NULL,
		              G_TYPE_BOOLEAN, 1, LRG_TYPE_DOCK_PANEL);

	/**
	 * LrgDockArea::panel-closed:
	 * @self: the dock area
	 * @panel: the panel that was closed (already removed)
	 *
	 * Emitted after a close that was not vetoed.
	 */
	signals[SIGNAL_PANEL_CLOSED] =
		g_signal_new ("panel-closed",
		              G_TYPE_FROM_CLASS (klass),
		              G_SIGNAL_RUN_LAST,
		              0, NULL, NULL, NULL,
		              G_TYPE_NONE, 1, LRG_TYPE_DOCK_PANEL);

	/**
	 * LrgDockArea::panel-activated:
	 * @self: the dock area
	 * @panel: the newly active panel
	 *
	 * Emitted when the area's active panel changes (a tab clicked,
	 * lrg_dock_area_present_panel(), a panel dropped).
	 */
	signals[SIGNAL_PANEL_ACTIVATED] =
		g_signal_new ("panel-activated",
		              G_TYPE_FROM_CLASS (klass),
		              G_SIGNAL_RUN_LAST,
		              0, NULL, NULL, NULL,
		              G_TYPE_NONE, 1, LRG_TYPE_DOCK_PANEL);

	/**
	 * LrgDockArea::layout-changed:
	 * @self: the dock area
	 *
	 * Emitted after the arrangement changed: panels added, removed or
	 * moved, a divider released or reset, an active tab switched, a
	 * layout loaded. Save the layout here to persist it.
	 */
	signals[SIGNAL_LAYOUT_CHANGED] =
		g_signal_new ("layout-changed",
		              G_TYPE_FROM_CLASS (klass),
		              G_SIGNAL_RUN_LAST,
		              0, NULL, NULL, NULL,
		              G_TYPE_NONE, 0);
}

static void
lrg_dock_area_init (LrgDockArea *self)
{
	self->frames = g_ptr_array_new_with_free_func (g_object_unref);
	self->divider_size = 6.0f;
	self->min_panel_size = 48.0f;
	self->allow_floating = TRUE;
	self->hot_button = -1;
	lrg_widget_set_focusable (LRG_WIDGET (self), TRUE);

	/* The tab list menu runs "lrg-dock.present" with a row index */
	{
		g_autoptr(GSimpleAction) present = g_simple_action_new ("present", G_VARIANT_TYPE_UINT32);

		self->menu_panels = g_ptr_array_new_with_free_func (g_object_unref);
		self->actions = g_simple_action_group_new ();
		g_signal_connect_object (present, "activate", G_CALLBACK (on_present_activate), self, 0);
		g_action_map_add_action (G_ACTION_MAP (self->actions), G_ACTION (present));
		lrg_widget_insert_action_group (LRG_WIDGET (self), ACTION_PREFIX, G_ACTION_GROUP (self->actions));
		g_signal_connect (self, "state-changed", G_CALLBACK (on_area_state_changed), NULL);
	}

	lrg_spring_init (&self->preview_cx, 0.0f, LRG_MOTION_BOUNCY);
	lrg_spring_init (&self->preview_cy, 0.0f, LRG_MOTION_BOUNCY);
	lrg_spring_init (&self->preview_w, 0.0f, LRG_MOTION_BOUNCY);
	lrg_spring_init (&self->preview_h, 0.0f, LRG_MOTION_BOUNCY);
	lrg_spring_init (&self->preview_shown, 0.0f, LRG_MOTION_BOUNCY);
	lrg_spring_init (&self->compass_shown, 0.0f, LRG_MOTION_BOUNCY);
	lrg_spring_init (&self->ghost_x, 0.0f, LRG_MOTION_BOUNCY);
	lrg_spring_init (&self->ghost_y, 0.0f, LRG_MOTION_BOUNCY);
	lrg_spring_init (&self->lift, 0.0f, LRG_MOTION_BOUNCY);
}

/* ==========================================================================
 * Public API: panels
 * ========================================================================== */

LrgDockArea *
lrg_dock_area_new (void)
{
	return g_object_new (LRG_TYPE_DOCK_AREA, NULL);
}

/* resolve_target: the stack of a docked reference panel, or NULL */
static DockNode *
resolve_target (LrgDockArea  *self,
                LrgDockPanel *relative_to)
{
	if (relative_to == NULL)
		return NULL;
	return find_stack (self->root, relative_to, NULL);
}

void
lrg_dock_area_add_panel (LrgDockArea     *self,
                         LrgDockPanel    *panel,
                         LrgDockPanel    *relative_to,
                         LrgDockPosition  position)
{
	DockNode *target;

	g_return_if_fail (LRG_IS_DOCK_AREA (self));
	g_return_if_fail (LRG_IS_DOCK_PANEL (panel));
	g_return_if_fail (lrg_widget_get_parent (LRG_WIDGET (panel)) == NULL);
	g_return_if_fail (relative_to == NULL || LRG_IS_DOCK_PANEL (relative_to));

	target = resolve_target (self, relative_to);

	/* Mark it ours first so the container hook does not adopt it */
	panel->area = self;
	lrg_container_add_child (LRG_CONTAINER (self), LRG_WIDGET (panel));
	insert_at (self, panel, target, position, -1,
	           target != NULL ? 0.5f : EDGE_SHARE);
	tree_changed (self);
	if (self->active_panel == NULL)
		set_active_panel (self, panel);
}

void
lrg_dock_area_remove_panel (LrgDockArea  *self,
                            LrgDockPanel *panel)
{
	g_autoptr(LrgDockPanel) keep = NULL;
	LrgDockFrame           *frame;
	DockNode               *s;

	g_return_if_fail (LRG_IS_DOCK_AREA (self));
	g_return_if_fail (LRG_IS_DOCK_PANEL (panel));

	if (panel->area != self)
		return;
	keep = g_object_ref (panel);

	if (self->drag_panel == panel)
		drag_cancel (self);
	if (self->ghost_panel == panel)
		g_clear_object (&self->ghost_panel);

	s = find_stack (self->root, panel, NULL);
	if (s != NULL)
	{
		detach_from_stack (self, s, panel);
		lrg_container_remove_child (LRG_CONTAINER (self), LRG_WIDGET (panel));
	}
	else if ((frame = frame_get_panel_frame (self, panel)) != NULL)
	{
		/* Take the panel out of its frame, then drop the frame */
		unfloat (self, frame);
		lrg_container_remove_child (LRG_CONTAINER (self), LRG_WIDGET (panel));
	}
	else if (lrg_widget_get_parent (LRG_WIDGET (panel)) == LRG_CONTAINER (self))
		lrg_container_remove_child (LRG_CONTAINER (self), LRG_WIDGET (panel));

	lrg_widget_set_visible (LRG_WIDGET (panel), TRUE);
	forget_panel (self, panel);

	/* Another panel becomes the active one */
	if (self->active_panel == NULL && self->root != NULL)
		set_active_panel (self, stack_active_panel (first_stack (self->root)));

	tree_changed (self);
}

gboolean
lrg_dock_area_close_panel (LrgDockArea  *self,
                           LrgDockPanel *panel)
{
	g_autoptr(LrgDockPanel) keep = NULL;
	gboolean                veto = FALSE;

	g_return_val_if_fail (LRG_IS_DOCK_AREA (self), FALSE);
	g_return_val_if_fail (LRG_IS_DOCK_PANEL (panel), FALSE);

	if (panel->area != self)
		return FALSE;
	keep = g_object_ref (panel);

	g_signal_emit (self, signals[SIGNAL_CLOSE_REQUEST], 0, panel, &veto);
	if (veto || panel->area != self)
		return FALSE;

	lrg_dock_area_remove_panel (self, panel);
	g_signal_emit (self, signals[SIGNAL_PANEL_CLOSED], 0, panel);
	return TRUE;
}

void
lrg_dock_area_move_panel (LrgDockArea     *self,
                          LrgDockPanel    *panel,
                          LrgDockPanel    *relative_to,
                          LrgDockPosition  position)
{
	DockNode *target;

	g_return_if_fail (LRG_IS_DOCK_AREA (self));
	g_return_if_fail (LRG_IS_DOCK_PANEL (panel));
	g_return_if_fail (relative_to == NULL || LRG_IS_DOCK_PANEL (relative_to));

	if (panel->area != self)
		return;
	target = resolve_target (self, relative_to);
	move_internal (self, panel, target, position, -1, target != NULL ? 0.5f : EDGE_SHARE);
}

gboolean
lrg_dock_area_float_panel (LrgDockArea  *self,
                           LrgDockPanel *panel,
                           gfloat        x,
                           gfloat        y,
                           gfloat        width,
                           gfloat        height)
{
	LrgCanvas    *canvas;
	LrgDockFrame *frame;
	DockNode     *s;

	g_return_val_if_fail (LRG_IS_DOCK_AREA (self), FALSE);
	g_return_val_if_fail (LRG_IS_DOCK_PANEL (panel), FALSE);

	if (panel->area != self)
		return FALSE;

	/* Already floating: move and resize */
	frame = frame_get_panel_frame (self, panel);
	if (frame != NULL)
	{
		frame_move_to (frame, x, y, width, height);
		return TRUE;
	}

	canvas = canvas_of (self);
	s = find_stack (self->root, panel, NULL);
	if (canvas == NULL || !self->allow_floating || s == NULL)
		return FALSE;

	detach_from_stack (self, s, panel);
	float_detached (self, canvas, panel, x, y, width, height);
	tree_changed (self);
	set_active_panel (self, panel);
	return TRUE;
}

gboolean
lrg_dock_area_is_panel_floating (LrgDockArea  *self,
                                 LrgDockPanel *panel)
{
	g_return_val_if_fail (LRG_IS_DOCK_AREA (self), FALSE);
	g_return_val_if_fail (LRG_IS_DOCK_PANEL (panel), FALSE);
	return frame_get_panel_frame (self, panel) != NULL;
}

void
lrg_dock_area_present_panel (LrgDockArea  *self,
                             LrgDockPanel *panel)
{
	LrgDockFrame *frame;
	DockNode     *s;
	guint         i;
	gboolean      switched = FALSE;

	g_return_if_fail (LRG_IS_DOCK_AREA (self));
	g_return_if_fail (LRG_IS_DOCK_PANEL (panel));

	if (panel->area != self)
		return;

	s = find_stack (self->root, panel, &i);
	if (s != NULL && s->active != i)
	{
		s->active = i;
		switched = TRUE;
		tree_changed (self);
	}
	else if (s == NULL && (frame = frame_get_panel_frame (self, panel)) != NULL)
		raise_frame (self, frame);

	/* A tab brought forward counts as an activation even when it was
	 * already the area's active panel */
	if (self->active_panel != panel)
		set_active_panel (self, panel);
	else if (switched)
		g_signal_emit (self, signals[SIGNAL_PANEL_ACTIVATED], 0, panel);
}

LrgDockPanel *
lrg_dock_area_get_active_panel (LrgDockArea *self)
{
	g_return_val_if_fail (LRG_IS_DOCK_AREA (self), NULL);
	return self->active_panel;
}

static void
collect_panels (DockNode  *n,
                GList    **out)
{
	guint i;

	if (n == NULL)
		return;
	if (n->is_split)
	{
		collect_panels (n->a, out);
		collect_panels (n->b, out);
		return;
	}
	for (i = 0; i < n->panels->len; i++)
		*out = g_list_prepend (*out, g_ptr_array_index (n->panels, i));
}

GList *
lrg_dock_area_get_panels (LrgDockArea *self)
{
	GList *out = NULL;
	guint  i;

	g_return_val_if_fail (LRG_IS_DOCK_AREA (self), NULL);

	collect_panels (self->root, &out);
	for (i = 0; i < self->frames->len; i++)
	{
		LrgDockFrame *f = g_ptr_array_index (self->frames, i);

		if (f->panel != NULL)
			out = g_list_prepend (out, f->panel);
	}
	return g_list_reverse (out);
}

LrgDockPanel *
lrg_dock_area_get_panel (LrgDockArea *self,
                         const gchar *id)
{
	g_autoptr(GList) all = NULL;
	GList           *l;

	g_return_val_if_fail (LRG_IS_DOCK_AREA (self), NULL);
	g_return_val_if_fail (id != NULL, NULL);

	all = lrg_dock_area_get_panels (self);
	for (l = all; l != NULL; l = l->next)
		if (g_strcmp0 (LRG_DOCK_PANEL (l->data)->id, id) == 0)
			return l->data;
	return NULL;
}

GList *
lrg_dock_area_get_stack_panels (LrgDockArea  *self,
                                LrgDockPanel *panel)
{
	DockNode *s;
	GList    *out = NULL;
	guint     i;

	g_return_val_if_fail (LRG_IS_DOCK_AREA (self), NULL);
	g_return_val_if_fail (LRG_IS_DOCK_PANEL (panel), NULL);

	s = find_stack (self->root, panel, NULL);
	if (s == NULL)
		return NULL;
	for (i = s->panels->len; i > 0; i--)
		out = g_list_prepend (out, g_ptr_array_index (s->panels, i - 1));
	return out;
}

gboolean
lrg_dock_area_get_stack_rect (LrgDockArea  *self,
                              LrgDockPanel *panel,
                              gfloat       *x,
                              gfloat       *y,
                              gfloat       *width,
                              gfloat       *height)
{
	DockNode *s;

	g_return_val_if_fail (LRG_IS_DOCK_AREA (self), FALSE);
	g_return_val_if_fail (LRG_IS_DOCK_PANEL (panel), FALSE);

	s = find_stack (self->root, panel, NULL);
	if (s == NULL)
		return FALSE;
	if (x != NULL)
		*x = lrg_widget_get_world_x (LRG_WIDGET (self)) + s->x;
	if (y != NULL)
		*y = lrg_widget_get_world_y (LRG_WIDGET (self)) + s->y;
	if (width != NULL)
		*width = s->w;
	if (height != NULL)
		*height = s->h;
	return TRUE;
}

gboolean
lrg_dock_area_get_tab_rect (LrgDockArea  *self,
                            LrgDockPanel *panel,
                            gfloat       *x,
                            gfloat       *y,
                            gfloat       *width,
                            gfloat       *height)
{
	g_autofree gfloat *xs = NULL;
	g_autofree gfloat *ws = NULL;
	DockNode          *s;
	guint              i;

	g_return_val_if_fail (LRG_IS_DOCK_AREA (self), FALSE);
	g_return_val_if_fail (LRG_IS_DOCK_PANEL (panel), FALSE);

	s = find_stack (self->root, panel, &i);
	if (s == NULL)
		return FALSE;
	xs = g_new (gfloat, s->panels->len);
	ws = g_new (gfloat, s->panels->len);
	stack_tabs (self, s, xs, ws);
	if (x != NULL)
		*x = lrg_widget_get_world_x (LRG_WIDGET (self)) + xs[i];
	if (y != NULL)
		*y = lrg_widget_get_world_y (LRG_WIDGET (self)) + s->y + TAB_INSET;
	if (width != NULL)
		*width = ws[i];
	if (height != NULL)
		*height = strip_height (self) - TAB_INSET * 2.0f;
	return TRUE;
}

/* ==========================================================================
 * Public API: tab strip overflow
 * ========================================================================== */

/* panel_stack: the stack holding @panel, laid out (NULL if floating) */
static DockNode *
panel_stack (LrgDockArea  *self,
             LrgDockPanel *panel,
             guint        *index)
{
	return find_stack (self->root, panel, index);
}

gboolean
lrg_dock_area_get_tabs_overflow (LrgDockArea  *self,
                                 LrgDockPanel *panel)
{
	DockNode  *s;
	StripInfo  info;

	g_return_val_if_fail (LRG_IS_DOCK_AREA (self), FALSE);
	g_return_val_if_fail (LRG_IS_DOCK_PANEL (panel), FALSE);

	s = panel_stack (self, panel, NULL);
	if (s == NULL)
		return FALSE;
	strip_info (self, s, &info);
	return info.overflow;
}

gfloat
lrg_dock_area_get_tab_scroll (LrgDockArea  *self,
                              LrgDockPanel *panel)
{
	DockNode  *s;
	StripInfo  info;

	g_return_val_if_fail (LRG_IS_DOCK_AREA (self), 0.0f);
	g_return_val_if_fail (LRG_IS_DOCK_PANEL (panel), 0.0f);

	s = panel_stack (self, panel, NULL);
	if (s == NULL)
		return 0.0f;
	strip_info (self, s, &info);
	return info.scroll;
}

gfloat
lrg_dock_area_get_tab_scroll_max (LrgDockArea  *self,
                                  LrgDockPanel *panel)
{
	DockNode  *s;
	StripInfo  info;

	g_return_val_if_fail (LRG_IS_DOCK_AREA (self), 0.0f);
	g_return_val_if_fail (LRG_IS_DOCK_PANEL (panel), 0.0f);

	s = panel_stack (self, panel, NULL);
	if (s == NULL)
		return 0.0f;
	strip_info (self, s, &info);
	return info.max_scroll;
}

void
lrg_dock_area_set_tab_scroll (LrgDockArea  *self,
                              LrgDockPanel *panel,
                              gfloat        offset)
{
	DockNode *s;

	g_return_if_fail (LRG_IS_DOCK_AREA (self));
	g_return_if_fail (LRG_IS_DOCK_PANEL (panel));

	s = panel_stack (self, panel, NULL);
	if (s != NULL)
		stack_scroll_to (self, s, offset, TRUE);
}

void
lrg_dock_area_scroll_tabs (LrgDockArea  *self,
                           LrgDockPanel *panel,
                           gfloat        delta)
{
	DockNode *s;

	g_return_if_fail (LRG_IS_DOCK_AREA (self));
	g_return_if_fail (LRG_IS_DOCK_PANEL (panel));

	s = panel_stack (self, panel, NULL);
	if (s != NULL)
		stack_scroll_to (self, s, s->scroll.target + delta, FALSE);
}

gboolean
lrg_dock_area_get_tab_visible (LrgDockArea  *self,
                               LrgDockPanel *panel)
{
	g_autofree gfloat *xs = NULL;
	g_autofree gfloat *ws = NULL;
	DockNode          *s;
	StripInfo          info;
	guint              i;

	g_return_val_if_fail (LRG_IS_DOCK_AREA (self), FALSE);
	g_return_val_if_fail (LRG_IS_DOCK_PANEL (panel), FALSE);

	s = panel_stack (self, panel, &i);
	if (s == NULL)
		return FALSE;
	xs = g_new (gfloat, s->panels->len);
	ws = g_new (gfloat, s->panels->len);
	stack_tabs_info (self, s, xs, ws, &info);
	if (!info.overflow)
		return TRUE;
	return xs[i] >= info.view_x - 0.5f && xs[i] + ws[i] <= info.view_x + info.view_w + 0.5f;
}

gboolean
lrg_dock_area_get_strip_button_rect (LrgDockArea        *self,
                                     LrgDockPanel       *panel,
                                     LrgDockStripButton  button,
                                     gfloat             *x,
                                     gfloat             *y,
                                     gfloat             *width,
                                     gfloat             *height)
{
	DockNode  *s;
	StripInfo  info;

	g_return_val_if_fail (LRG_IS_DOCK_AREA (self), FALSE);
	g_return_val_if_fail (LRG_IS_DOCK_PANEL (panel), FALSE);
	g_return_val_if_fail ((guint) button < N_STRIP_BUTTONS, FALSE);

	s = panel_stack (self, panel, NULL);
	if (s == NULL)
		return FALSE;
	strip_info (self, s, &info);
	if (!info.overflow)
		return FALSE;
	if (x != NULL)
		*x = lrg_widget_get_world_x (LRG_WIDGET (self)) + info.button_x[button];
	if (y != NULL)
		*y = lrg_widget_get_world_y (LRG_WIDGET (self)) + s->y + TAB_INSET;
	if (width != NULL)
		*width = STRIP_BUTTON_W;
	if (height != NULL)
		*height = strip_height (self) - TAB_INSET * 2.0f;
	return TRUE;
}

gboolean
lrg_dock_area_popup_tab_list (LrgDockArea  *self,
                              LrgDockPanel *panel)
{
	g_autoptr(GMenu) model = NULL;
	DockNode        *s;
	StripInfo        info;
	gfloat           ax;
	gfloat           ay;
	gfloat           aw;
	gfloat           ah;
	guint            i;

	g_return_val_if_fail (LRG_IS_DOCK_AREA (self), FALSE);
	g_return_val_if_fail (LRG_IS_DOCK_PANEL (panel), FALSE);

	s = panel_stack (self, panel, NULL);
	if (s == NULL || canvas_of (self) == NULL)
		return FALSE;

	/* One row per tab: icon and full title, running present(i) */
	model = g_menu_new ();
	g_ptr_array_set_size (self->menu_panels, 0);
	for (i = 0; i < s->panels->len; i++)
	{
		LrgDockPanel        *p = g_ptr_array_index (s->panels, i);
		g_autoptr(GMenuItem) item = g_menu_item_new (p->title != NULL && p->title[0] != '\0' ? p->title
		                                             : (p->id != NULL ? p->id : "?"), NULL);

		g_menu_item_set_action_and_target_value (item, ACTION_PREFIX ".present", g_variant_new_uint32 (i));
		if (panel_icon (p) != LRG_ICON_NONE)
			g_menu_item_set_attribute (item, G_MENU_ATTRIBUTE_ICON, "s", p->icon);
		g_menu_append_item (model, item);
		g_ptr_array_add (self->menu_panels, g_object_ref (p));
	}

	if (self->tab_menu == NULL)
		self->tab_menu = lrg_menu_new ();
	lrg_menu_set_model (self->tab_menu, G_MENU_MODEL (model));

	/* Under the list button, else under the strip's trailing end */
	strip_info (self, s, &info);
	aw = STRIP_BUTTON_W;
	ah = strip_height (self) - TAB_INSET * 2.0f;
	if (info.overflow)
		ax = info.button_x[LRG_DOCK_STRIP_BUTTON_LIST];
	else
		ax = is_rtl (self) ? s->x + STRIP_PAD : s->x + s->w - STRIP_PAD - aw;
	ay = s->y + TAB_INSET;
	lrg_menu_popup_at_rect (self->tab_menu, LRG_WIDGET (self), lrg_widget_get_world_x (LRG_WIDGET (self)) + ax,
	                        lrg_widget_get_world_y (LRG_WIDGET (self)) + ay, aw, ah, LRG_POPUP_PLACEMENT_BELOW);
	lrg_menu_set_highlighted (self->tab_menu, (gint) MIN (s->active, s->panels->len - 1));
	return lrg_menu_get_open (self->tab_menu);
}

LrgMenu *
lrg_dock_area_get_tab_list_menu (LrgDockArea *self)
{
	g_return_val_if_fail (LRG_IS_DOCK_AREA (self), NULL);

	return self->tab_menu;
}

gfloat
lrg_dock_area_get_panel_share (LrgDockArea  *self,
                               LrgDockPanel *panel)
{
	DockNode *s;
	DockNode *split;

	g_return_val_if_fail (LRG_IS_DOCK_AREA (self), 0.0f);
	g_return_val_if_fail (LRG_IS_DOCK_PANEL (panel), 0.0f);

	s = find_stack (self->root, panel, NULL);
	if (s == NULL)
		return 0.0f;
	split = s->parent;
	if (split == NULL)
		return 1.0f;
	return split->a == s ? split->ratio : 1.0f - split->ratio;
}

void
lrg_dock_area_set_panel_share (LrgDockArea  *self,
                               LrgDockPanel *panel,
                               gfloat        share)
{
	DockNode *s;
	DockNode *split;
	gfloat    ratio;

	g_return_if_fail (LRG_IS_DOCK_AREA (self));
	g_return_if_fail (LRG_IS_DOCK_PANEL (panel));

	s = find_stack (self->root, panel, NULL);
	if (s == NULL || s->parent == NULL)
		return;
	split = s->parent;
	share = CLAMP (share, 0.0f, 1.0f);
	ratio = split->a == s ? share : 1.0f - share;
	if (split->ratio == ratio)
		return;
	split->ratio = ratio;
	tree_changed (self);
}

/* describe_node: the text form of lrg_dock_area_describe() */
static void
describe_node (DockNode *n,
               GString  *out)
{
	gchar buf[G_ASCII_DTOSTR_BUF_SIZE];
	guint i;

	if (n->is_split)
	{
		g_string_append (out, n->orientation == LRG_ORIENTATION_HORIZONTAL ? "H(" : "V(");
		g_string_append (out, g_ascii_formatd (buf, sizeof buf, "%.2f", (gdouble) n->ratio));
		g_string_append_c (out, ',');
		describe_node (n->a, out);
		g_string_append_c (out, ',');
		describe_node (n->b, out);
		g_string_append_c (out, ')');
		return;
	}

	g_string_append_c (out, '[');
	for (i = 0; i < n->panels->len; i++)
	{
		LrgDockPanel *p = g_ptr_array_index (n->panels, i);

		if (i > 0)
			g_string_append_c (out, ',');
		g_string_append (out, p->id != NULL ? p->id : "?");
		if (i == n->active)
			g_string_append_c (out, '*');
	}
	g_string_append_c (out, ']');
}

gchar *
lrg_dock_area_describe (LrgDockArea *self)
{
	GString *out;
	guint    i;

	g_return_val_if_fail (LRG_IS_DOCK_AREA (self), NULL);

	out = g_string_new (NULL);
	if (self->root == NULL)
		g_string_append (out, "()");
	else
		describe_node (self->root, out);
	for (i = 0; i < self->frames->len; i++)
	{
		LrgDockFrame *f = g_ptr_array_index (self->frames, i);

		if (f->panel != NULL)
			g_string_append_printf (out, "+float[%s]", f->panel->id != NULL ? f->panel->id : "?");
	}
	return g_string_free (out, FALSE);
}

gboolean
lrg_dock_area_get_dragging (LrgDockArea *self)
{
	g_return_val_if_fail (LRG_IS_DOCK_AREA (self), FALSE);
	return self->dragging;
}

/* ==========================================================================
 * Public API: persistence
 * ========================================================================== */

static void
save_node (DockNode    *n,
           YamlBuilder *b)
{
	guint i;

	yaml_builder_begin_mapping (b);
	if (n->is_split)
	{
		yaml_builder_set_member_name (b, "split");
		yaml_builder_add_string_value (b, n->orientation == LRG_ORIENTATION_HORIZONTAL
		                                  ? "horizontal" : "vertical");
		yaml_builder_set_member_name (b, "ratio");
		yaml_builder_add_double_value (b, roundf (n->ratio * 10000.0f) / 10000.0);
		yaml_builder_set_member_name (b, "first");
		save_node (n->a, b);
		yaml_builder_set_member_name (b, "second");
		save_node (n->b, b);
	}
	else
	{
		LrgDockPanel *active = stack_active_panel (n);

		yaml_builder_set_member_name (b, "tabs");
		yaml_builder_begin_sequence (b);
		for (i = 0; i < n->panels->len; i++)
		{
			LrgDockPanel *p = g_ptr_array_index (n->panels, i);

			if (p->id != NULL)
				yaml_builder_add_string_value (b, p->id);
		}
		yaml_builder_end_sequence (b);
		if (active != NULL && active->id != NULL)
		{
			yaml_builder_set_member_name (b, "active");
			yaml_builder_add_string_value (b, active->id);
		}
	}
	yaml_builder_end_mapping (b);
}

gchar *
lrg_dock_area_save_layout (LrgDockArea *self)
{
	g_autoptr(YamlBuilder)   builder = NULL;
	g_autoptr(YamlDocument)  doc = NULL;
	g_autoptr(YamlGenerator) generator = NULL;
	g_autoptr(GError)        error = NULL;
	gchar                   *out;
	guint                    i;

	g_return_val_if_fail (LRG_IS_DOCK_AREA (self), NULL);

	builder = yaml_builder_new ();
	yaml_builder_begin_mapping (builder);

	yaml_builder_set_member_name (builder, "version");
	yaml_builder_add_int_value (builder, 1);

	yaml_builder_set_member_name (builder, "root");
	if (self->root != NULL)
		save_node (self->root, builder);
	else
		yaml_builder_add_null_value (builder);

	if (self->active_panel != NULL && self->active_panel->id != NULL)
	{
		yaml_builder_set_member_name (builder, "active");
		yaml_builder_add_string_value (builder, self->active_panel->id);
	}

	yaml_builder_set_member_name (builder, "floating");
	yaml_builder_begin_sequence (builder);
	for (i = 0; i < self->frames->len; i++)
	{
		LrgDockFrame *f = g_ptr_array_index (self->frames, i);
		LrgWidget    *fw = LRG_WIDGET (f);

		if (f->panel == NULL || f->panel->id == NULL)
			continue;
		yaml_builder_begin_mapping (builder);
		yaml_builder_set_member_name (builder, "id");
		yaml_builder_add_string_value (builder, f->panel->id);
		yaml_builder_set_member_name (builder, "x");
		yaml_builder_add_double_value (builder, lrg_widget_get_world_x (fw));
		yaml_builder_set_member_name (builder, "y");
		yaml_builder_add_double_value (builder, lrg_widget_get_world_y (fw));
		yaml_builder_set_member_name (builder, "width");
		yaml_builder_add_double_value (builder, lrg_widget_get_width (fw));
		yaml_builder_set_member_name (builder, "height");
		yaml_builder_add_double_value (builder, lrg_widget_get_height (fw));
		yaml_builder_end_mapping (builder);
	}
	yaml_builder_end_sequence (builder);

	yaml_builder_end_mapping (builder);

	doc = yaml_builder_get_document (builder);
	generator = yaml_generator_new ();
	yaml_generator_set_document (generator, doc);
	out = yaml_generator_to_data (generator, NULL, &error);
	if (out == NULL)
	{
		lrg_warning (LRG_LOG_DOMAIN, "dock layout: could not write YAML: %s",
		             error != NULL ? error->message : "unknown error");
		return g_strdup ("");
	}
	return out;
}

/* find_known: a panel of the area by id (docked or floating), unused */
static LrgDockPanel *
find_known (GList       *known,
            GHashTable  *used,
            const gchar *id)
{
	GList *l;

	if (id == NULL)
		return NULL;
	for (l = known; l != NULL; l = l->next)
	{
		LrgDockPanel *p = l->data;

		if (g_strcmp0 (p->id, id) == 0 && !g_hash_table_contains (used, p))
			return p;
	}
	return NULL;
}

/*
 * load_node:
 *
 * Builds a detached subtree from YAML, referring to existing panels.
 * Unknown ids are skipped; a stack with no known panel and a split
 * with an empty side collapse. Structural errors set @error.
 *
 * Returns: (nullable): the subtree, %NULL when empty or on error
 */
static DockNode *
load_node (YamlNode    *node,
           GList       *known,
           GHashTable  *used,
           gint         depth,
           GError     **error)
{
	YamlMapping *map;
	YamlNode    *member;

	if (depth > MAX_DEPTH)
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "dock layout nested too deeply");
		return NULL;
	}
	if (node == NULL || yaml_node_get_node_type (node) != YAML_NODE_MAPPING)
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "dock layout node is not a mapping");
		return NULL;
	}
	map = yaml_node_get_mapping (node);

	/* Split */
	member = yaml_mapping_get_member (map, "split");
	if (member != NULL)
	{
		const gchar    *kind = yaml_node_get_node_type (member) == YAML_NODE_SCALAR
		                       ? yaml_node_get_string (member) : NULL;
		LrgOrientation  o;
		gdouble         ratio = 0.5;
		DockNode       *a;
		DockNode       *b;

		if (g_strcmp0 (kind, "horizontal") == 0)
			o = LRG_ORIENTATION_HORIZONTAL;
		else if (g_strcmp0 (kind, "vertical") == 0)
			o = LRG_ORIENTATION_VERTICAL;
		else
		{
			g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
			             "dock layout split must be horizontal or vertical");
			return NULL;
		}

		member = yaml_mapping_get_member (map, "ratio");
		if (member != NULL && yaml_node_get_node_type (member) == YAML_NODE_SCALAR)
			ratio = yaml_node_get_double (member);
		if (!isfinite (ratio))
			ratio = 0.5;

		a = load_node (yaml_mapping_get_member (map, "first"), known, used, depth + 1, error);
		if (error != NULL && *error != NULL)
			return NULL;
		b = load_node (yaml_mapping_get_member (map, "second"), known, used, depth + 1, error);
		if (error != NULL && *error != NULL)
		{
			tree_free (a);
			return NULL;
		}

		/* An empty side collapses into the other */
		if (a == NULL || b == NULL)
			return a != NULL ? a : b;
		return split_new (o, CLAMP ((gfloat) ratio, 0.05f, 0.95f), a, b);
	}

	/* Stack */
	member = yaml_mapping_get_member (map, "tabs");
	if (member != NULL && yaml_node_get_node_type (member) == YAML_NODE_SEQUENCE)
	{
		YamlSequence *seq = yaml_node_get_sequence (member);
		const gchar  *active_id = NULL;
		DockNode     *s = stack_new (NULL);
		guint         n = yaml_sequence_get_length (seq);
		guint         i;

		member = yaml_mapping_get_member (map, "active");
		if (member != NULL && yaml_node_get_node_type (member) == YAML_NODE_SCALAR)
			active_id = yaml_node_get_string (member);

		for (i = 0; i < n; i++)
		{
			YamlNode     *e = yaml_sequence_get_element (seq, i);
			LrgDockPanel *p;

			if (e == NULL || yaml_node_get_node_type (e) != YAML_NODE_SCALAR)
				continue;
			p = find_known (known, used, yaml_node_get_string (e));
			if (p == NULL)
				continue;
			g_hash_table_add (used, p);
			if (g_strcmp0 (p->id, active_id) == 0)
				s->active = s->panels->len;
			g_ptr_array_add (s->panels, p);
		}

		if (s->panels->len == 0)
		{
			node_free (s);
			return NULL;
		}
		return s;
	}

	g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "dock layout node has neither split nor tabs");
	return NULL;
}

/* append_quiet: a last tab of the first stack, keeping its active tab */
static void
append_quiet (LrgDockArea  *self,
              LrgDockPanel *panel)
{
	DockNode *s = first_stack (self->root);
	guint     keep;

	if (s == NULL)
	{
		insert_at (self, panel, NULL, LRG_DOCK_POSITION_CENTER, -1, 0.5f);
		return;
	}
	keep = s->active;
	stack_insert (s, panel, -1);
	s->active = keep;
}

/* FloatRec: a floating entry of a layout being loaded */
typedef struct
{
	LrgDockPanel *panel;
	gfloat        x;
	gfloat        y;
	gfloat        w;
	gfloat        h;
} FloatRec;

/* scalar_double: a numeric member of a mapping, or @fallback */
static gfloat
scalar_double (YamlMapping *map,
               const gchar *name,
               gfloat       fallback)
{
	YamlNode *n = yaml_mapping_get_member (map, name);
	gdouble   v;

	if (n == NULL || yaml_node_get_node_type (n) != YAML_NODE_SCALAR)
		return fallback;
	v = yaml_node_get_double (n);
	return isfinite (v) ? (gfloat) v : fallback;
}

gboolean
lrg_dock_area_load_layout (LrgDockArea  *self,
                           const gchar  *yaml,
                           GError      **error)
{
	g_autoptr(YamlParser) parser = NULL;
	g_autoptr(GHashTable) used = NULL;
	g_autoptr(GList)      known = NULL;
	g_autoptr(GPtrArray)  floats = NULL;
	YamlNode             *root;
	YamlMapping          *map;
	YamlNode             *member;
	DockNode             *tree = NULL;
	GError               *local = NULL;
	LrgCanvas            *canvas;
	LrgDockPanel         *wanted_active = NULL;
	GList                *l;
	guint                 i;

	g_return_val_if_fail (LRG_IS_DOCK_AREA (self), FALSE);
	g_return_val_if_fail (yaml != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* 1. Parse and build the new tree without touching the area */
	parser = yaml_parser_new ();
	if (!yaml_parser_load_from_data (parser, yaml, -1, error))
		return FALSE;
	root = yaml_parser_get_root (parser);
	if (root == NULL || yaml_node_get_node_type (root) != YAML_NODE_MAPPING)
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "dock layout is not a mapping");
		return FALSE;
	}
	map = yaml_node_get_mapping (root);

	known = lrg_dock_area_get_panels (self);
	used = g_hash_table_new (NULL, NULL);
	member = yaml_mapping_get_member (map, "root");
	if (member != NULL && yaml_node_get_node_type (member) != YAML_NODE_NULL)
	{
		tree = load_node (member, known, used, 0, &local);
		if (local != NULL)
		{
			g_propagate_error (error, local);
			return FALSE;
		}
	}

	/* Floating entries: (panel, x, y, w, h) records */
	floats = g_ptr_array_new_with_free_func (g_free);
	member = yaml_mapping_get_member (map, "floating");
	if (member != NULL && yaml_node_get_node_type (member) == YAML_NODE_SEQUENCE)
	{
		YamlSequence *seq = yaml_node_get_sequence (member);

		for (i = 0; i < yaml_sequence_get_length (seq); i++)
		{
			YamlNode     *e = yaml_sequence_get_element (seq, i);
			YamlMapping  *fm;
			YamlNode     *idn;
			LrgDockPanel *p;
			FloatRec     *rec;

			if (e == NULL || yaml_node_get_node_type (e) != YAML_NODE_MAPPING)
				continue;
			fm = yaml_node_get_mapping (e);
			idn = yaml_mapping_get_member (fm, "id");
			if (idn == NULL || yaml_node_get_node_type (idn) != YAML_NODE_SCALAR)
				continue;
			p = find_known (known, used, yaml_node_get_string (idn));
			if (p == NULL)
				continue;
			g_hash_table_add (used, p);

			rec = g_new (FloatRec, 1);
			rec->panel = p;
			rec->x = scalar_double (fm, "x", 40.0f);
			rec->y = scalar_double (fm, "y", 40.0f);
			rec->w = scalar_double (fm, "width", FRAME_DEFAULT_W);
			rec->h = scalar_double (fm, "height", FRAME_DEFAULT_H);
			g_ptr_array_add (floats, rec);
		}
	}

	member = yaml_mapping_get_member (map, "active");
	if (member != NULL && yaml_node_get_node_type (member) == YAML_NODE_SCALAR)
	{
		const gchar *id = yaml_node_get_string (member);

		for (l = known; l != NULL; l = l->next)
			if (g_strcmp0 (LRG_DOCK_PANEL (l->data)->id, id) == 0)
				wanted_active = l->data;
	}

	/* 2. Apply: every panel back under the area, the new tree in place */
	if (self->dragging)
		drag_cancel (self);
	while (self->frames->len > 0)
		unfloat (self, g_ptr_array_index (self->frames, self->frames->len - 1));
	tree_free (self->root);
	self->root = tree;
	if (tree != NULL)
		tree->parent = NULL;

	/* Panels the layout did not mention keep a place: tabs of the first
	 * stack (floating entries are placed below) */
	for (l = known; l != NULL; l = l->next)
		if (!g_hash_table_contains (used, l->data))
			append_quiet (self, l->data);

	canvas = canvas_of (self);
	for (i = 0; i < floats->len; i++)
	{
		FloatRec *rec = g_ptr_array_index (floats, i);

		if (canvas != NULL && self->allow_floating)
			float_detached (self, canvas, rec->panel, rec->x, rec->y, rec->w, rec->h);
		else
			append_quiet (self, rec->panel);
	}

	tree_changed (self);
	if (wanted_active != NULL)
		set_active_panel (self, wanted_active);
	else if (self->active_panel == NULL && self->root != NULL)
		set_active_panel (self, stack_active_panel (first_stack (self->root)));
	return TRUE;
}

/* ==========================================================================
 * Public API: properties
 * ========================================================================== */

gfloat
lrg_dock_area_get_divider_size (LrgDockArea *self)
{
	g_return_val_if_fail (LRG_IS_DOCK_AREA (self), 0.0f);
	return self->divider_size;
}

void
lrg_dock_area_set_divider_size (LrgDockArea *self,
                                gfloat       size)
{
	g_return_if_fail (LRG_IS_DOCK_AREA (self));

	size = MAX (1.0f, roundf (size));
	if (self->divider_size == size)
		return;
	self->divider_size = size;
	lrg_widget_queue_layout (LRG_WIDGET (self));
	lrg_container_layout_children (LRG_CONTAINER (self));
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_DIVIDER_SIZE]);
}

gfloat
lrg_dock_area_get_tab_height (LrgDockArea *self)
{
	g_return_val_if_fail (LRG_IS_DOCK_AREA (self), 0.0f);
	return self->tab_height;
}

void
lrg_dock_area_set_tab_height (LrgDockArea *self,
                              gfloat       height)
{
	g_return_if_fail (LRG_IS_DOCK_AREA (self));

	height = MAX (0.0f, roundf (height));
	if (self->tab_height == height)
		return;
	self->tab_height = height;
	lrg_widget_queue_layout (LRG_WIDGET (self));
	lrg_container_layout_children (LRG_CONTAINER (self));
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TAB_HEIGHT]);
}

gfloat
lrg_dock_area_get_min_panel_size (LrgDockArea *self)
{
	g_return_val_if_fail (LRG_IS_DOCK_AREA (self), 0.0f);
	return self->min_panel_size;
}

void
lrg_dock_area_set_min_panel_size (LrgDockArea *self,
                                  gfloat       size)
{
	g_return_if_fail (LRG_IS_DOCK_AREA (self));

	size = MAX (0.0f, size);
	if (self->min_panel_size == size)
		return;
	self->min_panel_size = size;
	lrg_widget_queue_layout (LRG_WIDGET (self));
	lrg_container_layout_children (LRG_CONTAINER (self));
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MIN_PANEL_SIZE]);
}

gboolean
lrg_dock_area_get_allow_floating (LrgDockArea *self)
{
	g_return_val_if_fail (LRG_IS_DOCK_AREA (self), FALSE);
	return self->allow_floating;
}

void
lrg_dock_area_set_allow_floating (LrgDockArea *self,
                                  gboolean     allow)
{
	g_return_if_fail (LRG_IS_DOCK_AREA (self));

	allow = !!allow;
	if (self->allow_floating == allow)
		return;
	self->allow_floating = allow;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ALLOW_FLOATING]);
}
