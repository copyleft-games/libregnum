/* lrg-viewport.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgViewport: engine-rendered content inside the UI layout.
 *
 * In RENDER mode the viewport owns a GrlRenderTexture sized to its
 * content area (the allocation minus the BOX border) times a
 * resolution scale, or to a fixed pixel resolution. Every frame, from
 * the widget's step() (driven by lrg_canvas_update()), it enters
 * texture mode on that target and asks the application to draw: first
 * the C render function, then the "render" signal. That happens before
 * and outside the UI paint pass, so an application can freely use
 * BeginMode3D(), shaders and its own render textures; the paint pass
 * then only records an image instance sampling the finished texture.
 *
 * Frame order with a canvas:
 *
 *   lrg_canvas_handle_input / process_input
 *     -> lrg_canvas_update -> LrgViewport step -> render into texture
 *   BeginDrawing
 *     lrg_canvas_render -> layout -> paint: BOX look + texture image
 *   EndDrawing
 *
 * Layout happens in lrg_canvas_record(), after the step, so a resize is
 * seen one frame later; while the size keeps changing the old texture
 * is stretched over the new area and the texture is only recreated
 * once the size has settled for "resize-delay" seconds.
 *
 * In TEXTURE mode it shows an application texture (or the colour
 * buffer of an application render texture, flipped).
 *
 * Fit modes map the texture into the content area (stretch, contain
 * with letterbox bars in the BOX background, cover, integer scale);
 * the same geometry maps pointer positions back into texture pixels,
 * so pointer, scroll and touch events are re-emitted as signals in the
 * content's own coordinate space, and the focused viewport forwards
 * keys.
 */

#include "config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_UI

#include <math.h>
#include <string.h>
#include <raylib.h>
#include <rlgl.h>

#include "lrg-viewport.h"
#include "../lrg-log.h"

/* Preferred size when nothing better is known */
#define DEFAULT_SIZE (64.0f)

/* Allowed resolution scales */
#define MIN_SCALE (0.1f)
#define MAX_SCALE (8.0f)

struct _LrgViewport
{
	LrgWidget              parent_instance;

	/* Settings */
	LrgViewportMode        mode;
	LrgViewportFit         fit;
	GrlTextureFilter       filter;
	gfloat                 resolution_scale;
	gint                   fixed_width;
	gint                   fixed_height;
	gfloat                 resize_delay;
	gboolean               continuous;
	gboolean               auto_clear;
	GrlColor               clear_color;
	gboolean               flip_y;
	gboolean               lock_cursor;

	/* RENDER mode: the owned target */
	GrlRenderTexture      *target;
	GrlTexture            *target_texture;   /* colour buffer wrapper */
	gint                   target_width;
	gint                   target_height;
	gint                   pending_width;    /* size waiting to settle */
	gint                   pending_height;
	gfloat                 settle;           /* seconds pending size held */
	gboolean               render_queued;
	gboolean               warned_nested;

	/* TEXTURE mode: the application's content */
	GrlTexture            *texture;
	GrlRenderTexture      *source_target;

	/* C render callback */
	LrgViewportRenderFunc  render_func;
	gpointer               render_data;
	GDestroyNotify         render_destroy;

	/* Input */
	guint                  drag_buttons;     /* mask of buttons pressed on the content */
	gboolean               cursor_locked;
	gfloat                 saved_mouse_x;    /* where the cursor was when locked */
	gfloat                 saved_mouse_y;
	gboolean               have_last;        /* last_u/v valid for deltas */
	gfloat                 last_u;
	gfloat                 last_v;
	gboolean               panning;
};

G_DEFINE_FINAL_TYPE (LrgViewport, lrg_viewport, LRG_TYPE_WIDGET)

enum
{
	PROP_0,
	PROP_MODE,
	PROP_FIT,
	PROP_FILTER,
	PROP_RESOLUTION_SCALE,
	PROP_FIXED_WIDTH,
	PROP_FIXED_HEIGHT,
	PROP_RESIZE_DELAY,
	PROP_CONTINUOUS,
	PROP_AUTO_CLEAR,
	PROP_CLEAR_COLOR,
	PROP_TEXTURE,
	PROP_FLIP_Y,
	PROP_LOCK_CURSOR,
	PROP_DRAGGING,
	N_PROPS
};

enum
{
	SIGNAL_RENDER,
	SIGNAL_RESIZED,
	SIGNAL_POINTER_MOVE,
	SIGNAL_POINTER_PRESS,
	SIGNAL_POINTER_RELEASE,
	SIGNAL_POINTER_SCROLL,
	SIGNAL_KEY,
	SIGNAL_PAN,
	SIGNAL_PAN_END,
	SIGNAL_ZOOM,
	N_SIGNALS
};

static GParamSpec *properties[N_PROPS];
static guint       signals[N_SIGNALS];

/* ==========================================================================
 * Enumeration types
 * ========================================================================== */

GType
lrg_viewport_mode_get_type (void)
{
	static gsize type_id = 0;

	if (g_once_init_enter (&type_id))
	{
		static const GEnumValue values[] = {
			{ LRG_VIEWPORT_MODE_RENDER, "LRG_VIEWPORT_MODE_RENDER", "render" },
			{ LRG_VIEWPORT_MODE_TEXTURE, "LRG_VIEWPORT_MODE_TEXTURE", "texture" },
			{ 0, NULL, NULL }
		};
		GType id = g_enum_register_static (g_intern_static_string ("LrgViewportMode"), values);

		g_once_init_leave (&type_id, id);
	}
	return (GType) type_id;
}

GType
lrg_viewport_fit_get_type (void)
{
	static gsize type_id = 0;

	if (g_once_init_enter (&type_id))
	{
		static const GEnumValue values[] = {
			{ LRG_VIEWPORT_FIT_STRETCH, "LRG_VIEWPORT_FIT_STRETCH", "stretch" },
			{ LRG_VIEWPORT_FIT_CONTAIN, "LRG_VIEWPORT_FIT_CONTAIN", "contain" },
			{ LRG_VIEWPORT_FIT_COVER, "LRG_VIEWPORT_FIT_COVER", "cover" },
			{ LRG_VIEWPORT_FIT_INTEGER_SCALE, "LRG_VIEWPORT_FIT_INTEGER_SCALE", "integer-scale" },
			{ 0, NULL, NULL }
		};
		GType id = g_enum_register_static (g_intern_static_string ("LrgViewportFit"), values);

		g_once_init_leave (&type_id, id);
	}
	return (GType) type_id;
}

/* ==========================================================================
 * Geometry
 * ========================================================================== */

