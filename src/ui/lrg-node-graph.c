/* lrg-node-graph.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgNodeGraph: a node editor on an infinite canvas.
 *
 * Spaces. Nodes live in "graph units". The view maps them to canvas
 * units (the coordinates of UI events and of the paint list):
 *
 *     screen = widget_centre + (graph - centre) * zoom
 *
 * where centre is the graph point shown in the middle of the widget.
 * Zoom and centre are three springs. A plain glide (frame all, frame
 * selection, lrg_node_graph_set_view) moves the three independently; a
 * zoom about a point (mouse wheel) is "anchored": only the zoom spring
 * moves and the centre is recomputed every frame so that the graph
 * point under the pointer stays under it for the whole glide.
 *
 * Model. The graph owns its nodes (stacking order, last on top) and
 * its links (creation order); nodes own their ports; links keep a
 * reference to both ports. Node geometry (card size, port rows, body
 * slot) is measured lazily from the theme fonts and cached until a
 * title, port, body or font size changes.
 *
 * Painting draws, clipped to the widget: background, grid, links,
 * the live link being dragged, the nodes bottom to top (each followed
 * by its body widget), the rubber band and the minimap. Nodes and
 * links outside the view are skipped (culling), and below a zoom of
 * 0.4 text is skipped too, so large graphs stay cheap.
 *
 * Body widgets are real children of the graph (an LrgContainer): they
 * are allocated at their card's on-screen rectangle, so the canvas
 * hit-tests and delivers events to them normally.
 */

#include "config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_UI

#include <math.h>
#include <string.h>
#include <raylib.h>
#include <yaml-glib.h>

#include "lrg-node-graph.h"
#include "../tween/lrg-spring.h"
#include "../lrg-log.h"

/* ==========================================================================
 * Constants (graph units unless noted)
 * ========================================================================== */

#define HEADER_H         (28.0f)   /* card header strip */
#define ROW_H            (22.0f)   /* one port row */
#define ROWS_TOP         (6.0f)    /* gap between header and first row */
#define PAD              (10.0f)   /* card inner padding */
#define PORT_R           (5.0f)    /* port dot radius */
#define LABEL_INSET      (12.0f)   /* port centre to label */
#define LABEL_GAP        (18.0f)   /* between input and output labels */
#define MIN_WIDTH        (140.0f)
#define BODY_MIN_ZOOM    (0.45f)   /* below: bodies hidden */
#define TEXT_MIN_ZOOM    (0.4f)    /* below: no text */
#define PORT_REACH       (9.0f)    /* canvas units around a port dot */
#define LINK_REACH       (6.0f)    /* canvas units around a wire */
#define SNAP_REACH       (26.0f)   /* live link snaps within (canvas units) */
#define DRAG_THRESHOLD   (3.0f)    /* canvas units before a press drags */
#define DOUBLE_CLICK     (0.4)     /* seconds (canvas clock) */
#define FRAME_MARGIN     (48.0f)   /* canvas units around framed nodes */
#define WHEEL_STEP       (1.15f)   /* zoom factor per wheel notch */
#define MINIMAP_W        (180.0f)  /* canvas units */
#define MINIMAP_H        (120.0f)
#define MINIMAP_MARGIN   (12.0f)
#define LINK_SEGMENTS    (20)      /* samples for wire hit tests */
#define DOT_MAX          (4000)    /* most grid dots drawn */

/* Header / type palette: pleasant, mid-luminance hues */
static const guint32 palette[] = {
	0x89b4fa, 0xa6e3a1, 0xf9e2af, 0xf38ba8, 0xcba6f7,
	0x94e2d5, 0xfab387, 0x74c7ec, 0xf5c2e7, 0xb4befe
};

/* ==========================================================================
 * Enum types and error quark
 * ========================================================================== */

GType
lrg_graph_port_direction_get_type (void)
{
	static gsize type_id = 0;

	if (g_once_init_enter (&type_id))
	{
		static const GEnumValue values[] = {
			{ LRG_GRAPH_PORT_INPUT, "LRG_GRAPH_PORT_INPUT", "input" },
			{ LRG_GRAPH_PORT_OUTPUT, "LRG_GRAPH_PORT_OUTPUT", "output" },
			{ 0, NULL, NULL }
		};
		GType id = g_enum_register_static (g_intern_static_string ("LrgGraphPortDirection"), values);

		g_once_init_leave (&type_id, id);
	}
	return (GType) type_id;
}

GType
lrg_node_graph_grid_get_type (void)
{
	static gsize type_id = 0;

	if (g_once_init_enter (&type_id))
	{
		static const GEnumValue values[] = {
			{ LRG_NODE_GRAPH_GRID_NONE, "LRG_NODE_GRAPH_GRID_NONE", "none" },
			{ LRG_NODE_GRAPH_GRID_DOTS, "LRG_NODE_GRAPH_GRID_DOTS", "dots" },
			{ LRG_NODE_GRAPH_GRID_LINES, "LRG_NODE_GRAPH_GRID_LINES", "lines" },
			{ 0, NULL, NULL }
		};
		GType id = g_enum_register_static (g_intern_static_string ("LrgNodeGraphGrid"), values);

		g_once_init_leave (&type_id, id);
	}
	return (GType) type_id;
}

G_DEFINE_QUARK (lrg-node-graph-error-quark, lrg_node_graph_error)

/* ==========================================================================
 * Instance structures
 * ========================================================================== */

struct _LrgGraphPort
{
	GObject                parent_instance;

	gchar                 *name;
	LrgGraphPortDirection  direction;
	GQuark                 type;
	gboolean               multi_link;
	gboolean               has_color;
	GrlColor               color;

	LrgGraphNode          *node;       /* not owned */
	guint                  n_links;

	/* geometry cache, filled by ensure_geometry() */
	gfloat                 rel_y;      /* row centre below the node top */
	gfloat                 label_w;    /* label width at zoom 1 */
};

struct _LrgGraphLink
{
	GObject       parent_instance;
	LrgGraphPort *output;               /* owned reference */
	LrgGraphPort *input;                /* owned reference */
};

struct _LrgGraphNode
{
	GObject        parent_instance;

	gchar         *id;
	gchar         *title;
	gchar         *category;
	gboolean       has_accent;
	GrlColor       accent;
	gfloat         x;
	gfloat         y;
	gfloat         min_width;
	LrgWidget     *body;               /* owned reference */
	GPtrArray     *inputs;             /* LrgGraphPort, owned */
	GPtrArray     *outputs;

	LrgNodeGraph  *graph;              /* not owned */
	gboolean       selected;

	/* geometry cache */
	gboolean       geometry_dirty;
	gfloat         geometry_font;      /* font size it was measured with */
	gfloat         w;
	gfloat         h;
	gfloat         title_w;
	gfloat         body_y;
	gfloat         body_h;

	/* motion: lift (shadow) and selection outline */
	LrgSpring      lift;
	LrgSpring      sel;
	gboolean       animating;

	/* position when a move drag started */
	gfloat         drag_x;
	gfloat         drag_y;
};

typedef enum
{
	DRAG_NONE,
	DRAG_PENDING,   /* pressed on a node, not moved yet */
	DRAG_MOVE,      /* moving the selected nodes */
	DRAG_LINK,      /* pulling a wire out of a port */
	DRAG_BOX,       /* rubber band selection */
	DRAG_PAN,       /* middle button or Space + left button */
	DRAG_MINIMAP    /* scrubbing the minimap */
} DragMode;

struct _LrgNodeGraph
{
	LrgContainer      parent_instance;

	/* model */
	GPtrArray        *nodes;           /* LrgGraphNode, owned, stacking order */
	GHashTable       *by_id;           /* id -> node (not owned) */
	GPtrArray        *links;           /* LrgGraphLink, owned */
	guint             next_id;
	guint             n_selected;
	LrgGraphLink     *selected_link;   /* not owned */
	LrgGraphNode     *cursor;          /* not owned */
	guint             n_bodies;

	/* view */
	LrgSpring         zoom_s;
	LrgSpring         cx_s;
	LrgSpring         cy_s;
	gboolean          view_animating;
	gboolean          anchored;
	gfloat            anchor_gx;
	gfloat            anchor_gy;
	gfloat            anchor_rx;       /* anchor offset from the widget centre */
	gfloat            anchor_ry;
	gboolean          stepped;         /* first step snaps */

	/* settings */
	gfloat            min_zoom;
	gfloat            max_zoom;
	gfloat            grid_size;
	LrgNodeGraphGrid  grid_style;
	gboolean          snap;
	gboolean          show_minimap;
	gboolean          animate_links;

	/* interaction */
	DragMode          drag;
	gint              drag_button;
	gboolean          drag_additive;
	gfloat            press_x;
	gfloat            press_y;
	gfloat            pointer_x;
	gfloat            pointer_y;
	gboolean          pointer_in;
	LrgGraphNode     *press_node;      /* not owned */
	LrgGraphPort     *link_origin;     /* owned reference while dragging */
	LrgGraphPort     *link_snap;       /* not owned */
	GHashTable       *compatible;      /* ports a live link may land on */
	GHashTable       *box_base;        /* nodes selected before a box drag */
	gboolean          space_armed;
	gboolean          touch_panning;
	LrgGraphNode     *hover_node;      /* not owned */
	LrgGraphPort     *hover_port;
	LrgGraphLink     *hover_link;
	gdouble           clock;
	gdouble           last_click_time;
	LrgGraphNode     *last_click_node; /* not owned */
	gfloat            last_click_x;
	gfloat            last_click_y;
	gboolean          in_layout;

	/* minimap geometry frozen while it is scrubbed (it frames the view
	 * too, so it would otherwise move under the pointer) */
	gfloat            mm_frozen[9];

	/* diagnostics */
	guint             stat_nodes;
	guint             stat_links;
};

/* ==========================================================================
 * Forward declarations (graph hooks used by node and port setters)
 * ========================================================================== */

static void graph_node_changed     (LrgNodeGraph *self,
                                    LrgGraphNode *node,
                                    gboolean      geometry);
static void graph_node_moved       (LrgNodeGraph *self,
                                    LrgGraphNode *node);
static void graph_body_changed     (LrgNodeGraph *self,
                                    LrgGraphNode *node,
                                    LrgWidget    *old_body);
static void graph_port_removing    (LrgNodeGraph *self,
                                    LrgGraphPort *port);
static void sync_body_order        (LrgNodeGraph *self);

/* ==========================================================================
 * Colour helpers
 * ========================================================================== */

static void
color_from_hex (guint32   hex,
                GrlColor *out)
{
	out->r = (guint8) ((hex >> 16) & 0xff);
	out->g = (guint8) ((hex >> 8) & 0xff);
	out->b = (guint8) (hex & 0xff);
	out->a = 255;
}

/* palette_color: a stable palette entry for @key, @salt varies the pick */
static void
palette_color (const gchar *key,
               guint        salt,
               GrlColor    *out)
{
	guint h = g_str_hash (key) + salt * 2654435761u;

	color_from_hex (palette[h % G_N_ELEMENTS (palette)], out);
}

/* ==========================================================================
 * LrgGraphPort
 * ========================================================================== */

G_DEFINE_FINAL_TYPE (LrgGraphPort, lrg_graph_port, G_TYPE_OBJECT)

enum
{
	PORT_PROP_0,
	PORT_PROP_NAME,
	PORT_PROP_DIRECTION,
	PORT_PROP_TYPE_NAME,
	PORT_PROP_MULTI_LINK,
	PORT_PROP_COLOR,
	PORT_N_PROPS
};

static GParamSpec *port_props[PORT_N_PROPS];

static void
lrg_graph_port_finalize (GObject *object)
{
	LrgGraphPort *self = LRG_GRAPH_PORT (object);

	g_free (self->name);
	G_OBJECT_CLASS (lrg_graph_port_parent_class)->finalize (object);
}

