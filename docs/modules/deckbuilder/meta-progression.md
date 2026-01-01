# Meta-Progression

Meta-progression encompasses systems that persist across runs: characters, unlocks, ascension levels, and player profiles.

## LrgCharacterDef

Character definitions define playable characters with unique starting decks, relics, and card pools.

### Properties

| Property | Type | Description |
|----------|------|-------------|
| `id` | `gchar*` | Unique character identifier |
| `name` | `gchar*` | Display name |
| `description` | `gchar*` | Character description |
| `portrait` | `gchar*` | Character portrait path |
| `starting-health` | `gint` | Starting max HP |
| `starting-gold` | `gint` | Starting gold |
| `starting-energy` | `gint` | Energy per turn |
| `unlocked` | `gboolean` | Is character unlocked |

### Virtual Methods

```c
struct _LrgCharacterDefClass
{
    GObjectClass parent_class;

    /* Get starting health (may vary by ascension) */
    gint (*get_starting_health) (LrgCharacterDef *self);

    /* Get starting gold */
    gint (*get_starting_gold)   (LrgCharacterDef *self);

    /* Get energy per turn */
    gint (*get_starting_energy) (LrgCharacterDef *self);

    /* Get starting deck card IDs */
    GPtrArray * (*get_starting_deck)   (LrgCharacterDef *self);

    /* Get starting relic IDs */
    GPtrArray * (*get_starting_relics) (LrgCharacterDef *self);

    /* Get available card pool */
    GPtrArray * (*get_card_pool)       (LrgCharacterDef *self);

    /* Get colorless card pool */
    GPtrArray * (*get_colorless_pool)  (LrgCharacterDef *self);

    /* Check for special resource (e.g., Orbs, Stance) */
    gboolean (*has_special_resource)   (LrgCharacterDef *self);

    /* Get special resource definition */
    LrgResource * (*get_special_resource) (LrgCharacterDef *self);

    gpointer _reserved[8];
};
```

### Creating Characters

```c
/* Create character */
g_autoptr(LrgCharacterDef) ironclad = lrg_character_def_new ("ironclad");
lrg_character_def_set_name (ironclad, "The Ironclad");
lrg_character_def_set_description (ironclad,
    "A war veteran hardened by combat. Starts with the Burning Blood relic.");
lrg_character_def_set_starting_health (ironclad, 80);
lrg_character_def_set_starting_gold (ironclad, 99);
lrg_character_def_set_starting_energy (ironclad, 3);

/* Set starting deck */
lrg_character_def_add_starting_card (ironclad, "strike", 5);
lrg_character_def_add_starting_card (ironclad, "defend", 4);
lrg_character_def_add_starting_card (ironclad, "bash", 1);

/* Set starting relic */
lrg_character_def_add_starting_relic (ironclad, "burning-blood");

/* Set card pool */
lrg_character_def_add_to_card_pool (ironclad, "anger");
lrg_character_def_add_to_card_pool (ironclad, "armaments");
/* ... */
```

## LrgPlayerProfile

Player profile stores persistent progression data. It implements `LrgSaveable`.

### Properties

| Property | Type | Description |
|----------|------|-------------|
| `total-runs` | `guint` | Total runs started |
| `victories` | `guint` | Total victories |
| `highest-ascension` | `GHashTable*` | Highest ascension per character |
| `unlocks` | `GPtrArray*` | Unlocked content IDs |
| `statistics` | `GHashTable*` | Gameplay statistics |

### Profile Operations

```c
/* Get profile (singleton) */
LrgPlayerProfile *profile = lrg_player_profile_get_default ();

/* Check unlock status */
gboolean unlocked = lrg_player_profile_is_unlocked (profile, "silent");

/* Unlock content */
lrg_player_profile_unlock (profile, "silent");

/* Get character progress */
gint highest_asc = lrg_player_profile_get_highest_ascension (profile, "ironclad");
guint char_wins = lrg_player_profile_get_character_victories (profile, "ironclad");

/* Update statistics */
lrg_player_profile_increment_stat (profile, "cards_played", 1);
lrg_player_profile_add_stat (profile, "damage_dealt", 150);

/* High scores */
lrg_player_profile_record_high_score (profile, "ironclad", 0, score);
gint64 best = lrg_player_profile_get_high_score (profile, "ironclad", 0);

/* Save profile */
lrg_player_profile_save (profile, &error);
```

### Statistics Tracking

