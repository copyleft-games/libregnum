/* lrg-gui-raylib.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * raylib backend for the immediate-mode GUI. See lrg-gui-raylib.h.
 *
 * This file talks to raylib directly (not through graylib's boxed
 * wrappers) because a UI frame replays thousands of commands and the
 * wrappers allocate per call.
 */

#include "lrg-gui-raylib.h"
#include <graylib.h>
#include "lrg-gui-raylib-private.h"
#include <math.h>
#include <string.h>

struct _LrgGuiRaylib
{
	GObject   parent_instance;

	Font      ui_font;
	Font      mono_font;
	gboolean  ui_loaded;
	gboolean  mono_loaded;
	gchar    *ui_path;
	gfloat    loaded_size;
	gint      cursor;        /* raylib cursor last applied */

	GArray   *clip_stack;    /* Rectangle */

	/* The scissor state last set during lrg_gui_raylib_render(), so a
	 * clip change to the same rectangle (common: a region pops back to
	 * its parent's clip) does not end and restart scissor mode, each of
	 * which flushes raylib's batch. scissor_known is FALSE when the GL
	 * state is not ours to assume (frame start, after a callback). */
	gboolean  scissor_known;
	gboolean  scissor_on;
	gint      scissor[4];    /* x, y, w, h as given to BeginScissorMode */
};

G_DEFINE_FINAL_TYPE (LrgGuiRaylib, lrg_gui_raylib, G_TYPE_OBJECT)

/* ==========================================================================
 * Click latching
 *
 * raylib samples button state once per frame, so a press and release that
 * both land between two frames (a touchpad tap, a fast click on a busy
 * frame) is never seen. A GLFW button callback chained in front of
 * raylib's counts presses; poll_input reports a latched press as "down"
 * for one frame, which gives the GUI its click edge. GLFW is linked into
 * libregnum through raylib's desktop platform; only the two entry points
 * used here are declared.
 * ========================================================================== */

typedef struct GLFWwindow GLFWwindow;
typedef void (*GLFWmousebuttonfun) (GLFWwindow *window, int button, int action, int mods);
typedef void (*GLFWkeyfun) (GLFWwindow *window, int key, int scancode, int action, int mods);

GLFWwindow *       glfwGetCurrentContext      (void);
GLFWmousebuttonfun glfwSetMouseButtonCallback (GLFWwindow *window, GLFWmousebuttonfun callback);
GLFWkeyfun         glfwSetKeyCallback         (GLFWwindow *window, GLFWkeyfun callback);

#define LATCH_GLFW_PRESS     (1)
#define LATCH_GLFW_REPEAT    (2)
#define LATCH_GLFW_MOD_SHIFT (0x0001)
#define LATCH_GLFW_MOD_CTRL  (0x0002)
#define LATCH_GLFW_MOD_ALT   (0x0004)
#define LATCH_GLFW_MOD_SUPER (0x0008)
#define LATCH_MAX_KEYS       (64)

/* A key press with the modifiers GLFW reported for it. A chord typed
 * faster than a frame (Ctrl down, A, Ctrl up) keeps its Ctrl this way;
 * sampling IsKeyDown(CTRL) at poll time would have lost it. */
typedef struct
{
	gint            key;     /* raylib key code (= GLFW key code) */
	LrgGuiModifiers mods;
	gboolean        repeat;
} LatchedKey;

static GLFWwindow         *latch_window = NULL;
static GLFWmousebuttonfun  latch_chain = NULL;
static GLFWkeyfun          latch_key_chain = NULL;
static guint               latch_presses[LRG_GUI_MOUSE_BUTTON_COUNT];
static LatchedKey          latch_keys[LATCH_MAX_KEYS];
static guint               latch_n_keys = 0;

static void
latch_mouse_button (GLFWwindow *window,
                    int         button,
                    int         action,
                    int         mods)
{
	/* GLFW numbers left/right/middle 0/1/2, like LrgGuiMouseButton. */
	if (action == LATCH_GLFW_PRESS && button >= 0 && button < LRG_GUI_MOUSE_BUTTON_COUNT)
		latch_presses[button]++;
	if (latch_chain != NULL)
		latch_chain (window, button, action, mods);
}

