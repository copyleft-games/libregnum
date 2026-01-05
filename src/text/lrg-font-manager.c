/* lrg-font-manager.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_TEXT

#include "lrg-font-manager.h"
#include "../lrg-log.h"
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
 *
 * On initialization, the manager searches for common system fonts:
 * - Linux: Liberation Sans, Noto Sans, DejaVu Sans
 * - Windows: Segoe UI, Arial, Verdana
 */

/* ==========================================================================
 * Platform-specific font search configuration
 * ========================================================================== */

#ifdef G_OS_WIN32
static const gchar * const FONT_SEARCH_PATHS[] = {
    "C:/Windows/Fonts",
    NULL
};
static const gchar * const FONT_CANDIDATES[] = {
    "segoeui.ttf",      /* Segoe UI - Windows default */
    "arial.ttf",        /* Arial */
    "verdana.ttf",      /* Verdana */
    NULL
};
#else
static const gchar * const FONT_SEARCH_PATHS[] = {
    "/usr/share/fonts/liberation-sans-fonts",  /* Fedora */
    "/usr/share/fonts/liberation-sans",
    "/usr/share/fonts/truetype/liberation",
    "/usr/share/fonts/google-noto-vf",         /* Fedora Noto variable fonts */
    "/usr/share/fonts/google-noto",
    "/usr/share/fonts/truetype/noto",
    "/usr/share/fonts/dejavu-sans-fonts",
    "/usr/share/fonts/truetype/dejavu",
    "/usr/share/fonts/TTF",                    /* Arch Linux */
    "/usr/share/fonts/liberation",             /* Some distros */
    "/usr/share/fonts/noto",                   /* Some distros */
    NULL
};
static const gchar * const FONT_CANDIDATES[] = {
    "LiberationSans-Regular.ttf",
    "NotoSans-Regular.ttf",
    "DejaVuSans.ttf",
    NULL
};
#endif

/* UI font size presets */
#define FONT_SIZE_SMALL  12
#define FONT_SIZE_NORMAL 16
#define FONT_SIZE_LARGE  24

/* ==========================================================================
 * Private Data
 * ========================================================================== */

typedef struct
{
    gchar   *path;
    gint     size;
    GrlFont *font;
} FontEntry;

static void
font_entry_free (FontEntry *entry)
{
    if (entry != NULL)
    {
        g_free (entry->path);
        g_clear_object (&entry->font);
        g_slice_free (FontEntry, entry);
    }
}

struct _LrgFontManager
{
    GObject     parent_instance;

    GHashTable *fonts;          /* name -> FontEntry */
    gchar      *default_font;   /* Name of default font */
    gboolean    initialized;    /* Whether initialize() was called */
};

G_DEFINE_TYPE (LrgFontManager, lrg_font_manager, G_TYPE_OBJECT)

static LrgFontManager *default_manager = NULL;

/* ==========================================================================
 * Private Helpers
 * ========================================================================== */

/*
 * find_system_font:
 *
 * Searches system font paths for the first available font file.
 *
 * Returns: (transfer full) (nullable): Path to found font, or %NULL
 */
static gchar *
find_system_font (void)
{
    guint i;
    guint j;

    for (i = 0; FONT_SEARCH_PATHS[i] != NULL; i++)
    {
        for (j = 0; FONT_CANDIDATES[j] != NULL; j++)
        {
            g_autofree gchar *path = NULL;

            path = g_build_filename (FONT_SEARCH_PATHS[i],
                                     FONT_CANDIDATES[j],
                                     NULL);

            if (g_file_test (path, G_FILE_TEST_EXISTS))
            {
                lrg_debug (LRG_LOG_DOMAIN_TEXT, "Found system font: %s", path);
                return g_steal_pointer (&path);
            }
        }
    }

    return NULL;
}

/* ==========================================================================
 * GObject Implementation
 * ========================================================================== */

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
    self->initialized = FALSE;
}

/* ==========================================================================
 * Public API - Singleton
 * ========================================================================== */

LrgFontManager *
lrg_font_manager_get_default (void)
{
    if (default_manager == NULL)
        default_manager = g_object_new (LRG_TYPE_FONT_MANAGER, NULL);

    return default_manager;
}

/* ==========================================================================
 * Public API - Initialization
 * ========================================================================== */

gboolean
lrg_font_manager_initialize (LrgFontManager  *self,
                             GError         **error)
{
    return lrg_font_manager_initialize_with_sizes (self,
                                                   FONT_SIZE_SMALL,
                                                   FONT_SIZE_NORMAL,
                                                   FONT_SIZE_LARGE,
                                                   error);
}

