/* lrg-bt-asset.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgBtAsset: the .rbt behaviour tree asset. A tree of LrgBtAssetNode
 * descriptions (composites, decorators, leaves with parameters, subtree
 * references), a typed blackboard schema and named subtrees. Leaves bind
 * by name to C actions and conditions registered with
 * lrg_bt_register_action() / lrg_bt_register_condition(), or to script
 * functions called through an LrgScripting context. An asset builds
 * LrgBehaviorTree instances; LrgBtRunner wraps one with the per-node,
 * per-tick debug state a live tree view needs.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-enums.h"
#include "lrg-blackboard.h"
#include "lrg-bt-node.h"
#include "lrg-behavior-tree.h"
#include "../scripting/lrg-scripting.h"

G_BEGIN_DECLS

/* ==========================================================================
 * Errors and enumerations
 * ========================================================================== */

/**
 * LRG_BT_ASSET_ERROR:
 *
 * Error domain for #LrgBtAsset and #LrgBtRunner.
 *
 * Since: 0.9.0
 */
#define LRG_BT_ASSET_ERROR (lrg_bt_asset_error_quark ())

/**
 * lrg_bt_asset_error_quark:
 *
 * Gets the error quark for #LrgBtAsset.
 *
 * Returns: the quark
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
GQuark lrg_bt_asset_error_quark (void);

/**
 * LrgBtAssetError:
 * @LRG_BT_ASSET_ERROR_PARSE: the text is not YAML or not a tree mapping
 * @LRG_BT_ASSET_ERROR_VERSION: the file's version is newer than this
 *   build reads
 * @LRG_BT_ASSET_ERROR_INVALID: a value has the wrong type or range, a
 *   required key is missing, a node has the wrong number of children, or
 *   an id is duplicated
 * @LRG_BT_ASSET_ERROR_REFERENCE: a subtree, node type or blackboard key
 *   refers to something that does not exist, or subtrees form a cycle
 * @LRG_BT_ASSET_ERROR_BINDING: a leaf names an action, condition or script
 *   function that is not registered (see lrg_bt_asset_set_allow_unbound())
 * @LRG_BT_ASSET_ERROR_IO: reading or writing a file failed
 *
 * Error codes in the %LRG_BT_ASSET_ERROR domain. Messages start with the
 * path of the offending key, for example
 * `root.children[1].child: decorator 'inverter' needs exactly one child`.
 *
 * Since: 0.9.0
 */
typedef enum
{
	LRG_BT_ASSET_ERROR_PARSE,
	LRG_BT_ASSET_ERROR_VERSION,
	LRG_BT_ASSET_ERROR_INVALID,
	LRG_BT_ASSET_ERROR_REFERENCE,
	LRG_BT_ASSET_ERROR_BINDING,
	LRG_BT_ASSET_ERROR_IO
} LrgBtAssetError;

LRG_AVAILABLE_IN_ALL
GType lrg_bt_asset_error_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_BT_ASSET_ERROR (lrg_bt_asset_error_get_type ())

/**
 * LrgBtNodeKind:
 * @LRG_BT_NODE_KIND_COMPOSITE: ticks a list of children (`sequence`,
 *   `selector`, `parallel`, custom #LrgBTComposite types)
 * @LRG_BT_NODE_KIND_DECORATOR: wraps exactly one child (`inverter`,
 *   `repeater`, `succeeder`, `failer`, `cooldown`, `time_limit`, `guard`,
 *   custom #LrgBTDecorator types)
 * @LRG_BT_NODE_KIND_LEAF: does the work (`action`, `condition`, `check`,
 *   `set`, `wait`, custom leaf types)
 * @LRG_BT_NODE_KIND_SUBTREE: expands to a named subtree (`subtree`)
 *
 * The structural role of a node type.
 *
 * Since: 0.9.0
 */
typedef enum
{
	LRG_BT_NODE_KIND_COMPOSITE,
	LRG_BT_NODE_KIND_DECORATOR,
	LRG_BT_NODE_KIND_LEAF,
	LRG_BT_NODE_KIND_SUBTREE
} LrgBtNodeKind;

LRG_AVAILABLE_IN_ALL
GType lrg_bt_node_kind_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_BT_NODE_KIND (lrg_bt_node_kind_get_type ())

/**
 * LRG_BT_ASSET_FORMAT_VERSION:
 *
 * The .rbt format version this build writes (and the newest it reads).
 *
 * Since: 0.9.0
 */
#define LRG_BT_ASSET_FORMAT_VERSION (1)

/**
 * lrg_bt_status_to_string:
 * @status: a status
 *
 * Returns: (transfer none): `invalid`, `success`, `failure` or `running`
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_bt_status_to_string (LrgBTStatus status);

/* ==========================================================================
 * The binding registry
 * ========================================================================== */

/**
 * LrgBtBoundActionFunc:
 * @blackboard: the tree's blackboard
 * @delta_time: the tick's delta in seconds (0 for state machine enter and
 *   exit actions)
 * @params: the node's `params:` as an `a{sv}` dictionary (never %NULL;
 *   empty when the node has none). Plain YAML scalars arrive as booleans
 *   (`b`), 64-bit integers (`x`), doubles (`d`) or strings (`s`); quoted
 *   scalars are always strings
 * @user_data: the data given at registration
 *
 * A C action a `.rbt` `action` leaf (or a `.rfsm` action) binds to by name.
 * State machines ignore the returned status.
 *
 * Returns: the leaf's status for this tick
 *
 * Since: 0.9.0
 */
