/* lrg-spatial-camera.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "config.h"
#include <math.h>
#include <graylib.h>
#include "lrg-spatial-camera.h"

/* Exponential-smoothing time constant (seconds): smaller eases faster. */
#define LRG_CAMERA_TAU 0.10f
/* Converged when the summed pose delta drops below this. */
#define LRG_CAMERA_EPSILON 0.01f

struct _LrgSpatialCamera
{
	GObject parent_instance;

	GrlCamera3D *cam;
	LrgPose     *current;
	LrgPose     *target;
	gboolean     animating;
};

G_DEFINE_FINAL_TYPE (LrgSpatialCamera, lrg_spatial_camera, G_TYPE_OBJECT)

static LrgPose *
default_pose (void)
{
	/* A ~14 deg elevated 3/4 view (pulled back a touch from head-on) looking at
	   the panel centre.  This reads as 3D immediately -- the panel shows real
	   perspective and the environment floor/grid recedes into the distance --
	   while keeping the whole panel in view and the text comfortably legible.
	   Camera moves (C-c 3) orbit/zoom around this.  */
	return lrg_pose_new (0.0f, 1.6f, 6.6f,
						 0.0f, 0.0f, 0.0f,
						 0.0f, 1.0f, 0.0f,
						 45.0f);
}

/* Summed absolute difference of the animated components (position + target +
   fovy).  Used to decide when an ease has converged.  */
static gfloat
pose_delta (const LrgPose *a,
			const LrgPose *b)
{
	gfloat apx, apy, apz, atx, aty, atz;
	gfloat bpx, bpy, bpz, btx, bty, btz;
	gfloat d = 0.0f;

	lrg_pose_get_position (a, &apx, &apy, &apz);
	lrg_pose_get_target (a, &atx, &aty, &atz);
	lrg_pose_get_position (b, &bpx, &bpy, &bpz);
	lrg_pose_get_target (b, &btx, &bty, &btz);

	d += fabsf (apx - bpx) + fabsf (apy - bpy) + fabsf (apz - bpz);
	d += fabsf (atx - btx) + fabsf (aty - bty) + fabsf (atz - btz);
	d += fabsf (lrg_pose_get_fovy (a) - lrg_pose_get_fovy (b));

	return d;
}

/* Push the current pose into the underlying GrlCamera3D.  Up is left at the
   default (0,1,0); panels never roll.  */
static void
apply_pose (LrgSpatialCamera *self)
{
	gfloat px, py, pz, tx, ty, tz;

	lrg_pose_get_position (self->current, &px, &py, &pz);
	lrg_pose_get_target (self->current, &tx, &ty, &tz);

	grl_camera3d_set_position_xyz (self->cam, px, py, pz);
	grl_camera3d_set_target_xyz (self->cam, tx, ty, tz);
	grl_camera3d_set_fovy (self->cam, lrg_pose_get_fovy (self->current));
}

LrgSpatialCamera *
lrg_spatial_camera_new (void)
{
	return g_object_new (LRG_TYPE_SPATIAL_CAMERA, NULL);
}

void
lrg_spatial_camera_set_target_pose (LrgSpatialCamera *self,
									const LrgPose    *pose)
{
	g_return_if_fail (LRG_IS_SPATIAL_CAMERA (self));
	g_return_if_fail (pose != NULL);

	g_clear_pointer (&self->target, lrg_pose_free);
	self->target = lrg_pose_copy (pose);
	self->animating = (pose_delta (self->current, self->target) > LRG_CAMERA_EPSILON);
}

void
lrg_spatial_camera_set_pose (LrgSpatialCamera *self,
							 const LrgPose    *pose)
{
	g_return_if_fail (LRG_IS_SPATIAL_CAMERA (self));
	g_return_if_fail (pose != NULL);

	g_clear_pointer (&self->current, lrg_pose_free);
	g_clear_pointer (&self->target, lrg_pose_free);
	self->current = lrg_pose_copy (pose);
	self->target = lrg_pose_copy (pose);
	self->animating = FALSE;
	apply_pose (self);
}

