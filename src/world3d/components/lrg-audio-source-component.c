/* lrg-audio-source-component.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgAudioSourceComponent: a positional emitter.
 *
 * Two backends share one spatialisation:
 *
 *  - a device voice (lrg_audio_manager_play_sound_at ()) when the source
 *    has no mixer: the component evaluates gain and pan itself and hands
 *    them over with lrg_audio_manager_set_voice_mix (), plus the doppler
 *    pitch;
 *  - a mixer voice (lrg_audio_mixer_play ()) on #LrgAudioSourceComponent:bus
 *    when the component has an explicit mixer, or a bus and the audio
 *    manager has a mixer: gain and pan are ramped per frame over the
 *    update interval, pitch and the occlusion low-pass are set directly.
 *
 * Every late update measures the entity's velocity, casts the optional
 * occlusion ray from the listener to the source through the world's
 * physics, evaluates distance rolloff x cone x occlusion (blended by the
 * spatial blend) and pushes the result to the voice.
 */

#include "config.h"

#include <math.h>
#include <string.h>

#include "lrg-audio-source-component.h"
#include "../lrg-entity3d.h"
#include "../lrg-world3d.h"
#include "../../audio/lrg-sound-bank.h"
#include "../../audio/lrg-wave-data.h"
#include "../../physics/lrg-dynamics.h"

#define MAX_OCCLUDERS (4)

struct _LrgAudioSourceComponent
{
	LrgComponent      parent_instance;

	gchar            *bank;
	gchar            *sound;
	gchar            *sound_path;
	gboolean          path_failed;    /* the file did not load: do not retry */
	gfloat            volume;
	gfloat            pitch;
	gfloat            min_distance;
	gfloat            max_distance;
	gfloat            rolloff;
	gfloat            spatial_blend;
	gboolean          loop;
	gboolean          autoplay;
	LrgAudioManager  *manager;        /* explicit; NULL means the default */

	/* 0.9.0 spatialisation */
	LrgAudioRolloff   rolloff_mode;
	LrgKeyframeCurve *rolloff_curve;
	gfloat            cone_inner;     /* degrees, full angle */
	gfloat            cone_outer;
	gfloat            cone_outer_gain;
	gfloat            doppler;
	gfloat            speed_of_sound;
	gboolean          occlusion;
	guint             occlusion_mask;
	gfloat            occlusion_gain;
	gfloat            occlusion_cutoff;
	gfloat            occlusion_fade;

	/* mixer routing */
	gchar            *bus;
	LrgAudioMixer    *mixer;          /* explicit; NULL: the manager's when bus is set */
	LrgWaveData      *wave;           /* explicit */
	LrgWaveData      *loaded_wave;    /* from sound-path or the bank file */
	gchar            *loaded_path;