static void
lrg_graph_port_get_property (GObject    *object,
                             guint       prop_id,
                             GValue     *value,
                             GParamSpec *pspec)
{
	LrgGraphPort *self = LRG_GRAPH_PORT (object);

	switch (prop_id)
	{
	case PORT_PROP_NAME:
		g_value_set_string (value, self->name);
		break;
	case PORT_PROP_DIRECTION:
		g_value_set_enum (value, self->direction);
		break;
	case PORT_PROP_TYPE_NAME:
		g_value_set_string (value, lrg_graph_port_get_type_name (self));
		break;
	case PORT_PROP_MULTI_LINK:
		g_value_set_boolean (value, self->multi_link);
		break;
	case PORT_PROP_COLOR:
		g_value_set_boxed (value, self->has_color ? &self->color : NULL);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_graph_port_set_property (GObject      *object,
                             guint         prop_id,
                             const GValue *value,
                             GParamSpec   *pspec)
{
	LrgGraphPort *self = LRG_GRAPH_PORT (object);
	const gchar  *s;

	switch (prop_id)
	{
	case PORT_PROP_NAME:
		g_free (self->name);
		self->name = g_value_dup_string (value);
		break;
	case PORT_PROP_DIRECTION:
		self->direction = (LrgGraphPortDirection) g_value_get_enum (value);
		/* Outputs fan out by default, inputs take one link */
		self->multi_link = self->direction == LRG_GRAPH_PORT_OUTPUT;
		break;
	case PORT_PROP_TYPE_NAME:
		s = g_value_get_string (value);
		self->type = (s != NULL && *s != '\0') ? g_quark_from_string (s) : 0;
		break;
	case PORT_PROP_MULTI_LINK:
		lrg_graph_port_set_multi_link (self, g_value_get_boolean (value));
		break;
	case PORT_PROP_COLOR:
		lrg_graph_port_set_color (self, g_value_get_boxed (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_graph_port_class_init (LrgGraphPortClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = lrg_graph_port_finalize;
	object_class->get_property = lrg_graph_port_get_property;
	object_class->set_property = lrg_graph_port_set_property;

	/**
	 * LrgGraphPort:name:
	 *
	 * Port name: its label and key within the node.
	 */
	port_props[PORT_PROP_NAME] =
		g_param_spec_string ("name", "Name", "Port name", NULL,
		                     G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

	/**
	 * LrgGraphPort:direction:
	 *
	 * Input (left edge) or output (right edge).
	 */
	port_props[PORT_PROP_DIRECTION] =
		g_param_spec_enum ("direction", "Direction", "Input or output",
		                   LRG_TYPE_GRAPH_PORT_DIRECTION, LRG_GRAPH_PORT_INPUT,
		                   G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

	/**
	 * LrgGraphPort:type-name:
	 *
	 * Type for compatibility and colour, %NULL for "any".
	 */
	port_props[PORT_PROP_TYPE_NAME] =
		g_param_spec_string ("type-name", "Type name", "Port type", NULL,
		                     G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

	/**
	 * LrgGraphPort:multi-link:
	 *
	 * Whether more than one link may attach (outputs: yes by default).
	 */
	port_props[PORT_PROP_MULTI_LINK] =
		g_param_spec_boolean ("multi-link", "Multi link", "Accept several links", FALSE,
		                      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgGraphPort:color:
	 *
	 * Explicit colour of the port and its links, %NULL for the colour
	 * derived from the type.
	 */
	port_props[PORT_PROP_COLOR] =
		g_param_spec_boxed ("color", "Color", "Explicit port colour", GRL_TYPE_COLOR,
		                    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	g_object_class_install_properties (object_class, PORT_N_PROPS, port_props);
}

static void
lrg_graph_port_init (LrgGraphPort *self)
{
}

static LrgGraphPort *
port_new (LrgGraphPortDirection  direction,
          const gchar           *name,
          const gchar           *type_name)
{
	return g_object_new (LRG_TYPE_GRAPH_PORT, "direction", direction, "name", name,
	                     "type-name", type_name, NULL);
}

const gchar *
lrg_graph_port_get_name (LrgGraphPort *self)
{
	g_return_val_if_fail (LRG_IS_GRAPH_PORT (self), NULL);
	return self->name;
}

LrgGraphPortDirection
lrg_graph_port_get_direction (LrgGraphPort *self)
{
	g_return_val_if_fail (LRG_IS_GRAPH_PORT (self), LRG_GRAPH_PORT_INPUT);
	return self->direction;
}

GQuark
lrg_graph_port_get_port_type (LrgGraphPort *self)
{
	g_return_val_if_fail (LRG_IS_GRAPH_PORT (self), 0);
	return self->type;
}

const gchar *
lrg_graph_port_get_type_name (LrgGraphPort *self)
{
	g_return_val_if_fail (LRG_IS_GRAPH_PORT (self), NULL);
	return self->type != 0 ? g_quark_to_string (self->type) : NULL;
}

LrgGraphNode *
lrg_graph_port_get_node (LrgGraphPort *self)
{
	g_return_val_if_fail (LRG_IS_GRAPH_PORT (self), NULL);
	return self->node;
}

gboolean
lrg_graph_port_get_multi_link (LrgGraphPort *self)
{
	g_return_val_if_fail (LRG_IS_GRAPH_PORT (self), FALSE);
	return self->multi_link;
}

void
lrg_graph_port_set_multi_link (LrgGraphPort *self,
                               gboolean      multi_link)
{
	g_return_if_fail (LRG_IS_GRAPH_PORT (self));

	multi_link = !!multi_link;
	if (self->multi_link == multi_link)
		return;
	self->multi_link = multi_link;
	g_object_notify_by_pspec (G_OBJECT (self), port_props[PORT_PROP_MULTI_LINK]);
}

gboolean
lrg_graph_port_get_color (LrgGraphPort *self,
                          GrlColor     *color)
{
	g_return_val_if_fail (LRG_IS_GRAPH_PORT (self), FALSE);
	g_return_val_if_fail (color != NULL, FALSE);

	if (self->has_color)
		*color = self->color;
	else if (self->type != 0)
		palette_color (g_quark_to_string (self->type), 7, color);
	else
		color_from_hex (0xbac2de, color);
	return self->has_color;
}

void
lrg_graph_port_set_color (LrgGraphPort   *self,
                          const GrlColor *color)
{
	g_return_if_fail (LRG_IS_GRAPH_PORT (self));

	if (color == NULL && !self->has_color)
		return;
	if (color != NULL && self->has_color && memcmp (color, &self->color, sizeof (GrlColor)) == 0)
		return;
	self->has_color = color != NULL;
	if (color != NULL)
		self->color = *color;
	if (self->node != NULL && self->node->graph != NULL)
		graph_node_changed (self->node->graph, self->node, FALSE);
	g_object_notify_by_pspec (G_OBJECT (self), port_props[PORT_PROP_COLOR]);
}

guint
lrg_graph_port_get_n_links (LrgGraphPort *self)
{
	g_return_val_if_fail (LRG_IS_GRAPH_PORT (self), 0);
	return self->n_links;
}

/* ==========================================================================
 * LrgGraphLink
 * ========================================================================== */

G_DEFINE_FINAL_TYPE (LrgGraphLink, lrg_graph_link, G_TYPE_OBJECT)

enum
{
	LINK_PROP_0,
	LINK_PROP_OUTPUT,
	LINK_PROP_INPUT,
	LINK_N_PROPS
};

static GParamSpec *link_props[LINK_N_PROPS];

static void
lrg_graph_link_dispose (GObject *object)
{
	LrgGraphLink *self = LRG_GRAPH_LINK (object);

	g_clear_object (&self->output);
	g_clear_object (&self->input);
	G_OBJECT_CLASS (lrg_graph_link_parent_class)->dispose (object);
}

static void
lrg_graph_link_get_property (GObject    *object,
                             guint       prop_id,
                             GValue     *value,
                             GParamSpec *pspec)
{
	LrgGraphLink *self = LRG_GRAPH_LINK (object);

	switch (prop_id)
	{
	case LINK_PROP_OUTPUT:
		g_value_set_object (value, self->output);
		break;
	case LINK_PROP_INPUT:
		g_value_set_object (value, self->input);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_graph_link_class_init (LrgGraphLinkClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->dispose = lrg_graph_link_dispose;
	object_class->get_property = lrg_graph_link_get_property;

	/**
	 * LrgGraphLink:output:
	 *
	 * The output port the link starts at.
	 */
	link_props[LINK_PROP_OUTPUT] =
		g_param_spec_object ("output", "Output", "Output port", LRG_TYPE_GRAPH_PORT,
		                     G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

	/**
	 * LrgGraphLink:input:
	 *
	 * The input port the link ends at.
	 */
	link_props[LINK_PROP_INPUT] =
		g_param_spec_object ("input", "Input", "Input port", LRG_TYPE_GRAPH_PORT,
		                     G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

	g_object_class_install_properties (object_class, LINK_N_PROPS, link_props);
}

static void
lrg_graph_link_init (LrgGraphLink *self)
{
}

LrgGraphPort *
lrg_graph_link_get_output (LrgGraphLink *self)
{
	g_return_val_if_fail (LRG_IS_GRAPH_LINK (self), NULL);
	return self->output;
}

LrgGraphPort *
lrg_graph_link_get_input (LrgGraphLink *self)
{
	g_return_val_if_fail (LRG_IS_GRAPH_LINK (self), NULL);
	return self->input;
}

/* ==========================================================================
 * LrgGraphNode
 * ========================================================================== */

G_DEFINE_FINAL_TYPE (LrgGraphNode, lrg_graph_node, G_TYPE_OBJECT)

enum
{
	NODE_PROP_0,
	NODE_PROP_ID,
	NODE_PROP_TITLE,
	NODE_PROP_CATEGORY,
	NODE_PROP_ACCENT,
	NODE_PROP_X,
	NODE_PROP_Y,
	NODE_PROP_MIN_WIDTH,
	NODE_PROP_BODY,
	NODE_PROP_SELECTED,
	NODE_N_PROPS
};

static GParamSpec *node_props[NODE_N_PROPS];

static void
lrg_graph_node_dispose (GObject *object)
{
	LrgGraphNode *self = LRG_GRAPH_NODE (object);
	guint         i;

	/* Ports outlive the node only through links, which the graph
	 * removed already; cut their back pointers anyway */
	for (i = 0; self->inputs != NULL && i < self->inputs->len; i++)
		((LrgGraphPort *) g_ptr_array_index (self->inputs, i))->node = NULL;
	for (i = 0; self->outputs != NULL && i < self->outputs->len; i++)
		((LrgGraphPort *) g_ptr_array_index (self->outputs, i))->node = NULL;
	g_clear_pointer (&self->inputs, g_ptr_array_unref);
	g_clear_pointer (&self->outputs, g_ptr_array_unref);
	g_clear_object (&self->body);
	G_OBJECT_CLASS (lrg_graph_node_parent_class)->dispose (object);
}

static void
lrg_graph_node_finalize (GObject *object)
{
	LrgGraphNode *self = LRG_GRAPH_NODE (object);

	g_free (self->id);
	g_free (self->title);
	g_free (self->category);
	G_OBJECT_CLASS (lrg_graph_node_parent_class)->finalize (object);
}

static void
lrg_graph_node_get_property (GObject    *object,
                             guint       prop_id,
                             GValue     *value,
                             GParamSpec *pspec)
{
	LrgGraphNode *self = LRG_GRAPH_NODE (object);

	switch (prop_id)
	{
	case NODE_PROP_ID:
		g_value_set_string (value, self->id);
		break;
	case NODE_PROP_TITLE:
		g_value_set_string (value, self->title);
		break;
	case NODE_PROP_CATEGORY:
		g_value_set_string (value, self->category);
		break;
	case NODE_PROP_ACCENT:
		g_value_set_boxed (value, self->has_accent ? &self->accent : NULL);
		break;
	case NODE_PROP_X:
		g_value_set_float (value, self->x);
		break;
	case NODE_PROP_Y:
		g_value_set_float (value, self->y);
		break;
	case NODE_PROP_MIN_WIDTH:
		g_value_set_float (value, self->min_width);
		break;
	case NODE_PROP_BODY:
		g_value_set_object (value, self->body);
		break;
	case NODE_PROP_SELECTED:
		g_value_set_boolean (value, self->selected);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_graph_node_set_property (GObject      *object,
                             guint         prop_id,
                             const GValue *value,
                             GParamSpec   *pspec)
{
	LrgGraphNode *self = LRG_GRAPH_NODE (object);

	switch (prop_id)
	{
	case NODE_PROP_ID:
		g_free (self->id);
		self->id = g_value_dup_string (value);
		break;
	case NODE_PROP_TITLE:
		lrg_graph_node_set_title (self, g_value_get_string (value));
		break;
	case NODE_PROP_CATEGORY:
		lrg_graph_node_set_category (self, g_value_get_string (value));
		break;
	case NODE_PROP_ACCENT:
		lrg_graph_node_set_accent (self, g_value_get_boxed (value));
		break;
	case NODE_PROP_X:
		lrg_graph_node_set_position (self, g_value_get_float (value), self->y);
		break;
	case NODE_PROP_Y:
		lrg_graph_node_set_position (self, self->x, g_value_get_float (value));
		break;
	case NODE_PROP_MIN_WIDTH:
		lrg_graph_node_set_min_width (self, g_value_get_float (value));
		break;
	case NODE_PROP_BODY:
		lrg_graph_node_set_body (self, g_value_get_object (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_graph_node_class_init (LrgGraphNodeClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->dispose = lrg_graph_node_dispose;
	object_class->finalize = lrg_graph_node_finalize;
	object_class->get_property = lrg_graph_node_get_property;
	object_class->set_property = lrg_graph_node_set_property;

	/**
	 * LrgGraphNode:id:
	 *
	 * Unique id within the graph (assigned on insertion when %NULL).
	 */
	node_props[NODE_PROP_ID] =
		g_param_spec_string ("id", "Id", "Unique node id", NULL,
		                     G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

	/**
	 * LrgGraphNode:title:
	 *
	 * Header text.
	 */
	node_props[NODE_PROP_TITLE] =
		g_param_spec_string ("title", "Title", "Header text", NULL,
		                     G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgGraphNode:category:
	 *
	 * Category name; picks the header colour when no accent is set.
	 */
	node_props[NODE_PROP_CATEGORY] =
		g_param_spec_string ("category", "Category", "Node category", NULL,
		                     G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgGraphNode:accent:
	 *
	 * Explicit header colour, %NULL for the category colour.
	 */
	node_props[NODE_PROP_ACCENT] =
		g_param_spec_boxed ("accent", "Accent", "Header colour", GRL_TYPE_COLOR,
		                    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgGraphNode:x:
	 *
	 * Left edge in graph units.
	 */
	node_props[NODE_PROP_X] =
		g_param_spec_float ("x", "X", "Left edge in graph units", -G_MAXFLOAT, G_MAXFLOAT, 0.0f,
		                    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgGraphNode:y:
	 *
	 * Top edge in graph units.
	 */
	node_props[NODE_PROP_Y] =
		g_param_spec_float ("y", "Y", "Top edge in graph units", -G_MAXFLOAT, G_MAXFLOAT, 0.0f,
		                    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgGraphNode:min-width:
	 *
	 * Smallest card width in graph units.
	 */
	node_props[NODE_PROP_MIN_WIDTH] =
		g_param_spec_float ("min-width", "Min width", "Smallest card width", 0.0f, G_MAXFLOAT,
		                    MIN_WIDTH,
		                    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgGraphNode:body:
	 *
	 * Widget embedded under the port rows.
	 */
	node_props[NODE_PROP_BODY] =
		g_param_spec_object ("body", "Body", "Embedded widget", LRG_TYPE_WIDGET,
		                     G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgGraphNode:selected:
	 *
	 * Whether the node is selected in its graph.
	 */
	node_props[NODE_PROP_SELECTED] =
		g_param_spec_boolean ("selected", "Selected", "Selected in the graph", FALSE,
		                      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	g_object_class_install_properties (object_class, NODE_N_PROPS, node_props);
}

static void
lrg_graph_node_init (LrgGraphNode *self)
{
	self->inputs = g_ptr_array_new_with_free_func (g_object_unref);
	self->outputs = g_ptr_array_new_with_free_func (g_object_unref);
	self->min_width = MIN_WIDTH;
	self->geometry_dirty = TRUE;
	lrg_spring_init (&self->lift, 0.0f, LRG_MOTION_SNAPPY);
	lrg_spring_init (&self->sel, 0.0f, LRG_MOTION_SNAPPY);
}

LrgGraphNode *
lrg_graph_node_new (const gchar *id,
                    const gchar *title)
{
	return g_object_new (LRG_TYPE_GRAPH_NODE, "id", id, "title", title, NULL);
}

const gchar *
lrg_graph_node_get_id (LrgGraphNode *self)
{
	g_return_val_if_fail (LRG_IS_GRAPH_NODE (self), NULL);
	return self->id;
}

const gchar *
lrg_graph_node_get_title (LrgGraphNode *self)
{
	g_return_val_if_fail (LRG_IS_GRAPH_NODE (self), NULL);
	return self->title;
}

void
lrg_graph_node_set_title (LrgGraphNode *self,
                          const gchar  *title)
{
	g_return_if_fail (LRG_IS_GRAPH_NODE (self));

	if (g_strcmp0 (self->title, title) == 0)
		return;
	g_free (self->title);
	self->title = g_strdup (title);
	self->geometry_dirty = TRUE;
	if (self->graph != NULL)
		graph_node_changed (self->graph, self, TRUE);
	g_object_notify_by_pspec (G_OBJECT (self), node_props[NODE_PROP_TITLE]);
}

const gchar *
lrg_graph_node_get_category (LrgGraphNode *self)
{
	g_return_val_if_fail (LRG_IS_GRAPH_NODE (self), NULL);
	return self->category;
}

void
lrg_graph_node_set_category (LrgGraphNode *self,
                             const gchar  *category)
{
	g_return_if_fail (LRG_IS_GRAPH_NODE (self));

	if (g_strcmp0 (self->category, category) == 0)
		return;
	g_free (self->category);
	self->category = g_strdup (category);
	if (self->graph != NULL)
		graph_node_changed (self->graph, self, FALSE);
	g_object_notify_by_pspec (G_OBJECT (self), node_props[NODE_PROP_CATEGORY]);
}

gboolean
lrg_graph_node_get_accent (LrgGraphNode *self,
                           GrlColor     *color)
{
	g_return_val_if_fail (LRG_IS_GRAPH_NODE (self), FALSE);

	if (self->has_accent && color != NULL)
		*color = self->accent;
	return self->has_accent;
}

void
lrg_graph_node_set_accent (LrgGraphNode   *self,
                           const GrlColor *color)
{
	g_return_if_fail (LRG_IS_GRAPH_NODE (self));

	if (color == NULL && !self->has_accent)
		return;
	if (color != NULL && self->has_accent && memcmp (color, &self->accent, sizeof (GrlColor)) == 0)
		return;
	self->has_accent = color != NULL;
	if (color != NULL)
		self->accent = *color;
	if (self->graph != NULL)
		graph_node_changed (self->graph, self, FALSE);
	g_object_notify_by_pspec (G_OBJECT (self), node_props[NODE_PROP_ACCENT]);
}

gfloat
lrg_graph_node_get_x (LrgGraphNode *self)
{
	g_return_val_if_fail (LRG_IS_GRAPH_NODE (self), 0.0f);
	return self->x;
}

gfloat
lrg_graph_node_get_y (LrgGraphNode *self)
{
	g_return_val_if_fail (LRG_IS_GRAPH_NODE (self), 0.0f);
	return self->y;
}

/* node_place: moves the node and notifies x / y, no node-moved */
static gboolean
node_place (LrgGraphNode *self,
            gfloat        x,
            gfloat        y)
{
	gboolean xc = self->x != x;
	gboolean yc = self->y != y;

	if (!xc && !yc)
		return FALSE;
	self->x = x;
	self->y = y;
	if (xc)
		g_object_notify_by_pspec (G_OBJECT (self), node_props[NODE_PROP_X]);
	if (yc)
		g_object_notify_by_pspec (G_OBJECT (self), node_props[NODE_PROP_Y]);
	return TRUE;
}

void
lrg_graph_node_set_position (LrgGraphNode *self,
                             gfloat        x,
                             gfloat        y)
{
	g_return_if_fail (LRG_IS_GRAPH_NODE (self));

	if (node_place (self, x, y) && self->graph != NULL)
		graph_node_moved (self->graph, self);
}

gfloat
lrg_graph_node_get_min_width (LrgGraphNode *self)
{
	g_return_val_if_fail (LRG_IS_GRAPH_NODE (self), 0.0f);
	return self->min_width;
}

void
lrg_graph_node_set_min_width (LrgGraphNode *self,
                              gfloat        width)
{
	g_return_if_fail (LRG_IS_GRAPH_NODE (self));

	width = MAX (0.0f, width);
	if (self->min_width == width)
		return;
	self->min_width = width;
	self->geometry_dirty = TRUE;
	if (self->graph != NULL)
		graph_node_changed (self->graph, self, TRUE);
	g_object_notify_by_pspec (G_OBJECT (self), node_props[NODE_PROP_MIN_WIDTH]);
}

LrgWidget *
lrg_graph_node_get_body (LrgGraphNode *self)
{
	g_return_val_if_fail (LRG_IS_GRAPH_NODE (self), NULL);
	return self->body;
}

void
lrg_graph_node_set_body (LrgGraphNode *self,
                         LrgWidget    *body)
{
	g_autoptr(LrgWidget) old = NULL;

	g_return_if_fail (LRG_IS_GRAPH_NODE (self));
	g_return_if_fail (body == NULL || LRG_IS_WIDGET (body));

	if (self->body == body)
		return;
	old = g_steal_pointer (&self->body);
	self->body = body != NULL ? g_object_ref (body) : NULL;
	self->geometry_dirty = TRUE;
	if (self->graph != NULL)
		graph_body_changed (self->graph, self, old);
	g_object_notify_by_pspec (G_OBJECT (self), node_props[NODE_PROP_BODY]);
}

gboolean
lrg_graph_node_get_selected (LrgGraphNode *self)
{
	g_return_val_if_fail (LRG_IS_GRAPH_NODE (self), FALSE);
	return self->selected;
}

LrgNodeGraph *
lrg_graph_node_get_graph (LrgGraphNode *self)
{
	g_return_val_if_fail (LRG_IS_GRAPH_NODE (self), NULL);
	return self->graph;
}

static GPtrArray *
node_ports (LrgGraphNode          *self,
            LrgGraphPortDirection  direction)
{
	return direction == LRG_GRAPH_PORT_OUTPUT ? self->outputs : self->inputs;
}

LrgGraphPort *
lrg_graph_node_add_port (LrgGraphNode          *self,
                         LrgGraphPortDirection  direction,
                         const gchar           *name,
                         const gchar           *type_name)
{
	LrgGraphPort *port;

	g_return_val_if_fail (LRG_IS_GRAPH_NODE (self), NULL);
	g_return_val_if_fail (name != NULL, NULL);

	if (lrg_graph_node_find_port (self, direction, name) != NULL)
	{
		g_warning ("node '%s' already has a port named '%s'",
		           self->id != NULL ? self->id : "(new)", name);
		return NULL;
	}

	port = port_new (direction, name, type_name);
	port->node = self;
	g_ptr_array_add (node_ports (self, direction), port);
	self->geometry_dirty = TRUE;
	if (self->graph != NULL)
		graph_node_changed (self->graph, self, TRUE);
	return port;
}

void
lrg_graph_node_remove_port (LrgGraphNode *self,
                            LrgGraphPort *port)
{
	GPtrArray *ports;

	g_return_if_fail (LRG_IS_GRAPH_NODE (self));
	g_return_if_fail (LRG_IS_GRAPH_PORT (port));
	g_return_if_fail (port->node == self);

	if (self->graph != NULL)
		graph_port_removing (self->graph, port);

	ports = node_ports (self, port->direction);
	port->node = NULL;
	g_ptr_array_remove (ports, port);
	self->geometry_dirty = TRUE;
	if (self->graph != NULL)
		graph_node_changed (self->graph, self, TRUE);
}

guint
lrg_graph_node_get_n_ports (LrgGraphNode          *self,
                            LrgGraphPortDirection  direction)
{
	g_return_val_if_fail (LRG_IS_GRAPH_NODE (self), 0);
	return node_ports (self, direction)->len;
}

LrgGraphPort *
lrg_graph_node_get_port (LrgGraphNode          *self,
                         LrgGraphPortDirection  direction,
                         guint                  index)
{
	GPtrArray *ports;

	g_return_val_if_fail (LRG_IS_GRAPH_NODE (self), NULL);
	ports = node_ports (self, direction);
	return index < ports->len ? g_ptr_array_index (ports, index) : NULL;
}

LrgGraphPort *
lrg_graph_node_find_port (LrgGraphNode          *self,
                          LrgGraphPortDirection  direction,
                          const gchar           *name)
{
	GPtrArray *ports;
	guint      i;

	g_return_val_if_fail (LRG_IS_GRAPH_NODE (self), NULL);
	g_return_val_if_fail (name != NULL, NULL);

	ports = node_ports (self, direction);
	for (i = 0; i < ports->len; i++)
	{
		LrgGraphPort *p = g_ptr_array_index (ports, i);

		if (g_strcmp0 (p->name, name) == 0)
			return p;
	}
	return NULL;
}

/* ==========================================================================
 * LrgNodeGraph: type, properties, signals
 * ========================================================================== */

G_DEFINE_FINAL_TYPE (LrgNodeGraph, lrg_node_graph, LRG_TYPE_CONTAINER)

enum
{
	PROP_0,
	PROP_ZOOM,
	PROP_CENTER_X,
	PROP_CENTER_Y,
	PROP_MIN_ZOOM,
	PROP_MAX_ZOOM,
	PROP_GRID_SIZE,
	PROP_GRID_STYLE,
	PROP_SNAP_TO_GRID,
	PROP_SHOW_MINIMAP,
	PROP_ANIMATE_LINKS,
	N_PROPS
};

enum
{
	SIGNAL_NODE_ADDED,
	SIGNAL_NODE_REMOVED,
	SIGNAL_NODE_MOVED,
	SIGNAL_NODE_ACTIVATED,
	SIGNAL_NODE_DUPLICATED,
	SIGNAL_LINK_ADDED,
	SIGNAL_LINK_REMOVED,
	SIGNAL_LINK_DROPPED,
	SIGNAL_CAN_CONNECT,
	SIGNAL_SELECTION_CHANGED,
	N_SIGNALS
};

static GParamSpec *properties[N_PROPS];
static guint       signals[N_SIGNALS];

/* ==========================================================================
 * View mapping
 * ========================================================================== */

typedef struct
{
	gfloat wx;    /* widget world rectangle */
	gfloat wy;
	gfloat w;
	gfloat h;
	gfloat z;     /* displayed zoom */
	gfloat cx;    /* displayed centre (graph units) */
	gfloat cy;
} View;

static void
view_get (LrgNodeGraph *self,
          View         *v)
{
	LrgWidget *w = LRG_WIDGET (self);

	v->wx = lrg_widget_get_world_x (w);
	v->wy = lrg_widget_get_world_y (w);
	v->w = lrg_widget_get_width (w);
	v->h = lrg_widget_get_height (w);
	v->z = MAX (0.0001f, self->zoom_s.value);
	v->cx = self->cx_s.value;
	v->cy = self->cy_s.value;
}

static inline gfloat
to_sx (const View *v,
       gfloat      gx)
{
	return v->wx + v->w * 0.5f + (gx - v->cx) * v->z;
}

static inline gfloat
to_sy (const View *v,
       gfloat      gy)
{
	return v->wy + v->h * 0.5f + (gy - v->cy) * v->z;
}

static inline gfloat
to_gx (const View *v,
       gfloat      sx)
{
	return v->cx + (sx - (v->wx + v->w * 0.5f)) / v->z;
}

static inline gfloat
to_gy (const View *v,
       gfloat      sy)
{
	return v->cy + (sy - (v->wy + v->h * 0.5f)) / v->z;
}

static gboolean
smooth (LrgNodeGraph *self)
{
	return lrg_widget_get_motion (LRG_WIDGET (self)) != LRG_MOTION_NONE && self->stepped;
}

static gfloat
clamp_zoom (LrgNodeGraph *self,
            gfloat        zoom)
{
	if (!isfinite (zoom))
		zoom = 1.0f;
	return CLAMP (zoom, self->min_zoom, self->max_zoom);
}

/* relayout_bodies: re-place body widgets after the view or a node moved */
static void
relayout_bodies (LrgNodeGraph *self)
{
	if (self->n_bodies > 0)
		lrg_container_layout_children (LRG_CONTAINER (self));
}

/* notify_view: notifies the view properties whose targets changed */
static void
notify_view (LrgNodeGraph *self,
             gfloat        old_zoom,
             gfloat        old_cx,
             gfloat        old_cy)
{
	if (self->zoom_s.target != old_zoom)
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ZOOM]);
	if (self->cx_s.target != old_cx)
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_CENTER_X]);
	if (self->cy_s.target != old_cy)
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_CENTER_Y]);
}

/*
 * apply_anchor:
 *
 * In anchored mode the centre follows the zoom so the anchor graph
 * point stays at its screen offset: centre = anchor - offset / zoom.
 * The value tracks the displayed zoom, the target the target zoom.
 */
static void
apply_anchor (LrgNodeGraph *self)
{
	gfloat z = MAX (0.0001f, self->zoom_s.value);
	gfloat zt = MAX (0.0001f, self->zoom_s.target);

	self->cx_s.value = self->anchor_gx - self->anchor_rx / z;
	self->cy_s.value = self->anchor_gy - self->anchor_ry / z;
	self->cx_s.target = self->anchor_gx - self->anchor_rx / zt;
	self->cy_s.target = self->anchor_gy - self->anchor_ry / zt;
	self->cx_s.velocity = 0.0f;
	self->cy_s.velocity = 0.0f;
}

/* ==========================================================================
 * Geometry
 * ========================================================================== */

static GrlFont *
ui_font (LrgNodeGraph *self)
{
	return lrg_theme_get_ui_font (lrg_widget_get_theme (LRG_WIDGET (self)));
}

static gfloat
font_size (LrgNodeGraph *self)
{
	return lrg_theme_get_metric (lrg_widget_get_theme (LRG_WIDGET (self)), LRG_THEME_METRIC_FONT_SIZE);
}

static gfloat
font_size_small (LrgNodeGraph *self)
{
	return lrg_theme_get_metric (lrg_widget_get_theme (LRG_WIDGET (self)),
	                             LRG_THEME_METRIC_FONT_SIZE_SMALL);
}

static gfloat
text_width (GrlFont     *font,
            const gchar *text,
            gfloat       size)
{
	gfloat w = 0.0f;

	if (text == NULL || *text == '\0')
		return 0.0f;
	lrg_paint_measure_text (font, text, size, 0.0f, &w, NULL);
	return w;
}

/*
 * ensure_geometry:
 *
 * Measures a node card at zoom 1: the header, one row per port pair
 * (inputs and outputs share rows), then the body slot. The width fits
 * the title, the widest input label beside the widest output label,
 * and the body's preferred width, never below the node's min-width.
 */
static void
ensure_geometry (LrgNodeGraph *self,
                 LrgGraphNode *node)
{
	GrlFont *font;
	gfloat   fs = font_size (self);
	gfloat   fss = font_size_small (self);
	gfloat   max_in = 0.0f;
	gfloat   max_out = 0.0f;
	gfloat   width;
	gfloat   bw = 0.0f;
	gfloat   bh = 0.0f;
	guint    rows;
	guint    i;

	if (!node->geometry_dirty && node->geometry_font == fs)
		return;

	font = ui_font (self);
	node->title_w = text_width (font, node->title, fs);

	/* --- ports: rows and label widths --- */
	rows = MAX (node->inputs->len, node->outputs->len);
	for (i = 0; i < node->inputs->len; i++)
	{
		LrgGraphPort *p = g_ptr_array_index (node->inputs, i);

		p->label_w = text_width (font, p->name, fss);
		p->rel_y = HEADER_H + ROWS_TOP + (gfloat) i * ROW_H + ROW_H * 0.5f;
		max_in = MAX (max_in, p->label_w);
	}
	for (i = 0; i < node->outputs->len; i++)
	{
		LrgGraphPort *p = g_ptr_array_index (node->outputs, i);

		p->label_w = text_width (font, p->name, fss);
		p->rel_y = HEADER_H + ROWS_TOP + (gfloat) i * ROW_H + ROW_H * 0.5f;
		max_out = MAX (max_out, p->label_w);
	}

	/* --- body --- */
	if (node->body != NULL && lrg_widget_get_visible (node->body))
		lrg_widget_get_preferred_size (node->body, &bw, &bh);

	/* --- width --- */
	width = MAX (node->min_width, node->title_w + PAD * 2.0f + 8.0f);
	width = MAX (width, max_in + max_out + LABEL_INSET * 2.0f + LABEL_GAP);
	width = MAX (width, bw + PAD * 2.0f);
	node->w = ceilf (width);

	/* --- height --- */
	node->body_y = HEADER_H + ROWS_TOP + (gfloat) rows * ROW_H + (rows > 0 ? 4.0f : 2.0f);
	node->body_h = bh;
	if (bh > 0.0f)
		node->h = ceilf (node->body_y + bh + PAD);
	else
		node->h = ceilf (MAX (HEADER_H + 14.0f, node->body_y + 4.0f));

	node->geometry_dirty = FALSE;
	node->geometry_font = fs;
}

/* port_graph_pos: port centre in graph units */
static void
port_graph_pos (LrgNodeGraph *self,
                LrgGraphPort *port,
                gfloat       *x,
                gfloat       *y)
{
	LrgGraphNode *node = port->node;

	ensure_geometry (self, node);
	*x = node->x + (port->direction == LRG_GRAPH_PORT_OUTPUT ? node->w : 0.0f);
	*y = node->y + port->rel_y;
}

/* port_screen_pos: port centre in canvas units through the displayed view */
static void
port_screen_pos (LrgNodeGraph *self,
                 const View   *v,
                 LrgGraphPort *port,
                 gfloat       *x,
                 gfloat       *y)
{
	gfloat gx;
	gfloat gy;

	port_graph_pos (self, port, &gx, &gy);
	*x = to_sx (v, gx);
	*y = to_sy (v, gy);
}

/*
 * wire_points:
 *
 * The cubic Bezier of a wire from an output at (x0, y0) to an input at
 * (x3, y3): horizontal tangents whose length grows with the distance,
 * so short wires stay tidy and backwards wires loop around.
 */
static void
wire_points (gfloat  x0,
             gfloat  y0,
             gfloat  x3,
             gfloat  y3,
             gfloat  zoom,
             gfloat  pts[8])
{
	gfloat d = MAX (fabsf (x3 - x0) * 0.5f, 50.0f * zoom);

	d = MIN (d, MAX (220.0f * zoom, fabsf (y3 - y0) * 0.5f));
	pts[0] = x0;
	pts[1] = y0;
	pts[2] = x0 + d;
	pts[3] = y0;
	pts[4] = x3 - d;
	pts[5] = y3;
	pts[6] = x3;
	pts[7] = y3;
}

static void
bezier_at (const gfloat  pts[8],
           gfloat        t,
           gfloat       *x,
           gfloat       *y)
{
	gfloat u = 1.0f - t;
	gfloat a = u * u * u;
	gfloat b = 3.0f * u * u * t;
	gfloat c = 3.0f * u * t * t;
	gfloat d = t * t * t;

	*x = a * pts[0] + b * pts[2] + c * pts[4] + d * pts[6];
	*y = a * pts[1] + b * pts[3] + c * pts[5] + d * pts[7];
}

/* segment_distance: distance from (px, py) to the segment a-b */
static gfloat
segment_distance (gfloat px,
                  gfloat py,
                  gfloat ax,
                  gfloat ay,
                  gfloat bx,
                  gfloat by)
{
	gfloat dx = bx - ax;
	gfloat dy = by - ay;
	gfloat len2 = dx * dx + dy * dy;
	gfloat t = len2 > 0.0f ? ((px - ax) * dx + (py - ay) * dy) / len2 : 0.0f;
	gfloat qx;
	gfloat qy;

	t = CLAMP (t, 0.0f, 1.0f);
	qx = ax + dx * t - px;
	qy = ay + dy * t - py;
	return sqrtf (qx * qx + qy * qy);
}

/* link_wire: the screen-space control points of a link */
static void
link_wire (LrgNodeGraph *self,
           const View   *v,
           LrgGraphLink *link,
           gfloat        pts[8])
{
	gfloat x0;
	gfloat y0;
	gfloat x3;
	gfloat y3;

	port_screen_pos (self, v, link->output, &x0, &y0);
	port_screen_pos (self, v, link->input, &x3, &y3);
	wire_points (x0, y0, x3, y3, v->z, pts);
}

/* rects_overlap: two rectangles given as x, y, w, h intersect */
static gboolean
rects_overlap (gfloat ax,
               gfloat ay,
               gfloat aw,
               gfloat ah,
               gfloat bx,
               gfloat by,
               gfloat bw,
               gfloat bh)
{
	return ax < bx + bw && ax + aw > bx && ay < by + bh && ay + ah > by;
}

/* nodes_bounds: union of the node cards in graph units; FALSE if none */
static gboolean
nodes_bounds (LrgNodeGraph *self,
              gboolean      selected_only,
              gfloat       *x0,
              gfloat       *y0,
              gfloat       *x1,
              gfloat       *y1)
{
	gboolean any = FALSE;
	guint    i;

	for (i = 0; i < self->nodes->len; i++)
	{
		LrgGraphNode *n = g_ptr_array_index (self->nodes, i);

		if (selected_only && !n->selected)
			continue;
		ensure_geometry (self, n);
		if (!any)
		{
			*x0 = n->x;
			*y0 = n->y;
			*x1 = n->x + n->w;
			*y1 = n->y + n->h;
			any = TRUE;
			continue;
		}
		*x0 = MIN (*x0, n->x);
		*y0 = MIN (*y0, n->y);
		*x1 = MAX (*x1, n->x + n->w);
		*y1 = MAX (*y1, n->y + n->h);
	}
	return any;
}

/* ==========================================================================
 * Node motion (lift and selection springs)
 * ========================================================================== */

static void
update_node_motion (LrgNodeGraph *self,
                    LrgGraphNode *node)
{
	gfloat lift = 0.0f;

	if (node->selected && self->drag == DRAG_MOVE)
		lift = 1.0f;
	else if (node->selected)
		lift = 0.35f;

	lrg_spring_set_target (&node->lift, lift);
	lrg_spring_set_target (&node->sel, node->selected ? 1.0f : 0.0f);
	if (!smooth (self))
	{
		lrg_spring_snap (&node->lift, lift);
		lrg_spring_snap (&node->sel, node->selected ? 1.0f : 0.0f);
		node->animating = FALSE;
		return;
	}
	node->animating = TRUE;
}

/* ==========================================================================
 * Graph hooks from nodes and ports
 * ========================================================================== */

static void
graph_node_changed (LrgNodeGraph *self,
                    LrgGraphNode *node,
                    gboolean      geometry)
{
	if (geometry)
	{
		node->geometry_dirty = TRUE;
		if (node->body != NULL)
			relayout_bodies (self);
	}
}

static void
graph_node_moved (LrgNodeGraph *self,
                  LrgGraphNode *node)
{
	if (node->body != NULL)
		relayout_bodies (self);
	g_signal_emit (self, signals[SIGNAL_NODE_MOVED], 0, node);
}

static void
graph_body_changed (LrgNodeGraph *self,
                    LrgGraphNode *node,
                    LrgWidget    *old_body)
{
	LrgContainer *c = LRG_CONTAINER (self);

	self->in_layout = TRUE;
	if (old_body != NULL)
	{
		lrg_container_remove_child (c, old_body);
		self->n_bodies--;
	}
	if (node->body != NULL)
	{
		lrg_container_add_child (c, node->body);
		self->n_bodies++;
	}
	self->in_layout = FALSE;
	sync_body_order (self);
	relayout_bodies (self);
	lrg_widget_queue_layout (LRG_WIDGET (self));
}

/* ==========================================================================
 * Links
 * ========================================================================== */

static void
emit_selection_changed (LrgNodeGraph *self)
{
	g_signal_emit (self, signals[SIGNAL_SELECTION_CHANGED], 0);
}

/*
 * remove_link_at:
 *
 * Detaches the link at @index: port counters, selection and hover
 * pointers are cleared before link-removed is emitted, so handlers see
 * a consistent graph.
 */
static void
remove_link_at (LrgNodeGraph *self,
                guint         index)
{
	g_autoptr(LrgGraphLink) link = g_ptr_array_steal_index (self->links, index);
	gboolean                was_selected = self->selected_link == link;

	link->output->n_links--;
	link->input->n_links--;
	if (was_selected)
		self->selected_link = NULL;
	if (self->hover_link == link)
		self->hover_link = NULL;

	g_signal_emit (self, signals[SIGNAL_LINK_REMOVED], 0, link);
	if (was_selected)
		emit_selection_changed (self);
}

/* remove_links_where: removes every link attached to @port (or any port of @node) */
static void
remove_links_of (LrgNodeGraph *self,
                 LrgGraphPort *port,
                 LrgGraphNode *node)
{
	guint i = self->links->len;

	while (i > 0)
	{
		LrgGraphLink *l;

		i--;
		if (i >= self->links->len)
			continue;
		l = g_ptr_array_index (self->links, i);
		if ((port != NULL && (l->output == port || l->input == port)) ||
		    (node != NULL && (l->output->node == node || l->input->node == node)))
			remove_link_at (self, i);
	}
}

static void
graph_port_removing (LrgNodeGraph *self,
                     LrgGraphPort *port)
{
	remove_links_of (self, port, NULL);
	if (self->hover_port == port)
		self->hover_port = NULL;
	if (self->link_snap == port)
		self->link_snap = NULL;
}

/*
 * default_can_connect:
 *
 * Class handler of ::can-connect: equal types, or either side "any".
 */
static gboolean
default_can_connect (LrgNodeGraph *self,
                     LrgGraphPort *output,
                     LrgGraphPort *input,
                     gpointer      user_data)
{
	return output->type == 0 || input->type == 0 || output->type == input->type;
}

/* structurally_valid: an output and an input on two nodes of this graph */
static gboolean
structurally_valid (LrgNodeGraph *self,
                    LrgGraphPort *output,
                    LrgGraphPort *input)
{
	return output->direction == LRG_GRAPH_PORT_OUTPUT &&
	       input->direction == LRG_GRAPH_PORT_INPUT &&
	       output->node != NULL && input->node != NULL &&
	       output->node->graph == self && input->node->graph == self &&
	       output->node != input->node;
}

gboolean
lrg_node_graph_can_connect (LrgNodeGraph *self,
                            LrgGraphPort *output,
                            LrgGraphPort *input)
{
	gboolean ok = FALSE;

	g_return_val_if_fail (LRG_IS_NODE_GRAPH (self), FALSE);
	g_return_val_if_fail (LRG_IS_GRAPH_PORT (output), FALSE);
	g_return_val_if_fail (LRG_IS_GRAPH_PORT (input), FALSE);

	if (!structurally_valid (self, output, input))
		return FALSE;
	g_signal_emit (self, signals[SIGNAL_CAN_CONNECT], 0, output, input, &ok);
	return ok;
}

LrgGraphLink *
lrg_node_graph_find_link (LrgNodeGraph *self,
                          LrgGraphPort *output,
                          LrgGraphPort *input)
{
	guint i;

	g_return_val_if_fail (LRG_IS_NODE_GRAPH (self), NULL);

	for (i = 0; i < self->links->len; i++)
	{
		LrgGraphLink *l = g_ptr_array_index (self->links, i);

		if (l->output == output && l->input == input)
			return l;
	}
	return NULL;
}

LrgGraphLink *
lrg_node_graph_connect (LrgNodeGraph  *self,
                        LrgGraphPort  *output,
                        LrgGraphPort  *input,
                        GError       **error)
{
	LrgGraphLink *link;

	g_return_val_if_fail (LRG_IS_NODE_GRAPH (self), NULL);
	g_return_val_if_fail (LRG_IS_GRAPH_PORT (output), NULL);
	g_return_val_if_fail (LRG_IS_GRAPH_PORT (input), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* --- validation --- */
	if (!structurally_valid (self, output, input))
	{
		g_set_error (error, LRG_NODE_GRAPH_ERROR, LRG_NODE_GRAPH_ERROR_INVALID_PORTS,
		             "a link needs an output and an input of two different nodes of the graph");
		return NULL;
	}
	if (lrg_node_graph_find_link (self, output, input) != NULL)
	{
		g_set_error (error, LRG_NODE_GRAPH_ERROR, LRG_NODE_GRAPH_ERROR_EXISTS,
		             "%s.%s is already linked to %s.%s", output->node->id, output->name,
		             input->node->id, input->name);
		return NULL;
	}
	if (!lrg_node_graph_can_connect (self, output, input))
	{
		g_set_error (error, LRG_NODE_GRAPH_ERROR, LRG_NODE_GRAPH_ERROR_INCOMPATIBLE,
		             "%s.%s (%s) cannot connect to %s.%s (%s)", output->node->id, output->name,
		             output->type != 0 ? g_quark_to_string (output->type) : "any",
		             input->node->id, input->name,
		             input->type != 0 ? g_quark_to_string (input->type) : "any");
		return NULL;
	}

	/* --- single-link ports drop what they have --- */
	if (!input->multi_link && input->n_links > 0)
		remove_links_of (self, input, NULL);
	if (!output->multi_link && output->n_links > 0)
		remove_links_of (self, output, NULL);

	/* The removal handlers may have removed a node: re-check */
	if (!structurally_valid (self, output, input))
	{
		g_set_error (error, LRG_NODE_GRAPH_ERROR, LRG_NODE_GRAPH_ERROR_INVALID_PORTS,
		             "a port left the graph while its old link was removed");
		return NULL;
	}

	/* --- the link --- */
	link = g_object_new (LRG_TYPE_GRAPH_LINK, NULL);
	link->output = g_object_ref (output);
	link->input = g_object_ref (input);
	output->n_links++;
	input->n_links++;
	g_ptr_array_add (self->links, link);
	g_signal_emit (self, signals[SIGNAL_LINK_ADDED], 0, link);
	return link;
}

LrgGraphLink *
lrg_node_graph_connect_by_name (LrgNodeGraph  *self,
                                const gchar   *output_node,
                                const gchar   *output_port,
                                const gchar   *input_node,
                                const gchar   *input_port,
                                GError       **error)
{
	LrgGraphNode *on;
	LrgGraphNode *in;
	LrgGraphPort *op = NULL;
	LrgGraphPort *ip = NULL;

	g_return_val_if_fail (LRG_IS_NODE_GRAPH (self), NULL);
	g_return_val_if_fail (output_node != NULL && output_port != NULL, NULL);
	g_return_val_if_fail (input_node != NULL && input_port != NULL, NULL);

	on = lrg_node_graph_get_node (self, output_node);
	in = lrg_node_graph_get_node (self, input_node);
	if (on != NULL)
		op = lrg_graph_node_find_port (on, LRG_GRAPH_PORT_OUTPUT, output_port);
	if (in != NULL)
		ip = lrg_graph_node_find_port (in, LRG_GRAPH_PORT_INPUT, input_port);
	if (op == NULL || ip == NULL)
	{
		g_set_error (error, LRG_NODE_GRAPH_ERROR, LRG_NODE_GRAPH_ERROR_INVALID_PORTS,
		             "no such port: %s", op == NULL ? output_port : input_port);
		return NULL;
	}
	return lrg_node_graph_connect (self, op, ip, error);
}

void
lrg_node_graph_disconnect (LrgNodeGraph *self,
                           LrgGraphLink *link)
{
	guint index;

	g_return_if_fail (LRG_IS_NODE_GRAPH (self));
	g_return_if_fail (LRG_IS_GRAPH_LINK (link));

	if (g_ptr_array_find (self->links, link, &index))
		remove_link_at (self, index);
}

void
lrg_node_graph_disconnect_port (LrgNodeGraph *self,
                                LrgGraphPort *port)
{
	g_return_if_fail (LRG_IS_NODE_GRAPH (self));
	g_return_if_fail (LRG_IS_GRAPH_PORT (port));
	remove_links_of (self, port, NULL);
}

guint
lrg_node_graph_get_n_links (LrgNodeGraph *self)
{
	g_return_val_if_fail (LRG_IS_NODE_GRAPH (self), 0);
	return self->links->len;
}

LrgGraphLink *
lrg_node_graph_get_nth_link (LrgNodeGraph *self,
                             guint         index)
{
	g_return_val_if_fail (LRG_IS_NODE_GRAPH (self), NULL);
	return index < self->links->len ? g_ptr_array_index (self->links, index) : NULL;
}

GList *
lrg_node_graph_get_links (LrgNodeGraph *self)
{
	GList *list = NULL;
	guint  i;

	g_return_val_if_fail (LRG_IS_NODE_GRAPH (self), NULL);
	for (i = self->links->len; i > 0; i--)
		list = g_list_prepend (list, g_ptr_array_index (self->links, i - 1));
	return list;
}

GList *
lrg_node_graph_get_port_links (LrgNodeGraph *self,
                               LrgGraphPort *port)
{
	GList *list = NULL;
	guint  i;

	g_return_val_if_fail (LRG_IS_NODE_GRAPH (self), NULL);
	g_return_val_if_fail (LRG_IS_GRAPH_PORT (port), NULL);

	for (i = self->links->len; i > 0; i--)
	{
		LrgGraphLink *l = g_ptr_array_index (self->links, i - 1);

		if (l->output == port || l->input == port)
			list = g_list_prepend (list, l);
	}
	return list;
}

/* ==========================================================================
 * Nodes
 * ========================================================================== */

LrgNodeGraph *
lrg_node_graph_new (void)
{
	return g_object_new (LRG_TYPE_NODE_GRAPH, NULL);
}

/* unique_id: @base, or @base-2, @base-3 ... whichever is free */
static gchar *
unique_id (LrgNodeGraph *self,
           const gchar  *base)
{
	guint n;

	if (!g_hash_table_contains (self->by_id, base))
		return g_strdup (base);
	for (n = 2;; n++)
	{
		gchar *id = g_strdup_printf ("%s-%u", base, n);

		if (!g_hash_table_contains (self->by_id, id))
			return id;
		g_free (id);
	}
}

gboolean
lrg_node_graph_append_node (LrgNodeGraph *self,
                            LrgGraphNode *node)
{
	g_return_val_if_fail (LRG_IS_NODE_GRAPH (self), FALSE);
	g_return_val_if_fail (LRG_IS_GRAPH_NODE (node), FALSE);
	g_return_val_if_fail (node->graph == NULL, FALSE);

	/* --- id: automatic or unique --- */
	if (node->id == NULL)
	{
		do
		{
			g_free (node->id);
			node->id = g_strdup_printf ("node-%u", ++self->next_id);
		}
		while (g_hash_table_contains (self->by_id, node->id));
		g_object_notify_by_pspec (G_OBJECT (node), node_props[NODE_PROP_ID]);
	}
	else if (g_hash_table_contains (self->by_id, node->id))
	{
		g_warning ("node graph already has a node with id '%s'", node->id);
		return FALSE;
	}

	/* --- insert on top --- */
	g_ptr_array_add (self->nodes, g_object_ref (node));
	g_hash_table_insert (self->by_id, node->id, node);
	node->graph = self;
	node->geometry_dirty = TRUE;
	update_node_motion (self, node);
	lrg_spring_snap (&node->lift, node->lift.target);
	lrg_spring_snap (&node->sel, node->sel.target);

	if (node->body != NULL)
		graph_body_changed (self, node, NULL);

	g_signal_emit (self, signals[SIGNAL_NODE_ADDED], 0, node);
	return TRUE;
}

LrgGraphNode *
lrg_node_graph_add_node (LrgNodeGraph *self,
                         const gchar  *id,
                         const gchar  *title,
                         gfloat        x,
                         gfloat        y)
{
	g_autoptr(LrgGraphNode) node = NULL;

	g_return_val_if_fail (LRG_IS_NODE_GRAPH (self), NULL);

	if (id != NULL && g_hash_table_contains (self->by_id, id))
	{
		g_warning ("node graph already has a node with id '%s'", id);
		return NULL;
	}
	node = lrg_graph_node_new (id, title);
	node->x = x;
	node->y = y;
	if (!lrg_node_graph_append_node (self, node))
		return NULL;
	return node;
}

void
lrg_node_graph_remove_node (LrgNodeGraph *self,
                            LrgGraphNode *node)
{
	g_autoptr(LrgGraphNode) keep = NULL;
	gboolean                was_selected;
	guint                   index;

	g_return_if_fail (LRG_IS_NODE_GRAPH (self));
	g_return_if_fail (LRG_IS_GRAPH_NODE (node));
	g_return_if_fail (node->graph == self);

	keep = g_object_ref (node);

	/* --- links first (handlers still see the node in the graph) --- */
	remove_links_of (self, NULL, node);
	if (node->graph != self || !g_ptr_array_find (self->nodes, node, &index))
		return;

	/* --- interaction state that points at it --- */
	was_selected = node->selected;
	if (node->selected)
		self->n_selected--;
	node->selected = FALSE;
	if (self->cursor == node)
		self->cursor = NULL;
	if (self->hover_node == node)
		self->hover_node = NULL;
	if (self->press_node == node)
		self->press_node = NULL;
	if (self->last_click_node == node)
		self->last_click_node = NULL;
	if (self->hover_port != NULL && self->hover_port->node == node)
		self->hover_port = NULL;
	if (self->link_snap != NULL && self->link_snap->node == node)
		self->link_snap = NULL;
	if (self->link_origin != NULL && self->link_origin->node == node)
	{
		g_clear_object (&self->link_origin);
		self->drag = DRAG_NONE;
	}
	if (self->compatible != NULL)
		g_hash_table_remove_all (self->compatible);
	if (self->box_base != NULL)
		g_hash_table_remove (self->box_base, node);

	/* --- the node and its body --- */
	if (node->body != NULL)
	{
		self->in_layout = TRUE;
		lrg_container_remove_child (LRG_CONTAINER (self), node->body);
		self->in_layout = FALSE;
		self->n_bodies--;
	}
	g_hash_table_remove (self->by_id, node->id);
	g_ptr_array_remove_index (self->nodes, index);
	node->graph = NULL;

	g_signal_emit (self, signals[SIGNAL_NODE_REMOVED], 0, node);
	if (was_selected)
	{
		g_object_notify_by_pspec (G_OBJECT (node), node_props[NODE_PROP_SELECTED]);
		emit_selection_changed (self);
	}
}

void
lrg_node_graph_clear (LrgNodeGraph *self)
{
	g_return_if_fail (LRG_IS_NODE_GRAPH (self));

	while (self->links->len > 0)
		remove_link_at (self, self->links->len - 1);
	while (self->nodes->len > 0)
		lrg_node_graph_remove_node (self, g_ptr_array_index (self->nodes, self->nodes->len - 1));
}

guint
lrg_node_graph_get_n_nodes (LrgNodeGraph *self)
{
	g_return_val_if_fail (LRG_IS_NODE_GRAPH (self), 0);
	return self->nodes->len;
}

LrgGraphNode *
lrg_node_graph_get_nth_node (LrgNodeGraph *self,
                             guint         index)
{
	g_return_val_if_fail (LRG_IS_NODE_GRAPH (self), NULL);
	return index < self->nodes->len ? g_ptr_array_index (self->nodes, index) : NULL;
}

LrgGraphNode *
lrg_node_graph_get_node (LrgNodeGraph *self,
                         const gchar  *id)
{
	g_return_val_if_fail (LRG_IS_NODE_GRAPH (self), NULL);
	g_return_val_if_fail (id != NULL, NULL);
	return g_hash_table_lookup (self->by_id, id);
}

GList *
lrg_node_graph_get_nodes (LrgNodeGraph *self)
{
	GList *list = NULL;
	guint  i;

	g_return_val_if_fail (LRG_IS_NODE_GRAPH (self), NULL);
	for (i = self->nodes->len; i > 0; i--)
		list = g_list_prepend (list, g_ptr_array_index (self->nodes, i - 1));
	return list;
}

/*
 * sync_body_order:
 *
 * Puts the body widgets in the graph's child list in card stacking
 * order, so the canvas paints and hit-tests the top card's body first.
 */
static void
sync_body_order (LrgNodeGraph *self)
{
	guint i;

	if (self->n_bodies < 2)
		return;
	self->in_layout = TRUE;
	for (i = 0; i < self->nodes->len; i++)
	{
		LrgGraphNode *n = g_ptr_array_index (self->nodes, i);

		if (n->body != NULL)
			lrg_container_reorder_child (LRG_CONTAINER (self), n->body, G_MAXUINT);
	}
	self->in_layout = FALSE;
	relayout_bodies (self);
}

/* raise_node: moves @node to the top of the stacking order */
static void
raise_node (LrgNodeGraph *self,
            LrgGraphNode *node)
{
	guint index;

	if (!g_ptr_array_find (self->nodes, node, &index))
		return;
	if (index + 1 < self->nodes->len)
		g_ptr_array_add (self->nodes, g_ptr_array_steal_index (self->nodes, index));
	sync_body_order (self);
}

/* ==========================================================================
 * Selection
 * ========================================================================== */

/* set_selected: flips one node's flag; returns TRUE if it changed */
static gboolean
set_selected (LrgNodeGraph *self,
              LrgGraphNode *node,
              gboolean      selected)
{
	if (node->selected == selected)
		return FALSE;
	node->selected = selected;
	if (selected)
		self->n_selected++;
	else
		self->n_selected--;
	update_node_motion (self, node);
	g_object_notify_by_pspec (G_OBJECT (node), node_props[NODE_PROP_SELECTED]);
	return TRUE;
}

/* clear_selection: unselects every node and the link, without emitting */
static gboolean
clear_selection (LrgNodeGraph *self)
{
	gboolean changed = self->selected_link != NULL;
	guint    i;

	self->selected_link = NULL;
	for (i = 0; i < self->nodes->len && self->n_selected > 0; i++)
		changed |= set_selected (self, g_ptr_array_index (self->nodes, i), FALSE);
	return changed;
}

void
lrg_node_graph_select_node (LrgNodeGraph *self,
                            LrgGraphNode *node,
                            gboolean      extend)
{
	gboolean changed = FALSE;

	g_return_if_fail (LRG_IS_NODE_GRAPH (self));
	g_return_if_fail (LRG_IS_GRAPH_NODE (node));
	g_return_if_fail (node->graph == self);

	if (!extend)
	{
		/* Only @node: unselect the others but keep @node's flag so it
		 * does not flicker */
		guint i;

		if (self->selected_link != NULL)
		{
			self->selected_link = NULL;
			changed = TRUE;
		}
		for (i = 0; i < self->nodes->len; i++)
		{
			LrgGraphNode *n = g_ptr_array_index (self->nodes, i);

			if (n != node)
				changed |= set_selected (self, n, FALSE);
		}
	}
	changed |= set_selected (self, node, TRUE);
	self->cursor = node;
	if (changed)
		emit_selection_changed (self);
}

void
lrg_node_graph_unselect_node (LrgNodeGraph *self,
                              LrgGraphNode *node)
{
	g_return_if_fail (LRG_IS_NODE_GRAPH (self));
	g_return_if_fail (LRG_IS_GRAPH_NODE (node));

	if (node->graph == self && set_selected (self, node, FALSE))
		emit_selection_changed (self);
}

void
lrg_node_graph_select_all (LrgNodeGraph *self)
{
	gboolean changed = FALSE;
	guint    i;

	g_return_if_fail (LRG_IS_NODE_GRAPH (self));

	if (self->selected_link != NULL)
	{
		self->selected_link = NULL;
		changed = TRUE;
	}
	for (i = 0; i < self->nodes->len; i++)
		changed |= set_selected (self, g_ptr_array_index (self->nodes, i), TRUE);
	if (changed)
		emit_selection_changed (self);
}

void
lrg_node_graph_unselect_all (LrgNodeGraph *self)
{
	g_return_if_fail (LRG_IS_NODE_GRAPH (self));

	if (clear_selection (self))
		emit_selection_changed (self);
}

GList *
lrg_node_graph_get_selected_nodes (LrgNodeGraph *self)
{
	GList *list = NULL;
	guint  i;

	g_return_val_if_fail (LRG_IS_NODE_GRAPH (self), NULL);
	for (i = self->nodes->len; i > 0; i--)
	{
		LrgGraphNode *n = g_ptr_array_index (self->nodes, i - 1);

		if (n->selected)
			list = g_list_prepend (list, n);
	}
	return list;
}

guint
lrg_node_graph_get_n_selected (LrgNodeGraph *self)
{
	g_return_val_if_fail (LRG_IS_NODE_GRAPH (self), 0);
	return self->n_selected;
}

LrgGraphLink *
lrg_node_graph_get_selected_link (LrgNodeGraph *self)
{
	g_return_val_if_fail (LRG_IS_NODE_GRAPH (self), NULL);
	return self->selected_link;
}

void
lrg_node_graph_select_link (LrgNodeGraph *self,
                            LrgGraphLink *link)
{
	gboolean changed;

	g_return_if_fail (LRG_IS_NODE_GRAPH (self));
	g_return_if_fail (link == NULL || LRG_IS_GRAPH_LINK (link));

	if (link != NULL && !g_ptr_array_find (self->links, link, NULL))
		return;
	changed = clear_selection (self);
	self->selected_link = link;
	if (changed || link != NULL)
		emit_selection_changed (self);
}

LrgGraphNode *
lrg_node_graph_get_cursor_node (LrgNodeGraph *self)
{
	g_return_val_if_fail (LRG_IS_NODE_GRAPH (self), NULL);
	return self->cursor;
}

guint
lrg_node_graph_delete_selection (LrgNodeGraph *self)
{
	GList *nodes;
	GList *l;
	guint  removed = 0;

	g_return_val_if_fail (LRG_IS_NODE_GRAPH (self), 0);

	if (self->selected_link != NULL)
	{
		lrg_node_graph_disconnect (self, self->selected_link);
		removed++;
	}

	/* Hold references: removal handlers may remove other nodes */
	nodes = lrg_node_graph_get_selected_nodes (self);
	g_list_foreach (nodes, (GFunc) (void (*) (void)) g_object_ref, NULL);
	for (l = nodes; l != NULL; l = l->next)
	{
		LrgGraphNode *n = l->data;

		if (n->graph == self)
		{
			lrg_node_graph_remove_node (self, n);
			removed++;
		}
	}
	g_list_free_full (nodes, g_object_unref);
	return removed;
}

guint
lrg_node_graph_duplicate_selection (LrgNodeGraph *self)
{
	g_autoptr(GHashTable) copies = NULL;
	GList *originals;
	GList *l;
	guint  made = 0;
	guint  i;
	gfloat offset;

	g_return_val_if_fail (LRG_IS_NODE_GRAPH (self), 0);

	originals = lrg_node_graph_get_selected_nodes (self);
	if (originals == NULL)
		return 0;
	g_list_foreach (originals, (GFunc) (void (*) (void)) g_object_ref, NULL);
	copies = g_hash_table_new (NULL, NULL);
	offset = self->grid_size * 2.0f;

	/* --- copy each node with its ports --- */
	for (l = originals; l != NULL; l = l->next)
	{
		LrgGraphNode            *orig = l->data;
		g_autofree gchar        *base = NULL;
		g_autofree gchar        *id = NULL;
		g_autoptr(LrgGraphNode)  copy = NULL;
		guint                    d;

		if (orig->graph != self)
			continue;
		base = g_strdup_printf ("%s-copy", orig->id);
		id = unique_id (self, base);
		copy = lrg_graph_node_new (id, orig->title);
		copy->x = orig->x + offset;
		copy->y = orig->y + offset;
		copy->category = g_strdup (orig->category);
		copy->has_accent = orig->has_accent;
		copy->accent = orig->accent;
		copy->min_width = orig->min_width;
		for (d = 0; d < 2; d++)
		{
			GPtrArray *ports = d == 0 ? orig->inputs : orig->outputs;

			for (i = 0; i < ports->len; i++)
			{
				LrgGraphPort *p = g_ptr_array_index (ports, i);
				LrgGraphPort *np = lrg_graph_node_add_port (copy, p->direction, p->name,
				                                            lrg_graph_port_get_type_name (p));

				np->multi_link = p->multi_link;
				np->has_color = p->has_color;
				np->color = p->color;
			}
		}
		if (lrg_node_graph_append_node (self, copy))
		{
			g_hash_table_insert (copies, orig, copy);
			made++;
			g_signal_emit (self, signals[SIGNAL_NODE_DUPLICATED], 0, orig, copy);
		}
	}

	/* --- links whose two ends were both copied --- */
	for (i = 0; i < self->links->len; i++)
	{
		LrgGraphLink *link = g_ptr_array_index (self->links, i);
		LrgGraphNode *on = g_hash_table_lookup (copies, link->output->node);
		LrgGraphNode *in = g_hash_table_lookup (copies, link->input->node);
		LrgGraphPort *op;
		LrgGraphPort *ip;

		if (on == NULL || in == NULL || on->graph != self || in->graph != self)
			continue;
		op = lrg_graph_node_find_port (on, LRG_GRAPH_PORT_OUTPUT, link->output->name);
		ip = lrg_graph_node_find_port (in, LRG_GRAPH_PORT_INPUT, link->input->name);
		if (op != NULL && ip != NULL)
			lrg_node_graph_connect (self, op, ip, NULL);
	}

	/* --- the copies become the selection --- */
	if (made > 0)
	{
		GHashTableIter iter;
		gpointer       value;

		clear_selection (self);
		g_hash_table_iter_init (&iter, copies);
		while (g_hash_table_iter_next (&iter, NULL, &value))
			if (LRG_GRAPH_NODE (value)->graph == self)
				set_selected (self, value, TRUE);
		emit_selection_changed (self);
	}
	g_list_free_full (originals, g_object_unref);
	return made;
}

/* ==========================================================================
 * View API
 * ========================================================================== */

gfloat
lrg_node_graph_get_zoom (LrgNodeGraph *self)
{
	g_return_val_if_fail (LRG_IS_NODE_GRAPH (self), 1.0f);
	return self->zoom_s.target;
}

gfloat
lrg_node_graph_get_displayed_zoom (LrgNodeGraph *self)
{
	g_return_val_if_fail (LRG_IS_NODE_GRAPH (self), 1.0f);
	return self->zoom_s.value;
}

void
lrg_node_graph_get_center (LrgNodeGraph *self,
                           gfloat       *x,
                           gfloat       *y)
{
	g_return_if_fail (LRG_IS_NODE_GRAPH (self));

	if (x != NULL)
		*x = self->cx_s.target;
	if (y != NULL)
		*y = self->cy_s.target;
}

void
lrg_node_graph_set_view (LrgNodeGraph *self,
                         gfloat        center_x,
                         gfloat        center_y,
                         gfloat        zoom,
                         gboolean      animate)
{
	gfloat oz;
	gfloat ox;
	gfloat oy;

	g_return_if_fail (LRG_IS_NODE_GRAPH (self));

	oz = self->zoom_s.target;
	ox = self->cx_s.target;
	oy = self->cy_s.target;

	self->anchored = FALSE;
	lrg_spring_set_target (&self->zoom_s, clamp_zoom (self, zoom));
	lrg_spring_set_target (&self->cx_s, isfinite (center_x) ? center_x : 0.0f);
	lrg_spring_set_target (&self->cy_s, isfinite (center_y) ? center_y : 0.0f);
	if (!animate || !smooth (self))
	{
		lrg_spring_snap (&self->zoom_s, self->zoom_s.target);
		lrg_spring_snap (&self->cx_s, self->cx_s.target);
		lrg_spring_snap (&self->cy_s, self->cy_s.target);
		self->view_animating = FALSE;
	}
	else
		self->view_animating = TRUE;

	relayout_bodies (self);
	notify_view (self, oz, ox, oy);
}

void
lrg_node_graph_set_zoom (LrgNodeGraph *self,
                         gfloat        zoom)
{
	View v;

	g_return_if_fail (LRG_IS_NODE_GRAPH (self));
	view_get (self, &v);
	lrg_node_graph_zoom_at (self, v.wx + v.w * 0.5f, v.wy + v.h * 0.5f, zoom, FALSE);
}

void
lrg_node_graph_zoom_at (LrgNodeGraph *self,
                        gfloat        x,
                        gfloat        y,
                        gfloat        zoom,
                        gboolean      animate)
{
	View   v;
	gfloat oz;
	gfloat ox;
	gfloat oy;

	g_return_if_fail (LRG_IS_NODE_GRAPH (self));

	oz = self->zoom_s.target;
	ox = self->cx_s.target;
	oy = self->cy_s.target;
	view_get (self, &v);

	/* The graph point under (x, y) now becomes the anchor */
	self->anchor_gx = to_gx (&v, x);
	self->anchor_gy = to_gy (&v, y);
	self->anchor_rx = x - (v.wx + v.w * 0.5f);
	self->anchor_ry = y - (v.wy + v.h * 0.5f);
	self->anchored = TRUE;

	lrg_spring_set_target (&self->zoom_s, clamp_zoom (self, zoom));
	if (!animate || !smooth (self))
	{
		lrg_spring_snap (&self->zoom_s, self->zoom_s.target);
		apply_anchor (self);
		self->anchored = FALSE;
		self->view_animating = FALSE;
	}
	else
	{
		apply_anchor (self);
		self->view_animating = TRUE;
	}

	relayout_bodies (self);
	notify_view (self, oz, ox, oy);
}

void
lrg_node_graph_pan_by (LrgNodeGraph *self,
                       gfloat        dx,
                       gfloat        dy)
{
	gfloat z;
	gfloat ox;
	gfloat oy;

	g_return_if_fail (LRG_IS_NODE_GRAPH (self));

	ox = self->cx_s.target;
	oy = self->cy_s.target;
	if (self->anchored)
	{
		/* Keep the zoom glide: the anchor moves with the content */
		self->anchor_rx += dx;
		self->anchor_ry += dy;
		apply_anchor (self);
	}
	else
	{
		/* Shift value and target alike: a running glide continues
		 * relative to the content */
		z = MAX (0.0001f, self->zoom_s.value);
		self->cx_s.value -= dx / z;
		self->cy_s.value -= dy / z;
		self->cx_s.target -= dx / MAX (0.0001f, self->zoom_s.target);
		self->cy_s.target -= dy / MAX (0.0001f, self->zoom_s.target);
	}
	relayout_bodies (self);
	notify_view (self, self->zoom_s.target, ox, oy);
}

/* frame_rect: glide to show the graph rectangle x0..x1, y0..y1 */
static void
frame_rect (LrgNodeGraph *self,
            gfloat        x0,
            gfloat        y0,
            gfloat        x1,
            gfloat        y1,
            gboolean      animate)
{
	LrgWidget *w = LRG_WIDGET (self);
	gfloat     ww = lrg_widget_get_width (w);
	gfloat     wh = lrg_widget_get_height (w);
	gfloat     zoom = self->zoom_s.target;
	gfloat     bw = MAX (1.0f, x1 - x0);
	gfloat     bh = MAX (1.0f, y1 - y0);

	if (ww > FRAME_MARGIN * 2.0f + 1.0f && wh > FRAME_MARGIN * 2.0f + 1.0f)
	{
		zoom = MIN ((ww - FRAME_MARGIN * 2.0f) / bw, (wh - FRAME_MARGIN * 2.0f) / bh);
		zoom = MIN (zoom, MIN (1.0f, self->max_zoom));
	}
	lrg_node_graph_set_view (self, (x0 + x1) * 0.5f, (y0 + y1) * 0.5f, zoom, animate);
}

void
lrg_node_graph_frame_all (LrgNodeGraph *self,
                          gboolean      animate)
{
	gfloat x0;
	gfloat y0;
	gfloat x1;
	gfloat y1;

	g_return_if_fail (LRG_IS_NODE_GRAPH (self));

	if (nodes_bounds (self, FALSE, &x0, &y0, &x1, &y1))
		frame_rect (self, x0, y0, x1, y1, animate);
}

void
lrg_node_graph_frame_selection (LrgNodeGraph *self,
                                gboolean      animate)
{
	gfloat x0;
	gfloat y0;
	gfloat x1;
	gfloat y1;

	g_return_if_fail (LRG_IS_NODE_GRAPH (self));

	if (self->n_selected > 0 && nodes_bounds (self, TRUE, &x0, &y0, &x1, &y1))
		frame_rect (self, x0, y0, x1, y1, animate);
	else
		lrg_node_graph_frame_all (self, animate);
}

void
lrg_node_graph_graph_to_screen (LrgNodeGraph *self,
                                gfloat        graph_x,
                                gfloat        graph_y,
                                gfloat       *screen_x,
                                gfloat       *screen_y)
{
	View v;

	g_return_if_fail (LRG_IS_NODE_GRAPH (self));
	view_get (self, &v);
	if (screen_x != NULL)
		*screen_x = to_sx (&v, graph_x);
	if (screen_y != NULL)
		*screen_y = to_sy (&v, graph_y);
}

void
lrg_node_graph_screen_to_graph (LrgNodeGraph *self,
                                gfloat        screen_x,
                                gfloat        screen_y,
                                gfloat       *graph_x,
                                gfloat       *graph_y)
{
	View v;

	g_return_if_fail (LRG_IS_NODE_GRAPH (self));
	view_get (self, &v);
	if (graph_x != NULL)
		*graph_x = to_gx (&v, screen_x);
	if (graph_y != NULL)
		*graph_y = to_gy (&v, screen_y);
}

/* ==========================================================================
 * Geometry and hit testing API
 * ========================================================================== */

void
lrg_node_graph_get_node_bounds (LrgNodeGraph *self,
                                LrgGraphNode *node,
                                gfloat       *x,
                                gfloat       *y,
                                gfloat       *width,
                                gfloat       *height)
{
	g_return_if_fail (LRG_IS_NODE_GRAPH (self));
	g_return_if_fail (LRG_IS_GRAPH_NODE (node));

	ensure_geometry (self, node);
	if (x != NULL)
		*x = node->x;
	if (y != NULL)
		*y = node->y;
	if (width != NULL)
		*width = node->w;
	if (height != NULL)
		*height = node->h;
}

void
lrg_node_graph_get_port_position (LrgNodeGraph *self,
                                  LrgGraphPort *port,
                                  gfloat       *x,
                                  gfloat       *y)
{
	gfloat gx = 0.0f;
	gfloat gy = 0.0f;

	g_return_if_fail (LRG_IS_NODE_GRAPH (self));
	g_return_if_fail (LRG_IS_GRAPH_PORT (port));
	g_return_if_fail (port->node != NULL);

	port_graph_pos (self, port, &gx, &gy);
	if (x != NULL)
		*x = gx;
	if (y != NULL)
		*y = gy;
}

LrgGraphNode *
lrg_node_graph_node_at (LrgNodeGraph *self,
                        gfloat        x,
                        gfloat        y)
{
	View   v;
	gfloat gx;
	gfloat gy;
	guint  i;

	g_return_val_if_fail (LRG_IS_NODE_GRAPH (self), NULL);

	view_get (self, &v);
	gx = to_gx (&v, x);
	gy = to_gy (&v, y);
	for (i = self->nodes->len; i > 0; i--)
	{
		LrgGraphNode *n = g_ptr_array_index (self->nodes, i - 1);

		ensure_geometry (self, n);
		if (gx >= n->x && gx < n->x + n->w && gy >= n->y && gy < n->y + n->h)
			return n;
	}
	return NULL;
}

/*
 * port_near:
 *
 * The port whose dot is closest to (x, y) within @reach canvas units,
 * among @candidates (a set) when given. Nodes are scanned top-down and
 * the first node with a port in reach wins, so a port hidden under a
 * card above it is not picked.
 */
static LrgGraphPort *
port_near (LrgNodeGraph *self,
           gfloat        x,
           gfloat        y,
           gfloat        reach,
           GHashTable   *candidates)
{
	View          v;
	LrgGraphPort *best = NULL;
	gfloat        best_d = reach;
	guint         i;

	view_get (self, &v);
	for (i = self->nodes->len; i > 0; i--)
	{
		LrgGraphNode *n = g_ptr_array_index (self->nodes, i - 1);
		gfloat        r = reach / v.z;
		gfloat        gx = to_gx (&v, x);
		gfloat        gy = to_gy (&v, y);
		guint         d;

		ensure_geometry (self, n);
		/* Cheap reject: outside the card grown by the reach */
		if (gx < n->x - r || gx > n->x + n->w + r || gy < n->y - r || gy > n->y + n->h + r)
			continue;
		for (d = 0; d < 2; d++)
		{
			GPtrArray *ports = d == 0 ? n->inputs : n->outputs;
			guint      k;

			for (k = 0; k < ports->len; k++)
			{
				LrgGraphPort *p = g_ptr_array_index (ports, k);
				gfloat        px;
				gfloat        py;
				gfloat        dist;

				if (candidates != NULL && !g_hash_table_contains (candidates, p))
					continue;
				port_screen_pos (self, &v, p, &px, &py);
				dist = sqrtf ((px - x) * (px - x) + (py - y) * (py - y));
				if (dist <= best_d)
				{
					best_d = dist;
					best = p;
				}
			}
		}
		if (best != NULL)
			return best;
		/* Inside this card but no port: cards below are covered */
		if (gx >= n->x && gx < n->x + n->w && gy >= n->y && gy < n->y + n->h)
			return NULL;
	}
	return NULL;
}

LrgGraphPort *
lrg_node_graph_port_at (LrgNodeGraph *self,
                        gfloat        x,
                        gfloat        y)
{
	g_return_val_if_fail (LRG_IS_NODE_GRAPH (self), NULL);
	return port_near (self, x, y, PORT_REACH, NULL);
}

LrgGraphLink *
lrg_node_graph_link_at (LrgNodeGraph *self,
                        gfloat        x,
                        gfloat        y)
{
	View  v;
	guint i;

	g_return_val_if_fail (LRG_IS_NODE_GRAPH (self), NULL);

	view_get (self, &v);
	for (i = self->links->len; i > 0; i--)
	{
		LrgGraphLink *l = g_ptr_array_index (self->links, i - 1);
		gfloat        pts[8];
		gfloat        px;
		gfloat        py;
		gint          s;

		link_wire (self, &v, l, pts);

		/* Cheap reject on the control polygon's bounding box */
		if (x < MIN (MIN (pts[0], pts[2]), MIN (pts[4], pts[6])) - LINK_REACH ||
		    x > MAX (MAX (pts[0], pts[2]), MAX (pts[4], pts[6])) + LINK_REACH ||
		    y < MIN (MIN (pts[1], pts[3]), MIN (pts[5], pts[7])) - LINK_REACH ||
		    y > MAX (MAX (pts[1], pts[3]), MAX (pts[5], pts[7])) + LINK_REACH)
			continue;

		px = pts[0];
		py = pts[1];
		for (s = 1; s <= LINK_SEGMENTS; s++)
		{
			gfloat qx;
			gfloat qy;

			bezier_at (pts, (gfloat) s / (gfloat) LINK_SEGMENTS, &qx, &qy);
			if (segment_distance (x, y, px, py, qx, qy) <= LINK_REACH)
				return l;
			px = qx;
			py = qy;
		}
	}
	return NULL;
}

void
lrg_node_graph_get_paint_stats (LrgNodeGraph *self,
                                guint        *n_nodes,
                                guint        *n_links)
{
	g_return_if_fail (LRG_IS_NODE_GRAPH (self));

	if (n_nodes != NULL)
		*n_nodes = self->stat_nodes;
	if (n_links != NULL)
		*n_links = self->stat_links;
}

/* ==========================================================================
 * Settings API
 * ========================================================================== */

gfloat
lrg_node_graph_get_min_zoom (LrgNodeGraph *self)
{
	g_return_val_if_fail (LRG_IS_NODE_GRAPH (self), 0.0f);
	return self->min_zoom;
}

gfloat
lrg_node_graph_get_max_zoom (LrgNodeGraph *self)
{
	g_return_val_if_fail (LRG_IS_NODE_GRAPH (self), 0.0f);
	return self->max_zoom;
}

void
lrg_node_graph_set_zoom_range (LrgNodeGraph *self,
                               gfloat        min_zoom,
                               gfloat        max_zoom)
{
	gboolean minc;
	gboolean maxc;

	g_return_if_fail (LRG_IS_NODE_GRAPH (self));

	min_zoom = MAX (0.01f, min_zoom);
	max_zoom = MAX (min_zoom, max_zoom);
	minc = self->min_zoom != min_zoom;
	maxc = self->max_zoom != max_zoom;
	if (!minc && !maxc)
		return;
	self->min_zoom = min_zoom;
	self->max_zoom = max_zoom;
	if (self->zoom_s.target < min_zoom || self->zoom_s.target > max_zoom)
		lrg_node_graph_set_zoom (self, self->zoom_s.target);
	if (minc)
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MIN_ZOOM]);
	if (maxc)
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MAX_ZOOM]);
}

gfloat
lrg_node_graph_get_grid_size (LrgNodeGraph *self)
{
	g_return_val_if_fail (LRG_IS_NODE_GRAPH (self), 0.0f);
	return self->grid_size;
}

void
lrg_node_graph_set_grid_size (LrgNodeGraph *self,
                              gfloat        size)
{
	g_return_if_fail (LRG_IS_NODE_GRAPH (self));

	size = MAX (1.0f, size);
	if (self->grid_size == size)
		return;
	self->grid_size = size;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_GRID_SIZE]);
}

LrgNodeGraphGrid
lrg_node_graph_get_grid_style (LrgNodeGraph *self)
{
	g_return_val_if_fail (LRG_IS_NODE_GRAPH (self), LRG_NODE_GRAPH_GRID_NONE);
	return self->grid_style;
}

void
lrg_node_graph_set_grid_style (LrgNodeGraph     *self,
                               LrgNodeGraphGrid  style)
{
	g_return_if_fail (LRG_IS_NODE_GRAPH (self));

	if (self->grid_style == style)
		return;
	self->grid_style = style;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_GRID_STYLE]);
}

gboolean
lrg_node_graph_get_snap_to_grid (LrgNodeGraph *self)
{
	g_return_val_if_fail (LRG_IS_NODE_GRAPH (self), FALSE);
	return self->snap;
}

void
lrg_node_graph_set_snap_to_grid (LrgNodeGraph *self,
                                 gboolean      snap)
{
	g_return_if_fail (LRG_IS_NODE_GRAPH (self));

	snap = !!snap;
	if (self->snap == snap)
		return;
	self->snap = snap;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SNAP_TO_GRID]);
}

gboolean
lrg_node_graph_get_show_minimap (LrgNodeGraph *self)
{
	g_return_val_if_fail (LRG_IS_NODE_GRAPH (self), FALSE);
	return self->show_minimap;
}

void
lrg_node_graph_set_show_minimap (LrgNodeGraph *self,
                                 gboolean      show)
{
	g_return_if_fail (LRG_IS_NODE_GRAPH (self));

	show = !!show;
	if (self->show_minimap == show)
		return;
	self->show_minimap = show;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SHOW_MINIMAP]);
}

gboolean
lrg_node_graph_get_animate_links (LrgNodeGraph *self)
{
	g_return_val_if_fail (LRG_IS_NODE_GRAPH (self), FALSE);
	return self->animate_links;
}

void
lrg_node_graph_set_animate_links (LrgNodeGraph *self,
                                  gboolean      animate)
{
	g_return_if_fail (LRG_IS_NODE_GRAPH (self));

	animate = !!animate;
	if (self->animate_links == animate)
		return;
	self->animate_links = animate;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ANIMATE_LINKS]);
}

/* ==========================================================================
 * Layout
 * ========================================================================== */

/*
 * lrg_node_graph_layout_children:
 *
 * Places each body widget over its card's body slot at the card's
 * on-screen size. Bodies of culled nodes, and all bodies below
 * BODY_MIN_ZOOM, get an empty rectangle so they receive no input.
 */
static void
lrg_node_graph_layout_children (LrgContainer *container)
{
	LrgNodeGraph *self = LRG_NODE_GRAPH (container);
	View          v;
	guint         i;

	if (self->in_layout || self->n_bodies == 0)
		return;
	self->in_layout = TRUE;
	view_get (self, &v);

	for (i = 0; i < self->nodes->len; i++)
	{
		LrgGraphNode *n = g_ptr_array_index (self->nodes, i);
		gfloat        sx;
		gfloat        sy;
		gfloat        sw;
		gfloat        sh;

		if (n->body == NULL)
			continue;
		/* The body's preferred size may have changed (that is what
		 * usually brings us here) */
		n->geometry_dirty = TRUE;
		ensure_geometry (self, n);

		sx = to_sx (&v, n->x + PAD);
		sy = to_sy (&v, n->y + n->body_y);
		sw = (n->w - PAD * 2.0f) * v.z;
		sh = n->body_h * v.z;
		if (v.z < BODY_MIN_ZOOM ||
		    !rects_overlap (sx, sy, sw, sh, v.wx, v.wy, v.w, v.h))
		{
			lrg_widget_allocate (n->body, 0.0f, 0.0f, 0.0f, 0.0f);
			continue;
		}
		lrg_widget_allocate (n->body, roundf (sx - v.wx), roundf (sy - v.wy), roundf (sw), roundf (sh));
	}
	self->in_layout = FALSE;
}

static void
lrg_node_graph_measure (LrgWidget *widget,
                        gfloat    *preferred_width,
                        gfloat    *preferred_height)
{
	/* A canvas has no natural size: ask for a comfortable area */
	*preferred_width = 480.0f;
	*preferred_height = 320.0f;
}

/* ==========================================================================
 * Looks
 * ========================================================================== */

/*
 * lrg_node_graph_style_defaults:
 *
 * Parts: BOX (canvas background), ITEM (node card; SELECTED adds the
 * accent outline), HEADER (card header: background is the fallback
 * accent, foreground the title colour), LABEL (port labels), MARK
 * (port dots: size, ring colour), TRACK (grid: background minor,
 * foreground major), FILL (wires: size is the thickness, glow the
 * halo, foreground the live wire), INDICATOR (rubber band), POPUP
 * (minimap).
 */
static void
lrg_node_graph_style_defaults (LrgWidget      *widget,
                               LrgUIPart       part,
                               LrgWidgetState  state,
                               LrgPartLook    *look)
{
	LrgTheme *theme = lrg_widget_get_theme (widget);
	gfloat    radius = lrg_theme_get_metric (theme, LRG_THEME_METRIC_CORNER_RADIUS);
	gfloat    bw = MAX (1.0f, lrg_theme_get_metric (theme, LRG_THEME_METRIC_BORDER_WIDTH));
	GrlColor  c;

	switch (part)
	{
	case LRG_UI_PART_BOX:
		lrg_paint_color_mix (lrg_theme_get_color (theme, LRG_THEME_COLOR_WINDOW),
		                     lrg_theme_get_color (theme, LRG_THEME_COLOR_PANEL), 0.5f, &c);
		lrg_part_look_set_background (look, &c);
		lrg_part_look_set_border (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_BORDER), bw);
		lrg_part_look_set_radius (look, radius);
		break;

	case LRG_UI_PART_ITEM:
		lrg_part_look_set_background (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_SURFACE));
		lrg_part_look_set_foreground (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_TEXT));
		lrg_part_look_set_radius (look, MAX (6.0f, radius * 1.25f));
		lrg_part_look_set_shadow (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_SHADOW),
		                          MAX (8.0f, lrg_theme_get_metric (theme, LRG_THEME_METRIC_SHADOW_BLUR)),
		                          MAX (2.0f, lrg_theme_get_metric (theme, LRG_THEME_METRIC_SHADOW_OFFSET_Y)));
		if (state & LRG_WIDGET_STATE_SELECTED)
			lrg_part_look_set_border (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_ACCENT), 2.0f);
		else
			lrg_part_look_set_border (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_BORDER), bw);
		break;

	case LRG_UI_PART_HEADER:
		lrg_part_look_set_background (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_ACCENT));
		lrg_part_look_set_foreground (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_TEXT_ON_ACCENT));
		break;

	case LRG_UI_PART_LABEL:
		lrg_part_look_set_foreground (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_TEXT_MUTED));
		break;

	case LRG_UI_PART_MARK:
		lrg_part_look_set_size (look, PORT_R * 2.0f);
		lrg_part_look_set_border (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_SURFACE), 2.0f);
		break;

	case LRG_UI_PART_TRACK:
		lrg_paint_color_with_alpha (lrg_theme_get_color (theme, LRG_THEME_COLOR_BORDER), 0.35f, &c);
		lrg_part_look_set_background (look, &c);
		lrg_paint_color_with_alpha (lrg_theme_get_color (theme, LRG_THEME_COLOR_BORDER), 0.75f, &c);
		lrg_part_look_set_foreground (look, &c);
		break;

	case LRG_UI_PART_FILL:
		lrg_part_look_set_foreground (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_ACCENT));
		lrg_part_look_set_size (look, 2.5f);
		lrg_paint_color_with_alpha (lrg_theme_get_color (theme, LRG_THEME_COLOR_ACCENT), 0.4f, &c);
		lrg_part_look_set_glow (look, &c, 5.0f);
		break;

	case LRG_UI_PART_INDICATOR:
		lrg_paint_color_with_alpha (lrg_theme_get_color (theme, LRG_THEME_COLOR_ACCENT), 0.12f, &c);
		lrg_part_look_set_background (look, &c);
		lrg_part_look_set_border (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_ACCENT), 1.0f);
		lrg_part_look_set_radius (look, 3.0f);
		break;

	case LRG_UI_PART_POPUP:
		lrg_paint_color_with_alpha (lrg_theme_get_color (theme, LRG_THEME_COLOR_POPUP), 0.92f, &c);
		lrg_part_look_set_background (look, &c);
		lrg_part_look_set_border (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_BORDER), bw);
		lrg_part_look_set_radius (look, lrg_theme_get_metric (theme, LRG_THEME_METRIC_POPUP_RADIUS));
		lrg_part_look_set_shadow (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_SHADOW),
		                          lrg_theme_get_metric (theme, LRG_THEME_METRIC_SHADOW_BLUR),
		                          lrg_theme_get_metric (theme, LRG_THEME_METRIC_SHADOW_OFFSET_Y));
		break;

	default:
		LRG_WIDGET_CLASS (lrg_node_graph_parent_class)->style_defaults (widget, part, state, look);
		return;
	}

	if (state & LRG_WIDGET_STATE_DISABLED)
		lrg_part_look_set_opacity (look, 0.45f);
}

