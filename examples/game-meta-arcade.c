/*
 * game-meta-arcade.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Phase 5 Example: Enhancements
 * Demonstrates: Analytics, Achievements, Photo Mode, Demo Mode
 *
 * A top-down space shooter with meta-game features.
 *
 * Controls:
 *   Arrow Keys - Move ship
 *   Space      - Shoot
 *   P          - Photo Mode
 *   Escape     - Pause / Exit Photo Mode
 */

#include <libregnum.h>
#include <graylib.h>
#include <math.h>

/* ===== Constants ===== */

#define SCREEN_WIDTH  800
#define SCREEN_HEIGHT 600
#define PLAYER_SPEED  300.0f
#define BULLET_SPEED  500.0f
#define MAX_BULLETS   50
#define MAX_ENEMIES   30
#define MAX_PARTICLES 100
#define COMBO_TIMEOUT 2.0f

/* Achievement IDs */
#define ACH_FIRST_BLOOD    "first-blood"
#define ACH_COMBO_MASTER   "combo-master"
#define ACH_WAVE1_COMPLETE "wave-1-complete"
#define ACH_WAVE5_COMPLETE "wave-5-complete"
#define ACH_BOSS_SLAYER    "boss-slayer"
#define ACH_NO_DEATHS      "no-deaths"
#define ACH_SPEEDRUNNER    "speedrunner"
#define ACH_COLLECTOR      "collector"
#define ACH_PACIFIST_WAVE  "pacifist-wave"
#define ACH_100_KILLS      "100-kills"
#define ACH_500_KILLS      "500-kills"
#define ACH_TRUE_DEFENDER  "true-defender"

/* Demo mode - only first 3 waves in demo */
#define DEMO_MAX_WAVE 3

/* ===== Types ===== */

typedef enum {
    GAME_STATE_CONSENT,
    GAME_STATE_MENU,
    GAME_STATE_PLAYING,
    GAME_STATE_PAUSED,
    GAME_STATE_PHOTO_MODE,
    GAME_STATE_DEMO_GATE,
    GAME_STATE_GAME_OVER,
    GAME_STATE_VICTORY
} AppState;

typedef enum {
    ENEMY_BASIC,
    ENEMY_FAST,
    ENEMY_TANK
} EnemyType;

typedef struct {
    gfloat x, y;
    gfloat vx, vy;
    gboolean active;
} Bullet;

typedef struct {
    gfloat x, y;
    gfloat vx, vy;
    EnemyType type;
    gint health;
    gfloat shoot_timer;
    gboolean active;
} Enemy;

typedef struct {
    gfloat x, y;
    gfloat vx, vy;
    gfloat life;
    guint8 r, g, b;
    gboolean active;
} Particle;

typedef struct {
    gfloat x, y;
    gfloat width, height;
    gint lives;
    gint max_lives;
    gfloat invulnerable_timer;
    gboolean has_shield;
    gfloat shield_timer;
} Player;

/* ===== Achievement Definition ===== */

typedef struct {
    const gchar *id;
    const gchar *name;
    const gchar *description;
    gint points;
    gboolean unlocked;
} AchievementDef;

/* ===== Global State ===== */

static GrlWindow *g_window = NULL;
static AppState g_state = GAME_STATE_CONSENT;
static Player g_player;
static Bullet g_bullets[MAX_BULLETS];
static Bullet g_enemy_bullets[MAX_BULLETS];
static Enemy g_enemies[MAX_ENEMIES];
static Particle g_particles[MAX_PARTICLES];

static gint g_score = 0;
static gint g_wave = 1;
static gint g_enemies_remaining = 0;
static gfloat g_wave_delay = 0.0f;
static gint g_combo = 0;
static gfloat g_combo_timer = 0.0f;
static gint g_total_kills = 0;
static gint g_deaths_this_run = 0;
static gint g_powerups_collected = 0;
static gint g_shots_fired_this_wave = 0;
static gfloat g_game_time = 0.0f;
static gboolean g_boss_active = FALSE;
static gfloat g_star_offset = 0.0f;

/* Consent state */
static gboolean g_consent_analytics = FALSE;
static gboolean g_consent_shown = FALSE;
static gint g_consent_selection = 0;

/* Photo mode state */
static gfloat g_photo_zoom = 1.0f;
static gfloat g_photo_offset_x = 0.0f;
static gfloat g_photo_offset_y = 0.0f;
static gint g_photo_filter = 0;
static gboolean g_photo_hide_ui = FALSE;
static const gchar *g_photo_filter_names[] = { "Normal", "Noir", "Neon", "Vintage" };

/* Demo mode */
static gboolean g_demo_mode = TRUE;  /* Set to FALSE for full game */

/* Achievement notification */
static const gchar *g_achievement_popup = NULL;
static gfloat g_achievement_popup_timer = 0.0f;

/* Shooting cooldown */
static gfloat g_shoot_cooldown = 0.0f;

/* Achievement definitions */
static AchievementDef g_achievements[] = {
    { ACH_FIRST_BLOOD, "First Blood", "Defeat your first enemy", 10, FALSE },
    { ACH_COMBO_MASTER, "Combo Master", "Achieve a 10-kill combo", 25, FALSE },
    { ACH_WAVE1_COMPLETE, "Wave 1 Complete", "Beat wave 1", 15, FALSE },
    { ACH_WAVE5_COMPLETE, "Wave 5 Complete", "Beat wave 5", 50, FALSE },
    { ACH_BOSS_SLAYER, "Boss Slayer", "Defeat the boss", 100, FALSE },
    { ACH_NO_DEATHS, "No Deaths", "Complete game without dying", 200, FALSE },
    { ACH_SPEEDRUNNER, "Speedrunner", "Complete in under 5 minutes", 150, FALSE },
    { ACH_COLLECTOR, "Collector", "Collect all powerup types", 75, FALSE },
    { ACH_PACIFIST_WAVE, "Pacifist Wave", "Complete a wave without shooting", 100, FALSE },
    { ACH_100_KILLS, "100 Kills", "Defeat 100 enemies total", 50, FALSE },
    { ACH_500_KILLS, "500 Kills", "Defeat 500 enemies total", 100, FALSE },
    { ACH_TRUE_DEFENDER, "True Defender", "Unlock all other achievements", 500, FALSE },
    { NULL, NULL, NULL, 0, FALSE }
};

