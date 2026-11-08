/* lrg-blackboard.h
 *
 * Copyright 2024 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Blackboard for behavior tree data sharing.
 */

#ifndef LRG_BLACKBOARD_H
#define LRG_BLACKBOARD_H

#include <glib-object.h>
#include "lrg-version.h"
#include "lrg-types.h"

G_BEGIN_DECLS

#define LRG_TYPE_BLACKBOARD (lrg_blackboard_get_type ())

G_DECLARE_FINAL_TYPE (LrgBlackboard, lrg_blackboard, LRG, BLACKBOARD, GObject)

/**
 * lrg_blackboard_new:
 *
 * Creates a new blackboard for storing behavior tree data.
 *
 * Returns: (transfer full): A new #LrgBlackboard
 */
LRG_AVAILABLE_IN_ALL
LrgBlackboard *     lrg_blackboard_new               (void);

/**
 * lrg_blackboard_set_int:
 * @self: an #LrgBlackboard
 * @key: The key name
 * @value: The integer value
 *
 * Sets an integer value in the blackboard.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_blackboard_set_int           (LrgBlackboard *self,
                                                      const gchar   *key,
                                                      gint           value);

/**
 * lrg_blackboard_get_int:
 * @self: an #LrgBlackboard
 * @key: The key name
 * @default_value: Default if key not found
 *
 * Gets an integer value from the blackboard.
 *
 * Returns: The integer value, or @default_value if not found
 */
LRG_AVAILABLE_IN_ALL
gint                lrg_blackboard_get_int           (LrgBlackboard *self,
                                                      const gchar   *key,
                                                      gint           default_value);

/**
 * lrg_blackboard_set_float:
 * @self: an #LrgBlackboard
 * @key: The key name
 * @value: The float value
 *
 * Sets a float value in the blackboard.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_blackboard_set_float         (LrgBlackboard *self,
                                                      const gchar   *key,
                                                      gfloat         value);

/**
 * lrg_blackboard_get_float:
 * @self: an #LrgBlackboard
 * @key: The key name
 * @default_value: Default if key not found
 *
 * Gets a float value from the blackboard.
 *
 * Returns: The float value, or @default_value if not found
 */
LRG_AVAILABLE_IN_ALL
gfloat              lrg_blackboard_get_float         (LrgBlackboard *self,
                                                      const gchar   *key,
                                                      gfloat         default_value);

/**
 * lrg_blackboard_set_bool:
 * @self: an #LrgBlackboard
 * @key: The key name
 * @value: The boolean value
 *
 * Sets a boolean value in the blackboard.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_blackboard_set_bool          (LrgBlackboard *self,
                                                      const gchar   *key,
                                                      gboolean       value);

/**
 * lrg_blackboard_get_bool:
 * @self: an #LrgBlackboard
 * @key: The key name
 * @default_value: Default if key not found
 *
 * Gets a boolean value from the blackboard.
 *
 * Returns: The boolean value, or @default_value if not found
 */
LRG_AVAILABLE_IN_ALL
gboolean            lrg_blackboard_get_bool          (LrgBlackboard *self,
                                                      const gchar   *key,
                                                      gboolean       default_value);

/**
 * lrg_blackboard_set_string:
 * @self: an #LrgBlackboard
 * @key: The key name
 * @value: (nullable): The string value
 *
 * Sets a string value in the blackboard.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_blackboard_set_string        (LrgBlackboard *self,
                                                      const gchar   *key,
                                                      const gchar   *value);

/**
 * lrg_blackboard_get_string:
 * @self: an #LrgBlackboard
 * @key: The key name
 *
 * Gets a string value from the blackboard.
 *
 * Returns: (transfer none) (nullable): The string value, or %NULL if not found
 */
LRG_AVAILABLE_IN_ALL
const gchar *       lrg_blackboard_get_string        (LrgBlackboard *self,
                                                      const gchar   *key);

