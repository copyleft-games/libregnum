/* lrg-gui-context.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Immediate-mode GUI core: frame lifecycle, identity, regions and layout,
 * text, buttons, item queries, popups and menus, tabs, tooltips, drag and
 * drop, raw input. Value editors and trees live in lrg-gui-widgets.c.
 *
 * Interaction model
 * -----------------
 * A widget is "hovered" when the pointer is inside its rectangle, inside
 * the current clip, and its region is on the top-most popup layer under
 * the pointer (computed at frame start from last frame's popup
 * rectangles; a modal blocks every layer below it). Pressing a mouse
 * button on a hovered widget makes it the "active" widget until release;
 * buttons report a click on release over themselves, rows on press. An
 * active widget that is not submitted during a frame loses activity at
 * frame end, so closing a panel mid-drag never leaves the UI stuck.
 */

#include "lrg-gui-private.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#define DOUBLE_CLICK_TIME     (0.30)
#define DOUBLE_CLICK_DIST     (6.0f)
#define TOOLTIP_DELAY         (0.45)
#define DRAG_THRESHOLD        (4.0f)
#define DEFAULT_POPUP_WIDTH   (180.0f)

G_DEFINE_FINAL_TYPE (LrgGuiContext, lrg_gui_context, G_TYPE_OBJECT)

/* ==========================================================================
 * Input snapshot
 * ========================================================================== */

void
lrg_gui_input_init (LrgGuiInput *input)
{
	g_return_if_fail (input != NULL);

	memset (input, 0, sizeof *input);
	input->mouse_x = -100000.0f;
	input->mouse_y = -100000.0f;
}

void
lrg_gui_input_new_frame (LrgGuiInput *input,
                         gfloat       delta)
{
	g_return_if_fail (input != NULL);

	input->n_keys = 0;
	input->n_chars = 0;
	input->wheel_x = 0.0f;
	input->wheel_y = 0.0f;
	input->delta = delta;
	input->time += delta;
}

void
lrg_gui_input_add_key (LrgGuiInput     *input,
                       LrgGuiKey        key,
                       LrgGuiModifiers  mods)
{
	g_return_if_fail (input != NULL);

	if (key <= LRG_GUI_KEY_NONE || key >= LRG_GUI_KEY_COUNT)
		return;
	if (input->n_keys < LRG_GUI_MAX_INPUT_EVENTS)
	{
		input->keys[input->n_keys].key = key;
		input->keys[input->n_keys].mods = mods;
		input->keys[input->n_keys].repeat = input->key_down[key];
		input->n_keys++;
	}
	input->key_down[key] = TRUE;
}

void
lrg_gui_input_add_text (LrgGuiInput *input,
                        const gchar *text)
{
	const gchar *p;

	g_return_if_fail (input != NULL);
	g_return_if_fail (text != NULL);

	for (p = text; *p != '\0' && input->n_chars < LRG_GUI_MAX_INPUT_EVENTS; p = g_utf8_next_char (p))
		input->chars[input->n_chars++] = g_utf8_get_char (p);
}

/* ==========================================================================
 * Style
 * ========================================================================== */

typedef struct
{
	const gchar *name;
	guint32      text, subtext0, overlay0, surface2, surface1, surface0;
	guint32      base, mantle, crust;
	guint32      blue, lavender, red, yellow, green, peach, teal;
	gboolean     light;
} GuiPalette;

static const GuiPalette palettes[] = {
	{ "mocha", 0xcdd6f4, 0xa6adc8, 0x6c7086, 0x585b70, 0x45475a, 0x313244,
	  0x1e1e2e, 0x181825, 0x11111b,
	  0x89b4fa, 0xb4befe, 0xf38ba8, 0xf9e2af, 0xa6e3a1, 0xfab387, 0x94e2d5, FALSE },
	{ "macchiato", 0xcad3f5, 0xa5adcb, 0x6e738d, 0x5b6078, 0x494d64, 0x363a4f,
	  0x24273a, 0x1e2030, 0x181926,
	  0x8aadf4, 0xb7bdf8, 0xed8796, 0xeed49f, 0xa6da95, 0xf5a97f, 0x8bd5ca, FALSE },
	{ "frappe", 0xc6d0f5, 0xa5adce, 0x737994, 0x626880, 0x51576d, 0x414559,
	  0x303446, 0x292c3c, 0x232634,
	  0x8caaee, 0xbabbf1, 0xe78284, 0xe5c890, 0xa6d189, 0xef9f76, 0x81c8be, FALSE },
	{ "latte", 0x4c4f69, 0x6c6f85, 0x9ca0b0, 0xacb0be, 0xbcc0cc, 0xccd0da,
	  0xeff1f5, 0xe6e9ef, 0xdce0e8,
	  0x1e66f5, 0x7287fd, 0xd20f39, 0xdf8e1d, 0x40a02b, 0xfe640b, 0x179299, TRUE },
};

static LrgGuiColor
with_alpha (guint32 rgb,
            guint8  alpha)
{
	return (rgb << 8) | alpha;
}

void
lrg_gui_style_init (LrgGuiStyle *style,
                    const gchar *theme,
                    gfloat       scale)
{
	const GuiPalette *p = &palettes[0];
	LrgGuiColor      *c;
	gsize             i;

	g_return_if_fail (style != NULL);

	for (i = 0; theme != NULL && i < G_N_ELEMENTS (palettes); i++)
		if (g_ascii_strcasecmp (theme, palettes[i].name) == 0)
			p = &palettes[i];

	if (scale <= 0.0f)
		scale = 1.0f;

	memset (style, 0, sizeof *style);
	c = style->colors;
	c[LRG_GUI_COL_TEXT]             = LRG_GUI_HEX (p->text);
	c[LRG_GUI_COL_TEXT_DISABLED]    = LRG_GUI_HEX (p->overlay0);
	c[LRG_GUI_COL_WINDOW_BG]        = LRG_GUI_HEX (p->crust);
	c[LRG_GUI_COL_PANEL_BG]         = LRG_GUI_HEX (p->base);
	c[LRG_GUI_COL_POPUP_BG]         = LRG_GUI_HEX (p->mantle);
	c[LRG_GUI_COL_BORDER]           = LRG_GUI_HEX (p->surface0);
	c[LRG_GUI_COL_FRAME_BG]         = LRG_GUI_HEX (p->surface0);
	c[LRG_GUI_COL_FRAME_BG_HOVERED] = LRG_GUI_HEX (p->surface1);
	c[LRG_GUI_COL_FRAME_BG_ACTIVE]  = LRG_GUI_HEX (p->surface2);
	c[LRG_GUI_COL_BUTTON]           = LRG_GUI_HEX (p->surface0);
	c[LRG_GUI_COL_BUTTON_HOVERED]   = LRG_GUI_HEX (p->surface1);
	c[LRG_GUI_COL_BUTTON_ACTIVE]    = LRG_GUI_HEX (p->surface2);
	c[LRG_GUI_COL_HEADER]           = with_alpha (p->blue, 0x50);
	c[LRG_GUI_COL_HEADER_HOVERED]   = with_alpha (p->surface1, 0xc0);
	c[LRG_GUI_COL_HEADER_ACTIVE]    = with_alpha (p->blue, 0x80);
	c[LRG_GUI_COL_CHECK_MARK]       = LRG_GUI_HEX (p->blue);
	c[LRG_GUI_COL_SLIDER_GRAB]      = LRG_GUI_HEX (p->blue);
	c[LRG_GUI_COL_SEPARATOR]        = LRG_GUI_HEX (p->surface1);
	c[LRG_GUI_COL_SCROLLBAR_BG]     = LRG_GUI_HEX (p->mantle);
	c[LRG_GUI_COL_SCROLLBAR_GRAB]   = LRG_GUI_HEX (p->surface1);
	c[LRG_GUI_COL_TAB]              = LRG_GUI_HEX (p->mantle);
	c[LRG_GUI_COL_TAB_HOVERED]      = LRG_GUI_HEX (p->surface0);
	c[LRG_GUI_COL_TAB_ACTIVE]       = LRG_GUI_HEX (p->base);
	c[LRG_GUI_COL_MENUBAR_BG]       = LRG_GUI_HEX (p->mantle);
	c[LRG_GUI_COL_TEXT_SELECTION]   = with_alpha (p->blue, 0x60);
	c[LRG_GUI_COL_DROP_TARGET]      = LRG_GUI_HEX (p->peach);
	c[LRG_GUI_COL_MODAL_DIM]        = with_alpha (p->crust, p->light ? 0x80 : 0xa8);
	c[LRG_GUI_COL_ACCENT]           = LRG_GUI_HEX (p->lavender);
	c[LRG_GUI_COL_ERROR]            = LRG_GUI_HEX (p->red);
	c[LRG_GUI_COL_WARNING]          = LRG_GUI_HEX (p->yellow);
	c[LRG_GUI_COL_SUCCESS]          = LRG_GUI_HEX (p->green);
	c[LRG_GUI_COL_AXIS_X]           = LRG_GUI_HEX (p->red);
	c[LRG_GUI_COL_AXIS_Y]           = LRG_GUI_HEX (p->green);
	c[LRG_GUI_COL_AXIS_Z]           = LRG_GUI_HEX (p->blue);

	style->font_size       = roundf (15.0f * scale);
	style->frame_padding_x = roundf (6.0f * scale);
	style->frame_padding_y = roundf (3.0f * scale);
	style->item_spacing_x  = roundf (6.0f * scale);
	style->item_spacing_y  = roundf (4.0f * scale);
	style->region_padding  = roundf (6.0f * scale);
	style->indent          = roundf (16.0f * scale);
	style->rounding        = roundf (3.0f * scale);
	style->scrollbar_size  = roundf (10.0f * scale);
	style->label_ratio     = 0.38f;
}

/* ==========================================================================
 * GObject
 * ========================================================================== */

static gfloat
default_measure (const gchar *text,
                 gssize       length,
                 gfloat       font_size,
                 gboolean     monospace,
                 gpointer     user_data)
{
	glong n = (length < 0) ? g_utf8_strlen (text, -1) : g_utf8_strlen (text, length);

	return (gfloat) n * font_size * (monospace ? 0.6f : 0.55f);
}

static void
lrg_gui_context_finalize (GObject *object)
{
	LrgGuiContext *self = LRG_GUI_CONTEXT (object);
	gint           i;

	if (self->measure_destroy != NULL)
		self->measure_destroy (self->measure_data);
	if (self->clip_destroy != NULL)
		self->clip_destroy (self->clip_data);
	g_free (self->clipboard);

	for (i = 0; i < GUI_N_LAYERS; i++)
		g_object_unref (self->layers[i]);
	g_object_unref (self->output);

	g_array_unref (self->id_stack);
	g_array_unref (self->regions);
	g_array_unref (self->tree_indents);
	g_array_unref (self->popups);
	g_array_unref (self->tab_bars);
	g_hash_table_unref (self->popups_at_start);
	g_string_free (self->edit_buf, TRUE);
	g_string_free (self->edit_initial, TRUE);
	g_free (self->tooltip);
	g_free (self->dnd_type);
	g_free (self->dnd_data);
	g_free (self->dnd_preview);
	g_hash_table_unref (self->state);

	G_OBJECT_CLASS (lrg_gui_context_parent_class)->finalize (object);
}

static void
lrg_gui_context_class_init (LrgGuiContextClass *klass)
{
	G_OBJECT_CLASS (klass)->finalize = lrg_gui_context_finalize;
}

static void
lrg_gui_context_init (LrgGuiContext *self)
{
	gint i;

	lrg_gui_style_init (&self->style, NULL, 1.0f);
	self->measure = default_measure;
	lrg_gui_input_init (&self->input);

	for (i = 0; i < GUI_N_LAYERS; i++)
		self->layers[i] = lrg_gui_draw_list_new ();
	self->output = lrg_gui_draw_list_new ();

	self->id_stack = g_array_new (FALSE, FALSE, sizeof (LrgGuiId));
	self->regions = g_array_new (FALSE, TRUE, sizeof (GuiRegion));
	self->tree_indents = g_array_new (FALSE, FALSE, sizeof (gfloat));
	self->popups = g_array_new (FALSE, TRUE, sizeof (GuiPopup));
	self->tab_bars = g_array_new (FALSE, TRUE, sizeof (GuiTabBar));
	self->popups_at_start = g_hash_table_new (g_direct_hash, g_direct_equal);
	self->edit_buf = g_string_new (NULL);
	self->edit_initial = g_string_new (NULL);
	self->state = g_hash_table_new_full (g_direct_hash, g_direct_equal, NULL, g_free);
	self->next_item_open = -1;
	self->fallback_region.content_w = 100.0f;
}

LrgGuiContext *
lrg_gui_context_new (void)
{
	return g_object_new (LRG_TYPE_GUI_CONTEXT, NULL);
}

LrgGuiStyle *
lrg_gui_context_get_style (LrgGuiContext *self)
{
	g_return_val_if_fail (LRG_IS_GUI_CONTEXT (self), NULL);

	return &self->style;
}

void
lrg_gui_context_set_measure_func (LrgGuiContext     *self,
                                  LrgGuiMeasureFunc  func,
                                  gpointer           user_data,
                                  GDestroyNotify     destroy)
{
	g_return_if_fail (LRG_IS_GUI_CONTEXT (self));

	if (self->measure_destroy != NULL)
		self->measure_destroy (self->measure_data);

	self->measure = (func != NULL) ? func : default_measure;
	self->measure_data = user_data;
	self->measure_destroy = destroy;
}

void
lrg_gui_context_set_clipboard_funcs (LrgGuiContext          *self,
                                     LrgGuiClipboardGetFunc  get_func,
                                     LrgGuiClipboardSetFunc  set_func,
                                     gpointer                user_data,
                                     GDestroyNotify          destroy)
{
	g_return_if_fail (LRG_IS_GUI_CONTEXT (self));

	if (self->clip_destroy != NULL)
		self->clip_destroy (self->clip_data);

	self->clip_get = get_func;
	self->clip_set = set_func;
	self->clip_data = user_data;
	self->clip_destroy = destroy;
}

