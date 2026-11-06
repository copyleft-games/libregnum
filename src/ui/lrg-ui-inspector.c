/* lrg-ui-inspector.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgUIInspector: Inspects a canvas: widget tree, geometry, state, looks and timings, with pick mode.
 *
 * Design notes:
 *
 * - The inspector never sits in the widget hot path. It holds the canvas
 *   through a weak reference and walks the tree only when asked.
 * - The highlight is drawn by one private widget (LrgUIInspectorOverlay)
 *   in the canvas overlay layer. It is pointer-transparent and has no
 *   children, so canvas hit-testing never returns it, and the inspector
 *   leaves it out of snapshots, paths and layer indices.
 * - Pick mode works on input frames (lrg_ui_inspector_filter_input())
 *   before they reach lrg_canvas_process_input(), which is the public
 *   seam the canvas offers: the click is consumed there, so the canvas
 *   never focuses, captures, dismisses popups or dispatches anything.
 * - Phase timings come from a tiny private hook in lrg-canvas.c that
 *   reads g_get_monotonic_time() only while an inspector is attached.
 */

#include "config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_UI

#include <math.h>
#include <string.h>
#include <raylib.h>
#include <json-glib/json-glib.h>

#include "lrg-ui-inspector.h"
#include "lrg-canvas.h"
#include "lrg-canvas-private.h"
#include "lrg-container.h"
#include "lrg-widget.h"
#include "lrg-theme.h"
#include "lrg-style.h"
#include "../paint/lrg-paint-renderer.h"
#include "../lrg-enums.h"
#include "../lrg-log.h"

/* Highlight palette (fixed, so it reads the same over every theme) */
#define HL_LABEL_SIZE    (13.0f)
#define HL_LABEL_PAD_X   (6.0f)
#define HL_LABEL_PAD_Y   (3.0f)
#define HL_LABEL_GAP     (4.0f)
#define HL_BORDER_WIDTH  (1.5f)

static const GrlColor hl_selected_fill   = { 137, 180, 250, 56 };
static const GrlColor hl_selected_border = { 137, 180, 250, 235 };
static const GrlColor hl_hovered_fill    = { 148, 226, 213, 40 };
static const GrlColor hl_hovered_border  = { 148, 226, 213, 220 };
static const GrlColor hl_margin_fill     = { 250, 179, 135, 72 };
static const GrlColor hl_label_bg        = { 24, 24, 37, 235 };
static const GrlColor hl_label_fg        = { 205, 214, 244, 255 };

/* ==========================================================================
 * Errors and timings
 * ========================================================================== */

G_DEFINE_QUARK (lrg-ui-inspector-error-quark, lrg_ui_inspector_error)

GType
lrg_ui_inspector_error_get_type (void)
{
	static gsize type_id = 0;

	if (g_once_init_enter (&type_id))
	{
		static const GEnumValue values[] = {
			{ LRG_UI_INSPECTOR_ERROR_NO_CANVAS, "LRG_UI_INSPECTOR_ERROR_NO_CANVAS", "no-canvas" },
			{ LRG_UI_INSPECTOR_ERROR_NOT_FOUND, "LRG_UI_INSPECTOR_ERROR_NOT_FOUND", "not-found" },
			{ LRG_UI_INSPECTOR_ERROR_INVALID_PROPERTY, "LRG_UI_INSPECTOR_ERROR_INVALID_PROPERTY",
			  "invalid-property" },
			{ LRG_UI_INSPECTOR_ERROR_INVALID_VALUE, "LRG_UI_INSPECTOR_ERROR_INVALID_VALUE",
			  "invalid-value" },
			{ 0, NULL, NULL }
		};
		GType id = g_enum_register_static (g_intern_static_string ("LrgUIInspectorError"), values);

		g_once_init_leave (&type_id, id);
	}
	return (GType) type_id;
}

LrgUIInspectorTimings *
lrg_ui_inspector_timings_copy (const LrgUIInspectorTimings *self)
{
	g_return_val_if_fail (self != NULL, NULL);

	return (LrgUIInspectorTimings *) g_memdup2 (self, sizeof (LrgUIInspectorTimings));
}

void
lrg_ui_inspector_timings_free (LrgUIInspectorTimings *self)
{
	g_free (self);
}

G_DEFINE_BOXED_TYPE (LrgUIInspectorTimings, lrg_ui_inspector_timings,
                     lrg_ui_inspector_timings_copy, lrg_ui_inspector_timings_free)

/* ==========================================================================
 * The inspector
 * ========================================================================== */

struct _LrgUIInspector
{
	GObject    parent_instance;

	LrgCanvas *canvas;            /* weak (g_object_weak_ref) */
	LrgWidget *overlay;           /* weak: owned by the canvas overlay layer */
	LrgWidget *selected;          /* weak */
	LrgWidget *hovered;           /* weak */

	gboolean   pick_mode;
	gboolean   highlight_visible;

	/* Pick-mode input bookkeeping */
	guint      app_buttons;       /* buttons the app saw go down and not up */
	gboolean   touch_down;        /* a finger was down last frame */

	/* Statistics */
	guint64    frames;            /* records since attach */
	guint      overlay_instances; /* instances the highlight added last frame */
};

G_DEFINE_FINAL_TYPE (LrgUIInspector, lrg_ui_inspector, G_TYPE_OBJECT)

enum
{
	PROP_0,
	PROP_CANVAS,
	PROP_PICK_MODE,
	PROP_SELECTED,
	PROP_HOVERED,
	PROP_HIGHLIGHT_VISIBLE,
	N_PROPS
};

enum
{
	SIGNAL_PICKED,
	N_SIGNALS
};

static GParamSpec     *properties[N_PROPS];
static guint           signals[N_SIGNALS];
static LrgUIInspector *default_inspector = NULL;

/* ==========================================================================
 * Overlay widget (private): draws the highlight from the overlay layer
 * ========================================================================== */

#define LRG_TYPE_UI_INSPECTOR_OVERLAY (lrg_ui_inspector_overlay_get_type ())
G_DECLARE_FINAL_TYPE (LrgUIInspectorOverlay, lrg_ui_inspector_overlay,
                      LRG, UI_INSPECTOR_OVERLAY, LrgWidget)

struct _LrgUIInspectorOverlay
{
	LrgWidget       parent_instance;
	LrgUIInspector *inspector;    /* weak */
};

G_DEFINE_FINAL_TYPE (LrgUIInspectorOverlay, lrg_ui_inspector_overlay, LRG_TYPE_WIDGET)

/*
 * overlay_paint:
 *
 * Runs at the end of lrg_canvas_record() (the overlay layer is painted
 * last), so it sees the final layout of the frame. It counts the frame
 * and how many instances the highlight added, so the timings can report
 * the application's own instance count.
 */
static void
overlay_paint (LrgWidget    *widget,
               LrgPaintList *list)
{
	LrgUIInspectorOverlay *self = LRG_UI_INSPECTOR_OVERLAY (widget);
	guint                  before;

	if (self->inspector == NULL)
		return;

	before = lrg_paint_list_get_n_instances (list);
	lrg_ui_inspector_paint_highlight (self->inspector, list);
	self->inspector->overlay_instances = lrg_paint_list_get_n_instances (list) - before;
	self->inspector->frames++;
}

/* overlay_measure: takes no room; it draws at other widgets' positions */
static void
overlay_measure (LrgWidget *widget,
                 gfloat    *preferred_width,
                 gfloat    *preferred_height)
{
	*preferred_width = 0.0f;
	*preferred_height = 0.0f;
}

static void
lrg_ui_inspector_overlay_dispose (GObject *object)
{
	LrgUIInspectorOverlay *self = LRG_UI_INSPECTOR_OVERLAY (object);

	g_clear_weak_pointer (&self->inspector);
	G_OBJECT_CLASS (lrg_ui_inspector_overlay_parent_class)->dispose (object);
}

static void
lrg_ui_inspector_overlay_class_init (LrgUIInspectorOverlayClass *klass)
{
	GObjectClass   *object_class = G_OBJECT_CLASS (klass);
	LrgWidgetClass *widget_class = LRG_WIDGET_CLASS (klass);

	object_class->dispose = lrg_ui_inspector_overlay_dispose;
	widget_class->paint = overlay_paint;
	widget_class->measure = overlay_measure;
}

static void
lrg_ui_inspector_overlay_init (LrgUIInspectorOverlay *self)
{
	/* Invisible to hit-testing: no children and pointer-transparent */
	lrg_widget_set_pointer_transparent (LRG_WIDGET (self), TRUE);
}

/* ==========================================================================
 * Layers and paths
 * ========================================================================== */

/*
 * collect_layers:
 *
 * The canvas's open popups and overlay widgets, borrowed, without the
 * inspector's own overlay (so indices do not depend on it).
 */
