/* test-ui.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Unit tests for UI module (Widget, Container, Label, Button, etc.).
 */

#include <glib.h>
#include <libregnum.h>

/* ==========================================================================
 * Test Fixtures - Widget (using Panel as concrete widget)
 * ========================================================================== */

typedef struct
{
    LrgPanel *widget;
} WidgetFixture;

static void
widget_fixture_set_up (WidgetFixture *fixture,
                       gconstpointer  user_data)
{
    fixture->widget = lrg_panel_new ();
    g_assert_nonnull (fixture->widget);
}

static void
widget_fixture_tear_down (WidgetFixture *fixture,
                          gconstpointer  user_data)
{
    g_clear_object (&fixture->widget);
}

/* ==========================================================================
 * Test Fixtures - Container
 * ========================================================================== */

typedef struct
{
    LrgPanel *container;
} ContainerFixture;

static void
container_fixture_set_up (ContainerFixture *fixture,
                          gconstpointer     user_data)
{
    fixture->container = lrg_panel_new ();
    g_assert_nonnull (fixture->container);
}

static void
container_fixture_tear_down (ContainerFixture *fixture,
                             gconstpointer     user_data)
{
    g_clear_object (&fixture->container);
}

/* ==========================================================================
 * Test Fixtures - Canvas
 * ========================================================================== */

typedef struct
{
    LrgCanvas *canvas;
} CanvasFixture;

static void
canvas_fixture_set_up (CanvasFixture *fixture,
                       gconstpointer  user_data)
{
    fixture->canvas = lrg_canvas_new ();
    g_assert_nonnull (fixture->canvas);
}

static void
canvas_fixture_tear_down (CanvasFixture *fixture,
                          gconstpointer  user_data)
{
    g_clear_object (&fixture->canvas);
}

/* ==========================================================================
 * Test Cases - UIEvent
 * ========================================================================== */

static void
test_ui_event_mouse_move (void)
{
    g_autoptr(LrgUIEvent) event = NULL;

    event = lrg_ui_event_new_mouse_move (100.0f, 200.0f);

    g_assert_nonnull (event);
    g_assert_cmpint (lrg_ui_event_get_event_type (event), ==, LRG_UI_EVENT_MOUSE_MOVE);
    g_assert_cmpfloat_with_epsilon (lrg_ui_event_get_x (event), 100.0f, 0.0001f);
    g_assert_cmpfloat_with_epsilon (lrg_ui_event_get_y (event), 200.0f, 0.0001f);
    g_assert_false (lrg_ui_event_get_consumed (event));
}

static void
test_ui_event_mouse_button (void)
{
    g_autoptr(LrgUIEvent) event = NULL;

    event = lrg_ui_event_new_mouse_button (LRG_UI_EVENT_MOUSE_BUTTON_DOWN,
                                           GRL_MOUSE_BUTTON_LEFT,
                                           50.0f, 75.0f);

    g_assert_nonnull (event);
    g_assert_cmpint (lrg_ui_event_get_event_type (event), ==, LRG_UI_EVENT_MOUSE_BUTTON_DOWN);
    g_assert_cmpfloat_with_epsilon (lrg_ui_event_get_x (event), 50.0f, 0.0001f);
    g_assert_cmpfloat_with_epsilon (lrg_ui_event_get_y (event), 75.0f, 0.0001f);
    g_assert_cmpint (lrg_ui_event_get_button (event), ==, GRL_MOUSE_BUTTON_LEFT);
}

static void
test_ui_event_key (void)
{
    g_autoptr(LrgUIEvent) event = NULL;

    event = lrg_ui_event_new_key (LRG_UI_EVENT_KEY_DOWN, GRL_KEY_SPACE);

    g_assert_nonnull (event);
    g_assert_cmpint (lrg_ui_event_get_event_type (event), ==, LRG_UI_EVENT_KEY_DOWN);
    g_assert_cmpint (lrg_ui_event_get_key (event), ==, GRL_KEY_SPACE);
}

static void
test_ui_event_scroll (void)
{
    g_autoptr(LrgUIEvent) event = NULL;

    event = lrg_ui_event_new_scroll (10.0f, 20.0f, 0.0f, -1.0f);

    g_assert_nonnull (event);
    g_assert_cmpint (lrg_ui_event_get_event_type (event), ==, LRG_UI_EVENT_SCROLL);
    g_assert_cmpfloat_with_epsilon (lrg_ui_event_get_scroll_x (event), 0.0f, 0.0001f);
    g_assert_cmpfloat_with_epsilon (lrg_ui_event_get_scroll_y (event), -1.0f, 0.0001f);
}

static void
test_ui_event_focus (void)
{
    g_autoptr(LrgUIEvent) focus_in = NULL;
    g_autoptr(LrgUIEvent) focus_out = NULL;

    focus_in = lrg_ui_event_new_focus_in ();
    focus_out = lrg_ui_event_new_focus_out ();

    g_assert_cmpint (lrg_ui_event_get_event_type (focus_in), ==, LRG_UI_EVENT_FOCUS_IN);
    g_assert_cmpint (lrg_ui_event_get_event_type (focus_out), ==, LRG_UI_EVENT_FOCUS_OUT);
}

static void
test_ui_event_consumed (void)
{
    g_autoptr(LrgUIEvent) event = NULL;

    event = lrg_ui_event_new_mouse_move (0.0f, 0.0f);

    g_assert_false (lrg_ui_event_get_consumed (event));

    lrg_ui_event_set_consumed (event, TRUE);
    g_assert_true (lrg_ui_event_get_consumed (event));
}

