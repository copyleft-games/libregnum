/* lrg-editor-play-session.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgEditorPlaySession: play-in-editor, moved out of the immediate-mode
 * src/editor/ui/lrg-editor-play.c (editor_play_start / stop /
 * toggle_pause / step / tick) with the same snapshot, restore and
 * write-back rules, plus the live object model, the GUID map,
 * apply-to-level and profiler sections.
 *
 * What the old functions did around the session (status line, log lines,
 * the "play" IPC event, timeline preview/autoplay) is left to the caller,
 * which connects to the started / stopped / paused / ticked signals.
 */

#include "config.h"

#include "lrg-editor-play-session.h"
#include "lrg-editor.h"
#include "lrg-level.h"
#include "lrg-node.h"
#include "lrg-component-desc.h"
#include "lrg-level-instantiate.h"
#include "../core/lrg-engine.h"
#include "../core/lrg-registry.h"
#include "../ecs/lrg-world.h"
#include "../ecs/lrg-game-object.h"
#include "../ecs/lrg-component.h"
#include "../debug/lrg-profiler.h"
#include <string.h>

/* Per-node state captured when play starts. */
typedef struct
{
	LrgNode *copy;      /* owned: the node's state at play start */
	gfloat   x0;        /* entity state written by instantiation */
	gfloat   y0;
	gfloat   rot0;
	gfloat   scale0;
} PlayEntry;

/* A value pushed with apply_to_level(), re-applied after Stop. */
typedef struct
{
	gchar  *guid;
	guint   index;      /* component desc index on the node */
	gchar  *key;
	GValue  value;
} Applied;

struct _LrgEditorPlaySession
{
	GObject             parent_instance;

	LrgEditor          *editor;      /* strong ref */
	LrgEngine          *engine;      /* strong ref, nullable = default */
	LrgProfiler        *profiler;    /* strong ref, nullable */

	LrgWorld           *world;       /* NULL when stopped */
	GHashTable         *entries;     /* GUID -> PlayEntry */
	guint               undo_base;
	gboolean            was_dirty;
	LrgEditorPlayState  state;
	gboolean            step_once;
	gdouble             time;

	GListStore         *objects;     /* LrgGameObject, world order */
	GHashTable         *by_guid;     /* owned GUID -> LrgGameObject (held by @objects) */
	GHashTable         *by_object;   /* LrgGameObject -> GUID (key of @by_guid) */
	GPtrArray          *applied;     /* Applied* */
};

G_DEFINE_FINAL_TYPE (LrgEditorPlaySession, lrg_editor_play_session, G_TYPE_OBJECT)

enum
{
	PROP_0,
	PROP_EDITOR,
	PROP_ENGINE,
	PROP_PROFILER,
	PROP_STATE,
	PROP_TIME,
	N_PROPS
};

enum
{
	SIGNAL_STARTED,
	SIGNAL_STOPPED,
	SIGNAL_PAUSED,
	SIGNAL_TICKED,
	N_SIGNALS
};

static GParamSpec *properties[N_PROPS];
static guint       signals[N_SIGNALS];

static void
play_entry_free (gpointer data)
{
	PlayEntry *e = data;

	g_clear_object (&e->copy);
	g_free (e);
}

static void
applied_free (gpointer data)
{
	Applied *a = data;

	g_free (a->guid);
	g_free (a->key);
	g_value_unset (&a->value);
	g_free (a);
}

static LrgEngine *
engine (LrgEditorPlaySession *self)
{
	return self->engine != NULL ? self->engine : lrg_engine_get_default ();
}

static void
set_state (LrgEditorPlaySession *self,
           LrgEditorPlayState    state)
{
	if (self->state == state)
		return;
	self->state = state;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_STATE]);
}

/* --- GObject --------------------------------------------------------------------- */

static void
lrg_editor_play_session_dispose (GObject *object)
{
	LrgEditorPlaySession *self = LRG_EDITOR_PLAY_SESSION (object);

	/* A session dropped while playing still puts the level back. */
	if (self->state != LRG_EDITOR_PLAY_STATE_STOPPED && self->editor != NULL)
		lrg_editor_play_session_stop (self);

	g_clear_object (&self->world);
	g_clear_object (&self->profiler);
	g_clear_object (&self->engine);
	g_clear_object (&self->editor);

	G_OBJECT_CLASS (lrg_editor_play_session_parent_class)->dispose (object);
}

