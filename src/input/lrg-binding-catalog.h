/* lrg-binding-catalog.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgBindingCatalog - the list of rebindable actions a key binding
 * screen shows: actions in ordered groups, with labels and owners (the
 * game, a mod), each bound to an ordered list of key names, and the
 * conflicts between them.  Keys are opaque names ("W", "Shift+1",
 * "PadA") compared without case, so the catalogue works with any input
 * layer; the game applies the result to its own input map.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib.h>
#include <glib-object.h>

#include "../lrg-version.h"
#include "../lrg-enums.h"

G_BEGIN_DECLS

/**
 * LRG_BINDING_CATALOG_MAX_KEYS:
 *
 * Keys one action can hold.
 */
#define LRG_BINDING_CATALOG_MAX_KEYS (4)

#define LRG_TYPE_BINDING_CATALOG (lrg_binding_catalog_get_type ())

#pragma GCC visibility push(default)
G_DECLARE_FINAL_TYPE (LrgBindingCatalog, lrg_binding_catalog, LRG, BINDING_CATALOG, GObject)
#pragma GCC visibility pop

/**
 * LrgBindingCatalog:
 *
 * Groups and actions keep the order they were added in.  An action
 * added to a group that does not exist creates the group (labelled with
 * its id).  #LrgBindingCatalog::changed fires after every change of an
 * action's keys, with the action's id, and with %NULL when actions were
 * added or removed.
 */

/**
 * LrgBindingCatalog::changed:
 * @self: the catalogue
 * @action: (nullable): the action whose keys changed, %NULL when actions
 *   were added or removed
 */

/**
 * lrg_binding_catalog_new:
 *
 * Returns: (transfer full): an empty catalogue
 */
LRG_AVAILABLE_IN_ALL
LrgBindingCatalog *
lrg_binding_catalog_new (void);

/**
 * lrg_binding_catalog_normalize_key:
 * @key: a key name
 *
 * The form keys are compared in: trimmed, lower case.
 *
 * Returns: (transfer full): the normalized name
 */
LRG_AVAILABLE_IN_ALL
gchar *
lrg_binding_catalog_normalize_key (const gchar *key);

/* --- groups --- */

/**
 * lrg_binding_catalog_add_group:
 * @self: an #LrgBindingCatalog
 * @id: a group id ("movement")
 * @label: (nullable): what the screen shows, %NULL for @id
 *
 * Adds a group at the end, or relabels an existing one.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_binding_catalog_add_group (LrgBindingCatalog *self,
                               const gchar       *id,
                               const gchar       *label);

/**
 * lrg_binding_catalog_get_groups:
 * @self: an #LrgBindingCatalog
 *
 * Returns: (transfer full): the group ids in order
 */
LRG_AVAILABLE_IN_ALL
GStrv
lrg_binding_catalog_get_groups (LrgBindingCatalog *self);

/**
 * lrg_binding_catalog_get_group_label:
 * @self: an #LrgBindingCatalog
 * @id: a group id
 *
 * Returns: (transfer none) (nullable): the label
 */
LRG_AVAILABLE_IN_ALL
const gchar *
lrg_binding_catalog_get_group_label (LrgBindingCatalog *self,
                                     const gchar       *id);

/* --- actions --- */

/**
 * lrg_binding_catalog_add_action:
 * @self: an #LrgBindingCatalog
 * @id: the action id ("jump")
 * @label: (nullable): what the screen shows, %NULL for @id
 * @group: the group id
 * @owner: (nullable): who added it (a mod id), for
 *   lrg_binding_catalog_remove_owner()
 * @keys: (nullable) (array zero-terminated=1): the keys it starts with
 *
 * Returns: %FALSE when @id exists already (nothing changes)
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_binding_catalog_add_action (LrgBindingCatalog   *self,
                                const gchar         *id,
                                const gchar         *label,
                                const gchar         *group,
                                const gchar         *owner,
                                const gchar * const *keys);

/**
 * lrg_binding_catalog_remove_action:
 * @self: an #LrgBindingCatalog
 * @id: an action id
 *
 * Returns: %TRUE when it existed
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_binding_catalog_remove_action (LrgBindingCatalog *self,
                                   const gchar       *id);

/**
 * lrg_binding_catalog_remove_owner:
 * @self: an #LrgBindingCatalog
 * @owner: an owner tag
 *
 * Removes every action @owner added (a mod unloading).
 *
 * Returns: the actions removed
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_binding_catalog_remove_owner (LrgBindingCatalog *self,
                                  const gchar       *owner);

/**
 * lrg_binding_catalog_has_action:
 * @self: an #LrgBindingCatalog
 * @id: an action id
 *
 * Returns: %TRUE when the catalogue lists @id
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_binding_catalog_has_action (LrgBindingCatalog *self,
                                const gchar       *id);

/**
 * lrg_binding_catalog_get_actions:
 * @self: an #LrgBindingCatalog
 * @group: (nullable): a group id, %NULL for every action
 *
 * Returns: (transfer full): the action ids in order
 */