LrgPose *
lrg_spatial_camera_get_pose (LrgSpatialCamera *self)
{
	g_return_val_if_fail (LRG_IS_SPATIAL_CAMERA (self), NULL);
	return lrg_pose_copy (self->current);
}

gboolean
lrg_spatial_camera_step (LrgSpatialCamera *self,
						 gfloat            dt)
{
	LrgPose *eased;
	gfloat f;

	g_return_val_if_fail (LRG_IS_SPATIAL_CAMERA (self), FALSE);

	if (!self->animating)
		return FALSE;

	if (dt < 0.0f)
		dt = 0.0f;
	f = 1.0f - expf (-dt / LRG_CAMERA_TAU);

	eased = lrg_pose_lerp (self->current, self->target, f);
	g_clear_pointer (&self->current, lrg_pose_free);
	self->current = eased;

	if (pose_delta (self->current, self->target) <= LRG_CAMERA_EPSILON)
	{
		g_clear_pointer (&self->current, lrg_pose_free);
		self->current = lrg_pose_copy (self->target);
		self->animating = FALSE;
	}

	apply_pose (self);
	return self->animating;
}

gboolean
lrg_spatial_camera_is_animating (LrgSpatialCamera *self)
{
	g_return_val_if_fail (LRG_IS_SPATIAL_CAMERA (self), FALSE);
	return self->animating;
}

void
lrg_spatial_camera_begin (LrgSpatialCamera *self)
{
	g_return_if_fail (LRG_IS_SPATIAL_CAMERA (self));
	apply_pose (self);
	grl_camera3d_begin (self->cam);
}

void
lrg_spatial_camera_end (LrgSpatialCamera *self)
{
	g_return_if_fail (LRG_IS_SPATIAL_CAMERA (self));
	grl_camera3d_end (self->cam);
}

void
lrg_spatial_camera_reset (LrgSpatialCamera *self)
{
    g_autoptr (LrgPose) p = NULL;

    g_return_if_fail (LRG_IS_SPATIAL_CAMERA (self));
    p = default_pose ();
    lrg_spatial_camera_set_target_pose (self, p);
}

void
lrg_spatial_camera_zoom (LrgSpatialCamera *self,
                         gfloat            factor)
{
    gfloat px, py, pz, tx, ty, tz;
    g_autoptr (LrgPose) p = NULL;

    g_return_if_fail (LRG_IS_SPATIAL_CAMERA (self));
    if (factor <= 0.0f)
        return;

    lrg_pose_get_position (self->current, &px, &py, &pz);
    lrg_pose_get_target (self->current, &tx, &ty, &tz);
    px = tx + (px - tx) * factor;
    py = ty + (py - ty) * factor;
    pz = tz + (pz - tz) * factor;
    p = lrg_pose_new (px, py, pz, tx, ty, tz, 0.0f, 1.0f, 0.0f,
                      lrg_pose_get_fovy (self->current));
    lrg_spatial_camera_set_target_pose (self, p);
}

/* Compute the pose obtained by orbiting the current eye around the target by
   (@dyaw, @dpitch) degrees.  Returns NULL if the eye sits on the target.
   (transfer full) */
static LrgPose *
orbit_pose (LrgSpatialCamera *self,
            gfloat            dyaw,
            gfloat            dpitch)
{
    gfloat px, py, pz, tx, ty, tz, ox, oy, oz, r, az, el;

    lrg_pose_get_position (self->current, &px, &py, &pz);
    lrg_pose_get_target (self->current, &tx, &ty, &tz);
    ox = px - tx;
    oy = py - ty;
    oz = pz - tz;
    r = sqrtf (ox * ox + oy * oy + oz * oz);
    if (r < 0.0001f)
        return NULL;

    az = atan2f (ox, oz);
    el = asinf (oy / r);
    az += dyaw * (gfloat) (G_PI / 180.0);
    el += dpitch * (gfloat) (G_PI / 180.0);
    /* Clamp elevation so the orbit never flips over the poles. */
    if (el > 1.5f)
        el = 1.5f;
    else if (el < -1.5f)
        el = -1.5f;

    ox = r * cosf (el) * sinf (az);
    oy = r * sinf (el);
    oz = r * cosf (el) * cosf (az);
    return lrg_pose_new (tx + ox, ty + oy, tz + oz, tx, ty, tz, 0.0f, 1.0f, 0.0f,
                         lrg_pose_get_fovy (self->current));
}

