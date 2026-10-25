/* lrg-node.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Editable node in an #LrgLevel authoring tree.
 *
 * LrgNode is the single unifier of 2D and 3D content in the editor document.
 * Every node carries an always-3D local TRS transform (2D nodes simply live on
 * Z=0 with #LrgNode:is-2d set), an ordered list of child nodes, an optional
 * tagged #LrgNodeVisual payload, and ordered lists of component descriptions
 * (#LrgComponentDesc) and script bindings (#LrgScriptBinding) that are
 * materialized onto a runtime #LrgGameObject when the level is played.
 *
 * LrgNode is derivable: custom node kinds may subclass it and override the
 * @instantiate virtual method to build their own runtime representation.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <graylib.h>
#include "../lrg-version.h"
#include "../lrg-types.h"

G_BEGIN_DECLS

#define LRG_TYPE_NODE (lrg_node_get_type ())

/**
 * LrgNodeForeachFunc:
 * @node: the node being visited
 * @user_data: (closure): user data
 *
 * Callback for lrg_node_foreach().
 *
 * Returns: %TRUE to continue walking, %FALSE to stop
 */
typedef gboolean (*LrgNodeForeachFunc) (LrgNode  *node,
                                        gpointer  user_data);

LRG_AVAILABLE_IN_ALL
G_DECLARE_DERIVABLE_TYPE (LrgNode, lrg_node, LRG, NODE, GObject)

/**
 * LrgNodeClass:
 * @parent_class: the parent class
 * @instantiate: virtual method to build a runtime representation of this node
 *   onto a game object (called by lrg_level_instantiate())
 *
 * The class structure for #LrgNode.
 */
struct _LrgNodeClass
{
    GObjectClass parent_class;

    /**
     * LrgNodeClass::instantiate:
     * @self: the node
     * @object: the runtime #LrgGameObject being built for this node
     *
     * Called during play-in-editor to materialize this node's runtime
     * representation onto @object. The default implementation applies the
     * node's component descriptions. Subclasses may chain up and add their
     * own behavior.
     */
    void (*instantiate) (LrgNode       *self,
                         LrgGameObject *object);

    /*< private >*/
    gpointer _reserved[8];
};

/* ==========================================================================
 * Constructors
 * ========================================================================== */

/**
 * lrg_node_new:
 * @name: (nullable): the node name
 *
 * Creates a new #LrgNode with an automatically generated guid, identity
 * transform, and no visual.
 *
 * Returns: (transfer full): a new #LrgNode
 */
LRG_AVAILABLE_IN_ALL
LrgNode * lrg_node_new (const gchar *name);

/* ==========================================================================
 * Identity
 * ========================================================================== */

/**
 * lrg_node_get_name:
 * @self: an #LrgNode
 *
 * Returns: (transfer none) (nullable): the node name
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_node_get_name (LrgNode *self);

/**
 * lrg_node_set_name:
 * @self: an #LrgNode
 * @name: (nullable): the new name
 */
LRG_AVAILABLE_IN_ALL
void lrg_node_set_name (LrgNode     *self,
                        const gchar *name);

/**
 * lrg_node_get_guid:
 * @self: an #LrgNode
 *
 * Gets the stable unique identifier of the node.
 *
 * Returns: (transfer none): the guid
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_node_get_guid (LrgNode *self);

/**
 * lrg_node_set_guid:
 * @self: an #LrgNode
 * @guid: the guid (e.g. loaded from a serialized level)
 *
 * Sets the stable unique identifier of the node.
 */
LRG_AVAILABLE_IN_ALL
void lrg_node_set_guid (LrgNode     *self,
                        const gchar *guid);

/* ==========================================================================
 * Flags
 * ========================================================================== */

/**
 * lrg_node_get_visible:
 * @self: an #LrgNode
 *
 * Returns: %TRUE if the node is visible
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_node_get_visible (LrgNode *self);

/**
 * lrg_node_set_visible:
 * @self: an #LrgNode
 * @visible: visibility flag
 */
LRG_AVAILABLE_IN_ALL
void lrg_node_set_visible (LrgNode  *self,
                           gboolean  visible);

