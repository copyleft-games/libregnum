/* lrg-sequence-player.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgSequencePlayer: playhead, crossing logic (events, sounds) and
 * evaluation of an #LrgSequence into an #LrgWorld3D.
 *
 * Crossing rule: a move from a to b covers (a, b] forwards and [b, a)
 * backwards, so each pass over a point fires it exactly once; right after
 * play/stop/set_time/loop-wrap the start point itself is included.
 */

#include "config.h"

#include "lrg-sequence-player.h"
#include "lrg-bone.h"
#include "lrg-gltf-animation.h"
#include "../world3d/components/lrg-camera-component.h"
#include "../world3d/components/lrg-audio-source-component.h"
#include <math.h>
#include <string.h>

/* Advance never loops more than this many times in one call (a huge
 * delta skips whole loops with fmod instead). */
#define MAX_WRAPS (64)

/* A crossed event or sound start, in parent (top-level) time. */
typedef struct
{
	gfloat            time;
	LrgSequenceTrack *track;     /* borrowed */
	guint             section;
	gfloat            local_end; /* audio: parent-time end, 0 = none */
} Cue;

/* A sound this player started (stopped on stop() or leaving its section). */
typedef struct
{
	GWeakRef  component;
	gfloat    start;
	gfloat    end;               /* parent time; 0 = play to the end */
} ActiveSound;

struct _LrgSequencePlayer
{
	GObject parent_instance;

	LrgSequence         *sequence;
	LrgWorld3D          *world;        /* weak pointer */
	gfloat               time;
	gboolean             playing;
	gfloat               rate;
	LrgSequencePlayMode  play_mode;
	gint                 direction;
	gboolean             fresh;        /* include the start point when crossing */

	GHashTable *overrides;   /* binding -> GWeakRef* (LrgEntity3D) */
	GHashTable *skeletons;   /* binding -> LrgSkeleton (ref) */
	GHashTable *clips;       /* name -> LrgAnimationClip (ref) */
	GHashTable *sequences;   /* name or resolved path -> LrgSequence (ref) or NULL */
	GPtrArray  *sounds;      /* ActiveSound* */
	gchar      *cut_binding; /* camera binding we made active, or NULL */
};

enum
{
	PROP_0,
	PROP_SEQUENCE,
	PROP_WORLD,
	PROP_TIME,
	PROP_PLAYING,
	PROP_RATE,
	PROP_PLAY_MODE,
	PROP_DIRECTION,
	N_PROPS
};

static GParamSpec *properties[N_PROPS];

enum
{
	SIGNAL_EVENT,
	SIGNAL_CAMERA_CUT,
	SIGNAL_AUDIO_CUE,
	SIGNAL_ANIMATION_SAMPLE,
	SIGNAL_FINISHED,
	SIGNAL_LOOPED,
	N_SIGNALS
};

static guint signals[N_SIGNALS];

G_DEFINE_FINAL_TYPE (LrgSequencePlayer, lrg_sequence_player, G_TYPE_OBJECT)

static void
weak_ref_free (gpointer data)
{
	GWeakRef *ref = data;

	g_weak_ref_clear (ref);
	g_free (ref);
}

/* Sub-sequence cache values may be NULL (a path that failed to load). */
static void
unref_nullable (gpointer data)
{
	if (data != NULL)
		g_object_unref (data);
}

static void
active_sound_free (gpointer data)
{
	ActiveSound *s = data;

	g_weak_ref_clear (&s->component);
	g_free (s);
}

/* ==========================================================================
 * GObject
 * ========================================================================== */

static void
lrg_sequence_player_dispose (GObject *object)
{
	LrgSequencePlayer *self = LRG_SEQUENCE_PLAYER (object);

	if (self->world != NULL)
		g_object_remove_weak_pointer (G_OBJECT (self->world), (gpointer *) &self->world);
	self->world = NULL;
	g_clear_object (&self->sequence);
	if (self->sounds != NULL)
		g_ptr_array_set_size (self->sounds, 0);

	G_OBJECT_CLASS (lrg_sequence_player_parent_class)->dispose (object);
}

static void
lrg_sequence_player_finalize (GObject *object)
{
	LrgSequencePlayer *self = LRG_SEQUENCE_PLAYER (object);

	g_clear_pointer (&self->overrides, g_hash_table_unref);
	g_clear_pointer (&self->skeletons, g_hash_table_unref);
	g_clear_pointer (&self->clips, g_hash_table_unref);
	g_clear_pointer (&self->sequences, g_hash_table_unref);
	g_clear_pointer (&self->sounds, g_ptr_array_unref);
	g_clear_pointer (&self->cut_binding, g_free);

	G_OBJECT_CLASS (lrg_sequence_player_parent_class)->finalize (object);
}

