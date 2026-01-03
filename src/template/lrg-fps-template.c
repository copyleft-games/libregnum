/* lrg-fps-template.c - First-person shooter game template implementation
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "config.h"

#include "lrg-fps-template.h"
#include "../lrg-log.h"

#include <graylib.h>
#include <raylib.h>
#include <math.h>

/* Default values */
#define LRG_FPS_DEFAULT_WALK_SPEED       (5.0f)
#define LRG_FPS_DEFAULT_SPRINT_MULT      (1.8f)
#define LRG_FPS_DEFAULT_CROUCH_MULT      (0.5f)
#define LRG_FPS_DEFAULT_JUMP_HEIGHT      (1.5f)
#define LRG_FPS_DEFAULT_GRAVITY          (20.0f)
#define LRG_FPS_DEFAULT_STANDING_HEIGHT  (1.7f)
#define LRG_FPS_DEFAULT_CROUCH_HEIGHT    (0.9f)
#define LRG_FPS_DEFAULT_MAX_HEALTH       (100.0f)
#define LRG_FPS_DEFAULT_MAX_ARMOR        (100.0f)
#define LRG_FPS_DEFAULT_HEAD_BOB_SPEED   (10.0f)
#define LRG_FPS_DEFAULT_HEAD_BOB_AMOUNT  (0.05f)
#define LRG_FPS_DEFAULT_FLOOR_Y          (0.0f)

typedef struct
{
    /* Position */
    gfloat pos_x;
    gfloat pos_y;
    gfloat pos_z;
    gfloat vel_y;   /* Vertical velocity for gravity/jump */

    /* Movement settings */
    gfloat walk_speed;
    gfloat sprint_multiplier;
    gfloat crouch_multiplier;
    gfloat jump_height;
    gfloat gravity;

    /* Heights */
    gfloat standing_height;
    gfloat crouch_height;
    gfloat current_height;
    gfloat target_height;

    /* Posture/state */
    LrgFPSPosture posture;
    gboolean is_sprinting;
    gboolean on_ground;
    gboolean is_dead;

    /* Health/Armor */
    gfloat health;
    gfloat max_health;
    gfloat armor;
    gfloat max_armor;

    /* Weapon */
    gint current_weapon;
    gint ammo;
    gboolean is_reloading;
    gfloat reload_timer;

    /* Head bob */
    gboolean head_bob_enabled;
    gfloat head_bob_intensity;
    gfloat head_bob_timer;

    /* Crosshair */
    gboolean crosshair_visible;

    /* Input state */
    gfloat move_x;
    gfloat move_z;
    gboolean jump_pressed;
    gboolean sprint_held;
    gboolean crouch_held;
    gboolean fire_pressed;
    gboolean fire_secondary;
    gboolean reload_pressed;
} LrgFPSTemplatePrivate;

G_DEFINE_TYPE_WITH_PRIVATE (LrgFPSTemplate, lrg_fps_template,
                            LRG_TYPE_GAME_3D_TEMPLATE)