gboolean
lrg_viewport_compute_fit (LrgViewportFit      fit,
                          gfloat              texture_width,
                          gfloat              texture_height,
                          const GrlRectangle *area,
                          GrlRectangle       *dest,
                          GrlRectangle       *source)
{
	gfloat sx;
	gfloat sy;
	gfloat iw;
	gfloat ih;
	gfloat ix;
	gfloat iy;
	gfloat x0;
	gfloat y0;
	gfloat x1;
	gfloat y1;

	g_return_val_if_fail (area != NULL, FALSE);
	g_return_val_if_fail (dest != NULL, FALSE);
	g_return_val_if_fail (source != NULL, FALSE);

	dest->x = area->x;
	dest->y = area->y;
	dest->width = 0.0f;
	dest->height = 0.0f;
	source->x = source->y = source->width = source->height = 0.0f;

	if (texture_width <= 0.0f || texture_height <= 0.0f || area->width <= 0.0f || area->height <= 0.0f)
		return FALSE;

	/* 1. Scale factors per fit rule */
	sx = area->width / texture_width;
	sy = area->height / texture_height;
	switch (fit)
	{
	case LRG_VIEWPORT_FIT_CONTAIN:
		sx = sy = MIN (sx, sy);
		break;
	case LRG_VIEWPORT_FIT_COVER:
		sx = sy = MAX (sx, sy);
		break;
	case LRG_VIEWPORT_FIT_INTEGER_SCALE:
		/* Small epsilon so an exact multiple is not lost to rounding */
		sx = sy = MAX (1.0f, floorf (MIN (sx, sy) + 1e-4f));
		break;
	case LRG_VIEWPORT_FIT_STRETCH:
	default:
		break;
	}

	/* 2. The whole scaled image, centred in the area. Integer scaling
	 *    snaps the origin to whole units so texels land on pixels. */
	iw = texture_width * sx;
	ih = texture_height * sy;
	ix = area->x + (area->width - iw) * 0.5f;
	iy = area->y + (area->height - ih) * 0.5f;
	if (fit == LRG_VIEWPORT_FIT_INTEGER_SCALE)
	{
		ix = floorf (ix);
		iy = floorf (iy);
	}

	/* 3. Clip to the area; the source shrinks by the same amount
	 *    (cover and oversized integer scale crop here) */
	x0 = MAX (ix, area->x);
	y0 = MAX (iy, area->y);
	x1 = MIN (ix + iw, area->x + area->width);
	y1 = MIN (iy + ih, area->y + area->height);
	if (x1 <= x0 || y1 <= y0)
		return FALSE;

	dest->x = x0;
	dest->y = y0;
	dest->width = x1 - x0;
	dest->height = y1 - y0;
	source->x = (x0 - ix) / sx;
	source->y = (y0 - iy) / sy;
	source->width = dest->width / sx;
	source->height = dest->height / sy;
	return TRUE;
}

/* box_look: the resolved BOX look (border width defines the content area) */
static void
box_look (LrgViewport *self,
          LrgPartLook *look)
{
	lrg_widget_resolve_look (LRG_WIDGET (self), LRG_UI_PART_BOX, look);
}

/* content_rect_from: the allocation inset by @look's border, widget-relative */
static void
content_rect_from (LrgViewport       *self,
                   const LrgPartLook *look,
                   GrlRectangle      *rect)
{
	gfloat bw = MAX (0.0f, look->border_width);

	rect->x = bw;
	rect->y = bw;
	rect->width = MAX (0.0f, lrg_widget_get_width (LRG_WIDGET (self)) - 2.0f * bw);
	rect->height = MAX (0.0f, lrg_widget_get_height (LRG_WIDGET (self)) - 2.0f * bw);
}

void
lrg_viewport_get_content_rect (LrgViewport  *self,
                               GrlRectangle *rect)
{
	LrgPartLook look;

	g_return_if_fail (LRG_IS_VIEWPORT (self));
	g_return_if_fail (rect != NULL);

	box_look (self, &look);
	content_rect_from (self, &look, rect);
}

/*
 * desired_size:
 *
 * The render texture size RENDER mode wants now: the fixed resolution
 * when both dimensions are set, otherwise the content area times the
 * resolution scale (at least one pixel when the area is not empty).
 */
static void
desired_size (LrgViewport *self,
              gint        *width,
              gint        *height)
{
	GrlRectangle area;

	if (self->fixed_width > 0 && self->fixed_height > 0)
	{
		*width = self->fixed_width;
		*height = self->fixed_height;
		return;
	}

	lrg_viewport_get_content_rect (self, &area);
	*width = area.width > 0.0f ? MAX (1, (gint) roundf (area.width * self->resolution_scale)) : 0;
	*height = area.height > 0.0f ? MAX (1, (gint) roundf (area.height * self->resolution_scale)) : 0;
}

/*
 * shown_texture:
 * @actual_w: (out): pixel size of the texture actually sampled
 * @actual_h: (out)
 * @flipped: (out): whether it is stored bottom-up
 *
 * The texture painted right now, or %NULL (headless, nothing set).
 */
static GrlTexture *
shown_texture (LrgViewport *self,
               gint        *actual_w,
               gint        *actual_h,
               gboolean    *flipped)
{
	GrlTexture *tex;

	if (self->mode == LRG_VIEWPORT_MODE_RENDER)
	{
		tex = self->target_texture;
		*flipped = TRUE;
	}
	else
	{
		tex = self->texture;
		*flipped = self->flip_y;
	}

	*actual_w = tex != NULL ? grl_texture_get_width (tex) : 0;
	*actual_h = tex != NULL ? grl_texture_get_height (tex) : 0;
	return tex;
}

void
lrg_viewport_get_content_size (LrgViewport *self,
                               gint        *width,
                               gint        *height)
{
	gint     w = 0;
	gint     h = 0;
	gboolean flipped;

	g_return_if_fail (LRG_IS_VIEWPORT (self));

	if (shown_texture (self, &w, &h, &flipped) == NULL && self->mode == LRG_VIEWPORT_MODE_RENDER)
		desired_size (self, &w, &h);

	if (width != NULL)
		*width = w;
	if (height != NULL)
		*height = h;
}

/*
 * display_geometry:
 * @dest: (out): widget-relative rectangle the content is drawn in
 * @source: (out): visible region in content pixels (the texture's own
 *   pixels, i.e. the space pointer coordinates are reported in)
 *
 * In RENDER mode the layout is computed with the size the texture is
 * heading for (desired), so letterbox bars do not jump while a resize
 * waits to settle; the source is then rescaled into the pixels of the
 * texture that still exists, which stretches it over the new area.
 */
static gboolean
display_geometry (LrgViewport  *self,
                  GrlRectangle *dest,
                  GrlRectangle *source)
{
	GrlRectangle area;
	gint         lw;
	gint         lh;
	gint         cw;
	gint         ch;
	gboolean     flipped;

	lrg_viewport_get_content_rect (self, &area);

	if (self->mode == LRG_VIEWPORT_MODE_RENDER)
		desired_size (self, &lw, &lh);
	else
		shown_texture (self, &lw, &lh, &flipped);

	if (!lrg_viewport_compute_fit (self->fit, (gfloat) lw, (gfloat) lh, &area, dest, source))
		return FALSE;

	lrg_viewport_get_content_size (self, &cw, &ch);
	if (cw != lw || ch != lh)
	{
		gfloat kx = (gfloat) cw / (gfloat) lw;
		gfloat ky = (gfloat) ch / (gfloat) lh;

		source->x *= kx;
		source->width *= kx;
		source->y *= ky;
		source->height *= ky;
	}
	return TRUE;
}

gboolean
lrg_viewport_get_display_rect (LrgViewport  *self,
                               GrlRectangle *rect)
{
	GrlRectangle source;

	g_return_val_if_fail (LRG_IS_VIEWPORT (self), FALSE);
	g_return_val_if_fail (rect != NULL, FALSE);

	return display_geometry (self, rect, &source);
}

gboolean
lrg_viewport_widget_to_texture (LrgViewport *self,
                                gfloat       x,
                                gfloat       y,
                                gfloat      *u,
                                gfloat      *v)
{
	GrlRectangle dest;
	GrlRectangle source;
	gfloat       tu = 0.0f;
	gfloat       tv = 0.0f;
	gboolean     inside = FALSE;

	g_return_val_if_fail (LRG_IS_VIEWPORT (self), FALSE);

	if (display_geometry (self, &dest, &source))
	{
		/* Linear map dest -> source; extrapolates outside */
		tu = source.x + (x - dest.x) * (source.width / dest.width);
		tv = source.y + (y - dest.y) * (source.height / dest.height);
		inside = x >= dest.x && x < dest.x + dest.width &&
		         y >= dest.y && y < dest.y + dest.height;
	}

	if (u != NULL)
		*u = tu;
	if (v != NULL)
		*v = tv;
	return inside;
}

