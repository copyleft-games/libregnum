/* lrg-demo-manager.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Implementation of the demo mode manager.
 */

#include "lrg-demo-manager.h"
#include <gio/gio.h>

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_DEMO
#include "../lrg-log.h"

/**
 * SECTION:lrg-demo-manager
 * @title: LrgDemoManager
 * @short_description: Demo mode management
 *
 * The #LrgDemoManager provides centralized control over demo mode
 * functionality including content gating, time limits, and save
 * file management.
 *
 * ## Signals
 *
 * - **demo-ended**: Emitted when the demo session ends
 * - **time-warning**: Emitted when time is running low
 * - **content-blocked**: Emitted when gated content is accessed
 *
 * ## Example Usage
 *
 * ```c
 * LrgDemoManager *demo = lrg_demo_manager_get_default ();
 *
 * // Configure demo
 * lrg_demo_manager_set_demo_mode (demo, TRUE);
 * lrg_demo_manager_set_time_limit (demo, 1800.0f);  // 30 minutes
 * lrg_demo_manager_set_warning_times (demo, (gfloat[]){300, 60}, 2);
 * lrg_demo_manager_set_purchase_url (demo, "https://store.example.com/game");
 *
 * // Gate content
 * lrg_demo_manager_gate_content (demo, "level-5");
 * lrg_demo_manager_gate_content (demo, "boss-final");
 *
 * // Start demo session
 * lrg_demo_manager_start (demo);
 *
 * // In game loop
 * lrg_demo_manager_update (demo, delta_time);
 * ```
 *
 * Since: 1.0
 */

/* ==========================================================================
 * Type Definition
 * ========================================================================== */

struct _LrgDemoManager
{
    GObject parent_instance;

    /* Demo state */
    gboolean is_demo_mode;
    gboolean is_running;
    gfloat time_elapsed;
    gfloat time_limit;

    /* Warning times (sorted descending) */
    GArray *warning_times;
    guint next_warning_index;

    /* Gated content IDs */
    GHashTable *gated_content;

    /* Demo saves */
    GHashTable *demo_saves;

    /* Purchase URL */
    gchar *purchase_url;

    /* Upgrade check */
    gboolean (*upgrade_check_func) (gpointer);
    gpointer upgrade_check_data;
};

G_DEFINE_TYPE (LrgDemoManager, lrg_demo_manager, G_TYPE_OBJECT)

/* Singleton instance */
static LrgDemoManager *default_manager = NULL;

/* ==========================================================================
 * Signals
 * ========================================================================== */

enum
{
    SIGNAL_DEMO_ENDED,
    SIGNAL_TIME_WARNING,
    SIGNAL_CONTENT_BLOCKED,
    N_SIGNALS
};

static guint signals[N_SIGNALS];

/* ==========================================================================
 * Properties
 * ========================================================================== */

