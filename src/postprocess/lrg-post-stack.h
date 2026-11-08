/* lrg-post-stack.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgPostStack: a post-processing look as data (the `.rpost` format):
 * an ordered list of effects by type name with their parameters, enabled
 * flags and weights, plus a quality tier. Builds into an LrgPostProcessor
 * (reusing the processor's effects when the chain matches) and blends with
 * another stack by weight (post volumes).
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "lrg-post-effect.h"
#include "lrg-post-processor.h"

G_BEGIN_DECLS

/**
 * LRG_POST_STACK_ERROR:
 *
 * Error domain for #LrgPostStack. Errors use #LrgPostStackError codes.
 */
#define LRG_POST_STACK_ERROR (lrg_post_stack_error_quark ())

/**
 * lrg_post_stack_error_quark:
 *
 * Gets the error quark for #LrgPostStack.
 *
 * Returns: the quark
 */
LRG_AVAILABLE_IN_ALL
GQuark lrg_post_stack_error_quark (void);

/**
 * LrgPostStackError:
 * @LRG_POST_STACK_ERROR_PARSE: The YAML is malformed or not a post stack
 *   (missing `effects:`, an effect without `type:`, a newer `version:`)
 * @LRG_POST_STACK_ERROR_UNKNOWN_EFFECT: An effect type name is not
 *   registered (lrg_post_stack_register_effect_type())
 * @LRG_POST_STACK_ERROR_UNKNOWN_PARAM: A parameter is not a property of
 *   the effect type (nor a special key such as `palette` or `lut`)
 * @LRG_POST_STACK_ERROR_INVALID_VALUE: A parameter value does not convert
 *   to the property's type
 * @LRG_POST_STACK_ERROR_IO: Reading or writing the file failed, or a LUT
 *   or palette named by the stack did not load
 *
 * Error codes in the %LRG_POST_STACK_ERROR domain.
 */
typedef enum
{
	LRG_POST_STACK_ERROR_PARSE,
	LRG_POST_STACK_ERROR_UNKNOWN_EFFECT,
	LRG_POST_STACK_ERROR_UNKNOWN_PARAM,
	LRG_POST_STACK_ERROR_INVALID_VALUE,
	LRG_POST_STACK_ERROR_IO
} LrgPostStackError;

/**
 * lrg_post_stack_error_get_type:
 *
 * Gets the #GType of #LrgPostStackError.
 *
 * Returns: the enum #GType
 */
LRG_AVAILABLE_IN_ALL
GType lrg_post_stack_error_get_type (void) G_GNUC_CONST;

#define LRG_TYPE_POST_STACK_ERROR (lrg_post_stack_error_get_type ())

/**
 * LRG_POST_STACK_FORMAT_VERSION:
 *
 * The `.rpost` `version:` this build writes and the newest it reads.
 */
#define LRG_POST_STACK_FORMAT_VERSION (1)

#define LRG_TYPE_POST_STACK (lrg_post_stack_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgPostStack, lrg_post_stack, LRG, POST_STACK, GObject)

/* ==========================================================================
 * Effect type registry
 * ========================================================================== */

/**
 * lrg_post_stack_register_effect_type:
 * @name: The type name used in `.rpost` files (kebab-case)
 * @type: A #LrgPostEffect subtype, constructible with g_object_new() and
 *   no properties
 *
 * Registers an effect type under @name. The built-in effects are
 * pre-registered under their effect names (bloom, color-grade,
 * colorblind-filter, film-grain, fxaa, heat-haze, palettize,
 * screen-shake, sharpen, tonemap, vignette); registering one of those
 * names again replaces it. Thread-safe.
 */
LRG_AVAILABLE_IN_ALL
void            lrg_post_stack_register_effect_type (const gchar *name,
                                                     GType        type);

/**
 * lrg_post_stack_lookup_effect_type:
 * @name: A type name
 *
 * Resolves an `.rpost` type name: a registered name first, then a GType
 * name (e.g. `MyGameSepia`) when that type is a #LrgPostEffect.
 *
 * Returns: The #GType, or %G_TYPE_INVALID
 */
LRG_AVAILABLE_IN_ALL
GType           lrg_post_stack_lookup_effect_type   (const gchar *name);

/**
 * lrg_post_stack_list_effect_types:
 *
 * Lists the registered type names, sorted (for editor menus).
 *
 * Returns: (transfer full): A %NULL-terminated array; free with
 *   g_strfreev()
 */
LRG_AVAILABLE_IN_ALL
gchar **        lrg_post_stack_list_effect_types    (void);

