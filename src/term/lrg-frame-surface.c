/* lrg-frame-surface.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "config.h"
#include "lrg-frame-surface.h"

typedef struct
{
    gint          width;
    gint          height;
    gfloat        scale;
    LrgRenderMode mode;
    /* Pixel translation added to every primitive (see set_draw_offset). */
    gint          ox;
    gint          oy;
} LrgFrameSurfacePrivate;

G_DEFINE_ABSTRACT_TYPE_WITH_PRIVATE (LrgFrameSurface, lrg_frame_surface, G_TYPE_OBJECT)

enum
{
    PROP_0,
    PROP_WIDTH,
    PROP_HEIGHT,
    PROP_SCALE,
    PROP_RENDER_MODE,
    N_PROPS
};

enum
{
    SIGNAL_RESIZED,
    N_SIGNALS
};

static GParamSpec *properties[N_PROPS];
static guint signals[N_SIGNALS];

static gboolean
lrg_frame_surface_real_pick (LrgFrameSurface *self,
                             gfloat           px,
                             gfloat           py,
                             gfloat          *out_x,
                             gfloat          *out_y)
{
    (void) self;

    if (out_x != NULL)
        *out_x = px;
    if (out_y != NULL)
        *out_y = py;

    return TRUE;
}

#define SURFACE_VCALL(self, member) \
    LrgFrameSurfaceClass *klass = LRG_FRAME_SURFACE_GET_CLASS (self); \
    if (klass->member == NULL) \
        return;

void
lrg_frame_surface_begin_frame (LrgFrameSurface *self)
{
    g_return_if_fail (LRG_IS_FRAME_SURFACE (self));
    {
        SURFACE_VCALL (self, begin_frame);
        klass->begin_frame (self);
    }
}

void
lrg_frame_surface_end_frame (LrgFrameSurface *self)
{
    g_return_if_fail (LRG_IS_FRAME_SURFACE (self));
    {
        SURFACE_VCALL (self, end_frame);
        klass->end_frame (self);
    }
}

void
lrg_frame_surface_clear (LrgFrameSurface *self,
                         const GrlColor  *color)
{
    g_return_if_fail (LRG_IS_FRAME_SURFACE (self));
    {
        SURFACE_VCALL (self, clear);
        klass->clear (self, color);
    }
}

void
lrg_frame_surface_fill_rect (LrgFrameSurface *self,
                             gint             x,
                             gint             y,
                             gint             width,
                             gint             height,
                             const GrlColor  *color)
{
    g_return_if_fail (LRG_IS_FRAME_SURFACE (self));
    {
        LrgFrameSurfacePrivate *priv = lrg_frame_surface_get_instance_private (self);
        SURFACE_VCALL (self, fill_rect);
        klass->fill_rect (self, x + priv->ox, y + priv->oy, width, height, color);
    }
}

void
lrg_frame_surface_draw_rect_outline (LrgFrameSurface *self,
                                     gint             x,
                                     gint             y,
                                     gint             width,
                                     gint             height,
                                     gfloat           thickness,
                                     const GrlColor  *color)
{
    g_return_if_fail (LRG_IS_FRAME_SURFACE (self));
    {
        LrgFrameSurfacePrivate *priv = lrg_frame_surface_get_instance_private (self);
        SURFACE_VCALL (self, draw_rect_outline);
        klass->draw_rect_outline (self, x + priv->ox, y + priv->oy,
                                  width, height, thickness, color);
    }
}

void
lrg_frame_surface_draw_line (LrgFrameSurface *self,
                             gint             x1,
                             gint             y1,
                             gint             x2,
                             gint             y2,
                             gfloat           thickness,
                             const GrlColor  *color)
{
    g_return_if_fail (LRG_IS_FRAME_SURFACE (self));
    {
        LrgFrameSurfacePrivate *priv = lrg_frame_surface_get_instance_private (self);
        SURFACE_VCALL (self, draw_line);
        klass->draw_line (self, x1 + priv->ox, y1 + priv->oy,
                          x2 + priv->ox, y2 + priv->oy, thickness, color);
    }
}

void
lrg_frame_surface_push_clip (LrgFrameSurface *self,
                             gint             x,
                             gint             y,
                             gint             width,
                             gint             height)
{
    g_return_if_fail (LRG_IS_FRAME_SURFACE (self));
    {
        LrgFrameSurfacePrivate *priv = lrg_frame_surface_get_instance_private (self);
        SURFACE_VCALL (self, push_clip);
        klass->push_clip (self, x + priv->ox, y + priv->oy, width, height);
    }
}

