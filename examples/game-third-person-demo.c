/* game-third-person-demo.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * A third-person action game demo demonstrating LrgThirdPersonTemplate.
 *
 * Features demonstrated:
 * - Subclassing LrgThirdPersonTemplate for custom third-person game logic
 * - Orbiting camera with collision avoidance
 * - Over-the-shoulder aiming with shoulder swap
 * - Character movement with sprint and dodge
 * - Jump and gravity physics
 * - Health and stamina system
 * - Basic melee combat with combo system
 * - Simple enemy AI with patrol/chase behavior
 * - Collectible items (health pickups, coins)
 * - Lock-on targeting system
 *
 * Controls:
 *   WASD        - Move character
 *   Mouse       - Camera orbit
 *   Space       - Jump
 *   Shift       - Sprint (hold)
 *   Ctrl/RMB    - Aim mode
 *   Tab         - Swap shoulder
 *   LMB         - Attack (combo chain)
 *   E           - Dodge/Roll
 *   Q           - Lock-on toggle
 *   F           - Interact
 *   ESC         - Exit
 */

/* =============================================================================
 * INCLUDES
 * ========================================================================== */

#include <libregnum.h>
#include <graylib.h>
#include <math.h>

/* =============================================================================
 * CONSTANTS
 * ========================================================================== */

#define WINDOW_WIDTH        1280
#define WINDOW_HEIGHT       720
#define ARENA_SIZE          50.0f
#define PLATFORM_COUNT      8
#define ENEMY_COUNT         5
#define PICKUP_COUNT        10
#define COIN_VALUE          10
#define HEALTH_PICKUP_VALUE 25.0f
#define ATTACK_RANGE        3.0f
#define ATTACK_DAMAGE       20.0f
#define ATTACK_COOLDOWN     0.4f
#define COMBO_WINDOW        0.8f
#define MAX_COMBO           3
#define ENEMY_HEALTH        60.0f
#define ENEMY_SPEED         2.5f
#define ENEMY_CHASE_RANGE   12.0f
#define ENEMY_ATTACK_RANGE  2.5f
#define ENEMY_ATTACK_DAMAGE 10.0f
#define ENEMY_RESPAWN_TIME  5.0f

/* =============================================================================
 * COLORS
 * ========================================================================== */

static GrlColor *color_sky = NULL;
static GrlColor *color_ground = NULL;
static GrlColor *color_platform = NULL;
static GrlColor *color_player = NULL;
static GrlColor *color_player_aim = NULL;
static GrlColor *color_enemy = NULL;
static GrlColor *color_enemy_alerted = NULL;
static GrlColor *color_coin = NULL;
static GrlColor *color_health = NULL;
static GrlColor *color_health_bar = NULL;
static GrlColor *color_health_bar_bg = NULL;
static GrlColor *color_stamina_bar = NULL;
static GrlColor *color_stamina_bar_bg = NULL;
static GrlColor *color_crosshair = NULL;
static GrlColor *color_lock_on = NULL;
static GrlColor *color_hud = NULL;
static GrlColor *color_wall = NULL;
static GrlColor *color_shadow = NULL;
static GrlColor *color_combo = NULL;

/* =============================================================================
 * DATA STRUCTURES
 * ========================================================================== */

typedef struct
{
    gfloat x, y, z;
    gfloat width, height, depth;
} Platform;

typedef enum
{
    ENEMY_STATE_IDLE,
    ENEMY_STATE_PATROL,
    ENEMY_STATE_CHASE,
    ENEMY_STATE_ATTACK,
    ENEMY_STATE_HURT,
    ENEMY_STATE_DEAD
} EnemyState;

typedef struct
{
    gfloat x, y, z;
    gfloat health;
    gfloat rotation;
    EnemyState state;
    gfloat state_timer;
    gfloat attack_cooldown;
    gfloat patrol_target_x;
    gfloat patrol_target_z;
    gfloat respawn_timer;
} Enemy;

typedef enum
{
    PICKUP_TYPE_COIN,
    PICKUP_TYPE_HEALTH
} PickupType;

typedef struct
{
    gfloat x, y, z;
    PickupType type;
    gboolean active;
    gfloat bob_offset;
    gfloat spin_angle;
} Pickup;

/* Static game data */
static Platform platforms[PLATFORM_COUNT];
static Enemy enemies[ENEMY_COUNT];
static Pickup pickups[PICKUP_COUNT];

/* =============================================================================
 * CUSTOM THIRD PERSON TYPE
 * ========================================================================== */

#define DEMO_TYPE_THIRD_PERSON (demo_third_person_get_type ())
G_DECLARE_FINAL_TYPE (DemoThirdPerson, demo_third_person, DEMO, THIRD_PERSON, LrgThirdPersonTemplate)

struct _DemoThirdPerson
{
    LrgThirdPersonTemplate parent_instance;

    /* Combat */
    gint combo_count;
    gfloat combo_timer;
    gfloat attack_cooldown;
    gboolean is_attacking;
    gfloat attack_anim_timer;

    /* Stats */
    gint score;
    gint coins_collected;
    gint enemies_defeated;
    gfloat play_time;

    /* Lock-on */
    gint lock_on_enemy_index;

    /* Visual feedback */
    gfloat damage_flash;
    gfloat dodge_cooldown;
};

G_DEFINE_FINAL_TYPE (DemoThirdPerson, demo_third_person, LRG_TYPE_THIRD_PERSON_TEMPLATE)

/* =============================================================================
 * UTILITY FUNCTIONS
 * ========================================================================== */