typedef LrgBTStatus (*LrgBtBoundActionFunc) (LrgBlackboard *blackboard,
                                             gfloat         delta_time,
                                             GVariant      *params,
                                             gpointer       user_data);

/**
 * LrgBtBoundConditionFunc:
 * @blackboard: the tree's blackboard
 * @params: the node's `params:` (`a{sv}`, never %NULL)
 * @user_data: the data given at registration
 *
 * A C condition a `.rbt` `condition` leaf (or a `.rfsm` `when:` entry)
 * binds to by name.
 *
 * Returns: %TRUE when the condition holds
 *
 * Since: 0.9.0
 */
typedef gboolean (*LrgBtBoundConditionFunc) (LrgBlackboard *blackboard,
                                             GVariant      *params,
                                             gpointer       user_data);

/**
 * lrg_bt_register_action:
 * @name: the name leaves use (`action: name`)
 * @func: (scope notified) (closure user_data) (destroy destroy): the action
 * @user_data: data passed to @func
 * @destroy: (nullable): frees @user_data when the action is replaced or
 *   unregistered and no built tree uses it any more
 *
 * Registers (or replaces) a named C action in the process-wide registry
 * shared by `.rbt` trees and `.rfsm` machines. Trees bind at build time:
 * a tree keeps the action it was built with even if the name is replaced
 * later. Thread-safe.
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
void          lrg_bt_register_action      (const gchar           *name,
                                           LrgBtBoundActionFunc   func,
                                           gpointer               user_data,
                                           GDestroyNotify         destroy);

/**
 * lrg_bt_register_condition:
 * @name: the name leaves use (`condition: name`)
 * @func: (scope notified) (closure user_data) (destroy destroy): the
 *   condition
 * @user_data: data passed to @func
 * @destroy: (nullable): frees @user_data when no longer used
 *
 * Registers (or replaces) a named C condition. See
 * lrg_bt_register_action().
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
void          lrg_bt_register_condition   (const gchar             *name,
                                           LrgBtBoundConditionFunc  func,
                                           gpointer                 user_data,
                                           GDestroyNotify           destroy);

/**
 * lrg_bt_unregister_action:
 * @name: an action name
 *
 * Removes a registered action (trees already built keep theirs).
 *
 * Returns: %TRUE if @name was registered
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
gboolean      lrg_bt_unregister_action    (const gchar *name);

/**
 * lrg_bt_unregister_condition:
 * @name: a condition name
 *
 * Removes a registered condition (trees already built keep theirs).
 *
 * Returns: %TRUE if @name was registered
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
gboolean      lrg_bt_unregister_condition (const gchar *name);

/**
 * lrg_bt_has_action:
 * @name: an action name
 *
 * Returns: %TRUE if an action is registered under @name
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
gboolean      lrg_bt_has_action           (const gchar *name);

/**
 * lrg_bt_has_condition:
 * @name: a condition name
 *
 * Returns: %TRUE if a condition is registered under @name
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
gboolean      lrg_bt_has_condition        (const gchar *name);

/**
 * lrg_bt_dup_action_names:
 *
 * The registered action names in bytewise order (an editor's palette).
 *
 * Returns: (transfer full) (array zero-terminated=1): the names
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
gchar **      lrg_bt_dup_action_names     (void);

/**
 * lrg_bt_dup_condition_names:
 *
 * The registered condition names in bytewise order.
 *
 * Returns: (transfer full) (array zero-terminated=1): the names
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
gchar **      lrg_bt_dup_condition_names  (void);

/**
 * lrg_bt_register_node_type:
 * @name: the `type:` spelling
 * @type: an instantiable #LrgBTNode subclass
 *
 * Makes a custom node class available to `.rbt` files. The node's other
 * keys (except `id`, `name`, `child` and `children`) are set as GObject
 * properties of the same name (underscores become dashes): booleans,
 * integers, floats, strings and enums (by nick) convert. #LrgBTComposite
 * subclasses take `children:`, #LrgBTDecorator subclasses one `child:`.
 *
 * Returns: %FALSE if @name is a built-in type (those cannot be replaced)
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
gboolean      lrg_bt_register_node_type   (const gchar *name,
                                           GType        type);

/**
 * lrg_bt_lookup_node_type:
 * @name: a `type:` spelling
 * @out_kind: (out) (optional): the type's kind
 *
 * Looks a node type up among the built-in and registered ones.
 *
 * Returns: %TRUE if @name is known
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
gboolean      lrg_bt_lookup_node_type     (const gchar   *name,
                                           LrgBtNodeKind *out_kind);

/**
 * lrg_bt_dup_node_type_names:
 *
 * Every known node type: the built-in ones in palette order (composites,
 * decorators, leaves, `subtree`), then the registered ones in bytewise
 * order.
 *
 * Returns: (transfer full) (array zero-terminated=1): the names
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
gchar **      lrg_bt_dup_node_type_names  (void);

/* --- parameter helpers ------------------------------------------------- */