static void
lrg_sequence_player_get_property (GObject    *object,
                                  guint       prop_id,
                                  GValue     *value,
                                  GParamSpec *pspec)
{
	LrgSequencePlayer *self = LRG_SEQUENCE_PLAYER (object);

	switch (prop_id)
	{
	case PROP_SEQUENCE:
		g_value_set_object (value, self->sequence);
		break;
	case PROP_WORLD:
		g_value_set_object (value, self->world);
		break;
	case PROP_TIME:
		g_value_set_float (value, self->time);
		break;
	case PROP_PLAYING:
		g_value_set_boolean (value, self->playing);
		break;
	case PROP_RATE:
		g_value_set_float (value, self->rate);
		break;
	case PROP_PLAY_MODE:
		g_value_set_enum (value, (gint) self->play_mode);
		break;
	case PROP_DIRECTION:
		g_value_set_int (value, self->direction);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
lrg_sequence_player_set_property (GObject      *object,
                                  guint         prop_id,
                                  const GValue *value,
                                  GParamSpec   *pspec)
{
	LrgSequencePlayer *self = LRG_SEQUENCE_PLAYER (object);

	switch (prop_id)
	{
	case PROP_SEQUENCE:
		lrg_sequence_player_set_sequence (self, g_value_get_object (value));
		break;
	case PROP_WORLD:
		lrg_sequence_player_set_world (self, g_value_get_object (value));
		break;
	case PROP_TIME:
		lrg_sequence_player_set_time (self, g_value_get_float (value));
		break;
	case PROP_RATE:
		lrg_sequence_player_set_rate (self, g_value_get_float (value));
		break;
	case PROP_PLAY_MODE:
		lrg_sequence_player_set_play_mode (self, (LrgSequencePlayMode) g_value_get_enum (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
lrg_sequence_player_class_init (LrgSequencePlayerClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->dispose = lrg_sequence_player_dispose;
	object_class->finalize = lrg_sequence_player_finalize;
	object_class->get_property = lrg_sequence_player_get_property;
	object_class->set_property = lrg_sequence_player_set_property;

	/**
	 * LrgSequencePlayer:sequence:
	 *
	 * The sequence being played.
	 */
	properties[PROP_SEQUENCE] =
		g_param_spec_object ("sequence", "Sequence", "The sequence being played", LRG_TYPE_SEQUENCE,
		                     G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgSequencePlayer:world:
	 *
	 * The world whose entities the bindings resolve to (held weakly).
	 */
	properties[PROP_WORLD] =
		g_param_spec_object ("world", "World", "The driven world", LRG_TYPE_WORLD3D,
		                     G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgSequencePlayer:time:
	 *
	 * The playhead in seconds. Setting it jumps without events
	 * (lrg_sequence_player_set_time()). Not notified while playing.
	 */
	properties[PROP_TIME] =
		g_param_spec_float ("time", "Time", "The playhead in seconds", 0.0f, G_MAXFLOAT, 0.0f,
		                    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgSequencePlayer:playing:
	 *
	 * Whether the playhead advances.
	 */
	properties[PROP_PLAYING] =
		g_param_spec_boolean ("playing", "Playing", "Whether the playhead advances", FALSE,
		                      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgSequencePlayer:rate:
	 *
	 * Playback rate (1 = real time, negative plays backwards).
	 */
	properties[PROP_RATE] =
		g_param_spec_float ("rate", "Rate", "Playback rate", -G_MAXFLOAT, G_MAXFLOAT, 1.0f,
		                    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgSequencePlayer:play-mode:
	 *
	 * What happens at the ends (taken from the sequence when it is set).
	 */
	properties[PROP_PLAY_MODE] =
		g_param_spec_enum ("play-mode", "Play mode", "What happens at the ends", LRG_TYPE_SEQUENCE_PLAY_MODE,
		                   LRG_SEQUENCE_PLAY_ONCE,
		                   G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgSequencePlayer:direction:
	 *
	 * 1 forwards, -1 backwards (ping-pong flips it at the ends).
	 */
	properties[PROP_DIRECTION] =
		g_param_spec_int ("direction", "Direction", "1 forwards, -1 backwards", -1, 1, 1,
		                  G_PARAM_READABLE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	g_object_class_install_properties (object_class, N_PROPS, properties);

	/**
	 * LrgSequencePlayer::event:
	 * @self: the player
	 * @track: the event track (of the sequence or of a sub-sequence)
	 * @name: the event name
	 * @payload: (nullable): the event payload
	 * @time: the event time in the played (top-level) sequence
	 * @reverse: %TRUE when the playhead crossed it moving backwards
	 *
	 * Emitted for every event the playhead crosses, in crossing order.
	 */
	signals[SIGNAL_EVENT] =
		g_signal_new ("event", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
		              G_TYPE_NONE, 5, LRG_TYPE_SEQUENCE_TRACK, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_FLOAT,
		              G_TYPE_BOOLEAN);

	/**
	 * LrgSequencePlayer::camera-cut:
	 * @self: the player
	 * @camera: (nullable): the entity whose camera became active, or %NULL
	 *   when the player handed the camera back
	 *
	 * Emitted when evaluation switches the world's active camera.
	 */
	signals[SIGNAL_CAMERA_CUT] =
		g_signal_new ("camera-cut", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
		              G_TYPE_NONE, 1, LRG_TYPE_ENTITY3D);

	/**
	 * LrgSequencePlayer::audio-cue:
	 * @self: the player
	 * @track: the audio track
	 * @entity: (nullable): the bound entity
	 * @sound: "bank:sound" or a file path
	 * @volume: the section volume
	 *
	 * Emitted when playback crosses the start of an audio section, before
	 * the sound starts on the entity's #LrgAudioSourceComponent (when it
	 * has one). Games without positional audio can play the sound here.
	 */
	signals[SIGNAL_AUDIO_CUE] =
		g_signal_new ("audio-cue", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
		              G_TYPE_NONE, 4, LRG_TYPE_SEQUENCE_TRACK, LRG_TYPE_ENTITY3D, G_TYPE_STRING, G_TYPE_FLOAT);

	/**
	 * LrgSequencePlayer::animation-sample:
	 * @self: the player
	 * @entity: (nullable): the bound entity
	 * @binding: the track binding
	 * @clip: the clip name
	 * @clip_time: the clip time to show, in seconds
	 * @weight: the blend weight
	 *
	 * Emitted while evaluating each active animation section. Return
	 * %TRUE to take over (for example to drive an animator component);
	 * otherwise the registered clip is sampled onto the binding's skeleton
	 * (lrg_sequence_player_bind_skeleton()).
	 *
	 * Returns: %TRUE when handled
	 */
	signals[SIGNAL_ANIMATION_SAMPLE] =
		g_signal_new ("animation-sample", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST, 0,
		              g_signal_accumulator_true_handled, NULL, NULL, G_TYPE_BOOLEAN, 5, LRG_TYPE_ENTITY3D,
		              G_TYPE_STRING, G_TYPE_STRING, G_TYPE_FLOAT, G_TYPE_FLOAT);

	/**
	 * LrgSequencePlayer::finished:
	 * @self: the player
	 *
	 * Emitted when a once-mode player reaches the end (or the start when
	 * playing backwards) and stops.
	 */
	signals[SIGNAL_FINISHED] =
		g_signal_new ("finished", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
		              G_TYPE_NONE, 0);

	/**
	 * LrgSequencePlayer::looped:
	 * @self: the player
	 *
	 * Emitted when a loop player wraps or a ping-pong player turns.
	 */
	signals[SIGNAL_LOOPED] =
		g_signal_new ("looped", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
		              G_TYPE_NONE, 0);
}

static void
lrg_sequence_player_init (LrgSequencePlayer *self)
{
	self->rate = 1.0f;
	self->direction = 1;
	self->fresh = TRUE;
	self->overrides = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, weak_ref_free);
	self->skeletons = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_object_unref);
	self->clips = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_object_unref);
	self->sequences = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, unref_nullable);
	self->sounds = g_ptr_array_new_with_free_func (active_sound_free);
}

LrgSequencePlayer *
lrg_sequence_player_new (LrgSequence *sequence)
{
	LrgSequencePlayer *self;

	g_return_val_if_fail (sequence == NULL || LRG_IS_SEQUENCE (sequence), NULL);

	self = g_object_new (LRG_TYPE_SEQUENCE_PLAYER, NULL);
	lrg_sequence_player_set_sequence (self, sequence);
	return self;
}

/* ==========================================================================
 * Accessors
 * ========================================================================== */

static gfloat
seq_length (LrgSequencePlayer *self)
{
	return (self->sequence != NULL) ? lrg_sequence_get_length (self->sequence) : 0.0f;
}

static void
set_playing (LrgSequencePlayer *self,
             gboolean           playing)
{
	if (self->playing == playing)
		return;
	self->playing = playing;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_PLAYING]);
}

static void
set_direction (LrgSequencePlayer *self,
               gint               direction)
{
	if (self->direction == direction)
		return;
	self->direction = direction;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_DIRECTION]);
}

LrgSequence *
lrg_sequence_player_get_sequence (LrgSequencePlayer *self)
{
	g_return_val_if_fail (LRG_IS_SEQUENCE_PLAYER (self), NULL);
	return self->sequence;
}

void
lrg_sequence_player_set_sequence (LrgSequencePlayer *self,
                                  LrgSequence       *sequence)
{
	g_return_if_fail (LRG_IS_SEQUENCE_PLAYER (self));
	g_return_if_fail (sequence == NULL || LRG_IS_SEQUENCE (sequence));

	if (self->sequence == sequence)
		return;
	lrg_sequence_player_stop (self);
	g_set_object (&self->sequence, sequence);
	if (sequence != NULL)
		lrg_sequence_player_set_play_mode (self, lrg_sequence_get_play_mode (sequence));
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SEQUENCE]);
}

LrgWorld3D *
lrg_sequence_player_get_world (LrgSequencePlayer *self)
{
	g_return_val_if_fail (LRG_IS_SEQUENCE_PLAYER (self), NULL);
	return self->world;
}

void
lrg_sequence_player_set_world (LrgSequencePlayer *self,
                               LrgWorld3D        *world)
{
	g_return_if_fail (LRG_IS_SEQUENCE_PLAYER (self));
	g_return_if_fail (world == NULL || LRG_IS_WORLD3D (world));

	if (self->world == world)
		return;
	if (self->world != NULL)
		g_object_remove_weak_pointer (G_OBJECT (self->world), (gpointer *) &self->world);
	self->world = world;
	if (world != NULL)
		g_object_add_weak_pointer (G_OBJECT (world), (gpointer *) &self->world);
	/* A camera we forced belongs to the old world. */
	g_clear_pointer (&self->cut_binding, g_free);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_WORLD]);
}

gboolean
lrg_sequence_player_is_playing (LrgSequencePlayer *self)
{
	g_return_val_if_fail (LRG_IS_SEQUENCE_PLAYER (self), FALSE);
	return self->playing;
}

gfloat
lrg_sequence_player_get_time (LrgSequencePlayer *self)
{
	g_return_val_if_fail (LRG_IS_SEQUENCE_PLAYER (self), 0.0f);
	return self->time;
}

gfloat
lrg_sequence_player_get_rate (LrgSequencePlayer *self)
{
	g_return_val_if_fail (LRG_IS_SEQUENCE_PLAYER (self), 1.0f);
	return self->rate;
}

void
lrg_sequence_player_set_rate (LrgSequencePlayer *self,
                              gfloat             rate)
{
	g_return_if_fail (LRG_IS_SEQUENCE_PLAYER (self));
	g_return_if_fail (isfinite (rate));

	if (self->rate == rate)
		return;
	self->rate = rate;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_RATE]);
}