/* node_accent: explicit accent, else category colour, else @fallback */
static void
node_accent (LrgGraphNode   *node,
             const GrlColor *fallback,
             GrlColor       *out)
{
	if (node->has_accent)
		*out = node->accent;
	else if (node->category != NULL && *node->category != '\0')
		palette_color (node->category, 0, out);
	else
		*out = *fallback;
}

/* ==========================================================================
 * Painting
 * ========================================================================== */

/* Looks resolved once per paint, shared by every node */
typedef struct
{
	LrgPartLook item;
	LrgPartLook item_sel;
	LrgPartLook header;
	LrgPartLook label;
	LrgPartLook mark;
	LrgPartLook track;
	LrgPartLook fill;
	LrgPartLook band;
	LrgPartLook popup;
	LrgPartLook focus;
	GrlFont    *font;
	gfloat      fs;
	gfloat      fss;
	GrlColor    text;
	gboolean    focus_visible;
	gboolean    light;        /* light theme: type colours are darkened */
} Looks;

static void
resolve_looks (LrgNodeGraph *self,
               Looks        *k)
{
	LrgWidget     *w = LRG_WIDGET (self);
	LrgTheme      *theme = lrg_widget_get_theme (w);
	LrgWidgetState st = lrg_widget_get_state_flags (w) &
	                    (LRG_WIDGET_STATE_DISABLED | LRG_WIDGET_STATE_FOCUSED);

	lrg_widget_resolve_look_for_state (w, LRG_UI_PART_ITEM, st, &k->item);
	lrg_widget_resolve_look_for_state (w, LRG_UI_PART_ITEM, st | LRG_WIDGET_STATE_SELECTED, &k->item_sel);
	lrg_widget_resolve_look_for_state (w, LRG_UI_PART_HEADER, st, &k->header);
	lrg_widget_resolve_look_for_state (w, LRG_UI_PART_LABEL, st, &k->label);
	lrg_widget_resolve_look_for_state (w, LRG_UI_PART_MARK, st, &k->mark);
	lrg_widget_resolve_look_for_state (w, LRG_UI_PART_TRACK, st, &k->track);
	lrg_widget_resolve_look_for_state (w, LRG_UI_PART_FILL, st, &k->fill);
	lrg_widget_resolve_look_for_state (w, LRG_UI_PART_INDICATOR, st, &k->band);
	lrg_widget_resolve_look_for_state (w, LRG_UI_PART_POPUP, st, &k->popup);
	lrg_widget_resolve_look (w, LRG_UI_PART_FOCUS, &k->focus);
	k->font = lrg_theme_get_ui_font (theme);
	k->fs = font_size (self);
	k->fss = font_size_small (self);
	k->text = *lrg_theme_get_color (theme, LRG_THEME_COLOR_TEXT);
	k->focus_visible = (lrg_widget_get_state_flags (w) & LRG_WIDGET_STATE_FOCUS_VISIBLE) != 0;
	k->light = lrg_paint_color_luminance (lrg_theme_get_color (theme, LRG_THEME_COLOR_WINDOW)) > 0.5f;
}

