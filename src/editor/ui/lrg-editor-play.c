/* lrg-editor-play.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Play-in-editor and external game runs.
 *
 * Play-in-editor (F5) materialises the open level into a private LrgWorld
 * with lrg_level_instantiate(); every runtime object carries its node's
 * GUID as its entity tag, so after each world tick the editor copies
 * what the simulation moved back onto the authoring nodes and the
 * viewport shows the game running in place. Stopping puts everything
 * back: edits made while playing are undone (they stay on the redo stack,
 * so "keep the change" is one redo away) and the transforms the
 * simulation wrote are restored from a per-node snapshot. The document's
 * dirty state is restored too.
 *
 * Run Game (Ctrl+F5) starts the game as a separate process from the
 * project's run command, a template with {project}, {level} and {module}
 * placeholders (default: "lrgldr {module} -- --level {level}"). The
 * template is split like a shell would before substitution, so paths with
 * spaces stay one argument. Its output is streamed into the console.
 */

#include "lrg-editor-app-private.h"
#include "../../core/lrg-engine.h"
#include "../../ecs/lrg-game-object.h"
#include <math.h>
#include <signal.h>
#include <string.h>

#define DEFAULT_RUN_COMMAND "lrgldr {module} -- --level {level}"
#define STEP_SECONDS        (1.0f / 60.0f)
#define MAX_TICK_SECONDS    (0.1f)

/* Per-node state captured when play starts. */
typedef struct
{
	LrgNode *copy;      /* owned: the node's state at play start */
	gfloat   x0;        /* entity state written by instantiation */
	gfloat   y0;
	gfloat   rot0;
	gfloat   scale0;
} PlayEntry;

static void
play_entry_free (gpointer data)
{
	PlayEntry *e = data;

	g_clear_object (&e->copy);
	g_free (e);
}

/* ==========================================================================
 * Play-in-editor
 * ========================================================================== */

static gboolean
snapshot_node (LrgNode  *node,
               gpointer  user_data)
{
	GHashTable *play_entries = user_data;
	PlayEntry  *e;

	if (lrg_node_get_guid (node) == NULL)
		return TRUE;

	e = g_new0 (PlayEntry, 1);
	e->copy = lrg_node_new (lrg_node_get_name (node));
	lrg_node_copy_state (e->copy, node);
	g_hash_table_replace (play_entries, g_strdup (lrg_node_get_guid (node)), e);
	return TRUE;
}

/* Remembers what instantiation wrote, so only real simulation changes
 * flow back into the nodes. */
static void
record_entity_baselines (LrgEditorApp *self)
{
	GList *objects = lrg_world_get_objects (self->play_world);
	GList *l;

	for (l = objects; l != NULL; l = l->next)
	{
		GrlEntity   *entity = GRL_ENTITY (l->data);
		const gchar *tag = grl_entity_get_tag (entity);
		PlayEntry   *e = (tag != NULL) ? g_hash_table_lookup (self->play_entries, tag) : NULL;

		if (e == NULL)
			continue;
		e->x0 = grl_entity_get_x (entity);
		e->y0 = grl_entity_get_y (entity);
		e->rot0 = grl_entity_get_rotation (entity);
		e->scale0 = grl_entity_get_scale (entity);
	}
	g_list_free (objects);
}

void
editor_play_start (LrgEditorApp *self)
{
	g_autoptr(GError) error = NULL;
	LrgLevel         *level = lrg_editor_get_level (self->editor);

	if (self->playing)
		return;

	/* Snapshot everything the simulation may touch. */
	g_clear_pointer (&self->play_entries, g_hash_table_unref);
	self->play_entries = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, play_entry_free);
	lrg_node_foreach (lrg_level_get_root (level), snapshot_node, self->play_entries);
	g_free (self->play_snapshot);
	self->play_snapshot = lrg_editor_serialize (self->editor);
	self->play_undo_base = lrg_editor_get_undo_count (self->editor);
	self->play_was_dirty = lrg_editor_is_dirty (self->editor);

	g_clear_object (&self->play_world);
	self->play_world = lrg_world_new ();
	if (!lrg_level_instantiate (level, self->play_world, lrg_engine_get_default (), &error))
	{
		editor_app_notify (self, 3, "Cannot play: %s", error->message);
		g_clear_object (&self->play_world);
		g_clear_pointer (&self->play_entries, g_hash_table_unref);
		return;
	}
	record_entity_baselines (self);

	self->playing = TRUE;
	self->paused = FALSE;
	self->step_once = FALSE;
	self->play_time = 0.0;
	lrg_editor_log_append (self->log, G_LOG_LEVEL_MESSAGE, "play", "Play started");
	editor_app_set_status (self, "Playing (%u objects)", lrg_world_get_object_count (self->play_world));
	lrg_editor_server_emit_event (self->server, "play", NULL);
}

