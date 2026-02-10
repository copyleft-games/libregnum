/* lrg-wipe-transition.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Directional wipe transition.
 */

#include "lrg-wipe-transition.h"
#include "../tween/lrg-easing.h"
#include "../lrg-log.h"

#include <graylib.h>

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_TRANSITION

/**
 * LrgWipeTransition:
 *
 * A transition that wipes across the screen in a specified direction,
 * revealing a solid color and then the new scene.
 *
 * The wipe transition moves a "curtain" across the screen:
 *
 * 1. **OUT phase**: Wipe reveals solid color from the direction
 * 2. **HOLD phase**: Screen shows solid color (scene switch occurs here)
 * 3. **IN phase**: Wipe continues, revealing new scene
 *
 * The wipe can go in four directions: left, right, up, or down.
 * The `softness` property controls how soft/blurred the wipe edge is.
 *
 * ## Example usage
 *
 * ```c
 * LrgWipeTransition *wipe = lrg_wipe_transition_new_with_direction (
 *     LRG_TRANSITION_DIRECTION_RIGHT);
 * lrg_wipe_transition_set_softness (wipe, 0.1f);
 * lrg_transition_start (LRG_TRANSITION (wipe));
 * ```
 *
 * Since: 1.0
 */

struct _LrgWipeTransition
{
    LrgTransition parent_instance;

    LrgTransitionDirection direction;
    gfloat softness;
};

