/* lrg-animator-component.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Animation controller component.
 */

#include "config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_ECS

#include "lrg-animator-component.h"
#include "lrg-sprite-component.h"
#include "../lrg-game-object.h"
#include "../../lrg-log.h"

/* ==========================================================================
 * Animation Definition
 * ========================================================================== */

typedef struct
{
    gchar   *name;
    gint     start_frame;
    gint     frame_count;
    gfloat   fps;
    gboolean loop;
} AnimationDef;

static AnimationDef *
animation_def_new (const gchar *name,
                   gint         start_frame,
                   gint         frame_count,
                   gfloat       fps,
                   gboolean     loop)
{
    AnimationDef *def = g_new0 (AnimationDef, 1);

    def->name = g_strdup (name);
    def->start_frame = start_frame;
    def->frame_count = frame_count;
    def->fps = fps;
    def->loop = loop;

    return def;
}

static void
animation_def_free (AnimationDef *def)
{
    if (def != NULL)
    {
        g_free (def->name);
        g_free (def);
    }
}

/* ==========================================================================
 * Private Data
 * ========================================================================== */

typedef struct
{
    /* Spritesheet */
    GrlTexture   *texture;
    gint          frame_width;
    gint          frame_height;
    gint          cols;             /* Frames per row */

    /* Animation definitions */
    GHashTable   *animations;       /* name -> AnimationDef* */

    /* Playback state */
    gchar        *current_animation;
    gchar        *default_animation;
    gint          current_frame;    /* Absolute frame in spritesheet */
    gint          anim_frame;       /* Frame within current animation */
    gfloat        frame_time;       /* Time accumulator */
    gfloat        speed;            /* Speed multiplier */
    gboolean      playing;
    gboolean      finished;
} LrgAnimatorComponentPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (LrgAnimatorComponent, lrg_animator_component, LRG_TYPE_COMPONENT)

