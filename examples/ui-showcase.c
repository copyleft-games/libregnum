/* ui-showcase.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * An app-style tour of the retained UI framework: every widget, the
 * theme presets and accent derivation, spring motion presets, elevation
 * shadows, gradients, staggered appear animations, toasts, a modal
 * dialog and a 10,000-row virtualized list.
 *
 * Controls:
 *   Mouse                 everything
 *   Tab / Shift+Tab       move focus (focus ring appears)
 *   Arrows / gamepad      spatial focus navigation, list cursor, sliders
 *   Enter / Space / A     activate the focused widget
 *   Escape / B            close popups and dialogs
 *
 * Run with --help for options (theme, motion, page, screenshots).
 */

#include <libregnum.h>
#include <graylib.h>
#include <raylib.h>
#include <stdlib.h>

#define WINDOW_WIDTH  (1280)
#define WINDOW_HEIGHT (800)
#define SIDEBAR_WIDTH (240.0f)
#define N_LIST_ITEMS  (10000)

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

static const gchar *page_names[] = { "Controls", "Cards", "Feedback", "Data" };

/* ==========================================================================
 * A list item for the data page
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
 * App state
 * ========================================================================== */

typedef struct
{
	LrgCanvas      *canvas;
	LrgHBox        *root;
	LrgListView    *nav;
	LrgOverlay     *pages;
	LrgWidget      *page[4];
	GPtrArray      *cards;       /* LrgPanel, for replaying their appear */
	LrgToastQueue  *toasts;
	LrgComboBox    *toast_mode;
	LrgProgressBar *progress;
	LrgSlider      *volume;
	LrgLabel       *selection;
	LrgDialog      *dialog;
	gchar          *accent;
} Showcase;

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
	lrg_theme_set_default (theme);
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
	Showcase   *app = user_data;
	const gchar *hex = g_object_get_data (G_OBJECT (button), "accent");
	GrlColor    accent;

	g_free (app->accent);
	app->accent = g_strdup (hex);
	if (lrg_color_parse (hex, &accent))
		lrg_theme_set_accent (lrg_theme_get_default (), &accent);
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

/* section: a titled card panel with a vertical box inside */
static LrgVBox *
section (LrgContainer *parent,
         const gchar  *title)
{
	LrgPanel *panel = lrg_panel_new ();
	LrgVBox  *box = lrg_vbox_new ();

	lrg_panel_set_elevation (panel, 1);
	lrg_panel_set_fill_children (panel, TRUE);
	lrg_container_set_padding (LRG_CONTAINER (panel), 18.0f);
	lrg_container_set_padding (LRG_CONTAINER (box), 0.0f);
	lrg_container_set_spacing (LRG_CONTAINER (box), 12.0f);
	add (box, label_with_class (title, "heading"));
	lrg_container_add_child (LRG_CONTAINER (panel), LRG_WIDGET (box));
	g_object_unref (box);
	add (parent, panel);
	return box;
}

/* ==========================================================================
 * Pages
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
		LrgPanel         *card = lrg_panel_new ();
		LrgVBox          *box = lrg_vbox_new ();

		lrg_widget_add_class (LRG_WIDGET (card), klass);
		lrg_panel_set_elevation (card, i % 4);
		lrg_panel_set_fill_children (card, TRUE);
		lrg_container_set_padding (LRG_CONTAINER (card), 22.0f);
		lrg_widget_set_appear (LRG_WIDGET (card), LRG_APPEAR_POP, 0.06f * (gfloat) i);
		lrg_container_set_spacing (LRG_CONTAINER (box), 8.0f);
		add (box, label_with_class (title, "heading"));
		add (box, label_with_class ("Gradient fill from a theme\nstyle rule; hover to glow.", NULL));
		lrg_container_add_child (LRG_CONTAINER (card), LRG_WIDGET (box));
		g_object_unref (box);
		g_ptr_array_add (app->cards, card);
		lrg_container_add_child (LRG_CONTAINER (grid), LRG_WIDGET (card));
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
build_data_page (Showcase *app)
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
 * Navigation
 * ========================================================================== */

