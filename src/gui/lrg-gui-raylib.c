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
#include "../paint/lrg-paint-renderer.h"
#include "../paint/lrg-paint-private.h"
#include "../paint/lrg-paint-font.h"
/* graylib's O(1)-glyph-lookup forms of DrawTextEx()/MeasureTextEx():
 * header-only, on raylib's public API, same quads and measurements.
 * raylib's own functions scan the glyph table linearly per character.
 * Glyph quads go through graylib's fast 2D vertex path. */
#include "text/grl-text-layout-private.h"
/* graylib's fast 2D vertex path: drop-in forms of DrawTexturePro(),
 * DrawRectangleRec(), DrawRectangleLinesEx(), DrawRectangleRounded() and
 * DrawCircleV() that write quads straight into rlgl's batch when they
 * continue its current draw call (same pixels; graylib is linked into
 * libregnum statically, so its internal symbols are reachable) */
#include "drawing/grl-fast2d-private.h"
#include <math.h>
#include <string.h>

struct _LrgGuiRaylib
{
	GObject   parent_instance;

	Font      ui_font;
	Font      mono_font;
	/*
	 * SDF twins of the two faces (with system fallbacks for symbols,
	 * emoji, CJK and right-to-left scripts). When set they draw and
	 * measure editor text; the bitmap fonts stay as the fallback.
	 */
	LrgPaintFont *ui_sdf;
	LrgPaintFont *mono_sdf;
	gboolean  ui_loaded;
	gboolean  mono_loaded;
	gchar    *ui_path;
	gfloat    loaded_size;
	gint      cursor;        /* raylib cursor last applied */

	/* The frame is translated into SDF paint instances and drawn by
	 * the shared paint renderer: anti-aliased rounded shapes, soft
	 * shadows, and text glyphs in the same batch (usually one draw
	 * call for the whole editor). Clips become per-instance clip
	 * rectangles, so no scissor changes or batch flushes. */
	LrgPaintList *paint;
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
	g_clear_object (&self->ui_sdf);
	g_clear_object (&self->mono_sdf);
}