static void
collect_layers (LrgUIInspector *self,
                GPtrArray      *popups,
                GPtrArray      *overlays)
{
	_lrg_canvas_get_layers (self->canvas, popups, overlays);
	if (overlays != NULL && self->overlay != NULL)
		g_ptr_array_remove (overlays, self->overlay);
}

/*
 * child_segments:
 *
 * One pass over a container's children building every child's path
 * segment: "Type[n]" for unnamed widgets (n counts earlier siblings of
 * the same type) and "Type#name" / "Type#name[k]" for named ones (k
 * counts earlier siblings with the same type and name).
 *
 * Returns: (transfer full): array of segments, parallel to @children
 */
static GPtrArray *
child_segments (GList *children)
{
	g_autoptr(GHashTable) counts = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
	GPtrArray            *segments = g_ptr_array_new_with_free_func (g_free);
	GList                *l;

	for (l = children; l != NULL; l = l->next)
	{
		LrgWidget        *child = LRG_WIDGET (l->data);
		const gchar      *type = G_OBJECT_TYPE_NAME (child);
		const gchar      *name = lrg_widget_get_name (child);
		g_autofree gchar *named_key = NULL;
		guint             type_index;
		guint             name_index;

		/* Every widget counts towards its type's index */
		type_index = GPOINTER_TO_UINT (g_hash_table_lookup (counts, type));
		g_hash_table_replace (counts, g_strdup (type), GUINT_TO_POINTER (type_index + 1));

		if (name == NULL || *name == '\0')
		{
			g_ptr_array_add (segments, g_strdup_printf ("%s[%u]", type, type_index));
			continue;
		}

		/* Named widgets count separately, per type and name */
		named_key = g_strdup_printf ("%s#%s", type, name);
		name_index = GPOINTER_TO_UINT (g_hash_table_lookup (counts, named_key));
		g_hash_table_replace (counts, g_strdup (named_key), GUINT_TO_POINTER (name_index + 1));
		if (name_index == 0)
			g_ptr_array_add (segments, g_strdup (named_key));
		else
			g_ptr_array_add (segments, g_strdup_printf ("%s[%u]", named_key, name_index));
	}
	return segments;
}

/* children_of: a widget's child list (NULL for leaves) */
static GList *
children_of (LrgWidget *widget)
{
	return LRG_IS_CONTAINER (widget) ? lrg_container_get_children (LRG_CONTAINER (widget)) : NULL;
}

/* join_path: "parent/segment", or just the segment below the canvas */
static gchar *
join_path (const gchar *parent,
           const gchar *segment)
{
	if (parent == NULL || *parent == '\0')
		return g_strdup (segment);
	return g_strconcat (parent, "/", segment, NULL);
}

/*
 * layer_segment:
 *
 * The first segment of a widget whose parent is the canvas: its child
 * segment, or ":popup[i]" / ":overlay[i]" for layer roots.
 */
static gchar *
layer_segment (LrgUIInspector *self,
               LrgWidget      *top)
{
	g_autoptr(GPtrArray) popups = g_ptr_array_new ();
	g_autoptr(GPtrArray) overlays = g_ptr_array_new ();
	GList               *children = children_of (LRG_WIDGET (self->canvas));
	gint                 index;
	guint                i;

	index = g_list_index (children, top);
	if (index >= 0)
	{
		g_autoptr(GPtrArray) segments = child_segments (children);

		return g_strdup (g_ptr_array_index (segments, index));
	}

	collect_layers (self, popups, overlays);
	if (g_ptr_array_find (popups, top, &i))
		return g_strdup_printf (":popup[%u]", i);
	if (g_ptr_array_find (overlays, top, &i))
		return g_strdup_printf (":overlay[%u]", i);
	return NULL;
}

gchar *
lrg_ui_inspector_get_path (LrgUIInspector *self,
                           LrgWidget      *widget)
{
	g_autoptr(GPtrArray) chain = NULL;
	g_autofree gchar    *path = NULL;
	LrgWidget           *w;
	gint                 i;

	g_return_val_if_fail (LRG_IS_UI_INSPECTOR (self), NULL);
	g_return_val_if_fail (LRG_IS_WIDGET (widget), NULL);

	if (self->canvas == NULL)
		return NULL;
	if (widget == LRG_WIDGET (self->canvas))
		return g_strdup ("");

	/* The chain from @widget up to (not including) the canvas */
	chain = g_ptr_array_new ();
	for (w = widget; w != NULL && w != LRG_WIDGET (self->canvas);
	     w = (LrgWidget *) lrg_widget_get_parent (w))
		g_ptr_array_add (chain, w);
	if (w == NULL || chain->len == 0)
		return NULL;

	/* Top-down: the layer root, then each child's segment */
	path = layer_segment (self, g_ptr_array_index (chain, chain->len - 1));
	if (path == NULL)
		return NULL;

	for (i = (gint) chain->len - 2; i >= 0; i--)
	{
		LrgWidget           *child = g_ptr_array_index (chain, i);
		LrgWidget           *parent = g_ptr_array_index (chain, i + 1);
		GList               *children = children_of (parent);
		gint                 index = g_list_index (children, child);
		g_autoptr(GPtrArray) segments = NULL;
		gchar               *joined;

		/* Internal children kept out of the child list are not addressable */
		if (index < 0)
			return NULL;
		segments = child_segments (children);
		joined = join_path (path, g_ptr_array_index (segments, index));
		g_free (path);
		path = joined;
	}
	return g_steal_pointer (&path);
}

/*
 * parse_segment:
 *
 * Splits "Type#name[3]" into its parts. Type and name may be empty
 * (match anything), the index defaults to 0.
 */
static gboolean
parse_segment (const gchar  *segment,
               gchar       **type,
               gchar       **name,
               guint        *index)
{
	const gchar *hash = strchr (segment, '#');
	const gchar *bracket = strrchr (segment, '[');
	const gchar *type_end;

	*index = 0;
	*name = NULL;

	/* A trailing "[n]" index */
	if (bracket != NULL)
	{
		gchar   *end = NULL;
		guint64  n = g_ascii_strtoull (bracket + 1, &end, 10);

		if (end == bracket + 1 || end == NULL || *end != ']' || end[1] != '\0' || n > G_MAXUINT)
			return FALSE;
		*index = (guint) n;
	}
	else
		bracket = segment + strlen (segment);

	if (hash != NULL && hash > bracket)
		hash = NULL;
	type_end = hash != NULL ? hash : bracket;
	*type = g_strndup (segment, (gsize) (type_end - segment));
	if (hash != NULL)
		*name = g_strndup (hash + 1, (gsize) (bracket - hash - 1));
	return TRUE;
}

/* find_child: the @index-th child matching type (if any) and name (if any) */
static LrgWidget *
find_child (LrgWidget   *parent,
            const gchar *type,
            const gchar *name,
            guint        index)
{
	GList *l;
	guint  seen = 0;

	for (l = children_of (parent); l != NULL; l = l->next)
	{
		LrgWidget *child = LRG_WIDGET (l->data);

		if (*type != '\0' && g_strcmp0 (G_OBJECT_TYPE_NAME (child), type) != 0)
			continue;
		if (name != NULL && g_strcmp0 (lrg_widget_get_name (child), name) != 0)
			continue;
		if (seen++ == index)
			return child;
	}
	return NULL;
}

LrgWidget *
lrg_ui_inspector_lookup (LrgUIInspector *self,
                         const gchar    *path)
{
	g_auto(GStrv) parts = NULL;
	LrgWidget    *current;
	guint         i;

	g_return_val_if_fail (LRG_IS_UI_INSPECTOR (self), NULL);

	if (self->canvas == NULL)
		return NULL;
	current = LRG_WIDGET (self->canvas);
	if (path == NULL)
		return current;

	parts = g_strsplit (path, "/", -1);
	for (i = 0; parts[i] != NULL; i++)
	{
		g_autofree gchar *type = NULL;
		g_autofree gchar *name = NULL;
		guint             index;

		if (*parts[i] == '\0')
			continue;   /* leading "/", "//" */

		/* Layer roots: ":popup[i]" and ":overlay[i]", first segment only */
		if (*parts[i] == ':')
		{
			g_autoptr(GPtrArray) popups = g_ptr_array_new ();
			g_autoptr(GPtrArray) overlays = g_ptr_array_new ();
			GPtrArray           *layer;

			if (current != LRG_WIDGET (self->canvas) ||
			    !parse_segment (parts[i] + 1, &type, &name, &index) || name != NULL)
				return NULL;
			collect_layers (self, popups, overlays);
			if (g_strcmp0 (type, "popup") == 0)
				layer = popups;
			else if (g_strcmp0 (type, "overlay") == 0)
				layer = overlays;
			else
				return NULL;
			if (index >= layer->len)
				return NULL;
			current = g_ptr_array_index (layer, index);
			continue;
		}

		if (!parse_segment (parts[i], &type, &name, &index))
			return NULL;
		current = find_child (current, type, name, index);
		if (current == NULL)
			return NULL;
	}
	return current;
}

