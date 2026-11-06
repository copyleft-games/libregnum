/* lrg-editor-preferences.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgEditorPreferences: the Preferences dialog content (see the header).
 *
 * Pieces, top to bottom in this file:
 *
 *  PrefsSource    an LrgPropertySource showing a fixed list of
 *                 LrgEditorSettings properties with our own labels,
 *                 groups and hints. Values go through an inner
 *                 LrgObjectPropertySource on the settings; every write is
 *                 followed by apply_live() so snapping, history and
 *                 camera values reach the editor at once (theme, scale
 *                 and font size are applied by the app's own notify).
 *  ThemeGallery   painted cards, one per built-in theme, previewing its
 *                 window / panel / accent / text colours; a click sets
 *                 the theme. "Load YAML..." adds any theme file.
 *  Keymap         the searchable shortcut table (also the Help >
 *                 Keyboard Shortcuts dialog): one row widget per action
 *                 with a chip button per accelerator, "+" to add an
 *                 alternative, "x" to remove one and Reset. Clicking a
 *                 chip (or "+") captures the next key press: the keymap
 *                 takes keyboard focus and turns the KEY_DOWN event into
 *                 an accelerator with lrg_accel_name() (modifier keys
 *                 alone wait for the real key; Escape cancels; Backspace
 *                 removes the alternative). Rows are rebuilt on an idle
 *                 after ::accels-changed so a button is never destroyed
 *                 inside its own "clicked" emission.
 *  LrgEditorPreferences   the tab view of pages plus Reset All / Edit
 *                 editor.yaml, and lrg_editor_preferences_present(),
 *                 which wraps it in an LrgDialog following the
 *                 lrg-editor-dialogs.c protocol (object data
 *                 "lrg-editor-dialog" on the app, id "preferences").
 */

#include "config.h"

#include "lrg-editor-app-private.h"
#include "lrg-editor-preferences.h"
#include "lrg-editor-code-panel.h"
#include "../../ui/lrg-tab-view.h"
#include "../../ui/lrg-scroll-box.h"
#include "../../ui/lrg-vbox.h"
#include "../../ui/lrg-hbox.h"
#include "../../ui/lrg-label.h"
#include "../../ui/lrg-button.h"
#include "../../ui/lrg-text-input.h"
#include "../../ui/lrg-dialog.h"
#include "../../ui/lrg-file-dialog.h"
#include "../../ui/lrg-property-source.h"
#include "../../ui/lrg-ui-actions.h"
#include "../../ui/lrg-ui-event.h"
#include "../../ui/lrg-theme.h"
#include "../../paint/lrg-paint-list.h"
#include "../../paint/lrg-paint-icons.h"
#include <math.h>
#include <string.h>

#define DIALOG_KEY "lrg-editor-dialog"
#define MOD_MASK   (LRG_INPUT_MODIFIER_SHIFT | LRG_INPUT_MODIFIER_CTRL | \
                    LRG_INPUT_MODIFIER_ALT | LRG_INPUT_MODIFIER_SUPER)

/* ==========================================================================
 * Pages and their settings
 * ========================================================================== */

typedef struct
{
	const gchar *name;      /* LrgEditorSettings property */
	const gchar *label;
	const gchar *group;
	const gchar *hint;      /* property grid hint, or NULL */
	const gchar *tooltip;
} PrefEntry;

static const PrefEntry general_entries[] = {
	{ "autosave-interval", "Autosave every (s, 0 = off)", "Documents", "range:0,3600,5",
	  "Seconds between autosaves of the open level (written next to it as .autosave)" },
	{ "history-limit", "Undo steps kept", "Documents", "range:10,100000,10", NULL },
	{ "reload-on-external-change", "Reload levels changed on disk", "Documents", NULL,
	  "A level edited elsewhere reloads when it has no unsaved changes" },
	{ "confirm-on-close", "Ask before discarding changes", "Documents", NULL, NULL },
	{ "restore-last-session", "Reopen the last project", "Session", NULL, NULL },
};

static const PrefEntry appearance_entries[] = {
	{ "ui-scale", "UI scale", "Interface", "range:0.5,3,0.05", "Scales every metric of the theme" },
	{ "font-size", "Font size", "Interface", "range:8,40,1", "Text size the theme is scaled for (16 = 1:1)" },
	{ "font-path", "Font file", "Interface", NULL, "A TTF/OTF file; empty uses the theme's font" },
};

static const PrefEntry external_entries[] = {
	{ "script-editor", "Scripts open in", "Code", "enum-strings:internal,external,emacs",
	  "internal: the Code panel; external: the editor below; emacs: a connected Emacs" },
	{ "external-editor", "External editor", "External editor", NULL,
	  "\"auto\" follows $VISUAL / $EDITOR" },
	{ "external-editor-command", "Custom command", "External editor", NULL,
	  "Used by the \"custom\" preset. Placeholders: {file} {line} {col} {project} {dir}" },
	{ "editor-in-terminal", "Run in a terminal", "External editor", "enum-strings:auto,always,never", NULL },
	{ "terminal-command", "Terminal", "External editor", NULL, "Empty detects $TERMINAL, gst, foot, kitty..." },
	{ "emacs-integration", "Use a connected Emacs", "Emacs", NULL, "libregnum.el, M-x libregnum-connect" },
	{ "emacs-server-name", "Server name (-s)", "Emacs", NULL, NULL },
};

static const PrefEntry viewport_entries[] = {
	{ "snap-enabled", "Snap by default", "Snapping", NULL, NULL },
	{ "snap-translate", "Move step (m)", "Snapping", "range:0.001,1000,0.05", NULL },
	{ "snap-rotate-degrees", "Rotate step (deg)", "Snapping", "range:0.1,180,0.5", NULL },
	{ "snap-scale", "Scale step", "Snapping", "range:0.001,100,0.05", NULL },
	{ "grid-visible", "Show grid", "Grid", NULL, NULL },
	{ "grid-size", "Cell size (m)", "Grid", "range:0.01,1000,0.1", NULL },
	{ "grid-extent", "Half extent (cells)", "Grid", "range:1,200,1", NULL },
	{ "camera-navigation", "Navigation scheme", "Camera", "enum-strings:default,blender,trackpad",
	  "default: right drag flies, middle pans, Alt+left orbits; blender: middle orbits; trackpad: scroll orbits" },
	{ "camera-fov", "Field of view (deg)", "Camera", "range:10,150,1", NULL },
	{ "camera-fly-speed", "Fly speed (m/s)", "Camera", "range:0.1,500,0.5", NULL },
	{ "camera-orbit-speed", "Orbit speed", "Camera", "range:0.01,5,0.01", NULL },
	{ "camera-invert-y", "Invert Y", "Camera", NULL, NULL },
};

static const PrefEntry server_entries[] = {
	{ "server-enabled", "Start the server", "IPC server", NULL,
	  "Emacs, lrgeditorctl and MCP clients connect through it" },
	{ "server-socket", "Socket path", "IPC server", NULL, "Empty: $XDG_RUNTIME_DIR/libregnum/editor-PID.sock" },
};

typedef struct
{
	const gchar     *id;
	const gchar     *title;
	const PrefEntry *entries;
	guint            n_entries;
} PageSpec;

static const PageSpec pages[] = {
	{ "general", "General", general_entries, G_N_ELEMENTS (general_entries) },
	{ "appearance", "Appearance", appearance_entries, G_N_ELEMENTS (appearance_entries) },
	{ "external-editor", "External Editor", external_entries, G_N_ELEMENTS (external_entries) },
	{ "viewport", "Viewport", viewport_entries, G_N_ELEMENTS (viewport_entries) },
	{ "keybindings", "Keybindings", NULL, 0 },
	{ "server", "Server", server_entries, G_N_ELEMENTS (server_entries) },
};

/* Built-in themes of the gallery: settings value and label */
static const gchar * const gallery_themes[][2] = {
	{ "catppuccin-mocha", "Catppuccin Mocha" },
	{ "catppuccin-macchiato", "Catppuccin Macchiato" },
	{ "catppuccin-frappe", "Catppuccin Frappe" },
	{ "catppuccin-latte", "Catppuccin Latte" },
	{ "midnight", "Midnight" },
	{ "daylight", "Daylight" },
	{ "high-contrast", "High Contrast" },
};

/* ==========================================================================
 * Live application of a changed setting
 * ========================================================================== */

static void
apply_live (LrgEditorApp *app,
            const gchar  *name)
{
	LrgEditorSettings *s;

	if (app == NULL)
		return;
	s = app->settings;
	if (g_str_has_prefix (name, "snap-"))
	{
		gboolean on = FALSE;
		gdouble  st = 1.0;
		gdouble  sr = 15.0;
		gdouble  ss = 0.1;

		g_object_get (s, "snap-enabled", &on, "snap-translate", &st, "snap-rotate-degrees", &sr, "snap-scale", &ss,
		              NULL);
		lrg_editor_set_snap_enabled (app->editor, on);
		lrg_editor_set_snap (app->editor, st, sr * G_PI / 180.0, ss);
	}
	else if (g_strcmp0 (name, "history-limit") == 0)
	{
		guint limit = 100;

		g_object_get (s, "history-limit", &limit, NULL);
		lrg_editor_set_history_limit (app->editor, limit);
	}
	else if (g_strcmp0 (name, "camera-fov") == 0)
	{
		gdouble fov = 50.0;

		g_object_get (s, "camera-fov", &fov, NULL);
		lrg_editor_view_state_set_fov (app->view, fov);
	}
	else if (g_strcmp0 (name, "camera-fly-speed") == 0)
	{
		gdouble speed = 5.0;

		g_object_get (s, "camera-fly-speed", &speed, NULL);
		lrg_editor_view_state_set_fly_speed (app->view, speed);
	}
	else if (g_strcmp0 (name, "grid-visible") == 0)
	{
		LrgEditorViewportLayout layout = lrg_editor_view_state_get_layout (app->view);
		gint                    n = (layout == LRG_EDITOR_VIEWPORT_LAYOUT_SINGLE) ? 1
		                            : (layout == LRG_EDITOR_VIEWPORT_LAYOUT_QUAD) ? 4 : 2;
		gboolean                on = TRUE;
		gint                    i;

		g_object_get (s, "grid-visible", &on, NULL);
		for (i = 0; i < n; i++)
		{
			LrgEditorViewOverlays o = lrg_editor_view_state_get_overlays (app->view, i);

			o = on ? (o | LRG_EDITOR_VIEW_OVERLAY_GRID) : (o & ~LRG_EDITOR_VIEW_OVERLAY_GRID);
			lrg_editor_view_state_set_overlays (app->view, i, o);
		}
	}
}