static void
lrg_editor_play_session_finalize (GObject *object)
{
	LrgEditorPlaySession *self = LRG_EDITOR_PLAY_SESSION (object);

	g_clear_pointer (&self->entries, g_hash_table_unref);
	g_clear_pointer (&self->by_object, g_hash_table_unref);
	g_clear_pointer (&self->by_guid, g_hash_table_unref);
	g_clear_pointer (&self->applied, g_ptr_array_unref);
	g_clear_object (&self->objects);

	G_OBJECT_CLASS (lrg_editor_play_session_parent_class)->finalize (object);
}

static void
lrg_editor_play_session_get_property (GObject    *object,
                                      guint       prop_id,
                                      GValue     *value,
                                      GParamSpec *pspec)
{
	LrgEditorPlaySession *self = LRG_EDITOR_PLAY_SESSION (object);

	switch (prop_id)
	{
	case PROP_EDITOR:   g_value_set_object (value, self->editor); break;
	case PROP_ENGINE:   g_value_set_object (value, self->engine); break;
	case PROP_PROFILER: g_value_set_object (value, self->profiler); break;
	case PROP_STATE:    g_value_set_enum (value, (gint) self->state); break;
	case PROP_TIME:     g_value_set_double (value, self->time); break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
lrg_editor_play_session_set_property (GObject      *object,
                                      guint         prop_id,
                                      const GValue *value,
                                      GParamSpec   *pspec)
{
	LrgEditorPlaySession *self = LRG_EDITOR_PLAY_SESSION (object);

	switch (prop_id)
	{
	case PROP_EDITOR:
		g_set_object (&self->editor, g_value_get_object (value));
		break;
	case PROP_ENGINE:
		lrg_editor_play_session_set_engine (self, g_value_get_object (value));
		break;
	case PROP_PROFILER:
		lrg_editor_play_session_set_profiler (self, g_value_get_object (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
lrg_editor_play_session_class_init (LrgEditorPlaySessionClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	GParamFlags   rw = G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY;
	GParamFlags   ro = G_PARAM_READABLE | G_PARAM_STATIC_STRINGS;

	object_class->dispose = lrg_editor_play_session_dispose;
	object_class->finalize = lrg_editor_play_session_finalize;
	object_class->get_property = lrg_editor_play_session_get_property;
	object_class->set_property = lrg_editor_play_session_set_property;

	/**
	 * LrgEditorPlaySession:editor:
	 *
	 * The editor whose level is played.
	 */
	properties[PROP_EDITOR] =
		g_param_spec_object ("editor", "Editor", "The editor whose level is played",
		                     LRG_TYPE_EDITOR,
		                     G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

	/**
	 * LrgEditorPlaySession:engine:
	 *
	 * The engine whose registry builds components, %NULL for the default
	 * engine.
	 */
	properties[PROP_ENGINE] =
		g_param_spec_object ("engine", "Engine", "Engine used to instantiate the level",
		                     LRG_TYPE_ENGINE, rw);

	/**
	 * LrgEditorPlaySession:profiler:
	 *
	 * When set, each tick is timed in its "play.tick" section.
	 */
	properties[PROP_PROFILER] =
		g_param_spec_object ("profiler", "Profiler", "Profiler ticks are timed with",
		                     LRG_TYPE_PROFILER, rw);

	/**
	 * LrgEditorPlaySession:state:
	 *
	 * Stopped, playing or paused.
	 */
	properties[PROP_STATE] =
		g_param_spec_enum ("state", "State", "Play state",
		                   LRG_TYPE_EDITOR_PLAY_STATE, LRG_EDITOR_PLAY_STATE_STOPPED, ro);

	/**
	 * LrgEditorPlaySession:time:
	 *
	 * Simulated seconds since start (notified on every tick).
	 */
	properties[PROP_TIME] =
		g_param_spec_double ("time", "Time", "Simulated seconds since start",
		                     0.0, G_MAXDOUBLE, 0.0, ro);

	g_object_class_install_properties (object_class, N_PROPS, properties);

	/**
	 * LrgEditorPlaySession::started:
	 * @self: the session
	 *
	 * Play started; the world exists and the object model is filled.
	 */
	signals[SIGNAL_STARTED] =
		g_signal_new ("started", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
		              0, NULL, NULL, NULL, G_TYPE_NONE, 0);

	/**
	 * LrgEditorPlaySession::stopped:
	 * @self: the session
	 *
	 * Play stopped and the level was restored.
	 */
	signals[SIGNAL_STOPPED] =
		g_signal_new ("stopped", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
		              0, NULL, NULL, NULL, G_TYPE_NONE, 0);

	/**
	 * LrgEditorPlaySession::paused:
	 * @self: the session
	 * @paused: whether the simulation is now paused
	 *
	 * Pause was toggled.
	 */
	signals[SIGNAL_PAUSED] =
		g_signal_new ("paused", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
		              0, NULL, NULL, NULL, G_TYPE_NONE, 1, G_TYPE_BOOLEAN);

	/**
	 * LrgEditorPlaySession::ticked:
	 * @self: the session
	 * @delta: the seconds simulated
	 *
	 * The world was updated and the nodes show the result (timeline
	 * autoplay hooks in here with lrg_editor_play_session_get_time()).
	 */
	signals[SIGNAL_TICKED] =
		g_signal_new ("ticked", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
		              0, NULL, NULL, NULL, G_TYPE_NONE, 1, G_TYPE_FLOAT);
}

static void
lrg_editor_play_session_init (LrgEditorPlaySession *self)
{
	self->state = LRG_EDITOR_PLAY_STATE_STOPPED;
	self->objects = g_list_store_new (LRG_TYPE_GAME_OBJECT);
	self->by_guid = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
	self->by_object = g_hash_table_new (g_direct_hash, g_direct_equal);
	self->applied = g_ptr_array_new_with_free_func (applied_free);
}

LrgEditorPlaySession *
lrg_editor_play_session_new (LrgEditor *editor)
{
	g_return_val_if_fail (LRG_IS_EDITOR (editor), NULL);

	return g_object_new (LRG_TYPE_EDITOR_PLAY_SESSION, "editor", editor, NULL);
}

LrgEditor *
lrg_editor_play_session_get_editor (LrgEditorPlaySession *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_PLAY_SESSION (self), NULL);

	return self->editor;
}

LrgEngine *
lrg_editor_play_session_get_engine (LrgEditorPlaySession *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_PLAY_SESSION (self), NULL);

	return self->engine;
}

void
lrg_editor_play_session_set_engine (LrgEditorPlaySession *self,
                                    LrgEngine            *new_engine)
{
	g_return_if_fail (LRG_IS_EDITOR_PLAY_SESSION (self));
	g_return_if_fail (new_engine == NULL || LRG_IS_ENGINE (new_engine));

	if (g_set_object (&self->engine, new_engine))
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ENGINE]);
}

LrgProfiler *
lrg_editor_play_session_get_profiler (LrgEditorPlaySession *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_PLAY_SESSION (self), NULL);

	return self->profiler;
}

void
lrg_editor_play_session_set_profiler (LrgEditorPlaySession *self,
                                      LrgProfiler          *profiler)
{
	g_return_if_fail (LRG_IS_EDITOR_PLAY_SESSION (self));
	g_return_if_fail (profiler == NULL || LRG_IS_PROFILER (profiler));

	if (g_set_object (&self->profiler, profiler))
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_PROFILER]);
}

