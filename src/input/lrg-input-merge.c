/* lrg-input-merge.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Several inputs read as one (lrg-input-merge.h has the rules).  Every
 * query walks the enabled parts in order; the helpers below are the
 * three ways answers combine: any (booleans), largest magnitude (axes)
 * and first (names, ids, the mouse position).
 */

#include "config.h"
#include "lrg-input-merge.h"

#include <math.h>

struct _LrgInputMerge
{
	LrgInput   parent_instance;

	GPtrArray *parts;   /* LrgInput *, owned */
};

G_DEFINE_FINAL_TYPE (LrgInputMerge, lrg_input_merge, LRG_TYPE_INPUT)

/* the @i-th part when it is enabled, else NULL */
static LrgInput *
enabled_part (LrgInputMerge *self,
              guint          i)
{
	LrgInput *part;

	part = g_ptr_array_index (self->parts, i);
	return lrg_input_get_enabled (part) ? part : NULL;
}

/* ==========================================================================
 * LrgInput
 * ========================================================================== */

static void
lrg_input_merge_poll (LrgInput *input)
{
	LrgInputMerge *self = LRG_INPUT_MERGE (input);
	guint i;

	for (i = 0; i < self->parts->len; i++)
	{
		LrgInput *part = enabled_part (self, i);

		if (part != NULL)
			lrg_input_poll (part);
	}
}

/* "any part": one macro body per boolean query keeps the eleven of them
 * identical; ARGS is the argument list after the part */
#define MERGE_ANY(self, QUERY, ARGS) \
	G_STMT_START { \
		guint i_; \
		for (i_ = 0; i_ < (self)->parts->len; i_++) \
		{ \
			LrgInput *part_ = enabled_part ((self), i_); \
			if (part_ != NULL && QUERY ARGS) \
				return TRUE; \
		} \
		return FALSE; \
	} G_STMT_END

static gboolean
lrg_input_merge_is_key_pressed (LrgInput *input,
                                GrlKey    key)
{
	MERGE_ANY (LRG_INPUT_MERGE (input), lrg_input_is_key_pressed, (part_, key));
}

static gboolean
lrg_input_merge_is_key_down (LrgInput *input,
                             GrlKey    key)
{
	MERGE_ANY (LRG_INPUT_MERGE (input), lrg_input_is_key_down, (part_, key));
}

static gboolean
lrg_input_merge_is_key_released (LrgInput *input,
                                 GrlKey    key)
{
	MERGE_ANY (LRG_INPUT_MERGE (input), lrg_input_is_key_released, (part_, key));
}

static gboolean
lrg_input_merge_is_mouse_button_pressed (LrgInput       *input,
                                         GrlMouseButton  button)
{
	MERGE_ANY (LRG_INPUT_MERGE (input), lrg_input_is_mouse_button_pressed, (part_, button));
}

static gboolean
lrg_input_merge_is_mouse_button_down (LrgInput       *input,
                                      GrlMouseButton  button)
{
	MERGE_ANY (LRG_INPUT_MERGE (input), lrg_input_is_mouse_button_down, (part_, button));
}

static gboolean
lrg_input_merge_is_mouse_button_released (LrgInput       *input,
                                          GrlMouseButton  button)
{
	MERGE_ANY (LRG_INPUT_MERGE (input), lrg_input_is_mouse_button_released, (part_, button));
}

static gboolean
lrg_input_merge_is_gamepad_available (LrgInput *input,
                                      gint      gamepad)
{
	MERGE_ANY (LRG_INPUT_MERGE (input), lrg_input_is_gamepad_available, (part_, gamepad));
}

static gboolean
lrg_input_merge_is_gamepad_button_pressed (LrgInput         *input,
                                           gint              gamepad,
                                           GrlGamepadButton  button)
{
	MERGE_ANY (LRG_INPUT_MERGE (input), lrg_input_is_gamepad_button_pressed, (part_, gamepad, button));
}

static gboolean
lrg_input_merge_is_gamepad_button_down (LrgInput         *input,
                                        gint              gamepad,
                                        GrlGamepadButton  button)
{
	MERGE_ANY (LRG_INPUT_MERGE (input), lrg_input_is_gamepad_button_down, (part_, gamepad, button));
}

static gboolean
lrg_input_merge_is_gamepad_button_released (LrgInput         *input,
                                            gint              gamepad,
                                            GrlGamepadButton  button)
{
	MERGE_ANY (LRG_INPUT_MERGE (input), lrg_input_is_gamepad_button_released, (part_, gamepad, button));
}

#undef MERGE_ANY

/* the mouse position of the first part that reports one */
static void
lrg_input_merge_get_mouse_position (LrgInput *input,
                                    gfloat   *x,
                                    gfloat   *y)
{
	LrgInputMerge *self = LRG_INPUT_MERGE (input);
	guint i;

	*x = 0.0f;
	*y = 0.0f;
	for (i = 0; i < self->parts->len; i++)
	{
		LrgInput *part = enabled_part (self, i);
		gfloat px;
		gfloat py;

		if (part == NULL)
			continue;
		px = 0.0f;
		py = 0.0f;
		lrg_input_get_mouse_position (part, &px, &py);
		if (px != 0.0f || py != 0.0f)
		{
			*x = px;
			*y = py;
			return;
		}
	}
}

