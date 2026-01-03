/* game-fps-demo.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * A first-person shooter demo demonstrating LrgFPSTemplate.
 *
 * Features demonstrated:
 * - Subclassing LrgFPSTemplate for custom FPS game logic
 * - First-person movement (WASD + mouse look)
 * - Sprint and crouch mechanics
 * - Weapon firing and crosshair
 * - Health and damage system
 * - Head bob effect
 * - Basic 3D environment
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

#define WINDOW_WIDTH     1280
#define WINDOW_HEIGHT    720
#define NUM_TARGETS      5
#define TARGET_RADIUS    1.0f
#define FIRE_COOLDOWN    0.15f
#define FLOOR_SIZE       40.0f

/* =============================================================================
 * COLORS
 * ========================================================================== */

static GrlColor *color_sky = NULL;
static GrlColor *color_floor = NULL;
static GrlColor *color_wall = NULL;
static GrlColor *color_target = NULL;
static GrlColor *color_target_hit = NULL;
static GrlColor *color_crosshair = NULL;
static GrlColor *color_hud = NULL;
static GrlColor *color_health = NULL;
static GrlColor *color_health_bg = NULL;

/* =============================================================================
 * TARGET DATA
 * ========================================================================== */

typedef struct
{
    gfloat x, y, z;
    gboolean active;
    gfloat respawn_timer;
} Target;

static Target targets[NUM_TARGETS] = {
    {  5.0f, 1.5f,  8.0f, TRUE, 0.0f },
    { -6.0f, 2.0f,  5.0f, TRUE, 0.0f },
    {  8.0f, 1.0f, -3.0f, TRUE, 0.0f },
    { -4.0f, 2.5f, -8.0f, TRUE, 0.0f },
    {  0.0f, 3.0f, 10.0f, TRUE, 0.0f },
};

/* =============================================================================
 * CUSTOM FPS TYPE
 * ========================================================================== */

#define DEMO_TYPE_FPS (demo_fps_get_type ())
G_DECLARE_FINAL_TYPE (DemoFPS, demo_fps, DEMO, FPS, LrgFPSTemplate)

struct _DemoFPS
{
    LrgFPSTemplate parent_instance;

    gint score;
    gint shots_fired;
    gint targets_hit;
    gfloat play_time;
    gfloat fire_cooldown;
    gboolean firing;
    gfloat muzzle_flash;
};

G_DEFINE_FINAL_TYPE (DemoFPS, demo_fps, LRG_TYPE_FPS_TEMPLATE)

/* =============================================================================
 * RAY-SPHERE INTERSECTION
 * ========================================================================== */

static gboolean
ray_sphere_intersect (gfloat ox, gfloat oy, gfloat oz,   /* Ray origin */
                      gfloat dx, gfloat dy, gfloat dz,   /* Ray direction (normalized) */
                      gfloat sx, gfloat sy, gfloat sz,   /* Sphere center */
                      gfloat radius)
{
    gfloat lx, ly, lz;
    gfloat a, b, c, disc;

    lx = ox - sx;
    ly = oy - sy;
    lz = oz - sz;

    a = dx * dx + dy * dy + dz * dz;
    b = 2.0f * (lx * dx + ly * dy + lz * dz);
    c = lx * lx + ly * ly + lz * lz - radius * radius;

    disc = b * b - 4.0f * a * c;
    return disc >= 0.0f;
}

/* =============================================================================
 * GAME EVENTS
 * ========================================================================== */

static gboolean
demo_fps_on_fire (LrgFPSTemplate *template,
                  gboolean        is_primary)
{
    DemoFPS *self = DEMO_FPS (template);
    gfloat px, py, pz;
    gfloat pitch, yaw;
    gfloat dx, dy, dz;
    gint i;
    Target *t;
    gfloat len;

    (void)is_primary;

    /* Check cooldown */
    if (self->fire_cooldown > 0.0f)
        return FALSE;

    self->fire_cooldown = FIRE_COOLDOWN;
    self->shots_fired++;
    self->muzzle_flash = 0.1f;
    self->firing = TRUE;

    /* Get player position and look direction */
    lrg_fps_template_get_position (template, &px, &py, &pz);
    pitch = lrg_game_3d_template_get_pitch (LRG_GAME_3D_TEMPLATE (template));
    yaw = lrg_game_3d_template_get_yaw (LRG_GAME_3D_TEMPLATE (template));

    /* Add eye height */
    py += lrg_fps_template_get_standing_height (template);

    /* Calculate look direction */
    dx = cosf (pitch) * sinf (yaw);
    dy = sinf (pitch);
    dz = cosf (pitch) * cosf (yaw);

    /* Normalize (should already be close) */
    len = sqrtf (dx * dx + dy * dy + dz * dz);
    dx /= len;
    dy /= len;
    dz /= len;

    /* Check hits on targets */
    for (i = 0; i < NUM_TARGETS; i++)
    {
        t = &targets[i];
        if (!t->active)
            continue;

        if (ray_sphere_intersect (px, py, pz, dx, dy, dz,
                                  t->x, t->y, t->z, TARGET_RADIUS))
        {
            t->active = FALSE;
            t->respawn_timer = 3.0f;
            self->targets_hit++;
            self->score += 100;

            /* Screen shake for hit feedback */
            lrg_game_template_shake (LRG_GAME_TEMPLATE (template), 0.2f);
            break;
        }
    }

    return TRUE;
}

