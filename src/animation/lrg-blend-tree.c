/* lrg-blend-tree.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "config.h"
#include "lrg-blend-tree.h"
#include <math.h>

/**
 * SECTION:lrg-blend-tree
 * @Title: LrgBlendTree
 * @Short_description: Parameter-driven animation blending
 *
 * #LrgBlendTree provides smooth blending between multiple animations
 * based on one or two parameters. Supports 1D threshold blending,
 * 2D directional blending, and direct weight control.
 */

/*
 * LrgBlendTreeChild boxed type
 */

G_DEFINE_BOXED_TYPE (LrgBlendTreeChild, lrg_blend_tree_child,
                     lrg_blend_tree_child_copy,
                     lrg_blend_tree_child_free)

LrgBlendTreeChild *
lrg_blend_tree_child_new (LrgAnimationClip *clip)
{
    LrgBlendTreeChild *child;

    child = g_slice_new0 (LrgBlendTreeChild);
    child->clip = clip ? g_object_ref (clip) : NULL;
    child->threshold = 0.0f;
    child->position_x = 0.0f;
    child->position_y = 0.0f;
    child->weight = 0.0f;
    child->speed = 1.0f;
    child->computed_weight = 0.0f;
    child->time = 0.0f;

    return child;
}

LrgBlendTreeChild *
lrg_blend_tree_child_copy (const LrgBlendTreeChild *child)
{
    LrgBlendTreeChild *copy;

    if (child == NULL)
        return NULL;

    copy = g_slice_new (LrgBlendTreeChild);
    *copy = *child;
    if (child->clip != NULL)
        copy->clip = g_object_ref (child->clip);

    return copy;
}

void
lrg_blend_tree_child_free (LrgBlendTreeChild *child)
{
    if (child == NULL)
        return;

    g_clear_object (&child->clip);
    g_slice_free (LrgBlendTreeChild, child);
}

/*
 * LrgBlendTree
 */

typedef struct
{
    LrgBlendType blend_type;
    GList       *children;
    gfloat       param_x;
    gfloat       param_y;
    gfloat       time;
} LrgBlendTreePrivate;

G_DEFINE_TYPE_WITH_PRIVATE (LrgBlendTree, lrg_blend_tree, G_TYPE_OBJECT)

