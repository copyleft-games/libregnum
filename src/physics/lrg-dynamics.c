/* lrg-dynamics.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Bounded 3D rigid motion. Y is up. A dynamic body integrates force,
 * gravity and torque with a diagonal body-frame inertia, then a sequential
 * impulse solve handles sphere contacts, caller-axis sphyl contacts and
 * joint limits. A sphyl axis is the caller's unit vector, not the collision
 * library's local +Y capsule. Box half extents and an explicit diagonal
 * never become a contact shape. A contactless body generates no contacts.
 * Nothing in this file reports a ragdoll or reads Karma bytes.
 */

#include "config.h"

#include <math.h>
#include <stdarg.h>
#include <string.h>

#ifndef LIBREGNUM_COMPILATION
#define LIBREGNUM_COMPILATION
#endif
#include "physics/lrg-dynamics.h"
#include "../animation/lrg-bone-pose-private.h"

#define DYN_MAX_CONTACTS (256u)
#define DYN_SLOP (0.005f)
#define DYN_BAUMGARTE (0.2f)
#define DYN_POSITION (0.6f)
#define DYN_PI (3.14159265358979323846f)

typedef enum {
    DYN_SHAPE_SPHERE = 0,
    DYN_SHAPE_PLANE = 1,
    DYN_SHAPE_SPHYL = 2,
    DYN_SHAPE_INERTIAL = 3
} DynShape;

typedef struct
{
    gboolean alive;
    gboolean frozen;
    gboolean sleeping;
    guint quiet;
    LrgDynamicsBodyKind kind;
    gboolean plane;
    DynShape shape;
    gfloat axis[3];
    gfloat half_length;
    gfloat mass;
    gfloat inv_mass;
    gfloat inertia[3];
    gfloat inv_inertia[3];
    gfloat radius;
    gfloat plane_normal[3];
    gfloat plane_offset;
    gfloat position[3];
    gfloat orientation[4];
    gfloat linear_velocity[3];
    gfloat angular_velocity[3];
    gfloat force[3];
    gfloat torque[3];
} DynBody;

typedef struct
{
    guint body_a;
    guint body_b;
    gfloat anchor_a[3];
    gfloat axis_a[3];
    gfloat anchor_b[3];
    gfloat axis_b[3];
    gfloat linear_min;
    gfloat linear_max;
    gfloat angular_min;
    gfloat angular_max;
} DynJoint;

typedef struct
{
    guint body_a;
    guint body_b;
    gfloat point[3];
    gfloat normal[3];
    gfloat depth;
    LrgDynamicsContactKind kind;
} DynContact;

struct _LrgDynamicsWorld
{
    GObject parent_instance;
    DynBody bodies[LRG_DYNAMICS_MAX_BODIES];
    guint n_bodies;
    DynJoint joints[LRG_DYNAMICS_MAX_JOINTS];
    guint n_joints;
    DynContact contacts[DYN_MAX_CONTACTS];
    guint n_contacts;
    guint evicted[LRG_DYNAMICS_MAX_BODIES];
    guint n_evicted;
    gfloat gravity[3];
    gfloat fixed_dt;
    guint max_substeps;
    gfloat accumulator;
    gfloat dropped;
    gfloat sleep_speed2;
    guint sleep_after;
    guint iterations;
    gfloat restitution;
    gfloat friction;
    gfloat bounds_min[3];
    gfloat bounds_max[3];
    gboolean has_bounds;
};

G_DEFINE_TYPE (LrgDynamicsWorld, lrg_dynamics_world, G_TYPE_OBJECT)

G_DEFINE_QUARK (lrg-dynamics-error-quark, lrg_dynamics_error)

static GType
register_enum (const gchar *name, const GEnumValue *values)
{
    static gsize guard_kind = 0;
    static gsize guard_contact = 0;
    gsize *guard;
    GType registered;

    guard = (strcmp (name, "LrgDynamicsBodyKind") == 0) ? &guard_kind : &guard_contact;
    if (g_once_init_enter (guard))
    {
        registered = g_enum_register_static (name, values);
        g_once_init_leave (guard, (gsize) registered);
    }
    return (GType) *guard;
}

GType
lrg_dynamics_body_kind_get_type (void)
{
    static const GEnumValue values[] = {
        { LRG_DYNAMICS_BODY_DYNAMIC, "LRG_DYNAMICS_BODY_DYNAMIC", "dynamic" },
        { LRG_DYNAMICS_BODY_KINEMATIC, "LRG_DYNAMICS_BODY_KINEMATIC", "kinematic" },
        { LRG_DYNAMICS_BODY_STATIC, "LRG_DYNAMICS_BODY_STATIC", "static" },
        { 0, NULL, NULL }
    };

    return register_enum ("LrgDynamicsBodyKind", values);
}

GType
lrg_dynamics_contact_kind_get_type (void)
{
    static const GEnumValue values[] = {
        { LRG_DYNAMICS_CONTACT_SPHERE_SPHERE, "LRG_DYNAMICS_CONTACT_SPHERE_SPHERE", "sphere-sphere" },
        { LRG_DYNAMICS_CONTACT_SPHERE_PLANE, "LRG_DYNAMICS_CONTACT_SPHERE_PLANE", "sphere-plane" },
        { LRG_DYNAMICS_CONTACT_SPHYL_SPHERE, "LRG_DYNAMICS_CONTACT_SPHYL_SPHERE", "sphyl-sphere" },
        { LRG_DYNAMICS_CONTACT_SPHYL_PLANE, "LRG_DYNAMICS_CONTACT_SPHYL_PLANE", "sphyl-plane" },
        { LRG_DYNAMICS_CONTACT_SPHYL_SPHYL, "LRG_DYNAMICS_CONTACT_SPHYL_SPHYL", "sphyl-sphyl" },
        { 0, NULL, NULL }
    };

    return register_enum ("LrgDynamicsContactKind", values);
}

static gboolean
dyn_error (GError          **error,
           LrgDynamicsError  code,
           const gchar      *format,
           ...)
{
    va_list args;
    gchar *message;

    va_start (args, format);
    message = g_strdup_vprintf (format, args);
    va_end (args);
    g_set_error_literal (error, LRG_DYNAMICS_ERROR, (gint) code, message);
    g_free (message);
    return FALSE;
}

static gboolean
finite1 (gfloat value)
{
    return isfinite ((double) value) ? TRUE : FALSE;
}

static gboolean
finite3 (const gfloat *value)
{
    return finite1 (value[0]) && finite1 (value[1]) && finite1 (value[2]);
}

static gfloat
dot3 (const gfloat *a, const gfloat *b)
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

static gfloat
len3 (const gfloat *value)
{
    return sqrtf (dot3 (value, value));
}

static void
cross3 (const gfloat *a, const gfloat *b, gfloat *out)
{
    out[0] = a[1] * b[2] - a[2] * b[1];
    out[1] = a[2] * b[0] - a[0] * b[2];
    out[2] = a[0] * b[1] - a[1] * b[0];
}

static void
quat_mul (const gfloat *a, const gfloat *b, gfloat *out)
{
    out[3] = a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2];
    out[0] = a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1];
    out[1] = a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0];
    out[2] = a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3];
}

static void
quat_rotate (const gfloat *q, const gfloat *v, gfloat *out)
{
    gfloat tx;
    gfloat ty;
    gfloat tz;

    tx = 2.0f * (q[1] * v[2] - q[2] * v[1]);
    ty = 2.0f * (q[2] * v[0] - q[0] * v[2]);
    tz = 2.0f * (q[0] * v[1] - q[1] * v[0]);
    out[0] = v[0] + q[3] * tx + (q[1] * tz - q[2] * ty);
    out[1] = v[1] + q[3] * ty + (q[2] * tx - q[0] * tz);
    out[2] = v[2] + q[3] * tz + (q[0] * ty - q[1] * tx);
}

static void
quat_inv_rotate (const gfloat *q, const gfloat *v, gfloat *out)
{
    gfloat conj[4];

    conj[0] = -q[0];
    conj[1] = -q[1];
    conj[2] = -q[2];
    conj[3] = q[3];
    quat_rotate (conj, v, out);
}