/* ==========================================================================
 * Live objects and the GUID map
 * ========================================================================== */

/* Rebuilds both directions of the GUID map from the object model. */
static void
rebuild_maps (LrgEditorPlaySession *self)
{
	guint i, n;

	g_hash_table_remove_all (self->by_object);
	g_hash_table_remove_all (self->by_guid);

	n = g_list_model_get_n_items (G_LIST_MODEL (self->objects));
	for (i = 0; i < n; i++)
	{
		g_autoptr(LrgGameObject) obj = g_list_model_get_item (G_LIST_MODEL (self->objects), i);
		const gchar             *tag = grl_entity_get_tag (GRL_ENTITY (obj));
		gchar                   *key;

		/* The first object with a GUID wins (instantiation tags one
		 * object per node). */
		if (tag == NULL || g_hash_table_contains (self->by_guid, tag))
			continue;
		key = g_strdup (tag);
		g_hash_table_insert (self->by_guid, key, obj);   /* borrowed: the store holds it */
		g_hash_table_insert (self->by_object, obj, key);
	}
}

void
lrg_editor_play_session_sync_objects (LrgEditorPlaySession *self)
{
	g_autoptr(GPtrArray) now = NULL;
	GList               *objects, *l;
	guint                n_old, n_new, prefix = 0, suffix = 0;

	g_return_if_fail (LRG_IS_EDITOR_PLAY_SESSION (self));

	/* The world as it is now (empty when stopped). */
	now = g_ptr_array_new ();
	objects = (self->world != NULL) ? lrg_world_get_objects (self->world) : NULL;
	for (l = objects; l != NULL; l = l->next)
		g_ptr_array_add (now, l->data);
	g_list_free (objects);

	/* Keep the common head and tail and splice the middle, so a spawned
	 * or destroyed object is one small items-changed. */
	n_old = g_list_model_get_n_items (G_LIST_MODEL (self->objects));
	n_new = now->len;
	while (prefix < n_old && prefix < n_new)
	{
		g_autoptr(GObject) item = g_list_model_get_item (G_LIST_MODEL (self->objects), prefix);

		if ((gpointer) item != g_ptr_array_index (now, prefix))
			break;
		prefix++;
	}
	while (suffix < n_old - prefix && suffix < n_new - prefix)
	{
		g_autoptr(GObject) item = g_list_model_get_item (G_LIST_MODEL (self->objects), n_old - 1 - suffix);

		if ((gpointer) item != g_ptr_array_index (now, n_new - 1 - suffix))
			break;
		suffix++;
	}
	if (prefix == n_old && prefix == n_new)
		return;

	g_list_store_splice (self->objects, prefix, n_old - prefix - suffix,
	                     now->pdata + prefix, n_new - prefix - suffix);
	rebuild_maps (self);
}