static gfloat
distance_3d (gfloat x1, gfloat y1, gfloat z1,
             gfloat x2, gfloat y2, gfloat z2)
{
    gfloat dx = x2 - x1;
    gfloat dy = y2 - y1;
    gfloat dz = z2 - z1;
    return sqrtf (dx * dx + dy * dy + dz * dz);
}

static gfloat
distance_2d (gfloat x1, gfloat z1, gfloat x2, gfloat z2)
{
    gfloat dx = x2 - x1;
    gfloat dz = z2 - z1;
    return sqrtf (dx * dx + dz * dz);
}

static gfloat
angle_to_target (gfloat x1, gfloat z1, gfloat x2, gfloat z2)
{
    return atan2f (x2 - x1, z2 - z1);
}

static gfloat
lerp_angle (gfloat a, gfloat b, gfloat t)
{
    gfloat diff = fmodf (b - a + G_PI * 3, G_PI * 2) - G_PI;
    return a + diff * t;
}

/* =============================================================================
 * WORLD INITIALIZATION
 * ========================================================================== */

static void
init_world (void)
{
    gint i;

    /* Initialize platforms - scattered around arena */
    platforms[0] = (Platform){ -15.0f, 1.0f, -15.0f, 8.0f, 2.0f, 8.0f };
    platforms[1] = (Platform){  15.0f, 1.5f, -12.0f, 6.0f, 3.0f, 6.0f };
    platforms[2] = (Platform){ -10.0f, 2.0f,  15.0f, 7.0f, 4.0f, 7.0f };
    platforms[3] = (Platform){  18.0f, 2.5f,  10.0f, 5.0f, 5.0f, 5.0f };
    platforms[4] = (Platform){   0.0f, 3.0f,   0.0f, 10.0f, 6.0f, 10.0f };
    platforms[5] = (Platform){ -20.0f, 1.0f,   5.0f, 4.0f, 2.0f, 8.0f };
    platforms[6] = (Platform){  10.0f, 1.5f,  -5.0f, 5.0f, 3.0f, 5.0f };
    platforms[7] = (Platform){  -5.0f, 2.0f,  20.0f, 6.0f, 4.0f, 4.0f };

    /* Initialize enemies */
    for (i = 0; i < ENEMY_COUNT; i++)
    {
        gfloat angle = (gfloat)i / ENEMY_COUNT * G_PI * 2.0f;
        enemies[i].x = cosf (angle) * 15.0f;
        enemies[i].y = 0.0f;
        enemies[i].z = sinf (angle) * 15.0f;
        enemies[i].health = ENEMY_HEALTH;
        enemies[i].rotation = g_random_double () * G_PI * 2.0f;
        enemies[i].state = ENEMY_STATE_PATROL;
        enemies[i].state_timer = 0.0f;
        enemies[i].attack_cooldown = 0.0f;
        enemies[i].patrol_target_x = enemies[i].x + (g_random_double () - 0.5) * 10.0f;
        enemies[i].patrol_target_z = enemies[i].z + (g_random_double () - 0.5) * 10.0f;
        enemies[i].respawn_timer = 0.0f;
    }

    /* Initialize pickups */
    for (i = 0; i < PICKUP_COUNT; i++)
    {
        pickups[i].x = (g_random_double () - 0.5) * ARENA_SIZE * 0.8f;
        pickups[i].z = (g_random_double () - 0.5) * ARENA_SIZE * 0.8f;
        pickups[i].y = 1.0f;
        pickups[i].type = (i < 7) ? PICKUP_TYPE_COIN : PICKUP_TYPE_HEALTH;
        pickups[i].active = TRUE;
        pickups[i].bob_offset = g_random_double () * G_PI * 2.0f;
        pickups[i].spin_angle = 0.0f;
    }
}

/* =============================================================================
 * ENEMY AI
 * ========================================================================== */

