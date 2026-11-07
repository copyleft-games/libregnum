/* lrg-editor-mixer-panel.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgEditorMixerPanel: the docked "Mixer" panel.
 *
 * Layout (top to bottom): a header row (title, mixer path, meter source,
 * Open / Preview / Save / New chips), then the compact channel strips of
 * the project's default mixer in a sideways scroll box, or an empty-state
 * hint with New when the project has no mixer.
 *
 * What the strips show: the open mixer editor's layout when the file is
 * open in a tab (so unsaved edits show), else the file as last read from
 * disk (re-read when its mtime changes; checked once a second). Strip
 * edits go through the editor, opening it on the first edit.
 *
 * Play: when play-in-editor starts and the audio manager has no game
 * mixer, the panel builds one from the project
 * (lrg_audio_manager_load_project_mixer()) and removes it again when
 * play stops, so the strips meter the game.
 */

#include "config.h"

#define LRG_EDITOR_PANEL_INTERNAL

#include "lrg-editor-mixer-panel.h"
#include "lrg-editor-app-private.h"
#include "lrg-editor-console-panel.h"
#include "../lrg-editor-play-session.h"
#include "../lrg-project.h"
#include "../../audio/lrg-audio-manager.h"
#include "../../ui/lrg-hbox.h"
#include "../../ui/lrg-vbox.h"
#include "../../ui/lrg-label.h"
#include "../../ui/lrg-scroll-box.h"
#include <glib/gstdio.h>

#define PANEL_KEY "lrg-editor-mixer-panel"

struct _LrgEditorMixerPanel
{
	LrgContainer          parent_instance;

	LrgEditorApp         *app;        /* weak */
	LrgEditorPlaySession *play;       /* owned */

	gchar                *path;       /* absolute project mixer path, nullable */
	LrgMixerAsset        *disk;       /* the file as read, nullable */
	gint64                disk_mtime;
	gchar                *disk_error;
	gdouble               poll;       /* seconds until the next path/mtime check */
	gboolean              own_game_mixer; /* we gave the audio manager its mixer */

	LrgWidget            *top;
	LrgWidget            *header;
	LrgLabel             *path_label;
	LrgLabel             *source_label;
	LrgWidget            *open_chip;
	LrgWidget            *preview_chip;
	LrgWidget            *save_chip;
	LrgWidget            *new_chip;
	LrgWidget            *scroll;
	LrgWidget            *strips;
	LrgWidget            *empty;
	LrgWidget            *hint;
};

G_DEFINE_FINAL_TYPE (LrgEditorMixerPanel, lrg_editor_mixer_panel, LRG_TYPE_CONTAINER)

static void sync_widgets (LrgEditorMixerPanel *self);

/* ==========================================================================
 * The mixer shown
 * ========================================================================== */

/* The open editor of the shown mixer; @open opens it. */
static LrgEditorMixerEditor *
panel_editor (LrgEditorMixerPanel *self,
              gboolean             open)
{
	g_autoptr(GError)     error = NULL;
	LrgEditorMixerEditor *ed;

	if (self->app == NULL || self->path == NULL)
		return NULL;
	ed = lrg_editor_mixer_editor_find_for_path (self->app, self->path, open, &error);
	if (ed == NULL && open && error != NULL)
		editor_app_set_status (self->app, "Mixer: %s", error->message);
	return ed;
}

static LrgMixerAsset *
strips_asset (gpointer user_data)
{
	return lrg_editor_mixer_panel_get_asset (LRG_EDITOR_MIXER_PANEL (user_data));
}

static LrgEditorMixerEditor *
strips_editor (gboolean open,
               gpointer user_data)
{
	return panel_editor (LRG_EDITOR_MIXER_PANEL (user_data), open);
}

