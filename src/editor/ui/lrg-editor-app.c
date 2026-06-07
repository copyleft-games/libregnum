/* lrg-editor-app.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Standalone engine-drawn editor application.
 *
 * Layout (1280 x 800 default):
 *   [0 .. 239]   outliner panel  (left)
 *   [240 .. 999] 3-D viewport    (centre) -- rendered directly with LrgCamera3D
 *   [1000 .. 1279] properties panel (right)
 *
 * The 3-D viewport renders every visible node in the level as an LrgCube3D
 * positioned at the node's local location with scale applied.  Selected nodes
 * are tinted yellow; unselected nodes are a neutral grey.  An orbit camera
 * (right-mouse drag + scroll wheel) lets the user orbit around the origin.
 *
 * Input handled here (window-level, outside the Canvas widget system):
 *   Right-mouse drag  -- orbit camera (yaw / pitch)
 *   Scroll wheel      -- zoom
 *   Left-click (viewport) -- select node under cursor (not yet ray-cast;
 *                             selects the first non-selected node for now)
 *   N                 -- add a new cube node
 *   Delete            -- delete the primary selection
 *   Ctrl+Z            -- undo
 *   Ctrl+Y            -- redo
 *   Ctrl+S            -- save level (if a path was set)
 */

#include "lrg-editor-app.h"
#include "../lrg-editor.h"
#include "../lrg-editor-selection.h"
#include "../lrg-level.h"
#include "../lrg-node.h"
#include "../lrg-node-visual.h"
#include "../../ui/lrg-canvas.h"
#include "../../ui/lrg-vbox.h"
#include "../../ui/lrg-hbox.h"
#include "../../ui/lrg-label.h"
#include "../../ui/lrg-panel.h"
#include "../../ui/lrg-container.h"
#include "../../ui/lrg-widget.h"
#include "../../shapes/lrg-cube3d.h"
#include "../../shapes/lrg-shape3d.h"
#include "../../lrg-enums.h"

#include <graylib.h>
#include <stdio.h>
#include <math.h>
#include <string.h>

/* =========================================================================
 * Layout constants
 * ========================================================================= */

#define EDITOR_WIN_W        1280
#define EDITOR_WIN_H        800
#define OUTLINER_W          240.0f
#define PROPS_W             280.0f
#define PANEL_H             ((gfloat) EDITOR_WIN_H)

/* Viewport rect (screen coords) */
#define VP_X                ((gint) OUTLINER_W)
#define VP_W                (EDITOR_WIN_W - (gint)(OUTLINER_W + PROPS_W))
#define VP_H                EDITOR_WIN_H

/* Camera orbit sensitivity */
#define ORBIT_SENSITIVITY   0.4f
#define ZOOM_SENSITIVITY    1.2f
#define MIN_DISTANCE        1.0f
#define MAX_DISTANCE        100.0f

/* Default camera angles (degrees) */
#define DEFAULT_YAW         45.0f
#define DEFAULT_PITCH       30.0f
#define DEFAULT_DIST        12.0f

/* Node visual size in the viewport (cube half-extents for scale=1) */
#define NODE_CUBE_SIZE      0.5f

/* =========================================================================
 * Private instance data
 * ========================================================================= */

struct _LrgEditorApp
{
    GObject parent_instance;

    LrgEditor    *editor;      /* owned */
    LrgCanvas    *canvas;      /* owned */
    LrgVBox      *outliner;    /* borrowed (child of canvas) */
    LrgVBox      *props;       /* borrowed (child of canvas) */
    LrgLabel     *prop_name;   /* borrowed */
    LrgLabel     *prop_pos;    /* borrowed */
    LrgLabel     *prop_rot;    /* borrowed */
    LrgLabel     *prop_scale;  /* borrowed */

    /* Saved level path (for Ctrl+S) */
    gchar        *level_path;  /* owned, nullable */

    /* Orbit camera state */
    gfloat        cam_yaw;     /* degrees */
    gfloat        cam_pitch;   /* degrees */
    gfloat        cam_dist;    /* world units */
    gboolean      orbiting;    /* right-mouse held */

    /* Node cube drawables – rebuilt each refresh */
    GPtrArray    *node_cubes;  /* element: LrgCube3D*, owned */
    GPtrArray    *node_refs;   /* parallel: LrgNode* (borrowed) */
};

