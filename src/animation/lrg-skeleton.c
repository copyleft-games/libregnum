/* lrg-skeleton.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "config.h"
#include "lrg-skeleton.h"
#include "lrg-skeleton-private.h"
#include "lrg-bone-private.h"
#include "lrg-bone-pose-private.h"

#include <string.h>

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

/*
 * Pose order entry: one bone of the parent-before-child order the world
 * pose pass walks, with its parent resolved. kind says what the legacy
 * multi-pass algorithm would have done with the bone: copy the local pose
 * (root), leave the world pose alone (parent index names no bone), or
 * compose it onto the parent's world pose.
 */
typedef enum
{
    POSE_ENTRY_ROOT,
    POSE_ENTRY_DANGLING,
    POSE_ENTRY_CHILD
} PoseEntryKind;

typedef struct
{
    LrgBone       *bone;
    LrgBone       *parent;
    PoseEntryKind  kind;
} PoseEntry;

/*
 * Bones whose index lies in [0, n * DENSE_INDEX_FACTOR + DENSE_INDEX_SLACK)
 * are found through a flat array; anything else (negative or very sparse
 * indices) goes through a hash table.
 */
#define DENSE_INDEX_FACTOR (4)
#define DENSE_INDEX_SLACK  (64)

typedef struct
{
    gchar  *name;
    GList  *bones;         /* List of LrgBone */
    GHashTable *bone_map;  /* name -> LrgBone */
    guint   n_bones;       /* g_list_length (bones), kept up to date */

    /*
     * Lookup caches, rebuilt lazily after the bone list changes.
     *
     * by_position:   the bones in list order (borrowed).
     * dense_index:   bone index -> list position, -1 when no bone has it;
     *                the first bone in list order wins, as the list walk
     *                in lrg_skeleton_get_bone() always did.
     * sparse_index:  same for indices outside the dense range (may be NULL).
     * cached_parent: each bone's parent index when pose_order was built;
     *                a mismatch means a bone was reparented since.
     * pose_order:    parent-before-child walk for calculate_world_poses.
     * cyclic:        some parent chain loops; fall back to the legacy
     *                fixed-iteration algorithm, which is defined (if
     *                meaningless) for that case.
     */
    GPtrArray  *by_position;
    GArray     *dense_index;    /* gint */
    GHashTable *sparse_index;   /* GINT_TO_POINTER(index) -> GINT_TO_POINTER(pos + 1) */
    GArray     *cached_parent;  /* gint */
    GArray     *pose_order;     /* PoseEntry */
    gboolean    index_valid;
    gboolean    order_valid;
    gboolean    cyclic;
    guint       generation;
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

/*
 * skeleton_invalidate:
 *
 * Drops the lookup caches after the bone list changed and bumps the
 * generation that LrgAnimator keys its track bindings on.
 */
static void
skeleton_invalidate (LrgSkeletonPrivate *priv)
{
    priv->index_valid = FALSE;
    priv->order_valid = FALSE;
    priv->generation++;
}

/*
 * skeleton_ensure_index:
 *
 * Rebuilds the bone index -> list position maps if the bone list changed.
 * Bone indices are construct-only, so only add/remove invalidate them.
 */
static void
skeleton_ensure_index (LrgSkeletonPrivate *priv)
{
    GList *l;
    guint  pos;
    gint   max_index;
    guint  dense_len;

    if (priv->index_valid)
        return;

    g_ptr_array_set_size (priv->by_position, 0);
    g_clear_pointer (&priv->sparse_index, g_hash_table_unref);

    /* Size the dense table from the largest non-negative index. */
    max_index = -1;
    for (l = priv->bones; l != NULL; l = l->next)
    {
        LrgBone *bone = l->data;

        g_ptr_array_add (priv->by_position, bone);
        if (bone->index > max_index)
            max_index = bone->index;
    }
    dense_len = (guint) max_index + 1;
    if (dense_len > priv->n_bones * DENSE_INDEX_FACTOR + DENSE_INDEX_SLACK)
        dense_len = priv->n_bones * DENSE_INDEX_FACTOR + DENSE_INDEX_SLACK;

    g_array_set_size (priv->dense_index, dense_len);
    memset (priv->dense_index->data, 0xff, dense_len * sizeof (gint));

    /* First bone in list order wins a shared index. */
    for (pos = 0; pos < priv->by_position->len; pos++)
    {
        LrgBone *bone = g_ptr_array_index (priv->by_position, pos);

        if (bone->index >= 0 && (guint) bone->index < dense_len)
        {
            gint *slot = &g_array_index (priv->dense_index, gint, bone->index);

            if (*slot < 0)
                *slot = (gint) pos;
        }
        else
        {
            if (priv->sparse_index == NULL)
                priv->sparse_index = g_hash_table_new (g_direct_hash, g_direct_equal);
            if (!g_hash_table_contains (priv->sparse_index,
                                        GINT_TO_POINTER (bone->index)))
                g_hash_table_insert (priv->sparse_index,
                                     GINT_TO_POINTER (bone->index),
                                     GUINT_TO_POINTER (pos + 1));
        }
    }

    priv->index_valid = TRUE;
}

/*
 * skeleton_position_of_index:
 *
 * Returns the list position of the first bone with @index, or -1.
 */
static gint
skeleton_position_of_index (LrgSkeletonPrivate *priv,
                            gint                index)
{
    gpointer value;

    skeleton_ensure_index (priv);

    if (index >= 0 && (guint) index < priv->dense_index->len)
        return g_array_index (priv->dense_index, gint, index);
    if (priv->sparse_index == NULL)
        return -1;
    value = g_hash_table_lookup (priv->sparse_index, GINT_TO_POINTER (index));
    if (value == NULL)
        return -1;
    return (gint) GPOINTER_TO_UINT (value) - 1;
}

static LrgBone *
skeleton_lookup (LrgSkeletonPrivate *priv,
                 gint                index)
{
    gint pos;

    pos = skeleton_position_of_index (priv, index);
    if (pos < 0)
        return NULL;
    return g_ptr_array_index (priv->by_position, pos);
}

/*
 * skeleton_ensure_order:
 *
 * Makes pose_order a parent-before-child walk of the current hierarchy.
 * Parent indices are mutable (lrg_bone_set_parent_index() or the
 * property), so every call compares them with the snapshot the order was
 * built from: O(n), against the O(n^2) per pass of looking parents up in
 * the list.
 *
 * Each bone's parent is the bone lrg_skeleton_get_bone() would return for
 * its parent index. Bones are sorted by depth (roots and bones with a
 * missing parent are depth 0), which is enough: a bone only depends on
 * its parent. A parent chain that loops marks the skeleton cyclic.
 */
static void
skeleton_ensure_order (LrgSkeletonPrivate *priv)
{
    guint  n;
    guint  pos;
    gint  *parent_pos;
    gint  *depth;
    guint *stack;
    guint *count;
    guint  max_depth;

    skeleton_ensure_index (priv);
    n = priv->by_position->len;

    if (priv->order_valid)
    {
        gboolean same = TRUE;

        for (pos = 0; pos < n; pos++)
        {
            LrgBone *bone = g_ptr_array_index (priv->by_position, pos);

            if (bone->parent_index != g_array_index (priv->cached_parent, gint, pos))
            {
                same = FALSE;
                break;
            }
        }
        if (same)
            return;
    }

    g_array_set_size (priv->cached_parent, n);
    g_array_set_size (priv->pose_order, 0);
    priv->cyclic = FALSE;

    parent_pos = g_new (gint, n + 1);
    depth = g_new (gint, n + 1);
    stack = g_new (guint, n + 1);
    count = g_new0 (guint, n + 2);

    /* Resolve parents; -1 = root or dangling (no world pose dependency). */
    for (pos = 0; pos < n; pos++)
    {
        LrgBone *bone = g_ptr_array_index (priv->by_position, pos);

        g_array_index (priv->cached_parent, gint, pos) = bone->parent_index;
        parent_pos[pos] = bone->parent_index < 0
            ? -1 : skeleton_position_of_index (priv, bone->parent_index);
        depth[pos] = -1;
    }

    /* Depth of every bone: walk up to a known depth, then unwind. A walk
     * that meets itself (depth -2 = on the current path) is a cycle. */
    max_depth = 0;
    for (pos = 0; pos < n && !priv->cyclic; pos++)
    {
        guint top = 0;
        guint cur = pos;
        gint  d;

        while (depth[cur] == -1)
        {
            depth[cur] = -2;
            stack[top++] = cur;
            if (parent_pos[cur] < 0)
                break;
            cur = (guint) parent_pos[cur];
        }
        if (depth[cur] == -2 && parent_pos[cur] >= 0)
        {
            priv->cyclic = TRUE;
            break;
        }

        /* cur is either a resolved bone or the root/dangling end of the
         * path (still on the stack, depth -2). */
        d = depth[cur] >= 0 ? depth[cur] : -1;
        while (top > 0)
        {
            guint b = stack[--top];

            d++;
            depth[b] = d;
            if ((guint) d > max_depth)
                max_depth = (guint) d;
        }
    }

    if (!priv->cyclic)
    {
        guint d;
        guint *start;

        /* Counting sort by depth, stable in list order. */
        for (pos = 0; pos < n; pos++)
            count[depth[pos] + 1]++;
        start = count;
        for (d = 1; d <= max_depth + 1; d++)
            start[d] += start[d - 1];

        g_array_set_size (priv->pose_order, n);
        for (pos = 0; pos < n; pos++)
        {
            LrgBone   *bone = g_ptr_array_index (priv->by_position, pos);
            PoseEntry *entry;

            entry = &g_array_index (priv->pose_order, PoseEntry,
                                    start[depth[pos]]++);
            entry->bone = bone;
            if (bone->parent_index < 0)
            {
                entry->kind = POSE_ENTRY_ROOT;
                entry->parent = NULL;
            }
            else if (parent_pos[pos] < 0)
            {
                entry->kind = POSE_ENTRY_DANGLING;
                entry->parent = NULL;
            }
            else
            {
                entry->kind = POSE_ENTRY_CHILD;
                entry->parent = g_ptr_array_index (priv->by_position, parent_pos[pos]);
            }
        }
    }

    g_free (parent_pos);
    g_free (depth);
    g_free (stack);
    g_free (count);

    priv->order_valid = TRUE;
}

/*
 * skeleton_calculate_world_poses_iterative:
 *
 * The original algorithm, kept for hierarchies whose parent chains loop:
 * roots copy their local pose, then every child is composed onto its
 * parent, list order, for 100 passes. On an acyclic hierarchy the single
 * ordered pass below gives the same poses (it is this loop's fixed point).
 */
static void
skeleton_calculate_world_poses_iterative (LrgSkeletonPrivate *priv)
{
    GList *l;
    guint  pass;

    for (l = priv->bones; l != NULL; l = l->next)
    {
        LrgBone *bone = l->data;

        if (bone->parent_index < 0)
            bone->world_pose = bone->local_pose;
    }

    for (pass = 0; pass < 100; pass++)
    {
        for (l = priv->bones; l != NULL; l = l->next)
        {
            LrgBone *bone = l->data;
            LrgBone *parent;

            if (bone->parent_index < 0)
                continue;
            parent = skeleton_lookup (priv, bone->parent_index);
            if (parent == NULL)
                continue;
            lrg_bone_pose_compose_into (&parent->world_pose, &bone->local_pose,
                                        &bone->world_pose);
        }
    }
}

static void
lrg_skeleton_real_calculate_world_poses (LrgSkeleton *self)
{
    LrgSkeletonPrivate *priv = lrg_skeleton_get_instance_private (self);
    guint i;

    /*
     * One pass, parents before children: a root's world pose is its local
     * pose, a child's is its local pose composed onto its parent's world
     * pose, and a bone whose parent index names no bone keeps whatever
     * world pose it had (its children compose onto that). Before this the
     * bones were walked in list order up to 100 times (always 100: every
     * pass "changed" something), with a list walk per parent lookup and a
     * heap allocation per composition; hierarchies deeper than 100 bones
     * listed child-first never converged.
     */
    skeleton_ensure_order (priv);

    if (priv->cyclic)
    {
        skeleton_calculate_world_poses_iterative (priv);
        return;
    }

    for (i = 0; i < priv->pose_order->len; i++)
    {
        PoseEntry *entry = &g_array_index (priv->pose_order, PoseEntry, i);

        switch (entry->kind)
        {
        case POSE_ENTRY_ROOT:
            entry->bone->world_pose = entry->bone->local_pose;
            break;
        case POSE_ENTRY_CHILD:
            lrg_bone_pose_compose_into (&entry->parent->world_pose,
                                        &entry->bone->local_pose,
                                        &entry->bone->world_pose);
            break;
        case POSE_ENTRY_DANGLING:
        default:
            break;
        }
    }
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
    g_clear_pointer (&priv->by_position, g_ptr_array_unref);
    g_clear_pointer (&priv->dense_index, g_array_unref);
    g_clear_pointer (&priv->sparse_index, g_hash_table_unref);
    g_clear_pointer (&priv->cached_parent, g_array_unref);
    g_clear_pointer (&priv->pose_order, g_array_unref);

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
        g_value_set_uint (value, priv->n_bones);
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
    priv->n_bones = 0;
    priv->by_position = g_ptr_array_new ();
    priv->dense_index = g_array_new (FALSE, FALSE, sizeof (gint));
    priv->sparse_index = NULL;
    priv->cached_parent = g_array_new (FALSE, FALSE, sizeof (gint));
    priv->pose_order = g_array_new (FALSE, FALSE, sizeof (PoseEntry));
    priv->index_valid = FALSE;
    priv->order_valid = FALSE;
    priv->cyclic = FALSE;
    priv->generation = 0;
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
    priv->n_bones++;
    skeleton_invalidate (priv);

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
        priv->n_bones--;
        skeleton_invalidate (priv);
        g_object_unref (bone);

        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_BONE_COUNT]);
    }
}

