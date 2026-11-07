/* lrg-animation-state-machine.c
 *
 * Copyright 2025-2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * The animation state machine: states (clips, blend trees, nested
 * machines), typed parameters shared between machines, transitions with
 * conditions, exit times, durations, offsets and interruption, any-state
 * transitions, animation events and root motion.
 *
 * One update:
 *   1. advance the current state (and the next one during a transition)
 *      by delta x speed x speed parameter, after feeding blend trees their
 *      parameters;
 *   2. fire the clip events the update crossed;
 *   3. during a transition: advance its progress, let an allowed
 *      transition interrupt it (the blend then restarts from the last
 *      sampled pose), finish it at progress 1;
 *      otherwise: fire the first transition (any-state first, then the
 *      current state's, each by priority) whose exit time was crossed and
 *      whose conditions hold, consuming its triggers;
 *   4. with a skeleton set (the pre-0.9 standalone use) sample the pose
 *      and write it to the skeleton.
 */

#include "config.h"

#include <math.h>
#include <string.h>

#include "lrg-animation-state-machine.h"
#include "lrg-blend-tree.h"
#include "lrg-anim-graph.h"

/**
 * SECTION:lrg-animation-state-machine
 * @Title: LrgAnimationStateMachine
 * @Short_description: Animation state machine controller
 *
 * #LrgAnimationStateMachine manages animation states and transitions
 * between them based on parameters and conditions. It samples animations
 * and applies them to a skeleton.
 */

/* ==========================================================================
 * Parameter table (shared between the layers of a graph and sub-machines)
 * ========================================================================== */

typedef struct
{
	LrgAnimParameterType  type;
	GVariant             *default_value;
} ParamDecl;

typedef struct
{
	gint        ref_count;
	GHashTable *values;  /* name -> GVariant */
	GHashTable *decls;   /* name -> ParamDecl */
	GPtrArray  *order;   /* declared names, owned */
} ParamTable;

static void
param_decl_free (gpointer data)
{
	ParamDecl *d = data;

	g_variant_unref (d->default_value);
	g_free (d);
}

static ParamTable *
param_table_new (void)
{
	ParamTable *t = g_new0 (ParamTable, 1);

	t->ref_count = 1;
	t->values = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, (GDestroyNotify) g_variant_unref);
	t->decls = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, param_decl_free);
	t->order = g_ptr_array_new_with_free_func (g_free);
	return t;
}

static ParamTable *
param_table_ref (ParamTable *t)
{
	t->ref_count++;
	return t;
}

static void
param_table_unref (ParamTable *t)
{
	if (--t->ref_count > 0)
		return;
	g_hash_table_unref (t->values);
	g_hash_table_unref (t->decls);
	g_ptr_array_unref (t->order);
	g_free (t);
}

/* The default value of a type when none is given. */
static GVariant *
type_default (LrgAnimParameterType type)
{
	switch (type)
	{
	case LRG_ANIM_PARAMETER_INT:
		return g_variant_new_int32 (0);
	case LRG_ANIM_PARAMETER_BOOL:
	case LRG_ANIM_PARAMETER_TRIGGER:
		return g_variant_new_boolean (FALSE);
	case LRG_ANIM_PARAMETER_FLOAT:
	default:
		return g_variant_new_double (0.0);
	}
}

GType
lrg_anim_parameter_type_get_type (void)
{
	static gsize type_id = 0;

	if (g_once_init_enter (&type_id))
	{
		static const GEnumValue values[] = {
			{ LRG_ANIM_PARAMETER_FLOAT, "LRG_ANIM_PARAMETER_FLOAT", "float" },
			{ LRG_ANIM_PARAMETER_INT, "LRG_ANIM_PARAMETER_INT", "int" },
			{ LRG_ANIM_PARAMETER_BOOL, "LRG_ANIM_PARAMETER_BOOL", "bool" },
			{ LRG_ANIM_PARAMETER_TRIGGER, "LRG_ANIM_PARAMETER_TRIGGER", "trigger" },
			{ 0, NULL, NULL }
		};
		GType type = g_enum_register_static ("LrgAnimParameterType", values);

		g_once_init_leave (&type_id, type);
	}
	return (GType) type_id;
}

/* ==========================================================================
 * Instance
 * ========================================================================== */

typedef struct
{
	LrgSkeleton            *skeleton;
	GHashTable             *states;        /* name -> LrgAnimationState (ref) */
	GPtrArray              *state_order;   /* LrgAnimationState (borrowed) */
	GList                  *transitions;   /* LrgAnimationTransition (ref) */
	GList                  *sorted;        /* by priority, borrowed */
	gboolean                sorted_dirty;
	ParamTable             *params;

	gchar                  *default_state;
	LrgAnimationState      *current_state;
	LrgAnimationState      *next_state;
	LrgAnimationTransition *active;        /* ref */

	gboolean                running;
	gboolean                transitioning;
	gboolean                from_snapshot;
	gfloat                  transition_progress;
	gfloat                  transition_duration;

	GArray                 *snapshot;      /* LrgBonePose */
	GArray                 *last_pose;     /* LrgBonePose */
	GArray                 *scratch;       /* LrgBonePose */
	GHashTable             *forwarded;     /* sub-machines whose events we re-emit */
} LrgAnimationStateMachinePrivate;

G_DEFINE_TYPE_WITH_PRIVATE (LrgAnimationStateMachine, lrg_animation_state_machine, G_TYPE_OBJECT)

enum
{
	PROP_0,
	PROP_SKELETON,
	PROP_RUNNING,
	PROP_DEFAULT_STATE,
	N_PROPS
};

enum
{
	SIGNAL_STATE_ENTERED,
	SIGNAL_STATE_EXITED,
	SIGNAL_EVENT,
	SIGNAL_TRANSITION_STARTED,
	N_SIGNALS
};

static GParamSpec *properties[N_PROPS];
static guint       signals[N_SIGNALS];

/* ==========================================================================
 * Helpers
 * ========================================================================== */

/* Highest priority first; g_list_sort is stable, so equal priorities keep
 * the order they were added in. */
static gint
by_priority (gconstpointer a,
             gconstpointer b)
{
	gint pa = lrg_animation_transition_get_priority ((LrgAnimationTransition *) a);
	gint pb = lrg_animation_transition_get_priority ((LrgAnimationTransition *) b);

	return (pa < pb) - (pa > pb);
}

static GList *
sorted_transitions (LrgAnimationStateMachinePrivate *priv)
{
	if (priv->sorted_dirty)
	{
		g_list_free (priv->sorted);
		priv->sorted = g_list_sort (g_list_copy (priv->transitions), by_priority);
		priv->sorted_dirty = FALSE;
	}
	return priv->sorted;
}

