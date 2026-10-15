/* lrg-character-controller.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Capsule move-and-slide on LrgCollisionMesh.
 *
 * Every sweep uses the capsule inflated by the skin width, so the real
 * capsule keeps that gap from geometry, and the mesh's contact rule (a
 * touching triangle blocks only motion that goes deeper) lets a resting
 * capsule slide along floors and walls. Two normals matter:
 *
 *  - the contact normal (from the surface to the capsule axis) deflects
 *    motion: walkable contacts turn horizontal motion along the slope,
 *    steeper ones act as vertical walls;
 *  - the face normal of the triangle touched decides whether the capsule
 *    stands on walkable ground, so the rounded bottom resting on a step
 *    edge counts as standing on the tread.
 */

#include "config.h"

#include <math.h>
#include <string.h>

#ifndef LIBREGNUM_COMPILATION
#define LIBREGNUM_COMPILATION
#endif
#include "physics/lrg-character-controller.h"
#include "physics/lrg-collision-mesh-private.h"

/* Contacts handled per collide-and-slide pass. */
#define SLIDE_ITERATIONS        (4)
/* Remaining motion shorter than this ends a slide. */
#define SLIDE_MIN_MOTION        (1e-9)
/* Depenetration passes per move. */
#define DEPENETRATE_ITERATIONS  (4)
/* Extra progress a stepped move needs over the plain one. */
#define STEP_MIN_GAIN           (1e-4)
/* Terrain normal sampling offset (world units). */
#define TERRAIN_NORMAL_DELTA    (0.25)

struct _LrgCharacterController
{
    GObject           parent_instance;

    gdouble           radius;
    gdouble           height;
    gdouble           step_height;
    gdouble           max_slope;     /* degrees */
    gdouble           min_ground_y;  /* cos (max_slope) */
    gdouble           skin;
    LrgCollisionMesh *world;         /* owned, nullable */
    LrgHeightFunc     height_func;
    gpointer          height_data;
    GDestroyNotify    height_destroy;
};

G_DEFINE_TYPE (LrgCharacterController, lrg_character_controller, G_TYPE_OBJECT)

static void
lrg_character_controller_dispose (GObject *object)
{
    LrgCharacterController *self;

    self = LRG_CHARACTER_CONTROLLER (object);
    g_clear_object (&self->world);
    lrg_character_controller_set_height_func (self, NULL, NULL, NULL);

    G_OBJECT_CLASS (lrg_character_controller_parent_class)->dispose (object);
}

static void
lrg_character_controller_class_init (LrgCharacterControllerClass *klass)
{
    G_OBJECT_CLASS (klass)->dispose = lrg_character_controller_dispose;
}

static void
lrg_character_controller_init (LrgCharacterController *self)
{
    self->radius = 0.5;
    self->height = 2.0;
    self->step_height = LRG_CHARACTER_CONTROLLER_DEFAULT_STEP_HEIGHT;
    self->max_slope = LRG_CHARACTER_CONTROLLER_DEFAULT_MAX_SLOPE;
    self->min_ground_y = cos (self->max_slope * G_PI / 180.0);
    self->skin = LRG_CHARACTER_CONTROLLER_DEFAULT_SKIN_WIDTH;
}

LrgCharacterController *
lrg_character_controller_new (gfloat radius,
                              gfloat height)
{
    LrgCharacterController *self;

    self = (LrgCharacterController *) g_object_new (LRG_TYPE_CHARACTER_CONTROLLER, NULL);
    lrg_character_controller_set_radius (self, radius);
    lrg_character_controller_set_height (self, height);
    return self;
}

/* ------------------------------------------------------------------------ */
/* Properties                                                               */
/* ------------------------------------------------------------------------ */

void
lrg_character_controller_set_radius (LrgCharacterController *self,
                                     gfloat                  radius)
{
    g_return_if_fail (LRG_IS_CHARACTER_CONTROLLER (self));

    if (!isfinite (radius) || radius <= 0.0f)
        return;
    self->radius = radius;
    self->height = MAX (self->height, 2.0 * self->radius);
    self->skin = CLAMP (self->skin, 0.0001, self->radius / 2.0);
}

gfloat
lrg_character_controller_get_radius (LrgCharacterController *self)
{
    g_return_val_if_fail (LRG_IS_CHARACTER_CONTROLLER (self), 0.0f);

    return (gfloat) self->radius;
}

