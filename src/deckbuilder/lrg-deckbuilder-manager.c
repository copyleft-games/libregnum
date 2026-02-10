/* lrg-deckbuilder-manager.c
 *
 * Copyright 2025 Libregnum Authors
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "lrg-deckbuilder-manager.h"
#include "lrg-character-def.h"
#include "lrg-player-profile.h"
#include "lrg-unlock-def.h"
#include "lrg-ascension.h"
#include "lrg-run.h"
#include "../save/lrg-save-context.h"
#include "../save/lrg-saveable.h"
#include "../lrg-log.h"

#include <gio/gio.h>

/**
 * LrgDeckbuilderManager:
 *
 * Central coordinator for deckbuilder systems.
 *
 * Since: 1.0
 */

struct _LrgDeckbuilderManager
{
    GObject parent_instance;

    LrgPlayerProfile *profile;

    /* Registries: id -> object */
    GHashTable *characters;
    GHashTable *unlocks;

    /* Ascension configs: level -> LrgAscension */
    GHashTable *ascensions;

    /* Current run (if any) */
    LrgRun *current_run;
};

G_DEFINE_TYPE (LrgDeckbuilderManager, lrg_deckbuilder_manager, G_TYPE_OBJECT)

/* Default singleton */
static LrgDeckbuilderManager *default_manager = NULL;

enum
{
    SIGNAL_RUN_STARTED,
    SIGNAL_RUN_ENDED,
    SIGNAL_UNLOCK_GRANTED,
    N_SIGNALS
};

static guint signals[N_SIGNALS];

/* ==========================================================================
 * GObject Implementation
 * ========================================================================== */

static void
lrg_deckbuilder_manager_dispose (GObject *object)
{
    LrgDeckbuilderManager *self;

    self = LRG_DECKBUILDER_MANAGER (object);

    g_clear_object (&self->profile);
    g_clear_object (&self->current_run);

    G_OBJECT_CLASS (lrg_deckbuilder_manager_parent_class)->dispose (object);
}

static void
lrg_deckbuilder_manager_finalize (GObject *object)
{
    LrgDeckbuilderManager *self;

    self = LRG_DECKBUILDER_MANAGER (object);

    g_clear_pointer (&self->characters, g_hash_table_unref);
    g_clear_pointer (&self->unlocks, g_hash_table_unref);
    g_clear_pointer (&self->ascensions, g_hash_table_unref);

    G_OBJECT_CLASS (lrg_deckbuilder_manager_parent_class)->finalize (object);
}

static void
lrg_deckbuilder_manager_class_init (LrgDeckbuilderManagerClass *klass)
{
    GObjectClass *object_class;

    object_class = G_OBJECT_CLASS (klass);

    object_class->dispose = lrg_deckbuilder_manager_dispose;
    object_class->finalize = lrg_deckbuilder_manager_finalize;

    /**
     * LrgDeckbuilderManager::run-started:
     * @manager: the manager
     * @run: the new run
     *
     * Emitted when a run starts.
     *
     * Since: 1.0
     */
    signals[SIGNAL_RUN_STARTED] =
        g_signal_new ("run-started",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0,
                      NULL, NULL,
                      NULL,
                      G_TYPE_NONE, 1, LRG_TYPE_RUN);

    /**
     * LrgDeckbuilderManager::run-ended:
     * @manager: the manager
     * @run: the ended run
     * @victory: whether the run was won
     *
     * Emitted when a run ends.
     *
     * Since: 1.0
     */
    signals[SIGNAL_RUN_ENDED] =
        g_signal_new ("run-ended",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0,
                      NULL, NULL,
                      NULL,
                      G_TYPE_NONE, 2, LRG_TYPE_RUN, G_TYPE_BOOLEAN);

    /**
     * LrgDeckbuilderManager::unlock-granted:
     * @manager: the manager
     * @unlock: the granted unlock
     *
     * Emitted when an unlock is granted.
     *
     * Since: 1.0
     */
    signals[SIGNAL_UNLOCK_GRANTED] =
        g_signal_new ("unlock-granted",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0,
                      NULL, NULL,
                      NULL,
                      G_TYPE_NONE, 1, LRG_TYPE_UNLOCK_DEF);
}