static void
update_enemy (Enemy  *enemy,
              gfloat  player_x,
              gfloat  player_y,
              gfloat  player_z,
              gfloat  delta,
              DemoThirdPerson *game)
{
    gfloat dist_to_player;
    gfloat angle_to_player;
    gfloat move_speed;

    if (enemy->state == ENEMY_STATE_DEAD)
    {
        enemy->respawn_timer -= delta;
        if (enemy->respawn_timer <= 0.0f)
        {
            /* Respawn */
            gfloat angle = g_random_double () * G_PI * 2.0f;
            enemy->x = cosf (angle) * 20.0f;
            enemy->z = sinf (angle) * 20.0f;
            enemy->y = 0.0f;
            enemy->health = ENEMY_HEALTH;
            enemy->state = ENEMY_STATE_PATROL;
            enemy->state_timer = 0.0f;
        }
        return;
    }

    if (enemy->state == ENEMY_STATE_HURT)
    {
        enemy->state_timer -= delta;
        if (enemy->state_timer <= 0.0f)
        {
            enemy->state = ENEMY_STATE_CHASE;
        }
        return;
    }

    dist_to_player = distance_2d (enemy->x, enemy->z, player_x, player_z);
    angle_to_player = angle_to_target (enemy->x, enemy->z, player_x, player_z);

    /* Update attack cooldown */
    if (enemy->attack_cooldown > 0.0f)
        enemy->attack_cooldown -= delta;

    /* State transitions */
    switch (enemy->state)
    {
    case ENEMY_STATE_IDLE:
        enemy->state_timer -= delta;
        if (enemy->state_timer <= 0.0f)
        {
            enemy->state = ENEMY_STATE_PATROL;
            enemy->patrol_target_x = enemy->x + (g_random_double () - 0.5) * 10.0f;
            enemy->patrol_target_z = enemy->z + (g_random_double () - 0.5) * 10.0f;
        }
        if (dist_to_player < ENEMY_CHASE_RANGE)
        {
            enemy->state = ENEMY_STATE_CHASE;
        }
        break;

    case ENEMY_STATE_PATROL:
        /* Move towards patrol target */
        {
            gfloat patrol_dist = distance_2d (enemy->x, enemy->z,
                                              enemy->patrol_target_x,
                                              enemy->patrol_target_z);
            if (patrol_dist < 1.0f)
            {
                enemy->state = ENEMY_STATE_IDLE;
                enemy->state_timer = 1.0f + g_random_double () * 2.0f;
            }
            else
            {
                gfloat patrol_angle = angle_to_target (enemy->x, enemy->z,
                                                       enemy->patrol_target_x,
                                                       enemy->patrol_target_z);
                enemy->rotation = lerp_angle (enemy->rotation, patrol_angle, delta * 5.0f);
                enemy->x += sinf (enemy->rotation) * ENEMY_SPEED * 0.5f * delta;
                enemy->z += cosf (enemy->rotation) * ENEMY_SPEED * 0.5f * delta;
            }
        }
        if (dist_to_player < ENEMY_CHASE_RANGE)
        {
            enemy->state = ENEMY_STATE_CHASE;
        }
        break;

    case ENEMY_STATE_CHASE:
        if (dist_to_player >= ENEMY_CHASE_RANGE * 1.5f)
        {
            enemy->state = ENEMY_STATE_PATROL;
            enemy->patrol_target_x = enemy->x + (g_random_double () - 0.5) * 10.0f;
            enemy->patrol_target_z = enemy->z + (g_random_double () - 0.5) * 10.0f;
        }
        else if (dist_to_player < ENEMY_ATTACK_RANGE && enemy->attack_cooldown <= 0.0f)
        {
            enemy->state = ENEMY_STATE_ATTACK;
            enemy->state_timer = 0.3f;
        }
        else
        {
            enemy->rotation = lerp_angle (enemy->rotation, angle_to_player, delta * 8.0f);
            move_speed = ENEMY_SPEED;
            enemy->x += sinf (enemy->rotation) * move_speed * delta;
            enemy->z += cosf (enemy->rotation) * move_speed * delta;
        }
        break;

    case ENEMY_STATE_ATTACK:
        enemy->rotation = lerp_angle (enemy->rotation, angle_to_player, delta * 10.0f);
        enemy->state_timer -= delta;
        if (enemy->state_timer <= 0.0f)
        {
            /* Deal damage to player if still in range */
            if (dist_to_player < ENEMY_ATTACK_RANGE + 0.5f)
            {
                lrg_third_person_template_apply_damage (
                    LRG_THIRD_PERSON_TEMPLATE (game),
                    ENEMY_ATTACK_DAMAGE,
                    enemy->x, enemy->y + 1.0f, enemy->z);
                game->damage_flash = 0.3f;
                lrg_game_template_shake (LRG_GAME_TEMPLATE (game), 0.3f);
            }
            enemy->attack_cooldown = 1.5f;
            enemy->state = ENEMY_STATE_CHASE;
        }
        break;

    default:
        break;
    }

    /* Keep in arena bounds */
    if (enemy->x < -ARENA_SIZE / 2) enemy->x = -ARENA_SIZE / 2;
    if (enemy->x >  ARENA_SIZE / 2) enemy->x =  ARENA_SIZE / 2;
    if (enemy->z < -ARENA_SIZE / 2) enemy->z = -ARENA_SIZE / 2;
    if (enemy->z >  ARENA_SIZE / 2) enemy->z =  ARENA_SIZE / 2;
}

/* =============================================================================
 * COMBAT
 * ========================================================================== */

static gboolean
demo_third_person_on_attack (LrgThirdPersonTemplate *template,
                             gint                    attack_type)
{
    DemoThirdPerson *self = DEMO_THIRD_PERSON (template);
    gfloat px, py, pz;
    gfloat player_rotation;
    gint i;

    (void)attack_type;

    /* Check cooldown */
    if (self->attack_cooldown > 0.0f)
        return FALSE;

    /* Start attack */
    self->is_attacking = TRUE;
    self->attack_anim_timer = 0.2f;
    self->attack_cooldown = ATTACK_COOLDOWN;

    /* Handle combo */
    if (self->combo_timer > 0.0f && self->combo_count < MAX_COMBO)
    {
        self->combo_count++;
    }
    else
    {
        self->combo_count = 1;
    }
    self->combo_timer = COMBO_WINDOW;

    /* Get player position and facing */
    lrg_third_person_template_get_position (template, &px, &py, &pz);
    player_rotation = lrg_third_person_template_get_rotation (template);

    /* Check hits on enemies */
    for (i = 0; i < ENEMY_COUNT; i++)
    {
        Enemy *enemy = &enemies[i];
        gfloat dist;
        gfloat angle_to_enemy;
        gfloat angle_diff;

        if (enemy->state == ENEMY_STATE_DEAD)
            continue;

        dist = distance_2d (px, pz, enemy->x, enemy->z);
        if (dist > ATTACK_RANGE)
            continue;

        /* Check if enemy is in front of player */
        angle_to_enemy = angle_to_target (px, pz, enemy->x, enemy->z);
        angle_diff = fabsf (fmodf (angle_to_enemy - player_rotation + (gfloat)G_PI * 3.0f, (gfloat)G_PI * 2.0f) - (gfloat)G_PI);

        if (angle_diff < (gfloat)G_PI / 2.5f)
        {
            gfloat damage;
            gfloat knockback;

            damage = ATTACK_DAMAGE * (1.0f + self->combo_count * 0.25f);
            enemy->health -= damage;
            enemy->state = ENEMY_STATE_HURT;
            enemy->state_timer = 0.3f;

            /* Knockback */
            knockback = 1.5f;
            enemy->x += sinf (player_rotation) * knockback;
            enemy->z += cosf (player_rotation) * knockback;

            if (enemy->health <= 0.0f)
            {
                enemy->state = ENEMY_STATE_DEAD;
                enemy->respawn_timer = ENEMY_RESPAWN_TIME;
                self->enemies_defeated++;
                self->score += 100 * self->combo_count;
            }

            lrg_game_template_shake (LRG_GAME_TEMPLATE (template), 0.15f);
        }
    }

    return TRUE;
}