void
lrg_viewport_texture_to_widget (LrgViewport *self,
                                gfloat       u,
                                gfloat       v,
                                gfloat      *x,
                                gfloat      *y)
{
	GrlRectangle dest;
	GrlRectangle source;
	gfloat       wx = 0.0f;
	gfloat       wy = 0.0f;

	g_return_if_fail (LRG_IS_VIEWPORT (self));

	if (display_geometry (self, &dest, &source))
	{
		wx = dest.x + (u - source.x) * (dest.width / source.width);
		wy = dest.y + (v - source.y) * (dest.height / source.height);
	}

	if (x != NULL)
		*x = wx;
	if (y != NULL)
		*y = wy;
}

/* ==========================================================================
 * Render target
 * ========================================================================== */

/* release_target: drop the owned render texture */
static void
release_target (LrgViewport *self)
{
	g_clear_object (&self->target_texture);
	g_clear_object (&self->target);
	self->target_width = 0;
	self->target_height = 0;
	self->settle = 0.0f;
}

/*
 * ensure_target:
 * @allow_resize: recreate an existing target whose size differs
 *
 * Creates the render texture at the desired size when there is none,
 * or recreates it on a size change when allowed. Emits "resized" for
 * every new texture.
 *
 * Returns: %TRUE if a valid target exists afterwards
 */
static gboolean
ensure_target (LrgViewport *self,
               gint         width,
               gint         height,
               gboolean     allow_resize)
{
	g_autoptr(GrlRenderTexture) target = NULL;

	if (self->target != NULL &&
	    (!allow_resize || (self->target_width == width && self->target_height == height)))
		return TRUE;

	target = grl_render_texture_new (width, height);
	if (target == NULL || !grl_render_texture_is_valid (target))
	{
		lrg_warning (LRG_LOG_DOMAIN_UI, "viewport: could not create a %d x %d render texture",
		             width, height);
		return self->target != NULL;
	}

	release_target (self);
	self->target = g_steal_pointer (&target);
	self->target_texture = grl_render_texture_get_texture (self->target);
	self->target_width = width;
	self->target_height = height;
	self->pending_width = width;
	self->pending_height = height;
	if (self->target_texture != NULL)
		grl_texture_set_filter (self->target_texture, self->filter);

	lrg_debug (LRG_LOG_DOMAIN_UI, "viewport: render texture %d x %d", width, height);
	g_signal_emit (self, signals[SIGNAL_RESIZED], 0, width, height);
	return TRUE;
}

/*
 * render_internal:
 * @allow_resize: whether a size change may recreate the target now
 *
 * The body of lrg_viewport_render(): needs a GL context, a non-empty
 * size and no other framebuffer bound (raylib texture modes do not
 * nest: EndTextureMode() would leave the outer target unbound).
 */
static gboolean
render_internal (LrgViewport *self,
                 gboolean     allow_resize)
{
	gint   width;
	gint   height;
	gfloat tw;
	gfloat th;

	if (self->mode != LRG_VIEWPORT_MODE_RENDER || !IsWindowReady ())
		return FALSE;

	desired_size (self, &width, &height);
	if (width <= 0 || height <= 0)
		return FALSE;

	/* Inside someone else's texture mode (or the UI paint pass into a
	 * render texture): try again on the next step */
	if (rlGetActiveFramebuffer () != 0)
	{
		if (!self->warned_nested)
		{
			lrg_warning (LRG_LOG_DOMAIN_UI,
			             "viewport: render skipped while another render texture is active; "
			             "call lrg_canvas_update() outside BeginTextureMode()");
			self->warned_nested = TRUE;
		}
		self->render_queued = TRUE;
		return FALSE;
	}

	if (!ensure_target (self, width, height, allow_resize))
		return FALSE;

	/* The application draws in the target's own pixels */
	tw = (gfloat) self->target_width;
	th = (gfloat) self->target_height;

	grl_render_texture_begin (self->target);
	if (self->auto_clear)
		ClearBackground ((Color) { self->clear_color.r, self->clear_color.g,
		                           self->clear_color.b, self->clear_color.a });
	if (self->render_func != NULL)
		self->render_func (self, self->target, tw, th, self->render_data);
	g_signal_emit (self, signals[SIGNAL_RENDER], 0, self->target, tw, th);
	grl_render_texture_end (self->target);

	self->render_queued = FALSE;
	return TRUE;
}

gboolean
lrg_viewport_render (LrgViewport *self)
{
	g_return_val_if_fail (LRG_IS_VIEWPORT (self), FALSE);

	return render_internal (self, TRUE);
}

/* ==========================================================================
 * Cursor lock
 * ========================================================================== */

static void
set_cursor_locked (LrgViewport *self,
                   gboolean     locked)
{
	if (self->cursor_locked == locked)
		return;
	self->cursor_locked = locked;

	if (!IsWindowReady ())
		return;

	/* raylib re-centres the cursor on both calls; remember and restore
	 * where it was so the pointer reappears where the drag began */
	if (locked)
	{
		Vector2 p = GetMousePosition ();

		self->saved_mouse_x = p.x;
		self->saved_mouse_y = p.y;
		DisableCursor ();
	}
	else
	{
		EnableCursor ();
		SetMousePosition ((gint) self->saved_mouse_x, (gint) self->saved_mouse_y);
	}
}

/* ==========================================================================
 * LrgWidget
 * ========================================================================== */

static void
lrg_viewport_measure (LrgWidget *widget,
                      gfloat    *preferred_width,
                      gfloat    *preferred_height)
{
	LrgViewport *self = LRG_VIEWPORT (widget);
	LrgPartLook  look;
	gfloat       bw;

	/* A viewport takes the space it is given; a fixed resolution is a
	 * natural size (one texel per unit), otherwise a small minimum */
	box_look (self, &look);
	bw = 2.0f * MAX (0.0f, look.border_width);
	if (self->fixed_width > 0 && self->fixed_height > 0)
	{
		*preferred_width = (gfloat) self->fixed_width + bw;
		*preferred_height = (gfloat) self->fixed_height + bw;
	}
	else
	{
		*preferred_width = DEFAULT_SIZE + bw;
		*preferred_height = DEFAULT_SIZE + bw;
	}
}

static void
lrg_viewport_style_defaults (LrgWidget      *widget,
                             LrgUIPart       part,
                             LrgWidgetState  state,
                             LrgPartLook    *look)
{
	LrgTheme *theme = lrg_widget_get_theme (widget);

	if (part != LRG_UI_PART_BOX)
	{
		LRG_WIDGET_CLASS (lrg_viewport_parent_class)->style_defaults (widget, part, state, look);
		return;
	}

	/* A recessed frame: WINDOW shows through letterbox bars, a hairline
	 * border that turns ACCENT while the scene has keyboard focus, the
	 * theme's corner radius and a soft shadow */
	lrg_part_look_set_background (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_WINDOW));
	lrg_part_look_set_border (look,
	                          lrg_theme_get_color (theme, (state & LRG_WIDGET_STATE_FOCUSED)
	                                                      ? LRG_THEME_COLOR_ACCENT
	                                                      : LRG_THEME_COLOR_BORDER),
	                          lrg_theme_get_metric (theme, LRG_THEME_METRIC_BORDER_WIDTH));
	lrg_part_look_set_radius (look, lrg_theme_get_metric (theme, LRG_THEME_METRIC_CORNER_RADIUS));
	lrg_part_look_set_shadow (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_SHADOW),
	                          lrg_theme_get_metric (theme, LRG_THEME_METRIC_SHADOW_BLUR) * 0.5f,
	                          lrg_theme_get_metric (theme, LRG_THEME_METRIC_SHADOW_OFFSET_Y) * 0.5f);
	if (state & LRG_WIDGET_STATE_DISABLED)
		lrg_part_look_set_opacity (look, 0.45f);
}