static void
lrg_deckbuilder_manager_init (LrgDeckbuilderManager *self)
{
    self->characters = g_hash_table_new_full (g_str_hash, g_str_equal,
                                               g_free, g_object_unref);
    self->unlocks = g_hash_table_new_full (g_str_hash, g_str_equal,
                                            g_free, g_object_unref);
    self->ascensions = g_hash_table_new_full (g_direct_hash, g_direct_equal,
                                               NULL, g_object_unref);

    /* Default profile */
    self->profile = lrg_player_profile_get_default ();
    g_object_ref (self->profile);
}

/* ==========================================================================
 * Public API - Singleton
 * ========================================================================== */

/**
 * lrg_deckbuilder_manager_get_default:
 *
 * Gets the default deckbuilder manager singleton.
 *
 * Returns: (transfer none): the default #LrgDeckbuilderManager
 *
 * Since: 1.0
 */
LrgDeckbuilderManager *
lrg_deckbuilder_manager_get_default (void)
{
    if (default_manager == NULL)
    {
        default_manager = g_object_new (LRG_TYPE_DECKBUILDER_MANAGER, NULL);
    }

    return default_manager;
}

/* ==========================================================================
 * Public API - Player Profile
 * ========================================================================== */

/**
 * lrg_deckbuilder_manager_get_profile:
 * @self: a #LrgDeckbuilderManager
 *
 * Gets the current player profile.
 *
 * Returns: (transfer none): the #LrgPlayerProfile
 *
 * Since: 1.0
 */
LrgPlayerProfile *
lrg_deckbuilder_manager_get_profile (LrgDeckbuilderManager *self)
{
    g_return_val_if_fail (LRG_IS_DECKBUILDER_MANAGER (self), NULL);

    return self->profile;
}

/**
 * lrg_deckbuilder_manager_set_profile:
 * @self: a #LrgDeckbuilderManager
 * @profile: (transfer none): the profile to use
 *
 * Sets the current player profile.
 *
 * Since: 1.0
 */
void
lrg_deckbuilder_manager_set_profile (LrgDeckbuilderManager *self,
                                      LrgPlayerProfile      *profile)
{
    g_return_if_fail (LRG_IS_DECKBUILDER_MANAGER (self));
    g_return_if_fail (LRG_IS_PLAYER_PROFILE (profile));

    if (self->profile != profile)
    {
        g_clear_object (&self->profile);
        self->profile = g_object_ref (profile);
    }
}

/* ==========================================================================
 * Public API - Character Registry
 * ========================================================================== */

/**
 * lrg_deckbuilder_manager_register_character:
 * @self: a #LrgDeckbuilderManager
 * @character: (transfer none): character to register
 *
 * Registers a character definition.
 *
 * Since: 1.0
 */
void
lrg_deckbuilder_manager_register_character (LrgDeckbuilderManager *self,
                                             LrgCharacterDef       *character)
{
    const gchar *id;

    g_return_if_fail (LRG_IS_DECKBUILDER_MANAGER (self));
    g_return_if_fail (LRG_IS_CHARACTER_DEF (character));

    id = lrg_character_def_get_id (character);
    g_return_if_fail (id != NULL);

    g_hash_table_insert (self->characters,
                         g_strdup (id),
                         g_object_ref (character));
}

/**
 * lrg_deckbuilder_manager_get_character:
 * @self: a #LrgDeckbuilderManager
 * @id: character ID
 *
 * Gets a character by ID.
 *
 * Returns: (transfer none) (nullable): the #LrgCharacterDef, or %NULL
 *
 * Since: 1.0
 */
LrgCharacterDef *
lrg_deckbuilder_manager_get_character (LrgDeckbuilderManager *self,
                                        const gchar           *id)
{
    g_return_val_if_fail (LRG_IS_DECKBUILDER_MANAGER (self), NULL);
    g_return_val_if_fail (id != NULL, NULL);

    return g_hash_table_lookup (self->characters, id);
}

/**
 * lrg_deckbuilder_manager_get_characters:
 * @self: a #LrgDeckbuilderManager
 *
 * Gets all registered characters.
 *
 * Returns: (transfer none) (element-type LrgCharacterDef): characters
 *
 * Since: 1.0
 */
