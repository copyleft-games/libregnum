/* lrg-input-inject.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgInputInject: decodes host input messages, applies them to an
 * LrgInputSoftware and re-emits them as signals.
 */

#include "config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_INPUT

#include "lrg-input-inject.h"
#include "lrg-frame-wire-private.h"
#include "../lrg-log.h"

#ifdef G_OS_UNIX
#include <unistd.h>
#endif

#define INJECT_BUTTONS (8)

struct _LrgInputInject
{
	GObject           parent_instance;

	LrgFrameReader    reader;
	LrgInputSoftware *target;

	gfloat            pointer_x;
	gfloat            pointer_y;
	gboolean          buttons[INJECT_BUTTONS];
	gfloat            scroll_x;
	gfloat            scroll_y;
	gboolean          have_size;
	guint             width;
	guint             height;
	guint             modifiers;
};

G_DEFINE_FINAL_TYPE (LrgInputInject, lrg_input_inject, G_TYPE_OBJECT)

enum
{
	SIGNAL_POINTER_MOTION,
	SIGNAL_BUTTON,
	SIGNAL_SCROLL,
	SIGNAL_KEY,
	SIGNAL_RESIZE,
	N_SIGNALS
};

static guint signals[N_SIGNALS];

static void
lrg_input_inject_finalize (GObject *object)
{
	LrgInputInject *self = LRG_INPUT_INJECT (object);

	lrg_frame_reader_clear (&self->reader);
	g_clear_object (&self->target);
	G_OBJECT_CLASS (lrg_input_inject_parent_class)->finalize (object);
}

static void
lrg_input_inject_class_init (LrgInputInjectClass *klass)
{
	GType type = G_TYPE_FROM_CLASS (klass);

	G_OBJECT_CLASS (klass)->finalize = lrg_input_inject_finalize;

	/**
	 * LrgInputInject::pointer-motion:
	 * @self: the injector
	 * @x: x in frame pixels
	 * @y: y in frame pixels
	 *
	 * The host moved the pointer.
	 *
	 * Since: 0.11.0
	 */
	signals[SIGNAL_POINTER_MOTION] = g_signal_new ("pointer-motion", type, G_SIGNAL_RUN_LAST,
	                                               0, NULL, NULL, NULL, G_TYPE_NONE, 2,
	                                               G_TYPE_FLOAT, G_TYPE_FLOAT);

	/**
	 * LrgInputInject::button:
	 * @self: the injector
	 * @button: 0 left, 1 right, 2 middle
	 * @pressed: %TRUE for a press
	 * @x: x in frame pixels
	 * @y: y in frame pixels
	 *
	 * The host pressed or released a pointer button.
	 *
	 * Since: 0.11.0
	 */
	signals[SIGNAL_BUTTON] = g_signal_new ("button", type, G_SIGNAL_RUN_LAST, 0, NULL, NULL,
	                                       NULL, G_TYPE_NONE, 4, G_TYPE_UINT, G_TYPE_BOOLEAN,
	                                       G_TYPE_FLOAT, G_TYPE_FLOAT);

	/**
	 * LrgInputInject::scroll:
	 * @self: the injector
	 * @dx: horizontal steps
	 * @dy: vertical steps, positive away from the user
	 *
	 * The host scrolled.
	 *
	 * Since: 0.11.0
	 */
	signals[SIGNAL_SCROLL] = g_signal_new ("scroll", type, G_SIGNAL_RUN_LAST, 0, NULL, NULL,
	                                       NULL, G_TYPE_NONE, 2, G_TYPE_FLOAT, G_TYPE_FLOAT);

	/**
	 * LrgInputInject::key:
	 * @self: the injector
	 * @key: the #GrlKey (0 for text only)
	 * @pressed: %TRUE for a press
	 * @modifiers: #LrgFrameModifier bits
	 * @codepoint: the character typed, 0 for none
	 *
	 * The host pressed or released a key.
	 *
	 * Since: 0.11.0
	 */
	signals[SIGNAL_KEY] = g_signal_new ("key", type, G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
	                                    G_TYPE_NONE, 4, G_TYPE_UINT, G_TYPE_BOOLEAN, G_TYPE_UINT,
	                                    G_TYPE_UINT);

	/**
	 * LrgInputInject::resize:
	 * @self: the injector
	 * @width: the width the host wants
	 * @height: the height
	 *
	 * The host wants frames at a new size; pass it to
	 * lrg_frame_export_set_size().
	 *
	 * Since: 0.11.0
	 */
	signals[SIGNAL_RESIZE] = g_signal_new ("resize", type, G_SIGNAL_RUN_LAST, 0, NULL, NULL,
	                                       NULL, G_TYPE_NONE, 2, G_TYPE_UINT, G_TYPE_UINT);
}