G_DEFINE_FINAL_TYPE (LrgEditorApp, lrg_editor_app, G_TYPE_OBJECT)

/* =========================================================================
 * Forward declarations
 * ========================================================================= */

static void rebuild_viewport_drawables (LrgEditorApp *self);
static void refresh_props_panel        (LrgEditorApp *self);
static void render_viewport            (LrgEditorApp *self);
static void handle_viewport_input      (LrgEditorApp *self);

/* =========================================================================
 * Outliner helpers
 * ========================================================================= */

static void
add_node_rows (LrgEditorApp *self,
               LrgNode      *node,
               gint          depth)
{
    GPtrArray *children;
    guint      i;
    GString   *text;
    LrgLabel  *label;

    text = g_string_new (NULL);
    for (i = 0; i < (guint) depth; i++)
        g_string_append (text, "  ");
    g_string_append (text, (lrg_node_get_name (node) != NULL)
                           ? lrg_node_get_name (node) : "(node)");

    label = lrg_label_new (text->str);
    lrg_container_add_child (LRG_CONTAINER (self->outliner), LRG_WIDGET (label));
    g_string_free (text, TRUE);

    children = lrg_node_get_children (node);
    for (i = 0; children != NULL && i < children->len; i++)
        add_node_rows (self, g_ptr_array_index (children, i), depth + 1);
}

/* =========================================================================
 * Viewport drawable rebuild
 * ========================================================================= */

static void
collect_nodes_recursive (LrgEditorApp *self,
                         LrgNode      *node)
{
    GPtrArray *children;
    guint      i;

    if (!lrg_node_get_visible (node))
        return;

    g_ptr_array_add (self->node_refs, node);

    children = lrg_node_get_children (node);
    for (i = 0; children != NULL && i < children->len; i++)
        collect_nodes_recursive (self, g_ptr_array_index (children, i));
}

static void
rebuild_viewport_drawables (LrgEditorApp *self)
{
    LrgLevel  *level;
    LrgNode   *root;
    GPtrArray *children;
    guint      i;

    g_ptr_array_set_size (self->node_cubes, 0);
    g_ptr_array_set_size (self->node_refs, 0);

    level = lrg_editor_get_level (self->editor);
    if (level == NULL)
        return;

    root = lrg_level_get_root (level);
    children = lrg_node_get_children (root);
    for (i = 0; children != NULL && i < children->len; i++)
        collect_nodes_recursive (self, g_ptr_array_index (children, i));

    for (i = 0; i < self->node_refs->len; i++)
    {
        LrgNode   *node;
        GrlVector3 *loc;
        GrlVector3 *rot;
        GrlVector3 *scl;
        LrgCube3D  *cube;

        node = g_ptr_array_index (self->node_refs, i);
        loc  = lrg_node_get_location (node);
        rot  = lrg_node_get_rotation (node);
        scl  = lrg_node_get_scale (node);

        cube = lrg_cube3d_new_at (loc->x, loc->y, loc->z,
                                  scl->x * NODE_CUBE_SIZE * 2.0f,
                                  scl->y * NODE_CUBE_SIZE * 2.0f,
                                  scl->z * NODE_CUBE_SIZE * 2.0f);
        lrg_shape3d_set_rotation_xyz (LRG_SHAPE3D (cube),
                                      rot->x, rot->y, rot->z);

        g_ptr_array_add (self->node_cubes, cube);
    }
}

/* =========================================================================
 * Properties panel refresh
 * ========================================================================= */

