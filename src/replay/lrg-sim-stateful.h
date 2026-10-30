/* lrg-sim-stateful.h - Interface for objects with rollback-able simulation state
 *
 * Copyright 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * This file is part of Libregnum.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "lrg-state-buffer.h"

G_BEGIN_DECLS

#define LRG_TYPE_SIM_STATEFUL (lrg_sim_stateful_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_INTERFACE (LrgSimStateful, lrg_sim_stateful, LRG, SIM_STATEFUL, GObject)

/**
 * LrgSimStatefulInterface:
 * @parent_iface: the parent interface
 * @get_state_id: returns a stable identifier for the object (used to check
 *   that a saved state is loaded into the same object). Optional; the type
 *   name is used when unset.
 * @save_state: writes the complete simulation state into the buffer. Must be
 *   a pure function of that state: the same state always writes the same
 *   bytes (no pointers, hash-table order or wall-clock values).
 * @load_state: restores exactly what @save_state wrote, so that the next
 *   simulated tick behaves as if the object had never left that state.
 *
 * Objects that take part in a deterministic #LrgSimulation implement this
 * interface. Its byte stream is used three ways: hashed by the desync
 * detector, kept per tick by the rollback session, and compared by golden
 * replay tests. It is binary and versionless on purpose: it only has to
 * survive within one build of one game, unlike #LrgSaveable data.
 *
 * Since: 0.4
 */
struct _LrgSimStatefulInterface
{
    GTypeInterface parent_iface;

    /*< public >*/
    const gchar * (*get_state_id) (LrgSimStateful  *self);
    void          (*save_state)   (LrgSimStateful  *self,
                                   LrgStateBuffer  *buffer);
    gboolean      (*load_state)   (LrgSimStateful  *self,
                                   LrgStateBuffer  *buffer,
                                   GError         **error);

    /*< private >*/
    gpointer _reserved[4];
};

/**
 * lrg_sim_stateful_get_state_id:
 * @self: an #LrgSimStateful
 *
 * Returns: (transfer none): the object's state identifier
 */
LRG_AVAILABLE_IN_ALL
const gchar *lrg_sim_stateful_get_state_id (LrgSimStateful *self);

/**
 * lrg_sim_stateful_save_state:
 * @self: an #LrgSimStateful
 * @buffer: the buffer to append to
 *
 * Appends the object's simulation state to @buffer.
 */
LRG_AVAILABLE_IN_ALL
void lrg_sim_stateful_save_state (LrgSimStateful *self,
                                  LrgStateBuffer *buffer);

/**
 * lrg_sim_stateful_load_state:
 * @self: an #LrgSimStateful
 * @buffer: the buffer to read from, at the object's data
 * @error: (nullable): return location for an error
 *
 * Restores the object's simulation state from @buffer.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_sim_stateful_load_state (LrgSimStateful  *self,
                                      LrgStateBuffer  *buffer,
                                      GError         **error);

/**
 * lrg_sim_stateful_checksum:
 * @self: an #LrgSimStateful
 *
 * Saves the object into a scratch buffer and hashes it (64-bit FNV-1a).
 *
 * Returns: the checksum of the object's current state
 */
LRG_AVAILABLE_IN_ALL
guint64 lrg_sim_stateful_checksum (LrgSimStateful *self);

G_END_DECLS