void
lrg_frame_surface_pop_clip (LrgFrameSurface *self)
{
    g_return_if_fail (LRG_IS_FRAME_SURFACE (self));
    {
        SURFACE_VCALL (self, pop_clip);
        klass->pop_clip (self);
    }
}

void
lrg_frame_surface_draw_glyph (LrgFrameSurface   *self,
                              LrgGlyphAtlas     *atlas,
                              const LrgGlyphKey *key,
                              gfloat             x,
                              gfloat             y,
                              const GrlColor    *fg)
{
    g_return_if_fail (LRG_IS_FRAME_SURFACE (self));
    {
        LrgFrameSurfacePrivate *priv = lrg_frame_surface_get_instance_private (self);
        SURFACE_VCALL (self, draw_glyph);
        klass->draw_glyph (self, atlas, key, x + (gfloat) priv->ox,
                           y + (gfloat) priv->oy, fg);
    }
}

void
lrg_frame_surface_draw_texture_region (LrgFrameSurface    *self,
                                       GrlTexture         *texture,
                                       const GrlRectangle *src,
                                       gfloat              dx,
                                       gfloat              dy,
                                       gfloat              dw,
                                       gfloat              dh,
                                       const GrlColor     *tint)
{
    g_return_if_fail (LRG_IS_FRAME_SURFACE (self));
    {
        LrgFrameSurfacePrivate *priv = lrg_frame_surface_get_instance_private (self);
        SURFACE_VCALL (self, draw_texture_region);
        klass->draw_texture_region (self, texture, src, dx + (gfloat) priv->ox,
                                    dy + (gfloat) priv->oy, dw, dh, tint);
    }
}

void
lrg_frame_surface_set_draw_offset (LrgFrameSurface *self,
                                   gint             ox,
                                   gint             oy)
{
    LrgFrameSurfacePrivate *priv;

    g_return_if_fail (LRG_IS_FRAME_SURFACE (self));

    priv = lrg_frame_surface_get_instance_private (self);
    priv->ox = ox;
    priv->oy = oy;
}

void
lrg_frame_surface_get_draw_offset (LrgFrameSurface *self,
                                   gint            *ox,
                                   gint            *oy)
{
    LrgFrameSurfacePrivate *priv;

    g_return_if_fail (LRG_IS_FRAME_SURFACE (self));

    priv = lrg_frame_surface_get_instance_private (self);
    if (ox != NULL)
        *ox = priv->ox;
    if (oy != NULL)
        *oy = priv->oy;
}

gboolean
lrg_frame_surface_pick (LrgFrameSurface *self,
                        gfloat           px,
                        gfloat           py,
                        gfloat          *out_x,
                        gfloat          *out_y)
{
    LrgFrameSurfaceClass *klass;

    g_return_val_if_fail (LRG_IS_FRAME_SURFACE (self), FALSE);

    klass = LRG_FRAME_SURFACE_GET_CLASS (self);
    if (klass->pick == NULL)
        return FALSE;

    return klass->pick (self, px, py, out_x, out_y);
}

GrlWindow *
lrg_frame_surface_get_window (LrgFrameSurface *self)
{
	LrgFrameSurfaceClass *klass;

	g_return_val_if_fail (LRG_IS_FRAME_SURFACE (self), NULL);

	klass = LRG_FRAME_SURFACE_GET_CLASS (self);
	return klass->get_window != NULL ? klass->get_window (self) : NULL;
}

gint
lrg_frame_surface_get_width (LrgFrameSurface *self)
{
    LrgFrameSurfacePrivate *priv;

    g_return_val_if_fail (LRG_IS_FRAME_SURFACE (self), 0);

    priv = lrg_frame_surface_get_instance_private (self);
    return priv->width;
}

gint
lrg_frame_surface_get_height (LrgFrameSurface *self)
{
    LrgFrameSurfacePrivate *priv;

    g_return_val_if_fail (LRG_IS_FRAME_SURFACE (self), 0);

    priv = lrg_frame_surface_get_instance_private (self);
    return priv->height;
}

gfloat
lrg_frame_surface_get_scale (LrgFrameSurface *self)
{
    LrgFrameSurfacePrivate *priv;

    g_return_val_if_fail (LRG_IS_FRAME_SURFACE (self), 1.0f);

    priv = lrg_frame_surface_get_instance_private (self);
    return priv->scale;
}

