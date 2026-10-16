/* lrg-light-set.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Forward lighting for static geometry. Point lights are plain records
 * in a slot array (removed slots are reused); select() keeps the best N
 * by insertion into a fixed array, which beats sorting for N <= 16. The
 * shader uses raylib's default attribute and matrix names, so DrawMesh()
 * (LrgStaticBatch, LrgTerrain) feeds it model and normal matrices
 * without any extra work; lighting happens in the space the model
 * matrix maps to (origin-relative with a floating origin). Fog is a
 * blend toward one colour by distance from the selected camera; the
 * GLSL and lrg_fog_amount() share one model (lrg-light-set-private.h).
 */

#include "config.h"

#include <math.h>
#include <string.h>
#include <raylib.h>
#include <rlgl.h>

#ifndef LIBREGNUM_COMPILATION
#define LIBREGNUM_COMPILATION
#endif
#include "graphics/lrg-light-set.h"
#include "graphics/lrg-light-set-private.h"

typedef struct
{
    gboolean used;
    gboolean enabled;
    gdouble  position[3];   /* world */
    gfloat   color[3];      /* 0 .. 1 */
    gfloat   radius;
    gfloat   intensity;
} PointLight;

struct _LrgLightSet
{
    GObject     parent_instance;

    gfloat      ambient[3];
    gboolean    has_directional;
    gfloat      directional_dir[3];
    gfloat      directional_color[3];
    gdouble     origin[3];

    GArray     *lights;          /* PointLight, slot = id */
    guint       n_lights;        /* used slots */
    guint       max_active;
    gfloat      max_distance;
    guint       active[LRG_LIGHT_SET_MAX_ACTIVE];
    guint       n_active;
    gfloat      cutoff;
    gdouble     camera[3];       /* world, last select() */

    gboolean    fog_on;
    LrgFogType  fog_type;
    gfloat      fog_color[4];    /* 0 .. 1, alpha caps the amount */
    gfloat      fog_density;
    gfloat      fog_start;
    gfloat      fog_end;
    gfloat      fog_base;        /* world height */
    gfloat      fog_falloff;

    GrlShader  *shader;
    gboolean    shader_tried;
    gint        loc_ambient;
    gint        loc_sun_dir;
    gint        loc_sun_color;
    gint        loc_count;
    gint        loc_positions;
    gint        loc_colors;
    gint        loc_radii;
    gint        loc_cutoff;
    LrgLightSetFogLocations fog_locs;
};

G_DEFINE_TYPE (LrgLightSet, lrg_light_set, G_TYPE_OBJECT)

/* The bound light set (lrg_light_set_bind()); drawing is single
 * threaded, like raylib's own current-shader state. */
static LrgLightSet *bound_light_set = NULL;

/* GLSL 330 with raylib's default attribute and uniform names. */
static const gchar light_vs[] =
    "#version 330\n"
    "in vec3 vertexPosition;\n"
    "in vec2 vertexTexCoord;\n"
    "in vec3 vertexNormal;\n"
    "in vec4 vertexColor;\n"
    "uniform mat4 mvp;\n"
    "uniform mat4 matModel;\n"
    "uniform mat4 matNormal;\n"
    "out vec3 fragPosition;\n"
    "out vec2 fragTexCoord;\n"
    "out vec4 fragColor;\n"
    "out vec3 fragNormal;\n"
    "void main() {\n"
    "    fragPosition = vec3(matModel * vec4(vertexPosition, 1.0));\n"
    "    fragTexCoord = vertexTexCoord;\n"
    "    fragColor = vertexColor;\n"
    "    fragNormal = vec3(matNormal * vec4(vertexNormal, 0.0));\n"
    "    gl_Position = mvp * vec4(vertexPosition, 1.0);\n"
    "}\n";

