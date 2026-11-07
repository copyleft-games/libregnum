/* lrg-animation-state.c
 *
 * Copyright 2025-2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * One state of an animation state machine. A state plays a clip, a blend
 * tree or a nested state machine (0.9.0) at its own speed; it records the
 * interval its last update covered so the machine can fire the clip
 * events and take the root motion of exactly that interval.
 */

#include "config.h"

#include <math.h>

#include "lrg-animation-state.h"
#include "lrg-animation-state-machine.h"
#include "lrg-blend-tree.h"
#include "lrg-anim-graph.h"

/**
 * SECTION:lrg-animation-state
 * @Title: LrgAnimationState
 * @Short_description: Animation state for state machines
 *
 * #LrgAnimationState represents a single state in an animation
 * state machine. Each state plays an animation clip, a blend tree or a
 * nested state machine, with playback settings and enter/exit/update
 * hooks.
 */

typedef struct
{
	gchar                    *name;
	LrgAnimationClip         *clip;
	gchar                    *clip_name;
	LrgBlendTree             *blend_tree;
	LrgAnimationStateMachine *sub_machine;
	gchar                    *speed_parameter;
	gfloat                    speed;
	gboolean                  mirror;
	gfloat                    time;
	gfloat                    previous_time;
} LrgAnimationStatePrivate;

G_DEFINE_TYPE_WITH_PRIVATE (LrgAnimationState, lrg_animation_state, G_TYPE_OBJECT)

enum
{
	PROP_0,
	PROP_NAME,
	PROP_CLIP,
	PROP_SPEED,
	PROP_MIRROR,
	PROP_TIME,
	PROP_CLIP_NAME,
	PROP_BLEND_TREE,
	PROP_SUB_MACHINE,
	PROP_SPEED_PARAMETER,
	N_PROPS
};

static GParamSpec *properties[N_PROPS];

/* ==========================================================================
 * Default virtual methods
 * ========================================================================== */

/* Entering rewinds the state (and starts its nested motion). */
static void
lrg_animation_state_real_enter (LrgAnimationState *self)
{
	LrgAnimationStatePrivate *priv = lrg_animation_state_get_instance_private (self);

	priv->time = 0.0f;
	priv->previous_time = 0.0f;
	if (priv->sub_machine != NULL)
	{
		if (lrg_animation_state_machine_is_running (priv->sub_machine))
			lrg_animation_state_machine_stop (priv->sub_machine);
		lrg_animation_state_machine_start (priv->sub_machine);
	}
	else if (priv->blend_tree != NULL)
	{
		lrg_blend_tree_reset (priv->blend_tree);
	}
}

/* Leaving stops a nested machine. */
static void
lrg_animation_state_real_exit (LrgAnimationState *self)
{
	LrgAnimationStatePrivate *priv = lrg_animation_state_get_instance_private (self);

	if (priv->sub_machine != NULL && lrg_animation_state_machine_is_running (priv->sub_machine))
		lrg_animation_state_machine_stop (priv->sub_machine);
}

/* Advances the playback time and the nested motion. */
static void
lrg_animation_state_real_update (LrgAnimationState *self,
                                 gfloat             delta_time)
{
	LrgAnimationStatePrivate *priv = lrg_animation_state_get_instance_private (self);
	gfloat                    step = delta_time * priv->speed;

	priv->previous_time = priv->time;
	priv->time += step;
	if (priv->sub_machine != NULL)
		lrg_animation_state_machine_update (priv->sub_machine, step);
	else if (priv->blend_tree != NULL)
		lrg_blend_tree_update (priv->blend_tree, step);
}

