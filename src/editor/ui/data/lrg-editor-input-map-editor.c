/* lrg-editor-input-map-editor.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgEditorInputMapEditor: the input map data editor (see the header).
 *
 * Layout. The content is an overlay of a horizontal splitter (the action
 * table on the left, the binding grid on the right) and the capture veil
 * on top (hidden unless capturing). The table has two columns: the action
 * name (text drawn by the table) and the bindings, a custom cell that
 * paints one chip per binding plus a trailing "+" chip and hit-tests
 * clicks on them itself.
 *
 * Selection is (action, slot): slot -1 is the action row itself (the grid
 * shows its name), slot >= 0 a binding (the grid shows its type and the
 * fields of that type). Clicking a selected chip again, the Capture tool
 * or the "+" chip starts capture.
 *
 * Every edit goes through the document: renames are rename_key, a type
 * change replaces the binding mapping with the defaults of the new type,
 * modifier flags rewrite the "modifiers" sequence, a captured key replaces
 * the binding. The table model is rebuilt from the document on every
 * change, keeping the selection by name.
 */

#include "config.h"

#define LRG_EDITOR_PANEL_INTERNAL
#include "lrg-editor-input-map-editor.h"
#include "../lrg-editor-app-private.h"
#include "../lrg-editor-console-panel.h"
#include "../../../ui/lrg-table.h"
#include "../../../ui/lrg-label.h"
#include "../../../ui/lrg-vbox.h"
#include "../../../ui/lrg-overlay.h"
#include "../../../ui/lrg-splitter.h"
#include "../../../ui/lrg-custom-widget.h"
#include "../../../ui/lrg-property-grid.h"
#include "../../../ui/lrg-ui-event.h"
#include "../../../paint/lrg-paint-list.h"
#include <string.h>

/* ==========================================================================
 * Name tables (the spellings of lrg-input-map.c)
 * ========================================================================== */

typedef struct
{
	const gchar *name;
	gint         value;
} NameEntry;

static const NameEntry key_names[] = {
	{ "SPACE", GRL_KEY_SPACE }, { "ESCAPE", GRL_KEY_ESCAPE }, { "ENTER", GRL_KEY_ENTER },
	{ "TAB", GRL_KEY_TAB }, { "BACKSPACE", GRL_KEY_BACKSPACE }, { "INSERT", GRL_KEY_INSERT },
	{ "DELETE", GRL_KEY_DELETE }, { "RIGHT", GRL_KEY_RIGHT }, { "LEFT", GRL_KEY_LEFT },
	{ "DOWN", GRL_KEY_DOWN }, { "UP", GRL_KEY_UP }, { "PAGE_UP", GRL_KEY_PAGE_UP },
	{ "PAGE_DOWN", GRL_KEY_PAGE_DOWN }, { "HOME", GRL_KEY_HOME }, { "END", GRL_KEY_END },
	{ "CAPS_LOCK", GRL_KEY_CAPS_LOCK }, { "SCROLL_LOCK", GRL_KEY_SCROLL_LOCK }, { "NUM_LOCK", GRL_KEY_NUM_LOCK },
	{ "PRINT_SCREEN", GRL_KEY_PRINT_SCREEN }, { "PAUSE", GRL_KEY_PAUSE },
	{ "F1", GRL_KEY_F1 }, { "F2", GRL_KEY_F2 }, { "F3", GRL_KEY_F3 }, { "F4", GRL_KEY_F4 },
	{ "F5", GRL_KEY_F5 }, { "F6", GRL_KEY_F6 }, { "F7", GRL_KEY_F7 }, { "F8", GRL_KEY_F8 },
	{ "F9", GRL_KEY_F9 }, { "F10", GRL_KEY_F10 }, { "F11", GRL_KEY_F11 }, { "F12", GRL_KEY_F12 },
	{ "LEFT_SHIFT", GRL_KEY_LEFT_SHIFT }, { "LEFT_CONTROL", GRL_KEY_LEFT_CONTROL },
	{ "LEFT_ALT", GRL_KEY_LEFT_ALT }, { "RIGHT_SHIFT", GRL_KEY_RIGHT_SHIFT },
	{ "RIGHT_CONTROL", GRL_KEY_RIGHT_CONTROL }, { "RIGHT_ALT", GRL_KEY_RIGHT_ALT },
	{ "A", GRL_KEY_A }, { "B", GRL_KEY_B }, { "C", GRL_KEY_C }, { "D", GRL_KEY_D }, { "E", GRL_KEY_E },
	{ "F", GRL_KEY_F }, { "G", GRL_KEY_G }, { "H", GRL_KEY_H }, { "I", GRL_KEY_I }, { "J", GRL_KEY_J },
	{ "K", GRL_KEY_K }, { "L", GRL_KEY_L }, { "M", GRL_KEY_M }, { "N", GRL_KEY_N }, { "O", GRL_KEY_O },
	{ "P", GRL_KEY_P }, { "Q", GRL_KEY_Q }, { "R", GRL_KEY_R }, { "S", GRL_KEY_S }, { "T", GRL_KEY_T },
	{ "U", GRL_KEY_U }, { "V", GRL_KEY_V }, { "W", GRL_KEY_W }, { "X", GRL_KEY_X }, { "Y", GRL_KEY_Y },
	{ "Z", GRL_KEY_Z }, { "0", GRL_KEY_ZERO }, { "1", GRL_KEY_ONE }, { "2", GRL_KEY_TWO },
	{ "3", GRL_KEY_THREE }, { "4", GRL_KEY_FOUR }, { "5", GRL_KEY_FIVE }, { "6", GRL_KEY_SIX },
	{ "7", GRL_KEY_SEVEN }, { "8", GRL_KEY_EIGHT }, { "9", GRL_KEY_NINE },
	{ NULL, 0 }
};

static const NameEntry mouse_names[] = {
	{ "LEFT", GRL_MOUSE_BUTTON_LEFT }, { "RIGHT", GRL_MOUSE_BUTTON_RIGHT }, { "MIDDLE", GRL_MOUSE_BUTTON_MIDDLE },
	{ "SIDE", GRL_MOUSE_BUTTON_SIDE }, { "EXTRA", GRL_MOUSE_BUTTON_EXTRA }, { "FORWARD", GRL_MOUSE_BUTTON_FORWARD },
	{ "BACK", GRL_MOUSE_BUTTON_BACK },
	{ NULL, 0 }
};

/* Gamepad buttons with short chip labels */
static const struct
{
	const gchar *name;
	const gchar *label;
} pad_buttons[] = {
	{ "RIGHT_FACE_DOWN", "A" }, { "RIGHT_FACE_RIGHT", "B" }, { "RIGHT_FACE_LEFT", "X" }, { "RIGHT_FACE_UP", "Y" },
	{ "LEFT_FACE_UP", "D-pad Up" }, { "LEFT_FACE_DOWN", "D-pad Down" }, { "LEFT_FACE_LEFT", "D-pad Left" },
	{ "LEFT_FACE_RIGHT", "D-pad Right" }, { "LEFT_TRIGGER_1", "LB" }, { "LEFT_TRIGGER_2", "LT" },
	{ "RIGHT_TRIGGER_1", "RB" }, { "RIGHT_TRIGGER_2", "RT" }, { "MIDDLE_LEFT", "Select" }, { "MIDDLE", "Home" },
	{ "MIDDLE_RIGHT", "Start" }, { "LEFT_THUMB", "L3" }, { "RIGHT_THUMB", "R3" },
	{ NULL, NULL }
};

static const struct
{
	const gchar *name;
	const gchar *label;
} pad_axes[] = {
	{ "LEFT_X", "Left Stick X" }, { "LEFT_Y", "Left Stick Y" }, { "RIGHT_X", "Right Stick X" },
	{ "RIGHT_Y", "Right Stick Y" }, { "LEFT_TRIGGER", "Left Trigger" }, { "RIGHT_TRIGGER", "Right Trigger" },
	{ NULL, NULL }
};

static const gchar *
name_for (const NameEntry *table,
          gint             value)
{
	for (; table->name != NULL; table++)
		if (table->value == value)
			return table->name;
	return NULL;
}

/* "enum-strings:A,B,C" of a table */
static gchar *
enum_hint (const NameEntry *table)
{
	GString *s = g_string_new ("enum-strings:");

	for (; table->name != NULL; table++)
	{
		g_string_append (s, table->name);
		if (table[1].name != NULL)
			g_string_append_c (s, ',');
	}
	return g_string_free (s, FALSE);
}

