/* lrg-nine-slice.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Nine-slice (9-patch) rendering for UI elements.
 */

#include <gio/gio.h>

#include "lrg-nine-slice.h"

#include <math.h>
#include <yaml-glib.h>

struct _LrgNineSlice
{
    GObject parent_instance;

    gchar           *name;
    LrgAtlasRegion  *source_region;
    LrgNineSliceMode mode;

    /* Border sizes in pixels */
    gint border_left;
    gint border_right;
    gint border_top;
    gint border_bottom;
};

G_DEFINE_FINAL_TYPE (LrgNineSlice, lrg_nine_slice, G_TYPE_OBJECT)

enum
{
    PROP_0,
    PROP_NAME,
    PROP_MODE,
    PROP_BORDER_LEFT,
    PROP_BORDER_RIGHT,
    PROP_BORDER_TOP,
    PROP_BORDER_BOTTOM,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

static void
lrg_nine_slice_finalize (GObject *object)
{
    LrgNineSlice *self = LRG_NINE_SLICE (object);

    g_clear_pointer (&self->name, g_free);
    g_clear_pointer (&self->source_region, lrg_atlas_region_free);

    G_OBJECT_CLASS (lrg_nine_slice_parent_class)->finalize (object);
}

static void
lrg_nine_slice_get_property (GObject    *object,
                             guint       prop_id,
                             GValue     *value,
                             GParamSpec *pspec)
{
    LrgNineSlice *self = LRG_NINE_SLICE (object);

    switch (prop_id)
    {
    case PROP_NAME:
        g_value_set_string (value, self->name);
        break;
    case PROP_MODE:
        g_value_set_int (value, self->mode);
        break;
    case PROP_BORDER_LEFT:
        g_value_set_int (value, self->border_left);
        break;
    case PROP_BORDER_RIGHT:
        g_value_set_int (value, self->border_right);
        break;
    case PROP_BORDER_TOP:
        g_value_set_int (value, self->border_top);
        break;
    case PROP_BORDER_BOTTOM:
        g_value_set_int (value, self->border_bottom);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_nine_slice_set_property (GObject      *object,
                             guint         prop_id,
                             const GValue *value,
                             GParamSpec   *pspec)
{
    LrgNineSlice *self = LRG_NINE_SLICE (object);

    switch (prop_id)
    {
    case PROP_NAME:
        g_clear_pointer (&self->name, g_free);
        self->name = g_value_dup_string (value);
        break;
    case PROP_MODE:
        self->mode = g_value_get_int (value);
        break;
    case PROP_BORDER_LEFT:
        self->border_left = g_value_get_int (value);
        break;
    case PROP_BORDER_RIGHT:
        self->border_right = g_value_get_int (value);
        break;
    case PROP_BORDER_TOP:
        self->border_top = g_value_get_int (value);
        break;
    case PROP_BORDER_BOTTOM:
        self->border_bottom = g_value_get_int (value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_nine_slice_class_init (LrgNineSliceClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = lrg_nine_slice_finalize;
    object_class->get_property = lrg_nine_slice_get_property;
    object_class->set_property = lrg_nine_slice_set_property;

    /**
     * LrgNineSlice:name:
     *
     * The name identifier of the nine-slice.
     *
     * Since: 1.0
     */
    properties[PROP_NAME] =
        g_param_spec_string ("name", NULL, NULL,
                             NULL,
                             G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgNineSlice:mode:
     *
     * The fill mode for center and edge regions.
     *
     * Since: 1.0
     */
    properties[PROP_MODE] =
        g_param_spec_int ("mode", NULL, NULL,
                          LRG_NINE_SLICE_MODE_STRETCH,
                          LRG_NINE_SLICE_MODE_TILE_FIT,
                          LRG_NINE_SLICE_MODE_STRETCH,
                          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgNineSlice:border-left:
     *
     * Left border width in pixels.
     *
     * Since: 1.0
     */
    properties[PROP_BORDER_LEFT] =
        g_param_spec_int ("border-left", NULL, NULL,
                          0, G_MAXINT, 0,
                          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgNineSlice:border-right:
     *
     * Right border width in pixels.
     *
     * Since: 1.0
     */
    properties[PROP_BORDER_RIGHT] =
        g_param_spec_int ("border-right", NULL, NULL,
                          0, G_MAXINT, 0,
                          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgNineSlice:border-top:
     *
     * Top border height in pixels.
     *
     * Since: 1.0
     */
    properties[PROP_BORDER_TOP] =
        g_param_spec_int ("border-top", NULL, NULL,
                          0, G_MAXINT, 0,
                          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgNineSlice:border-bottom:
     *
     * Bottom border height in pixels.
     *
     * Since: 1.0
     */
    properties[PROP_BORDER_BOTTOM] =
        g_param_spec_int ("border-bottom", NULL, NULL,
                          0, G_MAXINT, 0,
                          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_nine_slice_init (LrgNineSlice *self)
{
    self->mode = LRG_NINE_SLICE_MODE_STRETCH;
}

/**
 * lrg_nine_slice_new:
 * @name: Name identifier for the nine-slice
 *
 * Creates a new nine-slice without any source region.
 *
 * Returns: (transfer full): A new #LrgNineSlice
 *
 * Since: 1.0
 */
LrgNineSlice *
lrg_nine_slice_new (const gchar *name)
{
    return g_object_new (LRG_TYPE_NINE_SLICE,
                         "name", name,
                         NULL);
}

/**
 * lrg_nine_slice_new_from_region:
 * @name: Name identifier
 * @region: Source atlas region
 * @left: Left border width in pixels
 * @right: Right border width in pixels
 * @top: Top border height in pixels
 * @bottom: Bottom border height in pixels
 *
 * Creates a nine-slice from an atlas region with specified borders.
 *
 * Returns: (transfer full): A new #LrgNineSlice
 *
 * Since: 1.0
 */
LrgNineSlice *
lrg_nine_slice_new_from_region (const gchar          *name,
                                const LrgAtlasRegion *region,
                                gint                  left,
                                gint                  right,
                                gint                  top,
                                gint                  bottom)
{
    LrgNineSlice *self;

    self = lrg_nine_slice_new (name);

    if (region != NULL)
    {
        self->source_region = lrg_atlas_region_copy (region);
    }

    self->border_left = left;
    self->border_right = right;
    self->border_top = top;
    self->border_bottom = bottom;

    return self;
}

/**
 * lrg_nine_slice_new_from_file:
 * @path: Path to the nine-slice definition file (YAML)
 * @error: (nullable): Return location for error
 *
 * Creates a nine-slice by loading a definition file.
 *
 * Returns: (transfer full) (nullable): A new #LrgNineSlice or %NULL on error
 *
 * Since: 1.0
 */
LrgNineSlice *
lrg_nine_slice_new_from_file (const gchar  *path,
                              GError      **error)
{
    g_autoptr(YamlParser) parser = NULL;
    YamlNode *root;
    YamlMapping *mapping;
    YamlMapping *region_map;
    const gchar *name;
    const gchar *mode_str;
    LrgNineSliceMode mode;
    LrgAtlasRegion *region;
    LrgNineSlice *self;

    g_return_val_if_fail (path != NULL, NULL);

    /* Parse the YAML file */
    parser = yaml_parser_new ();
    if (!yaml_parser_load_from_file (parser, path, error))
        return NULL;

    root = yaml_parser_get_root (parser);
    if (root == NULL)
    {
        g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                     "Empty nine-slice file: %s", path);
        return NULL;
    }

    mapping = yaml_node_get_mapping (root);
    if (mapping == NULL)
    {
        g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                     "Nine-slice root must be a mapping: %s", path);
        return NULL;
    }

    /* Get required 'name' field */
    name = yaml_mapping_get_string_member (mapping, "name");
    if (name == NULL)
    {
        g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                     "Nine-slice missing 'name' field: %s", path);
        return NULL;
    }

    /* Parse mode (defaults to stretch) */
    mode = LRG_NINE_SLICE_MODE_STRETCH;
    mode_str = yaml_mapping_get_string_member (mapping, "mode");
    if (mode_str != NULL)
    {
        if (g_strcmp0 (mode_str, "tile") == 0)
            mode = LRG_NINE_SLICE_MODE_TILE;
        else if (g_strcmp0 (mode_str, "tile_fit") == 0)
            mode = LRG_NINE_SLICE_MODE_TILE_FIT;
    }

    /* Parse source_region if present */
    region = NULL;
    if (yaml_mapping_has_member (mapping, "source_region"))
    {
        const gchar *region_name;
        gint rx, ry, rw, rh;

        region_map = yaml_mapping_get_mapping_member (mapping, "source_region");
        if (region_map != NULL)
        {
            region_name = yaml_mapping_get_string_member (region_map, "name");
            rx = (gint) yaml_mapping_get_int_member (region_map, "x");
            ry = (gint) yaml_mapping_get_int_member (region_map, "y");
            rw = (gint) yaml_mapping_get_int_member (region_map, "width");
            rh = (gint) yaml_mapping_get_int_member (region_map, "height");

            region = lrg_atlas_region_new (region_name, rx, ry, rw, rh);
        }
    }

    /* Create the nine-slice */
    self = lrg_nine_slice_new_from_region (
        name,
        region,
        (gint) yaml_mapping_get_int_member (mapping, "border_left"),
        (gint) yaml_mapping_get_int_member (mapping, "border_right"),
        (gint) yaml_mapping_get_int_member (mapping, "border_top"),
        (gint) yaml_mapping_get_int_member (mapping, "border_bottom")
    );

    lrg_nine_slice_set_mode (self, mode);

    if (region != NULL)
        lrg_atlas_region_free (region);

    return self;
}

/**
 * lrg_nine_slice_get_name:
 * @self: A #LrgNineSlice
 *
 * Gets the name of the nine-slice.
 *
 * Returns: (transfer none) (nullable): The name
 *
 * Since: 1.0
 */
const gchar *
lrg_nine_slice_get_name (LrgNineSlice *self)
{
    g_return_val_if_fail (LRG_IS_NINE_SLICE (self), NULL);
    return self->name;
}

/**
 * lrg_nine_slice_get_source_region:
 * @self: A #LrgNineSlice
 *
 * Gets the source atlas region.
 *
 * Returns: (transfer none) (nullable): The source region
 *
 * Since: 1.0
 */
const LrgAtlasRegion *
lrg_nine_slice_get_source_region (LrgNineSlice *self)
{
    g_return_val_if_fail (LRG_IS_NINE_SLICE (self), NULL);
    return self->source_region;
}

/**
 * lrg_nine_slice_set_source_region:
 * @self: A #LrgNineSlice
 * @region: The source region (copied)
 *
 * Sets the source atlas region.
 *
 * Since: 1.0
 */
void
lrg_nine_slice_set_source_region (LrgNineSlice         *self,
                                  const LrgAtlasRegion *region)
{
    g_return_if_fail (LRG_IS_NINE_SLICE (self));

    g_clear_pointer (&self->source_region, lrg_atlas_region_free);

    if (region != NULL)
    {
        self->source_region = lrg_atlas_region_copy (region);
    }
}

/**
 * lrg_nine_slice_get_mode:
 * @self: A #LrgNineSlice
 *
 * Gets the center/edge fill mode.
 *
 * Returns: The fill mode
 *
 * Since: 1.0
 */
LrgNineSliceMode
lrg_nine_slice_get_mode (LrgNineSlice *self)
{
    g_return_val_if_fail (LRG_IS_NINE_SLICE (self), LRG_NINE_SLICE_MODE_STRETCH);
    return self->mode;
}

/**
 * lrg_nine_slice_set_mode:
 * @self: A #LrgNineSlice
 * @mode: The fill mode
 *
 * Sets the center/edge fill mode.
 *
 * Since: 1.0
 */
void
lrg_nine_slice_set_mode (LrgNineSlice    *self,
                         LrgNineSliceMode mode)
{
    g_return_if_fail (LRG_IS_NINE_SLICE (self));

    self->mode = mode;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MODE]);
}

/**
 * lrg_nine_slice_get_border_left:
 * @self: A #LrgNineSlice
 *
 * Gets the left border width.
 *
 * Returns: Left border in pixels
 *
 * Since: 1.0
 */
gint
lrg_nine_slice_get_border_left (LrgNineSlice *self)
{
    g_return_val_if_fail (LRG_IS_NINE_SLICE (self), 0);
    return self->border_left;
}

/**
 * lrg_nine_slice_get_border_right:
 * @self: A #LrgNineSlice
 *
 * Gets the right border width.
 *
 * Returns: Right border in pixels
 *
 * Since: 1.0
 */
gint
lrg_nine_slice_get_border_right (LrgNineSlice *self)
{
    g_return_val_if_fail (LRG_IS_NINE_SLICE (self), 0);
    return self->border_right;
}

/**
 * lrg_nine_slice_get_border_top:
 * @self: A #LrgNineSlice
 *
 * Gets the top border height.
 *
 * Returns: Top border in pixels
 *
 * Since: 1.0
 */
gint
lrg_nine_slice_get_border_top (LrgNineSlice *self)
{
    g_return_val_if_fail (LRG_IS_NINE_SLICE (self), 0);
    return self->border_top;
}

/**
 * lrg_nine_slice_get_border_bottom:
 * @self: A #LrgNineSlice
 *
 * Gets the bottom border height.
 *
 * Returns: Bottom border in pixels
 *
 * Since: 1.0
 */
gint
lrg_nine_slice_get_border_bottom (LrgNineSlice *self)
{
    g_return_val_if_fail (LRG_IS_NINE_SLICE (self), 0);
    return self->border_bottom;
}

/**
 * lrg_nine_slice_get_borders:
 * @self: A #LrgNineSlice
 * @out_left: (out) (nullable): Return location for left border
 * @out_right: (out) (nullable): Return location for right border
 * @out_top: (out) (nullable): Return location for top border
 * @out_bottom: (out) (nullable): Return location for bottom border
 *
 * Gets all border values.
 *
 * Since: 1.0
 */
void
lrg_nine_slice_get_borders (LrgNineSlice *self,
                            gint         *out_left,
                            gint         *out_right,
                            gint         *out_top,
                            gint         *out_bottom)
{
    g_return_if_fail (LRG_IS_NINE_SLICE (self));

    if (out_left != NULL)
        *out_left = self->border_left;
    if (out_right != NULL)
        *out_right = self->border_right;
    if (out_top != NULL)
        *out_top = self->border_top;
    if (out_bottom != NULL)
        *out_bottom = self->border_bottom;
}

/**
 * lrg_nine_slice_set_borders:
 * @self: A #LrgNineSlice
 * @left: Left border width
 * @right: Right border width
 * @top: Top border height
 * @bottom: Bottom border height
 *
 * Sets all border values.
 *
 * Since: 1.0
 */
void
lrg_nine_slice_set_borders (LrgNineSlice *self,
                            gint          left,
                            gint          right,
                            gint          top,
                            gint          bottom)
{
    g_return_if_fail (LRG_IS_NINE_SLICE (self));

    g_object_freeze_notify (G_OBJECT (self));

    self->border_left = left;
    self->border_right = right;
    self->border_top = top;
    self->border_bottom = bottom;

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_BORDER_LEFT]);
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_BORDER_RIGHT]);
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_BORDER_TOP]);
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_BORDER_BOTTOM]);