gboolean
lrg_font_manager_initialize_with_sizes (LrgFontManager  *self,
                                        gint             size_small,
                                        gint             size_normal,
                                        gint             size_large,
                                        GError         **error)
{
    g_autofree gchar *font_path = NULL;
    gboolean          success = FALSE;

    g_return_val_if_fail (LRG_IS_FONT_MANAGER (self), FALSE);
    g_return_val_if_fail (size_small > 0, FALSE);
    g_return_val_if_fail (size_normal > 0, FALSE);
    g_return_val_if_fail (size_large > 0, FALSE);

    if (self->initialized)
        return TRUE;

    lrg_info (LRG_LOG_DOMAIN_TEXT,
              "Initializing font manager with sizes %d/%d/%d",
              size_small, size_normal, size_large);

    /* Find a system font */
    font_path = find_system_font ();

    if (font_path == NULL)
    {
        lrg_warning (LRG_LOG_DOMAIN_TEXT,
                     "No system fonts found, using raylib default");
        self->initialized = TRUE;
        return TRUE;  /* Not a fatal error - raylib has a fallback */
    }

    /* Load font at multiple sizes for UI presets */
    if (lrg_font_manager_load_font (self, "ui-small", font_path,
                                    size_small, NULL))
    {
        success = TRUE;
    }

    if (lrg_font_manager_load_font (self, "ui-normal", font_path,
                                    size_normal, NULL))
    {
        success = TRUE;
    }

    if (lrg_font_manager_load_font (self, "ui-large", font_path,
                                    size_large, NULL))
    {
        success = TRUE;
    }

    if (success)
    {
        /* Set ui-normal as the default */
        lrg_font_manager_set_default_font_name (self, "ui-normal");
        lrg_info (LRG_LOG_DOMAIN_TEXT,
                  "Font manager initialized with %s", font_path);
    }
    else
    {
        lrg_warning (LRG_LOG_DOMAIN_TEXT,
                     "Failed to load fonts from %s", font_path);
    }

    self->initialized = TRUE;
    return success;
}

/* ==========================================================================
 * Public API - Font Access
 * ========================================================================== */

GrlFont *
lrg_font_manager_get_font (LrgFontManager *self,
                           const gchar    *name)
{
    FontEntry   *entry;
    const gchar *lookup_name;

    g_return_val_if_fail (LRG_IS_FONT_MANAGER (self), NULL);

    lookup_name = name != NULL ? name : self->default_font;

    if (lookup_name == NULL)
        return NULL;

    entry = g_hash_table_lookup (self->fonts, lookup_name);
    if (entry == NULL)
        return NULL;

    return entry->font;
}

GrlFont *
lrg_font_manager_get_default_font (LrgFontManager *self)
{
    g_return_val_if_fail (LRG_IS_FONT_MANAGER (self), NULL);

    return lrg_font_manager_get_font (self, NULL);
}

/* ==========================================================================
 * Public API - Font Loading
 * ========================================================================== */

gboolean
lrg_font_manager_load_font (LrgFontManager  *self,
                            const gchar     *name,
                            const gchar     *path,
                            gint             size,
                            GError         **error)
{
    FontEntry *entry;
    GrlFont   *font;

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

    /* Load the font via graylib */
    font = grl_font_new_from_file_ex (path, size, NULL, 0);
    if (font == NULL || !grl_font_is_valid (font))
    {
        g_clear_object (&font);
        g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                     "Failed to load font: %s", path);
        return FALSE;
    }

    entry = g_slice_new (FontEntry);
    entry->path = g_strdup (path);
    entry->size = size;
    entry->font = font;  /* Takes ownership */

    g_hash_table_insert (self->fonts, g_strdup (name), entry);

    lrg_debug (LRG_LOG_DOMAIN_TEXT,
               "Loaded font '%s' from %s (size %d)", name, path, size);

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

/* ==========================================================================
 * Public API - Default Font
 * ========================================================================== */

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
        lrg_warning (LRG_LOG_DOMAIN_TEXT, "Font '%s' is not loaded", name);
        return;
    }

    g_free (self->default_font);
    self->default_font = g_strdup (name);
}

GPtrArray *
lrg_font_manager_get_font_names (LrgFontManager *self)
{
    GPtrArray      *names;
    GHashTableIter  iter;
    gpointer        key;

    g_return_val_if_fail (LRG_IS_FONT_MANAGER (self), NULL);

    names = g_ptr_array_new_with_free_func (g_free);

    g_hash_table_iter_init (&iter, self->fonts);
    while (g_hash_table_iter_next (&iter, &key, NULL))
        g_ptr_array_add (names, g_strdup (key));

    return names;
}

/* ==========================================================================
 * Public API - Text Operations
 * ========================================================================== */

void
lrg_font_manager_measure_text (LrgFontManager *self,
                               const gchar    *font_name,
                               const gchar    *text,
                               gfloat          font_size,
                               gfloat         *width,
                               gfloat         *height)
{
    GrlFont *font;

    g_return_if_fail (LRG_IS_FONT_MANAGER (self));
    g_return_if_fail (text != NULL);

    font = lrg_font_manager_get_font (self, font_name);

    if (font != NULL)
    {
        g_autoptr(GrlVector2) size = NULL;

        size = grl_font_measure_text (font, text, font_size, 1.0f);

        if (width != NULL)
            *width = size->x;
        if (height != NULL)
            *height = size->y;
    }
    else
    {
        /* Fallback approximation when no font is loaded */
        gsize len;

        len = g_utf8_strlen (text, -1);

        if (width != NULL)
            *width = (gfloat) len * font_size * 0.6f;
        if (height != NULL)
            *height = font_size;
    }
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
    GrlFont *font;

    g_return_if_fail (LRG_IS_FONT_MANAGER (self));
    g_return_if_fail (text != NULL);

    font = lrg_font_manager_get_font (self, font_name);

    if (font != NULL)
    {
        g_autoptr(GrlVector2) pos = NULL;
        g_autoptr(GrlColor)   color = NULL;

        pos = grl_vector2_new (x, y);
        color = grl_color_new (r, g, b, a);

        grl_draw_text_ex (font, text, pos, font_size, 1.0f, color);
    }
    else
    {
        /* Fallback to raylib default font */
        g_autoptr(GrlColor) color = NULL;

        color = grl_color_new (r, g, b, a);
        grl_draw_text (text, (gint)x, (gint)y, (gint)font_size, color);
    }
}
