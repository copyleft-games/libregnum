/* lrg-bone.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "config.h"
#include "lrg-bone.h"

/**
 * SECTION:lrg-bone
 * @Title: LrgBone
 * @Short_description: Individual bone in a skeleton
 *
 * #LrgBone represents a single bone in a #LrgSkeleton hierarchy.
 * Each bone has a name, index, parent reference, and three poses:
 *
 * - **Bind pose**: The default rest pose
 * - **Local pose**: Current pose relative to parent
 * - **World pose**: Accumulated world-space transformation
 *
 * Bones are organized in a parent-child hierarchy where the root bone(s)
 * have no parent (parent_index = -1).
 */

struct _LrgBone
{
    GObject parent_instance;

    gchar       *name;
    gint         index;
    gint         parent_index;
    gfloat       length;

    LrgBonePose  bind_pose;
    LrgBonePose  local_pose;
    LrgBonePose  world_pose;
};

G_DEFINE_FINAL_TYPE (LrgBone, lrg_bone, G_TYPE_OBJECT)

enum
{
    PROP_0,
    PROP_NAME,
    PROP_INDEX,
    PROP_PARENT_INDEX,
    PROP_LENGTH,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

static void
lrg_bone_finalize (GObject *object)
{
    LrgBone *self = LRG_BONE (object);

    g_clear_pointer (&self->name, g_free);

    G_OBJECT_CLASS (lrg_bone_parent_class)->finalize (object);
}

static void
lrg_bone_get_property (GObject    *object,
                       guint       prop_id,
                       GValue     *value,
                       GParamSpec *pspec)
{
    LrgBone *self = LRG_BONE (object);

    switch (prop_id)
    {
    case PROP_NAME:
        g_value_set_string (value, self->name);
        break;
    case PROP_INDEX:
        g_value_set_int (value, self->index);
        break;
    case PROP_PARENT_INDEX:
        g_value_set_int (value, self->parent_index);
        break;
    case PROP_LENGTH:
        g_value_set_float (value, self->length);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_bone_set_property (GObject      *object,
                       guint         prop_id,
                       const GValue *value,
                       GParamSpec   *pspec)
{
    LrgBone *self = LRG_BONE (object);

    switch (prop_id)
    {
    case PROP_NAME:
        g_free (self->name);
        self->name = g_value_dup_string (value);
        break;
    case PROP_INDEX:
        self->index = g_value_get_int (value);
        break;
    case PROP_PARENT_INDEX:
        self->parent_index = g_value_get_int (value);
        break;
    case PROP_LENGTH:
        self->length = g_value_get_float (value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_bone_class_init (LrgBoneClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = lrg_bone_finalize;
    object_class->get_property = lrg_bone_get_property;
    object_class->set_property = lrg_bone_set_property;

    /**
     * LrgBone:name:
     *
     * The bone name.
     */
    properties[PROP_NAME] =
        g_param_spec_string ("name",
                             "Name",
                             "Bone name",
                             NULL,
                             G_PARAM_READWRITE |
                             G_PARAM_CONSTRUCT_ONLY |
                             G_PARAM_STATIC_STRINGS);

    /**
     * LrgBone:index:
     *
     * The bone index in the skeleton.
     */
    properties[PROP_INDEX] =
        g_param_spec_int ("index",
                          "Index",
                          "Bone index in skeleton",
                          -1, G_MAXINT, 0,
                          G_PARAM_READWRITE |
                          G_PARAM_CONSTRUCT_ONLY |
                          G_PARAM_STATIC_STRINGS);

    /**
     * LrgBone:parent-index:
     *
     * The parent bone index (-1 for root).
     */
    properties[PROP_PARENT_INDEX] =
        g_param_spec_int ("parent-index",
                          "Parent Index",
                          "Parent bone index (-1 for root)",
                          -1, G_MAXINT, -1,
                          G_PARAM_READWRITE |
                          G_PARAM_STATIC_STRINGS |
                          G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgBone:length:
     *
     * The bone length.
     */
    properties[PROP_LENGTH] =
        g_param_spec_float ("length",
                            "Length",
                            "Bone length",
                            0.0f, G_MAXFLOAT, 1.0f,
                            G_PARAM_READWRITE |
                            G_PARAM_STATIC_STRINGS |
                            G_PARAM_EXPLICIT_NOTIFY);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_bone_init (LrgBone *self)
{
    self->name = NULL;
    self->index = 0;
    self->parent_index = -1;
    self->length = 1.0f;

    /* Initialize poses to identity */
    lrg_bone_pose_set_identity (&self->bind_pose);
    lrg_bone_pose_set_identity (&self->local_pose);
    lrg_bone_pose_set_identity (&self->world_pose);
}

LrgBone *
lrg_bone_new (const gchar *name,
              gint         index)
{
    return g_object_new (LRG_TYPE_BONE,
                         "name", name,
                         "index", index,
                         NULL);
}

const gchar *
lrg_bone_get_name (LrgBone *self)
{
    g_return_val_if_fail (LRG_IS_BONE (self), NULL);

    return self->name;
}

gint
lrg_bone_get_index (LrgBone *self)
{
    g_return_val_if_fail (LRG_IS_BONE (self), -1);

    return self->index;
}

gint
lrg_bone_get_parent_index (LrgBone *self)
{
    g_return_val_if_fail (LRG_IS_BONE (self), -1);

    return self->parent_index;
}

void
lrg_bone_set_parent_index (LrgBone *self,
                           gint     parent_index)
{
    g_return_if_fail (LRG_IS_BONE (self));

    if (self->parent_index == parent_index)
        return;

    self->parent_index = parent_index;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_PARENT_INDEX]);
}

gboolean
lrg_bone_is_root (LrgBone *self)
{
    g_return_val_if_fail (LRG_IS_BONE (self), FALSE);

    return self->parent_index < 0;
}

const LrgBonePose *
lrg_bone_get_bind_pose (LrgBone *self)
{
    g_return_val_if_fail (LRG_IS_BONE (self), NULL);

    return &self->bind_pose;
}

void
lrg_bone_set_bind_pose (LrgBone           *self,
                        const LrgBonePose *pose)
{
    g_return_if_fail (LRG_IS_BONE (self));
    g_return_if_fail (pose != NULL);

    self->bind_pose = *pose;
}

const LrgBonePose *
lrg_bone_get_local_pose (LrgBone *self)
{
    g_return_val_if_fail (LRG_IS_BONE (self), NULL);

    return &self->local_pose;
}

void
lrg_bone_set_local_pose (LrgBone           *self,
                         const LrgBonePose *pose)
{
    g_return_if_fail (LRG_IS_BONE (self));
    g_return_if_fail (pose != NULL);

    self->local_pose = *pose;
}

const LrgBonePose *
lrg_bone_get_world_pose (LrgBone *self)
{
    g_return_val_if_fail (LRG_IS_BONE (self), NULL);

    return &self->world_pose;
}

void
lrg_bone_set_world_pose (LrgBone           *self,
                         const LrgBonePose *pose)
{
    g_return_if_fail (LRG_IS_BONE (self));
    g_return_if_fail (pose != NULL);

    self->world_pose = *pose;
}

void
lrg_bone_reset_to_bind (LrgBone *self)
{
    g_return_if_fail (LRG_IS_BONE (self));

    self->local_pose = self->bind_pose;
}

gfloat
lrg_bone_get_length (LrgBone *self)
{
    g_return_val_if_fail (LRG_IS_BONE (self), 0.0f);

    return self->length;
}

void
lrg_bone_set_length (LrgBone *self,
                     gfloat   length)
{
    g_return_if_fail (LRG_IS_BONE (self));

    if (self->length == length)
        return;

    self->length = length;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_LENGTH]);
}
