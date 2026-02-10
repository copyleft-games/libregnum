/* lrg-shader-transition.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Custom shader-based transition.
 */

#include "lrg-shader-transition.h"
#include "../tween/lrg-easing.h"
#include "../lrg-log.h"

#include <graylib.h>

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_TRANSITION

/**
 * LrgShaderTransition:
 *
 * A transition that uses a custom fragment shader for rendering.
 *
 * This transition allows for fully custom transition effects by
 * providing a fragment shader that receives:
 *
 * - `u_progress`: Overall transition progress (0.0 to 1.0)
 * - `u_phase`: Current phase (0=OUT, 1=HOLD, 2=IN)
 * - `u_phase_progress`: Progress within current phase (0.0 to 1.0)
 * - `u_old_scene`: Sampler2D for old scene texture
 * - `u_new_scene`: Sampler2D for new scene texture
 * - `u_resolution`: vec2 viewport resolution
 *
 * ## Shader Requirements
 *
 * The shader should define the following inputs:
 *
 * ```glsl
 * uniform float u_progress;
 * uniform int u_phase;
 * uniform float u_phase_progress;
 * uniform sampler2D u_old_scene;
 * uniform sampler2D u_new_scene;
 * uniform vec2 u_resolution;
 * ```
 *
 * ## Example shader (circle wipe)
 *
 * ```glsl
 * void main() {
 *     vec2 uv = gl_FragCoord.xy / u_resolution;
 *     vec2 center = vec2(0.5, 0.5);
 *     float dist = distance(uv, center);
 *     float radius = u_progress * 1.5;
 *
 *     if (dist < radius) {
 *         gl_FragColor = texture2D(u_new_scene, uv);
 *     } else {
 *         gl_FragColor = texture2D(u_old_scene, uv);
 *     }
 * }
 * ```
 *
 * Since: 1.0
 */

/* Error domain for shader transitions */
#define LRG_SHADER_TRANSITION_ERROR (lrg_shader_transition_error_quark ())

typedef enum
{
    LRG_SHADER_TRANSITION_ERROR_LOAD_FAILED,
    LRG_SHADER_TRANSITION_ERROR_COMPILE_FAILED,
} LrgShaderTransitionError;

static GQuark
lrg_shader_transition_error_quark (void)
{
    return g_quark_from_static_string ("lrg-shader-transition-error");
}

typedef struct _ShaderUniform
{
    gchar  *name;
    gint    type;   /* 0=float, 1=vec2, 2=vec3, 3=vec4, 4=int */
    gfloat  values[4];
    gint    int_value;
} ShaderUniform;

struct _LrgShaderTransition
{
    LrgTransition parent_instance;

    gchar      *fragment_source;
    gboolean    shader_loaded;
    GrlShader  *shader;

    /* Custom uniforms */
    GHashTable *uniforms;
};