LrgSequencePlayMode
lrg_sequence_player_get_play_mode (LrgSequencePlayer *self)
{
	g_return_val_if_fail (LRG_IS_SEQUENCE_PLAYER (self), LRG_SEQUENCE_PLAY_ONCE);
	return self->play_mode;
}

void
lrg_sequence_player_set_play_mode (LrgSequencePlayer   *self,
                                   LrgSequencePlayMode  mode)
{
	g_return_if_fail (LRG_IS_SEQUENCE_PLAYER (self));
	g_return_if_fail ((guint) mode <= LRG_SEQUENCE_PLAY_PING_PONG);

	if (self->play_mode == mode)
		return;
	self->play_mode = mode;
	if (mode != LRG_SEQUENCE_PLAY_PING_PONG)
		set_direction (self, 1);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_PLAY_MODE]);
}

gint
lrg_sequence_player_get_direction (LrgSequencePlayer *self)
{
	g_return_val_if_fail (LRG_IS_SEQUENCE_PLAYER (self), 1);
	return self->direction;
}

const gchar *
lrg_sequence_player_get_camera_binding (LrgSequencePlayer *self)
{
	g_return_val_if_fail (LRG_IS_SEQUENCE_PLAYER (self), NULL);
	return self->cut_binding;
}

/* ==========================================================================
 * Bindings, skeletons, clips, sub-sequences
 * ========================================================================== */

void
lrg_sequence_player_set_binding_override (LrgSequencePlayer *self,
                                          const gchar       *binding,
                                          LrgEntity3D       *entity)
{
	GWeakRef *ref;

	g_return_if_fail (LRG_IS_SEQUENCE_PLAYER (self));
	g_return_if_fail (binding != NULL);
	g_return_if_fail (entity == NULL || LRG_IS_ENTITY3D (entity));

	if (entity == NULL)
	{
		g_hash_table_remove (self->overrides, binding);
		return;
	}
	ref = g_new0 (GWeakRef, 1);
	g_weak_ref_init (ref, entity);
	g_hash_table_replace (self->overrides, g_strdup (binding), ref);
}

LrgEntity3D *
lrg_sequence_player_resolve_binding (LrgSequencePlayer *self,
                                     const gchar       *binding)
{
	GWeakRef *ref;

	g_return_val_if_fail (LRG_IS_SEQUENCE_PLAYER (self), NULL);

	if (binding == NULL)
		return NULL;
	ref = g_hash_table_lookup (self->overrides, binding);
	if (ref != NULL)
	{
		/* The weak ref keeps no reference: the entity lives in its world
		 * (or with its owner) for at least the current call. */
		LrgEntity3D *e = g_weak_ref_get (ref);

		if (e != NULL)
		{
			g_object_unref (e);
			return e;
		}
	}
	if (self->world == NULL)
		return NULL;
	return lrg_world3d_find_by_guid (self->world, binding);
}

