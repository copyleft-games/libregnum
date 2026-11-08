/* lrg-foley-patch.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Editable sound-effect recipes (see lrg-foley-patch.h and
 * docs/modules/audio/foley.org).
 *
 * The synthesis parts (bodies, grains, washes, the rain bed) are the ones
 * lrg_foley_synthesize() always used, kept arithmetic for arithmetic: the
 * same float and double operations in the same order, so a patch built
 * from a kind renders the same bytes.  A built-in kind is turned into a
 * patch by running its recipe in "record" mode: every part it would add
 * becomes a layer holding the values the recipe drew at that point and the
 * noise generator's state when the part began, and the generator is
 * advanced by exactly the draws the part would have made.  Rendering a
 * layer restores that state, so layers are independent of each other.
 *
 * Layers whose seed is 0 derive their noise state from the patch seed and
 * their index.  count > 1 scatters copies of a layer over `spread`
 * seconds; jitter varies their level, pitch and length.  Both use their
 * own generator, so a layer with count 1 and no jitter draws nothing
 * beyond its noise.
 */

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_AUDIO

#include "config.h"

#include <gio/gio.h>
#include <math.h>
#include <string.h>

#include "lrg-foley-patch.h"
#include "lrg-audio-yaml-private.h"

#define DEFAULT_PEAK     (0.8)
#define DEFAULT_FADE     (0.004)
#define DEFAULT_DURATION (0.5)
#define MAX_PARAMS       (20)

G_DEFINE_ENUM_TYPE (LrgFoleyLayerType, lrg_foley_layer_type,
	G_DEFINE_ENUM_VALUE (LRG_FOLEY_LAYER_BODY, "body"),
	G_DEFINE_ENUM_VALUE (LRG_FOLEY_LAYER_GRAIN, "grain"),
	G_DEFINE_ENUM_VALUE (LRG_FOLEY_LAYER_WASH, "wash"),
	G_DEFINE_ENUM_VALUE (LRG_FOLEY_LAYER_BED, "bed"),
	G_DEFINE_ENUM_VALUE (LRG_FOLEY_LAYER_TONE, "tone"),
	G_DEFINE_ENUM_VALUE (LRG_FOLEY_LAYER_NOISE, "noise"))

G_DEFINE_QUARK (lrg-foley-patch-error-quark, lrg_foley_patch_error)

/* ==========================================================================
 * Parameter tables
 * ========================================================================== */

typedef struct
{
	const gchar *name;
	const gchar *blurb;
	gdouble      min;
	gdouble      max;
	gdouble      def;
} ParamDef;

/* every layer type starts with these, in this order */
enum
{
	P_AT,
	P_AMP,
	P_MUTE,
	P_SEED,
	P_COUNT,
	P_SPREAD,
	P_JITTER,
	N_COMMON
};

static const ParamDef common_params[N_COMMON] = {
	{ "at", "Start time in seconds", 0.0, LRG_FOLEY_PATCH_MAX_DURATION, 0.0 },
	{ "amp", "Level of the layer", 0.0, 4.0, 1.0 },
	{ "mute", "1 silences the layer", 0.0, 1.0, 0.0 },
	{ "seed", "Noise seed (0: derived from the patch seed)", 0.0, 4294967295.0, 0.0 },
	{ "count", "Copies scattered over the spread", 1.0, 64.0, 1.0 },
	{ "spread", "Seconds the copies scatter over", 0.0, LRG_FOLEY_PATCH_MAX_DURATION, 0.0 },
	{ "jitter", "How much copies vary in level, pitch and length", 0.0, 1.0, 0.0 },
};

/* type-specific parameter indices (after the common ones) */
enum { BODY_FREQ = N_COMMON, BODY_SWEEP, BODY_DECAY, BODY_N };
enum { GRAIN_LENGTH = N_COMMON, GRAIN_LOW, GRAIN_HIGH, GRAIN_N };
enum { WASH_DECAY = N_COMMON, WASH_CUTOFF, WASH_N };
enum { BED_LOW = N_COMMON, BED_HIGH, BED_DRIFT, BED_BASE, BED_DEPTH, BED_DROPLETS, BED_N };
enum { TONE_FREQ = N_COMMON, TONE_FREQ_END, TONE_WAVEFORM, TONE_DUTY, TONE_LENGTH, TONE_ATTACK, TONE_DECAY,
       TONE_VIB_RATE, TONE_VIB_DEPTH, TONE_N };
enum { NOISE_LENGTH = N_COMMON, NOISE_ATTACK, NOISE_DECAY, NOISE_CUTOFF, NOISE_CUTOFF_END, NOISE_HIGHPASS,
       NOISE_N };

static const ParamDef body_params[] = {
	{ "freq", "Frequency in Hz", 10.0, 20000.0, 120.0 },
	{ "sweep", "Frequency multiplier over the decay", 0.05, 20.0, 1.0 },
	{ "decay", "Seconds to fall by 1/e", 0.0005, 10.0, 0.05 },
};

static const ParamDef grain_params[] = {
	{ "length", "Length in seconds", 0.0005, 10.0, 0.02 },
	{ "low", "Lower edge of the noise band in Hz", 10.0, 20000.0, 500.0 },
	{ "high", "Upper edge of the noise band in Hz", 10.0, 20000.0, 4000.0 },
};

static const ParamDef wash_params[] = {
	{ "decay", "Seconds to fall by 1/e", 0.0005, 10.0, 0.05 },
	{ "cutoff", "Low-pass of the noise in Hz", 10.0, 20000.0, 1000.0 },
};

static const ParamDef bed_params[] = {
	{ "low", "Lower edge of the noise band in Hz", 10.0, 20000.0, 600.0 },
	{ "high", "Upper edge of the noise band in Hz", 10.0, 20000.0, 7000.0 },
	{ "drift", "Speed of the level drift in Hz", 0.01, 20.0, 0.7 },
	{ "base", "Level the drift moves around", 0.0, 2.0, 0.55 },
	{ "depth", "Size of the drift", 0.0, 2.0, 0.6 },
	{ "droplets", "Droplet ticks per second", 0.0, 2000.0, 73.5 },
};

static const ParamDef tone_params[] = {
	{ "freq", "Start frequency in Hz", 10.0, 20000.0, 880.0 },
	{ "freq-end", "End frequency in Hz (exponential sweep)", 10.0, 20000.0, 880.0 },
	{ "waveform", "0 sine, 1 triangle, 2 square, 3 saw", 0.0, 3.0, 0.0 },
	{ "duty", "Pulse width of the square", 0.05, 0.95, 0.5 },
	{ "length", "Length in seconds", 0.001, LRG_FOLEY_PATCH_MAX_DURATION, 0.25 },
	{ "attack", "Seconds to full level", 0.0, 10.0, 0.002 },
	{ "decay", "Seconds to fall by 1/e (0: hold)", 0.0, 10.0, 0.0 },
	{ "vibrato-rate", "Vibrato speed in Hz", 0.0, 50.0, 0.0 },
	{ "vibrato-depth", "Vibrato depth in semitones", 0.0, 24.0, 0.0 },
};

static const ParamDef noise_params[] = {
	{ "length", "Length in seconds", 0.001, LRG_FOLEY_PATCH_MAX_DURATION, 0.4 },
	{ "attack", "Seconds to full level", 0.0, 10.0, 0.005 },
	{ "decay", "Seconds to fall by 1/e (0: hold)", 0.0, 10.0, 0.12 },
	{ "cutoff", "Start low-pass in Hz", 10.0, 20000.0, 8000.0 },
	{ "cutoff-end", "End low-pass in Hz (exponential sweep)", 10.0, 20000.0, 8000.0 },
	{ "highpass", "High-pass in Hz (0: off)", 0.0, 20000.0, 0.0 },
};

