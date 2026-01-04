/* lrg-game-template.h - Base game template with full engine orchestration
 *
 * Copyright 2025 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * This file is part of Libregnum.
 */

#ifndef LRG_GAME_TEMPLATE_H
#define LRG_GAME_TEMPLATE_H

#include <glib-object.h>
#include "../lrg-version.h"

G_BEGIN_DECLS

/* Forward declarations - avoid circular includes */
typedef struct _LrgEngine LrgEngine;
typedef struct _LrgSettings LrgSettings;
typedef struct _LrgInputMap LrgInputMap;
typedef struct _LrgGameStateManager LrgGameStateManager;
typedef struct _LrgGameState LrgGameState;
typedef struct _LrgEventBus LrgEventBus;
typedef struct _LrgTheme LrgTheme;
typedef struct _LrgRegistry LrgRegistry;

#define LRG_TYPE_GAME_TEMPLATE (lrg_game_template_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_DERIVABLE_TYPE (LrgGameTemplate, lrg_game_template, LRG, GAME_TEMPLATE, GObject)

/**
 * LrgGameTemplateClass:
 * @parent_class: the parent class
 * @configure: Called before window creation to configure template
 * @pre_startup: Called before initial state is pushed
 * @post_startup: Called after initial state is pushed
 * @shutdown: Called during shutdown
 * @pre_update: Called before state update each frame
 * @post_update: Called after state update each frame
 * @fixed_update: Called 0-N times per frame for physics (fixed timestep)
 * @pre_draw: Called before rendering
 * @post_draw: Called after rendering
 * @create_initial_state: Create the first game state
 * @create_pause_state: Create the pause menu state
 * @create_loading_state: Create the loading screen state
 * @create_settings_state: Create the settings menu state
 * @create_error_state: Create the error recovery state
 * @create_controller_disconnect_state: Create controller disconnect state
 * @setup_default_input: Setup default input bindings
 * @handle_global_input: Handle global input (Alt+Enter, etc.)
 * @on_focus_gained: Called when window gains focus
 * @on_focus_lost: Called when window loses focus
 * @on_controller_connected: Called when gamepad is connected
 * @on_controller_disconnected: Called when gamepad is disconnected
 * @create_theme: Create custom UI theme
 * @on_auto_save: Called when auto-save triggers
 * @on_save_completed: Called after save completes
 * @register_types: Register custom types with registry
 *
 * The virtual function table for #LrgGameTemplate.
 * Override these methods to customize game behavior.
 *
 * Since: 1.0
 */
struct _LrgGameTemplateClass
{
    GObjectClass parent_class;

    /*< public >*/

    /* Configuration phase (before window creation) */
    void (*configure) (LrgGameTemplate *self);

    /* Lifecycle hooks */
    void (*pre_startup)  (LrgGameTemplate *self);
    void (*post_startup) (LrgGameTemplate *self);
    void (*shutdown)     (LrgGameTemplate *self);

    /* Frame hooks - variable timestep */
    void (*pre_update)  (LrgGameTemplate *self,
                         gdouble          delta);
    void (*post_update) (LrgGameTemplate *self,
                         gdouble          delta);
    void (*pre_draw)    (LrgGameTemplate *self);
    void (*post_draw)   (LrgGameTemplate *self);

    /* Fixed timestep update - for physics/logic (called 0-N times per frame) */
    void (*fixed_update) (LrgGameTemplate *self,
                          gdouble          fixed_delta);

    /* State management - create built-in states */
    LrgGameState * (*create_initial_state)    (LrgGameTemplate *self);
    LrgGameState * (*create_pause_state)      (LrgGameTemplate *self);
    LrgGameState * (*create_loading_state)    (LrgGameTemplate *self);
    LrgGameState * (*create_settings_state)   (LrgGameTemplate *self);
    LrgGameState * (*create_error_state)      (LrgGameTemplate *self,
                                               const GError    *error);
    LrgGameState * (*create_controller_disconnect_state) (LrgGameTemplate *self);

    /* Input */
    void     (*setup_default_input) (LrgGameTemplate *self,
                                     LrgInputMap     *map);
    gboolean (*handle_global_input) (LrgGameTemplate *self);

    /* Focus handling */
    void (*on_focus_gained)           (LrgGameTemplate *self);
    void (*on_focus_lost)             (LrgGameTemplate *self);
    void (*on_controller_connected)   (LrgGameTemplate *self,
                                       gint             gamepad_id);
    void (*on_controller_disconnected) (LrgGameTemplate *self,
                                        gint             gamepad_id);

    /* UI */
    LrgTheme * (*create_theme) (LrgGameTemplate *self);