/* "PAGE_UP" -> "Page Up" */
static gchar *
pretty_name (const gchar *name)
{
	GString  *s = g_string_new (NULL);
	gboolean  start = TRUE;

	for (; name != NULL && *name != '\0'; name++)
	{
		if (*name == '_')
		{
			g_string_append_c (s, ' ');
			start = TRUE;
			continue;
		}
		g_string_append_c (s, start ? g_ascii_toupper (*name) : g_ascii_tolower (*name));
		start = FALSE;
	}
	return g_string_free (s, FALSE);
}

/* ==========================================================================
 * Rows of the table: one per action
 * ========================================================================== */

#define INPUT_TYPE_ACTION_ROW (input_action_row_get_type ())
G_DECLARE_FINAL_TYPE (InputActionRow, input_action_row, INPUT, ACTION_ROW, GObject)

struct _InputActionRow
{
	GObject  parent_instance;
	gchar   *name;
};

G_DEFINE_FINAL_TYPE (InputActionRow, input_action_row, G_TYPE_OBJECT)

static void
input_action_row_finalize (GObject *object)
{
	g_free (INPUT_ACTION_ROW (object)->name);
	G_OBJECT_CLASS (input_action_row_parent_class)->finalize (object);
}

static void
input_action_row_class_init (InputActionRowClass *klass)
{
	G_OBJECT_CLASS (klass)->finalize = input_action_row_finalize;
}

static void
input_action_row_init (InputActionRow *self)
{
}

/* ==========================================================================
 * Instance
 * ========================================================================== */

struct _LrgEditorInputMapEditor
{
	LrgEditorDataEditor  parent_instance;

	LrgTable            *table;
	GListStore          *rows;       /* InputActionRow */
	LrgPropertyGrid     *grid;
	LrgEditorDataSource *source;
	LrgLabel            *grid_title;
	LrgWidget           *grid_hint;
	LrgWidget           *veil;       /* capture prompt */

	gchar               *sel_action;
	gint                 sel_slot;
	gboolean             capturing;
	gchar               *cap_action;
	gint                 cap_slot;
	gboolean             syncing;    /* rebuilding: ignore selection signals */
	gchar               *grid_sig;   /* see grid_signature() */
	gboolean             grid_stale; /* rebuild the grid on the next step */
};

G_DEFINE_FINAL_TYPE (LrgEditorInputMapEditor, lrg_editor_input_map_editor, LRG_TYPE_EDITOR_DATA_EDITOR)

static void rebuild_grid (LrgEditorInputMapEditor *self);
static gchar *grid_signature (LrgEditorInputMapEditor *self);

static LrgEditorDocument *
doc_of (LrgEditorInputMapEditor *self)
{
	return lrg_editor_data_editor_get_document (LRG_EDITOR_DATA_EDITOR (self));
}

/* "/actions/<name>" */
static gchar *
action_path (const gchar *action)
{
	return lrg_editor_document_path_join ("/actions", action);
}

/* "/actions/<name>/bindings" */
static gchar *
bindings_path (const gchar *action)
{
	g_autofree gchar *a = action_path (action);

	return lrg_editor_document_path_join (a, "bindings");
}

/* "/actions/<name>/bindings/<slot>[/<field>]" */
static gchar *
binding_path (const gchar *action,
              gint         slot,
              const gchar *field)
{
	g_autofree gchar *b = bindings_path (action);
	g_autofree gchar *idx = g_strdup_printf ("%d", slot);
	g_autofree gchar *p = lrg_editor_document_path_join (b, idx);

	return field != NULL ? lrg_editor_document_path_join (p, field) : g_steal_pointer (&p);
}

static gint
n_bindings (LrgEditorInputMapEditor *self,
            const gchar             *action)
{
	g_autofree gchar *b = bindings_path (action);

	if (lrg_editor_document_get_kind (doc_of (self), b) != LRG_EDITOR_DOCUMENT_NODE_SEQUENCE)
		return 0;
	return lrg_editor_document_get_length (doc_of (self), b);
}

static gboolean
has_action (LrgEditorInputMapEditor *self,
            const gchar             *action)
{
	g_autofree gchar *a = NULL;

	if (action == NULL)
		return FALSE;
	a = action_path (action);
	return lrg_editor_document_has_path (doc_of (self), a);
}

static const gchar *
binding_field (LrgEditorInputMapEditor *self,
               const gchar             *action,
               gint                     slot,
               const gchar             *field)
{
	g_autofree gchar *p = binding_path (action, slot, field);

	return lrg_editor_document_get_scalar (doc_of (self), p);
}

/* Whether the binding's "modifiers" sequence holds @mod (case-insensitive;
 * CONTROL counts as CTRL). */
static gboolean
binding_has_modifier (LrgEditorInputMapEditor *self,
                      const gchar             *action,
                      gint                     slot,
                      const gchar             *mod)
{
	g_autofree gchar *seq = binding_path (action, slot, "modifiers");
	gint              n, i;

	if (lrg_editor_document_get_kind (doc_of (self), seq) != LRG_EDITOR_DOCUMENT_NODE_SEQUENCE)
		return FALSE;
	n = lrg_editor_document_get_length (doc_of (self), seq);
	for (i = 0; i < n; i++)
	{
		g_autofree gchar *idx = g_strdup_printf ("%d", i);
		g_autofree gchar *p = lrg_editor_document_path_join (seq, idx);
		const gchar      *v = lrg_editor_document_get_scalar (doc_of (self), p);

		if (v != NULL && (g_ascii_strcasecmp (v, mod) == 0 ||
		                  (g_ascii_strcasecmp (mod, "CTRL") == 0 && g_ascii_strcasecmp (v, "CONTROL") == 0)))
			return TRUE;
	}
	return FALSE;
}

/* ==========================================================================
 * Describing bindings
 * ========================================================================== */

typedef enum
{
	KIND_KEYBOARD,
	KIND_MOUSE,
	KIND_PAD,
	KIND_AXIS,
	KIND_UNKNOWN
} BindingKind;

static BindingKind
binding_kind (LrgEditorInputMapEditor *self,
              const gchar             *action,
              gint                     slot)
{
	const gchar *type = binding_field (self, action, slot, "type");

	if (type == NULL)
		return KIND_UNKNOWN;
	if (g_ascii_strcasecmp (type, "keyboard") == 0)
		return KIND_KEYBOARD;
	if (g_ascii_strcasecmp (type, "mouse_button") == 0)
		return KIND_MOUSE;
	if (g_ascii_strcasecmp (type, "gamepad_button") == 0)
		return KIND_PAD;
	if (g_ascii_strcasecmp (type, "gamepad_axis") == 0)
		return KIND_AXIS;
	return KIND_UNKNOWN;
}

gchar *
lrg_editor_input_map_editor_describe_binding (LrgEditorInputMapEditor *self,
                                              const gchar             *action,
                                              gint                     slot)
{
	GString          *s;
	const gchar      *v;
	g_autofree gchar *pretty = NULL;
	guint             i;

	g_return_val_if_fail (LRG_IS_EDITOR_INPUT_MAP_EDITOR (self), NULL);
	g_return_val_if_fail (action != NULL, NULL);
	if (slot < 0 || slot >= n_bindings (self, action))
		return NULL;

	s = g_string_new (NULL);
	switch (binding_kind (self, action, slot))
	{
	case KIND_KEYBOARD:
	case KIND_MOUSE:
		if (binding_has_modifier (self, action, slot, "CTRL"))
			g_string_append (s, "Ctrl+");
		if (binding_has_modifier (self, action, slot, "ALT"))
			g_string_append (s, "Alt+");
		if (binding_has_modifier (self, action, slot, "SHIFT"))
			g_string_append (s, "Shift+");
		if (binding_kind (self, action, slot) == KIND_MOUSE)
		{
			g_string_append (s, "Mouse ");
			v = binding_field (self, action, slot, "button");
		}
		else
			v = binding_field (self, action, slot, "key");
		pretty = pretty_name (v != NULL ? v : "?");
		g_string_append (s, pretty);
		break;
	case KIND_PAD:
		v = binding_field (self, action, slot, "button");
		g_string_append (s, "Pad ");
		for (i = 0; pad_buttons[i].name != NULL; i++)
			if (v != NULL && g_ascii_strcasecmp (v, pad_buttons[i].name) == 0)
				break;
		if (pad_buttons[i].name != NULL)
			g_string_append (s, pad_buttons[i].label);
		else
		{
			pretty = pretty_name (v != NULL ? v : "?");
			g_string_append (s, pretty);
		}
		break;
	case KIND_AXIS:
		{
			g_autofree gchar *pp = binding_path (action, slot, "positive");
			g_auto(GValue)    pv = G_VALUE_INIT;
			gboolean          positive = TRUE;

			v = binding_field (self, action, slot, "axis");
			for (i = 0; pad_axes[i].name != NULL; i++)
				if (v != NULL && g_ascii_strcasecmp (v, pad_axes[i].name) == 0)
					break;
			g_string_append (s, pad_axes[i].name != NULL ? pad_axes[i].label : (v != NULL ? v : "Axis"));
			if (lrg_editor_document_get_value (doc_of (self), pp, &pv) && G_VALUE_HOLDS_BOOLEAN (&pv))
				positive = g_value_get_boolean (&pv);
			g_string_append (s, positive ? "+" : "\xe2\x88\x92");    /* "−" */
		}
		break;
	default:
		g_string_append (s, "Unknown");
		break;
	}
	return g_string_free (s, FALSE);
}