void
lrg_gui_context_set_clipboard (LrgGuiContext *self,
                               const gchar   *text)
{
	g_return_if_fail (LRG_IS_GUI_CONTEXT (self));

	g_free (self->clipboard);
	self->clipboard = g_strdup (text);
	if (self->clip_set != NULL && text != NULL)
		self->clip_set (text, self->clip_data);
}

gchar *
lrg_gui_context_get_clipboard (LrgGuiContext *self)
{
	g_return_val_if_fail (LRG_IS_GUI_CONTEXT (self), NULL);

	if (self->clip_get != NULL)
	{
		gchar *text = self->clip_get (self->clip_data);

		if (text != NULL)
			return text;
	}

	return g_strdup (self->clipboard);
}

/* ==========================================================================
 * Small helpers
 * ========================================================================== */

gboolean
gui_rect_contains (const LrgGuiRect *rect,
                   gfloat            x,
                   gfloat            y)
{
	return x >= rect->x && y >= rect->y && x < rect->x + rect->w && y < rect->y + rect->h;
}

LrgGuiRect
gui_rect_intersect (const LrgGuiRect *a,
                    const LrgGuiRect *b)
{
	LrgGuiRect r;
	gfloat     x2 = MIN (a->x + a->w, b->x + b->w);
	gfloat     y2 = MIN (a->y + a->h, b->y + b->h);

	r.x = MAX (a->x, b->x);
	r.y = MAX (a->y, b->y);
	r.w = MAX (0.0f, x2 - r.x);
	r.h = MAX (0.0f, y2 - r.y);
	return r;
}

LrgGuiColor
gui_col (LrgGuiContext    *self,
         LrgGuiStyleColor  idx)
{
	return self->style.colors[idx];
}

GuiRegion *
gui_region (LrgGuiContext *self)
{
	if (self->regions->len == 0)
		return &self->fallback_region;

	return &g_array_index (self->regions, GuiRegion, self->regions->len - 1);
}

LrgGuiDrawList *
gui_dl (LrgGuiContext *self)
{
	return self->layers[gui_region (self)->layer];
}

gfloat
gui_text_width (LrgGuiContext *self,
                const gchar   *text,
                gssize         len,
                gboolean       mono)
{
	if (text == NULL || len == 0 || *text == '\0')
		return 0.0f;

	return self->measure (text, len, self->style.font_size, mono, self->measure_data);
}

gfloat
gui_frame_height (LrgGuiContext *self)
{
	return roundf (self->style.font_size + self->style.frame_padding_y * 2.0f);
}

/*
 * gui_format_value:
 *
 * Formats @value with a printf-like @format that contains one numeric
 * conversion (%f, %g, %e or %d, optionally with a precision such as %.2f)
 * plus any literal prefix/suffix ("%.1f deg"). Only literal format
 * strings reach printf, so user-supplied formats cannot misbehave.
 */
void
gui_format_value (gchar       *buf,
                  gsize        size,
                  const gchar *format,
                  gdouble      value)
{
	GString     *out = g_string_new (NULL);
	const gchar *p = format;
	gboolean     done = FALSE;

	while (*p != '\0')
	{
		if (*p == '%' && p[1] == '%')
		{
			g_string_append_c (out, '%');
			p += 2;
			continue;
		}
		if (*p == '%' && !done)
		{
			gint  precision = -1;
			gchar conv;
			gchar tmp[64];

			p++;
			/* Flags and width are accepted but not used. */
			while (*p == '-' || *p == '+' || *p == ' ' || *p == '#' || *p == '0')
				p++;
			while (g_ascii_isdigit (*p))
				p++;
			if (*p == '.')
			{
				p++;
				precision = 0;
				while (g_ascii_isdigit (*p))
					precision = precision * 10 + (*p++ - '0');
			}
			while (*p == 'l' || *p == 'h')
				p++;
			conv = *p != '\0' ? *p++ : 'f';
			precision = CLAMP (precision, -1, 12);

			switch (conv)
			{
			case 'd':
			case 'i':
			case 'u':
				g_snprintf (tmp, sizeof tmp, "%.0f", value);
				break;
			case 'g':
			case 'G':
				g_snprintf (tmp, sizeof tmp, "%.*g", precision < 0 ? 6 : precision, value);
				break;
			case 'e':
			case 'E':
				g_snprintf (tmp, sizeof tmp, "%.*e", precision < 0 ? 6 : precision, value);
				break;
			default:
				g_snprintf (tmp, sizeof tmp, "%.*f", precision < 0 ? 6 : precision, value);
				break;
			}
			g_string_append (out, tmp);
			done = TRUE;
			continue;
		}
		g_string_append_c (out, *p++);
	}

	if (!done)
		g_string_append_printf (out, "%.3f", value);

	g_strlcpy (buf, out->str, size);
	g_string_free (out, TRUE);
}

/* The visible part of a label ends at "##". */
const gchar *
gui_label_end (const gchar *label)
{
	const gchar *hash = strstr (label, "##");

	return (hash != NULL) ? hash : label + strlen (label);
}

/* FNV-1a over @data seeded with @seed. */
static LrgGuiId
hash_bytes (gconstpointer data,
            gsize         len,
            LrgGuiId      seed)
{
	const guint8 *p = data;
	guint32       h = seed ^ 2166136261u;
	gsize         i;

	for (i = 0; i < len; i++)
	{
		h ^= p[i];
		h *= 16777619u;
	}

	/* 0 means "no id". */
	return (h == 0) ? 1 : h;
}

static LrgGuiId
current_seed (LrgGuiContext *self)
{
	if (self->id_stack->len == 0)
		return 0;

	return g_array_index (self->id_stack, LrgGuiId, self->id_stack->len - 1);
}

LrgGuiId
gui_hash_label (LrgGuiContext *self,
                const gchar   *label)
{
	const gchar *triple = strstr (label, "###");

	/* "###id" keys only on what follows, so the visible text can change. */
	if (triple != NULL)
		return hash_bytes (triple + 3, strlen (triple + 3), current_seed (self));

	return hash_bytes (label, strlen (label), current_seed (self));
}

GuiState *
gui_state (LrgGuiContext *self,
           LrgGuiId       id)
{
	GuiState *st = g_hash_table_lookup (self->state, GUINT_TO_POINTER (id));

	if (st == NULL)
	{
		st = g_new0 (GuiState, 1);
		g_hash_table_insert (self->state, GUINT_TO_POINTER (id), st);
	}
	return st;
}

/* ==========================================================================
 * Identity
 * ========================================================================== */

LrgGuiId
lrg_gui_get_id (LrgGuiContext *self,
                const gchar   *str)
{
	g_return_val_if_fail (LRG_IS_GUI_CONTEXT (self), 0);
	g_return_val_if_fail (str != NULL, 0);

	return gui_hash_label (self, str);
}

void
lrg_gui_push_id (LrgGuiContext *self,
                 const gchar   *str)
{
	LrgGuiId id;

	g_return_if_fail (LRG_IS_GUI_CONTEXT (self));
	g_return_if_fail (str != NULL);

	id = hash_bytes (str, strlen (str), current_seed (self));
	g_array_append_val (self->id_stack, id);
}

void
lrg_gui_push_id_int (LrgGuiContext *self,
                     gint64         value)
{
	LrgGuiId id;

	g_return_if_fail (LRG_IS_GUI_CONTEXT (self));

	id = hash_bytes (&value, sizeof value, current_seed (self));
	g_array_append_val (self->id_stack, id);
}

void
lrg_gui_push_id_ptr (LrgGuiContext *self,
                     gconstpointer  ptr)
{
	LrgGuiId id;

	g_return_if_fail (LRG_IS_GUI_CONTEXT (self));

	id = hash_bytes (&ptr, sizeof ptr, current_seed (self));
	g_array_append_val (self->id_stack, id);
}

void
lrg_gui_pop_id (LrgGuiContext *self)
{
	g_return_if_fail (LRG_IS_GUI_CONTEXT (self));
	g_return_if_fail (self->id_stack->len > 0);

	g_array_set_size (self->id_stack, self->id_stack->len - 1);
}

/* ==========================================================================
 * Frame lifecycle
 * ========================================================================== */

static gint
top_modal_depth (LrgGuiContext *self)
{
	gint i;

	for (i = (gint) self->popups->len - 1; i >= 0; i--)
		if (g_array_index (self->popups, GuiPopup, i).modal)
			return i + 1;
	return 0;
}

static void
compute_mouse_depth (LrgGuiContext *self)
{
	gint i;

	/* The top-most popup containing the pointer, by last frame's rects. */
	self->mouse_top_depth = 0;
	for (i = (gint) self->popups->len - 1; i >= 0; i--)
	{
		GuiPopup *p = &g_array_index (self->popups, GuiPopup, i);

		if (gui_rect_contains (&p->rect, self->input.mouse_x, self->input.mouse_y))
		{
			self->mouse_top_depth = i + 1;
			break;
		}
	}
	self->modal_depth = top_modal_depth (self);
}

void
lrg_gui_context_begin_frame (LrgGuiContext     *self,
                             const LrgGuiInput *input,
                             gfloat             width,
                             gfloat             height)
{
	gint i;

	g_return_if_fail (LRG_IS_GUI_CONTEXT (self));
	g_return_if_fail (input != NULL);
	g_return_if_fail (!self->in_frame);

	self->prev_mouse_x = self->input.mouse_x;
	self->prev_mouse_y = self->input.mouse_y;
	for (i = 0; i < LRG_GUI_MOUSE_BUTTON_COUNT; i++)
		self->prev_mouse_down[i] = self->input.mouse_down[i];

	self->input = *input;
	self->width = width;
	self->height = height;
	self->frame++;
	self->in_frame = TRUE;
	self->cursor = LRG_GUI_CURSOR_ARROW;
	self->wheel_x = input->wheel_x;
	self->wheel_y = input->wheel_y;

	/* Mouse transitions and double clicks. */
	for (i = 0; i < LRG_GUI_MOUSE_BUTTON_COUNT; i++)
	{
		self->mouse_clicked[i] = input->mouse_down[i] && !self->prev_mouse_down[i];
		self->mouse_released[i] = !input->mouse_down[i] && self->prev_mouse_down[i];
		self->mouse_double[i] = FALSE;
		if (self->mouse_clicked[i])
		{
			gdouble dt = input->time - self->last_click_time[i];
			gfloat  dx = input->mouse_x - self->last_click_x[i];
			gfloat  dy = input->mouse_y - self->last_click_y[i];

			if (dt < DOUBLE_CLICK_TIME && dx * dx + dy * dy < DOUBLE_CLICK_DIST * DOUBLE_CLICK_DIST)
			{
				self->mouse_double[i] = TRUE;
				self->last_click_time[i] = -1000.0;   /* no triple counting */
			}
			else
			{
				self->last_click_time[i] = input->time;
			}
			self->last_click_x[i] = input->mouse_x;
			self->last_click_y[i] = input->mouse_y;
			self->click_x[i] = input->mouse_x;
			self->click_y[i] = input->mouse_y;
		}
	}

	for (i = 0; i < GUI_N_LAYERS; i++)
		lrg_gui_draw_list_clear (self->layers[i]);
	lrg_gui_draw_list_clear (self->output);

	g_array_set_size (self->id_stack, 0);
	g_array_set_size (self->regions, 0);
	g_array_set_size (self->tree_indents, 0);
	g_array_set_size (self->tab_bars, 0);
	self->has_next_item_width = FALSE;
	self->next_item_open = -1;
	self->hovered_id = 0;
	self->any_hovered = FALSE;
	self->active_id_prev = self->active_id;
	self->active_seen = FALSE;
	self->focus_seen = FALSE;
	memset (&self->item, 0, sizeof self->item);
	g_clear_pointer (&self->tooltip, g_free);
	self->popup_level = 0;
	self->in_menubar = FALSE;
	self->dnd_dropped = FALSE;
	self->dnd_source_submitted = FALSE;
	if (!self->dnd_active)
	{
		g_clear_pointer (&self->dnd_type, g_free);
		g_clear_pointer (&self->dnd_data, g_free);
		g_clear_pointer (&self->dnd_preview, g_free);
	}

	/* Remember which popups were open before dismissal (menu headers use
	 * this to toggle closed instead of reopening). */
	g_hash_table_remove_all (self->popups_at_start);
	for (i = 0; i < (gint) self->popups->len; i++)
	{
		GuiPopup *p = &g_array_index (self->popups, GuiPopup, i);

		p->visited = FALSE;
		g_hash_table_add (self->popups_at_start, GUINT_TO_POINTER (p->id));
	}

	/* A press outside the top popups closes them (not modals). */
	compute_mouse_depth (self);
	if (self->mouse_clicked[0] || self->mouse_clicked[1] || self->mouse_clicked[2])
	{
		gint keep = MAX (self->mouse_top_depth, self->modal_depth);

		if (keep < (gint) self->popups->len)
			g_array_set_size (self->popups, keep);
		compute_mouse_depth (self);
	}

	/* Escape closes the top popup (unless a text field wants it). */
	if (self->popups->len > 0 && self->focus_id == 0 &&
	    gui_key_pressed (self, LRG_GUI_KEY_ESCAPE, FALSE, NULL))
	{
		g_array_set_size (self->popups, self->popups->len - 1);
		compute_mouse_depth (self);
	}

	/* The whole window is the implicit root region. */
	{
		LrgGuiRect root = { 0.0f, 0.0f, width, height };

		gui_begin_region_full (self, 1, &root, LRG_GUI_REGION_NO_PADDING, FALSE, -1,
		                       GUI_LAYER_BASE);
	}
}