/* lookup_or_error: lookup with a GError for the property and MCP paths */
static LrgWidget *
lookup_or_error (LrgUIInspector  *self,
                 const gchar     *path,
                 GError         **error)
{
	LrgWidget *widget;

	if (self->canvas == NULL)
	{
		g_set_error_literal (error, LRG_UI_INSPECTOR_ERROR, LRG_UI_INSPECTOR_ERROR_NO_CANVAS,
		                     "The UI inspector is not attached to a canvas");
		return NULL;
	}
	widget = lrg_ui_inspector_lookup (self, path);
	if (widget == NULL)
		g_set_error (error, LRG_UI_INSPECTOR_ERROR, LRG_UI_INSPECTOR_ERROR_NOT_FOUND,
		             "No widget at path \"%s\"", path != NULL ? path : "");
	return widget;
}

/* ==========================================================================
 * JSON helpers
 * ========================================================================== */

static gchar *
color_string (const GrlColor *c)
{
	return g_strdup_printf ("#%02x%02x%02x%02x", c->r, c->g, c->b, c->a);
}

static void
set_color_member (JsonObject     *o,
                  const gchar    *name,
                  const GrlColor *c)
{
	g_autofree gchar *text = color_string (c);

	json_object_set_string_member (o, name, text);
}

static const gchar *
enum_nick (GType type,
           gint  value)
{
	GEnumClass *klass = g_type_class_ref (type);
	GEnumValue *v = g_enum_get_value (klass, value);
	const gchar *nick = v != NULL ? v->value_nick : NULL;

	g_type_class_unref (klass);   /* static enum types stay registered */
	return nick;
}

/* flags_array: the nicks of every bit set in @value */
static JsonArray *
flags_array (GType type,
             guint value)
{
	GFlagsClass *klass = g_type_class_ref (type);
	JsonArray   *array = json_array_new ();
	guint        i;

	for (i = 0; i < klass->n_values; i++)
	{
		const GFlagsValue *v = &klass->values[i];

		if (v->value != 0 && (value & v->value) == v->value)
			json_array_add_string_element (array, v->value_nick);
	}
	g_type_class_unref (klass);
	return array;
}

/* look_json: the resolved BOX look, the widget's main surface */
static JsonObject *
look_json (LrgWidget *widget)
{
	LrgPartLook  look;
	JsonObject  *o = json_object_new ();
	JsonArray   *radii = json_array_new ();

	lrg_widget_resolve_look (widget, LRG_UI_PART_BOX, &look);

	json_object_set_string_member (o, "part", "box");
	json_object_set_array_member (o, "set", flags_array (LRG_TYPE_PART_LOOK_FIELDS, look.set));
	set_color_member (o, "background", &look.background);
	set_color_member (o, "foreground", &look.foreground);
	set_color_member (o, "border", &look.border);
	json_object_set_double_member (o, "border_width", look.border_width);
	json_array_add_double_element (radii, look.radii.top_left);
	json_array_add_double_element (radii, look.radii.top_right);
	json_array_add_double_element (radii, look.radii.bottom_right);
	json_array_add_double_element (radii, look.radii.bottom_left);
	json_object_set_array_member (o, "radii", radii);
	set_color_member (o, "shadow", &look.shadow);
	json_object_set_double_member (o, "shadow_blur", look.shadow_blur);
	json_object_set_double_member (o, "shadow_offset_y", look.shadow_offset_y);
	json_object_set_double_member (o, "opacity", look.opacity);
	json_object_set_double_member (o, "size", look.size);
	set_color_member (o, "glow", &look.glow);
	json_object_set_double_member (o, "glow_size", look.glow_size);
	return o;
}

/*
 * node_json:
 *
 * One widget as a JSON object. With @recurse the children are nested
 * nodes; otherwise "children" lists their paths.
 */
static JsonObject *
node_json (LrgWidget   *widget,
           const gchar *path,
           gboolean     recurse)
{
	JsonObject          *o = json_object_new ();
	JsonObject          *rect = json_object_new ();
	JsonObject          *margin = json_object_new ();
	JsonArray           *classes = json_array_new ();
	JsonArray           *children = json_array_new ();
	const GQuark        *quarks;
	guint                n_classes = 0;
	guint                i;
	LrgInsets            m;
	GList               *list = children_of (widget);
	GList               *l;
	g_autoptr(GPtrArray) segments = NULL;

	/* Identity */
	if (path != NULL)
		json_object_set_string_member (o, "path", path);
	else
		json_object_set_null_member (o, "path");
	json_object_set_string_member (o, "type", G_OBJECT_TYPE_NAME (widget));
	if (lrg_widget_get_name (widget) != NULL)
		json_object_set_string_member (o, "name", lrg_widget_get_name (widget));
	else
		json_object_set_null_member (o, "name");
	quarks = lrg_widget_get_classes (widget, &n_classes);
	for (i = 0; i < n_classes; i++)
		json_array_add_string_element (classes, g_quark_to_string (quarks[i]));
	json_object_set_array_member (o, "classes", classes);
	json_object_set_array_member (o, "state",
	                              flags_array (LRG_TYPE_WIDGET_STATE,
	                                           (guint) lrg_widget_get_state_flags (widget)));

	/* Geometry */
	json_object_set_double_member (rect, "x", lrg_widget_get_world_x (widget));
	json_object_set_double_member (rect, "y", lrg_widget_get_world_y (widget));
	json_object_set_double_member (rect, "width", lrg_widget_get_width (widget));
	json_object_set_double_member (rect, "height", lrg_widget_get_height (widget));
	json_object_set_object_member (o, "rect", rect);
	lrg_widget_get_margin (widget, &m);
	json_object_set_double_member (margin, "left", m.left);
	json_object_set_double_member (margin, "top", m.top);
	json_object_set_double_member (margin, "right", m.right);
	json_object_set_double_member (margin, "bottom", m.bottom);
	json_object_set_object_member (o, "margin", margin);
	json_object_set_string_member (o, "halign", enum_nick (LRG_TYPE_ALIGN, lrg_widget_get_halign (widget)));
	json_object_set_string_member (o, "valign", enum_nick (LRG_TYPE_ALIGN, lrg_widget_get_valign (widget)));
	json_object_set_double_member (o, "fill_weight", lrg_widget_get_fill_weight (widget));

	/* Visibility and interaction */
	json_object_set_boolean_member (o, "visible", lrg_widget_get_visible (widget));
	json_object_set_boolean_member (o, "enabled", lrg_widget_get_enabled (widget));
	json_object_set_boolean_member (o, "focusable", lrg_widget_get_focusable (widget));
	json_object_set_boolean_member (o, "pointer_transparent", lrg_widget_get_pointer_transparent (widget));
	json_object_set_double_member (o, "opacity", lrg_widget_get_opacity (widget));

	/* Look of the main part */
	json_object_set_object_member (o, "look", look_json (widget));

	/* Children */
	json_object_set_int_member (o, "n_children", g_list_length (list));
	segments = child_segments (list);
	for (l = list, i = 0; l != NULL; l = l->next, i++)
	{
		g_autofree gchar *child_path = path != NULL
			? join_path (path, g_ptr_array_index (segments, i)) : NULL;

		if (recurse)
			json_array_add_object_element (children, node_json (LRG_WIDGET (l->data), child_path, TRUE));
		else if (child_path != NULL)
			json_array_add_string_element (children, child_path);
		else
			json_array_add_null_element (children);
	}
	json_object_set_array_member (o, "children", children);
	return o;
}

/* object_to_string: serialise and free @o */
static gchar *
object_to_string (JsonObject *o,
                  gboolean    pretty)
{
	g_autoptr(JsonNode)      node = json_node_new (JSON_NODE_OBJECT);
	g_autoptr(JsonGenerator) gen = json_generator_new ();

	json_node_take_object (node, o);
	json_generator_set_root (gen, node);
	json_generator_set_pretty (gen, pretty);
	return json_generator_to_data (gen, NULL);
}

/* set_path_member: a widget's path, or null */
static void
set_path_member (LrgUIInspector *self,
                 JsonObject     *o,
                 const gchar    *member,
                 LrgWidget      *widget)
{
	g_autofree gchar *path = widget != NULL ? lrg_ui_inspector_get_path (self, widget) : NULL;

	if (path != NULL)
		json_object_set_string_member (o, member, path);
	else
		json_object_set_null_member (o, member);
}

