/* lrg-sprite-animation.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Frame animation clips, sprite sheet slicing, the .rsprite format and
 * the LrgSpriteAnimationPlayer component.
 *
 * The .rsprite state that LrgSpriteSheet itself does not hold (the grid
 * the first frames came from, the clips, unknown keys of a loaded file)
 * is kept on the sheet as qdata (SheetData below).
 */

#include "config.h"

#include "lrg-sprite-animation.h"
#include "../ecs/lrg-game-object.h"
#include "../ecs/components/lrg-sprite-component.h"
#include "../core/lrg-data-loader.h"

#include <yaml-glib.h>
#include <math.h>
#include <string.h>

/* ==========================================================================
 * Enums and errors
 * ========================================================================== */

G_DEFINE_QUARK (lrg-sprite-animation-error-quark, lrg_sprite_animation_error)

GType
lrg_sprite_animation_error_get_type (void)
{
	static gsize type_id = 0;

	if (g_once_init_enter (&type_id))
	{
		static const GEnumValue values[] = {
			{ LRG_SPRITE_ANIMATION_ERROR_IO, "LRG_SPRITE_ANIMATION_ERROR_IO", "io" },
			{ LRG_SPRITE_ANIMATION_ERROR_PARSE, "LRG_SPRITE_ANIMATION_ERROR_PARSE", "parse" },
			{ LRG_SPRITE_ANIMATION_ERROR_INVALID, "LRG_SPRITE_ANIMATION_ERROR_INVALID", "invalid" },
			{ 0, NULL, NULL }
		};
		GType id = g_enum_register_static (g_intern_static_string ("LrgSpriteAnimationError"), values);

		g_once_init_leave (&type_id, id);
	}
	return (GType) type_id;
}

/* nicks double as the .rsprite `loop:` vocabulary */
static const GEnumValue loop_values[] = {
	{ LRG_SPRITE_LOOP_ONCE, "LRG_SPRITE_LOOP_ONCE", "once" },
	{ LRG_SPRITE_LOOP_LOOP, "LRG_SPRITE_LOOP_LOOP", "loop" },
	{ LRG_SPRITE_LOOP_PING_PONG, "LRG_SPRITE_LOOP_PING_PONG", "ping-pong" },
	{ 0, NULL, NULL }
};

GType
lrg_sprite_loop_mode_get_type (void)
{
	static gsize type_id = 0;

	if (g_once_init_enter (&type_id))
	{
		GType id = g_enum_register_static (g_intern_static_string ("LrgSpriteLoopMode"), loop_values);

		g_once_init_leave (&type_id, id);
	}
	return (GType) type_id;
}

/* ==========================================================================
 * LrgSpriteAnimation
 * ========================================================================== */

typedef struct
{
	guint  frame;      /* sheet frame index */
	gfloat duration;   /* seconds, 0: 1 / fps */
} ClipFrame;

typedef struct
{
	guint  frame;      /* clip position */
	gchar *name;
} ClipEvent;

struct _LrgSpriteAnimation
{
	GObject            parent_instance;

	gchar             *name;
	gfloat             fps;
	LrgSpriteLoopMode  loop_mode;
	GArray            *frames;   /* ClipFrame */
	GArray            *events;   /* ClipEvent, sorted by frame (stable) */
	YamlMapping       *extra;    /* unknown keys of the loaded clip */
};

G_DEFINE_FINAL_TYPE (LrgSpriteAnimation, lrg_sprite_animation, G_TYPE_OBJECT)

enum
{
	ANIM_PROP_0,
	ANIM_PROP_NAME,
	ANIM_PROP_FPS,
	ANIM_PROP_LOOP_MODE,
	ANIM_N_PROPS
};

static GParamSpec *anim_properties[ANIM_N_PROPS];

enum
{
	ANIM_SIGNAL_CHANGED,
	ANIM_N_SIGNALS
};

static guint anim_signals[ANIM_N_SIGNALS];

static void
clip_event_clear (gpointer data)
{
	ClipEvent *ev = data;

	g_free (ev->name);
}

static void
lrg_sprite_animation_finalize (GObject *object)
{
	LrgSpriteAnimation *self = LRG_SPRITE_ANIMATION (object);

	g_free (self->name);
	g_array_unref (self->frames);
	g_array_unref (self->events);
	if (self->extra != NULL)
		yaml_mapping_unref (self->extra);

	G_OBJECT_CLASS (lrg_sprite_animation_parent_class)->finalize (object);
}

