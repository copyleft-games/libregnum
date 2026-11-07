/* lrg-post-processor.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Post-processing pipeline manager.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-enums.h"
#include "lrg-post-effect.h"

G_BEGIN_DECLS

#define LRG_TYPE_POST_PROCESSOR (lrg_post_processor_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_DERIVABLE_TYPE (LrgPostProcessor, lrg_post_processor, LRG, POST_PROCESSOR, GObject)

/**
 * LrgPostProcessorClass:
 * @parent_class: Parent class
 * @begin_capture: Virtual method to start scene capture (binds the
 *   capture target)
 * @end_capture: Virtual method to end scene capture (rebinds the target
 *   that was bound before the capture)
 * @render: Virtual method to run the effect chain and composite the result
 *   into the currently bound target
 *
 * The class structure for #LrgPostProcessor.
 */
struct _LrgPostProcessorClass
{
    GObjectClass parent_class;

    /* Virtual methods */

    /**
     * LrgPostProcessorClass::begin_capture:
     * @self: A #LrgPostProcessor
     *
     * Begins capturing the scene to an offscreen render target.
     * Called before scene rendering.
     */
    void    (*begin_capture)    (LrgPostProcessor   *self);

    /**
     * LrgPostProcessorClass::end_capture:
     * @self: A #LrgPostProcessor
     *
     * Ends scene capture.
     * Called after scene rendering, before effects are applied.
     */
    void    (*end_capture)      (LrgPostProcessor   *self);

    /**
     * LrgPostProcessorClass::render:
     * @self: A #LrgPostProcessor
     * @delta_time: Time since last frame
     *
     * Runs the effect chain over the captured image and composites the
     * result into the currently bound target (the window or a render
     * texture).
     */
    void    (*render)           (LrgPostProcessor   *self,
                                 gfloat              delta_time);

    /*< private >*/
    gpointer _reserved[8];
};

/**
 * lrg_post_processor_new:
 * @width: Initial render target width
 * @height: Initial render target height
 *
 * Creates a new post-processor.
 *
 * Returns: (transfer full): A new #LrgPostProcessor
 */
LRG_AVAILABLE_IN_ALL
LrgPostProcessor *  lrg_post_processor_new              (guint               width,
                                                         guint               height);

/**
 * lrg_post_processor_add_effect:
 * @self: A #LrgPostProcessor
 * @effect: (transfer none): Effect to add
 *
 * Adds an effect to the processing chain.
 * Effects are applied in priority order (lower priority first).
 */
LRG_AVAILABLE_IN_ALL
void                lrg_post_processor_add_effect       (LrgPostProcessor   *self,
                                                         LrgPostEffect      *effect);

/**
 * lrg_post_processor_remove_effect:
 * @self: A #LrgPostProcessor
 * @effect: Effect to remove
 *
 * Removes an effect from the processing chain.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_post_processor_remove_effect    (LrgPostProcessor   *self,
                                                         LrgPostEffect      *effect);

/**
 * lrg_post_processor_get_effect:
 * @self: A #LrgPostProcessor
 * @name: Effect name to find
 *
 * Finds an effect by name.
 *
 * Returns: (transfer none) (nullable): The effect, or %NULL if not found
 */
LRG_AVAILABLE_IN_ALL
LrgPostEffect *     lrg_post_processor_get_effect       (LrgPostProcessor   *self,
                                                         const gchar        *name);

/**
 * lrg_post_processor_get_effects:
 * @self: A #LrgPostProcessor
 *
 * Gets all effects in the chain.
 *
 * Returns: (transfer none) (element-type LrgPostEffect): The effect list
 */
LRG_AVAILABLE_IN_ALL
GList *             lrg_post_processor_get_effects      (LrgPostProcessor   *self);

/**
 * lrg_post_processor_get_effect_count:
 * @self: A #LrgPostProcessor
 *
 * Gets the number of effects in the chain.
 *
 * Returns: The effect count
 */
LRG_AVAILABLE_IN_ALL
guint               lrg_post_processor_get_effect_count (LrgPostProcessor   *self);