static void
emit_overlay (LrgGuiContext *self)
{
	LrgGuiDrawList *dl = self->layers[GUI_LAYER_OVERLAY];
	const gchar    *text = NULL;
	gfloat          pad = self->style.frame_padding_x;

	/* Drag preview follows the pointer. */
	if (self->dnd_active && self->dnd_preview != NULL)
		text = self->dnd_preview;
	else if (self->tooltip != NULL)
		text = self->tooltip;

	if (text != NULL)
	{
		g_auto(GStrv) lines = g_strsplit (text, "\n", -1);
		gfloat        w = 0.0f;
		gfloat        line_h = self->style.font_size + 2.0f;
		LrgGuiRect    r;
		guint         i, n = g_strv_length (lines);

		for (i = 0; i < n; i++)
			w = MAX (w, gui_text_width (self, lines[i], -1, FALSE));

		r.w = w + pad * 2.0f;
		r.h = line_h * n + self->style.frame_padding_y * 2.0f;
		r.x = self->input.mouse_x + 14.0f;
		r.y = self->input.mouse_y + 18.0f;
		if (r.x + r.w > self->width)
			r.x = MAX (0.0f, self->width - r.w);
		if (r.y + r.h > self->height)
			r.y = MAX (0.0f, self->input.mouse_y - r.h - 4.0f);

		lrg_gui_draw_list_rect_filled (dl, &r, gui_col (self, LRG_GUI_COL_POPUP_BG), self->style.rounding);
		lrg_gui_draw_list_rect_outline (dl, &r, gui_col (self, LRG_GUI_COL_BORDER), 1.0f, self->style.rounding);
		for (i = 0; i < n; i++)
			lrg_gui_draw_list_text (dl, r.x + pad, r.y + self->style.frame_padding_y + line_h * i,
			                        lines[i], -1, self->style.font_size,
			                        gui_col (self, LRG_GUI_COL_TEXT), FALSE);
	}
}

void
lrg_gui_context_end_frame (LrgGuiContext *self)
{
	gint i;

	g_return_if_fail (LRG_IS_GUI_CONTEXT (self));
	g_return_if_fail (self->in_frame);

	/* Close the root region (and any left open by mistake). */
	while (self->regions->len > 0)
		lrg_gui_end_region (self);

	/* Popups not built this frame are gone. */
	for (i = 0; i < (gint) self->popups->len; i++)
	{
		if (!g_array_index (self->popups, GuiPopup, i).visited)
		{
			g_array_set_size (self->popups, i);
			break;
		}
	}

	/* An active widget that vanished releases the pointer. */
	if (self->active_id != 0 && !self->active_seen)
		self->active_id = 0;
	if (self->focus_id != 0 && !self->focus_seen)
		self->focus_id = 0;

	emit_overlay (self);

	/* Drag and drop: release ends it, whether or not it was accepted.
	 * The payload strings stay valid until the next frame begins, so a
	 * caller may keep using what lrg_gui_accept_drop() returned. */
	if (self->dnd_active && !self->input.mouse_down[LRG_GUI_MOUSE_LEFT])
		self->dnd_active = FALSE;

	for (i = 0; i < GUI_N_LAYERS; i++)
		lrg_gui_draw_list_append (self->output, self->layers[i]);

	self->in_frame = FALSE;
}

LrgGuiDrawList *
lrg_gui_context_get_draw_list (LrgGuiContext *self)
{
	g_return_val_if_fail (LRG_IS_GUI_CONTEXT (self), NULL);

	return self->output;
}

guint64
lrg_gui_context_get_frame (LrgGuiContext *self)
{
	g_return_val_if_fail (LRG_IS_GUI_CONTEXT (self), 0);

	return self->frame;
}

LrgGuiCursor
lrg_gui_context_get_cursor (LrgGuiContext *self)
{
	g_return_val_if_fail (LRG_IS_GUI_CONTEXT (self), LRG_GUI_CURSOR_ARROW);

	return self->cursor;
}

gboolean
lrg_gui_context_wants_mouse (LrgGuiContext *self)
{
	g_return_val_if_fail (LRG_IS_GUI_CONTEXT (self), FALSE);

	return self->any_hovered || self->active_id != 0 || self->mouse_top_depth > 0 ||
	       self->modal_depth > 0 || self->dnd_active;
}

gboolean
lrg_gui_context_wants_keyboard (LrgGuiContext *self)
{
	g_return_val_if_fail (LRG_IS_GUI_CONTEXT (self), FALSE);

	return self->focus_id != 0;
}

gboolean
lrg_gui_context_needs_redraw (LrgGuiContext *self)
{
	g_return_val_if_fail (LRG_IS_GUI_CONTEXT (self), FALSE);

	return self->focus_id != 0 || self->active_id != 0 || self->dnd_active ||
	       self->tooltip != NULL || self->hover_item_for_tooltip != 0;
}

/* ==========================================================================
 * Regions and layout
 * ========================================================================== */

void
gui_begin_region_full (LrgGuiContext     *self,
                       LrgGuiId           id,
                       const LrgGuiRect  *rect,
                       LrgGuiRegionFlags  flags,
                       gboolean           is_popup,
                       gint               popup_index,
                       GuiLayer           layer)
{
	GuiRegion  region;
	GuiRegion *parent = (self->regions->len > 0) ? gui_region (self) : NULL;
	GuiState  *st = gui_state (self, id);
	gfloat     pad = (flags & LRG_GUI_REGION_NO_PADDING) ? 0.0f : self->style.region_padding;
	gfloat     content_h, visible_h, content_w, visible_w;

	memset (&region, 0, sizeof region);
	region.id = id;
	region.rect = *rect;
	region.flags = flags;
	region.is_popup = is_popup;
	region.popup_index = popup_index;
	region.layer = layer;
	region.depth = is_popup ? popup_index + 1 : (parent != NULL ? parent->depth : 0);

	/* Popups clip only to the window, everything else to its parent. */
	if (parent != NULL && !is_popup)
		region.clip = gui_rect_intersect (rect, &parent->clip);
	else
	{
		LrgGuiRect win = { 0.0f, 0.0f, self->width, self->height };

		region.clip = gui_rect_intersect (rect, &win);
	}

	/* Scroll limits come from last frame's measured content. */
	content_w = st->f2;
	content_h = st->f3;
	visible_w = rect->w - pad * 2.0f;
	visible_h = rect->h - pad * 2.0f;
	region.scroll_x = (flags & LRG_GUI_REGION_SCROLL_X) ? CLAMP (st->f0, 0.0f, MAX (0.0f, content_w - visible_w)) : 0.0f;
	region.scroll_y = (flags & LRG_GUI_REGION_SCROLL_Y) ? CLAMP (st->f1, 0.0f, MAX (0.0f, content_h - visible_h)) : 0.0f;

	region.content_w = rect->w - pad * 2.0f;
	if ((flags & LRG_GUI_REGION_SCROLL_Y) && content_h > visible_h + 0.5f)
		region.content_w -= self->style.scrollbar_size;

	region.origin_x = rect->x + pad;
	region.origin_y = rect->y + pad;
	region.cursor_x = region.origin_x - region.scroll_x;
	region.cursor_y = region.origin_y - region.scroll_y;
	region.line_y = region.cursor_y;
	region.prev_line_y = region.cursor_y;
	region.max_x = region.cursor_x;
	region.max_y = region.cursor_y;

	g_array_append_val (self->regions, region);

	/* Background before clipping so the border is not clipped away. */
	if (flags & LRG_GUI_REGION_BACKGROUND)
		lrg_gui_draw_list_rect_filled (self->layers[layer], rect,
		                               gui_col (self, is_popup ? LRG_GUI_COL_POPUP_BG
		                                                       : LRG_GUI_COL_PANEL_BG), 0.0f);
	lrg_gui_draw_list_push_clip (self->layers[layer], &gui_region (self)->clip);
}

void
lrg_gui_begin_region (LrgGuiContext     *self,
                      const gchar       *str_id,
                      const LrgGuiRect  *rect,
                      LrgGuiRegionFlags  flags)
{
	g_return_if_fail (LRG_IS_GUI_CONTEXT (self));
	g_return_if_fail (str_id != NULL);

	if (rect == NULL)
	{
		lrg_gui_begin_child (self, str_id, 0.0f, 0.0f, flags);
		return;
	}

	gui_begin_region_full (self, gui_hash_label (self, str_id), rect, flags, FALSE, -1,
	                       gui_region (self)->layer);
}

void
lrg_gui_begin_child (LrgGuiContext     *self,
                     const gchar       *str_id,
                     gfloat             width,
                     gfloat             height,
                     LrgGuiRegionFlags  flags)
{
	gfloat     avail_w, avail_h;
	LrgGuiRect rect;
	LrgGuiId   id;

	g_return_if_fail (LRG_IS_GUI_CONTEXT (self));
	g_return_if_fail (str_id != NULL);

	lrg_gui_get_content_avail (self, &avail_w, &avail_h);
	if (width <= 0.0f)
		width = MAX (1.0f, avail_w + width);
	if (height <= 0.0f)
		height = MAX (1.0f, avail_h + height);

	id = gui_hash_label (self, str_id);
	gui_item_add (self, id, width, height, &rect);
	gui_begin_region_full (self, id, &rect, flags, FALSE, -1, gui_region (self)->layer);
}

static void
draw_scrollbar (LrgGuiContext *self,
                GuiRegion     *r,
                GuiState      *st,
                gfloat         content_h,
                gfloat         visible_h)
{
	LrgGuiDrawList *dl = self->layers[r->layer];
	LrgGuiRect      track, thumb;
	gfloat          sb = self->style.scrollbar_size;
	gfloat          ratio, thumb_h, max_scroll;
	LrgGuiId        id = r->id + 7;
	gboolean        hovered, held;

	track.x = r->rect.x + r->rect.w - sb;
	track.y = r->rect.y;
	track.w = sb;
	track.h = r->rect.h;

	max_scroll = MAX (1.0f, content_h - visible_h);
	ratio = visible_h / content_h;
	thumb_h = MAX (sb * 2.0f, track.h * ratio);
	thumb.x = track.x + 2.0f;
	thumb.w = sb - 4.0f;
	thumb.h = thumb_h;
	thumb.y = track.y + (track.h - thumb_h) * (r->scroll_y / max_scroll);

	/* Dragging the thumb (or clicking the track) scrolls. */
	gui_button_behavior (self, id, &track, GUI_BUTTON_ON_PRESS, &hovered, &held);
	if (held)
	{
		gfloat t = (self->input.mouse_y - track.y - thumb_h * 0.5f) / MAX (1.0f, track.h - thumb_h);

		st->f1 = CLAMP (t, 0.0f, 1.0f) * max_scroll;
	}

	lrg_gui_draw_list_rect_filled (dl, &track, gui_col (self, LRG_GUI_COL_SCROLLBAR_BG), 0.0f);
	lrg_gui_draw_list_rect_filled (dl, &thumb,
	                               gui_col (self, (hovered || held) ? LRG_GUI_COL_FRAME_BG_ACTIVE
	                                                                : LRG_GUI_COL_SCROLLBAR_GRAB),
	                               thumb.w * 0.5f);
}

void
lrg_gui_end_region (LrgGuiContext *self)
{
	GuiRegion *r;
	GuiState  *st;
	gfloat     pad, content_w, content_h, visible_w, visible_h;
	gboolean   hovered;

	g_return_if_fail (LRG_IS_GUI_CONTEXT (self));
	g_return_if_fail (self->regions->len > 0);

	r = gui_region (self);
	st = gui_state (self, r->id);
	pad = (r->flags & LRG_GUI_REGION_NO_PADDING) ? 0.0f : self->style.region_padding;

	/* Measure what was laid out. */
	content_w = (r->max_x - (r->origin_x - r->scroll_x));
	content_h = (r->max_y - (r->origin_y - r->scroll_y));
	visible_w = r->rect.w - pad * 2.0f;
	visible_h = r->rect.h - pad * 2.0f;
	st->f2 = content_w;
	st->f3 = content_h;

	lrg_gui_draw_list_pop_clip (self->layers[r->layer]);

	/* Wheel scrolling: the innermost hovered scrollable region takes it. */
	hovered = gui_rect_contains (&r->clip, self->input.mouse_x, self->input.mouse_y) &&
	          r->depth == self->mouse_top_depth && r->depth >= self->modal_depth;
	if ((r->flags & LRG_GUI_REGION_SCROLL_Y) && hovered && self->wheel_y != 0.0f &&
	    !(self->input.mods & LRG_GUI_MOD_SHIFT) && content_h > visible_h)
	{
		st->f1 = CLAMP (r->scroll_y - self->wheel_y * (self->style.font_size * 3.0f),
		                0.0f, content_h - visible_h);
		self->wheel_y = 0.0f;
	}
	else if (r->flags & LRG_GUI_REGION_SCROLL_Y)
	{
		st->f1 = CLAMP (st->f1, 0.0f, MAX (0.0f, content_h - visible_h));
	}

	if ((r->flags & LRG_GUI_REGION_SCROLL_X) && hovered && content_w > visible_w)
	{
		gfloat wx = (self->input.mods & LRG_GUI_MOD_SHIFT) ? self->wheel_y : self->wheel_x;

		if (wx != 0.0f)
		{
			st->f0 = CLAMP (r->scroll_x - wx * (self->style.font_size * 3.0f),
			                0.0f, content_w - visible_w);
			self->wheel_x = 0.0f;
			if (self->input.mods & LRG_GUI_MOD_SHIFT)
				self->wheel_y = 0.0f;
		}
	}

	if ((r->flags & LRG_GUI_REGION_SCROLL_Y) && content_h > visible_h + 0.5f)
		draw_scrollbar (self, r, st, content_h, visible_h);

	if (r->flags & LRG_GUI_REGION_BORDER)
		lrg_gui_draw_list_rect_outline (self->layers[r->layer], &r->rect,
		                                gui_col (self, LRG_GUI_COL_BORDER), 1.0f, 0.0f);

	g_array_set_size (self->regions, self->regions->len - 1);
}

void
lrg_gui_get_region_rect (LrgGuiContext *self,
                         LrgGuiRect    *rect)
{
	g_return_if_fail (LRG_IS_GUI_CONTEXT (self));
	g_return_if_fail (rect != NULL);

	*rect = gui_region (self)->rect;
}

/* Where the next item would go, honouring same_line. */
static void
next_item_pos (LrgGuiContext *self,
               gfloat        *x,
               gfloat        *y)
{
	GuiRegion *r = gui_region (self);

	if (r->same_line)
	{
		*x = r->cursor_x;
		*y = r->prev_line_y;
	}
	else
	{
		*x = r->cursor_x;
		*y = r->cursor_y;
	}
}

void
lrg_gui_get_content_avail (LrgGuiContext *self,
                           gfloat        *width,
                           gfloat        *height)
{
	GuiRegion *r;
	gfloat     x, y, pad;

	g_return_if_fail (LRG_IS_GUI_CONTEXT (self));

	r = gui_region (self);
	pad = (r->flags & LRG_GUI_REGION_NO_PADDING) ? 0.0f : self->style.region_padding;
	next_item_pos (self, &x, &y);
	if (width != NULL)
		*width = MAX (0.0f, (r->origin_x - r->scroll_x + r->content_w) - x);
	if (height != NULL)
		*height = MAX (0.0f, (r->rect.y + r->rect.h - pad) - y);
}

