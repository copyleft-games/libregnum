/* lrg-transition.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Abstract base class for scene transitions.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-enums.h"

G_BEGIN_DECLS

#define LRG_TYPE_TRANSITION (lrg_transition_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_DERIVABLE_TYPE (LrgTransition, lrg_transition, LRG, TRANSITION, GObject)

/**
 * LrgTransitionClass:
 * @parent_class: Parent class
 * @initialize: Initialize resources (called before first use)
 * @shutdown: Free resources
 * @start: Begin the transition
 * @update: Update the transition state
 * @render: Render the transition effect
 * @reset: Reset to initial state
 *
 * The class structure for #LrgTransition.
 * Subclasses implement specific transition effects (fade, wipe, dissolve, etc.).
 */
struct _LrgTransitionClass
{
    GObjectClass parent_class;

    /* Virtual methods */

    /**
     * LrgTransitionClass::initialize:
     * @self: A #LrgTransition
     * @width: Viewport width
     * @height: Viewport height
     * @error: (nullable): Return location for error
     *
     * Initializes the transition's resources (textures, shaders, etc.).
     * Called once before the transition is used.
     *
     * Returns: %TRUE on success, %FALSE on error
     */
    gboolean (*initialize)  (LrgTransition   *self,
                             guint            width,
                             guint            height,
                             GError         **error);

    /**
     * LrgTransitionClass::shutdown:
     * @self: A #LrgTransition
     *
     * Frees resources allocated during initialize.
     */
    void     (*shutdown)    (LrgTransition   *self);

    /**
     * LrgTransitionClass::start:
     * @self: A #LrgTransition
     *
     * Begins the transition.
     */
    void     (*start)       (LrgTransition   *self);

    /**
     * LrgTransitionClass::update:
     * @self: A #LrgTransition
     * @delta_time: Time elapsed since last update in seconds
     *
     * Updates the transition state.
     */
    void     (*update)      (LrgTransition   *self,
                             gfloat           delta_time);

    /**
     * LrgTransitionClass::render:
     * @self: A #LrgTransition
     * @old_scene_texture: Texture ID of the outgoing scene
     * @new_scene_texture: Texture ID of the incoming scene (may be 0 during OUT phase)
     * @width: Viewport width
     * @height: Viewport height
     *
     * Renders the transition effect to the screen.
     */
    void     (*render)      (LrgTransition   *self,
                             guint            old_scene_texture,
                             guint            new_scene_texture,
                             guint            width,
                             guint            height);

    /**
     * LrgTransitionClass::reset:
     * @self: A #LrgTransition
     *
     * Resets the transition to its initial state.
     */
    void     (*reset)       (LrgTransition   *self);

    /*< private >*/
    gpointer _reserved[8];
};

/* Lifecycle */

/**
 * lrg_transition_initialize:
 * @self: A #LrgTransition
 * @width: Viewport width
 * @height: Viewport height
 * @error: (nullable): Return location for error
 *
 * Initializes the transition's resources.
 *
 * Returns: %TRUE on success, %FALSE on error
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean            lrg_transition_initialize       (LrgTransition   *self,
                                                     guint            width,
                                                     guint            height,
                                                     GError         **error);

/**
 * lrg_transition_shutdown:
 * @self: A #LrgTransition
 *
 * Frees resources allocated during initialization.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void                lrg_transition_shutdown         (LrgTransition   *self);

/**
 * lrg_transition_start:
 * @self: A #LrgTransition
 *
 * Begins the transition. The transition will go through OUT, HOLD, and IN phases.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void                lrg_transition_start            (LrgTransition   *self);

/**
 * lrg_transition_update:
 * @self: A #LrgTransition
 * @delta_time: Time elapsed since last update in seconds
 *
 * Updates the transition state.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void                lrg_transition_update           (LrgTransition   *self,
                                                     gfloat           delta_time);

/**
 * lrg_transition_render:
 * @self: A #LrgTransition
 * @old_scene_texture: Texture ID of the outgoing scene
 * @new_scene_texture: Texture ID of the incoming scene
 * @width: Viewport width
 * @height: Viewport height
 *
 * Renders the transition effect.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void                lrg_transition_render           (LrgTransition   *self,
                                                     guint            old_scene_texture,
                                                     guint            new_scene_texture,
                                                     guint            width,
                                                     guint            height);

/**
 * lrg_transition_reset:
 * @self: A #LrgTransition
 *
 * Resets the transition to its initial state.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void                lrg_transition_reset            (LrgTransition   *self);

/* State queries */