/* Reads the file when it changed since the last read. */
static void
read_disk (LrgEditorMixerPanel *self,
           gboolean             force)
{
	GStatBuf          st;
	g_autoptr(GError) error = NULL;

	if (self->path == NULL || g_stat (self->path, &st) != 0)
	{
		g_clear_object (&self->disk);
		g_clear_pointer (&self->disk_error, g_free);
		if (self->path != NULL)
			self->disk_error = g_strdup ("The mixer file is missing");
		self->disk_mtime = 0;
		return;
	}
	if (!force && (gint64) st.st_mtime == self->disk_mtime && (self->disk != NULL || self->disk_error != NULL))
		return;
	self->disk_mtime = (gint64) st.st_mtime;
	g_clear_object (&self->disk);
	g_clear_pointer (&self->disk_error, g_free);
	self->disk = lrg_mixer_asset_new_from_file (self->path, &error);
	if (self->disk == NULL)
		self->disk_error = g_strdup (error->message);
}

void
lrg_editor_mixer_panel_reload (LrgEditorMixerPanel *self)
{
	g_autofree gchar *path = NULL;

	g_return_if_fail (LRG_IS_EDITOR_MIXER_PANEL (self));

	path = self->app != NULL ? lrg_editor_mixer_editor_project_mixer_path (self->app) : NULL;
	if (g_strcmp0 (path, self->path) != 0)
	{
		g_free (self->path);
		self->path = g_steal_pointer (&path);
		read_disk (self, TRUE);
	}
	else
		read_disk (self, FALSE);
	sync_widgets (self);
}

const gchar *
lrg_editor_mixer_panel_get_path (LrgEditorMixerPanel *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_MIXER_PANEL (self), NULL);
	return self->path;
}

LrgMixerAsset *
lrg_editor_mixer_panel_get_asset (LrgEditorMixerPanel *self)
{
	LrgEditorMixerEditor *ed;

	g_return_val_if_fail (LRG_IS_EDITOR_MIXER_PANEL (self), NULL);
	ed = panel_editor (self, FALSE);
	if (ed != NULL)
		return lrg_editor_mixer_editor_get_asset (ed);
	return self->disk;
}

LrgWidget *
lrg_editor_mixer_panel_get_strips (LrgEditorMixerPanel *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_MIXER_PANEL (self), NULL);
	return self->strips;
}

/* ==========================================================================
 * Widgets
 * ========================================================================== */

static void
sync_widgets (LrgEditorMixerPanel *self)
{
	LrgEditorMixerEditor *ed = panel_editor (self, FALSE);
	const gchar          *source = "none";
	g_autofree gchar     *rel = NULL;
	g_autofree gchar     *src_text = NULL;
	gboolean              has = self->path != NULL;

	if (self->top == NULL)
		return;
	if (self->app != NULL)
		lrg_editor_mixer_editor_get_meter_mixer (self->app, ed, &source);
	if (has && self->app != NULL)
		rel = editor_app_relative_path (self->app, self->path);
	lrg_label_set_text (self->path_label, rel != NULL ? rel : "No project mixer");
	if (g_strcmp0 (source, "play") == 0)
		src_text = g_strdup ("meters: game");
	else if (g_strcmp0 (source, "preview") == 0)
		src_text = g_strdup ("meters: preview");
	else
		src_text = g_strdup (self->disk_error != NULL && ed == NULL ? self->disk_error : "meters: idle");
	lrg_label_set_text (self->source_label, src_text);
	lrg_widget_set_visible (self->open_chip, has);
	lrg_widget_set_visible (self->preview_chip, has);
	lrg_widget_set_visible (self->save_chip, ed != NULL);
	lrg_widget_set_visible (self->new_chip, !has);
	editor_chip_set_checked (self->preview_chip, ed != NULL && lrg_editor_mixer_editor_get_previewing (ed));
	lrg_widget_set_visible (self->scroll, has);
	lrg_widget_set_visible (self->empty, !has);
	lrg_widget_queue_layout (LRG_WIDGET (self));
}

static void
on_open (LrgWidget *chip,
         gpointer   user_data)
{
	panel_editor (LRG_EDITOR_MIXER_PANEL (user_data), TRUE);
	sync_widgets (LRG_EDITOR_MIXER_PANEL (user_data));
}