GPtrArray *
lrg_deckbuilder_manager_get_characters (LrgDeckbuilderManager *self)
{
    GPtrArray      *result;
    GHashTableIter  iter;
    gpointer        value;

    g_return_val_if_fail (LRG_IS_DECKBUILDER_MANAGER (self), NULL);

    result = g_ptr_array_new ();

    g_hash_table_iter_init (&iter, self->characters);
    while (g_hash_table_iter_next (&iter, NULL, &value))
    {
        g_ptr_array_add (result, value);
    }

    return result;
}

/**
 * lrg_deckbuilder_manager_get_unlocked_characters:
 * @self: a #LrgDeckbuilderManager
 *
 * Gets characters unlocked for the current profile.
 *
 * Returns: (transfer full) (element-type LrgCharacterDef): unlocked characters
 *
 * Since: 1.0
 */
GPtrArray *
lrg_deckbuilder_manager_get_unlocked_characters (LrgDeckbuilderManager *self)
{
    GPtrArray      *result;
    GHashTableIter  iter;
    gpointer        value;

    g_return_val_if_fail (LRG_IS_DECKBUILDER_MANAGER (self), NULL);

    result = g_ptr_array_new_with_free_func (g_object_unref);

    g_hash_table_iter_init (&iter, self->characters);
    while (g_hash_table_iter_next (&iter, NULL, &value))
    {
        LrgCharacterDef *character;
        const gchar     *id;
        gboolean         unlocked;

        character = LRG_CHARACTER_DEF (value);
        id = lrg_character_def_get_id (character);

        /* Check if unlocked by default or via profile */
        unlocked = lrg_character_def_get_unlocked_by_default (character) ||
                   lrg_player_profile_is_unlocked (self->profile,
                                                   LRG_UNLOCK_TYPE_CHARACTER,
                                                   id);

        if (unlocked)
        {
            g_ptr_array_add (result, g_object_ref (character));
        }
    }

    return result;
}

/* ==========================================================================
 * Public API - Unlock Registry
 * ========================================================================== */

/**
 * lrg_deckbuilder_manager_register_unlock:
 * @self: a #LrgDeckbuilderManager
 * @unlock: (transfer none): unlock definition to register
 *
 * Registers an unlock definition.
 *
 * Since: 1.0
 */
void
lrg_deckbuilder_manager_register_unlock (LrgDeckbuilderManager *self,
                                          LrgUnlockDef          *unlock)
{
    const gchar *id;

    g_return_if_fail (LRG_IS_DECKBUILDER_MANAGER (self));
    g_return_if_fail (LRG_IS_UNLOCK_DEF (unlock));

    id = lrg_unlock_def_get_id (unlock);
    g_return_if_fail (id != NULL);

    g_hash_table_insert (self->unlocks,
                         g_strdup (id),
                         g_object_ref (unlock));
}

/**
 * lrg_deckbuilder_manager_get_unlock:
 * @self: a #LrgDeckbuilderManager
 * @id: unlock ID
 *
 * Gets an unlock by ID.
 *
 * Returns: (transfer none) (nullable): the #LrgUnlockDef, or %NULL
 *
 * Since: 1.0
 */
LrgUnlockDef *
lrg_deckbuilder_manager_get_unlock (LrgDeckbuilderManager *self,
                                     const gchar           *id)
{
    g_return_val_if_fail (LRG_IS_DECKBUILDER_MANAGER (self), NULL);
    g_return_val_if_fail (id != NULL, NULL);

    return g_hash_table_lookup (self->unlocks, id);
}

/**
 * lrg_deckbuilder_manager_check_unlocks:
 * @self: a #LrgDeckbuilderManager
 *
 * Checks all unlock conditions and grants any met unlocks.
 *
 * Returns: (transfer full) (element-type LrgUnlockDef): newly granted unlocks
 *
 * Since: 1.0
 */
