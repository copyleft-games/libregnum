/* lrg-game-3d-template.c - 3D game template implementation
 *
 * Copyright 2025 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * This file is part of Libregnum.
 */

#include "../config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_TEMPLATE

#include "lrg-game-3d-template.h"
#include "lrg-game-3d-template-private.h"
#include "../lrg-log.h"
#include "../graphics/lrg-window.h"

#include <graylib.h>
#include <raylib.h>
#include <math.h>

/* ==========================================================================
 * Property IDs
 * ========================================================================== */

enum
{
    PROP_0,

    PROP_CAMERA,
    PROP_FOV,
    PROP_NEAR_CLIP,
    PROP_FAR_CLIP,
    PROP_MOUSE_LOOK_ENABLED,
    PROP_MOUSE_SENSITIVITY,
    PROP_INVERT_Y,
    PROP_YAW,
    PROP_PITCH,

    N_PROPS
};

static GParamSpec *properties[N_PROPS];

/* ==========================================================================
 * Type Definition
 * ========================================================================== */

G_DEFINE_TYPE_WITH_PRIVATE (LrgGame3DTemplate, lrg_game_3d_template,
                            LRG_TYPE_GAME_TEMPLATE)

/* ==========================================================================
 * Private Implementation
 * ========================================================================== */

LrgGame3DTemplatePrivate *
lrg_game_3d_template_get_private (LrgGame3DTemplate *self)
{
    return lrg_game_3d_template_get_instance_private (self);
}

void
lrg_game_3d_template_update_camera_orientation (LrgGame3DTemplate *self)
{
    LrgGame3DTemplatePrivate *priv;
    gfloat yaw_rad;
    gfloat pitch_rad;
    gfloat cos_pitch;
    gfloat forward_x;
    gfloat forward_y;
    gfloat forward_z;
    gfloat target_x;
    gfloat target_y;
    gfloat target_z;

    priv = lrg_game_3d_template_get_instance_private (self);

    if (priv->camera == NULL)
        return;

    /* Convert to radians */
    yaw_rad = priv->yaw * (gfloat)(G_PI / 180.0);
    pitch_rad = priv->pitch * (gfloat)(G_PI / 180.0);

    /*
     * Calculate forward vector from yaw/pitch.
     * Yaw rotates around Y-axis, pitch rotates around X-axis.
     */
    cos_pitch = cosf (pitch_rad);
    forward_x = cos_pitch * sinf (yaw_rad);
    forward_y = sinf (pitch_rad);
    forward_z = cos_pitch * cosf (yaw_rad);

    /* Target is position + forward direction */
    target_x = priv->position_x + forward_x;
    target_y = priv->position_y + forward_y;
    target_z = priv->position_z + forward_z;

    lrg_camera3d_set_target_xyz (priv->camera, target_x, target_y, target_z);
}

static void
template_sync_position_from_camera (LrgGame3DTemplate *self)
{
    LrgGame3DTemplatePrivate *priv;
    g_autoptr(GrlVector3) pos = NULL;

    priv = lrg_game_3d_template_get_instance_private (self);

    if (priv->camera == NULL)
        return;

    pos = lrg_camera3d_get_position (priv->camera);
    if (pos != NULL)
    {
        priv->position_x = pos->x;
        priv->position_y = pos->y;
        priv->position_z = pos->z;
    }
}

static void
template_sync_camera_position (LrgGame3DTemplate *self)
{
    LrgGame3DTemplatePrivate *priv;

    priv = lrg_game_3d_template_get_instance_private (self);

    if (priv->camera == NULL)
        return;

    lrg_camera3d_set_position_xyz (priv->camera,
                                   priv->position_x,
                                   priv->position_y,
                                   priv->position_z);
}

/* ==========================================================================
 * Virtual Method Implementations
 * ========================================================================== */

static void
lrg_game_3d_template_real_draw_skybox (LrgGame3DTemplate *self)
{
    /* Default: no skybox */
}

static void
lrg_game_3d_template_real_draw_world (LrgGame3DTemplate *self)
{
    /* Default: draw a simple grid for reference */
    grl_draw_grid (20, 1.0f);
}

static void
lrg_game_3d_template_real_draw_effects (LrgGame3DTemplate *self)
{
    /* Default: no effects */
}

static void
lrg_game_3d_template_real_draw_ui (LrgGame3DTemplate *self)
{
    /* Default: no UI */
}

static void
lrg_game_3d_template_real_update_camera (LrgGame3DTemplate *self,
                                          gdouble            delta)
{
    /* Default: no automatic camera updates */
}

