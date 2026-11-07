/* lrg-property-grid.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgPropertyGrid: a reflected property editor.
 *
 * Structure
 * ---------
 * The grid is a vertical stack: an optional filter field, the rows of
 * ungrouped properties, then one LrgExpander section per group with a
 * vbox of rows. Each LrgPropertyRow paints its label column itself and
 * holds one editor widget, made by a factory looked up by hint and
 * value type (see lrg_property_grid_register_editor()).
 *
 * Sources
 * -------
 * set_sources() lists the first source's properties and keeps those
 * every other source has with the same name and value type. Rows are
 * keyed by name and reused when name, value type, hint and read-only
 * flag match, so selecting another node with the same schema only
 * refreshes values: no widget is created or removed, focus stays. A
 * value is "mixed" when lrg_property_values_equal() finds a source
 * that differs from the first one.
 *
 * Edits
 * -----
 * Editors talk to their row: begin_edit / edit_value / end_edit (or
 * apply_value for one-shot changes). The row turns these into the
 * grid's edit-begin / edit-value / edit-end signals with a gesture id,
 * and in DIRECT mode writes to every source and, on cancel, writes the
 * values captured at begin back. In DELEGATE mode it only emits, so an
 * application can make one undo command per gesture. Rows ignore their
 * editor while syncing (pushing a value into it), so editors that emit
 * change signals on programmatic updates do not echo them back.
 *
 * Built-in editors
 * ----------------
 * numbers: LrgSpinBox (drag to scrub) inside a private frame that
 * observes the press so a scrub is one gesture and Escape cancels it;
 * booleans: LrgCheckbox; strings: LrgTextInput or LrgTextEditor
 * (multiline) or LrgComboBox (enum-strings); enums: LrgComboBox; flags:
 * a check box per flag; colours: LrgColorPicker in popup mode; vectors:
 * LrgVectorEdit; curves: LrgCurveEdit; gradients: LrgGradientEdit;
 * references: a drop-down (or a label for objects) with pick and clear
 * buttons; arrays: an LrgExpander of element rows with move, remove and
 * add buttons; anything else: a read-only label.
 */

#include "config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_UI

#include <math.h>
#include <string.h>
#include <raylib.h>

#include "lrg-property-grid.h"
#include "lrg-canvas.h"
#include "lrg-checkbox.h"
#include "lrg-color-picker.h"
#include "lrg-combo-box.h"
#include "lrg-curve-edit.h"
#include "lrg-drag.h"
#include "lrg-drop-target.h"
#include "lrg-gradient-edit.h"
#include "lrg-hbox.h"
#include "lrg-label.h"
#include "lrg-spin-box.h"
#include "lrg-style.h"
#include "lrg-text-editor.h"
#include "lrg-text-input.h"
#include "lrg-vbox.h"
#include "lrg-vector-edit.h"
#include "../paint/lrg-paint-icons.h"
#include "../tween/lrg-keyframe-curve.h"
#include "../lrg-log.h"

/* Default width of the label column */
#define DEFAULT_LABEL_WIDTH (120.0f)

/* Largest share of a row the label column takes */
#define LABEL_SHARE_MAX     (0.6f)

/* Vertical padding inside a row and the label's left inset */
#define ROW_PAD_Y           (2.0f)
#define LABEL_INSET         (8.0f)

/* Width of the overridden marker bar */
#define OVERRIDE_BAR        (3.0f)

/* Height of a multi-line string editor */
#define MULTILINE_HEIGHT    (84.0f)

/* Label column of array element rows */
#define ELEMENT_LABEL_WIDTH (34.0f)

/* The em dash shown for mixed values */
#define EM_DASH             "\xe2\x80\x94"

/* ==========================================================================
 * LrgPropertyApplyMode
 * ========================================================================== */

GType
lrg_property_apply_mode_get_type (void)
{
	static gsize type_id = 0;

	if (g_once_init_enter (&type_id))
	{
		static const GEnumValue values[] = {
			{ LRG_PROPERTY_APPLY_DIRECT, "LRG_PROPERTY_APPLY_DIRECT", "direct" },
			{ LRG_PROPERTY_APPLY_DELEGATE, "LRG_PROPERTY_APPLY_DELEGATE", "delegate" },
			{ 0, NULL, NULL }
		};
		GType id = g_enum_register_static (g_intern_static_string ("LrgPropertyApplyMode"), values);

		g_once_init_leave (&type_id, id);
	}
	return type_id;
}

/* ==========================================================================
 * Grid and row structures
 * ========================================================================== */

struct _LrgPropertyGrid
{
	LrgContainer          parent_instance;

	GPtrArray            *sources;       /* LrgPropertySource, owned */
	GArray               *handler_ids;   /* gulong per source */
	GPtrArray            *infos;         /* LrgPropertyInfo of the shown schema */
	GHashTable           *rows;          /* name -> LrgPropertyRow (ref) */
	GHashTable           *sections;      /* group -> LrgExpander (ref) */
	GHashTable           *section_boxes; /* group -> LrgVBox (borrowed) */
	GHashTable           *expanded;      /* group -> GINT_TO_POINTER (0/1) */
	GHashTable           *editors;       /* key -> EditorEntry, per grid */
	LrgTextInput         *filter;        /* owned ref, child while shown */

	gboolean              locked;
	gboolean              show_filter;
	gchar                *filter_text;
	gfloat                label_width;
	LrgPropertyApplyMode  apply_mode;
	guint                 next_gesture;
	gboolean              setting_filter;

	LrgPropertyReferenceFunc ref_func;
	gpointer              ref_data;
	GDestroyNotify        ref_destroy;
};

struct _LrgPropertyRow
{
	LrgContainer       parent_instance;

	LrgPropertyGrid   *grid;          /* not owned */
	LrgPropertyRow    *parent_row;    /* not owned: array element rows */
	gint               element_index;
	LrgPropertyInfo   *info;
	gchar             *name;
	LrgWidget         *editor;        /* child */

	GValue             value;         /* shown value (first source) */
	gboolean           mixed;
	LrgPropertyState   state;
	gboolean           syncing;
	gboolean           shows_mixed;

	/* Gesture */
	guint              gesture_id;
	GPtrArray         *originals;     /* GValue* per source at begin */
	GValue             start;         /* shown value at begin */

	/* Arrays */
	GPtrArray         *elements;      /* element rows (ref) */
	LrgWidget         *element_box;   /* vbox of element items */
	LrgExpander       *array_expander;

	GSimpleActionGroup *actions;
};

G_DEFINE_FINAL_TYPE (LrgPropertyGrid, lrg_property_grid, LRG_TYPE_CONTAINER)
G_DEFINE_FINAL_TYPE (LrgPropertyRow, lrg_property_row, LRG_TYPE_CONTAINER)

enum
{
	PROP_0,
	PROP_LOCKED,
	PROP_SHOW_FILTER,
	PROP_FILTER_TEXT,
	PROP_LABEL_WIDTH,
	PROP_APPLY_MODE,
	N_PROPS
};

enum
{
	SIGNAL_EDIT_BEGIN,
	SIGNAL_EDIT_VALUE,
	SIGNAL_EDIT_END,
	SIGNAL_RESET_REQUESTED,
	SIGNAL_PICK_REQUESTED,
	N_SIGNALS
};

enum
{
	ROW_SIGNAL_SYNC,
	ROW_N_SIGNALS
};

static GParamSpec *properties[N_PROPS];
static guint       signals[N_SIGNALS];
static guint       row_signals[ROW_N_SIGNALS];

static void            row_refresh          (LrgPropertyRow *self);
static LrgPropertyRow *row_new              (LrgPropertyGrid *grid,
                                             LrgPropertyInfo *info);
static LrgWidget      *create_editor        (LrgPropertyGrid *grid,
                                             LrgPropertyRow  *row);
static gboolean        row_is_array         (LrgPropertyRow *row);

/* ==========================================================================
 * Editor registry
 * ========================================================================== */

typedef struct
{
	LrgPropertyEditorFactory factory;
	gpointer                 data;
	GDestroyNotify           destroy;
} EditorEntry;

static void
editor_entry_free (gpointer p)
{
	EditorEntry *e = p;

	if (e->destroy != NULL)
		e->destroy (e->data);
	g_free (e);
}

static GHashTable *
editor_table_new (void)
{
	return g_hash_table_new_full (g_str_hash, g_str_equal, g_free, editor_entry_free);
}

static void
editor_table_set (GHashTable               *table,
                  const gchar              *key,
                  LrgPropertyEditorFactory  factory,
                  gpointer                  data,
                  GDestroyNotify            destroy)
{
	EditorEntry *e = g_new0 (EditorEntry, 1);

	e->factory = factory;
	e->data = data;
	e->destroy = destroy;
	g_hash_table_replace (table, g_strdup (key), e);
}

/* Global registrations; guarded because registration may come from
 * plugin loaders on another thread */
static GMutex      registry_lock;
static GHashTable *global_editors;
static GHashTable *builtin_editors;

static void register_builtins (GHashTable *table);

static GHashTable *
get_builtins (void)
{
	static gsize init = 0;

	if (g_once_init_enter (&init))
	{
		builtin_editors = editor_table_new ();
		register_builtins (builtin_editors);
		g_once_init_leave (&init, 1);
	}
	return builtin_editors;
}

void
lrg_property_grid_register_editor (const gchar              *hint_or_type_name,
                                   LrgPropertyEditorFactory  factory,
                                   gpointer                  user_data,
                                   GDestroyNotify            destroy)
{
	g_return_if_fail (hint_or_type_name != NULL);
	g_return_if_fail (factory != NULL);

	g_mutex_lock (&registry_lock);
	if (global_editors == NULL)
		global_editors = editor_table_new ();
	editor_table_set (global_editors, hint_or_type_name, factory, user_data, destroy);
	g_mutex_unlock (&registry_lock);
}

gboolean
lrg_property_grid_unregister_editor (const gchar *hint_or_type_name)
{
	gboolean removed = FALSE;

	g_return_val_if_fail (hint_or_type_name != NULL, FALSE);

	g_mutex_lock (&registry_lock);
	if (global_editors != NULL)
		removed = g_hash_table_remove (global_editors, hint_or_type_name);
	g_mutex_unlock (&registry_lock);
	return removed;
}

void
lrg_property_grid_add_editor (LrgPropertyGrid          *self,
                              const gchar              *hint_or_type_name,
                              LrgPropertyEditorFactory  factory,
                              gpointer                  user_data,
                              GDestroyNotify            destroy)
{
	g_return_if_fail (LRG_IS_PROPERTY_GRID (self));
	g_return_if_fail (hint_or_type_name != NULL);
	g_return_if_fail (factory != NULL);

	editor_table_set (self->editors, hint_or_type_name, factory, user_data, destroy);
}

/*
 * lookup_factory:
 *
 * The factory for @key: this grid's, else the global one, else the
 * built-in one. Copies the entry so the global lock is not held while
 * the factory runs.
 */
static gboolean
lookup_factory (LrgPropertyGrid *grid,
                const gchar     *key,
                EditorEntry     *out)
{
	EditorEntry *e = NULL;

	if (key == NULL)
		return FALSE;
	if (grid != NULL)
		e = g_hash_table_lookup (grid->editors, key);
	if (e == NULL)
	{
		g_mutex_lock (&registry_lock);
		if (global_editors != NULL)
			e = g_hash_table_lookup (global_editors, key);
		if (e != NULL)
			*out = *e;
		g_mutex_unlock (&registry_lock);
		if (e != NULL)
			return TRUE;
		e = g_hash_table_lookup (get_builtins (), key);
	}
	if (e == NULL)
		return FALSE;
	*out = *e;
	return TRUE;
}

/* ==========================================================================
 * Value helpers
 * ========================================================================== */

/* value_copy_into: @dest becomes a copy of @src (unset first) */
static void
value_copy_into (GValue       *dest,
                 const GValue *src)
{
	if (G_IS_VALUE (dest))
		g_value_unset (dest);
	if (src != NULL && G_IS_VALUE (src))
	{
		g_value_init (dest, G_VALUE_TYPE (src));
		g_value_copy (src, dest);
	}
}

static void
gvalue_free (gpointer p)
{
	GValue *v = p;

	if (G_IS_VALUE (v))
		g_value_unset (v);
	g_free (v);
}

/* convert_value: @value as @type, rounding doubles for integer types */
static gboolean
convert_value (const GValue *value,
               GType         type,
               GValue       *out)
{
	g_value_init (out, type);
	if (G_VALUE_HOLDS (value, type))
	{
		g_value_copy (value, out);
		return TRUE;
	}
	if (G_VALUE_HOLDS_DOUBLE (value) && G_TYPE_FUNDAMENTAL (type) != G_TYPE_FLOAT &&
	    G_TYPE_FUNDAMENTAL (type) != G_TYPE_DOUBLE && g_value_type_transformable (G_TYPE_DOUBLE, type))
	{
		g_auto(GValue) r = G_VALUE_INIT;

		g_value_init (&r, G_TYPE_DOUBLE);
		g_value_set_double (&r, round (g_value_get_double (value)));
		return g_value_transform (&r, out);
	}
	if (g_value_type_transformable (G_VALUE_TYPE (value), type))
		return g_value_transform (value, out);
	g_value_unset (out);
	return FALSE;
}

/* vector_size: number of floats in a GrlVectorN type, 0 otherwise */
static guint
vector_size (GType type)
{
	if (type == GRL_TYPE_VECTOR2)
		return 2;
	if (type == GRL_TYPE_VECTOR3)
		return 3;
	if (type == GRL_TYPE_VECTOR4)
		return 4;
	return 0;
}

/* ==========================================================================
 * Arrays (GStrv and GVariant arrays of basic types)
 * ========================================================================== */

/* variant_element_type: the element GType of a variant element type
 * string such as "d", or G_TYPE_INVALID */
static GType
variant_element_gtype (const GVariantType *vt)
{
	if (g_variant_type_equal (vt, G_VARIANT_TYPE_BOOLEAN))
		return G_TYPE_BOOLEAN;
	if (g_variant_type_equal (vt, G_VARIANT_TYPE_INT32))
		return G_TYPE_INT;
	if (g_variant_type_equal (vt, G_VARIANT_TYPE_UINT32))
		return G_TYPE_UINT;
	if (g_variant_type_equal (vt, G_VARIANT_TYPE_INT64))
		return G_TYPE_INT64;
	if (g_variant_type_equal (vt, G_VARIANT_TYPE_UINT64))
		return G_TYPE_UINT64;
	if (g_variant_type_equal (vt, G_VARIANT_TYPE_DOUBLE))
		return G_TYPE_DOUBLE;
	if (g_variant_type_equal (vt, G_VARIANT_TYPE_STRING))
		return G_TYPE_STRING;
	return G_TYPE_INVALID;
}

/* array_variant_type: the variant array type of an array row, from its
 * pspec, e.g. "ad"; NULL if it is not a supported array */
static const GVariantType *
array_variant_type (LrgPropertyRow *row)
{
	GParamSpec *pspec = lrg_property_info_get_pspec (row->info);

	if (!G_IS_PARAM_SPEC_VARIANT (pspec))
		return NULL;
	if (!g_variant_type_is_array (G_PARAM_SPEC_VARIANT (pspec)->type))
		return NULL;
	if (variant_element_gtype (g_variant_type_element (G_PARAM_SPEC_VARIANT (pspec)->type)) == G_TYPE_INVALID)
		return NULL;
	return G_PARAM_SPEC_VARIANT (pspec)->type;
}

/* array_elements: the elements of @arr as GValues */
static GPtrArray *
array_elements (LrgPropertyRow *row,
                const GValue   *arr)
{
	GPtrArray *out = g_ptr_array_new_with_free_func (gvalue_free);

	if (arr == NULL || !G_IS_VALUE (arr))
		return out;

	if (G_VALUE_HOLDS (arr, G_TYPE_STRV))
	{
		const gchar * const *v = g_value_get_boxed (arr);
		guint                i;

		for (i = 0; v != NULL && v[i] != NULL; i++)
		{
			GValue *e = g_new0 (GValue, 1);

			g_value_init (e, G_TYPE_STRING);
			g_value_set_string (e, v[i]);
			g_ptr_array_add (out, e);
		}
	}
	else if (G_VALUE_HOLDS (arr, G_TYPE_VARIANT) && g_value_get_variant (arr) != NULL)
	{
		GVariant *v = g_value_get_variant (arr);
		gsize     n = g_variant_is_container (v) ? g_variant_n_children (v) : 0;
		gsize     i;

		for (i = 0; i < n; i++)
		{
			g_autoptr(GVariant) child = g_variant_get_child_value (v, i);
			GValue             *e = g_new0 (GValue, 1);

			g_dbus_gvariant_to_gvalue (child, e);
			g_ptr_array_add (out, e);
		}
	}
	(void) row;
	return out;
}

/* array_build: an array value of @row's type from @elements */
static void
array_build (LrgPropertyRow *row,
             GPtrArray      *elements,
             GValue         *out)
{
	GType type = lrg_property_info_get_value_type (row->info);
	guint i;

	g_value_init (out, type);
	if (type == G_TYPE_STRV)
	{
		gchar **v = g_new0 (gchar *, elements->len + 1);

		for (i = 0; i < elements->len; i++)
		{
			GValue *e = g_ptr_array_index (elements, i);

			v[i] = g_strdup (G_VALUE_HOLDS_STRING (e) && g_value_get_string (e) != NULL
			                 ? g_value_get_string (e) : "");
		}
		g_value_take_boxed (out, v);
	}
	else
	{
		const GVariantType *vt = array_variant_type (row);
		GVariantBuilder     b;

		if (vt == NULL)
			return;
		g_variant_builder_init (&b, vt);
		for (i = 0; i < elements->len; i++)
		{
			GValue             *e = g_ptr_array_index (elements, i);
			g_autoptr(GVariant) child = g_dbus_gvalue_to_gvariant (e, g_variant_type_element (vt));

			if (child != NULL)
				g_variant_builder_add_value (&b, child);
		}
		g_value_take_variant (out, g_variant_ref_sink (g_variant_builder_end (&b)));
	}
}

/* element_pspec: a synthetic pspec for elements of @row's array */
static GParamSpec *
element_pspec (LrgPropertyRow *row)
{
	GType               type = lrg_property_info_get_value_type (row->info);
	const GVariantType *vt;

	if (type == G_TYPE_STRV)
		return g_param_spec_string ("item", NULL, NULL, "", G_PARAM_READWRITE);

	vt = array_variant_type (row);
	switch (vt != NULL ? variant_element_gtype (g_variant_type_element (vt)) : G_TYPE_INVALID)
	{
	case G_TYPE_BOOLEAN:
		return g_param_spec_boolean ("item", NULL, NULL, FALSE, G_PARAM_READWRITE);
	case G_TYPE_INT:
		return g_param_spec_int ("item", NULL, NULL, G_MININT, G_MAXINT, 0, G_PARAM_READWRITE);
	case G_TYPE_UINT:
		return g_param_spec_uint ("item", NULL, NULL, 0, G_MAXUINT, 0, G_PARAM_READWRITE);
	case G_TYPE_INT64:
		return g_param_spec_int64 ("item", NULL, NULL, G_MININT64, G_MAXINT64, 0, G_PARAM_READWRITE);
	case G_TYPE_UINT64:
		return g_param_spec_uint64 ("item", NULL, NULL, 0, G_MAXUINT64, 0, G_PARAM_READWRITE);
	case G_TYPE_DOUBLE:
		return g_param_spec_double ("item", NULL, NULL, -G_MAXDOUBLE, G_MAXDOUBLE, 0.0, G_PARAM_READWRITE);
	case G_TYPE_STRING:
		return g_param_spec_string ("item", NULL, NULL, "", G_PARAM_READWRITE);
	default:
		return NULL;
	}
}