GPtrArray *
lrg_deckbuilder_manager_check_unlocks (LrgDeckbuilderManager *self)
{
    GPtrArray      *granted;
    GHashTableIter  iter;
    gpointer        value;

    g_return_val_if_fail (LRG_IS_DECKBUILDER_MANAGER (self), NULL);

    granted = g_ptr_array_new_with_free_func (g_object_unref);

    g_hash_table_iter_init (&iter, self->unlocks);
    while (g_hash_table_iter_next (&iter, NULL, &value))
    {
        LrgUnlockDef *unlock;

        unlock = LRG_UNLOCK_DEF (value);

        if (lrg_unlock_def_grant (unlock, self->profile))
        {
            g_ptr_array_add (granted, g_object_ref (unlock));
            g_signal_emit (self, signals[SIGNAL_UNLOCK_GRANTED], 0, unlock);
        }
    }

    return granted;
}

/* ==========================================================================
 * Public API - Ascension
 * ========================================================================== */

/**
 * lrg_deckbuilder_manager_get_ascension:
 * @self: a #LrgDeckbuilderManager
 * @level: ascension level (0-20)
 *
 * Gets or creates an ascension configuration for a level.
 *
 * Returns: (transfer none): the #LrgAscension for that level
 *
 * Since: 1.0
 */
LrgAscension *
lrg_deckbuilder_manager_get_ascension (LrgDeckbuilderManager *self,
                                        gint                   level)
{
    LrgAscension *ascension;

    g_return_val_if_fail (LRG_IS_DECKBUILDER_MANAGER (self), NULL);

    level = CLAMP (level, 0, LRG_ASCENSION_MAX_LEVEL);

    ascension = g_hash_table_lookup (self->ascensions,
                                      GINT_TO_POINTER (level));
    if (ascension == NULL)
    {
        ascension = lrg_ascension_new_default (level);
        g_hash_table_insert (self->ascensions,
                             GINT_TO_POINTER (level),
                             ascension);
    }

    return ascension;
}

/**
 * lrg_deckbuilder_manager_get_max_ascension:
 * @self: a #LrgDeckbuilderManager
 * @character_id: character ID
 *
 * Gets the max unlocked ascension for a character.
 *
 * Returns: max ascension level
 *
 * Since: 1.0
 */
gint
lrg_deckbuilder_manager_get_max_ascension (LrgDeckbuilderManager *self,
                                            const gchar           *character_id)
{
    g_return_val_if_fail (LRG_IS_DECKBUILDER_MANAGER (self), 0);
    g_return_val_if_fail (character_id != NULL, 0);

    return lrg_player_profile_get_max_ascension (self->profile, character_id);
}

/* ==========================================================================
 * Public API - Run Management
 * ========================================================================== */

/**
 * lrg_deckbuilder_manager_get_current_run:
 * @self: a #LrgDeckbuilderManager
 *
 * Gets the current active run.
 *
 * Returns: (transfer none) (nullable): the current #LrgRun, or %NULL
 *
 * Since: 1.0
 */
LrgRun *
lrg_deckbuilder_manager_get_current_run (LrgDeckbuilderManager *self)
{
    g_return_val_if_fail (LRG_IS_DECKBUILDER_MANAGER (self), NULL);

    return self->current_run;
}

/**
 * lrg_deckbuilder_manager_start_run:
 * @self: a #LrgDeckbuilderManager
 * @character_id: character to play
 * @ascension_level: ascension level (0 = normal)
 * @seed: (nullable): seed for RNG, or %NULL for random
 *
 * Starts a new run.
 *
 * Returns: (transfer none): the new #LrgRun
 *
 * Since: 1.0
 */
