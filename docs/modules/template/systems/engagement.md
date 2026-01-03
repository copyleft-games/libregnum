# Engagement Systems

The template system provides three interconnected systems for player engagement and retention: statistics tracking, daily rewards, and dynamic difficulty adjustment.

## Components

| Type | Description |
|------|-------------|
| `LrgTemplateStatistics` | Track game metrics and player behavior |
| `LrgTemplateDailyRewards` | Daily login rewards with streak bonuses |
| `LrgTemplateDifficulty` | Dynamic difficulty based on player performance |

---

## LrgTemplateStatistics

Statistics tracking allows games to record and persist player metrics for achievements, analytics, and gameplay systems.

### Stat Types

```c
typedef enum {
    LRG_STAT_TYPE_COUNTER,   /* Cumulative count (e.g., enemies killed) */
    LRG_STAT_TYPE_MAXIMUM,   /* Highest value (e.g., high score) */
    LRG_STAT_TYPE_MINIMUM,   /* Lowest value (e.g., fastest time) */
    LRG_STAT_TYPE_TIMER      /* Time accumulator (e.g., play time) */
} LrgStatType;
```

### Registration

```c
LrgTemplateStatistics *stats = lrg_template_statistics_new ();

/* Register stats with their types */
lrg_template_statistics_register (stats, "enemies_killed", LRG_STAT_TYPE_COUNTER);
lrg_template_statistics_register (stats, "high_score", LRG_STAT_TYPE_MAXIMUM);
lrg_template_statistics_register (stats, "fastest_level", LRG_STAT_TYPE_MINIMUM);
lrg_template_statistics_register (stats, "total_play_time", LRG_STAT_TYPE_TIMER);

/* Register with initial value */
lrg_template_statistics_register_with_default (stats, "best_combo",
                                                LRG_STAT_TYPE_MAXIMUM, 0);
```

### Recording Values

```c
/* Counter - always increments */
lrg_template_statistics_increment (stats, "enemies_killed");
lrg_template_statistics_add (stats, "enemies_killed", 5);

/* Maximum - only updates if higher */
lrg_template_statistics_set_max (stats, "high_score", current_score);

/* Minimum - only updates if lower */
lrg_template_statistics_set_min (stats, "fastest_level", completion_time);

/* Timer - call during update loop */
lrg_template_statistics_add_time (stats, "total_play_time", delta);
```

### Querying Values

```c
/* Get current value */
gint64 kills = lrg_template_statistics_get_value (stats, "enemies_killed");
gint64 high = lrg_template_statistics_get_value (stats, "high_score");

/* Check if stat exists */
if (lrg_template_statistics_has_stat (stats, "secret_found"))
{
    /* ... */
}

/* Get stat type */
LrgStatType type = lrg_template_statistics_get_type (stats, "enemies_killed");
```

### Persistence

```c
/* Save all statistics */
lrg_template_statistics_save (stats, "player_stats.yaml", &error);

/* Load statistics */
lrg_template_statistics_load (stats, "player_stats.yaml", &error);

/* Reset all stats */
lrg_template_statistics_reset_all (stats);

/* Reset specific stat */
lrg_template_statistics_reset (stats, "enemies_killed");
```

### Session Tracking

```c
/* Track per-session vs lifetime stats */
lrg_template_statistics_register (stats, "session_kills", LRG_STAT_TYPE_COUNTER);
lrg_template_statistics_register (stats, "lifetime_kills", LRG_STAT_TYPE_COUNTER);

/* On enemy kill */
lrg_template_statistics_increment (stats, "session_kills");
lrg_template_statistics_increment (stats, "lifetime_kills");

/* Reset session stats on new game */
lrg_template_statistics_reset (stats, "session_kills");
```

### Integration with Achievements

```c
static void
on_stat_changed (LrgTemplateStatistics *stats,
                 const gchar           *stat_name,
                 gint64                 new_value,
                 gpointer               user_data)
{
    LrgAchievementManager *achievements = user_data;

    if (g_str_equal (stat_name, "enemies_killed"))
    {
        if (new_value >= 100)
            lrg_achievement_manager_unlock (achievements, "kill_100");
        if (new_value >= 1000)
            lrg_achievement_manager_unlock (achievements, "kill_1000");
    }
}

g_signal_connect (stats, "stat-changed", G_CALLBACK (on_stat_changed), achievements);
```

