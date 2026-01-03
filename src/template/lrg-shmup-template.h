/* lrg-shmup-template.h - Scrolling shooter (shmup) game template
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * This file is part of Libregnum.
 *
 * Template for scrolling shooter (shmup) games.
 *
 * This template extends LrgShooter2DTemplate with shmup-specific features:
 * - Automatic vertical or horizontal scrolling
 * - Power-up collection and weapon upgrades
 * - Bomb/super weapon system
 * - Lives and continues
 * - Bullet grazing (scoring bonus for near-misses)
 * - Boss encounter support
 *
 * Use for games like Gradius, R-Type, Touhou, or any scrolling shooter.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-types.h"
#include "lrg-shooter-2d-template.h"

G_BEGIN_DECLS

#define LRG_TYPE_SHMUP_TEMPLATE (lrg_shmup_template_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_DERIVABLE_TYPE (LrgShmupTemplate, lrg_shmup_template,
                          LRG, SHMUP_TEMPLATE, LrgShooter2DTemplate)

/**
 * LrgShmupTemplateClass:
 * @parent_class: parent class
 * @on_life_lost: called when player loses a life
 * @on_bomb_used: called when player uses a bomb
 * @on_power_changed: called when power level changes
 * @on_graze: called when player grazes a bullet
 *
 * Class structure for #LrgShmupTemplate.
 *
 * Subclasses can override these virtual methods to customize
 * shmup behavior.
 */
struct _LrgShmupTemplateClass
{
    LrgShooter2DTemplateClass parent_class;

    /*< public >*/

    /**
     * LrgShmupTemplateClass::on_life_lost:
     * @self: a #LrgShmupTemplate
     * @remaining: remaining lives
     *
     * Called when the player loses a life. Override to add death effects.
     */
    void (*on_life_lost) (LrgShmupTemplate *self,
                          gint              remaining);

    /**
     * LrgShmupTemplateClass::on_bomb_used:
     * @self: a #LrgShmupTemplate
     * @remaining: remaining bombs
     *
     * Called when a bomb is used. Override to add bomb effects.
     */
    void (*on_bomb_used) (LrgShmupTemplate *self,
                          gint              remaining);

    /**
     * LrgShmupTemplateClass::on_power_changed:
     * @self: a #LrgShmupTemplate
     * @new_level: new power level
     *
     * Called when power level changes. Override to update weapons.
     */
    void (*on_power_changed) (LrgShmupTemplate *self,
                              gint              new_level);

    /**
     * LrgShmupTemplateClass::on_graze:
     * @self: a #LrgShmupTemplate
     * @graze_count: total graze count
     *
     * Called when player grazes a bullet. Override for graze effects.
     */
    void (*on_graze) (LrgShmupTemplate *self,
                      guint             graze_count);

    /*< private >*/
    gpointer _reserved[8];
};

/**
 * LrgShmupScrollDirection:
 * @LRG_SHMUP_SCROLL_UP: Scroll upward (vertical shmup)
 * @LRG_SHMUP_SCROLL_DOWN: Scroll downward
 * @LRG_SHMUP_SCROLL_LEFT: Scroll left
 * @LRG_SHMUP_SCROLL_RIGHT: Scroll right (horizontal shmup)
 * @LRG_SHMUP_SCROLL_NONE: No automatic scrolling
 *
 * Scrolling directions for the shmup template.
 */
typedef enum
{
    LRG_SHMUP_SCROLL_UP,
    LRG_SHMUP_SCROLL_DOWN,
    LRG_SHMUP_SCROLL_LEFT,
    LRG_SHMUP_SCROLL_RIGHT,
    LRG_SHMUP_SCROLL_NONE
} LrgShmupScrollDirection;

/* ==========================================================================
 * Constructor
 * ========================================================================== */

/**
 * lrg_shmup_template_new:
 *
 * Creates a new shmup template with default settings.
 *
 * Returns: (transfer full): a new #LrgShmupTemplate
 */
LRG_AVAILABLE_IN_ALL
LrgShmupTemplate * lrg_shmup_template_new (void);

/* ==========================================================================
 * Scrolling
 * ========================================================================== */

/**
 * lrg_shmup_template_get_scroll_direction:
 * @self: a #LrgShmupTemplate
 *
 * Gets the automatic scroll direction.
 *
 * Returns: the #LrgShmupScrollDirection
 */
LRG_AVAILABLE_IN_ALL
LrgShmupScrollDirection lrg_shmup_template_get_scroll_direction (LrgShmupTemplate *self);

/**
 * lrg_shmup_template_set_scroll_direction:
 * @self: a #LrgShmupTemplate
 * @direction: the scroll direction
 *
 * Sets the automatic scroll direction.
 */