void
lrg_gui_get_cursor (LrgGuiContext *self,
                    gfloat        *x,
                    gfloat        *y)
{
	gfloat cx, cy;

	g_return_if_fail (LRG_IS_GUI_CONTEXT (self));

	next_item_pos (self, &cx, &cy);
	if (x != NULL)
		*x = cx;
	if (y != NULL)
		*y = cy;
}

void
lrg_gui_set_cursor (LrgGuiContext *self,
                    gfloat         x,
                    gfloat         y)
{
	GuiRegion *r;

	g_return_if_fail (LRG_IS_GUI_CONTEXT (self));

	r = gui_region (self);
	r->cursor_x = x;
	r->cursor_y = y;
	r->line_y = y;
	r->line_h = 0.0f;
	r->same_line = FALSE;
}

/*
 * gui_item_add:
 *
 * Lays out an item of @w x @h at the cursor and advances the cursor.
 * Returns TRUE if any part of it is inside the clip (worth drawing).
 */
gboolean
gui_item_add (LrgGuiContext *self,
              LrgGuiId       id,
              gfloat         w,
              gfloat         h,
              LrgGuiRect    *out)
{
	GuiRegion *r = gui_region (self);
	gboolean   horizontal = (r->flags & LRG_GUI_REGION_HORIZONTAL) != 0;
	LrgGuiRect rect;

	if (r->same_line || horizontal)
	{
		/* Continue the current line. */
		rect.x = r->cursor_x;
		rect.y = r->same_line ? r->prev_line_y : r->line_y;
		if (!r->same_line && horizontal && r->line_h == 0.0f)
			rect.y = r->cursor_y;
	}
	else
	{
		rect.x = r->origin_x - r->scroll_x + r->indent;
		rect.y = r->cursor_y;
	}
	rect.w = w;
	rect.h = h;

	r->prev_line_y = rect.y;
	r->line_h = (r->same_line || horizontal) ? MAX (r->line_h, h) : h;
	r->prev_line_h = r->line_h;
	r->prev_item_x2 = rect.x + w;
	r->same_line = FALSE;

	if (horizontal)
	{
		/* Toolbars and menu bars keep flowing right on one line. */
		r->cursor_x = rect.x + w + self->style.item_spacing_x;
		r->line_y = rect.y;
		r->cursor_y = rect.y + r->line_h + self->style.item_spacing_y;
	}
	else
	{
		r->cursor_x = r->origin_x - r->scroll_x + r->indent;
		r->cursor_y = rect.y + r->line_h + self->style.item_spacing_y;
		r->line_y = r->cursor_y;
	}

	r->max_x = MAX (r->max_x, rect.x + w);
	r->max_y = MAX (r->max_y, rect.y + h);

	self->item.id = id;
	self->item.rect = rect;
	self->item.valid = TRUE;
	self->item.hovered = FALSE;
	self->item.active = FALSE;
	self->item.activated = FALSE;
	self->item.deactivated = FALSE;
	self->item.double_clicked = FALSE;
	memset (self->item.clicked, 0, sizeof self->item.clicked);

	if (out != NULL)
		*out = rect;

	return rect.x < r->clip.x + r->clip.w && rect.x + rect.w > r->clip.x &&
	       rect.y < r->clip.y + r->clip.h && rect.y + rect.h > r->clip.y;
}

void
lrg_gui_same_line (LrgGuiContext *self,
                   gfloat         spacing)
{
	GuiRegion *r;

	g_return_if_fail (LRG_IS_GUI_CONTEXT (self));

	r = gui_region (self);
	r->cursor_x = r->prev_item_x2 + (spacing < 0.0f ? self->style.item_spacing_x : spacing);
	r->cursor_y = r->prev_line_y;
	r->same_line = TRUE;
}

void
lrg_gui_spacing (LrgGuiContext *self)
{
	g_return_if_fail (LRG_IS_GUI_CONTEXT (self));

	lrg_gui_dummy (self, 0.0f, self->style.item_spacing_y);
}

void
lrg_gui_dummy (LrgGuiContext *self,
               gfloat         width,
               gfloat         height)
{
	g_return_if_fail (LRG_IS_GUI_CONTEXT (self));

	gui_item_add (self, 0, width, height, NULL);
}

void
lrg_gui_separator (LrgGuiContext *self)
{
	LrgGuiRect rect;
	gfloat     w;

	g_return_if_fail (LRG_IS_GUI_CONTEXT (self));

	lrg_gui_get_content_avail (self, &w, NULL);
	if (gui_item_add (self, 0, w, 1.0f, &rect))
		lrg_gui_draw_list_line (gui_dl (self), rect.x, rect.y + 0.5f, rect.x + rect.w,
		                        rect.y + 0.5f, gui_col (self, LRG_GUI_COL_SEPARATOR), 1.0f);
}

void
lrg_gui_separator_text (LrgGuiContext *self,
                        const gchar   *label)
{
	LrgGuiRect  rect;
	gfloat      w, tw, h, mid;
	LrgGuiColor sep;

	g_return_if_fail (LRG_IS_GUI_CONTEXT (self));
	g_return_if_fail (label != NULL);

	lrg_gui_get_content_avail (self, &w, NULL);
	h = self->style.font_size + self->style.item_spacing_y;
	if (!gui_item_add (self, 0, w, h, &rect))
		return;

	tw = gui_text_width (self, label, -1, FALSE);
	mid = rect.y + h * 0.5f;
	sep = gui_col (self, LRG_GUI_COL_SEPARATOR);
	lrg_gui_draw_list_line (gui_dl (self), rect.x, mid, rect.x + 8.0f, mid, sep, 1.0f);
	gui_draw_text (self, rect.x + 12.0f, rect.y + self->style.item_spacing_y * 0.5f, label, -1,
	               gui_col (self, LRG_GUI_COL_TEXT_DISABLED), FALSE);
	lrg_gui_draw_list_line (gui_dl (self), rect.x + 16.0f + tw, mid, rect.x + rect.w, mid, sep, 1.0f);
}

void
lrg_gui_indent (LrgGuiContext *self,
                gfloat         width)
{
	GuiRegion *r;

	g_return_if_fail (LRG_IS_GUI_CONTEXT (self));

	r = gui_region (self);
	r->indent += (width > 0.0f) ? width : self->style.indent;
	if (!r->same_line)
		r->cursor_x = r->origin_x - r->scroll_x + r->indent;
}

void
lrg_gui_unindent (LrgGuiContext *self,
                  gfloat         width)
{
	GuiRegion *r;

	g_return_if_fail (LRG_IS_GUI_CONTEXT (self));

	r = gui_region (self);
	r->indent = MAX (0.0f, r->indent - ((width > 0.0f) ? width : self->style.indent));
	if (!r->same_line)
		r->cursor_x = r->origin_x - r->scroll_x + r->indent;
}

void
lrg_gui_set_next_item_width (LrgGuiContext *self,
                             gfloat         width)
{
	g_return_if_fail (LRG_IS_GUI_CONTEXT (self));

	self->next_item_width = width;
	self->has_next_item_width = TRUE;
}

/* Width for the next widget: explicit, or @fallback (<= 0 = fill). */
gfloat
gui_item_width (LrgGuiContext *self,
                gfloat         fallback)
{
	gfloat avail;
	gfloat w;

	lrg_gui_get_content_avail (self, &avail, NULL);
	if (self->has_next_item_width)
	{
		self->has_next_item_width = FALSE;
		w = self->next_item_width;
	}
	else
	{
		w = fallback;
	}

	if (w <= 0.0f)
		w = avail + w;

	return MAX (w, 4.0f);
}

void
lrg_gui_property_label (LrgGuiContext *self,
                        const gchar   *label)
{
	GuiRegion  *r;
	LrgGuiRect  rect;
	gfloat      avail, label_w;
	const gchar *end;

	g_return_if_fail (LRG_IS_GUI_CONTEXT (self));
	g_return_if_fail (label != NULL);

	r = gui_region (self);
	lrg_gui_get_content_avail (self, &avail, NULL);
	label_w = roundf (MAX (avail * self->style.label_ratio, 40.0f));
	end = gui_label_end (label);

	/* Label cell, vertically centred on a frame-height row. */
	if (gui_item_add (self, 0, label_w, gui_frame_height (self), &rect))
	{
		LrgGuiRect cell = rect;

		cell.w -= self->style.item_spacing_x;
		gui_draw_text_clipped (self, &cell, label, end - label,
		                       gui_col (self, LRG_GUI_COL_TEXT_DISABLED), 0.0f, FALSE);
	}

	lrg_gui_same_line (self, 0.0f);
	lrg_gui_set_next_item_width (self, MAX (4.0f, avail - label_w));
	(void) r;
}

gfloat
lrg_gui_calc_text_width (LrgGuiContext *self,
                         const gchar   *text)
{
	g_return_val_if_fail (LRG_IS_GUI_CONTEXT (self), 0.0f);
	g_return_val_if_fail (text != NULL, 0.0f);

	return gui_text_width (self, text, gui_label_end (text) - text, FALSE);
}

gfloat
lrg_gui_get_frame_height (LrgGuiContext *self)
{
	g_return_val_if_fail (LRG_IS_GUI_CONTEXT (self), 0.0f);

	return gui_frame_height (self);
}

LrgGuiDrawList *
lrg_gui_get_draw_list (LrgGuiContext *self)
{
	g_return_val_if_fail (LRG_IS_GUI_CONTEXT (self), NULL);

	return gui_dl (self);
}

void
lrg_gui_draw_callback (LrgGuiContext      *self,
                       const LrgGuiRect   *rect,
                       LrgGuiDrawCallback  callback,
                       gpointer            user_data)
{
	g_return_if_fail (LRG_IS_GUI_CONTEXT (self));

	lrg_gui_draw_list_callback (gui_dl (self), rect, callback, user_data);
}

/* ==========================================================================
 * Drawing helpers
 * ========================================================================== */

void
gui_draw_text (LrgGuiContext *self,
               gfloat         x,
               gfloat         y,
               const gchar   *text,
               gssize         len,
               LrgGuiColor    color,
               gboolean       mono)
{
	lrg_gui_draw_list_text (gui_dl (self), roundf (x), roundf (y), text, len,
	                        self->style.font_size, color, mono);
}

/* Text vertically centred in @rect, horizontally aligned by @align_x
 * (0 left, 0.5 centre, 1 right) and clipped to it. */
void
gui_draw_text_clipped (LrgGuiContext    *self,
                       const LrgGuiRect *rect,
                       const gchar      *text,
                       gssize            len,
                       LrgGuiColor       color,
                       gfloat            align_x,
                       gboolean          mono)
{
	gfloat tw = gui_text_width (self, text, len, mono);
	gfloat x = rect->x + MAX (0.0f, (rect->w - tw) * align_x);
	gfloat y = rect->y + (rect->h - self->style.font_size) * 0.5f;
	gboolean clip = tw > rect->w;

	if (clip)
		lrg_gui_draw_list_push_clip (gui_dl (self), rect);
	gui_draw_text (self, x, y, text, len, color, mono);
	if (clip)
		lrg_gui_draw_list_pop_clip (gui_dl (self));
}

void
gui_draw_frame (LrgGuiContext    *self,
                const LrgGuiRect *rect,
                LrgGuiColor       fill,
                gboolean          border)
{
	lrg_gui_draw_list_rect_filled (gui_dl (self), rect, fill, self->style.rounding);
	if (border)
		lrg_gui_draw_list_rect_outline (gui_dl (self), rect, gui_col (self, LRG_GUI_COL_BORDER),
		                                1.0f, self->style.rounding);
}

void
gui_draw_arrow (LrgGuiContext *self,
                gfloat         x,
                gfloat         y,
                gfloat         size,
                gboolean       down,
                LrgGuiColor    color)
{
	gfloat h = size * 0.5f;

	/* x, y is the centre. */
	if (down)
		lrg_gui_draw_list_triangle (gui_dl (self), x - h, y - h * 0.5f, x + h, y - h * 0.5f,
		                            x, y + h * 0.7f, color);
	else
		lrg_gui_draw_list_triangle (gui_dl (self), x - h * 0.5f, y - h, x - h * 0.5f, y + h,
		                            x + h * 0.7f, y, color);
}

void
gui_draw_check (LrgGuiContext    *self,
                const LrgGuiRect *rect,
                LrgGuiColor       color)
{
	gfloat t = MAX (1.5f, rect->w / 6.0f);
	gfloat x0 = rect->x + rect->w * 0.2f, y0 = rect->y + rect->h * 0.5f;
	gfloat x1 = rect->x + rect->w * 0.42f, y1 = rect->y + rect->h * 0.72f;
	gfloat x2 = rect->x + rect->w * 0.8f, y2 = rect->y + rect->h * 0.28f;

	lrg_gui_draw_list_line (gui_dl (self), x0, y0, x1, y1, color, t);
	lrg_gui_draw_list_line (gui_dl (self), x1, y1, x2, y2, color, t);
}

/* ==========================================================================
 * Interaction
 * ========================================================================== */

gboolean
gui_hoverable (LrgGuiContext    *self,
               const LrgGuiRect *rect)
{
	GuiRegion *r = gui_region (self);
	gfloat     mx = self->input.mouse_x;
	gfloat     my = self->input.mouse_y;

	if (!gui_rect_contains (rect, mx, my) || !gui_rect_contains (&r->clip, mx, my))
		return FALSE;
	if (r->depth != self->mouse_top_depth || r->depth < self->modal_depth)
		return FALSE;

	/* While something else is held, nothing else hovers (except drop
	 * targets, which check the drag payload themselves). */
	if (self->active_id != 0 && self->active_id != self->item.id && !self->dnd_active)
		return FALSE;

	return TRUE;
}

void
gui_set_active (LrgGuiContext *self,
                LrgGuiId       id)
{
	self->active_id = id;
	self->active_seen = TRUE;
	self->active_accum = 0.0f;
}