void
lrg_character_controller_set_height (LrgCharacterController *self,
                                     gfloat                  height)
{
    g_return_if_fail (LRG_IS_CHARACTER_CONTROLLER (self));

    if (!isfinite (height))
        return;
    self->height = MAX ((gdouble) height, 2.0 * self->radius);
}

gfloat
lrg_character_controller_get_height (LrgCharacterController *self)
{
    g_return_val_if_fail (LRG_IS_CHARACTER_CONTROLLER (self), 0.0f);

    return (gfloat) self->height;
}

void
lrg_character_controller_set_step_height (LrgCharacterController *self,
                                          gfloat                  step_height)
{
    g_return_if_fail (LRG_IS_CHARACTER_CONTROLLER (self));

    if (!isfinite (step_height) || step_height < 0.0f)
        return;
    self->step_height = step_height;
}

gfloat
lrg_character_controller_get_step_height (LrgCharacterController *self)
{
    g_return_val_if_fail (LRG_IS_CHARACTER_CONTROLLER (self), 0.0f);

    return (gfloat) self->step_height;
}

void
lrg_character_controller_set_max_slope (LrgCharacterController *self,
                                        gfloat                  degrees)
{
    g_return_if_fail (LRG_IS_CHARACTER_CONTROLLER (self));

    if (!isfinite (degrees))
        return;
    self->max_slope = CLAMP ((gdouble) degrees, 0.0, 89.9);
    self->min_ground_y = cos (self->max_slope * G_PI / 180.0);
}

gfloat
lrg_character_controller_get_max_slope (LrgCharacterController *self)
{
    g_return_val_if_fail (LRG_IS_CHARACTER_CONTROLLER (self), 0.0f);

    return (gfloat) self->max_slope;
}

void
lrg_character_controller_set_skin_width (LrgCharacterController *self,
                                         gfloat                  skin)
{
    g_return_if_fail (LRG_IS_CHARACTER_CONTROLLER (self));

    if (!isfinite (skin))
        return;
    self->skin = CLAMP ((gdouble) skin, 0.0001, self->radius / 2.0);
}

gfloat
lrg_character_controller_get_skin_width (LrgCharacterController *self)
{
    g_return_val_if_fail (LRG_IS_CHARACTER_CONTROLLER (self), 0.0f);

    return (gfloat) self->skin;
}

void
lrg_character_controller_set_world (LrgCharacterController *self,
                                    LrgCollisionMesh       *world)
{
    g_return_if_fail (LRG_IS_CHARACTER_CONTROLLER (self));
    g_return_if_fail (world == NULL || LRG_IS_COLLISION_MESH (world));

    g_set_object (&self->world, world);
}

LrgCollisionMesh *
lrg_character_controller_get_world (LrgCharacterController *self)
{
    g_return_val_if_fail (LRG_IS_CHARACTER_CONTROLLER (self), NULL);

    return self->world;
}

void
lrg_character_controller_set_height_func (LrgCharacterController *self,
                                          LrgHeightFunc           func,
                                          gpointer                user_data,
                                          GDestroyNotify          destroy)
{
    GDestroyNotify old_destroy;
    gpointer old_data;

    g_return_if_fail (LRG_IS_CHARACTER_CONTROLLER (self));

    /* release the old closure after installing the new one */
    old_destroy = self->height_destroy;
    old_data = self->height_data;
    self->height_func = func;
    self->height_data = user_data;
    self->height_destroy = destroy;
    if (old_destroy != NULL)
        old_destroy (old_data);
}

/* ------------------------------------------------------------------------ */
/* Sweeps                                                                   */
/* ------------------------------------------------------------------------ */

typedef struct
{
    gdouble  fraction;
    gdouble  normal[3];    /* contact normal, towards the capsule */
    gdouble  face[3];      /* face normal on the capsule's side */
    gdouble  point[3];     /* contact point on the surface */
    guint64  instance_id;
} Contact;

/* Capsule axis for feet pos and height h. */
static void
axis_points (LrgCharacterController *self,
             const gdouble          *pos,
             gdouble                 h,
             gdouble                *a,
             gdouble                *b)
{
    gdouble top;

    top = MAX (h - self->radius, self->radius);
    a[0] = pos[0];
    a[1] = pos[1] + self->radius;
    a[2] = pos[2];
    b[0] = pos[0];
    b[1] = pos[1] + top;
    b[2] = pos[2];
}