static void
on_preview (LrgWidget *chip,
            gpointer   user_data)
{
	LrgEditorMixerPanel  *self = LRG_EDITOR_MIXER_PANEL (user_data);
	LrgEditorMixerEditor *ed = panel_editor (self, TRUE);

	if (ed != NULL)
		lrg_editor_mixer_editor_set_previewing (ed, !lrg_editor_mixer_editor_get_previewing (ed));
	sync_widgets (self);
}

static void
on_save (LrgWidget *chip,
         gpointer   user_data)
{
	LrgEditorMixerPanel  *self = LRG_EDITOR_MIXER_PANEL (user_data);
	LrgEditorMixerEditor *ed = panel_editor (self, FALSE);
	g_autoptr(GError)     error = NULL;

	if (ed == NULL)
		return;
	if (!lrg_editor_data_editor_save (LRG_EDITOR_DATA_EDITOR (ed), &error))
		editor_app_set_status (self->app, "Mixer: %s", error->message);
	else
		editor_app_set_status (self->app, "Saved %s", lrg_editor_data_editor_get_path (LRG_EDITOR_DATA_EDITOR (ed)));
	read_disk (self, TRUE);
}

static void
on_new (LrgWidget *chip,
        gpointer   user_data)
{
	LrgEditorMixerPanel *self = LRG_EDITOR_MIXER_PANEL (user_data);
	LrgEditorActions    *actions;

	if (self->app == NULL)
		return;
	actions = lrg_editor_app_get_actions (self->app);
	if (lrg_editor_actions_has (actions, "mixer.new"))
		lrg_editor_actions_activate (actions, "mixer.new");
	lrg_editor_mixer_panel_reload (self);
}

/* ==========================================================================
 * Play: give the game a mixer when it has none
 * ========================================================================== */

static void
on_play_started (LrgEditorMixerPanel *self)
{
	LrgAudioManager  *audio = lrg_audio_manager_get_default ();
	g_autoptr(GError) error = NULL;
	const gchar      *dir;

	if (self->app == NULL || self->path == NULL || lrg_audio_manager_get_mixer (audio) != NULL)
	{
		sync_widgets (self);
		return;
	}
	dir = lrg_editor_app_get_project_dir (self->app);
	if (dir != NULL && lrg_audio_manager_load_project_mixer (audio, dir, 0, &error))
		self->own_game_mixer = lrg_audio_manager_get_mixer (audio) != NULL;
	else if (error != NULL)
		editor_app_set_status (self->app, "Mixer: %s", error->message);
	sync_widgets (self);
}

static void
on_play_stopped (LrgEditorMixerPanel *self)
{
	if (self->own_game_mixer)
		lrg_audio_manager_set_mixer (lrg_audio_manager_get_default (), NULL);
	self->own_game_mixer = FALSE;
	sync_widgets (self);
}

/* ==========================================================================
 * Actions
 * ========================================================================== */

static void
a_show (gpointer data)
{
	LrgEditorMixerPanel *self = LRG_EDITOR_MIXER_PANEL (data);

	if (self->app != NULL)
		editor_app_show_panel (self->app, LRG_EDITOR_MIXER_PANEL_ID);
	lrg_editor_mixer_panel_reload (self);
}

/* ==========================================================================
 * GObject / widget
 * ========================================================================== */

static gboolean
lrg_editor_mixer_panel_step (LrgWidget *widget,
                             gfloat     delta_time)
{
	LrgEditorMixerPanel *self = LRG_EDITOR_MIXER_PANEL (widget);
	LrgWidgetClass      *parent = LRG_WIDGET_CLASS (lrg_editor_mixer_panel_parent_class);
	gboolean             busy = parent->step != NULL && parent->step (widget, delta_time);

	self->poll -= delta_time;
	if (self->poll <= 0.0)
	{
		self->poll = 1.0;
		lrg_editor_mixer_panel_reload (self);
	}
	return busy;
}

