/* lrg-theme.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Theme singleton for consistent UI styling.
 */

#include "lrg-theme.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_UI
#include "../lrg-log.h"
#include "../text/lrg-font-manager.h"

/* ==========================================================================
 * Private Structure
 * ========================================================================== */

struct _LrgTheme
{
    GObject parent_instance;

    /* Colors */
    GrlColor primary_color;
    GrlColor secondary_color;
    GrlColor accent_color;
    GrlColor background_color;
    GrlColor surface_color;
    GrlColor text_color;
    GrlColor text_secondary_color;
    GrlColor border_color;
    GrlColor error_color;
    GrlColor success_color;

    /* Typography */
    GrlFont *default_font;
    gfloat   font_size_small;
    gfloat   font_size_normal;
    gfloat   font_size_large;

    /* Spacing */
    gfloat padding_small;
    gfloat padding_normal;
    gfloat padding_large;
    gfloat border_width;
    gfloat corner_radius;
};

G_DEFINE_TYPE (LrgTheme, lrg_theme, G_TYPE_OBJECT)

/* ==========================================================================
 * Property IDs
 * ========================================================================== */

enum
{
    PROP_0,

    /* Colors */
    PROP_PRIMARY_COLOR,
    PROP_SECONDARY_COLOR,
    PROP_ACCENT_COLOR,
    PROP_BACKGROUND_COLOR,
    PROP_SURFACE_COLOR,
    PROP_TEXT_COLOR,
    PROP_TEXT_SECONDARY_COLOR,
    PROP_BORDER_COLOR,
    PROP_ERROR_COLOR,
    PROP_SUCCESS_COLOR,

    /* Typography */
    PROP_DEFAULT_FONT,
    PROP_FONT_SIZE_SMALL,
    PROP_FONT_SIZE_NORMAL,
    PROP_FONT_SIZE_LARGE,

    /* Spacing */
    PROP_PADDING_SMALL,
    PROP_PADDING_NORMAL,
    PROP_PADDING_LARGE,
    PROP_BORDER_WIDTH,
    PROP_CORNER_RADIUS,

    N_PROPS
};

static GParamSpec *properties[N_PROPS];

/* ==========================================================================
 * Singleton
 * ========================================================================== */

static LrgTheme *default_theme = NULL;
static gboolean  font_init_attempted = FALSE;

/* ==========================================================================
 * Default Theme Values
 * ========================================================================== */

/* Dark theme color palette */
#define DEFAULT_PRIMARY_R        100
#define DEFAULT_PRIMARY_G        149
#define DEFAULT_PRIMARY_B        237
#define DEFAULT_PRIMARY_A        255

#define DEFAULT_SECONDARY_R      138
#define DEFAULT_SECONDARY_G      43
#define DEFAULT_SECONDARY_B      226
#define DEFAULT_SECONDARY_A      255

#define DEFAULT_ACCENT_R         255
#define DEFAULT_ACCENT_G         193
#define DEFAULT_ACCENT_B         7
#define DEFAULT_ACCENT_A         255

#define DEFAULT_BACKGROUND_R     18
#define DEFAULT_BACKGROUND_G     18
#define DEFAULT_BACKGROUND_B     18
#define DEFAULT_BACKGROUND_A     255

#define DEFAULT_SURFACE_R        30
#define DEFAULT_SURFACE_G        30
#define DEFAULT_SURFACE_B        30
#define DEFAULT_SURFACE_A        255

#define DEFAULT_TEXT_R           240
#define DEFAULT_TEXT_G           240
#define DEFAULT_TEXT_B           240
#define DEFAULT_TEXT_A           255

#define DEFAULT_TEXT_SECONDARY_R 160
#define DEFAULT_TEXT_SECONDARY_G 160
#define DEFAULT_TEXT_SECONDARY_B 160
#define DEFAULT_TEXT_SECONDARY_A 255

#define DEFAULT_BORDER_R         60
#define DEFAULT_BORDER_G         60
#define DEFAULT_BORDER_B         60
#define DEFAULT_BORDER_A         255

#define DEFAULT_ERROR_R          220
#define DEFAULT_ERROR_G          53
#define DEFAULT_ERROR_B          69
#define DEFAULT_ERROR_A          255

#define DEFAULT_SUCCESS_R        40
#define DEFAULT_SUCCESS_G        167
#define DEFAULT_SUCCESS_B        69
#define DEFAULT_SUCCESS_A        255

/* Typography defaults */
#define DEFAULT_FONT_SIZE_SMALL  12.0f
#define DEFAULT_FONT_SIZE_NORMAL 16.0f
#define DEFAULT_FONT_SIZE_LARGE  24.0f

/* Spacing defaults */
#define DEFAULT_PADDING_SMALL    4.0f
#define DEFAULT_PADDING_NORMAL   8.0f
#define DEFAULT_PADDING_LARGE    16.0f
#define DEFAULT_BORDER_WIDTH     1.0f
#define DEFAULT_CORNER_RADIUS    4.0f