/*
 * port_color:
 *
 * The colour a port and its wires use: the explicit colour, the theme
 * accent for "any", else the type's palette colour, darkened on light
 * themes so pastel wires keep their contrast.
 */
static void
port_color (LrgNodeGraph *self,
            const Looks  *k,
            LrgGraphPort *port,
            GrlColor     *out)
{
	if (!port->has_color && port->type == 0)
		*out = *lrg_theme_get_color (lrg_widget_get_theme (LRG_WIDGET (self)), LRG_THEME_COLOR_ACCENT);
	else if (!lrg_graph_port_get_color (port, out) && k->light)
		lrg_paint_color_shade (out, -0.3f, out);
}

/*
 * paint_grid:
 *
 * Minor cells every grid-size units and major cells every four, each
 * fading in as its on-screen spacing grows past a few units, so the
 * grid never turns into noise when zoomed out.
 */
static void
paint_grid (LrgNodeGraph *self,
            LrgPaintList *list,
            const View   *v,
            const Looks  *k)
{
	gfloat step = self->grid_size;
	gint   level;

	if (self->grid_style == LRG_NODE_GRAPH_GRID_NONE)
		return;

	/* Coarsen until a major cell is at least 24 canvas units */
	while (step * 4.0f * v->z < 24.0f)
		step *= 4.0f;

	for (level = 0; level < 2; level++)
	{
		gfloat   gstep = level == 0 ? step : step * 4.0f;
		gfloat   s = gstep * v->z;
		gfloat   fade = level == 0 ? CLAMP ((s - 6.0f) / 14.0f, 0.0f, 1.0f)
		                           : CLAMP ((s - 12.0f) / 24.0f, 0.0f, 1.0f);
		GrlColor c = level == 0 ? k->track.background : k->track.foreground;
		gfloat   gx0 = floorf (to_gx (v, v->wx) / gstep) * gstep;
		gfloat   gy0 = floorf (to_gy (v, v->wy) / gstep) * gstep;
		gfloat   gx;
		gfloat   gy;

		if (fade <= 0.01f)
			continue;
		c.a = (guint8) ((gfloat) c.a * fade);

		if (self->grid_style == LRG_NODE_GRAPH_GRID_LINES)
		{
			for (gx = gx0; to_sx (v, gx) <= v->wx + v->w; gx += gstep)
			{
				/* Minor lines under a major one are skipped */
				if (level == 0 && fmodf (fabsf (gx), gstep * 4.0f) < gstep * 0.5f)
					continue;
				lrg_paint_list_line (list, roundf (to_sx (v, gx)) + 0.5f, v->wy,
				                     roundf (to_sx (v, gx)) + 0.5f, v->wy + v->h, 1.0f, &c);
			}
			for (gy = gy0; to_sy (v, gy) <= v->wy + v->h; gy += gstep)
			{
				if (level == 0 && fmodf (fabsf (gy), gstep * 4.0f) < gstep * 0.5f)
					continue;
				lrg_paint_list_line (list, v->wx, roundf (to_sy (v, gy)) + 0.5f,
				                     v->wx + v->w, roundf (to_sy (v, gy)) + 0.5f, 1.0f, &c);
			}
		}
		else
		{
			gfloat cols = v->w / s + 2.0f;
			gfloat rows = v->h / s + 2.0f;
			gfloat r = level == 0 ? 1.0f : 1.6f;

			/* Dots are one instance each: bail out if too dense */
			if (cols * rows > (gfloat) DOT_MAX || s < 10.0f)
				continue;
			for (gx = gx0; to_sx (v, gx) <= v->wx + v->w; gx += gstep)
				for (gy = gy0; to_sy (v, gy) <= v->wy + v->h; gy += gstep)
					lrg_paint_list_circle (list, to_sx (v, gx), to_sy (v, gy), r, &c);
		}
	}
}