/**
 * lrg_blackboard_set_object:
 * @self: an #LrgBlackboard
 * @key: The key name
 * @object: (nullable) (transfer none): The GObject to store
 *
 * Sets a GObject reference in the blackboard.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_blackboard_set_object        (LrgBlackboard *self,
                                                      const gchar   *key,
                                                      GObject       *object);

/**
 * lrg_blackboard_get_object:
 * @self: an #LrgBlackboard
 * @key: The key name
 *
 * Gets a GObject from the blackboard.
 *
 * Returns: (transfer none) (nullable): The GObject, or %NULL if not found
 */
LRG_AVAILABLE_IN_ALL
GObject *           lrg_blackboard_get_object        (LrgBlackboard *self,
                                                      const gchar   *key);

/**
 * lrg_blackboard_set_pointer:
 * @self: an #LrgBlackboard
 * @key: The key name
 * @pointer: (nullable): The pointer to store
 * @destroy: (nullable): Destroy function for the pointer
 *
 * Sets an arbitrary pointer in the blackboard.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_blackboard_set_pointer       (LrgBlackboard  *self,
                                                      const gchar    *key,
                                                      gpointer        pointer,
                                                      GDestroyNotify  destroy);

/**
 * lrg_blackboard_get_pointer:
 * @self: an #LrgBlackboard
 * @key: The key name
 *
 * Gets a pointer from the blackboard.
 *
 * Returns: (nullable): The pointer, or %NULL if not found
 */
LRG_AVAILABLE_IN_ALL
gpointer            lrg_blackboard_get_pointer       (LrgBlackboard *self,
                                                      const gchar   *key);

/**
 * lrg_blackboard_has_key:
 * @self: an #LrgBlackboard
 * @key: The key name
 *
 * Checks if a key exists in the blackboard.
 *
 * Returns: %TRUE if key exists
 */
LRG_AVAILABLE_IN_ALL
gboolean            lrg_blackboard_has_key           (LrgBlackboard *self,
                                                      const gchar   *key);

/**
 * lrg_blackboard_remove:
 * @self: an #LrgBlackboard
 * @key: The key name
 *
 * Removes a key from the blackboard.
 *
 * Returns: %TRUE if key was removed
 */
LRG_AVAILABLE_IN_ALL
gboolean            lrg_blackboard_remove            (LrgBlackboard *self,
                                                      const gchar   *key);

/**
 * lrg_blackboard_clear:
 * @self: an #LrgBlackboard
 *
 * Removes all entries from the blackboard.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_blackboard_clear             (LrgBlackboard *self);

/**
 * lrg_blackboard_get_keys:
 * @self: an #LrgBlackboard
 *
 * Gets all keys in the blackboard.
 *
 * Returns: (transfer container) (element-type utf8): List of key names
 */
LRG_AVAILABLE_IN_ALL
GList *             lrg_blackboard_get_keys          (LrgBlackboard *self);

/**
 * lrg_blackboard_get_size:
 * @self: a blackboard
 *
 * Counts entries in constant time, including entries storing NULL values.
 *
 * Returns: number of stored keys
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_blackboard_get_size (LrgBlackboard *self);

/**
 * lrg_blackboard_dup_keys:
 * @self: a blackboard
 *
 * Copies key names in bytewise lexical order. The snapshot survives changes
 * to, or destruction of, the blackboard. Free it with g_strfreev().
 *
 * Returns: (transfer full) (array zero-terminated=1): owned key names
 */
LRG_AVAILABLE_IN_ALL
gchar **
lrg_blackboard_dup_keys (LrgBlackboard *self);

/**
 * lrg_blackboard_dup_string:
 * @self: a blackboard
 * @key: key name
 *
 * Copies a stored string so it survives replacement or blackboard destruction.
 *
 * Returns: (transfer full) (nullable): owned string, or NULL for a missing key,
 *   a different value type, or a stored NULL string
 */
LRG_AVAILABLE_IN_ALL
gchar *
lrg_blackboard_dup_string (LrgBlackboard *self,
                           const gchar   *key);


