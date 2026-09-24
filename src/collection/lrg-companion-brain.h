/* lrg-companion-brain.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgCompanionBrain - pure, server-authoritative decision logic for pets
 * and other companions: follow / assist / defend / passive. No rendering,
 * no ECS, no clock, no randomness; the caller feeds a snapshot of the world
 * each tick and applies the returned intent.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-types.h"
#include "../lrg-enums.h"
#include "lrg-pet-def.h"

G_BEGIN_DECLS

#define LRG_TYPE_COMPANION_INPUT  (lrg_companion_input_get_type ())
#define LRG_TYPE_COMPANION_OUTPUT (lrg_companion_output_get_type ())
#define LRG_TYPE_COMPANION_BRAIN  (lrg_companion_brain_get_type ())

/**
 * LRG_COMPANION_ARRIVE_DISTANCE:
 *
 * Distance to the follow point at or below which an idle companion stays
 * put instead of issuing %LRG_COMPANION_ACTION_FOLLOW.
 */
#define LRG_COMPANION_ARRIVE_DISTANCE (0.75)

/**
 * LrgCompanionInput:
 * @x: companion X position
 * @z: companion Z position
 * @owner_x: owner X position
 * @owner_z: owner Z position
 * @owner_facing: owner facing in radians; 0 faces -Z and positive values
 *   rotate toward -X
 * @owner_target: hostile the owner is targeting, 0 for none
 * @owner_target_alive: whether @owner_target is alive
 * @owner_target_x: X position of @owner_target
 * @owner_target_z: Z position of @owner_target
 * @owner_attacker: hostile currently attacking the owner, 0 for none
 * @owner_attacker_x: X position of @owner_attacker
 * @owner_attacker_z: Z position of @owner_attacker
 * @self_attacker: hostile currently attacking the companion, 0 for none
 * @self_attacker_x: X position of @self_attacker
 * @self_attacker_z: Z position of @self_attacker
 * @owner_in_combat: whether the owner is in combat (informational)
 * @alive: whether the companion is alive
 *
 * World snapshot for one lrg_companion_brain_think() call. Positions are
 * on the XZ plane. A hostile whose coordinates are not finite is treated
 * as unknown.
 */
struct _LrgCompanionInput
{
    gdouble  x;
    gdouble  z;
    gdouble  owner_x;
    gdouble  owner_z;
    gdouble  owner_facing;
    guint    owner_target;
    gboolean owner_target_alive;
    gdouble  owner_target_x;
    gdouble  owner_target_z;
    guint    owner_attacker;
    gdouble  owner_attacker_x;
    gdouble  owner_attacker_z;
    guint    self_attacker;
    gdouble  self_attacker_x;
    gdouble  self_attacker_z;
    gboolean owner_in_combat;
    gboolean alive;
};

/**
 * LrgCompanionOutput:
 * @action: the chosen intent
 * @dest_x: X coordinate to move to / snap to (see lrg_companion_brain_think())
 * @dest_z: Z coordinate to move to / snap to
 * @target: attack target when @action is %LRG_COMPANION_ACTION_ATTACK, else 0
 *
 * Intent produced by lrg_companion_brain_think().
 */
struct _LrgCompanionOutput
{
    LrgCompanionAction action;
    gdouble            dest_x;
    gdouble            dest_z;
    guint              target;
};

LRG_AVAILABLE_IN_ALL
GType lrg_companion_input_get_type (void) G_GNUC_CONST;

LRG_AVAILABLE_IN_ALL
GType lrg_companion_output_get_type (void) G_GNUC_CONST;

/**
 * lrg_companion_input_new:
 *
 * Creates a zero-initialised input. Note that @alive starts %FALSE.
 *
 * Returns: (transfer full): a new #LrgCompanionInput
 */
LRG_AVAILABLE_IN_ALL
LrgCompanionInput *
lrg_companion_input_new (void);

/**
 * lrg_companion_input_copy:
 * @self: an #LrgCompanionInput
 *
 * Returns: (transfer full): a copy of @self
 */
LRG_AVAILABLE_IN_ALL
LrgCompanionInput *
lrg_companion_input_copy (const LrgCompanionInput *self);

/**
 * lrg_companion_input_free:
 * @self: (nullable): an #LrgCompanionInput
 *
 * Frees @self.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_companion_input_free (LrgCompanionInput *self);

/**
 * lrg_companion_output_new:
 *
 * Creates a zero-initialised output (action IDLE).
 *
 * Returns: (transfer full): a new #LrgCompanionOutput
 */
LRG_AVAILABLE_IN_ALL
LrgCompanionOutput *
lrg_companion_output_new (void);

/**
 * lrg_companion_output_copy:
 * @self: an #LrgCompanionOutput
 *
 * Returns: (transfer full): a copy of @self
 */