/**
 * lrg_transition_get_state:
 * @self: A #LrgTransition
 *
 * Gets the current transition state.
 *
 * Returns: The current #LrgTransitionState
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgTransitionState  lrg_transition_get_state        (LrgTransition   *self);

/**
 * lrg_transition_is_complete:
 * @self: A #LrgTransition
 *
 * Checks if the transition has completed.
 *
 * Returns: %TRUE if complete
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean            lrg_transition_is_complete      (LrgTransition   *self);

/**
 * lrg_transition_is_at_midpoint:
 * @self: A #LrgTransition
 *
 * Checks if the transition is at the midpoint (HOLD state).
 * This is when the scene should be switched.
 *
 * Returns: %TRUE if at midpoint
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean            lrg_transition_is_at_midpoint   (LrgTransition   *self);

/* Timing properties */

/**
 * lrg_transition_get_duration:
 * @self: A #LrgTransition
 *
 * Gets the total duration of the transition (out + hold + in).
 *
 * Returns: Duration in seconds
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat              lrg_transition_get_duration     (LrgTransition   *self);

/**
 * lrg_transition_set_duration:
 * @self: A #LrgTransition
 * @duration: Duration in seconds
 *
 * Sets the total duration of the transition.
 * The out, hold, and in phases will be proportionally adjusted.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void                lrg_transition_set_duration     (LrgTransition   *self,
                                                     gfloat           duration);

/**
 * lrg_transition_get_out_duration:
 * @self: A #LrgTransition
 *
 * Gets the duration of the OUT phase.
 *
 * Returns: Duration in seconds
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat              lrg_transition_get_out_duration (LrgTransition   *self);

/**
 * lrg_transition_set_out_duration:
 * @self: A #LrgTransition
 * @duration: Duration in seconds
 *
 * Sets the duration of the OUT phase.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void                lrg_transition_set_out_duration (LrgTransition   *self,
                                                     gfloat           duration);

/**
 * lrg_transition_get_hold_duration:
 * @self: A #LrgTransition
 *
 * Gets the duration of the HOLD phase.
 *
 * Returns: Duration in seconds
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat              lrg_transition_get_hold_duration (LrgTransition  *self);

/**
 * lrg_transition_set_hold_duration:
 * @self: A #LrgTransition
 * @duration: Duration in seconds
 *
 * Sets the duration of the HOLD phase.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void                lrg_transition_set_hold_duration (LrgTransition  *self,
                                                      gfloat          duration);

/**
 * lrg_transition_get_in_duration:
 * @self: A #LrgTransition
 *
 * Gets the duration of the IN phase.
 *
 * Returns: Duration in seconds
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat              lrg_transition_get_in_duration  (LrgTransition   *self);

/**
 * lrg_transition_set_in_duration:
 * @self: A #LrgTransition
 * @duration: Duration in seconds
 *
 * Sets the duration of the IN phase.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void                lrg_transition_set_in_duration  (LrgTransition   *self,
                                                     gfloat           duration);

/**
 * lrg_transition_get_progress:
 * @self: A #LrgTransition
 *
 * Gets the overall progress of the transition (0.0 to 1.0).
 *
 * Returns: Progress value
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat              lrg_transition_get_progress     (LrgTransition   *self);

/**
 * lrg_transition_get_phase_progress:
 * @self: A #LrgTransition
 *
 * Gets the progress within the current phase (0.0 to 1.0).
 *
 * Returns: Phase progress value
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat              lrg_transition_get_phase_progress (LrgTransition *self);

/* Easing */

/**
 * lrg_transition_get_easing:
 * @self: A #LrgTransition
 *
 * Gets the easing function type used for the transition.
 *
 * Returns: The #LrgEasingType
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgEasingType       lrg_transition_get_easing       (LrgTransition   *self);

/**
 * lrg_transition_set_easing:
 * @self: A #LrgTransition
 * @easing: The easing function type
 *
 * Sets the easing function to use for the transition.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void                lrg_transition_set_easing       (LrgTransition   *self,
                                                     LrgEasingType    easing);

G_END_DECLS