static void
test_ui_event_copy (void)
{
    g_autoptr(LrgUIEvent) original = NULL;
    g_autoptr(LrgUIEvent) copy = NULL;

    original = lrg_ui_event_new_mouse_move (123.0f, 456.0f);
    copy = lrg_ui_event_copy (original);

    g_assert_nonnull (copy);
    g_assert_true (copy != original);
    g_assert_cmpfloat_with_epsilon (lrg_ui_event_get_x (copy), 123.0f, 0.0001f);
    g_assert_cmpfloat_with_epsilon (lrg_ui_event_get_y (copy), 456.0f, 0.0001f);
}

/* ==========================================================================
 * Test Cases - Widget Properties
 * ========================================================================== */

static void
test_widget_new (void)
{
    g_autoptr(LrgPanel) widget = NULL;

    widget = lrg_panel_new ();

    g_assert_nonnull (widget);
    g_assert_true (LRG_IS_WIDGET (widget));
    g_assert_true (LRG_IS_CONTAINER (widget));
    g_assert_true (lrg_widget_get_visible (LRG_WIDGET (widget)));
    g_assert_true (lrg_widget_get_enabled (LRG_WIDGET (widget)));
}

static void
test_widget_position (WidgetFixture *fixture,
                      gconstpointer  user_data)
{
    LrgWidget *widget = LRG_WIDGET (fixture->widget);

    /* Default position is 0,0 */
    g_assert_cmpfloat_with_epsilon (lrg_widget_get_x (widget), 0.0f, 0.0001f);
    g_assert_cmpfloat_with_epsilon (lrg_widget_get_y (widget), 0.0f, 0.0001f);

    /* Set position */
    lrg_widget_set_x (widget, 100.0f);
    lrg_widget_set_y (widget, 200.0f);

    g_assert_cmpfloat_with_epsilon (lrg_widget_get_x (widget), 100.0f, 0.0001f);
    g_assert_cmpfloat_with_epsilon (lrg_widget_get_y (widget), 200.0f, 0.0001f);

    /* Set position convenience */
    lrg_widget_set_position (widget, 50.0f, 75.0f);

    g_assert_cmpfloat_with_epsilon (lrg_widget_get_x (widget), 50.0f, 0.0001f);
    g_assert_cmpfloat_with_epsilon (lrg_widget_get_y (widget), 75.0f, 0.0001f);
}

static void
test_widget_size (WidgetFixture *fixture,
                  gconstpointer  user_data)
{
    LrgWidget *widget = LRG_WIDGET (fixture->widget);

    /* Set size */
    lrg_widget_set_width (widget, 320.0f);
    lrg_widget_set_height (widget, 240.0f);

    g_assert_cmpfloat_with_epsilon (lrg_widget_get_width (widget), 320.0f, 0.0001f);
    g_assert_cmpfloat_with_epsilon (lrg_widget_get_height (widget), 240.0f, 0.0001f);

    /* Set size convenience */
    lrg_widget_set_size (widget, 640.0f, 480.0f);

    g_assert_cmpfloat_with_epsilon (lrg_widget_get_width (widget), 640.0f, 0.0001f);
    g_assert_cmpfloat_with_epsilon (lrg_widget_get_height (widget), 480.0f, 0.0001f);
}

static void
test_widget_visible (WidgetFixture *fixture,
                     gconstpointer  user_data)
{
    LrgWidget *widget = LRG_WIDGET (fixture->widget);

    g_assert_true (lrg_widget_get_visible (widget));

    lrg_widget_set_visible (widget, FALSE);
    g_assert_false (lrg_widget_get_visible (widget));

    lrg_widget_set_visible (widget, TRUE);
    g_assert_true (lrg_widget_get_visible (widget));
}

static void
test_widget_enabled (WidgetFixture *fixture,
                     gconstpointer  user_data)
{
    LrgWidget *widget = LRG_WIDGET (fixture->widget);

    g_assert_true (lrg_widget_get_enabled (widget));

    lrg_widget_set_enabled (widget, FALSE);
    g_assert_false (lrg_widget_get_enabled (widget));

    lrg_widget_set_enabled (widget, TRUE);
    g_assert_true (lrg_widget_get_enabled (widget));
}

static void
test_widget_world_coordinates (WidgetFixture *fixture,
                               gconstpointer  user_data)
{
    g_autoptr(LrgPanel) parent = NULL;
    LrgWidget          *widget = LRG_WIDGET (fixture->widget);

    parent = lrg_panel_new ();
    lrg_widget_set_position (LRG_WIDGET (parent), 100.0f, 200.0f);
    lrg_widget_set_position (widget, 50.0f, 75.0f);

    /* Without parent, world coords = local coords */
    g_assert_cmpfloat_with_epsilon (lrg_widget_get_world_x (widget), 50.0f, 0.0001f);
    g_assert_cmpfloat_with_epsilon (lrg_widget_get_world_y (widget), 75.0f, 0.0001f);

    /* Add to parent */
    lrg_container_add_child (LRG_CONTAINER (parent), widget);

    /* World coords = parent + local */
    g_assert_cmpfloat_with_epsilon (lrg_widget_get_world_x (widget), 150.0f, 0.0001f);
    g_assert_cmpfloat_with_epsilon (lrg_widget_get_world_y (widget), 275.0f, 0.0001f);

    /* Remove to avoid double-free */
    lrg_container_remove_child (LRG_CONTAINER (parent), widget);
}

static void
test_widget_contains_point (WidgetFixture *fixture,
                            gconstpointer  user_data)
{
    LrgWidget *widget = LRG_WIDGET (fixture->widget);

    lrg_widget_set_position (widget, 100.0f, 100.0f);
    lrg_widget_set_size (widget, 50.0f, 50.0f);

    /* Inside */
    g_assert_true (lrg_widget_contains_point (widget, 100.0f, 100.0f));
    g_assert_true (lrg_widget_contains_point (widget, 125.0f, 125.0f));
    g_assert_true (lrg_widget_contains_point (widget, 149.0f, 149.0f));

    /* Outside */
    g_assert_false (lrg_widget_contains_point (widget, 99.0f, 100.0f));
    g_assert_false (lrg_widget_contains_point (widget, 100.0f, 99.0f));
    g_assert_false (lrg_widget_contains_point (widget, 150.0f, 100.0f));
    g_assert_false (lrg_widget_contains_point (widget, 100.0f, 150.0f));
}