/* ===== Colors (initialized at startup) ===== */

static GrlColor *color_white = NULL;
static GrlColor *color_black = NULL;
static GrlColor *color_gray = NULL;
static GrlColor *color_yellow = NULL;
static GrlColor *color_red = NULL;
static GrlColor *color_cyan = NULL;
static GrlColor *color_gold = NULL;
static GrlColor *color_bg_dark = NULL;
static GrlColor *color_ship = NULL;
static GrlColor *color_bullet = NULL;
static GrlColor *color_enemy_bullet = NULL;
static GrlColor *color_highlight = NULL;

static void
init_colors (void)
{
    color_white = grl_color_new (255, 255, 255, 255);
    color_black = grl_color_new (0, 0, 0, 255);
    color_gray = grl_color_new (150, 150, 150, 255);
    color_yellow = grl_color_new (255, 255, 0, 255);
    color_red = grl_color_new (255, 100, 100, 255);
    color_cyan = grl_color_new (0, 255, 255, 255);
    color_gold = grl_color_new (255, 215, 0, 255);
    color_bg_dark = grl_color_new (5, 5, 15, 255);
    color_ship = grl_color_new (50, 200, 255, 255);
    color_bullet = grl_color_new (0, 255, 255, 255);
    color_enemy_bullet = grl_color_new (255, 100, 100, 255);
    color_highlight = grl_color_new (100, 200, 255, 255);
}

static void
cleanup_colors (void)
{
    g_clear_pointer (&color_white, grl_color_free);
    g_clear_pointer (&color_black, grl_color_free);
    g_clear_pointer (&color_gray, grl_color_free);
    g_clear_pointer (&color_yellow, grl_color_free);
    g_clear_pointer (&color_red, grl_color_free);
    g_clear_pointer (&color_cyan, grl_color_free);
    g_clear_pointer (&color_gold, grl_color_free);
    g_clear_pointer (&color_bg_dark, grl_color_free);
    g_clear_pointer (&color_ship, grl_color_free);
    g_clear_pointer (&color_bullet, grl_color_free);
    g_clear_pointer (&color_enemy_bullet, grl_color_free);
    g_clear_pointer (&color_highlight, grl_color_free);
}

/* ===== Helper Functions ===== */

static void
unlock_achievement (const gchar *id)
{
    gint i;

    for (i = 0; g_achievements[i].id != NULL; i++)
    {
        if (g_strcmp0 (g_achievements[i].id, id) == 0 && !g_achievements[i].unlocked)
        {
            g_achievements[i].unlocked = TRUE;
            g_achievement_popup = g_achievements[i].name;
            g_achievement_popup_timer = 3.0f;

            /* Track with analytics if consented */
            if (g_consent_analytics)
            {
                g_print ("[Analytics] Achievement unlocked: %s\n", id);
            }
            break;
        }
    }
}

static gboolean
is_achievement_unlocked (const gchar *id)
{
    gint i;

    for (i = 0; g_achievements[i].id != NULL; i++)
    {
        if (g_strcmp0 (g_achievements[i].id, id) == 0)
            return g_achievements[i].unlocked;
    }
    return FALSE;
}

static gint
count_unlocked_achievements (void)
{
    gint count;
    gint i;

    count = 0;
    for (i = 0; g_achievements[i].id != NULL; i++)
    {
        if (g_achievements[i].unlocked && g_strcmp0 (g_achievements[i].id, ACH_TRUE_DEFENDER) != 0)
            count++;
    }
    return count;
}

static void
check_true_defender (void)
{
    /* 11 achievements excluding True Defender itself */
    if (count_unlocked_achievements () >= 11)
    {
        unlock_achievement (ACH_TRUE_DEFENDER);
    }
}

static void
track_event (const gchar *event_name,
             const gchar *key,
             gint         value)
{
    if (g_consent_analytics)
    {
        g_print ("[Analytics] Event: %s (%s=%d)\n", event_name, key ? key : "none", value);
    }
}

static void
spawn_particle (gfloat x,
                gfloat y,
                guint8 r,
                guint8 g,
                guint8 b)
{
    gint i;

    for (i = 0; i < MAX_PARTICLES; i++)
    {
        if (!g_particles[i].active)
        {
            g_particles[i].x = x;
            g_particles[i].y = y;
            g_particles[i].vx = ((gfloat)g_random_int_range (-100, 100));
            g_particles[i].vy = ((gfloat)g_random_int_range (-100, 100));
            g_particles[i].life = 0.5f;
            g_particles[i].r = r;
            g_particles[i].g = g;
            g_particles[i].b = b;
            g_particles[i].active = TRUE;
            break;
        }
    }
}

static void
spawn_explosion (gfloat x,
                 gfloat y,
                 gint   count,
                 guint8 r,
                 guint8 g,
                 guint8 b)
{
    gint i;

    for (i = 0; i < count; i++)
    {
        spawn_particle (x, y, r, g, b);
    }
}

/* ===== Game Logic ===== */

static void
init_player (void)
{
    g_player.x = SCREEN_WIDTH / 2.0f;
    g_player.y = SCREEN_HEIGHT - 80.0f;
    g_player.width = 40.0f;
    g_player.height = 40.0f;
    g_player.lives = 3;
    g_player.max_lives = 3;
    g_player.invulnerable_timer = 0.0f;
    g_player.has_shield = FALSE;
    g_player.shield_timer = 0.0f;
}

