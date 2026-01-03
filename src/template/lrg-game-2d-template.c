/* lrg-game-2d-template.c - 2D game template implementation
 *
 * Copyright 2025 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * This file is part of Libregnum.
 */

#include "../config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_TEMPLATE

#include "lrg-game-2d-template.h"
#include "lrg-game-2d-template-private.h"
#include "lrg-template-scalable.h"
#include "../lrg-log.h"

#include <graylib.h>
#include <math.h>

#ifndef G_PI
#define G_PI 3.14159265358979323846
#endif

/* ==========================================================================
 * Forward Declarations
 * ========================================================================== */

static void lrg_template_scalable_interface_init (LrgTemplateScalableInterface *iface);

/* ==========================================================================
 * Property IDs
 * ========================================================================== */

enum
{
    PROP_0,

    PROP_VIRTUAL_WIDTH,
    PROP_VIRTUAL_HEIGHT,
    PROP_SCALING_MODE,
    PROP_PIXEL_PERFECT,
    PROP_CAMERA,
    PROP_CAMERA_SMOOTHING,
    PROP_LETTERBOX_COLOR,

    N_PROPS
};

static GParamSpec *properties[N_PROPS];

/* ==========================================================================
 * Signal IDs
 * ========================================================================== */

enum
{
    SIGNAL_RESOLUTION_CHANGED,
    N_SIGNALS
};

static guint signals[N_SIGNALS];

/* ==========================================================================
 * Type Definition
 * ========================================================================== */

G_DEFINE_TYPE_WITH_CODE (LrgGame2DTemplate, lrg_game_2d_template,
                         LRG_TYPE_GAME_TEMPLATE,
                         G_ADD_PRIVATE (LrgGame2DTemplate)
                         G_IMPLEMENT_INTERFACE (LRG_TYPE_TEMPLATE_SCALABLE,
                                               lrg_template_scalable_interface_init))

/* ==========================================================================
 * Private Implementation
 * ========================================================================== */

LrgGame2DTemplatePrivate *
lrg_game_2d_template_get_private (LrgGame2DTemplate *self)
{
    return lrg_game_2d_template_get_instance_private (self);
}

void
lrg_game_2d_template_update_scaling (LrgGame2DTemplate *self,
                                     gint               window_width,
                                     gint               window_height)
{
    LrgGame2DTemplatePrivate *priv;
    gfloat sx;
    gfloat sy;
    LrgScalingMode effective_mode;

    priv = lrg_game_2d_template_get_instance_private (self);

    /* Calculate base scale factors */
    sx = (gfloat) window_width / (gfloat) priv->virtual_width;
    sy = (gfloat) window_height / (gfloat) priv->virtual_height;

    /* Determine effective scaling mode */
    effective_mode = priv->pixel_perfect
                     ? LRG_SCALING_MODE_PIXEL_PERFECT
                     : priv->scaling_mode;

    priv->offset_x = 0.0f;
    priv->offset_y = 0.0f;

    switch (effective_mode)
    {
        case LRG_SCALING_MODE_STRETCH:
            /* Non-uniform scaling - use separate X/Y scales */
            priv->scale_x = sx;
            priv->scale_y = sy;
            break;

        case LRG_SCALING_MODE_LETTERBOX:
        case LRG_SCALING_MODE_PILLARBOX:
            {
                /* Uniform scaling with bars */
                gfloat uniform_scale;

                uniform_scale = fminf (sx, sy);
                priv->scale_x = priv->scale_y = uniform_scale;
                priv->offset_x = (window_width - priv->virtual_width * uniform_scale) / 2.0f;
                priv->offset_y = (window_height - priv->virtual_height * uniform_scale) / 2.0f;
            }
            break;

        case LRG_SCALING_MODE_CROP:
            {
                /* Fill window, crop excess */
                gfloat uniform_scale;

                uniform_scale = fmaxf (sx, sy);
                priv->scale_x = priv->scale_y = uniform_scale;
                priv->offset_x = (window_width - priv->virtual_width * uniform_scale) / 2.0f;
                priv->offset_y = (window_height - priv->virtual_height * uniform_scale) / 2.0f;
            }
            break;

        case LRG_SCALING_MODE_PIXEL_PERFECT:
            {
                /* Integer scaling only */
                gint int_scale;

                int_scale = (gint) fminf (sx, sy);
                if (int_scale < 1)
                    int_scale = 1;

                priv->scale_x = priv->scale_y = (gfloat) int_scale;
                priv->offset_x = (window_width - priv->virtual_width * int_scale) / 2.0f;
                priv->offset_y = (window_height - priv->virtual_height * int_scale) / 2.0f;
            }
            break;
    }

    priv->viewport_width = priv->virtual_width * priv->scale_x;
    priv->viewport_height = priv->virtual_height * priv->scale_y;
}

void
lrg_game_2d_template_ensure_render_target (LrgGame2DTemplate *self)
{
    LrgGame2DTemplatePrivate *priv;

    priv = lrg_game_2d_template_get_instance_private (self);

    /* Check if we need to create or recreate */
    if (priv->render_target != NULL && priv->render_target_valid)
    {
        gint rt_width;
        gint rt_height;

        rt_width = grl_render_texture_get_width (priv->render_target);
        rt_height = grl_render_texture_get_height (priv->render_target);

        if (rt_width == priv->virtual_width && rt_height == priv->virtual_height)
            return;  /* Already correct size */

        /* Wrong size, need to recreate */
        g_clear_object (&priv->render_target);
        priv->render_target_valid = FALSE;
    }

    /* Create new render target */
    priv->render_target = grl_render_texture_new (priv->virtual_width,
                                                   priv->virtual_height);
    priv->render_target_valid = grl_render_texture_is_valid (priv->render_target);

    if (!priv->render_target_valid)
    {
        lrg_warning (LRG_LOG_DOMAIN,
                     "Failed to create render target %dx%d",
                     priv->virtual_width, priv->virtual_height);
    }
}

