/* lrg-photo-mode.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgPhotoMode - Singleton controller for photo mode.
 */

#include "config.h"

#include <errno.h>
#include <math.h>
#include <string.h>
#include <glib/gstdio.h>

#include "lrg-photo-mode.h"
#include "../lrg-log.h"

struct _LrgPhotoMode
{
    GObject parent_instance;

    /* State */
    gboolean active;
    gboolean ui_visible;

    /* Camera */
    LrgPhotoCameraController *camera_controller;

    /* Screenshot settings */
    gchar *screenshot_directory;
    LrgScreenshotFormat default_format;
    guint screenshot_counter;
    gchar *filename_prefix;

    /* M21: the game, the look, the lens, the overlays */
    LrgPhotoHost *host;
    LrgPhotoLook *look;
    LrgPhotoDof *dof;
    LrgPhotoFrame frame;
    LrgPhotoGrid grid;
    guint frame_color;

    /* the world contract */
    gboolean pause_world;
    gboolean world_frozen;
    gboolean hud_visible;
    gboolean time_override_set;
    gfloat time_override;
    gchar *weather_override;

    /* capture */
    guint capture_scale;
    guint max_capture_side;
    gboolean capture_pending;
    gchar *last_path;
    GHashTable *extra_meta;
    guint grain_seed;

    /* the timer */
    gfloat timer_seconds;
    gfloat timer_remaining;
    gboolean timer_running;
};

enum
{
    PROP_0,
    PROP_ACTIVE,
    PROP_UI_VISIBLE,
    PROP_SCREENSHOT_DIRECTORY,
    PROP_DEFAULT_FORMAT,
    PROP_HOST,
    PROP_LOOK,
    PROP_DOF,
    PROP_FRAME,
    PROP_GRID,
    PROP_FRAME_COLOR,
    PROP_PAUSE_WORLD,
    PROP_WORLD_FROZEN,
    PROP_HUD_VISIBLE,
    PROP_CAPTURE_SCALE,
    PROP_MAX_CAPTURE_SIDE,
    PROP_TIMER_SECONDS,
    PROP_FILENAME_PREFIX,
    N_PROPS
};

enum
{
    SIGNAL_ENTERED,
    SIGNAL_EXITED,
    SIGNAL_SCREENSHOT_TAKEN,
    SIGNAL_CAPTURE_REQUESTED,
    SIGNAL_TIMER_TICK,
    SIGNAL_PHOTO_SAVED,
    N_SIGNALS
};

static GParamSpec *properties[N_PROPS];
static guint signals[N_SIGNALS];

static LrgPhotoMode *default_instance = NULL;

G_DEFINE_TYPE (LrgPhotoMode, lrg_photo_mode, G_TYPE_OBJECT)

/* ==========================================================================
 * GObject Implementation
 * ========================================================================== */

static void
lrg_photo_mode_finalize (GObject *object)
{
    LrgPhotoMode *self = LRG_PHOTO_MODE (object);

    g_clear_object (&self->camera_controller);
    g_clear_pointer (&self->screenshot_directory, g_free);
    g_clear_pointer (&self->filename_prefix, g_free);
    g_clear_pointer (&self->weather_override, g_free);
    g_clear_pointer (&self->last_path, g_free);
    g_clear_pointer (&self->extra_meta, g_hash_table_unref);
    g_clear_object (&self->look);
    g_clear_object (&self->dof);
    g_clear_weak_pointer (&self->host);

    if (default_instance == self)
        default_instance = NULL;

    G_OBJECT_CLASS (lrg_photo_mode_parent_class)->finalize (object);
}