static void
latch_key (GLFWwindow *window,
           int         key,
           int         scancode,
           int         action,
           int         mods)
{
	if ((action == LATCH_GLFW_PRESS || action == LATCH_GLFW_REPEAT) && latch_n_keys < LATCH_MAX_KEYS)
	{
		LatchedKey *k = &latch_keys[latch_n_keys++];

		k->key = key;
		k->repeat = (action == LATCH_GLFW_REPEAT);
		k->mods = LRG_GUI_MOD_NONE;
		if (mods & LATCH_GLFW_MOD_SHIFT)
			k->mods |= LRG_GUI_MOD_SHIFT;
		if (mods & LATCH_GLFW_MOD_CTRL)
			k->mods |= LRG_GUI_MOD_CTRL;
		if (mods & LATCH_GLFW_MOD_ALT)
			k->mods |= LRG_GUI_MOD_ALT;
		if (mods & LATCH_GLFW_MOD_SUPER)
			k->mods |= LRG_GUI_MOD_SUPER;
	}
	if (latch_key_chain != NULL)
		latch_key_chain (window, key, scancode, action, mods);
}

static void
latch_install (void)
{
	GLFWwindow *window;

	if (latch_window != NULL || !IsWindowReady ())
		return;
	window = glfwGetCurrentContext ();
	if (window == NULL)
		return;

	latch_window = window;
	latch_chain = glfwSetMouseButtonCallback (window, latch_mouse_button);
	latch_key_chain = glfwSetKeyCallback (window, latch_key);
	memset (latch_presses, 0, sizeof latch_presses);
	latch_n_keys = 0;
}

static void
latch_uninstall (void)
{
	if (latch_window == NULL)
		return;
	if (IsWindowReady () && glfwGetCurrentContext () == latch_window)
	{
		glfwSetMouseButtonCallback (latch_window, latch_chain);
		glfwSetKeyCallback (latch_window, latch_key_chain);
	}
	latch_window = NULL;
	latch_chain = NULL;
	latch_key_chain = NULL;
	latch_n_keys = 0;
}

/* ==========================================================================
 * Fonts
 * ========================================================================== */

static const gchar *ui_candidates[] = {
	"/usr/share/fonts/dejavu-sans-fonts/DejaVuSans.ttf",
	"/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
	"/usr/share/fonts/TTF/DejaVuSans.ttf",
	"/usr/share/fonts/dejavu/DejaVuSans.ttf",
	"/usr/share/fonts/google-noto/NotoSans-Regular.ttf",
	"/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf",
	"/usr/share/fonts/noto/NotoSans-Regular.ttf",
	"/usr/share/fonts/liberation-sans-fonts/LiberationSans-Regular.ttf",
	"/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
	"/usr/share/fonts/adwaita-sans-fonts/AdwaitaSans-Regular.ttf",
	"/usr/share/fonts/open-sans/OpenSans-Regular.ttf",
	NULL
};

static const gchar *mono_candidates[] = {
	"/usr/share/fonts/dejavu-sans-mono-fonts/DejaVuSansMono.ttf",
	"/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
	"/usr/share/fonts/TTF/DejaVuSansMono.ttf",
	"/usr/share/fonts/dejavu/DejaVuSansMono.ttf",
	"/usr/share/fonts/google-noto/NotoSansMono-Regular.ttf",
	"/usr/share/fonts/truetype/noto/NotoSansMono-Regular.ttf",
	"/usr/share/fonts/liberation-mono-fonts/LiberationMono-Regular.ttf",
	"/usr/share/fonts/truetype/liberation/LiberationMono-Regular.ttf",
	"/usr/share/fonts/adobe-source-code-pro-fonts/SourceCodePro-Regular.otf",
	NULL
};

gchar *
lrg_gui_raylib_find_system_font (gboolean monospace)
{
	const gchar **c = monospace ? mono_candidates : ui_candidates;
	guint         i;

	for (i = 0; c[i] != NULL; i++)
		if (g_file_test (c[i], G_FILE_TEST_IS_REGULAR))
			return g_strdup (c[i]);

	return NULL;
}