/**
 * lrg_bt_params_get_double:
 * @params: (nullable): an `a{sv}` params dictionary
 * @key: the parameter name
 * @default_value: returned when the key is missing or not numeric
 *
 * Reads a numeric parameter (integers and booleans convert).
 *
 * Returns: the value
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
gdouble       lrg_bt_params_get_double    (GVariant    *params,
                                           const gchar *key,
                                           gdouble      default_value);

/**
 * lrg_bt_params_get_int:
 * @params: (nullable): an `a{sv}` params dictionary
 * @key: the parameter name
 * @default_value: returned when the key is missing or not numeric
 *
 * Reads an integer parameter (doubles truncate, booleans are 0/1).
 *
 * Returns: the value
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
gint64        lrg_bt_params_get_int       (GVariant    *params,
                                           const gchar *key,
                                           gint64       default_value);

/**
 * lrg_bt_params_get_bool:
 * @params: (nullable): an `a{sv}` params dictionary
 * @key: the parameter name
 * @default_value: returned when the key is missing or not a boolean or
 *   number
 *
 * Returns: the value (numbers are true when non-zero)
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
gboolean      lrg_bt_params_get_bool      (GVariant    *params,
                                           const gchar *key,
                                           gboolean     default_value);

/**
 * lrg_bt_params_get_string:
 * @params: (nullable): an `a{sv}` params dictionary
 * @key: the parameter name
 * @default_value: (nullable): returned when the key is missing or not a
 *   string
 *
 * Returns: (transfer none) (nullable): the string, owned by @params
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_bt_params_get_string    (GVariant    *params,
                                           const gchar *key,
                                           const gchar *default_value);

/* ==========================================================================
 * LrgBtAssetNode
 * ========================================================================== */

#define LRG_TYPE_BT_ASSET_NODE (lrg_bt_asset_node_get_type ())

