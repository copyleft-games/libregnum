/* lrg-animator-component.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Animation controller component.
 *
 * LrgAnimatorComponent manages sprite animations for game objects.
 * It supports multiple named animations with individual settings,
 * and can drive animation playback for sprite components.
 *
 * Animations are defined as frame ranges within a spritesheet, and
 * can be configured with individual speed and loop settings.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <graylib.h>

#include "../../lrg-version.h"
#include "../../lrg-types.h"
#include "../lrg-component.h"

G_BEGIN_DECLS

#define LRG_TYPE_ANIMATOR_COMPONENT (lrg_animator_component_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_DERIVABLE_TYPE (LrgAnimatorComponent, lrg_animator_component,
                          LRG, ANIMATOR_COMPONENT, LrgComponent)

/**
 * LrgAnimatorComponentClass:
 * @parent_class: The parent class
 * @animation_started: Signal handler called when an animation starts
 * @animation_finished: Signal handler called when a non-looping animation finishes
 * @animation_looped: Signal handler called each time a looping animation loops
 *
 * The class structure for #LrgAnimatorComponent.
 */
struct _LrgAnimatorComponentClass
{
    LrgComponentClass parent_class;

    /* Signals */
    void (*animation_started)  (LrgAnimatorComponent *self,
                                const gchar          *animation_name);
    void (*animation_finished) (LrgAnimatorComponent *self,
                                const gchar          *animation_name);
    void (*animation_looped)   (LrgAnimatorComponent *self,
                                const gchar          *animation_name);

    /*< private >*/
    gpointer _reserved[8];
};

/*
 * Construction
 */

/**
 * lrg_animator_component_new:
 *
 * Creates a new animator component.
 *
 * Returns: (transfer full): A new #LrgAnimatorComponent
 */
LRG_AVAILABLE_IN_ALL
LrgAnimatorComponent * lrg_animator_component_new               (void);

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
LRG_AVAILABLE_IN_ALL
LrgAnimatorComponent * lrg_animator_component_new_with_texture  (GrlTexture *texture,
                                                                  gint        frame_width,
                                                                  gint        frame_height);

/*
 * Spritesheet Configuration
 */

/**
 * lrg_animator_component_set_texture:
 * @self: an #LrgAnimatorComponent
 * @texture: The spritesheet texture
 * @frame_width: Width of each frame
 * @frame_height: Height of each frame
 *
 * Sets the spritesheet texture and frame dimensions.
 */
LRG_AVAILABLE_IN_ALL
void         lrg_animator_component_set_texture         (LrgAnimatorComponent *self,
                                                         GrlTexture           *texture,
                                                         gint                  frame_width,
                                                         gint                  frame_height);

/**
 * lrg_animator_component_get_texture:
 * @self: an #LrgAnimatorComponent
 *
 * Gets the spritesheet texture.
 *
 * Returns: (transfer none) (nullable): The texture, or %NULL
 */
LRG_AVAILABLE_IN_ALL
GrlTexture * lrg_animator_component_get_texture         (LrgAnimatorComponent *self);

/**
 * lrg_animator_component_get_frame_width:
 * @self: an #LrgAnimatorComponent
 *
 * Gets the frame width.
 *
 * Returns: Frame width in pixels
 */
LRG_AVAILABLE_IN_ALL
gint         lrg_animator_component_get_frame_width     (LrgAnimatorComponent *self);

/**
 * lrg_animator_component_get_frame_height:
 * @self: an #LrgAnimatorComponent
 *
 * Gets the frame height.
 *
 * Returns: Frame height in pixels
 */
LRG_AVAILABLE_IN_ALL
gint         lrg_animator_component_get_frame_height    (LrgAnimatorComponent *self);

/*
 * Animation Definition
 */

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
 * Frame indices are based on left-to-right, top-to-bottom order.
 *
 * Returns: %TRUE if added successfully, %FALSE if name already exists
 */
LRG_AVAILABLE_IN_ALL
gboolean     lrg_animator_component_add_animation       (LrgAnimatorComponent *self,
                                                         const gchar          *name,
                                                         gint                  start_frame,
                                                         gint                  frame_count,
                                                         gfloat                fps,
                                                         gboolean              loop);

/**
 * lrg_animator_component_remove_animation:
 * @self: an #LrgAnimatorComponent
 * @name: Animation name to remove
 *
 * Removes an animation by name.
 *
 * Returns: %TRUE if removed, %FALSE if not found
 */
LRG_AVAILABLE_IN_ALL
gboolean     lrg_animator_component_remove_animation    (LrgAnimatorComponent *self,
                                                         const gchar          *name);

/**
 * lrg_animator_component_has_animation:
 * @self: an #LrgAnimatorComponent
 * @name: Animation name to check
 *
 * Checks if an animation exists.
 *
 * Returns: %TRUE if the animation exists
 */
LRG_AVAILABLE_IN_ALL
gboolean     lrg_animator_component_has_animation       (LrgAnimatorComponent *self,
                                                         const gchar          *name);

/**
 * lrg_animator_component_get_animation_names:
 * @self: an #LrgAnimatorComponent
 *
 * Gets a list of all animation names.
 *
 * Returns: (transfer container) (element-type utf8): List of animation names
 */
LRG_AVAILABLE_IN_ALL
GList *      lrg_animator_component_get_animation_names (LrgAnimatorComponent *self);

