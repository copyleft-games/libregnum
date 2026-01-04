/* lrg-game-template.c - Base game template implementation
 *
 * Copyright 2025 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * This file is part of Libregnum.
 */

#include "../config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_TEMPLATE

#include "lrg-game-template.h"
#include "lrg-game-template-private.h"
#include "../lrg-log.h"
#include "../core/lrg-engine.h"
#include "../gamestate/lrg-game-state.h"
#include "../gamestate/lrg-game-state-manager.h"
#include "../input/lrg-input-map.h"
#include "../core/lrg-event-bus.h"
#include "../settings/lrg-settings.h"
#include "../ui/lrg-theme.h"
#include "../audio/lrg-audio-manager.h"
#include "../audio/lrg-sound-bank.h"
#include "../postprocess/effects/lrg-screen-shake.h"
#include "../graphics/lrg-grl-window.h"

#include <graylib.h>
#include <math.h>

/* Property IDs */
enum
{
    PROP_0,

    /* Window properties */
    PROP_TITLE,
    PROP_WINDOW_WIDTH,
    PROP_WINDOW_HEIGHT,
    PROP_MIN_WIDTH,
    PROP_MIN_HEIGHT,
    PROP_FULLSCREEN_MODE,
    PROP_VSYNC,
    PROP_TARGET_FPS,
    PROP_ALLOW_RESIZE,
    PROP_ALLOW_ALT_ENTER,

    /* Frame timing */
    PROP_USE_FIXED_TIMESTEP,
    PROP_FIXED_TIMESTEP,
    PROP_MAX_FRAME_TIME,
    PROP_MAX_UPDATES_PER_FRAME,

    /* Application */
    PROP_APP_ID,
    PROP_ENABLE_SETTINGS,
    PROP_ENABLE_CRASH_REPORTER,
    PROP_ENABLE_AUTO_SAVE,
    PROP_AUTO_SAVE_INTERVAL,
    PROP_USE_ATOMIC_SAVES,

    /* Focus handling */
    PROP_PAUSE_ON_FOCUS_LOSS,
    PROP_DUCK_AUDIO_ON_FOCUS_LOSS,
    PROP_FOCUS_LOSS_DUCK_FACTOR,
    PROP_PAUSE_ON_CONTROLLER_DISCONNECT,

    /* Debug */
    PROP_ENABLE_DEBUG_OVERLAY,
    PROP_ENABLE_DEBUG_CONSOLE,
    PROP_LOG_FRAME_DROPS,

    /* Error handling */
    PROP_SHOW_ERROR_SCREEN_ON_CRASH,
    PROP_ERROR_SCREEN_ALLOW_RETRY,

    /* Theming */
    PROP_BACKGROUND_COLOR,
    PROP_BASE_FONT_PATH,
    PROP_UI_FONT_SIZE,
    PROP_THEME,

    /* Input buffering */
    PROP_ENABLE_INPUT_BUFFERING,
    PROP_INPUT_BUFFER_FRAMES,

    N_PROPS
};

static GParamSpec *properties[N_PROPS];

G_DEFINE_TYPE_WITH_PRIVATE (LrgGameTemplate, lrg_game_template, G_TYPE_OBJECT)

/* ========================================================================== */
/* Error Quark                                                                */
/* ========================================================================== */

GQuark
lrg_template_error_quark (void)
{
    return g_quark_from_static_string ("lrg-template-error-quark");
}

/* ========================================================================== */
/* Deferred State Operations                                                  */
/* ========================================================================== */

LrgDeferredStateOp *
lrg_deferred_state_op_new (LrgStateOpType  op_type,
                           LrgGameState   *state)
{
    LrgDeferredStateOp *op;

    op = g_new0 (LrgDeferredStateOp, 1);
    op->op_type = op_type;
    op->state = (state != NULL) ? g_object_ref (state) : NULL;

    return op;
}

void
lrg_deferred_state_op_free (LrgDeferredStateOp *op)
{
    if (op == NULL)
        return;

    g_clear_object (&op->state);
    g_free (op);
}

/* ========================================================================== */
/* Private Helper Functions                                                   */
/* ========================================================================== */

static void
template_process_deferred_ops (LrgGameTemplate *self)
{
    LrgGameTemplatePrivate *priv;
    guint i;

    priv = lrg_game_template_get_instance_private (self);

    if (priv->deferred_state_ops == NULL ||
        priv->deferred_state_ops->len == 0)
        return;

    for (i = 0; i < priv->deferred_state_ops->len; i++)
    {
        LrgDeferredStateOp *op;

        op = g_ptr_array_index (priv->deferred_state_ops, i);

        switch (op->op_type)
        {
        case LRG_STATE_OP_PUSH:
            lrg_game_state_manager_push (priv->state_manager, op->state);
            op->state = NULL; /* Manager took ownership */
            break;

        case LRG_STATE_OP_POP:
            lrg_game_state_manager_pop (priv->state_manager);
            break;

        case LRG_STATE_OP_REPLACE:
            lrg_game_state_manager_replace (priv->state_manager, op->state);
            op->state = NULL; /* Manager took ownership */
            break;
        }
    }

    g_ptr_array_set_size (priv->deferred_state_ops, 0);
}

static void
template_safe_state_update (LrgGameTemplate *self,
                            gdouble          delta)
{
    LrgGameTemplatePrivate *priv;
    LrgGameState *current;
    g_autoptr(GError) error = NULL;

    priv = lrg_game_template_get_instance_private (self);
    current = lrg_game_state_manager_get_current (priv->state_manager);

    if (current == NULL)
        return;

    /* Use safe update if available */
    if (!lrg_game_state_update_safe (current, delta, &error))
    {
        LrgGameTemplateClass *klass;

        lrg_warning (LRG_LOG_DOMAIN_TEMPLATE,
                     "State update failed: %s", error->message);

        /* Push error state if available */
        klass = LRG_GAME_TEMPLATE_GET_CLASS (self);
        if (klass->create_error_state != NULL &&
            priv->show_error_screen_on_crash)
        {
            LrgGameState *error_state;

            error_state = klass->create_error_state (self, error);
            if (error_state != NULL)
            {
                lrg_game_state_manager_push (priv->state_manager, error_state);
            }
        }
    }
}

static void
template_check_gamepad_state (LrgGameTemplate *self)
{
    LrgGameTemplatePrivate *priv;
    LrgGameTemplateClass *klass;
    gboolean was_connected;

    priv = lrg_game_template_get_instance_private (self);
    klass = LRG_GAME_TEMPLATE_GET_CLASS (self);

    was_connected = priv->gamepad_connected;
    priv->gamepad_connected = grl_input_is_gamepad_available (0);

    if (was_connected && !priv->gamepad_connected)
    {
        /* Controller disconnected */
        if (klass->on_controller_disconnected != NULL)
            klass->on_controller_disconnected (self, 0);

        if (priv->pause_on_controller_disconnect)
        {
            LrgGameState *disconnect_state;

            if (klass->create_controller_disconnect_state != NULL)
            {
                disconnect_state = klass->create_controller_disconnect_state (self);
                if (disconnect_state != NULL)
                {
                    lrg_game_state_manager_push (priv->state_manager, disconnect_state);
                }
            }
        }
    }
    else if (!was_connected && priv->gamepad_connected)
    {
        /* Controller connected */
        if (klass->on_controller_connected != NULL)
            klass->on_controller_connected (self, 0);
    }
}

static void
template_check_focus_state (LrgGameTemplate *self)
{
    LrgGameTemplatePrivate *priv;
    LrgGameTemplateClass *klass;
    GrlWindow *raw_window;
    gboolean is_focused;

    priv = lrg_game_template_get_instance_private (self);
    klass = LRG_GAME_TEMPLATE_GET_CLASS (self);

    if (priv->window == NULL)
        return;

    raw_window = lrg_grl_window_get_grl_window (LRG_GRL_WINDOW (priv->window));
    is_focused = grl_window_is_focused (raw_window);

    if (priv->has_focus && !is_focused)
    {
        /* Lost focus */
        priv->has_focus = FALSE;

        if (klass->on_focus_lost != NULL)
            klass->on_focus_lost (self);

        /* Duck audio if configured */
        if (priv->duck_audio_on_focus_loss)
        {
            LrgAudioManager *audio;

            audio = lrg_audio_manager_get_default ();

            priv->saved_sfx_volume = lrg_audio_manager_get_sfx_volume (audio);
            priv->saved_music_volume = lrg_audio_manager_get_music_volume (audio);

            lrg_audio_manager_set_sfx_volume (audio,
                priv->saved_sfx_volume * priv->focus_loss_duck_factor);
            lrg_audio_manager_set_music_volume (audio,
                priv->saved_music_volume * priv->focus_loss_duck_factor);
        }

        /* Auto-pause if configured */
        if (priv->pause_on_focus_loss && !priv->is_paused)
        {
            lrg_game_template_pause (self);
        }
    }
    else if (!priv->has_focus && is_focused)
    {
        /* Gained focus */
        priv->has_focus = TRUE;

        if (klass->on_focus_gained != NULL)
            klass->on_focus_gained (self);

        /* Restore audio if ducked */
        if (priv->duck_audio_on_focus_loss)
        {
            LrgAudioManager *audio;

            audio = lrg_audio_manager_get_default ();

            lrg_audio_manager_set_sfx_volume (audio, priv->saved_sfx_volume);
            lrg_audio_manager_set_music_volume (audio, priv->saved_music_volume);
        }
    }
}

static gboolean
template_handle_global_input_default (LrgGameTemplate *self)
{
    LrgGameTemplatePrivate *priv;

    priv = lrg_game_template_get_instance_private (self);

    /* Alt+Enter toggles fullscreen */
    if (priv->allow_alt_enter && priv->window != NULL)
    {
        if (grl_input_is_key_down (GRL_KEY_LEFT_ALT) &&
            grl_input_is_key_pressed (GRL_KEY_ENTER))
        {
            lrg_grl_window_toggle_fullscreen (LRG_GRL_WINDOW (priv->window));
            return TRUE;
        }
    }

    return FALSE;
}