/* =============================================================================
 * DODGE
 * ========================================================================== */

static void
demo_third_person_on_dodge (LrgThirdPersonTemplate *template,
                            gfloat                  direction_x,
                            gfloat                  direction_z)
{
    DemoThirdPerson *self = DEMO_THIRD_PERSON (template);

    (void)direction_x;
    (void)direction_z;

    self->dodge_cooldown = 0.5f;
}

/* =============================================================================
 * DAMAGE
 * ========================================================================== */

static void
demo_third_person_on_damage (LrgThirdPersonTemplate *template,
                             gfloat                  amount,
                             gfloat                  source_x,
                             gfloat                  source_y,
                             gfloat                  source_z)
{
    DemoThirdPerson *self = DEMO_THIRD_PERSON (template);

    (void)amount;
    (void)source_x;
    (void)source_y;
    (void)source_z;

    self->damage_flash = 0.3f;

    /* Break combo on hit */
    self->combo_count = 0;
    self->combo_timer = 0.0f;
}

static void
demo_third_person_on_death (LrgThirdPersonTemplate *template)
{
    DemoThirdPerson *self = DEMO_THIRD_PERSON (template);

    /* Reset health and position */
    lrg_third_person_template_set_health (template, 100.0f);
    lrg_third_person_template_set_position (template, 0.0f, 5.0f, -10.0f);

    /* Penalty */
    self->score = MAX (0, self->score - 200);
}

/* =============================================================================
 * PICKUP COLLECTION
 * ========================================================================== */

static void
check_pickups (DemoThirdPerson *self)
{
    LrgThirdPersonTemplate *template = LRG_THIRD_PERSON_TEMPLATE (self);
    gfloat px, py, pz;
    gint i;

    lrg_third_person_template_get_position (template, &px, &py, &pz);

    for (i = 0; i < PICKUP_COUNT; i++)
    {
        Pickup *pickup = &pickups[i];
        gfloat dist;

        if (!pickup->active)
            continue;

        dist = distance_3d (px, py + 1.0f, pz,
                           pickup->x, pickup->y, pickup->z);

        if (dist < 1.5f)
        {
            pickup->active = FALSE;

            if (pickup->type == PICKUP_TYPE_COIN)
            {
                self->coins_collected++;
                self->score += COIN_VALUE;
            }
            else
            {
                gfloat health = lrg_third_person_template_get_health (template);
                gfloat max_health = lrg_third_person_template_get_max_health (template);
                health = MIN (health + HEALTH_PICKUP_VALUE, max_health);
                lrg_third_person_template_set_health (template, health);
            }
        }
    }
}

/* =============================================================================
 * UPDATE
 * ========================================================================== */

static void
demo_third_person_pre_update (LrgGameTemplate *template,
                              gdouble          delta)
{
    DemoThirdPerson *self = DEMO_THIRD_PERSON (template);
    LrgThirdPersonTemplate *tp = LRG_THIRD_PERSON_TEMPLATE (template);
    gfloat px, py, pz;
    gint i;

    /* Update timers */
    self->play_time += (gfloat)delta;

    if (self->attack_cooldown > 0.0f)
        self->attack_cooldown -= (gfloat)delta;

    if (self->combo_timer > 0.0f)
    {
        self->combo_timer -= (gfloat)delta;
        if (self->combo_timer <= 0.0f)
            self->combo_count = 0;
    }

    if (self->attack_anim_timer > 0.0f)
    {
        self->attack_anim_timer -= (gfloat)delta;
        if (self->attack_anim_timer <= 0.0f)
            self->is_attacking = FALSE;
    }

    if (self->damage_flash > 0.0f)
        self->damage_flash -= (gfloat)delta;

    if (self->dodge_cooldown > 0.0f)
        self->dodge_cooldown -= (gfloat)delta;

    /* Get player position */
    lrg_third_person_template_get_position (tp, &px, &py, &pz);

    /* Update enemies */
    for (i = 0; i < ENEMY_COUNT; i++)
    {
        update_enemy (&enemies[i], px, py, pz, (gfloat)delta, self);
    }

    /* Update pickups animation */
    for (i = 0; i < PICKUP_COUNT; i++)
    {
        if (pickups[i].active)
        {
            pickups[i].spin_angle += (gfloat)delta * 2.0f;
            pickups[i].y = 1.0f + sinf (self->play_time * 2.0f + pickups[i].bob_offset) * 0.3f;
        }
    }

    /* Check pickup collection */
    check_pickups (self);

    /* Lock-on input handling */
    if (grl_input_is_key_pressed (GRL_KEY_Q))
    {
        if (self->lock_on_enemy_index >= 0)
        {
            /* Clear lock-on */
            lrg_third_person_template_clear_lock_on (tp);
            self->lock_on_enemy_index = -1;
        }
        else
        {
            /* Find closest enemy to lock onto */
            gfloat closest_dist = ENEMY_CHASE_RANGE * 2.0f;
            gint closest_idx = -1;
            gfloat dist;

            for (i = 0; i < ENEMY_COUNT; i++)
            {
                if (enemies[i].state == ENEMY_STATE_DEAD)
                    continue;

                dist = distance_2d (px, pz, enemies[i].x, enemies[i].z);
                if (dist < closest_dist)
                {
                    closest_dist = dist;
                    closest_idx = i;
                }
            }

            if (closest_idx >= 0)
            {
                self->lock_on_enemy_index = closest_idx;
                lrg_third_person_template_set_lock_on_target (
                    tp,
                    enemies[closest_idx].x,
                    enemies[closest_idx].y + 1.0f,
                    enemies[closest_idx].z);
            }
        }
    }

    /* Update lock-on target position */
    if (self->lock_on_enemy_index >= 0)
    {
        Enemy *target = &enemies[self->lock_on_enemy_index];
        if (target->state == ENEMY_STATE_DEAD)
        {
            lrg_third_person_template_clear_lock_on (tp);
            self->lock_on_enemy_index = -1;
        }
        else
        {
            lrg_third_person_template_set_lock_on_target (
                tp, target->x, target->y + 1.0f, target->z);
        }
    }

    /* Shoulder swap */
    if (grl_input_is_key_pressed (GRL_KEY_TAB))
    {
        lrg_third_person_template_swap_shoulder (tp);
    }

    LRG_GAME_TEMPLATE_CLASS (demo_third_person_parent_class)->pre_update (template, delta);
}