/* ==========================================================================
 * PrefsSource
 * ========================================================================== */

#define LRG_TYPE_EDITOR_PREFS_SOURCE (lrg_editor_prefs_source_get_type ())
G_DECLARE_FINAL_TYPE (LrgEditorPrefsSource, lrg_editor_prefs_source, LRG, EDITOR_PREFS_SOURCE, GObject)

struct _LrgEditorPrefsSource
{
	GObject                  parent_instance;

	LrgObjectPropertySource *inner;
	const PrefEntry         *entries;
	guint                    n_entries;
	LrgEditorApp            *app;      /* weak */
	gchar                   *presets;  /* enum-strings hint of the external editor presets */
};

static void prefs_source_iface_init (LrgPropertySourceInterface *iface);

G_DEFINE_FINAL_TYPE_WITH_CODE (LrgEditorPrefsSource, lrg_editor_prefs_source, G_TYPE_OBJECT,
                               G_IMPLEMENT_INTERFACE (LRG_TYPE_PROPERTY_SOURCE, prefs_source_iface_init))

static const PrefEntry *
prefs_source_entry (LrgEditorPrefsSource *self,
                    const gchar          *name)
{
	guint i;

	for (i = 0; i < self->n_entries; i++)
		if (g_strcmp0 (self->entries[i].name, name) == 0)
			return &self->entries[i];
	return NULL;
}

static GPtrArray *
prefs_source_list (LrgPropertySource *source)
{
	LrgEditorPrefsSource *self = LRG_EDITOR_PREFS_SOURCE (source);
	GPtrArray            *out = g_ptr_array_new_with_free_func ((GDestroyNotify) lrg_property_info_unref);
	GObject              *settings = lrg_object_property_source_get_object (self->inner);
	guint                 i;

	for (i = 0; i < self->n_entries; i++)
	{
		const PrefEntry *e = &self->entries[i];
		GParamSpec      *pspec = g_object_class_find_property (G_OBJECT_GET_CLASS (settings), e->name);
		LrgPropertyInfo *info;

		if (pspec == NULL)
			continue;
		info = lrg_property_info_new (pspec);
		lrg_property_info_set_label (info, e->label);
		lrg_property_info_set_group (info, e->group);
		if (g_strcmp0 (e->name, "external-editor") == 0)
			lrg_property_info_set_hint (info, self->presets);
		else if (e->hint != NULL)
			lrg_property_info_set_hint (info, e->hint);
		if (e->tooltip != NULL)
			lrg_property_info_set_tooltip (info, e->tooltip);
		g_ptr_array_add (out, info);
	}
	return out;
}

static gboolean
prefs_source_get (LrgPropertySource *source,
                  const gchar       *name,
                  GValue            *value)
{
	LrgEditorPrefsSource *self = LRG_EDITOR_PREFS_SOURCE (source);

	if (prefs_source_entry (self, name) == NULL)
		return FALSE;
	return lrg_property_source_get_value (LRG_PROPERTY_SOURCE (self->inner), name, value);
}

static gboolean
prefs_source_set (LrgPropertySource *source,
                  const gchar       *name,
                  const GValue      *value)
{
	LrgEditorPrefsSource *self = LRG_EDITOR_PREFS_SOURCE (source);

	if (prefs_source_entry (self, name) == NULL ||
	    !lrg_property_source_set_value (LRG_PROPERTY_SOURCE (self->inner), name, value))
		return FALSE;
	apply_live (self->app, name);
	return TRUE;
}

static void
prefs_source_iface_init (LrgPropertySourceInterface *iface)
{
	iface->list_properties = prefs_source_list;
	iface->get_value = prefs_source_get;
	iface->set_value = prefs_source_set;
}

/* The settings changed (us, the IPC, Reset All): tell the grid */
static void
on_inner_changed (LrgPropertySource *inner,
                  const gchar       *name,
                  gpointer           user_data)
{
	LrgEditorPrefsSource *self = LRG_EDITOR_PREFS_SOURCE (user_data);

	if (prefs_source_entry (self, name) != NULL)
		lrg_property_source_emit_changed (LRG_PROPERTY_SOURCE (self), name);
}

static void
lrg_editor_prefs_source_dispose (GObject *object)
{
	LrgEditorPrefsSource *self = LRG_EDITOR_PREFS_SOURCE (object);

	if (self->inner != NULL)
		g_signal_handlers_disconnect_by_data (self->inner, self);
	g_clear_object (&self->inner);
	g_clear_weak_pointer (&self->app);
	g_clear_pointer (&self->presets, g_free);
	G_OBJECT_CLASS (lrg_editor_prefs_source_parent_class)->dispose (object);
}

static void
lrg_editor_prefs_source_class_init (LrgEditorPrefsSourceClass *klass)
{
	G_OBJECT_CLASS (klass)->dispose = lrg_editor_prefs_source_dispose;
}

static void
lrg_editor_prefs_source_init (LrgEditorPrefsSource *self)
{
}

static LrgEditorPrefsSource *
prefs_source_new (LrgEditorApp    *app,
                  const PrefEntry *entries,
                  guint            n_entries)
{
	LrgEditorPrefsSource *self = g_object_new (LRG_TYPE_EDITOR_PREFS_SOURCE, NULL);
	const gchar * const  *presets = lrg_external_editor_list_presets ();
	g_autofree gchar     *joined = g_strjoinv (",", (gchar **) presets);

	self->inner = lrg_object_property_source_new (G_OBJECT (app->settings));
	self->entries = entries;
	self->n_entries = n_entries;
	self->presets = g_strconcat ("enum-strings:", joined, NULL);
	g_set_weak_pointer (&self->app, app);
	g_signal_connect (self->inner, "property-changed", G_CALLBACK (on_inner_changed), self);
	return self;
}

/* ==========================================================================
 * ThemeGallery
 * ========================================================================== */

#define LRG_TYPE_EDITOR_THEME_GALLERY (lrg_editor_theme_gallery_get_type ())
G_DECLARE_FINAL_TYPE (LrgEditorThemeGallery, lrg_editor_theme_gallery, LRG, EDITOR_THEME_GALLERY, LrgWidget)

#define CARD_W      (150.0f)
#define CARD_H      (100.0f)
#define CARD_GAP    (10.0f)
#define CARD_COLS   (4)

typedef struct
{
	gchar    *value;   /* the "theme" setting value */
	gchar    *label;
	LrgTheme *theme;   /* for the preview colours */
} ThemeCard;

struct _LrgEditorThemeGallery
{
	LrgWidget     parent_instance;

	LrgEditorApp *app;      /* weak */
	GArray       *cards;    /* ThemeCard */
	gint          hover;
	gint          pressed;
};

G_DEFINE_FINAL_TYPE (LrgEditorThemeGallery, lrg_editor_theme_gallery, LRG_TYPE_WIDGET)

static void
theme_card_clear (gpointer data)
{
	ThemeCard *card = data;

	g_free (card->value);
	g_free (card->label);
	g_clear_object (&card->theme);
}

static void
gallery_add (LrgEditorThemeGallery *self,
             const gchar           *value,
             const gchar           *label)
{
	ThemeCard         card;
	g_autofree gchar *file = NULL;

	memset (&card, 0, sizeof card);
	card.theme = lrg_theme_new_preset (value);
	if (card.theme == NULL)
	{
		file = (strchr (value, '/') != NULL) ? g_strdup (value) : lrg_editor_settings_find_theme_file (value);
		if (file != NULL)
			card.theme = lrg_theme_new_from_file (file, NULL);
	}
	if (card.theme == NULL)
		return;   /* a theme file that is not installed: no card */
	card.value = g_strdup (value);
	card.label = g_strdup (label);
	g_array_append_val (self->cards, card);
}

/* The current "theme" value, with the 0.8.0 names mapped */
static gchar *
current_theme (LrgEditorThemeGallery *self)
{
	g_autofree gchar *value = NULL;

	if (self->app == NULL)
		return NULL;
	g_object_get (self->app->settings, "theme", &value, NULL);
	if (g_strcmp0 (value, "mocha") == 0)
		return g_strdup ("catppuccin-mocha");
	if (g_strcmp0 (value, "latte") == 0)
		return g_strdup ("catppuccin-latte");
	if (g_strcmp0 (value, "macchiato") == 0 || g_strcmp0 (value, "frappe") == 0)
		return g_strconcat ("catppuccin-", value, NULL);
	return g_steal_pointer (&value);
}

/* Adds a card for a custom theme file when that is the current theme */
static void
gallery_sync (LrgEditorThemeGallery *self)
{
	g_autofree gchar *value = current_theme (self);
	guint             i;

	if (value == NULL || *value == '\0')
		return;
	for (i = 0; i < self->cards->len; i++)
		if (g_strcmp0 (g_array_index (self->cards, ThemeCard, i).value, value) == 0)
			return;
	if (strchr (value, '/') != NULL || g_str_has_suffix (value, ".yaml") || g_str_has_suffix (value, ".yml"))
	{
		g_autofree gchar *base = g_path_get_basename (value);

		gallery_add (self, value, base);
		lrg_widget_queue_layout (LRG_WIDGET (self));
	}
}

static void
card_rect (LrgEditorThemeGallery *self,
           guint                  i,
           gfloat                *x,
           gfloat                *y,
           gfloat                *w)
{
	gfloat width = lrg_widget_get_width (LRG_WIDGET (self));
	gfloat cw = MAX ((width - CARD_GAP * (CARD_COLS - 1)) / CARD_COLS, 60.0f);

	*w = cw;
	*x = lrg_widget_get_world_x (LRG_WIDGET (self)) + (gfloat) (i % CARD_COLS) * (cw + CARD_GAP);
	*y = lrg_widget_get_world_y (LRG_WIDGET (self)) + (gfloat) (i / CARD_COLS) * (CARD_H + CARD_GAP);
}

