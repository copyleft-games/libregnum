/* lrg-lighting-manager.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Lighting system manager implementation.
 */

#include "lrg-lighting-manager.h"
#include "lrg-light2d.h"
#include "lrg-shadow-caster.h"
#include "lrg-lightmap.h"
#include "../lrg-log.h"

struct _LrgLightingManager
{
    GObject parent_instance;

    GList        *lights;
    GList        *shadow_casters;
    LrgLightmap  *lightmap;

    /* Ambient lighting */
    guint8 ambient_r, ambient_g, ambient_b;
    gfloat ambient_intensity;

    /* Settings */
    gboolean         shadows_enabled;
    LrgShadowMethod  default_shadow_method;
    LrgLightBlendMode blend_mode;

    /* Viewport */
    gfloat viewport_x, viewport_y;
    gfloat viewport_width, viewport_height;

    /* Render target */
    guint light_texture_id;
};

G_DEFINE_FINAL_TYPE (LrgLightingManager, lrg_lighting_manager, G_TYPE_OBJECT)

enum
{
    PROP_0,
    PROP_SHADOWS_ENABLED,
    PROP_DEFAULT_SHADOW_METHOD,
    PROP_BLEND_MODE,
    PROP_AMBIENT_INTENSITY,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

enum
{
    SIGNAL_LIGHT_ADDED,
    SIGNAL_LIGHT_REMOVED,
    SIGNAL_AMBIENT_CHANGED,
    N_SIGNALS
};

static guint signals[N_SIGNALS];

static void
lrg_lighting_manager_dispose (GObject *object)
{
    LrgLightingManager *self = LRG_LIGHTING_MANAGER (object);

    g_list_free_full (self->lights, g_object_unref);
    self->lights = NULL;

    g_list_free_full (self->shadow_casters, g_object_unref);
    self->shadow_casters = NULL;

    g_clear_object (&self->lightmap);

    G_OBJECT_CLASS (lrg_lighting_manager_parent_class)->dispose (object);
}

static void
lrg_lighting_manager_get_property (GObject    *object,
                                   guint       prop_id,
                                   GValue     *value,
                                   GParamSpec *pspec)
{
    LrgLightingManager *self = LRG_LIGHTING_MANAGER (object);

    switch (prop_id)
    {
    case PROP_SHADOWS_ENABLED:
        g_value_set_boolean (value, self->shadows_enabled);
        break;
    case PROP_DEFAULT_SHADOW_METHOD:
        g_value_set_enum (value, self->default_shadow_method);
        break;
    case PROP_BLEND_MODE:
        g_value_set_enum (value, self->blend_mode);
        break;
    case PROP_AMBIENT_INTENSITY:
        g_value_set_float (value, self->ambient_intensity);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_lighting_manager_set_property (GObject      *object,
                                   guint         prop_id,
                                   const GValue *value,
                                   GParamSpec   *pspec)
{
    LrgLightingManager *self = LRG_LIGHTING_MANAGER (object);

    switch (prop_id)
    {
    case PROP_SHADOWS_ENABLED:
        lrg_lighting_manager_set_shadows_enabled (self, g_value_get_boolean (value));
        break;
    case PROP_DEFAULT_SHADOW_METHOD:
        lrg_lighting_manager_set_default_shadow_method (self, g_value_get_enum (value));
        break;
    case PROP_BLEND_MODE:
        lrg_lighting_manager_set_blend_mode (self, g_value_get_enum (value));
        break;
    case PROP_AMBIENT_INTENSITY:
        lrg_lighting_manager_set_ambient_intensity (self, g_value_get_float (value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_lighting_manager_class_init (LrgLightingManagerClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->dispose = lrg_lighting_manager_dispose;
    object_class->get_property = lrg_lighting_manager_get_property;
    object_class->set_property = lrg_lighting_manager_set_property;

    /**
     * LrgLightingManager:shadows-enabled:
     *
     * Whether shadow casting is enabled globally.
     */
    properties[PROP_SHADOWS_ENABLED] =
        g_param_spec_boolean ("shadows-enabled",
                              "Shadows Enabled",
                              "Whether shadow casting is enabled",
                              TRUE,
                              G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgLightingManager:default-shadow-method:
     *
     * Default shadow calculation method for lights.
     */
    properties[PROP_DEFAULT_SHADOW_METHOD] =
        g_param_spec_enum ("default-shadow-method",
                           "Default Shadow Method",
                           "Default shadow calculation method",
                           LRG_TYPE_SHADOW_METHOD,
                           LRG_SHADOW_METHOD_RAY_CAST,
                           G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgLightingManager:blend-mode:
     *
     * How lights are blended with the scene.
     */
    properties[PROP_BLEND_MODE] =
        g_param_spec_enum ("blend-mode",
                           "Blend Mode",
                           "Light blending mode",
                           LRG_TYPE_LIGHT_BLEND_MODE,
                           LRG_LIGHT_BLEND_MULTIPLY,
                           G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgLightingManager:ambient-intensity:
     *
     * Global ambient light intensity.
     */
    properties[PROP_AMBIENT_INTENSITY] =
        g_param_spec_float ("ambient-intensity",
                            "Ambient Intensity",
                            "Global ambient light intensity",
                            0.0f, 1.0f, 0.2f,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);

    /**
     * LrgLightingManager::light-added:
     * @self: the lighting manager
     * @light: the light that was added
     *
     * Emitted when a light is added to the manager.
     */
    signals[SIGNAL_LIGHT_ADDED] =
        g_signal_new ("light-added",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0,
                      NULL, NULL,
                      NULL,
                      G_TYPE_NONE, 1, LRG_TYPE_LIGHT2D);

    /**
     * LrgLightingManager::light-removed:
     * @self: the lighting manager
     * @light: the light that was removed
     *
     * Emitted when a light is removed from the manager.
     */
    signals[SIGNAL_LIGHT_REMOVED] =
        g_signal_new ("light-removed",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0,
                      NULL, NULL,
                      NULL,
                      G_TYPE_NONE, 1, LRG_TYPE_LIGHT2D);

    /**
     * LrgLightingManager::ambient-changed:
     * @self: the lighting manager
     *
     * Emitted when ambient lighting settings change.
     */
    signals[SIGNAL_AMBIENT_CHANGED] =
        g_signal_new ("ambient-changed",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0,
                      NULL, NULL,
                      NULL,
                      G_TYPE_NONE, 0);
}

static void
lrg_lighting_manager_init (LrgLightingManager *self)
{
    self->lights = NULL;
    self->shadow_casters = NULL;
    self->lightmap = NULL;

    /* Default ambient: dim white */
    self->ambient_r = 50;
    self->ambient_g = 50;
    self->ambient_b = 60;
    self->ambient_intensity = 0.2f;

    /* Default settings */
    self->shadows_enabled = TRUE;
    self->default_shadow_method = LRG_SHADOW_METHOD_RAY_CAST;
    self->blend_mode = LRG_LIGHT_BLEND_MULTIPLY;

    /* Default viewport */
    self->viewport_x = 0.0f;
    self->viewport_y = 0.0f;
    self->viewport_width = 800.0f;
    self->viewport_height = 600.0f;

    self->light_texture_id = 0;
}

/* Public API */

/**
 * lrg_lighting_manager_new:
 *
 * Creates a new lighting manager.
 *
 * Returns: (transfer full): A new #LrgLightingManager
 */
LrgLightingManager *
lrg_lighting_manager_new (void)
{
    return g_object_new (LRG_TYPE_LIGHTING_MANAGER, NULL);
}

/* Light management */

/**
 * lrg_lighting_manager_add_light:
 * @self: an #LrgLightingManager
 * @light: the #LrgLight2D to add
 *
 * Adds a light to the manager.
 */
void
lrg_lighting_manager_add_light (LrgLightingManager *self,
                                LrgLight2D         *light)
{
    g_return_if_fail (LRG_IS_LIGHTING_MANAGER (self));
    g_return_if_fail (LRG_IS_LIGHT2D (light));

    /* Check if already added */
    if (g_list_find (self->lights, light) != NULL)
        return;

    self->lights = g_list_prepend (self->lights, g_object_ref (light));
    g_signal_emit (self, signals[SIGNAL_LIGHT_ADDED], 0, light);

    lrg_debug (LRG_LOG_DOMAIN_LIGHTING, "Added light to manager, total: %u",
               g_list_length (self->lights));
}

/**
 * lrg_lighting_manager_remove_light:
 * @self: an #LrgLightingManager
 * @light: the #LrgLight2D to remove
 *
 * Removes a light from the manager.
 */
void
lrg_lighting_manager_remove_light (LrgLightingManager *self,
                                   LrgLight2D         *light)
{
    GList *link;

    g_return_if_fail (LRG_IS_LIGHTING_MANAGER (self));
    g_return_if_fail (LRG_IS_LIGHT2D (light));

    link = g_list_find (self->lights, light);
    if (link == NULL)
        return;

    self->lights = g_list_delete_link (self->lights, link);
    g_signal_emit (self, signals[SIGNAL_LIGHT_REMOVED], 0, light);
    g_object_unref (light);

    lrg_debug (LRG_LOG_DOMAIN_LIGHTING, "Removed light from manager, remaining: %u",
               g_list_length (self->lights));
}

/**
 * lrg_lighting_manager_get_lights:
 * @self: an #LrgLightingManager
 *
 * Gets all lights in the manager.
 *
 * Returns: (transfer none) (element-type LrgLight2D): List of lights
 */
GList *
lrg_lighting_manager_get_lights (LrgLightingManager *self)
{
    g_return_val_if_fail (LRG_IS_LIGHTING_MANAGER (self), NULL);
    return self->lights;
}

/**
 * lrg_lighting_manager_get_light_count:
 * @self: an #LrgLightingManager
 *
 * Gets the number of lights.
 *
 * Returns: Number of lights
 */
guint
lrg_lighting_manager_get_light_count (LrgLightingManager *self)
{
    g_return_val_if_fail (LRG_IS_LIGHTING_MANAGER (self), 0);
    return g_list_length (self->lights);
}

/* Shadow caster management */

/**
 * lrg_lighting_manager_add_shadow_caster:
 * @self: an #LrgLightingManager
 * @caster: the #LrgShadowCaster to add
 *
 * Adds a shadow caster to the manager.
 */
void
lrg_lighting_manager_add_shadow_caster (LrgLightingManager *self,
                                        LrgShadowCaster    *caster)
{
    g_return_if_fail (LRG_IS_LIGHTING_MANAGER (self));
    g_return_if_fail (LRG_IS_SHADOW_CASTER (caster));

    if (g_list_find (self->shadow_casters, caster) != NULL)
        return;

    self->shadow_casters = g_list_prepend (self->shadow_casters, g_object_ref (caster));
}

/**
 * lrg_lighting_manager_remove_shadow_caster:
 * @self: an #LrgLightingManager
 * @caster: the #LrgShadowCaster to remove
 *
 * Removes a shadow caster from the manager.
 */
void
lrg_lighting_manager_remove_shadow_caster (LrgLightingManager *self,
                                           LrgShadowCaster    *caster)
{
    GList *link;

    g_return_if_fail (LRG_IS_LIGHTING_MANAGER (self));
    g_return_if_fail (LRG_IS_SHADOW_CASTER (caster));

    link = g_list_find (self->shadow_casters, caster);
    if (link == NULL)
        return;

    self->shadow_casters = g_list_delete_link (self->shadow_casters, link);
    g_object_unref (caster);
}

/**
 * lrg_lighting_manager_get_shadow_caster_count:
 * @self: an #LrgLightingManager
 *
 * Gets the number of shadow casters.
 *
 * Returns: Number of shadow casters
 */
guint
lrg_lighting_manager_get_shadow_caster_count (LrgLightingManager *self)
{
    g_return_val_if_fail (LRG_IS_LIGHTING_MANAGER (self), 0);
    return g_list_length (self->shadow_casters);
}

/* Lightmap */

/**
 * lrg_lighting_manager_get_lightmap:
 * @self: an #LrgLightingManager
 *
 * Gets the baked lightmap.
 *
 * Returns: (transfer none) (nullable): The lightmap, or %NULL
 */
LrgLightmap *
lrg_lighting_manager_get_lightmap (LrgLightingManager *self)
{
    g_return_val_if_fail (LRG_IS_LIGHTING_MANAGER (self), NULL);
    return self->lightmap;
}

/**
 * lrg_lighting_manager_set_lightmap:
 * @self: an #LrgLightingManager
 * @lightmap: (nullable): the #LrgLightmap to use
 *
 * Sets the baked lightmap for static lighting.
 */
void
lrg_lighting_manager_set_lightmap (LrgLightingManager *self,
                                   LrgLightmap        *lightmap)
{
    g_return_if_fail (LRG_IS_LIGHTING_MANAGER (self));
    g_return_if_fail (lightmap == NULL || LRG_IS_LIGHTMAP (lightmap));

    if (self->lightmap == lightmap)
        return;

    g_clear_object (&self->lightmap);
    if (lightmap != NULL)
        self->lightmap = g_object_ref (lightmap);
}

/* Ambient */

/**
 * lrg_lighting_manager_get_ambient_color:
 * @self: an #LrgLightingManager
 * @r: (out) (optional): red component
 * @g: (out) (optional): green component
 * @b: (out) (optional): blue component
 *
 * Gets the ambient light color.
 */
void
lrg_lighting_manager_get_ambient_color (LrgLightingManager *self,
                                        guint8             *r,
                                        guint8             *g,
                                        guint8             *b)
{
    g_return_if_fail (LRG_IS_LIGHTING_MANAGER (self));

    if (r != NULL) *r = self->ambient_r;
    if (g != NULL) *g = self->ambient_g;
    if (b != NULL) *b = self->ambient_b;
}

/**
 * lrg_lighting_manager_set_ambient_color:
 * @self: an #LrgLightingManager
 * @r: red component
 * @g: green component
 * @b: blue component
 *
 * Sets the ambient light color.
 */
void
lrg_lighting_manager_set_ambient_color (LrgLightingManager *self,
                                        guint8              r,
                                        guint8              g,
                                        guint8              b)
{
    g_return_if_fail (LRG_IS_LIGHTING_MANAGER (self));

    if (self->ambient_r == r && self->ambient_g == g && self->ambient_b == b)
        return;

    self->ambient_r = r;
    self->ambient_g = g;
    self->ambient_b = b;

    g_signal_emit (self, signals[SIGNAL_AMBIENT_CHANGED], 0);
}

/**
 * lrg_lighting_manager_get_ambient_intensity:
 * @self: an #LrgLightingManager
 *
 * Gets the ambient light intensity.
 *
 * Returns: Ambient intensity (0.0-1.0)
 */
gfloat
lrg_lighting_manager_get_ambient_intensity (LrgLightingManager *self)
{
    g_return_val_if_fail (LRG_IS_LIGHTING_MANAGER (self), 0.0f);
    return self->ambient_intensity;
}

/**
 * lrg_lighting_manager_set_ambient_intensity:
 * @self: an #LrgLightingManager
 * @intensity: the intensity (0.0-1.0)
 *
 * Sets the ambient light intensity.
 */
void
lrg_lighting_manager_set_ambient_intensity (LrgLightingManager *self,
                                            gfloat              intensity)
{
    g_return_if_fail (LRG_IS_LIGHTING_MANAGER (self));

    intensity = CLAMP (intensity, 0.0f, 1.0f);

    if (self->ambient_intensity == intensity)
        return;

    self->ambient_intensity = intensity;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_AMBIENT_INTENSITY]);
    g_signal_emit (self, signals[SIGNAL_AMBIENT_CHANGED], 0);
}

/* Settings */

/**
 * lrg_lighting_manager_get_shadows_enabled:
 * @self: an #LrgLightingManager
 *
 * Checks if shadows are enabled globally.
 *
 * Returns: %TRUE if shadows are enabled
 */
gboolean
lrg_lighting_manager_get_shadows_enabled (LrgLightingManager *self)
{
    g_return_val_if_fail (LRG_IS_LIGHTING_MANAGER (self), FALSE);
    return self->shadows_enabled;
}

/**
 * lrg_lighting_manager_set_shadows_enabled:
 * @self: an #LrgLightingManager
 * @enabled: whether to enable shadows
 *
 * Enables or disables shadow casting globally.
 */
void
lrg_lighting_manager_set_shadows_enabled (LrgLightingManager *self,
                                          gboolean            enabled)
{
    g_return_if_fail (LRG_IS_LIGHTING_MANAGER (self));

    if (self->shadows_enabled == enabled)
        return;

    self->shadows_enabled = enabled;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SHADOWS_ENABLED]);
}

/**
 * lrg_lighting_manager_get_default_shadow_method:
 * @self: an #LrgLightingManager
 *
 * Gets the default shadow calculation method.
 *
 * Returns: The default shadow method
 */
LrgShadowMethod
lrg_lighting_manager_get_default_shadow_method (LrgLightingManager *self)
{
    g_return_val_if_fail (LRG_IS_LIGHTING_MANAGER (self), LRG_SHADOW_METHOD_RAY_CAST);
    return self->default_shadow_method;
}

/**
 * lrg_lighting_manager_set_default_shadow_method:
 * @self: an #LrgLightingManager
 * @method: the shadow method
 *
 * Sets the default shadow calculation method.
 */
void
lrg_lighting_manager_set_default_shadow_method (LrgLightingManager *self,
                                                LrgShadowMethod     method)
{
    g_return_if_fail (LRG_IS_LIGHTING_MANAGER (self));

    if (self->default_shadow_method == method)
        return;

    self->default_shadow_method = method;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_DEFAULT_SHADOW_METHOD]);
}

/**
 * lrg_lighting_manager_get_blend_mode:
 * @self: an #LrgLightingManager
 *
 * Gets the light blending mode.
 *
 * Returns: The blend mode
 */
LrgLightBlendMode
lrg_lighting_manager_get_blend_mode (LrgLightingManager *self)
{
    g_return_val_if_fail (LRG_IS_LIGHTING_MANAGER (self), LRG_LIGHT_BLEND_MULTIPLY);
    return self->blend_mode;
}

/**
 * lrg_lighting_manager_set_blend_mode:
 * @self: an #LrgLightingManager
 * @mode: the blend mode
 *
 * Sets how lights are blended with the scene.
 */
void
lrg_lighting_manager_set_blend_mode (LrgLightingManager *self,
                                     LrgLightBlendMode   mode)
{
    g_return_if_fail (LRG_IS_LIGHTING_MANAGER (self));

    if (self->blend_mode == mode)
        return;

    self->blend_mode = mode;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_BLEND_MODE]);
}

/* Viewport */

/**
 * lrg_lighting_manager_set_viewport:
 * @self: an #LrgLightingManager
 * @x: viewport x position
 * @y: viewport y position
 * @width: viewport width
 * @height: viewport height
 *
 * Sets the visible viewport for culling.
 */
void
lrg_lighting_manager_set_viewport (LrgLightingManager *self,
                                   gfloat              x,
                                   gfloat              y,
                                   gfloat              width,
                                   gfloat              height)
{
    g_return_if_fail (LRG_IS_LIGHTING_MANAGER (self));

    self->viewport_x = x;
    self->viewport_y = y;
    self->viewport_width = width;
    self->viewport_height = height;
}

/* Update and render */

/**
 * lrg_lighting_manager_update:
 * @self: an #LrgLightingManager
 * @delta_time: time since last update in seconds
 *
 * Updates all lights (animations, flicker, etc.).
 */
void
lrg_lighting_manager_update (LrgLightingManager *self,
                             gfloat              delta_time)
{
    GList *l;

    g_return_if_fail (LRG_IS_LIGHTING_MANAGER (self));

    for (l = self->lights; l != NULL; l = l->next)
    {
        LrgLight2D *light = LRG_LIGHT2D (l->data);
        lrg_light2d_update (light, delta_time);
    }
}

/**
 * lrg_lighting_manager_render:
 * @self: an #LrgLightingManager
 *
 * Renders all lights to the light texture.
 *
 * This composites all visible lights into a single texture
 * that can be applied to the scene.
 */
void
lrg_lighting_manager_render (LrgLightingManager *self)
{
    GList *l;
    guint visible_count;

    g_return_if_fail (LRG_IS_LIGHTING_MANAGER (self));

    /*
     * Rendering flow:
     * 1. Begin render to light texture
     * 2. Clear with ambient color
     * 3. For each visible light:
     *    a. Calculate shadows if enabled
     *    b. Render light contribution
     * 4. End render to light texture
     * 5. Blend lightmap if present
     *
     * The actual GPU rendering would use graylib render targets.
     * This is a placeholder showing the structure.
     */

    /* Count visible lights for debugging */
    visible_count = 0;
    for (l = self->lights; l != NULL; l = l->next)
    {
        LrgLight2D *light = LRG_LIGHT2D (l->data);

        if (!lrg_light2d_get_enabled (light))
            continue;

        /* TODO: viewport culling check */
        visible_count++;
    }

    lrg_debug (LRG_LOG_DOMAIN_LIGHTING, "Rendering %u visible lights", visible_count);
}

/**
 * lrg_lighting_manager_get_light_texture_id:
 * @self: an #LrgLightingManager
 *
 * Gets the GPU texture ID of the rendered light map.
 *
 * Returns: OpenGL texture ID
 */
guint
lrg_lighting_manager_get_light_texture_id (LrgLightingManager *self)
{
    g_return_val_if_fail (LRG_IS_LIGHTING_MANAGER (self), 0);
    return self->light_texture_id;
}