/* ==========================================================================
 * Test Cases - Container
 * ========================================================================== */

static void
test_container_add_child (ContainerFixture *fixture,
                          gconstpointer     user_data)
{
    g_autoptr(LrgLabel) child = NULL;
    GList              *children;

    child = lrg_label_new ("Test");

    lrg_container_add_child (LRG_CONTAINER (fixture->container), LRG_WIDGET (child));

    children = lrg_container_get_children (LRG_CONTAINER (fixture->container));
    g_assert_cmpuint (g_list_length (children), ==, 1);
    g_assert_true (children->data == child);

    /* Parent should be set */
    g_assert_true (lrg_widget_get_parent (LRG_WIDGET (child)) ==
                   LRG_CONTAINER (fixture->container));

    /* Remove to avoid double-free */
    lrg_container_remove_child (LRG_CONTAINER (fixture->container), LRG_WIDGET (child));
}

static void
test_container_remove_child (ContainerFixture *fixture,
                             gconstpointer     user_data)
{
    g_autoptr(LrgLabel) child = NULL;
    GList              *children;

    child = lrg_label_new ("Test");
    g_object_ref (child);

    lrg_container_add_child (LRG_CONTAINER (fixture->container), LRG_WIDGET (child));

    children = lrg_container_get_children (LRG_CONTAINER (fixture->container));
    g_assert_cmpuint (g_list_length (children), ==, 1);

    lrg_container_remove_child (LRG_CONTAINER (fixture->container), LRG_WIDGET (child));

    children = lrg_container_get_children (LRG_CONTAINER (fixture->container));
    g_assert_cmpuint (g_list_length (children), ==, 0);
    g_assert_null (lrg_widget_get_parent (LRG_WIDGET (child)));
}

static void
test_container_spacing (ContainerFixture *fixture,
                        gconstpointer     user_data)
{
    LrgContainer *container = LRG_CONTAINER (fixture->container);

    /* Default spacing */
    g_assert_cmpfloat_with_epsilon (lrg_container_get_spacing (container), 0.0f, 0.0001f);

    lrg_container_set_spacing (container, 10.0f);
    g_assert_cmpfloat_with_epsilon (lrg_container_get_spacing (container), 10.0f, 0.0001f);
}

static void
test_container_padding (ContainerFixture *fixture,
                        gconstpointer     user_data)
{
    LrgContainer *container = LRG_CONTAINER (fixture->container);

    /* Default padding */
    g_assert_cmpfloat_with_epsilon (lrg_container_get_padding (container), 0.0f, 0.0001f);

    lrg_container_set_padding (container, 5.0f);
    g_assert_cmpfloat_with_epsilon (lrg_container_get_padding (container), 5.0f, 0.0001f);
}

/* ==========================================================================
 * Test Cases - Label
 * ========================================================================== */

static void
test_label_new (void)
{
    g_autoptr(LrgLabel) label = NULL;

    label = lrg_label_new ("Hello World");

    g_assert_nonnull (label);
    g_assert_true (LRG_IS_LABEL (label));
    g_assert_true (LRG_IS_WIDGET (label));
    g_assert_cmpstr (lrg_label_get_text (label), ==, "Hello World");
}

static void
test_label_text (void)
{
    g_autoptr(LrgLabel) label = NULL;

    label = lrg_label_new ("Initial");

    g_assert_cmpstr (lrg_label_get_text (label), ==, "Initial");

    lrg_label_set_text (label, "Changed");
    g_assert_cmpstr (lrg_label_get_text (label), ==, "Changed");
}

static void
test_label_font_size (void)
{
    g_autoptr(LrgLabel) label = NULL;

    label = lrg_label_new ("Test");

    /* Default font size is 20 */
    g_assert_cmpfloat_with_epsilon (lrg_label_get_font_size (label), 20.0f, 0.0001f);

    lrg_label_set_font_size (label, 24.0f);
    g_assert_cmpfloat_with_epsilon (lrg_label_get_font_size (label), 24.0f, 0.0001f);
}

static void
test_label_alignment (void)
{
    g_autoptr(LrgLabel) label = NULL;

    label = lrg_label_new ("Test");

    /* Default alignment is left */
    g_assert_cmpint (lrg_label_get_alignment (label), ==, LRG_TEXT_ALIGN_LEFT);

    lrg_label_set_alignment (label, LRG_TEXT_ALIGN_CENTER);
    g_assert_cmpint (lrg_label_get_alignment (label), ==, LRG_TEXT_ALIGN_CENTER);

    lrg_label_set_alignment (label, LRG_TEXT_ALIGN_RIGHT);
    g_assert_cmpint (lrg_label_get_alignment (label), ==, LRG_TEXT_ALIGN_RIGHT);
}

/* ==========================================================================
 * Test Cases - Button
 * ========================================================================== */

static void
test_button_new (void)
{
    g_autoptr(LrgButton) button = NULL;

    button = lrg_button_new ("Click Me");

    g_assert_nonnull (button);
    g_assert_true (LRG_IS_BUTTON (button));
    g_assert_true (LRG_IS_WIDGET (button));
    g_assert_cmpstr (lrg_button_get_text (button), ==, "Click Me");
}

static void
test_button_text (void)
{
    g_autoptr(LrgButton) button = NULL;

    button = lrg_button_new ("Initial");

    g_assert_cmpstr (lrg_button_get_text (button), ==, "Initial");

    lrg_button_set_text (button, "Changed");
    g_assert_cmpstr (lrg_button_get_text (button), ==, "Changed");
}