/* ==========================================================================
 * Construction, load and save
 * ========================================================================== */

/**
 * lrg_post_stack_new:
 *
 * Creates an empty stack (no effects, quality high, HDR on).
 *
 * Returns: (transfer full): a new #LrgPostStack
 */
LRG_AVAILABLE_IN_ALL
LrgPostStack *  lrg_post_stack_new                  (void);

/**
 * lrg_post_stack_new_from_file:
 * @path: Path of a `.rpost` file
 * @error: (nullable): Return location for an error
 *
 * Loads a stack. Relative paths inside it (a colour grade `lut:`) resolve
 * against the file's directory.
 *
 * Returns: (transfer full) (nullable): The stack, or %NULL with @error set
 */
LRG_AVAILABLE_IN_ALL
LrgPostStack *  lrg_post_stack_new_from_file        (const gchar   *path,
                                                     GError       **error);

/**
 * lrg_post_stack_new_from_data:
 * @data: `.rpost` YAML text
 * @length: Length of @data, or -1 when NUL-terminated
 * @base_dir: (nullable): Directory relative paths resolve against
 * @error: (nullable): Return location for an error
 *
 * Parses a stack from memory.
 *
 * Returns: (transfer full) (nullable): The stack, or %NULL with @error set
 */
LRG_AVAILABLE_IN_ALL
LrgPostStack *  lrg_post_stack_new_from_data        (const gchar   *data,
                                                     gssize         length,
                                                     const gchar   *base_dir,
                                                     GError       **error);

/**
 * lrg_post_stack_new_from_processor:
 * @processor: A #LrgPostProcessor
 *
 * Captures a processor's chain as a stack: every effect whose type is
 * registered (others are skipped), in chain order, with its enabled flag,
 * base intensity as `weight`, and every effect property that differs from
 * its default. The editor's "save look" uses this.
 *
 * Returns: (transfer full): a new #LrgPostStack
 */
LRG_AVAILABLE_IN_ALL
LrgPostStack *  lrg_post_stack_new_from_processor   (LrgPostProcessor *processor);

/**
 * lrg_post_stack_copy:
 * @self: A #LrgPostStack
 *
 * Deep-copies a stack.
 *
 * Returns: (transfer full): a new #LrgPostStack
 */
LRG_AVAILABLE_IN_ALL
LrgPostStack *  lrg_post_stack_copy                 (LrgPostStack  *self);

/**
 * lrg_post_stack_to_data:
 * @self: A #LrgPostStack
 * @error: (nullable): Return location for an error
 *
 * Serialises the stack as `.rpost` YAML. Unknown keys read from a file
 * (top level and per effect) are written back unchanged.
 *
 * Returns: (transfer full) (nullable): The YAML text, or %NULL
 */
LRG_AVAILABLE_IN_ALL
gchar *         lrg_post_stack_to_data              (LrgPostStack  *self,
                                                     GError       **error);

/**
 * lrg_post_stack_save_to_file:
 * @self: A #LrgPostStack
 * @path: Destination path
 * @error: (nullable): Return location for an error
 *
 * Writes the stack as `.rpost` YAML (atomically).
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean        lrg_post_stack_save_to_file         (LrgPostStack  *self,
                                                     const gchar   *path,
                                                     GError       **error);

/* ==========================================================================
 * Stack settings
 * ========================================================================== */

/**
 * lrg_post_stack_get_name:
 * @self: A #LrgPostStack
 *
 * Gets the display name.
 *
 * Returns: (transfer none) (nullable): The name
 */
LRG_AVAILABLE_IN_ALL
const gchar *   lrg_post_stack_get_name             (LrgPostStack  *self);

/**
 * lrg_post_stack_set_name:
 * @self: A #LrgPostStack
 * @name: (nullable): The display name
 *
 * Sets the display name.
 */
LRG_AVAILABLE_IN_ALL
void            lrg_post_stack_set_name             (LrgPostStack  *self,
                                                     const gchar   *name);

/**
 * lrg_post_stack_get_quality:
 * @self: A #LrgPostStack
 *
 * Gets the quality tier the stack builds with.
 *
 * Returns: The quality tier
 */
LRG_AVAILABLE_IN_ALL
LrgPostQuality  lrg_post_stack_get_quality          (LrgPostStack  *self);

/**
 * lrg_post_stack_set_quality:
 * @self: A #LrgPostStack
 * @quality: The quality tier
 *
 * Sets the quality tier (lrg_post_processor_set_quality() at build).
 */
