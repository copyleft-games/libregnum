/* lrg-skeleton.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "config.h"
#include "lrg-skeleton.h"

/**
 * SECTION:lrg-skeleton
 * @Title: LrgSkeleton
 * @Short_description: Skeletal hierarchy for animation
 *
 * #LrgSkeleton manages a hierarchical collection of #LrgBone objects.
 * It handles:
 *
 * - Bone organization in a parent-child hierarchy
 * - World pose calculation from local poses
 * - Pose manipulation (set, blend, reset)
 *
 * The skeleton maintains bones in a list, with each bone referencing
 * its parent by index. Root bones have a parent index of -1.
 */

typedef struct
{
    gchar  *name;
    GList  *bones;         /* List of LrgBone */
    GHashTable *bone_map;  /* name -> LrgBone */
} LrgSkeletonPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (LrgSkeleton, lrg_skeleton, G_TYPE_OBJECT)

enum
{
    PROP_0,
    PROP_NAME,
    PROP_BONE_COUNT,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

static void
lrg_skeleton_real_calculate_world_poses (LrgSkeleton *self)
{
    LrgSkeletonPrivate *priv = lrg_skeleton_get_instance_private (self);
    GList *l;
    gboolean changed;
    guint max_iterations;

    /*
     * We need to process bones in order from roots to leaves.
     * Since bones may not be in order, we do multiple passes.
     * A more efficient implementation would sort bones by depth first.
     */

    max_iterations = 100;  /* Prevent infinite loops */

    /* First pass: set world pose = local pose for root bones */
    for (l = priv->bones; l != NULL; l = l->next)
    {
        LrgBone *bone = LRG_BONE (l->data);

        if (lrg_bone_is_root (bone))
        {
            const LrgBonePose *local = lrg_bone_get_local_pose (bone);
            lrg_bone_set_world_pose (bone, local);
        }
    }

    /* Subsequent passes: calculate world poses for children */

    do
    {
        changed = FALSE;
        max_iterations--;

        for (l = priv->bones; l != NULL; l = l->next)
        {
            LrgBone *bone = LRG_BONE (l->data);
            gint parent_index;
            LrgBone *parent;
            const LrgBonePose *parent_world;
            const LrgBonePose *local;
            LrgBonePose *combined;

            if (lrg_bone_is_root (bone))
                continue;

            parent_index = lrg_bone_get_parent_index (bone);
            parent = lrg_skeleton_get_bone (self, parent_index);

            if (parent == NULL)
                continue;

            parent_world = lrg_bone_get_world_pose (parent);
            local = lrg_bone_get_local_pose (bone);

            /* Combine parent world pose with local pose */
            combined = lrg_bone_pose_multiply (parent_world, local);
            lrg_bone_set_world_pose (bone, combined);
            lrg_bone_pose_free (combined);

            changed = TRUE;
        }
    } while (changed && max_iterations > 0);
}

static void
lrg_skeleton_real_update (LrgSkeleton *self,
                          gfloat       delta_time)
{
    /* Default implementation just recalculates world poses */
    lrg_skeleton_calculate_world_poses (self);
}

static void
lrg_skeleton_finalize (GObject *object)
{
    LrgSkeleton        *self = LRG_SKELETON (object);
    LrgSkeletonPrivate *priv = lrg_skeleton_get_instance_private (self);

    g_clear_pointer (&priv->name, g_free);
    g_list_free_full (priv->bones, g_object_unref);
    priv->bones = NULL;
    g_clear_pointer (&priv->bone_map, g_hash_table_destroy);

    G_OBJECT_CLASS (lrg_skeleton_parent_class)->finalize (object);
}

static void
lrg_skeleton_get_property (GObject    *object,
                           guint       prop_id,
                           GValue     *value,
                           GParamSpec *pspec)
{
    LrgSkeleton        *self = LRG_SKELETON (object);
    LrgSkeletonPrivate *priv = lrg_skeleton_get_instance_private (self);

    switch (prop_id)
    {
    case PROP_NAME:
        g_value_set_string (value, priv->name);
        break;
    case PROP_BONE_COUNT:
        g_value_set_uint (value, g_list_length (priv->bones));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_skeleton_set_property (GObject      *object,
                           guint         prop_id,
                           const GValue *value,
                           GParamSpec   *pspec)
{
    LrgSkeleton        *self = LRG_SKELETON (object);
    LrgSkeletonPrivate *priv = lrg_skeleton_get_instance_private (self);

    switch (prop_id)
    {
    case PROP_NAME:
        g_free (priv->name);
        priv->name = g_value_dup_string (value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_skeleton_class_init (LrgSkeletonClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = lrg_skeleton_finalize;
    object_class->get_property = lrg_skeleton_get_property;
    object_class->set_property = lrg_skeleton_set_property;

    /* Virtual methods */
    klass->calculate_world_poses = lrg_skeleton_real_calculate_world_poses;
    klass->update = lrg_skeleton_real_update;

    /**
     * LrgSkeleton:name:
     *
     * The skeleton name.
     */
    properties[PROP_NAME] =
        g_param_spec_string ("name",
                             "Name",
                             "Skeleton name",
                             NULL,
                             G_PARAM_READWRITE |
                             G_PARAM_STATIC_STRINGS |
                             G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgSkeleton:bone-count:
     *
     * Number of bones in the skeleton.
     */
    properties[PROP_BONE_COUNT] =
        g_param_spec_uint ("bone-count",
                           "Bone Count",
                           "Number of bones",
                           0, G_MAXUINT, 0,
                           G_PARAM_READABLE |
                           G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_skeleton_init (LrgSkeleton *self)
{
    LrgSkeletonPrivate *priv = lrg_skeleton_get_instance_private (self);

    priv->name = NULL;
    priv->bones = NULL;
    priv->bone_map = g_hash_table_new (g_str_hash, g_str_equal);
}

LrgSkeleton *
lrg_skeleton_new (void)
{
    return g_object_new (LRG_TYPE_SKELETON, NULL);
}

void
lrg_skeleton_add_bone (LrgSkeleton *self,
                       LrgBone     *bone)
{
    LrgSkeletonPrivate *priv;
    const gchar *name;

    g_return_if_fail (LRG_IS_SKELETON (self));
    g_return_if_fail (LRG_IS_BONE (bone));

    priv = lrg_skeleton_get_instance_private (self);

    priv->bones = g_list_append (priv->bones, g_object_ref (bone));

    name = lrg_bone_get_name (bone);
    if (name != NULL)
        g_hash_table_insert (priv->bone_map, (gpointer)name, bone);

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_BONE_COUNT]);
}

void
lrg_skeleton_remove_bone (LrgSkeleton *self,
                          LrgBone     *bone)
{
    LrgSkeletonPrivate *priv;
    GList *l;
    const gchar *name;

    g_return_if_fail (LRG_IS_SKELETON (self));
    g_return_if_fail (LRG_IS_BONE (bone));

    priv = lrg_skeleton_get_instance_private (self);

    l = g_list_find (priv->bones, bone);
    if (l != NULL)
    {
        name = lrg_bone_get_name (bone);
        if (name != NULL)
            g_hash_table_remove (priv->bone_map, name);

        priv->bones = g_list_delete_link (priv->bones, l);
        g_object_unref (bone);

        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_BONE_COUNT]);
    }
}

LrgBone *
lrg_skeleton_get_bone (LrgSkeleton *self,
                       gint         index)
{
    LrgSkeletonPrivate *priv;
    GList *l;

    g_return_val_if_fail (LRG_IS_SKELETON (self), NULL);

    priv = lrg_skeleton_get_instance_private (self);

    for (l = priv->bones; l != NULL; l = l->next)
    {
        LrgBone *bone = LRG_BONE (l->data);

        if (lrg_bone_get_index (bone) == index)
            return bone;
    }

    return NULL;
}

LrgBone *
lrg_skeleton_get_bone_by_name (LrgSkeleton *self,
                               const gchar *name)
{
    LrgSkeletonPrivate *priv;

    g_return_val_if_fail (LRG_IS_SKELETON (self), NULL);
    g_return_val_if_fail (name != NULL, NULL);

    priv = lrg_skeleton_get_instance_private (self);

    return g_hash_table_lookup (priv->bone_map, name);
}

guint
lrg_skeleton_get_bone_count (LrgSkeleton *self)
{
    LrgSkeletonPrivate *priv;

    g_return_val_if_fail (LRG_IS_SKELETON (self), 0);

    priv = lrg_skeleton_get_instance_private (self);

    return g_list_length (priv->bones);
}

GList *
lrg_skeleton_get_bones (LrgSkeleton *self)
{
    LrgSkeletonPrivate *priv;

    g_return_val_if_fail (LRG_IS_SKELETON (self), NULL);

    priv = lrg_skeleton_get_instance_private (self);

    return priv->bones;
}

GList *
lrg_skeleton_get_root_bones (LrgSkeleton *self)
{
    LrgSkeletonPrivate *priv;
    GList *roots;
    GList *l;

    g_return_val_if_fail (LRG_IS_SKELETON (self), NULL);

    priv = lrg_skeleton_get_instance_private (self);
    roots = NULL;

    for (l = priv->bones; l != NULL; l = l->next)
    {
        LrgBone *bone = LRG_BONE (l->data);

        if (lrg_bone_is_root (bone))
            roots = g_list_append (roots, bone);
    }

    return roots;
}

GList *
lrg_skeleton_get_children (LrgSkeleton *self,
                           LrgBone     *bone)
{
    LrgSkeletonPrivate *priv;
    GList *children;
    GList *l;
    gint parent_index;

    g_return_val_if_fail (LRG_IS_SKELETON (self), NULL);
    g_return_val_if_fail (LRG_IS_BONE (bone), NULL);

    priv = lrg_skeleton_get_instance_private (self);
    children = NULL;
    parent_index = lrg_bone_get_index (bone);

    for (l = priv->bones; l != NULL; l = l->next)
    {
        LrgBone *child = LRG_BONE (l->data);

        if (lrg_bone_get_parent_index (child) == parent_index)
            children = g_list_append (children, child);
    }

    return children;
}

void
lrg_skeleton_calculate_world_poses (LrgSkeleton *self)
{
    LrgSkeletonClass *klass;

    g_return_if_fail (LRG_IS_SKELETON (self));

    klass = LRG_SKELETON_GET_CLASS (self);

    if (klass->calculate_world_poses != NULL)
        klass->calculate_world_poses (self);
}

void
lrg_skeleton_update (LrgSkeleton *self,
                     gfloat       delta_time)
{
    LrgSkeletonClass *klass;

    g_return_if_fail (LRG_IS_SKELETON (self));

    klass = LRG_SKELETON_GET_CLASS (self);

    if (klass->update != NULL)
        klass->update (self, delta_time);
}

void
lrg_skeleton_reset_to_bind (LrgSkeleton *self)
{
    LrgSkeletonPrivate *priv;
    GList *l;

    g_return_if_fail (LRG_IS_SKELETON (self));

    priv = lrg_skeleton_get_instance_private (self);

    for (l = priv->bones; l != NULL; l = l->next)
    {
        LrgBone *bone = LRG_BONE (l->data);
        lrg_bone_reset_to_bind (bone);
    }

    lrg_skeleton_calculate_world_poses (self);
}

void
lrg_skeleton_set_pose (LrgSkeleton       *self,
                       gint               bone_index,
                       const LrgBonePose *pose)
{
    LrgBone *bone;

    g_return_if_fail (LRG_IS_SKELETON (self));
    g_return_if_fail (pose != NULL);

    bone = lrg_skeleton_get_bone (self, bone_index);
    if (bone != NULL)
        lrg_bone_set_local_pose (bone, pose);
}

void
lrg_skeleton_blend_pose (LrgSkeleton       *self,
                         gint               bone_index,
                         const LrgBonePose *pose,
                         gfloat             weight)
{
    LrgBone *bone;
    const LrgBonePose *current;
    g_autoptr(LrgBonePose) blended = NULL;

    g_return_if_fail (LRG_IS_SKELETON (self));
    g_return_if_fail (pose != NULL);

    bone = lrg_skeleton_get_bone (self, bone_index);
    if (bone == NULL)
        return;

    current = lrg_bone_get_local_pose (bone);
    blended = lrg_bone_pose_lerp (current, pose, weight);
    lrg_bone_set_local_pose (bone, blended);
}

const gchar *
lrg_skeleton_get_name (LrgSkeleton *self)
{
    LrgSkeletonPrivate *priv;

    g_return_val_if_fail (LRG_IS_SKELETON (self), NULL);

    priv = lrg_skeleton_get_instance_private (self);

    return priv->name;
}

void
lrg_skeleton_set_name (LrgSkeleton *self,
                       const gchar *name)
{
    LrgSkeletonPrivate *priv;

    g_return_if_fail (LRG_IS_SKELETON (self));

    priv = lrg_skeleton_get_instance_private (self);

    if (g_strcmp0 (priv->name, name) == 0)
        return;

    g_free (priv->name);
    priv->name = g_strdup (name);

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_NAME]);
}

LrgSkeleton *
lrg_skeleton_copy (LrgSkeleton *self)
{
    LrgSkeletonPrivate *priv;
    LrgSkeleton *copy;
    GList *l;

    g_return_val_if_fail (LRG_IS_SKELETON (self), NULL);

    priv = lrg_skeleton_get_instance_private (self);
    copy = lrg_skeleton_new ();

    lrg_skeleton_set_name (copy, priv->name);

    for (l = priv->bones; l != NULL; l = l->next)
    {
        LrgBone *bone = LRG_BONE (l->data);
        LrgBone *bone_copy;

        bone_copy = lrg_bone_new (lrg_bone_get_name (bone),
                                  lrg_bone_get_index (bone));
        lrg_bone_set_parent_index (bone_copy, lrg_bone_get_parent_index (bone));
        lrg_bone_set_length (bone_copy, lrg_bone_get_length (bone));
        lrg_bone_set_bind_pose (bone_copy, lrg_bone_get_bind_pose (bone));
        lrg_bone_set_local_pose (bone_copy, lrg_bone_get_local_pose (bone));

        lrg_skeleton_add_bone (copy, bone_copy);
        g_object_unref (bone_copy);
    }

    lrg_skeleton_calculate_world_poses (copy);

    return copy;
}
