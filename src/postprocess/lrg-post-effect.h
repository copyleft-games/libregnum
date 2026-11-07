/* lrg-post-effect.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Base class for post-processing effects.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-enums.h"
#include <graylib.h>

G_BEGIN_DECLS

/**
 * LrgPostQuality:
 * @LRG_POST_QUALITY_LOW: Cheapest settings (fewest bloom levels, shortest searches)
 * @LRG_POST_QUALITY_MEDIUM: Balanced settings for integrated GPUs
 * @LRG_POST_QUALITY_HIGH: The default; full quality for discrete GPUs
 * @LRG_POST_QUALITY_ULTRA: Extra bloom levels and taps, for screenshots and fast GPUs
 *
 * A quality tier for a whole post stack. #LrgPostProcessor hands its tier
 * to every effect it owns (#LrgPostEffect:quality-tier); each effect decides
 * what the tier changes (see the effect pages in docs/modules/postprocess/).
 */
typedef enum
{
    LRG_POST_QUALITY_LOW,
    LRG_POST_QUALITY_MEDIUM,
    LRG_POST_QUALITY_HIGH,
    LRG_POST_QUALITY_ULTRA
} LrgPostQuality;

/**
 * lrg_post_quality_get_type:
 *
 * Gets the #GType of #LrgPostQuality.
 *
 * Returns: the enum #GType
 */
LRG_AVAILABLE_IN_ALL
GType lrg_post_quality_get_type (void) G_GNUC_CONST;

#define LRG_TYPE_POST_QUALITY (lrg_post_quality_get_type ())

#define LRG_TYPE_POST_EFFECT (lrg_post_effect_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_DERIVABLE_TYPE (LrgPostEffect, lrg_post_effect, LRG, POST_EFFECT, GObject)

/**
 * LrgPostEffectClass:
 * @parent_class: Parent class
 * @initialize: Virtual method to initialize GPU resources
 * @shutdown: Virtual method to release GPU resources
 * @apply: Virtual method to apply the effect to a texture
 * @resize: Virtual method called when render target size changes
 * @get_name: Virtual method to get the effect's display name
 *
 * The class structure for #LrgPostEffect.
 */
struct _LrgPostEffectClass
{
    GObjectClass parent_class;

    /* Virtual methods */

    /**
     * LrgPostEffectClass::initialize:
     * @self: A #LrgPostEffect
     * @width: Render target width
     * @height: Render target height
     * @error: Return location for error
     *
     * Initializes GPU resources (shaders, textures, etc.).
     * Called once before the effect is first used.
     *
     * Returns: %TRUE on success
     */
    gboolean    (*initialize)   (LrgPostEffect  *self,
                                 guint           width,
                                 guint           height,
                                 GError        **error);

    /**
     * LrgPostEffectClass::shutdown:
     * @self: A #LrgPostEffect
     *
     * Releases GPU resources.
     * Called when the effect is removed or the processor is destroyed.
     */
    void        (*shutdown)     (LrgPostEffect  *self);

    /**
     * LrgPostEffectClass::apply:
     * @self: A #LrgPostEffect
     * @source_texture_id: OpenGL texture ID of the input
     * @target_texture_id: OpenGL texture ID of the output
     * @width: Texture width
     * @height: Texture height
     * @delta_time: Time since last frame (for animated effects)
     *
     * Applies the effect, rendering from source to target. The caller
     * (normally #LrgPostProcessor) has already bound the framebuffer
     * whose colour attachment is @target_texture_id, sized @width x
     * @height, with colour blending disabled. Draw the whole target
     * (lrg_post_effect_draw_fullscreen()) and leave the same framebuffer
     * bound and blending disabled on return.
     */
    void        (*apply)        (LrgPostEffect  *self,
                                 guint           source_texture_id,
                                 guint           target_texture_id,
                                 guint           width,
                                 guint           height,
                                 gfloat          delta_time);

