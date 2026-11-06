/* ui-theme-editor.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * A live theme editor built on the retained UI widgets. The left panel
 * edits a theme: an accent colour with tint and depth sliders that
 * generate a whole palette (lrg_theme_generate_palette()), a base
 * preset, a dark/light switch, a colour picker for each of the 27
 * colour roles, every metric and the motion preset. The right side is
 * a live preview of real widgets drawn with that theme, and a contrast
 * panel lists the text/background pairs with their WCAG ratio and an
 * AA/AAA/fail badge, with buttons that call lrg_theme_fix_contrast().
 * The theme saves to (and loads from) YAML that
 * lrg_theme_new_from_file() reads back unchanged.
 *
 * The edited theme is the process-wide default theme, so the editor's
 * own chrome restyles too: what you see is what a game gets.
 *
 * Controls:
 *   Mouse                 everything
 *   Tab / Shift+Tab       move focus
 *   Arrows                spatial focus navigation, sliders, spin boxes
 *   Enter / Space         activate the focused widget
 *   Escape                close popups
 *
 * Run with --help for options.
 */

#include <libregnum.h>
#include <graylib.h>
#include <raylib.h>
#include <stdlib.h>

#define WINDOW_WIDTH    (1280)
#define WINDOW_HEIGHT   (800)
#define EDITOR_WIDTH    (300.0f)
#define CONTRAST_WIDTH  (320.0f)
#define DEFAULT_ACCENT  "#7c6cff"
#define DEFAULT_OUTPUT  "theme.yaml"
#define DEFAULT_TINT    (0.25)
#define DEFAULT_DEPTH   (0.5)
#define AA_RATIO        (4.5f)
#define AAA_RATIO       (7.0f)

static const gchar *license_text =
	"ui-theme-editor - live theme editor for libregnum\n"
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

/* Motion presets, in combo-box order */
static const struct { const gchar *name; LrgMotion motion; } motions[] = {
	{ "None",   LRG_MOTION_NONE },
	{ "Snappy", LRG_MOTION_SNAPPY },
	{ "Smooth", LRG_MOTION_SMOOTH },
	{ "Bouncy", LRG_MOTION_BOUNCY },
	{ "Wobbly", LRG_MOTION_WOBBLY }
};

/* The 27 colour roles, grouped into one expander each */
typedef struct
{
	const gchar         *title;
	const LrgThemeColor *roles;
	guint                n_roles;
} RoleGroup;

static const LrgThemeColor surface_roles[] = {
	LRG_THEME_COLOR_WINDOW, LRG_THEME_COLOR_PANEL, LRG_THEME_COLOR_SURFACE,
	LRG_THEME_COLOR_POPUP, LRG_THEME_COLOR_TOOLTIP, LRG_THEME_COLOR_INPUT,
	LRG_THEME_COLOR_BORDER
};
static const LrgThemeColor text_roles[] = {
	LRG_THEME_COLOR_TEXT, LRG_THEME_COLOR_TEXT_MUTED, LRG_THEME_COLOR_TEXT_ON_ACCENT,
	LRG_THEME_COLOR_LINK, LRG_THEME_COLOR_DISABLED
};
static const LrgThemeColor accent_roles[] = {
	LRG_THEME_COLOR_ACCENT, LRG_THEME_COLOR_ACCENT_HOVER, LRG_THEME_COLOR_ACCENT_PRESSED,
	LRG_THEME_COLOR_FOCUS, LRG_THEME_COLOR_SELECTION
};
static const LrgThemeColor control_roles[] = {
	LRG_THEME_COLOR_CONTROL, LRG_THEME_COLOR_CONTROL_HOVER, LRG_THEME_COLOR_CONTROL_PRESSED,
	LRG_THEME_COLOR_TRACK, LRG_THEME_COLOR_HANDLE
};
static const LrgThemeColor status_roles[] = {
	LRG_THEME_COLOR_SUCCESS, LRG_THEME_COLOR_WARNING, LRG_THEME_COLOR_DANGER,
	LRG_THEME_COLOR_SHADOW, LRG_THEME_COLOR_VEIL
};

static const RoleGroup role_groups[] = {
	{ "Surfaces",         surface_roles, G_N_ELEMENTS (surface_roles) },
	{ "Text",             text_roles,    G_N_ELEMENTS (text_roles) },
	{ "Accent family",    accent_roles,  G_N_ELEMENTS (accent_roles) },
	{ "Controls",         control_roles, G_N_ELEMENTS (control_roles) },
	{ "Status & effects", status_roles,  G_N_ELEMENTS (status_roles) }
};

/* Spin-box range and step per metric, in LrgThemeMetric order */
static const struct { gdouble min; gdouble max; gdouble step; } metric_ranges[LRG_THEME_METRIC_COUNT] = {
	{ 6.0, 64.0, 1.0 },    /* FONT_SIZE */
	{ 6.0, 48.0, 1.0 },    /* FONT_SIZE_SMALL */
	{ 8.0, 72.0, 1.0 },    /* FONT_SIZE_LARGE */
	{ 10.0, 96.0, 1.0 },   /* FONT_SIZE_TITLE */
	{ 0.0, 32.0, 1.0 },    /* CORNER_RADIUS */
	{ 0.0, 8.0, 0.5 },     /* BORDER_WIDTH */
	{ 16.0, 80.0, 1.0 },   /* CONTROL_HEIGHT */
	{ 0.0, 48.0, 1.0 },    /* PADDING_X */
	{ 0.0, 32.0, 1.0 },    /* PADDING_Y */
	{ 0.0, 40.0, 1.0 },    /* SPACING */
	{ 16.0, 80.0, 1.0 },   /* ROW_HEIGHT */
	{ 0.0, 40.0, 1.0 },    /* POPUP_RADIUS */
	{ 0.0, 64.0, 1.0 },    /* SHADOW_BLUR */
	{ -16.0, 32.0, 1.0 },  /* SHADOW_OFFSET_Y */
	{ 0.0, 8.0, 0.5 },     /* FOCUS_RING_WIDTH */
	{ 2.0, 24.0, 1.0 },    /* SCROLLBAR_SIZE */
	{ 10.0, 40.0, 1.0 },   /* CHECK_SIZE */
	{ 8.0, 40.0, 1.0 }     /* HANDLE_SIZE */
};

/* ==========================================================================
 * A row item for the preview list and table
 * ========================================================================== */

#define EDITOR_TYPE_ITEM (editor_item_get_type ())
G_DECLARE_FINAL_TYPE (EditorItem, editor_item, EDITOR, ITEM, GObject)

struct _EditorItem
{
	GObject  parent_instance;
	gchar   *label;
	gchar   *kind;
	gint     level;
};

G_DEFINE_FINAL_TYPE (EditorItem, editor_item, G_TYPE_OBJECT)

enum
{
	ITEM_PROP_0,
	ITEM_PROP_LABEL,
	ITEM_PROP_KIND,
	ITEM_PROP_LEVEL,
	ITEM_N_PROPS
};

static void
editor_item_finalize (GObject *object)
{
	EditorItem *self = EDITOR_ITEM (object);

	g_free (self->label);
	g_free (self->kind);
	G_OBJECT_CLASS (editor_item_parent_class)->finalize (object);
}