static void
demo_fps_on_jump (LrgFPSTemplate *template)
{
    /* Could play jump sound here */
    (void)template;
}

static void
demo_fps_on_land (LrgFPSTemplate *template,
                  gfloat          fall_velocity)
{
    /* Add camera shake on hard landing */
    if (fall_velocity > 5.0f)
    {
        gfloat trauma = (fall_velocity - 5.0f) * 0.05f;
        if (trauma > 0.5f)
            trauma = 0.5f;
        lrg_game_template_shake (LRG_GAME_TEMPLATE (template), trauma);
    }
}

/* =============================================================================
 * UPDATE
 * ========================================================================== */

static void
demo_fps_pre_update (LrgGameTemplate *template,
                     gdouble          delta)
{
    DemoFPS *self = DEMO_FPS (template);
    gint i;
    Target *t;

    /* Update play time */
    self->play_time += (gfloat)delta;

    /* Update fire cooldown */
    if (self->fire_cooldown > 0.0f)
    {
        self->fire_cooldown -= (gfloat)delta;
        if (self->fire_cooldown <= 0.0f)
        {
            self->firing = FALSE;
        }
    }

    /* Update muzzle flash */
    if (self->muzzle_flash > 0.0f)
    {
        self->muzzle_flash -= (gfloat)delta;
    }

    /* Update target respawns */
    for (i = 0; i < NUM_TARGETS; i++)
    {
        t = &targets[i];
        if (!t->active)
        {
            t->respawn_timer -= (gfloat)delta;
            if (t->respawn_timer <= 0.0f)
            {
                t->active = TRUE;
                /* Move to new random position */
                t->x = (g_random_double () - 0.5) * 16.0f;
                t->z = g_random_double () * 15.0f + 3.0f;
                t->y = g_random_double () * 2.0f + 1.0f;
            }
        }
    }

    LRG_GAME_TEMPLATE_CLASS (demo_fps_parent_class)->pre_update (template, delta);
}

/* =============================================================================
 * RENDERING
 * ========================================================================== */

static void
demo_fps_draw_world (LrgGame3DTemplate *template)
{
    DemoFPS *self = DEMO_FPS (template);
    gint i;
    Target *t;
    GrlVector3 *pos;
    GrlVector3 *floor_size;
    GrlVector3 *wall_size;
    GrlColor *target_color;
    gfloat bob_offset;

    /* Draw floor */
    pos = grl_vector3_new (0.0f, -0.5f, 0.0f);
    floor_size = grl_vector3_new (FLOOR_SIZE, 1.0f, FLOOR_SIZE);
    grl_draw_cube_v (pos, floor_size, color_floor);
    grl_vector3_free (pos);
    grl_vector3_free (floor_size);

    /* Draw walls */
    wall_size = grl_vector3_new (FLOOR_SIZE, 8.0f, 1.0f);

    /* Back wall */
    pos = grl_vector3_new (0.0f, 4.0f, FLOOR_SIZE / 2);
    grl_draw_cube_v (pos, wall_size, color_wall);
    grl_vector3_free (pos);

    /* Side walls */
    grl_vector3_free (wall_size);
    wall_size = grl_vector3_new (1.0f, 8.0f, FLOOR_SIZE);

    pos = grl_vector3_new (FLOOR_SIZE / 2, 4.0f, 0.0f);
    grl_draw_cube_v (pos, wall_size, color_wall);
    grl_vector3_free (pos);

    pos = grl_vector3_new (-FLOOR_SIZE / 2, 4.0f, 0.0f);
    grl_draw_cube_v (pos, wall_size, color_wall);
    grl_vector3_free (pos);
    grl_vector3_free (wall_size);

    /* Draw targets with bobbing animation */
    bob_offset = sinf (self->play_time * 2.0f) * 0.2f;

    for (i = 0; i < NUM_TARGETS; i++)
    {
        t = &targets[i];
        target_color = t->active ? color_target : color_target_hit;

        pos = grl_vector3_new (t->x, t->y + bob_offset * (i % 2 == 0 ? 1 : -1), t->z);
        grl_draw_sphere (pos, TARGET_RADIUS, target_color);
        grl_vector3_free (pos);
    }
}