```c
/* Common statistics */
lrg_player_profile_increment_stat (profile, "runs_started", 1);
lrg_player_profile_increment_stat (profile, "runs_completed", 1);
lrg_player_profile_increment_stat (profile, "cards_played", cards);
lrg_player_profile_add_stat (profile, "damage_dealt", damage);
lrg_player_profile_add_stat (profile, "damage_taken", damage);
lrg_player_profile_add_stat (profile, "gold_earned", gold);
lrg_player_profile_add_stat (profile, "cards_added", 1);
lrg_player_profile_add_stat (profile, "cards_removed", 1);
lrg_player_profile_add_stat (profile, "cards_upgraded", 1);
lrg_player_profile_add_stat (profile, "relics_obtained", 1);
lrg_player_profile_add_stat (profile, "potions_used", 1);
lrg_player_profile_add_stat (profile, "bosses_killed", 1);
lrg_player_profile_add_stat (profile, "elites_killed", 1);
lrg_player_profile_add_stat (profile, "perfect_bosses", 1);

/* Get statistics */
guint64 total_damage = lrg_player_profile_get_stat (profile, "damage_dealt");
```

## LrgUnlockDef

Unlock definitions define conditions for unlocking content.

### Properties

| Property | Type | Description |
|----------|------|-------------|
| `id` | `gchar*` | Unique unlock identifier |
| `name` | `gchar*` | Display name |
| `description` | `gchar*` | How to unlock |
| `unlock-type` | `LrgUnlockType` | What is unlocked |
| `unlock-id` | `gchar*` | ID of unlocked content |

### Unlock Types

```c
typedef enum {
    LRG_UNLOCK_TYPE_CHARACTER,  /* Unlock a character */
    LRG_UNLOCK_TYPE_CARD,       /* Unlock a card */
    LRG_UNLOCK_TYPE_RELIC,      /* Unlock a relic */
    LRG_UNLOCK_TYPE_POTION,     /* Unlock a potion */
    LRG_UNLOCK_TYPE_JOKER,      /* Unlock a joker */
    LRG_UNLOCK_TYPE_COSMETIC    /* Unlock a cosmetic */
} LrgUnlockType;
```

### Virtual Methods

```c
struct _LrgUnlockDefClass
{
    GObjectClass parent_class;

    /* Check if unlock condition is met */
    gboolean (*check_condition) (LrgUnlockDef    *self,
                                 LrgPlayerProfile *profile);

    /* Called when unlocked */
    void (*on_unlock) (LrgUnlockDef    *self,
                       LrgPlayerProfile *profile);

    /* Get hint for how to unlock */
    gchar * (*get_hint) (LrgUnlockDef *self);

    gpointer _reserved[8];
};
```

### Creating Unlocks

```c
/* Win-based unlock */
g_autoptr(LrgUnlockDef) unlock = lrg_unlock_def_new ("unlock_silent");
lrg_unlock_def_set_name (unlock, "The Silent");
lrg_unlock_def_set_description (unlock, "Defeat the Act 3 Boss with the Ironclad.");
lrg_unlock_def_set_unlock_type (unlock, LRG_UNLOCK_TYPE_CHARACTER);
lrg_unlock_def_set_unlock_id (unlock, "silent");
lrg_unlock_def_set_condition_type (unlock, LRG_UNLOCK_CONDITION_WIN);
lrg_unlock_def_set_condition_character (unlock, "ironclad");

/* Statistic-based unlock */
g_autoptr(LrgUnlockDef) card_unlock = lrg_unlock_def_new ("unlock_barricade");
lrg_unlock_def_set_condition_type (card_unlock, LRG_UNLOCK_CONDITION_STAT);
lrg_unlock_def_set_condition_stat (card_unlock, "block_gained", 1000);
```

### Custom Unlock Conditions

```c
G_DECLARE_FINAL_TYPE (MyUnlock, my_unlock, MY, UNLOCK, LrgUnlockDef)

static gboolean
my_unlock_check_condition (LrgUnlockDef    *def,
                           LrgPlayerProfile *profile)
{
    /* Custom condition: win with exactly 1 HP */
    return lrg_player_profile_get_stat (profile, "one_hp_victories") >= 1;
}
```

## LrgAscension

Ascension provides increasing difficulty through cumulative modifiers.

### Properties

| Property | Type | Description |
|----------|------|-------------|
| `level` | `gint` | Ascension level (0-20) |
| `modifiers` | `LrgAscensionModifier` | Active modifier flags |

### Ascension Modifiers

