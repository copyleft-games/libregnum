/* game-shmup-demo.c
 *
 * Copyright 2025 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Vertical scrolling shooter (shmup) demo using LrgShmupTemplate.
 * Features bullet patterns, power-ups, and boss encounters.
 *
 * Controls:
 *   Arrow keys / WASD - Movement
 *   Z / Space        - Primary fire (hold)
 *   X                - Bomb / Special
 *   C / Shift        - Focus mode (slow + show hitbox)
 *   Escape           - Pause / Quit
 *
 * Features demonstrated:
 *   - LrgShmupTemplate usage
 *   - Continuous vertical scrolling
 *   - Power level system
 *   - Bullet patterns (aimed, spiral, wave)
 *   - Boss encounters
 *   - Lives and continues
 *   - Bullet grazing
 */

#include <libregnum.h>
#include <graylib.h>
#include <locale.h>
#include <math.h>

/* ========================================================================== */
/* Type Declarations                                                          */
/* ========================================================================== */

#define SHMUP_TYPE_DEMO (shmup_demo_get_type ())
G_DECLARE_FINAL_TYPE (ShmupDemo, shmup_demo, SHMUP, DEMO, LrgShmupTemplate)

/* ========================================================================== */
/* Constants                                                                  */
/* ========================================================================== */

#define SCREEN_WIDTH          800
#define SCREEN_HEIGHT         900
#define PLAY_AREA_X           50
#define PLAY_AREA_Y           25
#define PLAY_AREA_WIDTH       500
#define PLAY_AREA_HEIGHT      850

#define PLAYER_SPEED          350.0f
#define PLAYER_FOCUS_SPEED    150.0f
#define PLAYER_WIDTH          24.0f
#define PLAYER_HEIGHT         32.0f
#define PLAYER_HITBOX         3.0f

#define BULLET_SPEED          500.0f
#define ENEMY_BULLET_SPEED    200.0f

#define MAX_PLAYER_BULLETS    100
#define MAX_ENEMY_BULLETS     500
#define MAX_ENEMIES           30
#define MAX_POWERUPS          10
#define MAX_EXPLOSIONS        20

#define SCROLL_SPEED          30.0f
#define STAR_LAYERS           3
#define STARS_PER_LAYER       50

/* ========================================================================== */
/* Enumerations                                                               */
/* ========================================================================== */

typedef enum
{
    ENEMY_SMALL,        /* Basic enemy, straight path */
    ENEMY_MEDIUM,       /* Shoots bullets */
    ENEMY_LARGE,        /* Tougher, multiple shots */
    ENEMY_BOSS          /* Stage boss */
} EnemyType;

typedef enum
{
    PATTERN_AIMED,      /* Aims at player */
    PATTERN_SPIRAL,     /* Spiral pattern */
    PATTERN_WAVE,       /* Wave pattern */
    PATTERN_SPREAD,     /* Fan spread */
    PATTERN_RING        /* Ring of bullets */
} BulletPattern;

typedef enum
{
    POWERUP_POWER,      /* Increase power level */
    POWERUP_BOMB,       /* Extra bomb */
    POWERUP_LIFE,       /* Extra life */
    POWERUP_POINT       /* Bonus points */
} PowerupType;

typedef enum
{
    STATE_PLAYING,
    STATE_PAUSED,
    STATE_GAME_OVER,
    STATE_BOSS_WARNING,
    STATE_BOSS_FIGHT,
    STATE_STAGE_CLEAR
} GameState;

/* ========================================================================== */
/* Data Structures                                                            */
/* ========================================================================== */

typedef struct
{
    gfloat    x, y;
    gfloat    vx, vy;
    gfloat    radius;
    gboolean  active;
    gboolean  grazed;     /* Already grazed by player */
} Bullet;

typedef struct
{
    gfloat         x, y;
    gfloat         vx, vy;
    gfloat         health;
    gfloat         max_health;
    gfloat         shoot_timer;
    gfloat         pattern_timer;
    gint           score_value;
    EnemyType      type;
    BulletPattern  pattern;
    gboolean       active;
} Enemy;

typedef struct
{
    gfloat      x, y;
    gfloat      vy;
    gfloat      lifetime;
    PowerupType type;
    gboolean    active;
} Powerup;

typedef struct
{
    gfloat   x, y;
    gfloat   radius;
    gfloat   timer;
    gboolean active;
} Explosion;

typedef struct
{
    gfloat x, y;
    gfloat speed;
} Star;

/* ========================================================================== */
/* Private Structure                                                          */
/* ========================================================================== */

struct _ShmupDemo
{
    LrgShmupTemplate parent_instance;

    /* Player */
    gfloat     player_x;
    gfloat     player_y;
    gfloat     player_shoot_timer;
    gfloat     invuln_timer;
    gboolean   respawning;
    gfloat     respawn_timer;

    /* Bullets */
    Bullet     player_bullets[MAX_PLAYER_BULLETS];
    Bullet     enemy_bullets[MAX_ENEMY_BULLETS];

    /* Enemies and effects */
    Enemy      enemies[MAX_ENEMIES];
    Powerup    powerups[MAX_POWERUPS];
    Explosion  explosions[MAX_EXPLOSIONS];

    /* Background */
    Star       stars[STAR_LAYERS][STARS_PER_LAYER];

    /* Stage */
    gint       stage;
    gfloat     stage_timer;
    gfloat     spawn_timer;
    gfloat     boss_timer;
    gboolean   boss_spawned;

    /* Scoring */
    gint64     score;
    gint64     high_score;

    /* Game state */
    GameState  state;
    gfloat     state_timer;
};

G_DEFINE_FINAL_TYPE (ShmupDemo, shmup_demo, LRG_TYPE_SHMUP_TEMPLATE)

/* ========================================================================== */
/* Forward Declarations                                                       */
/* ========================================================================== */

static void spawn_enemy          (ShmupDemo *self, EnemyType type, gfloat x, gfloat y);
static void spawn_boss           (ShmupDemo *self);
static void spawn_player_bullet  (ShmupDemo *self, gfloat x, gfloat y, gfloat angle);
static void spawn_enemy_bullet   (ShmupDemo *self, gfloat x, gfloat y, gfloat vx, gfloat vy);
static void spawn_powerup        (ShmupDemo *self, gfloat x, gfloat y, PowerupType type);
static void spawn_explosion      (ShmupDemo *self, gfloat x, gfloat y, gfloat radius);
static void fire_pattern         (ShmupDemo *self, Enemy *enemy);
static void player_die           (ShmupDemo *self);
static void reset_game           (ShmupDemo *self);
static void clear_bullets        (ShmupDemo *self);
static ShmupDemo *shmup_demo_new (void);

