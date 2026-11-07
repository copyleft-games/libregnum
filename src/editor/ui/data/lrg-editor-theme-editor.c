/* lrg-editor-theme-editor.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgEditorThemeEditor: the theme data editor (see the header).
 *
 * Layout: a horizontal splitter, the property grid on the left and the
 * gallery on the right. The grid has three sections built from the theme
 * format: General (name, preset, dark, motion, accent, font), Colors (one
 * colour row per LrgThemeColor role) and Metrics (one number per
 * LrgThemeMetric). A key the file does not set shows the value the theme
 * would use (from the preview theme) and is marked default; Reset removes
 * the key again. "styles" rules are kept as they are (edited as YAML in
 * the code panel); the gallery header counts them.
 *
 * Preview. After every change the document's text is loaded with
 * lrg_theme_new_from_data(); on success it becomes the gallery's theme
 * (lrg_widget_set_theme on the gallery, inherited by its children) and
 * the theme editor paints the gallery background with it; on failure the
 * last good theme stays and the error is shown above the gallery.
 *
 * "Use for editor" saves, points the editor's "theme" setting at the file
 * and re-applies it; later saves of the same file re-apply it too.
 */

#include "config.h"

#define LRG_EDITOR_PANEL_INTERNAL
#include "lrg-editor-theme-editor.h"
#include "../lrg-editor-app-private.h"
#include "../lrg-editor-console-panel.h"
#include "../../../ui/lrg-splitter.h"
#include "../../../ui/lrg-vbox.h"
#include "../../../ui/lrg-hbox.h"
#include "../../../ui/lrg-label.h"
#include "../../../ui/lrg-button.h"
#include "../../../ui/lrg-checkbox.h"
#include "../../../ui/lrg-slider.h"
#include "../../../ui/lrg-progress-bar.h"
#include "../../../ui/lrg-text-input.h"
#include "../../../ui/lrg-combo-box.h"
#include "../../../ui/lrg-property-grid.h"
#include "../../../ui/lrg-overlay.h"
#include "../../../ui/lrg-scroll-box.h"
#include "../../../ui/lrg-custom-widget.h"
#include "../../../paint/lrg-paint-list.h"
#include <string.h>

struct _LrgEditorThemeEditor
{
	LrgEditorDataEditor  parent_instance;

	LrgPropertyGrid     *grid;
	LrgEditorDataSource *source;
	LrgTheme            *preview;
	gchar               *preview_error;
	LrgVBox             *gallery;
	LrgLabel            *gallery_title;
	LrgLabel            *gallery_error;
	gboolean             grid_stale;
};

G_DEFINE_FINAL_TYPE (LrgEditorThemeEditor, lrg_editor_theme_editor, LRG_TYPE_EDITOR_DATA_EDITOR)

static LrgEditorDocument *
doc_of (LrgEditorThemeEditor *self)
{
	return lrg_editor_data_editor_get_document (LRG_EDITOR_DATA_EDITOR (self));
}

/* "#rrggbb" or "#rrggbbaa" of a colour */
static gchar *
color_hex (const GrlColor *c)
{
	if (c == NULL)
		return NULL;
	if (c->a == 255)
		return g_strdup_printf ("#%02x%02x%02x", c->r, c->g, c->b);
	return g_strdup_printf ("#%02x%02x%02x%02x", c->r, c->g, c->b, c->a);
}

/* ==========================================================================
 * Preview
 * ========================================================================== */

