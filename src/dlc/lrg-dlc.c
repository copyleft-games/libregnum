/* lrg-dlc.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * DLC (Downloadable Content) implementation.
 */

#include "lrg-dlc.h"
#include "../steam/lrg-steam-client.h"
#include <gio/gio.h>

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_MOD
#include "../lrg-log.h"

/**
 * SECTION:lrg-dlc
 * @title: LrgDlc
 * @short_description: DLC representation
 *
 * #LrgDlc extends #LrgMod with DLC-specific functionality:
 * - Ownership verification via #LrgDlcOwnership implementations
 * - Store integration (Steam, etc.)
 * - Trial content gating
 *
 * DLC can be in various ownership states:
 * - %LRG_DLC_OWNERSHIP_UNKNOWN: Not yet verified
 * - %LRG_DLC_OWNERSHIP_NOT_OWNED: User doesn't own
 * - %LRG_DLC_OWNERSHIP_OWNED: User owns full access
 * - %LRG_DLC_OWNERSHIP_TRIAL: User has trial access
 * - %LRG_DLC_OWNERSHIP_ERROR: Verification failed
 *
 * Since: 1.0
 */

typedef struct
{
    /* DLC type */
    LrgDlcType dlc_type;

    /* Pricing info */
    gchar *price_string;
    gchar *currency;

    /* Store info */
    guint32 steam_app_id;
    gchar *store_id;

    /* Release info */
    GDateTime *release_date;
    gchar *min_game_version;

    /* Ownership */
    LrgDlcOwnershipState ownership_state;
    LrgDlcOwnership *ownership_checker;

    /* Trial content */
    gboolean trial_enabled;
    GPtrArray *trial_content_ids;
} LrgDlcPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (LrgDlc, lrg_dlc, LRG_TYPE_MOD)

enum
{
    PROP_0,
    PROP_DLC_TYPE,
    PROP_PRICE_STRING,
    PROP_CURRENCY,
    PROP_STEAM_APP_ID,
    PROP_STORE_ID,
    PROP_RELEASE_DATE,
    PROP_MIN_GAME_VERSION,
    PROP_OWNERSHIP_STATE,
    PROP_TRIAL_ENABLED,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

enum
{
    SIGNAL_OWNERSHIP_CHANGED,
    SIGNAL_PURCHASE_PROMPTED,
    N_SIGNALS
};

static guint signals[N_SIGNALS];

/* ==========================================================================
 * Virtual Method Implementations
 * ========================================================================== */

static LrgDlcOwnershipState
lrg_dlc_real_verify_ownership (LrgDlc   *self,
                                GError  **error)
{
    LrgDlcPrivate *priv = lrg_dlc_get_instance_private (self);
    LrgDlcOwnershipState old_state;
    LrgDlcOwnershipState new_state;
    const gchar *dlc_id;

    old_state = priv->ownership_state;
    dlc_id = lrg_mod_get_id (LRG_MOD (self));

    if (priv->ownership_checker == NULL)
    {
        /* No checker - assume owned */
        new_state = LRG_DLC_OWNERSHIP_OWNED;
    }
    else
    {
        g_autoptr(GError) local_error = NULL;

        if (lrg_dlc_ownership_check_ownership (priv->ownership_checker, dlc_id, &local_error))
        {
            new_state = LRG_DLC_OWNERSHIP_OWNED;
        }
        else
        {
            if (local_error != NULL)
            {
                if (g_error_matches (local_error, LRG_DLC_ERROR, LRG_DLC_ERROR_NOT_OWNED))
                {
                    /* Check if trial is available */
                    if (priv->trial_enabled)
                        new_state = LRG_DLC_OWNERSHIP_TRIAL;
                    else
                        new_state = LRG_DLC_OWNERSHIP_NOT_OWNED;
                }
                else
                {
                    new_state = LRG_DLC_OWNERSHIP_ERROR;
                    if (error != NULL)
                        *error = g_error_copy (local_error);
                }
            }
            else
            {
                new_state = LRG_DLC_OWNERSHIP_NOT_OWNED;
            }
        }
    }

    priv->ownership_state = new_state;

    if (old_state != new_state)
    {
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_OWNERSHIP_STATE]);
        g_signal_emit (self, signals[SIGNAL_OWNERSHIP_CHANGED], 0, new_state);
    }

    return new_state;
}