/**
 * LrgBtAssetNode:
 *
 * One node of a `.rbt` tree as authored: a type name, an id, a display
 * name, type-specific properties (`count`, `seconds`, `key`, `action`,
 * ...), binding parameters (`params:`) and children. Editors change trees
 * through this API and rebuild runners from the asset.
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgBtAssetNode, lrg_bt_asset_node, LRG, BT_ASSET_NODE, GObject)

/**
 * lrg_bt_asset_node_new:
 * @type_name: the node type (`sequence`, `action`, a registered type, ...)
 *
 * Returns: (transfer full): a new node without id, properties or children
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
LrgBtAssetNode * lrg_bt_asset_node_new             (const gchar *type_name);

/**
 * lrg_bt_asset_node_copy:
 * @self: a node
 *
 * Deep-copies the node and its children (ids included: give the copy new
 * ids before inserting it into the same asset), unknown keys too.
 *
 * Returns: (transfer full): the copy
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
LrgBtAssetNode * lrg_bt_asset_node_copy            (LrgBtAssetNode *self);

/**
 * lrg_bt_asset_node_get_type_name:
 * @self: a node
 *
 * Returns: (transfer none): the node type
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
const gchar *    lrg_bt_asset_node_get_type_name   (LrgBtAssetNode *self);

/**
 * lrg_bt_asset_node_set_type_name:
 * @self: a node
 * @type_name: the new type
 *
 * Changes the node type (properties and children are kept; validation
 * tells what no longer fits).
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
void             lrg_bt_asset_node_set_type_name   (LrgBtAssetNode *self,
                                                    const gchar    *type_name);

/**
 * lrg_bt_asset_node_get_kind:
 * @self: a node
 *
 * The kind of the node's type. A type that is neither built in nor
 * registered counts as a composite when it was loaded with `children:`
 * (or has more than one child), a decorator when loaded with `child:` (or
 * has exactly one), and a leaf otherwise.
 *
 * Returns: the kind
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
LrgBtNodeKind    lrg_bt_asset_node_get_kind        (LrgBtAssetNode *self);

/**
 * lrg_bt_asset_node_get_id:
 * @self: a node
 *
 * The node's id: the `id:` from the file, the one set with
 * lrg_bt_asset_node_set_id(), or one the asset generated (`n0`, `n1`, ...)
 * the first time it needed ids; %NULL before that.
 *
 * Returns: (transfer none) (nullable): the id
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
const gchar *    lrg_bt_asset_node_get_id          (LrgBtAssetNode *self);

/**
 * lrg_bt_asset_node_set_id:
 * @self: a node
 * @id: (nullable): an explicit id (saved as `id:`), or %NULL to let the
 *   asset generate one (not saved)
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
void             lrg_bt_asset_node_set_id          (LrgBtAssetNode *self,
                                                    const gchar    *id);

/**
 * lrg_bt_asset_node_has_explicit_id:
 * @self: a node
 *
 * Returns: %TRUE if the id is saved with the node (not generated)
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
gboolean         lrg_bt_asset_node_has_explicit_id (LrgBtAssetNode *self);

/**
 * lrg_bt_asset_node_get_name:
 * @self: a node
 *
 * Returns: (transfer none) (nullable): the display name
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
const gchar *    lrg_bt_asset_node_get_name        (LrgBtAssetNode *self);

/**
 * lrg_bt_asset_node_set_name:
 * @self: a node
 * @name: (nullable): the display name
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
void             lrg_bt_asset_node_set_name        (LrgBtAssetNode *self,
                                                    const gchar    *name);

/**
 * lrg_bt_asset_node_get_prop:
 * @self: a node
 * @key: a property key (`count`, `seconds`, `key`, `op`, `value`,
 *   `action`, `script`, `condition`, `ref`, `policy`, `duration`, or a
 *   custom type's property)
 *
 * Returns: (transfer none) (nullable): the value, %NULL when unset
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
GVariant *       lrg_bt_asset_node_get_prop        (LrgBtAssetNode *self,
                                                    const gchar    *key);

/**
 * lrg_bt_asset_node_set_prop:
 * @self: a node
 * @key: a property key
 * @value: (nullable): a `b`, `x`/`i`, `d` or `s` variant (a floating
 *   reference is sunk), or %NULL to remove the property
 *
 * Sets a type-specific property, keeping the order properties were first
 * set in (that is the order they are saved in).
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
void             lrg_bt_asset_node_set_prop        (LrgBtAssetNode *self,
                                                    const gchar    *key,
                                                    GVariant       *value);

/**
 * lrg_bt_asset_node_dup_prop_names:
 * @self: a node
 *
 * Returns: (transfer full) (array zero-terminated=1): the property keys in
 *   their saved order
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
gchar **         lrg_bt_asset_node_dup_prop_names  (LrgBtAssetNode *self);

/**
 * lrg_bt_asset_node_get_param:
 * @self: a node
 * @key: a parameter name
 *
 * Returns: (transfer none) (nullable): the binding parameter, %NULL when
 *   unset
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
GVariant *       lrg_bt_asset_node_get_param       (LrgBtAssetNode *self,
                                                    const gchar    *key);

/**
 * lrg_bt_asset_node_set_param:
 * @self: a node
 * @key: a parameter name
 * @value: (nullable): a scalar variant (floating is sunk), or %NULL to
 *   remove it
 *
 * Sets a binding parameter (the `params:` passed to the bound action,
 * condition or script function), keeping first-set order.
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
void             lrg_bt_asset_node_set_param       (LrgBtAssetNode *self,
                                                    const gchar    *key,
                                                    GVariant       *value);

/**
 * lrg_bt_asset_node_dup_param_names:
 * @self: a node
 *
 * Returns: (transfer full) (array zero-terminated=1): the parameter names
 *   in their saved order
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
gchar **         lrg_bt_asset_node_dup_param_names (LrgBtAssetNode *self);

/**
 * lrg_bt_asset_node_get_params:
 * @self: a node
 *
 * Returns: (transfer none) (nullable): every parameter as an `a{sv}`
 *   dictionary in saved order, %NULL when there are none
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
GVariant *       lrg_bt_asset_node_get_params      (LrgBtAssetNode *self);

/**
 * lrg_bt_asset_node_get_child_count:
 * @self: a node
 *
 * Returns: the number of children
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
guint            lrg_bt_asset_node_get_child_count (LrgBtAssetNode *self);

/**
 * lrg_bt_asset_node_get_child:
 * @self: a node
 * @index: a child index
 *
 * Returns: (transfer none) (nullable): the child, %NULL out of range
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
LrgBtAssetNode * lrg_bt_asset_node_get_child       (LrgBtAssetNode *self,
                                                    guint           index);

/**
 * lrg_bt_asset_node_insert_child:
 * @self: a node
 * @child: (transfer none): the child (must not have a parent)
 * @position: the index to insert at, or -1 (or past the end) to append
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
void             lrg_bt_asset_node_insert_child    (LrgBtAssetNode *self,
                                                    LrgBtAssetNode *child,
                                                    gint            position);

/**
 * lrg_bt_asset_node_remove_child:
 * @self: a node
 * @child: a child of @self
 *
 * Returns: %TRUE if @child was a child of @self
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
gboolean         lrg_bt_asset_node_remove_child    (LrgBtAssetNode *self,
                                                    LrgBtAssetNode *child);

/**
 * lrg_bt_asset_node_get_parent:
 * @self: a node
 *
 * Returns: (transfer none) (nullable): the parent node, %NULL for a root
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
LrgBtAssetNode * lrg_bt_asset_node_get_parent      (LrgBtAssetNode *self);

/* ==========================================================================
 * LrgBtAsset
 * ========================================================================== */

#define LRG_TYPE_BT_ASSET (lrg_bt_asset_get_type ())

