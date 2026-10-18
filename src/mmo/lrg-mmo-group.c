/* SPDX-License-Identifier: AGPL-3.0-or-later */
/*
 * lrg-mmo-group.c - server-side group membership authority
 *
 * A group is a sorted member list with a leader, a capacity, optional
 * subgroups (raid groups) and pending invitations with a deadline.  Every
 * mutation names the authenticated actor; only the leader admits,
 * invites, removes others, promotes, resizes and moves.  An empty group
 * is closed: it refuses everything.
 */
#include "lrg-mmo-group.h"

struct _LrgMmoGroup
{
    GObject parent_instance;
    GArray *members;          /* guint64, ascending */
    guint capacity;
    guint64 leader;
    guint subgroup_size;      /* 0: no subgroups */
    GHashTable *subgroups;    /* guint64* member -> subgroup (from 1) */
    GHashTable *invites;      /* guint64* invitee -> gint64* deadline */
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
    LrgMmoGroup *self = LRG_MMO_GROUP (object);

    g_array_unref (self->members);
    g_hash_table_unref (self->subgroups);
    g_hash_table_unref (self->invites);
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
    self->subgroups = g_hash_table_new_full (g_int64_hash, g_int64_equal, g_free, NULL);
    self->invites = g_hash_table_new_full (g_int64_hash, g_int64_equal, g_free, g_free);
}

static gboolean
is_member (LrgMmoGroup *self,
           guint64      member)
{
    guint i;

    for (i = 0; i < self->members->len; i++)
        if (g_array_index (self->members, guint64, i) == member)
            return TRUE;
    return FALSE;
}

static gboolean
require_leader (LrgMmoGroup  *self,
                guint64       actor,
                const gchar  *what,
                GError      **error)
{
    if (actor == 0 || self->leader == 0 || actor != self->leader)
    {
        g_set_error (error, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED,
                     "Only the leader may %s", what);
        return FALSE;
    }
    return TRUE;
}

/* subgroups: how many there are for the current capacity */
static guint
subgroup_count (LrgMmoGroup *self)
{
    if (self->subgroup_size == 0)
        return 0;
    return (self->capacity + self->subgroup_size - 1) / self->subgroup_size;
}

static guint
subgroup_fill (LrgMmoGroup *self,
               guint        subgroup)
{
    GHashTableIter iter;
    gpointer value;
    guint n = 0;

    g_hash_table_iter_init (&iter, self->subgroups);
    while (g_hash_table_iter_next (&iter, NULL, &value))
        if (GPOINTER_TO_UINT (value) == subgroup)
            n++;
    return n;
}

static void
set_subgroup (LrgMmoGroup *self,
              guint64      member,
              guint        subgroup)
{
    guint64 *key = g_new (guint64, 1);

    *key = member;
    g_hash_table_replace (self->subgroups, key, GUINT_TO_POINTER (subgroup));
}

/* puts a member without a subgroup into the lowest one with room */
static void
place_member (LrgMmoGroup *self,
              guint64      member)
{
    guint count = subgroup_count (self);
    guint i;

    for (i = 1; i <= count; i++)
        if (subgroup_fill (self, i) < self->subgroup_size)
        {
            set_subgroup (self, member, i);
            return;
        }
    /* capacity always leaves room: count * size >= capacity >= members */
    g_warn_if_reached ();
}

/* adds without the leader check (admission is checked by the callers) */
static void
insert_member (LrgMmoGroup *self,
               guint64      member)
{
    g_array_append_val (self->members, member);
    g_array_sort (self->members, compare_ids);
    if (self->subgroup_size > 0)
        place_member (self, member);
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
    g_return_val_if_fail (LRG_IS_MMO_GROUP (self), FALSE);
    if (actor == 0 || actor != self->leader || member == 0)
    {
        g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED,
                             "Only the leader may add a nonzero member");
        return FALSE;
    }
    if (is_member (self, member))
    {
        g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_EXISTS, "Already a member");
        return FALSE;
    }
    /* a pending invitation of someone else keeps its seat */
    if (self->members->len + g_hash_table_size (self->invites) -
        (g_hash_table_contains (self->invites, &member) ? 1 : 0) >= self->capacity)
    {
        g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_NO_SPACE, "Group is full");
        return FALSE;
    }
    g_hash_table_remove (self->invites, &member);
    insert_member (self, member);
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
            g_hash_table_remove (self->subgroups, &member);
            if (member == self->leader)
                self->leader = self->members->len > 0 ? g_array_index (self->members, guint64, 0) : 0;
            /* a closed group holds no invitations */
            if (self->leader == 0)
                g_hash_table_remove_all (self->invites);
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