static gint
card_at (LrgEditorThemeGallery *self,
         gfloat                 px,
         gfloat                 py)
{
	guint i;

	for (i = 0; i < self->cards->len; i++)
	{
		gfloat x, y, w;

		card_rect (self, i, &x, &y, &w);
		if (px >= x && px < x + w && py >= y && py < y + CARD_H)
			return (gint) i;
	}
	return -1;
}

static void
lrg_editor_theme_gallery_measure (LrgWidget *widget,
                                  gfloat    *width,
                                  gfloat    *height)
{
	LrgEditorThemeGallery *self = LRG_EDITOR_THEME_GALLERY (widget);
	guint                  rows = (self->cards->len + CARD_COLS - 1) / CARD_COLS;

	*width = CARD_W * CARD_COLS + CARD_GAP * (CARD_COLS - 1);
	*height = rows * CARD_H + (rows > 0 ? rows - 1 : 0) * CARD_GAP;
}

static void
lrg_editor_theme_gallery_paint (LrgWidget    *widget,
                                LrgPaintList *list)
{
	LrgEditorThemeGallery *self = LRG_EDITOR_THEME_GALLERY (widget);
	LrgTheme              *ui = lrg_widget_get_theme (widget);
	GrlFont               *font = lrg_theme_get_ui_font (ui);
	gfloat                 radius = lrg_theme_get_metric (ui, LRG_THEME_METRIC_CORNER_RADIUS) * 1.5f;
	gfloat                 small = lrg_theme_get_metric (ui, LRG_THEME_METRIC_FONT_SIZE_SMALL);
	g_autofree gchar      *current = current_theme (self);
	guint                  i;

	for (i = 0; i < self->cards->len; i++)
	{
		ThemeCard *card = &g_array_index (self->cards, ThemeCard, i);
		LrgTheme  *t = card->theme;
		gboolean   selected = g_strcmp0 (card->value, current) == 0;
		gfloat     x, y, w;
		gfloat     lift = ((gint) i == self->hover) ? 2.0f : 0.0f;
		gfloat     preview_h = CARD_H - small * 2.2f;

		card_rect (self, i, &x, &y, &w);
		y -= lift;

		/* The card: the theme's window colour with a panel, a title
		 * strip, an accent button and a few lines of text */
		lrg_paint_list_shadow (list, x, y, w, CARD_H, radius, 8.0f + lift * 3.0f, 0.0f, 0.0f, 2.0f + lift,
		                       lrg_theme_get_color (ui, LRG_THEME_COLOR_SHADOW));
		lrg_paint_list_rect (list, x, y, w, CARD_H, radius, lrg_theme_get_color (t, LRG_THEME_COLOR_WINDOW));
		lrg_paint_list_rect (list, x + 8.0f, y + 8.0f, w - 16.0f, preview_h - 12.0f, radius * 0.6f,
		                     lrg_theme_get_color (t, LRG_THEME_COLOR_PANEL));
		lrg_paint_list_rect (list, x + 8.0f, y + 8.0f, w - 16.0f, 8.0f, radius * 0.6f,
		                     lrg_theme_get_color (t, LRG_THEME_COLOR_SURFACE));
		lrg_paint_list_rect (list, x + 16.0f, y + 24.0f, w * 0.45f, 5.0f, 2.5f,
		                     lrg_theme_get_color (t, LRG_THEME_COLOR_TEXT));
		lrg_paint_list_rect (list, x + 16.0f, y + 34.0f, w * 0.3f, 5.0f, 2.5f,
		                     lrg_theme_get_color (t, LRG_THEME_COLOR_TEXT_MUTED));
		lrg_paint_list_rect (list, x + 16.0f, y + 44.0f, w * 0.36f, 5.0f, 2.5f,
		                     lrg_theme_get_color (t, LRG_THEME_COLOR_TEXT_MUTED));
		lrg_paint_list_rect (list, x + w - 16.0f - w * 0.28f, y + preview_h - 24.0f, w * 0.28f, 12.0f, 6.0f,
		                     lrg_theme_get_color (t, LRG_THEME_COLOR_ACCENT));
		lrg_paint_list_circle (list, x + w - 22.0f, y + 28.0f, 5.0f, lrg_theme_get_color (t, LRG_THEME_COLOR_SUCCESS));
		lrg_paint_list_circle (list, x + w - 36.0f, y + 28.0f, 5.0f, lrg_theme_get_color (t, LRG_THEME_COLOR_WARNING));
		lrg_paint_list_circle (list, x + w - 50.0f, y + 28.0f, 5.0f, lrg_theme_get_color (t, LRG_THEME_COLOR_DANGER));

		/* Name under the preview, in the card theme's text colour */
		lrg_paint_list_text (list, font, card->label, roundf (x + 10.0f), roundf (y + CARD_H - small * 1.6f), small,
		                     1.0f, lrg_theme_get_color (t, LRG_THEME_COLOR_TEXT));

		/* Ring: accent when chosen, a hairline otherwise */
		if (selected)
		{
			lrg_paint_list_border (list, x - 2.0f, y - 2.0f, w + 4.0f, CARD_H + 4.0f, radius + 2.0f, 2.5f,
			                       lrg_theme_get_color (ui, LRG_THEME_COLOR_ACCENT));
			lrg_paint_list_circle (list, x + w - 12.0f, y + CARD_H - small * 1.0f, 8.0f,
			                       lrg_theme_get_color (ui, LRG_THEME_COLOR_ACCENT));
			lrg_paint_list_icon (list, LRG_ICON_CHECK, x + w - 19.0f, y + CARD_H - small * 1.0f - 7.0f, 14.0f, 0.0f,
			                     lrg_theme_get_color (ui, LRG_THEME_COLOR_TEXT_ON_ACCENT));
		}
		else
			lrg_paint_list_border (list, x, y, w, CARD_H, radius, 1.0f,
			                       lrg_theme_get_color (ui, LRG_THEME_COLOR_BORDER));
	}
}

static gboolean
lrg_editor_theme_gallery_handle_event (LrgWidget        *widget,
                                       const LrgUIEvent *event)
{
	LrgEditorThemeGallery *self = LRG_EDITOR_THEME_GALLERY (widget);
	gint                   card = card_at (self, lrg_ui_event_get_x (event), lrg_ui_event_get_y (event));

	switch (lrg_ui_event_get_event_type (event))
	{
	case LRG_UI_EVENT_MOUSE_MOVE:
		self->hover = card;
		return FALSE;

	case LRG_UI_EVENT_POINTER_LEAVE:
		self->hover = -1;
		return FALSE;

	case LRG_UI_EVENT_MOUSE_BUTTON_DOWN:
		if (lrg_ui_event_get_button (event) != 0 || card < 0)
			return FALSE;
		self->pressed = card;
		return TRUE;

	case LRG_UI_EVENT_MOUSE_BUTTON_UP:
		if (lrg_ui_event_get_button (event) != 0)
			return FALSE;
		if (card >= 0 && card == self->pressed && self->app != NULL)
			g_object_set (self->app->settings, "theme", g_array_index (self->cards, ThemeCard, card).value, NULL);
		self->pressed = -1;
		return card >= 0;

	default:
		return FALSE;
	}
}

static void
lrg_editor_theme_gallery_finalize (GObject *object)
{
	LrgEditorThemeGallery *self = LRG_EDITOR_THEME_GALLERY (object);

	g_clear_weak_pointer (&self->app);
	g_clear_pointer (&self->cards, g_array_unref);
	G_OBJECT_CLASS (lrg_editor_theme_gallery_parent_class)->finalize (object);
}

static void
lrg_editor_theme_gallery_class_init (LrgEditorThemeGalleryClass *klass)
{
	LrgWidgetClass *widget_class = LRG_WIDGET_CLASS (klass);

	G_OBJECT_CLASS (klass)->finalize = lrg_editor_theme_gallery_finalize;
	widget_class->measure = lrg_editor_theme_gallery_measure;
	widget_class->paint = lrg_editor_theme_gallery_paint;
	widget_class->handle_event = lrg_editor_theme_gallery_handle_event;
}

static void
lrg_editor_theme_gallery_init (LrgEditorThemeGallery *self)
{
	self->cards = g_array_new (FALSE, TRUE, sizeof (ThemeCard));
	g_array_set_clear_func (self->cards, theme_card_clear);
	self->hover = -1;
	self->pressed = -1;
}

static LrgEditorThemeGallery *
theme_gallery_new (LrgEditorApp *app)
{
	LrgEditorThemeGallery *self = g_object_new (LRG_TYPE_EDITOR_THEME_GALLERY, NULL);
	gsize                  i;

	g_set_weak_pointer (&self->app, app);
	lrg_widget_set_name (LRG_WIDGET (self), "theme-gallery");
	for (i = 0; i < G_N_ELEMENTS (gallery_themes); i++)
		gallery_add (self, gallery_themes[i][0], gallery_themes[i][1]);
	gallery_sync (self);
	return self;
}

/* ==========================================================================
 * Keymap
 * ========================================================================== */

#define LRG_TYPE_EDITOR_KEYMAP (lrg_editor_keymap_get_type ())
G_DECLARE_FINAL_TYPE (LrgEditorKeymap, lrg_editor_keymap, LRG, EDITOR_KEYMAP, LrgContainer)

struct _LrgEditorKeymap
{
	LrgContainer      parent_instance;

	LrgEditorApp     *app;            /* weak */
	gboolean          editable;
	LrgHBox          *top;            /* filter, conflicts, reset all */
	LrgTextInput     *filter;
	LrgLabel         *conflicts;
	LrgScrollBox     *scroll;
	LrgVBox          *rows;
	gchar            *capture_id;     /* capturing for this action */
	gint              capture_index;  /* alternative replaced, -1 adds */
	LrgButton        *capture_button; /* shows "Press keys..." (borrowed) */
	guint             rebuild_idle;
	gulong            accels_id;
};

G_DEFINE_FINAL_TYPE (LrgEditorKeymap, lrg_editor_keymap, LRG_TYPE_CONTAINER)

static void keymap_rebuild (LrgEditorKeymap *self);