gchar *
lrg_ui_inspector_snapshot_json (LrgUIInspector *self,
                                gboolean        pretty)
{
	g_autoptr(GPtrArray) popups = NULL;
	g_autoptr(GPtrArray) overlays = NULL;
	JsonObject          *top;
	JsonObject          *size;
	JsonArray           *popup_nodes;
	JsonArray           *overlay_nodes;
	LrgWidget           *root;
	guint                i;

	g_return_val_if_fail (LRG_IS_UI_INSPECTOR (self), NULL);

	top = json_object_new ();
	if (self->canvas == NULL)
	{
		json_object_set_string_member (top, "error", "not attached to a canvas");
		return object_to_string (top, pretty);
	}
	root = LRG_WIDGET (self->canvas);

	size = json_object_new ();
	json_object_set_double_member (size, "width", lrg_widget_get_width (root));
	json_object_set_double_member (size, "height", lrg_widget_get_height (root));
	json_object_set_object_member (top, "canvas", size);
	json_object_set_object_member (top, "root", node_json (root, "", TRUE));

	/* The popup and overlay layers, each root with its own path */
	popups = g_ptr_array_new ();
	overlays = g_ptr_array_new ();
	collect_layers (self, popups, overlays);
	popup_nodes = json_array_new ();
	for (i = 0; i < popups->len; i++)
	{
		g_autofree gchar *path = g_strdup_printf (":popup[%u]", i);

		json_array_add_object_element (popup_nodes, node_json (g_ptr_array_index (popups, i), path, TRUE));
	}
	overlay_nodes = json_array_new ();
	for (i = 0; i < overlays->len; i++)
	{
		g_autofree gchar *path = g_strdup_printf (":overlay[%u]", i);

		json_array_add_object_element (overlay_nodes,
		                               node_json (g_ptr_array_index (overlays, i), path, TRUE));
	}
	json_object_set_array_member (top, "popups", popup_nodes);
	json_object_set_array_member (top, "overlays", overlay_nodes);

	set_path_member (self, top, "selected", self->selected);
	set_path_member (self, top, "hovered", self->hovered);
	return object_to_string (top, pretty);
}

/* --------------------------------------------------------------------------
 * GVariant snapshot
 * -------------------------------------------------------------------------- */

static GVariant *
color_variant (const GrlColor *c)
{
	return g_variant_new ("(yyyy)", c->r, c->g, c->b, c->a);
}

/* look_variant: the essentials of the resolved BOX look as a{sv} */
static GVariant *
look_variant (LrgWidget *widget)
{
	GVariantBuilder b;
	LrgPartLook     look;

	lrg_widget_resolve_look (widget, LRG_UI_PART_BOX, &look);
	g_variant_builder_init (&b, G_VARIANT_TYPE_VARDICT);
	g_variant_builder_add (&b, "{sv}", "background", color_variant (&look.background));
	g_variant_builder_add (&b, "{sv}", "foreground", color_variant (&look.foreground));
	g_variant_builder_add (&b, "{sv}", "border", color_variant (&look.border));
	g_variant_builder_add (&b, "{sv}", "border-width", g_variant_new_double (look.border_width));
	g_variant_builder_add (&b, "{sv}", "opacity", g_variant_new_double (look.opacity));
	g_variant_builder_add (&b, "{sv}", "shadow-blur", g_variant_new_double (look.shadow_blur));
	g_variant_builder_add (&b, "{sv}", "size", g_variant_new_double (look.size));
	g_variant_builder_add (&b, "{sv}", "radii",
	                       g_variant_new ("(dddd)", (gdouble) look.radii.top_left,
	                                      (gdouble) look.radii.top_right,
	                                      (gdouble) look.radii.bottom_right,
	                                      (gdouble) look.radii.bottom_left));
	return g_variant_builder_end (&b);
}

/*
 * add_variant_nodes:
 *
 * Appends @widget and (depth-first) its subtree to @builder as a{sv}
 * dictionaries.
 */
static void
add_variant_nodes (GVariantBuilder *builder,
                   LrgWidget       *widget,
                   const gchar     *path,
                   const gchar     *parent,
                   guint            depth,
                   const gchar     *layer)
{
	GVariantBuilder      node;
	GVariantBuilder      classes;
	const GQuark        *quarks;
	guint                n_classes = 0;
	guint                i;
	LrgInsets            m;
	GList               *list = children_of (widget);
	GList               *l;
	g_autoptr(GPtrArray) segments = NULL;
	const gchar         *name = lrg_widget_get_name (widget);

	g_variant_builder_init (&classes, G_VARIANT_TYPE_STRING_ARRAY);
	quarks = lrg_widget_get_classes (widget, &n_classes);
	for (i = 0; i < n_classes; i++)
		g_variant_builder_add (&classes, "s", g_quark_to_string (quarks[i]));
	lrg_widget_get_margin (widget, &m);

	g_variant_builder_init (&node, G_VARIANT_TYPE_VARDICT);
	g_variant_builder_add (&node, "{sv}", "path", g_variant_new_string (path));
	g_variant_builder_add (&node, "{sv}", "parent", g_variant_new_string (parent));
	g_variant_builder_add (&node, "{sv}", "depth", g_variant_new_uint32 (depth));
	g_variant_builder_add (&node, "{sv}", "layer", g_variant_new_string (layer));
	g_variant_builder_add (&node, "{sv}", "type", g_variant_new_string (G_OBJECT_TYPE_NAME (widget)));
	g_variant_builder_add (&node, "{sv}", "name", g_variant_new_string (name != NULL ? name : ""));
	g_variant_builder_add (&node, "{sv}", "classes", g_variant_builder_end (&classes));
	g_variant_builder_add (&node, "{sv}", "state",
	                       g_variant_new_uint32 ((guint32) lrg_widget_get_state_flags (widget)));
	g_variant_builder_add (&node, "{sv}", "x", g_variant_new_double (lrg_widget_get_world_x (widget)));
	g_variant_builder_add (&node, "{sv}", "y", g_variant_new_double (lrg_widget_get_world_y (widget)));
	g_variant_builder_add (&node, "{sv}", "width", g_variant_new_double (lrg_widget_get_width (widget)));
	g_variant_builder_add (&node, "{sv}", "height", g_variant_new_double (lrg_widget_get_height (widget)));
	g_variant_builder_add (&node, "{sv}", "margin",
	                       g_variant_new ("(dddd)", (gdouble) m.left, (gdouble) m.top,
	                                      (gdouble) m.right, (gdouble) m.bottom));
	g_variant_builder_add (&node, "{sv}", "halign",
	                       g_variant_new_string (enum_nick (LRG_TYPE_ALIGN, lrg_widget_get_halign (widget))));
	g_variant_builder_add (&node, "{sv}", "valign",
	                       g_variant_new_string (enum_nick (LRG_TYPE_ALIGN, lrg_widget_get_valign (widget))));
	g_variant_builder_add (&node, "{sv}", "fill-weight",
	                       g_variant_new_double (lrg_widget_get_fill_weight (widget)));
	g_variant_builder_add (&node, "{sv}", "visible", g_variant_new_boolean (lrg_widget_get_visible (widget)));
	g_variant_builder_add (&node, "{sv}", "enabled", g_variant_new_boolean (lrg_widget_get_enabled (widget)));
	g_variant_builder_add (&node, "{sv}", "focusable",
	                       g_variant_new_boolean (lrg_widget_get_focusable (widget)));
	g_variant_builder_add (&node, "{sv}", "pointer-transparent",
	                       g_variant_new_boolean (lrg_widget_get_pointer_transparent (widget)));
	g_variant_builder_add (&node, "{sv}", "opacity", g_variant_new_double (lrg_widget_get_opacity (widget)));
	g_variant_builder_add (&node, "{sv}", "n-children", g_variant_new_uint32 (g_list_length (list)));
	g_variant_builder_add (&node, "{sv}", "look", look_variant (widget));
	g_variant_builder_add_value (builder, g_variant_builder_end (&node));

	segments = child_segments (list);
	for (l = list, i = 0; l != NULL; l = l->next, i++)
	{
		g_autofree gchar *child_path = join_path (path, g_ptr_array_index (segments, i));

		add_variant_nodes (builder, LRG_WIDGET (l->data), child_path, path, depth + 1, layer);
	}
}

GVariant *
lrg_ui_inspector_snapshot (LrgUIInspector *self)
{
	GVariantBuilder      builder;
	g_autoptr(GPtrArray) popups = NULL;
	g_autoptr(GPtrArray) overlays = NULL;
	guint                i;

	g_return_val_if_fail (LRG_IS_UI_INSPECTOR (self), NULL);

	g_variant_builder_init (&builder, G_VARIANT_TYPE ("aa{sv}"));
	if (self->canvas != NULL)
	{
		add_variant_nodes (&builder, LRG_WIDGET (self->canvas), "", "", 0, "canvas");

		popups = g_ptr_array_new ();
		overlays = g_ptr_array_new ();
		collect_layers (self, popups, overlays);
		for (i = 0; i < popups->len; i++)
		{
			g_autofree gchar *path = g_strdup_printf (":popup[%u]", i);

			add_variant_nodes (&builder, g_ptr_array_index (popups, i), path, "", 0, "popup");
		}
		for (i = 0; i < overlays->len; i++)
		{
			g_autofree gchar *path = g_strdup_printf (":overlay[%u]", i);

			add_variant_nodes (&builder, g_ptr_array_index (overlays, i), path, "", 0, "overlay");
		}
	}
	return g_variant_ref_sink (g_variant_builder_end (&builder));
}