/* ==========================================================================
 * Typed access, comparisons and change tracking (0.9.0)
 * ========================================================================== */

/**
 * LrgBlackboardOp:
 * @LRG_BLACKBOARD_OP_EQ: the value equals the operand
 * @LRG_BLACKBOARD_OP_NE: the value differs from the operand (a missing key
 *   differs from everything)
 * @LRG_BLACKBOARD_OP_LT: the number is less than the operand
 * @LRG_BLACKBOARD_OP_LE: the number is less than or equal to the operand
 * @LRG_BLACKBOARD_OP_GT: the number is greater than the operand
 * @LRG_BLACKBOARD_OP_GE: the number is greater than or equal to the operand
 * @LRG_BLACKBOARD_OP_SET: the key is present and "truthy": a non-zero
 *   number, %TRUE, a non-empty string, a non-%NULL object or pointer
 * @LRG_BLACKBOARD_OP_UNSET: the negation of @LRG_BLACKBOARD_OP_SET
 *
 * A comparison of a blackboard value against an operand, used by
 * lrg_blackboard_test(), the `check` and `guard` nodes of `.rbt` behaviour
 * trees and the `when` conditions of `.rfsm` state machines. The nicks
 * (`eq`, `ne`, `lt`, `le`, `gt`, `ge`, `set`, `unset`) are the spellings
 * used in those files.
 *
 * Since: 0.9.0
 */
typedef enum
{
    LRG_BLACKBOARD_OP_EQ,
    LRG_BLACKBOARD_OP_NE,
    LRG_BLACKBOARD_OP_LT,
    LRG_BLACKBOARD_OP_LE,
    LRG_BLACKBOARD_OP_GT,
    LRG_BLACKBOARD_OP_GE,
    LRG_BLACKBOARD_OP_SET,
    LRG_BLACKBOARD_OP_UNSET
} LrgBlackboardOp;

LRG_AVAILABLE_IN_ALL
GType lrg_blackboard_op_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_BLACKBOARD_OP (lrg_blackboard_op_get_type ())