LRG_AVAILABLE_IN_ALL
void            lrg_post_stack_set_quality          (LrgPostStack  *self,
                                                     LrgPostQuality quality);

/**
 * lrg_post_stack_get_hdr:
 * @self: A #LrgPostStack
 *
 * Gets whether the stack asks for HDR targets.
 *
 * Returns: %TRUE if HDR is requested
 */
LRG_AVAILABLE_IN_ALL
gboolean        lrg_post_stack_get_hdr              (LrgPostStack  *self);

/**
 * lrg_post_stack_set_hdr:
 * @self: A #LrgPostStack
 * @hdr: Whether to ask for HDR targets
 *
 * Sets whether the stack asks for HDR targets (lrg_post_processor_set_hdr()
 * at build).
 */
LRG_AVAILABLE_IN_ALL
void            lrg_post_stack_set_hdr              (LrgPostStack  *self,
                                                     gboolean       hdr);

/* ==========================================================================
 * Effects
 * ========================================================================== */

/**
 * lrg_post_stack_get_effect_count:
 * @self: A #LrgPostStack
 *
 * Gets the number of effects.
 *
 * Returns: The effect count
 */
LRG_AVAILABLE_IN_ALL
guint           lrg_post_stack_get_effect_count     (LrgPostStack  *self);

/**
 * lrg_post_stack_add_effect:
 * @self: A #LrgPostStack
 * @type_name: A registered effect type name
 * @error: (nullable): Return location for an error
 *
 * Appends an effect (enabled, weight 1, no parameters: property defaults).
 *
 * Returns: The new effect's index, or -1 with
 *   %LRG_POST_STACK_ERROR_UNKNOWN_EFFECT
 */
LRG_AVAILABLE_IN_ALL
gint            lrg_post_stack_add_effect           (LrgPostStack  *self,
                                                     const gchar   *type_name,
                                                     GError       **error);

/**
 * lrg_post_stack_remove_effect:
 * @self: A #LrgPostStack
 * @index: Effect index
 *
 * Removes an effect.
 */
LRG_AVAILABLE_IN_ALL
void            lrg_post_stack_remove_effect        (LrgPostStack  *self,
                                                     guint          index);

/**
 * lrg_post_stack_move_effect:
 * @self: A #LrgPostStack
 * @from: Current index
 * @to: New index
 *
 * Moves an effect within the chain (the order effects run in).
 */
LRG_AVAILABLE_IN_ALL
void            lrg_post_stack_move_effect          (LrgPostStack  *self,
                                                     guint          from,
                                                     guint          to);

/**
 * lrg_post_stack_find_effect:
 * @self: A #LrgPostStack
 * @type_name: An effect type name
 *
 * Finds the first effect of a type.
 *
 * Returns: Its index, or -1
 */
LRG_AVAILABLE_IN_ALL
gint            lrg_post_stack_find_effect          (LrgPostStack  *self,
                                                     const gchar   *type_name);

/**
 * lrg_post_stack_get_effect_type:
 * @self: A #LrgPostStack
 * @index: Effect index
 *
 * Gets an effect's type name as written in the file.
 *
 * Returns: (transfer none): The type name
 */
LRG_AVAILABLE_IN_ALL
const gchar *   lrg_post_stack_get_effect_type      (LrgPostStack  *self,
                                                     guint          index);

/**
 * lrg_post_stack_get_effect_enabled:
 * @self: A #LrgPostStack
 * @index: Effect index
 *
 * Gets an effect's enabled flag.
 *
 * Returns: %TRUE if enabled
 */
LRG_AVAILABLE_IN_ALL
gboolean        lrg_post_stack_get_effect_enabled   (LrgPostStack  *self,
                                                     guint          index);

/**
 * lrg_post_stack_set_effect_enabled:
 * @self: A #LrgPostStack
 * @index: Effect index
 * @enabled: Whether the effect runs
 *
 * Sets an effect's enabled flag (#LrgPostEffect:enabled at build).
 */
LRG_AVAILABLE_IN_ALL
void            lrg_post_stack_set_effect_enabled   (LrgPostStack  *self,
                                                     guint          index,
                                                     gboolean       enabled);

/**
 * lrg_post_stack_get_effect_weight:
 * @self: A #LrgPostStack
 * @index: Effect index
 *
 * Gets an effect's weight.
 *
 * Returns: The weight, 0..1
 */
LRG_AVAILABLE_IN_ALL
gfloat          lrg_post_stack_get_effect_weight    (LrgPostStack  *self,
                                                     guint          index);