void
lrg_spatial_camera_look_drag (LrgSpatialCamera *self,
                              gfloat            dyaw,
                              gfloat            dpitch)
{
    gfloat ex, ey, ez, tx, ty, tz, dx, dy, dz, r, az, el;
    g_autoptr (LrgPose) p = NULL;

    g_return_if_fail (LRG_IS_SPATIAL_CAMERA (self));

    lrg_pose_get_position (self->current, &ex, &ey, &ez);
    lrg_pose_get_target (self->current, &tx, &ty, &tz);
    dx = tx - ex;
    dy = ty - ey;
    dz = tz - ez;
    r = sqrtf (dx * dx + dy * dy + dz * dz);
    if (r < 0.0001f)
        return;

    /* Rotate the look direction about the (fixed) eye. */
    az = atan2f (dx, dz);
    el = asinf (dy / r);
    az += dyaw * (gfloat) (G_PI / 180.0);
    el += dpitch * (gfloat) (G_PI / 180.0);
    if (el > 1.5f)
        el = 1.5f;
    else if (el < -1.5f)
        el = -1.5f;

    dx = r * cosf (el) * sinf (az);
    dy = r * sinf (el);
    dz = r * cosf (el) * cosf (az);
    p = lrg_pose_new (ex, ey, ez, ex + dx, ey + dy, ez + dz, 0.0f, 1.0f, 0.0f,
                      lrg_pose_get_fovy (self->current));
    lrg_spatial_camera_set_pose (self, p);
}

void
lrg_spatial_camera_dolly_drag (LrgSpatialCamera *self,
                               gfloat            factor)
{
    gfloat px, py, pz, tx, ty, tz;
    g_autoptr (LrgPose) p = NULL;

    g_return_if_fail (LRG_IS_SPATIAL_CAMERA (self));
    if (factor <= 0.0f)
        return;

    lrg_pose_get_position (self->current, &px, &py, &pz);
    lrg_pose_get_target (self->current, &tx, &ty, &tz);
    px = tx + (px - tx) * factor;
    py = ty + (py - ty) * factor;
    pz = tz + (pz - tz) * factor;
    p = lrg_pose_new (px, py, pz, tx, ty, tz, 0.0f, 1.0f, 0.0f,
                      lrg_pose_get_fovy (self->current));
    lrg_spatial_camera_set_pose (self, p);
}

void
lrg_spatial_camera_orbit (LrgSpatialCamera *self,
                          gfloat            dyaw,
                          gfloat            dpitch)
{
    g_autoptr (LrgPose) p = NULL;

    g_return_if_fail (LRG_IS_SPATIAL_CAMERA (self));

    p = orbit_pose (self, dyaw, dpitch);
    if (p != NULL)
        lrg_spatial_camera_set_target_pose (self, p);
}

void
lrg_spatial_camera_orbit_drag (LrgSpatialCamera *self,
                               gfloat            dyaw,
                               gfloat            dpitch)
{
    g_autoptr (LrgPose) p = NULL;

    g_return_if_fail (LRG_IS_SPATIAL_CAMERA (self));

    p = orbit_pose (self, dyaw, dpitch);
    if (p != NULL)
        lrg_spatial_camera_set_pose (self, p);
}

