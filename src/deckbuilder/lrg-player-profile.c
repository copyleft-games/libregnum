/* lrg-player-profile.c
 *
 * Copyright 2025 Libregnum Authors
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "lrg-player-profile.h"
#include "../save/lrg-save-context.h"
#include "../lrg-log.h"

/**
 * LrgPlayerProfile:
 *
 * Persistent player progress data.
 *
 * Since: 1.0
 */

/* Structure for per-item unlock tracking */
typedef struct
{
    LrgUnlockStatus status;
} UnlockEntry;

/* Structure for per-character progress */
typedef struct
{
    gint  wins;
    gint  runs;
    gint  max_ascension;
    gint64 high_score;
} CharacterProgress;

struct _LrgPlayerProfile
{
    GObject parent_instance;

    gchar  *name;
    gint64  total_playtime;
    gboolean dirty;

    /* Unlock tracking: type -> (id -> UnlockEntry) */
    GHashTable *unlocks[8];  /* One per LrgUnlockType */

    /* Character progress: character_id -> CharacterProgress */
    GHashTable *character_progress;

    /* Statistics: stat_name -> value */
    GHashTable *statistics;

    /* Global high score */
    gint64 global_high_score;
};

static void lrg_player_profile_saveable_init (LrgSaveableInterface *iface);

G_DEFINE_TYPE_WITH_CODE (LrgPlayerProfile, lrg_player_profile, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (LRG_TYPE_SAVEABLE,
                                                lrg_player_profile_saveable_init))

