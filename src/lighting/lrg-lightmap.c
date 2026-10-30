/* lrg-lightmap.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Baked lightmap implementation.
 *
 * The CPU copy is a tightly packed RGB8 buffer (width * height * 3).
 * Files go through graylib's GrlImage, so every format raylib can load
 * (PNG, BMP, TGA, JPG, QOI, ...) can be a lightmap and every format it
 * can export can be a save target; none of that needs a window.  The
 * GPU copy is a GrlTexture created on the first lrg_lightmap_upload()
 * once a GL context exists and refreshed whenever pixels change.
 */

#include "config.h"

#include <gio/gio.h>
#include <raylib.h>
#include <graylib.h>

#include "lrg-lightmap.h"
#include "../lrg-log.h"

/* Upper bound on either side; matches the "width"/"height" pspecs. */
#define LIGHTMAP_MAX_SIZE (8192)

struct _LrgLightmap
{
    GObject parent_instance;

    guint       width;
    guint       height;
    guint       texture_id;
    guint8     *data;      /* RGB data */
    gboolean    dirty;
    GrlTexture *texture;   /* GPU copy, NULL until uploaded */
};

G_DEFINE_FINAL_TYPE (LrgLightmap, lrg_lightmap, G_TYPE_OBJECT)

enum
{
    PROP_0,
    PROP_WIDTH,
    PROP_HEIGHT,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

static void
lrg_lightmap_finalize (GObject *object)
{
    LrgLightmap *self = LRG_LIGHTMAP (object);

    g_clear_pointer (&self->data, g_free);
    g_clear_object (&self->texture);

    G_OBJECT_CLASS (lrg_lightmap_parent_class)->finalize (object);
}

static void
lrg_lightmap_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
    LrgLightmap *self = LRG_LIGHTMAP (object);
    switch (prop_id)
    {
    case PROP_WIDTH: g_value_set_uint (value, self->width); break;
    case PROP_HEIGHT: g_value_set_uint (value, self->height); break;
    default: G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_lightmap_class_init (LrgLightmapClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = lrg_lightmap_finalize;
    object_class->get_property = lrg_lightmap_get_property;

    properties[PROP_WIDTH] = g_param_spec_uint ("width", "Width", "Lightmap width", 1, LIGHTMAP_MAX_SIZE, 256, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
    properties[PROP_HEIGHT] = g_param_spec_uint ("height", "Height", "Lightmap height", 1, LIGHTMAP_MAX_SIZE, 256, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_lightmap_init (LrgLightmap *self)
{
    self->width = 256;
    self->height = 256;
    self->texture_id = 0;
    self->data = NULL;
    self->dirty = FALSE;
    self->texture = NULL;
}

/*
 * to_rgba_image:
 *
 * Expands the RGB8 buffer into a new opaque RGBA8 #GrlImage, the one
 * pixel format graylib can wrap from raw memory.
 */
static GrlImage *
to_rgba_image (LrgLightmap *self)
{
    g_autofree guint8 *rgba = NULL;
    gsize n_pixels;
    gsize i;

    n_pixels = (gsize) self->width * (gsize) self->height;
    rgba = g_malloc (n_pixels * 4);

    for (i = 0; i < n_pixels; i++)
    {
        rgba[i * 4]     = self->data[i * 3];
        rgba[i * 4 + 1] = self->data[i * 3 + 1];
        rgba[i * 4 + 2] = self->data[i * 3 + 2];
        rgba[i * 4 + 3] = 255;
    }

    return grl_image_new_from_pixels ((gint) self->width, (gint) self->height,
                                      GRL_PIXELFORMAT_UNCOMPRESSED_R8G8B8A8,
                                      rgba);
}

/* Public API */

/**
 * lrg_lightmap_new:
 * @width: width in texels (1 - 8192)
 * @height: height in texels (1 - 8192)
 *
 * Creates a black lightmap of the given size.
 *
 * Returns: (transfer full): a new lightmap
 */
LrgLightmap *
lrg_lightmap_new (guint width, guint height)
{
    LrgLightmap *self;

    g_return_val_if_fail (width > 0 && width <= LIGHTMAP_MAX_SIZE, NULL);
    g_return_val_if_fail (height > 0 && height <= LIGHTMAP_MAX_SIZE, NULL);

    self = g_object_new (LRG_TYPE_LIGHTMAP, NULL);
    self->width = width;
    self->height = height;
    self->data = g_new0 (guint8, (gsize) width * height * 3);
    self->dirty = TRUE;
    return self;
}

/**
 * lrg_lightmap_load:
 * @path: (type filename): the image file holding the lightmap
 * @error: (nullable): return location for a #GError
 *
 * Loads a lightmap from an image file.  Any format graylib (raylib)
 * can decode works; the RGB channels become the light colour and alpha
 * is ignored.  Loading is CPU only and needs no window; call
 * lrg_lightmap_upload() once a GL context exists to use it on the GPU.
 *
 * Errors are in the %G_IO_ERROR domain: %G_IO_ERROR_NOT_FOUND when
 * @path does not exist, %G_IO_ERROR_INVALID_DATA when it cannot be
 * decoded or is larger than 8192 texels on a side.
 *
 * Returns: (transfer full) (nullable): a new lightmap, or %NULL on error
 */
LrgLightmap *
lrg_lightmap_load (const gchar *path, GError **error)
{
    g_autoptr(GrlImage) image = NULL;
    LrgLightmap *self;
    const guint8 *rgba;
    gint width;
    gint height;
    gsize n_pixels;
    gsize i;

    g_return_val_if_fail (path != NULL, NULL);
    g_return_val_if_fail (error == NULL || *error == NULL, NULL);

    /* raylib only logs a missing file; report it properly first. */
    if (!g_file_test (path, G_FILE_TEST_IS_REGULAR))
    {
        g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
                     "Lightmap file '%s' does not exist", path);
        return NULL;
    }

    image = grl_image_new_from_file (path);
    if (image == NULL || !grl_image_is_valid (image))
    {
        g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                     "Lightmap file '%s' is not a readable image", path);
        return NULL;
    }

    width = grl_image_get_width (image);
    height = grl_image_get_height (image);
    if (width <= 0 || height <= 0 ||
        width > LIGHTMAP_MAX_SIZE || height > LIGHTMAP_MAX_SIZE)
    {
        g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                     "Lightmap '%s' is %d x %d; sides must be 1 - %d texels",
                     path, width, height, LIGHTMAP_MAX_SIZE);
        return NULL;
    }

    /* Normalises any decoded format (grey, RGB, 16-bit...) to RGBA8. */
    rgba = grl_image_get_pixels (image, NULL);
    if (rgba == NULL)
    {
        g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                     "Lightmap file '%s' has no pixel data", path);
        return NULL;
    }

    self = lrg_lightmap_new ((guint) width, (guint) height);
    n_pixels = (gsize) width * (gsize) height;
    for (i = 0; i < n_pixels; i++)
    {
        self->data[i * 3]     = rgba[i * 4];
        self->data[i * 3 + 1] = rgba[i * 4 + 1];
        self->data[i * 3 + 2] = rgba[i * 4 + 2];
    }

    lrg_debug (LRG_LOG_DOMAIN_LIGHTING, "Loaded %d x %d lightmap from %s",
               width, height, path);
    return self;
}

/**
 * lrg_lightmap_save:
 * @self: an #LrgLightmap
 * @path: (type filename): the output file; its extension picks the
 *   format (".png", ".bmp", ".tga", ".qoi", ...)
 * @error: (nullable): return location for a #GError
 *
 * Writes the lightmap to an opaque RGBA image file.  Lossless formats
 * (PNG, BMP, TGA, QOI) round-trip exactly through lrg_lightmap_load().
 * Needs no window.
 *
 * Returns: %TRUE on success; on failure %FALSE with @error set
 *   (%G_IO_ERROR_FAILED)
 */
gboolean
lrg_lightmap_save (LrgLightmap *self, const gchar *path, GError **error)
{
    g_autoptr(GrlImage) image = NULL;

    g_return_val_if_fail (LRG_IS_LIGHTMAP (self), FALSE);
    g_return_val_if_fail (path != NULL, FALSE);
    g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

    image = to_rgba_image (self);
    if (image == NULL)
    {
        g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                     "Could not build an image for lightmap '%s'", path);
        return FALSE;
    }