/**
 * lrg_post_stack_set_effect_weight:
 * @self: A #LrgPostStack
 * @index: Effect index
 * @weight: 0..1, how much of the effect shows
 *
 * Sets an effect's weight: the base intensity
 * (lrg_post_effect_set_intensity()) at build, which every built-in effect
 * scales its result by. Blending fades effects through it.
 */
LRG_AVAILABLE_IN_ALL
void            lrg_post_stack_set_effect_weight    (LrgPostStack  *self,
                                                     guint          index,
                                                     gfloat         weight);

/**
 * lrg_post_stack_set_param:
 * @self: A #LrgPostStack
 * @index: Effect index
 * @name: A property name of the effect type (`_` is read as `-`), or a
 *   special key (`palette` for palettize, `lut`/`lut-strength` for
 *   color-grade)
 * @value: The value; converted to the property type
 * @error: (nullable): Return location for an error
 *
 * Sets one parameter.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean        lrg_post_stack_set_param            (LrgPostStack  *self,
                                                     guint          index,
                                                     const gchar   *name,
                                                     const GValue  *value,
                                                     GError       **error);

/**
 * lrg_post_stack_get_param:
 * @self: A #LrgPostStack
 * @index: Effect index
 * @name: A parameter name
 * @value: (out caller-allocates): An uninitialised #GValue to receive it
 *
 * Gets a parameter set in the stack (not a property default).
 *
 * Returns: %TRUE if the stack sets @name
 */
LRG_AVAILABLE_IN_ALL
gboolean        lrg_post_stack_get_param            (LrgPostStack  *self,
                                                     guint          index,
                                                     const gchar   *name,
                                                     GValue        *value);

/**
 * lrg_post_stack_unset_param:
 * @self: A #LrgPostStack
 * @index: Effect index
 * @name: A parameter name
 *
 * Removes a parameter (the property default applies again).
 */
LRG_AVAILABLE_IN_ALL
void            lrg_post_stack_unset_param          (LrgPostStack  *self,
                                                     guint          index,
                                                     const gchar   *name);

/**
 * lrg_post_stack_get_param_names:
 * @self: A #LrgPostStack
 * @index: Effect index
 *
 * Lists the parameters the stack sets for an effect, in order.
 *
 * Returns: (transfer full): A %NULL-terminated array; free with
 *   g_strfreev()
 */
LRG_AVAILABLE_IN_ALL
gchar **        lrg_post_stack_get_param_names      (LrgPostStack  *self,
                                                     guint          index);

/* ==========================================================================
 * Build and blend
 * ========================================================================== */

/**
 * lrg_post_stack_build:
 * @self: A #LrgPostStack
 * @processor: The processor to configure
 * @error: (nullable): Return location for an error
 *
 * Makes @processor's chain match the stack: effects run in stack order
 * (priority = index * 10), each with its enabled flag, weight (base
 * intensity) and parameters; properties the stack does not set go back
 * to their defaults. Effects already in the processor are reused when
 * their type matches in order, so calling this every frame (a blended
 * volume) costs only property sets; mismatched ones are removed and new
 * ones added. Also sets the processor's quality and HDR request.
 *
 * Returns: %TRUE on success; on failure the processor is unchanged
 */
LRG_AVAILABLE_IN_ALL
gboolean        lrg_post_stack_build                (LrgPostStack     *self,
                                                     LrgPostProcessor *processor,
                                                     GError          **error);

/**
 * lrg_post_stack_blend:
 * @a: The stack at weight 0
 * @b: The stack at weight 1
 * @weight: 0..1
 *
 * Blends two stacks for post volumes. Effects match by type in order
 * (the n-th bloom of @a with the n-th bloom of @b). Matched effects lerp
 * every numeric parameter (a parameter only one side sets uses the
 * property default on the other) and their weights; booleans, enums and
 * strings switch at 0.5. Effects only in @a fade out (weight * (1 -
 * @weight)), effects only in @b fade in (weight * @weight) and are
 * appended after @a's. Disabled effects count as weight 0; an effect
 * ends enabled when its blended weight is above 0. Quality switches at
 * 0.5; HDR is on if either asks for it.
 *
 * Returns: (transfer full): a new #LrgPostStack
 */
LRG_AVAILABLE_IN_ALL
LrgPostStack *  lrg_post_stack_blend                (LrgPostStack  *a,
                                                     LrgPostStack  *b,
                                                     gfloat         weight);

G_END_DECLS