static void
lrg_photo_mode_get_property (GObject    *object,
                             guint       prop_id,
                             GValue     *value,
                             GParamSpec *pspec)
{
    LrgPhotoMode *self = LRG_PHOTO_MODE (object);

    switch (prop_id)
    {
    case PROP_ACTIVE:
        g_value_set_boolean (value, self->active);
        break;
    case PROP_UI_VISIBLE:
        g_value_set_boolean (value, self->ui_visible);
        break;
    case PROP_SCREENSHOT_DIRECTORY:
        g_value_set_string (value, self->screenshot_directory);
        break;
    case PROP_DEFAULT_FORMAT:
        g_value_set_int (value, self->default_format);
        break;
    case PROP_HOST:
        g_value_set_object (value, self->host);
        break;
    case PROP_LOOK:
        g_value_set_object (value, self->look);
        break;
    case PROP_DOF:
        g_value_set_object (value, self->dof);
        break;
    case PROP_FRAME:
        g_value_set_enum (value, self->frame);
        break;
    case PROP_GRID:
        g_value_set_enum (value, self->grid);
        break;
    case PROP_FRAME_COLOR:
        g_value_set_uint (value, self->frame_color);
        break;
    case PROP_PAUSE_WORLD:
        g_value_set_boolean (value, self->pause_world);
        break;
    case PROP_WORLD_FROZEN:
        g_value_set_boolean (value, self->world_frozen);
        break;
    case PROP_HUD_VISIBLE:
        g_value_set_boolean (value, self->hud_visible);
        break;
    case PROP_CAPTURE_SCALE:
        g_value_set_uint (value, self->capture_scale);
        break;
    case PROP_MAX_CAPTURE_SIDE:
        g_value_set_uint (value, self->max_capture_side);
        break;
    case PROP_TIMER_SECONDS:
        g_value_set_float (value, self->timer_seconds);
        break;
    case PROP_FILENAME_PREFIX:
        g_value_set_string (value, self->filename_prefix);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_photo_mode_set_property (GObject      *object,
                             guint         prop_id,
                             const GValue *value,
                             GParamSpec   *pspec)
{
    LrgPhotoMode *self = LRG_PHOTO_MODE (object);

    switch (prop_id)
    {
    case PROP_UI_VISIBLE:
        lrg_photo_mode_set_ui_visible (self, g_value_get_boolean (value));
        break;
    case PROP_SCREENSHOT_DIRECTORY:
        lrg_photo_mode_set_screenshot_directory (self, g_value_get_string (value));
        break;
    case PROP_DEFAULT_FORMAT:
        self->default_format = g_value_get_int (value);
        break;
    case PROP_HOST:
        lrg_photo_mode_set_host (self, g_value_get_object (value));
        break;
    case PROP_FRAME:
        lrg_photo_mode_set_frame (self, g_value_get_enum (value));
        break;
    case PROP_GRID:
        lrg_photo_mode_set_grid (self, g_value_get_enum (value));
        break;
    case PROP_FRAME_COLOR:
        lrg_photo_mode_set_frame_color (self, g_value_get_uint (value));
        break;
    case PROP_PAUSE_WORLD:
        lrg_photo_mode_set_pause_world (self, g_value_get_boolean (value));
        break;
    case PROP_HUD_VISIBLE:
        lrg_photo_mode_set_hud_visible (self, g_value_get_boolean (value));
        break;
    case PROP_CAPTURE_SCALE:
        lrg_photo_mode_set_capture_scale (self, g_value_get_uint (value));
        break;
    case PROP_MAX_CAPTURE_SIDE:
        lrg_photo_mode_set_max_capture_side (self, g_value_get_uint (value));
        break;
    case PROP_TIMER_SECONDS:
        lrg_photo_mode_set_timer_seconds (self, g_value_get_float (value));
        break;
    case PROP_FILENAME_PREFIX:
        lrg_photo_mode_set_filename_prefix (self, g_value_get_string (value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_photo_mode_class_init (LrgPhotoModeClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = lrg_photo_mode_finalize;
    object_class->get_property = lrg_photo_mode_get_property;
    object_class->set_property = lrg_photo_mode_set_property;

    /**
     * LrgPhotoMode:active:
     *
     * Whether photo mode is currently active.
     *
     * Since: 1.0
     */
    properties[PROP_ACTIVE] =
        g_param_spec_boolean ("active",
                              "Active",
                              "Whether photo mode is active",
                              FALSE,
                              G_PARAM_READABLE |
                              G_PARAM_STATIC_STRINGS);

    /**
     * LrgPhotoMode:ui-visible:
     *
     * Whether UI elements should be visible.
     *
     * Since: 1.0
     */
    properties[PROP_UI_VISIBLE] =
        g_param_spec_boolean ("ui-visible",
                              "UI Visible",
                              "Whether UI should be shown",
                              TRUE,
                              G_PARAM_READWRITE |
                              G_PARAM_STATIC_STRINGS);

    /**
     * LrgPhotoMode:screenshot-directory:
     *
     * Directory where screenshots are saved.
     *
     * Since: 1.0
     */
    properties[PROP_SCREENSHOT_DIRECTORY] =
        g_param_spec_string ("screenshot-directory",
                             "Screenshot Directory",
                             "Directory for saving screenshots",
                             NULL,
                             G_PARAM_READWRITE |
                             G_PARAM_STATIC_STRINGS);

    /**
     * LrgPhotoMode:default-format:
     *
     * Default format for screenshots.
     *
     * Since: 1.0
     */
    properties[PROP_DEFAULT_FORMAT] =
        g_param_spec_int ("default-format",
                          "Default Format",
                          "Default screenshot format",
                          LRG_SCREENSHOT_FORMAT_PNG,
                          LRG_SCREENSHOT_FORMAT_JPG,
                          LRG_SCREENSHOT_FORMAT_PNG,
                          G_PARAM_READWRITE |
                          G_PARAM_STATIC_STRINGS);

    /**
     * LrgPhotoMode:host:
     *
     * The game's #LrgPhotoHost (freeze, HUD, time of day, weather).
     *
     * Since: 1.0
     */
    properties[PROP_HOST] =
        g_param_spec_object ("host", "Host", "The game's photo host", LRG_TYPE_PHOTO_HOST,
                             G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgPhotoMode:look:
     *
     * The colour look (grade, table, vignette, grain).
     *
     * Since: 1.0
     */
    properties[PROP_LOOK] =
        g_param_spec_object ("look", "Look", "The colour look", LRG_TYPE_PHOTO_LOOK,
                             G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgPhotoMode:dof:
     *
     * The depth of field.
     *
     * Since: 1.0
     */
    properties[PROP_DOF] =
        g_param_spec_object ("dof", "Depth of field", "The depth of field", LRG_TYPE_PHOTO_DOF,
                             G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgPhotoMode:frame:
     *
     * The frame baked into captured photos.
     *
     * Since: 1.0
     */
    properties[PROP_FRAME] =
        g_param_spec_enum ("frame", "Frame", "The frame of a photo", LRG_TYPE_PHOTO_FRAME,
                           LRG_PHOTO_FRAME_NONE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgPhotoMode:grid:
     *
     * The composition grid of the preview.
     *
     * Since: 1.0
     */
    properties[PROP_GRID] =
        g_param_spec_enum ("grid", "Grid", "The composition grid", LRG_TYPE_PHOTO_GRID,
                           LRG_PHOTO_GRID_NONE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgPhotoMode:frame-color:
     *
     * The frame's colour as 0xRRGGBB.
     *
     * Since: 1.0
     */
    properties[PROP_FRAME_COLOR] =
        g_param_spec_uint ("frame-color", "Frame colour", "The frame's colour as 0xRRGGBB",
                           0, 0xffffffu, 0xffffffu, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgPhotoMode:pause-world:
     *
     * Whether entering asks the host to freeze the world (default %TRUE).
     * A host may refuse (an online game), see #LrgPhotoMode:world-frozen.
     *
     * Since: 1.0
     */
    properties[PROP_PAUSE_WORLD] =
        g_param_spec_boolean ("pause-world", "Pause world", "Ask the host to freeze the world", TRUE,
                              G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgPhotoMode:world-frozen:
     *
     * Whether the host froze the world on entering.
     *
     * Since: 1.0
     */
    properties[PROP_WORLD_FROZEN] =
        g_param_spec_boolean ("world-frozen", "World frozen", "Whether the world is frozen", FALSE,
                              G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgPhotoMode:hud-visible:
     *
     * Whether the game's HUD is drawn in photo mode (default %FALSE).
     *
     * Since: 1.0
     */
    properties[PROP_HUD_VISIBLE] =
        g_param_spec_boolean ("hud-visible", "HUD visible", "Whether the game's HUD is shown", FALSE,
                              G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgPhotoMode:capture-scale:
     *
     * The supersampling of a capture: 1 is the window's size, 2 and 4
     * render at two and four times the width and height.
     *
     * Since: 1.0
     */
    properties[PROP_CAPTURE_SCALE] =
        g_param_spec_uint ("capture-scale", "Capture scale", "Supersampling of a capture",
                           1, 8, 1, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgPhotoMode:max-capture-side:
     *
     * The longest side a capture may have, in pixels (the GPU's texture limit).
     *
     * Since: 1.0
     */
    properties[PROP_MAX_CAPTURE_SIDE] =
        g_param_spec_uint ("max-capture-side", "Max capture side", "Longest side of a capture",
                           256, 32768, 8192, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgPhotoMode:timer-seconds:
     *
     * The delay of the self timer; 0 means a capture is immediate.
     *
     * Since: 1.0
     */
    properties[PROP_TIMER_SECONDS] =
        g_param_spec_float ("timer-seconds", "Timer", "Self timer delay", 0.0f, 60.0f, 0.0f,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgPhotoMode:filename-prefix:
     *
     * What file names start with ("screenshot", or "photo").
     *
     * Since: 1.0
     */
    properties[PROP_FILENAME_PREFIX] =
        g_param_spec_string ("filename-prefix", "Filename prefix", "What file names start with",
                             "screenshot", G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);

    /**
     * LrgPhotoMode::capture-requested:
     * @self: the #LrgPhotoMode
     * @scale: the supersampling to render at (already limited by the
     *   maximum capture side for the window)
     *
     * Emitted when a photo is due (the button, or the timer ran out).
     * The host renders one frame at @scale times the window's size
     * through its look and hands the result to
     * lrg_photo_mode_finish_capture().
     *
     * Since: 1.0
     */
    signals[SIGNAL_CAPTURE_REQUESTED] =
        g_signal_new ("capture-requested", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
                      0, NULL, NULL, NULL, G_TYPE_NONE, 1, G_TYPE_UINT);

    /**
     * LrgPhotoMode::timer-tick:
     * @self: the #LrgPhotoMode
     * @seconds: whole seconds left
     *
     * Emitted when the self timer's countdown changes second.
     *
     * Since: 1.0
     */
    signals[SIGNAL_TIMER_TICK] =
        g_signal_new ("timer-tick", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
                      0, NULL, NULL, NULL, G_TYPE_NONE, 1, G_TYPE_UINT);

    /**
     * LrgPhotoMode::photo-saved:
     * @self: the #LrgPhotoMode
     * @path: where the photo was written
     *
     * Emitted after lrg_photo_mode_finish_capture() wrote a file.
     *
     * Since: 1.0
     */
    signals[SIGNAL_PHOTO_SAVED] =
        g_signal_new ("photo-saved", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
                      0, NULL, NULL, NULL, G_TYPE_NONE, 1, G_TYPE_STRING);

    /**
     * LrgPhotoMode::entered:
     * @self: the #LrgPhotoMode
     *
     * Emitted when photo mode is entered.
     *
     * Since: 1.0
     */
    signals[SIGNAL_ENTERED] =
        g_signal_new ("entered",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0, NULL, NULL, NULL,
                      G_TYPE_NONE, 0);

    /**
     * LrgPhotoMode::exited:
     * @self: the #LrgPhotoMode
     *
     * Emitted when photo mode is exited.
     *
     * Since: 1.0
     */
    signals[SIGNAL_EXITED] =
        g_signal_new ("exited",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0, NULL, NULL, NULL,
                      G_TYPE_NONE, 0);

    /**
     * LrgPhotoMode::screenshot-taken:
     * @self: the #LrgPhotoMode
     * @screenshot: the captured #LrgScreenshot
     *
     * Emitted when a screenshot is captured.
     *
     * Since: 1.0
     */
    signals[SIGNAL_SCREENSHOT_TAKEN] =
        g_signal_new ("screenshot-taken",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0, NULL, NULL, NULL,
                      G_TYPE_NONE, 1,
                      LRG_TYPE_SCREENSHOT);
}

static void
lrg_photo_mode_init (LrgPhotoMode *self)
{
    self->active = FALSE;
    self->ui_visible = TRUE;
    self->camera_controller = NULL;
    self->screenshot_directory = g_strdup (g_get_user_special_dir (G_USER_DIRECTORY_PICTURES));
    self->default_format = LRG_SCREENSHOT_FORMAT_PNG;
    self->screenshot_counter = 0;
    self->filename_prefix = g_strdup ("screenshot");
    self->look = lrg_photo_look_new ();
    self->dof = lrg_photo_dof_new ();
    self->frame = LRG_PHOTO_FRAME_NONE;
    self->grid = LRG_PHOTO_GRID_NONE;
    self->frame_color = 0xffffffu;
    self->pause_world = TRUE;
    self->hud_visible = FALSE;
    self->capture_scale = 1;
    self->max_capture_side = 8192;
    self->extra_meta = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);

    /* Fall back to home directory if Pictures not available */
    if (self->screenshot_directory == NULL)
        self->screenshot_directory = g_strdup (g_get_home_dir ());
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

LrgPhotoMode *
lrg_photo_mode_get_default (void)
{
    if (default_instance == NULL)
        default_instance = g_object_new (LRG_TYPE_PHOTO_MODE, NULL);

    return default_instance;
}

gboolean
lrg_photo_mode_is_active (LrgPhotoMode *self)
{
    g_return_val_if_fail (LRG_IS_PHOTO_MODE (self), FALSE);

    return self->active;
}

gboolean
lrg_photo_mode_enter (LrgPhotoMode  *self,
                      LrgCamera3D   *game_camera,
                      GError       **error)
{
    g_return_val_if_fail (LRG_IS_PHOTO_MODE (self), FALSE);

    if (self->active)
    {
        g_set_error (error,
                     LRG_PHOTO_MODE_ERROR,
                     LRG_PHOTO_MODE_ERROR_ALREADY_ACTIVE,
                     "Photo mode is already active");
        return FALSE;
    }

    /* Create camera controller */
    g_clear_object (&self->camera_controller);
    if (game_camera != NULL)
        self->camera_controller = lrg_photo_camera_controller_new_from_camera (game_camera);
    else
        self->camera_controller = lrg_photo_camera_controller_new ();

    self->active = TRUE;
    self->ui_visible = TRUE;
    self->capture_pending = FALSE;
    self->timer_running = FALSE;
    self->hud_visible = FALSE;
    self->grain_seed = (guint)g_get_monotonic_time ();

    /* the world contract: the camera stays near the game's anchor (the
     * player), the world is frozen when asked and possible, the HUD goes */
    if (self->host != NULL)
    {
        gfloat anchor[3];

        if (lrg_photo_host_get_anchor (self->host, anchor))
            lrg_photo_camera_controller_set_anchor (self->camera_controller,
                                                    anchor[0], anchor[1], anchor[2]);
        self->world_frozen = self->pause_world ?
            lrg_photo_host_freeze_world (self->host, TRUE) : FALSE;
        lrg_photo_host_set_hud_visible (self->host, FALSE);
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_WORLD_FROZEN]);
    }
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_HUD_VISIBLE]);

    lrg_info (LRG_LOG_DOMAIN_PHOTOMODE, "Photo mode entered");

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ACTIVE]);
    g_signal_emit (self, signals[SIGNAL_ENTERED], 0);

    return TRUE;
}

void
lrg_photo_mode_exit (LrgPhotoMode *self)
{
    g_return_if_fail (LRG_IS_PHOTO_MODE (self));

    if (!self->active)
        return;

    g_clear_object (&self->camera_controller);

    /* everything photo mode changed in the game goes back */
    self->timer_running = FALSE;
    self->capture_pending = FALSE;
    if (self->host != NULL)
    {
        if (self->time_override_set)
            lrg_photo_host_clear_time_of_day (self->host);
        if (self->weather_override != NULL)
            lrg_photo_host_clear_weather (self->host);
        lrg_photo_host_set_hud_visible (self->host, TRUE);
        if (self->world_frozen)
            lrg_photo_host_freeze_world (self->host, FALSE);
    }
    self->time_override_set = FALSE;
    g_clear_pointer (&self->weather_override, g_free);
    if (self->world_frozen)
    {
        self->world_frozen = FALSE;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_WORLD_FROZEN]);
    }

    self->active = FALSE;

    lrg_info (LRG_LOG_DOMAIN_PHOTOMODE, "Photo mode exited");

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ACTIVE]);
    g_signal_emit (self, signals[SIGNAL_EXITED], 0);
}

gboolean
lrg_photo_mode_toggle (LrgPhotoMode  *self,
                       LrgCamera3D   *game_camera,
                       GError       **error)
{
    g_return_val_if_fail (LRG_IS_PHOTO_MODE (self), FALSE);

    if (self->active)
    {
        lrg_photo_mode_exit (self);
        return TRUE;
    }
    else
    {
        return lrg_photo_mode_enter (self, game_camera, error);
    }
}

LrgPhotoCameraController *
lrg_photo_mode_get_camera_controller (LrgPhotoMode *self)
{
    g_return_val_if_fail (LRG_IS_PHOTO_MODE (self), NULL);

    return self->camera_controller;
}

LrgCamera3D *
lrg_photo_mode_get_camera (LrgPhotoMode *self)
{
    g_return_val_if_fail (LRG_IS_PHOTO_MODE (self), NULL);

    if (self->camera_controller == NULL)
        return NULL;

    return lrg_photo_camera_controller_get_camera (self->camera_controller);
}

gboolean
lrg_photo_mode_get_ui_visible (LrgPhotoMode *self)
{
    g_return_val_if_fail (LRG_IS_PHOTO_MODE (self), TRUE);

    return self->ui_visible;
}

void
lrg_photo_mode_set_ui_visible (LrgPhotoMode *self,
                               gboolean      visible)
{
    g_return_if_fail (LRG_IS_PHOTO_MODE (self));

    if (self->ui_visible != visible)
    {
        self->ui_visible = visible;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_UI_VISIBLE]);
    }
}

void
lrg_photo_mode_toggle_ui (LrgPhotoMode *self)
{
    g_return_if_fail (LRG_IS_PHOTO_MODE (self));

    lrg_photo_mode_set_ui_visible (self, !self->ui_visible);
}

LrgScreenshot *
lrg_photo_mode_capture (LrgPhotoMode  *self,
                        GError       **error)
{
    LrgScreenshot *screenshot;

    g_return_val_if_fail (LRG_IS_PHOTO_MODE (self), NULL);

    screenshot = lrg_screenshot_capture (error);
    if (screenshot != NULL)
    {
        g_signal_emit (self, signals[SIGNAL_SCREENSHOT_TAKEN], 0, screenshot);
        lrg_debug (LRG_LOG_DOMAIN_PHOTOMODE, "Screenshot captured");
    }

    return screenshot;
}

gboolean
lrg_photo_mode_capture_and_save (LrgPhotoMode        *self,
                                 const gchar         *path,
                                 LrgScreenshotFormat  format,
                                 GError             **error)
{
    g_autoptr(LrgScreenshot) screenshot = NULL;

    g_return_val_if_fail (LRG_IS_PHOTO_MODE (self), FALSE);
    g_return_val_if_fail (path != NULL, FALSE);

    screenshot = lrg_photo_mode_capture (self, error);
    if (screenshot == NULL)
        return FALSE;

    return lrg_screenshot_save (screenshot, path, format, error);
}

const gchar *
lrg_photo_mode_get_screenshot_directory (LrgPhotoMode *self)
{
    g_return_val_if_fail (LRG_IS_PHOTO_MODE (self), NULL);

    return self->screenshot_directory;
}

void
lrg_photo_mode_set_screenshot_directory (LrgPhotoMode *self,
                                         const gchar  *directory)
{
    g_return_if_fail (LRG_IS_PHOTO_MODE (self));

    if (g_strcmp0 (self->screenshot_directory, directory) != 0)
    {
        g_free (self->screenshot_directory);
        self->screenshot_directory = g_strdup (directory);
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SCREENSHOT_DIRECTORY]);
    }
}

LrgScreenshotFormat
lrg_photo_mode_get_default_format (LrgPhotoMode *self)
{
    g_return_val_if_fail (LRG_IS_PHOTO_MODE (self), LRG_SCREENSHOT_FORMAT_PNG);

    return self->default_format;
}

void
lrg_photo_mode_set_default_format (LrgPhotoMode        *self,
                                   LrgScreenshotFormat  format)
{
    g_return_if_fail (LRG_IS_PHOTO_MODE (self));

    if (self->default_format != format)
    {
        self->default_format = format;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_DEFAULT_FORMAT]);
    }
}

gchar *
lrg_photo_mode_generate_filename (LrgPhotoMode        *self,
                                  LrgScreenshotFormat  format)
{
    g_autoptr(GDateTime) now = NULL;
    g_autofree gchar *timestamp = NULL;
    const gchar *extension;

    g_return_val_if_fail (LRG_IS_PHOTO_MODE (self), NULL);

    now = g_date_time_new_now_local ();
    timestamp = g_date_time_format (now, "%Y%m%d_%H%M%S");

    switch (format)
    {
    case LRG_SCREENSHOT_FORMAT_PNG:
        extension = "png";
        break;
    case LRG_SCREENSHOT_FORMAT_JPG:
        extension = "jpg";
        break;
    default:
        extension = "png";
        break;
    }

    self->screenshot_counter++;

    return g_strdup_printf ("%s/%s_%s_%03u.%s",
                            self->screenshot_directory,
                            self->filename_prefix,
                            timestamp,
                            self->screenshot_counter,
                            extension);
}

void
lrg_photo_mode_update (LrgPhotoMode *self,
                       gfloat        delta)
{
    g_return_if_fail (LRG_IS_PHOTO_MODE (self));

    if (!self->active)
        return;

    if (self->camera_controller != NULL)
    {
        lrg_photo_camera_controller_update (self->camera_controller, delta);

        /* the lens follows the camera's field of view */
        lrg_photo_dof_set_fov (self->dof,
                               lrg_photo_camera_controller_get_fov (self->camera_controller));
    }

    /* the self timer counts down on the real clock (the world is frozen,
     * the host passes the frame's delta) */
    if (self->timer_running)
    {
        guint before = (guint)ceilf (self->timer_remaining);
        guint after;

        self->timer_remaining -= delta;
        if (self->timer_remaining <= 0.0f)
        {
            g_autoptr(GError) error = NULL;

            self->timer_remaining = 0.0f;
            self->timer_running = FALSE;
            g_signal_emit (self, signals[SIGNAL_TIMER_TICK], 0, 0u);
            if (!lrg_photo_mode_request_capture (self, &error))
                lrg_warning (LRG_LOG_DOMAIN_PHOTOMODE, "Timer capture: %s", error->message);
            return;
        }
        after = (guint)ceilf (self->timer_remaining);
        if (after != before)
            g_signal_emit (self, signals[SIGNAL_TIMER_TICK], 0, after);
    }
}

/* ==========================================================================
 * Host, look, lens, overlays
 * ========================================================================== */

LrgPhotoHost *
lrg_photo_mode_get_host (LrgPhotoMode *self)
{
    g_return_val_if_fail (LRG_IS_PHOTO_MODE (self), NULL);

    return self->host;
}

void
lrg_photo_mode_set_host (LrgPhotoMode *self,
                         LrgPhotoHost *host)
{
    g_return_if_fail (LRG_IS_PHOTO_MODE (self));
    g_return_if_fail (host == NULL || LRG_IS_PHOTO_HOST (host));

    if (self->host == host)
        return;
    g_return_if_fail (!self->active);
    /*
     * A weak reference: the host is usually the game object that owns
     * this mode (librefall's LfPhoto), and a strong one both ways would
     * keep both alive forever.  A host that goes away reads as none.
     */
    g_set_weak_pointer (&self->host, host);
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_HOST]);
}

LrgPhotoLook *
lrg_photo_mode_get_look (LrgPhotoMode *self)
{
    g_return_val_if_fail (LRG_IS_PHOTO_MODE (self), NULL);

    return self->look;
}

LrgPhotoDof *
lrg_photo_mode_get_dof (LrgPhotoMode *self)
{
    g_return_val_if_fail (LRG_IS_PHOTO_MODE (self), NULL);

    return self->dof;
}

LrgPhotoFrame
lrg_photo_mode_get_frame (LrgPhotoMode *self)
{
    g_return_val_if_fail (LRG_IS_PHOTO_MODE (self), LRG_PHOTO_FRAME_NONE);

    return self->frame;
}

void
lrg_photo_mode_set_frame (LrgPhotoMode *self,
                          LrgPhotoFrame frame)
{
    g_return_if_fail (LRG_IS_PHOTO_MODE (self));

    if (self->frame == frame)
        return;
    self->frame = frame;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_FRAME]);
}