/* Latin, Latin-1, Latin Extended-A and the symbols the editor draws. */
static gint *
build_codepoints (gint *count)
{
	static const gint extras[] = {
		0x2013, 0x2014, 0x2018, 0x2019, 0x201c, 0x201d, 0x2022, 0x2026,
		0x2190, 0x2191, 0x2192, 0x2193, 0x2195, 0x21bb, 0x2302, 0x2318,
		0x25a0, 0x25a1, 0x25b2, 0x25b6, 0x25bc, 0x25c0, 0x25cf, 0x25cb,
		0x2605, 0x2606, 0x2699, 0x26a0, 0x2713, 0x2715, 0x2716, 0x271a,
		0x00d7, 0x00b0
	};
	GArray *cps = g_array_new (FALSE, FALSE, sizeof (gint));
	gint    c;
	gsize   i;

	for (c = 32; c < 127; c++)
		g_array_append_val (cps, c);
	for (c = 160; c < 384; c++)
		g_array_append_val (cps, c);
	for (i = 0; i < G_N_ELEMENTS (extras); i++)
		g_array_append_val (cps, extras[i]);

	*count = (gint) cps->len;
	return (gint *) g_array_free (cps, FALSE);
}

static void
unload_fonts (LrgGuiRaylib *self)
{
	if (self->ui_loaded)
		UnloadFont (self->ui_font);
	if (self->mono_loaded)
		UnloadFont (self->mono_font);
	self->ui_loaded = FALSE;
	self->mono_loaded = FALSE;
}

static gboolean
load_one (const gchar *path,
          gfloat       size,
          Font        *out)
{
	gint  count;
	gint *cps;

	if (path == NULL || !g_file_test (path, G_FILE_TEST_IS_REGULAR))
		return FALSE;

	cps = build_codepoints (&count);
	*out = LoadFontEx (path, (gint) roundf (size), cps, count);
	g_free (cps);

	if (out->texture.id == 0 || out->glyphCount == 0)
		return FALSE;

	SetTextureFilter (out->texture, TEXTURE_FILTER_BILINEAR);
	return TRUE;
}

void
lrg_gui_raylib_load_fonts (LrgGuiRaylib *self,
                           const gchar  *ui_path,
                           const gchar  *mono_path,
                           gfloat        size)
{
	g_autofree gchar *ui = NULL;
	g_autofree gchar *mono = NULL;

	g_return_if_fail (LRG_IS_GUI_RAYLIB (self));

	unload_fonts (self);
	ui = (ui_path != NULL && *ui_path != '\0') ? g_strdup (ui_path) : lrg_gui_raylib_find_system_font (FALSE);
	mono = (mono_path != NULL && *mono_path != '\0') ? g_strdup (mono_path)
	                                                 : lrg_gui_raylib_find_system_font (TRUE);

	self->ui_loaded = load_one (ui, size, &self->ui_font);
	self->mono_loaded = load_one (mono, size, &self->mono_font);
	self->loaded_size = size;

	g_free (self->ui_path);
	self->ui_path = self->ui_loaded ? g_steal_pointer (&ui) : NULL;
}

const gchar *
lrg_gui_raylib_get_font_path (LrgGuiRaylib *self)
{
	g_return_val_if_fail (LRG_IS_GUI_RAYLIB (self), NULL);

	return self->ui_path;
}

static Font
font_for (LrgGuiRaylib *self,
          gboolean      mono)
{
	if (mono && self->mono_loaded)
		return self->mono_font;
	if (self->ui_loaded)
		return self->ui_font;
	return GetFontDefault ();
}

/* ==========================================================================
 * GObject
 * ========================================================================== */

static void
lrg_gui_raylib_finalize (GObject *object)
{
	LrgGuiRaylib *self = LRG_GUI_RAYLIB (object);

	/* Fonts live on the GL context; only unload while it exists. */
	if (IsWindowReady ())
		unload_fonts (self);
	latch_uninstall ();
	g_free (self->ui_path);
	g_array_unref (self->clip_stack);

	G_OBJECT_CLASS (lrg_gui_raylib_parent_class)->finalize (object);
}