static GPtrArray *
lrg_dlc_real_get_trial_content_ids (LrgDlc *self)
{
    LrgDlcPrivate *priv = lrg_dlc_get_instance_private (self);

    return priv->trial_content_ids;
}

static gchar *
lrg_dlc_real_get_store_url (LrgDlc *self)
{
    LrgDlcPrivate *priv = lrg_dlc_get_instance_private (self);

    /* Generate Steam store URL if we have an app ID */
    if (priv->steam_app_id != 0)
    {
        return g_strdup_printf ("https://store.steampowered.com/app/%u",
                                priv->steam_app_id);
    }

    /* Fall back to store_id if available */
    if (priv->store_id != NULL)
    {
        return g_strdup (priv->store_id);
    }

    return NULL;
}

/* Override LrgMod::can_load to check ownership */
static gboolean
lrg_dlc_can_load (LrgMod   *mod,
                   GError  **error)
{
    LrgDlc *self = LRG_DLC (mod);
    LrgDlcOwnershipState state;

    /* Verify ownership first */
    state = lrg_dlc_verify_ownership (self, error);

    if (state == LRG_DLC_OWNERSHIP_ERROR)
        return FALSE;

    if (state == LRG_DLC_OWNERSHIP_NOT_OWNED)
    {
        g_set_error (error,
                     LRG_DLC_ERROR,
                     LRG_DLC_ERROR_NOT_OWNED,
                     "DLC '%s' is not owned",
                     lrg_mod_get_id (mod));
        return FALSE;
    }

    /* Chain up to parent can_load */
    return LRG_MOD_CLASS (lrg_dlc_parent_class)->can_load (mod, error);
}

/* ==========================================================================
 * GObject Implementation
 * ========================================================================== */