static void
lrg_game_3d_template_real_on_mouse_look (LrgGame3DTemplate *self,
                                          gfloat             delta_x,
                                          gfloat             delta_y)
{
    LrgGame3DTemplatePrivate *priv;
    gfloat new_yaw;
    gfloat new_pitch;

    priv = lrg_game_3d_template_get_instance_private (self);

    /* Apply sensitivity */
    delta_x *= priv->mouse_sensitivity;
    delta_y *= priv->mouse_sensitivity;

    /* Invert Y if requested */
    if (priv->invert_y)
        delta_y = -delta_y;

    /* Update yaw (horizontal) */
    new_yaw = priv->yaw + delta_x;

    /* Wrap yaw to 0-360 range */
    while (new_yaw < 0.0f)
        new_yaw += 360.0f;
    while (new_yaw >= 360.0f)
        new_yaw -= 360.0f;

    priv->yaw = new_yaw;

    /* Update pitch (vertical) with limits */
    new_pitch = priv->pitch - delta_y;  /* Subtract because screen Y is inverted */
    new_pitch = CLAMP (new_pitch, priv->min_pitch, priv->max_pitch);
    priv->pitch = new_pitch;

    /* Update camera orientation */
    lrg_game_3d_template_update_camera_orientation (self);
}

/* ==========================================================================
 * Overridden Parent Virtual Methods
 * ========================================================================== */

static void
lrg_game_3d_template_pre_startup (LrgGameTemplate *self)
{
    LrgGame3DTemplatePrivate *priv;
    LrgGameTemplateClass *parent_class;

    /* Chain up first */
    parent_class = LRG_GAME_TEMPLATE_CLASS (lrg_game_3d_template_parent_class);
    if (parent_class->pre_startup != NULL)
        parent_class->pre_startup (self);

    priv = lrg_game_3d_template_get_instance_private (LRG_GAME_3D_TEMPLATE (self));

    /* Create camera if needed */
    if (priv->camera == NULL)
    {
        priv->camera = lrg_camera3d_new ();
        priv->camera_owned = TRUE;

        /* Set initial position */
        lrg_camera3d_set_position_xyz (priv->camera,
                                       priv->position_x,
                                       priv->position_y,
                                       priv->position_z);

        /* Set FOV */
        lrg_camera3d_set_fovy (priv->camera, priv->fov);

        /* Update orientation to match yaw/pitch */
        lrg_game_3d_template_update_camera_orientation (LRG_GAME_3D_TEMPLATE (self));
    }
}

static void
lrg_game_3d_template_pre_update (LrgGameTemplate *self,
                                  gdouble          delta)
{
    LrgGame3DTemplate *template;
    LrgGame3DTemplateClass *klass;
    LrgGame3DTemplatePrivate *priv;
    LrgGameTemplateClass *parent_class;

    template = LRG_GAME_3D_TEMPLATE (self);
    klass = LRG_GAME_3D_TEMPLATE_GET_CLASS (template);
    priv = lrg_game_3d_template_get_instance_private (template);

    /* Handle mouse look */
    if (priv->mouse_look_enabled)
    {
        g_autoptr(GrlVector2) delta_mouse = NULL;

        delta_mouse = grl_input_get_mouse_delta ();

        if (delta_mouse != NULL &&
            (fabsf (delta_mouse->x) > 0.01f || fabsf (delta_mouse->y) > 0.01f))
        {
            if (klass->on_mouse_look != NULL)
                klass->on_mouse_look (template, delta_mouse->x, delta_mouse->y);
        }
    }

    /* Update camera */
    if (klass->update_camera != NULL)
        klass->update_camera (template, delta);

    /* Chain up */
    parent_class = LRG_GAME_TEMPLATE_CLASS (lrg_game_3d_template_parent_class);
    if (parent_class->pre_update != NULL)
        parent_class->pre_update (self, delta);
}

static void
lrg_game_3d_template_pre_draw (LrgGameTemplate *self)
{
    LrgGame3DTemplate *template;
    LrgGame3DTemplateClass *klass;
    LrgGame3DTemplatePrivate *priv;
    LrgGameTemplateClass *parent_class;

    template = LRG_GAME_3D_TEMPLATE (self);
    klass = LRG_GAME_3D_TEMPLATE_GET_CLASS (template);
    priv = lrg_game_3d_template_get_instance_private (template);

    /* Chain up first */
    parent_class = LRG_GAME_TEMPLATE_CLASS (lrg_game_3d_template_parent_class);
    if (parent_class->pre_draw != NULL)
        parent_class->pre_draw (self);

    /* Begin 3D camera mode */
    if (priv->camera != NULL)
        lrg_camera_begin (LRG_CAMERA (priv->camera));

    /* Draw skybox (may disable depth write) */
    if (klass->draw_skybox != NULL)
        klass->draw_skybox (template);

    /* Draw world */
    if (klass->draw_world != NULL)
        klass->draw_world (template);

    /* Draw effects */
    if (klass->draw_effects != NULL)
        klass->draw_effects (template);
}

