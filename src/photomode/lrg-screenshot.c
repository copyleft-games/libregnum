/* lrg-screenshot.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgScreenshot - Screenshot capture and save.
 */

#include "config.h"

#include <string.h>

#include "lrg-screenshot.h"
#include "../lrg-log.h"

struct _LrgScreenshot
{
    GObject parent_instance;

    GrlImage *image;
    gint width;
    gint height;

    /* PNG text entries: keys in insertion order + the values */
    GPtrArray *meta_keys;
    GHashTable *meta;
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
    g_clear_pointer (&self->meta_keys, g_ptr_array_unref);
    g_clear_pointer (&self->meta, g_hash_table_unref);

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

/* ==========================================================================
 * PNG text chunks
 * ========================================================================== */

#define PNG_SIGNATURE_LEN (8)
#define PNG_KEY_MAX (79)

static const guint8 png_signature[PNG_SIGNATURE_LEN] = { 0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a };

/* CRC-32 of a chunk (type and data), the table made on first use */
static guint32
png_crc (const guint8 *data,
         gsize         length)
{
    static guint32 table[256];
    static gsize   ready = 0;
    guint32 crc = 0xffffffffu;
    gsize i;

    if (g_once_init_enter (&ready))
    {
        guint32 n;

        for (n = 0; n < 256; n++)
        {
            guint32 c = n;
            gint k;

            for (k = 0; k < 8; k++)
                c = (c & 1) ? 0xedb88320u ^ (c >> 1) : c >> 1;
            table[n] = c;
        }
        g_once_init_leave (&ready, 1);
    }
    for (i = 0; i < length; i++)
        crc = table[(crc ^ data[i]) & 0xff] ^ (crc >> 8);
    return crc ^ 0xffffffffu;
}

static void
put_u32 (guint8  *out,
         guint32  value)
{
    out[0] = (guint8)(value >> 24);
    out[1] = (guint8)(value >> 16);
    out[2] = (guint8)(value >> 8);
    out[3] = (guint8)value;
}

static guint32
get_u32 (const guint8 *in)
{
    return ((guint32)in[0] << 24) | ((guint32)in[1] << 16) | ((guint32)in[2] << 8) | (guint32)in[3];
}

/* one tEXt chunk: length, "tEXt", keyword, NUL, text, CRC */
static void
append_text_chunk (GByteArray  *out,
                   const gchar *key,
                   const gchar *value)
{
    gsize key_len = strlen (key);
    gsize value_len = strlen (value);
    gsize data_len = key_len + 1 + value_len;
    g_autofree guint8 *chunk = NULL;
    guint8 word[4];

    /* type and data are one buffer so the CRC covers both */
    chunk = g_malloc (4 + data_len);
    memcpy (chunk, "tEXt", 4);
    memcpy (chunk + 4, key, key_len);
    chunk[4 + key_len] = 0;
    memcpy (chunk + 4 + key_len + 1, value, value_len);
    put_u32 (word, (guint32)data_len);
    g_byte_array_append (out, word, 4);
    g_byte_array_append (out, chunk, (guint)(4 + data_len));
    put_u32 (word, png_crc (chunk, 4 + data_len));
    g_byte_array_append (out, word, 4);
}

/* the PNG of the image with the text entries after IHDR */
static GBytes *
encode_png_with_text (LrgScreenshot  *self,
                      GError        **error)
{
    g_autofree guint8 *raw = NULL;
    g_autoptr(GByteArray) out = NULL;
    gsize raw_size = 0;
    gsize header_end;
    guint i;

    raw = grl_image_export_to_memory (self->image, ".png", &raw_size);
    if (raw == NULL || raw_size < PNG_SIGNATURE_LEN + 25 ||
        memcmp (raw, png_signature, PNG_SIGNATURE_LEN) != 0)
    {
        g_set_error_literal (error, LRG_PHOTO_MODE_ERROR, LRG_PHOTO_MODE_ERROR_SAVE,
                             "Failed to encode the PNG");
        return NULL;
    }

    /* signature, then IHDR: length (13), type, data, CRC = 25 bytes */
    header_end = PNG_SIGNATURE_LEN + 25;
    out = g_byte_array_new ();
    g_byte_array_append (out, raw, (guint)header_end);
    for (i = 0; i < self->meta_keys->len; i++)
    {
        const gchar *key = g_ptr_array_index (self->meta_keys, i);

        append_text_chunk (out, key, g_hash_table_lookup (self->meta, key));
    }
    g_byte_array_append (out, raw + header_end, (guint)(raw_size - header_end));
    return g_byte_array_free_to_bytes (g_steal_pointer (&out));
}

GHashTable *
lrg_screenshot_read_png_metadata (const gchar  *path,
                                  GError      **error)
{
    g_autofree gchar *contents = NULL;
    g_autoptr(GHashTable) table = NULL;
    g_autoptr(GError) local = NULL;
    const guint8 *data;
    gsize length = 0;
    gsize at;

    g_return_val_if_fail (path != NULL, NULL);

    if (!g_file_get_contents (path, &contents, &length, &local))
    {
        g_set_error (error, LRG_PHOTO_MODE_ERROR, LRG_PHOTO_MODE_ERROR_FAILED,
                     "Cannot read %s: %s", path, local->message);
        return NULL;
    }
    data = (const guint8 *)contents;
    if (length < PNG_SIGNATURE_LEN || memcmp (data, png_signature, PNG_SIGNATURE_LEN) != 0)
    {
        g_set_error (error, LRG_PHOTO_MODE_ERROR, LRG_PHOTO_MODE_ERROR_INVALID_FORMAT,
                     "%s is not a PNG file", path);
        return NULL;
    }

    /* walk the chunks: length, type, data, CRC */
    table = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
    at = PNG_SIGNATURE_LEN;
    while (at + 12 <= length)
    {
        guint32 chunk_len = get_u32 (data + at);
        const guint8 *type = data + at + 4;

        if ((gsize)chunk_len > length - at - 12)
        {
            g_set_error (error, LRG_PHOTO_MODE_ERROR, LRG_PHOTO_MODE_ERROR_INVALID_FORMAT,
                         "%s: a chunk runs past the end of the file", path);
            return NULL;
        }
        if (png_crc (type, (gsize)chunk_len + 4) != get_u32 (data + at + 8 + chunk_len))
        {
            g_set_error (error, LRG_PHOTO_MODE_ERROR, LRG_PHOTO_MODE_ERROR_INVALID_FORMAT,
                         "%s: a chunk has a bad CRC", path);
            return NULL;
        }
        if (memcmp (type, "tEXt", 4) == 0)
        {
            const gchar *body = (const gchar *)(data + at + 8);
            const gchar *nul = memchr (body, 0, chunk_len);

            if (nul != NULL)
                g_hash_table_replace (table, g_strndup (body, (gsize)(nul - body)),
                                      g_strndup (nul + 1, (gsize)chunk_len - (gsize)(nul - body) - 1));
        }
        if (memcmp (type, "IEND", 4) == 0)
            break;
        at += 12 + (gsize)chunk_len;
    }
    return (GHashTable *)g_steal_pointer (&table);
}

LrgScreenshot *
lrg_screenshot_new_from_render_texture (GrlRenderTexture *texture)
{
    g_autoptr(GrlImage) image = NULL;

    g_return_val_if_fail (GRL_IS_RENDER_TEXTURE (texture), NULL);

    image = grl_texture_to_image (grl_render_texture_get_texture (texture));
    if (image == NULL)
        return NULL;
    /* render textures are stored bottom up */
    grl_image_flip_vertical (image);
    return lrg_screenshot_new_from_image (image);
}

void
lrg_screenshot_set_metadata (LrgScreenshot *self,
                             const gchar   *key,
                             const gchar   *value)
{
    guint i;

    g_return_if_fail (LRG_IS_SCREENSHOT (self));
    g_return_if_fail (key != NULL && key[0] != '\0' && strlen (key) <= PNG_KEY_MAX);

    if (self->meta == NULL)
    {
        self->meta = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
        self->meta_keys = g_ptr_array_new_with_free_func (g_free);
    }
    if (value == NULL)
    {
        g_hash_table_remove (self->meta, key);
        for (i = 0; i < self->meta_keys->len; i++)
        {
            if (g_strcmp0 (g_ptr_array_index (self->meta_keys, i), key) == 0)
            {
                g_ptr_array_remove_index (self->meta_keys, i);
                break;
            }
        }
        return;
    }
    if (!g_hash_table_contains (self->meta, key))
        g_ptr_array_add (self->meta_keys, g_strdup (key));
    g_hash_table_replace (self->meta, g_strdup (key), g_strdup (value));
}

const gchar *
lrg_screenshot_get_metadata (LrgScreenshot *self,
                             const gchar   *key)
{
    g_return_val_if_fail (LRG_IS_SCREENSHOT (self), NULL);
    g_return_val_if_fail (key != NULL, NULL);

    return self->meta != NULL ? g_hash_table_lookup (self->meta, key) : NULL;
}

gchar **
lrg_screenshot_get_metadata_keys (LrgScreenshot *self)
{
    GPtrArray *out;
    guint i;

    g_return_val_if_fail (LRG_IS_SCREENSHOT (self), NULL);

    out = g_ptr_array_new ();
    if (self->meta_keys != NULL)
        for (i = 0; i < self->meta_keys->len; i++)
            g_ptr_array_add (out, g_strdup (g_ptr_array_index (self->meta_keys, i)));
    g_ptr_array_add (out, NULL);
    return (gchar **)g_ptr_array_free (out, FALSE);
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

    /* with text entries the PNG is encoded to memory, the chunks inserted
     * after the header, and the file written in one go */
    if (self->meta_keys != NULL && self->meta_keys->len > 0)
    {
        g_autoptr(GBytes) png = NULL;
        g_autoptr(GError) local = NULL;

        png = encode_png_with_text (self, error);
        if (png == NULL)
            return FALSE;
        if (!g_file_set_contents (path, g_bytes_get_data (png, NULL),
                                  (gssize)g_bytes_get_size (png), &local))
        {
            g_set_error (error, LRG_PHOTO_MODE_ERROR, LRG_PHOTO_MODE_ERROR_SAVE,
                         "Failed to save PNG to %s: %s", path, local->message);
            return FALSE;
        }
    }
    else if (!grl_image_export (self->image, path))
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
