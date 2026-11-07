/* lrg-camera-component.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgCameraComponent: an #LrgCamera3D driven by the owning entity, and its
 * registration in the world's camera registry.
 */

#include "config.h"

#include "lrg-camera-component.h"
#include "../lrg-entity3d.h"
#include "../../graphics/lrg-camera.h"

struct _LrgCameraComponent
{
	LrgComponent  parent_instance;

	LrgCamera3D  *camera;
	gfloat        fov;
	gfloat        near_plane;
	gfloat        far_plane;
	gboolean      orthographic;
	gfloat        ortho_size;
	gfloat        aspect;
	guint32       cull_mask;
	gint          priority;
	gboolean      active;

	LrgWorld3D   *world;   /* weak */
};

G_DEFINE_FINAL_TYPE (LrgCameraComponent, lrg_camera_component, LRG_TYPE_COMPONENT)

enum
{
	PROP_0,
	PROP_CAMERA,
	PROP_FOV,
	PROP_NEAR,
	PROP_FAR,
	PROP_ORTHOGRAPHIC,
	PROP_ORTHO_SIZE,
	PROP_ASPECT,
	PROP_CULL_MASK,
	PROP_PRIORITY,
	PROP_ACTIVE,
	N_PROPS
};

static GParamSpec *properties[N_PROPS];

/* Quark of the camera -> component back pointer */
static GQuark
owner_quark (void)
{
	static GQuark quark = 0;

	if (quark == 0)
		quark = g_quark_from_static_string ("lrg-camera-component-owner");
	return quark;
}

/* ==========================================================================
 * Registration
 * ========================================================================== */

static void
set_world (LrgCameraComponent *self,
           LrgWorld3D         *world)
{
	if (self->world == world)
		return;
	if (self->world != NULL)
		g_object_remove_weak_pointer (G_OBJECT (self->world), (gpointer *) &self->world);
	self->world = world;
	if (self->world != NULL)
		g_object_add_weak_pointer (G_OBJECT (self->world), (gpointer *) &self->world);
}

/* Registers the camera with its priority while active and enabled,
 * unregisters it otherwise. */
static void
update_registration (LrgCameraComponent *self)
{
	if (self->world == NULL)
		return;
	if (self->active && lrg_component_get_enabled (LRG_COMPONENT (self)))
		lrg_world3d_register_camera (self->world, LRG_CAMERA (self->camera), self->priority);
	else
		lrg_world3d_unregister_camera (self->world, LRG_CAMERA (self->camera));
}

static void
lrg_camera_component_world_entered (LrgComponent *component,
                                    LrgWorld3D   *world)
{
	LrgCameraComponent *self = LRG_CAMERA_COMPONENT (component);

	set_world (self, world);
	lrg_camera_component_sync (self);
	update_registration (self);
}

static void
lrg_camera_component_world_exited (LrgComponent *component,
                                   LrgWorld3D   *world)
{
	LrgCameraComponent *self = LRG_CAMERA_COMPONENT (component);

	lrg_world3d_unregister_camera (world, LRG_CAMERA (self->camera));
	if (self->world == world)
		set_world (self, NULL);
}

/* Cameras follow their entity after every update ran. */
static void
lrg_camera_component_late_update (LrgComponent *component,
                                  gfloat        delta)
{
	(void) delta;
	lrg_camera_component_sync (LRG_CAMERA_COMPONENT (component));
}

/* Projection changes reach the camera at once; activity changes the
 * registration. */
static void
on_notify (GObject    *object,
           GParamSpec *pspec,
           gpointer    user_data)
{
	LrgCameraComponent *self = LRG_CAMERA_COMPONENT (object);

	(void) user_data;
	if (g_strcmp0 (pspec->name, "enabled") == 0 || pspec == properties[PROP_ACTIVE] ||
	    pspec == properties[PROP_PRIORITY])
		update_registration (self);
	else
		lrg_camera_component_sync (self);
}

/* ==========================================================================
 * GObject
 * ========================================================================== */

static void
lrg_camera_component_dispose (GObject *object)
{
	LrgCameraComponent *self = LRG_CAMERA_COMPONENT (object);

	if (self->world != NULL && self->camera != NULL)
		lrg_world3d_unregister_camera (self->world, LRG_CAMERA (self->camera));
	set_world (self, NULL);

	/* The camera may outlive us (another holder): drop the back pointer */
	if (self->camera != NULL)
		g_object_set_qdata (G_OBJECT (self->camera), owner_quark (), NULL);
	g_clear_object (&self->camera);

	G_OBJECT_CLASS (lrg_camera_component_parent_class)->dispose (object);
}

