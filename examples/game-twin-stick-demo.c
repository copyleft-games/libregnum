/* game-twin-stick-demo.c
 *
 * Copyright 2025 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Twin-stick shooter demo using LrgTwinStickTemplate.
 * Features wave-based arena survival with multiple weapons and enemies.
 *
 * Controls:
 *   WASD        - Movement
 *   Mouse       - Aim
 *   LMB         - Fire primary weapon
 *   RMB         - Fire secondary weapon
 *   Space       - Bomb (screen clear)
 *   1-4         - Select weapon
 *   Shift       - Dash
 *   Escape      - Pause/Quit
 *
 * Features demonstrated:
 *   - LrgTwinStickTemplate usage
 *   - Wave-based enemy spawning
 *   - Multiple weapon types
 *   - Power-up system
 *   - Score and combo system
 *   - Dash mechanics
 */

#include <libregnum.h>
#include <graylib.h>
#include <locale.h>
#include <math.h>

/* ========================================================================== */
/* Type Declarations                                                          */
/* ========================================================================== */

#define TWIN_STICK_TYPE_DEMO (twin_stick_demo_get_type ())
G_DECLARE_FINAL_TYPE (TwinStickDemo, twin_stick_demo, TWIN_STICK, DEMO, LrgTwinStickTemplate)

/* ========================================================================== */
/* Constants                                                                  */
/* ========================================================================== */

#define ARENA_WIDTH           1200.0f
#define ARENA_HEIGHT          800.0f
#define ARENA_MARGIN          100.0f

#define PLAYER_RADIUS         16.0f
#define PLAYER_SPEED          300.0f
#define PLAYER_MAX_HEALTH     100.0f
#define PLAYER_INVULN_TIME    1.5f

#define BULLET_SPEED          600.0f
#define BULLET_RADIUS         4.0f
#define BULLET_LIFETIME       2.0f

#define ENEMY_SPAWN_MARGIN    50.0f
#define MAX_ENEMIES           50
#define MAX_BULLETS           200
#define MAX_POWERUPS          10

#define COMBO_DECAY_TIME      2.0f
#define COMBO_MULTIPLIER_MAX  10

#define BOMB_FLASH_DURATION   0.3f
#define STARTING_BOMBS        3

/* ========================================================================== */
/* Enumerations                                                               */
/* ========================================================================== */

typedef enum
{
    WEAPON_RAPID,       /* Fast single shots */
    WEAPON_SPREAD,      /* 3-way spread */
    WEAPON_LASER,       /* Continuous beam */
    WEAPON_ROCKET,      /* Slow but powerful */
    WEAPON_COUNT
} WeaponType;

typedef enum
{
    ENEMY_SWARMER,      /* Fast, weak, chases player */
    ENEMY_TANK,         /* Slow, tough, high damage */
    ENEMY_SHOOTER,      /* Ranged attacks */
    ENEMY_BOMBER,       /* Explodes on death */
    ENEMY_BOSS          /* Large, multiple attacks */
} EnemyType;

typedef enum
{
    POWERUP_HEALTH,
    POWERUP_BOMB,
    POWERUP_WEAPON,
    POWERUP_SPEED,
    POWERUP_SHIELD
} PowerupType;

typedef enum
{
    GAME_PLAYING,
    GAME_PAUSED,
    GAME_OVER,
    GAME_WAVE_COMPLETE
} GameState;

/* ========================================================================== */
/* Data Structures                                                            */
/* ========================================================================== */

typedef struct
{
    gfloat    x, y;
    gfloat    vx, vy;
    gfloat    radius;
    gfloat    lifetime;
    gint      damage;
    gboolean  player_owned;
    gboolean  active;
} Bullet;

typedef struct
{
    gfloat     x, y;
    gfloat     vx, vy;
    gfloat     radius;
    gfloat     health;
    gfloat     max_health;
    gint       damage;
    gint       score_value;
    gfloat     shoot_timer;
    gfloat     shoot_interval;
    EnemyType  type;
    gboolean   active;
} Enemy;

typedef struct
{
    gfloat      x, y;
    gfloat      lifetime;
    PowerupType type;
    gboolean    active;
} Powerup;

/* ========================================================================== */
/* Private Structure                                                          */
/* ========================================================================== */

struct _TwinStickDemo
{
    LrgTwinStickTemplate parent_instance;

    /* Player state */
    gfloat      player_x;
    gfloat      player_y;
    gfloat      player_health;
    gfloat      player_invuln_timer;
    gfloat      player_speed_mult;
    gboolean    player_shielded;
    gfloat      shield_timer;

    /* Weapons */
    WeaponType  current_weapon;
    gfloat      weapon_timers[WEAPON_COUNT];
    gfloat      weapon_cooldowns[WEAPON_COUNT];
    gfloat      laser_active_time;

    /* Combat */
    Bullet      bullets[MAX_BULLETS];
    Enemy       enemies[MAX_ENEMIES];
    Powerup     powerups[MAX_POWERUPS];
    gint        bombs;

    /* Wave system */
    gint        current_wave;
    gint        enemies_remaining;
    gint        enemies_to_spawn;
    gfloat      spawn_timer;
    gfloat      wave_delay_timer;

    /* Scoring */
    gint        score;
    gint        high_score;
    gint        combo;
    gfloat      combo_timer;

    /* Effects */
    gfloat      bomb_flash_timer;
    gfloat      screen_shake;

    /* Game state */
    GameState   state;
};

G_DEFINE_FINAL_TYPE (TwinStickDemo, twin_stick_demo, LRG_TYPE_TWIN_STICK_TEMPLATE)