/* =============================================================================
 * RENDERING
 * ========================================================================== */

static void
draw_ground_plane (void)
{
    GrlVector3 *pos;
    GrlVector3 *size;
    gint i;

    /* Main ground */
    pos = grl_vector3_new (0.0f, -0.5f, 0.0f);
    size = grl_vector3_new (ARENA_SIZE, 1.0f, ARENA_SIZE);
    grl_draw_cube_v (pos, size, color_ground);
    grl_vector3_free (pos);
    grl_vector3_free (size);

    /* Arena walls */
    size = grl_vector3_new (ARENA_SIZE + 2.0f, 5.0f, 1.0f);

    pos = grl_vector3_new (0.0f, 2.5f, ARENA_SIZE / 2 + 0.5f);
    grl_draw_cube_v (pos, size, color_wall);
    grl_vector3_free (pos);

    pos = grl_vector3_new (0.0f, 2.5f, -ARENA_SIZE / 2 - 0.5f);
    grl_draw_cube_v (pos, size, color_wall);
    grl_vector3_free (pos);
    grl_vector3_free (size);

    size = grl_vector3_new (1.0f, 5.0f, ARENA_SIZE + 2.0f);

    pos = grl_vector3_new (ARENA_SIZE / 2 + 0.5f, 2.5f, 0.0f);
    grl_draw_cube_v (pos, size, color_wall);
    grl_vector3_free (pos);

    pos = grl_vector3_new (-ARENA_SIZE / 2 - 0.5f, 2.5f, 0.0f);
    grl_draw_cube_v (pos, size, color_wall);
    grl_vector3_free (pos);
    grl_vector3_free (size);

    /* Platforms */
    for (i = 0; i < PLATFORM_COUNT; i++)
    {
        Platform *p = &platforms[i];
        pos = grl_vector3_new (p->x, p->y / 2, p->z);
        size = grl_vector3_new (p->width, p->height, p->depth);
        grl_draw_cube_v (pos, size, color_platform);
        grl_vector3_free (pos);
        grl_vector3_free (size);
    }
}

static void
draw_enemies (DemoThirdPerson *self)
{
    gint i;
    GrlVector3 *pos;
    GrlColor *enemy_color;

    (void)self;

    for (i = 0; i < ENEMY_COUNT; i++)
    {
        Enemy *enemy = &enemies[i];

        if (enemy->state == ENEMY_STATE_DEAD)
            continue;

        /* Choose color based on state */
        enemy_color = (enemy->state == ENEMY_STATE_CHASE ||
                       enemy->state == ENEMY_STATE_ATTACK)
                      ? color_enemy_alerted : color_enemy;

        /* Draw enemy body */
        pos = grl_vector3_new (enemy->x, enemy->y + 0.75f, enemy->z);
        grl_draw_cylinder (pos, 0.4f, 0.4f, 1.5f, 8, enemy_color);
        grl_vector3_free (pos);

        /* Draw enemy head */
        pos = grl_vector3_new (enemy->x, enemy->y + 1.8f, enemy->z);
        grl_draw_sphere (pos, 0.35f, enemy_color);
        grl_vector3_free (pos);

        /* Draw facing indicator */
        {
            GrlVector3 *from = grl_vector3_new (enemy->x, enemy->y + 1.0f, enemy->z);
            GrlVector3 *to = grl_vector3_new (
                enemy->x + sinf (enemy->rotation) * 0.8f,
                enemy->y + 1.0f,
                enemy->z + cosf (enemy->rotation) * 0.8f);
            grl_draw_line_3d (from, to, color_enemy_alerted);
            grl_vector3_free (from);
            grl_vector3_free (to);
        }

        /* Health bar above enemy */
        if (enemy->health < ENEMY_HEALTH)
        {
            gfloat health_pct = enemy->health / ENEMY_HEALTH;
            GrlVector3 *bar_pos = grl_vector3_new (enemy->x, enemy->y + 2.3f, enemy->z);
            GrlVector3 *bar_size_bg = grl_vector3_new (1.0f, 0.1f, 0.05f);
            GrlVector3 *bar_size = grl_vector3_new (health_pct * 1.0f, 0.1f, 0.05f);

            grl_draw_cube_v (bar_pos, bar_size_bg, color_health_bar_bg);
            grl_vector3_free (bar_pos);

            bar_pos = grl_vector3_new (enemy->x - (1.0f - health_pct) * 0.5f,
                                       enemy->y + 2.3f, enemy->z);
            grl_draw_cube_v (bar_pos, bar_size, color_health_bar);
            grl_vector3_free (bar_pos);
            grl_vector3_free (bar_size_bg);
            grl_vector3_free (bar_size);
        }
    }
}