static void
test_button_corner_radius (void)
{
    g_autoptr(LrgButton) button = NULL;

    button = lrg_button_new ("Test");

    /* Default corner radius is 4 */
    g_assert_cmpfloat_with_epsilon (lrg_button_get_corner_radius (button), 4.0f, 0.0001f);

    lrg_button_set_corner_radius (button, 10.0f);
    g_assert_cmpfloat_with_epsilon (lrg_button_get_corner_radius (button), 10.0f, 0.0001f);
}

/* ==========================================================================
 * Test Cases - Panel
 * ========================================================================== */

static void
test_panel_new (void)
{
    g_autoptr(LrgPanel) panel = NULL;

    panel = lrg_panel_new ();

    g_assert_nonnull (panel);
    g_assert_true (LRG_IS_PANEL (panel));
    g_assert_true (LRG_IS_CONTAINER (panel));
}

static void
test_panel_corner_radius (void)
{
    g_autoptr(LrgPanel) panel = NULL;

    panel = lrg_panel_new ();

    /* Default corner radius */
    g_assert_cmpfloat_with_epsilon (lrg_panel_get_corner_radius (panel), 0.0f, 0.0001f);

    lrg_panel_set_corner_radius (panel, 8.0f);
    g_assert_cmpfloat_with_epsilon (lrg_panel_get_corner_radius (panel), 8.0f, 0.0001f);
}

static void
test_panel_border_width (void)
{
    g_autoptr(LrgPanel) panel = NULL;

    panel = lrg_panel_new ();

    /* Default border width is 1.0f */
    g_assert_cmpfloat_with_epsilon (lrg_panel_get_border_width (panel), 1.0f, 0.0001f);

    lrg_panel_set_border_width (panel, 2.0f);
    g_assert_cmpfloat_with_epsilon (lrg_panel_get_border_width (panel), 2.0f, 0.0001f);
}

/* ==========================================================================
 * Test Cases - VBox
 * ========================================================================== */

static void
test_vbox_new (void)
{
    g_autoptr(LrgVBox) vbox = NULL;

    vbox = lrg_vbox_new ();

    g_assert_nonnull (vbox);
    g_assert_true (LRG_IS_VBOX (vbox));
    g_assert_true (LRG_IS_CONTAINER (vbox));
    g_assert_false (lrg_vbox_get_homogeneous (vbox));
}

static void
test_vbox_homogeneous (void)
{
    g_autoptr(LrgVBox) vbox = NULL;

    vbox = lrg_vbox_new ();

    g_assert_false (lrg_vbox_get_homogeneous (vbox));

    lrg_vbox_set_homogeneous (vbox, TRUE);
    g_assert_true (lrg_vbox_get_homogeneous (vbox));

    lrg_vbox_set_homogeneous (vbox, FALSE);
    g_assert_false (lrg_vbox_get_homogeneous (vbox));
}

/* ==========================================================================
 * Test Cases - HBox
 * ========================================================================== */

static void
test_hbox_new (void)
{
    g_autoptr(LrgHBox) hbox = NULL;

    hbox = lrg_hbox_new ();

    g_assert_nonnull (hbox);
    g_assert_true (LRG_IS_HBOX (hbox));
    g_assert_true (LRG_IS_CONTAINER (hbox));
    g_assert_false (lrg_hbox_get_homogeneous (hbox));
}

static void
test_hbox_homogeneous (void)
{
    g_autoptr(LrgHBox) hbox = NULL;

    hbox = lrg_hbox_new ();

    g_assert_false (lrg_hbox_get_homogeneous (hbox));

    lrg_hbox_set_homogeneous (hbox, TRUE);
    g_assert_true (lrg_hbox_get_homogeneous (hbox));
}

/* ==========================================================================
 * Test Cases - Grid
 * ========================================================================== */

static void
test_grid_new (void)
{
    g_autoptr(LrgGrid) grid = NULL;

    grid = lrg_grid_new (3);

    g_assert_nonnull (grid);
    g_assert_true (LRG_IS_GRID (grid));
    g_assert_true (LRG_IS_CONTAINER (grid));
    g_assert_cmpuint (lrg_grid_get_columns (grid), ==, 3);
}

static void
test_grid_columns (void)
{
    g_autoptr(LrgGrid) grid = NULL;

    grid = lrg_grid_new (2);

    g_assert_cmpuint (lrg_grid_get_columns (grid), ==, 2);

    lrg_grid_set_columns (grid, 4);
    g_assert_cmpuint (lrg_grid_get_columns (grid), ==, 4);
}

static void
test_grid_spacing (void)
{
    g_autoptr(LrgGrid) grid = NULL;

    grid = lrg_grid_new (2);

    /* Default spacing */
    g_assert_cmpfloat_with_epsilon (lrg_grid_get_column_spacing (grid), 0.0f, 0.0001f);
    g_assert_cmpfloat_with_epsilon (lrg_grid_get_row_spacing (grid), 0.0f, 0.0001f);

    lrg_grid_set_column_spacing (grid, 10.0f);
    lrg_grid_set_row_spacing (grid, 5.0f);

    g_assert_cmpfloat_with_epsilon (lrg_grid_get_column_spacing (grid), 10.0f, 0.0001f);
    g_assert_cmpfloat_with_epsilon (lrg_grid_get_row_spacing (grid), 5.0f, 0.0001f);
}

/* ==========================================================================
 * Test Cases - Canvas
 * ========================================================================== */

static void
test_canvas_new (void)
{
    g_autoptr(LrgCanvas) canvas = NULL;

    canvas = lrg_canvas_new ();

    g_assert_nonnull (canvas);
    g_assert_true (LRG_IS_CANVAS (canvas));
    g_assert_true (LRG_IS_CONTAINER (canvas));
    g_assert_null (lrg_canvas_get_focused_widget (canvas));
    g_assert_null (lrg_canvas_get_hovered_widget (canvas));
}