static void
editor_item_get_property (GObject    *object,
                          guint       prop_id,
                          GValue     *value,
                          GParamSpec *pspec)
{
	EditorItem *self = EDITOR_ITEM (object);

	switch (prop_id)
	{
	case ITEM_PROP_LABEL:
		g_value_set_string (value, self->label);
		break;
	case ITEM_PROP_KIND:
		g_value_set_string (value, self->kind);
		break;
	case ITEM_PROP_LEVEL:
		g_value_set_int (value, self->level);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
editor_item_class_init (EditorItemClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = editor_item_finalize;
	object_class->get_property = editor_item_get_property;

	/* The list view's default rows show "label"; the table shows all
	 * three through property columns */
	g_object_class_install_property (object_class, ITEM_PROP_LABEL,
		g_param_spec_string ("label", NULL, NULL, NULL,
		                     G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));
	g_object_class_install_property (object_class, ITEM_PROP_KIND,
		g_param_spec_string ("kind", NULL, NULL, NULL,
		                     G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));
	g_object_class_install_property (object_class, ITEM_PROP_LEVEL,
		g_param_spec_int ("level", NULL, NULL, 0, G_MAXINT, 0,
		                  G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));
}

static void
editor_item_init (EditorItem *self)
{
}

static EditorItem *
editor_item_new (const gchar *label,
                 const gchar *kind,
                 gint         level)
{
	EditorItem *self = g_object_new (EDITOR_TYPE_ITEM, NULL);

	self->label = g_strdup (label);
	self->kind = g_strdup (kind);
	self->level = level;
	return self;
}

/* ==========================================================================
 * App state
 * ========================================================================== */

/* One line of the contrast panel */
typedef struct
{
	LrgThemeColor  fg;
	LrgThemeColor  bg;
	LrgPanel      *chip;     /* "Aa" drawn with fg on bg */
	LrgLabel      *chip_text;
	LrgLabel      *ratio;
	LrgPanel      *badge;
	LrgLabel      *badge_text;
} ContrastRow;

typedef struct
{
	LrgTheme       *theme;      /* the edited theme, also the default */
	LrgCanvas      *canvas;
	LrgHBox        *root;
	LrgToastQueue  *toasts;

	/* Generator inputs */
	GrlColor        accent;
	gdouble         tint;
	gdouble         depth;
	gboolean        dark;
	gboolean        generating; /* accent/tint/depth regenerate all colours */

	/* Editor controls, kept in sync with the theme */
	LrgColorPicker *accent_picker;
	LrgSlider      *tint_slider;
	LrgSlider      *depth_slider;
	LrgLabel       *tint_value;
	LrgLabel       *depth_value;
	LrgCheckbox    *dark_switch;
	LrgCheckbox    *generate_switch;
	LrgComboBox    *preset_combo;
	LrgComboBox    *motion_combo;
	LrgColorPicker *role_pickers[LRG_THEME_COLOR_COUNT];
	LrgSpinBox     *metric_spins[LRG_THEME_METRIC_COUNT];

	/* Header */
	LrgLabel       *name_label;
	LrgTextInput   *path_input;

	/* Preview */
	LrgPanel       *glass_backdrop;
	LrgPanel       *glass_card;

	/* Contrast panel */
	GArray         *contrast_rows; /* ContrastRow */
	LrgLabel       *contrast_summary;

	gboolean        syncing;    /* TRUE while pushing theme values into controls */
} Editor;

/* ==========================================================================
 * Small builders
 * ========================================================================== */

/* add: append a child and drop our reference */
static void
add (gpointer container,
     gpointer child)
{
	lrg_container_add_child (LRG_CONTAINER (container), LRG_WIDGET (child));
	g_object_unref (child);
}

static LrgLabel *
label_with_class (const gchar *text,
                  const gchar *style_class)
{
	LrgLabel *label = lrg_label_new (text);

	if (style_class != NULL)
		lrg_widget_add_class (LRG_WIDGET (label), style_class);
	return label;
}

/* section: a titled card panel with a vertical box inside */
static LrgVBox *
section (gpointer     parent,
         const gchar *title)
{
	LrgPanel *panel = lrg_panel_new ();
	LrgVBox  *box = lrg_vbox_new ();

	lrg_panel_set_elevation (panel, 1);
	lrg_panel_set_fill_children (panel, TRUE);
	lrg_container_set_padding (LRG_CONTAINER (panel), 14.0f);
	lrg_container_set_padding (LRG_CONTAINER (box), 0.0f);
	lrg_container_set_spacing (LRG_CONTAINER (box), 10.0f);
	add (box, label_with_class (title, "heading"));
	lrg_container_add_child (LRG_CONTAINER (panel), LRG_WIDGET (box));
	g_object_unref (box);
	add (parent, panel);
	return box;
}

/*
 * labelled_row:
 *
 * A row with a label that takes the spare width and @control on the
 * right. Takes ownership of @control.
 */
static void
labelled_row (gpointer     parent,
              const gchar *text,
              gpointer     control)
{
	LrgHBox  *row = lrg_hbox_new ();
	LrgLabel *label = lrg_label_new (text);

	lrg_container_set_padding (LRG_CONTAINER (row), 0.0f);
	lrg_container_set_spacing (LRG_CONTAINER (row), 10.0f);
	lrg_widget_set_fill_weight (LRG_WIDGET (label), 1.0f);
	lrg_widget_set_valign (LRG_WIDGET (label), LRG_ALIGN_CENTER);
	add (row, label);
	lrg_widget_set_valign (LRG_WIDGET (control), LRG_ALIGN_CENTER);
	add (row, control);
	add (parent, row);
}

/*
 * pretty_nick:
 *
 * "text-on-accent" -> "Text on accent", for enum nicks shown as labels.
 */
static gchar *
pretty_nick (GType type,
             gint  value)
{
	g_autoptr(GEnumClass) klass = g_type_class_ref (type);
	GEnumValue           *v = g_enum_get_value (klass, value);
	gchar                *text;
	gchar                *p;

	text = g_strdup (v != NULL ? v->value_nick : "?");
	for (p = text; *p != '\0'; p++)
		if (*p == '-')
			*p = ' ';
	text[0] = g_ascii_toupper (text[0]);
	return text;
}

/*
 * short_role_name:
 *
 * Compact role names for the contrast list, where a pair has to fit
 * one line: "On accent / Pressed" rather than "Text on accent /
 * Accent pressed".
 */
static gchar *
short_role_name (LrgThemeColor role)
{
	switch (role)
	{
	case LRG_THEME_COLOR_TEXT_MUTED:
		return g_strdup ("Muted");
	case LRG_THEME_COLOR_TEXT_ON_ACCENT:
		return g_strdup ("On accent");
	case LRG_THEME_COLOR_ACCENT_HOVER:
		return g_strdup ("Hover");
	case LRG_THEME_COLOR_ACCENT_PRESSED:
		return g_strdup ("Pressed");
	default:
		return pretty_nick (LRG_TYPE_THEME_COLOR, (gint) role);
	}
}

static void
show_toast (Editor       *app,
            const gchar  *title,
            const gchar  *message,
            LrgToastKind  kind)
{
	g_autoptr(LrgToast) toast = lrg_toast_new (title, message);

	lrg_toast_set_kind (toast, kind);
	lrg_toast_queue_show (app->toasts, toast);
}

/* ==========================================================================
 * Theme -> controls
 * ========================================================================== */

/* sync_picker: set a picker's colour unless it already shows it (so a
 * picker being dragged keeps its hue for greys) */
static void
sync_picker (LrgColorPicker *picker,
             const GrlColor *color)
{
	GrlColor current;

	lrg_color_picker_get_color (picker, &current);
	if (current.r != color->r || current.g != color->g ||
	    current.b != color->b || current.a != color->a)
		lrg_color_picker_set_color (picker, color);
}

/* badge_for: the WCAG level a ratio reaches, and the role it is shown in */
static const gchar *
badge_for (gfloat         ratio,
           LrgThemeColor *role)
{
	if (ratio >= AAA_RATIO)
	{
		*role = LRG_THEME_COLOR_SUCCESS;
		return "AAA";
	}
	if (ratio >= AA_RATIO)
	{
		*role = LRG_THEME_COLOR_ACCENT;
		return "AA";
	}
	*role = LRG_THEME_COLOR_DANGER;
	return "FAIL";
}

/* update_contrast: refresh every contrast row and the summary */
static void
update_contrast (Editor *app)
{
	g_autofree gchar *summary = NULL;
	guint             failures;
	guint             i;

	for (i = 0; i < app->contrast_rows->len; i++)
	{
		ContrastRow      *row = &g_array_index (app->contrast_rows, ContrastRow, i);
		const GrlColor   *fg = lrg_theme_get_color (app->theme, row->fg);
		const GrlColor   *bg = lrg_theme_get_color (app->theme, row->bg);
		gfloat            ratio = lrg_color_contrast_ratio (fg, bg);
		g_autofree gchar *text = g_strdup_printf ("%.2f", (gdouble) ratio);
		static const GrlColor white = { 255, 255, 255, 255 };
		static const GrlColor black = { 17, 17, 22, 255 };
		LrgThemeColor     badge_role;
		const GrlColor   *badge_bg;
		const gchar      *level;

		/* The chip shows the pair itself */
		lrg_panel_set_background_color (row->chip, bg);
		lrg_label_set_color (row->chip_text, fg);
		lrg_label_set_text (row->ratio, text);

		/* The badge text picks black or white for its own background */
		level = badge_for (ratio, &badge_role);
		badge_bg = lrg_theme_get_color (app->theme, badge_role);
		lrg_label_set_text (row->badge_text, level);
		lrg_panel_set_background_color (row->badge, badge_bg);
		lrg_label_set_color (row->badge_text,
		                     lrg_color_contrast_ratio (&white, badge_bg) >=
		                     lrg_color_contrast_ratio (&black, badge_bg) ? &white : &black);
	}

	failures = lrg_theme_count_contrast_failures (app->theme, AA_RATIO);
	if (failures == 0)
		summary = g_strdup_printf ("All %u pairs pass AA (4.5:1)", app->contrast_rows->len);
	else
		summary = g_strdup_printf ("%u of %u pairs fail AA (4.5:1)", failures, app->contrast_rows->len);
	lrg_label_set_text (app->contrast_summary, summary);
}

/* update_glass_backdrop: the glass card sits on an accent gradient */
static void
update_glass_backdrop (Editor *app)
{
	LrgPartLook      look;
	LrgPaintGradient gradient;

	lrg_part_look_init (&look);
	lrg_paint_gradient_init_linear (&gradient,
	                                lrg_theme_get_color (app->theme, LRG_THEME_COLOR_ACCENT),
	                                lrg_theme_get_color (app->theme, LRG_THEME_COLOR_LINK), 35.0f);
	lrg_part_look_set_gradient (&look, &gradient);
	lrg_widget_set_part_look (LRG_WIDGET (app->glass_backdrop), LRG_UI_PART_BOX,
	                          LRG_WIDGET_STATE_NONE, &look);

	/* The card is frosted glass tinted with the panel colour, strong
	 * enough that the theme's text reads on it in light and dark
	 * themes alike */
	{
		GrlColor tint;

		lrg_part_look_init (&look);
		lrg_paint_color_with_alpha (lrg_theme_get_color (app->theme, LRG_THEME_COLOR_PANEL), 0.62f, &tint);
		lrg_part_look_set_material (&look, LRG_PAINT_MATERIAL_TINTED);
		lrg_part_look_set_background (&look, &tint);
		lrg_widget_set_part_look (LRG_WIDGET (app->glass_card), LRG_UI_PART_BOX,
		                          LRG_WIDGET_STATE_NONE, &look);
	}
}

/* sync_controls: push every theme value into the editor's controls */
static void
sync_controls (Editor *app)
{
	g_autofree gchar *name = NULL;
	guint             i;

	app->syncing = TRUE;

	for (i = 0; i < LRG_THEME_COLOR_COUNT; i++)
		sync_picker (app->role_pickers[i], lrg_theme_get_color (app->theme, i));
	for (i = 0; i < LRG_THEME_METRIC_COUNT; i++)
		if (lrg_spin_box_get_value (app->metric_spins[i]) != (gdouble) lrg_theme_get_metric (app->theme, i))
			lrg_spin_box_set_value (app->metric_spins[i], (gdouble) lrg_theme_get_metric (app->theme, i));
	for (i = 0; i < G_N_ELEMENTS (motions); i++)
		if (motions[i].motion == lrg_theme_get_motion (app->theme) &&
		    lrg_combo_box_get_selected (app->motion_combo) != (gint) i)
			lrg_combo_box_set_selected (app->motion_combo, (gint) i);

	/* The accent picker always shows the theme's accent */
	app->accent = *lrg_theme_get_color (app->theme, LRG_THEME_COLOR_ACCENT);
	sync_picker (app->accent_picker, &app->accent);
	app->dark = lrg_theme_get_dark (app->theme);
	if (lrg_checkbox_get_checked (app->dark_switch) != app->dark)
		lrg_checkbox_set_checked (app->dark_switch, app->dark);
	if (lrg_checkbox_get_checked (app->generate_switch) != app->generating)
		lrg_checkbox_set_checked (app->generate_switch, app->generating);

	name = g_strdup_printf ("%s%s", lrg_theme_get_name (app->theme) != NULL
	                        ? lrg_theme_get_name (app->theme) : "untitled",
	                        app->generating ? " (generated)" : "");
	lrg_label_set_text (app->name_label, name);

	update_contrast (app);
	update_glass_backdrop (app);

	app->syncing = FALSE;
}

/* The single place theme changes flow back into the UI */
static void
on_theme_changed (LrgTheme *theme,
                  gpointer  user_data)
{
	Editor *app = user_data;

	if (!app->syncing)
		sync_controls (app);
}

/* ==========================================================================
 * Controls -> theme
 * ========================================================================== */

/* regenerate: rebuild every colour from accent, tint, depth and dark */
static void
regenerate (Editor *app)
{
	lrg_theme_generate_palette (app->theme, &app->accent, (gfloat) app->tint,
	                            (gfloat) app->depth, app->dark);
}

static void
set_value_label (LrgLabel *label,
                 gdouble   value)
{
	g_autofree gchar *text = g_strdup_printf ("%3.0f%%", value * 100.0);

	lrg_label_set_text (label, text);
}

static void
on_accent_changed (LrgColorPicker *picker,
                   gpointer        user_data)
{
	Editor *app = user_data;

	if (app->syncing)
		return;

	lrg_color_picker_get_color (picker, &app->accent);
	app->accent.a = 255;
	if (app->generating)
		regenerate (app);
	else
		lrg_theme_set_accent (app->theme, &app->accent);
}

/* on_shape_changed: tint or depth moved; switches generation on */
static void
on_shape_changed (LrgSlider *slider,
                  gpointer   user_data)
{
	Editor *app = user_data;

	app->tint = lrg_slider_get_value (app->tint_slider) / 100.0;
	app->depth = lrg_slider_get_value (app->depth_slider) / 100.0;
	set_value_label (app->tint_value, app->tint);
	set_value_label (app->depth_value, app->depth);
	if (app->syncing)
		return;

	app->generating = TRUE;
	regenerate (app);
}

static void
on_dark_toggled (LrgCheckbox *check,
                 gboolean     checked,
                 gpointer     user_data)
{
	Editor *app = user_data;

	if (app->syncing)
		return;

	app->dark = checked;
	if (app->generating)
	{
		regenerate (app);
	}
	else
	{
		/* Keep the colours, re-derive the accent shades for the new
		 * polarity */
		lrg_theme_set_dark (app->theme, checked);
		lrg_theme_set_accent (app->theme, &app->accent);
	}
}

static void
on_generate_toggled (LrgCheckbox *check,
                     gboolean     checked,
                     gpointer     user_data)
{
	Editor *app = user_data;

	if (app->syncing)
		return;

	app->generating = checked;
	if (checked)
		regenerate (app);
	else
		sync_controls (app);
}

static void
on_preset_changed (LrgComboBox *combo,
                   gpointer     user_data)
{
	Editor      *app = user_data;
	const gchar *name = lrg_combo_box_get_selected_text (combo);

	if (app->syncing || name == NULL)
		return;

	/* A preset brings its own colours: stop generating until the
	 * user touches tint or depth again */
	app->generating = FALSE;
	lrg_theme_apply_preset (app->theme, name);
}

static void
on_motion_changed (LrgComboBox *combo,
                   gpointer     user_data)
{
	Editor *app = user_data;
	gint    i = lrg_combo_box_get_selected (combo);

	if (app->syncing)
		return;
	if (i >= 0 && i < (gint) G_N_ELEMENTS (motions))
		lrg_theme_set_motion (app->theme, motions[i].motion);
}

static void
on_role_changed (LrgColorPicker *picker,
                 gpointer        user_data)
{
	Editor   *app = user_data;
	gint      role = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (picker), "role"));
	GrlColor  color;

	if (app->syncing)
		return;

	/* Hand-editing a role means the palette is no longer generated */
	app->generating = FALSE;
	lrg_color_picker_get_color (picker, &color);
	lrg_theme_set_color (app->theme, (LrgThemeColor) role, &color);
}