/* ==========================================================================
 * GObject Virtual Methods
 * ========================================================================== */

static void
lrg_theme_finalize (GObject *object)
{
    LrgTheme *self = LRG_THEME (object);

    g_clear_object (&self->default_font);

    G_OBJECT_CLASS (lrg_theme_parent_class)->finalize (object);
}

static void
lrg_theme_get_property (GObject    *object,
                        guint       prop_id,
                        GValue     *value,
                        GParamSpec *pspec)
{
    LrgTheme *self = LRG_THEME (object);

    switch (prop_id)
    {
    case PROP_PRIMARY_COLOR:
        g_value_set_boxed (value, &self->primary_color);
        break;
    case PROP_SECONDARY_COLOR:
        g_value_set_boxed (value, &self->secondary_color);
        break;
    case PROP_ACCENT_COLOR:
        g_value_set_boxed (value, &self->accent_color);
        break;
    case PROP_BACKGROUND_COLOR:
        g_value_set_boxed (value, &self->background_color);
        break;
    case PROP_SURFACE_COLOR:
        g_value_set_boxed (value, &self->surface_color);
        break;
    case PROP_TEXT_COLOR:
        g_value_set_boxed (value, &self->text_color);
        break;
    case PROP_TEXT_SECONDARY_COLOR:
        g_value_set_boxed (value, &self->text_secondary_color);
        break;
    case PROP_BORDER_COLOR:
        g_value_set_boxed (value, &self->border_color);
        break;
    case PROP_ERROR_COLOR:
        g_value_set_boxed (value, &self->error_color);
        break;
    case PROP_SUCCESS_COLOR:
        g_value_set_boxed (value, &self->success_color);
        break;
    case PROP_DEFAULT_FONT:
        g_value_set_object (value, self->default_font);
        break;
    case PROP_FONT_SIZE_SMALL:
        g_value_set_float (value, self->font_size_small);
        break;
    case PROP_FONT_SIZE_NORMAL:
        g_value_set_float (value, self->font_size_normal);
        break;
    case PROP_FONT_SIZE_LARGE:
        g_value_set_float (value, self->font_size_large);
        break;
    case PROP_PADDING_SMALL:
        g_value_set_float (value, self->padding_small);
        break;
    case PROP_PADDING_NORMAL:
        g_value_set_float (value, self->padding_normal);
        break;
    case PROP_PADDING_LARGE:
        g_value_set_float (value, self->padding_large);
        break;
    case PROP_BORDER_WIDTH:
        g_value_set_float (value, self->border_width);
        break;
    case PROP_CORNER_RADIUS:
        g_value_set_float (value, self->corner_radius);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_theme_set_property (GObject      *object,
                        guint         prop_id,
                        const GValue *value,
                        GParamSpec   *pspec)
{
    LrgTheme *self = LRG_THEME (object);
    const GrlColor *color;

    switch (prop_id)
    {
    case PROP_PRIMARY_COLOR:
        color = g_value_get_boxed (value);
        if (color != NULL)
            lrg_theme_set_primary_color (self, color);
        break;
    case PROP_SECONDARY_COLOR:
        color = g_value_get_boxed (value);
        if (color != NULL)
            lrg_theme_set_secondary_color (self, color);
        break;
    case PROP_ACCENT_COLOR:
        color = g_value_get_boxed (value);
        if (color != NULL)
            lrg_theme_set_accent_color (self, color);
        break;
    case PROP_BACKGROUND_COLOR:
        color = g_value_get_boxed (value);
        if (color != NULL)
            lrg_theme_set_background_color (self, color);
        break;
    case PROP_SURFACE_COLOR:
        color = g_value_get_boxed (value);
        if (color != NULL)
            lrg_theme_set_surface_color (self, color);
        break;
    case PROP_TEXT_COLOR:
        color = g_value_get_boxed (value);
        if (color != NULL)
            lrg_theme_set_text_color (self, color);
        break;
    case PROP_TEXT_SECONDARY_COLOR:
        color = g_value_get_boxed (value);
        if (color != NULL)
            lrg_theme_set_text_secondary_color (self, color);
        break;
    case PROP_BORDER_COLOR:
        color = g_value_get_boxed (value);
        if (color != NULL)
            lrg_theme_set_border_color (self, color);
        break;
    case PROP_ERROR_COLOR:
        color = g_value_get_boxed (value);
        if (color != NULL)
            lrg_theme_set_error_color (self, color);
        break;
    case PROP_SUCCESS_COLOR:
        color = g_value_get_boxed (value);
        if (color != NULL)
            lrg_theme_set_success_color (self, color);
        break;
    case PROP_DEFAULT_FONT:
        lrg_theme_set_default_font (self, g_value_get_object (value));
        break;
    case PROP_FONT_SIZE_SMALL:
        lrg_theme_set_font_size_small (self, g_value_get_float (value));
        break;
    case PROP_FONT_SIZE_NORMAL:
        lrg_theme_set_font_size_normal (self, g_value_get_float (value));
        break;
    case PROP_FONT_SIZE_LARGE:
        lrg_theme_set_font_size_large (self, g_value_get_float (value));
        break;
    case PROP_PADDING_SMALL:
        lrg_theme_set_padding_small (self, g_value_get_float (value));
        break;
    case PROP_PADDING_NORMAL:
        lrg_theme_set_padding_normal (self, g_value_get_float (value));
        break;
    case PROP_PADDING_LARGE:
        lrg_theme_set_padding_large (self, g_value_get_float (value));
        break;
    case PROP_BORDER_WIDTH:
        lrg_theme_set_border_width (self, g_value_get_float (value));
        break;
    case PROP_CORNER_RADIUS:
        lrg_theme_set_corner_radius (self, g_value_get_float (value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_theme_class_init (LrgThemeClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = lrg_theme_finalize;
    object_class->get_property = lrg_theme_get_property;
    object_class->set_property = lrg_theme_set_property;

    /* Color properties */
    properties[PROP_PRIMARY_COLOR] =
        g_param_spec_boxed ("primary-color",
                            "Primary Color",
                            "The primary theme color",
                            GRL_TYPE_COLOR,
                            G_PARAM_READWRITE |
                            G_PARAM_EXPLICIT_NOTIFY |
                            G_PARAM_STATIC_STRINGS);

    properties[PROP_SECONDARY_COLOR] =
        g_param_spec_boxed ("secondary-color",
                            "Secondary Color",
                            "The secondary theme color",
                            GRL_TYPE_COLOR,
                            G_PARAM_READWRITE |
                            G_PARAM_EXPLICIT_NOTIFY |
                            G_PARAM_STATIC_STRINGS);

    properties[PROP_ACCENT_COLOR] =
        g_param_spec_boxed ("accent-color",
                            "Accent Color",
                            "The accent theme color",
                            GRL_TYPE_COLOR,
                            G_PARAM_READWRITE |
                            G_PARAM_EXPLICIT_NOTIFY |
                            G_PARAM_STATIC_STRINGS);

    properties[PROP_BACKGROUND_COLOR] =
        g_param_spec_boxed ("background-color",
                            "Background Color",
                            "The background color",
                            GRL_TYPE_COLOR,
                            G_PARAM_READWRITE |
                            G_PARAM_EXPLICIT_NOTIFY |
                            G_PARAM_STATIC_STRINGS);

    properties[PROP_SURFACE_COLOR] =
        g_param_spec_boxed ("surface-color",
                            "Surface Color",
                            "The surface color for panels and cards",
                            GRL_TYPE_COLOR,
                            G_PARAM_READWRITE |
                            G_PARAM_EXPLICIT_NOTIFY |
                            G_PARAM_STATIC_STRINGS);

    properties[PROP_TEXT_COLOR] =
        g_param_spec_boxed ("text-color",
                            "Text Color",
                            "The primary text color",
                            GRL_TYPE_COLOR,
                            G_PARAM_READWRITE |
                            G_PARAM_EXPLICIT_NOTIFY |
                            G_PARAM_STATIC_STRINGS);

    properties[PROP_TEXT_SECONDARY_COLOR] =
        g_param_spec_boxed ("text-secondary-color",
                            "Text Secondary Color",
                            "The secondary text color",
                            GRL_TYPE_COLOR,
                            G_PARAM_READWRITE |
                            G_PARAM_EXPLICIT_NOTIFY |
                            G_PARAM_STATIC_STRINGS);

    properties[PROP_BORDER_COLOR] =
        g_param_spec_boxed ("border-color",
                            "Border Color",
                            "The border color",
                            GRL_TYPE_COLOR,
                            G_PARAM_READWRITE |
                            G_PARAM_EXPLICIT_NOTIFY |
                            G_PARAM_STATIC_STRINGS);

    properties[PROP_ERROR_COLOR] =
        g_param_spec_boxed ("error-color",
                            "Error Color",
                            "The error state color",
                            GRL_TYPE_COLOR,
                            G_PARAM_READWRITE |
                            G_PARAM_EXPLICIT_NOTIFY |
                            G_PARAM_STATIC_STRINGS);

    properties[PROP_SUCCESS_COLOR] =
        g_param_spec_boxed ("success-color",
                            "Success Color",
                            "The success state color",
                            GRL_TYPE_COLOR,
                            G_PARAM_READWRITE |
                            G_PARAM_EXPLICIT_NOTIFY |
                            G_PARAM_STATIC_STRINGS);

    /* Typography properties */
    properties[PROP_DEFAULT_FONT] =
        g_param_spec_object ("default-font",
                             "Default Font",
                             "The default font",
                             GRL_TYPE_FONT,
                             G_PARAM_READWRITE |
                             G_PARAM_EXPLICIT_NOTIFY |
                             G_PARAM_STATIC_STRINGS);

    properties[PROP_FONT_SIZE_SMALL] =
        g_param_spec_float ("font-size-small",
                            "Font Size Small",
                            "The small font size",
                            1.0f, 100.0f, DEFAULT_FONT_SIZE_SMALL,
                            G_PARAM_READWRITE |
                            G_PARAM_EXPLICIT_NOTIFY |
                            G_PARAM_STATIC_STRINGS);

    properties[PROP_FONT_SIZE_NORMAL] =
        g_param_spec_float ("font-size-normal",
                            "Font Size Normal",
                            "The normal font size",
                            1.0f, 100.0f, DEFAULT_FONT_SIZE_NORMAL,
                            G_PARAM_READWRITE |
                            G_PARAM_EXPLICIT_NOTIFY |
                            G_PARAM_STATIC_STRINGS);

    properties[PROP_FONT_SIZE_LARGE] =
        g_param_spec_float ("font-size-large",
                            "Font Size Large",
                            "The large font size",
                            1.0f, 100.0f, DEFAULT_FONT_SIZE_LARGE,
                            G_PARAM_READWRITE |
                            G_PARAM_EXPLICIT_NOTIFY |
                            G_PARAM_STATIC_STRINGS);

    /* Spacing properties */
    properties[PROP_PADDING_SMALL] =
        g_param_spec_float ("padding-small",
                            "Padding Small",
                            "The small padding value",
                            0.0f, 100.0f, DEFAULT_PADDING_SMALL,
                            G_PARAM_READWRITE |
                            G_PARAM_EXPLICIT_NOTIFY |
                            G_PARAM_STATIC_STRINGS);

    properties[PROP_PADDING_NORMAL] =
        g_param_spec_float ("padding-normal",
                            "Padding Normal",
                            "The normal padding value",
                            0.0f, 100.0f, DEFAULT_PADDING_NORMAL,
                            G_PARAM_READWRITE |
                            G_PARAM_EXPLICIT_NOTIFY |
                            G_PARAM_STATIC_STRINGS);

    properties[PROP_PADDING_LARGE] =
        g_param_spec_float ("padding-large",
                            "Padding Large",
                            "The large padding value",
                            0.0f, 100.0f, DEFAULT_PADDING_LARGE,
                            G_PARAM_READWRITE |
                            G_PARAM_EXPLICIT_NOTIFY |
                            G_PARAM_STATIC_STRINGS);

    properties[PROP_BORDER_WIDTH] =
        g_param_spec_float ("border-width",
                            "Border Width",
                            "The default border width",
                            0.0f, 20.0f, DEFAULT_BORDER_WIDTH,
                            G_PARAM_READWRITE |
                            G_PARAM_EXPLICIT_NOTIFY |
                            G_PARAM_STATIC_STRINGS);

    properties[PROP_CORNER_RADIUS] =
        g_param_spec_float ("corner-radius",
                            "Corner Radius",
                            "The default corner radius",
                            0.0f, 50.0f, DEFAULT_CORNER_RADIUS,
                            G_PARAM_READWRITE |
                            G_PARAM_EXPLICIT_NOTIFY |
                            G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_theme_init (LrgTheme *self)
{
    /* Initialize default dark theme colors */
    self->primary_color = (GrlColor){
        DEFAULT_PRIMARY_R, DEFAULT_PRIMARY_G,
        DEFAULT_PRIMARY_B, DEFAULT_PRIMARY_A
    };
    self->secondary_color = (GrlColor){
        DEFAULT_SECONDARY_R, DEFAULT_SECONDARY_G,
        DEFAULT_SECONDARY_B, DEFAULT_SECONDARY_A
    };
    self->accent_color = (GrlColor){
        DEFAULT_ACCENT_R, DEFAULT_ACCENT_G,
        DEFAULT_ACCENT_B, DEFAULT_ACCENT_A
    };
    self->background_color = (GrlColor){
        DEFAULT_BACKGROUND_R, DEFAULT_BACKGROUND_G,
        DEFAULT_BACKGROUND_B, DEFAULT_BACKGROUND_A
    };
    self->surface_color = (GrlColor){
        DEFAULT_SURFACE_R, DEFAULT_SURFACE_G,
        DEFAULT_SURFACE_B, DEFAULT_SURFACE_A
    };
    self->text_color = (GrlColor){
        DEFAULT_TEXT_R, DEFAULT_TEXT_G,
        DEFAULT_TEXT_B, DEFAULT_TEXT_A
    };
    self->text_secondary_color = (GrlColor){
        DEFAULT_TEXT_SECONDARY_R, DEFAULT_TEXT_SECONDARY_G,
        DEFAULT_TEXT_SECONDARY_B, DEFAULT_TEXT_SECONDARY_A
    };
    self->border_color = (GrlColor){
        DEFAULT_BORDER_R, DEFAULT_BORDER_G,
        DEFAULT_BORDER_B, DEFAULT_BORDER_A
    };
    self->error_color = (GrlColor){
        DEFAULT_ERROR_R, DEFAULT_ERROR_G,
        DEFAULT_ERROR_B, DEFAULT_ERROR_A
    };
    self->success_color = (GrlColor){
        DEFAULT_SUCCESS_R, DEFAULT_SUCCESS_G,
        DEFAULT_SUCCESS_B, DEFAULT_SUCCESS_A
    };

    /* Typography defaults */
    self->default_font = NULL;
    self->font_size_small = DEFAULT_FONT_SIZE_SMALL;
    self->font_size_normal = DEFAULT_FONT_SIZE_NORMAL;
    self->font_size_large = DEFAULT_FONT_SIZE_LARGE;

    /* Spacing defaults */
    self->padding_small = DEFAULT_PADDING_SMALL;
    self->padding_normal = DEFAULT_PADDING_NORMAL;
    self->padding_large = DEFAULT_PADDING_LARGE;
    self->border_width = DEFAULT_BORDER_WIDTH;
    self->corner_radius = DEFAULT_CORNER_RADIUS;
}

/* ==========================================================================
 * Singleton Access
 * ========================================================================== */

/**
 * lrg_theme_get_default:
 *
 * Gets the default theme singleton.
 *
 * Returns: (transfer none): The default #LrgTheme instance
 */
LrgTheme *
lrg_theme_get_default (void)
{
    if (default_theme == NULL)
    {
        default_theme = lrg_theme_new ();
    }

    return default_theme;
}

/**
 * lrg_theme_new:
 *
 * Creates a new custom theme.
 *
 * Returns: (transfer full): A new #LrgTheme
 */
LrgTheme *
lrg_theme_new (void)
{
    return g_object_new (LRG_TYPE_THEME, NULL);
}

/* ==========================================================================
 * Color Getters/Setters
 * ========================================================================== */

/**
 * lrg_theme_get_primary_color:
 * @self: an #LrgTheme
 *
 * Gets the primary color.
 *
 * Returns: (transfer none): The primary color
 */
const GrlColor *
lrg_theme_get_primary_color (LrgTheme *self)
{
    g_return_val_if_fail (LRG_IS_THEME (self), NULL);
    return &self->primary_color;
}

/**
 * lrg_theme_set_primary_color:
 * @self: an #LrgTheme
 * @color: the primary color
 *
 * Sets the primary color.
 */
void
lrg_theme_set_primary_color (LrgTheme       *self,
                             const GrlColor *color)
{
    g_return_if_fail (LRG_IS_THEME (self));
    g_return_if_fail (color != NULL);

    if (memcmp (&self->primary_color, color, sizeof (GrlColor)) == 0)
        return;

    self->primary_color = *color;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_PRIMARY_COLOR]);
}

/**
 * lrg_theme_get_secondary_color:
 * @self: an #LrgTheme
 *
 * Gets the secondary color.
 *
 * Returns: (transfer none): The secondary color
 */
const GrlColor *
lrg_theme_get_secondary_color (LrgTheme *self)
{
    g_return_val_if_fail (LRG_IS_THEME (self), NULL);
    return &self->secondary_color;
}

/**
 * lrg_theme_set_secondary_color:
 * @self: an #LrgTheme
 * @color: the secondary color
 *
 * Sets the secondary color.
 */
void
lrg_theme_set_secondary_color (LrgTheme       *self,
                               const GrlColor *color)
{
    g_return_if_fail (LRG_IS_THEME (self));
    g_return_if_fail (color != NULL);

    if (memcmp (&self->secondary_color, color, sizeof (GrlColor)) == 0)
        return;

    self->secondary_color = *color;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SECONDARY_COLOR]);
}

/**
 * lrg_theme_get_accent_color:
 * @self: an #LrgTheme
 *
 * Gets the accent color.
 *
 * Returns: (transfer none): The accent color
 */
const GrlColor *
lrg_theme_get_accent_color (LrgTheme *self)
{
    g_return_val_if_fail (LRG_IS_THEME (self), NULL);
    return &self->accent_color;
}

/**
 * lrg_theme_set_accent_color:
 * @self: an #LrgTheme
 * @color: the accent color
 *
 * Sets the accent color.
 */
void
lrg_theme_set_accent_color (LrgTheme       *self,
                            const GrlColor *color)
{
    g_return_if_fail (LRG_IS_THEME (self));
    g_return_if_fail (color != NULL);

    if (memcmp (&self->accent_color, color, sizeof (GrlColor)) == 0)
        return;

    self->accent_color = *color;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ACCENT_COLOR]);
}

/**
 * lrg_theme_get_background_color:
 * @self: an #LrgTheme
 *
 * Gets the background color.
 *
 * Returns: (transfer none): The background color
 */
const GrlColor *
lrg_theme_get_background_color (LrgTheme *self)
{
    g_return_val_if_fail (LRG_IS_THEME (self), NULL);
    return &self->background_color;
}

/**
 * lrg_theme_set_background_color:
 * @self: an #LrgTheme
 * @color: the background color
 *
 * Sets the background color.
 */
void
lrg_theme_set_background_color (LrgTheme       *self,
                                const GrlColor *color)
{
    g_return_if_fail (LRG_IS_THEME (self));
    g_return_if_fail (color != NULL);

    if (memcmp (&self->background_color, color, sizeof (GrlColor)) == 0)
        return;

    self->background_color = *color;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_BACKGROUND_COLOR]);
}