LRG_AVAILABLE_IN_ALL
void lrg_shmup_template_set_scroll_direction (LrgShmupTemplate       *self,
                                               LrgShmupScrollDirection direction);

/**
 * lrg_shmup_template_get_scroll_speed:
 * @self: a #LrgShmupTemplate
 *
 * Gets the scroll speed in units per second.
 *
 * Returns: scroll speed
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_shmup_template_get_scroll_speed (LrgShmupTemplate *self);

/**
 * lrg_shmup_template_set_scroll_speed:
 * @self: a #LrgShmupTemplate
 * @speed: scroll speed in units per second
 *
 * Sets the scroll speed.
 */
LRG_AVAILABLE_IN_ALL
void lrg_shmup_template_set_scroll_speed (LrgShmupTemplate *self,
                                           gfloat            speed);

/**
 * lrg_shmup_template_get_scroll_position:
 * @self: a #LrgShmupTemplate
 *
 * Gets the current scroll position (level progress).
 *
 * Returns: scroll position in units
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_shmup_template_get_scroll_position (LrgShmupTemplate *self);

/**
 * lrg_shmup_template_set_scroll_position:
 * @self: a #LrgShmupTemplate
 * @position: scroll position in units
 *
 * Sets the current scroll position.
 */
LRG_AVAILABLE_IN_ALL
void lrg_shmup_template_set_scroll_position (LrgShmupTemplate *self,
                                              gfloat            position);

/**
 * lrg_shmup_template_get_scroll_paused:
 * @self: a #LrgShmupTemplate
 *
 * Gets whether scrolling is paused (e.g., during boss battles).
 *
 * Returns: %TRUE if scrolling is paused
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_shmup_template_get_scroll_paused (LrgShmupTemplate *self);

/**
 * lrg_shmup_template_set_scroll_paused:
 * @self: a #LrgShmupTemplate
 * @paused: whether to pause scrolling
 *
 * Pauses or resumes automatic scrolling.
 */
LRG_AVAILABLE_IN_ALL
void lrg_shmup_template_set_scroll_paused (LrgShmupTemplate *self,
                                            gboolean          paused);

/* ==========================================================================
 * Lives & Continues
 * ========================================================================== */

/**
 * lrg_shmup_template_get_lives:
 * @self: a #LrgShmupTemplate
 *
 * Gets the current number of lives.
 *
 * Returns: number of lives remaining
 */
LRG_AVAILABLE_IN_ALL
gint lrg_shmup_template_get_lives (LrgShmupTemplate *self);

/**
 * lrg_shmup_template_set_lives:
 * @self: a #LrgShmupTemplate
 * @lives: number of lives
 *
 * Sets the current number of lives.
 */
LRG_AVAILABLE_IN_ALL
void lrg_shmup_template_set_lives (LrgShmupTemplate *self,
                                    gint              lives);

/**
 * lrg_shmup_template_get_max_lives:
 * @self: a #LrgShmupTemplate
 *
 * Gets the maximum number of lives.
 *
 * Returns: maximum lives
 */
LRG_AVAILABLE_IN_ALL
gint lrg_shmup_template_get_max_lives (LrgShmupTemplate *self);

/**
 * lrg_shmup_template_set_max_lives:
 * @self: a #LrgShmupTemplate
 * @max_lives: maximum lives
 *
 * Sets the maximum number of lives.
 */
LRG_AVAILABLE_IN_ALL
void lrg_shmup_template_set_max_lives (LrgShmupTemplate *self,
                                        gint              max_lives);

/**
 * lrg_shmup_template_get_continues:
 * @self: a #LrgShmupTemplate
 *
 * Gets the number of continues remaining.
 *
 * Returns: number of continues
 */
LRG_AVAILABLE_IN_ALL
gint lrg_shmup_template_get_continues (LrgShmupTemplate *self);

/**
 * lrg_shmup_template_set_continues:
 * @self: a #LrgShmupTemplate
 * @continues: number of continues
 *
 * Sets the number of continues remaining.
 */
LRG_AVAILABLE_IN_ALL
void lrg_shmup_template_set_continues (LrgShmupTemplate *self,
                                        gint              continues);

/**
 * lrg_shmup_template_lose_life:
 * @self: a #LrgShmupTemplate
 *
 * Decrements lives. Emits "life-lost" signal.
 *
 * Returns: remaining lives (negative if game over)
 */
LRG_AVAILABLE_IN_ALL
gint lrg_shmup_template_lose_life (LrgShmupTemplate *self);