LRG_AVAILABLE_IN_ALL
LrgCompanionOutput *
lrg_companion_output_copy (const LrgCompanionOutput *self);

/**
 * lrg_companion_output_free:
 * @self: (nullable): an #LrgCompanionOutput
 *
 * Frees @self.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_companion_output_free (LrgCompanionOutput *self);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (LrgCompanionInput, lrg_companion_input_free)
G_DEFINE_AUTOPTR_CLEANUP_FUNC (LrgCompanionOutput, lrg_companion_output_free)

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgCompanionBrain, lrg_companion_brain, LRG, COMPANION_BRAIN, GObject)

/**
 * lrg_companion_brain_new:
 * @kind: vanity or combat
 *
 * Creates a brain with default tuning (stance ASSIST, follow distance
 * 2.0, follow angle 2.356, leash 30, teleport 60, attack range 3.0).
 *
 * Returns: (transfer full): a new #LrgCompanionBrain
 */
LRG_AVAILABLE_IN_ALL
LrgCompanionBrain *
lrg_companion_brain_new (LrgCompanionKind kind);

/**
 * lrg_companion_brain_new_for_pet:
 * @pet: an #LrgPetDef
 *
 * Creates a brain whose kind, follow distance, follow angle and attack
 * range are copied from @pet.
 *
 * Returns: (transfer full): a new #LrgCompanionBrain
 */
LRG_AVAILABLE_IN_ALL
LrgCompanionBrain *
lrg_companion_brain_new_for_pet (LrgPetDef *pet);

/**
 * lrg_companion_brain_get_kind:
 * @self: an #LrgCompanionBrain
 *
 * Returns: the companion kind
 */
LRG_AVAILABLE_IN_ALL
LrgCompanionKind
lrg_companion_brain_get_kind (LrgCompanionBrain *self);

/**
 * lrg_companion_brain_set_kind:
 * @self: an #LrgCompanionBrain
 * @kind: the companion kind
 *
 * Sets the kind. Switching to %LRG_COMPANION_KIND_VANITY clears the
 * current target.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_companion_brain_set_kind (LrgCompanionBrain *self,
                              LrgCompanionKind   kind);

/**
 * lrg_companion_brain_get_stance:
 * @self: an #LrgCompanionBrain
 *
 * Returns: the combat stance
 */
LRG_AVAILABLE_IN_ALL
LrgCompanionStance
lrg_companion_brain_get_stance (LrgCompanionBrain *self);

/**
 * lrg_companion_brain_set_stance:
 * @self: an #LrgCompanionBrain
 * @stance: the combat stance
 *
 * Sets the combat stance. The current target is kept; a PASSIVE brain
 * drops a target it picked itself on the next think but keeps an
 * explicitly commanded one.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_companion_brain_set_stance (LrgCompanionBrain  *self,
                                LrgCompanionStance  stance);

/**
 * lrg_companion_brain_get_follow_distance:
 * @self: an #LrgCompanionBrain
 *
 * Returns: follow distance
 */
LRG_AVAILABLE_IN_ALL
gdouble
lrg_companion_brain_get_follow_distance (LrgCompanionBrain *self);

/**
 * lrg_companion_brain_set_follow_distance:
 * @self: an #LrgCompanionBrain
 * @distance: follow distance in [0, 1000]
 *
 * Sets the follow distance.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_companion_brain_set_follow_distance (LrgCompanionBrain *self,
                                         gdouble            distance);

/**
 * lrg_companion_brain_get_follow_angle:
 * @self: an #LrgCompanionBrain
 *
 * Returns: follow angle in radians relative to the owner's facing
 */
LRG_AVAILABLE_IN_ALL
gdouble
lrg_companion_brain_get_follow_angle (LrgCompanionBrain *self);

/**
 * lrg_companion_brain_set_follow_angle:
 * @self: an #LrgCompanionBrain
 * @angle: follow angle in radians, in [-2*pi, 2*pi]
 *
 * Sets the follow angle.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_companion_brain_set_follow_angle (LrgCompanionBrain *self,
                                      gdouble            angle);

/**
 * lrg_companion_brain_get_leash_range:
 * @self: an #LrgCompanionBrain
 *
 * Returns: leash range
 */
LRG_AVAILABLE_IN_ALL
gdouble
lrg_companion_brain_get_leash_range (LrgCompanionBrain *self);

/**
 * lrg_companion_brain_set_leash_range:
 * @self: an #LrgCompanionBrain
 * @range: leash range in [0, 100000]
 *
 * Sets the leash range.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_companion_brain_set_leash_range (LrgCompanionBrain *self,
                                     gdouble            range);

/**
 * lrg_companion_brain_get_teleport_range:
 * @self: an #LrgCompanionBrain
 *
 * Returns: teleport range
 */
