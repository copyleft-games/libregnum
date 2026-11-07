/* lrg-light2d.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Base 2D light class implementation.
 */

#include "config.h"

#include <math.h>

#include "lrg-light2d.h"
#include "lrg-point-light2d.h"
#include "lrg-spot-light2d.h"
#include "lrg-directional-light2d.h"
#include "lrg-shadow-caster.h"
#include "../lrg-log.h"

/* most shadow samples a soft light may use */
#define MAX_SHADOW_SAMPLES (32u)

typedef struct
{
    gfloat   x, y;
    guint8   color_r, color_g, color_b;
    gfloat   intensity;
    gboolean enabled;
    gboolean casts_shadows;
    LrgShadowMethod shadow_method;
    gfloat   shadow_softness;
    LrgLightFalloff falloff;
    LrgLightBlendMode blend_mode;
    gint     layer;

    /* 0.9.0: shadow geometry */
    guint      shadow_samples;   /* configured samples for soft shadows */
    GPtrArray *shadow_tris;      /* per sample: GArray of gfloat, 6 per triangle */
    GPtrArray *shadow_alpha;     /* per sample: GArray of gfloat, 1 per triangle */
} LrgLight2DPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (LrgLight2D, lrg_light2d, G_TYPE_OBJECT)

enum
{
    PROP_0,
    PROP_X,
    PROP_Y,
    PROP_INTENSITY,
    PROP_ENABLED,
    PROP_CASTS_SHADOWS,
    PROP_SHADOW_METHOD,
    PROP_SHADOW_SOFTNESS,
    PROP_FALLOFF,
    PROP_BLEND_MODE,
    PROP_LAYER,
    PROP_SHADOW_SAMPLES,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

static void
lrg_light2d_real_render (LrgLight2D *self,
                         guint       target_id,
                         guint       width,
                         guint       height)
{
    /* Base implementation does nothing - subclasses override */
    (void)self;
    (void)target_id;
    (void)width;
    (void)height;
}

static gboolean
lrg_light2d_real_is_visible (LrgLight2D *self,
                             gfloat      viewport_x,
                             gfloat      viewport_y,
                             gfloat      viewport_width,
                             gfloat      viewport_height)
{
    LrgLight2DPrivate *priv = lrg_light2d_get_instance_private (self);

    /* Default: always visible (subclasses should override with proper bounds check) */
    (void)viewport_x;
    (void)viewport_y;
    (void)viewport_width;
    (void)viewport_height;

    return priv->enabled;
}

static void
lrg_light2d_real_update (LrgLight2D *self,
                         gfloat      delta_time)
{
    /* Base implementation does nothing */
    (void)self;
    (void)delta_time;
}

/*
 * light_shape:
 * What the shadow projection needs to know about a light: whether it is
 * directional (parallel projection along @dir_x, @dir_y) and its reach
 * (point/spot radius, directional shadow length).
 */
static void
light_shape (LrgLight2D *self,
             gboolean   *directional,
             gfloat     *dir_x,
             gfloat     *dir_y,
             gfloat     *reach)
{
    *directional = FALSE;
    *dir_x = 0.0f;
    *dir_y = 0.0f;
    *reach = 1024.0f;

    if (LRG_IS_DIRECTIONAL_LIGHT2D (self))
    {
        gfloat angle = lrg_directional_light2d_get_direction (LRG_DIRECTIONAL_LIGHT2D (self)) *
                       (gfloat) G_PI / 180.0f;

        *directional = TRUE;
        *dir_x = cosf (angle);
        *dir_y = sinf (angle);
        *reach = lrg_directional_light2d_get_shadow_length (LRG_DIRECTIONAL_LIGHT2D (self));
    }
    else if (LRG_IS_POINT_LIGHT2D (self))
        *reach = lrg_point_light2d_get_radius (LRG_POINT_LIGHT2D (self));
    else if (LRG_IS_SPOT_LIGHT2D (self))
        *reach = lrg_spot_light2d_get_radius (LRG_SPOT_LIGHT2D (self));
}

/* distance from (px, py) to the segment a-b */
static gfloat
point_segment_distance (gfloat px,
                        gfloat py,
                        gfloat ax,
                        gfloat ay,
                        gfloat bx,
                        gfloat by)
{
    gfloat dx = bx - ax;
    gfloat dy = by - ay;
    gfloat len2 = dx * dx + dy * dy;
    gfloat t = 0.0f;

    if (len2 > 1e-12f)
        t = CLAMP (((px - ax) * dx + (py - ay) * dy) / len2, 0.0f, 1.0f);
    dx = ax + t * dx - px;
    dy = ay + t * dy - py;
    return sqrtf (dx * dx + dy * dy);
}

static void
push_triangle (GArray *tris,
               GArray *alpha,
               gfloat  x1,
               gfloat  y1,
               gfloat  x2,
               gfloat  y2,
               gfloat  x3,
               gfloat  y3,
               gfloat  opacity)
{
    gfloat t[6];

    t[0] = x1; t[1] = y1;
    t[2] = x2; t[3] = y2;
    t[4] = x3; t[5] = y3;
    g_array_append_vals (tris, t, 6);
    g_array_append_val (alpha, opacity);
}

/*
 * project_edge:
 * Adds the shadow quad (two triangles) of edge a-b for one light sample:
 * the edge and its end points pushed away from the light.  Point lights
 * push each end point along the ray from the light far enough to leave
 * the light's reach (the far edge is then off the lit area); directional
 * lights push both by the shadow length along the light direction.
 */
static void
project_edge (gboolean  directional,
              gfloat    lx,
              gfloat    ly,
              gfloat    dir_x,
              gfloat    dir_y,
              gfloat    reach,
              gfloat    ax,
              gfloat    ay,
              gfloat    bx,
              gfloat    by,
              gfloat    opacity,
              GArray   *tris,
              GArray   *alpha)
{
    gfloat fax;
    gfloat fay;
    gfloat fbx;
    gfloat fby;

    if (directional)
    {
        fax = ax + dir_x * reach;
        fay = ay + dir_y * reach;
        fbx = bx + dir_x * reach;
        fby = by + dir_y * reach;
    }
    else
    {
        gfloat da = hypotf (ax - lx, ay - ly);
        gfloat db = hypotf (bx - lx, by - ly);
        gfloat far;

        /* an end point on the light itself has no direction to push */
        if (da < 1e-3f || db < 1e-3f)
            return;
        far = reach * 2.0f + MAX (da, db);
        fax = lx + (ax - lx) * (far / da);
        fay = ly + (ay - ly) * (far / da);
        fbx = lx + (bx - lx) * (far / db);
        fby = ly + (by - ly) * (far / db);
    }

    push_triangle (tris, alpha, ax, ay, bx, by, fbx, fby, opacity);
    push_triangle (tris, alpha, ax, ay, fbx, fby, fax, fay, opacity);
}

static void
lrg_light2d_real_calculate_shadows (LrgLight2D *self,
                                    GPtrArray  *casters)
{
    LrgLight2DPrivate *priv = lrg_light2d_get_instance_private (self);
    g_autoptr(GPtrArray) edge_lists = NULL;
    g_autoptr(GArray) oriented_flags = NULL;
    g_autoptr(GArray) opacities = NULL;
    gboolean directional;
    gfloat dir_x;
    gfloat dir_y;
    gfloat reach;
    guint n_samples;
    guint s;
    guint c;

    g_ptr_array_set_size (priv->shadow_tris, 0);
    g_ptr_array_set_size (priv->shadow_alpha, 0);
    if (casters == NULL || casters->len == 0 || !priv->casts_shadows)
        return;

    light_shape (self, &directional, &dir_x, &dir_y, &reach);
    if (reach <= 0.0f)
        return;

    n_samples = priv->shadow_softness > 0.0f ? MAX (priv->shadow_samples, 1u) : 1u;

    /*
     * Gather every caster's edges once (get_edges allocates), with the
     * per-caster flags, then project them for each sample.
     */
    edge_lists = g_ptr_array_new_with_free_func ((GDestroyNotify) g_ptr_array_unref);
    oriented_flags = g_array_new (FALSE, FALSE, sizeof (gboolean));
    opacities = g_array_new (FALSE, FALSE, sizeof (gfloat));
    for (c = 0; c < casters->len; c++)
    {
        LrgShadowCaster *caster = g_ptr_array_index (casters, c);
        gboolean oriented = LRG_IS_SHADOW_OCCLUDER2D (caster) &&
                            !lrg_shadow_occluder2d_get_self_shadow (LRG_SHADOW_OCCLUDER2D (caster));
        gfloat opacity = CLAMP (lrg_shadow_caster_get_shadow_opacity (caster), 0.0f, 1.0f);
        GPtrArray *edges;

        if (opacity <= 0.0f)
            continue;
        edges = lrg_shadow_caster_get_edges (caster);
        if (edges == NULL)
            continue;
        g_ptr_array_add (edge_lists, edges);
        g_array_append_val (oriented_flags, oriented);
        g_array_append_val (opacities, opacity);
    }

    for (s = 0; s < n_samples; s++)
    {
        GArray *tris = g_array_new (FALSE, FALSE, sizeof (gfloat));
        GArray *alpha = g_array_new (FALSE, FALSE, sizeof (gfloat));
        gfloat lx = priv->x;
        gfloat ly = priv->y;
        gfloat sdx = dir_x;
        gfloat sdy = dir_y;

        /*
         * Sample placement.  Point lights: a Vogel spiral over a disc of
         * radius softness * 15% of the reach (an area light); directional
         * lights: directions spread evenly over +- softness * 8 degrees.
         */
        if (n_samples > 1)
        {
            if (directional)
            {
                gfloat spread = priv->shadow_softness * 8.0f * (gfloat) G_PI / 180.0f;
                gfloat t = ((gfloat) s + 0.5f) / (gfloat) n_samples * 2.0f - 1.0f;
                gfloat a = atan2f (dir_y, dir_x) + t * spread;

                sdx = cosf (a);
                sdy = sinf (a);
            }
            else
            {
                gfloat radius = priv->shadow_softness * 0.15f * reach *
                                sqrtf (((gfloat) s + 0.5f) / (gfloat) n_samples);
                gfloat a = (gfloat) s * 2.399963f;

                lx += cosf (a) * radius;
                ly += sinf (a) * radius;
            }
        }

        for (c = 0; c < edge_lists->len; c++)
        {
            GPtrArray *edges = g_ptr_array_index (edge_lists, c);
            gboolean oriented = g_array_index (oriented_flags, gboolean, c);
            gfloat opacity = g_array_index (opacities, gfloat, c);
            guint e;

            for (e = 0; e < edges->len; e++)
            {
                LrgShadowEdge *edge = g_ptr_array_index (edges, e);

                /* edges the light cannot reach cast nothing */
                if (!directional &&
                    point_segment_distance (lx, ly, edge->x1, edge->y1, edge->x2, edge->y2) > reach)
                    continue;

                /*
                 * Oriented edges (solid on the right, outward normal
                 * (dy, -dx)) only cast when they face away from the light,
                 * so shadows start behind the occluder and it stays lit.
                 */
                if (oriented)
                {
                    gfloat nx = edge->y2 - edge->y1;
                    gfloat ny = -(edge->x2 - edge->x1);
                    gfloat facing;

                    if (directional)
                        facing = nx * sdx + ny * sdy;
                    else
                        facing = nx * (edge->x1 - lx) + ny * (edge->y1 - ly);
                    if (facing <= 0.0f)
                        continue;
                }

                project_edge (directional, lx, ly, sdx, sdy, reach,
                              edge->x1, edge->y1, edge->x2, edge->y2, opacity, tris, alpha);
            }
        }

        g_ptr_array_add (priv->shadow_tris, tris);
        g_ptr_array_add (priv->shadow_alpha, alpha);
    }
}

static void
lrg_light2d_get_property (GObject    *object,
                          guint       prop_id,
                          GValue     *value,
                          GParamSpec *pspec)
{
    LrgLight2D *self = LRG_LIGHT2D (object);
    LrgLight2DPrivate *priv = lrg_light2d_get_instance_private (self);

    switch (prop_id)
    {
    case PROP_X:
        g_value_set_float (value, priv->x);
        break;
    case PROP_Y:
        g_value_set_float (value, priv->y);
        break;
    case PROP_INTENSITY:
        g_value_set_float (value, priv->intensity);
        break;
    case PROP_ENABLED:
        g_value_set_boolean (value, priv->enabled);
        break;
    case PROP_CASTS_SHADOWS:
        g_value_set_boolean (value, priv->casts_shadows);
        break;
    case PROP_SHADOW_METHOD:
        g_value_set_enum (value, priv->shadow_method);
        break;
    case PROP_SHADOW_SOFTNESS:
        g_value_set_float (value, priv->shadow_softness);
        break;
    case PROP_FALLOFF:
        g_value_set_enum (value, priv->falloff);
        break;
    case PROP_BLEND_MODE:
        g_value_set_enum (value, priv->blend_mode);
        break;
    case PROP_LAYER:
        g_value_set_int (value, priv->layer);
        break;
    case PROP_SHADOW_SAMPLES:
        g_value_set_uint (value, priv->shadow_samples);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_light2d_set_property (GObject      *object,
                          guint         prop_id,
                          const GValue *value,
                          GParamSpec   *pspec)
{
    LrgLight2D *self = LRG_LIGHT2D (object);
    LrgLight2DPrivate *priv = lrg_light2d_get_instance_private (self);

    switch (prop_id)
    {
    case PROP_X:
        priv->x = g_value_get_float (value);
        break;
    case PROP_Y:
        priv->y = g_value_get_float (value);
        break;
    case PROP_INTENSITY:
        priv->intensity = g_value_get_float (value);
        break;
    case PROP_ENABLED:
        priv->enabled = g_value_get_boolean (value);
        break;
    case PROP_CASTS_SHADOWS:
        priv->casts_shadows = g_value_get_boolean (value);
        break;
    case PROP_SHADOW_METHOD:
        priv->shadow_method = g_value_get_enum (value);
        break;
    case PROP_SHADOW_SOFTNESS:
        priv->shadow_softness = CLAMP (g_value_get_float (value), 0.0f, 1.0f);
        break;
    case PROP_FALLOFF:
        priv->falloff = g_value_get_enum (value);
        break;
    case PROP_BLEND_MODE:
        priv->blend_mode = g_value_get_enum (value);
        break;
    case PROP_LAYER:
        priv->layer = g_value_get_int (value);
        break;
    case PROP_SHADOW_SAMPLES:
        lrg_light2d_set_shadow_samples (LRG_LIGHT2D (object), g_value_get_uint (value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_light2d_finalize (GObject *object)
{
    LrgLight2DPrivate *priv = lrg_light2d_get_instance_private (LRG_LIGHT2D (object));

    g_clear_pointer (&priv->shadow_tris, g_ptr_array_unref);
    g_clear_pointer (&priv->shadow_alpha, g_ptr_array_unref);

    G_OBJECT_CLASS (lrg_light2d_parent_class)->finalize (object);
}

static void
lrg_light2d_class_init (LrgLight2DClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = lrg_light2d_finalize;
    object_class->get_property = lrg_light2d_get_property;
    object_class->set_property = lrg_light2d_set_property;

    klass->render = lrg_light2d_real_render;
    klass->is_visible = lrg_light2d_real_is_visible;
    klass->update = lrg_light2d_real_update;
    klass->calculate_shadows = lrg_light2d_real_calculate_shadows;

    properties[PROP_X] =
        g_param_spec_float ("x", "X", "X position",
                            -G_MAXFLOAT, G_MAXFLOAT, 0.0f,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_Y] =
        g_param_spec_float ("y", "Y", "Y position",
                            -G_MAXFLOAT, G_MAXFLOAT, 0.0f,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_INTENSITY] =
        g_param_spec_float ("intensity", "Intensity", "Light intensity",
                            0.0f, G_MAXFLOAT, 1.0f,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_ENABLED] =
        g_param_spec_boolean ("enabled", "Enabled", "Whether light is enabled",
                              TRUE,
                              G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_CASTS_SHADOWS] =
        g_param_spec_boolean ("casts-shadows", "Casts Shadows", "Whether light casts shadows",
                              TRUE,
                              G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_SHADOW_METHOD] =
        g_param_spec_enum ("shadow-method", "Shadow Method", "Shadow calculation method",
                           LRG_TYPE_SHADOW_METHOD, LRG_SHADOW_METHOD_GEOMETRY,
                           G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_SHADOW_SOFTNESS] =
        g_param_spec_float ("shadow-softness", "Shadow Softness", "Shadow edge softness",
                            0.0f, 1.0f, 0.0f,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_FALLOFF] =
        g_param_spec_enum ("falloff", "Falloff", "Light falloff type",
                           LRG_TYPE_LIGHT_FALLOFF, LRG_LIGHT_FALLOFF_QUADRATIC,
                           G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_BLEND_MODE] =
        g_param_spec_enum ("blend-mode", "Blend Mode", "Light blending mode",
                           LRG_TYPE_LIGHT_BLEND_MODE, LRG_LIGHT_BLEND_MULTIPLY,
                           G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_LAYER] =
        g_param_spec_int ("layer", "Layer", "Render layer",
                          G_MININT, G_MAXINT, 0,
                          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgLight2D:shadow-samples:
     *
     * Samples used for soft shadows (#LrgLight2D:shadow-softness above
     * 0): the shadow geometry is built this many times from points spread
     * over the light, and the light pass draws the light once per sample.
     */
    properties[PROP_SHADOW_SAMPLES] =
        g_param_spec_uint ("shadow-samples", NULL, NULL, 1, MAX_SHADOW_SAMPLES, 8,
                           G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_light2d_init (LrgLight2D *self)
{
    LrgLight2DPrivate *priv = lrg_light2d_get_instance_private (self);

    priv->x = 0.0f;
    priv->y = 0.0f;
    priv->color_r = 255;
    priv->color_g = 255;
    priv->color_b = 255;
    priv->intensity = 1.0f;
    priv->enabled = TRUE;
    priv->casts_shadows = TRUE;
    priv->shadow_method = LRG_SHADOW_METHOD_GEOMETRY;
    priv->shadow_softness = 0.0f;
    priv->falloff = LRG_LIGHT_FALLOFF_QUADRATIC;
    priv->blend_mode = LRG_LIGHT_BLEND_MULTIPLY;
    priv->layer = 0;
    priv->shadow_samples = 8;
    priv->shadow_tris = g_ptr_array_new_with_free_func ((GDestroyNotify) g_array_unref);
    priv->shadow_alpha = g_ptr_array_new_with_free_func ((GDestroyNotify) g_array_unref);
}

/* Public API */

void
lrg_light2d_get_position (LrgLight2D *self,
                          gfloat     *x,
                          gfloat     *y)
{
    LrgLight2DPrivate *priv;

    g_return_if_fail (LRG_IS_LIGHT2D (self));

    priv = lrg_light2d_get_instance_private (self);

    if (x) *x = priv->x;
    if (y) *y = priv->y;
}

void
lrg_light2d_set_position (LrgLight2D *self,
                          gfloat      x,
                          gfloat      y)
{
    LrgLight2DPrivate *priv;

    g_return_if_fail (LRG_IS_LIGHT2D (self));

    priv = lrg_light2d_get_instance_private (self);
    priv->x = x;
    priv->y = y;

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_X]);
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_Y]);
}

void
lrg_light2d_get_color (LrgLight2D *self,
                       guint8     *r,
                       guint8     *g,
                       guint8     *b)
{
    LrgLight2DPrivate *priv;

    g_return_if_fail (LRG_IS_LIGHT2D (self));

    priv = lrg_light2d_get_instance_private (self);

    if (r) *r = priv->color_r;
    if (g) *g = priv->color_g;
    if (b) *b = priv->color_b;
}

void
lrg_light2d_set_color (LrgLight2D *self,
                       guint8      r,
                       guint8      g,
                       guint8      b)
{
    LrgLight2DPrivate *priv;

    g_return_if_fail (LRG_IS_LIGHT2D (self));

    priv = lrg_light2d_get_instance_private (self);
    priv->color_r = r;
    priv->color_g = g;
    priv->color_b = b;
}

gfloat
lrg_light2d_get_intensity (LrgLight2D *self)
{
    LrgLight2DPrivate *priv;

    g_return_val_if_fail (LRG_IS_LIGHT2D (self), 0.0f);

    priv = lrg_light2d_get_instance_private (self);
    return priv->intensity;
}

void
lrg_light2d_set_intensity (LrgLight2D *self,
                           gfloat      intensity)
{
    LrgLight2DPrivate *priv;

    g_return_if_fail (LRG_IS_LIGHT2D (self));

    priv = lrg_light2d_get_instance_private (self);
    priv->intensity = intensity;

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_INTENSITY]);
}

gboolean
lrg_light2d_get_enabled (LrgLight2D *self)
{
    LrgLight2DPrivate *priv;

    g_return_val_if_fail (LRG_IS_LIGHT2D (self), FALSE);

    priv = lrg_light2d_get_instance_private (self);
    return priv->enabled;
}

void
lrg_light2d_set_enabled (LrgLight2D *self,
                         gboolean    enabled)
{
    LrgLight2DPrivate *priv;

    g_return_if_fail (LRG_IS_LIGHT2D (self));

    priv = lrg_light2d_get_instance_private (self);
    priv->enabled = enabled;

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ENABLED]);
}

gboolean
lrg_light2d_get_casts_shadows (LrgLight2D *self)
{
    LrgLight2DPrivate *priv;

    g_return_val_if_fail (LRG_IS_LIGHT2D (self), FALSE);

    priv = lrg_light2d_get_instance_private (self);
    return priv->casts_shadows;
}

void
lrg_light2d_set_casts_shadows (LrgLight2D *self,
                               gboolean    casts_shadows)
{
    LrgLight2DPrivate *priv;

    g_return_if_fail (LRG_IS_LIGHT2D (self));

    priv = lrg_light2d_get_instance_private (self);
    priv->casts_shadows = casts_shadows;

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_CASTS_SHADOWS]);
}