LrgPhotoGrid
lrg_photo_mode_get_grid (LrgPhotoMode *self)
{
    g_return_val_if_fail (LRG_IS_PHOTO_MODE (self), LRG_PHOTO_GRID_NONE);

    return self->grid;
}

void
lrg_photo_mode_set_grid (LrgPhotoMode *self,
                         LrgPhotoGrid  grid)
{
    g_return_if_fail (LRG_IS_PHOTO_MODE (self));

    if (self->grid == grid)
        return;
    self->grid = grid;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_GRID]);
}

guint
lrg_photo_mode_get_frame_color (LrgPhotoMode *self)
{
    g_return_val_if_fail (LRG_IS_PHOTO_MODE (self), 0xffffffu);

    return self->frame_color;
}

void
lrg_photo_mode_set_frame_color (LrgPhotoMode *self,
                                guint         rgb)
{
    g_return_if_fail (LRG_IS_PHOTO_MODE (self));

    rgb &= 0xffffffu;
    if (self->frame_color == rgb)
        return;
    self->frame_color = rgb;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_FRAME_COLOR]);
}

gboolean
lrg_photo_mode_get_pause_world (LrgPhotoMode *self)
{
    g_return_val_if_fail (LRG_IS_PHOTO_MODE (self), TRUE);

    return self->pause_world;
}

