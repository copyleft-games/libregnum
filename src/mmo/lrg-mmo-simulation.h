/* SPDX-License-Identifier: AGPL-3.0-or-later */
#pragma once
#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif
#include <gio/gio.h>
#include "../lrg-version.h"
G_BEGIN_DECLS
#define LRG_TYPE_MMO_SIMULATION (lrg_mmo_simulation_get_type ())
LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgMmoSimulation, lrg_mmo_simulation, LRG, MMO_SIMULATION, GObject)

/**
 * lrg_mmo_simulation_new:
 * @capacity: maximum characters
 *
 * Creates a thread-confined authoritative RPG simulation. A tick is 50ms.
 * All setup methods are trusted server administration, not client commands.
 * Returns: (transfer full): simulation
 */
LRG_AVAILABLE_IN_ALL
LrgMmoSimulation *lrg_mmo_simulation_new (guint capacity);

/**
 * lrg_mmo_simulation_spawn:
 * @self: simulation
 * @id: nonzero stable character ID
 * @account: verified owner ID
 * @zone: zone identifier
 * @x: spawn X
 * @y: spawn Y
 * @z: spawn Z
 * @radius: collision radius, 0 to 1000
 * @health: initial health, 1 to one billion
 * @faction: allies share a faction and cannot damage each other
 * @speed: maximum world units per second, positive up to one million
 * @damage: attack damage, 1 to one billion
 * @range: maximum center-to-center attack distance, 0 to 1000
 * @cooldown: attack cooldown in ticks, at least one
 * @error: (nullable): return location for error
 *
 * Registers server-owned character configuration. Existing IDs reject.
 * Returns: whether spawned
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_mmo_simulation_spawn (LrgMmoSimulation *self, guint64 id, const gchar *account,
                                   const gchar *zone, gdouble x, gdouble y, gdouble z,
                                   gdouble radius, guint health, guint faction, gdouble speed,
                                   guint damage, gdouble range, guint cooldown, GError **error);

/**
 * lrg_mmo_simulation_add_obstacle:
 * @self: simulation
 * @zone: zone identifier
 * @min_x: minimum X
 * @min_y: minimum Y
 * @min_z: minimum Z
 * @max_x: maximum X
 * @max_y: maximum Y
 * @max_z: maximum Z
 * @error: (nullable): return location for error
 *
 * Adds a static solid box, up to 4096 boxes. Collision uses the moving character's
 * radius to expand boxes conservatively; obstacles also block attack sight lines.
 * Returns: whether added
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_mmo_simulation_add_obstacle (LrgMmoSimulation *self, const gchar *zone,
                                          gdouble min_x, gdouble min_y, gdouble min_z,
                                          gdouble max_x, gdouble max_y, gdouble max_z, GError **error);

/**
 * lrg_mmo_simulation_advance:
 * @self: simulation
 * @ticks: trusted elapsed fixed ticks, 1 to 20
 * @error: (nullable): return location for error
 *
 * Advances the server clock. Clients must never supply elapsed time. Movement
 * credit is capped at five ticks, preventing long-idle teleportation.
 * Returns: whether advanced
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_mmo_simulation_advance (LrgMmoSimulation *self, guint ticks, GError **error);

/**
 * lrg_mmo_simulation_move:
 * @self: simulation
 * @account: authenticated actor from the session
 * @id: controlled character
 * @sequence: nonzero increasing command sequence shared by move and attack
 * @x: requested destination X
 * @y: requested destination Y
 * @z: requested destination Z
 * @error: (nullable): return location for error
 *
 * Rejects unauthorized/dead characters, replay, speed violations and swept
 * collision. Successful movement consumes all accumulated movement credit.
 * Returns: whether moved
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_mmo_simulation_move (LrgMmoSimulation *self, const gchar *account, guint64 id,
                                  guint64 sequence, gdouble x, gdouble y, gdouble z, GError **error);

/**
 * lrg_mmo_simulation_attack:
 * @self: simulation
 * @account: authenticated actor
 * @id: controlled attacker
 * @sequence: increasing command sequence
 * @target: target character ID
 * @error: (nullable): return location for error
 *
 * Validates ownership, health, zone, faction, range, cooldown and line of sight.
 * Damage comes exclusively from server configuration. No client timestamp is used.
 * Returns: whether damage was applied
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_mmo_simulation_attack (LrgMmoSimulation *self, const gchar *account,
                                    guint64 id, guint64 sequence, guint64 target, GError **error);

/**
 * lrg_mmo_simulation_lookup:
 * @self: simulation
 * @id: character ID
 *
 * Reads public position, health and last accepted command sequence.
 * Returns: (transfer full) (nullable): (dddut), or NULL if absent
 */
LRG_AVAILABLE_IN_ALL
GVariant *lrg_mmo_simulation_lookup (LrgMmoSimulation *self, guint64 id);

/**
 * lrg_mmo_simulation_snapshot:
 * @self: simulation
 *
 * Captures authoritative character configuration, clocks and sequences for
 * persistence/handoff. This contains account ownership; never broadcast it.
 * Returns: (transfer full): (ta(tssdddduududtttt)) snapshot
 */
LRG_AVAILABLE_IN_ALL
GVariant *lrg_mmo_simulation_snapshot (LrgMmoSimulation *self);

/**
 * lrg_mmo_simulation_restore:
 * @self: simulation
 * @snapshot: trusted-server snapshot; validated before replacement
 * @error: (nullable): return location for error
 *
 * Atomically replaces all characters and the clock. Static obstacles remain
 * from the destination's content. Restored characters must fit that content.
 * Returns: whether restored
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_mmo_simulation_restore (LrgMmoSimulation *self, GVariant *snapshot, GError **error);
G_END_DECLS
