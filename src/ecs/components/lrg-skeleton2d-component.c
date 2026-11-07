/* lrg-skeleton2d-component.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgSkeleton2DComponent: two-track (current + fading out) clip player
 * for an LrgSkeleton2D on an LrgGameObject, with events, and drawing at
 * the owner's render pose.
 */

#include "config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_ECS

#include <math.h>
#include <raylib.h>

#include "lrg-skeleton2d-component.h"
#include "../lrg-game-object.h"
#include "../lrg-component-private.h"
#include "../../lrg-log.h"

/* One playing clip. */
typedef struct
{
	LrgSkeleton2DClip *clip;
	gfloat             time;
	gboolean           loop;
	gboolean           finished;
} Track;

struct _LrgSkeleton2DComponent
{
	LrgComponent   parent_instance;

	LrgSkeleton2D *skeleton;
	gchar         *skeleton_path;
	gboolean       textures_tried;

	Track          cur;
	Track          prev;          /* fading out; clip NULL when none */
	gfloat         fade_time;
	gfloat         fade_duration;

	gfloat         speed;
	gfloat         scale;
	gboolean       flip_x;
	gboolean       flip_y;
	gfloat         default_mix;
	gboolean       show_bones;
	GrlColor       tint;
};

G_DEFINE_FINAL_TYPE (LrgSkeleton2DComponent, lrg_skeleton2d_component, LRG_TYPE_COMPONENT)

enum
{
	PROP_0,
	PROP_SKELETON,
	PROP_SKELETON_PATH,
	PROP_CLIP,
	PROP_SPEED,
	PROP_SCALE,
	PROP_FLIP_X,
	PROP_FLIP_Y,
	PROP_DEFAULT_MIX,
	PROP_SHOW_BONES,
	N_PROPS
};

static GParamSpec *properties[N_PROPS];

enum
{
	SIGNAL_EVENT,
	SIGNAL_FINISHED,
	N_SIGNALS
};

static guint signals[N_SIGNALS];

static void
track_clear (Track *t)
{
	g_clear_object (&t->clip);
	t->time = 0.0f;
	t->loop = FALSE;
	t->finished = FALSE;
}

/* ------------------------------------------------------------------ */
/* Posing                                                              */
/* ------------------------------------------------------------------ */

/* Resets the skeleton to its setup pose and applies the tracks: the
 * fading-out clip fully, then the current clip at its mix weight. */
static void
apply_pose (LrgSkeleton2DComponent *self)
{
	if (self->skeleton == NULL)
		return;
	lrg_skeleton2d_reset_pose (self->skeleton);
	if (self->prev.clip != NULL)
		lrg_skeleton2d_clip_apply (self->prev.clip, self->skeleton, self->prev.time, 1.0f);
	if (self->cur.clip != NULL)
		lrg_skeleton2d_clip_apply (self->cur.clip, self->skeleton, self->cur.time,
		                           lrg_skeleton2d_component_get_mix_weight (self));
}

/* Emits ::event for every event of the current clip in (from, to]
 * (from < 0 includes events at time 0). */
static void
fire_events (LrgSkeleton2DComponent *self,
             gfloat                  from,
             gfloat                  to)
{
	LrgSkeleton2DClip *clip = self->cur.clip;
	guint              n;
	guint              i;

	n = lrg_skeleton2d_clip_get_event_count (clip);
	for (i = 0; i < n && clip == self->cur.clip; i++)
	{
		gfloat       t;
		const gchar *name = lrg_skeleton2d_clip_get_event (clip, i, &t);

		if (t > from && t <= to)
			g_signal_emit (self, signals[SIGNAL_EVENT], 0, lrg_skeleton2d_clip_get_name (clip), name);
	}
}