/**
 * lrg_shmup_template_use_continue:
 * @self: a #LrgShmupTemplate
 *
 * Uses a continue to restore lives.
 *
 * Returns: %TRUE if continue was used, %FALSE if none remaining
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_shmup_template_use_continue (LrgShmupTemplate *self);

/* ==========================================================================
 * Bombs / Super Weapons
 * ========================================================================== */

/**
 * lrg_shmup_template_get_bombs:
 * @self: a #LrgShmupTemplate
 *
 * Gets the current number of bombs/super weapons.
 *
 * Returns: number of bombs
 */
LRG_AVAILABLE_IN_ALL
gint lrg_shmup_template_get_bombs (LrgShmupTemplate *self);

/**
 * lrg_shmup_template_set_bombs:
 * @self: a #LrgShmupTemplate
 * @bombs: number of bombs
 *
 * Sets the current number of bombs.
 */
LRG_AVAILABLE_IN_ALL
void lrg_shmup_template_set_bombs (LrgShmupTemplate *self,
                                    gint              bombs);

/**
 * lrg_shmup_template_get_max_bombs:
 * @self: a #LrgShmupTemplate
 *
 * Gets the maximum number of bombs.
 *
 * Returns: maximum bombs
 */
LRG_AVAILABLE_IN_ALL
gint lrg_shmup_template_get_max_bombs (LrgShmupTemplate *self);

/**
 * lrg_shmup_template_set_max_bombs:
 * @self: a #LrgShmupTemplate
 * @max_bombs: maximum bombs
 *
 * Sets the maximum number of bombs.
 */
LRG_AVAILABLE_IN_ALL
void lrg_shmup_template_set_max_bombs (LrgShmupTemplate *self,
                                        gint              max_bombs);

/**
 * lrg_shmup_template_use_bomb:
 * @self: a #LrgShmupTemplate
 *
 * Uses a bomb. Emits "bomb-used" signal.
 *
 * Returns: %TRUE if bomb was used, %FALSE if none remaining
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_shmup_template_use_bomb (LrgShmupTemplate *self);

/**
 * lrg_shmup_template_is_bomb_active:
 * @self: a #LrgShmupTemplate
 *
 * Checks if a bomb effect is currently active.
 *
 * Returns: %TRUE if bomb is active
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_shmup_template_is_bomb_active (LrgShmupTemplate *self);

/**
 * lrg_shmup_template_get_bomb_duration:
 * @self: a #LrgShmupTemplate
 *
 * Gets the bomb effect duration.
 *
 * Returns: duration in seconds
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_shmup_template_get_bomb_duration (LrgShmupTemplate *self);

/**
 * lrg_shmup_template_set_bomb_duration:
 * @self: a #LrgShmupTemplate
 * @duration: duration in seconds
 *
 * Sets the bomb effect duration.
 */
LRG_AVAILABLE_IN_ALL
void lrg_shmup_template_set_bomb_duration (LrgShmupTemplate *self,
                                            gfloat            duration);

/* ==========================================================================
 * Power Level
 * ========================================================================== */

/**
 * lrg_shmup_template_get_power_level:
 * @self: a #LrgShmupTemplate
 *
 * Gets the current weapon power level.
 *
 * Returns: power level
 */
LRG_AVAILABLE_IN_ALL
gint lrg_shmup_template_get_power_level (LrgShmupTemplate *self);

/**
 * lrg_shmup_template_set_power_level:
 * @self: a #LrgShmupTemplate
 * @level: power level
 *
 * Sets the weapon power level.
 */
LRG_AVAILABLE_IN_ALL
void lrg_shmup_template_set_power_level (LrgShmupTemplate *self,
                                          gint              level);

/**
 * lrg_shmup_template_get_max_power_level:
 * @self: a #LrgShmupTemplate
 *
 * Gets the maximum power level.
 *
 * Returns: maximum power level
 */
LRG_AVAILABLE_IN_ALL
gint lrg_shmup_template_get_max_power_level (LrgShmupTemplate *self);

/**
 * lrg_shmup_template_set_max_power_level:
 * @self: a #LrgShmupTemplate
 * @max_level: maximum power level
 *
 * Sets the maximum power level.
 */
LRG_AVAILABLE_IN_ALL
void lrg_shmup_template_set_max_power_level (LrgShmupTemplate *self,
                                              gint              max_level);

/**
 * lrg_shmup_template_add_power:
 * @self: a #LrgShmupTemplate
 * @amount: power to add
 *
 * Adds power points. Automatically levels up when threshold is reached.
 */
LRG_AVAILABLE_IN_ALL
void lrg_shmup_template_add_power (LrgShmupTemplate *self,
                                    gint              amount);

/* ==========================================================================
 * Grazing (Bullet Dodging Bonus)
 * ========================================================================== */