/* --------------------------------------------------------------------------
 * Single widget description
 * -------------------------------------------------------------------------- */

/*
 * value_to_json:
 *
 * A property value as JSON: scalars as themselves, enums as nicks,
 * flags as arrays of nicks, GrlColor as "#rrggbbaa", objects as their
 * type name (or null), anything else as g_strdup_value_contents().
 */
static JsonNode *
value_to_json (const GValue *value)
{
	JsonNode *node = json_node_new (JSON_NODE_VALUE);
	GType     type = G_VALUE_TYPE (value);

	switch (G_TYPE_FUNDAMENTAL (type))
	{
	case G_TYPE_BOOLEAN:
		json_node_set_boolean (node, g_value_get_boolean (value));
		return node;
	case G_TYPE_CHAR:
		json_node_set_int (node, g_value_get_schar (value));
		return node;
	case G_TYPE_UCHAR:
		json_node_set_int (node, g_value_get_uchar (value));
		return node;
	case G_TYPE_INT:
		json_node_set_int (node, g_value_get_int (value));
		return node;
	case G_TYPE_UINT:
		json_node_set_int (node, g_value_get_uint (value));
		return node;
	case G_TYPE_LONG:
		json_node_set_int (node, g_value_get_long (value));
		return node;
	case G_TYPE_ULONG:
		json_node_set_int (node, (gint64) g_value_get_ulong (value));
		return node;
	case G_TYPE_INT64:
		json_node_set_int (node, g_value_get_int64 (value));
		return node;
	case G_TYPE_UINT64:
		json_node_set_int (node, (gint64) g_value_get_uint64 (value));
		return node;
	case G_TYPE_FLOAT:
		json_node_set_double (node, g_value_get_float (value));
		return node;
	case G_TYPE_DOUBLE:
		json_node_set_double (node, g_value_get_double (value));
		return node;
	case G_TYPE_STRING:
		if (g_value_get_string (value) == NULL)
		{
			json_node_free (node);
			return json_node_new (JSON_NODE_NULL);
		}
		json_node_set_string (node, g_value_get_string (value));
		return node;
	case G_TYPE_ENUM:
		{
			const gchar *nick = enum_nick (type, g_value_get_enum (value));

			if (nick != NULL)
				json_node_set_string (node, nick);
			else
				json_node_set_int (node, g_value_get_enum (value));
			return node;
		}
	case G_TYPE_FLAGS:
		json_node_free (node);
		node = json_node_new (JSON_NODE_ARRAY);
		json_node_take_array (node, flags_array (type, g_value_get_flags (value)));
		return node;
	case G_TYPE_OBJECT:
		if (g_value_get_object (value) == NULL)
		{
			json_node_free (node);
			return json_node_new (JSON_NODE_NULL);
		}
		json_node_set_string (node, G_OBJECT_TYPE_NAME (g_value_get_object (value)));
		return node;
	default:
		break;
	}

	if (type == GRL_TYPE_COLOR && g_value_get_boxed (value) != NULL)
	{
		g_autofree gchar *text = color_string (g_value_get_boxed (value));

		json_node_set_string (node, text);
	}
	else
	{
		g_autofree gchar *text = g_strdup_value_contents (value);

		json_node_set_string (node, text);
	}
	return node;
}

gchar *
lrg_ui_inspector_describe_widget (LrgUIInspector *self,
                                  LrgWidget      *widget,
                                  gboolean        pretty)
{
	g_autofree gchar   *path = NULL;
	g_autofree GParamSpec **specs = NULL;
	JsonObject         *o;
	JsonObject         *props;
	guint               n_specs = 0;
	guint               i;

	g_return_val_if_fail (LRG_IS_UI_INSPECTOR (self), NULL);
	g_return_val_if_fail (LRG_IS_WIDGET (widget), NULL);

	path = lrg_ui_inspector_get_path (self, widget);
	if (path == NULL)
		return NULL;

	o = node_json (widget, path, FALSE);

	/* Every readable property, for live tweaking */
	props = json_object_new ();
	specs = g_object_class_list_properties (G_OBJECT_GET_CLASS (widget), &n_specs);
	for (i = 0; i < n_specs; i++)
	{
		GValue value = G_VALUE_INIT;

		if (!(specs[i]->flags & G_PARAM_READABLE))
			continue;
		g_value_init (&value, specs[i]->value_type);
		g_object_get_property (G_OBJECT (widget), specs[i]->name, &value);
		json_object_set_member (props, specs[i]->name, value_to_json (&value));
		g_value_unset (&value);
	}
	json_object_set_object_member (o, "properties", props);
	return object_to_string (o, pretty);
}

/* ==========================================================================
 * Picking, pick mode and selection
 * ========================================================================== */

LrgWidget *
lrg_ui_inspector_pick (LrgUIInspector *self,
                       gfloat          x,
                       gfloat          y)
{
	g_return_val_if_fail (LRG_IS_UI_INSPECTOR (self), NULL);

	if (self->canvas == NULL)
		return NULL;

	/* The canvas's own hit-test: overlays, then popups (modal veils
	 * stop the search), then the tree. The inspector overlay is
	 * pointer-transparent and childless, so it is never returned. */
	return lrg_canvas_widget_at_point (self->canvas, x, y);
}

static void
set_hovered (LrgUIInspector *self,
             LrgWidget      *widget)
{
	if (self->hovered == widget)
		return;
	g_set_weak_pointer (&self->hovered, widget);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_HOVERED]);
}

gboolean
lrg_ui_inspector_get_pick_mode (LrgUIInspector *self)
{
	g_return_val_if_fail (LRG_IS_UI_INSPECTOR (self), FALSE);

	return self->pick_mode;
}

void
lrg_ui_inspector_set_pick_mode (LrgUIInspector *self,
                                gboolean        pick_mode)
{
	g_return_if_fail (LRG_IS_UI_INSPECTOR (self));

	pick_mode = !!pick_mode;
	if (self->pick_mode == pick_mode)
		return;
	self->pick_mode = pick_mode;
	if (!pick_mode)
		set_hovered (self, NULL);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_PICK_MODE]);
}

gboolean
lrg_ui_inspector_filter_input (LrgUIInspector *self,
                               LrgCanvasInput *input)
{
	const guint left = LRG_CANVAS_BUTTON_MASK (GRL_MOUSE_BUTTON_LEFT);
	guint       released;
	gboolean    touch_began;
	guint       i;

	g_return_val_if_fail (LRG_IS_UI_INSPECTOR (self), FALSE);
	g_return_val_if_fail (input != NULL, FALSE);

	/* Outside pick mode: just remember what the app holds, so a release
	 * after pick mode starts still reaches the widget that captured it */
	if (!self->pick_mode || self->canvas == NULL)
	{
		self->app_buttons = (self->app_buttons | input->buttons_pressed) & ~input->buttons_released;
		self->touch_down = input->n_touches > 0;
		return FALSE;
	}

	/* Escape leaves pick mode and never reaches the app */
	for (i = 0; i < input->n_keys; i++)
	{
		if (input->keys[i] != GRL_KEY_ESCAPE)
			continue;
		memmove (&input->keys[i], &input->keys[i + 1],
		         (input->n_keys - i - 1) * sizeof (input->keys[0]));
		input->n_keys--;
		input->cancel = FALSE;
		lrg_ui_inspector_set_pick_mode (self, FALSE);
		break;
	}

	/* Hover follows the pointer (or the first finger) */
	touch_began = input->n_touches > 0 && !self->touch_down;
	self->touch_down = input->n_touches > 0;
	if (self->pick_mode)
	{
		gfloat     x = input->n_touches > 0 ? input->touches[0].x : input->pointer_x;
		gfloat     y = input->n_touches > 0 ? input->touches[0].y : input->pointer_y;
		LrgWidget *hit = lrg_ui_inspector_pick (self, x, y);

		set_hovered (self, hit);

		/* A left press or a new touch selects */
		if ((input->buttons_pressed & left) != 0 || touch_began)
		{
			lrg_ui_inspector_set_selected (self, hit);
			g_signal_emit (self, signals[SIGNAL_PICKED], 0, hit);
		}
	}

	/* Strip the click: no presses, wheel or touches reach the app, and
	 * only releases of buttons it saw go down (an app capture lets go) */
	released = input->buttons_released & self->app_buttons;
	self->app_buttons &= ~input->buttons_released;
	input->buttons_released = released;
	input->buttons_pressed = 0;
	input->buttons_down &= self->app_buttons;
	input->wheel_x = 0.0f;
	input->wheel_y = 0.0f;
	input->n_touches = 0;
	return TRUE;
}