void
gui_clear_active (LrgGuiContext *self)
{
	self->active_id = 0;
}

/*
 * gui_button_behavior:
 *
 * The shared press/hold/release logic. Fills the item flags and returns
 * TRUE on a click (per GUI_BUTTON_ON_PRESS / ON_RELEASE).
 */
gboolean
gui_button_behavior (LrgGuiContext  *self,
                     LrgGuiId        id,
                     const LrgGuiRect *rect,
                     GuiButtonFlags  flags,
                     gboolean       *out_hovered,
                     gboolean       *out_held)
{
	gboolean hovered;
	gboolean held = FALSE;
	gboolean clicked = FALSE;
	gint     b;

	self->item.id = id;
	self->item.rect = *rect;
	hovered = gui_hoverable (self, rect);
	if (hovered)
	{
		self->hovered_id = id;
		self->any_hovered = TRUE;
	}

	if (self->active_id == id)
		self->active_seen = TRUE;

	/* Press on a hovered item. */
	for (b = 0; b < LRG_GUI_MOUSE_BUTTON_COUNT; b++)
	{
		gboolean accept = (b == LRG_GUI_MOUSE_LEFT) ||
		                  (b == LRG_GUI_MOUSE_RIGHT && (flags & GUI_BUTTON_RIGHT)) ||
		                  (b == LRG_GUI_MOUSE_MIDDLE && (flags & GUI_BUTTON_MIDDLE));

		if (hovered && self->mouse_clicked[b])
		{
			self->item.clicked[b] = TRUE;
			if (b == LRG_GUI_MOUSE_LEFT && self->mouse_double[b])
				self->item.double_clicked = TRUE;

			if (accept && !(flags & GUI_BUTTON_NO_ACTIVATE))
			{
				gui_set_active (self, id);
				if (flags & GUI_BUTTON_ON_PRESS)
					clicked = TRUE;
			}
		}
	}

	if (self->active_id == id)
	{
		gboolean any_down = self->input.mouse_down[0] ||
		                    ((flags & GUI_BUTTON_RIGHT) && self->input.mouse_down[1]) ||
		                    ((flags & GUI_BUTTON_MIDDLE) && self->input.mouse_down[2]);

		if (any_down)
		{
			held = TRUE;
		}
		else
		{
			/* Release: a click if still over the item. */
			if (!(flags & GUI_BUTTON_ON_PRESS) && gui_rect_contains (rect, self->input.mouse_x,
			                                                         self->input.mouse_y))
				clicked = TRUE;
			gui_clear_active (self);
		}
	}

	self->item.hovered = hovered;
	self->item.active = (self->active_id == id);
	self->item.activated = self->item.active && self->active_id_prev != id;
	self->item.deactivated = !self->item.active && self->active_id_prev == id;

	/* Tooltip hover timer. */
	if (hovered)
	{
		if (self->hover_item_for_tooltip != id)
		{
			self->hover_item_for_tooltip = id;
			self->hover_start = self->input.time;
		}
	}

	if (out_hovered != NULL)
		*out_hovered = hovered;
	if (out_held != NULL)
		*out_held = held;

	return clicked;
}

/* Records a non-interactive item for queries (text, images). */
void
gui_finish_item (LrgGuiContext    *self,
                 LrgGuiId          id,
                 const LrgGuiRect *rect)
{
	gint b;

	self->item.id = id;
	self->item.rect = *rect;
	self->item.hovered = gui_hoverable (self, rect);
	if (self->item.hovered)
	{
		self->any_hovered = TRUE;
		for (b = 0; b < LRG_GUI_MOUSE_BUTTON_COUNT; b++)
			self->item.clicked[b] = self->mouse_clicked[b];
		self->item.double_clicked = self->mouse_double[0];
		if (self->hover_item_for_tooltip != id || id == 0)
		{
			self->hover_item_for_tooltip = id;
			self->hover_start = self->input.time;
		}
	}
}

/* ==========================================================================
 * Text
 * ========================================================================== */

static void
text_item (LrgGuiContext *self,
           const gchar   *text,
           LrgGuiColor    color,
           gboolean       mono)
{
	LrgGuiRect rect;
	gfloat     w = gui_text_width (self, text, -1, mono);
	gfloat     h = self->style.font_size;

	if (gui_item_add (self, 0, w, h, &rect))
		gui_draw_text (self, rect.x, rect.y, text, -1, color, mono);
	gui_finish_item (self, 0, &rect);
}

void
lrg_gui_text (LrgGuiContext *self,
              const gchar   *text)
{
	g_return_if_fail (LRG_IS_GUI_CONTEXT (self));
	g_return_if_fail (text != NULL);

	text_item (self, text, gui_col (self, LRG_GUI_COL_TEXT), FALSE);
}

void
lrg_gui_textf (LrgGuiContext *self,
               const gchar   *format,
               ...)
{
	g_autofree gchar *text = NULL;
	va_list           args;

	g_return_if_fail (LRG_IS_GUI_CONTEXT (self));
	g_return_if_fail (format != NULL);

	va_start (args, format);
	text = g_strdup_vprintf (format, args);
	va_end (args);

	text_item (self, text, gui_col (self, LRG_GUI_COL_TEXT), FALSE);
}

void
lrg_gui_text_colored (LrgGuiContext *self,
                      LrgGuiColor    color,
                      const gchar   *text)
{
	g_return_if_fail (LRG_IS_GUI_CONTEXT (self));
	g_return_if_fail (text != NULL);

	text_item (self, text, color, FALSE);
}

void
lrg_gui_text_disabled (LrgGuiContext *self,
                       const gchar   *text)
{
	g_return_if_fail (LRG_IS_GUI_CONTEXT (self));
	g_return_if_fail (text != NULL);

	text_item (self, text, gui_col (self, LRG_GUI_COL_TEXT_DISABLED), FALSE);
}

void
lrg_gui_text_mono (LrgGuiContext *self,
                   LrgGuiColor    color,
                   const gchar   *text)
{
	g_return_if_fail (LRG_IS_GUI_CONTEXT (self));
	g_return_if_fail (text != NULL);

	text_item (self, text, color, TRUE);
}

void
lrg_gui_text_wrapped (LrgGuiContext *self,
                      const gchar   *text)
{
	gfloat       avail;
	const gchar *line;
	const gchar *p;

	g_return_if_fail (LRG_IS_GUI_CONTEXT (self));
	g_return_if_fail (text != NULL);

	lrg_gui_get_content_avail (self, &avail, NULL);
	avail = MAX (avail, self->style.font_size * 4.0f);

	/* Greedy word wrap; explicit newlines force breaks. */
	line = text;
	while (*line != '\0')
	{
		const gchar *last_space = NULL;
		const gchar *end = line;
		g_autofree gchar *chunk = NULL;

		for (p = line; *p != '\0' && *p != '\n'; p = g_utf8_next_char (p))
		{
			const gchar *next = g_utf8_next_char (p);

			if (*p == ' ')
				last_space = p;
			if (gui_text_width (self, line, next - line, FALSE) > avail && p > line)
			{
				end = (last_space != NULL) ? last_space : p;
				break;
			}
			end = next;
		}
		if (*p == '\0' || *p == '\n')
			end = p;

		chunk = g_strndup (line, end - line);
		lrg_gui_text (self, chunk);

		line = end;
		if (*line == ' ' || *line == '\n')
			line++;
	}
}

/* ==========================================================================
 * Buttons
 * ========================================================================== */

gboolean
lrg_gui_button_sized (LrgGuiContext *self,
                      const gchar   *label,
                      gfloat         width,
                      gfloat         height)
{
	LrgGuiId     id;
	LrgGuiRect   rect;
	const gchar *end;
	gboolean     hovered, held, clicked, visible;
	gfloat       tw;

	g_return_val_if_fail (LRG_IS_GUI_CONTEXT (self), FALSE);
	g_return_val_if_fail (label != NULL, FALSE);

	id = gui_hash_label (self, label);
	end = gui_label_end (label);
	tw = gui_text_width (self, label, end - label, FALSE);

	if (self->has_next_item_width)
		width = gui_item_width (self, 0.0f);
	else if (width <= 0.0f)
		width = tw + self->style.frame_padding_x * 2.0f;
	if (height <= 0.0f)
		height = gui_frame_height (self);

	visible = gui_item_add (self, id, width, height, &rect);
	clicked = gui_button_behavior (self, id, &rect, GUI_BUTTON_ON_RELEASE, &hovered, &held);

	if (visible)
	{
		LrgGuiStyleColor c = held ? LRG_GUI_COL_BUTTON_ACTIVE
		                          : (hovered ? LRG_GUI_COL_BUTTON_HOVERED : LRG_GUI_COL_BUTTON);

		gui_draw_frame (self, &rect, gui_col (self, c), FALSE);
		gui_draw_text_clipped (self, &rect, label, end - label,
		                       gui_col (self, LRG_GUI_COL_TEXT), 0.5f, FALSE);
	}

	return clicked;
}

gboolean
lrg_gui_button (LrgGuiContext *self,
                const gchar   *label)
{
	return lrg_gui_button_sized (self, label, 0.0f, 0.0f);
}

gboolean
lrg_gui_toggle_button (LrgGuiContext *self,
                       const gchar   *label,
                       gboolean      *active)
{
	LrgGuiId     id;
	LrgGuiRect   rect;
	const gchar *end;
	gboolean     hovered, held, clicked, visible;
	gfloat       w;

	g_return_val_if_fail (LRG_IS_GUI_CONTEXT (self), FALSE);
	g_return_val_if_fail (label != NULL, FALSE);
	g_return_val_if_fail (active != NULL, FALSE);

	id = gui_hash_label (self, label);
	end = gui_label_end (label);
	w = self->has_next_item_width ? gui_item_width (self, 0.0f)
	                              : gui_text_width (self, label, end - label, FALSE) +
	                                self->style.frame_padding_x * 2.0f;

	visible = gui_item_add (self, id, w, gui_frame_height (self), &rect);
	clicked = gui_button_behavior (self, id, &rect, GUI_BUTTON_ON_RELEASE, &hovered, &held);
	if (clicked)
		*active = !*active;

	if (visible)
	{
		LrgGuiColor fill;

		if (*active)
			fill = gui_col (self, LRG_GUI_COL_HEADER_ACTIVE);
		else
			fill = gui_col (self, held ? LRG_GUI_COL_BUTTON_ACTIVE
			                           : (hovered ? LRG_GUI_COL_BUTTON_HOVERED : LRG_GUI_COL_BUTTON));
		gui_draw_frame (self, &rect, fill, FALSE);
		gui_draw_text_clipped (self, &rect, label, end - label, gui_col (self, LRG_GUI_COL_TEXT),
		                       0.5f, FALSE);
	}

	return clicked;
}

gboolean
lrg_gui_invisible_button (LrgGuiContext *self,
                          const gchar   *str_id,
                          gfloat         width,
                          gfloat         height)
{
	LrgGuiId   id;
	LrgGuiRect rect;

	g_return_val_if_fail (LRG_IS_GUI_CONTEXT (self), FALSE);
	g_return_val_if_fail (str_id != NULL, FALSE);

	id = gui_hash_label (self, str_id);
	gui_item_add (self, id, MAX (width, 1.0f), MAX (height, 1.0f), &rect);
	return gui_button_behavior (self, id, &rect,
	                            GUI_BUTTON_ON_RELEASE | GUI_BUTTON_RIGHT | GUI_BUTTON_MIDDLE,
	                            NULL, NULL);
}

gboolean
lrg_gui_checkbox (LrgGuiContext *self,
                  const gchar   *label,
                  gboolean      *value)
{
	LrgGuiId     id;
	LrgGuiRect   rect, box;
	const gchar *end;
	gboolean     hovered, held, clicked, visible;
	gfloat       sz, tw;

	g_return_val_if_fail (LRG_IS_GUI_CONTEXT (self), FALSE);
	g_return_val_if_fail (label != NULL, FALSE);
	g_return_val_if_fail (value != NULL, FALSE);

	id = gui_hash_label (self, label);
	end = gui_label_end (label);
	sz = gui_frame_height (self);
	tw = gui_text_width (self, label, end - label, FALSE);
	self->has_next_item_width = FALSE;

	visible = gui_item_add (self, id, sz + (tw > 0.0f ? self->style.item_spacing_x + tw : 0.0f),
	                        sz, &rect);
	clicked = gui_button_behavior (self, id, &rect, GUI_BUTTON_ON_RELEASE, &hovered, &held);
	if (clicked)
		*value = !*value;

	if (visible)
	{
		box.x = rect.x;
		box.y = rect.y;
		box.w = sz;
		box.h = sz;
		gui_draw_frame (self, &box,
		                gui_col (self, held ? LRG_GUI_COL_FRAME_BG_ACTIVE
		                                    : (hovered ? LRG_GUI_COL_FRAME_BG_HOVERED
		                                               : LRG_GUI_COL_FRAME_BG)), FALSE);
		if (*value)
		{
			LrgGuiRect inner = { box.x + 3.0f, box.y + 3.0f, box.w - 6.0f, box.h - 6.0f };

			gui_draw_check (self, &inner, gui_col (self, LRG_GUI_COL_CHECK_MARK));
		}
		if (tw > 0.0f)
			gui_draw_text (self, rect.x + sz + self->style.item_spacing_x,
			               rect.y + (sz - self->style.font_size) * 0.5f, label, end - label,
			               gui_col (self, LRG_GUI_COL_TEXT), FALSE);
	}

	return clicked;
}