/* ==========================================================================
 * Source access
 * ========================================================================== */

/* read_source: @name of source @index into @out (uninitialised) */
static gboolean
read_source (LrgPropertyGrid *grid,
             guint            index,
             const gchar     *name,
             GValue          *out)
{
	if (grid == NULL || index >= grid->sources->len)
		return FALSE;
	return lrg_property_source_get_value (g_ptr_array_index (grid->sources, index), name, out);
}

/* ==========================================================================
 * Mixed veil
 * ========================================================================== */

/* paint_mixed_veil: input-coloured box with a centred em dash */
static void
paint_mixed_veil (LrgWidget    *widget,
                  LrgPaintList *list,
                  gfloat        x,
                  gfloat        y,
                  gfloat        w,
                  gfloat        h)
{
	LrgTheme *theme = lrg_widget_get_theme (widget);
	GrlFont  *font = lrg_theme_get_ui_font (theme);
	gfloat    size = lrg_theme_get_metric (theme, LRG_THEME_METRIC_FONT_SIZE);
	gfloat    tw = 0.0f;
	gfloat    th = 0.0f;

	if (w <= 0.0f || h <= 0.0f)
		return;
	lrg_paint_list_rect (list, x, y, w, h, lrg_theme_get_metric (theme, LRG_THEME_METRIC_CORNER_RADIUS) * 0.5f,
	                     lrg_theme_get_color (theme, LRG_THEME_COLOR_INPUT));
	lrg_paint_measure_text (font, EM_DASH, size, 1.0f, &tw, &th);
	lrg_paint_list_text (list, font, EM_DASH, roundf (x + (w - tw) * 0.5f), roundf (y + (h - th) * 0.5f), size,
	                     1.0f, lrg_theme_get_color (theme, LRG_THEME_COLOR_TEXT_MUTED));
}

/* ==========================================================================
 * Private: a small icon button
 * ========================================================================== */

#define LRG_TYPE_PROPERTY_ICON_BUTTON (lrg_property_icon_button_get_type ())
G_DECLARE_FINAL_TYPE (LrgPropertyIconButton, lrg_property_icon_button, LRG, PROPERTY_ICON_BUTTON, LrgWidget)

/*
 * LrgPropertyIconButton:
 *
 * A square control-coloured button showing one LrgIcon, for the array
 * and reference editors' small actions. Emits "clicked".
 */
struct _LrgPropertyIconButton
{
	LrgWidget parent_instance;
	LrgIcon   icon;
	gboolean  armed;
};

G_DEFINE_FINAL_TYPE (LrgPropertyIconButton, lrg_property_icon_button, LRG_TYPE_WIDGET)

static guint icon_button_clicked;

static void
lrg_property_icon_button_measure (LrgWidget *widget,
                                  gfloat    *preferred_width,
                                  gfloat    *preferred_height)
{
	gfloat ch = lrg_theme_get_metric (lrg_widget_get_theme (widget), LRG_THEME_METRIC_CONTROL_HEIGHT);

	*preferred_width = ch;
	*preferred_height = ch;
}

static void
lrg_property_icon_button_style_defaults (LrgWidget      *widget,
                                         LrgUIPart       part,
                                         LrgWidgetState  state,
                                         LrgPartLook    *look)
{
	LrgTheme *theme = lrg_widget_get_theme (widget);

	if (part == LRG_UI_PART_BOX)
	{
		lrg_part_look_set_background (look, lrg_theme_get_color (theme,
		                              (state & LRG_WIDGET_STATE_PRESSED) ? LRG_THEME_COLOR_CONTROL_PRESSED :
		                              (state & LRG_WIDGET_STATE_HOVERED) ? LRG_THEME_COLOR_CONTROL_HOVER
		                                                                 : LRG_THEME_COLOR_CONTROL));
		lrg_part_look_set_foreground (look, lrg_theme_get_color (theme,
		                              (state & LRG_WIDGET_STATE_HOVERED) ? LRG_THEME_COLOR_TEXT
		                                                                 : LRG_THEME_COLOR_TEXT_MUTED));
		lrg_part_look_set_radius (look, lrg_theme_get_metric (theme, LRG_THEME_METRIC_CORNER_RADIUS));
		if (state & LRG_WIDGET_STATE_DISABLED)
			lrg_part_look_set_opacity (look, 0.45f);
		return;
	}
	LRG_WIDGET_CLASS (lrg_property_icon_button_parent_class)->style_defaults (widget, part, state, look);
}

static void
lrg_property_icon_button_paint (LrgWidget    *widget,
                                LrgPaintList *list)
{
	LrgPropertyIconButton *self = LRG_PROPERTY_ICON_BUTTON (widget);
	LrgPartLook            look;
	gfloat                 x = lrg_widget_get_world_x (widget);
	gfloat                 y = lrg_widget_get_world_y (widget);
	gfloat                 w = lrg_widget_get_width (widget);
	gfloat                 h = lrg_widget_get_height (widget);
	gfloat                 s = MIN (w, h) * 0.6f;

	lrg_widget_resolve_look (widget, LRG_UI_PART_BOX, &look);
	if (look.opacity < 1.0f)
		lrg_paint_list_push_opacity (list, look.opacity);
	lrg_widget_paint_box (widget, list, &look, x, y, w, h);
	lrg_paint_list_icon (list, self->icon, x + (w - s) * 0.5f, y + (h - s) * 0.5f, s, 0.0f, &look.foreground);
	if (look.opacity < 1.0f)
		lrg_paint_list_pop_opacity (list);
}

static gboolean
lrg_property_icon_button_handle_event (LrgWidget        *widget,
                                       const LrgUIEvent *event)
{
	LrgPropertyIconButton *self = LRG_PROPERTY_ICON_BUTTON (widget);

	switch (lrg_ui_event_get_event_type (event))
	{
	case LRG_UI_EVENT_MOUSE_BUTTON_DOWN:
		if (lrg_ui_event_get_button (event) != 0)
			return FALSE;
		self->armed = TRUE;
		return TRUE;

	case LRG_UI_EVENT_MOUSE_BUTTON_UP:
		if (lrg_ui_event_get_button (event) != 0 || !self->armed)
			return FALSE;
		self->armed = FALSE;
		if (lrg_widget_contains_point (widget, lrg_ui_event_get_x (event), lrg_ui_event_get_y (event)))
			g_signal_emit (self, icon_button_clicked, 0);
		return TRUE;

	case LRG_UI_EVENT_ACTIVATE:
		g_signal_emit (self, icon_button_clicked, 0);
		return TRUE;

	default:
		return FALSE;
	}
}

static void
lrg_property_icon_button_class_init (LrgPropertyIconButtonClass *klass)
{
	LrgWidgetClass *widget_class = LRG_WIDGET_CLASS (klass);

	widget_class->measure = lrg_property_icon_button_measure;
	widget_class->style_defaults = lrg_property_icon_button_style_defaults;
	widget_class->paint = lrg_property_icon_button_paint;
	widget_class->handle_event = lrg_property_icon_button_handle_event;

	icon_button_clicked = g_signal_new ("clicked", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
	                                    0, NULL, NULL, NULL, G_TYPE_NONE, 0);
}

static void
lrg_property_icon_button_init (LrgPropertyIconButton *self)
{
	lrg_widget_set_focusable (LRG_WIDGET (self), TRUE);
}

static LrgWidget *
icon_button_new (LrgIcon      icon,
                 const gchar *name)
{
	LrgPropertyIconButton *b = g_object_new (LRG_TYPE_PROPERTY_ICON_BUTTON, NULL);

	b->icon = icon;
	lrg_widget_set_name (LRG_WIDGET (b), name);
	return LRG_WIDGET (b);
}

/* ==========================================================================
 * LrgPropertyRow: gestures
 * ========================================================================== */

guint
lrg_property_row_begin_edit (LrgPropertyRow *self)
{
	LrgPropertyGrid *grid;
	guint            i;

	g_return_val_if_fail (LRG_IS_PROPERTY_ROW (self), 0);

	if (self->syncing)
		return 0;

	/* Array elements edit through their array's row */
	if (self->parent_row != NULL)
	{
		self->gesture_id = lrg_property_row_begin_edit (self->parent_row);
		return self->gesture_id;
	}

	grid = self->grid;
	if (grid == NULL || lrg_property_info_get_read_only (self->info) || grid->sources->len == 0)
		return 0;
	if (self->gesture_id != 0)
		return self->gesture_id;

	/* Capture every source's value, for cancel and multi-edit merging */
	self->gesture_id = grid->next_gesture++;
	g_ptr_array_set_size (self->originals, 0);
	for (i = 0; i < grid->sources->len; i++)
	{
		GValue *v = g_new0 (GValue, 1);

		read_source (grid, i, self->name, v);
		g_ptr_array_add (self->originals, v);
	}
	value_copy_into (&self->start, &self->value);

	g_signal_emit (grid, signals[SIGNAL_EDIT_BEGIN], 0, self->name, self->gesture_id);
	return self->gesture_id;
}

/*
 * merge_for_source:
 *
 * The value an edit means for source @index: vectors take only the
 * components the gesture changed (relative to the shown value at
 * begin); everything else is @edited as is.
 */
static void
merge_for_source (LrgPropertyRow *self,
                  guint           index,
                  const GValue   *edited,
                  GValue         *out)
{
	GType   type = G_VALUE_TYPE (edited);
	guint   n = vector_size (type);
	GValue *orig = index < self->originals->len ? g_ptr_array_index (self->originals, index) : NULL;

	g_value_init (out, type);
	if (n > 0 && orig != NULL && G_VALUE_HOLDS (orig, type) && G_VALUE_HOLDS (&self->start, type) &&
	    g_value_get_boxed (orig) != NULL && g_value_get_boxed (&self->start) != NULL &&
	    g_value_get_boxed (edited) != NULL)
	{
		const gfloat *e = g_value_get_boxed (edited);
		const gfloat *s = g_value_get_boxed (&self->start);
		const gfloat *o = g_value_get_boxed (orig);
		gfloat        m[4];
		guint         i;

		for (i = 0; i < n; i++)
			m[i] = e[i] != s[i] ? e[i] : o[i];
		g_value_set_boxed (out, m);
		return;
	}
	g_value_copy (edited, out);
}

void
lrg_property_row_edit_value (LrgPropertyRow *self,
                             const GValue   *value,
                             gboolean        interactive)
{
	g_auto(GValue)   converted = G_VALUE_INIT;
	LrgPropertyGrid *grid;
	guint            i;

	g_return_if_fail (LRG_IS_PROPERTY_ROW (self));
	g_return_if_fail (G_IS_VALUE (value));

	if (self->syncing)
		return;

	/* Element: rebuild the whole array with this element replaced */
	if (self->parent_row != NULL)
	{
		LrgPropertyRow      *parent = self->parent_row;
		g_autoptr(GPtrArray) elements = NULL;
		g_auto(GValue)       arr = G_VALUE_INIT;
		GValue              *slot;

		if (lrg_property_row_begin_edit (self) == 0)
			return;
		if (!convert_value (value, lrg_property_info_get_value_type (self->info), &converted))
			return;
		elements = array_elements (parent, &parent->value);
		if (self->element_index < 0 || (guint) self->element_index >= elements->len)
			return;
		slot = g_ptr_array_index (elements, self->element_index);
		value_copy_into (slot, &converted);
		value_copy_into (&self->value, &converted);
		array_build (parent, elements, &arr);
		lrg_property_row_edit_value (parent, &arr, interactive);
		return;
	}

	grid = self->grid;
	if (grid == NULL || lrg_property_row_begin_edit (self) == 0)
		return;
	if (!convert_value (value, lrg_property_info_get_value_type (self->info), &converted))
	{
		lrg_warning (LRG_LOG_DOMAIN_UI, "property grid: cannot convert a %s to %s for '%s'",
		             G_VALUE_TYPE_NAME (value), g_type_name (lrg_property_info_get_value_type (self->info)),
		             self->name);
		return;
	}

	/* The row shows the edit at once, whoever applies it */
	value_copy_into (&self->value, &converted);
	self->mixed = FALSE;

	/* DIRECT: write every source (vectors merged per component) */
	if (grid->apply_mode == LRG_PROPERTY_APPLY_DIRECT)
	{
		for (i = 0; i < grid->sources->len; i++)
		{
			g_auto(GValue) per = G_VALUE_INIT;

			merge_for_source (self, i, &converted, &per);
			lrg_property_source_set_value (g_ptr_array_index (grid->sources, i), self->name, &per);
		}
	}

	g_signal_emit (grid, signals[SIGNAL_EDIT_VALUE], 0, self->name, &converted, self->gesture_id, interactive);
}

void
lrg_property_row_end_edit (LrgPropertyRow *self,
                           gboolean        commit)
{
	LrgPropertyGrid *grid;
	guint            id;
	guint            i;

	g_return_if_fail (LRG_IS_PROPERTY_ROW (self));

	if (self->syncing || self->gesture_id == 0)
		return;

	if (self->parent_row != NULL)
	{
		self->gesture_id = 0;
		lrg_property_row_end_edit (self->parent_row, commit);
		return;
	}

	grid = self->grid;
	id = self->gesture_id;

	/* DIRECT cancel: put back what begin captured */
	if (!commit && grid != NULL && grid->apply_mode == LRG_PROPERTY_APPLY_DIRECT)
	{
		for (i = 0; i < self->originals->len && i < grid->sources->len; i++)
		{
			GValue *v = g_ptr_array_index (self->originals, i);

			if (G_IS_VALUE (v))
				lrg_property_source_set_value (g_ptr_array_index (grid->sources, i), self->name, v);
		}
	}

	if (grid != NULL)
		g_signal_emit (grid, signals[SIGNAL_EDIT_END], 0, self->name, id, commit);
	self->gesture_id = 0;
	g_ptr_array_set_size (self->originals, 0);

	/* Reload the editor from the sources (and its elements) */
	row_refresh (self);
}

void
lrg_property_row_apply_value (LrgPropertyRow *self,
                              const GValue   *value)
{
	g_return_if_fail (LRG_IS_PROPERTY_ROW (self));
	g_return_if_fail (G_IS_VALUE (value));

	if (self->syncing || lrg_property_row_begin_edit (self) == 0)
		return;
	lrg_property_row_edit_value (self, value, FALSE);
	lrg_property_row_end_edit (self, TRUE);
}

/* ==========================================================================
 * LrgPropertyRow: refresh and sync
 * ========================================================================== */

/* row_emit_sync: let the editor reload, with edits ignored meanwhile */
static void
row_emit_sync (LrgPropertyRow *self)
{
	self->syncing = TRUE;
	g_signal_emit (self, row_signals[ROW_SIGNAL_SYNC], 0);
	self->syncing = FALSE;
}

/* update_actions: enable context-menu actions for the current state */
static void
update_actions (LrgPropertyRow *self)
{
	gboolean ro = lrg_property_row_get_read_only (self);
	GAction *a;

	if (self->actions == NULL)
		return;
	a = g_action_map_lookup_action (G_ACTION_MAP (self->actions), "reset");
	g_simple_action_set_enabled (G_SIMPLE_ACTION (a), !ro);
	a = g_action_map_lookup_action (G_ACTION_MAP (self->actions), "paste");
	g_simple_action_set_enabled (G_SIMPLE_ACTION (a), !ro);
	a = g_action_map_lookup_action (G_ACTION_MAP (self->actions), "copy");
	g_simple_action_set_enabled (G_SIMPLE_ACTION (a), !self->mixed);
}

/*
 * row_refresh:
 *
 * Reads the value from the first source (or the parent array), works
 * out mixed and state across all sources, and syncs the editor unless
 * a gesture is in flight (the editor already shows the edit).
 */
static void
row_refresh (LrgPropertyRow *self)
{
	LrgPropertyGrid *grid = self->grid;
	guint            i;

	if (self->parent_row != NULL)
	{
		g_autoptr(GPtrArray) elements = array_elements (self->parent_row, &self->parent_row->value);

		if (self->element_index >= 0 && (guint) self->element_index < elements->len)
			value_copy_into (&self->value, g_ptr_array_index (elements, self->element_index));
		self->mixed = self->parent_row->mixed;
	}
	else if (grid != NULL && grid->sources->len > 0)
	{
		g_auto(GValue) first = G_VALUE_INIT;
		gboolean       all_default = TRUE;

		if (read_source (grid, 0, self->name, &first))
			value_copy_into (&self->value, &first);

		/* Mixed: any source that differs from the first */
		self->mixed = FALSE;
		for (i = 1; i < grid->sources->len && G_IS_VALUE (&first); i++)
		{
			g_auto(GValue) other = G_VALUE_INIT;

			if (read_source (grid, i, self->name, &other) && !lrg_property_values_equal (&first, &other))
			{
				self->mixed = TRUE;
				break;
			}
		}

		/* State: markers when any source has them, DEFAULT when all do */
		self->state = LRG_PROPERTY_STATE_NONE;
		for (i = 0; i < grid->sources->len; i++)
		{
			LrgPropertyState s = lrg_property_source_get_state (g_ptr_array_index (grid->sources, i),
			                                                    self->name);

			self->state |= s & (LRG_PROPERTY_STATE_OVERRIDDEN | LRG_PROPERTY_STATE_ANIMATED |
			                    LRG_PROPERTY_STATE_INVALID);
			if (!(s & LRG_PROPERTY_STATE_DEFAULT))
				all_default = FALSE;
		}
		if (all_default)
			self->state |= LRG_PROPERTY_STATE_DEFAULT;
	}

	update_actions (self);
	if (self->gesture_id == 0)
		row_emit_sync (self);
}

/* ==========================================================================
 * LrgPropertyRow: context menu
 * ========================================================================== */

static void
on_action_reset (GSimpleAction *action,
                 GVariant      *parameter,
                 gpointer       user_data)
{
	LrgPropertyRow *self = LRG_PROPERTY_ROW (user_data);

	if (self->grid != NULL && self->parent_row == NULL)
		lrg_property_grid_reset_property (self->grid, self->name);
}

static void
on_action_copy (GSimpleAction *action,
                GVariant      *parameter,
                gpointer       user_data)
{
	LrgPropertyRow *self = LRG_PROPERTY_ROW (user_data);

	if (self->grid != NULL && self->parent_row == NULL)
		lrg_property_grid_copy_value (self->grid, self->name);
}

static void
on_action_paste (GSimpleAction *action,
                 GVariant      *parameter,
                 gpointer       user_data)
{
	LrgPropertyRow *self = LRG_PROPERTY_ROW (user_data);

	if (self->grid != NULL && self->parent_row == NULL)
		lrg_property_grid_paste_value (self->grid, self->name);
}