static void
template_check_resolution_change (LrgGame2DTemplate *self)
{
    LrgGame2DTemplatePrivate *priv;
    LrgGame2DTemplateClass *klass;
    gint width;
    gint height;

    priv = lrg_game_2d_template_get_instance_private (self);

    /* Get window size from parent template */
    lrg_game_template_get_window_size (LRG_GAME_TEMPLATE (self), &width, &height);

    if (width == 0 || height == 0)
        return;

    if (width == priv->last_window_width && height == priv->last_window_height)
        return;

    priv->last_window_width = width;
    priv->last_window_height = height;

    /* Update scaling */
    lrg_game_2d_template_update_scaling (self, width, height);

    /* Call virtual method */
    klass = LRG_GAME_2D_TEMPLATE_GET_CLASS (self);
    if (klass->on_resolution_changed != NULL)
        klass->on_resolution_changed (self, width, height);

    /* Emit signal */
    g_signal_emit (self, signals[SIGNAL_RESOLUTION_CHANGED], 0, width, height);
}

static void
template_apply_camera_deadzone (LrgGame2DTemplate        *self,
                                LrgGame2DTemplatePrivate *priv,
                                gfloat                   *target_x,
                                gfloat                   *target_y)
{
    gfloat camera_center_x;
    gfloat camera_center_y;
    gfloat half_dz_width;
    gfloat half_dz_height;
    gfloat dx;
    gfloat dy;
    g_autoptr(GrlVector2) cam_target = NULL;

    if (priv->deadzone_width <= 0.0f && priv->deadzone_height <= 0.0f)
        return;  /* No deadzone */

    if (priv->camera == NULL)
        return;

    cam_target = lrg_camera2d_get_target (priv->camera);
    if (cam_target == NULL)
        return;

    camera_center_x = cam_target->x;
    camera_center_y = cam_target->y;

    half_dz_width = priv->deadzone_width / 2.0f;
    half_dz_height = priv->deadzone_height / 2.0f;

    dx = *target_x - camera_center_x;
    dy = *target_y - camera_center_y;

    /*
     * Only move camera if target is outside deadzone.
     * Adjust target to edge of deadzone.
     */
    if (fabsf (dx) <= half_dz_width)
        *target_x = camera_center_x;
    else if (dx > 0)
        *target_x = camera_center_x + (dx - half_dz_width);
    else
        *target_x = camera_center_x + (dx + half_dz_width);

    if (fabsf (dy) <= half_dz_height)
        *target_y = camera_center_y;
    else if (dy > 0)
        *target_y = camera_center_y + (dy - half_dz_height);
    else
        *target_y = camera_center_y + (dy + half_dz_height);
}

static void
template_apply_camera_bounds (LrgGame2DTemplate        *self,
                              LrgGame2DTemplatePrivate *priv,
                              gfloat                   *target_x,
                              gfloat                   *target_y)
{
    gfloat half_view_width;
    gfloat half_view_height;
    gfloat min_cam_x;
    gfloat min_cam_y;
    gfloat max_cam_x;
    gfloat max_cam_y;
    gfloat zoom;

    if (!priv->has_camera_bounds)
        return;

    if (priv->camera == NULL)
        return;

    zoom = lrg_camera2d_get_zoom (priv->camera);
    if (zoom <= 0.0f)
        zoom = 1.0f;

    /* Calculate visible area at current zoom */
    half_view_width = (priv->virtual_width / zoom) / 2.0f;
    half_view_height = (priv->virtual_height / zoom) / 2.0f;

    /* Camera target must stay within bounds such that viewport stays inside */
    min_cam_x = priv->bounds_min_x + half_view_width;
    min_cam_y = priv->bounds_min_y + half_view_height;
    max_cam_x = priv->bounds_max_x - half_view_width;
    max_cam_y = priv->bounds_max_y - half_view_height;

    /* Handle case where viewport is larger than bounds */
    if (min_cam_x > max_cam_x)
        *target_x = (priv->bounds_min_x + priv->bounds_max_x) / 2.0f;
    else
        *target_x = CLAMP (*target_x, min_cam_x, max_cam_x);

    if (min_cam_y > max_cam_y)
        *target_y = (priv->bounds_min_y + priv->bounds_max_y) / 2.0f;
    else
        *target_y = CLAMP (*target_y, min_cam_y, max_cam_y);
}

/* ==========================================================================
 * Interface Implementation - LrgTemplateScalable
 * ========================================================================== */

static gint
scalable_get_virtual_width (LrgTemplateScalable *self)
{
    LrgGame2DTemplatePrivate *priv;

    priv = lrg_game_2d_template_get_instance_private (LRG_GAME_2D_TEMPLATE (self));
    return priv->virtual_width;
}

static gint
scalable_get_virtual_height (LrgTemplateScalable *self)
{
    LrgGame2DTemplatePrivate *priv;

    priv = lrg_game_2d_template_get_instance_private (LRG_GAME_2D_TEMPLATE (self));
    return priv->virtual_height;
}

static LrgScalingMode
scalable_get_scaling_mode (LrgTemplateScalable *self)
{
    LrgGame2DTemplatePrivate *priv;

    priv = lrg_game_2d_template_get_instance_private (LRG_GAME_2D_TEMPLATE (self));
    return priv->pixel_perfect ? LRG_SCALING_MODE_PIXEL_PERFECT : priv->scaling_mode;
}

static void
scalable_world_to_screen (LrgTemplateScalable *self,
                          gfloat               world_x,
                          gfloat               world_y,
                          gfloat              *screen_x,
                          gfloat              *screen_y)
{
    lrg_game_2d_template_world_to_screen (LRG_GAME_2D_TEMPLATE (self),
                                          world_x, world_y,
                                          screen_x, screen_y);
}

static void
scalable_screen_to_world (LrgTemplateScalable *self,
                          gfloat               screen_x,
                          gfloat               screen_y,
                          gfloat              *world_x,
                          gfloat              *world_y)
{
    lrg_game_2d_template_screen_to_world (LRG_GAME_2D_TEMPLATE (self),
                                          screen_x, screen_y,
                                          world_x, world_y);
}

static void
lrg_template_scalable_interface_init (LrgTemplateScalableInterface *iface)
{
    iface->get_virtual_width = scalable_get_virtual_width;
    iface->get_virtual_height = scalable_get_virtual_height;
    iface->get_scaling_mode = scalable_get_scaling_mode;
    iface->world_to_screen = scalable_world_to_screen;
    iface->screen_to_world = scalable_screen_to_world;
}