/* ========================================================================== */
/* Forward Declarations                                                       */
/* ========================================================================== */

static void spawn_enemy          (TwinStickDemo *self, EnemyType type);
static void spawn_bullet         (TwinStickDemo *self, gfloat x, gfloat y,
                                  gfloat angle, gint damage, gboolean player_owned);
static void spawn_powerup        (TwinStickDemo *self, gfloat x, gfloat y);
static void start_wave           (TwinStickDemo *self);
static void apply_damage         (TwinStickDemo *self, gfloat damage);
static void add_score            (TwinStickDemo *self, gint points);
static void use_bomb             (TwinStickDemo *self);
static void reset_game           (TwinStickDemo *self);
static TwinStickDemo *twin_stick_demo_new (void);

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

static gfloat
angle_to (gfloat x1, gfloat y1, gfloat x2, gfloat y2)
{
    return atan2f (y2 - y1, x2 - x1);
}

static void
clamp_to_arena (gfloat *x, gfloat *y, gfloat radius)
{
    gfloat min_x = ARENA_MARGIN + radius;
    gfloat max_x = ARENA_MARGIN + ARENA_WIDTH - radius;
    gfloat min_y = ARENA_MARGIN + radius;
    gfloat max_y = ARENA_MARGIN + ARENA_HEIGHT - radius;

    if (*x < min_x) *x = min_x;
    if (*x > max_x) *x = max_x;
    if (*y < min_y) *y = min_y;
    if (*y > max_y) *y = max_y;
}

/* ========================================================================== */
/* Spawning Functions                                                         */
/* ========================================================================== */

static void
spawn_enemy (TwinStickDemo *self, EnemyType type)
{
    Enemy *enemy = NULL;
    gint   i;
    gint   edge;
    gfloat spawn_x, spawn_y;

    /* Find inactive slot */
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

    /* Spawn at random edge */
    edge = rand () % 4;
    switch (edge)
    {
    case 0: /* Top */
        spawn_x = randf (ARENA_MARGIN, ARENA_MARGIN + ARENA_WIDTH);
        spawn_y = ARENA_MARGIN - ENEMY_SPAWN_MARGIN;
        break;
    case 1: /* Bottom */
        spawn_x = randf (ARENA_MARGIN, ARENA_MARGIN + ARENA_WIDTH);
        spawn_y = ARENA_MARGIN + ARENA_HEIGHT + ENEMY_SPAWN_MARGIN;
        break;
    case 2: /* Left */
        spawn_x = ARENA_MARGIN - ENEMY_SPAWN_MARGIN;
        spawn_y = randf (ARENA_MARGIN, ARENA_MARGIN + ARENA_HEIGHT);
        break;
    default: /* Right */
        spawn_x = ARENA_MARGIN + ARENA_WIDTH + ENEMY_SPAWN_MARGIN;
        spawn_y = randf (ARENA_MARGIN, ARENA_MARGIN + ARENA_HEIGHT);
        break;
    }

    enemy->x = spawn_x;
    enemy->y = spawn_y;
    enemy->vx = 0.0f;
    enemy->vy = 0.0f;
    enemy->type = type;
    enemy->shoot_timer = 0.0f;
    enemy->active = TRUE;

    /* Type-specific stats */
    switch (type)
    {
    case ENEMY_SWARMER:
        enemy->radius = 12.0f;
        enemy->health = 20.0f;
        enemy->max_health = 20.0f;
        enemy->damage = 10;
        enemy->score_value = 100;
        enemy->shoot_interval = 0.0f; /* Doesn't shoot */
        break;
    case ENEMY_TANK:
        enemy->radius = 28.0f;
        enemy->health = 100.0f;
        enemy->max_health = 100.0f;
        enemy->damage = 25;
        enemy->score_value = 300;
        enemy->shoot_interval = 0.0f;
        break;
    case ENEMY_SHOOTER:
        enemy->radius = 16.0f;
        enemy->health = 40.0f;
        enemy->max_health = 40.0f;
        enemy->damage = 15;
        enemy->score_value = 200;
        enemy->shoot_interval = 1.5f;
        break;
    case ENEMY_BOMBER:
        enemy->radius = 14.0f;
        enemy->health = 30.0f;
        enemy->max_health = 30.0f;
        enemy->damage = 35;
        enemy->score_value = 150;
        enemy->shoot_interval = 0.0f;
        break;
    case ENEMY_BOSS:
        enemy->radius = 48.0f;
        enemy->health = 500.0f;
        enemy->max_health = 500.0f;
        enemy->damage = 30;
        enemy->score_value = 2000;
        enemy->shoot_interval = 0.8f;
        /* Spawn in center top */
        enemy->x = ARENA_MARGIN + ARENA_WIDTH / 2.0f;
        enemy->y = ARENA_MARGIN - ENEMY_SPAWN_MARGIN;
        break;
    }
}

static void
spawn_bullet (TwinStickDemo *self,
              gfloat         x,
              gfloat         y,
              gfloat         angle,
              gint           damage,
              gboolean       player_owned)
{
    Bullet *bullet = NULL;
    gint    i;

    for (i = 0; i < MAX_BULLETS; i++)
    {
        if (!self->bullets[i].active)
        {
            bullet = &self->bullets[i];
            break;
        }
    }
    if (bullet == NULL)
        return;

    bullet->x = x;
    bullet->y = y;
    bullet->vx = cosf (angle) * BULLET_SPEED;
    bullet->vy = sinf (angle) * BULLET_SPEED;
    bullet->radius = BULLET_RADIUS;
    bullet->lifetime = BULLET_LIFETIME;
    bullet->damage = damage;
    bullet->player_owned = player_owned;
    bullet->active = TRUE;
}