/**
 * lrg_node_get_locked:
 * @self: an #LrgNode
 *
 * Returns: %TRUE if the node is locked against editing
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_node_get_locked (LrgNode *self);

/**
 * lrg_node_set_locked:
 * @self: an #LrgNode
 * @locked: locked flag
 */
LRG_AVAILABLE_IN_ALL
void lrg_node_set_locked (LrgNode  *self,
                          gboolean  locked);

/**
 * lrg_node_get_is_2d:
 * @self: an #LrgNode
 *
 * Returns: %TRUE if the node is authored as 2D (gizmo/camera hint)
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_node_get_is_2d (LrgNode *self);

/**
 * lrg_node_set_is_2d:
 * @self: an #LrgNode
 * @is_2d: 2D hint flag
 */
LRG_AVAILABLE_IN_ALL
void lrg_node_set_is_2d (LrgNode  *self,
                         gboolean  is_2d);

/* ==========================================================================
 * Transform (local TRS)
 * ========================================================================== */

/**
 * lrg_node_get_location:
 * @self: an #LrgNode
 *
 * Returns: (transfer none): the local position vector
 */
LRG_AVAILABLE_IN_ALL
GrlVector3 * lrg_node_get_location (LrgNode *self);

/**
 * lrg_node_set_location_xyz:
 * @self: an #LrgNode
 * @x: X coordinate
 * @y: Y coordinate
 * @z: Z coordinate
 */
LRG_AVAILABLE_IN_ALL
void lrg_node_set_location_xyz (LrgNode *self,
                                gfloat   x,
                                gfloat   y,
                                gfloat   z);

/**
 * lrg_node_get_rotation:
 * @self: an #LrgNode
 *
 * Returns: (transfer none): the local rotation vector (Euler radians)
 */
LRG_AVAILABLE_IN_ALL
GrlVector3 * lrg_node_get_rotation (LrgNode *self);

/**
 * lrg_node_set_rotation_xyz:
 * @self: an #LrgNode
 * @rx: X rotation (radians)
 * @ry: Y rotation (radians)
 * @rz: Z rotation (radians)
 */
LRG_AVAILABLE_IN_ALL
void lrg_node_set_rotation_xyz (LrgNode *self,
                                gfloat   rx,
                                gfloat   ry,
                                gfloat   rz);

/**
 * lrg_node_get_scale:
 * @self: an #LrgNode
 *
 * Returns: (transfer none): the local scale vector
 */
LRG_AVAILABLE_IN_ALL
GrlVector3 * lrg_node_get_scale (LrgNode *self);

/**
 * lrg_node_set_scale_xyz:
 * @self: an #LrgNode
 * @sx: X scale factor
 * @sy: Y scale factor
 * @sz: Z scale factor
 */
LRG_AVAILABLE_IN_ALL
void lrg_node_set_scale_xyz (LrgNode *self,
                             gfloat   sx,
                             gfloat   sy,
                             gfloat   sz);

/* ==========================================================================
 * Hierarchy
 * ========================================================================== */

/**
 * lrg_node_get_parent:
 * @self: an #LrgNode
 *
 * Gets the parent node, if any. The parent is a borrowed back-pointer and is
 * not owned by @self.
 *
 * Returns: (transfer none) (nullable): the parent node
 */
LRG_AVAILABLE_IN_ALL
LrgNode * lrg_node_get_parent (LrgNode *self);

/**
 * lrg_node_add_child:
 * @self: an #LrgNode
 * @child: (transfer none): the child to append
 *
 * Appends @child to @self's ordered child list and sets the child's parent to
 * @self. A reference is taken on @child.
 */
LRG_AVAILABLE_IN_ALL
void lrg_node_add_child (LrgNode *self,
                         LrgNode *child);

/**
 * lrg_node_remove_child:
 * @self: an #LrgNode
 * @child: the child to remove
 *
 * Removes @child from @self's child list and clears its parent pointer.
 *
 * Returns: %TRUE if the child was found and removed
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_node_remove_child (LrgNode *self,
                                LrgNode *child);

/**
 * lrg_node_get_children:
 * @self: an #LrgNode
 *
 * Returns: (transfer none) (element-type LrgNode): the ordered child array
 */
LRG_AVAILABLE_IN_ALL
GPtrArray * lrg_node_get_children (LrgNode *self);