LrgShadowMethod
lrg_light2d_get_shadow_method (LrgLight2D *self)
{
    LrgLight2DPrivate *priv;

    g_return_val_if_fail (LRG_IS_LIGHT2D (self), LRG_SHADOW_METHOD_GEOMETRY);

    priv = lrg_light2d_get_instance_private (self);
    return priv->shadow_method;
}

void
lrg_light2d_set_shadow_method (LrgLight2D      *self,
                               LrgShadowMethod  method)
{
    LrgLight2DPrivate *priv;

    g_return_if_fail (LRG_IS_LIGHT2D (self));

    priv = lrg_light2d_get_instance_private (self);
    priv->shadow_method = method;

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SHADOW_METHOD]);
}

gfloat
lrg_light2d_get_shadow_softness (LrgLight2D *self)
{
    LrgLight2DPrivate *priv;

    g_return_val_if_fail (LRG_IS_LIGHT2D (self), 0.0f);

    priv = lrg_light2d_get_instance_private (self);
    return priv->shadow_softness;
}

void
lrg_light2d_set_shadow_softness (LrgLight2D *self,
                                 gfloat      softness)
{
    LrgLight2DPrivate *priv;

    g_return_if_fail (LRG_IS_LIGHT2D (self));

    priv = lrg_light2d_get_instance_private (self);
    priv->shadow_softness = CLAMP (softness, 0.0f, 1.0f);

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SHADOW_SOFTNESS]);
}