LrgRun *
lrg_deckbuilder_manager_start_run (LrgDeckbuilderManager *self,
                                    const gchar           *character_id,
                                    gint                   ascension_level,
                                    const gchar           *seed)
{
    LrgCharacterDef *character;
    LrgAscension    *ascension;
    guint64          run_seed;

    g_return_val_if_fail (LRG_IS_DECKBUILDER_MANAGER (self), NULL);
    g_return_val_if_fail (character_id != NULL, NULL);

    /* Abort any current run */
    if (self->current_run != NULL)
    {
        lrg_deckbuilder_manager_abandon_run (self);
    }

    /* Get character */
    character = lrg_deckbuilder_manager_get_character (self, character_id);
    if (character == NULL)
    {
        lrg_warning (LRG_LOG_DOMAIN_DECKBUILDER,
                     "Unknown character: %s", character_id);
        return NULL;
    }

    /* Get ascension config (stored for reference) */
    ascension = lrg_deckbuilder_manager_get_ascension (self, ascension_level);
    (void) ascension;  /* Currently unused by LrgRun */

    /* Generate seed from string or use random */
    if (seed != NULL)
    {
        run_seed = g_str_hash (seed);
    }
    else
    {
        run_seed = g_random_int ();
        run_seed = (run_seed << 32) | g_random_int ();
    }

    /* Create run */
    self->current_run = lrg_run_new (character_id, run_seed);

    /* Record run start */
    lrg_player_profile_add_character_run (self->profile, character_id);

    /* Notify character */
    lrg_character_def_on_run_start (character, self->current_run);

    /* Emit signal */
    g_signal_emit (self, signals[SIGNAL_RUN_STARTED], 0, self->current_run);

    return self->current_run;
}

/**
 * lrg_deckbuilder_manager_end_run:
 * @self: a #LrgDeckbuilderManager
 * @victory: whether the run was won
 *
 * Ends the current run.
 *
 * Since: 1.0
 */
void
lrg_deckbuilder_manager_end_run (LrgDeckbuilderManager *self,
                                  gboolean               victory)
{
    LrgCharacterDef *character;
    const gchar     *character_id;

    g_return_if_fail (LRG_IS_DECKBUILDER_MANAGER (self));
    g_return_if_fail (self->current_run != NULL);

    /* Get run info */
    character_id = lrg_run_get_character_id (self->current_run);
    character = lrg_deckbuilder_manager_get_character (self, character_id);

    /* Record victory */
    if (victory)
    {
        lrg_player_profile_add_character_win (self->profile, character_id);

        /* Unlock next ascension on victory */
        lrg_player_profile_unlock_next_ascension (self->profile, character_id);
    }

    /* Notify character if found */
    if (character != NULL)
    {
        lrg_character_def_on_run_end (character, self->current_run, victory);
    }

    /* Emit signal */
    g_signal_emit (self, signals[SIGNAL_RUN_ENDED], 0, self->current_run, victory);

    /* Check for new unlocks */
    lrg_deckbuilder_manager_check_unlocks (self);

    /* Clear run */
    g_clear_object (&self->current_run);
}

/**
 * lrg_deckbuilder_manager_abandon_run:
 * @self: a #LrgDeckbuilderManager
 *
 * Abandons the current run without completing it.
 *
 * Since: 1.0
 */
void
lrg_deckbuilder_manager_abandon_run (LrgDeckbuilderManager *self)
{
    g_return_if_fail (LRG_IS_DECKBUILDER_MANAGER (self));

    if (self->current_run != NULL)
    {
        lrg_deckbuilder_manager_end_run (self, FALSE);
    }
}

/* ==========================================================================
 * Public API - Statistics
 * ========================================================================== */

/**
 * lrg_deckbuilder_manager_get_run_count:
 * @self: a #LrgDeckbuilderManager
 * @character_id: (nullable): character ID, or %NULL for total
 *
 * Gets run count.
 *
 * Returns: number of runs
 *
 * Since: 1.0
 */
gint
lrg_deckbuilder_manager_get_run_count (LrgDeckbuilderManager *self,
                                        const gchar           *character_id)
{
    g_return_val_if_fail (LRG_IS_DECKBUILDER_MANAGER (self), 0);

    if (character_id != NULL)
        return lrg_player_profile_get_character_runs (self->profile, character_id);
    else
        return lrg_player_profile_get_total_runs (self->profile);
}

/**
 * lrg_deckbuilder_manager_get_win_count:
 * @self: a #LrgDeckbuilderManager
 * @character_id: (nullable): character ID, or %NULL for total
 *
 * Gets win count.
 *
 * Returns: number of wins
 *
 * Since: 1.0
 */
gint
lrg_deckbuilder_manager_get_win_count (LrgDeckbuilderManager *self,
                                        const gchar           *character_id)
{
    g_return_val_if_fail (LRG_IS_DECKBUILDER_MANAGER (self), 0);

    if (character_id != NULL)
        return lrg_player_profile_get_character_wins (self->profile, character_id);
    else
        return lrg_player_profile_get_total_wins (self->profile);
}

