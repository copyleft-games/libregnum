/* lrg-audio-source-component.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgAudioSourceComponent: plays positional voices through an
 * #LrgAudioManager and keeps them at the owning entity's world position.
 */

#include "config.h"

#include "lrg-audio-source-component.h"
#include "../lrg-entity3d.h"
#include "../../audio/lrg-sound-bank.h"

struct _LrgAudioSourceComponent
{
	LrgComponent     parent_instance;

	gchar           *bank;
	gchar           *sound;
	gchar           *sound_path;
	gboolean         path_failed;    /* the file did not load: do not retry */
	gfloat           volume;
	gfloat           pitch;
	gfloat           min_distance;
	gfloat           max_distance;
	gfloat           rolloff;
	gfloat           spatial_blend;
	gboolean         loop;
	gboolean         autoplay;
	LrgAudioManager *manager;        /* explicit; NULL means the default */

	guint            voice;          /* current voice, 0 for none */
};

G_DEFINE_FINAL_TYPE (LrgAudioSourceComponent, lrg_audio_source_component, LRG_TYPE_COMPONENT)

enum
{
	PROP_0,
	PROP_BANK,
	PROP_SOUND,
	PROP_SOUND_PATH,
	PROP_VOLUME,
	PROP_PITCH,
	PROP_MIN_DISTANCE,
	PROP_MAX_DISTANCE,
	PROP_ROLLOFF,
	PROP_SPATIAL_BLEND,
	PROP_LOOP,
	PROP_AUTOPLAY,
	PROP_AUDIO_MANAGER,
	PROP_PLAYING,
	N_PROPS
};

static GParamSpec *properties[N_PROPS];

/* ==========================================================================
 * Helpers
 * ========================================================================== */

static LrgAudioManager *
get_manager (LrgAudioSourceComponent *self)
{
	return (self->manager != NULL) ? self->manager : lrg_audio_manager_get_default ();
}

/* Resolves what to play into a (bank, sound) pair: the bank entry when a
 * bank is set, otherwise the sound file, loaded once into the shared file
 * bank.  Returns FALSE when there is nothing playable. */
static gboolean
resolve_sound (LrgAudioSourceComponent  *self,
               LrgAudioManager          *manager,
               const gchar             **out_bank,
               const gchar             **out_sound)
{
	LrgSoundBank     *bank;
	g_autoptr(GError) error = NULL;

	if (self->bank != NULL && self->sound != NULL)
	{
		*out_bank = self->bank;
		*out_sound = self->sound;
		return TRUE;
	}
	if (self->sound_path == NULL || self->path_failed)
		return FALSE;

	/* The shared file bank of this manager, created on first use */
	bank = lrg_audio_manager_get_bank (manager, LRG_AUDIO_SOURCE_FILE_BANK);
	if (bank == NULL)
	{
		g_autoptr(LrgSoundBank) created = lrg_sound_bank_new (LRG_AUDIO_SOURCE_FILE_BANK);

		lrg_audio_manager_add_bank (manager, created);
		bank = lrg_audio_manager_get_bank (manager, LRG_AUDIO_SOURCE_FILE_BANK);
		if (bank == NULL)
			return FALSE;
	}

	/* Load the file once; a missing file or no device is remembered */
	if (!lrg_sound_bank_contains (bank, self->sound_path))
	{
		if (!g_file_test (self->sound_path, G_FILE_TEST_IS_REGULAR) ||
		    !lrg_sound_bank_load (bank, self->sound_path, self->sound_path, &error))
		{
			g_debug ("LrgAudioSourceComponent: cannot load '%s': %s", self->sound_path,
			         error != NULL ? error->message : "no such file");
			self->path_failed = TRUE;
			return FALSE;
		}
	}
	*out_bank = LRG_AUDIO_SOURCE_FILE_BANK;
	*out_sound = self->sound_path;
	return TRUE;
}

/* Replaces a string field and notifies; returns TRUE on a change. */
static gboolean
set_string (LrgAudioSourceComponent  *self,
            gchar                   **slot,
            const gchar              *value,
            guint                     prop)
{
	if (value != NULL && *value == '\0')
		value = NULL;
	if (g_strcmp0 (*slot, value) == 0)
		return FALSE;
	g_free (*slot);
	*slot = g_strdup (value);
	g_object_notify_by_pspec (G_OBJECT (self), properties[prop]);
	return TRUE;
}