enum
{
    PROP_0,
    PROP_DIRECTION,
    PROP_SOFTNESS,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

G_DEFINE_FINAL_TYPE (LrgWipeTransition, lrg_wipe_transition, LRG_TYPE_TRANSITION)

/*
 * LrgTransition virtual method implementations
 */

static gboolean
lrg_wipe_transition_initialize (LrgTransition  *transition,
                                guint           width,
                                guint           height,
                                GError        **error)
{
    (void) transition;
    (void) width;
    (void) height;
    (void) error;

    lrg_debug (LRG_LOG_DOMAIN_TRANSITION, "Wipe transition initialized (viewport: %ux%u)", width, height);

    return TRUE;
}

static void
lrg_wipe_transition_shutdown (LrgTransition *transition)
{
    (void) transition;

    lrg_debug (LRG_LOG_DOMAIN_TRANSITION, "Wipe transition shutdown");
}

static void
lrg_wipe_transition_start (LrgTransition *transition)
{
    LrgWipeTransition *self;
    const gchar *direction_name;

    self = LRG_WIPE_TRANSITION (transition);

    switch (self->direction)
    {
    case LRG_TRANSITION_DIRECTION_LEFT:
        direction_name = "left";
        break;
    case LRG_TRANSITION_DIRECTION_RIGHT:
        direction_name = "right";
        break;
    case LRG_TRANSITION_DIRECTION_UP:
        direction_name = "up";
        break;
    case LRG_TRANSITION_DIRECTION_DOWN:
        direction_name = "down";
        break;
    default:
        direction_name = "unknown";
        break;
    }

    lrg_debug (LRG_LOG_DOMAIN_TRANSITION, "Wipe transition started (direction: %s, softness: %.2f)",
                   direction_name, self->softness);
}

static void
lrg_wipe_transition_update (LrgTransition *transition,
                            gfloat         delta_time)
{
    (void) transition;
    (void) delta_time;
}

static void
lrg_wipe_transition_render (LrgTransition *transition,
                            guint          old_scene_texture,
                            guint          new_scene_texture,
                            guint          width,
                            guint          height)
{
    LrgWipeTransition *self;
    LrgTransitionState state;
    gfloat phase_progress;
    gfloat eased_progress;
    LrgEasingType easing;
    gfloat wipe_position;

    self = LRG_WIPE_TRANSITION (transition);
    state = lrg_transition_get_state (transition);
    phase_progress = lrg_transition_get_phase_progress (transition);
    easing = lrg_transition_get_easing (transition);
    eased_progress = lrg_easing_apply (easing, phase_progress);

    /*
     * Calculate wipe position (0.0 to 1.0 across the screen)
     *
     * OUT phase: wipe from 0 to 1 (covering old scene)
     * HOLD: fully covered
     * IN phase: wipe from 0 to 1 (revealing new scene, continuing same direction)
     */
    switch (state)
    {
    case LRG_TRANSITION_STATE_OUT:
        wipe_position = eased_progress;
        break;

    case LRG_TRANSITION_STATE_HOLD:
        wipe_position = 1.0f;
        break;

    case LRG_TRANSITION_STATE_IN:
        wipe_position = eased_progress;
        break;

    case LRG_TRANSITION_STATE_IDLE:
    case LRG_TRANSITION_STATE_COMPLETE:
    default:
        wipe_position = 0.0f;
        break;
    }

    /*
     * Render the wipe by using scissor clipping.
     *
     * During OUT: draw old scene in uncovered area, solid color in wiped area
     * During HOLD: solid color fills the entire screen
     * During IN: draw new scene in revealed area, solid color in covered area
     *
     * The wipe position determines the boundary.
     */
    if (state == LRG_TRANSITION_STATE_HOLD)
    {
        /* Solid black fill during hold */
        GrlColor black;

        black = grl_color_init (0, 0, 0, 255);
        grl_draw_rectangle (0, 0, (gint) width, (gint) height, &black);
        return;
    }

    {
        guint scene_tex;
        gint wipe_px;
        gint sx, sy, sw, sh;   /* scissor for scene visible area */
        gint cx, cy, cw, ch;   /* rect for covered/solid color area */
        GrlColor solid;

        scene_tex = (state == LRG_TRANSITION_STATE_OUT) ? old_scene_texture : new_scene_texture;
        solid = grl_color_init (0, 0, 0, 255);

        /* Calculate wipe boundary and scissor regions */
        switch (self->direction)
        {
        case LRG_TRANSITION_DIRECTION_LEFT:
            wipe_px = (gint)(wipe_position * (gfloat) width);
            if (state == LRG_TRANSITION_STATE_OUT)
            {
                /* Scene visible on right, solid on left */
                sx = wipe_px; sy = 0; sw = (gint) width - wipe_px; sh = (gint) height;
                cx = 0; cy = 0; cw = wipe_px; ch = (gint) height;
            }
            else
            {
                /* Scene revealed from left, solid on right */
                sx = 0; sy = 0; sw = wipe_px; sh = (gint) height;
                cx = wipe_px; cy = 0; cw = (gint) width - wipe_px; ch = (gint) height;
            }
            break;

        case LRG_TRANSITION_DIRECTION_RIGHT:
            wipe_px = (gint)(wipe_position * (gfloat) width);
            if (state == LRG_TRANSITION_STATE_OUT)
            {
                /* Scene visible on left, solid on right */
                sx = 0; sy = 0; sw = (gint) width - wipe_px; sh = (gint) height;
                cx = (gint) width - wipe_px; cy = 0; cw = wipe_px; ch = (gint) height;
            }
            else
            {
                /* Scene revealed from right, solid on left */
                sx = (gint) width - wipe_px; sy = 0; sw = wipe_px; sh = (gint) height;
                cx = 0; cy = 0; cw = (gint) width - wipe_px; ch = (gint) height;
            }
            break;

        case LRG_TRANSITION_DIRECTION_UP:
            wipe_px = (gint)(wipe_position * (gfloat) height);
            if (state == LRG_TRANSITION_STATE_OUT)
            {
                sx = 0; sy = wipe_px; sw = (gint) width; sh = (gint) height - wipe_px;
                cx = 0; cy = 0; cw = (gint) width; ch = wipe_px;
            }
            else
            {
                sx = 0; sy = 0; sw = (gint) width; sh = wipe_px;
                cx = 0; cy = wipe_px; cw = (gint) width; ch = (gint) height - wipe_px;
            }
            break;

        case LRG_TRANSITION_DIRECTION_DOWN:
        default:
            wipe_px = (gint)(wipe_position * (gfloat) height);
            if (state == LRG_TRANSITION_STATE_OUT)
            {
                sx = 0; sy = 0; sw = (gint) width; sh = (gint) height - wipe_px;
                cx = 0; cy = (gint) height - wipe_px; cw = (gint) width; ch = wipe_px;
            }
            else
            {
                sx = 0; sy = (gint) height - wipe_px; sw = (gint) width; sh = wipe_px;
                cx = 0; cy = 0; cw = (gint) width; ch = (gint) height - wipe_px;
            }
            break;
        }

        /* Draw solid color in wiped area */
        if (cw > 0 && ch > 0)
            grl_draw_rectangle (cx, cy, cw, ch, &solid);

        /* Draw scene in visible area using scissor clip */
        if (sw > 0 && sh > 0 && scene_tex != 0)
        {
            grl_draw_begin_scissor_mode (sx, sy, sw, sh);

            grl_rlgl_enable_texture (scene_tex);
            grl_rlgl_begin (GRL_RLGL_QUADS);
                grl_rlgl_color4ub (255, 255, 255, 255);
                grl_rlgl_tex_coord2f (0.0f, 1.0f); grl_rlgl_vertex2f (0.0f, 0.0f);
                grl_rlgl_tex_coord2f (0.0f, 0.0f); grl_rlgl_vertex2f (0.0f, (gfloat) height);
                grl_rlgl_tex_coord2f (1.0f, 0.0f); grl_rlgl_vertex2f ((gfloat) width, (gfloat) height);
                grl_rlgl_tex_coord2f (1.0f, 1.0f); grl_rlgl_vertex2f ((gfloat) width, 0.0f);
            grl_rlgl_end ();
            grl_rlgl_disable_texture ();

            grl_draw_end_scissor_mode ();
        }
    }
}

static void
lrg_wipe_transition_reset (LrgTransition *transition)
{
    (void) transition;

    lrg_debug (LRG_LOG_DOMAIN_TRANSITION, "Wipe transition reset");
}

/*
 * GObject virtual methods
 */

static void
lrg_wipe_transition_get_property (GObject    *object,
                                  guint       prop_id,
                                  GValue     *value,
                                  GParamSpec *pspec)
{
    LrgWipeTransition *self;

    self = LRG_WIPE_TRANSITION (object);

    switch (prop_id)
    {
    case PROP_DIRECTION:
        g_value_set_enum (value, self->direction);
        break;

    case PROP_SOFTNESS:
        g_value_set_float (value, self->softness);
        break;

    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
lrg_wipe_transition_set_property (GObject      *object,
                                  guint         prop_id,
                                  const GValue *value,
                                  GParamSpec   *pspec)
{
    LrgWipeTransition *self;

    self = LRG_WIPE_TRANSITION (object);

    switch (prop_id)
    {
    case PROP_DIRECTION:
        self->direction = g_value_get_enum (value);
        break;

    case PROP_SOFTNESS:
        self->softness = g_value_get_float (value);
        break;

    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
lrg_wipe_transition_class_init (LrgWipeTransitionClass *klass)
{
    GObjectClass *object_class;
    LrgTransitionClass *transition_class;

    object_class = G_OBJECT_CLASS (klass);
    object_class->get_property = lrg_wipe_transition_get_property;
    object_class->set_property = lrg_wipe_transition_set_property;

    transition_class = LRG_TRANSITION_CLASS (klass);
    transition_class->initialize = lrg_wipe_transition_initialize;
    transition_class->shutdown = lrg_wipe_transition_shutdown;
    transition_class->start = lrg_wipe_transition_start;
    transition_class->update = lrg_wipe_transition_update;
    transition_class->render = lrg_wipe_transition_render;
    transition_class->reset = lrg_wipe_transition_reset;

    /**
     * LrgWipeTransition:direction:
     *
     * The direction of the wipe effect.
     *
     * Since: 1.0
     */
    properties[PROP_DIRECTION] =
        g_param_spec_enum ("direction",
                           "Direction",
                           "The wipe direction",
                           LRG_TYPE_TRANSITION_DIRECTION,
                           LRG_TRANSITION_DIRECTION_LEFT,
                           G_PARAM_READWRITE |
                           G_PARAM_CONSTRUCT |
                           G_PARAM_STATIC_STRINGS);

    /**
     * LrgWipeTransition:softness:
     *
     * Edge softness (0.0 = hard edge, 1.0 = very soft).
     *
     * Since: 1.0
     */
    properties[PROP_SOFTNESS] =
        g_param_spec_float ("softness",
                            "Softness",
                            "Edge softness",
                            0.0f,
                            1.0f,
                            0.0f,
                            G_PARAM_READWRITE |
                            G_PARAM_CONSTRUCT |
                            G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_wipe_transition_init (LrgWipeTransition *self)
{
    self->direction = LRG_TRANSITION_DIRECTION_LEFT;
    self->softness = 0.0f;
}

/*
 * Public API
 */

/**
 * lrg_wipe_transition_new:
 *
 * Creates a new wipe transition with default settings (wipe from left).
 *
 * Returns: (transfer full): A new #LrgWipeTransition
 *
 * Since: 1.0
 */
LrgWipeTransition *
lrg_wipe_transition_new (void)
{
    return g_object_new (LRG_TYPE_WIPE_TRANSITION, NULL);
}

/**
 * lrg_wipe_transition_new_with_direction:
 * @direction: The wipe direction
 *
 * Creates a new wipe transition with the specified direction.
 *
 * Returns: (transfer full): A new #LrgWipeTransition
 *
 * Since: 1.0
 */
LrgWipeTransition *
lrg_wipe_transition_new_with_direction (LrgTransitionDirection direction)
{
    return g_object_new (LRG_TYPE_WIPE_TRANSITION,
                         "direction", direction,
                         NULL);
}

/**
 * lrg_wipe_transition_get_direction:
 * @self: A #LrgWipeTransition
 *
 * Gets the wipe direction.
 *
 * Returns: The #LrgTransitionDirection
 *
 * Since: 1.0
 */
LrgTransitionDirection
lrg_wipe_transition_get_direction (LrgWipeTransition *self)
{
    g_return_val_if_fail (LRG_IS_WIPE_TRANSITION (self), LRG_TRANSITION_DIRECTION_LEFT);

    return self->direction;
}

/**
 * lrg_wipe_transition_set_direction:
 * @self: A #LrgWipeTransition
 * @direction: The wipe direction
 *
 * Sets the wipe direction.
 *
 * Since: 1.0
 */
void
lrg_wipe_transition_set_direction (LrgWipeTransition      *self,
                                   LrgTransitionDirection  direction)
{
    g_return_if_fail (LRG_IS_WIPE_TRANSITION (self));

    if (self->direction != direction)
    {
        self->direction = direction;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_DIRECTION]);
    }
}

/**
 * lrg_wipe_transition_get_softness:
 * @self: A #LrgWipeTransition
 *
 * Gets the edge softness.
 *
 * Returns: Softness value (0.0 = hard edge, 1.0 = very soft)
 *
 * Since: 1.0
 */
gfloat
lrg_wipe_transition_get_softness (LrgWipeTransition *self)
{
    g_return_val_if_fail (LRG_IS_WIPE_TRANSITION (self), 0.0f);

    return self->softness;
}

/**
 * lrg_wipe_transition_set_softness:
 * @self: A #LrgWipeTransition
 * @softness: Edge softness (0.0 = hard, 1.0 = soft)
 *
 * Sets the edge softness.
 *
 * Since: 1.0
 */
void
lrg_wipe_transition_set_softness (LrgWipeTransition *self,
                                  gfloat             softness)
{
    g_return_if_fail (LRG_IS_WIPE_TRANSITION (self));

    softness = CLAMP (softness, 0.0f, 1.0f);

    if (self->softness != softness)
    {
        self->softness = softness;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SOFTNESS]);
    }
}