static void
spawn_enemy (EnemyType type,
             gfloat    x,
             gfloat    y)
{
    gint i;

    for (i = 0; i < MAX_ENEMIES; i++)
    {
        if (!g_enemies[i].active)
        {
            g_enemies[i].x = x;
            g_enemies[i].y = y;
            g_enemies[i].type = type;
            g_enemies[i].active = TRUE;
            g_enemies[i].shoot_timer = g_random_double_range (1.0, 3.0);

            switch (type)
            {
            case ENEMY_BASIC:
                g_enemies[i].vx = 0.0f;
                g_enemies[i].vy = 50.0f;
                g_enemies[i].health = 1;
                break;
            case ENEMY_FAST:
                g_enemies[i].vx = g_random_double_range (-100, 100);
                g_enemies[i].vy = 100.0f;
                g_enemies[i].health = 1;
                break;
            case ENEMY_TANK:
                g_enemies[i].vx = 0.0f;
                g_enemies[i].vy = 30.0f;
                g_enemies[i].health = 3;
                break;
            }

            g_enemies_remaining++;
            break;
        }
    }
}

static void
spawn_boss (void)
{
    gint i;

    for (i = 0; i < MAX_ENEMIES; i++)
    {
        if (!g_enemies[i].active)
        {
            g_enemies[i].x = SCREEN_WIDTH / 2.0f;
            g_enemies[i].y = -50.0f;
            g_enemies[i].vx = 100.0f;
            g_enemies[i].vy = 0.0f;
            g_enemies[i].type = ENEMY_TANK;
            g_enemies[i].health = 20;
            g_enemies[i].shoot_timer = 0.5f;
            g_enemies[i].active = TRUE;
            g_enemies_remaining = 1;
            g_boss_active = TRUE;
            break;
        }
    }
}

static void
spawn_wave (gint wave_num)
{
    gint enemy_count;
    gint i;
    EnemyType type;
    gfloat x;
    gfloat y;

    g_shots_fired_this_wave = 0;

    enemy_count = 5 + wave_num * 2;
    if (enemy_count > MAX_ENEMIES)
        enemy_count = MAX_ENEMIES;

    for (i = 0; i < enemy_count; i++)
    {
        type = ENEMY_BASIC;
        if (wave_num >= 2 && g_random_double () < 0.3)
            type = ENEMY_FAST;
        if (wave_num >= 3 && g_random_double () < 0.2)
            type = ENEMY_TANK;

        x = g_random_double_range (50, SCREEN_WIDTH - 50);
        y = g_random_double_range (-200, -50);
        spawn_enemy (type, x, y);
    }

    track_event ("wave_start", "wave", wave_num);
}

static void
fire_bullet (void)
{
    gint i;

    for (i = 0; i < MAX_BULLETS; i++)
    {
        if (!g_bullets[i].active)
        {
            g_bullets[i].x = g_player.x;
            g_bullets[i].y = g_player.y - g_player.height / 2.0f;
            g_bullets[i].vx = 0.0f;
            g_bullets[i].vy = -BULLET_SPEED;
            g_bullets[i].active = TRUE;
            g_shots_fired_this_wave++;
            break;
        }
    }
}

static void
enemy_fire (Enemy *enemy)
{
    gint i;
    gfloat dx;
    gfloat dy;
    gfloat len;

    for (i = 0; i < MAX_BULLETS; i++)
    {
        if (!g_enemy_bullets[i].active)
        {
            g_enemy_bullets[i].x = enemy->x;
            g_enemy_bullets[i].y = enemy->y + 20.0f;

            /* Aim at player */
            dx = g_player.x - enemy->x;
            dy = g_player.y - enemy->y;
            len = sqrtf (dx * dx + dy * dy);
            if (len > 0.0f)
            {
                g_enemy_bullets[i].vx = (dx / len) * 200.0f;
                g_enemy_bullets[i].vy = (dy / len) * 200.0f;
            }
            else
            {
                g_enemy_bullets[i].vx = 0.0f;
                g_enemy_bullets[i].vy = 200.0f;
            }
            g_enemy_bullets[i].active = TRUE;
            break;
        }
    }
}

static void
kill_enemy (gint index)
{
    Enemy *e;
    gint points;
    guint8 r;
    guint8 g;
    guint8 b;

    e = &g_enemies[index];

    /* Score based on type */
    points = 10;
    if (e->type == ENEMY_FAST)
        points = 15;
    else if (e->type == ENEMY_TANK)
        points = 25;

    /* Boss gives big points */
    if (g_boss_active && e->health <= 0)
        points = 500;

    /* Combo bonus */
    g_combo++;
    g_combo_timer = COMBO_TIMEOUT;
    if (g_combo > 1)
        points = (gint)(points * (1.0f + g_combo * 0.1f));

    g_score += points;
    g_total_kills++;
    g_enemies_remaining--;

    /* Explosion effect */
    r = 255;
    g = 200;
    b = 50;
    if (e->type == ENEMY_FAST)
    {
        r = 100;
        g = 200;
        b = 255;
    }
    else if (e->type == ENEMY_TANK)
    {
        r = 255;
        g = 100;
        b = 100;
    }
    spawn_explosion (e->x, e->y, 10, r, g, b);

    e->active = FALSE;

    /* Achievement checks */
    if (g_total_kills == 1)
        unlock_achievement (ACH_FIRST_BLOOD);
    if (g_combo >= 10 && !is_achievement_unlocked (ACH_COMBO_MASTER))
        unlock_achievement (ACH_COMBO_MASTER);
    if (g_total_kills >= 100 && !is_achievement_unlocked (ACH_100_KILLS))
        unlock_achievement (ACH_100_KILLS);
    if (g_total_kills >= 500 && !is_achievement_unlocked (ACH_500_KILLS))
        unlock_achievement (ACH_500_KILLS);
    if (g_boss_active && g_enemies_remaining == 0)
        unlock_achievement (ACH_BOSS_SLAYER);

    check_true_defender ();

    track_event ("enemy_killed", "type", (gint)e->type);
}

static void
damage_player (void)
{
    if (g_player.invulnerable_timer > 0.0f)
        return;

    if (g_player.has_shield)
    {
        g_player.has_shield = FALSE;
        g_player.invulnerable_timer = 1.0f;
        return;
    }

    g_player.lives--;
    g_deaths_this_run++;
    g_player.invulnerable_timer = 2.0f;
    spawn_explosion (g_player.x, g_player.y, 15, 255, 100, 100);

    track_event ("player_death", "wave", g_wave);

    if (g_player.lives <= 0)
    {
        g_state = GAME_STATE_GAME_OVER;
        track_event ("game_over", "score", g_score);
    }
}