static void
lrg_gui_raylib_class_init (LrgGuiRaylibClass *klass)
{
	G_OBJECT_CLASS (klass)->finalize = lrg_gui_raylib_finalize;
}

static void
lrg_gui_raylib_init (LrgGuiRaylib *self)
{
	self->clip_stack = g_array_new (FALSE, FALSE, sizeof (Rectangle));
	self->cursor = -1;
}

LrgGuiRaylib *
lrg_gui_raylib_new (void)
{
	return g_object_new (LRG_TYPE_GUI_RAYLIB, NULL);
}

/* ==========================================================================
 * Measurement and clipboard
 * ========================================================================== */

static gfloat
measure (const gchar *text,
         gssize       length,
         gfloat       font_size,
         gboolean     monospace,
         gpointer     user_data)
{
	LrgGuiRaylib *self = user_data;
	Font          font = font_for (self, monospace);
	gchar         stack[512];
	gchar        *heap = NULL;
	const gchar  *s = text;
	Vector2       v;

	/* MeasureTextEx wants a NUL-terminated string. */
	if (length >= 0)
	{
		if ((gsize) length < sizeof stack)
		{
			memcpy (stack, text, length);
			stack[length] = '\0';
			s = stack;
		}
		else
		{
			heap = g_strndup (text, length);
			s = heap;
		}
	}

	v = MeasureTextEx (font, s, font_size, (font.texture.id == GetFontDefault ().texture.id)
	                                       ? font_size / 10.0f : 0.0f);
	g_free (heap);
	return v.x;
}

static gchar *
clip_get (gpointer user_data)
{
	const gchar *t = GetClipboardText ();

	return (t != NULL) ? g_strdup (t) : NULL;
}

static void
clip_set (const gchar *text,
          gpointer     user_data)
{
	SetClipboardText (text);
}

void
lrg_gui_raylib_attach (LrgGuiRaylib  *self,
                       LrgGuiContext *ctx)
{
	g_return_if_fail (LRG_IS_GUI_RAYLIB (self));
	g_return_if_fail (LRG_IS_GUI_CONTEXT (ctx));

	lrg_gui_context_set_measure_func (ctx, measure, g_object_ref (self), g_object_unref);
	lrg_gui_context_set_clipboard_funcs (ctx, clip_get, clip_set, NULL, NULL);
	latch_install ();
}

/* ==========================================================================
 * Input
 * ========================================================================== */

static const struct
{
	gint      ray;
	LrgGuiKey key;
} key_map[] = {
	{ KEY_TAB, LRG_GUI_KEY_TAB }, { KEY_LEFT, LRG_GUI_KEY_LEFT }, { KEY_RIGHT, LRG_GUI_KEY_RIGHT },
	{ KEY_UP, LRG_GUI_KEY_UP }, { KEY_DOWN, LRG_GUI_KEY_DOWN }, { KEY_PAGE_UP, LRG_GUI_KEY_PAGE_UP },
	{ KEY_PAGE_DOWN, LRG_GUI_KEY_PAGE_DOWN }, { KEY_HOME, LRG_GUI_KEY_HOME }, { KEY_END, LRG_GUI_KEY_END },
	{ KEY_INSERT, LRG_GUI_KEY_INSERT }, { KEY_DELETE, LRG_GUI_KEY_DELETE },
	{ KEY_BACKSPACE, LRG_GUI_KEY_BACKSPACE }, { KEY_SPACE, LRG_GUI_KEY_SPACE },
	{ KEY_ENTER, LRG_GUI_KEY_ENTER }, { KEY_KP_ENTER, LRG_GUI_KEY_ENTER },
	{ KEY_ESCAPE, LRG_GUI_KEY_ESCAPE },
	{ KEY_MINUS, LRG_GUI_KEY_MINUS }, { KEY_EQUAL, LRG_GUI_KEY_EQUAL },
	{ KEY_KP_SUBTRACT, LRG_GUI_KEY_MINUS }, { KEY_KP_ADD, LRG_GUI_KEY_EQUAL },
	{ KEY_LEFT_BRACKET, LRG_GUI_KEY_LEFT_BRACKET }, { KEY_RIGHT_BRACKET, LRG_GUI_KEY_RIGHT_BRACKET },
	{ KEY_SLASH, LRG_GUI_KEY_SLASH }, { KEY_BACKSLASH, LRG_GUI_KEY_BACKSLASH },
	{ KEY_COMMA, LRG_GUI_KEY_COMMA }, { KEY_PERIOD, LRG_GUI_KEY_PERIOD },
	{ KEY_SEMICOLON, LRG_GUI_KEY_SEMICOLON }, { KEY_APOSTROPHE, LRG_GUI_KEY_APOSTROPHE },
	{ KEY_GRAVE, LRG_GUI_KEY_GRAVE },
	{ KEY_F1, LRG_GUI_KEY_F1 }, { KEY_F2, LRG_GUI_KEY_F2 }, { KEY_F3, LRG_GUI_KEY_F3 },
	{ KEY_F4, LRG_GUI_KEY_F4 }, { KEY_F5, LRG_GUI_KEY_F5 }, { KEY_F6, LRG_GUI_KEY_F6 },
	{ KEY_F7, LRG_GUI_KEY_F7 }, { KEY_F8, LRG_GUI_KEY_F8 }, { KEY_F9, LRG_GUI_KEY_F9 },
	{ KEY_F10, LRG_GUI_KEY_F10 }, { KEY_F11, LRG_GUI_KEY_F11 }, { KEY_F12, LRG_GUI_KEY_F12 },
};

