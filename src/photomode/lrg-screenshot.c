/* lrg-screenshot.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgScreenshot - Screenshot capture and save.
 */

#include "config.h"

#include "lrg-screenshot.h"
#include "../lrg-log.h"

struct _LrgScreenshot
{
    GObject parent_instance;

    GrlImage *image;
    gint width;
    gint height;
};

enum
{
    PROP_0,
    PROP_WIDTH,
    PROP_HEIGHT,
    PROP_IMAGE,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

G_DEFINE_TYPE (LrgScreenshot, lrg_screenshot, G_TYPE_OBJECT)

/* ==========================================================================
 * GObject Implementation
 * ========================================================================== */

static void
lrg_screenshot_finalize (GObject *object)
{
    LrgScreenshot *self = LRG_SCREENSHOT (object);

    g_clear_object (&self->image);

    G_OBJECT_CLASS (lrg_screenshot_parent_class)->finalize (object);
}

static void
lrg_screenshot_get_property (GObject    *object,
                             guint       prop_id,
                             GValue     *value,
                             GParamSpec *pspec)
{
    LrgScreenshot *self = LRG_SCREENSHOT (object);

    switch (prop_id)
    {
    case PROP_WIDTH:
        g_value_set_int (value, self->width);
        break;
    case PROP_HEIGHT:
        g_value_set_int (value, self->height);
        break;
    case PROP_IMAGE:
        g_value_set_object (value, self->image);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_screenshot_set_property (GObject      *object,
                             guint         prop_id,
                             const GValue *value,
                             GParamSpec   *pspec)
{
    LrgScreenshot *self = LRG_SCREENSHOT (object);

    switch (prop_id)
    {
    case PROP_IMAGE:
        g_clear_object (&self->image);
        self->image = g_value_dup_object (value);
        if (self->image != NULL)
        {
            self->width = grl_image_get_width (self->image);
            self->height = grl_image_get_height (self->image);
        }
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_screenshot_class_init (LrgScreenshotClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = lrg_screenshot_finalize;
    object_class->get_property = lrg_screenshot_get_property;
    object_class->set_property = lrg_screenshot_set_property;

    /**
     * LrgScreenshot:width:
     *
     * The screenshot width in pixels.
     *
     * Since: 1.0
     */
    properties[PROP_WIDTH] =
        g_param_spec_int ("width",
                          "Width",
                          "Screenshot width in pixels",
                          0, G_MAXINT, 0,
                          G_PARAM_READABLE |
                          G_PARAM_STATIC_STRINGS);

    /**
     * LrgScreenshot:height:
     *
     * The screenshot height in pixels.
     *
     * Since: 1.0
     */
    properties[PROP_HEIGHT] =
        g_param_spec_int ("height",
                          "Height",
                          "Screenshot height in pixels",
                          0, G_MAXINT, 0,
                          G_PARAM_READABLE |
                          G_PARAM_STATIC_STRINGS);

    /**
     * LrgScreenshot:image:
     *
     * The underlying image data.
     *
     * Since: 1.0
     */
    properties[PROP_IMAGE] =
        g_param_spec_object ("image",
                             "Image",
                             "The underlying image data",
                             GRL_TYPE_IMAGE,
                             G_PARAM_READWRITE |
                             G_PARAM_CONSTRUCT_ONLY |
                             G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_screenshot_init (LrgScreenshot *self)
{
    self->image = NULL;
    self->width = 0;
    self->height = 0;
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

LrgScreenshot *
lrg_screenshot_new (void)
{
    return g_object_new (LRG_TYPE_SCREENSHOT, NULL);
}

LrgScreenshot *
lrg_screenshot_new_from_image (GrlImage *image)
{
    g_return_val_if_fail (GRL_IS_IMAGE (image), NULL);

    return g_object_new (LRG_TYPE_SCREENSHOT,
                         "image", image,
                         NULL);
}

LrgScreenshot *
lrg_screenshot_capture (GError **error)
{
    g_autoptr(GrlImage) image = NULL;

    /* Capture the current screen content using graylib */
    image = grl_image_new_from_screen ();
    if (image == NULL)
    {
        g_set_error (error,
                     LRG_PHOTO_MODE_ERROR,
                     LRG_PHOTO_MODE_ERROR_CAPTURE,
                     "Failed to capture screenshot from screen");
        return NULL;
    }

    lrg_debug (LRG_LOG_DOMAIN_PHOTOMODE,
               "Captured screenshot: %dx%d",
               grl_image_get_width (image),
               grl_image_get_height (image));

    return lrg_screenshot_new_from_image (image);
}

gint
lrg_screenshot_get_width (LrgScreenshot *self)
{
    g_return_val_if_fail (LRG_IS_SCREENSHOT (self), 0);

    return self->width;
}

gint
lrg_screenshot_get_height (LrgScreenshot *self)
{
    g_return_val_if_fail (LRG_IS_SCREENSHOT (self), 0);

    return self->height;
}

GrlImage *
lrg_screenshot_get_image (LrgScreenshot *self)
{
    g_return_val_if_fail (LRG_IS_SCREENSHOT (self), NULL);

    return self->image;
}

gboolean
lrg_screenshot_save (LrgScreenshot       *self,
                     const gchar         *path,
                     LrgScreenshotFormat  format,
                     GError             **error)
{
    g_return_val_if_fail (LRG_IS_SCREENSHOT (self), FALSE);
    g_return_val_if_fail (path != NULL, FALSE);

    switch (format)
    {
    case LRG_SCREENSHOT_FORMAT_PNG:
        return lrg_screenshot_save_png (self, path, error);
    case LRG_SCREENSHOT_FORMAT_JPG:
        return lrg_screenshot_save_jpg (self, path, 90, error);
    default:
        g_set_error (error,
                     LRG_PHOTO_MODE_ERROR,
                     LRG_PHOTO_MODE_ERROR_INVALID_FORMAT,
                     "Invalid screenshot format: %d", format);
        return FALSE;
    }
}

gboolean
lrg_screenshot_save_png (LrgScreenshot  *self,
                         const gchar    *path,
                         GError        **error)
{
    g_return_val_if_fail (LRG_IS_SCREENSHOT (self), FALSE);
    g_return_val_if_fail (path != NULL, FALSE);

    if (self->image == NULL)
    {
        g_set_error (error,
                     LRG_PHOTO_MODE_ERROR,
                     LRG_PHOTO_MODE_ERROR_SAVE,
                     "No image data to save");
        return FALSE;
    }

    if (!grl_image_export (self->image, path))
    {
        g_set_error (error,
                     LRG_PHOTO_MODE_ERROR,
                     LRG_PHOTO_MODE_ERROR_SAVE,
                     "Failed to save PNG to: %s", path);
        return FALSE;
    }

    lrg_debug (LRG_LOG_DOMAIN_PHOTOMODE, "Saved screenshot to: %s", path);

    return TRUE;
}

gboolean
lrg_screenshot_save_jpg (LrgScreenshot  *self,
                         const gchar    *path,
                         gint            quality,
                         GError        **error)
{
    g_return_val_if_fail (LRG_IS_SCREENSHOT (self), FALSE);
    g_return_val_if_fail (path != NULL, FALSE);
    g_return_val_if_fail (quality >= 1 && quality <= 100, FALSE);

    if (self->image == NULL)
    {
        g_set_error (error,
                     LRG_PHOTO_MODE_ERROR,
                     LRG_PHOTO_MODE_ERROR_SAVE,
                     "No image data to save");
        return FALSE;
    }

    /* Export to JPG - graylib handles format based on extension */
    if (!grl_image_export (self->image, path))
    {
        g_set_error (error,
                     LRG_PHOTO_MODE_ERROR,
                     LRG_PHOTO_MODE_ERROR_SAVE,
                     "Failed to save JPG to: %s", path);
        return FALSE;
    }

    lrg_debug (LRG_LOG_DOMAIN_PHOTOMODE,
               "Saved screenshot to: %s (quality: %d)",
               path, quality);

    return TRUE;
}

GrlTexture *
lrg_screenshot_to_texture (LrgScreenshot *self)
{
    g_return_val_if_fail (LRG_IS_SCREENSHOT (self), NULL);

    if (self->image == NULL)
        return NULL;

    return grl_texture_new_from_image (self->image);
}