static void
update_game (gfloat delta)
{
    gint i;
    gint j;
    gfloat dx;
    gfloat dy;
    gfloat hit_dist;
    Enemy *e;

    g_game_time += delta;

    /* Update invulnerability */
    if (g_player.invulnerable_timer > 0.0f)
        g_player.invulnerable_timer -= delta;

    /* Update shield */
    if (g_player.has_shield)
    {
        g_player.shield_timer -= delta;
        if (g_player.shield_timer <= 0.0f)
            g_player.has_shield = FALSE;
    }

    /* Update combo timer */
    if (g_combo_timer > 0.0f)
    {
        g_combo_timer -= delta;
        if (g_combo_timer <= 0.0f)
            g_combo = 0;
    }

    /* Player movement */
    if (grl_input_is_key_down (GRL_KEY_LEFT) || grl_input_is_key_down (GRL_KEY_A))
        g_player.x -= PLAYER_SPEED * delta;
    if (grl_input_is_key_down (GRL_KEY_RIGHT) || grl_input_is_key_down (GRL_KEY_D))
        g_player.x += PLAYER_SPEED * delta;
    if (grl_input_is_key_down (GRL_KEY_UP) || grl_input_is_key_down (GRL_KEY_W))
        g_player.y -= PLAYER_SPEED * delta;
    if (grl_input_is_key_down (GRL_KEY_DOWN) || grl_input_is_key_down (GRL_KEY_S))
        g_player.y += PLAYER_SPEED * delta;

    /* Clamp player position */
    if (g_player.x < g_player.width / 2.0f)
        g_player.x = g_player.width / 2.0f;
    if (g_player.x > SCREEN_WIDTH - g_player.width / 2.0f)
        g_player.x = SCREEN_WIDTH - g_player.width / 2.0f;
    if (g_player.y < g_player.height / 2.0f)
        g_player.y = g_player.height / 2.0f;
    if (g_player.y > SCREEN_HEIGHT - g_player.height / 2.0f)
        g_player.y = SCREEN_HEIGHT - g_player.height / 2.0f;

    /* Shooting */
    g_shoot_cooldown -= delta;
    if (grl_input_is_key_down (GRL_KEY_SPACE) && g_shoot_cooldown <= 0.0f)
    {
        fire_bullet ();
        g_shoot_cooldown = 0.15f;
    }

    /* Update player bullets */
    for (i = 0; i < MAX_BULLETS; i++)
    {
        if (g_bullets[i].active)
        {
            g_bullets[i].x += g_bullets[i].vx * delta;
            g_bullets[i].y += g_bullets[i].vy * delta;

            if (g_bullets[i].y < -10.0f)
                g_bullets[i].active = FALSE;
        }
    }

    /* Update enemy bullets */
    for (i = 0; i < MAX_BULLETS; i++)
    {
        if (g_enemy_bullets[i].active)
        {
            g_enemy_bullets[i].x += g_enemy_bullets[i].vx * delta;
            g_enemy_bullets[i].y += g_enemy_bullets[i].vy * delta;

            /* Off screen */
            if (g_enemy_bullets[i].y > SCREEN_HEIGHT + 10.0f ||
                g_enemy_bullets[i].y < -10.0f ||
                g_enemy_bullets[i].x < -10.0f ||
                g_enemy_bullets[i].x > SCREEN_WIDTH + 10.0f)
            {
                g_enemy_bullets[i].active = FALSE;
            }

            /* Hit player */
            dx = g_enemy_bullets[i].x - g_player.x;
            dy = g_enemy_bullets[i].y - g_player.y;
            if (dx * dx + dy * dy < 400.0f)
            {
                g_enemy_bullets[i].active = FALSE;
                damage_player ();
            }
        }
    }

    /* Update enemies */
    for (i = 0; i < MAX_ENEMIES; i++)
    {
        if (g_enemies[i].active)
        {
            e = &g_enemies[i];

            /* Boss movement pattern */
            if (g_boss_active && e->health > 0)
            {
                if (e->y < 100.0f)
                    e->y += 50.0f * delta;
                e->x += e->vx * delta;
                if (e->x < 100.0f || e->x > SCREEN_WIDTH - 100.0f)
                    e->vx = -e->vx;
            }
            else
            {
                e->x += e->vx * delta;
                e->y += e->vy * delta;
            }

            /* Enemy shooting */
            e->shoot_timer -= delta;
            if (e->shoot_timer <= 0.0f)
            {
                enemy_fire (e);
                e->shoot_timer = g_boss_active ? 0.3f : g_random_double_range (2.0, 4.0);
            }

            /* Off screen */
            if (e->y > SCREEN_HEIGHT + 50.0f)
            {
                e->active = FALSE;
                g_enemies_remaining--;
            }

            /* Collision with player */
            dx = e->x - g_player.x;
            dy = e->y - g_player.y;
            if (dx * dx + dy * dy < 900.0f)
            {
                damage_player ();
            }

            /* Collision with player bullets */
            for (j = 0; j < MAX_BULLETS; j++)
            {
                if (g_bullets[j].active)
                {
                    dx = g_bullets[j].x - e->x;
                    dy = g_bullets[j].y - e->y;
                    hit_dist = g_boss_active ? 2500.0f : 625.0f;
                    if (dx * dx + dy * dy < hit_dist)
                    {
                        g_bullets[j].active = FALSE;
                        e->health--;
                        spawn_particle (g_bullets[j].x, g_bullets[j].y, 255, 255, 0);
                        if (e->health <= 0)
                        {
                            kill_enemy (i);
                        }
                    }
                }
            }
        }
    }

    /* Update particles */
    for (i = 0; i < MAX_PARTICLES; i++)
    {
        if (g_particles[i].active)
        {
            g_particles[i].x += g_particles[i].vx * delta;
            g_particles[i].y += g_particles[i].vy * delta;
            g_particles[i].life -= delta;
            if (g_particles[i].life <= 0.0f)
                g_particles[i].active = FALSE;
        }
    }

    /* Wave completion check */
    if (g_enemies_remaining <= 0 && g_wave_delay <= 0.0f)
    {
        /* Pacifist check */
        if (g_shots_fired_this_wave == 0 && g_wave > 1)
            unlock_achievement (ACH_PACIFIST_WAVE);

        /* Wave completion achievements */
        if (g_wave == 1)
            unlock_achievement (ACH_WAVE1_COMPLETE);
        if (g_wave == 5)
            unlock_achievement (ACH_WAVE5_COMPLETE);

        track_event ("wave_complete", "wave", g_wave);

        g_wave++;

        /* Check for demo gate */
        if (g_demo_mode && g_wave > DEMO_MAX_WAVE)
        {
            g_state = GAME_STATE_DEMO_GATE;
            return;
        }

        /* Boss wave after wave 5 */
        if (g_wave == 6)
        {
            g_wave_delay = 2.0f;
        }
        else if (g_wave > 6)
        {
            /* Victory! */
            if (g_deaths_this_run == 0)
                unlock_achievement (ACH_NO_DEATHS);
            if (g_game_time < 300.0f)
                unlock_achievement (ACH_SPEEDRUNNER);
            check_true_defender ();
            g_state = GAME_STATE_VICTORY;
            track_event ("game_complete", "score", g_score);
        }
        else
        {
            g_wave_delay = 2.0f;
        }
    }

    /* Wave delay */
    if (g_wave_delay > 0.0f)
    {
        g_wave_delay -= delta;
        if (g_wave_delay <= 0.0f)
        {
            if (g_wave == 6)
                spawn_boss ();
            else
                spawn_wave (g_wave);
        }
    }

    /* Photo mode */
    if (grl_input_is_key_pressed (GRL_KEY_P))
    {
        g_state = GAME_STATE_PHOTO_MODE;
        g_photo_zoom = 1.0f;
        g_photo_offset_x = 0.0f;
        g_photo_offset_y = 0.0f;
        g_photo_filter = 0;
        g_photo_hide_ui = FALSE;
    }

    /* Pause */
    if (grl_input_is_key_pressed (GRL_KEY_ESCAPE))
    {
        g_state = GAME_STATE_PAUSED;
    }
}