static const gchar light_fs[] =
    "#version 330\n"
    "#define MAX_LIGHTS 16\n"
    "in vec3 fragPosition;\n"
    "in vec2 fragTexCoord;\n"
    "in vec4 fragColor;\n"
    "in vec3 fragNormal;\n"
    "uniform sampler2D texture0;\n"
    "uniform vec4 colDiffuse;\n"
    "uniform vec3 ambientColor;\n"
    "uniform vec3 sunDirection;\n"
    "uniform vec3 sunColor;\n"
    "uniform int lightCount;\n"
    "uniform vec3 lightPosition[MAX_LIGHTS];\n"
    "uniform vec3 lightColor[MAX_LIGHTS];\n"
    "uniform float lightRadius[MAX_LIGHTS];\n"
    "uniform float alphaCutoff;\n"
    LRG_FOG_GLSL
    "out vec4 finalColor;\n"
    "void main() {\n"
    "    vec4 texel = texture(texture0, fragTexCoord) * colDiffuse * fragColor;\n"
    "    if (texel.a < alphaCutoff) discard;\n"
    "    float nlen = length(fragNormal);\n"
    "    vec3 n = nlen > 0.0 ? fragNormal / nlen : vec3(0.0, 1.0, 0.0);\n"
    "    vec3 light = ambientColor + sunColor * max(dot(n, -sunDirection), 0.0);\n"
    "    for (int i = 0; i < MAX_LIGHTS; i++) {\n"
    "        if (i >= lightCount) break;\n"
    "        vec3 d = lightPosition[i] - fragPosition;\n"
    "        float dist = length(d);\n"
    "        float k = clamp(1.0 - dist / lightRadius[i], 0.0, 1.0);\n"
    "        float lambert = dist > 0.0001 ? max(dot(n, d / dist), 0.0) : 1.0;\n"
    "        light += lightColor[i] * (k * k * lambert);\n"
    "    }\n"
    "    vec3 lit = texel.rgb * light;\n"
    "    finalColor = vec4(mix(lit, fogColor.rgb, lrg_fog_amount(fragPosition)), texel.a);\n"
    "}\n";

/* ------------------------------------------------------------------------ */
/* Lifecycle                                                                */
/* ------------------------------------------------------------------------ */

static void
lrg_light_set_finalize (GObject *object)
{
    LrgLightSet *self = LRG_LIGHT_SET (object);

    g_clear_object (&self->shader);
    g_clear_pointer (&self->lights, g_array_unref);

    G_OBJECT_CLASS (lrg_light_set_parent_class)->finalize (object);
}

static void
lrg_light_set_class_init (LrgLightSetClass *klass)
{
    G_OBJECT_CLASS (klass)->finalize = lrg_light_set_finalize;
}

static void
lrg_light_set_init (LrgLightSet *self)
{
    self->ambient[0] = self->ambient[1] = self->ambient[2] = 64.0f / 255.0f;
    self->lights = g_array_new (FALSE, TRUE, sizeof (PointLight));
    self->max_active = LRG_LIGHT_SET_MAX_ACTIVE;
    self->fog_type = LRG_FOG_TYPE_LINEAR;
    self->fog_falloff = 1.0f;
}

LrgLightSet *
lrg_light_set_new (void)
{
    return g_object_new (LRG_TYPE_LIGHT_SET, NULL);
}

static void
color_to_rgb (const GrlColor *color,
              gfloat         *out)
{
    out[0] = (gfloat)color->r / 255.0f;
    out[1] = (gfloat)color->g / 255.0f;
    out[2] = (gfloat)color->b / 255.0f;
}

/* ------------------------------------------------------------------------ */
/* Global lights                                                            */
/* ------------------------------------------------------------------------ */

void
lrg_light_set_set_ambient (LrgLightSet    *self,
                           const GrlColor *color)
{
    g_return_if_fail (LRG_IS_LIGHT_SET (self));
    g_return_if_fail (color != NULL);

    color_to_rgb (color, self->ambient);
}