enum
{
    PROP_0,
    PROP_CURRENT_ANIMATION,
    PROP_PLAYING,
    PROP_SPEED,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

enum
{
    SIGNAL_ANIMATION_STARTED,
    SIGNAL_ANIMATION_FINISHED,
    SIGNAL_ANIMATION_LOOPED,
    N_SIGNALS
};

static guint signals[N_SIGNALS];

/* ==========================================================================
 * Component Virtual Methods
 * ========================================================================== */

static void
lrg_animator_component_update_impl (LrgComponent *component,
                                    gfloat        delta)
{
    LrgAnimatorComponent        *self = LRG_ANIMATOR_COMPONENT (component);
    LrgAnimatorComponentPrivate *priv = lrg_animator_component_get_instance_private (self);
    AnimationDef                *anim;
    gfloat                       frame_duration;

    if (!priv->playing || priv->current_animation == NULL)
    {
        return;
    }

    anim = g_hash_table_lookup (priv->animations, priv->current_animation);
    if (anim == NULL || anim->frame_count <= 0 || anim->fps <= 0)
    {
        return;
    }

    /* Accumulate time */
    priv->frame_time += delta * priv->speed;

    /* Calculate frame duration */
    frame_duration = 1.0f / anim->fps;

    /* Advance frames */
    while (priv->frame_time >= frame_duration)
    {
        priv->frame_time -= frame_duration;
        priv->anim_frame++;

        /* Check for animation end */
        if (priv->anim_frame >= anim->frame_count)
        {
            if (anim->loop)
            {
                priv->anim_frame = 0;
                g_signal_emit (self, signals[SIGNAL_ANIMATION_LOOPED], 0, priv->current_animation);
            }
            else
            {
                priv->anim_frame = anim->frame_count - 1;
                priv->playing = FALSE;
                priv->finished = TRUE;
                g_signal_emit (self, signals[SIGNAL_ANIMATION_FINISHED], 0, priv->current_animation);

                /* Transition to default animation if set */
                if (priv->default_animation != NULL)
                {
                    lrg_animator_component_play (self, priv->default_animation);
                }
                break;
            }
        }
    }

    /* Update current frame */
    priv->current_frame = anim->start_frame + priv->anim_frame;
}

/* ==========================================================================
 * GObject Implementation
 * ========================================================================== */

static void
lrg_animator_component_dispose (GObject *object)
{
    LrgAnimatorComponent        *self = LRG_ANIMATOR_COMPONENT (object);
    LrgAnimatorComponentPrivate *priv = lrg_animator_component_get_instance_private (self);

    g_clear_object (&priv->texture);

    G_OBJECT_CLASS (lrg_animator_component_parent_class)->dispose (object);
}

static void
lrg_animator_component_finalize (GObject *object)
{
    LrgAnimatorComponent        *self = LRG_ANIMATOR_COMPONENT (object);
    LrgAnimatorComponentPrivate *priv = lrg_animator_component_get_instance_private (self);

    g_clear_pointer (&priv->animations, g_hash_table_unref);
    g_clear_pointer (&priv->current_animation, g_free);
    g_clear_pointer (&priv->default_animation, g_free);

    G_OBJECT_CLASS (lrg_animator_component_parent_class)->finalize (object);
}

static void
lrg_animator_component_get_property (GObject    *object,
                                     guint       prop_id,
                                     GValue     *value,
                                     GParamSpec *pspec)
{
    LrgAnimatorComponent        *self = LRG_ANIMATOR_COMPONENT (object);
    LrgAnimatorComponentPrivate *priv = lrg_animator_component_get_instance_private (self);

    switch (prop_id)
    {
    case PROP_CURRENT_ANIMATION:
        g_value_set_string (value, priv->current_animation);
        break;
    case PROP_PLAYING:
        g_value_set_boolean (value, priv->playing);
        break;
    case PROP_SPEED:
        g_value_set_float (value, priv->speed);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_animator_component_set_property (GObject      *object,
                                     guint         prop_id,
                                     const GValue *value,
                                     GParamSpec   *pspec)
{
    LrgAnimatorComponent *self = LRG_ANIMATOR_COMPONENT (object);

    switch (prop_id)
    {
    case PROP_SPEED:
        lrg_animator_component_set_speed (self, g_value_get_float (value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_animator_component_class_init (LrgAnimatorComponentClass *klass)
{
    GObjectClass      *object_class = G_OBJECT_CLASS (klass);
    LrgComponentClass *component_class = LRG_COMPONENT_CLASS (klass);

    object_class->dispose = lrg_animator_component_dispose;
    object_class->finalize = lrg_animator_component_finalize;
    object_class->get_property = lrg_animator_component_get_property;
    object_class->set_property = lrg_animator_component_set_property;

    component_class->update = lrg_animator_component_update_impl;

    /**
     * LrgAnimatorComponent:current-animation:
     *
     * The name of the currently playing animation.
     */
    properties[PROP_CURRENT_ANIMATION] =
        g_param_spec_string ("current-animation",
                             "Current Animation",
                             "Name of the current animation",
                             NULL,
                             G_PARAM_READABLE |
                             G_PARAM_STATIC_STRINGS |
                             G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgAnimatorComponent:playing:
     *
     * Whether an animation is currently playing.
     */
    properties[PROP_PLAYING] =
        g_param_spec_boolean ("playing",
                              "Playing",
                              "Whether animation is playing",
                              FALSE,
                              G_PARAM_READABLE |
                              G_PARAM_STATIC_STRINGS |
                              G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgAnimatorComponent:speed:
     *
     * Playback speed multiplier (1.0 = normal).
     */
    properties[PROP_SPEED] =
        g_param_spec_float ("speed",
                            "Speed",
                            "Playback speed multiplier",
                            -G_MAXFLOAT, G_MAXFLOAT, 1.0f,
                            G_PARAM_READWRITE |
                            G_PARAM_STATIC_STRINGS |
                            G_PARAM_EXPLICIT_NOTIFY);

    g_object_class_install_properties (object_class, N_PROPS, properties);

    /**
     * LrgAnimatorComponent::animation-started:
     * @self: the animator component
     * @animation_name: name of the animation that started
     *
     * Emitted when an animation starts playing.
     */
    signals[SIGNAL_ANIMATION_STARTED] =
        g_signal_new ("animation-started",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (LrgAnimatorComponentClass, animation_started),
                      NULL, NULL, NULL,
                      G_TYPE_NONE, 1,
                      G_TYPE_STRING);

    /**
     * LrgAnimatorComponent::animation-finished:
     * @self: the animator component
     * @animation_name: name of the animation that finished
     *
     * Emitted when a non-looping animation finishes.
     */
    signals[SIGNAL_ANIMATION_FINISHED] =
        g_signal_new ("animation-finished",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (LrgAnimatorComponentClass, animation_finished),
                      NULL, NULL, NULL,
                      G_TYPE_NONE, 1,
                      G_TYPE_STRING);

    /**
     * LrgAnimatorComponent::animation-looped:
     * @self: the animator component
     * @animation_name: name of the animation that looped
     *
     * Emitted each time a looping animation loops.
     */
    signals[SIGNAL_ANIMATION_LOOPED] =
        g_signal_new ("animation-looped",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (LrgAnimatorComponentClass, animation_looped),
                      NULL, NULL, NULL,
                      G_TYPE_NONE, 1,
                      G_TYPE_STRING);
}

static void
lrg_animator_component_init (LrgAnimatorComponent *self)
{
    LrgAnimatorComponentPrivate *priv = lrg_animator_component_get_instance_private (self);

    priv->animations = g_hash_table_new_full (g_str_hash, g_str_equal,
                                              NULL,
                                              (GDestroyNotify)animation_def_free);
    priv->texture = NULL;
    priv->frame_width = 0;
    priv->frame_height = 0;
    priv->cols = 0;
    priv->current_animation = NULL;
    priv->default_animation = NULL;
    priv->current_frame = 0;
    priv->anim_frame = 0;
    priv->frame_time = 0.0f;
    priv->speed = 1.0f;
    priv->playing = FALSE;
    priv->finished = FALSE;
}

/* ==========================================================================
 * Public API - Construction
 * ========================================================================== */

/**
 * lrg_animator_component_new:
 *
 * Creates a new animator component.
 *
 * Returns: (transfer full): A new #LrgAnimatorComponent
 */
LrgAnimatorComponent *
lrg_animator_component_new (void)
{
    return g_object_new (LRG_TYPE_ANIMATOR_COMPONENT, NULL);
}

/**
 * lrg_animator_component_new_with_texture:
 * @texture: The spritesheet texture
 * @frame_width: Width of each frame
 * @frame_height: Height of each frame
 *
 * Creates a new animator component with a spritesheet.
 *
 * Returns: (transfer full): A new #LrgAnimatorComponent
 */
LrgAnimatorComponent *
lrg_animator_component_new_with_texture (GrlTexture *texture,
                                         gint        frame_width,
                                         gint        frame_height)
{
    LrgAnimatorComponent *self = lrg_animator_component_new ();

    lrg_animator_component_set_texture (self, texture, frame_width, frame_height);

    return self;
}

/* ==========================================================================
 * Public API - Spritesheet Configuration
 * ========================================================================== */

/**
 * lrg_animator_component_set_texture:
 * @self: an #LrgAnimatorComponent
 * @texture: The spritesheet texture
 * @frame_width: Width of each frame
 * @frame_height: Height of each frame
 *
 * Sets the spritesheet texture and frame dimensions.
 */
void
lrg_animator_component_set_texture (LrgAnimatorComponent *self,
                                    GrlTexture           *texture,
                                    gint                  frame_width,
                                    gint                  frame_height)
{
    LrgAnimatorComponentPrivate *priv;
    gint                         tex_width;

    g_return_if_fail (LRG_IS_ANIMATOR_COMPONENT (self));

    priv = lrg_animator_component_get_instance_private (self);

    g_set_object (&priv->texture, texture);
    priv->frame_width = frame_width;
    priv->frame_height = frame_height;

    /* Calculate columns for frame rect calculation */
    if (texture != NULL && frame_width > 0)
    {
        tex_width = grl_texture_get_width (texture);
        priv->cols = tex_width / frame_width;
    }
    else
    {
        priv->cols = 0;
    }
}

/**
 * lrg_animator_component_get_texture:
 * @self: an #LrgAnimatorComponent
 *
 * Gets the spritesheet texture.
 *
 * Returns: (transfer none) (nullable): The texture, or %NULL
 */
GrlTexture *
lrg_animator_component_get_texture (LrgAnimatorComponent *self)
{
    LrgAnimatorComponentPrivate *priv;

    g_return_val_if_fail (LRG_IS_ANIMATOR_COMPONENT (self), NULL);

    priv = lrg_animator_component_get_instance_private (self);
    return priv->texture;
}

/**
 * lrg_animator_component_get_frame_width:
 * @self: an #LrgAnimatorComponent
 *
 * Gets the frame width.
 *
 * Returns: Frame width in pixels
 */
gint
lrg_animator_component_get_frame_width (LrgAnimatorComponent *self)
{
    LrgAnimatorComponentPrivate *priv;

    g_return_val_if_fail (LRG_IS_ANIMATOR_COMPONENT (self), 0);

    priv = lrg_animator_component_get_instance_private (self);
    return priv->frame_width;
}

/**
 * lrg_animator_component_get_frame_height:
 * @self: an #LrgAnimatorComponent
 *
 * Gets the frame height.
 *
 * Returns: Frame height in pixels
 */
gint
lrg_animator_component_get_frame_height (LrgAnimatorComponent *self)
{
    LrgAnimatorComponentPrivate *priv;

    g_return_val_if_fail (LRG_IS_ANIMATOR_COMPONENT (self), 0);

    priv = lrg_animator_component_get_instance_private (self);
    return priv->frame_height;
}

/* ==========================================================================
 * Public API - Animation Definition
 * ========================================================================== */

/**
 * lrg_animator_component_add_animation:
 * @self: an #LrgAnimatorComponent
 * @name: Unique animation name
 * @start_frame: First frame index (0-based)
 * @frame_count: Number of frames in this animation
 * @fps: Frames per second
 * @loop: Whether to loop
 *
 * Adds a named animation with the given settings.
 *
 * Returns: %TRUE if added successfully
 */
gboolean
lrg_animator_component_add_animation (LrgAnimatorComponent *self,
                                      const gchar          *name,
                                      gint                  start_frame,
                                      gint                  frame_count,
                                      gfloat                fps,
                                      gboolean              loop)
{
    LrgAnimatorComponentPrivate *priv;
    AnimationDef                *def;

    g_return_val_if_fail (LRG_IS_ANIMATOR_COMPONENT (self), FALSE);
    g_return_val_if_fail (name != NULL && name[0] != '\0', FALSE);
    g_return_val_if_fail (frame_count > 0, FALSE);
    g_return_val_if_fail (fps > 0, FALSE);

    priv = lrg_animator_component_get_instance_private (self);

    if (g_hash_table_contains (priv->animations, name))
    {
        lrg_debug (LRG_LOG_DOMAIN_ECS, "Animation '%s' already exists, not adding", name);
        return FALSE;
    }

    def = animation_def_new (name, start_frame, frame_count, fps, loop);
    g_hash_table_insert (priv->animations, def->name, def);

    lrg_debug (LRG_LOG_DOMAIN_ECS, "Added animation '%s' (frames %d-%d, %.1f fps, %s)",
               name, start_frame, start_frame + frame_count - 1, fps,
               loop ? "looping" : "one-shot");

    return TRUE;
}

/**
 * lrg_animator_component_remove_animation:
 * @self: an #LrgAnimatorComponent
 * @name: Animation name to remove
 *
 * Removes an animation by name.
 *
 * Returns: %TRUE if removed
 */
gboolean
lrg_animator_component_remove_animation (LrgAnimatorComponent *self,
                                         const gchar          *name)
{
    LrgAnimatorComponentPrivate *priv;

    g_return_val_if_fail (LRG_IS_ANIMATOR_COMPONENT (self), FALSE);
    g_return_val_if_fail (name != NULL, FALSE);

    priv = lrg_animator_component_get_instance_private (self);

    /* Stop if removing current animation */
    if (priv->current_animation != NULL && g_strcmp0 (priv->current_animation, name) == 0)
    {
        lrg_animator_component_stop (self);
    }

    return g_hash_table_remove (priv->animations, name);
}

/**
 * lrg_animator_component_has_animation:
 * @self: an #LrgAnimatorComponent
 * @name: Animation name to check
 *
 * Checks if an animation exists.
 *
 * Returns: %TRUE if the animation exists
 */
gboolean
lrg_animator_component_has_animation (LrgAnimatorComponent *self,
                                      const gchar          *name)
{
    LrgAnimatorComponentPrivate *priv;

    g_return_val_if_fail (LRG_IS_ANIMATOR_COMPONENT (self), FALSE);
    g_return_val_if_fail (name != NULL, FALSE);

    priv = lrg_animator_component_get_instance_private (self);

    return g_hash_table_contains (priv->animations, name);
}

/**
 * lrg_animator_component_get_animation_names:
 * @self: an #LrgAnimatorComponent
 *
 * Gets a list of all animation names.
 *
 * Returns: (transfer container) (element-type utf8): List of animation names
 */
GList *
lrg_animator_component_get_animation_names (LrgAnimatorComponent *self)
{
    LrgAnimatorComponentPrivate *priv;

    g_return_val_if_fail (LRG_IS_ANIMATOR_COMPONENT (self), NULL);

    priv = lrg_animator_component_get_instance_private (self);

    return g_hash_table_get_keys (priv->animations);
}

/**
 * lrg_animator_component_clear_animations:
 * @self: an #LrgAnimatorComponent
 *
 * Removes all animations.
 */
void
lrg_animator_component_clear_animations (LrgAnimatorComponent *self)
{
    LrgAnimatorComponentPrivate *priv;

    g_return_if_fail (LRG_IS_ANIMATOR_COMPONENT (self));

    priv = lrg_animator_component_get_instance_private (self);

    lrg_animator_component_stop (self);
    g_hash_table_remove_all (priv->animations);
}

/* ==========================================================================
 * Public API - Playback Control
 * ========================================================================== */

/**
 * lrg_animator_component_play:
 * @self: an #LrgAnimatorComponent
 * @name: Animation name to play
 *
 * Starts playing an animation from the beginning.
 *
 * Returns: %TRUE if animation started
 */
gboolean
lrg_animator_component_play (LrgAnimatorComponent *self,
                             const gchar          *name)
{
    LrgAnimatorComponentPrivate *priv;
    AnimationDef                *anim;

    g_return_val_if_fail (LRG_IS_ANIMATOR_COMPONENT (self), FALSE);
    g_return_val_if_fail (name != NULL, FALSE);

    priv = lrg_animator_component_get_instance_private (self);

    anim = g_hash_table_lookup (priv->animations, name);
    if (anim == NULL)
    {
        lrg_debug (LRG_LOG_DOMAIN_ECS, "Animation '%s' not found, cannot play", name);
        return FALSE;
    }

    /* Set current animation */
    g_free (priv->current_animation);
    priv->current_animation = g_strdup (name);

    /* Reset state */
    priv->anim_frame = 0;
    priv->current_frame = anim->start_frame;
    priv->frame_time = 0.0f;
    priv->playing = TRUE;
    priv->finished = FALSE;

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_CURRENT_ANIMATION]);
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_PLAYING]);

    g_signal_emit (self, signals[SIGNAL_ANIMATION_STARTED], 0, name);

    return TRUE;
}

/**
 * lrg_animator_component_play_if_different:
 * @self: an #LrgAnimatorComponent
 * @name: Animation name to play
 *
 * Starts playing an animation only if it's not already playing.
 *
 * Returns: %TRUE if animation started or already playing
 */
gboolean
lrg_animator_component_play_if_different (LrgAnimatorComponent *self,
                                          const gchar          *name)
{
    LrgAnimatorComponentPrivate *priv;

    g_return_val_if_fail (LRG_IS_ANIMATOR_COMPONENT (self), FALSE);
    g_return_val_if_fail (name != NULL, FALSE);

    priv = lrg_animator_component_get_instance_private (self);

    /* Already playing this animation? */
    if (priv->current_animation != NULL &&
        g_strcmp0 (priv->current_animation, name) == 0 &&
        priv->playing)
    {
        return TRUE;
    }

    return lrg_animator_component_play (self, name);
}

/**
 * lrg_animator_component_stop:
 * @self: an #LrgAnimatorComponent
 *
 * Stops the current animation and resets to the first frame.
 */
void
lrg_animator_component_stop (LrgAnimatorComponent *self)
{
    LrgAnimatorComponentPrivate *priv;
    AnimationDef                *anim;

    g_return_if_fail (LRG_IS_ANIMATOR_COMPONENT (self));

    priv = lrg_animator_component_get_instance_private (self);

    if (priv->current_animation != NULL)
    {
        anim = g_hash_table_lookup (priv->animations, priv->current_animation);
        if (anim != NULL)
        {
            priv->current_frame = anim->start_frame;
        }
    }

    priv->anim_frame = 0;
    priv->frame_time = 0.0f;
    priv->playing = FALSE;
    priv->finished = FALSE;

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_PLAYING]);
}

/**
 * lrg_animator_component_pause:
 * @self: an #LrgAnimatorComponent
 *
 * Pauses the current animation.
 */
void
lrg_animator_component_pause (LrgAnimatorComponent *self)
{
    LrgAnimatorComponentPrivate *priv;

    g_return_if_fail (LRG_IS_ANIMATOR_COMPONENT (self));

    priv = lrg_animator_component_get_instance_private (self);

    if (priv->playing)
    {
        priv->playing = FALSE;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_PLAYING]);
    }
}

/**
 * lrg_animator_component_resume:
 * @self: an #LrgAnimatorComponent
 *
 * Resumes a paused animation.
 */
void
lrg_animator_component_resume (LrgAnimatorComponent *self)
{
    LrgAnimatorComponentPrivate *priv;

    g_return_if_fail (LRG_IS_ANIMATOR_COMPONENT (self));

    priv = lrg_animator_component_get_instance_private (self);

    if (!priv->playing && priv->current_animation != NULL && !priv->finished)
    {
        priv->playing = TRUE;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_PLAYING]);
    }
}