void
lrg_sequence_player_bind_skeleton (LrgSequencePlayer *self,
                                   const gchar       *binding,
                                   LrgSkeleton       *skeleton)
{
	g_return_if_fail (LRG_IS_SEQUENCE_PLAYER (self));
	g_return_if_fail (binding != NULL);
	g_return_if_fail (skeleton == NULL || LRG_IS_SKELETON (skeleton));

	if (skeleton == NULL)
		g_hash_table_remove (self->skeletons, binding);
	else
		g_hash_table_replace (self->skeletons, g_strdup (binding), g_object_ref (skeleton));
}

LrgSkeleton *
lrg_sequence_player_get_skeleton (LrgSequencePlayer *self,
                                  const gchar       *binding)
{
	g_return_val_if_fail (LRG_IS_SEQUENCE_PLAYER (self), NULL);
	return (binding != NULL) ? g_hash_table_lookup (self->skeletons, binding) : NULL;
}

void
lrg_sequence_player_add_clip (LrgSequencePlayer *self,
                              LrgAnimationClip  *clip)
{
	const gchar *name;

	g_return_if_fail (LRG_IS_SEQUENCE_PLAYER (self));
	g_return_if_fail (LRG_IS_ANIMATION_CLIP (clip));

	name = lrg_animation_clip_get_name (clip);
	g_return_if_fail (name != NULL);
	g_hash_table_replace (self->clips, g_strdup (name), g_object_ref (clip));
}

gint
lrg_sequence_player_load_clips (LrgSequencePlayer  *self,
                                const gchar        *path,
                                GError            **error)
{
	g_autoptr(GPtrArray) clips = NULL;
	guint                i;

	g_return_val_if_fail (LRG_IS_SEQUENCE_PLAYER (self), -1);
	g_return_val_if_fail (path != NULL, -1);

	clips = lrg_gltf_load_clips (path, error);
	if (clips == NULL)
		return -1;
	for (i = 0; i < clips->len; i++)
		lrg_sequence_player_add_clip (self, g_ptr_array_index (clips, i));
	return (gint) clips->len;
}

LrgAnimationClip *
lrg_sequence_player_get_clip (LrgSequencePlayer *self,
                              const gchar       *name)
{
	g_return_val_if_fail (LRG_IS_SEQUENCE_PLAYER (self), NULL);
	return (name != NULL) ? g_hash_table_lookup (self->clips, name) : NULL;
}

void
lrg_sequence_player_add_sequence (LrgSequencePlayer *self,
                                  const gchar       *name,
                                  LrgSequence       *sequence)
{
	g_return_if_fail (LRG_IS_SEQUENCE_PLAYER (self));
	g_return_if_fail (name != NULL);
	g_return_if_fail (LRG_IS_SEQUENCE (sequence));

	g_hash_table_replace (self->sequences, g_strdup (name), g_object_ref (sequence));
}

/* Finds a sub-sequence: by registered name, else as a `.rseq` path
 * relative to @parent's base directory (loaded once; failures are
 * remembered as NULL and warned about once). */
static LrgSequence *
resolve_subsequence (LrgSequencePlayer *self,
                     LrgSequence       *parent,
                     const gchar       *name)
{
	g_autofree gchar *path = NULL;
	g_autoptr(GError) error = NULL;
	LrgSequence      *seq;

	if (name == NULL)
		return NULL;
	if (g_hash_table_lookup_extended (self->sequences, name, NULL, (gpointer *) &seq))
		return seq;

	if (g_path_is_absolute (name) || lrg_sequence_get_base_dir (parent) == NULL)
		path = g_strdup (name);
	else
		path = g_build_filename (lrg_sequence_get_base_dir (parent), name, NULL);
	if (g_hash_table_lookup_extended (self->sequences, path, NULL, (gpointer *) &seq))
		return seq;

	seq = lrg_sequence_new_from_file (path, &error);
	if (seq == NULL)
		g_warning ("LrgSequencePlayer: sub-sequence '%s': %s", name, error->message);
	g_hash_table_insert (self->sequences, g_steal_pointer (&path), seq);
	return seq;
}

/* Local time of a clip or sub-sequence section at parent time @t. */
static gfloat
section_local_time (const LrgSequenceSection *s,
                    gfloat                    t,
                    gfloat                    inner_length)
{
	gfloat local = s->offset + (t - s->start) * s->rate;

	if (inner_length > 0.0f)
	{
		if (s->loop)
		{
			local = fmodf (local, inner_length);
			if (local < 0.0f)
				local += inner_length;
		}
		else
			local = CLAMP (local, 0.0f, inner_length);
	}
	return MAX (local, 0.0f);
}

/* ==========================================================================
 * Evaluation
 * ========================================================================== */

typedef struct
{
	GHashTable  *touched;      /* LrgSkeleton* set: recalc world poses */
	const gchar *cut_binding;  /* the camera the cuts ask for (borrowed) */
	gboolean     cut_seen;     /* some camera cut track was evaluated */
} EvalState;

/* Writes the keyed channels of a transform track onto @entity. */
static void
apply_entity_transform (LrgSequenceTrack *track,
                        LrgEntity3D      *entity,
                        gfloat            t)
{
	gfloat   v[LRG_SEQUENCE_N_CHANNELS];
	gboolean group[3] = { FALSE, FALSE, FALSE };
	guint    i;

	lrg_entity3d_get_position (entity, v);
	lrg_entity3d_get_rotation_euler (entity, v + 3);
	lrg_entity3d_get_scale (entity, v + 6);

	for (i = 0; i < LRG_SEQUENCE_N_CHANNELS; i++)
	{
		LrgSequenceCurve *c = lrg_sequence_track_get_channel_curve (track, (LrgSequenceTransformChannel) i);

		if (c == NULL || lrg_sequence_curve_get_n_keys (c) == 0)
			continue;
		v[i] = lrg_sequence_curve_evaluate (c, t);
		group[i / 3] = TRUE;
	}
	if (group[0])
		lrg_entity3d_set_position (entity, v);
	if (group[1])
		lrg_entity3d_set_rotation_euler (entity, v + 3);
	if (group[2])
		lrg_entity3d_set_scale (entity, v + 6);
}