gboolean
lrg_gui_radio_button (LrgGuiContext *self,
                      const gchar   *label,
                      gboolean       active)
{
	LrgGuiId     id;
	LrgGuiRect   rect;
	const gchar *end;
	gboolean     hovered, held, clicked, visible;
	gfloat       sz, tw;

	g_return_val_if_fail (LRG_IS_GUI_CONTEXT (self), FALSE);
	g_return_val_if_fail (label != NULL, FALSE);

	id = gui_hash_label (self, label);
	end = gui_label_end (label);
	sz = gui_frame_height (self);
	tw = gui_text_width (self, label, end - label, FALSE);

	visible = gui_item_add (self, id, sz + self->style.item_spacing_x + tw, sz, &rect);
	clicked = gui_button_behavior (self, id, &rect, GUI_BUTTON_ON_RELEASE, &hovered, &held);

	if (visible)
	{
		gfloat cx = rect.x + sz * 0.5f;
		gfloat cy = rect.y + sz * 0.5f;

		lrg_gui_draw_list_circle (gui_dl (self), cx, cy, sz * 0.45f,
		                          gui_col (self, hovered ? LRG_GUI_COL_FRAME_BG_HOVERED
		                                                 : LRG_GUI_COL_FRAME_BG), TRUE);
		if (active)
			lrg_gui_draw_list_circle (gui_dl (self), cx, cy, sz * 0.22f,
			                          gui_col (self, LRG_GUI_COL_CHECK_MARK), TRUE);
		gui_draw_text (self, rect.x + sz + self->style.item_spacing_x,
		               rect.y + (sz - self->style.font_size) * 0.5f, label, end - label,
		               gui_col (self, LRG_GUI_COL_TEXT), FALSE);
	}

	return clicked;
}

gboolean
lrg_gui_selectable (LrgGuiContext         *self,
                    const gchar           *label,
                    gboolean               selected,
                    LrgGuiSelectableFlags  flags)
{
	LrgGuiId     id;
	LrgGuiRect   rect;
	const gchar *end;
	gboolean     hovered = FALSE, held = FALSE, clicked = FALSE, visible;
	gfloat       w, h;
	gboolean     mono = (flags & LRG_GUI_SELECTABLE_MONOSPACE) != 0;

	g_return_val_if_fail (LRG_IS_GUI_CONTEXT (self), FALSE);
	g_return_val_if_fail (label != NULL, FALSE);

	id = gui_hash_label (self, label);
	end = gui_label_end (label);
	h = self->style.font_size + self->style.frame_padding_y * 2.0f;
	w = gui_item_width (self, 0.0f);

	visible = gui_item_add (self, id, w, h, &rect);
	if (!(flags & LRG_GUI_SELECTABLE_DISABLED))
		clicked = gui_button_behavior (self, id, &rect,
		                               gui_in_popup (self) ? GUI_BUTTON_ON_RELEASE : GUI_BUTTON_ON_PRESS,
		                               &hovered, &held);
	else
		gui_finish_item (self, id, &rect);

	if (visible)
	{
		if (selected || hovered || held)
			lrg_gui_draw_list_rect_filled (gui_dl (self), &rect,
			                               gui_col (self, held ? LRG_GUI_COL_HEADER_ACTIVE
			                                                   : (selected ? LRG_GUI_COL_HEADER
			                                                               : LRG_GUI_COL_HEADER_HOVERED)),
			                               self->style.rounding);
		{
			LrgGuiRect inner = rect;

			inner.x += self->style.frame_padding_x;
			inner.w -= self->style.frame_padding_x * 2.0f;
			gui_draw_text_clipped (self, &inner, label, end - label,
			                       gui_col (self, (flags & LRG_GUI_SELECTABLE_DISABLED)
			                                      ? LRG_GUI_COL_TEXT_DISABLED : LRG_GUI_COL_TEXT),
			                       0.0f, mono);
		}
	}

	/* A row clicked inside a popup closes the popup chain. */
	if (clicked && gui_in_popup (self) && !(flags & LRG_GUI_SELECTABLE_DONT_CLOSE_POPUP))
		lrg_gui_close_all_popups (self);

	return clicked;
}

gboolean
lrg_gui_splitter (LrgGuiContext    *self,
                  const gchar      *str_id,
                  const LrgGuiRect *rect,
                  gboolean          vertical,
                  gfloat           *delta)
{
	LrgGuiId id;
	gboolean hovered, held;

	g_return_val_if_fail (LRG_IS_GUI_CONTEXT (self), FALSE);
	g_return_val_if_fail (str_id != NULL && rect != NULL && delta != NULL, FALSE);

	*delta = 0.0f;
	id = gui_hash_label (self, str_id);
	gui_button_behavior (self, id, rect, GUI_BUTTON_ON_PRESS, &hovered, &held);

	if (hovered || held)
		self->cursor = vertical ? LRG_GUI_CURSOR_RESIZE_EW : LRG_GUI_CURSOR_RESIZE_NS;
	/* Only motion while already held counts (not the jump to the press). */
	if (held && self->active_id_prev == id)
		*delta = vertical ? self->input.mouse_x - self->prev_mouse_x
		                  : self->input.mouse_y - self->prev_mouse_y;

	lrg_gui_draw_list_rect_filled (gui_dl (self), rect,
	                               gui_col (self, (hovered || held) ? LRG_GUI_COL_ACCENT
	                                                                : LRG_GUI_COL_WINDOW_BG), 0.0f);
	return held;
}

/* ==========================================================================
 * Item queries
 * ========================================================================== */

gboolean
lrg_gui_is_item_hovered (LrgGuiContext *self)
{
	g_return_val_if_fail (LRG_IS_GUI_CONTEXT (self), FALSE);

	return self->item.hovered;
}

gboolean
lrg_gui_is_item_active (LrgGuiContext *self)
{
	g_return_val_if_fail (LRG_IS_GUI_CONTEXT (self), FALSE);

	return self->item.active;
}

gboolean
lrg_gui_is_item_activated (LrgGuiContext *self)
{
	g_return_val_if_fail (LRG_IS_GUI_CONTEXT (self), FALSE);

	return self->item.activated;
}

gboolean
lrg_gui_is_item_deactivated (LrgGuiContext *self)
{
	g_return_val_if_fail (LRG_IS_GUI_CONTEXT (self), FALSE);

	return self->item.deactivated;
}

gboolean
lrg_gui_is_item_clicked (LrgGuiContext     *self,
                         LrgGuiMouseButton  button)
{
	g_return_val_if_fail (LRG_IS_GUI_CONTEXT (self), FALSE);
	g_return_val_if_fail (button < LRG_GUI_MOUSE_BUTTON_COUNT, FALSE);

	return self->item.clicked[button];
}

gboolean
lrg_gui_is_item_double_clicked (LrgGuiContext *self)
{
	g_return_val_if_fail (LRG_IS_GUI_CONTEXT (self), FALSE);

	return self->item.double_clicked;
}

void
lrg_gui_get_item_rect (LrgGuiContext *self,
                       LrgGuiRect    *rect)
{
	g_return_if_fail (LRG_IS_GUI_CONTEXT (self));
	g_return_if_fail (rect != NULL);

	*rect = self->item.rect;
}

LrgGuiId
lrg_gui_get_item_id (LrgGuiContext *self)
{
	g_return_val_if_fail (LRG_IS_GUI_CONTEXT (self), 0);

	return self->item.id;
}

void
lrg_gui_set_item_tooltip (LrgGuiContext *self,
                          const gchar   *text)
{
	g_return_if_fail (LRG_IS_GUI_CONTEXT (self));
	g_return_if_fail (text != NULL);

	if (self->item.hovered && self->active_id == 0 && !self->dnd_active &&
	    self->hover_item_for_tooltip == self->item.id &&
	    self->input.time - self->hover_start >= TOOLTIP_DELAY)
		lrg_gui_set_tooltip (self, text);
}

void
lrg_gui_set_tooltip (LrgGuiContext *self,
                     const gchar   *text)
{
	g_return_if_fail (LRG_IS_GUI_CONTEXT (self));
	g_return_if_fail (text != NULL);

	g_free (self->tooltip);
	self->tooltip = g_strdup (text);
}

/* ==========================================================================
 * Popups
 * ========================================================================== */

gboolean
gui_in_popup (LrgGuiContext *self)
{
	return gui_region (self)->depth > 0;
}

gboolean
gui_is_popup_open_id (LrgGuiContext *self,
                      LrgGuiId       id)
{
	guint i;

	for (i = 0; i < self->popups->len; i++)
		if (g_array_index (self->popups, GuiPopup, i).id == id)
			return TRUE;
	return FALSE;
}

void
gui_close_popups_from (LrgGuiContext *self,
                       gint           level)
{
	if (level < 0)
		level = 0;
	if (level < (gint) self->popups->len)
		g_array_set_size (self->popups, level);
}

void
gui_open_popup_id (LrgGuiContext *self,
                   LrgGuiId       id,
                   gfloat         x,
                   gfloat         y,
                   gboolean       modal,
                   gboolean       menu)
{
	GuiPopup p;
	gint     level = self->popup_level;

	/* Reopening an already-open popup at this level keeps it. */
	if (level < (gint) self->popups->len &&
	    g_array_index (self->popups, GuiPopup, level).id == id)
		return;

	gui_close_popups_from (self, level);

	memset (&p, 0, sizeof p);
	p.id = id;
	p.x = self->has_next_popup_pos ? self->next_popup_x : x;
	p.y = self->has_next_popup_pos ? self->next_popup_y : y;
	p.open_frame = self->frame;
	p.modal = modal;
	p.menu = menu;
	p.source_id = self->item.id;
	p.visited = FALSE;
	g_array_append_val (self->popups, p);
	self->has_next_popup_pos = FALSE;
}

void
lrg_gui_open_popup (LrgGuiContext *self,
                    const gchar   *str_id)
{
	g_return_if_fail (LRG_IS_GUI_CONTEXT (self));
	g_return_if_fail (str_id != NULL);

	gui_open_popup_id (self, gui_hash_label (self, str_id), self->input.mouse_x,
	                   self->input.mouse_y, FALSE, FALSE);
}

gboolean
lrg_gui_is_popup_open (LrgGuiContext *self,
                       const gchar   *str_id)
{
	g_return_val_if_fail (LRG_IS_GUI_CONTEXT (self), FALSE);
	g_return_val_if_fail (str_id != NULL, FALSE);

	return gui_is_popup_open_id (self, gui_hash_label (self, str_id));
}

/*
 * gui_begin_popup_ex:
 *
 * Builds the popup at the current nesting level if it is the one open
 * there: positions it (clamped to the window using last frame's size),
 * reserves its background and opens its region on the popup layer.
 */
gboolean
gui_begin_popup_ex (LrgGuiContext *self,
                    LrgGuiId       id,
                    gboolean       menu)
{
	GuiPopup  *p;
	LrgGuiRect rect;
	gint       level = self->popup_level;
	gfloat     w, h;
	guint      bg;

	if (level >= (gint) self->popups->len ||
	    g_array_index (self->popups, GuiPopup, level).id != id)
	{
		self->has_next_popup_size = FALSE;
		return FALSE;
	}

	p = &g_array_index (self->popups, GuiPopup, level);
	p->visited = TRUE;
	p->menu = p->menu || menu;
	if (self->has_next_popup_size)
	{
		p->req_w = self->next_popup_w;
		p->req_h = self->next_popup_h;
		self->has_next_popup_size = FALSE;
	}

	w = (p->req_w > 0.0f) ? p->req_w : MAX (p->w, DEFAULT_POPUP_WIDTH);
	h = (p->req_h > 0.0f) ? p->req_h : (p->h > 0.0f ? p->h : self->height);

	if (p->modal)
	{
		/* Centred, with a dimmed backdrop. */
		LrgGuiRect dim = { 0.0f, 0.0f, self->width, self->height };

		lrg_gui_draw_list_rect_filled (self->layers[GUI_LAYER_POPUP], &dim,
		                               gui_col (self, LRG_GUI_COL_MODAL_DIM), 0.0f);
		rect.x = roundf ((self->width - w) * 0.5f);
		rect.y = roundf ((self->height - h) * 0.4f);
	}
	else
	{
		rect.x = p->x;
		rect.y = p->y;
		if (rect.x + w > self->width)
			rect.x = MAX (0.0f, self->width - w);
		if (p->h > 0.0f && rect.y + h > self->height)
			rect.y = MAX (0.0f, self->height - h);
	}
	rect.w = w;
	rect.h = h;
	if (rect.h > self->height)
		rect.h = self->height;

	/* Background now, patched to the measured size at end_popup. */
	bg = lrg_gui_draw_list_rect_filled (self->layers[GUI_LAYER_POPUP], &rect,
	                                    gui_col (self, LRG_GUI_COL_POPUP_BG), self->style.rounding);

	self->popup_level++;
	gui_begin_region_full (self, id, &rect,
	                       (p->req_h > 0.0f && !menu) ? LRG_GUI_REGION_SCROLL_Y : LRG_GUI_REGION_NONE,
	                       TRUE, level, GUI_LAYER_POPUP);
	gui_region (self)->bg_cmd = bg;
	gui_region (self)->has_bg_cmd = TRUE;
	return TRUE;
}

gboolean
lrg_gui_begin_popup (LrgGuiContext *self,
                     const gchar   *str_id)
{
	g_return_val_if_fail (LRG_IS_GUI_CONTEXT (self), FALSE);
	g_return_val_if_fail (str_id != NULL, FALSE);

	return gui_begin_popup_ex (self, gui_hash_label (self, str_id), FALSE);
}

gboolean
lrg_gui_begin_popup_context_item (LrgGuiContext *self,
                                  const gchar   *str_id)
{
	LrgGuiId id;

	g_return_val_if_fail (LRG_IS_GUI_CONTEXT (self), FALSE);

	id = (str_id != NULL) ? gui_hash_label (self, str_id) : self->item.id + 0x5bd1e995u;
	if (self->item.hovered && self->mouse_released[LRG_GUI_MOUSE_RIGHT])
		gui_open_popup_id (self, id, self->input.mouse_x, self->input.mouse_y, FALSE, FALSE);

	return gui_begin_popup_ex (self, id, FALSE);
}

gboolean
lrg_gui_begin_popup_context_region (LrgGuiContext *self,
                                    const gchar   *str_id)
{
	GuiRegion *r;
	LrgGuiId   id;

	g_return_val_if_fail (LRG_IS_GUI_CONTEXT (self), FALSE);
	g_return_val_if_fail (str_id != NULL, FALSE);

	r = gui_region (self);
	id = gui_hash_label (self, str_id);

	/* Right-click on the region where no item claimed the pointer. */
	if (self->mouse_released[LRG_GUI_MOUSE_RIGHT] && self->hovered_id == 0 &&
	    gui_rect_contains (&r->clip, self->input.mouse_x, self->input.mouse_y) &&
	    r->depth == self->mouse_top_depth && r->depth >= self->modal_depth)
		gui_open_popup_id (self, id, self->input.mouse_x, self->input.mouse_y, FALSE, FALSE);

	return gui_begin_popup_ex (self, id, FALSE);
}

