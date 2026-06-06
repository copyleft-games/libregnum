/* lrg-loaded-game.c - Load a libregnum game packaged as a shared module
 *
 * Copyright 2025 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * This file is part of Libregnum.
 */

#include "config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_GAMEMODULE

#include <gmodule.h>

#include "lrg-loaded-game.h"
#include "../lrg-log.h"
#include "../template/lrg-game-template.h"
#include "../mod/lrg-mod-manifest.h"

struct _LrgLoadedGame
{
    GObject parent_instance;

    GModule                 *module;     /* Owned; kept open + resident */
    const LrgGameModuleInfo *info;       /* Points into the module image */
    GType                    game_type;
    LrgGameTemplate         *game;       /* Owned */
    gboolean                 loaded;
};

G_DEFINE_TYPE (LrgLoadedGame, lrg_loaded_game, G_TYPE_OBJECT)

G_DEFINE_QUARK (lrg-loaded-game-error-quark, lrg_loaded_game_error)

/* ==========================================================================
 * GObject
 * ========================================================================== */

static void
lrg_loaded_game_dispose (GObject *object)
{
    lrg_loaded_game_unload (LRG_LOADED_GAME (object));

    G_OBJECT_CLASS (lrg_loaded_game_parent_class)->dispose (object);
}

static void
lrg_loaded_game_class_init (LrgLoadedGameClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->dispose = lrg_loaded_game_dispose;
}

static void
lrg_loaded_game_init (LrgLoadedGame *self)
{
    self->module = NULL;
    self->info = NULL;
    self->game_type = G_TYPE_INVALID;
    self->game = NULL;
    self->loaded = FALSE;
}

/* ==========================================================================
 * Loading
 * ========================================================================== */

LrgLoadedGame *
lrg_loaded_game_load (const gchar  *so_path,
                      GError      **error)
{
    LrgLoadedGame           *self;
    GModule                 *module;
    gpointer                 symbol = NULL;
    LrgGameModuleQueryFunc   query;
    const LrgGameModuleInfo *info;
    GType                    game_type;
    GObject                 *game_obj;

    g_return_val_if_fail (so_path != NULL, NULL);

    /* Bind locally so the well-known entry symbol of one module never clashes
     * with another's. */
    module = g_module_open (so_path, G_MODULE_BIND_LAZY | G_MODULE_BIND_LOCAL);
    if (module == NULL)
    {
        g_set_error (error, LRG_LOADED_GAME_ERROR,
                     LRG_LOADED_GAME_ERROR_OPEN_FAILED,
                     "Failed to open game module '%s': %s",
                     so_path, g_module_error ());
        return NULL;
    }

    if (!g_module_symbol (module, LRG_GAME_MODULE_ENTRY_SYMBOL, &symbol) ||
        symbol == NULL)
    {
        g_set_error (error, LRG_LOADED_GAME_ERROR,
                     LRG_LOADED_GAME_ERROR_NO_ENTRY,
                     "Game module '%s' has no '%s' entry symbol",
                     so_path, LRG_GAME_MODULE_ENTRY_SYMBOL);
        g_module_close (module);
        return NULL;
    }

    query = (LrgGameModuleQueryFunc) symbol;
    info = query ();
    if (info == NULL)
    {
        g_set_error (error, LRG_LOADED_GAME_ERROR,
                     LRG_LOADED_GAME_ERROR_NO_ENTRY,
                     "Game module '%s' returned no module info", so_path);
        g_module_close (module);
        return NULL;
    }

    if (info->abi_version != LRG_GAME_MODULE_ABI_VERSION)
    {
        g_set_error (error, LRG_LOADED_GAME_ERROR,
                     LRG_LOADED_GAME_ERROR_ABI_MISMATCH,
                     "Game module '%s' ABI version %u does not match %u",
                     so_path, info->abi_version,
                     (guint) LRG_GAME_MODULE_ABI_VERSION);
        g_module_close (module);
        return NULL;
    }

    /* Refuse a module built against a newer libregnum than the running one. */
    if (info->lrg_major > LRG_VERSION_MAJOR ||
        (info->lrg_major == LRG_VERSION_MAJOR &&
         info->lrg_minor > LRG_VERSION_MINOR))
    {
        g_set_error (error, LRG_LOADED_GAME_ERROR,
                     LRG_LOADED_GAME_ERROR_VERSION_MISMATCH,
                     "Game module '%s' was built against libregnum %u.%u.%u, "
                     "newer than %d.%d.%d",
                     so_path, info->lrg_major, info->lrg_minor, info->lrg_micro,
                     LRG_VERSION_MAJOR, LRG_VERSION_MINOR, LRG_VERSION_MICRO);
        g_module_close (module);
        return NULL;
    }

    if (info->get_type == NULL)
    {
        g_set_error (error, LRG_LOADED_GAME_ERROR,
                     LRG_LOADED_GAME_ERROR_BAD_TYPE,
                     "Game module '%s' provides no game type", so_path);
        g_module_close (module);
        return NULL;
    }

    /* The game registers a GType the moment we resolve it. GLib cannot
     * unregister a GType, so keep the module's code mapped for the process
     * lifetime to avoid dangling class vtables after unload. */
    g_module_make_resident (module);

    game_type = info->get_type ();
    if (!g_type_is_a (game_type, LRG_TYPE_GAME_TEMPLATE))
    {
        g_set_error (error, LRG_LOADED_GAME_ERROR,
                     LRG_LOADED_GAME_ERROR_BAD_TYPE,
                     "Game module '%s' type '%s' is not an LrgGameTemplate",
                     so_path, g_type_name (game_type));
        g_module_close (module);
        return NULL;
    }

    game_obj = g_object_new (game_type, NULL);
    if (!LRG_IS_GAME_TEMPLATE (game_obj))
    {
        g_set_error (error, LRG_LOADED_GAME_ERROR,
                     LRG_LOADED_GAME_ERROR_BAD_TYPE,
                     "Game module '%s' failed to instantiate its game type",
                     so_path);
        g_clear_object (&game_obj);
        g_module_close (module);
        return NULL;
    }

    self = g_object_new (LRG_TYPE_LOADED_GAME, NULL);
    self->module = module;
    self->info = info;
    self->game_type = game_type;
    self->game = LRG_GAME_TEMPLATE (game_obj);
    self->loaded = TRUE;

    lrg_debug (LRG_LOG_DOMAIN_GAMEMODULE,
               "Loaded game module '%s' (%s %s)", so_path,
               info->game_id != NULL ? info->game_id : "?",
               info->game_version != NULL ? info->game_version : "?");

    return self;
}