/* Canonical form of an accelerator (for conflict counting) */
static gchar *
normalize_accel (const gchar *accel)
{
	GrlKey            key;
	LrgInputModifiers mods;

	if (accel == NULL || !lrg_accel_parse (accel, &key, &mods))
		return NULL;
	return lrg_accel_name (key, mods & MOD_MASK);
}

/* accel -> number of actions bound to it */
static GHashTable *
count_accels (LrgEditorActions *actions)
{
	GHashTable *counts = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
	GList      *ids = lrg_editor_actions_list (actions);
	GList      *l;

	for (l = ids; l != NULL; l = l->next)
	{
		g_auto(GStrv) accels = lrg_editor_actions_get_accels (actions, l->data);
		guint         i;

		for (i = 0; accels != NULL && accels[i] != NULL; i++)
		{
			gchar *norm = normalize_accel (accels[i]);

			if (norm == NULL)
				continue;
			g_hash_table_insert (counts, norm,
			                     GUINT_TO_POINTER (GPOINTER_TO_UINT (g_hash_table_lookup (counts, norm)) + 1));
		}
	}
	g_list_free (ids);
	return counts;
}

static gboolean
rebuild_idle_cb (gpointer user_data)
{
	LrgEditorKeymap *self = LRG_EDITOR_KEYMAP (user_data);

	self->rebuild_idle = 0;
	keymap_rebuild (self);
	return G_SOURCE_REMOVE;
}

static void
queue_rebuild (LrgEditorKeymap *self)
{
	if (self->rebuild_idle == 0)
		self->rebuild_idle = g_idle_add_full (G_PRIORITY_DEFAULT, rebuild_idle_cb, self, NULL);
}

static void
end_capture (LrgEditorKeymap *self)
{
	g_clear_pointer (&self->capture_id, g_free);
	self->capture_button = NULL;
	self->capture_index = -1;
	queue_rebuild (self);
}

static void
begin_capture (LrgEditorKeymap *self,
               const gchar     *id,
               gint             index,
               LrgButton       *button)
{
	if (self->app == NULL)
		return;
	g_free (self->capture_id);
	self->capture_id = g_strdup (id);
	self->capture_index = index;
	self->capture_button = button;
	if (button != NULL)
		lrg_button_set_text (button, "Press keys...");
	lrg_canvas_set_focused_widget (self->app->canvas, LRG_WIDGET (self));
	editor_app_set_status (self->app, "Press the new shortcut for %s (Escape cancels, Backspace removes)",
	                       lrg_editor_actions_get_label (self->app->actions, id));
}

/* Writes @accels (NULL-terminated) as the override of @id */
static void
store_accels (LrgEditorKeymap *self,
              const gchar     *id,
              GPtrArray       *accels)
{
	g_ptr_array_add (accels, NULL);
	lrg_editor_actions_set_accels (self->app->actions, id, (const gchar * const *) accels->pdata);
}

/* The captured key press -> accelerator list of the action */
static void
apply_capture (LrgEditorKeymap   *self,
               GrlKey             key,
               LrgInputModifiers  mods)
{
	g_auto(GStrv)        current = lrg_editor_actions_get_accels (self->app->actions, self->capture_id);
	g_autoptr(GPtrArray) list = g_ptr_array_new_with_free_func (g_free);
	g_autofree gchar    *accel = NULL;
	g_autofree gchar    *label = NULL;
	guint                i;

	/* Backspace / Delete alone removes the alternative being edited */
	if ((key == GRL_KEY_BACKSPACE || key == GRL_KEY_DELETE) && (mods & MOD_MASK) == 0)
	{
		for (i = 0; current != NULL && current[i] != NULL; i++)
			if ((gint) i != self->capture_index)
				g_ptr_array_add (list, g_strdup (current[i]));
		store_accels (self, self->capture_id, list);
		end_capture (self);
		return;
	}

	accel = lrg_accel_name (key, mods & MOD_MASK);
	if (accel == NULL)
		return;   /* a key without a name: keep waiting */
	for (i = 0; current != NULL && current[i] != NULL; i++)
	{
		g_autofree gchar *norm = normalize_accel (current[i]);

		if ((gint) i == self->capture_index)
			g_ptr_array_add (list, g_strdup (accel));
		else if (g_strcmp0 (norm, accel) != 0)
			g_ptr_array_add (list, g_strdup (current[i]));
	}
	if (self->capture_index < 0 || current == NULL || self->capture_index >= (gint) g_strv_length (current))
		g_ptr_array_add (list, g_strdup (accel));
	store_accels (self, self->capture_id, list);
	label = lrg_editor_accel_to_label (accel);
	editor_app_set_status (self->app, "%s bound to %s", lrg_editor_actions_get_label (self->app->actions,
	                                                                                  self->capture_id), label);
	end_capture (self);
}

static gboolean
is_modifier_key (GrlKey key)
{
	return key == GRL_KEY_LEFT_SHIFT || key == GRL_KEY_RIGHT_SHIFT || key == GRL_KEY_LEFT_CONTROL ||
	       key == GRL_KEY_RIGHT_CONTROL || key == GRL_KEY_LEFT_ALT || key == GRL_KEY_RIGHT_ALT ||
	       key == GRL_KEY_LEFT_SUPER || key == GRL_KEY_RIGHT_SUPER;
}

static gboolean
lrg_editor_keymap_handle_event (LrgWidget        *widget,
                                const LrgUIEvent *event)
{
	LrgEditorKeymap *self = LRG_EDITOR_KEYMAP (widget);
	GrlKey           key;

	if (self->capture_id == NULL || self->app == NULL)
		return FALSE;
	switch (lrg_ui_event_get_event_type (event))
	{
	case LRG_UI_EVENT_KEY_DOWN:
		key = lrg_ui_event_get_key (event);
		if (is_modifier_key (key))
			return TRUE;
		if (key == GRL_KEY_ESCAPE && (lrg_ui_event_get_modifiers (event) & MOD_MASK) == 0)
		{
			editor_app_set_status (self->app, "Shortcut unchanged");
			end_capture (self);
			return TRUE;
		}
		apply_capture (self, key, lrg_ui_event_get_modifiers (event));
		return TRUE;

	case LRG_UI_EVENT_KEY_UP:
	case LRG_UI_EVENT_TEXT_INPUT:
	case LRG_UI_EVENT_CANCEL:
		return TRUE;

	default:
		return FALSE;
	}
}

/* --- rows --------------------------------------------------------------- */

static void
on_chip_clicked (LrgButton *button,
                 gpointer   user_data)
{
	LrgEditorKeymap *self = LRG_EDITOR_KEYMAP (user_data);

	begin_capture (self, g_object_get_data (G_OBJECT (button), "action"),
	               GPOINTER_TO_INT (g_object_get_data (G_OBJECT (button), "index")), button);
}

static void
on_remove_clicked (LrgButton *button,
                   gpointer   user_data)
{
	LrgEditorKeymap     *self = LRG_EDITOR_KEYMAP (user_data);
	const gchar         *id = g_object_get_data (G_OBJECT (button), "action");
	gint                 index = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (button), "index"));
	g_auto(GStrv)        current = NULL;
	g_autoptr(GPtrArray) list = g_ptr_array_new_with_free_func (g_free);
	guint                i;

	if (self->app == NULL)
		return;
	current = lrg_editor_actions_get_accels (self->app->actions, id);
	for (i = 0; current != NULL && current[i] != NULL; i++)
		if ((gint) i != index)
			g_ptr_array_add (list, g_strdup (current[i]));
	store_accels (self, id, list);
}

static void
on_reset_clicked (LrgButton *button,
                  gpointer   user_data)
{
	LrgEditorKeymap *self = LRG_EDITOR_KEYMAP (user_data);

	if (self->app != NULL)
		lrg_editor_actions_set_accels (self->app->actions, g_object_get_data (G_OBJECT (button), "action"), NULL);
}

static void
on_reset_all_clicked (LrgButton *button,
                      gpointer   user_data)
{
	LrgEditorKeymap *self = LRG_EDITOR_KEYMAP (user_data);
	GList           *ids;
	GList           *l;

	if (self->app == NULL)
		return;
	ids = lrg_editor_settings_list_keybindings (self->app->settings);
	for (l = ids; l != NULL; l = l->next)
		lrg_editor_actions_set_accels (self->app->actions, l->data, NULL);
	g_list_free (ids);
	editor_app_set_status (self->app, "Every shortcut is back to its default");
}

static LrgButton *
small_button (LrgHBox     *row,
              const gchar *text,
              const gchar *name,
              const gchar *action,
              gint         index)
{
	LrgButton *b = lrg_button_new (text);

	lrg_widget_set_name (LRG_WIDGET (b), name);
	g_object_set_data_full (G_OBJECT (b), "action", g_strdup (action), g_free);
	g_object_set_data (G_OBJECT (b), "index", GINT_TO_POINTER (index));
	lrg_widget_set_valign (LRG_WIDGET (b), LRG_ALIGN_CENTER);
	lrg_container_add_child (LRG_CONTAINER (row), LRG_WIDGET (b));
	g_object_unref (b);
	return b;
}

static void
add_heading (LrgEditorKeymap *self,
             const gchar     *text)
{
	LrgLabel *label = lrg_label_new (text);
	LrgTheme *theme = lrg_widget_get_theme (LRG_WIDGET (self));

	lrg_widget_add_class (LRG_WIDGET (label), "heading");
	lrg_label_set_color (label, lrg_theme_get_color (theme, LRG_THEME_COLOR_ACCENT));
	lrg_widget_set_margin (LRG_WIDGET (label), &(LrgInsets) { .left = 4.0f, .top = 10.0f, .right = 4.0f, .bottom = 2.0f });
	lrg_container_add_child (LRG_CONTAINER (self->rows), LRG_WIDGET (label));
	g_object_unref (label);
}