void
lrg_light_set_set_directional (LrgLightSet      *self,
                               const GrlVector3 *direction,
                               const GrlColor   *color)
{
    gfloat len;

    g_return_if_fail (LRG_IS_LIGHT_SET (self));

    self->has_directional = FALSE;
    if (direction == NULL || color == NULL)
        return;

    len = sqrtf (direction->x * direction->x + direction->y * direction->y
                 + direction->z * direction->z);
    if (len <= 0.0f || !isfinite (len))
        return;

    self->directional_dir[0] = direction->x / len;
    self->directional_dir[1] = direction->y / len;
    self->directional_dir[2] = direction->z / len;
    color_to_rgb (color, self->directional_color);
    self->has_directional = TRUE;
}

void
lrg_light_set_set_origin (LrgLightSet *self,
                          gdouble      x,
                          gdouble      y,
                          gdouble      z)
{
    g_return_if_fail (LRG_IS_LIGHT_SET (self));

    self->origin[0] = x;
    self->origin[1] = y;
    self->origin[2] = z;
}

/* ------------------------------------------------------------------------ */
/* Point lights                                                             */
/* ------------------------------------------------------------------------ */

static PointLight *
light_at (LrgLightSet *self,
          guint        light)
{
    PointLight *pl;

    if (light >= self->lights->len)
        return NULL;
    pl = &g_array_index (self->lights, PointLight, light);
    return pl->used ? pl : NULL;
}

guint
lrg_light_set_add_point_light (LrgLightSet    *self,
                               gdouble         x,
                               gdouble         y,
                               gdouble         z,
                               const GrlColor *color,
                               gfloat          radius)
{
    PointLight *pl = NULL;
    guint       id;

    g_return_val_if_fail (LRG_IS_LIGHT_SET (self), LRG_LIGHT_SET_INVALID_LIGHT);
    g_return_val_if_fail (color != NULL, LRG_LIGHT_SET_INVALID_LIGHT);
    g_return_val_if_fail (radius > 0.0f, LRG_LIGHT_SET_INVALID_LIGHT);

    /* Reuse the first free slot, else append. */
    for (id = 0; id < self->lights->len; id++)
    {
        if (!g_array_index (self->lights, PointLight, id).used)
        {
            pl = &g_array_index (self->lights, PointLight, id);
            break;
        }
    }
    if (pl == NULL)
    {
        g_array_set_size (self->lights, self->lights->len + 1);
        id = self->lights->len - 1;
        pl = &g_array_index (self->lights, PointLight, id);
    }

    memset (pl, 0, sizeof *pl);
    pl->used = TRUE;
    pl->enabled = TRUE;
    pl->position[0] = x;
    pl->position[1] = y;
    pl->position[2] = z;
    color_to_rgb (color, pl->color);
    pl->radius = radius;
    pl->intensity = 1.0f;
    self->n_lights++;
    return id;
}

gboolean
lrg_light_set_remove_point_light (LrgLightSet *self,
                                  guint        light)
{
    PointLight *pl;

    g_return_val_if_fail (LRG_IS_LIGHT_SET (self), FALSE);

    pl = light_at (self, light);
    if (pl == NULL)
        return FALSE;
    pl->used = FALSE;
    self->n_lights--;
    /* The last selection may name it; drop the selection. */
    self->n_active = 0;
    return TRUE;
}

void
lrg_light_set_clear_point_lights (LrgLightSet *self)
{
    g_return_if_fail (LRG_IS_LIGHT_SET (self));

    g_array_set_size (self->lights, 0);
    self->n_lights = 0;
    self->n_active = 0;
}

guint
lrg_light_set_get_point_light_count (LrgLightSet *self)
{
    g_return_val_if_fail (LRG_IS_LIGHT_SET (self), 0);
    return self->n_lights;
}