void
lrg_photo_mode_set_pause_world (LrgPhotoMode *self,
                                gboolean      pause)
{
    g_return_if_fail (LRG_IS_PHOTO_MODE (self));

    pause = !!pause;
    if (self->pause_world == pause)
        return;
    self->pause_world = pause;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_PAUSE_WORLD]);
}

gboolean
lrg_photo_mode_is_world_frozen (LrgPhotoMode *self)
{
    g_return_val_if_fail (LRG_IS_PHOTO_MODE (self), FALSE);

    return self->world_frozen;
}

gboolean
lrg_photo_mode_get_hud_visible (LrgPhotoMode *self)
{
    g_return_val_if_fail (LRG_IS_PHOTO_MODE (self), FALSE);

    return self->hud_visible;
}

void
lrg_photo_mode_set_hud_visible (LrgPhotoMode *self,
                                gboolean      visible)
{
    g_return_if_fail (LRG_IS_PHOTO_MODE (self));

    visible = !!visible;
    if (self->hud_visible == visible)
        return;
    self->hud_visible = visible;
    if (self->active && self->host != NULL)
        lrg_photo_host_set_hud_visible (self->host, visible);
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_HUD_VISIBLE]);
}

/* ==========================================================================
 * Time of day and weather
 * ========================================================================== */