    g_object_thaw_notify (G_OBJECT (self));
}

/**
 * lrg_nine_slice_set_uniform_border:
 * @self: A #LrgNineSlice
 * @border: Border size for all sides
 *
 * Sets all borders to the same value.
 *
 * Since: 1.0
 */
void
lrg_nine_slice_set_uniform_border (LrgNineSlice *self,
                                   gint          border)
{
    lrg_nine_slice_set_borders (self, border, border, border, border);
}

/**
 * lrg_nine_slice_get_min_width:
 * @self: A #LrgNineSlice
 *
 * Gets the minimum width (left + right borders).
 *
 * Returns: Minimum width in pixels
 *
 * Since: 1.0
 */
gint
lrg_nine_slice_get_min_width (LrgNineSlice *self)
{
    g_return_val_if_fail (LRG_IS_NINE_SLICE (self), 0);
    return self->border_left + self->border_right;
}

/**
 * lrg_nine_slice_get_min_height:
 * @self: A #LrgNineSlice
 *
 * Gets the minimum height (top + bottom borders).
 *
 * Returns: Minimum height in pixels
 *
 * Since: 1.0
 */
gint
lrg_nine_slice_get_min_height (LrgNineSlice *self)
{
    g_return_val_if_fail (LRG_IS_NINE_SLICE (self), 0);
    return self->border_top + self->border_bottom;
}