/* ========================================================================== */
/* Helper Functions                                                           */
/* ========================================================================== */

static gfloat
randf (gfloat min, gfloat max)
{
    return min + (gfloat)rand () / (gfloat)RAND_MAX * (max - min);
}

static gfloat
distance (gfloat x1, gfloat y1, gfloat x2, gfloat y2)
{
    gfloat dx = x2 - x1;
    gfloat dy = y2 - y1;
    return sqrtf (dx * dx + dy * dy);
}

static gboolean
in_play_area (gfloat x, gfloat y, gfloat margin)
{
    return x >= PLAY_AREA_X - margin &&
           x <= PLAY_AREA_X + PLAY_AREA_WIDTH + margin &&
           y >= PLAY_AREA_Y - margin &&
           y <= PLAY_AREA_Y + PLAY_AREA_HEIGHT + margin;
}

/* ========================================================================== */
/* Spawning Functions                                                         */
/* ========================================================================== */

static void
spawn_enemy (ShmupDemo *self, EnemyType type, gfloat x, gfloat y)
{
    Enemy *enemy = NULL;
    gint   i;

    for (i = 0; i < MAX_ENEMIES; i++)
    {
        if (!self->enemies[i].active)
        {
            enemy = &self->enemies[i];
            break;
        }
    }
    if (enemy == NULL)
        return;

    enemy->x = x;
    enemy->y = y;
    enemy->type = type;
    enemy->shoot_timer = randf (0.5f, 1.5f);
    enemy->pattern_timer = 0.0f;
    enemy->active = TRUE;

    switch (type)
    {
    case ENEMY_SMALL:
        enemy->health = 10.0f;
        enemy->max_health = 10.0f;
        enemy->vx = randf (-30.0f, 30.0f);
        enemy->vy = 80.0f;
        enemy->score_value = 100;
        enemy->pattern = PATTERN_AIMED;
        break;
    case ENEMY_MEDIUM:
        enemy->health = 30.0f;
        enemy->max_health = 30.0f;
        enemy->vx = randf (-20.0f, 20.0f);
        enemy->vy = 50.0f;
        enemy->score_value = 300;
        enemy->pattern = (rand () % 2 == 0) ? PATTERN_AIMED : PATTERN_SPREAD;
        break;
    case ENEMY_LARGE:
        enemy->health = 80.0f;
        enemy->max_health = 80.0f;
        enemy->vx = 0.0f;
        enemy->vy = 30.0f;
        enemy->score_value = 500;
        enemy->pattern = PATTERN_SPIRAL;
        break;
    case ENEMY_BOSS:
        enemy->health = 1000.0f;
        enemy->max_health = 1000.0f;
        enemy->vx = 0.0f;
        enemy->vy = 20.0f;
        enemy->score_value = 10000;
        enemy->pattern = PATTERN_RING;
        break;
    }
}

static void
spawn_boss (ShmupDemo *self)
{
    spawn_enemy (self, ENEMY_BOSS,
                 PLAY_AREA_X + PLAY_AREA_WIDTH / 2.0f,
                 PLAY_AREA_Y - 50.0f);
    self->boss_spawned = TRUE;
    self->state = STATE_BOSS_FIGHT;
    lrg_shmup_template_set_scroll_paused (LRG_SHMUP_TEMPLATE (self), TRUE);
}

static void
spawn_player_bullet (ShmupDemo *self, gfloat x, gfloat y, gfloat angle)
{
    Bullet *bullet = NULL;
    gint    i;

    for (i = 0; i < MAX_PLAYER_BULLETS; i++)
    {
        if (!self->player_bullets[i].active)
        {
            bullet = &self->player_bullets[i];
            break;
        }
    }
    if (bullet == NULL)
        return;

    bullet->x = x;
    bullet->y = y;
    bullet->vx = sinf (angle) * BULLET_SPEED * 0.3f;
    bullet->vy = -BULLET_SPEED;
    bullet->radius = 4.0f;
    bullet->active = TRUE;
    bullet->grazed = FALSE;
}

static void
spawn_enemy_bullet (ShmupDemo *self, gfloat x, gfloat y, gfloat vx, gfloat vy)
{
    Bullet *bullet = NULL;
    gint    i;

    for (i = 0; i < MAX_ENEMY_BULLETS; i++)
    {
        if (!self->enemy_bullets[i].active)
        {
            bullet = &self->enemy_bullets[i];
            break;
        }
    }
    if (bullet == NULL)
        return;

    bullet->x = x;
    bullet->y = y;
    bullet->vx = vx;
    bullet->vy = vy;
    bullet->radius = 5.0f;
    bullet->active = TRUE;
    bullet->grazed = FALSE;
}

static void
spawn_powerup (ShmupDemo *self, gfloat x, gfloat y, PowerupType type)
{
    Powerup *powerup = NULL;
    gint     i;

    for (i = 0; i < MAX_POWERUPS; i++)
    {
        if (!self->powerups[i].active)
        {
            powerup = &self->powerups[i];
            break;
        }
    }
    if (powerup == NULL)
        return;

    powerup->x = x;
    powerup->y = y;
    powerup->vy = 60.0f;
    powerup->lifetime = 8.0f;
    powerup->type = type;
    powerup->active = TRUE;
}

static void
spawn_explosion (ShmupDemo *self, gfloat x, gfloat y, gfloat radius)
{
    Explosion *exp = NULL;
    gint       i;

    for (i = 0; i < MAX_EXPLOSIONS; i++)
    {
        if (!self->explosions[i].active)
        {
            exp = &self->explosions[i];
            break;
        }
    }
    if (exp == NULL)
        return;

    exp->x = x;
    exp->y = y;
    exp->radius = radius;
    exp->timer = 0.4f;
    exp->active = TRUE;
}

/* ========================================================================== */
/* Bullet Patterns                                                            */
/* ========================================================================== */