/*
 * paint_wire:
 *
 * One wire as a gradient Bezier from @from to @to with a glow halo,
 * stronger when @emphasis (hovered or selected), and flowing pulses
 * when animate-links is on.
 */
static void
paint_wire (LrgNodeGraph   *self,
            LrgPaintList   *list,
            const View     *v,
            const Looks    *k,
            const gfloat    pts[8],
            const GrlColor *from,
            const GrlColor *to,
            gfloat          emphasis,
            gboolean        pulses)
{
	LrgPaintCurveStyle style;
	gfloat             thickness = MAX (1.25f, k->fill.size * v->z) * (1.0f + emphasis * 0.4f);
	GrlColor           mid;

	lrg_paint_curve_style_init (&style, from);
	style.color_start = *from;
	style.color_end = *to;
	lrg_paint_color_mix (from, to, 0.5f, &mid);
	lrg_paint_color_with_alpha (&mid, 0.12f + 0.38f * emphasis, &style.glow);
	style.glow_size = MAX (2.0f, k->fill.glow_size * v->z) * (0.6f + emphasis * 0.8f);
	if (pulses)
	{
		lrg_paint_color_mix (&mid, &(GrlColor){ 255, 255, 255, 255 }, 0.6f, &style.pulse_color);
		style.pulse_count = 3;
		style.pulse_speed = 0.35f;
		style.pulse_phase = 0.0f;
	}
	lrg_paint_list_curve (list, pts[0], pts[1], pts[2], pts[3], pts[4], pts[5], pts[6], pts[7],
	                      thickness, &style);
}

/* curve_visible: the control polygon's box (grown by @pad) meets the view */
static gboolean
curve_visible (const View   *v,
               const gfloat  pts[8],
               gfloat        pad)
{
	gfloat x0 = MIN (MIN (pts[0], pts[2]), MIN (pts[4], pts[6])) - pad;
	gfloat x1 = MAX (MAX (pts[0], pts[2]), MAX (pts[4], pts[6])) + pad;
	gfloat y0 = MIN (MIN (pts[1], pts[3]), MIN (pts[5], pts[7])) - pad;
	gfloat y1 = MAX (MAX (pts[1], pts[3]), MAX (pts[5], pts[7])) + pad;

	return rects_overlap (x0, y0, x1 - x0, y1 - y0, v->wx, v->wy, v->w, v->h);
}

static void
paint_links (LrgNodeGraph *self,
             LrgPaintList *list,
             const View   *v,
             const Looks  *k)
{
	guint i;

	for (i = 0; i < self->links->len; i++)
	{
		LrgGraphLink *l = g_ptr_array_index (self->links, i);
		gfloat        pts[8];
		GrlColor      a;
		GrlColor      b;
		gfloat        emphasis = 0.0f;

		link_wire (self, v, l, pts);
		if (!curve_visible (v, pts, 16.0f))
			continue;

		port_color (self, k, l->output, &a);
		port_color (self, k, l->input, &b);
		if (l == self->selected_link)
			emphasis = 1.0f;
		else if (l == self->hover_link || l->output->node->selected || l->input->node->selected)
			emphasis = 0.5f;
		paint_wire (self, list, v, k, pts, &a, &b, emphasis, self->animate_links);
		self->stat_links++;
	}
}

/*
 * paint_live_link:
 *
 * The wire being dragged out of a port. It ends at the snapped port
 * when one is in reach, else at the pointer; when dragged out of an
 * input it runs backwards (the pointer is the output end).
 */
static void
paint_live_link (LrgNodeGraph *self,
                 LrgPaintList *list,
                 const View   *v,
                 const Looks  *k)
{
	LrgGraphPort *o = self->link_origin;
	gfloat        ox;
	gfloat        oy;
	gfloat        ex = self->pointer_x;
	gfloat        ey = self->pointer_y;
	gfloat        pts[8];
	GrlColor      oc;
	GrlColor      ec;

	if (self->drag != DRAG_LINK || o == NULL || o->node == NULL)
		return;

	port_screen_pos (self, v, o, &ox, &oy);
	port_color (self, k, o, &oc);
	ec = oc;
	if (self->link_snap != NULL)
	{
		port_screen_pos (self, v, self->link_snap, &ex, &ey);
		port_color (self, k, self->link_snap, &ec);
	}
	if (o->direction == LRG_GRAPH_PORT_OUTPUT)
	{
		wire_points (ox, oy, ex, ey, v->z, pts);
		paint_wire (self, list, v, k, pts, &oc, &ec, 1.0f, TRUE);
	}
	else
	{
		wire_points (ex, ey, ox, oy, v->z, pts);
		paint_wire (self, list, v, k, pts, &ec, &oc, 1.0f, TRUE);
	}
}

/*
 * paint_port:
 *
 * A port dot: filled when linked, a ring when free. While a wire is
 * dragged, ports it can land on grow and glow, the others fade.
 */
static void
paint_port (LrgNodeGraph *self,
            LrgPaintList *list,
            const View   *v,
            const Looks  *k,
            LrgGraphPort *p,
            gfloat        sx,
            gfloat        sy)
{
	GrlColor c;
	GrlColor ring;
	gfloat   r = k->mark.size * 0.5f * v->z;
	gfloat   alpha = 1.0f;
	gboolean hot = p == self->hover_port || p == self->link_snap;

	port_color (self, k, p, &c);
	if (self->drag == DRAG_LINK)
	{
		if (self->compatible != NULL && g_hash_table_contains (self->compatible, p))
			hot = TRUE;
		else if (p != self->link_origin)
			alpha = 0.3f;
	}
	if (hot)
	{
		GrlColor glow;

		r *= 1.35f;
		lrg_paint_color_with_alpha (&c, 0.3f * alpha, &glow);
		lrg_paint_list_circle (list, sx, sy, r + 4.0f * v->z, &glow);
	}
	c.a = (guint8) ((gfloat) c.a * alpha);
	lrg_paint_list_circle (list, sx, sy, r, &c);
	if (p->n_links == 0 && p != self->link_snap)
	{
		ring = k->mark.border;
		ring.a = (guint8) ((gfloat) ring.a * alpha);
		lrg_paint_list_circle (list, sx, sy, MAX (0.5f, r - MAX (1.5f, k->mark.border_width * v->z)),
		                       &ring);
	}
}

/*
 * paint_node:
 *
 * One card: lifted shadow (spring), body, header gradient in the
 * accent colour with the title, the selection outline (spring), the
 * keyboard cursor ring, then the ports and their labels.
 */
static void
paint_node (LrgNodeGraph *self,
            LrgPaintList *list,
            const View   *v,
            const Looks  *k,
            LrgGraphNode *n)
{
	gfloat           sx = to_sx (v, n->x);
	gfloat           sy = to_sy (v, n->y);
	gfloat           sw = n->w * v->z;
	gfloat           sh = n->h * v->z;
	gfloat           r = k->item.radii.top_left * v->z;
	gfloat           hh = HEADER_H * v->z;
	gfloat           lift = CLAMP (n->lift.value, 0.0f, 1.5f);
	gfloat           sel = CLAMP (n->sel.value, 0.0f, 1.0f);
	gboolean         text = v->z >= TEXT_MIN_ZOOM;
	LrgCornerRadii   top = { r, r, 0.0f, 0.0f };
	LrgPaintGradient grad;
	GrlColor         accent;
	GrlColor         title;
	GrlColor         shadow;
	guint            d;

	/* --- shadow, lifting while selected / dragged --- */
	shadow = k->item.shadow;
	shadow.a = (guint8) MIN (255.0f, (gfloat) shadow.a * (1.0f + lift * 0.6f));
	if (shadow.a > 0)
		lrg_paint_list_shadow (list, sx, sy, sw, sh, r,
		                       k->item.shadow_blur * (1.0f + lift * 1.2f) * MAX (0.5f, v->z), 0.0f,
		                       0.0f, k->item.shadow_offset_y * (1.0f + lift * 2.0f) * MAX (0.5f, v->z),
		                       &shadow);

	/* --- card --- */
	lrg_paint_list_rect_ex (list, sx, sy, sw, sh, &(LrgCornerRadii){ r, r, r, r },
	                        &k->item.background, NULL, k->item.border_width, &k->item.border);

	/* --- header: accent gradient, title in a contrasting colour --- */
	node_accent (n, &k->header.background, &accent);
	memset (&grad, 0, sizeof grad);
	grad.kind = LRG_PAINT_GRADIENT_LINEAR;
	grad.start = accent;
	lrg_paint_color_shade (&accent, -0.28f, &grad.end);
	grad.angle = 0.0f;
	lrg_paint_list_rect_ex (list, sx, sy, sw, hh, &top, NULL, &grad, 0.0f, NULL);
	if (text && n->title != NULL)
	{
		title = k->header.foreground;
		if (lrg_paint_color_contrast (&title, &accent) < 3.0f)
			title = lrg_paint_color_contrast (&k->text, &accent) >= 3.0f
			        ? k->text : (lrg_paint_color_luminance (&accent) > 0.4f
			                     ? (GrlColor){ 20, 20, 24, 255 } : (GrlColor){ 250, 250, 250, 255 });
		lrg_paint_list_push_clip (list, sx + 4.0f * v->z, sy, MAX (0.0f, sw - 8.0f * v->z), hh);
		lrg_paint_list_text (list, k->font, n->title, roundf (sx + PAD * v->z),
		                     roundf (sy + (hh - k->fs * v->z) * 0.5f), k->fs * v->z, 0.0f, &title);
		lrg_paint_list_pop_clip (list);
	}

	/* --- selection outline (springs in) and keyboard cursor --- */
	if (sel > 0.01f)
	{
		GrlColor c = k->item_sel.border;
		gfloat   grow = 2.0f;

		c.a = (guint8) ((gfloat) c.a * sel);
		lrg_paint_list_border (list, sx - grow, sy - grow, sw + grow * 2.0f, sh + grow * 2.0f, r + grow,
		                       MAX (1.0f, k->item_sel.border_width), &c);
	}
	if (n == self->cursor && k->focus_visible && k->focus.border_width > 0.0f)
	{
		gfloat g = 3.0f + k->focus.border_width;

		lrg_paint_list_border (list, sx - g, sy - g, sw + g * 2.0f, sh + g * 2.0f, r + g,
		                       k->focus.border_width, &k->focus.border);
	}

	/* --- ports and labels --- */
	for (d = 0; d < 2; d++)
	{
		GPtrArray *ports = d == 0 ? n->inputs : n->outputs;
		guint      i;

		for (i = 0; i < ports->len; i++)
		{
			LrgGraphPort *p = g_ptr_array_index (ports, i);
			gfloat        px = d == 0 ? sx : sx + sw;
			gfloat        py = sy + p->rel_y * v->z;

			if (text && p->name != NULL)
			{
				gfloat lx = d == 0 ? px + LABEL_INSET * v->z
				                   : px - LABEL_INSET * v->z - p->label_w * v->z;

				lrg_paint_list_text (list, k->font, p->name, roundf (lx),
				                     roundf (py - k->fss * v->z * 0.5f), k->fss * v->z, 0.0f,
				                     &k->label.foreground);
			}
			paint_port (self, list, v, k, p, px, py);
		}
	}
}