static void
template_frame (LrgGameTemplate *self)
{
    LrgGameTemplatePrivate *priv;
    LrgGameTemplateClass *klass;
    gdouble raw_delta;
    gdouble delta;

    priv = lrg_game_template_get_instance_private (self);
    klass = LRG_GAME_TEMPLATE_GET_CLASS (self);

    /* Get raw delta, clamp to max */
    raw_delta = (gdouble)lrg_window_get_frame_time (priv->window);
    delta = CLAMP (raw_delta, 0.0, priv->max_frame_time);

    /* Log frame drops if configured */
    if (priv->log_frame_drops && raw_delta > priv->max_frame_time)
    {
        lrg_debug (LRG_LOG_DOMAIN_TEMPLATE,
                   "Frame drop: delta %.3f clamped to %.3f",
                   raw_delta, delta);
    }

    /* Apply time scale (for hit stop) */
    if (priv->hit_stop_remaining > 0)
    {
        priv->hit_stop_remaining -= raw_delta; /* Use real time */
        if (priv->hit_stop_remaining <= 0)
        {
            priv->time_scale = priv->saved_time_scale;
            priv->hit_stop_remaining = 0;
        }
        else
        {
            delta = 0; /* Freeze game time during hit stop */
        }
    }
    else
    {
        delta *= priv->time_scale;
    }

    /* Check focus and gamepad state */
    template_check_focus_state (self);
    template_check_gamepad_state (self);

    /* Global input (Alt+Enter, etc.) */
    if (klass->handle_global_input != NULL)
        klass->handle_global_input (self);

    /* Fixed timestep loop */
    if (priv->use_fixed_timestep)
    {
        gint updates;

        priv->accumulator += delta;
        updates = 0;

        while (priv->accumulator >= priv->fixed_timestep &&
               updates < priv->max_updates_per_frame)
        {
            priv->in_state_transition = TRUE;

            if (klass->fixed_update != NULL)
                klass->fixed_update (self, priv->fixed_timestep);

            /* Update state manager with fixed timestep */
            lrg_game_state_manager_update (priv->state_manager,
                                           priv->fixed_timestep);

            priv->in_state_transition = FALSE;
            template_process_deferred_ops (self);

            priv->accumulator -= priv->fixed_timestep;
            updates++;
        }

        /* Prevent spiral of death */
        if (updates >= priv->max_updates_per_frame)
        {
            lrg_debug (LRG_LOG_DOMAIN_TEMPLATE,
                       "Spiral of death prevented: %d updates, resetting accumulator",
                       updates);
            priv->accumulator = 0;
        }

        /* Calculate interpolation alpha for rendering */
        priv->interpolation_alpha = priv->accumulator / priv->fixed_timestep;
    }
    else
    {
        /* Variable timestep */
        priv->in_state_transition = TRUE;

        template_safe_state_update (self, delta);

        priv->in_state_transition = FALSE;
        template_process_deferred_ops (self);
    }

    /* Variable timestep hooks */
    if (klass->pre_update != NULL)
        klass->pre_update (self, delta);

    if (klass->post_update != NULL)
        klass->post_update (self, delta);

    /* Update game feel systems */
    if (priv->screen_shake != NULL)
        lrg_screen_shake_update (priv->screen_shake, (gfloat)delta);

    /* Update camera zoom pulse */
    if (priv->camera_zoom_pulse_timer > 0.0f)
    {
        priv->camera_zoom_pulse_timer -= (gfloat)delta;
        if (priv->camera_zoom_pulse_timer <= 0.0f)
        {
            priv->camera_zoom_pulse_timer = 0.0f;
            priv->camera_zoom_pulse_target = priv->camera_zoom_pulse_original;
        }
    }

    /* Render */
    lrg_window_begin_frame (priv->window);

    if (priv->background_color != NULL)
        lrg_window_clear (priv->window, priv->background_color);

    if (klass->pre_draw != NULL)
        klass->pre_draw (self);

    lrg_game_state_manager_draw (priv->state_manager);

    if (klass->post_draw != NULL)
        klass->post_draw (self);

    lrg_window_end_frame (priv->window);

    /* Auto-save check */
    if (priv->enable_auto_save && !priv->is_paused)
    {
        priv->auto_save_timer += delta;

        if (priv->auto_save_timer >= priv->auto_save_interval)
        {
            priv->auto_save_timer = 0;

            if (klass->on_auto_save != NULL)
            {
                g_autoptr(GError) error = NULL;
                gboolean success;

                success = klass->on_auto_save (self, &error);

                if (klass->on_save_completed != NULL)
                    klass->on_save_completed (self, success);
            }
        }
    }

    /* Audio update */
    lrg_audio_manager_update (lrg_audio_manager_get_default ());
}

/* ========================================================================== */
/* Default Virtual Method Implementations                                     */
/* ========================================================================== */

static void
lrg_game_template_real_configure (LrgGameTemplate *self)
{
    /* Default: do nothing */
}

static void
lrg_game_template_real_pre_startup (LrgGameTemplate *self)
{
    /* Default: do nothing */
}

static void
lrg_game_template_real_post_startup (LrgGameTemplate *self)
{
    /* Default: do nothing */
}

static void
lrg_game_template_real_shutdown (LrgGameTemplate *self)
{
    /* Default: do nothing */
}

static void
lrg_game_template_real_pre_update (LrgGameTemplate *self,
                                    gdouble          delta)
{
    /* Default: do nothing */
}

static void
lrg_game_template_real_post_update (LrgGameTemplate *self,
                                     gdouble          delta)
{
    /* Default: do nothing */
}

static void
lrg_game_template_real_fixed_update (LrgGameTemplate *self,
                                      gdouble          fixed_delta)
{
    /* Default: do nothing */
}

static void
lrg_game_template_real_pre_draw (LrgGameTemplate *self)
{
    /* Default: do nothing */
}

static void
lrg_game_template_real_post_draw (LrgGameTemplate *self)
{
    /* Default: do nothing */
}

static LrgGameState *
lrg_game_template_real_create_initial_state (LrgGameTemplate *self)
{
    /* Default: subclasses must override this */
    lrg_warning (LRG_LOG_DOMAIN_TEMPLATE,
                 "create_initial_state not implemented - no initial state");
    return NULL;
}

static LrgGameState *
lrg_game_template_real_create_pause_state (LrgGameTemplate *self)
{
    /* Default: no pause state */
    return NULL;
}

static LrgGameState *
lrg_game_template_real_create_loading_state (LrgGameTemplate *self)
{
    /* Default: no loading state */
    return NULL;
}

static LrgGameState *
lrg_game_template_real_create_settings_state (LrgGameTemplate *self)
{
    /* Default: no settings state */
    return NULL;
}

static LrgGameState *
lrg_game_template_real_create_error_state (LrgGameTemplate *self,
                                            const GError    *error)
{
    /* Default: no error state */
    return NULL;
}

static LrgGameState *
lrg_game_template_real_create_controller_disconnect_state (LrgGameTemplate *self)
{
    /* Default: no controller disconnect state */
    return NULL;
}

static void
lrg_game_template_real_setup_default_input (LrgGameTemplate *self,
                                             LrgInputMap     *map)
{
    /* Default: no input bindings */
}

static gboolean
lrg_game_template_real_handle_global_input (LrgGameTemplate *self)
{
    return template_handle_global_input_default (self);
}

static void
lrg_game_template_real_on_focus_gained (LrgGameTemplate *self)
{
    /* Default: do nothing */
}

static void
lrg_game_template_real_on_focus_lost (LrgGameTemplate *self)
{
    /* Default: do nothing */
}

static void
lrg_game_template_real_on_controller_connected (LrgGameTemplate *self,
                                                 gint             gamepad_id)
{
    lrg_debug (LRG_LOG_DOMAIN_TEMPLATE,
               "Controller %d connected", gamepad_id);
}

static void
lrg_game_template_real_on_controller_disconnected (LrgGameTemplate *self,
                                                    gint             gamepad_id)
{
    lrg_debug (LRG_LOG_DOMAIN_TEMPLATE,
               "Controller %d disconnected", gamepad_id);
}

static LrgTheme *
lrg_game_template_real_create_theme (LrgGameTemplate *self)
{
    return lrg_theme_get_default ();
}

static gboolean
lrg_game_template_real_on_auto_save (LrgGameTemplate  *self,
                                      GError          **error)
{
    /* Default: do nothing */
    return TRUE;
}

static void
lrg_game_template_real_on_save_completed (LrgGameTemplate *self,
                                           gboolean         success)
{
    if (!success)
    {
        lrg_warning (LRG_LOG_DOMAIN_TEMPLATE, "Auto-save failed");
    }
}

static void
lrg_game_template_real_register_types (LrgGameTemplate *self,
                                        LrgRegistry     *registry)
{
    /* Default: no custom types */
}

/* ========================================================================== */
/* GObject Implementation                                                     */
/* ========================================================================== */

static void
lrg_game_template_finalize (GObject *object)
{
    LrgGameTemplate *self;
    LrgGameTemplatePrivate *priv;

    self = LRG_GAME_TEMPLATE (object);
    priv = lrg_game_template_get_instance_private (self);

    /* Free strings */
    g_clear_pointer (&priv->title, g_free);
    g_clear_pointer (&priv->app_id, g_free);
    g_clear_pointer (&priv->base_font_path, g_free);

    /* Free GBoxed types */
    g_clear_pointer (&priv->background_color, grl_color_free);

    /* Free deferred ops */
    if (priv->deferred_state_ops != NULL)
    {
        g_ptr_array_free (priv->deferred_state_ops, TRUE);
        priv->deferred_state_ops = NULL;
    }

    /* Free signal handlers array */
    if (priv->signal_handlers != NULL)
    {
        g_ptr_array_free (priv->signal_handlers, TRUE);
        priv->signal_handlers = NULL;
    }

    /* Unref owned subsystems */
    g_clear_object (&priv->window);
    g_clear_object (&priv->state_manager);
    g_clear_object (&priv->input_map);
    g_clear_object (&priv->settings);
    g_clear_object (&priv->event_bus);

    /* Theme is a singleton - don't unref unless we created it */
    priv->theme = NULL;

    /* Engine is a singleton - don't unref */
    priv->engine = NULL;

    /* Game feel - screen shake */
    g_clear_object (&priv->screen_shake);

    /* Game feel - sound bank (not owned, just a reference) */
    priv->default_sound_bank = NULL;

    G_OBJECT_CLASS (lrg_game_template_parent_class)->finalize (object);
}

