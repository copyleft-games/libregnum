/* lrg-editor-legacy-input.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgGuiInput -> LrgCanvasInput, for the deprecated lrg_editor_app_frame().
 * Plain functions: the app keeps a copy of the previous LrgGuiInput (the
 * EditorLegacyInput state below) to diff button and key states.
 */

#include "config.h"

#include "lrg-editor-app-private.h"

/* The previous frame, owned by the app (LrgEditorApp:legacy). */
struct _EditorLegacyInput
{
	LrgGuiInput previous;
	gboolean    have_previous;
};

GrlKey
lrg_editor_gui_key_to_grl (LrgGuiKey key)
{
	/* Named keys; letters, digits, F-keys and keypad digits are ranges */
	switch (key)
	{
	case LRG_GUI_KEY_TAB:           return GRL_KEY_TAB;
	case LRG_GUI_KEY_LEFT:          return GRL_KEY_LEFT;
	case LRG_GUI_KEY_RIGHT:         return GRL_KEY_RIGHT;
	case LRG_GUI_KEY_UP:            return GRL_KEY_UP;
	case LRG_GUI_KEY_DOWN:          return GRL_KEY_DOWN;
	case LRG_GUI_KEY_PAGE_UP:       return GRL_KEY_PAGE_UP;
	case LRG_GUI_KEY_PAGE_DOWN:     return GRL_KEY_PAGE_DOWN;
	case LRG_GUI_KEY_HOME:          return GRL_KEY_HOME;
	case LRG_GUI_KEY_END:           return GRL_KEY_END;
	case LRG_GUI_KEY_INSERT:        return GRL_KEY_INSERT;
	case LRG_GUI_KEY_DELETE:        return GRL_KEY_DELETE;
	case LRG_GUI_KEY_BACKSPACE:     return GRL_KEY_BACKSPACE;
	case LRG_GUI_KEY_SPACE:         return GRL_KEY_SPACE;
	case LRG_GUI_KEY_ENTER:         return GRL_KEY_ENTER;
	case LRG_GUI_KEY_ESCAPE:        return GRL_KEY_ESCAPE;
	case LRG_GUI_KEY_MINUS:         return GRL_KEY_MINUS;
	case LRG_GUI_KEY_EQUAL:         return GRL_KEY_EQUAL;
	case LRG_GUI_KEY_LEFT_BRACKET:  return GRL_KEY_LEFT_BRACKET;
	case LRG_GUI_KEY_RIGHT_BRACKET: return GRL_KEY_RIGHT_BRACKET;
	case LRG_GUI_KEY_SLASH:         return GRL_KEY_SLASH;
	case LRG_GUI_KEY_BACKSLASH:     return GRL_KEY_BACKSLASH;
	case LRG_GUI_KEY_COMMA:         return GRL_KEY_COMMA;
	case LRG_GUI_KEY_PERIOD:        return GRL_KEY_PERIOD;
	case LRG_GUI_KEY_SEMICOLON:     return GRL_KEY_SEMICOLON;
	case LRG_GUI_KEY_APOSTROPHE:    return GRL_KEY_APOSTROPHE;
	case LRG_GUI_KEY_GRAVE:         return GRL_KEY_GRAVE;
	default:
		break;
	}
	if (key >= LRG_GUI_KEY_A && key <= LRG_GUI_KEY_Z)
		return (GrlKey) (GRL_KEY_A + (key - LRG_GUI_KEY_A));
	if (key >= LRG_GUI_KEY_0 && key <= LRG_GUI_KEY_9)
		return (GrlKey) (GRL_KEY_ZERO + (key - LRG_GUI_KEY_0));
	if (key >= LRG_GUI_KEY_F1 && key <= LRG_GUI_KEY_F12)
		return (GrlKey) (GRL_KEY_F1 + (key - LRG_GUI_KEY_F1));
	if (key >= LRG_GUI_KEY_KP_0 && key <= LRG_GUI_KEY_KP_9)
		return (GrlKey) (GRL_KEY_KP_0 + (key - LRG_GUI_KEY_KP_0));
	return GRL_KEY_NULL;
}