/* row_menu_model: the shared Reset / Copy / Paste menu */
static GMenuModel *
row_menu_model (void)
{
	static gsize  init = 0;
	static GMenu *menu = NULL;

	if (g_once_init_enter (&init))
	{
		menu = g_menu_new ();
		g_menu_append (menu, "Reset to default", "prop.reset");
		g_menu_append (menu, "Copy value", "prop.copy");
		g_menu_append (menu, "Paste value", "prop.paste");
		g_once_init_leave (&init, 1);
	}
	return G_MENU_MODEL (menu);
}

/* ==========================================================================
 * LrgPropertyRow: widget
 * ========================================================================== */

/* row_label_width: the label column, at most LABEL_SHARE_MAX of the row */
static gfloat
row_label_width (LrgPropertyRow *self)
{
	gfloat want = self->parent_row != NULL ? ELEMENT_LABEL_WIDTH
	              : self->grid != NULL ? self->grid->label_width : DEFAULT_LABEL_WIDTH;

	return MIN (want, lrg_widget_get_width (LRG_WIDGET (self)) * LABEL_SHARE_MAX);
}

static void
lrg_property_row_measure (LrgWidget *widget,
                          gfloat    *preferred_width,
                          gfloat    *preferred_height)
{
	LrgPropertyRow *self = LRG_PROPERTY_ROW (widget);
	gfloat          ch = lrg_theme_get_metric (lrg_widget_get_theme (widget), LRG_THEME_METRIC_CONTROL_HEIGHT);
	gfloat          lw = self->parent_row != NULL ? ELEMENT_LABEL_WIDTH
	                     : self->grid != NULL ? self->grid->label_width : DEFAULT_LABEL_WIDTH;
	gfloat          ew = 0.0f;
	gfloat          eh = 0.0f;

	if (self->editor != NULL && lrg_widget_get_visible (self->editor))
		lrg_widget_get_preferred_size (self->editor, &ew, &eh);
	*preferred_width = lw + ew;
	*preferred_height = MAX (ch, eh) + ROW_PAD_Y * 2.0f;
}

static void
lrg_property_row_layout_children (LrgContainer *container)
{
	LrgPropertyRow *self = LRG_PROPERTY_ROW (container);
	LrgWidget      *widget = LRG_WIDGET (self);
	gfloat          ch = lrg_theme_get_metric (lrg_widget_get_theme (widget), LRG_THEME_METRIC_CONTROL_HEIGHT);
	gfloat          lw = row_label_width (self);
	gfloat          ew = 0.0f;
	gfloat          eh = 0.0f;

	if (self->editor == NULL)
		return;
	lrg_widget_get_preferred_size (self->editor, &ew, &eh);
	lrg_widget_allocate (self->editor, lw, ROW_PAD_Y, MAX (0.0f, lrg_widget_get_width (widget) - lw),
	                     MAX (ch, eh));
}

/* paint_diamond: the animated-key marker */
static void
paint_diamond (LrgPaintList   *list,
               gfloat          cx,
               gfloat          cy,
               gfloat          r,
               const GrlColor *color)
{
	lrg_paint_list_triangle (list, cx, cy - r, cx - r, cy, cx + r, cy, color);
	lrg_paint_list_triangle (list, cx - r, cy, cx, cy + r, cx + r, cy, color);
}

static void
lrg_property_row_paint (LrgWidget    *widget,
                        LrgPaintList *list)
{
	LrgPropertyRow *self = LRG_PROPERTY_ROW (widget);
	LrgTheme       *theme = lrg_widget_get_theme (widget);
	GrlFont        *font = lrg_theme_get_ui_font (theme);
	gfloat          x = lrg_widget_get_world_x (widget);
	gfloat          y = lrg_widget_get_world_y (widget);
	gfloat          w = lrg_widget_get_width (widget);
	gfloat          h = lrg_widget_get_height (widget);
	gfloat          ch = lrg_theme_get_metric (theme, LRG_THEME_METRIC_CONTROL_HEIGHT);
	gfloat          size = lrg_theme_get_metric (theme, LRG_THEME_METRIC_FONT_SIZE);
	gfloat          lw = row_label_width (self);
	gfloat          hover = CLAMP (lrg_widget_get_hover_progress (widget), 0.0f, 1.0f);
	gfloat          th = size;
	gfloat          ly;
	const gchar    *label = lrg_property_info_get_label (self->info);
	g_autofree gchar *element_label = NULL;
	GrlColor        c;
	LrgThemeColor   role;

	/* 1. Hover wash across the row */
	if (hover > 0.001f)
	{
		lrg_paint_color_with_alpha (lrg_theme_get_color (theme, LRG_THEME_COLOR_CONTROL_HOVER), 0.5f * hover, &c);
		lrg_paint_list_rect (list, x, y, w, h, lrg_theme_get_metric (theme, LRG_THEME_METRIC_CORNER_RADIUS) * 0.5f,
		                     &c);
	}

	/* 2. Overridden: accent bar at the left edge */
	if (self->state & LRG_PROPERTY_STATE_OVERRIDDEN)
		lrg_paint_list_rect (list, x, y + ROW_PAD_Y, OVERRIDE_BAR, h - ROW_PAD_Y * 2.0f, OVERRIDE_BAR * 0.5f,
		                     lrg_theme_get_color (theme, LRG_THEME_COLOR_ACCENT));

	/* 3. Label: muted when read-only, danger when invalid, faux bold
	 *    (drawn twice) when overridden, centred on the first line */
	if (self->parent_row != NULL)
		label = element_label = g_strdup_printf ("%d", self->element_index);
	role = (self->state & LRG_PROPERTY_STATE_INVALID) ? LRG_THEME_COLOR_DANGER
	       : lrg_property_row_get_read_only (self) ? LRG_THEME_COLOR_TEXT_MUTED : LRG_THEME_COLOR_TEXT;
	lrg_paint_measure_text (font, label, size, 1.0f, NULL, &th);
	ly = roundf (y + ROW_PAD_Y + (ch - th) * 0.5f);
	lrg_paint_list_push_clip (list, x, y, MAX (0.0f, lw - 4.0f), h);
	lrg_paint_list_text (list, font, label, x + LABEL_INSET, ly, size, 1.0f, lrg_theme_get_color (theme, role));
	if (self->state & LRG_PROPERTY_STATE_OVERRIDDEN)
		lrg_paint_list_text (list, font, label, x + LABEL_INSET + 0.7f, ly, size, 1.0f,
		                     lrg_theme_get_color (theme, role));
	lrg_paint_list_pop_clip (list);

	/* 4. Animated: a key diamond at the end of the label column */
	if (self->state & LRG_PROPERTY_STATE_ANIMATED)
		paint_diamond (list, x + lw - 10.0f, y + ROW_PAD_Y + ch * 0.5f, 4.5f,
		               lrg_theme_get_color (theme, LRG_THEME_COLOR_WARNING));

	/* 5. Editor, then the mixed veil when it cannot show mixed itself */
	if (self->editor != NULL)
	{
		lrg_widget_paint (self->editor, list);

		/* A spin box has no mixed look of its own: cover the number
		 * between its two square step buttons, leaving them usable */
		if (self->mixed && LRG_IS_SPIN_BOX (self->editor) && lrg_widget_get_visible (self->editor))
		{
			gfloat eh = lrg_widget_get_height (self->editor);

			paint_mixed_veil (widget, list, lrg_widget_get_world_x (self->editor) + eh,
			                  lrg_widget_get_world_y (self->editor) + 3.0f,
			                  lrg_widget_get_width (self->editor) - eh * 2.0f, eh - 6.0f);
		}
		else if (self->mixed && !self->shows_mixed && lrg_widget_get_visible (self->editor))
			paint_mixed_veil (widget, list, lrg_widget_get_world_x (self->editor),
			                  lrg_widget_get_world_y (self->editor),
			                  MIN (lrg_widget_get_width (self->editor), 80.0f),
			                  MIN (lrg_widget_get_height (self->editor), ch));
	}
}

static gboolean
lrg_property_row_handle_event (LrgWidget        *widget,
                               const LrgUIEvent *event)
{
	LrgPropertyRow *self = LRG_PROPERTY_ROW (widget);

	/* Escape that no editor handled cancels the row's gesture */
	if (lrg_ui_event_get_event_type (event) == LRG_UI_EVENT_CANCEL && self->gesture_id != 0)
	{
		lrg_property_row_end_edit (self, FALSE);
		return TRUE;
	}
	return FALSE;
}

static void
lrg_property_row_dispose (GObject *object)
{
	LrgPropertyRow *self = LRG_PROPERTY_ROW (object);

	g_clear_pointer (&self->elements, g_ptr_array_unref);
	g_clear_object (&self->actions);
	self->editor = NULL;
	self->element_box = NULL;
	self->array_expander = NULL;
	G_OBJECT_CLASS (lrg_property_row_parent_class)->dispose (object);
}

static void
lrg_property_row_finalize (GObject *object)
{
	LrgPropertyRow *self = LRG_PROPERTY_ROW (object);

	if (G_IS_VALUE (&self->value))
		g_value_unset (&self->value);
	if (G_IS_VALUE (&self->start))
		g_value_unset (&self->start);
	g_ptr_array_unref (self->originals);
	g_clear_pointer (&self->info, lrg_property_info_unref);
	g_free (self->name);
	G_OBJECT_CLASS (lrg_property_row_parent_class)->finalize (object);
}

static void
lrg_property_row_class_init (LrgPropertyRowClass *klass)
{
	GObjectClass      *object_class = G_OBJECT_CLASS (klass);
	LrgWidgetClass    *widget_class = LRG_WIDGET_CLASS (klass);
	LrgContainerClass *container_class = LRG_CONTAINER_CLASS (klass);

	object_class->dispose = lrg_property_row_dispose;
	object_class->finalize = lrg_property_row_finalize;

	widget_class->measure = lrg_property_row_measure;
	widget_class->paint = lrg_property_row_paint;
	widget_class->handle_event = lrg_property_row_handle_event;
	container_class->layout_children = lrg_property_row_layout_children;

	/**
	 * LrgPropertyRow::sync:
	 * @self: the row
	 *
	 * The editor should reload the value (lrg_property_row_get_value(),
	 * lrg_property_row_get_mixed()). Emitted after the row is built and
	 * whenever the grid refreshes it outside a gesture; edits made by
	 * the editor during the emission are ignored.
	 */
	row_signals[ROW_SIGNAL_SYNC] =
		g_signal_new ("sync", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
		              0, NULL, NULL, NULL, G_TYPE_NONE, 0);
}

static void
lrg_property_row_init (LrgPropertyRow *self)
{
	self->element_index = -1;
	self->originals = g_ptr_array_new_with_free_func (gvalue_free);
}

/* row_new: a row for @info, with its editor; not yet synced */
static LrgPropertyRow *
row_new (LrgPropertyGrid *grid,
         LrgPropertyInfo *info)
{
	static const GActionEntry entries[] = {
		{ "reset", on_action_reset, NULL, NULL, NULL, { 0 } },
		{ "copy", on_action_copy, NULL, NULL, NULL, { 0 } },
		{ "paste", on_action_paste, NULL, NULL, NULL, { 0 } }
	};
	LrgPropertyRow *self = g_object_new (LRG_TYPE_PROPERTY_ROW, NULL);

	self->grid = grid;
	self->info = lrg_property_info_ref (info);
	self->name = g_strdup (lrg_property_info_get_name (info));
	lrg_widget_set_name (LRG_WIDGET (self), self->name);

	/* Per-row actions behind the shared context menu */
	self->actions = g_simple_action_group_new ();
	g_action_map_add_action_entries (G_ACTION_MAP (self->actions), entries, G_N_ELEMENTS (entries), self);
	lrg_widget_insert_action_group (LRG_WIDGET (self), "prop", G_ACTION_GROUP (self->actions));
	lrg_widget_set_context_menu (LRG_WIDGET (self), row_menu_model ());
	return self;
}

/* row_set_editor: install the editor made by the factory */
static void
row_set_editor (LrgPropertyRow *self,
                LrgWidget      *editor)
{
	self->editor = editor;
	if (editor == NULL)
		return;
	if (lrg_property_row_get_read_only (self))
		lrg_widget_set_enabled (editor, FALSE);
	lrg_container_add_child (LRG_CONTAINER (self), editor);
	g_object_unref (editor);
}

/* ==========================================================================
 * LrgPropertyRow: public accessors
 * ========================================================================== */

LrgPropertyGrid *
lrg_property_row_get_grid (LrgPropertyRow *self)
{
	g_return_val_if_fail (LRG_IS_PROPERTY_ROW (self), NULL);
	return self->grid;
}

LrgPropertyInfo *
lrg_property_row_get_info (LrgPropertyRow *self)
{
	g_return_val_if_fail (LRG_IS_PROPERTY_ROW (self), NULL);
	return self->info;
}

const gchar *
lrg_property_row_get_name (LrgPropertyRow *self)
{
	g_return_val_if_fail (LRG_IS_PROPERTY_ROW (self), NULL);
	return self->name;
}

const gchar *
lrg_property_row_get_hint (LrgPropertyRow *self)
{
	g_return_val_if_fail (LRG_IS_PROPERTY_ROW (self), NULL);
	return lrg_property_info_get_hint (self->info);
}

LrgWidget *
lrg_property_row_get_editor (LrgPropertyRow *self)
{
	g_return_val_if_fail (LRG_IS_PROPERTY_ROW (self), NULL);
	return self->editor;
}

gboolean
lrg_property_row_get_value (LrgPropertyRow *self,
                            GValue         *value)
{
	g_return_val_if_fail (LRG_IS_PROPERTY_ROW (self), FALSE);
	g_return_val_if_fail (value != NULL && !G_IS_VALUE (value), FALSE);

	if (!G_IS_VALUE (&self->value))
		return FALSE;
	g_value_init (value, G_VALUE_TYPE (&self->value));
	g_value_copy (&self->value, value);
	return TRUE;
}

gboolean
lrg_property_row_get_mixed (LrgPropertyRow *self)
{
	g_return_val_if_fail (LRG_IS_PROPERTY_ROW (self), FALSE);
	return self->mixed;
}

LrgPropertyState
lrg_property_row_get_state (LrgPropertyRow *self)
{
	g_return_val_if_fail (LRG_IS_PROPERTY_ROW (self), LRG_PROPERTY_STATE_NONE);
	return self->state;
}

gboolean
lrg_property_row_get_read_only (LrgPropertyRow *self)
{
	g_return_val_if_fail (LRG_IS_PROPERTY_ROW (self), TRUE);
	if (self->parent_row != NULL)
		return lrg_property_row_get_read_only (self->parent_row);
	return lrg_property_info_get_read_only (self->info);
}

gboolean
lrg_property_row_is_syncing (LrgPropertyRow *self)
{
	g_return_val_if_fail (LRG_IS_PROPERTY_ROW (self), FALSE);
	return self->syncing;
}

gboolean
lrg_property_row_get_editing (LrgPropertyRow *self)
{
	g_return_val_if_fail (LRG_IS_PROPERTY_ROW (self), FALSE);
	return self->gesture_id != 0;
}

void
lrg_property_row_set_shows_mixed (LrgPropertyRow *self,
                                  gboolean        shows_mixed)
{
	g_return_if_fail (LRG_IS_PROPERTY_ROW (self));
	self->shows_mixed = !!shows_mixed;
}

LrgPropertyRow *
lrg_property_row_get_parent_row (LrgPropertyRow *self)
{
	g_return_val_if_fail (LRG_IS_PROPERTY_ROW (self), NULL);
	return self->parent_row;
}

gint
lrg_property_row_get_element_index (LrgPropertyRow *self)
{
	g_return_val_if_fail (LRG_IS_PROPERTY_ROW (self), -1);
	return self->element_index;
}

/* ==========================================================================
 * Arrays: element rows and operations
 * ========================================================================== */

static gboolean
row_is_array (LrgPropertyRow *row)
{
	GType type = lrg_property_info_get_value_type (row->info);

	return type == G_TYPE_STRV || (type == G_TYPE_VARIANT && array_variant_type (row) != NULL);
}

guint
lrg_property_row_get_n_elements (LrgPropertyRow *self)
{
	g_return_val_if_fail (LRG_IS_PROPERTY_ROW (self), 0);
	return self->elements != NULL ? self->elements->len : 0;
}

LrgPropertyRow *
lrg_property_row_get_element_row (LrgPropertyRow *self,
                                  guint           index)
{
	g_return_val_if_fail (LRG_IS_PROPERTY_ROW (self), NULL);

	if (self->elements == NULL || index >= self->elements->len)
		return NULL;
	return g_ptr_array_index (self->elements, index);
}

/* apply_elements: one-shot edit of the array with @elements */
static void
apply_elements (LrgPropertyRow *self,
                GPtrArray      *elements)
{
	g_auto(GValue) arr = G_VALUE_INIT;

	array_build (self, elements, &arr);
	if (G_IS_VALUE (&arr))
		lrg_property_row_apply_value (self, &arr);
}

gboolean
lrg_property_row_add_element (LrgPropertyRow *self)
{
	g_autoptr(GPtrArray) elements = NULL;
	GParamSpec          *pspec;
	GValue              *e;

	g_return_val_if_fail (LRG_IS_PROPERTY_ROW (self), FALSE);

	if (!row_is_array (self) || lrg_property_row_get_read_only (self))
		return FALSE;
	pspec = element_pspec (self);
	if (pspec == NULL)
		return FALSE;
	g_param_spec_ref_sink (pspec);

	/* The element type's default, appended */
	elements = array_elements (self, &self->value);
	e = g_new0 (GValue, 1);
	g_value_init (e, pspec->value_type);
	g_param_value_set_default (pspec, e);
	g_ptr_array_add (elements, e);
	g_param_spec_unref (pspec);
	apply_elements (self, elements);
	return TRUE;
}

gboolean
lrg_property_row_remove_element (LrgPropertyRow *self,
                                 guint           index)
{
	g_autoptr(GPtrArray) elements = NULL;

	g_return_val_if_fail (LRG_IS_PROPERTY_ROW (self), FALSE);

	if (!row_is_array (self) || lrg_property_row_get_read_only (self))
		return FALSE;
	elements = array_elements (self, &self->value);
	if (index >= elements->len)
		return FALSE;
	g_ptr_array_remove_index (elements, index);
	apply_elements (self, elements);
	return TRUE;
}

gboolean
lrg_property_row_move_element (LrgPropertyRow *self,
                               guint           from,
                               guint           to)
{
	g_autoptr(GPtrArray) elements = NULL;
	gpointer             e;

	g_return_val_if_fail (LRG_IS_PROPERTY_ROW (self), FALSE);

	if (!row_is_array (self) || lrg_property_row_get_read_only (self))
		return FALSE;
	elements = array_elements (self, &self->value);
	if (from >= elements->len || to >= elements->len || from == to)
		return FALSE;
	e = g_ptr_array_steal_index (elements, from);
	g_ptr_array_insert (elements, (gint) to, e);
	apply_elements (self, elements);
	return TRUE;
}

/* ==========================================================================
 * Built-in editors
 * ========================================================================== */

/* ---- numbers ------------------------------------------------------------ */