/**
 * lrg_animator_component_clear_animations:
 * @self: an #LrgAnimatorComponent
 *
 * Removes all animations.
 */
LRG_AVAILABLE_IN_ALL
void         lrg_animator_component_clear_animations    (LrgAnimatorComponent *self);

/*
 * Playback Control
 */

/**
 * lrg_animator_component_play:
 * @self: an #LrgAnimatorComponent
 * @name: Animation name to play
 *
 * Starts playing an animation from the beginning.
 * Emits the "animation-started" signal.
 *
 * Returns: %TRUE if animation started, %FALSE if not found
 */
LRG_AVAILABLE_IN_ALL
gboolean     lrg_animator_component_play                (LrgAnimatorComponent *self,
                                                         const gchar          *name);

/**
 * lrg_animator_component_play_if_different:
 * @self: an #LrgAnimatorComponent
 * @name: Animation name to play
 *
 * Starts playing an animation only if it's not already playing.
 * This prevents restarting an animation when called repeatedly.
 *
 * Returns: %TRUE if animation started or already playing
 */
LRG_AVAILABLE_IN_ALL
gboolean     lrg_animator_component_play_if_different   (LrgAnimatorComponent *self,
                                                         const gchar          *name);

/**
 * lrg_animator_component_stop:
 * @self: an #LrgAnimatorComponent
 *
 * Stops the current animation and resets to the first frame.
 */
LRG_AVAILABLE_IN_ALL
void         lrg_animator_component_stop                (LrgAnimatorComponent *self);

/**
 * lrg_animator_component_pause:
 * @self: an #LrgAnimatorComponent
 *
 * Pauses the current animation.
 */
LRG_AVAILABLE_IN_ALL
void         lrg_animator_component_pause               (LrgAnimatorComponent *self);

/**
 * lrg_animator_component_resume:
 * @self: an #LrgAnimatorComponent
 *
 * Resumes a paused animation.
 */
LRG_AVAILABLE_IN_ALL
void         lrg_animator_component_resume              (LrgAnimatorComponent *self);

/*
 * State Queries
 */

/**
 * lrg_animator_component_get_current_animation:
 * @self: an #LrgAnimatorComponent
 *
 * Gets the name of the current animation.
 *
 * Returns: (nullable): Current animation name, or %NULL if none
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_animator_component_get_current_animation (LrgAnimatorComponent *self);

/**
 * lrg_animator_component_is_playing:
 * @self: an #LrgAnimatorComponent
 *
 * Checks if an animation is currently playing.
 *
 * Returns: %TRUE if playing
 */
LRG_AVAILABLE_IN_ALL
gboolean     lrg_animator_component_is_playing          (LrgAnimatorComponent *self);

/**
 * lrg_animator_component_is_finished:
 * @self: an #LrgAnimatorComponent
 *
 * Checks if a non-looping animation has finished.
 *
 * Returns: %TRUE if finished
 */
LRG_AVAILABLE_IN_ALL
gboolean     lrg_animator_component_is_finished         (LrgAnimatorComponent *self);

/**
 * lrg_animator_component_get_current_frame:
 * @self: an #LrgAnimatorComponent
 *
 * Gets the current frame index (absolute, not relative to animation).
 *
 * Returns: Current frame index
 */
LRG_AVAILABLE_IN_ALL
gint         lrg_animator_component_get_current_frame   (LrgAnimatorComponent *self);

/**
 * lrg_animator_component_get_current_frame_rect:
 * @self: an #LrgAnimatorComponent
 *
 * Gets the source rectangle for the current frame.
 *
 * Returns: (transfer full) (nullable): Current frame rectangle, or %NULL
 */
LRG_AVAILABLE_IN_ALL
GrlRectangle * lrg_animator_component_get_current_frame_rect (LrgAnimatorComponent *self);

/*
 * Speed Control
 */

/**
 * lrg_animator_component_get_speed:
 * @self: an #LrgAnimatorComponent
 *
 * Gets the playback speed multiplier.
 *
 * Returns: Speed multiplier (1.0 = normal)
 */
LRG_AVAILABLE_IN_ALL
gfloat       lrg_animator_component_get_speed           (LrgAnimatorComponent *self);

/**
 * lrg_animator_component_set_speed:
 * @self: an #LrgAnimatorComponent
 * @speed: Speed multiplier (1.0 = normal, 2.0 = double, 0.5 = half)
 *
 * Sets the playback speed multiplier.
 * Negative values play backwards.
 */
LRG_AVAILABLE_IN_ALL
void         lrg_animator_component_set_speed           (LrgAnimatorComponent *self,
                                                         gfloat                speed);

/*
 * Transition Helpers
 */

/**
 * lrg_animator_component_set_default_animation:
 * @self: an #LrgAnimatorComponent
 * @name: (nullable): Default animation name, or %NULL for none
 *
 * Sets the default animation to play when current animation finishes.
 * If set, finished non-looping animations will transition to this.
 */
LRG_AVAILABLE_IN_ALL
void         lrg_animator_component_set_default_animation (LrgAnimatorComponent *self,
                                                           const gchar          *name);

/**
 * lrg_animator_component_get_default_animation:
 * @self: an #LrgAnimatorComponent
 *
 * Gets the default animation name.
 *
 * Returns: (nullable): Default animation name, or %NULL
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_animator_component_get_default_animation (LrgAnimatorComponent *self);

G_END_DECLS
