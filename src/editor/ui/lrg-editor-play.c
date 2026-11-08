/* lrg-editor-play.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Playback in the editor:
 *
 *  - play-in-editor glue: LrgEditorPlaySession (src/editor) does the work
 *    (private LrgWorld, exact restore on Stop); this file starts and stops
 *    it for the app, mirrors its state into LrgEditorViewState:play-state,
 *    sets the status line, logs, emits the "play" IPC event and drives
 *    autoplay timelines while playing;
 *  - Run Game (Ctrl+F5): the game as a separate process from the
 *    project's run command, a template with {project}, {level} and
 *    {module} placeholders (default: "lrgldr {module} -- --level {level}").
 *    The template is split like a shell would before substitution, so
 *    paths with spaces stay one argument; its output streams into the log;
 *  - timeline preview: scrubbing or playing writes sampled values straight
 *    onto the nodes (outside the undo history). The value each animated
 *    channel had before the preview started is kept in timeline_rest and
 *    written back when the preview ends (Stop, saving, play-in-editor,
 *    switching timelines), so previewing never changes the saved
 *    document. The playhead, play flag and timeline name live in the view
 *    state (timeline-* properties), so timeline.* IPC works headless;
 *  - the tile tool's target node.
 */

#include "lrg-editor-app-private.h"
#include "../../core/lrg-engine.h"
#include <math.h>
#include <signal.h>
#include <string.h>

#define DEFAULT_RUN_COMMAND "lrgldr {module} -- --level {level}"

/* ==========================================================================
 * Play-in-editor
 * ========================================================================== */

static void timeline_play_begin (LrgEditorApp *self);
static void timeline_play_apply (LrgEditorApp *self, gdouble play_time);

static void
sync_play_state (LrgEditorApp *self)
{
	lrg_editor_view_state_set_play_state (self->view, lrg_editor_play_session_get_state (self->play));
	lrg_editor_server_emit_event (self->server, "play", NULL);
}

static void
on_play_started (LrgEditorPlaySession *session,
                 gpointer              user_data)
{
	LrgEditorApp *self = LRG_EDITOR_APP (user_data);

	timeline_play_begin (self);
	lrg_editor_log_append (self->log, G_LOG_LEVEL_MESSAGE, "play", "Play started");
	/* 2D game objects or 3D entities, whichever world is playing */
	editor_app_set_status (self, "Playing (%u objects)", lrg_editor_play_session_get_n_objects (session));
	sync_play_state (self);
}

static void
on_play_stopped (LrgEditorPlaySession *session,
                 gpointer              user_data)
{
	LrgEditorApp *self = LRG_EDITOR_APP (user_data);

	g_hash_table_remove_all (self->timeline_play);
	lrg_editor_log_append (self->log, G_LOG_LEVEL_MESSAGE, "play", "Play stopped");
	editor_app_set_status (self, "Stopped after %.1f s", lrg_editor_play_session_get_time (session));
	sync_play_state (self);
}

static void
on_play_paused (LrgEditorPlaySession *session,
                gboolean              paused,
                gpointer              user_data)
{
	LrgEditorApp *self = LRG_EDITOR_APP (user_data);

	editor_app_set_status (self, paused ? "Paused" : "Resumed");
	sync_play_state (self);
}

static void
on_play_ticked (LrgEditorPlaySession *session,
                gfloat                delta,
                gpointer              user_data)
{
	/* Autoplay timelines pose the nodes (restored when play stops) */
	timeline_play_apply (LRG_EDITOR_APP (user_data), lrg_editor_play_session_get_time (session));
}

void
editor_play_init (LrgEditorApp *self)
{
	self->timeline_rest = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
	self->timeline_play = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
	self->timeline_easing = LRG_EASING_LINEAR;
	self->tile_hover_x = self->tile_hover_y = -1;

	self->play = lrg_editor_play_session_new (self->editor);
	g_signal_connect (self->play, "started", G_CALLBACK (on_play_started), self);
	g_signal_connect (self->play, "stopped", G_CALLBACK (on_play_stopped), self);
	g_signal_connect (self->play, "paused", G_CALLBACK (on_play_paused), self);
	g_signal_connect (self->play, "ticked", G_CALLBACK (on_play_ticked), self);
	self->tile_painter = lrg_editor_tile_painter_new (self->editor);
}