/**
 * lrg_node_get_n_children:
 * @self: an #LrgNode
 *
 * Returns: the number of child nodes
 */
LRG_AVAILABLE_IN_ALL
guint lrg_node_get_n_children (LrgNode *self);

/**
 * lrg_node_find_by_guid:
 * @self: an #LrgNode
 * @guid: the guid to search for
 *
 * Recursively searches @self and its descendants for a node with @guid.
 *
 * Returns: (transfer none) (nullable): the matching node, or %NULL
 */
LRG_AVAILABLE_IN_ALL
LrgNode * lrg_node_find_by_guid (LrgNode     *self,
                                 const gchar *guid);

/* ==========================================================================
 * Visual
 * ========================================================================== */

/**
 * lrg_node_get_visual:
 * @self: an #LrgNode
 *
 * Returns: (transfer none) (nullable): the node's visual payload
 */
LRG_AVAILABLE_IN_ALL
LrgNodeVisual * lrg_node_get_visual (LrgNode *self);

/**
 * lrg_node_set_visual:
 * @self: an #LrgNode
 * @visual: (transfer none) (nullable): the visual payload
 *
 * Sets the node's visual payload. A reference is taken on @visual.
 */
LRG_AVAILABLE_IN_ALL
void lrg_node_set_visual (LrgNode       *self,
                          LrgNodeVisual *visual);

/* ==========================================================================
 * Components and Scripts
 * ========================================================================== */

/**
 * lrg_node_add_component:
 * @self: an #LrgNode
 * @desc: (transfer none): the component description to append
 *
 * Appends a component description. A reference is taken on @desc.
 */
LRG_AVAILABLE_IN_ALL
void lrg_node_add_component (LrgNode          *self,
                             LrgComponentDesc *desc);

/**
 * lrg_node_get_components:
 * @self: an #LrgNode
 *
 * Returns: (transfer none) (element-type LrgComponentDesc): component array
 */
LRG_AVAILABLE_IN_ALL
GPtrArray * lrg_node_get_components (LrgNode *self);

/**
 * lrg_node_add_script:
 * @self: an #LrgNode
 * @binding: (transfer none): the script binding to append
 *
 * Appends a script binding. A reference is taken on @binding.
 */
LRG_AVAILABLE_IN_ALL
void lrg_node_add_script (LrgNode          *self,
                          LrgScriptBinding *binding);

/**
 * lrg_node_get_scripts:
 * @self: an #LrgNode
 *
 * Returns: (transfer none) (element-type LrgScriptBinding): script array
 */
LRG_AVAILABLE_IN_ALL
GPtrArray * lrg_node_get_scripts (LrgNode *self);

/**
 * lrg_node_insert_component:
 * @self: an #LrgNode
 * @desc: (transfer none): the component description to insert
 * @index: position to insert at, or -1 to append
 *
 * Inserts a component description at @index. A reference is taken on @desc.
 */
LRG_AVAILABLE_IN_ALL
void lrg_node_insert_component (LrgNode          *self,
                                LrgComponentDesc *desc,
                                gint              index);

/**
 * lrg_node_remove_component_at:
 * @self: an #LrgNode
 * @index: index of the component description to remove
 *
 * Removes the component description at @index.
 *
 * Returns: %TRUE if a component was removed
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_node_remove_component_at (LrgNode *self,
                                       guint    index);

/**
 * lrg_node_find_component:
 * @self: an #LrgNode
 * @type_name: the registry type name to look for
 *
 * Returns: (transfer none) (nullable): the first component description whose
 *   type name is @type_name, or %NULL
 */
LRG_AVAILABLE_IN_ALL
LrgComponentDesc * lrg_node_find_component (LrgNode     *self,
                                            const gchar *type_name);

/**
 * lrg_node_insert_script:
 * @self: an #LrgNode
 * @binding: (transfer none): the script binding to insert
 * @index: position to insert at, or -1 to append
 *
 * Inserts a script binding at @index. A reference is taken on @binding.
 */
LRG_AVAILABLE_IN_ALL
void lrg_node_insert_script (LrgNode          *self,
                             LrgScriptBinding *binding,
                             gint              index);

