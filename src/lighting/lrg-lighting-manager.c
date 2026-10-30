/* lrg-lighting-manager.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Lighting system manager implementation.
 */

#include "config.h"

#include <math.h>
#include <raylib.h>
#include <graylib.h>

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

    /* Lightmap placement in world units (width <= 0: lightmap size) */
    gfloat lightmap_x, lightmap_y;
    gfloat lightmap_width, lightmap_height;

    /* Light pass */
    GrlRenderTexture *light_target;
    GrlCamera2D      *light_camera;
    guint             rendered_light_count;

    /* Render target */
    guint light_texture_id;
};

/* Largest side of the light render target, in pixels. A viewport larger
 * than this (a zoomed-out camera) renders at reduced resolution. */
#define LIGHT_TARGET_MAX_SIZE (4096.0f)

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
    g_clear_object (&self->light_target);
    g_clear_object (&self->light_camera);

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

    self->lightmap_x = 0.0f;
    self->lightmap_y = 0.0f;
    self->lightmap_width = 0.0f;
    self->lightmap_height = 0.0f;

    self->light_target = NULL;
    self->light_camera = NULL;
    self->rendered_light_count = 0;

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

/* Viewport queries */

/**
 * lrg_lighting_manager_get_viewport:
 * @self: an #LrgLightingManager
 * @x: (out) (optional): viewport x position (world units)
 * @y: (out) (optional): viewport y position (world units)
 * @width: (out) (optional): viewport width (world units)
 * @height: (out) (optional): viewport height (world units)
 *
 * Gets the world-space rectangle lights are culled against and the
 * light pass renders.
 */
void
lrg_lighting_manager_get_viewport (LrgLightingManager *self,
                                   gfloat             *x,
                                   gfloat             *y,
                                   gfloat             *width,
                                   gfloat             *height)
{
    g_return_if_fail (LRG_IS_LIGHTING_MANAGER (self));

    if (x != NULL) *x = self->viewport_x;
    if (y != NULL) *y = self->viewport_y;
    if (width != NULL) *width = self->viewport_width;
    if (height != NULL) *height = self->viewport_height;
}

/**
 * lrg_lighting_manager_set_viewport_from_camera:
 * @self: an #LrgLightingManager
 * @camera: the #LrgCamera2D the scene is drawn with
 * @screen_width: width of the screen (or render target) the camera
 *   draws into, in pixels
 * @screen_height: height of that screen, in pixels
 *
 * Sets the viewport to the world-space area @camera shows on a screen of
 * the given size: the axis-aligned bounding box of the four screen
 * corners mapped back through the camera's offset, zoom and rotation
 * (the inverse of raylib's 2D camera transform).  A rotated camera
 * therefore culls against a slightly larger box than it shows, which
 * can only keep extra lights, never drop visible ones.
 */
void
lrg_lighting_manager_set_viewport_from_camera (LrgLightingManager *self,
                                               LrgCamera2D        *camera,
                                               gfloat              screen_width,
                                               gfloat              screen_height)
{
    g_autoptr(GrlVector2) target = NULL;
    g_autoptr(GrlVector2) offset = NULL;
    gfloat zoom;
    gfloat angle;
    gfloat cos_a;
    gfloat sin_a;
    gfloat min_x, min_y, max_x, max_y;
    guint i;

    g_return_if_fail (LRG_IS_LIGHTING_MANAGER (self));
    g_return_if_fail (LRG_IS_CAMERA2D (camera));

    target = lrg_camera2d_get_target (camera);
    offset = lrg_camera2d_get_offset (camera);
    zoom = lrg_camera2d_get_zoom (camera);
    if (zoom <= 0.0f)
        zoom = 1.0f;

    /*
     * raylib: screen = R(rot) * (world - target) * zoom + offset, so
     * world = target + R(-rot) * (screen - offset) / zoom.
     */
    angle = -lrg_camera2d_get_rotation (camera) * (gfloat) G_PI / 180.0f;
    cos_a = cosf (angle);
    sin_a = sinf (angle);

    min_x = min_y = G_MAXFLOAT;
    max_x = max_y = -G_MAXFLOAT;

    for (i = 0; i < 4; i++)
    {
        gfloat sx, sy;
        gfloat wx, wy;

        /* Corners in order (0,0) (w,0) (0,h) (w,h). */
        sx = ((i & 1u) != 0 ? screen_width : 0.0f) - offset->x;
        sy = ((i & 2u) != 0 ? screen_height : 0.0f) - offset->y;
        sx /= zoom;
        sy /= zoom;

        wx = target->x + sx * cos_a - sy * sin_a;
        wy = target->y + sx * sin_a + sy * cos_a;

        min_x = MIN (min_x, wx);
        min_y = MIN (min_y, wy);
        max_x = MAX (max_x, wx);
        max_y = MAX (max_y, wy);
    }

    lrg_lighting_manager_set_viewport (self, min_x, min_y,
                                       max_x - min_x, max_y - min_y);
}