/* pspec_range: range and default step of a numeric pspec */
static void
pspec_range (GParamSpec *pspec,
             gdouble    *min,
             gdouble    *max,
             gboolean   *integer)
{
	*integer = TRUE;
	*min = -G_MAXDOUBLE;
	*max = G_MAXDOUBLE;

	if (G_IS_PARAM_SPEC_INT (pspec))
	{
		*min = G_PARAM_SPEC_INT (pspec)->minimum;
		*max = G_PARAM_SPEC_INT (pspec)->maximum;
	}
	else if (G_IS_PARAM_SPEC_UINT (pspec))
	{
		*min = G_PARAM_SPEC_UINT (pspec)->minimum;
		*max = G_PARAM_SPEC_UINT (pspec)->maximum;
	}
	else if (G_IS_PARAM_SPEC_CHAR (pspec))
	{
		*min = G_PARAM_SPEC_CHAR (pspec)->minimum;
		*max = G_PARAM_SPEC_CHAR (pspec)->maximum;
	}
	else if (G_IS_PARAM_SPEC_UCHAR (pspec))
	{
		*min = G_PARAM_SPEC_UCHAR (pspec)->minimum;
		*max = G_PARAM_SPEC_UCHAR (pspec)->maximum;
	}
	else if (G_IS_PARAM_SPEC_LONG (pspec))
	{
		*min = (gdouble) G_PARAM_SPEC_LONG (pspec)->minimum;
		*max = (gdouble) G_PARAM_SPEC_LONG (pspec)->maximum;
	}
	else if (G_IS_PARAM_SPEC_ULONG (pspec))
	{
		*min = (gdouble) G_PARAM_SPEC_ULONG (pspec)->minimum;
		*max = (gdouble) G_PARAM_SPEC_ULONG (pspec)->maximum;
	}
	else if (G_IS_PARAM_SPEC_INT64 (pspec))
	{
		*min = (gdouble) G_PARAM_SPEC_INT64 (pspec)->minimum;
		*max = (gdouble) G_PARAM_SPEC_INT64 (pspec)->maximum;
	}
	else if (G_IS_PARAM_SPEC_UINT64 (pspec))
	{
		*min = (gdouble) G_PARAM_SPEC_UINT64 (pspec)->minimum;
		*max = (gdouble) G_PARAM_SPEC_UINT64 (pspec)->maximum;
	}
	else if (G_IS_PARAM_SPEC_FLOAT (pspec))
	{
		*integer = FALSE;
		*min = G_PARAM_SPEC_FLOAT (pspec)->minimum;
		*max = G_PARAM_SPEC_FLOAT (pspec)->maximum;
	}
	else if (G_IS_PARAM_SPEC_DOUBLE (pspec))
	{
		*integer = FALSE;
		*min = G_PARAM_SPEC_DOUBLE (pspec)->minimum;
		*max = G_PARAM_SPEC_DOUBLE (pspec)->maximum;
	}
}

/* value_as_double: a numeric GValue as a double */
static gdouble
value_as_double (const GValue *value)
{
	g_auto(GValue) d = G_VALUE_INIT;

	if (value == NULL || !G_IS_VALUE (value))
		return 0.0;
	g_value_init (&d, G_TYPE_DOUBLE);
	if (!g_value_transform (value, &d))
		return 0.0;
	return g_value_get_double (&d);
}

/*
 * SpinEdit:
 *
 * Per spin box state of a numeric row, attached to the spin box. The
 * spin box announces its press gestures itself (LrgSpinBox::scrub-begin
 * .. LrgSpinBox::scrub-end, also for a click or a held step button) and
 * handles CANCEL (Escape) by putting the value back and ending the
 * gesture with commit FALSE. The row's gesture is opened lazily on the
 * first change inside a press, so a press that changes nothing records
 * nothing; changes outside a press (wheel, arrow keys) are one-shot.
 */
typedef struct
{
	LrgPropertyRow *row;       /* not owned (the row owns the spin box) */
	gdouble         scale;     /* spin value = property value * scale */
	gboolean        pressed;   /* between scrub-begin and scrub-end */
	gboolean        began;     /* the row's gesture is open */
} SpinEdit;

#define SPIN_EDIT_KEY "lrg-property-spin-edit"

static SpinEdit *
spin_edit (LrgSpinBox *spin)
{
	return g_object_get_data (G_OBJECT (spin), SPIN_EDIT_KEY);
}

static void
spin_sync (LrgSpinBox     *spin,
           LrgPropertyRow *row)
{
	SpinEdit      *edit = spin_edit (spin);
	g_auto(GValue) v = G_VALUE_INIT;

	if (lrg_property_row_get_value (row, &v))
		lrg_spin_box_set_value (spin, value_as_double (&v) * edit->scale);
}

/* on_spin_scrub_begin: a press starts; the row gesture opens on the
 * first change */
static void
on_spin_scrub_begin (LrgSpinBox *spin,
                     gpointer    user_data)
{
	SpinEdit *edit = user_data;

	edit->pressed = TRUE;
	edit->began = FALSE;
}

/* on_spin_scrub_end: release or focus loss commits, CANCEL does not
 * (the spin box has already put its value back) */
static void
on_spin_scrub_end (LrgSpinBox *spin,
                   gboolean    commit,
                   gpointer    user_data)
{
	SpinEdit *edit = user_data;
	gboolean  began = edit->began;

	edit->pressed = FALSE;
	edit->began = FALSE;
	if (began && edit->row != NULL)
		lrg_property_row_end_edit (edit->row, commit);
}

static void
on_spin_value_changed (LrgSpinBox *spin,
                       gpointer    user_data)
{
	SpinEdit      *edit = user_data;
	g_auto(GValue) v = G_VALUE_INIT;

	if (edit->row == NULL || lrg_property_row_is_syncing (edit->row))
		return;

	/* The cancel restore arrives after the spin box closed its gesture
	 * but before scrub-end: the row's end_edit (commit FALSE) restores
	 * the sources, so the restored value is not written as an edit */
	if (edit->pressed && !lrg_spin_box_get_scrubbing (spin))
		return;

	g_value_init (&v, G_TYPE_DOUBLE);
	g_value_set_double (&v, lrg_spin_box_get_value (spin) / edit->scale);

	/* Inside a press: one gesture; otherwise one-shot */
	if (edit->pressed)
	{
		if (!edit->began)
		{
			edit->began = TRUE;
			lrg_property_row_begin_edit (edit->row);
		}
		lrg_property_row_edit_value (edit->row, &v, TRUE);
	}
	else
		lrg_property_row_apply_value (edit->row, &v);
}

/* make_spin: a spin box for a numeric row; @scale converts for display */
static LrgWidget *
make_spin (LrgPropertyRow *row,
           gdouble         scale)
{
	LrgSpinBox  *spin = lrg_spin_box_new ();
	SpinEdit    *edit = g_new0 (SpinEdit, 1);
	GParamSpec  *pspec = lrg_property_info_get_pspec (row->info);
	const gchar *hint = lrg_property_info_get_hint (row->info);
	gdouble      min;
	gdouble      max;
	gdouble      step = 0.0;
	gboolean     integer;

	edit->row = row;
	edit->scale = scale;
	g_object_set_data_full (G_OBJECT (spin), SPIN_EDIT_KEY, edit, g_free);
	pspec_range (pspec, &min, &max, &integer);
	lrg_property_hint_parse_range (hint, &min, &max, &step);
	if (step <= 0.0)
		step = integer ? 1.0 : (scale != 1.0 ? 1.0 : 0.1);

	lrg_spin_box_set_range (spin, min * scale, max * scale);
	lrg_spin_box_set_step (spin, step);
	lrg_spin_box_set_digits (spin, integer ? 0 : (step >= 1.0 ? 1 : step >= 0.1 ? 2 : 3));

	/* The row paints the mixed veil over the number only (between the
	 * step buttons), see lrg_property_row_paint() */
	lrg_property_row_set_shows_mixed (row, TRUE);
	g_signal_connect (spin, "scrub-begin", G_CALLBACK (on_spin_scrub_begin), edit);
	g_signal_connect (spin, "scrub-end", G_CALLBACK (on_spin_scrub_end), edit);
	g_signal_connect (spin, "value-changed", G_CALLBACK (on_spin_value_changed), edit);
	g_signal_connect_object (row, "sync", G_CALLBACK (spin_sync), spin, G_CONNECT_SWAPPED);
	return LRG_WIDGET (spin);
}

static LrgWidget *
factory_number (LrgPropertyRow *row,
                gpointer        user_data)
{
	return make_spin (row, 1.0);
}

static LrgWidget *
factory_angle (LrgPropertyRow *row,
               gpointer        user_data)
{
	/* Radians shown in degrees, unless the hint says degrees already */
	return make_spin (row, g_strcmp0 (lrg_property_info_get_hint (row->info), "angle:degrees") == 0
	                       ? 1.0 : 180.0 / G_PI);
}

/* ---- booleans ----------------------------------------------------------- */

static void
check_sync (LrgCheckbox    *check,
            LrgPropertyRow *row)
{
	g_auto(GValue) v = G_VALUE_INIT;

	/* Mixed: unchecked underneath, with the indeterminate dash. A click
	 * clears it and checks the box, which writes TRUE to every source */
	if (lrg_property_row_get_value (row, &v) && G_VALUE_HOLDS_BOOLEAN (&v))
		lrg_checkbox_set_checked (check, !row->mixed && g_value_get_boolean (&v));
	lrg_checkbox_set_inconsistent (check, row->mixed);
}

static void
on_check_toggled (LrgCheckbox *check,
                  gboolean     checked,
                  gpointer     user_data)
{
	LrgPropertyRow *row = LRG_PROPERTY_ROW (user_data);
	g_auto(GValue)  v = G_VALUE_INIT;

	g_value_init (&v, G_TYPE_BOOLEAN);
	g_value_set_boolean (&v, checked);
	lrg_property_row_apply_value (row, &v);
}

static LrgWidget *
factory_bool (LrgPropertyRow *row,
              gpointer        user_data)
{
	LrgCheckbox *check = lrg_checkbox_new ("");

	lrg_widget_set_halign (LRG_WIDGET (check), LRG_ALIGN_START);
	lrg_property_row_set_shows_mixed (row, TRUE);
	g_signal_connect_object (check, "toggled", G_CALLBACK (on_check_toggled), row, 0);
	g_signal_connect_object (row, "sync", G_CALLBACK (check_sync), check, G_CONNECT_SWAPPED);
	return LRG_WIDGET (check);
}

/* ---- strings ------------------------------------------------------------ */

static void
text_sync (LrgTextInput   *input,
           LrgPropertyRow *row)
{
	g_auto(GValue) v = G_VALUE_INIT;
	const gchar   *s = "";

	if (lrg_property_row_get_value (row, &v) && G_VALUE_HOLDS_STRING (&v) && g_value_get_string (&v) != NULL)
		s = g_value_get_string (&v);
	lrg_text_input_set_placeholder (input, row->mixed ? EM_DASH : "");
	if (row->mixed)
		s = "";
	if (g_strcmp0 (lrg_text_input_get_text (input), s) != 0)
		lrg_text_input_set_text (input, s);
}

static void
on_text_changed (LrgTextInput *input,
                 gpointer      user_data)
{
	LrgPropertyRow *row = LRG_PROPERTY_ROW (user_data);
	g_auto(GValue)  v = G_VALUE_INIT;

	/* Typing is one gesture until Enter or focus leaves */
	g_value_init (&v, G_TYPE_STRING);
	g_value_set_string (&v, lrg_text_input_get_text (input));
	lrg_property_row_edit_value (row, &v, TRUE);
}

static void
on_text_submitted (LrgTextInput *input,
                   gpointer      user_data)
{
	lrg_property_row_end_edit (LRG_PROPERTY_ROW (user_data), TRUE);
}

/* on_editor_focus_changed: leaving a typing editor commits its gesture */
static void
on_editor_focus_changed (LrgWidget      *editor,
                         LrgWidgetState  previous,
                         gpointer        user_data)
{
	if ((previous & LRG_WIDGET_STATE_FOCUSED) && !(lrg_widget_get_state_flags (editor) & LRG_WIDGET_STATE_FOCUSED))
		lrg_property_row_end_edit (LRG_PROPERTY_ROW (user_data), TRUE);
}

static void
multiline_sync (LrgTextEditor  *editor,
                LrgPropertyRow *row)
{
	g_auto(GValue)    v = G_VALUE_INIT;
	g_autofree gchar *now = lrg_text_editor_get_text (editor);
	const gchar      *s = "";

	if (lrg_property_row_get_value (row, &v) && G_VALUE_HOLDS_STRING (&v) && g_value_get_string (&v) != NULL)
		s = g_value_get_string (&v);
	if (row->mixed)
		s = "";
	if (g_strcmp0 (now, s) != 0)
		lrg_text_editor_set_text (editor, s);
}

static void
on_multiline_changed (LrgTextEditor *editor,
                      gpointer       user_data)
{
	LrgPropertyRow   *row = LRG_PROPERTY_ROW (user_data);
	g_autofree gchar *text = lrg_text_editor_get_text (editor);
	g_auto(GValue)    v = G_VALUE_INIT;

	g_value_init (&v, G_TYPE_STRING);
	g_value_set_string (&v, text);
	lrg_property_row_edit_value (row, &v, TRUE);
}

static LrgWidget *factory_enum_strings (LrgPropertyRow *row, gpointer user_data);
static LrgWidget *factory_color (LrgPropertyRow *row, gpointer user_data);
static LrgWidget *factory_reference (LrgPropertyRow *row, gpointer user_data);

static LrgWidget *
factory_string (LrgPropertyRow *row,
                gpointer        user_data)
{
	const gchar      *hint = lrg_property_info_get_hint (row->info);
	g_autofree gchar *kind = NULL;
	LrgTextInput     *input;

	/* String hints that pick a different editor */
	lrg_property_hint_split (hint, &kind, NULL);
	if (g_strcmp0 (kind, "enum-strings") == 0)
		return factory_enum_strings (row, user_data);
	if (g_strcmp0 (kind, "color") == 0)
		return factory_color (row, user_data);
	if (g_strcmp0 (kind, "node-ref") == 0 || g_strcmp0 (kind, "asset") == 0 || g_strcmp0 (kind, "object-ref") == 0)
		return factory_reference (row, user_data);
	if (g_strcmp0 (kind, "multiline") == 0)
	{
		LrgTextEditor *editor = lrg_text_editor_new ();

		lrg_widget_set_min_size (LRG_WIDGET (editor), 0.0f, MULTILINE_HEIGHT);
		lrg_widget_set_max_size (LRG_WIDGET (editor), G_MAXFLOAT, MULTILINE_HEIGHT);
		g_signal_connect_object (editor, "changed", G_CALLBACK (on_multiline_changed), row, 0);
		g_signal_connect_object (editor, "state-changed", G_CALLBACK (on_editor_focus_changed), row, 0);
		g_signal_connect_object (row, "sync", G_CALLBACK (multiline_sync), editor, G_CONNECT_SWAPPED);
		return LRG_WIDGET (editor);
	}

	input = lrg_text_input_new ();
	lrg_property_row_set_shows_mixed (row, TRUE);
	g_signal_connect_object (input, "text-changed", G_CALLBACK (on_text_changed), row, 0);
	g_signal_connect_object (input, "submitted", G_CALLBACK (on_text_submitted), row, 0);
	g_signal_connect_object (input, "state-changed", G_CALLBACK (on_editor_focus_changed), row, 0);
	g_signal_connect_object (row, "sync", G_CALLBACK (text_sync), input, G_CONNECT_SWAPPED);
	return LRG_WIDGET (input);
}

/* ---- enums and enum-strings --------------------------------------------- */

static void
enum_sync (LrgComboBox    *combo,
           LrgPropertyRow *row)
{
	g_auto(GValue)        v = G_VALUE_INIT;
	g_autoptr(GEnumClass) klass = NULL;
	guint                 i;

	lrg_combo_box_set_placeholder (combo, row->mixed ? EM_DASH : "");
	if (row->mixed || !lrg_property_row_get_value (row, &v) || !G_VALUE_HOLDS_ENUM (&v))
	{
		lrg_combo_box_set_selected (combo, -1);
		return;
	}
	klass = g_type_class_ref (G_VALUE_TYPE (&v));
	for (i = 0; i < klass->n_values; i++)
		if (klass->values[i].value == g_value_get_enum (&v))
			lrg_combo_box_set_selected (combo, (gint) i);
}

static void
on_enum_changed (LrgComboBox *combo,
                 gpointer     user_data)
{
	LrgPropertyRow       *row = LRG_PROPERTY_ROW (user_data);
	GType                 type = lrg_property_info_get_value_type (row->info);
	g_autoptr(GEnumClass) klass = g_type_class_ref (type);
	g_auto(GValue)        v = G_VALUE_INIT;
	gint                  sel = lrg_combo_box_get_selected (combo);

	if (sel < 0 || (guint) sel >= klass->n_values)
		return;
	g_value_init (&v, type);
	g_value_set_enum (&v, klass->values[sel].value);
	lrg_property_row_apply_value (row, &v);
}

static LrgWidget *
factory_enum (LrgPropertyRow *row,
              gpointer        user_data)
{
	LrgComboBox          *combo = lrg_combo_box_new ();
	g_autoptr(GEnumClass) klass = g_type_class_ref (lrg_property_info_get_value_type (row->info));
	guint                 i;

	for (i = 0; i < klass->n_values; i++)
		lrg_combo_box_append (combo, klass->values[i].value_nick);
	lrg_property_row_set_shows_mixed (row, TRUE);
	g_signal_connect_object (combo, "changed", G_CALLBACK (on_enum_changed), row, 0);
	g_signal_connect_object (row, "sync", G_CALLBACK (enum_sync), combo, G_CONNECT_SWAPPED);
	return LRG_WIDGET (combo);
}

static void
enum_strings_sync (LrgComboBox    *combo,
                   LrgPropertyRow *row)
{
	g_auto(GValue) v = G_VALUE_INIT;
	guint          i;

	lrg_combo_box_set_placeholder (combo, row->mixed ? EM_DASH : "");
	lrg_combo_box_set_selected (combo, -1);
	if (row->mixed || !lrg_property_row_get_value (row, &v) || !G_VALUE_HOLDS_STRING (&v))
		return;
	for (i = 0; i < lrg_combo_box_get_n_items (combo); i++)
		if (g_strcmp0 (lrg_combo_box_get_item_text (combo, i), g_value_get_string (&v)) == 0)
			lrg_combo_box_set_selected (combo, (gint) i);
}

static void
on_enum_strings_changed (LrgComboBox *combo,
                         gpointer     user_data)
{
	g_auto(GValue) v = G_VALUE_INIT;
	const gchar   *text = lrg_combo_box_get_selected_text (combo);

	if (text == NULL)
		return;
	g_value_init (&v, G_TYPE_STRING);
	g_value_set_string (&v, text);
	lrg_property_row_apply_value (LRG_PROPERTY_ROW (user_data), &v);
}