/* ==========================================================================
 * Public API - State Queries
 * ========================================================================== */

/**
 * lrg_animator_component_get_current_animation:
 * @self: an #LrgAnimatorComponent
 *
 * Gets the name of the current animation.
 *
 * Returns: (nullable): Current animation name
 */
const gchar *
lrg_animator_component_get_current_animation (LrgAnimatorComponent *self)
{
    LrgAnimatorComponentPrivate *priv;

    g_return_val_if_fail (LRG_IS_ANIMATOR_COMPONENT (self), NULL);

    priv = lrg_animator_component_get_instance_private (self);
    return priv->current_animation;
}

/**
 * lrg_animator_component_is_playing:
 * @self: an #LrgAnimatorComponent
 *
 * Checks if an animation is currently playing.
 *
 * Returns: %TRUE if playing
 */
gboolean
lrg_animator_component_is_playing (LrgAnimatorComponent *self)
{
    LrgAnimatorComponentPrivate *priv;

    g_return_val_if_fail (LRG_IS_ANIMATOR_COMPONENT (self), FALSE);

    priv = lrg_animator_component_get_instance_private (self);
    return priv->playing;
}

/**
 * lrg_animator_component_is_finished:
 * @self: an #LrgAnimatorComponent
 *
 * Checks if a non-looping animation has finished.
 *
 * Returns: %TRUE if finished
 */