static void
on_metric_changed (LrgSpinBox *spin,
                   gpointer    user_data)
{
	Editor *app = user_data;
	gint    metric = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (spin), "metric"));

	if (app->syncing)
		return;
	lrg_theme_set_metric (app->theme, (LrgThemeMetric) metric, (gfloat) lrg_spin_box_get_value (spin));
}

static void
on_fix_clicked (LrgButton *button,
                gpointer   user_data)
{
	Editor           *app = user_data;
	gfloat            ratio = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (button), "aaa")) ? AAA_RATIO : AA_RATIO;
	guint             changed;
	g_autofree gchar *msg = NULL;

	changed = lrg_theme_fix_contrast (app->theme, ratio);
	if (changed == 0)
		msg = g_strdup_printf ("Every pair already reaches %.1f:1.", (gdouble) ratio);
	else
		msg = g_strdup_printf ("Adjusted %u colour role%s to reach %.1f:1.", changed,
		                       changed == 1 ? "" : "s", (gdouble) ratio);
	show_toast (app, "Contrast", msg, LRG_TOAST_KIND_SUCCESS);
}

/* ==========================================================================
 * Save / load
 * ========================================================================== */

static gboolean
save_theme (Editor       *app,
            const gchar  *path,
            GError      **error)
{
	return lrg_theme_save_to_file (app->theme, path, error);
}