static void
test_canvas_focus (CanvasFixture *fixture,
                   gconstpointer  user_data)
{
    g_autoptr(LrgButton) button = NULL;

    button = lrg_button_new ("Test");
    lrg_container_add_child (LRG_CONTAINER (fixture->canvas), LRG_WIDGET (button));

    g_assert_null (lrg_canvas_get_focused_widget (fixture->canvas));

    lrg_canvas_set_focused_widget (fixture->canvas, LRG_WIDGET (button));
    g_assert_true (lrg_canvas_get_focused_widget (fixture->canvas) == LRG_WIDGET (button));

    lrg_canvas_set_focused_widget (fixture->canvas, NULL);
    g_assert_null (lrg_canvas_get_focused_widget (fixture->canvas));

    /* Remove to avoid double-free */
    lrg_container_remove_child (LRG_CONTAINER (fixture->canvas), LRG_WIDGET (button));
}

static void
test_canvas_widget_at_point (CanvasFixture *fixture,
                             gconstpointer  user_data)
{
    g_autoptr(LrgButton) button = NULL;
    LrgWidget           *found;

    lrg_widget_set_size (LRG_WIDGET (fixture->canvas), 800.0f, 600.0f);

    button = lrg_button_new ("Test");
    lrg_widget_set_position (LRG_WIDGET (button), 100.0f, 100.0f);
    lrg_widget_set_size (LRG_WIDGET (button), 80.0f, 30.0f);

    lrg_container_add_child (LRG_CONTAINER (fixture->canvas), LRG_WIDGET (button));

    /* Find button */
    found = lrg_canvas_widget_at_point (fixture->canvas, 120.0f, 110.0f);
    g_assert_true (found == LRG_WIDGET (button));

    /* Miss */
    found = lrg_canvas_widget_at_point (fixture->canvas, 50.0f, 50.0f);
    g_assert_null (found);

    /* Remove to avoid double-free */
    lrg_container_remove_child (LRG_CONTAINER (fixture->canvas), LRG_WIDGET (button));
}

/* ==========================================================================
 * Test Cases - Checkbox
 * ========================================================================== */

static void
test_checkbox_new (void)
{
    g_autoptr(LrgCheckbox) checkbox = NULL;

    checkbox = lrg_checkbox_new (NULL);

    g_assert_nonnull (checkbox);
    g_assert_true (LRG_IS_CHECKBOX (checkbox));
    g_assert_true (LRG_IS_WIDGET (checkbox));
    g_assert_false (lrg_checkbox_get_checked (checkbox));
}

static void
test_checkbox_checked (void)
{
    g_autoptr(LrgCheckbox) checkbox = NULL;

    checkbox = lrg_checkbox_new (NULL);

    g_assert_false (lrg_checkbox_get_checked (checkbox));

    lrg_checkbox_set_checked (checkbox, TRUE);
    g_assert_true (lrg_checkbox_get_checked (checkbox));

    lrg_checkbox_set_checked (checkbox, FALSE);
    g_assert_false (lrg_checkbox_get_checked (checkbox));
}

static void
test_checkbox_with_label (void)
{
    g_autoptr(LrgCheckbox) checkbox = NULL;

    checkbox = lrg_checkbox_new ("Enable feature");

    g_assert_nonnull (checkbox);
    g_assert_cmpstr (lrg_checkbox_get_label (checkbox), ==, "Enable feature");
}

static void
test_checkbox_label (void)
{
    g_autoptr(LrgCheckbox) checkbox = NULL;

    checkbox = lrg_checkbox_new (NULL);

    lrg_checkbox_set_label (checkbox, "New label");
    g_assert_cmpstr (lrg_checkbox_get_label (checkbox), ==, "New label");
}

/* ==========================================================================
 * Test Cases - ProgressBar
 * ========================================================================== */

static void
test_progress_bar_new (void)
{
    g_autoptr(LrgProgressBar) bar = NULL;

    bar = lrg_progress_bar_new ();

    g_assert_nonnull (bar);
    g_assert_true (LRG_IS_PROGRESS_BAR (bar));
    g_assert_true (LRG_IS_WIDGET (bar));
    g_assert_cmpfloat_with_epsilon (lrg_progress_bar_get_value (bar), 0.0, 0.0001);
}

static void
test_progress_bar_value (void)
{
    g_autoptr(LrgProgressBar) bar = NULL;

    bar = lrg_progress_bar_new ();

    lrg_progress_bar_set_value (bar, 50.0);
    g_assert_cmpfloat_with_epsilon (lrg_progress_bar_get_value (bar), 50.0, 0.0001);

    /* Test clamping */
    lrg_progress_bar_set_value (bar, 150.0);
    g_assert_cmpfloat_with_epsilon (lrg_progress_bar_get_value (bar), 100.0, 0.0001);

    lrg_progress_bar_set_value (bar, -10.0);
    g_assert_cmpfloat_with_epsilon (lrg_progress_bar_get_value (bar), 0.0, 0.0001);
}

static void
test_progress_bar_percentage (void)
{
    g_autoptr(LrgProgressBar) bar = NULL;
    gdouble percentage;

    bar = lrg_progress_bar_new ();
    lrg_progress_bar_set_max (bar, 200.0);
    lrg_progress_bar_set_value (bar, 100.0);

    /* Calculate percentage manually: (value / max) * 100 */
    percentage = (lrg_progress_bar_get_value (bar) / lrg_progress_bar_get_max (bar)) * 100.0;
    g_assert_cmpfloat_with_epsilon (percentage, 50.0, 0.0001);
}