LrgLightFalloff
lrg_light2d_get_falloff (LrgLight2D *self)
{
    LrgLight2DPrivate *priv;

    g_return_val_if_fail (LRG_IS_LIGHT2D (self), LRG_LIGHT_FALLOFF_QUADRATIC);

    priv = lrg_light2d_get_instance_private (self);
    return priv->falloff;
}

void
lrg_light2d_set_falloff (LrgLight2D      *self,
                         LrgLightFalloff  falloff)
{
    LrgLight2DPrivate *priv;

    g_return_if_fail (LRG_IS_LIGHT2D (self));

    priv = lrg_light2d_get_instance_private (self);
    priv->falloff = falloff;

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_FALLOFF]);
}

LrgLightBlendMode
lrg_light2d_get_blend_mode (LrgLight2D *self)
{
    LrgLight2DPrivate *priv;

    g_return_val_if_fail (LRG_IS_LIGHT2D (self), LRG_LIGHT_BLEND_MULTIPLY);

    priv = lrg_light2d_get_instance_private (self);
    return priv->blend_mode;
}

void
lrg_light2d_set_blend_mode (LrgLight2D        *self,
                            LrgLightBlendMode  mode)
{
    LrgLight2DPrivate *priv;

    g_return_if_fail (LRG_IS_LIGHT2D (self));

    priv = lrg_light2d_get_instance_private (self);
    priv->blend_mode = mode;

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_BLEND_MODE]);
}