static void
on_priority_changed (GObject    *object,
                     GParamSpec *pspec,
                     gpointer    user_data)
{
	LrgAnimationStateMachinePrivate *priv =
		lrg_animation_state_machine_get_instance_private (LRG_ANIMATION_STATE_MACHINE (user_data));

	priv->sorted_dirty = TRUE;
}

/* Re-emits a sub-machine's events on the parent. */
static void
on_sub_event (LrgAnimationStateMachine *sub,
              const gchar              *state_name,
              LrgAnimationEvent        *event,
              gpointer                  user_data)
{
	LrgAnimationStateMachine *self = LRG_ANIMATION_STATE_MACHINE (user_data);

	g_signal_emit (self, signals[SIGNAL_EVENT],
	               event->name != NULL ? g_quark_from_string (event->name) : 0,
	               state_name, event);
}

/* Makes a state's sub-machine share our parameters and forward events. */
static void
adopt_sub_machine (LrgAnimationStateMachine *self,
                   LrgAnimationState        *state)
{
	LrgAnimationStateMachinePrivate *priv = lrg_animation_state_machine_get_instance_private (self);
	LrgAnimationStateMachine        *sub = lrg_animation_state_get_sub_machine (state);
	LrgAnimationStateMachinePrivate *spriv;

	if (sub == NULL || sub == self)
		return;
	spriv = lrg_animation_state_machine_get_instance_private (sub);
	if (spriv->params != priv->params)
		lrg_animation_state_machine_share_parameters (sub, self);
	if (!g_hash_table_contains (priv->forwarded, sub))
	{
		g_hash_table_add (priv->forwarded, sub);
		g_signal_connect_object (sub, "event", G_CALLBACK (on_sub_event), self, 0);
	}
}

/* A numeric parameter as a float (doubles, ints and booleans). */
static gfloat
param_number (LrgAnimationStateMachinePrivate *priv,
              const gchar                     *name)
{
	GVariant *v = g_hash_table_lookup (priv->params->values, name);

	if (v == NULL)
		return 0.0f;
	if (g_variant_is_of_type (v, G_VARIANT_TYPE_DOUBLE))
		return (gfloat) g_variant_get_double (v);
	if (g_variant_is_of_type (v, G_VARIANT_TYPE_INT32))
		return (gfloat) g_variant_get_int32 (v);
	if (g_variant_is_of_type (v, G_VARIANT_TYPE_BOOLEAN))
		return g_variant_get_boolean (v) ? 1.0f : 0.0f;
	return 0.0f;
}

/* Advances one state: parameters into its blend tree, speed parameter,
 * nested machine adoption. */
static void
step_state (LrgAnimationStateMachine *self,
            LrgAnimationState        *state,
            gfloat                    delta_time)
{
	LrgAnimationStateMachinePrivate *priv = lrg_animation_state_machine_get_instance_private (self);
	LrgBlendTree                    *tree = lrg_animation_state_get_blend_tree (state);
	const gchar                     *speed_param = lrg_animation_state_get_speed_parameter (state);
	gfloat                           mult = 1.0f;

	if (tree != NULL)
		lrg_blend_tree_pull_parameters (tree, self);
	if (speed_param != NULL)
		mult = param_number (priv, speed_param);
	adopt_sub_machine (self, state);
	lrg_animation_state_update (state, delta_time * mult);
}

/* Emits the clip events of @state's last update. Sub-machines emit their
 * own (forwarded); blend trees fire their dominant clip's. */
static void
fire_events (LrgAnimationStateMachine *self,
             LrgAnimationState        *state)
{
	LrgAnimationClip    *clip = NULL;
	LrgBlendTree        *tree;
	gfloat               t0, t1;
	g_autoptr(GPtrArray) events = NULL;
	guint                i;

	if (lrg_animation_state_get_sub_machine (state) != NULL)
		return;
	tree = lrg_animation_state_get_blend_tree (state);
	if (tree != NULL)
	{
		clip = lrg_blend_tree_get_dominant_clip (tree, &t0, &t1);
	}
	else
	{
		clip = lrg_animation_state_get_clip (state);
		t0 = lrg_animation_state_get_previous_time (state);
		t1 = lrg_animation_state_get_time (state);
	}
	if (clip == NULL || lrg_animation_clip_get_event_count (clip) == 0)
		return;

	events = lrg_anim_pose_clip_events (clip, t0, t1);
	for (i = 0; i < events->len; i++)
	{
		LrgAnimationEvent *ev = g_ptr_array_index (events, i);

		g_signal_emit (self, signals[SIGNAL_EVENT],
		               ev->name != NULL ? g_quark_from_string (ev->name) : 0,
		               lrg_animation_state_get_name (state), ev);
	}
}

/* Resets the triggers a firing transition's conditions use. */
static void
consume_triggers (LrgAnimationStateMachinePrivate *priv,
                  LrgAnimationTransition          *trans)
{
	GList *l;

	for (l = lrg_animation_transition_get_conditions (trans); l != NULL; l = l->next)
	{
		LrgTransitionCondition *c = l->data;
		ParamDecl              *d;

		if (c->parameter == NULL)
			continue;
		d = g_hash_table_lookup (priv->params->decls, c->parameter);
		if (d != NULL && d->type == LRG_ANIM_PARAMETER_TRIGGER)
			g_hash_table_insert (priv->params->values, g_strdup (c->parameter),
			                     g_variant_ref_sink (g_variant_new_boolean (FALSE)));
	}
}

/* The first transition out of @from (or any-state when @from is NULL)
 * that fires, given the source's normalized time interval. */
static LrgAnimationTransition *
find_transition (LrgAnimationStateMachine *self,
                 LrgAnimationState        *from,
                 gboolean                  any_state,
                 gfloat                    prev_norm,
                 gfloat                    norm)
{
	LrgAnimationStateMachinePrivate *priv = lrg_animation_state_machine_get_instance_private (self);
	const gchar                     *from_name = from != NULL ? lrg_animation_state_get_name (from) : NULL;
	GList                           *l;

	for (l = sorted_transitions (priv); l != NULL; l = l->next)
	{
		LrgAnimationTransition *t = l->data;
		LrgAnimationState      *target;

		if (any_state)
		{
			if (!lrg_animation_transition_is_any_state (t))
				continue;
		}
		else if (g_strcmp0 (lrg_animation_transition_get_source (t), from_name) != 0)
		{
			continue;
		}
		target = g_hash_table_lookup (priv->states, lrg_animation_transition_get_target (t));
		if (target == NULL)
			continue;
		/* an any-state transition does not restart its own target */
		if (any_state && target == priv->current_state &&
		    !lrg_animation_transition_get_can_transition_to_self (t))
			continue;
		/* nor re-target the transition already in progress */
		if (priv->transitioning && target == priv->next_state)
			continue;
		if (lrg_animation_transition_evaluate_range (t, priv->params->values, prev_norm, norm))
			return t;
	}
	return NULL;
}