/* Offsets the keyed groups of a bone from its bind pose. */
static void
apply_bone_transform (LrgSequenceTrack *track,
                      LrgSkeleton      *skeleton,
                      gfloat            t,
                      EvalState        *st)
{
	LrgBone           *bone = lrg_skeleton_get_bone_by_name (skeleton, lrg_sequence_track_get_bone (track));
	gfloat             off[LRG_SEQUENCE_N_CHANNELS] = { 0, 0, 0, 0, 0, 0, 1, 1, 1 };
	gboolean           group[3] = { FALSE, FALSE, FALSE };
	LrgBonePose        composed, pose;
	const LrgBonePose *bind, *local;
	guint              i;

	if (bone == NULL)
		return;
	for (i = 0; i < LRG_SEQUENCE_N_CHANNELS; i++)
	{
		LrgSequenceCurve *c = lrg_sequence_track_get_channel_curve (track, (LrgSequenceTransformChannel) i);

		if (c == NULL || lrg_sequence_curve_get_n_keys (c) == 0)
			continue;
		off[i] = lrg_sequence_curve_evaluate (c, t);
		group[i / 3] = TRUE;
	}
	if (!group[0] && !group[1] && !group[2])
		return;

	/* Compose against the bind pose, then keep the unkeyed groups of the
	 * current local pose (so a rotation-only bone track layers over a
	 * clip's translation). */
	bind = lrg_bone_get_bind_pose (bone);
	local = lrg_bone_get_local_pose (bone);
	lrg_sequence_compose_bone_offset (bind, off, &composed);
	pose = *local;
	if (group[0])
	{
		pose.position_x = composed.position_x;
		pose.position_y = composed.position_y;
		pose.position_z = composed.position_z;
	}
	if (group[1])
	{
		pose.rotation_x = composed.rotation_x;
		pose.rotation_y = composed.rotation_y;
		pose.rotation_z = composed.rotation_z;
		pose.rotation_w = composed.rotation_w;
	}
	if (group[2])
	{
		pose.scale_x = composed.scale_x;
		pose.scale_y = composed.scale_y;
		pose.scale_z = composed.scale_z;
	}
	lrg_skeleton_set_pose (skeleton, lrg_bone_get_index (bone), &pose);
	g_hash_table_add (st->touched, skeleton);
}

/* Writes a scalar into a numeric, boolean or enum property. */
static void
apply_property (LrgSequenceTrack *track,
                LrgEntity3D      *entity,
                gfloat            t)
{
	const gchar *type_name = lrg_sequence_track_get_component_type (track);
	const gchar *prop = lrg_sequence_track_get_property_name (track);
	GObject     *target = G_OBJECT (entity);
	GParamSpec  *pspec;
	GValue       value = G_VALUE_INIT;
	gfloat       v;
	GType        ptype;

	if (prop == NULL || lrg_sequence_curve_get_n_keys (lrg_sequence_track_get_curve (track)) == 0)
		return;
	if (type_name != NULL)
	{
		GType type = g_type_from_name (type_name);

		if (type == G_TYPE_INVALID || !g_type_is_a (type, LRG_TYPE_COMPONENT))
			return;
		target = (GObject *) lrg_entity3d_get_component (entity, type);
		if (target == NULL)
			return;
	}
	pspec = g_object_class_find_property (G_OBJECT_GET_CLASS (target), prop);
	if (pspec == NULL || !(pspec->flags & G_PARAM_WRITABLE) || (pspec->flags & G_PARAM_CONSTRUCT_ONLY))
		return;

	v = lrg_sequence_curve_evaluate (lrg_sequence_track_get_curve (track), t);
	ptype = G_PARAM_SPEC_VALUE_TYPE (pspec);
	g_value_init (&value, ptype);
	switch (G_TYPE_FUNDAMENTAL (ptype))
	{
	case G_TYPE_BOOLEAN:
		g_value_set_boolean (&value, v >= 0.5f);
		break;
	case G_TYPE_INT:
		g_value_set_int (&value, (gint) lroundf (v));
		break;
	case G_TYPE_UINT:
		g_value_set_uint (&value, (guint) MAX (lroundf (v), 0));
		break;
	case G_TYPE_LONG:
		g_value_set_long (&value, (glong) lroundf (v));
		break;
	case G_TYPE_ULONG:
		g_value_set_ulong (&value, (gulong) MAX (lroundf (v), 0));
		break;
	case G_TYPE_INT64:
		g_value_set_int64 (&value, (gint64) llroundf (v));
		break;
	case G_TYPE_UINT64:
		g_value_set_uint64 (&value, (guint64) MAX (llroundf (v), 0));
		break;
	case G_TYPE_FLOAT:
		g_value_set_float (&value, v);
		break;
	case G_TYPE_DOUBLE:
		g_value_set_double (&value, (gdouble) v);
		break;
	case G_TYPE_ENUM:
		g_value_set_enum (&value, (gint) lroundf (v));
		break;
	default:
		g_value_unset (&value);
		return;
	}
	/* Out-of-range values are clamped by the pspec, not rejected. */
	g_param_value_validate (pspec, &value);
	g_object_set_property (target, prop, &value);
	g_value_unset (&value);
}

/* Samples an animation section onto the binding's skeleton (unless an
 * animation-sample handler takes it). */