/* ==========================================================================
 * Virtual Method Implementations
 * ========================================================================== */

static void
lrg_game_2d_template_real_on_resolution_changed (LrgGame2DTemplate *self,
                                                  gint               new_width,
                                                  gint               new_height)
{
    lrg_debug (LRG_LOG_DOMAIN,
               "Resolution changed to %dx%d", new_width, new_height);
}

static void
lrg_game_2d_template_real_draw_background (LrgGame2DTemplate *self)
{
    /* Default: empty background */
}

static void
lrg_game_2d_template_real_draw_world (LrgGame2DTemplate *self)
{
    /* Default: nothing in world */
}

static void
lrg_game_2d_template_real_draw_ui (LrgGame2DTemplate *self)
{
    /* Default: no UI */
}

static void
lrg_game_2d_template_real_update_camera (LrgGame2DTemplate *self,
                                          gdouble            delta)
{
    LrgGame2DTemplatePrivate *priv;
    gfloat target_x;
    gfloat target_y;
    gfloat current_x;
    gfloat current_y;
    gfloat new_x;
    gfloat new_y;
    gfloat lerp_factor;
    g_autoptr(GrlVector2) cam_target = NULL;

    priv = lrg_game_2d_template_get_instance_private (self);

    if (priv->camera == NULL)
        return;

    target_x = priv->camera_target_x;
    target_y = priv->camera_target_y;

    /* Apply deadzone */
    template_apply_camera_deadzone (self, priv, &target_x, &target_y);

    /* Apply bounds */
    template_apply_camera_bounds (self, priv, &target_x, &target_y);

    /* Get current camera position */
    cam_target = lrg_camera2d_get_target (priv->camera);
    if (cam_target != NULL)
    {
        current_x = cam_target->x;
        current_y = cam_target->y;
    }
    else
    {
        current_x = target_x;
        current_y = target_y;
    }

    /* Apply smoothing */
    if (priv->camera_smoothing <= 0.0f)
    {
        /* Instant snap */
        new_x = target_x;
        new_y = target_y;
    }
    else
    {
        /*
         * Exponential smoothing using lerp.
         * Higher smoothing = slower movement.
         */
        lerp_factor = 1.0f - powf (priv->camera_smoothing, (gfloat) delta * 60.0f);
        lerp_factor = CLAMP (lerp_factor, 0.0f, 1.0f);

        new_x = current_x + (target_x - current_x) * lerp_factor;
        new_y = current_y + (target_y - current_y) * lerp_factor;
    }

    lrg_camera2d_set_target_xy (priv->camera, new_x, new_y);
}

/* ==========================================================================
 * Overridden Parent Virtual Methods
 * ========================================================================== */

static void
lrg_game_2d_template_pre_startup (LrgGameTemplate *self)
{
    LrgGame2DTemplatePrivate *priv;
    LrgGameTemplateClass *parent_class;

    /* Chain up first */
    parent_class = LRG_GAME_TEMPLATE_CLASS (lrg_game_2d_template_parent_class);
    if (parent_class->pre_startup != NULL)
        parent_class->pre_startup (self);

    priv = lrg_game_2d_template_get_instance_private (LRG_GAME_2D_TEMPLATE (self));

    /* Create camera if needed */
    if (priv->camera == NULL)
    {
        priv->camera = lrg_camera2d_new ();
        priv->camera_owned = TRUE;

        /* Center camera offset on virtual resolution */
        lrg_camera2d_set_offset_xy (priv->camera,
                                    priv->virtual_width / 2.0f,
                                    priv->virtual_height / 2.0f);
    }

    /* Initialize window tracking */
    priv->last_window_width = 0;
    priv->last_window_height = 0;
}

static void
lrg_game_2d_template_post_startup (LrgGameTemplate *self)
{
    LrgGame2DTemplatePrivate *priv;
    LrgGameTemplateClass *parent_class;
    gint width;
    gint height;

    priv = lrg_game_2d_template_get_instance_private (LRG_GAME_2D_TEMPLATE (self));

    /* Create render target now that window exists */
    lrg_game_2d_template_ensure_render_target (LRG_GAME_2D_TEMPLATE (self));

    /* Initialize scaling */
    lrg_game_template_get_window_size (self, &width, &height);
    if (width > 0 && height > 0)
    {
        lrg_game_2d_template_update_scaling (LRG_GAME_2D_TEMPLATE (self),
                                             width, height);
        priv->last_window_width = width;
        priv->last_window_height = height;
    }

    /* Chain up */
    parent_class = LRG_GAME_TEMPLATE_CLASS (lrg_game_2d_template_parent_class);
    if (parent_class->post_startup != NULL)
        parent_class->post_startup (self);
}

static void
lrg_game_2d_template_pre_update (LrgGameTemplate *self,
                                  gdouble          delta)
{
    LrgGame2DTemplateClass *klass;
    LrgGameTemplateClass *parent_class;

    /* Check for resolution changes */
    template_check_resolution_change (LRG_GAME_2D_TEMPLATE (self));

    /* Update camera */
    klass = LRG_GAME_2D_TEMPLATE_GET_CLASS (self);
    if (klass->update_camera != NULL)
        klass->update_camera (LRG_GAME_2D_TEMPLATE (self), delta);

    /* Chain up */
    parent_class = LRG_GAME_TEMPLATE_CLASS (lrg_game_2d_template_parent_class);
    if (parent_class->pre_update != NULL)
        parent_class->pre_update (self, delta);
}