/* Advances a track; @events fires the current clip's events. */
static void
advance_track (LrgSkeleton2DComponent *self,
               Track                  *t,
               gfloat                  dt,
               gboolean                events)
{
	gfloat duration;
	gfloat from;
	gfloat to;

	if (t->clip == NULL || t->finished)
		return;
	duration = lrg_skeleton2d_clip_get_duration (t->clip);
	from = t->time;
	to = t->time + dt;

	if (to < duration || (duration <= 0.0f && !t->loop))
	{
		if (events && dt > 0.0f)
			fire_events (self, from, to);
		t->time = to;
		if (duration <= 0.0f)
			t->finished = TRUE;
		return;
	}

	if (t->loop && duration > 0.0f)
	{
		/* fire to the end, then every whole loop crossed, then the rest */
		if (events)
			fire_events (self, from, duration);
		to -= duration;
		while (to >= duration)
		{
			if (events && self->cur.clip == t->clip)
				fire_events (self, -1.0f, duration);
			to -= duration;
		}
		if (events && self->cur.clip == t->clip)
			fire_events (self, -1.0f, to);
		t->time = to;
		return;
	}

	/* a one-shot clip reaches its end: hold the last pose */
	if (events)
		fire_events (self, from, duration);
	t->time = duration;
	t->finished = TRUE;
	if (events)
	{
		g_autoptr(LrgSkeleton2DClip) keep = g_object_ref (t->clip);

		g_signal_emit (self, signals[SIGNAL_FINISHED], 0, lrg_skeleton2d_clip_get_name (keep));
	}
}

static void
lrg_skeleton2d_component_update (LrgComponent *component,
                                 gfloat        delta)
{
	LrgSkeleton2DComponent *self = LRG_SKELETON2D_COMPONENT (component);
	gfloat                  dt;

	if (self->skeleton == NULL)
		return;
	dt = delta * self->speed;

	/* the fade advances with playback time, so speed 0 freezes it too */
	if (self->prev.clip != NULL)
	{
		advance_track (self, &self->prev, dt, FALSE);
		self->fade_time += dt;
		if (self->fade_time >= self->fade_duration)
			track_clear (&self->prev);
	}
	advance_track (self, &self->cur, dt, TRUE);
	apply_pose (self);
}

/* ------------------------------------------------------------------ */
/* GObject                                                             */
/* ------------------------------------------------------------------ */

static void
lrg_skeleton2d_component_dispose (GObject *object)
{
	LrgSkeleton2DComponent *self = LRG_SKELETON2D_COMPONENT (object);

	track_clear (&self->cur);
	track_clear (&self->prev);
	g_clear_object (&self->skeleton);

	G_OBJECT_CLASS (lrg_skeleton2d_component_parent_class)->dispose (object);
}

static void
lrg_skeleton2d_component_finalize (GObject *object)
{
	LrgSkeleton2DComponent *self = LRG_SKELETON2D_COMPONENT (object);

	g_clear_pointer (&self->skeleton_path, g_free);

	G_OBJECT_CLASS (lrg_skeleton2d_component_parent_class)->finalize (object);
}

static void
set_float_prop (LrgSkeleton2DComponent *self,
                gfloat                 *field,
                gfloat                  value,
                guint                   prop)
{
	if (*field == value)
		return;
	*field = value;
	g_object_notify_by_pspec (G_OBJECT (self), properties[prop]);
}

static void
set_bool_prop (LrgSkeleton2DComponent *self,
               gboolean               *field,
               gboolean                value,
               guint                   prop)
{
	value = !!value;
	if (*field == value)
		return;
	*field = value;
	g_object_notify_by_pspec (G_OBJECT (self), properties[prop]);
}