void
lrg_ui_inspector_handle_input (LrgUIInspector *self)
{
	LrgCanvasInput input;

	g_return_if_fail (LRG_IS_UI_INSPECTOR (self));

	if (self->canvas == NULL)
		return;

	lrg_canvas_input_poll (&input, IsWindowReady () ? GetFrameTime () : 0.0f);
	lrg_ui_inspector_filter_input (self, &input);
	lrg_canvas_process_input (self->canvas, &input);
}

LrgWidget *
lrg_ui_inspector_get_selected (LrgUIInspector *self)
{
	g_return_val_if_fail (LRG_IS_UI_INSPECTOR (self), NULL);

	return self->selected;
}

void
lrg_ui_inspector_set_selected (LrgUIInspector *self,
                               LrgWidget      *widget)
{
	g_return_if_fail (LRG_IS_UI_INSPECTOR (self));
	g_return_if_fail (widget == NULL || LRG_IS_WIDGET (widget));

	if (self->selected == widget)
		return;
	g_set_weak_pointer (&self->selected, widget);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SELECTED]);
}

LrgWidget *
lrg_ui_inspector_get_hovered (LrgUIInspector *self)
{
	g_return_val_if_fail (LRG_IS_UI_INSPECTOR (self), NULL);

	return self->hovered;
}

gboolean
lrg_ui_inspector_highlight (LrgUIInspector  *self,
                            const gchar     *path,
                            GError         **error)
{
	LrgWidget *widget;

	g_return_val_if_fail (LRG_IS_UI_INSPECTOR (self), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	if (path == NULL)
	{
		lrg_ui_inspector_set_selected (self, NULL);
		return TRUE;
	}
	widget = lookup_or_error (self, path, error);
	if (widget == NULL)
		return FALSE;
	lrg_ui_inspector_set_selected (self, widget);
	return TRUE;
}

gboolean
lrg_ui_inspector_get_highlight_visible (LrgUIInspector *self)
{
	g_return_val_if_fail (LRG_IS_UI_INSPECTOR (self), FALSE);

	return self->highlight_visible;
}

void
lrg_ui_inspector_set_highlight_visible (LrgUIInspector *self,
                                        gboolean        visible)
{
	g_return_if_fail (LRG_IS_UI_INSPECTOR (self));

	visible = !!visible;
	if (self->highlight_visible == visible)
		return;
	self->highlight_visible = visible;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_HIGHLIGHT_VISIBLE]);
}

/* ==========================================================================
 * Highlight
 * ========================================================================== */

/* in_canvas: @widget is still shown in the inspected canvas */
static gboolean
in_canvas (LrgUIInspector *self,
           LrgWidget      *widget)
{
	return widget != NULL && self->canvas != NULL &&
	       lrg_widget_get_root (widget) == LRG_WIDGET (self->canvas) &&
	       lrg_widget_get_visible (widget);
}

/*
 * paint_margin:
 *
 * The margin as four translucent strips around the bounds (the classic
 * box-model view): top and bottom span the full outer width, left and
 * right only the widget's height.
 */
static void
paint_margin (LrgPaintList    *list,
              const LrgInsets *m,
              gfloat           x,
              gfloat           y,
              gfloat           w,
              gfloat           h)
{
	gfloat outer_x = x - m->left;
	gfloat outer_w = w + m->left + m->right;

	if (m->top > 0.0f)
		lrg_paint_list_rect (list, outer_x, y - m->top, outer_w, m->top, 0.0f, &hl_margin_fill);
	if (m->bottom > 0.0f)
		lrg_paint_list_rect (list, outer_x, y + h, outer_w, m->bottom, 0.0f, &hl_margin_fill);
	if (m->left > 0.0f)
		lrg_paint_list_rect (list, outer_x, y, m->left, h, 0.0f, &hl_margin_fill);
	if (m->right > 0.0f)
		lrg_paint_list_rect (list, x + w, y, m->right, h, 0.0f, &hl_margin_fill);
}

/*
 * paint_label:
 *
 * A dark pill with "Type#name  w × h" above the widget, or below it when
 * there is no room above, clamped to the canvas's left edge.
 */
static void
paint_label (LrgUIInspector *self,
             LrgPaintList   *list,
             LrgWidget      *widget,
             gfloat          x,
             gfloat          y,
             gfloat          w,
             gfloat          h)
{
	const gchar      *name = lrg_widget_get_name (widget);
	g_autofree gchar *text = NULL;
	GrlFont          *font;
	gfloat            tw = 0.0f;
	gfloat            th = 0.0f;
	gfloat            bw;
	gfloat            bh;
	gfloat            bx;
	gfloat            by;

	text = g_strdup_printf ("%s%s%s  %.0f\303\227%.0f", G_OBJECT_TYPE_NAME (widget),
	                        name != NULL ? "#" : "", name != NULL ? name : "", w, h);
	font = lrg_theme_get_ui_font (lrg_widget_get_theme (LRG_WIDGET (self->canvas)));
	lrg_paint_measure_text (font, text, HL_LABEL_SIZE, 0.0f, &tw, &th);

	bw = tw + HL_LABEL_PAD_X * 2.0f;
	bh = th + HL_LABEL_PAD_Y * 2.0f;
	bx = MAX (x, lrg_widget_get_world_x (LRG_WIDGET (self->canvas)));
	by = y - bh - HL_LABEL_GAP;
	if (by < lrg_widget_get_world_y (LRG_WIDGET (self->canvas)))
		by = y + h + HL_LABEL_GAP;

	lrg_paint_list_rect (list, bx, by, bw, bh, 4.0f, &hl_label_bg);
	lrg_paint_list_text (list, font, text, bx + HL_LABEL_PAD_X, by + HL_LABEL_PAD_Y,
	                     HL_LABEL_SIZE, 0.0f, &hl_label_fg);
}

void
lrg_ui_inspector_paint_highlight (LrgUIInspector *self,
                                  LrgPaintList   *list)
{
	g_return_if_fail (LRG_IS_UI_INSPECTOR (self));
	g_return_if_fail (LRG_IS_PAINT_LIST (list));

	if (!self->highlight_visible || self->canvas == NULL)
		return;

	/* Hovered (pick mode): a light outline only */
	if (in_canvas (self, self->hovered) && self->hovered != self->selected)
	{
		LrgWidget *w = self->hovered;
		gfloat     x = lrg_widget_get_world_x (w);
		gfloat     y = lrg_widget_get_world_y (w);
		gfloat     width = lrg_widget_get_width (w);
		gfloat     height = lrg_widget_get_height (w);

		lrg_paint_list_rect (list, x, y, width, height, 0.0f, &hl_hovered_fill);
		lrg_paint_list_border (list, x, y, width, height, 0.0f, HL_BORDER_WIDTH, &hl_hovered_border);
	}

	/* Selected: margin, bounds and the type label */
	if (in_canvas (self, self->selected))
	{
		LrgWidget *w = self->selected;
		LrgInsets  m;
		gfloat     x = lrg_widget_get_world_x (w);
		gfloat     y = lrg_widget_get_world_y (w);
		gfloat     width = lrg_widget_get_width (w);
		gfloat     height = lrg_widget_get_height (w);

		lrg_widget_get_margin (w, &m);
		paint_margin (list, &m, x, y, width, height);
		lrg_paint_list_rect (list, x, y, width, height, 0.0f, &hl_selected_fill);
		lrg_paint_list_border (list, x, y, width, height, 0.0f, HL_BORDER_WIDTH, &hl_selected_border);
		paint_label (self, list, w, x, y, width, height);
	}
}

/* ==========================================================================
 * Timings
 * ========================================================================== */

void
lrg_ui_inspector_get_timings (LrgUIInspector        *self,
                              LrgUIInspectorTimings *timings)
{
	LrgPaintList *list;
	guint         n;

	g_return_if_fail (LRG_IS_UI_INSPECTOR (self));
	g_return_if_fail (timings != NULL);

	memset (timings, 0, sizeof (*timings));
	if (self->canvas == NULL)
		return;

	_lrg_canvas_get_timings (self->canvas, &timings->step_us, &timings->layout_us,
	                         &timings->record_us);

	/* Counts from the last recorded list, minus the highlight */
	list = lrg_canvas_get_paint_list (self->canvas);
	n = lrg_paint_list_get_n_instances (list);
	timings->n_instances = n >= self->overlay_instances ? n - self->overlay_instances : 0;
	timings->n_commands = lrg_paint_list_get_n_commands (list);
	timings->draw_calls = lrg_paint_renderer_get_draw_calls (lrg_paint_renderer_get_default ());
	timings->frames = self->frames;
}

/* ==========================================================================
 * Properties by name
 * ========================================================================== */