/* the type-specific table of @type and its length */
static const ParamDef *
type_params (LrgFoleyLayerType  type,
             guint             *n)
{
	switch (type)
	{
	case LRG_FOLEY_LAYER_BODY: *n = G_N_ELEMENTS (body_params); return body_params;
	case LRG_FOLEY_LAYER_GRAIN: *n = G_N_ELEMENTS (grain_params); return grain_params;
	case LRG_FOLEY_LAYER_WASH: *n = G_N_ELEMENTS (wash_params); return wash_params;
	case LRG_FOLEY_LAYER_BED: *n = G_N_ELEMENTS (bed_params); return bed_params;
	case LRG_FOLEY_LAYER_TONE: *n = G_N_ELEMENTS (tone_params); return tone_params;
	case LRG_FOLEY_LAYER_NOISE: *n = G_N_ELEMENTS (noise_params); return noise_params;
	default: *n = 0; return NULL;
	}
}

static gboolean
valid_type (LrgFoleyLayerType type)
{
	return (guint) type <= (guint) LRG_FOLEY_LAYER_NOISE;
}

/* what param_at () returns past the end (callers stay below param_count ()) */
static const ParamDef no_param = { "", "", 0.0, 0.0, 0.0 };

/* the definition of parameter @i of @type (common first) */
static const ParamDef *
param_at (LrgFoleyLayerType type,
          guint             i)
{
	guint           n = 0;
	const ParamDef *specific = type_params (type, &n);

	if (i < N_COMMON)
		return &common_params[i];
	if (specific == NULL || i - N_COMMON >= n)
		return &no_param;
	return &specific[i - N_COMMON];
}

static guint
param_count (LrgFoleyLayerType type)
{
	guint n = 0;

	type_params (type, &n);
	return N_COMMON + n;
}

static gint
param_index (LrgFoleyLayerType  type,
             const gchar       *name)
{
	guint i, n = param_count (type);

	if (name == NULL)
		return -1;
	for (i = 0; i < n; i++)
		if (g_strcmp0 (param_at (type, i)->name, name) == 0)
			return (gint) i;
	return -1;
}

/* ==========================================================================
 * The object
 * ========================================================================== */

typedef struct
{
	LrgFoleyLayerType type;
	gdouble           p[MAX_PARAMS];
} Layer;

struct _LrgFoleyPatch
{
	GObject  parent_instance;

	gchar   *name;
	gdouble  duration;
	guint    rate;
	guint32  seed;
	gboolean loop;
	gdouble  loop_tail;
	gdouble  fade;
	gdouble  peak;
	gboolean normalize;
	GArray  *layers;      /* Layer */
};

G_DEFINE_FINAL_TYPE (LrgFoleyPatch, lrg_foley_patch, G_TYPE_OBJECT)

enum
{
	PROP_0,
	PROP_NAME,
	PROP_DURATION,
	PROP_SAMPLE_RATE,
	PROP_SEED,
	PROP_LOOP,
	PROP_LOOP_TAIL,
	PROP_FADE,
	PROP_PEAK,
	PROP_NORMALIZE,
	N_PROPS
};

static GParamSpec *props[N_PROPS];

enum
{
	SIGNAL_CHANGED,
	N_SIGNALS
};

static guint signals[N_SIGNALS];

static void
changed (LrgFoleyPatch *self)
{
	g_signal_emit (self, signals[SIGNAL_CHANGED], 0);
}

static void
layer_defaults (Layer             *layer,
                LrgFoleyLayerType  type)
{
	guint i, n = param_count (type);

	memset (layer, 0, sizeof *layer);
	layer->type = type;
	for (i = 0; i < n; i++)
		layer->p[i] = param_at (type, i)->def;
}

static void
lrg_foley_patch_finalize (GObject *object)
{
	LrgFoleyPatch *self = LRG_FOLEY_PATCH (object);

	g_free (self->name);
	g_array_unref (self->layers);
	G_OBJECT_CLASS (lrg_foley_patch_parent_class)->finalize (object);
}

