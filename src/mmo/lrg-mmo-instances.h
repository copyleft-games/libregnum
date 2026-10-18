/* SPDX-License-Identifier: AGPL-3.0-or-later */
#pragma once
#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif
#include <gio/gio.h>
#include "../lrg-version.h"

G_BEGIN_DECLS
#define LRG_TYPE_MMO_INSTANCES (lrg_mmo_instances_get_type ())
LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgMmoInstances, lrg_mmo_instances, LRG, MMO_INSTANCES, GObject)

/**
 * LrgMmoInstances:
 *
 * The private copies of content a host runs for one group at a time
 * (dungeon instances, delves, raids, arenas).  Each instance has a
 * numeric id, a content id chosen by the host, a capacity and the
 * accounts it belongs to; an account belongs to one instance at most.
 * Only members are admitted, never more than the capacity, and the
 * host tells it who is inside so empty instances can be closed after a
 * while.  Ephemeral and confined to one thread; lrg_mmo_instances_save()
 * gives a value that survives a restart.
 */

/**
 * lrg_mmo_instances_new:
 * @max_instances: the most instances open at once (1 or more)
 *
 * Returns: (transfer full): an empty instance table
 */
LRG_AVAILABLE_IN_ALL
LrgMmoInstances *
lrg_mmo_instances_new (guint max_instances);

/**
 * lrg_mmo_instances_create:
 * @self: instance table
 * @content: the host's content ID (letters, digits, '-' and '_')
 * @capacity: the most members, 1 to 1024
 * @members: (array zero-terminated=1): the accounts it belongs to (at most @capacity, unique)
 * @now_us: monotonic microseconds
 * @error: (nullable): error return
 *
 * Opens an instance.  %G_IO_ERROR_NO_SPACE when the table is full or there
 * are more members than seats, %G_IO_ERROR_EXISTS when a member already
 * belongs to another instance, %G_IO_ERROR_INVALID_ARGUMENT for a bad ID.
 * A failed call changes nothing.
 *
 * Returns: the new instance's ID (never 0), or 0 on error
 */
LRG_AVAILABLE_IN_ALL
guint64
lrg_mmo_instances_create (LrgMmoInstances     *self,
                          const gchar         *content,
                          guint                capacity,
                          const gchar * const *members,
                          gint64               now_us,
                          GError             **error);

/**
 * lrg_mmo_instances_close:
 * @self: instance table
 * @id: an instance ID
 *
 * Closes an instance; its members belong to none afterwards.
 *
 * Returns: whether it was open
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_mmo_instances_close (LrgMmoInstances *self,
                         guint64          id);

/**
 * lrg_mmo_instances_has:
 * @self: instance table
 * @id: an instance ID
 *
 * Returns: whether the instance is open
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_mmo_instances_has (LrgMmoInstances *self,
                       guint64          id);

/**
 * lrg_mmo_instances_get_count:
 * @self: instance table
 *
 * Returns: the number of open instances
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_mmo_instances_get_count (LrgMmoInstances *self);

/**
 * lrg_mmo_instances_list:
 * @self: instance table
 *
 * Returns: (transfer full) (element-type guint64): the open instances, oldest first
 */
LRG_AVAILABLE_IN_ALL
GArray *
lrg_mmo_instances_list (LrgMmoInstances *self);

/**
 * lrg_mmo_instances_get_content:
 * @self: instance table
 * @id: an instance ID
 *
 * Returns: (transfer none) (nullable): the content ID of the instance
 */
LRG_AVAILABLE_IN_ALL
const gchar *
lrg_mmo_instances_get_content (LrgMmoInstances *self,
                               guint64          id);

/**
 * lrg_mmo_instances_get_capacity:
 * @self: instance table
 * @id: an instance ID
 *
 * Returns: the instance's seats, 0 when it is not open
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_mmo_instances_get_capacity (LrgMmoInstances *self,
                                guint64          id);

/**
 * lrg_mmo_instances_get_created:
 * @self: instance table
 * @id: an instance ID
 *
 * Returns: when the instance was opened (monotonic microseconds), 0 when it is not open
 */
LRG_AVAILABLE_IN_ALL
gint64
lrg_mmo_instances_get_created (LrgMmoInstances *self,
                               guint64          id);

/**
 * lrg_mmo_instances_get_members:
 * @self: instance table
 * @id: an instance ID
 *
 * Returns: (transfer full) (element-type utf8) (nullable): its members in
 *   the order they joined, %NULL when it is not open
 */
LRG_AVAILABLE_IN_ALL
GPtrArray *
lrg_mmo_instances_get_members (LrgMmoInstances *self,
                               guint64          id);

/**
 * lrg_mmo_instances_find:
 * @self: instance table
 * @account: account ID
 *
 * Returns: the instance @account belongs to, 0 for none
 */