/**
 * LrgBtAsset:
 *
 * A `.rbt` behaviour tree file: name, blackboard schema, named subtrees
 * and a root node. Loading is strict about values (wrong types, bad
 * operators, duplicate ids, undefined subtree names fail with the key
 * path) and keeps unknown keys so a load/save round trip is byte-stable.
 * Unknown node types and unbound leaves load fine (an editor may not have
 * the game's C code); lrg_bt_asset_validate() and lrg_bt_asset_build()
 * report them.
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgBtAsset, lrg_bt_asset, LRG, BT_ASSET, GObject)

/**
 * lrg_bt_asset_new:
 *
 * Returns: (transfer full): a new empty asset (no root)
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
LrgBtAsset *     lrg_bt_asset_new                  (void);

/**
 * lrg_bt_asset_new_from_file:
 * @path: a `.rbt` file
 * @error: (nullable): return location for an error
 *
 * Loads a file; its directory becomes the base directory subtree paths
 * resolve against.
 *
 * Returns: (transfer full) (nullable): the asset, %NULL on error
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
LrgBtAsset *     lrg_bt_asset_new_from_file        (const gchar  *path,
                                                    GError      **error);

/**
 * lrg_bt_asset_new_from_data:
 * @data: YAML text
 * @length: its length, or -1 when nul-terminated
 * @error: (nullable): return location for an error
 *
 * Returns: (transfer full) (nullable): the asset, %NULL on error
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
LrgBtAsset *     lrg_bt_asset_new_from_data        (const gchar  *data,
                                                    gssize        length,
                                                    GError      **error);

/**
 * lrg_bt_asset_load_from_file:
 * @self: an asset
 * @path: a `.rbt` file
 * @error: (nullable): return location for an error
 *
 * Replaces the asset's contents with the file's (unchanged on error) and
 * sets the base directory to the file's directory.
 *
 * Returns: %TRUE on success
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
gboolean         lrg_bt_asset_load_from_file       (LrgBtAsset   *self,
                                                    const gchar  *path,
                                                    GError      **error);

/**
 * lrg_bt_asset_load_from_data:
 * @self: an asset
 * @data: YAML text
 * @length: its length, or -1 when nul-terminated
 * @error: (nullable): return location for an error
 *
 * Replaces the asset's contents (unchanged on error).
 *
 * Returns: %TRUE on success
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
gboolean         lrg_bt_asset_load_from_data       (LrgBtAsset   *self,
                                                    const gchar  *data,
                                                    gssize        length,
                                                    GError      **error);

/**
 * lrg_bt_asset_to_data:
 * @self: an asset
 * @error: (nullable): return location for an error
 *
 * Serialises the asset to `.rbt` YAML. Saving what was loaded gives the
 * same text back on the second round (the first save normalises spacing,
 * quoting and number spelling).
 *
 * Returns: (transfer full) (nullable): the text, %NULL on error
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
gchar *          lrg_bt_asset_to_data              (LrgBtAsset   *self,
                                                    GError      **error);

/**
 * lrg_bt_asset_save_to_file:
 * @self: an asset
 * @path: the file to write
 * @error: (nullable): return location for an error
 *
 * Returns: %TRUE on success
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
gboolean         lrg_bt_asset_save_to_file         (LrgBtAsset   *self,
                                                    const gchar  *path,
                                                    GError      **error);

/**
 * lrg_bt_asset_get_name:
 * @self: an asset
 *
 * Returns: (transfer none) (nullable): the tree's name
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
const gchar *    lrg_bt_asset_get_name             (LrgBtAsset *self);

/**
 * lrg_bt_asset_set_name:
 * @self: an asset
 * @name: (nullable): the tree's name
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
void             lrg_bt_asset_set_name             (LrgBtAsset  *self,
                                                    const gchar *name);

/**
 * lrg_bt_asset_get_base_dir:
 * @self: an asset
 *
 * Returns: (transfer none) (nullable): the directory relative subtree
 *   paths resolve against (the loaded file's directory)
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
const gchar *    lrg_bt_asset_get_base_dir         (LrgBtAsset *self);

/**
 * lrg_bt_asset_set_base_dir:
 * @self: an asset
 * @dir: (nullable): a directory, %NULL for the current directory
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
void             lrg_bt_asset_set_base_dir         (LrgBtAsset  *self,
                                                    const gchar *dir);

/**
 * lrg_bt_asset_get_schema:
 * @self: an asset
 *
 * The `blackboard:` schema. Built trees get its defaults; `check`,
 * `guard` and `set` nodes must name declared keys when it is not empty.
 *
 * Returns: (transfer none): the schema (edit it in place)
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
LrgBlackboardSchema * lrg_bt_asset_get_schema      (LrgBtAsset *self);

/**
 * lrg_bt_asset_get_root:
 * @self: an asset
 *
 * Returns: (transfer none) (nullable): the root node
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
LrgBtAssetNode * lrg_bt_asset_get_root             (LrgBtAsset *self);

/**
 * lrg_bt_asset_set_root:
 * @self: an asset
 * @root: (nullable) (transfer none): the root node (must not have a parent)
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
void             lrg_bt_asset_set_root             (LrgBtAsset     *self,
                                                    LrgBtAssetNode *root);

/**
 * lrg_bt_asset_find_node:
 * @self: an asset
 * @id: a node id
 *
 * Finds a node by id in the root tree and the inline subtrees (generating
 * ids for nodes without one first).
 *
 * Returns: (transfer none) (nullable): the node
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
LrgBtAssetNode * lrg_bt_asset_find_node            (LrgBtAsset  *self,
                                                    const gchar *id);

/**
 * lrg_bt_asset_set_subtree_path:
 * @self: an asset
 * @name: the subtree name `subtree` nodes use (`ref: name`)
 * @path: a `.rbt` file, relative to the base directory or absolute
 *
 * Declares (or replaces) a subtree that lives in another file.
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
void             lrg_bt_asset_set_subtree_path     (LrgBtAsset  *self,
                                                    const gchar *name,
                                                    const gchar *path);

/**
 * lrg_bt_asset_set_subtree_root:
 * @self: an asset
 * @name: the subtree name
 * @root: (transfer none): the subtree's root node (must not have a parent)
 *
 * Declares (or replaces) a subtree written inline in this file.
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
void             lrg_bt_asset_set_subtree_root     (LrgBtAsset     *self,
                                                    const gchar    *name,
                                                    LrgBtAssetNode *root);

/**
 * lrg_bt_asset_remove_subtree:
 * @self: an asset
 * @name: a subtree name
 *
 * Returns: %TRUE if the subtree was declared
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
gboolean         lrg_bt_asset_remove_subtree       (LrgBtAsset  *self,
                                                    const gchar *name);

/**
 * lrg_bt_asset_dup_subtree_names:
 * @self: an asset
 *
 * Returns: (transfer full) (array zero-terminated=1): the subtree names in
 *   declaration order
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
gchar **         lrg_bt_asset_dup_subtree_names    (LrgBtAsset *self);

/**
 * lrg_bt_asset_get_subtree_path:
 * @self: an asset
 * @name: a subtree name
 *
 * Returns: (transfer none) (nullable): the file of a file subtree, %NULL
 *   for an inline or undeclared one
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
const gchar *    lrg_bt_asset_get_subtree_path     (LrgBtAsset  *self,
                                                    const gchar *name);

/**
 * lrg_bt_asset_get_subtree_root:
 * @self: an asset
 * @name: a subtree name
 *
 * Returns: (transfer none) (nullable): the root of an inline subtree,
 *   %NULL for a file or undeclared one
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
LrgBtAssetNode * lrg_bt_asset_get_subtree_root     (LrgBtAsset  *self,
                                                    const gchar *name);

/**
 * lrg_bt_asset_get_allow_unbound:
 * @self: an asset
 *
 * Returns: whether unbound leaves and unknown types build as failing
 *   leaves instead of failing the build
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
gboolean         lrg_bt_asset_get_allow_unbound    (LrgBtAsset *self);

/**
 * lrg_bt_asset_set_allow_unbound:
 * @self: an asset
 * @allow: %TRUE to build leaves whose action, condition, script function
 *   or node type is missing as leaves that always fail (logged once),
 *   so an editor can preview a tree without the game's code
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
void             lrg_bt_asset_set_allow_unbound    (LrgBtAsset *self,
                                                    gboolean    allow);

/**
 * lrg_bt_asset_validate:
 * @self: an asset
 * @error: (nullable): return location for the first problem found
 *
 * Checks structure: a root exists, composites have children lists,
 * decorators exactly one child, leaves none, required properties are
 * present with the right types, `check`/`guard`/`set` keys are declared
 * in a non-empty schema, subtrees resolve (file subtrees are loaded) and
 * do not recurse. Bindings are not checked (see
 * lrg_bt_asset_dup_unbound()).
 *
 * Returns: %TRUE if the asset can be built
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
gboolean         lrg_bt_asset_validate             (LrgBtAsset  *self,
                                                    GError     **error);

/**
 * lrg_bt_asset_dup_unbound:
 * @self: an asset
 * @scripting: (nullable): the scripting context script leaves would use;
 *   with %NULL every script leaf counts as unbound
 *
 * Lists what a build would not find, as `action:NAME`,
 * `condition:NAME`, `script:NAME` and `type:NAME` entries (each once, in
 * tree order, subtrees included).
 *
 * Returns: (transfer full) (array zero-terminated=1): the missing names
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
gchar **         lrg_bt_asset_dup_unbound          (LrgBtAsset   *self,
                                                    LrgScripting *scripting);

/**
 * lrg_bt_asset_build:
 * @self: an asset
 * @scripting: (nullable): the context `script:` leaves call into
 * @blackboard: (nullable): the blackboard the tree uses (shared), or
 *   %NULL for the tree's own
 * @error: (nullable): return location for an error
 *
 * Builds a runnable tree: validates, binds every leaf, expands subtrees
 * and writes the schema defaults into the blackboard for keys it does not
 * have yet. Each built node is named after the asset node's name (or id).
 *
 * Returns: (transfer full) (nullable): the tree, %NULL on error
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
LrgBehaviorTree * lrg_bt_asset_build               (LrgBtAsset     *self,
                                                    LrgScripting   *scripting,
                                                    LrgBlackboard  *blackboard,
                                                    GError        **error);

/* ==========================================================================
 * LrgBtRunner
 * ========================================================================== */