LrgLoadedGame *
lrg_loaded_game_load_from_manifest (LrgModManifest  *manifest,
                                    const gchar     *base_path,
                                    GError         **error)
{
    const gchar      *entry_point;
    g_autofree gchar *so_path = NULL;

    g_return_val_if_fail (manifest != NULL, NULL);

    entry_point = lrg_mod_manifest_get_entry_point (manifest);
    if (entry_point == NULL)
    {
        g_set_error (error, LRG_LOADED_GAME_ERROR,
                     LRG_LOADED_GAME_ERROR_NO_ENTRY,
                     "Game manifest '%s' has no entry_point",
                     lrg_mod_manifest_get_id (manifest));
        return NULL;
    }

    if (base_path != NULL)
        so_path = g_build_filename (base_path, entry_point, NULL);
    else
        so_path = g_strdup (entry_point);

    return lrg_loaded_game_load (so_path, error);
}

/* ==========================================================================
 * Accessors / teardown
 * ========================================================================== */

LrgGameTemplate *
lrg_loaded_game_get_game (LrgLoadedGame *self)
{
    g_return_val_if_fail (LRG_IS_LOADED_GAME (self), NULL);

    return self->game;
}

const LrgGameModuleInfo *
lrg_loaded_game_get_info (LrgLoadedGame *self)
{
    g_return_val_if_fail (LRG_IS_LOADED_GAME (self), NULL);

    return self->info;
}

void
lrg_loaded_game_unload (LrgLoadedGame *self)
{
    g_return_if_fail (LRG_IS_LOADED_GAME (self));

    if (!self->loaded)
        return;

    /* Release the game object (running its finalize) while the module is still
     * mapped, then close the module. The module is resident, so closing it is
     * a safe no-op as far as the address space is concerned. */
    g_clear_object (&self->game);
    self->info = NULL;
    self->game_type = G_TYPE_INVALID;

    if (self->module != NULL)
    {
        g_module_close (self->module);
        self->module = NULL;
    }

    self->loaded = FALSE;
}
