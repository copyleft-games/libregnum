/* lrg-animation-layer.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "config.h"
#include "lrg-animation-layer.h"

/**
 * SECTION:lrg-animation-layer
 * @Title: LrgAnimationLayer
 * @Short_description: Animation layer for layered blending
 *
 * #LrgAnimationLayer allows layering animations on top of each other.
 * Each layer has a weight and blend mode, and can optionally have
 * a bone mask to only affect specific bones.
 */

struct _LrgAnimationLayer
{
    GObject parent_instance;

    gchar             *name;
    gfloat             weight;
    LrgLayerBlendMode  blend_mode;
    LrgAnimationState *state;
    GHashTable        *bone_mask;  /* Set of bone names, or NULL for all */
    gboolean           enabled;
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
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

static void
lrg_animation_layer_finalize (GObject *object)
{
    LrgAnimationLayer *self = LRG_ANIMATION_LAYER (object);

    g_free (self->name);
    g_clear_object (&self->state);
    g_clear_pointer (&self->bone_mask, g_hash_table_unref);

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
    case PROP_NAME:
        g_value_set_string (value, self->name);
        break;
    case PROP_WEIGHT:
        g_value_set_float (value, self->weight);
        break;
    case PROP_BLEND_MODE:
        g_value_set_enum (value, self->blend_mode);
        break;
    case PROP_STATE:
        g_value_set_object (value, self->state);
        break;
    case PROP_ENABLED:
        g_value_set_boolean (value, self->enabled);
        break;
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
        self->weight = g_value_get_float (value);
        break;
    case PROP_BLEND_MODE:
        self->blend_mode = g_value_get_enum (value);
        break;
    case PROP_STATE:
        g_set_object (&self->state, g_value_get_object (value));
        break;
    case PROP_ENABLED:
        self->enabled = g_value_get_boolean (value);
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

    properties[PROP_NAME] =
        g_param_spec_string ("name", "Name", "Layer name",
                             NULL, G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

    properties[PROP_WEIGHT] =
        g_param_spec_float ("weight", "Weight", "Blend weight",
                            0.0f, 1.0f, 1.0f, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_BLEND_MODE] =
        g_param_spec_enum ("blend-mode", "Blend Mode", "Layer blend mode",
                           LRG_TYPE_LAYER_BLEND_MODE, LRG_LAYER_BLEND_OVERRIDE,
                           G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_STATE] =
        g_param_spec_object ("state", "State", "Animation state",
                             LRG_TYPE_ANIMATION_STATE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_ENABLED] =
        g_param_spec_boolean ("enabled", "Enabled", "Whether enabled",
                              TRUE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_animation_layer_init (LrgAnimationLayer *self)
{
    self->name = NULL;
    self->weight = 1.0f;
    self->blend_mode = LRG_LAYER_BLEND_OVERRIDE;
    self->state = NULL;
    self->bone_mask = NULL;
    self->enabled = TRUE;
}

/*
 * Public API
 */

LrgAnimationLayer *
lrg_animation_layer_new (const gchar *name)
{
    return g_object_new (LRG_TYPE_ANIMATION_LAYER,
                         "name", name,
                         NULL);
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

    if (self->weight != weight)
    {
        self->weight = weight;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_WEIGHT]);
    }
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

    if (self->blend_mode != mode)
    {
        self->blend_mode = mode;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_BLEND_MODE]);
    }
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

void
lrg_animation_layer_add_mask (LrgAnimationLayer *self,
                               const gchar       *bone_name)
{
    g_return_if_fail (LRG_IS_ANIMATION_LAYER (self));
    g_return_if_fail (bone_name != NULL);

    if (self->bone_mask == NULL)
        self->bone_mask = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);

    g_hash_table_add (self->bone_mask, g_strdup (bone_name));
}

void
lrg_animation_layer_remove_mask (LrgAnimationLayer *self,
                                  const gchar       *bone_name)
{
    g_return_if_fail (LRG_IS_ANIMATION_LAYER (self));
    g_return_if_fail (bone_name != NULL);

    if (self->bone_mask != NULL)
        g_hash_table_remove (self->bone_mask, bone_name);
}

void
lrg_animation_layer_clear_mask (LrgAnimationLayer *self)
{
    g_return_if_fail (LRG_IS_ANIMATION_LAYER (self));

    g_clear_pointer (&self->bone_mask, g_hash_table_unref);
}

gboolean
lrg_animation_layer_is_bone_masked (LrgAnimationLayer *self,
                                     const gchar       *bone_name)
{
    g_return_val_if_fail (LRG_IS_ANIMATION_LAYER (self), TRUE);

    /* No mask means all bones are affected */
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

    if (self->enabled != enabled)
    {
        self->enabled = enabled;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ENABLED]);
    }
}

void
lrg_animation_layer_update (LrgAnimationLayer *self,
                             gfloat             delta_time)
{
    g_return_if_fail (LRG_IS_ANIMATION_LAYER (self));

    if (!self->enabled || self->state == NULL)
        return;

    lrg_animation_state_update (self->state, delta_time);
}

void
lrg_animation_layer_apply (LrgAnimationLayer *self,
                            LrgBonePose       *base_pose,
                            const gchar       *bone_name)
{
    LrgBonePose layer_pose;
    gfloat w;

    g_return_if_fail (LRG_IS_ANIMATION_LAYER (self));
    g_return_if_fail (base_pose != NULL);

    if (!self->enabled || self->state == NULL || self->weight <= 0.0001f)
        return;

    /* Check bone mask */
    if (!lrg_animation_layer_is_bone_masked (self, bone_name))
        return;

    lrg_animation_state_sample (self->state, &layer_pose, bone_name);

    w = self->weight;

    switch (self->blend_mode)
    {
    case LRG_LAYER_BLEND_OVERRIDE:
        /* Blend towards layer pose */
        lrg_bone_pose_lerp_to (base_pose, &layer_pose, w, base_pose);
        break;

    case LRG_LAYER_BLEND_ADDITIVE:
        /*
         * Additive blending: add layer's delta from identity.
         * position_result = position_base + (layer_position - identity_position) * weight
         * For position identity is 0, for scale identity is 1.
         */
        base_pose->position_x += layer_pose.position_x * w;
        base_pose->position_y += layer_pose.position_y * w;
        base_pose->position_z += layer_pose.position_z * w;

        base_pose->scale_x += (layer_pose.scale_x - 1.0f) * w;
        base_pose->scale_y += (layer_pose.scale_y - 1.0f) * w;
        base_pose->scale_z += (layer_pose.scale_z - 1.0f) * w;

        /*
         * For rotation, additive is more complex.
         * We multiply base rotation by the layer rotation scaled.
         * Simplified: just blend towards combined quaternion.
         */
        {
            LrgBonePose combined;
            LrgBonePose *comb_ptr;

            comb_ptr = lrg_bone_pose_multiply (base_pose, &layer_pose);
            if (comb_ptr != NULL)
            {
                combined = *comb_ptr;
                lrg_bone_pose_free (comb_ptr);

                base_pose->rotation_x += (combined.rotation_x - base_pose->rotation_x) * w;
                base_pose->rotation_y += (combined.rotation_y - base_pose->rotation_y) * w;
                base_pose->rotation_z += (combined.rotation_z - base_pose->rotation_z) * w;
                base_pose->rotation_w += (combined.rotation_w - base_pose->rotation_w) * w;

                lrg_bone_pose_normalize_rotation (base_pose);
            }
        }
        break;
    }
}