```c
typedef enum {
    LRG_ASCENSION_MODIFIER_NONE           = 0,
    LRG_ASCENSION_MODIFIER_HARDER_ELITES  = 1 << 0,   /* A1: Elites harder */
    LRG_ASCENSION_MODIFIER_CURSES         = 1 << 1,   /* A2: Start with curse */
    LRG_ASCENSION_MODIFIER_LESS_HEAL      = 1 << 2,   /* A3: Less rest healing */
    LRG_ASCENSION_MODIFIER_LESS_GOLD      = 1 << 3,   /* A4: Less starting gold */
    LRG_ASCENSION_MODIFIER_LESS_MAX_HP    = 1 << 4,   /* A6: Less max HP */
    LRG_ASCENSION_MODIFIER_ELITE_HP       = 1 << 5,   /* A7: Elites have more HP */
    LRG_ASCENSION_MODIFIER_HARDER_BOSSES  = 1 << 6,   /* A10: Bosses harder */
    LRG_ASCENSION_MODIFIER_BOSS_HP        = 1 << 7,   /* A15: Bosses have more HP */
    LRG_ASCENSION_MODIFIER_EVEN_LESS_HEAL = 1 << 8,   /* A17: Even less healing */
    LRG_ASCENSION_MODIFIER_EVEN_LESS_HP   = 1 << 9,   /* A18: Even less max HP */
    LRG_ASCENSION_MODIFIER_MORE_DAMAGE    = 1 << 10   /* A20: Enemies hit harder */
} LrgAscensionModifier;
```

### Ascension Effects by Level

| Level | Effect |
|-------|--------|
| A1 | Elites use harder attack patterns |
| A2 | Start with Ascender's Bane curse |
| A3 | Heal 25% less at rest sites |
| A4 | Start with 10% less gold |
| A5 | Heal 25% less between acts |
| A6 | Start with 10% less max HP |
| A7 | Elites have 25% more HP |
| A8 | Bosses use harder patterns |
| A9 | ? nodes have worse outcomes |
| A10 | Bosses have 25% more HP |
| A11 | Elites appear more often |
| A12 | Upgraded cards appear less often |
| A13 | Bosses have harder patterns |
| A14 | Events have worse outcomes |
| A15 | Bosses have 50% more HP |
| A16 | Card removal costs more |
| A17 | All healing reduced by 25% |
| A18 | Start with even less max HP |
| A19 | Elites have harder patterns |
| A20 | Enemies deal 10% more damage |

### Using Ascension

```c
/* Get ascension for level */
LrgAscension *asc = lrg_ascension_new (10);

/* Check modifiers */
if (lrg_ascension_has_modifier (asc, LRG_ASCENSION_MODIFIER_HARDER_ELITES))
{
    /* Use harder elite AI */
}

/* Apply ascension effects */
gint starting_hp = lrg_ascension_apply_starting_hp (asc, base_hp);
gint starting_gold = lrg_ascension_apply_starting_gold (asc, base_gold);
gint heal_amount = lrg_ascension_apply_heal (asc, base_heal);
gint enemy_hp = lrg_ascension_apply_enemy_hp (asc, base_hp, is_elite, is_boss);

/* Get name */
const gchar *name = lrg_ascension_get_name (asc);  /* "Ascension 10" */
```

## LrgDeckbuilderManager

The deckbuilder manager is the central coordinator singleton.

### Operations

```c
/* Get manager */
LrgDeckbuilderManager *manager = lrg_deckbuilder_manager_get_default ();

/* Character management */
lrg_deckbuilder_manager_register_character (manager, character);
LrgCharacterDef *character = lrg_deckbuilder_manager_get_character (manager, "ironclad");
GPtrArray *all_chars = lrg_deckbuilder_manager_get_all_characters (manager);

/* Unlock management */
lrg_deckbuilder_manager_register_unlock (manager, unlock);
lrg_deckbuilder_manager_check_unlocks (manager, profile);

/* Run management */
LrgRun *run = lrg_deckbuilder_manager_start_run (manager, "ironclad", 5, "seed123");
LrgRun *current = lrg_deckbuilder_manager_get_current_run (manager);
lrg_deckbuilder_manager_end_run (manager, victory);

/* Profile */
LrgPlayerProfile *profile = lrg_deckbuilder_manager_get_profile (manager);

/* Statistics */
lrg_deckbuilder_manager_record_run_stats (manager, run);
```

### Run Lifecycle

```c
/* Start a new run */
LrgRun *run = lrg_deckbuilder_manager_start_run (manager,
    "ironclad",  /* character */
    5,           /* ascension level */
    NULL);       /* NULL for random seed */

/* Run continues via combat/map/shop systems */
/* ... */

/* End run */
if (victory)
{
    /* Record victory */
    lrg_deckbuilder_manager_end_run (manager, TRUE);

    /* Check for unlocks */
    lrg_deckbuilder_manager_check_unlocks (manager, profile);

    /* Update ascension progress */
    gint current_asc = lrg_player_profile_get_highest_ascension (profile, "ironclad");
    if (run->ascension_level == current_asc)
    {
        lrg_player_profile_set_highest_ascension (profile, "ironclad", current_asc + 1);
    }
}
else
{
    /* Record defeat */
    lrg_deckbuilder_manager_end_run (manager, FALSE);
}
```

## See Also

- [Characters and Unlocks](modding.md#custom-characters)
- [Run Structure Documentation](run-structure.md)
- [Combat Documentation](combat.md)
