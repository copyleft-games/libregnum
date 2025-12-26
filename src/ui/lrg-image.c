/* lrg-image.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Image widget for displaying textures.
 */

#include "config.h"
#include "lrg-image.h"
#include <math.h>

/**
 * SECTION:lrg-image
 * @title: LrgImage
 * @short_description: Image display widget
 *
 * #LrgImage is a widget that displays a texture with various
 * scaling modes. It supports sprite sheet rendering via source
 * rectangles and color tinting.
 *
 * ## Scale Modes
 *
 * - %LRG_IMAGE_SCALE_MODE_FIT: Scale the image to fit within the widget
 *   bounds while maintaining aspect ratio. May leave empty space.
 * - %LRG_IMAGE_SCALE_MODE_FILL: Scale the image to completely fill
 *   the widget bounds while maintaining aspect ratio. May crop.
 * - %LRG_IMAGE_SCALE_MODE_STRETCH: Stretch the image to exactly match
 *   the widget bounds. Does not maintain aspect ratio.
 * - %LRG_IMAGE_SCALE_MODE_TILE: Tile the image to fill the widget bounds.
 *
 * ## Example
 *
 * |[<!-- language="C" -->
 * g_autoptr(GrlTexture) tex = grl_texture_new_from_file ("image.png");
 * LrgImage *image = lrg_image_new_with_texture (tex);
 *
 * lrg_image_set_scale_mode (image, LRG_IMAGE_SCALE_MODE_FIT);
 * lrg_widget_set_size (LRG_WIDGET (image), 200.0f, 150.0f);
 * ]|
 */

struct _LrgImage
{
    LrgWidget          parent_instance;

    GrlTexture        *texture;
    LrgImageScaleMode  scale_mode;
    GrlColor           tint;
    GrlRectangle      *source_rect;
};

G_DEFINE_TYPE (LrgImage, lrg_image, LRG_TYPE_WIDGET)