static void
lrg_game_3d_template_post_draw (LrgGameTemplate *self)
{
    LrgGame3DTemplate *template;
    LrgGame3DTemplateClass *klass;
    LrgGame3DTemplatePrivate *priv;
    LrgGameTemplateClass *parent_class;

    template = LRG_GAME_3D_TEMPLATE (self);
    klass = LRG_GAME_3D_TEMPLATE_GET_CLASS (template);
    priv = lrg_game_3d_template_get_instance_private (template);

    /* End 3D camera mode */
    if (priv->camera != NULL)
        lrg_camera_end (LRG_CAMERA (priv->camera));

    /* Draw 2D UI overlay */
    if (klass->draw_ui != NULL)
        klass->draw_ui (template);

    /* Chain up */
    parent_class = LRG_GAME_TEMPLATE_CLASS (lrg_game_3d_template_parent_class);
    if (parent_class->post_draw != NULL)
        parent_class->post_draw (self);
}

/* ==========================================================================
 * GObject Virtual Methods
 * ========================================================================== */

static void
lrg_game_3d_template_finalize (GObject *object)
{
    LrgGame3DTemplatePrivate *priv;

    priv = lrg_game_3d_template_get_instance_private (LRG_GAME_3D_TEMPLATE (object));

    if (priv->camera_owned)
        g_clear_object (&priv->camera);
    else
        priv->camera = NULL;

    G_OBJECT_CLASS (lrg_game_3d_template_parent_class)->finalize (object);
}