gboolean
lrg_gui_begin_popup_modal (LrgGuiContext *self,
                           const gchar   *title,
                           gboolean      *open)
{
	LrgGuiId     id;
	GuiPopup    *p;
	const gchar *end;
	LrgGuiRect   bar;
	gfloat       w;

	g_return_val_if_fail (LRG_IS_GUI_CONTEXT (self), FALSE);
	g_return_val_if_fail (title != NULL, FALSE);

	id = gui_hash_label (self, title);
	if (self->popup_level < (gint) self->popups->len &&
	    (p = &g_array_index (self->popups, GuiPopup, self->popup_level))->id == id)
		p->modal = TRUE;

	if (!gui_begin_popup_ex (self, id, FALSE))
		return FALSE;

	/* Title bar with an optional close button. */
	end = gui_label_end (title);
	lrg_gui_get_content_avail (self, &w, NULL);
	if (gui_item_add (self, 0, w, gui_frame_height (self), &bar))
		gui_draw_text_clipped (self, &bar, title, end - title, gui_col (self, LRG_GUI_COL_TEXT),
		                       0.0f, FALSE);
	if (open != NULL)
	{
		LrgGuiRect close = { bar.x + bar.w - bar.h, bar.y, bar.h, bar.h };
		gboolean   hovered;
		LrgGuiId   cid = id + 31;

		if (gui_button_behavior (self, cid, &close, GUI_BUTTON_ON_RELEASE, &hovered, NULL) ||
		    (self->focus_id == 0 && gui_key_pressed (self, LRG_GUI_KEY_ESCAPE, FALSE, NULL)))
		{
			*open = FALSE;
			lrg_gui_close_current_popup (self);
		}
		gui_draw_text_clipped (self, &close, "x", 1,
		                       gui_col (self, hovered ? LRG_GUI_COL_ERROR : LRG_GUI_COL_TEXT_DISABLED),
		                       0.5f, FALSE);
	}
	lrg_gui_separator (self);
	return TRUE;
}

void
lrg_gui_end_popup (LrgGuiContext *self)
{
	GuiRegion *r;
	gint       index;
	gfloat     pad;
	gfloat     w, h;

	g_return_if_fail (LRG_IS_GUI_CONTEXT (self));

	r = gui_region (self);
	g_return_if_fail (r != NULL && r->is_popup);

	index = r->popup_index;
	pad = self->style.region_padding;
	w = (r->max_x - r->rect.x) + pad;
	h = (r->max_y - (r->rect.y - r->scroll_y)) + pad;

	if (index < (gint) self->popups->len)
	{
		GuiPopup  *p = &g_array_index (self->popups, GuiPopup, index);
		LrgGuiRect final = r->rect;

		p->w = MAX (w, 40.0f);
		p->h = MAX (h, 10.0f);
		if (p->req_w <= 0.0f)
			final.w = p->w;
		if (p->req_h <= 0.0f)
			final.h = MIN (p->h, self->height);
		p->rect = final;

		if (r->has_bg_cmd)
			lrg_gui_draw_list_set_rect (self->layers[GUI_LAYER_POPUP], r->bg_cmd, &final);
		lrg_gui_draw_list_rect_outline (self->layers[GUI_LAYER_POPUP], &final,
		                                gui_col (self, LRG_GUI_COL_BORDER), 1.0f,
		                                self->style.rounding);
	}

	lrg_gui_end_region (self);
	self->popup_level--;
}

void
lrg_gui_close_current_popup (LrgGuiContext *self)
{
	g_return_if_fail (LRG_IS_GUI_CONTEXT (self));

	gui_close_popups_from (self, self->popup_level - 1);
}

void
lrg_gui_close_all_popups (LrgGuiContext *self)
{
	g_return_if_fail (LRG_IS_GUI_CONTEXT (self));

	/* Keep modals that contain the popup chain being closed. */
	gui_close_popups_from (self, top_modal_depth (self));
}

void
lrg_gui_set_next_popup_size (LrgGuiContext *self,
                             gfloat         width,
                             gfloat         height)
{
	g_return_if_fail (LRG_IS_GUI_CONTEXT (self));

	self->next_popup_w = width;
	self->next_popup_h = height;
	self->has_next_popup_size = TRUE;
}

void
lrg_gui_set_next_popup_pos (LrgGuiContext *self,
                            gfloat         x,
                            gfloat         y)
{
	g_return_if_fail (LRG_IS_GUI_CONTEXT (self));

	self->next_popup_x = x;
	self->next_popup_y = y;
	self->has_next_popup_pos = TRUE;
}

/* ==========================================================================
 * Menus
 * ========================================================================== */

void
lrg_gui_begin_menu_bar (LrgGuiContext    *self,
                        const LrgGuiRect *rect)
{
	g_return_if_fail (LRG_IS_GUI_CONTEXT (self));
	g_return_if_fail (rect != NULL);

	lrg_gui_draw_list_rect_filled (gui_dl (self), rect, gui_col (self, LRG_GUI_COL_MENUBAR_BG), 0.0f);
	lrg_gui_begin_region (self, "##menubar", rect,
	                      LRG_GUI_REGION_HORIZONTAL | LRG_GUI_REGION_NO_PADDING);
	self->menubar_id = gui_region (self)->id;
	self->in_menubar = TRUE;
	gui_region (self)->cursor_x += self->style.item_spacing_x;
	gui_region (self)->cursor_y = rect->y + (rect->h - gui_frame_height (self)) * 0.5f;
	gui_region (self)->line_y = gui_region (self)->cursor_y;
}

void
lrg_gui_end_menu_bar (LrgGuiContext *self)
{
	g_return_if_fail (LRG_IS_GUI_CONTEXT (self));

	self->in_menubar = FALSE;
	lrg_gui_end_region (self);
}

/* TRUE if a popup at @level is a menu (some menu of this chain is open). */
static gboolean
menu_open_at (LrgGuiContext *self,
              gint           level)
{
	return level < (gint) self->popups->len && g_array_index (self->popups, GuiPopup, level).menu;
}

gboolean
lrg_gui_begin_menu (LrgGuiContext *self,
                    const gchar   *label,
                    gboolean       enabled)
{
	LrgGuiId     id;
	LrgGuiRect   rect;
	const gchar *end;
	gboolean     hovered, held, pressed, visible;
	gboolean     in_bar = self->in_menubar && !gui_in_popup (self);
	gboolean     open;
	gint         level = self->popup_level;
	gfloat       tw;

	g_return_val_if_fail (LRG_IS_GUI_CONTEXT (self), FALSE);
	g_return_val_if_fail (label != NULL, FALSE);

	id = gui_hash_label (self, label);
	end = gui_label_end (label);
	tw = gui_text_width (self, label, end - label, FALSE);
	open = gui_is_popup_open_id (self, id);

	if (in_bar)
	{
		visible = gui_item_add (self, id, tw + self->style.frame_padding_x * 2.0f,
		                        gui_frame_height (self), &rect);
		pressed = enabled && gui_button_behavior (self, id, &rect, GUI_BUTTON_ON_PRESS, &hovered, &held);
		if (!enabled)
			hovered = FALSE;

		if (pressed)
		{
			/* Clicking an open menu's header closes it. */
			if (g_hash_table_contains (self->popups_at_start, GUINT_TO_POINTER (id)) || open)
			{
				gui_close_popups_from (self, level);
				open = FALSE;
			}
			else
			{
				gui_open_popup_id (self, id, rect.x, rect.y + rect.h, FALSE, TRUE);
				open = TRUE;
			}
		}
		else if (hovered && enabled && menu_open_at (self, level) && !open)
		{
			/* Sliding across the bar switches menus. */
			gui_open_popup_id (self, id, rect.x, rect.y + rect.h, FALSE, TRUE);
			open = TRUE;
		}

		if (visible)
		{
			if (open || hovered)
				lrg_gui_draw_list_rect_filled (gui_dl (self), &rect,
				                               gui_col (self, open ? LRG_GUI_COL_HEADER_ACTIVE
				                                                   : LRG_GUI_COL_HEADER_HOVERED),
				                               self->style.rounding);
			gui_draw_text_clipped (self, &rect, label, end - label,
			                       gui_col (self, enabled ? LRG_GUI_COL_TEXT : LRG_GUI_COL_TEXT_DISABLED),
			                       0.5f, FALSE);
		}
	}
	else
	{
		/* Submenu row inside a menu: opens to the right on hover. */
		gfloat w = gui_item_width (self, 0.0f);

		w = MAX (w, tw + self->style.frame_padding_x * 4.0f + self->style.font_size);
		visible = gui_item_add (self, id, w, gui_frame_height (self), &rect);
		gui_button_behavior (self, id, &rect, GUI_BUTTON_NO_ACTIVATE, &hovered, &held);
		if (!enabled)
			hovered = FALSE;

		if (hovered && !open)
		{
			gui_open_popup_id (self, id, rect.x + rect.w, rect.y, FALSE, TRUE);
			open = TRUE;
		}
		else if (hovered && level + 1 < (gint) self->popups->len)
		{
			/* Hovering this row again closes deeper submenus of siblings. */
			gui_close_popups_from (self, level + 1);
		}

		if (visible)
		{
			LrgGuiRect inner = rect;

			if (open || hovered)
				lrg_gui_draw_list_rect_filled (gui_dl (self), &rect,
				                               gui_col (self, LRG_GUI_COL_HEADER_HOVERED),
				                               self->style.rounding);
			inner.x += self->style.frame_padding_x;
			gui_draw_text_clipped (self, &inner, label, end - label,
			                       gui_col (self, enabled ? LRG_GUI_COL_TEXT : LRG_GUI_COL_TEXT_DISABLED),
			                       0.0f, FALSE);
			gui_draw_arrow (self, rect.x + rect.w - self->style.font_size * 0.6f,
			                rect.y + rect.h * 0.5f, self->style.font_size * 0.5f, FALSE,
			                gui_col (self, LRG_GUI_COL_TEXT_DISABLED));
		}
	}

	if (!open)
		return FALSE;

	lrg_gui_set_next_popup_size (self, 0.0f, 0.0f);
	return gui_begin_popup_ex (self, id, TRUE);
}

void
lrg_gui_end_menu (LrgGuiContext *self)
{
	lrg_gui_end_popup (self);
}

gboolean
lrg_gui_menu_item (LrgGuiContext *self,
                   const gchar   *label,
                   const gchar   *shortcut,
                   gboolean       checked,
                   gboolean       enabled)
{
	LrgGuiId     id;
	LrgGuiRect   rect;
	const gchar *end;
	gboolean     hovered = FALSE, held = FALSE, clicked = FALSE, visible;
	gfloat       tw, sw, check_w, w;
	gint         level = self->popup_level;

	g_return_val_if_fail (LRG_IS_GUI_CONTEXT (self), FALSE);
	g_return_val_if_fail (label != NULL, FALSE);

	id = gui_hash_label (self, label);
	end = gui_label_end (label);
	tw = gui_text_width (self, label, end - label, FALSE);
	sw = (shortcut != NULL) ? gui_text_width (self, shortcut, -1, FALSE) : 0.0f;
	check_w = self->style.font_size + self->style.frame_padding_x;

	w = gui_item_width (self, 0.0f);
	w = MAX (w, check_w + tw + (sw > 0.0f ? sw + self->style.font_size * 2.0f : 0.0f) +
	            self->style.frame_padding_x * 2.0f);
	visible = gui_item_add (self, id, w, gui_frame_height (self), &rect);

	if (enabled)
		clicked = gui_button_behavior (self, id, &rect, GUI_BUTTON_ON_RELEASE, &hovered, &held);
	else
		gui_finish_item (self, id, &rect);

	/* Hovering an item closes sibling submenus. */
	if (hovered && level < (gint) self->popups->len)
		gui_close_popups_from (self, level);

	if (visible)
	{
		LrgGuiColor text = gui_col (self, enabled ? LRG_GUI_COL_TEXT : LRG_GUI_COL_TEXT_DISABLED);

		if (hovered)
			lrg_gui_draw_list_rect_filled (gui_dl (self), &rect,
			                               gui_col (self, held ? LRG_GUI_COL_HEADER_ACTIVE
			                                                   : LRG_GUI_COL_HEADER_HOVERED),
			                               self->style.rounding);
		if (checked)
		{
			LrgGuiRect box = { rect.x + self->style.frame_padding_x * 0.5f,
			                   rect.y + (rect.h - self->style.font_size) * 0.5f,
			                   self->style.font_size, self->style.font_size };

			gui_draw_check (self, &box, gui_col (self, LRG_GUI_COL_CHECK_MARK));
		}
		gui_draw_text (self, rect.x + check_w + self->style.frame_padding_x * 0.5f,
		               rect.y + (rect.h - self->style.font_size) * 0.5f, label, end - label, text, FALSE);
		if (shortcut != NULL)
			gui_draw_text (self, rect.x + rect.w - sw - self->style.frame_padding_x,
			               rect.y + (rect.h - self->style.font_size) * 0.5f, shortcut, -1,
			               gui_col (self, LRG_GUI_COL_TEXT_DISABLED), FALSE);
	}

	if (clicked)
		lrg_gui_close_all_popups (self);

	return clicked;
}

/* ==========================================================================
 * Tabs
 * ========================================================================== */

gboolean
lrg_gui_begin_tab_bar (LrgGuiContext *self,
                       const gchar   *str_id)
{
	GuiTabBar  bar;
	LrgGuiRect rect;
	gfloat     w;

	g_return_val_if_fail (LRG_IS_GUI_CONTEXT (self), FALSE);
	g_return_val_if_fail (str_id != NULL, FALSE);

	memset (&bar, 0, sizeof bar);
	bar.id = gui_hash_label (self, str_id);
	lrg_gui_get_content_avail (self, &w, NULL);
	gui_item_add (self, bar.id, w, gui_frame_height (self), &rect);
	bar.x = rect.x;
	bar.x0 = rect.x;
	bar.y = rect.y;
	bar.h = rect.h;

	lrg_gui_draw_list_line (gui_dl (self), rect.x, rect.y + rect.h - 0.5f, rect.x + rect.w,
	                        rect.y + rect.h - 0.5f, gui_col (self, LRG_GUI_COL_SEPARATOR), 1.0f);
	g_array_append_val (self->tab_bars, bar);
	return TRUE;
}