#define LRG_TYPE_BT_RUNNER (lrg_bt_runner_get_type ())

/**
 * LrgBtRunner:
 *
 * A tree built from an #LrgBtAsset plus what a live view needs: which
 * built node belongs to which asset node id, each node's status, and
 * whether the last tick visited it. Nodes inside a subtree get ids of the
 * form `SUBTREE-NODE-ID/INNER-ID`.
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgBtRunner, lrg_bt_runner, LRG, BT_RUNNER, GObject)

/**
 * lrg_bt_runner_new:
 * @asset: the asset
 * @scripting: (nullable): the context `script:` leaves call into
 * @blackboard: (nullable): the blackboard to share, or %NULL for a new one
 * @error: (nullable): return location for an error
 *
 * Builds @asset (see lrg_bt_asset_build()). The runner keeps a reference
 * to @asset but later edits to it do not change the built tree: build a
 * new runner to pick them up.
 *
 * Returns: (transfer full) (nullable): the runner, %NULL on error
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
LrgBtRunner *    lrg_bt_runner_new                 (LrgBtAsset     *asset,
                                                    LrgScripting   *scripting,
                                                    LrgBlackboard  *blackboard,
                                                    GError        **error);

/**
 * lrg_bt_runner_get_asset:
 * @self: a runner
 *
 * Returns: (transfer none): the asset it was built from
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
LrgBtAsset *     lrg_bt_runner_get_asset           (LrgBtRunner *self);

/**
 * lrg_bt_runner_get_tree:
 * @self: a runner
 *
 * Returns: (transfer none): the built tree
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
LrgBehaviorTree * lrg_bt_runner_get_tree           (LrgBtRunner *self);

/**
 * lrg_bt_runner_get_blackboard:
 * @self: a runner
 *
 * Returns: (transfer none): the tree's blackboard
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
LrgBlackboard *  lrg_bt_runner_get_blackboard      (LrgBtRunner *self);

/**
 * lrg_bt_runner_tick:
 * @self: a runner
 * @delta_time: seconds since the last tick
 *
 * Ticks the tree once and records the debug state (which nodes the tick
 * reached and their statuses), then emits #LrgBtRunner::ticked.
 *
 * Returns: the root's status
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
LrgBTStatus      lrg_bt_runner_tick                (LrgBtRunner *self,
                                                    gfloat       delta_time);

/**
 * lrg_bt_runner_reset:
 * @self: a runner
 *
 * Aborts running nodes and resets the tree (the blackboard is kept).
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
void             lrg_bt_runner_reset               (LrgBtRunner *self);

/**
 * lrg_bt_runner_get_status:
 * @self: a runner
 *
 * Returns: the root's status after the last tick
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
LrgBTStatus      lrg_bt_runner_get_status          (LrgBtRunner *self);

/**
 * lrg_bt_runner_get_tick_count:
 * @self: a runner
 *
 * Returns: the number of ticks so far
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
guint64          lrg_bt_runner_get_tick_count      (LrgBtRunner *self);

/**
 * lrg_bt_runner_get_node_count:
 * @self: a runner
 *
 * Returns: the number of built nodes (subtrees expanded)
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
guint            lrg_bt_runner_get_node_count      (LrgBtRunner *self);

/**
 * lrg_bt_runner_get_node_id:
 * @self: a runner
 * @index: a node index, in depth-first (root first) order
 *
 * Returns: (transfer none) (nullable): the node's id, %NULL out of range
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
const gchar *    lrg_bt_runner_get_node_id         (LrgBtRunner *self,
                                                    guint        index);

/**
 * lrg_bt_runner_get_node:
 * @self: a runner
 * @id: a node id
 *
 * Returns: (transfer none) (nullable): the built node
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
LrgBTNode *      lrg_bt_runner_get_node            (LrgBtRunner *self,
                                                    const gchar *id);

/**
 * lrg_bt_runner_get_node_status:
 * @self: a runner
 * @id: a node id
 *
 * Returns: the node's status after the last tick that reached it
 *   (%LRG_BT_STATUS_INVALID for unknown ids and nodes never ticked)
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
LrgBTStatus      lrg_bt_runner_get_node_status     (LrgBtRunner *self,
                                                    const gchar *id);

/**
 * lrg_bt_runner_get_node_visited:
 * @self: a runner
 * @id: a node id
 *
 * Returns: %TRUE if the last tick reached the node
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
gboolean         lrg_bt_runner_get_node_visited    (LrgBtRunner *self,
                                                    const gchar *id);

/**
 * lrg_bt_runner_dup_active_path:
 * @self: a runner
 *
 * The nodes the last tick left running, root first: the branch the tree
 * is "in".
 *
 * Returns: (transfer full) (array zero-terminated=1): node ids
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
gchar **         lrg_bt_runner_dup_active_path     (LrgBtRunner *self);

/**
 * lrg_bt_runner_get_current_node:
 * @self: a runner
 *
 * The deepest node the last tick left running, or (when nothing is
 * running) the last node it visited.
 *
 * Returns: (transfer none) (nullable): a node id, %NULL before the first
 *   tick
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
const gchar *    lrg_bt_runner_get_current_node    (LrgBtRunner *self);

/**
 * lrg_bt_runner_dup_debug_state:
 * @self: a runner
 *
 * The last tick as YAML, for IPC and logs:
 *
 * |[
 * tick: 42
 * status: running
 * current: attack
 * nodes:
 * - {id: root, status: running, visited: true, ticks: 42}
 * ]|
 *
 * Returns: (transfer full): the text
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
gchar *          lrg_bt_runner_dup_debug_state     (LrgBtRunner *self);

/* ==========================================================================
 * Library-internal helpers shared by lrg-bt-asset.c, lrg-logic-machine.c
 * and the AI component. Not public API: only visible to files that define
 * LRG_AI_PRIVATE before including this header (the GIR scanner and the
 * installed header users never do).
 * ========================================================================== */