static void
lrg_foley_patch_get_property (GObject    *object,
                              guint       prop_id,
                              GValue     *value,
                              GParamSpec *pspec)
{
	LrgFoleyPatch *self = LRG_FOLEY_PATCH (object);

	switch (prop_id)
	{
	case PROP_NAME: g_value_set_string (value, self->name); break;
	case PROP_DURATION: g_value_set_double (value, self->duration); break;
	case PROP_SAMPLE_RATE: g_value_set_uint (value, self->rate); break;
	case PROP_SEED: g_value_set_uint (value, self->seed); break;
	case PROP_LOOP: g_value_set_boolean (value, self->loop); break;
	case PROP_LOOP_TAIL: g_value_set_double (value, self->loop_tail); break;
	case PROP_FADE: g_value_set_double (value, self->fade); break;
	case PROP_PEAK: g_value_set_double (value, self->peak); break;
	case PROP_NORMALIZE: g_value_set_boolean (value, self->normalize); break;
	default: G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_foley_patch_set_property (GObject      *object,
                              guint         prop_id,
                              const GValue *value,
                              GParamSpec   *pspec)
{
	LrgFoleyPatch *self = LRG_FOLEY_PATCH (object);

	switch (prop_id)
	{
	case PROP_NAME:
		g_free (self->name);
		self->name = g_value_dup_string (value);
		break;
	case PROP_DURATION: self->duration = g_value_get_double (value); break;
	case PROP_SAMPLE_RATE: self->rate = g_value_get_uint (value); break;
	case PROP_SEED: self->seed = g_value_get_uint (value); break;
	case PROP_LOOP: self->loop = g_value_get_boolean (value); break;
	case PROP_LOOP_TAIL: self->loop_tail = g_value_get_double (value); break;
	case PROP_FADE: self->fade = g_value_get_double (value); break;
	case PROP_PEAK: self->peak = g_value_get_double (value); break;
	case PROP_NORMALIZE: self->normalize = g_value_get_boolean (value); break;
	default: G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec); return;
	}
	changed (self);
}

static void
lrg_foley_patch_class_init (LrgFoleyPatchClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	GParamFlags   flags = G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS;

	object_class->finalize = lrg_foley_patch_finalize;
	object_class->get_property = lrg_foley_patch_get_property;
	object_class->set_property = lrg_foley_patch_set_property;

	/**
	 * LrgFoleyPatch:name:
	 *
	 * The display name (nullable).
	 */
	props[PROP_NAME] = g_param_spec_string ("name", NULL, NULL, NULL, flags);

	/**
	 * LrgFoleyPatch:duration:
	 *
	 * Length of the rendered clip in seconds.
	 */
	props[PROP_DURATION] = g_param_spec_double ("duration", NULL, NULL, 0.01, LRG_FOLEY_PATCH_MAX_DURATION,
	                                            DEFAULT_DURATION, flags);

	/**
	 * LrgFoleyPatch:sample-rate:
	 *
	 * Render rate in Hz.
	 */
	props[PROP_SAMPLE_RATE] = g_param_spec_uint ("sample-rate", NULL, NULL, 8000, 96000,
	                                             LRG_FOLEY_DEFAULT_RATE, flags);

	/**
	 * LrgFoleyPatch:seed:
	 *
	 * The seed of the layers whose own seed is 0.
	 */
	props[PROP_SEED] = g_param_spec_uint ("seed", NULL, NULL, 0, G_MAXUINT32, 1, flags);

	/**
	 * LrgFoleyPatch:loop:
	 *
	 * Whether the clip loops seamlessly: it is rendered
	 * #LrgFoleyPatch:loop-tail seconds longer and that tail is crossfaded
	 * onto the head instead of fading the ends.
	 */
	props[PROP_LOOP] = g_param_spec_boolean ("loop", NULL, NULL, FALSE, flags);

	/**
	 * LrgFoleyPatch:loop-tail:
	 *
	 * Seconds of the loop crossfade.
	 */
	props[PROP_LOOP_TAIL] = g_param_spec_double ("loop-tail", NULL, NULL, 0.0, 10.0, 0.25, flags);

	/**
	 * LrgFoleyPatch:fade:
	 *
	 * Seconds of the fade-in and fade-out of a clip that does not loop
	 * (0: none).
	 */
	props[PROP_FADE] = g_param_spec_double ("fade", NULL, NULL, 0.0, 1.0, DEFAULT_FADE, flags);

	/**
	 * LrgFoleyPatch:peak:
	 *
	 * The peak level normalisation scales to.
	 */
	props[PROP_PEAK] = g_param_spec_double ("peak", NULL, NULL, 0.01, 1.0, DEFAULT_PEAK, flags);

	/**
	 * LrgFoleyPatch:normalize:
	 *
	 * Whether the render is scaled so its peak is #LrgFoleyPatch:peak;
	 * without it the layers' levels are absolute (clamped at full scale).
	 */
	props[PROP_NORMALIZE] = g_param_spec_boolean ("normalize", NULL, NULL, TRUE, flags);

	g_object_class_install_properties (object_class, N_PROPS, props);

	/**
	 * LrgFoleyPatch::changed:
	 * @self: the patch
	 *
	 * Emitted after any setting or layer changed.
	 */
	signals[SIGNAL_CHANGED] = g_signal_new ("changed", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST, 0,
	                                        NULL, NULL, NULL, G_TYPE_NONE, 0);
}

static void
lrg_foley_patch_init (LrgFoleyPatch *self)
{
	self->duration = DEFAULT_DURATION;
	self->rate = LRG_FOLEY_DEFAULT_RATE;
	self->seed = 1;
	self->loop_tail = 0.25;
	self->fade = DEFAULT_FADE;
	self->peak = DEFAULT_PEAK;
	self->normalize = TRUE;
	self->layers = g_array_new (FALSE, FALSE, sizeof (Layer));
}

LrgFoleyPatch *
lrg_foley_patch_new (void)
{
	return g_object_new (LRG_TYPE_FOLEY_PATCH, NULL);
}

LrgFoleyPatch *
lrg_foley_patch_copy (LrgFoleyPatch *self)
{
	LrgFoleyPatch *copy;

	g_return_val_if_fail (LRG_IS_FOLEY_PATCH (self), NULL);

	copy = lrg_foley_patch_new ();
	copy->name = g_strdup (self->name);
	copy->duration = self->duration;
	copy->rate = self->rate;
	copy->seed = self->seed;
	copy->loop = self->loop;
	copy->loop_tail = self->loop_tail;
	copy->fade = self->fade;
	copy->peak = self->peak;
	copy->normalize = self->normalize;
	g_array_append_vals (copy->layers, self->layers->data, self->layers->len);
	return copy;
}

/* ==========================================================================
 * The synthesis parts (the arithmetic of 0.9's lrg-foley.c, unchanged)
 * ========================================================================== */

typedef struct
{
	guint32  state;
	guint    rate;
	gfloat  *out;
	guint    frames;

	/* record mode (lrg_foley_patch_new_from_kind ()): parts become layers */
	GArray  *record;
} Synth;

/* xorshift32, never 0 */
static guint32
next_random (Synth *s)
{
	guint32 x = s->state != 0 ? s->state : 0x9E3779B9u;

	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;
	s->state = x;
	return x;
}

/* 0 .. 1 */
static gfloat
unit (Synth *s)
{
	return (gfloat) (next_random (s) & 0xFFFFFFu) / (gfloat) 0xFFFFFFu;
}

/* -1 .. 1 white noise */
static gfloat
noise (Synth *s)
{
	return unit (s) * 2.0f - 1.0f;
}

/* a value in [lo, hi] */
static gfloat
between (Synth *s,
         gfloat lo,
         gfloat hi)
{
	return lo + (hi - lo) * unit (s);
}

/* one-pole low-pass coefficient for a cut-off in Hz */
static gfloat
lp_coeff (Synth *s,
          gfloat hz)
{
	return 1.0f - expf (-2.0f * (gfloat) G_PI * hz / (gfloat) s->rate);
}

/* the iterations of a part of @length frames starting at @start */
static guint
part_iterations (Synth *s,
                 guint  start,
                 guint  length)
{
	if (start >= s->frames)
		return 0;
	return MIN (length, s->frames - start);
}

/* record mode: a layer of @type with the generator's state as its seed */
static Layer *
record_layer (Synth             *s,
              LrgFoleyLayerType  type,
              gdouble            at,
              gfloat             amp)
{
	Layer layer;

	layer_defaults (&layer, type);
	layer.p[P_AT] = at;
	layer.p[P_AMP] = (gdouble) amp;
	layer.p[P_SEED] = (gdouble) (s->state != 0 ? s->state : 0x9E3779B9u);
	g_array_append_val (s->record, layer);
	return &g_array_index (s->record, Layer, s->record->len - 1);
}

/*
 * add_body:
 * A damped sine: @amp at @hz, decaying by 1/e in @decay seconds, starting
 * @at seconds in; @sweep multiplies the frequency by that much over the
 * decay (a thud falling in pitch, a bubble rising).
 */
static void
add_body (Synth  *s,
          gdouble at,
          gfloat  hz,
          gfloat  sweep,
          gfloat  decay,
          gfloat  amp)
{
	guint   start = (guint) (at * s->rate);
	guint   length = (guint) (decay * 6.0f * (gfloat) s->rate);
	gdouble phase = 0.0;
	guint   i;

	if (s->record != NULL)
	{
		Layer *layer = record_layer (s, LRG_FOLEY_LAYER_BODY, at, amp);

		layer->p[BODY_FREQ] = hz;
		layer->p[BODY_SWEEP] = sweep;
		layer->p[BODY_DECAY] = decay;
		layer->p[P_SEED] = 0.0;   /* a body draws no noise */
		return;
	}
	for (i = 0; i < length && start + i < s->frames; i++)
	{
		gfloat t = (gfloat) i / (gfloat) s->rate;
		gfloat f = hz * powf (sweep, MIN (t / decay, 1.0f));

		phase += 2.0 * G_PI * f / s->rate;
		s->out[start + i] += amp * expf (-t / decay) * (gfloat) sin (phase);
	}
}

/*
 * add_grain:
 * A burst of band-limited noise (high-passed by subtracting a slow
 * low-pass, then low-passed at @hz): @amp, @length seconds with a
 * raised-cosine shape, starting @at seconds in.
 */
static void
add_grain (Synth  *s,
           gdouble at,
           gfloat  length,
           gfloat  low_hz,
           gfloat  high_hz,
           gfloat  amp)
{
	guint  start = (guint) (at * s->rate);
	guint  n = MAX ((guint) (length * (gfloat) s->rate), 2u);
	gfloat a_low = lp_coeff (s, low_hz);
	gfloat a_high = lp_coeff (s, high_hz);
	gfloat slow = 0.0f;
	gfloat fast = 0.0f;
	guint  i;

	if (s->record != NULL)
	{
		Layer *layer = record_layer (s, LRG_FOLEY_LAYER_GRAIN, at, amp);
		guint  draws = part_iterations (s, start, n);

		layer->p[GRAIN_LENGTH] = length;
		layer->p[GRAIN_LOW] = low_hz;
		layer->p[GRAIN_HIGH] = high_hz;
		for (i = 0; i < draws; i++)
			next_random (s);
		return;
	}
	for (i = 0; i < n && start + i < s->frames; i++)
	{
		gfloat x = noise (s);
		gfloat shape = 0.5f - 0.5f * cosf (2.0f * (gfloat) G_PI * (gfloat) i / (gfloat) (n - 1));

		slow += a_low * (x - slow);
		fast += a_high * (x - fast);
		s->out[start + i] += amp * shape * (fast - slow);
	}
}

/* low-passed noise held under an exponential decay, @at seconds in */
static void
add_wash (Synth  *s,
          gdouble at,
          gfloat  decay,
          gfloat  hz,
          gfloat  amp)
{
	guint  start = (guint) (at * s->rate);
	guint  length = (guint) (decay * 5.0f * (gfloat) s->rate);
	gfloat a = lp_coeff (s, hz);
	gfloat y = 0.0f;
	guint  i;

	if (s->record != NULL)
	{
		Layer *layer = record_layer (s, LRG_FOLEY_LAYER_WASH, at, amp);
		guint  draws = part_iterations (s, start, length);

		layer->p[WASH_DECAY] = decay;
		layer->p[WASH_CUTOFF] = hz;
		for (i = 0; i < draws; i++)
			next_random (s);
		return;
	}
	for (i = 0; i < length && start + i < s->frames; i++)
	{
		gfloat t = (gfloat) i / (gfloat) s->rate;

		y += a * (noise (s) - y);
		s->out[start + i] += amp * expf (-t / decay) * y * 3.0f;
	}
}

/*
 * add_bed:
 * A band of noise drifting in level over the whole buffer from @start,
 * with @count droplet ticks at random places (0.9's rain, parameterised;
 * the droplet expression is kept as it was so the draws happen in the
 * same order).
 */
static void
add_bed (Synth  *s,
         guint   start,
         gfloat  low_hz,
         gfloat  high_hz,
         gfloat  drift_hz,
         gfloat  base,
         gfloat  depth,
         gfloat  amp,
         guint   count)
{
	guint  i;
	gfloat slow = 0.0f;
	gfloat mid = 0.0f;
	gfloat a_slow = lp_coeff (s, low_hz);
	gfloat a_mid = lp_coeff (s, high_hz);
	gfloat drift = 0.0f;
	gfloat a_drift = lp_coeff (s, drift_hz);

	for (i = start; i < s->frames; i++)
	{
		gfloat x = noise (s);

		slow += a_slow * (x - slow);
		mid += a_mid * (x - mid);
		drift += a_drift * (noise (s) - drift);
		s->out[i] += (mid - slow) * (base + depth * drift) * amp;
	}
	for (i = 0; i < count; i++)
		add_grain (s, (gdouble) unit (s) * (gdouble) s->frames / (gdouble) s->rate, between (s, 0.002f, 0.006f),
		           2500.0f, 9000.0f, between (s, 0.15f, 0.5f) * amp);
}

/* an oscillator with an exponential pitch sweep, envelope and vibrato */
static void
add_tone (Synth         *s,
          const gdouble *p,
          gdouble        at,
          gdouble        amp)
{
	guint   start = (guint) (at * s->rate);
	gdouble length = p[TONE_LENGTH];
	guint   n = (guint) (length * s->rate);
	guint   wave = (guint) CLAMP (round (p[TONE_WAVEFORM]), 0.0, 3.0);
	guint   edge = MAX ((guint) (0.003 * s->rate), 1u);
	gdouble ratio = p[TONE_FREQ_END] / p[TONE_FREQ];
	gdouble phase = 0.0;
	guint   i;

	for (i = 0; i < n && start + i < s->frames; i++)
	{
		gdouble t = (gdouble) i / s->rate;
		gdouble f = p[TONE_FREQ] * pow (ratio, t / length);
		gdouble env = 1.0;
		gdouble w;

		if (p[TONE_VIB_DEPTH] > 0.0 && p[TONE_VIB_RATE] > 0.0)
			f *= pow (2.0, p[TONE_VIB_DEPTH] / 12.0 * sin (2.0 * G_PI * p[TONE_VIB_RATE] * t));
		phase += f / s->rate;
		phase -= floor (phase);
		switch (wave)
		{
		case 1: w = 1.0 - 4.0 * fabs (phase - 0.5); break;
		case 2: w = phase < p[TONE_DUTY] ? 1.0 : -1.0; break;
		case 3: w = 2.0 * phase - 1.0; break;
		default: w = sin (2.0 * G_PI * phase); break;
		}
		if (p[TONE_ATTACK] > 0.0 && t < p[TONE_ATTACK])
			env = t / p[TONE_ATTACK];
		if (p[TONE_DECAY] > 0.0)
			env *= exp (-t / p[TONE_DECAY]);
		if (n - i < edge)
			env *= (gdouble) (n - i) / (gdouble) edge;   /* no click at the end */
		s->out[start + i] += (gfloat) (amp * env * w);
	}
}

/* a noise burst through a sweeping one-pole low-pass and a high-pass */
static void
add_noise_burst (Synth         *s,
                 const gdouble *p,
                 gdouble        at,
                 gdouble        amp)
{
	guint   start = (guint) (at * s->rate);
	gdouble length = p[NOISE_LENGTH];
	guint   n = (guint) (length * s->rate);
	guint   edge = MAX ((guint) (0.003 * s->rate), 1u);
	gdouble ratio = p[NOISE_CUTOFF_END] / p[NOISE_CUTOFF];
	gdouble a_hp = p[NOISE_HIGHPASS] > 0.0 ? 1.0 - exp (-2.0 * G_PI * p[NOISE_HIGHPASS] / s->rate) : 0.0;
	gdouble y = 0.0;
	gdouble z = 0.0;
	guint   i;

	for (i = 0; i < n && start + i < s->frames; i++)
	{
		gdouble t = (gdouble) i / s->rate;
		gdouble fc = MIN (p[NOISE_CUTOFF] * pow (ratio, t / length), 0.45 * s->rate);
		gdouble a = 1.0 - exp (-2.0 * G_PI * fc / s->rate);
		gdouble env = 1.0;
		gdouble v;

		y += a * ((gdouble) noise (s) - y);
		v = y;
		if (a_hp > 0.0)
		{
			z += a_hp * (v - z);
			v -= z;
		}
		if (p[NOISE_ATTACK] > 0.0 && t < p[NOISE_ATTACK])
			env = t / p[NOISE_ATTACK];
		if (p[NOISE_DECAY] > 0.0)
			env *= exp (-t / p[NOISE_DECAY]);
		if (n - i < edge)
			env *= (gdouble) (n - i) / (gdouble) edge;
		s->out[start + i] += (gfloat) (amp * env * v * 2.0);
	}
}

/* ==========================================================================
 * The built-in kinds (0.9's recipes, unchanged)
 * ========================================================================== */

static void
make_dirt (Synth *s)
{
	add_body (s, 0.0, between (s, 70.0f, 110.0f), 0.6f, 0.035f, 1.0f);
	add_wash (s, 0.0, 0.03f, between (s, 700.0f, 1100.0f), 0.8f);
	add_grain (s, 0.01, 0.05f, 200.0f, 1800.0f, 0.25f);
}

static void
make_stone (Synth *s)
{
	gfloat ring = between (s, 1100.0f, 1700.0f);

	add_grain (s, 0.0, 0.008f, 800.0f, 6000.0f, 0.9f);
	add_body (s, 0.0, between (s, 150.0f, 230.0f), 0.7f, 0.045f, 0.9f);
	add_body (s, 0.0, ring, 1.0f, 0.05f, 0.18f);
	add_body (s, 0.0, ring * 1.51f, 1.0f, 0.035f, 0.1f);
}

static void
make_wood (Synth *s)
{
	gfloat f = between (s, 120.0f, 170.0f);

	add_grain (s, 0.0, 0.006f, 500.0f, 4000.0f, 0.5f);
	add_body (s, 0.0, f, 0.9f, 0.05f, 1.0f);
	add_body (s, 0.0, f * 2.3f, 0.95f, 0.04f, 0.5f);
	add_body (s, 0.0, f * 3.9f, 1.0f, 0.03f, 0.2f);
}

static void
make_grass (Synth *s)
{
	add_body (s, 0.0, between (s, 60.0f, 90.0f), 0.7f, 0.03f, 0.5f);
	add_grain (s, 0.0, 0.09f, 1200.0f, 5000.0f, 0.7f);
	add_grain (s, between (s, 0.05f, 0.09f), 0.08f, 1500.0f, 6000.0f, 0.4f);
}

static void
make_gravel (Synth *s)
{
	guint n = 7 + (guint) (unit (s) * 4.0f);
	guint i;

	add_body (s, 0.0, between (s, 80.0f, 120.0f), 0.7f, 0.03f, 0.35f);
	for (i = 0; i < n; i++)
		add_grain (s, (gdouble) (unit (s) * 0.18f), between (s, 0.006f, 0.018f), 1500.0f,
		           between (s, 4000.0f, 9000.0f), between (s, 0.35f, 0.8f));
}

static void
make_mud (Synth *s)
{
	add_body (s, 0.0, between (s, 90.0f, 130.0f), 0.45f, 0.06f, 0.9f);
	add_wash (s, 0.0, 0.07f, between (s, 350.0f, 600.0f), 1.0f);
	add_body (s, 0.14, between (s, 140.0f, 220.0f), 2.2f, 0.03f, 0.35f);
	add_wash (s, 0.13, 0.04f, 900.0f, 0.3f);
}

static void
make_snow (Synth *s)
{
	guint n = 9 + (guint) (unit (s) * 6.0f);
	guint i;

	add_body (s, 0.0, between (s, 60.0f, 90.0f), 0.7f, 0.025f, 0.25f);
	for (i = 0; i < n; i++)
		add_grain (s, (gdouble) (unit (s) * 0.17f), between (s, 0.008f, 0.02f), 2500.0f,
		           between (s, 6000.0f, 10000.0f), between (s, 0.2f, 0.55f));
}

static void
make_water (Synth *s)
{
	guint n = 3 + (guint) (unit (s) * 3.0f);
	guint i;

	add_wash (s, 0.0, 0.07f, 3500.0f, 0.9f);
	add_grain (s, 0.0, 0.12f, 400.0f, 3000.0f, 0.5f);
	for (i = 0; i < n; i++)
		add_body (s, (gdouble) (0.02f + unit (s) * 0.25f), between (s, 350.0f, 700.0f), 2.6f,
		          between (s, 0.012f, 0.03f), between (s, 0.25f, 0.5f));
}

static void
make_clink (Synth *s)
{
	gfloat base = between (s, 1700.0f, 2600.0f);
	guint  partial;

	add_grain (s, 0.0, 0.004f, 2000.0f, 9000.0f, 0.5f);
	/* inharmonic partials of a struck plate: ratios of a free bar */
	for (partial = 0; partial < 4; partial++)
	{
		static const gfloat ratio[4] = { 1.0f, 2.76f, 5.4f, 8.93f };

		add_body (s, 0.0, base * ratio[partial] * between (s, 0.98f, 1.02f), 1.0f,
		          between (s, 0.04f, 0.09f) / (1.0f + 0.5f * (gfloat) partial), 0.5f / (1.0f + (gfloat) partial));
	}
}

LrgFoleyPatch *
lrg_foley_patch_new_from_kind (LrgFoleyKind   kind,
                               guint          variant,
                               guint          sample_rate,
                               GError       **error)
{
	g_autoptr(LrgFoleyPatch) self = NULL;
	g_autoptr(GEnumClass)    klass = NULL;
	GEnumValue              *ev;
	Synth                    synth;
	gdouble                  duration;

	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	if (sample_rate == 0)
		sample_rate = LRG_FOLEY_DEFAULT_RATE;
	duration = lrg_foley_get_duration (kind);
	if (duration <= 0.0)
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "unknown foley kind %d", (gint) kind);
		return NULL;
	}
	if (sample_rate < 8000 || sample_rate > 96000)
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
		             "foley sample rate %u is outside 8000 .. 96000", sample_rate);
		return NULL;
	}

	self = lrg_foley_patch_new ();
	klass = g_type_class_ref (LRG_TYPE_FOLEY_KIND);
	ev = g_enum_get_value (klass, (gint) kind);
	self->name = g_strdup (ev != NULL ? ev->value_nick : NULL);
	self->duration = duration;
	self->rate = sample_rate;
	self->seed = variant;
	self->loop = kind == LRG_FOLEY_RAIN_LOOP;

	/* the generator as 0.9 seeded it: kind and variant mixed, two steps */
	memset (&synth, 0, sizeof synth);
	synth.rate = sample_rate;
	synth.frames = (guint) (duration * sample_rate) + (self->loop ? sample_rate / 4 : 0);
	synth.state = ((guint32) kind + 1u) * 2654435761u ^ (variant + 1u) * 2246822519u;
	next_random (&synth);
	next_random (&synth);
	synth.record = self->layers;

	switch (kind)
	{
	case LRG_FOLEY_FOOTSTEP_DIRT: make_dirt (&synth); break;
	case LRG_FOLEY_FOOTSTEP_STONE: make_stone (&synth); break;
	case LRG_FOLEY_FOOTSTEP_WOOD: make_wood (&synth); break;
	case LRG_FOLEY_FOOTSTEP_GRASS: make_grass (&synth); break;
	case LRG_FOLEY_FOOTSTEP_GRAVEL: make_gravel (&synth); break;
	case LRG_FOLEY_FOOTSTEP_MUD: make_mud (&synth); break;
	case LRG_FOLEY_FOOTSTEP_SNOW: make_snow (&synth); break;
	case LRG_FOLEY_FOOTSTEP_WATER: make_water (&synth); break;
	case LRG_FOLEY_ARMOR_CLINK: make_clink (&synth); break;
	case LRG_FOLEY_RAIN_LOOP:
	default:
		{
			/* the rain is one bed layer; its droplets are 0.9's one per
			 * 600 frames of the playing length */
			Layer *layer = record_layer (&synth, LRG_FOLEY_LAYER_BED, 0.0, 1.0f);

			layer->p[BED_DROPLETS] = (gdouble) sample_rate / 600.0;
		}
		break;
	}
	return g_steal_pointer (&self);
}