static void
lrg_game_3d_template_get_property (GObject    *object,
                                    guint       prop_id,
                                    GValue     *value,
                                    GParamSpec *pspec)
{
    LrgGame3DTemplate *self;
    LrgGame3DTemplatePrivate *priv;

    self = LRG_GAME_3D_TEMPLATE (object);
    priv = lrg_game_3d_template_get_instance_private (self);

    switch (prop_id)
    {
        case PROP_CAMERA:
            g_value_set_object (value, priv->camera);
            break;

        case PROP_FOV:
            g_value_set_float (value, priv->fov);
            break;

        case PROP_NEAR_CLIP:
            g_value_set_float (value, priv->near_clip);
            break;

        case PROP_FAR_CLIP:
            g_value_set_float (value, priv->far_clip);
            break;

        case PROP_MOUSE_LOOK_ENABLED:
            g_value_set_boolean (value, priv->mouse_look_enabled);
            break;

        case PROP_MOUSE_SENSITIVITY:
            g_value_set_float (value, priv->mouse_sensitivity);
            break;

        case PROP_INVERT_Y:
            g_value_set_boolean (value, priv->invert_y);
            break;

        case PROP_YAW:
            g_value_set_float (value, priv->yaw);
            break;

        case PROP_PITCH:
            g_value_set_float (value, priv->pitch);
            break;

        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_game_3d_template_set_property (GObject      *object,
                                    guint         prop_id,
                                    const GValue *value,
                                    GParamSpec   *pspec)
{
    LrgGame3DTemplate *self;

    self = LRG_GAME_3D_TEMPLATE (object);

    switch (prop_id)
    {
        case PROP_CAMERA:
            lrg_game_3d_template_set_camera (self, g_value_get_object (value));
            break;

        case PROP_FOV:
            lrg_game_3d_template_set_fov (self, g_value_get_float (value));
            break;

        case PROP_NEAR_CLIP:
            lrg_game_3d_template_set_near_clip (self, g_value_get_float (value));
            break;

        case PROP_FAR_CLIP:
            lrg_game_3d_template_set_far_clip (self, g_value_get_float (value));
            break;

        case PROP_MOUSE_LOOK_ENABLED:
            lrg_game_3d_template_set_mouse_look_enabled (self, g_value_get_boolean (value));
            break;

        case PROP_MOUSE_SENSITIVITY:
            lrg_game_3d_template_set_mouse_sensitivity (self, g_value_get_float (value));
            break;

        case PROP_INVERT_Y:
            lrg_game_3d_template_set_invert_y (self, g_value_get_boolean (value));
            break;

        case PROP_YAW:
            lrg_game_3d_template_set_yaw (self, g_value_get_float (value));
            break;

        case PROP_PITCH:
            lrg_game_3d_template_set_pitch (self, g_value_get_float (value));
            break;

        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

/* ==========================================================================
 * Class Initialization
 * ========================================================================== */

static void
lrg_game_3d_template_class_init (LrgGame3DTemplateClass *klass)
{
    GObjectClass *object_class;
    LrgGameTemplateClass *template_class;

    object_class = G_OBJECT_CLASS (klass);
    template_class = LRG_GAME_TEMPLATE_CLASS (klass);

    object_class->finalize = lrg_game_3d_template_finalize;
    object_class->get_property = lrg_game_3d_template_get_property;
    object_class->set_property = lrg_game_3d_template_set_property;

    /* Override parent virtual methods */
    template_class->pre_startup = lrg_game_3d_template_pre_startup;
    template_class->pre_update = lrg_game_3d_template_pre_update;
    template_class->pre_draw = lrg_game_3d_template_pre_draw;
    template_class->post_draw = lrg_game_3d_template_post_draw;

    /* Set default implementations for 3D virtuals */
    klass->draw_skybox = lrg_game_3d_template_real_draw_skybox;
    klass->draw_world = lrg_game_3d_template_real_draw_world;
    klass->draw_effects = lrg_game_3d_template_real_draw_effects;
    klass->draw_ui = lrg_game_3d_template_real_draw_ui;
    klass->update_camera = lrg_game_3d_template_real_update_camera;
    klass->on_mouse_look = lrg_game_3d_template_real_on_mouse_look;

    /* Properties */

    /**
     * LrgGame3DTemplate:camera:
     *
     * The 3D camera used for world rendering.
     */
    properties[PROP_CAMERA] =
        g_param_spec_object ("camera",
                             "Camera",
                             "The 3D camera for world rendering",
                             LRG_TYPE_CAMERA3D,
                             G_PARAM_READWRITE |
                             G_PARAM_STATIC_STRINGS |
                             G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgGame3DTemplate:fov:
     *
     * Camera field of view (vertical, in degrees).
     */
    properties[PROP_FOV] =
        g_param_spec_float ("fov",
                            "Field of View",
                            "Camera vertical field of view in degrees",
                            1.0f, 179.0f, LRG_DEFAULT_3D_FOV,
                            G_PARAM_READWRITE |
                            G_PARAM_STATIC_STRINGS |
                            G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgGame3DTemplate:near-clip:
     *
     * Near clipping plane distance.
     */
    properties[PROP_NEAR_CLIP] =
        g_param_spec_float ("near-clip",
                            "Near Clip",
                            "Near clipping plane distance",
                            0.001f, G_MAXFLOAT, LRG_DEFAULT_NEAR_CLIP,
                            G_PARAM_READWRITE |
                            G_PARAM_STATIC_STRINGS |
                            G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgGame3DTemplate:far-clip:
     *
     * Far clipping plane distance.
     */
    properties[PROP_FAR_CLIP] =
        g_param_spec_float ("far-clip",
                            "Far Clip",
                            "Far clipping plane distance",
                            0.1f, G_MAXFLOAT, LRG_DEFAULT_FAR_CLIP,
                            G_PARAM_READWRITE |
                            G_PARAM_STATIC_STRINGS |
                            G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgGame3DTemplate:mouse-look-enabled:
     *
     * Whether mouse look (FPS-style camera rotation) is enabled.
     */
    properties[PROP_MOUSE_LOOK_ENABLED] =
        g_param_spec_boolean ("mouse-look-enabled",
                              "Mouse Look Enabled",
                              "Whether mouse controls camera rotation",
                              FALSE,
                              G_PARAM_READWRITE |
                              G_PARAM_STATIC_STRINGS |
                              G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgGame3DTemplate:mouse-sensitivity:
     *
     * Mouse look sensitivity multiplier.
     */
    properties[PROP_MOUSE_SENSITIVITY] =
        g_param_spec_float ("mouse-sensitivity",
                            "Mouse Sensitivity",
                            "Mouse look sensitivity",
                            0.01f, 10.0f, LRG_DEFAULT_MOUSE_SENSITIVITY,
                            G_PARAM_READWRITE |
                            G_PARAM_STATIC_STRINGS |
                            G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgGame3DTemplate:invert-y:
     *
     * Whether to invert Y-axis mouse look.
     */
    properties[PROP_INVERT_Y] =
        g_param_spec_boolean ("invert-y",
                              "Invert Y",
                              "Invert Y-axis mouse look",
                              FALSE,
                              G_PARAM_READWRITE |
                              G_PARAM_STATIC_STRINGS |
                              G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgGame3DTemplate:yaw:
     *
     * Camera yaw (horizontal rotation) in degrees.
     */
    properties[PROP_YAW] =
        g_param_spec_float ("yaw",
                            "Yaw",
                            "Camera horizontal rotation in degrees",
                            -G_MAXFLOAT, G_MAXFLOAT, 0.0f,
                            G_PARAM_READWRITE |
                            G_PARAM_STATIC_STRINGS |
                            G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgGame3DTemplate:pitch:
     *
     * Camera pitch (vertical rotation) in degrees.
     */
    properties[PROP_PITCH] =
        g_param_spec_float ("pitch",
                            "Pitch",
                            "Camera vertical rotation in degrees",
                            -90.0f, 90.0f, 0.0f,
                            G_PARAM_READWRITE |
                            G_PARAM_STATIC_STRINGS |
                            G_PARAM_EXPLICIT_NOTIFY);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_game_3d_template_init (LrgGame3DTemplate *self)
{
    LrgGame3DTemplatePrivate *priv;

    priv = lrg_game_3d_template_get_instance_private (self);

    priv->camera = NULL;
    priv->camera_owned = FALSE;

    priv->fov = LRG_DEFAULT_3D_FOV;
    priv->near_clip = LRG_DEFAULT_NEAR_CLIP;
    priv->far_clip = LRG_DEFAULT_FAR_CLIP;

    priv->mouse_look_enabled = FALSE;
    priv->mouse_sensitivity = LRG_DEFAULT_MOUSE_SENSITIVITY;
    priv->invert_y = FALSE;

    priv->yaw = 0.0f;
    priv->pitch = 0.0f;
    priv->min_pitch = LRG_DEFAULT_MIN_PITCH;
    priv->max_pitch = LRG_DEFAULT_MAX_PITCH;

    priv->position_x = 0.0f;
    priv->position_y = LRG_DEFAULT_CAMERA_Y;
    priv->position_z = LRG_DEFAULT_CAMERA_Z;
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

/**
 * lrg_game_3d_template_new:
 *
 * Creates a new 3D game template with default settings.
 *
 * Returns: (transfer full): a new #LrgGame3DTemplate
 */
LrgGame3DTemplate *
lrg_game_3d_template_new (void)
{
    return g_object_new (LRG_TYPE_GAME_3D_TEMPLATE, NULL);
}

LrgCamera3D *
lrg_game_3d_template_get_camera (LrgGame3DTemplate *self)
{
    LrgGame3DTemplatePrivate *priv;

    g_return_val_if_fail (LRG_IS_GAME_3D_TEMPLATE (self), NULL);

    priv = lrg_game_3d_template_get_instance_private (self);

    /* Create camera on first access if needed */
    if (priv->camera == NULL)
    {
        priv->camera = lrg_camera3d_new ();
        priv->camera_owned = TRUE;
        lrg_camera3d_set_position_xyz (priv->camera,
                                       priv->position_x,
                                       priv->position_y,
                                       priv->position_z);
        lrg_camera3d_set_fovy (priv->camera, priv->fov);
        lrg_game_3d_template_update_camera_orientation (self);
    }

    return priv->camera;
}

void
lrg_game_3d_template_set_camera (LrgGame3DTemplate *self,
                                 LrgCamera3D       *camera)
{
    LrgGame3DTemplatePrivate *priv;

    g_return_if_fail (LRG_IS_GAME_3D_TEMPLATE (self));
    g_return_if_fail (camera == NULL || LRG_IS_CAMERA3D (camera));

    priv = lrg_game_3d_template_get_instance_private (self);

    if (priv->camera == camera)
        return;

    if (priv->camera_owned)
        g_clear_object (&priv->camera);
    else
        priv->camera = NULL;

    if (camera != NULL)
    {
        priv->camera = g_object_ref (camera);
        priv->camera_owned = TRUE;
        template_sync_position_from_camera (self);
    }

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_CAMERA]);
}

gfloat
lrg_game_3d_template_get_fov (LrgGame3DTemplate *self)
{
    LrgGame3DTemplatePrivate *priv;

    g_return_val_if_fail (LRG_IS_GAME_3D_TEMPLATE (self), LRG_DEFAULT_3D_FOV);

    priv = lrg_game_3d_template_get_instance_private (self);
    return priv->fov;
}

void
lrg_game_3d_template_set_fov (LrgGame3DTemplate *self,
                              gfloat             fov)
{
    LrgGame3DTemplatePrivate *priv;

    g_return_if_fail (LRG_IS_GAME_3D_TEMPLATE (self));
    g_return_if_fail (fov > 0.0f && fov < 180.0f);

    priv = lrg_game_3d_template_get_instance_private (self);

    if (fabsf (priv->fov - fov) < 0.001f)
        return;

    priv->fov = fov;

    if (priv->camera != NULL)
        lrg_camera3d_set_fovy (priv->camera, fov);

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_FOV]);
}

gfloat
lrg_game_3d_template_get_near_clip (LrgGame3DTemplate *self)
{
    LrgGame3DTemplatePrivate *priv;

    g_return_val_if_fail (LRG_IS_GAME_3D_TEMPLATE (self), LRG_DEFAULT_NEAR_CLIP);

    priv = lrg_game_3d_template_get_instance_private (self);
    return priv->near_clip;
}

void
lrg_game_3d_template_set_near_clip (LrgGame3DTemplate *self,
                                    gfloat             distance)
{
    LrgGame3DTemplatePrivate *priv;

    g_return_if_fail (LRG_IS_GAME_3D_TEMPLATE (self));
    g_return_if_fail (distance > 0.0f);

    priv = lrg_game_3d_template_get_instance_private (self);

    if (fabsf (priv->near_clip - distance) < 0.0001f)
        return;

    priv->near_clip = distance;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_NEAR_CLIP]);
}

gfloat
lrg_game_3d_template_get_far_clip (LrgGame3DTemplate *self)
{
    LrgGame3DTemplatePrivate *priv;

    g_return_val_if_fail (LRG_IS_GAME_3D_TEMPLATE (self), LRG_DEFAULT_FAR_CLIP);

    priv = lrg_game_3d_template_get_instance_private (self);
    return priv->far_clip;
}

void
lrg_game_3d_template_set_far_clip (LrgGame3DTemplate *self,
                                   gfloat             distance)
{
    LrgGame3DTemplatePrivate *priv;

    g_return_if_fail (LRG_IS_GAME_3D_TEMPLATE (self));

    priv = lrg_game_3d_template_get_instance_private (self);

    if (fabsf (priv->far_clip - distance) < 0.0001f)
        return;

    priv->far_clip = distance;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_FAR_CLIP]);
}

gboolean
lrg_game_3d_template_get_mouse_look_enabled (LrgGame3DTemplate *self)
{
    LrgGame3DTemplatePrivate *priv;

    g_return_val_if_fail (LRG_IS_GAME_3D_TEMPLATE (self), FALSE);

    priv = lrg_game_3d_template_get_instance_private (self);
    return priv->mouse_look_enabled;
}

void
lrg_game_3d_template_set_mouse_look_enabled (LrgGame3DTemplate *self,
                                             gboolean           enabled)
{
    LrgGame3DTemplatePrivate *priv;

    g_return_if_fail (LRG_IS_GAME_3D_TEMPLATE (self));

    priv = lrg_game_3d_template_get_instance_private (self);

    if (priv->mouse_look_enabled == enabled)
        return;

    priv->mouse_look_enabled = enabled;

    /* Lock/unlock cursor */
    if (enabled)
    {
        grl_window_disable_cursor (NULL);
    }
    else
    {
        grl_window_enable_cursor (NULL);
    }

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MOUSE_LOOK_ENABLED]);
}

gfloat
lrg_game_3d_template_get_mouse_sensitivity (LrgGame3DTemplate *self)
{
    LrgGame3DTemplatePrivate *priv;

    g_return_val_if_fail (LRG_IS_GAME_3D_TEMPLATE (self), LRG_DEFAULT_MOUSE_SENSITIVITY);

    priv = lrg_game_3d_template_get_instance_private (self);
    return priv->mouse_sensitivity;
}

void
lrg_game_3d_template_set_mouse_sensitivity (LrgGame3DTemplate *self,
                                            gfloat             sensitivity)
{
    LrgGame3DTemplatePrivate *priv;

    g_return_if_fail (LRG_IS_GAME_3D_TEMPLATE (self));

    priv = lrg_game_3d_template_get_instance_private (self);

    if (fabsf (priv->mouse_sensitivity - sensitivity) < 0.001f)
        return;

    priv->mouse_sensitivity = sensitivity;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MOUSE_SENSITIVITY]);
}

gboolean
lrg_game_3d_template_get_invert_y (LrgGame3DTemplate *self)
{
    LrgGame3DTemplatePrivate *priv;

    g_return_val_if_fail (LRG_IS_GAME_3D_TEMPLATE (self), FALSE);

    priv = lrg_game_3d_template_get_instance_private (self);
    return priv->invert_y;
}

void
lrg_game_3d_template_set_invert_y (LrgGame3DTemplate *self,
                                   gboolean           invert)
{
    LrgGame3DTemplatePrivate *priv;

    g_return_if_fail (LRG_IS_GAME_3D_TEMPLATE (self));

    priv = lrg_game_3d_template_get_instance_private (self);

    if (priv->invert_y == invert)
        return;

    priv->invert_y = invert;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_INVERT_Y]);
}