/**
 * lrg_theme_get_surface_color:
 * @self: an #LrgTheme
 *
 * Gets the surface color (panels, cards, etc.).
 *
 * Returns: (transfer none): The surface color
 */
const GrlColor *
lrg_theme_get_surface_color (LrgTheme *self)
{
    g_return_val_if_fail (LRG_IS_THEME (self), NULL);
    return &self->surface_color;
}

/**
 * lrg_theme_set_surface_color:
 * @self: an #LrgTheme
 * @color: the surface color
 *
 * Sets the surface color.
 */
void
lrg_theme_set_surface_color (LrgTheme       *self,
                             const GrlColor *color)
{
    g_return_if_fail (LRG_IS_THEME (self));
    g_return_if_fail (color != NULL);

    if (memcmp (&self->surface_color, color, sizeof (GrlColor)) == 0)
        return;

    self->surface_color = *color;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SURFACE_COLOR]);
}

/**
 * lrg_theme_get_text_color:
 * @self: an #LrgTheme
 *
 * Gets the primary text color.
 *
 * Returns: (transfer none): The text color
 */
const GrlColor *
lrg_theme_get_text_color (LrgTheme *self)
{
    g_return_val_if_fail (LRG_IS_THEME (self), NULL);
    return &self->text_color;
}

