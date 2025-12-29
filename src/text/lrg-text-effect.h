/* lrg-text-effect.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Animated text effects for rich text.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-enums.h"

G_BEGIN_DECLS

#define LRG_TYPE_TEXT_EFFECT (lrg_text_effect_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_DERIVABLE_TYPE (LrgTextEffect, lrg_text_effect, LRG, TEXT_EFFECT, GObject)

/**
 * LrgTextEffectClass:
 * @parent_class: Parent class
 * @update: Update effect state with delta time
 * @apply: Apply effect to character position/color
 * @reset: Reset effect state
 *
 * Class structure for #LrgTextEffect.
 */
struct _LrgTextEffectClass
{
    GObjectClass parent_class;

    /*< public >*/
    void     (*update)  (LrgTextEffect *effect,
                         gfloat         delta_time);
    void     (*apply)   (LrgTextEffect *effect,
                         guint          char_index,
                         gfloat        *offset_x,
                         gfloat        *offset_y,
                         guint8        *r,
                         guint8        *g,
                         guint8        *b,
                         guint8        *a);
    void     (*reset)   (LrgTextEffect *effect);

    /*< private >*/
    gpointer _reserved[8];
};

/**
 * lrg_text_effect_new:
 * @effect_type: The type of effect
 *
 * Creates a new text effect of the specified type.
 *
 * Returns: (transfer full): A new #LrgTextEffect
 */
LRG_AVAILABLE_IN_ALL
LrgTextEffect *     lrg_text_effect_new             (LrgTextEffectType effect_type);

/**
 * lrg_text_effect_get_effect_type:
 * @effect: A #LrgTextEffect
 *
 * Gets the effect type.
 *
 * Returns: The effect type
 */
LRG_AVAILABLE_IN_ALL
LrgTextEffectType   lrg_text_effect_get_effect_type (LrgTextEffect *effect);

/**
 * lrg_text_effect_get_speed:
 * @effect: A #LrgTextEffect
 *
 * Gets the animation speed.
 *
 * Returns: The speed multiplier
 */
LRG_AVAILABLE_IN_ALL
gfloat              lrg_text_effect_get_speed       (LrgTextEffect *effect);

/**
 * lrg_text_effect_set_speed:
 * @effect: A #LrgTextEffect
 * @speed: The speed multiplier
 *
 * Sets the animation speed.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_text_effect_set_speed       (LrgTextEffect *effect,
                                                     gfloat         speed);

/**
 * lrg_text_effect_get_intensity:
 * @effect: A #LrgTextEffect
 *
 * Gets the effect intensity.
 *
 * Returns: The intensity (0.0 - 1.0)
 */
LRG_AVAILABLE_IN_ALL
gfloat              lrg_text_effect_get_intensity   (LrgTextEffect *effect);

/**
 * lrg_text_effect_set_intensity:
 * @effect: A #LrgTextEffect
 * @intensity: The intensity (0.0 - 1.0)
 *
 * Sets the effect intensity.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_text_effect_set_intensity   (LrgTextEffect *effect,
                                                     gfloat         intensity);

/**
 * lrg_text_effect_update:
 * @effect: A #LrgTextEffect
 * @delta_time: Time elapsed since last update
 *
 * Updates the effect's internal state.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_text_effect_update          (LrgTextEffect *effect,
                                                     gfloat         delta_time);

/**
 * lrg_text_effect_apply:
 * @effect: A #LrgTextEffect
 * @char_index: Character index within the text
 * @offset_x: (inout): X offset to apply
 * @offset_y: (inout): Y offset to apply
 * @r: (inout): Red component
 * @g: (inout): Green component
 * @b: (inout): Blue component
 * @a: (inout): Alpha component
 *
 * Applies the effect to a character, modifying position and color.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_text_effect_apply           (LrgTextEffect *effect,
                                                     guint          char_index,
                                                     gfloat        *offset_x,
                                                     gfloat        *offset_y,
                                                     guint8        *r,
                                                     guint8        *g,
                                                     guint8        *b,
                                                     guint8        *a);

/**
 * lrg_text_effect_reset:
 * @effect: A #LrgTextEffect
 *
 * Resets the effect state.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_text_effect_reset           (LrgTextEffect *effect);

/**
 * lrg_text_effect_get_time:
 * @effect: A #LrgTextEffect
 *
 * Gets the current animation time.
 *
 * Returns: The elapsed time
 */
LRG_AVAILABLE_IN_ALL
gfloat              lrg_text_effect_get_time        (LrgTextEffect *effect);

/**
 * lrg_text_effect_is_complete:
 * @effect: A #LrgTextEffect
 *
 * Checks if a finite effect has completed (e.g., typewriter).
 *
 * Returns: %TRUE if the effect is complete
 */
LRG_AVAILABLE_IN_ALL
gboolean            lrg_text_effect_is_complete     (LrgTextEffect *effect);

/**
 * lrg_text_effect_set_char_count:
 * @effect: A #LrgTextEffect
 * @count: Total character count
 *
 * Sets the total character count for effects like typewriter.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_text_effect_set_char_count  (LrgTextEffect *effect,
                                                     guint          count);

G_END_DECLS