/* Stores a float and notifies when it changed. */
static void
set_float (LrgAudioSourceComponent *self,
           gfloat                  *slot,
           gfloat                   value,
           guint                    prop)
{
	if (*slot == value)
		return;
	*slot = value;
	g_object_notify_by_pspec (G_OBJECT (self), properties[prop]);
}

/* Stores a boolean and notifies when it changed. */
static void
set_bool (LrgAudioSourceComponent *self,
          gboolean                *slot,
          gboolean                 value,
          guint                    prop)
{
	value = !!value;
	if (*slot == value)
		return;
	*slot = value;
	g_object_notify_by_pspec (G_OBJECT (self), properties[prop]);
}

/* ==========================================================================
 * LrgComponent vfuncs
 * ========================================================================== */

static void
lrg_audio_source_component_world_entered (LrgComponent *component,
                                          LrgWorld3D   *world)
{
	LrgAudioSourceComponent *self = LRG_AUDIO_SOURCE_COMPONENT (component);

	(void) world;
	if (self->autoplay && lrg_component_get_enabled (component))
		lrg_audio_source_component_play (self);
}

static void
lrg_audio_source_component_world_exited (LrgComponent *component,
                                         LrgWorld3D   *world)
{
	(void) world;
	lrg_audio_source_component_stop (LRG_AUDIO_SOURCE_COMPONENT (component));
}

/* Moves the voice with the entity; restarts a looping voice that ended. */
static void
lrg_audio_source_component_late_update (LrgComponent *component,
                                        gfloat        delta)
{
	LrgAudioSourceComponent *self = LRG_AUDIO_SOURCE_COMPONENT (component);
	LrgAudioManager         *manager;
	LrgEntity3D             *entity;
	gfloat                   pos[3];

	(void) delta;
	if (self->voice == 0)
		return;

	manager = get_manager (self);
	if (!lrg_audio_manager_is_voice_playing (manager, self->voice))
	{
		self->voice = 0;
		if (self->loop)
			lrg_audio_source_component_play (self);
		return;
	}

	entity = lrg_component_get_entity (component);
	if (entity != NULL)
	{
		lrg_entity3d_get_world_position (entity, pos);
		lrg_audio_manager_set_voice_position (manager, self->voice, pos);
	}
}

/* ==========================================================================
 * GObject
 * ========================================================================== */

static void
lrg_audio_source_component_dispose (GObject *object)
{
	LrgAudioSourceComponent *self = LRG_AUDIO_SOURCE_COMPONENT (object);

	lrg_audio_source_component_stop (self);
	g_clear_object (&self->manager);

	G_OBJECT_CLASS (lrg_audio_source_component_parent_class)->dispose (object);
}

static void
lrg_audio_source_component_finalize (GObject *object)
{
	LrgAudioSourceComponent *self = LRG_AUDIO_SOURCE_COMPONENT (object);

	g_free (self->bank);
	g_free (self->sound);
	g_free (self->sound_path);

	G_OBJECT_CLASS (lrg_audio_source_component_parent_class)->finalize (object);
}