static void
on_save_clicked (LrgButton *button,
                 gpointer   user_data)
{
	Editor            *app = user_data;
	const gchar       *path = lrg_text_input_get_text (app->path_input);
	g_autoptr(GError)  error = NULL;
	g_autofree gchar  *msg = NULL;

	if (path == NULL || *path == '\0')
	{
		show_toast (app, "Nothing saved", "Type a file name first.", LRG_TOAST_KIND_WARNING);
		return;
	}

	if (!save_theme (app, path, &error))
	{
		show_toast (app, "Save failed", error->message, LRG_TOAST_KIND_DANGER);
		return;
	}
	msg = g_strdup_printf ("Theme written to %s", path);
	show_toast (app, "Saved", msg, LRG_TOAST_KIND_SUCCESS);
}

static void
on_load_clicked (LrgButton *button,
                 gpointer   user_data)
{
	Editor            *app = user_data;
	const gchar       *path = lrg_text_input_get_text (app->path_input);
	g_autoptr(GError)  error = NULL;
	g_autofree gchar  *msg = NULL;

	if (path == NULL || *path == '\0')
		return;

	app->generating = FALSE;
	if (!lrg_theme_load_from_file (app->theme, path, &error))
	{
		show_toast (app, "Load failed", error->message, LRG_TOAST_KIND_DANGER);
		return;
	}
	msg = g_strdup_printf ("Loaded %s", path);
	show_toast (app, "Loaded", msg, LRG_TOAST_KIND_INFO);
}

static void
on_toast_clicked (LrgButton *button,
                  gpointer   user_data)
{
	show_toast (user_data, "Quest complete", "The Ember Relic is yours. +250 XP", LRG_TOAST_KIND_SUCCESS);
}

/* ==========================================================================
 * Editor panel (left)
 * ========================================================================== */

static LrgSlider *
percent_slider (Editor    *app,
                gdouble    value,
                LrgLabel **value_label,
                LrgVBox   *box,
                const gchar *title)
{
	LrgHBox   *head = lrg_hbox_new ();
	LrgLabel  *label = lrg_label_new (title);
	LrgSlider *slider = lrg_slider_new_with_range (0.0, 100.0, 1.0);

	/* "Tint ........ 25%" above the slider */
	lrg_container_set_padding (LRG_CONTAINER (head), 0.0f);
	lrg_widget_set_fill_weight (LRG_WIDGET (label), 1.0f);
	add (head, label);
	*value_label = label_with_class ("", "muted");
	set_value_label (*value_label, value);
	add (head, *value_label);
	add (box, head);

	lrg_slider_set_value (slider, value * 100.0);
	g_signal_connect (slider, "value-changed", G_CALLBACK (on_shape_changed), app);
	add (box, slider);
	return slider;
}