static void
fire_pattern (ShmupDemo *self, Enemy *enemy)
{
    gfloat angle;
    gfloat speed;
    gint   i, count;

    switch (enemy->pattern)
    {
    case PATTERN_AIMED:
        angle = atan2f (self->player_y - enemy->y, self->player_x - enemy->x);
        speed = ENEMY_BULLET_SPEED;
        spawn_enemy_bullet (self, enemy->x, enemy->y,
                            cosf (angle) * speed, sinf (angle) * speed);
        break;

    case PATTERN_SPREAD:
        count = 3;
        for (i = 0; i < count; i++)
        {
            angle = G_PI / 2.0f + (i - count / 2) * 0.3f;
            speed = ENEMY_BULLET_SPEED;
            spawn_enemy_bullet (self, enemy->x, enemy->y,
                                cosf (angle) * speed, sinf (angle) * speed);
        }
        break;

    case PATTERN_SPIRAL:
        angle = enemy->pattern_timer * 3.0f;
        speed = ENEMY_BULLET_SPEED * 0.8f;
        spawn_enemy_bullet (self, enemy->x, enemy->y,
                            cosf (angle) * speed, sinf (angle) * speed);
        spawn_enemy_bullet (self, enemy->x, enemy->y,
                            cosf (angle + G_PI) * speed, sinf (angle + G_PI) * speed);
        break;

    case PATTERN_WAVE:
        angle = G_PI / 2.0f + sinf (enemy->pattern_timer * 2.0f) * 0.5f;
        speed = ENEMY_BULLET_SPEED;
        spawn_enemy_bullet (self, enemy->x, enemy->y,
                            cosf (angle) * speed, sinf (angle) * speed);
        break;

    case PATTERN_RING:
        count = 12;
        for (i = 0; i < count; i++)
        {
            angle = (i * 2.0f * G_PI / count) + enemy->pattern_timer;
            speed = ENEMY_BULLET_SPEED * 0.7f;
            spawn_enemy_bullet (self, enemy->x, enemy->y,
                                cosf (angle) * speed, sinf (angle) * speed);
        }
        break;
    }
}

/* ========================================================================== */
/* Game Logic                                                                 */
/* ========================================================================== */

static void
player_die (ShmupDemo *self)
{
    gint remaining;
    gint power;

    spawn_explosion (self, self->player_x, self->player_y, 40.0f);

    remaining = lrg_shmup_template_lose_life (LRG_SHMUP_TEMPLATE (self));

    if (remaining < 0)
    {
        /* Check for continues */
        if (lrg_shmup_template_use_continue (LRG_SHMUP_TEMPLATE (self)))
        {
            self->respawning = TRUE;
            self->respawn_timer = 2.0f;
            clear_bullets (self);
        }
        else
        {
            self->state = STATE_GAME_OVER;
            if (self->score > self->high_score)
                self->high_score = self->score;
        }
    }
    else
    {
        self->respawning = TRUE;
        self->respawn_timer = 2.0f;
        self->invuln_timer = 3.0f;
        clear_bullets (self);

        /* Reset power on death */
        power = lrg_shmup_template_get_power_level (LRG_SHMUP_TEMPLATE (self));
        if (power > 0)
            lrg_shmup_template_set_power_level (LRG_SHMUP_TEMPLATE (self), power - 1);
    }
}

static void
clear_bullets (ShmupDemo *self)
{
    gint i;

    for (i = 0; i < MAX_ENEMY_BULLETS; i++)
        self->enemy_bullets[i].active = FALSE;
}

static void
reset_game (ShmupDemo *self)
{
    gint i, j;

    self->player_x = PLAY_AREA_X + PLAY_AREA_WIDTH / 2.0f;
    self->player_y = PLAY_AREA_Y + PLAY_AREA_HEIGHT - 80.0f;
    self->player_shoot_timer = 0.0f;
    self->invuln_timer = 2.0f;
    self->respawning = FALSE;

    for (i = 0; i < MAX_PLAYER_BULLETS; i++)
        self->player_bullets[i].active = FALSE;
    for (i = 0; i < MAX_ENEMY_BULLETS; i++)
        self->enemy_bullets[i].active = FALSE;
    for (i = 0; i < MAX_ENEMIES; i++)
        self->enemies[i].active = FALSE;
    for (i = 0; i < MAX_POWERUPS; i++)
        self->powerups[i].active = FALSE;
    for (i = 0; i < MAX_EXPLOSIONS; i++)
        self->explosions[i].active = FALSE;

    /* Initialize stars */
    for (i = 0; i < STAR_LAYERS; i++)
    {
        for (j = 0; j < STARS_PER_LAYER; j++)
        {
            self->stars[i][j].x = randf (PLAY_AREA_X, PLAY_AREA_X + PLAY_AREA_WIDTH);
            self->stars[i][j].y = randf (PLAY_AREA_Y, PLAY_AREA_Y + PLAY_AREA_HEIGHT);
            self->stars[i][j].speed = 20.0f + i * 30.0f;
        }
    }

    self->stage = 1;
    self->stage_timer = 0.0f;
    self->spawn_timer = 0.0f;
    self->boss_spawned = FALSE;
    self->score = 0;

    lrg_shmup_template_set_lives (LRG_SHMUP_TEMPLATE (self), 3);
    lrg_shmup_template_set_bombs (LRG_SHMUP_TEMPLATE (self), 3);
    lrg_shmup_template_set_continues (LRG_SHMUP_TEMPLATE (self), 2);
    lrg_shmup_template_set_power_level (LRG_SHMUP_TEMPLATE (self), 0);
    lrg_shmup_template_set_scroll_paused (LRG_SHMUP_TEMPLATE (self), FALSE);

    self->state = STATE_PLAYING;
}

/* ========================================================================== */
/* Update Functions                                                           */
/* ========================================================================== */