/* Legacy per-bone sample by name (identity for bones without a track). */
static void
lrg_animation_state_real_sample (LrgAnimationState *self,
                                 LrgBonePose       *out_pose,
                                 const gchar       *bone_name)
{
	LrgAnimationStatePrivate *priv = lrg_animation_state_get_instance_private (self);
	gint                      track;

	if (priv->sub_machine != NULL)
	{
		LrgAnimationState *cur = lrg_animation_state_machine_get_current_state (priv->sub_machine);

		if (cur != NULL)
			lrg_animation_state_sample (cur, out_pose, bone_name);
		else
			lrg_bone_pose_set_identity (out_pose);
		return;
	}
	if (priv->blend_tree != NULL)
	{
		lrg_blend_tree_sample (priv->blend_tree, out_pose, bone_name);
		return;
	}
	if (priv->clip == NULL || bone_name == NULL)
	{
		lrg_bone_pose_set_identity (out_pose);
		return;
	}

	/* the track of this bone, if the clip has one (was: track 0) */
	track = lrg_animation_clip_find_track (priv->clip, bone_name);
	if (track < 0)
	{
		lrg_bone_pose_set_identity (out_pose);
		return;
	}
	lrg_animation_clip_sample_track (priv->clip, (guint) track, priv->time, out_pose);

	/* mirroring: reflect across the YZ plane */
	if (priv->mirror)
	{
		out_pose->position_x = -out_pose->position_x;
		out_pose->rotation_y = -out_pose->rotation_y;
		out_pose->rotation_z = -out_pose->rotation_z;
	}
}

/* ==========================================================================
 * GObject
 * ========================================================================== */

static void
lrg_animation_state_finalize (GObject *object)
{
	LrgAnimationState        *self = LRG_ANIMATION_STATE (object);
	LrgAnimationStatePrivate *priv = lrg_animation_state_get_instance_private (self);

	g_free (priv->name);
	g_free (priv->clip_name);
	g_free (priv->speed_parameter);
	g_clear_object (&priv->clip);
	g_clear_object (&priv->blend_tree);
	g_clear_object (&priv->sub_machine);

	G_OBJECT_CLASS (lrg_animation_state_parent_class)->finalize (object);
}