static LrgWidget *
factory_enum_strings (LrgPropertyRow *row,
                      gpointer        user_data)
{
	LrgComboBox      *combo = lrg_combo_box_new ();
	g_autofree gchar *arg = NULL;
	g_auto(GStrv)     items = NULL;
	guint             i;

	lrg_property_hint_split (lrg_property_info_get_hint (row->info), NULL, &arg);
	items = g_strsplit (arg != NULL ? arg : "", ",", -1);
	for (i = 0; items[i] != NULL; i++)
		if (*g_strstrip (items[i]) != '\0')
			lrg_combo_box_append (combo, items[i]);
	lrg_property_row_set_shows_mixed (row, TRUE);
	g_signal_connect_object (combo, "changed", G_CALLBACK (on_enum_strings_changed), row, 0);
	g_signal_connect_object (row, "sync", G_CALLBACK (enum_strings_sync), combo, G_CONNECT_SWAPPED);
	return LRG_WIDGET (combo);
}

/* ---- flags -------------------------------------------------------------- */

static void
flags_sync (LrgWidget      *box,
            LrgPropertyRow *row)
{
	g_auto(GValue) v = G_VALUE_INIT;
	guint          bits = 0;
	GList         *l;

	if (lrg_property_row_get_value (row, &v) && G_VALUE_HOLDS_FLAGS (&v))
		bits = g_value_get_flags (&v);
	for (l = lrg_container_get_children (LRG_CONTAINER (box)); l != NULL; l = l->next)
	{
		guint bit = GPOINTER_TO_UINT (g_object_get_data (G_OBJECT (l->data), "lrg-flag-bit"));

		lrg_checkbox_set_checked (LRG_CHECKBOX (l->data), (bits & bit) == bit);
	}
}

static void
on_flag_toggled (LrgCheckbox *check,
                 gboolean     checked,
                 gpointer     user_data)
{
	LrgPropertyRow *row = LRG_PROPERTY_ROW (user_data);
	g_auto(GValue)  v = G_VALUE_INIT;
	g_auto(GValue)  nv = G_VALUE_INIT;
	guint           bit = GPOINTER_TO_UINT (g_object_get_data (G_OBJECT (check), "lrg-flag-bit"));
	guint           bits = 0;

	if (lrg_property_row_get_value (row, &v) && G_VALUE_HOLDS_FLAGS (&v))
		bits = g_value_get_flags (&v);
	bits = checked ? (bits | bit) : (bits & ~bit);
	g_value_init (&nv, lrg_property_info_get_value_type (row->info));
	g_value_set_flags (&nv, bits);
	lrg_property_row_apply_value (row, &nv);
}

static LrgWidget *
factory_flags (LrgPropertyRow *row,
               gpointer        user_data)
{
	LrgVBox               *box = lrg_vbox_new ();
	g_autoptr(GFlagsClass) klass = g_type_class_ref (lrg_property_info_get_value_type (row->info));
	guint                  i;

	/* One check box per single-bit flag value */
	for (i = 0; i < klass->n_values; i++)
	{
		GFlagsValue *fv = &klass->values[i];
		LrgCheckbox *check;

		if (fv->value == 0 || (fv->value & (fv->value - 1)) != 0)
			continue;
		check = lrg_checkbox_new (fv->value_nick);
		g_object_set_data (G_OBJECT (check), "lrg-flag-bit", GUINT_TO_POINTER (fv->value));
		g_signal_connect_object (check, "toggled", G_CALLBACK (on_flag_toggled), row, 0);
		lrg_container_add_child (LRG_CONTAINER (box), LRG_WIDGET (check));
		g_object_unref (check);
	}
	g_signal_connect_object (row, "sync", G_CALLBACK (flags_sync), box, G_CONNECT_SWAPPED);
	return LRG_WIDGET (box);
}

/* ---- colours ------------------------------------------------------------ */

/* color_from_value: GrlColor, "#hex" string or 0xRRGGBBAA uint */
static gboolean
color_from_value (const GValue *v,
                  GrlColor     *c)
{
	if (v == NULL || !G_IS_VALUE (v))
		return FALSE;
	if (G_VALUE_HOLDS (v, GRL_TYPE_COLOR) && g_value_get_boxed (v) != NULL)
	{
		*c = *(const GrlColor *) g_value_get_boxed (v);
		return TRUE;
	}
	if (G_VALUE_HOLDS_STRING (v) && g_value_get_string (v) != NULL)
		return lrg_color_parse (g_value_get_string (v), c);
	if (G_VALUE_HOLDS_UINT (v))
	{
		guint u = g_value_get_uint (v);

		c->r = (guint8) (u >> 24);
		c->g = (guint8) (u >> 16);
		c->b = (guint8) (u >> 8);
		c->a = (guint8) u;
		return TRUE;
	}
	return FALSE;
}

/* color_to_value: @c as a value of @type */
static void
color_to_value (const GrlColor *c,
                GType           type,
                GValue         *v)
{
	g_value_init (v, type);
	if (type == GRL_TYPE_COLOR)
		g_value_set_boxed (v, c);
	else if (type == G_TYPE_STRING)
		g_value_take_string (v, lrg_color_to_string (c));
	else if (type == G_TYPE_UINT)
		g_value_set_uint (v, ((guint) c->r << 24) | ((guint) c->g << 16) | ((guint) c->b << 8) | (guint) c->a);
}

static void
color_sync (LrgColorPicker *picker,
            LrgPropertyRow *row)
{
	g_auto(GValue) v = G_VALUE_INIT;
	GrlColor       c;

	if (lrg_property_row_get_value (row, &v) && color_from_value (&v, &c))
		lrg_color_picker_set_color (picker, &c);
}

static void
on_color_changed (LrgColorPicker *picker,
                  gpointer        user_data)
{
	LrgPropertyRow *row = LRG_PROPERTY_ROW (user_data);
	g_auto(GValue)  v = G_VALUE_INIT;
	GrlColor        c;

	lrg_color_picker_get_color (picker, &c);
	color_to_value (&c, lrg_property_info_get_value_type (row->info), &v);

	/* The open popup is one gesture, closed by on_color_state_changed */
	if (lrg_color_picker_get_popup_open (picker))
		lrg_property_row_edit_value (row, &v, TRUE);
	else
		lrg_property_row_apply_value (row, &v);
}

static void
on_color_state_changed (LrgWidget      *picker,
                        LrgWidgetState  previous,
                        gpointer        user_data)
{
	if ((previous & LRG_WIDGET_STATE_OPEN) && !(lrg_widget_get_state_flags (picker) & LRG_WIDGET_STATE_OPEN))
		lrg_property_row_end_edit (LRG_PROPERTY_ROW (user_data), TRUE);
}

static LrgWidget *
factory_color (LrgPropertyRow *row,
               gpointer        user_data)
{
	LrgColorPicker *picker = lrg_color_picker_new ();

	lrg_color_picker_set_popup_mode (picker, TRUE);
	lrg_color_picker_set_show_alpha (picker, TRUE);
	lrg_widget_set_halign (LRG_WIDGET (picker), LRG_ALIGN_START);
	g_signal_connect_object (picker, "color-changed", G_CALLBACK (on_color_changed), row, 0);
	g_signal_connect_object (picker, "state-changed", G_CALLBACK (on_color_state_changed), row, 0);
	g_signal_connect_object (row, "sync", G_CALLBACK (color_sync), picker, G_CONNECT_SWAPPED);
	return LRG_WIDGET (picker);
}

/* ---- vectors ------------------------------------------------------------ */

static void
vector_sync (LrgVectorEdit  *edit,
             LrgPropertyRow *row)
{
	GType          type = lrg_property_info_get_value_type (row->info);
	guint          n = vector_size (type);
	g_auto(GValue) v = G_VALUE_INIT;
	guint          i;
	guint          s;

	if (!lrg_property_row_get_value (row, &v) || g_value_get_boxed (&v) == NULL)
		return;
	lrg_vector_edit_set_values (edit, g_value_get_boxed (&v), n);

	/* Mixed per axis: compare each source's component with the first */
	for (i = 0; i < n; i++)
		lrg_vector_edit_set_axis_mixed (edit, i, FALSE);
	for (s = 1; row->grid != NULL && row->parent_row == NULL && s < row->grid->sources->len; s++)
	{
		g_auto(GValue) o = G_VALUE_INIT;
		const gfloat  *a = g_value_get_boxed (&v);
		const gfloat  *b;

		if (!read_source (row->grid, s, row->name, &o) || !G_VALUE_HOLDS (&o, type) ||
		    (b = g_value_get_boxed (&o)) == NULL)
			continue;
		for (i = 0; i < n; i++)
			if (a[i] != b[i])
				lrg_vector_edit_set_axis_mixed (edit, i, TRUE);
	}
}

static void
on_vector_begin (LrgVectorEdit *edit,
                 gpointer       user_data)
{
	lrg_property_row_begin_edit (LRG_PROPERTY_ROW (user_data));
}

static void
on_vector_changed (LrgVectorEdit *edit,
                   gpointer       user_data)
{
	LrgPropertyRow *row = LRG_PROPERTY_ROW (user_data);
	g_auto(GValue)  v = G_VALUE_INIT;
	gfloat          f[LRG_VECTOR_EDIT_MAX_COMPONENTS];

	lrg_vector_edit_get_values (edit, f);
	g_value_init (&v, lrg_property_info_get_value_type (row->info));
	g_value_set_boxed (&v, f);
	lrg_property_row_edit_value (row, &v, TRUE);
}

static void
on_vector_end (LrgVectorEdit *edit,
               gboolean       commit,
               gpointer       user_data)
{
	lrg_property_row_end_edit (LRG_PROPERTY_ROW (user_data), commit);
}

static LrgWidget *
factory_vector (LrgPropertyRow *row,
                gpointer        user_data)
{
	guint             n = vector_size (lrg_property_info_get_value_type (row->info));
	LrgVectorEdit    *edit = lrg_vector_edit_new (MAX (n, 1u));
	g_autofree gchar *arg = NULL;
	gdouble           min;
	gdouble           max;
	gdouble           step;

	/* "vector3:uniform" shows the uniform lock (scales) */
	lrg_property_hint_split (lrg_property_info_get_hint (row->info), NULL, &arg);
	lrg_vector_edit_set_show_uniform (edit, g_strcmp0 (arg, "uniform") == 0);
	if (lrg_property_hint_parse_range (lrg_property_info_get_hint (row->info), &min, &max, &step))
	{
		lrg_vector_edit_set_range (edit, min, max);
		if (step > 0.0)
			lrg_vector_edit_set_step (edit, step);
	}
	lrg_property_row_set_shows_mixed (row, TRUE);
	g_signal_connect_object (edit, "edit-begin", G_CALLBACK (on_vector_begin), row, 0);
	g_signal_connect_object (edit, "value-changed", G_CALLBACK (on_vector_changed), row, 0);
	g_signal_connect_object (edit, "edit-end", G_CALLBACK (on_vector_end), row, 0);
	g_signal_connect_object (row, "sync", G_CALLBACK (vector_sync), edit, G_CONNECT_SWAPPED);
	return LRG_WIDGET (edit);
}

/* ---- gradients ---------------------------------------------------------- */

static void
gradient_sync (LrgGradientEdit *edit,
               LrgPropertyRow  *row)
{
	g_auto(GValue) v = G_VALUE_INIT;

	if (lrg_property_row_get_value (row, &v) && G_VALUE_HOLDS (&v, LRG_TYPE_GRADIENT_STOPS))
		lrg_gradient_edit_set_stops (edit, g_value_get_boxed (&v));
}

static void
on_gradient_begin (LrgGradientEdit *edit,
                   gpointer         user_data)
{
	lrg_property_row_begin_edit (LRG_PROPERTY_ROW (user_data));
}

static void
on_gradient_changed (LrgGradientEdit *edit,
                     gpointer         user_data)
{
	g_auto(GValue) v = G_VALUE_INIT;

	g_value_init (&v, LRG_TYPE_GRADIENT_STOPS);
	g_value_set_boxed (&v, lrg_gradient_edit_get_stops (edit));
	lrg_property_row_edit_value (LRG_PROPERTY_ROW (user_data), &v, TRUE);
}

static void
on_gradient_end (LrgGradientEdit *edit,
                 gboolean         commit,
                 gpointer         user_data)
{
	lrg_property_row_end_edit (LRG_PROPERTY_ROW (user_data), commit);
}

static LrgWidget *
factory_gradient (LrgPropertyRow *row,
                  gpointer        user_data)
{
	LrgGradientEdit *edit = lrg_gradient_edit_new ();

	g_signal_connect_object (edit, "edit-begin", G_CALLBACK (on_gradient_begin), row, 0);
	g_signal_connect_object (edit, "stops-changed", G_CALLBACK (on_gradient_changed), row, 0);
	g_signal_connect_object (edit, "edit-end", G_CALLBACK (on_gradient_end), row, 0);
	g_signal_connect_object (row, "sync", G_CALLBACK (gradient_sync), edit, G_CONNECT_SWAPPED);
	return LRG_WIDGET (edit);
}

/* ---- curves ------------------------------------------------------------- */

static void
curve_sync (LrgCurveEdit   *edit,
            LrgPropertyRow *row)
{
	g_auto(GValue) v = G_VALUE_INIT;

	if (lrg_property_row_get_value (row, &v) && G_VALUE_HOLDS (&v, LRG_TYPE_KEYFRAME_CURVE))
		lrg_curve_edit_set_curve (edit, g_value_get_object (&v));
}

static void
on_curve_begin (LrgCurveEdit *edit,
                gpointer      user_data)
{
	lrg_property_row_begin_edit (LRG_PROPERTY_ROW (user_data));
}

static void
on_curve_changed (LrgCurveEdit *edit,
                  gpointer      user_data)
{
	LrgPropertyRow             *row = LRG_PROPERTY_ROW (user_data);
	g_autoptr(LrgKeyframeCurve) copy = lrg_keyframe_curve_copy (lrg_curve_edit_get_curve (edit));
	g_auto(GValue)              v = G_VALUE_INIT;

	/* Every edit hands the sources a fresh snapshot */
	g_value_init (&v, lrg_property_info_get_value_type (row->info));
	g_value_set_object (&v, copy);
	lrg_property_row_edit_value (row, &v, TRUE);
}

static void
on_curve_end (LrgCurveEdit *edit,
              gboolean      commit,
              gpointer      user_data)
{
	lrg_property_row_end_edit (LRG_PROPERTY_ROW (user_data), commit);
}

static LrgWidget *
factory_curve (LrgPropertyRow *row,
               gpointer        user_data)
{
	LrgCurveEdit *edit = lrg_curve_edit_new ();
	gdouble       min;
	gdouble       max;

	if (lrg_property_hint_parse_range (lrg_property_info_get_hint (row->info), &min, &max, NULL))
		lrg_curve_edit_set_value_range (edit, (gfloat) min, (gfloat) max);
	g_signal_connect_object (edit, "edit-begin", G_CALLBACK (on_curve_begin), row, 0);
	g_signal_connect_object (edit, "curve-changed", G_CALLBACK (on_curve_changed), row, 0);
	g_signal_connect_object (edit, "edit-end", G_CALLBACK (on_curve_end), row, 0);
	g_signal_connect_object (row, "sync", G_CALLBACK (curve_sync), edit, G_CONNECT_SWAPPED);
	return LRG_WIDGET (edit);
}

/* ---- references --------------------------------------------------------- */

/*
 * The reference editor is an hbox: a drop-down of candidates for
 * string references (node GUIDs, asset paths), or a label naming the
 * object for object references, then a pick button that emits
 * pick-requested and a clear button.
 */
typedef struct
{
	LrgPropertyRow *row;
	LrgComboBox    *combo;   /* string references */
	LrgLabel       *label;   /* object references */
} RefEditor;

static void
ref_editor_free (gpointer p)
{
	g_free (p);
}

/* ref_hint: the hint, defaulting to object-ref for object values */
static const gchar *
ref_hint (LrgPropertyRow *row)
{
	const gchar *hint = lrg_property_info_get_hint (row->info);

	return hint != NULL ? hint : "object-ref";
}

static void
ref_sync (LrgWidget      *box,
          LrgPropertyRow *row)
{
	RefEditor     *ref = g_object_get_data (G_OBJECT (box), "lrg-ref-editor");
	g_auto(GValue) v = G_VALUE_INIT;
	gboolean       have = lrg_property_row_get_value (row, &v);

	if (ref->label != NULL)
	{
		GObject *obj = have && G_VALUE_HOLDS_OBJECT (&v) ? g_value_get_object (&v) : NULL;

		lrg_label_set_text (ref->label, row->mixed ? EM_DASH : obj != NULL ? G_OBJECT_TYPE_NAME (obj) : "None");
		return;
	}

	/* Refill the candidates, keeping the current value in the list */
	{
		const gchar *current = have && G_VALUE_HOLDS_STRING (&v) ? g_value_get_string (&v) : NULL;
		g_auto(GStrv) items = NULL;
		gboolean      found = FALSE;
		guint         i;

		if (row->grid != NULL && row->grid->ref_func != NULL)
			items = row->grid->ref_func (row->grid, row->name, ref_hint (row), row->grid->ref_data);
		lrg_combo_box_remove_all (ref->combo);
		for (i = 0; items != NULL && items[i] != NULL; i++)
		{
			lrg_combo_box_append (ref->combo, items[i]);
			if (g_strcmp0 (items[i], current) == 0)
				found = TRUE;
		}
		if (!found && current != NULL && *current != '\0')
			lrg_combo_box_append (ref->combo, current);
		lrg_combo_box_set_placeholder (ref->combo, row->mixed ? EM_DASH : "None");
		lrg_combo_box_set_selected (ref->combo, -1);
		for (i = 0; !row->mixed && current != NULL && i < lrg_combo_box_get_n_items (ref->combo); i++)
			if (g_strcmp0 (lrg_combo_box_get_item_text (ref->combo, i), current) == 0)
				lrg_combo_box_set_selected (ref->combo, (gint) i);
	}
}

static void
on_ref_combo_changed (LrgComboBox *combo,
                      gpointer     user_data)
{
	RefEditor     *ref = user_data;
	g_auto(GValue) v = G_VALUE_INIT;
	const gchar   *text = lrg_combo_box_get_selected_text (combo);

	if (text == NULL)
		return;
	g_value_init (&v, G_TYPE_STRING);
	g_value_set_string (&v, text);
	lrg_property_row_apply_value (ref->row, &v);
}

static void
on_ref_pick (LrgWidget *button,
             gpointer   user_data)
{
	RefEditor *ref = user_data;

	if (ref->row->grid != NULL)
		g_signal_emit (ref->row->grid, signals[SIGNAL_PICK_REQUESTED], 0, ref->row->name, ref_hint (ref->row));
}

static void
on_ref_clear (LrgWidget *button,
              gpointer   user_data)
{
	RefEditor     *ref = user_data;
	g_auto(GValue) v = G_VALUE_INIT;

	g_value_init (&v, lrg_property_info_get_value_type (ref->row->info));
	lrg_property_row_apply_value (ref->row, &v);
}

/* ref_payload_fits: whether a dragged payload can become this row's value */
static gboolean
ref_payload_fits (RefEditor      *ref,
                  LrgDragPayload *payload)
{
	const GValue     *v = lrg_drag_payload_get_value (payload);
	GType             type = lrg_property_info_get_value_type (ref->row->info);
	g_autofree gchar *arg = NULL;

	if (v == NULL || !G_IS_VALUE (v) || lrg_property_row_get_read_only (ref->row))
		return FALSE;

	/* object-ref:GType narrows objects to that type */
	lrg_property_hint_split (ref_hint (ref->row), NULL, &arg);
	if (G_VALUE_HOLDS_OBJECT (v) && arg != NULL && g_value_get_object (v) != NULL)
	{
		GType want = g_type_from_name (arg);

		if (want != G_TYPE_INVALID && !g_type_is_a (G_OBJECT_TYPE (g_value_get_object (v)), want))
			return FALSE;
	}
	return g_value_type_compatible (G_VALUE_TYPE (v), type) || g_value_type_transformable (G_VALUE_TYPE (v), type);
}