void
lrg_game_3d_template_set_pitch_limits (LrgGame3DTemplate *self,
                                       gfloat             min_pitch,
                                       gfloat             max_pitch)
{
    LrgGame3DTemplatePrivate *priv;

    g_return_if_fail (LRG_IS_GAME_3D_TEMPLATE (self));
    g_return_if_fail (min_pitch < max_pitch);

    priv = lrg_game_3d_template_get_instance_private (self);
    priv->min_pitch = min_pitch;
    priv->max_pitch = max_pitch;

    /* Re-clamp current pitch */
    priv->pitch = CLAMP (priv->pitch, priv->min_pitch, priv->max_pitch);
}

void
lrg_game_3d_template_get_pitch_limits (LrgGame3DTemplate *self,
                                       gfloat            *min_pitch,
                                       gfloat            *max_pitch)
{
    LrgGame3DTemplatePrivate *priv;

    g_return_if_fail (LRG_IS_GAME_3D_TEMPLATE (self));

    priv = lrg_game_3d_template_get_instance_private (self);

    if (min_pitch != NULL)
        *min_pitch = priv->min_pitch;
    if (max_pitch != NULL)
        *max_pitch = priv->max_pitch;
}

gfloat
lrg_game_3d_template_get_yaw (LrgGame3DTemplate *self)
{
    LrgGame3DTemplatePrivate *priv;

    g_return_val_if_fail (LRG_IS_GAME_3D_TEMPLATE (self), 0.0f);

    priv = lrg_game_3d_template_get_instance_private (self);
    return priv->yaw;
}