gboolean
lrg_animator_component_is_finished (LrgAnimatorComponent *self)
{
    LrgAnimatorComponentPrivate *priv;

    g_return_val_if_fail (LRG_IS_ANIMATOR_COMPONENT (self), FALSE);

    priv = lrg_animator_component_get_instance_private (self);
    return priv->finished;
}

/**
 * lrg_animator_component_get_current_frame:
 * @self: an #LrgAnimatorComponent
 *
 * Gets the current frame index.
 *
 * Returns: Current frame index
 */
gint
lrg_animator_component_get_current_frame (LrgAnimatorComponent *self)
{
    LrgAnimatorComponentPrivate *priv;

    g_return_val_if_fail (LRG_IS_ANIMATOR_COMPONENT (self), 0);

    priv = lrg_animator_component_get_instance_private (self);
    return priv->current_frame;
}

/**
 * lrg_animator_component_get_current_frame_rect:
 * @self: an #LrgAnimatorComponent
 *
 * Gets the source rectangle for the current frame.
 *
 * Returns: (transfer full) (nullable): Current frame rectangle
 */
GrlRectangle *
lrg_animator_component_get_current_frame_rect (LrgAnimatorComponent *self)
{
    LrgAnimatorComponentPrivate *priv;
    gint                         row;
    gint                         col;
    gfloat                       x;
    gfloat                       y;

    g_return_val_if_fail (LRG_IS_ANIMATOR_COMPONENT (self), NULL);

    priv = lrg_animator_component_get_instance_private (self);

    if (priv->cols <= 0 || priv->frame_width <= 0 || priv->frame_height <= 0)
    {
        return NULL;
    }

    /* Calculate position in grid */
    row = priv->current_frame / priv->cols;
    col = priv->current_frame % priv->cols;

    x = (gfloat)(col * priv->frame_width);
    y = (gfloat)(row * priv->frame_height);

    return grl_rectangle_new (x, y, (gfloat)priv->frame_width, (gfloat)priv->frame_height);
}

