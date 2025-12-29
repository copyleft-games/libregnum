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
 * @begin_capture: Virtual method to start scene capture
 * @end_capture: Virtual method to end scene capture
 * @render: Virtual method to render the post-processing chain
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
     * Renders the post-processing effect chain and outputs to screen.
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
 * Begins scene capture. Call before rendering the scene.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_post_processor_begin_capture    (LrgPostProcessor   *self);

/**
 * lrg_post_processor_end_capture:
 * @self: A #LrgPostProcessor
 *
 * Ends scene capture. Call after rendering the scene.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_post_processor_end_capture      (LrgPostProcessor   *self);

/**
 * lrg_post_processor_render:
 * @self: A #LrgPostProcessor
 * @delta_time: Time since last frame
 *
 * Applies the effect chain and renders to screen.
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
 * Resizes the render targets.
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

G_END_DECLS