/* Ends the transition: the next state becomes current. */
static void
finish_transition (LrgAnimationStateMachine *self)
{
	LrgAnimationStateMachinePrivate *priv = lrg_animation_state_machine_get_instance_private (self);
	LrgAnimationState               *old = priv->current_state;
	LrgAnimationState               *new_state = priv->next_state;

	if (old != NULL && old != new_state)
	{
		lrg_animation_state_exit (old);
		g_signal_emit (self, signals[SIGNAL_STATE_EXITED], 0, lrg_animation_state_get_name (old));
	}
	priv->current_state = new_state;
	priv->next_state = NULL;
	priv->transitioning = FALSE;
	priv->from_snapshot = FALSE;
	priv->transition_progress = 0.0f;
	g_clear_object (&priv->active);
	if (new_state != NULL)
		g_signal_emit (self, signals[SIGNAL_STATE_ENTERED], 0, lrg_animation_state_get_name (new_state));
}

/* Starts @trans from the current state. */
static void
begin_transition (LrgAnimationStateMachine *self,
                  LrgAnimationTransition   *trans)
{
	LrgAnimationStateMachinePrivate *priv = lrg_animation_state_machine_get_instance_private (self);
	LrgAnimationState               *target;
	gfloat                           offset;

	target = g_hash_table_lookup (priv->states, lrg_animation_transition_get_target (trans));
	if (target == NULL)
		return;

	consume_triggers (priv, trans);
	g_signal_emit (self, signals[SIGNAL_TRANSITION_STARTED], 0,
	               priv->current_state != NULL ? lrg_animation_state_get_name (priv->current_state) : NULL,
	               lrg_animation_state_get_name (target));

	priv->next_state = target;
	lrg_animation_state_enter (target);
	offset = lrg_animation_transition_get_offset (trans);
	if (offset > 0.0f)
		lrg_animation_state_set_time (target, offset * lrg_animation_state_get_duration (target));

	g_set_object (&priv->active, trans);
	priv->transitioning = TRUE;
	priv->transition_progress = 0.0f;
	priv->transition_duration = lrg_animation_transition_get_duration (trans);
	if (priv->transition_duration <= 0.0f)
		finish_transition (self);
}

/* Lets an allowed transition cut into the one in progress. Returns TRUE
 * when it did. */
static gboolean
try_interrupt (LrgAnimationStateMachine *self,
               gfloat                    cur_prev, gfloat cur_now,
               gfloat                    next_prev, gfloat next_now)
{
	LrgAnimationStateMachinePrivate *priv = lrg_animation_state_machine_get_instance_private (self);
	LrgTransitionInterruption        mode;
	LrgAnimationTransition          *cand = NULL;
	gboolean                         from_destination = FALSE;
	gint                             pass;

	if (priv->active == NULL)
		return FALSE;
	mode = lrg_animation_transition_get_interruption (priv->active);
	if (mode == LRG_TRANSITION_INTERRUPTION_NONE)
		return FALSE;

	/* up to two lists, in the order the mode asks for */
	for (pass = 0; pass < 2 && cand == NULL; pass++)
	{
		gboolean dest;

		switch (mode)
		{
		case LRG_TRANSITION_INTERRUPTION_SOURCE:
			if (pass > 0)
				continue;
			dest = FALSE;
			break;
		case LRG_TRANSITION_INTERRUPTION_DESTINATION:
			if (pass > 0)
				continue;
			dest = TRUE;
			break;
		case LRG_TRANSITION_INTERRUPTION_SOURCE_THEN_DESTINATION:
			dest = pass == 1;
			break;
		case LRG_TRANSITION_INTERRUPTION_DESTINATION_THEN_SOURCE:
		default:
			dest = pass == 0;
			break;
		}
		if (dest)
			cand = find_transition (self, priv->next_state, FALSE, next_prev, next_now);
		else
			cand = find_transition (self, priv->current_state, FALSE, cur_prev, cur_now);
		from_destination = dest;
	}
	if (cand == NULL)
		return FALSE;

	/* freeze the blended pose; the new blend starts from it */
	if (priv->last_pose->len > 0)
	{
		g_array_set_size (priv->snapshot, priv->last_pose->len);
		memcpy (priv->snapshot->data, priv->last_pose->data, priv->last_pose->len * sizeof (LrgBonePose));
		priv->from_snapshot = TRUE;
	}

	/* the state the new transition leaves becomes current; the other exits */
	if (from_destination)
	{
		LrgAnimationState *old = priv->current_state;

		if (old != NULL && old != priv->next_state)
		{
			lrg_animation_state_exit (old);
			g_signal_emit (self, signals[SIGNAL_STATE_EXITED], 0, lrg_animation_state_get_name (old));
		}
		priv->current_state = priv->next_state;
	}
	else if (priv->next_state != NULL && priv->next_state != priv->current_state)
	{
		lrg_animation_state_exit (priv->next_state);
	}
	priv->next_state = NULL;
	priv->transitioning = FALSE;
	begin_transition (self, cand);
	return TRUE;
}

/* ==========================================================================
 * Update
 * ========================================================================== */

static void
lrg_animation_state_machine_real_update (LrgAnimationStateMachine *self,
                                         gfloat                    delta_time)
{
	LrgAnimationStateMachinePrivate *priv = lrg_animation_state_machine_get_instance_private (self);
	LrgAnimationState               *cur;
	gfloat                           cur_prev, cur_now;
	gfloat                           next_prev = 0.0f, next_now = 0.0f;

	if (!priv->running || priv->current_state == NULL)
		return;

	/* 1. advance the playing states */
	cur = priv->current_state;
	cur_prev = lrg_animation_state_get_normalized_time (cur);
	step_state (self, cur, delta_time);
	cur_now = lrg_animation_state_get_normalized_time (cur);
	if (priv->transitioning && priv->next_state != NULL && priv->next_state != cur)
	{
		next_prev = lrg_animation_state_get_normalized_time (priv->next_state);
		step_state (self, priv->next_state, delta_time);
		next_now = lrg_animation_state_get_normalized_time (priv->next_state);
	}

	/* 2. events of the interval just played */
	if (!priv->from_snapshot)
		fire_events (self, cur);
	if (priv->transitioning && priv->next_state != NULL && priv->next_state != cur)
		fire_events (self, priv->next_state);

	/* 3. transitions */
	if (priv->transitioning)
	{
		priv->transition_progress += delta_time / MAX (priv->transition_duration, 1e-4f);
		if (!try_interrupt (self, cur_prev, cur_now, next_prev, next_now) &&
		    priv->transition_progress >= 1.0f)
			finish_transition (self);
	}
	else
	{
		LrgAnimationTransition *t;

		t = find_transition (self, cur, TRUE, cur_prev, cur_now);
		if (t == NULL)
			t = find_transition (self, cur, FALSE, cur_prev, cur_now);
		if (t != NULL)
			begin_transition (self, t);
	}

	/* 4. standalone use: pose the skeleton */
	if (priv->skeleton != NULL)
	{
		guint n = lrg_skeleton_get_bone_count (priv->skeleton);

		g_array_set_size (priv->scratch, n);
		lrg_animation_state_machine_sample_pose (self, priv->skeleton,
		                                         (LrgBonePose *) (gpointer) priv->scratch->data, n);
		lrg_anim_pose_apply (priv->skeleton, (LrgBonePose *) (gpointer) priv->scratch->data, n);
	}
}

