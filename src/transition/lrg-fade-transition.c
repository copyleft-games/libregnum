/* lrg-fade-transition.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Fade transition that fades to/from a color.
 */

#include "lrg-fade-transition.h"
#include "../tween/lrg-easing.h"
#include "../lrg-log.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_TRANSITION

/**
 * LrgFadeTransition:
 *
 * A transition that fades the screen to a solid color (typically black)
 * and then fades back in to reveal the new scene.
 *
 * The fade transition is the simplest and most commonly used transition:
 *
 * 1. **OUT phase**: Current scene fades to the target color
 * 2. **HOLD phase**: Screen shows solid color (scene switch occurs here)
 * 3. **IN phase**: Target color fades to reveal new scene
 *
 * ## Example usage
 *
 * ```c
 * LrgFadeTransition *fade = lrg_fade_transition_new ();
 * lrg_transition_set_duration (LRG_TRANSITION (fade), 1.0f);
 * lrg_transition_start (LRG_TRANSITION (fade));
 * ```
 *
 * ## Custom fade color
 *
 * ```c
 * LrgFadeTransition *fade = lrg_fade_transition_new_with_color (255, 255, 255);
 * // Fades to white instead of black
 * ```
 *
 * Since: 1.0
 */

struct _LrgFadeTransition
{
    LrgTransition parent_instance;

    /* Fade color */
    guint8 red;
    guint8 green;
    guint8 blue;
};