GListModel *
lrg_editor_play_session_get_objects (LrgEditorPlaySession *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_PLAY_SESSION (self), NULL);

	return G_LIST_MODEL (self->objects);
}

LrgGameObject *
lrg_editor_play_session_get_object_for_guid (LrgEditorPlaySession *self,
                                             const gchar          *guid)
{
	g_return_val_if_fail (LRG_IS_EDITOR_PLAY_SESSION (self), NULL);

	if (guid == NULL)
		return NULL;
	return g_hash_table_lookup (self->by_guid, guid);
}

LrgGameObject *
lrg_editor_play_session_get_object_for_node (LrgEditorPlaySession *self,
                                             LrgNode              *node)
{
	g_return_val_if_fail (LRG_IS_EDITOR_PLAY_SESSION (self), NULL);
	g_return_val_if_fail (LRG_IS_NODE (node), NULL);

	return lrg_editor_play_session_get_object_for_guid (self, lrg_node_get_guid (node));
}

const gchar *
lrg_editor_play_session_get_guid_for_object (LrgEditorPlaySession *self,
                                             LrgGameObject        *object)
{
	g_return_val_if_fail (LRG_IS_EDITOR_PLAY_SESSION (self), NULL);
	g_return_val_if_fail (LRG_IS_GAME_OBJECT (object), NULL);

	return g_hash_table_lookup (self->by_object, object);
}

LrgNode *
lrg_editor_play_session_get_node_for_object (LrgEditorPlaySession *self,
                                             LrgGameObject        *object)
{
	const gchar *guid;

	g_return_val_if_fail (LRG_IS_EDITOR_PLAY_SESSION (self), NULL);
	g_return_val_if_fail (LRG_IS_GAME_OBJECT (object), NULL);

	guid = g_hash_table_lookup (self->by_object, object);
	if (guid == NULL)
		return NULL;
	return lrg_node_find_by_guid (lrg_level_get_root (lrg_editor_get_level (self->editor)), guid);
}