/* Culling */

/**
 * lrg_lighting_manager_get_visible_lights:
 * @self: an #LrgLightingManager
 *
 * Culls the lights against the current viewport: returns the enabled
 * lights whose #LrgLight2DClass.is_visible reports that their area of
 * effect overlaps the viewport (point and spot lights test their
 * radius bounds, directional lights are always visible while enabled).
 * This is the set the light pass draws.
 *
 * Returns: (transfer container) (element-type LrgLight2D): the visible
 *   lights, in the manager's order
 */
GPtrArray *
lrg_lighting_manager_get_visible_lights (LrgLightingManager *self)
{
    GPtrArray *visible;
    GList *l;

    g_return_val_if_fail (LRG_IS_LIGHTING_MANAGER (self), NULL);

    visible = g_ptr_array_new ();

    for (l = self->lights; l != NULL; l = l->next)
    {
        LrgLight2D *light = LRG_LIGHT2D (l->data);

        if (!lrg_light2d_get_enabled (light))
            continue;

        if (lrg_light2d_is_visible (light,
                                    self->viewport_x, self->viewport_y,
                                    self->viewport_width, self->viewport_height))
            g_ptr_array_add (visible, light);
    }

    return visible;
}

/**
 * lrg_lighting_manager_get_visible_light_count:
 * @self: an #LrgLightingManager
 *
 * Counts the lights that survive viewport culling.
 *
 * Returns: the number of visible lights
 */
guint
lrg_lighting_manager_get_visible_light_count (LrgLightingManager *self)
{
    g_autoptr(GPtrArray) visible = NULL;

    g_return_val_if_fail (LRG_IS_LIGHTING_MANAGER (self), 0);

    visible = lrg_lighting_manager_get_visible_lights (self);
    return visible->len;
}

/* Lightmap placement */

/**
 * lrg_lighting_manager_set_lightmap_bounds:
 * @self: an #LrgLightingManager
 * @x: world x of the lightmap's top-left corner
 * @y: world y of the lightmap's top-left corner
 * @width: world width the lightmap is stretched over, or 0 for one
 *   world unit per texel
 * @height: world height the lightmap is stretched over, or 0 for one
 *   world unit per texel
 *
 * Places the baked lightmap in the world.  By default it sits at the
 * origin with one texel per world unit.
 */
void
lrg_lighting_manager_set_lightmap_bounds (LrgLightingManager *self,
                                          gfloat              x,
                                          gfloat              y,
                                          gfloat              width,
                                          gfloat              height)
{
    g_return_if_fail (LRG_IS_LIGHTING_MANAGER (self));

    self->lightmap_x = x;
    self->lightmap_y = y;
    self->lightmap_width = MAX (width, 0.0f);
    self->lightmap_height = MAX (height, 0.0f);
}