static void
apply_animation (LrgSequencePlayer *self,
                 LrgSequenceTrack  *track,
                 gfloat             t,
                 gfloat             length,
                 EvalState         *st)
{
	const gchar              *binding = lrg_sequence_track_get_binding (track);
	gint                      idx = lrg_sequence_track_find_section_at (track, t, length);
	const LrgSequenceSection *s;
	LrgAnimationClip         *clip;
	LrgSkeleton              *skeleton;
	gfloat                    clip_time;
	gboolean                  handled = FALSE;
	guint                     j, n;

	if (idx < 0)
		return;
	s = lrg_sequence_track_get_section (track, (guint) idx);
	clip = lrg_sequence_player_get_clip (self, s->name);
	clip_time = section_local_time (s, t, clip != NULL ? lrg_animation_clip_get_duration (clip) : 0.0f);

	g_signal_emit (self, signals[SIGNAL_ANIMATION_SAMPLE], 0, lrg_sequence_player_resolve_binding (self, binding),
	               binding != NULL ? binding : "", s->name, clip_time, s->weight, &handled);
	if (handled)
		return;

	skeleton = lrg_sequence_player_get_skeleton (self, binding);
	if (skeleton == NULL || clip == NULL)
		return;

	/* Default path: every clip track onto the bone with the same name. */
	n = lrg_animation_clip_get_track_count (clip);
	for (j = 0; j < n; j++)
	{
		LrgBone    *bone = lrg_skeleton_get_bone_by_name (skeleton, lrg_animation_clip_get_track_bone_name (clip, j));
		LrgBonePose pose;

		if (bone == NULL)
			continue;
		memset (&pose, 0, sizeof pose);
		lrg_animation_clip_sample_track (clip, j, clip_time, &pose);
		if (s->weight >= 1.0f)
			lrg_skeleton_set_pose (skeleton, lrg_bone_get_index (bone), &pose);
		else if (s->weight > 0.0f)
			lrg_skeleton_blend_pose (skeleton, lrg_bone_get_index (bone), &pose, s->weight);
	}
	g_hash_table_add (st->touched, skeleton);
}

/*
 * Evaluates @seq at local time @t. Pass 0 does everything except bone
 * tracks; pass 1 does bone tracks, so they layer over clip poses.
 */
static void
eval_sequence (LrgSequencePlayer *self,
               LrgSequence       *seq,
               gfloat             t,
               guint              depth,
               guint              pass,
               EvalState         *st)
{
	gfloat length = lrg_sequence_get_length (seq);
	guint  i;

	if (depth >= LRG_SEQUENCE_MAX_DEPTH)
		return;

	for (i = 0; i < lrg_sequence_get_n_tracks (seq); i++)
	{
		LrgSequenceTrack *track = lrg_sequence_get_track (seq, i);
		const gchar      *binding = lrg_sequence_track_get_binding (track);
		LrgEntity3D      *entity;
		gint              idx;

		if (lrg_sequence_track_get_muted (track))
			continue;

		switch (lrg_sequence_track_get_kind (track))
		{
		case LRG_SEQUENCE_TRACK_TRANSFORM:
			if (lrg_sequence_track_get_bone (track) != NULL)
			{
				LrgSkeleton *skeleton = lrg_sequence_player_get_skeleton (self, binding);

				if (pass == 1 && skeleton != NULL)
					apply_bone_transform (track, skeleton, t, st);
			}
			else if (pass == 0 && (entity = lrg_sequence_player_resolve_binding (self, binding)) != NULL)
				apply_entity_transform (track, entity, t);
			break;
		case LRG_SEQUENCE_TRACK_PROPERTY:
			if (pass == 0 && (entity = lrg_sequence_player_resolve_binding (self, binding)) != NULL)
				apply_property (track, entity, t);
			break;
		case LRG_SEQUENCE_TRACK_CAMERA_CUT:
			if (pass == 0)
			{
				idx = lrg_sequence_track_find_section_at (track, t, length);
				st->cut_seen = TRUE;
				st->cut_binding = (idx >= 0) ? lrg_sequence_track_get_section (track, (guint) idx)->name : NULL;
			}
			break;
		case LRG_SEQUENCE_TRACK_ANIMATION:
			if (pass == 0)
				apply_animation (self, track, t, length, st);
			break;
		case LRG_SEQUENCE_TRACK_SUBSEQUENCE:
			idx = lrg_sequence_track_find_section_at (track, t, length);
			if (idx >= 0)
			{
				const LrgSequenceSection *s = lrg_sequence_track_get_section (track, (guint) idx);
				LrgSequence              *sub = resolve_subsequence (self, seq, s->name);

				if (sub != NULL && sub != seq)
					eval_sequence (self, sub, section_local_time (s, t, lrg_sequence_get_length (sub)),
					               depth + 1, pass, st);
			}
			break;
		case LRG_SEQUENCE_TRACK_AUDIO:
		case LRG_SEQUENCE_TRACK_EVENT:
		default:
			break;
		}
	}
}

/* Switches (or releases) the world's active camera after evaluation. */
static void
apply_camera (LrgSequencePlayer *self,
              EvalState         *st)
{
	LrgEntity3D        *entity = NULL;
	LrgCameraComponent *cc = NULL;

	if (!st->cut_seen || self->world == NULL)
		return;
	if (g_strcmp0 (st->cut_binding, self->cut_binding) == 0)
		return;

	if (st->cut_binding != NULL)
	{
		entity = lrg_sequence_player_resolve_binding (self, st->cut_binding);
		if (entity != NULL)
			cc = (LrgCameraComponent *) lrg_entity3d_get_component (entity, LRG_TYPE_CAMERA_COMPONENT);
	}
	if (cc != NULL)
	{
		lrg_world3d_set_active_camera (self->world, LRG_CAMERA (lrg_camera_component_get_camera (cc)));
		g_free (self->cut_binding);
		self->cut_binding = g_strdup (st->cut_binding);
		g_signal_emit (self, signals[SIGNAL_CAMERA_CUT], 0, entity);
	}
	else if (self->cut_binding != NULL)
	{
		/* Before the first cut (or an unresolved camera): hand back. */
		lrg_world3d_set_active_camera (self->world, NULL);
		g_clear_pointer (&self->cut_binding, g_free);
		g_signal_emit (self, signals[SIGNAL_CAMERA_CUT], 0, NULL);
	}
}

/* Stops sounds whose section the playhead has left. */
static void
update_sounds (LrgSequencePlayer *self)
{
	guint i = self->sounds->len;

	while (i-- > 0)
	{
		ActiveSound             *s = g_ptr_array_index (self->sounds, i);
		LrgAudioSourceComponent *comp;

		if (s->end <= 0.0f || (self->time >= s->start && self->time < s->end))
			continue;
		comp = g_weak_ref_get (&s->component);
		if (comp != NULL)
		{
			lrg_audio_source_component_stop (comp);
			g_object_unref (comp);
		}
		g_ptr_array_remove_index (self->sounds, i);
	}
}