/**
 * lrg_theme_set_text_color:
 * @self: an #LrgTheme
 * @color: the text color
 *
 * Sets the primary text color.
 */
void
lrg_theme_set_text_color (LrgTheme       *self,
                          const GrlColor *color)
{
    g_return_if_fail (LRG_IS_THEME (self));
    g_return_if_fail (color != NULL);

    if (memcmp (&self->text_color, color, sizeof (GrlColor)) == 0)
        return;

    self->text_color = *color;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TEXT_COLOR]);
}

/**
 * lrg_theme_get_text_secondary_color:
 * @self: an #LrgTheme
 *
 * Gets the secondary text color.
 *
 * Returns: (transfer none): The secondary text color
 */
const GrlColor *
lrg_theme_get_text_secondary_color (LrgTheme *self)
{
    g_return_val_if_fail (LRG_IS_THEME (self), NULL);
    return &self->text_secondary_color;
}

/**
 * lrg_theme_set_text_secondary_color:
 * @self: an #LrgTheme
 * @color: the secondary text color
 *
 * Sets the secondary text color.
 */
void
lrg_theme_set_text_secondary_color (LrgTheme       *self,
                                    const GrlColor *color)
{
    g_return_if_fail (LRG_IS_THEME (self));
    g_return_if_fail (color != NULL);

    if (memcmp (&self->text_secondary_color, color, sizeof (GrlColor)) == 0)
        return;

    self->text_secondary_color = *color;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TEXT_SECONDARY_COLOR]);
}