    /* Save/Load hooks */
    gboolean (*on_auto_save)      (LrgGameTemplate *self,
                                   GError         **error);
    void     (*on_save_completed) (LrgGameTemplate *self,
                                   gboolean         success);

    /* Extension point */
    void (*register_types) (LrgGameTemplate *self,
                            LrgRegistry     *registry);

    /*< private >*/
    gpointer _reserved[12];
};

/* ==========================================================================
 * Construction
 * ========================================================================== */

/**
 * lrg_game_template_new:
 *
 * Creates a new game template with default settings.
 *
 * Returns: (transfer full): a new #LrgGameTemplate
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgGameTemplate *
lrg_game_template_new (void);

/* ==========================================================================
 * Main Entry Point
 * ========================================================================== */

/**
 * lrg_game_template_run:
 * @self: an #LrgGameTemplate
 * @argc: argument count from main()
 * @argv: argument vector from main()
 *
 * Runs the game loop. This is the main entry point for template-based games.
 * Initializes all subsystems, runs the game loop, and cleans up on exit.
 *
 * Returns: exit code (0 for success)
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint
lrg_game_template_run (LrgGameTemplate *self,
                       int              argc,
                       char           **argv);

/* ==========================================================================
 * Control
 * ========================================================================== */

/**
 * lrg_game_template_quit:
 * @self: an #LrgGameTemplate
 *
 * Signals the game to quit after the current frame.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_game_template_quit (LrgGameTemplate *self);

/**
 * lrg_game_template_pause:
 * @self: an #LrgGameTemplate
 *
 * Pauses the game by pushing the pause state.
 * Does nothing if already paused.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_game_template_pause (LrgGameTemplate *self);

/**
 * lrg_game_template_resume:
 * @self: an #LrgGameTemplate
 *
 * Resumes the game by popping the pause state.
 * Does nothing if not paused.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_game_template_resume (LrgGameTemplate *self);

/**
 * lrg_game_template_is_paused:
 * @self: an #LrgGameTemplate
 *
 * Checks if the game is currently paused.
 *
 * Returns: %TRUE if paused
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_game_template_is_paused (LrgGameTemplate *self);

/* ==========================================================================
 * State Management
 * ========================================================================== */

/**
 * lrg_game_template_push_state:
 * @self: an #LrgGameTemplate
 * @state: (transfer full): state to push
 *
 * Pushes a new state onto the state stack.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_game_template_push_state (LrgGameTemplate *self,
                              LrgGameState    *state);

/**
 * lrg_game_template_pop_state:
 * @self: an #LrgGameTemplate
 *
 * Pops the current state from the stack.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_game_template_pop_state (LrgGameTemplate *self);

/**
 * lrg_game_template_replace_state:
 * @self: an #LrgGameTemplate
 * @state: (transfer full): new state
 *
 * Replaces the current state with a new one.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_game_template_replace_state (LrgGameTemplate *self,
                                 LrgGameState    *state);

/**
 * lrg_game_template_get_current_state:
 * @self: an #LrgGameTemplate
 *
 * Gets the currently active state.
 *
 * Returns: (transfer none) (nullable): the current state
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgGameState *
lrg_game_template_get_current_state (LrgGameTemplate *self);

/* ==========================================================================
 * Subsystem Access
 * ========================================================================== */

/**
 * lrg_game_template_get_engine:
 * @self: an #LrgGameTemplate
 *
 * Gets the engine singleton.
 *
 * Returns: (transfer none): the engine
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgEngine *
lrg_game_template_get_engine (LrgGameTemplate *self);

/**
 * lrg_game_template_get_settings:
 * @self: an #LrgGameTemplate
 *
 * Gets the settings instance.
 *
 * Returns: (transfer none) (nullable): the settings
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgSettings *
lrg_game_template_get_settings (LrgGameTemplate *self);

/**
 * lrg_game_template_get_input_map:
 * @self: an #LrgGameTemplate
 *
 * Gets the input map.
 *
 * Returns: (transfer none): the input map
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgInputMap *
lrg_game_template_get_input_map (LrgGameTemplate *self);

/**
 * lrg_game_template_get_state_manager:
 * @self: an #LrgGameTemplate
 *
 * Gets the state manager.
 *
 * Returns: (transfer none): the state manager
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgGameStateManager *
lrg_game_template_get_state_manager (LrgGameTemplate *self);

/**
 * lrg_game_template_get_event_bus:
 * @self: an #LrgGameTemplate
 *
 * Gets the event bus.
 *
 * Returns: (transfer none): the event bus
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgEventBus *
lrg_game_template_get_event_bus (LrgGameTemplate *self);

/**
 * lrg_game_template_get_theme:
 * @self: an #LrgGameTemplate
 *
 * Gets the UI theme.
 *
 * Returns: (transfer none): the theme
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgTheme *
lrg_game_template_get_theme (LrgGameTemplate *self);

/* ==========================================================================
 * Game Feel / Juice
 * ========================================================================== */

