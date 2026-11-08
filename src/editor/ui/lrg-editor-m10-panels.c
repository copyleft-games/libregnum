/* lrg-editor-m10-panels.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "config.h"

#include "lrg-editor-m10-panels.h"
#include "../lrg-editor.h"
#include "../lrg-editor-vcs.h"
#include "../lrg-editor-scenes.h"
#include "../lrg-game-view.h"
#include "../../ui/lrg-vbox.h"
#include "../../ui/lrg-label.h"
#include "../../ui/lrg-button.h"
#include "../../ui/lrg-text-input.h"
#include "../../ui/lrg-container.h"

#include <string.h>

#define VCS_KEY   "lrg-vcs-panel"
#define VIEW_KEY  "lrg-game-view-preset"
#define SCENE_KEY "lrg-scene-bar"

typedef struct
{
	LrgEditorVcs *vcs;
	LrgLabel     *diff;
	LrgLabel     *log;
	LrgLabel     *locks;
	LrgLabel     *magit;
	LrgTextInput *message;
} VcsState;

typedef struct
{
	LrgEditor *editor;
	LrgLabel  *active;
} SceneBar;

static void
label_set_trunc (LrgLabel    *label,
                 const gchar *text)
{
	gchar       *cut = NULL;
	const gchar *shown;

	shown = (text != NULL) ? text : "";
	if (strlen (shown) > 4000)
	{
		cut = g_strndup (shown, 4000);
		shown = cut;
	}
	lrg_label_set_text (label, shown);
	g_free (cut);
}

static void
vcs_state_free (gpointer data)
{
	VcsState *st = data;

	g_clear_object (&st->vcs);
	g_free (st);
}

static void
vcs_refresh (LrgWidget *panel)
{
	VcsState         *st = g_object_get_data (G_OBJECT (panel), VCS_KEY);
	g_autoptr(GError) error = NULL;
	g_autofree gchar *diff = NULL;
	g_autofree gchar *log_text = NULL;
	g_autofree gchar *locks = NULL;
	gboolean          available = FALSE;

	if (st == NULL || st->vcs == NULL)
		return;

	diff = lrg_editor_vcs_diff (st->vcs, &error);
	if (diff == NULL)
		label_set_trunc (st->diff, error != NULL ? error->message : "");
	else
		label_set_trunc (st->diff, diff);
	g_clear_error (&error);

	log_text = lrg_editor_vcs_log (st->vcs, &error);
	if (log_text == NULL)
		label_set_trunc (st->log, error != NULL ? error->message : "");
	else
		label_set_trunc (st->log, log_text);
	g_clear_error (&error);

	locks = lrg_editor_vcs_lfs_locks (st->vcs, &available, &error);
	if (locks == NULL)
		label_set_trunc (st->locks, error != NULL ? error->message : "");
	else if (!available)
		label_set_trunc (st->locks, locks[0] != '\0' ? locks : "(git-lfs not in use)");
	else
		label_set_trunc (st->locks, locks);
	g_clear_error (&error);

	lrg_label_set_text (st->magit, lrg_editor_vcs_magit_handoff (st->vcs));
}

static void
on_vcs_commit (LrgButton *button,
               gpointer   user_data)
{
	LrgWidget        *panel = user_data;
	VcsState         *st = g_object_get_data (G_OBJECT (panel), VCS_KEY);
	const gchar      *msg;
	g_autoptr(GError) error = NULL;
	g_autofree gchar *out = NULL;

	(void) button;
	if (st == NULL || st->vcs == NULL)
		return;
	msg = lrg_text_input_get_text (st->message);
	if (msg == NULL || msg[0] == '\0')
		msg = "update";
	out = lrg_editor_vcs_commit (st->vcs, msg, &error);
	if (out == NULL)
		label_set_trunc (st->log, error != NULL ? error->message : "commit failed");
	else
		vcs_refresh (panel);
}

static LrgLabel *
named_label (LrgVBox     *box,
             const gchar *name,
             const gchar *text)
{
	LrgLabel *label = lrg_label_new (text);

	lrg_widget_set_name (LRG_WIDGET (label), name);
	lrg_container_add_child (LRG_CONTAINER (box), LRG_WIDGET (label));
	g_object_unref (label);
	return label;
}

LrgWidget *
lrg_editor_vcs_panel_new (void)
{
	LrgVBox      *box = lrg_vbox_new ();
	VcsState     *st = g_new0 (VcsState, 1);
	LrgTextInput *message;
	LrgButton    *commit;

	lrg_widget_set_name (LRG_WIDGET (box), "vcs-panel");
	st->diff = named_label (box, "vcs-diff", "");
	st->log = named_label (box, "vcs-log", "");
	st->locks = named_label (box, "vcs-locks", "");
	st->magit = named_label (box, "vcs-magit", "(magit-status)");
	message = lrg_text_input_new ();
	lrg_widget_set_name (LRG_WIDGET (message), "vcs-message");
	lrg_container_add_child (LRG_CONTAINER (box), LRG_WIDGET (message));
	g_object_unref (message);
	st->message = message;
	commit = lrg_button_new ("Commit");
	lrg_widget_set_name (LRG_WIDGET (commit), "vcs-commit");
	lrg_container_add_child (LRG_CONTAINER (box), LRG_WIDGET (commit));
	g_signal_connect (commit, "clicked", G_CALLBACK (on_vcs_commit), box);
	g_object_unref (commit);
	g_object_set_data_full (G_OBJECT (box), VCS_KEY, st, vcs_state_free);
	return LRG_WIDGET (box);
}

void
lrg_editor_vcs_panel_set_project (LrgWidget   *panel,
                                  const gchar *project_dir)
{
	VcsState *st;

	g_return_if_fail (LRG_IS_WIDGET (panel));
	g_return_if_fail (project_dir != NULL);

	st = g_object_get_data (G_OBJECT (panel), VCS_KEY);
	g_return_if_fail (st != NULL);
	g_clear_object (&st->vcs);
	st->vcs = lrg_editor_vcs_new (project_dir);
	vcs_refresh (panel);
}

LrgWidget *
lrg_editor_game_view_panel_new (void)
{
	LrgVBox  *box = lrg_vbox_new ();
	LrgLabel *label;

	lrg_widget_set_name (LRG_WIDGET (box), "game-view-panel");
	label = named_label (box, "game-view-preset", "16:9");
	(void) label;
	g_object_set_data_full (G_OBJECT (box), VIEW_KEY, g_strdup ("16:9"), g_free);
	return LRG_WIDGET (box);
}

void
lrg_editor_game_view_panel_set_preset (LrgWidget   *panel,
                                       const gchar *preset)
{
	LrgWidget *label;

	g_return_if_fail (LRG_IS_WIDGET (panel));
	g_return_if_fail (preset != NULL);

	g_object_set_data_full (G_OBJECT (panel), VIEW_KEY, g_strdup (preset), g_free);
	label = lrg_widget_find (panel, "game-view-preset");
	if (LRG_IS_LABEL (label))
		lrg_label_set_text (LRG_LABEL (label), preset);
}

void
lrg_editor_game_view_panel_get_rect (LrgWidget *panel,
                                     gint       view_w,
                                     gint       view_h,
                                     gint      *x,
                                     gint      *y,
                                     gint      *w,
                                     gint      *h)
{
	const gchar *preset;

	g_return_if_fail (LRG_IS_WIDGET (panel));
	preset = g_object_get_data (G_OBJECT (panel), VIEW_KEY);
	lrg_game_view_letterbox (preset != NULL ? preset : "16:9", view_w, view_h, x, y, w, h);
}

static void
scene_bar_free (gpointer data)
{
	SceneBar *st = data;

	g_clear_object (&st->editor);
	g_free (st);
}

LrgWidget *
lrg_editor_scene_bar_new (LrgEditor *editor)
{
	LrgVBox  *box;
	SceneBar *st;

	g_return_val_if_fail (LRG_IS_EDITOR (editor), NULL);

	box = lrg_vbox_new ();
	lrg_widget_set_name (LRG_WIDGET (box), "scene-bar");
	st = g_new0 (SceneBar, 1);
	st->editor = g_object_ref (editor);
	st->active = named_label (box, "scenes-active", "");
	g_object_set_data_full (G_OBJECT (box), SCENE_KEY, st, scene_bar_free);
	return LRG_WIDGET (box);
}

gboolean
lrg_editor_scene_bar_open (LrgWidget    *panel,
                           const gchar  *path,
                           GError      **error)
{
	SceneBar        *st;
	LrgEditorScenes *scenes;

	g_return_val_if_fail (LRG_IS_WIDGET (panel), FALSE);
	g_return_val_if_fail (path != NULL, FALSE);

	st = g_object_get_data (G_OBJECT (panel), SCENE_KEY);
	g_return_val_if_fail (st != NULL, FALSE);
	if (!lrg_editor_load_level (st->editor, path, error))
		return FALSE;
	scenes = lrg_editor_scenes_ensure (G_OBJECT (st->editor));
	lrg_editor_scenes_add (scenes, path);
	lrg_label_set_text (st->active, path);
	return TRUE;
}

gboolean
lrg_editor_scene_bar_set_active (LrgWidget  *panel,
                                 guint       index,
                                 GError    **error)
{
	SceneBar        *st;
	LrgEditorScenes *scenes;
	const gchar     *path;

	g_return_val_if_fail (LRG_IS_WIDGET (panel), FALSE);

	st = g_object_get_data (G_OBJECT (panel), SCENE_KEY);
	g_return_val_if_fail (st != NULL, FALSE);
	scenes = lrg_editor_scenes_ensure (G_OBJECT (st->editor));
	path = lrg_editor_scenes_get_path (scenes, index);
	if (path == NULL)
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "no scene %u", index);
		return FALSE;
	}
	if (!lrg_editor_load_level (st->editor, path, error))
		return FALSE;
	lrg_editor_scenes_set_active (scenes, index);
	lrg_label_set_text (st->active, path);
	return TRUE;
}