enum
{
    PROP_0,
    PROP_SHADER_LOADED,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

G_DEFINE_FINAL_TYPE (LrgShaderTransition, lrg_shader_transition, LRG_TYPE_TRANSITION)

/*
 * Uniform helpers
 */

static ShaderUniform *
shader_uniform_new (void)
{
    return g_slice_new0 (ShaderUniform);
}

static void
shader_uniform_free (gpointer data)
{
    ShaderUniform *uniform;

    uniform = (ShaderUniform *) data;
    if (uniform != NULL)
    {
        g_free (uniform->name);
        g_slice_free (ShaderUniform, uniform);
    }
}

/*
 * LrgTransition virtual method implementations
 */

static gboolean
lrg_shader_transition_initialize (LrgTransition  *transition,
                                  guint           width,
                                  guint           height,
                                  GError        **error)
{
    LrgShaderTransition *self;

    self = LRG_SHADER_TRANSITION (transition);

    if (!self->shader_loaded)
    {
        g_set_error (error,
                     LRG_SHADER_TRANSITION_ERROR,
                     LRG_SHADER_TRANSITION_ERROR_LOAD_FAILED,
                     "No shader loaded for shader transition");
        return FALSE;
    }

    (void) width;
    (void) height;

    /* Compile shader from stored fragment source */
    self->shader = grl_shader_new_from_memory (NULL, self->fragment_source, error);
    if (self->shader == NULL)
    {
        lrg_warning (LRG_LOG_DOMAIN_TRANSITION, "Failed to compile custom shader");
        return FALSE;
    }

    lrg_debug (LRG_LOG_DOMAIN_TRANSITION, "Shader transition initialized (viewport: %ux%u)", width, height);

    return TRUE;
}

static void
lrg_shader_transition_shutdown (LrgTransition *transition)
{
    LrgShaderTransition *self;

    self = LRG_SHADER_TRANSITION (transition);

    /* Unload the compiled shader */
    g_clear_object (&self->shader);

    lrg_debug (LRG_LOG_DOMAIN_TRANSITION, "Shader transition shutdown");
}

static void
lrg_shader_transition_start (LrgTransition *transition)
{
    (void) transition;

    lrg_debug (LRG_LOG_DOMAIN_TRANSITION, "Shader transition started");
}

static void
lrg_shader_transition_update (LrgTransition *transition,
                              gfloat         delta_time)
{
    (void) transition;
    (void) delta_time;
}

static void
lrg_shader_transition_render (LrgTransition *transition,
                              guint          old_scene_texture,
                              guint          new_scene_texture,
                              guint          width,
                              guint          height)
{
    LrgShaderTransition *self;
    LrgTransitionState state;
    gfloat overall_progress;
    gfloat phase_progress;
    gint phase_int;

    self = LRG_SHADER_TRANSITION (transition);
    state = lrg_transition_get_state (transition);
    overall_progress = lrg_transition_get_progress (transition);
    phase_progress = lrg_transition_get_phase_progress (transition);

    /* Convert state to shader phase int */
    switch (state)
    {
    case LRG_TRANSITION_STATE_OUT:
        phase_int = 0;
        break;
    case LRG_TRANSITION_STATE_HOLD:
        phase_int = 1;
        break;
    case LRG_TRANSITION_STATE_IN:
        phase_int = 2;
        break;
    default:
        phase_int = -1;
        break;
    }

    if (self->shader == NULL)
        return;

    {
        gint loc_progress;
        gint loc_phase;
        gint loc_phase_progress;
        gint loc_resolution;
        gint loc_old_scene;
        gint loc_new_scene;
        GHashTableIter iter;
        gpointer key;
        gpointer value;
        GrlColor white;

        /* Get built-in uniform locations */
        loc_progress = grl_shader_get_location (self->shader, "u_progress");
        loc_phase = grl_shader_get_location (self->shader, "u_phase");
        loc_phase_progress = grl_shader_get_location (self->shader, "u_phase_progress");
        loc_resolution = grl_shader_get_location (self->shader, "u_resolution");
        loc_old_scene = grl_shader_get_location (self->shader, "u_old_scene");
        loc_new_scene = grl_shader_get_location (self->shader, "u_new_scene");

        grl_shader_begin (self->shader);

        /* Set built-in uniforms */
        grl_shader_set_value_float (self->shader, loc_progress, overall_progress);
        grl_shader_set_value_int (self->shader, loc_phase, phase_int);
        grl_shader_set_value_float (self->shader, loc_phase_progress, phase_progress);
        grl_shader_set_value_vec2 (self->shader, loc_resolution, (gfloat) width, (gfloat) height);

        /* Set scene textures using low-level sampler API for raw texture IDs */
        if (loc_old_scene >= 0)
            grl_rlgl_set_uniform_sampler (loc_old_scene, old_scene_texture);
        if (loc_new_scene >= 0)
            grl_rlgl_set_uniform_sampler (loc_new_scene, new_scene_texture);

        /* Set custom uniforms from the hash table */
        g_hash_table_iter_init (&iter, self->uniforms);
        while (g_hash_table_iter_next (&iter, &key, &value))
        {
            ShaderUniform *u;
            gint loc;

            u = (ShaderUniform *) value;
            loc = grl_shader_get_location (self->shader, u->name);
            if (loc < 0)
                continue;

            switch (u->type)
            {
            case 0: /* float */
                grl_shader_set_value_float (self->shader, loc, u->values[0]);
                break;
            case 1: /* vec2 */
                grl_shader_set_value_vec2 (self->shader, loc, u->values[0], u->values[1]);
                break;
            case 2: /* vec3 */
                grl_shader_set_value_vec3 (self->shader, loc, u->values[0], u->values[1], u->values[2]);
                break;
            case 3: /* vec4 */
                grl_shader_set_value_vec4 (self->shader, loc, u->values[0], u->values[1], u->values[2], u->values[3]);
                break;
            case 4: /* int */
                grl_shader_set_value_int (self->shader, loc, u->int_value);
                break;
            default:
                break;
            }
        }

        /* Draw fullscreen quad */
        white = grl_color_init (255, 255, 255, 255);
        grl_draw_rectangle (0, 0, (gint) width, (gint) height, &white);

        grl_shader_end (self->shader);
    }
}

static void
lrg_shader_transition_reset (LrgTransition *transition)
{
    (void) transition;

    lrg_debug (LRG_LOG_DOMAIN_TRANSITION, "Shader transition reset");
}

/*
 * GObject virtual methods
 */

static void
lrg_shader_transition_get_property (GObject    *object,
                                    guint       prop_id,
                                    GValue     *value,
                                    GParamSpec *pspec)
{
    LrgShaderTransition *self;

    self = LRG_SHADER_TRANSITION (object);

    switch (prop_id)
    {
    case PROP_SHADER_LOADED:
        g_value_set_boolean (value, self->shader_loaded);
        break;

    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
lrg_shader_transition_finalize (GObject *object)
{
    LrgShaderTransition *self;

    self = LRG_SHADER_TRANSITION (object);

    g_clear_pointer (&self->fragment_source, g_free);
    g_clear_pointer (&self->uniforms, g_hash_table_unref);

    G_OBJECT_CLASS (lrg_shader_transition_parent_class)->finalize (object);
}

static void
lrg_shader_transition_class_init (LrgShaderTransitionClass *klass)
{
    GObjectClass *object_class;
    LrgTransitionClass *transition_class;

    object_class = G_OBJECT_CLASS (klass);
    object_class->get_property = lrg_shader_transition_get_property;
    object_class->finalize = lrg_shader_transition_finalize;

    transition_class = LRG_TRANSITION_CLASS (klass);
    transition_class->initialize = lrg_shader_transition_initialize;
    transition_class->shutdown = lrg_shader_transition_shutdown;
    transition_class->start = lrg_shader_transition_start;
    transition_class->update = lrg_shader_transition_update;
    transition_class->render = lrg_shader_transition_render;
    transition_class->reset = lrg_shader_transition_reset;

    /**
     * LrgShaderTransition:shader-loaded:
     *
     * Whether a shader has been loaded.
     *
     * Since: 1.0
     */
    properties[PROP_SHADER_LOADED] =
        g_param_spec_boolean ("shader-loaded",
                              "Shader Loaded",
                              "Whether a shader has been loaded",
                              FALSE,
                              G_PARAM_READABLE |
                              G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_shader_transition_init (LrgShaderTransition *self)
{
    self->fragment_source = NULL;
    self->shader_loaded = FALSE;
    self->shader = NULL;
    self->uniforms = g_hash_table_new_full (g_str_hash, g_str_equal,
                                            NULL, shader_uniform_free);
}

/*
 * Public API
 */

/**
 * lrg_shader_transition_new:
 *
 * Creates a new shader transition.
 *
 * Returns: (transfer full): A new #LrgShaderTransition
 *
 * Since: 1.0
 */
LrgShaderTransition *
lrg_shader_transition_new (void)
{
    return g_object_new (LRG_TYPE_SHADER_TRANSITION, NULL);
}

/**
 * lrg_shader_transition_new_from_file:
 * @fragment_path: Path to fragment shader file
 * @error: (nullable): Return location for error
 *
 * Creates a new shader transition from a file.
 *
 * Returns: (transfer full) (nullable): A new #LrgShaderTransition, or %NULL on error
 *
 * Since: 1.0
 */
LrgShaderTransition *
lrg_shader_transition_new_from_file (const gchar  *fragment_path,
                                     GError      **error)
{
    LrgShaderTransition *self;

    g_return_val_if_fail (fragment_path != NULL, NULL);

    self = lrg_shader_transition_new ();
    if (!lrg_shader_transition_load_from_file (self, fragment_path, error))
    {
        g_object_unref (self);
        return NULL;
    }

    return self;
}

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
LrgShaderTransition *
lrg_shader_transition_new_from_source (const gchar  *fragment_source,
                                       GError      **error)
{
    LrgShaderTransition *self;

    g_return_val_if_fail (fragment_source != NULL, NULL);

    self = lrg_shader_transition_new ();
    if (!lrg_shader_transition_load_from_source (self, fragment_source, error))
    {
        g_object_unref (self);
        return NULL;
    }

    return self;
}

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
gboolean
lrg_shader_transition_load_from_file (LrgShaderTransition  *self,
                                      const gchar          *fragment_path,
                                      GError              **error)
{
    g_autofree gchar *contents = NULL;
    gsize length;

    g_return_val_if_fail (LRG_IS_SHADER_TRANSITION (self), FALSE);
    g_return_val_if_fail (fragment_path != NULL, FALSE);

    if (!g_file_get_contents (fragment_path, &contents, &length, error))
    {
        return FALSE;
    }

    return lrg_shader_transition_load_from_source (self, contents, error);
}

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
gboolean
lrg_shader_transition_load_from_source (LrgShaderTransition  *self,
                                        const gchar          *fragment_source,
                                        GError              **error)
{
    g_return_val_if_fail (LRG_IS_SHADER_TRANSITION (self), FALSE);
    g_return_val_if_fail (fragment_source != NULL, FALSE);

    (void) error;

    g_clear_pointer (&self->fragment_source, g_free);
    self->fragment_source = g_strdup (fragment_source);
    self->shader_loaded = TRUE;

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SHADER_LOADED]);

    lrg_debug (LRG_LOG_DOMAIN_TRANSITION, "Shader loaded (%zu bytes)", strlen (fragment_source));

    return TRUE;
}

/**
 * lrg_shader_transition_set_uniform_float:
 * @self: A #LrgShaderTransition
 * @name: Uniform name
 * @value: Float value
 *
 * Sets a float uniform.
 *
 * Since: 1.0
 */
void
lrg_shader_transition_set_uniform_float (LrgShaderTransition *self,
                                         const gchar         *name,
                                         gfloat               value)
{
    ShaderUniform *uniform;

    g_return_if_fail (LRG_IS_SHADER_TRANSITION (self));
    g_return_if_fail (name != NULL);

    uniform = shader_uniform_new ();
    uniform->name = g_strdup (name);
    uniform->type = 0;
    uniform->values[0] = value;

    g_hash_table_replace (self->uniforms, uniform->name, uniform);
}

/**
 * lrg_shader_transition_set_uniform_vec2:
 * @self: A #LrgShaderTransition
 * @name: Uniform name
 * @x: X component
 * @y: Y component
 *
 * Sets a vec2 uniform.
 *
 * Since: 1.0
 */
void
lrg_shader_transition_set_uniform_vec2 (LrgShaderTransition *self,
                                        const gchar         *name,
                                        gfloat               x,
                                        gfloat               y)
{
    ShaderUniform *uniform;

    g_return_if_fail (LRG_IS_SHADER_TRANSITION (self));
    g_return_if_fail (name != NULL);

    uniform = shader_uniform_new ();
    uniform->name = g_strdup (name);
    uniform->type = 1;
    uniform->values[0] = x;
    uniform->values[1] = y;

    g_hash_table_replace (self->uniforms, uniform->name, uniform);
}

/**
 * lrg_shader_transition_set_uniform_vec3:
 * @self: A #LrgShaderTransition
 * @name: Uniform name
 * @x: X component
 * @y: Y component
 * @z: Z component
 *
 * Sets a vec3 uniform.
 *
 * Since: 1.0
 */
void
lrg_shader_transition_set_uniform_vec3 (LrgShaderTransition *self,
                                        const gchar         *name,
                                        gfloat               x,
                                        gfloat               y,
                                        gfloat               z)
{
    ShaderUniform *uniform;

    g_return_if_fail (LRG_IS_SHADER_TRANSITION (self));
    g_return_if_fail (name != NULL);

    uniform = shader_uniform_new ();
    uniform->name = g_strdup (name);
    uniform->type = 2;
    uniform->values[0] = x;
    uniform->values[1] = y;
    uniform->values[2] = z;

    g_hash_table_replace (self->uniforms, uniform->name, uniform);
}

/**
 * lrg_shader_transition_set_uniform_vec4:
 * @self: A #LrgShaderTransition
 * @name: Uniform name
 * @x: X component
 * @y: Y component
 * @z: Z component
 * @w: W component
 *
 * Sets a vec4 uniform.
 *
 * Since: 1.0
 */
void
lrg_shader_transition_set_uniform_vec4 (LrgShaderTransition *self,
                                        const gchar         *name,
                                        gfloat               x,
                                        gfloat               y,
                                        gfloat               z,
                                        gfloat               w)
{
    ShaderUniform *uniform;

    g_return_if_fail (LRG_IS_SHADER_TRANSITION (self));
    g_return_if_fail (name != NULL);

    uniform = shader_uniform_new ();
    uniform->name = g_strdup (name);
    uniform->type = 3;
    uniform->values[0] = x;
    uniform->values[1] = y;
    uniform->values[2] = z;
    uniform->values[3] = w;

    g_hash_table_replace (self->uniforms, uniform->name, uniform);
}

/**
 * lrg_shader_transition_set_uniform_int:
 * @self: A #LrgShaderTransition
 * @name: Uniform name
 * @value: Integer value
 *
 * Sets an integer uniform.
 *
 * Since: 1.0
 */
void
lrg_shader_transition_set_uniform_int (LrgShaderTransition *self,
                                       const gchar         *name,
                                       gint                 value)
{
    ShaderUniform *uniform;

    g_return_if_fail (LRG_IS_SHADER_TRANSITION (self));
    g_return_if_fail (name != NULL);

    uniform = shader_uniform_new ();
    uniform->name = g_strdup (name);
    uniform->type = 4;
    uniform->int_value = value;

    g_hash_table_replace (self->uniforms, uniform->name, uniform);
}

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
gboolean
lrg_shader_transition_is_shader_loaded (LrgShaderTransition *self)
{
    g_return_val_if_fail (LRG_IS_SHADER_TRANSITION (self), FALSE);

    return self->shader_loaded;
}