static void
refresh_props_panel (LrgEditorApp *self)
{
    LrgEditorSelection *sel;
    LrgNode            *primary;

    sel     = lrg_editor_get_selection (self->editor);
    primary = lrg_editor_selection_get_primary (sel);

    if (primary == NULL)
    {
        lrg_label_set_text (self->prop_name,  "Name: (none)");
        lrg_label_set_text (self->prop_pos,   "Pos:  -");
        lrg_label_set_text (self->prop_rot,   "Rot:  -");
        lrg_label_set_text (self->prop_scale, "Scale: -");
    }
    else
    {
        GrlVector3 *loc;
        GrlVector3 *rot;
        GrlVector3 *scl;
        gchar       buf[128];

        loc = lrg_node_get_location (primary);
        rot = lrg_node_get_rotation (primary);
        scl = lrg_node_get_scale (primary);

        g_snprintf (buf, sizeof (buf), "Name: %s",
                    lrg_node_get_name (primary) != NULL
                    ? lrg_node_get_name (primary) : "(node)");
        lrg_label_set_text (self->prop_name, buf);

        g_snprintf (buf, sizeof (buf), "Pos:  %.2f, %.2f, %.2f",
                    loc->x, loc->y, loc->z);
        lrg_label_set_text (self->prop_pos, buf);

        g_snprintf (buf, sizeof (buf), "Rot:  %.2f, %.2f, %.2f",
                    rot->x, rot->y, rot->z);
        lrg_label_set_text (self->prop_rot, buf);

        g_snprintf (buf, sizeof (buf), "Scale: %.2f, %.2f, %.2f",
                    scl->x, scl->y, scl->z);
        lrg_label_set_text (self->prop_scale, buf);
    }
}

/* (no standalone camera_from_orbit needed -- handled inline) */

/* =========================================================================
 * Viewport rendering
 * ========================================================================= */

static void
render_viewport (LrgEditorApp *self)
{
    LrgEditorSelection *sel;
    GrlCamera3D        *cam;
    guint               i;

    /* Dark background for the viewport region */
    {
        g_autoptr(GrlColor) vp_bg = grl_color_new (40, 44, 52, 255);
        grl_draw_rectangle (VP_X, 0, VP_W, VP_H, vp_bg);
    }

    /* Build orbit camera position from yaw/pitch/distance */
    cam = grl_camera3d_new ();
    {
        gfloat yaw_rad;
        gfloat pitch_rad;
        gfloat cos_pitch;
        gfloat px;
        gfloat py;
        gfloat pz;

        yaw_rad   = self->cam_yaw   * ((gfloat) G_PI / 180.0f);
        pitch_rad = self->cam_pitch * ((gfloat) G_PI / 180.0f);
        cos_pitch = cosf (pitch_rad);

        px = self->cam_dist * cos_pitch * sinf (yaw_rad);
        py = self->cam_dist * sinf (pitch_rad);
        pz = self->cam_dist * cos_pitch * cosf (yaw_rad);

        grl_camera3d_set_position_xyz (cam, px, py, pz);
    }
    grl_camera3d_set_target_xyz (cam, 0.0f, 0.0f, 0.0f);
    {
        g_autoptr(GrlVector3) up = grl_vector3_new (0.0f, 1.0f, 0.0f);
        grl_camera3d_set_up (cam, up);
    }
    grl_camera3d_set_fovy (cam, 45.0f);

    /* 3-D section */
    grl_camera3d_begin (cam);

    grl_draw_grid (20, 1.0f);

    sel = lrg_editor_get_selection (self->editor);

    for (i = 0; i < self->node_cubes->len; i++)
    {
        LrgCube3D             *cube;
        LrgNode               *node;
        gboolean               is_selected;
        GrlVector3            *pos;
        g_autoptr(GrlColor)    fill  = NULL;
        g_autoptr(GrlColor)    wire  = NULL;

        cube        = g_ptr_array_index (self->node_cubes, i);
        node        = g_ptr_array_index (self->node_refs,  i);
        is_selected = lrg_editor_selection_contains (sel, node);

        if (is_selected)
            fill = grl_color_new (255, 220, 50, 255);
        else
            fill = grl_color_new (130, 160, 200, 255);

        wire = grl_color_new (20, 20, 20, 255);
        pos  = lrg_shape3d_get_position (LRG_SHAPE3D (cube));

        grl_draw_cube       (pos,
                             lrg_cube3d_get_width  (cube),
                             lrg_cube3d_get_height (cube),
                             lrg_cube3d_get_depth  (cube),
                             fill);
        grl_draw_cube_wires (pos,
                             lrg_cube3d_get_width  (cube),
                             lrg_cube3d_get_height (cube),
                             lrg_cube3d_get_depth  (cube),
                             wire);
    }

    grl_camera3d_end (cam);
    g_object_unref (cam);

    /* Viewport border */
    {
        g_autoptr(GrlColor) border = grl_color_new (80, 90, 110, 255);
        grl_draw_rectangle_lines (VP_X, 0, VP_W, VP_H, border);
    }

    /* Viewport label */
    {
        gchar                buf[64];
        g_autoptr(GrlColor)  lbl  = grl_color_new (160, 175, 200, 255);

        g_snprintf (buf, sizeof (buf),
                    "3D View  [%u nodes]", self->node_refs->len);
        grl_draw_text (buf, VP_X + 8, 8, 14, lbl);
    }
}