/**
 * lrg_deckbuilder_manager_get_win_rate:
 * @self: a #LrgDeckbuilderManager
 * @character_id: (nullable): character ID, or %NULL for total
 *
 * Gets win rate as a percentage.
 *
 * Returns: win rate (0.0 - 100.0)
 *
 * Since: 1.0
 */
gfloat
lrg_deckbuilder_manager_get_win_rate (LrgDeckbuilderManager *self,
                                       const gchar           *character_id)
{
    gint runs;
    gint wins;

    g_return_val_if_fail (LRG_IS_DECKBUILDER_MANAGER (self), 0.0f);

    runs = lrg_deckbuilder_manager_get_run_count (self, character_id);
    wins = lrg_deckbuilder_manager_get_win_count (self, character_id);

    if (runs == 0)
        return 0.0f;

    return (gfloat)wins / (gfloat)runs * 100.0f;
}

/* ==========================================================================
 * Public API - Persistence
 * ========================================================================== */

/**
 * lrg_deckbuilder_manager_save:
 * @self: a #LrgDeckbuilderManager
 * @error: (nullable): return location for error
 *
 * Saves all profile data.
 *
 * Returns: %TRUE on success
 *
 * Since: 1.0
 */
gboolean
lrg_deckbuilder_manager_save (LrgDeckbuilderManager  *self,
                               GError                **error)
{
    g_autoptr(LrgSaveContext) context = NULL;
    g_autofree gchar *save_path = NULL;

    g_return_val_if_fail (LRG_IS_DECKBUILDER_MANAGER (self), FALSE);

    /* Create save context and serialize profile via LrgSaveable interface */
    context = lrg_save_context_new_for_save ();

    if (!lrg_saveable_save (LRG_SAVEABLE (self->profile), context, error))
        return FALSE;

    /* Write to profile save file in user data directory */
    save_path = g_build_filename (g_get_user_data_dir (),
                                  "libregnum", "profile.sav", NULL);

    /* Ensure the directory exists */
    {
        g_autofree gchar *dir = NULL;

        dir = g_path_get_dirname (save_path);
        if (g_mkdir_with_parents (dir, 0755) != 0)
        {
            g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                         "Failed to create save directory: %s", dir);
            return FALSE;
        }
    }

    if (!lrg_save_context_to_file (context, save_path, error))
        return FALSE;

    lrg_player_profile_mark_clean (self->profile);
    lrg_info (LRG_LOG_DOMAIN_DECKBUILDER, "Profile saved to: %s", save_path);

    return TRUE;
}

/**
 * lrg_deckbuilder_manager_load:
 * @self: a #LrgDeckbuilderManager
 * @error: (nullable): return location for error
 *
 * Loads profile data.
 *
 * Returns: %TRUE on success
 *
 * Since: 1.0
 */
gboolean
lrg_deckbuilder_manager_load (LrgDeckbuilderManager  *self,
                               GError                **error)
{
    g_autoptr(LrgSaveContext) context = NULL;
    g_autofree gchar *save_path = NULL;

    g_return_val_if_fail (LRG_IS_DECKBUILDER_MANAGER (self), FALSE);

    /* Build path to profile save file */
    save_path = g_build_filename (g_get_user_data_dir (),
                                  "libregnum", "profile.sav", NULL);

    /* If no save file exists, silently succeed with defaults */
    if (!g_file_test (save_path, G_FILE_TEST_EXISTS))
    {
        lrg_info (LRG_LOG_DOMAIN_DECKBUILDER,
                  "No profile save file found at: %s", save_path);
        return TRUE;
    }

    /* Load save context from file */
    context = lrg_save_context_new_from_file (save_path, error);
    if (context == NULL)
        return FALSE;

    /* Deserialize profile via LrgSaveable interface */
    if (!lrg_saveable_load (LRG_SAVEABLE (self->profile), context, error))
        return FALSE;

    lrg_info (LRG_LOG_DOMAIN_DECKBUILDER, "Profile loaded from: %s", save_path);

    return TRUE;
}