enum
{
    PROP_0,
    PROP_RED,
    PROP_GREEN,
    PROP_BLUE,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

G_DEFINE_FINAL_TYPE (LrgFadeTransition, lrg_fade_transition, LRG_TYPE_TRANSITION)

/*
 * LrgTransition virtual method implementations
 */

static gboolean
lrg_fade_transition_initialize (LrgTransition  *transition,
                                guint           width,
                                guint           height,
                                GError        **error)
{
    /*
     * Fade transition doesn't need any special resources.
     * More complex transitions would allocate shaders, textures, etc. here.
     */
    (void) transition;
    (void) width;
    (void) height;
    (void) error;

    lrg_debug (LRG_LOG_DOMAIN_TRANSITION, "Fade transition initialized (viewport: %ux%u)", width, height);

    return TRUE;
}

static void
lrg_fade_transition_shutdown (LrgTransition *transition)
{
    (void) transition;

    lrg_debug (LRG_LOG_DOMAIN_TRANSITION, "Fade transition shutdown");
}

static void
lrg_fade_transition_start (LrgTransition *transition)
{
    LrgFadeTransition *self;

    self = LRG_FADE_TRANSITION (transition);

    lrg_debug (LRG_LOG_DOMAIN_TRANSITION, "Fade transition started (color: #%02x%02x%02x)",
                   self->red, self->green, self->blue);
}

static void
lrg_fade_transition_update (LrgTransition *transition,
                            gfloat         delta_time)
{
    /* No additional update logic needed for fade */
    (void) transition;
    (void) delta_time;
}

static void
lrg_fade_transition_render (LrgTransition *transition,
                            guint          old_scene_texture,
                            guint          new_scene_texture,
                            guint          width,
                            guint          height)
{
    LrgFadeTransition *self;
    LrgTransitionState state;
    gfloat phase_progress;
    gfloat eased_progress;
    LrgEasingType easing;
    gfloat alpha;

    self = LRG_FADE_TRANSITION (transition);
    state = lrg_transition_get_state (transition);
    phase_progress = lrg_transition_get_phase_progress (transition);
    easing = lrg_transition_get_easing (transition);
    eased_progress = lrg_easing_apply (easing, phase_progress);

    /*
     * Calculate the fade alpha based on current phase:
     * - OUT: alpha goes from 0 to 1 (fade TO color)
     * - HOLD: alpha stays at 1 (solid color)
     * - IN: alpha goes from 1 to 0 (fade FROM color)
     */
    switch (state)
    {
    case LRG_TRANSITION_STATE_OUT:
        alpha = eased_progress;
        break;

    case LRG_TRANSITION_STATE_HOLD:
        alpha = 1.0f;
        break;

    case LRG_TRANSITION_STATE_IN:
        alpha = 1.0f - eased_progress;
        break;

    case LRG_TRANSITION_STATE_IDLE:
    case LRG_TRANSITION_STATE_COMPLETE:
    default:
        alpha = 0.0f;
        break;
    }

    /*
     * Rendering strategy:
     * 1. Draw the appropriate scene (old during OUT, new during IN)
     * 2. Overlay the fade color with calculated alpha
     *
     * Note: The actual drawing is done via the graphics subsystem.
     * This function calculates what SHOULD be drawn. The transition
     * manager or game state manager calls the graphics API.
     *
     * For now, we store the calculated values. In a full implementation,
     * this would use graylib's rendering functions:
     *
     * During OUT phase:
     *   - Draw texture from old_scene_texture
     *   - Draw rectangle with color (r,g,b,alpha*255)
     *
     * During HOLD phase:
     *   - Draw solid rectangle with color (r,g,b,255)
     *
     * During IN phase:
     *   - Draw texture from new_scene_texture
     *   - Draw rectangle with color (r,g,b,alpha*255)
     */

    (void) self;
    (void) alpha;
    (void) old_scene_texture;
    (void) new_scene_texture;
    (void) width;
    (void) height;

    /*
     * TODO: Integrate with graylib rendering when graphics subsystem is ready.
     * For now, this serves as the interface that transition managers will use.
     *
     * Example integration:
     *
     * GrlColor fade_color = { self->red, self->green, self->blue, (guint8)(alpha * 255) };
     *
     * if (state == LRG_TRANSITION_STATE_OUT)
     *     grl_draw_texture (old_scene_texture, 0, 0, GRL_WHITE);
     * else if (state == LRG_TRANSITION_STATE_IN)
     *     grl_draw_texture (new_scene_texture, 0, 0, GRL_WHITE);
     *
     * grl_draw_rectangle (0, 0, width, height, fade_color);
     */
}

static void
lrg_fade_transition_reset (LrgTransition *transition)
{
    /* No additional reset logic needed */
    (void) transition;

    lrg_debug (LRG_LOG_DOMAIN_TRANSITION, "Fade transition reset");
}

/*
 * GObject virtual methods
 */

static void
lrg_fade_transition_get_property (GObject    *object,
                                  guint       prop_id,
                                  GValue     *value,
                                  GParamSpec *pspec)
{
    LrgFadeTransition *self;

    self = LRG_FADE_TRANSITION (object);

    switch (prop_id)
    {
    case PROP_RED:
        g_value_set_uint (value, self->red);
        break;

    case PROP_GREEN:
        g_value_set_uint (value, self->green);
        break;

    case PROP_BLUE:
        g_value_set_uint (value, self->blue);
        break;

    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
lrg_fade_transition_set_property (GObject      *object,
                                  guint         prop_id,
                                  const GValue *value,
                                  GParamSpec   *pspec)
{
    LrgFadeTransition *self;

    self = LRG_FADE_TRANSITION (object);

    switch (prop_id)
    {
    case PROP_RED:
        self->red = (guint8) g_value_get_uint (value);
        break;

    case PROP_GREEN:
        self->green = (guint8) g_value_get_uint (value);
        break;

    case PROP_BLUE:
        self->blue = (guint8) g_value_get_uint (value);
        break;

    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
lrg_fade_transition_class_init (LrgFadeTransitionClass *klass)
{
    GObjectClass *object_class;
    LrgTransitionClass *transition_class;

    object_class = G_OBJECT_CLASS (klass);
    object_class->get_property = lrg_fade_transition_get_property;
    object_class->set_property = lrg_fade_transition_set_property;

    transition_class = LRG_TRANSITION_CLASS (klass);
    transition_class->initialize = lrg_fade_transition_initialize;
    transition_class->shutdown = lrg_fade_transition_shutdown;
    transition_class->start = lrg_fade_transition_start;
    transition_class->update = lrg_fade_transition_update;
    transition_class->render = lrg_fade_transition_render;
    transition_class->reset = lrg_fade_transition_reset;

    /**
     * LrgFadeTransition:red:
     *
     * Red component of the fade color (0-255).
     *
     * Since: 1.0
     */
    properties[PROP_RED] =
        g_param_spec_uint ("red",
                           "Red",
                           "Red component of the fade color",
                           0,
                           255,
                           0,
                           G_PARAM_READWRITE |
                           G_PARAM_CONSTRUCT |
                           G_PARAM_STATIC_STRINGS);

    /**
     * LrgFadeTransition:green:
     *
     * Green component of the fade color (0-255).
     *
     * Since: 1.0
     */
    properties[PROP_GREEN] =
        g_param_spec_uint ("green",
                           "Green",
                           "Green component of the fade color",
                           0,
                           255,
                           0,
                           G_PARAM_READWRITE |
                           G_PARAM_CONSTRUCT |
                           G_PARAM_STATIC_STRINGS);

    /**
     * LrgFadeTransition:blue:
     *
     * Blue component of the fade color (0-255).
     *
     * Since: 1.0
     */
    properties[PROP_BLUE] =
        g_param_spec_uint ("blue",
                           "Blue",
                           "Blue component of the fade color",
                           0,
                           255,
                           0,
                           G_PARAM_READWRITE |
                           G_PARAM_CONSTRUCT |
                           G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_fade_transition_init (LrgFadeTransition *self)
{
    /* Default to fade to black */
    self->red = 0;
    self->green = 0;
    self->blue = 0;
}

/*
 * Public API
 */

/**
 * lrg_fade_transition_new:
 *
 * Creates a new fade transition with default settings (fade to black).
 *
 * Returns: (transfer full): A new #LrgFadeTransition
 *
 * Since: 1.0
 */
LrgFadeTransition *
lrg_fade_transition_new (void)
{
    return g_object_new (LRG_TYPE_FADE_TRANSITION, NULL);
}

/**
 * lrg_fade_transition_new_with_color:
 * @r: Red component (0-255)
 * @g: Green component (0-255)
 * @b: Blue component (0-255)
 *
 * Creates a new fade transition that fades to/from the specified color.
 *
 * Returns: (transfer full): A new #LrgFadeTransition
 *
 * Since: 1.0
 */
LrgFadeTransition *
lrg_fade_transition_new_with_color (guint8 r,
                                    guint8 g,
                                    guint8 b)
{
    return g_object_new (LRG_TYPE_FADE_TRANSITION,
                         "red", (guint) r,
                         "green", (guint) g,
                         "blue", (guint) b,
                         NULL);
}

/**
 * lrg_fade_transition_get_color:
 * @self: A #LrgFadeTransition
 * @r: (out) (nullable): Return location for red component
 * @g: (out) (nullable): Return location for green component
 * @b: (out) (nullable): Return location for blue component
 *
 * Gets the fade color.
 *
 * Since: 1.0
 */
void
lrg_fade_transition_get_color (LrgFadeTransition *self,
                               guint8            *r,
                               guint8            *g,
                               guint8            *b)
{
    g_return_if_fail (LRG_IS_FADE_TRANSITION (self));

    if (r != NULL)
        *r = self->red;
    if (g != NULL)
        *g = self->green;
    if (b != NULL)
        *b = self->blue;
}

/**
 * lrg_fade_transition_set_color:
 * @self: A #LrgFadeTransition
 * @r: Red component (0-255)
 * @g: Green component (0-255)
 * @b: Blue component (0-255)
 *
 * Sets the fade color.
 *
 * Since: 1.0
 */
void
lrg_fade_transition_set_color (LrgFadeTransition *self,
                               guint8             r,
                               guint8             g,
                               guint8             b)
{
    g_return_if_fail (LRG_IS_FADE_TRANSITION (self));

    g_object_freeze_notify (G_OBJECT (self));

    if (self->red != r)
    {
        self->red = r;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_RED]);
    }

    if (self->green != g)
    {
        self->green = g;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_GREEN]);
    }

    if (self->blue != b)
    {
        self->blue = b;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_BLUE]);
    }

    g_object_thaw_notify (G_OBJECT (self));
}
