/* lrg-gui-draw-list.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Recorded 2D drawing commands. See lrg-gui-draw-list.h.
 */

#include "lrg-gui-draw-list.h"
#include <string.h>

struct _LrgGuiDrawList
{
	GObject  parent_instance;

	GArray  *commands;   /* LrgGuiCommand */
};

G_DEFINE_FINAL_TYPE (LrgGuiDrawList, lrg_gui_draw_list, G_TYPE_OBJECT)

/* Releases what a command owns (text, texture ref). */
static void
command_clear (gpointer data)
{
	LrgGuiCommand *cmd = data;

	g_clear_pointer (&cmd->text, g_free);
	g_clear_object (&cmd->texture);
}

static void
lrg_gui_draw_list_finalize (GObject *object)
{
	LrgGuiDrawList *self = LRG_GUI_DRAW_LIST (object);

	g_array_unref (self->commands);

	G_OBJECT_CLASS (lrg_gui_draw_list_parent_class)->finalize (object);
}

static void
lrg_gui_draw_list_class_init (LrgGuiDrawListClass *klass)
{
	G_OBJECT_CLASS (klass)->finalize = lrg_gui_draw_list_finalize;
}

static void
lrg_gui_draw_list_init (LrgGuiDrawList *self)
{
	self->commands = g_array_sized_new (FALSE, TRUE, sizeof (LrgGuiCommand), 512);
	g_array_set_clear_func (self->commands, command_clear);
}

LrgGuiDrawList *
lrg_gui_draw_list_new (void)
{
	return g_object_new (LRG_TYPE_GUI_DRAW_LIST, NULL);
}

void
lrg_gui_draw_list_clear (LrgGuiDrawList *self)
{
	g_return_if_fail (LRG_IS_GUI_DRAW_LIST (self));

	g_array_set_size (self->commands, 0);
}

guint
lrg_gui_draw_list_get_length (LrgGuiDrawList *self)
{
	g_return_val_if_fail (LRG_IS_GUI_DRAW_LIST (self), 0);

	return self->commands->len;
}

const LrgGuiCommand *
lrg_gui_draw_list_get_command (LrgGuiDrawList *self,
                               guint           index)
{
	g_return_val_if_fail (LRG_IS_GUI_DRAW_LIST (self), NULL);

	if (index >= self->commands->len)
		return NULL;

	return &g_array_index (self->commands, LrgGuiCommand, index);
}

/* Appends a zeroed command of @kind and returns it for filling in. */
static LrgGuiCommand *
push (LrgGuiDrawList    *self,
      LrgGuiCommandKind  kind)
{
	LrgGuiCommand cmd;

	memset (&cmd, 0, sizeof cmd);
	cmd.kind = kind;
	g_array_append_val (self->commands, cmd);
	return &g_array_index (self->commands, LrgGuiCommand, self->commands->len - 1);
}

guint
lrg_gui_draw_list_rect_filled (LrgGuiDrawList   *self,
                               const LrgGuiRect *rect,
                               LrgGuiColor       color,
                               gfloat            rounding)
{
	LrgGuiCommand *cmd;

	g_return_val_if_fail (LRG_IS_GUI_DRAW_LIST (self), 0);
	g_return_val_if_fail (rect != NULL, 0);

	cmd = push (self, LRG_GUI_CMD_RECT_FILLED);
	cmd->rect = *rect;
	cmd->color = color;
	cmd->rounding = rounding;
	return self->commands->len - 1;
}

guint
lrg_gui_draw_list_rect_outline (LrgGuiDrawList   *self,
                                const LrgGuiRect *rect,
                                LrgGuiColor       color,
                                gfloat            thickness,
                                gfloat            rounding)
{
	LrgGuiCommand *cmd;

	g_return_val_if_fail (LRG_IS_GUI_DRAW_LIST (self), 0);
	g_return_val_if_fail (rect != NULL, 0);

	cmd = push (self, LRG_GUI_CMD_RECT_OUTLINE);
	cmd->rect = *rect;
	cmd->color = color;
	cmd->thickness = thickness;
	cmd->rounding = rounding;
	return self->commands->len - 1;
}

void
lrg_gui_draw_list_line (LrgGuiDrawList *self,
                        gfloat          x0,
                        gfloat          y0,
                        gfloat          x1,
                        gfloat          y1,
                        LrgGuiColor     color,
                        gfloat          thickness)
{
	LrgGuiCommand *cmd;

	g_return_if_fail (LRG_IS_GUI_DRAW_LIST (self));

	cmd = push (self, LRG_GUI_CMD_LINE);
	cmd->points[0] = x0;
	cmd->points[1] = y0;
	cmd->points[2] = x1;
	cmd->points[3] = y1;
	cmd->color = color;
	cmd->thickness = thickness;
}

void
lrg_gui_draw_list_triangle (LrgGuiDrawList *self,
                            gfloat          x0,
                            gfloat          y0,
                            gfloat          x1,
                            gfloat          y1,
                            gfloat          x2,
                            gfloat          y2,
                            LrgGuiColor     color)
{
	LrgGuiCommand *cmd;

	g_return_if_fail (LRG_IS_GUI_DRAW_LIST (self));

	cmd = push (self, LRG_GUI_CMD_TRIANGLE);
	cmd->points[0] = x0;
	cmd->points[1] = y0;
	cmd->points[2] = x1;
	cmd->points[3] = y1;
	cmd->points[4] = x2;
	cmd->points[5] = y2;
	cmd->color = color;
}

