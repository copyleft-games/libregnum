/* lrg-sprite-component.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Sprite rendering component.
 */

#include "config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_ECS

#include "lrg-sprite-component.h"
#include "../lrg-game-object.h"
#include "../lrg-component-private.h"
#include "../../lrg-log.h"

/* ==========================================================================
 * Private Data
 * ========================================================================== */

struct _LrgSpriteComponent
{
    LrgComponent   parent_instance;

    GrlTexture    *texture;        /* The texture to render */
    GrlRectangle  *source_rect;    /* Source rectangle (NULL = full texture) */
    GrlColor      *tint;           /* Tint color */
    gboolean       flip_h;         /* Flip horizontally */
    gboolean       flip_v;         /* Flip vertically */
};

G_DEFINE_TYPE (LrgSpriteComponent, lrg_sprite_component, LRG_TYPE_COMPONENT)

enum
{
    PROP_0,
    PROP_TEXTURE,
    PROP_FLIP_H,
    PROP_FLIP_V,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

/* ==========================================================================
 * GObject Implementation
 * ========================================================================== */

static void
lrg_sprite_component_dispose (GObject *object)
{
    LrgSpriteComponent *self = LRG_SPRITE_COMPONENT (object);

    g_clear_object (&self->texture);

    G_OBJECT_CLASS (lrg_sprite_component_parent_class)->dispose (object);
}

static void
lrg_sprite_component_finalize (GObject *object)
{
    LrgSpriteComponent *self = LRG_SPRITE_COMPONENT (object);

    g_clear_pointer (&self->source_rect, grl_rectangle_free);
    g_clear_pointer (&self->tint, grl_color_free);

    G_OBJECT_CLASS (lrg_sprite_component_parent_class)->finalize (object);
}

static void
lrg_sprite_component_get_property (GObject    *object,
                                   guint       prop_id,
                                   GValue     *value,
                                   GParamSpec *pspec)
{
    LrgSpriteComponent *self = LRG_SPRITE_COMPONENT (object);

    switch (prop_id)
    {
    case PROP_TEXTURE:
        g_value_set_object (value, self->texture);
        break;
    case PROP_FLIP_H:
        g_value_set_boolean (value, self->flip_h);
        break;
    case PROP_FLIP_V:
        g_value_set_boolean (value, self->flip_v);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_sprite_component_set_property (GObject      *object,
                                   guint         prop_id,
                                   const GValue *value,
                                   GParamSpec   *pspec)
{
    LrgSpriteComponent *self = LRG_SPRITE_COMPONENT (object);

    switch (prop_id)
    {
    case PROP_TEXTURE:
        lrg_sprite_component_set_texture (self, g_value_get_object (value));
        break;
    case PROP_FLIP_H:
        lrg_sprite_component_set_flip_h (self, g_value_get_boolean (value));
        break;
    case PROP_FLIP_V:
        lrg_sprite_component_set_flip_v (self, g_value_get_boolean (value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_sprite_component_class_init (LrgSpriteComponentClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->dispose = lrg_sprite_component_dispose;
    object_class->finalize = lrg_sprite_component_finalize;
    object_class->get_property = lrg_sprite_component_get_property;
    object_class->set_property = lrg_sprite_component_set_property;

    /**
     * LrgSpriteComponent:texture:
     *
     * The texture to render.
     */
    properties[PROP_TEXTURE] =
        g_param_spec_object ("texture",
                             "Texture",
                             "The texture to render",
                             GRL_TYPE_TEXTURE,
                             G_PARAM_READWRITE |
                             G_PARAM_STATIC_STRINGS |
                             G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgSpriteComponent:flip-h:
     *
     * Whether to flip the sprite horizontally.
     */
    properties[PROP_FLIP_H] =
        g_param_spec_boolean ("flip-h",
                              "Flip Horizontal",
                              "Whether to flip horizontally",
                              FALSE,
                              G_PARAM_READWRITE |
                              G_PARAM_STATIC_STRINGS |
                              G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgSpriteComponent:flip-v:
     *
     * Whether to flip the sprite vertically.
     */
    properties[PROP_FLIP_V] =
        g_param_spec_boolean ("flip-v",
                              "Flip Vertical",
                              "Whether to flip vertically",
                              FALSE,
                              G_PARAM_READWRITE |
                              G_PARAM_STATIC_STRINGS |
                              G_PARAM_EXPLICIT_NOTIFY);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_sprite_component_init (LrgSpriteComponent *self)
{
    self->texture = NULL;
    self->source_rect = NULL;
    self->tint = grl_color_new (255, 255, 255, 255);  /* White = no tint */
    self->flip_h = FALSE;
    self->flip_v = FALSE;
}

/* ==========================================================================
 * Public API - Construction
 * ========================================================================== */

/*
 * lrg_sprite_component_new:
 *
 * Creates a new sprite component with no texture.
 *
 * Returns: (transfer full): A new #LrgSpriteComponent
 */
LrgSpriteComponent *
lrg_sprite_component_new (void)
{
    return g_object_new (LRG_TYPE_SPRITE_COMPONENT, NULL);
}

/*
 * lrg_sprite_component_new_with_texture:
 * @texture: initial texture
 *
 * Creates a new sprite component with the specified texture.
 *
 * Returns: (transfer full): A new #LrgSpriteComponent
 */
LrgSpriteComponent *
lrg_sprite_component_new_with_texture (GrlTexture *texture)
{
    return g_object_new (LRG_TYPE_SPRITE_COMPONENT,
                         "texture", texture,
                         NULL);
}

/* ==========================================================================
 * Public API - Texture Management
 * ========================================================================== */

/*
 * lrg_sprite_component_set_texture:
 * @self: an #LrgSpriteComponent
 * @texture: the texture to use
 *
 * Sets the texture to render.
 */
void
lrg_sprite_component_set_texture (LrgSpriteComponent *self,
                                  GrlTexture         *texture)
{
    g_return_if_fail (LRG_IS_SPRITE_COMPONENT (self));
    g_return_if_fail (texture == NULL || GRL_IS_TEXTURE (texture));

    if (g_set_object (&self->texture, texture))
    {
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TEXTURE]);
    }
}

/*
 * lrg_sprite_component_get_texture:
 * @self: an #LrgSpriteComponent
 *
 * Gets the current texture.
 *
 * Returns: (transfer none) (nullable): The texture, or %NULL
 */
GrlTexture *
lrg_sprite_component_get_texture (LrgSpriteComponent *self)
{
    g_return_val_if_fail (LRG_IS_SPRITE_COMPONENT (self), NULL);

    return self->texture;
}

/* ==========================================================================
 * Public API - Source Rectangle
 * ========================================================================== */

/*
 * lrg_sprite_component_set_source:
 * @self: an #LrgSpriteComponent
 * @x: source X coordinate
 * @y: source Y coordinate
 * @width: source width
 * @height: source height
 *
 * Sets the source rectangle for sprite sheet rendering.
 */
void
lrg_sprite_component_set_source (LrgSpriteComponent *self,
                                 gint                x,
                                 gint                y,
                                 gint                width,
                                 gint                height)
{
    g_return_if_fail (LRG_IS_SPRITE_COMPONENT (self));

    g_clear_pointer (&self->source_rect, grl_rectangle_free);
    self->source_rect = grl_rectangle_new ((gfloat)x, (gfloat)y,
                                           (gfloat)width, (gfloat)height);
}

/*
 * lrg_sprite_component_get_source:
 * @self: an #LrgSpriteComponent
 *
 * Gets the source rectangle.
 *
 * Returns: (transfer full) (nullable): The source rectangle, or %NULL
 */
GrlRectangle *
lrg_sprite_component_get_source (LrgSpriteComponent *self)
{
    g_return_val_if_fail (LRG_IS_SPRITE_COMPONENT (self), NULL);

    if (self->source_rect == NULL)
    {
        return NULL;
    }

    return grl_rectangle_copy (self->source_rect);
}

/*
 * lrg_sprite_component_clear_source:
 * @self: an #LrgSpriteComponent
 *
 * Clears the source rectangle.
 */
void
lrg_sprite_component_clear_source (LrgSpriteComponent *self)
{
    g_return_if_fail (LRG_IS_SPRITE_COMPONENT (self));

    g_clear_pointer (&self->source_rect, grl_rectangle_free);
}

/* ==========================================================================
 * Public API - Tint Color
 * ========================================================================== */

/*
 * lrg_sprite_component_set_tint:
 * @self: an #LrgSpriteComponent
 * @color: the tint color
 *
 * Sets the tint color applied to the texture.
 */
void
lrg_sprite_component_set_tint (LrgSpriteComponent *self,
                               GrlColor           *color)
{
    g_return_if_fail (LRG_IS_SPRITE_COMPONENT (self));
    g_return_if_fail (color != NULL);

    g_clear_pointer (&self->tint, grl_color_free);
    self->tint = grl_color_copy (color);
}

/*
 * lrg_sprite_component_get_tint:
 * @self: an #LrgSpriteComponent
 *
 * Gets the current tint color.
 *
 * Returns: (transfer full): The tint color
 */
GrlColor *
lrg_sprite_component_get_tint (LrgSpriteComponent *self)
{
    g_return_val_if_fail (LRG_IS_SPRITE_COMPONENT (self), NULL);

    return grl_color_copy (self->tint);
}

/* ==========================================================================
 * Public API - Flip
 * ========================================================================== */

/*
 * lrg_sprite_component_set_flip_h:
 * @self: an #LrgSpriteComponent
 * @flip: whether to flip horizontally
 *
 * Sets whether the sprite is flipped horizontally.
 */
void
lrg_sprite_component_set_flip_h (LrgSpriteComponent *self,
                                 gboolean            flip)
{
    g_return_if_fail (LRG_IS_SPRITE_COMPONENT (self));

    flip = !!flip;

    if (self->flip_h != flip)
    {
        self->flip_h = flip;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_FLIP_H]);
    }
}

/*
 * lrg_sprite_component_get_flip_h:
 * @self: an #LrgSpriteComponent
 *
 * Gets whether the sprite is flipped horizontally.
 *
 * Returns: %TRUE if flipped horizontally
 */
gboolean
lrg_sprite_component_get_flip_h (LrgSpriteComponent *self)
{
    g_return_val_if_fail (LRG_IS_SPRITE_COMPONENT (self), FALSE);

    return self->flip_h;
}

/*
 * lrg_sprite_component_set_flip_v:
 * @self: an #LrgSpriteComponent
 * @flip: whether to flip vertically
 *
 * Sets whether the sprite is flipped vertically.
 */
void
lrg_sprite_component_set_flip_v (LrgSpriteComponent *self,
                                 gboolean            flip)
{
    g_return_if_fail (LRG_IS_SPRITE_COMPONENT (self));

    flip = !!flip;

    if (self->flip_v != flip)
    {
        self->flip_v = flip;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_FLIP_V]);
    }
}

/*
 * lrg_sprite_component_get_flip_v:
 * @self: an #LrgSpriteComponent
 *
 * Gets whether the sprite is flipped vertically.
 *
 * Returns: %TRUE if flipped vertically
 */
gboolean
lrg_sprite_component_get_flip_v (LrgSpriteComponent *self)
{
    g_return_val_if_fail (LRG_IS_SPRITE_COMPONENT (self), FALSE);

    return self->flip_v;
}

/* ==========================================================================
 * Public API - Drawing
 * ========================================================================== */

/*
 * lrg_sprite_component_draw:
 * @self: an #LrgSpriteComponent
 *
 * Draws the sprite at the owning game object's render pose (its
 * interpolated position and rotation while a fixed-timestep template
 * drives render interpolation, see grl_entity_get_render_pose()).
 */
void
lrg_sprite_component_draw (LrgSpriteComponent *self)
{
    LrgGameObject    *owner;
    GrlEntity        *entity;
    GrlRectangle      source;
    GrlRectangle      dest;
    GrlVector2        origin;
    gfloat            width;
    gfloat            height;
    gfloat            rotation;

    g_return_if_fail (LRG_IS_SPRITE_COMPONENT (self));

    /* Need a texture to draw */
    if (self->texture == NULL)
    {
        return;
    }

    /* Need an owner to get position from */
    /* self passed the sprite check above, so it is a component */
    owner = _lrg_component_get_owner_unchecked (LRG_COMPONENT (self));
    if (owner == NULL)
    {
        return;
    }
    entity = GRL_ENTITY (owner);

    /* Source region: the explicit source rect, or the whole texture.
     * Everything here is on the stack; sprites draw every frame. */
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

    /* Use entity size if set, otherwise use texture/source size */
    width = grl_entity_get_width (entity);
    height = grl_entity_get_height (entity);
    if (width <= 0)
    {
        width = source.width;
    }
    if (height <= 0)
    {
        height = source.height;
    }

    /* Apply flipping by negating the source dimensions */
    if (self->flip_h)
    {
        source.width = -source.width;
    }
    if (self->flip_v)
    {
        source.height = -source.height;
    }

    /* Destination rectangle from the owner's render pose (interpolated
     * between fixed steps; the true pose otherwise) */
    grl_entity_get_render_pose (entity, &dest.x, &dest.y, &rotation, NULL);
    dest.width = width;
    dest.height = height;
    grl_entity_get_origin_xy (entity, &origin.x, &origin.y);

    /* Draw the texture */
    grl_draw_texture_pro (self->texture,
                          &source,
                          &dest,
                          &origin,
                          rotation,
                          self->tint);
}
