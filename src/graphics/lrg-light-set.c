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
 *
 * Light grid: select() also sorts every light into the XZ cells its
 * radius reaches (a square of cells centred on the camera's cell, at
 * most LRG_LIGHT_SET_GRID_SLOTS per cell, the closest relative to their
 * radius first). apply() uploads the lights as a float texture (two
 * texels per light: position and radius, colour times intensity) and the
 * cell lists as a second one (four light indices per texel, -1 ends a
 * list), binds them to texture units 14 and 15 (above raylib's material
 * maps, which DrawMesh() binds and unbinds per draw) and the shader
 * walks the list of each fragment's cell. sample() reads the same lists
 * so billboards match the geometry.
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

    /* light grid (cells == 0: off) */
    gfloat      grid_cell;
    guint       grid_cells;
    gboolean    grid_built;      /* select() filled the lists */
    gdouble     grid_x0;         /* world corner of cell (0, 0) */
    gdouble     grid_z0;
    guint      *grid_ids;        /* cells * cells * SLOTS light ids */
    gfloat     *grid_keys;
    guint8     *grid_counts;
    guint       tex_data;        /* GL texture ids, 0 = none */
    gint        tex_data_height;
    guint       tex_grid;
    gint        tex_grid_cells;

    gfloat      light_limit;
    gfloat      emission[3];

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
    gint        loc_use_grid;
    gint        loc_light_data;
    gint        loc_light_grid;
    gint        loc_grid_params;
    gint        loc_limit;
    gint        loc_emission_color;
    LrgLightSetFogLocations fog_locs;
};

/* texture units of the grid textures: above raylib's 12 material maps */
#define GRID_DATA_UNIT  (14)
#define GRID_CELLS_UNIT (15)

/* the soft light limit passes values up to this share of the limit */
#define LIMIT_KNEE      (0.75f)

G_DEFINE_TYPE (LrgLightSet, lrg_light_set, G_TYPE_OBJECT)