gint
lrg_light2d_get_layer (LrgLight2D *self)
{
    LrgLight2DPrivate *priv;

    g_return_val_if_fail (LRG_IS_LIGHT2D (self), 0);

    priv = lrg_light2d_get_instance_private (self);
    return priv->layer;
}

void
lrg_light2d_set_layer (LrgLight2D *self,
                       gint        layer)
{
    LrgLight2DPrivate *priv;

    g_return_if_fail (LRG_IS_LIGHT2D (self));

    priv = lrg_light2d_get_instance_private (self);
    priv->layer = layer;

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_LAYER]);
}

/* Virtual method wrappers */

void
lrg_light2d_render (LrgLight2D *self,
                    guint       target_id,
                    guint       width,
                    guint       height)
{
    g_return_if_fail (LRG_IS_LIGHT2D (self));
    LRG_LIGHT2D_GET_CLASS (self)->render (self, target_id, width, height);
}

gboolean
lrg_light2d_is_visible (LrgLight2D *self,
                        gfloat      viewport_x,
                        gfloat      viewport_y,
                        gfloat      viewport_width,
                        gfloat      viewport_height)
{
    g_return_val_if_fail (LRG_IS_LIGHT2D (self), FALSE);
    return LRG_LIGHT2D_GET_CLASS (self)->is_visible (self, viewport_x, viewport_y,
                                                     viewport_width, viewport_height);
}