/**
 * lrg_post_processor_clear_effects:
 * @self: A #LrgPostProcessor
 *
 * Removes all effects from the chain.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_post_processor_clear_effects    (LrgPostProcessor   *self);

/**
 * lrg_post_processor_begin_capture:
 * @self: A #LrgPostProcessor
 *
 * Begins scene capture. Call before rendering the scene. With a GL context
 * this creates (or resizes) the capture target, remembers the bound target
 * and binds the capture target cleared to transparent black; everything
 * drawn until lrg_post_processor_end_capture() lands in it. Without a
 * window only the capturing state changes. Does nothing while the
 * processor is disabled (the scene then draws straight to the target).
 */
LRG_AVAILABLE_IN_ALL
void                lrg_post_processor_begin_capture    (LrgPostProcessor   *self);

/**
 * lrg_post_processor_end_capture:
 * @self: A #LrgPostProcessor
 *
 * Ends scene capture. Call after rendering the scene. Rebinds the target
 * that was bound at lrg_post_processor_begin_capture().
 */
LRG_AVAILABLE_IN_ALL
void                lrg_post_processor_end_capture      (LrgPostProcessor   *self);

/**
 * lrg_post_processor_render:
 * @self: A #LrgPostProcessor
 * @delta_time: Time since last frame
 *
 * Runs every enabled effect in priority order over the captured image
 * (lrg_post_processor_process()) and draws the result over the whole
 * currently bound target with alpha blending, scaled when the sizes
 * differ. Ends a capture still in progress. Does nothing when nothing
 * was captured this frame.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_post_processor_render           (LrgPostProcessor   *self,
                                                         gfloat              delta_time);

/**
 * lrg_post_processor_resize:
 * @self: A #LrgPostProcessor
 * @width: New width
 * @height: New height
 *
 * Resizes the render targets (recreated on the next capture) and tells
 * every effect (lrg_post_effect_resize()).
 */
LRG_AVAILABLE_IN_ALL
void                lrg_post_processor_resize           (LrgPostProcessor   *self,
                                                         guint               width,
                                                         guint               height);

/**
 * lrg_post_processor_get_width:
 * @self: A #LrgPostProcessor
 *
 * Gets the current render target width.
 *
 * Returns: The width
 */
LRG_AVAILABLE_IN_ALL
guint               lrg_post_processor_get_width        (LrgPostProcessor   *self);

/**
 * lrg_post_processor_get_height:
 * @self: A #LrgPostProcessor
 *
 * Gets the current render target height.
 *
 * Returns: The height
 */
LRG_AVAILABLE_IN_ALL
guint               lrg_post_processor_get_height       (LrgPostProcessor   *self);

/**
 * lrg_post_processor_is_enabled:
 * @self: A #LrgPostProcessor
 *
 * Checks if post-processing is enabled.
 *
 * Returns: %TRUE if enabled
 */
LRG_AVAILABLE_IN_ALL
gboolean            lrg_post_processor_is_enabled       (LrgPostProcessor   *self);

/**
 * lrg_post_processor_set_enabled:
 * @self: A #LrgPostProcessor
 * @enabled: Whether to enable post-processing
 *
 * Enables or disables the entire post-processing chain.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_post_processor_set_enabled      (LrgPostProcessor   *self,
                                                         gboolean            enabled);

/**
 * lrg_post_processor_is_capturing:
 * @self: A #LrgPostProcessor
 *
 * Checks if the processor is currently capturing.
 *
 * Returns: %TRUE if capturing
 */
LRG_AVAILABLE_IN_ALL
gboolean            lrg_post_processor_is_capturing     (LrgPostProcessor   *self);

/**
 * lrg_post_processor_sort_effects:
 * @self: A #LrgPostProcessor
 *
 * Re-sorts effects by priority. Call after changing effect priorities.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_post_processor_sort_effects     (LrgPostProcessor   *self);

/**
 * lrg_post_processor_process:
 * @self: A #LrgPostProcessor
 * @source_texture_id: OpenGL texture ID of the input image (a render
 *   texture of the processor's size, stored bottom up)
 * @delta_time: Time since last frame
 *
 * Runs the enabled effects over @source_texture_id through the processor's
 * ping-pong targets, without drawing anything into the bound target (the
 * binding is restored on return). This is the entry point for renderers
 * that own their scene target (#LrgWorld3DRenderer): draw the returned
 * texture yourself. Without a GL context, or with no enabled effect, it
 * returns @source_texture_id.
 *
 * Returns: The texture ID holding the result; valid until the next
 *   process, capture or resize
 */
