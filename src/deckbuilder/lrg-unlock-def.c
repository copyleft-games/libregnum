/* lrg-unlock-def.c
 *
 * Copyright 2025 Libregnum Authors
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "lrg-unlock-def.h"
#include "lrg-player-profile.h"
#include "../lrg-log.h"

/**
 * LrgUnlockDef:
 *
 * Base class for unlock condition definitions.
 *
 * Since: 1.0
 */

typedef struct
{
    gchar        *id;
    LrgUnlockType unlock_type;
    gchar        *target_id;
    gchar        *name;
    gchar        *description;
    gboolean      hidden;

    /* Simple condition data */
    gchar *win_character_id;
    gint   win_count;

    gchar *run_character_id;
    gint   run_count;

    gchar *ascension_character_id;
    gint   ascension_level;

    gchar *required_unlock_id;
} LrgUnlockDefPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (LrgUnlockDef, lrg_unlock_def, G_TYPE_OBJECT)

enum
{
    PROP_0,
    PROP_ID,
    PROP_UNLOCK_TYPE,
    PROP_TARGET_ID,
    PROP_NAME,
    PROP_DESCRIPTION,
    PROP_HIDDEN,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

/* ==========================================================================
 * Default Virtual Method Implementations
 * ========================================================================== */

static gboolean
lrg_unlock_def_real_check_condition (LrgUnlockDef     *self,
                                      LrgPlayerProfile *profile)
{
    LrgUnlockDefPrivate *priv;

    priv = lrg_unlock_def_get_instance_private (self);

    if (profile == NULL)
        return FALSE;

    /* Check win count requirement */
    if (priv->win_count > 0)
    {
        gint wins;

        if (priv->win_character_id != NULL)
            wins = lrg_player_profile_get_character_wins (profile, priv->win_character_id);
        else
            wins = lrg_player_profile_get_total_wins (profile);

        if (wins < priv->win_count)
            return FALSE;
    }

    /* Check run count requirement */
    if (priv->run_count > 0)
    {
        gint runs;

        if (priv->run_character_id != NULL)
            runs = lrg_player_profile_get_character_runs (profile, priv->run_character_id);
        else
            runs = lrg_player_profile_get_total_runs (profile);

        if (runs < priv->run_count)
            return FALSE;
    }

    /* Check ascension requirement */
    if (priv->ascension_level > 0 && priv->ascension_character_id != NULL)
    {
        gint max_asc;

        max_asc = lrg_player_profile_get_max_ascension (profile, priv->ascension_character_id);
        if (max_asc < priv->ascension_level)
            return FALSE;
    }

    /* Check prerequisite unlock */
    if (priv->required_unlock_id != NULL)
    {
        /* This would need to check another unlock - for now just pass */
        /* In a real implementation, this would look up the required unlock
         * and check if it's been granted to the profile */
    }

    return TRUE;
}

static gfloat
lrg_unlock_def_real_get_progress (LrgUnlockDef     *self,
                                   LrgPlayerProfile *profile)
{
    LrgUnlockDefPrivate *priv;
    gfloat               progress;
    gint                 num_conditions;

    priv = lrg_unlock_def_get_instance_private (self);

    if (profile == NULL)
        return 0.0f;

    progress = 0.0f;
    num_conditions = 0;

    /* Average progress across all conditions */

    if (priv->win_count > 0)
    {
        gint wins;
        gfloat win_progress;

        if (priv->win_character_id != NULL)
            wins = lrg_player_profile_get_character_wins (profile, priv->win_character_id);
        else
            wins = lrg_player_profile_get_total_wins (profile);

        win_progress = (gfloat)wins / (gfloat)priv->win_count;
        if (win_progress > 1.0f)
            win_progress = 1.0f;

        progress += win_progress;
        num_conditions++;
    }

    if (priv->run_count > 0)
    {
        gint runs;
        gfloat run_progress;

        if (priv->run_character_id != NULL)
            runs = lrg_player_profile_get_character_runs (profile, priv->run_character_id);
        else
            runs = lrg_player_profile_get_total_runs (profile);

        run_progress = (gfloat)runs / (gfloat)priv->run_count;
        if (run_progress > 1.0f)
            run_progress = 1.0f;

        progress += run_progress;
        num_conditions++;
    }

    if (priv->ascension_level > 0 && priv->ascension_character_id != NULL)
    {
        gint max_asc;
        gfloat asc_progress;

        max_asc = lrg_player_profile_get_max_ascension (profile, priv->ascension_character_id);
        asc_progress = (gfloat)max_asc / (gfloat)priv->ascension_level;
        if (asc_progress > 1.0f)
            asc_progress = 1.0f;

        progress += asc_progress;
        num_conditions++;
    }

    if (num_conditions == 0)
        return 1.0f;  /* No conditions = unlocked */

    return progress / (gfloat)num_conditions;
}

static gchar *
lrg_unlock_def_real_get_requirement_text (LrgUnlockDef *self)
{
    LrgUnlockDefPrivate *priv;
    GString             *text;

    priv = lrg_unlock_def_get_instance_private (self);
    text = g_string_new (NULL);

    if (priv->win_count > 0)
    {
        if (priv->win_character_id != NULL)
            g_string_append_printf (text, "Win %d runs with %s. ",
                                    priv->win_count, priv->win_character_id);
        else
            g_string_append_printf (text, "Win %d runs. ", priv->win_count);
    }

    if (priv->run_count > 0)
    {
        if (priv->run_character_id != NULL)
            g_string_append_printf (text, "Complete %d runs with %s. ",
                                    priv->run_count, priv->run_character_id);
        else
            g_string_append_printf (text, "Complete %d runs. ", priv->run_count);
    }

    if (priv->ascension_level > 0 && priv->ascension_character_id != NULL)
    {
        g_string_append_printf (text, "Reach Ascension %d with %s. ",
                                priv->ascension_level, priv->ascension_character_id);
    }

    if (priv->required_unlock_id != NULL)
    {
        g_string_append_printf (text, "Unlock: %s. ", priv->required_unlock_id);
    }

    if (text->len == 0)
        g_string_append (text, "Unknown requirement.");

    return g_string_free (text, FALSE);
}

static void
lrg_unlock_def_real_on_unlocked (LrgUnlockDef     *self,
                                  LrgPlayerProfile *profile)
{
    /* Default: no-op */
}

/* ==========================================================================
 * GObject Implementation
 * ========================================================================== */

static void
lrg_unlock_def_finalize (GObject *object)
{
    LrgUnlockDef        *self;
    LrgUnlockDefPrivate *priv;

    self = LRG_UNLOCK_DEF (object);
    priv = lrg_unlock_def_get_instance_private (self);

    g_clear_pointer (&priv->id, g_free);
    g_clear_pointer (&priv->target_id, g_free);
    g_clear_pointer (&priv->name, g_free);
    g_clear_pointer (&priv->description, g_free);
    g_clear_pointer (&priv->win_character_id, g_free);
    g_clear_pointer (&priv->run_character_id, g_free);
    g_clear_pointer (&priv->ascension_character_id, g_free);
    g_clear_pointer (&priv->required_unlock_id, g_free);

    G_OBJECT_CLASS (lrg_unlock_def_parent_class)->finalize (object);
}

static void
lrg_unlock_def_get_property (GObject    *object,
                              guint       prop_id,
                              GValue     *value,
                              GParamSpec *pspec)
{
    LrgUnlockDef        *self;
    LrgUnlockDefPrivate *priv;

    self = LRG_UNLOCK_DEF (object);
    priv = lrg_unlock_def_get_instance_private (self);

    switch (prop_id)
    {
        case PROP_ID:
            g_value_set_string (value, priv->id);
            break;

        case PROP_UNLOCK_TYPE:
            g_value_set_enum (value, priv->unlock_type);
            break;

        case PROP_TARGET_ID:
            g_value_set_string (value, priv->target_id);
            break;

        case PROP_NAME:
            g_value_set_string (value, priv->name);
            break;

        case PROP_DESCRIPTION:
            g_value_set_string (value, priv->description);
            break;

        case PROP_HIDDEN:
            g_value_set_boolean (value, priv->hidden);
            break;

        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_unlock_def_set_property (GObject      *object,
                              guint         prop_id,
                              const GValue *value,
                              GParamSpec   *pspec)
{
    LrgUnlockDef        *self;
    LrgUnlockDefPrivate *priv;

    self = LRG_UNLOCK_DEF (object);
    priv = lrg_unlock_def_get_instance_private (self);

    switch (prop_id)
    {
        case PROP_ID:
            g_clear_pointer (&priv->id, g_free);
            priv->id = g_value_dup_string (value);
            break;

        case PROP_UNLOCK_TYPE:
            priv->unlock_type = g_value_get_enum (value);
            break;

        case PROP_TARGET_ID:
            g_clear_pointer (&priv->target_id, g_free);
            priv->target_id = g_value_dup_string (value);
            break;

        case PROP_NAME:
            g_clear_pointer (&priv->name, g_free);
            priv->name = g_value_dup_string (value);
            break;

        case PROP_DESCRIPTION:
            g_clear_pointer (&priv->description, g_free);
            priv->description = g_value_dup_string (value);
            break;

        case PROP_HIDDEN:
            priv->hidden = g_value_get_boolean (value);
            break;

        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_unlock_def_class_init (LrgUnlockDefClass *klass)
{
    GObjectClass *object_class;

    object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = lrg_unlock_def_finalize;
    object_class->get_property = lrg_unlock_def_get_property;
    object_class->set_property = lrg_unlock_def_set_property;

    /* Virtual methods */
    klass->check_condition = lrg_unlock_def_real_check_condition;
    klass->get_progress = lrg_unlock_def_real_get_progress;
    klass->get_requirement_text = lrg_unlock_def_real_get_requirement_text;
    klass->on_unlocked = lrg_unlock_def_real_on_unlocked;

    /**
     * LrgUnlockDef:id:
     *
     * The unlock's unique identifier.
     *
     * Since: 1.0
     */
    properties[PROP_ID] =
        g_param_spec_string ("id",
                             "ID",
                             "Unique identifier",
                             NULL,
                             G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

    /**
     * LrgUnlockDef:unlock-type:
     *
     * The type of content being unlocked.
     *
     * Since: 1.0
     */
    properties[PROP_UNLOCK_TYPE] =
        g_param_spec_enum ("unlock-type",
                           "Unlock Type",
                           "Type of content being unlocked",
                           LRG_TYPE_UNLOCK_TYPE,
                           LRG_UNLOCK_TYPE_CHARACTER,
                           G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

    /**
     * LrgUnlockDef:target-id:
     *
     * The ID of the content to unlock.
     *
     * Since: 1.0
     */
    properties[PROP_TARGET_ID] =
        g_param_spec_string ("target-id",
                             "Target ID",
                             "ID of content to unlock",
                             NULL,
                             G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

    /**
     * LrgUnlockDef:name:
     *
     * The unlock's display name.
     *
     * Since: 1.0
     */
    properties[PROP_NAME] =
        g_param_spec_string ("name",
                             "Name",
                             "Display name",
                             NULL,
                             G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgUnlockDef:description:
     *
     * The unlock's description.
     *
     * Since: 1.0
     */
    properties[PROP_DESCRIPTION] =
        g_param_spec_string ("description",
                             "Description",
                             "Unlock description",
                             NULL,
                             G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgUnlockDef:hidden:
     *
     * Whether this unlock is hidden until discovered.
     *
     * Since: 1.0
     */
    properties[PROP_HIDDEN] =
        g_param_spec_boolean ("hidden",
                              "Hidden",
                              "Whether hidden until discovered",
                              FALSE,
                              G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_unlock_def_init (LrgUnlockDef *self)
{
    /* Defaults are fine */
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

/**
 * lrg_unlock_def_new:
 * @id: unique identifier
 * @unlock_type: type of content being unlocked
 * @target_id: ID of the content to unlock
 *
 * Creates a new unlock definition.
 *
 * Returns: (transfer full): a new #LrgUnlockDef
 *
 * Since: 1.0
 */
LrgUnlockDef *
lrg_unlock_def_new (const gchar   *id,
                     LrgUnlockType  unlock_type,
                     const gchar   *target_id)
{
    g_return_val_if_fail (id != NULL, NULL);
    g_return_val_if_fail (target_id != NULL, NULL);

    return g_object_new (LRG_TYPE_UNLOCK_DEF,
                         "id", id,
                         "unlock-type", unlock_type,
                         "target-id", target_id,
                         NULL);
}

/**
 * lrg_unlock_def_get_id:
 * @self: a #LrgUnlockDef
 *
 * Gets the unlock's unique identifier.
 *
 * Returns: (transfer none): the ID
 *
 * Since: 1.0
 */
const gchar *
lrg_unlock_def_get_id (LrgUnlockDef *self)
{
    LrgUnlockDefPrivate *priv;

    g_return_val_if_fail (LRG_IS_UNLOCK_DEF (self), NULL);

    priv = lrg_unlock_def_get_instance_private (self);
    return priv->id;
}

/**
 * lrg_unlock_def_get_unlock_type:
 * @self: a #LrgUnlockDef
 *
 * Gets the type of content being unlocked.
 *
 * Returns: the #LrgUnlockType
 *
 * Since: 1.0
 */
LrgUnlockType
lrg_unlock_def_get_unlock_type (LrgUnlockDef *self)
{
    LrgUnlockDefPrivate *priv;

    g_return_val_if_fail (LRG_IS_UNLOCK_DEF (self), LRG_UNLOCK_TYPE_CHARACTER);

    priv = lrg_unlock_def_get_instance_private (self);
    return priv->unlock_type;
}

/**
 * lrg_unlock_def_get_target_id:
 * @self: a #LrgUnlockDef
 *
 * Gets the ID of the content to unlock.
 *
 * Returns: (transfer none): the target ID
 *
 * Since: 1.0
 */
const gchar *
lrg_unlock_def_get_target_id (LrgUnlockDef *self)
{
    LrgUnlockDefPrivate *priv;

    g_return_val_if_fail (LRG_IS_UNLOCK_DEF (self), NULL);

    priv = lrg_unlock_def_get_instance_private (self);
    return priv->target_id;
}

/**
 * lrg_unlock_def_get_name:
 * @self: a #LrgUnlockDef
 *
 * Gets the unlock's display name.
 *
 * Returns: (transfer none) (nullable): the name
 *
 * Since: 1.0
 */
const gchar *
lrg_unlock_def_get_name (LrgUnlockDef *self)
{
    LrgUnlockDefPrivate *priv;

    g_return_val_if_fail (LRG_IS_UNLOCK_DEF (self), NULL);

    priv = lrg_unlock_def_get_instance_private (self);
    return priv->name;
}

/**
 * lrg_unlock_def_set_name:
 * @self: a #LrgUnlockDef
 * @name: (nullable): the display name
 *
 * Sets the unlock's display name.
 *
 * Since: 1.0
 */
void
lrg_unlock_def_set_name (LrgUnlockDef *self,
                          const gchar  *name)
{
    g_return_if_fail (LRG_IS_UNLOCK_DEF (self));

    g_object_set (self, "name", name, NULL);
}

/**
 * lrg_unlock_def_get_description:
 * @self: a #LrgUnlockDef
 *
 * Gets the unlock's description.
 *
 * Returns: (transfer none) (nullable): the description
 *
 * Since: 1.0
 */
const gchar *
lrg_unlock_def_get_description (LrgUnlockDef *self)
{
    LrgUnlockDefPrivate *priv;

    g_return_val_if_fail (LRG_IS_UNLOCK_DEF (self), NULL);

    priv = lrg_unlock_def_get_instance_private (self);
    return priv->description;
}

/**
 * lrg_unlock_def_set_description:
 * @self: a #LrgUnlockDef
 * @description: (nullable): the description
 *
 * Sets the unlock's description.
 *
 * Since: 1.0
 */
void
lrg_unlock_def_set_description (LrgUnlockDef *self,
                                 const gchar  *description)
{
    g_return_if_fail (LRG_IS_UNLOCK_DEF (self));

    g_object_set (self, "description", description, NULL);
}

/**
 * lrg_unlock_def_get_hidden:
 * @self: a #LrgUnlockDef
 *
 * Gets whether this unlock is hidden until discovered.
 *
 * Returns: %TRUE if hidden
 *
 * Since: 1.0
 */
gboolean
lrg_unlock_def_get_hidden (LrgUnlockDef *self)
{
    LrgUnlockDefPrivate *priv;

    g_return_val_if_fail (LRG_IS_UNLOCK_DEF (self), FALSE);

    priv = lrg_unlock_def_get_instance_private (self);
    return priv->hidden;
}

/**
 * lrg_unlock_def_set_hidden:
 * @self: a #LrgUnlockDef
 * @hidden: whether hidden
 *
 * Sets whether this unlock is hidden until discovered.
 *
 * Since: 1.0
 */
void
lrg_unlock_def_set_hidden (LrgUnlockDef *self,
                            gboolean      hidden)
{
    g_return_if_fail (LRG_IS_UNLOCK_DEF (self));

    g_object_set (self, "hidden", hidden, NULL);
}

/**
 * lrg_unlock_def_set_win_count:
 * @self: a #LrgUnlockDef
 * @character_id: (nullable): character ID, or NULL for any
 * @count: number of wins required
 *
 * Sets a win count requirement.
 *
 * Since: 1.0
 */
void
lrg_unlock_def_set_win_count (LrgUnlockDef *self,
                               const gchar  *character_id,
                               gint          count)
{
    LrgUnlockDefPrivate *priv;

    g_return_if_fail (LRG_IS_UNLOCK_DEF (self));
    g_return_if_fail (count > 0);

    priv = lrg_unlock_def_get_instance_private (self);

    g_clear_pointer (&priv->win_character_id, g_free);
    priv->win_character_id = g_strdup (character_id);
    priv->win_count = count;
}

/**
 * lrg_unlock_def_set_run_count:
 * @self: a #LrgUnlockDef
 * @character_id: (nullable): character ID, or NULL for any
 * @count: number of runs required
 *
 * Sets a run count requirement.
 *
 * Since: 1.0
 */
void
lrg_unlock_def_set_run_count (LrgUnlockDef *self,
                               const gchar  *character_id,
                               gint          count)
{
    LrgUnlockDefPrivate *priv;

    g_return_if_fail (LRG_IS_UNLOCK_DEF (self));
    g_return_if_fail (count > 0);

    priv = lrg_unlock_def_get_instance_private (self);

    g_clear_pointer (&priv->run_character_id, g_free);
    priv->run_character_id = g_strdup (character_id);
    priv->run_count = count;
}

/**
 * lrg_unlock_def_set_ascension_requirement:
 * @self: a #LrgUnlockDef
 * @character_id: character ID
 * @level: ascension level required
 *
 * Sets an ascension level requirement.
 *
 * Since: 1.0
 */
void
lrg_unlock_def_set_ascension_requirement (LrgUnlockDef *self,
                                           const gchar  *character_id,
                                           gint          level)
{
    LrgUnlockDefPrivate *priv;

    g_return_if_fail (LRG_IS_UNLOCK_DEF (self));
    g_return_if_fail (character_id != NULL);
    g_return_if_fail (level > 0);

    priv = lrg_unlock_def_get_instance_private (self);

    g_clear_pointer (&priv->ascension_character_id, g_free);
    priv->ascension_character_id = g_strdup (character_id);
    priv->ascension_level = level;
}

/**
 * lrg_unlock_def_set_unlock_requirement:
 * @self: a #LrgUnlockDef
 * @required_unlock_id: ID of unlock that must be completed first
 *
 * Sets a prerequisite unlock requirement.
 *
 * Since: 1.0
 */
void
lrg_unlock_def_set_unlock_requirement (LrgUnlockDef *self,
                                        const gchar  *required_unlock_id)
{
    LrgUnlockDefPrivate *priv;

    g_return_if_fail (LRG_IS_UNLOCK_DEF (self));
    g_return_if_fail (required_unlock_id != NULL);

    priv = lrg_unlock_def_get_instance_private (self);

    g_clear_pointer (&priv->required_unlock_id, g_free);
    priv->required_unlock_id = g_strdup (required_unlock_id);
}

/**
 * lrg_unlock_def_check_condition:
 * @self: a #LrgUnlockDef
 * @profile: player profile to check
 *
 * Checks if unlock conditions are met.
 *
 * Returns: %TRUE if conditions are met
 *
 * Since: 1.0
 */
gboolean
lrg_unlock_def_check_condition (LrgUnlockDef     *self,
                                 LrgPlayerProfile *profile)
{
    LrgUnlockDefClass *klass;

    g_return_val_if_fail (LRG_IS_UNLOCK_DEF (self), FALSE);

    klass = LRG_UNLOCK_DEF_GET_CLASS (self);

    if (klass->check_condition != NULL)
        return klass->check_condition (self, profile);

    return FALSE;
}

/**
 * lrg_unlock_def_get_progress:
 * @self: a #LrgUnlockDef
 * @profile: player profile to check
 *
 * Gets progress toward unlock.
 *
 * Returns: progress (0.0 - 1.0)
 *
 * Since: 1.0
 */
gfloat
lrg_unlock_def_get_progress (LrgUnlockDef     *self,
                              LrgPlayerProfile *profile)
{
    LrgUnlockDefClass *klass;

    g_return_val_if_fail (LRG_IS_UNLOCK_DEF (self), 0.0f);

    klass = LRG_UNLOCK_DEF_GET_CLASS (self);

    if (klass->get_progress != NULL)
        return klass->get_progress (self, profile);

    return 0.0f;
}

/**
 * lrg_unlock_def_get_requirement_text:
 * @self: a #LrgUnlockDef
 *
 * Gets human-readable requirement description.
 *
 * Returns: (transfer full): requirement text
 *
 * Since: 1.0
 */
gchar *
lrg_unlock_def_get_requirement_text (LrgUnlockDef *self)
{
    LrgUnlockDefClass *klass;

    g_return_val_if_fail (LRG_IS_UNLOCK_DEF (self), NULL);

    klass = LRG_UNLOCK_DEF_GET_CLASS (self);

    if (klass->get_requirement_text != NULL)
        return klass->get_requirement_text (self);

    return g_strdup ("Unknown requirement.");
}

/**
 * lrg_unlock_def_grant:
 * @self: a #LrgUnlockDef
 * @profile: player profile to unlock for
 *
 * Grants the unlock if conditions are met.
 *
 * Returns: %TRUE if unlock was granted
 *
 * Since: 1.0
 */
gboolean
lrg_unlock_def_grant (LrgUnlockDef     *self,
                       LrgPlayerProfile *profile)
{
    LrgUnlockDefPrivate *priv;
    LrgUnlockDefClass   *klass;

    g_return_val_if_fail (LRG_IS_UNLOCK_DEF (self), FALSE);
    g_return_val_if_fail (LRG_IS_PLAYER_PROFILE (profile), FALSE);

    priv = lrg_unlock_def_get_instance_private (self);
    klass = LRG_UNLOCK_DEF_GET_CLASS (self);

    /* Check if already unlocked */
    if (lrg_player_profile_is_unlocked (profile, priv->unlock_type, priv->target_id))
        return FALSE;

    /* Check conditions */
    if (!lrg_unlock_def_check_condition (self, profile))
        return FALSE;

    /* Grant the unlock */
    lrg_player_profile_unlock (profile, priv->unlock_type, priv->target_id);

    /* Notify */
    if (klass->on_unlocked != NULL)
        klass->on_unlocked (self, profile);

    return TRUE;
}