/* writable_spec: the property, if it exists and can be written now */
static GParamSpec *
writable_spec (LrgWidget    *widget,
               const gchar  *name,
               GError      **error)
{
	GParamSpec *spec = g_object_class_find_property (G_OBJECT_GET_CLASS (widget), name);

	if (spec == NULL || !(spec->flags & G_PARAM_WRITABLE) || (spec->flags & G_PARAM_CONSTRUCT_ONLY))
	{
		g_set_error (error, LRG_UI_INSPECTOR_ERROR, LRG_UI_INSPECTOR_ERROR_INVALID_PROPERTY,
		             "%s has no writable property \"%s\"", G_OBJECT_TYPE_NAME (widget), name);
		return NULL;
	}
	return spec;
}

gboolean
lrg_ui_inspector_get_property (LrgUIInspector  *self,
                               const gchar     *path,
                               const gchar     *name,
                               GValue          *value,
                               GError         **error)
{
	LrgWidget  *widget;
	GParamSpec *spec;

	g_return_val_if_fail (LRG_IS_UI_INSPECTOR (self), FALSE);
	g_return_val_if_fail (name != NULL, FALSE);
	g_return_val_if_fail (value != NULL && !G_IS_VALUE (value), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	widget = lookup_or_error (self, path, error);
	if (widget == NULL)
		return FALSE;

	spec = g_object_class_find_property (G_OBJECT_GET_CLASS (widget), name);
	if (spec == NULL || !(spec->flags & G_PARAM_READABLE))
	{
		g_set_error (error, LRG_UI_INSPECTOR_ERROR, LRG_UI_INSPECTOR_ERROR_INVALID_PROPERTY,
		             "%s has no readable property \"%s\"", G_OBJECT_TYPE_NAME (widget), name);
		return FALSE;
	}
	g_value_init (value, spec->value_type);
	g_object_get_property (G_OBJECT (widget), name, value);
	return TRUE;
}

/*
 * apply_value:
 *
 * Converts @value to the property's type, rejects out-of-range values
 * (g_param_value_validate() would silently clamp them) and sets it.
 */
static gboolean
apply_value (LrgWidget     *widget,
             GParamSpec    *spec,
             const GValue  *value,
             GError       **error)
{
	GValue converted = G_VALUE_INIT;

	g_value_init (&converted, spec->value_type);
	if (G_VALUE_TYPE (value) == spec->value_type)
		g_value_copy (value, &converted);
	else if (!g_value_type_transformable (G_VALUE_TYPE (value), spec->value_type) ||
	         !g_value_transform (value, &converted))
	{
		g_set_error (error, LRG_UI_INSPECTOR_ERROR, LRG_UI_INSPECTOR_ERROR_INVALID_VALUE,
		             "Cannot convert %s to %s for \"%s\"", G_VALUE_TYPE_NAME (value),
		             g_type_name (spec->value_type), spec->name);
		g_value_unset (&converted);
		return FALSE;
	}

	if (g_param_value_validate (spec, &converted))
	{
		g_set_error (error, LRG_UI_INSPECTOR_ERROR, LRG_UI_INSPECTOR_ERROR_INVALID_VALUE,
		             "Value out of range for \"%s\"", spec->name);
		g_value_unset (&converted);
		return FALSE;
	}

	g_object_set_property (G_OBJECT (widget), spec->name, &converted);
	g_value_unset (&converted);
	return TRUE;
}

gboolean
lrg_ui_inspector_set_property (LrgUIInspector  *self,
                               const gchar     *path,
                               const gchar     *name,
                               const GValue    *value,
                               GError         **error)
{
	LrgWidget  *widget;
	GParamSpec *spec;

	g_return_val_if_fail (LRG_IS_UI_INSPECTOR (self), FALSE);
	g_return_val_if_fail (name != NULL, FALSE);
	g_return_val_if_fail (G_IS_VALUE (value), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	widget = lookup_or_error (self, path, error);
	if (widget == NULL)
		return FALSE;
	spec = writable_spec (widget, name, error);
	if (spec == NULL)
		return FALSE;
	return apply_value (widget, spec, value, error);
}

/* parse_color: "#rrggbb" or "#rrggbbaa" */
static gboolean
parse_color (const gchar *text,
             GrlColor    *color)
{
	gsize   len;
	guint64 v;
	gchar  *end = NULL;

	if (text[0] != '#')
		return FALSE;
	len = strlen (text + 1);
	if (len != 6 && len != 8)
		return FALSE;
	v = g_ascii_strtoull (text + 1, &end, 16);
	if (end == NULL || *end != '\0')
		return FALSE;
	if (len == 6)
		v = (v << 8) | 0xff;
	color->r = (guint8) ((v >> 24) & 0xff);
	color->g = (guint8) ((v >> 16) & 0xff);
	color->b = (guint8) ((v >> 8) & 0xff);
	color->a = (guint8) (v & 0xff);
	return TRUE;
}

/*
 * parse_text_value:
 *
 * Fills @value (initialised to a type @text can express) from @text for
 * a property of @type: booleans, integers, floats, enums (nick, name or
 * number), flags ("a|b"), strings and GrlColor.
 */
static gboolean
parse_text_value (GType        type,
                  const gchar *text,
                  GValue      *value)
{
	gchar *end = NULL;

	switch (G_TYPE_FUNDAMENTAL (type))
	{
	case G_TYPE_STRING:
		g_value_init (value, G_TYPE_STRING);
		g_value_set_string (value, text);
		return TRUE;
	case G_TYPE_BOOLEAN:
		g_value_init (value, G_TYPE_BOOLEAN);
		if (g_ascii_strcasecmp (text, "true") == 0 || g_strcmp0 (text, "1") == 0 ||
		    g_ascii_strcasecmp (text, "yes") == 0)
			g_value_set_boolean (value, TRUE);
		else if (g_ascii_strcasecmp (text, "false") == 0 || g_strcmp0 (text, "0") == 0 ||
		         g_ascii_strcasecmp (text, "no") == 0)
			g_value_set_boolean (value, FALSE);
		else
			return FALSE;
		return TRUE;
	case G_TYPE_CHAR:
	case G_TYPE_INT:
	case G_TYPE_LONG:
	case G_TYPE_INT64:
		{
			gint64 n = g_ascii_strtoll (text, &end, 10);

			if (end == text || *end != '\0')
				return FALSE;
			g_value_init (value, G_TYPE_INT64);
			g_value_set_int64 (value, n);
			return TRUE;
		}
	case G_TYPE_UCHAR:
	case G_TYPE_UINT:
	case G_TYPE_ULONG:
	case G_TYPE_UINT64:
		{
			guint64 n;

			if (text[0] == '-')
				return FALSE;
			n = g_ascii_strtoull (text, &end, 10);
			if (end == text || *end != '\0')
				return FALSE;
			g_value_init (value, G_TYPE_UINT64);
			g_value_set_uint64 (value, n);
			return TRUE;
		}
	case G_TYPE_FLOAT:
	case G_TYPE_DOUBLE:
		{
			gdouble d = g_ascii_strtod (text, &end);

			if (end == text || *end != '\0' || !isfinite (d))
				return FALSE;
			g_value_init (value, G_TYPE_DOUBLE);
			g_value_set_double (value, d);
			return TRUE;
		}
	case G_TYPE_ENUM:
		{
			GEnumClass *klass = g_type_class_ref (type);
			GEnumValue *v = g_enum_get_value_by_nick (klass, text);
			gint64      n;
			gboolean    ok = TRUE;

			if (v == NULL)
				v = g_enum_get_value_by_name (klass, text);
			if (v != NULL)
				n = v->value;
			else
			{
				n = g_ascii_strtoll (text, &end, 10);
				ok = end != text && *end == '\0' && g_enum_get_value (klass, (gint) n) != NULL;
			}
			g_type_class_unref (klass);
			if (!ok)
				return FALSE;
			g_value_init (value, type);
			g_value_set_enum (value, (gint) n);
			return TRUE;
		}
	case G_TYPE_FLAGS:
		{
			GFlagsClass  *klass = g_type_class_ref (type);
			g_auto(GStrv) parts = g_strsplit (text, "|", -1);
			guint         bits = 0;
			gboolean      ok = TRUE;
			guint         i;

			for (i = 0; parts[i] != NULL && ok; i++)
			{
				const gchar *part = g_strstrip (parts[i]);
				GFlagsValue *v;

				if (*part == '\0')
					continue;
				v = g_flags_get_value_by_nick (klass, part);
				if (v == NULL)
					v = g_flags_get_value_by_name (klass, part);
				if (v != NULL)
					bits |= v->value;
				else
					ok = FALSE;
			}
			g_type_class_unref (klass);
			if (!ok)
				return FALSE;
			g_value_init (value, type);
			g_value_set_flags (value, bits);
			return TRUE;
		}
	default:
		break;
	}

	if (type == GRL_TYPE_COLOR)
	{
		GrlColor color;

		if (!parse_color (text, &color))
			return FALSE;
		g_value_init (value, GRL_TYPE_COLOR);
		g_value_set_boxed (value, &color);
		return TRUE;
	}
	return FALSE;
}

gboolean
lrg_ui_inspector_set_property_from_string (LrgUIInspector  *self,
                                           const gchar     *path,
                                           const gchar     *name,
                                           const gchar     *text,
                                           GError         **error)
{
	LrgWidget  *widget;
	GParamSpec *spec;
	GValue      value = G_VALUE_INIT;
	gboolean    ok;

	g_return_val_if_fail (LRG_IS_UI_INSPECTOR (self), FALSE);
	g_return_val_if_fail (name != NULL, FALSE);
	g_return_val_if_fail (text != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	widget = lookup_or_error (self, path, error);
	if (widget == NULL)
		return FALSE;
	spec = writable_spec (widget, name, error);
	if (spec == NULL)
		return FALSE;

	if (!parse_text_value (spec->value_type, text, &value))
	{
		g_set_error (error, LRG_UI_INSPECTOR_ERROR, LRG_UI_INSPECTOR_ERROR_INVALID_VALUE,
		             "Cannot parse \"%s\" as %s for \"%s\"", text, g_type_name (spec->value_type), name);
		if (G_IS_VALUE (&value))
			g_value_unset (&value);
		return FALSE;
	}
	ok = apply_value (widget, spec, &value, error);
	g_value_unset (&value);
	return ok;
}

/* ==========================================================================
 * Attaching
 * ========================================================================== */

/* canvas_finalized: the canvas died; it took the overlay with it */
static void
canvas_finalized (gpointer  data,
                  GObject  *where_the_object_was)
{
	LrgUIInspector *self = LRG_UI_INSPECTOR (data);

	self->canvas = NULL;
	g_clear_weak_pointer (&self->overlay);
	g_clear_weak_pointer (&self->selected);
	g_clear_weak_pointer (&self->hovered);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_CANVAS]);
}

/* detach: undo everything attach did on a still-alive canvas */
static void
detach (LrgUIInspector *self)
{
	LrgCanvas *canvas = self->canvas;

	if (canvas == NULL)
		return;

	g_object_weak_unref (G_OBJECT (canvas), canvas_finalized, self);
	self->canvas = NULL;
	if (self->overlay != NULL)
	{
		LrgWidget *overlay = self->overlay;

		g_clear_weak_pointer (&self->overlay);
		lrg_canvas_remove_overlay (canvas, overlay);
	}
	_lrg_canvas_set_timing_enabled (canvas, FALSE);
	g_clear_weak_pointer (&self->selected);
	g_clear_weak_pointer (&self->hovered);
}

LrgCanvas *
lrg_ui_inspector_get_canvas (LrgUIInspector *self)
{
	g_return_val_if_fail (LRG_IS_UI_INSPECTOR (self), NULL);

	return self->canvas;
}

void
lrg_ui_inspector_set_canvas (LrgUIInspector *self,
                             LrgCanvas      *canvas)
{
	LrgUIInspectorOverlay *overlay;

	g_return_if_fail (LRG_IS_UI_INSPECTOR (self));
	g_return_if_fail (canvas == NULL || LRG_IS_CANVAS (canvas));

	if (self->canvas == canvas)
		return;

	detach (self);
	self->frames = 0;
	self->overlay_instances = 0;
	self->app_buttons = 0;
	self->touch_down = FALSE;

	if (canvas != NULL)
	{
		self->canvas = canvas;
		g_object_weak_ref (G_OBJECT (canvas), canvas_finalized, self);

		/* The highlight layer: the canvas owns the widget */
		overlay = g_object_new (LRG_TYPE_UI_INSPECTOR_OVERLAY, NULL);
		g_set_weak_pointer (&overlay->inspector, self);
		lrg_canvas_add_overlay (canvas, LRG_WIDGET (overlay));
		g_set_weak_pointer (&self->overlay, LRG_WIDGET (overlay));
		g_object_unref (overlay);

		_lrg_canvas_set_timing_enabled (canvas, TRUE);
	}
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_CANVAS]);
}