static void
lrg_game_2d_template_pre_draw (LrgGameTemplate *self)
{
    LrgGame2DTemplate *template;
    LrgGame2DTemplateClass *klass;
    LrgGame2DTemplatePrivate *priv;
    LrgGameTemplateClass *parent_class;

    template = LRG_GAME_2D_TEMPLATE (self);
    klass = LRG_GAME_2D_TEMPLATE_GET_CLASS (template);
    priv = lrg_game_2d_template_get_instance_private (template);

    /* Chain up first */
    parent_class = LRG_GAME_TEMPLATE_CLASS (lrg_game_2d_template_parent_class);
    if (parent_class->pre_draw != NULL)
        parent_class->pre_draw (self);

    /* Ensure we have a valid render target */
    if (!priv->render_target_valid || priv->render_target == NULL)
    {
        lrg_game_2d_template_ensure_render_target (template);
        if (!priv->render_target_valid)
            return;
    }

    /*
     * Clear screen with letterbox color first.
     * Parent already cleared with background_color, but we want letterbox bars.
     */
    grl_draw_clear_background (priv->letterbox_color);

    /* Begin rendering to the virtual resolution texture */
    grl_render_texture_begin (priv->render_target);

    /* Clear render target with game background */
    if (priv->background_color != NULL)
        grl_draw_clear_background (priv->background_color);

    /* Draw background layer (no camera) */
    if (klass->draw_background != NULL)
        klass->draw_background (template);

    /* Begin camera transform for world rendering */
    if (priv->camera != NULL)
        lrg_camera_begin (LRG_CAMERA (priv->camera));

    /* Draw world layer (with camera) - before game states */
    if (klass->draw_world != NULL)
        klass->draw_world (template);

    /*
     * NOTE: Game states draw after this (via state manager).
     * Camera is still active, so they draw in world space.
     */
}

static void
lrg_game_2d_template_post_draw (LrgGameTemplate *self)
{
    LrgGame2DTemplate *template;
    LrgGame2DTemplateClass *klass;
    LrgGame2DTemplatePrivate *priv;
    LrgGameTemplateClass *parent_class;
    g_autoptr(GrlTexture) rt_texture = NULL;
    g_autoptr(GrlRectangle) src_rect = NULL;
    g_autoptr(GrlRectangle) dst_rect = NULL;
    g_autoptr(GrlVector2) origin = NULL;
    g_autoptr(GrlColor) white = NULL;

    template = LRG_GAME_2D_TEMPLATE (self);
    klass = LRG_GAME_2D_TEMPLATE_GET_CLASS (template);
    priv = lrg_game_2d_template_get_instance_private (template);

    /* End camera transform */
    if (priv->camera != NULL)
        lrg_camera_end (LRG_CAMERA (priv->camera));

    /* Draw UI layer (no camera) */
    if (klass->draw_ui != NULL)
        klass->draw_ui (template);

    /* End render target */
    if (priv->render_target != NULL && priv->render_target_valid)
    {
        grl_render_texture_end (priv->render_target);

        /* Get texture from render target */
        rt_texture = grl_render_texture_get_texture (priv->render_target);

        if (rt_texture != NULL)
        {
            /* Source rectangle (flipped Y for OpenGL) */
            src_rect = grl_rectangle_new (0.0f,
                                           (gfloat) priv->virtual_height,
                                           (gfloat) priv->virtual_width,
                                           -(gfloat) priv->virtual_height);

            /* Destination rectangle */
            dst_rect = grl_rectangle_new (priv->offset_x,
                                           priv->offset_y,
                                           priv->viewport_width,
                                           priv->viewport_height);

            origin = grl_vector2_new (0.0f, 0.0f);
            white = grl_color_new (255, 255, 255, 255);

            grl_draw_texture_pro (rt_texture, src_rect, dst_rect, origin, 0.0f, white);
        }
    }

    /* Chain up */
    parent_class = LRG_GAME_TEMPLATE_CLASS (lrg_game_2d_template_parent_class);
    if (parent_class->post_draw != NULL)
        parent_class->post_draw (self);
}

static void
lrg_game_2d_template_shutdown (LrgGameTemplate *self)
{
    LrgGame2DTemplatePrivate *priv;
    LrgGameTemplateClass *parent_class;

    priv = lrg_game_2d_template_get_instance_private (LRG_GAME_2D_TEMPLATE (self));

    /* Clean up render target before GPU context is destroyed */
    g_clear_object (&priv->render_target);
    priv->render_target_valid = FALSE;

    /* Chain up */
    parent_class = LRG_GAME_TEMPLATE_CLASS (lrg_game_2d_template_parent_class);
    if (parent_class->shutdown != NULL)
        parent_class->shutdown (self);
}

/* ==========================================================================
 * GObject Virtual Methods
 * ========================================================================== */

static void
lrg_game_2d_template_finalize (GObject *object)
{
    LrgGame2DTemplatePrivate *priv;

    priv = lrg_game_2d_template_get_instance_private (LRG_GAME_2D_TEMPLATE (object));

    g_clear_object (&priv->render_target);
    g_clear_pointer (&priv->letterbox_color, grl_color_free);
    g_clear_pointer (&priv->background_color, grl_color_free);

    if (priv->camera_owned)
        g_clear_object (&priv->camera);
    else
        priv->camera = NULL;

    G_OBJECT_CLASS (lrg_game_2d_template_parent_class)->finalize (object);
}

