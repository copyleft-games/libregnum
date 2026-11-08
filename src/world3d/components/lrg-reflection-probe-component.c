/* lrg-reflection-probe-component.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgReflectionProbeComponent: owns an LrgReflectionProbe, keeps it on
 * the entity and registered with the world's environment, and tracks
 * pending captures.
 *
 * Pending captures are counted against the probe's capture counter: a
 * request remembers the counter value it was made at, and any capture
 * after that (by the component, the renderer, or anyone holding the
 * probe) satisfies it.  So the renderer can capture the environment's
 * probes directly without knowing about components.
 */

#include "config.h"

#include <math.h>
#include <string.h>

#include "lrg-reflection-probe-component.h"
#include "../lrg-entity3d.h"
#include "../../lighting/lrg-environment.h"

struct _LrgReflectionProbeComponent
{
	LrgComponent        parent_instance;

	LrgReflectionProbe *probe;
	gfloat              extents[3];      /* authored half sizes */
	gfloat              near_plane;
	gfloat              far_plane;
	gboolean            auto_recapture;

	gboolean            pending;         /* a capture was requested ... */
	guint               pending_at;      /* ... when the probe had this many */

	gboolean            placed;          /* position / box written once */
	gfloat              last_position[3];
	gfloat              last_box[3];

	LrgEnvironment     *registered;      /* the environment holding the probe (ref) */
};

G_DEFINE_FINAL_TYPE (LrgReflectionProbeComponent, lrg_reflection_probe_component, LRG_TYPE_COMPONENT)

enum
{
	PROP_0,
	PROP_EXTENTS_X,
	PROP_EXTENTS_Y,
	PROP_EXTENTS_Z,
	PROP_BLEND_DISTANCE,
	PROP_INTENSITY,
	PROP_BOX_PROJECTION,
	PROP_RESOLUTION,
	PROP_NEAR_CLIP,
	PROP_FAR_CLIP,
	PROP_AUTO_RECAPTURE,
	PROP_NEEDS_CAPTURE,
	N_PROPS
};

static GParamSpec *properties[N_PROPS];

/* ==========================================================================
 * Helpers
 * ========================================================================== */

/* mark_pending: requests a capture (satisfied by the next one) */
static void
mark_pending (LrgReflectionProbeComponent *self)
{
	gboolean was = lrg_reflection_probe_component_get_needs_capture (self);

	self->pending = TRUE;
	self->pending_at = lrg_reflection_probe_get_capture_count (self->probe);
	if (!was)
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_NEEDS_CAPTURE]);
}

/* register_with: moves the probe into @env (NULL: out of every environment) */
static void
register_with (LrgReflectionProbeComponent *self,
               LrgEnvironment              *env)
{
	if (self->registered == env)
		return;
	if (self->registered != NULL)
	{
		lrg_environment_remove_probe (self->registered, self->probe);
		g_clear_object (&self->registered);
	}
	if (env != NULL)
	{
		lrg_environment_add_probe (env, self->probe);
		self->registered = (LrgEnvironment *) g_object_ref (env);
	}
}

/* is_live: enabled, on an entity that is active in its hierarchy */
static gboolean
is_live (LrgReflectionProbeComponent *self)
{
	LrgEntity3D *entity = lrg_component_get_entity (LRG_COMPONENT (self));

	return entity != NULL && lrg_component_get_enabled (LRG_COMPONENT (self))
	       && lrg_entity3d_is_active_in_hierarchy (entity);
}

/* ==========================================================================
 * LrgComponent vfuncs
 * ========================================================================== */

static void
lrg_reflection_probe_component_world_entered (LrgComponent *component,
                                              LrgWorld3D   *world)
{
	LrgReflectionProbeComponent *self = LRG_REFLECTION_PROBE_COMPONENT (component);

	(void) world;
	/* a probe that enters a world is captured once, whatever the mode */
	mark_pending (self);
	lrg_reflection_probe_component_sync (self);
}

static void
lrg_reflection_probe_component_world_exited (LrgComponent *component,
                                             LrgWorld3D   *world)
{
	LrgReflectionProbeComponent *self = LRG_REFLECTION_PROBE_COMPONENT (component);

	(void) world;
	register_with (self, NULL);
	self->placed = FALSE;
}

static void
lrg_reflection_probe_component_late_update (LrgComponent *component,
                                            gfloat        delta)
{
	(void) delta;
	lrg_reflection_probe_component_sync (LRG_REFLECTION_PROBE_COMPONENT (component));
}