/* ==========================================================================
 * GObject
 * ========================================================================== */

static void
lrg_ui_inspector_dispose (GObject *object)
{
	LrgUIInspector *self = LRG_UI_INSPECTOR (object);

	detach (self);
	G_OBJECT_CLASS (lrg_ui_inspector_parent_class)->dispose (object);
}

static void
lrg_ui_inspector_get_property_impl (GObject    *object,
                                    guint       prop_id,
                                    GValue     *value,
                                    GParamSpec *pspec)
{
	LrgUIInspector *self = LRG_UI_INSPECTOR (object);

	switch (prop_id)
	{
	case PROP_CANVAS:
		g_value_set_object (value, self->canvas);
		break;
	case PROP_PICK_MODE:
		g_value_set_boolean (value, self->pick_mode);
		break;
	case PROP_SELECTED:
		g_value_set_object (value, self->selected);
		break;
	case PROP_HOVERED:
		g_value_set_object (value, self->hovered);
		break;
	case PROP_HIGHLIGHT_VISIBLE:
		g_value_set_boolean (value, self->highlight_visible);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
lrg_ui_inspector_set_property_impl (GObject      *object,
                                    guint         prop_id,
                                    const GValue *value,
                                    GParamSpec   *pspec)
{
	LrgUIInspector *self = LRG_UI_INSPECTOR (object);

	switch (prop_id)
	{
	case PROP_CANVAS:
		lrg_ui_inspector_set_canvas (self, g_value_get_object (value));
		break;
	case PROP_PICK_MODE:
		lrg_ui_inspector_set_pick_mode (self, g_value_get_boolean (value));
		break;
	case PROP_SELECTED:
		lrg_ui_inspector_set_selected (self, g_value_get_object (value));
		break;
	case PROP_HIGHLIGHT_VISIBLE:
		lrg_ui_inspector_set_highlight_visible (self, g_value_get_boolean (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
lrg_ui_inspector_class_init (LrgUIInspectorClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->dispose = lrg_ui_inspector_dispose;
	object_class->get_property = lrg_ui_inspector_get_property_impl;
	object_class->set_property = lrg_ui_inspector_set_property_impl;

	/**
	 * LrgUIInspector:canvas:
	 *
	 * The inspected canvas, held weakly (%NULL once it is finalized).
	 */
	properties[PROP_CANVAS] =
		g_param_spec_object ("canvas", "Canvas", "The inspected canvas",
		                     LRG_TYPE_CANVAS,
		                     G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgUIInspector:pick-mode:
	 *
	 * Whether hovering highlights and clicking selects (see
	 * lrg_ui_inspector_filter_input()).
	 */
	properties[PROP_PICK_MODE] =
		g_param_spec_boolean ("pick-mode", "Pick mode", "Hover highlights, click selects",
		                      FALSE,
		                      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgUIInspector:selected:
	 *
	 * The selected widget, held weakly.
	 */
	properties[PROP_SELECTED] =
		g_param_spec_object ("selected", "Selected", "The selected widget",
		                     LRG_TYPE_WIDGET,
		                     G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgUIInspector:hovered:
	 *
	 * The widget under the pointer while pick mode is on.
	 */
	properties[PROP_HOVERED] =
		g_param_spec_object ("hovered", "Hovered", "The widget under the pointer in pick mode",
		                     LRG_TYPE_WIDGET,
		                     G_PARAM_READABLE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgUIInspector:highlight-visible:
	 *
	 * Whether the selected and hovered widgets are drawn highlighted.
	 */
	properties[PROP_HIGHLIGHT_VISIBLE] =
		g_param_spec_boolean ("highlight-visible", "Highlight visible", "Draw the highlight overlay",
		                      TRUE,
		                      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	g_object_class_install_properties (object_class, N_PROPS, properties);

	/**
	 * LrgUIInspector::picked:
	 * @self: the inspector
	 * @widget: (nullable): the widget clicked in pick mode, or %NULL for
	 *   empty space
	 *
	 * Emitted when a click (or touch) in pick mode selects a widget.
	 */
	signals[SIGNAL_PICKED] =
		g_signal_new ("picked", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
		              0, NULL, NULL, NULL, G_TYPE_NONE, 1, LRG_TYPE_WIDGET);
}

static void
lrg_ui_inspector_init (LrgUIInspector *self)
{
	self->highlight_visible = TRUE;
}

LrgUIInspector *
lrg_ui_inspector_new (void)
{
	return g_object_new (LRG_TYPE_UI_INSPECTOR, NULL);
}

LrgUIInspector *
lrg_ui_inspector_get_default (void)
{
	/* UI runs on the main thread; created on first use, never freed */
	if (default_inspector == NULL)
		default_inspector = lrg_ui_inspector_new ();
	return default_inspector;
}

void
lrg_ui_inspector_register_canvas (LrgCanvas *canvas)
{
	g_return_if_fail (canvas == NULL || LRG_IS_CANVAS (canvas));

	lrg_ui_inspector_set_canvas (lrg_ui_inspector_get_default (), canvas);
}