static void
update_player (ShmupDemo *self, gfloat delta)
{
    gfloat speed;
    gfloat dx = 0.0f, dy = 0.0f;
    gint   power;
    gint   shots;
    gfloat spread;
    gint   i;

    if (self->respawning)
    {
        self->respawn_timer -= delta;
        if (self->respawn_timer <= 0.0f)
        {
            self->respawning = FALSE;
            self->player_x = PLAY_AREA_X + PLAY_AREA_WIDTH / 2.0f;
            self->player_y = PLAY_AREA_Y + PLAY_AREA_HEIGHT - 80.0f;
        }
        return;
    }

    /* Focus mode */
    if (grl_input_is_key_down (GRL_KEY_C) || grl_input_is_key_down (GRL_KEY_LEFT_SHIFT))
    {
        lrg_shmup_template_set_focused (LRG_SHMUP_TEMPLATE (self), TRUE);
        lrg_shmup_template_set_show_hitbox (LRG_SHMUP_TEMPLATE (self), TRUE);
        speed = PLAYER_FOCUS_SPEED;
    }
    else
    {
        lrg_shmup_template_set_focused (LRG_SHMUP_TEMPLATE (self), FALSE);
        lrg_shmup_template_set_show_hitbox (LRG_SHMUP_TEMPLATE (self), FALSE);
        speed = PLAYER_SPEED;
    }

    /* Movement */
    if (grl_input_is_key_down (GRL_KEY_UP) || grl_input_is_key_down (GRL_KEY_W))
        dy -= 1.0f;
    if (grl_input_is_key_down (GRL_KEY_DOWN) || grl_input_is_key_down (GRL_KEY_S))
        dy += 1.0f;
    if (grl_input_is_key_down (GRL_KEY_LEFT) || grl_input_is_key_down (GRL_KEY_A))
        dx -= 1.0f;
    if (grl_input_is_key_down (GRL_KEY_RIGHT) || grl_input_is_key_down (GRL_KEY_D))
        dx += 1.0f;

    /* Normalize diagonal movement */
    if (dx != 0.0f && dy != 0.0f)
    {
        dx *= 0.707f;
        dy *= 0.707f;
    }

    self->player_x += dx * speed * delta;
    self->player_y += dy * speed * delta;

    /* Clamp to play area */
    if (self->player_x < PLAY_AREA_X + PLAYER_WIDTH / 2.0f)
        self->player_x = PLAY_AREA_X + PLAYER_WIDTH / 2.0f;
    if (self->player_x > PLAY_AREA_X + PLAY_AREA_WIDTH - PLAYER_WIDTH / 2.0f)
        self->player_x = PLAY_AREA_X + PLAY_AREA_WIDTH - PLAYER_WIDTH / 2.0f;
    if (self->player_y < PLAY_AREA_Y + PLAYER_HEIGHT / 2.0f)
        self->player_y = PLAY_AREA_Y + PLAYER_HEIGHT / 2.0f;
    if (self->player_y > PLAY_AREA_Y + PLAY_AREA_HEIGHT - PLAYER_HEIGHT / 2.0f)
        self->player_y = PLAY_AREA_Y + PLAY_AREA_HEIGHT - PLAYER_HEIGHT / 2.0f;

    /* Update invulnerability */
    if (self->invuln_timer > 0.0f)
        self->invuln_timer -= delta;

    /* Shooting */
    self->player_shoot_timer -= delta;
    if ((grl_input_is_key_down (GRL_KEY_Z) || grl_input_is_key_down (GRL_KEY_SPACE)) &&
        self->player_shoot_timer <= 0.0f)
    {
        power = lrg_shmup_template_get_power_level (LRG_SHMUP_TEMPLATE (self));
        shots = 1 + power;
        if (shots > 5) shots = 5;

        spread = 0.15f;
        for (i = 0; i < shots; i++)
        {
            gfloat angle = (i - (shots - 1) / 2.0f) * spread;
            spawn_player_bullet (self, self->player_x, self->player_y - 10.0f, angle);
        }

        self->player_shoot_timer = 0.08f - power * 0.01f;
        if (self->player_shoot_timer < 0.04f)
            self->player_shoot_timer = 0.04f;
    }

    /* Bomb */
    if (grl_input_is_key_pressed (GRL_KEY_X))
    {
        if (lrg_shmup_template_use_bomb (LRG_SHMUP_TEMPLATE (self)))
        {
            clear_bullets (self);
            self->invuln_timer = 2.0f;

            /* Damage all enemies */
            for (i = 0; i < MAX_ENEMIES; i++)
            {
                if (self->enemies[i].active)
                {
                    self->enemies[i].health -= 50.0f;
                }
            }
        }
    }
}

static void
update_enemies (ShmupDemo *self, gfloat delta)
{
    gint i;

    for (i = 0; i < MAX_ENEMIES; i++)
    {
        Enemy *e = &self->enemies[i];
        if (!e->active)
            continue;

        e->x += e->vx * delta;
        e->y += e->vy * delta;
        e->pattern_timer += delta;

        /* Boss behavior */
        if (e->type == ENEMY_BOSS)
        {
            if (e->y > PLAY_AREA_Y + 120.0f)
            {
                e->vy = 0.0f;
                e->vx = sinf (self->stage_timer * 0.5f) * 80.0f;
            }
        }

        /* Shooting */
        e->shoot_timer -= delta;
        if (e->shoot_timer <= 0.0f && e->type != ENEMY_SMALL)
        {
            fire_pattern (self, e);

            switch (e->type)
            {
            case ENEMY_MEDIUM:
                e->shoot_timer = 1.5f;
                break;
            case ENEMY_LARGE:
                e->shoot_timer = 0.15f;
                break;
            case ENEMY_BOSS:
                e->shoot_timer = 0.3f;
                /* Change patterns */
                if ((gint)(e->pattern_timer) % 5 == 0)
                    e->pattern = (e->pattern + 1) % 5;
                break;
            default:
                e->shoot_timer = 2.0f;
                break;
            }
        }

        /* Remove if off screen */
        if (!in_play_area (e->x, e->y, 100.0f))
        {
            e->active = FALSE;
        }

        /* Check if dead */
        if (e->health <= 0.0f)
        {
            spawn_explosion (self, e->x, e->y,
                             e->type == ENEMY_BOSS ? 80.0f : 20.0f + e->type * 10.0f);
            self->score += e->score_value;

            /* Drop powerup */
            if (randf (0.0f, 1.0f) < 0.3f || e->type == ENEMY_BOSS)
            {
                PowerupType type = POWERUP_POWER;
                if (randf (0.0f, 1.0f) < 0.2f)
                    type = POWERUP_BOMB;
                if (e->type == ENEMY_BOSS)
                    type = POWERUP_LIFE;

                spawn_powerup (self, e->x, e->y, type);
            }

            if (e->type == ENEMY_BOSS)
            {
                self->state = STATE_STAGE_CLEAR;
                self->state_timer = 3.0f;
            }

            e->active = FALSE;
        }
    }
}