    /**
     * LrgPostEffectClass::resize:
     * @self: A #LrgPostEffect
     * @width: New width
     * @height: New height
     *
     * Called when the render target size changes.
     * Effects can recreate size-dependent resources here.
     */
    void        (*resize)       (LrgPostEffect  *self,
                                 guint           width,
                                 guint           height);

    /**
     * LrgPostEffectClass::get_name:
     * @self: A #LrgPostEffect
     *
     * Gets the display name of the effect.
     *
     * Returns: (transfer none): The effect name
     */
    const gchar * (*get_name)   (LrgPostEffect  *self);

    /*< private >*/
    gpointer _reserved[8];
};

/**
 * lrg_post_effect_initialize:
 * @self: A #LrgPostEffect
 * @width: Render target width
 * @height: Render target height
 * @error: (nullable): Return location for error
 *
 * Initializes the effect's GPU resources.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean        lrg_post_effect_initialize      (LrgPostEffect  *self,
                                                 guint           width,
                                                 guint           height,
                                                 GError        **error);

/**
 * lrg_post_effect_shutdown:
 * @self: A #LrgPostEffect
 *
 * Releases the effect's GPU resources.
 */
LRG_AVAILABLE_IN_ALL
void            lrg_post_effect_shutdown        (LrgPostEffect  *self);

/**
 * lrg_post_effect_apply:
 * @self: A #LrgPostEffect
 * @source_texture_id: OpenGL texture ID of the input
 * @target_texture_id: OpenGL texture ID of the output
 * @width: Texture width
 * @height: Texture height
 * @delta_time: Time since last frame
 *
 * Applies the effect.
 */
LRG_AVAILABLE_IN_ALL
void            lrg_post_effect_apply           (LrgPostEffect  *self,
                                                 guint           source_texture_id,
                                                 guint           target_texture_id,
                                                 guint           width,
                                                 guint           height,
                                                 gfloat          delta_time);

/**
 * lrg_post_effect_resize:
 * @self: A #LrgPostEffect
 * @width: New width
 * @height: New height
 *
 * Notifies the effect of a render target size change.
 */
LRG_AVAILABLE_IN_ALL
void            lrg_post_effect_resize          (LrgPostEffect  *self,
                                                 guint           width,
                                                 guint           height);

/**
 * lrg_post_effect_get_name:
 * @self: A #LrgPostEffect
 *
 * Gets the display name of the effect.
 *
 * Returns: (transfer none): The effect name
 */
LRG_AVAILABLE_IN_ALL
const gchar *   lrg_post_effect_get_name        (LrgPostEffect  *self);

/**
 * lrg_post_effect_is_enabled:
 * @self: A #LrgPostEffect
 *
 * Checks if the effect is enabled.
 *
 * Returns: %TRUE if enabled
 */
LRG_AVAILABLE_IN_ALL
gboolean        lrg_post_effect_is_enabled      (LrgPostEffect  *self);

/**
 * lrg_post_effect_set_enabled:
 * @self: A #LrgPostEffect
 * @enabled: Whether to enable the effect
 *
 * Enables or disables the effect.
 */
LRG_AVAILABLE_IN_ALL
void            lrg_post_effect_set_enabled     (LrgPostEffect  *self,
                                                 gboolean        enabled);

/**
 * lrg_post_effect_get_intensity:
 * @self: A #LrgPostEffect
 *
 * Gets the effect intensity (0.0 = off, 1.0 = full).
 *
 * Returns: The intensity value
 */
LRG_AVAILABLE_IN_ALL
gfloat          lrg_post_effect_get_intensity   (LrgPostEffect  *self);

/**
 * lrg_post_effect_set_intensity:
 * @self: A #LrgPostEffect
 * @intensity: Intensity value (0.0 - 1.0)
 *
 * Sets the effect intensity.
 */
LRG_AVAILABLE_IN_ALL
void            lrg_post_effect_set_intensity   (LrgPostEffect  *self,
                                                 gfloat          intensity);