/* Sweeps the skin-inflated capsule; FALSE when unobstructed. */
static gboolean
sweep (LrgCharacterController *self,
       const gdouble          *pos,
       const gdouble          *motion,
       Contact                *contact)
{
    gdouble a[3];
    gdouble b[3];
    LrgCollisionHit hit;
    guint k;

    if (self->world == NULL)
        return FALSE;
    axis_points (self, pos, self->height, a, b);
    if (!lrg_collision_mesh_sweep_segment (self->world, a, b, self->radius + self->skin,
                                           motion, &hit, &contact->fraction, contact->face))
        return FALSE;
    for (k = 0; k < 3; k++)
    {
        contact->normal[k] = hit.normal[k];
        contact->point[k] = hit.position[k];
    }
    contact->instance_id = hit.instance_id;
    return TRUE;
}

/* ------------------------------------------------------------------------ */
/* Depenetration                                                            */
/* ------------------------------------------------------------------------ */

typedef struct
{
    const gdouble *a;
    const gdouble *b;
    gdouble        r;
    gdouble        depth;      /* deepest penetration found */
    gdouble        push[3];    /* its push direction */
} PenetrationData;

static gboolean
penetration_visit (const LrgCollisionTri *tri,
                   gpointer               user_data)
{
    PenetrationData *pd;
    gdouble cs[3];
    gdouble ct[3];
    gdouble d;
    gdouble n[3];

    pd = (PenetrationData *) user_data;
    d = lrg_collision_segment_triangle (pd->a, pd->b, tri->v, cs, ct);
    if (!(d < pd->r - 1e-7) || pd->r - d <= pd->depth)
        return TRUE;

    if (d > 1e-9)
    {
        n[0] = (cs[0] - ct[0]) / d;
        n[1] = (cs[1] - ct[1]) / d;
        n[2] = (cs[2] - ct[2]) / d;
    }
    else
    {
        gdouble mid[3];
        gdouble centroid[3];
        gdouble side;

        /* the axis crosses the triangle: leave towards the axis middle */
        lrg_collision_triangle_normal (tri->v, n);
        mid[0] = (pd->a[0] + pd->b[0]) * 0.5;
        mid[1] = (pd->a[1] + pd->b[1]) * 0.5;
        mid[2] = (pd->a[2] + pd->b[2]) * 0.5;
        centroid[0] = (tri->v[0] + tri->v[3] + tri->v[6]) / 3.0;
        centroid[1] = (tri->v[1] + tri->v[4] + tri->v[7]) / 3.0;
        centroid[2] = (tri->v[2] + tri->v[5] + tri->v[8]) / 3.0;
        side = (mid[0] - centroid[0]) * n[0] + (mid[1] - centroid[1]) * n[1] +
               (mid[2] - centroid[2]) * n[2];
        if (side < 0.0 || (side == 0.0 && n[1] < 0.0))
        {
            n[0] = -n[0];
            n[1] = -n[1];
            n[2] = -n[2];
        }
    }
    pd->depth = pd->r - d;
    memcpy (pd->push, n, sizeof (n));
    return TRUE;
}

/* Pushes pos out to the skin distance from geometry, deepest contact first. */
static void
depenetrate (LrgCharacterController *self,
             gdouble                *pos)
{
    guint iter;

    if (self->world == NULL)
        return;

    for (iter = 0; iter < DEPENETRATE_ITERATIONS; iter++)
    {
        PenetrationData pd;
        gdouble a[3];
        gdouble b[3];
        gdouble qmin[3];
        gdouble qmax[3];
        gdouble amount;
        guint k;

        axis_points (self, pos, self->height, a, b);
        for (k = 0; k < 3; k++)
        {
            qmin[k] = MIN (a[k], b[k]) - self->radius - self->skin;
            qmax[k] = MAX (a[k], b[k]) + self->radius + self->skin;
        }
        memset (&pd, 0, sizeof (pd));
        pd.a = a;
        pd.b = b;
        /* closer than half the skin counts: a capsule resting deeper
         * than its skin would catch the edges of the face it rests on */
        pd.r = self->radius + self->skin * 0.5;
        lrg_collision_mesh_query_box (self->world, qmin, qmax, penetration_visit, &pd);
        if (pd.depth <= 0.0)
            return;

        /* out to the full skin so the next sweeps start in contact */
        amount = pd.depth + self->skin * 0.5;
        for (k = 0; k < 3; k++)
            pos[k] += pd.push[k] * amount;
    }
}

/* ------------------------------------------------------------------------ */
/* Collide and slide                                                        */
/* ------------------------------------------------------------------------ */