static gboolean
lrg_reflection_probe_component_get_bounds (LrgComponent     *component,
                                           LrgBoundingBox3D *out_local)
{
	LrgReflectionProbeComponent *self = LRG_REFLECTION_PROBE_COMPONENT (component);

	/* local box: the authored extents (the entity's scale applies on top) */
	out_local->min.x = -self->extents[0];
	out_local->min.y = -self->extents[1];
	out_local->min.z = -self->extents[2];
	out_local->max.x = self->extents[0];
	out_local->max.y = self->extents[1];
	out_local->max.z = self->extents[2];
	return TRUE;
}

/* ==========================================================================
 * GObject
 * ========================================================================== */

static void
lrg_reflection_probe_component_dispose (GObject *object)
{
	LrgReflectionProbeComponent *self = LRG_REFLECTION_PROBE_COMPONENT (object);

	if (self->probe != NULL)
		register_with (self, NULL);
	g_clear_object (&self->probe);

	G_OBJECT_CLASS (lrg_reflection_probe_component_parent_class)->dispose (object);
}

static void
lrg_reflection_probe_component_get_property (GObject    *object,
                                             guint       prop_id,
                                             GValue     *value,
                                             GParamSpec *pspec)
{
	LrgReflectionProbeComponent *self = LRG_REFLECTION_PROBE_COMPONENT (object);

	switch (prop_id)
	{
	case PROP_EXTENTS_X:
	case PROP_EXTENTS_Y:
	case PROP_EXTENTS_Z:
		g_value_set_float (value, self->extents[prop_id - PROP_EXTENTS_X]);
		break;
	case PROP_BLEND_DISTANCE:
		g_value_set_float (value, lrg_reflection_probe_get_blend_distance (self->probe));
		break;
	case PROP_INTENSITY:
		g_value_set_float (value, lrg_reflection_probe_get_intensity (self->probe));
		break;
	case PROP_BOX_PROJECTION:
		g_value_set_boolean (value, lrg_reflection_probe_get_box_projection (self->probe));
		break;
	case PROP_RESOLUTION:
		g_value_set_uint (value, lrg_reflection_probe_get_resolution (self->probe));
		break;
	case PROP_NEAR_CLIP:
		g_value_set_float (value, self->near_plane);
		break;
	case PROP_FAR_CLIP:
		g_value_set_float (value, self->far_plane);
		break;
	case PROP_AUTO_RECAPTURE:
		g_value_set_boolean (value, self->auto_recapture);
		break;
	case PROP_NEEDS_CAPTURE:
		g_value_set_boolean (value, lrg_reflection_probe_component_get_needs_capture (self));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

/* set_clip: pushes the clip planes when they are valid together */
static void
set_clip (LrgReflectionProbeComponent *self,
          gfloat                       near_plane,
          gfloat                       far_plane,
          guint                        prop)
{
	if (self->near_plane == near_plane && self->far_plane == far_plane)
		return;
	self->near_plane = near_plane;
	self->far_plane = far_plane;
	if (far_plane > near_plane)
		lrg_reflection_probe_set_clip (self->probe, near_plane, far_plane);
	mark_pending (self);
	g_object_notify_by_pspec (G_OBJECT (self), properties[prop]);
}

static void
lrg_reflection_probe_component_set_property (GObject      *object,
                                             guint         prop_id,
                                             const GValue *value,
                                             GParamSpec   *pspec)
{
	LrgReflectionProbeComponent *self = LRG_REFLECTION_PROBE_COMPONENT (object);

	switch (prop_id)
	{
	case PROP_EXTENTS_X:
	case PROP_EXTENTS_Y:
	case PROP_EXTENTS_Z:
		{
			gfloat e[3];

			memcpy (e, self->extents, sizeof e);
			e[prop_id - PROP_EXTENTS_X] = g_value_get_float (value);
			lrg_reflection_probe_component_set_extents (self, e[0], e[1], e[2]);
		}
		break;
	case PROP_BLEND_DISTANCE:
		if (lrg_reflection_probe_get_blend_distance (self->probe) != g_value_get_float (value))
		{
			lrg_reflection_probe_set_blend_distance (self->probe, g_value_get_float (value));
			g_object_notify_by_pspec (object, pspec);
		}
		break;
	case PROP_INTENSITY:
		if (lrg_reflection_probe_get_intensity (self->probe) != g_value_get_float (value))
		{
			lrg_reflection_probe_set_intensity (self->probe, g_value_get_float (value));
			g_object_notify_by_pspec (object, pspec);
		}
		break;
	case PROP_BOX_PROJECTION:
		if (lrg_reflection_probe_get_box_projection (self->probe) != g_value_get_boolean (value))
		{
			lrg_reflection_probe_set_box_projection (self->probe, g_value_get_boolean (value));
			g_object_notify_by_pspec (object, pspec);
		}
		break;
	case PROP_RESOLUTION:
		{
			guint before = lrg_reflection_probe_get_resolution (self->probe);

			lrg_reflection_probe_set_resolution (self->probe, g_value_get_uint (value));
			if (lrg_reflection_probe_get_resolution (self->probe) != before)
			{
				/* the new size only exists after a capture */
				mark_pending (self);
				g_object_notify_by_pspec (object, pspec);
			}
		}
		break;
	case PROP_NEAR_CLIP:
		set_clip (self, g_value_get_float (value), self->far_plane, PROP_NEAR_CLIP);
		break;
	case PROP_FAR_CLIP:
		set_clip (self, self->near_plane, g_value_get_float (value), PROP_FAR_CLIP);
		break;
	case PROP_AUTO_RECAPTURE:
		lrg_reflection_probe_component_set_auto_recapture (self, g_value_get_boolean (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
lrg_reflection_probe_component_class_init (LrgReflectionProbeComponentClass *klass)
{
	GObjectClass      *object_class = G_OBJECT_CLASS (klass);
	LrgComponentClass *component_class = LRG_COMPONENT_CLASS (klass);
	GParamFlags        rw = G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY;

	object_class->dispose = lrg_reflection_probe_component_dispose;
	object_class->get_property = lrg_reflection_probe_component_get_property;
	object_class->set_property = lrg_reflection_probe_component_set_property;

	component_class->world_entered = lrg_reflection_probe_component_world_entered;
	component_class->world_exited = lrg_reflection_probe_component_world_exited;
	component_class->late_update = lrg_reflection_probe_component_late_update;
	component_class->get_bounds = lrg_reflection_probe_component_get_bounds;

	/**
	 * LrgReflectionProbeComponent:extents-x:
	 *
	 * Half size of the influence / projection box along X, before the
	 * entity's world scale.
	 */
	properties[PROP_EXTENTS_X] =
		g_param_spec_float ("extents-x", NULL, "Box half size X", 0.001f, G_MAXFLOAT, 5.0f, rw);

	/**
	 * LrgReflectionProbeComponent:extents-y:
	 *
	 * Half size of the box along Y, before the entity's world scale.
	 */
	properties[PROP_EXTENTS_Y] =
		g_param_spec_float ("extents-y", NULL, "Box half size Y", 0.001f, G_MAXFLOAT, 5.0f, rw);

	/**
	 * LrgReflectionProbeComponent:extents-z:
	 *
	 * Half size of the box along Z, before the entity's world scale.
	 */
	properties[PROP_EXTENTS_Z] =
		g_param_spec_float ("extents-z", NULL, "Box half size Z", 0.001f, G_MAXFLOAT, 5.0f, rw);

	/**
	 * LrgReflectionProbeComponent:blend-distance:
	 *
	 * Width of the band inside the box faces over which the probe's
	 * influence fades out (lrg_reflection_probe_set_blend_distance()).
	 */
	properties[PROP_BLEND_DISTANCE] =
		g_param_spec_float ("blend-distance", NULL, "Influence fade band", 0.0f, G_MAXFLOAT, 1.0f, rw);

	/**
	 * LrgReflectionProbeComponent:intensity:
	 *
	 * Multiplier of the captured light.
	 */
	properties[PROP_INTENSITY] =
		g_param_spec_float ("intensity", NULL, "Intensity", 0.0f, G_MAXFLOAT, 1.0f, rw);

	/**
	 * LrgReflectionProbeComponent:box-projection:
	 *
	 * Parallax-correct lookups against the box (rooms); off for an
	 * infinitely distant capture (open areas).
	 */
	properties[PROP_BOX_PROJECTION] =
		g_param_spec_boolean ("box-projection", NULL, "Box projection", TRUE, rw);

	/**
	 * LrgReflectionProbeComponent:resolution:
	 *
	 * Cubemap edge in pixels, rounded down to a power of two in
	 * 16 .. 1024.  A change requests a capture.
	 */
	properties[PROP_RESOLUTION] =
		g_param_spec_uint ("resolution", NULL, "Cubemap edge", 16, 1024,
		                   LRG_REFLECTION_PROBE_DEFAULT_RESOLUTION, rw);

	/**
	 * LrgReflectionProbeComponent:near-clip:
	 *
	 * Capture near plane.  A change requests a capture.
	 */
	properties[PROP_NEAR_CLIP] =
		g_param_spec_float ("near-clip", NULL, "Capture near plane", 0.0001f, G_MAXFLOAT, 0.05f, rw);

	/**
	 * LrgReflectionProbeComponent:far-clip:
	 *
	 * Capture far plane (must stay above #LrgReflectionProbeComponent:near-clip
	 * to apply).  A change requests a capture.
	 */
	properties[PROP_FAR_CLIP] =
		g_param_spec_float ("far-clip", NULL, "Capture far plane", 0.001f, G_MAXFLOAT, 1000.0f, rw);

	/**
	 * LrgReflectionProbeComponent:auto-recapture:
	 *
	 * Whether moving the entity or changing the box requests a capture.
	 */
	properties[PROP_AUTO_RECAPTURE] =
		g_param_spec_boolean ("auto-recapture", NULL, "Recapture on change", TRUE, rw);

	/**
	 * LrgReflectionProbeComponent:needs-capture:
	 *
	 * Whether a capture is pending (read only, see
	 * lrg_reflection_probe_component_get_needs_capture()).  Notified when
	 * a request is made; a capture made elsewhere clears it silently.
	 */
	properties[PROP_NEEDS_CAPTURE] =
		g_param_spec_boolean ("needs-capture", NULL, "Capture pending", FALSE,
		                      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_reflection_probe_component_init (LrgReflectionProbeComponent *self)
{
	self->probe = lrg_reflection_probe_new ();
	lrg_reflection_probe_get_extents (self->probe, self->extents);
	self->near_plane = 0.05f;
	self->far_plane = 1000.0f;
	self->auto_recapture = TRUE;
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

LrgReflectionProbeComponent *
lrg_reflection_probe_component_new (void)
{
	return (LrgReflectionProbeComponent *) g_object_new (LRG_TYPE_REFLECTION_PROBE_COMPONENT, NULL);
}

LrgReflectionProbe *
lrg_reflection_probe_component_get_probe (LrgReflectionProbeComponent *self)
{
	g_return_val_if_fail (LRG_IS_REFLECTION_PROBE_COMPONENT (self), NULL);

	return self->probe;
}

void
lrg_reflection_probe_component_set_extents (LrgReflectionProbeComponent *self,
                                            gfloat                       x,
                                            gfloat                       y,
                                            gfloat                       z)
{
	gfloat   v[3];
	guint    k;
	gboolean any = FALSE;

	g_return_if_fail (LRG_IS_REFLECTION_PROBE_COMPONENT (self));

	v[0] = MAX (x, 0.001f);
	v[1] = MAX (y, 0.001f);
	v[2] = MAX (z, 0.001f);
	g_object_freeze_notify (G_OBJECT (self));
	for (k = 0; k < 3; k++)
		if (self->extents[k] != v[k])
		{
			self->extents[k] = v[k];
			g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_EXTENTS_X + k]);
			any = TRUE;
		}
	g_object_thaw_notify (G_OBJECT (self));
	if (any)
	{
		LrgEntity3D *entity = lrg_component_get_entity (LRG_COMPONENT (self));
		LrgWorld3D  *world = lrg_component_get_world3d (LRG_COMPONENT (self));

		if (world != NULL && entity != NULL)
			lrg_world3d_invalidate_bounds (world, entity);
		lrg_reflection_probe_component_sync (self);
	}
}

void
lrg_reflection_probe_component_get_extents (LrgReflectionProbeComponent *self,
                                            gfloat                      *out_extents)
{
	g_return_if_fail (LRG_IS_REFLECTION_PROBE_COMPONENT (self));
	g_return_if_fail (out_extents != NULL);

	memcpy (out_extents, self->extents, sizeof self->extents);
}

void
lrg_reflection_probe_component_set_auto_recapture (LrgReflectionProbeComponent *self,
                                                   gboolean                     auto_recapture)
{
	g_return_if_fail (LRG_IS_REFLECTION_PROBE_COMPONENT (self));

	auto_recapture = !!auto_recapture;
	if (self->auto_recapture == auto_recapture)
		return;
	self->auto_recapture = auto_recapture;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_AUTO_RECAPTURE]);
}

gboolean
lrg_reflection_probe_component_get_auto_recapture (LrgReflectionProbeComponent *self)
{
	g_return_val_if_fail (LRG_IS_REFLECTION_PROBE_COMPONENT (self), FALSE);

	return self->auto_recapture;
}

void
lrg_reflection_probe_component_request_capture (LrgReflectionProbeComponent *self)
{
	g_return_if_fail (LRG_IS_REFLECTION_PROBE_COMPONENT (self));

	mark_pending (self);
}

gboolean
lrg_reflection_probe_component_get_needs_capture (LrgReflectionProbeComponent *self)
{
	g_return_val_if_fail (LRG_IS_REFLECTION_PROBE_COMPONENT (self), FALSE);

	/* any capture after the request satisfies it */
	if (self->pending && lrg_reflection_probe_get_capture_count (self->probe) > self->pending_at)
		self->pending = FALSE;
	return self->pending;
}

void
lrg_reflection_probe_component_sync (LrgReflectionProbeComponent *self)
{
	LrgEntity3D *entity;
	LrgWorld3D  *world;
	gfloat       pos[3];
	gfloat       scale[3];
	gfloat       box[3];
	guint        k;

	g_return_if_fail (LRG_IS_REFLECTION_PROBE_COMPONENT (self));

	entity = lrg_component_get_entity (LRG_COMPONENT (self));
	world = lrg_component_get_world3d (LRG_COMPONENT (self));

	/* 1. registration follows the world's current environment */
	register_with (self, (world != NULL && is_live (self)) ? lrg_world3d_get_environment (world) : NULL);
	if (entity == NULL)
		return;

	/* 2. the box: world position, extents times the world scale */
	lrg_entity3d_get_world_position (entity, pos);
	lrg_entity3d_get_world_scale (entity, scale);
	for (k = 0; k < 3; k++)
		box[k] = MAX (self->extents[k] * fabsf (scale[k]), 0.001f);

	if (self->placed && memcmp (pos, self->last_position, sizeof pos) == 0
	    && memcmp (box, self->last_box, sizeof box) == 0)
		return;

	lrg_reflection_probe_set_position (self->probe, pos[0], pos[1], pos[2]);
	lrg_reflection_probe_set_extents (self->probe, box[0], box[1], box[2]);

	/* 3. a move or a box change makes the capture stale */
	if (self->placed && self->auto_recapture)
		mark_pending (self);
	memcpy (self->last_position, pos, sizeof pos);
	memcpy (self->last_box, box, sizeof box);
	self->placed = TRUE;
}

gboolean
lrg_reflection_probe_component_capture (LrgReflectionProbeComponent  *self,
                                        LrgReflectionProbeDrawFunc    draw_func,
                                        gpointer                      user_data,
                                        GError                      **error)
{
	g_return_val_if_fail (LRG_IS_REFLECTION_PROBE_COMPONENT (self), FALSE);
	g_return_val_if_fail (draw_func != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	lrg_reflection_probe_component_sync (self);
	if (!lrg_reflection_probe_capture (self->probe, draw_func, user_data, error))
		return FALSE;
	self->pending = FALSE;
	return TRUE;
}

gint
lrg_reflection_probe_component_capture_world (LrgWorld3D                  *world,
                                              gboolean                     force,
                                              LrgReflectionProbeDrawFunc   draw_func,
                                              gpointer                     user_data,
                                              GError                     **error)
{
	g_autoptr(GPtrArray) comps = NULL;
	guint                i;
	gint                 n = 0;

	g_return_val_if_fail (LRG_IS_WORLD3D (world), -1);
	g_return_val_if_fail (draw_func != NULL, -1);
	g_return_val_if_fail (error == NULL || *error == NULL, -1);

	comps = lrg_world3d_get_components_of_type (world, LRG_TYPE_REFLECTION_PROBE_COMPONENT);
	for (i = 0; comps != NULL && i < comps->len; i++)
	{
		LrgReflectionProbeComponent *c = g_ptr_array_index (comps, i);

		lrg_reflection_probe_component_sync (c);
		if (!is_live (c))
			continue;
		if (!force && !lrg_reflection_probe_component_get_needs_capture (c))
			continue;
		if (!lrg_reflection_probe_component_capture (c, draw_func, user_data, error))
			return -1;
		n++;
	}
	return n;
}

guint
lrg_reflection_probe_component_count_pending (LrgWorld3D *world)
{
	g_autoptr(GPtrArray) comps = NULL;
	guint                i;
	guint                n = 0;

	g_return_val_if_fail (LRG_IS_WORLD3D (world), 0);

	comps = lrg_world3d_get_components_of_type (world, LRG_TYPE_REFLECTION_PROBE_COMPONENT);
	for (i = 0; comps != NULL && i < comps->len; i++)
	{
		LrgReflectionProbeComponent *c = g_ptr_array_index (comps, i);

		if (is_live (c) && lrg_reflection_probe_component_get_needs_capture (c))
			n++;
	}
	return n;
}