static void
paint_minimap (LrgNodeGraph *self,
               LrgPaintList *list,
               const View   *v,
               const Looks  *k);

static void
lrg_node_graph_paint (LrgWidget    *widget,
                      LrgPaintList *list)
{
	LrgNodeGraph *self = LRG_NODE_GRAPH (widget);
	LrgPartLook   box;
	Looks         k;
	View          v;
	guint         i;

	view_get (self, &v);
	resolve_looks (self, &k);
	self->stat_nodes = 0;
	self->stat_links = 0;

	/* --- background --- */
	lrg_widget_resolve_look (widget, LRG_UI_PART_BOX, &box);
	lrg_widget_paint_box (widget, list, &box, v.wx, v.wy, v.w, v.h);

	lrg_paint_list_push_clip (list, v.wx, v.wy, v.w, v.h);
	paint_grid (self, list, &v, &k);

	/* --- wires under the cards --- */
	paint_links (self, list, &v, &k);
	paint_live_link (self, list, &v, &k);

	/* --- cards bottom to top, each with its body widget --- */
	for (i = 0; i < self->nodes->len; i++)
	{
		LrgGraphNode *n = g_ptr_array_index (self->nodes, i);
		gfloat        grow = 32.0f;   /* shadow and port overhang */

		ensure_geometry (self, n);
		if (!rects_overlap (to_sx (&v, n->x) - grow, to_sy (&v, n->y) - grow,
		                    n->w * v.z + grow * 2.0f, n->h * v.z + grow * 2.0f,
		                    v.wx, v.wy, v.w, v.h))
			continue;
		paint_node (self, list, &v, &k, n);
		self->stat_nodes++;

		if (n->body != NULL && lrg_widget_get_visible (n->body) &&
		    lrg_widget_get_width (n->body) > 0.0f && lrg_widget_get_height (n->body) > 0.0f)
		{
			lrg_paint_list_push_clip (list, to_sx (&v, n->x), to_sy (&v, n->y), n->w * v.z, n->h * v.z);
			lrg_widget_paint (n->body, list);
			lrg_paint_list_pop_clip (list);
		}
	}

	/* --- rubber band --- */
	if (self->drag == DRAG_BOX)
	{
		gfloat x0 = MIN (self->press_x, self->pointer_x);
		gfloat y0 = MIN (self->press_y, self->pointer_y);

		lrg_widget_paint_box (widget, list, &k.band, x0, y0, fabsf (self->pointer_x - self->press_x),
		                      fabsf (self->pointer_y - self->press_y));
	}

	paint_minimap (self, list, &v, &k);
	lrg_paint_list_pop_clip (list);
}

/* ==========================================================================
 * Minimap
 * ========================================================================== */

typedef struct
{
	gfloat x;       /* panel rectangle (canvas units) */
	gfloat y;
	gfloat w;
	gfloat h;
	gfloat gx0;     /* graph rectangle shown */
	gfloat gy0;
	gfloat scale;   /* canvas units per graph unit */
	gfloat ox;      /* offset that centres the content */
	gfloat oy;
} Minimap;

G_STATIC_ASSERT (sizeof (Minimap) == 9 * sizeof (gfloat));

/*
 * minimap_geometry:
 *
 * The panel in the bottom-right corner shows the union of the nodes
 * and the view rectangle, fitted and centred. While the minimap is
 * being scrubbed the geometry from the press is reused.
 *
 * Returns: %FALSE when no minimap is shown
 */
static gboolean
minimap_geometry (LrgNodeGraph *self,
                  const View   *v,
                  Minimap      *m)
{
	gfloat x0;
	gfloat y0;
	gfloat x1;
	gfloat y1;
	gfloat inner_w;
	gfloat inner_h;

	if (!self->show_minimap || v->w < MINIMAP_W + MINIMAP_MARGIN * 2.0f ||
	    v->h < MINIMAP_H + MINIMAP_MARGIN * 2.0f ||
	    !nodes_bounds (self, FALSE, &x0, &y0, &x1, &y1))
		return FALSE;
	if (self->drag == DRAG_MINIMAP)
	{
		memcpy (m, self->mm_frozen, sizeof (Minimap));
		return TRUE;
	}

	/* Include the view so its rectangle is always inside the panel */
	x0 = MIN (x0, to_gx (v, v->wx));
	y0 = MIN (y0, to_gy (v, v->wy));
	x1 = MAX (x1, to_gx (v, v->wx + v->w));
	y1 = MAX (y1, to_gy (v, v->wy + v->h));

	m->w = MINIMAP_W;
	m->h = MINIMAP_H;
	m->x = v->wx + v->w - MINIMAP_MARGIN - m->w;
	m->y = v->wy + v->h - MINIMAP_MARGIN - m->h;
	inner_w = m->w - 12.0f;
	inner_h = m->h - 12.0f;
	m->scale = MIN (inner_w / MAX (1.0f, x1 - x0), inner_h / MAX (1.0f, y1 - y0));
	m->gx0 = x0;
	m->gy0 = y0;
	m->ox = m->x + 6.0f + (inner_w - (x1 - x0) * m->scale) * 0.5f;
	m->oy = m->y + 6.0f + (inner_h - (y1 - y0) * m->scale) * 0.5f;
	return TRUE;
}

static void
paint_minimap (LrgNodeGraph *self,
               LrgPaintList *list,
               const View   *v,
               const Looks  *k)
{
	Minimap  m;
	GrlColor c;
	gfloat   vx0;
	gfloat   vy0;
	gfloat   vx1;
	gfloat   vy1;
	guint    i;

	if (!minimap_geometry (self, v, &m))
		return;

	lrg_widget_paint_box (LRG_WIDGET (self), list, &k->popup, m.x, m.y, m.w, m.h);
	lrg_paint_list_push_clip (list, m.x, m.y, m.w, m.h);

	/* Nodes as accent rectangles */
	for (i = 0; i < self->nodes->len; i++)
	{
		LrgGraphNode *n = g_ptr_array_index (self->nodes, i);

		node_accent (n, &k->header.background, &c);
		c.a = n->selected ? 255 : 190;
		lrg_paint_list_rect (list, m.ox + (n->x - m.gx0) * m.scale, m.oy + (n->y - m.gy0) * m.scale,
		                     MAX (2.0f, n->w * m.scale), MAX (2.0f, n->h * m.scale), 1.5f, &c);
	}

	/* The view rectangle */
	vx0 = m.ox + (to_gx (v, v->wx) - m.gx0) * m.scale;
	vy0 = m.oy + (to_gy (v, v->wy) - m.gy0) * m.scale;
	vx1 = m.ox + (to_gx (v, v->wx + v->w) - m.gx0) * m.scale;
	vy1 = m.oy + (to_gy (v, v->wy + v->h) - m.gy0) * m.scale;
	lrg_paint_color_with_alpha (&k->fill.foreground, 0.1f, &c);
	lrg_paint_list_rect (list, vx0, vy0, vx1 - vx0, vy1 - vy0, 2.0f, &c);
	lrg_paint_list_border (list, vx0, vy0, vx1 - vx0, vy1 - vy0, 2.0f, 1.0f, &k->fill.foreground);
	lrg_paint_list_pop_clip (list);
}

/* minimap_jump: centres the view on the graph point under (x, y) of the minimap */
static void
minimap_jump (LrgNodeGraph *self,
              gfloat        x,
              gfloat        y)
{
	View    v;
	Minimap m;

	view_get (self, &v);
	if (!minimap_geometry (self, &v, &m))
		return;
	lrg_node_graph_set_view (self, m.gx0 + (x - m.ox) / m.scale, m.gy0 + (y - m.oy) / m.scale,
	                         self->zoom_s.target, FALSE);
}

/* ==========================================================================
 * Animation
 * ========================================================================== */

/*
 * lrg_node_graph_step:
 *
 * Advances the clock (double clicks), the view springs and the node
 * lift/selection springs. The first step snaps everything so a new
 * screen does not animate in.
 *
 * Returns: %TRUE while anything moves
 */
static gboolean
lrg_node_graph_step (LrgWidget *widget,
                     gfloat     delta_time)
{
	LrgNodeGraph *self = LRG_NODE_GRAPH (widget);
	LrgMotion     motion = lrg_widget_get_motion (widget);
	gboolean      moving = FALSE;
	guint         i;

	if (delta_time > 0.0f)
		self->clock += (gdouble) delta_time;

	/* --- first frame: everything at rest --- */
	if (!self->stepped)
	{
		self->stepped = TRUE;
		lrg_spring_snap (&self->zoom_s, self->zoom_s.target);
		if (self->anchored)
			apply_anchor (self);
		lrg_spring_snap (&self->cx_s, self->cx_s.target);
		lrg_spring_snap (&self->cy_s, self->cy_s.target);
		self->view_animating = FALSE;
		self->anchored = FALSE;
		for (i = 0; i < self->nodes->len; i++)
		{
			LrgGraphNode *n = g_ptr_array_index (self->nodes, i);

			lrg_spring_snap (&n->lift, n->lift.target);
			lrg_spring_snap (&n->sel, n->sel.target);
			n->animating = FALSE;
		}
		relayout_bodies (self);
		return FALSE;
	}

	/* --- view --- */
	if (self->view_animating)
	{
		gboolean v = FALSE;

		lrg_spring_set_motion (&self->zoom_s, motion == LRG_MOTION_NONE ? motion : LRG_MOTION_SMOOTH);
		lrg_spring_set_motion (&self->cx_s, motion == LRG_MOTION_NONE ? motion : LRG_MOTION_SMOOTH);
		lrg_spring_set_motion (&self->cy_s, motion == LRG_MOTION_NONE ? motion : LRG_MOTION_SMOOTH);
		v |= lrg_spring_step (&self->zoom_s, delta_time);
		if (self->anchored)
			apply_anchor (self);
		else
		{
			v |= lrg_spring_step (&self->cx_s, delta_time);
			v |= lrg_spring_step (&self->cy_s, delta_time);
		}
		if (!v)
		{
			lrg_spring_snap (&self->zoom_s, self->zoom_s.target);
			if (self->anchored)
				apply_anchor (self);
			lrg_spring_snap (&self->cx_s, self->cx_s.target);
			lrg_spring_snap (&self->cy_s, self->cy_s.target);
			self->view_animating = FALSE;
			self->anchored = FALSE;
		}
		relayout_bodies (self);
		moving |= v;
	}

	/* --- node lift and outline --- */
	for (i = 0; i < self->nodes->len; i++)
	{
		LrgGraphNode *n = g_ptr_array_index (self->nodes, i);
		gboolean      a = FALSE;

		if (!n->animating)
			continue;
		lrg_spring_set_motion (&n->lift, motion);
		lrg_spring_set_motion (&n->sel, motion);
		a |= lrg_spring_step (&n->lift, delta_time);
		a |= lrg_spring_step (&n->sel, delta_time);
		if (!a)
		{
			lrg_spring_snap (&n->lift, n->lift.target);
			lrg_spring_snap (&n->sel, n->sel.target);
			n->animating = FALSE;
		}
		moving |= a;
	}
	return moving;
}

/* ==========================================================================
 * Interaction
 * ========================================================================== */

/* compatible_ports: every port a wire from @origin can land on */
static void
collect_compatible (LrgNodeGraph *self,
                    LrgGraphPort *origin)
{
	guint i;

	if (self->compatible == NULL)
		self->compatible = g_hash_table_new (NULL, NULL);
	g_hash_table_remove_all (self->compatible);

	for (i = 0; i < self->nodes->len; i++)
	{
		LrgGraphNode *n = g_ptr_array_index (self->nodes, i);
		GPtrArray    *ports = origin->direction == LRG_GRAPH_PORT_OUTPUT ? n->inputs : n->outputs;
		guint         k;

		for (k = 0; k < ports->len; k++)
		{
			LrgGraphPort *p = g_ptr_array_index (ports, k);
			gboolean      ok;

			ok = origin->direction == LRG_GRAPH_PORT_OUTPUT
			     ? lrg_node_graph_can_connect (self, origin, p)
			     : lrg_node_graph_can_connect (self, p, origin);
			if (ok)
				g_hash_table_add (self->compatible, p);
		}
	}
}

/*
 * begin_link_drag:
 *
 * Starts pulling a wire out of @port. Pulling from an input that has a
 * link detaches it (the last one) and continues from its output end,
 * so wires can be re-routed by grabbing their input end.
 */
static void
begin_link_drag (LrgNodeGraph *self,
                 LrgGraphPort *port)
{
	LrgGraphPort *origin = port;
	guint         i;

	if (port->direction == LRG_GRAPH_PORT_INPUT && port->n_links > 0)
	{
		for (i = self->links->len; i > 0; i--)
		{
			LrgGraphLink *l = g_ptr_array_index (self->links, i - 1);

			if (l->input == port)
			{
				origin = l->output;
				g_object_ref (origin);
				remove_link_at (self, i - 1);
				g_object_unref (origin);
				break;
			}
		}
	}
	if (origin->node == NULL || origin->node->graph != self)
		return;

	g_set_object (&self->link_origin, origin);
	self->link_snap = NULL;
	self->drag = DRAG_LINK;
	collect_compatible (self, origin);
}

static void
end_link_drag (LrgNodeGraph *self,
               gboolean      cancel)
{
	g_autoptr(LrgGraphPort) origin = g_steal_pointer (&self->link_origin);
	LrgGraphPort           *target = self->link_snap;

	self->drag = DRAG_NONE;
	self->link_snap = NULL;
	if (self->compatible != NULL)
		g_hash_table_remove_all (self->compatible);
	if (cancel || origin == NULL || origin->node == NULL)
		return;

	if (target != NULL)
	{
		if (origin->direction == LRG_GRAPH_PORT_OUTPUT)
			lrg_node_graph_connect (self, origin, target, NULL);
		else
			lrg_node_graph_connect (self, target, origin, NULL);
	}
	else if (lrg_node_graph_node_at (self, self->pointer_x, self->pointer_y) == NULL)
	{
		gfloat gx;
		gfloat gy;

		lrg_node_graph_screen_to_graph (self, self->pointer_x, self->pointer_y, &gx, &gy);
		g_signal_emit (self, signals[SIGNAL_LINK_DROPPED], 0, origin, gx, gy);
	}
}

/* start_move: remembers where every selected node started */
static void
start_move (LrgNodeGraph *self)
{
	guint i;

	for (i = 0; i < self->nodes->len; i++)
	{
		LrgGraphNode *n = g_ptr_array_index (self->nodes, i);

		n->drag_x = n->x;
		n->drag_y = n->y;
	}
}

/*
 * drag_move:
 *
 * Moves every selected node by the pointer travel in graph units.
 * With snap-to-grid the pressed node lands on the grid and the others
 * keep their offsets to it.
 */
static void
drag_move (LrgNodeGraph *self)
{
	gfloat z = MAX (0.0001f, self->zoom_s.value);
	gfloat dx = (self->pointer_x - self->press_x) / z;
	gfloat dy = (self->pointer_y - self->press_y) / z;
	guint  i;

	if (self->snap && self->press_node != NULL)
	{
		gfloat g = self->grid_size;
		gfloat nx = roundf ((self->press_node->drag_x + dx) / g) * g;
		gfloat ny = roundf ((self->press_node->drag_y + dy) / g) * g;

		dx = nx - self->press_node->drag_x;
		dy = ny - self->press_node->drag_y;
	}

	for (i = 0; i < self->nodes->len; i++)
	{
		LrgGraphNode *n = g_ptr_array_index (self->nodes, i);

		if (n->selected)
			node_place (n, n->drag_x + dx, n->drag_y + dy);
	}
	relayout_bodies (self);
}

/* end_move: node-moved for every node that ended somewhere new */
static void
end_move (LrgNodeGraph *self,
          gboolean      cancel)
{
	g_autoptr(GPtrArray) moved = g_ptr_array_new_with_free_func (g_object_unref);
	guint                i;

	self->drag = DRAG_NONE;
	for (i = 0; i < self->nodes->len; i++)
	{
		LrgGraphNode *n = g_ptr_array_index (self->nodes, i);

		if (!n->selected)
			continue;
		if (cancel)
			node_place (n, n->drag_x, n->drag_y);
		else if (n->x != n->drag_x || n->y != n->drag_y)
			g_ptr_array_add (moved, g_object_ref (n));
		update_node_motion (self, n);
	}
	relayout_bodies (self);
	for (i = 0; i < moved->len; i++)
		g_signal_emit (self, signals[SIGNAL_NODE_MOVED], 0, g_ptr_array_index (moved, i));
}

/*
 * box_update:
 *
 * Live rubber-band selection: the nodes selected when the drag began
 * (with Shift / Ctrl) plus every card the band touches.
 */
static void
box_update (LrgNodeGraph *self)
{
	View     v;
	gfloat   gx0;
	gfloat   gy0;
	gfloat   gx1;
	gfloat   gy1;
	gboolean changed = FALSE;
	guint    i;

	view_get (self, &v);
	gx0 = to_gx (&v, MIN (self->press_x, self->pointer_x));
	gy0 = to_gy (&v, MIN (self->press_y, self->pointer_y));
	gx1 = to_gx (&v, MAX (self->press_x, self->pointer_x));
	gy1 = to_gy (&v, MAX (self->press_y, self->pointer_y));

	for (i = 0; i < self->nodes->len; i++)
	{
		LrgGraphNode *n = g_ptr_array_index (self->nodes, i);
		gboolean      want;

		ensure_geometry (self, n);
		want = (self->box_base != NULL && g_hash_table_contains (self->box_base, n)) ||
		       rects_overlap (n->x, n->y, n->w, n->h, gx0, gy0, gx1 - gx0, gy1 - gy0);
		changed |= set_selected (self, n, want);
	}
	if (changed)
		emit_selection_changed (self);
}

static void
cancel_drag (LrgNodeGraph *self)
{
	switch (self->drag)
	{
	case DRAG_MOVE:
		end_move (self, TRUE);
		break;
	case DRAG_LINK:
		end_link_drag (self, TRUE);
		break;
	case DRAG_BOX:
		{
			gboolean changed = FALSE;
			guint    i;

			for (i = 0; i < self->nodes->len; i++)
			{
				LrgGraphNode *n = g_ptr_array_index (self->nodes, i);

				changed |= set_selected (self, n, self->box_base != NULL &&
				                         g_hash_table_contains (self->box_base, n));
			}
			if (changed)
				emit_selection_changed (self);
		}
		break;
	default:
		break;
	}
	self->drag = DRAG_NONE;
}

/* space_held: Space (for Space + drag panning) is down */
static gboolean
space_held (LrgNodeGraph *self)
{
	if (IsWindowReady ())
		return self->space_armed && IsKeyDown (KEY_SPACE);
	return self->space_armed;
}

/* press_node: a left press on a card (not on a port) */
static void
press_node (LrgNodeGraph      *self,
            LrgGraphNode      *node,
            LrgInputModifiers  mods)
{
	gboolean additive = (mods & (LRG_INPUT_MODIFIER_SHIFT | LRG_INPUT_MODIFIER_CTRL)) != 0;

	/* --- double click (two presses on the same spot) activates --- */
	if (node == self->last_click_node && self->clock - self->last_click_time <= DOUBLE_CLICK &&
	    fabsf (self->press_x - self->last_click_x) + fabsf (self->press_y - self->last_click_y) <=
	    DRAG_THRESHOLD * 2.0f)
	{
		self->last_click_node = NULL;
		self->drag = DRAG_NONE;
		lrg_node_graph_select_node (self, node, FALSE);
		g_signal_emit (self, signals[SIGNAL_NODE_ACTIVATED], 0, node);
		return;
	}
	self->last_click_node = node;
	self->last_click_time = self->clock;
	self->last_click_x = self->press_x;
	self->last_click_y = self->press_y;

	/* --- selection --- */
	if (additive && node->selected && (mods & LRG_INPUT_MODIFIER_CTRL))
	{
		lrg_node_graph_unselect_node (self, node);
		self->drag = DRAG_NONE;
		return;
	}
	if (additive || !node->selected)
		lrg_node_graph_select_node (self, node, additive);
	self->cursor = node;
	raise_node (self, node);

	/* --- arm a move --- */
	self->press_node = node;
	self->drag = DRAG_PENDING;
	self->drag_additive = additive;
	start_move (self);
}

/* update_hover: which port / node / link the pointer is over */
static void
update_hover (LrgNodeGraph *self,
              gfloat        x,
              gfloat        y)
{
	self->hover_port = lrg_node_graph_port_at (self, x, y);
	self->hover_node = self->hover_port != NULL ? self->hover_port->node
	                                            : lrg_node_graph_node_at (self, x, y);
	self->hover_link = (self->hover_port == NULL && self->hover_node == NULL)
	                   ? lrg_node_graph_link_at (self, x, y) : NULL;
}

