/* lrg-animation-layer.c
 *
 * Copyright 2025-2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * An animation layer: a state machine (or a single state) blended over
 * the layers below it, by weight, through a bone mask, either overriding
 * them or adding its difference from the bind pose.
 */

#include "config.h"

#include <string.h>

#include "lrg-animation-layer.h"
#include "lrg-animation-state-machine.h"
#include "lrg-anim-graph.h"

/**
 * SECTION:lrg-animation-layer
 * @Title: LrgAnimationLayer
 * @Short_description: Layered animation blending
 *
 * #LrgAnimationLayer blends a state machine (or one state) over lower
 * layers with a weight, a blend mode and an optional bone mask.
 */

struct _LrgAnimationLayer
{
	GObject parent_instance;

	gchar                    *name;
	gfloat                    weight;
	LrgLayerBlendMode         blend_mode;
	LrgAnimationState        *state;
	LrgAnimationStateMachine *machine;
	GHashTable               *bone_mask;    /* set of names, NULL = all */
	GPtrArray                *mask_order;   /* the same names, in order */
	gboolean                  mask_recursive;
	gboolean                  enabled;

	GArray                   *scratch;      /* LrgBonePose */
	GArray                   *reference;    /* LrgBonePose (bind) */
	GArray                   *weights;      /* gfloat per bone */
};

G_DEFINE_TYPE (LrgAnimationLayer, lrg_animation_layer, G_TYPE_OBJECT)

enum
{
	PROP_0,
	PROP_NAME,
	PROP_WEIGHT,
	PROP_BLEND_MODE,
	PROP_STATE,
	PROP_ENABLED,
	PROP_STATE_MACHINE,
	PROP_MASK_RECURSIVE,
	N_PROPS
};

static GParamSpec *properties[N_PROPS];

static void
lrg_animation_layer_finalize (GObject *object)
{
	LrgAnimationLayer *self = LRG_ANIMATION_LAYER (object);

	g_free (self->name);
	g_clear_object (&self->state);
	g_clear_object (&self->machine);
	g_clear_pointer (&self->bone_mask, g_hash_table_unref);
	g_ptr_array_unref (self->mask_order);
	g_array_unref (self->scratch);
	g_array_unref (self->reference);
	g_array_unref (self->weights);

	G_OBJECT_CLASS (lrg_animation_layer_parent_class)->finalize (object);
}