void
lrg_spatial_camera_orbit_around_drag (LrgSpatialCamera *self,
                                      gfloat            px,
                                      gfloat            py,
                                      gfloat            pz,
                                      gfloat            dyaw,
                                      gfloat            dpitch)
{
    gfloat ex, ey, ez, ox, oy, oz, r, az, el;
    g_autoptr (LrgPose) p = NULL;

    g_return_if_fail (LRG_IS_SPATIAL_CAMERA (self));

    lrg_pose_get_position (self->current, &ex, &ey, &ez);
    ox = ex - px;
    oy = ey - py;
    oz = ez - pz;
    r = sqrtf (ox * ox + oy * oy + oz * oz);
    if (r < 0.0001f)
        return;

    az = atan2f (ox, oz);
    el = asinf (oy / r);
    az += dyaw * (gfloat) (G_PI / 180.0);
    el += dpitch * (gfloat) (G_PI / 180.0);
    if (el > 1.5f)
        el = 1.5f;
    else if (el < -1.5f)
        el = -1.5f;

    ox = r * cosf (el) * sinf (az);
    oy = r * sinf (el);
    oz = r * cosf (el) * cosf (az);
    /* Look at the pivot: a turntable around @p. */
    p = lrg_pose_new (px + ox, py + oy, pz + oz, px, py, pz, 0.0f, 1.0f, 0.0f,
                      lrg_pose_get_fovy (self->current));
    lrg_spatial_camera_set_pose (self, p);
}

void
lrg_spatial_camera_pan_drag (LrgSpatialCamera *self,
                             gfloat            dx,
                             gfloat            dy)
{
    gfloat px, py, pz, tx, ty, tz;
    gfloat fx, fy, fz, rx, ry, rz, ux, uy, uz, len;
    g_autoptr (LrgPose) p = NULL;

    g_return_if_fail (LRG_IS_SPATIAL_CAMERA (self));

    lrg_pose_get_position (self->current, &px, &py, &pz);
    lrg_pose_get_target (self->current, &tx, &ty, &tz);

    /* forward = normalize(target - eye) */
    fx = tx - px; fy = ty - py; fz = tz - pz;
    len = sqrtf (fx * fx + fy * fy + fz * fz);
    if (len < 0.0001f)
        return;
    fx /= len; fy /= len; fz /= len;

    /* right = normalize(forward x worldUp), worldUp = (0,1,0) => (-fz, 0, fx) */
    rx = -fz;
    ry = 0.0f;
    rz = fx;
    len = sqrtf (rx * rx + ry * ry + rz * rz);
    if (len < 0.0001f)
        return;
    rx /= len; ry /= len; rz /= len;

    /* up = right x forward (orthonormal) */
    ux = ry * fz - rz * fy;
    uy = rz * fx - rx * fz;
    uz = rx * fy - ry * fx;

    /* Drag right -> scene shifts right -> camera slides left; drag up similarly. */
    px -= rx * dx; py -= ry * dx; pz -= rz * dx;
    tx -= rx * dx; ty -= ry * dx; tz -= rz * dx;
    px += ux * dy; py += uy * dy; pz += uz * dy;
    tx += ux * dy; ty += uy * dy; tz += uz * dy;

    p = lrg_pose_new (px, py, pz, tx, ty, tz, 0.0f, 1.0f, 0.0f,
                      lrg_pose_get_fovy (self->current));
    lrg_spatial_camera_set_pose (self, p);
}

static void
lrg_spatial_camera_dispose (GObject *object)
{
	LrgSpatialCamera *self = LRG_SPATIAL_CAMERA (object);

	g_clear_object (&self->cam);
	g_clear_pointer (&self->current, lrg_pose_free);
	g_clear_pointer (&self->target, lrg_pose_free);

	G_OBJECT_CLASS (lrg_spatial_camera_parent_class)->dispose (object);
}

static void
lrg_spatial_camera_class_init (LrgSpatialCameraClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->dispose = lrg_spatial_camera_dispose;
}

static void
lrg_spatial_camera_init (LrgSpatialCamera *self)
{
	self->cam = grl_camera3d_new ();
	self->current = default_pose ();
	self->target = default_pose ();
	self->animating = FALSE;
	apply_pose (self);
}
