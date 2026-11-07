/* lrg-audio-listener-component.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgAudioListenerComponent: moves the audio manager's listener with the
 * owning entity and registers the entity as the world's listener.
 */

#include "config.h"

#include <string.h>

#include "lrg-audio-listener-component.h"
#include "../lrg-entity3d.h"
#include "../../audio/lrg-audio-manager.h"

struct _LrgAudioListenerComponent
{
	LrgComponent     parent_instance;

	LrgAudioManager *manager;   /* explicit; NULL means the default */

	/* doppler: velocity measured between late updates */
	gfloat           velocity[3];
	gfloat           last_position[3];
	gboolean         has_last_position;
};

G_DEFINE_FINAL_TYPE (LrgAudioListenerComponent, lrg_audio_listener_component, LRG_TYPE_COMPONENT)

enum
{
	PROP_0,
	PROP_AUDIO_MANAGER,
	N_PROPS
};

static GParamSpec *properties[N_PROPS];

/* The manager to feed: the explicit one or the process default. */
static LrgAudioManager *
get_manager (LrgAudioListenerComponent *self)
{
	return (self->manager != NULL) ? self->manager : lrg_audio_manager_get_default ();
}

static void
lrg_audio_listener_component_world_entered (LrgComponent *component,
                                            LrgWorld3D   *world)
{
	LrgAudioListenerComponent *self = LRG_AUDIO_LISTENER_COMPONENT (component);

	lrg_world3d_set_audio_listener (world, lrg_component_get_entity (component));
	self->has_last_position = FALSE;
	memset (self->velocity, 0, sizeof self->velocity);
	lrg_audio_listener_component_sync (self);
}

static void
lrg_audio_listener_component_world_exited (LrgComponent *component,
                                           LrgWorld3D   *world)
{
	/* Only clear the world's listener if it is still ours */
	if (lrg_world3d_get_audio_listener (world) == lrg_component_get_entity (component))
		lrg_world3d_set_audio_listener (world, NULL);
}

static void
lrg_audio_listener_component_late_update (LrgComponent *component,
                                          gfloat        delta)
{
	LrgAudioListenerComponent *self = LRG_AUDIO_LISTENER_COMPONENT (component);
	LrgEntity3D               *entity = lrg_component_get_entity (component);
	gfloat                     pos[3];

	/* the listener's velocity for doppler, from its movement */
	if (entity != NULL && lrg_component_get_enabled (component))
	{
		lrg_entity3d_get_world_position (entity, pos);
		if (self->has_last_position && delta > 0.0f)
		{
			self->velocity[0] = (pos[0] - self->last_position[0]) / delta;
			self->velocity[1] = (pos[1] - self->last_position[1]) / delta;
			self->velocity[2] = (pos[2] - self->last_position[2]) / delta;
		}
		memcpy (self->last_position, pos, sizeof pos);
		self->has_last_position = TRUE;
		lrg_audio_manager_set_listener_velocity (get_manager (self), self->velocity);
	}
	lrg_audio_listener_component_sync (self);
}

static void
lrg_audio_listener_component_dispose (GObject *object)
{
	LrgAudioListenerComponent *self = LRG_AUDIO_LISTENER_COMPONENT (object);

	g_clear_object (&self->manager);

	G_OBJECT_CLASS (lrg_audio_listener_component_parent_class)->dispose (object);
}

static void
lrg_audio_listener_component_get_property (GObject    *object,
                                           guint       prop_id,
                                           GValue     *value,
                                           GParamSpec *pspec)
{
	LrgAudioListenerComponent *self = LRG_AUDIO_LISTENER_COMPONENT (object);

	switch (prop_id)
	{
	case PROP_AUDIO_MANAGER:
		g_value_set_object (value, self->manager);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_audio_listener_component_set_property (GObject      *object,
                                           guint         prop_id,
                                           const GValue *value,
                                           GParamSpec   *pspec)
{
	LrgAudioListenerComponent *self = LRG_AUDIO_LISTENER_COMPONENT (object);

	switch (prop_id)
	{
	case PROP_AUDIO_MANAGER:
		lrg_audio_listener_component_set_audio_manager (self, g_value_get_object (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_audio_listener_component_class_init (LrgAudioListenerComponentClass *klass)
{
	GObjectClass      *object_class = G_OBJECT_CLASS (klass);
	LrgComponentClass *component_class = LRG_COMPONENT_CLASS (klass);

	object_class->dispose = lrg_audio_listener_component_dispose;
	object_class->get_property = lrg_audio_listener_component_get_property;
	object_class->set_property = lrg_audio_listener_component_set_property;

	component_class->world_entered = lrg_audio_listener_component_world_entered;
	component_class->world_exited = lrg_audio_listener_component_world_exited;
	component_class->late_update = lrg_audio_listener_component_late_update;

	/**
	 * LrgAudioListenerComponent:audio-manager:
	 *
	 * The audio manager whose listener moves; %NULL for
	 * lrg_audio_manager_get_default().
	 */
	properties[PROP_AUDIO_MANAGER] =
		g_param_spec_object ("audio-manager", NULL, "Audio manager (NULL default)",
		                     LRG_TYPE_AUDIO_MANAGER,
		                     G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_audio_listener_component_init (LrgAudioListenerComponent *self)
{
	(void) self;
}

LrgAudioListenerComponent *
lrg_audio_listener_component_new (void)
{
	return (LrgAudioListenerComponent *) g_object_new (LRG_TYPE_AUDIO_LISTENER_COMPONENT, NULL);
}

LrgAudioManager *
lrg_audio_listener_component_get_audio_manager (LrgAudioListenerComponent *self)
{
	g_return_val_if_fail (LRG_IS_AUDIO_LISTENER_COMPONENT (self), NULL);

	return self->manager;
}

void
lrg_audio_listener_component_set_audio_manager (LrgAudioListenerComponent *self,
                                                LrgAudioManager           *manager)
{
	g_return_if_fail (LRG_IS_AUDIO_LISTENER_COMPONENT (self));
	g_return_if_fail (manager == NULL || LRG_IS_AUDIO_MANAGER (manager));

	if (g_set_object (&self->manager, manager))
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_AUDIO_MANAGER]);
}

void
lrg_audio_listener_component_sync (LrgAudioListenerComponent *self)
{
	LrgEntity3D *entity;
	gfloat       pos[3], fwd[3], up[3];

	g_return_if_fail (LRG_IS_AUDIO_LISTENER_COMPONENT (self));

	entity = lrg_component_get_entity (LRG_COMPONENT (self));
	if (entity == NULL || !lrg_component_get_enabled (LRG_COMPONENT (self)))
		return;

	lrg_entity3d_get_world_position (entity, pos);
	lrg_entity3d_get_forward (entity, fwd);
	lrg_entity3d_get_up (entity, up);
	lrg_audio_manager_set_listener (get_manager (self), pos, fwd, up);
}

void
lrg_audio_listener_component_get_velocity (LrgAudioListenerComponent *self,
                                           gfloat                    *out_velocity)
{
	g_return_if_fail (LRG_IS_AUDIO_LISTENER_COMPONENT (self));
	g_return_if_fail (out_velocity != NULL);

	memcpy (out_velocity, self->velocity, sizeof self->velocity);
}