static void
draw_pickups (DemoThirdPerson *self)
{
    gint i;
    GrlVector3 *pos;

    (void)self;

    for (i = 0; i < PICKUP_COUNT; i++)
    {
        Pickup *pickup = &pickups[i];

        if (!pickup->active)
            continue;

        pos = grl_vector3_new (pickup->x, pickup->y, pickup->z);

        if (pickup->type == PICKUP_TYPE_COIN)
        {
            /* Draw spinning coin */
            grl_draw_cylinder (pos, 0.25f, 0.25f, 0.1f, 16, color_coin);
        }
        else
        {
            /* Draw health pickup */
            grl_draw_sphere (pos, 0.3f, color_health);
        }

        grl_vector3_free (pos);
    }
}

static void
demo_third_person_draw_character (LrgThirdPersonTemplate *template)
{
    DemoThirdPerson *self = DEMO_THIRD_PERSON (template);
    gfloat px, py, pz;
    gfloat rotation;
    GrlVector3 *pos;
    GrlColor *body_color;
    gboolean is_aiming;

    lrg_third_person_template_get_position (template, &px, &py, &pz);
    rotation = lrg_third_person_template_get_rotation (template);
    is_aiming = lrg_third_person_template_is_aiming (template);

    body_color = is_aiming ? color_player_aim : color_player;

    /* Shadow */
    pos = grl_vector3_new (px, 0.02f, pz);
    grl_draw_cylinder (pos, 0.5f, 0.5f, 0.02f, 16, color_shadow);
    grl_vector3_free (pos);

    /* Body (torso) */
    pos = grl_vector3_new (px, py + 0.75f, pz);
    grl_draw_cylinder (pos, 0.35f, 0.3f, 1.2f, 8, body_color);
    grl_vector3_free (pos);

    /* Head */
    pos = grl_vector3_new (px, py + 1.7f, pz);
    grl_draw_sphere (pos, 0.3f, body_color);
    grl_vector3_free (pos);

    /* Arms - extend when attacking */
    {
        gfloat arm_extend = self->is_attacking ? 0.8f : 0.0f;
        GrlVector3 *arm_pos;

        /* Right arm */
        arm_pos = grl_vector3_new (
            px + sinf (rotation + G_PI / 2) * 0.4f + sinf (rotation) * arm_extend,
            py + 1.0f,
            pz + cosf (rotation + G_PI / 2) * 0.4f + cosf (rotation) * arm_extend);
        grl_draw_sphere (arm_pos, 0.15f, body_color);
        grl_vector3_free (arm_pos);

        /* Left arm */
        arm_pos = grl_vector3_new (
            px + sinf (rotation - G_PI / 2) * 0.4f,
            py + 1.0f,
            pz + cosf (rotation - G_PI / 2) * 0.4f);
        grl_draw_sphere (arm_pos, 0.15f, body_color);
        grl_vector3_free (arm_pos);
    }

    /* Facing indicator */
    {
        GrlVector3 *from = grl_vector3_new (px, py + 1.0f, pz);
        GrlVector3 *to = grl_vector3_new (
            px + sinf (rotation) * 1.0f,
            py + 1.0f,
            pz + cosf (rotation) * 1.0f);
        grl_draw_line_3d (from, to, color_crosshair);
        grl_vector3_free (from);
        grl_vector3_free (to);
    }
}

static void
demo_third_person_draw_world (LrgGame3DTemplate *template)
{
    DemoThirdPerson *self = DEMO_THIRD_PERSON (template);

    grl_draw_clear_background (color_sky);

    draw_ground_plane ();
    draw_enemies (self);
    draw_pickups (self);
}

static void
demo_third_person_draw_target_indicator (LrgThirdPersonTemplate *template)
{
    DemoThirdPerson *self = DEMO_THIRD_PERSON (template);
    gfloat tx, ty, tz;
    GrlVector3 *pos;
    GrlVector3 *axis_y;

    if (self->lock_on_enemy_index < 0)
        return;

    if (!lrg_third_person_template_get_lock_on_target (template, &tx, &ty, &tz))
        return;

    /* Y axis for horizontal circles */
    axis_y = grl_vector3_new (0.0f, 1.0f, 0.0f);

    /* Draw lock-on ring around target */
    pos = grl_vector3_new (tx, ty, tz);
    grl_draw_circle_3d (pos, 0.8f, axis_y, 0.0f, color_lock_on);
    grl_vector3_free (pos);

    pos = grl_vector3_new (tx, ty + 0.3f, tz);
    grl_draw_circle_3d (pos, 0.6f, axis_y, self->play_time * 2.0f, color_lock_on);
    grl_vector3_free (pos);

    grl_vector3_free (axis_y);
}