static void
add_action_row (LrgEditorKeymap *self,
                const gchar     *id,
                GHashTable      *counts)
{
	LrgEditorActions *actions = self->app->actions;
	LrgTheme         *theme = lrg_widget_get_theme (LRG_WIDGET (self));
	g_auto(GStrv)     accels = lrg_editor_actions_get_accels (actions, id);
	g_auto(GStrv)     defaults = lrg_editor_actions_get_default_accels (actions, id);
	g_autofree gchar *joined = (accels != NULL) ? g_strjoinv ("|", accels) : g_strdup ("");
	g_autofree gchar *djoined = (defaults != NULL) ? g_strjoinv ("|", defaults) : g_strdup ("");
	g_autofree gchar *row_name = g_strconcat ("kb-row:", id, NULL);
	LrgHBox          *row = lrg_hbox_new ();
	LrgLabel         *label = lrg_label_new (lrg_editor_actions_get_label (actions, id));
	guint             i;

	lrg_widget_set_name (LRG_WIDGET (row), row_name);
	lrg_container_set_spacing (LRG_CONTAINER (row), 4.0f);
	lrg_widget_set_margin (LRG_WIDGET (row), &(LrgInsets) { .left = 12.0f, .top = 1.0f, .right = 8.0f, .bottom = 1.0f });
	lrg_widget_set_valign (LRG_WIDGET (label), LRG_ALIGN_CENTER);
	lrg_widget_set_fill_weight (LRG_WIDGET (label), 1.0f);
	lrg_container_add_child (LRG_CONTAINER (row), LRG_WIDGET (label));
	g_object_unref (label);

	for (i = 0; accels != NULL && accels[i] != NULL; i++)
	{
		g_autofree gchar *text = lrg_editor_accel_to_label (accels[i]);
		g_autofree gchar *norm = normalize_accel (accels[i]);
		g_autofree gchar *chip_name = g_strdup_printf ("kb-chip:%s:%u", id, i);
		gboolean          conflict = norm != NULL && GPOINTER_TO_UINT (g_hash_table_lookup (counts, norm)) > 1;

		if (self->editable)
		{
			g_autofree gchar *rm_name = g_strdup_printf ("kb-remove:%s:%u", id, i);
			LrgButton        *chip = small_button (row, text != NULL ? text : accels[i], chip_name, id, (gint) i);
			LrgButton        *rm = small_button (row, "x", rm_name, id, (gint) i);

			if (conflict)
				lrg_button_set_text_color (chip, lrg_theme_get_color (theme, LRG_THEME_COLOR_WARNING));
			g_signal_connect_object (chip, "clicked", G_CALLBACK (on_chip_clicked), self, 0);
			g_signal_connect_object (rm, "clicked", G_CALLBACK (on_remove_clicked), self, 0);
			if (g_strcmp0 (self->capture_id, id) == 0 && self->capture_index == (gint) i)
			{
				lrg_button_set_text (chip, "Press keys...");
				self->capture_button = chip;
			}
		}
		else
		{
			LrgLabel *chip = lrg_label_new (text != NULL ? text : accels[i]);

			lrg_widget_set_name (LRG_WIDGET (chip), chip_name);
			lrg_widget_set_valign (LRG_WIDGET (chip), LRG_ALIGN_CENTER);
			lrg_label_set_color (chip, lrg_theme_get_color (theme, conflict ? LRG_THEME_COLOR_WARNING
			                                                                 : LRG_THEME_COLOR_TEXT_MUTED));
			lrg_widget_set_margin (LRG_WIDGET (chip), &(LrgInsets) { .left = 10.0f, .top = 0.0f, .right = 0.0f, .bottom = 0.0f });
			lrg_container_add_child (LRG_CONTAINER (row), LRG_WIDGET (chip));
			g_object_unref (chip);
		}
	}

	if (self->editable)
	{
		g_autofree gchar *add_name = g_strconcat ("kb-add:", id, NULL);
		g_autofree gchar *reset_name = g_strconcat ("kb-reset:", id, NULL);
		LrgButton        *add = small_button (row, (g_strcmp0 (self->capture_id, id) == 0 && self->capture_index < 0)
		                                           ? "Press keys..." : "+", add_name, id, -1);

		g_signal_connect_object (add, "clicked", G_CALLBACK (on_chip_clicked), self, 0);
		if (g_strcmp0 (self->capture_id, id) == 0 && self->capture_index < 0)
			self->capture_button = add;
		if (g_strcmp0 (joined, djoined) != 0)
		{
			LrgButton *reset = small_button (row, "Reset", reset_name, id, -1);

			g_signal_connect_object (reset, "clicked", G_CALLBACK (on_reset_clicked), self, 0);
		}
	}
	lrg_container_add_child (LRG_CONTAINER (self->rows), LRG_WIDGET (row));
	g_object_unref (row);
}

/* Actions matching the filter: the registry's fuzzy search plus every
 * action one of whose shortcut labels contains the text ("ctrl+s") */
static GList *
filtered_ids (LrgEditorKeymap *self,
              const gchar     *filter)
{
	LrgEditorActions *actions = self->app->actions;
	GList            *ids;
	GList            *all;
	GList            *l;
	g_autofree gchar *needle = NULL;

	if (filter == NULL || *filter == '\0')
		return lrg_editor_actions_list (actions);
	ids = lrg_editor_actions_search (actions, filter);
	needle = g_utf8_strdown (filter, -1);
	all = lrg_editor_actions_list (actions);
	for (l = all; l != NULL; l = l->next)
	{
		g_auto(GStrv) accels = NULL;
		guint         i;

		if (g_list_find_custom (ids, l->data, (GCompareFunc) g_strcmp0) != NULL)
			continue;
		accels = lrg_editor_actions_get_accels (actions, l->data);
		for (i = 0; accels != NULL && accels[i] != NULL; i++)
		{
			g_autofree gchar *label = lrg_editor_accel_to_label (accels[i]);
			g_autofree gchar *low = g_utf8_strdown (label != NULL ? label : accels[i], -1);

			if (strstr (low, needle) != NULL)
			{
				ids = g_list_append (ids, l->data);
				break;
			}
		}
	}
	g_list_free (all);
	return ids;
}

static void
keymap_rebuild (LrgEditorKeymap *self)
{
	static const gchar * const nav[][2] = {
		{ "Fly look / move", "Right drag + W A S D Q E (Shift faster, wheel speed)" },
		{ "Orbit", "Alt + left drag, the axis gizmo, Num4/6/8/2" },
		{ "Look along an axis", "Click the axis gizmo, Num7/1/3 (+Ctrl opposite)" },
		{ "Pan", "Middle drag, Alt + Shift + left drag" },
		{ "Zoom", "Mouse wheel, = and -" },
		{ "Other schemes", "Preferences > Viewport > Navigation scheme" },
		{ "Select / cycle overlapping", "Left click (Shift adds, Ctrl toggles)" },
		{ "Box select", "Left drag on empty space" },
		{ "Invert snapping", "Hold Ctrl while dragging a gizmo" },
		{ "Cancel a drag", "Escape or right click" },
	};
	g_autoptr(GHashTable) counts = NULL;
	g_autoptr(GPtrArray)  conflicts = NULL;
	const gchar          *filter;
	const gchar          *category = NULL;
	GList                *ids;
	GList                *l;
	guint                 shown = 0;

	if (self->app == NULL)
		return;
	if (self->rebuild_idle != 0)
	{
		g_source_remove (self->rebuild_idle);
		self->rebuild_idle = 0;
	}
	self->capture_button = NULL;
	lrg_container_remove_all (LRG_CONTAINER (self->rows));
	counts = count_accels (self->app->actions);
	filter = lrg_text_input_get_text (self->filter);

	/* Conflicts summary */
	conflicts = lrg_editor_actions_find_conflicts (self->app->actions);
	if (conflicts != NULL && conflicts->len > 0)
	{
		g_autofree gchar *text = g_strdup_printf ("%u conflicting shortcut%s (drawn in the warning colour)",
		                                          conflicts->len, conflicts->len == 1 ? "" : "s");

		lrg_label_set_text (self->conflicts, text);
	}
	else
		lrg_label_set_text (self->conflicts, "");

	/* The viewport's mouse navigation is no action: list it first */
	if (!self->editable && (filter == NULL || *filter == '\0'))
	{
		gsize i;

		add_heading (self, "Viewport");
		for (i = 0; i < G_N_ELEMENTS (nav); i++)
		{
			LrgHBox  *row = lrg_hbox_new ();
			LrgLabel *what = lrg_label_new (nav[i][0]);
			LrgLabel *how = lrg_label_new (nav[i][1]);

			lrg_widget_set_margin (LRG_WIDGET (row), &(LrgInsets) { .left = 12.0f, .top = 1.0f, .right = 8.0f, .bottom = 1.0f });
			lrg_widget_set_fill_weight (LRG_WIDGET (what), 1.0f);
			lrg_label_set_color (how, lrg_theme_get_color (lrg_widget_get_theme (LRG_WIDGET (self)),
			                                               LRG_THEME_COLOR_TEXT_MUTED));
			lrg_container_add_child (LRG_CONTAINER (row), LRG_WIDGET (what));
			lrg_container_add_child (LRG_CONTAINER (row), LRG_WIDGET (how));
			lrg_container_add_child (LRG_CONTAINER (self->rows), LRG_WIDGET (row));
			g_object_unref (what);
			g_object_unref (how);
			g_object_unref (row);
		}
	}

	ids = filtered_ids (self, filter);
	for (l = ids; l != NULL; l = l->next)
	{
		const gchar   *id = l->data;
		const gchar   *cat = lrg_editor_actions_get_category (self->app->actions, id);
		g_auto(GStrv)  accels = NULL;

		/* The read-only list shows bound actions only */
		if (!self->editable)
		{
			accels = lrg_editor_actions_get_accels (self->app->actions, id);
			if (accels == NULL || accels[0] == NULL)
				continue;
		}
		if ((filter == NULL || *filter == '\0') && g_strcmp0 (cat, category) != 0)
		{
			add_heading (self, cat != NULL ? cat : "Other");
			category = cat;
		}
		add_action_row (self, id, counts);
		shown++;
	}
	g_list_free (ids);
	if (shown == 0)
	{
		LrgLabel *none = lrg_label_new ("No matching actions");

		lrg_widget_add_class (LRG_WIDGET (none), "dim-label");
		lrg_widget_set_margin_all (LRG_WIDGET (none), 12.0f);
		lrg_container_add_child (LRG_CONTAINER (self->rows), LRG_WIDGET (none));
		g_object_unref (none);
	}
	lrg_widget_queue_layout (LRG_WIDGET (self));
}

static void
on_filter_changed (LrgTextInput *input,
                   gpointer      user_data)
{
	keymap_rebuild (LRG_EDITOR_KEYMAP (user_data));
}