static void
lrg_animation_layer_get_property (GObject    *object,
                                  guint       prop_id,
                                  GValue     *value,
                                  GParamSpec *pspec)
{
	LrgAnimationLayer *self = LRG_ANIMATION_LAYER (object);

	switch (prop_id)
	{
	case PROP_NAME:           g_value_set_string (value, self->name); break;
	case PROP_WEIGHT:         g_value_set_float (value, self->weight); break;
	case PROP_BLEND_MODE:     g_value_set_enum (value, self->blend_mode); break;
	case PROP_STATE:          g_value_set_object (value, self->state); break;
	case PROP_ENABLED:        g_value_set_boolean (value, self->enabled); break;
	case PROP_STATE_MACHINE:  g_value_set_object (value, self->machine); break;
	case PROP_MASK_RECURSIVE: g_value_set_boolean (value, self->mask_recursive); break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_animation_layer_set_property (GObject      *object,
                                  guint         prop_id,
                                  const GValue *value,
                                  GParamSpec   *pspec)
{
	LrgAnimationLayer *self = LRG_ANIMATION_LAYER (object);

	switch (prop_id)
	{
	case PROP_NAME:
		g_free (self->name);
		self->name = g_value_dup_string (value);
		break;
	case PROP_WEIGHT:
		lrg_animation_layer_set_weight (self, g_value_get_float (value));
		break;
	case PROP_BLEND_MODE:
		lrg_animation_layer_set_blend_mode (self, g_value_get_enum (value));
		break;
	case PROP_STATE:
		lrg_animation_layer_set_state (self, g_value_get_object (value));
		break;
	case PROP_ENABLED:
		lrg_animation_layer_set_enabled (self, g_value_get_boolean (value));
		break;
	case PROP_STATE_MACHINE:
		lrg_animation_layer_set_state_machine (self, g_value_get_object (value));
		break;
	case PROP_MASK_RECURSIVE:
		lrg_animation_layer_set_mask_recursive (self, g_value_get_boolean (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_animation_layer_class_init (LrgAnimationLayerClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = lrg_animation_layer_finalize;
	object_class->get_property = lrg_animation_layer_get_property;
	object_class->set_property = lrg_animation_layer_set_property;

	/**
	 * LrgAnimationLayer:name:
	 *
	 * The layer name.
	 */
	properties[PROP_NAME] =
		g_param_spec_string ("name", "Name", "Layer name", NULL,
		                     G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

	/**
	 * LrgAnimationLayer:weight:
	 *
	 * The blend weight, 0..1.
	 */
	properties[PROP_WEIGHT] =
		g_param_spec_float ("weight", "Weight", "Blend weight", 0.0f, 1.0f, 1.0f,
		                    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgAnimationLayer:blend-mode:
	 *
	 * Override or additive.
	 */
	properties[PROP_BLEND_MODE] =
		g_param_spec_enum ("blend-mode", "Blend Mode", "Layer blend mode",
		                   LRG_TYPE_LAYER_BLEND_MODE, LRG_LAYER_BLEND_OVERRIDE,
		                   G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgAnimationLayer:state:
	 *
	 * A single state played when there is no state machine.
	 */
	properties[PROP_STATE] =
		g_param_spec_object ("state", "State", "Animation state", LRG_TYPE_ANIMATION_STATE,
		                     G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgAnimationLayer:enabled:
	 *
	 * Whether the layer contributes.
	 */
	properties[PROP_ENABLED] =
		g_param_spec_boolean ("enabled", "Enabled", "Whether enabled", TRUE,
		                      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgAnimationLayer:state-machine:
	 *
	 * The state machine the layer plays.
	 */
	properties[PROP_STATE_MACHINE] =
		g_param_spec_object ("state-machine", "State Machine", "The layer's machine",
		                     LRG_TYPE_ANIMATION_STATE_MACHINE,
		                     G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgAnimationLayer:mask-recursive:
	 *
	 * Whether masked bones cover their descendants.
	 */
	properties[PROP_MASK_RECURSIVE] =
		g_param_spec_boolean ("mask-recursive", "Mask Recursive", "Mask covers descendants", TRUE,
		                      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_animation_layer_init (LrgAnimationLayer *self)
{
	self->weight = 1.0f;
	self->blend_mode = LRG_LAYER_BLEND_OVERRIDE;
	self->enabled = TRUE;
	self->mask_recursive = TRUE;
	self->mask_order = g_ptr_array_new_with_free_func (g_free);
	self->scratch = g_array_new (FALSE, TRUE, sizeof (LrgBonePose));
	self->reference = g_array_new (FALSE, TRUE, sizeof (LrgBonePose));
	self->weights = g_array_new (FALSE, TRUE, sizeof (gfloat));
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

LrgAnimationLayer *
lrg_animation_layer_new (const gchar *name)
{
	return g_object_new (LRG_TYPE_ANIMATION_LAYER, "name", name, NULL);
}

const gchar *
lrg_animation_layer_get_name (LrgAnimationLayer *self)
{
	g_return_val_if_fail (LRG_IS_ANIMATION_LAYER (self), NULL);
	return self->name;
}

gfloat
lrg_animation_layer_get_weight (LrgAnimationLayer *self)
{
	g_return_val_if_fail (LRG_IS_ANIMATION_LAYER (self), 0.0f);
	return self->weight;
}

void
lrg_animation_layer_set_weight (LrgAnimationLayer *self,
                                gfloat             weight)
{
	g_return_if_fail (LRG_IS_ANIMATION_LAYER (self));

	weight = CLAMP (weight, 0.0f, 1.0f);
	if (self->weight == weight)
		return;
	self->weight = weight;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_WEIGHT]);
}

LrgLayerBlendMode
lrg_animation_layer_get_blend_mode (LrgAnimationLayer *self)
{
	g_return_val_if_fail (LRG_IS_ANIMATION_LAYER (self), LRG_LAYER_BLEND_OVERRIDE);
	return self->blend_mode;
}

void
lrg_animation_layer_set_blend_mode (LrgAnimationLayer *self,
                                    LrgLayerBlendMode  mode)
{
	g_return_if_fail (LRG_IS_ANIMATION_LAYER (self));

	if (self->blend_mode == mode)
		return;
	self->blend_mode = mode;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_BLEND_MODE]);
}

LrgAnimationState *
lrg_animation_layer_get_state (LrgAnimationLayer *self)
{
	g_return_val_if_fail (LRG_IS_ANIMATION_LAYER (self), NULL);
	return self->state;
}

void
lrg_animation_layer_set_state (LrgAnimationLayer *self,
                               LrgAnimationState *state)
{
	g_return_if_fail (LRG_IS_ANIMATION_LAYER (self));

	if (g_set_object (&self->state, state))
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_STATE]);
}

LrgAnimationStateMachine *
lrg_animation_layer_get_state_machine (LrgAnimationLayer *self)
{
	g_return_val_if_fail (LRG_IS_ANIMATION_LAYER (self), NULL);
	return self->machine;
}

void
lrg_animation_layer_set_state_machine (LrgAnimationLayer        *self,
                                       LrgAnimationStateMachine *machine)
{
	g_return_if_fail (LRG_IS_ANIMATION_LAYER (self));
	g_return_if_fail (machine == NULL || LRG_IS_ANIMATION_STATE_MACHINE (machine));

	if (g_set_object (&self->machine, machine))
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_STATE_MACHINE]);
}

void
lrg_animation_layer_add_mask (LrgAnimationLayer *self,
                              const gchar       *bone_name)
{
	g_return_if_fail (LRG_IS_ANIMATION_LAYER (self));
	g_return_if_fail (bone_name != NULL);

	if (self->bone_mask == NULL)
		self->bone_mask = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
	if (g_hash_table_add (self->bone_mask, g_strdup (bone_name)))
		g_ptr_array_add (self->mask_order, g_strdup (bone_name));
}

void
lrg_animation_layer_remove_mask (LrgAnimationLayer *self,
                                 const gchar       *bone_name)
{
	guint i;

	g_return_if_fail (LRG_IS_ANIMATION_LAYER (self));
	g_return_if_fail (bone_name != NULL);

	if (self->bone_mask == NULL)
		return;
	g_hash_table_remove (self->bone_mask, bone_name);
	for (i = 0; i < self->mask_order->len; i++)
		if (g_strcmp0 (g_ptr_array_index (self->mask_order, i), bone_name) == 0)
		{
			g_ptr_array_remove_index (self->mask_order, i);
			break;
		}
}

void
lrg_animation_layer_clear_mask (LrgAnimationLayer *self)
{
	g_return_if_fail (LRG_IS_ANIMATION_LAYER (self));

	g_clear_pointer (&self->bone_mask, g_hash_table_unref);
	g_ptr_array_set_size (self->mask_order, 0);
}

gchar **
lrg_animation_layer_get_mask (LrgAnimationLayer *self)
{
	gchar **out;
	guint   i;

	g_return_val_if_fail (LRG_IS_ANIMATION_LAYER (self), NULL);

	if (self->bone_mask == NULL)
		return NULL;
	out = g_new0 (gchar *, self->mask_order->len + 1);
	for (i = 0; i < self->mask_order->len; i++)
		out[i] = g_strdup (g_ptr_array_index (self->mask_order, i));
	return out;
}

gboolean
lrg_animation_layer_get_mask_recursive (LrgAnimationLayer *self)
{
	g_return_val_if_fail (LRG_IS_ANIMATION_LAYER (self), TRUE);
	return self->mask_recursive;
}

void
lrg_animation_layer_set_mask_recursive (LrgAnimationLayer *self,
                                        gboolean           recursive)
{
	g_return_if_fail (LRG_IS_ANIMATION_LAYER (self));

	recursive = !!recursive;
	if (self->mask_recursive == recursive)
		return;
	self->mask_recursive = recursive;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MASK_RECURSIVE]);
}

gboolean
lrg_animation_layer_is_bone_masked (LrgAnimationLayer *self,
                                    const gchar       *bone_name)
{
	g_return_val_if_fail (LRG_IS_ANIMATION_LAYER (self), TRUE);

	/* no mask: every bone is affected */
	if (self->bone_mask == NULL)
		return TRUE;
	return g_hash_table_contains (self->bone_mask, bone_name);
}

gboolean
lrg_animation_layer_get_enabled (LrgAnimationLayer *self)
{
	g_return_val_if_fail (LRG_IS_ANIMATION_LAYER (self), FALSE);
	return self->enabled;
}

void
lrg_animation_layer_set_enabled (LrgAnimationLayer *self,
                                 gboolean           enabled)
{
	g_return_if_fail (LRG_IS_ANIMATION_LAYER (self));

	enabled = !!enabled;
	if (self->enabled == enabled)
		return;
	self->enabled = enabled;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ENABLED]);
}

void
lrg_animation_layer_update (LrgAnimationLayer *self,
                            gfloat             delta_time)
{
	g_return_if_fail (LRG_IS_ANIMATION_LAYER (self));

	if (!self->enabled)
		return;
	if (self->machine != NULL)
	{
		if (!lrg_animation_state_machine_is_running (self->machine))
			lrg_animation_state_machine_start (self->machine);
		lrg_animation_state_machine_update (self->machine, delta_time);
	}
	else if (self->state != NULL)
	{
		lrg_animation_state_update (self->state, delta_time);
	}
}

void
lrg_animation_layer_apply (LrgAnimationLayer *self,
                           LrgBonePose       *base_pose,
                           const gchar       *bone_name)
{
	LrgBonePose layer_pose;
	LrgBonePose identity;

	g_return_if_fail (LRG_IS_ANIMATION_LAYER (self));
	g_return_if_fail (base_pose != NULL);

	if (!self->enabled || self->state == NULL || self->weight <= 0.0001f)
		return;
	if (!lrg_animation_layer_is_bone_masked (self, bone_name))
		return;

	lrg_animation_state_sample (self->state, &layer_pose, bone_name);
	if (self->blend_mode == LRG_LAYER_BLEND_OVERRIDE)
	{
		lrg_anim_pose_blend (base_pose, &layer_pose, self->weight, NULL, 1);
		return;
	}
	/* per bone there is no skeleton: the identity is the reference */
	lrg_bone_pose_set_identity (&identity);
	lrg_anim_pose_add (base_pose, &layer_pose, &identity, self->weight, NULL, 1);
}

void
lrg_animation_layer_get_bone_weights (LrgAnimationLayer *self,
                                      LrgSkeleton       *skeleton,
                                      gfloat            *out_weights,
                                      guint              n_weights)
{
	guint i;

	g_return_if_fail (LRG_IS_ANIMATION_LAYER (self));
	g_return_if_fail (LRG_IS_SKELETON (skeleton));
	g_return_if_fail (out_weights != NULL || n_weights == 0);

	for (i = 0; i < n_weights; i++)
	{
		LrgBone *bone;
		gfloat   w = 0.0f;

		if (self->bone_mask == NULL)
		{
			out_weights[i] = 1.0f;
			continue;
		}
		bone = lrg_skeleton_get_bone (skeleton, (gint) i);
		/* the bone itself, then (recursive mask) each ancestor; the
		 * depth guard stops a malformed cyclic hierarchy */
		{
			guint guard = 0;

			while (bone != NULL && guard++ <= n_weights)
			{
				gint parent;

				if (g_hash_table_contains (self->bone_mask, lrg_bone_get_name (bone)))
				{
					w = 1.0f;
					break;
				}
				if (!self->mask_recursive)
					break;
				parent = lrg_bone_get_parent_index (bone);
				bone = parent >= 0 ? lrg_skeleton_get_bone (skeleton, parent) : NULL;
			}
		}
		out_weights[i] = w;
	}
}

void
lrg_animation_layer_sample_pose (LrgAnimationLayer *self,
                                 LrgSkeleton       *skeleton,
                                 LrgBonePose       *poses,
                                 guint              n_poses)
{
	g_return_if_fail (LRG_IS_ANIMATION_LAYER (self));
	g_return_if_fail (LRG_IS_SKELETON (skeleton));

	if (self->machine != NULL)
		lrg_animation_state_machine_sample_pose (self->machine, skeleton, poses, n_poses);
	else if (self->state != NULL)
		lrg_animation_state_sample_pose (self->state, skeleton, poses, n_poses);
	else
		lrg_anim_pose_bind (skeleton, poses, n_poses);
}

void
lrg_animation_layer_blend_pose (LrgAnimationLayer *self,
                                LrgSkeleton       *skeleton,
                                LrgBonePose       *poses,
                                guint              n_poses)
{
	LrgBonePose  *layer_pose;
	const gfloat *mask = NULL;

	g_return_if_fail (LRG_IS_ANIMATION_LAYER (self));
	g_return_if_fail (LRG_IS_SKELETON (skeleton));
	g_return_if_fail (poses != NULL || n_poses == 0);

	if (!self->enabled || self->weight <= 0.0f || (self->machine == NULL && self->state == NULL))
		return;

	g_array_set_size (self->scratch, n_poses);
	layer_pose = (LrgBonePose *) (gpointer) self->scratch->data;
	lrg_animation_layer_sample_pose (self, skeleton, layer_pose, n_poses);

	if (self->bone_mask != NULL)
	{
		g_array_set_size (self->weights, n_poses);
		lrg_animation_layer_get_bone_weights (self, skeleton, (gfloat *) (gpointer) self->weights->data, n_poses);
		mask = (const gfloat *) (gpointer) self->weights->data;
	}

	if (self->blend_mode == LRG_LAYER_BLEND_OVERRIDE)
	{
		lrg_anim_pose_blend (poses, layer_pose, self->weight, mask, n_poses);
		return;
	}

	/* additive: the difference from the bind pose */
	g_array_set_size (self->reference, n_poses);
	lrg_anim_pose_bind (skeleton, (LrgBonePose *) (gpointer) self->reference->data, n_poses);
	lrg_anim_pose_add (poses, layer_pose, (const LrgBonePose *) (gpointer) self->reference->data,
	                   self->weight, mask, n_poses);
}