static void
lrg_editor_mixer_panel_layout_children (LrgContainer *container)
{
	LrgEditorMixerPanel *self = LRG_EDITOR_MIXER_PANEL (container);

	editor_panel_fit_toolbar (self->header, lrg_widget_get_width (LRG_WIDGET (self)) - 16.0f);
	editor_panel_place_column (container, self->top, self->scroll, NULL);
	editor_panel_place_column (container, self->top, self->empty, NULL);
}

static void
lrg_editor_mixer_panel_dispose (GObject *object)
{
	LrgEditorMixerPanel *self = LRG_EDITOR_MIXER_PANEL (object);

	if (self->play != NULL)
		g_signal_handlers_disconnect_by_data (self->play, self);
	if (self->own_game_mixer)
		lrg_audio_manager_set_mixer (lrg_audio_manager_get_default (), NULL);
	self->own_game_mixer = FALSE;
	if (self->app != NULL && g_object_get_data (G_OBJECT (self->app), PANEL_KEY) == self)
		g_object_set_data (G_OBJECT (self->app), PANEL_KEY, NULL);
	g_clear_object (&self->play);
	g_clear_weak_pointer (&self->app);
	self->top = NULL;
	G_OBJECT_CLASS (lrg_editor_mixer_panel_parent_class)->dispose (object);
}

static void
lrg_editor_mixer_panel_finalize (GObject *object)
{
	LrgEditorMixerPanel *self = LRG_EDITOR_MIXER_PANEL (object);

	g_free (self->path);
	g_clear_object (&self->disk);
	g_free (self->disk_error);
	G_OBJECT_CLASS (lrg_editor_mixer_panel_parent_class)->finalize (object);
}

static void
lrg_editor_mixer_panel_class_init (LrgEditorMixerPanelClass *klass)
{
	G_OBJECT_CLASS (klass)->dispose = lrg_editor_mixer_panel_dispose;
	G_OBJECT_CLASS (klass)->finalize = lrg_editor_mixer_panel_finalize;
	LRG_WIDGET_CLASS (klass)->step = lrg_editor_mixer_panel_step;
	LRG_CONTAINER_CLASS (klass)->layout_children = lrg_editor_mixer_panel_layout_children;
}

static gpointer
add_to (gpointer box,
        gpointer child)
{
	lrg_container_add_child (LRG_CONTAINER (box), LRG_WIDGET (child));
	g_object_unref (child);
	return child;
}

static void
lrg_editor_mixer_panel_init (LrgEditorMixerPanel *self)
{
}