static LrgWidget *
build_editor_panel (Editor *app)
{
	LrgPanel            *panel = lrg_panel_new ();
	LrgScrollBox        *scroll = lrg_scroll_box_new ();
	LrgVBox             *content = lrg_vbox_new ();
	LrgVBox             *box;
	const gchar * const *presets = lrg_theme_list_presets ();
	guint                g;
	guint                i;

	lrg_widget_set_min_size (LRG_WIDGET (panel), EDITOR_WIDTH, 0.0f);
	lrg_widget_set_max_size (LRG_WIDGET (panel), EDITOR_WIDTH, 0.0f);
	lrg_panel_set_elevation (panel, 2);
	lrg_panel_set_fill_children (panel, TRUE);
	lrg_container_set_padding (LRG_CONTAINER (panel), 6.0f);

	lrg_container_set_padding (LRG_CONTAINER (content), 12.0f);
	lrg_container_set_spacing (LRG_CONTAINER (content), 12.0f);

	add (content, label_with_class ("Theme editor", "title"));

	/* Palette generator: accent, tint, depth, polarity */
	box = lrg_vbox_new ();
	lrg_container_set_padding (LRG_CONTAINER (box), 0.0f);
	lrg_container_set_spacing (LRG_CONTAINER (box), 8.0f);
	add (box, label_with_class ("Palette", "heading"));
	app->accent_picker = lrg_color_picker_new ();
	lrg_color_picker_set_popup_mode (app->accent_picker, TRUE);
	lrg_color_picker_set_show_alpha (app->accent_picker, FALSE);
	lrg_color_picker_set_color (app->accent_picker, &app->accent);
	g_signal_connect (app->accent_picker, "color-changed", G_CALLBACK (on_accent_changed), app);
	labelled_row (box, "Accent", app->accent_picker);
	app->tint_slider = percent_slider (app, app->tint, &app->tint_value, box, "Tint");
	app->depth_slider = percent_slider (app, app->depth, &app->depth_value, box, "Depth");
	app->dark_switch = lrg_checkbox_new_with_style ("Dark", LRG_CHECKBOX_STYLE_SWITCH);
	lrg_checkbox_set_checked (app->dark_switch, app->dark);
	g_signal_connect (app->dark_switch, "toggled", G_CALLBACK (on_dark_toggled), app);
	lrg_container_add_child (LRG_CONTAINER (box), LRG_WIDGET (app->dark_switch));
	app->generate_switch = lrg_checkbox_new_with_style ("Generate neutrals from accent",
	                                                    LRG_CHECKBOX_STYLE_SWITCH);
	lrg_checkbox_set_checked (app->generate_switch, app->generating);
	g_signal_connect (app->generate_switch, "toggled", G_CALLBACK (on_generate_toggled), app);
	lrg_container_add_child (LRG_CONTAINER (box), LRG_WIDGET (app->generate_switch));

	/* Base preset and motion */
	app->preset_combo = lrg_combo_box_new ();
	lrg_combo_box_set_placeholder (app->preset_combo, "Choose...");
	for (i = 0; presets[i] != NULL; i++)
		lrg_combo_box_append (app->preset_combo, presets[i]);
	lrg_widget_set_min_size (LRG_WIDGET (app->preset_combo), 160.0f, 0.0f);
	g_signal_connect (app->preset_combo, "changed", G_CALLBACK (on_preset_changed), app);
	g_object_ref (app->preset_combo);
	labelled_row (box, "Preset", app->preset_combo);

	app->motion_combo = lrg_combo_box_new ();
	for (i = 0; i < G_N_ELEMENTS (motions); i++)
		lrg_combo_box_append (app->motion_combo, motions[i].name);
	lrg_widget_set_min_size (LRG_WIDGET (app->motion_combo), 160.0f, 0.0f);
	g_signal_connect (app->motion_combo, "changed", G_CALLBACK (on_motion_changed), app);
	g_object_ref (app->motion_combo);
	labelled_row (box, "Motion", app->motion_combo);
	add (content, box);

	/* One expander per group of colour roles */
	add (content, label_with_class ("Colour roles", "heading"));
	for (g = 0; g < G_N_ELEMENTS (role_groups); g++)
	{
		LrgExpander *expander = lrg_expander_new (role_groups[g].title);
		LrgVBox     *rows = lrg_vbox_new ();

		lrg_container_set_padding (LRG_CONTAINER (rows), 4.0f);
		lrg_container_set_spacing (LRG_CONTAINER (rows), 6.0f);
		for (i = 0; i < role_groups[g].n_roles; i++)
		{
			LrgThemeColor     role = role_groups[g].roles[i];
			LrgColorPicker   *picker = lrg_color_picker_new ();
			g_autofree gchar *name = pretty_nick (LRG_TYPE_THEME_COLOR, (gint) role);

			lrg_color_picker_set_popup_mode (picker, TRUE);
			lrg_color_picker_set_color (picker, lrg_theme_get_color (app->theme, role));
			g_object_set_data (G_OBJECT (picker), "role", GINT_TO_POINTER ((gint) role));
			g_signal_connect (picker, "color-changed", G_CALLBACK (on_role_changed), app);
			app->role_pickers[role] = picker;
			labelled_row (rows, name, picker);
		}
		lrg_expander_set_child (expander, LRG_WIDGET (rows));
		g_object_unref (rows);
		lrg_expander_set_expanded (expander, g == 0);
		add (content, expander);
	}

	/* Metrics */
	{
		LrgExpander *expander = lrg_expander_new ("Metrics");
		LrgVBox     *rows = lrg_vbox_new ();

		lrg_container_set_padding (LRG_CONTAINER (rows), 4.0f);
		lrg_container_set_spacing (LRG_CONTAINER (rows), 6.0f);
		for (i = 0; i < LRG_THEME_METRIC_COUNT; i++)
		{
			LrgSpinBox       *spin = lrg_spin_box_new_with_range (metric_ranges[i].min, metric_ranges[i].max,
			                                                      metric_ranges[i].step);
			g_autofree gchar *name = pretty_nick (LRG_TYPE_THEME_METRIC, (gint) i);

			lrg_spin_box_set_digits (spin, metric_ranges[i].step < 1.0 ? 1 : 0);
			lrg_spin_box_set_value (spin, (gdouble) lrg_theme_get_metric (app->theme, i));
			lrg_widget_set_min_size (LRG_WIDGET (spin), 130.0f, 0.0f);
			g_object_set_data (G_OBJECT (spin), "metric", GINT_TO_POINTER ((gint) i));
			g_signal_connect (spin, "value-changed", G_CALLBACK (on_metric_changed), app);
			app->metric_spins[i] = spin;
			labelled_row (rows, name, spin);
		}
		lrg_expander_set_child (expander, LRG_WIDGET (rows));
		g_object_unref (rows);
		lrg_expander_set_expanded (expander, TRUE);
		add (content, expander);
	}

	lrg_scroll_box_set_child (scroll, LRG_WIDGET (content));
	g_object_unref (content);
	lrg_widget_set_fill_weight (LRG_WIDGET (scroll), 1.0f);
	add (panel, scroll);
	return LRG_WIDGET (panel);
}

/* ==========================================================================
 * Preview (centre)
 * ========================================================================== */

static GListModel *
preview_items (void)
{
	static const struct { const gchar *label; const gchar *kind; gint level; } rows[] = {
		{ "Ember Relic",     "Artifact", 7 },
		{ "Frost Lance",     "Weapon",   4 },
		{ "Gale Boots",      "Armour",   3 },
		{ "Stone Ward",      "Charm",    5 },
		{ "Tide Caller",     "Staff",    6 },
		{ "Volt Coil",       "Trinket",  2 }
	};
	GListStore *store = g_list_store_new (EDITOR_TYPE_ITEM);
	guint       i;

	for (i = 0; i < G_N_ELEMENTS (rows); i++)
	{
		g_autoptr(EditorItem) item = editor_item_new (rows[i].label, rows[i].kind, rows[i].level);

		g_list_store_append (store, item);
	}
	return G_LIST_MODEL (store);
}

