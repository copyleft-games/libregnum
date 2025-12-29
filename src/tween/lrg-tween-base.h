/* lrg-tween-base.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Abstract base class for all tween types.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-enums.h"

G_BEGIN_DECLS

#define LRG_TYPE_TWEEN_BASE (lrg_tween_base_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_DERIVABLE_TYPE (LrgTweenBase, lrg_tween_base, LRG, TWEEN_BASE, GObject)

/**
 * LrgTweenBaseClass:
 * @parent_class: Parent class
 * @start: Virtual method to start the tween
 * @stop: Virtual method to stop the tween
 * @pause: Virtual method to pause the tween
 * @resume: Virtual method to resume the tween
 * @update: Virtual method called each frame with delta time
 * @reset: Virtual method to reset the tween to initial state
 * @is_finished: Virtual method to check if the tween has completed
 *
 * The class structure for #LrgTweenBase.
 * Subclasses should override these methods to implement specific tween behavior.
 */
struct _LrgTweenBaseClass
{
    GObjectClass parent_class;

    /* Virtual methods */

    /**
     * LrgTweenBaseClass::start:
     * @self: A #LrgTweenBase
     *
     * Starts the tween playback.
     * Subclasses should chain up to parent implementation.
     */
    void        (*start)        (LrgTweenBase    *self);

    /**
     * LrgTweenBaseClass::stop:
     * @self: A #LrgTweenBase
     *
     * Stops the tween playback and resets to initial state.
     * Subclasses should chain up to parent implementation.
     */
    void        (*stop)         (LrgTweenBase    *self);

    /**
     * LrgTweenBaseClass::pause:
     * @self: A #LrgTweenBase
     *
     * Pauses the tween at its current position.
     * Subclasses should chain up to parent implementation.
     */
    void        (*pause)        (LrgTweenBase    *self);

    /**
     * LrgTweenBaseClass::resume:
     * @self: A #LrgTweenBase
     *
     * Resumes the tween from its paused position.
     * Subclasses should chain up to parent implementation.
     */
    void        (*resume)       (LrgTweenBase    *self);

    /**
     * LrgTweenBaseClass::update:
     * @self: A #LrgTweenBase
     * @delta_time: Time elapsed since last update in seconds
     *
     * Updates the tween state. Called each frame.
     * Subclasses MUST implement this to perform actual animation.
     */
    void        (*update)       (LrgTweenBase    *self,
                                 gfloat           delta_time);

    /**
     * LrgTweenBaseClass::reset:
     * @self: A #LrgTweenBase
     *
     * Resets the tween to its initial state without starting.
     * Subclasses should chain up to parent implementation.
     */
    void        (*reset)        (LrgTweenBase    *self);

    /**
     * LrgTweenBaseClass::is_finished:
     * @self: A #LrgTweenBase
     *
     * Checks if the tween has completed all iterations.
     *
     * Returns: %TRUE if finished
     */
    gboolean    (*is_finished)  (LrgTweenBase    *self);

    /*< private >*/
    gpointer _reserved[8];
};

/* Lifecycle methods */

/**
 * lrg_tween_base_start:
 * @self: A #LrgTweenBase
 *
 * Starts the tween playback.
 * If the tween is paused, this will restart from the beginning.
 * Use lrg_tween_base_resume() to continue from a paused state.
 *
 * Emits the #LrgTweenBase::started signal.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void            lrg_tween_base_start            (LrgTweenBase    *self);

/**
 * lrg_tween_base_stop:
 * @self: A #LrgTweenBase
 *
 * Stops the tween and resets it to the initial state.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void            lrg_tween_base_stop             (LrgTweenBase    *self);

/**
 * lrg_tween_base_pause:
 * @self: A #LrgTweenBase
 *
 * Pauses the tween at its current position.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void            lrg_tween_base_pause            (LrgTweenBase    *self);

/**
 * lrg_tween_base_resume:
 * @self: A #LrgTweenBase
 *
 * Resumes the tween from a paused state.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void            lrg_tween_base_resume           (LrgTweenBase    *self);

/**
 * lrg_tween_base_update:
 * @self: A #LrgTweenBase
 * @delta_time: Time elapsed since last update in seconds
 *
 * Updates the tween state. Should be called every frame.
 * This handles delay, elapsed time, progress calculation, and looping.
 *
 * Emits the #LrgTweenBase::updated signal after state update.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void            lrg_tween_base_update           (LrgTweenBase    *self,
                                                 gfloat           delta_time);

/**
 * lrg_tween_base_reset:
 * @self: A #LrgTweenBase
 *
 * Resets the tween to its initial state without starting.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void            lrg_tween_base_reset            (LrgTweenBase    *self);

/* State queries */