static void
lrg_audio_source_component_get_property (GObject    *object,
                                         guint       prop_id,
                                         GValue     *value,
                                         GParamSpec *pspec)
{
	LrgAudioSourceComponent *self = LRG_AUDIO_SOURCE_COMPONENT (object);

	switch (prop_id)
	{
	case PROP_BANK:          g_value_set_string (value, self->bank); break;
	case PROP_SOUND:         g_value_set_string (value, self->sound); break;
	case PROP_SOUND_PATH:    g_value_set_string (value, self->sound_path); break;
	case PROP_VOLUME:        g_value_set_float (value, self->volume); break;
	case PROP_PITCH:         g_value_set_float (value, self->pitch); break;
	case PROP_MIN_DISTANCE:  g_value_set_float (value, self->min_distance); break;
	case PROP_MAX_DISTANCE:  g_value_set_float (value, self->max_distance); break;
	case PROP_ROLLOFF:       g_value_set_float (value, self->rolloff); break;
	case PROP_SPATIAL_BLEND: g_value_set_float (value, self->spatial_blend); break;
	case PROP_LOOP:          g_value_set_boolean (value, self->loop); break;
	case PROP_AUTOPLAY:      g_value_set_boolean (value, self->autoplay); break;
	case PROP_AUDIO_MANAGER: g_value_set_object (value, self->manager); break;
	case PROP_PLAYING:
		g_value_set_boolean (value, lrg_audio_source_component_is_playing (self));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_audio_source_component_set_property (GObject      *object,
                                         guint         prop_id,
                                         const GValue *value,
                                         GParamSpec   *pspec)
{
	LrgAudioSourceComponent *self = LRG_AUDIO_SOURCE_COMPONENT (object);

	switch (prop_id)
	{
	case PROP_BANK:
		set_string (self, &self->bank, g_value_get_string (value), prop_id);
		break;
	case PROP_SOUND:
		set_string (self, &self->sound, g_value_get_string (value), prop_id);
		break;
	case PROP_SOUND_PATH:
		lrg_audio_source_component_set_sound_path (self, g_value_get_string (value));
		break;
	case PROP_VOLUME:        set_float (self, &self->volume, g_value_get_float (value), prop_id); break;
	case PROP_PITCH:         set_float (self, &self->pitch, g_value_get_float (value), prop_id); break;
	case PROP_MIN_DISTANCE:  set_float (self, &self->min_distance, g_value_get_float (value), prop_id); break;
	case PROP_MAX_DISTANCE:  set_float (self, &self->max_distance, g_value_get_float (value), prop_id); break;
	case PROP_ROLLOFF:       set_float (self, &self->rolloff, g_value_get_float (value), prop_id); break;
	case PROP_SPATIAL_BLEND: set_float (self, &self->spatial_blend, g_value_get_float (value), prop_id); break;
	case PROP_LOOP:          set_bool (self, &self->loop, g_value_get_boolean (value), prop_id); break;
	case PROP_AUTOPLAY:      set_bool (self, &self->autoplay, g_value_get_boolean (value), prop_id); break;
	case PROP_AUDIO_MANAGER:
		lrg_audio_source_component_set_audio_manager (self, g_value_get_object (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_audio_source_component_class_init (LrgAudioSourceComponentClass *klass)
{
	GObjectClass      *object_class = G_OBJECT_CLASS (klass);
	LrgComponentClass *component_class = LRG_COMPONENT_CLASS (klass);
	const GParamFlags  rw = G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY;

	object_class->dispose = lrg_audio_source_component_dispose;
	object_class->finalize = lrg_audio_source_component_finalize;
	object_class->get_property = lrg_audio_source_component_get_property;
	object_class->set_property = lrg_audio_source_component_set_property;

	component_class->world_entered = lrg_audio_source_component_world_entered;
	component_class->world_exited = lrg_audio_source_component_world_exited;
	component_class->late_update = lrg_audio_source_component_late_update;

	/**
	 * LrgAudioSourceComponent:bank:
	 *
	 * The sound bank name (lrg_audio_manager_get_bank()).
	 */
	properties[PROP_BANK] = g_param_spec_string ("bank", NULL, "Sound bank", NULL, rw);

	/**
	 * LrgAudioSourceComponent:sound:
	 *
	 * The sound name in #LrgAudioSourceComponent:bank.
	 */
	properties[PROP_SOUND] = g_param_spec_string ("sound", NULL, "Sound in the bank", NULL, rw);

	/**
	 * LrgAudioSourceComponent:sound-path:
	 *
	 * A sound file played when no bank is set.
	 */
	properties[PROP_SOUND_PATH] = g_param_spec_string ("sound-path", NULL, "Sound file", NULL, rw);

	/**
	 * LrgAudioSourceComponent:volume:
	 *
	 * Voice volume 0 .. 1 before the manager volumes and the attenuation;
	 * read when a voice starts.
	 */
	properties[PROP_VOLUME] = g_param_spec_float ("volume", NULL, "Volume", 0.0f, 4.0f, 1.0f, rw);

	/**
	 * LrgAudioSourceComponent:pitch:
	 *
	 * Playback pitch multiplier.  Stored for the level format and the
	 * inspector; voices of the 0.9 audio manager play at the sound's pitch.
	 */
	properties[PROP_PITCH] = g_param_spec_float ("pitch", NULL, "Pitch", 0.01f, 8.0f, 1.0f, rw);

	/**
	 * LrgAudioSourceComponent:min-distance:
	 *
	 * Full volume within this distance.
	 */
	properties[PROP_MIN_DISTANCE] =
		g_param_spec_float ("min-distance", NULL, "Full-volume distance", 0.0f, G_MAXFLOAT,
		                    LRG_AUDIO_DEFAULT_MIN_DISTANCE, rw);

	/**
	 * LrgAudioSourceComponent:max-distance:
	 *
	 * Silent beyond this distance.
	 */
	properties[PROP_MAX_DISTANCE] =
		g_param_spec_float ("max-distance", NULL, "Silent distance", 0.0f, G_MAXFLOAT,
		                    LRG_AUDIO_DEFAULT_MAX_DISTANCE, rw);

	/**
	 * LrgAudioSourceComponent:rolloff:
	 *
	 * Attenuation curve: 0 linear, above 0 inverse distance.
	 */
	properties[PROP_ROLLOFF] =
		g_param_spec_float ("rolloff", NULL, "Rolloff", 0.0f, G_MAXFLOAT,
		                    LRG_AUDIO_DEFAULT_ROLLOFF, rw);

	/**
	 * LrgAudioSourceComponent:spatial-blend:
	 *
	 * 0 plays flat (2D), 1 fully positional.
	 */
	properties[PROP_SPATIAL_BLEND] =
		g_param_spec_float ("spatial-blend", NULL, "Spatial blend", 0.0f, 1.0f, 1.0f, rw);

	/**
	 * LrgAudioSourceComponent:loop:
	 *
	 * Restarts the sound when it ends (checked every late update).
	 */
	properties[PROP_LOOP] = g_param_spec_boolean ("loop", NULL, "Loop", FALSE, rw);

	/**
	 * LrgAudioSourceComponent:autoplay:
	 *
	 * Plays when the entity enters a world.
	 */
	properties[PROP_AUTOPLAY] = g_param_spec_boolean ("autoplay", NULL, "Play on world entry",
	                                                  FALSE, rw);

	/**
	 * LrgAudioSourceComponent:audio-manager:
	 *
	 * The audio manager voices play through; %NULL for the default one.
	 */
	properties[PROP_AUDIO_MANAGER] =
		g_param_spec_object ("audio-manager", NULL, "Audio manager (NULL default)",
		                     LRG_TYPE_AUDIO_MANAGER, rw);

	/**
	 * LrgAudioSourceComponent:playing:
	 *
	 * Whether the component's voice is audible (read-only, not notified).
	 */
	properties[PROP_PLAYING] =
		g_param_spec_boolean ("playing", NULL, "Voice audible", FALSE,
		                      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

	g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_audio_source_component_init (LrgAudioSourceComponent *self)
{
	self->volume = 1.0f;
	self->pitch = 1.0f;
	self->min_distance = LRG_AUDIO_DEFAULT_MIN_DISTANCE;
	self->max_distance = LRG_AUDIO_DEFAULT_MAX_DISTANCE;
	self->rolloff = LRG_AUDIO_DEFAULT_ROLLOFF;
	self->spatial_blend = 1.0f;
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

LrgAudioSourceComponent *
lrg_audio_source_component_new (void)
{
	return (LrgAudioSourceComponent *) g_object_new (LRG_TYPE_AUDIO_SOURCE_COMPONENT, NULL);
}

LrgAudioSourceComponent *
lrg_audio_source_component_new_with_sound (const gchar *bank,
                                           const gchar *sound)
{
	return (LrgAudioSourceComponent *) g_object_new (LRG_TYPE_AUDIO_SOURCE_COMPONENT,
	                                                 "bank", bank, "sound", sound, NULL);
}

void
lrg_audio_source_component_set_sound (LrgAudioSourceComponent *self,
                                      const gchar             *bank,
                                      const gchar             *sound)
{
	g_return_if_fail (LRG_IS_AUDIO_SOURCE_COMPONENT (self));

	g_object_freeze_notify (G_OBJECT (self));
	set_string (self, &self->bank, bank, PROP_BANK);
	set_string (self, &self->sound, sound, PROP_SOUND);
	g_object_thaw_notify (G_OBJECT (self));
}

const gchar *
lrg_audio_source_component_get_bank (LrgAudioSourceComponent *self)
{
	g_return_val_if_fail (LRG_IS_AUDIO_SOURCE_COMPONENT (self), NULL);

	return self->bank;
}

const gchar *
lrg_audio_source_component_get_sound (LrgAudioSourceComponent *self)
{
	g_return_val_if_fail (LRG_IS_AUDIO_SOURCE_COMPONENT (self), NULL);

	return self->sound;
}

const gchar *
lrg_audio_source_component_get_sound_path (LrgAudioSourceComponent *self)
{
	g_return_val_if_fail (LRG_IS_AUDIO_SOURCE_COMPONENT (self), NULL);

	return self->sound_path;
}

void
lrg_audio_source_component_set_sound_path (LrgAudioSourceComponent *self,
                                           const gchar             *path)
{
	g_return_if_fail (LRG_IS_AUDIO_SOURCE_COMPONENT (self));

	if (set_string (self, &self->sound_path, path, PROP_SOUND_PATH))
		self->path_failed = FALSE;
}

void
lrg_audio_source_component_get_attenuation (LrgAudioSourceComponent *self,
                                            LrgAudioAttenuation     *out)
{
	g_return_if_fail (LRG_IS_AUDIO_SOURCE_COMPONENT (self));
	g_return_if_fail (out != NULL);

	out->min_distance = self->min_distance;
	out->max_distance = MAX (self->max_distance, self->min_distance);
	out->rolloff = self->rolloff;
	out->spatial_blend = self->spatial_blend;
}

void
lrg_audio_source_component_set_attenuation (LrgAudioSourceComponent   *self,
                                            const LrgAudioAttenuation *attenuation)
{
	g_return_if_fail (LRG_IS_AUDIO_SOURCE_COMPONENT (self));
	g_return_if_fail (attenuation != NULL);

	g_object_freeze_notify (G_OBJECT (self));
	set_float (self, &self->min_distance, MAX (attenuation->min_distance, 0.0f), PROP_MIN_DISTANCE);
	set_float (self, &self->max_distance, MAX (attenuation->max_distance, 0.0f), PROP_MAX_DISTANCE);
	set_float (self, &self->rolloff, MAX (attenuation->rolloff, 0.0f), PROP_ROLLOFF);
	set_float (self, &self->spatial_blend, CLAMP (attenuation->spatial_blend, 0.0f, 1.0f),
	           PROP_SPATIAL_BLEND);
	g_object_thaw_notify (G_OBJECT (self));
}

guint
lrg_audio_source_component_play (LrgAudioSourceComponent *self)
{
	LrgAudioManager     *manager;
	LrgAudioAttenuation  att;
	LrgEntity3D         *entity;
	const gchar         *bank = NULL;
	const gchar         *sound = NULL;
	gfloat               pos[3];

	g_return_val_if_fail (LRG_IS_AUDIO_SOURCE_COMPONENT (self), 0);

	lrg_audio_source_component_stop (self);

	manager = get_manager (self);
	if (!resolve_sound (self, manager, &bank, &sound))
		return 0;

	/* Positional at the entity, flat without one */
	lrg_audio_source_component_get_attenuation (self, &att);
	entity = lrg_component_get_entity (LRG_COMPONENT (self));
	if (entity != NULL)
		lrg_entity3d_get_world_position (entity, pos);
	self->voice = lrg_audio_manager_play_sound_at (manager, bank, sound,
	                                               (entity != NULL) ? pos : NULL,
	                                               self->volume, &att);
	return self->voice;
}

void
lrg_audio_source_component_stop (LrgAudioSourceComponent *self)
{
	g_return_if_fail (LRG_IS_AUDIO_SOURCE_COMPONENT (self));

	if (self->voice == 0)
		return;
	lrg_audio_manager_stop_voice (get_manager (self), self->voice);
	self->voice = 0;
}

gboolean
lrg_audio_source_component_is_playing (LrgAudioSourceComponent *self)
{
	g_return_val_if_fail (LRG_IS_AUDIO_SOURCE_COMPONENT (self), FALSE);

	return self->voice != 0 && lrg_audio_manager_is_voice_playing (get_manager (self), self->voice);
}

guint
lrg_audio_source_component_get_voice (LrgAudioSourceComponent *self)
{
	g_return_val_if_fail (LRG_IS_AUDIO_SOURCE_COMPONENT (self), 0);

	return self->voice;
}

LrgAudioManager *
lrg_audio_source_component_get_audio_manager (LrgAudioSourceComponent *self)
{
	g_return_val_if_fail (LRG_IS_AUDIO_SOURCE_COMPONENT (self), NULL);

	return self->manager;
}

void
lrg_audio_source_component_set_audio_manager (LrgAudioSourceComponent *self,
                                              LrgAudioManager         *manager)
{
	g_return_if_fail (LRG_IS_AUDIO_SOURCE_COMPONENT (self));
	g_return_if_fail (manager == NULL || LRG_IS_AUDIO_MANAGER (manager));

	if (self->manager == manager)
		return;
	lrg_audio_source_component_stop (self);
	g_set_object (&self->manager, manager);
	self->path_failed = FALSE;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_AUDIO_MANAGER]);
}