static LrgGuiKey
map_key (gint ray)
{
	gsize i;

	if (ray >= KEY_A && ray <= KEY_Z)
		return LRG_GUI_KEY_A + (ray - KEY_A);
	if (ray >= KEY_ZERO && ray <= KEY_NINE)
		return LRG_GUI_KEY_0 + (ray - KEY_ZERO);
	if (ray >= KEY_KP_0 && ray <= KEY_KP_9)
		return LRG_GUI_KEY_KP_0 + (ray - KEY_KP_0);
	for (i = 0; i < G_N_ELEMENTS (key_map); i++)
		if (key_map[i].ray == ray)
			return key_map[i].key;
	return LRG_GUI_KEY_NONE;
}

/* raylib key for a toolkit key (the first mapping wins). */
static gint
unmap_key (LrgGuiKey key)
{
	gsize i;

	if (key >= LRG_GUI_KEY_A && key <= LRG_GUI_KEY_Z)
		return KEY_A + (key - LRG_GUI_KEY_A);
	if (key >= LRG_GUI_KEY_0 && key <= LRG_GUI_KEY_9)
		return KEY_ZERO + (key - LRG_GUI_KEY_0);
	if (key >= LRG_GUI_KEY_KP_0 && key <= LRG_GUI_KEY_KP_9)
		return KEY_KP_0 + (key - LRG_GUI_KEY_KP_0);
	for (i = 0; i < G_N_ELEMENTS (key_map); i++)
		if (key_map[i].key == key)
			return key_map[i].ray;
	return 0;
}

static void
push_key (LrgGuiInput     *input,
          LrgGuiKey        key,
          LrgGuiModifiers  mods,
          gboolean         repeat)
{
	if (key == LRG_GUI_KEY_NONE || input->n_keys >= LRG_GUI_MAX_INPUT_EVENTS)
		return;
	input->keys[input->n_keys].key = key;
	input->keys[input->n_keys].mods = mods;
	input->keys[input->n_keys].repeat = repeat;
	input->n_keys++;
}

