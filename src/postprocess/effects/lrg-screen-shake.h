/* lrg-screen-shake.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Screen shake post-processing effect.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-post-effect.h"

G_BEGIN_DECLS

#define LRG_TYPE_SCREEN_SHAKE (lrg_screen_shake_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgScreenShake, lrg_screen_shake, LRG, SCREEN_SHAKE, LrgPostEffect)

/**
 * lrg_screen_shake_new:
 *
 * Creates a new screen shake effect.
 *
 * Returns: (transfer full): A new #LrgScreenShake
 */
LRG_AVAILABLE_IN_ALL
LrgScreenShake *    lrg_screen_shake_new            (void);

/**
 * lrg_screen_shake_add_trauma:
 * @self: A #LrgScreenShake
 * @amount: Trauma to add (0.0 to 1.0)
 *
 * Adds trauma to the shake. Trauma is squared for shake
 * intensity, creating a smooth falloff.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_screen_shake_add_trauma     (LrgScreenShake *self,
                                                     gfloat          amount);

/**
 * lrg_screen_shake_get_trauma:
 * @self: A #LrgScreenShake
 *
 * Gets the current trauma level.
 *
 * Returns: The trauma (0.0 to 1.0)
 */
LRG_AVAILABLE_IN_ALL
gfloat              lrg_screen_shake_get_trauma     (LrgScreenShake *self);

/**
 * lrg_screen_shake_set_trauma:
 * @self: A #LrgScreenShake
 * @trauma: The trauma level (0.0 to 1.0)
 *
 * Sets the trauma level directly.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_screen_shake_set_trauma     (LrgScreenShake *self,
                                                     gfloat          trauma);

/**
 * lrg_screen_shake_get_decay:
 * @self: A #LrgScreenShake
 *
 * Gets the trauma decay rate.
 *
 * Returns: The decay rate per second
 */
LRG_AVAILABLE_IN_ALL
gfloat              lrg_screen_shake_get_decay      (LrgScreenShake *self);

/**
 * lrg_screen_shake_set_decay:
 * @self: A #LrgScreenShake
 * @decay: Trauma decay per second
 *
 * Sets how fast trauma decays.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_screen_shake_set_decay      (LrgScreenShake *self,
                                                     gfloat          decay);

/**
 * lrg_screen_shake_get_max_offset:
 * @self: A #LrgScreenShake
 * @x: (out) (optional): Max X offset in pixels
 * @y: (out) (optional): Max Y offset in pixels
 *
 * Gets the maximum shake offset.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_screen_shake_get_max_offset (LrgScreenShake *self,
                                                     gfloat         *x,
                                                     gfloat         *y);

/**
 * lrg_screen_shake_set_max_offset:
 * @self: A #LrgScreenShake
 * @x: Max X offset in pixels
 * @y: Max Y offset in pixels
 *
 * Sets the maximum shake offset at full trauma.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_screen_shake_set_max_offset (LrgScreenShake *self,
                                                     gfloat          x,
                                                     gfloat          y);

/**
 * lrg_screen_shake_get_max_rotation:
 * @self: A #LrgScreenShake
 *
 * Gets the maximum rotation angle in degrees.
 *
 * Returns: Max rotation in degrees
 */
LRG_AVAILABLE_IN_ALL
gfloat              lrg_screen_shake_get_max_rotation (LrgScreenShake *self);

/**
 * lrg_screen_shake_set_max_rotation:
 * @self: A #LrgScreenShake
 * @degrees: Max rotation in degrees
 *
 * Sets the maximum rotation angle at full trauma.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_screen_shake_set_max_rotation (LrgScreenShake *self,
                                                       gfloat          degrees);

/**
 * lrg_screen_shake_get_frequency:
 * @self: A #LrgScreenShake
 *
 * Gets the shake frequency.
 *
 * Returns: Shake frequency in Hz
 */
LRG_AVAILABLE_IN_ALL
gfloat              lrg_screen_shake_get_frequency  (LrgScreenShake *self);

/**
 * lrg_screen_shake_set_frequency:
 * @self: A #LrgScreenShake
 * @frequency: Shake frequency in Hz
 *
 * Sets how fast the shake oscillates.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_screen_shake_set_frequency  (LrgScreenShake *self,
                                                     gfloat          frequency);

/**
 * lrg_screen_shake_update:
 * @self: A #LrgScreenShake
 * @delta_time: Time since last update in seconds
 *
 * Updates the shake effect (decays trauma, updates noise).
 */
LRG_AVAILABLE_IN_ALL
void                lrg_screen_shake_update         (LrgScreenShake *self,
                                                     gfloat          delta_time);

/**
 * lrg_screen_shake_get_current_offset:
 * @self: A #LrgScreenShake
 * @x: (out) (optional): Current X offset
 * @y: (out) (optional): Current Y offset
 *
 * Gets the current shake offset for this frame.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_screen_shake_get_current_offset (LrgScreenShake *self,
                                                         gfloat         *x,
                                                         gfloat         *y);

/**
 * lrg_screen_shake_get_current_rotation:
 * @self: A #LrgScreenShake
 *
 * Gets the current rotation for this frame.
 *
 * Returns: Current rotation in degrees
 */
LRG_AVAILABLE_IN_ALL
gfloat              lrg_screen_shake_get_current_rotation (LrgScreenShake *self);

G_END_DECLS