	/* state */
	guint             voice;          /* device voice, 0 for none */
	guint64           mixer_voice;    /* mixer voice, 0 for none */
	LrgAudioMixer    *voice_mixer;    /* the mixer mixer_voice lives in (ref) */
	gfloat            velocity[3];
	gfloat            last_position[3];
	gboolean          has_last_position;
	gfloat            occlusion_now;  /* applied occlusion gain */
	gfloat            occlusion_goal;
	guint             occluders;
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
	PROP_ROLLOFF_MODE,
	PROP_ROLLOFF_CURVE_KEYS,
	PROP_CONE_INNER_ANGLE,
	PROP_CONE_OUTER_ANGLE,
	PROP_CONE_OUTER_GAIN,
	PROP_DOPPLER_FACTOR,
	PROP_SPEED_OF_SOUND,
	PROP_OCCLUSION,
	PROP_OCCLUSION_MASK,
	PROP_OCCLUSION_GAIN,
	PROP_OCCLUSION_CUTOFF,
	PROP_OCCLUSION_FADE,
	PROP_BUS,
	PROP_MIXER,
	PROP_WAVE,
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

/* the mixer the next voice plays through, or NULL for a device voice */
static LrgAudioMixer *
get_mixer (LrgAudioSourceComponent *self)
{
	if (self->mixer != NULL)
		return self->mixer;
	if (self->bus != NULL)
		return lrg_audio_manager_get_mixer (get_manager (self));
	return NULL;
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

/* The file a mixer voice reads: the sound path, else the bank entry's
 * manifest file under the bank's base path. */
static gchar *
wave_file (LrgAudioSourceComponent *self)
{
	LrgSoundBank *bank;
	const gchar  *file;
	const gchar  *base;

	if (self->bank == NULL || self->sound == NULL)
		return g_strdup (self->sound_path);
	bank = lrg_audio_manager_get_bank (get_manager (self), self->bank);
	if (bank == NULL)
		return NULL;
	file = lrg_sound_bank_get_file (bank, self->sound);
	if (file == NULL)
		return NULL;
	base = lrg_sound_bank_get_base_path (bank);
	if (g_path_is_absolute (file) || base == NULL)
		return g_strdup (file);
	return g_build_filename (base, file, NULL);
}

/* The samples of a mixer voice: the explicit wave, else the file (loaded
 * once per path and kept). */
static LrgWaveData *
resolve_wave (LrgAudioSourceComponent *self)
{
	g_autofree gchar *path = NULL;
	g_autoptr(GError) error = NULL;

	if (self->wave != NULL)
		return self->wave;
	path = wave_file (self);
	if (path == NULL)
		return NULL;
	if (self->loaded_wave != NULL && g_strcmp0 (self->loaded_path, path) == 0)
		return self->loaded_wave;
	g_clear_object (&self->loaded_wave);
	g_clear_pointer (&self->loaded_path, g_free);
	if (!g_file_test (path, G_FILE_TEST_IS_REGULAR))
		return NULL;
	self->loaded_wave = lrg_wave_data_new_from_file (path, &error);
	if (self->loaded_wave == NULL)
	{
		g_debug ("LrgAudioSourceComponent: cannot load '%s' for the mixer: %s", path,
		         error != NULL ? error->message : "unknown error");
		return NULL;
	}
	self->loaded_path = g_steal_pointer (&path);
	return self->loaded_wave;
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

/* "t:gain, t:gain, ..." of the rolloff curve's keys (NULL without one) */
static gchar *
curve_to_keys (LrgKeyframeCurve *curve)
{
	GString *out;
	guint    i, n;

	if (curve == NULL)
		return NULL;
	out = g_string_new (NULL);
	n = lrg_keyframe_curve_get_key_count (curve);
	for (i = 0; i < n; i++)
	{
		gchar  tbuf[G_ASCII_DTOSTR_BUF_SIZE];
		gchar  vbuf[G_ASCII_DTOSTR_BUF_SIZE];
		gfloat t = 0.0f;
		gfloat v = 0.0f;

		lrg_keyframe_curve_get_key (curve, i, &t, &v, NULL);
		g_string_append_printf (out, "%s%s:%s", i > 0 ? ", " : "",
		                        g_ascii_formatd (tbuf, sizeof tbuf, "%g", (gdouble) t),
		                        g_ascii_formatd (vbuf, sizeof vbuf, "%g", (gdouble) v));
	}
	return g_string_free (out, FALSE);
}

/* parses "t:gain, ..." into a new linear curve; NULL on a syntax error */
static LrgKeyframeCurve *
curve_from_keys (const gchar *text)
{
	g_autoptr(LrgKeyframeCurve) curve = lrg_keyframe_curve_new ();
	g_auto(GStrv)               pairs = NULL;
	guint                       i;

	pairs = g_strsplit_set (text, ",;", -1);
	for (i = 0; pairs[i] != NULL; i++)
	{
		g_auto(GStrv) tv = NULL;
		gchar        *end = NULL;
		gdouble       t, v;

		g_strstrip (pairs[i]);
		if (*pairs[i] == '\0')
			continue;
		tv = g_strsplit (pairs[i], ":", 2);
		if (tv[0] == NULL || tv[1] == NULL)
			return NULL;
		t = g_ascii_strtod (g_strstrip (tv[0]), &end);
		if (end == tv[0] || *end != '\0' || !isfinite (t))
			return NULL;
		v = g_ascii_strtod (g_strstrip (tv[1]), &end);
		if (end == tv[1] || *end != '\0' || !isfinite (v))
			return NULL;
		lrg_keyframe_curve_add_key (curve, (gfloat) t, (gfloat) v, LRG_EASING_LINEAR);
	}
	if (lrg_keyframe_curve_get_key_count (curve) == 0)
		return NULL;
	return g_steal_pointer (&curve);
}

/* stops a mixer voice and forgets it */
static void
stop_mixer_voice (LrgAudioSourceComponent *self)
{
	if (self->mixer_voice != 0 && self->voice_mixer != NULL)
		lrg_audio_mixer_stop_voice (self->voice_mixer, self->mixer_voice);
	self->mixer_voice = 0;
	g_clear_object (&self->voice_mixer);
}

/* ==========================================================================
 * Occlusion
 * ========================================================================== */

/*
 * cast_occlusion:
 * Counts the bodies between the listener and the source along a ray of
 * the world's physics (hits within the last 2% of the ray belong to the
 * source itself and are ignored) and sets the occlusion goal.
 */
static void
cast_occlusion (LrgAudioSourceComponent *self,
                const gfloat            *listener,
                const gfloat            *source)
{
	LrgWorld3D        *world = lrg_component_get_world3d (LRG_COMPONENT (self));
	LrgDynamicsWorld  *physics = world != NULL ? lrg_world3d_get_physics (world) : NULL;
	g_autoptr(GArray)  hits = NULL;
	gfloat             dir[3];
	guint              i, count = 0;

	self->occluders = 0;
	self->occlusion_goal = 1.0f;
	if (!self->occlusion || physics == NULL)
		return;
	dir[0] = source[0] - listener[0];
	dir[1] = source[1] - listener[1];
	dir[2] = source[2] - listener[2];
	if (dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2] < 1e-8f)
		return;
	hits = lrg_dynamics_world_raycast_all (physics, listener, dir, 1.0f, (guint32) self->occlusion_mask,
	                                       G_MAXUINT);
	for (i = 0; hits != NULL && i < hits->len; i++)
		if (g_array_index (hits, LrgRayHit, i).fraction < 0.98f)
			count++;
	self->occluders = count;
	self->occlusion_goal = powf (self->occlusion_gain, (gfloat) MIN (count, (guint) MAX_OCCLUDERS));
}

/* moves the applied occlusion toward its goal over occlusion-fade */
static void
fade_occlusion (LrgAudioSourceComponent *self,
                gfloat                   delta)
{
	gfloat k;

	if (self->occlusion_fade <= 0.0f || delta <= 0.0f)
	{
		self->occlusion_now = self->occlusion_goal;
		return;
	}
	k = 1.0f - expf (-delta / self->occlusion_fade);
	self->occlusion_now += (self->occlusion_goal - self->occlusion_now) * k;
}

/* ==========================================================================
 * Spatialisation
 * ========================================================================== */

void
lrg_audio_source_component_evaluate (LrgAudioSourceComponent *self,
                                     const gfloat            *listener_position,
                                     const gfloat            *listener_forward,
                                     const gfloat            *listener_up,
                                     const gfloat            *listener_velocity,
                                     gfloat                  *out_gain,
                                     gfloat                  *out_pan,
                                     gfloat                  *out_pitch)
{
	LrgEntity3D         *entity;
	LrgAudioAttenuation  att;
	gfloat               pos[3], d[3];
	gfloat               distance;
	gfloat               gain = 1.0f;
	gfloat               pan = 0.0f;
	gfloat               pitch = 1.0f;
	gfloat               blend;

	g_return_if_fail (LRG_IS_AUDIO_SOURCE_COMPONENT (self));
	g_return_if_fail (listener_position != NULL);
	g_return_if_fail (listener_forward != NULL);

	entity = lrg_component_get_entity (LRG_COMPONENT (self));
	blend = CLAMP (self->spatial_blend, 0.0f, 1.0f);
	if (entity != NULL && blend > 0.0f)
	{
		lrg_entity3d_get_world_position (entity, pos);
		d[0] = listener_position[0] - pos[0];
		d[1] = listener_position[1] - pos[1];
		d[2] = listener_position[2] - pos[2];
		distance = sqrtf (d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);

		/* distance rolloff */
		gain = lrg_audio_source_component_get_distance_gain (self, distance);

		/* cone: the angle between the entity's forward and the listener */
		if (self->cone_inner < 360.0f && distance > 1e-6f)
		{
			gfloat fwd[3];
			gfloat c;

			lrg_entity3d_get_forward (entity, fwd);
			c = (fwd[0] * d[0] + fwd[1] * d[1] + fwd[2] * d[2]) / distance;
			c = CLAMP (c, -1.0f, 1.0f);
			gain *= lrg_audio_cone_gain (self->cone_inner, self->cone_outer, self->cone_outer_gain,
			                             acosf (c) * (gfloat) (180.0 / G_PI));
		}

		/* occlusion (the faded value of the last ray) */
		gain *= self->occlusion_now;

		/* pan from the listener's basis (the manager's pan law) */
		att.min_distance = self->min_distance;
		att.max_distance = MAX (self->max_distance, self->min_distance);
		att.rolloff = self->rolloff;
		att.spatial_blend = 1.0f;
		lrg_audio_compute_spatial (listener_position, listener_forward, listener_up, pos, &att, NULL, &pan);

		/* doppler */
		pitch = lrg_audio_doppler_pitch (listener_position, listener_velocity, pos, self->velocity,
		                                 self->speed_of_sound, self->doppler * blend);

		gain = 1.0f + blend * (gain - 1.0f);
		pan *= blend;
	}
	if (out_gain != NULL)
		*out_gain = CLAMP (gain, 0.0f, 1.0f);
	if (out_pan != NULL)
		*out_pan = CLAMP (pan, -1.0f, 1.0f);
	if (out_pitch != NULL)
		*out_pitch = CLAMP (pitch * self->pitch, 0.125f, 8.0f);
}

gfloat
lrg_audio_source_component_get_distance_gain (LrgAudioSourceComponent *self,
                                              gfloat                   distance)
{
	g_return_val_if_fail (LRG_IS_AUDIO_SOURCE_COMPONENT (self), 1.0f);

	return lrg_audio_rolloff_evaluate (self->rolloff_mode, self->min_distance, self->max_distance,
	                                   self->rolloff, self->rolloff_curve, distance);
}

/* pushes the evaluated spatialisation to the live voice; @ramp is the
 * mixer ramp length (seconds) */
static void
push_spatial (LrgAudioSourceComponent *self,
              gfloat                   ramp)
{
	LrgAudioManager *manager = get_manager (self);
	gfloat           lpos[3], lfwd[3], lup[3], lvel[3];
	gfloat           gain, pan, pitch;

	lrg_audio_manager_get_listener (manager, lpos, lfwd, lup);
	lrg_audio_manager_get_listener_velocity (manager, lvel);
	lrg_audio_source_component_evaluate (self, lpos, lfwd, lup, lvel, &gain, &pan, &pitch);

	if (self->mixer_voice != 0 && self->voice_mixer != NULL)
	{
		gdouble cutoff = 0.0;

		lrg_audio_mixer_set_voice_gain (self->voice_mixer, self->mixer_voice,
		                                CLAMP ((gdouble) (gain * self->volume), 0.0, 4.0), ramp);
		lrg_audio_mixer_set_voice_pan (self->voice_mixer, self->mixer_voice, pan, ramp);
		lrg_audio_mixer_set_voice_pitch (self->voice_mixer, self->mixer_voice, pitch);
		if (self->occluders > 0 && self->occlusion_cutoff > 0.0f)
			cutoff = MIN ((gdouble) self->occlusion_cutoff,
			              0.49 * (gdouble) lrg_audio_mixer_get_sample_rate (self->voice_mixer));
		lrg_audio_mixer_set_voice_lowpass (self->voice_mixer, self->mixer_voice, cutoff);
	}
	else if (self->voice != 0)
	{
		lrg_audio_manager_set_voice_mix (manager, self->voice, gain, pan);
		lrg_audio_manager_set_voice_pitch (manager, self->voice, pitch);
	}
}

void
lrg_audio_source_component_update_spatial (LrgAudioSourceComponent *self,
                                           gfloat                   delta)
{
	LrgEntity3D     *entity;
	LrgAudioManager *manager;
	gfloat           pos[3], lpos[3];

	g_return_if_fail (LRG_IS_AUDIO_SOURCE_COMPONENT (self));

	manager = get_manager (self);
	entity = lrg_component_get_entity (LRG_COMPONENT (self));

	/* velocity from the movement since the last update */
	if (entity != NULL)
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

		/* occlusion ray from the ears to the source */
		lrg_audio_manager_get_listener (manager, lpos, NULL, NULL);
		cast_occlusion (self, lpos, pos);
		fade_occlusion (self, delta);

		/* a device voice also follows the position (for its bookkeeping) */
		if (self->voice != 0)
			lrg_audio_manager_set_voice_position (manager, self->voice, pos);
	}
	push_spatial (self, delta);
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
	self->has_last_position = FALSE;
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

/* Moves the voice with the entity; restarts a looping device voice that
 * ended (a mixer voice loops by itself). */
static void
lrg_audio_source_component_late_update (LrgComponent *component,
                                        gfloat        delta)
{
	LrgAudioSourceComponent *self = LRG_AUDIO_SOURCE_COMPONENT (component);
	LrgAudioManager         *manager;

	if (self->mixer_voice != 0)
	{
		if (self->voice_mixer == NULL || !lrg_audio_mixer_has_voice (self->voice_mixer, self->mixer_voice))
		{
			self->mixer_voice = 0;
			g_clear_object (&self->voice_mixer);
			return;
		}
		lrg_audio_source_component_update_spatial (self, delta);
		return;
	}
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
	lrg_audio_source_component_update_spatial (self, delta);
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
	g_clear_object (&self->mixer);
	g_clear_object (&self->wave);
	g_clear_object (&self->loaded_wave);
	g_clear_object (&self->rolloff_curve);

	G_OBJECT_CLASS (lrg_audio_source_component_parent_class)->dispose (object);
}

static void
lrg_audio_source_component_finalize (GObject *object)
{
	LrgAudioSourceComponent *self = LRG_AUDIO_SOURCE_COMPONENT (object);

	g_free (self->bank);
	g_free (self->sound);
	g_free (self->sound_path);
	g_free (self->bus);
	g_free (self->loaded_path);

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
	case PROP_BANK:             g_value_set_string (value, self->bank); break;
	case PROP_SOUND:            g_value_set_string (value, self->sound); break;
	case PROP_SOUND_PATH:       g_value_set_string (value, self->sound_path); break;
	case PROP_VOLUME:           g_value_set_float (value, self->volume); break;
	case PROP_PITCH:            g_value_set_float (value, self->pitch); break;
	case PROP_MIN_DISTANCE:     g_value_set_float (value, self->min_distance); break;
	case PROP_MAX_DISTANCE:     g_value_set_float (value, self->max_distance); break;
	case PROP_ROLLOFF:          g_value_set_float (value, self->rolloff); break;
	case PROP_SPATIAL_BLEND:    g_value_set_float (value, self->spatial_blend); break;
	case PROP_LOOP:             g_value_set_boolean (value, self->loop); break;
	case PROP_AUTOPLAY:         g_value_set_boolean (value, self->autoplay); break;
	case PROP_AUDIO_MANAGER:    g_value_set_object (value, self->manager); break;
	case PROP_PLAYING:
		g_value_set_boolean (value, lrg_audio_source_component_is_playing (self));
		break;
	case PROP_ROLLOFF_MODE:     g_value_set_enum (value, (gint) self->rolloff_mode); break;
	case PROP_ROLLOFF_CURVE_KEYS:
		g_value_take_string (value, curve_to_keys (self->rolloff_curve));
		break;
	case PROP_CONE_INNER_ANGLE: g_value_set_float (value, self->cone_inner); break;
	case PROP_CONE_OUTER_ANGLE: g_value_set_float (value, self->cone_outer); break;
	case PROP_CONE_OUTER_GAIN:  g_value_set_float (value, self->cone_outer_gain); break;
	case PROP_DOPPLER_FACTOR:   g_value_set_float (value, self->doppler); break;
	case PROP_SPEED_OF_SOUND:   g_value_set_float (value, self->speed_of_sound); break;
	case PROP_OCCLUSION:        g_value_set_boolean (value, self->occlusion); break;
	case PROP_OCCLUSION_MASK:   g_value_set_uint (value, self->occlusion_mask); break;
	case PROP_OCCLUSION_GAIN:   g_value_set_float (value, self->occlusion_gain); break;
	case PROP_OCCLUSION_CUTOFF: g_value_set_float (value, self->occlusion_cutoff); break;
	case PROP_OCCLUSION_FADE:   g_value_set_float (value, self->occlusion_fade); break;
	case PROP_BUS:              g_value_set_string (value, self->bus); break;
	case PROP_MIXER:            g_value_set_object (value, self->mixer); break;
	case PROP_WAVE:             g_value_set_object (value, self->wave); break;
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
	case PROP_ROLLOFF_MODE:
		if (self->rolloff_mode != (LrgAudioRolloff) g_value_get_enum (value))
		{
			self->rolloff_mode = (LrgAudioRolloff) g_value_get_enum (value);
			g_object_notify_by_pspec (object, pspec);
		}
		break;
	case PROP_ROLLOFF_CURVE_KEYS:
		{
			const gchar                *text = g_value_get_string (value);
			g_autoptr(LrgKeyframeCurve) curve = NULL;

			if (text == NULL || *text == '\0')
			{
				lrg_audio_source_component_set_rolloff_curve (self, NULL);
				break;
			}
			curve = curve_from_keys (text);
			if (curve == NULL)
			{
				g_warning ("LrgAudioSourceComponent: rolloff-curve-keys '%s' is not 't:gain, ...'", text);
				break;
			}
			lrg_audio_source_component_set_rolloff_curve (self, curve);
		}
		break;
	case PROP_CONE_INNER_ANGLE: set_float (self, &self->cone_inner, g_value_get_float (value), prop_id); break;
	case PROP_CONE_OUTER_ANGLE: set_float (self, &self->cone_outer, g_value_get_float (value), prop_id); break;
	case PROP_CONE_OUTER_GAIN:  set_float (self, &self->cone_outer_gain, g_value_get_float (value), prop_id); break;
	case PROP_DOPPLER_FACTOR:   set_float (self, &self->doppler, g_value_get_float (value), prop_id); break;
	case PROP_SPEED_OF_SOUND:   set_float (self, &self->speed_of_sound, g_value_get_float (value), prop_id); break;
	case PROP_OCCLUSION:
		set_bool (self, &self->occlusion, g_value_get_boolean (value), prop_id);
		if (!self->occlusion)
		{
			self->occluders = 0;
			self->occlusion_goal = self->occlusion_now = 1.0f;
		}
		break;
	case PROP_OCCLUSION_MASK:
		if (self->occlusion_mask != g_value_get_uint (value))
		{
			self->occlusion_mask = g_value_get_uint (value);
			g_object_notify_by_pspec (object, pspec);
		}
		break;
	case PROP_OCCLUSION_GAIN:   set_float (self, &self->occlusion_gain, g_value_get_float (value), prop_id); break;
	case PROP_OCCLUSION_CUTOFF: set_float (self, &self->occlusion_cutoff, g_value_get_float (value), prop_id); break;
	case PROP_OCCLUSION_FADE:   set_float (self, &self->occlusion_fade, g_value_get_float (value), prop_id); break;
	case PROP_BUS:
		lrg_audio_source_component_set_bus (self, g_value_get_string (value));
		break;
	case PROP_MIXER:
		lrg_audio_source_component_set_mixer (self, g_value_get_object (value));
		break;
	case PROP_WAVE:
		lrg_audio_source_component_set_wave (self, g_value_get_object (value));
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
	 * Voice volume before the manager volumes and the attenuation; read
	 * when a voice starts and at every update of a mixer voice.
	 */
	properties[PROP_VOLUME] = g_param_spec_float ("volume", NULL, "Volume", 0.0f, 4.0f, 1.0f, rw);

	/**
	 * LrgAudioSourceComponent:pitch:
	 *
	 * Playback pitch multiplier, applied with the doppler shift at every
	 * update.
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
	 * Silent beyond this distance (the custom curve's t = 1).
	 */
	properties[PROP_MAX_DISTANCE] =
		g_param_spec_float ("max-distance", NULL, "Silent distance", 0.0f, G_MAXFLOAT,
		                    LRG_AUDIO_DEFAULT_MAX_DISTANCE, rw);

	/**
	 * LrgAudioSourceComponent:rolloff:
	 *
	 * Steepness of %LRG_AUDIO_ROLLOFF_INVERSE: 0 linear, above 0 inverse
	 * distance.
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
	 * Plays the sound in a loop (a device voice is restarted by the late
	 * update, a mixer voice loops by itself).
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
	 * The audio manager voices play through (and whose listener and mixer
	 * are used); %NULL for the default one.
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

	/**
	 * LrgAudioSourceComponent:rolloff-mode:
	 *
	 * The distance curve (lrg_audio_rolloff_evaluate()).  The default,
	 * %LRG_AUDIO_ROLLOFF_INVERSE with #LrgAudioSourceComponent:rolloff,
	 * matches the audio manager's own attenuation.
	 */
	properties[PROP_ROLLOFF_MODE] =
		g_param_spec_enum ("rolloff-mode", NULL, "Rolloff curve", LRG_TYPE_AUDIO_ROLLOFF,
		                   LRG_AUDIO_ROLLOFF_INVERSE, rw);

	/**
	 * LrgAudioSourceComponent:rolloff-curve-keys:
	 *
	 * The %LRG_AUDIO_ROLLOFF_CUSTOM curve (gain over normalised distance,
	 * 0 at min-distance, 1 at max-distance) as text: `t:gain` pairs
	 * separated by commas (`"0:1, 0.25:0.4, 1:0"`, linear between keys),
	 * the form level visual params, data files and inspectors use.
	 * Setting it builds a new curve and selects %LRG_AUDIO_ROLLOFF_CUSTOM;
	 * an empty string clears it.  Reading it lists the keys of the curve
	 * set with lrg_audio_source_component_set_rolloff_curve() (their
	 * easing is not part of the text).
	 */
	properties[PROP_ROLLOFF_CURVE_KEYS] =
		g_param_spec_string ("rolloff-curve-keys", NULL, "Custom rolloff keys", NULL, rw);

	/**
	 * LrgAudioSourceComponent:cone-inner-angle:
	 *
	 * Full angle in degrees around the entity's forward (-Z) heard at full
	 * volume; 360 makes the source omnidirectional.
	 */
	properties[PROP_CONE_INNER_ANGLE] =
		g_param_spec_float ("cone-inner-angle", NULL, "Inner cone angle", 0.0f, 360.0f, 360.0f, rw);

	/**
	 * LrgAudioSourceComponent:cone-outer-angle:
	 *
	 * Full angle in degrees beyond which #LrgAudioSourceComponent:cone-outer-gain
	 * applies (linear between the cones).
	 */
	properties[PROP_CONE_OUTER_ANGLE] =
		g_param_spec_float ("cone-outer-angle", NULL, "Outer cone angle", 0.0f, 360.0f, 360.0f, rw);

	/**
	 * LrgAudioSourceComponent:cone-outer-gain:
	 *
	 * Gain behind the source, outside the outer cone.
	 */
	properties[PROP_CONE_OUTER_GAIN] =
		g_param_spec_float ("cone-outer-gain", NULL, "Outer cone gain", 0.0f, 1.0f, 0.0f, rw);

	/**
	 * LrgAudioSourceComponent:doppler-factor:
	 *
	 * Doppler exaggeration (lrg_audio_doppler_pitch()); 0 turns it off.
	 * Velocities are measured from movement between late updates.
	 */
	properties[PROP_DOPPLER_FACTOR] =
		g_param_spec_float ("doppler-factor", NULL, "Doppler factor", 0.0f, 10.0f, 1.0f, rw);

	/**
	 * LrgAudioSourceComponent:speed-of-sound:
	 *
	 * World units per second for doppler.
	 */
	properties[PROP_SPEED_OF_SOUND] =
		g_param_spec_float ("speed-of-sound", NULL, "Speed of sound", 1.0f, G_MAXFLOAT,
		                    LRG_AUDIO_SPEED_OF_SOUND, rw);

	/**
	 * LrgAudioSourceComponent:occlusion:
	 *
	 * Casts a ray from the listener to the source through the world's
	 * physics (lrg_world3d_get_physics()) every late update; each body hit
	 * multiplies the gain by #LrgAudioSourceComponent:occlusion-gain.
	 */
	properties[PROP_OCCLUSION] = g_param_spec_boolean ("occlusion", NULL, "Physics occlusion", FALSE, rw);

	/**
	 * LrgAudioSourceComponent:occlusion-mask:
	 *
	 * Physics layers that occlude (one bit per layer).
	 */
	properties[PROP_OCCLUSION_MASK] =
		g_param_spec_uint ("occlusion-mask", NULL, "Occluding layers", 0, G_MAXUINT32, G_MAXUINT32, rw);

	/**
	 * LrgAudioSourceComponent:occlusion-gain:
	 *
	 * Gain per occluding body (up to four count).
	 */
	properties[PROP_OCCLUSION_GAIN] =
		g_param_spec_float ("occlusion-gain", NULL, "Gain per occluder", 0.0f, 1.0f, 0.35f, rw);

	/**
	 * LrgAudioSourceComponent:occlusion-cutoff:
	 *
	 * Low-pass cut-off in Hz while occluded (mixer voices); 0 for none.
	 */
	properties[PROP_OCCLUSION_CUTOFF] =
		g_param_spec_float ("occlusion-cutoff", NULL, "Occluded low-pass", 0.0f, 96000.0f, 1200.0f, rw);

	/**
	 * LrgAudioSourceComponent:occlusion-fade:
	 *
	 * Time constant in seconds of the occlusion gain's fade.
	 */
	properties[PROP_OCCLUSION_FADE] =
		g_param_spec_float ("occlusion-fade", NULL, "Occlusion fade", 0.0f, 10.0f, 0.1f, rw);

	/**
	 * LrgAudioSourceComponent:bus:
	 *
	 * The mixer bus the source plays into.  With a bus and no explicit
	 * #LrgAudioSourceComponent:mixer the audio manager's mixer is used.
	 */
	properties[PROP_BUS] = g_param_spec_string ("bus", NULL, "Mixer bus", NULL, rw);

	/**
	 * LrgAudioSourceComponent:mixer:
	 *
	 * An explicit mixer graph to play through.
	 */
	properties[PROP_MIXER] =
		g_param_spec_object ("mixer", NULL, "Mixer graph", LRG_TYPE_AUDIO_MIXER, rw);

	/**
	 * LrgAudioSourceComponent:wave:
	 *
	 * Explicit samples for a mixer voice.
	 */
	properties[PROP_WAVE] =
		g_param_spec_object ("wave", NULL, "Wave for the mixer", LRG_TYPE_WAVE_DATA, rw);

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
	self->rolloff_mode = LRG_AUDIO_ROLLOFF_INVERSE;
	self->cone_inner = 360.0f;
	self->cone_outer = 360.0f;
	self->cone_outer_gain = 0.0f;
	self->doppler = 1.0f;
	self->speed_of_sound = LRG_AUDIO_SPEED_OF_SOUND;
	self->occlusion_mask = G_MAXUINT32;
	self->occlusion_gain = 0.35f;
	self->occlusion_cutoff = 1200.0f;
	self->occlusion_fade = 0.1f;
	self->occlusion_now = 1.0f;
	self->occlusion_goal = 1.0f;
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
	LrgAudioMixer       *mixer;
	LrgAudioAttenuation  att;
	LrgEntity3D         *entity;
	const gchar         *bank = NULL;
	const gchar         *sound = NULL;
	gfloat               pos[3];

	g_return_val_if_fail (LRG_IS_AUDIO_SOURCE_COMPONENT (self), 0);

	lrg_audio_source_component_stop (self);

	manager = get_manager (self);
	entity = lrg_component_get_entity (LRG_COMPONENT (self));
	if (entity != NULL)
	{
		lrg_entity3d_get_world_position (entity, pos);
		memcpy (self->last_position, pos, sizeof pos);
		self->has_last_position = TRUE;
	}

	/* a mixer voice on the bus */
	mixer = get_mixer (self);
	if (mixer != NULL)
	{
		LrgWaveData *wave = resolve_wave (self);
		const gchar *bus = self->bus != NULL ? self->bus : "master";

		if (wave == NULL)
			return 0;
		self->mixer_voice = lrg_audio_mixer_play (mixer, wave, bus, 0.0, self->loop);
		if (self->mixer_voice == 0)
		{
			g_debug ("LrgAudioSourceComponent: the mixer refused the voice (bus '%s')", bus);
			return 0;
		}
		self->voice_mixer = g_object_ref (mixer);
		if (entity != NULL)
		{
			gfloat lpos[3];

			lrg_audio_manager_get_listener (manager, lpos, NULL, NULL);
			cast_occlusion (self, lpos, pos);
			self->occlusion_now = self->occlusion_goal;
		}
		push_spatial (self, 0.0f);
		return (guint) MIN (self->mixer_voice, (guint64) G_MAXUINT);
	}

	/* a device voice: positional at the entity, flat without one */
	if (!resolve_sound (self, manager, &bank, &sound))
		return 0;
	lrg_audio_source_component_get_attenuation (self, &att);
	self->voice = lrg_audio_manager_play_sound_at (manager, bank, sound,
	                                               (entity != NULL) ? pos : NULL,
	                                               self->volume, &att);
	if (self->voice != 0)
		push_spatial (self, 0.0f);
	return self->voice;
}

void
lrg_audio_source_component_stop (LrgAudioSourceComponent *self)
{
	g_return_if_fail (LRG_IS_AUDIO_SOURCE_COMPONENT (self));

	stop_mixer_voice (self);
	if (self->voice == 0)
		return;
	lrg_audio_manager_stop_voice (get_manager (self), self->voice);
	self->voice = 0;
}

gboolean
lrg_audio_source_component_is_playing (LrgAudioSourceComponent *self)
{
	g_return_val_if_fail (LRG_IS_AUDIO_SOURCE_COMPONENT (self), FALSE);

	if (self->mixer_voice != 0)
		return self->voice_mixer != NULL && lrg_audio_mixer_has_voice (self->voice_mixer, self->mixer_voice);
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

void
lrg_audio_source_component_set_rolloff_curve (LrgAudioSourceComponent *self,
                                              LrgKeyframeCurve        *curve)
{
	LrgAudioRolloff mode;

	g_return_if_fail (LRG_IS_AUDIO_SOURCE_COMPONENT (self));
	g_return_if_fail (curve == NULL || LRG_IS_KEYFRAME_CURVE (curve));

	g_object_freeze_notify (G_OBJECT (self));
	if (g_set_object (&self->rolloff_curve, curve))
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ROLLOFF_CURVE_KEYS]);
	mode = curve != NULL ? LRG_AUDIO_ROLLOFF_CUSTOM
	       : (self->rolloff_mode == LRG_AUDIO_ROLLOFF_CUSTOM ? LRG_AUDIO_ROLLOFF_INVERSE : self->rolloff_mode);
	if (mode != self->rolloff_mode)
	{
		self->rolloff_mode = mode;
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ROLLOFF_MODE]);
	}
	g_object_thaw_notify (G_OBJECT (self));
}