static void
on_accels_changed (LrgEditorActions *actions,
                   const gchar      *id,
                   gpointer          user_data)
{
	queue_rebuild (LRG_EDITOR_KEYMAP (user_data));
}

static void
lrg_editor_keymap_layout_children (LrgContainer *container)
{
	LrgEditorKeymap *self = LRG_EDITOR_KEYMAP (container);
	gfloat           w = lrg_widget_get_width (LRG_WIDGET (self));
	gfloat           h = lrg_widget_get_height (LRG_WIDGET (self));
	gfloat           tw = 0.0f;
	gfloat           th = 0.0f;

	/* Children are added one by one in the constructor */
	if (self->top == NULL || self->scroll == NULL)
		return;
	lrg_widget_get_preferred_size (LRG_WIDGET (self->top), &tw, &th);
	lrg_widget_place_in_slot (LRG_WIDGET (self->top), 0.0f, 0.0f, w, th);
	lrg_widget_place_in_slot (LRG_WIDGET (self->scroll), 0.0f, th + 4.0f, w, MAX (h - th - 4.0f, 0.0f));
}

static void
lrg_editor_keymap_measure (LrgWidget *widget,
                           gfloat    *width,
                           gfloat    *height)
{
	*width = 640.0f;
	*height = 420.0f;
}

static void
lrg_editor_keymap_dispose (GObject *object)
{
	LrgEditorKeymap *self = LRG_EDITOR_KEYMAP (object);

	if (self->rebuild_idle != 0)
		g_source_remove (self->rebuild_idle);
	self->rebuild_idle = 0;
	if (self->app != NULL && self->accels_id != 0)
		g_signal_handler_disconnect (self->app->actions, self->accels_id);
	self->accels_id = 0;
	g_clear_weak_pointer (&self->app);
	g_clear_pointer (&self->capture_id, g_free);
	G_OBJECT_CLASS (lrg_editor_keymap_parent_class)->dispose (object);
}

static void
lrg_editor_keymap_class_init (LrgEditorKeymapClass *klass)
{
	LrgWidgetClass *widget_class = LRG_WIDGET_CLASS (klass);

	G_OBJECT_CLASS (klass)->dispose = lrg_editor_keymap_dispose;
	widget_class->measure = lrg_editor_keymap_measure;
	widget_class->handle_event = lrg_editor_keymap_handle_event;
	LRG_CONTAINER_CLASS (klass)->layout_children = lrg_editor_keymap_layout_children;
}

static void
lrg_editor_keymap_init (LrgEditorKeymap *self)
{
	self->capture_index = -1;
}

LrgWidget *
lrg_editor_preferences_create_keymap (LrgEditorApp *app,
                                      gboolean      editable)
{
	LrgEditorKeymap *self;
	LrgTheme        *theme;

	g_return_val_if_fail (LRG_IS_EDITOR_APP (app), NULL);

	self = g_object_new (LRG_TYPE_EDITOR_KEYMAP, NULL);
	g_set_weak_pointer (&self->app, app);
	self->editable = editable;
	lrg_widget_set_name (LRG_WIDGET (self), "keymap");
	lrg_widget_set_focusable (LRG_WIDGET (self), TRUE);
	theme = lrg_widget_get_theme (LRG_WIDGET (app->canvas));

	/* Filter row */
	self->top = lrg_hbox_new ();
	lrg_container_set_spacing (LRG_CONTAINER (self->top), 8.0f);
	self->filter = lrg_text_input_new_with_placeholder ("Filter by action, category or key (e.g. ctrl+s)...");
	lrg_widget_set_name (LRG_WIDGET (self->filter), "keymap-filter");
	lrg_widget_set_fill_weight (LRG_WIDGET (self->filter), 1.0f);
	g_signal_connect_object (self->filter, "text-changed", G_CALLBACK (on_filter_changed), self, 0);
	lrg_container_add_child (LRG_CONTAINER (self->top), LRG_WIDGET (self->filter));
	g_object_unref (self->filter);
	self->conflicts = lrg_label_new ("");
	lrg_widget_set_name (LRG_WIDGET (self->conflicts), "keymap-conflicts");
	lrg_widget_set_valign (LRG_WIDGET (self->conflicts), LRG_ALIGN_CENTER);
	lrg_label_set_color (self->conflicts, lrg_theme_get_color (theme, LRG_THEME_COLOR_WARNING));
	lrg_container_add_child (LRG_CONTAINER (self->top), LRG_WIDGET (self->conflicts));
	g_object_unref (self->conflicts);
	if (editable)
	{
		LrgButton *reset = lrg_button_new ("Reset All Shortcuts");

		lrg_widget_set_name (LRG_WIDGET (reset), "keymap-reset-all");
		g_signal_connect_object (reset, "clicked", G_CALLBACK (on_reset_all_clicked), self, 0);
		lrg_container_add_child (LRG_CONTAINER (self->top), LRG_WIDGET (reset));
		g_object_unref (reset);
	}
	lrg_container_add_child (LRG_CONTAINER (self), LRG_WIDGET (self->top));
	g_object_unref (self->top);

	/* Rows */
	self->scroll = lrg_scroll_box_new ();
	self->rows = lrg_vbox_new ();
	lrg_container_set_spacing (LRG_CONTAINER (self->rows), 1.0f);
	lrg_scroll_box_set_child (self->scroll, LRG_WIDGET (self->rows));
	g_object_unref (self->rows);
	lrg_container_add_child (LRG_CONTAINER (self), LRG_WIDGET (self->scroll));
	g_object_unref (self->scroll);

	self->accels_id = g_signal_connect (app->actions, "accels-changed", G_CALLBACK (on_accels_changed), self);
	keymap_rebuild (self);
	return LRG_WIDGET (self);
}

/* ==========================================================================
 * LrgEditorPreferences
 * ========================================================================== */

struct _LrgEditorPreferences
{
	LrgContainer          parent_instance;

	LrgEditorApp         *app;          /* weak */
	LrgTabView           *tabs;
	LrgHBox              *footer;
	LrgPropertyGrid      *grids[G_N_ELEMENTS (pages)];
	LrgEditorKeymap      *keymap;
	LrgEditorThemeGallery *gallery;
	LrgLabel             *resolve_label;
	LrgLabel             *preview_label;
	LrgLabel             *emacs_label;
	LrgLabel             *server_label;
	gulong                notify_id;
};

G_DEFINE_FINAL_TYPE (LrgEditorPreferences, lrg_editor_preferences, LRG_TYPE_CONTAINER)

static gint
page_index (const gchar *page)
{
	gsize i;

	for (i = 0; i < G_N_ELEMENTS (pages); i++)
		if (g_strcmp0 (pages[i].id, page) == 0)
			return (gint) i;
	return -1;
}

/* The external editor and server status lines */
static void
refresh_info (LrgEditorPreferences *self)
{
	LrgEditorApp     *app = self->app;
	g_autofree gchar *source = NULL;
	g_autofree gchar *resolved = NULL;
	g_autofree gchar *line = NULL;
	g_autofree gchar *preview = NULL;
	g_autoptr(GError) error = NULL;
	g_auto(GStrv)     argv = NULL;
	const gchar      *sock;
	const gchar      *label;

	if (app == NULL || self->resolve_label == NULL)
		return;

	resolved = lrg_external_editor_resolve_preset (app->external, &source);
	label = lrg_external_editor_preset_get_label (resolved);
	line = g_strdup_printf ("Resolves to: %s%s%s%s", label != NULL ? label : (resolved != NULL ? resolved : "?"),
	                        source != NULL ? "  (from " : "", source != NULL ? source : "",
	                        source != NULL ? ")" : "");
	lrg_label_set_text (self->resolve_label, line);

	argv = lrg_external_editor_build_argv (app->external, "/path/to/script.lua", 12, 4, &error);
	preview = (argv != NULL) ? g_strjoinv (" ", argv) : g_strdup (error != NULL ? error->message : "");
	g_free (line);
	line = g_strconcat ("Command: ", preview, NULL);
	lrg_label_set_text (self->preview_label, line);

	sock = lrg_editor_server_get_socket_path (app->server);
	if (lrg_editor_server_has_capability (app->server, "open-file"))
		lrg_label_set_text (self->emacs_label, "Emacs: connected (libregnum.el)");
	else if (sock != NULL)
		lrg_label_set_text (self->emacs_label, "Emacs: not connected (M-x libregnum-connect)");
	else
		lrg_label_set_text (self->emacs_label, "Emacs: the editor server is off");

	g_free (line);
	if (sock != NULL)
		line = g_strdup_printf ("Listening on %s  \xc2\xb7  %u client%s connected", sock,
		                        lrg_editor_server_get_n_clients (app->server),
		                        lrg_editor_server_get_n_clients (app->server) == 1 ? "" : "s");
	else
		line = g_strdup ("Not running");
	lrg_label_set_text (self->server_label, line);
}

static void
on_settings_notify (GObject    *settings,
                    GParamSpec *pspec,
                    gpointer    user_data)
{
	LrgEditorPreferences *self = LRG_EDITOR_PREFERENCES (user_data);

	refresh_info (self);
	if (self->gallery != NULL && g_strcmp0 (g_param_spec_get_name (pspec), "theme") == 0)
		gallery_sync (self->gallery);
}

static LrgLabel *
add_label (LrgVBox     *box,
           const gchar *text,
           const gchar *name,
           gboolean     muted)
{
	LrgLabel *label = lrg_label_new (text);

	if (name != NULL)
		lrg_widget_set_name (LRG_WIDGET (label), name);
	if (muted)
		lrg_widget_add_class (LRG_WIDGET (label), "dim-label");
	lrg_widget_set_margin (LRG_WIDGET (label), &(LrgInsets) { .left = 4.0f, .top = 2.0f, .right = 4.0f, .bottom = 2.0f });
	lrg_container_add_child (LRG_CONTAINER (box), LRG_WIDGET (label));
	g_object_unref (label);
	return label;
}