void
lrg_light_set_set_point_light_position (LrgLightSet *self,
                                        guint        light,
                                        gdouble      x,
                                        gdouble      y,
                                        gdouble      z)
{
    PointLight *pl;

    g_return_if_fail (LRG_IS_LIGHT_SET (self));

    pl = light_at (self, light);
    g_return_if_fail (pl != NULL);
    pl->position[0] = x;
    pl->position[1] = y;
    pl->position[2] = z;
}

void
lrg_light_set_set_point_light_color (LrgLightSet    *self,
                                     guint           light,
                                     const GrlColor *color)
{
    PointLight *pl;

    g_return_if_fail (LRG_IS_LIGHT_SET (self));
    g_return_if_fail (color != NULL);

    pl = light_at (self, light);
    g_return_if_fail (pl != NULL);
    color_to_rgb (color, pl->color);
}

void
lrg_light_set_set_point_light_radius (LrgLightSet *self,
                                      guint        light,
                                      gfloat       radius)
{
    PointLight *pl;

    g_return_if_fail (LRG_IS_LIGHT_SET (self));
    g_return_if_fail (radius > 0.0f);

    pl = light_at (self, light);
    g_return_if_fail (pl != NULL);
    pl->radius = radius;
}

void
lrg_light_set_set_point_light_intensity (LrgLightSet *self,
                                         guint        light,
                                         gfloat       intensity)
{
    PointLight *pl;

    g_return_if_fail (LRG_IS_LIGHT_SET (self));
    g_return_if_fail (intensity >= 0.0f);

    pl = light_at (self, light);
    g_return_if_fail (pl != NULL);
    pl->intensity = intensity;
}

void
lrg_light_set_set_point_light_enabled (LrgLightSet *self,
                                       guint        light,
                                       gboolean     enabled)
{
    PointLight *pl;

    g_return_if_fail (LRG_IS_LIGHT_SET (self));

    pl = light_at (self, light);
    g_return_if_fail (pl != NULL);
    pl->enabled = enabled;
}

void
lrg_light_set_set_max_active (LrgLightSet *self,
                              guint        count)
{
    g_return_if_fail (LRG_IS_LIGHT_SET (self));
    g_return_if_fail (count <= LRG_LIGHT_SET_MAX_ACTIVE);

    self->max_active = count;
}

void
lrg_light_set_set_max_distance (LrgLightSet *self,
                                gfloat       distance)
{
    g_return_if_fail (LRG_IS_LIGHT_SET (self));
    g_return_if_fail (distance >= 0.0f);

    self->max_distance = distance;
}

/* ------------------------------------------------------------------------ */
/* Selection                                                                */
/* ------------------------------------------------------------------------ */

guint
lrg_light_set_select (LrgLightSet *self,
                      gdouble      camera_x,
                      gdouble      camera_y,
                      gdouble      camera_z)
{
    gdouble keys[LRG_LIGHT_SET_MAX_ACTIVE];
    guint   id;

    g_return_val_if_fail (LRG_IS_LIGHT_SET (self), 0);

    /* The fog measures distances from this camera too. */
    self->camera[0] = camera_x;
    self->camera[1] = camera_y;
    self->camera[2] = camera_z;

    self->n_active = 0;
    if (self->max_active == 0)
        return 0;

    for (id = 0; id < self->lights->len; id++)
    {
        PointLight *pl = &g_array_index (self->lights, PointLight, id);
        gdouble     dx;
        gdouble     dy;
        gdouble     dz;
        gdouble     key;
        guint       slot;

        if (!pl->used || !pl->enabled || pl->intensity <= 0.0f)
            continue;

        dx = pl->position[0] - camera_x;
        dy = pl->position[1] - camera_y;
        dz = pl->position[2] - camera_z;
        key = sqrt (dx * dx + dy * dy + dz * dz) - (gdouble)pl->radius;
        if (self->max_distance > 0.0f && key > (gdouble)self->max_distance)
            continue;

        /* Insertion into the sorted best-N; ids arrive ascending, so a
         * strict comparison keeps the lower id first on ties. */
        if (self->n_active == self->max_active && key >= keys[self->n_active - 1])
            continue;
        slot = self->n_active < self->max_active ? self->n_active : self->n_active - 1;
        while (slot > 0 && keys[slot - 1] > key)
        {
            keys[slot] = keys[slot - 1];
            self->active[slot] = self->active[slot - 1];
            slot--;
        }
        keys[slot] = key;
        self->active[slot] = id;
        if (self->n_active < self->max_active)
            self->n_active++;
    }
    return self->n_active;
}