#ifdef LRG_AI_PRIVATE
#include <yaml-glib.h>

/* ref-counted registry entry: a C action or condition and its data */
typedef struct _LrgAiBound LrgAiBound;

G_GNUC_INTERNAL gboolean      lrg_ai_priv_fail             (GError      **error,
                                                            GQuark        domain,
                                                            gint          code,
                                                            const gchar  *path,
                                                            const gchar  *fmt,
                                                            ...) G_GNUC_PRINTF (5, 6);
G_GNUC_INTERNAL gchar *       lrg_ai_priv_sub_path         (const gchar *path,
                                                            const gchar *key);
G_GNUC_INTERNAL gchar *       lrg_ai_priv_index_path       (const gchar *path,
                                                            const gchar *key,
                                                            guint        index);
G_GNUC_INTERNAL YamlMapping * lrg_ai_priv_as_mapping       (YamlNode     *node,
                                                            const gchar  *path,
                                                            GQuark        domain,
                                                            GError      **error);
G_GNUC_INTERNAL YamlSequence * lrg_ai_priv_as_sequence     (YamlNode     *node,
                                                            const gchar  *path,
                                                            GQuark        domain,
                                                            GError      **error);
G_GNUC_INTERNAL const gchar * lrg_ai_priv_as_scalar        (YamlNode     *node,
                                                            const gchar  *path,
                                                            GQuark        domain,
                                                            GError      **error);