gboolean
lrg_photo_mode_set_time_of_day (LrgPhotoMode *self,
                                gfloat        hour)
{
    gfloat current;

    g_return_val_if_fail (LRG_IS_PHOTO_MODE (self), FALSE);

    if (!self->active || self->host == NULL ||
        !lrg_photo_host_get_time_of_day (self->host, &current))
        return FALSE;
    if (!isfinite (hour))
        return FALSE;

    /* the hour wraps round the day */
    hour = fmodf (hour, 24.0f);
    if (hour < 0.0f)
        hour += 24.0f;
    lrg_photo_host_set_time_of_day (self->host, hour);
    self->time_override_set = TRUE;
    self->time_override = hour;
    return TRUE;
}

gboolean
lrg_photo_mode_get_time_of_day (LrgPhotoMode *self,
                                gfloat       *hour)
{
    g_return_val_if_fail (LRG_IS_PHOTO_MODE (self), FALSE);
    g_return_val_if_fail (hour != NULL, FALSE);

    if (!self->active || self->host == NULL)
        return FALSE;
    return lrg_photo_host_get_time_of_day (self->host, hour);
}

gboolean
lrg_photo_mode_has_time_override (LrgPhotoMode *self)
{
    g_return_val_if_fail (LRG_IS_PHOTO_MODE (self), FALSE);

    return self->time_override_set;
}