void
lrg_game_3d_template_set_yaw (LrgGame3DTemplate *self,
                              gfloat             yaw)
{
    LrgGame3DTemplatePrivate *priv;

    g_return_if_fail (LRG_IS_GAME_3D_TEMPLATE (self));

    priv = lrg_game_3d_template_get_instance_private (self);

    /* Normalize to 0-360 range */
    while (yaw < 0.0f)
        yaw += 360.0f;
    while (yaw >= 360.0f)
        yaw -= 360.0f;

    if (fabsf (priv->yaw - yaw) < 0.001f)
        return;

    priv->yaw = yaw;
    lrg_game_3d_template_update_camera_orientation (self);
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_YAW]);
}

gfloat
lrg_game_3d_template_get_pitch (LrgGame3DTemplate *self)
{
    LrgGame3DTemplatePrivate *priv;

    g_return_val_if_fail (LRG_IS_GAME_3D_TEMPLATE (self), 0.0f);

    priv = lrg_game_3d_template_get_instance_private (self);
    return priv->pitch;
}

void
lrg_game_3d_template_set_pitch (LrgGame3DTemplate *self,
                                gfloat             pitch)
{
    LrgGame3DTemplatePrivate *priv;

    g_return_if_fail (LRG_IS_GAME_3D_TEMPLATE (self));

    priv = lrg_game_3d_template_get_instance_private (self);

    pitch = CLAMP (pitch, priv->min_pitch, priv->max_pitch);

    if (fabsf (priv->pitch - pitch) < 0.001f)
        return;

    priv->pitch = pitch;
    lrg_game_3d_template_update_camera_orientation (self);
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_PITCH]);
}