typedef struct
{
    gboolean blocked;          /* a non-walkable contact */
    gboolean any;              /* any contact */
    gboolean ground;           /* a walkable face was touched */
    gdouble  ground_normal[3];
    guint64  ground_id;
} SlideResult;

static gdouble
dot3 (const gdouble *a,
      const gdouble *b)
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

/*
 * Moves pos along motion, sliding over contacts. horizontal: steep
 * contacts are flattened into vertical walls (no climbing them);
 * otherwise (the downward pass) the slide stops on walkable ground.
 */
static void
slide (LrgCharacterController *self,
       gdouble                *pos,
       const gdouble          *motion,
       gboolean                horizontal,
       SlideResult            *res)
{
    gdouble rem[3];
    gdouble planes[3][3];
    guint n_planes;
    guint iter;
    guint k;

    memcpy (rem, motion, sizeof (rem));
    n_planes = 0;

    for (iter = 0; iter < SLIDE_ITERATIONS; iter++)
    {
        Contact c;
        gdouble n[3];
        gdouble into;
        gboolean walkable;

        if (sqrt (dot3 (rem, rem)) < SLIDE_MIN_MOTION)
            break;

        if (!sweep (self, pos, rem, &c))
        {
            for (k = 0; k < 3; k++)
                pos[k] += rem[k];
            break;
        }

        /* advance to the contact */
        for (k = 0; k < 3; k++)
        {
            pos[k] += rem[k] * c.fraction;
            rem[k] *= 1.0 - c.fraction;
        }
        res->any = TRUE;
        memcpy (n, c.normal, sizeof (n));
        walkable = c.normal[1] >= self->min_ground_y;

        if (c.face[1] >= self->min_ground_y && !res->ground)
        {
            res->ground = TRUE;
            memcpy (res->ground_normal, c.face, sizeof (c.face));
            res->ground_id = c.instance_id;
        }

        if (horizontal && !walkable)
        {
            gdouble flat;

            /* steep: a vertical wall, never a ramp */
            res->blocked = TRUE;
            flat = sqrt (n[0] * n[0] + n[2] * n[2]);
            if (flat > 1e-6)
            {
                n[0] /= flat;
                n[1] = 0.0;
                n[2] /= flat;
            }
        }
        else if (!horizontal && c.face[1] >= self->min_ground_y)
        {
            /* the downward pass lands */
            break;
        }

        /* remove the part of the motion going into the surface */
        into = dot3 (rem, n);
        if (into < 0.0)
        {
            for (k = 0; k < 3; k++)
                rem[k] -= into * n[k];
        }

        /* two planes: follow their crease; three: stuck */
        if (n_planes >= 1)
        {
            guint p;
            gboolean against;

            against = FALSE;
            for (p = 0; p < n_planes; p++)
            {
                if (dot3 (rem, planes[p]) < -1e-9)
                    against = TRUE;
            }
            if (against && n_planes == 1)
            {
                gdouble crease[3];
                gdouble len;
                gdouble along;

                crease[0] = planes[0][1] * n[2] - planes[0][2] * n[1];
                crease[1] = planes[0][2] * n[0] - planes[0][0] * n[2];
                crease[2] = planes[0][0] * n[1] - planes[0][1] * n[0];
                len = sqrt (dot3 (crease, crease));
                if (len < 1e-9)
                {
                    memset (rem, 0, sizeof (rem));
                }
                else
                {
                    for (k = 0; k < 3; k++)
                        crease[k] /= len;
                    along = dot3 (rem, crease);
                    for (k = 0; k < 3; k++)
                        rem[k] = crease[k] * along;
                }
            }
            else if (against)
            {
                memset (rem, 0, sizeof (rem));
            }
        }
        if (n_planes < 3)
        {
            memcpy (planes[n_planes], n, sizeof (n));
            n_planes++;
        }
    }
}

/* ------------------------------------------------------------------------ */
/* Terrain                                                                  */
/* ------------------------------------------------------------------------ */

static gboolean
terrain_height (LrgCharacterController *self,
                gdouble                 x,
                gdouble                 z,
                gdouble                *out)
{
    gfloat h;

    if (self->height_func == NULL)
        return FALSE;
    h = 0.0f;
    if (!self->height_func ((gfloat) x, (gfloat) z, &h, self->height_data) || !isfinite (h))
        return FALSE;
    *out = h;
    return TRUE;
}