gboolean
lrg_mmo_group_promote (LrgMmoGroup  *self,
                       guint64       actor,
                       guint64       member,
                       GError      **error)
{
    g_return_val_if_fail (LRG_IS_MMO_GROUP (self), FALSE);
    if (!require_leader (self, actor, "promote", error))
        return FALSE;
    if (!is_member (self, member))
    {
        g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "Member not found");
        return FALSE;
    }
    self->leader = member;
    return TRUE;
}

gboolean
lrg_mmo_group_has_member (LrgMmoGroup *self,
                          guint64      member)
{
    g_return_val_if_fail (LRG_IS_MMO_GROUP (self), FALSE);
    return member != 0 && is_member (self, member);
}

guint
lrg_mmo_group_get_size (LrgMmoGroup *self)
{
    g_return_val_if_fail (LRG_IS_MMO_GROUP (self), 0);
    return self->members->len;
}

guint
lrg_mmo_group_get_capacity (LrgMmoGroup *self)
{
    g_return_val_if_fail (LRG_IS_MMO_GROUP (self), 0);
    return self->capacity;
}

gboolean
lrg_mmo_group_set_capacity (LrgMmoGroup  *self,
                            guint64       actor,
                            guint         capacity,
                            GError      **error)
{
    GHashTableIter iter;
    gpointer value;
    guint count;
    guint i;

    g_return_val_if_fail (LRG_IS_MMO_GROUP (self), FALSE);
    if (!require_leader (self, actor, "resize the group", error))
        return FALSE;
    if (capacity == 0 || capacity < self->members->len + g_hash_table_size (self->invites))
    {
        g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_NO_SPACE,
                             "The members and invitations do not fit");
        return FALSE;
    }
    self->capacity = capacity;
    if (self->subgroup_size == 0)
        return TRUE;

    /* a smaller group has fewer subgroups: their members move down */
    count = subgroup_count (self);
    g_hash_table_iter_init (&iter, self->subgroups);
    while (g_hash_table_iter_next (&iter, NULL, &value))
        if (GPOINTER_TO_UINT (value) > count)
            g_hash_table_iter_remove (&iter);
    for (i = 0; i < self->members->len; i++)
    {
        guint64 member = g_array_index (self->members, guint64, i);

        if (!g_hash_table_contains (self->subgroups, &member))
            place_member (self, member);
    }
    return TRUE;
}

gboolean
lrg_mmo_group_set_subgroup_size (LrgMmoGroup  *self,
                                 guint64       actor,
                                 guint         size,
                                 GError      **error)
{
    g_autoptr(GHashTable) old = NULL;
    guint i;

    g_return_val_if_fail (LRG_IS_MMO_GROUP (self), FALSE);
    if (!require_leader (self, actor, "change subgroups", error))
        return FALSE;
    old = self->subgroups;
    self->subgroups = g_hash_table_new_full (g_int64_hash, g_int64_equal, g_free, NULL);
    self->subgroup_size = size;
    if (size == 0)
        return TRUE;

    /* keep the subgroups that still exist and have room, in ID order */
    for (i = 0; i < self->members->len; i++)
    {
        guint64 member = g_array_index (self->members, guint64, i);
        guint subgroup = GPOINTER_TO_UINT (g_hash_table_lookup (old, &member));

        if (subgroup >= 1 && subgroup <= subgroup_count (self) &&
            subgroup_fill (self, subgroup) < size)
            set_subgroup (self, member, subgroup);
    }
    for (i = 0; i < self->members->len; i++)
    {
        guint64 member = g_array_index (self->members, guint64, i);

        if (!g_hash_table_contains (self->subgroups, &member))
            place_member (self, member);
    }
    return TRUE;
}

guint
lrg_mmo_group_get_subgroup_size (LrgMmoGroup *self)
{
    g_return_val_if_fail (LRG_IS_MMO_GROUP (self), 0);
    return self->subgroup_size;
}

guint
lrg_mmo_group_get_subgroup_count (LrgMmoGroup *self)
{
    g_return_val_if_fail (LRG_IS_MMO_GROUP (self), 0);
    return subgroup_count (self);
}

guint
lrg_mmo_group_get_subgroup (LrgMmoGroup *self,
                            guint64      member)
{
    g_return_val_if_fail (LRG_IS_MMO_GROUP (self), 0);
    return GPOINTER_TO_UINT (g_hash_table_lookup (self->subgroups, &member));
}