void
lrg_gui_raylib_poll_input (LrgGuiRaylib *self,
                           LrgGuiInput  *input)
{
	static const LrgGuiKey repeating[] = {
		LRG_GUI_KEY_BACKSPACE, LRG_GUI_KEY_DELETE, LRG_GUI_KEY_LEFT, LRG_GUI_KEY_RIGHT,
		LRG_GUI_KEY_UP, LRG_GUI_KEY_DOWN, LRG_GUI_KEY_PAGE_UP, LRG_GUI_KEY_PAGE_DOWN,
		LRG_GUI_KEY_ENTER, LRG_GUI_KEY_TAB
	};
	LrgGuiModifiers mods = LRG_GUI_MOD_NONE;
	Vector2         wheel;
	gint            k, c;
	gsize           i;

	g_return_if_fail (LRG_IS_GUI_RAYLIB (self));
	g_return_if_fail (input != NULL);

	lrg_gui_input_new_frame (input, GetFrameTime ());
	input->time = GetTime ();

	if (IsKeyDown (KEY_LEFT_CONTROL) || IsKeyDown (KEY_RIGHT_CONTROL))
		mods |= LRG_GUI_MOD_CTRL;
	if (IsKeyDown (KEY_LEFT_SHIFT) || IsKeyDown (KEY_RIGHT_SHIFT))
		mods |= LRG_GUI_MOD_SHIFT;
	if (IsKeyDown (KEY_LEFT_ALT) || IsKeyDown (KEY_RIGHT_ALT))
		mods |= LRG_GUI_MOD_ALT;
	if (IsKeyDown (KEY_LEFT_SUPER) || IsKeyDown (KEY_RIGHT_SUPER))
		mods |= LRG_GUI_MOD_SUPER;
	input->mods = mods;

	/* Key presses in order, each with its own modifiers (the GLFW latch),
	 * or raylib's queue and sampled modifiers when no latch is installed. */
	latch_install ();
	if (latch_window != NULL)
	{
		guint n;

		while (GetKeyPressed () != 0)
			;   /* keep raylib's queue drained */
		for (n = 0; n < latch_n_keys; n++)
		{
			LrgGuiKey key = map_key (latch_keys[n].key);

			/* Only editing keys auto-repeat. */
			if (latch_keys[n].repeat)
			{
				gboolean editing = FALSE;

				for (i = 0; i < G_N_ELEMENTS (repeating); i++)
					editing = editing || repeating[i] == key;
				if (!editing)
					continue;
			}
			push_key (input, key, latch_keys[n].mods, latch_keys[n].repeat);
		}
		latch_n_keys = 0;
	}
	else
	{
		while ((k = GetKeyPressed ()) != 0)
			push_key (input, map_key (k), mods, FALSE);
		for (i = 0; i < G_N_ELEMENTS (repeating); i++)
			if (IsKeyPressedRepeat (unmap_key (repeating[i])))
				push_key (input, repeating[i], mods, TRUE);
	}

	while ((c = GetCharPressed ()) != 0 && input->n_chars < LRG_GUI_MAX_INPUT_EVENTS)
		input->chars[input->n_chars++] = (gunichar) c;

	/* Held state for continuous controls (fly camera). */
	for (k = LRG_GUI_KEY_NONE + 1; k < LRG_GUI_KEY_COUNT; k++)
	{
		gint ray = unmap_key ((LrgGuiKey) k);

		input->key_down[k] = (ray != 0) && IsKeyDown (ray);
	}

	input->mouse_x = (gfloat) GetMouseX ();
	input->mouse_y = (gfloat) GetMouseY ();
	/* Held now, or pressed (and maybe already released) since last poll. */
	input->mouse_down[LRG_GUI_MOUSE_LEFT] = IsMouseButtonDown (MOUSE_BUTTON_LEFT) || latch_presses[0] > 0;
	input->mouse_down[LRG_GUI_MOUSE_RIGHT] = IsMouseButtonDown (MOUSE_BUTTON_RIGHT) || latch_presses[1] > 0;
	input->mouse_down[LRG_GUI_MOUSE_MIDDLE] = IsMouseButtonDown (MOUSE_BUTTON_MIDDLE) || latch_presses[2] > 0;
	memset (latch_presses, 0, sizeof latch_presses);
	wheel = GetMouseWheelMoveV ();
	input->wheel_x = wheel.x;
	input->wheel_y = wheel.y;

	/* Without focus GLFW reports no releases: a button or key held while
	 * the window lost focus (Alt+Tab mid-drag) would stay down forever. */
	if (!IsWindowFocused ())
	{
		memset (input->key_down, 0, sizeof input->key_down);
		memset (input->mouse_down, 0, sizeof input->mouse_down);
		input->mods = LRG_GUI_MOD_NONE;
	}
}

/* ==========================================================================
 * Rendering
 * ========================================================================== */