/**
 * lrg_theme_get_border_color:
 * @self: an #LrgTheme
 *
 * Gets the border color.
 *
 * Returns: (transfer none): The border color
 */
const GrlColor *
lrg_theme_get_border_color (LrgTheme *self)
{
    g_return_val_if_fail (LRG_IS_THEME (self), NULL);
    return &self->border_color;
}

/**
 * lrg_theme_set_border_color:
 * @self: an #LrgTheme
 * @color: the border color
 *
 * Sets the border color.
 */
void
lrg_theme_set_border_color (LrgTheme       *self,
                            const GrlColor *color)
{
    g_return_if_fail (LRG_IS_THEME (self));
    g_return_if_fail (color != NULL);

    if (memcmp (&self->border_color, color, sizeof (GrlColor)) == 0)
        return;

    self->border_color = *color;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_BORDER_COLOR]);
}

/**
 * lrg_theme_get_error_color:
 * @self: an #LrgTheme
 *
 * Gets the error color.
 *
 * Returns: (transfer none): The error color
 */
const GrlColor *
lrg_theme_get_error_color (LrgTheme *self)
{
    g_return_val_if_fail (LRG_IS_THEME (self), NULL);
    return &self->error_color;
}

/**
 * lrg_theme_set_error_color:
 * @self: an #LrgTheme
 * @color: the error color
 *
 * Sets the error color.
 */