/* ===== Rendering ===== */

static void
draw_game_world (gfloat delta)
{
    gint i;
    gint x;
    gint y;
    gint brightness;
    GrlColor *star_color;
    guint8 alpha;
    GrlColor *c;
    Enemy *e;
    GrlColor *enemy_color;
    gfloat size;
    GrlVector2 *v1;
    GrlVector2 *v2;
    GrlVector2 *v3;
    gfloat health_pct;
    GrlColor *bar_bg;
    GrlColor *bar_fg;
    gboolean visible;
    GrlColor *shield_color;

    /* Stars background */
    g_star_offset += 30.0f * delta;
    if (g_star_offset > 100.0f)
        g_star_offset -= 100.0f;

    for (i = 0; i < 50; i++)
    {
        x = (i * 17) % SCREEN_WIDTH;
        y = ((gint)(i * 23 + g_star_offset)) % SCREEN_HEIGHT;
        brightness = 100 + (i * 3) % 156;
        star_color = grl_color_new (brightness, brightness, brightness, 255);
        grl_draw_pixel (x, y, star_color);
        grl_color_free (star_color);
    }

    /* Particles */
    for (i = 0; i < MAX_PARTICLES; i++)
    {
        if (g_particles[i].active)
        {
            alpha = (guint8)(255 * (g_particles[i].life / 0.5f));
            c = grl_color_new (g_particles[i].r, g_particles[i].g, g_particles[i].b, alpha);
            grl_draw_circle ((gint)g_particles[i].x, (gint)g_particles[i].y, 3, c);
            grl_color_free (c);
        }
    }

    /* Player bullets */
    for (i = 0; i < MAX_BULLETS; i++)
    {
        if (g_bullets[i].active)
        {
            grl_draw_rectangle ((gint)(g_bullets[i].x - 2.0f), (gint)(g_bullets[i].y - 8.0f),
                               4, 16, color_bullet);
        }
    }

    /* Enemy bullets */
    for (i = 0; i < MAX_BULLETS; i++)
    {
        if (g_enemy_bullets[i].active)
        {
            grl_draw_circle ((gint)g_enemy_bullets[i].x, (gint)g_enemy_bullets[i].y, 5, color_enemy_bullet);
        }
    }

    /* Enemies */
    for (i = 0; i < MAX_ENEMIES; i++)
    {
        if (g_enemies[i].active)
        {
            e = &g_enemies[i];
            size = 20.0f;

            switch (e->type)
            {
            case ENEMY_BASIC:
                enemy_color = grl_color_new (200, 50, 50, 255);
                break;
            case ENEMY_FAST:
                enemy_color = grl_color_new (50, 150, 255, 255);
                size = 15.0f;
                break;
            case ENEMY_TANK:
                enemy_color = grl_color_new (150, 150, 150, 255);
                size = g_boss_active ? 50.0f : 30.0f;
                break;
            default:
                enemy_color = grl_color_new (200, 50, 50, 255);
                break;
            }

            /* Draw enemy ship (triangle pointing down) */
            v1 = grl_vector2_new (e->x, e->y + size);
            v2 = grl_vector2_new (e->x - size, e->y - size);
            v3 = grl_vector2_new (e->x + size, e->y - size);
            grl_draw_triangle (v1, v2, v3, enemy_color);
            grl_vector2_free (v1);
            grl_vector2_free (v2);
            grl_vector2_free (v3);
            grl_color_free (enemy_color);

            /* Boss health bar */
            if (g_boss_active && e->health > 0)
            {
                health_pct = e->health / 20.0f;
                bar_bg = grl_color_new (50, 50, 50, 255);
                bar_fg = grl_color_new (255, 50, 50, 255);
                grl_draw_rectangle (SCREEN_WIDTH / 2 - 100, 20, 200, 15, bar_bg);
                grl_draw_rectangle (SCREEN_WIDTH / 2 - 100, 20, (gint)(200 * health_pct), 15, bar_fg);
                grl_color_free (bar_bg);
                grl_color_free (bar_fg);
            }
        }
    }

    /* Player */
    if (g_player.lives > 0)
    {
        /* Blink when invulnerable */
        visible = TRUE;
        if (g_player.invulnerable_timer > 0.0f)
            visible = ((gint)(g_player.invulnerable_timer * 10) % 2) == 0;

        if (visible)
        {
            /* Draw player ship (triangle pointing up) */
            v1 = grl_vector2_new (g_player.x, g_player.y - g_player.height / 2);
            v2 = grl_vector2_new (g_player.x - g_player.width / 2, g_player.y + g_player.height / 2);
            v3 = grl_vector2_new (g_player.x + g_player.width / 2, g_player.y + g_player.height / 2);
            grl_draw_triangle (v1, v2, v3, color_ship);
            grl_vector2_free (v1);
            grl_vector2_free (v2);
            grl_vector2_free (v3);

            /* Shield effect */
            if (g_player.has_shield)
            {
                shield_color = grl_color_new (100, 200, 255, 100);
                grl_draw_circle_lines ((gint)g_player.x, (gint)g_player.y, 35, shield_color);
                grl_color_free (shield_color);
            }
        }
    }
}