static void
lrg_game_template_get_property (GObject    *object,
                                 guint       prop_id,
                                 GValue     *value,
                                 GParamSpec *pspec)
{
    LrgGameTemplate *self;
    LrgGameTemplatePrivate *priv;

    self = LRG_GAME_TEMPLATE (object);
    priv = lrg_game_template_get_instance_private (self);

    switch (prop_id)
    {
    case PROP_TITLE:
        g_value_set_string (value, priv->title);
        break;
    case PROP_WINDOW_WIDTH:
        g_value_set_int (value, priv->window_width);
        break;
    case PROP_WINDOW_HEIGHT:
        g_value_set_int (value, priv->window_height);
        break;
    case PROP_MIN_WIDTH:
        g_value_set_int (value, priv->min_width);
        break;
    case PROP_MIN_HEIGHT:
        g_value_set_int (value, priv->min_height);
        break;
    case PROP_FULLSCREEN_MODE:
        g_value_set_enum (value, priv->fullscreen_mode);
        break;
    case PROP_VSYNC:
        g_value_set_boolean (value, priv->vsync);
        break;
    case PROP_TARGET_FPS:
        g_value_set_int (value, priv->target_fps);
        break;
    case PROP_ALLOW_RESIZE:
        g_value_set_boolean (value, priv->allow_resize);
        break;
    case PROP_ALLOW_ALT_ENTER:
        g_value_set_boolean (value, priv->allow_alt_enter);
        break;
    case PROP_USE_FIXED_TIMESTEP:
        g_value_set_boolean (value, priv->use_fixed_timestep);
        break;
    case PROP_FIXED_TIMESTEP:
        g_value_set_double (value, priv->fixed_timestep);
        break;
    case PROP_MAX_FRAME_TIME:
        g_value_set_double (value, priv->max_frame_time);
        break;
    case PROP_MAX_UPDATES_PER_FRAME:
        g_value_set_int (value, priv->max_updates_per_frame);
        break;
    case PROP_APP_ID:
        g_value_set_string (value, priv->app_id);
        break;
    case PROP_ENABLE_SETTINGS:
        g_value_set_boolean (value, priv->enable_settings);
        break;
    case PROP_ENABLE_CRASH_REPORTER:
        g_value_set_boolean (value, priv->enable_crash_reporter);
        break;
    case PROP_ENABLE_AUTO_SAVE:
        g_value_set_boolean (value, priv->enable_auto_save);
        break;
    case PROP_AUTO_SAVE_INTERVAL:
        g_value_set_double (value, priv->auto_save_interval);
        break;
    case PROP_USE_ATOMIC_SAVES:
        g_value_set_boolean (value, priv->use_atomic_saves);
        break;
    case PROP_PAUSE_ON_FOCUS_LOSS:
        g_value_set_boolean (value, priv->pause_on_focus_loss);
        break;
    case PROP_DUCK_AUDIO_ON_FOCUS_LOSS:
        g_value_set_boolean (value, priv->duck_audio_on_focus_loss);
        break;
    case PROP_FOCUS_LOSS_DUCK_FACTOR:
        g_value_set_float (value, priv->focus_loss_duck_factor);
        break;
    case PROP_PAUSE_ON_CONTROLLER_DISCONNECT:
        g_value_set_boolean (value, priv->pause_on_controller_disconnect);
        break;
    case PROP_ENABLE_DEBUG_OVERLAY:
        g_value_set_boolean (value, priv->enable_debug_overlay);
        break;
    case PROP_ENABLE_DEBUG_CONSOLE:
        g_value_set_boolean (value, priv->enable_debug_console);
        break;
    case PROP_LOG_FRAME_DROPS:
        g_value_set_boolean (value, priv->log_frame_drops);
        break;
    case PROP_SHOW_ERROR_SCREEN_ON_CRASH:
        g_value_set_boolean (value, priv->show_error_screen_on_crash);
        break;
    case PROP_ERROR_SCREEN_ALLOW_RETRY:
        g_value_set_boolean (value, priv->error_screen_allow_retry);
        break;
    case PROP_BACKGROUND_COLOR:
        g_value_set_boxed (value, priv->background_color);
        break;
    case PROP_BASE_FONT_PATH:
        g_value_set_string (value, priv->base_font_path);
        break;
    case PROP_UI_FONT_SIZE:
        g_value_set_int (value, priv->ui_font_size);
        break;
    case PROP_THEME:
        g_value_set_object (value, priv->theme);
        break;
    case PROP_ENABLE_INPUT_BUFFERING:
        g_value_set_boolean (value, priv->enable_input_buffering);
        break;
    case PROP_INPUT_BUFFER_FRAMES:
        g_value_set_int (value, priv->input_buffer_frames);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_game_template_set_property (GObject      *object,
                                 guint         prop_id,
                                 const GValue *value,
                                 GParamSpec   *pspec)
{
    LrgGameTemplate *self;
    LrgGameTemplatePrivate *priv;

    self = LRG_GAME_TEMPLATE (object);
    priv = lrg_game_template_get_instance_private (self);

    switch (prop_id)
    {
    case PROP_TITLE:
        g_free (priv->title);
        priv->title = g_value_dup_string (value);
        break;
    case PROP_WINDOW_WIDTH:
        priv->window_width = g_value_get_int (value);
        break;
    case PROP_WINDOW_HEIGHT:
        priv->window_height = g_value_get_int (value);
        break;
    case PROP_MIN_WIDTH:
        priv->min_width = g_value_get_int (value);
        break;
    case PROP_MIN_HEIGHT:
        priv->min_height = g_value_get_int (value);
        break;
    case PROP_FULLSCREEN_MODE:
        priv->fullscreen_mode = g_value_get_enum (value);
        break;
    case PROP_VSYNC:
        priv->vsync = g_value_get_boolean (value);
        break;
    case PROP_TARGET_FPS:
        priv->target_fps = g_value_get_int (value);
        break;
    case PROP_ALLOW_RESIZE:
        priv->allow_resize = g_value_get_boolean (value);
        break;
    case PROP_ALLOW_ALT_ENTER:
        priv->allow_alt_enter = g_value_get_boolean (value);
        break;
    case PROP_USE_FIXED_TIMESTEP:
        priv->use_fixed_timestep = g_value_get_boolean (value);
        break;
    case PROP_FIXED_TIMESTEP:
        priv->fixed_timestep = g_value_get_double (value);
        break;
    case PROP_MAX_FRAME_TIME:
        priv->max_frame_time = g_value_get_double (value);
        break;
    case PROP_MAX_UPDATES_PER_FRAME:
        priv->max_updates_per_frame = g_value_get_int (value);
        break;
    case PROP_APP_ID:
        g_free (priv->app_id);
        priv->app_id = g_value_dup_string (value);
        break;
    case PROP_ENABLE_SETTINGS:
        priv->enable_settings = g_value_get_boolean (value);
        break;
    case PROP_ENABLE_CRASH_REPORTER:
        priv->enable_crash_reporter = g_value_get_boolean (value);
        break;
    case PROP_ENABLE_AUTO_SAVE:
        priv->enable_auto_save = g_value_get_boolean (value);
        break;
    case PROP_AUTO_SAVE_INTERVAL:
        priv->auto_save_interval = g_value_get_double (value);
        break;
    case PROP_USE_ATOMIC_SAVES:
        priv->use_atomic_saves = g_value_get_boolean (value);
        break;
    case PROP_PAUSE_ON_FOCUS_LOSS:
        priv->pause_on_focus_loss = g_value_get_boolean (value);
        break;
    case PROP_DUCK_AUDIO_ON_FOCUS_LOSS:
        priv->duck_audio_on_focus_loss = g_value_get_boolean (value);
        break;
    case PROP_FOCUS_LOSS_DUCK_FACTOR:
        priv->focus_loss_duck_factor = g_value_get_float (value);
        break;
    case PROP_PAUSE_ON_CONTROLLER_DISCONNECT:
        priv->pause_on_controller_disconnect = g_value_get_boolean (value);
        break;
    case PROP_ENABLE_DEBUG_OVERLAY:
        priv->enable_debug_overlay = g_value_get_boolean (value);
        break;
    case PROP_ENABLE_DEBUG_CONSOLE:
        priv->enable_debug_console = g_value_get_boolean (value);
        break;
    case PROP_LOG_FRAME_DROPS:
        priv->log_frame_drops = g_value_get_boolean (value);
        break;
    case PROP_SHOW_ERROR_SCREEN_ON_CRASH:
        priv->show_error_screen_on_crash = g_value_get_boolean (value);
        break;
    case PROP_ERROR_SCREEN_ALLOW_RETRY:
        priv->error_screen_allow_retry = g_value_get_boolean (value);
        break;
    case PROP_BACKGROUND_COLOR:
        g_clear_pointer (&priv->background_color, grl_color_free);
        priv->background_color = g_value_dup_boxed (value);
        break;
    case PROP_BASE_FONT_PATH:
        g_free (priv->base_font_path);
        priv->base_font_path = g_value_dup_string (value);
        break;
    case PROP_UI_FONT_SIZE:
        priv->ui_font_size = g_value_get_int (value);
        break;
    case PROP_THEME:
        priv->theme = g_value_get_object (value);
        break;
    case PROP_ENABLE_INPUT_BUFFERING:
        priv->enable_input_buffering = g_value_get_boolean (value);
        break;
    case PROP_INPUT_BUFFER_FRAMES:
        priv->input_buffer_frames = g_value_get_int (value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_game_template_class_init (LrgGameTemplateClass *klass)
{
    GObjectClass *object_class;

    object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = lrg_game_template_finalize;
    object_class->get_property = lrg_game_template_get_property;
    object_class->set_property = lrg_game_template_set_property;

    /* Virtual methods */
    klass->configure = lrg_game_template_real_configure;
    klass->pre_startup = lrg_game_template_real_pre_startup;
    klass->post_startup = lrg_game_template_real_post_startup;
    klass->shutdown = lrg_game_template_real_shutdown;
    klass->pre_update = lrg_game_template_real_pre_update;
    klass->post_update = lrg_game_template_real_post_update;
    klass->fixed_update = lrg_game_template_real_fixed_update;
    klass->pre_draw = lrg_game_template_real_pre_draw;
    klass->post_draw = lrg_game_template_real_post_draw;
    klass->create_initial_state = lrg_game_template_real_create_initial_state;
    klass->create_pause_state = lrg_game_template_real_create_pause_state;
    klass->create_loading_state = lrg_game_template_real_create_loading_state;
    klass->create_settings_state = lrg_game_template_real_create_settings_state;
    klass->create_error_state = lrg_game_template_real_create_error_state;
    klass->create_controller_disconnect_state = lrg_game_template_real_create_controller_disconnect_state;
    klass->setup_default_input = lrg_game_template_real_setup_default_input;
    klass->handle_global_input = lrg_game_template_real_handle_global_input;
    klass->on_focus_gained = lrg_game_template_real_on_focus_gained;
    klass->on_focus_lost = lrg_game_template_real_on_focus_lost;
    klass->on_controller_connected = lrg_game_template_real_on_controller_connected;
    klass->on_controller_disconnected = lrg_game_template_real_on_controller_disconnected;
    klass->create_theme = lrg_game_template_real_create_theme;
    klass->on_auto_save = lrg_game_template_real_on_auto_save;
    klass->on_save_completed = lrg_game_template_real_on_save_completed;
    klass->register_types = lrg_game_template_real_register_types;

    /* Window properties */
    properties[PROP_TITLE] =
        g_param_spec_string ("title",
                             "Title",
                             "Window title",
                             "Libregnum Game",
                             G_PARAM_READWRITE |
                             G_PARAM_STATIC_STRINGS |
                             G_PARAM_CONSTRUCT);

    properties[PROP_WINDOW_WIDTH] =
        g_param_spec_int ("window-width",
                          "Window Width",
                          "Initial window width",
                          1, G_MAXINT, 1280,
                          G_PARAM_READWRITE |
                          G_PARAM_STATIC_STRINGS |
                          G_PARAM_CONSTRUCT);

    properties[PROP_WINDOW_HEIGHT] =
        g_param_spec_int ("window-height",
                          "Window Height",
                          "Initial window height",
                          1, G_MAXINT, 720,
                          G_PARAM_READWRITE |
                          G_PARAM_STATIC_STRINGS |
                          G_PARAM_CONSTRUCT);

    properties[PROP_MIN_WIDTH] =
        g_param_spec_int ("min-width",
                          "Minimum Width",
                          "Minimum window width",
                          1, G_MAXINT, 640,
                          G_PARAM_READWRITE |
                          G_PARAM_STATIC_STRINGS |
                          G_PARAM_CONSTRUCT);

    properties[PROP_MIN_HEIGHT] =
        g_param_spec_int ("min-height",
                          "Minimum Height",
                          "Minimum window height",
                          1, G_MAXINT, 360,
                          G_PARAM_READWRITE |
                          G_PARAM_STATIC_STRINGS |
                          G_PARAM_CONSTRUCT);

    properties[PROP_FULLSCREEN_MODE] =
        g_param_spec_enum ("fullscreen-mode",
                           "Fullscreen Mode",
                           "Window fullscreen mode",
                           LRG_TYPE_FULLSCREEN_MODE,
                           LRG_FULLSCREEN_WINDOWED,
                           G_PARAM_READWRITE |
                           G_PARAM_STATIC_STRINGS |
                           G_PARAM_CONSTRUCT);

    properties[PROP_VSYNC] =
        g_param_spec_boolean ("vsync",
                              "VSync",
                              "Enable vertical sync",
                              TRUE,
                              G_PARAM_READWRITE |
                              G_PARAM_STATIC_STRINGS |
                              G_PARAM_CONSTRUCT);

    properties[PROP_TARGET_FPS] =
        g_param_spec_int ("target-fps",
                          "Target FPS",
                          "Target frame rate",
                          1, 240, 60,
                          G_PARAM_READWRITE |
                          G_PARAM_STATIC_STRINGS |
                          G_PARAM_CONSTRUCT);

    properties[PROP_ALLOW_RESIZE] =
        g_param_spec_boolean ("allow-resize",
                              "Allow Resize",
                              "Allow window resize",
                              TRUE,
                              G_PARAM_READWRITE |
                              G_PARAM_STATIC_STRINGS |
                              G_PARAM_CONSTRUCT);

    properties[PROP_ALLOW_ALT_ENTER] =
        g_param_spec_boolean ("allow-alt-enter",
                              "Allow Alt+Enter",
                              "Alt+Enter toggles fullscreen",
                              TRUE,
                              G_PARAM_READWRITE |
                              G_PARAM_STATIC_STRINGS |
                              G_PARAM_CONSTRUCT);

    /* Frame timing properties */
    properties[PROP_USE_FIXED_TIMESTEP] =
        g_param_spec_boolean ("use-fixed-timestep",
                              "Use Fixed Timestep",
                              "Enable fixed timestep for physics",
                              TRUE,
                              G_PARAM_READWRITE |
                              G_PARAM_STATIC_STRINGS |
                              G_PARAM_CONSTRUCT);

    properties[PROP_FIXED_TIMESTEP] =
        g_param_spec_double ("fixed-timestep",
                             "Fixed Timestep",
                             "Fixed update interval in seconds",
                             0.001, 0.1, LRG_DEFAULT_FIXED_TIMESTEP,
                             G_PARAM_READWRITE |
                             G_PARAM_STATIC_STRINGS |
                             G_PARAM_CONSTRUCT);

    properties[PROP_MAX_FRAME_TIME] =
        g_param_spec_double ("max-frame-time",
                             "Max Frame Time",
                             "Maximum delta time before clamping",
                             0.01, 1.0, LRG_DEFAULT_MAX_FRAME_TIME,
                             G_PARAM_READWRITE |
                             G_PARAM_STATIC_STRINGS |
                             G_PARAM_CONSTRUCT);

    properties[PROP_MAX_UPDATES_PER_FRAME] =
        g_param_spec_int ("max-updates-per-frame",
                          "Max Updates Per Frame",
                          "Maximum fixed updates per frame",
                          1, 20, LRG_DEFAULT_MAX_UPDATES_PER_FRAME,
                          G_PARAM_READWRITE |
                          G_PARAM_STATIC_STRINGS |
                          G_PARAM_CONSTRUCT);

    /* Application properties */
    properties[PROP_APP_ID] =
        g_param_spec_string ("app-id",
                             "App ID",
                             "Application identifier for settings/saves",
                             NULL,
                             G_PARAM_READWRITE |
                             G_PARAM_STATIC_STRINGS |
                             G_PARAM_CONSTRUCT);

    properties[PROP_ENABLE_SETTINGS] =
        g_param_spec_boolean ("enable-settings",
                              "Enable Settings",
                              "Auto-load/save settings",
                              TRUE,
                              G_PARAM_READWRITE |
                              G_PARAM_STATIC_STRINGS |
                              G_PARAM_CONSTRUCT);

    properties[PROP_ENABLE_CRASH_REPORTER] =
        g_param_spec_boolean ("enable-crash-reporter",
                              "Enable Crash Reporter",
                              "Install crash handler",
                              TRUE,
                              G_PARAM_READWRITE |
                              G_PARAM_STATIC_STRINGS |
                              G_PARAM_CONSTRUCT);

    properties[PROP_ENABLE_AUTO_SAVE] =
        g_param_spec_boolean ("enable-auto-save",
                              "Enable Auto Save",
                              "Enable auto-save system",
                              TRUE,
                              G_PARAM_READWRITE |
                              G_PARAM_STATIC_STRINGS |
                              G_PARAM_CONSTRUCT);

    properties[PROP_AUTO_SAVE_INTERVAL] =
        g_param_spec_double ("auto-save-interval",
                             "Auto Save Interval",
                             "Auto-save interval in seconds",
                             1.0, 3600.0, LRG_DEFAULT_AUTO_SAVE_INTERVAL,
                             G_PARAM_READWRITE |
                             G_PARAM_STATIC_STRINGS |
                             G_PARAM_CONSTRUCT);

    properties[PROP_USE_ATOMIC_SAVES] =
        g_param_spec_boolean ("use-atomic-saves",
                              "Use Atomic Saves",
                              "Use .tmp/.bak atomic save pattern",
                              TRUE,
                              G_PARAM_READWRITE |
                              G_PARAM_STATIC_STRINGS |
                              G_PARAM_CONSTRUCT);

    /* Focus handling properties */
    properties[PROP_PAUSE_ON_FOCUS_LOSS] =
        g_param_spec_boolean ("pause-on-focus-loss",
                              "Pause on Focus Loss",
                              "Auto-pause when window loses focus",
                              TRUE,
                              G_PARAM_READWRITE |
                              G_PARAM_STATIC_STRINGS |
                              G_PARAM_CONSTRUCT);

    properties[PROP_DUCK_AUDIO_ON_FOCUS_LOSS] =
        g_param_spec_boolean ("duck-audio-on-focus-loss",
                              "Duck Audio on Focus Loss",
                              "Reduce volume when unfocused",
                              TRUE,
                              G_PARAM_READWRITE |
                              G_PARAM_STATIC_STRINGS |
                              G_PARAM_CONSTRUCT);

    properties[PROP_FOCUS_LOSS_DUCK_FACTOR] =
        g_param_spec_float ("focus-loss-duck-factor",
                            "Focus Loss Duck Factor",
                            "Volume multiplier when unfocused",
                            0.0f, 1.0f, LRG_DEFAULT_FOCUS_LOSS_DUCK_FACTOR,
                            G_PARAM_READWRITE |
                            G_PARAM_STATIC_STRINGS |
                            G_PARAM_CONSTRUCT);

    properties[PROP_PAUSE_ON_CONTROLLER_DISCONNECT] =
        g_param_spec_boolean ("pause-on-controller-disconnect",
                              "Pause on Controller Disconnect",
                              "Show reconnect prompt",
                              TRUE,
                              G_PARAM_READWRITE |
                              G_PARAM_STATIC_STRINGS |
                              G_PARAM_CONSTRUCT);

    /* Debug properties */
    properties[PROP_ENABLE_DEBUG_OVERLAY] =
        g_param_spec_boolean ("enable-debug-overlay",
                              "Enable Debug Overlay",
                              "Show FPS/debug info",
                              FALSE,
                              G_PARAM_READWRITE |
                              G_PARAM_STATIC_STRINGS |
                              G_PARAM_CONSTRUCT);

    properties[PROP_ENABLE_DEBUG_CONSOLE] =
        g_param_spec_boolean ("enable-debug-console",
                              "Enable Debug Console",
                              "Enable ~ console",
                              FALSE,
                              G_PARAM_READWRITE |
                              G_PARAM_STATIC_STRINGS |
                              G_PARAM_CONSTRUCT);

    properties[PROP_LOG_FRAME_DROPS] =
        g_param_spec_boolean ("log-frame-drops",
                              "Log Frame Drops",
                              "Log when frames exceed target",
                              FALSE,
                              G_PARAM_READWRITE |
                              G_PARAM_STATIC_STRINGS |
                              G_PARAM_CONSTRUCT);

    /* Error handling properties */
    properties[PROP_SHOW_ERROR_SCREEN_ON_CRASH] =
        g_param_spec_boolean ("show-error-screen-on-crash",
                              "Show Error Screen on Crash",
                              "Show error state vs hard crash",
                              TRUE,
                              G_PARAM_READWRITE |
                              G_PARAM_STATIC_STRINGS |
                              G_PARAM_CONSTRUCT);

    properties[PROP_ERROR_SCREEN_ALLOW_RETRY] =
        g_param_spec_boolean ("error-screen-allow-retry",
                              "Error Screen Allow Retry",
                              "Allow retry from error screen",
                              TRUE,
                              G_PARAM_READWRITE |
                              G_PARAM_STATIC_STRINGS |
                              G_PARAM_CONSTRUCT);

    /* Theming properties */
    properties[PROP_BACKGROUND_COLOR] =
        g_param_spec_boxed ("background-color",
                            "Background Color",
                            "Default clear color",
                            GRL_TYPE_COLOR,
                            G_PARAM_READWRITE |
                            G_PARAM_STATIC_STRINGS);

    properties[PROP_BASE_FONT_PATH] =
        g_param_spec_string ("base-font-path",
                             "Base Font Path",
                             "Custom font path (NULL = system fallback)",
                             NULL,
                             G_PARAM_READWRITE |
                             G_PARAM_STATIC_STRINGS |
                             G_PARAM_CONSTRUCT);

    properties[PROP_UI_FONT_SIZE] =
        g_param_spec_int ("ui-font-size",
                          "UI Font Size",
                          "Default UI font size",
                          8, 72, 16,
                          G_PARAM_READWRITE |
                          G_PARAM_STATIC_STRINGS |
                          G_PARAM_CONSTRUCT);

    properties[PROP_THEME] =
        g_param_spec_object ("theme",
                             "Theme",
                             "UI theme",
                             LRG_TYPE_THEME,
                             G_PARAM_READWRITE |
                             G_PARAM_STATIC_STRINGS);

    /* Input buffering properties */
    properties[PROP_ENABLE_INPUT_BUFFERING] =
        g_param_spec_boolean ("enable-input-buffering",
                              "Enable Input Buffering",
                              "Enable input buffer for action games",
                              FALSE,
                              G_PARAM_READWRITE |
                              G_PARAM_STATIC_STRINGS |
                              G_PARAM_CONSTRUCT);

    properties[PROP_INPUT_BUFFER_FRAMES] =
        g_param_spec_int ("input-buffer-frames",
                          "Input Buffer Frames",
                          "Frames to buffer inputs",
                          1, 20, LRG_DEFAULT_INPUT_BUFFER_FRAMES,
                          G_PARAM_READWRITE |
                          G_PARAM_STATIC_STRINGS |
                          G_PARAM_CONSTRUCT);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_game_template_init (LrgGameTemplate *self)
{
    LrgGameTemplatePrivate *priv;

    priv = lrg_game_template_get_instance_private (self);

    /* Initialize defaults */
    priv->engine = NULL;
    priv->state_manager = NULL;
    priv->input_map = NULL;
    priv->settings = NULL;
    priv->event_bus = NULL;
    priv->theme = NULL;

    priv->title = g_strdup ("Libregnum Game");
    priv->window_width = 1280;
    priv->window_height = 720;
    priv->min_width = 640;
    priv->min_height = 360;
    priv->fullscreen_mode = LRG_FULLSCREEN_WINDOWED;
    priv->vsync = TRUE;
    priv->target_fps = 60;
    priv->allow_resize = TRUE;
    priv->allow_alt_enter = TRUE;

    priv->use_fixed_timestep = TRUE;
    priv->fixed_timestep = LRG_DEFAULT_FIXED_TIMESTEP;
    priv->max_frame_time = LRG_DEFAULT_MAX_FRAME_TIME;
    priv->max_updates_per_frame = LRG_DEFAULT_MAX_UPDATES_PER_FRAME;
    priv->accumulator = 0.0;
    priv->interpolation_alpha = 0.0;

    priv->hit_stop_remaining = 0.0;
    priv->saved_time_scale = 1.0;
    priv->time_scale = 1.0;

    priv->should_quit = FALSE;
    priv->is_paused = FALSE;
    priv->has_focus = TRUE;
    priv->is_running = FALSE;
    priv->in_state_transition = FALSE;

    priv->deferred_state_ops = g_ptr_array_new_with_free_func (
        (GDestroyNotify)lrg_deferred_state_op_free);

    priv->enable_auto_save = TRUE;
    priv->auto_save_interval = LRG_DEFAULT_AUTO_SAVE_INTERVAL;
    priv->auto_save_timer = 0.0;
    priv->app_id = NULL;
    priv->use_atomic_saves = TRUE;

    priv->pause_on_focus_loss = TRUE;
    priv->duck_audio_on_focus_loss = TRUE;
    priv->focus_loss_duck_factor = LRG_DEFAULT_FOCUS_LOSS_DUCK_FACTOR;
    priv->saved_sfx_volume = 1.0f;
    priv->saved_music_volume = 1.0f;
    priv->pause_on_controller_disconnect = TRUE;
    priv->gamepad_connected = FALSE;

    priv->enable_debug_overlay = FALSE;
    priv->enable_debug_console = FALSE;
    priv->log_frame_drops = FALSE;

    priv->show_error_screen_on_crash = TRUE;
    priv->error_screen_allow_retry = TRUE;

    priv->background_color = grl_color_new (0, 0, 0, 255);
    priv->base_font_path = NULL;
    priv->ui_font_size = 16;

    priv->enable_input_buffering = FALSE;
    priv->input_buffer_frames = LRG_DEFAULT_INPUT_BUFFER_FRAMES;
    priv->input_buffer = NULL;

    priv->enable_settings = TRUE;
    priv->enable_crash_reporter = TRUE;

    priv->signal_handlers = g_ptr_array_new ();

    /* Game feel - screen shake */
    priv->screen_shake = NULL;  /* Created lazily */
    priv->default_shake_decay = LRG_DEFAULT_SHAKE_DECAY;
    priv->default_shake_max_offset_x = LRG_DEFAULT_SHAKE_MAX_OFFSET_X;
    priv->default_shake_max_offset_y = LRG_DEFAULT_SHAKE_MAX_OFFSET_Y;
    priv->default_shake_frequency = LRG_DEFAULT_SHAKE_FREQUENCY;

    /* Game feel - sound variation */
    priv->default_sound_bank = NULL;
    priv->default_pitch_variance = LRG_DEFAULT_PITCH_VARIANCE;
    priv->default_volume_variance = LRG_DEFAULT_VOLUME_VARIANCE;

    /* Game feel - camera follow */
    priv->camera_follow_enabled = FALSE;
    priv->camera_follow_target_x = 0.0f;
    priv->camera_follow_target_y = 0.0f;
    priv->camera_follow_smoothing = LRG_DEFAULT_CAMERA_SMOOTHING;
    priv->camera_deadzone_x = LRG_DEFAULT_CAMERA_DEADZONE;
    priv->camera_deadzone_y = LRG_DEFAULT_CAMERA_DEADZONE;

    /* Game feel - camera zoom pulse */
    priv->camera_zoom_pulse_target = 1.0f;
    priv->camera_zoom_pulse_duration = 0.0f;
    priv->camera_zoom_pulse_timer = 0.0f;
    priv->camera_zoom_pulse_original = 1.0f;
}

/* ========================================================================== */
/* Public API                                                                 */
/* ========================================================================== */

/**
 * lrg_game_template_new:
 *
 * Creates a new game template with default settings.
 *
 * Returns: (transfer full): a new #LrgGameTemplate
 *
 * Since: 1.0
 */
LrgGameTemplate *
lrg_game_template_new (void)
{
    return g_object_new (LRG_TYPE_GAME_TEMPLATE, NULL);
}

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
gint
lrg_game_template_run (LrgGameTemplate *self,
                       int              argc,
                       char           **argv)
{
    LrgGameTemplatePrivate *priv;
    LrgGameTemplateClass *klass;
    g_autoptr(GError) error = NULL;
    LrgGrlWindow *grl_win;
    GrlWindow *raw_window;
    LrgGameState *initial_state;

    g_return_val_if_fail (LRG_IS_GAME_TEMPLATE (self), 1);

    priv = lrg_game_template_get_instance_private (self);
    klass = LRG_GAME_TEMPLATE_GET_CLASS (self);

    /* 1. Configure (vfunc) */
    if (klass->configure != NULL)
        klass->configure (self);

    /* 2. Get engine singleton (NOT owned) */
    priv->engine = lrg_engine_get_default ();

    /* 3. Start engine */
    if (!lrg_engine_startup (priv->engine, &error))
    {
        lrg_warning (LRG_LOG_DOMAIN_TEMPLATE,
                     "Engine startup failed: %s", error->message);
        return 1;
    }

    /* 4. Create window via LrgGrlWindow wrapper */
    grl_win = lrg_grl_window_new (priv->window_width,
                                  priv->window_height,
                                  priv->title);

    if (grl_win == NULL)
    {
        lrg_warning (LRG_LOG_DOMAIN_TEMPLATE, "Failed to create window");
        lrg_engine_shutdown (priv->engine);
        return 1;
    }

    priv->window = LRG_WINDOW (grl_win);

    /* Configure window via raw GrlWindow for features not in LrgWindow */
    raw_window = lrg_grl_window_get_grl_window (grl_win);

    lrg_window_set_target_fps (priv->window, priv->target_fps);

    if (priv->vsync)
        lrg_grl_window_set_vsync (grl_win, TRUE);

    if (priv->allow_resize)
        grl_window_set_state (raw_window, GRL_FLAG_WINDOW_RESIZABLE);

    grl_window_set_min_size (raw_window, priv->min_width, priv->min_height);

    /* Apply fullscreen mode */
    if (priv->fullscreen_mode == LRG_FULLSCREEN_FULLSCREEN)
        lrg_grl_window_toggle_fullscreen (grl_win);
    else if (priv->fullscreen_mode == LRG_FULLSCREEN_BORDERLESS)
        grl_window_toggle_borderless (raw_window);

    /* 5. Initialize subsystems */
    priv->state_manager = lrg_game_state_manager_new ();
    priv->input_map = lrg_input_map_new ();
    priv->event_bus = lrg_event_bus_new ();
    priv->settings = lrg_settings_new ();

    /* 6. Create/get theme */
    if (klass->create_theme != NULL)
        priv->theme = klass->create_theme (self);
    if (priv->theme == NULL)
        priv->theme = lrg_theme_get_default ();

    /* 7. Load settings if configured */
    if (priv->enable_settings && priv->app_id != NULL)
    {
        g_autoptr(GError) settings_error = NULL;
        g_autofree gchar *settings_path = NULL;

        settings_path = g_build_filename (g_get_user_config_dir (),
                                          priv->app_id,
                                          "settings.yaml",
                                          NULL);

        /* Don't fail if settings don't exist yet */
        lrg_settings_load (priv->settings, settings_path, NULL);
    }

    /* 8. Setup default input (vfunc) */
    if (klass->setup_default_input != NULL)
        klass->setup_default_input (self, priv->input_map);

    /* 9. Register custom types */
    if (klass->register_types != NULL)
    {
        LrgRegistry *registry;

        registry = lrg_engine_get_registry (priv->engine);
        klass->register_types (self, registry);
    }

    /* 10. Pre-startup hook (vfunc) */
    if (klass->pre_startup != NULL)
        klass->pre_startup (self);

    /* 11. Create and push initial state */
    initial_state = NULL;
    if (klass->create_initial_state != NULL)
        initial_state = klass->create_initial_state (self);

    if (initial_state != NULL)
    {
        lrg_game_state_manager_push (priv->state_manager, initial_state);
    }
    else
    {
        lrg_warning (LRG_LOG_DOMAIN_TEMPLATE,
                     "No initial state created - game will have no active state");
    }

    /* 12. Post-startup hook (vfunc) */
    if (klass->post_startup != NULL)
        klass->post_startup (self);

    /* Mark as running */
    priv->is_running = TRUE;
    priv->has_focus = grl_window_is_focused (raw_window);
    priv->gamepad_connected = grl_input_is_gamepad_available (0);

    /* 13. Main loop */
    while (!priv->should_quit && !lrg_window_should_close (priv->window))
    {
        template_frame (self);
    }

    /* Mark as not running */
    priv->is_running = FALSE;

    /* 14. Shutdown hook (vfunc) */
    if (klass->shutdown != NULL)
        klass->shutdown (self);

    /* 15. Save settings if configured */
    if (priv->enable_settings && priv->app_id != NULL)
    {
        g_autofree gchar *settings_dir = NULL;
        g_autofree gchar *settings_path = NULL;

        settings_dir = g_build_filename (g_get_user_config_dir (),
                                         priv->app_id,
                                         NULL);

        g_mkdir_with_parents (settings_dir, 0755);

        settings_path = g_build_filename (settings_dir,
                                          "settings.yaml",
                                          NULL);

        lrg_settings_save (priv->settings, settings_path, NULL);
    }

    /* 16. Clear states */
    lrg_game_state_manager_clear (priv->state_manager);

    /* 17. Shutdown engine */
    lrg_engine_shutdown (priv->engine);

    /* Window is auto-closed when unreferenced (g_autoptr) */

    return 0;
}

/**
 * lrg_game_template_quit:
 * @self: an #LrgGameTemplate
 *
 * Signals the game to quit after the current frame.
 *
 * Since: 1.0
 */
void
lrg_game_template_quit (LrgGameTemplate *self)
{
    LrgGameTemplatePrivate *priv;

    g_return_if_fail (LRG_IS_GAME_TEMPLATE (self));

    priv = lrg_game_template_get_instance_private (self);
    priv->should_quit = TRUE;
}

/**
 * lrg_game_template_pause:
 * @self: an #LrgGameTemplate
 *
 * Pauses the game by pushing the pause state.
 * Does nothing if already paused.
 *
 * Since: 1.0
 */
void
lrg_game_template_pause (LrgGameTemplate *self)
{
    LrgGameTemplatePrivate *priv;
    LrgGameTemplateClass *klass;
    LrgGameState *pause_state;

    g_return_if_fail (LRG_IS_GAME_TEMPLATE (self));

    priv = lrg_game_template_get_instance_private (self);

    if (priv->is_paused)
        return;

    klass = LRG_GAME_TEMPLATE_GET_CLASS (self);

    if (klass->create_pause_state != NULL)
    {
        pause_state = klass->create_pause_state (self);

        if (pause_state != NULL)
        {
            priv->is_paused = TRUE;
            lrg_game_template_push_state (self, pause_state);
        }
    }
}

/**
 * lrg_game_template_resume:
 * @self: an #LrgGameTemplate
 *
 * Resumes the game by popping the pause state.
 * Does nothing if not paused.
 *
 * Since: 1.0
 */
void
lrg_game_template_resume (LrgGameTemplate *self)
{
    LrgGameTemplatePrivate *priv;

    g_return_if_fail (LRG_IS_GAME_TEMPLATE (self));

    priv = lrg_game_template_get_instance_private (self);

    if (!priv->is_paused)
        return;

    priv->is_paused = FALSE;
    lrg_game_template_pop_state (self);
}

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
gboolean
lrg_game_template_is_paused (LrgGameTemplate *self)
{
    LrgGameTemplatePrivate *priv;

    g_return_val_if_fail (LRG_IS_GAME_TEMPLATE (self), FALSE);

    priv = lrg_game_template_get_instance_private (self);

    return priv->is_paused;
}

/**
 * lrg_game_template_push_state:
 * @self: an #LrgGameTemplate
 * @state: (transfer full): state to push
 *
 * Pushes a new state onto the state stack.
 *
 * Since: 1.0
 */
void
lrg_game_template_push_state (LrgGameTemplate *self,
                              LrgGameState    *state)
{
    LrgGameTemplatePrivate *priv;

    g_return_if_fail (LRG_IS_GAME_TEMPLATE (self));
    g_return_if_fail (LRG_IS_GAME_STATE (state));

    priv = lrg_game_template_get_instance_private (self);

    if (priv->in_state_transition)
    {
        /* Defer until after current transition */
        LrgDeferredStateOp *op;

        op = lrg_deferred_state_op_new (LRG_STATE_OP_PUSH, state);
        g_ptr_array_add (priv->deferred_state_ops, op);
        g_object_unref (state); /* Op took a ref, release caller's ref */
        return;
    }

    lrg_game_state_manager_push (priv->state_manager, state);
}

/**
 * lrg_game_template_pop_state:
 * @self: an #LrgGameTemplate
 *
 * Pops the current state from the stack.
 *
 * Since: 1.0
 */
void
lrg_game_template_pop_state (LrgGameTemplate *self)
{
    LrgGameTemplatePrivate *priv;

    g_return_if_fail (LRG_IS_GAME_TEMPLATE (self));

    priv = lrg_game_template_get_instance_private (self);

    if (priv->in_state_transition)
    {
        /* Defer until after current transition */
        LrgDeferredStateOp *op;

        op = lrg_deferred_state_op_new (LRG_STATE_OP_POP, NULL);
        g_ptr_array_add (priv->deferred_state_ops, op);
        return;
    }

    lrg_game_state_manager_pop (priv->state_manager);
}

/**
 * lrg_game_template_replace_state:
 * @self: an #LrgGameTemplate
 * @state: (transfer full): new state
 *
 * Replaces the current state with a new one.
 *
 * Since: 1.0
 */
void
lrg_game_template_replace_state (LrgGameTemplate *self,
                                 LrgGameState    *state)
{
    LrgGameTemplatePrivate *priv;

    g_return_if_fail (LRG_IS_GAME_TEMPLATE (self));
    g_return_if_fail (LRG_IS_GAME_STATE (state));

    priv = lrg_game_template_get_instance_private (self);

    if (priv->in_state_transition)
    {
        /* Defer until after current transition */
        LrgDeferredStateOp *op;

        op = lrg_deferred_state_op_new (LRG_STATE_OP_REPLACE, state);
        g_ptr_array_add (priv->deferred_state_ops, op);
        g_object_unref (state); /* Op took a ref, release caller's ref */
        return;
    }

    lrg_game_state_manager_replace (priv->state_manager, state);
}

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
LrgGameState *
lrg_game_template_get_current_state (LrgGameTemplate *self)
{
    LrgGameTemplatePrivate *priv;

    g_return_val_if_fail (LRG_IS_GAME_TEMPLATE (self), NULL);

    priv = lrg_game_template_get_instance_private (self);

    return lrg_game_state_manager_get_current (priv->state_manager);
}

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
LrgEngine *
lrg_game_template_get_engine (LrgGameTemplate *self)
{
    LrgGameTemplatePrivate *priv;

    g_return_val_if_fail (LRG_IS_GAME_TEMPLATE (self), NULL);

    priv = lrg_game_template_get_instance_private (self);

    return priv->engine;
}

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
LrgSettings *
lrg_game_template_get_settings (LrgGameTemplate *self)
{
    LrgGameTemplatePrivate *priv;

    g_return_val_if_fail (LRG_IS_GAME_TEMPLATE (self), NULL);

    priv = lrg_game_template_get_instance_private (self);

    return priv->settings;
}

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
LrgInputMap *
lrg_game_template_get_input_map (LrgGameTemplate *self)
{
    LrgGameTemplatePrivate *priv;

    g_return_val_if_fail (LRG_IS_GAME_TEMPLATE (self), NULL);

    priv = lrg_game_template_get_instance_private (self);

    return priv->input_map;
}

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
LrgGameStateManager *
lrg_game_template_get_state_manager (LrgGameTemplate *self)
{
    LrgGameTemplatePrivate *priv;

    g_return_val_if_fail (LRG_IS_GAME_TEMPLATE (self), NULL);

    priv = lrg_game_template_get_instance_private (self);

    return priv->state_manager;
}

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
LrgEventBus *
lrg_game_template_get_event_bus (LrgGameTemplate *self)
{
    LrgGameTemplatePrivate *priv;

    g_return_val_if_fail (LRG_IS_GAME_TEMPLATE (self), NULL);

    priv = lrg_game_template_get_instance_private (self);

    return priv->event_bus;
}

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
LrgTheme *
lrg_game_template_get_theme (LrgGameTemplate *self)
{
    LrgGameTemplatePrivate *priv;

    g_return_val_if_fail (LRG_IS_GAME_TEMPLATE (self), NULL);

    priv = lrg_game_template_get_instance_private (self);

    return priv->theme;
}

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
void
lrg_game_template_hit_stop (LrgGameTemplate *self,
                            gdouble          duration)
{
    LrgGameTemplatePrivate *priv;

    g_return_if_fail (LRG_IS_GAME_TEMPLATE (self));
    g_return_if_fail (duration >= 0.0);

    priv = lrg_game_template_get_instance_private (self);

    /* Save current time scale and set hit stop */
    if (priv->hit_stop_remaining <= 0)
    {
        priv->saved_time_scale = priv->time_scale;
    }

    priv->hit_stop_remaining = duration;
}

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
gdouble
lrg_game_template_get_time_scale (LrgGameTemplate *self)
{
    LrgGameTemplatePrivate *priv;

    g_return_val_if_fail (LRG_IS_GAME_TEMPLATE (self), 1.0);

    priv = lrg_game_template_get_instance_private (self);

    return priv->time_scale;
}

/**
 * lrg_game_template_set_time_scale:
 * @self: an #LrgGameTemplate
 * @scale: time scale multiplier
 *
 * Sets the time scale multiplier for slow-motion or fast-forward effects.
 *
 * Since: 1.0
 */
void
lrg_game_template_set_time_scale (LrgGameTemplate *self,
                                  gdouble          scale)
{
    LrgGameTemplatePrivate *priv;

    g_return_if_fail (LRG_IS_GAME_TEMPLATE (self));
    g_return_if_fail (scale >= 0.0);

    priv = lrg_game_template_get_instance_private (self);

    priv->time_scale = scale;

    /* Also update saved time scale if not in hit stop */
    if (priv->hit_stop_remaining <= 0)
        priv->saved_time_scale = scale;
}

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
gdouble
lrg_game_template_get_interpolation_alpha (LrgGameTemplate *self)
{
    LrgGameTemplatePrivate *priv;

    g_return_val_if_fail (LRG_IS_GAME_TEMPLATE (self), 0.0);

    priv = lrg_game_template_get_instance_private (self);

    return priv->interpolation_alpha;
}

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
const gchar *
lrg_game_template_get_title (LrgGameTemplate *self)
{
    LrgGameTemplatePrivate *priv;

    g_return_val_if_fail (LRG_IS_GAME_TEMPLATE (self), NULL);

    priv = lrg_game_template_get_instance_private (self);

    return priv->title;
}

/**
 * lrg_game_template_set_title:
 * @self: an #LrgGameTemplate
 * @title: new window title
 *
 * Sets the window title.
 *
 * Since: 1.0
 */
void
lrg_game_template_set_title (LrgGameTemplate *self,
                             const gchar     *title)
{
    LrgGameTemplatePrivate *priv;

    g_return_if_fail (LRG_IS_GAME_TEMPLATE (self));

    priv = lrg_game_template_get_instance_private (self);

    g_free (priv->title);
    priv->title = g_strdup (title);

    /* Update window title if running */
    if (priv->is_running && priv->window != NULL)
    {
        lrg_window_set_title (priv->window, title);
    }
}

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
void
lrg_game_template_get_window_size (LrgGameTemplate *self,
                                   gint            *width,
                                   gint            *height)
{
    LrgGameTemplatePrivate *priv;

    g_return_if_fail (LRG_IS_GAME_TEMPLATE (self));

    priv = lrg_game_template_get_instance_private (self);

    if (priv->is_running && priv->window != NULL)
    {
        if (width != NULL)
            *width = lrg_window_get_width (priv->window);
        if (height != NULL)
            *height = lrg_window_get_height (priv->window);
    }
    else
    {
        if (width != NULL)
            *width = priv->window_width;
        if (height != NULL)
            *height = priv->window_height;
    }
}

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
void
lrg_game_template_set_window_size (LrgGameTemplate *self,
                                   gint             width,
                                   gint             height)
{
    LrgGameTemplatePrivate *priv;
    GrlWindow *raw_window;

    g_return_if_fail (LRG_IS_GAME_TEMPLATE (self));
    g_return_if_fail (width > 0);
    g_return_if_fail (height > 0);

    priv = lrg_game_template_get_instance_private (self);

    /* Update stored config values */
    priv->window_width = width;
    priv->window_height = height;

    /* If running, apply to actual window */
    if (priv->is_running && priv->window != NULL)
    {
        raw_window = lrg_grl_window_get_grl_window (LRG_GRL_WINDOW (priv->window));
        grl_window_set_size (raw_window, width, height);
    }
}

/**
 * lrg_game_template_toggle_fullscreen:
 * @self: an #LrgGameTemplate
 *
 * Toggles fullscreen mode. In fullscreen, the window uses
 * the monitor's native resolution.
 *
 * Since: 1.0
 */
void
lrg_game_template_toggle_fullscreen (LrgGameTemplate *self)
{
    LrgGameTemplatePrivate *priv;

    g_return_if_fail (LRG_IS_GAME_TEMPLATE (self));

    priv = lrg_game_template_get_instance_private (self);

    if (priv->is_running && priv->window != NULL)
        lrg_grl_window_toggle_fullscreen (LRG_GRL_WINDOW (priv->window));
}

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
gboolean
lrg_game_template_is_fullscreen (LrgGameTemplate *self)
{
    LrgGameTemplatePrivate *priv;
    GrlWindow *raw_window;

    g_return_val_if_fail (LRG_IS_GAME_TEMPLATE (self), FALSE);

    priv = lrg_game_template_get_instance_private (self);

    if (!priv->is_running || priv->window == NULL)
        return FALSE;

    raw_window = lrg_grl_window_get_grl_window (LRG_GRL_WINDOW (priv->window));
    return grl_window_is_fullscreen (raw_window);
}

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
gboolean
lrg_game_template_has_focus (LrgGameTemplate *self)
{
    LrgGameTemplatePrivate *priv;

    g_return_val_if_fail (LRG_IS_GAME_TEMPLATE (self), FALSE);

    priv = lrg_game_template_get_instance_private (self);

    return priv->has_focus;
}

/* ========================================================================== */
/* Screen Shake                                                               */
/* ========================================================================== */

static void
template_ensure_screen_shake (LrgGameTemplate *self)
{
    LrgGameTemplatePrivate *priv;

    priv = lrg_game_template_get_instance_private (self);

    if (priv->screen_shake == NULL)
    {
        priv->screen_shake = lrg_screen_shake_new ();
        lrg_screen_shake_set_decay (priv->screen_shake, priv->default_shake_decay);
        lrg_screen_shake_set_max_offset (priv->screen_shake,
                                          priv->default_shake_max_offset_x,
                                          priv->default_shake_max_offset_y);
        lrg_screen_shake_set_frequency (priv->screen_shake, priv->default_shake_frequency);
    }
}

/**
 * lrg_game_template_shake:
 * @self: an #LrgGameTemplate
 * @trauma: trauma amount to add (0.0 to 1.0)
 *
 * Adds screen shake trauma.
 *
 * Since: 1.0
 */
void
lrg_game_template_shake (LrgGameTemplate *self,
                          gfloat           trauma)
{
    LrgGameTemplatePrivate *priv;

    g_return_if_fail (LRG_IS_GAME_TEMPLATE (self));

    priv = lrg_game_template_get_instance_private (self);

    template_ensure_screen_shake (self);

    lrg_screen_shake_add_trauma (priv->screen_shake, trauma);
}

/**
 * lrg_game_template_shake_with_params:
 * @self: an #LrgGameTemplate
 * @trauma: trauma amount to add (0.0 to 1.0)
 * @decay: decay rate per second
 * @frequency: shake frequency in Hz
 *
 * Adds screen shake with custom parameters.
 *
 * Since: 1.0
 */
void
lrg_game_template_shake_with_params (LrgGameTemplate *self,
                                      gfloat           trauma,
                                      gfloat           decay,
                                      gfloat           frequency)
{
    LrgGameTemplatePrivate *priv;

    g_return_if_fail (LRG_IS_GAME_TEMPLATE (self));

    priv = lrg_game_template_get_instance_private (self);

    template_ensure_screen_shake (self);

    lrg_screen_shake_set_decay (priv->screen_shake, decay);
    lrg_screen_shake_set_frequency (priv->screen_shake, frequency);
    lrg_screen_shake_add_trauma (priv->screen_shake, trauma);
}

/**
 * lrg_game_template_get_shake_offset:
 * @self: an #LrgGameTemplate
 * @x: (out) (optional): return location for X offset
 * @y: (out) (optional): return location for Y offset
 *
 * Gets the current screen shake offset.
 *
 * Since: 1.0
 */
void
lrg_game_template_get_shake_offset (LrgGameTemplate *self,
                                     gfloat          *x,
                                     gfloat          *y)
{
    LrgGameTemplatePrivate *priv;

    g_return_if_fail (LRG_IS_GAME_TEMPLATE (self));

    priv = lrg_game_template_get_instance_private (self);

    if (priv->screen_shake != NULL)
    {
        lrg_screen_shake_get_current_offset (priv->screen_shake, x, y);
    }
    else
    {
        if (x != NULL)
            *x = 0.0f;
        if (y != NULL)
            *y = 0.0f;
    }
}

/* ========================================================================== */
/* Audio Helpers                                                              */
/* ========================================================================== */

/**
 * lrg_game_template_set_sound_bank:
 * @self: an #LrgGameTemplate
 * @bank: (transfer none): the sound bank to use
 *
 * Sets the default sound bank for play_sound convenience methods.
 *
 * Since: 1.0
 */
void
lrg_game_template_set_sound_bank (LrgGameTemplate *self,
                                   gpointer         bank)
{
    LrgGameTemplatePrivate *priv;

    g_return_if_fail (LRG_IS_GAME_TEMPLATE (self));

    priv = lrg_game_template_get_instance_private (self);

    /* Not taking ownership - just storing reference */
    priv->default_sound_bank = (LrgSoundBank *)bank;
}

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
gboolean
lrg_game_template_play_sound (LrgGameTemplate *self,
                               const gchar     *sound_name)
{
    LrgGameTemplatePrivate *priv;

    g_return_val_if_fail (LRG_IS_GAME_TEMPLATE (self), FALSE);
    g_return_val_if_fail (sound_name != NULL, FALSE);

    priv = lrg_game_template_get_instance_private (self);

    if (priv->default_sound_bank == NULL)
    {
        lrg_debug (LRG_LOG_DOMAIN_TEMPLATE,
                   "No sound bank set - cannot play '%s'", sound_name);
        return FALSE;
    }

    return lrg_sound_bank_play (priv->default_sound_bank, sound_name);
}

/**
 * lrg_game_template_play_sound_varied:
 * @self: an #LrgGameTemplate
 * @sound_name: name of the sound in the default bank
 * @pitch_variance: pitch variance in semitones (±)
 * @volume_variance: volume variance as fraction (±)
 *
 * Plays a sound with random pitch and volume variation.
 *
 * Returns: %TRUE if the sound was found and played
 *
 * Since: 1.0
 */
gboolean
lrg_game_template_play_sound_varied (LrgGameTemplate *self,
                                      const gchar     *sound_name,
                                      gfloat           pitch_variance,
                                      gfloat           volume_variance)
{
    LrgGameTemplatePrivate *priv;
    GrlSound *sound;
    gfloat pitch;
    gfloat volume;
    gfloat semitones;

    g_return_val_if_fail (LRG_IS_GAME_TEMPLATE (self), FALSE);
    g_return_val_if_fail (sound_name != NULL, FALSE);

    priv = lrg_game_template_get_instance_private (self);

    if (priv->default_sound_bank == NULL)
    {
        lrg_debug (LRG_LOG_DOMAIN_TEMPLATE,
                   "No sound bank set - cannot play '%s'", sound_name);
        return FALSE;
    }

    sound = lrg_sound_bank_get (priv->default_sound_bank, sound_name);
    if (sound == NULL)
    {
        lrg_debug (LRG_LOG_DOMAIN_TEMPLATE,
                   "Sound '%s' not found in bank", sound_name);
        return FALSE;
    }

    /* Calculate randomized pitch: 2^(semitones/12) */
    semitones = (gfloat)g_random_double_range (-pitch_variance, pitch_variance);
    pitch = powf (2.0f, semitones / 12.0f);

    /* Calculate randomized volume */
    volume = 1.0f + (gfloat)g_random_double_range (-volume_variance, volume_variance);
    if (volume < 0.0f)
        volume = 0.0f;

    /* Play with modified pitch and volume */
    grl_sound_set_pitch (sound, pitch);
    grl_sound_set_volume (sound, volume);
    grl_sound_play_multi (sound);

    /* Reset to defaults after playing */
    grl_sound_set_pitch (sound, 1.0f);
    grl_sound_set_volume (sound, 1.0f);

    return TRUE;
}

/* ========================================================================== */
/* Camera Juice                                                               */
/* ========================================================================== */

/**
 * lrg_game_template_camera_zoom_pulse:
 * @self: an #LrgGameTemplate
 * @zoom_delta: amount to change zoom by
 * @duration: duration in seconds to return to normal
 *
 * Creates a quick zoom pulse effect.
 *
 * Since: 1.0
 */
void
lrg_game_template_camera_zoom_pulse (LrgGameTemplate *self,
                                      gfloat           zoom_delta,
                                      gfloat           duration)
{
    LrgGameTemplatePrivate *priv;

    g_return_if_fail (LRG_IS_GAME_TEMPLATE (self));
    g_return_if_fail (duration > 0.0f);

    priv = lrg_game_template_get_instance_private (self);

    /* Store original zoom (assume 1.0 if not in a pulse already) */
    if (priv->camera_zoom_pulse_timer <= 0.0f)
    {
        priv->camera_zoom_pulse_original = 1.0f;
    }

    priv->camera_zoom_pulse_target = priv->camera_zoom_pulse_original + zoom_delta;
    priv->camera_zoom_pulse_duration = duration;
    priv->camera_zoom_pulse_timer = duration;
}

/**
 * lrg_game_template_set_camera_follow:
 * @self: an #LrgGameTemplate
 * @enabled: whether to enable camera follow
 * @smoothing: smoothing factor (0.0 = instant, 1.0 = very slow)
 *
 * Enables or disables smooth camera following.
 *
 * Since: 1.0
 */
void
lrg_game_template_set_camera_follow (LrgGameTemplate *self,
                                      gboolean         enabled,
                                      gfloat           smoothing)
{
    LrgGameTemplatePrivate *priv;

    g_return_if_fail (LRG_IS_GAME_TEMPLATE (self));

    priv = lrg_game_template_get_instance_private (self);

    priv->camera_follow_enabled = enabled;
    priv->camera_follow_smoothing = CLAMP (smoothing, 0.0f, 1.0f);
}

/**
 * lrg_game_template_set_camera_deadzone:
 * @self: an #LrgGameTemplate
 * @deadzone_x: horizontal deadzone size
 * @deadzone_y: vertical deadzone size
 *
 * Sets the camera deadzone.
 *
 * Since: 1.0
 */
void
lrg_game_template_set_camera_deadzone (LrgGameTemplate *self,
                                        gfloat           deadzone_x,
                                        gfloat           deadzone_y)
{
    LrgGameTemplatePrivate *priv;

    g_return_if_fail (LRG_IS_GAME_TEMPLATE (self));

    priv = lrg_game_template_get_instance_private (self);

    priv->camera_deadzone_x = deadzone_x;
    priv->camera_deadzone_y = deadzone_y;
}

/**
 * lrg_game_template_update_camera_follow_target:
 * @self: an #LrgGameTemplate
 * @target_x: target X position
 * @target_y: target Y position
 *
 * Updates the camera follow target position.
 *
 * Since: 1.0
 */
void
lrg_game_template_update_camera_follow_target (LrgGameTemplate *self,
                                                gfloat           target_x,
                                                gfloat           target_y)
{
    LrgGameTemplatePrivate *priv;

    g_return_if_fail (LRG_IS_GAME_TEMPLATE (self));

    priv = lrg_game_template_get_instance_private (self);

    priv->camera_follow_target_x = target_x;
    priv->camera_follow_target_y = target_y;
}

/**
 * lrg_game_template_get_camera_position:
 * @self: an #LrgGameTemplate
 * @x: (out) (optional): return location for camera X
 * @y: (out) (optional): return location for camera Y
 *
 * Gets the current camera position (after follow and shake).
 *
 * Since: 1.0
 */
void
lrg_game_template_get_camera_position (LrgGameTemplate *self,
                                        gfloat          *x,
                                        gfloat          *y)
{
    LrgGameTemplatePrivate *priv;
    gfloat cam_x;
    gfloat cam_y;
    gfloat shake_x;
    gfloat shake_y;

    g_return_if_fail (LRG_IS_GAME_TEMPLATE (self));

    priv = lrg_game_template_get_instance_private (self);

    /* Start with follow target if enabled, otherwise 0 */
    if (priv->camera_follow_enabled)
    {
        cam_x = priv->camera_follow_target_x;
        cam_y = priv->camera_follow_target_y;
    }
    else
    {
        cam_x = 0.0f;
        cam_y = 0.0f;
    }

    /* Add shake offset */
    lrg_game_template_get_shake_offset (self, &shake_x, &shake_y);
    cam_x += shake_x;
    cam_y += shake_y;

    if (x != NULL)
        *x = cam_x;
    if (y != NULL)
        *y = cam_y;
}