/*
 * lrg_viewport_paint:
 *
 * 1. The BOX look over the whole allocation (shadow, letterbox fill,
 *    border).
 * 2. The content image at the fit rectangle inside the border, its
 *    corners rounded to follow the inner edge of the box: full inner
 *    radius when the image reaches the corners, less as letterbox bars
 *    move it inwards (none once a bar is as wide as the radius).
 */
static void
lrg_viewport_paint (LrgWidget    *widget,
                    LrgPaintList *list)
{
	LrgViewport  *self = LRG_VIEWPORT (widget);
	LrgPartLook   look;
	GrlRectangle  area;
	GrlRectangle  dest;
	GrlRectangle  source;
	GrlRectangle  sampled;
	GrlTexture   *tex;
	gfloat        wx = lrg_widget_get_world_x (widget);
	gfloat        wy = lrg_widget_get_world_y (widget);
	gfloat        radius;
	gfloat        inset;
	gint          tw;
	gint          th;
	gboolean      flipped;

	/* 1. Frame */
	box_look (self, &look);
	lrg_widget_paint_box (widget, list, &look, wx, wy,
	                      lrg_widget_get_width (widget), lrg_widget_get_height (widget));

	/* 2. Content */
	tex = shown_texture (self, &tw, &th, &flipped);
	if (tex == NULL || tw <= 0 || th <= 0 || !display_geometry (self, &dest, &source))
		return;

	content_rect_from (self, &look, &area);
	inset = MAX (MAX (dest.x - area.x, area.x + area.width - (dest.x + dest.width)),
	             MAX (dest.y - area.y, area.y + area.height - (dest.y + dest.height)));
	radius = MAX (0.0f, look.radii.top_left - MAX (0.0f, look.border_width) - inset);

	/* Bottom-up textures sample with a negative source height */
	sampled = source;
	if (flipped)
	{
		sampled.y = (gfloat) th - source.y;
		sampled.height = -source.height;
	}

	if (look.opacity < 1.0f)
		lrg_paint_list_push_opacity (list, look.opacity);
	lrg_paint_list_image (list, tex, &sampled, wx + dest.x, wy + dest.y, dest.width, dest.height,
	                      radius, NULL);
	if (look.opacity < 1.0f)
		lrg_paint_list_pop_opacity (list);
}

/*
 * lrg_viewport_step:
 *
 * Tracks the desired texture size (debouncing changes by
 * "resize-delay") and renders the content when continuous, queued, or
 * when a pending resize has settled.
 *
 * Returns: %TRUE while continuously rendering or while a resize is
 *   waiting to settle (only with a graphics context)
 */
static gboolean
lrg_viewport_step (LrgWidget *widget,
                   gfloat     delta_time)
{
	LrgViewport *self = LRG_VIEWPORT (widget);
	gint         width;
	gint         height;
	gboolean     resize_due = FALSE;
	gboolean     waiting = FALSE;

	if (self->mode != LRG_VIEWPORT_MODE_RENDER || !lrg_widget_get_visible (widget) || !IsWindowReady ())
		return FALSE;

	/* 1. Debounce size changes of an existing texture */
	desired_size (self, &width, &height);
	if (self->target != NULL && (width != self->target_width || height != self->target_height))
	{
		if (width != self->pending_width || height != self->pending_height)
		{
			self->pending_width = width;
			self->pending_height = height;
			self->settle = 0.0f;
		}
		else
			self->settle += MAX (0.0f, delta_time);

		resize_due = self->settle >= self->resize_delay;
		waiting = !resize_due;
	}
	else
	{
		self->pending_width = width;
		self->pending_height = height;
		self->settle = 0.0f;
	}

	/* 2. Render when wanted (the first render always creates a texture) */
	if (self->continuous || self->render_queued || resize_due || self->target == NULL)
		render_internal (self, resize_due || self->target == NULL);

	return self->continuous || waiting || self->render_queued;
}

/* emit_pointer_move: move with deltas since the previous position */
static void
emit_pointer_move (LrgViewport *self,
                   gfloat       u,
                   gfloat       v)
{
	gfloat dx = self->have_last ? u - self->last_u : 0.0f;
	gfloat dy = self->have_last ? v - self->last_v : 0.0f;

	self->last_u = u;
	self->last_v = v;
	self->have_last = TRUE;
	g_signal_emit (self, signals[SIGNAL_POINTER_MOVE], 0, u, v, dx, dy);
}

/* content_scale: content pixels per canvas unit (for touch deltas) */
static void
content_scale (LrgViewport *self,
               gfloat      *kx,
               gfloat      *ky)
{
	GrlRectangle dest;
	GrlRectangle source;

	*kx = *ky = 1.0f;
	if (display_geometry (self, &dest, &source))
	{
		*kx = source.width / dest.width;
		*ky = source.height / dest.height;
	}
}