static void
draw_hud (gfloat delta)
{
    gchar *score_text;
    const gchar *wave_name;
    gchar *wave_text;
    gint i;
    GrlColor *heart;
    gchar *combo_text;
    gint x;
    const gchar *announce;
    gfloat slide;
    gint box_x;
    GrlColor *box_bg;

    /* Score */
    score_text = g_strdup_printf ("Score: %d", g_score);
    grl_draw_text (score_text, 10, 10, 20, color_white);
    g_free (score_text);

    /* Wave */
    wave_name = g_boss_active ? "BOSS" : NULL;
    if (wave_name)
        wave_text = g_strdup_printf ("Wave: %s", wave_name);
    else
        wave_text = g_strdup_printf ("Wave: %d", g_wave);
    grl_draw_text (wave_text, 10, 35, 20, color_white);
    g_free (wave_text);

    /* Lives */
    grl_draw_text ("Lives:", 10, 60, 20, color_white);
    heart = grl_color_new (255, 50, 100, 255);
    for (i = 0; i < g_player.lives; i++)
    {
        grl_draw_circle (75 + i * 25, 70, 8, heart);
    }
    grl_color_free (heart);

    /* Combo */
    if (g_combo > 1)
    {
        combo_text = g_strdup_printf ("x%d COMBO!", g_combo);
        x = SCREEN_WIDTH / 2 - 50;
        grl_draw_text (combo_text, x, 50, 25, color_yellow);
        g_free (combo_text);
    }

    /* Wave announcement */
    if (g_wave_delay > 0.0f && g_enemies_remaining == 0)
    {
        announce = g_wave == 6 ? "BOSS INCOMING!" : "NEXT WAVE...";
        x = SCREEN_WIDTH / 2 - 80;
        grl_draw_text (announce, x, SCREEN_HEIGHT / 2, 30, color_red);
    }

    /* Achievement popup */
    if (g_achievement_popup_timer > 0.0f)
    {
        g_achievement_popup_timer -= delta;

        if (g_achievement_popup_timer > 2.5f)
            slide = (3.0f - g_achievement_popup_timer) * 2.0f;
        else if (g_achievement_popup_timer < 0.5f)
            slide = g_achievement_popup_timer * 2.0f;
        else
            slide = 1.0f;

        box_x = SCREEN_WIDTH - (gint)(250 * slide);
        box_bg = grl_color_new (30, 30, 30, 220);

        grl_draw_rectangle (box_x, 80, 240, 60, box_bg);
        grl_draw_text ("Achievement Unlocked!", box_x + 10, 88, 15, color_gold);
        grl_draw_text (g_achievement_popup, box_x + 10, 110, 20, color_white);
        grl_color_free (box_bg);
    }
}

static void
draw_consent_screen (void)
{
    GrlColor *bg;
    GrlColor *yes_color;
    GrlColor *no_color;

    bg = grl_color_new (20, 20, 30, 255);
    grl_draw_clear_background (bg);
    grl_color_free (bg);

    grl_draw_text ("Privacy Settings", SCREEN_WIDTH / 2 - 100, 100, 30, color_white);
    grl_draw_text ("We respect your privacy.", SCREEN_WIDTH / 2 - 120, 150, 18, color_gray);

    grl_draw_text ("Allow analytics to help improve the game?", 100, 220, 18, color_white);
    grl_draw_text ("(Anonymous gameplay data only)", 100, 245, 14, color_gray);

    yes_color = (g_consent_selection == 0) ? color_highlight : color_white;
    no_color = (g_consent_selection == 1) ? color_highlight : color_white;

    grl_draw_text ("[ YES ]", 200, 300, 25, yes_color);
    grl_draw_text ("[ NO ]", 400, 300, 25, no_color);

    grl_draw_text ("Press LEFT/RIGHT to select, ENTER to confirm", 150, 400, 16, color_gray);
}