/**
 * lrg_game_template_hit_stop:
 * @self: an #LrgGameTemplate
 * @duration: freeze duration in seconds
 *
 * Applies a brief freeze (hit stop) effect. Game time is paused
 * for the specified duration while real time continues.
 * Useful for combat impact feedback.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_game_template_hit_stop (LrgGameTemplate *self,
                            gdouble          duration);

/**
 * lrg_game_template_get_time_scale:
 * @self: an #LrgGameTemplate
 *
 * Gets the current time scale multiplier.
 *
 * Returns: time scale (1.0 = normal speed)
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gdouble
lrg_game_template_get_time_scale (LrgGameTemplate *self);

/**
 * lrg_game_template_set_time_scale:
 * @self: an #LrgGameTemplate
 * @scale: time scale multiplier
 *
 * Sets the time scale multiplier for slow-motion or fast-forward effects.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_game_template_set_time_scale (LrgGameTemplate *self,
                                  gdouble          scale);

/**
 * lrg_game_template_get_interpolation_alpha:
 * @self: an #LrgGameTemplate
 *
 * Gets the interpolation alpha for render interpolation.
 * This is the fraction of a fixed timestep that has elapsed
 * since the last fixed update.
 *
 * Returns: interpolation alpha (0.0 to 1.0)
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gdouble
lrg_game_template_get_interpolation_alpha (LrgGameTemplate *self);

/* ==========================================================================
 * Screen Shake
 * ========================================================================== */

/**
 * lrg_game_template_shake:
 * @self: an #LrgGameTemplate
 * @trauma: trauma amount to add (0.0 to 1.0)
 *
 * Adds screen shake trauma. The shake intensity is the square of trauma,
 * creating a natural falloff. Use values like 0.3 for small hits,
 * 0.6 for medium impacts, and 1.0 for huge explosions.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_game_template_shake (LrgGameTemplate *self,
                          gfloat           trauma);

/**
 * lrg_game_template_shake_with_params:
 * @self: an #LrgGameTemplate
 * @trauma: trauma amount to add (0.0 to 1.0)
 * @decay: decay rate per second (default ~0.8)
 * @frequency: shake frequency in Hz (default ~30)
 *
 * Adds screen shake with custom parameters.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_game_template_shake_with_params (LrgGameTemplate *self,
                                      gfloat           trauma,
                                      gfloat           decay,
                                      gfloat           frequency);

/**
 * lrg_game_template_get_shake_offset:
 * @self: an #LrgGameTemplate
 * @x: (out) (optional): return location for X offset
 * @y: (out) (optional): return location for Y offset
 *
 * Gets the current screen shake offset. Use this when applying
 * the shake to your camera or render target.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_game_template_get_shake_offset (LrgGameTemplate *self,
                                     gfloat          *x,
                                     gfloat          *y);

/* ==========================================================================
 * Audio Helpers
 * ========================================================================== */

/**
 * lrg_game_template_set_sound_bank:
 * @self: an #LrgGameTemplate
 * @bank: (transfer none): the sound bank to use
 *
 * Sets the default sound bank for play_sound convenience methods.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_game_template_set_sound_bank (LrgGameTemplate *self,
                                   gpointer         bank);

/**
 * lrg_game_template_play_sound:
 * @self: an #LrgGameTemplate
 * @sound_name: name of the sound in the default bank
 *
 * Plays a sound from the default sound bank.
 *
 * Returns: %TRUE if the sound was found and played
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_game_template_play_sound (LrgGameTemplate *self,
                               const gchar     *sound_name);

/**
 * lrg_game_template_play_sound_varied:
 * @self: an #LrgGameTemplate
 * @sound_name: name of the sound in the default bank
 * @pitch_variance: pitch variance in semitones (±)
 * @volume_variance: volume variance as fraction (±)
 *
 * Plays a sound with random pitch and volume variation.
 * Pitch variance is in semitones (e.g., 2.0 means ±2 semitones).
 * Volume variance is a fraction (e.g., 0.1 means ±10% volume).
 *
 * This helps prevent repetitive audio fatigue when the same
 * sound plays frequently.
 *
 * Returns: %TRUE if the sound was found and played
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_game_template_play_sound_varied (LrgGameTemplate *self,
                                      const gchar     *sound_name,
                                      gfloat           pitch_variance,
                                      gfloat           volume_variance);

/* ==========================================================================
 * Camera Juice
 * ========================================================================== */