/* ==========================================================================
 * The bindings cell: chips painted and hit-tested here
 * ========================================================================== */

typedef struct
{
	LrgEditorInputMapEditor *editor;   /* weak (the editor outlives its table) */
	gchar                   *action;
} BindingCell;

static void
binding_cell_free (gpointer data)
{
	BindingCell *c = data;

	g_free (c->action);
	g_free (c);
}

/* Chip geometry: fills @rects (x, w pairs) for the bindings and the "+" chip. */
static GArray *
chip_layout (LrgEditorInputMapEditor *self,
             LrgWidget               *cell,
             const gchar             *action,
             gfloat                   x0)
{
	LrgTheme *theme = lrg_widget_get_theme (cell);
	GrlFont  *font = lrg_theme_get_ui_font (theme);
	gfloat    fs = lrg_theme_get_metric (theme, LRG_THEME_METRIC_FONT_SIZE_SMALL);
	GArray   *out = g_array_new (FALSE, FALSE, sizeof (gfloat));
	gfloat    x = x0 + 2.0f;
	gint      n = n_bindings (self, action);
	gint      i;

	for (i = 0; i <= n; i++)
	{
		g_autofree gchar *label = (i < n) ? lrg_editor_input_map_editor_describe_binding (self, action, i) : NULL;
		gfloat            tw = 0.0f, th = 0.0f, w;

		if (self->capturing && g_strcmp0 (self->cap_action, action) == 0 &&
		    (self->cap_slot == i || (self->cap_slot < 0 && i == n)))
		{
			g_free (label);
			label = g_strdup ("Press a key\xe2\x80\xa6");
		}
		if (label != NULL)
			lrg_paint_measure_text (font, label, fs, 0.0f, &tw, &th);
		w = (label != NULL) ? tw + 20.0f : fs + 12.0f;
		g_array_append_val (out, x);
		g_array_append_val (out, w);
		x += w + 6.0f;
	}
	return out;
}

static void
binding_cell_paint (LrgCustomWidget *widget,
                    LrgPaintList    *list,
                    gfloat           x,
                    gfloat           y,
                    gfloat           width,
                    gfloat           height,
                    gpointer         user_data)
{
	BindingCell             *c = user_data;
	LrgEditorInputMapEditor *self = c->editor;
	LrgWidget               *w = LRG_WIDGET (widget);
	LrgTheme                *theme = lrg_widget_get_theme (w);
	GrlFont                 *font = lrg_theme_get_ui_font (theme);
	gfloat                   fs = lrg_theme_get_metric (theme, LRG_THEME_METRIC_FONT_SIZE_SMALL);
	gfloat                   ch = MIN (height - 6.0f, fs + 10.0f);
	gfloat                   cy = y + (height - ch) * 0.5f;
	g_autoptr(GArray)        rects = NULL;
	gint                     n, i;

	if (self == NULL || c->action == NULL || !has_action (self, c->action))
		return;
	n = n_bindings (self, c->action);
	rects = chip_layout (self, w, c->action, x);
	lrg_paint_list_push_clip (list, x, y, width, height);
	for (i = 0; i <= n; i++)
	{
		gfloat            cx = g_array_index (rects, gfloat, i * 2);
		gfloat            cw = g_array_index (rects, gfloat, i * 2 + 1);
		gboolean          selected = g_strcmp0 (self->sel_action, c->action) == 0 && self->sel_slot == i;
		gboolean          capture = self->capturing && g_strcmp0 (self->cap_action, c->action) == 0 &&
		                            (self->cap_slot == i || (self->cap_slot < 0 && i == n));
		LrgThemeColor     role;
		const GrlColor   *tint;
		GrlColor          fill, border;
		g_autofree gchar *label = NULL;

		if (i == n && !capture)
		{
			/* the "+" chip: a dashed-looking hollow pill */
			lrg_paint_color_with_alpha (lrg_theme_get_color (theme, LRG_THEME_COLOR_BORDER), 0.8f, &border);
			lrg_paint_list_border (list, cx, cy, cw, ch, ch * 0.5f, 1.0f, &border);
			lrg_paint_list_icon (list, LRG_ICON_PLUS, cx + (cw - fs) * 0.5f, cy + (ch - fs) * 0.5f, fs, 0.0f,
			                     lrg_theme_get_color (theme, LRG_THEME_COLOR_TEXT_MUTED));
			continue;
		}
		switch (capture ? KIND_UNKNOWN : binding_kind (self, c->action, i))
		{
		case KIND_KEYBOARD:
			role = LRG_THEME_COLOR_ACCENT;
			break;
		case KIND_MOUSE:
			role = LRG_THEME_COLOR_LINK;
			break;
		case KIND_PAD:
		case KIND_AXIS:
			role = LRG_THEME_COLOR_SUCCESS;
			break;
		default:
			role = LRG_THEME_COLOR_WARNING;
			break;
		}
		tint = lrg_theme_get_color (theme, role);
		lrg_paint_color_with_alpha (tint, selected || capture ? 0.32f : 0.16f, &fill);
		lrg_paint_color_with_alpha (tint, selected || capture ? 0.95f : 0.45f, &border);
		lrg_paint_list_rect (list, cx, cy, cw, ch, ch * 0.5f, &fill);
		lrg_paint_list_border (list, cx, cy, cw, ch, ch * 0.5f, selected ? 2.0f : 1.0f, &border);
		label = capture ? g_strdup ("Press a key\xe2\x80\xa6")
		                : lrg_editor_input_map_editor_describe_binding (self, c->action, i);
		lrg_paint_list_text (list, font, label, cx + 10.0f, cy + (ch - fs) * 0.5f, fs, 0.0f,
		                     lrg_theme_get_color (theme, LRG_THEME_COLOR_TEXT));
	}
	lrg_paint_list_pop_clip (list);
}

static gboolean
binding_cell_event (LrgCustomWidget  *widget,
                    const LrgUIEvent *event,
                    gpointer          user_data)
{
	BindingCell             *c = user_data;
	LrgEditorInputMapEditor *self = c->editor;
	g_autoptr(GArray)        rects = NULL;
	gfloat                   px;
	gint                     n, i;

	if (self == NULL || c->action == NULL || lrg_ui_event_get_event_type (event) != LRG_UI_EVENT_MOUSE_BUTTON_DOWN ||
	    lrg_ui_event_get_button (event) != 0 || self->capturing)
		return FALSE;
	n = n_bindings (self, c->action);
	rects = chip_layout (self, LRG_WIDGET (widget), c->action, lrg_widget_get_world_x (LRG_WIDGET (widget)));
	px = lrg_ui_event_get_x (event);
	for (i = 0; i <= n; i++)
	{
		gfloat cx = g_array_index (rects, gfloat, i * 2);
		gfloat cw = g_array_index (rects, gfloat, i * 2 + 1);

		if (px < cx || px > cx + cw)
			continue;
		if (i == n)
			lrg_editor_input_map_editor_start_capture (self, c->action, -1);
		else if (g_strcmp0 (self->sel_action, c->action) == 0 && self->sel_slot == i)
			lrg_editor_input_map_editor_start_capture (self, c->action, i);
		else
			lrg_editor_input_map_editor_select (self, c->action, i);
		return TRUE;
	}
	/* empty space: the row (and the action) */
	lrg_editor_input_map_editor_select (self, c->action, -1);
	return TRUE;
}