static void
draw_menu (void)
{
    GrlColor *bg;
    gint unlocked;
    gchar *ach_text;

    bg = grl_color_new (10, 10, 20, 255);
    grl_draw_clear_background (bg);
    grl_color_free (bg);

    /* Title */
    grl_draw_text ("SPACE DEFENDER", SCREEN_WIDTH / 2 - 140, 100, 40, color_cyan);

    if (g_demo_mode)
    {
        grl_draw_text ("[ DEMO VERSION ]", SCREEN_WIDTH / 2 - 80, 150, 20, color_yellow);
    }

    grl_draw_text ("Press SPACE to Start", SCREEN_WIDTH / 2 - 100, 300, 25, color_white);
    grl_draw_text ("Arrow Keys - Move", SCREEN_WIDTH / 2 - 80, 380, 18, color_gray);
    grl_draw_text ("Space - Shoot", SCREEN_WIDTH / 2 - 60, 405, 18, color_gray);
    grl_draw_text ("P - Photo Mode", SCREEN_WIDTH / 2 - 65, 430, 18, color_gray);
    grl_draw_text ("ESC - Pause", SCREEN_WIDTH / 2 - 50, 455, 18, color_gray);

    /* Achievement count */
    unlocked = count_unlocked_achievements ();
    ach_text = g_strdup_printf ("Achievements: %d/12", unlocked);
    grl_draw_text (ach_text, SCREEN_WIDTH / 2 - 70, 520, 16, color_gray);
    g_free (ach_text);
}

static void
draw_paused (void)
{
    GrlColor *overlay;

    /* Dim overlay */
    overlay = grl_color_new (0, 0, 0, 180);
    grl_draw_rectangle (0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, overlay);
    grl_color_free (overlay);

    grl_draw_text ("PAUSED", SCREEN_WIDTH / 2 - 60, SCREEN_HEIGHT / 2 - 50, 40, color_white);
    grl_draw_text ("Press SPACE to Resume", SCREEN_WIDTH / 2 - 100, SCREEN_HEIGHT / 2 + 20, 20, color_gray);
    grl_draw_text ("Press Q to Quit", SCREEN_WIDTH / 2 - 70, SCREEN_HEIGHT / 2 + 50, 20, color_gray);
}

static void
draw_photo_mode (gfloat delta)
{
    GrlColor *bg;
    gchar *filter_text;

    draw_game_world (delta);

    if (!g_photo_hide_ui)
    {
        /* Photo mode UI */
        bg = grl_color_new (0, 0, 0, 150);
        grl_draw_rectangle (0, SCREEN_HEIGHT - 100, SCREEN_WIDTH, 100, bg);
        grl_color_free (bg);

        grl_draw_text ("PHOTO MODE", 20, SCREEN_HEIGHT - 90, 20, color_white);

        filter_text = g_strdup_printf ("Filter: %s (F)", g_photo_filter_names[g_photo_filter]);
        grl_draw_text (filter_text, 20, SCREEN_HEIGHT - 60, 16, color_white);
        g_free (filter_text);

        grl_draw_text ("H - Hide UI | Enter - Screenshot | ESC - Exit", 20, SCREEN_HEIGHT - 30, 14, color_white);
    }
}

static void
draw_demo_gate (void)
{
    GrlColor *bg;

    bg = grl_color_new (20, 20, 40, 255);
    grl_draw_clear_background (bg);
    grl_color_free (bg);

    grl_draw_text ("DEMO COMPLETE!", SCREEN_WIDTH / 2 - 120, 150, 35, color_yellow);

    grl_draw_text ("Thanks for playing the demo!", SCREEN_WIDTH / 2 - 130, 220, 20, color_white);
    grl_draw_text ("The full game includes:", SCREEN_WIDTH / 2 - 100, 260, 18, color_gray);
    grl_draw_text ("- 5 more challenging waves", SCREEN_WIDTH / 2 - 100, 290, 16, color_gray);
    grl_draw_text ("- Epic boss battle", SCREEN_WIDTH / 2 - 100, 315, 16, color_gray);
    grl_draw_text ("- All 12 achievements", SCREEN_WIDTH / 2 - 100, 340, 16, color_gray);

    grl_draw_text ("Press ENTER to return to menu", SCREEN_WIDTH / 2 - 130, 420, 18, color_white);
    grl_draw_text ("(Full version: example.com/buy)", SCREEN_WIDTH / 2 - 130, 480, 14, color_gray);
}

static void
draw_game_over_screen (void)
{
    GrlColor *bg;
    gchar *score_text;
    gchar *wave_text;
    gchar *kills_text;

    bg = grl_color_new (30, 10, 10, 255);
    grl_draw_clear_background (bg);
    grl_color_free (bg);

    grl_draw_text ("GAME OVER", SCREEN_WIDTH / 2 - 100, 150, 40, color_red);

    score_text = g_strdup_printf ("Final Score: %d", g_score);
    grl_draw_text (score_text, SCREEN_WIDTH / 2 - 80, 220, 25, color_white);
    g_free (score_text);

    wave_text = g_strdup_printf ("Reached Wave: %d", g_wave);
    grl_draw_text (wave_text, SCREEN_WIDTH / 2 - 80, 260, 20, color_gray);
    g_free (wave_text);

    kills_text = g_strdup_printf ("Enemies Defeated: %d", g_total_kills);
    grl_draw_text (kills_text, SCREEN_WIDTH / 2 - 95, 290, 18, color_gray);
    g_free (kills_text);

    grl_draw_text ("Press SPACE to try again", SCREEN_WIDTH / 2 - 110, 380, 20, color_white);
    grl_draw_text ("Press Q to quit", SCREEN_WIDTH / 2 - 70, 420, 18, color_gray);
}

static void
draw_victory (void)
{
    GrlColor *bg;
    gchar *score_text;
    gint minutes;
    gint seconds;
    gchar *time_text;
    gint unlocked;
    gchar *ach_text;

    bg = grl_color_new (10, 20, 40, 255);
    grl_draw_clear_background (bg);
    grl_color_free (bg);

    grl_draw_text ("VICTORY!", SCREEN_WIDTH / 2 - 80, 100, 45, color_gold);
    grl_draw_text ("Earth is saved!", SCREEN_WIDTH / 2 - 80, 160, 20, color_white);

    score_text = g_strdup_printf ("Final Score: %d", g_score);
    grl_draw_text (score_text, SCREEN_WIDTH / 2 - 80, 220, 25, color_white);
    g_free (score_text);

    minutes = (gint)(g_game_time / 60.0f);
    seconds = (gint)g_game_time % 60;
    time_text = g_strdup_printf ("Time: %d:%02d", minutes, seconds);
    grl_draw_text (time_text, SCREEN_WIDTH / 2 - 50, 260, 20, color_gray);
    g_free (time_text);

    unlocked = count_unlocked_achievements ();
    ach_text = g_strdup_printf ("Achievements: %d/12", unlocked);
    grl_draw_text (ach_text, SCREEN_WIDTH / 2 - 70, 300, 18, color_gray);
    g_free (ach_text);

    grl_draw_text ("Press SPACE to play again", SCREEN_WIDTH / 2 - 115, 400, 20, color_white);
    grl_draw_text ("Press Q to quit", SCREEN_WIDTH / 2 - 70, 440, 18, color_gray);
}