LrgRenderMode
lrg_frame_surface_get_render_mode (LrgFrameSurface *self)
{
    LrgFrameSurfacePrivate *priv;

    g_return_val_if_fail (LRG_IS_FRAME_SURFACE (self), LRG_RENDER_MODE_2D);

    priv = lrg_frame_surface_get_instance_private (self);
    return priv->mode;
}

void
lrg_frame_surface_set_geometry (LrgFrameSurface *self,
                                gint             width,
                                gint             height,
                                gfloat           scale)
{
    LrgFrameSurfacePrivate *priv;
    gboolean size_changed;

    g_return_if_fail (LRG_IS_FRAME_SURFACE (self));

    priv = lrg_frame_surface_get_instance_private (self);
    size_changed = (priv->width != width || priv->height != height);

    if (!size_changed && priv->scale == scale)
        return;

    if (priv->width != width)
    {
        priv->width = width;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_WIDTH]);
    }
    if (priv->height != height)
    {
        priv->height = height;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_HEIGHT]);
    }
    if (priv->scale != scale)
    {
        priv->scale = scale;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SCALE]);
    }

    if (size_changed)
        g_signal_emit (self, signals[SIGNAL_RESIZED], 0, width, height);
}

void
lrg_frame_surface_set_render_mode (LrgFrameSurface *self,
                                   LrgRenderMode    mode)
{
    LrgFrameSurfacePrivate *priv;

    g_return_if_fail (LRG_IS_FRAME_SURFACE (self));

    priv = lrg_frame_surface_get_instance_private (self);
    if (priv->mode != mode)
    {
        priv->mode = mode;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_RENDER_MODE]);
    }
}

static void
lrg_frame_surface_get_property (GObject    *object,
                                guint       prop_id,
                                GValue     *value,
                                GParamSpec *pspec)
{
    LrgFrameSurface *self = LRG_FRAME_SURFACE (object);
    LrgFrameSurfacePrivate *priv = lrg_frame_surface_get_instance_private (self);

    switch (prop_id)
    {
    case PROP_WIDTH:
        g_value_set_int (value, priv->width);
        break;
    case PROP_HEIGHT:
        g_value_set_int (value, priv->height);
        break;
    case PROP_SCALE:
        g_value_set_float (value, priv->scale);
        break;
    case PROP_RENDER_MODE:
        g_value_set_enum (value, priv->mode);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

void
lrg_frame_surface_begin_content (LrgFrameSurface *self)
{
	g_return_if_fail (LRG_IS_FRAME_SURFACE (self));
	{
		SURFACE_VCALL (self, begin_content);
		klass->begin_content (self);
	}
}

void
lrg_frame_surface_end_content (LrgFrameSurface *self)
{
	g_return_if_fail (LRG_IS_FRAME_SURFACE (self));
	{
		SURFACE_VCALL (self, end_content);
		klass->end_content (self);
	}
}

static void
lrg_frame_surface_class_init (LrgFrameSurfaceClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->get_property = lrg_frame_surface_get_property;

    klass->pick = lrg_frame_surface_real_pick;

    properties[PROP_WIDTH] =
        g_param_spec_int ("width", "Width", "Logical width in pixels",
                          0, G_MAXINT, 0,
                          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
    properties[PROP_HEIGHT] =
        g_param_spec_int ("height", "Height", "Logical height in pixels",
                          0, G_MAXINT, 0,
                          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
    properties[PROP_SCALE] =
        g_param_spec_float ("scale", "Scale", "DPI scale factor",
                            0.0f, G_MAXFLOAT, 1.0f,
                            G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
    properties[PROP_RENDER_MODE] =
        g_param_spec_enum ("render-mode", "Render mode",
                           "Render mode implemented by this surface",
                           LRG_TYPE_RENDER_MODE, LRG_RENDER_MODE_2D,
                           G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);

    /**
     * LrgFrameSurface::resized:
     * @self: the surface
     * @width: new logical width
     * @height: new logical height
     *
     * Emitted when the surface's logical size changes.
     */
    signals[SIGNAL_RESIZED] =
        g_signal_new ("resized",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_FIRST,
                      0, NULL, NULL, NULL,
                      G_TYPE_NONE, 2, G_TYPE_INT, G_TYPE_INT);
}

static void
lrg_frame_surface_init (LrgFrameSurface *self)
{
    LrgFrameSurfacePrivate *priv = lrg_frame_surface_get_instance_private (self);

    priv->width = 0;
    priv->height = 0;
    priv->scale = 1.0f;
    priv->mode = LRG_RENDER_MODE_2D;
    priv->ox = 0;
    priv->oy = 0;
}
