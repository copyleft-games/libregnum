/* lrg-weather-asset.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * The `.rweather` format, its wind and particle layer effects and its
 * runtime (building and live-syncing LrgWeather objects).
 *
 * Data model: an asset is a list of presets, a preset a list of layers,
 * a layer a kind (a registered LrgWeatherEffect subtype), an id, an
 * intensity, an enabled flag and parameters. Every parameter is described
 * by a GParamSpec (the effect's own properties, plus "format-only"
 * parameters synthesised here for settings the effects expose only
 * through setters: colours, areas, volumes, bolt shape, seeds, and the
 * particle emitter's description), so loading, saving, validation and the
 * editor's inspector all work from one schema. Parameters are stored as
 * their scalar text: a loaded file is written back exactly as it was read
 * (byte-stable), values set through the API are normalised (floats with
 * six significant digits, colours as #rrggbbaa, enums by nick).
 */

#include "config.h"

#include <math.h>
#include <string.h>
#include <yaml-glib.h>

#include "lrg-weather-asset.h"
#include "lrg-rain.h"
#include "lrg-snow.h"
#include "lrg-fog.h"
#include "lrg-lightning.h"
#include "../lrg-log.h"

/* ==========================================================================
 * Error domain
 * ========================================================================== */

G_DEFINE_QUARK (lrg-weather-asset-error-quark, lrg_weather_asset_error)

GType
lrg_weather_asset_error_get_type (void)
{
	static gsize type_id = 0;

	if (g_once_init_enter (&type_id))
	{
		static const GEnumValue values[] = {
			{ LRG_WEATHER_ASSET_ERROR_PARSE, "LRG_WEATHER_ASSET_ERROR_PARSE", "parse" },
			{ LRG_WEATHER_ASSET_ERROR_INVALID_VALUE, "LRG_WEATHER_ASSET_ERROR_INVALID_VALUE", "invalid-value" },
			{ LRG_WEATHER_ASSET_ERROR_UNKNOWN_KIND, "LRG_WEATHER_ASSET_ERROR_UNKNOWN_KIND", "unknown-kind" },
			{ LRG_WEATHER_ASSET_ERROR_UNKNOWN_PARAM, "LRG_WEATHER_ASSET_ERROR_UNKNOWN_PARAM", "unknown-param" },
			{ LRG_WEATHER_ASSET_ERROR_DUPLICATE, "LRG_WEATHER_ASSET_ERROR_DUPLICATE", "duplicate" },
			{ LRG_WEATHER_ASSET_ERROR_NOT_FOUND, "LRG_WEATHER_ASSET_ERROR_NOT_FOUND", "not-found" },
			{ LRG_WEATHER_ASSET_ERROR_IO, "LRG_WEATHER_ASSET_ERROR_IO", "io" },
			{ 0, NULL, NULL }
		};
		GType t = g_enum_register_static (g_intern_static_string ("LrgWeatherAssetError"), values);

		g_once_init_leave (&type_id, t);
	}
	return (GType) type_id;
}

/* ==========================================================================
 * LrgWeatherWind
 * ========================================================================== */

struct _LrgWeatherWind
{
	LrgWeatherEffect parent_instance;

	gfloat  speed[2];
	gfloat  gust_strength;
	gfloat  gust_frequency;
	guint   seed;
	gdouble time;
	gfloat  current[2];
};

G_DEFINE_FINAL_TYPE (LrgWeatherWind, lrg_weather_wind, LRG_TYPE_WEATHER_EFFECT)

enum
{
	WIND_PROP_0,
	WIND_PROP_SPEED_X,
	WIND_PROP_SPEED_Y,
	WIND_PROP_GUST_STRENGTH,
	WIND_PROP_GUST_FREQUENCY,
	WIND_PROP_SEED,
	WIND_N_PROPS
};

static GParamSpec *wind_props[WIND_N_PROPS];

/* wind_phase: a deterministic phase in [0, 2 pi) from the seed */
static gfloat
wind_phase (guint seed,
            guint salt)
{
	guint32 x = (guint32) seed * 0x9e3779b9u + salt * 0x85ebca6bu;

	x ^= x >> 16;
	x *= 0x7feb352du;
	x ^= x >> 15;
	return (gfloat) (x & 0xffffu) / 65536.0f * (gfloat) (2.0 * G_PI);
}

/* recompute: the current wind from the clock, gusts and intensity */
static void
lrg_weather_wind_recompute (LrgWeatherWind *self)
{
	LrgWeatherEffect *effect = LRG_WEATHER_EFFECT (self);
	gfloat            len, dir_x = 1.0f, dir_y = 0.0f, gust, k;
	gdouble           w = 2.0 * G_PI * (gdouble) self->gust_frequency * self->time;

	if (!lrg_weather_effect_is_active (effect))
	{
		self->current[0] = 0.0f;
		self->current[1] = 0.0f;
		return;
	}
	len = sqrtf (self->speed[0] * self->speed[0] + self->speed[1] * self->speed[1]);
	if (len > 1e-6f)
	{
		dir_x = self->speed[0] / len;
		dir_y = self->speed[1] / len;
	}
	/* two incommensurate sines: gusts that never visibly repeat */
	gust = self->gust_strength * (0.6f * (gfloat) sin (w + (gdouble) wind_phase (self->seed, 1)) +
	                              0.4f * (gfloat) sin (2.3 * w + (gdouble) wind_phase (self->seed, 2)));
	k = lrg_weather_effect_get_intensity (effect);
	self->current[0] = (self->speed[0] + dir_x * gust) * k;
	self->current[1] = (self->speed[1] + dir_y * gust) * k;
}

static void
lrg_weather_wind_update (LrgWeatherEffect *effect,
                         gfloat            delta_time)
{
	LrgWeatherWind *self = LRG_WEATHER_WIND (effect);

	LRG_WEATHER_EFFECT_CLASS (lrg_weather_wind_parent_class)->update (effect, delta_time);
	if (lrg_weather_effect_is_active (effect))
		self->time += (gdouble) MAX (delta_time, 0.0f);
	lrg_weather_wind_recompute (self);
}

static void
lrg_weather_wind_activate (LrgWeatherEffect *effect)
{
	LRG_WEATHER_EFFECT_CLASS (lrg_weather_wind_parent_class)->activate (effect);
	lrg_weather_wind_recompute (LRG_WEATHER_WIND (effect));
}

static void
lrg_weather_wind_deactivate (LrgWeatherEffect *effect)
{
	LRG_WEATHER_EFFECT_CLASS (lrg_weather_wind_parent_class)->deactivate (effect);
	lrg_weather_wind_recompute (LRG_WEATHER_WIND (effect));
}