/* ===== Game Reset ===== */

static void
reset_game (void)
{
    gint i;

    init_player ();

    for (i = 0; i < MAX_BULLETS; i++)
    {
        g_bullets[i].active = FALSE;
        g_enemy_bullets[i].active = FALSE;
    }

    for (i = 0; i < MAX_ENEMIES; i++)
        g_enemies[i].active = FALSE;

    for (i = 0; i < MAX_PARTICLES; i++)
        g_particles[i].active = FALSE;

    g_score = 0;
    g_wave = 1;
    g_enemies_remaining = 0;
    g_wave_delay = 1.0f;
    g_combo = 0;
    g_combo_timer = 0.0f;
    g_deaths_this_run = 0;
    g_powerups_collected = 0;
    g_shots_fired_this_wave = 0;
    g_game_time = 0.0f;
    g_boss_active = FALSE;
    g_shoot_cooldown = 0.0f;

    track_event ("game_start", NULL, 0);
}

/* ===== Main ===== */

int
main (int   argc,
      char *argv[])
{
    gint i;
    gfloat delta;

    /* Check for demo flag */
    for (i = 1; i < argc; i++)
    {
        if (g_strcmp0 (argv[i], "--full") == 0)
            g_demo_mode = FALSE;
    }

    g_window = grl_window_new (SCREEN_WIDTH, SCREEN_HEIGHT, "Space Defender Arcade - Phase 5 Demo");
    grl_window_set_target_fps (g_window, 60);

    /* Initialize colors and state */
    init_colors ();
    init_player ();
    g_state = GAME_STATE_CONSENT;

    g_print ("[Analytics] Session started\n");

    while (!grl_window_should_close (g_window))
    {
        delta = grl_window_get_frame_time (g_window);

        /* State-specific input handling */
        switch (g_state)
        {
        case GAME_STATE_CONSENT:
            if (grl_input_is_key_pressed (GRL_KEY_LEFT))
                g_consent_selection = 0;
            if (grl_input_is_key_pressed (GRL_KEY_RIGHT))
                g_consent_selection = 1;
            if (grl_input_is_key_pressed (GRL_KEY_ENTER))
            {
                g_consent_analytics = (g_consent_selection == 0);
                g_consent_shown = TRUE;
                g_state = GAME_STATE_MENU;
                if (g_consent_analytics)
                    track_event ("consent_granted", NULL, 0);
            }
            break;

        case GAME_STATE_MENU:
            if (grl_input_is_key_pressed (GRL_KEY_SPACE))
            {
                reset_game ();
                g_state = GAME_STATE_PLAYING;
            }
            if (grl_input_is_key_pressed (GRL_KEY_Q))
                goto cleanup;
            break;

        case GAME_STATE_PLAYING:
            update_game (delta);
            break;

        case GAME_STATE_PAUSED:
            if (grl_input_is_key_pressed (GRL_KEY_SPACE))
                g_state = GAME_STATE_PLAYING;
            if (grl_input_is_key_pressed (GRL_KEY_Q))
                g_state = GAME_STATE_MENU;
            break;

        case GAME_STATE_PHOTO_MODE:
            if (grl_input_is_key_pressed (GRL_KEY_ESCAPE))
                g_state = GAME_STATE_PLAYING;
            if (grl_input_is_key_pressed (GRL_KEY_F))
                g_photo_filter = (g_photo_filter + 1) % 4;
            if (grl_input_is_key_pressed (GRL_KEY_H))
                g_photo_hide_ui = !g_photo_hide_ui;
            if (grl_input_is_key_pressed (GRL_KEY_ENTER))
            {
                g_print ("[Photo Mode] Screenshot saved!\n");
                track_event ("screenshot_taken", "filter", g_photo_filter);
            }
            break;

        case GAME_STATE_DEMO_GATE:
            if (grl_input_is_key_pressed (GRL_KEY_ENTER))
                g_state = GAME_STATE_MENU;
            break;

        case GAME_STATE_GAME_OVER:
        case GAME_STATE_VICTORY:
            if (grl_input_is_key_pressed (GRL_KEY_SPACE))
            {
                reset_game ();
                g_state = GAME_STATE_PLAYING;
            }
            if (grl_input_is_key_pressed (GRL_KEY_Q))
                g_state = GAME_STATE_MENU;
            break;
        }

        /* Rendering */
        grl_window_begin_drawing (g_window);

        switch (g_state)
        {
        case GAME_STATE_CONSENT:
            draw_consent_screen ();
            break;

        case GAME_STATE_MENU:
            draw_menu ();
            break;

        case GAME_STATE_PLAYING:
            grl_draw_clear_background (color_bg_dark);
            draw_game_world (delta);
            draw_hud (delta);
            break;

        case GAME_STATE_PAUSED:
            grl_draw_clear_background (color_bg_dark);
            draw_game_world (delta);
            draw_hud (delta);
            draw_paused ();
            break;

        case GAME_STATE_PHOTO_MODE:
            grl_draw_clear_background (color_bg_dark);
            draw_photo_mode (delta);
            break;

        case GAME_STATE_DEMO_GATE:
            draw_demo_gate ();
            break;

        case GAME_STATE_GAME_OVER:
            draw_game_over_screen ();
            break;

        case GAME_STATE_VICTORY:
            draw_victory ();
            break;
        }

        grl_window_end_drawing (g_window);
    }

cleanup:
    g_print ("[Analytics] Session ended\n");
    cleanup_colors ();
    g_object_unref (g_window);

    return 0;
}