static void
update_player_bullets (ShmupDemo *self, gfloat delta)
{
    gint i, j;

    for (i = 0; i < MAX_PLAYER_BULLETS; i++)
    {
        Bullet *b = &self->player_bullets[i];
        if (!b->active)
            continue;

        b->x += b->vx * delta;
        b->y += b->vy * delta;

        if (!in_play_area (b->x, b->y, 10.0f))
        {
            b->active = FALSE;
            continue;
        }

        /* Check enemy collisions */
        for (j = 0; j < MAX_ENEMIES; j++)
        {
            Enemy *e = &self->enemies[j];
            gfloat enemy_radius;

            if (!e->active)
                continue;

            enemy_radius = 15.0f + e->type * 10.0f;
            if (e->type == ENEMY_BOSS)
                enemy_radius = 50.0f;

            if (distance (b->x, b->y, e->x, e->y) < enemy_radius + b->radius)
            {
                e->health -= 10.0f;
                b->active = FALSE;
                break;
            }
        }
    }
}

static void
update_enemy_bullets (ShmupDemo *self, gfloat delta)
{
    gint   i;
    gfloat hitbox;
    gfloat graze_radius;

    if (self->respawning)
        return;

    hitbox = lrg_shmup_template_get_hitbox_radius (LRG_SHMUP_TEMPLATE (self));
    graze_radius = lrg_shmup_template_get_graze_radius (LRG_SHMUP_TEMPLATE (self));

    for (i = 0; i < MAX_ENEMY_BULLETS; i++)
    {
        Bullet *b = &self->enemy_bullets[i];
        gfloat  dist;

        if (!b->active)
            continue;

        b->x += b->vx * delta;
        b->y += b->vy * delta;

        if (!in_play_area (b->x, b->y, 20.0f))
        {
            b->active = FALSE;
            continue;
        }

        dist = distance (b->x, b->y, self->player_x, self->player_y);

        /* Graze detection */
        if (!b->grazed && dist < graze_radius + b->radius)
        {
            lrg_shmup_template_add_graze (LRG_SHMUP_TEMPLATE (self));
            self->score += lrg_shmup_template_get_graze_points (LRG_SHMUP_TEMPLATE (self));
            b->grazed = TRUE;
        }

        /* Hit detection */
        if (self->invuln_timer <= 0.0f && dist < hitbox + b->radius)
        {
            player_die (self);
            b->active = FALSE;
        }
    }
}

static void
update_powerups (ShmupDemo *self, gfloat delta)
{
    gint i;

    for (i = 0; i < MAX_POWERUPS; i++)
    {
        Powerup *p = &self->powerups[i];
        if (!p->active)
            continue;

        p->y += p->vy * delta;
        p->lifetime -= delta;

        if (p->lifetime <= 0.0f || p->y > PLAY_AREA_Y + PLAY_AREA_HEIGHT + 20.0f)
        {
            p->active = FALSE;
            continue;
        }

        /* Collect */
        if (distance (p->x, p->y, self->player_x, self->player_y) < 25.0f)
        {
            switch (p->type)
            {
            case POWERUP_POWER:
                lrg_shmup_template_add_power (LRG_SHMUP_TEMPLATE (self), 1);
                self->score += 500;
                break;
            case POWERUP_BOMB:
                {
                    gint bombs = lrg_shmup_template_get_bombs (LRG_SHMUP_TEMPLATE (self));
                    lrg_shmup_template_set_bombs (LRG_SHMUP_TEMPLATE (self), bombs + 1);
                    self->score += 1000;
                }
                break;
            case POWERUP_LIFE:
                {
                    gint lives = lrg_shmup_template_get_lives (LRG_SHMUP_TEMPLATE (self));
                    lrg_shmup_template_set_lives (LRG_SHMUP_TEMPLATE (self), lives + 1);
                    self->score += 5000;
                }
                break;
            case POWERUP_POINT:
                self->score += 10000;
                break;
            }
            p->active = FALSE;
        }
    }
}

static void
update_explosions (ShmupDemo *self, gfloat delta)
{
    gint i;

    for (i = 0; i < MAX_EXPLOSIONS; i++)
    {
        Explosion *e = &self->explosions[i];
        if (!e->active)
            continue;

        e->timer -= delta;
        if (e->timer <= 0.0f)
            e->active = FALSE;
    }
}

static void
update_stars (ShmupDemo *self, gfloat delta)
{
    gint i, j;

    for (i = 0; i < STAR_LAYERS; i++)
    {
        for (j = 0; j < STARS_PER_LAYER; j++)
        {
            Star *s = &self->stars[i][j];
            s->y += s->speed * delta;

            if (s->y > PLAY_AREA_Y + PLAY_AREA_HEIGHT)
            {
                s->y = PLAY_AREA_Y;
                s->x = randf (PLAY_AREA_X, PLAY_AREA_X + PLAY_AREA_WIDTH);
            }
        }
    }
}

static void
update_spawning (ShmupDemo *self, gfloat delta)
{
    gfloat spawn_interval;
    EnemyType type;

    if (self->boss_spawned)
        return;

    self->stage_timer += delta;
    self->spawn_timer += delta;

    /* Boss warning at 60 seconds */
    if (self->stage_timer > 55.0f && self->state == STATE_PLAYING)
    {
        self->state = STATE_BOSS_WARNING;
        self->state_timer = 3.0f;
    }

    /* Boss spawn */
    if (self->stage_timer > 60.0f)
    {
        spawn_boss (self);
        return;
    }

    /* Regular spawning */
    spawn_interval = 2.0f - self->stage_timer * 0.02f;
    if (spawn_interval < 0.5f)
        spawn_interval = 0.5f;

    if (self->spawn_timer >= spawn_interval)
    {
        self->spawn_timer = 0.0f;

        /* Enemy type based on stage progress */
        if (self->stage_timer < 20.0f)
        {
            type = ENEMY_SMALL;
        }
        else if (self->stage_timer < 40.0f)
        {
            type = (rand () % 3 == 0) ? ENEMY_MEDIUM : ENEMY_SMALL;
        }
        else
        {
            gint r = rand () % 4;
            if (r == 0)
                type = ENEMY_LARGE;
            else if (r == 1)
                type = ENEMY_MEDIUM;
            else
                type = ENEMY_SMALL;
        }

        spawn_enemy (self, type,
                     randf (PLAY_AREA_X + 30.0f, PLAY_AREA_X + PLAY_AREA_WIDTH - 30.0f),
                     PLAY_AREA_Y - 30.0f);
    }
}