static void
spawn_powerup (TwinStickDemo *self, gfloat x, gfloat y)
{
    Powerup *powerup = NULL;
    gint     i;

    /* 30% chance to spawn powerup */
    if (randf (0.0f, 1.0f) > 0.3f)
        return;

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
    powerup->lifetime = 10.0f;
    powerup->active = TRUE;

    /* Random powerup type */
    i = rand () % 5;
    powerup->type = (PowerupType)i;
}

/* ========================================================================== */
/* Wave System                                                                */
/* ========================================================================== */

static void
start_wave (TwinStickDemo *self)
{
    gint base_enemies;
    gint i;

    self->current_wave++;
    self->wave_delay_timer = 0.0f;

    /* Calculate enemies for this wave */
    base_enemies = 5 + self->current_wave * 2;
    self->enemies_to_spawn = base_enemies;
    self->enemies_remaining = base_enemies;

    /* Boss every 5 waves */
    if (self->current_wave % 5 == 0)
    {
        self->enemies_to_spawn++;
        self->enemies_remaining++;
    }

    /* Clear existing entities for new wave */
    for (i = 0; i < MAX_BULLETS; i++)
        self->bullets[i].active = FALSE;

    self->state = GAME_PLAYING;
    self->spawn_timer = 0.0f;
}

static void
update_wave_spawning (TwinStickDemo *self, gfloat delta)
{
    EnemyType type;
    gfloat    spawn_interval;

    if (self->enemies_to_spawn <= 0)
        return;

    /* Faster spawning in later waves */
    spawn_interval = 2.0f - (self->current_wave * 0.1f);
    if (spawn_interval < 0.5f)
        spawn_interval = 0.5f;

    self->spawn_timer += delta;
    if (self->spawn_timer < spawn_interval)
        return;

    self->spawn_timer = 0.0f;

    /* Boss wave */
    if (self->current_wave % 5 == 0 && self->enemies_to_spawn == 1)
    {
        spawn_enemy (self, ENEMY_BOSS);
        self->enemies_to_spawn--;
        return;
    }

    /* Random enemy type based on wave */
    if (self->current_wave < 3)
    {
        type = ENEMY_SWARMER;
    }
    else if (self->current_wave < 5)
    {
        type = (rand () % 2 == 0) ? ENEMY_SWARMER : ENEMY_SHOOTER;
    }
    else
    {
        gint r = rand () % 4;
        switch (r)
        {
        case 0: type = ENEMY_SWARMER; break;
        case 1: type = ENEMY_SHOOTER; break;
        case 2: type = ENEMY_TANK; break;
        default: type = ENEMY_BOMBER; break;
        }
    }

    spawn_enemy (self, type);
    self->enemies_to_spawn--;
}

/* ========================================================================== */
/* Combat Functions                                                           */
/* ========================================================================== */

static void
apply_damage (TwinStickDemo *self, gfloat damage)
{
    if (self->player_invuln_timer > 0.0f)
        return;

    if (self->player_shielded)
    {
        self->player_shielded = FALSE;
        self->shield_timer = 0.0f;
        self->player_invuln_timer = 0.5f;
        return;
    }

    self->player_health -= damage;
    self->player_invuln_timer = PLAYER_INVULN_TIME;
    self->screen_shake = 0.3f;
    self->combo = 0;
    self->combo_timer = 0.0f;

    if (self->player_health <= 0.0f)
    {
        self->player_health = 0.0f;
        self->state = GAME_OVER;
        if (self->score > self->high_score)
            self->high_score = self->score;
    }
}

static void
add_score (TwinStickDemo *self, gint points)
{
    gint multiplied;

    self->combo++;
    if (self->combo > COMBO_MULTIPLIER_MAX)
        self->combo = COMBO_MULTIPLIER_MAX;

    self->combo_timer = COMBO_DECAY_TIME;

    multiplied = points * self->combo;
    self->score += multiplied;
}

static void
use_bomb (TwinStickDemo *self)
{
    gint i;

    if (self->bombs <= 0)
        return;

    self->bombs--;
    self->bomb_flash_timer = BOMB_FLASH_DURATION;

    /* Destroy all enemies and bullets */
    for (i = 0; i < MAX_ENEMIES; i++)
    {
        if (self->enemies[i].active)
        {
            add_score (self, self->enemies[i].score_value / 2);
            spawn_powerup (self, self->enemies[i].x, self->enemies[i].y);
            self->enemies[i].active = FALSE;
            self->enemies_remaining--;
        }
    }

    for (i = 0; i < MAX_BULLETS; i++)
    {
        if (self->bullets[i].active && !self->bullets[i].player_owned)
            self->bullets[i].active = FALSE;
    }
}