enum
{
    PROP_0,
    PROP_TEXTURE,
    PROP_SCALE_MODE,
    PROP_TINT,
    PROP_SOURCE_RECT,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

/* ==========================================================================
 * Private Helpers
 * ========================================================================== */

/*
 * Calculate the destination rectangle for FIT mode.
 * The image is scaled to fit within bounds while maintaining aspect ratio.
 */
static GrlRectangle
calculate_fit_rect (gfloat src_width,
                    gfloat src_height,
                    gfloat dest_x,
                    gfloat dest_y,
                    gfloat dest_width,
                    gfloat dest_height)
{
    GrlRectangle result;
    gfloat scale_x;
    gfloat scale_y;
    gfloat scale;
    gfloat final_width;
    gfloat final_height;

    scale_x = dest_width / src_width;
    scale_y = dest_height / src_height;
    scale = (scale_x < scale_y) ? scale_x : scale_y;

    final_width = src_width * scale;
    final_height = src_height * scale;

    /* Center the image within the destination */
    result.x = dest_x + (dest_width - final_width) / 2.0f;
    result.y = dest_y + (dest_height - final_height) / 2.0f;
    result.width = final_width;
    result.height = final_height;

    return result;
}

/*
 * Calculate the source and destination rectangles for FILL mode.
 * The image is scaled to fill bounds while maintaining aspect ratio,
 * cropping if necessary.
 */
static void
calculate_fill_rects (gfloat         src_width,
                      gfloat         src_height,
                      gfloat         dest_x,
                      gfloat         dest_y,
                      gfloat         dest_width,
                      gfloat         dest_height,
                      GrlRectangle  *out_source,
                      GrlRectangle  *out_dest)
{
    gfloat scale_x;
    gfloat scale_y;
    gfloat scale;
    gfloat visible_width;
    gfloat visible_height;

    scale_x = dest_width / src_width;
    scale_y = dest_height / src_height;
    scale = (scale_x > scale_y) ? scale_x : scale_y;

    /* Calculate how much of the source is visible after scaling */
    visible_width = dest_width / scale;
    visible_height = dest_height / scale;

    /* Center the visible region in the source */
    out_source->x = (src_width - visible_width) / 2.0f;
    out_source->y = (src_height - visible_height) / 2.0f;
    out_source->width = visible_width;
    out_source->height = visible_height;

    /* Destination fills the entire widget */
    out_dest->x = dest_x;
    out_dest->y = dest_y;
    out_dest->width = dest_width;
    out_dest->height = dest_height;
}

/* ==========================================================================
 * Virtual Method Overrides
 * ========================================================================== */

static void
lrg_image_draw (LrgWidget *widget)
{
    LrgImage *self = LRG_IMAGE (widget);
    GrlRectangle bounds;
    GrlRectangle source;
    GrlRectangle dest;
    GrlVector2 origin;
    gfloat src_width;
    gfloat src_height;

    /* Nothing to draw without a texture */
    if (self->texture == NULL || !grl_texture_is_valid (self->texture))
        return;

    /* Get widget bounds */
    bounds.x = lrg_widget_get_world_x (widget);
    bounds.y = lrg_widget_get_world_y (widget);
    bounds.width = lrg_widget_get_width (widget);
    bounds.height = lrg_widget_get_height (widget);

    /* Determine source rectangle (whole texture or specified region) */
    if (self->source_rect != NULL)
    {
        source = *self->source_rect;
    }
    else
    {
        source.x = 0.0f;
        source.y = 0.0f;
        source.width = (gfloat)grl_texture_get_width (self->texture);
        source.height = (gfloat)grl_texture_get_height (self->texture);
    }

    src_width = source.width;
    src_height = source.height;
    origin.x = 0.0f;
    origin.y = 0.0f;

    switch (self->scale_mode)
    {
    case LRG_IMAGE_SCALE_MODE_FIT:
        dest = calculate_fit_rect (src_width, src_height,
                                   bounds.x, bounds.y,
                                   bounds.width, bounds.height);
        grl_draw_texture_pro (self->texture, &source, &dest, &origin, 0.0f, &self->tint);
        break;

    case LRG_IMAGE_SCALE_MODE_FILL:
        {
            GrlRectangle fill_source;
            GrlRectangle fill_dest;

            calculate_fill_rects (src_width, src_height,
                                  bounds.x, bounds.y,
                                  bounds.width, bounds.height,
                                  &fill_source, &fill_dest);

            /* Adjust source rectangle if using a custom source rect */
            if (self->source_rect != NULL)
            {
                fill_source.x += self->source_rect->x;
                fill_source.y += self->source_rect->y;
            }

            grl_draw_texture_pro (self->texture, &fill_source, &fill_dest, &origin, 0.0f, &self->tint);
        }
        break;

    case LRG_IMAGE_SCALE_MODE_STRETCH:
        dest.x = bounds.x;
        dest.y = bounds.y;
        dest.width = bounds.width;
        dest.height = bounds.height;
        grl_draw_texture_pro (self->texture, &source, &dest, &origin, 0.0f, &self->tint);
        break;

    case LRG_IMAGE_SCALE_MODE_TILE:
        {
            gfloat tile_x;
            gfloat tile_y;
            GrlRectangle tile_dest;

            /* Draw tiled copies of the texture */
            for (tile_y = bounds.y; tile_y < bounds.y + bounds.height; tile_y += src_height)
            {
                for (tile_x = bounds.x; tile_x < bounds.x + bounds.width; tile_x += src_width)
                {
                    GrlRectangle tile_source;
                    gfloat draw_width;
                    gfloat draw_height;

                    /* Calculate how much of this tile is visible */
                    draw_width = src_width;
                    draw_height = src_height;

                    if (tile_x + draw_width > bounds.x + bounds.width)
                        draw_width = bounds.x + bounds.width - tile_x;
                    if (tile_y + draw_height > bounds.y + bounds.height)
                        draw_height = bounds.y + bounds.height - tile_y;

                    /* Set up source (possibly partial for edge tiles) */
                    tile_source = source;
                    tile_source.width = draw_width;
                    tile_source.height = draw_height;

                    /* Set up destination */
                    tile_dest.x = tile_x;
                    tile_dest.y = tile_y;
                    tile_dest.width = draw_width;
                    tile_dest.height = draw_height;

                    grl_draw_texture_pro (self->texture, &tile_source, &tile_dest, &origin, 0.0f, &self->tint);
                }
            }
        }
        break;

    default:
        /* Fallback to stretch */
        dest.x = bounds.x;
        dest.y = bounds.y;
        dest.width = bounds.width;
        dest.height = bounds.height;
        grl_draw_texture_pro (self->texture, &source, &dest, &origin, 0.0f, &self->tint);
        break;
    }
}

static void
lrg_image_measure (LrgWidget *widget,
                   gfloat    *preferred_width,
                   gfloat    *preferred_height)
{
    LrgImage *self = LRG_IMAGE (widget);
    gfloat width;
    gfloat height;

    width = 0.0f;
    height = 0.0f;

    if (self->texture != NULL && grl_texture_is_valid (self->texture))
    {
        if (self->source_rect != NULL)
        {
            /* Use source rectangle dimensions */
            width = self->source_rect->width;
            height = self->source_rect->height;
        }
        else
        {
            /* Use full texture dimensions */
            width = (gfloat)grl_texture_get_width (self->texture);
            height = (gfloat)grl_texture_get_height (self->texture);
        }
    }

    if (preferred_width != NULL)
        *preferred_width = width > 0.0f ? width : 100.0f;
    if (preferred_height != NULL)
        *preferred_height = height > 0.0f ? height : 100.0f;
}

static gboolean
lrg_image_handle_event (LrgWidget        *widget,
                        const LrgUIEvent *event)
{
    /* Image widget is display-only, no event handling */
    (void)widget;
    (void)event;
    return FALSE;
}

/* ==========================================================================
 * GObject Implementation
 * ========================================================================== */

static void
lrg_image_dispose (GObject *object)
{
    LrgImage *self = LRG_IMAGE (object);

    g_clear_object (&self->texture);

    G_OBJECT_CLASS (lrg_image_parent_class)->dispose (object);
}

static void
lrg_image_finalize (GObject *object)
{
    LrgImage *self = LRG_IMAGE (object);

    g_clear_pointer (&self->source_rect, grl_rectangle_free);

    G_OBJECT_CLASS (lrg_image_parent_class)->finalize (object);
}

static void
lrg_image_get_property (GObject    *object,
                        guint       prop_id,
                        GValue     *value,
                        GParamSpec *pspec)
{
    LrgImage *self = LRG_IMAGE (object);

    switch (prop_id)
    {
    case PROP_TEXTURE:
        g_value_set_object (value, self->texture);
        break;
    case PROP_SCALE_MODE:
        g_value_set_enum (value, self->scale_mode);
        break;
    case PROP_TINT:
        g_value_set_boxed (value, &self->tint);
        break;
    case PROP_SOURCE_RECT:
        g_value_set_boxed (value, self->source_rect);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_image_set_property (GObject      *object,
                        guint         prop_id,
                        const GValue *value,
                        GParamSpec   *pspec)
{
    LrgImage *self = LRG_IMAGE (object);

    switch (prop_id)
    {
    case PROP_TEXTURE:
        lrg_image_set_texture (self, g_value_get_object (value));
        break;
    case PROP_SCALE_MODE:
        lrg_image_set_scale_mode (self, g_value_get_enum (value));
        break;
    case PROP_TINT:
        lrg_image_set_tint (self, g_value_get_boxed (value));
        break;
    case PROP_SOURCE_RECT:
        lrg_image_set_source_rect (self, g_value_get_boxed (value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_image_class_init (LrgImageClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    LrgWidgetClass *widget_class = LRG_WIDGET_CLASS (klass);

    object_class->dispose = lrg_image_dispose;
    object_class->finalize = lrg_image_finalize;
    object_class->get_property = lrg_image_get_property;
    object_class->set_property = lrg_image_set_property;

    widget_class->draw = lrg_image_draw;
    widget_class->measure = lrg_image_measure;
    widget_class->handle_event = lrg_image_handle_event;

    /**
     * LrgImage:texture:
     *
     * The texture to display.
     */
    properties[PROP_TEXTURE] =
        g_param_spec_object ("texture",
                             "Texture",
                             "The texture to display",
                             GRL_TYPE_TEXTURE,
                             G_PARAM_READWRITE |
                             G_PARAM_STATIC_STRINGS |
                             G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgImage:scale-mode:
     *
     * How the image is scaled to fit the widget bounds.
     */
    properties[PROP_SCALE_MODE] =
        g_param_spec_enum ("scale-mode",
                           "Scale Mode",
                           "How the image is scaled to fit the widget bounds",
                           LRG_TYPE_IMAGE_SCALE_MODE,
                           LRG_IMAGE_SCALE_MODE_FIT,
                           G_PARAM_READWRITE |
                           G_PARAM_STATIC_STRINGS |
                           G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgImage:tint:
     *
     * The color tint applied to the texture.
     */
    properties[PROP_TINT] =
        g_param_spec_boxed ("tint",
                            "Tint",
                            "The color tint applied to the texture",
                            GRL_TYPE_COLOR,
                            G_PARAM_READWRITE |
                            G_PARAM_STATIC_STRINGS |
                            G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgImage:source-rect:
     *
     * Optional source rectangle for sprite sheet rendering.
     */
    properties[PROP_SOURCE_RECT] =
        g_param_spec_boxed ("source-rect",
                            "Source Rectangle",
                            "Optional source rectangle for sprite sheet rendering",
                            GRL_TYPE_RECTANGLE,
                            G_PARAM_READWRITE |
                            G_PARAM_STATIC_STRINGS |
                            G_PARAM_EXPLICIT_NOTIFY);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_image_init (LrgImage *self)
{
    /* Default to white (no tint) */
    self->tint.r = 255;
    self->tint.g = 255;
    self->tint.b = 255;
    self->tint.a = 255;

    self->scale_mode = LRG_IMAGE_SCALE_MODE_FIT;
    self->texture = NULL;
    self->source_rect = NULL;
}

/* ==========================================================================
 * Public API - Construction
 * ========================================================================== */

/**
 * lrg_image_new:
 *
 * Creates a new image widget without a texture.
 *
 * Returns: (transfer full): A new #LrgImage
 */
LrgImage *
lrg_image_new (void)
{
    return g_object_new (LRG_TYPE_IMAGE, NULL);
}

/**
 * lrg_image_new_with_texture:
 * @texture: (nullable): the texture to display
 *
 * Creates a new image widget with the specified texture.
 *
 * Returns: (transfer full): A new #LrgImage
 */
LrgImage *
lrg_image_new_with_texture (GrlTexture *texture)
{
    return g_object_new (LRG_TYPE_IMAGE,
                         "texture", texture,
                         NULL);
}

/* ==========================================================================
 * Public API - Texture
 * ========================================================================== */

/**
 * lrg_image_get_texture:
 * @self: an #LrgImage
 *
 * Gets the texture being displayed.
 *
 * Returns: (transfer none) (nullable): The texture, or %NULL
 */
GrlTexture *
lrg_image_get_texture (LrgImage *self)
{
    g_return_val_if_fail (LRG_IS_IMAGE (self), NULL);

    return self->texture;
}

/**
 * lrg_image_set_texture:
 * @self: an #LrgImage
 * @texture: (nullable): the texture to display
 *
 * Sets the texture to display.
 */
void
lrg_image_set_texture (LrgImage   *self,
                       GrlTexture *texture)
{
    g_return_if_fail (LRG_IS_IMAGE (self));
    g_return_if_fail (texture == NULL || GRL_IS_TEXTURE (texture));

    if (self->texture == texture)
        return;

    g_clear_object (&self->texture);
    if (texture != NULL)
        self->texture = g_object_ref (texture);

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TEXTURE]);
}

/* ==========================================================================
 * Public API - Scale Mode
 * ========================================================================== */

/**
 * lrg_image_get_scale_mode:
 * @self: an #LrgImage
 *
 * Gets the image scaling mode.
 *
 * Returns: The scale mode
 */
LrgImageScaleMode
lrg_image_get_scale_mode (LrgImage *self)
{
    g_return_val_if_fail (LRG_IS_IMAGE (self), LRG_IMAGE_SCALE_MODE_FIT);

    return self->scale_mode;
}

/**
 * lrg_image_set_scale_mode:
 * @self: an #LrgImage
 * @mode: the scale mode
 *
 * Sets how the image is scaled to fit the widget bounds.
 */
void
lrg_image_set_scale_mode (LrgImage          *self,
                          LrgImageScaleMode  mode)
{
    g_return_if_fail (LRG_IS_IMAGE (self));

    if (self->scale_mode == mode)
        return;

    self->scale_mode = mode;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SCALE_MODE]);
}

/* ==========================================================================
 * Public API - Tint
 * ========================================================================== */

/**
 * lrg_image_get_tint:
 * @self: an #LrgImage
 *
 * Gets the color tint applied to the texture.
 *
 * Returns: (transfer none): The tint color
 */
const GrlColor *
lrg_image_get_tint (LrgImage *self)
{
    g_return_val_if_fail (LRG_IS_IMAGE (self), NULL);

    return &self->tint;
}

/**
 * lrg_image_set_tint:
 * @self: an #LrgImage
 * @tint: the tint color
 *
 * Sets the color tint applied to the texture.
 */
void
lrg_image_set_tint (LrgImage       *self,
                    const GrlColor *tint)
{
    g_return_if_fail (LRG_IS_IMAGE (self));
    g_return_if_fail (tint != NULL);

    if (self->tint.r == tint->r &&
        self->tint.g == tint->g &&
        self->tint.b == tint->b &&
        self->tint.a == tint->a)
        return;

    self->tint = *tint;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TINT]);
}

/* ==========================================================================
 * Public API - Source Rectangle
 * ========================================================================== */

/**
 * lrg_image_get_source_rect:
 * @self: an #LrgImage
 *
 * Gets the source rectangle for sprite sheet rendering.
 *
 * Returns: (transfer none) (nullable): The source rectangle, or %NULL
 */
const GrlRectangle *
lrg_image_get_source_rect (LrgImage *self)
{
    g_return_val_if_fail (LRG_IS_IMAGE (self), NULL);

    return self->source_rect;
}

/**
 * lrg_image_set_source_rect:
 * @self: an #LrgImage
 * @rect: (nullable): the source rectangle, or %NULL for whole texture
 *
 * Sets the source rectangle to draw from the texture.
 */
void
lrg_image_set_source_rect (LrgImage           *self,
                           const GrlRectangle *rect)
{
    g_return_if_fail (LRG_IS_IMAGE (self));

    g_clear_pointer (&self->source_rect, grl_rectangle_free);

    if (rect != NULL)
        self->source_rect = grl_rectangle_copy (rect);

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SOURCE_RECT]);
}

/**
 * lrg_image_clear_source_rect:
 * @self: an #LrgImage
 *
 * Clears the source rectangle so the entire texture is drawn.
 */
void
lrg_image_clear_source_rect (LrgImage *self)
{
    lrg_image_set_source_rect (self, NULL);
}