void
lrg_game_3d_template_look_at (LrgGame3DTemplate *self,
                              gfloat             target_x,
                              gfloat             target_y,
                              gfloat             target_z)
{
    LrgGame3DTemplatePrivate *priv;
    gfloat dx;
    gfloat dy;
    gfloat dz;
    gfloat horizontal_dist;

    g_return_if_fail (LRG_IS_GAME_3D_TEMPLATE (self));

    priv = lrg_game_3d_template_get_instance_private (self);

    /* Calculate direction to target */
    dx = target_x - priv->position_x;
    dy = target_y - priv->position_y;
    dz = target_z - priv->position_z;

    /* Calculate yaw from horizontal direction */
    priv->yaw = atan2f (dx, dz) * (gfloat)(180.0 / G_PI);
    if (priv->yaw < 0.0f)
        priv->yaw += 360.0f;

    /* Calculate pitch from vertical direction */
    horizontal_dist = sqrtf (dx * dx + dz * dz);
    priv->pitch = atan2f (dy, horizontal_dist) * (gfloat)(180.0 / G_PI);
    priv->pitch = CLAMP (priv->pitch, priv->min_pitch, priv->max_pitch);

    lrg_game_3d_template_update_camera_orientation (self);

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_YAW]);
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_PITCH]);
}

void
lrg_game_3d_template_move_forward (LrgGame3DTemplate *self,
                                   gfloat             distance)
{
    LrgGame3DTemplatePrivate *priv;
    gfloat yaw_rad;
    gfloat pitch_rad;
    gfloat cos_pitch;

    g_return_if_fail (LRG_IS_GAME_3D_TEMPLATE (self));

    priv = lrg_game_3d_template_get_instance_private (self);

    yaw_rad = priv->yaw * (gfloat)(G_PI / 180.0);
    pitch_rad = priv->pitch * (gfloat)(G_PI / 180.0);
    cos_pitch = cosf (pitch_rad);

    priv->position_x += distance * cos_pitch * sinf (yaw_rad);
    priv->position_y += distance * sinf (pitch_rad);
    priv->position_z += distance * cos_pitch * cosf (yaw_rad);

    template_sync_camera_position (self);
    lrg_game_3d_template_update_camera_orientation (self);
}

void
lrg_game_3d_template_move_right (LrgGame3DTemplate *self,
                                 gfloat             distance)
{
    LrgGame3DTemplatePrivate *priv;
    gfloat yaw_rad;

    g_return_if_fail (LRG_IS_GAME_3D_TEMPLATE (self));

    priv = lrg_game_3d_template_get_instance_private (self);

    yaw_rad = priv->yaw * (gfloat)(G_PI / 180.0);

    /* Right vector is perpendicular to forward in the XZ plane */
    priv->position_x += distance * cosf (yaw_rad);
    priv->position_z -= distance * sinf (yaw_rad);

    template_sync_camera_position (self);
    lrg_game_3d_template_update_camera_orientation (self);
}

void
lrg_game_3d_template_move_up (LrgGame3DTemplate *self,
                              gfloat             distance)
{
    LrgGame3DTemplatePrivate *priv;

    g_return_if_fail (LRG_IS_GAME_3D_TEMPLATE (self));

    priv = lrg_game_3d_template_get_instance_private (self);

    priv->position_y += distance;

    template_sync_camera_position (self);
    lrg_game_3d_template_update_camera_orientation (self);
}