LrgEditorMixerPanel *
lrg_editor_mixer_panel_new (LrgEditorApp *app)
{
	const LrgInsets      pad = { 8.0f, 6.0f, 8.0f, 4.0f };
	LrgEditorMixerPanel *self;
	LrgWidget           *row, *w, *title;

	g_return_val_if_fail (LRG_IS_EDITOR_APP (app), NULL);

	self = g_object_new (LRG_TYPE_EDITOR_MIXER_PANEL, NULL);
	lrg_widget_set_name (LRG_WIDGET (self), "mixer-panel");
	g_set_weak_pointer (&self->app, app);
	self->play = (app->play != NULL) ? g_object_ref (app->play) : NULL;
	g_object_set_data (G_OBJECT (app), PANEL_KEY, self);

	/* Header */
	self->top = LRG_WIDGET (lrg_vbox_new ());
	lrg_widget_set_name (self->top, "mixer-panel-top");
	lrg_widget_set_margin (self->top, &pad);
	row = LRG_WIDGET (lrg_hbox_new ());
	lrg_widget_set_name (row, "mixer-panel-header");
	lrg_container_set_spacing (LRG_CONTAINER (row), 6.0f);
	self->header = row;
	title = add_to (row, lrg_label_new ("Mixer"));
	lrg_widget_add_class (title, "heading");
	lrg_widget_set_valign (title, LRG_ALIGN_CENTER);
	self->path_label = add_to (row, lrg_label_new (""));
	lrg_widget_set_name (LRG_WIDGET (self->path_label), "mixer-panel-path");
	lrg_widget_add_class (LRG_WIDGET (self->path_label), "dim");
	lrg_widget_set_valign (LRG_WIDGET (self->path_label), LRG_ALIGN_CENTER);
	self->source_label = add_to (row, lrg_label_new (""));
	lrg_widget_set_name (LRG_WIDGET (self->source_label), "mixer-panel-source");
	lrg_widget_add_class (LRG_WIDGET (self->source_label), "dim");
	lrg_widget_set_valign (LRG_WIDGET (self->source_label), LRG_ALIGN_CENTER);
	lrg_widget_set_fill_weight (LRG_WIDGET (self->source_label), 1.0f);
	self->preview_chip = add_to (row, editor_chip_new ("mixer-panel-preview", LRG_ICON_PLAY, "Preview", on_preview,
	                                                   self));
	editor_chip_set_tint (self->preview_chip, LRG_THEME_COLOR_ACCENT);
	self->open_chip = add_to (row, editor_chip_new ("mixer-panel-open", LRG_ICON_EDIT, "Open", on_open, self));
	self->save_chip = add_to (row, editor_chip_new ("mixer-panel-save", LRG_ICON_DOWNLOAD, "Save", on_save, self));
	self->new_chip = add_to (row, editor_chip_new ("mixer-panel-new", LRG_ICON_PLUS, "New Mixer", on_new, self));
	editor_chip_set_tint (self->new_chip, LRG_THEME_COLOR_ACCENT);
	add_to (self->top, row);
	lrg_container_add_child (LRG_CONTAINER (self), self->top);
	g_object_unref (self->top);

	/* Strips */
	self->strips = lrg_editor_mixer_strips_new (app, TRUE, strips_asset, strips_editor, self, NULL);
	self->scroll = LRG_WIDGET (lrg_scroll_box_new ());
	lrg_widget_set_name (self->scroll, "mixer-panel-scroll");
	lrg_scroll_box_set_hscroll (LRG_SCROLL_BOX (self->scroll), TRUE);
	lrg_scroll_box_set_child (LRG_SCROLL_BOX (self->scroll), self->strips);
	g_object_unref (self->strips);
	lrg_container_add_child (LRG_CONTAINER (self), self->scroll);
	g_object_unref (self->scroll);

	/* Empty state */
	self->empty = LRG_WIDGET (lrg_vbox_new ());
	lrg_widget_set_name (self->empty, "mixer-panel-empty");
	lrg_container_set_spacing (LRG_CONTAINER (self->empty), 10.0f);
	lrg_widget_set_margin (self->empty, &pad);
	self->hint = add_to (self->empty, editor_panel_hint_new (
		"mixer-panel-hint", "The project has no default mixer. A mixer layout (.rmixer) routes the game's sounds "
		"through buses (music, sfx, dialogue) with faders, effects, sends, snapshots and ducking; New Mixer "
		"writes audio/main.rmixer and names it in project.ryaml (audio: { mixer: ... })."));
	w = add_to (self->empty, editor_chip_new ("mixer-panel-empty-new", LRG_ICON_PLUS, "New Mixer", on_new, self));
	editor_chip_set_tint (w, LRG_THEME_COLOR_ACCENT);
	lrg_widget_set_halign (w, LRG_ALIGN_CENTER);
	lrg_container_add_child (LRG_CONTAINER (self), self->empty);
	g_object_unref (self->empty);

	if (self->play != NULL)
	{
		g_signal_connect_swapped (self->play, "started", G_CALLBACK (on_play_started), self);
		g_signal_connect_swapped (self->play, "stopped", G_CALLBACK (on_play_stopped), self);
	}

	/* Actions and IPC */
	lrg_editor_actions_add (lrg_editor_app_get_actions (app), "mixer.show", "Mixer Panel", "View", NULL, a_show, NULL,
	                        NULL, self);
	lrg_editor_mixer_editor_install (app);

	lrg_editor_mixer_panel_reload (self);
	return self;
}

LrgEditorMixerPanel *
lrg_editor_mixer_panel_find (LrgEditorApp *app)
{
	g_return_val_if_fail (LRG_IS_EDITOR_APP (app), NULL);
	return g_object_get_data (G_OBJECT (app), PANEL_KEY);
}