static void
demo_fps_draw_crosshair (LrgFPSTemplate *template)
{
    DemoFPS *self = DEMO_FPS (template);
    gint cx, cy;
    gint gap, size;
    GrlColor *cross_color;

    cx = WINDOW_WIDTH / 2;
    cy = WINDOW_HEIGHT / 2;

    /* Dynamic crosshair - spreads when firing */
    gap = self->firing ? 8 : 4;
    size = 10;

    /* Crosshair color changes when ready to fire */
    cross_color = (self->fire_cooldown <= 0.0f) ? color_crosshair : color_hud;

    /* Draw cross */
    grl_draw_rectangle (cx - 1, cy - gap - size, 2, size, cross_color);  /* Top */
    grl_draw_rectangle (cx - 1, cy + gap, 2, size, cross_color);          /* Bottom */
    grl_draw_rectangle (cx - gap - size, cy - 1, size, 2, cross_color);   /* Left */
    grl_draw_rectangle (cx + gap, cy - 1, size, 2, cross_color);          /* Right */

    /* Center dot */
    grl_draw_rectangle (cx - 1, cy - 1, 2, 2, cross_color);
}

static void
demo_fps_draw_hud (LrgFPSTemplate *template)
{
    DemoFPS *self = DEMO_FPS (template);
    gfloat health;
    gint health_width;
    g_autofree gchar *score_str = NULL;
    g_autofree gchar *stats_str = NULL;
    g_autofree gchar *time_str = NULL;

    /* Health bar */
    health = lrg_fps_template_get_health (template);
    health_width = (gint)((health / 100.0f) * 200.0f);

    grl_draw_rectangle (20, WINDOW_HEIGHT - 40, 200, 20, color_health_bg);
    grl_draw_rectangle (20, WINDOW_HEIGHT - 40, health_width, 20, color_health);
    grl_draw_text ("HEALTH", 25, WINDOW_HEIGHT - 36, 16, color_hud);

    /* Score */
    score_str = g_strdup_printf ("SCORE: %d", self->score);
    grl_draw_text (score_str, 20, 20, 24, color_hud);

    /* Stats */
    stats_str = g_strdup_printf ("Hits: %d / %d  Accuracy: %.0f%%",
                                 self->targets_hit, self->shots_fired,
                                 self->shots_fired > 0 ?
                                     (gfloat)self->targets_hit / self->shots_fired * 100.0f : 0.0f);
    grl_draw_text (stats_str, 20, 50, 16, color_hud);

    /* Time */
    time_str = g_strdup_printf ("Time: %.1fs", self->play_time);
    grl_draw_text (time_str, WINDOW_WIDTH - 120, 20, 20, color_hud);

    /* Muzzle flash effect (screen border) */
    if (self->muzzle_flash > 0.0f)
    {
        GrlColor *flash = grl_color_new (255, 200, 100, (guint8)(self->muzzle_flash * 500.0f));
        grl_draw_rectangle (0, 0, 10, WINDOW_HEIGHT, flash);
        grl_draw_rectangle (WINDOW_WIDTH - 10, 0, 10, WINDOW_HEIGHT, flash);
        grl_color_free (flash);
    }

    /* Controls help */
    grl_draw_text ("WASD: Move  Mouse: Look  LMB: Shoot  Shift: Sprint  Ctrl: Crouch  Space: Jump",
                   20, WINDOW_HEIGHT - 60, 12, color_hud);
}

/* =============================================================================
 * CONFIGURATION
 * ========================================================================== */