/* ==========================================================================
 * Start / stop
 * ========================================================================== */

static gboolean
snapshot_node (LrgNode  *node,
               gpointer  user_data)
{
	GHashTable *entries = user_data;
	PlayEntry  *e;

	if (lrg_node_get_guid (node) == NULL)
		return TRUE;

	e = g_new0 (PlayEntry, 1);
	e->copy = lrg_node_new (lrg_node_get_name (node));
	lrg_node_copy_state (e->copy, node);
	g_hash_table_replace (entries, g_strdup (lrg_node_get_guid (node)), e);
	return TRUE;
}

/* Remembers what instantiation wrote, so only real simulation changes
 * flow back into the nodes. */
static void
record_entity_baselines (LrgEditorPlaySession *self)
{
	GList *objects = lrg_world_get_objects (self->world);
	GList *l;

	for (l = objects; l != NULL; l = l->next)
	{
		GrlEntity   *entity = GRL_ENTITY (l->data);
		const gchar *tag = grl_entity_get_tag (entity);
		PlayEntry   *e = (tag != NULL) ? g_hash_table_lookup (self->entries, tag) : NULL;

		if (e == NULL)
			continue;
		e->x0 = grl_entity_get_x (entity);
		e->y0 = grl_entity_get_y (entity);
		e->rot0 = grl_entity_get_rotation (entity);
		e->scale0 = grl_entity_get_scale (entity);
	}
	g_list_free (objects);
}

gboolean
lrg_editor_play_session_start (LrgEditorPlaySession  *self,
                               GError               **error)
{
	LrgLevel *level;

	g_return_val_if_fail (LRG_IS_EDITOR_PLAY_SESSION (self), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	if (self->state != LRG_EDITOR_PLAY_STATE_STOPPED)
		return TRUE;

	level = lrg_editor_get_level (self->editor);

	/* Snapshot everything the simulation may touch, and the history and
	 * dirty state Stop goes back to. */
	g_clear_pointer (&self->entries, g_hash_table_unref);
	self->entries = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, play_entry_free);
	lrg_node_foreach (lrg_level_get_root (level), snapshot_node, self->entries);
	self->undo_base = lrg_editor_get_undo_count (self->editor);
	self->was_dirty = lrg_editor_is_dirty (self->editor);
	g_ptr_array_set_size (self->applied, 0);

	/* Materialise the level. */
	g_clear_object (&self->world);
	self->world = lrg_world_new ();
	if (!lrg_level_instantiate (level, self->world, engine (self), error))
	{
		g_clear_object (&self->world);
		g_clear_pointer (&self->entries, g_hash_table_unref);
		return FALSE;
	}
	record_entity_baselines (self);

	self->step_once = FALSE;
	self->time = 0.0;
	g_object_freeze_notify (G_OBJECT (self));
	set_state (self, LRG_EDITOR_PLAY_STATE_PLAYING);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TIME]);
	g_object_thaw_notify (G_OBJECT (self));

	lrg_editor_play_session_sync_objects (self);
	g_signal_emit (self, signals[SIGNAL_STARTED], 0);
	return TRUE;
}

/* Restores one node from its snapshot when the simulation changed it. */
static gboolean
restore_node (LrgNode  *node,
              gpointer  user_data)
{
	GHashTable *entries = user_data;
	PlayEntry  *e;

	if (lrg_node_get_guid (node) == NULL)
		return TRUE;
	e = g_hash_table_lookup (entries, lrg_node_get_guid (node));
	if (e != NULL && !lrg_node_state_equal (node, e->copy))
		lrg_node_copy_state (node, e->copy);
	return TRUE;
}

/* Re-applies the "Apply to level" values after the restore, together as
 * one undo step. */