static gboolean
on_ref_accept (LrgDropTarget  *target,
               LrgDragPayload *payload,
               gpointer        user_data)
{
	return ref_payload_fits (user_data, payload);
}

static gboolean
on_ref_drop (LrgDropTarget  *target,
             LrgDragPayload *payload,
             gfloat          x,
             gfloat          y,
             gpointer        user_data)
{
	RefEditor *ref = user_data;

	if (ref->row->grid == NULL || !ref_payload_fits (ref, payload))
		return FALSE;
	return lrg_property_grid_drop_reference (ref->row->grid, ref->row->name,
	                                         lrg_drag_payload_get_content_type (payload),
	                                         lrg_drag_payload_get_value (payload));
}

static LrgWidget *
factory_reference (LrgPropertyRow *row,
                   gpointer        user_data)
{
	LrgHBox   *box = lrg_hbox_new ();
	RefEditor *ref = g_new0 (RefEditor, 1);
	LrgWidget *pick;
	LrgWidget *clear;

	ref->row = row;
	lrg_container_set_spacing (LRG_CONTAINER (box), 4.0f);
	g_object_set_data_full (G_OBJECT (box), "lrg-ref-editor", ref, ref_editor_free);

	if (G_TYPE_IS_OBJECT (lrg_property_info_get_value_type (row->info)) ||
	    G_TYPE_IS_INTERFACE (lrg_property_info_get_value_type (row->info)))
	{
		ref->label = lrg_label_new ("None");
		lrg_widget_set_fill_weight (LRG_WIDGET (ref->label), 1.0f);
		lrg_widget_set_valign (LRG_WIDGET (ref->label), LRG_ALIGN_CENTER);
		lrg_container_add_child (LRG_CONTAINER (box), LRG_WIDGET (ref->label));
		g_object_unref (ref->label);
	}
	else
	{
		ref->combo = lrg_combo_box_new ();
		lrg_widget_set_fill_weight (LRG_WIDGET (ref->combo), 1.0f);
		g_signal_connect (ref->combo, "changed", G_CALLBACK (on_ref_combo_changed), ref);
		lrg_container_add_child (LRG_CONTAINER (box), LRG_WIDGET (ref->combo));
		g_object_unref (ref->combo);
	}

	pick = icon_button_new (LRG_ICON_SEARCH, "pick");
	clear = icon_button_new (LRG_ICON_CLOSE, "clear");
	g_signal_connect (pick, "clicked", G_CALLBACK (on_ref_pick), ref);
	g_signal_connect (clear, "clicked", G_CALLBACK (on_ref_clear), ref);
	lrg_container_add_child (LRG_CONTAINER (box), pick);
	lrg_container_add_child (LRG_CONTAINER (box), clear);
	g_object_unref (pick);
	g_object_unref (clear);

	/* Drop hook: nodes onto node/object references, assets onto assets */
	{
		g_autofree gchar   *kind = NULL;
		const gchar        *types[2] = { NULL, NULL };
		LrgDropTarget      *drop;

		lrg_property_hint_split (ref_hint (row), &kind, NULL);
		types[0] = g_strcmp0 (kind, "asset") == 0 ? "application/x-lrg-asset" : "application/x-lrg-node";
		drop = lrg_drop_target_new (types, LRG_DRAG_ACTION_COPY | LRG_DRAG_ACTION_LINK);
		g_signal_connect (drop, "accept", G_CALLBACK (on_ref_accept), ref);
		g_signal_connect (drop, "drop", G_CALLBACK (on_ref_drop), ref);
		lrg_widget_add_drop_target (LRG_WIDGET (box), drop);
	}

	lrg_property_row_set_shows_mixed (row, TRUE);
	g_signal_connect_object (row, "sync", G_CALLBACK (ref_sync), box, G_CONNECT_SWAPPED);
	return LRG_WIDGET (box);
}

/* ---- arrays ------------------------------------------------------------- */

typedef struct
{
	LrgPropertyRow *array;
	guint           index;
} ElementAction;

static void
on_element_up (LrgWidget *button,
               gpointer   user_data)
{
	ElementAction *a = user_data;

	if (a->index > 0)
		lrg_property_row_move_element (a->array, a->index, a->index - 1);
}

static void
on_element_down (LrgWidget *button,
                 gpointer   user_data)
{
	ElementAction *a = user_data;

	lrg_property_row_move_element (a->array, a->index, a->index + 1);
}

static void
on_element_remove (LrgWidget *button,
                   gpointer   user_data)
{
	ElementAction *a = user_data;

	lrg_property_row_remove_element (a->array, a->index);
}

static void
on_element_add (LrgWidget *button,
                gpointer   user_data)
{
	lrg_property_row_add_element (LRG_PROPERTY_ROW (user_data));
}

/* element_button: a small icon button acting on element @index */
static LrgWidget *
element_button (LrgPropertyRow *array,
                guint           index,
                LrgIcon         icon,
                const gchar    *name,
                GCallback       callback)
{
	LrgWidget     *b = icon_button_new (icon, name);
	ElementAction *a = g_new0 (ElementAction, 1);

	a->array = array;
	a->index = index;
	g_object_set_data_full (G_OBJECT (b), "lrg-element-action", a, g_free);
	g_signal_connect (b, "clicked", callback, a);
	return b;
}

/* rebuild_elements: one item (row + buttons) per element */
static void
rebuild_elements (LrgPropertyRow *self,
                  guint           n)
{
	g_autoptr(LrgPropertyInfo) info = NULL;
	GParamSpec                *pspec;
	g_autofree gchar          *inner = NULL;
	guint                      i;

	lrg_container_remove_all (LRG_CONTAINER (self->element_box));
	g_ptr_array_set_size (self->elements, 0);

	/* Elements share one synthetic info: the element pspec and the
	 * hint after "array:" */
	pspec = element_pspec (self);
	if (pspec == NULL)
		return;
	info = lrg_property_info_new (pspec);
	lrg_property_hint_split (lrg_property_info_get_hint (self->info), NULL, &inner);
	lrg_property_info_set_hint (info, inner);
	lrg_property_info_set_read_only (info, lrg_property_row_get_read_only (self));

	for (i = 0; i < n; i++)
	{
		LrgHBox          *item = lrg_hbox_new ();
		LrgPropertyRow   *er = row_new (self->grid, info);
		LrgWidget        *editor;
		g_autofree gchar *name = g_strdup_printf ("%s[%u]", self->name, i);

		/* Element row: edits route to this array row */
		er->parent_row = self;
		er->element_index = (gint) i;
		g_free (er->name);
		er->name = g_strdup (name);
		lrg_widget_set_name (LRG_WIDGET (er), name);
		lrg_widget_set_context_menu (LRG_WIDGET (er), NULL);
		editor = create_editor (self->grid, er);
		row_set_editor (er, editor);
		lrg_widget_set_fill_weight (LRG_WIDGET (er), 1.0f);
		g_ptr_array_add (self->elements, g_object_ref (er));

		lrg_container_set_spacing (LRG_CONTAINER (item), 2.0f);
		lrg_container_add_child (LRG_CONTAINER (item), LRG_WIDGET (er));
		g_object_unref (er);
		{
			LrgWidget *up = element_button (self, i, LRG_ICON_CHEVRON_UP, "up", G_CALLBACK (on_element_up));
			LrgWidget *down = element_button (self, i, LRG_ICON_CHEVRON_DOWN, "down", G_CALLBACK (on_element_down));
			LrgWidget *rm = element_button (self, i, LRG_ICON_TRASH, "remove", G_CALLBACK (on_element_remove));

			lrg_widget_set_enabled (up, i > 0 && !lrg_property_row_get_read_only (self));
			lrg_widget_set_enabled (down, i + 1 < n && !lrg_property_row_get_read_only (self));
			lrg_widget_set_enabled (rm, !lrg_property_row_get_read_only (self));
			lrg_container_add_child (LRG_CONTAINER (item), up);
			lrg_container_add_child (LRG_CONTAINER (item), down);
			lrg_container_add_child (LRG_CONTAINER (item), rm);
			g_object_unref (up);
			g_object_unref (down);
			g_object_unref (rm);
		}
		lrg_container_add_child (LRG_CONTAINER (self->element_box), LRG_WIDGET (item));
		g_object_unref (item);
	}

	/* The add button closes the list */
	{
		LrgWidget *add = icon_button_new (LRG_ICON_PLUS, "add");

		lrg_widget_set_halign (add, LRG_ALIGN_START);
		lrg_widget_set_enabled (add, !lrg_property_row_get_read_only (self));
		g_signal_connect (add, "clicked", G_CALLBACK (on_element_add), self);
		lrg_container_add_child (LRG_CONTAINER (self->element_box), add);
		g_object_unref (add);
	}
	g_param_spec_unref (g_param_spec_ref_sink (pspec));
}

static void
array_sync (LrgExpander    *expander,
            LrgPropertyRow *row)
{
	g_autoptr(GPtrArray) elements = array_elements (row, &row->value);
	g_autofree gchar    *title = NULL;
	guint                i;

	title = row->mixed ? g_strdup (EM_DASH)
	        : g_strdup_printf (elements->len == 1 ? "%u item" : "%u items", elements->len);
	lrg_expander_set_title (expander, title);

	/* Same count: reuse the element rows; otherwise rebuild them */
	if (elements->len != row->elements->len ||
	    lrg_container_get_child_count (LRG_CONTAINER (row->element_box)) == 0)
		rebuild_elements (row, elements->len);
	for (i = 0; i < row->elements->len; i++)
	{
		LrgPropertyRow *er = g_ptr_array_index (row->elements, i);

		er->gesture_id = 0;
		row_refresh (er);
	}
}

static LrgWidget *
factory_array (LrgPropertyRow *row,
               gpointer        user_data)
{
	LrgExpander *expander;
	LrgVBox     *box;

	if (!row_is_array (row))
		return NULL;
	expander = lrg_expander_new ("0 items");
	box = lrg_vbox_new ();
	lrg_container_set_spacing (LRG_CONTAINER (box), 2.0f);
	lrg_expander_set_child (expander, LRG_WIDGET (box));
	g_object_unref (box);
	lrg_expander_set_expanded (expander, TRUE);

	row->array_expander = expander;
	row->element_box = LRG_WIDGET (box);
	row->elements = g_ptr_array_new_with_free_func (g_object_unref);
	lrg_property_row_set_shows_mixed (row, TRUE);
	g_signal_connect_object (row, "sync", G_CALLBACK (array_sync), expander, G_CONNECT_SWAPPED);
	return LRG_WIDGET (expander);
}

/* ---- fallback ----------------------------------------------------------- */

static void
fallback_sync (LrgLabel       *label,
               LrgPropertyRow *row)
{
	g_auto(GValue)    v = G_VALUE_INIT;
	g_autofree gchar *text = NULL;

	if (lrg_property_row_get_value (row, &v))
		text = lrg_property_value_to_string (&v);
	lrg_label_set_text (label, row->mixed ? EM_DASH : text != NULL ? text : "");
}

static LrgWidget *
factory_fallback (LrgPropertyRow *row,
                  gpointer        user_data)
{
	LrgLabel *label = lrg_label_new ("");

	lrg_widget_set_valign (LRG_WIDGET (label), LRG_ALIGN_CENTER);
	lrg_property_row_set_shows_mixed (row, TRUE);
	g_signal_connect_object (row, "sync", G_CALLBACK (fallback_sync), label, G_CONNECT_SWAPPED);
	return LRG_WIDGET (label);
}

/* ---- the built-in table ------------------------------------------------- */

static void
register_builtins (GHashTable *table)
{
	static const gchar *numbers[] = {
		"gchar", "guchar", "gint", "guint", "glong", "gulong", "gint64", "guint64", "gfloat", "gdouble"
	};
	guint i;

	for (i = 0; i < G_N_ELEMENTS (numbers); i++)
		editor_table_set (table, numbers[i], factory_number, NULL, NULL);
	editor_table_set (table, "range", factory_number, NULL, NULL);
	editor_table_set (table, "angle", factory_angle, NULL, NULL);
	editor_table_set (table, "gboolean", factory_bool, NULL, NULL);
	editor_table_set (table, "gchararray", factory_string, NULL, NULL);
	editor_table_set (table, "multiline", factory_string, NULL, NULL);
	editor_table_set (table, "enum-strings", factory_enum_strings, NULL, NULL);
	editor_table_set (table, "GEnum", factory_enum, NULL, NULL);
	editor_table_set (table, "GFlags", factory_flags, NULL, NULL);
	editor_table_set (table, "color", factory_color, NULL, NULL);
	editor_table_set (table, "GrlColor", factory_color, NULL, NULL);
	editor_table_set (table, "vector2", factory_vector, NULL, NULL);
	editor_table_set (table, "vector3", factory_vector, NULL, NULL);
	editor_table_set (table, "vector4", factory_vector, NULL, NULL);
	editor_table_set (table, "GrlVector2", factory_vector, NULL, NULL);
	editor_table_set (table, "GrlVector3", factory_vector, NULL, NULL);
	editor_table_set (table, "GrlVector4", factory_vector, NULL, NULL);
	editor_table_set (table, "gradient", factory_gradient, NULL, NULL);
	editor_table_set (table, "LrgGradientStops", factory_gradient, NULL, NULL);
	editor_table_set (table, "curve", factory_curve, NULL, NULL);
	editor_table_set (table, "LrgKeyframeCurve", factory_curve, NULL, NULL);
	editor_table_set (table, "node-ref", factory_reference, NULL, NULL);
	editor_table_set (table, "object-ref", factory_reference, NULL, NULL);
	editor_table_set (table, "asset", factory_reference, NULL, NULL);
	editor_table_set (table, "GObject", factory_reference, NULL, NULL);
	editor_table_set (table, "array", factory_array, NULL, NULL);
	editor_table_set (table, "GStrv", factory_array, NULL, NULL);
	editor_table_set (table, "GVariant", factory_array, NULL, NULL);
}

/*
 * hint_fits:
 *
 * Whether a hint's built-in editor can edit @type. Hints written for
 * another value type (a "color" hint on a double) fall through to the
 * type's editor instead of producing a broken one.
 */
static gboolean
hint_fits (const gchar *kind,
           GType        type)
{
	GType f = G_TYPE_FUNDAMENTAL (type);

	if (g_strcmp0 (kind, "color") == 0)
		return type == GRL_TYPE_COLOR || type == G_TYPE_STRING || type == G_TYPE_UINT;
	if (g_str_has_prefix (kind, "vector"))
		return vector_size (type) > 0;
	if (g_strcmp0 (kind, "angle") == 0 || g_strcmp0 (kind, "range") == 0)
		return f == G_TYPE_FLOAT || f == G_TYPE_DOUBLE || f == G_TYPE_INT || f == G_TYPE_UINT ||
		       f == G_TYPE_INT64 || f == G_TYPE_UINT64 || f == G_TYPE_LONG || f == G_TYPE_ULONG ||
		       f == G_TYPE_CHAR || f == G_TYPE_UCHAR || vector_size (type) > 0 ||
		       g_type_is_a (type, LRG_TYPE_KEYFRAME_CURVE);
	if (g_strcmp0 (kind, "multiline") == 0 || g_strcmp0 (kind, "enum-strings") == 0)
		return type == G_TYPE_STRING;
	if (g_strcmp0 (kind, "curve") == 0)
		return g_type_is_a (type, LRG_TYPE_KEYFRAME_CURVE);
	if (g_strcmp0 (kind, "gradient") == 0)
		return type == LRG_TYPE_GRADIENT_STOPS;
	if (g_strcmp0 (kind, "node-ref") == 0 || g_strcmp0 (kind, "asset") == 0 || g_strcmp0 (kind, "object-ref") == 0)
		return type == G_TYPE_STRING || f == G_TYPE_OBJECT || f == G_TYPE_INTERFACE;
	if (g_strcmp0 (kind, "array") == 0)
		return type == G_TYPE_STRV || type == G_TYPE_VARIANT;
	return TRUE;
}

/*
 * create_editor:
 *
 * Looks up the factory for @row (full hint, hint kind, then the value
 * type and its ancestors; per-grid, global, built-in at each key) and
 * makes the editor. Falls back to a read-only label.
 */
static LrgWidget *
create_editor (LrgPropertyGrid *grid,
               LrgPropertyRow  *row)
{
	const gchar      *hint = lrg_property_info_get_hint (row->info);
	GType             type = lrg_property_info_get_value_type (row->info);
	g_autofree gchar *kind = NULL;
	EditorEntry       e;
	LrgWidget        *editor = NULL;
	GType             t;

	lrg_property_hint_split (hint, &kind, NULL);

	/* 1. The hint, then its kind (built-ins only when they fit) */
	if (hint != NULL && lookup_factory (grid, hint, &e) &&
	    (e.factory != NULL) && (hint_fits (kind, type) || !g_hash_table_contains (get_builtins (), hint)))
		editor = e.factory (row, e.data);
	if (editor == NULL && kind != NULL && g_strcmp0 (kind, hint) != 0 && lookup_factory (grid, kind, &e) &&
	    hint_fits (kind, type))
		editor = e.factory (row, e.data);

	/* 2. The value type up to its fundamental type */
	for (t = type; editor == NULL && t != G_TYPE_INVALID; t = g_type_parent (t))
		if (lookup_factory (grid, g_type_name (t), &e))
			editor = e.factory (row, e.data);

	/* 3. Show it at least */
	if (editor == NULL)
		editor = factory_fallback (row, NULL);
	return editor;
}

/* ==========================================================================
 * LrgPropertyGrid: schema and layout
 * ========================================================================== */

/* info_dup: a private copy of @info the grid may change */
static LrgPropertyInfo *
info_dup (LrgPropertyInfo *info)
{
	LrgPropertyInfo *copy = lrg_property_info_new (lrg_property_info_get_pspec (info));

	lrg_property_info_set_label (copy, lrg_property_info_get_label (info));
	lrg_property_info_set_group (copy, lrg_property_info_get_group (info));
	lrg_property_info_set_hint (copy, lrg_property_info_get_hint (info));
	lrg_property_info_set_tooltip (copy, lrg_property_info_get_tooltip (info));
	lrg_property_info_set_read_only (copy, lrg_property_info_get_read_only (info));
	return copy;
}

/*
 * compute_schema:
 *
 * The first source's properties that every other source also has
 * with the same value type; read-only when any source says so.
 */
