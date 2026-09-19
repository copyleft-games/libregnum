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

G_END_DECLS