static void
lrg_game_2d_template_get_property (GObject    *object,
                                    guint       prop_id,
                                    GValue     *value,
                                    GParamSpec *pspec)
{
    LrgGame2DTemplate *self;
    LrgGame2DTemplatePrivate *priv;

    self = LRG_GAME_2D_TEMPLATE (object);
    priv = lrg_game_2d_template_get_instance_private (self);

    switch (prop_id)
    {
        case PROP_VIRTUAL_WIDTH:
            g_value_set_int (value, priv->virtual_width);
            break;

        case PROP_VIRTUAL_HEIGHT:
            g_value_set_int (value, priv->virtual_height);
            break;

        case PROP_SCALING_MODE:
            g_value_set_enum (value, priv->scaling_mode);
            break;

        case PROP_PIXEL_PERFECT:
            g_value_set_boolean (value, priv->pixel_perfect);
            break;

        case PROP_CAMERA:
            g_value_set_object (value, priv->camera);
            break;

        case PROP_CAMERA_SMOOTHING:
            g_value_set_float (value, priv->camera_smoothing);
            break;

        case PROP_LETTERBOX_COLOR:
            g_value_set_boxed (value, priv->letterbox_color);
            break;

        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_game_2d_template_set_property (GObject      *object,
                                    guint         prop_id,
                                    const GValue *value,
                                    GParamSpec   *pspec)
{
    LrgGame2DTemplate *self;

    self = LRG_GAME_2D_TEMPLATE (object);

    switch (prop_id)
    {
        case PROP_VIRTUAL_WIDTH:
            lrg_game_2d_template_set_virtual_width (self, g_value_get_int (value));
            break;

        case PROP_VIRTUAL_HEIGHT:
            lrg_game_2d_template_set_virtual_height (self, g_value_get_int (value));
            break;

        case PROP_SCALING_MODE:
            lrg_game_2d_template_set_scaling_mode (self, g_value_get_enum (value));
            break;

        case PROP_PIXEL_PERFECT:
            lrg_game_2d_template_set_pixel_perfect (self, g_value_get_boolean (value));
            break;

        case PROP_CAMERA:
            lrg_game_2d_template_set_camera (self, g_value_get_object (value));
            break;

        case PROP_CAMERA_SMOOTHING:
            lrg_game_2d_template_set_camera_smoothing (self, g_value_get_float (value));
            break;

        case PROP_LETTERBOX_COLOR:
            lrg_game_2d_template_set_letterbox_color (self, g_value_get_boxed (value));
            break;

        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

/* ==========================================================================
 * Class Initialization
 * ========================================================================== */

static void
lrg_game_2d_template_class_init (LrgGame2DTemplateClass *klass)
{
    GObjectClass *object_class;
    LrgGameTemplateClass *template_class;

    object_class = G_OBJECT_CLASS (klass);
    template_class = LRG_GAME_TEMPLATE_CLASS (klass);

    object_class->finalize = lrg_game_2d_template_finalize;
    object_class->get_property = lrg_game_2d_template_get_property;
    object_class->set_property = lrg_game_2d_template_set_property;

    /* Override parent virtual methods */
    template_class->pre_startup = lrg_game_2d_template_pre_startup;
    template_class->post_startup = lrg_game_2d_template_post_startup;
    template_class->pre_update = lrg_game_2d_template_pre_update;
    template_class->pre_draw = lrg_game_2d_template_pre_draw;
    template_class->post_draw = lrg_game_2d_template_post_draw;
    template_class->shutdown = lrg_game_2d_template_shutdown;

    /* Set default implementations for 2D virtuals */
    klass->on_resolution_changed = lrg_game_2d_template_real_on_resolution_changed;
    klass->draw_background = lrg_game_2d_template_real_draw_background;
    klass->draw_world = lrg_game_2d_template_real_draw_world;
    klass->draw_ui = lrg_game_2d_template_real_draw_ui;
    klass->update_camera = lrg_game_2d_template_real_update_camera;

    /* Properties */

    /**
     * LrgGame2DTemplate:virtual-width:
     *
     * The virtual (design) resolution width.
     *
     * The game renders to this width and is then scaled to fit the window.
     */
    properties[PROP_VIRTUAL_WIDTH] =
        g_param_spec_int ("virtual-width",
                          "Virtual Width",
                          "Virtual resolution width",
                          1, G_MAXINT, LRG_DEFAULT_VIRTUAL_WIDTH,
                          G_PARAM_READWRITE |
                          G_PARAM_STATIC_STRINGS |
                          G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgGame2DTemplate:virtual-height:
     *
     * The virtual (design) resolution height.
     *
     * The game renders to this height and is then scaled to fit the window.
     */
    properties[PROP_VIRTUAL_HEIGHT] =
        g_param_spec_int ("virtual-height",
                          "Virtual Height",
                          "Virtual resolution height",
                          1, G_MAXINT, LRG_DEFAULT_VIRTUAL_HEIGHT,
                          G_PARAM_READWRITE |
                          G_PARAM_STATIC_STRINGS |
                          G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgGame2DTemplate:scaling-mode:
     *
     * How the virtual resolution is scaled to fit the window.
     */
    properties[PROP_SCALING_MODE] =
        g_param_spec_enum ("scaling-mode",
                           "Scaling Mode",
                           "How virtual resolution is scaled",
                           LRG_TYPE_SCALING_MODE,
                           LRG_SCALING_MODE_LETTERBOX,
                           G_PARAM_READWRITE |
                           G_PARAM_STATIC_STRINGS |
                           G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgGame2DTemplate:pixel-perfect:
     *
     * Whether to use pixel-perfect (integer) scaling.
     *
     * When enabled, the virtual resolution is scaled by an integer
     * factor only, avoiding sub-pixel artifacts in pixel art.
     */
    properties[PROP_PIXEL_PERFECT] =
        g_param_spec_boolean ("pixel-perfect",
                              "Pixel Perfect",
                              "Use integer scaling only",
                              FALSE,
                              G_PARAM_READWRITE |
                              G_PARAM_STATIC_STRINGS |
                              G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgGame2DTemplate:camera:
     *
     * The 2D camera used for world rendering.
     */
    properties[PROP_CAMERA] =
        g_param_spec_object ("camera",
                             "Camera",
                             "The 2D camera for world rendering",
                             LRG_TYPE_CAMERA2D,
                             G_PARAM_READWRITE |
                             G_PARAM_STATIC_STRINGS |
                             G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgGame2DTemplate:camera-smoothing:
     *
     * Camera follow smoothing factor.
     *
     * 0.0 = instant snap to target
     * Higher values = smoother, slower following
     */
    properties[PROP_CAMERA_SMOOTHING] =
        g_param_spec_float ("camera-smoothing",
                            "Camera Smoothing",
                            "Camera follow smoothing factor",
                            0.0f, 1.0f, LRG_2D_TEMPLATE_DEFAULT_CAMERA_SMOOTHING,
                            G_PARAM_READWRITE |
                            G_PARAM_STATIC_STRINGS |
                            G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgGame2DTemplate:letterbox-color:
     *
     * The color used for letterbox/pillarbox bars.
     */
    properties[PROP_LETTERBOX_COLOR] =
        g_param_spec_boxed ("letterbox-color",
                            "Letterbox Color",
                            "Color for letterbox/pillarbox bars",
                            GRL_TYPE_COLOR,
                            G_PARAM_READWRITE |
                            G_PARAM_STATIC_STRINGS |
                            G_PARAM_EXPLICIT_NOTIFY);

    g_object_class_install_properties (object_class, N_PROPS, properties);

    /* Signals */

    /**
     * LrgGame2DTemplate::resolution-changed:
     * @self: the template
     * @width: new window width
     * @height: new window height
     *
     * Emitted when the window resolution changes.
     */
    signals[SIGNAL_RESOLUTION_CHANGED] =
        g_signal_new ("resolution-changed",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (LrgGame2DTemplateClass, on_resolution_changed),
                      NULL, NULL, NULL,
                      G_TYPE_NONE, 2, G_TYPE_INT, G_TYPE_INT);
}

static void
lrg_game_2d_template_init (LrgGame2DTemplate *self)
{
    LrgGame2DTemplatePrivate *priv;

    priv = lrg_game_2d_template_get_instance_private (self);

    priv->virtual_width = LRG_DEFAULT_VIRTUAL_WIDTH;
    priv->virtual_height = LRG_DEFAULT_VIRTUAL_HEIGHT;
    priv->scaling_mode = LRG_SCALING_MODE_LETTERBOX;
    priv->pixel_perfect = FALSE;

    priv->render_target = NULL;
    priv->render_target_valid = FALSE;

    priv->letterbox_color = grl_color_new (0, 0, 0, 255);  /* Black */
    priv->background_color = grl_color_new (40, 40, 40, 255);  /* Dark gray */

    priv->scale_x = 1.0f;
    priv->scale_y = 1.0f;
    priv->offset_x = 0.0f;
    priv->offset_y = 0.0f;
    priv->viewport_width = (gfloat) priv->virtual_width;
    priv->viewport_height = (gfloat) priv->virtual_height;

    priv->last_window_width = 0;
    priv->last_window_height = 0;

    priv->camera = NULL;
    priv->camera_owned = FALSE;

    priv->camera_target_x = 0.0f;
    priv->camera_target_y = 0.0f;
    priv->camera_smoothing = LRG_2D_TEMPLATE_DEFAULT_CAMERA_SMOOTHING;

    priv->deadzone_width = 0.0f;
    priv->deadzone_height = 0.0f;

    priv->has_camera_bounds = FALSE;
    priv->bounds_min_x = 0.0f;
    priv->bounds_min_y = 0.0f;
    priv->bounds_max_x = 0.0f;
    priv->bounds_max_y = 0.0f;
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

/**
 * lrg_game_2d_template_new:
 *
 * Creates a new 2D game template with default settings.
 *
 * Returns: (transfer full): a new #LrgGame2DTemplate
 */
LrgGame2DTemplate *
lrg_game_2d_template_new (void)
{
    return g_object_new (LRG_TYPE_GAME_2D_TEMPLATE, NULL);
}

gint
lrg_game_2d_template_get_virtual_width (LrgGame2DTemplate *self)
{
    LrgGame2DTemplatePrivate *priv;

    g_return_val_if_fail (LRG_IS_GAME_2D_TEMPLATE (self), LRG_DEFAULT_VIRTUAL_WIDTH);

    priv = lrg_game_2d_template_get_instance_private (self);
    return priv->virtual_width;
}

void
lrg_game_2d_template_set_virtual_width (LrgGame2DTemplate *self,
                                        gint               width)
{
    LrgGame2DTemplatePrivate *priv;

    g_return_if_fail (LRG_IS_GAME_2D_TEMPLATE (self));
    g_return_if_fail (width >= 1);

    priv = lrg_game_2d_template_get_instance_private (self);

    if (priv->virtual_width == width)
        return;

    priv->virtual_width = width;
    priv->render_target_valid = FALSE;  /* Force recreation */

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_VIRTUAL_WIDTH]);
}

gint
lrg_game_2d_template_get_virtual_height (LrgGame2DTemplate *self)
{
    LrgGame2DTemplatePrivate *priv;

    g_return_val_if_fail (LRG_IS_GAME_2D_TEMPLATE (self), LRG_DEFAULT_VIRTUAL_HEIGHT);

    priv = lrg_game_2d_template_get_instance_private (self);
    return priv->virtual_height;
}

void
lrg_game_2d_template_set_virtual_height (LrgGame2DTemplate *self,
                                         gint               height)
{
    LrgGame2DTemplatePrivate *priv;

    g_return_if_fail (LRG_IS_GAME_2D_TEMPLATE (self));
    g_return_if_fail (height >= 1);

    priv = lrg_game_2d_template_get_instance_private (self);

    if (priv->virtual_height == height)
        return;

    priv->virtual_height = height;
    priv->render_target_valid = FALSE;  /* Force recreation */

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_VIRTUAL_HEIGHT]);
}

void
lrg_game_2d_template_set_virtual_resolution (LrgGame2DTemplate *self,
                                             gint               width,
                                             gint               height)
{
    g_return_if_fail (LRG_IS_GAME_2D_TEMPLATE (self));
    g_return_if_fail (width >= 1);
    g_return_if_fail (height >= 1);

    g_object_freeze_notify (G_OBJECT (self));
    lrg_game_2d_template_set_virtual_width (self, width);
    lrg_game_2d_template_set_virtual_height (self, height);
    g_object_thaw_notify (G_OBJECT (self));
}

LrgScalingMode
lrg_game_2d_template_get_scaling_mode (LrgGame2DTemplate *self)
{
    LrgGame2DTemplatePrivate *priv;

    g_return_val_if_fail (LRG_IS_GAME_2D_TEMPLATE (self), LRG_SCALING_MODE_LETTERBOX);

    priv = lrg_game_2d_template_get_instance_private (self);
    return priv->scaling_mode;
}

void
lrg_game_2d_template_set_scaling_mode (LrgGame2DTemplate *self,
                                       LrgScalingMode     mode)
{
    LrgGame2DTemplatePrivate *priv;

    g_return_if_fail (LRG_IS_GAME_2D_TEMPLATE (self));

    priv = lrg_game_2d_template_get_instance_private (self);

    if (priv->scaling_mode == mode)
        return;

    priv->scaling_mode = mode;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SCALING_MODE]);
}

gboolean
lrg_game_2d_template_get_pixel_perfect (LrgGame2DTemplate *self)
{
    LrgGame2DTemplatePrivate *priv;

    g_return_val_if_fail (LRG_IS_GAME_2D_TEMPLATE (self), FALSE);

    priv = lrg_game_2d_template_get_instance_private (self);
    return priv->pixel_perfect;
}

void
lrg_game_2d_template_set_pixel_perfect (LrgGame2DTemplate *self,
                                        gboolean           pixel_perfect)
{
    LrgGame2DTemplatePrivate *priv;

    g_return_if_fail (LRG_IS_GAME_2D_TEMPLATE (self));

    priv = lrg_game_2d_template_get_instance_private (self);

    if (priv->pixel_perfect == pixel_perfect)
        return;

    priv->pixel_perfect = pixel_perfect;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_PIXEL_PERFECT]);
}

LrgCamera2D *
lrg_game_2d_template_get_camera (LrgGame2DTemplate *self)
{
    LrgGame2DTemplatePrivate *priv;

    g_return_val_if_fail (LRG_IS_GAME_2D_TEMPLATE (self), NULL);

    priv = lrg_game_2d_template_get_instance_private (self);

    /* Create camera on first access if needed */
    if (priv->camera == NULL)
    {
        priv->camera = lrg_camera2d_new ();
        priv->camera_owned = TRUE;
        lrg_camera2d_set_offset_xy (priv->camera,
                                    priv->virtual_width / 2.0f,
                                    priv->virtual_height / 2.0f);
    }

    return priv->camera;
}

void
lrg_game_2d_template_set_camera (LrgGame2DTemplate *self,
                                 LrgCamera2D       *camera)
{
    LrgGame2DTemplatePrivate *priv;

    g_return_if_fail (LRG_IS_GAME_2D_TEMPLATE (self));
    g_return_if_fail (camera == NULL || LRG_IS_CAMERA2D (camera));

    priv = lrg_game_2d_template_get_instance_private (self);

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
    }

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_CAMERA]);
}