enum
{
    PROP_0,
    PROP_NAME,
    PROP_TOTAL_PLAYTIME,
    PROP_DIRTY,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

/* Default singleton */
static LrgPlayerProfile *default_profile = NULL;

/* ==========================================================================
 * Internal Helpers
 * ========================================================================== */

static void
unlock_entry_free (gpointer data)
{
    g_free (data);
}

static void
character_progress_free (gpointer data)
{
    g_free (data);
}

static CharacterProgress *
get_or_create_character_progress (LrgPlayerProfile *self,
                                  const gchar      *character_id)
{
    CharacterProgress *progress;

    progress = g_hash_table_lookup (self->character_progress, character_id);
    if (progress == NULL)
    {
        progress = g_new0 (CharacterProgress, 1);
        g_hash_table_insert (self->character_progress,
                             g_strdup (character_id),
                             progress);
    }

    return progress;
}

/* ==========================================================================
 * LrgSaveable Implementation
 * ========================================================================== */

static const gchar *
lrg_player_profile_get_save_id_impl (LrgSaveable *saveable)
{
    return "player-profile";
}

static gboolean
lrg_player_profile_save_impl (LrgSaveable    *saveable,
                              LrgSaveContext *context,
                              GError        **error)
{
    LrgPlayerProfile *self;
    GHashTableIter    iter;
    gpointer          key;
    gpointer          value;
    gint              i;

    self = LRG_PLAYER_PROFILE (saveable);

    /* Profile info section */
    lrg_save_context_begin_section (context, "profile");
    lrg_save_context_write_string (context, "name", self->name);
    lrg_save_context_write_int (context, "total-playtime", self->total_playtime);
    lrg_save_context_write_int (context, "global-high-score", self->global_high_score);
    lrg_save_context_end_section (context);

    /*
     * Unlocks section: one sub-section per unlock type.
     * Each type stores a comma-separated list of IDs in "_keys" and
     * individual status values for each ID.
     */
    lrg_save_context_begin_section (context, "unlocks");
    for (i = 0; i < 8; i++)
    {
        g_autofree gchar *type_key = NULL;
        GString *keys_csv;

        type_key = g_strdup_printf ("type-%d", i);
        lrg_save_context_begin_section (context, type_key);

        /* Build CSV of all IDs for this type so we can iterate on load */
        keys_csv = g_string_new (NULL);
        g_hash_table_iter_init (&iter, self->unlocks[i]);
        while (g_hash_table_iter_next (&iter, &key, &value))
        {
            UnlockEntry *entry = (UnlockEntry *)value;

            if (keys_csv->len > 0)
                g_string_append_c (keys_csv, ',');
            g_string_append (keys_csv, (const gchar *)key);

            lrg_save_context_write_int (context, (const gchar *)key,
                                        (gint64)entry->status);
        }
        lrg_save_context_write_string (context, "_keys", keys_csv->str);
        g_string_free (keys_csv, TRUE);

        lrg_save_context_end_section (context);
    }
    lrg_save_context_end_section (context);

    /* Character progress section with key tracking */
    lrg_save_context_begin_section (context, "characters");
    {
        GString *char_keys;

        char_keys = g_string_new (NULL);
        g_hash_table_iter_init (&iter, self->character_progress);
        while (g_hash_table_iter_next (&iter, &key, &value))
        {
            CharacterProgress *progress = (CharacterProgress *)value;

            if (char_keys->len > 0)
                g_string_append_c (char_keys, ',');
            g_string_append (char_keys, (const gchar *)key);

            lrg_save_context_begin_section (context, (const gchar *)key);
            lrg_save_context_write_int (context, "wins", progress->wins);
            lrg_save_context_write_int (context, "runs", progress->runs);
            lrg_save_context_write_int (context, "max-ascension", progress->max_ascension);
            lrg_save_context_write_int (context, "high-score", progress->high_score);
            lrg_save_context_end_section (context);
        }
        lrg_save_context_write_string (context, "_keys", char_keys->str);
        g_string_free (char_keys, TRUE);
    }
    lrg_save_context_end_section (context);

    /* Statistics section with key tracking */
    lrg_save_context_begin_section (context, "statistics");
    {
        GString *stat_keys;

        stat_keys = g_string_new (NULL);
        g_hash_table_iter_init (&iter, self->statistics);
        while (g_hash_table_iter_next (&iter, &key, &value))
        {
            if (stat_keys->len > 0)
                g_string_append_c (stat_keys, ',');
            g_string_append (stat_keys, (const gchar *)key);

            lrg_save_context_write_int (context, (const gchar *)key,
                                        (gint64)GPOINTER_TO_SIZE (value));
        }
        lrg_save_context_write_string (context, "_keys", stat_keys->str);
        g_string_free (stat_keys, TRUE);
    }
    lrg_save_context_end_section (context);

    self->dirty = FALSE;

    return TRUE;
}

static gboolean
lrg_player_profile_load_impl (LrgSaveable    *saveable,
                              LrgSaveContext *context,
                              GError        **error)
{
    LrgPlayerProfile *self;
    gint              i;

    self = LRG_PLAYER_PROFILE (saveable);

    /* Load profile info */
    if (lrg_save_context_enter_section (context, "profile"))
    {
        const gchar *name;

        name = lrg_save_context_read_string (context, "name", "Player");
        g_clear_pointer (&self->name, g_free);
        self->name = g_strdup (name);

        self->total_playtime = lrg_save_context_read_int (context, "total-playtime", 0);
        self->global_high_score = lrg_save_context_read_int (context, "global-high-score", 0);

        lrg_save_context_leave_section (context);
    }

    /* Load unlocks */
    if (lrg_save_context_enter_section (context, "unlocks"))
    {
        for (i = 0; i < 8; i++)
        {
            g_autofree gchar *type_key = NULL;

            type_key = g_strdup_printf ("type-%d", i);

            /* Clear existing entries for this type */
            g_hash_table_remove_all (self->unlocks[i]);

            if (lrg_save_context_enter_section (context, type_key))
            {
                const gchar *keys_csv;

                keys_csv = lrg_save_context_read_string (context, "_keys", "");
                if (keys_csv != NULL && keys_csv[0] != '\0')
                {
                    g_auto(GStrv) ids = NULL;
                    gint j;

                    ids = g_strsplit (keys_csv, ",", -1);
                    for (j = 0; ids[j] != NULL; j++)
                    {
                        UnlockEntry *entry;
                        gint64       status;

                        status = lrg_save_context_read_int (context, ids[j], 0);
                        entry = g_new0 (UnlockEntry, 1);
                        entry->status = (LrgUnlockStatus)status;
                        g_hash_table_insert (self->unlocks[i],
                                             g_strdup (ids[j]),
                                             entry);
                    }
                }

                lrg_save_context_leave_section (context);
            }
        }

        lrg_save_context_leave_section (context);
    }

    /* Load character progress */
    if (lrg_save_context_enter_section (context, "characters"))
    {
        const gchar *keys_csv;

        g_hash_table_remove_all (self->character_progress);

        keys_csv = lrg_save_context_read_string (context, "_keys", "");
        if (keys_csv != NULL && keys_csv[0] != '\0')
        {
            g_auto(GStrv) ids = NULL;
            gint j;

            ids = g_strsplit (keys_csv, ",", -1);
            for (j = 0; ids[j] != NULL; j++)
            {
                if (lrg_save_context_enter_section (context, ids[j]))
                {
                    CharacterProgress *progress;

                    progress = g_new0 (CharacterProgress, 1);
                    progress->wins = (gint)lrg_save_context_read_int (context, "wins", 0);
                    progress->runs = (gint)lrg_save_context_read_int (context, "runs", 0);
                    progress->max_ascension = (gint)lrg_save_context_read_int (context, "max-ascension", 0);
                    progress->high_score = lrg_save_context_read_int (context, "high-score", 0);

                    g_hash_table_insert (self->character_progress,
                                         g_strdup (ids[j]),
                                         progress);

                    lrg_save_context_leave_section (context);
                }
            }
        }

        lrg_save_context_leave_section (context);
    }

    /* Load statistics */
    if (lrg_save_context_enter_section (context, "statistics"))
    {
        const gchar *keys_csv;

        g_hash_table_remove_all (self->statistics);

        keys_csv = lrg_save_context_read_string (context, "_keys", "");
        if (keys_csv != NULL && keys_csv[0] != '\0')
        {
            g_auto(GStrv) ids = NULL;
            gint j;

            ids = g_strsplit (keys_csv, ",", -1);
            for (j = 0; ids[j] != NULL; j++)
            {
                gint64 val;

                val = lrg_save_context_read_int (context, ids[j], 0);
                g_hash_table_insert (self->statistics,
                                     g_strdup (ids[j]),
                                     GSIZE_TO_POINTER ((gsize)val));
            }
        }

        lrg_save_context_leave_section (context);
    }

    self->dirty = FALSE;

    return TRUE;
}

static void
lrg_player_profile_saveable_init (LrgSaveableInterface *iface)
{
    iface->get_save_id = lrg_player_profile_get_save_id_impl;
    iface->save = lrg_player_profile_save_impl;
    iface->load = lrg_player_profile_load_impl;
}

/* ==========================================================================
 * GObject Implementation
 * ========================================================================== */

static void
lrg_player_profile_finalize (GObject *object)
{
    LrgPlayerProfile *self;
    gint              i;

    self = LRG_PLAYER_PROFILE (object);

    g_clear_pointer (&self->name, g_free);

    for (i = 0; i < 8; i++)
    {
        g_clear_pointer (&self->unlocks[i], g_hash_table_unref);
    }

    g_clear_pointer (&self->character_progress, g_hash_table_unref);
    g_clear_pointer (&self->statistics, g_hash_table_unref);

    G_OBJECT_CLASS (lrg_player_profile_parent_class)->finalize (object);
}

static void
lrg_player_profile_get_property (GObject    *object,
                                  guint       prop_id,
                                  GValue     *value,
                                  GParamSpec *pspec)
{
    LrgPlayerProfile *self;

    self = LRG_PLAYER_PROFILE (object);

    switch (prop_id)
    {
        case PROP_NAME:
            g_value_set_string (value, self->name);
            break;

        case PROP_TOTAL_PLAYTIME:
            g_value_set_int64 (value, self->total_playtime);
            break;

        case PROP_DIRTY:
            g_value_set_boolean (value, self->dirty);
            break;

        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_player_profile_set_property (GObject      *object,
                                  guint         prop_id,
                                  const GValue *value,
                                  GParamSpec   *pspec)
{
    LrgPlayerProfile *self;

    self = LRG_PLAYER_PROFILE (object);

    switch (prop_id)
    {
        case PROP_NAME:
            g_clear_pointer (&self->name, g_free);
            self->name = g_value_dup_string (value);
            self->dirty = TRUE;
            break;

        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_player_profile_class_init (LrgPlayerProfileClass *klass)
{
    GObjectClass *object_class;

    object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = lrg_player_profile_finalize;
    object_class->get_property = lrg_player_profile_get_property;
    object_class->set_property = lrg_player_profile_set_property;

    /**
     * LrgPlayerProfile:name:
     *
     * The profile name.
     *
     * Since: 1.0
     */
    properties[PROP_NAME] =
        g_param_spec_string ("name",
                             "Name",
                             "Profile name",
                             "Player",
                             G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgPlayerProfile:total-playtime:
     *
     * Total playtime in seconds.
     *
     * Since: 1.0
     */
    properties[PROP_TOTAL_PLAYTIME] =
        g_param_spec_int64 ("total-playtime",
                            "Total Playtime",
                            "Total playtime in seconds",
                            0, G_MAXINT64, 0,
                            G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgPlayerProfile:dirty:
     *
     * Whether there are unsaved changes.
     *
     * Since: 1.0
     */
    properties[PROP_DIRTY] =
        g_param_spec_boolean ("dirty",
                              "Dirty",
                              "Whether there are unsaved changes",
                              FALSE,
                              G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_player_profile_init (LrgPlayerProfile *self)
{
    gint i;

    self->name = g_strdup ("Player");

    /* Initialize unlock hash tables */
    for (i = 0; i < 8; i++)
    {
        self->unlocks[i] = g_hash_table_new_full (g_str_hash, g_str_equal,
                                                   g_free, unlock_entry_free);
    }

    self->character_progress = g_hash_table_new_full (g_str_hash, g_str_equal,
                                                       g_free, character_progress_free);

    self->statistics = g_hash_table_new_full (g_str_hash, g_str_equal,
                                               g_free, NULL);
}

/* ==========================================================================
 * Public API - Constructors
 * ========================================================================== */

/**
 * lrg_player_profile_new:
 * @profile_name: (nullable): profile name
 *
 * Creates a new player profile.
 *
 * Returns: (transfer full): a new #LrgPlayerProfile
 *
 * Since: 1.0
 */
LrgPlayerProfile *
lrg_player_profile_new (const gchar *profile_name)
{
    LrgPlayerProfile *self;

    self = g_object_new (LRG_TYPE_PLAYER_PROFILE, NULL);

    if (profile_name != NULL)
    {
        g_clear_pointer (&self->name, g_free);
        self->name = g_strdup (profile_name);
    }

    return self;
}

/**
 * lrg_player_profile_get_default:
 *
 * Gets the default player profile singleton.
 *
 * Returns: (transfer none): the default #LrgPlayerProfile
 *
 * Since: 1.0
 */
LrgPlayerProfile *
lrg_player_profile_get_default (void)
{
    if (default_profile == NULL)
    {
        default_profile = lrg_player_profile_new (NULL);
    }

    return default_profile;
}

/* ==========================================================================
 * Public API - Profile Info
 * ========================================================================== */

/**
 * lrg_player_profile_get_name:
 * @self: a #LrgPlayerProfile
 *
 * Gets the profile name.
 *
 * Returns: (transfer none) (nullable): profile name
 *
 * Since: 1.0
 */
const gchar *
lrg_player_profile_get_name (LrgPlayerProfile *self)
{
    g_return_val_if_fail (LRG_IS_PLAYER_PROFILE (self), NULL);

    return self->name;
}

/**
 * lrg_player_profile_set_name:
 * @self: a #LrgPlayerProfile
 * @name: (nullable): new profile name
 *
 * Sets the profile name.
 *
 * Since: 1.0
 */
void
lrg_player_profile_set_name (LrgPlayerProfile *self,
                              const gchar      *name)
{
    g_return_if_fail (LRG_IS_PLAYER_PROFILE (self));

    g_object_set (self, "name", name, NULL);
}

/**
 * lrg_player_profile_get_total_playtime:
 * @self: a #LrgPlayerProfile
 *
 * Gets total playtime in seconds.
 *
 * Returns: total playtime
 *
 * Since: 1.0
 */
gint64
lrg_player_profile_get_total_playtime (LrgPlayerProfile *self)
{
    g_return_val_if_fail (LRG_IS_PLAYER_PROFILE (self), 0);

    return self->total_playtime;
}

/**
 * lrg_player_profile_add_playtime:
 * @self: a #LrgPlayerProfile
 * @seconds: seconds to add
 *
 * Adds to total playtime.
 *
 * Since: 1.0
 */
void
lrg_player_profile_add_playtime (LrgPlayerProfile *self,
                                  gint64            seconds)
{
    g_return_if_fail (LRG_IS_PLAYER_PROFILE (self));
    g_return_if_fail (seconds >= 0);

    self->total_playtime += seconds;
    self->dirty = TRUE;
}

/* ==========================================================================
 * Public API - Unlock Tracking
 * ========================================================================== */

/**
 * lrg_player_profile_is_unlocked:
 * @self: a #LrgPlayerProfile
 * @unlock_type: type of unlock
 * @id: ID of the item
 *
 * Checks if an item is unlocked.
 *
 * Returns: %TRUE if unlocked
 *
 * Since: 1.0
 */
gboolean
lrg_player_profile_is_unlocked (LrgPlayerProfile *self,
                                 LrgUnlockType     unlock_type,
                                 const gchar      *id)
{
    UnlockEntry *entry;

    g_return_val_if_fail (LRG_IS_PLAYER_PROFILE (self), FALSE);
    g_return_val_if_fail (unlock_type < 8, FALSE);
    g_return_val_if_fail (id != NULL, FALSE);

    entry = g_hash_table_lookup (self->unlocks[unlock_type], id);
    if (entry == NULL)
        return FALSE;

    return entry->status != LRG_UNLOCK_STATUS_LOCKED;
}

/**
 * lrg_player_profile_unlock:
 * @self: a #LrgPlayerProfile
 * @unlock_type: type of unlock
 * @id: ID of the item to unlock
 *
 * Unlocks an item.
 *
 * Returns: %TRUE if newly unlocked, %FALSE if already unlocked
 *
 * Since: 1.0
 */
gboolean
lrg_player_profile_unlock (LrgPlayerProfile *self,
                            LrgUnlockType     unlock_type,
                            const gchar      *id)
{
    UnlockEntry *entry;

    g_return_val_if_fail (LRG_IS_PLAYER_PROFILE (self), FALSE);
    g_return_val_if_fail (unlock_type < 8, FALSE);
    g_return_val_if_fail (id != NULL, FALSE);

    entry = g_hash_table_lookup (self->unlocks[unlock_type], id);

    if (entry != NULL && entry->status != LRG_UNLOCK_STATUS_LOCKED)
    {
        /* Already unlocked */
        return FALSE;
    }

    if (entry == NULL)
    {
        entry = g_new0 (UnlockEntry, 1);
        g_hash_table_insert (self->unlocks[unlock_type],
                             g_strdup (id),
                             entry);
    }

    entry->status = LRG_UNLOCK_STATUS_NEW;
    self->dirty = TRUE;

    return TRUE;
}

/**
 * lrg_player_profile_get_unlock_status:
 * @self: a #LrgPlayerProfile
 * @unlock_type: type of unlock
 * @id: ID of the item
 *
 * Gets the unlock status of an item.
 *
 * Returns: the #LrgUnlockStatus
 *
 * Since: 1.0
 */
LrgUnlockStatus
lrg_player_profile_get_unlock_status (LrgPlayerProfile *self,
                                       LrgUnlockType     unlock_type,
                                       const gchar      *id)
{
    UnlockEntry *entry;

    g_return_val_if_fail (LRG_IS_PLAYER_PROFILE (self), LRG_UNLOCK_STATUS_LOCKED);
    g_return_val_if_fail (unlock_type < 8, LRG_UNLOCK_STATUS_LOCKED);
    g_return_val_if_fail (id != NULL, LRG_UNLOCK_STATUS_LOCKED);

    entry = g_hash_table_lookup (self->unlocks[unlock_type], id);
    if (entry == NULL)
        return LRG_UNLOCK_STATUS_LOCKED;

    return entry->status;
}

/**
 * lrg_player_profile_mark_seen:
 * @self: a #LrgPlayerProfile
 * @unlock_type: type of unlock
 * @id: ID of the item
 *
 * Marks a newly unlocked item as seen (removes "NEW" badge).
 *
 * Since: 1.0
 */
void
lrg_player_profile_mark_seen (LrgPlayerProfile *self,
                               LrgUnlockType     unlock_type,
                               const gchar      *id)
{
    UnlockEntry *entry;

    g_return_if_fail (LRG_IS_PLAYER_PROFILE (self));
    g_return_if_fail (unlock_type < 8);
    g_return_if_fail (id != NULL);

    entry = g_hash_table_lookup (self->unlocks[unlock_type], id);
    if (entry != NULL && entry->status == LRG_UNLOCK_STATUS_NEW)
    {
        entry->status = LRG_UNLOCK_STATUS_UNLOCKED;
        self->dirty = TRUE;
    }
}

/**
 * lrg_player_profile_get_unlocked_ids:
 * @self: a #LrgPlayerProfile
 * @unlock_type: type of unlock
 *
 * Gets all unlocked IDs of a type.
 *
 * Returns: (transfer full) (element-type utf8): array of unlocked IDs
 *
 * Since: 1.0
 */
GPtrArray *
lrg_player_profile_get_unlocked_ids (LrgPlayerProfile *self,
                                      LrgUnlockType     unlock_type)
{
    GPtrArray     *result;
    GHashTableIter iter;
    gpointer       key;
    gpointer       value;

    g_return_val_if_fail (LRG_IS_PLAYER_PROFILE (self), NULL);
    g_return_val_if_fail (unlock_type < 8, NULL);

    result = g_ptr_array_new_with_free_func (g_free);

    g_hash_table_iter_init (&iter, self->unlocks[unlock_type]);
    while (g_hash_table_iter_next (&iter, &key, &value))
    {
        UnlockEntry *entry;

        entry = value;
        if (entry->status != LRG_UNLOCK_STATUS_LOCKED)
        {
            g_ptr_array_add (result, g_strdup ((const gchar *)key));
        }
    }

    return result;
}

/* ==========================================================================
 * Public API - Character Progress
 * ========================================================================== */

/**
 * lrg_player_profile_get_character_wins:
 * @self: a #LrgPlayerProfile
 * @character_id: character ID
 *
 * Gets number of wins with a character.
 *
 * Returns: win count
 *
 * Since: 1.0
 */
gint
lrg_player_profile_get_character_wins (LrgPlayerProfile *self,
                                        const gchar      *character_id)
{
    CharacterProgress *progress;

    g_return_val_if_fail (LRG_IS_PLAYER_PROFILE (self), 0);
    g_return_val_if_fail (character_id != NULL, 0);

    progress = g_hash_table_lookup (self->character_progress, character_id);
    if (progress == NULL)
        return 0;

    return progress->wins;
}

/**
 * lrg_player_profile_add_character_win:
 * @self: a #LrgPlayerProfile
 * @character_id: character ID
 *
 * Records a win with a character.
 *
 * Since: 1.0
 */
void
lrg_player_profile_add_character_win (LrgPlayerProfile *self,
                                       const gchar      *character_id)
{
    CharacterProgress *progress;

    g_return_if_fail (LRG_IS_PLAYER_PROFILE (self));
    g_return_if_fail (character_id != NULL);

    progress = get_or_create_character_progress (self, character_id);
    progress->wins++;
    self->dirty = TRUE;
}

/**
 * lrg_player_profile_get_character_runs:
 * @self: a #LrgPlayerProfile
 * @character_id: character ID
 *
 * Gets total number of runs with a character.
 *
 * Returns: run count
 *
 * Since: 1.0
 */
gint
lrg_player_profile_get_character_runs (LrgPlayerProfile *self,
                                        const gchar      *character_id)
{
    CharacterProgress *progress;

    g_return_val_if_fail (LRG_IS_PLAYER_PROFILE (self), 0);
    g_return_val_if_fail (character_id != NULL, 0);

    progress = g_hash_table_lookup (self->character_progress, character_id);
    if (progress == NULL)
        return 0;

    return progress->runs;
}

/**
 * lrg_player_profile_add_character_run:
 * @self: a #LrgPlayerProfile
 * @character_id: character ID
 *
 * Records a run attempt with a character.
 *
 * Since: 1.0
 */
void
lrg_player_profile_add_character_run (LrgPlayerProfile *self,
                                       const gchar      *character_id)
{
    CharacterProgress *progress;

    g_return_if_fail (LRG_IS_PLAYER_PROFILE (self));
    g_return_if_fail (character_id != NULL);

    progress = get_or_create_character_progress (self, character_id);
    progress->runs++;
    self->dirty = TRUE;
}

/* ==========================================================================
 * Public API - Ascension Progress
 * ========================================================================== */

/**
 * lrg_player_profile_get_max_ascension:
 * @self: a #LrgPlayerProfile
 * @character_id: character ID
 *
 * Gets the maximum unlocked ascension level for a character.
 *
 * Returns: max ascension level (0 = none)
 *
 * Since: 1.0
 */
gint
lrg_player_profile_get_max_ascension (LrgPlayerProfile *self,
                                       const gchar      *character_id)
{
    CharacterProgress *progress;

    g_return_val_if_fail (LRG_IS_PLAYER_PROFILE (self), 0);
    g_return_val_if_fail (character_id != NULL, 0);

    progress = g_hash_table_lookup (self->character_progress, character_id);
    if (progress == NULL)
        return 0;

    return progress->max_ascension;
}

/**
 * lrg_player_profile_set_max_ascension:
 * @self: a #LrgPlayerProfile
 * @character_id: character ID
 * @level: ascension level
 *
 * Sets the maximum unlocked ascension level.
 *
 * Since: 1.0
 */
void
lrg_player_profile_set_max_ascension (LrgPlayerProfile *self,
                                       const gchar      *character_id,
                                       gint              level)
{
    CharacterProgress *progress;

    g_return_if_fail (LRG_IS_PLAYER_PROFILE (self));
    g_return_if_fail (character_id != NULL);
    g_return_if_fail (level >= 0);

    progress = get_or_create_character_progress (self, character_id);
    progress->max_ascension = level;
    self->dirty = TRUE;
}

/**
 * lrg_player_profile_unlock_next_ascension:
 * @self: a #LrgPlayerProfile
 * @character_id: character ID
 *
 * Unlocks the next ascension level for a character.
 *
 * Returns: the newly unlocked level, or -1 if at max (20)
 *
 * Since: 1.0
 */
gint
lrg_player_profile_unlock_next_ascension (LrgPlayerProfile *self,
                                           const gchar      *character_id)
{
    CharacterProgress *progress;
    gint               next_level;

    g_return_val_if_fail (LRG_IS_PLAYER_PROFILE (self), -1);
    g_return_val_if_fail (character_id != NULL, -1);

    progress = get_or_create_character_progress (self, character_id);

    /* Max ascension is typically 20 (like Slay the Spire) */
    if (progress->max_ascension >= 20)
        return -1;

    next_level = progress->max_ascension + 1;
    progress->max_ascension = next_level;
    self->dirty = TRUE;

    return next_level;
}

/* ==========================================================================
 * Public API - Statistics
 * ========================================================================== */

/**
 * lrg_player_profile_get_stat:
 * @self: a #LrgPlayerProfile
 * @stat_name: statistic name
 *
 * Gets a statistic value.
 *
 * Returns: the statistic value
 *
 * Since: 1.0
 */
gint64
lrg_player_profile_get_stat (LrgPlayerProfile *self,
                              const gchar      *stat_name)
{
    gpointer value;

    g_return_val_if_fail (LRG_IS_PLAYER_PROFILE (self), 0);
    g_return_val_if_fail (stat_name != NULL, 0);

    if (g_hash_table_lookup_extended (self->statistics, stat_name, NULL, &value))
        return GPOINTER_TO_SIZE (value);

    return 0;
}

/**
 * lrg_player_profile_set_stat:
 * @self: a #LrgPlayerProfile
 * @stat_name: statistic name
 * @value: new value
 *
 * Sets a statistic value.
 *
 * Since: 1.0
 */
void
lrg_player_profile_set_stat (LrgPlayerProfile *self,
                              const gchar      *stat_name,
                              gint64            value)
{
    g_return_if_fail (LRG_IS_PLAYER_PROFILE (self));
    g_return_if_fail (stat_name != NULL);

    g_hash_table_insert (self->statistics,
                         g_strdup (stat_name),
                         GSIZE_TO_POINTER ((gsize)value));
    self->dirty = TRUE;
}

/**
 * lrg_player_profile_increment_stat:
 * @self: a #LrgPlayerProfile
 * @stat_name: statistic name
 * @amount: amount to add
 *
 * Increments a statistic.
 *
 * Since: 1.0
 */
void
lrg_player_profile_increment_stat (LrgPlayerProfile *self,
                                    const gchar      *stat_name,
                                    gint64            amount)
{
    gint64 current;

    g_return_if_fail (LRG_IS_PLAYER_PROFILE (self));
    g_return_if_fail (stat_name != NULL);

    current = lrg_player_profile_get_stat (self, stat_name);
    lrg_player_profile_set_stat (self, stat_name, current + amount);
}

/**
 * lrg_player_profile_get_total_wins:
 * @self: a #LrgPlayerProfile
 *
 * Gets total number of wins across all characters.
 *
 * Returns: total wins
 *
 * Since: 1.0
 */
gint
lrg_player_profile_get_total_wins (LrgPlayerProfile *self)
{
    GHashTableIter iter;
    gpointer       value;
    gint           total;

    g_return_val_if_fail (LRG_IS_PLAYER_PROFILE (self), 0);

    total = 0;
    g_hash_table_iter_init (&iter, self->character_progress);
    while (g_hash_table_iter_next (&iter, NULL, &value))
    {
        CharacterProgress *progress;

        progress = value;
        total += progress->wins;
    }

    return total;
}

/**
 * lrg_player_profile_get_total_runs:
 * @self: a #LrgPlayerProfile
 *
 * Gets total number of runs across all characters.
 *
 * Returns: total runs
 *
 * Since: 1.0
 */
gint
lrg_player_profile_get_total_runs (LrgPlayerProfile *self)
{
    GHashTableIter iter;
    gpointer       value;
    gint           total;

    g_return_val_if_fail (LRG_IS_PLAYER_PROFILE (self), 0);

    total = 0;
    g_hash_table_iter_init (&iter, self->character_progress);
    while (g_hash_table_iter_next (&iter, NULL, &value))
    {
        CharacterProgress *progress;

        progress = value;
        total += progress->runs;
    }

    return total;
}

/* ==========================================================================
 * Public API - High Scores
 * ========================================================================== */

/**
 * lrg_player_profile_get_high_score:
 * @self: a #LrgPlayerProfile
 * @character_id: (nullable): character ID, or NULL for global
 *
 * Gets the high score for a character or globally.
 *
 * Returns: high score
 *
 * Since: 1.0
 */
gint64
lrg_player_profile_get_high_score (LrgPlayerProfile *self,
                                    const gchar      *character_id)
{
    CharacterProgress *progress;

    g_return_val_if_fail (LRG_IS_PLAYER_PROFILE (self), 0);

    if (character_id == NULL)
        return self->global_high_score;

    progress = g_hash_table_lookup (self->character_progress, character_id);
    if (progress == NULL)
        return 0;

    return progress->high_score;
}

/**
 * lrg_player_profile_submit_score:
 * @self: a #LrgPlayerProfile
 * @character_id: character ID
 * @score: the score
 *
 * Submits a score, updating high score if applicable.
 *
 * Returns: %TRUE if this was a new high score
 *
 * Since: 1.0
 */
gboolean
lrg_player_profile_submit_score (LrgPlayerProfile *self,
                                  const gchar      *character_id,
                                  gint64            score)
{
    CharacterProgress *progress;
    gboolean           is_new_high;

    g_return_val_if_fail (LRG_IS_PLAYER_PROFILE (self), FALSE);
    g_return_val_if_fail (character_id != NULL, FALSE);

    is_new_high = FALSE;
    progress = get_or_create_character_progress (self, character_id);

    if (score > progress->high_score)
    {
        progress->high_score = score;
        is_new_high = TRUE;
        self->dirty = TRUE;
    }

    if (score > self->global_high_score)
    {
        self->global_high_score = score;
        is_new_high = TRUE;
        self->dirty = TRUE;
    }

    return is_new_high;
}

/* ==========================================================================
 * Public API - Persistence
 * ========================================================================== */

/**
 * lrg_player_profile_reset:
 * @self: a #LrgPlayerProfile
 *
 * Resets all progress (dangerous!).
 *
 * Since: 1.0
 */
void
lrg_player_profile_reset (LrgPlayerProfile *self)
{
    gint i;

    g_return_if_fail (LRG_IS_PLAYER_PROFILE (self));

    for (i = 0; i < 8; i++)
    {
        g_hash_table_remove_all (self->unlocks[i]);
    }

    g_hash_table_remove_all (self->character_progress);
    g_hash_table_remove_all (self->statistics);

    self->total_playtime = 0;
    self->global_high_score = 0;
    self->dirty = TRUE;
}

/**
 * lrg_player_profile_is_dirty:
 * @self: a #LrgPlayerProfile
 *
 * Checks if the profile has unsaved changes.
 *
 * Returns: %TRUE if there are unsaved changes
 *
 * Since: 1.0
 */
gboolean
lrg_player_profile_is_dirty (LrgPlayerProfile *self)
{
    g_return_val_if_fail (LRG_IS_PLAYER_PROFILE (self), FALSE);

    return self->dirty;
}

/**
 * lrg_player_profile_mark_clean:
 * @self: a #LrgPlayerProfile
 *
 * Marks the profile as saved (no unsaved changes).
 *
 * Since: 1.0
 */
void
lrg_player_profile_mark_clean (LrgPlayerProfile *self)
{
    g_return_if_fail (LRG_IS_PLAYER_PROFILE (self));

    self->dirty = FALSE;
}
