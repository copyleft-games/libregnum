/* lrg-grid.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Grid layout container.
 */

#include "config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_UI

#include "lrg-grid.h"
#include "../lrg-log.h"

struct _LrgGrid
{
    LrgContainer  parent_instance;
    guint         columns;
    gfloat        column_spacing;
    gfloat        row_spacing;
};

G_DEFINE_TYPE (LrgGrid, lrg_grid, LRG_TYPE_CONTAINER)

enum
{
    PROP_0,
    PROP_COLUMNS,
    PROP_COLUMN_SPACING,
    PROP_ROW_SPACING,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

static void
lrg_grid_layout_children (LrgContainer *container)
{
    LrgGrid *self = LRG_GRID (container);
    GList   *children;
    GList   *l;
    gfloat   padding;
    gfloat   container_width;
    gfloat   container_height;
    guint    visible_count = 0;
    guint    current_col = 0;
    guint    current_row = 0;
    gfloat   cell_width;
    gfloat   cell_height;
    guint    num_rows;
    gfloat   available_width;
    gfloat   available_height;

    children = lrg_container_get_children (container);
    padding = lrg_container_get_padding (container);
    container_width = lrg_widget_get_width (LRG_WIDGET (container));
    container_height = lrg_widget_get_height (LRG_WIDGET (container));

    /* Count visible children */
    for (l = children; l != NULL; l = l->next)
    {
        if (lrg_widget_get_visible (LRG_WIDGET (l->data)))
        {
            visible_count++;
        }
    }

    if (visible_count == 0 || self->columns == 0)
    {
        return;
    }

    /* Calculate number of rows needed */
    num_rows = (visible_count + self->columns - 1) / self->columns;

    /* Calculate cell dimensions */
    available_width = container_width - padding * 2 -
                      self->column_spacing * (self->columns - 1);
    available_height = container_height - padding * 2 -
                       self->row_spacing * (num_rows - 1);

    cell_width = available_width / self->columns;
    cell_height = (num_rows > 0) ? available_height / num_rows : available_height;

    /* Position each visible child */
    for (l = children; l != NULL; l = l->next)
    {
        LrgWidget *child = LRG_WIDGET (l->data);
        gfloat     x, y;

        if (!lrg_widget_get_visible (child))
        {
            continue;
        }

        x = padding + current_col * (cell_width + self->column_spacing);
        y = padding + current_row * (cell_height + self->row_spacing);

        lrg_widget_set_position (child, x, y);
        lrg_widget_set_size (child, cell_width, cell_height);

        /* Advance to next cell */
        current_col++;
        if (current_col >= self->columns)
        {
            current_col = 0;
            current_row++;
        }
    }
}

static void
lrg_grid_measure (LrgWidget *widget,
                  gfloat    *preferred_width,
                  gfloat    *preferred_height)
{
    LrgGrid      *self = LRG_GRID (widget);
    LrgContainer *container = LRG_CONTAINER (widget);
    GList        *children;
    GList        *l;
    gfloat        max_child_width = 0.0f;
    gfloat        max_child_height = 0.0f;
    gfloat        padding;
    guint         visible_count = 0;
    guint         num_rows;

    children = lrg_container_get_children (container);
    padding = lrg_container_get_padding (container);

    /* Find maximum child dimensions */
    for (l = children; l != NULL; l = l->next)
    {
        LrgWidget *child = LRG_WIDGET (l->data);
        gfloat     child_width, child_height;

        if (!lrg_widget_get_visible (child))
        {
            continue;
        }

        lrg_widget_measure (child, &child_width, &child_height);

        if (child_width > max_child_width)
        {
            max_child_width = child_width;
        }
        if (child_height > max_child_height)
        {
            max_child_height = child_height;
        }
        visible_count++;
    }

    if (visible_count == 0 || self->columns == 0)
    {
        if (preferred_width != NULL)
        {
            *preferred_width = padding * 2;
        }
        if (preferred_height != NULL)
        {
            *preferred_height = padding * 2;
        }
        return;
    }

    /* Calculate number of rows */
    num_rows = (visible_count + self->columns - 1) / self->columns;

    if (preferred_width != NULL)
    {
        *preferred_width = max_child_width * self->columns +
                           self->column_spacing * (self->columns - 1) +
                           padding * 2;
    }
    if (preferred_height != NULL)
    {
        *preferred_height = max_child_height * num_rows +
                            self->row_spacing * (num_rows - 1) +
                            padding * 2;
    }
}

static void
lrg_grid_get_property (GObject    *object,
                       guint       prop_id,
                       GValue     *value,
                       GParamSpec *pspec)
{
    LrgGrid *self = LRG_GRID (object);

    switch (prop_id)
    {
    case PROP_COLUMNS:
        g_value_set_uint (value, self->columns);
        break;
    case PROP_COLUMN_SPACING:
        g_value_set_float (value, self->column_spacing);
        break;
    case PROP_ROW_SPACING:
        g_value_set_float (value, self->row_spacing);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_grid_set_property (GObject      *object,
                       guint         prop_id,
                       const GValue *value,
                       GParamSpec   *pspec)
{
    LrgGrid *self = LRG_GRID (object);

    switch (prop_id)
    {
    case PROP_COLUMNS:
        lrg_grid_set_columns (self, g_value_get_uint (value));
        break;
    case PROP_COLUMN_SPACING:
        lrg_grid_set_column_spacing (self, g_value_get_float (value));
        break;
    case PROP_ROW_SPACING:
        lrg_grid_set_row_spacing (self, g_value_get_float (value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_grid_class_init (LrgGridClass *klass)
{
    GObjectClass      *object_class = G_OBJECT_CLASS (klass);
    LrgWidgetClass    *widget_class = LRG_WIDGET_CLASS (klass);
    LrgContainerClass *container_class = LRG_CONTAINER_CLASS (klass);

    object_class->get_property = lrg_grid_get_property;
    object_class->set_property = lrg_grid_set_property;

    widget_class->measure = lrg_grid_measure;
    container_class->layout_children = lrg_grid_layout_children;

    /**
     * LrgGrid:columns:
     *
     * The number of columns in the grid.
     */
    properties[PROP_COLUMNS] =
        g_param_spec_uint ("columns",
                           "Columns",
                           "Number of columns in the grid",
                           1, G_MAXUINT, 1,
                           G_PARAM_READWRITE |
                           G_PARAM_STATIC_STRINGS |
                           G_PARAM_EXPLICIT_NOTIFY |
                           G_PARAM_CONSTRUCT);

    /**
     * LrgGrid:column-spacing:
     *
     * The horizontal spacing between columns.
     */
    properties[PROP_COLUMN_SPACING] =
        g_param_spec_float ("column-spacing",
                            "Column Spacing",
                            "Horizontal spacing between columns",
                            0.0f, G_MAXFLOAT, 0.0f,
                            G_PARAM_READWRITE |
                            G_PARAM_STATIC_STRINGS |
                            G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgGrid:row-spacing:
     *
     * The vertical spacing between rows.
     */
    properties[PROP_ROW_SPACING] =
        g_param_spec_float ("row-spacing",
                            "Row Spacing",
                            "Vertical spacing between rows",
                            0.0f, G_MAXFLOAT, 0.0f,
                            G_PARAM_READWRITE |
                            G_PARAM_STATIC_STRINGS |
                            G_PARAM_EXPLICIT_NOTIFY);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_grid_init (LrgGrid *self)
{
    self->columns = 1;
    self->column_spacing = 0.0f;
    self->row_spacing = 0.0f;
}

/**
 * lrg_grid_new:
 * @columns: number of columns
 *
 * Creates a new grid container with the specified number of columns.
 * Children are arranged left-to-right, top-to-bottom, wrapping to
 * a new row when the column count is reached.
 *
 * Returns: (transfer full): A new #LrgGrid
 */
LrgGrid *
lrg_grid_new (guint columns)
{
    return g_object_new (LRG_TYPE_GRID,
                         "columns", columns,
                         NULL);
}

/**
 * lrg_grid_get_columns:
 * @self: an #LrgGrid
 *
 * Gets the number of columns in the grid.
 *
 * Returns: The column count
 */
guint
lrg_grid_get_columns (LrgGrid *self)
{
    g_return_val_if_fail (LRG_IS_GRID (self), 1);
    return self->columns;
}

/**
 * lrg_grid_set_columns:
 * @self: an #LrgGrid
 * @columns: number of columns
 *
 * Sets the number of columns in the grid. Must be at least 1.
 */
void
lrg_grid_set_columns (LrgGrid *self,
                      guint    columns)
{
    g_return_if_fail (LRG_IS_GRID (self));
    g_return_if_fail (columns >= 1);

    if (self->columns != columns)
    {
        self->columns = columns;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_COLUMNS]);
        lrg_container_layout_children (LRG_CONTAINER (self));
    }
}

/**
 * lrg_grid_get_column_spacing:
 * @self: an #LrgGrid
 *
 * Gets the horizontal spacing between columns.
 *
 * Returns: The column spacing in pixels
 */
gfloat
lrg_grid_get_column_spacing (LrgGrid *self)
{
    g_return_val_if_fail (LRG_IS_GRID (self), 0.0f);
    return self->column_spacing;
}

/**
 * lrg_grid_set_column_spacing:
 * @self: an #LrgGrid
 * @spacing: the spacing in pixels
 *
 * Sets the horizontal spacing between columns.
 */
void
lrg_grid_set_column_spacing (LrgGrid *self,
                             gfloat   spacing)
{
    g_return_if_fail (LRG_IS_GRID (self));
    g_return_if_fail (spacing >= 0.0f);

    if (self->column_spacing != spacing)
    {
        self->column_spacing = spacing;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_COLUMN_SPACING]);
        lrg_container_layout_children (LRG_CONTAINER (self));
    }
}

/**
 * lrg_grid_get_row_spacing:
 * @self: an #LrgGrid
 *
 * Gets the vertical spacing between rows.
 *
 * Returns: The row spacing in pixels
 */
gfloat
lrg_grid_get_row_spacing (LrgGrid *self)
{
    g_return_val_if_fail (LRG_IS_GRID (self), 0.0f);
    return self->row_spacing;
}

/**
 * lrg_grid_set_row_spacing:
 * @self: an #LrgGrid
 * @spacing: the spacing in pixels
 *
 * Sets the vertical spacing between rows.
 */
void
lrg_grid_set_row_spacing (LrgGrid *self,
                          gfloat   spacing)
{
    g_return_if_fail (LRG_IS_GRID (self));
    g_return_if_fail (spacing >= 0.0f);

    if (self->row_spacing != spacing)
    {
        self->row_spacing = spacing;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ROW_SPACING]);
        lrg_container_layout_children (LRG_CONTAINER (self));
    }
}