/* Loads the document text as a theme; keeps the last good one on error. */
static void
update_preview (LrgEditorThemeEditor *self)
{
	g_autofree gchar    *text = lrg_editor_document_to_data (doc_of (self), NULL);
	g_autoptr(GError)    error = NULL;
	g_autoptr(LrgTheme)  theme = lrg_theme_new_from_data (text != NULL ? text : "", -1, &error);
	g_autofree gchar    *title = NULL;
	guint                styles = 0;

	g_clear_pointer (&self->preview_error, g_free);
	if (theme != NULL)
		g_set_object (&self->preview, theme);
	else
		self->preview_error = g_strdup (error != NULL ? error->message : "not a theme");
	if (self->preview == NULL)
		self->preview = lrg_theme_new ();

	lrg_widget_set_theme (LRG_WIDGET (self->gallery), self->preview);
	if (lrg_editor_document_get_kind (doc_of (self), "/styles") == LRG_EDITOR_DOCUMENT_NODE_SEQUENCE)
		styles = (guint) lrg_editor_document_get_length (doc_of (self), "/styles");
	title = g_strdup_printf ("Preview \xc2\xb7 %s \xc2\xb7 %u style rule%s",
	                         lrg_theme_get_name (self->preview) != NULL ? lrg_theme_get_name (self->preview)
	                         : "unnamed", styles, styles == 1 ? "" : "s");
	lrg_label_set_text (self->gallery_title, title);
	lrg_label_set_text (self->gallery_error, self->preview_error != NULL ? self->preview_error : "");
	lrg_widget_set_visible (LRG_WIDGET (self->gallery_error), self->preview_error != NULL);
	lrg_widget_queue_layout (LRG_WIDGET (self->gallery));
}

/* ==========================================================================
 * Grid
 * ========================================================================== */

/* "enum-strings:a,b,c" from an enum type's nicks (up to @below). */
static gchar *
enum_hint (GType type,
           gint  below)
{
	g_autoptr(GEnumClass) klass = g_type_class_ref (type);
	GString              *s = g_string_new ("enum-strings:");
	guint                 i;

	for (i = 0; i < klass->n_values; i++)
	{
		if (below >= 0 && klass->values[i].value >= below)
			continue;
		if (s->str[s->len - 1] != ':')
			g_string_append_c (s, ',');
		g_string_append (s, klass->values[i].value_nick);
	}
	return g_string_free (s, FALSE);
}

static void
rebuild_grid (LrgEditorThemeEditor *self)
{
	g_autoptr(GEnumClass)  colors = g_type_class_ref (LRG_TYPE_THEME_COLOR);
	g_autoptr(GEnumClass)  metrics = g_type_class_ref (LRG_TYPE_THEME_METRIC);
	g_autofree gchar      *motion_hint = enum_hint (LRG_TYPE_MOTION, -1);
	GString               *presets = g_string_new ("enum-strings:");
	const gchar * const   *names = lrg_theme_list_presets ();
	g_autofree gchar      *accent = color_hex (lrg_theme_get_accent_color (self->preview));
	g_autoptr(GEnumClass)  motion_class = g_type_class_ref (LRG_TYPE_MOTION);
	GEnumValue            *motion = g_enum_get_value (motion_class, (gint) lrg_theme_get_motion (self->preview));
	guint                  i;

	for (i = 0; names != NULL && names[i] != NULL; i++)
		g_string_append_printf (presets, "%s%s", i > 0 ? "," : "", names[i]);

	lrg_editor_data_source_clear (self->source);
	lrg_editor_data_source_add_field (self->source, "name", "/name", G_TYPE_STRING, "Name", "General", NULL,
	                                  lrg_theme_get_name (self->preview));
	lrg_editor_data_source_add_field (self->source, "preset", "/preset", G_TYPE_STRING, "Based on", "General",
	                                  presets->str, NULL);
	lrg_editor_data_source_add_field (self->source, "dark", "/dark", G_TYPE_BOOLEAN, "Dark", "General", NULL,
	                                  lrg_theme_get_dark (self->preview) ? "true" : "false");
	lrg_editor_data_source_add_field (self->source, "motion", "/motion", G_TYPE_STRING, "Motion", "General",
	                                  motion_hint, motion != NULL ? motion->value_nick : NULL);
	lrg_editor_data_source_add_field (self->source, "accent", "/accent", G_TYPE_STRING, "Accent", "General",
	                                  "color", accent);
	lrg_editor_data_source_add_field (self->source, "font", "/font", G_TYPE_STRING, "Font file", "General",
	                                  "asset", NULL);
	g_string_free (presets, TRUE);

	for (i = 0; i < colors->n_values; i++)
	{
		const GEnumValue *v = &colors->values[i];
		g_autofree gchar *name = NULL;
		g_autofree gchar *path = NULL;
		g_autofree gchar *def = NULL;

		if (v->value >= LRG_THEME_COLOR_COUNT)
			continue;
		name = g_strconcat ("color-", v->value_nick, NULL);
		path = lrg_editor_document_path_join ("/colors", v->value_nick);
		def = color_hex (lrg_theme_get_color (self->preview, (LrgThemeColor) v->value));
		lrg_editor_data_source_add_field (self->source, name, path, G_TYPE_STRING, v->value_nick, "Colors",
		                                  "color", def);
	}
	for (i = 0; i < metrics->n_values; i++)
	{
		const GEnumValue *v = &metrics->values[i];
		g_autofree gchar *name = NULL;
		g_autofree gchar *path = NULL;
		g_autofree gchar *def = NULL;
		gchar             buf[G_ASCII_DTOSTR_BUF_SIZE];

		if (v->value >= LRG_THEME_METRIC_COUNT)
			continue;
		name = g_strconcat ("metric-", v->value_nick, NULL);
		path = lrg_editor_document_path_join ("/metrics", v->value_nick);
		def = g_strdup (g_ascii_dtostr (buf, sizeof buf,
		                                lrg_theme_get_metric (self->preview, (LrgThemeMetric) v->value)));
		lrg_editor_data_source_add_field (self->source, name, path, G_TYPE_DOUBLE, v->value_nick, "Metrics",
		                                  "range:0:96:1", def);
	}
	self->grid_stale = FALSE;
	lrg_property_grid_set_source (self->grid, LRG_PROPERTY_SOURCE (self->source));
	lrg_property_grid_rebuild (self->grid);
}