void
lrg_game_2d_template_set_camera_target (LrgGame2DTemplate *self,
                                        gfloat             x,
                                        gfloat             y)
{
    LrgGame2DTemplatePrivate *priv;

    g_return_if_fail (LRG_IS_GAME_2D_TEMPLATE (self));

    priv = lrg_game_2d_template_get_instance_private (self);
    priv->camera_target_x = x;
    priv->camera_target_y = y;
}

gfloat
lrg_game_2d_template_get_camera_smoothing (LrgGame2DTemplate *self)
{
    LrgGame2DTemplatePrivate *priv;

    g_return_val_if_fail (LRG_IS_GAME_2D_TEMPLATE (self), LRG_2D_TEMPLATE_DEFAULT_CAMERA_SMOOTHING);

    priv = lrg_game_2d_template_get_instance_private (self);
    return priv->camera_smoothing;
}

void
lrg_game_2d_template_set_camera_smoothing (LrgGame2DTemplate *self,
                                           gfloat             smoothing)
{
    LrgGame2DTemplatePrivate *priv;

    g_return_if_fail (LRG_IS_GAME_2D_TEMPLATE (self));

    priv = lrg_game_2d_template_get_instance_private (self);

    smoothing = CLAMP (smoothing, 0.0f, 1.0f);

    if (fabsf (priv->camera_smoothing - smoothing) < 0.0001f)
        return;

    priv->camera_smoothing = smoothing;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_CAMERA_SMOOTHING]);
}