/**
 * lrg_blackboard_op_to_string:
 * @op: an operator
 *
 * Returns: (transfer none): the file spelling of @op (`eq`, `lt`, ...)
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
const gchar *
lrg_blackboard_op_to_string (LrgBlackboardOp op);

/**
 * lrg_blackboard_op_from_string:
 * @text: a spelling (`eq`, `ne`, `lt`, `le`, `gt`, `ge`, `set`, `unset`;
 *   the symbols `==`, `!=`, `<`, `<=`, `>`, `>=` are accepted too)
 * @out_op: (out): the operator
 *
 * Parses an operator spelling.
 *
 * Returns: %TRUE if @text named an operator
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_blackboard_op_from_string (const gchar     *text,
                               LrgBlackboardOp *out_op);

/**
 * lrg_blackboard_get_value_type:
 * @self: a blackboard
 * @key: key name
 *
 * The type of the value stored under @key: %G_TYPE_INT, %G_TYPE_FLOAT,
 * %G_TYPE_BOOLEAN, %G_TYPE_STRING, %G_TYPE_OBJECT or %G_TYPE_POINTER, or
 * %G_TYPE_INVALID when the key is missing. Inspectors use it to show and
 * edit values with the right widget.
 *
 * Returns: the stored value's type
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
GType
lrg_blackboard_get_value_type (LrgBlackboard *self,
                               const gchar   *key);

/**
 * lrg_blackboard_dup_value_text:
 * @self: a blackboard
 * @key: key name
 *
 * Formats the value under @key for display: integers and floats as
 * numbers (C locale), booleans as `true`/`false`, strings as they are,
 * objects as `TypeName@0xaddress` (or `null`), pointers as `0xaddress`.
 *
 * Returns: (transfer full) (nullable): the text, or %NULL for a missing key
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
gchar *
lrg_blackboard_dup_value_text (LrgBlackboard *self,
                               const gchar   *key);

/**
 * lrg_blackboard_dup_variant:
 * @self: a blackboard
 * @key: key name
 *
 * The value under @key as a #GVariant: `i` for integers, `d` for floats,
 * `b` for booleans, `s` for strings (an empty string for a stored %NULL).
 * Objects and pointers have no variant form.
 *
 * Returns: (transfer full) (nullable): a floating-free variant, or %NULL
 *   for a missing key, an object or a pointer
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
GVariant *
lrg_blackboard_dup_variant (LrgBlackboard *self,
                            const gchar   *key);

/**
 * lrg_blackboard_set_variant:
 * @self: a blackboard
 * @key: key name
 * @value: the value: a boolean (`b`), any integer type (stored as an int,
 *   clamped), a double (`d`, stored as a float) or a string (`s`); a
 *   floating reference is consumed
 *
 * Stores a variant's value with the matching typed setter. This is what
 * `.rbt` `set` nodes and `.rfsm` `set:` actions call.
 *
 * Returns: %TRUE if @value had a storable type
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_blackboard_set_variant (LrgBlackboard *self,
                            const gchar   *key,
                            GVariant      *value);

/**
 * lrg_blackboard_test:
 * @self: a blackboard
 * @key: key name
 * @op: the comparison
 * @operand: (nullable): what to compare with (ignored by
 *   %LRG_BLACKBOARD_OP_SET and %LRG_BLACKBOARD_OP_UNSET); a floating
 *   reference is consumed
 *
 * Compares the value under @key with @operand. Numbers (int and float
 * values, integer and double operands) compare numerically whatever their
 * storage; booleans compare with boolean or numeric operands (%TRUE is 1);
 * strings compare bytewise with string operands. A missing key, an
 * incomparable pair of types or a %NULL operand make every operator but
 * %LRG_BLACKBOARD_OP_NE and %LRG_BLACKBOARD_OP_UNSET false.
 *
 * Returns: whether the comparison holds
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_blackboard_test (LrgBlackboard   *self,
                     const gchar     *key,
                     LrgBlackboardOp  op,
                     GVariant        *operand);

/**
 * lrg_blackboard_get_serial:
 * @self: a blackboard
 *
 * A counter bumped by every set, remove and (non-empty) clear. Live
 * inspectors compare it with the value they saw last to redraw only on
 * change.
 *
 * Returns: the change serial
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
guint64
lrg_blackboard_get_serial (LrgBlackboard *self);

/* ==========================================================================
 * LrgBlackboardSchema: typed keys with defaults (0.9.0)
 * ========================================================================== */

#define LRG_TYPE_BLACKBOARD_SCHEMA (lrg_blackboard_schema_get_type ())