/* ==========================================================================
 * Rendering
 * ========================================================================== */

/* the noise state of layer @index: its seed, or one derived from the patch */
static guint32
layer_state (LrgFoleyPatch *self,
             const Layer   *layer,
             guint          index)
{
	Synth   tmp;
	guint32 seed = (guint32) layer->p[P_SEED];

	if (seed != 0)
		return seed;
	tmp.state = (self->seed + 1u) * 2654435761u ^ (index + 1u) * 2246822519u;
	next_random (&tmp);
	next_random (&tmp);
	return tmp.state != 0 ? tmp.state : 0x9E3779B9u;
}

/* one copy of @layer starting @at seconds, scaled by @amp and the jitter */
static void
render_copy (Synth       *s,
             const Layer *layer,
             gdouble      at,
             gdouble      amp,
             gdouble      pitch,
             gdouble      stretch,
             guint        playing)
{
	gdouble p[MAX_PARAMS];

	memcpy (p, layer->p, sizeof p);
	switch (layer->type)
	{
	case LRG_FOLEY_LAYER_BODY:
		add_body (s, at, (gfloat) (p[BODY_FREQ] * pitch), (gfloat) p[BODY_SWEEP], (gfloat) (p[BODY_DECAY] * stretch),
		          (gfloat) amp);
		break;
	case LRG_FOLEY_LAYER_GRAIN:
		add_grain (s, at, (gfloat) (p[GRAIN_LENGTH] * stretch), (gfloat) (p[GRAIN_LOW] * pitch),
		           (gfloat) (p[GRAIN_HIGH] * pitch), (gfloat) amp);
		break;
	case LRG_FOLEY_LAYER_WASH:
		add_wash (s, at, (gfloat) (p[WASH_DECAY] * stretch), (gfloat) (p[WASH_CUTOFF] * pitch), (gfloat) amp);
		break;
	case LRG_FOLEY_LAYER_BED:
		add_bed (s, (guint) (at * s->rate), (gfloat) p[BED_LOW], (gfloat) p[BED_HIGH], (gfloat) p[BED_DRIFT],
		         (gfloat) p[BED_BASE], (gfloat) p[BED_DEPTH], (gfloat) amp,
		         (guint) floor ((gdouble) playing * p[BED_DROPLETS] / (gdouble) s->rate + 1e-7));
		break;
	case LRG_FOLEY_LAYER_TONE:
		p[TONE_FREQ] = CLAMP (p[TONE_FREQ] * pitch, 10.0, 20000.0);
		p[TONE_FREQ_END] = CLAMP (p[TONE_FREQ_END] * pitch, 10.0, 20000.0);
		p[TONE_LENGTH] *= stretch;
		p[TONE_DECAY] *= stretch;
		add_tone (s, p, at, amp);
		break;
	case LRG_FOLEY_LAYER_NOISE:
	default:
		p[NOISE_CUTOFF] = CLAMP (p[NOISE_CUTOFF] * pitch, 10.0, 20000.0);
		p[NOISE_CUTOFF_END] = CLAMP (p[NOISE_CUTOFF_END] * pitch, 10.0, 20000.0);
		p[NOISE_LENGTH] *= stretch;
		p[NOISE_DECAY] *= stretch;
		add_noise_burst (s, p, at, amp);
		break;
	}
}