---

## LrgTemplateDailyRewards

The daily rewards system encourages regular play through login rewards and streak bonuses.

### Setup

```c
LrgTemplateDailyRewards *rewards = lrg_template_daily_rewards_new ();

/* Configure streak settings */
lrg_template_daily_rewards_set_streak_reset_hours (rewards, 48);  /* 48 hours to claim */
lrg_template_daily_rewards_set_max_streak (rewards, 7);           /* 7-day cycle */
```

### Defining Rewards

```c
/* Add rewards for each day of the streak */
lrg_template_daily_rewards_add_reward (rewards, 1, "coins", 100);
lrg_template_daily_rewards_add_reward (rewards, 2, "coins", 150);
lrg_template_daily_rewards_add_reward (rewards, 3, "coins", 200);
lrg_template_daily_rewards_add_reward (rewards, 4, "gems", 5);
lrg_template_daily_rewards_add_reward (rewards, 5, "coins", 300);
lrg_template_daily_rewards_add_reward (rewards, 6, "gems", 10);
lrg_template_daily_rewards_add_reward (rewards, 7, "legendary_crate", 1);  /* Day 7 jackpot */
```

### Checking and Claiming

```c
/* Check if reward is available */
if (lrg_template_daily_rewards_is_available (rewards))
{
    /* Get current streak day (1-7) */
    guint day = lrg_template_daily_rewards_get_current_day (rewards);

    /* Get reward info */
    const gchar *type = lrg_template_daily_rewards_get_reward_type (rewards, day);
    gint64 amount = lrg_template_daily_rewards_get_reward_amount (rewards, day);

    /* Show reward UI */
    show_reward_popup (type, amount, day);

    /* Claim the reward */
    lrg_template_daily_rewards_claim (rewards);
}
```

### Streak Information

```c
/* Get current streak count */
guint streak = lrg_template_daily_rewards_get_streak (rewards);

/* Get time until next reward (or until streak expires) */
gint64 seconds = lrg_template_daily_rewards_get_seconds_until_next (rewards);
gint64 expire_seconds = lrg_template_daily_rewards_get_seconds_until_expire (rewards);

/* Check if streak is about to expire */
if (expire_seconds < 3600)  /* Less than 1 hour */
{
    show_notification ("Claim your daily reward before your streak resets!");
}
```

### Persistence

```c
/* Save reward state */
lrg_template_daily_rewards_save (rewards, "daily_rewards.yaml", &error);

/* Load reward state */
lrg_template_daily_rewards_load (rewards, "daily_rewards.yaml", &error);
```

### Anti-Cheat Measures

The system includes protection against time manipulation:

```c
/* Uses server time when available */
lrg_template_daily_rewards_set_time_source (rewards, LRG_TIME_SOURCE_SERVER);

/* Falls back to monotonic clock checks */
lrg_template_daily_rewards_set_time_source (rewards, LRG_TIME_SOURCE_LOCAL);

/* Time anomaly detection */
g_signal_connect (rewards, "time-anomaly-detected",
                  G_CALLBACK (on_time_cheat_detected), NULL);
```

---

## LrgTemplateDifficulty

Dynamic Difficulty Adjustment (DDA) automatically tunes game difficulty based on player performance.

### Interface

`LrgTemplateDifficulty` is an interface that templates can implement:

```c
struct _LrgTemplateDifficultyInterface
{
    GTypeInterface parent_iface;

    /* Get current difficulty level (0.0 = easiest, 1.0 = hardest) */
    gfloat (*get_difficulty)    (LrgTemplateDifficulty *self);

    /* Set difficulty level */
    void   (*set_difficulty)    (LrgTemplateDifficulty *self,
                                 gfloat                 level);

    /* Adjust difficulty based on performance */
    void   (*adjust_difficulty) (LrgTemplateDifficulty *self,
                                 LrgPerformanceMetric   metric,
                                 gfloat                 value);

    /* Get difficulty multiplier for a specific aspect */
    gfloat (*get_multiplier)    (LrgTemplateDifficulty *self,
                                 const gchar           *aspect);
};
```