static LrgButton *
add_button (LrgContainer *box,
            const gchar  *text,
            const gchar  *name,
            GCallback     callback,
            gpointer      data)
{
	LrgButton *button = lrg_button_new (text);

	lrg_widget_set_name (LRG_WIDGET (button), name);
	lrg_widget_set_halign (LRG_WIDGET (button), LRG_ALIGN_START);
	g_signal_connect_object (button, "clicked", callback, data, 0);
	lrg_container_add_child (box, LRG_WIDGET (button));
	g_object_unref (button);
	return button;
}

/* --- buttons ------------------------------------------------------------ */

static gchar *
settings_file (LrgEditorApp *app)
{
	if (app->settings_path != NULL)
		return g_strdup (app->settings_path);
	if (lrg_editor_settings_get_path (app->settings) != NULL)
		return g_strdup (lrg_editor_settings_get_path (app->settings));
	return lrg_editor_settings_get_default_path ();
}

static void
on_edit_yaml (LrgButton *button,
              gpointer   user_data)
{
	LrgEditorPreferences *self = LRG_EDITOR_PREFERENCES (user_data);
	LrgEditorApp         *app = self->app;
	g_autoptr(GError)     error = NULL;
	g_autofree gchar     *path = NULL;

	if (app == NULL)
		return;
	g_object_ref (app);
	path = settings_file (app);
	if (!lrg_editor_app_save_settings (app, &error))
		editor_app_notify (app, EDITOR_NOTIFY_ERROR, "Could not save the preferences: %s", error->message);
	/* Close first: the code panel takes focus once no dialog is up */
	editor_dialog_close_all (app);
	lrg_editor_code_panel_open_script (app, path, 1);
	g_object_unref (app);
}

static void
on_reset_all (LrgButton *button,
              gpointer   user_data)
{
	lrg_editor_preferences_reset_all (LRG_EDITOR_PREFERENCES (user_data));
}

static void
on_test_editor (LrgButton *button,
                gpointer   user_data)
{
	LrgEditorPreferences *self = LRG_EDITOR_PREFERENCES (user_data);
	g_autofree gchar     *path = NULL;
	g_autoptr(GError)     error = NULL;

	if (self->app == NULL)
		return;
	path = settings_file (self->app);
	if (!lrg_editor_app_save_settings (self->app, &error))
		editor_app_notify (self->app, EDITOR_NOTIFY_ERROR, "%s", error->message);
	editor_app_open_file (self->app, path, 1, 1);
}

static void
on_copy_socket (LrgButton *button,
                gpointer   user_data)
{
	LrgEditorPreferences *self = LRG_EDITOR_PREFERENCES (user_data);
	const gchar          *sock;

	if (self->app == NULL)
		return;
	sock = lrg_editor_server_get_socket_path (self->app->server);
	if (sock != NULL)
	{
		editor_app_set_clipboard (self->app, sock);
		editor_app_set_status (self->app, "Copied %s", sock);
	}
}

static void
on_start_server (LrgButton *button,
                 gpointer   user_data)
{
	LrgEditorPreferences *self = LRG_EDITOR_PREFERENCES (user_data);
	g_autoptr(GError)     error = NULL;

	if (self->app == NULL || lrg_editor_server_get_socket_path (self->app->server) != NULL)
		return;
	if (!lrg_editor_app_start_server (self->app, NULL, &error))
		editor_app_notify (self->app, EDITOR_NOTIFY_ERROR, "Server: %s", error->message);
	refresh_info (self);
}

/* "Load YAML...": a theme file through the file dialog, on top of us */
typedef struct
{
	LrgEditorApp  *app;     /* weak */
	LrgFileDialog *dialog;
} ThemeRequest;

static void
on_theme_chosen (GObject      *source,
                 GAsyncResult *result,
                 gpointer      user_data)
{
	ThemeRequest     *req = user_data;
	g_autoptr(GFile)  file = lrg_file_dialog_open_finish (req->dialog, result, NULL);
	g_autofree gchar *path = (file != NULL) ? g_file_get_path (file) : NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(LrgTheme) probe = NULL;

	if (req->app != NULL && path != NULL)
	{
		/* Only switch to a file that really is a theme */
		probe = lrg_theme_new_from_file (path, &error);
		if (probe == NULL)
			editor_app_notify (req->app, EDITOR_NOTIFY_ERROR, "Not a theme: %s", error->message);
		else
			g_object_set (req->app->settings, "theme", path, NULL);
	}
	g_clear_weak_pointer (&req->app);
	g_clear_object (&req->dialog);
	g_free (req);
}

static void
on_load_theme (LrgButton *button,
               gpointer   user_data)
{
	LrgEditorPreferences    *self = LRG_EDITOR_PREFERENCES (user_data);
	ThemeRequest            *req;
	g_autoptr(GListStore)    filters = NULL;
	g_autoptr(LrgFileFilter) filter = NULL;
	g_autofree gchar        *start = NULL;
	g_autoptr(GFile)         folder = NULL;

	if (self->app == NULL)
		return;
	req = g_new0 (ThemeRequest, 1);
	g_set_weak_pointer (&req->app, self->app);
	req->dialog = lrg_file_dialog_new ();
	lrg_file_dialog_set_title (req->dialog, "Load Theme YAML");
	lrg_file_dialog_set_prefer_native (req->dialog, FALSE);
	lrg_file_dialog_set_modal (req->dialog, TRUE);
	start = lrg_editor_settings_find_theme_file ("catppuccin-frappe");
	if (start != NULL)
	{
		g_autofree gchar *dir = g_path_get_dirname (start);

		folder = g_file_new_for_path (dir);
	}
	else
		folder = g_file_new_for_path (g_get_home_dir ());
	lrg_file_dialog_set_initial_folder (req->dialog, folder);
	filters = g_list_store_new (LRG_TYPE_FILE_FILTER);
	filter = lrg_file_filter_new ("Theme YAML");
	lrg_file_filter_add_suffix (filter, ".yaml");
	lrg_file_filter_add_suffix (filter, ".yml");
	g_list_store_append (filters, filter);
	lrg_file_dialog_set_filters (req->dialog, G_LIST_MODEL (filters));
	lrg_file_dialog_open (req->dialog, LRG_WIDGET (self->app->canvas), NULL, on_theme_chosen, req);
}

/* --- pages -------------------------------------------------------------- */

static LrgPropertyGrid *
make_grid (LrgEditorPreferences *self,
           guint                 page)
{
	g_autoptr(LrgEditorPrefsSource) source = prefs_source_new (self->app, pages[page].entries,
	                                                           pages[page].n_entries);
	LrgPropertyGrid                *grid = lrg_property_grid_new ();
	g_autofree gchar               *name = g_strconcat ("prefs-grid:", pages[page].id, NULL);

	lrg_widget_set_name (LRG_WIDGET (grid), name);
	lrg_property_grid_set_label_width (grid, 280.0f);
	lrg_property_grid_set_source (grid, LRG_PROPERTY_SOURCE (source));
	self->grids[page] = grid;
	return grid;
}

/* A scrolling page: optional intro text, the grid, then extras */
static LrgVBox *
add_page (LrgEditorPreferences *self,
          guint                 page,
          const gchar          *intro)
{
	LrgScrollBox *scroll = lrg_scroll_box_new ();
	LrgVBox      *box = lrg_vbox_new ();
	g_autofree gchar *name = g_strconcat ("prefs-page:", pages[page].id, NULL);

	lrg_widget_set_name (LRG_WIDGET (scroll), name);
	lrg_container_set_spacing (LRG_CONTAINER (box), 8.0f);
	lrg_container_set_padding (LRG_CONTAINER (box), 12.0f);
	if (intro != NULL)
		add_label (box, intro, NULL, TRUE);
	if (pages[page].n_entries > 0)
	{
		LrgPropertyGrid *grid = make_grid (self, page);

		lrg_container_add_child (LRG_CONTAINER (box), LRG_WIDGET (grid));
		g_object_unref (grid);
	}
	lrg_scroll_box_set_child (scroll, LRG_WIDGET (box));
	g_object_unref (box);
	lrg_tab_view_add_tab (self->tabs, pages[page].title, LRG_WIDGET (scroll));
	g_object_unref (scroll);
	return box;
}

static void
build_pages (LrgEditorPreferences *self)
{
	LrgVBox *box;
	LrgHBox *row;
	guint    i;

	for (i = 0; i < G_N_ELEMENTS (pages); i++)
	{
		const gchar *id = pages[i].id;

		if (g_strcmp0 (id, "general") == 0)
			add_page (self, i, "Changes apply at once; editor.yaml is saved when the dialog closes.");
		else if (g_strcmp0 (id, "appearance") == 0)
		{
			LrgVBox *theme_box = lrg_vbox_new ();

			box = add_page (self, i, NULL);
			lrg_container_set_spacing (LRG_CONTAINER (theme_box), 8.0f);
			add_label (theme_box, "Theme", "theme-heading", FALSE);
			self->gallery = theme_gallery_new (self->app);
			lrg_container_add_child (LRG_CONTAINER (theme_box), LRG_WIDGET (self->gallery));
			g_object_unref (self->gallery);
			add_button (LRG_CONTAINER (theme_box), "Load YAML...", "theme-load", G_CALLBACK (on_load_theme), self);
			/* The gallery goes above the grid */
			lrg_container_insert_child_at (LRG_CONTAINER (box), LRG_WIDGET (theme_box), 0);
			g_object_unref (theme_box);
		}
		else if (g_strcmp0 (id, "external-editor") == 0)
		{
			box = add_page (self, i, "Where code and data files open. \"auto\" follows $VISUAL / $EDITOR.");
			self->resolve_label = add_label (box, "", "external-resolves", TRUE);
			self->preview_label = add_label (box, "", "external-preview", TRUE);
			self->emacs_label = add_label (box, "", "external-emacs", TRUE);
			add_button (LRG_CONTAINER (box), "Test: open editor.yaml externally", "external-test",
			            G_CALLBACK (on_test_editor), self);
		}
		else if (g_strcmp0 (id, "viewport") == 0)
			add_page (self, i, NULL);
		else if (g_strcmp0 (id, "keybindings") == 0)
		{
			self->keymap = LRG_EDITOR_KEYMAP (lrg_editor_preferences_create_keymap (self->app, TRUE));
			lrg_widget_set_margin_all (LRG_WIDGET (self->keymap), 8.0f);
			lrg_tab_view_add_tab (self->tabs, pages[i].title, LRG_WIDGET (self->keymap));
			g_object_unref (self->keymap);
		}
		else if (g_strcmp0 (id, "server") == 0)
		{
			box = add_page (self, i, "JSON-lines IPC for Emacs, lrgeditorctl and MCP clients.");
			self->server_label = add_label (box, "", "server-status", FALSE);
			row = lrg_hbox_new ();
			lrg_container_set_spacing (LRG_CONTAINER (row), 8.0f);
			add_button (LRG_CONTAINER (row), "Copy socket path", "server-copy", G_CALLBACK (on_copy_socket), self);
			add_button (LRG_CONTAINER (row), "Start now", "server-start", G_CALLBACK (on_start_server), self);
			lrg_container_add_child (LRG_CONTAINER (box), LRG_WIDGET (row));
			g_object_unref (row);
		}
	}
}