/* ==========================================================================
 * Gallery
 * ========================================================================== */

static LrgWidget *
gallery_row (LrgWidget *a,
             LrgWidget *b,
             LrgWidget *c)
{
	LrgHBox *row = lrg_hbox_new ();

	lrg_container_set_spacing (LRG_CONTAINER (row), 10.0f);
	lrg_container_add_child (LRG_CONTAINER (row), a);
	if (b != NULL)
		lrg_container_add_child (LRG_CONTAINER (row), b);
	if (c != NULL)
		lrg_container_add_child (LRG_CONTAINER (row), c);
	return LRG_WIDGET (row);
}

static void
build_gallery (LrgEditorThemeEditor *self)
{
	LrgButton      *primary;
	LrgButton      *secondary;
	LrgButton      *disabled;
	LrgCheckbox    *check1;
	LrgCheckbox    *check2;
	LrgSlider      *slider;
	LrgProgressBar *progress;
	LrgTextInput   *input;
	LrgComboBox    *combo;
	LrgLabel       *heading;
	LrgLabel       *body;
	LrgLabel       *muted;

	self->gallery = lrg_vbox_new ();
	lrg_widget_set_name (LRG_WIDGET (self->gallery), "theme-gallery");
	lrg_container_set_padding (LRG_CONTAINER (self->gallery), 28.0f);
	lrg_container_set_spacing (LRG_CONTAINER (self->gallery), 12.0f);

	self->gallery_title = lrg_label_new ("Preview");
	lrg_widget_set_name (LRG_WIDGET (self->gallery_title), "theme-gallery-title");
	self->gallery_error = lrg_label_new ("");
	lrg_widget_set_name (LRG_WIDGET (self->gallery_error), "theme-gallery-error");
	lrg_widget_set_visible (LRG_WIDGET (self->gallery_error), FALSE);

	heading = lrg_label_new ("The quick brown fox");
	lrg_label_set_font_size (heading, 26.0f);
	body = lrg_label_new ("jumps over the lazy dog, 0123456789.");
	muted = lrg_label_new ("Secondary text uses the muted colour.");
	lrg_widget_set_name (LRG_WIDGET (muted), "theme-gallery-muted");

	primary = lrg_button_new ("Primary");
	lrg_widget_add_class (LRG_WIDGET (primary), "primary");
	secondary = lrg_button_new ("Secondary");
	disabled = lrg_button_new ("Disabled");
	lrg_widget_set_enabled (LRG_WIDGET (disabled), FALSE);

	check1 = lrg_checkbox_new ("Enabled");
	lrg_checkbox_set_checked (check1, TRUE);
	check2 = lrg_checkbox_new ("Option");
	combo = lrg_combo_box_new ();
	lrg_combo_box_append (combo, "First choice");
	lrg_combo_box_append (combo, "Second choice");
	lrg_combo_box_set_selected (combo, 0);

	slider = lrg_slider_new_with_range (0.0, 100.0, 1.0);
	lrg_slider_set_value (slider, 62.0);
	lrg_widget_set_min_size (LRG_WIDGET (slider), 220.0f, 0.0f);
	progress = lrg_progress_bar_new ();
	lrg_progress_bar_set_max (progress, 1.0);
	lrg_progress_bar_set_value (progress, 0.45);
	lrg_widget_set_min_size (LRG_WIDGET (progress), 220.0f, 0.0f);
	input = lrg_text_input_new_with_placeholder ("Type here\xe2\x80\xa6");
	lrg_widget_set_min_size (LRG_WIDGET (input), 260.0f, 0.0f);

	lrg_container_add_child (LRG_CONTAINER (self->gallery), LRG_WIDGET (self->gallery_title));
	lrg_container_add_child (LRG_CONTAINER (self->gallery), LRG_WIDGET (self->gallery_error));
	lrg_container_add_child (LRG_CONTAINER (self->gallery), LRG_WIDGET (heading));
	lrg_container_add_child (LRG_CONTAINER (self->gallery), LRG_WIDGET (body));
	lrg_container_add_child (LRG_CONTAINER (self->gallery), LRG_WIDGET (muted));
	lrg_container_add_child (LRG_CONTAINER (self->gallery),
	                         gallery_row (LRG_WIDGET (primary), LRG_WIDGET (secondary), LRG_WIDGET (disabled)));
	lrg_container_add_child (LRG_CONTAINER (self->gallery),
	                         gallery_row (LRG_WIDGET (check1), LRG_WIDGET (check2), LRG_WIDGET (combo)));
	lrg_container_add_child (LRG_CONTAINER (self->gallery), gallery_row (LRG_WIDGET (slider), NULL, NULL));
	lrg_container_add_child (LRG_CONTAINER (self->gallery), gallery_row (LRG_WIDGET (progress), NULL, NULL));
	lrg_container_add_child (LRG_CONTAINER (self->gallery), LRG_WIDGET (input));
}