LrgKeyframeCurve *
lrg_audio_source_component_get_rolloff_curve (LrgAudioSourceComponent *self)
{
	g_return_val_if_fail (LRG_IS_AUDIO_SOURCE_COMPONENT (self), NULL);

	return self->rolloff_curve;
}

void
lrg_audio_source_component_get_velocity (LrgAudioSourceComponent *self,
                                         gfloat                  *out_velocity)
{
	g_return_if_fail (LRG_IS_AUDIO_SOURCE_COMPONENT (self));
	g_return_if_fail (out_velocity != NULL);

	memcpy (out_velocity, self->velocity, sizeof self->velocity);
}

gfloat
lrg_audio_source_component_get_occlusion (LrgAudioSourceComponent *self)
{
	g_return_val_if_fail (LRG_IS_AUDIO_SOURCE_COMPONENT (self), 1.0f);

	return self->occlusion_now;
}

guint
lrg_audio_source_component_get_occluders (LrgAudioSourceComponent *self)
{
	g_return_val_if_fail (LRG_IS_AUDIO_SOURCE_COMPONENT (self), 0);

	return self->occluders;
}

LrgAudioMixer *
lrg_audio_source_component_get_mixer (LrgAudioSourceComponent *self)
{
	g_return_val_if_fail (LRG_IS_AUDIO_SOURCE_COMPONENT (self), NULL);

	return self->mixer;
}