static void
terrain_normal (LrgCharacterController *self,
                gdouble                 x,
                gdouble                 z,
                gdouble                 center,
                gdouble                *n)
{
    gdouble hx;
    gdouble hz;
    gdouble len;

    if (!terrain_height (self, x + TERRAIN_NORMAL_DELTA, z, &hx))
        hx = center;
    if (!terrain_height (self, x, z + TERRAIN_NORMAL_DELTA, &hz))
        hz = center;
    n[0] = -(hx - center) / TERRAIN_NORMAL_DELTA;
    n[1] = 1.0;
    n[2] = -(hz - center) / TERRAIN_NORMAL_DELTA;
    len = sqrt (dot3 (n, n));
    n[0] /= len;
    n[1] /= len;
    n[2] /= len;
}

/* ------------------------------------------------------------------------ */
/* Move                                                                     */
/* ------------------------------------------------------------------------ */

static gdouble
progress_along (const gdouble *from,
                const gdouble *to,
                const gdouble *dir)
{
    return (to[0] - from[0]) * dir[0] + (to[2] - from[2]) * dir[2];
}

gboolean
lrg_character_controller_move (LrgCharacterController *self,
                               const gfloat           *feet,
                               const gfloat           *motion,
                               gboolean                snap_to_ground,
                               LrgCharacterMove       *out_move)
{
    gdouble pos[3];
    gdouble m[3];
    gdouble hdir[3];
    gdouble hlen;
    gboolean grounded;
    gdouble ground_normal[3];
    guint64 ground_id;
    gboolean hit_wall;
    gboolean hit_ceiling;
    gboolean stepped;
    gdouble th;
    guint k;

    g_return_val_if_fail (LRG_IS_CHARACTER_CONTROLLER (self), FALSE);
    g_return_val_if_fail (feet != NULL && motion != NULL && out_move != NULL, FALSE);

    memset (out_move, 0, sizeof (*out_move));
    out_move->ground_normal[1] = 1.0f;
    for (k = 0; k < 3; k++)
        out_move->position[k] = feet[k];
    for (k = 0; k < 3; k++)
    {
        if (!isfinite (feet[k]) || !isfinite (motion[k]))
            return FALSE;
        pos[k] = feet[k];
        m[k] = motion[k];
    }

    grounded = FALSE;
    ground_normal[0] = 0.0;
    ground_normal[1] = 1.0;
    ground_normal[2] = 0.0;
    ground_id = 0;
    hit_wall = FALSE;
    hit_ceiling = FALSE;
    stepped = FALSE;

    /* 1. out of anything already penetrated */
    depenetrate (self, pos);

    /* 2. up */
    if (m[1] > 0.0)
    {
        gdouble up[3];
        Contact c;

        up[0] = 0.0;
        up[1] = m[1];
        up[2] = 0.0;
        if (sweep (self, pos, up, &c))
        {
            pos[1] += m[1] * c.fraction;
            hit_ceiling = TRUE;
        }
        else
        {
            pos[1] += m[1];
        }
    }

    /* 3. horizontal, with a stepped retry when blocked */
    hlen = sqrt (m[0] * m[0] + m[2] * m[2]);
    if (hlen > SLIDE_MIN_MOTION)
    {
        gdouble h[3];
        gdouble start[3];
        gdouble a[3];
        SlideResult ra;

        h[0] = m[0];
        h[1] = 0.0;
        h[2] = m[2];
        hdir[0] = m[0] / hlen;
        hdir[1] = 0.0;
        hdir[2] = m[2] / hlen;
        memcpy (start, pos, sizeof (start));
        memcpy (a, pos, sizeof (a));
        memset (&ra, 0, sizeof (ra));
        slide (self, a, h, TRUE, &ra);
        memcpy (pos, a, sizeof (pos));
        hit_wall = ra.blocked;

        if (ra.blocked && self->step_height > 0.0 && self->world != NULL)
        {
            gdouble b[3];
            gdouble up[3];
            gdouble down[3];
            gdouble raised;
            Contact c;
            SlideResult rb;

            /* raise by the step height (less under a low ceiling) */
            memcpy (b, start, sizeof (b));
            up[0] = 0.0;
            up[1] = self->step_height;
            up[2] = 0.0;
            raised = sweep (self, b, up, &c) ? self->step_height * c.fraction : self->step_height;
            b[1] += raised;

            /* the same horizontal move up there */
            memset (&rb, 0, sizeof (rb));
            slide (self, b, h, TRUE, &rb);

            /* and back down onto walkable ground no higher than a step
             * (a sphere perched on a taller edge touches it from the side) */
            down[0] = 0.0;
            down[1] = -(raised + self->step_height);
            down[2] = 0.0;
            if (raised > 0.0 && sweep (self, b, down, &c) && c.face[1] >= self->min_ground_y &&
                c.point[1] - start[1] <= self->step_height + 1e-4)
            {
                b[1] += down[1] * c.fraction;
                if (progress_along (start, b, hdir) > progress_along (start, a, hdir) + STEP_MIN_GAIN)
                {
                    memcpy (pos, b, sizeof (pos));
                    stepped = b[1] > start[1] + 1e-4;
                    hit_wall = rb.blocked;
                    grounded = TRUE;
                    memcpy (ground_normal, c.face, sizeof (c.face));
                    ground_id = c.instance_id;
                }
            }
        }
    }

    /* 4. down, sliding off steep surfaces */
    if (m[1] < 0.0)
    {
        gdouble down[3];
        SlideResult rd;

        down[0] = 0.0;
        down[1] = m[1];
        down[2] = 0.0;
        memset (&rd, 0, sizeof (rd));
        slide (self, pos, down, FALSE, &rd);
        if (rd.ground)
        {
            grounded = TRUE;
            memcpy (ground_normal, rd.ground_normal, sizeof (ground_normal));
            ground_id = rd.ground_id;
        }
        else
        {
            grounded = FALSE;
        }
    }

    /* 5a. snap down to ground within the step height */
    if (snap_to_ground && m[1] <= 0.0 && !grounded && self->step_height > 0.0 &&
        self->world != NULL)
    {
        gdouble down[3];
        Contact c;

        down[0] = 0.0;
        down[1] = -self->step_height;
        down[2] = 0.0;
        if (sweep (self, pos, down, &c) && c.face[1] >= self->min_ground_y)
        {
            pos[1] += down[1] * c.fraction;
            grounded = TRUE;
            memcpy (ground_normal, c.face, sizeof (c.face));
            ground_id = c.instance_id;
        }
    }

    /* 5b. terrain is a floor nothing passes */
    if (terrain_height (self, pos[0], pos[2], &th))
    {
        gboolean on_terrain;

        on_terrain = FALSE;
        if (pos[1] < th)
        {
            pos[1] = th;
            on_terrain = TRUE;
        }
        else if (!grounded && m[1] <= 0.0 &&
                 (pos[1] - th <= 2.0 * self->skin ||
                  (snap_to_ground && pos[1] - th <= self->step_height)))
        {
            pos[1] = th;
            on_terrain = TRUE;
        }
        if (on_terrain)
        {
            grounded = TRUE;
            terrain_normal (self, pos[0], pos[2], th, ground_normal);
            ground_id = 0;
        }
    }

    /* 5c. ground probe: resting within the skin of a walkable face */
    if (!grounded && m[1] <= 0.0 && self->world != NULL)
    {
        gdouble down[3];
        Contact c;

        down[0] = 0.0;
        down[1] = -(2.0 * self->skin + 1e-3);
        down[2] = 0.0;
        if (sweep (self, pos, down, &c) && c.face[1] >= self->min_ground_y)
        {
            grounded = TRUE;
            memcpy (ground_normal, c.face, sizeof (c.face));
            ground_id = c.instance_id;
        }
    }

    for (k = 0; k < 3; k++)
    {
        out_move->position[k] = (gfloat) pos[k];
        out_move->ground_normal[k] = (gfloat) (grounded ? ground_normal[k] : (k == 1 ? 1.0 : 0.0));
    }
    out_move->grounded = grounded;
    out_move->ground_instance_id = grounded ? ground_id : 0;
    out_move->hit_wall = hit_wall;
    out_move->hit_ceiling = hit_ceiling;
    out_move->stepped = stepped;
    return TRUE;
}

gboolean
lrg_character_controller_can_stand (LrgCharacterController *self,
                                    const gfloat           *feet,
                                    gfloat                  height)
{
    gfloat base[3];

    g_return_val_if_fail (LRG_IS_CHARACTER_CONTROLLER (self), FALSE);
    g_return_val_if_fail (feet != NULL, FALSE);

    if (self->world == NULL)
        return TRUE;
    if (!isfinite (height))
        return FALSE;

    /* the skin is allowed: test the bare capsule */
    base[0] = feet[0];
    base[1] = feet[1];
    base[2] = feet[2];
    return !lrg_collision_mesh_overlap_capsule (self->world, base, (gfloat) self->radius,
                                                MAX (height, (gfloat) (2.0 * self->radius)));
}