void
lrg_game_2d_template_set_camera_deadzone (LrgGame2DTemplate *self,
                                          gfloat             width,
                                          gfloat             height)
{
    LrgGame2DTemplatePrivate *priv;

    g_return_if_fail (LRG_IS_GAME_2D_TEMPLATE (self));

    priv = lrg_game_2d_template_get_instance_private (self);
    priv->deadzone_width = fmaxf (width, 0.0f);
    priv->deadzone_height = fmaxf (height, 0.0f);
}

void
lrg_game_2d_template_get_camera_deadzone (LrgGame2DTemplate *self,
                                          gfloat            *width,
                                          gfloat            *height)
{
    LrgGame2DTemplatePrivate *priv;

    g_return_if_fail (LRG_IS_GAME_2D_TEMPLATE (self));

    priv = lrg_game_2d_template_get_instance_private (self);

    if (width != NULL)
        *width = priv->deadzone_width;
    if (height != NULL)
        *height = priv->deadzone_height;
}

void
lrg_game_2d_template_set_camera_bounds (LrgGame2DTemplate *self,
                                        gfloat             min_x,
                                        gfloat             min_y,
                                        gfloat             max_x,
                                        gfloat             max_y)
{
    LrgGame2DTemplatePrivate *priv;

    g_return_if_fail (LRG_IS_GAME_2D_TEMPLATE (self));

    priv = lrg_game_2d_template_get_instance_private (self);
    priv->has_camera_bounds = TRUE;
    priv->bounds_min_x = min_x;
    priv->bounds_min_y = min_y;
    priv->bounds_max_x = max_x;
    priv->bounds_max_y = max_y;
}

void
lrg_game_2d_template_clear_camera_bounds (LrgGame2DTemplate *self)
{
    LrgGame2DTemplatePrivate *priv;

    g_return_if_fail (LRG_IS_GAME_2D_TEMPLATE (self));

    priv = lrg_game_2d_template_get_instance_private (self);
    priv->has_camera_bounds = FALSE;
}

void
lrg_game_2d_template_world_to_screen (LrgGame2DTemplate *self,
                                      gfloat             world_x,
                                      gfloat             world_y,
                                      gfloat            *screen_x,
                                      gfloat            *screen_y)
{
    LrgGame2DTemplatePrivate *priv;
    gfloat virtual_x;
    gfloat virtual_y;

    g_return_if_fail (LRG_IS_GAME_2D_TEMPLATE (self));

    priv = lrg_game_2d_template_get_instance_private (self);

    virtual_x = world_x;
    virtual_y = world_y;

    /* First apply camera transform to get virtual coordinates */
    if (priv->camera != NULL)
    {
        g_autoptr(GrlVector2) target = NULL;
        gfloat zoom;
        gfloat rotation;
        g_autoptr(GrlVector2) offset = NULL;
        gfloat rad;
        gfloat cos_r;
        gfloat sin_r;
        gfloat rx;
        gfloat ry;

        target = lrg_camera2d_get_target (priv->camera);
        offset = lrg_camera2d_get_offset (priv->camera);
        zoom = lrg_camera2d_get_zoom (priv->camera);
        rotation = lrg_camera2d_get_rotation (priv->camera);

        if (zoom <= 0.0f)
            zoom = 1.0f;

        /* Translate relative to camera target */
        virtual_x = world_x - (target ? target->x : 0.0f);
        virtual_y = world_y - (target ? target->y : 0.0f);

        /* Apply zoom */
        virtual_x *= zoom;
        virtual_y *= zoom;

        /* Apply rotation if any */
        if (fabsf (rotation) > 0.001f)
        {
            rad = rotation * (G_PI / 180.0f);
            cos_r = cosf (rad);
            sin_r = sinf (rad);
            rx = virtual_x * cos_r - virtual_y * sin_r;
            ry = virtual_x * sin_r + virtual_y * cos_r;
            virtual_x = rx;
            virtual_y = ry;
        }

        /* Add offset (typically half screen to center) */
        virtual_x += (offset ? offset->x : 0.0f);
        virtual_y += (offset ? offset->y : 0.0f);
    }

    /* Then scale from virtual to screen */
    if (screen_x != NULL)
        *screen_x = priv->offset_x + virtual_x * priv->scale_x;
    if (screen_y != NULL)
        *screen_y = priv->offset_y + virtual_y * priv->scale_y;
}