static LrgWidget *
build_preview (Editor *app)
{
	LrgScrollBox         *scroll = lrg_scroll_box_new ();
	LrgHBox              *grid = lrg_hbox_new ();
	LrgVBox              *col[2];
	g_autoptr(GListModel) items = preview_items ();
	LrgVBox              *box;
	LrgHBox              *row;
	LrgButton            *btn;
	guint                 i;

	/* Two equal columns of cards, each card at its natural height */
	lrg_hbox_set_homogeneous (grid, TRUE);
	lrg_container_set_spacing (LRG_CONTAINER (grid), 14.0f);
	lrg_container_set_padding (LRG_CONTAINER (grid), 0.0f);
	for (i = 0; i < 2; i++)
	{
		col[i] = lrg_vbox_new ();
		lrg_container_set_padding (LRG_CONTAINER (col[i]), 0.0f);
		lrg_container_set_spacing (LRG_CONTAINER (col[i]), 14.0f);
		lrg_widget_set_valign (LRG_WIDGET (col[i]), LRG_ALIGN_START);
		add (grid, col[i]);
	}

	/* Buttons, including the toast trigger */
	box = section (col[0], "Buttons");
	row = lrg_hbox_new ();
	lrg_container_set_padding (LRG_CONTAINER (row), 0.0f);
	lrg_container_set_spacing (LRG_CONTAINER (row), 8.0f);
	add (row, lrg_button_new ("Default"));
	btn = lrg_button_new ("Primary");
	lrg_widget_add_class (LRG_WIDGET (btn), "primary");
	add (row, btn);
	btn = lrg_button_new ("Flat");
	lrg_widget_add_class (LRG_WIDGET (btn), "flat");
	add (row, btn);
	add (box, row);
	row = lrg_hbox_new ();
	lrg_container_set_padding (LRG_CONTAINER (row), 0.0f);
	lrg_container_set_spacing (LRG_CONTAINER (row), 8.0f);
	btn = lrg_button_new ("Disabled");
	lrg_widget_set_enabled (LRG_WIDGET (btn), FALSE);
	add (row, btn);
	btn = lrg_button_new ("Delete");
	lrg_widget_add_class (LRG_WIDGET (btn), "danger");
	add (row, btn);
	add (box, row);
	btn = lrg_button_new ("Show toast");
	lrg_widget_add_class (LRG_WIDGET (btn), "primary");
	lrg_widget_set_halign (LRG_WIDGET (btn), LRG_ALIGN_START);
	g_signal_connect (btn, "clicked", G_CALLBACK (on_toast_clicked), app);
	add (box, btn);

	/* Toggles */
	box = section (col[1], "Toggles");
	{
		LrgCheckbox *sw = lrg_checkbox_new_with_style ("Enable bloom", LRG_CHECKBOX_STYLE_SWITCH);
		LrgCheckbox *cb = lrg_checkbox_new ("Show subtitles");

		lrg_checkbox_set_checked (sw, TRUE);
		lrg_checkbox_set_checked (cb, TRUE);
		add (box, sw);
		add (box, cb);
		add (box, lrg_checkbox_new_with_style ("Hard mode", LRG_CHECKBOX_STYLE_RADIO));
	}

	/* Ranges and rating */
	box = section (col[1], "Ranges");
	{
		LrgSlider      *slider = lrg_slider_new_with_range (0.0, 100.0, 1.0);
		LrgProgressBar *progress = lrg_progress_bar_new ();
		LrgRating      *rating = lrg_rating_new ();

		lrg_slider_set_value (slider, 65.0);
		add (box, slider);
		lrg_progress_bar_set_max (progress, 100.0);
		lrg_progress_bar_set_value (progress, 40.0);
		add (box, progress);
		lrg_rating_set_max (rating, 5);
		lrg_rating_set_value (rating, 3.5);
		lrg_rating_set_allow_half (rating, TRUE);
		add (box, rating);
	}

	/* Text entry and combo */
	box = section (col[0], "Input");
	{
		LrgComboBox *combo = lrg_combo_box_new ();

		add (box, lrg_text_input_new_with_placeholder ("Hero name..."));
		lrg_combo_box_append (combo, "Easy");
		lrg_combo_box_append (combo, "Normal");
		lrg_combo_box_append (combo, "Nightmare");
		lrg_combo_box_set_selected (combo, 1);
		add (box, combo);
	}

	/* Tabs */
	box = section (col[1], "Tabs");
	{
		LrgTabView *tabs = lrg_tab_view_new ();

		lrg_widget_set_min_size (LRG_WIDGET (tabs), 0.0f, 96.0f);
		lrg_tab_view_add_tab (tabs, "Graphics", LRG_WIDGET (label_with_class ("Resolution, quality, effects.", "muted")));
		lrg_tab_view_add_tab (tabs, "Audio", LRG_WIDGET (label_with_class ("Volumes and output device.", "muted")));
		lrg_tab_view_add_tab (tabs, "Controls", LRG_WIDGET (label_with_class ("Keyboard and pad bindings.", "muted")));
		add (box, tabs);
	}

	/* A glass card over an accent gradient: glass needs something
	 * behind it to refract */
	box = section (col[0], "Glass");
	{
		LrgOverlay *stack = lrg_overlay_new ();
		LrgPanel   *card = lrg_panel_new ();
		LrgVBox    *inner = lrg_vbox_new ();

		lrg_widget_set_min_size (LRG_WIDGET (stack), 0.0f, 112.0f);
		app->glass_backdrop = lrg_panel_new ();
		add (stack, app->glass_backdrop);

		/* Its tinted-glass look follows the theme: update_glass_backdrop() */
		app->glass_card = card;
		lrg_panel_set_fill_children (card, TRUE);
		lrg_container_set_padding (LRG_CONTAINER (card), 12.0f);
		lrg_widget_set_margin_all (LRG_WIDGET (card), 16.0f);
		lrg_container_set_spacing (LRG_CONTAINER (inner), 4.0f);
		add (inner, label_with_class ("Frosted card", "heading"));
		add (inner, label_with_class ("Tinted glass over the accent.", NULL));
		lrg_container_add_child (LRG_CONTAINER (card), LRG_WIDGET (inner));
		g_object_unref (inner);
		add (stack, card);
		add (box, stack);
	}

	/* List */
	box = section (col[1], "List");
	{
		LrgListView *list = lrg_list_view_new ();

		lrg_list_view_set_model (list, items);
		lrg_widget_set_min_size (LRG_WIDGET (list), 0.0f, 150.0f);
		lrg_list_view_select (list, 1);
		add (box, list);
	}

	/* Table */
	box = section (col[0], "Table");
	{
		LrgTable *table = lrg_table_new ();

		lrg_table_add_property_column (table, "Name", "label", 130.0f,
		                               LRG_TABLE_COLUMN_SORTABLE | LRG_TABLE_COLUMN_EXPAND);
		lrg_table_add_property_column (table, "Kind", "kind", 90.0f, LRG_TABLE_COLUMN_SORTABLE);
		lrg_table_add_property_column (table, "Lvl", "level", 50.0f, LRG_TABLE_COLUMN_SORTABLE);
		lrg_table_set_model (table, items);
		lrg_table_set_striped (table, TRUE);
		lrg_widget_set_min_size (LRG_WIDGET (table), 0.0f, 150.0f);
		lrg_table_select (table, 0);
		add (box, table);
	}

	lrg_scroll_box_set_child (scroll, LRG_WIDGET (grid));
	g_object_unref (grid);
	return LRG_WIDGET (scroll);
}

/* ==========================================================================
 * Contrast panel (right)
 * ========================================================================== */