/**
 * lrg_post_effect_is_initialized:
 * @self: A #LrgPostEffect
 *
 * Checks if the effect has been initialized.
 *
 * Returns: %TRUE if initialized
 */
LRG_AVAILABLE_IN_ALL
gboolean        lrg_post_effect_is_initialized  (LrgPostEffect  *self);

/**
 * lrg_post_effect_get_priority:
 * @self: A #LrgPostEffect
 *
 * Gets the effect's priority (higher = applied later in chain).
 *
 * Returns: The priority value
 */
LRG_AVAILABLE_IN_ALL
gint            lrg_post_effect_get_priority    (LrgPostEffect  *self);

/**
 * lrg_post_effect_set_priority:
 * @self: A #LrgPostEffect
 * @priority: Priority value
 *
 * Sets the effect's priority.
 */
LRG_AVAILABLE_IN_ALL
void            lrg_post_effect_set_priority    (LrgPostEffect  *self,
                                                 gint            priority);

/**
 * lrg_post_effect_get_quality_tier:
 * @self: A #LrgPostEffect
 *
 * Gets the quality tier the effect renders at.
 *
 * Returns: The quality tier
 */
LRG_AVAILABLE_IN_ALL
LrgPostQuality  lrg_post_effect_get_quality_tier     (LrgPostEffect  *self);

/**
 * lrg_post_effect_set_quality_tier:
 * @self: A #LrgPostEffect
 * @quality: The quality tier
 *
 * Sets the quality tier. #LrgPostProcessor sets this on every effect it
 * owns when its own tier changes (lrg_post_processor_set_quality()).
 */
LRG_AVAILABLE_IN_ALL
void            lrg_post_effect_set_quality_tier     (LrgPostEffect  *self,
                                                 LrgPostQuality  quality);

/**
 * lrg_post_effect_compile_shader:
 * @fragment_source: GLSL 330 fragment shader source; raylib's default
 *   vertex shader feeds it `fragTexCoord` and `fragColor`, the source
 *   image is `texture0`
 * @error: (nullable): Return location for an error
 *
 * Compiles a full-screen pass shader for an effect. Needs a window (a GL
 * context): without one it returns %NULL and sets
 * %G_IO_ERROR_NOT_INITIALIZED. A compile failure (raylib falls back to its
 * default shader) sets %G_IO_ERROR_FAILED.
 *
 * Returns: (transfer full) (nullable): The shader, or %NULL
 */
LRG_AVAILABLE_IN_ALL
GrlShader *     lrg_post_effect_compile_shader  (const gchar    *fragment_source,
                                                 GError        **error);

/**
 * lrg_post_effect_draw_fullscreen:
 * @texture_id: OpenGL texture ID of a render-texture image
 * @width: Width of the bound target
 * @height: Height of the bound target
 *
 * Draws @texture_id over the whole bound target (0, 0, @width, @height)
 * with the active shader (or raylib's default one), flipped vertically
 * because render textures are stored bottom up. This is the one draw a
 * single-pass effect makes in its #LrgPostEffectClass.apply.
 */
LRG_AVAILABLE_IN_ALL
void            lrg_post_effect_draw_fullscreen (guint           texture_id,
                                                 guint           width,
                                                 guint           height);

/**
 * lrg_post_effect_bind_framebuffer:
 * @framebuffer_id: OpenGL framebuffer ID, 0 for the window
 * @width: Width of that framebuffer
 * @height: Height of that framebuffer
 *
 * Flushes the pending batch and makes @framebuffer_id the draw target with
 * a top-left origin orthographic projection of @width x @height (what
 * BeginTextureMode() does; 0 is EndTextureMode()). Multi-pass effects use
 * it to draw into their own targets and to return to the target the
 * processor bound (read it with rlGetActiveFramebuffer() first).
 */
LRG_AVAILABLE_IN_ALL
void            lrg_post_effect_bind_framebuffer (guint          framebuffer_id,
                                                  guint          width,
                                                  guint          height);

G_END_DECLS