/* =========================================================================
 * Input handling (viewport-level)
 * ========================================================================= */

static void
handle_viewport_input (LrgEditorApp *self)
{
    gboolean   ctrl;

    ctrl = grl_input_is_key_down (GRL_KEY_LEFT_CONTROL);

    /* ------------------------------------------------------------------ */
    /* Orbit camera: right-mouse drag                                      */
    /* ------------------------------------------------------------------ */
    if (grl_input_is_mouse_button_pressed (GRL_MOUSE_BUTTON_RIGHT))
        self->orbiting = TRUE;
    if (grl_input_is_mouse_button_released (GRL_MOUSE_BUTTON_RIGHT))
        self->orbiting = FALSE;

    if (self->orbiting)
    {
        g_autoptr(GrlVector2) delta = grl_input_get_mouse_delta ();
        if (delta != NULL)
        {
            self->cam_yaw   += delta->x * ORBIT_SENSITIVITY;
            self->cam_pitch -= delta->y * ORBIT_SENSITIVITY;

            /* Clamp pitch to avoid gimbal flip */
            if (self->cam_pitch >  89.0f) self->cam_pitch =  89.0f;
            if (self->cam_pitch < -89.0f) self->cam_pitch = -89.0f;
        }
    }

    /* ------------------------------------------------------------------ */
    /* Zoom: scroll wheel                                                  */
    /* ------------------------------------------------------------------ */
    {
        gfloat wheel = grl_input_get_mouse_wheel_move ();
        if (wheel != 0.0f)
        {
            self->cam_dist -= wheel * ZOOM_SENSITIVITY;
            if (self->cam_dist < MIN_DISTANCE) self->cam_dist = MIN_DISTANCE;
            if (self->cam_dist > MAX_DISTANCE) self->cam_dist = MAX_DISTANCE;
        }
    }

    /* ------------------------------------------------------------------ */
    /* Node selection: left-click in the viewport area                    */
    /* Cycle through visible nodes for now (real ray-cast is future work) */
    /* ------------------------------------------------------------------ */
    if (grl_input_is_mouse_button_pressed (GRL_MOUSE_BUTTON_LEFT))
    {
        gint mx = grl_input_get_mouse_x ();
        gint my = grl_input_get_mouse_y ();

        if (mx >= VP_X && mx < VP_X + VP_W &&
            my >= 0    && my < VP_H &&
            self->node_refs->len > 0)
        {
            LrgEditorSelection *sel;
            LrgNode            *primary;
            guint               idx;
            guint               n;

            sel     = lrg_editor_get_selection (self->editor);
            primary = lrg_editor_selection_get_primary (sel);
            n       = self->node_refs->len;

            /* Find index of current primary selection, advance to next */
            idx = 0;
            if (primary != NULL)
            {
                guint j;
                for (j = 0; j < n; j++)
                {
                    if (g_ptr_array_index (self->node_refs, j) == primary)
                    {
                        idx = (j + 1) % n;
                        break;
                    }
                }
            }

            lrg_editor_select (self->editor,
                               g_ptr_array_index (self->node_refs, idx),
                               FALSE);
            refresh_props_panel (self);
        }
    }

    /* ------------------------------------------------------------------ */
    /* Keyboard shortcuts                                                  */
    /* ------------------------------------------------------------------ */

    /* N -- add a new cube node */
    if (grl_input_is_key_pressed (GRL_KEY_N) && !ctrl)
    {
        LrgNode  *node;
        gchar    *name;
        guint     count;

        count = self->node_refs->len;
        name  = g_strdup_printf ("Node%u", count + 1);
        node  = lrg_node_new (name);
        g_free (name);

        {
            LrgNodeVisual *vis = lrg_node_visual_new (LRG_NODE_VISUAL_PRIMITIVE);
            lrg_node_visual_set_primitive (vis, LRG_PRIMITIVE_CUBE);
            lrg_node_set_visual (node, vis);
            g_object_unref (vis);
        }

        lrg_editor_add_node (self->editor, node, NULL);
        g_object_unref (node);

        rebuild_viewport_drawables (self);
        lrg_editor_app_refresh (self);
    }

    /* Delete -- delete primary selection */
    if (grl_input_is_key_pressed (GRL_KEY_DELETE))
    {
        LrgEditorSelection *sel;
        LrgNode            *primary;

        sel     = lrg_editor_get_selection (self->editor);
        primary = lrg_editor_selection_get_primary (sel);
        if (primary != NULL)
        {
            lrg_editor_delete_node (self->editor, primary);
            rebuild_viewport_drawables (self);
            lrg_editor_app_refresh (self);
        }
    }

    /* Ctrl+Z -- undo */
    if (ctrl && grl_input_is_key_pressed (GRL_KEY_Z))
    {
        if (lrg_editor_can_undo (self->editor))
        {
            lrg_editor_undo (self->editor);
            rebuild_viewport_drawables (self);
            lrg_editor_app_refresh (self);
        }
    }

    /* Ctrl+Y -- redo */
    if (ctrl && grl_input_is_key_pressed (GRL_KEY_Y))
    {
        if (lrg_editor_can_redo (self->editor))
        {
            lrg_editor_redo (self->editor);
            rebuild_viewport_drawables (self);
            lrg_editor_app_refresh (self);
        }
    }

    /* Ctrl+S -- save */
    if (ctrl && grl_input_is_key_pressed (GRL_KEY_S))
    {
        if (self->level_path != NULL)
        {
            g_autoptr(GError) err = NULL;
            if (!lrg_editor_save_level (self->editor, self->level_path, &err))
                g_warning ("Save failed: %s", err->message);
        }
    }
}

