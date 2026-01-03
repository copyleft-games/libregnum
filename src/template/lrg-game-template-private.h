/* lrg-game-template-private.h - Private data structures for game template
 *
 * Copyright 2025 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * This file is part of Libregnum.
 */

#ifndef LRG_GAME_TEMPLATE_PRIVATE_H
#define LRG_GAME_TEMPLATE_PRIVATE_H

#include "lrg-game-template.h"
#include "../lrg-enums.h"

G_BEGIN_DECLS

/* Forward declarations */
typedef struct _LrgInputBuffer LrgInputBuffer;
typedef struct _GrlColor GrlColor;
typedef struct _LrgScreenShake LrgScreenShake;
typedef struct _LrgSoundBank LrgSoundBank;
typedef struct _LrgCamera2D LrgCamera2D;
typedef struct _LrgWindow LrgWindow;

/**
 * LrgGameTemplatePrivate:
 *
 * Private data for #LrgGameTemplate.
 */
typedef struct _LrgGameTemplatePrivate
{
    /* Engine coordination (NOT owned - it's a singleton) */
    LrgEngine           *engine;

    /* Window (owned) */
    LrgWindow           *window;

    /* Owned subsystems */
    LrgGameStateManager *state_manager;
    LrgInputMap         *input_map;
    LrgSettings         *settings;
    LrgEventBus         *event_bus;
    LrgTheme            *theme;

    /* Window settings */
    gchar               *title;
    gint                 window_width;
    gint                 window_height;
    gint                 min_width;
    gint                 min_height;
    LrgFullscreenMode    fullscreen_mode;
    gboolean             vsync;
    gint                 target_fps;
    gboolean             allow_resize;
    gboolean             allow_alt_enter;

    /* Frame timing */
    gboolean             use_fixed_timestep;
    gdouble              fixed_timestep;        /* Default: 1.0/60.0 */
    gdouble              max_frame_time;        /* Default: 0.25 (4 FPS min) */
    gint                 max_updates_per_frame; /* Default: 5 */
    gdouble              accumulator;
    gdouble              interpolation_alpha;   /* For render interpolation */

    /* Hit stop / time scale */
    gdouble              hit_stop_remaining;
    gdouble              saved_time_scale;
    gdouble              time_scale;            /* Default: 1.0 */

    /* State flags */
    gboolean             should_quit;
    gboolean             is_paused;
    gboolean             has_focus;
    gboolean             is_running;
    gboolean             in_state_transition;

    /* Deferred state operations */
    GPtrArray           *deferred_state_ops;

    /* Auto-save */
    gboolean             enable_auto_save;
    gdouble              auto_save_interval;
    gdouble              auto_save_timer;
    gchar               *app_id;
    gboolean             use_atomic_saves;

    /* Focus handling */
    gboolean             pause_on_focus_loss;
    gboolean             duck_audio_on_focus_loss;
    gfloat               focus_loss_duck_factor;
    gfloat               saved_sfx_volume;
    gfloat               saved_music_volume;
    gboolean             pause_on_controller_disconnect;
    gboolean             gamepad_connected;

    /* Debug */
    gboolean             enable_debug_overlay;
    gboolean             enable_debug_console;
    gboolean             log_frame_drops;

    /* Error handling */
    gboolean             show_error_screen_on_crash;
    gboolean             error_screen_allow_retry;

    /* Theming */
    GrlColor            *background_color;
    gchar               *base_font_path;
    gint                 ui_font_size;

    /* Input buffering */
    gboolean             enable_input_buffering;
    gint                 input_buffer_frames;
    LrgInputBuffer      *input_buffer;

    /* Settings integration */
    gboolean             enable_settings;
    gboolean             enable_crash_reporter;

    /* Signal handler tracking for cleanup */
    GPtrArray           *signal_handlers;

    /* Screen shake (game feel) */
    LrgScreenShake      *screen_shake;
    gfloat               default_shake_decay;
    gfloat               default_shake_max_offset_x;
    gfloat               default_shake_max_offset_y;
    gfloat               default_shake_frequency;

    /* Sound banks (game feel) */
    LrgSoundBank        *default_sound_bank;
    gfloat               default_pitch_variance;
    gfloat               default_volume_variance;

    /* Camera follow (game feel) */
    gboolean             camera_follow_enabled;
    gfloat               camera_follow_target_x;
    gfloat               camera_follow_target_y;
    gfloat               camera_follow_smoothing;
    gfloat               camera_deadzone_x;
    gfloat               camera_deadzone_y;

    /* Camera zoom pulse */
    gfloat               camera_zoom_pulse_target;
    gfloat               camera_zoom_pulse_duration;
    gfloat               camera_zoom_pulse_timer;
    gfloat               camera_zoom_pulse_original;

} LrgGameTemplatePrivate;

/**
 * LrgDeferredStateOp:
 * @op_type: Operation type (push, pop, replace)
 * @state: State for push/replace operations
 *
 * Represents a deferred state operation to avoid recursion issues.
 */
typedef enum
{
    LRG_STATE_OP_PUSH,
    LRG_STATE_OP_POP,
    LRG_STATE_OP_REPLACE
} LrgStateOpType;

typedef struct
{
    LrgStateOpType  op_type;
    LrgGameState   *state;      /* For push/replace, NULL for pop */
} LrgDeferredStateOp;

/* Private helper functions */
LrgDeferredStateOp *
lrg_deferred_state_op_new (LrgStateOpType  op_type,
                           LrgGameState   *state);

void
lrg_deferred_state_op_free (LrgDeferredStateOp *op);

/* Default fixed timestep value: 1/60 second */
#define LRG_DEFAULT_FIXED_TIMESTEP (1.0 / 60.0)

/* Maximum delta time before clamping (prevents physics explosion) */
#define LRG_DEFAULT_MAX_FRAME_TIME (0.25)

/* Maximum fixed updates per frame (prevents spiral of death) */
#define LRG_DEFAULT_MAX_UPDATES_PER_FRAME (5)

/* Default auto-save interval in seconds */
#define LRG_DEFAULT_AUTO_SAVE_INTERVAL (60.0)

/* Default audio duck factor when focus is lost */
#define LRG_DEFAULT_FOCUS_LOSS_DUCK_FACTOR (0.2f)

/* Default input buffer frames for action games */
#define LRG_DEFAULT_INPUT_BUFFER_FRAMES (6)

/* Default screen shake parameters */
#define LRG_DEFAULT_SHAKE_DECAY        (0.8f)
#define LRG_DEFAULT_SHAKE_MAX_OFFSET_X (10.0f)
#define LRG_DEFAULT_SHAKE_MAX_OFFSET_Y (10.0f)
#define LRG_DEFAULT_SHAKE_FREQUENCY    (30.0f)

/* Default audio variation parameters */
#define LRG_DEFAULT_PITCH_VARIANCE     (0.0f)
#define LRG_DEFAULT_VOLUME_VARIANCE    (0.0f)

/* Default camera follow parameters */
#define LRG_DEFAULT_CAMERA_SMOOTHING   (0.1f)
#define LRG_DEFAULT_CAMERA_DEADZONE    (0.0f)

G_END_DECLS

#endif /* LRG_GAME_TEMPLATE_PRIVATE_H */