static gboolean
lrg_viewport_handle_event (LrgWidget        *widget,
                           const LrgUIEvent *event)
{
	LrgViewport *self = LRG_VIEWPORT (widget);
	gfloat       lx = lrg_ui_event_get_x (event) - lrg_widget_get_world_x (widget);
	gfloat       ly = lrg_ui_event_get_y (event) - lrg_widget_get_world_y (widget);
	gfloat       u = 0.0f;
	gfloat       v = 0.0f;
	gboolean     inside = FALSE;
	gint         button;
	guint        bit;
	gfloat       kx;
	gfloat       ky;

	switch (lrg_ui_event_get_event_type (event))
	{
	case LRG_UI_EVENT_MOUSE_MOVE:
	case LRG_UI_EVENT_MOUSE_BUTTON_DOWN:
	case LRG_UI_EVENT_MOUSE_BUTTON_UP:
	case LRG_UI_EVENT_SCROLL:
	case LRG_UI_EVENT_ZOOM:
	case LRG_UI_EVENT_PAN:
		inside = lrg_viewport_widget_to_texture (self, lx, ly, &u, &v);
		break;
	default:
		break;
	}

	switch (lrg_ui_event_get_event_type (event))
	{
	case LRG_UI_EVENT_MOUSE_MOVE:
		/* Over the content, or anywhere during a drag (the canvas keeps
		 * sending moves to the pressed widget) */
		if (!inside && self->drag_buttons == 0)
		{
			self->have_last = FALSE;
			return FALSE;
		}
		emit_pointer_move (self, u, v);
		return TRUE;

	case LRG_UI_EVENT_MOUSE_BUTTON_DOWN:
		/* Letterbox bars and the border are not content */
		if (!inside)
			return FALSE;
		button = lrg_ui_event_get_button (event);
		bit = 1u << (guint) CLAMP (button, 0, 31);
		if (self->drag_buttons == 0)
		{
			self->drag_buttons = bit;
			g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_DRAGGING]);
			if (self->lock_cursor)
			{
				set_cursor_locked (self, TRUE);
				/* the lock re-centres the cursor: no jump in the deltas */
				self->have_last = FALSE;
			}
		}
		else
			self->drag_buttons |= bit;
		g_signal_emit (self, signals[SIGNAL_POINTER_PRESS], 0, button, u, v,
		               lrg_ui_event_get_modifiers (event));
		return TRUE;

	case LRG_UI_EVENT_MOUSE_BUTTON_UP:
		button = lrg_ui_event_get_button (event);
		bit = 1u << (guint) CLAMP (button, 0, 31);
		if ((self->drag_buttons & bit) == 0 && !inside)
			return FALSE;
		if (self->drag_buttons & bit)
		{
			self->drag_buttons &= ~bit;
			if (self->drag_buttons == 0)
			{
				set_cursor_locked (self, FALSE);
				g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_DRAGGING]);
			}
		}
		g_signal_emit (self, signals[SIGNAL_POINTER_RELEASE], 0, button, u, v,
		               lrg_ui_event_get_modifiers (event));
		return TRUE;

	case LRG_UI_EVENT_SCROLL:
		if (!inside)
			return FALSE;
		g_signal_emit (self, signals[SIGNAL_POINTER_SCROLL], 0, u, v,
		               lrg_ui_event_get_scroll_x (event), lrg_ui_event_get_scroll_y (event));
		return TRUE;

	case LRG_UI_EVENT_KEY_DOWN:
		{
			gboolean handled = FALSE;

			/* Only while the viewport itself holds focus */
			if (lrg_ui_event_get_target (event) != widget)
				return FALSE;
			g_signal_emit (self, signals[SIGNAL_KEY], 0, lrg_ui_event_get_key (event),
			               lrg_ui_event_get_modifiers (event), &handled);
			return handled;
		}

	case LRG_UI_EVENT_PAN:
		/* Claimed only with a "pan" handler, so without one a touch
		 * drag is replayed as a mouse drag */
		if (!g_signal_has_handler_pending (self, signals[SIGNAL_PAN], 0, FALSE))
			return FALSE;
		if (!self->panning && !inside)
			return FALSE;
		self->panning = TRUE;
		content_scale (self, &kx, &ky);
		g_signal_emit (self, signals[SIGNAL_PAN], 0, u, v,
		               lrg_ui_event_get_delta_x (event) * kx, lrg_ui_event_get_delta_y (event) * ky);
		return TRUE;

	case LRG_UI_EVENT_PAN_END:
		if (!self->panning)
			return FALSE;
		self->panning = FALSE;
		content_scale (self, &kx, &ky);
		g_signal_emit (self, signals[SIGNAL_PAN_END], 0,
		               lrg_ui_event_get_velocity_x (event) * kx,
		               lrg_ui_event_get_velocity_y (event) * ky);
		return TRUE;

	case LRG_UI_EVENT_ZOOM:
		if (!inside || !g_signal_has_handler_pending (self, signals[SIGNAL_ZOOM], 0, FALSE))
			return FALSE;
		g_signal_emit (self, signals[SIGNAL_ZOOM], 0, u, v, lrg_ui_event_get_scale (event));
		return TRUE;

	case LRG_UI_EVENT_POINTER_LEAVE:
		if (self->drag_buttons == 0)
			self->have_last = FALSE;
		return FALSE;

	default:
		return FALSE;
	}
}

/* ==========================================================================
 * GObject
 * ========================================================================== */

static void
lrg_viewport_dispose (GObject *object)
{
	LrgViewport *self = LRG_VIEWPORT (object);

	set_cursor_locked (self, FALSE);
	release_target (self);
	g_clear_object (&self->texture);
	g_clear_object (&self->source_target);
	if (self->render_destroy != NULL)
		self->render_destroy (self->render_data);
	self->render_func = NULL;
	self->render_data = NULL;
	self->render_destroy = NULL;

	G_OBJECT_CLASS (lrg_viewport_parent_class)->dispose (object);
}