LRG_AVAILABLE_IN_ALL
GStrv
lrg_binding_catalog_get_actions (LrgBindingCatalog *self,
                                 const gchar       *group);

/**
 * lrg_binding_catalog_get_label:
 * @self: an #LrgBindingCatalog
 * @id: an action id
 *
 * Returns: (transfer none) (nullable): the action's label
 */
LRG_AVAILABLE_IN_ALL
const gchar *
lrg_binding_catalog_get_label (LrgBindingCatalog *self,
                               const gchar       *id);

/**
 * lrg_binding_catalog_get_group:
 * @self: an #LrgBindingCatalog
 * @id: an action id
 *
 * Returns: (transfer none) (nullable): the action's group
 */
LRG_AVAILABLE_IN_ALL
const gchar *
lrg_binding_catalog_get_group (LrgBindingCatalog *self,
                               const gchar       *id);

/**
 * lrg_binding_catalog_get_owner:
 * @self: an #LrgBindingCatalog
 * @id: an action id
 *
 * Returns: (transfer none) (nullable): who added the action
 */
LRG_AVAILABLE_IN_ALL
const gchar *
lrg_binding_catalog_get_owner (LrgBindingCatalog *self,
                               const gchar       *id);

/* --- keys --- */

/**
 * lrg_binding_catalog_get_keys:
 * @self: an #LrgBindingCatalog
 * @id: an action id
 *
 * Returns: (transfer none) (nullable) (array zero-terminated=1): the
 *   action's keys in order (primary first), %NULL for an unknown action
 */
LRG_AVAILABLE_IN_ALL
const gchar * const *
lrg_binding_catalog_get_keys (LrgBindingCatalog *self,
                              const gchar       *id);

/**
 * lrg_binding_catalog_set_keys:
 * @self: an #LrgBindingCatalog
 * @id: an action id
 * @keys: (nullable) (array zero-terminated=1): the new keys (duplicates
 *   and empty names dropped, at most %LRG_BINDING_CATALOG_MAX_KEYS)
 *
 * Replaces the action's keys without looking at conflicts.
 *
 * Returns: %FALSE for an unknown action
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_binding_catalog_set_keys (LrgBindingCatalog   *self,
                              const gchar         *id,
                              const gchar * const *keys);

/**
 * lrg_binding_catalog_find_actions:
 * @self: an #LrgBindingCatalog
 * @key: a key name
 * @except: (nullable): an action to leave out
 *
 * Returns: (transfer full): the actions bound to @key, in catalogue order
 */
LRG_AVAILABLE_IN_ALL
GStrv
lrg_binding_catalog_find_actions (LrgBindingCatalog *self,
                                  const gchar       *key,
                                  const gchar       *except);

/**
 * lrg_binding_catalog_bind:
 * @self: an #LrgBindingCatalog
 * @id: the action to bind
 * @slot: which of its keys (0 the primary; past the last: a new one)
 * @key: the key name
 * @policy: what happens to other actions holding @key
 * @out_conflicts: (out) (optional) (transfer full): the other actions
 *   that held @key before the call
 *
 * Binds @key to @id in @slot.  With %LRG_BINDING_CONFLICT_REFUSE and
 * conflicts nothing changes and %FALSE is returned (@out_conflicts says
 * why: a screen asks the player and binds again with the answer).
 *
 * Returns: %TRUE when the key is bound
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_binding_catalog_bind (LrgBindingCatalog         *self,
                          const gchar               *id,
                          guint                      slot,
                          const gchar               *key,
                          LrgBindingConflictPolicy   policy,
                          GStrv                     *out_conflicts);

/**
 * lrg_binding_catalog_unbind:
 * @self: an #LrgBindingCatalog
 * @id: an action id
 * @key: (nullable): the key to drop, %NULL for all of them
 *
 * Returns: %TRUE when a key was dropped
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_binding_catalog_unbind (LrgBindingCatalog *self,
                            const gchar       *id,
                            const gchar       *key);

/**
 * lrg_binding_catalog_get_conflicted_keys:
 * @self: an #LrgBindingCatalog
 *
 * Returns: (transfer full): every key (normalized) bound to more than one
 *   action, sorted
 */
LRG_AVAILABLE_IN_ALL
GStrv
lrg_binding_catalog_get_conflicted_keys (LrgBindingCatalog *self);

G_END_DECLS