guint
lrg_light_set_get_active_count (LrgLightSet *self)
{
    g_return_val_if_fail (LRG_IS_LIGHT_SET (self), 0);
    return self->n_active;
}

guint
lrg_light_set_get_active_light (LrgLightSet *self,
                                guint        index)
{
    g_return_val_if_fail (LRG_IS_LIGHT_SET (self), LRG_LIGHT_SET_INVALID_LIGHT);

    if (index >= self->n_active)
        return LRG_LIGHT_SET_INVALID_LIGHT;
    return self->active[index];
}

void
lrg_light_set_set_alpha_cutoff (LrgLightSet *self,
                                gfloat       cutoff)
{
    g_return_if_fail (LRG_IS_LIGHT_SET (self));
    self->cutoff = cutoff;
}

/* ------------------------------------------------------------------------ */
/* Shader                                                                   */
/* ------------------------------------------------------------------------ */

GrlShader *
lrg_light_set_get_shader (LrgLightSet *self)
{
    g_autoptr(GError) error = NULL;
    Shader *handle;

    g_return_val_if_fail (LRG_IS_LIGHT_SET (self), NULL);

    if (self->shader != NULL || self->shader_tried)
        return self->shader;
    if (!IsWindowReady ())
        return NULL;
    self->shader_tried = TRUE;

    /* On a compile failure raylib hands back its default shader. */
    self->shader = grl_shader_new_from_memory (light_vs, light_fs, &error);
    if (self->shader == NULL)
        return NULL;
    handle = grl_shader_get_handle (self->shader);
    if (handle->id == 0 || handle->id == rlGetShaderIdDefault ())
    {
        g_clear_object (&self->shader);
        return NULL;
    }

    self->loc_ambient = GetShaderLocation (*handle, "ambientColor");
    self->loc_sun_dir = GetShaderLocation (*handle, "sunDirection");
    self->loc_sun_color = GetShaderLocation (*handle, "sunColor");
    self->loc_count = GetShaderLocation (*handle, "lightCount");
    self->loc_positions = GetShaderLocation (*handle, "lightPosition");
    self->loc_colors = GetShaderLocation (*handle, "lightColor");
    self->loc_radii = GetShaderLocation (*handle, "lightRadius");
    self->loc_cutoff = GetShaderLocation (*handle, "alphaCutoff");
    lrg_light_set_fog_locations (*handle, &self->fog_locs);
    return self->shader;
}