void
lrg_sequence_player_evaluate (LrgSequencePlayer *self)
{
	EvalState      st;
	GHashTableIter iter;
	gpointer       skeleton;

	g_return_if_fail (LRG_IS_SEQUENCE_PLAYER (self));

	if (self->sequence == NULL)
		return;

	memset (&st, 0, sizeof st);
	st.touched = g_hash_table_new (g_direct_hash, g_direct_equal);
	eval_sequence (self, self->sequence, self->time, 0, 0, &st);
	eval_sequence (self, self->sequence, self->time, 0, 1, &st);

	/* Posed skeletons get their world (model-space) poses once. */
	g_hash_table_iter_init (&iter, st.touched);
	while (g_hash_table_iter_next (&iter, &skeleton, NULL))
		lrg_skeleton_calculate_world_poses (LRG_SKELETON (skeleton));
	g_hash_table_unref (st.touched);

	apply_camera (self, &st);
	update_sounds (self);
}

/* ==========================================================================
 * Crossing: events and sound starts between two times
 * ========================================================================== */

static gboolean
in_interval (gfloat   x,
             gfloat   a,
             gfloat   b,
             gboolean inclusive_start)
{
	if (b >= a)
		return (x > a || (inclusive_start && x == a)) && x <= b;
	return (x < a || (inclusive_start && x == a)) && x >= b;
}

/*
 * Collects the event (and, with @want_audio, audio) cues of @seq crossed
 * moving from @a to @b in local time. @scale / @shift map local time back
 * to top-level time (parent = local * scale + shift).
 */
static void
collect_cues (LrgSequencePlayer *self,
              LrgSequence       *seq,
              gfloat             a,
              gfloat             b,
              gboolean           inclusive_start,
              gboolean           want_audio,
              gfloat             scale,
              gfloat             shift,
              guint              depth,
              GArray            *out)
{
	gfloat length = lrg_sequence_get_length (seq);
	guint  i, j;

	if (depth >= LRG_SEQUENCE_MAX_DEPTH)
		return;

	for (i = 0; i < lrg_sequence_get_n_tracks (seq); i++)
	{
		LrgSequenceTrack    *track = lrg_sequence_get_track (seq, i);
		LrgSequenceTrackKind kind = lrg_sequence_track_get_kind (track);

		if (lrg_sequence_track_get_muted (track))
			continue;

		if (kind == LRG_SEQUENCE_TRACK_EVENT || (want_audio && kind == LRG_SEQUENCE_TRACK_AUDIO))
		{
			for (j = 0; j < lrg_sequence_track_get_n_sections (track); j++)
			{
				const LrgSequenceSection *s = lrg_sequence_track_get_section (track, j);
				Cue                       cue;

				if (s->start > length || !in_interval (s->start, a, b, inclusive_start))
					continue;
				cue.time = s->start * scale + shift;
				cue.track = track;
				cue.section = j;
				cue.local_end = (s->duration > 0.0f) ? (s->start + s->duration) * scale + shift : 0.0f;
				g_array_append_val (out, cue);
			}
		}
		else if (kind == LRG_SEQUENCE_TRACK_SUBSEQUENCE)
		{
			/* Recurse over the overlap of the crossed range with each
			 * section, mapped into the sub-sequence's time (no wrap:
			 * events of a looping sub-sequence fire on its first pass). */
			for (j = 0; j < lrg_sequence_track_get_n_sections (track); j++)
			{
				const LrgSequenceSection *s = lrg_sequence_track_get_section (track, j);
				gfloat                    end = lrg_sequence_track_get_section_end (track, j, length);
				gfloat                    lo = MIN (a, b), hi = MAX (a, b);
				gfloat                    ca, cb;
				LrgSequence              *sub;

				if (s->rate <= 0.0f || end < lo || s->start > hi)
					continue;
				sub = resolve_subsequence (self, seq, s->name);
				if (sub == NULL || sub == seq)
					continue;
				ca = CLAMP (a, s->start, end);
				cb = CLAMP (b, s->start, end);
				collect_cues (self, sub,
				              s->offset + (ca - s->start) * s->rate,
				              s->offset + (cb - s->start) * s->rate,
				              inclusive_start || ca != a, want_audio,
				              scale / s->rate, shift + (s->start - s->offset / s->rate) * scale,
				              depth + 1, out);
			}
		}
	}
}

static gint
cue_cmp_forward (gconstpointer a,
                 gconstpointer b)
{
	gfloat ta = ((const Cue *) a)->time, tb = ((const Cue *) b)->time;

	return (ta < tb) ? -1 : (ta > tb) ? 1 : 0;
}

static gint
cue_cmp_backward (gconstpointer a,
                  gconstpointer b)
{
	return cue_cmp_forward (b, a);
}

/* Starts the sound of an audio cue on its entity's audio source. */
static void
start_sound (LrgSequencePlayer *self,
             const Cue         *cue)
{
	const LrgSequenceSection *s = lrg_sequence_track_get_section (cue->track, cue->section);
	LrgEntity3D              *entity = lrg_sequence_player_resolve_binding (self,
	                                                                        lrg_sequence_track_get_binding (cue->track));
	LrgAudioSourceComponent  *comp = NULL;
	const gchar              *colon;
	ActiveSound              *active;

	g_signal_emit (self, signals[SIGNAL_AUDIO_CUE], 0, cue->track, entity, s->name, s->weight);
	if (entity != NULL)
		comp = (LrgAudioSourceComponent *) lrg_entity3d_get_component (entity, LRG_TYPE_AUDIO_SOURCE_COMPONENT);
	if (comp == NULL)
		return;

	/* "bank:sound" names a sound bank entry; anything else is a path. */
	colon = strchr (s->name, ':');
	if (colon != NULL && colon != s->name)
	{
		g_autofree gchar *bank = g_strndup (s->name, (gsize) (colon - s->name));

		lrg_audio_source_component_set_sound (comp, bank, colon + 1);
	}
	else
		lrg_audio_source_component_set_sound_path (comp, s->name);
	lrg_audio_source_component_play (comp);

	active = g_new0 (ActiveSound, 1);
	g_weak_ref_init (&active->component, comp);
	active->start = cue->time;
	active->end = cue->local_end;
	g_ptr_array_add (self->sounds, active);
}