static Color
to_color (LrgGuiColor c)
{
	Color out;

	out.r = (c >> 24) & 0xff;
	out.g = (c >> 16) & 0xff;
	out.b = (c >> 8) & 0xff;
	out.a = c & 0xff;
	return out;
}

static void
apply_clip (LrgGuiRaylib *self)
{
	gint want[4] = { 0, 0, 0, 0 };
	gboolean on = self->clip_stack->len > 0;

	if (on)
	{
		Rectangle r = g_array_index (self->clip_stack, Rectangle, self->clip_stack->len - 1);

		want[0] = (gint) floorf (r.x);
		want[1] = (gint) floorf (r.y);
		want[2] = (gint) ceilf (MAX (0.0f, r.width));
		want[3] = (gint) ceilf (MAX (0.0f, r.height));
	}

	/* Already in that state: nothing to do (and no batch flush). */
	if (self->scissor_known && self->scissor_on == on &&
	    (!on || memcmp (self->scissor, want, sizeof want) == 0))
		return;

	EndScissorMode ();
	if (on)
		BeginScissorMode (want[0], want[1], want[2], want[3]);
	self->scissor_known = TRUE;
	self->scissor_on = on;
	memcpy (self->scissor, want, sizeof want);
}

static void
push_clip (LrgGuiRaylib     *self,
           const LrgGuiRect *rect)
{
	Rectangle r = { rect->x, rect->y, rect->w, rect->h };

	if (self->clip_stack->len > 0)
	{
		Rectangle top = g_array_index (self->clip_stack, Rectangle, self->clip_stack->len - 1);
		gfloat    x2 = MIN (r.x + r.width, top.x + top.width);
		gfloat    y2 = MIN (r.y + r.height, top.y + top.height);

		r.x = MAX (r.x, top.x);
		r.y = MAX (r.y, top.y);
		r.width = MAX (0.0f, x2 - r.x);
		r.height = MAX (0.0f, y2 - r.y);
	}
	g_array_append_val (self->clip_stack, r);
	apply_clip (self);
}

static void
pop_clip (LrgGuiRaylib *self)
{
	if (self->clip_stack->len > 0)
		g_array_set_size (self->clip_stack, self->clip_stack->len - 1);
	apply_clip (self);
}

static gfloat
roundness_for (const LrgGuiRect *r,
               gfloat            radius)
{
	gfloat m = MIN (r->w, r->h);

	return (m > 0.0f) ? CLAMP (radius * 2.0f / m, 0.0f, 1.0f) : 0.0f;
}