static void
lrg_viewport_get_property (GObject    *object,
                           guint       prop_id,
                           GValue     *value,
                           GParamSpec *pspec)
{
	LrgViewport *self = LRG_VIEWPORT (object);

	switch (prop_id)
	{
	case PROP_MODE:
		g_value_set_enum (value, self->mode);
		break;
	case PROP_FIT:
		g_value_set_enum (value, self->fit);
		break;
	case PROP_FILTER:
		g_value_set_enum (value, self->filter);
		break;
	case PROP_RESOLUTION_SCALE:
		g_value_set_float (value, self->resolution_scale);
		break;
	case PROP_FIXED_WIDTH:
		g_value_set_int (value, self->fixed_width);
		break;
	case PROP_FIXED_HEIGHT:
		g_value_set_int (value, self->fixed_height);
		break;
	case PROP_RESIZE_DELAY:
		g_value_set_float (value, self->resize_delay);
		break;
	case PROP_CONTINUOUS:
		g_value_set_boolean (value, self->continuous);
		break;
	case PROP_AUTO_CLEAR:
		g_value_set_boolean (value, self->auto_clear);
		break;
	case PROP_CLEAR_COLOR:
		g_value_set_boxed (value, &self->clear_color);
		break;
	case PROP_TEXTURE:
		g_value_set_object (value, self->texture);
		break;
	case PROP_FLIP_Y:
		g_value_set_boolean (value, self->flip_y);
		break;
	case PROP_LOCK_CURSOR:
		g_value_set_boolean (value, self->lock_cursor);
		break;
	case PROP_DRAGGING:
		g_value_set_boolean (value, self->drag_buttons != 0);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_viewport_set_property (GObject      *object,
                           guint         prop_id,
                           const GValue *value,
                           GParamSpec   *pspec)
{
	LrgViewport *self = LRG_VIEWPORT (object);

	switch (prop_id)
	{
	case PROP_MODE:
		lrg_viewport_set_mode (self, (LrgViewportMode) g_value_get_enum (value));
		break;
	case PROP_FIT:
		lrg_viewport_set_fit (self, (LrgViewportFit) g_value_get_enum (value));
		break;
	case PROP_FILTER:
		lrg_viewport_set_filter (self, (GrlTextureFilter) g_value_get_enum (value));
		break;
	case PROP_RESOLUTION_SCALE:
		lrg_viewport_set_resolution_scale (self, g_value_get_float (value));
		break;
	case PROP_FIXED_WIDTH:
		lrg_viewport_set_fixed_resolution (self, g_value_get_int (value), self->fixed_height);
		break;
	case PROP_FIXED_HEIGHT:
		lrg_viewport_set_fixed_resolution (self, self->fixed_width, g_value_get_int (value));
		break;
	case PROP_RESIZE_DELAY:
		lrg_viewport_set_resize_delay (self, g_value_get_float (value));
		break;
	case PROP_CONTINUOUS:
		lrg_viewport_set_continuous (self, g_value_get_boolean (value));
		break;
	case PROP_AUTO_CLEAR:
		lrg_viewport_set_auto_clear (self, g_value_get_boolean (value));
		break;
	case PROP_CLEAR_COLOR:
		if (g_value_get_boxed (value) != NULL)
			lrg_viewport_set_clear_color (self, g_value_get_boxed (value));
		break;
	case PROP_TEXTURE:
		lrg_viewport_set_texture (self, g_value_get_object (value));
		break;
	case PROP_FLIP_Y:
		lrg_viewport_set_flip_y (self, g_value_get_boolean (value));
		break;
	case PROP_LOCK_CURSOR:
		lrg_viewport_set_lock_cursor (self, g_value_get_boolean (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_viewport_class_init (LrgViewportClass *klass)
{
	GObjectClass   *object_class = G_OBJECT_CLASS (klass);
	LrgWidgetClass *widget_class = LRG_WIDGET_CLASS (klass);
	const GParamFlags rw = G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY;

	object_class->dispose = lrg_viewport_dispose;
	object_class->get_property = lrg_viewport_get_property;
	object_class->set_property = lrg_viewport_set_property;

	widget_class->measure = lrg_viewport_measure;
	widget_class->paint = lrg_viewport_paint;
	widget_class->step = lrg_viewport_step;
	widget_class->style_defaults = lrg_viewport_style_defaults;
	widget_class->handle_event = lrg_viewport_handle_event;

	/**
	 * LrgViewport:mode:
	 *
	 * Where the content comes from: an owned render texture the
	 * application draws into, or an application texture.
	 */
	properties[PROP_MODE] =
		g_param_spec_enum ("mode", "Mode", "Where the content comes from",
		                   LRG_TYPE_VIEWPORT_MODE, LRG_VIEWPORT_MODE_RENDER, rw);

	/**
	 * LrgViewport:fit:
	 *
	 * How the texture is scaled into the content area.
	 */
	properties[PROP_FIT] =
		g_param_spec_enum ("fit", "Fit", "How the texture is scaled",
		                   LRG_TYPE_VIEWPORT_FIT, LRG_VIEWPORT_FIT_STRETCH, rw);

	/**
	 * LrgViewport:filter:
	 *
	 * Sampling filter of the owned render texture.
	 */
	properties[PROP_FILTER] =
		g_param_spec_enum ("filter", "Filter", "Sampling filter of the render texture",
		                   GRL_TYPE_TEXTURE_FILTER, GRL_TEXTURE_FILTER_BILINEAR, rw);

	/**
	 * LrgViewport:resolution-scale:
	 *
	 * Render texture pixels per canvas unit when following the
	 * allocation.
	 */
	properties[PROP_RESOLUTION_SCALE] =
		g_param_spec_float ("resolution-scale", "Resolution scale", "Pixels per canvas unit",
		                    MIN_SCALE, MAX_SCALE, 1.0f, rw);

	/**
	 * LrgViewport:fixed-width:
	 *
	 * Fixed render texture width in pixels; with
	 * #LrgViewport:fixed-height it replaces the allocation-driven size.
	 * 0 follows the allocation.
	 */
	properties[PROP_FIXED_WIDTH] =
		g_param_spec_int ("fixed-width", "Fixed width", "Fixed render width (0 = follow)",
		                  0, 16384, 0, rw);

	/**
	 * LrgViewport:fixed-height:
	 *
	 * Fixed render texture height in pixels, 0 to follow the allocation.
	 */
	properties[PROP_FIXED_HEIGHT] =
		g_param_spec_int ("fixed-height", "Fixed height", "Fixed render height (0 = follow)",
		                  0, 16384, 0, rw);

	/**
	 * LrgViewport:resize-delay:
	 *
	 * Seconds of canvas time a new size must hold before the render
	 * texture is recreated.
	 */
	properties[PROP_RESIZE_DELAY] =
		g_param_spec_float ("resize-delay", "Resize delay", "Settle time before recreating",
		                    0.0f, 10.0f, 0.1f, rw);

	/**
	 * LrgViewport:continuous:
	 *
	 * Render every frame (%TRUE) or only on request.
	 */
	properties[PROP_CONTINUOUS] =
		g_param_spec_boolean ("continuous", "Continuous", "Render every frame", TRUE, rw);

	/**
	 * LrgViewport:auto-clear:
	 *
	 * Clear the render texture to #LrgViewport:clear-color before each
	 * render.
	 */
	properties[PROP_AUTO_CLEAR] =
		g_param_spec_boolean ("auto-clear", "Auto clear", "Clear before rendering", TRUE, rw);

	/**
	 * LrgViewport:clear-color:
	 *
	 * Colour the render texture is cleared to.
	 */
	properties[PROP_CLEAR_COLOR] =
		g_param_spec_boxed ("clear-color", "Clear colour", "Colour the target is cleared to",
		                    GRL_TYPE_COLOR, rw);

	/**
	 * LrgViewport:texture:
	 *
	 * The application texture shown in %LRG_VIEWPORT_MODE_TEXTURE.
	 * Setting it switches to that mode.
	 */
	properties[PROP_TEXTURE] =
		g_param_spec_object ("texture", "Texture", "Application texture to show",
		                     GRL_TYPE_TEXTURE, rw);

	/**
	 * LrgViewport:flip-y:
	 *
	 * Show the application texture upside down (render textures).
	 */
	properties[PROP_FLIP_Y] =
		g_param_spec_boolean ("flip-y", "Flip Y", "Flip the texture vertically", FALSE, rw);

	/**
	 * LrgViewport:lock-cursor:
	 *
	 * Hide and lock the OS cursor while a button pressed on the content
	 * is held.
	 */
	properties[PROP_LOCK_CURSOR] =
		g_param_spec_boolean ("lock-cursor", "Lock cursor", "Lock the cursor during drags",
		                      FALSE, rw);

	/**
	 * LrgViewport:dragging:
	 *
	 * %TRUE while a pointer button pressed on the content is held.
	 */
	properties[PROP_DRAGGING] =
		g_param_spec_boolean ("dragging", "Dragging", "A button is held on the content",
		                      FALSE, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	g_object_class_install_properties (object_class, N_PROPS, properties);

	/**
	 * LrgViewport::render:
	 * @self: the viewport
	 * @target: the render texture, in texture mode
	 * @width: width of @target in pixels
	 * @height: height of @target in pixels
	 *
	 * Draw the content now. Emitted from lrg_canvas_update() (the
	 * widget step) between BeginTextureMode() and EndTextureMode() on
	 * @target, after the optional clear and the render function.
	 */
	signals[SIGNAL_RENDER] =
		g_signal_new ("render", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
		              0, NULL, NULL, NULL,
		              G_TYPE_NONE, 3, GRL_TYPE_RENDER_TEXTURE, G_TYPE_FLOAT, G_TYPE_FLOAT);

	/**
	 * LrgViewport::resized:
	 * @self: the viewport
	 * @width: new render texture width in pixels
	 * @height: new render texture height in pixels
	 *
	 * Emitted when a render texture was (re)created, before the next
	 * #LrgViewport::render: update camera aspect ratios here.
	 */
	signals[SIGNAL_RESIZED] =
		g_signal_new ("resized", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
		              0, NULL, NULL, NULL,
		              G_TYPE_NONE, 2, G_TYPE_INT, G_TYPE_INT);

	/**
	 * LrgViewport::pointer-move:
	 * @self: the viewport
	 * @u: X in content pixels
	 * @v: Y in content pixels
	 * @dx: change of @u since the previous move (0 for the first)
	 * @dy: change of @v since the previous move
	 *
	 * The pointer moved over the content, or anywhere while a button
	 * pressed on the content is held (coordinates then extrapolate
	 * past the edges).
	 */
	signals[SIGNAL_POINTER_MOVE] =
		g_signal_new ("pointer-move", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
		              0, NULL, NULL, NULL,
		              G_TYPE_NONE, 4, G_TYPE_FLOAT, G_TYPE_FLOAT, G_TYPE_FLOAT, G_TYPE_FLOAT);

	/**
	 * LrgViewport::pointer-press:
	 * @self: the viewport
	 * @button: mouse button (0 left, 1 right, 2 middle)
	 * @u: X in content pixels
	 * @v: Y in content pixels
	 * @modifiers: modifier keys held
	 *
	 * A button was pressed over the content (not over letterbox bars).
	 */
	signals[SIGNAL_POINTER_PRESS] =
		g_signal_new ("pointer-press", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
		              0, NULL, NULL, NULL,
		              G_TYPE_NONE, 4, G_TYPE_INT, G_TYPE_FLOAT, G_TYPE_FLOAT,
		              LRG_TYPE_INPUT_MODIFIERS);

	/**
	 * LrgViewport::pointer-release:
	 * @self: the viewport
	 * @button: mouse button
	 * @u: X in content pixels
	 * @v: Y in content pixels
	 * @modifiers: modifier keys held
	 *
	 * A button pressed on the content was released (wherever the
	 * pointer is), or a button was released over the content.
	 */
	signals[SIGNAL_POINTER_RELEASE] =
		g_signal_new ("pointer-release", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
		              0, NULL, NULL, NULL,
		              G_TYPE_NONE, 4, G_TYPE_INT, G_TYPE_FLOAT, G_TYPE_FLOAT,
		              LRG_TYPE_INPUT_MODIFIERS);

	/**
	 * LrgViewport::pointer-scroll:
	 * @self: the viewport
	 * @u: X in content pixels
	 * @v: Y in content pixels
	 * @scroll_x: horizontal wheel amount
	 * @scroll_y: vertical wheel amount
	 *
	 * The wheel turned over the content.
	 */
	signals[SIGNAL_POINTER_SCROLL] =
		g_signal_new ("pointer-scroll", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
		              0, NULL, NULL, NULL,
		              G_TYPE_NONE, 4, G_TYPE_FLOAT, G_TYPE_FLOAT, G_TYPE_FLOAT, G_TYPE_FLOAT);

	/**
	 * LrgViewport::key:
	 * @self: the viewport
	 * @key: the key pressed
	 * @modifiers: modifier keys held
	 *
	 * A key was pressed while the viewport has keyboard focus (click
	 * it or reach it with Tab). Return %TRUE to consume the key;
	 * unhandled arrows move focus and Escape cancels as usual.
	 *
	 * Returns: %TRUE to stop further handling
	 */
	signals[SIGNAL_KEY] =
		g_signal_new ("key", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
		              0, g_signal_accumulator_true_handled, NULL, NULL,
		              G_TYPE_BOOLEAN, 2, GRL_TYPE_KEY, LRG_TYPE_INPUT_MODIFIERS);

	/**
	 * LrgViewport::pan:
	 * @self: the viewport
	 * @u: finger X in content pixels
	 * @v: finger Y in content pixels
	 * @dx: movement since the last pan, in content pixels
	 * @dy: vertical movement in content pixels
	 *
	 * A one-finger touch drag that started on the content. The viewport
	 * claims touch drags only while this signal has a handler;
	 * otherwise they arrive as pointer press / move / release.
	 */
	signals[SIGNAL_PAN] =
		g_signal_new ("pan", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
		              0, NULL, NULL, NULL,
		              G_TYPE_NONE, 4, G_TYPE_FLOAT, G_TYPE_FLOAT, G_TYPE_FLOAT, G_TYPE_FLOAT);

	/**
	 * LrgViewport::pan-end:
	 * @self: the viewport
	 * @velocity_x: release velocity in content pixels per second
	 * @velocity_y: vertical release velocity
	 *
	 * The finger of a claimed pan lifted (use the velocity for flicks).
	 */
	signals[SIGNAL_PAN_END] =
		g_signal_new ("pan-end", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
		              0, NULL, NULL, NULL,
		              G_TYPE_NONE, 2, G_TYPE_FLOAT, G_TYPE_FLOAT);

	/**
	 * LrgViewport::zoom:
	 * @self: the viewport
	 * @u: pinch centre X in content pixels
	 * @v: pinch centre Y in content pixels
	 * @scale: change in finger distance since the last zoom event
	 *
	 * A two-finger pinch over the content (only consumed while a
	 * handler is connected).
	 */
	signals[SIGNAL_ZOOM] =
		g_signal_new ("zoom", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
		              0, NULL, NULL, NULL,
		              G_TYPE_NONE, 3, G_TYPE_FLOAT, G_TYPE_FLOAT, G_TYPE_FLOAT);
}

static void
lrg_viewport_init (LrgViewport *self)
{
	self->mode = LRG_VIEWPORT_MODE_RENDER;
	self->fit = LRG_VIEWPORT_FIT_STRETCH;
	self->filter = GRL_TEXTURE_FILTER_BILINEAR;
	self->resolution_scale = 1.0f;
	self->resize_delay = 0.1f;
	self->continuous = TRUE;
	self->auto_clear = TRUE;
	self->clear_color.r = 0;
	self->clear_color.g = 0;
	self->clear_color.b = 0;
	self->clear_color.a = 255;

	/* A scene inside the UI takes keyboard focus when clicked */
	lrg_widget_set_focusable (LRG_WIDGET (self), TRUE);
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

LrgViewport *
lrg_viewport_new (void)
{
	return g_object_new (LRG_TYPE_VIEWPORT, NULL);
}

LrgViewportMode
lrg_viewport_get_mode (LrgViewport *self)
{
	g_return_val_if_fail (LRG_IS_VIEWPORT (self), LRG_VIEWPORT_MODE_RENDER);

	return self->mode;
}

void
lrg_viewport_set_mode (LrgViewport     *self,
                       LrgViewportMode  mode)
{
	g_return_if_fail (LRG_IS_VIEWPORT (self));

	if (self->mode == mode)
		return;

	self->mode = mode;
	if (mode != LRG_VIEWPORT_MODE_RENDER)
		release_target (self);
	else
		self->render_queued = TRUE;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MODE]);
}

GrlTexture *
lrg_viewport_get_texture (LrgViewport *self)
{
	g_return_val_if_fail (LRG_IS_VIEWPORT (self), NULL);

	return self->texture;
}

/* set_texture_internal: replace the application texture (and its source target) */
static void
set_texture_internal (LrgViewport      *self,
                      GrlTexture       *texture,
                      GrlRenderTexture *source_target,
                      gboolean          flip_y)
{
	g_autoptr(GrlTexture) old = g_steal_pointer (&self->texture);

	self->texture = texture != NULL ? g_object_ref (texture) : NULL;
	g_set_object (&self->source_target, source_target);

	g_object_freeze_notify (G_OBJECT (self));
	if (old != self->texture)
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TEXTURE]);
	lrg_viewport_set_flip_y (self, flip_y);
	lrg_viewport_set_mode (self, LRG_VIEWPORT_MODE_TEXTURE);
	g_object_thaw_notify (G_OBJECT (self));
}

void
lrg_viewport_set_texture (LrgViewport *self,
                          GrlTexture  *texture)
{
	g_return_if_fail (LRG_IS_VIEWPORT (self));
	g_return_if_fail (texture == NULL || GRL_IS_TEXTURE (texture));

	set_texture_internal (self, texture, NULL, FALSE);
}

void
lrg_viewport_set_source_render_texture (LrgViewport      *self,
                                        GrlRenderTexture *target)
{
	g_autoptr(GrlTexture) color = NULL;

	g_return_if_fail (LRG_IS_VIEWPORT (self));
	g_return_if_fail (target == NULL || GRL_IS_RENDER_TEXTURE (target));

	if (target != NULL)
		color = grl_render_texture_get_texture (target);
	set_texture_internal (self, color, target, TRUE);
}

gboolean
lrg_viewport_get_flip_y (LrgViewport *self)
{
	g_return_val_if_fail (LRG_IS_VIEWPORT (self), FALSE);

	return self->flip_y;
}

void
lrg_viewport_set_flip_y (LrgViewport *self,
                         gboolean     flip_y)
{
	g_return_if_fail (LRG_IS_VIEWPORT (self));

	flip_y = !!flip_y;
	if (self->flip_y == flip_y)
		return;
	self->flip_y = flip_y;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_FLIP_Y]);
}

GrlRenderTexture *
lrg_viewport_get_render_texture (LrgViewport *self)
{
	g_return_val_if_fail (LRG_IS_VIEWPORT (self), NULL);

	return self->target;
}

void
lrg_viewport_set_render_func (LrgViewport           *self,
                              LrgViewportRenderFunc  func,
                              gpointer               user_data,
                              GDestroyNotify         destroy)
{
	GDestroyNotify old_destroy;
	gpointer       old_data;

	g_return_if_fail (LRG_IS_VIEWPORT (self));

	/* Swap first, free after: the destroy notify may touch the viewport */
	old_destroy = self->render_destroy;
	old_data = self->render_data;
	self->render_func = func;
	self->render_data = user_data;
	self->render_destroy = destroy;
	self->render_queued = TRUE;
	if (old_destroy != NULL)
		old_destroy (old_data);
}

void
lrg_viewport_queue_render (LrgViewport *self)
{
	g_return_if_fail (LRG_IS_VIEWPORT (self));

	self->render_queued = TRUE;
}

gboolean
lrg_viewport_get_continuous (LrgViewport *self)
{
	g_return_val_if_fail (LRG_IS_VIEWPORT (self), TRUE);

	return self->continuous;
}

void
lrg_viewport_set_continuous (LrgViewport *self,
                             gboolean     continuous)
{
	g_return_if_fail (LRG_IS_VIEWPORT (self));

	continuous = !!continuous;
	if (self->continuous == continuous)
		return;
	self->continuous = continuous;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_CONTINUOUS]);
}