void
lrg_audio_source_component_set_mixer (LrgAudioSourceComponent *self,
                                      LrgAudioMixer           *mixer)
{
	g_return_if_fail (LRG_IS_AUDIO_SOURCE_COMPONENT (self));
	g_return_if_fail (mixer == NULL || LRG_IS_AUDIO_MIXER (mixer));

	if (self->mixer == mixer)
		return;
	lrg_audio_source_component_stop (self);
	g_set_object (&self->mixer, mixer);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MIXER]);
}

const gchar *
lrg_audio_source_component_get_bus (LrgAudioSourceComponent *self)
{
	g_return_val_if_fail (LRG_IS_AUDIO_SOURCE_COMPONENT (self), NULL);

	return self->bus;
}

void
lrg_audio_source_component_set_bus (LrgAudioSourceComponent *self,
                                    const gchar             *bus)
{
	g_return_if_fail (LRG_IS_AUDIO_SOURCE_COMPONENT (self));

	set_string (self, &self->bus, bus, PROP_BUS);
}

LrgWaveData *
lrg_audio_source_component_get_wave (LrgAudioSourceComponent *self)
{
	g_return_val_if_fail (LRG_IS_AUDIO_SOURCE_COMPONENT (self), NULL);

	return self->wave;
}

void
lrg_audio_source_component_set_wave (LrgAudioSourceComponent *self,
                                     LrgWaveData             *wave)
{
	g_return_if_fail (LRG_IS_AUDIO_SOURCE_COMPONENT (self));
	g_return_if_fail (wave == NULL || LRG_IS_WAVE_DATA (wave));

	if (g_set_object (&self->wave, wave))
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_WAVE]);
}