gboolean
lrg_gui_tab_item (LrgGuiContext *self,
                  const gchar   *label,
                  gboolean      *open)
{
	GuiTabBar   *bar;
	GuiState    *st;
	LrgGuiId     id;
	LrgGuiRect   rect;
	const gchar *end;
	gboolean     hovered, held, selected;
	gfloat       tw, w;

	g_return_val_if_fail (LRG_IS_GUI_CONTEXT (self), FALSE);
	g_return_val_if_fail (label != NULL, FALSE);
	g_return_val_if_fail (self->tab_bars->len > 0, FALSE);

	bar = &g_array_index (self->tab_bars, GuiTabBar, self->tab_bars->len - 1);
	st = gui_state (self, bar->id);

	if (open != NULL && !*open)
		return FALSE;

	id = gui_hash_label (self, label);
	end = gui_label_end (label);
	tw = gui_text_width (self, label, end - label, FALSE);
	w = tw + self->style.frame_padding_x * 2.0f + (open != NULL ? self->style.font_size : 0.0f);

	if (!bar->any_seen)
		bar->first_tab = id;
	bar->any_seen = TRUE;

	rect.x = bar->x;
	rect.y = bar->y;
	rect.w = w;
	rect.h = bar->h;
	bar->x += w + 2.0f;

	if (gui_button_behavior (self, id, &rect, GUI_BUTTON_ON_PRESS, &hovered, &held))
		st->selected = id;

	/* Default to the first tab; fall back to it if the selection vanished. */
	if (st->selected == 0)
		st->selected = id;
	selected = (st->selected == id);
	if (selected)
		bar->seen_selected = id;

	lrg_gui_draw_list_rect_filled (gui_dl (self), &rect,
	                               gui_col (self, selected ? LRG_GUI_COL_TAB_ACTIVE
	                                                       : (hovered ? LRG_GUI_COL_TAB_HOVERED
	                                                                  : LRG_GUI_COL_TAB)),
	                               self->style.rounding);
	if (selected)
		lrg_gui_draw_list_line (gui_dl (self), rect.x, rect.y + 1.0f, rect.x + rect.w, rect.y + 1.0f,
		                        gui_col (self, LRG_GUI_COL_ACCENT), 2.0f);
	{
		LrgGuiRect text = rect;

		text.x += self->style.frame_padding_x;
		text.w = tw;
		gui_draw_text_clipped (self, &text, label, end - label,
		                       gui_col (self, selected ? LRG_GUI_COL_TEXT : LRG_GUI_COL_TEXT_DISABLED),
		                       0.0f, FALSE);
	}

	if (open != NULL)
	{
		LrgGuiRect close = { rect.x + rect.w - self->style.font_size - 2.0f, rect.y,
		                     self->style.font_size, rect.h };
		gboolean   ch;

		if (gui_button_behavior (self, id + 3, &close, GUI_BUTTON_ON_RELEASE, &ch, NULL))
			*open = FALSE;
		gui_draw_text_clipped (self, &close, "x", 1,
		                       gui_col (self, ch ? LRG_GUI_COL_ERROR : LRG_GUI_COL_TEXT_DISABLED),
		                       0.5f, FALSE);
	}

	self->item.id = id;
	self->item.rect = rect;
	return selected && (open == NULL || *open);
}

void
lrg_gui_end_tab_bar (LrgGuiContext *self)
{
	GuiTabBar *bar;
	GuiState  *st;

	g_return_if_fail (LRG_IS_GUI_CONTEXT (self));
	g_return_if_fail (self->tab_bars->len > 0);

	bar = &g_array_index (self->tab_bars, GuiTabBar, self->tab_bars->len - 1);
	st = gui_state (self, bar->id);

	/* The selected tab was closed or removed: select the first next frame. */
	if (bar->any_seen && bar->seen_selected == 0)
		st->selected = bar->first_tab;

	g_array_set_size (self->tab_bars, self->tab_bars->len - 1);
}

/* ==========================================================================
 * Drag and drop
 * ========================================================================== */

gboolean
lrg_gui_begin_drag_source (LrgGuiContext *self)
{
	gfloat dx, dy;

	g_return_val_if_fail (LRG_IS_GUI_CONTEXT (self), FALSE);

	if (self->dnd_active)
	{
		if (self->dnd_source == self->item.id)
		{
			self->dnd_source_submitted = TRUE;
			return TRUE;
		}
		return FALSE;
	}

	/* Start once the pointer moved far enough while holding the item. */
	if (self->active_id == 0 || self->active_id != self->item.id ||
	    !self->input.mouse_down[LRG_GUI_MOUSE_LEFT])
		return FALSE;

	dx = self->input.mouse_x - self->click_x[LRG_GUI_MOUSE_LEFT];
	dy = self->input.mouse_y - self->click_y[LRG_GUI_MOUSE_LEFT];
	if (dx * dx + dy * dy < DRAG_THRESHOLD * DRAG_THRESHOLD)
		return FALSE;

	self->dnd_active = TRUE;
	self->dnd_source = self->item.id;
	self->dnd_source_submitted = TRUE;
	self->dnd_accepted = FALSE;
	return TRUE;
}

void
lrg_gui_set_drag_payload (LrgGuiContext *self,
                          const gchar   *type,
                          const gchar   *data,
                          const gchar   *preview)
{
	g_return_if_fail (LRG_IS_GUI_CONTEXT (self));
	g_return_if_fail (type != NULL);

	g_free (self->dnd_type);
	g_free (self->dnd_data);
	g_free (self->dnd_preview);
	self->dnd_type = g_strdup (type);
	self->dnd_data = g_strdup (data);
	self->dnd_preview = g_strdup (preview);
}

void
lrg_gui_end_drag_source (LrgGuiContext *self)
{
	g_return_if_fail (LRG_IS_GUI_CONTEXT (self));

	/* The source item stays "active" so it keeps the pointer captured. */
	self->cursor = LRG_GUI_CURSOR_RESIZE_ALL;
}

const gchar *
lrg_gui_accept_drop (LrgGuiContext *self,
                     const gchar   *type)
{
	GuiRegion *r;
	gboolean   over;

	g_return_val_if_fail (LRG_IS_GUI_CONTEXT (self), NULL);
	g_return_val_if_fail (type != NULL, NULL);

	if (!self->dnd_active || g_strcmp0 (self->dnd_type, type) != 0 ||
	    self->item.id == self->dnd_source)
		return NULL;

	r = gui_region (self);
	over = gui_rect_contains (&self->item.rect, self->input.mouse_x, self->input.mouse_y) &&
	       gui_rect_contains (&r->clip, self->input.mouse_x, self->input.mouse_y) &&
	       r->depth == self->mouse_top_depth;
	if (!over)
		return NULL;

	lrg_gui_draw_list_rect_outline (gui_dl (self), &self->item.rect,
	                                gui_col (self, LRG_GUI_COL_DROP_TARGET), 2.0f, self->style.rounding);

	if (self->mouse_released[LRG_GUI_MOUSE_LEFT])
	{
		self->dnd_dropped = TRUE;
		self->dnd_accepted = TRUE;
		return self->dnd_data != NULL ? self->dnd_data : "";
	}

	return NULL;
}

const gchar *
lrg_gui_get_drag_payload (LrgGuiContext  *self,
                          const gchar   **type)
{
	g_return_val_if_fail (LRG_IS_GUI_CONTEXT (self), NULL);

	if (type != NULL)
		*type = self->dnd_active ? self->dnd_type : NULL;
	return self->dnd_active ? self->dnd_data : NULL;
}

gboolean
lrg_gui_was_payload_dropped (LrgGuiContext *self)
{
	g_return_val_if_fail (LRG_IS_GUI_CONTEXT (self), FALSE);

	return self->dnd_active && self->mouse_released[LRG_GUI_MOUSE_LEFT];
}

/* ==========================================================================
 * Raw input
 * ========================================================================== */

gboolean
gui_key_pressed (LrgGuiContext   *self,
                 LrgGuiKey        key,
                 gboolean         repeat,
                 LrgGuiModifiers *mods)
{
	guint i;

	for (i = 0; i < self->input.n_keys; i++)
	{
		const LrgGuiKeyEvent *ev = &self->input.keys[i];

		if (ev->key == key && (repeat || !ev->repeat))
		{
			if (mods != NULL)
				*mods = ev->mods;
			return TRUE;
		}
	}
	return FALSE;
}

gboolean
lrg_gui_is_key_pressed (LrgGuiContext *self,
                        LrgGuiKey      key,
                        gboolean       repeat)
{
	g_return_val_if_fail (LRG_IS_GUI_CONTEXT (self), FALSE);

	return gui_key_pressed (self, key, repeat, NULL);
}

gboolean
lrg_gui_is_key_down (LrgGuiContext *self,
                     LrgGuiKey      key)
{
	g_return_val_if_fail (LRG_IS_GUI_CONTEXT (self), FALSE);

	return key > LRG_GUI_KEY_NONE && key < LRG_GUI_KEY_COUNT && self->input.key_down[key];
}

gboolean
lrg_gui_shortcut (LrgGuiContext   *self,
                  LrgGuiModifiers  mods,
                  LrgGuiKey        key)
{
	guint i;

	g_return_val_if_fail (LRG_IS_GUI_CONTEXT (self), FALSE);

	/* Text editing owns plain keys and its own Ctrl chords. */
	if (self->focus_id != 0)
	{
		if (!(mods & LRG_GUI_MOD_CTRL))
			return FALSE;
		if (key == LRG_GUI_KEY_A || key == LRG_GUI_KEY_C || key == LRG_GUI_KEY_V ||
		    key == LRG_GUI_KEY_X || key == LRG_GUI_KEY_Z || key == LRG_GUI_KEY_Y ||
		    key == LRG_GUI_KEY_LEFT || key == LRG_GUI_KEY_RIGHT ||
		    key == LRG_GUI_KEY_BACKSPACE || key == LRG_GUI_KEY_DELETE)
			return FALSE;
	}

	for (i = 0; i < self->input.n_keys; i++)
	{
		const LrgGuiKeyEvent *ev = &self->input.keys[i];
		LrgGuiModifiers       m = ev->mods & (LRG_GUI_MOD_CTRL | LRG_GUI_MOD_SHIFT |
		                                      LRG_GUI_MOD_ALT | LRG_GUI_MOD_SUPER);

		if (ev->key == key && m == mods)
			return TRUE;
	}
	return FALSE;
}

LrgGuiModifiers
lrg_gui_get_mods (LrgGuiContext *self)
{
	g_return_val_if_fail (LRG_IS_GUI_CONTEXT (self), LRG_GUI_MOD_NONE);

	return self->input.mods;
}

gboolean
lrg_gui_is_mouse_clicked (LrgGuiContext     *self,
                          LrgGuiMouseButton  button)
{
	g_return_val_if_fail (LRG_IS_GUI_CONTEXT (self), FALSE);
	g_return_val_if_fail (button < LRG_GUI_MOUSE_BUTTON_COUNT, FALSE);

	return self->mouse_clicked[button];
}

gboolean
lrg_gui_is_mouse_released (LrgGuiContext     *self,
                           LrgGuiMouseButton  button)
{
	g_return_val_if_fail (LRG_IS_GUI_CONTEXT (self), FALSE);
	g_return_val_if_fail (button < LRG_GUI_MOUSE_BUTTON_COUNT, FALSE);

	return self->mouse_released[button];
}

gboolean
lrg_gui_is_mouse_down (LrgGuiContext     *self,
                       LrgGuiMouseButton  button)
{
	g_return_val_if_fail (LRG_IS_GUI_CONTEXT (self), FALSE);
	g_return_val_if_fail (button < LRG_GUI_MOUSE_BUTTON_COUNT, FALSE);

	return self->input.mouse_down[button];
}

gboolean
lrg_gui_is_mouse_double_clicked (LrgGuiContext     *self,
                                 LrgGuiMouseButton  button)
{
	g_return_val_if_fail (LRG_IS_GUI_CONTEXT (self), FALSE);
	g_return_val_if_fail (button < LRG_GUI_MOUSE_BUTTON_COUNT, FALSE);

	return self->mouse_double[button];
}

void
lrg_gui_get_mouse (LrgGuiContext *self,
                   gfloat        *x,
                   gfloat        *y)
{
	g_return_if_fail (LRG_IS_GUI_CONTEXT (self));

	if (x != NULL)
		*x = self->input.mouse_x;
	if (y != NULL)
		*y = self->input.mouse_y;
}

void
lrg_gui_get_mouse_delta (LrgGuiContext *self,
                         gfloat        *dx,
                         gfloat        *dy)
{
	g_return_if_fail (LRG_IS_GUI_CONTEXT (self));

	if (dx != NULL)
		*dx = self->input.mouse_x - self->prev_mouse_x;
	if (dy != NULL)
		*dy = self->input.mouse_y - self->prev_mouse_y;
}

gfloat
lrg_gui_get_mouse_wheel (LrgGuiContext *self)
{
	g_return_val_if_fail (LRG_IS_GUI_CONTEXT (self), 0.0f);

	return self->wheel_y;
}

gfloat
lrg_gui_get_delta_time (LrgGuiContext *self)
{
	g_return_val_if_fail (LRG_IS_GUI_CONTEXT (self), 0.0f);

	return self->input.delta;
}

void
lrg_gui_set_cursor_shape (LrgGuiContext *self,
                          LrgGuiCursor   cursor)
{
	g_return_if_fail (LRG_IS_GUI_CONTEXT (self));

	self->cursor = cursor;
}

gboolean
lrg_gui_is_rect_hovered (LrgGuiContext    *self,
                         const LrgGuiRect *rect)
{
	GuiRegion *r;

	g_return_val_if_fail (LRG_IS_GUI_CONTEXT (self), FALSE);
	g_return_val_if_fail (rect != NULL, FALSE);

	r = gui_region (self);
	return gui_rect_contains (rect, self->input.mouse_x, self->input.mouse_y) &&
	       gui_rect_contains (&r->clip, self->input.mouse_x, self->input.mouse_y) &&
	       r->depth == self->mouse_top_depth && r->depth >= self->modal_depth;
}