static void
reset_game (TwinStickDemo *self)
{
    gint i;

    self->player_x = ARENA_MARGIN + ARENA_WIDTH / 2.0f;
    self->player_y = ARENA_MARGIN + ARENA_HEIGHT / 2.0f;
    self->player_health = PLAYER_MAX_HEALTH;
    self->player_invuln_timer = 0.0f;
    self->player_speed_mult = 1.0f;
    self->player_shielded = FALSE;
    self->shield_timer = 0.0f;

    self->current_weapon = WEAPON_RAPID;
    for (i = 0; i < WEAPON_COUNT; i++)
        self->weapon_timers[i] = 0.0f;

    self->bombs = STARTING_BOMBS;
    self->current_wave = 0;
    self->score = 0;
    self->combo = 0;
    self->combo_timer = 0.0f;

    for (i = 0; i < MAX_BULLETS; i++)
        self->bullets[i].active = FALSE;
    for (i = 0; i < MAX_ENEMIES; i++)
        self->enemies[i].active = FALSE;
    for (i = 0; i < MAX_POWERUPS; i++)
        self->powerups[i].active = FALSE;

    self->state = GAME_PLAYING;
    start_wave (self);
}

/* ========================================================================== */
/* Update Functions                                                           */
/* ========================================================================== */

static void
update_player (TwinStickDemo *self, gfloat delta)
{
    gfloat move_x, move_y;
    gfloat aim_angle;
    gfloat speed;

    /* Get movement from template */
    lrg_twin_stick_template_get_move_direction (LRG_TWIN_STICK_TEMPLATE (self), &move_x, &move_y);
    aim_angle = lrg_twin_stick_template_get_aim_angle (LRG_TWIN_STICK_TEMPLATE (self));

    /* Apply movement */
    speed = PLAYER_SPEED * self->player_speed_mult;
    if (lrg_twin_stick_template_is_dashing (LRG_TWIN_STICK_TEMPLATE (self)))
        speed *= 2.5f;

    self->player_x += move_x * speed * delta;
    self->player_y += move_y * speed * delta;
    clamp_to_arena (&self->player_x, &self->player_y, PLAYER_RADIUS);

    /* Update invulnerability */
    if (self->player_invuln_timer > 0.0f)
        self->player_invuln_timer -= delta;

    /* Update shield */
    if (self->player_shielded)
    {
        self->shield_timer -= delta;
        if (self->shield_timer <= 0.0f)
            self->player_shielded = FALSE;
    }

    /* Update speed boost */
    if (self->player_speed_mult > 1.0f)
    {
        self->player_speed_mult -= delta * 0.1f;
        if (self->player_speed_mult < 1.0f)
            self->player_speed_mult = 1.0f;
    }

    /* Weapon firing */
    for (gint i = 0; i < WEAPON_COUNT; i++)
    {
        if (self->weapon_timers[i] > 0.0f)
            self->weapon_timers[i] -= delta;
    }

    if (grl_input_is_mouse_button_down (GRL_MOUSE_BUTTON_LEFT))
    {
        WeaponType w = self->current_weapon;
        if (self->weapon_timers[w] <= 0.0f)
        {
            gfloat spread;

            switch (w)
            {
            case WEAPON_RAPID:
                spawn_bullet (self, self->player_x, self->player_y, aim_angle, 10, TRUE);
                self->weapon_timers[w] = 0.1f;
                break;
            case WEAPON_SPREAD:
                spread = 0.25f;
                spawn_bullet (self, self->player_x, self->player_y, aim_angle, 8, TRUE);
                spawn_bullet (self, self->player_x, self->player_y, aim_angle - spread, 8, TRUE);
                spawn_bullet (self, self->player_x, self->player_y, aim_angle + spread, 8, TRUE);
                self->weapon_timers[w] = 0.3f;
                break;
            case WEAPON_LASER:
                spawn_bullet (self, self->player_x, self->player_y, aim_angle, 5, TRUE);
                self->weapon_timers[w] = 0.03f;
                break;
            case WEAPON_ROCKET:
                spawn_bullet (self, self->player_x, self->player_y, aim_angle, 50, TRUE);
                self->weapon_timers[w] = 0.8f;
                break;
            default:
                break;
            }
        }
    }

    /* Weapon selection */
    if (grl_input_is_key_pressed (GRL_KEY_ONE))
        self->current_weapon = WEAPON_RAPID;
    else if (grl_input_is_key_pressed (GRL_KEY_TWO))
        self->current_weapon = WEAPON_SPREAD;
    else if (grl_input_is_key_pressed (GRL_KEY_THREE))
        self->current_weapon = WEAPON_LASER;
    else if (grl_input_is_key_pressed (GRL_KEY_FOUR))
        self->current_weapon = WEAPON_ROCKET;

    /* Bomb */
    if (grl_input_is_key_pressed (GRL_KEY_SPACE))
        use_bomb (self);

    /* Dash input */
    if (grl_input_is_key_pressed (GRL_KEY_LEFT_SHIFT))
        lrg_twin_stick_template_dash (LRG_TWIN_STICK_TEMPLATE (self));
}