/* Restores one node from its snapshot when the simulation changed it. */
static gboolean
restore_node (LrgNode  *node,
              gpointer  user_data)
{
	GHashTable *play_entries = user_data;
	PlayEntry  *e;

	if (lrg_node_get_guid (node) == NULL)
		return TRUE;
	e = g_hash_table_lookup (play_entries, lrg_node_get_guid (node));
	if (e != NULL && !lrg_node_state_equal (node, e->copy))
		lrg_node_copy_state (node, e->copy);
	return TRUE;
}

void
editor_play_stop (LrgEditorApp *self)
{
	if (!self->playing)
		return;

	self->playing = FALSE;
	self->paused = FALSE;
	g_clear_object (&self->play_world);

	/* Undo what was edited during play (it remains redoable), then put
	 * back the transforms the simulation wrote outside the history. */
	while (lrg_editor_get_undo_count (self->editor) > self->play_undo_base && lrg_editor_can_undo (self->editor))
		lrg_editor_undo (self->editor);
	if (self->play_entries != NULL)
		lrg_node_foreach (lrg_level_get_root (lrg_editor_get_level (self->editor)), restore_node, self->play_entries);
	g_clear_pointer (&self->play_entries, g_hash_table_unref);
	g_clear_pointer (&self->play_snapshot, g_free);

	if (!self->play_was_dirty && lrg_editor_is_dirty (self->editor))
		lrg_editor_mark_clean (self->editor);

	lrg_editor_log_append (self->log, G_LOG_LEVEL_MESSAGE, "play", "Play stopped");
	editor_app_set_status (self, "Stopped after %.1f s", self->play_time);
	lrg_editor_server_emit_event (self->server, "play", NULL);
}

void
editor_play_toggle_pause (LrgEditorApp *self)
{
	if (!self->playing)
		return;

	self->paused = !self->paused;
	editor_app_set_status (self, self->paused ? "Paused" : "Resumed");
	lrg_editor_server_emit_event (self->server, "play", NULL);
}

void
editor_play_step (LrgEditorApp *self)
{
	if (!self->playing)
	{
		editor_play_start (self);
		if (!self->playing)
			return;
	}

	/* One fixed frame, then stay paused. */
	self->paused = TRUE;
	self->step_once = TRUE;
}

/* Copies what the simulation changed onto the authoring nodes. */
static void
sync_back (LrgEditorApp *self)
{
	LrgLevel *level = lrg_editor_get_level (self->editor);
	LrgNode  *root = lrg_level_get_root (level);
	GList    *objects = lrg_world_get_objects (self->play_world);
	GList    *l;

	for (l = objects; l != NULL; l = l->next)
	{
		GrlEntity   *entity = GRL_ENTITY (l->data);
		const gchar *tag = grl_entity_get_tag (entity);
		PlayEntry   *e;
		LrgNode     *node;
		GrlVector3  *loc, *rot;
		gfloat       x, y, r, s;

		if (tag == NULL || (e = g_hash_table_lookup (self->play_entries, tag)) == NULL)
			continue;
		node = lrg_node_find_by_guid (root, tag);
		if (node == NULL)
			continue;

		x = grl_entity_get_x (entity);
		y = grl_entity_get_y (entity);
		r = grl_entity_get_rotation (entity);
		s = grl_entity_get_scale (entity);
		loc = lrg_node_get_location (node);
		rot = lrg_node_get_rotation (node);

		/* Runtime objects are 2D entities: x/y map to the node's x/y,
		 * the entity rotation (degrees) to roll, scale is uniform. */
		if (x != e->x0 || y != e->y0)
			lrg_node_set_location_xyz (node, x, y, loc->z);
		if (r != e->rot0)
		{
			GrlVector3 *r0 = lrg_node_get_rotation (e->copy);

			lrg_node_set_rotation_xyz (node, rot->x, rot->y, r0->z + (r - e->rot0) * (gfloat) G_PI / 180.0f);
		}
		if (s != e->scale0 && e->scale0 != 0.0f)
		{
			GrlVector3 *s0 = lrg_node_get_scale (e->copy);
			gfloat      f = s / e->scale0;

			lrg_node_set_scale_xyz (node, s0->x * f, s0->y * f, s0->z * f);
		}
	}
	g_list_free (objects);
}

void
editor_play_tick (LrgEditorApp *self,
                  gfloat        delta)
{
	if (!self->playing || self->play_world == NULL)
		return;

	if (self->paused && !self->step_once)
		return;

	delta = self->step_once ? STEP_SECONDS : CLAMP (delta, 0.0f, MAX_TICK_SECONDS);
	self->step_once = FALSE;

	lrg_world_update (self->play_world, delta);
	self->play_time += delta;
	sync_back (self);
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
		editor_app_notify (self, g_subprocess_get_exit_status (proc) == 0 ? 0 : 2,
		                   "Game exited with status %d", g_subprocess_get_exit_status (proc));
	else
		editor_app_notify (self, 2, "Game terminated by signal %d", g_subprocess_get_term_sig (proc));

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
	editor_app_notify (self, 0, "Game started");
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