static void
lrg_camera_component_get_property (GObject    *object,
                                   guint       prop_id,
                                   GValue     *value,
                                   GParamSpec *pspec)
{
	LrgCameraComponent *self = LRG_CAMERA_COMPONENT (object);

	switch (prop_id)
	{
	case PROP_CAMERA:       g_value_set_object (value, self->camera); break;
	case PROP_FOV:          g_value_set_float (value, self->fov); break;
	case PROP_NEAR:         g_value_set_float (value, self->near_plane); break;
	case PROP_FAR:          g_value_set_float (value, self->far_plane); break;
	case PROP_ORTHOGRAPHIC: g_value_set_boolean (value, self->orthographic); break;
	case PROP_ORTHO_SIZE:   g_value_set_float (value, self->ortho_size); break;
	case PROP_ASPECT:       g_value_set_float (value, self->aspect); break;
	case PROP_CULL_MASK:    g_value_set_uint (value, (guint) self->cull_mask); break;
	case PROP_PRIORITY:     g_value_set_int (value, self->priority); break;
	case PROP_ACTIVE:       g_value_set_boolean (value, self->active); break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

/* Stores a float and notifies when it changed. */
static void
set_float (LrgCameraComponent *self,
           gfloat             *slot,
           gfloat              value,
           guint               prop)
{
	if (*slot == value)
		return;
	*slot = value;
	g_object_notify_by_pspec (G_OBJECT (self), properties[prop]);
}

static void
lrg_camera_component_set_property (GObject      *object,
                                   guint         prop_id,
                                   const GValue *value,
                                   GParamSpec   *pspec)
{
	LrgCameraComponent *self = LRG_CAMERA_COMPONENT (object);

	switch (prop_id)
	{
	case PROP_FOV:
		lrg_camera_component_set_fov (self, g_value_get_float (value));
		break;
	case PROP_NEAR:
		lrg_camera_component_set_clip (self, g_value_get_float (value), MAX (self->far_plane,
		                               g_value_get_float (value) + 0.001f));
		break;
	case PROP_FAR:
		lrg_camera_component_set_clip (self, self->near_plane, g_value_get_float (value));
		break;
	case PROP_ORTHOGRAPHIC:
		lrg_camera_component_set_orthographic (self, g_value_get_boolean (value));
		break;
	case PROP_ORTHO_SIZE:
		set_float (self, &self->ortho_size, g_value_get_float (value), prop_id);
		break;
	case PROP_ASPECT:
		set_float (self, &self->aspect, g_value_get_float (value), prop_id);
		break;
	case PROP_CULL_MASK:
		if (self->cull_mask != (guint32) g_value_get_uint (value))
		{
			self->cull_mask = (guint32) g_value_get_uint (value);
			g_object_notify_by_pspec (object, pspec);
		}
		break;
	case PROP_PRIORITY:
		lrg_camera_component_set_priority (self, g_value_get_int (value));
		break;
	case PROP_ACTIVE:
		lrg_camera_component_set_active (self, g_value_get_boolean (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_camera_component_class_init (LrgCameraComponentClass *klass)
{
	GObjectClass      *object_class = G_OBJECT_CLASS (klass);
	LrgComponentClass *component_class = LRG_COMPONENT_CLASS (klass);
	const GParamFlags  rw = G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY;

	object_class->dispose = lrg_camera_component_dispose;
	object_class->get_property = lrg_camera_component_get_property;
	object_class->set_property = lrg_camera_component_set_property;

	component_class->world_entered = lrg_camera_component_world_entered;
	component_class->world_exited = lrg_camera_component_world_exited;
	component_class->late_update = lrg_camera_component_late_update;

	/**
	 * LrgCameraComponent:camera:
	 *
	 * The #LrgCamera3D the component drives (read-only).
	 */
	properties[PROP_CAMERA] =
		g_param_spec_object ("camera", NULL, "Driven camera", LRG_TYPE_CAMERA3D,
		                     G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

	/**
	 * LrgCameraComponent:fov:
	 *
	 * Vertical field of view in degrees (perspective).
	 */
	properties[PROP_FOV] = g_param_spec_float ("fov", NULL, "Vertical FOV (degrees)", 1.0f, 179.0f,
	                                           60.0f, rw);

	/**
	 * LrgCameraComponent:near:
	 *
	 * Near clip distance.
	 */
	properties[PROP_NEAR] = g_param_spec_float ("near", NULL, "Near clip", 0.0001f, G_MAXFLOAT,
	                                            0.1f, rw);

	/**
	 * LrgCameraComponent:far:
	 *
	 * Far clip distance.
	 */
	properties[PROP_FAR] = g_param_spec_float ("far", NULL, "Far clip", 0.001f, G_MAXFLOAT,
	                                           1000.0f, rw);

	/**
	 * LrgCameraComponent:orthographic:
	 *
	 * Orthographic instead of perspective projection.
	 */
	properties[PROP_ORTHOGRAPHIC] = g_param_spec_boolean ("orthographic", NULL, "Orthographic",
	                                                      FALSE, rw);

	/**
	 * LrgCameraComponent:ortho-size:
	 *
	 * World units visible vertically with an orthographic projection.
	 */
	properties[PROP_ORTHO_SIZE] = g_param_spec_float ("ortho-size", NULL, "Ortho view height",
	                                                  0.001f, G_MAXFLOAT, 10.0f, rw);

	/**
	 * LrgCameraComponent:aspect:
	 *
	 * Aspect override (width / height), 0 for the render target's own.
	 */
	properties[PROP_ASPECT] = g_param_spec_float ("aspect", NULL, "Aspect override", 0.0f, 16.0f,
	                                              0.0f, rw);

	/**
	 * LrgCameraComponent:cull-mask:
	 *
	 * Render layers the camera sees (copied to #LrgCamera:cull-mask).
	 */
	properties[PROP_CULL_MASK] = g_param_spec_uint ("cull-mask", NULL, "Visible render layers", 0,
	                                                G_MAXUINT32, LRG_RENDER_LAYERS_MASK_ALL, rw);

	/**
	 * LrgCameraComponent:priority:
	 *
	 * Camera registry priority; the highest active camera is the world's
	 * active camera unless one is forced.
	 */
	properties[PROP_PRIORITY] = g_param_spec_int ("priority", NULL, "Selection priority",
	                                              G_MININT, G_MAXINT, 0, rw);

	/**
	 * LrgCameraComponent:active:
	 *
	 * Whether the camera takes part in the world's camera selection.
	 */
	properties[PROP_ACTIVE] = g_param_spec_boolean ("active", NULL, "Registered with the world",
	                                                TRUE, rw);

	g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_camera_component_init (LrgCameraComponent *self)
{
	self->fov = 60.0f;
	self->near_plane = 0.1f;
	self->far_plane = 1000.0f;
	self->ortho_size = 10.0f;
	self->cull_mask = LRG_RENDER_LAYERS_MASK_ALL;
	self->active = TRUE;

	self->camera = lrg_camera3d_new ();
	g_object_set_qdata (G_OBJECT (self->camera), owner_quark (), self);
	lrg_camera_component_sync (self);

	g_signal_connect (self, "notify", G_CALLBACK (on_notify), NULL);
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

LrgCameraComponent *
lrg_camera_component_new (void)
{
	return (LrgCameraComponent *) g_object_new (LRG_TYPE_CAMERA_COMPONENT, NULL);
}

LrgCamera3D *
lrg_camera_component_get_camera (LrgCameraComponent *self)
{
	g_return_val_if_fail (LRG_IS_CAMERA_COMPONENT (self), NULL);

	return self->camera;
}

LrgCameraComponent *
lrg_camera_component_from_camera (LrgCamera *camera)
{
	g_return_val_if_fail (LRG_IS_CAMERA (camera), NULL);

	return (LrgCameraComponent *) g_object_get_qdata (G_OBJECT (camera), owner_quark ());
}

gfloat
lrg_camera_component_get_fov (LrgCameraComponent *self)
{
	g_return_val_if_fail (LRG_IS_CAMERA_COMPONENT (self), 60.0f);

	return self->fov;
}

void
lrg_camera_component_set_fov (LrgCameraComponent *self,
                              gfloat              fov)
{
	g_return_if_fail (LRG_IS_CAMERA_COMPONENT (self));

	set_float (self, &self->fov, CLAMP (fov, 1.0f, 179.0f), PROP_FOV);
}

gfloat
lrg_camera_component_get_near (LrgCameraComponent *self)
{
	g_return_val_if_fail (LRG_IS_CAMERA_COMPONENT (self), 0.1f);

	return self->near_plane;
}

gfloat
lrg_camera_component_get_far (LrgCameraComponent *self)
{
	g_return_val_if_fail (LRG_IS_CAMERA_COMPONENT (self), 1000.0f);

	return self->far_plane;
}

void
lrg_camera_component_set_clip (LrgCameraComponent *self,
                               gfloat              near_plane,
                               gfloat              far_plane)
{
	g_return_if_fail (LRG_IS_CAMERA_COMPONENT (self));

	near_plane = MAX (near_plane, 0.0001f);
	far_plane = MAX (far_plane, near_plane + 0.001f);
	g_object_freeze_notify (G_OBJECT (self));
	set_float (self, &self->near_plane, near_plane, PROP_NEAR);
	set_float (self, &self->far_plane, far_plane, PROP_FAR);
	g_object_thaw_notify (G_OBJECT (self));
}

gboolean
lrg_camera_component_get_orthographic (LrgCameraComponent *self)
{
	g_return_val_if_fail (LRG_IS_CAMERA_COMPONENT (self), FALSE);

	return self->orthographic;
}

void
lrg_camera_component_set_orthographic (LrgCameraComponent *self,
                                       gboolean            orthographic)
{
	g_return_if_fail (LRG_IS_CAMERA_COMPONENT (self));

	orthographic = !!orthographic;
	if (self->orthographic == orthographic)
		return;
	self->orthographic = orthographic;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ORTHOGRAPHIC]);
}

gint
lrg_camera_component_get_priority (LrgCameraComponent *self)
{
	g_return_val_if_fail (LRG_IS_CAMERA_COMPONENT (self), 0);

	return self->priority;
}

void
lrg_camera_component_set_priority (LrgCameraComponent *self,
                                   gint                priority)
{
	g_return_if_fail (LRG_IS_CAMERA_COMPONENT (self));

	if (self->priority == priority)
		return;
	self->priority = priority;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_PRIORITY]);
}

gboolean
lrg_camera_component_get_active (LrgCameraComponent *self)
{
	g_return_val_if_fail (LRG_IS_CAMERA_COMPONENT (self), FALSE);

	return self->active;
}

void
lrg_camera_component_set_active (LrgCameraComponent *self,
                                 gboolean            active)
{
	g_return_if_fail (LRG_IS_CAMERA_COMPONENT (self));

	active = !!active;
	if (self->active == active)
		return;
	self->active = active;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ACTIVE]);
}

gboolean
lrg_camera_component_is_current (LrgCameraComponent *self)
{
	g_return_val_if_fail (LRG_IS_CAMERA_COMPONENT (self), FALSE);

	return self->world != NULL &&
	       lrg_world3d_get_active_camera (self->world) == LRG_CAMERA (self->camera);
}

void
lrg_camera_component_make_current (LrgCameraComponent *self)
{
	g_return_if_fail (LRG_IS_CAMERA_COMPONENT (self));

	if (self->world != NULL)
		lrg_world3d_set_active_camera (self->world, LRG_CAMERA (self->camera));
}

void
lrg_camera_component_sync (LrgCameraComponent *self)
{
	LrgEntity3D *entity;

	g_return_if_fail (LRG_IS_CAMERA_COMPONENT (self));

	if (self->camera == NULL)
		return;

	/* Projection: raylib reads fovy as the view height when orthographic */
	lrg_camera3d_set_projection (self->camera, self->orthographic ? LRG_PROJECTION_ORTHOGRAPHIC
	                                                              : LRG_PROJECTION_PERSPECTIVE);
	lrg_camera3d_set_fovy (self->camera, self->orthographic ? self->ortho_size : self->fov);
	lrg_camera3d_set_aspect (self->camera, self->aspect);
	lrg_camera_set_cull_mask (LRG_CAMERA (self->camera), self->cull_mask);

	/* Pose: eye at the entity, looking down its local -Z, up its local +Y */
	entity = lrg_component_get_entity (LRG_COMPONENT (self));
	if (entity != NULL)
	{
		gfloat pos[3], fwd[3], up[3];

		lrg_entity3d_get_world_position (entity, pos);
		lrg_entity3d_get_forward (entity, fwd);
		lrg_entity3d_get_up (entity, up);
		lrg_camera3d_set_position_xyz (self->camera, pos[0], pos[1], pos[2]);
		lrg_camera3d_set_target_xyz (self->camera, pos[0] + fwd[0], pos[1] + fwd[1], pos[2] + fwd[2]);
		lrg_camera3d_set_up_xyz (self->camera, up[0], up[1], up[2]);
	}
}