LRG_AVAILABLE_IN_ALL
guint64
lrg_mmo_instances_find (LrgMmoInstances *self,
                        const gchar     *account);

/**
 * lrg_mmo_instances_add_member:
 * @self: instance table
 * @id: an instance ID
 * @account: account ID
 * @error: (nullable): error return
 *
 * Gives a seat of the instance to @account (a newcomer to the group, or a
 * replacement).  %G_IO_ERROR_NO_SPACE when every seat is taken (a full
 * instance), %G_IO_ERROR_EXISTS when @account belongs to an instance,
 * %G_IO_ERROR_NOT_FOUND when the instance is not open.
 *
 * Returns: whether added
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_mmo_instances_add_member (LrgMmoInstances *self,
                              guint64          id,
                              const gchar     *account,
                              GError         **error);

/**
 * lrg_mmo_instances_remove_member:
 * @self: instance table
 * @account: account ID
 * @now_us: monotonic microseconds
 *
 * Takes @account out of its instance (and out of it, if inside); the seat
 * is free again.  The instance stays open, even empty, until it expires or
 * is closed.
 *
 * Returns: the instance @account belonged to, 0 for none
 */
LRG_AVAILABLE_IN_ALL
guint64
lrg_mmo_instances_remove_member (LrgMmoInstances *self,
                                 const gchar     *account,
                                 gint64           now_us);

/**
 * lrg_mmo_instances_enter:
 * @self: instance table
 * @id: an instance ID
 * @account: account ID
 * @error: (nullable): error return
 *
 * Admits @account into the instance: only a member gets in
 * (%G_IO_ERROR_PERMISSION_DENIED otherwise, %G_IO_ERROR_NOT_FOUND for an
 * instance that is not open).  Entering again is no error.
 *
 * Returns: whether admitted
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_mmo_instances_enter (LrgMmoInstances *self,
                         guint64          id,
                         const gchar     *account,
                         GError         **error);

/**
 * lrg_mmo_instances_leave:
 * @self: instance table
 * @account: account ID
 * @now_us: monotonic microseconds
 *
 * @account is no longer inside its instance (it stays a member).  The
 * instance is empty from @now_us when nobody is inside.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_mmo_instances_leave (LrgMmoInstances *self,
                         const gchar     *account,
                         gint64           now_us);

/**
 * lrg_mmo_instances_is_inside:
 * @self: instance table
 * @account: account ID
 *
 * Returns: whether @account is inside its instance
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_mmo_instances_is_inside (LrgMmoInstances *self,
                             const gchar     *account);

/**
 * lrg_mmo_instances_get_inside:
 * @self: instance table
 * @id: an instance ID
 *
 * Returns: how many members are inside the instance
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_mmo_instances_get_inside (LrgMmoInstances *self,
                              guint64          id);

/**
 * lrg_mmo_instances_expire:
 * @self: instance table
 * @empty_us: an instance nobody has been inside for this long closes
 * @lifetime_us: an instance open this long closes (%G_MAXINT64: never)
 * @now_us: monotonic microseconds
 *
 * Closes the instances that stood empty for @empty_us (an instance nobody
 * ever entered counts from its creation) or outlived @lifetime_us.
 *
 * Returns: (transfer full) (element-type guint64): the instances closed
 */
LRG_AVAILABLE_IN_ALL
GArray *
lrg_mmo_instances_expire (LrgMmoInstances *self,
                          gint64           empty_us,
                          gint64           lifetime_us,
                          gint64           now_us);

/**
 * lrg_mmo_instances_save:
 * @self: instance table
 * @now_us: monotonic microseconds
 *
 * The open instances as a value that survives a restart:
 * (ta(tsuxas)) of the next ID and per instance its ID, content, capacity,
 * age in microseconds and members.  Nobody is inside after a restart.
 *
 * Returns: (transfer full): the table
 */
LRG_AVAILABLE_IN_ALL
GVariant *
lrg_mmo_instances_save (LrgMmoInstances *self,
                        gint64           now_us);

/**
 * lrg_mmo_instances_load:
 * @self: an empty instance table
 * @saved: the value of lrg_mmo_instances_save()
 * @now_us: monotonic microseconds of this process
 * @error: (nullable): error return
 *
 * Opens every saved instance again with its ID and age, everybody
 * outside (empty from @now_us).  Everything is checked first: a malformed
 * value, a duplicate ID or member, too many instances or members, or a
 * table that is not empty fails and changes nothing.
 *
 * Returns: whether loaded
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_mmo_instances_load (LrgMmoInstances *self,
                        GVariant        *saved,
                        gint64           now_us,
                        GError         **error);
G_END_DECLS