void
lrg_theme_set_error_color (LrgTheme       *self,
                           const GrlColor *color)
{
    g_return_if_fail (LRG_IS_THEME (self));
    g_return_if_fail (color != NULL);

    if (memcmp (&self->error_color, color, sizeof (GrlColor)) == 0)
        return;

    self->error_color = *color;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ERROR_COLOR]);
}

/**
 * lrg_theme_get_success_color:
 * @self: an #LrgTheme
 *
 * Gets the success color.
 *
 * Returns: (transfer none): The success color
 */
const GrlColor *
lrg_theme_get_success_color (LrgTheme *self)
{
    g_return_val_if_fail (LRG_IS_THEME (self), NULL);
    return &self->success_color;
}

/**
 * lrg_theme_set_success_color:
 * @self: an #LrgTheme
 * @color: the success color
 *
 * Sets the success color.
 */
void
lrg_theme_set_success_color (LrgTheme       *self,
                             const GrlColor *color)
{
    g_return_if_fail (LRG_IS_THEME (self));
    g_return_if_fail (color != NULL);

    if (memcmp (&self->success_color, color, sizeof (GrlColor)) == 0)
        return;

    self->success_color = *color;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SUCCESS_COLOR]);
}

/* ==========================================================================
 * Typography Getters/Setters
 * ========================================================================== */