void
lrg_gui_draw_list_circle (LrgGuiDrawList *self,
                          gfloat          cx,
                          gfloat          cy,
                          gfloat          radius,
                          LrgGuiColor     color,
                          gboolean        filled)
{
	LrgGuiCommand *cmd;

	g_return_if_fail (LRG_IS_GUI_DRAW_LIST (self));

	cmd = push (self, filled ? LRG_GUI_CMD_CIRCLE_FILLED : LRG_GUI_CMD_CIRCLE_OUTLINE);
	cmd->rect.x = cx;
	cmd->rect.y = cy;
	cmd->thickness = radius;
	cmd->color = color;
}

void
lrg_gui_draw_list_text (LrgGuiDrawList *self,
                        gfloat          x,
                        gfloat          y,
                        const gchar    *text,
                        gssize          length,
                        gfloat          font_size,
                        LrgGuiColor     color,
                        gboolean        monospace)
{
	LrgGuiCommand *cmd;

	g_return_if_fail (LRG_IS_GUI_DRAW_LIST (self));
	g_return_if_fail (text != NULL);

	if (length == 0 || *text == '\0')
		return;

	cmd = push (self, LRG_GUI_CMD_TEXT);
	cmd->rect.x = x;
	cmd->rect.y = y;
	cmd->text = (length < 0) ? g_strdup (text) : g_strndup (text, length);
	cmd->font_size = font_size;
	cmd->color = color;
	cmd->monospace = monospace;
}

void
lrg_gui_draw_list_push_clip (LrgGuiDrawList   *self,
                             const LrgGuiRect *rect)
{
	LrgGuiCommand *cmd;

	g_return_if_fail (LRG_IS_GUI_DRAW_LIST (self));
	g_return_if_fail (rect != NULL);

	cmd = push (self, LRG_GUI_CMD_CLIP_PUSH);
	cmd->rect = *rect;
}

void
lrg_gui_draw_list_pop_clip (LrgGuiDrawList *self)
{
	g_return_if_fail (LRG_IS_GUI_DRAW_LIST (self));

	push (self, LRG_GUI_CMD_CLIP_POP);
}

void
lrg_gui_draw_list_image (LrgGuiDrawList   *self,
                         const LrgGuiRect *rect,
                         GObject          *texture,
                         LrgGuiColor       tint)
{
	LrgGuiCommand *cmd;

	g_return_if_fail (LRG_IS_GUI_DRAW_LIST (self));
	g_return_if_fail (rect != NULL);
	g_return_if_fail (G_IS_OBJECT (texture));

	cmd = push (self, LRG_GUI_CMD_IMAGE);
	cmd->rect = *rect;
	cmd->texture = g_object_ref (texture);
	cmd->color = tint;
}

void
lrg_gui_draw_list_callback (LrgGuiDrawList     *self,
                            const LrgGuiRect   *rect,
                            LrgGuiDrawCallback  callback,
                            gpointer            user_data)
{
	LrgGuiCommand *cmd;

	g_return_if_fail (LRG_IS_GUI_DRAW_LIST (self));
	g_return_if_fail (rect != NULL);
	g_return_if_fail (callback != NULL);

	cmd = push (self, LRG_GUI_CMD_CALLBACK);
	cmd->rect = *rect;
	cmd->callback = callback;
	cmd->user_data = user_data;
}

void
lrg_gui_draw_list_set_rect (LrgGuiDrawList   *self,
                            guint             index,
                            const LrgGuiRect *rect)
{
	g_return_if_fail (LRG_IS_GUI_DRAW_LIST (self));
	g_return_if_fail (rect != NULL);
	g_return_if_fail (index < self->commands->len);

	g_array_index (self->commands, LrgGuiCommand, index).rect = *rect;
}

void
lrg_gui_draw_list_append (LrgGuiDrawList *self,
                          LrgGuiDrawList *other)
{
	guint i;

	g_return_if_fail (LRG_IS_GUI_DRAW_LIST (self));
	g_return_if_fail (LRG_IS_GUI_DRAW_LIST (other));

	/* Deep copy so both lists can be cleared independently. */
	for (i = 0; i < other->commands->len; i++)
	{
		LrgGuiCommand  copy = g_array_index (other->commands, LrgGuiCommand, i);

		copy.text = g_strdup (copy.text);
		if (copy.texture != NULL)
			g_object_ref (copy.texture);
		g_array_append_val (self->commands, copy);
	}
}

const LrgGuiCommand *
lrg_gui_draw_list_find_text (LrgGuiDrawList *self,
                             const gchar    *text)
{
	guint i;

	g_return_val_if_fail (LRG_IS_GUI_DRAW_LIST (self), NULL);
	g_return_val_if_fail (text != NULL, NULL);

	for (i = 0; i < self->commands->len; i++)
	{
		const LrgGuiCommand *cmd = &g_array_index (self->commands, LrgGuiCommand, i);

		if (cmd->kind == LRG_GUI_CMD_TEXT && g_strcmp0 (cmd->text, text) == 0)
			return cmd;
	}

	return NULL;
}