LRG_AVAILABLE_IN_ALL
guint               lrg_post_processor_process          (LrgPostProcessor   *self,
                                                         guint               source_texture_id,
                                                         gfloat              delta_time);

/**
 * lrg_post_processor_get_capture_texture:
 * @self: A #LrgPostProcessor
 *
 * Gets the capture target's colour texture.
 *
 * Returns: The OpenGL texture ID, or 0 before the first GPU capture
 */
LRG_AVAILABLE_IN_ALL
guint               lrg_post_processor_get_capture_texture (LrgPostProcessor *self);

/**
 * lrg_post_processor_get_hdr:
 * @self: A #LrgPostProcessor
 *
 * Gets whether a floating-point (RGBA16F) capture target is requested.
 *
 * Returns: %TRUE if HDR targets are requested
 */
LRG_AVAILABLE_IN_ALL
gboolean            lrg_post_processor_get_hdr          (LrgPostProcessor   *self);

/**
 * lrg_post_processor_set_hdr:
 * @self: A #LrgPostProcessor
 * @hdr: Whether to request floating-point targets
 *
 * Requests floating-point (RGBA16F) targets so values above 1.0 survive
 * until a tonemap (#LrgTonemap). When the driver cannot render to them the
 * processor falls back to RGBA8 (lrg_post_processor_is_hdr_active()).
 * Takes effect on the next capture.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_post_processor_set_hdr          (LrgPostProcessor   *self,
                                                         gboolean            hdr);

/**
 * lrg_post_processor_is_hdr_active:
 * @self: A #LrgPostProcessor
 *
 * Checks whether the current targets are floating point.
 *
 * Returns: %TRUE if the live targets are RGBA16F
 */
LRG_AVAILABLE_IN_ALL
gboolean            lrg_post_processor_is_hdr_active    (LrgPostProcessor   *self);

/**
 * lrg_post_processor_get_quality:
 * @self: A #LrgPostProcessor
 *
 * Gets the quality tier.
 *
 * Returns: The quality tier
 */
LRG_AVAILABLE_IN_ALL
LrgPostQuality      lrg_post_processor_get_quality      (LrgPostProcessor   *self);

/**
 * lrg_post_processor_set_quality:
 * @self: A #LrgPostProcessor
 * @quality: The quality tier
 *
 * Sets the quality tier and hands it to every effect (now and when added).
 */
LRG_AVAILABLE_IN_ALL
void                lrg_post_processor_set_quality      (LrgPostProcessor   *self,
                                                         LrgPostQuality      quality);

/**
 * lrg_post_processor_get_auto_resize:
 * @self: A #LrgPostProcessor
 *
 * Gets whether captures follow the bound target's size.
 *
 * Returns: %TRUE if auto-resize is on
 */
LRG_AVAILABLE_IN_ALL
gboolean            lrg_post_processor_get_auto_resize  (LrgPostProcessor   *self);

/**
 * lrg_post_processor_set_auto_resize:
 * @self: A #LrgPostProcessor
 * @auto_resize: Whether to follow the bound target's size
 *
 * When on, lrg_post_processor_begin_capture() resizes the processor to the
 * target bound at that moment (the window's render size, or the render
 * texture's), so a resizable window needs no resize calls.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_post_processor_set_auto_resize  (LrgPostProcessor   *self,
                                                         gboolean            auto_resize);

/**
 * lrg_post_processor_get_pass_count:
 * @self: A #LrgPostProcessor
 *
 * Gets how many effect passes the last process or render ran (one per
 * enabled effect; multi-pass effects count once).
 *
 * Returns: The pass count
 */
LRG_AVAILABLE_IN_ALL
guint               lrg_post_processor_get_pass_count   (LrgPostProcessor   *self);

/**
 * lrg_post_processor_release:
 * @self: A #LrgPostProcessor
 *
 * Releases every GPU resource (targets, effect shaders and targets) while
 * keeping the effect chain; they are recreated on the next capture. Call
 * before CloseWindow() when the processor outlives the window.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_post_processor_release          (LrgPostProcessor   *self);

G_END_DECLS