static void
test_progress_bar_max (void)
{
    g_autoptr(LrgProgressBar) bar = NULL;

    bar = lrg_progress_bar_new ();

    g_assert_cmpfloat_with_epsilon (lrg_progress_bar_get_max (bar), 100.0, 0.0001);

    lrg_progress_bar_set_max (bar, 200.0);
    g_assert_cmpfloat_with_epsilon (lrg_progress_bar_get_max (bar), 200.0, 0.0001);
}

/* ==========================================================================
 * Test Cases - Image
 * ========================================================================== */

static void
test_image_new (void)
{
    g_autoptr(LrgImage) image = NULL;

    image = lrg_image_new ();

    g_assert_nonnull (image);
    g_assert_true (LRG_IS_IMAGE (image));
    g_assert_true (LRG_IS_WIDGET (image));
    g_assert_null (lrg_image_get_texture (image));
}

static void
test_image_scale_mode (void)
{
    g_autoptr(LrgImage) image = NULL;

    image = lrg_image_new ();

    /* Default is FIT */
    g_assert_cmpint (lrg_image_get_scale_mode (image), ==, LRG_IMAGE_SCALE_MODE_FIT);

    lrg_image_set_scale_mode (image, LRG_IMAGE_SCALE_MODE_FILL);
    g_assert_cmpint (lrg_image_get_scale_mode (image), ==, LRG_IMAGE_SCALE_MODE_FILL);

    lrg_image_set_scale_mode (image, LRG_IMAGE_SCALE_MODE_STRETCH);
    g_assert_cmpint (lrg_image_get_scale_mode (image), ==, LRG_IMAGE_SCALE_MODE_STRETCH);

    lrg_image_set_scale_mode (image, LRG_IMAGE_SCALE_MODE_TILE);
    g_assert_cmpint (lrg_image_get_scale_mode (image), ==, LRG_IMAGE_SCALE_MODE_TILE);
}

static void
test_image_tint (void)
{
    g_autoptr(LrgImage) image = NULL;
    GrlColor             tint;
    const GrlColor      *result;

    image = lrg_image_new ();

    tint.r = 255;
    tint.g = 128;
    tint.b = 64;
    tint.a = 255;

    lrg_image_set_tint (image, &tint);
    result = lrg_image_get_tint (image);

    g_assert_cmpint (result->r, ==, 255);
    g_assert_cmpint (result->g, ==, 128);
    g_assert_cmpint (result->b, ==, 64);
    g_assert_cmpint (result->a, ==, 255);
}

/* ==========================================================================
 * Test Cases - Slider
 * ========================================================================== */

static void
test_slider_new (void)
{
    g_autoptr(LrgSlider) slider = NULL;

    slider = lrg_slider_new ();

    g_assert_nonnull (slider);
    g_assert_true (LRG_IS_SLIDER (slider));
    g_assert_true (LRG_IS_WIDGET (slider));
}

static void
test_slider_value (void)
{
    g_autoptr(LrgSlider) slider = NULL;

    slider = lrg_slider_new ();

    /* Default value is 0 */
    g_assert_cmpfloat_with_epsilon (lrg_slider_get_value (slider), 0.0, 0.0001);

    lrg_slider_set_value (slider, 50.0);
    g_assert_cmpfloat_with_epsilon (lrg_slider_get_value (slider), 50.0, 0.0001);
}

static void
test_slider_value_range (void)
{
    g_autoptr(LrgSlider) slider = NULL;

    slider = lrg_slider_new ();

    /* Default range is 0-100 */
    g_assert_cmpfloat_with_epsilon (lrg_slider_get_min (slider), 0.0, 0.0001);
    g_assert_cmpfloat_with_epsilon (lrg_slider_get_max (slider), 100.0, 0.0001);

    /* Set custom range */
    lrg_slider_set_min (slider, -50.0);
    lrg_slider_set_max (slider, 50.0);

    g_assert_cmpfloat_with_epsilon (lrg_slider_get_min (slider), -50.0, 0.0001);
    g_assert_cmpfloat_with_epsilon (lrg_slider_get_max (slider), 50.0, 0.0001);
}

static void
test_slider_step (void)
{
    g_autoptr(LrgSlider) slider = NULL;

    slider = lrg_slider_new ();

    /* Default step is 1 */
    g_assert_cmpfloat_with_epsilon (lrg_slider_get_step (slider), 1.0, 0.0001);

    lrg_slider_set_step (slider, 5.0);
    g_assert_cmpfloat_with_epsilon (lrg_slider_get_step (slider), 5.0, 0.0001);
}

static void
test_slider_orientation (void)
{
    g_autoptr(LrgSlider) slider = NULL;

    slider = lrg_slider_new ();

    /* Default is horizontal */
    g_assert_cmpint (lrg_slider_get_orientation (slider), ==, LRG_ORIENTATION_HORIZONTAL);

    lrg_slider_set_orientation (slider, LRG_ORIENTATION_VERTICAL);
    g_assert_cmpint (lrg_slider_get_orientation (slider), ==, LRG_ORIENTATION_VERTICAL);
}

/* ==========================================================================
 * Test Cases - TextInput
 * ========================================================================== */

static void
test_text_input_new (void)
{
    g_autoptr(LrgTextInput) input = NULL;

    input = lrg_text_input_new ();

    g_assert_nonnull (input);
    g_assert_true (LRG_IS_TEXT_INPUT (input));
    g_assert_true (LRG_IS_WIDGET (input));
    /* Empty text input returns NULL or empty string */
    g_assert_true (lrg_text_input_get_text (input) == NULL ||
                   g_strcmp0 (lrg_text_input_get_text (input), "") == 0);
}

static void
test_text_input_text (void)
{
    g_autoptr(LrgTextInput) input = NULL;

    input = lrg_text_input_new ();

    lrg_text_input_set_text (input, "Hello World");
    g_assert_cmpstr (lrg_text_input_get_text (input), ==, "Hello World");
}