/* ==========================================================================
 * Public API - Speed Control
 * ========================================================================== */

/**
 * lrg_animator_component_get_speed:
 * @self: an #LrgAnimatorComponent
 *
 * Gets the playback speed multiplier.
 *
 * Returns: Speed multiplier
 */
gfloat
lrg_animator_component_get_speed (LrgAnimatorComponent *self)
{
    LrgAnimatorComponentPrivate *priv;

    g_return_val_if_fail (LRG_IS_ANIMATOR_COMPONENT (self), 1.0f);

    priv = lrg_animator_component_get_instance_private (self);
    return priv->speed;
}

/**
 * lrg_animator_component_set_speed:
 * @self: an #LrgAnimatorComponent
 * @speed: Speed multiplier
 *
 * Sets the playback speed multiplier.
 */
void
lrg_animator_component_set_speed (LrgAnimatorComponent *self,
                                  gfloat                speed)
{
    LrgAnimatorComponentPrivate *priv;

    g_return_if_fail (LRG_IS_ANIMATOR_COMPONENT (self));

    priv = lrg_animator_component_get_instance_private (self);

    if (priv->speed != speed)
    {
        priv->speed = speed;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SPEED]);
    }
}

/* ==========================================================================
 * Public API - Transition Helpers
 * ========================================================================== */

/**
 * lrg_animator_component_set_default_animation:
 * @self: an #LrgAnimatorComponent
 * @name: (nullable): Default animation name
 *
 * Sets the default animation to play when current animation finishes.
 */
void
lrg_animator_component_set_default_animation (LrgAnimatorComponent *self,
                                              const gchar          *name)
{
    LrgAnimatorComponentPrivate *priv;

    g_return_if_fail (LRG_IS_ANIMATOR_COMPONENT (self));

    priv = lrg_animator_component_get_instance_private (self);

    g_free (priv->default_animation);
    priv->default_animation = g_strdup (name);
}

/**
 * lrg_animator_component_get_default_animation:
 * @self: an #LrgAnimatorComponent
 *
 * Gets the default animation name.
 *
 * Returns: (nullable): Default animation name
 */
const gchar *
lrg_animator_component_get_default_animation (LrgAnimatorComponent *self)
{
    LrgAnimatorComponentPrivate *priv;

    g_return_val_if_fail (LRG_IS_ANIMATOR_COMPONENT (self), NULL);

    priv = lrg_animator_component_get_instance_private (self);
    return priv->default_animation;
}