gboolean
lrg_viewport_get_auto_clear (LrgViewport *self)
{
	g_return_val_if_fail (LRG_IS_VIEWPORT (self), TRUE);

	return self->auto_clear;
}

void
lrg_viewport_set_auto_clear (LrgViewport *self,
                             gboolean     auto_clear)
{
	g_return_if_fail (LRG_IS_VIEWPORT (self));

	auto_clear = !!auto_clear;
	if (self->auto_clear == auto_clear)
		return;
	self->auto_clear = auto_clear;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_AUTO_CLEAR]);
}

void
lrg_viewport_get_clear_color (LrgViewport *self,
                              GrlColor    *color)
{
	g_return_if_fail (LRG_IS_VIEWPORT (self));
	g_return_if_fail (color != NULL);

	*color = self->clear_color;
}

void
lrg_viewport_set_clear_color (LrgViewport    *self,
                              const GrlColor *color)
{
	g_return_if_fail (LRG_IS_VIEWPORT (self));
	g_return_if_fail (color != NULL);

	if (memcmp (&self->clear_color, color, sizeof (GrlColor)) == 0)
		return;
	self->clear_color = *color;
	self->render_queued = TRUE;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_CLEAR_COLOR]);
}

gfloat
lrg_viewport_get_resolution_scale (LrgViewport *self)
{
	g_return_val_if_fail (LRG_IS_VIEWPORT (self), 1.0f);

	return self->resolution_scale;
}