static LrgWidget *
binding_cell_create (LrgTable *table,
                     guint     column,
                     gpointer  user_data)
{
	static const LrgCustomWidgetFuncs funcs = { NULL, binding_cell_paint, NULL, binding_cell_event, { NULL } };
	LrgCustomWidget *w = lrg_custom_widget_new ();
	BindingCell     *c = g_new0 (BindingCell, 1);

	c->editor = LRG_EDITOR_INPUT_MAP_EDITOR (user_data);
	lrg_widget_set_name (LRG_WIDGET (w), "input-bindings-cell");
	lrg_custom_widget_set_funcs (w, &funcs, c, binding_cell_free);
	g_object_set_data (G_OBJECT (w), "lrg-binding-cell", c);
	return LRG_WIDGET (w);
}

static gchar *
action_cell_text (LrgTable  *table,
                  LrgWidget *cell,
                  GObject   *item,
                  guint      position,
                  gpointer   user_data)
{
	return g_strdup (INPUT_ACTION_ROW (item)->name);
}

static gchar *
bindings_cell_text (LrgTable  *table,
                    LrgWidget *cell,
                    GObject   *item,
                    guint      position,
                    gpointer   user_data)
{
	BindingCell *c = (cell != NULL) ? g_object_get_data (G_OBJECT (cell), "lrg-binding-cell") : NULL;

	if (c != NULL)
	{
		g_free (c->action);
		c->action = g_strdup (INPUT_ACTION_ROW (item)->name);
		lrg_widget_queue_layout (cell);
	}
	return NULL;
}

/* ==========================================================================
 * Model
 * ========================================================================== */

static gint
row_index (LrgEditorInputMapEditor *self,
           const gchar             *action)
{
	guint i, n = g_list_model_get_n_items (G_LIST_MODEL (self->rows));

	for (i = 0; i < n; i++)
	{
		g_autoptr(InputActionRow) row = g_list_model_get_item (G_LIST_MODEL (self->rows), i);

		if (g_strcmp0 (row->name, action) == 0)
			return (gint) i;
	}
	return -1;
}

/* Rebuilds the rows from the document, keeping the selection by name. */
static void
rebuild_rows (LrgEditorInputMapEditor *self)
{
	g_auto(GStrv)  actions = lrg_editor_document_input_actions (doc_of (self));
	g_autoptr(GPtrArray) items = g_ptr_array_new_with_free_func (g_object_unref);
	guint          i;
	gint           sel;

	for (i = 0; actions != NULL && actions[i] != NULL; i++)
	{
		InputActionRow *row = g_object_new (INPUT_TYPE_ACTION_ROW, NULL);

		row->name = g_strdup (actions[i]);
		g_ptr_array_add (items, row);
	}
	self->syncing = TRUE;
	g_list_store_splice (self->rows, 0, g_list_model_get_n_items (G_LIST_MODEL (self->rows)), items->pdata,
	                     items->len);

	/* the selection follows the action by name; a removed one clears it */
	if (self->sel_action != NULL && !has_action (self, self->sel_action))
	{
		g_clear_pointer (&self->sel_action, g_free);
		self->sel_slot = -1;
	}
	if (self->sel_action != NULL && self->sel_slot >= n_bindings (self, self->sel_action))
		self->sel_slot = -1;
	sel = row_index (self, self->sel_action);
	if (sel >= 0)
		lrg_table_select (self->table, (guint) sel);
	else
		lrg_table_unselect_all (self->table);
	self->syncing = FALSE;
	lrg_widget_queue_layout (LRG_WIDGET (self->table));
}

static void
on_table_selection (LrgTable *table,
                    gpointer  user_data)
{
	LrgEditorInputMapEditor  *self = LRG_EDITOR_INPUT_MAP_EDITOR (user_data);
	guint                     pos = lrg_table_get_selected (table);
	g_autoptr(InputActionRow) row = NULL;

	if (self->syncing || pos >= g_list_model_get_n_items (G_LIST_MODEL (self->rows)))
		return;
	row = g_list_model_get_item (G_LIST_MODEL (self->rows), pos);
	if (g_strcmp0 (row->name, self->sel_action) != 0)
		lrg_editor_input_map_editor_select (self, row->name, -1);
}

/* ==========================================================================
 * The grid: the selected action or binding
 * ========================================================================== */

/* Read hook: the action name, the modifier flags. */
static gboolean
source_read (LrgEditorDataSource *source,
             const gchar         *name,
             GValue              *value,
             gpointer             user_data)
{
	LrgEditorInputMapEditor *self = LRG_EDITOR_INPUT_MAP_EDITOR (user_data);

	if (self->sel_action == NULL)
		return FALSE;
	if (g_strcmp0 (name, "action-name") == 0)
	{
		g_value_set_string (value, self->sel_action);
		return TRUE;
	}
	if (g_str_has_prefix (name, "mod-") && self->sel_slot >= 0)
	{
		g_autofree gchar *mod = g_ascii_strup (name + 4, -1);

		g_value_set_boolean (value, binding_has_modifier (self, self->sel_action, self->sel_slot, mod));
		return TRUE;
	}
	return FALSE;
}

/* The YAML of a fresh binding of @type. */
static const gchar *
default_binding (const gchar *type)
{
	if (g_strcmp0 (type, "mouse_button") == 0)
		return "{ type: mouse_button, button: LEFT }";
	if (g_strcmp0 (type, "gamepad_button") == 0)
		return "{ type: gamepad_button, gamepad: 0, button: RIGHT_FACE_DOWN }";
	if (g_strcmp0 (type, "gamepad_axis") == 0)
		return "{ type: gamepad_axis, gamepad: 0, axis: LEFT_X, threshold: 0.5, positive: true }";
	return "{ type: keyboard, key: SPACE }";
}

/* Writes the modifiers sequence of the selected binding with @mod set or
 * cleared (removing the key when it becomes empty). */
static gboolean
write_modifier (LrgEditorInputMapEditor  *self,
                const gchar              *mod,
                gboolean                  on,
                GError                  **error)
{
	static const gchar * const order[] = { "CTRL", "ALT", "SHIFT", NULL };
	g_autofree gchar *seq = binding_path (self->sel_action, self->sel_slot, "modifiers");
	g_autofree gchar *bind = binding_path (self->sel_action, self->sel_slot, NULL);
	GString          *yaml = g_string_new ("[");
	gboolean          any = FALSE;
	gboolean          ok;
	guint             i;

	for (i = 0; order[i] != NULL; i++)
	{
		gboolean set = (g_strcmp0 (order[i], mod) == 0) ? on
		               : binding_has_modifier (self, self->sel_action, self->sel_slot, order[i]);

		if (!set)
			continue;
		g_string_append_printf (yaml, "%s%s", any ? ", " : "", order[i]);
		any = TRUE;
	}
	g_string_append_c (yaml, ']');
	if (!any)
		ok = !lrg_editor_document_has_path (doc_of (self), seq) || lrg_editor_document_remove (doc_of (self), seq,
		                                                                                         error);
	else if (lrg_editor_document_has_path (doc_of (self), seq))
		ok = lrg_editor_document_replace (doc_of (self), seq, yaml->str, error);
	else
		ok = lrg_editor_document_add_key (doc_of (self), bind, "modifiers", yaml->str, -1, error);
	g_string_free (yaml, TRUE);
	return ok;
}