void
lrg_photo_mode_clear_time_of_day (LrgPhotoMode *self)
{
    g_return_if_fail (LRG_IS_PHOTO_MODE (self));

    if (!self->time_override_set)
        return;
    self->time_override_set = FALSE;
    if (self->host != NULL)
        lrg_photo_host_clear_time_of_day (self->host);
}

const gchar * const *
lrg_photo_mode_list_weathers (LrgPhotoMode *self)
{
    g_return_val_if_fail (LRG_IS_PHOTO_MODE (self), NULL);

    return self->host != NULL ? lrg_photo_host_list_weathers (self->host) : NULL;
}

gboolean
lrg_photo_mode_set_weather (LrgPhotoMode *self,
                            const gchar  *weather)
{
    const gchar * const *known;
    guint i;

    g_return_val_if_fail (LRG_IS_PHOTO_MODE (self), FALSE);
    g_return_val_if_fail (weather != NULL, FALSE);

    if (!self->active || self->host == NULL)
        return FALSE;
    known = lrg_photo_host_list_weathers (self->host);
    if (known == NULL)
        return FALSE;

    /* only what the host lists */
    for (i = 0; known[i] != NULL; i++)
    {
        if (g_strcmp0 (known[i], weather) == 0)
        {
            lrg_photo_host_set_weather (self->host, weather);
            g_free (self->weather_override);
            self->weather_override = g_strdup (weather);
            return TRUE;
        }
    }
    return FALSE;
}

const gchar *
lrg_photo_mode_get_weather (LrgPhotoMode *self)
{
    g_return_val_if_fail (LRG_IS_PHOTO_MODE (self), NULL);

    if (self->host == NULL)
        return NULL;
    return lrg_photo_host_get_weather (self->host);
}

const gchar *
lrg_photo_mode_get_weather_override (LrgPhotoMode *self)
{
    g_return_val_if_fail (LRG_IS_PHOTO_MODE (self), NULL);

    return self->weather_override;
}

void
lrg_photo_mode_clear_weather (LrgPhotoMode *self)
{
    g_return_if_fail (LRG_IS_PHOTO_MODE (self));

    if (self->weather_override == NULL)
        return;
    g_clear_pointer (&self->weather_override, g_free);
    if (self->host != NULL)
        lrg_photo_host_clear_weather (self->host);
}

/* ==========================================================================
 * Capture
 * ========================================================================== */

guint
lrg_photo_mode_get_capture_scale (LrgPhotoMode *self)
{
    g_return_val_if_fail (LRG_IS_PHOTO_MODE (self), 1);

    return self->capture_scale;
}

void
lrg_photo_mode_set_capture_scale (LrgPhotoMode *self,
                                  guint         scale)
{
    g_return_if_fail (LRG_IS_PHOTO_MODE (self));

    scale = CLAMP (scale, 1u, 8u);
    if (self->capture_scale == scale)
        return;
    self->capture_scale = scale;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_CAPTURE_SCALE]);
}

guint
lrg_photo_mode_get_max_capture_side (LrgPhotoMode *self)
{
    g_return_val_if_fail (LRG_IS_PHOTO_MODE (self), 8192);

    return self->max_capture_side;
}

void
lrg_photo_mode_set_max_capture_side (LrgPhotoMode *self,
                                     guint         side)
{
    g_return_if_fail (LRG_IS_PHOTO_MODE (self));

    side = CLAMP (side, 256u, 32768u);
    if (self->max_capture_side == side)
        return;
    self->max_capture_side = side;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MAX_CAPTURE_SIDE]);
}

const gchar *
lrg_photo_mode_get_filename_prefix (LrgPhotoMode *self)
{
    g_return_val_if_fail (LRG_IS_PHOTO_MODE (self), NULL);

    return self->filename_prefix;
}

void
lrg_photo_mode_set_filename_prefix (LrgPhotoMode *self,
                                    const gchar  *prefix)
{
    g_return_if_fail (LRG_IS_PHOTO_MODE (self));

    if (prefix == NULL || prefix[0] == '\0' || strpbrk (prefix, "/\\") != NULL)
        prefix = "screenshot";
    if (g_strcmp0 (self->filename_prefix, prefix) == 0)
        return;
    g_free (self->filename_prefix);
    self->filename_prefix = g_strdup (prefix);
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_FILENAME_PREFIX]);
}