/**
 * lrg_nine_slice_get_center_width:
 * @self: A #LrgNineSlice
 *
 * Gets the width of the stretchable center region.
 *
 * Returns: Center width in pixels
 *
 * Since: 1.0
 */
gint
lrg_nine_slice_get_center_width (LrgNineSlice *self)
{
    gint source_width;

    g_return_val_if_fail (LRG_IS_NINE_SLICE (self), 0);

    if (self->source_region == NULL)
        return 0;

    source_width = lrg_atlas_region_get_width (self->source_region);
    return source_width - self->border_left - self->border_right;
}

/**
 * lrg_nine_slice_get_center_height:
 * @self: A #LrgNineSlice
 *
 * Gets the height of the stretchable center region.
 *
 * Returns: Center height in pixels
 *
 * Since: 1.0
 */
gint
lrg_nine_slice_get_center_height (LrgNineSlice *self)
{
    gint source_height;

    g_return_val_if_fail (LRG_IS_NINE_SLICE (self), 0);

    if (self->source_region == NULL)
        return 0;

    source_height = lrg_atlas_region_get_height (self->source_region);
    return source_height - self->border_top - self->border_bottom;
}

/**
 * lrg_nine_slice_get_patch_rect:
 * @self: A #LrgNineSlice
 * @patch: Which patch to get
 * @out_x: (out) (nullable): Return location for X position
 * @out_y: (out) (nullable): Return location for Y position
 * @out_width: (out) (nullable): Return location for width
 * @out_height: (out) (nullable): Return location for height
 *
 * Gets the source rectangle for a specific patch within the source region.
 *
 * Returns: %TRUE if the patch rectangle was retrieved
 *
 * Since: 1.0
 */
