/* lrg-mod.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Mod implementation.
 */

#include "config.h"
#include "lrg-mod.h"
#include "../scripting/lrg-scripting-manager.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_MOD
#include "../lrg-log.h"

#include <gio/gio.h>
#include <string.h>

/* ==========================================================================
 * Private Data
 * ========================================================================== */

typedef struct
{
    LrgModManifest  *manifest;
    gchar           *base_path;
    gchar           *data_path;

    LrgModState      state;
    gboolean         enabled;
    gchar           *error_message;

    /* For native mods */
    GModule         *module;
    gpointer         user_data;

    /* For script mods */
    LrgScripting    *scripting;
} LrgModPrivate;

#pragma GCC visibility push(default)
G_DEFINE_TYPE_WITH_PRIVATE (LrgMod, lrg_mod, G_TYPE_OBJECT)
#pragma GCC visibility pop

enum
{
    PROP_0,
    PROP_MANIFEST,
    PROP_BASE_PATH,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

enum
{
    SIGNAL_PREPARE_SCRIPTING,
    N_SIGNALS
};

static guint signals[N_SIGNALS];

/* Forward declarations for virtual method implementations */
static gboolean lrg_mod_real_load             (LrgMod   *self,
                                               GError  **error);
static void     lrg_mod_real_unload           (LrgMod   *self);
static gboolean lrg_mod_real_can_load         (LrgMod   *self,
                                               GError  **error);
static gchar *  lrg_mod_real_get_display_info (LrgMod   *self);

/* ==========================================================================
 * GObject Implementation
 * ========================================================================== */

static void
lrg_mod_get_property (GObject    *object,
                      guint       prop_id,
                      GValue     *value,
                      GParamSpec *pspec)
{
    LrgMod *self = LRG_MOD (object);
    LrgModPrivate *priv = lrg_mod_get_instance_private (self);

    switch (prop_id)
    {
    case PROP_MANIFEST:
        g_value_set_object (value, priv->manifest);
        break;
    case PROP_BASE_PATH:
        g_value_set_string (value, priv->base_path);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
lrg_mod_set_property (GObject      *object,
                      guint         prop_id,
                      const GValue *value,
                      GParamSpec   *pspec)
{
    LrgMod *self = LRG_MOD (object);
    LrgModPrivate *priv = lrg_mod_get_instance_private (self);
    const gchar *data_subpath;

    switch (prop_id)
    {
    case PROP_MANIFEST:
        g_clear_object (&priv->manifest);
        priv->manifest = g_value_dup_object (value);
        /* Rebuild data path when manifest is set */
        if (priv->manifest != NULL && priv->base_path != NULL)
        {
            g_free (priv->data_path);
            data_subpath = lrg_mod_manifest_get_data_path (priv->manifest);
            if (data_subpath != NULL)
                priv->data_path = g_build_filename (priv->base_path, data_subpath, NULL);
            else
                priv->data_path = NULL;
        }
        break;
    case PROP_BASE_PATH:
        g_free (priv->base_path);
        priv->base_path = g_value_dup_string (value);
        /* Rebuild data path when base_path is set */
        if (priv->manifest != NULL && priv->base_path != NULL)
        {
            g_free (priv->data_path);
            data_subpath = lrg_mod_manifest_get_data_path (priv->manifest);
            if (data_subpath != NULL)
                priv->data_path = g_build_filename (priv->base_path, data_subpath, NULL);
            else
                priv->data_path = NULL;
        }
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
lrg_mod_dispose (GObject *object)
{
    LrgMod *self = LRG_MOD (object);
    LrgModPrivate *priv = lrg_mod_get_instance_private (self);

    if (priv->state == LRG_MOD_STATE_LOADED)
        lrg_mod_unload (self);

    g_clear_object (&priv->manifest);

    G_OBJECT_CLASS (lrg_mod_parent_class)->dispose (object);
}

static void
lrg_mod_finalize (GObject *object)
{
    LrgMod *self = LRG_MOD (object);
    LrgModPrivate *priv = lrg_mod_get_instance_private (self);

    g_free (priv->base_path);
    g_free (priv->data_path);
    g_free (priv->error_message);

    G_OBJECT_CLASS (lrg_mod_parent_class)->finalize (object);
}

static void
lrg_mod_class_init (LrgModClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->dispose = lrg_mod_dispose;
    object_class->finalize = lrg_mod_finalize;
    object_class->get_property = lrg_mod_get_property;
    object_class->set_property = lrg_mod_set_property;

    /* Virtual methods */
    klass->load = lrg_mod_real_load;
    klass->unload = lrg_mod_real_unload;
    klass->can_load = lrg_mod_real_can_load;
    klass->get_display_info = lrg_mod_real_get_display_info;

    /**
     * LrgMod:manifest:
     *
     * The mod manifest containing metadata.
     *
     * Since: 1.0
     */
    properties[PROP_MANIFEST] =
        g_param_spec_object ("manifest",
                             "Manifest",
                             "The mod manifest",
                             LRG_TYPE_MOD_MANIFEST,
                             G_PARAM_READWRITE |
                             G_PARAM_CONSTRUCT |
                             G_PARAM_STATIC_STRINGS);

    /**
     * LrgMod:base-path:
     *
     * The base filesystem path for the mod.
     *
     * Since: 1.0
     */
    properties[PROP_BASE_PATH] =
        g_param_spec_string ("base-path",
                             "Base Path",
                             "Filesystem path to mod root",
                             NULL,
                             G_PARAM_READWRITE |
                             G_PARAM_CONSTRUCT |
                             G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);

    /**
     * LrgMod::prepare-scripting:
     * @self: the #LrgMod
     * @scripting: the freshly created #LrgScripting context
     *
     * Emitted for script mods after the scripting context is created and
     * before the entry point is loaded.  Hosts register the functions and
     * globals the mod may use here, so they exist while the entry file runs.
     *
     * Since: 0.2
     */
    signals[SIGNAL_PREPARE_SCRIPTING] =
        g_signal_new ("prepare-scripting",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0, NULL, NULL, NULL,
                      G_TYPE_NONE, 1, LRG_TYPE_SCRIPTING);
}

static void
lrg_mod_init (LrgMod *self)
{
    LrgModPrivate *priv = lrg_mod_get_instance_private (self);

    priv->state = LRG_MOD_STATE_UNLOADED;
    priv->enabled = TRUE;
}

/* ==========================================================================
 * Construction
 * ========================================================================== */

LrgMod *
lrg_mod_new (LrgModManifest *manifest,
             const gchar    *base_path)
{
    LrgMod *mod;
    LrgModPrivate *priv;

    g_return_val_if_fail (LRG_IS_MOD_MANIFEST (manifest), NULL);
    g_return_val_if_fail (base_path != NULL, NULL);

    mod = g_object_new (LRG_TYPE_MOD,
                        "manifest", manifest,
                        "base-path", base_path,
                        NULL);

    priv = lrg_mod_get_instance_private (mod);
    priv->state = LRG_MOD_STATE_DISCOVERED;

    lrg_debug (LRG_LOG_DOMAIN_MOD, "Created mod: %s at %s",
               lrg_mod_manifest_get_id (manifest), base_path);

    return mod;
}

/* ==========================================================================
 * Properties
 * ========================================================================== */

LrgModManifest *
lrg_mod_get_manifest (LrgMod *self)
{
    LrgModPrivate *priv;

    g_return_val_if_fail (LRG_IS_MOD (self), NULL);

    priv = lrg_mod_get_instance_private (self);
    return priv->manifest;
}

const gchar *
lrg_mod_get_id (LrgMod *self)
{
    LrgModPrivate *priv;

    g_return_val_if_fail (LRG_IS_MOD (self), NULL);

    priv = lrg_mod_get_instance_private (self);
    return lrg_mod_manifest_get_id (priv->manifest);
}

const gchar *
lrg_mod_get_base_path (LrgMod *self)
{
    LrgModPrivate *priv;

    g_return_val_if_fail (LRG_IS_MOD (self), NULL);

    priv = lrg_mod_get_instance_private (self);
    return priv->base_path;
}

const gchar *
lrg_mod_get_data_path (LrgMod *self)
{
    LrgModPrivate *priv;

    g_return_val_if_fail (LRG_IS_MOD (self), NULL);

    priv = lrg_mod_get_instance_private (self);
    return priv->data_path;
}

/* ==========================================================================
 * State
 * ========================================================================== */

LrgModState
lrg_mod_get_state (LrgMod *self)
{
    LrgModPrivate *priv;

    g_return_val_if_fail (LRG_IS_MOD (self), LRG_MOD_STATE_UNLOADED);

    priv = lrg_mod_get_instance_private (self);
    return priv->state;
}

gboolean
lrg_mod_is_loaded (LrgMod *self)
{
    LrgModPrivate *priv;

    g_return_val_if_fail (LRG_IS_MOD (self), FALSE);

    priv = lrg_mod_get_instance_private (self);
    return priv->state == LRG_MOD_STATE_LOADED;
}

gboolean
lrg_mod_is_enabled (LrgMod *self)
{
    LrgModPrivate *priv;

    g_return_val_if_fail (LRG_IS_MOD (self), FALSE);

    priv = lrg_mod_get_instance_private (self);
    return priv->enabled;
}

void
lrg_mod_set_enabled (LrgMod   *self,
                     gboolean  enabled)
{
    LrgModPrivate *priv;

    g_return_if_fail (LRG_IS_MOD (self));

    priv = lrg_mod_get_instance_private (self);

    if (priv->enabled == enabled)
        return;

    priv->enabled = enabled;

    if (!enabled && priv->state == LRG_MOD_STATE_LOADED)
    {
        priv->state = LRG_MOD_STATE_DISABLED;
        lrg_debug (LRG_LOG_DOMAIN_MOD, "Mod disabled: %s", lrg_mod_get_id (self));
    }
    else if (enabled && priv->state == LRG_MOD_STATE_DISABLED)
    {
        priv->state = LRG_MOD_STATE_LOADED;
        lrg_debug (LRG_LOG_DOMAIN_MOD, "Mod enabled: %s", lrg_mod_get_id (self));
    }
}

const gchar *
lrg_mod_get_error (LrgMod *self)
{
    LrgModPrivate *priv;

    g_return_val_if_fail (LRG_IS_MOD (self), NULL);

    priv = lrg_mod_get_instance_private (self);
    return priv->error_message;
}

/* ==========================================================================
 * Loading
 * ========================================================================== */

static gboolean
load_data_mod (LrgMod        *self,
               LrgModPrivate *priv,
               GError       **error)
{
    GFile *data_dir;
    gboolean exists;

    /* If no data_path specified, nothing to load */
    if (priv->data_path == NULL)
        return TRUE;

    /* Verify the data directory exists */
    data_dir = g_file_new_for_path (priv->data_path);
    exists = g_file_query_exists (data_dir, NULL);
    g_object_unref (data_dir);

    if (!exists)
    {
        /* Data directory specified but doesn't exist - log debug message */
        lrg_debug (LRG_LOG_DOMAIN_MOD,
                   "Mod data directory does not exist: %s", priv->data_path);
    }

    return TRUE;
}

/* Drops a script mod's context, calling its optional shutdown hook first */
static void
unload_script_mod (LrgMod        *self,
                   LrgModPrivate *priv)
{
    g_autoptr(GError) error = NULL;

    if (priv->scripting == NULL)
        return;

    if (priv->state == LRG_MOD_STATE_LOADED &&
        lrg_scripting_has_function (priv->scripting, LRG_MOD_HOOK_SHUTDOWN) &&
        !lrg_scripting_call_function (priv->scripting, LRG_MOD_HOOK_SHUTDOWN,
                                      NULL, 0, NULL, &error))
    {
        lrg_info (LRG_LOG_DOMAIN_MOD, "Mod %s shutdown hook failed: %s",
                  lrg_mod_get_id (self), error->message);
    }

    lrg_scripting_reset (priv->scripting);
    g_clear_object (&priv->scripting);
}

/*
 * Script mods: the entry point's extension picks the backend (lua, py, js,
 * c, or a native module).  The context searches the mod folder, hosts get
 * prepare-scripting to publish their API, then the entry file runs and an
 * optional lrg_mod_init() is called.
 */
static gboolean
load_script_mod (LrgMod        *self,
                 LrgModPrivate *priv,
                 GError       **error)
{
    LrgScriptingManager *manager;
    LrgScriptLanguage    language;
    const gchar         *entry_point;
    g_autofree gchar    *entry_path = NULL;
    g_autoptr(GError)    local_error = NULL;

    entry_point = lrg_mod_manifest_get_entry_point (priv->manifest);
    if (entry_point == NULL)
    {
        g_set_error (error, LRG_MOD_ERROR, LRG_MOD_ERROR_LOAD_FAILED,
                     "Script mod has no entry_point: %s", lrg_mod_get_id (self));
        return FALSE;
    }

    entry_path = g_build_filename (priv->base_path, entry_point, NULL);
    if (!g_file_test (entry_path, G_FILE_TEST_IS_REGULAR))
    {
        g_set_error (error, LRG_MOD_ERROR, LRG_MOD_ERROR_LOAD_FAILED,
                     "Entry point not found: %s", entry_path);
        return FALSE;
    }

    /* Pick the backend from the file type */
    manager = lrg_scripting_manager_get_default ();
    language = lrg_scripting_manager_language_for_path (manager, entry_path);
    if (language == LRG_SCRIPT_LANGUAGE_NONE)
    {
        g_set_error (error, LRG_MOD_ERROR, LRG_MOD_ERROR_LOAD_FAILED,
                     "Unknown script type for entry point: %s", entry_point);
        return FALSE;
    }
    if (!lrg_scripting_manager_is_available (manager, language))
    {
        g_set_error (error, LRG_MOD_ERROR, LRG_MOD_ERROR_LOAD_FAILED,
                     "%s scripting is not available in this build",
                     lrg_scripting_manager_get_display_name (manager, language));
        return FALSE;
    }

    priv->scripting = lrg_scripting_manager_create_context (manager, language);
    if (priv->scripting == NULL)
    {
        g_set_error (error, LRG_MOD_ERROR, LRG_MOD_ERROR_LOAD_FAILED,
                     "Could not create a %s context",
                     lrg_scripting_manager_get_display_name (manager, language));
        return FALSE;
    }

    /* Imports and includes resolve inside the mod folder */
    lrg_scripting_add_search_path (priv->scripting, priv->base_path);

    /* Hosts publish their API before any mod code runs */
    g_signal_emit (self, signals[SIGNAL_PREPARE_SCRIPTING], 0, priv->scripting);

    if (!lrg_scripting_load_file (priv->scripting, entry_path, &local_error) ||
        (lrg_scripting_has_function (priv->scripting, LRG_MOD_HOOK_INIT) &&
         !lrg_scripting_call_function (priv->scripting, LRG_MOD_HOOK_INIT,
                                       NULL, 0, NULL, &local_error)))
    {
        g_set_error (error, LRG_MOD_ERROR, LRG_MOD_ERROR_LOAD_FAILED,
                     "%s", local_error != NULL ? local_error->message : "script failed");
        lrg_scripting_reset (priv->scripting);
        g_clear_object (&priv->scripting);
        return FALSE;
    }

    lrg_debug (LRG_LOG_DOMAIN_MOD, "Loaded script mod %s from %s",
               lrg_mod_get_id (self), entry_path);
    return TRUE;
}

static gboolean
load_native_mod (LrgMod        *self,
                 LrgModPrivate *priv,
                 GError       **error)
{
    const gchar *entry_point;
    g_autofree gchar *module_path = NULL;
    gpointer init_func;

    entry_point = lrg_mod_manifest_get_entry_point (priv->manifest);
    if (entry_point == NULL)
    {
        g_set_error (error, LRG_MOD_ERROR, LRG_MOD_ERROR_LOAD_FAILED,
                     "Native mod has no entry_point: %s", lrg_mod_get_id (self));
        return FALSE;
    }

    module_path = g_build_filename (priv->base_path, entry_point, NULL);

    priv->module = g_module_open (module_path, G_MODULE_BIND_LAZY);
    if (priv->module == NULL)
    {
        g_set_error (error, LRG_MOD_ERROR, LRG_MOD_ERROR_LOAD_FAILED,
                     "Failed to load native module: %s", g_module_error ());
        return FALSE;
    }

    /* Look for lrg_mod_init symbol */
    if (g_module_symbol (priv->module, "lrg_mod_init", &init_func))
    {
        typedef gboolean (*LrgModInitFunc) (LrgMod *mod, gpointer *user_data);
        LrgModInitFunc init = (LrgModInitFunc)init_func;

        if (!init (self, &priv->user_data))
        {
            g_set_error (error, LRG_MOD_ERROR, LRG_MOD_ERROR_LOAD_FAILED,
                         "Mod init function returned FALSE: %s", lrg_mod_get_id (self));
            g_module_close (priv->module);
            priv->module = NULL;
            return FALSE;
        }
    }

    lrg_debug (LRG_LOG_DOMAIN_MOD, "Loaded native module: %s", module_path);

    return TRUE;
}

/* Virtual method implementation for load */
static gboolean
lrg_mod_real_load (LrgMod   *self,
                   GError  **error)
{
    LrgModPrivate *priv = lrg_mod_get_instance_private (self);
    LrgModType type;
    gboolean success;

    if (priv->state == LRG_MOD_STATE_LOADED)
        return TRUE;

    if (!priv->enabled)
    {
        priv->state = LRG_MOD_STATE_DISABLED;
        return TRUE;
    }

    priv->state = LRG_MOD_STATE_LOADING;
    g_clear_pointer (&priv->error_message, g_free);

    type = lrg_mod_manifest_get_mod_type (priv->manifest);

    switch (type)
    {
    case LRG_MOD_TYPE_DATA:
        success = load_data_mod (self, priv, error);
        break;

    case LRG_MOD_TYPE_SCRIPT:
        success = load_script_mod (self, priv, error);
        break;

    case LRG_MOD_TYPE_NATIVE:
        success = load_native_mod (self, priv, error);
        break;

    default:
        g_set_error (error, LRG_MOD_ERROR, LRG_MOD_ERROR_LOAD_FAILED,
                     "Unknown mod type: %d", type);
        success = FALSE;
        break;
    }

    if (success)
    {
        priv->state = LRG_MOD_STATE_LOADED;
        lrg_info (LRG_LOG_DOMAIN_MOD, "Loaded mod: %s", lrg_mod_get_id (self));
    }
    else
    {
        priv->state = LRG_MOD_STATE_FAILED;
        if (error != NULL && *error != NULL)
            priv->error_message = g_strdup ((*error)->message);
        /* An expected mod error (bad script): reported, never fatal in tests */
        lrg_info (LRG_LOG_DOMAIN_MOD, "Failed to load mod: %s - %s",
                     lrg_mod_get_id (self), priv->error_message);
    }

    return success;
}

/*
 * lrg_mod_load:
 * @self: a #LrgMod
 * @error: (nullable): return location for error
 *
 * Loads the mod. Calls the virtual load method which can be
 * overridden by subclasses.
 *
 * Returns: %TRUE on success
 */
gboolean
lrg_mod_load (LrgMod   *self,
              GError  **error)
{
    LrgModClass *klass;

    g_return_val_if_fail (LRG_IS_MOD (self), FALSE);

    klass = LRG_MOD_GET_CLASS (self);
    if (klass->load != NULL)
        return klass->load (self, error);

    return TRUE;
}

/* Virtual method implementation for unload */
static void
lrg_mod_real_unload (LrgMod *self)
{
    LrgModPrivate *priv = lrg_mod_get_instance_private (self);

    if (priv->state != LRG_MOD_STATE_LOADED &&
        priv->state != LRG_MOD_STATE_DISABLED)
        return;

    /* For native mods, call shutdown and close module */
    if (priv->module != NULL)
    {
        gpointer shutdown_func;

        if (g_module_symbol (priv->module, "lrg_mod_shutdown", &shutdown_func))
        {
            typedef void (*LrgModShutdownFunc) (LrgMod *mod, gpointer user_data);
            LrgModShutdownFunc shutdown = (LrgModShutdownFunc)shutdown_func;
            shutdown (self, priv->user_data);
        }

        g_module_close (priv->module);
        priv->module = NULL;
        priv->user_data = NULL;
    }

    /* For script mods, run the shutdown hook and drop the context */
    unload_script_mod (self, priv);

    priv->state = LRG_MOD_STATE_UNLOADED;
    lrg_info (LRG_LOG_DOMAIN_MOD, "Unloaded mod: %s", lrg_mod_get_id (self));
}

/*
 * lrg_mod_unload:
 * @self: a #LrgMod
 *
 * Unloads the mod. Calls the virtual unload method which can be
 * overridden by subclasses.
 */
void
lrg_mod_unload (LrgMod *self)
{
    LrgModClass *klass;

    g_return_if_fail (LRG_IS_MOD (self));

    klass = LRG_MOD_GET_CLASS (self);
    if (klass->unload != NULL)
        klass->unload (self);
}

/* ==========================================================================
 * Resources
 * ========================================================================== */

gchar *
lrg_mod_resolve_path (LrgMod      *self,
                      const gchar *relative_path)
{
    LrgModPrivate *priv;

    g_return_val_if_fail (LRG_IS_MOD (self), NULL);
    g_return_val_if_fail (relative_path != NULL, NULL);

    priv = lrg_mod_get_instance_private (self);

    if (priv->data_path == NULL)
        return NULL;

    return g_build_filename (priv->data_path, relative_path, NULL);
}

GPtrArray *
lrg_mod_list_files (LrgMod      *self,
                    const gchar *subdir,
                    const gchar *pattern)
{
    LrgModPrivate *priv;
    GPtrArray *files;
    g_autofree gchar *search_path = NULL;
    GDir *dir;
    const gchar *name;

    g_return_val_if_fail (LRG_IS_MOD (self), NULL);

    priv = lrg_mod_get_instance_private (self);
    files = g_ptr_array_new_with_free_func (g_free);

    if (priv->data_path == NULL)
        return files;

    if (subdir != NULL)
        search_path = g_build_filename (priv->data_path, subdir, NULL);
    else
        search_path = g_strdup (priv->data_path);

    dir = g_dir_open (search_path, 0, NULL);
    if (dir == NULL)
        return files;

    while ((name = g_dir_read_name (dir)) != NULL)
    {
        gboolean match;

        match = TRUE;
        if (pattern != NULL)
            match = g_pattern_match_simple (pattern, name);

        if (match)
            g_ptr_array_add (files, g_build_filename (search_path, name, NULL));
    }

    g_dir_close (dir);

    return files;
}

/* ==========================================================================
 * Additional Virtual Method Implementations
 * ========================================================================== */

/* Virtual method implementation for can_load */
static gboolean
lrg_mod_real_can_load (LrgMod   *self,
                       GError  **error)
{
    LrgModPrivate *priv = lrg_mod_get_instance_private (self);

    /* Default implementation: check if mod is enabled and not already loaded */
    if (!priv->enabled)
    {
        g_set_error (error, LRG_MOD_ERROR, LRG_MOD_ERROR_LOAD_FAILED,
                     "Mod is disabled: %s", lrg_mod_get_id (self));
        return FALSE;
    }

    if (priv->state == LRG_MOD_STATE_LOADED)
    {
        /* Already loaded is not an error, just return TRUE */
        return TRUE;
    }

    if (priv->state == LRG_MOD_STATE_FAILED)
    {
        g_set_error (error, LRG_MOD_ERROR, LRG_MOD_ERROR_LOAD_FAILED,
                     "Mod previously failed to load: %s", lrg_mod_get_id (self));
        return FALSE;
    }

    return TRUE;
}

/* Virtual method implementation for get_display_info */
static gchar *
lrg_mod_real_get_display_info (LrgMod *self)
{
    LrgModPrivate *priv = lrg_mod_get_instance_private (self);
    const gchar *name;
    const gchar *version;

    name = lrg_mod_manifest_get_name (priv->manifest);
    version = lrg_mod_manifest_get_version (priv->manifest);

    if (name == NULL)
        name = lrg_mod_manifest_get_id (priv->manifest);

    if (version != NULL)
        return g_strdup_printf ("%s v%s", name, version);
    else
        return g_strdup (name);
}

/*
 * lrg_mod_can_load:
 * @self: a #LrgMod
 * @error: (nullable): return location for error
 *
 * Checks if the mod can be loaded. Subclasses can override this
 * to add additional validation (e.g., DLC ownership verification).
 *
 * Returns: %TRUE if the mod can be loaded
 */
gboolean
lrg_mod_can_load (LrgMod   *self,
                  GError  **error)
{
    LrgModClass *klass;

    g_return_val_if_fail (LRG_IS_MOD (self), FALSE);

    klass = LRG_MOD_GET_CLASS (self);
    if (klass->can_load != NULL)
        return klass->can_load (self, error);

    return TRUE;
}

/*
 * lrg_mod_get_display_info:
 * @self: a #LrgMod
 *
 * Gets a human-readable display string for the mod.
 * Subclasses can override this to provide additional information.
 *
 * Returns: (transfer full): a display string, free with g_free()
 */
gchar *
lrg_mod_get_display_info (LrgMod *self)
{
    LrgModClass *klass;

    g_return_val_if_fail (LRG_IS_MOD (self), NULL);

    klass = LRG_MOD_GET_CLASS (self);
    if (klass->get_display_info != NULL)
        return klass->get_display_info (self);

    return g_strdup (lrg_mod_get_id (self));
}

/* ==========================================================================
 * Scripting and failure state
 * ========================================================================== */

/*
 * lrg_mod_get_scripting:
 * @self: a #LrgMod
 *
 * Gets the scripting context of a loaded script mod.
 *
 * Returns: (transfer none) (nullable): the context, or %NULL when the mod
 *   is not a loaded script mod
 */
LrgScripting *
lrg_mod_get_scripting (LrgMod *self)
{
    LrgModPrivate *priv;

    g_return_val_if_fail (LRG_IS_MOD (self), NULL);

    priv = lrg_mod_get_instance_private (self);
    return priv->scripting;
}

/*
 * lrg_mod_mark_failed:
 * @self: a #LrgMod
 * @message: why the mod failed
 *
 * Unloads the mod if it is loaded and records it as failed with @message.
 * Hosts use this when a loaded mod misbehaves at runtime (for example after
 * repeated script errors) or when a dependency could not be satisfied.
 *
 * Unloading resets the mod's scripting context (closing a native module,
 * tearing down a Lua state).  Never call this while the mod's own code is
 * on the stack, for example from a host function the mod just called:
 * record the failure and unload once that call has returned.  The same
 * applies to lrg_mod_manager_unload_mod() and lrg_mod_manager_reload_mod().
 */
void
lrg_mod_mark_failed (LrgMod      *self,
                     const gchar *message)
{
    LrgModPrivate *priv;

    g_return_if_fail (LRG_IS_MOD (self));
    g_return_if_fail (message != NULL);

    priv = lrg_mod_get_instance_private (self);

    if (priv->state == LRG_MOD_STATE_LOADED)
        lrg_mod_unload (self);

    priv->state = LRG_MOD_STATE_FAILED;
    g_free (priv->error_message);
    priv->error_message = g_strdup (message);
}