/**
 * LrgBlackboardSchema:
 *
 * An ordered list of typed blackboard keys with optional defaults: the
 * `blackboard:` section of `.rbt` and `.rfsm` files. Key types are
 * %G_TYPE_INT, %G_TYPE_FLOAT, %G_TYPE_BOOLEAN, %G_TYPE_STRING and
 * %G_TYPE_OBJECT (spelled `int`, `float`, `bool`, `string`, `object`).
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgBlackboardSchema, lrg_blackboard_schema, LRG, BLACKBOARD_SCHEMA, GObject)

/**
 * lrg_blackboard_schema_new:
 *
 * Creates an empty schema.
 *
 * Returns: (transfer full): a new #LrgBlackboardSchema
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
LrgBlackboardSchema *
lrg_blackboard_schema_new (void);

/**
 * lrg_blackboard_schema_add_key:
 * @self: a schema
 * @name: the key name
 * @type: %G_TYPE_INT, %G_TYPE_FLOAT, %G_TYPE_BOOLEAN, %G_TYPE_STRING or
 *   %G_TYPE_OBJECT
 * @default_value: (nullable): the default, converted to @type (an `i`, `d`,
 *   `b` or `s` variant; ignored for objects), or %NULL for none; a
 *   floating reference is consumed
 *
 * Declares a key (replacing a key of the same name in place, else
 * appending it).
 *
 * Returns: %TRUE if @type is a schema type and @default_value converts
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_blackboard_schema_add_key (LrgBlackboardSchema *self,
                               const gchar         *name,
                               GType                type,
                               GVariant            *default_value);

/**
 * lrg_blackboard_schema_remove_key:
 * @self: a schema
 * @name: the key name
 *
 * Returns: %TRUE if the key was declared
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_blackboard_schema_remove_key (LrgBlackboardSchema *self,
                                  const gchar         *name);

/**
 * lrg_blackboard_schema_get_key_count:
 * @self: a schema
 *
 * Returns: the number of declared keys
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_blackboard_schema_get_key_count (LrgBlackboardSchema *self);

/**
 * lrg_blackboard_schema_get_key_name:
 * @self: a schema
 * @index: a key index, in declaration order
 *
 * Returns: (transfer none) (nullable): the key's name, %NULL out of range
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
const gchar *
lrg_blackboard_schema_get_key_name (LrgBlackboardSchema *self,
                                    guint                index);

/**
 * lrg_blackboard_schema_has_key:
 * @self: a schema
 * @name: the key name
 *
 * Returns: %TRUE if @name is declared
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_blackboard_schema_has_key (LrgBlackboardSchema *self,
                               const gchar         *name);

/**
 * lrg_blackboard_schema_get_key_type:
 * @self: a schema
 * @name: the key name
 *
 * Returns: the key's type, %G_TYPE_INVALID when undeclared
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
GType
lrg_blackboard_schema_get_key_type (LrgBlackboardSchema *self,
                                    const gchar         *name);

/**
 * lrg_blackboard_schema_get_key_default:
 * @self: a schema
 * @name: the key name
 *
 * Returns: (transfer none) (nullable): the key's default (of the key's
 *   variant type), %NULL when it has none or is undeclared
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
GVariant *
lrg_blackboard_schema_get_key_default (LrgBlackboardSchema *self,
                                       const gchar         *name);

/**
 * lrg_blackboard_schema_apply_defaults:
 * @self: a schema
 * @blackboard: the blackboard to fill
 * @overwrite: %TRUE to set every default, %FALSE to set only keys the
 *   blackboard does not have yet
 *
 * Writes the declared defaults into @blackboard. Keys without a default
 * get the type's zero (0, 0.0, false, "") unless they are objects, which
 * are left unset.
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_blackboard_schema_apply_defaults (LrgBlackboardSchema *self,
                                      LrgBlackboard       *blackboard,
                                      gboolean             overwrite);

/**
 * lrg_blackboard_schema_validate:
 * @self: a schema
 * @blackboard: the blackboard to check
 * @error: (nullable): return location for a %G_IO_ERROR_INVALID_DATA error
 *   naming the first key whose stored type differs from its declaration
 *
 * Checks the declared keys present in @blackboard against their types
 * (missing keys and undeclared keys are fine).
 *
 * Returns: %TRUE if every present declared key has its declared type
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_blackboard_schema_validate (LrgBlackboardSchema  *self,
                                LrgBlackboard        *blackboard,
                                GError              **error);

/**
 * lrg_blackboard_schema_type_to_string:
 * @type: a schema key type
 *
 * Returns: (transfer none) (nullable): `int`, `float`, `bool`, `string` or
 *   `object`, %NULL for other types
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
const gchar *
lrg_blackboard_schema_type_to_string (GType type);

/**
 * lrg_blackboard_schema_type_from_string:
 * @text: `int`, `float`, `bool`, `string` or `object`
 *
 * Returns: the key type, %G_TYPE_INVALID for other text
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
GType
lrg_blackboard_schema_type_from_string (const gchar *text);

G_END_DECLS

#endif /* LRG_BLACKBOARD_H */