/**
 * lrg_tween_base_is_finished:
 * @self: A #LrgTweenBase
 *
 * Checks if the tween has completed all iterations.
 *
 * Returns: %TRUE if the tween has finished
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean        lrg_tween_base_is_finished      (LrgTweenBase    *self);

/**
 * lrg_tween_base_is_running:
 * @self: A #LrgTweenBase
 *
 * Checks if the tween is currently running.
 *
 * Returns: %TRUE if running
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean        lrg_tween_base_is_running       (LrgTweenBase    *self);

/**
 * lrg_tween_base_is_paused:
 * @self: A #LrgTweenBase
 *
 * Checks if the tween is currently paused.
 *
 * Returns: %TRUE if paused
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean        lrg_tween_base_is_paused        (LrgTweenBase    *self);

/**
 * lrg_tween_base_get_state:
 * @self: A #LrgTweenBase
 *
 * Gets the current state of the tween.
 *
 * Returns: The current #LrgTweenState
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgTweenState   lrg_tween_base_get_state        (LrgTweenBase    *self);

/* Timing properties */

/**
 * lrg_tween_base_get_duration:
 * @self: A #LrgTweenBase
 *
 * Gets the total duration of the tween in seconds.
 *
 * Returns: The duration in seconds
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat          lrg_tween_base_get_duration     (LrgTweenBase    *self);

/**
 * lrg_tween_base_set_duration:
 * @self: A #LrgTweenBase
 * @duration: Duration in seconds (must be > 0)
 *
 * Sets the total duration of the tween.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void            lrg_tween_base_set_duration     (LrgTweenBase    *self,
                                                 gfloat           duration);

/**
 * lrg_tween_base_get_delay:
 * @self: A #LrgTweenBase
 *
 * Gets the start delay of the tween in seconds.
 *
 * Returns: The delay in seconds
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat          lrg_tween_base_get_delay        (LrgTweenBase    *self);

/**
 * lrg_tween_base_set_delay:
 * @self: A #LrgTweenBase
 * @delay: Delay in seconds (>= 0)
 *
 * Sets the start delay before the tween begins animating.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void            lrg_tween_base_set_delay        (LrgTweenBase    *self,
                                                 gfloat           delay);

/**
 * lrg_tween_base_get_elapsed:
 * @self: A #LrgTweenBase
 *
 * Gets the time elapsed since the tween started animating
 * (after any delay has passed).
 *
 * Returns: Elapsed time in seconds
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat          lrg_tween_base_get_elapsed      (LrgTweenBase    *self);

/**
 * lrg_tween_base_get_progress:
 * @self: A #LrgTweenBase
 *
 * Gets the normalized progress of the tween (0.0 to 1.0).
 * This is the raw progress before easing is applied.
 *
 * Returns: Progress value between 0.0 and 1.0
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat          lrg_tween_base_get_progress     (LrgTweenBase    *self);

/* Easing */

/**
 * lrg_tween_base_get_easing:
 * @self: A #LrgTweenBase
 *
 * Gets the easing function type.
 *
 * Returns: The #LrgEasingType
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgEasingType   lrg_tween_base_get_easing       (LrgTweenBase    *self);

/**
 * lrg_tween_base_set_easing:
 * @self: A #LrgTweenBase
 * @easing: The easing function type
 *
 * Sets the easing function to use for interpolation.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void            lrg_tween_base_set_easing       (LrgTweenBase    *self,
                                                 LrgEasingType    easing);

/* Looping */

/**
 * lrg_tween_base_get_loop_count:
 * @self: A #LrgTweenBase
 *
 * Gets the number of times to loop.
 * -1 means infinite looping.
 * 0 means no looping (play once).
 *
 * Returns: The loop count
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint            lrg_tween_base_get_loop_count   (LrgTweenBase    *self);

/**
 * lrg_tween_base_set_loop_count:
 * @self: A #LrgTweenBase
 * @count: Number of loops (-1 for infinite, 0 for none)
 *
 * Sets the number of times to loop the tween.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void            lrg_tween_base_set_loop_count   (LrgTweenBase    *self,
                                                 gint             count);

/**
 * lrg_tween_base_get_loop_mode:
 * @self: A #LrgTweenBase
 *
 * Gets the loop mode.
 *
 * Returns: The #LrgTweenLoopMode
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgTweenLoopMode lrg_tween_base_get_loop_mode   (LrgTweenBase    *self);

/**
 * lrg_tween_base_set_loop_mode:
 * @self: A #LrgTweenBase
 * @mode: The loop mode
 *
 * Sets the loop mode (restart or ping-pong).
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void            lrg_tween_base_set_loop_mode    (LrgTweenBase    *self,
                                                 LrgTweenLoopMode mode);

/**
 * lrg_tween_base_get_current_loop:
 * @self: A #LrgTweenBase
 *
 * Gets the current loop iteration (0-based).
 *
 * Returns: The current loop number
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint            lrg_tween_base_get_current_loop (LrgTweenBase    *self);

/* Auto-start */

/**
 * lrg_tween_base_get_auto_start:
 * @self: A #LrgTweenBase
 *
 * Gets whether the tween starts automatically when added to a manager.
 *
 * Returns: %TRUE if auto-start is enabled
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean        lrg_tween_base_get_auto_start   (LrgTweenBase    *self);

/**
 * lrg_tween_base_set_auto_start:
 * @self: A #LrgTweenBase
 * @auto_start: Whether to auto-start
 *
 * Sets whether the tween should start automatically when added to a manager.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void            lrg_tween_base_set_auto_start   (LrgTweenBase    *self,
                                                 gboolean         auto_start);

G_END_DECLS