/* Write hook: renames, type changes and modifier flags are structural. */
static gboolean
source_write (LrgEditorDataSource *source,
              const gchar         *name,
              GValue              *value,
              gpointer             user_data)
{
	LrgEditorInputMapEditor *self = LRG_EDITOR_INPUT_MAP_EDITOR (user_data);
	g_autoptr(GError)        error = NULL;
	gboolean                 ok = TRUE;

	if (self->sel_action == NULL)
		return FALSE;
	if (g_strcmp0 (name, "action-name") == 0)
	{
		const gchar      *to = g_value_get_string (value);
		g_autofree gchar *from = g_strdup (self->sel_action);

		if (to == NULL || *to == '\0' || g_strcmp0 (to, from) == 0)
			return FALSE;
		g_free (self->sel_action);
		self->sel_action = g_strdup (to);
		if (!lrg_editor_document_rename_key (doc_of (self), "/actions", from, to, &error))
		{
			g_free (self->sel_action);
			self->sel_action = g_steal_pointer (&from);
			ok = FALSE;
		}
	}
	else if (g_strcmp0 (name, "type") == 0 && self->sel_slot >= 0)
	{
		g_autofree gchar *bind = binding_path (self->sel_action, self->sel_slot, NULL);
		const gchar      *cur = binding_field (self, self->sel_action, self->sel_slot, "type");

		if (g_strcmp0 (cur, g_value_get_string (value)) == 0)
			return FALSE;
		ok = lrg_editor_document_replace (doc_of (self), bind, default_binding (g_value_get_string (value)), &error);
	}
	else if (g_str_has_prefix (name, "mod-") && self->sel_slot >= 0)
	{
		g_autofree gchar *mod = g_ascii_strup (name + 4, -1);

		if (binding_has_modifier (self, self->sel_action, self->sel_slot, mod) == g_value_get_boolean (value))
			return FALSE;
		ok = write_modifier (self, mod, g_value_get_boolean (value), &error);
	}
	else
		return TRUE;    /* a plain scalar: the source writes it */

	if (!ok)
		lrg_editor_data_editor_report_error (LRG_EDITOR_DATA_EDITOR (self), error);
	return FALSE;
}

/* Fills the source with the fields of the current selection. */
static void
rebuild_grid (LrgEditorInputMapEditor *self)
{
	g_autofree gchar *title = NULL;
	gboolean          shown = self->sel_action != NULL;

	lrg_editor_data_source_clear (self->source);
	if (self->sel_action != NULL && self->sel_slot < 0)
	{
		title = g_strdup_printf ("Action \xc2\xb7 %s", self->sel_action);
		lrg_editor_data_source_add_field (self->source, "action-name", "", G_TYPE_STRING, "Name", "Action", NULL,
		                                  NULL);
	}
	else if (self->sel_action != NULL)
	{
		g_autofree gchar *desc = lrg_editor_input_map_editor_describe_binding (self, self->sel_action,
		                                                                       self->sel_slot);
		g_autofree gchar *p_type = binding_path (self->sel_action, self->sel_slot, "type");
		BindingKind       kind = binding_kind (self, self->sel_action, self->sel_slot);

		title = g_strdup_printf ("%s \xc2\xb7 binding %d \xc2\xb7 %s", self->sel_action, self->sel_slot + 1,
		                         desc != NULL ? desc : "");
		lrg_editor_data_source_add_field (self->source, "type", p_type, G_TYPE_STRING, "Type", "Binding",
		                                  "enum-strings:keyboard,mouse_button,gamepad_button,gamepad_axis",
		                                  "keyboard");
		if (kind == KIND_KEYBOARD || kind == KIND_MOUSE)
		{
			g_autofree gchar *field = binding_path (self->sel_action, self->sel_slot,
			                                        kind == KIND_KEYBOARD ? "key" : "button");
			g_autofree gchar *hint = enum_hint (kind == KIND_KEYBOARD ? key_names : mouse_names);

			lrg_editor_data_source_add_field (self->source, kind == KIND_KEYBOARD ? "key" : "button", field,
			                                  G_TYPE_STRING, kind == KIND_KEYBOARD ? "Key" : "Button", "Binding",
			                                  hint, NULL);
			lrg_editor_data_source_add_field (self->source, "mod-ctrl", "", G_TYPE_BOOLEAN, "Ctrl", "Modifiers",
			                                  NULL, NULL);
			lrg_editor_data_source_add_field (self->source, "mod-alt", "", G_TYPE_BOOLEAN, "Alt", "Modifiers",
			                                  NULL, NULL);
			lrg_editor_data_source_add_field (self->source, "mod-shift", "", G_TYPE_BOOLEAN, "Shift", "Modifiers",
			                                  NULL, NULL);
		}
		else if (kind == KIND_PAD || kind == KIND_AXIS)
		{
			g_autofree gchar *p_pad = binding_path (self->sel_action, self->sel_slot, "gamepad");
			GString          *hint = g_string_new ("enum-strings:");
			guint             i;

			lrg_editor_data_source_add_field (self->source, "gamepad", p_pad, G_TYPE_INT64, "Gamepad", "Binding",
			                                  "range:0:3:1", "0");
			if (kind == KIND_PAD)
			{
				g_autofree gchar *p = binding_path (self->sel_action, self->sel_slot, "button");

				for (i = 0; pad_buttons[i].name != NULL; i++)
					g_string_append_printf (hint, "%s%s", i > 0 ? "," : "", pad_buttons[i].name);
				lrg_editor_data_source_add_field (self->source, "button", p, G_TYPE_STRING, "Button", "Binding",
				                                  hint->str, NULL);
			}
			else
			{
				g_autofree gchar *p = binding_path (self->sel_action, self->sel_slot, "axis");
				g_autofree gchar *pt = binding_path (self->sel_action, self->sel_slot, "threshold");
				g_autofree gchar *pp = binding_path (self->sel_action, self->sel_slot, "positive");

				for (i = 0; pad_axes[i].name != NULL; i++)
					g_string_append_printf (hint, "%s%s", i > 0 ? "," : "", pad_axes[i].name);
				lrg_editor_data_source_add_field (self->source, "axis", p, G_TYPE_STRING, "Axis", "Binding",
				                                  hint->str, NULL);
				lrg_editor_data_source_add_field (self->source, "threshold", pt, G_TYPE_DOUBLE, "Threshold",
				                                  "Binding", "range:0:1:0.05", "0.5");
				lrg_editor_data_source_add_field (self->source, "positive", pp, G_TYPE_BOOLEAN, "Positive",
				                                  "Binding", NULL, "true");
			}
			g_string_free (hint, TRUE);
		}
	}
	g_free (self->grid_sig);
	self->grid_sig = grid_signature (self);
	self->grid_stale = FALSE;
	lrg_label_set_text (self->grid_title, title != NULL ? title : "Bindings");
	lrg_widget_set_visible (LRG_WIDGET (self->grid), shown);
	lrg_widget_set_visible (self->grid_hint, !shown);
	lrg_property_grid_set_source (self->grid, LRG_PROPERTY_SOURCE (self->source));
	lrg_property_grid_rebuild (self->grid);
}

/* ==========================================================================
 * Capture
 * ========================================================================== */

static void
set_capturing (LrgEditorInputMapEditor *self,
               gboolean                 on)
{
	LrgEditorApp *app = lrg_editor_data_editor_get_app (LRG_EDITOR_DATA_EDITOR (self));

	self->capturing = on;
	lrg_widget_set_visible (self->veil, on);
	if (!on)
		g_clear_pointer (&self->cap_action, g_free);
	/* key events must reach this editor first */
	if (on && app != NULL && app->canvas != NULL)
		lrg_canvas_set_focused_widget (app->canvas, LRG_WIDGET (self));
	lrg_widget_queue_layout (LRG_WIDGET (self->table));
	lrg_widget_queue_layout (LRG_WIDGET (self));
}

/* Writes @yaml as the captured binding (replace or append) and selects it. */
static void
finish_capture (LrgEditorInputMapEditor *self,
                const gchar             *yaml)
{
	g_autofree gchar *action = g_strdup (self->cap_action);
	gint              slot = self->cap_slot;
	g_autoptr(GError) error = NULL;
	gboolean          ok;

	set_capturing (self, FALSE);
	if (action == NULL)
		return;
	if (slot >= 0 && slot < n_bindings (self, action))
	{
		g_autofree gchar *bind = binding_path (action, slot, NULL);

		ok = lrg_editor_document_replace (doc_of (self), bind, yaml, &error);
	}
	else
	{
		g_autofree gchar *b = bindings_path (action);
		g_autofree gchar *a = action_path (action);

		if (lrg_editor_document_get_kind (doc_of (self), b) == LRG_EDITOR_DOCUMENT_NODE_SEQUENCE)
			ok = lrg_editor_document_insert_item (doc_of (self), b, -1, yaml, &error);
		else if (lrg_editor_document_has_path (doc_of (self), b))
		{
			g_autofree gchar *seq = g_strdup_printf ("[%s]", yaml);

			ok = lrg_editor_document_replace (doc_of (self), b, seq, &error);
		}
		else
		{
			g_autofree gchar *seq = g_strdup_printf ("[%s]", yaml);

			ok = lrg_editor_document_add_key (doc_of (self), a, "bindings", seq, -1, &error);
		}
		slot = n_bindings (self, action) - 1;
	}
	if (!ok)
	{
		lrg_editor_data_editor_report_error (LRG_EDITOR_DATA_EDITOR (self), error);
		return;
	}
	lrg_editor_input_map_editor_select (self, action, slot);
}