static void
quat_normalize (gfloat *q)
{
    gfloat len;

    len = sqrtf (q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
    if (len > 1.0e-8f)
    {
        q[0] /= len;
        q[1] /= len;
        q[2] /= len;
        q[3] /= len;
    }
    else
    {
        q[0] = 0.0f;
        q[1] = 0.0f;
        q[2] = 0.0f;
        q[3] = 1.0f;
    }
}

static void
wake_body (DynBody *body)
{
    body->sleeping = FALSE;
    body->quiet = 0;
}

static gboolean
dynamic_responsive (const DynBody *body)
{
    if (body == NULL || !body->alive || body->frozen)
        return FALSE;
    if (body->plane || body->kind != LRG_DYNAMICS_BODY_DYNAMIC)
        return FALSE;
    return TRUE;
}

static DynBody *
body_known (LrgDynamicsWorld *self, guint id, GError **error)
{
    if (id >= self->n_bodies)
    {
        dyn_error (error, LRG_DYNAMICS_ERROR_MISSING,
                   "dynamics body %u was never added", id);
        return NULL;
    }
    return &self->bodies[id];
}

static DynBody *
sphere_known (LrgDynamicsWorld *self, guint id, GError **error)
{
    DynBody *body;

    body = body_known (self, id, error);
    if (body == NULL)
        return NULL;
    if (body->plane)
    {
        dyn_error (error, LRG_DYNAMICS_ERROR_STATE,
                   "dynamics body %u is a plane", id);
        return NULL;
    }
    return body;
}

static void
apply_inv_inertia (const DynBody *body, const gfloat *torque, gfloat *out)
{
    gfloat local_torque[3];
    gfloat local_omega[3];

    if (!dynamic_responsive (body))
    {
        out[0] = 0.0f;
        out[1] = 0.0f;
        out[2] = 0.0f;
        return;
    }
    quat_inv_rotate (body->orientation, torque, local_torque);
    local_omega[0] = body->inv_inertia[0] * local_torque[0];
    local_omega[1] = body->inv_inertia[1] * local_torque[1];
    local_omega[2] = body->inv_inertia[2] * local_torque[2];
    quat_rotate (body->orientation, local_omega, out);
}

static void
apply_linear_impulse (DynBody *body, const gfloat *impulse)
{
    if (!dynamic_responsive (body))
        return;
    wake_body (body);
    body->linear_velocity[0] += impulse[0] * body->inv_mass;
    body->linear_velocity[1] += impulse[1] * body->inv_mass;
    body->linear_velocity[2] += impulse[2] * body->inv_mass;
}

static void
apply_angular_impulse (DynBody *body, const gfloat *impulse)
{
    gfloat delta[3];

    if (!dynamic_responsive (body))
        return;
    apply_inv_inertia (body, impulse, delta);
    wake_body (body);
    body->angular_velocity[0] += delta[0];
    body->angular_velocity[1] += delta[1];
    body->angular_velocity[2] += delta[2];
}

static void
apply_at_point (DynBody *body, const gfloat *impulse, const gfloat *point)
{
    gfloat offset[3];
    gfloat angular[3];

    if (body == NULL)
        return;
    offset[0] = point[0] - body->position[0];
    offset[1] = point[1] - body->position[1];
    offset[2] = point[2] - body->position[2];
    cross3 (offset, impulse, angular);
    apply_linear_impulse (body, impulse);
    apply_angular_impulse (body, angular);
}

static gfloat
inv_mass_along (const DynBody *body, const gfloat *point, const gfloat *dir)
{
    gfloat offset[3];
    gfloat moment[3];
    gfloat spin[3];
    gfloat tangential[3];

    if (!dynamic_responsive (body))
        return 0.0f;
    offset[0] = point[0] - body->position[0];
    offset[1] = point[1] - body->position[1];
    offset[2] = point[2] - body->position[2];
    cross3 (offset, dir, moment);
    apply_inv_inertia (body, moment, spin);
    cross3 (spin, offset, tangential);
    return body->inv_mass + dot3 (tangential, dir);
}

static void
point_velocity (const DynBody *body, const gfloat *point, gfloat *out)
{
    gfloat offset[3];
    gfloat tangential[3];

    if (!dynamic_responsive (body) &&
        (body == NULL || body->frozen || !body->alive || body->plane ||
         body->kind == LRG_DYNAMICS_BODY_STATIC))
    {
        out[0] = 0.0f;
        out[1] = 0.0f;
        out[2] = 0.0f;
        return;
    }
    offset[0] = point[0] - body->position[0];
    offset[1] = point[1] - body->position[1];
    offset[2] = point[2] - body->position[2];
    cross3 (body->angular_velocity, offset, tangential);
    out[0] = body->linear_velocity[0] + tangential[0];
    out[1] = body->linear_velocity[1] + tangential[1];
    out[2] = body->linear_velocity[2] + tangential[2];
}

static void
integrate_body (DynBody *body, const gfloat *gravity, gfloat dt)
{
    gfloat omega[3];
    gfloat omega_q[4];
    gfloat delta_q[4];
    gfloat next_q[4];

    if (!body->alive || body->plane)
        return;
    if (body->kind == LRG_DYNAMICS_BODY_DYNAMIC && !body->frozen && !body->sleeping)
    {
        body->linear_velocity[0] += (body->force[0] * body->inv_mass + gravity[0]) * dt;
        body->linear_velocity[1] += (body->force[1] * body->inv_mass + gravity[1]) * dt;
        body->linear_velocity[2] += (body->force[2] * body->inv_mass + gravity[2]) * dt;
        apply_inv_inertia (body, body->torque, omega);
        body->angular_velocity[0] += omega[0] * dt;
        body->angular_velocity[1] += omega[1] * dt;
        body->angular_velocity[2] += omega[2] * dt;
    }
    if (!body->frozen && !body->sleeping && body->kind != LRG_DYNAMICS_BODY_STATIC)
    {
        body->position[0] += body->linear_velocity[0] * dt;
        body->position[1] += body->linear_velocity[1] * dt;
        body->position[2] += body->linear_velocity[2] * dt;
        omega_q[0] = body->angular_velocity[0];
        omega_q[1] = body->angular_velocity[1];
        omega_q[2] = body->angular_velocity[2];
        omega_q[3] = 0.0f;
        quat_mul (omega_q, body->orientation, delta_q);
        next_q[0] = body->orientation[0] + 0.5f * delta_q[0] * dt;
        next_q[1] = body->orientation[1] + 0.5f * delta_q[1] * dt;
        next_q[2] = body->orientation[2] + 0.5f * delta_q[2] * dt;
        next_q[3] = body->orientation[3] + 0.5f * delta_q[3] * dt;
        quat_normalize (next_q);
        body->orientation[0] = next_q[0];
        body->orientation[1] = next_q[1];
        body->orientation[2] = next_q[2];
        body->orientation[3] = next_q[3];
    }
    body->force[0] = 0.0f;
    body->force[1] = 0.0f;
    body->force[2] = 0.0f;
    body->torque[0] = 0.0f;
    body->torque[1] = 0.0f;
    body->torque[2] = 0.0f;
}

static void
add_contact (LrgDynamicsWorld *self, const DynContact *contact)
{
    if (self->n_contacts >= DYN_MAX_CONTACTS)
        return;
    self->contacts[self->n_contacts] = *contact;
    self->n_contacts++;
}

static void
generate_sphere_pair (LrgDynamicsWorld *self, guint ia, guint ib)
{
    DynBody *a;
    DynBody *b;
    DynContact contact;
    gfloat delta[3];
    gfloat dist;
    gfloat reach;

    a = &self->bodies[ia];
    b = &self->bodies[ib];
    if (!a->alive || !b->alive ||
        a->shape != DYN_SHAPE_SPHERE || b->shape != DYN_SHAPE_SPHERE)
        return;
    delta[0] = a->position[0] - b->position[0];
    delta[1] = a->position[1] - b->position[1];
    delta[2] = a->position[2] - b->position[2];
    dist = len3 (delta);
    reach = a->radius + b->radius;
    if (dist >= reach)
        return;
    memset (&contact, 0, sizeof (contact));
    contact.body_a = ia;
    contact.body_b = ib;
    contact.kind = LRG_DYNAMICS_CONTACT_SPHERE_SPHERE;
    contact.depth = reach - dist;
    if (dist > 1.0e-6f)
    {
        contact.normal[0] = delta[0] / dist;
        contact.normal[1] = delta[1] / dist;
        contact.normal[2] = delta[2] / dist;
    }
    else
    {
        contact.normal[0] = 1.0f;
        contact.normal[1] = 0.0f;
        contact.normal[2] = 0.0f;
    }
    contact.point[0] = a->position[0] - contact.normal[0] * a->radius;
    contact.point[1] = a->position[1] - contact.normal[1] * a->radius;
    contact.point[2] = a->position[2] - contact.normal[2] * a->radius;
    add_contact (self, &contact);
}

static void
generate_plane (LrgDynamicsWorld *self, guint sphere, guint plane)
{
    DynBody *body;
    DynBody *wall;
    DynContact contact;
    gfloat signed_distance;

    body = &self->bodies[sphere];
    wall = &self->bodies[plane];
    if (!body->alive || !wall->alive ||
        body->shape != DYN_SHAPE_SPHERE || wall->shape != DYN_SHAPE_PLANE)
        return;
    signed_distance = dot3 (wall->plane_normal, body->position) - wall->plane_offset;
    if (signed_distance >= body->radius)
        return;
    memset (&contact, 0, sizeof (contact));
    contact.body_a = sphere;
    contact.body_b = plane;
    contact.kind = LRG_DYNAMICS_CONTACT_SPHERE_PLANE;
    contact.depth = body->radius - signed_distance;
    contact.normal[0] = wall->plane_normal[0];
    contact.normal[1] = wall->plane_normal[1];
    contact.normal[2] = wall->plane_normal[2];
    contact.point[0] = body->position[0] - wall->plane_normal[0] * signed_distance;
    contact.point[1] = body->position[1] - wall->plane_normal[1] * signed_distance;
    contact.point[2] = body->position[2] - wall->plane_normal[2] * signed_distance;
    add_contact (self, &contact);
}

static gfloat
clampf (gfloat value, gfloat low, gfloat high)
{
    if (value < low)
        return low;
    if (value > high)
        return high;
    return value;
}

static void
push_contact (LrgDynamicsWorld       *self,
              guint                   body_a,
              guint                   body_b,
              LrgDynamicsContactKind  kind,
              const gfloat           *point,
              const gfloat           *normal,
              gfloat                  depth)
{
    DynContact contact;

    if (!(depth > 0.0f))
        return;
    memset (&contact, 0, sizeof (contact));
    contact.body_a = body_a;
    contact.body_b = body_b;
    contact.kind = kind;
    contact.depth = depth;
    contact.point[0] = point[0];
    contact.point[1] = point[1];
    contact.point[2] = point[2];
    contact.normal[0] = normal[0];
    contact.normal[1] = normal[1];
    contact.normal[2] = normal[2];
    add_contact (self, &contact);
}

static void
unit_or_fallback (const gfloat *delta, gfloat *normal)
{
    gfloat dist;

    dist = len3 (delta);
    if (dist > 1.0e-6f)
    {
        normal[0] = delta[0] / dist;
        normal[1] = delta[1] / dist;
        normal[2] = delta[2] / dist;
        return;
    }
    normal[0] = 1.0f;
    normal[1] = 0.0f;
    normal[2] = 0.0f;
}

/* World segment of a sphyl. The stored axis is body-local and already unit. */
static void
sphyl_segment (const DynBody *body, gfloat *end0, gfloat *end1)
{
    gfloat axis[3];

    quat_rotate (body->orientation, body->axis, axis);
    end0[0] = body->position[0] - axis[0] * body->half_length;
    end0[1] = body->position[1] - axis[1] * body->half_length;
    end0[2] = body->position[2] - axis[2] * body->half_length;
    end1[0] = body->position[0] + axis[0] * body->half_length;
    end1[1] = body->position[1] + axis[1] * body->half_length;
    end1[2] = body->position[2] + axis[2] * body->half_length;
}

static void
closest_on_segment (const gfloat *end0, const gfloat *end1,
                    const gfloat *point, gfloat *closest)
{
    gfloat edge[3];
    gfloat offset[3];
    gfloat length2;
    gfloat t;

    edge[0] = end1[0] - end0[0];
    edge[1] = end1[1] - end0[1];
    edge[2] = end1[2] - end0[2];
    offset[0] = point[0] - end0[0];
    offset[1] = point[1] - end0[1];
    offset[2] = point[2] - end0[2];
    length2 = dot3 (edge, edge);
    if (length2 <= 1.0e-12f)
    {
        closest[0] = end0[0];
        closest[1] = end0[1];
        closest[2] = end0[2];
        return;
    }
    t = clampf (dot3 (offset, edge) / length2, 0.0f, 1.0f);
    closest[0] = end0[0] + edge[0] * t;
    closest[1] = end0[1] + edge[1] * t;
    closest[2] = end0[2] + edge[2] * t;
}

/*
 * Closest points on two segments. Parallel segments use an endpoint
 * projection, which still yields the perpendicular distance.
 */
static void
closest_segments (const gfloat *p0, const gfloat *p1,
                  const gfloat *q0, const gfloat *q1,
                  gfloat *c0, gfloat *c1)
{
    gfloat d0[3];
    gfloat d1[3];
    gfloat r[3];
    gfloat a;
    gfloat e;
    gfloat f;
    gfloat c;
    gfloat b;
    gfloat denom;
    gfloat s;
    gfloat t;

    d0[0] = p1[0] - p0[0];
    d0[1] = p1[1] - p0[1];
    d0[2] = p1[2] - p0[2];
    d1[0] = q1[0] - q0[0];
    d1[1] = q1[1] - q0[1];
    d1[2] = q1[2] - q0[2];
    r[0] = p0[0] - q0[0];
    r[1] = p0[1] - q0[1];
    r[2] = p0[2] - q0[2];
    a = dot3 (d0, d0);
    e = dot3 (d1, d1);
    f = dot3 (d1, r);
    b = dot3 (d0, d1);
    c = dot3 (d0, r);
    s = 0.0f;
    t = 0.0f;
    if (a <= 1.0e-12f && e <= 1.0e-12f)
    {
        c0[0] = p0[0];
        c0[1] = p0[1];
        c0[2] = p0[2];
        c1[0] = q0[0];
        c1[1] = q0[1];
        c1[2] = q0[2];
        return;
    }
    if (a <= 1.0e-12f)
    {
        s = 0.0f;
        t = (e > 1.0e-12f) ? clampf (f / e, 0.0f, 1.0f) : 0.0f;
    }
    else if (e <= 1.0e-12f)
    {
        t = 0.0f;
        s = clampf (-c / a, 0.0f, 1.0f);
    }
    else
    {
        denom = a * e - b * b;
        if (denom > 1.0e-12f)
            s = clampf ((b * f - c * e) / denom, 0.0f, 1.0f);
        else
            s = 0.0f;
        t = (b * s + f) / e;
        if (t < 0.0f)
        {
            t = 0.0f;
            s = clampf (-c / a, 0.0f, 1.0f);
        }
        else if (t > 1.0f)
        {
            t = 1.0f;
            s = clampf ((b - c) / a, 0.0f, 1.0f);
        }
    }
    c0[0] = p0[0] + d0[0] * s;
    c0[1] = p0[1] + d0[1] * s;
    c0[2] = p0[2] + d0[2] * s;
    c1[0] = q0[0] + d1[0] * t;
    c1[1] = q0[1] + d1[1] * t;
    c1[2] = q0[2] + d1[2] * t;
}

/* Body A is the sphere. The normal pushes that sphere off the capsule. */
static void
generate_sphyl_sphere (LrgDynamicsWorld *self, guint sphere, guint sphyl)
{
    DynBody *ball;
    DynBody *cap;
    gfloat end0[3];
    gfloat end1[3];
    gfloat closest[3];
    gfloat delta[3];
    gfloat normal[3];
    gfloat point[3];
    gfloat dist;
    gfloat reach;

    ball = &self->bodies[sphere];
    cap = &self->bodies[sphyl];
    if (!ball->alive || !cap->alive ||
        ball->shape != DYN_SHAPE_SPHERE || cap->shape != DYN_SHAPE_SPHYL)
        return;
    sphyl_segment (cap, end0, end1);
    closest_on_segment (end0, end1, ball->position, closest);
    delta[0] = ball->position[0] - closest[0];
    delta[1] = ball->position[1] - closest[1];
    delta[2] = ball->position[2] - closest[2];
    dist = len3 (delta);
    reach = ball->radius + cap->radius;
    if (dist >= reach)
        return;
    unit_or_fallback (delta, normal);
    point[0] = ball->position[0] - normal[0] * ball->radius;
    point[1] = ball->position[1] - normal[1] * ball->radius;
    point[2] = ball->position[2] - normal[2] * ball->radius;
    push_contact (self, sphere, sphyl, LRG_DYNAMICS_CONTACT_SPHYL_SPHERE,
                  point, normal, reach - dist);
}

/* The deepest capsule end against an infinite plane. The normal pushes the sphyl. */
static void
generate_sphyl_plane (LrgDynamicsWorld *self, guint sphyl, guint plane)
{
    DynBody *cap;
    DynBody *wall;
    gfloat end0[3];
    gfloat end1[3];
    gfloat *end;
    gfloat signed0;
    gfloat signed1;
    gfloat signed_distance;
    gfloat point[3];
    gfloat normal[3];

    cap = &self->bodies[sphyl];
    wall = &self->bodies[plane];
    if (!cap->alive || !wall->alive ||
        cap->shape != DYN_SHAPE_SPHYL || wall->shape != DYN_SHAPE_PLANE)
        return;
    sphyl_segment (cap, end0, end1);
    signed0 = dot3 (wall->plane_normal, end0) - wall->plane_offset;
    signed1 = dot3 (wall->plane_normal, end1) - wall->plane_offset;
    if (signed0 <= signed1)
    {
        end = end0;
        signed_distance = signed0;
    }
    else
    {
        end = end1;
        signed_distance = signed1;
    }
    if (signed_distance >= cap->radius)
        return;
    normal[0] = wall->plane_normal[0];
    normal[1] = wall->plane_normal[1];
    normal[2] = wall->plane_normal[2];
    point[0] = end[0] - normal[0] * signed_distance;
    point[1] = end[1] - normal[1] * signed_distance;
    point[2] = end[2] - normal[2] * signed_distance;
    push_contact (self, sphyl, plane, LRG_DYNAMICS_CONTACT_SPHYL_PLANE,
                  point, normal, cap->radius - signed_distance);
}

/* Body A's normal points from B's closest point toward A's. */
static void
generate_sphyl_pair (LrgDynamicsWorld *self, guint ia, guint ib)
{
    DynBody *a;
    DynBody *b;
    gfloat a0[3];
    gfloat a1[3];
    gfloat b0[3];
    gfloat b1[3];
    gfloat ca[3];
    gfloat cb[3];
    gfloat delta[3];
    gfloat normal[3];
    gfloat point[3];
    gfloat dist;
    gfloat reach;

    a = &self->bodies[ia];
    b = &self->bodies[ib];
    if (!a->alive || !b->alive ||
        a->shape != DYN_SHAPE_SPHYL || b->shape != DYN_SHAPE_SPHYL)
        return;
    sphyl_segment (a, a0, a1);
    sphyl_segment (b, b0, b1);
    closest_segments (a0, a1, b0, b1, ca, cb);
    delta[0] = ca[0] - cb[0];
    delta[1] = ca[1] - cb[1];
    delta[2] = ca[2] - cb[2];
    dist = len3 (delta);
    reach = a->radius + b->radius;
    if (dist >= reach)
        return;
    unit_or_fallback (delta, normal);
    point[0] = ca[0] - normal[0] * a->radius;
    point[1] = ca[1] - normal[1] * a->radius;
    point[2] = ca[2] - normal[2] * a->radius;
    push_contact (self, ia, ib, LRG_DYNAMICS_CONTACT_SPHYL_SPHYL,
                  point, normal, reach - dist);
}

static void
generate_contacts (LrgDynamicsWorld *self)
{
    guint i;
    guint j;
    DynShape left;
    DynShape right;

    self->n_contacts = 0;
    for (i = 0; i < self->n_bodies; i++)
    {
        for (j = i + 1; j < self->n_bodies; j++)
        {
            left = self->bodies[i].shape;
            right = self->bodies[j].shape;
            if (left == DYN_SHAPE_SPHERE && right == DYN_SHAPE_SPHERE)
                generate_sphere_pair (self, i, j);
            else if (left == DYN_SHAPE_SPHERE && right == DYN_SHAPE_SPHYL)
                generate_sphyl_sphere (self, i, j);
            else if (left == DYN_SHAPE_SPHYL && right == DYN_SHAPE_SPHERE)
                generate_sphyl_sphere (self, j, i);
            else if (left == DYN_SHAPE_SPHYL && right == DYN_SHAPE_SPHYL)
                generate_sphyl_pair (self, i, j);
        }
    }
    for (i = 0; i < self->n_bodies; i++)
    {
        if (self->bodies[i].shape == DYN_SHAPE_PLANE ||
            self->bodies[i].shape == DYN_SHAPE_INERTIAL)
            continue;
        for (j = 0; j < self->n_bodies; j++)
        {
            if (self->bodies[j].shape != DYN_SHAPE_PLANE)
                continue;
            if (self->bodies[i].shape == DYN_SHAPE_SPHERE)
                generate_plane (self, i, j);
            else if (self->bodies[i].shape == DYN_SHAPE_SPHYL)
                generate_sphyl_plane (self, i, j);
        }
    }
}

static void
solve_contact (LrgDynamicsWorld *self, DynContact *contact, gfloat dt)
{
    DynBody *a;
    DynBody *b;
    gfloat velocity_a[3];
    gfloat velocity_b[3];
    gfloat relative[3];
    gfloat normal_speed;
    gfloat inverse;
    gfloat bias;
    gfloat bounce;
    gfloat impulse_n;
    gfloat impulse[3];
    gfloat tangent[3];
    gfloat tangent_len;
    gfloat tangent_speed;
    gfloat impulse_t;
    gfloat friction_limit;

    a = &self->bodies[contact->body_a];
    b = &self->bodies[contact->body_b];
    point_velocity (a, contact->point, velocity_a);
    point_velocity (b, contact->point, velocity_b);
    relative[0] = velocity_a[0] - velocity_b[0];
    relative[1] = velocity_a[1] - velocity_b[1];
    relative[2] = velocity_a[2] - velocity_b[2];
    normal_speed = dot3 (relative, contact->normal);
    inverse = inv_mass_along (a, contact->point, contact->normal) +
              inv_mass_along (b, contact->point, contact->normal);
    if (inverse <= 1.0e-8f)
        return;
    bias = 0.0f;
    if (contact->depth > DYN_SLOP)
        bias = DYN_BAUMGARTE * (contact->depth - DYN_SLOP) / dt;
    bounce = (normal_speed < -0.1f) ? self->restitution : 0.0f;
    impulse_n = -(normal_speed * (1.0f + bounce) + bias) / inverse;
    if (impulse_n < 0.0f)
        impulse_n = 0.0f;
    impulse[0] = contact->normal[0] * impulse_n;
    impulse[1] = contact->normal[1] * impulse_n;
    impulse[2] = contact->normal[2] * impulse_n;
    apply_at_point (a, impulse, contact->point);
    impulse[0] = -impulse[0];
    impulse[1] = -impulse[1];
    impulse[2] = -impulse[2];
    apply_at_point (b, impulse, contact->point);
    if (self->friction <= 0.0f || impulse_n <= 0.0f)
        return;

    /* Friction opposes the tangential slip that remains after the normal impulse. */
    point_velocity (a, contact->point, velocity_a);
    point_velocity (b, contact->point, velocity_b);
    relative[0] = velocity_a[0] - velocity_b[0];
    relative[1] = velocity_a[1] - velocity_b[1];
    relative[2] = velocity_a[2] - velocity_b[2];
    normal_speed = dot3 (relative, contact->normal);
    tangent[0] = relative[0] - contact->normal[0] * normal_speed;
    tangent[1] = relative[1] - contact->normal[1] * normal_speed;
    tangent[2] = relative[2] - contact->normal[2] * normal_speed;
    tangent_len = len3 (tangent);
    if (tangent_len <= 1.0e-5f)
        return;
    tangent[0] /= tangent_len;
    tangent[1] /= tangent_len;
    tangent[2] /= tangent_len;
    inverse = inv_mass_along (a, contact->point, tangent) +
              inv_mass_along (b, contact->point, tangent);
    if (inverse <= 1.0e-8f)
        return;
    tangent_speed = dot3 (relative, tangent);
    impulse_t = -tangent_speed / inverse;
    friction_limit = self->friction * impulse_n;
    if (impulse_t > friction_limit)
        impulse_t = friction_limit;
    if (impulse_t < -friction_limit)
        impulse_t = -friction_limit;
    impulse[0] = tangent[0] * impulse_t;
    impulse[1] = tangent[1] * impulse_t;
    impulse[2] = tangent[2] * impulse_t;
    apply_at_point (a, impulse, contact->point);
    impulse[0] = -impulse[0];
    impulse[1] = -impulse[1];
    impulse[2] = -impulse[2];
    apply_at_point (b, impulse, contact->point);
}

static DynBody *
joint_body (LrgDynamicsWorld *self, guint id)
{
    if (id == LRG_DYNAMICS_WORLD_BODY || id >= self->n_bodies)
        return NULL;
    if (!self->bodies[id].alive)
        return NULL;
    return &self->bodies[id];
}

static void
joint_anchor (const DynBody *body, const gfloat *local, gfloat *out)
{
    gfloat rotated[3];

    if (body == NULL)
    {
        out[0] = local[0];
        out[1] = local[1];
        out[2] = local[2];
        return;
    }
    quat_rotate (body->orientation, local, rotated);
    out[0] = body->position[0] + rotated[0];
    out[1] = body->position[1] + rotated[1];
    out[2] = body->position[2] + rotated[2];
}

static void
joint_axis (const DynBody *body, const gfloat *local, gfloat *out)
{
    if (body == NULL)
    {
        out[0] = local[0];
        out[1] = local[1];
        out[2] = local[2];
        return;
    }
    quat_rotate (body->orientation, local, out);
}

static void
solve_linear_limit (DynBody *a, DynBody *b, const DynJoint *joint, gfloat dt)
{
    gfloat anchor_a[3];
    gfloat anchor_b[3];
    gfloat delta[3];
    gfloat direction[3];
    gfloat velocity_a[3];
    gfloat velocity_b[3];
    gfloat relative[3];
    gfloat distance;
    gfloat error;
    gfloat separation;
    gfloat inverse;
    gfloat bias;
    gfloat magnitude;
    gfloat impulse[3];
    gboolean closing;

    joint_anchor (a, joint->anchor_a, anchor_a);
    joint_anchor (b, joint->anchor_b, anchor_b);
    delta[0] = anchor_b[0] - anchor_a[0];
    delta[1] = anchor_b[1] - anchor_a[1];
    delta[2] = anchor_b[2] - anchor_a[2];
    distance = len3 (delta);
    if (distance > joint->linear_max)
    {
        error = distance - joint->linear_max;
        closing = TRUE;
    }
    else if (distance < joint->linear_min)
    {
        error = joint->linear_min - distance;
        closing = FALSE;
    }
    else
        return;
    if (distance > 1.0e-6f)
    {
        direction[0] = delta[0] / distance;
        direction[1] = delta[1] / distance;
        direction[2] = delta[2] / distance;
    }
    else
    {
        direction[0] = 1.0f;
        direction[1] = 0.0f;
        direction[2] = 0.0f;
    }
    point_velocity (a, anchor_a, velocity_a);
    point_velocity (b, anchor_b, velocity_b);
    relative[0] = velocity_b[0] - velocity_a[0];
    relative[1] = velocity_b[1] - velocity_a[1];
    relative[2] = velocity_b[2] - velocity_a[2];
    separation = dot3 (relative, direction);
    inverse = inv_mass_along (a, anchor_a, direction) + inv_mass_along (b, anchor_b, direction);
    if (inverse <= 1.0e-8f)
        return;
    bias = DYN_BAUMGARTE * error / dt;
    if (closing)
        magnitude = (separation + bias) / inverse;
    else
        magnitude = (separation - bias) / inverse;
    impulse[0] = direction[0] * magnitude;
    impulse[1] = direction[1] * magnitude;
    impulse[2] = direction[2] * magnitude;
    apply_at_point (a, impulse, anchor_a);
    impulse[0] = -impulse[0];
    impulse[1] = -impulse[1];
    impulse[2] = -impulse[2];
    apply_at_point (b, impulse, anchor_b);
}

static gboolean
axis_error (const DynBody *a,
            const DynBody *b,
            const DynJoint *joint,
            gfloat *error_out,
            gfloat *axis_out,
            gboolean *tighten_out)
{
    gfloat axis_a[3];
    gfloat axis_b[3];
    gfloat cross[3];
    gfloat cosine;
    gfloat angle;
    gfloat length;
    gboolean tighten;

    joint_axis (a, joint->axis_a, axis_a);
    joint_axis (b, joint->axis_b, axis_b);
    cosine = dot3 (axis_a, axis_b);
    if (cosine > 1.0f)
        cosine = 1.0f;
    if (cosine < -1.0f)
        cosine = -1.0f;
    angle = acosf (cosine);
    if (angle > joint->angular_max)
    {
        *error_out = angle - joint->angular_max;
        tighten = TRUE;
    }
    else if (angle < joint->angular_min)
    {
        *error_out = joint->angular_min - angle;
        tighten = FALSE;
    }
    else
        return FALSE;
    cross3 (axis_a, axis_b, cross);
    length = len3 (cross);
    if (length < 1.0e-5f)
        return FALSE;
    axis_out[0] = cross[0] / length;
    axis_out[1] = cross[1] / length;
    axis_out[2] = cross[2] / length;
    if (!tighten)
    {
        axis_out[0] = -axis_out[0];
        axis_out[1] = -axis_out[1];
        axis_out[2] = -axis_out[2];
    }
    *tighten_out = tighten;
    return TRUE;
}

static gfloat
scalar_inv_inertia (const DynBody *body, const gfloat *axis)
{
    gfloat omega[3];

    if (!dynamic_responsive (body))
        return 0.0f;
    apply_inv_inertia (body, axis, omega);
    return dot3 (omega, axis);
}

static void
solve_angular_limit (DynBody *a, DynBody *b, const DynJoint *joint, gfloat dt)
{
    gfloat axis[3];
    gfloat error;
    gfloat inverse;
    gfloat relative;
    gfloat bias;
    gfloat magnitude;
    gfloat impulse[3];
    gboolean tighten;

    if (!axis_error (a, b, joint, &error, axis, &tighten))
        return;
    (void) tighten;
    relative = 0.0f;
    if (dynamic_responsive (a))
        relative += dot3 (a->angular_velocity, axis);
    if (dynamic_responsive (b))
        relative -= dot3 (b->angular_velocity, axis);
    inverse = scalar_inv_inertia (a, axis) + scalar_inv_inertia (b, axis);
    if (inverse <= 1.0e-8f)
        return;
    bias = DYN_BAUMGARTE * error / dt;
    magnitude = (bias - relative) / inverse;
    impulse[0] = axis[0] * magnitude;
    impulse[1] = axis[1] * magnitude;
    impulse[2] = axis[2] * magnitude;
    apply_angular_impulse (a, impulse);
    impulse[0] = -impulse[0];
    impulse[1] = -impulse[1];
    impulse[2] = -impulse[2];
    apply_angular_impulse (b, impulse);
}

static void
correct_linear (DynBody *a, DynBody *b, const DynJoint *joint)
{
    gfloat anchor_a[3];
    gfloat anchor_b[3];
    gfloat delta[3];
    gfloat direction[3];
    gfloat distance;
    gfloat error;
    gfloat inverse_a;
    gfloat inverse_b;
    gfloat inverse;
    gfloat correction;
    gfloat sign;

    joint_anchor (a, joint->anchor_a, anchor_a);
    joint_anchor (b, joint->anchor_b, anchor_b);
    delta[0] = anchor_b[0] - anchor_a[0];
    delta[1] = anchor_b[1] - anchor_a[1];
    delta[2] = anchor_b[2] - anchor_a[2];
    distance = len3 (delta);
    if (distance > joint->linear_max)
        error = distance - joint->linear_max;
    else if (distance < joint->linear_min)
        error = distance - joint->linear_min;
    else
        return;
    if (fabsf (error) <= DYN_SLOP)
        return;
    if (distance > 1.0e-6f)
    {
        direction[0] = delta[0] / distance;
        direction[1] = delta[1] / distance;
        direction[2] = delta[2] / distance;
    }
    else
    {
        direction[0] = 1.0f;
        direction[1] = 0.0f;
        direction[2] = 0.0f;
    }
    inverse_a = dynamic_responsive (a) ? a->inv_mass : 0.0f;
    inverse_b = dynamic_responsive (b) ? b->inv_mass : 0.0f;
    inverse = inverse_a + inverse_b;
    if (inverse <= 1.0e-8f)
        return;
    /* Positive error is too far apart. Move A toward B and B toward A. */
    sign = (error > 0.0f) ? 1.0f : -1.0f;
    correction = (fabsf (error) - DYN_SLOP) * DYN_POSITION;
    if (inverse_a > 0.0f)
    {
        a->position[0] += direction[0] * sign * correction * (inverse_a / inverse);
        a->position[1] += direction[1] * sign * correction * (inverse_a / inverse);
        a->position[2] += direction[2] * sign * correction * (inverse_a / inverse);
    }
    if (inverse_b > 0.0f)
    {
        b->position[0] -= direction[0] * sign * correction * (inverse_b / inverse);
        b->position[1] -= direction[1] * sign * correction * (inverse_b / inverse);
        b->position[2] -= direction[2] * sign * correction * (inverse_b / inverse);
    }
}

static void
rotate_body (DynBody *body, const gfloat *axis, gfloat angle)
{
    gfloat delta[4];
    gfloat next[4];
    gfloat half;

    if (!dynamic_responsive (body) || angle == 0.0f)
        return;
    half = sinf (angle * 0.5f);
    delta[0] = axis[0] * half;
    delta[1] = axis[1] * half;
    delta[2] = axis[2] * half;
    delta[3] = cosf (angle * 0.5f);
    quat_mul (delta, body->orientation, next);
    quat_normalize (next);
    body->orientation[0] = next[0];
    body->orientation[1] = next[1];
    body->orientation[2] = next[2];
    body->orientation[3] = next[3];
}

static void
correct_angular (DynBody *a, DynBody *b, const DynJoint *joint)
{
    gfloat axis[3];
    gfloat error;
    gfloat inverse_a;
    gfloat inverse_b;
    gfloat inverse;
    gboolean tighten;

    if (!axis_error (a, b, joint, &error, axis, &tighten))
        return;
    (void) tighten;
    inverse_a = scalar_inv_inertia (a, axis);
    inverse_b = scalar_inv_inertia (b, axis);
    inverse = inverse_a + inverse_b;
    if (inverse <= 1.0e-8f)
        return;
    rotate_body (a, axis, error * DYN_POSITION * (inverse_a / inverse));
    rotate_body (b, axis, -error * DYN_POSITION * (inverse_b / inverse));
}

static void
correct_contact (DynContact *contact, DynBody *a, DynBody *b)
{
    gfloat inverse_a;
    gfloat inverse_b;
    gfloat inverse;
    gfloat correction;

    if (contact->depth <= DYN_SLOP)
        return;
    inverse_a = dynamic_responsive (a) ? a->inv_mass : 0.0f;
    inverse_b = dynamic_responsive (b) ? b->inv_mass : 0.0f;
    inverse = inverse_a + inverse_b;
    if (inverse <= 1.0e-8f)
        return;
    correction = (contact->depth - DYN_SLOP) * DYN_POSITION;
    if (inverse_a > 0.0f)
    {
        a->position[0] += contact->normal[0] * correction * (inverse_a / inverse);
        a->position[1] += contact->normal[1] * correction * (inverse_a / inverse);
        a->position[2] += contact->normal[2] * correction * (inverse_a / inverse);
    }
    if (inverse_b > 0.0f)
    {
        b->position[0] -= contact->normal[0] * correction * (inverse_b / inverse);
        b->position[1] -= contact->normal[1] * correction * (inverse_b / inverse);
        b->position[2] -= contact->normal[2] * correction * (inverse_b / inverse);
    }
}

static void
update_sleep (LrgDynamicsWorld *self)
{
    guint i;
    DynBody *body;
    gfloat speed2;

    for (i = 0; i < self->n_bodies; i++)
    {
        body = &self->bodies[i];
        if (!dynamic_responsive (body))
            continue;
        speed2 = dot3 (body->linear_velocity, body->linear_velocity) +
                 dot3 (body->angular_velocity, body->angular_velocity);
        if (speed2 < self->sleep_speed2)
        {
            if (body->quiet < G_MAXUINT)
                body->quiet++;
            if (body->quiet >= self->sleep_after)
                body->sleeping = TRUE;
        }
        else
        {
            body->quiet = 0;
            body->sleeping = FALSE;
        }
    }
}

static void
evict_outside (LrgDynamicsWorld *self)
{
    guint i;
    DynBody *body;

    if (!self->has_bounds)
        return;
    for (i = 0; i < self->n_bodies; i++)
    {
        body = &self->bodies[i];
        if (!body->alive || body->plane || body->kind != LRG_DYNAMICS_BODY_DYNAMIC)
            continue;
        if (body->position[0] < self->bounds_min[0] || body->position[0] > self->bounds_max[0] ||
            body->position[1] < self->bounds_min[1] || body->position[1] > self->bounds_max[1] ||
            body->position[2] < self->bounds_min[2] || body->position[2] > self->bounds_max[2])
        {
            body->alive = FALSE;
            body->linear_velocity[0] = 0.0f;
            body->linear_velocity[1] = 0.0f;
            body->linear_velocity[2] = 0.0f;
            body->angular_velocity[0] = 0.0f;
            body->angular_velocity[1] = 0.0f;
            body->angular_velocity[2] = 0.0f;
            if (self->n_evicted < LRG_DYNAMICS_MAX_BODIES)
            {
                self->evicted[self->n_evicted] = i;
                self->n_evicted++;
            }
        }
    }
}

static void
substep (LrgDynamicsWorld *self, gfloat dt)
{
    guint i;
    guint pass;
    DynBody *a;
    DynBody *b;

    for (i = 0; i < self->n_bodies; i++)
        integrate_body (&self->bodies[i], self->gravity, dt);
    generate_contacts (self);
    for (pass = 0; pass < self->iterations; pass++)
    {
        for (i = 0; i < self->n_contacts; i++)
            solve_contact (self, &self->contacts[i], dt);
        for (i = 0; i < self->n_joints; i++)
        {
            a = joint_body (self, self->joints[i].body_a);
            b = joint_body (self, self->joints[i].body_b);
            if (a == NULL)
                continue;
            solve_linear_limit (a, b, &self->joints[i], dt);
            solve_angular_limit (a, b, &self->joints[i], dt);
        }
    }
    for (i = 0; i < self->n_contacts; i++)
    {
        correct_contact (&self->contacts[i],
                         &self->bodies[self->contacts[i].body_a],
                         &self->bodies[self->contacts[i].body_b]);
    }
    for (i = 0; i < self->n_joints; i++)
    {
        a = joint_body (self, self->joints[i].body_a);
        b = joint_body (self, self->joints[i].body_b);
        if (a == NULL)
            continue;
        correct_linear (a, b, &self->joints[i]);
        correct_angular (a, b, &self->joints[i]);
    }
    update_sleep (self);
    evict_outside (self);
}

static gboolean
normalize_axis (const gfloat *in, gfloat *out, GError **error, const gchar *which)
{
    gfloat length;

    if (!finite3 (in))
        return dyn_error (error, LRG_DYNAMICS_ERROR_INVALID,
                          "dynamics %s is not finite", which);
    length = len3 (in);
    if (length <= 1.0e-8f)
        return dyn_error (error, LRG_DYNAMICS_ERROR_INVALID,
                          "dynamics %s has zero length", which);
    out[0] = in[0] / length;
    out[1] = in[1] / length;
    out[2] = in[2] / length;
    return TRUE;
}

static void
lrg_dynamics_world_class_init (LrgDynamicsWorldClass *klass)
{
    (void) klass;
}

static void
lrg_dynamics_world_init (LrgDynamicsWorld *self)
{
    self->fixed_dt = 1.0f / 60.0f;
    self->max_substeps = 4;
    self->sleep_speed2 = 1.0e-4f;
    self->sleep_after = 8;
    self->iterations = 8;
}

LrgDynamicsWorld *
lrg_dynamics_world_new (void)
{
    return g_object_new (LRG_TYPE_DYNAMICS_WORLD, NULL);
}

gboolean
lrg_dynamics_world_set_gravity (LrgDynamicsWorld *self,
                                const gfloat     *gravity,
                                GError          **error)
{
    g_return_val_if_fail (LRG_IS_DYNAMICS_WORLD (self), FALSE);
    g_return_val_if_fail (gravity != NULL, FALSE);
    if (!finite3 (gravity))
        return dyn_error (error, LRG_DYNAMICS_ERROR_INVALID,
                          "dynamics gravity is not finite");
    self->gravity[0] = gravity[0];
    self->gravity[1] = gravity[1];
    self->gravity[2] = gravity[2];
    return TRUE;
}

gboolean
lrg_dynamics_world_set_step_limits (LrgDynamicsWorld *self,
                                    gfloat            fixed_dt,
                                    guint             max_substeps,
                                    GError          **error)
{
    g_return_val_if_fail (LRG_IS_DYNAMICS_WORLD (self), FALSE);
    if (!finite1 (fixed_dt) || fixed_dt <= 0.0f || fixed_dt > 0.1f)
        return dyn_error (error, LRG_DYNAMICS_ERROR_INVALID,
                          "dynamics fixed step must be in (0, 0.1] seconds");
    if (max_substeps < 1 || max_substeps > LRG_DYNAMICS_MAX_SUBSTEPS)
        return dyn_error (error, LRG_DYNAMICS_ERROR_INVALID,
                          "dynamics substep cap must be from 1 to %u",
                          LRG_DYNAMICS_MAX_SUBSTEPS);
    self->fixed_dt = fixed_dt;
    self->max_substeps = max_substeps;
    return TRUE;
}

gboolean
lrg_dynamics_world_set_material (LrgDynamicsWorld *self,
                                 gfloat            restitution,
                                 gfloat            friction,
                                 GError          **error)
{
    g_return_val_if_fail (LRG_IS_DYNAMICS_WORLD (self), FALSE);
    if (!finite1 (restitution) || restitution < 0.0f || restitution > 1.0f)
        return dyn_error (error, LRG_DYNAMICS_ERROR_INVALID,
                          "dynamics restitution must be in [0, 1]");
    if (!finite1 (friction) || friction < 0.0f)
        return dyn_error (error, LRG_DYNAMICS_ERROR_INVALID,
                          "dynamics friction must be zero or positive");
    self->restitution = restitution;
    self->friction = friction;
    return TRUE;
}

void
lrg_dynamics_world_set_iterations (LrgDynamicsWorld *self,
                                   guint             iterations)
{
    g_return_if_fail (LRG_IS_DYNAMICS_WORLD (self));
    if (iterations < 1)
        iterations = 1;
    if (iterations > 32)
        iterations = 32;
    self->iterations = iterations;
}

gboolean
lrg_dynamics_world_set_sleep (LrgDynamicsWorld *self,
                              gfloat            speed_squared,
                              guint             quiet_substeps,
                              GError          **error)
{
    g_return_val_if_fail (LRG_IS_DYNAMICS_WORLD (self), FALSE);
    if (!finite1 (speed_squared) || speed_squared < 0.0f)
        return dyn_error (error, LRG_DYNAMICS_ERROR_INVALID,
                          "dynamics sleep speed must be zero or positive");
    if (quiet_substeps < 1)
        return dyn_error (error, LRG_DYNAMICS_ERROR_INVALID,
                          "dynamics sleep needs at least one quiet substep");
    self->sleep_speed2 = speed_squared;
    self->sleep_after = quiet_substeps;
    return TRUE;
}

gboolean
lrg_dynamics_world_set_bounds (LrgDynamicsWorld *self,
                               const gfloat     *minimum,
                               const gfloat     *maximum,
                               GError          **error)
{
    g_return_val_if_fail (LRG_IS_DYNAMICS_WORLD (self), FALSE);
    g_return_val_if_fail (minimum != NULL, FALSE);
    g_return_val_if_fail (maximum != NULL, FALSE);
    if (!finite3 (minimum) || !finite3 (maximum) ||
        !(minimum[0] < maximum[0]) ||
        !(minimum[1] < maximum[1]) ||
        !(minimum[2] < maximum[2]))
        return dyn_error (error, LRG_DYNAMICS_ERROR_INVALID,
                          "dynamics bounds need a positive finite extent on each axis");
    self->bounds_min[0] = minimum[0];
    self->bounds_min[1] = minimum[1];
    self->bounds_min[2] = minimum[2];
    self->bounds_max[0] = maximum[0];
    self->bounds_max[1] = maximum[1];
    self->bounds_max[2] = maximum[2];
    self->has_bounds = TRUE;
    return TRUE;
}

static gboolean
store_orientation (DynBody *body, const gfloat *orientation, GError **error)
{
    gfloat length;

    body->orientation[0] = 0.0f;
    body->orientation[1] = 0.0f;
    body->orientation[2] = 0.0f;
    body->orientation[3] = 1.0f;
    if (orientation == NULL)
        return TRUE;
    if (!finite1 (orientation[0]) || !finite1 (orientation[1]) ||
        !finite1 (orientation[2]) || !finite1 (orientation[3]))
        return dyn_error (error, LRG_DYNAMICS_ERROR_INVALID,
                          "dynamics orientation is not finite");
    length = sqrtf (orientation[0] * orientation[0] + orientation[1] * orientation[1] +
                    orientation[2] * orientation[2] + orientation[3] * orientation[3]);
    if (length <= 1.0e-8f)
        return dyn_error (error, LRG_DYNAMICS_ERROR_INVALID,
                          "dynamics orientation has zero length");
    body->orientation[0] = orientation[0] / length;
    body->orientation[1] = orientation[1] / length;
    body->orientation[2] = orientation[2] / length;
    body->orientation[3] = orientation[3] / length;
    return TRUE;
}

static void
store_isotropic (DynBody *body, gfloat mass, gfloat radius)
{
    gfloat inertia;

    if (body->kind != LRG_DYNAMICS_BODY_DYNAMIC)
        return;
    body->inv_mass = 1.0f / mass;
    inertia = 0.4f * mass * radius * radius;
    body->inertia[0] = inertia;
    body->inertia[1] = inertia;
    body->inertia[2] = inertia;
    body->inv_inertia[0] = 1.0f / inertia;
    body->inv_inertia[1] = 1.0f / inertia;
    body->inv_inertia[2] = 1.0f / inertia;
}

static void
store_diagonal (DynBody *body, gfloat ixx, gfloat iyy, gfloat izz)
{
    body->inertia[0] = ixx;
    body->inertia[1] = iyy;
    body->inertia[2] = izz;
    body->inv_inertia[0] = 1.0f / ixx;
    body->inv_inertia[1] = 1.0f / iyy;
    body->inv_inertia[2] = 1.0f / izz;
}

static gboolean
mass_accepted (LrgDynamicsBodyKind kind, gfloat mass, GError **error)
{
    if (kind != LRG_DYNAMICS_BODY_DYNAMIC &&
        kind != LRG_DYNAMICS_BODY_KINEMATIC &&
        kind != LRG_DYNAMICS_BODY_STATIC)
        return dyn_error (error, LRG_DYNAMICS_ERROR_INVALID,
                          "dynamics body kind is not dynamic, kinematic or static");
    if (!finite1 (mass) || mass < 0.0f ||
        (kind == LRG_DYNAMICS_BODY_DYNAMIC && !(mass > 0.0f)))
        return dyn_error (error, LRG_DYNAMICS_ERROR_INVALID,
                          "dynamics body mass must be finite and positive when dynamic");
    return TRUE;
}

gboolean
lrg_dynamics_world_add_sphere (LrgDynamicsWorld    *self,
                               LrgDynamicsBodyKind  kind,
                               gfloat               mass,
                               gfloat               radius,
                               const gfloat        *position,
                               const gfloat        *orientation,
                               guint               *body_id,
                               GError             **error)
{
    DynBody body;

    g_return_val_if_fail (LRG_IS_DYNAMICS_WORLD (self), FALSE);
    g_return_val_if_fail (position != NULL, FALSE);
    g_return_val_if_fail (body_id != NULL, FALSE);
    if (!mass_accepted (kind, mass, error))
        return FALSE;
    if (!finite1 (radius) || !(radius > 0.0f) || !finite3 (position))
        return dyn_error (error, LRG_DYNAMICS_ERROR_INVALID,
                          "dynamics sphere radius and position must be finite and the radius positive");
    memset (&body, 0, sizeof (body));
    if (!store_orientation (&body, orientation, error))
        return FALSE;
    if (self->n_bodies >= LRG_DYNAMICS_MAX_BODIES)
        return dyn_error (error, LRG_DYNAMICS_ERROR_CAPACITY,
                          "dynamics world already has %u bodies",
                          LRG_DYNAMICS_MAX_BODIES);
    body.alive = TRUE;
    body.shape = DYN_SHAPE_SPHERE;
    body.kind = kind;
    body.mass = mass;
    body.radius = radius;
    body.position[0] = position[0];
    body.position[1] = position[1];
    body.position[2] = position[2];
    store_isotropic (&body, mass, radius);
    self->bodies[self->n_bodies] = body;
    *body_id = self->n_bodies;
    self->n_bodies++;
    return TRUE;
}

gboolean
lrg_dynamics_world_add_plane (LrgDynamicsWorld *self,
                              const gfloat     *normal,
                              gfloat            offset,
                              guint            *body_id,
                              GError          **error)
{
    DynBody body;
    gfloat length;

    g_return_val_if_fail (LRG_IS_DYNAMICS_WORLD (self), FALSE);
    g_return_val_if_fail (normal != NULL, FALSE);
    g_return_val_if_fail (body_id != NULL, FALSE);
    if (!finite3 (normal) || !finite1 (offset))
        return dyn_error (error, LRG_DYNAMICS_ERROR_INVALID,
                          "dynamics plane normal and offset must be finite");
    length = len3 (normal);
    if (fabsf (length - 1.0f) > 1.0e-4f)
        return dyn_error (error, LRG_DYNAMICS_ERROR_INVALID,
                          "dynamics plane normal must already have length 1");
    if (self->n_bodies >= LRG_DYNAMICS_MAX_BODIES)
        return dyn_error (error, LRG_DYNAMICS_ERROR_CAPACITY,
                          "dynamics world already has %u bodies",
                          LRG_DYNAMICS_MAX_BODIES);
    memset (&body, 0, sizeof (body));
    body.alive = TRUE;
    body.plane = TRUE;
    body.shape = DYN_SHAPE_PLANE;
    body.kind = LRG_DYNAMICS_BODY_STATIC;
    body.orientation[3] = 1.0f;
    body.plane_normal[0] = normal[0];
    body.plane_normal[1] = normal[1];
    body.plane_normal[2] = normal[2];
    body.plane_offset = offset;
    self->bodies[self->n_bodies] = body;
    *body_id = self->n_bodies;
    self->n_bodies++;
    return TRUE;
}

gboolean
lrg_dynamics_world_add_sphyl (LrgDynamicsWorld    *self,
                              LrgDynamicsBodyKind  kind,
                              gfloat               mass,
                              gfloat               radius,
                              gfloat               half_length,
                              const gfloat        *axis,
                              const gfloat        *position,
                              const gfloat        *orientation,
                              guint               *body_id,
                              GError             **error)
{
    DynBody body;
    gfloat length;

    g_return_val_if_fail (LRG_IS_DYNAMICS_WORLD (self), FALSE);
    g_return_val_if_fail (axis != NULL, FALSE);
    g_return_val_if_fail (position != NULL, FALSE);
    g_return_val_if_fail (body_id != NULL, FALSE);
    if (!mass_accepted (kind, mass, error))
        return FALSE;
    if (!finite1 (radius) || !(radius > 0.0f) ||
        !finite1 (half_length) || half_length < 0.0f ||
        !finite3 (position) || !finite3 (axis))
        return dyn_error (error, LRG_DYNAMICS_ERROR_INVALID,
                          "dynamics sphyl radius, half-length, axis and position must be finite");
    length = len3 (axis);
    if (fabsf (length - 1.0f) > 1.0e-4f)
        return dyn_error (error, LRG_DYNAMICS_ERROR_INVALID,
                          "dynamics sphyl axis must already have length 1");
    memset (&body, 0, sizeof (body));
    if (!store_orientation (&body, orientation, error))
        return FALSE;
    if (self->n_bodies >= LRG_DYNAMICS_MAX_BODIES)
        return dyn_error (error, LRG_DYNAMICS_ERROR_CAPACITY,
                          "dynamics world already has %u bodies",
                          LRG_DYNAMICS_MAX_BODIES);
    body.alive = TRUE;
    body.shape = DYN_SHAPE_SPHYL;
    body.kind = kind;
    body.mass = mass;
    body.radius = radius;
    body.half_length = half_length;
    body.axis[0] = axis[0];
    body.axis[1] = axis[1];
    body.axis[2] = axis[2];
    body.position[0] = position[0];
    body.position[1] = position[1];
    body.position[2] = position[2];
    store_isotropic (&body, mass, radius);
    self->bodies[self->n_bodies] = body;
    *body_id = self->n_bodies;
    self->n_bodies++;
    return TRUE;
}

gboolean
lrg_dynamics_world_add_inertial (LrgDynamicsWorld *self,
                                 gfloat            mass,
                                 const gfloat     *inertia_diagonal,
                                 const gfloat     *position,
                                 const gfloat     *orientation,
                                 guint            *body_id,
                                 GError          **error)
{
    DynBody body;

    g_return_val_if_fail (LRG_IS_DYNAMICS_WORLD (self), FALSE);
    g_return_val_if_fail (inertia_diagonal != NULL, FALSE);
    g_return_val_if_fail (position != NULL, FALSE);
    g_return_val_if_fail (body_id != NULL, FALSE);
    if (!finite1 (mass) || !(mass > 0.0f) || !finite3 (position))
        return dyn_error (error, LRG_DYNAMICS_ERROR_INVALID,
                          "dynamics contactless mass and position must be finite and the mass positive");
    if (!finite1 (inertia_diagonal[0]) || !finite1 (inertia_diagonal[1]) ||
        !finite1 (inertia_diagonal[2]) ||
        !(inertia_diagonal[0] > 0.0f) || !(inertia_diagonal[1] > 0.0f) ||
        !(inertia_diagonal[2] > 0.0f))
        return dyn_error (error, LRG_DYNAMICS_ERROR_INVALID,
                          "dynamics contactless inertia diagonal must be finite and positive");
    memset (&body, 0, sizeof (body));
    if (!store_orientation (&body, orientation, error))
        return FALSE;
    if (self->n_bodies >= LRG_DYNAMICS_MAX_BODIES)
        return dyn_error (error, LRG_DYNAMICS_ERROR_CAPACITY,
                          "dynamics world already has %u bodies",
                          LRG_DYNAMICS_MAX_BODIES);
    body.alive = TRUE;
    body.shape = DYN_SHAPE_INERTIAL;
    body.kind = LRG_DYNAMICS_BODY_DYNAMIC;
    body.mass = mass;
    body.inv_mass = 1.0f / mass;
    body.position[0] = position[0];
    body.position[1] = position[1];
    body.position[2] = position[2];
    store_diagonal (&body, inertia_diagonal[0], inertia_diagonal[1], inertia_diagonal[2]);
    self->bodies[self->n_bodies] = body;
    *body_id = self->n_bodies;
    self->n_bodies++;
    return TRUE;
}

gboolean
lrg_dynamics_world_set_box_inertia (LrgDynamicsWorld *self,
                                    guint             body_id,
                                    gfloat            half_x,
                                    gfloat            half_y,
                                    gfloat            half_z,
                                    GError          **error)
{
    DynBody *body;
    gfloat ixx;
    gfloat iyy;
    gfloat izz;

    g_return_val_if_fail (LRG_IS_DYNAMICS_WORLD (self), FALSE);
    body = sphere_known (self, body_id, error);
    if (body == NULL)
        return FALSE;
    if (!body->alive)
        return dyn_error (error, LRG_DYNAMICS_ERROR_MISSING,
                          "dynamics body %u was evicted", body_id);
    if (body->shape != DYN_SHAPE_SPHERE ||
        body->kind != LRG_DYNAMICS_BODY_DYNAMIC || !(body->mass > 0.0f))
        return dyn_error (error, LRG_DYNAMICS_ERROR_STATE,
                          "dynamics box inertia requires a dynamic sphere");
    if (!finite1 (half_x) || !finite1 (half_y) || !finite1 (half_z) ||
        !(half_x > 0.0f) || !(half_y > 0.0f) || !(half_z > 0.0f))
        return dyn_error (error, LRG_DYNAMICS_ERROR_INVALID,
                          "dynamics box half extents must be finite and positive");
    ixx = body->mass / 12.0f * (half_y * half_y + half_z * half_z);
    iyy = body->mass / 12.0f * (half_x * half_x + half_z * half_z);
    izz = body->mass / 12.0f * (half_x * half_x + half_y * half_y);
    store_diagonal (body, ixx, iyy, izz);
    return TRUE;
}

gboolean
lrg_dynamics_world_set_inertia_diagonal (LrgDynamicsWorld *self,
                                         guint             body_id,
                                         gfloat            ixx,
                                         gfloat            iyy,
                                         gfloat            izz,
                                         GError          **error)
{
    DynBody *body;

    g_return_val_if_fail (LRG_IS_DYNAMICS_WORLD (self), FALSE);
    body = sphere_known (self, body_id, error);
    if (body == NULL)
        return FALSE;
    if (!body->alive)
        return dyn_error (error, LRG_DYNAMICS_ERROR_MISSING,
                          "dynamics body %u was evicted", body_id);
    if (body->kind != LRG_DYNAMICS_BODY_DYNAMIC || !(body->mass > 0.0f))
        return dyn_error (error, LRG_DYNAMICS_ERROR_STATE,
                          "dynamics inertia diagonal requires a dynamic body");
    if (!finite1 (ixx) || !finite1 (iyy) || !finite1 (izz) ||
        !(ixx > 0.0f) || !(iyy > 0.0f) || !(izz > 0.0f))
        return dyn_error (error, LRG_DYNAMICS_ERROR_INVALID,
                          "dynamics inertia diagonal must be finite and positive");
    store_diagonal (body, ixx, iyy, izz);
    return TRUE;
}

gboolean
lrg_dynamics_world_get_inertia (LrgDynamicsWorld *self,
                                guint             body_id,
                                gfloat           *inertia,
                                GError          **error)
{
    DynBody *body;

    g_return_val_if_fail (LRG_IS_DYNAMICS_WORLD (self), FALSE);
    g_return_val_if_fail (inertia != NULL, FALSE);
    body = sphere_known (self, body_id, error);
    if (body == NULL)
        return FALSE;
    inertia[0] = body->inertia[0];
    inertia[1] = body->inertia[1];
    inertia[2] = body->inertia[2];
    return TRUE;
}

gboolean
lrg_dynamics_world_set_pose (LrgDynamicsWorld *self,
                             guint             body_id,
                             const gfloat     *position,
                             const gfloat     *orientation,
                             GError          **error)
{
    DynBody *body;
    gfloat length;
    gfloat next[4];

    g_return_val_if_fail (LRG_IS_DYNAMICS_WORLD (self), FALSE);
    g_return_val_if_fail (position != NULL, FALSE);
    g_return_val_if_fail (orientation != NULL, FALSE);
    body = sphere_known (self, body_id, error);
    if (body == NULL)
        return FALSE;
    if (!body->alive)
        return dyn_error (error, LRG_DYNAMICS_ERROR_MISSING,
                          "dynamics body %u was evicted", body_id);
    if (!finite3 (position) || !finite1 (orientation[0]) || !finite1 (orientation[1]) ||
        !finite1 (orientation[2]) || !finite1 (orientation[3]))
        return dyn_error (error, LRG_DYNAMICS_ERROR_INVALID,
                          "dynamics pose is not finite");
    length = sqrtf (orientation[0] * orientation[0] + orientation[1] * orientation[1] +
                    orientation[2] * orientation[2] + orientation[3] * orientation[3]);
    if (length <= 1.0e-8f)
        return dyn_error (error, LRG_DYNAMICS_ERROR_INVALID,
                          "dynamics orientation has zero length");
    next[0] = orientation[0] / length;
    next[1] = orientation[1] / length;
    next[2] = orientation[2] / length;
    next[3] = orientation[3] / length;
    body->position[0] = position[0];
    body->position[1] = position[1];
    body->position[2] = position[2];
    body->orientation[0] = next[0];
    body->orientation[1] = next[1];
    body->orientation[2] = next[2];
    body->orientation[3] = next[3];
    if (!body->frozen)
        wake_body (body);
    return TRUE;
}

gboolean
lrg_dynamics_world_get_position (LrgDynamicsWorld *self,
                                 guint             body_id,
                                 gfloat           *position,
                                 GError          **error)
{
    DynBody *body;

    g_return_val_if_fail (LRG_IS_DYNAMICS_WORLD (self), FALSE);
    g_return_val_if_fail (position != NULL, FALSE);
    body = sphere_known (self, body_id, error);
    if (body == NULL)
        return FALSE;
    position[0] = body->position[0];
    position[1] = body->position[1];
    position[2] = body->position[2];
    return TRUE;
}

gboolean
lrg_dynamics_world_get_orientation (LrgDynamicsWorld *self,
                                    guint             body_id,
                                    gfloat           *orientation,
                                    GError          **error)
{
    DynBody *body;

    g_return_val_if_fail (LRG_IS_DYNAMICS_WORLD (self), FALSE);
    g_return_val_if_fail (orientation != NULL, FALSE);
    body = sphere_known (self, body_id, error);
    if (body == NULL)
        return FALSE;
    orientation[0] = body->orientation[0];
    orientation[1] = body->orientation[1];
    orientation[2] = body->orientation[2];
    orientation[3] = body->orientation[3];
    return TRUE;
}

gboolean
lrg_dynamics_world_get_linear_velocity (LrgDynamicsWorld *self,
                                        guint             body_id,
                                        gfloat           *velocity,
                                        GError          **error)
{
    DynBody *body;

    g_return_val_if_fail (LRG_IS_DYNAMICS_WORLD (self), FALSE);
    g_return_val_if_fail (velocity != NULL, FALSE);
    body = sphere_known (self, body_id, error);
    if (body == NULL)
        return FALSE;
    velocity[0] = body->linear_velocity[0];
    velocity[1] = body->linear_velocity[1];
    velocity[2] = body->linear_velocity[2];
    return TRUE;
}

gboolean
lrg_dynamics_world_get_angular_velocity (LrgDynamicsWorld *self,
                                         guint             body_id,
                                         gfloat           *velocity,
                                         GError          **error)
{
    DynBody *body;

    g_return_val_if_fail (LRG_IS_DYNAMICS_WORLD (self), FALSE);
    g_return_val_if_fail (velocity != NULL, FALSE);
    body = sphere_known (self, body_id, error);
    if (body == NULL)
        return FALSE;
    velocity[0] = body->angular_velocity[0];
    velocity[1] = body->angular_velocity[1];
    velocity[2] = body->angular_velocity[2];
    return TRUE;
}

gboolean
lrg_dynamics_world_set_linear_velocity (LrgDynamicsWorld *self,
                                        guint             body_id,
                                        const gfloat     *velocity,
                                        GError          **error)
{
    DynBody *body;

    g_return_val_if_fail (LRG_IS_DYNAMICS_WORLD (self), FALSE);
    g_return_val_if_fail (velocity != NULL, FALSE);
    body = sphere_known (self, body_id, error);
    if (body == NULL)
        return FALSE;
    if (!body->alive)
        return dyn_error (error, LRG_DYNAMICS_ERROR_MISSING,
                          "dynamics body %u was evicted", body_id);
    if (body->frozen || body->kind == LRG_DYNAMICS_BODY_STATIC)
        return dyn_error (error, LRG_DYNAMICS_ERROR_STATE,
                          "dynamics velocity requires an unfrozen dynamic or kinematic sphere");
    if (body->kind != LRG_DYNAMICS_BODY_DYNAMIC && body->kind != LRG_DYNAMICS_BODY_KINEMATIC)
        return dyn_error (error, LRG_DYNAMICS_ERROR_STATE,
                          "dynamics velocity requires an unfrozen dynamic or kinematic sphere");
    if (!finite3 (velocity))
        return dyn_error (error, LRG_DYNAMICS_ERROR_INVALID,
                          "dynamics velocity is not finite");
    body->linear_velocity[0] = velocity[0];
    body->linear_velocity[1] = velocity[1];
    body->linear_velocity[2] = velocity[2];
    wake_body (body);
    return TRUE;
}

gboolean
lrg_dynamics_world_apply_force (LrgDynamicsWorld *self,
                                guint             body_id,
                                const gfloat     *force,
                                GError          **error)
{
    DynBody *body;

    g_return_val_if_fail (LRG_IS_DYNAMICS_WORLD (self), FALSE);
    g_return_val_if_fail (force != NULL, FALSE);
    body = sphere_known (self, body_id, error);
    if (body == NULL)
        return FALSE;
    if (!body->alive)
        return dyn_error (error, LRG_DYNAMICS_ERROR_MISSING,
                          "dynamics body %u was evicted", body_id);
    if (body->frozen || body->kind != LRG_DYNAMICS_BODY_DYNAMIC)
        return dyn_error (error, LRG_DYNAMICS_ERROR_STATE,
                          "dynamics force requires an unfrozen dynamic sphere");
    if (!finite3 (force))
        return dyn_error (error, LRG_DYNAMICS_ERROR_INVALID,
                          "dynamics force is not finite");
    wake_body (body);
    body->force[0] += force[0];
    body->force[1] += force[1];
    body->force[2] += force[2];
    return TRUE;
}

gboolean
lrg_dynamics_world_apply_torque (LrgDynamicsWorld *self,
                                 guint             body_id,
                                 const gfloat     *torque,
                                 GError          **error)
{
    DynBody *body;

    g_return_val_if_fail (LRG_IS_DYNAMICS_WORLD (self), FALSE);
    g_return_val_if_fail (torque != NULL, FALSE);
    body = sphere_known (self, body_id, error);
    if (body == NULL)
        return FALSE;
    if (!body->alive)
        return dyn_error (error, LRG_DYNAMICS_ERROR_MISSING,
                          "dynamics body %u was evicted", body_id);
    if (body->frozen || body->kind != LRG_DYNAMICS_BODY_DYNAMIC)
        return dyn_error (error, LRG_DYNAMICS_ERROR_STATE,
                          "dynamics torque requires an unfrozen dynamic sphere");
    if (!finite3 (torque))
        return dyn_error (error, LRG_DYNAMICS_ERROR_INVALID,
                          "dynamics torque is not finite");
    wake_body (body);
    body->torque[0] += torque[0];
    body->torque[1] += torque[1];
    body->torque[2] += torque[2];
    return TRUE;
}

gboolean
lrg_dynamics_world_set_frozen (LrgDynamicsWorld *self,
                               guint             body_id,
                               gboolean          frozen,
                               GError          **error)
{
    DynBody *body;

    g_return_val_if_fail (LRG_IS_DYNAMICS_WORLD (self), FALSE);
    body = sphere_known (self, body_id, error);
    if (body == NULL)
        return FALSE;
    if (!body->alive)
        return dyn_error (error, LRG_DYNAMICS_ERROR_MISSING,
                          "dynamics body %u was evicted", body_id);
    body->frozen = frozen ? TRUE : FALSE;
    return TRUE;
}

gboolean
lrg_dynamics_world_get_sleeping (LrgDynamicsWorld *self,
                                 guint             body_id,
                                 gboolean         *sleeping,
                                 GError          **error)
{
    DynBody *body;

    g_return_val_if_fail (LRG_IS_DYNAMICS_WORLD (self), FALSE);
    g_return_val_if_fail (sleeping != NULL, FALSE);
    body = sphere_known (self, body_id, error);
    if (body == NULL)
        return FALSE;
    *sleeping = body->sleeping;
    return TRUE;
}

gboolean
lrg_dynamics_world_body_is_active (LrgDynamicsWorld *self,
                                   guint             body_id,
                                   gboolean         *active,
                                   GError          **error)
{
    DynBody *body;

    g_return_val_if_fail (LRG_IS_DYNAMICS_WORLD (self), FALSE);
    g_return_val_if_fail (active != NULL, FALSE);
    body = body_known (self, body_id, error);
    if (body == NULL)
        return FALSE;
    *active = body->alive;
    return TRUE;
}

gboolean
lrg_dynamics_world_add_joint (LrgDynamicsWorld *self,
                              guint             body_a,
                              guint             body_b,
                              const gfloat     *anchor_a,
                              const gfloat     *axis_a,
                              const gfloat     *anchor_b,
                              const gfloat     *axis_b,
                              gfloat            linear_min,
                              gfloat            linear_max,
                              gfloat            angular_min,
                              gfloat            angular_max,
                              guint            *joint_id,
                              GError          **error)
{
    DynJoint joint;
    DynBody *first;
    DynBody *second;

    g_return_val_if_fail (LRG_IS_DYNAMICS_WORLD (self), FALSE);
    g_return_val_if_fail (anchor_a != NULL && axis_a != NULL, FALSE);
    g_return_val_if_fail (anchor_b != NULL && axis_b != NULL, FALSE);
    g_return_val_if_fail (joint_id != NULL, FALSE);
    if (body_a == body_b)
        return dyn_error (error, LRG_DYNAMICS_ERROR_INVALID,
                          "dynamics joint needs two different endpoints");
    first = sphere_known (self, body_a, error);
    if (first == NULL)
        return FALSE;
    if (!first->alive)
        return dyn_error (error, LRG_DYNAMICS_ERROR_MISSING,
                          "dynamics body %u was evicted", body_a);
    if (body_b != LRG_DYNAMICS_WORLD_BODY)
    {
        second = sphere_known (self, body_b, error);
        if (second == NULL)
            return FALSE;
        if (!second->alive)
            return dyn_error (error, LRG_DYNAMICS_ERROR_MISSING,
                              "dynamics body %u was evicted", body_b);
    }
    if (!finite3 (anchor_a) || !finite3 (anchor_b))
        return dyn_error (error, LRG_DYNAMICS_ERROR_INVALID,
                          "dynamics joint anchors must be finite");
    if (!finite1 (linear_min) || !finite1 (linear_max) ||
        linear_min < 0.0f || linear_max < linear_min)
        return dyn_error (error, LRG_DYNAMICS_ERROR_INVALID,
                          "dynamics linear limits must be finite, non-negative and ordered");
    if (!finite1 (angular_min) || !finite1 (angular_max) ||
        angular_min < 0.0f || angular_max > DYN_PI || angular_max < angular_min)
        return dyn_error (error, LRG_DYNAMICS_ERROR_INVALID,
                          "dynamics angular limits must lie in [0, pi] and be ordered");
    if (self->n_joints >= LRG_DYNAMICS_MAX_JOINTS)
        return dyn_error (error, LRG_DYNAMICS_ERROR_CAPACITY,
                          "dynamics world already has %u joints",
                          LRG_DYNAMICS_MAX_JOINTS);
    memset (&joint, 0, sizeof (joint));
    if (!normalize_axis (axis_a, joint.axis_a, error, "joint axis A") ||
        !normalize_axis (axis_b, joint.axis_b, error, "joint axis B"))
        return FALSE;
    joint.body_a = body_a;
    joint.body_b = body_b;
    joint.anchor_a[0] = anchor_a[0];
    joint.anchor_a[1] = anchor_a[1];
    joint.anchor_a[2] = anchor_a[2];
    joint.anchor_b[0] = anchor_b[0];
    joint.anchor_b[1] = anchor_b[1];
    joint.anchor_b[2] = anchor_b[2];
    joint.linear_min = linear_min;
    joint.linear_max = linear_max;
    joint.angular_min = angular_min;
    joint.angular_max = angular_max;
    self->joints[self->n_joints] = joint;
    *joint_id = self->n_joints;
    self->n_joints++;
    return TRUE;
}

gboolean
lrg_dynamics_world_step (LrgDynamicsWorld *self,
                         gfloat            dt,
                         guint            *substeps,
                         GError          **error)
{
    guint taken;

    g_return_val_if_fail (LRG_IS_DYNAMICS_WORLD (self), FALSE);
    if (!finite1 (dt) || dt < 0.0f)
        return dyn_error (error, LRG_DYNAMICS_ERROR_INVALID,
                          "dynamics step time must be finite and non-negative");
    self->n_contacts = 0;
    self->n_evicted = 0;
    self->dropped = 0.0f;
    self->accumulator += dt;
    taken = 0;
    while (taken < self->max_substeps &&
           self->accumulator + 1.0e-6f >= self->fixed_dt)
    {
        substep (self, self->fixed_dt);
        self->accumulator -= self->fixed_dt;
        taken++;
    }
    if (self->accumulator >= self->fixed_dt)
    {
        self->dropped = self->accumulator;
        self->accumulator = 0.0f;
    }
    if (substeps != NULL)
        *substeps = taken;
    (void) error;
    return TRUE;
}

gfloat
lrg_dynamics_world_get_dropped_time (LrgDynamicsWorld *self)
{
    g_return_val_if_fail (LRG_IS_DYNAMICS_WORLD (self), 0.0f);
    return self->dropped;
}

guint
lrg_dynamics_world_get_contact_count (LrgDynamicsWorld *self)
{
    g_return_val_if_fail (LRG_IS_DYNAMICS_WORLD (self), 0);
    return self->n_contacts;
}

gboolean
lrg_dynamics_world_get_contact (LrgDynamicsWorld       *self,
                                guint                   index,
                                LrgDynamicsContactKind *kind,
                                guint                  *body_a,
                                guint                  *body_b,
                                gfloat                 *point,
                                gfloat                 *normal,
                                gfloat                 *depth,
                                GError                **error)
{
    DynContact *contact;

    g_return_val_if_fail (LRG_IS_DYNAMICS_WORLD (self), FALSE);
    g_return_val_if_fail (kind != NULL, FALSE);
    g_return_val_if_fail (body_a != NULL && body_b != NULL, FALSE);
    g_return_val_if_fail (point != NULL && normal != NULL && depth != NULL, FALSE);
    if (index >= self->n_contacts)
        return dyn_error (error, LRG_DYNAMICS_ERROR_MISSING,
                          "dynamics contact %u is outside the last substep", index);
    contact = &self->contacts[index];
    *kind = contact->kind;
    *body_a = contact->body_a;
    *body_b = contact->body_b;
    point[0] = contact->point[0];
    point[1] = contact->point[1];
    point[2] = contact->point[2];
    normal[0] = contact->normal[0];
    normal[1] = contact->normal[1];
    normal[2] = contact->normal[2];
    *depth = contact->depth;
    return TRUE;
}

guint
lrg_dynamics_world_get_evicted (LrgDynamicsWorld *self,
                                guint            *ids,
                                guint             max_ids)
{
    guint count;
    guint i;

    g_return_val_if_fail (LRG_IS_DYNAMICS_WORLD (self), 0);
    g_return_val_if_fail (ids != NULL || max_ids == 0, 0);
    count = self->n_evicted;
    if (ids == NULL)
        return count;
    for (i = 0; i < count && i < max_ids; i++)
        ids[i] = self->evicted[i];
    return count;
}

gboolean
lrg_dynamics_world_write_bone_pose (LrgDynamicsWorld  *self,
                                    guint              body_id,
                                    const LrgBonePose *parent_world,
                                    LrgBonePose       *local,
                                    LrgBonePose       *world_pose,
                                    GError           **error)
{
    DynBody *body;
    LrgBonePose parent;
    LrgBonePose local_out;
    LrgBonePose world_out;
    gfloat parent_q[4];
    gfloat body_q[4];
    gfloat local_q[4];
    gfloat conj[4];
    gfloat length;
    gfloat delta[3];
    gfloat local_pos[3];
    gfloat scale_x;
    gfloat scale_y;
    gfloat scale_z;

    g_return_val_if_fail (LRG_IS_DYNAMICS_WORLD (self), FALSE);
    g_return_val_if_fail (local != NULL, FALSE);
    g_return_val_if_fail (world_pose != NULL, FALSE);
    body = sphere_known (self, body_id, error);
    if (body == NULL)
        return FALSE;
    if (!body->alive)
        return dyn_error (error, LRG_DYNAMICS_ERROR_MISSING,
                          "dynamics body %u was evicted", body_id);
    scale_x = local->scale_x;
    scale_y = local->scale_y;
    scale_z = local->scale_z;
    if (!finite1 (scale_x) || !finite1 (scale_y) || !finite1 (scale_z))
        return dyn_error (error, LRG_DYNAMICS_ERROR_INVALID,
                          "dynamics bone scale is not finite");
    memset (&parent, 0, sizeof (parent));
    parent.rotation_w = 1.0f;
    parent.scale_x = 1.0f;
    parent.scale_y = 1.0f;
    parent.scale_z = 1.0f;
    if (parent_world != NULL)
        parent = *parent_world;
    if (!finite1 (parent.position_x) || !finite1 (parent.position_y) ||
        !finite1 (parent.position_z) || !finite1 (parent.rotation_x) ||
        !finite1 (parent.rotation_y) || !finite1 (parent.rotation_z) ||
        !finite1 (parent.rotation_w) || !finite1 (parent.scale_x) ||
        !finite1 (parent.scale_y) || !finite1 (parent.scale_z))
        return dyn_error (error, LRG_DYNAMICS_ERROR_INVALID,
                          "dynamics parent bone pose is not finite");
    if (fabsf (parent.scale_x) < 1.0e-8f || fabsf (parent.scale_y) < 1.0e-8f ||
        fabsf (parent.scale_z) < 1.0e-8f)
        return dyn_error (error, LRG_DYNAMICS_ERROR_INVALID,
                          "dynamics parent bone scale is zero");
    parent_q[0] = parent.rotation_x;
    parent_q[1] = parent.rotation_y;
    parent_q[2] = parent.rotation_z;
    parent_q[3] = parent.rotation_w;
    length = sqrtf (parent_q[0] * parent_q[0] + parent_q[1] * parent_q[1] +
                    parent_q[2] * parent_q[2] + parent_q[3] * parent_q[3]);
    if (length <= 1.0e-8f)
        return dyn_error (error, LRG_DYNAMICS_ERROR_INVALID,
                          "dynamics parent bone rotation has zero length");
    parent_q[0] /= length;
    parent_q[1] /= length;
    parent_q[2] /= length;
    parent_q[3] /= length;
    delta[0] = body->position[0] - parent.position_x;
    delta[1] = body->position[1] - parent.position_y;
    delta[2] = body->position[2] - parent.position_z;
    quat_inv_rotate (parent_q, delta, local_pos);
    local_pos[0] /= parent.scale_x;
    local_pos[1] /= parent.scale_y;
    local_pos[2] /= parent.scale_z;
    body_q[0] = body->orientation[0];
    body_q[1] = body->orientation[1];
    body_q[2] = body->orientation[2];
    body_q[3] = body->orientation[3];
    conj[0] = -parent_q[0];
    conj[1] = -parent_q[1];
    conj[2] = -parent_q[2];
    conj[3] = parent_q[3];
    quat_mul (conj, body_q, local_q);
    quat_normalize (local_q);
    memset (&local_out, 0, sizeof (local_out));
    local_out.position_x = local_pos[0];
    local_out.position_y = local_pos[1];
    local_out.position_z = local_pos[2];
    local_out.rotation_x = local_q[0];
    local_out.rotation_y = local_q[1];
    local_out.rotation_z = local_q[2];
    local_out.rotation_w = local_q[3];
    local_out.scale_x = scale_x;
    local_out.scale_y = scale_y;
    local_out.scale_z = scale_z;
    /* Always the shipped compose, including a root against identity. */
    lrg_bone_pose_compose_into (&parent, &local_out, &world_out);
    *local = local_out;
    *world_pose = world_out;
    return TRUE;
}