static GPtrArray *
compute_schema (LrgPropertyGrid *self)
{
	GPtrArray *out = g_ptr_array_new_with_free_func ((GDestroyNotify) lrg_property_info_unref);
	g_autoptr(GPtrArray) others = g_ptr_array_new_with_free_func ((GDestroyNotify) g_hash_table_unref);
	g_autoptr(GPtrArray) first = NULL;
	guint                i;
	guint                s;

	if (self->sources->len == 0)
		return out;

	first = lrg_property_source_list_properties (g_ptr_array_index (self->sources, 0));
	for (s = 1; s < self->sources->len; s++)
	{
		GPtrArray  *list = lrg_property_source_list_properties (g_ptr_array_index (self->sources, s));
		GHashTable *by_name = g_hash_table_new_full (g_str_hash, g_str_equal, NULL,
		                                             (GDestroyNotify) lrg_property_info_unref);

		for (i = 0; list != NULL && i < list->len; i++)
		{
			LrgPropertyInfo *info = g_ptr_array_index (list, i);

			g_hash_table_replace (by_name, (gpointer) lrg_property_info_get_name (info),
			                      lrg_property_info_ref (info));
		}
		if (list != NULL)
			g_ptr_array_unref (list);
		g_ptr_array_add (others, by_name);
	}

	for (i = 0; first != NULL && i < first->len; i++)
	{
		LrgPropertyInfo *info = g_ptr_array_index (first, i);
		gboolean         keep = TRUE;
		gboolean         read_only = lrg_property_info_get_read_only (info);
		LrgPropertyInfo *copy;

		for (s = 0; s < others->len && keep; s++)
		{
			LrgPropertyInfo *o = g_hash_table_lookup (g_ptr_array_index (others, s),
			                                          lrg_property_info_get_name (info));

			if (o == NULL || lrg_property_info_get_value_type (o) != lrg_property_info_get_value_type (info))
				keep = FALSE;
			else if (lrg_property_info_get_read_only (o))
				read_only = TRUE;
		}
		if (!keep)
			continue;
		copy = info_dup (info);
		lrg_property_info_set_read_only (copy, read_only);
		g_ptr_array_add (out, copy);
	}
	return out;
}

/* row_reusable: same value type, hint and read-only flag */
static gboolean
row_reusable (LrgPropertyRow  *row,
              LrgPropertyInfo *info)
{
	return lrg_property_info_get_value_type (row->info) == lrg_property_info_get_value_type (info) &&
	       g_strcmp0 (lrg_property_info_get_hint (row->info), lrg_property_info_get_hint (info)) == 0 &&
	       lrg_property_info_get_read_only (row->info) == lrg_property_info_get_read_only (info);
}

/* reconcile_children: make @container's children exactly @wanted, in
 * order, touching nothing that is already in place */
static void
reconcile_children (LrgContainer *container,
                    GPtrArray    *wanted)
{
	GList *children = g_list_copy (lrg_container_get_children (container));
	GList *l;
	guint  i;

	/* Drop children that are not wanted here */
	for (l = children; l != NULL; l = l->next)
	{
		gboolean keep = FALSE;

		for (i = 0; i < wanted->len && !keep; i++)
			keep = g_ptr_array_index (wanted, i) == l->data;
		if (!keep)
			lrg_container_remove_child (container, LRG_WIDGET (l->data));
	}
	g_list_free (children);

	/* Insert or move the wanted ones into place */
	for (i = 0; i < wanted->len; i++)
	{
		LrgWidget    *w = g_ptr_array_index (wanted, i);
		LrgContainer *parent = lrg_widget_get_parent (w);

		if (parent == container)
		{
			if (lrg_container_get_child (container, i) != w)
				lrg_container_reorder_child (container, w, i);
			continue;
		}
		g_object_ref (w);
		if (parent != NULL)
			lrg_container_remove_child (parent, w);
		lrg_container_insert_child_at (container, w, i);
		g_object_unref (w);
	}
}

static void
on_section_toggled (LrgExpander *expander,
                    gboolean     expanded,
                    gpointer     user_data)
{
	LrgPropertyGrid *self = LRG_PROPERTY_GRID (user_data);

	g_hash_table_replace (self->expanded, g_strdup (lrg_expander_get_title (expander)),
	                      GINT_TO_POINTER (expanded ? 1 : 0));
}

/* ensure_section: the expander and vbox of @group */
static LrgVBox *
ensure_section (LrgPropertyGrid *self,
                const gchar     *group)
{
	LrgExpander *expander = g_hash_table_lookup (self->sections, group);
	LrgVBox     *box;

	if (expander != NULL)
		return g_hash_table_lookup (self->section_boxes, group);

	expander = lrg_expander_new (group);
	box = lrg_vbox_new ();
	lrg_container_set_spacing (LRG_CONTAINER (box), 1.0f);
	lrg_expander_set_child (expander, LRG_WIDGET (box));
	g_object_unref (box);
	lrg_widget_set_name (LRG_WIDGET (expander), group);
	lrg_expander_set_expanded (expander, lrg_property_grid_get_group_expanded (self, group));
	g_signal_connect_object (expander, "toggled", G_CALLBACK (on_section_toggled), self, 0);
	g_hash_table_replace (self->sections, g_strdup (group), expander);
	g_hash_table_replace (self->section_boxes, g_strdup (group), box);
	return box;
}

/* matches_filter: label or name contains the filter, ignoring case */
static gboolean
matches_filter (LrgPropertyGrid *self,
                LrgPropertyRow  *row)
{
	g_autofree gchar *needle = NULL;
	g_autofree gchar *label = NULL;
	g_autofree gchar *name = NULL;

	if (self->filter_text == NULL || *self->filter_text == '\0')
		return TRUE;
	needle = g_utf8_casefold (self->filter_text, -1);
	label = g_utf8_casefold (lrg_property_info_get_label (row->info), -1);
	name = g_utf8_casefold (row->name, -1);
	return strstr (label, needle) != NULL || strstr (name, needle) != NULL;
}

/* apply_filter: row and section visibility from the filter */
static void
apply_filter (LrgPropertyGrid *self)
{
	GHashTableIter iter;
	gpointer       key;
	gpointer       value;
	guint          i;

	for (i = 0; i < self->infos->len; i++)
	{
		LrgPropertyRow *row = g_hash_table_lookup (self->rows,
		                                           lrg_property_info_get_name (g_ptr_array_index (self->infos, i)));

		if (row != NULL)
			lrg_widget_set_visible (LRG_WIDGET (row), matches_filter (self, row));
	}

	/* A section shows while one of its rows does */
	g_hash_table_iter_init (&iter, self->sections);
	while (g_hash_table_iter_next (&iter, &key, &value))
	{
		LrgVBox  *box = g_hash_table_lookup (self->section_boxes, key);
		GList    *l;
		gboolean  any = FALSE;

		for (l = lrg_container_get_children (LRG_CONTAINER (box)); l != NULL && !any; l = l->next)
			any = lrg_widget_get_visible (LRG_WIDGET (l->data));
		lrg_widget_set_visible (LRG_WIDGET (value), any);
	}
	lrg_widget_queue_layout (LRG_WIDGET (self));
}

/*
 * apply_schema:
 *
 * Builds or reuses a row per property of the current schema, places
 * rows in their sections (or at the top level) and refreshes values.
 * Nothing that is already in place is touched.
 */
static void
apply_schema (LrgPropertyGrid *self,
              gboolean         force_rebuild)
{
	g_autoptr(GPtrArray)  top = g_ptr_array_new ();
	g_autoptr(GHashTable) per_group = g_hash_table_new_full (g_str_hash, g_str_equal, NULL,
	                                                         (GDestroyNotify) g_ptr_array_unref);
	g_autoptr(GPtrArray)  group_order = g_ptr_array_new ();
	g_autoptr(GHashTable) old_rows = self->rows;
	GHashTableIter        iter;
	gpointer              key;
	gpointer              value;
	guint                 i;

	g_ptr_array_unref (self->infos);
	self->infos = compute_schema (self);
	self->rows = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_object_unref);

	if (self->show_filter)
		g_ptr_array_add (top, self->filter);

	/* 1. Rows: reuse by name when the schema matches, else build */
	for (i = 0; i < self->infos->len; i++)
	{
		LrgPropertyInfo *info = g_ptr_array_index (self->infos, i);
		const gchar     *name = lrg_property_info_get_name (info);
		const gchar     *group = lrg_property_info_get_group (info);
		LrgPropertyRow  *row = g_hash_table_lookup (old_rows, name);

		if (row != NULL && !force_rebuild && row_reusable (row, info))
		{
			g_object_ref (row);
			lrg_property_info_unref (row->info);
			row->info = lrg_property_info_ref (info);
		}
		else
		{
			row = row_new (self, info);
			row_set_editor (row, create_editor (self, row));
		}
		g_hash_table_replace (self->rows, g_strdup (name), row);

		if (group == NULL || *group == '\0')
			g_ptr_array_add (top, row);
		else
		{
			GPtrArray *rows = g_hash_table_lookup (per_group, group);

			if (rows == NULL)
			{
				rows = g_ptr_array_new ();
				g_hash_table_insert (per_group, (gpointer) group, rows);
				g_ptr_array_add (group_order, (gpointer) group);
			}
			g_ptr_array_add (rows, row);
		}
	}

	/* 2. Sections, in first-appearance order, with their rows */
	for (i = 0; i < group_order->len; i++)
	{
		const gchar *group = g_ptr_array_index (group_order, i);
		LrgVBox     *box = ensure_section (self, group);

		reconcile_children (LRG_CONTAINER (box), g_hash_table_lookup (per_group, group));
		g_ptr_array_add (top, g_hash_table_lookup (self->sections, group));
	}

	/* Sections no longer used are emptied and dropped */
	g_hash_table_iter_init (&iter, self->sections);
	while (g_hash_table_iter_next (&iter, &key, &value))
	{
		if (g_hash_table_contains (per_group, key))
			continue;
		lrg_container_remove_all (LRG_CONTAINER (g_hash_table_lookup (self->section_boxes, key)));
		g_hash_table_remove (self->section_boxes, key);
		g_hash_table_iter_remove (&iter);
	}

	/* 3. Top level: filter, ungrouped rows, sections */
	reconcile_children (LRG_CONTAINER (self), top);

	/* Rows that were not reused are detached from wherever they were */
	g_hash_table_iter_init (&iter, old_rows);
	while (g_hash_table_iter_next (&iter, &key, &value))
	{
		LrgWidget *w = LRG_WIDGET (value);

		if (g_hash_table_lookup (self->rows, key) == value)
			continue;
		LRG_PROPERTY_ROW (w)->grid = NULL;
		if (lrg_widget_get_parent (w) != NULL)
			lrg_container_remove_child (lrg_widget_get_parent (w), w);
	}

	/* 4. Values */
	lrg_property_grid_refresh (self);
	apply_filter (self);
}

static void
lrg_property_grid_measure (LrgWidget *widget,
                           gfloat    *preferred_width,
                           gfloat    *preferred_height)
{
	LrgContainer *container = LRG_CONTAINER (widget);
	gfloat        spacing = lrg_container_get_spacing (container);
	gfloat        w = 0.0f;
	gfloat        h = 0.0f;
	guint         n = 0;
	GList        *l;

	for (l = lrg_container_get_children (container); l != NULL; l = l->next)
	{
		gfloat cw;
		gfloat chh;

		if (!lrg_widget_get_visible (LRG_WIDGET (l->data)))
			continue;
		lrg_widget_get_preferred_size (LRG_WIDGET (l->data), &cw, &chh);
		w = MAX (w, cw);
		h += chh;
		n++;
	}
	*preferred_width = w;
	*preferred_height = h + (n > 1 ? spacing * (gfloat) (n - 1) : 0.0f);
}

static void
lrg_property_grid_layout_children (LrgContainer *container)
{
	gfloat spacing = lrg_container_get_spacing (container);
	gfloat w = lrg_widget_get_width (LRG_WIDGET (container));
	gfloat y = 0.0f;
	GList *l;

	/* A plain stack: every visible child full width at its height */
	for (l = lrg_container_get_children (container); l != NULL; l = l->next)
	{
		LrgWidget *child = LRG_WIDGET (l->data);
		gfloat     ch;

		if (!lrg_widget_get_visible (child))
			continue;
		lrg_widget_get_preferred_size (child, NULL, &ch);
		lrg_widget_allocate (child, 0.0f, y, w, ch);
		y += ch + spacing;
	}
}

/* ==========================================================================
 * LrgPropertyGrid: sources
 * ========================================================================== */

static void
on_source_property_changed (LrgPropertySource *source,
                            const gchar       *name,
                            gpointer           user_data)
{
	LrgPropertyGrid *self = LRG_PROPERTY_GRID (user_data);
	LrgPropertyRow  *row;

	/* NULL: the schema may have changed */
	if (name == NULL)
	{
		apply_schema (self, FALSE);
		return;
	}
	row = g_hash_table_lookup (self->rows, name);
	if (row != NULL)
		row_refresh (row);
}

static void
disconnect_sources (LrgPropertyGrid *self)
{
	guint i;

	for (i = 0; i < self->sources->len; i++)
		g_signal_handler_disconnect (g_ptr_array_index (self->sources, i),
		                             g_array_index (self->handler_ids, gulong, i));
	g_ptr_array_set_size (self->sources, 0);
	g_array_set_size (self->handler_ids, 0);
}

void
lrg_property_grid_set_sources (LrgPropertyGrid *self,
                               GPtrArray       *sources)
{
	guint i;

	g_return_if_fail (LRG_IS_PROPERTY_GRID (self));

	if (self->locked)
		return;

	/* Commit gestures in flight against the old sources */
	{
		GHashTableIter iter;
		gpointer       value;

		g_hash_table_iter_init (&iter, self->rows);
		while (g_hash_table_iter_next (&iter, NULL, &value))
			if (LRG_PROPERTY_ROW (value)->gesture_id != 0)
				lrg_property_row_end_edit (LRG_PROPERTY_ROW (value), TRUE);
	}

	disconnect_sources (self);
	for (i = 0; sources != NULL && i < sources->len; i++)
	{
		LrgPropertySource *source = g_ptr_array_index (sources, i);
		gulong             id;

		g_return_if_fail (LRG_IS_PROPERTY_SOURCE (source));
		g_ptr_array_add (self->sources, g_object_ref (source));
		id = g_signal_connect (source, "property-changed", G_CALLBACK (on_source_property_changed), self);
		g_array_append_val (self->handler_ids, id);
	}
	apply_schema (self, FALSE);
}

void
lrg_property_grid_set_source (LrgPropertyGrid   *self,
                              LrgPropertySource *source)
{
	g_autoptr(GPtrArray) one = g_ptr_array_new ();

	g_return_if_fail (LRG_IS_PROPERTY_GRID (self));
	g_return_if_fail (source == NULL || LRG_IS_PROPERTY_SOURCE (source));

	if (source != NULL)
		g_ptr_array_add (one, source);
	lrg_property_grid_set_sources (self, one);
}

void
lrg_property_grid_set_object (LrgPropertyGrid *self,
                              GObject         *object)
{
	g_autoptr(LrgObjectPropertySource) source = NULL;

	g_return_if_fail (LRG_IS_PROPERTY_GRID (self));
	g_return_if_fail (object == NULL || G_IS_OBJECT (object));

	if (object != NULL)
		source = lrg_object_property_source_new (object);
	lrg_property_grid_set_source (self, source != NULL ? LRG_PROPERTY_SOURCE (source) : NULL);
}

guint
lrg_property_grid_get_n_sources (LrgPropertyGrid *self)
{
	g_return_val_if_fail (LRG_IS_PROPERTY_GRID (self), 0);
	return self->sources->len;
}

LrgPropertySource *
lrg_property_grid_get_source (LrgPropertyGrid *self,
                              guint            index)
{
	g_return_val_if_fail (LRG_IS_PROPERTY_GRID (self), NULL);
	return index < self->sources->len ? g_ptr_array_index (self->sources, index) : NULL;
}

void
lrg_property_grid_refresh (LrgPropertyGrid *self)
{
	guint i;

	g_return_if_fail (LRG_IS_PROPERTY_GRID (self));

	for (i = 0; i < self->infos->len; i++)
	{
		LrgPropertyRow *row = g_hash_table_lookup (self->rows,
		                                           lrg_property_info_get_name (g_ptr_array_index (self->infos, i)));

		if (row != NULL)
			row_refresh (row);
	}
}

void
lrg_property_grid_rebuild (LrgPropertyGrid *self)
{
	g_return_if_fail (LRG_IS_PROPERTY_GRID (self));
	apply_schema (self, TRUE);
}

/* ==========================================================================
 * LrgPropertyGrid: actions on properties
 * ========================================================================== */

/* Process-wide property clipboard */
static GValue clip_value = G_VALUE_INIT;
static gchar *clip_text = NULL;

gboolean
lrg_property_grid_set_reference_value (LrgPropertyGrid *self,
                                       const gchar     *name,
                                       const GValue    *value)
{
	LrgPropertyRow *row;

	g_return_val_if_fail (LRG_IS_PROPERTY_GRID (self), FALSE);
	g_return_val_if_fail (name != NULL, FALSE);
	g_return_val_if_fail (G_IS_VALUE (value), FALSE);

	row = g_hash_table_lookup (self->rows, name);
	if (row == NULL || lrg_property_row_get_read_only (row))
		return FALSE;
	if (!g_value_type_transformable (G_VALUE_TYPE (value), lrg_property_info_get_value_type (row->info)) &&
	    !g_value_type_compatible (G_VALUE_TYPE (value), lrg_property_info_get_value_type (row->info)))
		return FALSE;
	lrg_property_row_apply_value (row, value);
	return TRUE;
}

gboolean
lrg_property_grid_drop_reference (LrgPropertyGrid *self,
                                  const gchar     *name,
                                  const gchar     *payload_type,
                                  const GValue    *value)
{
	LrgPropertyRow   *row;
	g_autofree gchar *kind = NULL;
	gboolean          is_node;
	gboolean          is_asset;

	g_return_val_if_fail (LRG_IS_PROPERTY_GRID (self), FALSE);
	g_return_val_if_fail (name != NULL && payload_type != NULL, FALSE);
	g_return_val_if_fail (G_IS_VALUE (value), FALSE);

	row = g_hash_table_lookup (self->rows, name);
	if (row == NULL)
		return FALSE;

	/* Nodes go to node-ref / object-ref rows, assets to asset rows */
	lrg_property_hint_split (ref_hint (row), &kind, NULL);
	is_node = g_strcmp0 (payload_type, "application/x-lrg-node") == 0;
	is_asset = g_strcmp0 (payload_type, "application/x-lrg-asset") == 0;
	if (!((is_node && (g_strcmp0 (kind, "node-ref") == 0 || g_strcmp0 (kind, "object-ref") == 0)) ||
	      (is_asset && g_strcmp0 (kind, "asset") == 0)))
		return FALSE;
	return lrg_property_grid_set_reference_value (self, name, value);
}

void
lrg_property_grid_reset_property (LrgPropertyGrid *self,
                                  const gchar     *name)
{
	LrgPropertyRow *row;
	guint           i;

	g_return_if_fail (LRG_IS_PROPERTY_GRID (self));
	g_return_if_fail (name != NULL);

	row = g_hash_table_lookup (self->rows, name);
	if (row == NULL || lrg_property_row_get_read_only (row))
		return;

	g_signal_emit (self, signals[SIGNAL_RESET_REQUESTED], 0, name);
	if (self->apply_mode == LRG_PROPERTY_APPLY_DIRECT)
	{
		for (i = 0; i < self->sources->len; i++)
			lrg_property_source_reset (g_ptr_array_index (self->sources, i), name);
		row_refresh (row);
	}
}