/* =========================================================================
 * Signal handlers
 * ========================================================================= */

static void
on_editor_changed (LrgEditor *editor,
                   gpointer   user_data)
{
    LrgEditorApp *self = LRG_EDITOR_APP (user_data);

    (void)editor;

    rebuild_viewport_drawables (self);
    lrg_editor_app_refresh (self);
}

/* =========================================================================
 * GObject lifecycle
 * ========================================================================= */

static void
lrg_editor_app_dispose (GObject *object)
{
    LrgEditorApp *self = LRG_EDITOR_APP (object);

    if (self->editor != NULL)
        g_signal_handlers_disconnect_by_data (self->editor, self);

    g_clear_object (&self->editor);
    g_clear_object (&self->canvas);
    self->outliner   = NULL;
    self->props      = NULL;
    self->prop_name  = NULL;
    self->prop_pos   = NULL;
    self->prop_rot   = NULL;
    self->prop_scale = NULL;

    if (self->node_cubes != NULL)
    {
        g_ptr_array_unref (self->node_cubes);
        self->node_cubes = NULL;
    }

    if (self->node_refs != NULL)
    {
        g_ptr_array_unref (self->node_refs);
        self->node_refs = NULL;
    }

    g_clear_pointer (&self->level_path, g_free);

    G_OBJECT_CLASS (lrg_editor_app_parent_class)->dispose (object);
}

static void
lrg_editor_app_class_init (LrgEditorAppClass *klass)
{
    G_OBJECT_CLASS (klass)->dispose = lrg_editor_app_dispose;
}

static void
lrg_editor_app_init (LrgEditorApp *self)
{
    self->cam_yaw   = DEFAULT_YAW;
    self->cam_pitch = DEFAULT_PITCH;
    self->cam_dist  = DEFAULT_DIST;
    self->orbiting  = FALSE;
    self->level_path = NULL;

    self->node_cubes = g_ptr_array_new_with_free_func (g_object_unref);
    self->node_refs  = g_ptr_array_new ();
}

/* =========================================================================
 * Public API
 * ========================================================================= */

/**
 * lrg_editor_app_new:
 * @editor: (nullable) (transfer none): editor to drive, or %NULL
 *
 * Returns: (transfer full): a new #LrgEditorApp
 */