gboolean
lrg_light_set_apply (LrgLightSet *self,
                     gdouble      camera_x,
                     gdouble      camera_y,
                     gdouble      camera_z)
{
    gfloat  positions[LRG_LIGHT_SET_MAX_ACTIVE * 3];
    gfloat  colors[LRG_LIGHT_SET_MAX_ACTIVE * 3];
    gfloat  radii[LRG_LIGHT_SET_MAX_ACTIVE];
    gfloat  sun_dir[3] = { 0.0f, -1.0f, 0.0f };
    gfloat  sun_color[3] = { 0.0f, 0.0f, 0.0f };
    Shader *handle;
    gint    count;
    guint   i;

    g_return_val_if_fail (LRG_IS_LIGHT_SET (self), FALSE);

    lrg_light_set_select (self, camera_x, camera_y, camera_z);
    if (lrg_light_set_get_shader (self) == NULL)
        return FALSE;
    handle = grl_shader_get_handle (self->shader);

    /* Positions relative to the floating origin, colours pre-scaled. */
    for (i = 0; i < self->n_active; i++)
    {
        PointLight *pl = &g_array_index (self->lights, PointLight, self->active[i]);
        guint       k;

        for (k = 0; k < 3; k++)
        {
            positions[i * 3 + k] = (gfloat)(pl->position[k] - self->origin[k]);
            colors[i * 3 + k] = pl->color[k] * pl->intensity;
        }
        radii[i] = pl->radius;
    }
    if (self->has_directional)
    {
        memcpy (sun_dir, self->directional_dir, sizeof sun_dir);
        memcpy (sun_color, self->directional_color, sizeof sun_color);
    }
    count = (gint)self->n_active;

    SetShaderValue (*handle, self->loc_ambient, self->ambient, SHADER_UNIFORM_VEC3);
    SetShaderValue (*handle, self->loc_sun_dir, sun_dir, SHADER_UNIFORM_VEC3);
    SetShaderValue (*handle, self->loc_sun_color, sun_color, SHADER_UNIFORM_VEC3);
    SetShaderValue (*handle, self->loc_count, &count, SHADER_UNIFORM_INT);
    SetShaderValue (*handle, self->loc_cutoff, &self->cutoff, SHADER_UNIFORM_FLOAT);
    if (count > 0)
    {
        SetShaderValueV (*handle, self->loc_positions, positions, SHADER_UNIFORM_VEC3, count);
        SetShaderValueV (*handle, self->loc_colors, colors, SHADER_UNIFORM_VEC3, count);
        SetShaderValueV (*handle, self->loc_radii, radii, SHADER_UNIFORM_FLOAT, count);
    }
    lrg_light_set_upload_fog (self, *handle, &self->fog_locs);
    return TRUE;
}

/* ------------------------------------------------------------------------ */
/* Fog                                                                      */
/* ------------------------------------------------------------------------ */

gfloat
lrg_fog_amount (LrgFogType type,
                gfloat     density,
                gfloat     start,
                gfloat     end,
                gfloat     base_height,
                gfloat     falloff,
                gfloat     distance,
                gfloat     height)
{
    gfloat f;

    /* Mirrors lrg_fog_amount() in LRG_FOG_GLSL line by line. */
    if (!isfinite (distance) || distance < 0.0f)
        distance = 0.0f;
    switch (type)
    {
    case LRG_FOG_TYPE_UNIFORM:
        f = density;
        break;
    case LRG_FOG_TYPE_LINEAR:
        if (end > start)
            f = (distance - start) / (end - start);
        else
            f = distance >= end ? 1.0f : 0.0f;
        break;
    case LRG_FOG_TYPE_HEIGHT:
        f = (1.0f - expf (-density * distance))
            * expf (-falloff * MAX (height - base_height, 0.0f));
        break;
    case LRG_FOG_TYPE_EXPONENTIAL:
    default:
        f = 1.0f - expf (-density * distance);
        break;
    }
    if (!isfinite (f))
        return 0.0f;
    return CLAMP (f, 0.0f, 1.0f);
}

void
lrg_light_set_fog_locations (Shader                   shader,
                             LrgLightSetFogLocations *out_locs)
{
    out_locs->type = GetShaderLocation (shader, "fogType");
    out_locs->color = GetShaderLocation (shader, "fogColor");
    out_locs->params = GetShaderLocation (shader, "fogParams");
    out_locs->base = GetShaderLocation (shader, "fogBase");
    out_locs->camera = GetShaderLocation (shader, "fogCamera");
}

