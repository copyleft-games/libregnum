/* lrg-font-manager.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "config.h"
#include "lrg-font-manager.h"
#include <gio/gio.h>
#include <string.h>

/**
 * SECTION:lrg-font-manager
 * @Title: LrgFontManager
 * @Short_description: Font loading and caching
 *
 * #LrgFontManager is a singleton that manages font loading, caching,
 * and provides text measurement and drawing utilities.
 *
 * Fonts are identified by a user-provided name for easy reference.
 * The manager caches loaded fonts and handles memory management.
 */

typedef struct
{
    gchar *path;
    gint   size;
    /* In a real implementation, this would hold GrlFont* or similar */
} FontEntry;

static void
font_entry_free (FontEntry *entry)
{
    if (entry != NULL)
    {
        g_free (entry->path);
        g_slice_free (FontEntry, entry);
    }
}

struct _LrgFontManager
{
    GObject parent_instance;

    GHashTable *fonts;  /* name -> FontEntry */
    gchar      *default_font;
};

G_DEFINE_TYPE (LrgFontManager, lrg_font_manager, G_TYPE_OBJECT)

static LrgFontManager *default_manager = NULL;

static void
lrg_font_manager_finalize (GObject *object)
{
    LrgFontManager *self = LRG_FONT_MANAGER (object);

    g_hash_table_unref (self->fonts);
    g_free (self->default_font);

    G_OBJECT_CLASS (lrg_font_manager_parent_class)->finalize (object);
}

static void
lrg_font_manager_class_init (LrgFontManagerClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = lrg_font_manager_finalize;
}

static void
lrg_font_manager_init (LrgFontManager *self)
{
    self->fonts = g_hash_table_new_full (g_str_hash, g_str_equal,
                                         g_free, (GDestroyNotify) font_entry_free);
    self->default_font = NULL;
}

/*
 * Public API
 */

LrgFontManager *
lrg_font_manager_get_default (void)
{
    if (default_manager == NULL)
        default_manager = g_object_new (LRG_TYPE_FONT_MANAGER, NULL);

    return default_manager;
}

gboolean
lrg_font_manager_load_font (LrgFontManager *self,
                            const gchar    *name,
                            const gchar    *path,
                            gint            size,
                            GError        **error)
{
    FontEntry *entry;

    g_return_val_if_fail (LRG_IS_FONT_MANAGER (self), FALSE);
    g_return_val_if_fail (name != NULL, FALSE);
    g_return_val_if_fail (path != NULL, FALSE);
    g_return_val_if_fail (size > 0, FALSE);

    /* Check if font already loaded */
    if (g_hash_table_contains (self->fonts, name))
    {
        g_set_error (error, G_IO_ERROR, G_IO_ERROR_EXISTS,
                     "Font '%s' is already loaded", name);
        return FALSE;
    }

    /* Check if file exists */
    if (!g_file_test (path, G_FILE_TEST_EXISTS))
    {
        g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
                     "Font file not found: %s", path);
        return FALSE;
    }

    /*
     * In a real implementation, this would load the font using
     * raylib's LoadFont() or similar through graylib.
     * For now, we just store the metadata.
     */
    entry = g_slice_new (FontEntry);
    entry->path = g_strdup (path);
    entry->size = size;

    g_hash_table_insert (self->fonts, g_strdup (name), entry);

    /* Set as default if first font */
    if (self->default_font == NULL)
        self->default_font = g_strdup (name);

    return TRUE;
}

gboolean
lrg_font_manager_has_font (LrgFontManager *self,
                           const gchar    *name)
{
    g_return_val_if_fail (LRG_IS_FONT_MANAGER (self), FALSE);
    g_return_val_if_fail (name != NULL, FALSE);

    return g_hash_table_contains (self->fonts, name);
}

void
lrg_font_manager_unload_font (LrgFontManager *self,
                              const gchar    *name)
{
    g_return_if_fail (LRG_IS_FONT_MANAGER (self));
    g_return_if_fail (name != NULL);

    g_hash_table_remove (self->fonts, name);

    /* Clear default if it was unloaded */
    if (self->default_font != NULL && g_str_equal (self->default_font, name))
    {
        g_free (self->default_font);
        self->default_font = NULL;
    }
}

void
lrg_font_manager_unload_all (LrgFontManager *self)
{
    g_return_if_fail (LRG_IS_FONT_MANAGER (self));

    g_hash_table_remove_all (self->fonts);
    g_clear_pointer (&self->default_font, g_free);
}

const gchar *
lrg_font_manager_get_default_font_name (LrgFontManager *self)
{
    g_return_val_if_fail (LRG_IS_FONT_MANAGER (self), NULL);
    return self->default_font;
}

void
lrg_font_manager_set_default_font_name (LrgFontManager *self,
                                        const gchar    *name)
{
    g_return_if_fail (LRG_IS_FONT_MANAGER (self));

    if (name != NULL && !g_hash_table_contains (self->fonts, name))
    {
        g_warning ("Font '%s' is not loaded", name);
        return;
    }

    g_free (self->default_font);
    self->default_font = g_strdup (name);
}

GPtrArray *
lrg_font_manager_get_font_names (LrgFontManager *self)
{
    GPtrArray *names;
    GHashTableIter iter;
    gpointer key;

    g_return_val_if_fail (LRG_IS_FONT_MANAGER (self), NULL);

    names = g_ptr_array_new_with_free_func (g_free);

    g_hash_table_iter_init (&iter, self->fonts);
    while (g_hash_table_iter_next (&iter, &key, NULL))
        g_ptr_array_add (names, g_strdup (key));

    return names;
}

void
lrg_font_manager_measure_text (LrgFontManager *self,
                               const gchar    *font_name,
                               const gchar    *text,
                               gfloat          font_size,
                               gfloat         *width,
                               gfloat         *height)
{
    const gchar *name;
    gsize len;

    g_return_if_fail (LRG_IS_FONT_MANAGER (self));
    g_return_if_fail (text != NULL);

    name = font_name != NULL ? font_name : self->default_font;

    /*
     * In a real implementation, this would use the loaded font
     * to measure the actual text dimensions.
     * For now, use a simple approximation.
     */
    len = g_utf8_strlen (text, -1);

    if (width != NULL)
        *width = (gfloat) len * font_size * 0.6f;

    if (height != NULL)
        *height = font_size;

    (void) name;
}

void
lrg_font_manager_draw_text (LrgFontManager *self,
                            const gchar    *font_name,
                            const gchar    *text,
                            gfloat          x,
                            gfloat          y,
                            gfloat          font_size,
                            guint8          r,
                            guint8          g,
                            guint8          b,
                            guint8          a)
{
    const gchar *name;

    g_return_if_fail (LRG_IS_FONT_MANAGER (self));
    g_return_if_fail (text != NULL);

    name = font_name != NULL ? font_name : self->default_font;

    /*
     * In a real implementation, this would use graylib's
     * text drawing functions with the loaded font.
     * This is a stub for the API.
     */
    (void) name;
    (void) x;
    (void) y;
    (void) font_size;
    (void) r;
    (void) g;
    (void) b;
    (void) a;
}