static void
update_enemies (TwinStickDemo *self, gfloat delta)
{
    gint   i;
    gfloat angle;
    gfloat speed;
    gfloat dist;

    for (i = 0; i < MAX_ENEMIES; i++)
    {
        Enemy *e = &self->enemies[i];
        if (!e->active)
            continue;

        angle = angle_to (e->x, e->y, self->player_x, self->player_y);
        dist = distance (e->x, e->y, self->player_x, self->player_y);

        switch (e->type)
        {
        case ENEMY_SWARMER:
            speed = 150.0f;
            e->vx = cosf (angle) * speed;
            e->vy = sinf (angle) * speed;
            break;
        case ENEMY_TANK:
            speed = 60.0f;
            e->vx = cosf (angle) * speed;
            e->vy = sinf (angle) * speed;
            break;
        case ENEMY_SHOOTER:
            /* Keep distance */
            if (dist < 200.0f)
            {
                e->vx = -cosf (angle) * 80.0f;
                e->vy = -sinf (angle) * 80.0f;
            }
            else if (dist > 300.0f)
            {
                e->vx = cosf (angle) * 80.0f;
                e->vy = sinf (angle) * 80.0f;
            }
            else
            {
                e->vx = 0.0f;
                e->vy = 0.0f;
            }
            /* Shoot */
            e->shoot_timer += delta;
            if (e->shoot_timer >= e->shoot_interval)
            {
                spawn_bullet (self, e->x, e->y, angle, e->damage, FALSE);
                e->shoot_timer = 0.0f;
            }
            break;
        case ENEMY_BOMBER:
            speed = 120.0f;
            e->vx = cosf (angle) * speed;
            e->vy = sinf (angle) * speed;
            break;
        case ENEMY_BOSS:
            /* Move toward center top area */
            if (e->y < ARENA_MARGIN + 150.0f)
            {
                e->vy = 50.0f;
            }
            else
            {
                e->vy = 0.0f;
                /* Strafe */
                e->vx = sinf (self->spawn_timer * 2.0f) * 100.0f;
            }
            /* Multi-shot */
            e->shoot_timer += delta;
            if (e->shoot_timer >= e->shoot_interval)
            {
                gint j;
                for (j = 0; j < 5; j++)
                {
                    gfloat spread_angle = angle + (j - 2) * 0.3f;
                    spawn_bullet (self, e->x, e->y, spread_angle, e->damage, FALSE);
                }
                e->shoot_timer = 0.0f;
            }
            break;
        }

        e->x += e->vx * delta;
        e->y += e->vy * delta;

        /* Collision with player */
        if (distance (e->x, e->y, self->player_x, self->player_y) < e->radius + PLAYER_RADIUS)
        {
            apply_damage (self, (gfloat)e->damage);
        }
    }
}

static void
update_bullets (TwinStickDemo *self, gfloat delta)
{
    gint i, j;

    for (i = 0; i < MAX_BULLETS; i++)
    {
        Bullet *b = &self->bullets[i];
        if (!b->active)
            continue;

        b->x += b->vx * delta;
        b->y += b->vy * delta;
        b->lifetime -= delta;

        /* Check bounds and lifetime */
        if (b->lifetime <= 0.0f ||
            b->x < ARENA_MARGIN - 50.0f || b->x > ARENA_MARGIN + ARENA_WIDTH + 50.0f ||
            b->y < ARENA_MARGIN - 50.0f || b->y > ARENA_MARGIN + ARENA_HEIGHT + 50.0f)
        {
            b->active = FALSE;
            continue;
        }

        if (b->player_owned)
        {
            /* Check enemy collisions */
            for (j = 0; j < MAX_ENEMIES; j++)
            {
                Enemy *e = &self->enemies[j];
                if (!e->active)
                    continue;

                if (distance (b->x, b->y, e->x, e->y) < b->radius + e->radius)
                {
                    e->health -= b->damage;
                    b->active = FALSE;

                    if (e->health <= 0.0f)
                    {
                        add_score (self, e->score_value);
                        spawn_powerup (self, e->x, e->y);
                        e->active = FALSE;
                        self->enemies_remaining--;

                        /* Bomber explosion */
                        if (e->type == ENEMY_BOMBER)
                        {
                            if (distance (e->x, e->y, self->player_x, self->player_y) < 80.0f)
                                apply_damage (self, 20.0f);
                        }
                    }
                    break;
                }
            }
        }
        else
        {
            /* Enemy bullet hits player */
            if (distance (b->x, b->y, self->player_x, self->player_y) < b->radius + PLAYER_RADIUS)
            {
                apply_damage (self, (gfloat)b->damage);
                b->active = FALSE;
            }
        }
    }
}

static void
update_powerups (TwinStickDemo *self, gfloat delta)
{
    gint i;

    for (i = 0; i < MAX_POWERUPS; i++)
    {
        Powerup *p = &self->powerups[i];
        if (!p->active)
            continue;

        p->lifetime -= delta;
        if (p->lifetime <= 0.0f)
        {
            p->active = FALSE;
            continue;
        }

        /* Collect */
        if (distance (p->x, p->y, self->player_x, self->player_y) < 30.0f)
        {
            switch (p->type)
            {
            case POWERUP_HEALTH:
                self->player_health += 25.0f;
                if (self->player_health > PLAYER_MAX_HEALTH)
                    self->player_health = PLAYER_MAX_HEALTH;
                break;
            case POWERUP_BOMB:
                self->bombs++;
                break;
            case POWERUP_WEAPON:
                self->current_weapon = (self->current_weapon + 1) % WEAPON_COUNT;
                break;
            case POWERUP_SPEED:
                self->player_speed_mult = 1.5f;
                break;
            case POWERUP_SHIELD:
                self->player_shielded = TRUE;
                self->shield_timer = 10.0f;
                break;
            }
            p->active = FALSE;
        }
    }
}

static void
update_combo (TwinStickDemo *self, gfloat delta)
{
    if (self->combo_timer > 0.0f)
    {
        self->combo_timer -= delta;
        if (self->combo_timer <= 0.0f)
        {
            self->combo = 0;
        }
    }
}

static void
update_effects (TwinStickDemo *self, gfloat delta)
{
    if (self->bomb_flash_timer > 0.0f)
        self->bomb_flash_timer -= delta;

    if (self->screen_shake > 0.0f)
        self->screen_shake -= delta;
}

/* ========================================================================== */
/* Rendering Functions                                                        */
/* ========================================================================== */