void
lrg_viewport_set_resolution_scale (LrgViewport *self,
                                   gfloat       scale)
{
	g_return_if_fail (LRG_IS_VIEWPORT (self));

	scale = CLAMP (scale, MIN_SCALE, MAX_SCALE);
	if (self->resolution_scale == scale)
		return;
	self->resolution_scale = scale;
	self->render_queued = TRUE;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_RESOLUTION_SCALE]);
}

void
lrg_viewport_get_fixed_resolution (LrgViewport *self,
                                   gint        *width,
                                   gint        *height)
{
	g_return_if_fail (LRG_IS_VIEWPORT (self));

	if (width != NULL)
		*width = self->fixed_width;
	if (height != NULL)
		*height = self->fixed_height;
}

void
lrg_viewport_set_fixed_resolution (LrgViewport *self,
                                   gint         width,
                                   gint         height)
{
	g_return_if_fail (LRG_IS_VIEWPORT (self));

	width = CLAMP (width, 0, 16384);
	height = CLAMP (height, 0, 16384);

	g_object_freeze_notify (G_OBJECT (self));
	if (self->fixed_width != width)
	{
		self->fixed_width = width;
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_FIXED_WIDTH]);
	}
	if (self->fixed_height != height)
	{
		self->fixed_height = height;
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_FIXED_HEIGHT]);
	}
	g_object_thaw_notify (G_OBJECT (self));

	/* The preferred size follows a fixed resolution */
	self->render_queued = TRUE;
	lrg_widget_queue_layout (LRG_WIDGET (self));
}

gfloat
lrg_viewport_get_resize_delay (LrgViewport *self)
{
	g_return_val_if_fail (LRG_IS_VIEWPORT (self), 0.0f);

	return self->resize_delay;
}

void
lrg_viewport_set_resize_delay (LrgViewport *self,
                               gfloat       delay)
{
	g_return_if_fail (LRG_IS_VIEWPORT (self));

	delay = CLAMP (delay, 0.0f, 10.0f);
	if (self->resize_delay == delay)
		return;
	self->resize_delay = delay;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_RESIZE_DELAY]);
}

LrgViewportFit
lrg_viewport_get_fit (LrgViewport *self)
{
	g_return_val_if_fail (LRG_IS_VIEWPORT (self), LRG_VIEWPORT_FIT_STRETCH);

	return self->fit;
}

void
lrg_viewport_set_fit (LrgViewport    *self,
                      LrgViewportFit  fit)
{
	g_return_if_fail (LRG_IS_VIEWPORT (self));

	if (self->fit == fit)
		return;
	self->fit = fit;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_FIT]);
}

GrlTextureFilter
lrg_viewport_get_filter (LrgViewport *self)
{
	g_return_val_if_fail (LRG_IS_VIEWPORT (self), GRL_TEXTURE_FILTER_BILINEAR);

	return self->filter;
}

void
lrg_viewport_set_filter (LrgViewport      *self,
                         GrlTextureFilter  filter)
{
	g_return_if_fail (LRG_IS_VIEWPORT (self));

	if (self->filter == filter)
		return;
	self->filter = filter;
	if (self->target_texture != NULL)
		grl_texture_set_filter (self->target_texture, filter);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_FILTER]);
}

gboolean
lrg_viewport_get_lock_cursor (LrgViewport *self)
{
	g_return_val_if_fail (LRG_IS_VIEWPORT (self), FALSE);

	return self->lock_cursor;
}

void
lrg_viewport_set_lock_cursor (LrgViewport *self,
                              gboolean     lock_cursor)
{
	g_return_if_fail (LRG_IS_VIEWPORT (self));

	lock_cursor = !!lock_cursor;
	if (self->lock_cursor == lock_cursor)
		return;
	self->lock_cursor = lock_cursor;
	if (!lock_cursor)
		set_cursor_locked (self, FALSE);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_LOCK_CURSOR]);
}

gboolean
lrg_viewport_get_dragging (LrgViewport *self)
{
	g_return_val_if_fail (LRG_IS_VIEWPORT (self), FALSE);

	return self->drag_buttons != 0;
}