static void
demo_third_person_draw_crosshair (LrgThirdPersonTemplate *template)
{
    DemoThirdPerson *self = DEMO_THIRD_PERSON (template);
    gint cx, cy;
    gint size;

    (void)self;

    if (!lrg_third_person_template_is_aiming (template))
        return;

    cx = WINDOW_WIDTH / 2;
    cy = WINDOW_HEIGHT / 2;
    size = 12;

    /* Draw crosshair */
    grl_draw_rectangle (cx - 1, cy - size, 2, size - 4, color_crosshair);
    grl_draw_rectangle (cx - 1, cy + 4, 2, size - 4, color_crosshair);
    grl_draw_rectangle (cx - size, cy - 1, size - 4, 2, color_crosshair);
    grl_draw_rectangle (cx + 4, cy - 1, size - 4, 2, color_crosshair);

    /* Center dot */
    grl_draw_circle (cx, cy, 2, color_crosshair);
}

static void
demo_third_person_draw_hud (LrgThirdPersonTemplate *template)
{
    DemoThirdPerson *self = DEMO_THIRD_PERSON (template);
    gfloat health, max_health, stamina, max_stamina;
    gint health_width, stamina_width;
    g_autofree gchar *score_str = NULL;
    g_autofree gchar *stats_str = NULL;
    g_autofree gchar *combo_str = NULL;

    health = lrg_third_person_template_get_health (template);
    max_health = lrg_third_person_template_get_max_health (template);
    stamina = lrg_third_person_template_get_stamina (template);
    max_stamina = lrg_third_person_template_get_max_stamina (template);

    /* Health bar */
    health_width = (gint)((health / max_health) * 200.0f);
    grl_draw_rectangle (20, 20, 200, 20, color_health_bar_bg);
    grl_draw_rectangle (20, 20, health_width, 20, color_health_bar);
    grl_draw_text ("HP", 24, 22, 16, color_hud);

    /* Stamina bar */
    stamina_width = (gint)((stamina / max_stamina) * 200.0f);
    grl_draw_rectangle (20, 45, 200, 14, color_stamina_bar_bg);
    grl_draw_rectangle (20, 45, stamina_width, 14, color_stamina_bar);

    /* Score */
    score_str = g_strdup_printf ("Score: %d", self->score);
    grl_draw_text (score_str, WINDOW_WIDTH - 150, 20, 24, color_hud);

    /* Stats */
    stats_str = g_strdup_printf ("Coins: %d  Enemies: %d",
                                 self->coins_collected,
                                 self->enemies_defeated);
    grl_draw_text (stats_str, WINDOW_WIDTH - 200, 50, 16, color_hud);

    /* Combo indicator */
    if (self->combo_count > 1 && self->combo_timer > 0.0f)
    {
        combo_str = g_strdup_printf ("%dx COMBO!", self->combo_count);
        grl_draw_text (combo_str, WINDOW_WIDTH / 2 - 50, 100, 32, color_combo);
    }

    /* Damage flash overlay */
    if (self->damage_flash > 0.0f)
    {
        GrlColor *flash = grl_color_new (255, 0, 0, (guint8)(self->damage_flash * 150.0f));
        grl_draw_rectangle (0, 0, WINDOW_WIDTH, WINDOW_HEIGHT, flash);
        grl_color_free (flash);
    }

    /* Controls help */
    grl_draw_text ("WASD: Move  Mouse: Camera  LMB: Attack  RMB: Aim  Space: Jump  E: Dodge  Q: Lock-on  Tab: Swap Shoulder",
                   20, WINDOW_HEIGHT - 25, 12, color_hud);
}

/* =============================================================================
 * CONFIGURATION
 * ========================================================================== */

static void
demo_third_person_configure (LrgGameTemplate *template)
{
    LrgGame3DTemplate *template_3d = LRG_GAME_3D_TEMPLATE (template);
    LrgThirdPersonTemplate *tp = LRG_THIRD_PERSON_TEMPLATE (template);

    LRG_GAME_TEMPLATE_CLASS (demo_third_person_parent_class)->configure (template);

    /* Window settings */
    lrg_game_template_set_title (template, "Third Person Demo - Template System");

    /* 3D settings */
    lrg_game_3d_template_set_fov (template_3d, 60.0f);
    lrg_game_3d_template_set_mouse_sensitivity (template_3d, 0.003f);

    /* Movement */
    lrg_third_person_template_set_move_speed (tp, 6.0f);
    lrg_third_person_template_set_run_multiplier (tp, 1.6f);
    lrg_third_person_template_set_jump_height (tp, 2.5f);
    lrg_third_person_template_set_rotation_speed (tp, 720.0f);

    /* Camera */
    lrg_third_person_template_set_camera_distance (tp, 5.0f);
    lrg_third_person_template_set_camera_height (tp, 2.0f);
    lrg_third_person_template_set_camera_smoothing (tp, 0.15f);
    lrg_third_person_template_set_aim_distance (tp, 2.5f);

    /* Shoulder offset */
    lrg_third_person_template_set_shoulder_offset (tp, 0.8f, 0.3f);

    /* Health and stamina */
    lrg_third_person_template_set_max_health (tp, 100.0f);
    lrg_third_person_template_set_health (tp, 100.0f);
    lrg_third_person_template_set_max_stamina (tp, 100.0f);
    lrg_third_person_template_set_stamina (tp, 100.0f);

    /* Dodge */
    lrg_third_person_template_set_dodge_distance (tp, 4.0f);
    lrg_third_person_template_set_dodge_stamina_cost (tp, 20.0f);

    /* Lock-on */
    lrg_third_person_template_set_lock_on_range (tp, 20.0f);

    /* Starting position */
    lrg_third_person_template_set_position (tp, 0.0f, 0.0f, -10.0f);
}