/**
 * lrg_shmup_template_get_graze_count:
 * @self: a #LrgShmupTemplate
 *
 * Gets the total number of bullet grazes.
 *
 * Returns: graze count
 */
LRG_AVAILABLE_IN_ALL
guint lrg_shmup_template_get_graze_count (LrgShmupTemplate *self);

/**
 * lrg_shmup_template_add_graze:
 * @self: a #LrgShmupTemplate
 *
 * Records a bullet graze. Emits "bullet-grazed" signal.
 */
LRG_AVAILABLE_IN_ALL
void lrg_shmup_template_add_graze (LrgShmupTemplate *self);

/**
 * lrg_shmup_template_get_graze_radius:
 * @self: a #LrgShmupTemplate
 *
 * Gets the graze detection radius around the player.
 *
 * Returns: graze radius in units
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_shmup_template_get_graze_radius (LrgShmupTemplate *self);

/**
 * lrg_shmup_template_set_graze_radius:
 * @self: a #LrgShmupTemplate
 * @radius: graze radius in units
 *
 * Sets the graze detection radius.
 */
LRG_AVAILABLE_IN_ALL
void lrg_shmup_template_set_graze_radius (LrgShmupTemplate *self,
                                           gfloat            radius);

/**
 * lrg_shmup_template_get_graze_points:
 * @self: a #LrgShmupTemplate
 *
 * Gets the score points awarded per graze.
 *
 * Returns: points per graze
 */
LRG_AVAILABLE_IN_ALL
gint64 lrg_shmup_template_get_graze_points (LrgShmupTemplate *self);

/**
 * lrg_shmup_template_set_graze_points:
 * @self: a #LrgShmupTemplate
 * @points: points per graze
 *
 * Sets the score points awarded per graze.
 */
LRG_AVAILABLE_IN_ALL
void lrg_shmup_template_set_graze_points (LrgShmupTemplate *self,
                                           gint64            points);

/* ==========================================================================
 * Player Hitbox
 * ========================================================================== */

/**
 * lrg_shmup_template_get_hitbox_radius:
 * @self: a #LrgShmupTemplate
 *
 * Gets the player hitbox radius.
 *
 * Returns: hitbox radius in units
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_shmup_template_get_hitbox_radius (LrgShmupTemplate *self);

/**
 * lrg_shmup_template_set_hitbox_radius:
 * @self: a #LrgShmupTemplate
 * @radius: hitbox radius in units
 *
 * Sets the player hitbox radius.
 */
LRG_AVAILABLE_IN_ALL
void lrg_shmup_template_set_hitbox_radius (LrgShmupTemplate *self,
                                            gfloat            radius);

/**
 * lrg_shmup_template_get_show_hitbox:
 * @self: a #LrgShmupTemplate
 *
 * Gets whether the hitbox is visually displayed.
 *
 * Returns: %TRUE if hitbox is shown
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_shmup_template_get_show_hitbox (LrgShmupTemplate *self);

/**
 * lrg_shmup_template_set_show_hitbox:
 * @self: a #LrgShmupTemplate
 * @show: whether to show the hitbox
 *
 * Sets whether to visually display the player hitbox.
 */
LRG_AVAILABLE_IN_ALL
void lrg_shmup_template_set_show_hitbox (LrgShmupTemplate *self,
                                          gboolean          show);

/* ==========================================================================
 * Focus Mode (Slow Movement for Precision)
 * ========================================================================== */

/**
 * lrg_shmup_template_get_focus_speed_multiplier:
 * @self: a #LrgShmupTemplate
 *
 * Gets the speed multiplier when in focus mode.
 *
 * Returns: focus speed multiplier (0-1)
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_shmup_template_get_focus_speed_multiplier (LrgShmupTemplate *self);

/**
 * lrg_shmup_template_set_focus_speed_multiplier:
 * @self: a #LrgShmupTemplate
 * @multiplier: focus speed multiplier (0-1)
 *
 * Sets the speed multiplier when in focus mode.
 */
LRG_AVAILABLE_IN_ALL
void lrg_shmup_template_set_focus_speed_multiplier (LrgShmupTemplate *self,
                                                     gfloat            multiplier);

/**
 * lrg_shmup_template_is_focused:
 * @self: a #LrgShmupTemplate
 *
 * Checks if the player is in focus mode.
 *
 * Returns: %TRUE if in focus mode
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_shmup_template_is_focused (LrgShmupTemplate *self);

/**
 * lrg_shmup_template_set_focused:
 * @self: a #LrgShmupTemplate
 * @focused: whether to enable focus mode
 *
 * Enables or disables focus mode.
 */
LRG_AVAILABLE_IN_ALL
void lrg_shmup_template_set_focused (LrgShmupTemplate *self,
                                      gboolean          focused);

G_END_DECLS