static LrgWidget *
build_contrast_panel (Editor *app)
{
	LrgPanel  *panel = lrg_panel_new ();
	LrgVBox   *box = lrg_vbox_new ();
	LrgHBox   *buttons = lrg_hbox_new ();
	LrgButton *btn;
	guint      i;

	lrg_widget_set_min_size (LRG_WIDGET (panel), CONTRAST_WIDTH, 0.0f);
	lrg_widget_set_max_size (LRG_WIDGET (panel), CONTRAST_WIDTH, 0.0f);
	lrg_panel_set_elevation (panel, 1);
	lrg_panel_set_fill_children (panel, TRUE);
	lrg_container_set_padding (LRG_CONTAINER (panel), 14.0f);
	lrg_container_set_padding (LRG_CONTAINER (box), 0.0f);
	lrg_container_set_spacing (LRG_CONTAINER (box), 8.0f);

	add (box, label_with_class ("Contrast", "heading"));
	app->contrast_summary = label_with_class ("", "muted");
	add (box, g_object_ref (app->contrast_summary));

	for (i = 0; i < lrg_theme_get_n_contrast_pairs (); i++)
	{
		ContrastRow       row = { 0 };
		LrgHBox          *line = lrg_hbox_new ();
		LrgLabel         *name;
		g_autofree gchar *fg_name = NULL;
		g_autofree gchar *bg_name = NULL;
		g_autofree gchar *text = NULL;

		lrg_theme_get_contrast_pair (i, &row.fg, &row.bg);
		fg_name = short_role_name (row.fg);
		bg_name = short_role_name (row.bg);
		lrg_container_set_padding (LRG_CONTAINER (line), 0.0f);
		lrg_container_set_spacing (LRG_CONTAINER (line), 8.0f);

		/* "Aa" in the pair's own colours */
		row.chip = lrg_panel_new ();
		lrg_panel_set_fill_children (row.chip, TRUE);
		lrg_panel_set_corner_radius (row.chip, 6.0f);
		lrg_container_set_padding (LRG_CONTAINER (row.chip), 2.0f);
		lrg_widget_set_min_size (LRG_WIDGET (row.chip), 38.0f, 26.0f);
		lrg_widget_set_max_size (LRG_WIDGET (row.chip), 38.0f, 26.0f);
		lrg_widget_set_valign (LRG_WIDGET (row.chip), LRG_ALIGN_CENTER);
		row.chip_text = lrg_label_new ("Aa");
		lrg_label_set_alignment (row.chip_text, LRG_TEXT_ALIGN_CENTER);
		lrg_widget_set_valign (LRG_WIDGET (row.chip_text), LRG_ALIGN_CENTER);
		add (row.chip, row.chip_text);
		add (line, row.chip);

		text = g_strdup_printf ("%s / %s", fg_name, bg_name);
		name = label_with_class (text, NULL);
		lrg_widget_set_fill_weight (LRG_WIDGET (name), 1.0f);
		lrg_widget_set_valign (LRG_WIDGET (name), LRG_ALIGN_CENTER);
		add (line, name);

		row.ratio = label_with_class ("", "muted");
		lrg_widget_set_valign (LRG_WIDGET (row.ratio), LRG_ALIGN_CENTER);
		add (line, row.ratio);

		row.badge = lrg_panel_new ();
		lrg_panel_set_fill_children (row.badge, TRUE);
		lrg_panel_set_corner_radius (row.badge, 10.0f);
		lrg_container_set_padding (LRG_CONTAINER (row.badge), 2.0f);
		lrg_widget_set_min_size (LRG_WIDGET (row.badge), 46.0f, 22.0f);
		lrg_widget_set_max_size (LRG_WIDGET (row.badge), 46.0f, 22.0f);
		lrg_widget_set_valign (LRG_WIDGET (row.badge), LRG_ALIGN_CENTER);
		row.badge_text = lrg_label_new ("");
		lrg_label_set_alignment (row.badge_text, LRG_TEXT_ALIGN_CENTER);
		lrg_label_set_font_size (row.badge_text, 12.0f);
		lrg_widget_set_valign (LRG_WIDGET (row.badge_text), LRG_ALIGN_CENTER);
		add (row.badge, row.badge_text);
		add (line, row.badge);

		add (box, line);
		g_array_append_val (app->contrast_rows, row);
	}

	/* Auto-fix to AA or AAA */
	lrg_container_set_padding (LRG_CONTAINER (buttons), 0.0f);
	lrg_container_set_spacing (LRG_CONTAINER (buttons), 8.0f);
	btn = lrg_button_new ("Auto-fix AA");
	lrg_widget_add_class (LRG_WIDGET (btn), "primary");
	g_signal_connect (btn, "clicked", G_CALLBACK (on_fix_clicked), app);
	add (buttons, btn);
	btn = lrg_button_new ("Auto-fix AAA");
	g_object_set_data (G_OBJECT (btn), "aaa", GINT_TO_POINTER (1));
	g_signal_connect (btn, "clicked", G_CALLBACK (on_fix_clicked), app);
	add (buttons, btn);
	add (box, buttons);

	add (panel, box);
	return LRG_WIDGET (panel);
}

/* ==========================================================================
 * Window layout
 * ========================================================================== */