gboolean
lrg_nine_slice_get_patch_rect (LrgNineSlice     *self,
                               LrgNineSlicePatch patch,
                               gint             *out_x,
                               gint             *out_y,
                               gint             *out_width,
                               gint             *out_height)
{
    gint src_x, src_y, src_w, src_h;
    gint x, y, w, h;
    gint center_w, center_h;

    g_return_val_if_fail (LRG_IS_NINE_SLICE (self), FALSE);

    if (self->source_region == NULL)
        return FALSE;

    lrg_atlas_region_get_rect (self->source_region, &src_x, &src_y, &src_w, &src_h);

    center_w = src_w - self->border_left - self->border_right;
    center_h = src_h - self->border_top - self->border_bottom;

    switch (patch)
    {
    case LRG_NINE_SLICE_PATCH_TOP_LEFT:
        x = src_x;
        y = src_y;
        w = self->border_left;
        h = self->border_top;
        break;
    case LRG_NINE_SLICE_PATCH_TOP:
        x = src_x + self->border_left;
        y = src_y;
        w = center_w;
        h = self->border_top;
        break;
    case LRG_NINE_SLICE_PATCH_TOP_RIGHT:
        x = src_x + self->border_left + center_w;
        y = src_y;
        w = self->border_right;
        h = self->border_top;
        break;
    case LRG_NINE_SLICE_PATCH_LEFT:
        x = src_x;
        y = src_y + self->border_top;
        w = self->border_left;
        h = center_h;
        break;
    case LRG_NINE_SLICE_PATCH_CENTER:
        x = src_x + self->border_left;
        y = src_y + self->border_top;
        w = center_w;
        h = center_h;
        break;
    case LRG_NINE_SLICE_PATCH_RIGHT:
        x = src_x + self->border_left + center_w;
        y = src_y + self->border_top;
        w = self->border_right;
        h = center_h;
        break;
    case LRG_NINE_SLICE_PATCH_BOTTOM_LEFT:
        x = src_x;
        y = src_y + self->border_top + center_h;
        w = self->border_left;
        h = self->border_bottom;
        break;
    case LRG_NINE_SLICE_PATCH_BOTTOM:
        x = src_x + self->border_left;
        y = src_y + self->border_top + center_h;
        w = center_w;
        h = self->border_bottom;
        break;
    case LRG_NINE_SLICE_PATCH_BOTTOM_RIGHT:
        x = src_x + self->border_left + center_w;
        y = src_y + self->border_top + center_h;
        w = self->border_right;
        h = self->border_bottom;
        break;
    default:
        return FALSE;
    }

    if (out_x != NULL) *out_x = x;
    if (out_y != NULL) *out_y = y;
    if (out_width != NULL) *out_width = w;
    if (out_height != NULL) *out_height = h;

    return TRUE;
}