void
lrg_game_3d_template_world_to_screen (LrgGame3DTemplate *self,
                                      gfloat             world_x,
                                      gfloat             world_y,
                                      gfloat             world_z,
                                      gfloat            *screen_x,
                                      gfloat            *screen_y)
{
    LrgGame3DTemplatePrivate *priv;
    Vector3 world_pos;
    Vector2 screen_pos;

    g_return_if_fail (LRG_IS_GAME_3D_TEMPLATE (self));

    priv = lrg_game_3d_template_get_instance_private (self);

    if (priv->camera == NULL)
    {
        if (screen_x != NULL)
            *screen_x = 0.0f;
        if (screen_y != NULL)
            *screen_y = 0.0f;
        return;
    }

    world_pos.x = world_x;
    world_pos.y = world_y;
    world_pos.z = world_z;

    /*
     * Use raylib's GetWorldToScreen function via camera.
     * Need to construct raylib Camera3D from our camera.
     */
    {
        Camera camera;
        g_autoptr(GrlVector3) pos = NULL;
        g_autoptr(GrlVector3) target = NULL;
        g_autoptr(GrlVector3) up = NULL;

        pos = lrg_camera3d_get_position (priv->camera);
        target = lrg_camera3d_get_target (priv->camera);
        up = lrg_camera3d_get_up (priv->camera);

        camera.position = (Vector3){ pos ? pos->x : 0, pos ? pos->y : 0, pos ? pos->z : 0 };
        camera.target = (Vector3){ target ? target->x : 0, target ? target->y : 0, target ? target->z : 0 };
        camera.up = (Vector3){ up ? up->x : 0, up ? up->y : 1, up ? up->z : 0 };
        camera.fovy = priv->fov;
        camera.projection = CAMERA_PERSPECTIVE;

        screen_pos = GetWorldToScreen (world_pos, camera);
    }

    if (screen_x != NULL)
        *screen_x = screen_pos.x;
    if (screen_y != NULL)
        *screen_y = screen_pos.y;
}

void
lrg_game_3d_template_screen_to_ray (LrgGame3DTemplate *self,
                                    gfloat             screen_x,
                                    gfloat             screen_y,
                                    gfloat            *ray_origin_x,
                                    gfloat            *ray_origin_y,
                                    gfloat            *ray_origin_z,
                                    gfloat            *ray_direction_x,
                                    gfloat            *ray_direction_y,
                                    gfloat            *ray_direction_z)
{
    LrgGame3DTemplatePrivate *priv;
    Ray ray;

    g_return_if_fail (LRG_IS_GAME_3D_TEMPLATE (self));

    priv = lrg_game_3d_template_get_instance_private (self);

    if (priv->camera == NULL)
    {
        if (ray_origin_x != NULL) *ray_origin_x = 0.0f;
        if (ray_origin_y != NULL) *ray_origin_y = 0.0f;
        if (ray_origin_z != NULL) *ray_origin_z = 0.0f;
        if (ray_direction_x != NULL) *ray_direction_x = 0.0f;
        if (ray_direction_y != NULL) *ray_direction_y = 0.0f;
        if (ray_direction_z != NULL) *ray_direction_z = -1.0f;
        return;
    }

    /* Construct raylib Camera3D */
    {
        Camera camera;
        g_autoptr(GrlVector3) pos = NULL;
        g_autoptr(GrlVector3) target = NULL;
        g_autoptr(GrlVector3) up = NULL;
        Vector2 screen_pos;

        pos = lrg_camera3d_get_position (priv->camera);
        target = lrg_camera3d_get_target (priv->camera);
        up = lrg_camera3d_get_up (priv->camera);

        camera.position = (Vector3){ pos ? pos->x : 0, pos ? pos->y : 0, pos ? pos->z : 0 };
        camera.target = (Vector3){ target ? target->x : 0, target ? target->y : 0, target ? target->z : 0 };
        camera.up = (Vector3){ up ? up->x : 0, up ? up->y : 1, up ? up->z : 0 };
        camera.fovy = priv->fov;
        camera.projection = CAMERA_PERSPECTIVE;

        screen_pos.x = screen_x;
        screen_pos.y = screen_y;

        ray = GetScreenToWorldRay (screen_pos, camera);
    }

    if (ray_origin_x != NULL)
        *ray_origin_x = ray.position.x;
    if (ray_origin_y != NULL)
        *ray_origin_y = ray.position.y;
    if (ray_origin_z != NULL)
        *ray_origin_z = ray.position.z;
    if (ray_direction_x != NULL)
        *ray_direction_x = ray.direction.x;
    if (ray_direction_y != NULL)
        *ray_direction_y = ray.direction.y;
    if (ray_direction_z != NULL)
        *ray_direction_z = ray.direction.z;
}