void
lrg_light2d_update (LrgLight2D *self,
                    gfloat      delta_time)
{
    g_return_if_fail (LRG_IS_LIGHT2D (self));
    LRG_LIGHT2D_GET_CLASS (self)->update (self, delta_time);
}

void
lrg_light2d_calculate_shadows (LrgLight2D *self,
                               GPtrArray  *casters)
{
    g_return_if_fail (LRG_IS_LIGHT2D (self));
    LRG_LIGHT2D_GET_CLASS (self)->calculate_shadows (self, casters);
}

/* ==========================================================================
 * Shadow geometry access (0.9.0)
 * ========================================================================== */

guint
lrg_light2d_get_shadow_samples (LrgLight2D *self)
{
    LrgLight2DPrivate *priv;

    g_return_val_if_fail (LRG_IS_LIGHT2D (self), 1);
    priv = lrg_light2d_get_instance_private (self);
    return priv->shadow_samples;
}

void
lrg_light2d_set_shadow_samples (LrgLight2D *self,
                                guint       samples)
{
    LrgLight2DPrivate *priv;

    g_return_if_fail (LRG_IS_LIGHT2D (self));

    priv = lrg_light2d_get_instance_private (self);
    samples = CLAMP (samples, 1u, MAX_SHADOW_SAMPLES);
    if (priv->shadow_samples == samples)
        return;
    priv->shadow_samples = samples;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SHADOW_SAMPLES]);
}