/**
 * lrg_lighting_manager_get_lightmap_bounds:
 * @self: an #LrgLightingManager
 * @x: (out) (optional): world x of the top-left corner
 * @y: (out) (optional): world y of the top-left corner
 * @width: (out) (optional): world width (resolved: the lightmap's width
 *   in texels when left at 0, or 0 without a lightmap)
 * @height: (out) (optional): world height (resolved likewise)
 *
 * Gets where the baked lightmap is drawn in the world.
 */
void
lrg_lighting_manager_get_lightmap_bounds (LrgLightingManager *self,
                                          gfloat             *x,
                                          gfloat             *y,
                                          gfloat             *width,
                                          gfloat             *height)
{
    gfloat w;
    gfloat h;

    g_return_if_fail (LRG_IS_LIGHTING_MANAGER (self));

    w = self->lightmap_width;
    h = self->lightmap_height;
    if (w <= 0.0f)
        w = self->lightmap != NULL ? (gfloat) lrg_lightmap_get_width (self->lightmap) : 0.0f;
    if (h <= 0.0f)
        h = self->lightmap != NULL ? (gfloat) lrg_lightmap_get_height (self->lightmap) : 0.0f;

    if (x != NULL) *x = self->lightmap_x;
    if (y != NULL) *y = self->lightmap_y;
    if (width != NULL) *width = w;
    if (height != NULL) *height = h;
}

/* Light pass */

/*
 * ensure_light_target:
 *
 * (Re)creates the render target and the camera that maps the viewport
 * onto it.  The target matches the viewport one pixel per world unit,
 * scaled down uniformly when a side would exceed LIGHT_TARGET_MAX_SIZE.
 */
static gboolean
ensure_light_target (LrgLightingManager *self)
{
    gfloat scale;
    gint   width;
    gint   height;

    scale = 1.0f;
    if (self->viewport_width > LIGHT_TARGET_MAX_SIZE ||
        self->viewport_height > LIGHT_TARGET_MAX_SIZE)
        scale = LIGHT_TARGET_MAX_SIZE / MAX (self->viewport_width, self->viewport_height);

    width = MAX ((gint) ceilf (self->viewport_width * scale), 1);
    height = MAX ((gint) ceilf (self->viewport_height * scale), 1);

    if (self->light_target == NULL ||
        grl_render_texture_get_width (self->light_target) != width ||
        grl_render_texture_get_height (self->light_target) != height)
    {
        g_clear_object (&self->light_target);
        self->light_target = grl_render_texture_new (width, height);
        if (self->light_target == NULL || !grl_render_texture_is_valid (self->light_target))
        {
            g_clear_object (&self->light_target);
            self->light_texture_id = 0;
            return FALSE;
        }
    }

    if (self->light_camera == NULL)
        self->light_camera = grl_camera2d_new ();

    /* World (viewport_x, viewport_y) lands on target pixel (0, 0). */
    grl_camera2d_set_offset_xy (self->light_camera, 0.0f, 0.0f);
    grl_camera2d_set_target_xy (self->light_camera, self->viewport_x, self->viewport_y);
    grl_camera2d_set_rotation (self->light_camera, 0.0f);
    grl_camera2d_set_zoom (self->light_camera, scale);

    return TRUE;
}

/*
 * draw_lightmap:
 *
 * Adds the baked lightmap, stretched over its world bounds, to the
 * light pass (the caller has the additive blend and the light camera
 * active).
 */
static void
draw_lightmap (LrgLightingManager *self)
{
    g_autoptr(GrlRectangle) source = NULL;
    g_autoptr(GrlRectangle) dest = NULL;
    g_autoptr(GrlVector2) origin = NULL;
    g_autoptr(GrlColor) tint = NULL;
    GrlTexture *texture;
    gfloat x, y, w, h;

    lrg_lightmap_upload (self->lightmap);
    texture = lrg_lightmap_get_texture (self->lightmap);
    if (texture == NULL)
        return;

    lrg_lighting_manager_get_lightmap_bounds (self, &x, &y, &w, &h);

    source = grl_rectangle_new (0.0f, 0.0f,
                                (gfloat) lrg_lightmap_get_width (self->lightmap),
                                (gfloat) lrg_lightmap_get_height (self->lightmap));
    dest = grl_rectangle_new (x, y, w, h);
    origin = grl_vector2_new (0.0f, 0.0f);
    tint = grl_color_new (255, 255, 255, 255);

    grl_draw_texture_pro (texture, source, dest, origin, 0.0f, tint);
}