static gchar *
modifiers_yaml (LrgInputModifiers mods)
{
	GString *s;

	if ((mods & (LRG_INPUT_MODIFIER_CTRL | LRG_INPUT_MODIFIER_ALT | LRG_INPUT_MODIFIER_SHIFT)) == 0)
		return g_strdup ("");
	s = g_string_new (", modifiers: [");
	if (mods & LRG_INPUT_MODIFIER_CTRL)
		g_string_append (s, "CTRL, ");
	if (mods & LRG_INPUT_MODIFIER_ALT)
		g_string_append (s, "ALT, ");
	if (mods & LRG_INPUT_MODIFIER_SHIFT)
		g_string_append (s, "SHIFT, ");
	g_string_truncate (s, s->len - 2);
	g_string_append_c (s, ']');
	return g_string_free (s, FALSE);
}

static gboolean
is_modifier_key (GrlKey key)
{
	return key == GRL_KEY_LEFT_SHIFT || key == GRL_KEY_RIGHT_SHIFT || key == GRL_KEY_LEFT_CONTROL ||
	       key == GRL_KEY_RIGHT_CONTROL || key == GRL_KEY_LEFT_ALT || key == GRL_KEY_RIGHT_ALT ||
	       key == GRL_KEY_LEFT_SUPER || key == GRL_KEY_RIGHT_SUPER;
}

static gboolean
capture_event (LrgEditorInputMapEditor *self,
               const LrgUIEvent        *event)
{
	LrgUIEventType    type = lrg_ui_event_get_event_type (event);
	LrgInputModifiers mods = lrg_ui_event_get_modifiers (event);

	if (type == LRG_UI_EVENT_KEY_DOWN)
	{
		GrlKey            key = lrg_ui_event_get_key (event);
		const gchar      *name;
		g_autofree gchar *m = NULL;
		g_autofree gchar *yaml = NULL;

		if (key == GRL_KEY_ESCAPE && mods == 0)
		{
			lrg_editor_input_map_editor_cancel_capture (self);
			return TRUE;
		}
		/* a lone modifier waits for the real key */
		if (is_modifier_key (key))
			return TRUE;
		name = name_for (key_names, (gint) key);
		if (name == NULL)
			return TRUE;
		m = modifiers_yaml (mods);
		yaml = g_strdup_printf ("{ type: keyboard, key: %s%s }", name, m);
		finish_capture (self, yaml);
		return TRUE;
	}
	if (type == LRG_UI_EVENT_MOUSE_BUTTON_DOWN)
	{
		const gchar      *name = name_for (mouse_names, lrg_ui_event_get_button (event));
		g_autofree gchar *m = modifiers_yaml (mods);
		g_autofree gchar *yaml = NULL;

		if (name == NULL)
			return TRUE;
		yaml = g_strdup_printf ("{ type: mouse_button, button: %s%s }", name, m);
		finish_capture (self, yaml);
		return TRUE;
	}
	if (type == LRG_UI_EVENT_CANCEL)
	{
		lrg_editor_input_map_editor_cancel_capture (self);
		return TRUE;
	}
	return type == LRG_UI_EVENT_KEY_UP || type == LRG_UI_EVENT_MOUSE_BUTTON_UP || type == LRG_UI_EVENT_TEXT_INPUT ||
	       type == LRG_UI_EVENT_ACTIVATE;
}

/* The veil: a dimmed prompt over the content that also eats clicks (they
 * are recorded as mouse bindings). */
static void
veil_paint (LrgCustomWidget *widget,
            LrgPaintList    *list,
            gfloat           x,
            gfloat           y,
            gfloat           width,
            gfloat           height,
            gpointer         user_data)
{
	LrgEditorInputMapEditor *self = LRG_EDITOR_INPUT_MAP_EDITOR (user_data);
	LrgTheme                *theme = lrg_widget_get_theme (LRG_WIDGET (widget));
	GrlFont                 *font = lrg_theme_get_ui_font (theme);
	gfloat                   fs = lrg_theme_get_metric (theme, LRG_THEME_METRIC_FONT_SIZE_LARGE);
	gfloat                   small = lrg_theme_get_metric (theme, LRG_THEME_METRIC_FONT_SIZE_SMALL);
	const gchar             *line1 = "Press a key or mouse button";
	g_autofree gchar        *line2 = NULL;
	gfloat                   w1 = 0.0f, w2 = 0.0f, th = 0.0f, bw, bh, bx, by;
	GrlColor                 veil;

	line2 = g_strdup_printf ("for %s \xc2\xb7 Esc cancels \xc2\xb7 hold Ctrl / Alt / Shift for a chord",
	                         self->cap_action != NULL ? self->cap_action : "");
	lrg_paint_color_with_alpha (lrg_theme_get_color (theme, LRG_THEME_COLOR_VEIL), 0.55f, &veil);
	lrg_paint_list_rect (list, x, y, width, height, 0.0f, &veil);
	lrg_paint_measure_text (font, line1, fs, 0.0f, &w1, &th);
	lrg_paint_measure_text (font, line2, small, 0.0f, &w2, &th);
	bw = MAX (w1, w2) + 48.0f;
	bh = fs + small + 44.0f;
	bx = x + (width - bw) * 0.5f;
	by = y + (height - bh) * 0.5f;
	lrg_paint_list_rect (list, bx, by, bw, bh, 12.0f, lrg_theme_get_color (theme, LRG_THEME_COLOR_POPUP));
	lrg_paint_list_border (list, bx, by, bw, bh, 12.0f, 2.0f, lrg_theme_get_color (theme, LRG_THEME_COLOR_ACCENT));
	lrg_paint_list_text (list, font, line1, bx + (bw - w1) * 0.5f, by + 16.0f, fs, 0.0f,
	                     lrg_theme_get_color (theme, LRG_THEME_COLOR_TEXT));
	lrg_paint_list_text (list, font, line2, bx + (bw - w2) * 0.5f, by + 24.0f + fs, small, 0.0f,
	                     lrg_theme_get_color (theme, LRG_THEME_COLOR_TEXT_MUTED));
}

static gboolean
veil_event (LrgCustomWidget  *widget,
            const LrgUIEvent *event,
            gpointer          user_data)
{
	LrgEditorInputMapEditor *self = LRG_EDITOR_INPUT_MAP_EDITOR (user_data);

	if (!self->capturing)
		return FALSE;
	return capture_event (self, event);
}

/* ==========================================================================
 * Tools
 * ========================================================================== */

static void
on_add_action (LrgWidget *chip,
               gpointer   user_data)
{
	g_autoptr(GError) error = NULL;

	if (lrg_editor_input_map_editor_add_action (LRG_EDITOR_INPUT_MAP_EDITOR (user_data), NULL, &error) == NULL)
		lrg_editor_data_editor_report_error (LRG_EDITOR_DATA_EDITOR (user_data), error);
}

static void
on_capture (LrgWidget *chip,
            gpointer   user_data)
{
	LrgEditorInputMapEditor *self = LRG_EDITOR_INPUT_MAP_EDITOR (user_data);

	if (self->sel_action != NULL)
		lrg_editor_input_map_editor_start_capture (self, self->sel_action, self->sel_slot);
}

static void
on_remove (LrgWidget *chip,
           gpointer   user_data)
{
	lrg_editor_input_map_editor_remove_selected (LRG_EDITOR_INPUT_MAP_EDITOR (user_data));
}

/* ==========================================================================
 * LrgEditorDataEditor
 * ========================================================================== */

/* What the grid shows: action, slot and binding kind; a different
 * signature needs a rebuild, the same one only a refresh. */
static gchar *
grid_signature (LrgEditorInputMapEditor *self)
{
	return g_strdup_printf ("%s\n%d\n%d", self->sel_action != NULL ? self->sel_action : "",
	                        self->sel_slot, self->sel_action != NULL && self->sel_slot >= 0
	                        ? (gint) binding_kind (self, self->sel_action, self->sel_slot) : -1);
}