/* ==========================================================================
 * Use for editor
 * ========================================================================== */

/* Whether the editor's theme setting names this file. */
static gboolean
is_editor_theme (LrgEditorThemeEditor *self)
{
	LrgEditorApp *app = lrg_editor_data_editor_get_app (LRG_EDITOR_DATA_EDITOR (self));
	const gchar  *path = lrg_editor_document_get_path (doc_of (self));

	return app != NULL && path != NULL && g_strcmp0 (lrg_editor_settings_get_theme_file (app->settings), path) == 0;
}

static void
reapply_editor_theme (LrgEditorThemeEditor *self)
{
	LrgEditorApp *app = lrg_editor_data_editor_get_app (LRG_EDITOR_DATA_EDITOR (self));

	if (app == NULL)
		return;
	/* the same setting value: drop the cache so the file is read again */
	g_clear_pointer (&app->applied_theme, g_free);
	editor_app_apply_settings (app);
}

static void
on_saved (LrgEditorDocument *doc,
          const gchar       *path,
          gpointer           user_data)
{
	LrgEditorThemeEditor *self = LRG_EDITOR_THEME_EDITOR (user_data);

	if (is_editor_theme (self))
		reapply_editor_theme (self);
}

static void
on_use (LrgWidget *chip,
        gpointer   user_data)
{
	LrgEditorThemeEditor *self = LRG_EDITOR_THEME_EDITOR (user_data);
	g_autoptr(GError)     error = NULL;
	LrgEditorApp         *app = lrg_editor_data_editor_get_app (LRG_EDITOR_DATA_EDITOR (self));

	if (!lrg_editor_theme_editor_use_for_editor (self, &error))
		lrg_editor_data_editor_report_error (LRG_EDITOR_DATA_EDITOR (self), error);
	else if (app != NULL)
		editor_app_notify (app, EDITOR_NOTIFY_SUCCESS, "The editor now uses %s",
		                   lrg_editor_document_get_display_name (doc_of (self)));
}