/**
 * lrg_lighting_manager_render:
 * @self: an #LrgLightingManager
 *
 * Runs the light pass: renders every visible light into the light
 * target, an off-screen texture covering the viewport.
 *
 * 1. Cull the lights against the viewport
 *    (lrg_lighting_manager_get_visible_lights()).
 * 2. Clear the target to the ambient colour scaled by the ambient
 *    intensity.
 * 3. With additive blending and a camera mapping world units onto the
 *    target, add the baked lightmap (if any) and then each visible
 *    light: lights that cast shadows first get
 *    lrg_light2d_calculate_shadows() with the registered shadow casters
 *    (when shadows are enabled), then lrg_light2d_render().
 *
 * Call it once per frame after lrg_lighting_manager_update() and before
 * drawing the scene, outside any other render-texture or camera mode;
 * then draw the scene and call lrg_lighting_manager_composite() inside
 * the scene's camera mode.
 *
 * Without a window (no GL context) only step 1 runs, so
 * lrg_lighting_manager_get_rendered_light_count() still reports what
 * would be drawn.
 */
void
lrg_lighting_manager_render (LrgLightingManager *self)
{
    g_autoptr(GPtrArray) visible = NULL;
    g_autoptr(GPtrArray) casters = NULL;
    g_autoptr(GrlColor) ambient = NULL;
    g_autoptr(GrlTexture) texture = NULL;
    GList *l;
    guint i;

    g_return_if_fail (LRG_IS_LIGHTING_MANAGER (self));

    visible = lrg_lighting_manager_get_visible_lights (self);
    self->rendered_light_count = visible->len;

    if (!IsWindowReady ())
        return;

    if (!ensure_light_target (self))
        return;

    /* Shadow casters, shared by every shadow-casting light this frame. */
    casters = g_ptr_array_new ();
    for (l = self->shadow_casters; l != NULL; l = l->next)
        g_ptr_array_add (casters, l->data);

    ambient = grl_color_new ((guint8) (self->ambient_r * self->ambient_intensity),
                             (guint8) (self->ambient_g * self->ambient_intensity),
                             (guint8) (self->ambient_b * self->ambient_intensity),
                             255);

    grl_render_texture_begin (self->light_target);
    grl_draw_clear_background (ambient);
    grl_camera2d_begin (self->light_camera);
    grl_draw_begin_blend_mode (GRL_BLEND_ADDITIVE);

    if (self->lightmap != NULL)
        draw_lightmap (self);

    texture = grl_render_texture_get_texture (self->light_target);
    self->light_texture_id = texture != NULL
        ? ((Texture2D *) grl_texture_get_handle (texture))->id
        : 0;

    for (i = 0; i < visible->len; i++)
    {
        LrgLight2D *light = g_ptr_array_index (visible, i);

        if (self->shadows_enabled && casters->len > 0 &&
            lrg_light2d_get_casts_shadows (light))
            lrg_light2d_calculate_shadows (light, casters);

        lrg_light2d_render (light, self->light_texture_id,
                            (guint) grl_render_texture_get_width (self->light_target),
                            (guint) grl_render_texture_get_height (self->light_target));
    }

    grl_draw_end_blend_mode ();
    grl_camera2d_end (self->light_camera);
    grl_render_texture_end (self->light_target);
}