/**
 * lrg_node_remove_script_at:
 * @self: an #LrgNode
 * @index: index of the script binding to remove
 *
 * Removes the script binding at @index.
 *
 * Returns: %TRUE if a script binding was removed
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_node_remove_script_at (LrgNode *self,
                                    guint    index);

/* ==========================================================================
 * Tree editing helpers
 * ========================================================================== */

/**
 * lrg_node_insert_child:
 * @self: an #LrgNode
 * @child: (transfer none): the node to insert
 * @index: position among the children, or -1 to append
 *
 * Inserts @child into the children of @self at @index. An out-of-range
 * @index appends. A reference is taken on @child.
 */
LRG_AVAILABLE_IN_ALL
void lrg_node_insert_child (LrgNode *self,
                            LrgNode *child,
                            gint     index);

/**
 * lrg_node_get_child_index:
 * @self: an #LrgNode
 * @child: a node
 *
 * Returns: the index of @child among the children of @self, or -1
 */
LRG_AVAILABLE_IN_ALL
gint lrg_node_get_child_index (LrgNode *self,
                               LrgNode *child);

/**
 * lrg_node_is_ancestor_of:
 * @self: an #LrgNode
 * @node: a node
 *
 * Returns: %TRUE if @self is @node or one of its ancestors
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_node_is_ancestor_of (LrgNode *self,
                                  LrgNode *node);

/**
 * lrg_node_get_depth:
 * @self: an #LrgNode
 *
 * Returns: the number of ancestors of @self (0 for a detached or root node)
 */
LRG_AVAILABLE_IN_ALL
guint lrg_node_get_depth (LrgNode *self);

/**
 * lrg_node_get_path:
 * @self: an #LrgNode
 *
 * Builds a slash-separated path of node names from the top-most named
 * ancestor down to @self (the unnamed implicit level root is skipped), for
 * example `"world/house/door"`. Unnamed nodes appear as `"(node)"`.
 *
 * Returns: (transfer full): the node path
 */
LRG_AVAILABLE_IN_ALL
gchar * lrg_node_get_path (LrgNode *self);

/**
 * lrg_node_find_by_path:
 * @self: the node to search under
 * @path: a slash-separated path of child names relative to @self
 *
 * Returns: (transfer none) (nullable): the first node matching @path
 */
LRG_AVAILABLE_IN_ALL
LrgNode * lrg_node_find_by_path (LrgNode     *self,
                                 const gchar *path);

/**
 * lrg_node_copy_state:
 * @self: the destination node
 * @source: the node to copy from
 *
 * Copies the node's own editable state from @source into @self: name, flags,
 * local transform, and deep copies of the visual payload, component
 * descriptions and script bindings. The GUID, parent and children of @self
 * are left untouched. This is the primitive behind the editor's
 * snapshot-based undo (see lrg_editor_snapshot_node()).
 */
LRG_AVAILABLE_IN_ALL
void lrg_node_copy_state (LrgNode *self,
                          LrgNode *source);

/**
 * lrg_node_state_equal:
 * @self: an #LrgNode
 * @other: another #LrgNode
 *
 * Compares the own editable state of two nodes (everything
 * lrg_node_copy_state() copies). Children and GUIDs are ignored.
 *
 * Returns: %TRUE if the states are equal
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_node_state_equal (LrgNode *self,
                               LrgNode *other);

/**
 * lrg_node_foreach:
 * @self: an #LrgNode
 * @func: (scope call) (closure user_data): function called for @self and
 *   every descendant in depth-first pre-order; return %FALSE to stop the walk
 * @user_data: data passed to @func
 *
 * Walks the subtree rooted at @self.
 *
 * Returns: %FALSE if @func stopped the walk early
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_node_foreach (LrgNode         *self,
                           LrgNodeForeachFunc func,
                           gpointer         user_data);

/* ==========================================================================
 * Instantiation
 * ========================================================================== */

/**
 * lrg_node_instantiate:
 * @self: an #LrgNode
 * @object: the runtime #LrgGameObject to populate
 *
 * Invokes the @instantiate virtual method to materialize this node onto a
 * runtime game object. Used by lrg_level_instantiate().
 */
LRG_AVAILABLE_IN_ALL
void lrg_node_instantiate (LrgNode       *self,
                           LrgGameObject *object);

G_END_DECLS