G_GNUC_INTERNAL gboolean      lrg_ai_priv_get_string       (YamlMapping  *map,
                                                            const gchar  *path,
                                                            const gchar  *key,
                                                            gboolean      required,
                                                            const gchar **out,
                                                            GQuark        domain,
                                                            GError      **error);
G_GNUC_INTERNAL gboolean      lrg_ai_priv_get_number       (YamlMapping  *map,
                                                            const gchar  *path,
                                                            const gchar  *key,
                                                            gdouble      *out,
                                                            gboolean     *present,
                                                            GQuark        domain,
                                                            GError      **error);
G_GNUC_INTERNAL GVariant *    lrg_ai_priv_scalar_to_variant (YamlNode *node);
G_GNUC_INTERNAL YamlNode *    lrg_ai_priv_variant_to_node  (GVariant *value);
/* an ordered string -> GVariant dictionary (GPtrArray of entries) */
G_GNUC_INTERNAL GPtrArray *   lrg_ai_priv_dict_new         (void);
G_GNUC_INTERNAL GPtrArray *   lrg_ai_priv_dict_copy        (GPtrArray *dict);
G_GNUC_INTERNAL void          lrg_ai_priv_dict_set         (GPtrArray   *dict,
                                                            const gchar *key,
                                                            GVariant    *value);
G_GNUC_INTERNAL GVariant *    lrg_ai_priv_dict_lookup      (GPtrArray   *dict,
                                                            const gchar *key);
G_GNUC_INTERNAL gchar **      lrg_ai_priv_dict_keys        (GPtrArray *dict);
G_GNUC_INTERNAL GVariant *    lrg_ai_priv_dict_to_variant  (GPtrArray *dict);
G_GNUC_INTERNAL YamlNode *    lrg_ai_priv_dict_to_node     (GPtrArray *dict);
G_GNUC_INTERNAL gboolean      lrg_ai_priv_parse_dict       (YamlNode     *node,
                                                            const gchar  *path,
                                                            GPtrArray   **out,
                                                            GQuark        domain,
                                                            GError      **error);
G_GNUC_INTERNAL YamlMapping * lrg_ai_priv_collect_extra    (YamlMapping        *map,
                                                            const gchar *const *known);
G_GNUC_INTERNAL void          lrg_ai_priv_copy_extra       (YamlMapping *map,
                                                            YamlMapping *extra);
G_GNUC_INTERNAL void          lrg_ai_priv_put              (YamlMapping *map,
                                                            const gchar *key,
                                                            YamlNode    *node);
G_GNUC_INTERNAL YamlNode *    lrg_ai_priv_number_node      (gdouble value);
G_GNUC_INTERNAL gboolean      lrg_ai_priv_parse_schema     (LrgBlackboardSchema  *schema,
                                                            YamlNode             *node,
                                                            const gchar          *path,
                                                            GQuark                domain,
                                                            GError              **error);
G_GNUC_INTERNAL YamlNode *    lrg_ai_priv_write_schema     (LrgBlackboardSchema *schema);
G_GNUC_INTERNAL gboolean      lrg_ai_priv_parse_op         (YamlMapping      *map,
                                                            const gchar      *path,
                                                            LrgBlackboardOp  *out_op,
                                                            GVariant        **out_value,
                                                            GQuark            domain,
                                                            GError          **error);
G_GNUC_INTERNAL gboolean      lrg_ai_priv_call_script      (LrgScripting   *scripting,
                                                            const gchar    *func,
                                                            LrgBlackboard  *blackboard,
                                                            gfloat          delta_time,
                                                            GVariant       *params,
                                                            GValue         *ret,
                                                            GError        **error);
G_GNUC_INTERNAL LrgBTStatus   lrg_ai_priv_status_of_value  (const GValue *value);
G_GNUC_INTERNAL gboolean      lrg_ai_priv_truth_of_value   (const GValue *value);
G_GNUC_INTERNAL LrgAiBound *  lrg_ai_priv_lookup_action    (const gchar *name);
G_GNUC_INTERNAL LrgAiBound *  lrg_ai_priv_lookup_condition (const gchar *name);
G_GNUC_INTERNAL void          lrg_ai_priv_bound_unref      (LrgAiBound *bound);
G_GNUC_INTERNAL LrgBTStatus   lrg_ai_priv_bound_act        (LrgAiBound    *bound,
                                                            LrgBlackboard *blackboard,
                                                            gfloat         delta_time,
                                                            GVariant      *params);
G_GNUC_INTERNAL gboolean      lrg_ai_priv_bound_test       (LrgAiBound    *bound,
                                                            LrgBlackboard *blackboard,
                                                            GVariant      *params);
G_GNUC_INTERNAL GVariant *    lrg_ai_priv_empty_dict       (void);
#endif /* LRG_AI_PRIVATE */

G_END_DECLS