/* Property IDs */
enum
{
    PROP_0,
    PROP_WALK_SPEED,
    PROP_SPRINT_MULTIPLIER,
    PROP_CROUCH_MULTIPLIER,
    PROP_JUMP_HEIGHT,
    PROP_GRAVITY,
    PROP_STANDING_HEIGHT,
    PROP_CROUCH_HEIGHT,
    PROP_HEALTH,
    PROP_MAX_HEALTH,
    PROP_ARMOR,
    PROP_HEAD_BOB_ENABLED,
    PROP_HEAD_BOB_INTENSITY,
    PROP_CROSSHAIR_VISIBLE,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

/* Signal IDs */
enum
{
    SIGNAL_FIRED,
    SIGNAL_RELOADED,
    SIGNAL_WEAPON_SWITCHED,
    SIGNAL_JUMPED,
    SIGNAL_LANDED,
    SIGNAL_DAMAGED,
    SIGNAL_DIED,
    SIGNAL_POSTURE_CHANGED,
    N_SIGNALS
};

static guint signals[N_SIGNALS];

/* ==========================================================================
 * Helper Functions
 * ========================================================================== */

static gfloat
calculate_jump_velocity (gfloat gravity,
                         gfloat jump_height)
{
    return sqrtf (2.0f * gravity * jump_height);
}

/* ==========================================================================
 * Virtual Method Implementations
 * ========================================================================== */

static gboolean
lrg_fps_template_real_on_fire (LrgFPSTemplate *self,
                               gboolean        is_primary)
{
    LrgFPSTemplatePrivate *priv;

    priv = lrg_fps_template_get_instance_private (self);

    if (priv->is_reloading || priv->is_dead)
        return FALSE;

    if (priv->ammo <= 0)
        return FALSE;

    priv->ammo--;
    g_signal_emit (self, signals[SIGNAL_FIRED], 0, is_primary);

    return TRUE;
}

static gboolean
lrg_fps_template_real_on_reload (LrgFPSTemplate *self)
{
    LrgFPSTemplatePrivate *priv;

    priv = lrg_fps_template_get_instance_private (self);

    if (priv->is_reloading || priv->is_dead)
        return FALSE;

    priv->is_reloading = TRUE;
    priv->reload_timer = 2.0f;  /* 2 second reload by default */

    g_signal_emit (self, signals[SIGNAL_RELOADED], 0);
    return TRUE;
}

static void
lrg_fps_template_real_on_weapon_switch (LrgFPSTemplate *self,
                                        gint            weapon_index)
{
    LrgFPSTemplatePrivate *priv;

    priv = lrg_fps_template_get_instance_private (self);

    if (priv->is_dead)
        return;

    priv->current_weapon = weapon_index;
    priv->is_reloading = FALSE;

    g_signal_emit (self, signals[SIGNAL_WEAPON_SWITCHED], 0, weapon_index);
}

static void
lrg_fps_template_real_on_jump (LrgFPSTemplate *self)
{
    g_signal_emit (self, signals[SIGNAL_JUMPED], 0);
}

static void
lrg_fps_template_real_on_land (LrgFPSTemplate *self,
                               gfloat          fall_velocity)
{
    g_signal_emit (self, signals[SIGNAL_LANDED], 0, fall_velocity);
}

static void
lrg_fps_template_real_on_damage (LrgFPSTemplate *self,
                                 gfloat          amount,
                                 gfloat          source_x,
                                 gfloat          source_y,
                                 gfloat          source_z)
{
    g_signal_emit (self, signals[SIGNAL_DAMAGED], 0, amount, source_x, source_y, source_z);
}

static void
lrg_fps_template_real_on_death (LrgFPSTemplate *self)
{
    LrgFPSTemplatePrivate *priv;

    priv = lrg_fps_template_get_instance_private (self);
    priv->is_dead = TRUE;

    g_signal_emit (self, signals[SIGNAL_DIED], 0);
}

static void
lrg_fps_template_real_on_posture_changed (LrgFPSTemplate *self,
                                          LrgFPSPosture   old_posture,
                                          LrgFPSPosture   new_posture)
{
    g_signal_emit (self, signals[SIGNAL_POSTURE_CHANGED], 0, old_posture, new_posture);
}

static void
lrg_fps_template_real_update_movement (LrgFPSTemplate *self,
                                       gdouble         delta)
{
    LrgFPSTemplateClass *klass;
    LrgFPSTemplatePrivate *priv;
    LrgCamera3D *camera;
    gfloat yaw;
    gfloat yaw_rad;
    gfloat move_speed;
    gfloat forward_x;
    gfloat forward_z;
    gfloat right_x;
    gfloat right_z;
    gfloat dx;
    gfloat dz;
    gboolean was_on_ground;
    gfloat old_vel_y;

    priv = lrg_fps_template_get_instance_private (self);
    klass = LRG_FPS_TEMPLATE_GET_CLASS (self);

    if (priv->is_dead)
        return;

    camera = lrg_game_3d_template_get_camera (LRG_GAME_3D_TEMPLATE (self));
    if (camera == NULL)
        return;

    /* Get camera yaw for movement direction */
    yaw = lrg_game_3d_template_get_yaw (LRG_GAME_3D_TEMPLATE (self));
    yaw_rad = yaw * (gfloat)G_PI / 180.0f;

    /* Calculate forward and right vectors (horizontal plane only) */
    forward_x = sinf (yaw_rad);
    forward_z = cosf (yaw_rad);
    right_x = cosf (yaw_rad);
    right_z = -sinf (yaw_rad);

    /* Calculate movement speed */
    move_speed = priv->walk_speed;

    if (priv->is_sprinting && priv->on_ground && priv->posture == LRG_FPS_POSTURE_STANDING)
        move_speed *= priv->sprint_multiplier;

    if (priv->posture == LRG_FPS_POSTURE_CROUCHING)
        move_speed *= priv->crouch_multiplier;
    else if (priv->posture == LRG_FPS_POSTURE_PRONE)
        move_speed *= priv->crouch_multiplier * 0.5f;

    /* Calculate movement delta */
    dx = (forward_x * priv->move_z + right_x * priv->move_x) * move_speed * (gfloat)delta;
    dz = (forward_z * priv->move_z + right_z * priv->move_x) * move_speed * (gfloat)delta;

    priv->pos_x += dx;
    priv->pos_z += dz;

    /* Gravity and jumping */
    was_on_ground = priv->on_ground;
    old_vel_y = priv->vel_y;

    /* Check for jump */
    if (priv->jump_pressed && priv->on_ground && priv->posture != LRG_FPS_POSTURE_PRONE)
    {
        priv->vel_y = calculate_jump_velocity (priv->gravity, priv->jump_height);
        priv->on_ground = FALSE;

        if (klass->on_jump != NULL)
            klass->on_jump (self);
    }

    /* Apply gravity */
    if (!priv->on_ground)
    {
        priv->vel_y -= priv->gravity * (gfloat)delta;
        priv->pos_y += priv->vel_y * (gfloat)delta;
    }

    /* Ground check */
    if (klass->check_ground != NULL)
        priv->on_ground = klass->check_ground (self);

    /* Handle landing */
    if (priv->on_ground && !was_on_ground)
    {
        if (klass->on_land != NULL)
            klass->on_land (self, old_vel_y);

        priv->vel_y = 0.0f;
    }

    /* Clamp to floor */
    if (priv->pos_y < LRG_FPS_DEFAULT_FLOOR_Y)
    {
        priv->pos_y = LRG_FPS_DEFAULT_FLOOR_Y;
        priv->on_ground = TRUE;
        priv->vel_y = 0.0f;
    }

    /* Update height based on posture */
    switch (priv->posture)
    {
        case LRG_FPS_POSTURE_STANDING:
            priv->target_height = priv->standing_height;
            break;
        case LRG_FPS_POSTURE_CROUCHING:
            priv->target_height = priv->crouch_height;
            break;
        case LRG_FPS_POSTURE_PRONE:
            priv->target_height = priv->crouch_height * 0.5f;
            break;
    }

    /* Smooth height transition */
    priv->current_height += (priv->target_height - priv->current_height) * 0.2f;

    /* Head bob */
    if (priv->head_bob_enabled && priv->on_ground &&
        (priv->move_x != 0.0f || priv->move_z != 0.0f))
    {
        gfloat bob_speed;

        bob_speed = LRG_FPS_DEFAULT_HEAD_BOB_SPEED;
        if (priv->is_sprinting)
            bob_speed *= 1.5f;

        priv->head_bob_timer += bob_speed * (gfloat)delta;
    }
    else
    {
        /* Settle head bob when not moving */
        priv->head_bob_timer *= 0.9f;
    }

    /* Update camera position */
    {
        gfloat cam_y;
        gfloat bob_offset;

        cam_y = priv->pos_y + priv->current_height;

        /* Apply head bob */
        if (priv->head_bob_enabled && priv->head_bob_intensity > 0.0f)
        {
            bob_offset = sinf (priv->head_bob_timer) *
                         LRG_FPS_DEFAULT_HEAD_BOB_AMOUNT *
                         priv->head_bob_intensity;
            cam_y += bob_offset;
        }

        lrg_camera3d_set_position_xyz (camera, priv->pos_x, cam_y, priv->pos_z);
    }
}

static gboolean
lrg_fps_template_real_check_ground (LrgFPSTemplate *self)
{
    LrgFPSTemplatePrivate *priv;

    priv = lrg_fps_template_get_instance_private (self);

    /* Default: on ground if at or below floor level */
    return priv->pos_y <= LRG_FPS_DEFAULT_FLOOR_Y + 0.1f;
}

static void
lrg_fps_template_real_draw_weapon (LrgFPSTemplate *self)
{
    /* Default: no weapon rendering */
    (void)self;
}

static void
lrg_fps_template_real_draw_crosshair (LrgFPSTemplate *self)
{
    LrgFPSTemplatePrivate *priv;
    gint screen_w;
    gint screen_h;
    gint cx;
    gint cy;
    gint size;
    gint gap;
    g_autoptr(GrlColor) color = NULL;

    priv = lrg_fps_template_get_instance_private (self);

    if (!priv->crosshair_visible)
        return;

    screen_w = GetScreenWidth ();
    screen_h = GetScreenHeight ();
    cx = screen_w / 2;
    cy = screen_h / 2;
    size = 10;
    gap = 4;

    color = grl_color_new (255, 255, 255, 200);

    /* Draw cross */
    grl_draw_line (cx - size, cy, cx - gap, cy, color);
    grl_draw_line (cx + gap, cy, cx + size, cy, color);
    grl_draw_line (cx, cy - size, cx, cy - gap, color);
    grl_draw_line (cx, cy + gap, cx, cy + size, color);
}

static void
lrg_fps_template_real_draw_hud (LrgFPSTemplate *self)
{
    LrgFPSTemplatePrivate *priv;
    gint screen_h;
    g_autofree gchar *health_text = NULL;
    g_autofree gchar *armor_text = NULL;
    g_autofree gchar *ammo_text = NULL;
    g_autoptr(GrlColor) text_color = NULL;
    g_autoptr(GrlColor) health_color = NULL;
    g_autoptr(GrlColor) armor_color = NULL;

    priv = lrg_fps_template_get_instance_private (self);

    screen_h = GetScreenHeight ();

    text_color = grl_color_new (255, 255, 255, 255);

    /* Health */
    health_color = grl_color_new (200, 50, 50, 255);
    health_text = g_strdup_printf ("%.0f", priv->health);
    grl_draw_text (health_text, 20, screen_h - 50, 32, health_color);

    /* Armor */
    if (priv->armor > 0.0f)
    {
        armor_color = grl_color_new (50, 150, 200, 255);
        armor_text = g_strdup_printf ("%.0f", priv->armor);
        grl_draw_text (armor_text, 120, screen_h - 50, 32, armor_color);
    }

    /* Ammo */
    ammo_text = g_strdup_printf ("%d", priv->ammo);
    grl_draw_text (ammo_text, GetScreenWidth () - 80, screen_h - 50, 32, text_color);

    /* Reloading indicator */
    if (priv->is_reloading)
    {
        grl_draw_text ("RELOADING", GetScreenWidth () / 2 - 60, screen_h / 2 + 40, 20, text_color);
    }
}

/* ==========================================================================
 * Overridden Parent Virtual Methods
 * ========================================================================== */

static void
lrg_fps_template_pre_update (LrgGameTemplate *template,
                             gdouble          delta)
{
    LrgFPSTemplate *self;
    LrgFPSTemplateClass *klass;
    LrgFPSTemplatePrivate *priv;

    self = LRG_FPS_TEMPLATE (template);
    klass = LRG_FPS_TEMPLATE_GET_CLASS (self);
    priv = lrg_fps_template_get_instance_private (self);

    /* Read movement input */
    priv->move_x = 0.0f;
    priv->move_z = 0.0f;

    if (IsKeyDown (KEY_W))
        priv->move_z = 1.0f;
    if (IsKeyDown (KEY_S))
        priv->move_z = -1.0f;
    if (IsKeyDown (KEY_D))
        priv->move_x = 1.0f;
    if (IsKeyDown (KEY_A))
        priv->move_x = -1.0f;

    /* Gamepad movement */
    if (IsGamepadAvailable (0))
    {
        gfloat gp_x;
        gfloat gp_y;

        gp_x = GetGamepadAxisMovement (0, GAMEPAD_AXIS_LEFT_X);
        gp_y = GetGamepadAxisMovement (0, GAMEPAD_AXIS_LEFT_Y);

        if (fabsf (gp_x) > 0.15f)
            priv->move_x = gp_x;
        if (fabsf (gp_y) > 0.15f)
            priv->move_z = -gp_y;
    }

    /* Normalize diagonal movement */
    {
        gfloat len;

        len = sqrtf (priv->move_x * priv->move_x + priv->move_z * priv->move_z);
        if (len > 1.0f)
        {
            priv->move_x /= len;
            priv->move_z /= len;
        }
    }

    /* Other inputs */
    priv->jump_pressed = IsKeyPressed (KEY_SPACE) ||
                         (IsGamepadAvailable (0) && IsGamepadButtonPressed (0, GAMEPAD_BUTTON_RIGHT_FACE_DOWN));
    priv->sprint_held = IsKeyDown (KEY_LEFT_SHIFT) ||
                        (IsGamepadAvailable (0) && IsGamepadButtonDown (0, GAMEPAD_BUTTON_LEFT_TRIGGER_1));
    priv->crouch_held = IsKeyDown (KEY_LEFT_CONTROL) || IsKeyDown (KEY_C);

    priv->fire_pressed = IsMouseButtonDown (MOUSE_BUTTON_LEFT) ||
                         (IsGamepadAvailable (0) && GetGamepadAxisMovement (0, GAMEPAD_AXIS_RIGHT_TRIGGER) > 0.5f);
    priv->fire_secondary = IsMouseButtonDown (MOUSE_BUTTON_RIGHT) ||
                           (IsGamepadAvailable (0) && GetGamepadAxisMovement (0, GAMEPAD_AXIS_LEFT_TRIGGER) > 0.5f);
    priv->reload_pressed = IsKeyPressed (KEY_R) ||
                           (IsGamepadAvailable (0) && IsGamepadButtonPressed (0, GAMEPAD_BUTTON_RIGHT_FACE_LEFT));

    /* Handle sprinting */
    priv->is_sprinting = priv->sprint_held && priv->move_z > 0.0f;

    /* Handle crouching */
    if (priv->crouch_held && priv->posture == LRG_FPS_POSTURE_STANDING)
    {
        LrgFPSPosture old_posture;

        old_posture = priv->posture;
        priv->posture = LRG_FPS_POSTURE_CROUCHING;

        if (klass->on_posture_changed != NULL)
            klass->on_posture_changed (self, old_posture, priv->posture);
    }
    else if (!priv->crouch_held && priv->posture == LRG_FPS_POSTURE_CROUCHING)
    {
        LrgFPSPosture old_posture;

        old_posture = priv->posture;
        priv->posture = LRG_FPS_POSTURE_STANDING;

        if (klass->on_posture_changed != NULL)
            klass->on_posture_changed (self, old_posture, priv->posture);
    }

    /* Weapon number keys */
    if (IsKeyPressed (KEY_ONE))
        lrg_fps_template_set_current_weapon (self, 0);
    else if (IsKeyPressed (KEY_TWO))
        lrg_fps_template_set_current_weapon (self, 1);
    else if (IsKeyPressed (KEY_THREE))
        lrg_fps_template_set_current_weapon (self, 2);

    /* Mouse wheel weapon switch */
    {
        gfloat wheel;

        wheel = GetMouseWheelMove ();
        if (wheel > 0.0f)
            lrg_fps_template_set_current_weapon (self, priv->current_weapon + 1);
        else if (wheel < 0.0f)
            lrg_fps_template_set_current_weapon (self, priv->current_weapon - 1);
    }

    /* Update reload timer */
    if (priv->is_reloading)
    {
        priv->reload_timer -= (gfloat)delta;
        if (priv->reload_timer <= 0.0f)
        {
            priv->is_reloading = FALSE;
            priv->ammo = 30;  /* Default mag size */
        }
    }

    /* Handle firing */
    if (priv->fire_pressed && !priv->is_reloading)
    {
        if (klass->on_fire != NULL)
            klass->on_fire (self, TRUE);
    }

    if (priv->fire_secondary && !priv->is_reloading)
    {
        if (klass->on_fire != NULL)
            klass->on_fire (self, FALSE);
    }

    /* Handle reload */
    if (priv->reload_pressed && !priv->is_reloading)
    {
        if (klass->on_reload != NULL)
            klass->on_reload (self);
    }

    /* Update movement */
    if (klass->update_movement != NULL)
        klass->update_movement (self, delta);

    /* Chain up */
    LRG_GAME_TEMPLATE_CLASS (lrg_fps_template_parent_class)->pre_update (template, delta);
}

static void
lrg_fps_template_draw_ui (LrgGame3DTemplate *template)
{
    LrgFPSTemplate *self;
    LrgFPSTemplateClass *klass;

    self = LRG_FPS_TEMPLATE (template);
    klass = LRG_FPS_TEMPLATE_GET_CLASS (self);

    /* Draw weapon */
    if (klass->draw_weapon != NULL)
        klass->draw_weapon (self);

    /* Draw crosshair */
    if (klass->draw_crosshair != NULL)
        klass->draw_crosshair (self);

    /* Draw HUD */
    if (klass->draw_hud != NULL)
        klass->draw_hud (self);

    /* Chain up */
    LRG_GAME_3D_TEMPLATE_CLASS (lrg_fps_template_parent_class)->draw_ui (template);
}

/* ==========================================================================
 * GObject Implementation
 * ========================================================================== */

static void
lrg_fps_template_set_property (GObject      *object,
                               guint         prop_id,
                               const GValue *value,
                               GParamSpec   *pspec)
{
    LrgFPSTemplate *self = LRG_FPS_TEMPLATE (object);

    switch (prop_id)
    {
        case PROP_WALK_SPEED:
            lrg_fps_template_set_walk_speed (self, g_value_get_float (value));
            break;
        case PROP_SPRINT_MULTIPLIER:
            lrg_fps_template_set_sprint_multiplier (self, g_value_get_float (value));
            break;
        case PROP_CROUCH_MULTIPLIER:
            lrg_fps_template_set_crouch_multiplier (self, g_value_get_float (value));
            break;
        case PROP_JUMP_HEIGHT:
            lrg_fps_template_set_jump_height (self, g_value_get_float (value));
            break;
        case PROP_GRAVITY:
            lrg_fps_template_set_gravity (self, g_value_get_float (value));
            break;
        case PROP_STANDING_HEIGHT:
            lrg_fps_template_set_standing_height (self, g_value_get_float (value));
            break;
        case PROP_CROUCH_HEIGHT:
            lrg_fps_template_set_crouch_height (self, g_value_get_float (value));
            break;
        case PROP_HEALTH:
            lrg_fps_template_set_health (self, g_value_get_float (value));
            break;
        case PROP_MAX_HEALTH:
            lrg_fps_template_set_max_health (self, g_value_get_float (value));
            break;
        case PROP_ARMOR:
            lrg_fps_template_set_armor (self, g_value_get_float (value));
            break;
        case PROP_HEAD_BOB_ENABLED:
            lrg_fps_template_set_head_bob_enabled (self, g_value_get_boolean (value));
            break;
        case PROP_HEAD_BOB_INTENSITY:
            lrg_fps_template_set_head_bob_intensity (self, g_value_get_float (value));
            break;
        case PROP_CROSSHAIR_VISIBLE:
            lrg_fps_template_set_crosshair_visible (self, g_value_get_boolean (value));
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_fps_template_get_property (GObject    *object,
                               guint       prop_id,
                               GValue     *value,
                               GParamSpec *pspec)
{
    LrgFPSTemplate *self = LRG_FPS_TEMPLATE (object);
    LrgFPSTemplatePrivate *priv = lrg_fps_template_get_instance_private (self);

    switch (prop_id)
    {
        case PROP_WALK_SPEED:
            g_value_set_float (value, priv->walk_speed);
            break;
        case PROP_SPRINT_MULTIPLIER:
            g_value_set_float (value, priv->sprint_multiplier);
            break;
        case PROP_CROUCH_MULTIPLIER:
            g_value_set_float (value, priv->crouch_multiplier);
            break;
        case PROP_JUMP_HEIGHT:
            g_value_set_float (value, priv->jump_height);
            break;
        case PROP_GRAVITY:
            g_value_set_float (value, priv->gravity);
            break;
        case PROP_STANDING_HEIGHT:
            g_value_set_float (value, priv->standing_height);
            break;
        case PROP_CROUCH_HEIGHT:
            g_value_set_float (value, priv->crouch_height);
            break;
        case PROP_HEALTH:
            g_value_set_float (value, priv->health);
            break;
        case PROP_MAX_HEALTH:
            g_value_set_float (value, priv->max_health);
            break;
        case PROP_ARMOR:
            g_value_set_float (value, priv->armor);
            break;
        case PROP_HEAD_BOB_ENABLED:
            g_value_set_boolean (value, priv->head_bob_enabled);
            break;
        case PROP_HEAD_BOB_INTENSITY:
            g_value_set_float (value, priv->head_bob_intensity);
            break;
        case PROP_CROSSHAIR_VISIBLE:
            g_value_set_boolean (value, priv->crosshair_visible);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_fps_template_constructed (GObject *object)
{
    LrgFPSTemplate *self = LRG_FPS_TEMPLATE (object);

    G_OBJECT_CLASS (lrg_fps_template_parent_class)->constructed (object);

    /* Enable mouse look by default for FPS */
    lrg_game_3d_template_set_mouse_look_enabled (LRG_GAME_3D_TEMPLATE (self), TRUE);
}

static void
lrg_fps_template_class_init (LrgFPSTemplateClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    LrgGameTemplateClass *template_class = LRG_GAME_TEMPLATE_CLASS (klass);
    LrgGame3DTemplateClass *template_3d_class = LRG_GAME_3D_TEMPLATE_CLASS (klass);

    object_class->set_property = lrg_fps_template_set_property;
    object_class->get_property = lrg_fps_template_get_property;
    object_class->constructed = lrg_fps_template_constructed;

    /* Override parent virtuals */
    template_class->pre_update = lrg_fps_template_pre_update;
    template_3d_class->draw_ui = lrg_fps_template_draw_ui;

    /* Set up class virtuals */
    klass->on_fire = lrg_fps_template_real_on_fire;
    klass->on_reload = lrg_fps_template_real_on_reload;
    klass->on_weapon_switch = lrg_fps_template_real_on_weapon_switch;
    klass->on_jump = lrg_fps_template_real_on_jump;
    klass->on_land = lrg_fps_template_real_on_land;
    klass->on_damage = lrg_fps_template_real_on_damage;
    klass->on_death = lrg_fps_template_real_on_death;
    klass->on_posture_changed = lrg_fps_template_real_on_posture_changed;
    klass->update_movement = lrg_fps_template_real_update_movement;
    klass->check_ground = lrg_fps_template_real_check_ground;
    klass->draw_weapon = lrg_fps_template_real_draw_weapon;
    klass->draw_crosshair = lrg_fps_template_real_draw_crosshair;
    klass->draw_hud = lrg_fps_template_real_draw_hud;

    /* Properties */
    properties[PROP_WALK_SPEED] =
        g_param_spec_float ("walk-speed", NULL, NULL,
                            0.1f, 100.0f, LRG_FPS_DEFAULT_WALK_SPEED,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_SPRINT_MULTIPLIER] =
        g_param_spec_float ("sprint-multiplier", NULL, NULL,
                            1.0f, 5.0f, LRG_FPS_DEFAULT_SPRINT_MULT,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_CROUCH_MULTIPLIER] =
        g_param_spec_float ("crouch-multiplier", NULL, NULL,
                            0.1f, 1.0f, LRG_FPS_DEFAULT_CROUCH_MULT,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_JUMP_HEIGHT] =
        g_param_spec_float ("jump-height", NULL, NULL,
                            0.1f, 10.0f, LRG_FPS_DEFAULT_JUMP_HEIGHT,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_GRAVITY] =
        g_param_spec_float ("gravity", NULL, NULL,
                            0.0f, 100.0f, LRG_FPS_DEFAULT_GRAVITY,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_STANDING_HEIGHT] =
        g_param_spec_float ("standing-height", NULL, NULL,
                            0.5f, 5.0f, LRG_FPS_DEFAULT_STANDING_HEIGHT,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_CROUCH_HEIGHT] =
        g_param_spec_float ("crouch-height", NULL, NULL,
                            0.3f, 3.0f, LRG_FPS_DEFAULT_CROUCH_HEIGHT,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_HEALTH] =
        g_param_spec_float ("health", NULL, NULL,
                            0.0f, G_MAXFLOAT, LRG_FPS_DEFAULT_MAX_HEALTH,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_MAX_HEALTH] =
        g_param_spec_float ("max-health", NULL, NULL,
                            1.0f, G_MAXFLOAT, LRG_FPS_DEFAULT_MAX_HEALTH,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_ARMOR] =
        g_param_spec_float ("armor", NULL, NULL,
                            0.0f, G_MAXFLOAT, 0.0f,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_HEAD_BOB_ENABLED] =
        g_param_spec_boolean ("head-bob-enabled", NULL, NULL,
                              TRUE,
                              G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_HEAD_BOB_INTENSITY] =
        g_param_spec_float ("head-bob-intensity", NULL, NULL,
                            0.0f, 1.0f, 0.5f,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_CROSSHAIR_VISIBLE] =
        g_param_spec_boolean ("crosshair-visible", NULL, NULL,
                              TRUE,
                              G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);

    /* Signals */
    signals[SIGNAL_FIRED] =
        g_signal_new ("fired",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0, NULL, NULL, NULL,
                      G_TYPE_NONE, 1, G_TYPE_BOOLEAN);

    signals[SIGNAL_RELOADED] =
        g_signal_new ("reloaded",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0, NULL, NULL, NULL,
                      G_TYPE_NONE, 0);

    signals[SIGNAL_WEAPON_SWITCHED] =
        g_signal_new ("weapon-switched",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0, NULL, NULL, NULL,
                      G_TYPE_NONE, 1, G_TYPE_INT);

    signals[SIGNAL_JUMPED] =
        g_signal_new ("jumped",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0, NULL, NULL, NULL,
                      G_TYPE_NONE, 0);

    signals[SIGNAL_LANDED] =
        g_signal_new ("landed",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0, NULL, NULL, NULL,
                      G_TYPE_NONE, 1, G_TYPE_FLOAT);

    signals[SIGNAL_DAMAGED] =
        g_signal_new ("damaged",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0, NULL, NULL, NULL,
                      G_TYPE_NONE, 4, G_TYPE_FLOAT, G_TYPE_FLOAT, G_TYPE_FLOAT, G_TYPE_FLOAT);

    signals[SIGNAL_DIED] =
        g_signal_new ("died",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0, NULL, NULL, NULL,
                      G_TYPE_NONE, 0);

    signals[SIGNAL_POSTURE_CHANGED] =
        g_signal_new ("posture-changed",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0, NULL, NULL, NULL,
                      G_TYPE_NONE, 2, G_TYPE_INT, G_TYPE_INT);
}

static void
lrg_fps_template_init (LrgFPSTemplate *self)
{
    LrgFPSTemplatePrivate *priv;

    priv = lrg_fps_template_get_instance_private (self);

    priv->pos_x = 0.0f;
    priv->pos_y = 0.0f;
    priv->pos_z = 0.0f;
    priv->vel_y = 0.0f;

    priv->walk_speed = LRG_FPS_DEFAULT_WALK_SPEED;
    priv->sprint_multiplier = LRG_FPS_DEFAULT_SPRINT_MULT;
    priv->crouch_multiplier = LRG_FPS_DEFAULT_CROUCH_MULT;
    priv->jump_height = LRG_FPS_DEFAULT_JUMP_HEIGHT;
    priv->gravity = LRG_FPS_DEFAULT_GRAVITY;

    priv->standing_height = LRG_FPS_DEFAULT_STANDING_HEIGHT;
    priv->crouch_height = LRG_FPS_DEFAULT_CROUCH_HEIGHT;
    priv->current_height = LRG_FPS_DEFAULT_STANDING_HEIGHT;
    priv->target_height = LRG_FPS_DEFAULT_STANDING_HEIGHT;

    priv->posture = LRG_FPS_POSTURE_STANDING;
    priv->is_sprinting = FALSE;
    priv->on_ground = TRUE;
    priv->is_dead = FALSE;

    priv->health = LRG_FPS_DEFAULT_MAX_HEALTH;
    priv->max_health = LRG_FPS_DEFAULT_MAX_HEALTH;
    priv->armor = 0.0f;
    priv->max_armor = LRG_FPS_DEFAULT_MAX_ARMOR;

    priv->current_weapon = 0;
    priv->ammo = 30;
    priv->is_reloading = FALSE;

    priv->head_bob_enabled = TRUE;
    priv->head_bob_intensity = 0.5f;
    priv->head_bob_timer = 0.0f;

    priv->crosshair_visible = TRUE;
}

/* ==========================================================================
 * Public API Implementation (abbreviated - key functions)
 * ========================================================================== */

LrgFPSTemplate *
lrg_fps_template_new (void)
{
    return g_object_new (LRG_TYPE_FPS_TEMPLATE, NULL);
}

void
lrg_fps_template_get_position (LrgFPSTemplate *self,
                               gfloat         *x,
                               gfloat         *y,
                               gfloat         *z)
{
    LrgFPSTemplatePrivate *priv;

    g_return_if_fail (LRG_IS_FPS_TEMPLATE (self));

    priv = lrg_fps_template_get_instance_private (self);

    if (x != NULL) *x = priv->pos_x;
    if (y != NULL) *y = priv->pos_y;
    if (z != NULL) *z = priv->pos_z;
}

void
lrg_fps_template_set_position (LrgFPSTemplate *self,
                               gfloat          x,
                               gfloat          y,
                               gfloat          z)
{
    LrgFPSTemplatePrivate *priv;

    g_return_if_fail (LRG_IS_FPS_TEMPLATE (self));

    priv = lrg_fps_template_get_instance_private (self);
    priv->pos_x = x;
    priv->pos_y = y;
    priv->pos_z = z;
}

gfloat lrg_fps_template_get_walk_speed (LrgFPSTemplate *self)
{
    LrgFPSTemplatePrivate *priv;
    g_return_val_if_fail (LRG_IS_FPS_TEMPLATE (self), LRG_FPS_DEFAULT_WALK_SPEED);
    priv = lrg_fps_template_get_instance_private (self);
    return priv->walk_speed;
}

void lrg_fps_template_set_walk_speed (LrgFPSTemplate *self, gfloat speed)
{
    LrgFPSTemplatePrivate *priv;
    g_return_if_fail (LRG_IS_FPS_TEMPLATE (self));
    priv = lrg_fps_template_get_instance_private (self);
    priv->walk_speed = MAX (speed, 0.1f);
}

gfloat lrg_fps_template_get_sprint_multiplier (LrgFPSTemplate *self)
{
    LrgFPSTemplatePrivate *priv;
    g_return_val_if_fail (LRG_IS_FPS_TEMPLATE (self), LRG_FPS_DEFAULT_SPRINT_MULT);
    priv = lrg_fps_template_get_instance_private (self);
    return priv->sprint_multiplier;
}

void lrg_fps_template_set_sprint_multiplier (LrgFPSTemplate *self, gfloat multiplier)
{
    LrgFPSTemplatePrivate *priv;
    g_return_if_fail (LRG_IS_FPS_TEMPLATE (self));
    priv = lrg_fps_template_get_instance_private (self);
    priv->sprint_multiplier = CLAMP (multiplier, 1.0f, 5.0f);
}

gfloat lrg_fps_template_get_crouch_multiplier (LrgFPSTemplate *self)
{
    LrgFPSTemplatePrivate *priv;
    g_return_val_if_fail (LRG_IS_FPS_TEMPLATE (self), LRG_FPS_DEFAULT_CROUCH_MULT);
    priv = lrg_fps_template_get_instance_private (self);
    return priv->crouch_multiplier;
}

void lrg_fps_template_set_crouch_multiplier (LrgFPSTemplate *self, gfloat multiplier)
{
    LrgFPSTemplatePrivate *priv;
    g_return_if_fail (LRG_IS_FPS_TEMPLATE (self));
    priv = lrg_fps_template_get_instance_private (self);
    priv->crouch_multiplier = CLAMP (multiplier, 0.1f, 1.0f);
}

gfloat lrg_fps_template_get_jump_height (LrgFPSTemplate *self)
{
    LrgFPSTemplatePrivate *priv;
    g_return_val_if_fail (LRG_IS_FPS_TEMPLATE (self), LRG_FPS_DEFAULT_JUMP_HEIGHT);
    priv = lrg_fps_template_get_instance_private (self);
    return priv->jump_height;
}

void lrg_fps_template_set_jump_height (LrgFPSTemplate *self, gfloat height)
{
    LrgFPSTemplatePrivate *priv;
    g_return_if_fail (LRG_IS_FPS_TEMPLATE (self));
    priv = lrg_fps_template_get_instance_private (self);
    priv->jump_height = MAX (height, 0.1f);
}

gfloat lrg_fps_template_get_gravity (LrgFPSTemplate *self)
{
    LrgFPSTemplatePrivate *priv;
    g_return_val_if_fail (LRG_IS_FPS_TEMPLATE (self), LRG_FPS_DEFAULT_GRAVITY);
    priv = lrg_fps_template_get_instance_private (self);
    return priv->gravity;
}

void lrg_fps_template_set_gravity (LrgFPSTemplate *self, gfloat gravity)
{
    LrgFPSTemplatePrivate *priv;
    g_return_if_fail (LRG_IS_FPS_TEMPLATE (self));
    priv = lrg_fps_template_get_instance_private (self);
    priv->gravity = MAX (gravity, 0.0f);
}

LrgFPSPosture lrg_fps_template_get_posture (LrgFPSTemplate *self)
{
    LrgFPSTemplatePrivate *priv;
    g_return_val_if_fail (LRG_IS_FPS_TEMPLATE (self), LRG_FPS_POSTURE_STANDING);
    priv = lrg_fps_template_get_instance_private (self);
    return priv->posture;
}

void lrg_fps_template_set_posture (LrgFPSTemplate *self, LrgFPSPosture posture)
{
    LrgFPSTemplateClass *klass;
    LrgFPSTemplatePrivate *priv;
    LrgFPSPosture old_posture;

    g_return_if_fail (LRG_IS_FPS_TEMPLATE (self));

    priv = lrg_fps_template_get_instance_private (self);
    klass = LRG_FPS_TEMPLATE_GET_CLASS (self);

    if (priv->posture == posture)
        return;

    old_posture = priv->posture;
    priv->posture = posture;

    if (klass->on_posture_changed != NULL)
        klass->on_posture_changed (self, old_posture, posture);
}

gboolean lrg_fps_template_is_sprinting (LrgFPSTemplate *self)
{
    LrgFPSTemplatePrivate *priv;
    g_return_val_if_fail (LRG_IS_FPS_TEMPLATE (self), FALSE);
    priv = lrg_fps_template_get_instance_private (self);
    return priv->is_sprinting;
}

gboolean lrg_fps_template_is_on_ground (LrgFPSTemplate *self)
{
    LrgFPSTemplatePrivate *priv;
    g_return_val_if_fail (LRG_IS_FPS_TEMPLATE (self), TRUE);
    priv = lrg_fps_template_get_instance_private (self);
    return priv->on_ground;
}

gfloat lrg_fps_template_get_standing_height (LrgFPSTemplate *self)
{
    LrgFPSTemplatePrivate *priv;
    g_return_val_if_fail (LRG_IS_FPS_TEMPLATE (self), LRG_FPS_DEFAULT_STANDING_HEIGHT);
    priv = lrg_fps_template_get_instance_private (self);
    return priv->standing_height;
}

void lrg_fps_template_set_standing_height (LrgFPSTemplate *self, gfloat height)
{
    LrgFPSTemplatePrivate *priv;
    g_return_if_fail (LRG_IS_FPS_TEMPLATE (self));
    priv = lrg_fps_template_get_instance_private (self);
    priv->standing_height = MAX (height, 0.5f);
}

gfloat lrg_fps_template_get_crouch_height (LrgFPSTemplate *self)
{
    LrgFPSTemplatePrivate *priv;
    g_return_val_if_fail (LRG_IS_FPS_TEMPLATE (self), LRG_FPS_DEFAULT_CROUCH_HEIGHT);
    priv = lrg_fps_template_get_instance_private (self);
    return priv->crouch_height;
}

void lrg_fps_template_set_crouch_height (LrgFPSTemplate *self, gfloat height)
{
    LrgFPSTemplatePrivate *priv;
    g_return_if_fail (LRG_IS_FPS_TEMPLATE (self));
    priv = lrg_fps_template_get_instance_private (self);
    priv->crouch_height = MAX (height, 0.3f);
}

gfloat lrg_fps_template_get_health (LrgFPSTemplate *self)
{
    LrgFPSTemplatePrivate *priv;
    g_return_val_if_fail (LRG_IS_FPS_TEMPLATE (self), 0.0f);
    priv = lrg_fps_template_get_instance_private (self);
    return priv->health;
}

void lrg_fps_template_set_health (LrgFPSTemplate *self, gfloat health)
{
    LrgFPSTemplatePrivate *priv;
    g_return_if_fail (LRG_IS_FPS_TEMPLATE (self));
    priv = lrg_fps_template_get_instance_private (self);
    priv->health = CLAMP (health, 0.0f, priv->max_health);
}

gfloat lrg_fps_template_get_max_health (LrgFPSTemplate *self)
{
    LrgFPSTemplatePrivate *priv;
    g_return_val_if_fail (LRG_IS_FPS_TEMPLATE (self), LRG_FPS_DEFAULT_MAX_HEALTH);
    priv = lrg_fps_template_get_instance_private (self);
    return priv->max_health;
}

void lrg_fps_template_set_max_health (LrgFPSTemplate *self, gfloat max)
{
    LrgFPSTemplatePrivate *priv;
    g_return_if_fail (LRG_IS_FPS_TEMPLATE (self));
    priv = lrg_fps_template_get_instance_private (self);
    priv->max_health = MAX (max, 1.0f);
}

gfloat lrg_fps_template_get_armor (LrgFPSTemplate *self)
{
    LrgFPSTemplatePrivate *priv;
    g_return_val_if_fail (LRG_IS_FPS_TEMPLATE (self), 0.0f);
    priv = lrg_fps_template_get_instance_private (self);
    return priv->armor;
}

void lrg_fps_template_set_armor (LrgFPSTemplate *self, gfloat armor)
{
    LrgFPSTemplatePrivate *priv;
    g_return_if_fail (LRG_IS_FPS_TEMPLATE (self));
    priv = lrg_fps_template_get_instance_private (self);
    priv->armor = CLAMP (armor, 0.0f, priv->max_armor);
}

void lrg_fps_template_apply_damage (LrgFPSTemplate *self, gfloat damage,
                                    gfloat source_x, gfloat source_y, gfloat source_z)
{
    LrgFPSTemplateClass *klass;
    LrgFPSTemplatePrivate *priv;
    gfloat absorbed;

    g_return_if_fail (LRG_IS_FPS_TEMPLATE (self));

    priv = lrg_fps_template_get_instance_private (self);
    klass = LRG_FPS_TEMPLATE_GET_CLASS (self);

    if (priv->is_dead)
        return;

    /* Armor absorbs 50% of damage */
    if (priv->armor > 0.0f)
    {
        absorbed = damage * 0.5f;
        if (absorbed > priv->armor)
            absorbed = priv->armor;

        priv->armor -= absorbed;
        damage -= absorbed;
    }

    priv->health -= damage;

    if (klass->on_damage != NULL)
        klass->on_damage (self, damage, source_x, source_y, source_z);

    if (priv->health <= 0.0f)
    {
        priv->health = 0.0f;

        if (klass->on_death != NULL)
            klass->on_death (self);
    }
}

gboolean lrg_fps_template_is_dead (LrgFPSTemplate *self)
{
    LrgFPSTemplatePrivate *priv;
    g_return_val_if_fail (LRG_IS_FPS_TEMPLATE (self), FALSE);
    priv = lrg_fps_template_get_instance_private (self);
    return priv->is_dead;
}

gint lrg_fps_template_get_current_weapon (LrgFPSTemplate *self)
{
    LrgFPSTemplatePrivate *priv;
    g_return_val_if_fail (LRG_IS_FPS_TEMPLATE (self), 0);
    priv = lrg_fps_template_get_instance_private (self);
    return priv->current_weapon;
}

void lrg_fps_template_set_current_weapon (LrgFPSTemplate *self, gint weapon_index)
{
    LrgFPSTemplateClass *klass;
    g_return_if_fail (LRG_IS_FPS_TEMPLATE (self));

    klass = LRG_FPS_TEMPLATE_GET_CLASS (self);

    if (klass->on_weapon_switch != NULL)
        klass->on_weapon_switch (self, weapon_index);
}

gint lrg_fps_template_get_ammo (LrgFPSTemplate *self)
{
    LrgFPSTemplatePrivate *priv;
    g_return_val_if_fail (LRG_IS_FPS_TEMPLATE (self), 0);
    priv = lrg_fps_template_get_instance_private (self);
    return priv->ammo;
}

void lrg_fps_template_set_ammo (LrgFPSTemplate *self, gint ammo)
{
    LrgFPSTemplatePrivate *priv;
    g_return_if_fail (LRG_IS_FPS_TEMPLATE (self));
    priv = lrg_fps_template_get_instance_private (self);
    priv->ammo = MAX (ammo, 0);
}

gboolean lrg_fps_template_is_reloading (LrgFPSTemplate *self)
{
    LrgFPSTemplatePrivate *priv;
    g_return_val_if_fail (LRG_IS_FPS_TEMPLATE (self), FALSE);
    priv = lrg_fps_template_get_instance_private (self);
    return priv->is_reloading;
}

gboolean lrg_fps_template_get_head_bob_enabled (LrgFPSTemplate *self)
{
    LrgFPSTemplatePrivate *priv;
    g_return_val_if_fail (LRG_IS_FPS_TEMPLATE (self), TRUE);
    priv = lrg_fps_template_get_instance_private (self);
    return priv->head_bob_enabled;
}

void lrg_fps_template_set_head_bob_enabled (LrgFPSTemplate *self, gboolean enabled)
{
    LrgFPSTemplatePrivate *priv;
    g_return_if_fail (LRG_IS_FPS_TEMPLATE (self));
    priv = lrg_fps_template_get_instance_private (self);
    priv->head_bob_enabled = enabled;
}

gfloat lrg_fps_template_get_head_bob_intensity (LrgFPSTemplate *self)
{
    LrgFPSTemplatePrivate *priv;
    g_return_val_if_fail (LRG_IS_FPS_TEMPLATE (self), 0.5f);
    priv = lrg_fps_template_get_instance_private (self);
    return priv->head_bob_intensity;
}

void lrg_fps_template_set_head_bob_intensity (LrgFPSTemplate *self, gfloat intensity)
{
    LrgFPSTemplatePrivate *priv;
    g_return_if_fail (LRG_IS_FPS_TEMPLATE (self));
    priv = lrg_fps_template_get_instance_private (self);
    priv->head_bob_intensity = CLAMP (intensity, 0.0f, 1.0f);
}

gboolean lrg_fps_template_get_crosshair_visible (LrgFPSTemplate *self)
{
    LrgFPSTemplatePrivate *priv;
    g_return_val_if_fail (LRG_IS_FPS_TEMPLATE (self), TRUE);
    priv = lrg_fps_template_get_instance_private (self);
    return priv->crosshair_visible;
}

void lrg_fps_template_set_crosshair_visible (LrgFPSTemplate *self, gboolean visible)
{
    LrgFPSTemplatePrivate *priv;
    g_return_if_fail (LRG_IS_FPS_TEMPLATE (self));
    priv = lrg_fps_template_get_instance_private (self);
    priv->crosshair_visible = visible;
}