static void
render_arena (TwinStickDemo *self)
{
    g_autoptr(GrlColor) bg_color = grl_color_new (20, 20, 30, 255);
    g_autoptr(GrlColor) border_color = grl_color_new (100, 100, 120, 255);
    g_autoptr(GrlColor) grid_color = grl_color_new (40, 40, 50, 255);
    g_autoptr(GrlRectangle) arena_rect = NULL;
    gfloat              grid_size = 50.0f;
    gfloat              x, y;

    grl_draw_rectangle (ARENA_MARGIN, ARENA_MARGIN, ARENA_WIDTH, ARENA_HEIGHT, bg_color);

    /* Grid */
    for (x = ARENA_MARGIN; x <= ARENA_MARGIN + ARENA_WIDTH; x += grid_size)
        grl_draw_line (x, ARENA_MARGIN, x, ARENA_MARGIN + ARENA_HEIGHT, grid_color);
    for (y = ARENA_MARGIN; y <= ARENA_MARGIN + ARENA_HEIGHT; y += grid_size)
        grl_draw_line (ARENA_MARGIN, y, ARENA_MARGIN + ARENA_WIDTH, y, grid_color);

    /* Border */
    arena_rect = grl_rectangle_new (ARENA_MARGIN, ARENA_MARGIN, ARENA_WIDTH, ARENA_HEIGHT);
    grl_draw_rectangle_lines_ex (arena_rect, 3.0f, border_color);
}

static void
render_player (TwinStickDemo *self)
{
    g_autoptr(GrlColor) player_color = NULL;
    g_autoptr(GrlColor) shield_color = grl_color_new (100, 200, 255, 100);
    g_autoptr(GrlColor) aim_color = NULL;
    g_autoptr(GrlVector2) start_pos = NULL;
    g_autoptr(GrlVector2) end_pos = NULL;
    gfloat              aim_angle;
    gfloat              aim_x, aim_y;

    /* Flicker when invulnerable */
    if (self->player_invuln_timer > 0.0f)
    {
        if ((gint)(self->player_invuln_timer * 10.0f) % 2 == 0)
            return;
    }

    player_color = grl_color_new (100, 200, 100, 255);
    grl_draw_circle (self->player_x, self->player_y, PLAYER_RADIUS, player_color);

    /* Aim indicator */
    aim_angle = lrg_twin_stick_template_get_aim_angle (LRG_TWIN_STICK_TEMPLATE (self));
    aim_x = self->player_x + cosf (aim_angle) * 30.0f;
    aim_y = self->player_y + sinf (aim_angle) * 30.0f;

    aim_color = grl_color_new (255, 255, 100, 255);
    start_pos = grl_vector2_new (self->player_x, self->player_y);
    end_pos = grl_vector2_new (aim_x, aim_y);
    grl_draw_line_ex (start_pos, end_pos, 3.0f, aim_color);

    /* Shield */
    if (self->player_shielded)
    {
        grl_draw_circle_lines (self->player_x, self->player_y, PLAYER_RADIUS + 8.0f, shield_color);
    }
}

static void
render_enemies (TwinStickDemo *self)
{
    gint i;

    for (i = 0; i < MAX_ENEMIES; i++)
    {
        Enemy             *e = &self->enemies[i];
        g_autoptr(GrlColor) color = NULL;

        if (!e->active)
            continue;

        switch (e->type)
        {
        case ENEMY_SWARMER:
            color = grl_color_new (255, 100, 100, 255);
            break;
        case ENEMY_TANK:
            color = grl_color_new (150, 80, 80, 255);
            break;
        case ENEMY_SHOOTER:
            color = grl_color_new (255, 150, 50, 255);
            break;
        case ENEMY_BOMBER:
            color = grl_color_new (255, 200, 50, 255);
            break;
        case ENEMY_BOSS:
            color = grl_color_new (200, 50, 200, 255);
            break;
        }

        grl_draw_circle (e->x, e->y, e->radius, color);

        /* Health bar for tough enemies */
        if (e->max_health >= 50.0f)
        {
            gfloat bar_width = e->radius * 2.0f;
            gfloat bar_height = 4.0f;
            gfloat health_pct = e->health / e->max_health;

            g_autoptr(GrlColor) bar_bg = grl_color_new (50, 50, 50, 200);
            g_autoptr(GrlColor) bar_fg = grl_color_new (255, 50, 50, 255);

            grl_draw_rectangle (e->x - bar_width / 2.0f, e->y - e->radius - 10.0f,
                                bar_width, bar_height, bar_bg);
            grl_draw_rectangle (e->x - bar_width / 2.0f, e->y - e->radius - 10.0f,
                                bar_width * health_pct, bar_height, bar_fg);
        }
    }
}

static void
render_bullets (TwinStickDemo *self)
{
    gint i;

    for (i = 0; i < MAX_BULLETS; i++)
    {
        Bullet            *b = &self->bullets[i];
        g_autoptr(GrlColor) color = NULL;

        if (!b->active)
            continue;

        if (b->player_owned)
            color = grl_color_new (100, 255, 100, 255);
        else
            color = grl_color_new (255, 100, 100, 255);

        grl_draw_circle (b->x, b->y, b->radius, color);
    }
}