    /* ExportImage picks the encoder from the extension. */
    if (!grl_image_export (image, path))
    {
        g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                     "Could not write lightmap '%s' (unwritable path or "
                     "unsupported extension)", path);
        return FALSE;
    }

    return TRUE;
}

guint lrg_lightmap_get_width (LrgLightmap *self) { g_return_val_if_fail (LRG_IS_LIGHTMAP (self), 0); return self->width; }
guint lrg_lightmap_get_height (LrgLightmap *self) { g_return_val_if_fail (LRG_IS_LIGHTMAP (self), 0); return self->height; }
guint lrg_lightmap_get_texture_id (LrgLightmap *self) { g_return_val_if_fail (LRG_IS_LIGHTMAP (self), 0); return self->texture_id; }

/**
 * lrg_lightmap_get_texture:
 * @self: an #LrgLightmap
 *
 * Gets the GPU copy made by the last successful lrg_lightmap_upload().
 *
 * Returns: (transfer none) (nullable): the texture, or %NULL before the
 *   first upload (or when no GL context was available)
 */
GrlTexture *
lrg_lightmap_get_texture (LrgLightmap *self)
{
    g_return_val_if_fail (LRG_IS_LIGHTMAP (self), NULL);
    return self->texture;
}

/**
 * lrg_lightmap_is_dirty:
 * @self: an #LrgLightmap
 *
 * Whether the CPU pixels changed since the last upload (a new lightmap
 * starts dirty).
 *
 * Returns: %TRUE if lrg_lightmap_upload() has work to do
 */