static void
lrg_editor_preferences_layout_children (LrgContainer *container)
{
	LrgEditorPreferences *self = LRG_EDITOR_PREFERENCES (container);
	gfloat                w = lrg_widget_get_width (LRG_WIDGET (self));
	gfloat                h = lrg_widget_get_height (LRG_WIDGET (self));
	gfloat                fw = 0.0f;
	gfloat                fh = 0.0f;

	/* Children are added one by one in the constructor */
	if (self->tabs == NULL || self->footer == NULL)
		return;
	lrg_widget_get_preferred_size (LRG_WIDGET (self->footer), &fw, &fh);
	lrg_widget_place_in_slot (LRG_WIDGET (self->tabs), 0.0f, 0.0f, w, MAX (h - fh - 8.0f, 0.0f));
	lrg_widget_place_in_slot (LRG_WIDGET (self->footer), 0.0f, h - fh, w, fh);
}

static void
lrg_editor_preferences_measure (LrgWidget *widget,
                                gfloat    *width,
                                gfloat    *height)
{
	LrgEditorPreferences *self = LRG_EDITOR_PREFERENCES (widget);
	gfloat                cw = 1600.0f;
	gfloat                ch = 960.0f;

	if (self->app != NULL && self->app->canvas != NULL && lrg_widget_get_width (LRG_WIDGET (self->app->canvas)) > 0.0f)
	{
		cw = lrg_widget_get_width (LRG_WIDGET (self->app->canvas));
		ch = lrg_widget_get_height (LRG_WIDGET (self->app->canvas));
	}
	*width = MIN (860.0f, MAX (cw - 120.0f, 420.0f));
	*height = MIN (600.0f, MAX (ch - 220.0f, 320.0f));
}

static void
lrg_editor_preferences_dispose (GObject *object)
{
	LrgEditorPreferences *self = LRG_EDITOR_PREFERENCES (object);

	if (self->app != NULL && self->notify_id != 0)
		g_signal_handler_disconnect (self->app->settings, self->notify_id);
	self->notify_id = 0;
	g_clear_weak_pointer (&self->app);
	G_OBJECT_CLASS (lrg_editor_preferences_parent_class)->dispose (object);
}

static void
lrg_editor_preferences_class_init (LrgEditorPreferencesClass *klass)
{
	LrgWidgetClass *widget_class = LRG_WIDGET_CLASS (klass);

	G_OBJECT_CLASS (klass)->dispose = lrg_editor_preferences_dispose;
	widget_class->measure = lrg_editor_preferences_measure;
	LRG_CONTAINER_CLASS (klass)->layout_children = lrg_editor_preferences_layout_children;
}

static void
lrg_editor_preferences_init (LrgEditorPreferences *self)
{
}

LrgEditorPreferences *
lrg_editor_preferences_new (LrgEditorApp *app)
{
	LrgEditorPreferences *self;

	g_return_val_if_fail (LRG_IS_EDITOR_APP (app), NULL);

	self = g_object_new (LRG_TYPE_EDITOR_PREFERENCES, NULL);
	g_set_weak_pointer (&self->app, app);
	lrg_widget_set_name (LRG_WIDGET (self), "preferences");

	self->tabs = lrg_tab_view_new ();
	lrg_widget_set_name (LRG_WIDGET (self->tabs), "prefs-tabs");
	lrg_container_add_child (LRG_CONTAINER (self), LRG_WIDGET (self->tabs));
	g_object_unref (self->tabs);
	build_pages (self);

	self->footer = lrg_hbox_new ();
	lrg_container_set_spacing (LRG_CONTAINER (self->footer), 8.0f);
	add_button (LRG_CONTAINER (self->footer), "Reset All", "prefs-reset-all", G_CALLBACK (on_reset_all), self);
	add_button (LRG_CONTAINER (self->footer), "Edit editor.yaml", "prefs-edit-yaml", G_CALLBACK (on_edit_yaml),
	            self);
	lrg_container_add_child (LRG_CONTAINER (self), LRG_WIDGET (self->footer));
	g_object_unref (self->footer);

	self->notify_id = g_signal_connect (app->settings, "notify", G_CALLBACK (on_settings_notify), self);
	refresh_info (self);
	return self;
}

LrgEditorApp *
lrg_editor_preferences_get_app (LrgEditorPreferences *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_PREFERENCES (self), NULL);
	return self->app;
}

const gchar *
lrg_editor_preferences_get_page (LrgEditorPreferences *self)
{
	guint i;

	g_return_val_if_fail (LRG_IS_EDITOR_PREFERENCES (self), NULL);

	i = lrg_tab_view_get_active_tab (self->tabs);
	return pages[MIN (i, G_N_ELEMENTS (pages) - 1)].id;
}

gboolean
lrg_editor_preferences_set_page (LrgEditorPreferences *self,
                                 const gchar          *page)
{
	gint i;

	g_return_val_if_fail (LRG_IS_EDITOR_PREFERENCES (self), FALSE);

	i = page_index (page);
	if (i < 0)
		return FALSE;
	lrg_tab_view_set_active_tab (self->tabs, (guint) i);
	return TRUE;
}

LrgPropertyGrid *
lrg_editor_preferences_get_grid (LrgEditorPreferences *self,
                                 const gchar          *page)
{
	gint i;

	g_return_val_if_fail (LRG_IS_EDITOR_PREFERENCES (self), NULL);

	i = page_index (page);
	return (i >= 0) ? self->grids[i] : NULL;
}

const gchar *
lrg_editor_preferences_get_capturing (LrgEditorPreferences *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_PREFERENCES (self), NULL);
	return self->keymap != NULL ? self->keymap->capture_id : NULL;
}

void
lrg_editor_preferences_reset_all (LrgEditorPreferences *self)
{
	static const gchar * const keep[] = {
		"ui-layout", "layout", "recent-projects", "recent-levels", "settings-version", "viewport-cameras",
		"viewport-layout"
	};
	LrgEditorApp *app;
	GValue        saved[G_N_ELEMENTS (keep)];
	gsize         i;

	g_return_if_fail (LRG_IS_EDITOR_PREFERENCES (self));

	app = self->app;
	if (app == NULL)
		return;

	/* Layout, recent files and the format version are not preferences */
	memset (saved, 0, sizeof saved);
	for (i = 0; i < G_N_ELEMENTS (keep); i++)
	{
		GParamSpec *pspec = g_object_class_find_property (G_OBJECT_GET_CLASS (app->settings), keep[i]);

		if (pspec == NULL)
			continue;
		g_value_init (&saved[i], pspec->value_type);
		g_object_get_property (G_OBJECT (app->settings), keep[i], &saved[i]);
	}
	lrg_editor_settings_reset (app->settings);
	for (i = 0; i < G_N_ELEMENTS (keep); i++)
	{
		if (!G_IS_VALUE (&saved[i]))
			continue;
		g_object_set_property (G_OBJECT (app->settings), keep[i], &saved[i]);
		g_value_unset (&saved[i]);
	}

	/* Apply everything (the theme already followed the notify) */
	editor_app_apply_settings (app);
	editor_window_sync_shortcuts (app->window);
	for (i = 0; i < G_N_ELEMENTS (pages); i++)
		if (self->grids[i] != NULL)
			lrg_property_grid_refresh (self->grids[i]);
	if (self->keymap != NULL)
		keymap_rebuild (self->keymap);
	editor_app_notify (app, EDITOR_NOTIFY_INFO, "Preferences reset to defaults");
}

/* ==========================================================================
 * The dialog
 * ========================================================================== */

static void
on_prefs_response (LrgDialog *dialog,
                   gint       response,
                   gpointer   user_data)
{
	LrgEditorApp     *app = LRG_EDITOR_APP (user_data);
	g_autoptr(GError) error = NULL;

	if (g_object_get_data (G_OBJECT (app), DIALOG_KEY) == dialog)
	{
		g_object_set_data (G_OBJECT (app), DIALOG_KEY, NULL);
		lrg_editor_view_state_set_dialog (app->view, NULL);
	}
	if (!lrg_editor_app_save_settings (app, &error))
		editor_app_notify (app, EDITOR_NOTIFY_ERROR, "Could not save the preferences: %s", error->message);
}

LrgEditorPreferences *
lrg_editor_preferences_present (LrgEditorApp *app,
                                const gchar  *page)
{
	g_autoptr(LrgDialog) dialog = NULL;
	LrgEditorPreferences *prefs;

	g_return_val_if_fail (LRG_IS_EDITOR_APP (app), NULL);

	if (app->canvas == NULL)
		return NULL;
	editor_dialog_close_all (app);

	dialog = lrg_dialog_new ("Preferences", NULL);
	prefs = lrg_editor_preferences_new (app);
	if (page != NULL)
		lrg_editor_preferences_set_page (prefs, page);
	lrg_dialog_set_content (dialog, LRG_WIDGET (prefs));
	g_object_unref (prefs);
	lrg_dialog_add_button (dialog, "Close", LRG_DIALOG_RESPONSE_CLOSE, TRUE);

	g_signal_connect_object (dialog, "response", G_CALLBACK (on_prefs_response), app, 0);
	g_object_set_data_full (G_OBJECT (app), DIALOG_KEY, g_object_ref_sink (dialog), g_object_unref);
	lrg_editor_view_state_set_dialog (app->view, "preferences");
	editor_window_set_menus_enabled (app->window, FALSE);
	lrg_dialog_present (dialog, app->canvas);
	return prefs;
}
