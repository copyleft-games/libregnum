/* lrg-sequence-component.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgSequenceComponent: an #LrgSequencePlayer driven by an entity's
 * lifecycle (load and autoplay on entering a world, advance in update,
 * stop on leaving).
 */

#include "config.h"

#include "lrg-sequence-component.h"
#include "../lrg-entity3d.h"
#include "../lrg-world3d.h"

struct _LrgSequenceComponent
{
	LrgComponent parent_instance;

	LrgSequencePlayer *player;
	gchar             *sequence_path;
	gboolean           autoplay;
	gboolean           has_play_mode;   /* play-mode was set explicitly */
	LrgSequencePlayMode play_mode;
	gfloat             rate;
};

G_DEFINE_FINAL_TYPE (LrgSequenceComponent, lrg_sequence_component, LRG_TYPE_COMPONENT)

enum
{
	PROP_0,
	PROP_SEQUENCE,
	PROP_SEQUENCE_PATH,
	PROP_AUTOPLAY,
	PROP_PLAY_MODE,
	PROP_RATE,
	N_PROPS
};

static GParamSpec *properties[N_PROPS];

/* Loads sequence-path into the player (logged, never fatal). */
static void
load_path (LrgSequenceComponent *self)
{
	g_autoptr(LrgSequence) seq = NULL;
	g_autoptr(GError)      error = NULL;

	if (self->sequence_path == NULL || *self->sequence_path == '\0')
		return;
	seq = lrg_sequence_new_from_file (self->sequence_path, &error);
	if (seq == NULL)
	{
		g_warning ("LrgSequenceComponent: %s", error->message);
		return;
	}
	lrg_sequence_player_set_sequence (self->player, seq);
	if (self->has_play_mode)
		lrg_sequence_player_set_play_mode (self->player, self->play_mode);
}

static void
lrg_sequence_component_attached_entity (LrgComponent *component,
                                        LrgEntity3D  *entity)
{
	LrgSequenceComponent *self = LRG_SEQUENCE_COMPONENT (component);

	/* "self" in a track binding means the owning entity. */
	lrg_sequence_player_set_binding_override (self->player, "self", entity);
}

static void
lrg_sequence_component_detached_entity (LrgComponent *component,
                                        LrgEntity3D  *entity)
{
	LrgSequenceComponent *self = LRG_SEQUENCE_COMPONENT (component);

	(void) entity;
	lrg_sequence_player_set_binding_override (self->player, "self", NULL);
}

static void
lrg_sequence_component_world_entered (LrgComponent *component,
                                      LrgWorld3D   *world)
{
	LrgSequenceComponent *self = LRG_SEQUENCE_COMPONENT (component);

	lrg_sequence_player_set_world (self->player, world);
	if (lrg_sequence_player_get_sequence (self->player) == NULL)
		load_path (self);
	if (self->autoplay && lrg_sequence_player_get_sequence (self->player) != NULL)
		lrg_sequence_player_play (self->player);
}

static void
lrg_sequence_component_world_exited (LrgComponent *component,
                                     LrgWorld3D   *world)
{
	LrgSequenceComponent *self = LRG_SEQUENCE_COMPONENT (component);

	(void) world;
	lrg_sequence_player_stop (self->player);
	lrg_sequence_player_set_world (self->player, NULL);
}

static void
lrg_sequence_component_update (LrgComponent *component,
                               gfloat        delta)
{
	lrg_sequence_player_advance (LRG_SEQUENCE_COMPONENT (component)->player, delta);
}

static void
lrg_sequence_component_dispose (GObject *object)
{
	LrgSequenceComponent *self = LRG_SEQUENCE_COMPONENT (object);

	g_clear_object (&self->player);
	G_OBJECT_CLASS (lrg_sequence_component_parent_class)->dispose (object);
}

static void
lrg_sequence_component_finalize (GObject *object)
{
	LrgSequenceComponent *self = LRG_SEQUENCE_COMPONENT (object);

	g_clear_pointer (&self->sequence_path, g_free);
	G_OBJECT_CLASS (lrg_sequence_component_parent_class)->finalize (object);
}