static void
input_map_refresh (LrgEditorDataEditor *editor,
                   const gchar         *path)
{
	LrgEditorInputMapEditor *self = LRG_EDITOR_INPUT_MAP_EDITOR (editor);
	g_autofree gchar        *sig = NULL;

	rebuild_rows (self);
	sig = grid_signature (self);
	if (g_strcmp0 (sig, self->grid_sig) != 0)
	{
		/* not now: the change may come from inside a grid row's edit */
		self->grid_stale = TRUE;
		return;
	}
	lrg_editor_data_source_document_changed (self->source, path);
	/* the modifier flags and the name live in other paths */
	lrg_property_grid_refresh (self->grid);
}

static void
input_map_reveal (LrgEditorDataEditor *editor,
                  const gchar         *path,
                  guint                line)
{
	LrgEditorInputMapEditor *self = LRG_EDITOR_INPUT_MAP_EDITOR (editor);
	g_auto(GStrv)            parts = lrg_editor_document_path_split (path, NULL);
	gint64                   slot = -1;

	/* actions / <name> [/ bindings / <i> ...] */
	if (parts == NULL || parts[0] == NULL || g_strcmp0 (parts[0], "actions") != 0 || parts[1] == NULL)
		return;
	if (parts[2] != NULL && g_strcmp0 (parts[2], "bindings") == 0 && parts[3] != NULL)
		g_ascii_string_to_signed (parts[3], 10, 0, G_MAXINT, &slot, NULL);
	lrg_editor_input_map_editor_select (self, parts[1], (gint) slot);
	{
		gint row = row_index (self, parts[1]);

		if (row >= 0)
			lrg_table_scroll_to (self->table, (guint) row, TRUE);
	}
}

static gchar *
input_map_get_state (LrgEditorDataEditor *editor)
{
	LrgEditorInputMapEditor *self = LRG_EDITOR_INPUT_MAP_EDITOR (editor);

	return g_strdup_printf ("actions=%u selected=%s slot=%d capturing=%s",
	                        g_list_model_get_n_items (G_LIST_MODEL (self->rows)),
	                        self->sel_action != NULL ? self->sel_action : "-", self->sel_slot,
	                        self->capturing ? "yes" : "no");
}

static gboolean
lrg_editor_input_map_editor_handle_event (LrgWidget        *widget,
                                          const LrgUIEvent *event)
{
	LrgEditorInputMapEditor *self = LRG_EDITOR_INPUT_MAP_EDITOR (widget);

	if (self->capturing && capture_event (self, event))
		return TRUE;
	if (!self->capturing && lrg_ui_event_get_event_type (event) == LRG_UI_EVENT_KEY_DOWN &&
	    lrg_ui_event_get_modifiers (event) == 0 && self->sel_action != NULL)
	{
		GrlKey key = lrg_ui_event_get_key (event);

		if (key == GRL_KEY_DELETE)
		{
			lrg_editor_input_map_editor_remove_selected (self);
			return TRUE;
		}
		if (key == GRL_KEY_ENTER && self->sel_slot >= 0 &&
		    lrg_ui_event_get_target (event) == widget)
		{
			lrg_editor_input_map_editor_start_capture (self, self->sel_action, self->sel_slot);
			return TRUE;
		}
	}
	return LRG_WIDGET_CLASS (lrg_editor_input_map_editor_parent_class)->handle_event (widget, event);
}

static gboolean
lrg_editor_input_map_editor_step (LrgWidget *widget,
                                  gfloat     delta_time)
{
	LrgEditorInputMapEditor *self = LRG_EDITOR_INPUT_MAP_EDITOR (widget);
	gboolean                 busy = LRG_WIDGET_CLASS (lrg_editor_input_map_editor_parent_class)->step (widget,
	                                                                                                   delta_time);

	if (self->grid_stale)
		rebuild_grid (self);
	return busy;
}

static void
lrg_editor_input_map_editor_constructed (GObject *object)
{
	LrgEditorInputMapEditor *self = LRG_EDITOR_INPUT_MAP_EDITOR (object);
	LrgEditorDataEditor     *base = LRG_EDITOR_DATA_EDITOR (object);
	static const LrgCustomWidgetFuncs veil_funcs = { NULL, veil_paint, NULL, veil_event, { NULL } };
	LrgOverlay              *overlay;
	LrgSplitter             *split;
	LrgVBox                 *side;
	LrgCustomWidget         *veil;
	guint                    col;

	G_OBJECT_CLASS (lrg_editor_input_map_editor_parent_class)->constructed (object);

	/* the action table */
	self->rows = g_list_store_new (INPUT_TYPE_ACTION_ROW);
	self->table = lrg_table_new ();
	lrg_widget_set_name (LRG_WIDGET (self->table), "input-actions");
	lrg_table_add_column (self->table, "Action", action_cell_text, NULL, NULL, 170.0f, LRG_TABLE_COLUMN_RESIZABLE);
	col = lrg_table_add_column (self->table, "Bindings", bindings_cell_text, NULL, NULL, 320.0f,
	                            LRG_TABLE_COLUMN_EXPAND);
	lrg_table_set_column_widget_factory (self->table, col, binding_cell_create, self, NULL);
	lrg_table_set_striped (self->table, TRUE);
	lrg_table_set_model (self->table, G_LIST_MODEL (self->rows));
	g_signal_connect_object (self->table, "selection-changed", G_CALLBACK (on_table_selection), self, 0);

	/* the grid side */
	side = lrg_vbox_new ();
	lrg_widget_set_name (LRG_WIDGET (side), "input-side");
	lrg_container_set_padding (LRG_CONTAINER (side), 6.0f);
	lrg_container_set_spacing (LRG_CONTAINER (side), 6.0f);
	self->grid_title = lrg_label_new ("Bindings");
	lrg_widget_set_name (LRG_WIDGET (self->grid_title), "input-grid-title");
	lrg_container_add_child (LRG_CONTAINER (side), LRG_WIDGET (self->grid_title));
	self->source = lrg_editor_data_source_new (base);
	lrg_editor_data_source_set_read_func (self->source, source_read, self, NULL);
	lrg_editor_data_source_set_write_func (self->source, source_write, self, NULL);
	self->grid = lrg_property_grid_new ();
	lrg_widget_set_name (LRG_WIDGET (self->grid), "input-grid");
	lrg_widget_set_fill_weight (LRG_WIDGET (self->grid), 1.0f);
	lrg_property_grid_set_label_width (self->grid, 96.0f);
	lrg_property_grid_set_source (self->grid, LRG_PROPERTY_SOURCE (self->source));
	lrg_editor_data_source_bind_grid (self->source, self->grid);
	lrg_container_add_child (LRG_CONTAINER (side), LRG_WIDGET (self->grid));
	self->grid_hint = editor_panel_hint_new ("input-grid-hint",
	                                         "Select an action or a binding. Click a selected binding again "
	                                         "(or press Enter) to record a new key; + adds one.");
	lrg_widget_set_fill_weight (self->grid_hint, 1.0f);
	lrg_container_add_child (LRG_CONTAINER (side), self->grid_hint);

	split = lrg_splitter_new (LRG_ORIENTATION_HORIZONTAL);
	lrg_widget_set_name (LRG_WIDGET (split), "input-split");
	lrg_splitter_set_start_child (split, LRG_WIDGET (self->table));
	lrg_splitter_set_end_child (split, LRG_WIDGET (side));
	lrg_splitter_set_default_fraction (split, 0.62f);
	lrg_splitter_set_fraction (split, 0.62f, FALSE);

	/* the capture veil above everything */
	veil = lrg_custom_widget_new ();
	lrg_widget_set_name (LRG_WIDGET (veil), "input-capture");
	lrg_custom_widget_set_funcs (veil, &veil_funcs, self, NULL);
	self->veil = LRG_WIDGET (veil);
	lrg_widget_set_visible (self->veil, FALSE);

	overlay = lrg_overlay_new ();
	lrg_container_add_child (LRG_CONTAINER (overlay), LRG_WIDGET (split));
	lrg_container_add_child (LRG_CONTAINER (overlay), self->veil);
	lrg_editor_data_editor_set_content (base, LRG_WIDGET (overlay));

	/* tools */
	lrg_editor_data_editor_add_tool (base, editor_chip_new ("input-add-action", LRG_ICON_PLUS, "Action",
	                                                        on_add_action, self));
	lrg_editor_data_editor_add_tool (base, editor_chip_new ("input-capture-tool", LRG_ICON_EDIT, "Capture",
	                                                        on_capture, self));
	lrg_editor_data_editor_add_tool (base, editor_chip_new ("input-remove", LRG_ICON_TRASH, "Remove", on_remove,
	                                                        self));

	rebuild_rows (self);
	rebuild_grid (self);
}