guint
lrg_light2d_get_shadow_sample_count (LrgLight2D *self)
{
    LrgLight2DPrivate *priv;

    g_return_val_if_fail (LRG_IS_LIGHT2D (self), 0);
    priv = lrg_light2d_get_instance_private (self);
    return priv->shadow_tris->len;
}

const gfloat *
lrg_light2d_get_shadow_triangles (LrgLight2D *self,
                                  guint       sample,
                                  guint      *n_coords)
{
    LrgLight2DPrivate *priv;
    GArray *tris;

    g_return_val_if_fail (LRG_IS_LIGHT2D (self), NULL);
    g_return_val_if_fail (n_coords != NULL, NULL);

    priv = lrg_light2d_get_instance_private (self);
    *n_coords = 0;
    if (sample >= priv->shadow_tris->len)
        return NULL;
    tris = g_ptr_array_index (priv->shadow_tris, sample);
    *n_coords = tris->len;
    return (const gfloat *) tris->data;
}

const gfloat *
lrg_light2d_get_shadow_opacities (LrgLight2D *self,
                                  guint       sample,
                                  guint      *n_triangles)
{
    LrgLight2DPrivate *priv;
    GArray *alpha;

    g_return_val_if_fail (LRG_IS_LIGHT2D (self), NULL);
    g_return_val_if_fail (n_triangles != NULL, NULL);

    priv = lrg_light2d_get_instance_private (self);
    *n_triangles = 0;
    if (sample >= priv->shadow_alpha->len)
        return NULL;
    alpha = g_ptr_array_index (priv->shadow_alpha, sample);
    *n_triangles = alpha->len;
    return (const gfloat *) alpha->data;
}