void
editor_play_dispose (LrgEditorApp *self)
{
	if (self->play != NULL)
	{
		lrg_editor_play_session_stop (self->play);
		g_signal_handlers_disconnect_by_data (self->play, self);
	}
	editor_run_stop (self);
	if (self->tile_painter != NULL && lrg_editor_tile_painter_is_stroking (self->tile_painter))
		lrg_editor_tile_painter_end_stroke (self->tile_painter);
	g_clear_object (&self->tile_painter);
	g_clear_object (&self->play);
	g_clear_pointer (&self->timeline_rest, g_hash_table_unref);
	g_clear_pointer (&self->timeline_play, g_hash_table_unref);
	g_clear_pointer (&self->timeline_key_guid, g_free);
	g_clear_pointer (&self->timeline_key_channel, g_free);
}

gboolean
editor_play_is_playing (LrgEditorApp *self)
{
	return self->play != NULL && lrg_editor_play_session_is_playing (self->play);
}

void
editor_play_start (LrgEditorApp *self)
{
	g_autoptr(GError) error = NULL;

	if (editor_play_is_playing (self))
		return;
	/* A timeline preview must not leak into the play snapshot */
	editor_timeline_end_preview (self);
	/* the default engine's registry builds the level's registry components
	 * (ai, light, audio-source, ...); lrgeditor never started the engine,
	 * so without this every one of them was skipped (startup is
	 * headless-safe and refcounted) */
	if (lrg_engine_get_registry (lrg_engine_get_default ()) == NULL &&
	    !lrg_engine_startup (lrg_engine_get_default (), &error))
	{
		editor_app_notify (self, EDITOR_NOTIFY_ERROR, "Cannot play: %s", error->message);
		return;
	}
	lrg_editor_play_session_set_engine (self->play, lrg_engine_get_default ());
	/* the open project's physics settings and layers (NULL: none open) */
	lrg_editor_play_session_set_project (self->play, self->project);
	if (!lrg_editor_play_session_start (self->play, &error))
		editor_app_notify (self, EDITOR_NOTIFY_ERROR, "Cannot play: %s", error->message);
}

void
editor_play_stop (LrgEditorApp *self)
{
	lrg_editor_play_session_stop (self->play);
}

void
editor_play_toggle_pause (LrgEditorApp *self)
{
	lrg_editor_play_session_toggle_pause (self->play);
}

void
editor_play_step (LrgEditorApp *self)
{
	g_autoptr(GError) error = NULL;

	if (!editor_play_is_playing (self))
		editor_timeline_end_preview (self);
	if (!lrg_editor_play_session_step (self->play, &error))
		editor_app_notify (self, EDITOR_NOTIFY_ERROR, "Cannot play: %s", error->message);
	sync_play_state (self);
}

/* The primary selection when it is a tilemap node. */
LrgNode *
editor_tiles_target (LrgEditorApp *self)
{
	LrgNode       *node = lrg_editor_selection_get_primary (lrg_editor_get_selection (self->editor));
	LrgNodeVisual *visual = (node != NULL) ? lrg_node_get_visual (node) : NULL;

	if (visual == NULL || lrg_node_visual_get_kind (visual) != LRG_NODE_VISUAL_TILEMAP)
		return NULL;
	return node;
}

/* ==========================================================================
 * Timeline preview
 * ========================================================================== */

/* The panel's timeline: the chosen one, else the level's first. */
LrgTimeline *
editor_timeline_current (LrgEditorApp *self)
{
	LrgLevel *level = lrg_editor_get_level (self->editor);
	gint      idx = lrg_level_find_timeline (level, lrg_editor_view_state_get_timeline_name (self->view));

	if (idx < 0)
	{
		if (lrg_level_get_n_timelines (level) == 0)
			return NULL;
		idx = 0;
		lrg_editor_view_state_set_timeline_name (self->view,
		                                         lrg_timeline_get_name (lrg_level_get_timeline (level, 0)));
	}
	return lrg_level_get_timeline (level, (guint) idx);
}

static gchar *
rest_key (const gchar *guid,
          const gchar *channel)
{
	return g_strconcat (guid, "\n", channel, NULL);
}

/* Remembers the pre-preview value of every channel @tl animates. */
static void
record_rest (LrgEditorApp *self,
             LrgTimeline  *tl)
{
	LrgNode *root = lrg_level_get_root (lrg_editor_get_level (self->editor));
	guint    i;

	for (i = 0; i < lrg_timeline_get_n_tracks (tl); i++)
	{
		LrgTimelineTrack *t = lrg_timeline_get_track (tl, i);
		gchar            *key = rest_key (lrg_timeline_track_get_node_guid (t), lrg_timeline_track_get_channel (t));
		LrgNode          *node;
		gfloat            value;

		if (g_hash_table_contains (self->timeline_rest, key) ||
		    (node = lrg_node_find_by_guid (root, lrg_timeline_track_get_node_guid (t))) == NULL ||
		    !lrg_timeline_channel_read (node, lrg_timeline_track_get_channel (t), &value))
		{
			g_free (key);
			continue;
		}
		g_hash_table_insert (self->timeline_rest, key, g_memdup2 (&value, sizeof value));
	}
}