/**
 * lrg_lighting_manager_composite:
 * @self: an #LrgLightingManager
 *
 * Applies the last light pass to what has been drawn: draws the light
 * target over the viewport rectangle (in world coordinates, so call it
 * inside the same camera mode the scene was drawn with) using the
 * manager's #LrgLightingManager:blend-mode:
 *
 * - %LRG_LIGHT_BLEND_MULTIPLY: scene x light (darkness where unlit);
 * - %LRG_LIGHT_BLEND_ADDITIVE: scene + light (glow);
 * - %LRG_LIGHT_BLEND_SOFT: the light layered at half opacity, a cheap
 *   approximation of soft light.
 *
 * Does nothing before the first lrg_lighting_manager_render() that had
 * a GL context.
 */
void
lrg_lighting_manager_composite (LrgLightingManager *self)
{
    g_autoptr(GrlTexture) texture = NULL;
    g_autoptr(GrlRectangle) source = NULL;
    g_autoptr(GrlRectangle) dest = NULL;
    g_autoptr(GrlVector2) origin = NULL;
    g_autoptr(GrlColor) tint = NULL;
    GrlBlendMode mode;

    g_return_if_fail (LRG_IS_LIGHTING_MANAGER (self));

    if (self->light_target == NULL || !IsWindowReady ())
        return;

    texture = grl_render_texture_get_texture (self->light_target);
    if (texture == NULL)
        return;

    switch (self->blend_mode)
    {
    case LRG_LIGHT_BLEND_ADDITIVE:
        mode = GRL_BLEND_ADDITIVE;
        tint = grl_color_new (255, 255, 255, 255);
        break;
    case LRG_LIGHT_BLEND_SOFT:
        mode = GRL_BLEND_ALPHA;
        tint = grl_color_new (255, 255, 255, 128);
        break;
    case LRG_LIGHT_BLEND_MULTIPLY:
    default:
        mode = GRL_BLEND_MULTIPLIED;
        tint = grl_color_new (255, 255, 255, 255);
        break;
    }

    /* Render textures are stored bottom-up: flip with a negative height. */
    source = grl_rectangle_new (0.0f, 0.0f,
                                (gfloat) grl_render_texture_get_width (self->light_target),
                                -(gfloat) grl_render_texture_get_height (self->light_target));
    dest = grl_rectangle_new (self->viewport_x, self->viewport_y,
                              self->viewport_width, self->viewport_height);
    origin = grl_vector2_new (0.0f, 0.0f);

    grl_draw_begin_blend_mode (mode);
    grl_draw_texture_pro (texture, source, dest, origin, 0.0f, tint);
    grl_draw_end_blend_mode ();
}

/**
 * lrg_lighting_manager_get_light_texture_id:
 * @self: an #LrgLightingManager
 *
 * Gets the GPU texture ID of the rendered light map.
 *
 * Returns: OpenGL texture ID, or 0 before the first light pass
 */
guint
lrg_lighting_manager_get_light_texture_id (LrgLightingManager *self)
{
    g_return_val_if_fail (LRG_IS_LIGHTING_MANAGER (self), 0);
    return self->light_texture_id;
}

/**
 * lrg_lighting_manager_get_light_target:
 * @self: an #LrgLightingManager
 *
 * Gets the render target the light pass draws into, for custom
 * compositing (a lighting shader, a post-process stage).
 *
 * Returns: (transfer none) (nullable): the light target, or %NULL
 *   before the first light pass with a GL context
 */
GrlRenderTexture *
lrg_lighting_manager_get_light_target (LrgLightingManager *self)
{
    g_return_val_if_fail (LRG_IS_LIGHTING_MANAGER (self), NULL);
    return self->light_target;
}

/**
 * lrg_lighting_manager_get_rendered_light_count:
 * @self: an #LrgLightingManager
 *
 * Gets how many lights the last lrg_lighting_manager_render() drew
 * (the lights that survived culling).
 *
 * Returns: the number of lights in the last light pass
 */
guint
lrg_lighting_manager_get_rendered_light_count (LrgLightingManager *self)
{
    g_return_val_if_fail (LRG_IS_LIGHTING_MANAGER (self), 0);
    return self->rendered_light_count;
}