static void
render_powerups (TwinStickDemo *self)
{
    gint i;

    for (i = 0; i < MAX_POWERUPS; i++)
    {
        Powerup           *p = &self->powerups[i];
        g_autoptr(GrlColor) color = NULL;
        gfloat              pulse;

        if (!p->active)
            continue;

        pulse = 1.0f + sinf (p->lifetime * 5.0f) * 0.2f;

        switch (p->type)
        {
        case POWERUP_HEALTH:
            color = grl_color_new (100, 255, 100, 255);
            break;
        case POWERUP_BOMB:
            color = grl_color_new (255, 200, 50, 255);
            break;
        case POWERUP_WEAPON:
            color = grl_color_new (100, 100, 255, 255);
            break;
        case POWERUP_SPEED:
            color = grl_color_new (255, 255, 100, 255);
            break;
        case POWERUP_SHIELD:
            color = grl_color_new (100, 200, 255, 255);
            break;
        }

        grl_draw_rectangle (p->x - 8.0f * pulse, p->y - 8.0f * pulse,
                            16.0f * pulse, 16.0f * pulse, color);
    }
}

static void
render_hud (TwinStickDemo *self)
{
    static const gchar *weapon_names[] = {"Rapid", "Spread", "Laser", "Rocket"};
    g_autoptr(GrlColor) white = grl_color_new (255, 255, 255, 255);
    g_autoptr(GrlColor) health_bg = grl_color_new (50, 50, 50, 200);
    g_autoptr(GrlColor) health_fg = grl_color_new (100, 200, 100, 255);
    g_autoptr(GrlColor) combo_color = grl_color_new (255, 200, 50, 255);
    gfloat              health_pct;
    gchar               text[64];

    /* Health bar */
    health_pct = self->player_health / PLAYER_MAX_HEALTH;
    grl_draw_rectangle (20, 20, 200, 20, health_bg);
    grl_draw_rectangle (20, 20, 200 * health_pct, 20, health_fg);

    /* Score */
    g_snprintf (text, sizeof (text), "Score: %d", self->score);
    grl_draw_text (text, 20, 50, 20, white);

    /* High score */
    g_snprintf (text, sizeof (text), "High: %d", self->high_score);
    grl_draw_text (text, 20, 75, 16, white);

    /* Wave */
    g_snprintf (text, sizeof (text), "Wave %d", self->current_wave);
    grl_draw_text (text, ARENA_MARGIN + ARENA_WIDTH / 2 - 30, 50, 24, white);

    /* Combo */
    if (self->combo > 1)
    {
        g_snprintf (text, sizeof (text), "x%d", self->combo);
        grl_draw_text (text, ARENA_MARGIN + ARENA_WIDTH - 80, 50, 28, combo_color);
    }

    /* Bombs */
    g_snprintf (text, sizeof (text), "Bombs: %d", self->bombs);
    grl_draw_text (text, 20, ARENA_MARGIN + ARENA_HEIGHT + 30, 18, white);

    /* Weapon */
    g_snprintf (text, sizeof (text), "Weapon: %s", weapon_names[self->current_weapon]);
    grl_draw_text (text, 200, ARENA_MARGIN + ARENA_HEIGHT + 30, 18, white);

    /* Controls hint */
    grl_draw_text ("WASD: Move | Mouse: Aim | LMB: Fire | Space: Bomb | 1-4: Weapons | Shift: Dash",
                   20, ARENA_MARGIN + ARENA_HEIGHT + 55, 14, white);
}

static void
render_game_over (TwinStickDemo *self)
{
    g_autoptr(GrlColor) overlay = grl_color_new (0, 0, 0, 180);
    g_autoptr(GrlColor) white = grl_color_new (255, 255, 255, 255);
    g_autoptr(GrlColor) yellow = grl_color_new (255, 200, 50, 255);
    gchar               text[64];
    gfloat              center_x = ARENA_MARGIN + ARENA_WIDTH / 2.0f;
    gfloat              center_y = ARENA_MARGIN + ARENA_HEIGHT / 2.0f;

    grl_draw_rectangle (ARENA_MARGIN, ARENA_MARGIN, ARENA_WIDTH, ARENA_HEIGHT, overlay);

    grl_draw_text ("GAME OVER", center_x - 80, center_y - 50, 36, white);

    g_snprintf (text, sizeof (text), "Final Score: %d", self->score);
    grl_draw_text (text, center_x - 70, center_y, 24, yellow);

    g_snprintf (text, sizeof (text), "Wave Reached: %d", self->current_wave);
    grl_draw_text (text, center_x - 70, center_y + 35, 20, white);

    grl_draw_text ("Press ENTER to restart", center_x - 90, center_y + 80, 18, white);
}

static void
render_wave_complete (TwinStickDemo *self)
{
    g_autoptr(GrlColor) white = grl_color_new (255, 255, 255, 255);
    gchar               text[64];
    gfloat              center_x = ARENA_MARGIN + ARENA_WIDTH / 2.0f;
    gfloat              center_y = ARENA_MARGIN + ARENA_HEIGHT / 2.0f;

    g_snprintf (text, sizeof (text), "Wave %d Complete!", self->current_wave);
    grl_draw_text (text, center_x - 100, center_y, 32, white);
}

/* ========================================================================== */
/* Virtual Method Overrides                                                   */
/* ========================================================================== */