### Performance Metrics

```c
typedef enum {
    LRG_METRIC_DEATH,           /* Player died */
    LRG_METRIC_DAMAGE_TAKEN,    /* Damage received */
    LRG_METRIC_TIME_TO_KILL,    /* How long to defeat enemies */
    LRG_METRIC_ACCURACY,        /* Hit/miss ratio */
    LRG_METRIC_COMBO_BROKEN,    /* Combo chain ended */
    LRG_METRIC_VICTORY,         /* Player succeeded */
    LRG_METRIC_FLAWLESS,        /* Perfect performance */
    LRG_METRIC_CUSTOM           /* Game-specific metric */
} LrgPerformanceMetric;
```

### Adjusting Difficulty

```c
/* Report player performance events */
static void
on_player_death (Game *game)
{
    LrgTemplateDifficulty *dda = LRG_TEMPLATE_DIFFICULTY (game->template);

    /* Death makes game easier */
    lrg_template_difficulty_adjust (dda, LRG_METRIC_DEATH, 1.0f);
}

static void
on_enemy_killed (Game *game, Enemy *enemy, gfloat time_to_kill)
{
    LrgTemplateDifficulty *dda = LRG_TEMPLATE_DIFFICULTY (game->template);

    /* Fast kills suggest player is doing well */
    if (time_to_kill < 2.0f)
        lrg_template_difficulty_adjust (dda, LRG_METRIC_TIME_TO_KILL, 0.5f);
    else
        lrg_template_difficulty_adjust (dda, LRG_METRIC_TIME_TO_KILL, time_to_kill);
}
```

### Applying Difficulty

```c
static gfloat
get_enemy_health (Game *game, EnemyType type)
{
    LrgTemplateDifficulty *dda = LRG_TEMPLATE_DIFFICULTY (game->template);
    gfloat base_health = enemy_type_get_base_health (type);

    /* Get enemy health multiplier based on current difficulty */
    gfloat mult = lrg_template_difficulty_get_multiplier (dda, "enemy_health");

    return base_health * mult;
}

static gfloat
get_enemy_damage (Game *game, EnemyType type)
{
    LrgTemplateDifficulty *dda = LRG_TEMPLATE_DIFFICULTY (game->template);
    gfloat base_damage = enemy_type_get_base_damage (type);

    gfloat mult = lrg_template_difficulty_get_multiplier (dda, "enemy_damage");

    return base_damage * mult;
}
```

### Implementing DDA in a Template

```c
typedef struct {
    gfloat difficulty;        /* 0.0 to 1.0 */
    gfloat adjustment_rate;   /* How fast difficulty changes */
    guint death_count;
    guint victory_count;
} DifficultyState;

static void
my_template_adjust_difficulty (LrgTemplateDifficulty *difficulty,
                               LrgPerformanceMetric   metric,
                               gfloat                 value)
{
    MyTemplate *self = MY_TEMPLATE (difficulty);
    DifficultyState *state = self->difficulty_state;

    gfloat adjustment = 0.0f;

    switch (metric)
    {
        case LRG_METRIC_DEATH:
            state->death_count++;
            adjustment = -0.05f;  /* Make easier */
            break;

        case LRG_METRIC_VICTORY:
            state->victory_count++;
            adjustment = 0.02f;   /* Make slightly harder */
            break;

        case LRG_METRIC_FLAWLESS:
            adjustment = 0.05f;   /* Make harder */
            break;

        case LRG_METRIC_TIME_TO_KILL:
            if (value < 2.0f)
                adjustment = 0.01f;   /* Fast kills = make harder */
            else if (value > 10.0f)
                adjustment = -0.01f;  /* Slow kills = make easier */
            break;

        default:
            break;
    }

    state->difficulty = CLAMP (state->difficulty + adjustment, 0.0f, 1.0f);
}

static gfloat
my_template_get_multiplier (LrgTemplateDifficulty *difficulty,
                            const gchar           *aspect)
{
    MyTemplate *self = MY_TEMPLATE (difficulty);
    gfloat diff = self->difficulty_state->difficulty;

    if (g_str_equal (aspect, "enemy_health"))
        return 0.5f + diff * 1.0f;  /* 0.5x to 1.5x */

    if (g_str_equal (aspect, "enemy_damage"))
        return 0.75f + diff * 0.5f;  /* 0.75x to 1.25x */

    if (g_str_equal (aspect, "enemy_count"))
        return 0.8f + diff * 0.4f;  /* 0.8x to 1.2x */

    if (g_str_equal (aspect, "resource_drop"))
        return 1.5f - diff * 0.75f;  /* 1.5x to 0.75x (inverse) */

    return 1.0f;  /* Unknown aspect - no modification */
}
```