/* load_sdf: an SDF font for @path with system fallbacks, or NULL */
static LrgPaintFont *
load_sdf (const gchar *path)
{
	g_autoptr(GError) error = NULL;
	LrgPaintFont     *font;

	font = lrg_paint_font_new_from_file (path, &error);
	if (font == NULL)
	{
		g_debug ("GUI: no SDF text for %s: %s", path, error->message);
		return NULL;
	}
	lrg_paint_font_add_system_fallbacks (font);
	return font;
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

	/* Crisp SDF text wherever the bitmap face loaded (needs a window) */
	if (self->ui_loaded)
		self->ui_sdf = load_sdf (ui);
	if (self->mono_loaded)
		self->mono_sdf = load_sdf (mono);
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
	g_clear_object (&self->ui_sdf);
	g_clear_object (&self->mono_sdf);
	latch_uninstall ();
	g_free (self->ui_path);
	g_clear_object (&self->paint);

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
	self->paint = lrg_paint_list_new ();
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
	LrgPaintFont *sdf = monospace ? self->mono_sdf : self->ui_sdf;
	Font          font;
	gchar         stack[512];
	gchar        *heap = NULL;
	const gchar  *s = text;
	Vector2       v;

	/* SDF faces measure exactly what they draw */
	if (sdf != NULL)
	{
		gfloat w = 0.0f;

		lrg_paint_font_measure_text (sdf, text, length, font_size, 0.0f,
		                             LRG_TEXT_DIRECTION_AUTO, &w, NULL);
		return w;
	}
	font = font_for (self, monospace);

	/* The measure wants a NUL-terminated string. */
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

	v = grl_text_measure_ex (font, s, font_size,
	                         (font.texture.id == GetFontDefault ().texture.id)
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

/* to_grl: 0xRRGGBBAA -> GrlColor */
static GrlColor
to_grl (LrgGuiColor c)
{
	GrlColor out;

	out.r = (c >> 24) & 0xff;
	out.g = (c >> 16) & 0xff;
	out.b = (c >> 8) & 0xff;
	out.a = c & 0xff;
	return out;
}

typedef struct
{
	LrgGuiDrawCallback callback;
	LrgGuiRect         rect;
	gpointer           user_data;
} CallbackData;

/* run_gui_callback: adapts a GUI draw callback to a paint callback */
static void
run_gui_callback (gpointer data)
{
	CallbackData *cb = data;

	cb->callback (&cb->rect, cb->user_data);
}

void
lrg_gui_raylib_render (LrgGuiRaylib   *self,
                       LrgGuiDrawList *list)
{
	LrgPaintList *paint;
	guint         i;
	guint         n;
	guint         depth = 0;

	g_return_if_fail (LRG_IS_GUI_RAYLIB (self));
	g_return_if_fail (LRG_IS_GUI_DRAW_LIST (list));

	paint = self->paint;
	lrg_paint_list_clear (paint);
	n = lrg_gui_draw_list_get_length (list);

	for (i = 0; i < n; i++)
	{
		const LrgGuiCommand *cmd = lrg_gui_draw_list_get_command (list, i);
		const LrgGuiRect    *r = &cmd->rect;
		GrlColor             col = to_grl (cmd->color);

		switch (cmd->kind)
		{
		case LRG_GUI_CMD_RECT_FILLED:
			lrg_paint_list_rect (paint, r->x, r->y, r->w, r->h, cmd->rounding, &col);
			break;

		case LRG_GUI_CMD_RECT_OUTLINE:
			lrg_paint_list_border (paint, r->x, r->y, r->w, r->h, cmd->rounding,
			                       MAX (cmd->thickness, 1.0f), &col);
			break;

		case LRG_GUI_CMD_SHADOW:
			lrg_paint_list_shadow (paint, r->x, r->y, r->w, r->h, cmd->rounding,
			                       cmd->thickness, 0.0f, 0.0f, cmd->points[1], &col);
			break;

		case LRG_GUI_CMD_LINE:
			lrg_paint_list_line (paint, cmd->points[0], cmd->points[1],
			                     cmd->points[2], cmd->points[3],
			                     MAX (cmd->thickness, 1.0f), &col);
			break;

		case LRG_GUI_CMD_TRIANGLE:
			lrg_paint_list_triangle (paint, cmd->points[0], cmd->points[1],
			                         cmd->points[2], cmd->points[3],
			                         cmd->points[4], cmd->points[5], &col);
			break;

		case LRG_GUI_CMD_CIRCLE_FILLED:
			lrg_paint_list_circle (paint, r->x, r->y, cmd->thickness, &col);
			break;

		case LRG_GUI_CMD_CIRCLE_OUTLINE:
			{
				/* rounding carries the stroke width (1 px below 1),
				 * centred on the requested radius */
				gfloat         w = cmd->rounding > 1.0f ? cmd->rounding : 1.0f;
				gfloat         outer = cmd->thickness + w * 0.5f;
				LrgCornerRadii radii;

				lrg_corner_radii_init_uniform (&radii, outer);
				lrg_paint_list_rect_ex (paint, r->x - outer, r->y - outer,
				                        outer * 2.0f, outer * 2.0f, &radii,
				                        NULL, NULL, w, &col);
			}
			break;

		case LRG_GUI_CMD_TEXT:
			if (cmd->text != NULL
			    && (cmd->monospace ? self->mono_sdf : self->ui_sdf) != NULL)
			{
				LrgPaintTextStyle style;

				lrg_paint_text_style_init (&style, &col);
				lrg_paint_font_draw_text (cmd->monospace ? self->mono_sdf : self->ui_sdf,
				                          paint, cmd->text, -1, r->x, r->y,
				                          cmd->font_size, &style);
			}
			else if (cmd->text != NULL)
			{
				Font   font = font_for (self, cmd->monospace);
				gfloat spacing = (font.texture.id == GetFontDefault ().texture.id)
				                 ? cmd->font_size / 10.0f : 0.0f;

				_lrg_paint_list_text_font (paint, &font, cmd->text, -1, r->x, r->y,
				                           cmd->font_size, spacing, &col);
			}
			break;

		case LRG_GUI_CMD_CLIP_PUSH:
			lrg_paint_list_push_clip (paint, r->x, r->y, r->w, r->h);
			depth++;
			break;

		case LRG_GUI_CMD_CLIP_POP:
			if (depth > 0)
			{
				lrg_paint_list_pop_clip (paint);
				depth--;
			}
			break;

		case LRG_GUI_CMD_IMAGE:
			if (cmd->texture != NULL && GRL_IS_TEXTURE (cmd->texture))
				lrg_paint_list_image (paint, GRL_TEXTURE (cmd->texture), NULL,
				                      r->x, r->y, r->w, r->h, 0.0f, &col);
			break;

		case LRG_GUI_CMD_CALLBACK:
			if (cmd->callback != NULL)
			{
				CallbackData *cb = g_new (CallbackData, 1);

				cb->callback = cmd->callback;
				cb->rect = *r;
				cb->user_data = cmd->user_data;
				lrg_paint_list_callback (paint, run_gui_callback, cb, g_free);
			}
			break;

		default:
			break;
		}
	}

	while (depth-- > 0)
		lrg_paint_list_pop_clip (paint);

	lrg_paint_renderer_render (lrg_paint_renderer_get_default (), paint);
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