/* Emits everything crossed from @a to @b (top-level time). */
static void
cross (LrgSequencePlayer *self,
       gfloat             a,
       gfloat             b,
       gboolean           want_audio)
{
	g_autoptr(GArray) cues = NULL;
	gboolean          reverse = b < a;
	guint             i;

	if (a == b && !self->fresh)
		return;
	cues = g_array_new (FALSE, FALSE, sizeof (Cue));
	collect_cues (self, self->sequence, a, b, self->fresh, want_audio, 1.0f, 0.0f, 0, cues);
	self->fresh = FALSE;
	g_array_sort (cues, reverse ? cue_cmp_backward : cue_cmp_forward);

	for (i = 0; i < cues->len; i++)
	{
		const Cue                *cue = &g_array_index (cues, Cue, i);
		const LrgSequenceSection *s = lrg_sequence_track_get_section (cue->track, cue->section);

		if (lrg_sequence_track_get_kind (cue->track) == LRG_SEQUENCE_TRACK_EVENT)
			g_signal_emit (self, signals[SIGNAL_EVENT], 0, cue->track, s->name, s->payload, cue->time, reverse);
		else if (!reverse)
			start_sound (self, cue);
	}
}

/* ==========================================================================
 * Transport
 * ========================================================================== */

static void
set_time_internal (LrgSequencePlayer *self,
                   gfloat             time)
{
	self->time = time;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TIME]);
}

static void
stop_sounds (LrgSequencePlayer *self)
{
	guint i;

	for (i = 0; i < self->sounds->len; i++)
	{
		ActiveSound             *s = g_ptr_array_index (self->sounds, i);
		LrgAudioSourceComponent *comp = g_weak_ref_get (&s->component);

		if (comp != NULL)
		{
			lrg_audio_source_component_stop (comp);
			g_object_unref (comp);
		}
	}
	g_ptr_array_set_size (self->sounds, 0);
}

void
lrg_sequence_player_play (LrgSequencePlayer *self)
{
	gfloat length;

	g_return_if_fail (LRG_IS_SEQUENCE_PLAYER (self));

	if (self->sequence == NULL || self->playing)
		return;

	/* A finished once-mode player starts over. */
	length = seq_length (self);
	if (self->play_mode == LRG_SEQUENCE_PLAY_ONCE)
	{
		if (self->rate >= 0.0f && self->time >= length)
		{
			set_time_internal (self, 0.0f);
			self->fresh = TRUE;
		}
		else if (self->rate < 0.0f && self->time <= 0.0f)
		{
			set_time_internal (self, length);
			self->fresh = TRUE;
		}
	}
	set_playing (self, TRUE);
	lrg_sequence_player_evaluate (self);
}

void
lrg_sequence_player_pause (LrgSequencePlayer *self)
{
	g_return_if_fail (LRG_IS_SEQUENCE_PLAYER (self));
	set_playing (self, FALSE);
}

void
lrg_sequence_player_stop (LrgSequencePlayer *self)
{
	g_return_if_fail (LRG_IS_SEQUENCE_PLAYER (self));

	set_playing (self, FALSE);
	stop_sounds (self);
	if (self->cut_binding != NULL)
	{
		if (self->world != NULL)
			lrg_world3d_set_active_camera (self->world, NULL);
		g_clear_pointer (&self->cut_binding, g_free);
		g_signal_emit (self, signals[SIGNAL_CAMERA_CUT], 0, NULL);
	}
	set_direction (self, 1);
	set_time_internal (self, 0.0f);
	self->fresh = TRUE;
}

void
lrg_sequence_player_set_time (LrgSequencePlayer *self,
                              gfloat             time)
{
	g_return_if_fail (LRG_IS_SEQUENCE_PLAYER (self));

	if (!isfinite (time))
		time = 0.0f;
	set_time_internal (self, CLAMP (time, 0.0f, seq_length (self)));
	self->fresh = TRUE;
	lrg_sequence_player_evaluate (self);
}

void
lrg_sequence_player_scrub (LrgSequencePlayer *self,
                           gfloat             time)
{
	g_return_if_fail (LRG_IS_SEQUENCE_PLAYER (self));

	if (self->sequence == NULL)
		return;
	if (!isfinite (time))
		time = 0.0f;
	time = CLAMP (time, 0.0f, seq_length (self));
	cross (self, self->time, time, FALSE);
	set_time_internal (self, time);
	lrg_sequence_player_evaluate (self);
}

void
lrg_sequence_player_advance (LrgSequencePlayer *self,
                             gfloat             delta)
{
	gfloat length, remaining, t;
	gint   dir;
	guint  wraps = 0;

	g_return_if_fail (LRG_IS_SEQUENCE_PLAYER (self));

	if (!self->playing || self->sequence == NULL || !isfinite (delta) || delta <= 0.0f || self->rate == 0.0f)
		return;

	length = seq_length (self);
	remaining = delta * fabsf (self->rate);
	t = self->time;
	dir = self->direction * ((self->rate < 0.0f) ? -1 : 1);

	/* Walk the playhead, splitting the step at every end it reaches. */
	while (remaining > 0.0f && self->playing)
	{
		gfloat target = t + (gfloat) dir * remaining;
		gfloat edge = (dir > 0) ? length : 0.0f;

		if ((dir > 0 && target < length) || (dir < 0 && target > 0.0f))
		{
			cross (self, t, target, TRUE);
			t = target;
			break;
		}

		/* Reach the end, then apply the play mode. */
		cross (self, t, edge, TRUE);
		remaining -= fabsf (edge - t);
		t = edge;

		switch (self->play_mode)
		{
		case LRG_SEQUENCE_PLAY_LOOP:
			t = (dir > 0) ? 0.0f : length;
			self->fresh = TRUE;
			g_signal_emit (self, signals[SIGNAL_LOOPED], 0);
			if (length > 0.0f && (remaining > length || ++wraps >= MAX_WRAPS))
				remaining = fmodf (remaining, length);
			break;
		case LRG_SEQUENCE_PLAY_PING_PONG:
			set_direction (self, -self->direction);
			dir = -dir;
			g_signal_emit (self, signals[SIGNAL_LOOPED], 0);
			if (length > 0.0f && (remaining > 2.0f * length || ++wraps >= MAX_WRAPS))
				remaining = fmodf (remaining, 2.0f * length);
			if (length <= 0.0f)
				remaining = 0.0f;
			break;
		case LRG_SEQUENCE_PLAY_ONCE:
		default:
			remaining = 0.0f;
			self->time = t;   /* no notify per frame; "playing" notifies */
			set_playing (self, FALSE);
			lrg_sequence_player_evaluate (self);
			g_signal_emit (self, signals[SIGNAL_FINISHED], 0);
			return;
		}
	}

	/* Not notified while playing (it would fire every frame). */
	self->time = t;
	lrg_sequence_player_evaluate (self);
}