LRG_AVAILABLE_IN_ALL
gdouble
lrg_companion_brain_get_teleport_range (LrgCompanionBrain *self);

/**
 * lrg_companion_brain_set_teleport_range:
 * @self: an #LrgCompanionBrain
 * @range: teleport range in [0, 100000]
 *
 * Sets the teleport range.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_companion_brain_set_teleport_range (LrgCompanionBrain *self,
                                        gdouble            range);

/**
 * lrg_companion_brain_get_attack_range:
 * @self: an #LrgCompanionBrain
 *
 * Returns: attack range
 */
LRG_AVAILABLE_IN_ALL
gdouble
lrg_companion_brain_get_attack_range (LrgCompanionBrain *self);

/**
 * lrg_companion_brain_set_attack_range:
 * @self: an #LrgCompanionBrain
 * @range: attack range in [0, 1000]
 *
 * Sets the attack range.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_companion_brain_set_attack_range (LrgCompanionBrain *self,
                                      gdouble            range);

/**
 * lrg_companion_brain_command_attack:
 * @self: an #LrgCompanionBrain
 * @target: hostile id, 0 clears the target
 *
 * Explicitly orders an attack. The order overrides the stance (including
 * PASSIVE) but is ignored by VANITY companions. The target is only kept
 * while it appears in the input as the owner's live target or as an
 * attacker.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_companion_brain_command_attack (LrgCompanionBrain *self,
                                    guint              target);

/**
 * lrg_companion_brain_command_follow:
 * @self: an #LrgCompanionBrain
 *
 * Clears the current target and any explicit attack order. The stance
 * may pick a new target on the next think; use the PASSIVE stance to
 * keep the companion out of combat.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_companion_brain_command_follow (LrgCompanionBrain *self);

/**
 * lrg_companion_brain_get_target:
 * @self: an #LrgCompanionBrain
 *
 * Returns: the current target, 0 for none
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_companion_brain_get_target (LrgCompanionBrain *self);

/**
 * lrg_companion_brain_is_commanded:
 * @self: an #LrgCompanionBrain
 *
 * Returns: %TRUE when the current target came from
 *   lrg_companion_brain_command_attack()
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_companion_brain_is_commanded (LrgCompanionBrain *self);

/**
 * lrg_companion_brain_think:
 * @self: an #LrgCompanionBrain
 * @input: world snapshot
 * @output: (out caller-allocates): the decided intent
 *
 * Runs one decision step. Rules, first match wins:
 *
 * 1. Dead (or non-finite companion/owner position or facing): IDLE at the
 *    companion's position (0,0 if non-finite); target and order cleared.
 * 2. VANITY: target cleared, then the follow rule (5).
 * 3. Farther than leash-range from the owner: target and order cleared;
 *    TELEPORT to the follow point if it is farther than teleport-range,
 *    otherwise RETURN to the follow point.
 * 4. Target: the current target is kept while it is still a known hostile
 *    with a finite position (the owner's target only while alive; an
 *    owner target reported dead invalidates it). Otherwise one is picked
 *    by stance: PASSIVE none; DEFENSIVE owner_attacker then self_attacker;
 *    ASSIST and AGGRESSIVE the live owner_target, then as DEFENSIVE. A
 *    PASSIVE brain drops targets it picked itself. With a target the
 *    action is ATTACK with @output->target set: within attack-range the
 *    destination is the companion's own position (hold and strike),
 *    otherwise the target's position (chase).
 * 5. Follow: distance to the follow point greater than teleport-range
 *    gives TELEPORT, greater than %LRG_COMPANION_ARRIVE_DISTANCE gives
 *    FOLLOW (both with the follow point as destination), else IDLE at the
 *    companion's position.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_companion_brain_think (LrgCompanionBrain       *self,
                           const LrgCompanionInput *input,
                           LrgCompanionOutput      *output);

/**
 * lrg_companion_brain_follow_point:
 * @self: an #LrgCompanionBrain
 * @owner_x: owner X position
 * @owner_z: owner Z position
 * @owner_facing: owner facing in radians (0 faces -Z, positive rotates toward -X)
 * @out_x: (out): follow point X
 * @out_z: (out): follow point Z
 *
 * Computes owner + dir(facing + follow-angle) * follow-distance where
 * dir(a) = (-sin a, -cos a). With the default angle (3*pi/4) the point is
 * behind-left of the owner.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_companion_brain_follow_point (LrgCompanionBrain *self,
                                  gdouble            owner_x,
                                  gdouble            owner_z,
                                  gdouble            owner_facing,
                                  gdouble           *out_x,
                                  gdouble           *out_z);

G_END_DECLS