static void
show_page (Showcase *app,
           guint     index)
{
	guint i;

	for (i = 0; i < G_N_ELEMENTS (app->page); i++)
		lrg_widget_set_visible (app->page[i], i == index);

	/* Cards pop in again each time their page opens */
	if (index == 1)
		for (i = 0; i < app->cards->len; i++)
			lrg_widget_play_appear (g_ptr_array_index (app->cards, i));
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

	for (i = 0; i < G_N_ELEMENTS (page_names); i++)
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
	app->page[0] = build_controls_page (app);
	app->page[1] = build_cards_page (app);
	app->page[2] = build_feedback_page (app);
	app->page[3] = build_data_page (app);
	for (i = 0; i < G_N_ELEMENTS (app->page); i++)
	{
		lrg_widget_set_appear (app->page[i], LRG_APPEAR_FLY_UP, 0.0f);
		lrg_widget_set_valign (app->page[i], i == 3 ? LRG_ALIGN_FILL : LRG_ALIGN_START);
		add (app->pages, app->page[i]);
	}
	lrg_container_add_child (LRG_CONTAINER (main_box), LRG_WIDGET (app->pages));
	add (app->root, main_box);

	lrg_container_add_child (LRG_CONTAINER (app->canvas), LRG_WIDGET (app->root));

	/* Toasts live in the overlay layer: above everything, even a
	 * modal dialog's veil, bottom-right */
	app->toasts = lrg_toast_queue_new ();
	lrg_canvas_add_overlay (app->canvas, LRG_WIDGET (app->toasts));

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
	Showcase                  app = { 0 };
	gint                      frame = 0;

	GOptionEntry entries[] = {
		{ "theme", 't', 0, G_OPTION_ARG_STRING, &opt_theme,
		  "Theme preset (midnight, daylight, catppuccin-mocha, catppuccin-latte, high-contrast)", "NAME" },
		{ "accent", 'a', 0, G_OPTION_ARG_STRING, &opt_accent, "Accent colour, e.g. '#22d3ee'", "HEX" },
		{ "motion", 'm', 0, G_OPTION_ARG_STRING, &opt_motion,
		  "Motion preset (smooth, snappy, bouncy, wobbly, none)", "NAME" },
		{ "page", 'p', 0, G_OPTION_ARG_STRING, &opt_page, "Start page (controls, cards, feedback, data)", "NAME" },
		{ "toasts", 0, 0, G_OPTION_ARG_NONE, &opt_toasts, "Show a few toasts at start-up", NULL },
		{ "dialog", 0, 0, G_OPTION_ARG_NONE, &opt_dialog, "Open the dialog at start-up", NULL },
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
		"  ui-showcase --page feedback --toasts --dialog --screenshot dialog.png --frames 120\n");
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

		for (i = 0; i < G_N_ELEMENTS (page_names); i++)
			if (g_ascii_strcasecmp (opt_page, page_names[i]) == 0)
				lrg_list_view_select (app.nav, i);
	}
	if (opt_toasts)
	{
		show_toast (&app, "Welcome back", "Last played 2 hours ago.", LRG_TOAST_KIND_INFO);
		show_toast (&app, "Achievement unlocked", "Pixel perfect: no damage taken.", LRG_TOAST_KIND_SUCCESS);
	}

	while (!grl_window_should_close (window))
	{
		gfloat w = (gfloat) GetScreenWidth ();
		gfloat h = (gfloat) GetScreenHeight ();

		/* The canvas and the root box follow the window */
		lrg_widget_set_size (LRG_WIDGET (app.canvas), w, h);
		lrg_widget_set_size (LRG_WIDGET (app.root), w, h);
		lrg_widget_set_size (LRG_WIDGET (app.toasts), w, h);

		lrg_canvas_handle_input (app.canvas);
		if (opt_dialog && frame == 10)
			lrg_dialog_present (app.dialog, app.canvas);

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

	g_clear_object (&app.dialog);
	g_clear_object (&app.toast_mode);
	g_clear_object (&app.selection);
	g_clear_object (&app.nav);
	g_clear_object (&app.pages);
	g_clear_object (&app.root);
	g_clear_object (&app.toasts);
	g_clear_object (&app.canvas);
	g_ptr_array_unref (app.cards);
	g_free (app.accent);
	return 0;
}
