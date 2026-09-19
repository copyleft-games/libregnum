/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include "lrg-mmo-group.h"

struct _LrgMmoGroup
{
    GObject parent_instance;
    GArray *members;
    guint capacity;
    guint64 leader;
};

G_DEFINE_TYPE (LrgMmoGroup, lrg_mmo_group, G_TYPE_OBJECT)

static gint
compare_ids (gconstpointer a,
             gconstpointer b)
{
    guint64 x = *(const guint64 *) a;
    guint64 y = *(const guint64 *) b;
    return (x > y) - (x < y);
}

static void
lrg_mmo_group_finalize (GObject *object)
{
    g_array_unref (LRG_MMO_GROUP (object)->members);
    G_OBJECT_CLASS (lrg_mmo_group_parent_class)->finalize (object);
}

static void
lrg_mmo_group_class_init (LrgMmoGroupClass *klass)
{
    G_OBJECT_CLASS (klass)->finalize = lrg_mmo_group_finalize;
}

static void
lrg_mmo_group_init (LrgMmoGroup *self)
{
    self->members = g_array_new (FALSE, FALSE, sizeof (guint64));
}

LrgMmoGroup *
lrg_mmo_group_new (guint64 leader,
                   guint   capacity)
{
    LrgMmoGroup *self;
    g_return_val_if_fail (leader != 0 && capacity > 0, NULL);
    self = g_object_new (LRG_TYPE_MMO_GROUP, NULL);
    self->leader = leader;
    self->capacity = capacity;
    g_array_append_val (self->members, leader);
    return self;
}

gboolean
lrg_mmo_group_add (LrgMmoGroup  *self,
                   guint64       actor,
                   guint64       member,
                   GError      **error)
{
    guint i;
    g_return_val_if_fail (LRG_IS_MMO_GROUP (self), FALSE);
    if (actor == 0 || actor != self->leader || member == 0)
    {
        g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED,
                             "Only the leader may add a nonzero member");
        return FALSE;
    }
    for (i = 0; i < self->members->len; i++)
        if (g_array_index (self->members, guint64, i) == member)
        {
            g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_EXISTS, "Already a member");
            return FALSE;
        }
    if (self->members->len >= self->capacity)
    {
        g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_NO_SPACE, "Group is full");
        return FALSE;
    }
    g_array_append_val (self->members, member);
    g_array_sort (self->members, compare_ids);
    return TRUE;
}

gboolean
lrg_mmo_group_remove (LrgMmoGroup  *self,
                      guint64       actor,
                      guint64       member,
                      GError      **error)
{
    guint i;
    g_return_val_if_fail (LRG_IS_MMO_GROUP (self), FALSE);
    if (actor == 0 || (actor != member && actor != self->leader))
    {
        g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED,
                             "Only self-leave or leader removal is allowed");
        return FALSE;
    }
    for (i = 0; i < self->members->len; i++)
        if (g_array_index (self->members, guint64, i) == member)
        {
            g_array_remove_index (self->members, i);
            if (member == self->leader)
                self->leader = self->members->len > 0 ? g_array_index (self->members, guint64, 0) : 0;
            return TRUE;
        }
    g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "Member not found");
    return FALSE;
}

guint64
lrg_mmo_group_get_leader (LrgMmoGroup *self)
{
    g_return_val_if_fail (LRG_IS_MMO_GROUP (self), 0);
    return self->leader;
}

GArray *
lrg_mmo_group_get_members (LrgMmoGroup *self)
{
    GArray *copy;
    g_return_val_if_fail (LRG_IS_MMO_GROUP (self), NULL);
    copy = g_array_sized_new (FALSE, FALSE, sizeof (guint64), self->members->len);
    g_array_append_vals (copy, self->members->data, self->members->len);
    return copy;
}