static LrgInputModifiers
mods_to_canvas (LrgGuiModifiers mods)
{
	guint out = LRG_INPUT_MODIFIER_NONE;

	if (mods & LRG_GUI_MOD_CTRL)
		out |= LRG_INPUT_MODIFIER_CTRL;
	if (mods & LRG_GUI_MOD_SHIFT)
		out |= LRG_INPUT_MODIFIER_SHIFT;
	if (mods & LRG_GUI_MOD_ALT)
		out |= LRG_INPUT_MODIFIER_ALT;
	if (mods & LRG_GUI_MOD_SUPER)
		out |= LRG_INPUT_MODIFIER_SUPER;
	return (LrgInputModifiers) out;
}

void
lrg_editor_gui_input_to_canvas (const LrgGuiInput *input,
                                const LrgGuiInput *previous,
                                LrgCanvasInput    *out)
{
	static const GrlMouseButton buttons[LRG_GUI_MOUSE_BUTTON_COUNT] = {
		GRL_MOUSE_BUTTON_LEFT, GRL_MOUSE_BUTTON_RIGHT, GRL_MOUSE_BUTTON_MIDDLE
	};
	guint i;

	g_return_if_fail (input != NULL);
	g_return_if_fail (out != NULL);

	lrg_canvas_input_init (out);

	/* Pointer, wheel, modifiers, clock */
	out->pointer_x = input->mouse_x;
	out->pointer_y = input->mouse_y;
	out->wheel_x = input->wheel_x;
	out->wheel_y = input->wheel_y;
	out->modifiers = mods_to_canvas (input->mods);
	out->delta_time = input->delta;

	/* Buttons: held now; pressed / released by diffing the last frame */
	for (i = 0; i < LRG_GUI_MOUSE_BUTTON_COUNT; i++)
	{
		guint    bit = LRG_CANVAS_BUTTON_MASK (buttons[i]);
		gboolean was = previous != NULL && previous->mouse_down[i];

		if (input->mouse_down[i])
			out->buttons_down |= bit;
		if (input->mouse_down[i] && !was)
			out->buttons_pressed |= bit;
		if (!input->mouse_down[i] && was)
			out->buttons_released |= bit;
	}

	/* Key presses in order, with their repeat flag */
	for (i = 0; i < input->n_keys && out->n_keys < LRG_CANVAS_INPUT_MAX_KEYS; i++)
	{
		GrlKey key = lrg_editor_gui_key_to_grl (input->keys[i].key);

		if (key == GRL_KEY_NULL)
			continue;
		if (input->keys[i].repeat)
			out->keys_repeat |= 1u << out->n_keys;
		out->keys[out->n_keys++] = key;
	}

	/* Held keys that are no longer held were released */
	for (i = 0; previous != NULL && i < LRG_GUI_KEY_COUNT; i++)
	{
		GrlKey key;

		if (!previous->key_down[i] || input->key_down[i] || out->n_keys_released >= LRG_CANVAS_INPUT_MAX_KEYS)
			continue;
		key = lrg_editor_gui_key_to_grl ((LrgGuiKey) i);
		if (key != GRL_KEY_NULL)
			out->keys_released[out->n_keys_released++] = key;
	}

	/* Typed text */
	for (i = 0; i < input->n_chars && out->n_chars < LRG_CANVAS_INPUT_MAX_KEYS; i++)
		out->chars[out->n_chars++] = input->chars[i];
}

EditorLegacyInput *
editor_legacy_input_new (void)
{
	return g_new0 (EditorLegacyInput, 1);
}

void
editor_legacy_input_free (gpointer data)
{
	g_free (data);
}

/* Translates @input against the remembered previous frame, then
 * remembers it. */
void
editor_legacy_input_translate (EditorLegacyInput *self,
                               const LrgGuiInput *input,
                               LrgCanvasInput    *out)
{
	lrg_editor_gui_input_to_canvas (input, self->have_previous ? &self->previous : NULL, out);
	self->previous = *input;
	self->have_previous = TRUE;
}