/* ==========================================================================
 * LrgEditorDataEditor / LrgWidget
 * ========================================================================== */

static void
theme_refresh (LrgEditorDataEditor *editor,
               const gchar         *path)
{
	LrgEditorThemeEditor *self = LRG_EDITOR_THEME_EDITOR (editor);

	update_preview (self);
	/* defaults (shown for unset keys) follow the preview: a preset, accent
	 * or dark change moves them; rebuild outside the grid's own edit */
	if (*path == '\0' || g_str_has_prefix (path, "/preset") || g_str_has_prefix (path, "/accent") ||
	    g_str_has_prefix (path, "/dark"))
		self->grid_stale = TRUE;
	else
	{
		lrg_editor_data_source_document_changed (self->source, path);
		lrg_property_grid_refresh (self->grid);
	}
}

static void
theme_reveal (LrgEditorDataEditor *editor,
              const gchar         *path,
              guint                line)
{
	LrgEditorThemeEditor *self = LRG_EDITOR_THEME_EDITOR (editor);
	const gchar          *field = lrg_editor_data_source_find_field (self->source, path);
	LrgPropertyRow       *row = field != NULL ? lrg_property_grid_get_row (self->grid, field) : NULL;
	LrgEditorApp         *app = lrg_editor_data_editor_get_app (editor);

	if (row == NULL)
	{
		/* styles and other keys have no row: show the line in the code panel */
		if (app != NULL && lrg_editor_document_get_path (doc_of (self)) != NULL)
		{
			g_autofree gchar *target = g_strdup_printf ("%s:%u", lrg_editor_document_get_path (doc_of (self)),
			                                            MAX (line, 1));

			lrg_widget_activate_action (LRG_WIDGET (self), "code.open", g_variant_new_string (target));
		}
		return;
	}
	/* scroll the grid's scroll box to the row */
	{
		LrgWidget *w;

		for (w = LRG_WIDGET (row); w != NULL; w = (LrgWidget *) lrg_widget_get_parent (w))
			if (LRG_IS_SCROLL_BOX (w))
			{
				lrg_scroll_box_scroll_to_widget (LRG_SCROLL_BOX (w), LRG_WIDGET (row));
				break;
			}
	}
	if (app != NULL && app->canvas != NULL && lrg_property_row_get_editor (row) != NULL)
		lrg_canvas_set_focused_widget (app->canvas, lrg_property_row_get_editor (row));
}

static gchar *
theme_get_state (LrgEditorDataEditor *editor)
{
	LrgEditorThemeEditor *self = LRG_EDITOR_THEME_EDITOR (editor);
	g_autofree gchar     *accent = color_hex (lrg_theme_get_accent_color (self->preview));

	return g_strdup_printf ("preview=%s accent=%s dark=%s error=%s",
	                        lrg_theme_get_name (self->preview) != NULL ? lrg_theme_get_name (self->preview) : "-",
	                        accent, lrg_theme_get_dark (self->preview) ? "yes" : "no",
	                        self->preview_error != NULL ? "yes" : "no");
}