static void
demo_third_person_post_startup (LrgGameTemplate *template)
{
    LRG_GAME_TEMPLATE_CLASS (demo_third_person_parent_class)->post_startup (template);

    /* Initialize colors */
    color_sky = grl_color_new (135, 180, 220, 255);
    color_ground = grl_color_new (80, 120, 80, 255);
    color_platform = grl_color_new (120, 100, 80, 255);
    color_player = grl_color_new (50, 100, 180, 255);
    color_player_aim = grl_color_new (80, 130, 200, 255);
    color_enemy = grl_color_new (180, 60, 60, 255);
    color_enemy_alerted = grl_color_new (220, 80, 40, 255);
    color_coin = grl_color_new (255, 220, 50, 255);
    color_health = grl_color_new (50, 220, 50, 255);
    color_health_bar = grl_color_new (200, 50, 50, 255);
    color_health_bar_bg = grl_color_new (60, 60, 60, 200);
    color_stamina_bar = grl_color_new (50, 150, 200, 255);
    color_stamina_bar_bg = grl_color_new (40, 40, 40, 200);
    color_crosshair = grl_color_new (255, 255, 255, 220);
    color_lock_on = grl_color_new (255, 200, 50, 255);
    color_hud = grl_color_new (240, 240, 240, 255);
    color_wall = grl_color_new (100, 90, 80, 255);
    color_shadow = grl_color_new (0, 0, 0, 80);
    color_combo = grl_color_new (255, 200, 50, 255);

    /* Initialize world */
    init_world ();
}

static void
demo_third_person_shutdown (LrgGameTemplate *template)
{
    g_clear_pointer (&color_sky, grl_color_free);
    g_clear_pointer (&color_ground, grl_color_free);
    g_clear_pointer (&color_platform, grl_color_free);
    g_clear_pointer (&color_player, grl_color_free);
    g_clear_pointer (&color_player_aim, grl_color_free);
    g_clear_pointer (&color_enemy, grl_color_free);
    g_clear_pointer (&color_enemy_alerted, grl_color_free);
    g_clear_pointer (&color_coin, grl_color_free);
    g_clear_pointer (&color_health, grl_color_free);
    g_clear_pointer (&color_health_bar, grl_color_free);
    g_clear_pointer (&color_health_bar_bg, grl_color_free);
    g_clear_pointer (&color_stamina_bar, grl_color_free);
    g_clear_pointer (&color_stamina_bar_bg, grl_color_free);
    g_clear_pointer (&color_crosshair, grl_color_free);
    g_clear_pointer (&color_lock_on, grl_color_free);
    g_clear_pointer (&color_hud, grl_color_free);
    g_clear_pointer (&color_wall, grl_color_free);
    g_clear_pointer (&color_shadow, grl_color_free);
    g_clear_pointer (&color_combo, grl_color_free);

    LRG_GAME_TEMPLATE_CLASS (demo_third_person_parent_class)->shutdown (template);
}

/* =============================================================================
 * CLASS INITIALIZATION
 * ========================================================================== */

static void
demo_third_person_class_init (DemoThirdPersonClass *klass)
{
    LrgGameTemplateClass *template_class = LRG_GAME_TEMPLATE_CLASS (klass);
    LrgGame3DTemplateClass *template_3d_class = LRG_GAME_3D_TEMPLATE_CLASS (klass);
    LrgThirdPersonTemplateClass *tp_class = LRG_THIRD_PERSON_TEMPLATE_CLASS (klass);

    /* Game template overrides */
    template_class->configure = demo_third_person_configure;
    template_class->post_startup = demo_third_person_post_startup;
    template_class->shutdown = demo_third_person_shutdown;
    template_class->pre_update = demo_third_person_pre_update;

    /* 3D template overrides */
    template_3d_class->draw_world = demo_third_person_draw_world;

    /* Third person template overrides */
    tp_class->on_attack = demo_third_person_on_attack;
    tp_class->on_dodge = demo_third_person_on_dodge;
    tp_class->on_damage = demo_third_person_on_damage;
    tp_class->on_death = demo_third_person_on_death;
    tp_class->draw_character = demo_third_person_draw_character;
    tp_class->draw_target_indicator = demo_third_person_draw_target_indicator;
    tp_class->draw_crosshair = demo_third_person_draw_crosshair;
    tp_class->draw_hud = demo_third_person_draw_hud;
}

static void
demo_third_person_init (DemoThirdPerson *self)
{
    self->combo_count = 0;
    self->combo_timer = 0.0f;
    self->attack_cooldown = 0.0f;
    self->is_attacking = FALSE;
    self->attack_anim_timer = 0.0f;
    self->score = 0;
    self->coins_collected = 0;
    self->enemies_defeated = 0;
    self->play_time = 0.0f;
    self->lock_on_enemy_index = -1;
    self->damage_flash = 0.0f;
    self->dodge_cooldown = 0.0f;
}

/* =============================================================================
 * MAIN
 * ========================================================================== */

int
main (int    argc,
      char **argv)
{
    g_autoptr(DemoThirdPerson) game = NULL;

    game = g_object_new (DEMO_TYPE_THIRD_PERSON, NULL);

    return lrg_game_template_run (LRG_GAME_TEMPLATE (game), argc, argv);
}