static void
lrg_sprite_animation_get_property (GObject    *object,
                                   guint       prop_id,
                                   GValue     *value,
                                   GParamSpec *pspec)
{
	LrgSpriteAnimation *self = LRG_SPRITE_ANIMATION (object);

	switch (prop_id)
	{
	case ANIM_PROP_NAME:
		g_value_set_string (value, self->name);
		break;
	case ANIM_PROP_FPS:
		g_value_set_float (value, self->fps);
		break;
	case ANIM_PROP_LOOP_MODE:
		g_value_set_enum (value, self->loop_mode);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_sprite_animation_set_property (GObject      *object,
                                   guint         prop_id,
                                   const GValue *value,
                                   GParamSpec   *pspec)
{
	LrgSpriteAnimation *self = LRG_SPRITE_ANIMATION (object);

	switch (prop_id)
	{
	case ANIM_PROP_NAME:
		lrg_sprite_animation_set_name (self, g_value_get_string (value));
		break;
	case ANIM_PROP_FPS:
		lrg_sprite_animation_set_fps (self, g_value_get_float (value));
		break;
	case ANIM_PROP_LOOP_MODE:
		lrg_sprite_animation_set_loop_mode (self, g_value_get_enum (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_sprite_animation_class_init (LrgSpriteAnimationClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = lrg_sprite_animation_finalize;
	object_class->get_property = lrg_sprite_animation_get_property;
	object_class->set_property = lrg_sprite_animation_set_property;

	/**
	 * LrgSpriteAnimation:name:
	 *
	 * The clip's name, unique within its sheet.
	 */
	anim_properties[ANIM_PROP_NAME] =
		g_param_spec_string ("name", NULL, NULL, NULL,
		                     G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgSpriteAnimation:fps:
	 *
	 * Frames per second for frames without their own duration.
	 */
	anim_properties[ANIM_PROP_FPS] =
		g_param_spec_float ("fps", NULL, NULL, 0.001f, 1000.0f, 10.0f,
		                    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgSpriteAnimation:loop-mode:
	 *
	 * What happens after the last frame.
	 */
	anim_properties[ANIM_PROP_LOOP_MODE] =
		g_param_spec_enum ("loop-mode", NULL, NULL, LRG_TYPE_SPRITE_LOOP_MODE, LRG_SPRITE_LOOP_LOOP,
		                   G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	g_object_class_install_properties (object_class, ANIM_N_PROPS, anim_properties);

	/**
	 * LrgSpriteAnimation::changed:
	 * @self: the clip
	 *
	 * Emitted when frames, events or timing change (editor previews
	 * refresh on it).
	 */
	anim_signals[ANIM_SIGNAL_CHANGED] =
		g_signal_new ("changed", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
		              0, NULL, NULL, NULL, G_TYPE_NONE, 0);
}

static void
lrg_sprite_animation_init (LrgSpriteAnimation *self)
{
	self->fps = 10.0f;
	self->loop_mode = LRG_SPRITE_LOOP_LOOP;
	self->frames = g_array_new (FALSE, FALSE, sizeof (ClipFrame));
	self->events = g_array_new (FALSE, FALSE, sizeof (ClipEvent));
	g_array_set_clear_func (self->events, clip_event_clear);
}

LrgSpriteAnimation *
lrg_sprite_animation_new (const gchar *name)
{
	return g_object_new (LRG_TYPE_SPRITE_ANIMATION, "name", name, NULL);
}

static void
anim_changed (LrgSpriteAnimation *self)
{
	g_signal_emit (self, anim_signals[ANIM_SIGNAL_CHANGED], 0);
}

const gchar *
lrg_sprite_animation_get_name (LrgSpriteAnimation *self)
{
	g_return_val_if_fail (LRG_IS_SPRITE_ANIMATION (self), NULL);
	return self->name;
}

void
lrg_sprite_animation_set_name (LrgSpriteAnimation *self,
                               const gchar        *name)
{
	g_return_if_fail (LRG_IS_SPRITE_ANIMATION (self));

	if (g_strcmp0 (self->name, name) == 0)
		return;
	g_free (self->name);
	self->name = g_strdup (name);
	g_object_notify_by_pspec (G_OBJECT (self), anim_properties[ANIM_PROP_NAME]);
}

gfloat
lrg_sprite_animation_get_fps (LrgSpriteAnimation *self)
{
	g_return_val_if_fail (LRG_IS_SPRITE_ANIMATION (self), 10.0f);
	return self->fps;
}

void
lrg_sprite_animation_set_fps (LrgSpriteAnimation *self,
                              gfloat              fps)
{
	g_return_if_fail (LRG_IS_SPRITE_ANIMATION (self));
	g_return_if_fail (fps > 0.0f);

	if (self->fps == fps)
		return;
	self->fps = fps;
	g_object_notify_by_pspec (G_OBJECT (self), anim_properties[ANIM_PROP_FPS]);
	anim_changed (self);
}

LrgSpriteLoopMode
lrg_sprite_animation_get_loop_mode (LrgSpriteAnimation *self)
{
	g_return_val_if_fail (LRG_IS_SPRITE_ANIMATION (self), LRG_SPRITE_LOOP_LOOP);
	return self->loop_mode;
}

void
lrg_sprite_animation_set_loop_mode (LrgSpriteAnimation *self,
                                    LrgSpriteLoopMode   mode)
{
	g_return_if_fail (LRG_IS_SPRITE_ANIMATION (self));
	g_return_if_fail (mode <= LRG_SPRITE_LOOP_PING_PONG);

	if (self->loop_mode == mode)
		return;
	self->loop_mode = mode;
	g_object_notify_by_pspec (G_OBJECT (self), anim_properties[ANIM_PROP_LOOP_MODE]);
	anim_changed (self);
}

void
lrg_sprite_animation_add_frame (LrgSpriteAnimation *self,
                                guint               frame,
                                gfloat              duration)
{
	lrg_sprite_animation_insert_frame (self, G_MAXUINT, frame, duration);
}

void
lrg_sprite_animation_insert_frame (LrgSpriteAnimation *self,
                                   guint               index,
                                   guint               frame,
                                   gfloat              duration)
{
	ClipFrame cf;
	guint i;

	g_return_if_fail (LRG_IS_SPRITE_ANIMATION (self));
	g_return_if_fail (duration >= 0.0f);

	index = MIN (index, self->frames->len);
	cf.frame = frame;
	cf.duration = duration;
	g_array_insert_val (self->frames, index, cf);

	/* events at or after the insertion point move with their frames */
	for (i = 0; i < self->events->len; i++)
	{
		ClipEvent *ev = &g_array_index (self->events, ClipEvent, i);

		if (ev->frame >= index && index < self->frames->len - 1)
			ev->frame++;
	}
	anim_changed (self);
}

gboolean
lrg_sprite_animation_remove_frame (LrgSpriteAnimation *self,
                                   guint               index)
{
	guint i;

	g_return_val_if_fail (LRG_IS_SPRITE_ANIMATION (self), FALSE);

	if (index >= self->frames->len)
		return FALSE;
	g_array_remove_index (self->frames, index);

	i = 0;
	while (i < self->events->len)
	{
		ClipEvent *ev = &g_array_index (self->events, ClipEvent, i);

		if (ev->frame == index)
		{
			g_array_remove_index (self->events, i);
			continue;
		}
		if (ev->frame > index)
			ev->frame--;
		i++;
	}
	anim_changed (self);
	return TRUE;
}

void
lrg_sprite_animation_clear_frames (LrgSpriteAnimation *self)
{
	g_return_if_fail (LRG_IS_SPRITE_ANIMATION (self));

	g_array_set_size (self->frames, 0);
	g_array_set_size (self->events, 0);
	anim_changed (self);
}

guint
lrg_sprite_animation_get_n_frames (LrgSpriteAnimation *self)
{
	g_return_val_if_fail (LRG_IS_SPRITE_ANIMATION (self), 0);
	return self->frames->len;
}

guint
lrg_sprite_animation_get_frame (LrgSpriteAnimation *self,
                                guint               index,
                                gfloat             *out_duration)
{
	ClipFrame *cf;

	g_return_val_if_fail (LRG_IS_SPRITE_ANIMATION (self), 0);

	if (index >= self->frames->len)
	{
		if (out_duration != NULL)
			*out_duration = 0.0f;
		return 0;
	}
	cf = &g_array_index (self->frames, ClipFrame, index);
	if (out_duration != NULL)
		*out_duration = cf->duration;
	return cf->frame;
}

gboolean
lrg_sprite_animation_set_frame (LrgSpriteAnimation *self,
                                guint               index,
                                guint               frame,
                                gfloat              duration)
{
	ClipFrame *cf;

	g_return_val_if_fail (LRG_IS_SPRITE_ANIMATION (self), FALSE);
	g_return_val_if_fail (duration >= 0.0f, FALSE);

	if (index >= self->frames->len)
		return FALSE;
	cf = &g_array_index (self->frames, ClipFrame, index);
	cf->frame = frame;
	cf->duration = duration;
	anim_changed (self);
	return TRUE;
}

gfloat
lrg_sprite_animation_get_frame_duration (LrgSpriteAnimation *self,
                                         guint               index)
{
	ClipFrame *cf;

	g_return_val_if_fail (LRG_IS_SPRITE_ANIMATION (self), 0.0f);

	if (index >= self->frames->len)
		return 0.0f;
	cf = &g_array_index (self->frames, ClipFrame, index);
	return cf->duration > 0.0f ? cf->duration : 1.0f / self->fps;
}

/*
 * Steps: the sequence of clip positions one cycle shows.  Once and loop:
 * 0 .. n-1.  Ping-pong: 0 .. n-1, then n-2 .. 1 (the ends are not
 * doubled), so a cycle has 2n - 2 steps (n for n < 2).
 */
static guint
step_count (LrgSpriteAnimation *self)
{
	guint n = self->frames->len;

	if (self->loop_mode == LRG_SPRITE_LOOP_PING_PONG && n > 2)
		return 2 * n - 2;
	return n;
}

static guint
step_frame (LrgSpriteAnimation *self,
            guint               step)
{
	guint n = self->frames->len;

	if (step < n)
		return step;
	return 2 * n - 2 - step;
}

gfloat
lrg_sprite_animation_get_duration (LrgSpriteAnimation *self)
{
	gfloat total = 0.0f;
	guint steps;
	guint s;

	g_return_val_if_fail (LRG_IS_SPRITE_ANIMATION (self), 0.0f);

	steps = step_count (self);
	for (s = 0; s < steps; s++)
		total += lrg_sprite_animation_get_frame_duration (self, step_frame (self, s));
	return total;
}

/*
 * absolute_step:
 * The number of steps started up to @time, counting across cycles, minus
 * one: the "absolute" step showing at @time.  Once clips stop at their
 * last step and report @finished.
 */
static gint64
absolute_step (LrgSpriteAnimation *self,
               gfloat              time,
               gboolean           *finished)
{
	gfloat total = lrg_sprite_animation_get_duration (self);
	guint steps = step_count (self);
	gint64 cycles = 0;
	gfloat rem;
	guint s;

	*finished = FALSE;
	if (steps == 0 || total <= 0.0f)
		return 0;
	if (time < 0.0f)
		time = 0.0f;

	if (self->loop_mode == LRG_SPRITE_LOOP_ONCE)
	{
		if (time >= total)
		{
			*finished = TRUE;
			return (gint64) steps - 1;
		}
		rem = time;
	}
	else
	{
		cycles = (gint64) floor ((gdouble) time / (gdouble) total);
		rem = (gfloat) ((gdouble) time - (gdouble) cycles * (gdouble) total);
	}

	for (s = 0; s < steps; s++)
	{
		gfloat d = lrg_sprite_animation_get_frame_duration (self, step_frame (self, s));

		if (rem < d)
			break;
		rem -= d;
	}
	if (s >= steps)
		s = steps - 1;
	return cycles * (gint64) steps + (gint64) s;
}

guint
lrg_sprite_animation_sample (LrgSpriteAnimation *self,
                             gfloat              time,
                             gboolean           *out_finished)
{
	gboolean finished = FALSE;
	gint64 step;

	g_return_val_if_fail (LRG_IS_SPRITE_ANIMATION (self), 0);

	step = absolute_step (self, time, &finished);
	if (out_finished != NULL)
		*out_finished = finished;
	if (self->frames->len == 0)
		return 0;
	return step_frame (self, (guint) (step % (gint64) step_count (self)));
}

void
lrg_sprite_animation_add_event (LrgSpriteAnimation *self,
                                guint               frame,
                                const gchar        *name)
{
	ClipEvent ev;
	guint i;

	g_return_if_fail (LRG_IS_SPRITE_ANIMATION (self));
	g_return_if_fail (name != NULL);

	ev.frame = frame;
	ev.name = g_strdup (name);

	/* keep events sorted by frame, insertion order within a frame */
	for (i = 0; i < self->events->len; i++)
		if (g_array_index (self->events, ClipEvent, i).frame > frame)
			break;
	g_array_insert_val (self->events, i, ev);
	anim_changed (self);
}

guint
lrg_sprite_animation_get_n_events (LrgSpriteAnimation *self)
{
	g_return_val_if_fail (LRG_IS_SPRITE_ANIMATION (self), 0);
	return self->events->len;
}

const gchar *
lrg_sprite_animation_get_event (LrgSpriteAnimation *self,
                                guint               index,
                                guint              *out_frame)
{
	ClipEvent *ev;

	g_return_val_if_fail (LRG_IS_SPRITE_ANIMATION (self), NULL);

	if (index >= self->events->len)
		return NULL;
	ev = &g_array_index (self->events, ClipEvent, index);
	if (out_frame != NULL)
		*out_frame = ev->frame;
	return ev->name;
}

gboolean
lrg_sprite_animation_remove_event (LrgSpriteAnimation *self,
                                   guint               index)
{
	g_return_val_if_fail (LRG_IS_SPRITE_ANIMATION (self), FALSE);

	if (index >= self->events->len)
		return FALSE;
	g_array_remove_index (self->events, index);
	anim_changed (self);
	return TRUE;
}

/* ==========================================================================
 * Sheet data (qdata on LrgSpriteSheet)
 * ========================================================================== */

typedef struct
{
	gboolean     has_grid;
	gint         frame_w;
	gint         frame_h;
	guint        columns;
	guint        rows;
	guint        count;
	gint         margin_x;
	gint         margin_y;
	gint         spacing_x;
	gint         spacing_y;
	gfloat       pivot_x;
	gfloat       pivot_y;
	GPtrArray   *clips;     /* LrgSpriteAnimation*, insertion order */
	YamlMapping *extra;     /* unknown top-level keys of the loaded file */
} SheetData;

static void
sheet_data_free (gpointer data)
{
	SheetData *sd = data;

	g_ptr_array_unref (sd->clips);
	if (sd->extra != NULL)
		yaml_mapping_unref (sd->extra);
	g_free (sd);
}

static SheetData *
sheet_data (LrgSpriteSheet *sheet)
{
	static GQuark quark = 0;
	SheetData *sd;

	if (quark == 0)
		quark = g_quark_from_static_string ("lrg-sprite-sheet-rsprite-data");
	sd = g_object_get_qdata (G_OBJECT (sheet), quark);
	if (sd == NULL)
	{
		sd = g_new0 (SheetData, 1);
		sd->clips = g_ptr_array_new_with_free_func (g_object_unref);
		g_object_set_qdata_full (G_OBJECT (sheet), quark, sd, sheet_data_free);
	}
	return sd;
}

/* the generated name of frame @index */
static gchar *
auto_frame_name (guint index)
{
	return g_strdup_printf ("frame_%u", index);
}

/* the rectangle grid frame @index covers */
static void
grid_frame_rect (const SheetData *sd,
                 guint            index,
                 gint            *x,
                 gint            *y)
{
	guint col = index % sd->columns;
	guint row = index / sd->columns;

	*x = sd->margin_x + (gint) col * (sd->frame_w + sd->spacing_x);
	*y = sd->margin_y + (gint) row * (sd->frame_h + sd->spacing_y);
}

guint
lrg_sprite_sheet_slice_grid (LrgSpriteSheet *self,
                             gint            frame_width,
                             gint            frame_height,
                             guint           columns,
                             guint           rows,
                             guint           count,
                             gint            margin_x,
                             gint            margin_y,
                             gint            spacing_x,
                             gint            spacing_y,
                             gfloat          pivot_x,
                             gfloat          pivot_y)
{
	SheetData *sd;
	guint i;

	g_return_val_if_fail (LRG_IS_SPRITE_SHEET (self), 0);
	g_return_val_if_fail (frame_width > 0 && frame_height > 0, 0);
	g_return_val_if_fail (columns > 0 && rows > 0, 0);

	sd = sheet_data (self);
	sd->has_grid = TRUE;
	sd->frame_w = frame_width;
	sd->frame_h = frame_height;
	sd->columns = columns;
	sd->rows = rows;
	sd->count = count > 0 ? MIN (count, columns * rows) : columns * rows;
	sd->margin_x = margin_x;
	sd->margin_y = margin_y;
	sd->spacing_x = spacing_x;
	sd->spacing_y = spacing_y;
	sd->pivot_x = pivot_x;
	sd->pivot_y = pivot_y;

	lrg_sprite_sheet_clear_frames (self);
	for (i = 0; i < sd->count; i++)
	{
		g_autofree gchar *name = auto_frame_name (i);
		guint index;
		gint x;
		gint y;

		grid_frame_rect (sd, i, &x, &y);
		index = lrg_sprite_sheet_add_frame_rect (self, name, x, y, frame_width, frame_height);
		lrg_atlas_region_set_pivot (lrg_sprite_sheet_get_frame (self, index), pivot_x, pivot_y);
	}
	return sd->count;
}

gboolean
lrg_sprite_sheet_get_grid (LrgSpriteSheet *self,
                           gint           *frame_width,
                           gint           *frame_height,
                           guint          *columns,
                           guint          *count)
{
	SheetData *sd;

	g_return_val_if_fail (LRG_IS_SPRITE_SHEET (self), FALSE);

	sd = sheet_data (self);
	if (frame_width != NULL)
		*frame_width = sd->has_grid ? sd->frame_w : 0;
	if (frame_height != NULL)
		*frame_height = sd->has_grid ? sd->frame_h : 0;
	if (columns != NULL)
		*columns = sd->has_grid ? sd->columns : 0;
	if (count != NULL)
		*count = sd->has_grid ? sd->count : 0;
	return sd->has_grid;
}

/* island bounding box while flood filling */
typedef struct
{
	gint x0;
	gint y0;
	gint x1;
	gint y1;
} Box;

static gint
compare_boxes (gconstpointer a,
               gconstpointer b)
{
	const Box *ba = a;
	const Box *bb = b;

	/* rows: boxes whose tops lie within half the first box's height share a row */
	if (ABS (ba->y0 - bb->y0) > MAX ((ba->y1 - ba->y0) / 2, 1))
		return ba->y0 < bb->y0 ? -1 : 1;
	if (ba->x0 != bb->x0)
		return ba->x0 < bb->x0 ? -1 : 1;
	return ba->y0 < bb->y0 ? -1 : (ba->y0 > bb->y0 ? 1 : 0);
}

guint
lrg_sprite_sheet_slice_alpha (LrgSpriteSheet *self,
                              const guint8   *rgba,
                              gsize           n_bytes,
                              guint           width,
                              guint           height,
                              guint8          alpha_threshold,
                              guint           min_size,
                              gfloat          pivot_x,
                              gfloat          pivot_y)
{
	g_autofree guint8 *seen = NULL;
	g_autoptr(GArray) boxes = NULL;
	g_autoptr(GArray) stack = NULL;
	SheetData *sd;
	guint x;
	guint y;
	guint i;

	g_return_val_if_fail (LRG_IS_SPRITE_SHEET (self), 0);
	g_return_val_if_fail (rgba != NULL, 0);
	g_return_val_if_fail (n_bytes >= (gsize) width * height * 4, 0);

	seen = g_new0 (guint8, (gsize) width * height);
	boxes = g_array_new (FALSE, FALSE, sizeof (Box));
	stack = g_array_new (FALSE, FALSE, sizeof (guint));

	/* 8-connected flood fill of every unvisited opaque pixel */
	for (y = 0; y < height; y++)
	{
		for (x = 0; x < width; x++)
		{
			gsize idx = (gsize) y * width + x;
			Box box;

			if (seen[idx] || rgba[idx * 4 + 3] <= alpha_threshold)
				continue;

			box.x0 = box.x1 = (gint) x;
			box.y0 = box.y1 = (gint) y;
			seen[idx] = 1;
			g_array_set_size (stack, 0);
			{
				guint start = (guint) idx;

				g_array_append_val (stack, start);
			}
			while (stack->len > 0)
			{
				guint cur = g_array_index (stack, guint, stack->len - 1);
				gint cx = (gint) (cur % width);
				gint cy = (gint) (cur / width);
				gint dx;
				gint dy;

				g_array_set_size (stack, stack->len - 1);
				box.x0 = MIN (box.x0, cx);
				box.y0 = MIN (box.y0, cy);
				box.x1 = MAX (box.x1, cx);
				box.y1 = MAX (box.y1, cy);

				for (dy = -1; dy <= 1; dy++)
				{
					for (dx = -1; dx <= 1; dx++)
					{
						gint nx = cx + dx;
						gint ny = cy + dy;
						guint n;

						if (nx < 0 || ny < 0 || (guint) nx >= width || (guint) ny >= height)
							continue;
						n = (guint) ny * width + (guint) nx;
						if (seen[n] || rgba[(gsize) n * 4 + 3] <= alpha_threshold)
							continue;
						seen[n] = 1;
						g_array_append_val (stack, n);
					}
				}
			}

			if ((guint) (box.x1 - box.x0 + 1) >= min_size && (guint) (box.y1 - box.y0 + 1) >= min_size)
				g_array_append_val (boxes, box);
		}
	}

	g_array_sort (boxes, compare_boxes);

	sd = sheet_data (self);
	sd->has_grid = FALSE;
	lrg_sprite_sheet_clear_frames (self);
	for (i = 0; i < boxes->len; i++)
	{
		Box *b = &g_array_index (boxes, Box, i);
		g_autofree gchar *name = auto_frame_name (i);
		gint w = b->x1 - b->x0 + 1;
		gint h = b->y1 - b->y0 + 1;
		guint index = lrg_sprite_sheet_add_frame_rect (self, name, b->x0, b->y0, w, h);

		lrg_atlas_region_set_pivot (lrg_sprite_sheet_get_frame (self, index),
		                            pivot_x * (gfloat) w, pivot_y * (gfloat) h);
	}
	return boxes->len;
}

/* ==========================================================================
 * Clips on the sheet
 * ========================================================================== */

void
lrg_sprite_sheet_add_clip (LrgSpriteSheet     *self,
                           LrgSpriteAnimation *clip)
{
	SheetData *sd;
	guint i;

	g_return_if_fail (LRG_IS_SPRITE_SHEET (self));
	g_return_if_fail (LRG_IS_SPRITE_ANIMATION (clip));

	sd = sheet_data (self);
	for (i = 0; i < sd->clips->len; i++)
	{
		LrgSpriteAnimation *other = g_ptr_array_index (sd->clips, i);

		if (other == clip)
			return;
		if (g_strcmp0 (other->name, clip->name) == 0)
		{
			/* same name: replace in place, keeping the order */
			g_object_unref (other);
			g_ptr_array_index (sd->clips, i) = g_object_ref (clip);
			return;
		}
	}
	g_ptr_array_add (sd->clips, g_object_ref (clip));
}

gboolean
lrg_sprite_sheet_remove_clip (LrgSpriteSheet *self,
                              const gchar    *name)
{
	SheetData *sd;
	guint i;

	g_return_val_if_fail (LRG_IS_SPRITE_SHEET (self), FALSE);
	g_return_val_if_fail (name != NULL, FALSE);

	sd = sheet_data (self);
	for (i = 0; i < sd->clips->len; i++)
	{
		LrgSpriteAnimation *clip = g_ptr_array_index (sd->clips, i);

		if (g_strcmp0 (clip->name, name) == 0)
		{
			g_ptr_array_remove_index (sd->clips, i);
			return TRUE;
		}
	}
	return FALSE;
}

LrgSpriteAnimation *
lrg_sprite_sheet_get_clip (LrgSpriteSheet *self,
                           const gchar    *name)
{
	SheetData *sd;
	guint i;

	g_return_val_if_fail (LRG_IS_SPRITE_SHEET (self), NULL);
	g_return_val_if_fail (name != NULL, NULL);

	sd = sheet_data (self);
	for (i = 0; i < sd->clips->len; i++)
	{
		LrgSpriteAnimation *clip = g_ptr_array_index (sd->clips, i);

		if (g_strcmp0 (clip->name, name) == 0)
			return clip;
	}
	return NULL;
}

guint
lrg_sprite_sheet_get_n_clips (LrgSpriteSheet *self)
{
	g_return_val_if_fail (LRG_IS_SPRITE_SHEET (self), 0);
	return sheet_data (self)->clips->len;
}

LrgSpriteAnimation *
lrg_sprite_sheet_get_clip_at (LrgSpriteSheet *self,
                              guint           index)
{
	SheetData *sd;

	g_return_val_if_fail (LRG_IS_SPRITE_SHEET (self), NULL);

	sd = sheet_data (self);
	if (index >= sd->clips->len)
		return NULL;
	return g_ptr_array_index (sd->clips, index);
}

guint
lrg_sprite_sheet_apply_to_animator (LrgSpriteSheet       *self,
                                    LrgAnimatorComponent *animator,
                                    GrlTexture           *texture)
{
	SheetData *sd;
	guint registered = 0;
	guint i;

	g_return_val_if_fail (LRG_IS_SPRITE_SHEET (self), 0);
	g_return_val_if_fail (LRG_IS_ANIMATOR_COMPONENT (animator), 0);

	/* the animator computes rects from a margin-less, gap-less grid */
	sd = sheet_data (self);
	if (!sd->has_grid || sd->margin_x != 0 || sd->margin_y != 0 ||
	    sd->spacing_x != 0 || sd->spacing_y != 0)
		return 0;

	if (texture != NULL)
		lrg_animator_component_set_texture (animator, texture, sd->frame_w, sd->frame_h);

	for (i = 0; i < sd->clips->len; i++)
	{
		LrgSpriteAnimation *clip = g_ptr_array_index (sd->clips, i);
		ClipFrame *first;
		gboolean ok = clip->frames->len > 0 && clip->loop_mode != LRG_SPRITE_LOOP_PING_PONG;
		guint k;

		if (!ok)
			continue;
		first = &g_array_index (clip->frames, ClipFrame, 0);
		for (k = 0; k < clip->frames->len && ok; k++)
		{
			ClipFrame *cf = &g_array_index (clip->frames, ClipFrame, k);

			ok = cf->frame == first->frame + k && cf->frame < sd->count &&
			     cf->duration == first->duration;
		}
		if (!ok)
			continue;

		lrg_animator_component_add_animation (animator, clip->name, (gint) first->frame,
		                                      (gint) clip->frames->len,
		                                      first->duration > 0.0f ? 1.0f / first->duration : clip->fps,
		                                      clip->loop_mode == LRG_SPRITE_LOOP_LOOP);
		registered++;
	}
	return registered;
}

/* ==========================================================================
 * .rsprite reader
 * ========================================================================== */

typedef struct
{
	const gchar  *source;
	GError      **error;
} Reader;

static gboolean
rd_fail (Reader      *rd,
         const gchar *path,
         const gchar *message)
{
	g_set_error (rd->error, LRG_SPRITE_ANIMATION_ERROR, LRG_SPRITE_ANIMATION_ERROR_INVALID,
	             "%s: %s: %s", rd->source, path, message);
	return FALSE;
}

static const gchar *
rd_scalar (YamlNode *node)
{
	if (node == NULL || yaml_node_get_node_type (node) != YAML_NODE_SCALAR)
		return NULL;
	return yaml_node_get_scalar (node);
}

static gboolean
parse_uint (const gchar *text,
            guint64     *out)
{
	gchar *end = NULL;

	if (text == NULL || text[0] < '0' || text[0] > '9')
		return FALSE;
	*out = g_ascii_strtoull (text, &end, 10);
	return end != NULL && *end == '\0';
}

static gboolean
rd_int (Reader      *rd,
        YamlNode    *node,
        const gchar *path,
        gint64       min,
        gint64       max,
        gint        *out)
{
	const gchar *text = rd_scalar (node);
	gchar *end = NULL;
	gint64 value;

	if (text == NULL || text[0] == '\0')
		return rd_fail (rd, path, "expected a whole number");
	value = g_ascii_strtoll (text, &end, 10);
	if (end == NULL || *end != '\0')
		return rd_fail (rd, path, "expected a whole number");
	if (value < min || value > max)
	{
		g_autofree gchar *msg = g_strdup_printf ("expected a whole number from %" G_GINT64_FORMAT
		                                         " to %" G_GINT64_FORMAT, min, max);

		return rd_fail (rd, path, msg);
	}
	*out = (gint) value;
	return TRUE;
}

static gboolean
rd_float (Reader      *rd,
          YamlNode    *node,
          const gchar *path,
          gfloat       min,
          gfloat      *out)
{
	const gchar *text = rd_scalar (node);
	gchar *end = NULL;
	gdouble value;

	if (text == NULL || text[0] == '\0')
		return rd_fail (rd, path, "expected a number");
	value = g_ascii_strtod (text, &end);
	if (end == NULL || *end != '\0' || !isfinite (value))
		return rd_fail (rd, path, "expected a number");
	if (value < (gdouble) min)
	{
		g_autofree gchar *msg = g_strdup_printf ("expected a number of at least %g", (gdouble) min);

		return rd_fail (rd, path, msg);
	}
	*out = (gfloat) value;
	return TRUE;
}

/* [a, b, ...] of exactly @n integers */
static gboolean
rd_ints (Reader      *rd,
         YamlNode    *node,
         const gchar *path,
         guint        n,
         gint64       min,
         gint        *out)
{
	YamlSequence *seq;
	guint i;

	if (node == NULL || yaml_node_get_node_type (node) != YAML_NODE_SEQUENCE ||
	    yaml_sequence_get_length (yaml_node_get_sequence (node)) != n)
	{
		g_autofree gchar *msg = g_strdup_printf ("expected a list of %u whole numbers", n);

		return rd_fail (rd, path, msg);
	}
	seq = yaml_node_get_sequence (node);
	for (i = 0; i < n; i++)
	{
		g_autofree gchar *p = g_strdup_printf ("%s/%u", path, i);

		if (!rd_int (rd, yaml_sequence_get_element (seq, i), p, min, G_MAXINT, &out[i]))
			return FALSE;
	}
	return TRUE;
}

static gboolean
rd_floats2 (Reader      *rd,
            YamlNode    *node,
            const gchar *path,
            gfloat      *out)
{
	YamlSequence *seq;
	guint i;

	if (node == NULL || yaml_node_get_node_type (node) != YAML_NODE_SEQUENCE ||
	    yaml_sequence_get_length (yaml_node_get_sequence (node)) != 2)
		return rd_fail (rd, path, "expected [x, y]");
	seq = yaml_node_get_sequence (node);
	for (i = 0; i < 2; i++)
	{
		g_autofree gchar *p = g_strdup_printf ("%s/%u", path, i);

		if (!rd_float (rd, yaml_sequence_get_element (seq, i), p, -G_MAXFLOAT, &out[i]))
			return FALSE;
	}
	return TRUE;
}

static gboolean
read_grid (Reader         *rd,
           LrgSpriteSheet *sheet,
           YamlNode       *node)
{
	YamlMapping *map;
	gint size[2] = { 0, 0 };
	gint columns = 0;
	gint rows = 0;
	gint count = 0;
	gint margin[2] = { 0, 0 };
	gint spacing[2] = { 0, 0 };
	gfloat pivot[2] = { 0.0f, 0.0f };
	gboolean have_size = FALSE;
	guint i;

	if (node == NULL || yaml_node_get_node_type (node) != YAML_NODE_MAPPING)
		return rd_fail (rd, "/grid", "expected a mapping");
	map = yaml_node_get_mapping (node);

	for (i = 0; i < yaml_mapping_get_size (map); i++)
	{
		const gchar *key = yaml_mapping_get_key (map, i);
		YamlNode *value = yaml_mapping_get_value (map, i);
		g_autofree gchar *p = g_strdup_printf ("/grid/%s", key);

		if (g_strcmp0 (key, "frame_size") == 0)
		{
			if (!rd_ints (rd, value, p, 2, 1, size))
				return FALSE;
			have_size = TRUE;
		}
		else if (g_strcmp0 (key, "columns") == 0)
		{
			if (!rd_int (rd, value, p, 1, 65536, &columns))
				return FALSE;
		}
		else if (g_strcmp0 (key, "rows") == 0)
		{
			if (!rd_int (rd, value, p, 1, 65536, &rows))
				return FALSE;
		}
		else if (g_strcmp0 (key, "count") == 0)
		{
			if (!rd_int (rd, value, p, 1, G_MAXINT, &count))
				return FALSE;
		}
		else if (g_strcmp0 (key, "margin") == 0)
		{
			if (!rd_ints (rd, value, p, 2, 0, margin))
				return FALSE;
		}
		else if (g_strcmp0 (key, "spacing") == 0)
		{
			if (!rd_ints (rd, value, p, 2, 0, spacing))
				return FALSE;
		}
		else if (g_strcmp0 (key, "pivot") == 0)
		{
			if (!rd_floats2 (rd, value, p, pivot))
				return FALSE;
		}
		else
		{
			g_autofree gchar *msg = g_strdup_printf ("unknown key \"%s\" (expected frame_size, columns, rows, "
			                                         "count, margin, spacing, pivot)", key);

			return rd_fail (rd, "/grid", msg);
		}
	}
	if (!have_size || columns == 0 || rows == 0)
		return rd_fail (rd, "/grid", "a grid needs frame_size, columns and rows");
	if (count > columns * rows)
		return rd_fail (rd, "/grid/count", "count is larger than columns x rows");

	lrg_sprite_sheet_slice_grid (sheet, size[0], size[1], (guint) columns, (guint) rows, (guint) count,
	                             margin[0], margin[1], spacing[0], spacing[1], pivot[0], pivot[1]);
	return TRUE;
}

static gboolean
read_frames (Reader         *rd,
             LrgSpriteSheet *sheet,
             YamlNode       *node)
{
	YamlSequence *seq;
	SheetData *sd = sheet_data (sheet);
	guint i;

	if (node == NULL || yaml_node_get_node_type (node) != YAML_NODE_SEQUENCE)
		return rd_fail (rd, "/frames", "expected a list of frames");
	seq = yaml_node_get_sequence (node);

	for (i = 0; i < yaml_sequence_get_length (seq); i++)
	{
		g_autofree gchar *path = g_strdup_printf ("/frames/%u", i);
		YamlNode *fnode = yaml_sequence_get_element (seq, i);
		YamlMapping *map;
		const gchar *name = NULL;
		gint rect[4] = { 0, 0, 0, 0 };
		gfloat pivot[2] = { 0.0f, 0.0f };
		gint index = -1;
		gboolean have_rect = FALSE;
		gboolean have_pivot = FALSE;
		g_autofree gchar *auto_name = NULL;
		LrgAtlasRegion *region;
		guint k;

		if (fnode == NULL || yaml_node_get_node_type (fnode) != YAML_NODE_MAPPING)
			return rd_fail (rd, path, "expected a mapping");
		map = yaml_node_get_mapping (fnode);
		for (k = 0; k < yaml_mapping_get_size (map); k++)
		{
			const gchar *key = yaml_mapping_get_key (map, k);
			YamlNode *value = yaml_mapping_get_value (map, k);
			g_autofree gchar *p = g_strdup_printf ("%s/%s", path, key);

			if (g_strcmp0 (key, "index") == 0)
			{
				if (!rd_int (rd, value, p, 0, sd->has_grid ? (gint64) sd->count - 1 : -1, &index))
					return FALSE;
			}
			else if (g_strcmp0 (key, "name") == 0)
			{
				name = rd_scalar (value);
				if (name == NULL || name[0] == '\0')
					return rd_fail (rd, p, "expected a non-empty string");
			}
			else if (g_strcmp0 (key, "rect") == 0)
			{
				if (!rd_ints (rd, value, p, 4, 0, rect))
					return FALSE;
				if (rect[2] == 0 || rect[3] == 0)
					return rd_fail (rd, p, "a frame needs a width and height above 0");
				have_rect = TRUE;
			}
			else if (g_strcmp0 (key, "pivot") == 0)
			{
				if (!rd_floats2 (rd, value, p, pivot))
					return FALSE;
				have_pivot = TRUE;
			}
			else
			{
				g_autofree gchar *msg = g_strdup_printf ("unknown key \"%s\" (expected index, name, rect, pivot)",
				                                         key);

				return rd_fail (rd, path, msg);
			}
		}

		if (index >= 0)
		{
			LrgAtlasRegion *old = lrg_sprite_sheet_get_frame (sheet, (guint) index);
			gint gx;
			gint gy;

			/* an override of a grid frame: name and pivot only */
			if (have_rect)
				return rd_fail (rd, path, "a grid frame override cannot change rect (the grid sets it)");
			grid_frame_rect (sd, (guint) index, &gx, &gy);
			if (name == NULL)
				name = lrg_atlas_region_get_name (old);
			if (!have_pivot)
			{
				pivot[0] = lrg_atlas_region_get_pivot_x (old);
				pivot[1] = lrg_atlas_region_get_pivot_y (old);
			}
			if (g_strcmp0 (name, lrg_atlas_region_get_name (old)) != 0 &&
			    lrg_sprite_sheet_get_frame_by_name (sheet, name) != NULL)
			{
				g_autofree gchar *msg = g_strdup_printf ("frame name \"%s\" is used twice", name);

				return rd_fail (rd, path, msg);
			}
			region = lrg_atlas_region_new (name, gx, gy, sd->frame_w, sd->frame_h);
			lrg_atlas_region_set_pivot (region, pivot[0], pivot[1]);
			lrg_sprite_sheet_replace_frame (sheet, (guint) index, region);
			continue;
		}

		if (!have_rect)
			return rd_fail (rd, path, "missing key \"rect\" ([x, y, width, height])");
		if (name == NULL)
		{
			auto_name = auto_frame_name (lrg_sprite_sheet_get_frame_count (sheet));
			name = auto_name;
		}
		if (lrg_sprite_sheet_get_frame_by_name (sheet, name) != NULL)
		{
			g_autofree gchar *msg = g_strdup_printf ("frame name \"%s\" is used twice", name);

			return rd_fail (rd, path, msg);
		}
		k = lrg_sprite_sheet_add_frame_rect (sheet, name, rect[0], rect[1], rect[2], rect[3]);
		lrg_atlas_region_set_pivot (lrg_sprite_sheet_get_frame (sheet, k), pivot[0], pivot[1]);
	}
	return TRUE;
}

/* a frame reference: an index, or a frame name */
static gboolean
read_frame_ref (Reader         *rd,
                LrgSpriteSheet *sheet,
                YamlNode       *node,
                const gchar    *path,
                guint          *out)
{
	const gchar *text = rd_scalar (node);
	guint64 index;
	guint i;

	if (text == NULL)
		return rd_fail (rd, path, "expected a frame index or name");
	if (parse_uint (text, &index))
	{
		if (index >= lrg_sprite_sheet_get_frame_count (sheet))
		{
			g_autofree gchar *msg = g_strdup_printf ("frame %" G_GUINT64_FORMAT " does not exist (the sheet has %u)",
			                                         index, lrg_sprite_sheet_get_frame_count (sheet));

			return rd_fail (rd, path, msg);
		}
		*out = (guint) index;
		return TRUE;
	}
	for (i = 0; i < lrg_sprite_sheet_get_frame_count (sheet); i++)
	{
		if (g_strcmp0 (lrg_atlas_region_get_name (lrg_sprite_sheet_get_frame (sheet, i)), text) == 0)
		{
			*out = i;
			return TRUE;
		}
	}
	{
		g_autofree gchar *msg = g_strdup_printf ("no frame named \"%s\"", text);

		return rd_fail (rd, path, msg);
	}
}

static gboolean
read_animation (Reader         *rd,
                LrgSpriteSheet *sheet,
                YamlNode       *node,
                const gchar    *path)
{
	g_autoptr(LrgSpriteAnimation) clip = NULL;
	YamlMapping *map;
	YamlNode *events = NULL;
	guint i;

	if (node == NULL || yaml_node_get_node_type (node) != YAML_NODE_MAPPING)
		return rd_fail (rd, path, "expected a mapping");
	map = yaml_node_get_mapping (node);
	if (rd_scalar (yaml_mapping_get_member (map, "name")) == NULL)
		return rd_fail (rd, path, "missing key \"name\" (a string)");
	if (lrg_sprite_sheet_get_clip (sheet, rd_scalar (yaml_mapping_get_member (map, "name"))) != NULL)
	{
		g_autofree gchar *msg = g_strdup_printf ("animation \"%s\" is defined twice",
		                                         rd_scalar (yaml_mapping_get_member (map, "name")));

		return rd_fail (rd, path, msg);
	}
	clip = lrg_sprite_animation_new (rd_scalar (yaml_mapping_get_member (map, "name")));

	for (i = 0; i < yaml_mapping_get_size (map); i++)
	{
		const gchar *key = yaml_mapping_get_key (map, i);
		YamlNode *value = yaml_mapping_get_value (map, i);
		g_autofree gchar *p = g_strdup_printf ("%s/%s", path, key);

		if (g_strcmp0 (key, "name") == 0)
			continue;
		else if (g_strcmp0 (key, "fps") == 0)
		{
			if (!rd_float (rd, value, p, 0.001f, &clip->fps))
				return FALSE;
		}
		else if (g_strcmp0 (key, "loop") == 0)
		{
			const gchar *text = rd_scalar (value);
			gboolean found = FALSE;
			guint m;

			for (m = 0; loop_values[m].value_nick != NULL; m++)
			{
				if (g_strcmp0 (text, loop_values[m].value_nick) == 0)
				{
					clip->loop_mode = (LrgSpriteLoopMode) loop_values[m].value;
					found = TRUE;
				}
			}
			if (!found)
				return rd_fail (rd, p, "expected once, loop or ping-pong");
		}
		else if (g_strcmp0 (key, "frames") == 0)
		{
			YamlSequence *seq;
			guint k;

			if (value == NULL || yaml_node_get_node_type (value) != YAML_NODE_SEQUENCE)
				return rd_fail (rd, p, "expected a list of frames");
			seq = yaml_node_get_sequence (value);
			for (k = 0; k < yaml_sequence_get_length (seq); k++)
			{
				g_autofree gchar *fp = g_strdup_printf ("%s/%u", p, k);
				YamlNode *fnode = yaml_sequence_get_element (seq, k);
				ClipFrame cf = { 0, 0.0f };

				if (fnode != NULL && yaml_node_get_node_type (fnode) == YAML_NODE_MAPPING)
				{
					YamlMapping *fmap = yaml_node_get_mapping (fnode);
					gboolean have_frame = FALSE;
					guint m;

					for (m = 0; m < yaml_mapping_get_size (fmap); m++)
					{
						const gchar *fkey = yaml_mapping_get_key (fmap, m);
						g_autofree gchar *kp = g_strdup_printf ("%s/%s", fp, fkey);

						if (g_strcmp0 (fkey, "frame") == 0)
						{
							if (!read_frame_ref (rd, sheet, yaml_mapping_get_value (fmap, m), kp, &cf.frame))
								return FALSE;
							have_frame = TRUE;
						}
						else if (g_strcmp0 (fkey, "duration") == 0)
						{
							if (!rd_float (rd, yaml_mapping_get_value (fmap, m), kp, 0.0f, &cf.duration))
								return FALSE;
						}
						else
						{
							g_autofree gchar *msg = g_strdup_printf ("unknown key \"%s\" (expected frame, duration)",
							                                         fkey);

							return rd_fail (rd, fp, msg);
						}
					}
					if (!have_frame)
						return rd_fail (rd, fp, "missing key \"frame\"");
				}
				else if (!read_frame_ref (rd, sheet, fnode, fp, &cf.frame))
					return FALSE;
				g_array_append_val (clip->frames, cf);
			}
		}
		else if (g_strcmp0 (key, "events") == 0)
			events = value;   /* read once the frames are known */
		else
		{
			if (clip->extra == NULL)
				clip->extra = yaml_mapping_new ();
			yaml_mapping_set_member (clip->extra, key, value);
		}
	}

	if (events != NULL)
	{
		g_autofree gchar *p = g_strdup_printf ("%s/events", path);
		YamlSequence *seq;
		guint k;

		if (yaml_node_get_node_type (events) != YAML_NODE_SEQUENCE)
			return rd_fail (rd, p, "expected a list of {frame, name}");
		seq = yaml_node_get_sequence (events);
		for (k = 0; k < yaml_sequence_get_length (seq); k++)
		{
			g_autofree gchar *ep = g_strdup_printf ("%s/%u", p, k);
			YamlNode *enode = yaml_sequence_get_element (seq, k);
			YamlMapping *emap;
			const gchar *name = NULL;
			gint frame = -1;
			guint m;

			if (enode == NULL || yaml_node_get_node_type (enode) != YAML_NODE_MAPPING)
				return rd_fail (rd, ep, "expected {frame, name}");
			emap = yaml_node_get_mapping (enode);
			for (m = 0; m < yaml_mapping_get_size (emap); m++)
			{
				const gchar *ekey = yaml_mapping_get_key (emap, m);
				g_autofree gchar *kp = g_strdup_printf ("%s/%s", ep, ekey);

				if (g_strcmp0 (ekey, "frame") == 0)
				{
					if (clip->frames->len == 0)
						return rd_fail (rd, kp, "the animation has no frames");
					if (!rd_int (rd, yaml_mapping_get_value (emap, m), kp, 0,
					             (gint64) clip->frames->len - 1, &frame))
						return FALSE;
				}
				else if (g_strcmp0 (ekey, "name") == 0)
				{
					name = rd_scalar (yaml_mapping_get_value (emap, m));
					if (name == NULL || name[0] == '\0')
						return rd_fail (rd, kp, "expected a non-empty string");
				}
				else
				{
					g_autofree gchar *msg = g_strdup_printf ("unknown key \"%s\" (expected frame, name)", ekey);

					return rd_fail (rd, ep, msg);
				}
			}
			if (frame < 0 || name == NULL)
				return rd_fail (rd, ep, "an event needs frame and name");
			lrg_sprite_animation_add_event (clip, (guint) frame, name);
		}
	}

	lrg_sprite_sheet_add_clip (sheet, clip);
	return TRUE;
}

static LrgSpriteSheet *
sheet_from_parser (YamlParser   *parser,
                   const gchar  *source,
                   GError      **error)
{
	g_autoptr(LrgSpriteSheet) sheet = NULL;
	Reader rd = { source, error };
	YamlNode *root = yaml_parser_get_root (parser);
	YamlMapping *map;
	SheetData *sd;
	guint i;

	if (root == NULL || yaml_node_get_node_type (root) != YAML_NODE_MAPPING)
	{
		rd_fail (&rd, "/", "an .rsprite document is a mapping");
		return NULL;
	}
	map = yaml_node_get_mapping (root);

	sheet = lrg_sprite_sheet_new (rd_scalar (yaml_mapping_get_member (map, "name")));
	sd = sheet_data (sheet);

	/* image size and the grid first: frames and animations refer to them */
	if (yaml_mapping_has_member (map, "image_size"))
	{
		gint size[2];

		if (!rd_ints (&rd, yaml_mapping_get_member (map, "image_size"), "/image_size", 2, 1, size))
			return NULL;
		lrg_sprite_sheet_set_texture_size (sheet, size[0], size[1]);
	}
	if (yaml_mapping_has_member (map, "grid") && !read_grid (&rd, sheet, yaml_mapping_get_member (map, "grid")))
		return NULL;
	if (yaml_mapping_has_member (map, "frames") &&
	    !read_frames (&rd, sheet, yaml_mapping_get_member (map, "frames")))
		return NULL;

	for (i = 0; i < yaml_mapping_get_size (map); i++)
	{
		const gchar *key = yaml_mapping_get_key (map, i);
		YamlNode *value = yaml_mapping_get_value (map, i);
		g_autofree gchar *p = g_strdup_printf ("/%s", key);

		if (g_strcmp0 (key, "image_size") == 0 || g_strcmp0 (key, "grid") == 0 ||
		    g_strcmp0 (key, "frames") == 0)
			continue;
		else if (g_strcmp0 (key, "version") == 0)
		{
			gint version = 0;

			if (!rd_int (&rd, value, p, 1, 1, &version))
				return NULL;
		}
		else if (g_strcmp0 (key, "name") == 0)
		{
			if (rd_scalar (value) == NULL)
			{
				rd_fail (&rd, p, "expected a string");
				return NULL;
			}
		}
		else if (g_strcmp0 (key, "image") == 0)
		{
			if (rd_scalar (value) == NULL)
			{
				rd_fail (&rd, p, "expected a path");
				return NULL;
			}
			lrg_sprite_sheet_set_texture_path (sheet, rd_scalar (value));
		}
		else if (g_strcmp0 (key, "animations") == 0)
		{
			YamlSequence *seq;
			guint k;

			if (value == NULL || yaml_node_get_node_type (value) != YAML_NODE_SEQUENCE)
			{
				rd_fail (&rd, p, "expected a list of animations");
				return NULL;
			}
			seq = yaml_node_get_sequence (value);
			for (k = 0; k < yaml_sequence_get_length (seq); k++)
			{
				g_autofree gchar *ap = g_strdup_printf ("%s/%u", p, k);

				if (!read_animation (&rd, sheet, yaml_sequence_get_element (seq, k), ap))
					return NULL;
			}
		}
		else
		{
			if (sd->extra == NULL)
				sd->extra = yaml_mapping_new ();
			yaml_mapping_set_member (sd->extra, key, value);
		}
	}

	return g_steal_pointer (&sheet);
}

static void
propagate_load_error (GError      **error,
                      GError       *local,
                      const gchar  *source)
{
	gint code = (local->domain == G_FILE_ERROR || local->domain == G_IO_ERROR)
		? LRG_SPRITE_ANIMATION_ERROR_IO : LRG_SPRITE_ANIMATION_ERROR_PARSE;

	g_set_error (error, LRG_SPRITE_ANIMATION_ERROR, code, "%s: %s", source, local->message);
}

LrgSpriteSheet *
lrg_sprite_sheet_new_from_rsprite_file (const gchar  *path,
                                        GError      **error)
{
	g_autoptr(YamlParser) parser = NULL;
	g_autoptr(GError) local = NULL;

	g_return_val_if_fail (path != NULL, NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	parser = yaml_parser_new ();
	if (!yaml_parser_load_from_file (parser, path, &local))
	{
		propagate_load_error (error, local, path);
		return NULL;
	}
	return sheet_from_parser (parser, path, error);
}

LrgSpriteSheet *
lrg_sprite_sheet_new_from_rsprite_data (const gchar  *data,
                                        gssize        length,
                                        const gchar  *source,
                                        GError      **error)
{
	g_autoptr(YamlParser) parser = NULL;
	g_autoptr(GError) local = NULL;

	g_return_val_if_fail (data != NULL, NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	if (source == NULL)
		source = "<data>";
	parser = yaml_parser_new ();
	if (!yaml_parser_load_from_data (parser, data, length, &local))
	{
		propagate_load_error (error, local, source);
		return NULL;
	}
	return sheet_from_parser (parser, source, error);
}

/* ==========================================================================
 * .rsprite writer
 * ========================================================================== */

static void
set_member (YamlMapping *map,
            const gchar *key,
            YamlNode    *node)
{
	yaml_mapping_set_member (map, key, node);
	yaml_node_unref (node);
}

static YamlNode *
int_list (const gint *values,
          guint       n)
{
	g_autoptr(YamlSequence) seq = yaml_sequence_new ();
	guint i;

	for (i = 0; i < n; i++)
	{
		g_autoptr(YamlNode) node = lrg_data_yaml_int (values[i]);

		yaml_sequence_add_element (seq, node);
	}
	return yaml_node_new_sequence (seq);
}

static YamlNode *
float_pair (gfloat a,
            gfloat b)
{
	g_autoptr(YamlSequence) seq = yaml_sequence_new ();

	{
		g_autoptr(YamlNode) node = lrg_data_yaml_float (a);

		yaml_sequence_add_element (seq, node);
	}
	{
		g_autoptr(YamlNode) node = lrg_data_yaml_float (b);

		yaml_sequence_add_element (seq, node);
	}
	return yaml_node_new_sequence (seq);
}

/* is @name the generated name of frame @index? */
static gboolean
is_auto_name (const gchar *name,
              guint        index)
{
	g_autofree gchar *expected = auto_frame_name (index);

	return g_strcmp0 (name, expected) == 0;
}

/* a frame reference: its name when it has a meaningful one, else its index */
static YamlNode *
frame_ref_node (LrgSpriteSheet *sheet,
                guint           frame)
{
	LrgAtlasRegion *region = lrg_sprite_sheet_get_frame (sheet, frame);
	const gchar *name = region != NULL ? lrg_atlas_region_get_name (region) : NULL;
	guint64 dummy;

	if (name == NULL || is_auto_name (name, frame) || parse_uint (name, &dummy))
		return lrg_data_yaml_int (frame);
	return lrg_data_yaml_string (name);
}

static YamlNode *
sheet_to_yaml (LrgSpriteSheet *sheet)
{
	g_autoptr(YamlMapping) root = yaml_mapping_new ();
	g_autoptr(YamlSequence) frames = yaml_sequence_new ();
	SheetData *sd = sheet_data (sheet);
	guint first_explicit = 0;
	guint n_frames;
	guint i;

	set_member (root, "version", lrg_data_yaml_int (1));
	if (lrg_sprite_sheet_get_name (sheet) != NULL)
		set_member (root, "name", lrg_data_yaml_string (lrg_sprite_sheet_get_name (sheet)));
	if (lrg_sprite_sheet_get_texture_path (sheet) != NULL)
		set_member (root, "image", lrg_data_yaml_string (lrg_sprite_sheet_get_texture_path (sheet)));
	if (lrg_sprite_sheet_get_texture_width (sheet) > 0 && lrg_sprite_sheet_get_texture_height (sheet) > 0)
	{
		gint size[2];

		size[0] = lrg_sprite_sheet_get_texture_width (sheet);
		size[1] = lrg_sprite_sheet_get_texture_height (sheet);
		set_member (root, "image_size", int_list (size, 2));
	}

	n_frames = lrg_sprite_sheet_get_frame_count (sheet);

	/* the grid block, then overrides of grid frames that changed */
	if (sd->has_grid && n_frames >= sd->count)
	{
		g_autoptr(YamlMapping) grid = yaml_mapping_new ();
		gint pair[2];

		pair[0] = sd->frame_w;
		pair[1] = sd->frame_h;
		set_member (grid, "frame_size", int_list (pair, 2));
		set_member (grid, "columns", lrg_data_yaml_int (sd->columns));
		set_member (grid, "rows", lrg_data_yaml_int (sd->rows));
		if (sd->count != sd->columns * sd->rows)
			set_member (grid, "count", lrg_data_yaml_int (sd->count));
		if (sd->margin_x != 0 || sd->margin_y != 0)
		{
			pair[0] = sd->margin_x;
			pair[1] = sd->margin_y;
			set_member (grid, "margin", int_list (pair, 2));
		}
		if (sd->spacing_x != 0 || sd->spacing_y != 0)
		{
			pair[0] = sd->spacing_x;
			pair[1] = sd->spacing_y;
			set_member (grid, "spacing", int_list (pair, 2));
		}
		if (sd->pivot_x != 0.0f || sd->pivot_y != 0.0f)
			set_member (grid, "pivot", float_pair (sd->pivot_x, sd->pivot_y));
		set_member (root, "grid", yaml_node_new_mapping (grid));

		for (i = 0; i < sd->count; i++)
		{
			LrgAtlasRegion *region = lrg_sprite_sheet_get_frame (sheet, i);
			gboolean name_changed = !is_auto_name (lrg_atlas_region_get_name (region), i);
			gboolean pivot_changed = lrg_atlas_region_get_pivot_x (region) != sd->pivot_x ||
			                         lrg_atlas_region_get_pivot_y (region) != sd->pivot_y;
			g_autoptr(YamlMapping) fmap = NULL;
			g_autoptr(YamlNode) node = NULL;

			if (!name_changed && !pivot_changed)
				continue;
			fmap = yaml_mapping_new ();
			set_member (fmap, "index", lrg_data_yaml_int (i));
			if (name_changed)
				set_member (fmap, "name", lrg_data_yaml_string (lrg_atlas_region_get_name (region)));
			if (pivot_changed)
				set_member (fmap, "pivot", float_pair (lrg_atlas_region_get_pivot_x (region),
				                                       lrg_atlas_region_get_pivot_y (region)));
			node = yaml_node_new_mapping (fmap);
			yaml_sequence_add_element (frames, node);
		}
		first_explicit = sd->count;
	}

	/* explicit frames */
	for (i = first_explicit; i < n_frames; i++)
	{
		LrgAtlasRegion *region = lrg_sprite_sheet_get_frame (sheet, i);
		g_autoptr(YamlMapping) fmap = yaml_mapping_new ();
		g_autoptr(YamlNode) node = NULL;
		gint rect[4];

		if (!is_auto_name (lrg_atlas_region_get_name (region), i))
			set_member (fmap, "name", lrg_data_yaml_string (lrg_atlas_region_get_name (region)));
		rect[0] = lrg_atlas_region_get_x (region);
		rect[1] = lrg_atlas_region_get_y (region);
		rect[2] = lrg_atlas_region_get_width (region);
		rect[3] = lrg_atlas_region_get_height (region);
		set_member (fmap, "rect", int_list (rect, 4));
		if (lrg_atlas_region_get_pivot_x (region) != 0.0f || lrg_atlas_region_get_pivot_y (region) != 0.0f)
			set_member (fmap, "pivot", float_pair (lrg_atlas_region_get_pivot_x (region),
			                                       lrg_atlas_region_get_pivot_y (region)));
		node = yaml_node_new_mapping (fmap);
		yaml_sequence_add_element (frames, node);
	}
	if (yaml_sequence_get_length (frames) > 0)
	{
		g_autoptr(YamlNode) node = yaml_node_new_sequence (frames);

		yaml_mapping_set_member (root, "frames", node);
	}

	/* clips */
	if (sd->clips->len > 0)
	{
		g_autoptr(YamlSequence) anims = yaml_sequence_new ();

		for (i = 0; i < sd->clips->len; i++)
		{
			LrgSpriteAnimation *clip = g_ptr_array_index (sd->clips, i);
			g_autoptr(YamlMapping) amap = yaml_mapping_new ();
			g_autoptr(YamlSequence) fseq = yaml_sequence_new ();
			g_autoptr(YamlNode) anode = NULL;
			guint k;

			set_member (amap, "name", lrg_data_yaml_string (clip->name != NULL ? clip->name : ""));
			if (clip->fps != 10.0f)
				set_member (amap, "fps", lrg_data_yaml_float (clip->fps));
			if (clip->loop_mode != LRG_SPRITE_LOOP_LOOP)
				set_member (amap, "loop", lrg_data_yaml_string (loop_values[clip->loop_mode].value_nick));

			for (k = 0; k < clip->frames->len; k++)
			{
				ClipFrame *cf = &g_array_index (clip->frames, ClipFrame, k);
				g_autoptr(YamlNode) fnode = NULL;

				if (cf->duration > 0.0f)
				{
					g_autoptr(YamlMapping) fmap = yaml_mapping_new ();

					set_member (fmap, "frame", frame_ref_node (sheet, cf->frame));
					set_member (fmap, "duration", lrg_data_yaml_float (cf->duration));
					fnode = yaml_node_new_mapping (fmap);
				}
				else
					fnode = frame_ref_node (sheet, cf->frame);
				yaml_sequence_add_element (fseq, fnode);
			}
			set_member (amap, "frames", yaml_node_new_sequence (fseq));

			if (clip->events->len > 0)
			{
				g_autoptr(YamlSequence) eseq = yaml_sequence_new ();

				for (k = 0; k < clip->events->len; k++)
				{
					ClipEvent *ev = &g_array_index (clip->events, ClipEvent, k);
					g_autoptr(YamlMapping) emap = yaml_mapping_new ();
					g_autoptr(YamlNode) enode = NULL;

					set_member (emap, "frame", lrg_data_yaml_int (ev->frame));
					set_member (emap, "name", lrg_data_yaml_string (ev->name));
					enode = yaml_node_new_mapping (emap);
					yaml_sequence_add_element (eseq, enode);
				}
				set_member (amap, "events", yaml_node_new_sequence (eseq));
			}

			if (clip->extra != NULL)
				for (k = 0; k < yaml_mapping_get_size (clip->extra); k++)
					yaml_mapping_set_member (amap, yaml_mapping_get_key (clip->extra, k),
					                         yaml_mapping_get_value (clip->extra, k));

			anode = yaml_node_new_mapping (amap);
			yaml_sequence_add_element (anims, anode);
		}
		set_member (root, "animations", yaml_node_new_sequence (anims));
	}

	if (sd->extra != NULL)
		for (i = 0; i < yaml_mapping_get_size (sd->extra); i++)
			yaml_mapping_set_member (root, yaml_mapping_get_key (sd->extra, i),
			                         yaml_mapping_get_value (sd->extra, i));

	return yaml_node_new_mapping (root);
}

gchar *
lrg_sprite_sheet_save_rsprite_to_data (LrgSpriteSheet  *self,
                                       gsize           *length,
                                       GError         **error)
{
	g_autoptr(YamlNode) root = NULL;

	g_return_val_if_fail (LRG_IS_SPRITE_SHEET (self), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	root = sheet_to_yaml (self);
	return lrg_data_yaml_to_data (root, length, error);
}

gboolean
lrg_sprite_sheet_save_rsprite_to_file (LrgSpriteSheet  *self,
                                       const gchar     *path,
                                       GError         **error)
{
	g_autoptr(YamlNode) root = NULL;
	g_autoptr(GError) local = NULL;

	g_return_val_if_fail (LRG_IS_SPRITE_SHEET (self), FALSE);
	g_return_val_if_fail (path != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	root = sheet_to_yaml (self);
	if (!lrg_data_yaml_to_file (root, path, &local))
	{
		g_set_error (error, LRG_SPRITE_ANIMATION_ERROR, LRG_SPRITE_ANIMATION_ERROR_IO,
		             "%s: %s", path, local->message);
		return FALSE;
	}
	return TRUE;
}

/* ==========================================================================
 * LrgSpriteAnimationPlayer
 * ========================================================================== */

struct _LrgSpriteAnimationPlayer
{
	LrgComponent        parent_instance;

	LrgSpriteSheet     *sheet;
	GrlTexture         *texture;
	LrgSpriteAnimation *clip;
	gfloat              time;
	gfloat              speed;
	gboolean            paused;
	gboolean            playing;
	gboolean            finished;
	gint64              step;     /* absolute step shown, see absolute_step() */
	guint               frame;    /* sheet frame shown */
};

G_DEFINE_FINAL_TYPE (LrgSpriteAnimationPlayer, lrg_sprite_animation_player, LRG_TYPE_COMPONENT)

enum
{
	PLAYER_PROP_0,
	PLAYER_PROP_SHEET,
	PLAYER_PROP_TEXTURE,
	PLAYER_PROP_SPEED,
	PLAYER_PROP_PAUSED,
	PLAYER_N_PROPS
};

static GParamSpec *player_properties[PLAYER_N_PROPS];

enum
{
	PLAYER_SIGNAL_FRAME_CHANGED,
	PLAYER_SIGNAL_EVENT,
	PLAYER_SIGNAL_FINISHED,
	PLAYER_N_SIGNALS
};

static guint player_signals[PLAYER_N_SIGNALS];

/* writes the current frame (and texture) into the owner's sprite component */
static void
player_push_to_sprite (LrgSpriteAnimationPlayer *self)
{
	LrgGameObject *owner = lrg_component_get_owner (LRG_COMPONENT (self));
	LrgSpriteComponent *sprite;
	LrgAtlasRegion *region;

	if (owner == NULL)
		return;
	sprite = LRG_SPRITE_COMPONENT (lrg_game_object_get_component (owner, LRG_TYPE_SPRITE_COMPONENT));
	if (sprite == NULL)
		return;
	if (self->texture != NULL && lrg_sprite_component_get_texture (sprite) != self->texture)
		lrg_sprite_component_set_texture (sprite, self->texture);
	region = lrg_sprite_animation_player_get_region (self);
	if (region != NULL)
		lrg_sprite_component_set_source (sprite,
		                                 lrg_atlas_region_get_x (region), lrg_atlas_region_get_y (region),
		                                 lrg_atlas_region_get_width (region),
		                                 lrg_atlas_region_get_height (region));
}

/* fires the events of clip position @clip_frame */
static void
player_fire_events (LrgSpriteAnimationPlayer *self,
                    guint                     clip_frame)
{
	guint i;

	for (i = 0; i < self->clip->events->len; i++)
	{
		ClipEvent *ev = &g_array_index (self->clip->events, ClipEvent, i);

		if (ev->frame == clip_frame)
			g_signal_emit (self, player_signals[PLAYER_SIGNAL_EVENT], 0, ev->name, clip_frame);
	}
}

/* updates the shown frame from the absolute step, emitting ::frame-changed */
static void
player_set_step (LrgSpriteAnimationPlayer *self,
                 gint64                    step)
{
	guint steps = step_count (self->clip);
	guint clip_frame;
	guint frame;

	self->step = step;
	if (steps == 0)
		return;
	clip_frame = step_frame (self->clip, (guint) (step % (gint64) steps));
	frame = g_array_index (self->clip->frames, ClipFrame, clip_frame).frame;
	if (frame != self->frame)
	{
		self->frame = frame;
		g_signal_emit (self, player_signals[PLAYER_SIGNAL_FRAME_CHANGED], 0, frame);
	}
}

void
lrg_sprite_animation_player_advance (LrgSpriteAnimationPlayer *self,
                                     gfloat                    delta)
{
	gboolean finished = FALSE;
	gint64 target;
	gint64 s;
	guint steps;
	guint fired = 0;

	g_return_if_fail (LRG_IS_SPRITE_ANIMATION_PLAYER (self));

	if (!self->playing || self->paused || self->clip == NULL || delta <= 0.0f)
		return;
	steps = step_count (self->clip);
	if (steps == 0)
		return;

	self->time += delta * self->speed;
	target = absolute_step (self->clip, self->time, &finished);

	/*
	 * Every step entered since the last advance fires its frame's events,
	 * across loops too; a huge delta is capped so a hitch cannot flood
	 * listeners (at most four cycles' worth).
	 */
	for (s = self->step + 1; s <= target; s++)
	{
		if (fired++ >= steps * 4u)
			break;
		player_fire_events (self, step_frame (self->clip, (guint) (s % (gint64) steps)));
	}
	player_set_step (self, target);
	player_push_to_sprite (self);

	if (finished && !self->finished)
	{
		self->finished = TRUE;
		self->playing = FALSE;
		g_signal_emit (self, player_signals[PLAYER_SIGNAL_FINISHED], 0, self->clip->name);
	}
}

static void
player_update (LrgComponent *component,
               gfloat        delta)
{
	lrg_sprite_animation_player_advance (LRG_SPRITE_ANIMATION_PLAYER (component), delta);
}

static void
player_attached (LrgComponent  *component,
                 LrgGameObject *owner)
{
	(void) owner;
	player_push_to_sprite (LRG_SPRITE_ANIMATION_PLAYER (component));
}

static void
lrg_sprite_animation_player_dispose (GObject *object)
{
	LrgSpriteAnimationPlayer *self = LRG_SPRITE_ANIMATION_PLAYER (object);

	g_clear_object (&self->clip);
	g_clear_object (&self->sheet);
	g_clear_object (&self->texture);

	G_OBJECT_CLASS (lrg_sprite_animation_player_parent_class)->dispose (object);
}

static void
lrg_sprite_animation_player_get_property (GObject    *object,
                                          guint       prop_id,
                                          GValue     *value,
                                          GParamSpec *pspec)
{
	LrgSpriteAnimationPlayer *self = LRG_SPRITE_ANIMATION_PLAYER (object);

	switch (prop_id)
	{
	case PLAYER_PROP_SHEET:
		g_value_set_object (value, self->sheet);
		break;
	case PLAYER_PROP_TEXTURE:
		g_value_set_object (value, self->texture);
		break;
	case PLAYER_PROP_SPEED:
		g_value_set_float (value, self->speed);
		break;
	case PLAYER_PROP_PAUSED:
		g_value_set_boolean (value, self->paused);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_sprite_animation_player_set_property (GObject      *object,
                                          guint         prop_id,
                                          const GValue *value,
                                          GParamSpec   *pspec)
{
	LrgSpriteAnimationPlayer *self = LRG_SPRITE_ANIMATION_PLAYER (object);

	switch (prop_id)
	{
	case PLAYER_PROP_SHEET:
		lrg_sprite_animation_player_set_sheet (self, g_value_get_object (value));
		break;
	case PLAYER_PROP_TEXTURE:
		lrg_sprite_animation_player_set_texture (self, g_value_get_object (value));
		break;
	case PLAYER_PROP_SPEED:
		lrg_sprite_animation_player_set_speed (self, g_value_get_float (value));
		break;
	case PLAYER_PROP_PAUSED:
		lrg_sprite_animation_player_set_paused (self, g_value_get_boolean (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_sprite_animation_player_class_init (LrgSpriteAnimationPlayerClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	LrgComponentClass *component_class = LRG_COMPONENT_CLASS (klass);

	object_class->dispose = lrg_sprite_animation_player_dispose;
	object_class->get_property = lrg_sprite_animation_player_get_property;
	object_class->set_property = lrg_sprite_animation_player_set_property;
	component_class->update = player_update;
	component_class->attached = player_attached;

	/**
	 * LrgSpriteAnimationPlayer:sheet:
	 *
	 * The sprite sheet whose clips are played.
	 */
	player_properties[PLAYER_PROP_SHEET] =
		g_param_spec_object ("sheet", NULL, NULL, LRG_TYPE_SPRITE_SHEET,
		                     G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgSpriteAnimationPlayer:texture:
	 *
	 * The sheet image; when set it is pushed to the owner's sprite
	 * component together with the frame rectangle.
	 */
	player_properties[PLAYER_PROP_TEXTURE] =
		g_param_spec_object ("texture", NULL, NULL, GRL_TYPE_TEXTURE,
		                     G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgSpriteAnimationPlayer:speed:
	 *
	 * Playback speed multiplier.
	 */
	player_properties[PLAYER_PROP_SPEED] =
		g_param_spec_float ("speed", NULL, NULL, 0.0f, 100.0f, 1.0f,
		                    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgSpriteAnimationPlayer:paused:
	 *
	 * Whether playback holds the current frame.
	 */
	player_properties[PLAYER_PROP_PAUSED] =
		g_param_spec_boolean ("paused", NULL, NULL, FALSE,
		                      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	g_object_class_install_properties (object_class, PLAYER_N_PROPS, player_properties);

	/**
	 * LrgSpriteAnimationPlayer::frame-changed:
	 * @self: the player
	 * @frame: the sheet frame index now shown
	 *
	 * Emitted when the shown sheet frame changes.
	 */
	player_signals[PLAYER_SIGNAL_FRAME_CHANGED] =
		g_signal_new ("frame-changed", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
		              0, NULL, NULL, NULL, G_TYPE_NONE, 1, G_TYPE_UINT);

	/**
	 * LrgSpriteAnimationPlayer::event:
	 * @self: the player
	 * @name: the event name
	 * @clip_frame: the clip position the event is on
	 *
	 * Emitted when playback enters a frame that carries events
	 * (lrg_sprite_animation_add_event()), including the first frame when a
	 * clip starts and every pass of a looping clip.
	 */
	player_signals[PLAYER_SIGNAL_EVENT] =
		g_signal_new ("event", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
		              0, NULL, NULL, NULL, G_TYPE_NONE, 2, G_TYPE_STRING, G_TYPE_UINT);

	/**
	 * LrgSpriteAnimationPlayer::finished:
	 * @self: the player
	 * @name: the clip that finished
	 *
	 * Emitted when a once clip reaches its end.
	 */
	player_signals[PLAYER_SIGNAL_FINISHED] =
		g_signal_new ("finished", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
		              0, NULL, NULL, NULL, G_TYPE_NONE, 1, G_TYPE_STRING);
}

static void
lrg_sprite_animation_player_init (LrgSpriteAnimationPlayer *self)
{
	self->speed = 1.0f;
}

LrgSpriteAnimationPlayer *
lrg_sprite_animation_player_new (LrgSpriteSheet *sheet)
{
	return g_object_new (LRG_TYPE_SPRITE_ANIMATION_PLAYER, "sheet", sheet, NULL);
}

LrgSpriteSheet *
lrg_sprite_animation_player_get_sheet (LrgSpriteAnimationPlayer *self)
{
	g_return_val_if_fail (LRG_IS_SPRITE_ANIMATION_PLAYER (self), NULL);
	return self->sheet;
}

void
lrg_sprite_animation_player_set_sheet (LrgSpriteAnimationPlayer *self,
                                       LrgSpriteSheet           *sheet)
{
	g_return_if_fail (LRG_IS_SPRITE_ANIMATION_PLAYER (self));
	g_return_if_fail (sheet == NULL || LRG_IS_SPRITE_SHEET (sheet));

	if (self->sheet == sheet)
		return;
	lrg_sprite_animation_player_stop (self);
	g_clear_object (&self->clip);
	g_set_object (&self->sheet, sheet);
	g_object_notify_by_pspec (G_OBJECT (self), player_properties[PLAYER_PROP_SHEET]);
}

GrlTexture *
lrg_sprite_animation_player_get_texture (LrgSpriteAnimationPlayer *self)
{
	g_return_val_if_fail (LRG_IS_SPRITE_ANIMATION_PLAYER (self), NULL);
	return self->texture;
}

void
lrg_sprite_animation_player_set_texture (LrgSpriteAnimationPlayer *self,
                                         GrlTexture               *texture)
{
	g_return_if_fail (LRG_IS_SPRITE_ANIMATION_PLAYER (self));
	g_return_if_fail (texture == NULL || GRL_IS_TEXTURE (texture));

	if (g_set_object (&self->texture, texture))
	{
		g_object_notify_by_pspec (G_OBJECT (self), player_properties[PLAYER_PROP_TEXTURE]);
		player_push_to_sprite (self);
	}
}

gboolean
lrg_sprite_animation_player_play (LrgSpriteAnimationPlayer *self,
                                  const gchar              *name)
{
	LrgSpriteAnimation *clip;

	g_return_val_if_fail (LRG_IS_SPRITE_ANIMATION_PLAYER (self), FALSE);
	g_return_val_if_fail (name != NULL, FALSE);

	if (self->sheet == NULL)
		return FALSE;
	clip = lrg_sprite_sheet_get_clip (self->sheet, name);
	if (clip == NULL)
		return FALSE;

	g_set_object (&self->clip, clip);
	self->time = 0.0f;
	self->playing = TRUE;
	self->finished = FALSE;
	self->frame = G_MAXUINT;   /* forces ::frame-changed for the first frame */
	if (clip->frames->len > 0)
	{
		player_set_step (self, 0);
		player_fire_events (self, 0);
	}
	player_push_to_sprite (self);
	return TRUE;
}

gboolean
lrg_sprite_animation_player_play_if_different (LrgSpriteAnimationPlayer *self,
                                               const gchar              *name)
{
	g_return_val_if_fail (LRG_IS_SPRITE_ANIMATION_PLAYER (self), FALSE);
	g_return_val_if_fail (name != NULL, FALSE);

	if (self->clip != NULL && g_strcmp0 (self->clip->name, name) == 0 && (self->playing || self->finished))
		return self->playing;
	return lrg_sprite_animation_player_play (self, name);
}

void
lrg_sprite_animation_player_stop (LrgSpriteAnimationPlayer *self)
{
	g_return_if_fail (LRG_IS_SPRITE_ANIMATION_PLAYER (self));

	self->playing = FALSE;
	self->finished = FALSE;
	self->time = 0.0f;
	self->step = 0;
	if (self->clip != NULL && self->clip->frames->len > 0)
		self->frame = g_array_index (self->clip->frames, ClipFrame, 0).frame;
}

void
lrg_sprite_animation_player_set_paused (LrgSpriteAnimationPlayer *self,
                                        gboolean                  paused)
{
	g_return_if_fail (LRG_IS_SPRITE_ANIMATION_PLAYER (self));

	paused = !!paused;
	if (self->paused == paused)
		return;
	self->paused = paused;
	g_object_notify_by_pspec (G_OBJECT (self), player_properties[PLAYER_PROP_PAUSED]);
}

gboolean
lrg_sprite_animation_player_get_paused (LrgSpriteAnimationPlayer *self)
{
	g_return_val_if_fail (LRG_IS_SPRITE_ANIMATION_PLAYER (self), FALSE);
	return self->paused;
}

gboolean
lrg_sprite_animation_player_is_playing (LrgSpriteAnimationPlayer *self)
{
	g_return_val_if_fail (LRG_IS_SPRITE_ANIMATION_PLAYER (self), FALSE);
	return self->playing;
}

LrgSpriteAnimation *
lrg_sprite_animation_player_get_current_clip (LrgSpriteAnimationPlayer *self)
{
	g_return_val_if_fail (LRG_IS_SPRITE_ANIMATION_PLAYER (self), NULL);
	return self->clip;
}

gfloat
lrg_sprite_animation_player_get_time (LrgSpriteAnimationPlayer *self)
{
	g_return_val_if_fail (LRG_IS_SPRITE_ANIMATION_PLAYER (self), 0.0f);
	return self->time;
}

guint
lrg_sprite_animation_player_get_frame (LrgSpriteAnimationPlayer *self)
{
	g_return_val_if_fail (LRG_IS_SPRITE_ANIMATION_PLAYER (self), 0);
	if (self->clip == NULL || self->frame == G_MAXUINT)
		return 0;
	return self->frame;
}

LrgAtlasRegion *
lrg_sprite_animation_player_get_region (LrgSpriteAnimationPlayer *self)
{
	g_return_val_if_fail (LRG_IS_SPRITE_ANIMATION_PLAYER (self), NULL);

	if (self->sheet == NULL || self->clip == NULL || self->clip->frames->len == 0)
		return NULL;
	return lrg_sprite_sheet_get_frame (self->sheet, lrg_sprite_animation_player_get_frame (self));
}

gfloat
lrg_sprite_animation_player_get_speed (LrgSpriteAnimationPlayer *self)
{
	g_return_val_if_fail (LRG_IS_SPRITE_ANIMATION_PLAYER (self), 1.0f);
	return self->speed;
}

void
lrg_sprite_animation_player_set_speed (LrgSpriteAnimationPlayer *self,
                                       gfloat                    speed)
{
	g_return_if_fail (LRG_IS_SPRITE_ANIMATION_PLAYER (self));
	g_return_if_fail (speed >= 0.0f);

	if (self->speed == speed)
		return;
	self->speed = speed;
	g_object_notify_by_pspec (G_OBJECT (self), player_properties[PLAYER_PROP_SPEED]);
}