static void
demo_fps_configure (LrgGameTemplate *template)
{
    LrgGame3DTemplate *template_3d = LRG_GAME_3D_TEMPLATE (template);
    LrgFPSTemplate *fps = LRG_FPS_TEMPLATE (template);

    /* Call parent configure */
    LRG_GAME_TEMPLATE_CLASS (demo_fps_parent_class)->configure (template);

    /* Window settings */
    lrg_game_template_set_title (template, "FPS Demo - Template System");

    /* 3D settings */
    lrg_game_3d_template_set_fov (template_3d, 75.0f);
    lrg_game_3d_template_set_mouse_sensitivity (template_3d, 0.002f);
    lrg_game_3d_template_set_invert_y (template_3d, FALSE);

    /* Movement */
    lrg_fps_template_set_walk_speed (fps, 5.0f);
    lrg_fps_template_set_sprint_multiplier (fps, 1.8f);
    lrg_fps_template_set_crouch_multiplier (fps, 0.5f);
    lrg_fps_template_set_jump_height (fps, 1.5f);

    /* Eye height */
    lrg_fps_template_set_standing_height (fps, 1.7f);
    lrg_fps_template_set_crouch_height (fps, 1.0f);

    /* Head bob */
    lrg_fps_template_set_head_bob_enabled (fps, TRUE);
    lrg_fps_template_set_head_bob_intensity (fps, 0.02f);

    /* Health */
    lrg_fps_template_set_max_health (fps, 100.0f);
    lrg_fps_template_set_health (fps, 100.0f);

    /* Starting position */
    lrg_fps_template_set_position (fps, 0.0f, 0.0f, -5.0f);
}

static void
demo_fps_post_startup (LrgGameTemplate *template)
{
    LRG_GAME_TEMPLATE_CLASS (demo_fps_parent_class)->post_startup (template);

    /* Initialize colors */
    color_sky = grl_color_new (100, 150, 200, 255);
    color_floor = grl_color_new (60, 100, 60, 255);
    color_wall = grl_color_new (100, 90, 80, 255);
    color_target = grl_color_new (200, 50, 50, 255);
    color_target_hit = grl_color_new (80, 80, 80, 128);
    color_crosshair = grl_color_new (0, 255, 0, 255);
    color_hud = grl_color_new (220, 220, 220, 255);
    color_health = grl_color_new (200, 50, 50, 255);
    color_health_bg = grl_color_new (60, 60, 60, 200);

    /* Background color is handled in draw_world */
}

static void
demo_fps_shutdown (LrgGameTemplate *template)
{
    g_clear_pointer (&color_sky, grl_color_free);
    g_clear_pointer (&color_floor, grl_color_free);
    g_clear_pointer (&color_wall, grl_color_free);
    g_clear_pointer (&color_target, grl_color_free);
    g_clear_pointer (&color_target_hit, grl_color_free);
    g_clear_pointer (&color_crosshair, grl_color_free);
    g_clear_pointer (&color_hud, grl_color_free);
    g_clear_pointer (&color_health, grl_color_free);
    g_clear_pointer (&color_health_bg, grl_color_free);

    LRG_GAME_TEMPLATE_CLASS (demo_fps_parent_class)->shutdown (template);
}

/* =============================================================================
 * CLASS INITIALIZATION
 * ========================================================================== */

static void
demo_fps_class_init (DemoFPSClass *klass)
{
    LrgGameTemplateClass *template_class = LRG_GAME_TEMPLATE_CLASS (klass);
    LrgGame3DTemplateClass *template_3d_class = LRG_GAME_3D_TEMPLATE_CLASS (klass);
    LrgFPSTemplateClass *fps_class = LRG_FPS_TEMPLATE_CLASS (klass);

    /* Game template overrides */
    template_class->configure = demo_fps_configure;
    template_class->post_startup = demo_fps_post_startup;
    template_class->shutdown = demo_fps_shutdown;
    template_class->pre_update = demo_fps_pre_update;

    /* 3D template overrides */
    template_3d_class->draw_world = demo_fps_draw_world;

    /* FPS template overrides */
    fps_class->on_fire = demo_fps_on_fire;
    fps_class->on_jump = demo_fps_on_jump;
    fps_class->on_land = demo_fps_on_land;
    fps_class->draw_crosshair = demo_fps_draw_crosshair;
    fps_class->draw_hud = demo_fps_draw_hud;
}

static void
demo_fps_init (DemoFPS *self)
{
    self->score = 0;
    self->shots_fired = 0;
    self->targets_hit = 0;
    self->play_time = 0.0f;
    self->fire_cooldown = 0.0f;
    self->firing = FALSE;
    self->muzzle_flash = 0.0f;
}

/* =============================================================================
 * MAIN
 * ========================================================================== */

int
main (int    argc,
      char **argv)
{
    g_autoptr(DemoFPS) game = NULL;

    game = g_object_new (DEMO_TYPE_FPS, NULL);

    return lrg_game_template_run (LRG_GAME_TEMPLATE (game), argc, argv);
}