static gboolean
handle_press (LrgNodeGraph     *self,
              const LrgUIEvent *event)
{
	gfloat            x = lrg_ui_event_get_x (event);
	gfloat            y = lrg_ui_event_get_y (event);
	gint              button = lrg_ui_event_get_button (event);
	LrgInputModifiers mods = lrg_ui_event_get_modifiers (event);
	LrgGraphPort     *port;
	LrgGraphNode     *node;
	LrgGraphLink     *link;
	View              v;
	Minimap           m;

	if (self->drag != DRAG_NONE)
		return TRUE;

	self->press_x = x;
	self->press_y = y;
	self->pointer_x = x;
	self->pointer_y = y;
	self->drag_button = button;

	/* --- panning: middle button, or Space + left --- */
	if (button == GRL_MOUSE_BUTTON_MIDDLE || (button == GRL_MOUSE_BUTTON_LEFT && space_held (self)))
	{
		self->drag = DRAG_PAN;
		return TRUE;
	}
	if (button != GRL_MOUSE_BUTTON_LEFT)
		return FALSE;

	/* --- the minimap sits on top of everything --- */
	view_get (self, &v);
	if (minimap_geometry (self, &v, &m) && x >= m.x && x < m.x + m.w && y >= m.y && y < m.y + m.h)
	{
		memcpy (self->mm_frozen, &m, sizeof (Minimap));
		self->drag = DRAG_MINIMAP;
		minimap_jump (self, x, y);
		return TRUE;
	}

	/* --- ports, cards, wires, then empty canvas --- */
	port = lrg_node_graph_port_at (self, x, y);
	if (port != NULL)
	{
		begin_link_drag (self, port);
		return TRUE;
	}
	node = lrg_node_graph_node_at (self, x, y);
	if (node != NULL)
	{
		press_node (self, node, mods);
		return TRUE;
	}
	link = lrg_node_graph_link_at (self, x, y);
	if (link != NULL)
	{
		lrg_node_graph_select_link (self, link);
		return TRUE;
	}

	/* Rubber band; without modifiers it starts from an empty selection */
	if (self->box_base == NULL)
		self->box_base = g_hash_table_new (NULL, NULL);
	g_hash_table_remove_all (self->box_base);
	if (mods & (LRG_INPUT_MODIFIER_SHIFT | LRG_INPUT_MODIFIER_CTRL))
	{
		guint i;

		for (i = 0; i < self->nodes->len; i++)
		{
			LrgGraphNode *n = g_ptr_array_index (self->nodes, i);

			if (n->selected)
				g_hash_table_add (self->box_base, n);
		}
	}
	else
		lrg_node_graph_unselect_all (self);
	self->drag = DRAG_BOX;
	return TRUE;
}

static gboolean
handle_motion (LrgNodeGraph     *self,
               const LrgUIEvent *event)
{
	gfloat x = lrg_ui_event_get_x (event);
	gfloat y = lrg_ui_event_get_y (event);
	gfloat dx = x - self->pointer_x;
	gfloat dy = y - self->pointer_y;

	self->pointer_x = x;
	self->pointer_y = y;
	self->pointer_in = TRUE;

	switch (self->drag)
	{
	case DRAG_PAN:
		lrg_node_graph_pan_by (self, dx, dy);
		return TRUE;
	case DRAG_MINIMAP:
		minimap_jump (self, x, y);
		return TRUE;
	case DRAG_PENDING:
		if (fabsf (x - self->press_x) + fabsf (y - self->press_y) < DRAG_THRESHOLD)
			return TRUE;
		{
			guint i;

			/* A press that turned into a drag is not half a double click */
			self->drag = DRAG_MOVE;
			self->last_click_node = NULL;
			for (i = 0; i < self->nodes->len; i++)
			{
				LrgGraphNode *n = g_ptr_array_index (self->nodes, i);

				if (n->selected)
					update_node_motion (self, n);
			}
		}
		drag_move (self);
		return TRUE;
	case DRAG_MOVE:
		drag_move (self);
		return TRUE;
	case DRAG_LINK:
		self->link_snap = port_near (self, x, y, SNAP_REACH, self->compatible);
		return TRUE;
	case DRAG_BOX:
		box_update (self);
		return TRUE;
	case DRAG_NONE:
	default:
		update_hover (self, x, y);
		return FALSE;
	}
}

static gboolean
handle_release (LrgNodeGraph     *self,
                const LrgUIEvent *event)
{
	gint button = lrg_ui_event_get_button (event);

	if (self->drag == DRAG_NONE || button != self->drag_button)
		return FALSE;

	self->pointer_x = lrg_ui_event_get_x (event);
	self->pointer_y = lrg_ui_event_get_y (event);

	switch (self->drag)
	{
	case DRAG_PENDING:
		/* A plain click on an already selected card selects only it */
		self->drag = DRAG_NONE;
		if (!self->drag_additive && self->press_node != NULL && self->n_selected > 1)
			lrg_node_graph_select_node (self, self->press_node, FALSE);
		break;
	case DRAG_MOVE:
		end_move (self, FALSE);
		break;
	case DRAG_LINK:
		end_link_drag (self, FALSE);
		break;
	case DRAG_BOX:
		self->drag = DRAG_NONE;
		if (self->box_base != NULL)
			g_hash_table_remove_all (self->box_base);
		break;
	default:
		self->drag = DRAG_NONE;
		break;
	}
	self->press_node = NULL;
	self->space_armed = FALSE;
	update_hover (self, self->pointer_x, self->pointer_y);
	return TRUE;
}

/*
 * move_cursor:
 *
 * Spatial keyboard navigation: from the cursor card to the nearest
 * card in the arrow's direction (distance along the arrow plus twice
 * the sideways distance). The new card is selected and scrolled into
 * view.
 */
static gboolean
move_cursor (LrgNodeGraph *self,
             GrlKey        key)
{
	LrgGraphNode *from = self->cursor;
	LrgGraphNode *best = NULL;
	gfloat        best_score = G_MAXFLOAT;
	gfloat        fx;
	gfloat        fy;
	gfloat        dirx = 0.0f;
	gfloat        diry = 0.0f;
	guint         i;

	if (self->nodes->len == 0)
		return FALSE;

	switch (key)
	{
	case GRL_KEY_LEFT:  dirx = -1.0f; break;
	case GRL_KEY_RIGHT: dirx = 1.0f;  break;
	case GRL_KEY_UP:    diry = -1.0f; break;
	case GRL_KEY_DOWN:  diry = 1.0f;  break;
	default:            return FALSE;
	}

	/* --- no cursor yet: the card nearest the view centre --- */
	if (from == NULL)
	{
		for (i = 0; i < self->nodes->len; i++)
		{
			LrgGraphNode *n = g_ptr_array_index (self->nodes, i);
			gfloat        ddx;
			gfloat        ddy;
			gfloat        s;

			ensure_geometry (self, n);
			ddx = n->x + n->w * 0.5f - self->cx_s.value;
			ddy = n->y + n->h * 0.5f - self->cy_s.value;
			s = ddx * ddx + ddy * ddy;
			if (s < best_score)
			{
				best_score = s;
				best = n;
			}
		}
	}
	else
	{
		ensure_geometry (self, from);
		fx = from->x + from->w * 0.5f;
		fy = from->y + from->h * 0.5f;
		for (i = 0; i < self->nodes->len; i++)
		{
			LrgGraphNode *n = g_ptr_array_index (self->nodes, i);
			gfloat        ddx;
			gfloat        ddy;
			gfloat        along;
			gfloat        side;
			gfloat        s;

			if (n == from)
				continue;
			ensure_geometry (self, n);
			ddx = n->x + n->w * 0.5f - fx;
			ddy = n->y + n->h * 0.5f - fy;
			along = ddx * dirx + ddy * diry;
			side = fabsf (ddx * diry) + fabsf (ddy * dirx);
			if (along <= 0.5f)
				continue;
			s = along + side * 2.0f;
			if (s < best_score)
			{
				best_score = s;
				best = n;
			}
		}
	}

	if (best == NULL)
		return TRUE;   /* at the edge: stay, but keep the arrow */

	lrg_node_graph_select_node (self, best, FALSE);

	/* --- scroll it into view --- */
	{
		View v;

		view_get (self, &v);
		if (to_sx (&v, best->x) < v.wx || to_sy (&v, best->y) < v.wy ||
		    to_sx (&v, best->x + best->w) > v.wx + v.w || to_sy (&v, best->y + best->h) > v.wy + v.h)
			lrg_node_graph_set_view (self, best->x + best->w * 0.5f, best->y + best->h * 0.5f,
			                         self->zoom_s.target, TRUE);
	}
	return TRUE;
}

static gboolean
handle_key (LrgNodeGraph      *self,
            GrlKey             key,
            LrgInputModifiers  mods)
{
	gboolean ctrl = (mods & LRG_INPUT_MODIFIER_CTRL) != 0;
	gboolean other = (mods & (LRG_INPUT_MODIFIER_SHIFT | LRG_INPUT_MODIFIER_ALT)) != 0;

	/* Only plain keys and the two Ctrl shortcuts below are ours: every
	 * other combination falls through to the canvas shortcuts */
	if (other || (ctrl && key != GRL_KEY_A && key != GRL_KEY_D))
		return FALSE;

	switch (key)
	{
	case GRL_KEY_A:
		if (!ctrl)
			return FALSE;
		lrg_node_graph_select_all (self);
		return TRUE;
	case GRL_KEY_D:
		if (!ctrl)
			return FALSE;
		lrg_node_graph_duplicate_selection (self);
		return TRUE;
	case GRL_KEY_DELETE:
	case GRL_KEY_BACKSPACE:
		return lrg_node_graph_delete_selection (self) > 0;
	case GRL_KEY_F:
		lrg_node_graph_frame_selection (self, TRUE);
		return TRUE;
	case GRL_KEY_HOME:
		lrg_node_graph_frame_all (self, TRUE);
		return TRUE;
	case GRL_KEY_LEFT:
	case GRL_KEY_RIGHT:
	case GRL_KEY_UP:
	case GRL_KEY_DOWN:
		return move_cursor (self, key);
	case GRL_KEY_ENTER:
	case GRL_KEY_KP_ENTER:
		if (self->cursor == NULL)
			return FALSE;
		g_signal_emit (self, signals[SIGNAL_NODE_ACTIVATED], 0, self->cursor);
		return TRUE;
	case GRL_KEY_SPACE:
		/* Arms Space + drag panning (Space is not an activation here) */
		self->space_armed = TRUE;
		return TRUE;
	case GRL_KEY_ESCAPE:
		if (self->drag == DRAG_NONE)
			return FALSE;
		cancel_drag (self);
		return TRUE;
	default:
		return FALSE;
	}
}

static gboolean
lrg_node_graph_handle_event (LrgWidget        *widget,
                             const LrgUIEvent *event)
{
	LrgNodeGraph *self = LRG_NODE_GRAPH (widget);
	LrgWidget    *target = lrg_ui_event_get_target (event);

	switch (lrg_ui_event_get_event_type (event))
	{
	case LRG_UI_EVENT_MOUSE_BUTTON_DOWN:
		return handle_press (self, event);

	case LRG_UI_EVENT_MOUSE_MOVE:
		return handle_motion (self, event);

	case LRG_UI_EVENT_MOUSE_BUTTON_UP:
		return handle_release (self, event);

	case LRG_UI_EVENT_SCROLL:
		{
			gfloat sy = lrg_ui_event_get_scroll_y (event);
			gfloat sx = lrg_ui_event_get_scroll_x (event);

			/* Wheel zooms about the pointer; horizontal scroll pans */
			if (sy != 0.0f)
				lrg_node_graph_zoom_at (self, lrg_ui_event_get_x (event), lrg_ui_event_get_y (event),
				                        self->zoom_s.target * powf (WHEEL_STEP, sy), TRUE);
			if (sx != 0.0f)
				lrg_node_graph_pan_by (self, -sx * 40.0f, 0.0f);
			return TRUE;
		}

	case LRG_UI_EVENT_PAN:
		{
			gfloat x = lrg_ui_event_get_x (event);
			gfloat y = lrg_ui_event_get_y (event);
			gfloat dx = lrg_ui_event_get_delta_x (event);
			gfloat dy = lrg_ui_event_get_delta_y (event);

			/* Claim drags that start on empty canvas; a drag on a card
			 * falls through to the mouse replay and moves it */
			if (!self->touch_panning)
			{
				if (self->drag != DRAG_NONE ||
				    lrg_node_graph_port_at (self, x - dx, y - dy) != NULL ||
				    lrg_node_graph_node_at (self, x - dx, y - dy) != NULL)
					return FALSE;
				self->touch_panning = TRUE;
			}
			lrg_node_graph_pan_by (self, dx, dy);
			return TRUE;
		}

	case LRG_UI_EVENT_PAN_END:
		if (!self->touch_panning)
			return FALSE;
		self->touch_panning = FALSE;
		return TRUE;

	case LRG_UI_EVENT_ZOOM:
		lrg_node_graph_zoom_at (self, lrg_ui_event_get_x (event), lrg_ui_event_get_y (event),
		                        self->zoom_s.value * lrg_ui_event_get_scale (event), FALSE);
		return TRUE;

	case LRG_UI_EVENT_KEY_DOWN:
		/* Keys typed into a body widget are its own business */
		if (target != widget)
			return FALSE;
		return handle_key (self, lrg_ui_event_get_key (event), lrg_ui_event_get_modifiers (event));

	case LRG_UI_EVENT_ACTIVATE:
		if (target != widget || self->cursor == NULL)
			return FALSE;
		g_signal_emit (self, signals[SIGNAL_NODE_ACTIVATED], 0, self->cursor);
		return TRUE;

	case LRG_UI_EVENT_CANCEL:
		if (self->drag == DRAG_NONE)
			return FALSE;
		cancel_drag (self);
		return TRUE;

	case LRG_UI_EVENT_POINTER_LEAVE:
		if (target == widget)
		{
			self->pointer_in = FALSE;
			self->hover_node = NULL;
			self->hover_port = NULL;
			self->hover_link = NULL;
		}
		return FALSE;

	case LRG_UI_EVENT_FOCUS_OUT:
		self->space_armed = FALSE;
		return FALSE;

	default:
		return FALSE;
	}
}

/* ==========================================================================
 * YAML
 * ========================================================================== */

static gchar *
color_to_hex (const GrlColor *c)
{
	return g_strdup_printf ("#%02x%02x%02x%02x", c->r, c->g, c->b, c->a);
}

/* hex_to_color: "#rrggbb" or "#rrggbbaa" */
static gboolean
hex_to_color (const gchar *text,
              GrlColor    *out)
{
	guint64 v;
	gchar  *end = NULL;
	gsize   len;

	if (text == NULL || text[0] != '#')
		return FALSE;
	len = strlen (text + 1);
	if (len != 6 && len != 8)
		return FALSE;
	v = g_ascii_strtoull (text + 1, &end, 16);
	if (end == NULL || *end != '\0')
		return FALSE;
	if (len == 6)
		v = (v << 8) | 0xff;
	out->r = (guint8) ((v >> 24) & 0xff);
	out->g = (guint8) ((v >> 16) & 0xff);
	out->b = (guint8) ((v >> 8) & 0xff);
	out->a = (guint8) (v & 0xff);
	return TRUE;
}