gboolean
lrg_lightmap_is_dirty (LrgLightmap *self)
{
    g_return_val_if_fail (LRG_IS_LIGHTMAP (self), FALSE);
    return self->dirty;
}

void
lrg_lightmap_set_pixel (LrgLightmap *self,
                        guint        x,
                        guint        y,
                        guint8       r,
                        guint8       g,
                        guint8       b)
{
    guint index;

    g_return_if_fail (LRG_IS_LIGHTMAP (self));
    g_return_if_fail (x < self->width);
    g_return_if_fail (y < self->height);

    index = (y * self->width + x) * 3;
    self->data[index] = r;
    self->data[index + 1] = g;
    self->data[index + 2] = b;
    self->dirty = TRUE;
}

void
lrg_lightmap_get_pixel (LrgLightmap *self,
                        guint        x,
                        guint        y,
                        guint8      *r,
                        guint8      *g,
                        guint8      *b)
{
    guint index;

    g_return_if_fail (LRG_IS_LIGHTMAP (self));
    g_return_if_fail (x < self->width);
    g_return_if_fail (y < self->height);

    index = (y * self->width + x) * 3;
    if (r) *r = self->data[index];
    if (g) *g = self->data[index + 1];
    if (b) *b = self->data[index + 2];
}

void
lrg_lightmap_clear (LrgLightmap *self,
                    guint8       r,
                    guint8       g,
                    guint8       b)
{
    guint i;

    g_return_if_fail (LRG_IS_LIGHTMAP (self));

    for (i = 0; i < self->width * self->height; i++)
    {
        self->data[i * 3] = r;
        self->data[i * 3 + 1] = g;
        self->data[i * 3 + 2] = b;
    }
    self->dirty = TRUE;
}

/**
 * lrg_lightmap_upload:
 * @self: an #LrgLightmap
 *
 * Copies changed pixels to the GPU: creates the texture on first use
 * and updates it in place afterwards.  Does nothing when the lightmap is
 * clean, and leaves it dirty when no window (GL context) exists yet so
 * a later call can still upload.
 */
void
lrg_lightmap_upload (LrgLightmap *self)
{
    g_autoptr(GrlImage) image = NULL;

    g_return_if_fail (LRG_IS_LIGHTMAP (self));

    if (!self->dirty)
        return;

    /* No GL context: keep the pixels pending. */
    if (!IsWindowReady ())
        return;

    image = to_rgba_image (self);
    if (image == NULL)
        return;

    if (self->texture == NULL)
    {
        self->texture = grl_texture_new_from_image (image);
        /* Smooth between texels, but never bleed across opposite edges. */
        grl_texture_set_filter (self->texture, GRL_TEXTURE_FILTER_BILINEAR);
        grl_texture_set_wrap (self->texture, GRL_TEXTURE_WRAP_CLAMP);
    }
    else
    {
        grl_texture_update (self->texture, image);
    }

    self->texture_id = ((Texture2D *) grl_texture_get_handle (self->texture))->id;
    self->dirty = FALSE;
}