static void
lrg_skeleton2d_component_get_property (GObject    *object,
                                       guint       prop_id,
                                       GValue     *value,
                                       GParamSpec *pspec)
{
	LrgSkeleton2DComponent *self = LRG_SKELETON2D_COMPONENT (object);

	switch (prop_id)
	{
	case PROP_SKELETON:
		g_value_set_object (value, self->skeleton);
		break;
	case PROP_SKELETON_PATH:
		g_value_set_string (value, self->skeleton_path);
		break;
	case PROP_CLIP:
		g_value_set_string (value, lrg_skeleton2d_component_get_clip (self));
		break;
	case PROP_SPEED:
		g_value_set_float (value, self->speed);
		break;
	case PROP_SCALE:
		g_value_set_float (value, self->scale);
		break;
	case PROP_FLIP_X:
		g_value_set_boolean (value, self->flip_x);
		break;
	case PROP_FLIP_Y:
		g_value_set_boolean (value, self->flip_y);
		break;
	case PROP_DEFAULT_MIX:
		g_value_set_float (value, self->default_mix);
		break;
	case PROP_SHOW_BONES:
		g_value_set_boolean (value, self->show_bones);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_skeleton2d_component_set_property (GObject      *object,
                                       guint         prop_id,
                                       const GValue *value,
                                       GParamSpec   *pspec)
{
	LrgSkeleton2DComponent *self = LRG_SKELETON2D_COMPONENT (object);

	switch (prop_id)
	{
	case PROP_SKELETON:
		lrg_skeleton2d_component_set_skeleton (self, g_value_get_object (value));
		break;
	case PROP_SKELETON_PATH:
		{
			const gchar       *path = g_value_get_string (value);
			g_autoptr(GError)  error = NULL;

			if (g_strcmp0 (path, self->skeleton_path) == 0)
				break;
			if (path != NULL && !lrg_skeleton2d_component_load (self, path, &error))
				lrg_warning (LRG_LOG_DOMAIN_ECS, "skeleton2d component: %s", error->message);
			g_free (self->skeleton_path);
			self->skeleton_path = g_strdup (path);
			g_object_notify_by_pspec (object, properties[PROP_SKELETON_PATH]);
		}
		break;
	case PROP_CLIP:
		{
			const gchar       *name = g_value_get_string (value);
			LrgSkeleton2DClip *clip;

			if (name == NULL)
			{
				lrg_skeleton2d_component_stop (self);
				break;
			}
			clip = self->skeleton != NULL ? lrg_skeleton2d_get_clip (self->skeleton, name) : NULL;
			if (clip == NULL)
				lrg_warning (LRG_LOG_DOMAIN_ECS, "skeleton2d component: no clip \"%s\"", name);
			else
				lrg_skeleton2d_component_play (self, name, lrg_skeleton2d_clip_get_loop (clip));
		}
		break;
	case PROP_SPEED:
		lrg_skeleton2d_component_set_speed (self, g_value_get_float (value));
		break;
	case PROP_SCALE:
		set_float_prop (self, &self->scale, g_value_get_float (value), PROP_SCALE);
		break;
	case PROP_FLIP_X:
		set_bool_prop (self, &self->flip_x, g_value_get_boolean (value), PROP_FLIP_X);
		break;
	case PROP_FLIP_Y:
		set_bool_prop (self, &self->flip_y, g_value_get_boolean (value), PROP_FLIP_Y);
		break;
	case PROP_DEFAULT_MIX:
		set_float_prop (self, &self->default_mix, g_value_get_float (value), PROP_DEFAULT_MIX);
		break;
	case PROP_SHOW_BONES:
		set_bool_prop (self, &self->show_bones, g_value_get_boolean (value), PROP_SHOW_BONES);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_skeleton2d_component_class_init (LrgSkeleton2DComponentClass *klass)
{
	GObjectClass      *object_class = G_OBJECT_CLASS (klass);
	LrgComponentClass *component_class = LRG_COMPONENT_CLASS (klass);

	object_class->dispose = lrg_skeleton2d_component_dispose;
	object_class->finalize = lrg_skeleton2d_component_finalize;
	object_class->get_property = lrg_skeleton2d_component_get_property;
	object_class->set_property = lrg_skeleton2d_component_set_property;
	component_class->update = lrg_skeleton2d_component_update;

	/**
	 * LrgSkeleton2DComponent:skeleton:
	 *
	 * The skeleton the component animates and draws.
	 */
	properties[PROP_SKELETON] =
		g_param_spec_object ("skeleton", "Skeleton", "The skeleton to animate and draw",
		                     LRG_TYPE_SKELETON2D,
		                     G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgSkeleton2DComponent:skeleton-path:
	 *
	 * A `.rskel2d` file to load as the skeleton (the level and prefab
	 * parameter `skeleton_path`).  Load failures are logged; use
	 * lrg_skeleton2d_component_load() to get the error.
	 */
	properties[PROP_SKELETON_PATH] =
		g_param_spec_string ("skeleton-path", "Skeleton path", ".rskel2d file to load", NULL,
		                     G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgSkeleton2DComponent:clip:
	 *
	 * The current clip.  Setting it plays the clip from the start (looping
	 * when the clip's #LrgSkeleton2DClip:loop is set); %NULL stops.
	 */
	properties[PROP_CLIP] =
		g_param_spec_string ("clip", "Clip", "Current clip", NULL,
		                     G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgSkeleton2DComponent:speed:
	 *
	 * Playback speed factor (0 pauses).
	 */
	properties[PROP_SPEED] =
		g_param_spec_float ("speed", "Speed", "Playback speed factor", 0.0f, 100.0f, 1.0f,
		                    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgSkeleton2DComponent:scale:
	 *
	 * Uniform scale applied on top of the owner's scale.
	 */
	properties[PROP_SCALE] =
		g_param_spec_float ("scale", "Scale", "Uniform scale on top of the owner's", 0.0f, 1000.0f, 1.0f,
		                    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgSkeleton2DComponent:flip-x:
	 *
	 * Mirrors the skeleton horizontally (a character facing left).
	 */
	properties[PROP_FLIP_X] =
		g_param_spec_boolean ("flip-x", "Flip X", "Mirror horizontally", FALSE,
		                      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgSkeleton2DComponent:flip-y:
	 *
	 * Mirrors the skeleton vertically.
	 */
	properties[PROP_FLIP_Y] =
		g_param_spec_boolean ("flip-y", "Flip Y", "Mirror vertically", FALSE,
		                      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgSkeleton2DComponent:default-mix:
	 *
	 * Crossfade length used when lrg_skeleton2d_component_crossfade() is
	 * given a negative duration, in seconds.
	 */
	properties[PROP_DEFAULT_MIX] =
		g_param_spec_float ("default-mix", "Default mix", "Default crossfade length in seconds",
		                    0.0f, 60.0f, 0.2f,
		                    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgSkeleton2DComponent:show-bones:
	 *
	 * Draws the bones over the skeleton (debugging, the editor's rig
	 * overlay).
	 */
	properties[PROP_SHOW_BONES] =
		g_param_spec_boolean ("show-bones", "Show bones", "Draw the bones over the skeleton", FALSE,
		                      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	g_object_class_install_properties (object_class, N_PROPS, properties);

	/**
	 * LrgSkeleton2DComponent::event:
	 * @self: the component
	 * @clip: the clip's name
	 * @event: the event's name
	 *
	 * Emitted during update when playback of the current clip crosses an
	 * event key (once per loop).  Events of a clip that is fading out do
	 * not fire.
	 */
	signals[SIGNAL_EVENT] =
		g_signal_new ("event", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
		              G_TYPE_NONE, 2, G_TYPE_STRING, G_TYPE_STRING);

	/**
	 * LrgSkeleton2DComponent::finished:
	 * @self: the component
	 * @clip: the clip's name
	 *
	 * Emitted when a non-looping current clip reaches its end (it then
	 * holds its last pose).
	 */
	signals[SIGNAL_FINISHED] =
		g_signal_new ("finished", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
		              G_TYPE_NONE, 1, G_TYPE_STRING);
}

static void
lrg_skeleton2d_component_init (LrgSkeleton2DComponent *self)
{
	self->speed = 1.0f;
	self->scale = 1.0f;
	self->default_mix = 0.2f;
	self->tint.r = self->tint.g = self->tint.b = self->tint.a = 255;
}

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

LrgSkeleton2DComponent *
lrg_skeleton2d_component_new (void)
{
	return g_object_new (LRG_TYPE_SKELETON2D_COMPONENT, NULL);
}

LrgSkeleton2DComponent *
lrg_skeleton2d_component_new_with_skeleton (LrgSkeleton2D *skeleton)
{
	return g_object_new (LRG_TYPE_SKELETON2D_COMPONENT, "skeleton", skeleton, NULL);
}

void
lrg_skeleton2d_component_set_skeleton (LrgSkeleton2DComponent *self,
                                       LrgSkeleton2D          *skeleton)
{
	g_return_if_fail (LRG_IS_SKELETON2D_COMPONENT (self));
	g_return_if_fail (skeleton == NULL || LRG_IS_SKELETON2D (skeleton));

	if (self->skeleton == skeleton)
		return;
	track_clear (&self->cur);
	track_clear (&self->prev);
	g_set_object (&self->skeleton, skeleton);
	self->textures_tried = FALSE;
	if (skeleton != NULL)
	{
		lrg_skeleton2d_reset_pose (skeleton);
		lrg_skeleton2d_update_world (skeleton);
	}
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SKELETON]);
}

LrgSkeleton2D *
lrg_skeleton2d_component_get_skeleton (LrgSkeleton2DComponent *self)
{
	g_return_val_if_fail (LRG_IS_SKELETON2D_COMPONENT (self), NULL);

	return self->skeleton;
}

gboolean
lrg_skeleton2d_component_load (LrgSkeleton2DComponent  *self,
                               const gchar             *path,
                               GError                 **error)
{
	g_autoptr(LrgSkeleton2D) skeleton = NULL;

	g_return_val_if_fail (LRG_IS_SKELETON2D_COMPONENT (self), FALSE);
	g_return_val_if_fail (path != NULL, FALSE);

	skeleton = lrg_skeleton2d_new_from_file (path, error);
	if (skeleton == NULL)
		return FALSE;
	lrg_skeleton2d_component_set_skeleton (self, skeleton);
	return TRUE;
}

static gboolean
start_clip (LrgSkeleton2DComponent *self,
            const gchar            *name,
            gboolean                loop,
            gfloat                  fade)
{
	LrgSkeleton2DClip *clip;

	if (self->skeleton == NULL || name == NULL)
		return FALSE;
	clip = lrg_skeleton2d_get_clip (self->skeleton, name);
	if (clip == NULL)
		return FALSE;

	if (fade > 0.0f && self->cur.clip != NULL)
	{
		/* the current clip fades out; whatever was already fading drops */
		track_clear (&self->prev);
		self->prev = self->cur;
		self->cur.clip = NULL;
		self->fade_time = 0.0f;
		self->fade_duration = fade;
	}
	else
	{
		track_clear (&self->prev);
		track_clear (&self->cur);
	}
	self->cur.clip = g_object_ref (clip);
	self->cur.time = 0.0f;
	self->cur.loop = loop;
	self->cur.finished = FALSE;

	/* events keyed at time 0 fire as playback starts */
	fire_events (self, -1.0f, 0.0f);
	apply_pose (self);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_CLIP]);
	return TRUE;
}

gboolean
lrg_skeleton2d_component_play (LrgSkeleton2DComponent *self,
                               const gchar            *clip,
                               gboolean                loop)
{
	g_return_val_if_fail (LRG_IS_SKELETON2D_COMPONENT (self), FALSE);

	return start_clip (self, clip, loop, 0.0f);
}

gboolean
lrg_skeleton2d_component_crossfade (LrgSkeleton2DComponent *self,
                                    const gchar            *clip,
                                    gfloat                  duration,
                                    gboolean                loop)
{
	g_return_val_if_fail (LRG_IS_SKELETON2D_COMPONENT (self), FALSE);

	return start_clip (self, clip, loop, duration < 0.0f ? self->default_mix : duration);
}

void
lrg_skeleton2d_component_stop (LrgSkeleton2DComponent *self)
{
	gboolean had;

	g_return_if_fail (LRG_IS_SKELETON2D_COMPONENT (self));

	had = self->cur.clip != NULL;
	track_clear (&self->cur);
	track_clear (&self->prev);
	if (self->skeleton != NULL)
		lrg_skeleton2d_reset_pose (self->skeleton);
	if (had)
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_CLIP]);
}

const gchar *
lrg_skeleton2d_component_get_clip (LrgSkeleton2DComponent *self)
{
	g_return_val_if_fail (LRG_IS_SKELETON2D_COMPONENT (self), NULL);

	return self->cur.clip != NULL ? lrg_skeleton2d_clip_get_name (self->cur.clip) : NULL;
}

gboolean
lrg_skeleton2d_component_is_playing (LrgSkeleton2DComponent *self)
{
	g_return_val_if_fail (LRG_IS_SKELETON2D_COMPONENT (self), FALSE);

	return self->cur.clip != NULL && !self->cur.finished;
}

gfloat
lrg_skeleton2d_component_get_time (LrgSkeleton2DComponent *self)
{
	g_return_val_if_fail (LRG_IS_SKELETON2D_COMPONENT (self), 0.0f);

	return self->cur.time;
}

void
lrg_skeleton2d_component_set_time (LrgSkeleton2DComponent *self,
                                   gfloat                  time)
{
	gfloat duration;

	g_return_if_fail (LRG_IS_SKELETON2D_COMPONENT (self));

	if (self->cur.clip == NULL)
		return;
	duration = lrg_skeleton2d_clip_get_duration (self->cur.clip);
	self->cur.time = CLAMP (time, 0.0f, duration);
	self->cur.finished = !self->cur.loop && duration > 0.0f && self->cur.time >= duration;
	apply_pose (self);
}

gfloat
lrg_skeleton2d_component_get_mix_weight (LrgSkeleton2DComponent *self)
{
	g_return_val_if_fail (LRG_IS_SKELETON2D_COMPONENT (self), 1.0f);

	if (self->prev.clip == NULL || self->fade_duration <= 0.0f)
		return 1.0f;
	return CLAMP (self->fade_time / self->fade_duration, 0.0f, 1.0f);
}

gfloat
lrg_skeleton2d_component_get_speed (LrgSkeleton2DComponent *self)
{
	g_return_val_if_fail (LRG_IS_SKELETON2D_COMPONENT (self), 1.0f);

	return self->speed;
}

void
lrg_skeleton2d_component_set_speed (LrgSkeleton2DComponent *self,
                                    gfloat                  speed)
{
	g_return_if_fail (LRG_IS_SKELETON2D_COMPONENT (self));

	set_float_prop (self, &self->speed, CLAMP (speed, 0.0f, 100.0f), PROP_SPEED);
}

void
lrg_skeleton2d_component_set_tint (LrgSkeleton2DComponent *self,
                                   const GrlColor         *tint)
{
	g_return_if_fail (LRG_IS_SKELETON2D_COMPONENT (self));

	if (tint != NULL)
		self->tint = *tint;
	else
		self->tint.r = self->tint.g = self->tint.b = self->tint.a = 255;
}

GrlColor *
lrg_skeleton2d_component_get_tint (LrgSkeleton2DComponent *self)
{
	g_return_val_if_fail (LRG_IS_SKELETON2D_COMPONENT (self), NULL);

	return grl_color_copy (&self->tint);
}

void
lrg_skeleton2d_component_sync_pose (LrgSkeleton2DComponent *self)
{
	LrgGameObject *owner;
	gfloat         x = 0.0f;
	gfloat         y = 0.0f;
	gfloat         rotation = 0.0f;
	gfloat         s = self->scale;

	g_return_if_fail (LRG_IS_SKELETON2D_COMPONENT (self));

	if (self->skeleton == NULL)
		return;
	owner = _lrg_component_get_owner_unchecked (LRG_COMPONENT (self));
	if (owner != NULL)
	{
		GrlEntity *entity = GRL_ENTITY (owner);

		grl_entity_get_render_pose (entity, &x, &y, &rotation, NULL);
		s *= grl_entity_get_scale (entity);
	}
	lrg_skeleton2d_set_root_transform (self->skeleton, x, y, rotation,
	                                   self->flip_x ? -s : s, self->flip_y ? -s : s);
	lrg_skeleton2d_update_world (self->skeleton);
}

void
lrg_skeleton2d_component_draw (LrgSkeleton2DComponent *self)
{
	g_return_if_fail (LRG_IS_SKELETON2D_COMPONENT (self));

	if (self->skeleton == NULL)
		return;

	/* textures load once, on the first draw (a window exists by now) */
	if (!self->textures_tried)
	{
		g_autoptr(GError) error = NULL;

		self->textures_tried = TRUE;
		if (!lrg_skeleton2d_load_textures (self->skeleton, &error))
			lrg_warning (LRG_LOG_DOMAIN_ECS, "skeleton2d component: %s", error->message);
	}

	lrg_skeleton2d_component_sync_pose (self);
	lrg_skeleton2d_draw (self->skeleton, &self->tint);
	if (self->show_bones)
		lrg_skeleton2d_draw_bones (self->skeleton, NULL);
}