void
lrg_gui_raylib_render (LrgGuiRaylib   *self,
                       LrgGuiDrawList *list)
{
	guint i, n;

	g_return_if_fail (LRG_IS_GUI_RAYLIB (self));
	g_return_if_fail (LRG_IS_GUI_DRAW_LIST (list));

	g_array_set_size (self->clip_stack, 0);
	self->scissor_known = FALSE;
	n = lrg_gui_draw_list_get_length (list);

	for (i = 0; i < n; i++)
	{
		const LrgGuiCommand *cmd = lrg_gui_draw_list_get_command (list, i);
		Rectangle            rec = { cmd->rect.x, cmd->rect.y, cmd->rect.w, cmd->rect.h };
		Color                col = to_color (cmd->color);

		switch (cmd->kind)
		{
		case LRG_GUI_CMD_RECT_FILLED:
			if (cmd->rounding > 0.5f && rec.width > 2.0f && rec.height > 2.0f)
				DrawRectangleRounded (rec, roundness_for (&cmd->rect, cmd->rounding), 6, col);
			else
				DrawRectangleRec (rec, col);
			break;

		case LRG_GUI_CMD_RECT_OUTLINE:
			if (cmd->rounding > 0.5f && rec.width > 2.0f && rec.height > 2.0f)
				DrawRectangleRoundedLinesEx (rec, roundness_for (&cmd->rect, cmd->rounding), 6,
				                             cmd->thickness, col);
			else
				DrawRectangleLinesEx (rec, cmd->thickness, col);
			break;

		case LRG_GUI_CMD_LINE:
			{
				Vector2 a = { cmd->points[0], cmd->points[1] };
				Vector2 b = { cmd->points[2], cmd->points[3] };

				DrawLineEx (a, b, cmd->thickness, col);
			}
			break;

		case LRG_GUI_CMD_TRIANGLE:
			{
				Vector2 a = { cmd->points[0], cmd->points[1] };
				Vector2 b = { cmd->points[2], cmd->points[3] };
				Vector2 c = { cmd->points[4], cmd->points[5] };
				gfloat  cross = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);

				/* raylib wants counter-clockwise on screen. */
				if (cross > 0.0f)
					DrawTriangle (a, c, b, col);
				else
					DrawTriangle (a, b, c, col);
			}
			break;

		case LRG_GUI_CMD_CIRCLE_FILLED:
			{
				Vector2 c = { cmd->rect.x, cmd->rect.y };

				DrawCircleV (c, cmd->thickness, col);
			}
			break;

		case LRG_GUI_CMD_CIRCLE_OUTLINE:
			{
				Vector2 c = { cmd->rect.x, cmd->rect.y };

				DrawCircleLinesV (c, cmd->thickness, col);
			}
			break;

		case LRG_GUI_CMD_TEXT:
			{
				Font    font = font_for (self, cmd->monospace);
				Vector2 pos = { cmd->rect.x, cmd->rect.y };
				gfloat  spacing = (font.texture.id == GetFontDefault ().texture.id)
				                  ? cmd->font_size / 10.0f : 0.0f;

				DrawTextEx (font, cmd->text, pos, cmd->font_size, spacing, col);
			}
			break;

		case LRG_GUI_CMD_CLIP_PUSH:
			push_clip (self, &cmd->rect);
			break;

		case LRG_GUI_CMD_CLIP_POP:
			pop_clip (self);
			break;

		case LRG_GUI_CMD_IMAGE:
			if (cmd->texture != NULL)
			{
				/* graylib textures wrap a raylib Texture2D. */
				Texture2D *tex = grl_texture_get_handle (GRL_TEXTURE (cmd->texture));

				if (tex != NULL && tex->id != 0)
				{
					Rectangle src = { 0.0f, 0.0f, (gfloat) tex->width, (gfloat) tex->height };
					Vector2   origin = { 0.0f, 0.0f };

					DrawTexturePro (*tex, src, rec, origin, 0.0f, col);
				}
			}
			break;

		case LRG_GUI_CMD_CALLBACK:
			if (cmd->callback != NULL)
			{
				/* Custom renderers (3D viewports) manage their own state;
				 * restore our clip afterwards. */
				EndScissorMode ();
				cmd->callback (&cmd->rect, cmd->user_data);
				self->scissor_known = FALSE;
				apply_clip (self);
			}
			break;

		default:
			break;
		}
	}

	g_array_set_size (self->clip_stack, 0);
	EndScissorMode ();
	self->scissor_known = FALSE;
}

void
lrg_gui_raylib_apply_cursor (LrgGuiRaylib *self,
                             LrgGuiCursor  cursor)
{
	gint ray;

	g_return_if_fail (LRG_IS_GUI_RAYLIB (self));

	switch (cursor)
	{
	case LRG_GUI_CURSOR_TEXT:        ray = MOUSE_CURSOR_IBEAM; break;
	case LRG_GUI_CURSOR_HAND:        ray = MOUSE_CURSOR_POINTING_HAND; break;
	case LRG_GUI_CURSOR_RESIZE_EW:   ray = MOUSE_CURSOR_RESIZE_EW; break;
	case LRG_GUI_CURSOR_RESIZE_NS:   ray = MOUSE_CURSOR_RESIZE_NS; break;
	case LRG_GUI_CURSOR_RESIZE_ALL:  ray = MOUSE_CURSOR_RESIZE_ALL; break;
	case LRG_GUI_CURSOR_NOT_ALLOWED: ray = MOUSE_CURSOR_NOT_ALLOWED; break;
	default:                         ray = MOUSE_CURSOR_DEFAULT; break;
	}

	if (ray != self->cursor)
	{
		SetMouseCursor (ray);
		self->cursor = ray;
	}
}