static void
lrg_input_inject_init (LrgInputInject *self)
{
}

LrgInputInject *
lrg_input_inject_new (gint fd)
{
	LrgInputInject *self;

	g_return_val_if_fail (fd >= 0, NULL);

	self = g_object_new (LRG_TYPE_INPUT_INJECT, NULL);
	lrg_frame_reader_init (&self->reader, fd);
	return self;
}

void
lrg_input_inject_set_target (LrgInputInject   *self,
                             LrgInputSoftware *target)
{
	g_return_if_fail (LRG_IS_INPUT_INJECT (self));
	g_return_if_fail (target == NULL || LRG_IS_INPUT_SOFTWARE (target));
	g_set_object (&self->target, target);
}

LrgInputSoftware *
lrg_input_inject_get_target (LrgInputInject *self)
{
	g_return_val_if_fail (LRG_IS_INPUT_INJECT (self), NULL);
	return self->target;
}

static void
handle (LrgInputInject        *self,
        const LrgFrameMessage *msg)
{
	const guint8 *b = msg->body;

	switch ((gint) msg->type)
	{
	case LRG_FRAME_MSG_POINTER_MOVE:
		if (msg->body_size < 8)
			return;
		self->pointer_x = lrg_frame_get_f32 (b);
		self->pointer_y = lrg_frame_get_f32 (b + 4);
		if (self->target != NULL)
			lrg_input_software_move_mouse_to (self->target, self->pointer_x, self->pointer_y);
		g_signal_emit (self, signals[SIGNAL_POINTER_MOTION], 0, self->pointer_x, self->pointer_y);
		break;

	case LRG_FRAME_MSG_POINTER_BUTTON:
	{
		guint button;
		gboolean pressed;

		if (msg->body_size < 16)
			return;
		button = lrg_frame_get_u32 (b);
		pressed = lrg_frame_get_u32 (b + 4) != 0;
		self->pointer_x = lrg_frame_get_f32 (b + 8);
		self->pointer_y = lrg_frame_get_f32 (b + 12);
		if (button < INJECT_BUTTONS)
			self->buttons[button] = pressed;
		if (self->target != NULL)
		{
			/* the press lands where the host clicked */
			lrg_input_software_move_mouse_to (self->target, self->pointer_x, self->pointer_y);
			if (button <= GRL_MOUSE_BUTTON_MIDDLE)
			{
				if (pressed)
					lrg_input_software_press_mouse_button (self->target, (GrlMouseButton) button);
				else
					lrg_input_software_release_mouse_button (self->target, (GrlMouseButton) button);
			}
		}
		g_signal_emit (self, signals[SIGNAL_BUTTON], 0, button, pressed, self->pointer_x,
		               self->pointer_y);
		break;
	}

	case LRG_FRAME_MSG_SCROLL:
	{
		gfloat dx;
		gfloat dy;

		if (msg->body_size < 8)
			return;
		dx = lrg_frame_get_f32 (b);
		dy = lrg_frame_get_f32 (b + 4);
		self->scroll_x += dx;
		self->scroll_y += dy;
		g_signal_emit (self, signals[SIGNAL_SCROLL], 0, dx, dy);
		break;
	}

	case LRG_FRAME_MSG_KEY:
	{
		guint key;
		gboolean pressed;
		guint codepoint;

		if (msg->body_size < 16)
			return;
		key = lrg_frame_get_u32 (b);
		pressed = lrg_frame_get_u32 (b + 4) != 0;
		self->modifiers = lrg_frame_get_u32 (b + 8);
		codepoint = lrg_frame_get_u32 (b + 12);
		if (self->target != NULL && key != 0)
		{
			if (pressed)
				lrg_input_software_press_key (self->target, (GrlKey) key);
			else
				lrg_input_software_release_key (self->target, (GrlKey) key);
		}
		g_signal_emit (self, signals[SIGNAL_KEY], 0, key, pressed, self->modifiers, codepoint);
		break;
	}

	case LRG_FRAME_MSG_RESIZE:
		if (msg->body_size < 8)
			return;
	{
		guint w = lrg_frame_get_u32 (b);
		guint h = lrg_frame_get_u32 (b + 4);

		/* a size nothing can render at is ignored, not applied */
		if (w == 0 || h == 0 || w > LRG_FRAME_MAX_DIMENSION || h > LRG_FRAME_MAX_DIMENSION)
			return;
		self->width = w;
		self->height = h;
		self->have_size = TRUE;
		g_signal_emit (self, signals[SIGNAL_RESIZE], 0, self->width, self->height);
		break;
	}

	default:
		/* frames, hellos and future types are not input */
		break;
	}
}