guint
lrg_photo_mode_get_capture_size (LrgPhotoMode *self,
                                 gint          width,
                                 gint          height,
                                 gint         *out_width,
                                 gint         *out_height)
{
    guint scale;

    g_return_val_if_fail (LRG_IS_PHOTO_MODE (self), 1);
    g_return_val_if_fail (width > 0 && height > 0, 1);

    /* the biggest scale up to the asked one that fits the side limit */
    scale = self->capture_scale;
    while (scale > 1 && (guint)MAX (width, height) * scale > self->max_capture_side)
        scale--;
    if (out_width != NULL)
        *out_width = width * (gint)scale;
    if (out_height != NULL)
        *out_height = height * (gint)scale;
    return scale;
}

void
lrg_photo_mode_set_extra_metadata (LrgPhotoMode *self,
                                   const gchar  *key,
                                   const gchar  *value)
{
    g_return_if_fail (LRG_IS_PHOTO_MODE (self));
    g_return_if_fail (key != NULL && key[0] != '\0' && strlen (key) <= 79);

    if (value == NULL)
        g_hash_table_remove (self->extra_meta, key);
    else
        g_hash_table_replace (self->extra_meta, g_strdup (key), g_strdup (value));
}

gboolean
lrg_photo_mode_request_capture (LrgPhotoMode  *self,
                                GError       **error)
{
    g_return_val_if_fail (LRG_IS_PHOTO_MODE (self), FALSE);

    if (!self->active)
    {
        g_set_error_literal (error, LRG_PHOTO_MODE_ERROR, LRG_PHOTO_MODE_ERROR_NOT_ACTIVE,
                             "Photo mode is not active");
        return FALSE;
    }
    if (self->capture_pending)
    {
        g_set_error_literal (error, LRG_PHOTO_MODE_ERROR, LRG_PHOTO_MODE_ERROR_BUSY,
                             "A capture is already in progress");
        return FALSE;
    }
    self->capture_pending = TRUE;
    g_signal_emit (self, signals[SIGNAL_CAPTURE_REQUESTED], 0, self->capture_scale);
    return TRUE;
}

gboolean
lrg_photo_mode_is_capture_pending (LrgPhotoMode *self)
{
    g_return_val_if_fail (LRG_IS_PHOTO_MODE (self), FALSE);

    return self->capture_pending;
}

void
lrg_photo_mode_abort_capture (LrgPhotoMode *self)
{
    g_return_if_fail (LRG_IS_PHOTO_MODE (self));

    self->capture_pending = FALSE;
}

/* the text entries of the photo: the camera, the lens, the look and the
 * game's own (host and extra) */
static void
describe_photo (LrgPhotoMode *self,
                GrlImage     *image,
                LrgScreenshot *shot)
{
    g_autoptr(GHashTable) game = NULL;
    g_autoptr(GDateTime) now = NULL;
    g_autofree gchar *when = NULL;
    GHashTableIter iter;
    gpointer key;
    gpointer value;

    now = g_date_time_new_now_utc ();
    when = g_date_time_format_iso8601 (now);
    lrg_screenshot_set_metadata (shot, "Software", "libregnum photo mode");
    lrg_screenshot_set_metadata (shot, "Creation Time", when);
    {
        g_autofree gchar *size = g_strdup_printf ("%dx%d", grl_image_get_width (image),
                                                  grl_image_get_height (image));
        g_autofree gchar *scale = g_strdup_printf ("%u", self->capture_scale);

        lrg_screenshot_set_metadata (shot, "Photo.Size", size);
        lrg_screenshot_set_metadata (shot, "Photo.CaptureScale", scale);
    }
    if (self->camera_controller != NULL)
    {
        g_autoptr(GrlVector3) pos = lrg_photo_camera_controller_get_position (self->camera_controller);
        g_autofree gchar *position = g_strdup_printf ("%.3f %.3f %.3f", pos->x, pos->y, pos->z);
        g_autofree gchar *angles = g_strdup_printf ("%.2f %.2f %.2f",
            lrg_photo_camera_controller_get_yaw (self->camera_controller),
            lrg_photo_camera_controller_get_pitch (self->camera_controller),
            lrg_photo_camera_controller_get_roll (self->camera_controller));
        g_autofree gchar *fov = g_strdup_printf ("%.2f",
            lrg_photo_camera_controller_get_fov (self->camera_controller));
        GEnumClass *mode_class = g_type_class_ref (LRG_TYPE_PHOTO_CAMERA_MODE);
        GEnumValue *mode = g_enum_get_value (mode_class,
            lrg_photo_camera_controller_get_mode (self->camera_controller));

        lrg_screenshot_set_metadata (shot, "Camera.Position", position);
        lrg_screenshot_set_metadata (shot, "Camera.YawPitchRoll", angles);
        lrg_screenshot_set_metadata (shot, "Camera.Fov", fov);
        lrg_screenshot_set_metadata (shot, "Camera.Mode", mode != NULL ? mode->value_nick : "free");
        g_type_class_unref (mode_class);
    }
    {
        GEnumClass *preset_class = g_type_class_ref (LRG_TYPE_PHOTO_PRESET);
        GEnumValue *preset = g_enum_get_value (preset_class, lrg_photo_look_get_preset (self->look));
        g_autofree gchar *exposure = g_strdup_printf ("%.2f", lrg_photo_look_get_exposure (self->look));

        lrg_screenshot_set_metadata (shot, "Look.Preset", preset != NULL ? preset->value_nick : "none");
        lrg_screenshot_set_metadata (shot, "Look.Exposure", exposure);
        g_type_class_unref (preset_class);
    }
    if (lrg_photo_dof_get_enabled (self->dof))
    {
        g_autofree gchar *focus = g_strdup_printf ("%.2f", lrg_photo_dof_get_focus_distance (self->dof));
        g_autofree gchar *aperture = g_strdup_printf ("f/%.1f", lrg_photo_dof_get_aperture (self->dof));

        lrg_screenshot_set_metadata (shot, "Photo.FocusDistance", focus);
        lrg_screenshot_set_metadata (shot, "Photo.Aperture", aperture);
    }
    if (self->time_override_set)
    {
        g_autofree gchar *hour = g_strdup_printf ("%.2f", self->time_override);

        lrg_screenshot_set_metadata (shot, "World.TimeOfDay", hour);
    }
    if (self->weather_override != NULL)
        lrg_screenshot_set_metadata (shot, "World.Weather", self->weather_override);

    /* what the game says, then what the caller set (which wins) */
    game = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
    if (self->host != NULL)
        lrg_photo_host_describe (self->host, game);
    g_hash_table_iter_init (&iter, game);
    while (g_hash_table_iter_next (&iter, &key, &value))
        if (key != NULL && ((const gchar *)key)[0] != '\0' && strlen (key) <= 79)
            lrg_screenshot_set_metadata (shot, key, value);
    g_hash_table_iter_init (&iter, self->extra_meta);
    while (g_hash_table_iter_next (&iter, &key, &value))
        lrg_screenshot_set_metadata (shot, key, value);
}