void
editor_timeline_end_preview (LrgEditorApp *self)
{
	LrgNode       *root;
	GHashTableIter iter;
	gpointer       key, value;

	lrg_editor_view_state_set_timeline_playing (self->view, FALSE);
	if (!lrg_editor_view_state_get_timeline_previewing (self->view))
		return;
	lrg_editor_view_state_set_timeline_previewing (self->view, FALSE);

	/* Put every previewed channel back to its rest value. */
	root = lrg_level_get_root (lrg_editor_get_level (self->editor));
	g_hash_table_iter_init (&iter, self->timeline_rest);
	while (g_hash_table_iter_next (&iter, &key, &value))
	{
		g_auto(GStrv) parts = g_strsplit (key, "\n", 2);
		LrgNode      *node = (parts[0] != NULL && parts[1] != NULL) ? lrg_node_find_by_guid (root, parts[0]) : NULL;

		if (node != NULL)
			lrg_timeline_channel_write (node, parts[1], *(gfloat *) value);
	}
	g_hash_table_remove_all (self->timeline_rest);
}

void
editor_timeline_seek (LrgEditorApp *self,
                      gfloat        time)
{
	LrgTimeline *tl = editor_timeline_current (self);

	if (tl == NULL || editor_play_is_playing (self))
		return;
	if (!isfinite (time))
		time = 0.0f;
	time = CLAMP (time, 0.0f, lrg_timeline_get_length (tl));
	lrg_editor_view_state_set_timeline_time (self->view, time);
	record_rest (self, tl);
	lrg_editor_view_state_set_timeline_previewing (self->view, TRUE);
	lrg_timeline_apply (tl, lrg_level_get_root (lrg_editor_get_level (self->editor)), time);
}

void
editor_timeline_set_playing (LrgEditorApp *self,
                             gboolean      playing)
{
	LrgTimeline *tl = editor_timeline_current (self);

	if (tl == NULL || editor_play_is_playing (self))
		return;
	if (playing && !lrg_timeline_get_loop (tl) &&
	    lrg_editor_view_state_get_timeline_time (self->view) >= lrg_timeline_get_length (tl))
		lrg_editor_view_state_set_timeline_time (self->view, 0.0f);
	lrg_editor_view_state_set_timeline_playing (self->view, playing);
	editor_timeline_seek (self, lrg_editor_view_state_get_timeline_time (self->view));
}

void
editor_timeline_tick (LrgEditorApp *self,
                      gfloat        delta)
{
	LrgTimeline *tl;
	gfloat       t;

	if (!lrg_editor_view_state_get_timeline_playing (self->view) || (tl = editor_timeline_current (self)) == NULL)
	{
		lrg_editor_view_state_set_timeline_playing (self->view, FALSE);
		return;
	}
	t = lrg_editor_view_state_get_timeline_time (self->view) + CLAMP (delta, 0.0f, 0.1f);
	if (t >= lrg_timeline_get_length (tl))
	{
		if (lrg_timeline_get_loop (tl))
			t = lrg_timeline_wrap_time (tl, t);
		else
		{
			t = lrg_timeline_get_length (tl);
			lrg_editor_view_state_set_timeline_playing (self->view, FALSE);
		}
	}
	editor_timeline_seek (self, t);
}

/* Inserts transform keys for every selected node at the playhead. */
guint
editor_timeline_key_selection (LrgEditorApp *self)
{
	GPtrArray   *nodes = lrg_editor_selection_get_nodes (lrg_editor_get_selection (self->editor));
	LrgTimeline *tl = editor_timeline_current (self);
	const gchar *name;
	guint        i, keys = 0;

	if (nodes->len == 0)
		return 0;
	name = (tl != NULL) ? lrg_timeline_get_name (tl) : "Timeline";
	if (tl == NULL)
		lrg_editor_view_state_set_timeline_name (self->view, name);

	lrg_editor_begin_group (self->editor, "Insert Keyframe");
	for (i = 0; i < nodes->len; i++)
		keys += lrg_editor_timeline_key_node (self->editor, name, g_ptr_array_index (nodes, i),
		                                      lrg_editor_view_state_get_timeline_time (self->view),
		                                      NULL, (LrgEasingType) self->timeline_easing);
	lrg_editor_end_group (self->editor);
	editor_app_set_status (self, "Keyed %u channel(s) at %.2f s", keys,
	                       lrg_editor_view_state_get_timeline_time (self->view));
	return keys;
}