gboolean
lrg_input_inject_dispatch (LrgInputInject  *self,
                           GError         **error)
{
	LrgFrameMessage msg;
	g_autoptr(GError) local = NULL;

	g_return_val_if_fail (LRG_IS_INPUT_INJECT (self), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	if (!lrg_frame_reader_fill (&self->reader, error))
		return FALSE;
	while (lrg_frame_reader_next (&self->reader, &msg, &local))
	{
		handle (self, &msg);
#ifdef G_OS_UNIX
		if (msg.fd >= 0)
			close (msg.fd);
#endif
	}
	if (local != NULL)
	{
		g_propagate_error (error, g_steal_pointer (&local));
		return FALSE;
	}
	if (self->reader.eof)
	{
		g_set_error (error, LRG_FRAME_ERROR, LRG_FRAME_ERROR_CLOSED,
		             "the host closed the connection");
		return FALSE;
	}
	return TRUE;
}

void
lrg_input_inject_get_pointer (LrgInputInject *self,
                              gfloat         *x,
                              gfloat         *y)
{
	g_return_if_fail (LRG_IS_INPUT_INJECT (self));
	if (x != NULL)
		*x = self->pointer_x;
	if (y != NULL)
		*y = self->pointer_y;
}

gboolean
lrg_input_inject_is_button_down (LrgInputInject *self,
                                 guint           button)
{
	g_return_val_if_fail (LRG_IS_INPUT_INJECT (self), FALSE);
	return button < INJECT_BUTTONS && self->buttons[button];
}

void
lrg_input_inject_take_scroll (LrgInputInject *self,
                              gfloat         *dx,
                              gfloat         *dy)
{
	g_return_if_fail (LRG_IS_INPUT_INJECT (self));
	if (dx != NULL)
		*dx = self->scroll_x;
	if (dy != NULL)
		*dy = self->scroll_y;
	self->scroll_x = 0.0f;
	self->scroll_y = 0.0f;
}

gboolean
lrg_input_inject_get_size (LrgInputInject *self,
                           guint          *width,
                           guint          *height)
{
	g_return_val_if_fail (LRG_IS_INPUT_INJECT (self), FALSE);

	if (!self->have_size)
		return FALSE;
	if (width != NULL)
		*width = self->width;
	if (height != NULL)
		*height = self->height;
	return TRUE;
}

guint
lrg_input_inject_get_modifiers (LrgInputInject *self)
{
	g_return_val_if_fail (LRG_IS_INPUT_INJECT (self), 0);
	return self->modifiers;
}