/**
 * lrg_nine_slice_get_patch_region:
 * @self: A #LrgNineSlice
 * @patch: Which patch to get
 *
 * Gets the atlas region for a specific patch.
 * The returned region has correct UV coordinates for the patch.
 *
 * Returns: (transfer full) (nullable): A new region for the patch, or %NULL if invalid
 *
 * Since: 1.0
 */
LrgAtlasRegion *
lrg_nine_slice_get_patch_region (LrgNineSlice     *self,
                                 LrgNineSlicePatch patch)
{
    gint x, y, w, h;
    gfloat src_u1, src_v1, src_u2, src_v2;
    gfloat u1, v1, u2, v2;
    gint src_w, src_h;
    g_autofree gchar *name = NULL;
    LrgAtlasRegion *region;

    g_return_val_if_fail (LRG_IS_NINE_SLICE (self), NULL);

    if (!lrg_nine_slice_get_patch_rect (self, patch, &x, &y, &w, &h))
        return NULL;

    /* Calculate UV coordinates based on source region UVs */
    lrg_atlas_region_get_uv (self->source_region, &src_u1, &src_v1, &src_u2, &src_v2);
    src_w = lrg_atlas_region_get_width (self->source_region);
    src_h = lrg_atlas_region_get_height (self->source_region);

    if (src_w <= 0 || src_h <= 0)
        return NULL;

    {
        gint src_x, src_y;
        gfloat u_scale, v_scale;

        src_x = lrg_atlas_region_get_x (self->source_region);
        src_y = lrg_atlas_region_get_y (self->source_region);

        u_scale = (src_u2 - src_u1) / (gfloat)src_w;
        v_scale = (src_v2 - src_v1) / (gfloat)src_h;

        u1 = src_u1 + (x - src_x) * u_scale;
        v1 = src_v1 + (y - src_y) * v_scale;
        u2 = u1 + w * u_scale;
        v2 = v1 + h * v_scale;
    }

    name = g_strdup_printf ("%s_patch_%d",
                            self->name ? self->name : "nine_slice",
                            patch);

    region = lrg_atlas_region_new_with_uv (name, x, y, w, h, u1, v1, u2, v2);

    return region;
}