/**
 * lrg_theme_get_default_font:
 * @self: an #LrgTheme
 *
 * Gets the default font. If no font has been set and fonts have not
 * been initialized, this will attempt to initialize the font manager
 * and load system fonts automatically.
 *
 * Returns: (transfer none) (nullable): The default font
 */
GrlFont *
lrg_theme_get_default_font (LrgTheme *self)
{
    g_return_val_if_fail (LRG_IS_THEME (self), NULL);

    /* Lazy initialization: if no font set, try to initialize font manager */
    if (self->default_font == NULL && !font_init_attempted)
    {
        LrgFontManager *font_mgr;

        font_init_attempted = TRUE;
        font_mgr = lrg_font_manager_get_default ();

        if (lrg_font_manager_initialize (font_mgr, NULL))
        {
            GrlFont *font;

            font = lrg_font_manager_get_default_font (font_mgr);
            if (font != NULL)
            {
                self->default_font = g_object_ref (font);
                lrg_debug (LRG_LOG_DOMAIN_UI,
                           "Lazy-initialized default font from font manager");
            }
        }
    }

    return self->default_font;
}

/**
 * lrg_theme_set_default_font:
 * @self: an #LrgTheme
 * @font: (nullable): the default font
 *
 * Sets the default font.
 */
void
lrg_theme_set_default_font (LrgTheme *self,
                            GrlFont  *font)
{
    g_return_if_fail (LRG_IS_THEME (self));

    if (self->default_font == font)
        return;

    g_clear_object (&self->default_font);

    if (font != NULL)
        self->default_font = g_object_ref (font);

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_DEFAULT_FONT]);
}

/**
 * lrg_theme_get_font_size_small:
 * @self: an #LrgTheme
 *
 * Gets the small font size.
 *
 * Returns: The small font size
 */
gfloat
lrg_theme_get_font_size_small (LrgTheme *self)
{
    g_return_val_if_fail (LRG_IS_THEME (self), DEFAULT_FONT_SIZE_SMALL);
    return self->font_size_small;
}

/**
 * lrg_theme_set_font_size_small:
 * @self: an #LrgTheme
 * @size: the small font size
 *
 * Sets the small font size.
 */
void
lrg_theme_set_font_size_small (LrgTheme *self,
                               gfloat    size)
{
    g_return_if_fail (LRG_IS_THEME (self));

    if (self->font_size_small == size)
        return;

    self->font_size_small = size;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_FONT_SIZE_SMALL]);
}

/**
 * lrg_theme_get_font_size_normal:
 * @self: an #LrgTheme
 *
 * Gets the normal font size.
 *
 * Returns: The normal font size
 */
gfloat
lrg_theme_get_font_size_normal (LrgTheme *self)
{
    g_return_val_if_fail (LRG_IS_THEME (self), DEFAULT_FONT_SIZE_NORMAL);
    return self->font_size_normal;
}

/**
 * lrg_theme_set_font_size_normal:
 * @self: an #LrgTheme
 * @size: the normal font size
 *
 * Sets the normal font size.
 */
void
lrg_theme_set_font_size_normal (LrgTheme *self,
                                gfloat    size)
{
    g_return_if_fail (LRG_IS_THEME (self));

    if (self->font_size_normal == size)
        return;

    self->font_size_normal = size;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_FONT_SIZE_NORMAL]);
}

/**
 * lrg_theme_get_font_size_large:
 * @self: an #LrgTheme
 *
 * Gets the large font size.
 *
 * Returns: The large font size
 */