static void
lrg_sequence_component_get_property (GObject    *object,
                                     guint       prop_id,
                                     GValue     *value,
                                     GParamSpec *pspec)
{
	LrgSequenceComponent *self = LRG_SEQUENCE_COMPONENT (object);

	switch (prop_id)
	{
	case PROP_SEQUENCE:
		g_value_set_object (value, lrg_sequence_player_get_sequence (self->player));
		break;
	case PROP_SEQUENCE_PATH:
		g_value_set_string (value, self->sequence_path);
		break;
	case PROP_AUTOPLAY:
		g_value_set_boolean (value, self->autoplay);
		break;
	case PROP_PLAY_MODE:
		g_value_set_enum (value, (gint) lrg_sequence_player_get_play_mode (self->player));
		break;
	case PROP_RATE:
		g_value_set_float (value, lrg_sequence_player_get_rate (self->player));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
lrg_sequence_component_set_property (GObject      *object,
                                     guint         prop_id,
                                     const GValue *value,
                                     GParamSpec   *pspec)
{
	LrgSequenceComponent *self = LRG_SEQUENCE_COMPONENT (object);

	switch (prop_id)
	{
	case PROP_SEQUENCE:
		lrg_sequence_component_set_sequence (self, g_value_get_object (value));
		break;
	case PROP_SEQUENCE_PATH:
		lrg_sequence_component_set_sequence_path (self, g_value_get_string (value));
		break;
	case PROP_AUTOPLAY:
		lrg_sequence_component_set_autoplay (self, g_value_get_boolean (value));
		break;
	case PROP_PLAY_MODE:
		self->has_play_mode = TRUE;
		self->play_mode = (LrgSequencePlayMode) g_value_get_enum (value);
		lrg_sequence_player_set_play_mode (self->player, self->play_mode);
		g_object_notify_by_pspec (object, pspec);
		break;
	case PROP_RATE:
		lrg_sequence_player_set_rate (self->player, g_value_get_float (value));
		g_object_notify_by_pspec (object, pspec);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
lrg_sequence_component_class_init (LrgSequenceComponentClass *klass)
{
	GObjectClass      *object_class = G_OBJECT_CLASS (klass);
	LrgComponentClass *component_class = LRG_COMPONENT_CLASS (klass);

	object_class->dispose = lrg_sequence_component_dispose;
	object_class->finalize = lrg_sequence_component_finalize;
	object_class->get_property = lrg_sequence_component_get_property;
	object_class->set_property = lrg_sequence_component_set_property;

	component_class->attached_entity = lrg_sequence_component_attached_entity;
	component_class->detached_entity = lrg_sequence_component_detached_entity;
	component_class->world_entered = lrg_sequence_component_world_entered;
	component_class->world_exited = lrg_sequence_component_world_exited;
	component_class->update = lrg_sequence_component_update;

	/**
	 * LrgSequenceComponent:sequence:
	 *
	 * The sequence played (set in code, or loaded from
	 * #LrgSequenceComponent:sequence-path).
	 */
	properties[PROP_SEQUENCE] =
		g_param_spec_object ("sequence", "Sequence", "The sequence played", LRG_TYPE_SEQUENCE,
		                     G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgSequenceComponent:sequence-path:
	 *
	 * The `.rseq` file to load (level node param "sequence_path").
	 */
	properties[PROP_SEQUENCE_PATH] =
		g_param_spec_string ("sequence-path", "Sequence path", "The .rseq file to load", NULL,
		                     G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgSequenceComponent:autoplay:
	 *
	 * Whether the sequence starts when the entity enters a world.
	 */
	properties[PROP_AUTOPLAY] =
		g_param_spec_boolean ("autoplay", "Autoplay", "Start when entering a world", TRUE,
		                      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgSequenceComponent:play-mode:
	 *
	 * The player's play mode. Until it is set, the sequence's own play mode
	 * is used.
	 */
	properties[PROP_PLAY_MODE] =
		g_param_spec_enum ("play-mode", "Play mode", "Once, loop or ping-pong", LRG_TYPE_SEQUENCE_PLAY_MODE,
		                   LRG_SEQUENCE_PLAY_ONCE,
		                   G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgSequenceComponent:rate:
	 *
	 * The player's playback rate.
	 */
	properties[PROP_RATE] =
		g_param_spec_float ("rate", "Rate", "Playback rate", -100.0f, 100.0f, 1.0f,
		                    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_sequence_component_init (LrgSequenceComponent *self)
{
	self->player = lrg_sequence_player_new (NULL);
	self->autoplay = TRUE;
}

LrgSequenceComponent *
lrg_sequence_component_new (void)
{
	return g_object_new (LRG_TYPE_SEQUENCE_COMPONENT, NULL);
}

LrgSequenceComponent *
lrg_sequence_component_new_with_sequence (LrgSequence *sequence)
{
	LrgSequenceComponent *self;

	g_return_val_if_fail (LRG_IS_SEQUENCE (sequence), NULL);

	self = lrg_sequence_component_new ();
	lrg_sequence_component_set_sequence (self, sequence);
	return self;
}

LrgSequencePlayer *
lrg_sequence_component_get_player (LrgSequenceComponent *self)
{
	g_return_val_if_fail (LRG_IS_SEQUENCE_COMPONENT (self), NULL);
	return self->player;
}

LrgSequence *
lrg_sequence_component_get_sequence (LrgSequenceComponent *self)
{
	g_return_val_if_fail (LRG_IS_SEQUENCE_COMPONENT (self), NULL);
	return lrg_sequence_player_get_sequence (self->player);
}

void
lrg_sequence_component_set_sequence (LrgSequenceComponent *self,
                                     LrgSequence          *sequence)
{
	g_return_if_fail (LRG_IS_SEQUENCE_COMPONENT (self));
	g_return_if_fail (sequence == NULL || LRG_IS_SEQUENCE (sequence));

	if (lrg_sequence_player_get_sequence (self->player) == sequence)
		return;
	lrg_sequence_player_set_sequence (self->player, sequence);
	if (self->has_play_mode)
		lrg_sequence_player_set_play_mode (self->player, self->play_mode);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SEQUENCE]);
}

const gchar *
lrg_sequence_component_get_sequence_path (LrgSequenceComponent *self)
{
	g_return_val_if_fail (LRG_IS_SEQUENCE_COMPONENT (self), NULL);
	return self->sequence_path;
}

void
lrg_sequence_component_set_sequence_path (LrgSequenceComponent *self,
                                          const gchar          *path)
{
	g_return_if_fail (LRG_IS_SEQUENCE_COMPONENT (self));

	if (g_strcmp0 (self->sequence_path, path) == 0)
		return;
	g_free (self->sequence_path);
	self->sequence_path = g_strdup (path);
	if (lrg_sequence_player_get_world (self->player) != NULL)
	{
		load_path (self);
		if (self->autoplay && lrg_sequence_player_get_sequence (self->player) != NULL)
			lrg_sequence_player_play (self->player);
	}
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SEQUENCE_PATH]);
}

gboolean
lrg_sequence_component_get_autoplay (LrgSequenceComponent *self)
{
	g_return_val_if_fail (LRG_IS_SEQUENCE_COMPONENT (self), FALSE);
	return self->autoplay;
}

void
lrg_sequence_component_set_autoplay (LrgSequenceComponent *self,
                                     gboolean              autoplay)
{
	g_return_if_fail (LRG_IS_SEQUENCE_COMPONENT (self));

	autoplay = autoplay != FALSE;
	if (self->autoplay == autoplay)
		return;
	self->autoplay = autoplay;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_AUTOPLAY]);
}

void
lrg_sequence_component_play (LrgSequenceComponent *self)
{
	g_return_if_fail (LRG_IS_SEQUENCE_COMPONENT (self));
	lrg_sequence_player_play (self->player);
}

void
lrg_sequence_component_stop (LrgSequenceComponent *self)
{
	g_return_if_fail (LRG_IS_SEQUENCE_COMPONENT (self));
	lrg_sequence_player_stop (self->player);
}