/**
 * lrg_nine_slice_calculate_dest_rects:
 * @self: A #LrgNineSlice
 * @dest_x: Destination X position
 * @dest_y: Destination Y position
 * @dest_width: Destination width
 * @dest_height: Destination height
 * @out_rects: (array fixed-size=9) (out): Array of 9 destination rectangles (x,y,w,h for each)
 *
 * Calculates destination rectangles for rendering all 9 patches.
 * Each rectangle is 4 floats: x, y, width, height (36 floats total).
 * The order matches #LrgNineSlicePatch enumeration.
 *
 * Since: 1.0
 */
void
lrg_nine_slice_calculate_dest_rects (LrgNineSlice *self,
                                     gfloat        dest_x,
                                     gfloat        dest_y,
                                     gfloat        dest_width,
                                     gfloat        dest_height,
                                     gfloat       *out_rects)
{
    gfloat left, right, top, bottom;
    gfloat center_w, center_h;
    gfloat x_left, x_center, x_right;
    gfloat y_top, y_center, y_bottom;

    g_return_if_fail (LRG_IS_NINE_SLICE (self));
    g_return_if_fail (out_rects != NULL);

    left = (gfloat)self->border_left;
    right = (gfloat)self->border_right;
    top = (gfloat)self->border_top;
    bottom = (gfloat)self->border_bottom;

    /* Clamp borders if destination is smaller than minimum */
    if (dest_width < left + right)
    {
        gfloat scale = dest_width / (left + right);
        left *= scale;
        right *= scale;
    }

    if (dest_height < top + bottom)
    {
        gfloat scale = dest_height / (top + bottom);
        top *= scale;
        bottom *= scale;
    }

    center_w = dest_width - left - right;
    center_h = dest_height - top - bottom;

    x_left = dest_x;
    x_center = dest_x + left;
    x_right = dest_x + left + center_w;

    y_top = dest_y;
    y_center = dest_y + top;
    y_bottom = dest_y + top + center_h;

    /* TOP_LEFT */
    out_rects[0] = x_left;
    out_rects[1] = y_top;
    out_rects[2] = left;
    out_rects[3] = top;

    /* TOP */
    out_rects[4] = x_center;
    out_rects[5] = y_top;
    out_rects[6] = center_w;
    out_rects[7] = top;

    /* TOP_RIGHT */
    out_rects[8] = x_right;
    out_rects[9] = y_top;
    out_rects[10] = right;
    out_rects[11] = top;

    /* LEFT */
    out_rects[12] = x_left;
    out_rects[13] = y_center;
    out_rects[14] = left;
    out_rects[15] = center_h;

    /* CENTER */
    out_rects[16] = x_center;
    out_rects[17] = y_center;
    out_rects[18] = center_w;
    out_rects[19] = center_h;

    /* RIGHT */
    out_rects[20] = x_right;
    out_rects[21] = y_center;
    out_rects[22] = right;
    out_rects[23] = center_h;

    /* BOTTOM_LEFT */
    out_rects[24] = x_left;
    out_rects[25] = y_bottom;
    out_rects[26] = left;
    out_rects[27] = bottom;

    /* BOTTOM */
    out_rects[28] = x_center;
    out_rects[29] = y_bottom;
    out_rects[30] = center_w;
    out_rects[31] = bottom;

    /* BOTTOM_RIGHT */
    out_rects[32] = x_right;
    out_rects[33] = y_bottom;
    out_rects[34] = right;
    out_rects[35] = bottom;
}