enum
{
    PROP_0,
    PROP_DEMO_MODE,
    PROP_TIME_LIMIT,
    PROP_TIME_ELAPSED,
    PROP_TIME_REMAINING,
    PROP_PURCHASE_URL,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

/* ==========================================================================
 * GObject Implementation
 * ========================================================================== */

static void
lrg_demo_manager_get_property (GObject    *object,
                               guint       prop_id,
                               GValue     *value,
                               GParamSpec *pspec)
{
    LrgDemoManager *self = LRG_DEMO_MANAGER (object);

    switch (prop_id)
    {
    case PROP_DEMO_MODE:
        g_value_set_boolean (value, self->is_demo_mode);
        break;
    case PROP_TIME_LIMIT:
        g_value_set_float (value, self->time_limit);
        break;
    case PROP_TIME_ELAPSED:
        g_value_set_float (value, self->time_elapsed);
        break;
    case PROP_TIME_REMAINING:
        g_value_set_float (value, lrg_demo_manager_get_time_remaining (self));
        break;
    case PROP_PURCHASE_URL:
        g_value_set_string (value, self->purchase_url);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_demo_manager_set_property (GObject      *object,
                               guint         prop_id,
                               const GValue *value,
                               GParamSpec   *pspec)
{
    LrgDemoManager *self = LRG_DEMO_MANAGER (object);

    switch (prop_id)
    {
    case PROP_DEMO_MODE:
        lrg_demo_manager_set_demo_mode (self, g_value_get_boolean (value));
        break;
    case PROP_TIME_LIMIT:
        lrg_demo_manager_set_time_limit (self, g_value_get_float (value));
        break;
    case PROP_PURCHASE_URL:
        lrg_demo_manager_set_purchase_url (self, g_value_get_string (value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_demo_manager_finalize (GObject *object)
{
    LrgDemoManager *self = LRG_DEMO_MANAGER (object);

    g_clear_pointer (&self->warning_times, g_array_unref);
    g_clear_pointer (&self->gated_content, g_hash_table_unref);
    g_clear_pointer (&self->demo_saves, g_hash_table_unref);
    g_clear_pointer (&self->purchase_url, g_free);

    G_OBJECT_CLASS (lrg_demo_manager_parent_class)->finalize (object);
}

static void
lrg_demo_manager_class_init (LrgDemoManagerClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->get_property = lrg_demo_manager_get_property;
    object_class->set_property = lrg_demo_manager_set_property;
    object_class->finalize = lrg_demo_manager_finalize;

    /* Properties */
    properties[PROP_DEMO_MODE] =
        g_param_spec_boolean ("demo-mode",
                              "Demo Mode",
                              "Whether demo mode is enabled",
                              FALSE,
                              G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_TIME_LIMIT] =
        g_param_spec_float ("time-limit",
                            "Time Limit",
                            "Demo time limit in seconds (0 = unlimited)",
                            0.0f, G_MAXFLOAT, 0.0f,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_TIME_ELAPSED] =
        g_param_spec_float ("time-elapsed",
                            "Time Elapsed",
                            "Time elapsed in current demo session",
                            0.0f, G_MAXFLOAT, 0.0f,
                            G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

    properties[PROP_TIME_REMAINING] =
        g_param_spec_float ("time-remaining",
                            "Time Remaining",
                            "Time remaining in demo (-1 if no limit)",
                            -1.0f, G_MAXFLOAT, -1.0f,
                            G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

    properties[PROP_PURCHASE_URL] =
        g_param_spec_string ("purchase-url",
                             "Purchase URL",
                             "URL for purchasing the full game",
                             NULL,
                             G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);

    /* Signals */

    /**
     * LrgDemoManager::demo-ended:
     * @self: the demo manager
     * @reason: the #LrgDemoEndReason
     *
     * Emitted when the demo session ends.
     */
    signals[SIGNAL_DEMO_ENDED] =
        g_signal_new ("demo-ended",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0, NULL, NULL, NULL,
                      G_TYPE_NONE, 1,
                      LRG_TYPE_DEMO_END_REASON);

    /**
     * LrgDemoManager::time-warning:
     * @self: the demo manager
     * @seconds_remaining: seconds remaining
     *
     * Emitted when a time warning threshold is crossed.
     */
    signals[SIGNAL_TIME_WARNING] =
        g_signal_new ("time-warning",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0, NULL, NULL, NULL,
                      G_TYPE_NONE, 1,
                      G_TYPE_FLOAT);

    /**
     * LrgDemoManager::content-blocked:
     * @self: the demo manager
     * @content_id: the blocked content ID
     * @unlock_message: message for the user
     *
     * Emitted when access to gated content is denied.
     */
    signals[SIGNAL_CONTENT_BLOCKED] =
        g_signal_new ("content-blocked",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0, NULL, NULL, NULL,
                      G_TYPE_NONE, 2,
                      G_TYPE_STRING,
                      G_TYPE_STRING);
}

static void
lrg_demo_manager_init (LrgDemoManager *self)
{
    self->is_demo_mode = FALSE;
    self->is_running = FALSE;
    self->time_elapsed = 0.0f;
    self->time_limit = 0.0f;
    self->next_warning_index = 0;

    self->warning_times = g_array_new (FALSE, FALSE, sizeof (gfloat));
    self->gated_content = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
    self->demo_saves = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);

    self->purchase_url = NULL;
    self->upgrade_check_func = NULL;
    self->upgrade_check_data = NULL;
}

/* ==========================================================================
 * Constructor
 * ========================================================================== */

/**
 * lrg_demo_manager_new:
 *
 * Creates a new demo manager.
 *
 * Returns: (transfer full): a new #LrgDemoManager
 *
 * Since: 1.0
 */
LrgDemoManager *
lrg_demo_manager_new (void)
{
    return g_object_new (LRG_TYPE_DEMO_MANAGER, NULL);
}

/**
 * lrg_demo_manager_get_default:
 *
 * Gets the default demo manager instance.
 *
 * Returns: (transfer none): the default #LrgDemoManager
 *
 * Since: 1.0
 */
LrgDemoManager *
lrg_demo_manager_get_default (void)
{
    if (default_manager == NULL)
        default_manager = lrg_demo_manager_new ();

    return default_manager;
}

/* ==========================================================================
 * Demo Mode State
 * ========================================================================== */

/**
 * lrg_demo_manager_set_demo_mode:
 * @self: a #LrgDemoManager
 * @is_demo: whether to enable demo mode
 *
 * Sets whether the application is running in demo mode.
 *
 * Since: 1.0
 */
void
lrg_demo_manager_set_demo_mode (LrgDemoManager *self,
                                gboolean        is_demo)
{
    g_return_if_fail (LRG_IS_DEMO_MANAGER (self));

    if (self->is_demo_mode != is_demo)
    {
        self->is_demo_mode = is_demo;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_DEMO_MODE]);

        lrg_debug (LRG_LOG_DOMAIN, "Demo mode %s", is_demo ? "enabled" : "disabled");
    }
}

/**
 * lrg_demo_manager_get_demo_mode:
 * @self: a #LrgDemoManager
 *
 * Gets whether the application is running in demo mode.
 *
 * Returns: %TRUE if in demo mode
 *
 * Since: 1.0
 */
gboolean
lrg_demo_manager_get_demo_mode (LrgDemoManager *self)
{
    g_return_val_if_fail (LRG_IS_DEMO_MANAGER (self), FALSE);

    return self->is_demo_mode;
}

/**
 * lrg_demo_manager_start:
 * @self: a #LrgDemoManager
 *
 * Starts the demo session.
 *
 * Since: 1.0
 */
void
lrg_demo_manager_start (LrgDemoManager *self)
{
    g_return_if_fail (LRG_IS_DEMO_MANAGER (self));

    self->is_running = TRUE;
    self->time_elapsed = 0.0f;
    self->next_warning_index = 0;

    lrg_info (LRG_LOG_DOMAIN, "Demo session started");
}

/**
 * lrg_demo_manager_stop:
 * @self: a #LrgDemoManager
 * @reason: the reason for stopping
 *
 * Stops the demo session.
 *
 * Since: 1.0
 */
void
lrg_demo_manager_stop (LrgDemoManager   *self,
                       LrgDemoEndReason  reason)
{
    g_return_if_fail (LRG_IS_DEMO_MANAGER (self));

    if (!self->is_running)
        return;

    self->is_running = FALSE;

    lrg_info (LRG_LOG_DOMAIN, "Demo session ended (reason: %d, elapsed: %.1fs)",
              reason, self->time_elapsed);

    g_signal_emit (self, signals[SIGNAL_DEMO_ENDED], 0, reason);
}

/**
 * lrg_demo_manager_update:
 * @self: a #LrgDemoManager
 * @delta_time: time since last update in seconds
 *
 * Updates the demo manager.
 *
 * Since: 1.0
 */
void
lrg_demo_manager_update (LrgDemoManager *self,
                         gfloat          delta_time)
{
    gfloat time_remaining;
    gfloat warning_time;

    g_return_if_fail (LRG_IS_DEMO_MANAGER (self));

    if (!self->is_running || !self->is_demo_mode)
        return;

    self->time_elapsed += delta_time;

    /* Check for time limit */
    if (self->time_limit > 0.0f)
    {
        time_remaining = self->time_limit - self->time_elapsed;

        /* Check warnings */
        while (self->next_warning_index < self->warning_times->len)
        {
            warning_time = g_array_index (self->warning_times, gfloat,
                                          self->next_warning_index);
            if (time_remaining <= warning_time)
            {
                g_signal_emit (self, signals[SIGNAL_TIME_WARNING], 0, time_remaining);
                self->next_warning_index++;
            }
            else
            {
                break;
            }
        }

        /* Check time expired */
        if (time_remaining <= 0.0f)
        {
            lrg_demo_manager_stop (self, LRG_DEMO_END_REASON_TIME_LIMIT);
        }
    }
}

/* ==========================================================================
 * Time Limit
 * ========================================================================== */

/**
 * lrg_demo_manager_set_time_limit:
 * @self: a #LrgDemoManager
 * @seconds: time limit in seconds (0 = no limit)
 *
 * Sets the demo time limit in seconds.
 *
 * Since: 1.0
 */
void
lrg_demo_manager_set_time_limit (LrgDemoManager *self,
                                 gfloat          seconds)
{
    g_return_if_fail (LRG_IS_DEMO_MANAGER (self));

    if (self->time_limit != seconds)
    {
        self->time_limit = seconds;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TIME_LIMIT]);
    }
}

/**
 * lrg_demo_manager_get_time_limit:
 * @self: a #LrgDemoManager
 *
 * Gets the demo time limit in seconds.
 *
 * Returns: the time limit, or 0 if no limit
 *
 * Since: 1.0
 */
gfloat
lrg_demo_manager_get_time_limit (LrgDemoManager *self)
{
    g_return_val_if_fail (LRG_IS_DEMO_MANAGER (self), 0.0f);

    return self->time_limit;
}

/**
 * lrg_demo_manager_get_time_remaining:
 * @self: a #LrgDemoManager
 *
 * Gets the remaining demo time in seconds.
 *
 * Returns: remaining time, or -1 if no limit set
 *
 * Since: 1.0
 */
gfloat
lrg_demo_manager_get_time_remaining (LrgDemoManager *self)
{
    gfloat remaining;

    g_return_val_if_fail (LRG_IS_DEMO_MANAGER (self), -1.0f);

    if (self->time_limit <= 0.0f)
        return -1.0f;

    remaining = self->time_limit - self->time_elapsed;
    return remaining > 0.0f ? remaining : 0.0f;
}

/**
 * lrg_demo_manager_get_time_elapsed:
 * @self: a #LrgDemoManager
 *
 * Gets the elapsed demo time in seconds.
 *
 * Returns: elapsed time since demo started
 *
 * Since: 1.0
 */
gfloat
lrg_demo_manager_get_time_elapsed (LrgDemoManager *self)
{
    g_return_val_if_fail (LRG_IS_DEMO_MANAGER (self), 0.0f);

    return self->time_elapsed;
}

/* Compare function for sorting warnings descending */
static gint
compare_float_desc (gconstpointer a,
                    gconstpointer b)
{
    gfloat fa = *(const gfloat *)a;
    gfloat fb = *(const gfloat *)b;

    if (fa > fb) return -1;
    if (fa < fb) return 1;
    return 0;
}

/**
 * lrg_demo_manager_set_warning_times:
 * @self: a #LrgDemoManager
 * @warning_seconds: (array length=n_warnings): array of warning times
 * @n_warnings: number of warning times
 *
 * Sets the times at which to emit time warnings.
 *
 * Since: 1.0
 */
void
lrg_demo_manager_set_warning_times (LrgDemoManager *self,
                                    const gfloat   *warning_seconds,
                                    guint           n_warnings)
{
    guint i;

    g_return_if_fail (LRG_IS_DEMO_MANAGER (self));

    g_array_set_size (self->warning_times, 0);

    if (warning_seconds == NULL || n_warnings == 0)
        return;

    for (i = 0; i < n_warnings; i++)
    {
        g_array_append_val (self->warning_times, warning_seconds[i]);
    }

    /* Sort descending so we check highest first */
    g_array_sort (self->warning_times, compare_float_desc);
    self->next_warning_index = 0;
}

/* ==========================================================================
 * Content Gating
 * ========================================================================== */

/**
 * lrg_demo_manager_gate_content:
 * @self: a #LrgDemoManager
 * @content_id: the content ID to gate
 *
 * Marks content as gated (unavailable in demo mode).
 *
 * Since: 1.0
 */
void
lrg_demo_manager_gate_content (LrgDemoManager *self,
                               const gchar    *content_id)
{
    g_return_if_fail (LRG_IS_DEMO_MANAGER (self));
    g_return_if_fail (content_id != NULL);

    g_hash_table_add (self->gated_content, g_strdup (content_id));
    lrg_debug (LRG_LOG_DOMAIN, "Gated content: %s", content_id);
}

/**
 * lrg_demo_manager_ungate_content:
 * @self: a #LrgDemoManager
 * @content_id: the content ID to ungate
 *
 * Removes content from the gated list.
 *
 * Since: 1.0
 */
void
lrg_demo_manager_ungate_content (LrgDemoManager *self,
                                 const gchar    *content_id)
{
    g_return_if_fail (LRG_IS_DEMO_MANAGER (self));
    g_return_if_fail (content_id != NULL);

    g_hash_table_remove (self->gated_content, content_id);
}

/**
 * lrg_demo_manager_is_content_gated:
 * @self: a #LrgDemoManager
 * @content_id: the content ID to check
 *
 * Checks if content is gated in demo mode.
 *
 * Returns: %TRUE if content is gated
 *
 * Since: 1.0
 */
gboolean
lrg_demo_manager_is_content_gated (LrgDemoManager *self,
                                   const gchar    *content_id)
{
    g_return_val_if_fail (LRG_IS_DEMO_MANAGER (self), FALSE);
    g_return_val_if_fail (content_id != NULL, FALSE);

    return g_hash_table_contains (self->gated_content, content_id);
}

/**
 * lrg_demo_manager_check_access:
 * @self: a #LrgDemoManager
 * @gatable: a #LrgDemoGatable object
 * @error: (optional): return location for error
 *
 * Checks if access to gatable content is allowed.
 *
 * Returns: %TRUE if access is allowed
 *
 * Since: 1.0
 */
gboolean
lrg_demo_manager_check_access (LrgDemoManager  *self,
                               LrgDemoGatable  *gatable,
                               GError         **error)
{
    const gchar *content_id;
    const gchar *unlock_message;

    g_return_val_if_fail (LRG_IS_DEMO_MANAGER (self), TRUE);
    g_return_val_if_fail (LRG_IS_DEMO_GATABLE (gatable), TRUE);

    /* Not in demo mode - allow everything */
    if (!self->is_demo_mode)
        return TRUE;

    content_id = lrg_demo_gatable_get_content_id (gatable);
    if (content_id == NULL)
        return TRUE;

    /* Check if object says it's demo content */
    if (lrg_demo_gatable_is_demo_content (gatable))
        return TRUE;

    /* Check if explicitly gated */
    if (!lrg_demo_manager_is_content_gated (self, content_id))
        return TRUE;

    /* Content is gated */
    unlock_message = lrg_demo_gatable_get_unlock_message (gatable);

    g_signal_emit (self, signals[SIGNAL_CONTENT_BLOCKED], 0,
                   content_id, unlock_message);

    g_set_error (error,
                 LRG_DEMO_ERROR,
                 LRG_DEMO_ERROR_CONTENT_GATED,
                 "Content '%s' is not available in demo mode",
                 content_id);

    return FALSE;
}

/**
 * lrg_demo_manager_get_gated_content:
 * @self: a #LrgDemoManager
 *
 * Gets the list of gated content IDs.
 *
 * Returns: (transfer container) (element-type utf8): array of content IDs
 *
 * Since: 1.0
 */
GPtrArray *
lrg_demo_manager_get_gated_content (LrgDemoManager *self)
{
    GPtrArray *result;
    GHashTableIter iter;
    gpointer key;

    g_return_val_if_fail (LRG_IS_DEMO_MANAGER (self), NULL);

    result = g_ptr_array_new ();

    g_hash_table_iter_init (&iter, self->gated_content);
    while (g_hash_table_iter_next (&iter, &key, NULL))
    {
        g_ptr_array_add (result, key);
    }

    return result;
}

/**
 * lrg_demo_manager_clear_gated_content:
 * @self: a #LrgDemoManager
 *
 * Removes all content from the gated list.
 *
 * Since: 1.0
 */
void
lrg_demo_manager_clear_gated_content (LrgDemoManager *self)
{
    g_return_if_fail (LRG_IS_DEMO_MANAGER (self));

    g_hash_table_remove_all (self->gated_content);
}

/* ==========================================================================
 * Demo Saves
 * ========================================================================== */

/**
 * lrg_demo_manager_mark_save_as_demo:
 * @self: a #LrgDemoManager
 * @save_id: the save identifier
 *
 * Marks a save file as being from demo mode.
 *
 * Since: 1.0
 */
void
lrg_demo_manager_mark_save_as_demo (LrgDemoManager *self,
                                    const gchar    *save_id)
{
    g_return_if_fail (LRG_IS_DEMO_MANAGER (self));
    g_return_if_fail (save_id != NULL);

    g_hash_table_add (self->demo_saves, g_strdup (save_id));
}

/**
 * lrg_demo_manager_is_demo_save:
 * @self: a #LrgDemoManager
 * @save_id: the save identifier
 *
 * Checks if a save file is from demo mode.
 *
 * Returns: %TRUE if save is from demo mode
 *
 * Since: 1.0
 */
gboolean
lrg_demo_manager_is_demo_save (LrgDemoManager *self,
                               const gchar    *save_id)
{
    g_return_val_if_fail (LRG_IS_DEMO_MANAGER (self), FALSE);
    g_return_val_if_fail (save_id != NULL, FALSE);

    return g_hash_table_contains (self->demo_saves, save_id);
}

/**
 * lrg_demo_manager_get_demo_saves:
 * @self: a #LrgDemoManager
 *
 * Gets all save IDs marked as demo saves.
 *
 * Returns: (transfer container) (element-type utf8): array of save IDs
 *
 * Since: 1.0
 */
GPtrArray *
lrg_demo_manager_get_demo_saves (LrgDemoManager *self)
{
    GPtrArray *result;
    GHashTableIter iter;
    gpointer key;

    g_return_val_if_fail (LRG_IS_DEMO_MANAGER (self), NULL);

    result = g_ptr_array_new ();

    g_hash_table_iter_init (&iter, self->demo_saves);
    while (g_hash_table_iter_next (&iter, &key, NULL))
    {
        g_ptr_array_add (result, key);
    }

    return result;
}

/**
 * lrg_demo_manager_convert_demo_save:
 * @self: a #LrgDemoManager
 * @save_id: the save identifier
 *
 * Converts a demo save to a full game save.
 *
 * Since: 1.0
 */
void
lrg_demo_manager_convert_demo_save (LrgDemoManager *self,
                                    const gchar    *save_id)
{
    g_return_if_fail (LRG_IS_DEMO_MANAGER (self));
    g_return_if_fail (save_id != NULL);

    g_hash_table_remove (self->demo_saves, save_id);
    lrg_info (LRG_LOG_DOMAIN, "Converted demo save: %s", save_id);
}

/* ==========================================================================
 * Purchase URL
 * ========================================================================== */

/**
 * lrg_demo_manager_set_purchase_url:
 * @self: a #LrgDemoManager
 * @url: (nullable): the purchase URL
 *
 * Sets the URL to redirect users for purchasing the full game.
 *
 * Since: 1.0
 */
void
lrg_demo_manager_set_purchase_url (LrgDemoManager *self,
                                   const gchar    *url)
{
    g_return_if_fail (LRG_IS_DEMO_MANAGER (self));

    g_free (self->purchase_url);
    self->purchase_url = g_strdup (url);
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_PURCHASE_URL]);
}

/**
 * lrg_demo_manager_get_purchase_url:
 * @self: a #LrgDemoManager
 *
 * Gets the purchase URL.
 *
 * Returns: (transfer none) (nullable): the purchase URL
 *
 * Since: 1.0
 */
const gchar *
lrg_demo_manager_get_purchase_url (LrgDemoManager *self)
{
    g_return_val_if_fail (LRG_IS_DEMO_MANAGER (self), NULL);

    return self->purchase_url;
}

/**
 * lrg_demo_manager_open_purchase_url:
 * @self: a #LrgDemoManager
 * @error: (optional): return location for error
 *
 * Opens the purchase URL in the system browser.
 *
 * Returns: %TRUE if URL was opened successfully
 *
 * Since: 1.0
 */
gboolean
lrg_demo_manager_open_purchase_url (LrgDemoManager  *self,
                                    GError         **error)
{
    g_return_val_if_fail (LRG_IS_DEMO_MANAGER (self), FALSE);

    if (self->purchase_url == NULL || self->purchase_url[0] == '\0')
    {
        g_set_error (error,
                     LRG_DEMO_ERROR,
                     LRG_DEMO_ERROR_FAILED,
                     "No purchase URL configured");
        return FALSE;
    }

    return g_app_info_launch_default_for_uri (self->purchase_url, NULL, error);
}

/* ==========================================================================
 * Upgrade Detection
 * ========================================================================== */

/**
 * lrg_demo_manager_set_upgrade_check_func:
 * @self: a #LrgDemoManager
 * @func: (nullable): function to check for upgrade
 * @user_data: (nullable): data passed to @func
 *
 * Sets a function to check if the user has upgraded to the full version.
 *
 * Since: 1.0
 */
void
lrg_demo_manager_set_upgrade_check_func (LrgDemoManager *self,
                                         gboolean (*func) (gpointer),
                                         gpointer        user_data)
{
    g_return_if_fail (LRG_IS_DEMO_MANAGER (self));

    self->upgrade_check_func = func;
    self->upgrade_check_data = user_data;
}

/**
 * lrg_demo_manager_check_upgrade:
 * @self: a #LrgDemoManager
 *
 * Checks if user has upgraded to the full version.
 *
 * Returns: %TRUE if upgraded
 *
 * Since: 1.0
 */
gboolean
lrg_demo_manager_check_upgrade (LrgDemoManager *self)
{
    gboolean upgraded;

    g_return_val_if_fail (LRG_IS_DEMO_MANAGER (self), FALSE);

    if (self->upgrade_check_func == NULL)
        return FALSE;

    upgraded = self->upgrade_check_func (self->upgrade_check_data);

    if (upgraded)
    {
        lrg_info (LRG_LOG_DOMAIN, "User upgraded to full version");
        lrg_demo_manager_set_demo_mode (self, FALSE);
        lrg_demo_manager_stop (self, LRG_DEMO_END_REASON_UPGRADED);
    }

    return upgraded;
}
