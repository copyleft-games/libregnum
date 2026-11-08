/* lrg-collab.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Multi-user editing session: an operation log keyed by node GUID
 * (last writer wins, by the caller's revision), a per-node lock and
 * presence cursors. There is no clock and no socket here. The editor's
 * IPC layer sends the operations; this object is the authority that
 * accepts or rejects them.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"

G_BEGIN_DECLS

#define LRG_TYPE_COLLAB_SESSION (lrg_collab_session_get_type ())
#define LRG_COLLAB_ERROR (lrg_collab_error_quark ())

/**
 * LrgCollabError:
 * @LRG_COLLAB_ERROR_STALE: a revision older than the one already stored
 * @LRG_COLLAB_ERROR_LOCKED: the node is locked by someone else
 * @LRG_COLLAB_ERROR_FAILED: the call was rejected
 */
typedef enum
{
	LRG_COLLAB_ERROR_STALE,
	LRG_COLLAB_ERROR_LOCKED,
	LRG_COLLAB_ERROR_FAILED
} LrgCollabError;

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgCollabSession, lrg_collab_session, LRG, COLLAB_SESSION, GObject)

LRG_AVAILABLE_IN_ALL
GQuark lrg_collab_error_quark (void);

/**
 * lrg_collab_session_new:
 *
 * Returns: (transfer full): an empty session
 */
LRG_AVAILABLE_IN_ALL
LrgCollabSession * lrg_collab_session_new (void);

/**
 * lrg_collab_apply:
 * @self: a session
 * @user: who sent the operation
 * @guid: the node GUID
 * @revision: caller-supplied monotonic revision for this property
 * @prop: property name ("location", "name", ...)
 * @value: the new property text
 * @error: (nullable): return location for a #GError
 *
 * Last writer wins. A revision older than the stored one is rejected
 * with %LRG_COLLAB_ERROR_STALE. An equal revision replaces the value
 * (the later call is the last writer). A node locked by another user
 * is rejected with %LRG_COLLAB_ERROR_LOCKED.
 *
 * Returns: %TRUE when the value was stored
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_collab_apply (LrgCollabSession  *self,
                           const gchar       *user,
                           const gchar       *guid,
                           guint64            revision,
                           const gchar       *prop,
                           const gchar       *value,
                           GError           **error);

/**
 * lrg_collab_get:
 * @self: a session
 * @guid: the node GUID
 * @prop: property name
 *
 * Returns: (transfer none) (nullable): the stored value
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_collab_get (LrgCollabSession *self,
                              const gchar      *guid,
                              const gchar      *prop);

/**
 * lrg_collab_lock:
 * @self: a session
 * @user: who wants the lock
 * @guid: the node GUID
 * @error: (nullable): return location for a #GError
 *
 * Locking a node you already hold succeeds. Locking a node someone
 * else holds fails with %LRG_COLLAB_ERROR_LOCKED.
 *
 * Returns: %TRUE when @user holds the lock afterwards
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_collab_lock (LrgCollabSession  *self,
                          const gchar       *user,
                          const gchar       *guid,
                          GError           **error);

/**
 * lrg_collab_unlock:
 * @self: a session
 * @user: who holds the lock
 * @guid: the node GUID
 * @error: (nullable): return location for a #GError
 *
 * Returns: %TRUE when the lock was released
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_collab_unlock (LrgCollabSession  *self,
                            const gchar       *user,
                            const gchar       *guid,
                            GError           **error);

/**
 * lrg_collab_lock_holder:
 * @self: a session
 * @guid: the node GUID
 *
 * Returns: (transfer none) (nullable): the user who holds the lock
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_collab_lock_holder (LrgCollabSession *self,
                                      const gchar      *guid);

/**
 * lrg_collab_set_cursor:
 * @self: a session
 * @user: who moved
 * @x: cursor X
 * @y: cursor Y
 * @z: cursor Z
 *
 * Records a presence cursor. A later call for the same user replaces it.
 */
LRG_AVAILABLE_IN_ALL
void lrg_collab_set_cursor (LrgCollabSession *self,
                            const gchar      *user,
                            gfloat            x,
                            gfloat            y,
                            gfloat            z);

/**
 * lrg_collab_presence_count:
 * @self: a session
 *
 * Returns: how many users have a cursor
 */
LRG_AVAILABLE_IN_ALL
guint lrg_collab_presence_count (LrgCollabSession *self);

/**
 * lrg_collab_presence_get:
 * @self: a session
 * @index: which user
 * @user: (out) (transfer none) (optional): user id
 * @x: (out) (optional): cursor X
 * @y: (out) (optional): cursor Y
 * @z: (out) (optional): cursor Z
 *
 * Returns: %FALSE when @index is out of range
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_collab_presence_get (LrgCollabSession  *self,
                                  guint              index,
                                  const gchar      **user,
                                  gfloat            *x,
                                  gfloat            *y,
                                  gfloat            *z);

G_END_DECLS