static void
timeline_play_begin (LrgEditorApp *self)
{
	LrgLevel *level = lrg_editor_get_level (self->editor);
	guint     i;

	editor_timeline_end_preview (self);
	g_hash_table_remove_all (self->timeline_play);
	for (i = 0; i < lrg_level_get_n_timelines (level); i++)
	{
		LrgTimeline *tl = lrg_level_get_timeline (level, i);

		if (lrg_timeline_get_autoplay (tl))
			g_hash_table_add (self->timeline_play, g_strdup (lrg_timeline_get_name (tl)));
	}
}

static void
timeline_play_apply (LrgEditorApp *self,
                            gdouble       play_time)
{
	LrgLevel      *level = lrg_editor_get_level (self->editor);
	GHashTableIter iter;
	gpointer       name;

	/* Nodes are restored from the play snapshot when play stops. */
	g_hash_table_iter_init (&iter, self->timeline_play);
	while (g_hash_table_iter_next (&iter, &name, NULL))
	{
		gint idx = lrg_level_find_timeline (level, name);

		if (idx >= 0)
		{
			LrgTimeline *tl = lrg_level_get_timeline (level, (guint) idx);

			lrg_timeline_apply (tl, lrg_level_get_root (level), lrg_timeline_wrap_time (tl, (gfloat) play_time));
		}
	}
}

/* ==========================================================================
 * External runs
 * ========================================================================== */

typedef struct
{
	LrgEditorApp     *self;   /* weak: cleared on dispose via run_cancel */
	GDataInputStream *lines;
	GCancellable     *cancel;
} RunReader;

static void
run_reader_free (RunReader *r)
{
	g_clear_object (&r->lines);
	g_clear_object (&r->cancel);
	g_free (r);
}

static void read_next_line (RunReader *r);

static void
on_line_read (GObject      *source,
              GAsyncResult *result,
              gpointer      user_data)
{
	RunReader        *r = user_data;
	g_autoptr(GError) error = NULL;
	g_autofree gchar *line = NULL;
	gsize             len = 0;
	GLogLevelFlags    level = G_LOG_LEVEL_MESSAGE;

	line = g_data_input_stream_read_line_finish_utf8 (G_DATA_INPUT_STREAM (source), result, &len, &error);
	if (line == NULL || g_cancellable_is_cancelled (r->cancel))
	{
		run_reader_free (r);
		return;
	}

	/* A rough guess at severity from the usual GLib/printf prefixes. */
	if (strstr (line, "CRITICAL") != NULL || strstr (line, "ERROR") != NULL || strstr (line, "error:") != NULL)
		level = G_LOG_LEVEL_CRITICAL;
	else if (strstr (line, "WARNING") != NULL || strstr (line, "warning:") != NULL)
		level = G_LOG_LEVEL_WARNING;
	lrg_editor_log_append (r->self->log, level, "game", line);

	read_next_line (r);
}

static void
read_next_line (RunReader *r)
{
	g_data_input_stream_read_line_async (r->lines, G_PRIORITY_DEFAULT, r->cancel, on_line_read, r);
}

static void
on_run_exited (GObject      *source,
               GAsyncResult *result,
               gpointer      user_data)
{
	LrgEditorApp     *self;
	GSubprocess      *proc = G_SUBPROCESS (source);
	g_autoptr(GError) error = NULL;
	g_autoptr(GCancellable) cancel = user_data;

	if (!g_subprocess_wait_finish (proc, result, &error) || g_cancellable_is_cancelled (cancel))
		return;   /* the app went away, or we were stopped */

	self = g_object_get_data (G_OBJECT (proc), "lrg-editor-app");
	if (self == NULL || self->run_proc != proc)
		return;

	if (g_subprocess_get_if_exited (proc))
		editor_app_notify (self, g_subprocess_get_exit_status (proc) == 0 ? EDITOR_NOTIFY_INFO : EDITOR_NOTIFY_WARNING,
		                   "Game exited with status %d", g_subprocess_get_exit_status (proc));
	else
		editor_app_notify (self, EDITOR_NOTIFY_WARNING, "Game terminated by signal %d", g_subprocess_get_term_sig (proc));

	g_clear_object (&self->run_proc);
	g_clear_object (&self->run_cancel);
	lrg_editor_server_emit_event (self->server, "run", NULL);
}