void
lrg_game_2d_template_screen_to_world (LrgGame2DTemplate *self,
                                      gfloat             screen_x,
                                      gfloat             screen_y,
                                      gfloat            *world_x,
                                      gfloat            *world_y)
{
    LrgGame2DTemplatePrivate *priv;
    gfloat virtual_x;
    gfloat virtual_y;

    g_return_if_fail (LRG_IS_GAME_2D_TEMPLATE (self));

    priv = lrg_game_2d_template_get_instance_private (self);

    /* First convert screen to virtual coordinates */
    virtual_x = (screen_x - priv->offset_x) / priv->scale_x;
    virtual_y = (screen_y - priv->offset_y) / priv->scale_y;

    /* Then apply inverse camera transform */
    if (priv->camera != NULL)
    {
        g_autoptr(GrlVector2) target = NULL;
        g_autoptr(GrlVector2) offset = NULL;
        gfloat zoom;
        gfloat rotation;
        gfloat wx;
        gfloat wy;
        gfloat rad;
        gfloat cos_r;
        gfloat sin_r;
        gfloat rx;
        gfloat ry;

        target = lrg_camera2d_get_target (priv->camera);
        offset = lrg_camera2d_get_offset (priv->camera);
        zoom = lrg_camera2d_get_zoom (priv->camera);
        rotation = lrg_camera2d_get_rotation (priv->camera);

        if (zoom <= 0.0f)
            zoom = 1.0f;

        /* Subtract offset */
        wx = virtual_x - (offset ? offset->x : 0.0f);
        wy = virtual_y - (offset ? offset->y : 0.0f);

        /* Apply inverse rotation if any */
        if (fabsf (rotation) > 0.001f)
        {
            rad = -rotation * (G_PI / 180.0f);
            cos_r = cosf (rad);
            sin_r = sinf (rad);
            rx = wx * cos_r - wy * sin_r;
            ry = wx * sin_r + wy * cos_r;
            wx = rx;
            wy = ry;
        }

        /* Apply inverse zoom */
        wx /= zoom;
        wy /= zoom;

        /* Add camera target */
        wx += (target ? target->x : 0.0f);
        wy += (target ? target->y : 0.0f);

        if (world_x != NULL)
            *world_x = wx;
        if (world_y != NULL)
            *world_y = wy;
    }
    else
    {
        if (world_x != NULL)
            *world_x = virtual_x;
        if (world_y != NULL)
            *world_y = virtual_y;
    }
}

void
lrg_game_2d_template_virtual_to_screen (LrgGame2DTemplate *self,
                                        gfloat             virtual_x,
                                        gfloat             virtual_y,
                                        gfloat            *screen_x,
                                        gfloat            *screen_y)
{
    LrgGame2DTemplatePrivate *priv;

    g_return_if_fail (LRG_IS_GAME_2D_TEMPLATE (self));

    priv = lrg_game_2d_template_get_instance_private (self);

    if (screen_x != NULL)
        *screen_x = priv->offset_x + virtual_x * priv->scale_x;
    if (screen_y != NULL)
        *screen_y = priv->offset_y + virtual_y * priv->scale_y;
}

void
lrg_game_2d_template_screen_to_virtual (LrgGame2DTemplate *self,
                                        gfloat             screen_x,
                                        gfloat             screen_y,
                                        gfloat            *virtual_x,
                                        gfloat            *virtual_y)
{
    LrgGame2DTemplatePrivate *priv;

    g_return_if_fail (LRG_IS_GAME_2D_TEMPLATE (self));

    priv = lrg_game_2d_template_get_instance_private (self);

    if (virtual_x != NULL)
        *virtual_x = (screen_x - priv->offset_x) / priv->scale_x;
    if (virtual_y != NULL)
        *virtual_y = (screen_y - priv->offset_y) / priv->scale_y;
}

GrlRenderTexture *
lrg_game_2d_template_get_render_texture (LrgGame2DTemplate *self)
{
    LrgGame2DTemplatePrivate *priv;

    g_return_val_if_fail (LRG_IS_GAME_2D_TEMPLATE (self), NULL);

    priv = lrg_game_2d_template_get_instance_private (self);
    return priv->render_target;
}

GrlColor *
lrg_game_2d_template_get_letterbox_color (LrgGame2DTemplate *self)
{
    LrgGame2DTemplatePrivate *priv;

    g_return_val_if_fail (LRG_IS_GAME_2D_TEMPLATE (self), NULL);

    priv = lrg_game_2d_template_get_instance_private (self);
    return grl_color_copy (priv->letterbox_color);
}

void
lrg_game_2d_template_set_letterbox_color (LrgGame2DTemplate *self,
                                          GrlColor          *color)
{
    LrgGame2DTemplatePrivate *priv;

    g_return_if_fail (LRG_IS_GAME_2D_TEMPLATE (self));

    priv = lrg_game_2d_template_get_instance_private (self);

    g_clear_pointer (&priv->letterbox_color, grl_color_free);

    if (color != NULL)
        priv->letterbox_color = grl_color_copy (color);
    else
        priv->letterbox_color = grl_color_new (0, 0, 0, 255);

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_LETTERBOX_COLOR]);
}