/* ========================================================================== */
/* Rendering Functions                                                        */
/* ========================================================================== */

static void
render_stars (ShmupDemo *self)
{
    gint i, j;

    for (i = 0; i < STAR_LAYERS; i++)
    {
        guint8 brightness = 80 + i * 60;
        g_autoptr(GrlColor) color = grl_color_new (brightness, brightness, brightness, 255);

        for (j = 0; j < STARS_PER_LAYER; j++)
        {
            Star *s = &self->stars[i][j];
            grl_draw_circle (s->x, s->y, 1.0f + i * 0.5f, color);
        }
    }
}

static void
render_play_area (ShmupDemo *self)
{
    g_autoptr(GrlColor) bg = grl_color_new (10, 10, 25, 255);
    g_autoptr(GrlColor) border = grl_color_new (100, 100, 150, 255);
    g_autoptr(GrlRectangle) play_rect = NULL;

    grl_draw_rectangle (PLAY_AREA_X, PLAY_AREA_Y, PLAY_AREA_WIDTH, PLAY_AREA_HEIGHT, bg);
    render_stars (self);
    play_rect = grl_rectangle_new (PLAY_AREA_X, PLAY_AREA_Y, PLAY_AREA_WIDTH, PLAY_AREA_HEIGHT);
    grl_draw_rectangle_lines_ex (play_rect, 2.0f, border);
}

static void
render_player (ShmupDemo *self)
{
    g_autoptr(GrlColor) ship_color = grl_color_new (100, 200, 255, 255);
    g_autoptr(GrlColor) hitbox_color = grl_color_new (255, 255, 255, 200);
    g_autoptr(GrlVector2) v1 = NULL;
    g_autoptr(GrlVector2) v2 = NULL;
    g_autoptr(GrlVector2) v3 = NULL;
    gboolean   show_hitbox;
    gfloat     hitbox;

    if (self->respawning)
        return;

    /* Blink when invulnerable */
    if (self->invuln_timer > 0.0f)
    {
        if ((gint)(self->invuln_timer * 10.0f) % 2 == 0)
            return;
    }

    /* Ship triangle */
    v1 = grl_vector2_new (self->player_x, self->player_y - PLAYER_HEIGHT / 2.0f);
    v2 = grl_vector2_new (self->player_x - PLAYER_WIDTH / 2.0f, self->player_y + PLAYER_HEIGHT / 2.0f);
    v3 = grl_vector2_new (self->player_x + PLAYER_WIDTH / 2.0f, self->player_y + PLAYER_HEIGHT / 2.0f);
    grl_draw_triangle (v1, v2, v3, ship_color);

    /* Hitbox */
    show_hitbox = lrg_shmup_template_get_show_hitbox (LRG_SHMUP_TEMPLATE (self));
    if (show_hitbox)
    {
        hitbox = lrg_shmup_template_get_hitbox_radius (LRG_SHMUP_TEMPLATE (self));
        grl_draw_circle (self->player_x, self->player_y, hitbox, hitbox_color);
    }
}

static void
render_enemies (ShmupDemo *self)
{
    gint i;

    for (i = 0; i < MAX_ENEMIES; i++)
    {
        Enemy              *e = &self->enemies[i];
        g_autoptr(GrlColor) color = NULL;
        gfloat              radius = 12.0f;

        if (!e->active)
            continue;

        switch (e->type)
        {
        case ENEMY_SMALL:
            color = grl_color_new (255, 100, 100, 255);
            radius = 12.0f;
            break;
        case ENEMY_MEDIUM:
            color = grl_color_new (255, 150, 50, 255);
            radius = 18.0f;
            break;
        case ENEMY_LARGE:
            color = grl_color_new (255, 200, 100, 255);
            radius = 25.0f;
            break;
        case ENEMY_BOSS:
            color = grl_color_new (200, 50, 255, 255);
            radius = 50.0f;
            break;
        }

        grl_draw_circle (e->x, e->y, radius, color);

        /* Health bar for boss */
        if (e->type == ENEMY_BOSS)
        {
            gfloat bar_width = PLAY_AREA_WIDTH - 40.0f;
            gfloat health_pct = e->health / e->max_health;

            g_autoptr(GrlColor) bar_bg = grl_color_new (50, 50, 50, 200);
            g_autoptr(GrlColor) bar_fg = grl_color_new (255, 50, 50, 255);

            grl_draw_rectangle (PLAY_AREA_X + 20.0f, PLAY_AREA_Y + 10.0f,
                                bar_width, 10.0f, bar_bg);
            grl_draw_rectangle (PLAY_AREA_X + 20.0f, PLAY_AREA_Y + 10.0f,
                                bar_width * health_pct, 10.0f, bar_fg);
        }
    }
}

static void
render_bullets (ShmupDemo *self)
{
    gint i;
    g_autoptr(GrlColor) player_bullet_color = grl_color_new (100, 255, 200, 255);
    g_autoptr(GrlColor) enemy_bullet_color = grl_color_new (255, 100, 150, 255);

    for (i = 0; i < MAX_PLAYER_BULLETS; i++)
    {
        Bullet *b = &self->player_bullets[i];
        if (!b->active)
            continue;
        grl_draw_circle (b->x, b->y, b->radius, player_bullet_color);
    }

    for (i = 0; i < MAX_ENEMY_BULLETS; i++)
    {
        Bullet *b = &self->enemy_bullets[i];
        if (!b->active)
            continue;
        grl_draw_circle (b->x, b->y, b->radius, enemy_bullet_color);
    }
}

static void
render_powerups (ShmupDemo *self)
{
    gint i;

    for (i = 0; i < MAX_POWERUPS; i++)
    {
        Powerup            *p = &self->powerups[i];
        g_autoptr(GrlColor) color = NULL;
        gfloat              pulse;

        if (!p->active)
            continue;

        pulse = 1.0f + sinf (p->lifetime * 5.0f) * 0.2f;

        switch (p->type)
        {
        case POWERUP_POWER:
            color = grl_color_new (255, 100, 100, 255);
            break;
        case POWERUP_BOMB:
            color = grl_color_new (100, 100, 255, 255);
            break;
        case POWERUP_LIFE:
            color = grl_color_new (100, 255, 100, 255);
            break;
        case POWERUP_POINT:
            color = grl_color_new (255, 255, 100, 255);
            break;
        }

        grl_draw_rectangle (p->x - 8.0f * pulse, p->y - 8.0f * pulse,
                            16.0f * pulse, 16.0f * pulse, color);
    }
}