/* ==========================================================================
 * GObject
 * ========================================================================== */

static void
lrg_animation_state_machine_dispose (GObject *object)
{
	LrgAnimationStateMachine        *self = LRG_ANIMATION_STATE_MACHINE (object);
	LrgAnimationStateMachinePrivate *priv = lrg_animation_state_machine_get_instance_private (self);

	g_clear_object (&priv->skeleton);
	g_clear_object (&priv->active);
	priv->current_state = NULL;
	priv->next_state = NULL;
	g_hash_table_remove_all (priv->forwarded);
	g_hash_table_remove_all (priv->states);
	g_ptr_array_set_size (priv->state_order, 0);

	G_OBJECT_CLASS (lrg_animation_state_machine_parent_class)->dispose (object);
}

static void
lrg_animation_state_machine_finalize (GObject *object)
{
	LrgAnimationStateMachine        *self = LRG_ANIMATION_STATE_MACHINE (object);
	LrgAnimationStateMachinePrivate *priv = lrg_animation_state_machine_get_instance_private (self);
	GList                           *l;

	for (l = priv->transitions; l != NULL; l = l->next)
		g_signal_handlers_disconnect_by_data (l->data, self);
	g_list_free (priv->sorted);
	g_list_free_full (priv->transitions, g_object_unref);
	g_hash_table_unref (priv->states);
	g_ptr_array_unref (priv->state_order);
	param_table_unref (priv->params);
	g_free (priv->default_state);
	g_array_unref (priv->snapshot);
	g_array_unref (priv->last_pose);
	g_array_unref (priv->scratch);
	g_hash_table_unref (priv->forwarded);

	G_OBJECT_CLASS (lrg_animation_state_machine_parent_class)->finalize (object);
}