static void
twin_stick_demo_pre_update (LrgGameTemplate *template, gdouble delta)
{
    TwinStickDemo *self = TWIN_STICK_DEMO (template);

    /* Chain up */
    LRG_GAME_TEMPLATE_CLASS (twin_stick_demo_parent_class)->pre_update (template, delta);

    /* Handle pause */
    if (grl_input_is_key_pressed (GRL_KEY_ESCAPE))
    {
        if (self->state == GAME_PLAYING)
            self->state = GAME_PAUSED;
        else if (self->state == GAME_PAUSED)
            self->state = GAME_PLAYING;
    }

    /* Restart */
    if (self->state == GAME_OVER && grl_input_is_key_pressed (GRL_KEY_ENTER))
    {
        reset_game (self);
        return;
    }

    if (self->state != GAME_PLAYING)
        return;

    /* Check wave completion */
    if (self->enemies_remaining <= 0 && self->enemies_to_spawn <= 0)
    {
        self->state = GAME_WAVE_COMPLETE;
        self->wave_delay_timer = 2.0f;
    }

    /* Wave delay */
    if (self->state == GAME_WAVE_COMPLETE)
    {
        self->wave_delay_timer -= delta;
        if (self->wave_delay_timer <= 0.0f)
            start_wave (self);
        return;
    }

    update_player (self, (gfloat)delta);
    update_enemies (self, (gfloat)delta);
    update_bullets (self, (gfloat)delta);
    update_powerups (self, (gfloat)delta);
    update_wave_spawning (self, (gfloat)delta);
    update_combo (self, (gfloat)delta);
    update_effects (self, (gfloat)delta);
}

static void
twin_stick_demo_pre_draw (LrgGameTemplate *template)
{
    TwinStickDemo     *self = TWIN_STICK_DEMO (template);
    g_autoptr(GrlColor) clear_color = grl_color_new (10, 10, 15, 255);

    grl_draw_clear_background (clear_color);

    /* Screen shake */
    if (self->screen_shake > 0.0f)
    {
        gfloat shake_x = randf (-5.0f, 5.0f) * self->screen_shake * 10.0f;
        gfloat shake_y = randf (-5.0f, 5.0f) * self->screen_shake * 10.0f;
        /* Note: In a full implementation, apply transform here */
        (void)shake_x;
        (void)shake_y;
    }

    /* Bomb flash */
    if (self->bomb_flash_timer > 0.0f)
    {
        g_autoptr(GrlColor) flash = grl_color_new (255, 255, 255,
                                                    (guint8)(self->bomb_flash_timer / BOMB_FLASH_DURATION * 200));
        grl_draw_rectangle (0, 0, 1400, 1000, flash);
    }

    render_arena (self);
    render_powerups (self);
    render_bullets (self);
    render_enemies (self);
    render_player (self);
    render_hud (self);

    if (self->state == GAME_OVER)
        render_game_over (self);
    else if (self->state == GAME_WAVE_COMPLETE)
        render_wave_complete (self);
    else if (self->state == GAME_PAUSED)
    {
        g_autoptr(GrlColor) overlay = grl_color_new (0, 0, 0, 150);
        g_autoptr(GrlColor) white = grl_color_new (255, 255, 255, 255);

        grl_draw_rectangle (ARENA_MARGIN, ARENA_MARGIN, ARENA_WIDTH, ARENA_HEIGHT, overlay);
        grl_draw_text ("PAUSED", ARENA_MARGIN + ARENA_WIDTH / 2 - 50,
                       ARENA_MARGIN + ARENA_HEIGHT / 2, 32, white);
    }
}

/* ========================================================================== */
/* GObject Implementation                                                     */
/* ========================================================================== */

static void
twin_stick_demo_constructed (GObject *object)
{
    TwinStickDemo *self = TWIN_STICK_DEMO (object);

    G_OBJECT_CLASS (twin_stick_demo_parent_class)->constructed (object);

    /* Configure template */
    lrg_twin_stick_template_set_aim_mode (LRG_TWIN_STICK_TEMPLATE (self), LRG_TWIN_STICK_AIM_MOUSE);
    lrg_twin_stick_template_set_dash_speed (LRG_TWIN_STICK_TEMPLATE (self), 600.0f);
    lrg_twin_stick_template_set_dash_duration (LRG_TWIN_STICK_TEMPLATE (self), 0.2f);
    lrg_twin_stick_template_set_dash_cooldown (LRG_TWIN_STICK_TEMPLATE (self), 1.0f);

    /* Initialize weapon cooldowns */
    self->weapon_cooldowns[WEAPON_RAPID] = 0.1f;
    self->weapon_cooldowns[WEAPON_SPREAD] = 0.3f;
    self->weapon_cooldowns[WEAPON_LASER] = 0.03f;
    self->weapon_cooldowns[WEAPON_ROCKET] = 0.8f;

    reset_game (self);
}

static void
twin_stick_demo_class_init (TwinStickDemoClass *klass)
{
    GObjectClass        *object_class = G_OBJECT_CLASS (klass);
    LrgGameTemplateClass *template_class = LRG_GAME_TEMPLATE_CLASS (klass);

    object_class->constructed = twin_stick_demo_constructed;

    template_class->pre_update = twin_stick_demo_pre_update;
    template_class->pre_draw = twin_stick_demo_pre_draw;
}

static void
twin_stick_demo_init (TwinStickDemo *self)
{
    self->high_score = 0;
}

/* ========================================================================== */
/* Public API                                                                 */
/* ========================================================================== */

static TwinStickDemo *
twin_stick_demo_new (void)
{
    return g_object_new (TWIN_STICK_TYPE_DEMO,
                         "title", "Twin-Stick Shooter Demo",
                         "window-width", 1400,
                         "window-height", 1000,
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
    g_autoptr(TwinStickDemo) game = NULL;

    setlocale (LC_ALL, "");
    srand ((unsigned int)time (NULL));

    game = twin_stick_demo_new ();
    lrg_game_template_run (LRG_GAME_TEMPLATE (game), argc, argv);

    return 0;
}