### Difficulty Persistence

```c
/* Save difficulty state with save game */
static gboolean
save_game (Game *game, LrgSaveContext *ctx, GError **error)
{
    LrgTemplateDifficulty *dda = LRG_TEMPLATE_DIFFICULTY (game->template);

    gfloat difficulty = lrg_template_difficulty_get_difficulty (dda);
    lrg_save_context_write_float (ctx, "difficulty", difficulty);

    return TRUE;
}

/* Load difficulty with save game */
static gboolean
load_game (Game *game, LrgSaveContext *ctx, GError **error)
{
    LrgTemplateDifficulty *dda = LRG_TEMPLATE_DIFFICULTY (game->template);

    gfloat difficulty = lrg_save_context_read_float (ctx, "difficulty");
    lrg_template_difficulty_set_difficulty (dda, difficulty);

    return TRUE;
}
```

---

## Combining All Three Systems

```c
typedef struct {
    LrgTemplateStatistics *stats;
    LrgTemplateDailyRewards *daily;
    /* DDA is part of the template via interface */
} EngagementSystems;

static void
game_init_engagement (Game *game)
{
    EngagementSystems *eng = g_new0 (EngagementSystems, 1);

    /* Statistics */
    eng->stats = lrg_template_statistics_new ();
    lrg_template_statistics_register (eng->stats, "total_deaths", LRG_STAT_TYPE_COUNTER);
    lrg_template_statistics_register (eng->stats, "total_kills", LRG_STAT_TYPE_COUNTER);
    lrg_template_statistics_register (eng->stats, "play_time", LRG_STAT_TYPE_TIMER);
    lrg_template_statistics_register (eng->stats, "high_score", LRG_STAT_TYPE_MAXIMUM);

    /* Daily rewards */
    eng->daily = lrg_template_daily_rewards_new ();
    lrg_template_daily_rewards_set_max_streak (eng->daily, 7);
    for (guint i = 1; i <= 7; i++)
        lrg_template_daily_rewards_add_reward (eng->daily, i, "coins", i * 100);

    /* Load persisted state */
    lrg_template_statistics_load (eng->stats, "stats.yaml", NULL);
    lrg_template_daily_rewards_load (eng->daily, "daily.yaml", NULL);

    game->engagement = eng;
}

static void
game_on_player_death (Game *game)
{
    EngagementSystems *eng = game->engagement;

    /* Update statistics */
    lrg_template_statistics_increment (eng->stats, "total_deaths");

    /* Adjust difficulty */
    LrgTemplateDifficulty *dda = LRG_TEMPLATE_DIFFICULTY (game->template);
    lrg_template_difficulty_adjust (dda, LRG_METRIC_DEATH, 1.0f);
}

static void
game_on_main_menu (Game *game)
{
    EngagementSystems *eng = game->engagement;

    /* Check for daily reward */
    if (lrg_template_daily_rewards_is_available (eng->daily))
    {
        show_daily_reward_popup (game);
    }
}

static void
game_shutdown_engagement (Game *game)
{
    EngagementSystems *eng = game->engagement;

    /* Persist state */
    lrg_template_statistics_save (eng->stats, "stats.yaml", NULL);
    lrg_template_daily_rewards_save (eng->daily, "daily.yaml", NULL);

    g_clear_object (&eng->stats);
    g_clear_object (&eng->daily);
    g_free (eng);
}
```

## Related Documentation

- [LrgAchievementManager](../../achievement/index.md) - Achievement system
- [Save System](../../save/index.md) - Persistence
- [LrgAnalytics](../../analytics/index.md) - Telemetry integration