static void
lrg_editor_input_map_editor_finalize (GObject *object)
{
	LrgEditorInputMapEditor *self = LRG_EDITOR_INPUT_MAP_EDITOR (object);

	g_clear_object (&self->rows);
	g_clear_object (&self->source);
	g_free (self->sel_action);
	g_free (self->cap_action);
	g_free (self->grid_sig);
	G_OBJECT_CLASS (lrg_editor_input_map_editor_parent_class)->finalize (object);
}

static void
lrg_editor_input_map_editor_class_init (LrgEditorInputMapEditorClass *klass)
{
	GObjectClass             *object_class = G_OBJECT_CLASS (klass);
	LrgWidgetClass           *widget_class = LRG_WIDGET_CLASS (klass);
	LrgEditorDataEditorClass *data_class = LRG_EDITOR_DATA_EDITOR_CLASS (klass);

	object_class->constructed = lrg_editor_input_map_editor_constructed;
	object_class->finalize = lrg_editor_input_map_editor_finalize;
	widget_class->handle_event = lrg_editor_input_map_editor_handle_event;
	widget_class->step = lrg_editor_input_map_editor_step;
	data_class->refresh = input_map_refresh;
	data_class->reveal = input_map_reveal;
	data_class->get_state = input_map_get_state;
}

static void
lrg_editor_input_map_editor_init (LrgEditorInputMapEditor *self)
{
	self->sel_slot = -1;
	self->cap_slot = -1;
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

LrgEditorInputMapEditor *
lrg_editor_input_map_editor_new (LrgEditorApp      *app,
                                 LrgEditorDocument *document)
{
	g_return_val_if_fail (app == NULL || LRG_IS_EDITOR_APP (app), NULL);
	g_return_val_if_fail (LRG_IS_EDITOR_DOCUMENT (document), NULL);
	return g_object_new (LRG_TYPE_EDITOR_INPUT_MAP_EDITOR, "app", app, "document", document, NULL);
}

gboolean
lrg_editor_input_map_editor_select (LrgEditorInputMapEditor *self,
                                    const gchar             *action,
                                    gint                     slot)
{
	gint row;

	g_return_val_if_fail (LRG_IS_EDITOR_INPUT_MAP_EDITOR (self), FALSE);

	if (action != NULL && (!has_action (self, action) || slot >= n_bindings (self, action)))
		return FALSE;
	if (action != self->sel_action)
	{
		g_free (self->sel_action);
		self->sel_action = g_strdup (action);
	}
	self->sel_slot = (action != NULL) ? MAX (slot, -1) : -1;
	row = row_index (self, action);
	self->syncing = TRUE;
	if (row >= 0)
		lrg_table_select (self->table, (guint) row);
	else
		lrg_table_unselect_all (self->table);
	self->syncing = FALSE;
	rebuild_grid (self);
	lrg_widget_queue_layout (LRG_WIDGET (self->table));
	return TRUE;
}

const gchar *
lrg_editor_input_map_editor_get_selected_action (LrgEditorInputMapEditor *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_INPUT_MAP_EDITOR (self), NULL);
	return self->sel_action;
}

gint
lrg_editor_input_map_editor_get_selected_slot (LrgEditorInputMapEditor *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_INPUT_MAP_EDITOR (self), -1);
	return self->sel_slot;
}

const gchar *
lrg_editor_input_map_editor_add_action (LrgEditorInputMapEditor  *self,
                                        const gchar              *name,
                                        GError                  **error)
{
	LrgEditorDocument *doc;
	g_autofree gchar  *fresh = NULL;
	guint              i;

	g_return_val_if_fail (LRG_IS_EDITOR_INPUT_MAP_EDITOR (self), NULL);
	doc = doc_of (self);

	if (name == NULL)
	{
		for (i = 1; fresh == NULL || has_action (self, fresh); i++)
		{
			g_free (fresh);
			fresh = g_strdup_printf ("action_%u", i);
		}
		name = fresh;
	}
	if (lrg_editor_document_get_kind (doc, "/actions") != LRG_EDITOR_DOCUMENT_NODE_MAPPING)
	{
		g_autofree gchar *yaml = g_strdup_printf ("{ %s: { bindings: [] } }", name);

		if (lrg_editor_document_has_path (doc, "/actions"))
		{
			if (!lrg_editor_document_replace (doc, "/actions", yaml, error))
				return NULL;
		}
		else if (!lrg_editor_document_add_key (doc, "", "actions", yaml, -1, error))
			return NULL;
	}
	else if (!lrg_editor_document_add_key (doc, "/actions", name, "{ bindings: [] }", -1, error))
		return NULL;
	lrg_editor_input_map_editor_select (self, name, -1);
	return self->sel_action;
}

gboolean
lrg_editor_input_map_editor_remove_selected (LrgEditorInputMapEditor *self)
{
	g_autofree gchar *path = NULL;
	g_autoptr(GError) error = NULL;
	gint              slot;

	g_return_val_if_fail (LRG_IS_EDITOR_INPUT_MAP_EDITOR (self), FALSE);
	if (self->sel_action == NULL)
		return FALSE;
	slot = self->sel_slot;
	path = (slot >= 0) ? binding_path (self->sel_action, slot, NULL) : action_path (self->sel_action);
	if (!lrg_editor_document_remove (doc_of (self), path, &error))
	{
		lrg_editor_data_editor_report_error (LRG_EDITOR_DATA_EDITOR (self), error);
		return FALSE;
	}
	/* keep a neighbouring binding selected */
	if (slot >= 0 && self->sel_action != NULL)
		lrg_editor_input_map_editor_select (self, self->sel_action,
		                                    MIN (slot, n_bindings (self, self->sel_action) - 1));
	return TRUE;
}

gboolean
lrg_editor_input_map_editor_start_capture (LrgEditorInputMapEditor *self,
                                           const gchar             *action,
                                           gint                     slot)
{
	g_return_val_if_fail (LRG_IS_EDITOR_INPUT_MAP_EDITOR (self), FALSE);
	g_return_val_if_fail (action != NULL, FALSE);

	if (!has_action (self, action))
		return FALSE;
	g_free (self->cap_action);
	self->cap_action = g_strdup (action);
	self->cap_slot = (slot >= 0 && slot < n_bindings (self, action)) ? slot : -1;
	set_capturing (self, TRUE);
	return TRUE;
}

void
lrg_editor_input_map_editor_cancel_capture (LrgEditorInputMapEditor *self)
{
	g_return_if_fail (LRG_IS_EDITOR_INPUT_MAP_EDITOR (self));
	if (self->capturing)
		set_capturing (self, FALSE);
}

gboolean
lrg_editor_input_map_editor_get_capturing (LrgEditorInputMapEditor *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_INPUT_MAP_EDITOR (self), FALSE);
	return self->capturing;
}

gboolean
lrg_editor_input_map_editor_get_chip_rect (LrgEditorInputMapEditor *self,
                                           const gchar             *action,
                                           gint                     slot,
                                           gfloat                  *x,
                                           gfloat                  *y,
                                           gfloat                  *width,
                                           gfloat                  *height)
{
	LrgWidget         *cell;
	g_autoptr(GArray)  rects = NULL;
	gint               row;
	gfloat             ch, fs;

	g_return_val_if_fail (LRG_IS_EDITOR_INPUT_MAP_EDITOR (self), FALSE);
	g_return_val_if_fail (action != NULL, FALSE);

	row = row_index (self, action);
	if (row < 0 || slot < 0 || slot > n_bindings (self, action))
		return FALSE;
	cell = lrg_table_get_cell_widget (self->table, (guint) row, 1);
	if (cell == NULL || !lrg_widget_get_visible (cell))
		return FALSE;
	rects = chip_layout (self, cell, action, lrg_widget_get_world_x (cell));
	fs = lrg_theme_get_metric (lrg_widget_get_theme (cell), LRG_THEME_METRIC_FONT_SIZE_SMALL);
	ch = MIN (lrg_widget_get_height (cell) - 6.0f, fs + 10.0f);
	if (x != NULL)
		*x = g_array_index (rects, gfloat, slot * 2);
	if (width != NULL)
		*width = g_array_index (rects, gfloat, slot * 2 + 1);
	if (y != NULL)
		*y = lrg_widget_get_world_y (cell) + (lrg_widget_get_height (cell) - ch) * 0.5f;
	if (height != NULL)
		*height = ch;
	return TRUE;
}
