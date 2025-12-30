/* lrg-photo-mode.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgPhotoMode - Singleton controller for photo mode.
 */

#include "config.h"

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
};

enum
{
    PROP_0,
    PROP_ACTIVE,
    PROP_UI_VISIBLE,
    PROP_SCREENSHOT_DIRECTORY,
    PROP_DEFAULT_FORMAT,
    N_PROPS
};

enum
{
    SIGNAL_ENTERED,
    SIGNAL_EXITED,
    SIGNAL_SCREENSHOT_TAKEN,
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

    g_object_class_install_properties (object_class, N_PROPS, properties);

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

    return g_strdup_printf ("%s/screenshot_%s_%03u.%s",
                            self->screenshot_directory,
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
        lrg_photo_camera_controller_update (self->camera_controller, delta);
}