gfloat
lrg_theme_get_font_size_large (LrgTheme *self)
{
    g_return_val_if_fail (LRG_IS_THEME (self), DEFAULT_FONT_SIZE_LARGE);
    return self->font_size_large;
}

/**
 * lrg_theme_set_font_size_large:
 * @self: an #LrgTheme
 * @size: the large font size
 *
 * Sets the large font size.
 */
void
lrg_theme_set_font_size_large (LrgTheme *self,
                               gfloat    size)
{
    g_return_if_fail (LRG_IS_THEME (self));

    if (self->font_size_large == size)
        return;

    self->font_size_large = size;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_FONT_SIZE_LARGE]);
}

/* ==========================================================================
 * Spacing Getters/Setters
 * ========================================================================== */

/**
 * lrg_theme_get_padding_small:
 * @self: an #LrgTheme
 *
 * Gets the small padding.
 *
 * Returns: The small padding
 */
gfloat
lrg_theme_get_padding_small (LrgTheme *self)
{
    g_return_val_if_fail (LRG_IS_THEME (self), DEFAULT_PADDING_SMALL);
    return self->padding_small;
}

/**
 * lrg_theme_set_padding_small:
 * @self: an #LrgTheme
 * @padding: the small padding
 *
 * Sets the small padding.
 */
void
lrg_theme_set_padding_small (LrgTheme *self,
                             gfloat    padding)
{
    g_return_if_fail (LRG_IS_THEME (self));

    if (self->padding_small == padding)
        return;

    self->padding_small = padding;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_PADDING_SMALL]);
}

/**
 * lrg_theme_get_padding_normal:
 * @self: an #LrgTheme
 *
 * Gets the normal padding.
 *
 * Returns: The normal padding
 */
gfloat
lrg_theme_get_padding_normal (LrgTheme *self)
{
    g_return_val_if_fail (LRG_IS_THEME (self), DEFAULT_PADDING_NORMAL);
    return self->padding_normal;
}

/**
 * lrg_theme_set_padding_normal:
 * @self: an #LrgTheme
 * @padding: the normal padding
 *
 * Sets the normal padding.
 */
void
lrg_theme_set_padding_normal (LrgTheme *self,
                              gfloat    padding)
{
    g_return_if_fail (LRG_IS_THEME (self));

    if (self->padding_normal == padding)
        return;

    self->padding_normal = padding;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_PADDING_NORMAL]);
}

/**
 * lrg_theme_get_padding_large:
 * @self: an #LrgTheme
 *
 * Gets the large padding.
 *
 * Returns: The large padding
 */
gfloat
lrg_theme_get_padding_large (LrgTheme *self)
{
    g_return_val_if_fail (LRG_IS_THEME (self), DEFAULT_PADDING_LARGE);
    return self->padding_large;
}

/**
 * lrg_theme_set_padding_large:
 * @self: an #LrgTheme
 * @padding: the large padding
 *
 * Sets the large padding.
 */
void
lrg_theme_set_padding_large (LrgTheme *self,
                             gfloat    padding)
{
    g_return_if_fail (LRG_IS_THEME (self));

    if (self->padding_large == padding)
        return;

    self->padding_large = padding;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_PADDING_LARGE]);
}

/**
 * lrg_theme_get_border_width:
 * @self: an #LrgTheme
 *
 * Gets the default border width.
 *
 * Returns: The border width
 */
gfloat
lrg_theme_get_border_width (LrgTheme *self)
{
    g_return_val_if_fail (LRG_IS_THEME (self), DEFAULT_BORDER_WIDTH);
    return self->border_width;
}

/**
 * lrg_theme_set_border_width:
 * @self: an #LrgTheme
 * @width: the border width
 *
 * Sets the default border width.
 */
void
lrg_theme_set_border_width (LrgTheme *self,
                            gfloat    width)
{
    g_return_if_fail (LRG_IS_THEME (self));

    if (self->border_width == width)
        return;

    self->border_width = width;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_BORDER_WIDTH]);
}

/**
 * lrg_theme_get_corner_radius:
 * @self: an #LrgTheme
 *
 * Gets the default corner radius.
 *
 * Returns: The corner radius
 */
gfloat
lrg_theme_get_corner_radius (LrgTheme *self)
{
    g_return_val_if_fail (LRG_IS_THEME (self), DEFAULT_CORNER_RADIUS);
    return self->corner_radius;
}

/**
 * lrg_theme_set_corner_radius:
 * @self: an #LrgTheme
 * @radius: the corner radius
 *
 * Sets the default corner radius.
 */
void
lrg_theme_set_corner_radius (LrgTheme *self,
                             gfloat    radius)
{
    g_return_if_fail (LRG_IS_THEME (self));

    if (self->corner_radius == radius)
        return;

    self->corner_radius = radius;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_CORNER_RADIUS]);
}