gchar *
lrg_photo_mode_finish_capture (LrgPhotoMode  *self,
                               GrlImage      *image,
                               gboolean       apply_look,
                               GError       **error)
{
    g_autoptr(LrgScreenshot) shot = NULL;
    g_autofree gchar *path = NULL;
    g_autoptr(GrlImage) copy = NULL;
    GError *local = NULL;

    g_return_val_if_fail (LRG_IS_PHOTO_MODE (self), NULL);
    g_return_val_if_fail (GRL_IS_IMAGE (image), NULL);

    if (!self->capture_pending)
    {
        g_set_error_literal (error, LRG_PHOTO_MODE_ERROR, LRG_PHOTO_MODE_ERROR_NOT_ACTIVE,
                             "No capture was requested");
        return NULL;
    }
    /* the request is over whatever happens next */
    self->capture_pending = FALSE;

    if (grl_image_get_width (image) <= 0 || grl_image_get_height (image) <= 0)
    {
        g_set_error_literal (error, LRG_PHOTO_MODE_ERROR, LRG_PHOTO_MODE_ERROR_CAPTURE,
                             "The captured image is empty");
        return NULL;
    }

    /* the caller's image is left alone: the look and the frame go on a copy */
    copy = grl_image_copy (image);
    if (copy == NULL)
    {
        g_set_error_literal (error, LRG_PHOTO_MODE_ERROR, LRG_PHOTO_MODE_ERROR_CAPTURE,
                             "Could not copy the captured image");
        return NULL;
    }
    if (apply_look)
        lrg_photo_look_apply_image (self->look, copy, self->grain_seed);
    lrg_photo_overlay_apply_frame (self->frame, copy,
                                   (guint8)((self->frame_color >> 16) & 0xff),
                                   (guint8)((self->frame_color >> 8) & 0xff),
                                   (guint8)(self->frame_color & 0xff));

    /* the file: the directory is made, the name is new */
    if (g_mkdir_with_parents (self->screenshot_directory, 0755) != 0)
    {
        g_set_error (error, LRG_PHOTO_MODE_ERROR, LRG_PHOTO_MODE_ERROR_SAVE,
                     "Cannot create %s: %s", self->screenshot_directory, g_strerror (errno));
        return NULL;
    }
    do
    {
        g_free (path);
        path = lrg_photo_mode_generate_filename (self, LRG_SCREENSHOT_FORMAT_PNG);
    }
    while (g_file_test (path, G_FILE_TEST_EXISTS));

    shot = lrg_screenshot_new_from_image (copy);
    describe_photo (self, copy, shot);
    if (!lrg_screenshot_save_png (shot, path, &local))
    {
        g_propagate_error (error, local);
        return NULL;
    }
    g_free (self->last_path);
    self->last_path = g_strdup (path);
    g_signal_emit (self, signals[SIGNAL_SCREENSHOT_TAKEN], 0, shot);
    g_signal_emit (self, signals[SIGNAL_PHOTO_SAVED], 0, path);
    return (gchar *)g_steal_pointer (&path);
}

const gchar *
lrg_photo_mode_get_last_path (LrgPhotoMode *self)
{
    g_return_val_if_fail (LRG_IS_PHOTO_MODE (self), NULL);

    return self->last_path;
}

/* ==========================================================================
 * The timer
 * ========================================================================== */

gfloat
lrg_photo_mode_get_timer_seconds (LrgPhotoMode *self)
{
    g_return_val_if_fail (LRG_IS_PHOTO_MODE (self), 0.0f);

    return self->timer_seconds;
}

void
lrg_photo_mode_set_timer_seconds (LrgPhotoMode *self,
                                  gfloat        seconds)
{
    g_return_if_fail (LRG_IS_PHOTO_MODE (self));

    if (!isfinite (seconds) || seconds < 0.0f)
        seconds = 0.0f;
    if (seconds > 60.0f)
        seconds = 60.0f;
    if (self->timer_seconds == seconds)
        return;
    self->timer_seconds = seconds;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TIMER_SECONDS]);
}

gboolean
lrg_photo_mode_shoot (LrgPhotoMode  *self,
                      GError       **error)
{
    g_return_val_if_fail (LRG_IS_PHOTO_MODE (self), FALSE);

    if (!self->active)
    {
        g_set_error_literal (error, LRG_PHOTO_MODE_ERROR, LRG_PHOTO_MODE_ERROR_NOT_ACTIVE,
                             "Photo mode is not active");
        return FALSE;
    }
    if (self->capture_pending || self->timer_running)
    {
        g_set_error_literal (error, LRG_PHOTO_MODE_ERROR, LRG_PHOTO_MODE_ERROR_BUSY,
                             "A photo is already on its way");
        return FALSE;
    }
    if (self->timer_seconds <= 0.0f)
        return lrg_photo_mode_request_capture (self, error);

    /* the countdown: the first tick is the full wait */
    self->timer_remaining = self->timer_seconds;
    self->timer_running = TRUE;
    g_signal_emit (self, signals[SIGNAL_TIMER_TICK], 0, (guint)ceilf (self->timer_remaining));
    return TRUE;
}

gboolean
lrg_photo_mode_is_timer_running (LrgPhotoMode *self)
{
    g_return_val_if_fail (LRG_IS_PHOTO_MODE (self), FALSE);

    return self->timer_running;
}

gfloat
lrg_photo_mode_get_timer_remaining (LrgPhotoMode *self)
{
    g_return_val_if_fail (LRG_IS_PHOTO_MODE (self), 0.0f);

    return self->timer_running ? self->timer_remaining : 0.0f;
}

void
lrg_photo_mode_cancel_timer (LrgPhotoMode *self)
{
    g_return_if_fail (LRG_IS_PHOTO_MODE (self));

    self->timer_running = FALSE;
    self->timer_remaining = 0.0f;
}
