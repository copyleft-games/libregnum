/* SPDX-License-Identifier: AGPL-3.0-or-later */
#pragma once
#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif
#include <gio/gio.h>
#include "../lrg-version.h"
G_BEGIN_DECLS
#define LRG_TYPE_MMO_GROUP (lrg_mmo_group_get_type ())
LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgMmoGroup, lrg_mmo_group, LRG, MMO_GROUP, GObject)

/**
 * lrg_mmo_group_new:
 * @leader: nonzero stable character ID
 * @capacity: maximum members, including leader
 *
 * Creates a group with its initial leader as its first member.
 *
 * Returns: (transfer full): a group
 */
LRG_AVAILABLE_IN_ALL
LrgMmoGroup *
lrg_mmo_group_new (guint64 leader,
                   guint capacity);

/**
 * lrg_mmo_group_add:
 * @self: the instance
 * @actor: authenticated requesting character
 * @member: nonzero character to add
 * @error: (nullable): return location for an error
 *
 * Only the leader may add members, subject to capacity.
 *
 * Returns: whether added
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_mmo_group_add (LrgMmoGroup *self,
                   guint64 actor,
                   guint64 member,
                   GError **error);

/**
 * lrg_mmo_group_remove:
 * @self: the instance
 * @actor: authenticated requesting character
 * @member: character to remove
 * @error: (nullable): return location for an error
 *
 * Allows self-leave or leader removal. Leadership passes to the lowest remaining ID.
 *
 * Returns: whether removed
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_mmo_group_remove (LrgMmoGroup *self,
                      guint64 actor,
                      guint64 member,
                      GError **error);

/**
 * lrg_mmo_group_get_leader:
 * @self: the instance
 *
 * Gets the current leader. Empty groups are closed and cannot be refilled.
 *
 * Returns: leader ID, or zero when empty
 */
LRG_AVAILABLE_IN_ALL
guint64
lrg_mmo_group_get_leader (LrgMmoGroup *self);

/**
 * lrg_mmo_group_get_members:
 * @self: the instance
 *
 * Copies members in ascending ID order, suitable for party/chat recipient routing.
 *
 * Returns: (transfer full) (element-type guint64): member IDs
 */
LRG_AVAILABLE_IN_ALL
GArray *
lrg_mmo_group_get_members (LrgMmoGroup *self);

/**
 * lrg_mmo_group_promote:
 * @self: the instance
 * @actor: authenticated requesting character
 * @member: the member who becomes leader
 * @error: (nullable): return location for an error
 *
 * Only the leader may pass leadership, and only to a current member.
 * Promoting the leader is a successful no-op.
 *
 * Returns: whether @member now leads
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_mmo_group_promote (LrgMmoGroup *self,
                       guint64 actor,
                       guint64 member,
                       GError **error);

/**
 * lrg_mmo_group_has_member:
 * @self: the instance
 * @member: a character ID
 *
 * Returns: whether @member belongs to the group
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_mmo_group_has_member (LrgMmoGroup *self,
                          guint64 member);

/**
 * lrg_mmo_group_get_size:
 * @self: the instance
 *
 * Returns: the number of members (pending invitations not counted)
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_mmo_group_get_size (LrgMmoGroup *self);

/**
 * lrg_mmo_group_get_capacity:
 * @self: the instance
 *
 * Returns: the maximum number of members, including the leader
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_mmo_group_get_capacity (LrgMmoGroup *self);

/**
 * lrg_mmo_group_set_capacity:
 * @self: the instance
 * @actor: authenticated requesting character
 * @capacity: the new maximum, at least the members plus pending invitations
 * @error: (nullable): return location for an error
 *
 * Only the leader may resize the group (a party becoming a raid, and back).
 * A capacity below the current members and pending invitations fails with
 * %G_IO_ERROR_NO_SPACE and changes nothing.
 *
 * Returns: whether resized
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_mmo_group_set_capacity (LrgMmoGroup *self,
                            guint64 actor,
                            guint capacity,
                            GError **error);

/**
 * lrg_mmo_group_set_subgroup_size:
 * @self: the instance
 * @actor: authenticated requesting character
 * @size: members per subgroup, or 0 for no subgroups
 * @error: (nullable): return location for an error
 *
 * Splits the group into subgroups of @size (numbered from 1; there are
 * capacity / @size of them, rounded up).  Members keep a subgroup that
 * still exists and has room; the others fill the lowest subgroups with
 * room in ascending ID order.  Only the leader may change it.
 *
 * Returns: whether changed
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_mmo_group_set_subgroup_size (LrgMmoGroup *self,
                                 guint64 actor,
                                 guint size,
                                 GError **error);

/**
 * lrg_mmo_group_get_subgroup_size:
 * @self: the instance
 *
 * Returns: members per subgroup, 0 without subgroups
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_mmo_group_get_subgroup_size (LrgMmoGroup *self);

/**
 * lrg_mmo_group_get_subgroup_count:
 * @self: the instance
 *
 * Returns: the number of subgroups, 0 without subgroups
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_mmo_group_get_subgroup_count (LrgMmoGroup *self);

/**
 * lrg_mmo_group_get_subgroup:
 * @self: the instance
 * @member: a character ID
 *
 * Returns: the member's subgroup (from 1), 0 for a non-member or a group
 *   without subgroups
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_mmo_group_get_subgroup (LrgMmoGroup *self,
                            guint64 member);

/**
 * lrg_mmo_group_move:
 * @self: the instance
 * @actor: authenticated requesting character
 * @member: the member to move
 * @subgroup: the destination subgroup (from 1)
 * @error: (nullable): return location for an error
 *
 * Only the leader may move members.  A full destination fails with
 * %G_IO_ERROR_NO_SPACE, a missing subgroup with
 * %G_IO_ERROR_INVALID_ARGUMENT.
 *
 * Returns: whether moved
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_mmo_group_move (LrgMmoGroup *self,
                    guint64 actor,
                    guint64 member,
                    guint subgroup,
                    GError **error);

/**
 * lrg_mmo_group_get_subgroup_members:
 * @self: the instance
 * @subgroup: a subgroup (from 1)
 *
 * Returns: (transfer full) (element-type guint64): the subgroup's members
 *   in ascending ID order (empty for a missing subgroup)
 */