LrgEditorApp *
lrg_editor_app_new (LrgEditor *editor)
{
    LrgEditorApp *self;
    LrgHBox      *root_hbox;
    LrgLabel     *title;
    LrgLabel     *props_title;

    self = g_object_new (LRG_TYPE_EDITOR_APP, NULL);

    self->editor = (editor != NULL) ? g_object_ref (editor) : lrg_editor_new ();
    self->canvas = lrg_canvas_new ();

    /* -------------------------------------------------------------------
     * Root horizontal layout (outliner | [viewport gap] | props)
     * The viewport is rendered directly by render_viewport(); it is NOT a
     * widget child -- we leave a blank gap in the middle of the HBox.
     * ------------------------------------------------------------------- */
    root_hbox = lrg_hbox_new ();
    lrg_widget_set_position (LRG_WIDGET (root_hbox), 0.0f, 0.0f);
    lrg_widget_set_size (LRG_WIDGET (root_hbox),
                         (gfloat) EDITOR_WIN_W, (gfloat) EDITOR_WIN_H);
    lrg_container_add_child (LRG_CONTAINER (self->canvas),
                             LRG_WIDGET (root_hbox));

    /* -------------------------------------------------------------------
     * Left panel: outliner
     * ------------------------------------------------------------------- */
    self->outliner = lrg_vbox_new ();
    lrg_widget_set_size (LRG_WIDGET (self->outliner), OUTLINER_W, PANEL_H);

    title = lrg_label_new ("-- Outliner --");
    lrg_container_add_child (LRG_CONTAINER (self->outliner), LRG_WIDGET (title));
    lrg_container_add_child (LRG_CONTAINER (root_hbox),
                             LRG_WIDGET (self->outliner));

    /* -------------------------------------------------------------------
     * Right panel: properties
     * ------------------------------------------------------------------- */
    self->props = lrg_vbox_new ();
    lrg_widget_set_size (LRG_WIDGET (self->props), PROPS_W, PANEL_H);

    props_title = lrg_label_new ("-- Properties --");
    lrg_container_add_child (LRG_CONTAINER (self->props),
                             LRG_WIDGET (props_title));

    self->prop_name  = lrg_label_new ("Name: (none)");
    self->prop_pos   = lrg_label_new ("Pos:  -");
    self->prop_rot   = lrg_label_new ("Rot:  -");
    self->prop_scale = lrg_label_new ("Scale: -");

    lrg_container_add_child (LRG_CONTAINER (self->props),
                             LRG_WIDGET (self->prop_name));
    lrg_container_add_child (LRG_CONTAINER (self->props),
                             LRG_WIDGET (self->prop_pos));
    lrg_container_add_child (LRG_CONTAINER (self->props),
                             LRG_WIDGET (self->prop_rot));
    lrg_container_add_child (LRG_CONTAINER (self->props),
                             LRG_WIDGET (self->prop_scale));

    lrg_container_add_child (LRG_CONTAINER (root_hbox),
                             LRG_WIDGET (self->props));

    /* -------------------------------------------------------------------
     * Wire up editor signal
     * ------------------------------------------------------------------- */
    g_signal_connect (self->editor, "changed",
                      G_CALLBACK (on_editor_changed), self);

    /* Initial populate */
    rebuild_viewport_drawables (self);
    lrg_editor_app_refresh (self);

    return self;
}

/**
 * lrg_editor_app_get_editor:
 * @self: an #LrgEditorApp
 *
 * Returns: (transfer none): the editor runtime
 */
LrgEditor *
lrg_editor_app_get_editor (LrgEditorApp *self)
{
    g_return_val_if_fail (LRG_IS_EDITOR_APP (self), NULL);
    return self->editor;
}

/**
 * lrg_editor_app_get_canvas:
 * @self: an #LrgEditorApp
 *
 * Returns: (transfer none): the root #LrgCanvas
 */
LrgCanvas *
lrg_editor_app_get_canvas (LrgEditorApp *self)
{
    g_return_val_if_fail (LRG_IS_EDITOR_APP (self), NULL);
    return self->canvas;
}