static gboolean
reapply_applied (LrgEditorPlaySession *self)
{
	LrgNode  *root = lrg_level_get_root (lrg_editor_get_level (self->editor));
	gboolean  any = FALSE;
	guint     i;

	if (self->applied->len == 0)
		return FALSE;

	lrg_editor_begin_group (self->editor, "Apply Play Changes");
	for (i = 0; i < self->applied->len; i++)
	{
		Applied *a = g_ptr_array_index (self->applied, i);
		LrgNode *node = lrg_node_find_by_guid (root, a->guid);

		if (node != NULL &&
		    lrg_editor_set_component_value (self->editor, node, a->index, a->key, &a->value, FALSE))
			any = TRUE;
	}
	lrg_editor_end_group (self->editor);
	g_ptr_array_set_size (self->applied, 0);
	return any;
}

void
lrg_editor_play_session_stop (LrgEditorPlaySession *self)
{
	gboolean applied;

	g_return_if_fail (LRG_IS_EDITOR_PLAY_SESSION (self));

	if (self->state == LRG_EDITOR_PLAY_STATE_STOPPED)
		return;

	/* Drop the world (and the live model that holds its objects). */
	self->step_once = FALSE;
	g_clear_object (&self->world);
	lrg_editor_play_session_sync_objects (self);

	/* Undo what was edited during play (it remains redoable), then put
	 * back the transforms the simulation wrote outside the history. */
	while (lrg_editor_get_undo_count (self->editor) > self->undo_base && lrg_editor_can_undo (self->editor))
		lrg_editor_undo (self->editor);
	if (self->entries != NULL)
		lrg_node_foreach (lrg_level_get_root (lrg_editor_get_level (self->editor)), restore_node,
		                  self->entries);
	g_clear_pointer (&self->entries, g_hash_table_unref);

	/* Values the user chose to keep come back as one step; otherwise the
	 * document is exactly as clean as it was. */
	applied = reapply_applied (self);
	if (!applied && !self->was_dirty && lrg_editor_is_dirty (self->editor))
		lrg_editor_mark_clean (self->editor);

	set_state (self, LRG_EDITOR_PLAY_STATE_STOPPED);
	g_signal_emit (self, signals[SIGNAL_STOPPED], 0);
}

void
lrg_editor_play_session_set_paused (LrgEditorPlaySession *self,
                                    gboolean              paused)
{
	LrgEditorPlayState next;

	g_return_if_fail (LRG_IS_EDITOR_PLAY_SESSION (self));

	if (self->state == LRG_EDITOR_PLAY_STATE_STOPPED)
		return;
	next = paused ? LRG_EDITOR_PLAY_STATE_PAUSED : LRG_EDITOR_PLAY_STATE_PLAYING;
	if (self->state == next)
		return;
	set_state (self, next);
	g_signal_emit (self, signals[SIGNAL_PAUSED], 0, paused ? TRUE : FALSE);
}

void
lrg_editor_play_session_toggle_pause (LrgEditorPlaySession *self)
{
	g_return_if_fail (LRG_IS_EDITOR_PLAY_SESSION (self));

	lrg_editor_play_session_set_paused (self, self->state == LRG_EDITOR_PLAY_STATE_PLAYING);
}

gboolean
lrg_editor_play_session_step (LrgEditorPlaySession  *self,
                              GError               **error)
{
	g_return_val_if_fail (LRG_IS_EDITOR_PLAY_SESSION (self), FALSE);

	if (self->state == LRG_EDITOR_PLAY_STATE_STOPPED && !lrg_editor_play_session_start (self, error))
		return FALSE;

	/* One fixed frame, then stay paused. */
	lrg_editor_play_session_set_paused (self, TRUE);
	self->step_once = TRUE;
	return TRUE;
}

/* ==========================================================================
 * Ticking
 * ========================================================================== */