void
lrg_light_set_upload_fog (LrgLightSet                   *self,
                          Shader                         shader,
                          const LrgLightSetFogLocations *locs)
{
    gint   type = -1;
    gfloat params[4];
    gfloat camera[3];
    gfloat base;
    guint  k;

    if (self == NULL || !self->fog_on)
    {
        SetShaderValue (shader, locs->type, &type, SHADER_UNIFORM_INT);
        return;
    }

    /* Everything in draw space: the eye and the height base move with
     * the floating origin, like the point lights. */
    type = (gint)self->fog_type;
    params[0] = self->fog_density;
    params[1] = self->fog_start;
    params[2] = self->fog_end;
    params[3] = self->fog_falloff;
    for (k = 0; k < 3; k++)
        camera[k] = (gfloat)(self->camera[k] - self->origin[k]);
    base = (gfloat)((gdouble)self->fog_base - self->origin[1]);

    SetShaderValue (shader, locs->type, &type, SHADER_UNIFORM_INT);
    SetShaderValue (shader, locs->color, self->fog_color, SHADER_UNIFORM_VEC4);
    SetShaderValue (shader, locs->params, params, SHADER_UNIFORM_VEC4);
    SetShaderValue (shader, locs->base, &base, SHADER_UNIFORM_FLOAT);
    SetShaderValue (shader, locs->camera, camera, SHADER_UNIFORM_VEC3);
}

void
lrg_light_set_set_fog (LrgLightSet    *self,
                       LrgFogType      type,
                       const GrlColor *color,
                       gfloat          density,
                       gfloat          start,
                       gfloat          end)
{
    g_return_if_fail (LRG_IS_LIGHT_SET (self));
    g_return_if_fail (color != NULL);

    /* Non-finite numbers would poison every fragment; treat them as 0. */
    self->fog_on = TRUE;
    self->fog_type = type;
    color_to_rgb (color, self->fog_color);
    self->fog_color[3] = (gfloat)color->a / 255.0f;
    self->fog_density = isfinite (density) ? MAX (density, 0.0f) : 0.0f;
    self->fog_start = isfinite (start) ? start : 0.0f;
    self->fog_end = isfinite (end) ? end : 0.0f;
}

void
lrg_light_set_set_fog_height (LrgLightSet *self,
                              gfloat       base_height,
                              gfloat       falloff)
{
    g_return_if_fail (LRG_IS_LIGHT_SET (self));

    self->fog_base = isfinite (base_height) ? base_height : 0.0f;
    self->fog_falloff = isfinite (falloff) ? MAX (falloff, 0.0f) : 0.0f;
}

void
lrg_light_set_clear_fog (LrgLightSet *self)
{
    g_return_if_fail (LRG_IS_LIGHT_SET (self));

    self->fog_on = FALSE;
}

gboolean
lrg_light_set_get_fog (LrgLightSet *self,
                       LrgFogType  *out_type,
                       GrlColor    *out_color,
                       gfloat      *out_density,
                       gfloat      *out_start,
                       gfloat      *out_end)
{
    g_return_val_if_fail (LRG_IS_LIGHT_SET (self), FALSE);

    if (!self->fog_on)
        return FALSE;
    if (out_type != NULL)
        *out_type = self->fog_type;
    if (out_color != NULL)
    {
        out_color->r = (guint8)(self->fog_color[0] * 255.0f + 0.5f);
        out_color->g = (guint8)(self->fog_color[1] * 255.0f + 0.5f);
        out_color->b = (guint8)(self->fog_color[2] * 255.0f + 0.5f);
        out_color->a = (guint8)(self->fog_color[3] * 255.0f + 0.5f);
    }
    if (out_density != NULL)
        *out_density = self->fog_density;
    if (out_start != NULL)
        *out_start = self->fog_start;
    if (out_end != NULL)
        *out_end = self->fog_end;
    return TRUE;
}