static void
lrg_animation_state_get_property (GObject    *object,
                                  guint       prop_id,
                                  GValue     *value,
                                  GParamSpec *pspec)
{
	LrgAnimationState        *self = LRG_ANIMATION_STATE (object);
	LrgAnimationStatePrivate *priv = lrg_animation_state_get_instance_private (self);

	switch (prop_id)
	{
	case PROP_NAME:            g_value_set_string (value, priv->name); break;
	case PROP_CLIP:            g_value_set_object (value, priv->clip); break;
	case PROP_SPEED:           g_value_set_float (value, priv->speed); break;
	case PROP_MIRROR:          g_value_set_boolean (value, priv->mirror); break;
	case PROP_TIME:            g_value_set_float (value, priv->time); break;
	case PROP_CLIP_NAME:       g_value_set_string (value, priv->clip_name); break;
	case PROP_BLEND_TREE:      g_value_set_object (value, priv->blend_tree); break;
	case PROP_SUB_MACHINE:     g_value_set_object (value, priv->sub_machine); break;
	case PROP_SPEED_PARAMETER: g_value_set_string (value, priv->speed_parameter); break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_animation_state_set_property (GObject      *object,
                                  guint         prop_id,
                                  const GValue *value,
                                  GParamSpec   *pspec)
{
	LrgAnimationState        *self = LRG_ANIMATION_STATE (object);
	LrgAnimationStatePrivate *priv = lrg_animation_state_get_instance_private (self);

	switch (prop_id)
	{
	case PROP_NAME:
		g_free (priv->name);
		priv->name = g_value_dup_string (value);
		break;
	case PROP_CLIP:
		lrg_animation_state_set_clip (self, g_value_get_object (value));
		break;
	case PROP_SPEED:
		lrg_animation_state_set_speed (self, g_value_get_float (value));
		break;
	case PROP_MIRROR:
		lrg_animation_state_set_mirror (self, g_value_get_boolean (value));
		break;
	case PROP_TIME:
		lrg_animation_state_set_time (self, g_value_get_float (value));
		break;
	case PROP_CLIP_NAME:
		lrg_animation_state_set_clip_name (self, g_value_get_string (value));
		break;
	case PROP_BLEND_TREE:
		lrg_animation_state_set_blend_tree (self, g_value_get_object (value));
		break;
	case PROP_SUB_MACHINE:
		lrg_animation_state_set_sub_machine (self, g_value_get_object (value));
		break;
	case PROP_SPEED_PARAMETER:
		lrg_animation_state_set_speed_parameter (self, g_value_get_string (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_animation_state_class_init (LrgAnimationStateClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = lrg_animation_state_finalize;
	object_class->get_property = lrg_animation_state_get_property;
	object_class->set_property = lrg_animation_state_set_property;

	klass->enter = lrg_animation_state_real_enter;
	klass->exit = lrg_animation_state_real_exit;
	klass->update = lrg_animation_state_real_update;
	klass->sample = lrg_animation_state_real_sample;

	/**
	 * LrgAnimationState:name:
	 *
	 * The state name.
	 */
	properties[PROP_NAME] =
		g_param_spec_string ("name", "Name", "State name", NULL,
		                     G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

	/**
	 * LrgAnimationState:clip:
	 *
	 * The animation clip.
	 */
	properties[PROP_CLIP] =
		g_param_spec_object ("clip", "Clip", "Animation clip", LRG_TYPE_ANIMATION_CLIP,
		                     G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgAnimationState:speed:
	 *
	 * The playback speed multiplier.
	 */
	properties[PROP_SPEED] =
		g_param_spec_float ("speed", "Speed", "Playback speed", -10.0f, 10.0f, 1.0f,
		                    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgAnimationState:mirror:
	 *
	 * Whether the animation is mirrored.
	 */
	properties[PROP_MIRROR] =
		g_param_spec_boolean ("mirror", "Mirror", "Mirror animation", FALSE,
		                      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgAnimationState:time:
	 *
	 * The current playback time.
	 */
	properties[PROP_TIME] =
		g_param_spec_float ("time", "Time", "Playback time", -G_MAXFLOAT, G_MAXFLOAT, 0.0f,
		                    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgAnimationState:clip-name:
	 *
	 * The clip key the state refers to in an #LrgAnimGraph.
	 */
	properties[PROP_CLIP_NAME] =
		g_param_spec_string ("clip-name", "Clip Name", "Clip key in the graph", NULL,
		                     G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgAnimationState:blend-tree:
	 *
	 * The blend tree played instead of a clip.
	 */
	properties[PROP_BLEND_TREE] =
		g_param_spec_object ("blend-tree", "Blend Tree", "Blend tree played", LRG_TYPE_BLEND_TREE,
		                     G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgAnimationState:sub-machine:
	 *
	 * The nested state machine run instead of a clip.
	 */
	properties[PROP_SUB_MACHINE] =
		g_param_spec_object ("sub-machine", "Sub-machine", "Nested state machine",
		                     LRG_TYPE_ANIMATION_STATE_MACHINE,
		                     G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgAnimationState:speed-parameter:
	 *
	 * The float parameter that multiplies the speed.
	 */
	properties[PROP_SPEED_PARAMETER] =
		g_param_spec_string ("speed-parameter", "Speed Parameter", "Speed multiplier parameter", NULL,
		                     G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_animation_state_init (LrgAnimationState *self)
{
	LrgAnimationStatePrivate *priv = lrg_animation_state_get_instance_private (self);

	priv->speed = 1.0f;
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

LrgAnimationState *
lrg_animation_state_new (const gchar *name)
{
	return g_object_new (LRG_TYPE_ANIMATION_STATE, "name", name, NULL);
}

const gchar *
lrg_animation_state_get_name (LrgAnimationState *self)
{
	LrgAnimationStatePrivate *priv;

	g_return_val_if_fail (LRG_IS_ANIMATION_STATE (self), NULL);
	priv = lrg_animation_state_get_instance_private (self);
	return priv->name;
}

LrgAnimationClip *
lrg_animation_state_get_clip (LrgAnimationState *self)
{
	LrgAnimationStatePrivate *priv;

	g_return_val_if_fail (LRG_IS_ANIMATION_STATE (self), NULL);
	priv = lrg_animation_state_get_instance_private (self);
	return priv->clip;
}

void
lrg_animation_state_set_clip (LrgAnimationState *self,
                              LrgAnimationClip  *clip)
{
	LrgAnimationStatePrivate *priv;

	g_return_if_fail (LRG_IS_ANIMATION_STATE (self));
	priv = lrg_animation_state_get_instance_private (self);
	if (g_set_object (&priv->clip, clip))
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_CLIP]);
}

gfloat
lrg_animation_state_get_speed (LrgAnimationState *self)
{
	LrgAnimationStatePrivate *priv;

	g_return_val_if_fail (LRG_IS_ANIMATION_STATE (self), 1.0f);
	priv = lrg_animation_state_get_instance_private (self);
	return priv->speed;
}

void
lrg_animation_state_set_speed (LrgAnimationState *self,
                               gfloat             speed)
{
	LrgAnimationStatePrivate *priv;

	g_return_if_fail (LRG_IS_ANIMATION_STATE (self));
	priv = lrg_animation_state_get_instance_private (self);
	if (priv->speed != speed)
	{
		priv->speed = speed;
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SPEED]);
	}
}

gboolean
lrg_animation_state_get_mirror (LrgAnimationState *self)
{
	LrgAnimationStatePrivate *priv;

	g_return_val_if_fail (LRG_IS_ANIMATION_STATE (self), FALSE);
	priv = lrg_animation_state_get_instance_private (self);
	return priv->mirror;
}

void
lrg_animation_state_set_mirror (LrgAnimationState *self,
                                gboolean           mirror)
{
	LrgAnimationStatePrivate *priv;

	g_return_if_fail (LRG_IS_ANIMATION_STATE (self));
	priv = lrg_animation_state_get_instance_private (self);
	mirror = !!mirror;
	if (priv->mirror != mirror)
	{
		priv->mirror = mirror;
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MIRROR]);
	}
}

gfloat
lrg_animation_state_get_time (LrgAnimationState *self)
{
	LrgAnimationStatePrivate *priv;

	g_return_val_if_fail (LRG_IS_ANIMATION_STATE (self), 0.0f);
	priv = lrg_animation_state_get_instance_private (self);
	return priv->time;
}

void
lrg_animation_state_set_time (LrgAnimationState *self,
                              gfloat             time)
{
	LrgAnimationStatePrivate *priv;

	g_return_if_fail (LRG_IS_ANIMATION_STATE (self));
	priv = lrg_animation_state_get_instance_private (self);

	/* a jump, not playback: the interval collapses (no events, no motion) */
	priv->previous_time = time;
	if (priv->blend_tree != NULL)
	{
		gfloat d = lrg_blend_tree_get_duration (priv->blend_tree);

		lrg_blend_tree_set_normalized_time (priv->blend_tree, d > 1e-6f ? time / d : 0.0f);
	}
	if (priv->time != time)
	{
		priv->time = time;
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TIME]);
	}
}

gfloat
lrg_animation_state_get_previous_time (LrgAnimationState *self)
{
	LrgAnimationStatePrivate *priv;

	g_return_val_if_fail (LRG_IS_ANIMATION_STATE (self), 0.0f);
	priv = lrg_animation_state_get_instance_private (self);
	return priv->previous_time;
}

gfloat
lrg_animation_state_get_duration (LrgAnimationState *self)
{
	LrgAnimationStatePrivate *priv;

	g_return_val_if_fail (LRG_IS_ANIMATION_STATE (self), 0.0f);
	priv = lrg_animation_state_get_instance_private (self);

	if (priv->sub_machine != NULL)
	{
		LrgAnimationState *cur = lrg_animation_state_machine_get_current_state (priv->sub_machine);

		return cur != NULL ? lrg_animation_state_get_duration (cur) : 0.0f;
	}
	if (priv->blend_tree != NULL)
		return lrg_blend_tree_get_duration (priv->blend_tree);
	if (priv->clip != NULL)
		return lrg_animation_clip_get_duration (priv->clip);
	return 0.0f;
}

gfloat
lrg_animation_state_get_normalized_time (LrgAnimationState *self)
{
	LrgAnimationStatePrivate *priv;
	gfloat                    duration;

	g_return_val_if_fail (LRG_IS_ANIMATION_STATE (self), 0.0f);
	priv = lrg_animation_state_get_instance_private (self);

	if (priv->sub_machine != NULL)
	{
		LrgAnimationState *cur = lrg_animation_state_machine_get_current_state (priv->sub_machine);

		return cur != NULL ? lrg_animation_state_get_normalized_time (cur) : 0.0f;
	}
	if (priv->blend_tree != NULL)
		return lrg_blend_tree_get_normalized_time (priv->blend_tree);
	if (priv->clip == NULL)
		return 0.0f;
	duration = lrg_animation_clip_get_duration (priv->clip);
	if (duration <= 0.0f)
		return 0.0f;
	return priv->time / duration;
}

void
lrg_animation_state_enter (LrgAnimationState *self)
{
	LrgAnimationStateClass *klass;

	g_return_if_fail (LRG_IS_ANIMATION_STATE (self));
	klass = LRG_ANIMATION_STATE_GET_CLASS (self);
	if (klass->enter != NULL)
		klass->enter (self);
}

void
lrg_animation_state_exit (LrgAnimationState *self)
{
	LrgAnimationStateClass *klass;

	g_return_if_fail (LRG_IS_ANIMATION_STATE (self));
	klass = LRG_ANIMATION_STATE_GET_CLASS (self);
	if (klass->exit != NULL)
		klass->exit (self);
}

void
lrg_animation_state_update (LrgAnimationState *self,
                            gfloat             delta_time)
{
	LrgAnimationStateClass *klass;

	g_return_if_fail (LRG_IS_ANIMATION_STATE (self));
	klass = LRG_ANIMATION_STATE_GET_CLASS (self);
	if (klass->update != NULL)
		klass->update (self, delta_time);
}

void
lrg_animation_state_sample (LrgAnimationState *self,
                            LrgBonePose       *out_pose,
                            const gchar       *bone_name)
{
	LrgAnimationStateClass *klass;

	g_return_if_fail (LRG_IS_ANIMATION_STATE (self));
	g_return_if_fail (out_pose != NULL);
	klass = LRG_ANIMATION_STATE_GET_CLASS (self);
	if (klass->sample != NULL)
		klass->sample (self, out_pose, bone_name);
}

/* --- 0.9.0 ---------------------------------------------------------------- */

LrgBlendTree *
lrg_animation_state_get_blend_tree (LrgAnimationState *self)
{
	LrgAnimationStatePrivate *priv;

	g_return_val_if_fail (LRG_IS_ANIMATION_STATE (self), NULL);
	priv = lrg_animation_state_get_instance_private (self);
	return priv->blend_tree;
}

void
lrg_animation_state_set_blend_tree (LrgAnimationState *self,
                                    LrgBlendTree      *tree)
{
	LrgAnimationStatePrivate *priv;

	g_return_if_fail (LRG_IS_ANIMATION_STATE (self));
	g_return_if_fail (tree == NULL || LRG_IS_BLEND_TREE (tree));
	priv = lrg_animation_state_get_instance_private (self);
	if (g_set_object (&priv->blend_tree, tree))
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_BLEND_TREE]);
}

LrgAnimationStateMachine *
lrg_animation_state_get_sub_machine (LrgAnimationState *self)
{
	LrgAnimationStatePrivate *priv;

	g_return_val_if_fail (LRG_IS_ANIMATION_STATE (self), NULL);
	priv = lrg_animation_state_get_instance_private (self);
	return priv->sub_machine;
}

void
lrg_animation_state_set_sub_machine (LrgAnimationState        *self,
                                     LrgAnimationStateMachine *machine)
{
	LrgAnimationStatePrivate *priv;

	g_return_if_fail (LRG_IS_ANIMATION_STATE (self));
	g_return_if_fail (machine == NULL || LRG_IS_ANIMATION_STATE_MACHINE (machine));
	priv = lrg_animation_state_get_instance_private (self);
	if (g_set_object (&priv->sub_machine, machine))
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SUB_MACHINE]);
}

const gchar *
lrg_animation_state_get_clip_name (LrgAnimationState *self)
{
	LrgAnimationStatePrivate *priv;

	g_return_val_if_fail (LRG_IS_ANIMATION_STATE (self), NULL);
	priv = lrg_animation_state_get_instance_private (self);
	return priv->clip_name;
}

void
lrg_animation_state_set_clip_name (LrgAnimationState *self,
                                   const gchar       *clip_name)
{
	LrgAnimationStatePrivate *priv;

	g_return_if_fail (LRG_IS_ANIMATION_STATE (self));
	priv = lrg_animation_state_get_instance_private (self);
	if (g_strcmp0 (priv->clip_name, clip_name) == 0)
		return;
	g_free (priv->clip_name);
	priv->clip_name = g_strdup (clip_name);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_CLIP_NAME]);
}

const gchar *
lrg_animation_state_get_speed_parameter (LrgAnimationState *self)
{
	LrgAnimationStatePrivate *priv;

	g_return_val_if_fail (LRG_IS_ANIMATION_STATE (self), NULL);
	priv = lrg_animation_state_get_instance_private (self);
	return priv->speed_parameter;
}

void
lrg_animation_state_set_speed_parameter (LrgAnimationState *self,
                                         const gchar       *name)
{
	LrgAnimationStatePrivate *priv;

	g_return_if_fail (LRG_IS_ANIMATION_STATE (self));
	priv = lrg_animation_state_get_instance_private (self);
	if (g_strcmp0 (priv->speed_parameter, name) == 0)
		return;
	g_free (priv->speed_parameter);
	priv->speed_parameter = g_strdup (name);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SPEED_PARAMETER]);
}

void
lrg_animation_state_sample_pose (LrgAnimationState *self,
                                 LrgSkeleton       *skeleton,
                                 LrgBonePose       *poses,
                                 guint              n_poses)
{
	LrgAnimationStatePrivate *priv;
	LrgAnimationStateClass   *klass;
	guint                     i;

	g_return_if_fail (LRG_IS_ANIMATION_STATE (self));
	g_return_if_fail (LRG_IS_SKELETON (skeleton));
	g_return_if_fail (poses != NULL || n_poses == 0);

	priv = lrg_animation_state_get_instance_private (self);
	klass = LRG_ANIMATION_STATE_GET_CLASS (self);

	/* a subclass sampler is honoured bone by bone */
	if (klass->sample != lrg_animation_state_real_sample)
	{
		lrg_anim_pose_bind (skeleton, poses, n_poses);
		for (i = 0; i < n_poses; i++)
		{
			LrgBone *bone = lrg_skeleton_get_bone (skeleton, (gint) i);

			if (bone != NULL)
				lrg_animation_state_sample (self, &poses[i], lrg_bone_get_name (bone));
		}
		return;
	}

	if (priv->sub_machine != NULL)
	{
		lrg_animation_state_machine_sample_pose (priv->sub_machine, skeleton, poses, n_poses);
		return;
	}
	if (priv->blend_tree != NULL)
	{
		lrg_blend_tree_sample_pose (priv->blend_tree, skeleton, poses, n_poses);
		return;
	}
	if (priv->clip == NULL)
	{
		lrg_anim_pose_bind (skeleton, poses, n_poses);
		return;
	}
	lrg_anim_pose_sample_clip (priv->clip, skeleton, priv->time, poses, n_poses);
	if (priv->mirror)
	{
		for (i = 0; i < n_poses; i++)
		{
			poses[i].position_x = -poses[i].position_x;
			poses[i].rotation_y = -poses[i].rotation_y;
			poses[i].rotation_z = -poses[i].rotation_z;
		}
	}
}

void
lrg_animation_state_get_root_motion (LrgAnimationState *self,
                                     const gchar       *bone_name,
                                     gfloat            *out_translation,
                                     gfloat            *out_yaw)
{
	LrgAnimationStatePrivate *priv;

	g_return_if_fail (LRG_IS_ANIMATION_STATE (self));
	g_return_if_fail (bone_name != NULL);
	g_return_if_fail (out_translation != NULL);

	priv = lrg_animation_state_get_instance_private (self);
	out_translation[0] = out_translation[1] = out_translation[2] = 0.0f;
	if (out_yaw != NULL)
		*out_yaw = 0.0f;

	if (priv->sub_machine != NULL)
		lrg_animation_state_machine_get_root_motion (priv->sub_machine, bone_name, out_translation, out_yaw);
	else if (priv->blend_tree != NULL)
		lrg_blend_tree_get_root_motion (priv->blend_tree, bone_name, out_translation, out_yaw);
	else if (priv->clip != NULL)
		lrg_anim_pose_clip_root_motion (priv->clip, bone_name, priv->previous_time, priv->time,
		                                out_translation, out_yaw);
}