static void build_grid (LrgLightSet *self,
                        gdouble      camera_x,
                        gdouble      camera_y,
                        gdouble      camera_z);

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
    "uniform int useGrid;\n"
    "uniform sampler2D lightData;\n"
    "uniform sampler2D lightGrid;\n"
    "uniform vec4 gridParams;\n"
    "uniform float lightLimit;\n"
    "uniform sampler2D emissionMap;\n"
    "uniform vec3 emissionColor;\n"
    LRG_FOG_GLSL
    "out vec4 finalColor;\n"
    "vec3 point_light(vec3 n, vec3 p, float r, vec3 c) {\n"
    "    vec3 d = p - fragPosition;\n"
    "    float dist = length(d);\n"
    "    float k = clamp(1.0 - dist / r, 0.0, 1.0);\n"
    "    float lambert = dist > 0.0001 ? max(dot(n, d / dist), 0.0) : 1.0;\n"
    "    return c * (k * k * lambert);\n"
    "}\n"
    "void main() {\n"
    "    vec4 texel = texture(texture0, fragTexCoord) * colDiffuse * fragColor;\n"
    "    if (texel.a < alphaCutoff) discard;\n"
    "    float nlen = length(fragNormal);\n"
    "    vec3 n = nlen > 0.0 ? fragNormal / nlen : vec3(0.0, 1.0, 0.0);\n"
    "    vec3 light = ambientColor + sunColor * max(dot(n, -sunDirection), 0.0);\n"
    "    if (useGrid == 1) {\n"
    "        vec2 cell = floor((fragPosition.xz - gridParams.xy) / gridParams.z);\n"
    "        if (cell.x >= 0.0 && cell.y >= 0.0 && cell.x < gridParams.w && cell.y < gridParams.w) {\n"
    "            ivec2 base = ivec2(int(cell.x) * 4, int(cell.y));\n"
    "            bool done = false;\n"
    "            for (int t = 0; t < 4 && !done; t++) {\n"
    "                vec4 ids = texelFetch(lightGrid, base + ivec2(t, 0), 0);\n"
    "                for (int j = 0; j < 4; j++) {\n"
    "                    if (ids[j] < 0.0) { done = true; break; }\n"
    "                    int id = int(ids[j] + 0.5);\n"
    "                    vec4 pr = texelFetch(lightData, ivec2(0, id), 0);\n"
    "                    vec4 col = texelFetch(lightData, ivec2(1, id), 0);\n"
    "                    light += point_light(n, pr.xyz, pr.w, col.rgb);\n"
    "                }\n"
    "            }\n"
    "        }\n"
    "    } else {\n"
    "        for (int i = 0; i < MAX_LIGHTS; i++) {\n"
    "            if (i >= lightCount) break;\n"
    "            light += point_light(n, lightPosition[i], lightRadius[i], lightColor[i]);\n"
    "        }\n"
    "    }\n"
    "    if (lightLimit > 0.0) {\n"
    "        float knee = lightLimit * 0.75;\n"
    "        vec3 over = max(light - knee, 0.0);\n"
    "        light = min(light, vec3(knee)) + (lightLimit - knee) * (1.0 - exp(-over / (lightLimit - knee)));\n"
    "    }\n"
    "    vec3 lit = texel.rgb * light + texture(emissionMap, fragTexCoord).rgb * emissionColor;\n"
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
    g_clear_pointer (&self->grid_ids, g_free);
    g_clear_pointer (&self->grid_keys, g_free);
    g_clear_pointer (&self->grid_counts, g_free);
    /* GPU textures only while the context lives (never at exit) */
    if (IsWindowReady ())
    {
        if (self->tex_data != 0)
            rlUnloadTexture (self->tex_data);
        if (self->tex_grid != 0)
            rlUnloadTexture (self->tex_grid);
    }
    self->tex_data = 0;
    self->tex_grid = 0;

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
    self->grid_built = FALSE;
    return TRUE;
}