guint64
lrg_audio_source_component_get_mixer_voice (LrgAudioSourceComponent *self)
{
	g_return_val_if_fail (LRG_IS_AUDIO_SOURCE_COMPONENT (self), 0);

	return self->mixer_voice;
}

void
lrg_audio_source_component_debug_draw (LrgAudioSourceComponent *self,
                                       LrgDebugDraw            *draw)
{
	LrgEntity3D *entity;
	gfloat       pos[3], fwd[3];
	GrlVector3   center;
	GrlColor     inner_color = { 166, 227, 161, 255 };   /* catppuccin green */
	GrlColor     outer_color = { 250, 179, 135, 255 };   /* peach */
	GrlColor     cone_color = { 137, 180, 250, 255 };    /* blue */
	gfloat       max_d;
	guint        old_category;

	g_return_if_fail (LRG_IS_AUDIO_SOURCE_COMPONENT (self));
	g_return_if_fail (draw == NULL || LRG_IS_DEBUG_DRAW (draw));

	entity = lrg_component_get_entity (LRG_COMPONENT (self));
	if (entity == NULL)
		return;
	if (draw == NULL)
		draw = lrg_debug_draw_get_default ();
	lrg_entity3d_get_world_position (entity, pos);
	center.x = pos[0];
	center.y = pos[1];
	center.z = pos[2];
	max_d = MAX (self->max_distance, self->min_distance);

	old_category = lrg_debug_draw_get_category (draw);
	lrg_debug_draw_set_category (draw, LRG_DEBUG_CATEGORY_AUDIO);
	lrg_debug_draw_sphere (draw, &center, self->min_distance, &inner_color, 0.0f);
	lrg_debug_draw_sphere (draw, &center, max_d, &outer_color, 0.0f);

	/* cones out to max distance along the forward (-Z) */
	if (self->cone_inner < 360.0f)
	{
		gfloat angles[2];
		guint  i;

		angles[0] = self->cone_inner;
		angles[1] = MAX (self->cone_outer, self->cone_inner);
		lrg_entity3d_get_forward (entity, fwd);
		for (i = 0; i < 2; i++)
		{
			gfloat     half = MIN (angles[i], 179.0f) * 0.5f * (gfloat) (G_PI / 180.0);
			gfloat     length = max_d * cosf (half);
			GrlVector3 base;

			base.x = pos[0] + fwd[0] * length;
			base.y = pos[1] + fwd[1] * length;
			base.z = pos[2] + fwd[2] * length;
			if (angles[i] < 180.0f)
				lrg_debug_draw_cone (draw, &center, &base, max_d * sinf (half), i == 0 ? &cone_color : &outer_color,
				                     0.0f);
		}
	}
	lrg_debug_draw_set_category (draw, old_category);
}