/**
 * lrg_editor_app_refresh:
 * @self: an #LrgEditorApp
 *
 * Rebuilds the outliner and properties panels from the current level tree
 * and selection.
 */
void
lrg_editor_app_refresh (LrgEditorApp *self)
{
    LrgLevel  *level;
    LrgNode   *root;
    GPtrArray *children;
    guint      i;
    LrgLabel  *title;

    g_return_if_fail (LRG_IS_EDITOR_APP (self));

    /* Rebuild outliner */
    lrg_container_remove_all (LRG_CONTAINER (self->outliner));

    title = lrg_label_new ("-- Outliner --");
    lrg_container_add_child (LRG_CONTAINER (self->outliner), LRG_WIDGET (title));

    level = lrg_editor_get_level (self->editor);
    if (level != NULL)
    {
        root     = lrg_level_get_root (level);
        children = lrg_node_get_children (root);
        for (i = 0; children != NULL && i < children->len; i++)
            add_node_rows (self, g_ptr_array_index (children, i), 0);
    }

    /* Refresh properties */
    refresh_props_panel (self);
}

/**
 * lrg_editor_app_handle_input:
 * @self: an #LrgEditorApp
 *
 * Dispatches pending input to the UI and 3D viewport (call once per frame).
 */
void
lrg_editor_app_handle_input (LrgEditorApp *self)
{
    g_return_if_fail (LRG_IS_EDITOR_APP (self));

    lrg_canvas_handle_input (self->canvas);
    handle_viewport_input (self);
}

/**
 * lrg_editor_app_render:
 * @self: an #LrgEditorApp
 *
 * Renders the 3D viewport and all UI panels for one frame.
 */
void
lrg_editor_app_render (LrgEditorApp *self)
{
    g_return_if_fail (LRG_IS_EDITOR_APP (self));

    /* Draw 3-D viewport first (may be clipped by scissor in future) */
    render_viewport (self);

    /* Then draw UI panels on top */
    lrg_canvas_render (self->canvas);
}

/**
 * lrg_editor_app_run:
 * @self: an #LrgEditorApp
 *
 * Opens a window and runs the editor loop until the user closes it.
 * Checks for a graphical display and exits cleanly if none is available.
 */
void
lrg_editor_app_run (LrgEditorApp *self)
{
    GrlWindow *window;

    g_return_if_fail (LRG_IS_EDITOR_APP (self));

    /* Display guard: exit cleanly in headless / CI environments.
     * Treat an unset *or empty* env var as "no display". */
    {
        const gchar *display  = g_getenv ("DISPLAY");
        const gchar *wayland  = g_getenv ("WAYLAND_DISPLAY");
        gboolean     has_disp;

        has_disp = (display != NULL && display[0] != '\0') ||
                   (wayland != NULL && wayland[0] != '\0');

        if (!has_disp)
        {
            fprintf (stderr,
                     "libregnum editor: no graphical display (DISPLAY/WAYLAND_DISPLAY "
                     "not set). Skipping window creation.\n");
            return;
        }
    }

    window = grl_window_new (EDITOR_WIN_W, EDITOR_WIN_H,
                             "Libregnum Editor");
    grl_window_set_target_fps (window, 60);

    /* Disable default ESC-to-quit so ESC can be used by the editor */
    grl_input_set_exit_key (GRL_KEY_NULL);

    while (!grl_window_should_close (window))
    {
        /* ESC still quits from our side */
        if (grl_input_is_key_pressed (GRL_KEY_ESCAPE))
            break;

        lrg_editor_app_handle_input (self);

        grl_window_begin_drawing (window);

        {
            g_autoptr(GrlColor) bg = grl_color_new (30, 33, 40, 255);
            grl_draw_clear_background (bg);
        }

        lrg_editor_app_render (self);

        {
            gchar                fps_buf[32];
            g_autoptr(GrlColor)  fps_col = grl_color_new (180, 200, 180, 255);
            g_snprintf (fps_buf, sizeof (fps_buf),
                        "FPS: %d", grl_window_get_fps (window));
            grl_draw_text (fps_buf,
                           EDITOR_WIN_W - 80, EDITOR_WIN_H - 20,
                           14, fps_col);
        }

        grl_window_end_drawing (window);
    }

    g_object_unref (window);
}