/**
 * lrg_game_template_camera_zoom_pulse:
 * @self: an #LrgGameTemplate
 * @zoom_delta: amount to change zoom by (e.g., 0.1 for 10% zoom in)
 * @duration: duration in seconds to return to normal
 *
 * Creates a quick zoom pulse effect that snaps to a new zoom level
 * then smoothly returns to the original. Great for impacts and
 * important moments.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_game_template_camera_zoom_pulse (LrgGameTemplate *self,
                                      gfloat           zoom_delta,
                                      gfloat           duration);

/**
 * lrg_game_template_set_camera_follow:
 * @self: an #LrgGameTemplate
 * @enabled: whether to enable camera follow
 * @smoothing: smoothing factor (0.0 = instant, 1.0 = very slow)
 *
 * Enables or disables smooth camera following.
 * When enabled, call lrg_game_template_update_camera_follow_target()
 * each frame with the target position.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_game_template_set_camera_follow (LrgGameTemplate *self,
                                      gboolean         enabled,
                                      gfloat           smoothing);

/**
 * lrg_game_template_set_camera_deadzone:
 * @self: an #LrgGameTemplate
 * @deadzone_x: horizontal deadzone size (pixels or world units)
 * @deadzone_y: vertical deadzone size (pixels or world units)
 *
 * Sets the camera deadzone. The camera won't move until the
 * follow target moves outside this zone around the center.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_game_template_set_camera_deadzone (LrgGameTemplate *self,
                                        gfloat           deadzone_x,
                                        gfloat           deadzone_y);

/**
 * lrg_game_template_update_camera_follow_target:
 * @self: an #LrgGameTemplate
 * @target_x: target X position
 * @target_y: target Y position
 *
 * Updates the camera follow target position. Call this each frame
 * when camera follow is enabled.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_game_template_update_camera_follow_target (LrgGameTemplate *self,
                                                gfloat           target_x,
                                                gfloat           target_y);

/**
 * lrg_game_template_get_camera_position:
 * @self: an #LrgGameTemplate
 * @x: (out) (optional): return location for camera X
 * @y: (out) (optional): return location for camera Y
 *
 * Gets the current smoothed camera position (after follow and shake).
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_game_template_get_camera_position (LrgGameTemplate *self,
                                        gfloat          *x,
                                        gfloat          *y);

/* ==========================================================================
 * Window Properties
 * ========================================================================== */

/**
 * lrg_game_template_get_title:
 * @self: an #LrgGameTemplate
 *
 * Gets the window title.
 *
 * Returns: (transfer none): the window title
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const gchar *
lrg_game_template_get_title (LrgGameTemplate *self);

/**
 * lrg_game_template_set_title:
 * @self: an #LrgGameTemplate
 * @title: new window title
 *
 * Sets the window title.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_game_template_set_title (LrgGameTemplate *self,
                             const gchar     *title);

/**
 * lrg_game_template_get_window_size:
 * @self: an #LrgGameTemplate
 * @width: (out) (optional): return location for width
 * @height: (out) (optional): return location for height
 *
 * Gets the current window size.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_game_template_get_window_size (LrgGameTemplate *self,
                                   gint            *width,
                                   gint            *height);

/**
 * lrg_game_template_set_window_size:
 * @self: an #LrgGameTemplate
 * @width: the new window width in pixels
 * @height: the new window height in pixels
 *
 * Sets the window size. This only works in windowed mode when
 * the game is running.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_game_template_set_window_size (LrgGameTemplate *self,
                                   gint             width,
                                   gint             height);

/**
 * lrg_game_template_toggle_fullscreen:
 * @self: an #LrgGameTemplate
 *
 * Toggles fullscreen mode. In fullscreen, the window uses
 * the monitor's native resolution.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_game_template_toggle_fullscreen (LrgGameTemplate *self);

/**
 * lrg_game_template_is_fullscreen:
 * @self: an #LrgGameTemplate
 *
 * Checks if the window is in fullscreen mode.
 *
 * Returns: %TRUE if fullscreen
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_game_template_is_fullscreen (LrgGameTemplate *self);

/**
 * lrg_game_template_has_focus:
 * @self: an #LrgGameTemplate
 *
 * Checks if the window has focus.
 *
 * Returns: %TRUE if focused
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_game_template_has_focus (LrgGameTemplate *self);

G_END_DECLS

#endif /* LRG_GAME_TEMPLATE_H */