void
lrg_light_set_clear_point_lights (LrgLightSet *self)
{
    g_return_if_fail (LRG_IS_LIGHT_SET (self));

    g_array_set_size (self->lights, 0);
    self->n_lights = 0;
    self->n_active = 0;
    self->grid_built = FALSE;
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
    if (self->grid_cells > 0)
        build_grid (self, camera_x, camera_y, camera_z);
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

/* ------------------------------------------------------------------------ */
/* Light grid                                                               */
/* ------------------------------------------------------------------------ */

void
lrg_light_set_set_grid (LrgLightSet *self,
                        gfloat       cell_size,
                        guint        cells)
{
    gsize n;

    g_return_if_fail (LRG_IS_LIGHT_SET (self));
    g_return_if_fail (cells == 0 || (cells >= 2 && cells <= LRG_LIGHT_SET_GRID_MAX_CELLS));
    g_return_if_fail (cells == 0 || (cell_size > 0.0f && isfinite (cell_size)));

    g_clear_pointer (&self->grid_ids, g_free);
    g_clear_pointer (&self->grid_keys, g_free);
    g_clear_pointer (&self->grid_counts, g_free);
    self->grid_built = FALSE;
    self->grid_cells = cells;
    self->grid_cell = cell_size;
    if (cells == 0)
        return;

    n = (gsize)cells * cells;
    self->grid_ids = g_new (guint, n * LRG_LIGHT_SET_GRID_SLOTS);
    self->grid_keys = g_new (gfloat, n * LRG_LIGHT_SET_GRID_SLOTS);
    self->grid_counts = g_new0 (guint8, n);
}

/* grid_insert:
 * Puts @id with @key into the sorted list of cell @c; a full list keeps
 * its best (smallest key) entries. Equal keys keep the earlier light. */
static void
grid_insert (LrgLightSet *self,
             gsize        c,
             guint        id,
             gfloat       key)
{
    guint  *ids = self->grid_ids + c * LRG_LIGHT_SET_GRID_SLOTS;
    gfloat *keys = self->grid_keys + c * LRG_LIGHT_SET_GRID_SLOTS;
    guint   count = self->grid_counts[c];
    guint   slot;

    if (count == LRG_LIGHT_SET_GRID_SLOTS && key >= keys[count - 1])
        return;
    slot = count < LRG_LIGHT_SET_GRID_SLOTS ? count : count - 1;
    while (slot > 0 && keys[slot - 1] > key)
    {
        keys[slot] = keys[slot - 1];
        ids[slot] = ids[slot - 1];
        slot--;
    }
    keys[slot] = key;
    ids[slot] = id;
    if (count < LRG_LIGHT_SET_GRID_SLOTS)
        self->grid_counts[c] = (guint8)(count + 1);
}

/* build_grid:
 * Sorts every lit light into the cells its radius reaches, around the
 * camera's cell. A light's key in a cell is the distance from the light
 * to the cell's rectangle over its radius: 0 inside, 1 at the edge of
 * its reach, so strong lights win over weak ones at equal distance. */
static void
build_grid (LrgLightSet *self,
            gdouble      camera_x,
            gdouble      camera_y,
            gdouble      camera_z)
{
    guint   n = self->grid_cells;
    gdouble cell = (gdouble)self->grid_cell;
    gdouble extent = cell * (gdouble)n;
    guint   id;

    memset (self->grid_counts, 0, (gsize)n * n);
    self->grid_x0 = (floor (camera_x / cell) - (gdouble)(n / 2)) * cell;
    self->grid_z0 = (floor (camera_z / cell) - (gdouble)(n / 2)) * cell;

    for (id = 0; id < self->lights->len; id++)
    {
        PointLight *pl = &g_array_index (self->lights, PointLight, id);
        gdouble     r;
        gdouble     lx;
        gdouble     lz;
        gint        gx0;
        gint        gx1;
        gint        gz0;
        gint        gz1;
        gint        gx;
        gint        gz;

        if (!pl->used || !pl->enabled || pl->intensity <= 0.0f)
            continue;
        r = (gdouble)pl->radius;
        if (self->max_distance > 0.0f)
        {
            gdouble dx = pl->position[0] - camera_x;
            gdouble dy = pl->position[1] - camera_y;
            gdouble dz = pl->position[2] - camera_z;

            if (sqrt (dx * dx + dy * dy + dz * dz) - r > (gdouble)self->max_distance)
                continue;
        }

        /* the cells the light's circle overlaps, clipped to the grid */
        lx = pl->position[0] - self->grid_x0;
        lz = pl->position[2] - self->grid_z0;
        if (lx + r < 0.0 || lz + r < 0.0 || lx - r >= extent || lz - r >= extent)
            continue;
        gx0 = (gint)CLAMP (floor ((lx - r) / cell), 0.0, (gdouble)(n - 1));
        gx1 = (gint)CLAMP (floor ((lx + r) / cell), 0.0, (gdouble)(n - 1));
        gz0 = (gint)CLAMP (floor ((lz - r) / cell), 0.0, (gdouble)(n - 1));
        gz1 = (gint)CLAMP (floor ((lz + r) / cell), 0.0, (gdouble)(n - 1));

        for (gz = gz0; gz <= gz1; gz++)
        {
            for (gx = gx0; gx <= gx1; gx++)
            {
                gdouble ax = (gdouble)gx * cell;
                gdouble az = (gdouble)gz * cell;
                gdouble dx = MAX (MAX (ax - lx, 0.0), lx - (ax + cell));
                gdouble dz = MAX (MAX (az - lz, 0.0), lz - (az + cell));
                gdouble d = sqrt (dx * dx + dz * dz);

                if (d >= r)
                    continue;
                grid_insert (self, (gsize)gz * n + (gsize)gx, id, (gfloat)(d / r));
            }
        }
    }
    self->grid_built = TRUE;
}

/* grid_cell_at:
 * The cell index of world (@x, @z), or -1 outside the built grid. */
static gssize
grid_cell_at (LrgLightSet *self,
              gdouble      x,
              gdouble      z)
{
    gdouble gx;
    gdouble gz;

    if (self->grid_cells == 0 || !self->grid_built)
        return -1;
    gx = floor ((x - self->grid_x0) / (gdouble)self->grid_cell);
    gz = floor ((z - self->grid_z0) / (gdouble)self->grid_cell);
    if (!(gx >= 0.0 && gz >= 0.0 && gx < (gdouble)self->grid_cells
          && gz < (gdouble)self->grid_cells))
        return -1;
    return (gssize)gz * (gssize)self->grid_cells + (gssize)gx;
}

guint
lrg_light_set_get_grid_light_count (LrgLightSet *self,
                                    gdouble      x,
                                    gdouble      z)
{
    gssize c;

    g_return_val_if_fail (LRG_IS_LIGHT_SET (self), 0);

    c = grid_cell_at (self, x, z);
    return c < 0 ? 0 : self->grid_counts[c];
}

guint
lrg_light_set_get_grid_light (LrgLightSet *self,
                              gdouble      x,
                              gdouble      z,
                              guint        index)
{
    gssize c;

    g_return_val_if_fail (LRG_IS_LIGHT_SET (self), LRG_LIGHT_SET_INVALID_LIGHT);

    c = grid_cell_at (self, x, z);
    if (c < 0 || index >= self->grid_counts[c])
        return LRG_LIGHT_SET_INVALID_LIGHT;
    return self->grid_ids[(gsize)c * LRG_LIGHT_SET_GRID_SLOTS + index];
}

/* ------------------------------------------------------------------------ */
/* Light limit and emission                                                 */
/* ------------------------------------------------------------------------ */

void
lrg_light_set_set_light_limit (LrgLightSet *self,
                               gfloat       limit)
{
    g_return_if_fail (LRG_IS_LIGHT_SET (self));
    g_return_if_fail (limit >= 0.0f);

    self->light_limit = isfinite (limit) ? limit : 0.0f;
}

gfloat
lrg_light_set_get_light_limit (LrgLightSet *self)
{
    g_return_val_if_fail (LRG_IS_LIGHT_SET (self), 0.0f);
    return self->light_limit;
}

gfloat
lrg_light_set_limit_light (gfloat limit,
                           gfloat value)
{
    gfloat knee;

    /* Mirrors the shader: identity to the knee, then an exponential
     * roll-off with slope 1 at the knee and @limit as its asymptote. */
    if (limit <= 0.0f || value <= limit * LIMIT_KNEE)
        return value;
    knee = limit * LIMIT_KNEE;
    return knee + (limit - knee) * (1.0f - expf (-(value - knee) / (limit - knee)));
}

void
lrg_light_set_set_emission (LrgLightSet    *self,
                            const GrlColor *color,
                            gfloat          intensity)
{
    guint k;

    g_return_if_fail (LRG_IS_LIGHT_SET (self));
    g_return_if_fail (intensity >= 0.0f);

    if (color == NULL)
    {
        self->emission[0] = self->emission[1] = self->emission[2] = 0.0f;
        return;
    }
    color_to_rgb (color, self->emission);
    for (k = 0; k < 3; k++)
        self->emission[k] *= intensity;
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
    self->loc_use_grid = GetShaderLocation (*handle, "useGrid");
    self->loc_light_data = GetShaderLocation (*handle, "lightData");
    self->loc_light_grid = GetShaderLocation (*handle, "lightGrid");
    self->loc_grid_params = GetShaderLocation (*handle, "gridParams");
    self->loc_limit = GetShaderLocation (*handle, "lightLimit");
    self->loc_emission_color = GetShaderLocation (*handle, "emissionColor");
    lrg_light_set_fog_locations (*handle, &self->fog_locs);

    /* The emission map is raylib's MATERIAL_MAP_EMISSION: DrawMesh()
     * binds a material's map to that unit and sets the sampler through
     * this location. Without a map the unit is empty and reads black;
     * the sampler must not default to unit 0 (the albedo). */
    {
        gint unit = LRG_LIGHT_SET_EMISSION_UNIT;

        handle->locs[SHADER_LOC_MAP_EMISSION] = GetShaderLocation (*handle, "emissionMap");
        SetShaderValue (*handle, handle->locs[SHADER_LOC_MAP_EMISSION], &unit, SHADER_UNIFORM_INT);
        unit = GRID_DATA_UNIT;
        SetShaderValue (*handle, self->loc_light_data, &unit, SHADER_UNIFORM_INT);
        unit = GRID_CELLS_UNIT;
        SetShaderValue (*handle, self->loc_light_grid, &unit, SHADER_UNIFORM_INT);
    }
    return self->shader;
}

/* upload_texture:
 * Creates or refreshes a float RGBA texture (point sampled) of @width x
 * @height texels; a size change recreates it. */
static void
upload_texture (guint        *id,
                gint         *have_w,
                gint         *have_h,
                gint          width,
                gint          height,
                const gfloat *texels)
{
    if (*id != 0 && (*have_w != width || *have_h != height))
    {
        rlUnloadTexture (*id);
        *id = 0;
    }
    if (*id == 0)
    {
        *id = rlLoadTexture (texels, width, height, RL_PIXELFORMAT_UNCOMPRESSED_R32G32B32A32, 1);
        rlTextureParameters (*id, RL_TEXTURE_MIN_FILTER, RL_TEXTURE_FILTER_NEAREST);
        rlTextureParameters (*id, RL_TEXTURE_MAG_FILTER, RL_TEXTURE_FILTER_NEAREST);
        *have_w = width;
        *have_h = height;
    }
    else
    {
        rlUpdateTexture (*id, 0, 0, width, height, RL_PIXELFORMAT_UNCOMPRESSED_R32G32B32A32, texels);
    }
}

/* upload_grid:
 * The lights of the built grid (two texels each, in first-use order)
 * and the cell lists (four indices per texel, -1 terminated), bound to
 * GRID_DATA_UNIT / GRID_CELLS_UNIT, plus the grid uniforms. */
static void
upload_grid (LrgLightSet *self,
             Shader      *handle)
{
    g_autofree gfloat *data = NULL;
    g_autofree gfloat *cells = NULL;
    g_autofree guint  *index_of = NULL;
    guint   n = self->grid_cells;
    guint   n_data = 0;
    gsize   c;
    gint    grid_w;
    gint    unused_w;
    gint    use_grid = 1;
    gfloat  params[4];

    /* raylib leaves the last material slot active after DrawMesh();
     * uploads must not bind into a material unit */
    rlActiveTextureSlot (0);
    grid_w = (gint)(n * (LRG_LIGHT_SET_GRID_SLOTS / 4));
    data = g_new0 (gfloat, (gsize)LRG_LIGHT_SET_GRID_MAX_LIGHTS * 8);
    cells = g_new (gfloat, (gsize)n * n * LRG_LIGHT_SET_GRID_SLOTS);
    index_of = g_new (guint, MAX (self->lights->len, 1));
    memset (index_of, 0xff, sizeof (guint) * MAX (self->lights->len, 1));

    for (c = 0; c < (gsize)n * n; c++)
    {
        guint written = 0;
        guint s;

        for (s = 0; s < self->grid_counts[c]; s++)
        {
            guint       id = self->grid_ids[c * LRG_LIGHT_SET_GRID_SLOTS + s];
            PointLight *pl = &g_array_index (self->lights, PointLight, id);

            if (index_of[id] == G_MAXUINT)
            {
                gfloat *t;
                guint   k;

                if (n_data == LRG_LIGHT_SET_GRID_MAX_LIGHTS)
                    continue;
                index_of[id] = n_data;
                t = data + (gsize)n_data * 8;
                for (k = 0; k < 3; k++)
                {
                    t[k] = (gfloat)(pl->position[k] - self->origin[k]);
                    t[4 + k] = pl->color[k] * pl->intensity;
                }
                t[3] = pl->radius;
                n_data++;
            }
            cells[c * LRG_LIGHT_SET_GRID_SLOTS + written++] = (gfloat)index_of[id];
        }
        for (; written < LRG_LIGHT_SET_GRID_SLOTS; written++)
            cells[c * LRG_LIGHT_SET_GRID_SLOTS + written] = -1.0f;
    }

    /* the cell texture is (n * 4) x n: cell (x, z) at texels 4x .. 4x+3
     * of row z; the light texture 2 x lights */
    unused_w = 2;
    upload_texture (&self->tex_data, &unused_w, &self->tex_data_height,
                    2, (gint)MAX (n_data, 1u), data);
    {
        gint have_w = self->tex_grid_cells * (LRG_LIGHT_SET_GRID_SLOTS / 4);
        gint have_h = self->tex_grid_cells;

        upload_texture (&self->tex_grid, &have_w, &have_h, grid_w, (gint)n, cells);
        self->tex_grid_cells = (gint)n;
    }

    rlActiveTextureSlot (GRID_DATA_UNIT);
    rlEnableTexture (self->tex_data);
    rlActiveTextureSlot (GRID_CELLS_UNIT);
    rlEnableTexture (self->tex_grid);
    rlActiveTextureSlot (0);

    params[0] = (gfloat)(self->grid_x0 - self->origin[0]);
    params[1] = (gfloat)(self->grid_z0 - self->origin[2]);
    params[2] = self->grid_cell;
    params[3] = (gfloat)n;
    SetShaderValue (*handle, self->loc_use_grid, &use_grid, SHADER_UNIFORM_INT);
    SetShaderValue (*handle, self->loc_grid_params, params, SHADER_UNIFORM_VEC4);
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
    SetShaderValue (*handle, self->loc_limit, &self->light_limit, SHADER_UNIFORM_FLOAT);
    SetShaderValue (*handle, self->loc_emission_color, self->emission, SHADER_UNIFORM_VEC3);
    if (self->grid_cells > 0 && self->grid_built)
    {
        upload_grid (self, handle);
    }
    else
    {
        gint use_grid = 0;

        SetShaderValue (*handle, self->loc_use_grid, &use_grid, SHADER_UNIFORM_INT);
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
    gssize cell;
    guint count;
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

    /* Point lights: the grid cell of @position when there is a grid,
     * else the last selection; origin-relative like the uniforms apply()
     * uploads. */
    cell = grid_cell_at (self, (gdouble)p[0] + self->origin[0], (gdouble)p[2] + self->origin[2]);
    if (self->grid_cells > 0 && self->grid_built)
        count = cell < 0 ? 0 : self->grid_counts[cell];
    else
        count = self->n_active;
    for (i = 0; i < count; i++)
    {
        PointLight *pl;
        gfloat d[3];
        gfloat dist;
        gfloat f;
        gfloat lambert = 1.0f;

        if (self->grid_cells > 0 && self->grid_built)
            pl = &g_array_index (self->lights, PointLight,
                                 self->grid_ids[(gsize)cell * LRG_LIGHT_SET_GRID_SLOTS + i]);
        else
            pl = &g_array_index (self->lights, PointLight, self->active[i]);

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

    for (k = 0; k < 3; k++)
        out_rgb[k] = lrg_light_set_limit_light (self->light_limit, out_rgb[k]);
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