/* point in triangle, edges included, either winding */
static gboolean
point_in_triangle (const gfloat *t,
                   gfloat        px,
                   gfloat        py)
{
    gfloat d1 = (px - t[2]) * (t[1] - t[3]) - (t[0] - t[2]) * (py - t[3]);
    gfloat d2 = (px - t[4]) * (t[3] - t[5]) - (t[2] - t[4]) * (py - t[5]);
    gfloat d3 = (px - t[0]) * (t[5] - t[1]) - (t[4] - t[0]) * (py - t[1]);
    gboolean neg = (d1 < 0.0f) || (d2 < 0.0f) || (d3 < 0.0f);
    gboolean pos = (d1 > 0.0f) || (d2 > 0.0f) || (d3 > 0.0f);

    return !(neg && pos);
}

gfloat
lrg_light2d_get_shadow_factor (LrgLight2D *self,
                               gfloat      x,
                               gfloat      y)
{
    LrgLight2DPrivate *priv;
    gfloat total = 0.0f;
    guint s;

    g_return_val_if_fail (LRG_IS_LIGHT2D (self), 0.0f);

    priv = lrg_light2d_get_instance_private (self);
    if (priv->shadow_tris->len == 0)
        return 0.0f;

    for (s = 0; s < priv->shadow_tris->len; s++)
    {
        GArray *tris = g_ptr_array_index (priv->shadow_tris, s);
        GArray *alpha = g_ptr_array_index (priv->shadow_alpha, s);
        gfloat strongest = 0.0f;
        guint t;

        for (t = 0; t < alpha->len; t++)
        {
            gfloat a = g_array_index (alpha, gfloat, t);

            if (a > strongest && point_in_triangle (&g_array_index (tris, gfloat, t * 6), x, y))
                strongest = a;
        }
        total += strongest;
    }
    return total / (gfloat) priv->shadow_tris->len;
}

void
lrg_light2d_clear_shadows (LrgLight2D *self)
{
    LrgLight2DPrivate *priv;

    g_return_if_fail (LRG_IS_LIGHT2D (self));
    priv = lrg_light2d_get_instance_private (self);
    g_ptr_array_set_size (priv->shadow_tris, 0);
    g_ptr_array_set_size (priv->shadow_alpha, 0);
}