static void
lrg_dlc_get_property (GObject    *object,
                       guint       prop_id,
                       GValue     *value,
                       GParamSpec *pspec)
{
    LrgDlc *self = LRG_DLC (object);
    LrgDlcPrivate *priv = lrg_dlc_get_instance_private (self);

    switch (prop_id)
    {
    case PROP_DLC_TYPE:
        g_value_set_enum (value, priv->dlc_type);
        break;
    case PROP_PRICE_STRING:
        g_value_set_string (value, priv->price_string);
        break;
    case PROP_CURRENCY:
        g_value_set_string (value, priv->currency);
        break;
    case PROP_STEAM_APP_ID:
        g_value_set_uint (value, priv->steam_app_id);
        break;
    case PROP_STORE_ID:
        g_value_set_string (value, priv->store_id);
        break;
    case PROP_RELEASE_DATE:
        g_value_set_boxed (value, priv->release_date);
        break;
    case PROP_MIN_GAME_VERSION:
        g_value_set_string (value, priv->min_game_version);
        break;
    case PROP_OWNERSHIP_STATE:
        g_value_set_enum (value, priv->ownership_state);
        break;
    case PROP_TRIAL_ENABLED:
        g_value_set_boolean (value, priv->trial_enabled);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
lrg_dlc_set_property (GObject      *object,
                       guint         prop_id,
                       const GValue *value,
                       GParamSpec   *pspec)
{
    LrgDlc *self = LRG_DLC (object);
    LrgDlcPrivate *priv = lrg_dlc_get_instance_private (self);

    switch (prop_id)
    {
    case PROP_DLC_TYPE:
        priv->dlc_type = g_value_get_enum (value);
        break;
    case PROP_PRICE_STRING:
        g_free (priv->price_string);
        priv->price_string = g_value_dup_string (value);
        break;
    case PROP_CURRENCY:
        g_free (priv->currency);
        priv->currency = g_value_dup_string (value);
        break;
    case PROP_STEAM_APP_ID:
        priv->steam_app_id = g_value_get_uint (value);
        break;
    case PROP_STORE_ID:
        g_free (priv->store_id);
        priv->store_id = g_value_dup_string (value);
        break;
    case PROP_RELEASE_DATE:
        g_clear_pointer (&priv->release_date, g_date_time_unref);
        priv->release_date = g_value_dup_boxed (value);
        break;
    case PROP_MIN_GAME_VERSION:
        g_free (priv->min_game_version);
        priv->min_game_version = g_value_dup_string (value);
        break;
    case PROP_TRIAL_ENABLED:
        priv->trial_enabled = g_value_get_boolean (value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
lrg_dlc_dispose (GObject *object)
{
    LrgDlc *self = LRG_DLC (object);
    LrgDlcPrivate *priv = lrg_dlc_get_instance_private (self);

    g_clear_object (&priv->ownership_checker);
    g_clear_pointer (&priv->release_date, g_date_time_unref);
    g_clear_pointer (&priv->trial_content_ids, g_ptr_array_unref);

    G_OBJECT_CLASS (lrg_dlc_parent_class)->dispose (object);
}

static void
lrg_dlc_finalize (GObject *object)
{
    LrgDlc *self = LRG_DLC (object);
    LrgDlcPrivate *priv = lrg_dlc_get_instance_private (self);

    g_clear_pointer (&priv->price_string, g_free);
    g_clear_pointer (&priv->currency, g_free);
    g_clear_pointer (&priv->store_id, g_free);
    g_clear_pointer (&priv->min_game_version, g_free);

    G_OBJECT_CLASS (lrg_dlc_parent_class)->finalize (object);
}

static void
lrg_dlc_class_init (LrgDlcClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    LrgModClass *mod_class = LRG_MOD_CLASS (klass);

    object_class->get_property = lrg_dlc_get_property;
    object_class->set_property = lrg_dlc_set_property;
    object_class->dispose = lrg_dlc_dispose;
    object_class->finalize = lrg_dlc_finalize;

    /* Override LrgMod virtual methods */
    mod_class->can_load = lrg_dlc_can_load;

    /* LrgDlc virtual methods */
    klass->verify_ownership = lrg_dlc_real_verify_ownership;
    klass->get_trial_content_ids = lrg_dlc_real_get_trial_content_ids;
    klass->get_store_url = lrg_dlc_real_get_store_url;

    /**
     * LrgDlc:dlc-type:
     *
     * The type of DLC content.
     */
    properties[PROP_DLC_TYPE] =
        g_param_spec_enum ("dlc-type",
                           "DLC Type",
                           "The type of DLC content",
                           LRG_TYPE_DLC_TYPE,
                           LRG_DLC_TYPE_EXPANSION,
                           G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

    /**
     * LrgDlc:price-string:
     *
     * Display price string (e.g., "$14.99").
     */
    properties[PROP_PRICE_STRING] =
        g_param_spec_string ("price-string",
                             "Price String",
                             "Display price string",
                             NULL,
                             G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgDlc:currency:
     *
     * Currency code (e.g., "USD").
     */
    properties[PROP_CURRENCY] =
        g_param_spec_string ("currency",
                             "Currency",
                             "Currency code",
                             NULL,
                             G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgDlc:steam-app-id:
     *
     * Steam App ID for this DLC (0 if not a Steam DLC).
     */
    properties[PROP_STEAM_APP_ID] =
        g_param_spec_uint ("steam-app-id",
                           "Steam App ID",
                           "Steam App ID for this DLC",
                           0, G_MAXUINT32, 0,
                           G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgDlc:store-id:
     *
     * Generic store identifier.
     */
    properties[PROP_STORE_ID] =
        g_param_spec_string ("store-id",
                             "Store ID",
                             "Generic store identifier",
                             NULL,
                             G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgDlc:release-date:
     *
     * DLC release date.
     */
    properties[PROP_RELEASE_DATE] =
        g_param_spec_boxed ("release-date",
                            "Release Date",
                            "DLC release date",
                            G_TYPE_DATE_TIME,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgDlc:min-game-version:
     *
     * Minimum game version required.
     */
    properties[PROP_MIN_GAME_VERSION] =
        g_param_spec_string ("min-game-version",
                             "Minimum Game Version",
                             "Minimum game version required",
                             NULL,
                             G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgDlc:ownership-state:
     *
     * Current ownership state.
     */
    properties[PROP_OWNERSHIP_STATE] =
        g_param_spec_enum ("ownership-state",
                           "Ownership State",
                           "Current ownership state",
                           LRG_TYPE_DLC_OWNERSHIP_STATE,
                           LRG_DLC_OWNERSHIP_UNKNOWN,
                           G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgDlc:trial-enabled:
     *
     * Whether trial mode is enabled.
     */
    properties[PROP_TRIAL_ENABLED] =
        g_param_spec_boolean ("trial-enabled",
                              "Trial Enabled",
                              "Whether trial mode is enabled",
                              FALSE,
                              G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);

    /**
     * LrgDlc::ownership-changed:
     * @self: the #LrgDlc
     * @state: the new #LrgDlcOwnershipState
     *
     * Emitted when the ownership state changes.
     */
    signals[SIGNAL_OWNERSHIP_CHANGED] =
        g_signal_new ("ownership-changed",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0,
                      NULL, NULL,
                      NULL,
                      G_TYPE_NONE, 1,
                      LRG_TYPE_DLC_OWNERSHIP_STATE);

    /**
     * LrgDlc::purchase-prompted:
     * @self: the #LrgDlc
     * @content_id: the content ID that was accessed
     *
     * Emitted when unowned content is accessed.
     */
    signals[SIGNAL_PURCHASE_PROMPTED] =
        g_signal_new ("purchase-prompted",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0,
                      NULL, NULL,
                      NULL,
                      G_TYPE_NONE, 1,
                      G_TYPE_STRING);
}

static void
lrg_dlc_init (LrgDlc *self)
{
    LrgDlcPrivate *priv = lrg_dlc_get_instance_private (self);

    priv->dlc_type = LRG_DLC_TYPE_EXPANSION;
    priv->ownership_state = LRG_DLC_OWNERSHIP_UNKNOWN;
    priv->trial_enabled = FALSE;
    priv->trial_content_ids = g_ptr_array_new_with_free_func (g_free);
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

/**
 * lrg_dlc_new:
 * @manifest: the mod manifest
 * @base_path: the DLC's base directory path
 * @dlc_type: the type of DLC
 *
 * Creates a new DLC from a manifest.
 *
 * Returns: (transfer full): a new #LrgDlc
 */
LrgDlc *
lrg_dlc_new (LrgModManifest *manifest,
             const gchar    *base_path,
             LrgDlcType      dlc_type)
{
    g_return_val_if_fail (manifest != NULL, NULL);
    g_return_val_if_fail (base_path != NULL, NULL);

    return g_object_new (LRG_TYPE_DLC,
                         "manifest", manifest,
                         "base-path", base_path,
                         "dlc-type", dlc_type,
                         NULL);
}

/**
 * lrg_dlc_get_dlc_type:
 * @self: a #LrgDlc
 *
 * Gets the DLC type.
 *
 * Returns: the #LrgDlcType
 */
LrgDlcType
lrg_dlc_get_dlc_type (LrgDlc *self)
{
    LrgDlcPrivate *priv;

    g_return_val_if_fail (LRG_IS_DLC (self), LRG_DLC_TYPE_EXPANSION);

    priv = lrg_dlc_get_instance_private (self);
    return priv->dlc_type;
}

/**
 * lrg_dlc_get_price_string:
 * @self: a #LrgDlc
 *
 * Gets the display price string.
 *
 * Returns: (transfer none) (nullable): the price string
 */
const gchar *
lrg_dlc_get_price_string (LrgDlc *self)
{
    LrgDlcPrivate *priv;

    g_return_val_if_fail (LRG_IS_DLC (self), NULL);

    priv = lrg_dlc_get_instance_private (self);
    return priv->price_string;
}

/**
 * lrg_dlc_set_price_string:
 * @self: a #LrgDlc
 * @price_string: the price string
 *
 * Sets the display price string.
 */
void
lrg_dlc_set_price_string (LrgDlc      *self,
                           const gchar *price_string)
{
    g_return_if_fail (LRG_IS_DLC (self));

    g_object_set (self, "price-string", price_string, NULL);
}

/**
 * lrg_dlc_get_currency:
 * @self: a #LrgDlc
 *
 * Gets the currency code.
 *
 * Returns: (transfer none) (nullable): the currency code
 */
const gchar *
lrg_dlc_get_currency (LrgDlc *self)
{
    LrgDlcPrivate *priv;

    g_return_val_if_fail (LRG_IS_DLC (self), NULL);

    priv = lrg_dlc_get_instance_private (self);
    return priv->currency;
}

/**
 * lrg_dlc_set_currency:
 * @self: a #LrgDlc
 * @currency: the currency code
 *
 * Sets the currency code.
 */
void
lrg_dlc_set_currency (LrgDlc      *self,
                       const gchar *currency)
{
    g_return_if_fail (LRG_IS_DLC (self));

    g_object_set (self, "currency", currency, NULL);
}

/**
 * lrg_dlc_get_steam_app_id:
 * @self: a #LrgDlc
 *
 * Gets the Steam App ID for this DLC.
 *
 * Returns: the Steam App ID, or 0 if not a Steam DLC
 */
guint32
lrg_dlc_get_steam_app_id (LrgDlc *self)
{
    LrgDlcPrivate *priv;

    g_return_val_if_fail (LRG_IS_DLC (self), 0);

    priv = lrg_dlc_get_instance_private (self);
    return priv->steam_app_id;
}

/**
 * lrg_dlc_set_steam_app_id:
 * @self: a #LrgDlc
 * @app_id: the Steam App ID
 *
 * Sets the Steam App ID for this DLC.
 */
void
lrg_dlc_set_steam_app_id (LrgDlc *self,
                           guint32 app_id)
{
    g_return_if_fail (LRG_IS_DLC (self));

    g_object_set (self, "steam-app-id", app_id, NULL);
}

/**
 * lrg_dlc_get_store_id:
 * @self: a #LrgDlc
 *
 * Gets the generic store identifier.
 *
 * Returns: (transfer none) (nullable): the store ID
 */
const gchar *
lrg_dlc_get_store_id (LrgDlc *self)
{
    LrgDlcPrivate *priv;

    g_return_val_if_fail (LRG_IS_DLC (self), NULL);

    priv = lrg_dlc_get_instance_private (self);
    return priv->store_id;
}

/**
 * lrg_dlc_set_store_id:
 * @self: a #LrgDlc
 * @store_id: the store identifier
 *
 * Sets the generic store identifier.
 */
void
lrg_dlc_set_store_id (LrgDlc      *self,
                       const gchar *store_id)
{
    g_return_if_fail (LRG_IS_DLC (self));

    g_object_set (self, "store-id", store_id, NULL);
}

/**
 * lrg_dlc_get_release_date:
 * @self: a #LrgDlc
 *
 * Gets the DLC release date.
 *
 * Returns: (transfer none) (nullable): the release date
 */
GDateTime *
lrg_dlc_get_release_date (LrgDlc *self)
{
    LrgDlcPrivate *priv;

    g_return_val_if_fail (LRG_IS_DLC (self), NULL);

    priv = lrg_dlc_get_instance_private (self);
    return priv->release_date;
}

/**
 * lrg_dlc_set_release_date:
 * @self: a #LrgDlc
 * @release_date: (nullable): the release date
 *
 * Sets the DLC release date.
 */
void
lrg_dlc_set_release_date (LrgDlc    *self,
                           GDateTime *release_date)
{
    g_return_if_fail (LRG_IS_DLC (self));

    g_object_set (self, "release-date", release_date, NULL);
}

/**
 * lrg_dlc_get_min_game_version:
 * @self: a #LrgDlc
 *
 * Gets the minimum game version required.
 *
 * Returns: (transfer none) (nullable): the minimum version string
 */
const gchar *
lrg_dlc_get_min_game_version (LrgDlc *self)
{
    LrgDlcPrivate *priv;

    g_return_val_if_fail (LRG_IS_DLC (self), NULL);

    priv = lrg_dlc_get_instance_private (self);
    return priv->min_game_version;
}

/**
 * lrg_dlc_set_min_game_version:
 * @self: a #LrgDlc
 * @version: the minimum version string
 *
 * Sets the minimum game version required.
 */
void
lrg_dlc_set_min_game_version (LrgDlc      *self,
                               const gchar *version)
{
    g_return_if_fail (LRG_IS_DLC (self));

    g_object_set (self, "min-game-version", version, NULL);
}

/**
 * lrg_dlc_get_trial_enabled:
 * @self: a #LrgDlc
 *
 * Gets whether trial mode is enabled.
 *
 * Returns: %TRUE if trial mode is enabled
 */
gboolean
lrg_dlc_get_trial_enabled (LrgDlc *self)
{
    LrgDlcPrivate *priv;

    g_return_val_if_fail (LRG_IS_DLC (self), FALSE);

    priv = lrg_dlc_get_instance_private (self);
    return priv->trial_enabled;
}

/**
 * lrg_dlc_set_trial_enabled:
 * @self: a #LrgDlc
 * @enabled: whether to enable trial mode
 *
 * Sets whether trial mode is enabled.
 */
void
lrg_dlc_set_trial_enabled (LrgDlc   *self,
                            gboolean  enabled)
{
    g_return_if_fail (LRG_IS_DLC (self));

    g_object_set (self, "trial-enabled", enabled, NULL);
}

/**
 * lrg_dlc_get_ownership_state:
 * @self: a #LrgDlc
 *
 * Gets the current ownership state.
 *
 * Returns: the #LrgDlcOwnershipState
 */
LrgDlcOwnershipState
lrg_dlc_get_ownership_state (LrgDlc *self)
{
    LrgDlcPrivate *priv;

    g_return_val_if_fail (LRG_IS_DLC (self), LRG_DLC_OWNERSHIP_UNKNOWN);

    priv = lrg_dlc_get_instance_private (self);
    return priv->ownership_state;
}

/**
 * lrg_dlc_set_ownership_checker:
 * @self: a #LrgDlc
 * @checker: (nullable): the ownership checker to use
 *
 * Sets the ownership checker for this DLC.
 */
void
lrg_dlc_set_ownership_checker (LrgDlc          *self,
                                LrgDlcOwnership *checker)
{
    LrgDlcPrivate *priv;

    g_return_if_fail (LRG_IS_DLC (self));
    g_return_if_fail (checker == NULL || LRG_IS_DLC_OWNERSHIP (checker));

    priv = lrg_dlc_get_instance_private (self);

    g_set_object (&priv->ownership_checker, checker);
}

/**
 * lrg_dlc_get_ownership_checker:
 * @self: a #LrgDlc
 *
 * Gets the ownership checker for this DLC.
 *
 * Returns: (transfer none) (nullable): the ownership checker
 */
LrgDlcOwnership *
lrg_dlc_get_ownership_checker (LrgDlc *self)
{
    LrgDlcPrivate *priv;

    g_return_val_if_fail (LRG_IS_DLC (self), NULL);

    priv = lrg_dlc_get_instance_private (self);
    return priv->ownership_checker;
}

/**
 * lrg_dlc_verify_ownership:
 * @self: a #LrgDlc
 * @error: (nullable): return location for a #GError
 *
 * Verifies ownership of this DLC.
 *
 * Returns: the new #LrgDlcOwnershipState
 */
LrgDlcOwnershipState
lrg_dlc_verify_ownership (LrgDlc   *self,
                           GError  **error)
{
    g_return_val_if_fail (LRG_IS_DLC (self), LRG_DLC_OWNERSHIP_ERROR);
    g_return_val_if_fail (error == NULL || *error == NULL, LRG_DLC_OWNERSHIP_ERROR);

    return LRG_DLC_GET_CLASS (self)->verify_ownership (self, error);
}

/**
 * lrg_dlc_is_owned:
 * @self: a #LrgDlc
 *
 * Checks if the DLC is owned.
 *
 * Returns: %TRUE if the DLC is accessible
 */
gboolean
lrg_dlc_is_owned (LrgDlc *self)
{
    LrgDlcPrivate *priv;

    g_return_val_if_fail (LRG_IS_DLC (self), FALSE);

    priv = lrg_dlc_get_instance_private (self);

    return (priv->ownership_state == LRG_DLC_OWNERSHIP_OWNED ||
            priv->ownership_state == LRG_DLC_OWNERSHIP_TRIAL);
}

/**
 * lrg_dlc_add_trial_content_id:
 * @self: a #LrgDlc
 * @content_id: the content identifier
 *
 * Adds a content ID that is accessible in trial mode.
 */
void
lrg_dlc_add_trial_content_id (LrgDlc      *self,
                               const gchar *content_id)
{
    LrgDlcPrivate *priv;

    g_return_if_fail (LRG_IS_DLC (self));
    g_return_if_fail (content_id != NULL);

    priv = lrg_dlc_get_instance_private (self);

    g_ptr_array_add (priv->trial_content_ids, g_strdup (content_id));
}

/**
 * lrg_dlc_remove_trial_content_id:
 * @self: a #LrgDlc
 * @content_id: the content identifier
 *
 * Removes a content ID from trial access.
 */
void
lrg_dlc_remove_trial_content_id (LrgDlc      *self,
                                  const gchar *content_id)
{
    LrgDlcPrivate *priv;
    guint i;

    g_return_if_fail (LRG_IS_DLC (self));
    g_return_if_fail (content_id != NULL);

    priv = lrg_dlc_get_instance_private (self);

    for (i = 0; i < priv->trial_content_ids->len; i++)
    {
        if (g_strcmp0 (g_ptr_array_index (priv->trial_content_ids, i), content_id) == 0)
        {
            g_ptr_array_remove_index (priv->trial_content_ids, i);
            return;
        }
    }
}

/**
 * lrg_dlc_get_trial_content_ids:
 * @self: a #LrgDlc
 *
 * Gets the list of content IDs accessible in trial mode.
 *
 * Returns: (transfer none) (element-type utf8): array of content IDs
 */
GPtrArray *
lrg_dlc_get_trial_content_ids (LrgDlc *self)
{
    g_return_val_if_fail (LRG_IS_DLC (self), NULL);

    return LRG_DLC_GET_CLASS (self)->get_trial_content_ids (self);
}

/**
 * lrg_dlc_is_content_accessible:
 * @self: a #LrgDlc
 * @content_id: the content identifier to check
 *
 * Checks if specific content is accessible.
 *
 * Returns: %TRUE if the content is accessible
 */
gboolean
lrg_dlc_is_content_accessible (LrgDlc      *self,
                                const gchar *content_id)
{
    LrgDlcPrivate *priv;
    guint i;

    g_return_val_if_fail (LRG_IS_DLC (self), FALSE);
    g_return_val_if_fail (content_id != NULL, FALSE);

    priv = lrg_dlc_get_instance_private (self);

    /* If fully owned, all content is accessible */
    if (priv->ownership_state == LRG_DLC_OWNERSHIP_OWNED)
        return TRUE;

    /* If in trial mode, check if content is in trial list */
    if (priv->ownership_state == LRG_DLC_OWNERSHIP_TRIAL)
    {
        for (i = 0; i < priv->trial_content_ids->len; i++)
        {
            if (g_strcmp0 (g_ptr_array_index (priv->trial_content_ids, i), content_id) == 0)
                return TRUE;
        }
    }

    /* Content not accessible - emit purchase prompt signal */
    g_signal_emit (self, signals[SIGNAL_PURCHASE_PROMPTED], 0, content_id);

    return FALSE;
}

/**
 * lrg_dlc_get_store_url:
 * @self: a #LrgDlc
 *
 * Gets the URL to the DLC's store page.
 *
 * Returns: (transfer full) (nullable): the store URL, or %NULL
 */
gchar *
lrg_dlc_get_store_url (LrgDlc *self)
{
    g_return_val_if_fail (LRG_IS_DLC (self), NULL);

    return LRG_DLC_GET_CLASS (self)->get_store_url (self);
}

/**
 * lrg_dlc_open_store_page:
 * @self: a #LrgDlc
 * @error: (nullable): return location for a #GError
 *
 * Opens the DLC's store page.
 *
 * Returns: %TRUE if the store page was opened successfully
 */
gboolean
lrg_dlc_open_store_page (LrgDlc   *self,
                          GError  **error)
{
    g_autofree gchar *url = NULL;

    g_return_val_if_fail (LRG_IS_DLC (self), FALSE);
    g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

    /*
     * Note: Steam overlay support would require additional Steam SDK
     * integration. For now, we fall back to opening the URL in browser.
     */

    url = lrg_dlc_get_store_url (self);
    if (url == NULL)
    {
        g_set_error (error,
                     LRG_DLC_ERROR,
                     LRG_DLC_ERROR_FAILED,
                     "No store URL available for DLC");
        return FALSE;
    }

    return g_app_info_launch_default_for_uri (url, NULL, error);
}