gboolean
lrg_property_grid_copy_value (LrgPropertyGrid *self,
                              const gchar     *name)
{
	LrgPropertyRow *row;

	g_return_val_if_fail (LRG_IS_PROPERTY_GRID (self), FALSE);
	g_return_val_if_fail (name != NULL, FALSE);

	row = g_hash_table_lookup (self->rows, name);
	if (row == NULL || row->mixed || !G_IS_VALUE (&row->value))
		return FALSE;

	value_copy_into (&clip_value, &row->value);
	g_free (clip_text);
	clip_text = lrg_property_value_to_string (&row->value);
	if (IsWindowReady ())
		SetClipboardText (clip_text);
	return TRUE;
}

gboolean
lrg_property_grid_paste_value (LrgPropertyGrid *self,
                               const gchar     *name)
{
	LrgPropertyRow *row;
	g_auto(GValue)  parsed = G_VALUE_INIT;
	const gchar    *text = clip_text;
	GType           type;

	g_return_val_if_fail (LRG_IS_PROPERTY_GRID (self), FALSE);
	g_return_val_if_fail (name != NULL, FALSE);

	row = g_hash_table_lookup (self->rows, name);
	if (row == NULL || lrg_property_row_get_read_only (row))
		return FALSE;
	type = lrg_property_info_get_value_type (row->info);

	/* Same type: the copied value itself */
	if (G_IS_VALUE (&clip_value) && G_VALUE_TYPE (&clip_value) == type)
	{
		lrg_property_row_apply_value (row, &clip_value);
		return TRUE;
	}

	/* Otherwise its text (or the system clipboard's), parsed */
	if (IsWindowReady () && GetClipboardText () != NULL)
		text = GetClipboardText ();
	if (text == NULL)
		return FALSE;
	g_value_init (&parsed, type);
	if (!lrg_property_value_from_string (&parsed, text))
		return FALSE;
	lrg_property_row_apply_value (row, &parsed);
	return TRUE;
}

gboolean
lrg_property_grid_get_value_for_source (LrgPropertyGrid *self,
                                        const gchar     *name,
                                        guint            index,
                                        const GValue    *edited,
                                        GValue          *out)
{
	LrgPropertyRow *row;

	g_return_val_if_fail (LRG_IS_PROPERTY_GRID (self), FALSE);
	g_return_val_if_fail (name != NULL, FALSE);
	g_return_val_if_fail (G_IS_VALUE (edited), FALSE);
	g_return_val_if_fail (out != NULL && !G_IS_VALUE (out), FALSE);

	row = g_hash_table_lookup (self->rows, name);
	if (row == NULL || index >= self->sources->len)
		return FALSE;
	merge_for_source (row, index, edited, out);
	return TRUE;
}

/* ==========================================================================
 * LrgPropertyGrid: GObject
 * ========================================================================== */

static void
on_filter_changed (LrgTextInput *input,
                   gpointer      user_data)
{
	LrgPropertyGrid *self = LRG_PROPERTY_GRID (user_data);

	if (!self->setting_filter)
		lrg_property_grid_set_filter_text (self, lrg_text_input_get_text (input));
}

static void
lrg_property_grid_dispose (GObject *object)
{
	LrgPropertyGrid *self = LRG_PROPERTY_GRID (object);
	GHashTableIter   iter;
	gpointer         value;

	if (self->sources != NULL)
		disconnect_sources (self);
	if (self->rows != NULL)
	{
		g_hash_table_iter_init (&iter, self->rows);
		while (g_hash_table_iter_next (&iter, NULL, &value))
		{
			LrgPropertyRow *row = LRG_PROPERTY_ROW (value);
			guint           i;

			/* Rows (and array elements) may outlive the grid */
			row->grid = NULL;
			for (i = 0; row->elements != NULL && i < row->elements->len; i++)
				LRG_PROPERTY_ROW (g_ptr_array_index (row->elements, i))->grid = NULL;
		}
		g_hash_table_remove_all (self->rows);
	}
	if (self->sections != NULL)
		g_hash_table_remove_all (self->sections);
	if (self->section_boxes != NULL)
		g_hash_table_remove_all (self->section_boxes);
	if (self->filter != NULL)
		g_signal_handlers_disconnect_by_data (self->filter, self);
	g_clear_object (&self->filter);
	if (self->ref_destroy != NULL && self->ref_data != NULL)
		self->ref_destroy (self->ref_data);
	self->ref_destroy = NULL;
	self->ref_data = NULL;
	self->ref_func = NULL;
	G_OBJECT_CLASS (lrg_property_grid_parent_class)->dispose (object);
}

static void
lrg_property_grid_finalize (GObject *object)
{
	LrgPropertyGrid *self = LRG_PROPERTY_GRID (object);

	g_ptr_array_unref (self->sources);
	g_array_unref (self->handler_ids);
	g_ptr_array_unref (self->infos);
	g_hash_table_unref (self->rows);
	g_hash_table_unref (self->sections);
	g_hash_table_unref (self->section_boxes);
	g_hash_table_unref (self->expanded);
	g_hash_table_unref (self->editors);
	g_free (self->filter_text);
	G_OBJECT_CLASS (lrg_property_grid_parent_class)->finalize (object);
}

static void
lrg_property_grid_get_property (GObject    *object,
                                guint       prop_id,
                                GValue     *value,
                                GParamSpec *pspec)
{
	LrgPropertyGrid *self = LRG_PROPERTY_GRID (object);

	switch (prop_id)
	{
	case PROP_LOCKED:
		g_value_set_boolean (value, self->locked);
		break;
	case PROP_SHOW_FILTER:
		g_value_set_boolean (value, self->show_filter);
		break;
	case PROP_FILTER_TEXT:
		g_value_set_string (value, self->filter_text);
		break;
	case PROP_LABEL_WIDTH:
		g_value_set_float (value, self->label_width);
		break;
	case PROP_APPLY_MODE:
		g_value_set_enum (value, self->apply_mode);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_property_grid_set_property (GObject      *object,
                                guint         prop_id,
                                const GValue *value,
                                GParamSpec   *pspec)
{
	LrgPropertyGrid *self = LRG_PROPERTY_GRID (object);

	switch (prop_id)
	{
	case PROP_LOCKED:
		lrg_property_grid_set_locked (self, g_value_get_boolean (value));
		break;
	case PROP_SHOW_FILTER:
		lrg_property_grid_set_show_filter (self, g_value_get_boolean (value));
		break;
	case PROP_FILTER_TEXT:
		lrg_property_grid_set_filter_text (self, g_value_get_string (value));
		break;
	case PROP_LABEL_WIDTH:
		lrg_property_grid_set_label_width (self, g_value_get_float (value));
		break;
	case PROP_APPLY_MODE:
		lrg_property_grid_set_apply_mode (self, (LrgPropertyApplyMode) g_value_get_enum (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_property_grid_class_init (LrgPropertyGridClass *klass)
{
	GObjectClass      *object_class = G_OBJECT_CLASS (klass);
	LrgWidgetClass    *widget_class = LRG_WIDGET_CLASS (klass);
	LrgContainerClass *container_class = LRG_CONTAINER_CLASS (klass);

	object_class->dispose = lrg_property_grid_dispose;
	object_class->finalize = lrg_property_grid_finalize;
	object_class->get_property = lrg_property_grid_get_property;
	object_class->set_property = lrg_property_grid_set_property;

	widget_class->measure = lrg_property_grid_measure;
	container_class->layout_children = lrg_property_grid_layout_children;

	/**
	 * LrgPropertyGrid:locked:
	 *
	 * While set, lrg_property_grid_set_sources() is ignored.
	 */
	properties[PROP_LOCKED] =
		g_param_spec_boolean ("locked", NULL, NULL, FALSE,
		                      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgPropertyGrid:show-filter:
	 *
	 * Whether a filter field is shown above the rows.
	 */
	properties[PROP_SHOW_FILTER] =
		g_param_spec_boolean ("show-filter", NULL, NULL, FALSE,
		                      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgPropertyGrid:filter-text:
	 *
	 * Rows whose label or name does not contain this are hidden.
	 */
	properties[PROP_FILTER_TEXT] =
		g_param_spec_string ("filter-text", NULL, NULL, "",
		                     G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgPropertyGrid:label-width:
	 *
	 * Width of the label column.
	 */
	properties[PROP_LABEL_WIDTH] =
		g_param_spec_float ("label-width", NULL, NULL, 0.0f, G_MAXFLOAT, DEFAULT_LABEL_WIDTH,
		                    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgPropertyGrid:apply-mode:
	 *
	 * Whether the grid writes edits itself (DIRECT) or only emits them
	 * (DELEGATE).
	 */
	properties[PROP_APPLY_MODE] =
		g_param_spec_enum ("apply-mode", NULL, NULL, LRG_TYPE_PROPERTY_APPLY_MODE, LRG_PROPERTY_APPLY_DIRECT,
		                   G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	g_object_class_install_properties (object_class, N_PROPS, properties);

	/**
	 * LrgPropertyGrid::edit-begin:
	 * @self: the grid
	 * @name: the property
	 * @gesture_id: identifies the gesture in the edit-value and
	 *   edit-end emissions that follow
	 *
	 * A gesture starts (a drag, a typing session, a popup, or a one-shot
	 * change). Open an undo group / start merging here.
	 */
	signals[SIGNAL_EDIT_BEGIN] =
		g_signal_new ("edit-begin", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
		              0, NULL, NULL, NULL, G_TYPE_NONE, 2, G_TYPE_STRING, G_TYPE_UINT);

	/**
	 * LrgPropertyGrid::edit-value:
	 * @self: the grid
	 * @name: the property
	 * @value: the new value, of the property's type
	 * @gesture_id: the gesture
	 * @interactive: %TRUE for intermediate values of a drag
	 *
	 * A value inside a gesture; repeats while scrubbing or dragging. In
	 * multi-edit, lrg_property_grid_get_value_for_source() gives what
	 * it means for each source.
	 */
	signals[SIGNAL_EDIT_VALUE] =
		g_signal_new ("edit-value", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
		              0, NULL, NULL, NULL, G_TYPE_NONE, 4, G_TYPE_STRING,
		              G_TYPE_VALUE | G_SIGNAL_TYPE_STATIC_SCOPE, G_TYPE_UINT, G_TYPE_BOOLEAN);

	/**
	 * LrgPropertyGrid::edit-end:
	 * @self: the grid
	 * @name: the property
	 * @gesture_id: the gesture
	 * @commit: %FALSE when cancelled (Escape): undo what the gesture did
	 *
	 * The gesture ended. In DIRECT mode a cancelled gesture's original
	 * values were already written back.
	 */
	signals[SIGNAL_EDIT_END] =
		g_signal_new ("edit-end", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
		              0, NULL, NULL, NULL, G_TYPE_NONE, 3, G_TYPE_STRING, G_TYPE_UINT, G_TYPE_BOOLEAN);

	/**
	 * LrgPropertyGrid::reset-requested:
	 * @self: the grid
	 * @name: the property
	 *
	 * "Reset to default" was chosen. In DIRECT mode the grid resets the
	 * sources after this signal; in DELEGATE mode the application does.
	 */
	signals[SIGNAL_RESET_REQUESTED] =
		g_signal_new ("reset-requested", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
		              0, NULL, NULL, NULL, G_TYPE_NONE, 1, G_TYPE_STRING);

	/**
	 * LrgPropertyGrid::pick-requested:
	 * @self: the grid
	 * @name: the property
	 * @hint: the reference hint ("node-ref", "asset:texture",
	 *   "object-ref:LrgNode")
	 *
	 * The pick button of a reference row was pressed. Let the user pick
	 * (an eyedropper in the viewport, an asset dialog) and hand the
	 * result to lrg_property_grid_set_reference_value().
	 */
	signals[SIGNAL_PICK_REQUESTED] =
		g_signal_new ("pick-requested", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
		              0, NULL, NULL, NULL, G_TYPE_NONE, 2, G_TYPE_STRING, G_TYPE_STRING);
}

static void
lrg_property_grid_init (LrgPropertyGrid *self)
{
	self->sources = g_ptr_array_new_with_free_func (g_object_unref);
	self->handler_ids = g_array_new (FALSE, TRUE, sizeof (gulong));
	self->infos = g_ptr_array_new_with_free_func ((GDestroyNotify) lrg_property_info_unref);
	self->rows = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_object_unref);
	self->sections = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_object_unref);
	self->section_boxes = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
	self->expanded = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
	self->editors = editor_table_new ();
	self->filter_text = g_strdup ("");
	self->label_width = DEFAULT_LABEL_WIDTH;
	self->apply_mode = LRG_PROPERTY_APPLY_DIRECT;
	self->next_gesture = 1;

	/* The filter field, a child only while shown */
	self->filter = g_object_ref_sink (lrg_text_input_new_with_placeholder ("Filter properties"));
	lrg_widget_set_name (LRG_WIDGET (self->filter), "filter");
	g_signal_connect (self->filter, "text-changed", G_CALLBACK (on_filter_changed), self);

	lrg_container_set_spacing (LRG_CONTAINER (self), 4.0f);
}

/* ==========================================================================
 * LrgPropertyGrid: public API
 * ========================================================================== */

LrgPropertyGrid *
lrg_property_grid_new (void)
{
	return g_object_new (LRG_TYPE_PROPERTY_GRID, NULL);
}

gboolean
lrg_property_grid_get_locked (LrgPropertyGrid *self)
{
	g_return_val_if_fail (LRG_IS_PROPERTY_GRID (self), FALSE);
	return self->locked;
}

void
lrg_property_grid_set_locked (LrgPropertyGrid *self,
                              gboolean         locked)
{
	g_return_if_fail (LRG_IS_PROPERTY_GRID (self));

	locked = !!locked;
	if (self->locked == locked)
		return;
	self->locked = locked;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_LOCKED]);
}

gboolean
lrg_property_grid_get_show_filter (LrgPropertyGrid *self)
{
	g_return_val_if_fail (LRG_IS_PROPERTY_GRID (self), FALSE);
	return self->show_filter;
}

void
lrg_property_grid_set_show_filter (LrgPropertyGrid *self,
                                   gboolean         show)
{
	g_return_if_fail (LRG_IS_PROPERTY_GRID (self));

	show = !!show;
	if (self->show_filter == show)
		return;
	self->show_filter = show;
	if (show)
		lrg_container_insert_child_at (LRG_CONTAINER (self), LRG_WIDGET (self->filter), 0);
	else
		lrg_container_remove_child (LRG_CONTAINER (self), LRG_WIDGET (self->filter));
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SHOW_FILTER]);
}

const gchar *
lrg_property_grid_get_filter_text (LrgPropertyGrid *self)
{
	g_return_val_if_fail (LRG_IS_PROPERTY_GRID (self), NULL);
	return self->filter_text;
}

void
lrg_property_grid_set_filter_text (LrgPropertyGrid *self,
                                   const gchar     *text)
{
	g_return_if_fail (LRG_IS_PROPERTY_GRID (self));

	if (text == NULL)
		text = "";
	if (g_strcmp0 (self->filter_text, text) == 0)
		return;
	g_free (self->filter_text);
	self->filter_text = g_strdup (text);

	/* Keep the field in step without echoing back */
	if (g_strcmp0 (lrg_text_input_get_text (self->filter), text) != 0)
	{
		self->setting_filter = TRUE;
		lrg_text_input_set_text (self->filter, text);
		self->setting_filter = FALSE;
	}
	apply_filter (self);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_FILTER_TEXT]);
}

gfloat
lrg_property_grid_get_label_width (LrgPropertyGrid *self)
{
	g_return_val_if_fail (LRG_IS_PROPERTY_GRID (self), 0.0f);
	return self->label_width;
}

void
lrg_property_grid_set_label_width (LrgPropertyGrid *self,
                                   gfloat           width)
{
	g_return_if_fail (LRG_IS_PROPERTY_GRID (self));
	g_return_if_fail (width >= 0.0f);

	if (self->label_width == width)
		return;
	self->label_width = width;
	lrg_widget_queue_layout (LRG_WIDGET (self));
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_LABEL_WIDTH]);
}

LrgPropertyApplyMode
lrg_property_grid_get_apply_mode (LrgPropertyGrid *self)
{
	g_return_val_if_fail (LRG_IS_PROPERTY_GRID (self), LRG_PROPERTY_APPLY_DIRECT);
	return self->apply_mode;
}

void
lrg_property_grid_set_apply_mode (LrgPropertyGrid      *self,
                                  LrgPropertyApplyMode  mode)
{
	g_return_if_fail (LRG_IS_PROPERTY_GRID (self));
	g_return_if_fail (mode == LRG_PROPERTY_APPLY_DIRECT || mode == LRG_PROPERTY_APPLY_DELEGATE);

	if (self->apply_mode == mode)
		return;
	self->apply_mode = mode;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_APPLY_MODE]);
}

LrgPropertyRow *
lrg_property_grid_get_row (LrgPropertyGrid *self,
                           const gchar     *name)
{
	g_return_val_if_fail (LRG_IS_PROPERTY_GRID (self), NULL);
	g_return_val_if_fail (name != NULL, NULL);
	return g_hash_table_lookup (self->rows, name);
}

LrgExpander *
lrg_property_grid_get_section (LrgPropertyGrid *self,
                               const gchar     *group)
{
	g_return_val_if_fail (LRG_IS_PROPERTY_GRID (self), NULL);
	g_return_val_if_fail (group != NULL, NULL);
	return g_hash_table_lookup (self->sections, group);
}

gboolean
lrg_property_grid_get_group_expanded (LrgPropertyGrid *self,
                                      const gchar     *group)
{
	gpointer value;

	g_return_val_if_fail (LRG_IS_PROPERTY_GRID (self), TRUE);
	g_return_val_if_fail (group != NULL, TRUE);

	if (!g_hash_table_lookup_extended (self->expanded, group, NULL, &value))
		return TRUE;
	return GPOINTER_TO_INT (value) != 0;
}

void
lrg_property_grid_set_group_expanded (LrgPropertyGrid *self,
                                      const gchar     *group,
                                      gboolean         expanded)
{
	LrgExpander *expander;

	g_return_if_fail (LRG_IS_PROPERTY_GRID (self));
	g_return_if_fail (group != NULL);

	g_hash_table_replace (self->expanded, g_strdup (group), GINT_TO_POINTER (expanded ? 1 : 0));
	expander = g_hash_table_lookup (self->sections, group);
	if (expander != NULL)
		lrg_expander_set_expanded (expander, expanded);
}

void
lrg_property_grid_set_reference_func (LrgPropertyGrid          *self,
                                      LrgPropertyReferenceFunc  func,
                                      gpointer                  user_data,
                                      GDestroyNotify            destroy)
{
	g_return_if_fail (LRG_IS_PROPERTY_GRID (self));

	if (self->ref_destroy != NULL && self->ref_data != NULL)
		self->ref_destroy (self->ref_data);
	self->ref_func = func;
	self->ref_data = user_data;
	self->ref_destroy = destroy;
	lrg_property_grid_refresh (self);
}

LrgWidget *
lrg_property_grid_get_filter_input (LrgPropertyGrid *self)
{
	g_return_val_if_fail (LRG_IS_PROPERTY_GRID (self), NULL);
	return LRG_WIDGET (self->filter);
}