static void
test_text_input_placeholder (void)
{
    g_autoptr(LrgTextInput) input = NULL;

    input = lrg_text_input_new_with_placeholder ("Enter name...");

    g_assert_cmpstr (lrg_text_input_get_placeholder (input), ==, "Enter name...");

    lrg_text_input_set_placeholder (input, "Type here");
    g_assert_cmpstr (lrg_text_input_get_placeholder (input), ==, "Type here");
}

static void
test_text_input_max_length (void)
{
    g_autoptr(LrgTextInput) input = NULL;

    input = lrg_text_input_new ();

    /* Default is 0 (unlimited) */
    g_assert_cmpuint (lrg_text_input_get_max_length (input), ==, 0);

    lrg_text_input_set_max_length (input, 50);
    g_assert_cmpuint (lrg_text_input_get_max_length (input), ==, 50);
}

static void
test_text_input_password_mode (void)
{
    g_autoptr(LrgTextInput) input = NULL;

    input = lrg_text_input_new ();

    g_assert_false (lrg_text_input_get_password_mode (input));

    lrg_text_input_set_password_mode (input, TRUE);
    g_assert_true (lrg_text_input_get_password_mode (input));
}

static void
test_text_input_cursor_position (void)
{
    g_autoptr(LrgTextInput) input = NULL;

    input = lrg_text_input_new ();
    lrg_text_input_set_text (input, "Hello");

    g_assert_cmpint (lrg_text_input_get_cursor_position (input), ==, 0);

    lrg_text_input_set_cursor_position (input, 3);
    g_assert_cmpint (lrg_text_input_get_cursor_position (input), ==, 3);
}

/* ==========================================================================
 * Test Cases - Theme
 * ========================================================================== */

static void
test_theme_default (void)
{
    LrgTheme *theme1;
    LrgTheme *theme2;

    theme1 = lrg_theme_get_default ();
    theme2 = lrg_theme_get_default ();

    g_assert_nonnull (theme1);
    g_assert_true (LRG_IS_THEME (theme1));
    /* Singleton - same instance */
    g_assert_true (theme1 == theme2);
}

static void
test_theme_colors (void)
{
    g_autoptr(LrgTheme) theme = NULL;
    GrlColor             color;
    const GrlColor      *result;

    theme = lrg_theme_new ();

    color.r = 100;
    color.g = 150;
    color.b = 200;
    color.a = 255;

    lrg_theme_set_primary_color (theme, &color);
    result = lrg_theme_get_primary_color (theme);

    g_assert_cmpint (result->r, ==, 100);
    g_assert_cmpint (result->g, ==, 150);
    g_assert_cmpint (result->b, ==, 200);
    g_assert_cmpint (result->a, ==, 255);
}

static void
test_theme_font_sizes (void)
{
    g_autoptr(LrgTheme) theme = NULL;

    theme = lrg_theme_new ();

    /* Check defaults */
    g_assert_cmpfloat_with_epsilon (lrg_theme_get_font_size_small (theme), 12.0f, 0.0001f);
    g_assert_cmpfloat_with_epsilon (lrg_theme_get_font_size_normal (theme), 16.0f, 0.0001f);
    g_assert_cmpfloat_with_epsilon (lrg_theme_get_font_size_large (theme), 24.0f, 0.0001f);

    /* Set and check */
    lrg_theme_set_font_size_small (theme, 10.0f);
    lrg_theme_set_font_size_normal (theme, 14.0f);
    lrg_theme_set_font_size_large (theme, 28.0f);

    g_assert_cmpfloat_with_epsilon (lrg_theme_get_font_size_small (theme), 10.0f, 0.0001f);
    g_assert_cmpfloat_with_epsilon (lrg_theme_get_font_size_normal (theme), 14.0f, 0.0001f);
    g_assert_cmpfloat_with_epsilon (lrg_theme_get_font_size_large (theme), 28.0f, 0.0001f);
}

static void
test_theme_spacing (void)
{
    g_autoptr(LrgTheme) theme = NULL;

    theme = lrg_theme_new ();

    /* Check defaults */
    g_assert_cmpfloat_with_epsilon (lrg_theme_get_padding_small (theme), 4.0f, 0.0001f);
    g_assert_cmpfloat_with_epsilon (lrg_theme_get_padding_normal (theme), 8.0f, 0.0001f);
    g_assert_cmpfloat_with_epsilon (lrg_theme_get_padding_large (theme), 16.0f, 0.0001f);
    g_assert_cmpfloat_with_epsilon (lrg_theme_get_border_width (theme), 1.0f, 0.0001f);
    g_assert_cmpfloat_with_epsilon (lrg_theme_get_corner_radius (theme), 4.0f, 0.0001f);
}

/* ==========================================================================
 * Main
 * ========================================================================== */