gfloat
lrg_light_set_sample_fog (LrgLightSet      *self,
                          const GrlVector3 *position)
{
    gdouble d[3];
    gfloat  distance;
    gfloat  height;

    g_return_val_if_fail (LRG_IS_LIGHT_SET (self), 0.0f);
    g_return_val_if_fail (position != NULL, 0.0f);

    if (!self->fog_on)
        return 0.0f;

    /* The shader's draw space: eye and base minus the origin. */
    d[0] = (gdouble)position->x - (self->camera[0] - self->origin[0]);
    d[1] = (gdouble)position->y - (self->camera[1] - self->origin[1]);
    d[2] = (gdouble)position->z - (self->camera[2] - self->origin[2]);
    distance = (gfloat)sqrt (d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    height = (gfloat)((gdouble)position->y + self->origin[1]);

    return lrg_fog_amount (self->fog_type, self->fog_density, self->fog_start,
                           self->fog_end, self->fog_base, self->fog_falloff,
                           distance, height) * self->fog_color[3];
}

/* ------------------------------------------------------------------------ */
/* CPU evaluation and binding                                               */
/* ------------------------------------------------------------------------ */

void
lrg_light_set_sample (LrgLightSet      *self,
                      const GrlVector3 *position,
                      const GrlVector3 *normal,
                      gfloat           *out_rgb)
{
    gfloat n[3] = { 0.0f, 0.0f, 0.0f };
    gfloat p[3];
    gboolean has_normal = FALSE;
    guint i;
    guint k;

    g_return_if_fail (LRG_IS_LIGHT_SET (self));
    g_return_if_fail (position != NULL);
    g_return_if_fail (out_rgb != NULL);

    /* The normal is normalized as the fragment shader does; a zero one
     * means "faces every light". */
    if (normal != NULL)
    {
        gfloat len = sqrtf (normal->x * normal->x + normal->y * normal->y
                            + normal->z * normal->z);

        if (len > 0.0f && isfinite (len))
        {
            n[0] = normal->x / len;
            n[1] = normal->y / len;
            n[2] = normal->z / len;
            has_normal = TRUE;
        }
    }

    p[0] = position->x;
    p[1] = position->y;
    p[2] = position->z;
    for (k = 0; k < 3; k++)
        out_rgb[k] = self->ambient[k];

    if (self->has_directional)
    {
        gfloat lambert = 1.0f;

        if (has_normal)
            lambert = MAX (-(n[0] * self->directional_dir[0]
                             + n[1] * self->directional_dir[1]
                             + n[2] * self->directional_dir[2]), 0.0f);
        for (k = 0; k < 3; k++)
            out_rgb[k] += self->directional_color[k] * lambert;
    }

    /* Point lights of the last selection, origin-relative like the
     * uniforms apply() uploads. */
    for (i = 0; i < self->n_active; i++)
    {
        PointLight *pl = &g_array_index (self->lights, PointLight, self->active[i]);
        gfloat d[3];
        gfloat dist;
        gfloat f;
        gfloat lambert = 1.0f;

        for (k = 0; k < 3; k++)
            d[k] = (gfloat)(pl->position[k] - self->origin[k]) - p[k];
        dist = sqrtf (d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
        f = CLAMP (1.0f - dist / pl->radius, 0.0f, 1.0f);
        if (f <= 0.0f)
            continue;
        if (has_normal && dist > 0.0001f)
            lambert = MAX ((n[0] * d[0] + n[1] * d[1] + n[2] * d[2]) / dist, 0.0f);
        for (k = 0; k < 3; k++)
            out_rgb[k] += pl->color[k] * pl->intensity * f * f * lambert;
    }
}

void
lrg_light_set_bind (LrgLightSet *self)
{
    g_return_if_fail (LRG_IS_LIGHT_SET (self));

    g_set_object (&bound_light_set, self);
}

void
lrg_light_set_unbind (void)
{
    g_clear_object (&bound_light_set);
}

LrgLightSet *
lrg_light_set_get_bound (void)
{
    return bound_light_set;
}