static void
build_ui (Editor      *app,
          const gchar *output)
{
	LrgVBox   *right = lrg_vbox_new ();
	LrgHBox   *header = lrg_hbox_new ();
	LrgHBox   *body = lrg_hbox_new ();
	LrgButton *btn;
	LrgWidget *preview;

	app->canvas = lrg_canvas_new ();
	lrg_canvas_set_paint_background (app->canvas, TRUE);

	app->root = lrg_hbox_new ();
	lrg_container_set_padding (LRG_CONTAINER (app->root), 16.0f);
	lrg_container_set_spacing (LRG_CONTAINER (app->root), 16.0f);

	add (app->root, build_editor_panel (app));

	/* Header: theme name, output path, save and load */
	lrg_container_set_padding (LRG_CONTAINER (header), 0.0f);
	lrg_container_set_spacing (LRG_CONTAINER (header), 10.0f);
	{
		LrgLabel *title = label_with_class ("Live preview", "title");

		lrg_widget_set_valign (LRG_WIDGET (title), LRG_ALIGN_CENTER);
		add (header, title);
	}
	app->name_label = label_with_class ("", "muted");
	lrg_widget_set_fill_weight (LRG_WIDGET (app->name_label), 1.0f);
	lrg_widget_set_valign (LRG_WIDGET (app->name_label), LRG_ALIGN_CENTER);
	add (header, g_object_ref (app->name_label));
	app->path_input = lrg_text_input_new_with_placeholder ("theme.yaml");
	lrg_text_input_set_text (app->path_input, output);
	lrg_widget_set_min_size (LRG_WIDGET (app->path_input), 260.0f, 0.0f);
	lrg_widget_set_valign (LRG_WIDGET (app->path_input), LRG_ALIGN_CENTER);
	add (header, g_object_ref (app->path_input));
	btn = lrg_button_new ("Load");
	lrg_widget_set_valign (LRG_WIDGET (btn), LRG_ALIGN_CENTER);
	g_signal_connect (btn, "clicked", G_CALLBACK (on_load_clicked), app);
	add (header, btn);
	btn = lrg_button_new ("Save YAML");
	lrg_widget_add_class (LRG_WIDGET (btn), "primary");
	lrg_widget_set_valign (LRG_WIDGET (btn), LRG_ALIGN_CENTER);
	g_signal_connect (btn, "clicked", G_CALLBACK (on_save_clicked), app);
	add (header, btn);

	lrg_container_set_padding (LRG_CONTAINER (right), 0.0f);
	lrg_container_set_spacing (LRG_CONTAINER (right), 14.0f);
	lrg_widget_set_fill_weight (LRG_WIDGET (right), 1.0f);
	add (right, header);

	/* Body: preview widgets beside the contrast panel */
	lrg_container_set_padding (LRG_CONTAINER (body), 0.0f);
	lrg_container_set_spacing (LRG_CONTAINER (body), 14.0f);
	lrg_widget_set_fill_weight (LRG_WIDGET (body), 1.0f);
	preview = build_preview (app);
	lrg_widget_set_fill_weight (preview, 1.0f);
	add (body, preview);
	add (body, build_contrast_panel (app));
	add (right, body);
	add (app->root, right);

	lrg_container_add_child (LRG_CONTAINER (app->canvas), LRG_WIDGET (app->root));

	/* Toasts float above everything, bottom-right */
	app->toasts = lrg_toast_queue_new ();
	lrg_canvas_add_overlay (app->canvas, LRG_WIDGET (app->toasts));
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
	g_autofree gchar         *opt_preset = NULL;
	g_autofree gchar         *opt_output = NULL;
	g_autofree gchar         *opt_accent = NULL;
	g_autofree gchar         *opt_screenshot = NULL;
	gdouble                   opt_tint = DEFAULT_TINT;
	gdouble                   opt_depth = DEFAULT_DEPTH;
	gint                      opt_frames = 60;
	gboolean                  opt_light = FALSE;
	gboolean                  opt_fix = FALSE;
	gboolean                  opt_save = FALSE;
	gboolean                  opt_license = FALSE;
	Editor                    app = { 0 };
	gint                      frame = 0;

	GOptionEntry entries[] = {
		{ "theme", 't', 0, G_OPTION_ARG_FILENAME, &opt_theme, "Theme YAML file to start from", "FILE" },
		{ "preset", 'p', 0, G_OPTION_ARG_STRING, &opt_preset,
		  "Preset to start from (midnight, daylight, catppuccin-mocha, catppuccin-latte, high-contrast)", "NAME" },
		{ "accent", 'a', 0, G_OPTION_ARG_STRING, &opt_accent,
		  "Generate the palette from this accent, e.g. '#22d3ee'", "HEX" },
		{ "tint", 0, 0, G_OPTION_ARG_DOUBLE, &opt_tint, "Accent hue in the neutrals, 0..1 (default 0.25)", "T" },
		{ "depth", 0, 0, G_OPTION_ARG_DOUBLE, &opt_depth, "Surface depth and layer spread, 0..1 (default 0.5)", "D" },
		{ "light", 'l', 0, G_OPTION_ARG_NONE, &opt_light, "Generate a light palette instead of a dark one", NULL },
		{ "fix-contrast", 0, 0, G_OPTION_ARG_NONE, &opt_fix, "Run the AA auto-fix at start-up", NULL },
		{ "output", 'o', 0, G_OPTION_ARG_FILENAME, &opt_output,
		  "File the Save button writes (default theme.yaml)", "FILE" },
		{ "save", 0, 0, G_OPTION_ARG_NONE, &opt_save,
		  "Write the theme to --output and exit, without opening a window", NULL },
		{ "screenshot", 's', 0, G_OPTION_ARG_FILENAME, &opt_screenshot,
		  "Save a PNG after --frames frames and exit", "PATH" },
		{ "frames", 'f', 0, G_OPTION_ARG_INT, &opt_frames, "Frames to run before --screenshot (default 60)", "N" },
		{ "license", 0, 0, G_OPTION_ARG_NONE, &opt_license, "Show the license and exit", NULL },
		{ NULL }
	};

	ctx = g_option_context_new ("- design libregnum UI themes live");
	g_option_context_add_main_entries (ctx, entries, NULL);
	g_option_context_set_description (ctx,
		"Examples:\n"
		"  ui-theme-editor\n"
		"  ui-theme-editor --accent '#22d3ee' --tint 0.4 --depth 0.7\n"
		"  ui-theme-editor --accent '#ff7a45' --light --output warm-light.yaml\n"
		"  ui-theme-editor --theme mytheme.yaml --output mytheme.yaml\n"
		"  ui-theme-editor --preset catppuccin-latte --fix-contrast --save --output latte-aa.yaml\n"
		"  ui-theme-editor --accent '#34d399' --screenshot editor.png --frames 90\n");
	if (!g_option_context_parse (ctx, &argc, &argv, &error))
	{
		g_printerr ("ui-theme-editor: %s\n", error->message);
		return 2;
	}
	if (opt_license)
	{
		g_print ("%s", license_text);
		return 0;
	}

	/* ------------------------------------------------------------------
	 * The theme: from a file, a preset, or generated from an accent
	 * ------------------------------------------------------------------ */
	app.tint = CLAMP (opt_tint, 0.0, 1.0);
	app.depth = CLAMP (opt_depth, 0.0, 1.0);
	app.dark = !opt_light;
	lrg_color_parse (DEFAULT_ACCENT, &app.accent);
	if (opt_accent != NULL && !lrg_color_parse (opt_accent, &app.accent))
	{
		g_printerr ("ui-theme-editor: '%s' is not a colour\n", opt_accent);
		return 2;
	}

	if (opt_theme != NULL)
	{
		app.theme = lrg_theme_new_from_file (opt_theme, &error);
		if (app.theme == NULL)
		{
			g_printerr ("ui-theme-editor: %s\n", error->message);
			return 1;
		}
	}
	else if (opt_preset != NULL)
	{
		app.theme = lrg_theme_new_preset (opt_preset);
		if (app.theme == NULL)
		{
			g_printerr ("ui-theme-editor: unknown preset '%s'\n", opt_preset);
			return 2;
		}
	}
	else
	{
		app.theme = lrg_theme_new ();
		lrg_theme_set_name (app.theme, "custom");
		app.generating = TRUE;
		regenerate (&app);
	}

	/* An explicit accent recolours a loaded theme without regenerating */
	if (!app.generating && opt_accent != NULL)
		lrg_theme_set_accent (app.theme, &app.accent);
	if (opt_fix)
		lrg_theme_fix_contrast (app.theme, AA_RATIO);

	/* Headless export: no window needed */
	if (opt_save)
	{
		const gchar *path = opt_output != NULL ? opt_output : DEFAULT_OUTPUT;

		if (!save_theme (&app, path, &error))
		{
			g_printerr ("ui-theme-editor: %s\n", error->message);
			g_object_unref (app.theme);
			return 1;
		}
		g_print ("ui-theme-editor: wrote %s\n", path);
		g_object_unref (app.theme);
		return 0;
	}

	SetConfigFlags (FLAG_WINDOW_RESIZABLE | FLAG_MSAA_4X_HINT | FLAG_WINDOW_HIGHDPI);
	window = grl_window_new (WINDOW_WIDTH, WINDOW_HEIGHT, "libregnum theme editor");
	grl_window_set_target_fps (window, 60);

	lrg_theme_set_default (app.theme);
	app.contrast_rows = g_array_new (FALSE, TRUE, sizeof (ContrastRow));
	build_ui (&app, opt_output != NULL ? opt_output : DEFAULT_OUTPUT);
	sync_controls (&app);
	g_signal_connect (app.theme, "changed", G_CALLBACK (on_theme_changed), &app);

	/* Show the preset in the combo when we started from one */
	if (opt_preset != NULL)
	{
		const gchar * const *presets = lrg_theme_list_presets ();
		guint                i;

		app.syncing = TRUE;
		for (i = 0; presets[i] != NULL; i++)
			if (g_strcmp0 (presets[i], opt_preset) == 0)
				lrg_combo_box_set_selected (app.preset_combo, (gint) i);
		app.syncing = FALSE;
	}

	while (!grl_window_should_close (window))
	{
		gfloat w = (gfloat) GetScreenWidth ();
		gfloat h = (gfloat) GetScreenHeight ();

		/* The canvas, root box and toast layer follow the window */
		lrg_widget_set_size (LRG_WIDGET (app.canvas), w, h);
		lrg_widget_set_size (LRG_WIDGET (app.root), w, h);
		lrg_widget_set_size (LRG_WIDGET (app.toasts), w, h);

		lrg_canvas_handle_input (app.canvas);

		grl_window_begin_drawing (window);
		lrg_canvas_render (app.canvas);

		if (opt_screenshot != NULL && frame >= opt_frames)
		{
			Image shot = LoadImageFromScreen ();

			ExportImage (shot, opt_screenshot);
			UnloadImage (shot);
			grl_window_end_drawing (window);
			break;
		}
		grl_window_end_drawing (window);
		frame++;
	}

	/* Tear down: stop listening before the widgets go away */
	g_signal_handlers_disconnect_by_data (app.theme, &app);
	g_clear_object (&app.preset_combo);
	g_clear_object (&app.motion_combo);
	g_clear_object (&app.name_label);
	g_clear_object (&app.path_input);
	g_clear_object (&app.contrast_summary);
	g_clear_object (&app.root);
	g_clear_object (&app.toasts);
	g_clear_object (&app.canvas);
	g_array_unref (app.contrast_rows);
	lrg_theme_set_default (NULL);
	g_object_unref (app.theme);
	return 0;
}