/**
 * lrg_nine_slice_calculate_tile_count:
 * @self: A #LrgNineSlice
 * @dest_width: Destination width
 * @dest_height: Destination height
 * @out_h_tiles: (out) (nullable): Return location for horizontal tile count
 * @out_v_tiles: (out) (nullable): Return location for vertical tile count
 *
 * Calculates how many tiles are needed for tiling modes.
 * Only applicable when mode is TILE or TILE_FIT.
 *
 * Since: 1.0
 */
void
lrg_nine_slice_calculate_tile_count (LrgNineSlice *self,
                                     gfloat        dest_width,
                                     gfloat        dest_height,
                                     guint        *out_h_tiles,
                                     guint        *out_v_tiles)
{
    gint center_w, center_h;
    gfloat stretch_w, stretch_h;

    g_return_if_fail (LRG_IS_NINE_SLICE (self));

    center_w = lrg_nine_slice_get_center_width (self);
    center_h = lrg_nine_slice_get_center_height (self);

    if (center_w <= 0 || center_h <= 0)
    {
        if (out_h_tiles != NULL) *out_h_tiles = 0;
        if (out_v_tiles != NULL) *out_v_tiles = 0;
        return;
    }

    stretch_w = dest_width - self->border_left - self->border_right;
    stretch_h = dest_height - self->border_top - self->border_bottom;

    if (out_h_tiles != NULL)
    {
        *out_h_tiles = (guint)ceilf (stretch_w / (gfloat)center_w);
    }

    if (out_v_tiles != NULL)
    {
        *out_v_tiles = (guint)ceilf (stretch_h / (gfloat)center_h);
    }
}