static void
lrg_animation_state_machine_get_property (GObject    *object,
                                          guint       prop_id,
                                          GValue     *value,
                                          GParamSpec *pspec)
{
	LrgAnimationStateMachine        *self = LRG_ANIMATION_STATE_MACHINE (object);
	LrgAnimationStateMachinePrivate *priv = lrg_animation_state_machine_get_instance_private (self);

	switch (prop_id)
	{
	case PROP_SKELETON:
		g_value_set_object (value, priv->skeleton);
		break;
	case PROP_RUNNING:
		g_value_set_boolean (value, priv->running);
		break;
	case PROP_DEFAULT_STATE:
		g_value_set_string (value, priv->default_state);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_animation_state_machine_set_property (GObject      *object,
                                          guint         prop_id,
                                          const GValue *value,
                                          GParamSpec   *pspec)
{
	LrgAnimationStateMachine *self = LRG_ANIMATION_STATE_MACHINE (object);

	switch (prop_id)
	{
	case PROP_SKELETON:
		lrg_animation_state_machine_set_skeleton (self, g_value_get_object (value));
		break;
	case PROP_DEFAULT_STATE:
		lrg_animation_state_machine_set_default_state (self, g_value_get_string (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_animation_state_machine_class_init (LrgAnimationStateMachineClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->dispose = lrg_animation_state_machine_dispose;
	object_class->finalize = lrg_animation_state_machine_finalize;
	object_class->get_property = lrg_animation_state_machine_get_property;
	object_class->set_property = lrg_animation_state_machine_set_property;

	klass->update = lrg_animation_state_machine_real_update;

	/**
	 * LrgAnimationStateMachine:skeleton:
	 *
	 * The skeleton each update poses (optional: an #LrgAnimGraph samples
	 * the machine itself).
	 */
	properties[PROP_SKELETON] =
		g_param_spec_object ("skeleton", "Skeleton", "Target skeleton", LRG_TYPE_SKELETON,
		                     G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgAnimationStateMachine:running:
	 *
	 * Whether the machine has been started.
	 */
	properties[PROP_RUNNING] =
		g_param_spec_boolean ("running", "Running", "Whether running", FALSE,
		                      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

	/**
	 * LrgAnimationStateMachine:default-state:
	 *
	 * The state the machine starts in.
	 */
	properties[PROP_DEFAULT_STATE] =
		g_param_spec_string ("default-state", "Default State", "Start state", NULL,
		                     G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	g_object_class_install_properties (object_class, N_PROPS, properties);

	/**
	 * LrgAnimationStateMachine::state-entered:
	 * @self: The state machine
	 * @state_name: Name of entered state
	 *
	 * Emitted when a state becomes the current state (at the end of the
	 * transition into it, or on start/force).
	 */
	signals[SIGNAL_STATE_ENTERED] =
		g_signal_new ("state-entered", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
		              G_STRUCT_OFFSET (LrgAnimationStateMachineClass, state_entered),
		              NULL, NULL, NULL, G_TYPE_NONE, 1, G_TYPE_STRING);

	/**
	 * LrgAnimationStateMachine::state-exited:
	 * @self: The state machine
	 * @state_name: Name of exited state
	 *
	 * Emitted when a state stops playing.
	 */
	signals[SIGNAL_STATE_EXITED] =
		g_signal_new ("state-exited", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
		              G_STRUCT_OFFSET (LrgAnimationStateMachineClass, state_exited),
		              NULL, NULL, NULL, G_TYPE_NONE, 1, G_TYPE_STRING);

	/**
	 * LrgAnimationStateMachine::event:
	 * @self: The state machine
	 * @state_name: the state whose clip carries the event
	 * @event: the animation event (owned by the clip)
	 *
	 * Emitted for every clip event an update plays past (see
	 * lrg_anim_pose_clip_events()), including those of the state being
	 * blended in and of nested machines. The detail is the event name, so
	 * `"event::footstep"` connects to footsteps only.
	 */
	signals[SIGNAL_EVENT] =
		g_signal_new ("event", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
		              0, NULL, NULL, NULL, G_TYPE_NONE, 2,
		              G_TYPE_STRING, LRG_TYPE_ANIMATION_EVENT | G_SIGNAL_TYPE_STATIC_SCOPE);

	/**
	 * LrgAnimationStateMachine::transition-started:
	 * @self: The state machine
	 * @source: (nullable): the state being left
	 * @target: the state being blended in
	 *
	 * Emitted when a transition fires (the editor's live graph highlights
	 * it).
	 */
	signals[SIGNAL_TRANSITION_STARTED] =
		g_signal_new ("transition-started", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
		              0, NULL, NULL, NULL, G_TYPE_NONE, 2, G_TYPE_STRING, G_TYPE_STRING);
}

static void
lrg_animation_state_machine_init (LrgAnimationStateMachine *self)
{
	LrgAnimationStateMachinePrivate *priv = lrg_animation_state_machine_get_instance_private (self);

	priv->states = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_object_unref);
	priv->state_order = g_ptr_array_new ();
	priv->params = param_table_new ();
	priv->snapshot = g_array_new (FALSE, TRUE, sizeof (LrgBonePose));
	priv->last_pose = g_array_new (FALSE, TRUE, sizeof (LrgBonePose));
	priv->scratch = g_array_new (FALSE, TRUE, sizeof (LrgBonePose));
	priv->forwarded = g_hash_table_new (g_direct_hash, g_direct_equal);
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

LrgAnimationStateMachine *
lrg_animation_state_machine_new (void)
{
	return g_object_new (LRG_TYPE_ANIMATION_STATE_MACHINE, NULL);
}

LrgSkeleton *
lrg_animation_state_machine_get_skeleton (LrgAnimationStateMachine *self)
{
	LrgAnimationStateMachinePrivate *priv;

	g_return_val_if_fail (LRG_IS_ANIMATION_STATE_MACHINE (self), NULL);
	priv = lrg_animation_state_machine_get_instance_private (self);
	return priv->skeleton;
}

void
lrg_animation_state_machine_set_skeleton (LrgAnimationStateMachine *self,
                                          LrgSkeleton              *skeleton)
{
	LrgAnimationStateMachinePrivate *priv;

	g_return_if_fail (LRG_IS_ANIMATION_STATE_MACHINE (self));
	priv = lrg_animation_state_machine_get_instance_private (self);
	if (g_set_object (&priv->skeleton, skeleton))
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SKELETON]);
}

void
lrg_animation_state_machine_add_state (LrgAnimationStateMachine *self,
                                       LrgAnimationState        *state)
{
	LrgAnimationStateMachinePrivate *priv;
	LrgAnimationState               *old;
	const gchar                     *name;

	g_return_if_fail (LRG_IS_ANIMATION_STATE_MACHINE (self));
	g_return_if_fail (LRG_IS_ANIMATION_STATE (state));

	priv = lrg_animation_state_machine_get_instance_private (self);
	name = lrg_animation_state_get_name (state);
	g_return_if_fail (name != NULL);

	/* a state of the same name is replaced in place (order kept) */
	old = g_hash_table_lookup (priv->states, name);
	if (old != NULL)
	{
		guint idx;

		if (g_ptr_array_find (priv->state_order, old, &idx))
			priv->state_order->pdata[idx] = state;
		if (priv->current_state == old)
			priv->current_state = state;
		if (priv->next_state == old)
			priv->next_state = state;
	}
	else
	{
		g_ptr_array_add (priv->state_order, state);
	}
	g_hash_table_insert (priv->states, g_strdup (name), g_object_ref (state));
	adopt_sub_machine (self, state);
}

void
lrg_animation_state_machine_remove_state (LrgAnimationStateMachine *self,
                                          const gchar              *name)
{
	LrgAnimationStateMachinePrivate *priv;
	LrgAnimationState               *state;

	g_return_if_fail (LRG_IS_ANIMATION_STATE_MACHINE (self));
	g_return_if_fail (name != NULL);
	priv = lrg_animation_state_machine_get_instance_private (self);

	state = g_hash_table_lookup (priv->states, name);
	if (state == NULL)
		return;
	if (priv->current_state == state)
		priv->current_state = NULL;
	if (priv->next_state == state)
	{
		priv->next_state = NULL;
		priv->transitioning = FALSE;
	}
	g_ptr_array_remove (priv->state_order, state);
	g_hash_table_remove (priv->states, name);
}

LrgAnimationState *
lrg_animation_state_machine_get_state (LrgAnimationStateMachine *self,
                                       const gchar              *name)
{
	LrgAnimationStateMachinePrivate *priv;

	g_return_val_if_fail (LRG_IS_ANIMATION_STATE_MACHINE (self), NULL);
	g_return_val_if_fail (name != NULL, NULL);
	priv = lrg_animation_state_machine_get_instance_private (self);
	return g_hash_table_lookup (priv->states, name);
}

GList *
lrg_animation_state_machine_get_states (LrgAnimationStateMachine *self)
{
	LrgAnimationStateMachinePrivate *priv;

	g_return_val_if_fail (LRG_IS_ANIMATION_STATE_MACHINE (self), NULL);
	priv = lrg_animation_state_machine_get_instance_private (self);
	return g_hash_table_get_values (priv->states);
}

GList *
lrg_animation_state_machine_get_states_ordered (LrgAnimationStateMachine *self)
{
	LrgAnimationStateMachinePrivate *priv;
	GList                           *list = NULL;
	guint                            i;

	g_return_val_if_fail (LRG_IS_ANIMATION_STATE_MACHINE (self), NULL);
	priv = lrg_animation_state_machine_get_instance_private (self);
	for (i = priv->state_order->len; i > 0; i--)
		list = g_list_prepend (list, g_ptr_array_index (priv->state_order, i - 1));
	return list;
}

void
lrg_animation_state_machine_add_transition (LrgAnimationStateMachine *self,
                                            LrgAnimationTransition   *transition)
{
	LrgAnimationStateMachinePrivate *priv;

	g_return_if_fail (LRG_IS_ANIMATION_STATE_MACHINE (self));
	g_return_if_fail (LRG_IS_ANIMATION_TRANSITION (transition));
	priv = lrg_animation_state_machine_get_instance_private (self);

	priv->transitions = g_list_append (priv->transitions, g_object_ref (transition));
	priv->sorted_dirty = TRUE;
	g_signal_connect (transition, "notify::priority", G_CALLBACK (on_priority_changed), self);
}

gboolean
lrg_animation_state_machine_remove_transition (LrgAnimationStateMachine *self,
                                               LrgAnimationTransition   *transition)
{
	LrgAnimationStateMachinePrivate *priv;
	GList                           *link;

	g_return_val_if_fail (LRG_IS_ANIMATION_STATE_MACHINE (self), FALSE);
	priv = lrg_animation_state_machine_get_instance_private (self);

	link = g_list_find (priv->transitions, transition);
	if (link == NULL)
		return FALSE;
	if (priv->active == transition)
		g_clear_object (&priv->active);
	g_signal_handlers_disconnect_by_data (transition, self);
	priv->transitions = g_list_delete_link (priv->transitions, link);
	g_object_unref (transition);
	priv->sorted_dirty = TRUE;
	return TRUE;
}

GList *
lrg_animation_state_machine_get_transitions (LrgAnimationStateMachine *self)
{
	LrgAnimationStateMachinePrivate *priv;

	g_return_val_if_fail (LRG_IS_ANIMATION_STATE_MACHINE (self), NULL);
	priv = lrg_animation_state_machine_get_instance_private (self);
	return g_list_copy (priv->transitions);
}

void
lrg_animation_state_machine_set_default_state (LrgAnimationStateMachine *self,
                                               const gchar              *name)
{
	LrgAnimationStateMachinePrivate *priv;

	g_return_if_fail (LRG_IS_ANIMATION_STATE_MACHINE (self));
	priv = lrg_animation_state_machine_get_instance_private (self);
	if (g_strcmp0 (priv->default_state, name) == 0)
		return;
	g_free (priv->default_state);
	priv->default_state = g_strdup (name);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_DEFAULT_STATE]);
}

const gchar *
lrg_animation_state_machine_get_default_state (LrgAnimationStateMachine *self)
{
	LrgAnimationStateMachinePrivate *priv;

	g_return_val_if_fail (LRG_IS_ANIMATION_STATE_MACHINE (self), NULL);
	priv = lrg_animation_state_machine_get_instance_private (self);
	return priv->default_state;
}

LrgAnimationState *
lrg_animation_state_machine_get_current_state (LrgAnimationStateMachine *self)
{
	LrgAnimationStateMachinePrivate *priv;

	g_return_val_if_fail (LRG_IS_ANIMATION_STATE_MACHINE (self), NULL);
	priv = lrg_animation_state_machine_get_instance_private (self);
	return priv->current_state;
}

const gchar *
lrg_animation_state_machine_get_current_state_name (LrgAnimationStateMachine *self)
{
	LrgAnimationState *s = lrg_animation_state_machine_get_current_state (self);

	return s != NULL ? lrg_animation_state_get_name (s) : NULL;
}

LrgAnimationState *
lrg_animation_state_machine_get_next_state (LrgAnimationStateMachine *self)
{
	LrgAnimationStateMachinePrivate *priv;

	g_return_val_if_fail (LRG_IS_ANIMATION_STATE_MACHINE (self), NULL);
	priv = lrg_animation_state_machine_get_instance_private (self);
	return priv->transitioning ? priv->next_state : NULL;
}

gfloat
lrg_animation_state_machine_get_transition_progress (LrgAnimationStateMachine *self)
{
	LrgAnimationStateMachinePrivate *priv;

	g_return_val_if_fail (LRG_IS_ANIMATION_STATE_MACHINE (self), 0.0f);
	priv = lrg_animation_state_machine_get_instance_private (self);
	return priv->transitioning ? CLAMP (priv->transition_progress, 0.0f, 1.0f) : 0.0f;
}

LrgAnimationTransition *
lrg_animation_state_machine_get_active_transition (LrgAnimationStateMachine *self)
{
	LrgAnimationStateMachinePrivate *priv;

	g_return_val_if_fail (LRG_IS_ANIMATION_STATE_MACHINE (self), NULL);
	priv = lrg_animation_state_machine_get_instance_private (self);
	return priv->transitioning ? priv->active : NULL;
}

/* --- parameters --------------------------------------------------------- */

void
lrg_animation_state_machine_set_parameter (LrgAnimationStateMachine *self,
                                           const gchar              *name,
                                           GVariant                 *value)
{
	LrgAnimationStateMachinePrivate *priv;

	g_return_if_fail (LRG_IS_ANIMATION_STATE_MACHINE (self));
	g_return_if_fail (name != NULL);
	priv = lrg_animation_state_machine_get_instance_private (self);

	if (value != NULL)
		g_hash_table_insert (priv->params->values, g_strdup (name), g_variant_ref_sink (value));
	else
		g_hash_table_remove (priv->params->values, name);
}

GVariant *
lrg_animation_state_machine_get_parameter (LrgAnimationStateMachine *self,
                                           const gchar              *name)
{
	LrgAnimationStateMachinePrivate *priv;

	g_return_val_if_fail (LRG_IS_ANIMATION_STATE_MACHINE (self), NULL);
	g_return_val_if_fail (name != NULL, NULL);
	priv = lrg_animation_state_machine_get_instance_private (self);
	return g_hash_table_lookup (priv->params->values, name);
}

void
lrg_animation_state_machine_set_float (LrgAnimationStateMachine *self,
                                       const gchar              *name,
                                       gfloat                    value)
{
	lrg_animation_state_machine_set_parameter (self, name, g_variant_new_double ((gdouble) value));
}

gfloat
lrg_animation_state_machine_get_float (LrgAnimationStateMachine *self,
                                       const gchar              *name)
{
	g_return_val_if_fail (LRG_IS_ANIMATION_STATE_MACHINE (self), 0.0f);
	g_return_val_if_fail (name != NULL, 0.0f);
	return param_number (lrg_animation_state_machine_get_instance_private (self), name);
}

void
lrg_animation_state_machine_set_int (LrgAnimationStateMachine *self,
                                     const gchar              *name,
                                     gint                      value)
{
	lrg_animation_state_machine_set_parameter (self, name, g_variant_new_int32 (value));
}

gint
lrg_animation_state_machine_get_int (LrgAnimationStateMachine *self,
                                     const gchar              *name)
{
	g_return_val_if_fail (LRG_IS_ANIMATION_STATE_MACHINE (self), 0);
	g_return_val_if_fail (name != NULL, 0);
	return (gint) param_number (lrg_animation_state_machine_get_instance_private (self), name);
}

void
lrg_animation_state_machine_set_bool (LrgAnimationStateMachine *self,
                                      const gchar              *name,
                                      gboolean                  value)
{
	lrg_animation_state_machine_set_parameter (self, name, g_variant_new_boolean (value));
}

gboolean
lrg_animation_state_machine_get_bool (LrgAnimationStateMachine *self,
                                      const gchar              *name)
{
	GVariant *v = lrg_animation_state_machine_get_parameter (self, name);

	if (v == NULL || !g_variant_is_of_type (v, G_VARIANT_TYPE_BOOLEAN))
		return FALSE;
	return g_variant_get_boolean (v);
}

void
lrg_animation_state_machine_set_trigger (LrgAnimationStateMachine *self,
                                         const gchar              *name)
{
	lrg_animation_state_machine_set_bool (self, name, TRUE);
}

void
lrg_animation_state_machine_reset_trigger (LrgAnimationStateMachine *self,
                                           const gchar              *name)
{
	lrg_animation_state_machine_set_bool (self, name, FALSE);
}

void
lrg_animation_state_machine_add_parameter (LrgAnimationStateMachine *self,
                                           const gchar              *name,
                                           LrgAnimParameterType      type,
                                           GVariant                 *default_value)
{
	LrgAnimationStateMachinePrivate *priv;
	ParamDecl                       *d;

	g_return_if_fail (LRG_IS_ANIMATION_STATE_MACHINE (self));
	g_return_if_fail (name != NULL);
	priv = lrg_animation_state_machine_get_instance_private (self);

	d = g_new0 (ParamDecl, 1);
	d->type = type;
	d->default_value = g_variant_ref_sink (default_value != NULL ? default_value : type_default (type));
	if (!g_hash_table_contains (priv->params->decls, name))
		g_ptr_array_add (priv->params->order, g_strdup (name));
	g_hash_table_insert (priv->params->decls, g_strdup (name), d);
	g_hash_table_insert (priv->params->values, g_strdup (name), g_variant_ref (d->default_value));
}

gboolean
lrg_animation_state_machine_remove_parameter (LrgAnimationStateMachine *self,
                                              const gchar              *name)
{
	LrgAnimationStateMachinePrivate *priv;
	guint                            i;

	g_return_val_if_fail (LRG_IS_ANIMATION_STATE_MACHINE (self), FALSE);
	g_return_val_if_fail (name != NULL, FALSE);
	priv = lrg_animation_state_machine_get_instance_private (self);

	if (!g_hash_table_remove (priv->params->decls, name))
		return FALSE;
	g_hash_table_remove (priv->params->values, name);
	for (i = 0; i < priv->params->order->len; i++)
		if (g_strcmp0 (g_ptr_array_index (priv->params->order, i), name) == 0)
		{
			g_ptr_array_remove_index (priv->params->order, i);
			break;
		}
	return TRUE;
}

gboolean
lrg_animation_state_machine_has_parameter (LrgAnimationStateMachine *self,
                                           const gchar              *name)
{
	LrgAnimationStateMachinePrivate *priv;

	g_return_val_if_fail (LRG_IS_ANIMATION_STATE_MACHINE (self), FALSE);
	g_return_val_if_fail (name != NULL, FALSE);
	priv = lrg_animation_state_machine_get_instance_private (self);
	return g_hash_table_contains (priv->params->decls, name);
}

LrgAnimParameterType
lrg_animation_state_machine_get_parameter_type (LrgAnimationStateMachine *self,
                                                const gchar              *name)
{
	LrgAnimationStateMachinePrivate *priv;
	ParamDecl                       *d;

	g_return_val_if_fail (LRG_IS_ANIMATION_STATE_MACHINE (self), LRG_ANIM_PARAMETER_FLOAT);
	g_return_val_if_fail (name != NULL, LRG_ANIM_PARAMETER_FLOAT);
	priv = lrg_animation_state_machine_get_instance_private (self);
	d = g_hash_table_lookup (priv->params->decls, name);
	return d != NULL ? d->type : LRG_ANIM_PARAMETER_FLOAT;
}

gchar **
lrg_animation_state_machine_get_parameter_names (LrgAnimationStateMachine *self)
{
	LrgAnimationStateMachinePrivate *priv;
	GStrvBuilder                    *b;
	gchar                          **out;
	guint                            i;

	g_return_val_if_fail (LRG_IS_ANIMATION_STATE_MACHINE (self), NULL);
	priv = lrg_animation_state_machine_get_instance_private (self);

	b = g_strv_builder_new ();
	for (i = 0; i < priv->params->order->len; i++)
		g_strv_builder_add (b, g_ptr_array_index (priv->params->order, i));
	out = g_strv_builder_end (b);
	g_strv_builder_unref (b);
	return out;
}

void
lrg_animation_state_machine_reset_parameters (LrgAnimationStateMachine *self)
{
	LrgAnimationStateMachinePrivate *priv;
	GHashTableIter                   it;
	gpointer                         key, value;

	g_return_if_fail (LRG_IS_ANIMATION_STATE_MACHINE (self));
	priv = lrg_animation_state_machine_get_instance_private (self);

	g_hash_table_iter_init (&it, priv->params->decls);
	while (g_hash_table_iter_next (&it, &key, &value))
		g_hash_table_insert (priv->params->values, g_strdup (key),
		                     g_variant_ref (((ParamDecl *) value)->default_value));
}

void
lrg_animation_state_machine_share_parameters (LrgAnimationStateMachine *self,
                                              LrgAnimationStateMachine *owner)
{
	LrgAnimationStateMachinePrivate *priv;
	ParamTable                      *table;

	g_return_if_fail (LRG_IS_ANIMATION_STATE_MACHINE (self));
	g_return_if_fail (owner == NULL || LRG_IS_ANIMATION_STATE_MACHINE (owner));
	priv = lrg_animation_state_machine_get_instance_private (self);

	if (owner != NULL)
		table = param_table_ref (((LrgAnimationStateMachinePrivate *)
		                          lrg_animation_state_machine_get_instance_private (owner))->params);
	else
		table = param_table_new ();
	param_table_unref (priv->params);
	priv->params = table;
}

/* --- running ------------------------------------------------------------ */

void
lrg_animation_state_machine_start (LrgAnimationStateMachine *self)
{
	LrgAnimationStateMachinePrivate *priv;

	g_return_if_fail (LRG_IS_ANIMATION_STATE_MACHINE (self));
	priv = lrg_animation_state_machine_get_instance_private (self);

	priv->transitioning = FALSE;
	priv->from_snapshot = FALSE;
	priv->next_state = NULL;
	g_clear_object (&priv->active);
	g_array_set_size (priv->last_pose, 0);
	if (priv->default_state != NULL)
	{
		priv->current_state = g_hash_table_lookup (priv->states, priv->default_state);
		if (priv->current_state != NULL)
		{
			adopt_sub_machine (self, priv->current_state);
			lrg_animation_state_enter (priv->current_state);
			g_signal_emit (self, signals[SIGNAL_STATE_ENTERED], 0,
			               lrg_animation_state_get_name (priv->current_state));
		}
	}
	priv->running = TRUE;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_RUNNING]);
}

void
lrg_animation_state_machine_stop (LrgAnimationStateMachine *self)
{
	LrgAnimationStateMachinePrivate *priv;

	g_return_if_fail (LRG_IS_ANIMATION_STATE_MACHINE (self));
	priv = lrg_animation_state_machine_get_instance_private (self);

	if (priv->next_state != NULL && priv->next_state != priv->current_state)
		lrg_animation_state_exit (priv->next_state);
	if (priv->current_state != NULL)
	{
		lrg_animation_state_exit (priv->current_state);
		g_signal_emit (self, signals[SIGNAL_STATE_EXITED], 0,
		               lrg_animation_state_get_name (priv->current_state));
	}
	priv->running = FALSE;
	priv->current_state = NULL;
	priv->next_state = NULL;
	priv->transitioning = FALSE;
	priv->from_snapshot = FALSE;
	g_clear_object (&priv->active);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_RUNNING]);
}

void
lrg_animation_state_machine_update (LrgAnimationStateMachine *self,
                                    gfloat                    delta_time)
{
	LrgAnimationStateMachineClass *klass;

	g_return_if_fail (LRG_IS_ANIMATION_STATE_MACHINE (self));
	klass = LRG_ANIMATION_STATE_MACHINE_GET_CLASS (self);
	if (klass->update != NULL)
		klass->update (self, delta_time);
}

void
lrg_animation_state_machine_force_state (LrgAnimationStateMachine *self,
                                         const gchar              *name)
{
	LrgAnimationStateMachinePrivate *priv;
	LrgAnimationState               *new_state;

	g_return_if_fail (LRG_IS_ANIMATION_STATE_MACHINE (self));
	g_return_if_fail (name != NULL);
	priv = lrg_animation_state_machine_get_instance_private (self);

	new_state = g_hash_table_lookup (priv->states, name);
	if (new_state == NULL)
		return;

	if (priv->next_state != NULL && priv->next_state != priv->current_state)
		lrg_animation_state_exit (priv->next_state);
	if (priv->current_state != NULL)
	{
		lrg_animation_state_exit (priv->current_state);
		g_signal_emit (self, signals[SIGNAL_STATE_EXITED], 0,
		               lrg_animation_state_get_name (priv->current_state));
	}
	priv->transitioning = FALSE;
	priv->from_snapshot = FALSE;
	priv->next_state = NULL;
	priv->transition_progress = 0.0f;
	g_clear_object (&priv->active);

	priv->current_state = new_state;
	adopt_sub_machine (self, new_state);
	lrg_animation_state_enter (new_state);
	g_signal_emit (self, signals[SIGNAL_STATE_ENTERED], 0, name);
}

gboolean
lrg_animation_state_machine_is_running (LrgAnimationStateMachine *self)
{
	LrgAnimationStateMachinePrivate *priv;

	g_return_val_if_fail (LRG_IS_ANIMATION_STATE_MACHINE (self), FALSE);
	priv = lrg_animation_state_machine_get_instance_private (self);
	return priv->running;
}

gboolean
lrg_animation_state_machine_is_transitioning (LrgAnimationStateMachine *self)
{
	LrgAnimationStateMachinePrivate *priv;

	g_return_val_if_fail (LRG_IS_ANIMATION_STATE_MACHINE (self), FALSE);
	priv = lrg_animation_state_machine_get_instance_private (self);
	return priv->transitioning;
}

/* --- evaluation --------------------------------------------------------- */

void
lrg_animation_state_machine_sample_pose (LrgAnimationStateMachine *self,
                                         LrgSkeleton              *skeleton,
                                         LrgBonePose              *poses,
                                         guint                     n_poses)
{
	LrgAnimationStateMachinePrivate *priv;

	g_return_if_fail (LRG_IS_ANIMATION_STATE_MACHINE (self));
	g_return_if_fail (LRG_IS_SKELETON (skeleton));
	g_return_if_fail (poses != NULL || n_poses == 0);
	priv = lrg_animation_state_machine_get_instance_private (self);

	if (priv->current_state == NULL)
	{
		lrg_anim_pose_bind (skeleton, poses, n_poses);
	}
	else if (!priv->transitioning || priv->next_state == NULL)
	{
		lrg_animation_state_sample_pose (priv->current_state, skeleton, poses, n_poses);
	}
	else
	{
		LrgBonePose *next;

		/* source: the current state, or the frozen pose after an interrupt */
		if (priv->from_snapshot && priv->snapshot->len == n_poses)
			memcpy (poses, priv->snapshot->data, n_poses * sizeof (LrgBonePose));
		else
			lrg_animation_state_sample_pose (priv->current_state, skeleton, poses, n_poses);
		g_array_set_size (priv->scratch, n_poses);
		next = (LrgBonePose *) (gpointer) priv->scratch->data;
		lrg_animation_state_sample_pose (priv->next_state, skeleton, next, n_poses);
		lrg_anim_pose_blend (poses, next, CLAMP (priv->transition_progress, 0.0f, 1.0f), NULL, n_poses);
	}

	/* remembered for a later interruption */
	g_array_set_size (priv->last_pose, n_poses);
	if (n_poses > 0)
		memcpy (priv->last_pose->data, poses, n_poses * sizeof (LrgBonePose));
}

void
lrg_animation_state_machine_get_root_motion (LrgAnimationStateMachine *self,
                                             const gchar              *bone_name,
                                             gfloat                   *out_translation,
                                             gfloat                   *out_yaw)
{
	LrgAnimationStateMachinePrivate *priv;
	gfloat                           t[3] = { 0.0f, 0.0f, 0.0f };
	gfloat                           yaw = 0.0f;

	g_return_if_fail (LRG_IS_ANIMATION_STATE_MACHINE (self));
	g_return_if_fail (bone_name != NULL);
	g_return_if_fail (out_translation != NULL);
	priv = lrg_animation_state_machine_get_instance_private (self);

	out_translation[0] = out_translation[1] = out_translation[2] = 0.0f;
	if (priv->current_state != NULL && !(priv->transitioning && priv->from_snapshot))
		lrg_animation_state_get_root_motion (priv->current_state, bone_name, out_translation, &yaw);

	if (priv->transitioning && priv->next_state != NULL && priv->next_state != priv->current_state)
	{
		gfloat w = CLAMP (priv->transition_progress, 0.0f, 1.0f);
		gfloat ny = 0.0f;
		guint  k;

		lrg_animation_state_get_root_motion (priv->next_state, bone_name, t, &ny);
		for (k = 0; k < 3; k++)
			out_translation[k] = out_translation[k] * (1.0f - w) + t[k] * w;
		yaw = yaw * (1.0f - w) + ny * w;
	}
	if (out_yaw != NULL)
		*out_yaw = yaw;
}