/* Expands one argv word. */
static gchar *
expand_word (const gchar *word,
             const gchar *project,
             const gchar *level,
             const gchar *module)
{
	GString *s = g_string_new (word);

	g_string_replace (s, "{project}", project != NULL ? project : "", 0);
	g_string_replace (s, "{level}", level != NULL ? level : "", 0);
	g_string_replace (s, "{module}", module != NULL ? module : "", 0);
	return g_string_free (s, FALSE);
}

gboolean
editor_run_start (LrgEditorApp  *self,
                  GError       **error)
{
	const gchar                    *tmpl = NULL;
	const gchar                    *level;
	g_autofree gchar               *module = NULL;
	g_auto(GStrv)                   words = NULL;
	g_autoptr(GPtrArray)            argv = NULL;
	g_autoptr(GSubprocessLauncher)  launcher = NULL;
	RunReader                      *reader;
	gint                            i;

	if (self->run_proc != NULL)
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_BUSY, "The game is already running");
		return FALSE;
	}

	/* The game reads the file, so save first. */
	if (lrg_editor_is_dirty (self->editor) && lrg_editor_get_path (self->editor) != NULL &&
	    !editor_app_save (self))
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED, "Could not save the level before running");
		return FALSE;
	}
	level = lrg_editor_get_path (self->editor);
	if (level == NULL)
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED, "Save the level before running the game");
		return FALSE;
	}

	if (self->project != NULL)
	{
		tmpl = lrg_project_get_run_command (self->project);
		if (lrg_project_get_game_output (self->project) != NULL)
			module = lrg_project_resolve (self->project, lrg_project_get_game_output (self->project));
	}
	if (tmpl == NULL || *tmpl == '\0')
		tmpl = DEFAULT_RUN_COMMAND;
	if (module == NULL && strstr (tmpl, "{module}") != NULL)
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
		             "The project has no game module (game_output) for the run command \"%s\"; "
		             "set one in Project Settings", tmpl);
		return FALSE;
	}

	if (!g_shell_parse_argv (tmpl, NULL, &words, error))
		return FALSE;
	argv = g_ptr_array_new_with_free_func (g_free);
	for (i = 0; words[i] != NULL; i++)
		g_ptr_array_add (argv, expand_word (words[i], self->project_dir, level, module));
	g_ptr_array_add (argv, NULL);

	launcher = g_subprocess_launcher_new (G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_MERGE);
	if (self->project_dir != NULL)
		g_subprocess_launcher_set_cwd (launcher, self->project_dir);
	self->run_proc = g_subprocess_launcher_spawnv (launcher, (const gchar * const *) argv->pdata, error);
	if (self->run_proc == NULL)
		return FALSE;
	g_object_set_data (G_OBJECT (self->run_proc), "lrg-editor-app", self);

	self->run_cancel = g_cancellable_new ();
	reader = g_new0 (RunReader, 1);
	reader->self = self;
	reader->cancel = g_object_ref (self->run_cancel);
	reader->lines = g_data_input_stream_new (g_subprocess_get_stdout_pipe (self->run_proc));
	read_next_line (reader);
	g_subprocess_wait_async (self->run_proc, self->run_cancel, on_run_exited, g_object_ref (self->run_cancel));

	{
		g_autofree gchar *cmd = g_strjoinv (" ", (gchar **) argv->pdata);

		lrg_editor_log_append (self->log, G_LOG_LEVEL_MESSAGE, "run", cmd);
	}
	editor_app_notify (self, EDITOR_NOTIFY_INFO, "Game started");
	lrg_editor_server_emit_event (self->server, "run", NULL);
	return TRUE;
}

void
editor_run_stop (LrgEditorApp *self)
{
	if (self->run_proc == NULL)
		return;

	/* Stop reading and waiting first; nothing may call back into us. */
	g_cancellable_cancel (self->run_cancel);
	g_subprocess_send_signal (self->run_proc, SIGTERM);
	g_object_set_data (G_OBJECT (self->run_proc), "lrg-editor-app", NULL);
	g_clear_object (&self->run_proc);
	g_clear_object (&self->run_cancel);
	editor_app_set_status (self, "Game stopped");
	lrg_editor_server_emit_event (self->server, "run", NULL);
}