static void
lrg_weather_wind_get_property (GObject    *object,
                               guint       prop_id,
                               GValue     *value,
                               GParamSpec *pspec)
{
	LrgWeatherWind *self = LRG_WEATHER_WIND (object);

	switch (prop_id)
	{
	case WIND_PROP_SPEED_X:
		g_value_set_float (value, self->speed[0]);
		break;
	case WIND_PROP_SPEED_Y:
		g_value_set_float (value, self->speed[1]);
		break;
	case WIND_PROP_GUST_STRENGTH:
		g_value_set_float (value, self->gust_strength);
		break;
	case WIND_PROP_GUST_FREQUENCY:
		g_value_set_float (value, self->gust_frequency);
		break;
	case WIND_PROP_SEED:
		g_value_set_uint (value, self->seed);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

/* set_wind_float: a float field with explicit notify */
static void
set_wind_float (LrgWeatherWind *self,
                gfloat         *field,
                gfloat          value,
                guint           prop)
{
	if (*field == value)
		return;
	*field = value;
	lrg_weather_wind_recompute (self);
	g_object_notify_by_pspec (G_OBJECT (self), wind_props[prop]);
}

static void
lrg_weather_wind_set_property (GObject      *object,
                               guint         prop_id,
                               const GValue *value,
                               GParamSpec   *pspec)
{
	LrgWeatherWind *self = LRG_WEATHER_WIND (object);

	switch (prop_id)
	{
	case WIND_PROP_SPEED_X:
		set_wind_float (self, &self->speed[0], g_value_get_float (value), prop_id);
		break;
	case WIND_PROP_SPEED_Y:
		set_wind_float (self, &self->speed[1], g_value_get_float (value), prop_id);
		break;
	case WIND_PROP_GUST_STRENGTH:
		set_wind_float (self, &self->gust_strength, g_value_get_float (value), prop_id);
		break;
	case WIND_PROP_GUST_FREQUENCY:
		set_wind_float (self, &self->gust_frequency, g_value_get_float (value), prop_id);
		break;
	case WIND_PROP_SEED:
		if (self->seed != g_value_get_uint (value))
		{
			self->seed = g_value_get_uint (value);
			lrg_weather_wind_recompute (self);
			g_object_notify_by_pspec (object, pspec);
		}
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_weather_wind_class_init (LrgWeatherWindClass *klass)
{
	GObjectClass          *object_class = G_OBJECT_CLASS (klass);
	LrgWeatherEffectClass *effect_class = LRG_WEATHER_EFFECT_CLASS (klass);

	object_class->get_property = lrg_weather_wind_get_property;
	object_class->set_property = lrg_weather_wind_set_property;
	effect_class->update = lrg_weather_wind_update;
	effect_class->activate = lrg_weather_wind_activate;
	effect_class->deactivate = lrg_weather_wind_deactivate;

	/**
	 * LrgWeatherWind:speed-x:
	 *
	 * Steady wind X at full intensity (world X / screen X).
	 */
	wind_props[WIND_PROP_SPEED_X] =
		g_param_spec_float ("speed-x", "Speed X", "Steady wind X", -G_MAXFLOAT, G_MAXFLOAT, 2.0f,
		                    G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

	/**
	 * LrgWeatherWind:speed-y:
	 *
	 * Steady wind Y at full intensity (world Z / screen Y).
	 */
	wind_props[WIND_PROP_SPEED_Y] =
		g_param_spec_float ("speed-y", "Speed Y", "Steady wind Y", -G_MAXFLOAT, G_MAXFLOAT, 0.0f,
		                    G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

	/**
	 * LrgWeatherWind:gust-strength:
	 *
	 * Largest gust, added along the wind direction.
	 */
	wind_props[WIND_PROP_GUST_STRENGTH] =
		g_param_spec_float ("gust-strength", "Gust strength", "Largest gust", 0.0f, G_MAXFLOAT, 0.0f,
		                    G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

	/**
	 * LrgWeatherWind:gust-frequency:
	 *
	 * Gusts per second (the base of two mixed sines).
	 */
	wind_props[WIND_PROP_GUST_FREQUENCY] =
		g_param_spec_float ("gust-frequency", "Gust frequency", "Gusts per second", 0.0f, 100.0f, 0.25f,
		                    G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

	/**
	 * LrgWeatherWind:seed:
	 *
	 * Seed of the gust phases.
	 */
	wind_props[WIND_PROP_SEED] =
		g_param_spec_uint ("seed", "Seed", "Gust phase seed", 0, G_MAXUINT32, 0,
		                   G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

	g_object_class_install_properties (object_class, WIND_N_PROPS, wind_props);
}

static void
lrg_weather_wind_init (LrgWeatherWind *self)
{
	self->speed[0] = 2.0f;
	self->gust_frequency = 0.25f;
}

LrgWeatherWind *
lrg_weather_wind_new (void)
{
	return g_object_new (LRG_TYPE_WEATHER_WIND, "id", "wind", NULL);
}

void
lrg_weather_wind_get_current (LrgWeatherWind *self,
                              gfloat         *wind_x,
                              gfloat         *wind_y)
{
	g_return_if_fail (LRG_IS_WEATHER_WIND (self));
	if (wind_x != NULL)
		*wind_x = self->current[0];
	if (wind_y != NULL)
		*wind_y = self->current[1];
}

/* ==========================================================================
 * LrgWeatherParticles
 * ========================================================================== */

#define WEATHER_PARTICLES_DEFAULT_CAPACITY (20000u)

struct _LrgWeatherParticles
{
	LrgWeatherEffect parent_instance;

	LrgGpuParticles   *emitter;
	LrgParticleEffect *effect;     /* CPU fallback for an .rparticles asset */
	guint            capacity;
	gboolean         gpu;
	gboolean         follow_camera;
	gfloat           offset[3];
	gdouble          accum;
};

G_DEFINE_FINAL_TYPE (LrgWeatherParticles, lrg_weather_particles, LRG_TYPE_WEATHER_EFFECT)

enum
{
	PART_PROP_0,
	PART_PROP_CAPACITY,
	PART_PROP_GPU,
	PART_PROP_FOLLOW_CAMERA,
	PART_PROP_OFFSET_X,
	PART_PROP_OFFSET_Y,
	PART_PROP_OFFSET_Z,
	PART_N_PROPS
};

static GParamSpec *part_props[PART_N_PROPS];

/* the emitter a fresh layer starts with: falling, camera-sized */
static void
weather_particles_defaults (LrgGpuParticles *emitter)
{
	GrlColor start = { 220, 225, 235, 200 };
	GrlColor end = { 220, 225, 235, 0 };

	g_object_set (emitter,
	              "emitting", FALSE,          /* the layer emits by intensity */
	              "shape", LRG_GPU_PARTICLES_SHAPE_BOX,
	              "box-x", 40.0f, "box-y", 1.0f, "box-z", 40.0f,
	              "direction-x", 0.0f, "direction-y", -1.0f, "direction-z", 0.0f,
	              "cone-angle", 5.0f,
	              "speed-min", 1.0f, "speed-max", 2.0f,
	              "lifetime-min", 6.0f, "lifetime-max", 8.0f,
	              "size-start", 0.08f, "size-end", 0.08f,
	              "drag", 0.5f,
	              "emission-rate", 1000.0f,
	              NULL);
	lrg_gpu_particles_set_colors (emitter, &start, &end);
}

static void
lrg_weather_particles_update (LrgWeatherEffect *effect,
                              gfloat            delta_time)
{
	LrgWeatherParticles *self = LRG_WEATHER_PARTICLES (effect);
	gfloat               rate = 0.0f;
	guint                n;

	LRG_WEATHER_EFFECT_CLASS (lrg_weather_particles_parent_class)->update (effect, delta_time);
	if (!lrg_weather_effect_is_active (effect))
		return;

	/* the emitter's rate is the rate at full intensity */
	g_object_get (self->emitter, "emission-rate", &rate, NULL);
	self->accum += (gdouble) rate * (gdouble) lrg_weather_effect_get_intensity (effect) *
	               (gdouble) MAX (delta_time, 0.0f);
	n = (guint) floor (self->accum);
	self->accum -= (gdouble) n;
	if (n > 0)
		lrg_gpu_particles_emit (self->emitter, n);

	if (self->follow_camera)
	{
		GrlVector3 cam;

		lrg_weather_effect_get_camera_position (effect, &cam);
		lrg_gpu_particles_set_position (self->emitter, cam.x + self->offset[0],
		                                cam.y + self->offset[1], cam.z + self->offset[2]);
	}
	else
		lrg_gpu_particles_set_position (self->emitter, self->offset[0], self->offset[1], self->offset[2]);
	if (self->effect != NULL)
	{
		gfloat x, y, z;

		lrg_gpu_particles_get_position (self->emitter, &x, &y, &z);
		lrg_particle_effect_set_position (self->effect, x, y, z);
		lrg_particle_effect_update (self->effect, delta_time);
		return;
	}
	lrg_gpu_particles_update (self->emitter, delta_time);
}

static void
lrg_weather_particles_set_wind (LrgWeatherEffect *effect,
                                gfloat            wind_x,
                                gfloat            wind_y)
{
	LrgWeatherParticles *self = LRG_WEATHER_PARTICLES (effect);

	LRG_WEATHER_EFFECT_CLASS (lrg_weather_particles_parent_class)->set_wind (effect, wind_x, wind_y);
	lrg_gpu_particles_set_wind (self->emitter, wind_x, 0.0f, wind_y);
}

static void
lrg_weather_particles_render_world (LrgWeatherEffect *effect,
                                    const GrlVector3 *camera_position)
{
	LrgWeatherParticles *self = LRG_WEATHER_PARTICLES (effect);

	if (!lrg_weather_effect_is_active (effect))
		return;
	if (self->effect != NULL)
	{
		const gfloat eye[3] = { camera_position->x, camera_position->y, camera_position->z };
		const gfloat up[3] = { 0.0f, 1.0f, 0.0f };

		if (lrg_weather_effect_get_intensity (effect) > 0.01f)
			lrg_particle_effect_draw (self->effect, eye, up);
		return;
	}
	lrg_gpu_particles_draw (self->emitter);
}

/* replace_emitter: a new emitter of @capacity with the old parameters */
static void
replace_emitter (LrgWeatherParticles *self,
                 guint                capacity)
{
	g_autoptr(LrgGpuParticles) fresh = lrg_gpu_particles_new (capacity);

	lrg_gpu_particles_copy_parameters (fresh, self->emitter);
	g_object_set (fresh, "emitting", FALSE, "prefer-gpu", self->gpu, NULL);
	g_set_object (&self->emitter, fresh);
	self->capacity = capacity;
}

static void
lrg_weather_particles_get_property (GObject    *object,
                                    guint       prop_id,
                                    GValue     *value,
                                    GParamSpec *pspec)
{
	LrgWeatherParticles *self = LRG_WEATHER_PARTICLES (object);

	switch (prop_id)
	{
	case PART_PROP_CAPACITY:
		g_value_set_uint (value, self->capacity);
		break;
	case PART_PROP_GPU:
		g_value_set_boolean (value, self->gpu);
		break;
	case PART_PROP_FOLLOW_CAMERA:
		g_value_set_boolean (value, self->follow_camera);
		break;
	case PART_PROP_OFFSET_X:
	case PART_PROP_OFFSET_Y:
	case PART_PROP_OFFSET_Z:
		g_value_set_float (value, self->offset[prop_id - PART_PROP_OFFSET_X]);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_weather_particles_set_property (GObject      *object,
                                    guint         prop_id,
                                    const GValue *value,
                                    GParamSpec   *pspec)
{
	LrgWeatherParticles *self = LRG_WEATHER_PARTICLES (object);

	switch (prop_id)
	{
	case PART_PROP_CAPACITY:
		if (self->capacity != g_value_get_uint (value))
		{
			replace_emitter (self, g_value_get_uint (value));
			g_object_notify_by_pspec (object, pspec);
		}
		break;
	case PART_PROP_GPU:
		if (self->gpu != g_value_get_boolean (value))
		{
			self->gpu = g_value_get_boolean (value);
			lrg_gpu_particles_set_prefer_gpu (self->emitter, self->gpu);
			g_object_notify_by_pspec (object, pspec);
		}
		break;
	case PART_PROP_FOLLOW_CAMERA:
		if (self->follow_camera != g_value_get_boolean (value))
		{
			self->follow_camera = g_value_get_boolean (value);
			g_object_notify_by_pspec (object, pspec);
		}
		break;
	case PART_PROP_OFFSET_X:
	case PART_PROP_OFFSET_Y:
	case PART_PROP_OFFSET_Z:
		if (self->offset[prop_id - PART_PROP_OFFSET_X] != g_value_get_float (value))
		{
			self->offset[prop_id - PART_PROP_OFFSET_X] = g_value_get_float (value);
			g_object_notify_by_pspec (object, pspec);
		}
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_weather_particles_constructed (GObject *object)
{
	G_OBJECT_CLASS (lrg_weather_particles_parent_class)->constructed (object);
	/* particles only make sense in the world pass */
	lrg_weather_effect_set_space (LRG_WEATHER_EFFECT (object), LRG_WEATHER_SPACE_WORLD);
}

static void
lrg_weather_particles_dispose (GObject *object)
{
	LrgWeatherParticles *self = LRG_WEATHER_PARTICLES (object);

	g_clear_object (&self->emitter);
	g_clear_object (&self->effect);
	G_OBJECT_CLASS (lrg_weather_particles_parent_class)->dispose (object);
}

static void
lrg_weather_particles_class_init (LrgWeatherParticlesClass *klass)
{
	GObjectClass          *object_class = G_OBJECT_CLASS (klass);
	LrgWeatherEffectClass *effect_class = LRG_WEATHER_EFFECT_CLASS (klass);

	object_class->get_property = lrg_weather_particles_get_property;
	object_class->set_property = lrg_weather_particles_set_property;
	object_class->constructed = lrg_weather_particles_constructed;
	object_class->dispose = lrg_weather_particles_dispose;
	effect_class->update = lrg_weather_particles_update;
	effect_class->set_wind = lrg_weather_particles_set_wind;
	effect_class->render_world = lrg_weather_particles_render_world;

	/**
	 * LrgWeatherParticles:capacity:
	 *
	 * Particle slots of the emitter (changing it replaces the emitter).
	 */
	part_props[PART_PROP_CAPACITY] =
		g_param_spec_uint ("capacity", "Capacity", "Particle slots", 1, 4194304,
		                   WEATHER_PARTICLES_DEFAULT_CAPACITY,
		                   G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

	/**
	 * LrgWeatherParticles:gpu:
	 *
	 * Simulate on the GPU when available (#LrgGpuParticles:prefer-gpu).
	 */
	part_props[PART_PROP_GPU] =
		g_param_spec_boolean ("gpu", "GPU", "Simulate on the GPU when available", TRUE,
		                      G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

	/**
	 * LrgWeatherParticles:follow-camera:
	 *
	 * Whether the emitter sits at the camera plus the offset (otherwise
	 * at the offset in world space).
	 */
	part_props[PART_PROP_FOLLOW_CAMERA] =
		g_param_spec_boolean ("follow-camera", "Follow camera", "Emit around the camera", TRUE,
		                      G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

	/**
	 * LrgWeatherParticles:offset-x:
	 *
	 * Emitter X relative to the camera (or absolute without follow-camera).
	 */
	part_props[PART_PROP_OFFSET_X] =
		g_param_spec_float ("offset-x", "Offset X", "Emitter offset X", -G_MAXFLOAT, G_MAXFLOAT, 0.0f,
		                    G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

	/**
	 * LrgWeatherParticles:offset-y:
	 *
	 * Emitter Y relative to the camera (or absolute without follow-camera).
	 */
	part_props[PART_PROP_OFFSET_Y] =
		g_param_spec_float ("offset-y", "Offset Y", "Emitter offset Y", -G_MAXFLOAT, G_MAXFLOAT, 10.0f,
		                    G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

	/**
	 * LrgWeatherParticles:offset-z:
	 *
	 * Emitter Z relative to the camera (or absolute without follow-camera).
	 */
	part_props[PART_PROP_OFFSET_Z] =
		g_param_spec_float ("offset-z", "Offset Z", "Emitter offset Z", -G_MAXFLOAT, G_MAXFLOAT, 0.0f,
		                    G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

	g_object_class_install_properties (object_class, PART_N_PROPS, part_props);
}

static void
lrg_weather_particles_init (LrgWeatherParticles *self)
{
	self->capacity = WEATHER_PARTICLES_DEFAULT_CAPACITY;
	self->gpu = TRUE;
	self->follow_camera = TRUE;
	self->offset[1] = 10.0f;
	self->emitter = lrg_gpu_particles_new (self->capacity);
	weather_particles_defaults (self->emitter);
}

LrgWeatherParticles *
lrg_weather_particles_new (void)
{
	return g_object_new (LRG_TYPE_WEATHER_PARTICLES, "id", "particles", NULL);
}

LrgGpuParticles *
lrg_weather_particles_get_emitter (LrgWeatherParticles *self)
{
	g_return_val_if_fail (LRG_IS_WEATHER_PARTICLES (self), NULL);
	return self->emitter;
}

gboolean
lrg_weather_particles_set_asset (LrgWeatherParticles  *self,
                                 LrgParticleAsset     *asset,
                                 GError              **error)
{
	g_autoptr(LrgGpuParticles) gpu = NULL;
	g_autoptr(GError)          local = NULL;

	g_return_val_if_fail (LRG_IS_WEATHER_PARTICLES (self), FALSE);
	g_return_val_if_fail (asset == NULL || LRG_IS_PARTICLE_ASSET (asset), FALSE);

	g_clear_object (&self->effect);
	if (asset == NULL)
		return TRUE;

	/* the GPU path takes a single emitter inside the supported subset */
	if (lrg_particle_asset_get_n_emitters (asset) != 1)
		g_set_error (&local, LRG_GPU_PARTICLES_ERROR, LRG_GPU_PARTICLES_ERROR_UNSUPPORTED,
		             "%u emitters: the GPU layer runs exactly one", lrg_particle_asset_get_n_emitters (asset));
	else
		gpu = lrg_gpu_particles_new_from_asset (asset, 0, self->capacity, &local);
	if (gpu != NULL)
	{
		lrg_gpu_particles_copy_parameters (self->emitter, gpu);
		g_object_set (self->emitter, "emitting", FALSE, NULL);
		return TRUE;
	}

	/* CPU fallback: the whole effect, at the emitter position */
	lrg_debug (LRG_LOG_DOMAIN_WEATHER, "weather particles on the CPU: %s", local->message);
	self->effect = lrg_particle_effect_new (asset);
	lrg_particle_effect_play (self->effect);
	g_propagate_error (error, g_steal_pointer (&local));
	return FALSE;
}

LrgParticleEffect *
lrg_weather_particles_get_effect (LrgWeatherParticles *self)
{
	g_return_val_if_fail (LRG_IS_WEATHER_PARTICLES (self), NULL);
	return self->effect;
}

/* ==========================================================================
 * Layer kinds and their parameter schema
 * ========================================================================== */

typedef struct
{
	gchar     *name;
	GType      type;
	GPtrArray *specials;   /* GParamSpec, format-only parameters */
} Kind;

static GHashTable *kinds;  /* name -> Kind */
static GMutex      kinds_lock;

static void
kind_free (gpointer data)
{
	Kind *k = data;

	g_free (k->name);
	if (k->specials != NULL)
		g_ptr_array_unref (k->specials);
	g_free (k);
}

/* the effect properties the format manages itself */
static gboolean
is_managed_property (const gchar *name)
{
	static const gchar * const managed[] = {
		"id", "active", "intensity", "target-intensity", "level", "wind-x", "wind-y", NULL
	};

	return g_strv_contains (managed, name);
}

static void
add_float_special (GPtrArray   *specials,
                   const gchar *name,
                   gfloat       min,
                   gfloat       max,
                   gfloat       def,
                   const gchar *blurb)
{
	g_ptr_array_add (specials, g_param_spec_ref_sink (
		g_param_spec_float (name, name, blurb, min, max, def, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
}

static void
add_color_special (GPtrArray   *specials,
                   const gchar *blurb)
{
	g_ptr_array_add (specials, g_param_spec_ref_sink (
		g_param_spec_boxed ("color", "color", blurb, GRL_TYPE_COLOR,
		                    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
}

static void
add_area_specials (GPtrArray *specials)
{
	add_float_special (specials, "area-x", -G_MAXFLOAT, G_MAXFLOAT, 0.0f, "Screen area left");
	add_float_special (specials, "area-y", -G_MAXFLOAT, G_MAXFLOAT, 0.0f, "Screen area top");
	add_float_special (specials, "area-width", 0.0f, G_MAXFLOAT, 800.0f, "Screen area width");
	add_float_special (specials, "area-height", 0.0f, G_MAXFLOAT, 600.0f, "Screen area height");
}

static void
add_volume_specials (GPtrArray *specials)
{
	add_float_special (specials, "volume-radius", 0.0f, G_MAXFLOAT, 25.0f, "World volume half width");
	add_float_special (specials, "volume-height", 0.0f, G_MAXFLOAT, 20.0f, "World volume height");
}

/* register_builtin_kinds: rain, snow, fog, lightning, wind, particles */
static void
register_builtin_kinds (void)
{
	Kind *k;

	/* rain */
	k = g_new0 (Kind, 1);
	k->name = g_strdup ("rain");
	k->type = LRG_TYPE_RAIN;
	k->specials = g_ptr_array_new_with_free_func ((GDestroyNotify) g_param_spec_unref);
	add_color_special (k->specials, "Drop colour");
	g_ptr_array_add (k->specials, g_param_spec_ref_sink (
		g_param_spec_uint ("seed", "seed", "Drop placement seed", 0, G_MAXUINT32, 0,
		                   G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
	add_area_specials (k->specials);
	add_volume_specials (k->specials);
	g_hash_table_insert (kinds, k->name, k);

	/* snow */
	k = g_new0 (Kind, 1);
	k->name = g_strdup ("snow");
	k->type = LRG_TYPE_SNOW;
	k->specials = g_ptr_array_new_with_free_func ((GDestroyNotify) g_param_spec_unref);
	add_color_special (k->specials, "Flake colour");
	add_area_specials (k->specials);
	add_volume_specials (k->specials);
	g_hash_table_insert (kinds, k->name, k);

	/* fog */
	k = g_new0 (Kind, 1);
	k->name = g_strdup ("fog");
	k->type = LRG_TYPE_FOG;
	k->specials = g_ptr_array_new_with_free_func ((GDestroyNotify) g_param_spec_unref);
	add_color_special (k->specials, "Fog colour");
	g_hash_table_insert (kinds, k->name, k);

	/* lightning */
	k = g_new0 (Kind, 1);
	k->name = g_strdup ("lightning");
	k->type = LRG_TYPE_LIGHTNING;
	k->specials = g_ptr_array_new_with_free_func ((GDestroyNotify) g_param_spec_unref);
	add_color_special (k->specials, "Flash colour (alpha ignored)");
	add_float_special (k->specials, "bolt-distance", 0.0f, G_MAXFLOAT, 80.0f, "Bolt distance from the camera");
	add_float_special (k->specials, "bolt-height", 0.0f, G_MAXFLOAT, 60.0f, "Bolt height");
	add_float_special (k->specials, "bolt-width", 0.0f, G_MAXFLOAT, 0.3f, "Bolt ribbon width");
	g_hash_table_insert (kinds, k->name, k);

	/* wind */
	k = g_new0 (Kind, 1);
	k->name = g_strdup ("wind");
	k->type = LRG_TYPE_WEATHER_WIND;
	g_hash_table_insert (kinds, k->name, k);

	/* particles: the layer's own properties plus the emitter description */
	k = g_new0 (Kind, 1);
	k->name = g_strdup ("particles");
	k->type = LRG_TYPE_WEATHER_PARTICLES;
	k->specials = g_ptr_array_new_with_free_func ((GDestroyNotify) g_param_spec_unref);
	{
		g_autoptr(GTypeClass) gclass = g_type_class_ref (LRG_TYPE_GPU_PARTICLES);
		const gchar * const  *names = lrg_gpu_particles_list_parameters ();
		guint                 i;

		for (i = 0; names[i] != NULL; i++)
		{
			GParamSpec *ps = g_object_class_find_property (G_OBJECT_CLASS (gclass), names[i]);

			if (ps != NULL)
				g_ptr_array_add (k->specials, g_param_spec_ref (ps));
		}
	}
	g_ptr_array_add (k->specials, g_param_spec_ref_sink (
		g_param_spec_string ("asset", "asset",
		                     "An .rparticles file (relative to the .rweather) played instead of the inline emitter",
		                     NULL, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
	g_hash_table_insert (kinds, k->name, k);
}

static void
ensure_kinds (void)
{
	g_mutex_lock (&kinds_lock);
	if (kinds == NULL)
	{
		kinds = g_hash_table_new_full (g_str_hash, g_str_equal, NULL, kind_free);
		register_builtin_kinds ();
	}
	g_mutex_unlock (&kinds_lock);
}

static Kind *
lookup_kind (const gchar *name)
{
	Kind *k;

	if (name == NULL)
		return NULL;
	ensure_kinds ();
	g_mutex_lock (&kinds_lock);
	k = g_hash_table_lookup (kinds, name);
	g_mutex_unlock (&kinds_lock);
	return k;
}

void
lrg_weather_asset_register_kind (const gchar *kind,
                                 GType        effect_type)
{
	Kind *k;

	g_return_if_fail (kind != NULL && *kind != '\0');
	g_return_if_fail (g_type_is_a (effect_type, LRG_TYPE_WEATHER_EFFECT));
	g_return_if_fail (!G_TYPE_IS_ABSTRACT (effect_type));

	ensure_kinds ();
	k = g_new0 (Kind, 1);
	k->name = g_strdup (kind);
	k->type = effect_type;
	g_mutex_lock (&kinds_lock);
	g_hash_table_replace (kinds, k->name, k);
	g_mutex_unlock (&kinds_lock);
}

GType
lrg_weather_asset_lookup_kind (const gchar *kind)
{
	Kind *k = lookup_kind (kind);

	return k != NULL ? k->type : G_TYPE_INVALID;
}

gchar **
lrg_weather_asset_list_kinds (void)
{
	GPtrArray     *out = g_ptr_array_new ();
	GHashTableIter it;
	gpointer       key;

	ensure_kinds ();
	g_mutex_lock (&kinds_lock);
	g_hash_table_iter_init (&it, kinds);
	while (g_hash_table_iter_next (&it, &key, NULL))
		g_ptr_array_add (out, g_strdup (key));
	g_mutex_unlock (&kinds_lock);
	g_ptr_array_sort_values (out, (GCompareFunc) g_strcmp0);
	g_ptr_array_add (out, NULL);
	return (gchar **) g_ptr_array_free (out, FALSE);
}

/* kind_class_param: one of the effect type's own (non-managed, writable)
 * properties, or NULL */
static GParamSpec *
kind_class_param (Kind        *k,
                  const gchar *name)
{
	g_autoptr(GTypeClass) gclass = g_type_class_ref (k->type);
	GParamSpec           *ps = g_object_class_find_property (G_OBJECT_CLASS (gclass), name);

	if (ps == NULL || is_managed_property (name) || !(ps->flags & G_PARAM_WRITABLE) ||
	    (ps->flags & G_PARAM_CONSTRUCT_ONLY))
		return NULL;
	return ps;
}

/* kind_param: the spec of @name for @k; *special tells which table */
static GParamSpec *
kind_param (Kind        *k,
            const gchar *name,
            gboolean    *special)
{
	GParamSpec *ps = kind_class_param (k, name);
	guint       i;

	if (special != NULL)
		*special = FALSE;
	if (ps != NULL)
		return ps;
	for (i = 0; k->specials != NULL && i < k->specials->len; i++)
	{
		ps = g_ptr_array_index (k->specials, i);
		if (g_strcmp0 (ps->name, name) == 0)
		{
			if (special != NULL)
				*special = TRUE;
			return ps;
		}
	}
	return NULL;
}

GParamSpec **
lrg_weather_asset_list_kind_params (const gchar *kind,
                                    guint       *n_params)
{
	Kind                 *k = lookup_kind (kind);
	g_autoptr(GTypeClass) gclass = NULL;
	GParamSpec          **all;
	GPtrArray            *out;
	guint                 n = 0, i;

	if (n_params != NULL)
		*n_params = 0;
	if (k == NULL)
		return NULL;

	out = g_ptr_array_new ();
	gclass = g_type_class_ref (k->type);
	all = g_object_class_list_properties (G_OBJECT_CLASS (gclass), &n);
	for (i = 0; i < n; i++)
		if (kind_class_param (k, all[i]->name) != NULL)
			g_ptr_array_add (out, all[i]);
	g_free (all);
	for (i = 0; k->specials != NULL && i < k->specials->len; i++)
		g_ptr_array_add (out, g_ptr_array_index (k->specials, i));
	if (n_params != NULL)
		*n_params = out->len;
	return (GParamSpec **) g_ptr_array_free (out, FALSE);
}

/* ==========================================================================
 * Value text <-> GValue
 * ========================================================================== */

static gboolean
parse_hex_color (const gchar *text,
                 GrlColor    *out)
{
	gsize  len;
	guint  i;
	guint8 v[4] = { 0, 0, 0, 255 };

	if (text == NULL || text[0] != '#')
		return FALSE;
	len = strlen (text + 1);
	if (len != 6 && len != 8)
		return FALSE;
	for (i = 0; i < len / 2; i++)
	{
		gint hi = g_ascii_xdigit_value (text[1 + i * 2]);
		gint lo = g_ascii_xdigit_value (text[2 + i * 2]);

		if (hi < 0 || lo < 0)
			return FALSE;
		v[i] = (guint8) (hi * 16 + lo);
	}
	out->r = v[0];
	out->g = v[1];
	out->b = v[2];
	out->a = v[3];
	return TRUE;
}

/* parse_text: the file syntax of @text into @out (initialised to the
 * spec's type), clamped to its range; @what prefixes errors */
static gboolean
parse_text (GParamSpec   *pspec,
            const gchar  *text,
            const gchar  *what,
            GValue       *out,
            GError      **error)
{
	GType   type = G_PARAM_SPEC_VALUE_TYPE (pspec);
	GType   fund = G_TYPE_FUNDAMENTAL (type);
	gdouble d;
	gchar  *end = NULL;

	g_value_init (out, type);
	if (text == NULL)
		goto bad;

	if (type == GRL_TYPE_COLOR)
	{
		GrlColor c;

		if (!parse_hex_color (text, &c))
			goto bad;
		g_value_set_boxed (out, &c);
		return TRUE;
	}
	if (type == LRG_TYPE_GRADIENT_STOPS)
	{
		g_autoptr(LrgGradientStops) stops = lrg_gradient_stops_new_from_string (text);

		if (stops == NULL)
			goto bad;
		g_value_set_boxed (out, stops);
		return TRUE;
	}

	switch (fund)
	{
	case G_TYPE_STRING:
		g_value_set_string (out, text);
		return TRUE;
	case G_TYPE_BOOLEAN:
		if (g_ascii_strcasecmp (text, "true") == 0 || g_ascii_strcasecmp (text, "yes") == 0 ||
		    g_strcmp0 (text, "1") == 0)
			g_value_set_boolean (out, TRUE);
		else if (g_ascii_strcasecmp (text, "false") == 0 || g_ascii_strcasecmp (text, "no") == 0 ||
		         g_strcmp0 (text, "0") == 0)
			g_value_set_boolean (out, FALSE);
		else
			goto bad;
		return TRUE;
	case G_TYPE_ENUM:
	{
		g_autoptr(GEnumClass) eclass = g_type_class_ref (type);
		GEnumValue           *ev = g_enum_get_value_by_nick (eclass, text);

		if (ev == NULL)
			ev = g_enum_get_value_by_name (eclass, text);
		if (ev == NULL)
			goto bad;
		g_value_set_enum (out, ev->value);
		return TRUE;
	}
	case G_TYPE_INT:
	case G_TYPE_UINT:
	case G_TYPE_FLOAT:
	case G_TYPE_DOUBLE:
		d = g_ascii_strtod (text, &end);
		if (end == text || *end != '\0' || !isfinite (d))
			goto bad;
		if (fund == G_TYPE_FLOAT)
			g_value_set_float (out, (gfloat) d);
		else if (fund == G_TYPE_DOUBLE)
			g_value_set_double (out, d);
		else if (fund == G_TYPE_INT)
			g_value_set_int (out, (gint) CLAMP (round (d), (gdouble) G_MININT, (gdouble) G_MAXINT));
		else
			g_value_set_uint (out, (guint) CLAMP (round (d), 0.0, (gdouble) G_MAXUINT));
		g_param_value_validate (pspec, out);
		return TRUE;
	default:
		break;
	}
	g_set_error (error, LRG_WEATHER_ASSET_ERROR, LRG_WEATHER_ASSET_ERROR_INVALID_VALUE,
	             "%s: values of type %s cannot be written in a .rweather", what, g_type_name (type));
	g_value_unset (out);
	return FALSE;

bad:
	g_set_error (error, LRG_WEATHER_ASSET_ERROR, LRG_WEATHER_ASSET_ERROR_INVALID_VALUE,
	             "%s: '%s' is not a valid %s", what, text != NULL ? text : "(null)",
	             type == GRL_TYPE_COLOR ? "\"#rrggbb(aa)\" colour" : g_type_name (type));
	g_value_unset (out);
	return FALSE;
}

static gchar *
format_float (gdouble v)
{
	gchar buf[G_ASCII_DTOSTR_BUF_SIZE];

	/* 6 significant digits: what a gfloat holds; whole numbers print as
	 * integers */
	return g_strdup (g_ascii_formatd (buf, sizeof buf, "%.6g", v));
}

/* value_text: @v (of the spec's type) in the file syntax */
static gchar *
value_text (const GValue *v)
{
	GType type = G_VALUE_TYPE (v);

	if (type == GRL_TYPE_COLOR)
	{
		const GrlColor *c = g_value_get_boxed (v);

		if (c == NULL)
			return g_strdup ("#ffffffff");
		return g_strdup_printf ("#%02x%02x%02x%02x", c->r, c->g, c->b, c->a);
	}
	if (type == LRG_TYPE_GRADIENT_STOPS)
	{
		const LrgGradientStops *s = g_value_get_boxed (v);

		return s != NULL ? lrg_gradient_stops_to_string (s) : g_strdup ("");
	}
	switch (G_TYPE_FUNDAMENTAL (type))
	{
	case G_TYPE_BOOLEAN:
		return g_strdup (g_value_get_boolean (v) ? "true" : "false");
	case G_TYPE_INT:
		return g_strdup_printf ("%d", g_value_get_int (v));
	case G_TYPE_UINT:
		return g_strdup_printf ("%u", g_value_get_uint (v));
	case G_TYPE_FLOAT:
		return format_float ((gdouble) g_value_get_float (v));
	case G_TYPE_DOUBLE:
		return format_float (g_value_get_double (v));
	case G_TYPE_ENUM:
	{
		g_autoptr(GEnumClass) eclass = g_type_class_ref (type);
		GEnumValue           *ev = g_enum_get_value (eclass, g_value_get_enum (v));

		return ev != NULL ? g_strdup (ev->value_nick) : g_strdup_printf ("%d", g_value_get_enum (v));
	}
	case G_TYPE_STRING:
	default:
		return g_strdup (G_VALUE_HOLDS_STRING (v) && g_value_get_string (v) != NULL
		                 ? g_value_get_string (v) : "");
	}
}

/* is_string_typed: values written as YAML strings (quoted when needed) */
static gboolean
is_string_typed (GParamSpec *pspec)
{
	GType type = G_PARAM_SPEC_VALUE_TYPE (pspec);

	return G_TYPE_FUNDAMENTAL (type) == G_TYPE_STRING || G_TYPE_FUNDAMENTAL (type) == G_TYPE_BOXED;
}

/* ==========================================================================
 * The data model
 * ========================================================================== */

typedef struct
{
	gchar *name;
	gchar *text;
} Param;

typedef struct
{
	gchar       *kind;
	gchar       *id;          /* NULL: the kind */
	gfloat       intensity;
	gboolean     enabled;
	GPtrArray   *params;      /* Param */
	YamlMapping *extra;
} Layer;

typedef struct
{
	gchar       *id;
	gchar       *name;
	gboolean     has_ambient;
	GrlColor     ambient;
	gfloat       brightness;
	GPtrArray   *layers;      /* Layer */
	YamlMapping *extra;
} Preset;

typedef struct
{
	gchar       *from;
	gchar       *to;
	gfloat       duration;
	YamlMapping *extra;
} Transition;

struct _LrgWeatherAsset
{
	GObject parent_instance;

	gchar       *name;
	gchar       *default_preset;
	gfloat       default_transition;
	gchar       *base_dir;
	GPtrArray   *presets;      /* Preset */
	GPtrArray   *transitions;  /* Transition */
	YamlMapping *extra;
};

G_DEFINE_FINAL_TYPE (LrgWeatherAsset, lrg_weather_asset, G_TYPE_OBJECT)

enum
{
	SIGNAL_CHANGED,
	N_SIGNALS
};

static guint asset_signals[N_SIGNALS];

static void
param_free (gpointer data)
{
	Param *p = data;

	g_free (p->name);
	g_free (p->text);
	g_free (p);
}

static Param *
param_copy (const Param *src)
{
	Param *p = g_new0 (Param, 1);

	p->name = g_strdup (src->name);
	p->text = g_strdup (src->text);
	return p;
}

static void
layer_free (gpointer data)
{
	Layer *l = data;

	g_free (l->kind);
	g_free (l->id);
	g_ptr_array_unref (l->params);
	if (l->extra != NULL)
		yaml_mapping_unref (l->extra);
	g_free (l);
}

static Layer *
layer_new (const gchar *kind)
{
	Layer *l = g_new0 (Layer, 1);

	l->kind = g_strdup (kind);
	l->intensity = 1.0f;
	l->enabled = TRUE;
	l->params = g_ptr_array_new_with_free_func (param_free);
	return l;
}

static Layer *
layer_copy (const Layer *src)
{
	Layer *l = layer_new (src->kind);
	guint  i;

	l->id = g_strdup (src->id);
	l->intensity = src->intensity;
	l->enabled = src->enabled;
	for (i = 0; i < src->params->len; i++)
		g_ptr_array_add (l->params, param_copy (g_ptr_array_index (src->params, i)));
	if (src->extra != NULL)
		l->extra = yaml_mapping_ref (src->extra);
	return l;
}

/* the effect id a layer builds */
static const gchar *
layer_effective_id (const Layer *l)
{
	return l->id != NULL ? l->id : l->kind;
}

static Param *
layer_find_param (const Layer *l,
                  const gchar *name)
{
	guint i;

	for (i = 0; i < l->params->len; i++)
	{
		Param *p = g_ptr_array_index (l->params, i);

		if (g_strcmp0 (p->name, name) == 0)
			return p;
	}
	return NULL;
}

static void
preset_free (gpointer data)
{
	Preset *p = data;

	g_free (p->id);
	g_free (p->name);
	g_ptr_array_unref (p->layers);
	if (p->extra != NULL)
		yaml_mapping_unref (p->extra);
	g_free (p);
}

static Preset *
preset_new (const gchar *id)
{
	Preset *p = g_new0 (Preset, 1);

	p->id = g_strdup (id);
	p->ambient = (GrlColor) { 255, 255, 255, 255 };
	p->brightness = 1.0f;
	p->layers = g_ptr_array_new_with_free_func (layer_free);
	return p;
}

static Preset *
preset_copy (const Preset *src)
{
	Preset *p = preset_new (src->id);
	guint   i;

	p->name = g_strdup (src->name);
	p->has_ambient = src->has_ambient;
	p->ambient = src->ambient;
	p->brightness = src->brightness;
	for (i = 0; i < src->layers->len; i++)
		g_ptr_array_add (p->layers, layer_copy (g_ptr_array_index (src->layers, i)));
	if (src->extra != NULL)
		p->extra = yaml_mapping_ref (src->extra);
	return p;
}

static void
transition_free (gpointer data)
{
	Transition *t = data;

	g_free (t->from);
	g_free (t->to);
	if (t->extra != NULL)
		yaml_mapping_unref (t->extra);
	g_free (t);
}

static Transition *
transition_copy (const Transition *src)
{
	Transition *t = g_new0 (Transition, 1);

	t->from = g_strdup (src->from);
	t->to = g_strdup (src->to);
	t->duration = src->duration;
	if (src->extra != NULL)
		t->extra = yaml_mapping_ref (src->extra);
	return t;
}

static Preset *
get_preset (LrgWeatherAsset *self,
            guint            preset)
{
	if (preset >= self->presets->len)
		return NULL;
	return g_ptr_array_index (self->presets, preset);
}

static Layer *
get_layer (LrgWeatherAsset *self,
           guint            preset,
           guint            layer)
{
	Preset *p = get_preset (self, preset);

	if (p == NULL || layer >= p->layers->len)
		return NULL;
	return g_ptr_array_index (p->layers, layer);
}

static void
emit_changed (LrgWeatherAsset *self,
              gint             preset,
              gint             layer)
{
	g_signal_emit (self, asset_signals[SIGNAL_CHANGED], 0, preset, layer);
}

/* ==========================================================================
 * YAML in
 * ========================================================================== */

static const gchar *
scalar_of (YamlNode *node)
{
	if (node == NULL || yaml_node_get_node_type (node) != YAML_NODE_SCALAR)
		return NULL;
	return yaml_node_get_scalar (node);
}

static gboolean
is_empty_node (YamlNode *node)
{
	const gchar *s;

	if (node == NULL || yaml_node_is_null (node))
		return TRUE;
	s = scalar_of (node);
	return s != NULL && (s[0] == '\0' || g_strcmp0 (s, "~") == 0 || g_strcmp0 (s, "null") == 0);
}

static void
keep_extra (YamlMapping **extra,
            const gchar  *key,
            YamlNode     *member)
{
	if (*extra == NULL)
		*extra = yaml_mapping_new ();
	yaml_mapping_set_member (*extra, key, member);
}

/* read_float / read_string / read_bool: typed scalars with key paths */
static gboolean
read_float (YamlNode     *node,
            const gchar  *what,
            gfloat        min,
            gfloat        max,
            gfloat       *out,
            GError      **error)
{
	const gchar *s = scalar_of (node);
	gchar       *end = NULL;
	gdouble      d;

	if (s == NULL)
		goto bad;
	d = g_ascii_strtod (s, &end);
	if (end == s || *end != '\0' || !isfinite (d))
		goto bad;
	*out = CLAMP ((gfloat) d, min, max);
	return TRUE;
bad:
	g_set_error (error, LRG_WEATHER_ASSET_ERROR, LRG_WEATHER_ASSET_ERROR_INVALID_VALUE,
	             "%s: expected a number", what);
	return FALSE;
}

static gboolean
read_string (YamlNode     *node,
             const gchar  *what,
             gchar       **out,
             GError      **error)
{
	const gchar *s = scalar_of (node);

	if (s == NULL || *s == '\0')
	{
		g_set_error (error, LRG_WEATHER_ASSET_ERROR, LRG_WEATHER_ASSET_ERROR_INVALID_VALUE,
		             "%s: expected a non-empty string", what);
		return FALSE;
	}
	g_free (*out);
	*out = g_strdup (s);
	return TRUE;
}

static gboolean
read_bool (YamlNode     *node,
           const gchar  *what,
           gboolean     *out,
           GError      **error)
{
	const gchar *s = scalar_of (node);

	if (s != NULL && (g_ascii_strcasecmp (s, "true") == 0 || g_ascii_strcasecmp (s, "yes") == 0))
		*out = TRUE;
	else if (s != NULL && (g_ascii_strcasecmp (s, "false") == 0 || g_ascii_strcasecmp (s, "no") == 0))
		*out = FALSE;
	else
	{
		g_set_error (error, LRG_WEATHER_ASSET_ERROR, LRG_WEATHER_ASSET_ERROR_INVALID_VALUE,
		             "%s: expected true or false", what);
		return FALSE;
	}
	return TRUE;
}

static gboolean
read_params (Layer        *layer,
             Kind         *kind,
             YamlNode     *node,
             const gchar  *path,
             GError      **error)
{
	YamlMapping      *map;
	g_autoptr(GList)  keys = NULL;
	GList            *l;

	if (is_empty_node (node))
		return TRUE;
	if (yaml_node_get_node_type (node) != YAML_NODE_MAPPING)
	{
		g_set_error (error, LRG_WEATHER_ASSET_ERROR, LRG_WEATHER_ASSET_ERROR_PARSE,
		             "%s.params: expected a mapping", path);
		return FALSE;
	}
	map = yaml_node_get_mapping (node);
	keys = yaml_mapping_get_members (map);
	for (l = keys; l != NULL; l = l->next)
	{
		const gchar      *key = l->data;
		YamlNode         *member = yaml_mapping_get_member (map, key);
		g_autofree gchar *what = g_strdup_printf ("%s.params.%s", path, key);
		g_auto(GValue)    v = G_VALUE_INIT;
		GParamSpec       *ps = kind_param (kind, key, NULL);
		Param            *p;

		if (ps == NULL)
		{
			g_set_error (error, LRG_WEATHER_ASSET_ERROR, LRG_WEATHER_ASSET_ERROR_UNKNOWN_PARAM,
			             "%s: '%s' layers have no parameter '%s'", what, kind->name, key);
			return FALSE;
		}
		if (scalar_of (member) == NULL)
		{
			g_set_error (error, LRG_WEATHER_ASSET_ERROR, LRG_WEATHER_ASSET_ERROR_INVALID_VALUE,
			             "%s: expected a single value", what);
			return FALSE;
		}
		if (!parse_text (ps, scalar_of (member), what, &v, error))
			return FALSE;
		p = g_new0 (Param, 1);
		p->name = g_strdup (key);
		p->text = g_strdup (scalar_of (member));
		g_ptr_array_add (layer->params, p);
	}
	return TRUE;
}

static Layer *
read_layer (YamlNode     *node,
            const gchar  *path,
            GError      **error)
{
	YamlMapping      *map;
	g_autoptr(GList)  keys = NULL;
	GList            *l;
	const gchar      *kind_name;
	Kind             *kind;
	Layer            *layer;

	if (node == NULL || yaml_node_get_node_type (node) != YAML_NODE_MAPPING)
	{
		g_set_error (error, LRG_WEATHER_ASSET_ERROR, LRG_WEATHER_ASSET_ERROR_PARSE,
		             "%s: expected a mapping", path);
		return NULL;
	}
	map = yaml_node_get_mapping (node);
	kind_name = scalar_of (yaml_mapping_get_member (map, "kind"));
	if (kind_name == NULL || *kind_name == '\0')
	{
		g_set_error (error, LRG_WEATHER_ASSET_ERROR, LRG_WEATHER_ASSET_ERROR_PARSE,
		             "%s: missing 'kind'", path);
		return NULL;
	}
	kind = lookup_kind (kind_name);
	if (kind == NULL)
	{
		g_set_error (error, LRG_WEATHER_ASSET_ERROR, LRG_WEATHER_ASSET_ERROR_UNKNOWN_KIND,
		             "%s.kind: unknown layer kind '%s'", path, kind_name);
		return NULL;
	}

	layer = layer_new (kind_name);
	keys = yaml_mapping_get_members (map);
	for (l = keys; l != NULL; l = l->next)
	{
		const gchar      *key = l->data;
		YamlNode         *member = yaml_mapping_get_member (map, key);
		g_autofree gchar *what = g_strdup_printf ("%s.%s", path, key);

		if (g_strcmp0 (key, "kind") == 0)
			continue;
		if (g_strcmp0 (key, "id") == 0)
		{
			if (!read_string (member, what, &layer->id, error))
				goto fail;
		}
		else if (g_strcmp0 (key, "intensity") == 0)
		{
			if (!read_float (member, what, 0.0f, 1.0f, &layer->intensity, error))
				goto fail;
		}
		else if (g_strcmp0 (key, "enabled") == 0)
		{
			if (!read_bool (member, what, &layer->enabled, error))
				goto fail;
		}
		else if (g_strcmp0 (key, "params") == 0)
		{
			if (!read_params (layer, kind, member, path, error))
				goto fail;
		}
		else
			keep_extra (&layer->extra, key, member);
	}
	return layer;

fail:
	layer_free (layer);
	return NULL;
}

static Preset *
read_preset (YamlNode     *node,
             const gchar  *path,
             GError      **error)
{
	YamlMapping      *map;
	g_autoptr(GList)  keys = NULL;
	GList            *l;
	const gchar      *id;
	Preset           *preset;

	if (node == NULL || yaml_node_get_node_type (node) != YAML_NODE_MAPPING)
	{
		g_set_error (error, LRG_WEATHER_ASSET_ERROR, LRG_WEATHER_ASSET_ERROR_PARSE,
		             "%s: expected a mapping", path);
		return NULL;
	}
	map = yaml_node_get_mapping (node);
	id = scalar_of (yaml_mapping_get_member (map, "id"));
	if (id == NULL || *id == '\0')
	{
		g_set_error (error, LRG_WEATHER_ASSET_ERROR, LRG_WEATHER_ASSET_ERROR_PARSE,
		             "%s: missing 'id'", path);
		return NULL;
	}

	preset = preset_new (id);
	keys = yaml_mapping_get_members (map);
	for (l = keys; l != NULL; l = l->next)
	{
		const gchar      *key = l->data;
		YamlNode         *member = yaml_mapping_get_member (map, key);
		g_autofree gchar *what = g_strdup_printf ("%s.%s", path, key);

		if (g_strcmp0 (key, "id") == 0)
			continue;
		if (g_strcmp0 (key, "name") == 0)
		{
			if (!read_string (member, what, &preset->name, error))
				goto fail;
		}
		else if (g_strcmp0 (key, "ambient-color") == 0)
		{
			if (!parse_hex_color (scalar_of (member), &preset->ambient))
			{
				g_set_error (error, LRG_WEATHER_ASSET_ERROR, LRG_WEATHER_ASSET_ERROR_INVALID_VALUE,
				             "%s: expected a \"#rrggbb\" colour", what);
				goto fail;
			}
			preset->ambient.a = 255;
			preset->has_ambient = TRUE;
		}
		else if (g_strcmp0 (key, "ambient-brightness") == 0)
		{
			if (!read_float (member, what, 0.0f, 2.0f, &preset->brightness, error))
				goto fail;
			preset->has_ambient = TRUE;
		}
		else if (g_strcmp0 (key, "layers") == 0)
		{
			YamlSequence *seq;
			guint         i, n;

			if (is_empty_node (member))
				continue;
			if (yaml_node_get_node_type (member) != YAML_NODE_SEQUENCE)
			{
				g_set_error (error, LRG_WEATHER_ASSET_ERROR, LRG_WEATHER_ASSET_ERROR_PARSE,
				             "%s: expected a list", what);
				goto fail;
			}
			seq = yaml_node_get_sequence (member);
			n = yaml_sequence_get_length (seq);
			for (i = 0; i < n; i++)
			{
				g_autofree gchar *lpath = g_strdup_printf ("%s.layers[%u]", path, i);
				Layer            *layer = read_layer (yaml_sequence_get_element (seq, i), lpath, error);
				guint             j;

				if (layer == NULL)
					goto fail;
				for (j = 0; j < preset->layers->len; j++)
					if (g_strcmp0 (layer_effective_id (g_ptr_array_index (preset->layers, j)),
					               layer_effective_id (layer)) == 0)
					{
						g_set_error (error, LRG_WEATHER_ASSET_ERROR, LRG_WEATHER_ASSET_ERROR_DUPLICATE,
						             "%s: a second layer with id '%s' (give it an 'id')", lpath,
						             layer_effective_id (layer));
						layer_free (layer);
						goto fail;
					}
				g_ptr_array_add (preset->layers, layer);
			}
		}
		else
			keep_extra (&preset->extra, key, member);
	}
	return preset;

fail:
	preset_free (preset);
	return NULL;
}

static Transition *
read_transition (YamlNode     *node,
                 const gchar  *path,
                 GError      **error)
{
	YamlMapping      *map;
	g_autoptr(GList)  keys = NULL;
	GList            *l;
	Transition       *t;
	gboolean          has_duration = FALSE;

	if (node == NULL || yaml_node_get_node_type (node) != YAML_NODE_MAPPING)
	{
		g_set_error (error, LRG_WEATHER_ASSET_ERROR, LRG_WEATHER_ASSET_ERROR_PARSE,
		             "%s: expected a mapping", path);
		return NULL;
	}
	map = yaml_node_get_mapping (node);
	t = g_new0 (Transition, 1);
	keys = yaml_mapping_get_members (map);
	for (l = keys; l != NULL; l = l->next)
	{
		const gchar      *key = l->data;
		YamlNode         *member = yaml_mapping_get_member (map, key);
		g_autofree gchar *what = g_strdup_printf ("%s.%s", path, key);

		if (g_strcmp0 (key, "from") == 0)
		{
			if (!read_string (member, what, &t->from, error))
				goto fail;
		}
		else if (g_strcmp0 (key, "to") == 0)
		{
			if (!read_string (member, what, &t->to, error))
				goto fail;
		}
		else if (g_strcmp0 (key, "duration") == 0)
		{
			if (!read_float (member, what, 0.0f, 100000.0f, &t->duration, error))
				goto fail;
			has_duration = TRUE;
		}
		else
			keep_extra (&t->extra, key, member);
	}
	if (t->from == NULL || t->to == NULL || !has_duration)
	{
		g_set_error (error, LRG_WEATHER_ASSET_ERROR, LRG_WEATHER_ASSET_ERROR_PARSE,
		             "%s: a transition needs 'from', 'to' and 'duration'", path);
		goto fail;
	}
	return t;

fail:
	transition_free (t);
	return NULL;
}

/* known_endpoint: a preset id, "*" or "none" */
static gboolean
known_endpoint (LrgWeatherAsset *self,
                const gchar     *id)
{
	return g_strcmp0 (id, "*") == 0 || g_strcmp0 (id, "none") == 0 ||
	       lrg_weather_asset_find_preset (self, id) >= 0;
}

static gboolean
read_asset (LrgWeatherAsset  *self,
            YamlNode         *root,
            GError          **error)
{
	YamlMapping      *map;
	g_autoptr(GList)  keys = NULL;
	GList            *l;
	guint             i;

	if (root == NULL || yaml_node_get_node_type (root) != YAML_NODE_MAPPING)
	{
		g_set_error_literal (error, LRG_WEATHER_ASSET_ERROR, LRG_WEATHER_ASSET_ERROR_PARSE,
		                     "a .rweather must be a YAML mapping");
		return FALSE;
	}
	map = yaml_node_get_mapping (root);
	keys = yaml_mapping_get_members (map);
	for (l = keys; l != NULL; l = l->next)
	{
		const gchar *key = l->data;
		YamlNode    *member = yaml_mapping_get_member (map, key);

		if (g_strcmp0 (key, "version") == 0)
		{
			gfloat v = 0.0f;

			if (!read_float (member, "version", 0.0f, 1000000.0f, &v, error))
				return FALSE;
			if ((gint) v > LRG_WEATHER_ASSET_FORMAT_VERSION)
			{
				g_set_error (error, LRG_WEATHER_ASSET_ERROR, LRG_WEATHER_ASSET_ERROR_PARSE,
				             "version: %d is newer than this build reads (%d)", (gint) v,
				             LRG_WEATHER_ASSET_FORMAT_VERSION);
				return FALSE;
			}
		}
		else if (g_strcmp0 (key, "name") == 0)
		{
			if (!read_string (member, "name", &self->name, error))
				return FALSE;
		}
		else if (g_strcmp0 (key, "default-preset") == 0)
		{
			if (!read_string (member, "default-preset", &self->default_preset, error))
				return FALSE;
		}
		else if (g_strcmp0 (key, "default-transition") == 0)
		{
			if (!read_float (member, "default-transition", 0.0f, 100000.0f, &self->default_transition, error))
				return FALSE;
		}
		else if (g_strcmp0 (key, "presets") == 0)
		{
			YamlSequence *seq;
			guint         n;

			if (is_empty_node (member))
				continue;
			if (yaml_node_get_node_type (member) != YAML_NODE_SEQUENCE)
			{
				g_set_error_literal (error, LRG_WEATHER_ASSET_ERROR, LRG_WEATHER_ASSET_ERROR_PARSE,
				                     "presets: expected a list");
				return FALSE;
			}
			seq = yaml_node_get_sequence (member);
			n = yaml_sequence_get_length (seq);
			for (i = 0; i < n; i++)
			{
				g_autofree gchar *path = g_strdup_printf ("presets[%u]", i);
				Preset           *p = read_preset (yaml_sequence_get_element (seq, i), path, error);

				if (p == NULL)
					return FALSE;
				if (lrg_weather_asset_find_preset (self, p->id) >= 0)
				{
					g_set_error (error, LRG_WEATHER_ASSET_ERROR, LRG_WEATHER_ASSET_ERROR_DUPLICATE,
					             "%s.id: a second preset '%s'", path, p->id);
					preset_free (p);
					return FALSE;
				}
				g_ptr_array_add (self->presets, p);
			}
		}
		else if (g_strcmp0 (key, "transitions") == 0)
		{
			YamlSequence *seq;
			guint         n;

			if (is_empty_node (member))
				continue;
			if (yaml_node_get_node_type (member) != YAML_NODE_SEQUENCE)
			{
				g_set_error_literal (error, LRG_WEATHER_ASSET_ERROR, LRG_WEATHER_ASSET_ERROR_PARSE,
				                     "transitions: expected a list");
				return FALSE;
			}
			seq = yaml_node_get_sequence (member);
			n = yaml_sequence_get_length (seq);
			for (i = 0; i < n; i++)
			{
				g_autofree gchar *path = g_strdup_printf ("transitions[%u]", i);
				Transition       *t = read_transition (yaml_sequence_get_element (seq, i), path, error);

				if (t == NULL)
					return FALSE;
				g_ptr_array_add (self->transitions, t);
			}
		}
		else
			keep_extra (&self->extra, key, member);
	}

	/* references resolve once every preset is known */
	if (self->default_preset != NULL && lrg_weather_asset_find_preset (self, self->default_preset) < 0)
	{
		g_set_error (error, LRG_WEATHER_ASSET_ERROR, LRG_WEATHER_ASSET_ERROR_NOT_FOUND,
		             "default-preset: no preset '%s'", self->default_preset);
		return FALSE;
	}
	for (i = 0; i < self->transitions->len; i++)
	{
		Transition *t = g_ptr_array_index (self->transitions, i);

		if (!known_endpoint (self, t->from) || !known_endpoint (self, t->to))
		{
			g_set_error (error, LRG_WEATHER_ASSET_ERROR, LRG_WEATHER_ASSET_ERROR_NOT_FOUND,
			             "transitions[%u]: '%s' is not a preset, '*' or 'none'", i,
			             known_endpoint (self, t->from) ? t->to : t->from);
			return FALSE;
		}
	}
	return TRUE;
}

/* ==========================================================================
 * YAML out
 * ========================================================================== */

static void
copy_extra (YamlMapping *map,
            YamlMapping *extra)
{
	g_autoptr(GList) keys = NULL;
	GList           *l;

	if (extra == NULL)
		return;
	keys = yaml_mapping_get_members (extra);
	for (l = keys; l != NULL; l = l->next)
		yaml_mapping_set_member (map, l->data, yaml_mapping_get_member (extra, l->data));
}

static void
set_plain (YamlMapping *map,
           const gchar *key,
           const gchar *text)
{
	g_autoptr(YamlNode) node = yaml_node_new_scalar (text, YAML_SCALAR_STYLE_PLAIN);

	yaml_mapping_set_member (map, key, node);
}

static void
set_float_member (YamlMapping *map,
                  const gchar *key,
                  gfloat       v)
{
	g_autofree gchar *text = format_float ((gdouble) v);

	set_plain (map, key, text);
}

static YamlNode *
layer_to_node (const Layer *layer)
{
	YamlNode    *node = yaml_node_new_mapping (NULL);
	YamlMapping *map = yaml_node_get_mapping (node);
	Kind        *kind = lookup_kind (layer->kind);
	guint        i;

	yaml_mapping_set_string_member (map, "kind", layer->kind);
	if (layer->id != NULL)
		yaml_mapping_set_string_member (map, "id", layer->id);
	set_float_member (map, "intensity", layer->intensity);
	if (!layer->enabled)
		yaml_mapping_set_boolean_member (map, "enabled", FALSE);
	if (layer->params->len > 0)
	{
		g_autoptr(YamlNode) pnode = yaml_node_new_mapping (NULL);
		YamlMapping        *pmap = yaml_node_get_mapping (pnode);

		for (i = 0; i < layer->params->len; i++)
		{
			Param      *p = g_ptr_array_index (layer->params, i);
			GParamSpec *ps = kind != NULL ? kind_param (kind, p->name, NULL) : NULL;

			if (ps != NULL && is_string_typed (ps))
				yaml_mapping_set_string_member (pmap, p->name, p->text);
			else
				set_plain (pmap, p->name, p->text);
		}
		yaml_mapping_set_member (map, "params", pnode);
	}
	copy_extra (map, layer->extra);
	return node;
}

static YamlNode *
preset_to_node (const Preset *preset)
{
	YamlNode           *node = yaml_node_new_mapping (NULL);
	YamlMapping        *map = yaml_node_get_mapping (node);
	g_autoptr(YamlNode) layers = yaml_node_new_sequence (NULL);
	YamlSequence       *seq = yaml_node_get_sequence (layers);
	guint               i;

	yaml_mapping_set_string_member (map, "id", preset->id);
	if (preset->name != NULL)
		yaml_mapping_set_string_member (map, "name", preset->name);
	if (preset->has_ambient)
	{
		g_autofree gchar *hex = g_strdup_printf ("#%02x%02x%02x", preset->ambient.r, preset->ambient.g,
		                                         preset->ambient.b);

		yaml_mapping_set_string_member (map, "ambient-color", hex);
		set_float_member (map, "ambient-brightness", preset->brightness);
	}
	copy_extra (map, preset->extra);
	for (i = 0; i < preset->layers->len; i++)
	{
		g_autoptr(YamlNode) lnode = layer_to_node (g_ptr_array_index (preset->layers, i));

		yaml_sequence_add_element (seq, lnode);
	}
	yaml_mapping_set_member (map, "layers", layers);
	return node;
}

gchar *
lrg_weather_asset_to_data (LrgWeatherAsset  *self,
                           GError          **error)
{
	g_autoptr(YamlNode)      root = NULL;
	g_autoptr(YamlNode)      presets = NULL;
	g_autoptr(YamlDocument)  doc = NULL;
	g_autoptr(YamlGenerator) gen = NULL;
	YamlMapping             *map;
	YamlSequence            *seq;
	guint                    i;

	g_return_val_if_fail (LRG_IS_WEATHER_ASSET (self), NULL);

	root = yaml_node_new_mapping (NULL);
	map = yaml_node_get_mapping (root);
	yaml_mapping_set_int_member (map, "version", LRG_WEATHER_ASSET_FORMAT_VERSION);
	if (self->name != NULL)
		yaml_mapping_set_string_member (map, "name", self->name);
	if (self->default_preset != NULL)
		yaml_mapping_set_string_member (map, "default-preset", self->default_preset);
	set_float_member (map, "default-transition", self->default_transition);
	copy_extra (map, self->extra);

	presets = yaml_node_new_sequence (NULL);
	seq = yaml_node_get_sequence (presets);
	for (i = 0; i < self->presets->len; i++)
	{
		g_autoptr(YamlNode) pnode = preset_to_node (g_ptr_array_index (self->presets, i));

		yaml_sequence_add_element (seq, pnode);
	}
	yaml_mapping_set_member (map, "presets", presets);

	if (self->transitions->len > 0)
	{
		g_autoptr(YamlNode) tnode = yaml_node_new_sequence (NULL);
		YamlSequence       *tseq = yaml_node_get_sequence (tnode);

		for (i = 0; i < self->transitions->len; i++)
		{
			Transition         *t = g_ptr_array_index (self->transitions, i);
			g_autoptr(YamlNode) node = yaml_node_new_mapping (NULL);
			YamlMapping        *tmap = yaml_node_get_mapping (node);

			yaml_mapping_set_string_member (tmap, "from", t->from);
			yaml_mapping_set_string_member (tmap, "to", t->to);
			set_float_member (tmap, "duration", t->duration);
			copy_extra (tmap, t->extra);
			yaml_sequence_add_element (tseq, node);
		}
		yaml_mapping_set_member (map, "transitions", tnode);
	}

	doc = yaml_document_new_with_root (root);
	gen = yaml_generator_new ();
	yaml_generator_set_document (gen, doc);
	return yaml_generator_to_data (gen, NULL, error);
}

gboolean
lrg_weather_asset_save_to_file (LrgWeatherAsset  *self,
                                const gchar      *path,
                                GError          **error)
{
	g_autofree gchar *text = NULL;
	g_autoptr(GError) local = NULL;

	g_return_val_if_fail (LRG_IS_WEATHER_ASSET (self), FALSE);
	g_return_val_if_fail (path != NULL, FALSE);

	text = lrg_weather_asset_to_data (self, error);
	if (text == NULL)
		return FALSE;
	if (!g_file_set_contents (path, text, -1, &local))
	{
		g_set_error (error, LRG_WEATHER_ASSET_ERROR, LRG_WEATHER_ASSET_ERROR_IO, "%s", local->message);
		return FALSE;
	}
	return TRUE;
}

/* ==========================================================================
 * GObject
 * ========================================================================== */

static void
lrg_weather_asset_finalize (GObject *object)
{
	LrgWeatherAsset *self = LRG_WEATHER_ASSET (object);

	g_free (self->name);
	g_free (self->default_preset);
	g_free (self->base_dir);
	g_ptr_array_unref (self->presets);
	g_ptr_array_unref (self->transitions);
	if (self->extra != NULL)
		yaml_mapping_unref (self->extra);

	G_OBJECT_CLASS (lrg_weather_asset_parent_class)->finalize (object);
}

static void
lrg_weather_asset_class_init (LrgWeatherAssetClass *klass)
{
	G_OBJECT_CLASS (klass)->finalize = lrg_weather_asset_finalize;

	/**
	 * LrgWeatherAsset::changed:
	 * @self: the asset
	 * @preset: the preset index that changed, or -1 for asset settings
	 *   (name, defaults, transitions) and preset list changes
	 * @layer: the layer index that changed, or -1 for the preset itself
	 *   (name, ambient, layer list changes)
	 *
	 * Emitted after every edit made through the API: an inspector
	 * redraws, and a live preview calls lrg_weather_asset_sync_weather()
	 * for @preset.
	 */
	asset_signals[SIGNAL_CHANGED] =
		g_signal_new ("changed", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
		              G_TYPE_NONE, 2, G_TYPE_INT, G_TYPE_INT);
}

static void
lrg_weather_asset_init (LrgWeatherAsset *self)
{
	self->default_transition = 5.0f;
	self->presets = g_ptr_array_new_with_free_func (preset_free);
	self->transitions = g_ptr_array_new_with_free_func (transition_free);
}

/* ==========================================================================
 * Construction
 * ========================================================================== */

LrgWeatherAsset *
lrg_weather_asset_new (void)
{
	return g_object_new (LRG_TYPE_WEATHER_ASSET, NULL);
}

LrgWeatherAsset *
lrg_weather_asset_new_from_data (const gchar  *data,
                                 gssize        length,
                                 const gchar  *base_dir,
                                 GError      **error)
{
	g_autoptr(LrgWeatherAsset) self = NULL;
	g_autoptr(YamlParser)      parser = NULL;
	g_autoptr(GError)          local = NULL;

	g_return_val_if_fail (data != NULL, NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	parser = yaml_parser_new ();
	if (!yaml_parser_load_from_data (parser, data, length, &local))
	{
		g_set_error (error, LRG_WEATHER_ASSET_ERROR, LRG_WEATHER_ASSET_ERROR_PARSE, "%s", local->message);
		return NULL;
	}
	self = lrg_weather_asset_new ();
	self->base_dir = g_strdup (base_dir);
	if (!read_asset (self, yaml_parser_get_root (parser), error))
		return NULL;
	return g_steal_pointer (&self);
}

LrgWeatherAsset *
lrg_weather_asset_new_from_file (const gchar  *path,
                                 GError      **error)
{
	g_autofree gchar *text = NULL;
	g_autofree gchar *dir = NULL;
	g_autoptr(GError) local = NULL;
	LrgWeatherAsset  *self;
	gsize             len = 0;

	g_return_val_if_fail (path != NULL, NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	if (!g_file_get_contents (path, &text, &len, &local))
	{
		g_set_error (error, LRG_WEATHER_ASSET_ERROR, LRG_WEATHER_ASSET_ERROR_IO, "%s", local->message);
		return NULL;
	}
	dir = g_path_get_dirname (path);
	self = lrg_weather_asset_new_from_data (text, (gssize) len, dir, &local);
	if (self == NULL)
	{
		g_set_error (error, LRG_WEATHER_ASSET_ERROR, local->code, "%s: %s", path, local->message);
		return NULL;
	}
	return self;
}

LrgWeatherAsset *
lrg_weather_asset_copy (LrgWeatherAsset *self)
{
	LrgWeatherAsset *copy;
	guint            i;

	g_return_val_if_fail (LRG_IS_WEATHER_ASSET (self), NULL);

	copy = lrg_weather_asset_new ();
	copy->name = g_strdup (self->name);
	copy->default_preset = g_strdup (self->default_preset);
	copy->default_transition = self->default_transition;
	copy->base_dir = g_strdup (self->base_dir);
	if (self->extra != NULL)
		copy->extra = yaml_mapping_ref (self->extra);
	for (i = 0; i < self->presets->len; i++)
		g_ptr_array_add (copy->presets, preset_copy (g_ptr_array_index (self->presets, i)));
	for (i = 0; i < self->transitions->len; i++)
		g_ptr_array_add (copy->transitions, transition_copy (g_ptr_array_index (self->transitions, i)));
	return copy;
}

const gchar *
lrg_weather_asset_get_base_dir (LrgWeatherAsset *self)
{
	g_return_val_if_fail (LRG_IS_WEATHER_ASSET (self), NULL);
	return self->base_dir;
}

/* ==========================================================================
 * Asset settings
 * ========================================================================== */

const gchar *
lrg_weather_asset_get_name (LrgWeatherAsset *self)
{
	g_return_val_if_fail (LRG_IS_WEATHER_ASSET (self), NULL);
	return self->name;
}

void
lrg_weather_asset_set_name (LrgWeatherAsset *self,
                            const gchar     *name)
{
	g_return_if_fail (LRG_IS_WEATHER_ASSET (self));
	if (g_set_str (&self->name, name))
		emit_changed (self, -1, -1);
}

const gchar *
lrg_weather_asset_get_default_preset (LrgWeatherAsset *self)
{
	g_return_val_if_fail (LRG_IS_WEATHER_ASSET (self), NULL);
	return self->default_preset;
}

void
lrg_weather_asset_set_default_preset (LrgWeatherAsset *self,
                                      const gchar     *preset_id)
{
	g_return_if_fail (LRG_IS_WEATHER_ASSET (self));
	if (g_set_str (&self->default_preset, preset_id))
		emit_changed (self, -1, -1);
}

gfloat
lrg_weather_asset_get_default_transition (LrgWeatherAsset *self)
{
	g_return_val_if_fail (LRG_IS_WEATHER_ASSET (self), 0.0f);
	return self->default_transition;
}

void
lrg_weather_asset_set_default_transition (LrgWeatherAsset *self,
                                          gfloat           seconds)
{
	g_return_if_fail (LRG_IS_WEATHER_ASSET (self));
	seconds = MAX (seconds, 0.0f);
	if (self->default_transition == seconds)
		return;
	self->default_transition = seconds;
	emit_changed (self, -1, -1);
}

/* ==========================================================================
 * Presets
 * ========================================================================== */

guint
lrg_weather_asset_get_preset_count (LrgWeatherAsset *self)
{
	g_return_val_if_fail (LRG_IS_WEATHER_ASSET (self), 0);
	return self->presets->len;
}

gint
lrg_weather_asset_find_preset (LrgWeatherAsset *self,
                               const gchar     *preset_id)
{
	guint i;

	g_return_val_if_fail (LRG_IS_WEATHER_ASSET (self), -1);

	if (preset_id == NULL)
		return -1;
	for (i = 0; i < self->presets->len; i++)
		if (g_strcmp0 (((Preset *) g_ptr_array_index (self->presets, i))->id, preset_id) == 0)
			return (gint) i;
	return -1;
}

gint
lrg_weather_asset_add_preset (LrgWeatherAsset  *self,
                              const gchar      *preset_id,
                              const gchar      *name,
                              GError          **error)
{
	Preset *p;

	g_return_val_if_fail (LRG_IS_WEATHER_ASSET (self), -1);
	g_return_val_if_fail (preset_id != NULL && *preset_id != '\0', -1);

	if (lrg_weather_asset_find_preset (self, preset_id) >= 0 || g_strcmp0 (preset_id, "*") == 0 ||
	    g_strcmp0 (preset_id, "none") == 0)
	{
		g_set_error (error, LRG_WEATHER_ASSET_ERROR, LRG_WEATHER_ASSET_ERROR_DUPLICATE,
		             "preset id '%s' is taken", preset_id);
		return -1;
	}
	p = preset_new (preset_id);
	p->name = g_strdup (name);
	g_ptr_array_add (self->presets, p);
	emit_changed (self, -1, -1);
	return (gint) self->presets->len - 1;
}

gboolean
lrg_weather_asset_remove_preset (LrgWeatherAsset *self,
                                 guint            preset)
{
	g_return_val_if_fail (LRG_IS_WEATHER_ASSET (self), FALSE);

	if (preset >= self->presets->len)
		return FALSE;
	g_ptr_array_remove_index (self->presets, preset);
	emit_changed (self, -1, -1);
	return TRUE;
}

const gchar *
lrg_weather_asset_get_preset_id (LrgWeatherAsset *self,
                                 guint            preset)
{
	Preset *p;

	g_return_val_if_fail (LRG_IS_WEATHER_ASSET (self), NULL);
	p = get_preset (self, preset);
	return p != NULL ? p->id : NULL;
}

const gchar *
lrg_weather_asset_get_preset_name (LrgWeatherAsset *self,
                                   guint            preset)
{
	Preset *p;

	g_return_val_if_fail (LRG_IS_WEATHER_ASSET (self), NULL);
	p = get_preset (self, preset);
	return p != NULL ? p->name : NULL;
}

void
lrg_weather_asset_set_preset_name (LrgWeatherAsset *self,
                                   guint            preset,
                                   const gchar     *name)
{
	Preset *p;

	g_return_if_fail (LRG_IS_WEATHER_ASSET (self));
	p = get_preset (self, preset);
	g_return_if_fail (p != NULL);
	if (g_set_str (&p->name, name))
		emit_changed (self, (gint) preset, -1);
}

void
lrg_weather_asset_get_preset_ambient (LrgWeatherAsset *self,
                                      guint            preset,
                                      GrlColor        *color,
                                      gfloat          *brightness)
{
	Preset *p;

	g_return_if_fail (LRG_IS_WEATHER_ASSET (self));
	p = get_preset (self, preset);
	g_return_if_fail (p != NULL);
	if (color != NULL)
		*color = p->ambient;
	if (brightness != NULL)
		*brightness = p->brightness;
}

void
lrg_weather_asset_set_preset_ambient (LrgWeatherAsset *self,
                                      guint            preset,
                                      const GrlColor  *color,
                                      gfloat           brightness)
{
	Preset *p;

	g_return_if_fail (LRG_IS_WEATHER_ASSET (self));
	g_return_if_fail (color != NULL);
	p = get_preset (self, preset);
	g_return_if_fail (p != NULL);

	p->has_ambient = TRUE;
	p->ambient = *color;
	p->ambient.a = 255;
	p->brightness = CLAMP (brightness, 0.0f, 2.0f);
	emit_changed (self, (gint) preset, -1);
}

/* ==========================================================================
 * Layers
 * ========================================================================== */

guint
lrg_weather_asset_get_layer_count (LrgWeatherAsset *self,
                                   guint            preset)
{
	Preset *p;

	g_return_val_if_fail (LRG_IS_WEATHER_ASSET (self), 0);
	p = get_preset (self, preset);
	return p != NULL ? p->layers->len : 0;
}

gint
lrg_weather_asset_find_layer (LrgWeatherAsset *self,
                              guint            preset,
                              const gchar     *layer_id)
{
	Preset *p;
	guint   i;

	g_return_val_if_fail (LRG_IS_WEATHER_ASSET (self), -1);
	p = get_preset (self, preset);
	if (p == NULL || layer_id == NULL)
		return -1;
	for (i = 0; i < p->layers->len; i++)
		if (g_strcmp0 (layer_effective_id (g_ptr_array_index (p->layers, i)), layer_id) == 0)
			return (gint) i;
	return -1;
}

gint
lrg_weather_asset_add_layer (LrgWeatherAsset  *self,
                             guint             preset,
                             const gchar      *kind,
                             const gchar      *layer_id,
                             GError          **error)
{
	Preset *p;
	Layer  *layer;

	g_return_val_if_fail (LRG_IS_WEATHER_ASSET (self), -1);
	g_return_val_if_fail (kind != NULL, -1);

	p = get_preset (self, preset);
	if (p == NULL)
	{
		g_set_error (error, LRG_WEATHER_ASSET_ERROR, LRG_WEATHER_ASSET_ERROR_NOT_FOUND,
		             "no preset %u", preset);
		return -1;
	}
	if (lookup_kind (kind) == NULL)
	{
		g_set_error (error, LRG_WEATHER_ASSET_ERROR, LRG_WEATHER_ASSET_ERROR_UNKNOWN_KIND,
		             "unknown layer kind '%s'", kind);
		return -1;
	}
	layer = layer_new (kind);
	if (layer_id != NULL)
	{
		if (lrg_weather_asset_find_layer (self, preset, layer_id) >= 0)
		{
			g_set_error (error, LRG_WEATHER_ASSET_ERROR, LRG_WEATHER_ASSET_ERROR_DUPLICATE,
			             "preset '%s' already has a layer '%s'", p->id, layer_id);
			layer_free (layer);
			return -1;
		}
		layer->id = g_strdup (layer_id);
	}
	else
	{
		/* the kind, numbered when taken */
		guint n = 2;

		layer->id = g_strdup (kind);
		while (lrg_weather_asset_find_layer (self, preset, layer->id) >= 0)
		{
			g_free (layer->id);
			layer->id = g_strdup_printf ("%s-%u", kind, n++);
		}
	}
	g_ptr_array_add (p->layers, layer);
	emit_changed (self, (gint) preset, -1);
	return (gint) p->layers->len - 1;
}

gboolean
lrg_weather_asset_remove_layer (LrgWeatherAsset *self,
                                guint            preset,
                                guint            layer)
{
	Preset *p;

	g_return_val_if_fail (LRG_IS_WEATHER_ASSET (self), FALSE);
	p = get_preset (self, preset);
	if (p == NULL || layer >= p->layers->len)
		return FALSE;
	g_ptr_array_remove_index (p->layers, layer);
	emit_changed (self, (gint) preset, -1);
	return TRUE;
}

gboolean
lrg_weather_asset_move_layer (LrgWeatherAsset *self,
                              guint            preset,
                              guint            from,
                              guint            to)
{
	Preset *p;
	Layer  *layer;

	g_return_val_if_fail (LRG_IS_WEATHER_ASSET (self), FALSE);
	p = get_preset (self, preset);
	if (p == NULL || from >= p->layers->len || to >= p->layers->len)
		return FALSE;
	if (from == to)
		return TRUE;
	layer = g_ptr_array_steal_index (p->layers, from);
	g_ptr_array_insert (p->layers, (gint) to, layer);
	emit_changed (self, (gint) preset, -1);
	return TRUE;
}

const gchar *
lrg_weather_asset_get_layer_kind (LrgWeatherAsset *self,
                                  guint            preset,
                                  guint            layer)
{
	Layer *l;

	g_return_val_if_fail (LRG_IS_WEATHER_ASSET (self), NULL);
	l = get_layer (self, preset, layer);
	return l != NULL ? l->kind : NULL;
}

const gchar *
lrg_weather_asset_get_layer_id (LrgWeatherAsset *self,
                                guint            preset,
                                guint            layer)
{
	Layer *l;

	g_return_val_if_fail (LRG_IS_WEATHER_ASSET (self), NULL);
	l = get_layer (self, preset, layer);
	return l != NULL ? layer_effective_id (l) : NULL;
}

gfloat
lrg_weather_asset_get_layer_intensity (LrgWeatherAsset *self,
                                       guint            preset,
                                       guint            layer)
{
	Layer *l;

	g_return_val_if_fail (LRG_IS_WEATHER_ASSET (self), 0.0f);
	l = get_layer (self, preset, layer);
	return l != NULL ? l->intensity : 0.0f;
}

void
lrg_weather_asset_set_layer_intensity (LrgWeatherAsset *self,
                                       guint            preset,
                                       guint            layer,
                                       gfloat           intensity)
{
	Layer *l;

	g_return_if_fail (LRG_IS_WEATHER_ASSET (self));
	l = get_layer (self, preset, layer);
	g_return_if_fail (l != NULL);
	intensity = CLAMP (intensity, 0.0f, 1.0f);
	if (l->intensity == intensity)
		return;
	l->intensity = intensity;
	emit_changed (self, (gint) preset, (gint) layer);
}

gboolean
lrg_weather_asset_get_layer_enabled (LrgWeatherAsset *self,
                                     guint            preset,
                                     guint            layer)
{
	Layer *l;

	g_return_val_if_fail (LRG_IS_WEATHER_ASSET (self), FALSE);
	l = get_layer (self, preset, layer);
	return l != NULL && l->enabled;
}

void
lrg_weather_asset_set_layer_enabled (LrgWeatherAsset *self,
                                     guint            preset,
                                     guint            layer,
                                     gboolean         enabled)
{
	Layer *l;

	g_return_if_fail (LRG_IS_WEATHER_ASSET (self));
	l = get_layer (self, preset, layer);
	g_return_if_fail (l != NULL);
	enabled = !!enabled;
	if (l->enabled == enabled)
		return;
	l->enabled = enabled;
	emit_changed (self, (gint) preset, (gint) layer);
}

gboolean
lrg_weather_asset_set_layer_param (LrgWeatherAsset  *self,
                                   guint             preset,
                                   guint             layer,
                                   const gchar      *name,
                                   const GValue     *value,
                                   GError          **error)
{
	Layer           *l;
	Kind            *kind;
	GParamSpec      *ps;
	g_auto(GValue)   v = G_VALUE_INIT;
	g_autofree gchar *what = NULL;
	Param           *p;
	GType            type;

	g_return_val_if_fail (LRG_IS_WEATHER_ASSET (self), FALSE);
	g_return_val_if_fail (name != NULL, FALSE);
	g_return_val_if_fail (G_IS_VALUE (value), FALSE);

	l = get_layer (self, preset, layer);
	if (l == NULL)
	{
		g_set_error (error, LRG_WEATHER_ASSET_ERROR, LRG_WEATHER_ASSET_ERROR_NOT_FOUND,
		             "no layer %u in preset %u", layer, preset);
		return FALSE;
	}
	kind = lookup_kind (l->kind);
	what = g_strdup_printf ("presets[%u].layers[%u].params.%s", preset, layer, name);
	ps = kind != NULL ? kind_param (kind, name, NULL) : NULL;
	if (ps == NULL)
	{
		g_set_error (error, LRG_WEATHER_ASSET_ERROR, LRG_WEATHER_ASSET_ERROR_UNKNOWN_PARAM,
		             "%s: '%s' layers have no parameter '%s'", what, l->kind, name);
		return FALSE;
	}

	/* to the parameter's type: file syntax for strings, else transform */
	type = G_PARAM_SPEC_VALUE_TYPE (ps);
	if (G_VALUE_HOLDS_STRING (value) && type != G_TYPE_STRING)
	{
		if (!parse_text (ps, g_value_get_string (value), what, &v, error))
			return FALSE;
	}
	else
	{
		g_value_init (&v, type);
		if (G_VALUE_TYPE (value) == type)
			g_value_copy (value, &v);
		else if (!g_value_type_transformable (G_VALUE_TYPE (value), type) || !g_value_transform (value, &v))
		{
			g_set_error (error, LRG_WEATHER_ASSET_ERROR, LRG_WEATHER_ASSET_ERROR_INVALID_VALUE,
			             "%s: cannot convert %s to %s", what, G_VALUE_TYPE_NAME (value), g_type_name (type));
			return FALSE;
		}
		g_param_value_validate (ps, &v);
	}

	p = layer_find_param (l, name);
	if (p == NULL)
	{
		p = g_new0 (Param, 1);
		p->name = g_strdup (name);
		g_ptr_array_add (l->params, p);
	}
	g_free (p->text);
	p->text = value_text (&v);
	emit_changed (self, (gint) preset, (gint) layer);
	return TRUE;
}

gboolean
lrg_weather_asset_get_layer_param (LrgWeatherAsset *self,
                                   guint            preset,
                                   guint            layer,
                                   const gchar     *name,
                                   GValue          *value)
{
	Layer      *l;
	Param      *p;
	Kind       *kind;
	GParamSpec *ps;

	g_return_val_if_fail (LRG_IS_WEATHER_ASSET (self), FALSE);
	g_return_val_if_fail (name != NULL, FALSE);
	g_return_val_if_fail (value != NULL, FALSE);

	l = get_layer (self, preset, layer);
	if (l == NULL)
		return FALSE;
	p = layer_find_param (l, name);
	kind = lookup_kind (l->kind);
	ps = kind != NULL ? kind_param (kind, name, NULL) : NULL;
	if (p == NULL || ps == NULL)
		return FALSE;
	return parse_text (ps, p->text, name, value, NULL);
}

gboolean
lrg_weather_asset_unset_layer_param (LrgWeatherAsset *self,
                                     guint            preset,
                                     guint            layer,
                                     const gchar     *name)
{
	Layer *l;
	Param *p;

	g_return_val_if_fail (LRG_IS_WEATHER_ASSET (self), FALSE);
	l = get_layer (self, preset, layer);
	if (l == NULL)
		return FALSE;
	p = layer_find_param (l, name);
	if (p == NULL)
		return FALSE;
	g_ptr_array_remove (l->params, p);
	emit_changed (self, (gint) preset, (gint) layer);
	return TRUE;
}

gchar **
lrg_weather_asset_get_layer_param_names (LrgWeatherAsset *self,
                                         guint            preset,
                                         guint            layer)
{
	GStrvBuilder *b;
	Layer        *l;
	guint         i;

	g_return_val_if_fail (LRG_IS_WEATHER_ASSET (self), NULL);

	b = g_strv_builder_new ();
	l = get_layer (self, preset, layer);
	for (i = 0; l != NULL && i < l->params->len; i++)
		g_strv_builder_add (b, ((Param *) g_ptr_array_index (l->params, i))->name);
	{
		gchar **out = g_strv_builder_end (b);

		g_strv_builder_unref (b);
		return out;
	}
}

/* ==========================================================================
 * Transitions
 * ========================================================================== */

static Transition *
find_rule (LrgWeatherAsset *self,
           const gchar     *from,
           const gchar     *to)
{
	guint i;

	for (i = 0; i < self->transitions->len; i++)
	{
		Transition *t = g_ptr_array_index (self->transitions, i);

		if (g_strcmp0 (t->from, from) == 0 && g_strcmp0 (t->to, to) == 0)
			return t;
	}
	return NULL;
}

void
lrg_weather_asset_set_transition (LrgWeatherAsset *self,
                                  const gchar     *from,
                                  const gchar     *to,
                                  gfloat           seconds)
{
	Transition *t;

	g_return_if_fail (LRG_IS_WEATHER_ASSET (self));
	g_return_if_fail (from != NULL && to != NULL);

	t = find_rule (self, from, to);
	if (t == NULL)
	{
		t = g_new0 (Transition, 1);
		t->from = g_strdup (from);
		t->to = g_strdup (to);
		g_ptr_array_add (self->transitions, t);
	}
	t->duration = MAX (seconds, 0.0f);
	emit_changed (self, -1, -1);
}

gboolean
lrg_weather_asset_remove_transition (LrgWeatherAsset *self,
                                     const gchar     *from,
                                     const gchar     *to)
{
	Transition *t;

	g_return_val_if_fail (LRG_IS_WEATHER_ASSET (self), FALSE);
	t = find_rule (self, from, to);
	if (t == NULL)
		return FALSE;
	g_ptr_array_remove (self->transitions, t);
	emit_changed (self, -1, -1);
	return TRUE;
}

guint
lrg_weather_asset_get_transition_count (LrgWeatherAsset *self)
{
	g_return_val_if_fail (LRG_IS_WEATHER_ASSET (self), 0);
	return self->transitions->len;
}

gboolean
lrg_weather_asset_get_transition (LrgWeatherAsset  *self,
                                  guint             index,
                                  const gchar     **from,
                                  const gchar     **to,
                                  gfloat           *seconds)
{
	Transition *t;

	g_return_val_if_fail (LRG_IS_WEATHER_ASSET (self), FALSE);
	if (index >= self->transitions->len)
		return FALSE;
	t = g_ptr_array_index (self->transitions, index);
	if (from != NULL)
		*from = t->from;
	if (to != NULL)
		*to = t->to;
	if (seconds != NULL)
		*seconds = t->duration;
	return TRUE;
}

gfloat
lrg_weather_asset_resolve_transition (LrgWeatherAsset *self,
                                      const gchar     *from,
                                      const gchar     *to)
{
	Transition *t;

	g_return_val_if_fail (LRG_IS_WEATHER_ASSET (self), 0.0f);

	if (from == NULL)
		from = "none";
	if (to == NULL)
		to = "none";
	if ((t = find_rule (self, from, to)) != NULL || (t = find_rule (self, from, "*")) != NULL ||
	    (t = find_rule (self, "*", to)) != NULL || (t = find_rule (self, "*", "*")) != NULL)
		return t->duration;
	return self->default_transition;
}

/* ==========================================================================
 * Runtime: building and syncing effects
 * ========================================================================== */

/* apply_special: a format-only parameter onto @effect */
static void
apply_special (LrgWeatherEffect *effect,
               const gchar      *name,
               const GValue     *v,
               const gchar      *base_dir)
{
	if (g_strcmp0 (name, "asset") == 0 && LRG_IS_WEATHER_PARTICLES (effect))
	{
		const gchar                *rel = g_value_get_string (v);
		g_autofree gchar           *path = NULL;
		g_autoptr(LrgParticleAsset) asset = NULL;
		g_autoptr(GError)           local = NULL;

		if (rel == NULL || *rel == '\0')
		{
			lrg_weather_particles_set_asset (LRG_WEATHER_PARTICLES (effect), NULL, NULL);
			return;
		}
		path = g_path_is_absolute (rel) || base_dir == NULL ? g_strdup (rel)
		                                                    : g_build_filename (base_dir, rel, NULL);
		asset = lrg_particle_asset_new_from_file (path, &local);
		if (asset == NULL)
		{
			lrg_warning (LRG_LOG_DOMAIN_WEATHER, "weather particles: %s", local->message);
			return;
		}
		lrg_weather_particles_set_asset (LRG_WEATHER_PARTICLES (effect), asset, NULL);
		return;
	}
	if (g_strcmp0 (name, "color") == 0)
	{
		const GrlColor *c = g_value_get_boxed (v);

		if (c == NULL)
			return;
		if (LRG_IS_RAIN (effect))
			lrg_rain_set_color (LRG_RAIN (effect), c->r, c->g, c->b, c->a);
		else if (LRG_IS_SNOW (effect))
			lrg_snow_set_color (LRG_SNOW (effect), c->r, c->g, c->b, c->a);
		else if (LRG_IS_FOG (effect))
			lrg_fog_set_color (LRG_FOG (effect), c->r, c->g, c->b, c->a);
		else if (LRG_IS_LIGHTNING (effect))
			lrg_lightning_set_color (LRG_LIGHTNING (effect), c->r, c->g, c->b);
		return;
	}
	if (g_strcmp0 (name, "seed") == 0 && LRG_IS_RAIN (effect))
	{
		lrg_rain_set_seed (LRG_RAIN (effect), g_value_get_uint (v));
		return;
	}
	if (g_str_has_prefix (name, "area-"))
	{
		gfloat a[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
		gfloat f = g_value_get_float (v);

		if (LRG_IS_RAIN (effect))
			lrg_rain_get_area (LRG_RAIN (effect), &a[0], &a[1], &a[2], &a[3]);
		else if (LRG_IS_SNOW (effect))
			lrg_snow_get_area (LRG_SNOW (effect), &a[0], &a[1], &a[2], &a[3]);
		else
			return;
		if (g_strcmp0 (name, "area-x") == 0)
			a[0] = f;
		else if (g_strcmp0 (name, "area-y") == 0)
			a[1] = f;
		else if (g_strcmp0 (name, "area-width") == 0)
			a[2] = f;
		else
			a[3] = f;
		if (LRG_IS_RAIN (effect))
			lrg_rain_set_area (LRG_RAIN (effect), a[0], a[1], a[2], a[3]);
		else
			lrg_snow_set_area (LRG_SNOW (effect), a[0], a[1], a[2], a[3]);
		return;
	}
	if (g_str_has_prefix (name, "volume-"))
	{
		gfloat radius, height;

		lrg_weather_effect_get_volume (effect, &radius, &height);
		if (g_strcmp0 (name, "volume-radius") == 0)
			radius = g_value_get_float (v);
		else
			height = g_value_get_float (v);
		lrg_weather_effect_set_volume (effect, radius, height);
		return;
	}
	if (g_str_has_prefix (name, "bolt-") && LRG_IS_LIGHTNING (effect))
	{
		gfloat s[3];

		lrg_lightning_get_bolt_shape (LRG_LIGHTNING (effect), &s[0], &s[1], &s[2]);
		if (g_strcmp0 (name, "bolt-distance") == 0)
			s[0] = g_value_get_float (v);
		else if (g_strcmp0 (name, "bolt-height") == 0)
			s[1] = g_value_get_float (v);
		else
			s[2] = g_value_get_float (v);
		lrg_lightning_set_bolt_shape (LRG_LIGHTNING (effect), s[0], s[1], s[2]);
		return;
	}
	if (LRG_IS_WEATHER_PARTICLES (effect))
		g_object_set_property (G_OBJECT (lrg_weather_particles_get_emitter (LRG_WEATHER_PARTICLES (effect))),
		                       name, v);
}

/* apply_param: one stored parameter onto @effect */
static gboolean
apply_param (Kind              *kind,
             LrgWeatherEffect  *effect,
             const Param       *p,
             const gchar       *path,
             const gchar       *base_dir,
             GError           **error)
{
	gboolean          special = FALSE;
	GParamSpec       *ps = kind_param (kind, p->name, &special);
	g_auto(GValue)    v = G_VALUE_INIT;
	g_autofree gchar *what = g_strdup_printf ("%s.params.%s", path, p->name);

	if (ps == NULL)
	{
		g_set_error (error, LRG_WEATHER_ASSET_ERROR, LRG_WEATHER_ASSET_ERROR_UNKNOWN_PARAM,
		             "%s: '%s' layers have no parameter '%s'", what, kind->name, p->name);
		return FALSE;
	}
	if (!parse_text (ps, p->text, what, &v, error))
		return FALSE;
	if (special)
		apply_special (effect, p->name, &v, base_dir);
	else
		g_object_set_property (G_OBJECT (effect), p->name, &v);
	return TRUE;
}

#define PARAMS_KEY "lrg-weather-asset-params"

/* param_names_key: the layer's parameter names, for detecting unsets */
static gchar *
param_names_key (const Layer *layer)
{
	GString *s = g_string_new (NULL);
	guint    i;

	for (i = 0; i < layer->params->len; i++)
	{
		g_string_append (s, ((Param *) g_ptr_array_index (layer->params, i))->name);
		g_string_append_c (s, '\n');
	}
	return g_string_free (s, FALSE);
}

/* configure_effect: id, parameters (space first: it resets the unit-
 * dependent settings) and level */
static gboolean
configure_effect (const Layer       *layer,
                  LrgWeatherEffect  *effect,
                  const gchar       *path,
                  const gchar       *base_dir,
                  GError           **error)
{
	Kind  *kind = lookup_kind (layer->kind);
	guint  i;

	if (kind == NULL)
	{
		g_set_error (error, LRG_WEATHER_ASSET_ERROR, LRG_WEATHER_ASSET_ERROR_UNKNOWN_KIND,
		             "%s.kind: unknown layer kind '%s'", path, layer->kind);
		return FALSE;
	}
	lrg_weather_effect_set_id (effect, layer_effective_id (layer));
	for (i = 0; i < layer->params->len; i++)
	{
		const Param *p = g_ptr_array_index (layer->params, i);

		if (g_strcmp0 (p->name, "space") == 0 && !apply_param (kind, effect, p, path, base_dir, error))
			return FALSE;
	}
	for (i = 0; i < layer->params->len; i++)
	{
		const Param *p = g_ptr_array_index (layer->params, i);

		if (g_strcmp0 (p->name, "space") != 0 && !apply_param (kind, effect, p, path, base_dir, error))
			return FALSE;
	}
	lrg_weather_effect_set_level (effect, layer->intensity);
	g_object_set_data_full (G_OBJECT (effect), PARAMS_KEY, param_names_key (layer), g_free);
	return TRUE;
}

static LrgWeatherEffect *
build_effect (const Layer  *layer,
              const gchar  *path,
              const gchar  *base_dir,
              GError      **error)
{
	Kind                       *kind = lookup_kind (layer->kind);
	g_autoptr(LrgWeatherEffect) effect = NULL;

	if (kind == NULL)
	{
		g_set_error (error, LRG_WEATHER_ASSET_ERROR, LRG_WEATHER_ASSET_ERROR_UNKNOWN_KIND,
		             "%s.kind: unknown layer kind '%s'", path, layer->kind);
		return NULL;
	}
	effect = g_object_new (kind->type, NULL);
	if (!configure_effect (layer, effect, path, base_dir, error))
		return NULL;
	lrg_weather_effect_set_intensity (effect, layer->intensity);
	return g_steal_pointer (&effect);
}

/* weather_apply_ambient: the preset's ambient light, when it has one */
static void
weather_apply_ambient (const Preset *preset,
                       LrgWeather   *weather)
{
	if (!preset->has_ambient)
		return;
	lrg_weather_set_ambient_color (weather, preset->ambient.r, preset->ambient.g, preset->ambient.b);
	lrg_weather_set_ambient_brightness (weather, preset->brightness);
}

LrgWeather *
lrg_weather_asset_build_weather (LrgWeatherAsset  *self,
                                 guint             preset,
                                 GError          **error)
{
	g_autoptr(LrgWeather) weather = NULL;
	Preset               *p;
	guint                 i;

	g_return_val_if_fail (LRG_IS_WEATHER_ASSET (self), NULL);

	p = get_preset (self, preset);
	if (p == NULL)
	{
		g_set_error (error, LRG_WEATHER_ASSET_ERROR, LRG_WEATHER_ASSET_ERROR_NOT_FOUND,
		             "no preset %u", preset);
		return NULL;
	}
	weather = lrg_weather_new (p->id, p->name != NULL ? p->name : p->id);
	weather_apply_ambient (p, weather);
	for (i = 0; i < p->layers->len; i++)
	{
		const Layer                *layer = g_ptr_array_index (p->layers, i);
		g_autofree gchar           *path = g_strdup_printf ("presets[%u].layers[%u]", preset, i);
		g_autoptr(LrgWeatherEffect) effect = NULL;

		if (!layer->enabled)
			continue;
		effect = build_effect (layer, path, self->base_dir, error);
		if (effect == NULL)
			return NULL;
		lrg_weather_add_effect (weather, effect);
	}
	return g_steal_pointer (&weather);
}

gboolean
lrg_weather_asset_sync_weather (LrgWeatherAsset  *self,
                                guint             preset,
                                LrgWeather       *weather,
                                GError          **error)
{
	Preset           *p;
	g_autoptr(GPtrArray) wanted = NULL;   /* effects in layer order */
	GList            *effects, *l;
	gboolean          order_ok = TRUE;
	guint             i, pos;

	g_return_val_if_fail (LRG_IS_WEATHER_ASSET (self), FALSE);
	g_return_val_if_fail (LRG_IS_WEATHER (weather), FALSE);

	p = get_preset (self, preset);
	if (p == NULL)
	{
		g_set_error (error, LRG_WEATHER_ASSET_ERROR, LRG_WEATHER_ASSET_ERROR_NOT_FOUND,
		             "no preset %u", preset);
		return FALSE;
	}
	weather_apply_ambient (p, weather);
	wanted = g_ptr_array_new_with_free_func (g_object_unref);

	/* 1. every enabled layer: reuse the live effect when the id and kind
	 *    match and no parameter was unset, else build a fresh one */
	for (i = 0; i < p->layers->len; i++)
	{
		const Layer      *layer = g_ptr_array_index (p->layers, i);
		g_autofree gchar *path = g_strdup_printf ("presets[%u].layers[%u]", preset, i);
		LrgWeatherEffect *live;
		Kind             *kind = lookup_kind (layer->kind);

		if (!layer->enabled)
			continue;
		live = lrg_weather_get_effect (weather, layer_effective_id (layer));
		if (live != NULL && kind != NULL && G_OBJECT_TYPE (live) == kind->type)
		{
			const gchar      *old = g_object_get_data (G_OBJECT (live), PARAMS_KEY);
			g_auto(GStrv)     old_names = g_strsplit (old != NULL ? old : "", "\n", -1);
			gboolean          unset = FALSE;
			guint             j;

			for (j = 0; old_names[j] != NULL; j++)
				if (old_names[j][0] != '\0' && layer_find_param (layer, old_names[j]) == NULL)
					unset = TRUE;
			if (!unset)
			{
				gfloat before = lrg_weather_effect_get_full_intensity (live);

				if (!configure_effect (layer, live, path, self->base_dir, error))
					return FALSE;
				/* a playing effect at full strength follows its new level */
				if (lrg_weather_effect_is_active (live) &&
				    fabsf (lrg_weather_effect_get_intensity (live) - before) < 1e-4f)
					lrg_weather_effect_set_intensity (live, layer->intensity);
				g_ptr_array_add (wanted, g_object_ref (live));
				continue;
			}
		}
		{
			LrgWeatherEffect *fresh = build_effect (layer, path, self->base_dir, error);

			if (fresh == NULL)
				return FALSE;
			g_ptr_array_add (wanted, fresh);
		}
	}

	/* 2. drop effects no layer wants, and check the order */
	effects = lrg_weather_get_effects (weather);
	pos = 0;
	for (l = effects; l != NULL; l = l->next)
	{
		if (!g_ptr_array_find (wanted, l->data, NULL))
		{
			lrg_weather_remove_effect (weather, l->data);
			continue;
		}
		if (pos >= wanted->len || g_ptr_array_index (wanted, pos) != l->data)
			order_ok = FALSE;
		pos++;
	}
	g_list_free (effects);

	/* 3. add the new ones (re-adding everything when the order changed) */
	if (!order_ok)
	{
		effects = lrg_weather_get_effects (weather);
		for (l = effects; l != NULL; l = l->next)
			lrg_weather_remove_effect (weather, l->data);
		g_list_free (effects);
	}
	for (i = 0; i < wanted->len; i++)
	{
		LrgWeatherEffect *e = g_ptr_array_index (wanted, i);
		GList            *cur = lrg_weather_get_effects (weather);
		gboolean          present = g_list_find (cur, e) != NULL;

		g_list_free (cur);
		if (!present)
			lrg_weather_add_effect (weather, e);
	}
	return TRUE;
}

gboolean
lrg_weather_asset_apply (LrgWeatherAsset    *self,
                         LrgWeatherManager  *manager,
                         GError            **error)
{
	guint i;

	g_return_val_if_fail (LRG_IS_WEATHER_ASSET (self), FALSE);
	g_return_val_if_fail (LRG_IS_WEATHER_MANAGER (manager), FALSE);

	for (i = 0; i < self->presets->len; i++)
	{
		Preset     *p = g_ptr_array_index (self->presets, i);
		LrgWeather *live = lrg_weather_manager_get_weather (manager, p->id);

		if (live != NULL)
		{
			if (!lrg_weather_asset_sync_weather (self, i, live, error))
				return FALSE;
		}
		else
		{
			g_autoptr(LrgWeather) built = lrg_weather_asset_build_weather (self, i, error);

			if (built == NULL)
				return FALSE;
			lrg_weather_manager_register_weather (manager, built);
		}
	}
	if (lrg_weather_manager_get_active_weather (manager) == NULL && self->default_preset != NULL)
		lrg_weather_manager_set_weather (manager, self->default_preset, 0.0f);
	return TRUE;
}

gboolean
lrg_weather_asset_transition (LrgWeatherAsset    *self,
                              LrgWeatherManager  *manager,
                              const gchar        *preset_id,
                              GError            **error)
{
	const gchar *from;

	g_return_val_if_fail (LRG_IS_WEATHER_ASSET (self), FALSE);
	g_return_val_if_fail (LRG_IS_WEATHER_MANAGER (manager), FALSE);

	if (preset_id != NULL && lrg_weather_manager_get_weather (manager, preset_id) == NULL)
	{
		g_set_error (error, LRG_WEATHER_ASSET_ERROR, LRG_WEATHER_ASSET_ERROR_NOT_FOUND,
		             "no preset '%s' on the manager (lrg_weather_asset_apply() first)", preset_id);
		return FALSE;
	}
	from = lrg_weather_manager_get_active_weather_id (manager);
	if (preset_id == NULL)
		lrg_weather_manager_clear_weather (manager, lrg_weather_asset_resolve_transition (self, from, NULL));
	else
		lrg_weather_manager_set_weather (manager, preset_id,
		                                 lrg_weather_asset_resolve_transition (self, from, preset_id));
	return TRUE;
}
