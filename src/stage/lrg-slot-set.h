/* lrg-slot-set.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgSlotSet: deterministic places for a group of actors in a zone --
 * N actors over M named seats (around a meeting table, at a bar),
 * everyone who does not fit standing in rings around the zone.  The same
 * calls give the same seating, whatever order the actors are listed in,
 * and people already placed keep their places as others come and go.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-types.h"
#include "../pathfinding/lrg-actor-mover.h"

G_BEGIN_DECLS

/**
 * LrgSlotKind:
 * @LRG_SLOT_NONE: not placed
 * @LRG_SLOT_SEAT: one of the named seats
 * @LRG_SLOT_STANDING: an overflow standing place
 *
 * What kind of place an actor was given.
 *
 * Since: 0.11.0
 */
typedef enum
{
	LRG_SLOT_NONE,
	LRG_SLOT_SEAT,
	LRG_SLOT_STANDING
} LrgSlotKind;

LRG_AVAILABLE_IN_ALL
GType lrg_slot_kind_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_SLOT_KIND (lrg_slot_kind_get_type ())

#define LRG_TYPE_SLOT_SET (lrg_slot_set_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgSlotSet, lrg_slot_set, LRG, SLOT_SET, GObject)

/**
 * lrg_slot_set_new:
 *
 * Creates an empty slot set on the x/y plane with its overflow centre at
 * the origin, 24 units out and 20 apart.
 *
 * Returns: (transfer full): a new #LrgSlotSet
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
LrgSlotSet * lrg_slot_set_new (void);

/**
 * lrg_slot_set_set_plane:
 * @self: an #LrgSlotSet
 * @plane: the ground plane for generated places and facings
 *
 * Sets whether overflow rings lie in x/y (2D) or x/z (3D, y up).
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_slot_set_set_plane (LrgSlotSet         *self,
                             LrgActorMoverPlane  plane);

/**
 * lrg_slot_set_add_seat:
 * @self: an #LrgSlotSet
 * @name: a unique seat name
 * @x: x
 * @y: y
 * @z: z
 * @facing: the direction a seated actor faces (see
 *   lrg_actor_mover_get_facing())
 *
 * Adds a seat.  Seats fill in the order they were added.
 *
 * Returns: %FALSE when a seat already has @name
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_slot_set_add_seat (LrgSlotSet  *self,
                                const gchar *name,
                                gfloat       x,
                                gfloat       y,
                                gfloat       z,
                                gfloat       facing);

/**
 * lrg_slot_set_add_seats_around:
 * @self: an #LrgSlotSet
 * @prefix: seat names are @prefix followed by 0, 1, 2 ...
 * @x: centre x (a round table)
 * @y: centre y
 * @z: centre z
 * @radius: distance of the seats from the centre
 * @count: number of seats
 * @start_angle: angle of the first seat, in the plane (radians)
 *
 * Adds @count seats evenly round a centre, each facing it.
 *
 * Returns: the number of seats added (names already used are skipped)
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
guint lrg_slot_set_add_seats_around (LrgSlotSet  *self,
                                     const gchar *prefix,
                                     gfloat       x,
                                     gfloat       y,
                                     gfloat       z,
                                     gfloat       radius,
                                     guint        count,
                                     gfloat       start_angle);

/**
 * lrg_slot_set_add_standing:
 * @self: an #LrgSlotSet
 * @x: x
 * @y: y
 * @z: z
 * @facing: the direction faced
 *
 * Adds a hand-placed standing place, used before the generated rings.
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_slot_set_add_standing (LrgSlotSet *self,
                                gfloat      x,
                                gfloat      y,
                                gfloat      z,
                                gfloat      facing);

/**
 * lrg_slot_set_set_overflow:
 * @self: an #LrgSlotSet
 * @x: centre x of the zone
 * @y: centre y
 * @z: centre z
 * @radius: radius of the first ring of standing places (> 0)
 * @spacing: distance between standing places, and between rings (> 0)
 *
 * Sets where overflow actors stand: on rings round the centre, the
 * first at @radius and each next one @spacing further out, places
 * @spacing apart along each ring starting at angle 0, everyone facing
 * the centre.  Generated places are numbered after the hand-placed
 * ones.
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_slot_set_set_overflow (LrgSlotSet *self,
                                gfloat      x,
                                gfloat      y,
                                gfloat      z,
                                gfloat      radius,
                                gfloat      spacing);

/**
 * lrg_slot_set_get_n_seats:
 * @self: an #LrgSlotSet
 *
 * Returns: the number of seats
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
guint lrg_slot_set_get_n_seats (LrgSlotSet *self);

/**
 * lrg_slot_set_get_seat_name:
 * @self: an #LrgSlotSet
 * @index: a seat index
 *
 * Returns: (transfer none) (nullable): the seat's name
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_slot_set_get_seat_name (LrgSlotSet *self,
                                          guint       index);

/**
 * lrg_slot_set_get_standing_place:
 * @self: an #LrgSlotSet
 * @index: a standing place index (hand-placed first, then generated)
 * @x: (out) (optional): x
 * @y: (out) (optional): y
 * @z: (out) (optional): z
 * @facing: (out) (optional): facing
 *
 * Computes a standing place; generated places exist for every index.
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_slot_set_get_standing_place (LrgSlotSet *self,
                                      guint       index,
                                      gfloat     *x,
                                      gfloat     *y,
                                      gfloat     *z,
                                      gfloat     *facing);

/**
 * lrg_slot_set_assign:
 * @self: an #LrgSlotSet
 * @actor_ids: (array zero-terminated=1): the actors in the zone now
 *
 * Places every listed actor and frees the places of actors no longer
 * listed.  Deterministic:
 *
 * 1. Actors that left lose their places.
 * 2. Actors already seated keep their seats; actors already standing
 *    keep their places unless a seat is free.
 * 3. Free seats go, in seat order, first to standing actors (in the
 *    order they arrived, then by id), then to new actors sorted by id.
 * 4. Remaining new actors take the lowest free standing places, by id.
 *
 * The order of @actor_ids does not matter; duplicates count once.
 * After lrg_slot_set_clear() the result depends on the ids alone.
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_slot_set_assign (LrgSlotSet          *self,
                          const gchar * const *actor_ids);

/**
 * lrg_slot_set_clear:
 * @self: an #LrgSlotSet
 *
 * Forgets every placement (seats and standing places stay defined).
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_slot_set_clear (LrgSlotSet *self);

/**
 * lrg_slot_set_get_placement:
 * @self: an #LrgSlotSet
 * @actor_id: an actor
 * @index: (out) (optional): the seat or standing place index
 * @x: (out) (optional): x
 * @y: (out) (optional): y
 * @z: (out) (optional): z
 * @facing: (out) (optional): the facing there
 *
 * Reads where an actor was placed.
 *
 * Returns: the kind of place, %LRG_SLOT_NONE when the actor is not placed
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
LrgSlotKind lrg_slot_set_get_placement (LrgSlotSet  *self,
                                        const gchar *actor_id,
                                        guint       *index,
                                        gfloat      *x,
                                        gfloat      *y,
                                        gfloat      *z,
                                        gfloat      *facing);

/**
 * lrg_slot_set_get_seat_occupant:
 * @self: an #LrgSlotSet
 * @index: a seat index
 *
 * Returns: (transfer none) (nullable): who sits there
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_slot_set_get_seat_occupant (LrgSlotSet *self,
                                              guint       index);

/**
 * lrg_slot_set_get_n_seated:
 * @self: an #LrgSlotSet
 *
 * Returns: how many actors have seats
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
guint lrg_slot_set_get_n_seated (LrgSlotSet *self);

/**
 * lrg_slot_set_get_n_standing:
 * @self: an #LrgSlotSet
 *
 * Returns: how many actors are standing
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
guint lrg_slot_set_get_n_standing (LrgSlotSet *self);

G_END_DECLS