gfloat *
lrg_foley_patch_render (LrgFoleyPatch  *self,
                        gsize          *n_frames,
                        GError        **error)
{
	Synth   synth;
	guint   playing;
	guint   extra;
	guint   i;
	gfloat  peak;
	gfloat  target;
	gfloat *result;

	g_return_val_if_fail (LRG_IS_FOLEY_PATCH (self), NULL);
	g_return_val_if_fail (n_frames != NULL, NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	*n_frames = 0;
	playing = (guint) (self->duration * self->rate);
	if (playing == 0)
	{
		g_set_error (error, LRG_FOLEY_PATCH_ERROR, LRG_FOLEY_PATCH_ERROR_RENDER, "the patch is too short");
		return NULL;
	}
	extra = self->loop ? (guint) (self->loop_tail * self->rate) : 0;
	extra = MIN (extra, playing);
	memset (&synth, 0, sizeof synth);
	synth.rate = self->rate;
	synth.frames = playing + extra;
	synth.out = g_new0 (gfloat, synth.frames);

	/* 1. every layer, from its own noise state */
	for (i = 0; i < self->layers->len; i++)
	{
		const Layer *layer = &g_array_index (self->layers, Layer, i);
		guint        copies = (guint) CLAMP (round (layer->p[P_COUNT]), 1.0, 64.0);
		gdouble      jitter = layer->p[P_JITTER];
		Synth        scatter;
		guint        k;

		if (layer->p[P_MUTE] >= 0.5)
			continue;
		synth.state = layer_state (self, layer, i);
		scatter.state = (synth.state ^ 0x5BD1E995u) | 1u;
		for (k = 0; k < copies; k++)
		{
			gdouble at = layer->p[P_AT];
			gdouble amp = layer->p[P_AMP];
			gdouble pitch = 1.0;
			gdouble stretch = 1.0;

			if (k > 0 && layer->p[P_SPREAD] > 0.0)
				at += layer->p[P_SPREAD] * (gdouble) unit (&scatter);
			if (jitter > 0.0)
			{
				amp *= 1.0 - 0.5 * jitter * (gdouble) unit (&scatter);
				pitch = pow (2.0, jitter * ((gdouble) unit (&scatter) * 2.0 - 1.0));
				stretch = 1.0 + jitter * ((gdouble) unit (&scatter) - 0.5);
			}
			if (layer->type == LRG_FOLEY_LAYER_BODY || layer->type == LRG_FOLEY_LAYER_GRAIN
			    || layer->type == LRG_FOLEY_LAYER_WASH)
				render_copy (&synth, layer, at, (gdouble) (gfloat) amp, pitch, stretch, playing);
			else
				render_copy (&synth, layer, at, amp, pitch, stretch, playing);
		}
	}

	/* 2. a loop: the tail fades out over the head fading in */
	for (i = 0; i < extra; i++)
	{
		gfloat t = (gfloat) i / (gfloat) extra;

		synth.out[i] = synth.out[i] * t + synth.out[playing + i] * (1.0f - t);
	}

	/* 3. short fades (not for a loop, whose ends meet) */
	if (!self->loop && self->fade > 0.0)
	{
		guint fade = MAX ((guint) (self->fade * self->rate), 1u);

		for (i = 0; i < fade && i < playing; i++)
		{
			gfloat g = (gfloat) i / (gfloat) fade;

			synth.out[i] *= g;
			synth.out[playing - 1 - i] *= g;
		}
	}

	/* 4. the peak */
	peak = 0.0f;
	for (i = 0; i < playing; i++)
		peak = MAX (peak, fabsf (synth.out[i]));
	if (peak < 1e-6f)
		peak = 1.0f;
	target = (gfloat) self->peak;
	result = g_new (gfloat, playing);
	for (i = 0; i < playing; i++)
	{
		if (self->normalize)
			result[i] = CLAMP (synth.out[i] / peak * target, -1.0f, 1.0f);
		else
			result[i] = CLAMP (synth.out[i], -1.0f, 1.0f);
	}
	g_free (synth.out);
	*n_frames = playing;
	return result;
}

LrgWaveData *
lrg_foley_patch_render_wave (LrgFoleyPatch  *self,
                             GError        **error)
{
	g_autofree gfloat *samples = NULL;
	gsize              n = 0;

	g_return_val_if_fail (LRG_IS_FOLEY_PATCH (self), NULL);

	samples = lrg_foley_patch_render (self, &n, error);
	if (samples == NULL)
		return NULL;
	return lrg_wave_data_new_from_samples (self->rate, 32, 1, (const guint8 *) samples, n * sizeof (gfloat));
}

static void
put16 (guint8  *p,
       guint16  v)
{
	p[0] = (guint8) (v & 0xFF);
	p[1] = (guint8) (v >> 8);
}

static void
put32 (guint8  *p,
       guint32  v)
{
	put16 (p, (guint16) (v & 0xFFFF));
	put16 (p + 2, (guint16) (v >> 16));
}

GBytes *
lrg_foley_patch_render_wav (LrgFoleyPatch  *self,
                            GError        **error)
{
	g_autofree gfloat *samples = NULL;
	gsize              n = 0;
	gsize              i;
	gsize              data_bytes;
	guint8            *wav;

	g_return_val_if_fail (LRG_IS_FOLEY_PATCH (self), NULL);

	samples = lrg_foley_patch_render (self, &n, error);
	if (samples == NULL)
		return NULL;
	data_bytes = n * 2;
	wav = g_malloc0 (44 + data_bytes);
	memcpy (wav, "RIFF", 4);
	put32 (wav + 4, (guint32) (36 + data_bytes));
	memcpy (wav + 8, "WAVEfmt ", 8);
	put32 (wav + 16, 16);
	put16 (wav + 20, 1);
	put16 (wav + 22, 1);
	put32 (wav + 24, self->rate);
	put32 (wav + 28, self->rate * 2);
	put16 (wav + 32, 2);
	put16 (wav + 34, 16);
	memcpy (wav + 36, "data", 4);
	put32 (wav + 40, (guint32) data_bytes);
	for (i = 0; i < n; i++)
		put16 (wav + 44 + i * 2, (guint16) (gint16) lrintf (samples[i] * 32767.0f));
	return g_bytes_new_take (wav, 44 + data_bytes);
}

/* ==========================================================================
 * Settings and layers
 * ========================================================================== */

gdouble
lrg_foley_patch_get_duration (LrgFoleyPatch *self)
{
	g_return_val_if_fail (LRG_IS_FOLEY_PATCH (self), 0.0);
	return self->duration;
}

guint
lrg_foley_patch_get_sample_rate (LrgFoleyPatch *self)
{
	g_return_val_if_fail (LRG_IS_FOLEY_PATCH (self), 0);
	return self->rate;
}

const gchar *
lrg_foley_patch_get_name (LrgFoleyPatch *self)
{
	g_return_val_if_fail (LRG_IS_FOLEY_PATCH (self), NULL);
	return self->name;
}

gboolean
lrg_foley_patch_set_global (LrgFoleyPatch *self,
                            const gchar   *name,
                            gdouble        value)
{
	g_return_val_if_fail (LRG_IS_FOLEY_PATCH (self), FALSE);

	if (name == NULL || !isfinite (value))
		return FALSE;
	if (g_str_equal (name, "duration"))
		self->duration = CLAMP (value, 0.01, LRG_FOLEY_PATCH_MAX_DURATION);
	else if (g_str_equal (name, "sample-rate") || g_str_equal (name, "sample_rate"))
		self->rate = (guint) CLAMP (round (value), 8000.0, 96000.0);
	else if (g_str_equal (name, "seed"))
		self->seed = (guint32) CLAMP (round (value), 0.0, 4294967295.0);
	else if (g_str_equal (name, "loop"))
		self->loop = value != 0.0;
	else if (g_str_equal (name, "loop-tail") || g_str_equal (name, "loop_tail"))
		self->loop_tail = CLAMP (value, 0.0, 10.0);
	else if (g_str_equal (name, "fade"))
		self->fade = CLAMP (value, 0.0, 1.0);
	else if (g_str_equal (name, "peak"))
		self->peak = CLAMP (value, 0.01, 1.0);
	else if (g_str_equal (name, "normalize"))
		self->normalize = value != 0.0;
	else
		return FALSE;
	changed (self);
	return TRUE;
}

guint
lrg_foley_patch_get_n_layers (LrgFoleyPatch *self)
{
	g_return_val_if_fail (LRG_IS_FOLEY_PATCH (self), 0);
	return self->layers->len;
}

gint
lrg_foley_patch_add_layer (LrgFoleyPatch     *self,
                           LrgFoleyLayerType  type)
{
	Layer layer;

	g_return_val_if_fail (LRG_IS_FOLEY_PATCH (self), -1);

	if (!valid_type (type) || self->layers->len >= LRG_FOLEY_PATCH_MAX_LAYERS)
		return -1;
	layer_defaults (&layer, type);
	g_array_append_val (self->layers, layer);
	changed (self);
	return (gint) self->layers->len - 1;
}

gboolean
lrg_foley_patch_remove_layer (LrgFoleyPatch *self,
                              guint          index)
{
	g_return_val_if_fail (LRG_IS_FOLEY_PATCH (self), FALSE);

	if (index >= self->layers->len)
		return FALSE;
	g_array_remove_index (self->layers, index);
	changed (self);
	return TRUE;
}

gboolean
lrg_foley_patch_move_layer (LrgFoleyPatch *self,
                            guint          from,
                            guint          to)
{
	Layer layer;

	g_return_val_if_fail (LRG_IS_FOLEY_PATCH (self), FALSE);

	if (from >= self->layers->len || to >= self->layers->len)
		return FALSE;
	if (from == to)
		return TRUE;
	layer = g_array_index (self->layers, Layer, from);
	g_array_remove_index (self->layers, from);
	g_array_insert_val (self->layers, to, layer);
	changed (self);
	return TRUE;
}

LrgFoleyLayerType
lrg_foley_patch_get_layer_type (LrgFoleyPatch *self,
                                guint          index)
{
	g_return_val_if_fail (LRG_IS_FOLEY_PATCH (self), LRG_FOLEY_LAYER_BODY);

	if (index >= self->layers->len)
		return LRG_FOLEY_LAYER_BODY;
	return g_array_index (self->layers, Layer, index).type;
}

gboolean
lrg_foley_patch_set_layer_param (LrgFoleyPatch *self,
                                 guint          index,
                                 const gchar   *name,
                                 gdouble        value)
{
	Layer          *layer;
	const ParamDef *def;
	gint            p;

	g_return_val_if_fail (LRG_IS_FOLEY_PATCH (self), FALSE);

	if (index >= self->layers->len || !isfinite (value))
		return FALSE;
	layer = &g_array_index (self->layers, Layer, index);
	p = param_index (layer->type, name);
	if (p < 0)
		return FALSE;
	def = param_at (layer->type, (guint) p);
	layer->p[p] = CLAMP (value, def->min, def->max);
	if (p == P_COUNT || p == P_MUTE || p == P_SEED || (layer->type == LRG_FOLEY_LAYER_TONE && p == TONE_WAVEFORM))
		layer->p[p] = round (layer->p[p]);
	changed (self);
	return TRUE;
}

gdouble
lrg_foley_patch_get_layer_param (LrgFoleyPatch *self,
                                 guint          index,
                                 const gchar   *name)
{
	const Layer *layer;
	gint         p;

	g_return_val_if_fail (LRG_IS_FOLEY_PATCH (self), NAN);

	if (index >= self->layers->len)
		return NAN;
	layer = &g_array_index (self->layers, Layer, index);
	p = param_index (layer->type, name);
	return p < 0 ? NAN : layer->p[p];
}

gchar **
lrg_foley_layer_type_list_params (LrgFoleyLayerType type)
{
	GPtrArray *names = g_ptr_array_new ();
	guint      i, n;

	if (valid_type (type))
	{
		n = param_count (type);
		for (i = 0; i < n; i++)
			g_ptr_array_add (names, g_strdup (param_at (type, i)->name));
	}
	g_ptr_array_add (names, NULL);
	return (gchar **) g_ptr_array_free (names, FALSE);
}

gboolean
lrg_foley_layer_type_get_param_info (LrgFoleyLayerType   type,
                                     const gchar        *name,
                                     gdouble            *out_min,
                                     gdouble            *out_max,
                                     gdouble            *out_default,
                                     const gchar       **out_blurb)
{
	const ParamDef *def;
	gint            p;

	if (!valid_type (type))
		return FALSE;
	p = param_index (type, name);
	if (p < 0)
		return FALSE;
	def = param_at (type, (guint) p);
	if (out_min != NULL)
		*out_min = def->min;
	if (out_max != NULL)
		*out_max = def->max;
	if (out_default != NULL)
		*out_default = def->def;
	if (out_blurb != NULL)
		*out_blurb = def->blurb;
	return TRUE;
}

/* ==========================================================================
 * .rfoley files
 *
 *   version: 1
 *   foley:
 *     name: laser
 *     duration: 0.5
 *     sample_rate: 44100
 *     seed: 1
 *     loop: false
 *     loop_tail: 0.25
 *     fade: 0.004
 *     peak: 0.8
 *     normalize: true
 *   layers:
 *     - type: tone
 *       freq: 1800
 *       freq-end: 200
 * ========================================================================== */

static gboolean
read_layer (LrgAudioYamlCtx *ctx,
            LrgFoleyPatch   *self,
            YamlNode        *node,
            guint            index)
{
	g_autofree gchar     *path = g_strdup_printf ("layers[%u]", index);
	g_autoptr(GEnumClass) klass = g_type_class_ref (LRG_TYPE_FOLEY_LAYER_TYPE);
	g_autoptr(GList)      keys = NULL;
	YamlMapping          *map = lrg_audio_yaml_map (ctx, node, path);
	const gchar          *type_name = NULL;
	GEnumValue           *ev;
	Layer                 layer;
	GList                *l;

	if (map == NULL)
		return FALSE;
	{
		g_autofree gchar *tp = g_strconcat (path, ".type", NULL);

		if (!lrg_audio_yaml_string (ctx, yaml_mapping_get_member (map, "type"), tp, &type_name))
			return FALSE;
		ev = g_enum_get_value_by_nick (klass, type_name);
		if (ev == NULL)
			return lrg_audio_yaml_fail (ctx, tp, "unknown layer type '%s' (body, grain, wash, bed, tone, noise)",
			                            type_name);
	}
	layer_defaults (&layer, (LrgFoleyLayerType) ev->value);
	keys = yaml_mapping_get_members (map);
	for (l = keys; l != NULL; l = l->next)
	{
		const gchar      *key = l->data;
		g_autofree gchar *kp = g_strconcat (path, ".", key, NULL);
		const ParamDef   *def;
		gint              p;

		if (g_str_equal (key, "type"))
			continue;
		p = param_index (layer.type, key);
		if (p < 0)
			return lrg_audio_yaml_fail (ctx, kp, "'%s' is not a parameter of a %s layer", key, type_name);
		def = param_at (layer.type, (guint) p);
		if (!lrg_audio_yaml_number (ctx, yaml_mapping_get_member (map, key), kp, def->min, def->max, &layer.p[p]))
			return FALSE;
	}
	g_array_append_val (self->layers, layer);
	return TRUE;
}

static gboolean
read_patch (LrgAudioYamlCtx *ctx,
            LrgFoleyPatch   *self,
            YamlNode        *root)
{
	YamlMapping *map = lrg_audio_yaml_map (ctx, root, "");
	YamlNode    *node;

	if (map == NULL)
		return FALSE;
	node = yaml_mapping_get_member (map, "version");
	if (node != NULL)
	{
		gint64 v = 0;

		if (!lrg_audio_yaml_int (ctx, node, "version", 1, LRG_FOLEY_PATCH_FORMAT_VERSION, &v))
			return FALSE;
	}
	node = yaml_mapping_get_member (map, "foley");
	if (node != NULL)
	{
		YamlMapping *g = lrg_audio_yaml_map (ctx, node, "foley");
		const gchar *text = NULL;
		gint64       iv = 0;

		if (g == NULL)
			return FALSE;
		if ((node = yaml_mapping_get_member (g, "name")) != NULL)
		{
			if (!lrg_audio_yaml_string (ctx, node, "foley.name", &text))
				return FALSE;
			g_free (self->name);
			self->name = g_strdup (text);
		}
		if ((node = yaml_mapping_get_member (g, "duration")) != NULL
		    && !lrg_audio_yaml_number (ctx, node, "foley.duration", 0.01, LRG_FOLEY_PATCH_MAX_DURATION,
		                               &self->duration))
			return FALSE;
		if ((node = yaml_mapping_get_member (g, "sample_rate")) != NULL)
		{
			if (!lrg_audio_yaml_int (ctx, node, "foley.sample_rate", 8000, 96000, &iv))
				return FALSE;
			self->rate = (guint) iv;
		}
		if ((node = yaml_mapping_get_member (g, "seed")) != NULL)
		{
			if (!lrg_audio_yaml_int (ctx, node, "foley.seed", 0, G_MAXUINT32, &iv))
				return FALSE;
			self->seed = (guint32) iv;
		}
		if ((node = yaml_mapping_get_member (g, "loop")) != NULL
		    && !lrg_audio_yaml_bool (ctx, node, "foley.loop", &self->loop))
			return FALSE;
		if ((node = yaml_mapping_get_member (g, "loop_tail")) != NULL
		    && !lrg_audio_yaml_number (ctx, node, "foley.loop_tail", 0.0, 10.0, &self->loop_tail))
			return FALSE;
		if ((node = yaml_mapping_get_member (g, "fade")) != NULL
		    && !lrg_audio_yaml_number (ctx, node, "foley.fade", 0.0, 1.0, &self->fade))
			return FALSE;
		if ((node = yaml_mapping_get_member (g, "peak")) != NULL
		    && !lrg_audio_yaml_number (ctx, node, "foley.peak", 0.01, 1.0, &self->peak))
			return FALSE;
		if ((node = yaml_mapping_get_member (g, "normalize")) != NULL
		    && !lrg_audio_yaml_bool (ctx, node, "foley.normalize", &self->normalize))
			return FALSE;
	}
	node = yaml_mapping_get_member (map, "layers");
	if (node != NULL && yaml_node_get_node_type (node) != YAML_NODE_NULL)
	{
		YamlSequence *seq = lrg_audio_yaml_seq (ctx, node, "layers");
		guint         i, n;

		if (seq == NULL)
			return FALSE;
		n = yaml_sequence_get_length (seq);
		if (n > LRG_FOLEY_PATCH_MAX_LAYERS)
			return lrg_audio_yaml_fail (ctx, "layers", "more than %d layers", LRG_FOLEY_PATCH_MAX_LAYERS);
		for (i = 0; i < n; i++)
			if (!read_layer (ctx, self, yaml_sequence_get_element (seq, i), i))
				return FALSE;
	}
	return TRUE;
}

LrgFoleyPatch *
lrg_foley_patch_new_from_data (const gchar  *data,
                               gssize        length,
                               GError      **error)
{
	g_autoptr(LrgFoleyPatch) self = NULL;
	g_autoptr(YamlParser)    parser = NULL;
	LrgAudioYamlCtx          ctx = { "<data>", 0, LRG_FOLEY_PATCH_ERROR_PARSE,
	                                 LRG_FOLEY_PATCH_ERROR_INVALID_VALUE, error };
	YamlNode                *root;

	g_return_val_if_fail (data != NULL, NULL);

	ctx.domain = LRG_FOLEY_PATCH_ERROR;
	root = lrg_audio_yaml_parse (&ctx, data, length, &parser);
	if (root == NULL)
		return NULL;
	self = lrg_foley_patch_new ();
	if (!read_patch (&ctx, self, root))
		return NULL;
	return g_steal_pointer (&self);
}

LrgFoleyPatch *
lrg_foley_patch_new_from_file (const gchar  *path,
                               GError      **error)
{
	g_autoptr(LrgFoleyPatch) self = NULL;
	g_autoptr(YamlParser)    parser = NULL;
	g_autoptr(GError)        local = NULL;
	g_autofree gchar        *text = NULL;
	gsize                    length = 0;
	LrgAudioYamlCtx          ctx = { path, 0, LRG_FOLEY_PATCH_ERROR_PARSE,
	                                 LRG_FOLEY_PATCH_ERROR_INVALID_VALUE, error };
	YamlNode                *root;

	g_return_val_if_fail (path != NULL, NULL);

	ctx.domain = LRG_FOLEY_PATCH_ERROR;
	if (!g_file_get_contents (path, &text, &length, &local))
	{
		g_set_error (error, LRG_FOLEY_PATCH_ERROR, LRG_FOLEY_PATCH_ERROR_IO, "%s: %s", path, local->message);
		return NULL;
	}
	root = lrg_audio_yaml_parse (&ctx, text, (gssize) length, &parser);
	if (root == NULL)
		return NULL;
	self = lrg_foley_patch_new ();
	if (!read_patch (&ctx, self, root))
		return NULL;
	return g_steal_pointer (&self);
}

gchar *
lrg_foley_patch_to_data (LrgFoleyPatch  *self,
                         gsize          *length,
                         GError        **error)
{
	g_autoptr(YamlNode) root = NULL;
	g_autoptr(GEnumClass) klass = g_type_class_ref (LRG_TYPE_FOLEY_LAYER_TYPE);
	YamlMapping        *map;
	YamlMapping        *g;
	YamlSequence       *seq;
	guint               i, j;

	g_return_val_if_fail (LRG_IS_FOLEY_PATCH (self), NULL);

	root = lrg_audio_yaml_new_map (&map);
	lrg_audio_yaml_set (map, "version", lrg_audio_yaml_int_node (LRG_FOLEY_PATCH_FORMAT_VERSION));
	lrg_audio_yaml_set (map, "foley", lrg_audio_yaml_new_map (&g));
	if (self->name != NULL)
		lrg_audio_yaml_set (g, "name", lrg_audio_yaml_str (self->name));
	lrg_audio_yaml_set (g, "duration", lrg_audio_yaml_num (self->duration));
	lrg_audio_yaml_set (g, "sample_rate", lrg_audio_yaml_int_node (self->rate));
	lrg_audio_yaml_set (g, "seed", lrg_audio_yaml_int_node (self->seed));
	lrg_audio_yaml_set (g, "loop", lrg_audio_yaml_bool_node (self->loop));
	if (self->loop)
		lrg_audio_yaml_set (g, "loop_tail", lrg_audio_yaml_num (self->loop_tail));
	lrg_audio_yaml_set (g, "fade", lrg_audio_yaml_num (self->fade));
	lrg_audio_yaml_set (g, "peak", lrg_audio_yaml_num (self->peak));
	lrg_audio_yaml_set (g, "normalize", lrg_audio_yaml_bool_node (self->normalize));

	lrg_audio_yaml_set (map, "layers", lrg_audio_yaml_new_seq (&seq));
	for (i = 0; i < self->layers->len; i++)
	{
		const Layer *layer = &g_array_index (self->layers, Layer, i);
		YamlMapping *lm;
		YamlNode    *ln = lrg_audio_yaml_new_map (&lm);
		guint        n = param_count (layer->type);

		lrg_audio_yaml_set (lm, "type", lrg_audio_yaml_str (g_enum_get_value (klass, layer->type)->value_nick));
		for (j = 0; j < n; j++)
		{
			const ParamDef *def = param_at (layer->type, j);

			if (layer->p[j] == def->def)
				continue;
			lrg_audio_yaml_set (lm, def->name, lrg_audio_yaml_num (layer->p[j]));
		}
		lrg_audio_yaml_append (seq, ln);
	}
	return lrg_audio_yaml_to_data (root, length, error);
}

gboolean
lrg_foley_patch_save_to_file (LrgFoleyPatch  *self,
                              const gchar    *path,
                              GError        **error)
{
	g_autofree gchar *text = NULL;
	g_autoptr(GError) local = NULL;
	gsize             length = 0;

	g_return_val_if_fail (LRG_IS_FOLEY_PATCH (self), FALSE);
	g_return_val_if_fail (path != NULL, FALSE);

	text = lrg_foley_patch_to_data (self, &length, error);
	if (text == NULL)
		return FALSE;
	if (!g_file_set_contents (path, text, (gssize) length, &local))
	{
		g_set_error (error, LRG_FOLEY_PATCH_ERROR, LRG_FOLEY_PATCH_ERROR_IO, "%s: %s", path, local->message);
		return FALSE;
	}
	return TRUE;
}