/* Copies what the simulation changed onto the authoring nodes. */
static void
sync_back (LrgEditorPlaySession *self)
{
	LrgNode *root = lrg_level_get_root (lrg_editor_get_level (self->editor));
	GList   *objects = lrg_world_get_objects (self->world);
	GList   *l;

	for (l = objects; l != NULL; l = l->next)
	{
		GrlEntity   *entity = GRL_ENTITY (l->data);
		const gchar *tag = grl_entity_get_tag (entity);
		PlayEntry   *e;
		LrgNode     *node;
		GrlVector3  *loc, *rot;
		gfloat       x, y, r, s;

		if (tag == NULL || (e = g_hash_table_lookup (self->entries, tag)) == NULL)
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

gboolean
lrg_editor_play_session_tick (LrgEditorPlaySession *self,
                              gfloat                delta)
{
	g_return_val_if_fail (LRG_IS_EDITOR_PLAY_SESSION (self), FALSE);

	if (self->state == LRG_EDITOR_PLAY_STATE_STOPPED || self->world == NULL)
		return FALSE;
	if (self->state == LRG_EDITOR_PLAY_STATE_PAUSED && !self->step_once)
		return FALSE;

	delta = self->step_once ? LRG_EDITOR_PLAY_STEP_SECONDS : CLAMP (delta, 0.0f, LRG_EDITOR_PLAY_MAX_TICK_SECONDS);
	self->step_once = FALSE;

	/* The simulation and the write-back are what the profiler times. */
	if (self->profiler != NULL)
		lrg_profiler_begin_section (self->profiler, LRG_EDITOR_PLAY_PROFILER_SECTION);
	lrg_world_update (self->world, delta);
	self->time += delta;
	sync_back (self);
	if (self->profiler != NULL)
		lrg_profiler_end_section (self->profiler, LRG_EDITOR_PLAY_PROFILER_SECTION);

	lrg_editor_play_session_sync_objects (self);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TIME]);
	g_signal_emit (self, signals[SIGNAL_TICKED], 0, delta);
	return TRUE;
}

/* ==========================================================================
 * State
 * ========================================================================== */

LrgEditorPlayState
lrg_editor_play_session_get_state (LrgEditorPlaySession *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_PLAY_SESSION (self), LRG_EDITOR_PLAY_STATE_STOPPED);

	return self->state;
}

gboolean
lrg_editor_play_session_is_playing (LrgEditorPlaySession *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_PLAY_SESSION (self), FALSE);

	return self->state != LRG_EDITOR_PLAY_STATE_STOPPED;
}

gboolean
lrg_editor_play_session_is_paused (LrgEditorPlaySession *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_PLAY_SESSION (self), FALSE);

	return self->state == LRG_EDITOR_PLAY_STATE_PAUSED;
}

gdouble
lrg_editor_play_session_get_time (LrgEditorPlaySession *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_PLAY_SESSION (self), 0.0);

	return self->time;
}

LrgWorld *
lrg_editor_play_session_get_world (LrgEditorPlaySession *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_PLAY_SESSION (self), NULL);

	return self->world;
}

/* ==========================================================================
 * Apply to level
 * ========================================================================== */

/* Converts a live property value into what the component description
 * should store: the type already stored under @key, or else the types
 * the level loader produces (bool, int64, double, string). */
static gboolean
desc_value_for (LrgComponentDesc *desc,
                const gchar      *key,
                const GValue     *live,
                GValue           *out)
{
	const GValue *stored = lrg_component_desc_get_value (desc, key);
	GType         t = G_VALUE_TYPE (live);

	if (stored != NULL)
	{
		g_value_init (out, G_VALUE_TYPE (stored));
		if (g_value_transform (live, out))
			return TRUE;
		g_value_unset (out);
	}

	if (t == G_TYPE_BOOLEAN || t == G_TYPE_STRING || t == G_TYPE_DOUBLE || t == G_TYPE_INT64)
	{
		g_value_init (out, t);
		g_value_copy (live, out);
		return TRUE;
	}
	if (t == G_TYPE_FLOAT)
	{
		g_value_init (out, G_TYPE_DOUBLE);
		g_value_set_double (out, (gdouble) g_value_get_float (live));
		return TRUE;
	}
	if (t == G_TYPE_INT || t == G_TYPE_UINT || t == G_TYPE_LONG || t == G_TYPE_ULONG ||
	    t == G_TYPE_UINT64 || t == G_TYPE_CHAR || t == G_TYPE_UCHAR ||
	    G_TYPE_IS_ENUM (t) || G_TYPE_IS_FLAGS (t))
	{
		g_value_init (out, G_TYPE_INT64);
		return g_value_transform (live, out);
	}
	return FALSE;
}