int
main (int   argc,
      char *argv[])
{
    g_test_init (&argc, &argv, NULL);

    /* UIEvent Tests */
    g_test_add_func ("/ui/event/mouse-move", test_ui_event_mouse_move);
    g_test_add_func ("/ui/event/mouse-button", test_ui_event_mouse_button);
    g_test_add_func ("/ui/event/key", test_ui_event_key);
    g_test_add_func ("/ui/event/scroll", test_ui_event_scroll);
    g_test_add_func ("/ui/event/focus", test_ui_event_focus);
    g_test_add_func ("/ui/event/consumed", test_ui_event_consumed);
    g_test_add_func ("/ui/event/copy", test_ui_event_copy);

    /* Widget Tests */
    g_test_add_func ("/ui/widget/new", test_widget_new);

    g_test_add ("/ui/widget/position",
                WidgetFixture, NULL,
                widget_fixture_set_up,
                test_widget_position,
                widget_fixture_tear_down);

    g_test_add ("/ui/widget/size",
                WidgetFixture, NULL,
                widget_fixture_set_up,
                test_widget_size,
                widget_fixture_tear_down);

    g_test_add ("/ui/widget/visible",
                WidgetFixture, NULL,
                widget_fixture_set_up,
                test_widget_visible,
                widget_fixture_tear_down);

    g_test_add ("/ui/widget/enabled",
                WidgetFixture, NULL,
                widget_fixture_set_up,
                test_widget_enabled,
                widget_fixture_tear_down);

    g_test_add ("/ui/widget/world-coordinates",
                WidgetFixture, NULL,
                widget_fixture_set_up,
                test_widget_world_coordinates,
                widget_fixture_tear_down);

    g_test_add ("/ui/widget/contains-point",
                WidgetFixture, NULL,
                widget_fixture_set_up,
                test_widget_contains_point,
                widget_fixture_tear_down);

    /* Container Tests */
    g_test_add ("/ui/container/add-child",
                ContainerFixture, NULL,
                container_fixture_set_up,
                test_container_add_child,
                container_fixture_tear_down);

    g_test_add ("/ui/container/remove-child",
                ContainerFixture, NULL,
                container_fixture_set_up,
                test_container_remove_child,
                container_fixture_tear_down);

    g_test_add ("/ui/container/spacing",
                ContainerFixture, NULL,
                container_fixture_set_up,
                test_container_spacing,
                container_fixture_tear_down);

    g_test_add ("/ui/container/padding",
                ContainerFixture, NULL,
                container_fixture_set_up,
                test_container_padding,
                container_fixture_tear_down);

    /* Label Tests */
    g_test_add_func ("/ui/label/new", test_label_new);
    g_test_add_func ("/ui/label/text", test_label_text);
    g_test_add_func ("/ui/label/font-size", test_label_font_size);
    g_test_add_func ("/ui/label/alignment", test_label_alignment);

    /* Button Tests */
    g_test_add_func ("/ui/button/new", test_button_new);
    g_test_add_func ("/ui/button/text", test_button_text);
    g_test_add_func ("/ui/button/corner-radius", test_button_corner_radius);

    /* Panel Tests */
    g_test_add_func ("/ui/panel/new", test_panel_new);
    g_test_add_func ("/ui/panel/corner-radius", test_panel_corner_radius);
    g_test_add_func ("/ui/panel/border-width", test_panel_border_width);

    /* VBox Tests */
    g_test_add_func ("/ui/vbox/new", test_vbox_new);
    g_test_add_func ("/ui/vbox/homogeneous", test_vbox_homogeneous);

    /* HBox Tests */
    g_test_add_func ("/ui/hbox/new", test_hbox_new);
    g_test_add_func ("/ui/hbox/homogeneous", test_hbox_homogeneous);

    /* Grid Tests */
    g_test_add_func ("/ui/grid/new", test_grid_new);
    g_test_add_func ("/ui/grid/columns", test_grid_columns);
    g_test_add_func ("/ui/grid/spacing", test_grid_spacing);

    /* Canvas Tests */
    g_test_add_func ("/ui/canvas/new", test_canvas_new);

    g_test_add ("/ui/canvas/focus",
                CanvasFixture, NULL,
                canvas_fixture_set_up,
                test_canvas_focus,
                canvas_fixture_tear_down);

    g_test_add ("/ui/canvas/widget-at-point",
                CanvasFixture, NULL,
                canvas_fixture_set_up,
                test_canvas_widget_at_point,
                canvas_fixture_tear_down);

    /* Checkbox Tests */
    g_test_add_func ("/ui/checkbox/new", test_checkbox_new);
    g_test_add_func ("/ui/checkbox/checked", test_checkbox_checked);
    g_test_add_func ("/ui/checkbox/with-label", test_checkbox_with_label);
    g_test_add_func ("/ui/checkbox/label", test_checkbox_label);

    /* ProgressBar Tests */
    g_test_add_func ("/ui/progress-bar/new", test_progress_bar_new);
    g_test_add_func ("/ui/progress-bar/value", test_progress_bar_value);
    g_test_add_func ("/ui/progress-bar/percentage", test_progress_bar_percentage);
    g_test_add_func ("/ui/progress-bar/max", test_progress_bar_max);

    /* Image Tests */
    g_test_add_func ("/ui/image/new", test_image_new);
    g_test_add_func ("/ui/image/scale-mode", test_image_scale_mode);
    g_test_add_func ("/ui/image/tint", test_image_tint);

    /* Slider Tests */
    g_test_add_func ("/ui/slider/new", test_slider_new);
    g_test_add_func ("/ui/slider/value", test_slider_value);
    g_test_add_func ("/ui/slider/value-range", test_slider_value_range);
    g_test_add_func ("/ui/slider/step", test_slider_step);
    g_test_add_func ("/ui/slider/orientation", test_slider_orientation);

    /* TextInput Tests */
    g_test_add_func ("/ui/text-input/new", test_text_input_new);
    g_test_add_func ("/ui/text-input/text", test_text_input_text);
    g_test_add_func ("/ui/text-input/placeholder", test_text_input_placeholder);
    g_test_add_func ("/ui/text-input/max-length", test_text_input_max_length);
    g_test_add_func ("/ui/text-input/password-mode", test_text_input_password_mode);
    g_test_add_func ("/ui/text-input/cursor-position", test_text_input_cursor_position);

    /* Theme Tests */
    g_test_add_func ("/ui/theme/default", test_theme_default);
    g_test_add_func ("/ui/theme/colors", test_theme_colors);
    g_test_add_func ("/ui/theme/font-sizes", test_theme_font_sizes);
    g_test_add_func ("/ui/theme/spacing", test_theme_spacing);

    return g_test_run ();
}