static gboolean
lrg_editor_theme_editor_step (LrgWidget *widget,
                              gfloat     delta_time)
{
	LrgEditorThemeEditor *self = LRG_EDITOR_THEME_EDITOR (widget);
	gboolean              busy = LRG_WIDGET_CLASS (lrg_editor_theme_editor_parent_class)->step (widget, delta_time);

	if (self->grid_stale)
		rebuild_grid (self);
	return busy;
}

/* The gallery's backdrop (under it in an overlay): the preview theme's
 * window colour with a panel card. */
static void
gallery_backdrop (LrgCustomWidget *widget,
                  LrgPaintList    *list,
                  gfloat           x,
                  gfloat           y,
                  gfloat           w,
                  gfloat           h,
                  gpointer         user_data)
{
	LrgEditorThemeEditor *self = LRG_EDITOR_THEME_EDITOR (user_data);
	gfloat                r = lrg_theme_get_metric (self->preview, LRG_THEME_METRIC_CORNER_RADIUS);

	lrg_paint_list_rect (list, x, y, w, h, 0.0f, lrg_theme_get_color (self->preview, LRG_THEME_COLOR_WINDOW));
	lrg_paint_list_rect (list, x + 10.0f, y + 10.0f, w - 20.0f, h - 20.0f, r,
	                     lrg_theme_get_color (self->preview, LRG_THEME_COLOR_PANEL));
	lrg_paint_list_border (list, x + 10.0f, y + 10.0f, w - 20.0f, h - 20.0f, r, 1.0f,
	                       lrg_theme_get_color (self->preview, LRG_THEME_COLOR_BORDER));
}

static void
lrg_editor_theme_editor_constructed (GObject *object)
{
	LrgEditorThemeEditor *self = LRG_EDITOR_THEME_EDITOR (object);
	LrgEditorDataEditor  *base = LRG_EDITOR_DATA_EDITOR (object);
	static const LrgCustomWidgetFuncs backdrop_funcs = { NULL, gallery_backdrop, NULL, NULL, { NULL } };
	LrgSplitter          *split;
	LrgOverlay           *stage;
	LrgCustomWidget      *backdrop;

	G_OBJECT_CLASS (lrg_editor_theme_editor_parent_class)->constructed (object);

	build_gallery (self);
	update_preview (self);

	self->source = lrg_editor_data_source_new (base);
	self->grid = lrg_property_grid_new ();
	lrg_widget_set_name (LRG_WIDGET (self->grid), "theme-grid");
	lrg_property_grid_set_label_width (self->grid, 130.0f);
	lrg_property_grid_set_show_filter (self->grid, TRUE);
	lrg_editor_data_source_bind_grid (self->source, self->grid);

	split = lrg_splitter_new (LRG_ORIENTATION_HORIZONTAL);
	lrg_widget_set_name (LRG_WIDGET (split), "theme-split");
	lrg_splitter_set_start_child (split, LRG_WIDGET (self->grid));
	backdrop = lrg_custom_widget_new ();
	lrg_custom_widget_set_funcs (backdrop, &backdrop_funcs, self, NULL);
	lrg_widget_set_pointer_transparent (LRG_WIDGET (backdrop), TRUE);
	stage = lrg_overlay_new ();
	lrg_widget_set_name (LRG_WIDGET (stage), "theme-stage");
	lrg_container_add_child (LRG_CONTAINER (stage), LRG_WIDGET (backdrop));
	lrg_container_add_child (LRG_CONTAINER (stage), LRG_WIDGET (self->gallery));
	lrg_splitter_set_end_child (split, LRG_WIDGET (stage));
	lrg_splitter_set_default_fraction (split, 0.48f);
	lrg_splitter_set_fraction (split, 0.48f, FALSE);
	lrg_editor_data_editor_set_content (base, LRG_WIDGET (split));

	lrg_editor_data_editor_add_tool (base, editor_chip_new ("theme-use", LRG_ICON_STAR, "Use for editor", on_use,
	                                                        self));
	g_signal_connect_object (doc_of (self), "saved", G_CALLBACK (on_saved), self, 0);
	rebuild_grid (self);
}