LrgBone *
lrg_skeleton_get_bone (LrgSkeleton *self,
                       gint         index)
{
    LrgSkeletonPrivate *priv;

    g_return_val_if_fail (LRG_IS_SKELETON (self), NULL);

    priv = lrg_skeleton_get_instance_private (self);

    /* The first bone in list order with this index, through the cache. */
    return skeleton_lookup (priv, index);
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

    return priv->n_bones;
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
    LrgSkeletonPrivate *priv;
    LrgBone *bone;

    g_return_if_fail (LRG_IS_SKELETON (self));
    g_return_if_fail (pose != NULL);

    priv = lrg_skeleton_get_instance_private (self);
    bone = skeleton_lookup (priv, bone_index);
    if (bone != NULL)
        bone->local_pose = *pose;
}

void
lrg_skeleton_blend_pose (LrgSkeleton       *self,
                         gint               bone_index,
                         const LrgBonePose *pose,
                         gfloat             weight)
{
    LrgSkeletonPrivate *priv;
    LrgBone *bone;
    LrgBonePose blended;

    g_return_if_fail (LRG_IS_SKELETON (self));
    g_return_if_fail (pose != NULL);

    priv = lrg_skeleton_get_instance_private (self);
    bone = skeleton_lookup (priv, bone_index);
    if (bone == NULL)
        return;

    lrg_bone_pose_lerp_to (&bone->local_pose, pose, weight, &blended);
    bone->local_pose = blended;
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

guint
lrg_skeleton_get_generation (LrgSkeleton *self)
{
    LrgSkeletonPrivate *priv = lrg_skeleton_get_instance_private (self);

    return priv->generation;
}