/**
 * lrg_nine_slice_save_to_file:
 * @self: A #LrgNineSlice
 * @path: Path to save to
 * @error: (nullable): Return location for error
 *
 * Saves the nine-slice definition to a YAML file.
 *
 * Returns: %TRUE on success
 *
 * Since: 1.0
 */
gboolean
lrg_nine_slice_save_to_file (LrgNineSlice  *self,
                             const gchar   *path,
                             GError       **error)
{
    g_autoptr(YamlBuilder) builder = NULL;
    g_autoptr(YamlGenerator) generator = NULL;
    YamlDocument *doc;
    gchar *yaml_str;
    gboolean ret;

    g_return_val_if_fail (LRG_IS_NINE_SLICE (self), FALSE);
    g_return_val_if_fail (path != NULL, FALSE);

    builder = yaml_builder_new ();

    /* Build root mapping */
    yaml_builder_begin_mapping (builder);

    yaml_builder_set_member_name (builder, "name");
    yaml_builder_add_string_value (builder, self->name != NULL ? self->name : "");

    /* Mode */
    yaml_builder_set_member_name (builder, "mode");
    switch (self->mode)
    {
    case LRG_NINE_SLICE_MODE_TILE:
        yaml_builder_add_string_value (builder, "tile");
        break;
    case LRG_NINE_SLICE_MODE_TILE_FIT:
        yaml_builder_add_string_value (builder, "tile_fit");
        break;
    default:
        yaml_builder_add_string_value (builder, "stretch");
        break;
    }

    /* Borders */
    yaml_builder_set_member_name (builder, "border_left");
    yaml_builder_add_int_value (builder, self->border_left);

    yaml_builder_set_member_name (builder, "border_right");
    yaml_builder_add_int_value (builder, self->border_right);

    yaml_builder_set_member_name (builder, "border_top");
    yaml_builder_add_int_value (builder, self->border_top);

    yaml_builder_set_member_name (builder, "border_bottom");
    yaml_builder_add_int_value (builder, self->border_bottom);

    /* Source region */
    if (self->source_region != NULL)
    {
        yaml_builder_set_member_name (builder, "source_region");
        yaml_builder_begin_mapping (builder);

        yaml_builder_set_member_name (builder, "name");
        yaml_builder_add_string_value (builder,
            lrg_atlas_region_get_name (self->source_region));

        yaml_builder_set_member_name (builder, "x");
        yaml_builder_add_int_value (builder,
            lrg_atlas_region_get_x (self->source_region));

        yaml_builder_set_member_name (builder, "y");
        yaml_builder_add_int_value (builder,
            lrg_atlas_region_get_y (self->source_region));

        yaml_builder_set_member_name (builder, "width");
        yaml_builder_add_int_value (builder,
            lrg_atlas_region_get_width (self->source_region));

        yaml_builder_set_member_name (builder, "height");
        yaml_builder_add_int_value (builder,
            lrg_atlas_region_get_height (self->source_region));

        yaml_builder_end_mapping (builder);  /* source_region */
    }

    yaml_builder_end_mapping (builder);  /* root */

    doc = yaml_builder_get_document (builder);
    if (doc == NULL)
    {
        g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                     "Failed to build YAML document for nine-slice");
        return FALSE;
    }

    generator = yaml_generator_new ();
    yaml_generator_set_document (generator, doc);
    yaml_str = yaml_generator_to_data (generator, NULL, error);

    if (yaml_str == NULL)
        return FALSE;

    ret = g_file_set_contents (path, yaml_str, -1, error);
    g_free (yaml_str);

    return ret;
}