static void
lrg_editor_theme_editor_finalize (GObject *object)
{
	LrgEditorThemeEditor *self = LRG_EDITOR_THEME_EDITOR (object);

	g_clear_object (&self->source);
	g_clear_object (&self->preview);
	g_free (self->preview_error);
	G_OBJECT_CLASS (lrg_editor_theme_editor_parent_class)->finalize (object);
}

static void
lrg_editor_theme_editor_class_init (LrgEditorThemeEditorClass *klass)
{
	GObjectClass             *object_class = G_OBJECT_CLASS (klass);
	LrgEditorDataEditorClass *data_class = LRG_EDITOR_DATA_EDITOR_CLASS (klass);

	object_class->constructed = lrg_editor_theme_editor_constructed;
	object_class->finalize = lrg_editor_theme_editor_finalize;
	LRG_WIDGET_CLASS (klass)->step = lrg_editor_theme_editor_step;
	data_class->refresh = theme_refresh;
	data_class->reveal = theme_reveal;
	data_class->get_state = theme_get_state;
}

static void
lrg_editor_theme_editor_init (LrgEditorThemeEditor *self)
{
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

LrgEditorThemeEditor *
lrg_editor_theme_editor_new (LrgEditorApp      *app,
                             LrgEditorDocument *document)
{
	g_return_val_if_fail (app == NULL || LRG_IS_EDITOR_APP (app), NULL);
	g_return_val_if_fail (LRG_IS_EDITOR_DOCUMENT (document), NULL);
	return g_object_new (LRG_TYPE_EDITOR_THEME_EDITOR, "app", app, "document", document, NULL);
}

LrgTheme *
lrg_editor_theme_editor_get_preview_theme (LrgEditorThemeEditor *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_THEME_EDITOR (self), NULL);
	return self->preview;
}

const gchar *
lrg_editor_theme_editor_get_preview_error (LrgEditorThemeEditor *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_THEME_EDITOR (self), NULL);
	return self->preview_error;
}

LrgWidget *
lrg_editor_theme_editor_get_gallery (LrgEditorThemeEditor *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_THEME_EDITOR (self), NULL);
	return LRG_WIDGET (self->gallery);
}

gboolean
lrg_editor_theme_editor_use_for_editor (LrgEditorThemeEditor  *self,
                                        GError               **error)
{
	LrgEditorApp *app;
	const gchar  *path;

	g_return_val_if_fail (LRG_IS_EDITOR_THEME_EDITOR (self), FALSE);
	app = lrg_editor_data_editor_get_app (LRG_EDITOR_DATA_EDITOR (self));
	path = lrg_editor_document_get_path (doc_of (self));
	if (app == NULL || path == NULL)
	{
		g_set_error_literal (error, LRG_EDITOR_DOCUMENT_ERROR, LRG_EDITOR_DOCUMENT_ERROR_NO_PATH,
		                     "The theme has no file or no editor to apply it to");
		return FALSE;
	}
	if (self->preview_error != NULL)
	{
		g_set_error (error, LRG_EDITOR_DOCUMENT_ERROR, LRG_EDITOR_DOCUMENT_ERROR_PARSE,
		             "The theme does not load: %s", self->preview_error);
		return FALSE;
	}
	if (lrg_editor_data_editor_get_dirty (LRG_EDITOR_DATA_EDITOR (self)) &&
	    !lrg_editor_data_editor_save (LRG_EDITOR_DATA_EDITOR (self), error))
		return FALSE;
	g_object_set (app->settings, "theme", path, NULL);
	reapply_editor_theme (self);
	return TRUE;
}