LRG_AVAILABLE_IN_ALL
GArray *
lrg_mmo_group_get_subgroup_members (LrgMmoGroup *self,
                                    guint subgroup);

/**
 * lrg_mmo_group_invite:
 * @self: the instance
 * @actor: authenticated requesting character
 * @invitee: nonzero character invited
 * @expires_at: when the invitation lapses, in the caller's clock (for
 *   example g_get_monotonic_time())
 * @error: (nullable): return location for an error
 *
 * Only the leader may invite.  A member or an already invited character
 * fails with %G_IO_ERROR_EXISTS; members plus pending invitations may not
 * exceed the capacity (%G_IO_ERROR_NO_SPACE), so an accepted invitation
 * always has a seat.
 *
 * Returns: whether invited
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_mmo_group_invite (LrgMmoGroup *self,
                      guint64 actor,
                      guint64 invitee,
                      gint64 expires_at,
                      GError **error);

/**
 * lrg_mmo_group_accept:
 * @self: the instance
 * @invitee: authenticated character answering
 * @now: the current time in the clock of lrg_mmo_group_invite()
 * @error: (nullable): return location for an error
 *
 * Joins the group on a pending invitation.  Without one it fails with
 * %G_IO_ERROR_NOT_FOUND; a lapsed one is dropped and fails with
 * %G_IO_ERROR_TIMED_OUT.
 *
 * Returns: whether joined
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_mmo_group_accept (LrgMmoGroup *self,
                      guint64 invitee,
                      gint64 now,
                      GError **error);

/**
 * lrg_mmo_group_decline:
 * @self: the instance
 * @invitee: authenticated character answering
 *
 * Drops @invitee's pending invitation, also used by the leader to
 * withdraw it.
 *
 * Returns: whether there was one
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_mmo_group_decline (LrgMmoGroup *self,
                       guint64 invitee);

/**
 * lrg_mmo_group_has_invite:
 * @self: the instance
 * @invitee: a character ID
 * @now: the current time in the clock of lrg_mmo_group_invite()
 *
 * Returns: whether @invitee holds an invitation that has not lapsed
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_mmo_group_has_invite (LrgMmoGroup *self,
                          guint64 invitee,
                          gint64 now);

/**
 * lrg_mmo_group_expire_invites:
 * @self: the instance
 * @now: the current time in the clock of lrg_mmo_group_invite()
 *
 * Drops every lapsed invitation.
 *
 * Returns: (transfer full) (element-type guint64): the invitees dropped,
 *   in ascending ID order
 */
LRG_AVAILABLE_IN_ALL
GArray *
lrg_mmo_group_expire_invites (LrgMmoGroup *self,
                              gint64 now);

G_END_DECLS