enum
{
    PROP_0,
    PROP_BLEND_TYPE,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

/*
 * Weight calculation helpers
 */

static void
compute_weights_1d (LrgBlendTree *self)
{
    LrgBlendTreePrivate *priv;
    GList *l;
    LrgBlendTreeChild *lower;
    LrgBlendTreeChild *upper;
    gfloat total_weight;

    priv = lrg_blend_tree_get_instance_private (self);

    lower = NULL;
    upper = NULL;

    /* Reset weights */
    for (l = priv->children; l != NULL; l = l->next)
    {
        LrgBlendTreeChild *c = l->data;
        c->computed_weight = 0.0f;
    }

    /* Find surrounding thresholds */
    for (l = priv->children; l != NULL; l = l->next)
    {
        LrgBlendTreeChild *c = l->data;

        if (c->threshold <= priv->param_x)
        {
            if (lower == NULL || c->threshold > lower->threshold)
                lower = c;
        }
        if (c->threshold >= priv->param_x)
        {
            if (upper == NULL || c->threshold < upper->threshold)
                upper = c;
        }
    }

    /* Compute blend weights */
    if (lower == upper && lower != NULL)
    {
        lower->computed_weight = 1.0f;
    }
    else if (lower != NULL && upper != NULL)
    {
        gfloat range;
        gfloat t;

        range = upper->threshold - lower->threshold;
        if (range > 0.0001f)
        {
            t = (priv->param_x - lower->threshold) / range;
            lower->computed_weight = 1.0f - t;
            upper->computed_weight = t;
        }
        else
        {
            lower->computed_weight = 0.5f;
            upper->computed_weight = 0.5f;
        }
    }
    else if (lower != NULL)
    {
        lower->computed_weight = 1.0f;
    }
    else if (upper != NULL)
    {
        upper->computed_weight = 1.0f;
    }

    /* Normalize */
    total_weight = 0.0f;
    for (l = priv->children; l != NULL; l = l->next)
    {
        LrgBlendTreeChild *c = l->data;
        total_weight += c->computed_weight;
    }

    if (total_weight > 0.0001f)
    {
        for (l = priv->children; l != NULL; l = l->next)
        {
            LrgBlendTreeChild *c = l->data;
            c->computed_weight /= total_weight;
        }
    }
}

static void
compute_weights_2d (LrgBlendTree *self)
{
    LrgBlendTreePrivate *priv;
    GList *l;
    gfloat total_weight;
    gfloat px;
    gfloat py;

    priv = lrg_blend_tree_get_instance_private (self);

    px = priv->param_x;
    py = priv->param_y;

    /*
     * Inverse distance weighting for 2D freeform blend.
     * w_i = 1 / d_i^2, where d_i is distance to child position.
     */
    total_weight = 0.0f;

    for (l = priv->children; l != NULL; l = l->next)
    {
        LrgBlendTreeChild *c = l->data;
        gfloat dx;
        gfloat dy;
        gfloat dist_sq;

        dx = px - c->position_x;
        dy = py - c->position_y;
        dist_sq = dx * dx + dy * dy;

        if (dist_sq < 0.0001f)
        {
            /* Very close, give full weight */
            c->computed_weight = 1000000.0f;
        }
        else
        {
            c->computed_weight = 1.0f / dist_sq;
        }

        total_weight += c->computed_weight;
    }

    /* Normalize */
    if (total_weight > 0.0001f)
    {
        for (l = priv->children; l != NULL; l = l->next)
        {
            LrgBlendTreeChild *c = l->data;
            c->computed_weight /= total_weight;
        }
    }
}

static void
compute_weights_direct (LrgBlendTree *self)
{
    LrgBlendTreePrivate *priv;
    GList *l;
    gfloat total_weight;

    priv = lrg_blend_tree_get_instance_private (self);

    /* Use explicit weights, normalize them */
    total_weight = 0.0f;

    for (l = priv->children; l != NULL; l = l->next)
    {
        LrgBlendTreeChild *c = l->data;
        c->computed_weight = c->weight;
        total_weight += c->weight;
    }

    if (total_weight > 0.0001f)
    {
        for (l = priv->children; l != NULL; l = l->next)
        {
            LrgBlendTreeChild *c = l->data;
            c->computed_weight /= total_weight;
        }
    }
}

/*
 * Virtual method implementations
 */

static void
lrg_blend_tree_real_update (LrgBlendTree *self,
                             gfloat        delta_time)
{
    LrgBlendTreePrivate *priv;
    GList *l;

    priv = lrg_blend_tree_get_instance_private (self);

    /* Compute weights based on blend type */
    switch (priv->blend_type)
    {
    case LRG_BLEND_TYPE_1D:
        compute_weights_1d (self);
        break;
    case LRG_BLEND_TYPE_2D_SIMPLE:
    case LRG_BLEND_TYPE_2D_FREEFORM:
        compute_weights_2d (self);
        break;
    case LRG_BLEND_TYPE_DIRECT:
        compute_weights_direct (self);
        break;
    }

    /* Update child times */
    for (l = priv->children; l != NULL; l = l->next)
    {
        LrgBlendTreeChild *c = l->data;
        c->time += delta_time * c->speed;
    }

    priv->time += delta_time;
}

static void
lrg_blend_tree_real_sample (LrgBlendTree *self,
                             LrgBonePose  *out_pose,
                             const gchar  *bone_name)
{
    LrgBlendTreePrivate *priv;
    GList *l;
    LrgBonePose result;
    gboolean first;

    priv = lrg_blend_tree_get_instance_private (self);

    lrg_bone_pose_set_identity (&result);
    first = TRUE;

    for (l = priv->children; l != NULL; l = l->next)
    {
        LrgBlendTreeChild *c = l->data;
        guint i;

        if (c->computed_weight < 0.0001f || c->clip == NULL)
            continue;

        /* Find track by bone name and sample */
        for (i = 0; i < lrg_animation_clip_get_track_count (c->clip); i++)
        {
            const gchar *track_bone;

            track_bone = lrg_animation_clip_get_track_bone_name (c->clip, i);
            if (track_bone != NULL && g_strcmp0 (track_bone, bone_name) == 0)
            {
                LrgBonePose child_pose;

                lrg_animation_clip_sample_track (c->clip, i, c->time, &child_pose);

                if (first)
                {
                    /* Scale first pose by its weight */
                    result.position_x = child_pose.position_x * c->computed_weight;
                    result.position_y = child_pose.position_y * c->computed_weight;
                    result.position_z = child_pose.position_z * c->computed_weight;
                    result.scale_x = child_pose.scale_x * c->computed_weight;
                    result.scale_y = child_pose.scale_y * c->computed_weight;
                    result.scale_z = child_pose.scale_z * c->computed_weight;
                    result.rotation_x = child_pose.rotation_x * c->computed_weight;
                    result.rotation_y = child_pose.rotation_y * c->computed_weight;
                    result.rotation_z = child_pose.rotation_z * c->computed_weight;
                    result.rotation_w = child_pose.rotation_w * c->computed_weight;
                    first = FALSE;
                }
                else
                {
                    /* Add weighted pose */
                    result.position_x += child_pose.position_x * c->computed_weight;
                    result.position_y += child_pose.position_y * c->computed_weight;
                    result.position_z += child_pose.position_z * c->computed_weight;
                    result.scale_x += child_pose.scale_x * c->computed_weight;
                    result.scale_y += child_pose.scale_y * c->computed_weight;
                    result.scale_z += child_pose.scale_z * c->computed_weight;
                    result.rotation_x += child_pose.rotation_x * c->computed_weight;
                    result.rotation_y += child_pose.rotation_y * c->computed_weight;
                    result.rotation_z += child_pose.rotation_z * c->computed_weight;
                    result.rotation_w += child_pose.rotation_w * c->computed_weight;
                }
                break;
            }
        }
    }

    /* Normalize rotation quaternion */
    lrg_bone_pose_normalize_rotation (&result);

    *out_pose = result;
}

/*
 * GObject virtual methods
 */

static void
lrg_blend_tree_finalize (GObject *object)
{
    LrgBlendTree *self = LRG_BLEND_TREE (object);
    LrgBlendTreePrivate *priv = lrg_blend_tree_get_instance_private (self);

    g_list_free_full (priv->children, (GDestroyNotify)lrg_blend_tree_child_free);

    G_OBJECT_CLASS (lrg_blend_tree_parent_class)->finalize (object);
}

static void
lrg_blend_tree_get_property (GObject    *object,
                              guint       prop_id,
                              GValue     *value,
                              GParamSpec *pspec)
{
    LrgBlendTree *self = LRG_BLEND_TREE (object);
    LrgBlendTreePrivate *priv = lrg_blend_tree_get_instance_private (self);

    switch (prop_id)
    {
    case PROP_BLEND_TYPE:
        g_value_set_enum (value, priv->blend_type);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_blend_tree_set_property (GObject      *object,
                              guint         prop_id,
                              const GValue *value,
                              GParamSpec   *pspec)
{
    LrgBlendTree *self = LRG_BLEND_TREE (object);
    LrgBlendTreePrivate *priv = lrg_blend_tree_get_instance_private (self);

    switch (prop_id)
    {
    case PROP_BLEND_TYPE:
        priv->blend_type = g_value_get_enum (value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_blend_tree_class_init (LrgBlendTreeClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = lrg_blend_tree_finalize;
    object_class->get_property = lrg_blend_tree_get_property;
    object_class->set_property = lrg_blend_tree_set_property;

    klass->update = lrg_blend_tree_real_update;
    klass->sample = lrg_blend_tree_real_sample;

    properties[PROP_BLEND_TYPE] =
        g_param_spec_enum ("blend-type", "Blend Type", "Type of blending",
                           LRG_TYPE_BLEND_TYPE, LRG_BLEND_TYPE_1D,
                           G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_blend_tree_init (LrgBlendTree *self)
{
    LrgBlendTreePrivate *priv = lrg_blend_tree_get_instance_private (self);

    priv->blend_type = LRG_BLEND_TYPE_1D;
    priv->children = NULL;
    priv->param_x = 0.0f;
    priv->param_y = 0.0f;
    priv->time = 0.0f;
}

/*
 * Public API
 */

LrgBlendTree *
lrg_blend_tree_new (LrgBlendType blend_type)
{
    return g_object_new (LRG_TYPE_BLEND_TREE,
                         "blend-type", blend_type,
                         NULL);
}

LrgBlendType
lrg_blend_tree_get_blend_type (LrgBlendTree *self)
{
    LrgBlendTreePrivate *priv;

    g_return_val_if_fail (LRG_IS_BLEND_TREE (self), LRG_BLEND_TYPE_1D);

    priv = lrg_blend_tree_get_instance_private (self);
    return priv->blend_type;
}

void
lrg_blend_tree_add_child (LrgBlendTree     *self,
                           LrgAnimationClip *clip,
                           gfloat            threshold)
{
    LrgBlendTreePrivate *priv;
    LrgBlendTreeChild *child;

    g_return_if_fail (LRG_IS_BLEND_TREE (self));

    priv = lrg_blend_tree_get_instance_private (self);

    child = lrg_blend_tree_child_new (clip);
    child->threshold = threshold;

    priv->children = g_list_append (priv->children, child);
}

void
lrg_blend_tree_add_child_2d (LrgBlendTree     *self,
                              LrgAnimationClip *clip,
                              gfloat            x,
                              gfloat            y)
{
    LrgBlendTreePrivate *priv;
    LrgBlendTreeChild *child;

    g_return_if_fail (LRG_IS_BLEND_TREE (self));

    priv = lrg_blend_tree_get_instance_private (self);

    child = lrg_blend_tree_child_new (clip);
    child->position_x = x;
    child->position_y = y;

    priv->children = g_list_append (priv->children, child);
}

void
lrg_blend_tree_clear_children (LrgBlendTree *self)
{
    LrgBlendTreePrivate *priv;

    g_return_if_fail (LRG_IS_BLEND_TREE (self));

    priv = lrg_blend_tree_get_instance_private (self);

    g_list_free_full (priv->children, (GDestroyNotify)lrg_blend_tree_child_free);
    priv->children = NULL;
}

GList *
lrg_blend_tree_get_children (LrgBlendTree *self)
{
    LrgBlendTreePrivate *priv;

    g_return_val_if_fail (LRG_IS_BLEND_TREE (self), NULL);

    priv = lrg_blend_tree_get_instance_private (self);
    return priv->children;
}

void
lrg_blend_tree_set_parameter (LrgBlendTree *self,
                               gfloat        value)
{
    LrgBlendTreePrivate *priv;

    g_return_if_fail (LRG_IS_BLEND_TREE (self));

    priv = lrg_blend_tree_get_instance_private (self);
    priv->param_x = value;
}

gfloat
lrg_blend_tree_get_parameter (LrgBlendTree *self)
{
    LrgBlendTreePrivate *priv;

    g_return_val_if_fail (LRG_IS_BLEND_TREE (self), 0.0f);

    priv = lrg_blend_tree_get_instance_private (self);
    return priv->param_x;
}

void
lrg_blend_tree_set_parameter_2d (LrgBlendTree *self,
                                  gfloat        x,
                                  gfloat        y)
{
    LrgBlendTreePrivate *priv;

    g_return_if_fail (LRG_IS_BLEND_TREE (self));

    priv = lrg_blend_tree_get_instance_private (self);
    priv->param_x = x;
    priv->param_y = y;
}

gfloat
lrg_blend_tree_get_parameter_x (LrgBlendTree *self)
{
    LrgBlendTreePrivate *priv;

    g_return_val_if_fail (LRG_IS_BLEND_TREE (self), 0.0f);

    priv = lrg_blend_tree_get_instance_private (self);
    return priv->param_x;
}

gfloat
lrg_blend_tree_get_parameter_y (LrgBlendTree *self)
{
    LrgBlendTreePrivate *priv;

    g_return_val_if_fail (LRG_IS_BLEND_TREE (self), 0.0f);

    priv = lrg_blend_tree_get_instance_private (self);
    return priv->param_y;
}

void
lrg_blend_tree_update (LrgBlendTree *self,
                        gfloat        delta_time)
{
    LrgBlendTreeClass *klass;

    g_return_if_fail (LRG_IS_BLEND_TREE (self));

    klass = LRG_BLEND_TREE_GET_CLASS (self);
    if (klass->update != NULL)
        klass->update (self, delta_time);
}

void
lrg_blend_tree_sample (LrgBlendTree *self,
                        LrgBonePose  *out_pose,
                        const gchar  *bone_name)
{
    LrgBlendTreeClass *klass;

    g_return_if_fail (LRG_IS_BLEND_TREE (self));
    g_return_if_fail (out_pose != NULL);

    klass = LRG_BLEND_TREE_GET_CLASS (self);
    if (klass->sample != NULL)
        klass->sample (self, out_pose, bone_name);
}

gfloat
lrg_blend_tree_get_time (LrgBlendTree *self)
{
    LrgBlendTreePrivate *priv;

    g_return_val_if_fail (LRG_IS_BLEND_TREE (self), 0.0f);

    priv = lrg_blend_tree_get_instance_private (self);
    return priv->time;
}

void
lrg_blend_tree_set_time (LrgBlendTree *self,
                          gfloat        time)
{
    LrgBlendTreePrivate *priv;
    GList *l;

    g_return_if_fail (LRG_IS_BLEND_TREE (self));

    priv = lrg_blend_tree_get_instance_private (self);
    priv->time = time;

    /* Reset child times too */
    for (l = priv->children; l != NULL; l = l->next)
    {
        LrgBlendTreeChild *c = l->data;
        c->time = time;
    }
}