gchar *
lrg_node_graph_to_yaml (LrgNodeGraph  *self,
                        gsize         *length,
                        GError       **error)
{
	g_autoptr(YamlGenerator) generator = NULL;
	g_autoptr(YamlNode)      root_node = NULL;
	YamlMapping             *root;
	YamlMapping             *view;
	YamlSequence            *nodes;
	YamlSequence            *links;
	gsize                    len = 0;
	gchar                   *data;
	guint                    i;

	g_return_val_if_fail (LRG_IS_NODE_GRAPH (self), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* --- view --- */
	view = yaml_mapping_new ();
	yaml_mapping_set_double_member (view, "center-x", (gdouble) self->cx_s.target);
	yaml_mapping_set_double_member (view, "center-y", (gdouble) self->cy_s.target);
	yaml_mapping_set_double_member (view, "zoom", (gdouble) self->zoom_s.target);

	/* --- nodes with their ports --- */
	nodes = yaml_sequence_new ();
	for (i = 0; i < self->nodes->len; i++)
	{
		LrgGraphNode *n = g_ptr_array_index (self->nodes, i);
		YamlMapping  *m = yaml_mapping_new ();
		YamlSequence *ports = yaml_sequence_new ();
		guint         d;

		yaml_mapping_set_string_member (m, "id", n->id);
		if (n->title != NULL)
			yaml_mapping_set_string_member (m, "title", n->title);
		yaml_mapping_set_double_member (m, "x", (gdouble) n->x);
		yaml_mapping_set_double_member (m, "y", (gdouble) n->y);
		if (n->category != NULL)
			yaml_mapping_set_string_member (m, "category", n->category);
		if (n->has_accent)
		{
			g_autofree gchar *hex = color_to_hex (&n->accent);

			yaml_mapping_set_string_member (m, "accent", hex);
		}
		if (n->min_width != MIN_WIDTH)
			yaml_mapping_set_double_member (m, "min-width", (gdouble) n->min_width);

		for (d = 0; d < 2; d++)
		{
			GPtrArray *list = d == 0 ? n->inputs : n->outputs;
			guint      k;

			for (k = 0; k < list->len; k++)
			{
				LrgGraphPort *p = g_ptr_array_index (list, k);
				YamlMapping  *pm = yaml_mapping_new ();

				yaml_mapping_set_string_member (pm, "name", p->name);
				yaml_mapping_set_string_member (pm, "direction", d == 0 ? "input" : "output");
				if (p->type != 0)
					yaml_mapping_set_string_member (pm, "type", g_quark_to_string (p->type));
				if (p->multi_link != (p->direction == LRG_GRAPH_PORT_OUTPUT))
					yaml_mapping_set_boolean_member (pm, "multi-link", p->multi_link);
				if (p->has_color)
				{
					g_autofree gchar *hex = color_to_hex (&p->color);

					yaml_mapping_set_string_member (pm, "color", hex);
				}
				yaml_sequence_add_mapping_element (ports, pm);
				yaml_mapping_unref (pm);
			}
		}
		yaml_mapping_set_sequence_member (m, "ports", ports);
		yaml_sequence_unref (ports);
		yaml_sequence_add_mapping_element (nodes, m);
		yaml_mapping_unref (m);
	}

	/* --- links --- */
	links = yaml_sequence_new ();
	for (i = 0; i < self->links->len; i++)
	{
		LrgGraphLink *l = g_ptr_array_index (self->links, i);
		YamlMapping  *m = yaml_mapping_new ();

		yaml_mapping_set_string_member (m, "from", l->output->node->id);
		yaml_mapping_set_string_member (m, "output", l->output->name);
		yaml_mapping_set_string_member (m, "to", l->input->node->id);
		yaml_mapping_set_string_member (m, "input", l->input->name);
		yaml_sequence_add_mapping_element (links, m);
		yaml_mapping_unref (m);
	}

	/* --- document --- */
	root = yaml_mapping_new ();
	yaml_mapping_set_int_member (root, "version", 1);
	yaml_mapping_set_mapping_member (root, "view", view);
	yaml_mapping_set_sequence_member (root, "nodes", nodes);
	yaml_mapping_set_sequence_member (root, "links", links);
	yaml_mapping_unref (view);
	yaml_sequence_unref (nodes);
	yaml_sequence_unref (links);
	root_node = yaml_node_new_mapping (root);
	yaml_mapping_unref (root);

	generator = yaml_generator_new ();
	yaml_generator_set_root (generator, root_node);
	data = yaml_generator_to_data (generator, &len, error);
	if (data != NULL && length != NULL)
		*length = len;
	return data;
}

/* Parsed document, validated before the graph is touched */
typedef struct
{
	gchar    *name;
	gint      direction;
	gchar    *type;
	gint      multi;       /* -1: default */
	gboolean  has_color;
	GrlColor  color;
} StagedPort;

typedef struct
{
	gchar    *id;
	gchar    *title;
	gchar    *category;
	gdouble   x;
	gdouble   y;
	gdouble   min_width;
	gboolean  has_accent;
	GrlColor  accent;
	GArray   *ports;       /* StagedPort */
} StagedNode;

typedef struct
{
	gchar *from;
	gchar *output;
	gchar *to;
	gchar *input;
} StagedLink;

static void
staged_port_clear (gpointer data)
{
	StagedPort *p = data;

	g_free (p->name);
	g_free (p->type);
}

static void
staged_node_clear (gpointer data)
{
	StagedNode *n = data;

	g_free (n->id);
	g_free (n->title);
	g_free (n->category);
	g_clear_pointer (&n->ports, g_array_unref);
}

static void
staged_link_clear (gpointer data)
{
	StagedLink *l = data;

	g_free (l->from);
	g_free (l->output);
	g_free (l->to);
	g_free (l->input);
}

/* scalar_member: a scalar member's text, %NULL when absent; FALSE if not a scalar */
static gboolean
scalar_member (YamlMapping  *m,
               const gchar  *key,
               const gchar **out)
{
	YamlNode *node;

	*out = NULL;
	if (!yaml_mapping_has_member (m, key))
		return TRUE;
	node = yaml_mapping_get_member (m, key);
	if (node == NULL || yaml_node_get_node_type (node) == YAML_NODE_NULL)
		return TRUE;
	if (yaml_node_get_node_type (node) != YAML_NODE_SCALAR)
		return FALSE;
	*out = yaml_node_get_scalar (node);
	return TRUE;
}

/* number_member: a finite number, @fallback when absent */
static gboolean
number_member (YamlMapping *m,
               const gchar *key,
               gdouble      fallback,
               gdouble     *out)
{
	const gchar *text;
	gchar       *end = NULL;

	if (!scalar_member (m, key, &text))
		return FALSE;
	if (text == NULL)
	{
		*out = fallback;
		return TRUE;
	}
	*out = g_ascii_strtod (text, &end);
	return end != NULL && end != text && *end == '\0' && isfinite (*out);
}

static gboolean
parse_fail (GError      **error,
            const gchar  *what,
            guint         index)
{
	g_set_error (error, LRG_NODE_GRAPH_ERROR, LRG_NODE_GRAPH_ERROR_PARSE,
	             "node graph YAML: bad %s (entry %u)", what, index);
	return FALSE;
}

/* stage_node: parses one `nodes:` entry */
static gboolean
stage_node (YamlNode    *item,
            guint        index,
            StagedNode  *out,
            GError     **error)
{
	YamlMapping  *m;
	YamlSequence *ports = NULL;
	const gchar  *s;
	guint         k;

	if (item == NULL || yaml_node_get_node_type (item) != YAML_NODE_MAPPING)
		return parse_fail (error, "node", index);
	m = yaml_node_get_mapping (item);

	if (!scalar_member (m, "id", &s) || s == NULL || *s == '\0')
		return parse_fail (error, "node id", index);
	out->id = g_strdup (s);
	if (!scalar_member (m, "title", &s))
		return parse_fail (error, "node title", index);
	out->title = g_strdup (s);
	if (!scalar_member (m, "category", &s))
		return parse_fail (error, "node category", index);
	out->category = g_strdup (s);
	if (!number_member (m, "x", 0.0, &out->x) || !number_member (m, "y", 0.0, &out->y) ||
	    !number_member (m, "min-width", MIN_WIDTH, &out->min_width))
		return parse_fail (error, "node position", index);
	if (!scalar_member (m, "accent", &s))
		return parse_fail (error, "node accent", index);
	if (s != NULL)
	{
		if (!hex_to_color (s, &out->accent))
			return parse_fail (error, "node accent", index);
		out->has_accent = TRUE;
	}

	/* --- ports --- */
	out->ports = g_array_new (FALSE, TRUE, sizeof (StagedPort));
	g_array_set_clear_func (out->ports, staged_port_clear);
	if (yaml_mapping_has_member (m, "ports"))
	{
		YamlNode *pn = yaml_mapping_get_member (m, "ports");

		if (pn != NULL && yaml_node_get_node_type (pn) == YAML_NODE_SEQUENCE)
			ports = yaml_node_get_sequence (pn);
		else if (pn != NULL && yaml_node_get_node_type (pn) != YAML_NODE_NULL)
			return parse_fail (error, "port list", index);
	}
	for (k = 0; ports != NULL && k < yaml_sequence_get_length (ports); k++)
	{
		YamlNode    *pi = yaml_sequence_get_element (ports, k);
		YamlMapping *pm;
		StagedPort   p;
		guint        j;

		memset (&p, 0, sizeof p);
		p.multi = -1;
		if (pi == NULL || yaml_node_get_node_type (pi) != YAML_NODE_MAPPING)
			return parse_fail (error, "port", index);
		pm = yaml_node_get_mapping (pi);
		if (!scalar_member (pm, "name", &s) || s == NULL || *s == '\0')
			return parse_fail (error, "port name", index);
		p.name = g_strdup (s);
		g_array_append_val (out->ports, p);   /* owned by the array from here */

		if (!scalar_member (pm, "direction", &s) || s == NULL)
			return parse_fail (error, "port direction", index);
		if (g_strcmp0 (s, "input") == 0)
			g_array_index (out->ports, StagedPort, k).direction = LRG_GRAPH_PORT_INPUT;
		else if (g_strcmp0 (s, "output") == 0)
			g_array_index (out->ports, StagedPort, k).direction = LRG_GRAPH_PORT_OUTPUT;
		else
			return parse_fail (error, "port direction", index);
		if (!scalar_member (pm, "type", &s))
			return parse_fail (error, "port type", index);
		g_array_index (out->ports, StagedPort, k).type = g_strdup (s);
		if (!scalar_member (pm, "multi-link", &s))
			return parse_fail (error, "port multi-link", index);
		if (s != NULL)
		{
			if (g_strcmp0 (s, "true") == 0 || g_strcmp0 (s, "yes") == 0)
				g_array_index (out->ports, StagedPort, k).multi = 1;
			else if (g_strcmp0 (s, "false") == 0 || g_strcmp0 (s, "no") == 0)
				g_array_index (out->ports, StagedPort, k).multi = 0;
			else
				return parse_fail (error, "port multi-link", index);
		}
		if (!scalar_member (pm, "color", &s))
			return parse_fail (error, "port color", index);
		if (s != NULL)
		{
			if (!hex_to_color (s, &g_array_index (out->ports, StagedPort, k).color))
				return parse_fail (error, "port color", index);
			g_array_index (out->ports, StagedPort, k).has_color = TRUE;
		}

		/* Names are unique per direction */
		for (j = 0; j < k; j++)
		{
			StagedPort *a = &g_array_index (out->ports, StagedPort, j);
			StagedPort *b = &g_array_index (out->ports, StagedPort, k);

			if (a->direction == b->direction && g_strcmp0 (a->name, b->name) == 0)
				return parse_fail (error, "duplicate port name", index);
		}
	}
	return TRUE;
}

/* staged_has_port: the staged node @id has a port @name in @direction */
static gboolean
staged_has_port (GArray      *nodes,
                 GHashTable  *index_of,
                 const gchar *id,
                 const gchar *name,
                 gint         direction)
{
	gpointer    idx;
	StagedNode *n;
	guint       k;

	if (id == NULL || name == NULL || !g_hash_table_lookup_extended (index_of, id, NULL, &idx))
		return FALSE;
	n = &g_array_index (nodes, StagedNode, GPOINTER_TO_UINT (idx));
	for (k = 0; k < n->ports->len; k++)
	{
		StagedPort *p = &g_array_index (n->ports, StagedPort, k);

		if (p->direction == direction && g_strcmp0 (p->name, name) == 0)
			return TRUE;
	}
	return FALSE;
}

gboolean
lrg_node_graph_load_yaml (LrgNodeGraph  *self,
                          const gchar   *data,
                          gssize         length,
                          GError       **error)
{
	g_autoptr(YamlParser)  parser = NULL;
	g_autoptr(GArray)      nodes = NULL;
	g_autoptr(GArray)      links = NULL;
	g_autoptr(GHashTable)  index_of = NULL;
	g_autoptr(GError)      local = NULL;
	YamlNode              *root_node;
	YamlMapping           *root;
	YamlSequence          *seq;
	gdouble                version;
	gdouble                vx = 0.0;
	gdouble                vy = 0.0;
	gdouble                vz = 1.0;
	gboolean               has_view = FALSE;
	guint                  i;

	g_return_val_if_fail (LRG_IS_NODE_GRAPH (self), FALSE);
	g_return_val_if_fail (data != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* --- the document --- */
	parser = yaml_parser_new ();
	if (!yaml_parser_load_from_data (parser, data, length, &local))
	{
		g_set_error (error, LRG_NODE_GRAPH_ERROR, LRG_NODE_GRAPH_ERROR_PARSE,
		             "node graph YAML: %s", local->message);
		return FALSE;
	}
	root_node = yaml_parser_get_root (parser);
	if (root_node == NULL || yaml_node_get_node_type (root_node) != YAML_NODE_MAPPING)
		return parse_fail (error, "document (not a mapping)", 0);
	root = yaml_node_get_mapping (root_node);
	if (!number_member (root, "version", 1.0, &version) || version < 1.0 || version > 1.0)
		return parse_fail (error, "version", 0);

	/* --- view --- */
	if (yaml_mapping_has_member (root, "view"))
	{
		YamlNode    *vn = yaml_mapping_get_member (root, "view");
		YamlMapping *vm;

		if (vn == NULL || yaml_node_get_node_type (vn) != YAML_NODE_MAPPING)
			return parse_fail (error, "view", 0);
		vm = yaml_node_get_mapping (vn);
		if (!number_member (vm, "center-x", 0.0, &vx) || !number_member (vm, "center-y", 0.0, &vy) ||
		    !number_member (vm, "zoom", 1.0, &vz) || vz <= 0.0)
			return parse_fail (error, "view", 0);
		has_view = TRUE;
	}

	/* --- nodes --- */
	nodes = g_array_new (FALSE, TRUE, sizeof (StagedNode));
	g_array_set_clear_func (nodes, staged_node_clear);
	index_of = g_hash_table_new (g_str_hash, g_str_equal);
	seq = NULL;
	if (yaml_mapping_has_member (root, "nodes"))
	{
		YamlNode *sn = yaml_mapping_get_member (root, "nodes");

		if (sn != NULL && yaml_node_get_node_type (sn) == YAML_NODE_SEQUENCE)
			seq = yaml_node_get_sequence (sn);
		else if (sn != NULL && yaml_node_get_node_type (sn) != YAML_NODE_NULL)
			return parse_fail (error, "node list", 0);
	}
	for (i = 0; seq != NULL && i < yaml_sequence_get_length (seq); i++)
	{
		StagedNode n;

		memset (&n, 0, sizeof n);
		g_array_append_val (nodes, n);
		if (!stage_node (yaml_sequence_get_element (seq, i), i,
		                 &g_array_index (nodes, StagedNode, i), error))
			return FALSE;
		if (g_hash_table_contains (index_of, g_array_index (nodes, StagedNode, i).id))
			return parse_fail (error, "duplicate node id", i);
		g_hash_table_insert (index_of, g_array_index (nodes, StagedNode, i).id, GUINT_TO_POINTER (i));
	}

	/* --- links: every end must name an existing port --- */
	links = g_array_new (FALSE, TRUE, sizeof (StagedLink));
	g_array_set_clear_func (links, staged_link_clear);
	seq = NULL;
	if (yaml_mapping_has_member (root, "links"))
	{
		YamlNode *sn = yaml_mapping_get_member (root, "links");

		if (sn != NULL && yaml_node_get_node_type (sn) == YAML_NODE_SEQUENCE)
			seq = yaml_node_get_sequence (sn);
		else if (sn != NULL && yaml_node_get_node_type (sn) != YAML_NODE_NULL)
			return parse_fail (error, "link list", 0);
	}
	for (i = 0; seq != NULL && i < yaml_sequence_get_length (seq); i++)
	{
		YamlNode    *li = yaml_sequence_get_element (seq, i);
		YamlMapping *lm;
		StagedLink   l;
		const gchar *from;
		const gchar *out;
		const gchar *to;
		const gchar *in;

		if (li == NULL || yaml_node_get_node_type (li) != YAML_NODE_MAPPING)
			return parse_fail (error, "link", i);
		lm = yaml_node_get_mapping (li);
		if (!scalar_member (lm, "from", &from) || !scalar_member (lm, "output", &out) ||
		    !scalar_member (lm, "to", &to) || !scalar_member (lm, "input", &in) ||
		    !staged_has_port (nodes, index_of, from, out, LRG_GRAPH_PORT_OUTPUT) ||
		    !staged_has_port (nodes, index_of, to, in, LRG_GRAPH_PORT_INPUT) ||
		    g_strcmp0 (from, to) == 0)
			return parse_fail (error, "link", i);
		l.from = g_strdup (from);
		l.output = g_strdup (out);
		l.to = g_strdup (to);
		l.input = g_strdup (in);
		g_array_append_val (links, l);
	}

	/* --- valid: replace the content --- */
	lrg_node_graph_clear (self);
	for (i = 0; i < nodes->len; i++)
	{
		StagedNode              *sn = &g_array_index (nodes, StagedNode, i);
		g_autoptr(LrgGraphNode)  node = lrg_graph_node_new (sn->id, sn->title);
		guint                    k;

		node->x = (gfloat) sn->x;
		node->y = (gfloat) sn->y;
		node->min_width = (gfloat) sn->min_width;
		node->category = g_strdup (sn->category);
		node->has_accent = sn->has_accent;
		node->accent = sn->accent;
		for (k = 0; k < sn->ports->len; k++)
		{
			StagedPort   *sp = &g_array_index (sn->ports, StagedPort, k);
			LrgGraphPort *p = lrg_graph_node_add_port (node, (LrgGraphPortDirection) sp->direction,
			                                           sp->name, sp->type);

			if (sp->multi >= 0)
				p->multi_link = sp->multi != 0;
			p->has_color = sp->has_color;
			p->color = sp->color;
		}
		lrg_node_graph_append_node (self, node);
	}
	for (i = 0; i < links->len; i++)
	{
		StagedLink *sl = &g_array_index (links, StagedLink, i);

		if (lrg_node_graph_connect_by_name (self, sl->from, sl->output, sl->to, sl->input, error) == NULL)
			return FALSE;
	}
	if (has_view)
		lrg_node_graph_set_view (self, (gfloat) vx, (gfloat) vy, (gfloat) vz, FALSE);
	return TRUE;
}

/* ==========================================================================
 * GObject
 * ========================================================================== */

static void
lrg_node_graph_dispose (GObject *object)
{
	LrgNodeGraph *self = LRG_NODE_GRAPH (object);
	guint         i;

	/* Silent teardown: no signals while being destroyed */
	if (self->links != NULL)
		g_ptr_array_set_size (self->links, 0);
	for (i = 0; self->nodes != NULL && i < self->nodes->len; i++)
		((LrgGraphNode *) g_ptr_array_index (self->nodes, i))->graph = NULL;
	if (self->nodes != NULL)
		g_ptr_array_set_size (self->nodes, 0);
	if (self->by_id != NULL)
		g_hash_table_remove_all (self->by_id);
	self->selected_link = NULL;
	self->cursor = NULL;
	self->hover_node = NULL;
	self->hover_port = NULL;
	self->hover_link = NULL;
	self->press_node = NULL;
	self->last_click_node = NULL;
	self->link_snap = NULL;
	self->n_bodies = 0;
	g_clear_object (&self->link_origin);

	G_OBJECT_CLASS (lrg_node_graph_parent_class)->dispose (object);
}

static void
lrg_node_graph_finalize (GObject *object)
{
	LrgNodeGraph *self = LRG_NODE_GRAPH (object);

	g_clear_pointer (&self->links, g_ptr_array_unref);
	g_clear_pointer (&self->nodes, g_ptr_array_unref);
	g_clear_pointer (&self->by_id, g_hash_table_unref);
	g_clear_pointer (&self->compatible, g_hash_table_unref);
	g_clear_pointer (&self->box_base, g_hash_table_unref);
	G_OBJECT_CLASS (lrg_node_graph_parent_class)->finalize (object);
}

static void
lrg_node_graph_get_property (GObject    *object,
                             guint       prop_id,
                             GValue     *value,
                             GParamSpec *pspec)
{
	LrgNodeGraph *self = LRG_NODE_GRAPH (object);

	switch (prop_id)
	{
	case PROP_ZOOM:
		g_value_set_float (value, self->zoom_s.target);
		break;
	case PROP_CENTER_X:
		g_value_set_float (value, self->cx_s.target);
		break;
	case PROP_CENTER_Y:
		g_value_set_float (value, self->cy_s.target);
		break;
	case PROP_MIN_ZOOM:
		g_value_set_float (value, self->min_zoom);
		break;
	case PROP_MAX_ZOOM:
		g_value_set_float (value, self->max_zoom);
		break;
	case PROP_GRID_SIZE:
		g_value_set_float (value, self->grid_size);
		break;
	case PROP_GRID_STYLE:
		g_value_set_enum (value, self->grid_style);
		break;
	case PROP_SNAP_TO_GRID:
		g_value_set_boolean (value, self->snap);
		break;
	case PROP_SHOW_MINIMAP:
		g_value_set_boolean (value, self->show_minimap);
		break;
	case PROP_ANIMATE_LINKS:
		g_value_set_boolean (value, self->animate_links);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_node_graph_set_property (GObject      *object,
                             guint         prop_id,
                             const GValue *value,
                             GParamSpec   *pspec)
{
	LrgNodeGraph *self = LRG_NODE_GRAPH (object);

	switch (prop_id)
	{
	case PROP_ZOOM:
		lrg_node_graph_set_zoom (self, g_value_get_float (value));
		break;
	case PROP_CENTER_X:
		lrg_node_graph_set_view (self, g_value_get_float (value), self->cy_s.target,
		                         self->zoom_s.target, FALSE);
		break;
	case PROP_CENTER_Y:
		lrg_node_graph_set_view (self, self->cx_s.target, g_value_get_float (value),
		                         self->zoom_s.target, FALSE);
		break;
	case PROP_MIN_ZOOM:
		lrg_node_graph_set_zoom_range (self, g_value_get_float (value), MAX (self->max_zoom,
		                               g_value_get_float (value)));
		break;
	case PROP_MAX_ZOOM:
		lrg_node_graph_set_zoom_range (self, MIN (self->min_zoom, g_value_get_float (value)),
		                               g_value_get_float (value));
		break;
	case PROP_GRID_SIZE:
		lrg_node_graph_set_grid_size (self, g_value_get_float (value));
		break;
	case PROP_GRID_STYLE:
		lrg_node_graph_set_grid_style (self, (LrgNodeGraphGrid) g_value_get_enum (value));
		break;
	case PROP_SNAP_TO_GRID:
		lrg_node_graph_set_snap_to_grid (self, g_value_get_boolean (value));
		break;
	case PROP_SHOW_MINIMAP:
		lrg_node_graph_set_show_minimap (self, g_value_get_boolean (value));
		break;
	case PROP_ANIMATE_LINKS:
		lrg_node_graph_set_animate_links (self, g_value_get_boolean (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_node_graph_class_init (LrgNodeGraphClass *klass)
{
	GObjectClass      *object_class = G_OBJECT_CLASS (klass);
	LrgWidgetClass    *widget_class = LRG_WIDGET_CLASS (klass);
	LrgContainerClass *container_class = LRG_CONTAINER_CLASS (klass);

	object_class->dispose = lrg_node_graph_dispose;
	object_class->finalize = lrg_node_graph_finalize;
	object_class->get_property = lrg_node_graph_get_property;
	object_class->set_property = lrg_node_graph_set_property;

	widget_class->measure = lrg_node_graph_measure;
	widget_class->paint = lrg_node_graph_paint;
	widget_class->step = lrg_node_graph_step;
	widget_class->style_defaults = lrg_node_graph_style_defaults;
	widget_class->handle_event = lrg_node_graph_handle_event;

	container_class->layout_children = lrg_node_graph_layout_children;

	/* Graph coordinates are absolute: never mirror them in RTL layouts */
	lrg_widget_class_set_handles_direction (widget_class, TRUE);

	/**
	 * LrgNodeGraph:zoom:
	 *
	 * Screen units per graph unit (the target while the view glides).
	 * Setting it zooms about the widget centre.
	 */
	properties[PROP_ZOOM] =
		g_param_spec_float ("zoom", "Zoom", "Screen units per graph unit", 0.01f, 100.0f, 1.0f,
		                    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgNodeGraph:center-x:
	 *
	 * Graph x shown at the middle of the widget.
	 */
	properties[PROP_CENTER_X] =
		g_param_spec_float ("center-x", "Center X", "Graph x at the widget centre",
		                    -G_MAXFLOAT, G_MAXFLOAT, 0.0f,
		                    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgNodeGraph:center-y:
	 *
	 * Graph y shown at the middle of the widget.
	 */
	properties[PROP_CENTER_Y] =
		g_param_spec_float ("center-y", "Center Y", "Graph y at the widget centre",
		                    -G_MAXFLOAT, G_MAXFLOAT, 0.0f,
		                    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgNodeGraph:min-zoom:
	 *
	 * Smallest zoom (default 0.2).
	 */
	properties[PROP_MIN_ZOOM] =
		g_param_spec_float ("min-zoom", "Min zoom", "Smallest zoom", 0.01f, 100.0f, 0.2f,
		                    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgNodeGraph:max-zoom:
	 *
	 * Largest zoom (default 2.5).
	 */
	properties[PROP_MAX_ZOOM] =
		g_param_spec_float ("max-zoom", "Max zoom", "Largest zoom", 0.01f, 100.0f, 2.5f,
		                    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgNodeGraph:grid-size:
	 *
	 * Grid cell in graph units; also the snap step.
	 */
	properties[PROP_GRID_SIZE] =
		g_param_spec_float ("grid-size", "Grid size", "Grid cell in graph units", 1.0f, G_MAXFLOAT,
		                    24.0f,
		                    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgNodeGraph:grid-style:
	 *
	 * How the background grid is drawn.
	 */
	properties[PROP_GRID_STYLE] =
		g_param_spec_enum ("grid-style", "Grid style", "Background grid",
		                   LRG_TYPE_NODE_GRAPH_GRID, LRG_NODE_GRAPH_GRID_LINES,
		                   G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgNodeGraph:snap-to-grid:
	 *
	 * Dragged nodes land on grid positions.
	 */
	properties[PROP_SNAP_TO_GRID] =
		g_param_spec_boolean ("snap-to-grid", "Snap to grid", "Snap dragged nodes", FALSE,
		                      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgNodeGraph:show-minimap:
	 *
	 * Show an overview in the bottom-right corner.
	 */
	properties[PROP_SHOW_MINIMAP] =
		g_param_spec_boolean ("show-minimap", "Show minimap", "Show the overview", FALSE,
		                      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgNodeGraph:animate-links:
	 *
	 * Pulses flow along the wires from outputs to inputs.
	 */
	properties[PROP_ANIMATE_LINKS] =
		g_param_spec_boolean ("animate-links", "Animate links", "Flowing pulses on wires", FALSE,
		                      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	g_object_class_install_properties (object_class, N_PROPS, properties);

	/**
	 * LrgNodeGraph::node-added:
	 * @self: the graph
	 * @node: the new node
	 *
	 * Emitted after a node was added (also for duplicates and loads).
	 */
	signals[SIGNAL_NODE_ADDED] =
		g_signal_new ("node-added", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
		              0, NULL, NULL, NULL, G_TYPE_NONE, 1, LRG_TYPE_GRAPH_NODE);

	/**
	 * LrgNodeGraph::node-removed:
	 * @self: the graph
	 * @node: the removed node (its links are gone already)
	 */
	signals[SIGNAL_NODE_REMOVED] =
		g_signal_new ("node-removed", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
		              0, NULL, NULL, NULL, G_TYPE_NONE, 1, LRG_TYPE_GRAPH_NODE);

	/**
	 * LrgNodeGraph::node-moved:
	 * @self: the graph
	 * @node: the node
	 *
	 * Emitted when lrg_graph_node_set_position() moved a node, and once
	 * per moved node when a drag ends (not on every pointer motion).
	 */
	signals[SIGNAL_NODE_MOVED] =
		g_signal_new ("node-moved", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
		              0, NULL, NULL, NULL, G_TYPE_NONE, 1, LRG_TYPE_GRAPH_NODE);

	/**
	 * LrgNodeGraph::node-activated:
	 * @self: the graph
	 * @node: the node
	 *
	 * Double click on a card, or Enter with the keyboard cursor on it.
	 */
	signals[SIGNAL_NODE_ACTIVATED] =
		g_signal_new ("node-activated", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
		              0, NULL, NULL, NULL, G_TYPE_NONE, 1, LRG_TYPE_GRAPH_NODE);

	/**
	 * LrgNodeGraph::node-duplicated:
	 * @self: the graph
	 * @original: the copied node
	 * @copy: the new node (already added)
	 *
	 * Emitted by lrg_node_graph_duplicate_selection() (Ctrl+D) for each
	 * copy, so the application can give it a body or its own data.
	 */
	signals[SIGNAL_NODE_DUPLICATED] =
		g_signal_new ("node-duplicated", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
		              0, NULL, NULL, NULL, G_TYPE_NONE, 2, LRG_TYPE_GRAPH_NODE, LRG_TYPE_GRAPH_NODE);

	/**
	 * LrgNodeGraph::link-added:
	 * @self: the graph
	 * @link: the new link
	 */
	signals[SIGNAL_LINK_ADDED] =
		g_signal_new ("link-added", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
		              0, NULL, NULL, NULL, G_TYPE_NONE, 1, LRG_TYPE_GRAPH_LINK);

	/**
	 * LrgNodeGraph::link-removed:
	 * @self: the graph
	 * @link: the removed link (still valid during the emission)
	 */
	signals[SIGNAL_LINK_REMOVED] =
		g_signal_new ("link-removed", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
		              0, NULL, NULL, NULL, G_TYPE_NONE, 1, LRG_TYPE_GRAPH_LINK);

	/**
	 * LrgNodeGraph::link-dropped:
	 * @self: the graph
	 * @port: the port the wire was dragged from
	 * @x: drop point x in graph units
	 * @y: drop point y in graph units
	 *
	 * A wire was dropped on empty canvas: the place to pop up a
	 * "create node" menu and connect the new node to @port.
	 */
	signals[SIGNAL_LINK_DROPPED] =
		g_signal_new ("link-dropped", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
		              0, NULL, NULL, NULL, G_TYPE_NONE, 3, LRG_TYPE_GRAPH_PORT, G_TYPE_FLOAT,
		              G_TYPE_FLOAT);

	/**
	 * LrgNodeGraph::can-connect:
	 * @self: the graph
	 * @output: an output port
	 * @input: an input port of another node
	 *
	 * Decides whether a link may join the two ports. The first handler
	 * connected decides; without one the default allows equal types
	 * and "any" on either side. It is asked by lrg_node_graph_connect()
	 * and, while a wire is dragged, once per candidate port to
	 * highlight the compatible ones.
	 *
	 * Returns: %TRUE to allow the link
	 */
	signals[SIGNAL_CAN_CONNECT] =
		g_signal_new_class_handler ("can-connect", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
		                            G_CALLBACK (default_can_connect),
		                            g_signal_accumulator_first_wins, NULL, NULL,
		                            G_TYPE_BOOLEAN, 2, LRG_TYPE_GRAPH_PORT, LRG_TYPE_GRAPH_PORT);

	/**
	 * LrgNodeGraph::selection-changed:
	 * @self: the graph
	 *
	 * The set of selected nodes or the selected link changed.
	 */
	signals[SIGNAL_SELECTION_CHANGED] =
		g_signal_new ("selection-changed", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
		              0, NULL, NULL, NULL, G_TYPE_NONE, 0);
}

static void
lrg_node_graph_init (LrgNodeGraph *self)
{
	self->nodes = g_ptr_array_new_with_free_func (g_object_unref);
	self->links = g_ptr_array_new_with_free_func (g_object_unref);
	self->by_id = g_hash_table_new (g_str_hash, g_str_equal);
	self->min_zoom = 0.2f;
	self->max_zoom = 2.5f;
	self->grid_size = 24.0f;
	self->grid_style = LRG_NODE_GRAPH_GRID_LINES;
	lrg_spring_init (&self->zoom_s, 1.0f, LRG_MOTION_SMOOTH);
	lrg_spring_init (&self->cx_s, 0.0f, LRG_MOTION_SMOOTH);
	lrg_spring_init (&self->cy_s, 0.0f, LRG_MOTION_SMOOTH);
	lrg_widget_set_focusable (LRG_WIDGET (self), TRUE);
}
