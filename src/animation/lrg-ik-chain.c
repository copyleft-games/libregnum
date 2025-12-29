/* lrg-ik-chain.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "config.h"
#include "lrg-ik-chain.h"

/**
 * SECTION:lrg-ik-chain
 * @Title: LrgIKChain
 * @Short_description: IK bone chain
 *
 * #LrgIKChain represents a chain of bones for inverse kinematics.
 * It stores the bones in order from root to tip, along with
 * target and pole vector positions.
 */

struct _LrgIKChain
{
    GObject parent_instance;

    LrgSkeleton *skeleton;
    GPtrArray   *bone_names;

    /* Target position for end effector */
    gfloat target_x;
    gfloat target_y;
    gfloat target_z;

    /* Pole vector for bend direction */
    gfloat pole_x;
    gfloat pole_y;
    gfloat pole_z;

    /* Cached bone lengths */
    GArray *bone_lengths;
    gfloat  total_length;
};

G_DEFINE_TYPE (LrgIKChain, lrg_ik_chain, G_TYPE_OBJECT)

enum
{
    PROP_0,
    PROP_SKELETON,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

static void
lrg_ik_chain_finalize (GObject *object)
{
    LrgIKChain *self = LRG_IK_CHAIN (object);

    g_clear_object (&self->skeleton);
    g_ptr_array_unref (self->bone_names);
    g_array_unref (self->bone_lengths);

    G_OBJECT_CLASS (lrg_ik_chain_parent_class)->finalize (object);
}

static void
lrg_ik_chain_get_property (GObject    *object,
                            guint       prop_id,
                            GValue     *value,
                            GParamSpec *pspec)
{
    LrgIKChain *self = LRG_IK_CHAIN (object);

    switch (prop_id)
    {
    case PROP_SKELETON:
        g_value_set_object (value, self->skeleton);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_ik_chain_set_property (GObject      *object,
                            guint         prop_id,
                            const GValue *value,
                            GParamSpec   *pspec)
{
    LrgIKChain *self = LRG_IK_CHAIN (object);

    switch (prop_id)
    {
    case PROP_SKELETON:
        g_set_object (&self->skeleton, g_value_get_object (value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_ik_chain_class_init (LrgIKChainClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = lrg_ik_chain_finalize;
    object_class->get_property = lrg_ik_chain_get_property;
    object_class->set_property = lrg_ik_chain_set_property;

    properties[PROP_SKELETON] =
        g_param_spec_object ("skeleton", "Skeleton", "Target skeleton",
                             LRG_TYPE_SKELETON, G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_ik_chain_init (LrgIKChain *self)
{
    self->skeleton = NULL;
    self->bone_names = g_ptr_array_new_with_free_func (g_free);
    self->bone_lengths = g_array_new (FALSE, TRUE, sizeof (gfloat));

    self->target_x = 0.0f;
    self->target_y = 0.0f;
    self->target_z = 0.0f;

    self->pole_x = 0.0f;
    self->pole_y = 0.0f;
    self->pole_z = 1.0f;  /* Default pole forward */

    self->total_length = 0.0f;
}

/*
 * Public API
 */

LrgIKChain *
lrg_ik_chain_new (LrgSkeleton *skeleton)
{
    return g_object_new (LRG_TYPE_IK_CHAIN,
                         "skeleton", skeleton,
                         NULL);
}

LrgSkeleton *
lrg_ik_chain_get_skeleton (LrgIKChain *self)
{
    g_return_val_if_fail (LRG_IS_IK_CHAIN (self), NULL);
    return self->skeleton;
}

void
lrg_ik_chain_add_bone (LrgIKChain  *self,
                        const gchar *bone_name)
{
    LrgBone *bone;
    gfloat length;

    g_return_if_fail (LRG_IS_IK_CHAIN (self));
    g_return_if_fail (bone_name != NULL);

    g_ptr_array_add (self->bone_names, g_strdup (bone_name));

    /* Get bone length */
    length = 0.0f;
    if (self->skeleton != NULL)
    {
        bone = lrg_skeleton_get_bone_by_name (self->skeleton, bone_name);
        if (bone != NULL)
            length = lrg_bone_get_length (bone);
    }

    g_array_append_val (self->bone_lengths, length);
    self->total_length += length;
}

void
lrg_ik_chain_clear_bones (LrgIKChain *self)
{
    g_return_if_fail (LRG_IS_IK_CHAIN (self));

    g_ptr_array_set_size (self->bone_names, 0);
    g_array_set_size (self->bone_lengths, 0);
    self->total_length = 0.0f;
}

guint
lrg_ik_chain_get_bone_count (LrgIKChain *self)
{
    g_return_val_if_fail (LRG_IS_IK_CHAIN (self), 0);
    return self->bone_names->len;
}

const gchar *
lrg_ik_chain_get_bone_name (LrgIKChain *self,
                             guint       index)
{
    g_return_val_if_fail (LRG_IS_IK_CHAIN (self), NULL);

    if (index >= self->bone_names->len)
        return NULL;

    return g_ptr_array_index (self->bone_names, index);
}

LrgBone *
lrg_ik_chain_get_bone (LrgIKChain *self,
                        guint       index)
{
    const gchar *name;

    g_return_val_if_fail (LRG_IS_IK_CHAIN (self), NULL);

    if (self->skeleton == NULL)
        return NULL;

    name = lrg_ik_chain_get_bone_name (self, index);
    if (name == NULL)
        return NULL;

    return lrg_skeleton_get_bone_by_name (self->skeleton, name);
}

void
lrg_ik_chain_get_target_position (LrgIKChain *self,
                                   gfloat     *x,
                                   gfloat     *y,
                                   gfloat     *z)
{
    g_return_if_fail (LRG_IS_IK_CHAIN (self));

    if (x != NULL)
        *x = self->target_x;
    if (y != NULL)
        *y = self->target_y;
    if (z != NULL)
        *z = self->target_z;
}

void
lrg_ik_chain_set_target_position (LrgIKChain *self,
                                   gfloat      x,
                                   gfloat      y,
                                   gfloat      z)
{
    g_return_if_fail (LRG_IS_IK_CHAIN (self));

    self->target_x = x;
    self->target_y = y;
    self->target_z = z;
}

void
lrg_ik_chain_get_pole_position (LrgIKChain *self,
                                 gfloat     *x,
                                 gfloat     *y,
                                 gfloat     *z)
{
    g_return_if_fail (LRG_IS_IK_CHAIN (self));

    if (x != NULL)
        *x = self->pole_x;
    if (y != NULL)
        *y = self->pole_y;
    if (z != NULL)
        *z = self->pole_z;
}

void
lrg_ik_chain_set_pole_position (LrgIKChain *self,
                                 gfloat      x,
                                 gfloat      y,
                                 gfloat      z)
{
    g_return_if_fail (LRG_IS_IK_CHAIN (self));

    self->pole_x = x;
    self->pole_y = y;
    self->pole_z = z;
}

gfloat
lrg_ik_chain_get_total_length (LrgIKChain *self)
{
    g_return_val_if_fail (LRG_IS_IK_CHAIN (self), 0.0f);
    return self->total_length;
}

void
lrg_ik_chain_get_end_effector_position (LrgIKChain *self,
                                         gfloat     *x,
                                         gfloat     *y,
                                         gfloat     *z)
{
    LrgBone *tip;
    const LrgBonePose *world_pose;

    g_return_if_fail (LRG_IS_IK_CHAIN (self));

    if (x != NULL)
        *x = 0.0f;
    if (y != NULL)
        *y = 0.0f;
    if (z != NULL)
        *z = 0.0f;

    if (self->bone_names->len == 0 || self->skeleton == NULL)
        return;

    /* Get the world pose of the last bone */
    tip = lrg_ik_chain_get_bone (self, self->bone_names->len - 1);
    if (tip == NULL)
        return;

    world_pose = lrg_bone_get_world_pose (tip);
    if (world_pose == NULL)
        return;

    if (x != NULL)
        *x = world_pose->position_x;
    if (y != NULL)
        *y = world_pose->position_y;
    if (z != NULL)
        *z = world_pose->position_z;
}

void
lrg_ik_chain_apply_to_skeleton (LrgIKChain *self)
{
    guint i;

    g_return_if_fail (LRG_IS_IK_CHAIN (self));

    if (self->skeleton == NULL)
        return;

    /* Apply local poses to skeleton and recalculate world poses */
    for (i = 0; i < self->bone_names->len; i++)
    {
        LrgBone *bone;
        const LrgBonePose *local_pose;
        gint bone_index;

        bone = lrg_ik_chain_get_bone (self, i);
        if (bone == NULL)
            continue;

        local_pose = lrg_bone_get_local_pose (bone);
        bone_index = lrg_bone_get_index (bone);

        lrg_skeleton_set_pose (self->skeleton, bone_index, local_pose);
    }

    lrg_skeleton_calculate_world_poses (self->skeleton);
}