static void
render_explosions (ShmupDemo *self)
{
    gint i;

    for (i = 0; i < MAX_EXPLOSIONS; i++)
    {
        Explosion          *e = &self->explosions[i];
        g_autoptr(GrlColor) color = NULL;
        gfloat              alpha;
        gfloat              current_radius;

        if (!e->active)
            continue;

        alpha = e->timer / 0.4f;
        current_radius = e->radius * (1.0f - alpha * 0.5f);

        color = grl_color_new (255, 200, 100, (guint8)(alpha * 200));
        grl_draw_circle (e->x, e->y, current_radius, color);
    }
}

static void
render_hud (ShmupDemo *self)
{
    g_autoptr(GrlColor) white = grl_color_new (255, 255, 255, 255);
    g_autoptr(GrlColor) yellow = grl_color_new (255, 200, 50, 255);
    gchar               text[64];
    gfloat              hud_x = PLAY_AREA_X + PLAY_AREA_WIDTH + 20.0f;
    gint                lives, bombs, power, max_power;
    guint               grazes;

    lives = lrg_shmup_template_get_lives (LRG_SHMUP_TEMPLATE (self));
    bombs = lrg_shmup_template_get_bombs (LRG_SHMUP_TEMPLATE (self));
    power = lrg_shmup_template_get_power_level (LRG_SHMUP_TEMPLATE (self));
    max_power = lrg_shmup_template_get_max_power_level (LRG_SHMUP_TEMPLATE (self));
    grazes = lrg_shmup_template_get_graze_count (LRG_SHMUP_TEMPLATE (self));

    /* Score */
    g_snprintf (text, sizeof (text), "Score");
    grl_draw_text (text, hud_x, 50, 16, white);
    g_snprintf (text, sizeof (text), "%'" G_GINT64_FORMAT, self->score);
    grl_draw_text (text, hud_x, 70, 20, yellow);

    /* High Score */
    g_snprintf (text, sizeof (text), "High Score");
    grl_draw_text (text, hud_x, 110, 16, white);
    g_snprintf (text, sizeof (text), "%'" G_GINT64_FORMAT, self->high_score);
    grl_draw_text (text, hud_x, 130, 18, white);

    /* Lives */
    g_snprintf (text, sizeof (text), "Lives: %d", lives);
    grl_draw_text (text, hud_x, 180, 18, white);

    /* Bombs */
    g_snprintf (text, sizeof (text), "Bombs: %d", bombs);
    grl_draw_text (text, hud_x, 210, 18, white);

    /* Power */
    g_snprintf (text, sizeof (text), "Power: %d/%d", power, max_power);
    grl_draw_text (text, hud_x, 250, 18, white);

    /* Graze */
    g_snprintf (text, sizeof (text), "Graze: %u", grazes);
    grl_draw_text (text, hud_x, 290, 18, white);

    /* Stage */
    g_snprintf (text, sizeof (text), "Stage %d", self->stage);
    grl_draw_text (text, hud_x, 340, 20, yellow);

    /* Controls */
    grl_draw_text ("Controls:", hud_x, 420, 16, white);
    grl_draw_text ("Arrow/WASD: Move", hud_x, 445, 14, white);
    grl_draw_text ("Z/Space: Fire", hud_x, 465, 14, white);
    grl_draw_text ("X: Bomb", hud_x, 485, 14, white);
    grl_draw_text ("C/Shift: Focus", hud_x, 505, 14, white);
}

static void
render_game_over (ShmupDemo *self)
{
    g_autoptr(GrlColor) overlay = grl_color_new (0, 0, 0, 180);
    g_autoptr(GrlColor) white = grl_color_new (255, 255, 255, 255);
    g_autoptr(GrlColor) yellow = grl_color_new (255, 200, 50, 255);
    gchar               text[64];
    gfloat              cx = PLAY_AREA_X + PLAY_AREA_WIDTH / 2.0f;
    gfloat              cy = PLAY_AREA_Y + PLAY_AREA_HEIGHT / 2.0f;

    grl_draw_rectangle (PLAY_AREA_X, PLAY_AREA_Y, PLAY_AREA_WIDTH, PLAY_AREA_HEIGHT, overlay);

    grl_draw_text ("GAME OVER", cx - 80, cy - 40, 32, white);

    g_snprintf (text, sizeof (text), "Score: %" G_GINT64_FORMAT, self->score);
    grl_draw_text (text, cx - 60, cy + 10, 20, yellow);

    grl_draw_text ("Press ENTER to restart", cx - 90, cy + 60, 16, white);
}

static void
render_boss_warning (ShmupDemo *self)
{
    g_autoptr(GrlColor) red = grl_color_new (255, 50, 50, 255);
    gfloat              cx = PLAY_AREA_X + PLAY_AREA_WIDTH / 2.0f;
    gfloat              cy = PLAY_AREA_Y + PLAY_AREA_HEIGHT / 2.0f;

    if ((gint)(self->state_timer * 3.0f) % 2 == 0)
    {
        grl_draw_text ("WARNING!", cx - 60, cy - 20, 32, red);
        grl_draw_text ("BOSS APPROACHING", cx - 90, cy + 20, 20, red);
    }
}

static void
render_stage_clear (ShmupDemo *self)
{
    g_autoptr(GrlColor) white = grl_color_new (255, 255, 255, 255);
    g_autoptr(GrlColor) yellow = grl_color_new (255, 200, 50, 255);
    gfloat              cx = PLAY_AREA_X + PLAY_AREA_WIDTH / 2.0f;
    gfloat              cy = PLAY_AREA_Y + PLAY_AREA_HEIGHT / 2.0f;

    grl_draw_text ("STAGE CLEAR!", cx - 80, cy - 20, 28, yellow);
    grl_draw_text ("Congratulations!", cx - 70, cy + 20, 18, white);
}

/* ========================================================================== */
/* Virtual Method Overrides                                                   */
/* ========================================================================== */