/* every part's motion this frame, added */
static void
lrg_input_merge_get_mouse_delta (LrgInput *input,
                                 gfloat   *dx,
                                 gfloat   *dy)
{
	LrgInputMerge *self = LRG_INPUT_MERGE (input);
	guint i;

	*dx = 0.0f;
	*dy = 0.0f;
	for (i = 0; i < self->parts->len; i++)
	{
		LrgInput *part = enabled_part (self, i);
		gfloat px;
		gfloat py;

		if (part == NULL)
			continue;
		px = 0.0f;
		py = 0.0f;
		lrg_input_get_mouse_delta (part, &px, &py);
		*dx += px;
		*dy += py;
	}
}

/* the value furthest from rest; ties keep the earlier part's */
static gfloat
lrg_input_merge_get_gamepad_axis (LrgInput       *input,
                                  gint            gamepad,
                                  GrlGamepadAxis  axis)
{
	LrgInputMerge *self = LRG_INPUT_MERGE (input);
	gfloat best;
	guint i;

	best = 0.0f;
	for (i = 0; i < self->parts->len; i++)
	{
		LrgInput *part = enabled_part (self, i);
		gfloat value;

		if (part == NULL || !lrg_input_is_gamepad_available (part, gamepad))
			continue;
		value = lrg_input_get_gamepad_axis (part, gamepad, axis);
		if (fabsf (value) > fabsf (best))
			best = value;
	}
	return best;
}

/* the first part that has the gamepad names it */
static const gchar *
lrg_input_merge_get_gamepad_name (LrgInput *input,
                                  gint      gamepad)
{
	LrgInputMerge *self = LRG_INPUT_MERGE (input);
	guint i;

	for (i = 0; i < self->parts->len; i++)
	{
		LrgInput *part = enabled_part (self, i);

		if (part != NULL && lrg_input_is_gamepad_available (part, gamepad))
			return lrg_input_get_gamepad_name (part, gamepad);
	}
	return NULL;
}

static gboolean
lrg_input_merge_get_gamepad_ids (LrgInput *input,
                                 gint      gamepad,
                                 guint16  *vendor,
                                 guint16  *product)
{
	LrgInputMerge *self = LRG_INPUT_MERGE (input);
	guint i;

	for (i = 0; i < self->parts->len; i++)
	{
		LrgInput *part = enabled_part (self, i);

		if (part != NULL && lrg_input_is_gamepad_available (part, gamepad))
			return lrg_input_get_gamepad_ids (part, gamepad, vendor, product);
	}
	return FALSE;
}

/* ==========================================================================
 * GObject
 * ========================================================================== */

static void
lrg_input_merge_finalize (GObject *object)
{
	LrgInputMerge *self = LRG_INPUT_MERGE (object);

	g_clear_pointer (&self->parts, g_ptr_array_unref);

	G_OBJECT_CLASS (lrg_input_merge_parent_class)->finalize (object);
}

static void
lrg_input_merge_class_init (LrgInputMergeClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	LrgInputClass *input_class = LRG_INPUT_CLASS (klass);

	object_class->finalize = lrg_input_merge_finalize;

	input_class->poll                       = lrg_input_merge_poll;
	input_class->is_key_pressed             = lrg_input_merge_is_key_pressed;
	input_class->is_key_down                = lrg_input_merge_is_key_down;
	input_class->is_key_released            = lrg_input_merge_is_key_released;
	input_class->is_mouse_button_pressed    = lrg_input_merge_is_mouse_button_pressed;
	input_class->is_mouse_button_down       = lrg_input_merge_is_mouse_button_down;
	input_class->is_mouse_button_released   = lrg_input_merge_is_mouse_button_released;
	input_class->get_mouse_position         = lrg_input_merge_get_mouse_position;
	input_class->get_mouse_delta            = lrg_input_merge_get_mouse_delta;
	input_class->is_gamepad_available       = lrg_input_merge_is_gamepad_available;
	input_class->is_gamepad_button_pressed  = lrg_input_merge_is_gamepad_button_pressed;
	input_class->is_gamepad_button_down     = lrg_input_merge_is_gamepad_button_down;
	input_class->is_gamepad_button_released = lrg_input_merge_is_gamepad_button_released;
	input_class->get_gamepad_axis           = lrg_input_merge_get_gamepad_axis;
	input_class->get_gamepad_name           = lrg_input_merge_get_gamepad_name;
	input_class->get_gamepad_ids            = lrg_input_merge_get_gamepad_ids;
}

static void
lrg_input_merge_init (LrgInputMerge *self)
{
	self->parts = g_ptr_array_new_with_free_func (g_object_unref);
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

LrgInputMerge *
lrg_input_merge_new (void)
{
	return g_object_new (LRG_TYPE_INPUT_MERGE, "name", "merge", NULL);
}

void
lrg_input_merge_add (LrgInputMerge *self,
                     LrgInput      *part)
{
	g_return_if_fail (LRG_IS_INPUT_MERGE (self));
	g_return_if_fail (LRG_IS_INPUT (part));

	if (part == LRG_INPUT (self) || g_ptr_array_find (self->parts, part, NULL))
		return;
	g_ptr_array_add (self->parts, g_object_ref (part));
}

gboolean
lrg_input_merge_remove (LrgInputMerge *self,
                        LrgInput      *part)
{
	g_return_val_if_fail (LRG_IS_INPUT_MERGE (self), FALSE);

	return g_ptr_array_remove (self->parts, part);
}

guint
lrg_input_merge_get_n_parts (LrgInputMerge *self)
{
	g_return_val_if_fail (LRG_IS_INPUT_MERGE (self), 0);

	return self->parts->len;
}

LrgInput *
lrg_input_merge_get_part (LrgInputMerge *self,
                          guint          index)
{
	g_return_val_if_fail (LRG_IS_INPUT_MERGE (self), NULL);

	if (index >= self->parts->len)
		return NULL;
	return g_ptr_array_index (self->parts, index);
}