gboolean
lrg_editor_play_session_apply_to_level (LrgEditorPlaySession  *self,
                                        LrgGameObject         *object,
                                        LrgComponent          *component,
                                        const gchar           *property,
                                        GError               **error)
{
	LrgNode          *node;
	LrgRegistry      *registry;
	const gchar      *type_name;
	GParamSpec       *pspec;
	GList            *comps, *l;
	GPtrArray        *descs;
	guint             ordinal = 0, seen = 0, i;
	gint              index = -1;
	LrgComponentDesc *desc = NULL;
	GValue            live = G_VALUE_INIT;
	GValue            value = G_VALUE_INIT;
	Applied          *a;

	g_return_val_if_fail (LRG_IS_EDITOR_PLAY_SESSION (self), FALSE);
	g_return_val_if_fail (LRG_IS_GAME_OBJECT (object), FALSE);
	g_return_val_if_fail (LRG_IS_COMPONENT (component), FALSE);
	g_return_val_if_fail (property != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* --- Which node, which property ---------------------------------- */
	if (self->state == LRG_EDITOR_PLAY_STATE_STOPPED)
	{
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_FAILED, "Not playing");
		return FALSE;
	}
	if (lrg_component_get_owner (component) != object)
	{
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
		                     "The component does not belong to the object");
		return FALSE;
	}
	node = lrg_editor_play_session_get_node_for_object (self, object);
	if (node == NULL)
	{
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
		                     "The object was not made from a node of the level");
		return FALSE;
	}
	pspec = g_object_class_find_property (G_OBJECT_GET_CLASS (component), property);
	if (pspec == NULL || !(pspec->flags & G_PARAM_READABLE))
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "%s has no readable property '%s'",
		             G_OBJECT_TYPE_NAME (component), property);
		return FALSE;
	}

	/* --- Which component description ---------------------------------
	 * Instantiation creates components in description order, skipping
	 * unregistered types, so the n-th live component of a registry type
	 * is the n-th description with that type name. */
	registry = lrg_engine_get_registry (engine (self));
	type_name = (registry != NULL) ? lrg_registry_lookup_name (registry, G_OBJECT_TYPE (component)) : NULL;
	if (type_name == NULL)
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
		             "%s is not a registered component type (scripts and node-made components "
		             "cannot be applied)", G_OBJECT_TYPE_NAME (component));
		return FALSE;
	}
	comps = lrg_game_object_get_components (object);
	for (l = comps; l != NULL && l->data != (gpointer) component; l = l->next)
		if (G_OBJECT_TYPE (l->data) == G_OBJECT_TYPE (component))
			ordinal++;
	g_list_free (comps);

	descs = lrg_node_get_components (node);
	for (i = 0; descs != NULL && i < descs->len; i++)
	{
		LrgComponentDesc *d = g_ptr_array_index (descs, i);

		if (g_strcmp0 (lrg_component_desc_get_type_name (d), type_name) != 0)
			continue;
		if (seen++ == ordinal)
		{
			index = (gint) i;
			desc = d;
			break;
		}
	}
	if (desc == NULL)
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "Node '%s' has no '%s' component #%u",
		             lrg_node_get_name (node), type_name, ordinal);
		return FALSE;
	}

	/* --- The value ---------------------------------------------------- */
	g_value_init (&live, pspec->value_type);
	g_object_get_property (G_OBJECT (component), property, &live);
	if (!desc_value_for (desc, property, &live, &value))
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
		             "Property '%s' of type %s cannot be stored in a level", property,
		             g_type_name (pspec->value_type));
		g_value_unset (&live);
		return FALSE;
	}
	g_value_unset (&live);

	/* One undoable command now (the inspector shows it at once), and
	 * remembered so it survives the restore on Stop. */
	lrg_editor_set_component_value (self->editor, node, (guint) index, property, &value, FALSE);
	a = g_new0 (Applied, 1);
	a->guid = g_strdup (lrg_node_get_guid (node));
	a->index = (guint) index;
	a->key = g_strdup (property);
	g_value_init (&a->value, G_VALUE_TYPE (&value));
	g_value_copy (&value, &a->value);
	g_ptr_array_add (self->applied, a);
	g_value_unset (&value);
	return TRUE;
}