static void
shmup_demo_pre_update (LrgGameTemplate *template, gdouble delta)
{
    ShmupDemo *self = SHMUP_DEMO (template);

    /* Chain up */
    LRG_GAME_TEMPLATE_CLASS (shmup_demo_parent_class)->pre_update (template, delta);

    /* Pause toggle */
    if (grl_input_is_key_pressed (GRL_KEY_ESCAPE))
    {
        if (self->state == STATE_PLAYING || self->state == STATE_BOSS_FIGHT)
            self->state = STATE_PAUSED;
        else if (self->state == STATE_PAUSED)
            self->state = STATE_PLAYING;
    }

    /* Restart */
    if (self->state == STATE_GAME_OVER && grl_input_is_key_pressed (GRL_KEY_ENTER))
    {
        reset_game (self);
        return;
    }

    if (self->state == STATE_PAUSED)
        return;

    /* Boss warning timer */
    if (self->state == STATE_BOSS_WARNING)
    {
        self->state_timer -= delta;
        if (self->state_timer <= 0.0f)
        {
            spawn_boss (self);
        }
        update_player (self, (gfloat)delta);
        update_player_bullets (self, (gfloat)delta);
        update_enemy_bullets (self, (gfloat)delta);
        update_explosions (self, (gfloat)delta);
        update_stars (self, (gfloat)delta);
        return;
    }

    /* Stage clear */
    if (self->state == STATE_STAGE_CLEAR)
    {
        self->state_timer -= (gfloat)delta;
        if (self->state_timer <= 0.0f)
        {
            /* Next stage or end */
            self->stage++;
            self->stage_timer = 0.0f;
            self->boss_spawned = FALSE;
            self->state = STATE_PLAYING;
            lrg_shmup_template_set_scroll_paused (LRG_SHMUP_TEMPLATE (self), FALSE);
        }
        update_explosions (self, (gfloat)delta);
        update_stars (self, (gfloat)delta);
        return;
    }

    if (self->state == STATE_GAME_OVER)
        return;

    update_player (self, (gfloat)delta);
    update_enemies (self, (gfloat)delta);
    update_player_bullets (self, (gfloat)delta);
    update_enemy_bullets (self, (gfloat)delta);
    update_powerups (self, (gfloat)delta);
    update_explosions (self, (gfloat)delta);
    update_stars (self, (gfloat)delta);

    if (self->state == STATE_PLAYING)
        update_spawning (self, (gfloat)delta);
}

static void
shmup_demo_pre_draw (LrgGameTemplate *template)
{
    ShmupDemo         *self = SHMUP_DEMO (template);
    g_autoptr(GrlColor) clear_color = grl_color_new (5, 5, 15, 255);

    grl_draw_clear_background (clear_color);

    render_play_area (self);
    render_powerups (self);
    render_bullets (self);
    render_enemies (self);
    render_player (self);
    render_explosions (self);
    render_hud (self);

    if (self->state == STATE_GAME_OVER)
        render_game_over (self);
    else if (self->state == STATE_BOSS_WARNING)
        render_boss_warning (self);
    else if (self->state == STATE_STAGE_CLEAR)
        render_stage_clear (self);
    else if (self->state == STATE_PAUSED)
    {
        g_autoptr(GrlColor) overlay = grl_color_new (0, 0, 0, 150);
        g_autoptr(GrlColor) white = grl_color_new (255, 255, 255, 255);
        gfloat              cx = PLAY_AREA_X + PLAY_AREA_WIDTH / 2.0f;
        gfloat              cy = PLAY_AREA_Y + PLAY_AREA_HEIGHT / 2.0f;

        grl_draw_rectangle (PLAY_AREA_X, PLAY_AREA_Y, PLAY_AREA_WIDTH, PLAY_AREA_HEIGHT, overlay);
        grl_draw_text ("PAUSED", cx - 50, cy, 28, white);
    }
}

/* ========================================================================== */
/* GObject Implementation                                                     */
/* ========================================================================== */

static void
shmup_demo_constructed (GObject *object)
{
    ShmupDemo *self = SHMUP_DEMO (object);

    G_OBJECT_CLASS (shmup_demo_parent_class)->constructed (object);

    /* Configure template */
    lrg_shmup_template_set_scroll_direction (LRG_SHMUP_TEMPLATE (self), LRG_SHMUP_SCROLL_UP);
    lrg_shmup_template_set_scroll_speed (LRG_SHMUP_TEMPLATE (self), SCROLL_SPEED);
    lrg_shmup_template_set_hitbox_radius (LRG_SHMUP_TEMPLATE (self), PLAYER_HITBOX);
    lrg_shmup_template_set_graze_radius (LRG_SHMUP_TEMPLATE (self), 20.0f);
    lrg_shmup_template_set_graze_points (LRG_SHMUP_TEMPLATE (self), 10);
    lrg_shmup_template_set_focus_speed_multiplier (LRG_SHMUP_TEMPLATE (self), 0.4f);
    lrg_shmup_template_set_max_lives (LRG_SHMUP_TEMPLATE (self), 9);
    lrg_shmup_template_set_max_bombs (LRG_SHMUP_TEMPLATE (self), 9);
    lrg_shmup_template_set_max_power_level (LRG_SHMUP_TEMPLATE (self), 4);
    lrg_shmup_template_set_bomb_duration (LRG_SHMUP_TEMPLATE (self), 1.0f);

    reset_game (self);
}

static void
shmup_demo_class_init (ShmupDemoClass *klass)
{
    GObjectClass         *object_class = G_OBJECT_CLASS (klass);
    LrgGameTemplateClass *template_class = LRG_GAME_TEMPLATE_CLASS (klass);

    object_class->constructed = shmup_demo_constructed;

    template_class->pre_update = shmup_demo_pre_update;
    template_class->pre_draw = shmup_demo_pre_draw;
}

static void
shmup_demo_init (ShmupDemo *self)
{
    self->high_score = 0;
}

/* ========================================================================== */
/* Public API                                                                 */
/* ========================================================================== */

static ShmupDemo *
shmup_demo_new (void)
{
    return g_object_new (SHMUP_TYPE_DEMO,
                         "title", "Shmup Demo",
                         "window-width", SCREEN_WIDTH,
                         "window-height", SCREEN_HEIGHT,
                         "target-fps", 60,
                         NULL);
}

/* ========================================================================== */
/* Main Entry Point                                                           */
/* ========================================================================== */

int
main (int   argc,
      char *argv[])
{
    g_autoptr(ShmupDemo) game = NULL;

    setlocale (LC_ALL, "");
    srand ((unsigned int)time (NULL));

    game = shmup_demo_new ();
    lrg_game_template_run (LRG_GAME_TEMPLATE (game), argc, argv);

    return 0;
}
