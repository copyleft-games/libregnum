/* lrg-ai-component.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgAiComponent: runs a .rbt behaviour tree and/or a .rfsm logic state
 * machine on an LrgEntity3D (or a 2D LrgGameObject: LrgComponent serves
 * both owners) over one shared blackboard.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../../lrg-version.h"
#include "../../ecs/lrg-component.h"
#include "../../ai/lrg-blackboard.h"
#include "../../ai/lrg-bt-asset.h"
#include "../../ai/lrg-logic-machine.h"
#include "../../scripting/lrg-scripting.h"

G_BEGIN_DECLS

/**
 * LRG_AI_COMPONENT_OWNER_KEY:
 *
 * The blackboard key under which the component stores its owner (the
 * #LrgEntity3D or #LrgGameObject) as a pointer while attached: C actions
 * read it with `lrg_blackboard_get_pointer (bb, LRG_AI_COMPONENT_OWNER_KEY)`.
 * It is a plain pointer (no reference) so the blackboard does not keep its
 * owner alive.
 *
 * Since: 0.9.0
 */
#define LRG_AI_COMPONENT_OWNER_KEY "owner"

#define LRG_TYPE_AI_COMPONENT (lrg_ai_component_get_type ())

/**
 * LrgAiComponent:
 *
 * Gives an entity a brain. The component owns a blackboard; on its first
 * update it loads its `.rbt` / `.rfsm` files (when given paths), builds an
 * #LrgBtRunner for the tree and starts the machine, both on that
 * blackboard. Every update (or every #LrgAiComponent:tick-interval
 * seconds, with the accumulated time) it updates the machine first and
 * then ticks the tree. Script leaves and actions call into
 * #LrgAiComponent:scripting, or, when that is unset, into the context of
 * an #LrgScriptComponent on the same owner.
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgAiComponent, lrg_ai_component, LRG, AI_COMPONENT, LrgComponent)

/**
 * lrg_ai_component_new:
 *
 * Returns: (transfer full): a new component with no tree or machine
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
LrgAiComponent *    lrg_ai_component_new                (void);

/**
 * lrg_ai_component_new_with_tree:
 * @tree: a behaviour tree asset
 *
 * Returns: (transfer full): a new component running @tree
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
LrgAiComponent *    lrg_ai_component_new_with_tree      (LrgBtAsset *tree);

/**
 * lrg_ai_component_new_with_machine:
 * @machine: a logic state machine (the component runs this instance)
 *
 * Returns: (transfer full): a new component running @machine
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
LrgAiComponent *    lrg_ai_component_new_with_machine   (LrgLogicMachine *machine);

/**
 * lrg_ai_component_get_tree:
 * @self: an #LrgAiComponent
 *
 * Returns: (transfer none) (nullable): the tree asset
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
LrgBtAsset *        lrg_ai_component_get_tree           (LrgAiComponent *self);

/**
 * lrg_ai_component_set_tree:
 * @self: an #LrgAiComponent
 * @tree: (nullable): the tree asset (shared: assets hold no runtime state)
 *
 * Sets the tree; the runner is rebuilt on the next update.
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
void                lrg_ai_component_set_tree           (LrgAiComponent *self,
                                                         LrgBtAsset     *tree);

/**
 * lrg_ai_component_get_tree_path:
 * @self: an #LrgAiComponent
 *
 * Returns: (transfer none) (nullable): the `.rbt` file loaded on the next
 *   update
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
const gchar *       lrg_ai_component_get_tree_path      (LrgAiComponent *self);

/**
 * lrg_ai_component_set_tree_path:
 * @self: an #LrgAiComponent
 * @path: (nullable): a `.rbt` file
 *
 * Replaces the tree with the file's, loaded on the next update.
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
void                lrg_ai_component_set_tree_path      (LrgAiComponent *self,
                                                         const gchar    *path);

/**
 * lrg_ai_component_get_machine:
 * @self: an #LrgAiComponent
 *
 * Returns: (transfer none) (nullable): the state machine
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
LrgLogicMachine *   lrg_ai_component_get_machine        (LrgAiComponent *self);

/**
 * lrg_ai_component_set_machine:
 * @self: an #LrgAiComponent
 * @machine: (nullable): the machine; the component starts and runs this
 *   instance, so do not share one machine between components
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
void                lrg_ai_component_set_machine        (LrgAiComponent  *self,
                                                         LrgLogicMachine *machine);

/**
 * lrg_ai_component_get_machine_path:
 * @self: an #LrgAiComponent
 *
 * Returns: (transfer none) (nullable): the `.rfsm` file loaded on the next
 *   update
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
const gchar *       lrg_ai_component_get_machine_path   (LrgAiComponent *self);

/**
 * lrg_ai_component_set_machine_path:
 * @self: an #LrgAiComponent
 * @path: (nullable): a `.rfsm` file
 *
 * Replaces the machine with a fresh one loaded from the file on the next
 * update.
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
void                lrg_ai_component_set_machine_path   (LrgAiComponent *self,
                                                         const gchar    *path);

/**
 * lrg_ai_component_get_scripting:
 * @self: an #LrgAiComponent
 *
 * Returns: (transfer none) (nullable): the explicit scripting context
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
LrgScripting *      lrg_ai_component_get_scripting      (LrgAiComponent *self);

/**
 * lrg_ai_component_set_scripting:
 * @self: an #LrgAiComponent
 * @scripting: (nullable): the context script entries call into
 *
 * Takes effect on the next (re)start.
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
void                lrg_ai_component_set_scripting      (LrgAiComponent *self,
                                                         LrgScripting   *scripting);

/**
 * lrg_ai_component_get_tick_interval:
 * @self: an #LrgAiComponent
 *
 * Returns: the seconds between AI steps (0: every update)
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
gfloat              lrg_ai_component_get_tick_interval  (LrgAiComponent *self);

/**
 * lrg_ai_component_set_tick_interval:
 * @self: an #LrgAiComponent
 * @seconds: the seconds between AI steps (0: every update). Each step
 *   gets the time accumulated since the previous one.
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
void                lrg_ai_component_set_tick_interval  (LrgAiComponent *self,
                                                         gfloat          seconds);

/**
 * lrg_ai_component_get_use_fixed_update:
 * @self: an #LrgAiComponent
 *
 * Returns: whether the AI steps in the fixed update
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
gboolean            lrg_ai_component_get_use_fixed_update (LrgAiComponent *self);

/**
 * lrg_ai_component_set_use_fixed_update:
 * @self: an #LrgAiComponent
 * @use_fixed: %TRUE to step in the #LrgWorld3D fixed update (deterministic
 *   steps; only #LrgEntity3D owners get fixed updates), %FALSE for the
 *   frame update
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
void                lrg_ai_component_set_use_fixed_update (LrgAiComponent *self,
                                                           gboolean        use_fixed);

/**
 * lrg_ai_component_get_running:
 * @self: an #LrgAiComponent
 *
 * Returns: %FALSE while paused
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
gboolean            lrg_ai_component_get_running        (LrgAiComponent *self);

/**
 * lrg_ai_component_set_running:
 * @self: an #LrgAiComponent
 * @running: %FALSE to pause the AI (state is kept), %TRUE to resume
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
void                lrg_ai_component_set_running        (LrgAiComponent *self,
                                                         gboolean        running);

/**
 * lrg_ai_component_get_blackboard:
 * @self: an #LrgAiComponent
 *
 * The blackboard the tree and the machine share. Set inputs on it
 * (`lrg_blackboard_set_float (bb, "health", 40)`) and read outputs.
 *
 * Returns: (transfer none): the blackboard
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
LrgBlackboard *     lrg_ai_component_get_blackboard     (LrgAiComponent *self);

/**
 * lrg_ai_component_get_runner:
 * @self: an #LrgAiComponent
 *
 * Returns: (transfer none) (nullable): the tree's runner (per-node live
 *   state), %NULL before the first update or without a tree
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
LrgBtRunner *       lrg_ai_component_get_runner         (LrgAiComponent *self);

/**
 * lrg_ai_component_send_event:
 * @self: an #LrgAiComponent
 * @event: an event name
 *
 * Queues @event on the machine (see lrg_logic_machine_send_event()).
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
void                lrg_ai_component_send_event         (LrgAiComponent *self,
                                                         const gchar    *event);

/**
 * lrg_ai_component_get_current_node:
 * @self: an #LrgAiComponent
 *
 * Returns: (transfer none) (nullable): the tree's current node id (see
 *   lrg_bt_runner_get_current_node())
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
const gchar *       lrg_ai_component_get_current_node   (LrgAiComponent *self);

/**
 * lrg_ai_component_get_current_state:
 * @self: an #LrgAiComponent
 *
 * Returns: (transfer none) (nullable): the machine's innermost active
 *   state path
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
const gchar *       lrg_ai_component_get_current_state  (LrgAiComponent *self);

/**
 * lrg_ai_component_restart:
 * @self: an #LrgAiComponent
 *
 * Drops the runner and stops the machine; the next update rebuilds and
 * restarts them (reloading files given by path). The blackboard is kept.
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
void                lrg_ai_component_restart            (LrgAiComponent *self);

/**
 * lrg_ai_component_step:
 * @self: an #LrgAiComponent
 * @delta_time: seconds
 *
 * Steps the AI by hand (what the update does): starts it if needed, then
 * honours the tick interval, updates the machine and ticks the tree. For
 * tests, tools and owners that are not in a world.
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
void                lrg_ai_component_step               (LrgAiComponent *self,
                                                         gfloat          delta_time);

/**
 * lrg_ai_component_dup_debug_state:
 * @self: an #LrgAiComponent
 *
 * The component's live state as YAML: `tree:` (the runner's debug state,
 * or `~`), `machine:` (the machine's, or `~`) and `blackboard:` (every
 * key with its value text).
 *
 * Returns: (transfer full): the text
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
gchar *             lrg_ai_component_dup_debug_state    (LrgAiComponent *self);

G_END_DECLS