gboolean
lrg_mmo_group_move (LrgMmoGroup  *self,
                    guint64       actor,
                    guint64       member,
                    guint         subgroup,
                    GError      **error)
{
    g_return_val_if_fail (LRG_IS_MMO_GROUP (self), FALSE);
    if (!require_leader (self, actor, "move members", error))
        return FALSE;
    if (!is_member (self, member))
    {
        g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "Member not found");
        return FALSE;
    }
    if (subgroup == 0 || subgroup > subgroup_count (self))
    {
        g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "No such subgroup");
        return FALSE;
    }
    if (lrg_mmo_group_get_subgroup (self, member) == subgroup)
        return TRUE;
    if (subgroup_fill (self, subgroup) >= self->subgroup_size)
    {
        g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_NO_SPACE, "Subgroup is full");
        return FALSE;
    }
    set_subgroup (self, member, subgroup);
    return TRUE;
}

GArray *
lrg_mmo_group_get_subgroup_members (LrgMmoGroup *self,
                                    guint        subgroup)
{
    GArray *out;
    guint i;

    g_return_val_if_fail (LRG_IS_MMO_GROUP (self), NULL);
    out = g_array_new (FALSE, FALSE, sizeof (guint64));
    for (i = 0; subgroup > 0 && i < self->members->len; i++)
    {
        guint64 member = g_array_index (self->members, guint64, i);

        if (lrg_mmo_group_get_subgroup (self, member) == subgroup)
            g_array_append_val (out, member);
    }
    return out;
}

gboolean
lrg_mmo_group_invite (LrgMmoGroup  *self,
                      guint64       actor,
                      guint64       invitee,
                      gint64        expires_at,
                      GError      **error)
{
    guint64 *key;
    gint64 *deadline;

    g_return_val_if_fail (LRG_IS_MMO_GROUP (self), FALSE);
    if (!require_leader (self, actor, "invite", error))
        return FALSE;
    if (invitee == 0)
    {
        g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "Nobody to invite");
        return FALSE;
    }
    if (is_member (self, invitee) || g_hash_table_contains (self->invites, &invitee))
    {
        g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_EXISTS, "Already a member or invited");
        return FALSE;
    }
    if (self->members->len + g_hash_table_size (self->invites) >= self->capacity)
    {
        g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_NO_SPACE, "Group is full");
        return FALSE;
    }
    key = g_new (guint64, 1);
    *key = invitee;
    deadline = g_new (gint64, 1);
    *deadline = expires_at;
    g_hash_table_replace (self->invites, key, deadline);
    return TRUE;
}

gboolean
lrg_mmo_group_accept (LrgMmoGroup  *self,
                      guint64       invitee,
                      gint64        now,
                      GError      **error)
{
    gint64 *deadline;

    g_return_val_if_fail (LRG_IS_MMO_GROUP (self), FALSE);
    deadline = g_hash_table_lookup (self->invites, &invitee);
    if (deadline == NULL || self->leader == 0)
    {
        g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "No invitation");
        return FALSE;
    }
    if (now >= *deadline)
    {
        g_hash_table_remove (self->invites, &invitee);
        g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_TIMED_OUT, "The invitation lapsed");
        return FALSE;
    }
    /* the invitation held a seat: there is always room */
    g_hash_table_remove (self->invites, &invitee);
    insert_member (self, invitee);
    return TRUE;
}

gboolean
lrg_mmo_group_decline (LrgMmoGroup *self,
                       guint64      invitee)
{
    g_return_val_if_fail (LRG_IS_MMO_GROUP (self), FALSE);
    return g_hash_table_remove (self->invites, &invitee);
}

gboolean
lrg_mmo_group_has_invite (LrgMmoGroup *self,
                          guint64      invitee,
                          gint64       now)
{
    gint64 *deadline;

    g_return_val_if_fail (LRG_IS_MMO_GROUP (self), FALSE);
    deadline = g_hash_table_lookup (self->invites, &invitee);
    return deadline != NULL && now < *deadline;
}

GArray *
lrg_mmo_group_expire_invites (LrgMmoGroup *self,
                              gint64       now)
{
    GHashTableIter iter;
    gpointer key;
    gpointer value;
    GArray *out;

    g_return_val_if_fail (LRG_IS_MMO_GROUP (self), NULL);
    out = g_array_new (FALSE, FALSE, sizeof (guint64));
    g_hash_table_iter_init (&iter, self->invites);
    while (g_hash_table_iter_next (&iter, &key, &value))
        if (now >= *(gint64 *) value)
        {
            g_array_append_val (out, *(guint64 *) key);
            g_hash_table_iter_remove (&iter);
        }
    g_array_sort (out, compare_ids);
    return out;
}
