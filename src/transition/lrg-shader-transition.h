/* lrg-shader-transition.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Custom shader-based transition.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "lrg-transition.h"

G_BEGIN_DECLS

#define LRG_TYPE_SHADER_TRANSITION (lrg_shader_transition_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgShaderTransition, lrg_shader_transition, LRG, SHADER_TRANSITION, LrgTransition)

/**
 * lrg_shader_transition_new:
 *
 * Creates a new shader transition (requires shader to be set).
 *
 * Returns: (transfer full): A new #LrgShaderTransition
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgShaderTransition *   lrg_shader_transition_new                   (void);

/**
 * lrg_shader_transition_new_from_file:
 * @fragment_path: Path to fragment shader file
 * @error: (nullable): Return location for error
 *
 * Creates a new shader transition from a shader file.
 *
 * Returns: (transfer full) (nullable): A new #LrgShaderTransition, or %NULL on error
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgShaderTransition *   lrg_shader_transition_new_from_file         (const gchar  *fragment_path,
                                                                     GError      **error);

/**
 * lrg_shader_transition_new_from_source:
 * @fragment_source: Fragment shader source code
 * @error: (nullable): Return location for error
 *
 * Creates a new shader transition from source code.
 *
 * Returns: (transfer full) (nullable): A new #LrgShaderTransition, or %NULL on error
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgShaderTransition *   lrg_shader_transition_new_from_source       (const gchar  *fragment_source,
                                                                     GError      **error);

/**
 * lrg_shader_transition_load_from_file:
 * @self: A #LrgShaderTransition
 * @fragment_path: Path to fragment shader file
 * @error: (nullable): Return location for error
 *
 * Loads a shader from a file.
 *
 * Returns: %TRUE on success
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean                lrg_shader_transition_load_from_file        (LrgShaderTransition  *self,
                                                                     const gchar          *fragment_path,
                                                                     GError              **error);

/**
 * lrg_shader_transition_load_from_source:
 * @self: A #LrgShaderTransition
 * @fragment_source: Fragment shader source code
 * @error: (nullable): Return location for error
 *
 * Loads a shader from source code.
 *
 * Returns: %TRUE on success
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean                lrg_shader_transition_load_from_source      (LrgShaderTransition  *self,
                                                                     const gchar          *fragment_source,
                                                                     GError              **error);

/**
 * lrg_shader_transition_set_uniform_float:
 * @self: A #LrgShaderTransition
 * @name: Uniform variable name
 * @value: Float value
 *
 * Sets a float uniform in the shader.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void                    lrg_shader_transition_set_uniform_float     (LrgShaderTransition *self,
                                                                     const gchar         *name,
                                                                     gfloat               value);

/**
 * lrg_shader_transition_set_uniform_vec2:
 * @self: A #LrgShaderTransition
 * @name: Uniform variable name
 * @x: X component
 * @y: Y component
 *
 * Sets a vec2 uniform in the shader.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void                    lrg_shader_transition_set_uniform_vec2      (LrgShaderTransition *self,
                                                                     const gchar         *name,
                                                                     gfloat               x,
                                                                     gfloat               y);

/**
 * lrg_shader_transition_set_uniform_vec3:
 * @self: A #LrgShaderTransition
 * @name: Uniform variable name
 * @x: X component
 * @y: Y component
 * @z: Z component
 *
 * Sets a vec3 uniform in the shader.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void                    lrg_shader_transition_set_uniform_vec3      (LrgShaderTransition *self,
                                                                     const gchar         *name,
                                                                     gfloat               x,
                                                                     gfloat               y,
                                                                     gfloat               z);

/**
 * lrg_shader_transition_set_uniform_vec4:
 * @self: A #LrgShaderTransition
 * @name: Uniform variable name
 * @x: X component
 * @y: Y component
 * @z: Z component
 * @w: W component
 *
 * Sets a vec4 uniform in the shader.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void                    lrg_shader_transition_set_uniform_vec4      (LrgShaderTransition *self,
                                                                     const gchar         *name,
                                                                     gfloat               x,
                                                                     gfloat               y,
                                                                     gfloat               z,
                                                                     gfloat               w);

/**
 * lrg_shader_transition_set_uniform_int:
 * @self: A #LrgShaderTransition
 * @name: Uniform variable name
 * @value: Integer value
 *
 * Sets an integer uniform in the shader.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void                    lrg_shader_transition_set_uniform_int       (LrgShaderTransition *self,
                                                                     const gchar         *name,
                                                                     gint                 value);

/**
 * lrg_shader_transition_is_shader_loaded:
 * @self: A #LrgShaderTransition
 *
 * Checks if a shader has been loaded.
 *
 * Returns: %TRUE if shader is loaded
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean                lrg_shader_transition_is_shader_loaded      (LrgShaderTransition *self);

G_END_DECLS
