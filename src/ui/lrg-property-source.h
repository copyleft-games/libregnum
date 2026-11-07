/* lrg-property-source.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgPropertySource: a reflected set of named, typed properties that an
 * LrgPropertyGrid edits. LrgPropertyInfo describes one property,
 * LrgObjectPropertySource exposes any GObject's properties.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"

G_BEGIN_DECLS

/* ==========================================================================
 * LrgPropertyState
 * ========================================================================== */

/**
 * LrgPropertyState:
 * @LRG_PROPERTY_STATE_NONE: nothing special: the value was set
 * @LRG_PROPERTY_STATE_DEFAULT: the value equals the property's default
 * @LRG_PROPERTY_STATE_OVERRIDDEN: the value overrides an inherited one
 *   (a prefab instance that differs from its source); the grid draws an
 *   accent bar and a bold label
 * @LRG_PROPERTY_STATE_ANIMATED: an animation drives the value; the grid
 *   draws a key marker
 * @LRG_PROPERTY_STATE_INVALID: the value fails validation; the grid
 *   draws the label in the danger colour
 *
 * Per-property state reported by lrg_property_source_get_state().
 */
typedef enum /*< flags >*/
{
	LRG_PROPERTY_STATE_NONE       = 0,
	LRG_PROPERTY_STATE_DEFAULT    = 1 << 0,
	LRG_PROPERTY_STATE_OVERRIDDEN = 1 << 1,
	LRG_PROPERTY_STATE_ANIMATED   = 1 << 2,
	LRG_PROPERTY_STATE_INVALID    = 1 << 3
} LrgPropertyState;

/**
 * lrg_property_state_get_type:
 *
 * Returns: the #GType of #LrgPropertyState
 */
LRG_AVAILABLE_IN_ALL
GType lrg_property_state_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_PROPERTY_STATE (lrg_property_state_get_type ())

/* ==========================================================================
 * Hints
 * ========================================================================== */

/**
 * lrg_property_hint_quark:
 *
 * The qdata key for an editor hint on a #GParamSpec. Set it with
 * `g_param_spec_set_qdata (pspec, lrg_property_hint_quark (), "color")`
 * (a static string) or g_param_spec_set_qdata_full() with g_free().
 * A blurb prefix "[hint:color]" does the same without code.
 *
 * Known hints: "color", "vector2", "vector3", "vector4", "angle"
 * (value in radians, shown in degrees; "angle:degrees" for a value
 * already in degrees), "curve", "gradient", "node-ref",
 * "object-ref:GTypeName", "asset:kind", "array:element-hint",
 * "multiline", "enum-strings:a,b,c", "range:min,max,step".
 *
 * Returns: the hint quark
 */
LRG_AVAILABLE_IN_ALL
GQuark lrg_property_hint_quark (void);

/**
 * lrg_property_group_quark:
 *
 * The qdata key for the group (section) of a #GParamSpec, the same way
 * as lrg_property_hint_quark(). A blurb prefix "[group:Combat]" does
 * the same without code.
 *
 * Returns: the group quark
 */
LRG_AVAILABLE_IN_ALL
GQuark lrg_property_group_quark (void);

/**
 * lrg_property_hint_split:
 * @hint: (nullable): a hint such as "asset:texture" or "color"
 * @kind: (out) (optional) (transfer full) (nullable): the part before
 *   the first ':' ("asset"), or %NULL when @hint is %NULL
 * @arg: (out) (optional) (transfer full) (nullable): the part after
 *   it ("texture"), or %NULL when there is none
 *
 * Splits a hint into its kind and its argument. Only the first ':'
 * separates them, so "array:asset:texture" gives "array" and
 * "asset:texture".
 */
LRG_AVAILABLE_IN_ALL
void lrg_property_hint_split (const gchar  *hint,
                              gchar       **kind,
                              gchar       **arg);

/**
 * lrg_property_hint_parse_range:
 * @hint: (nullable): a hint, "range:min,max[,step]" to match
 * @min: (out) (optional): the minimum
 * @max: (out) (optional): the maximum
 * @step: (out) (optional): the step, 0 when the hint has none
 *
 * Parses a range hint.
 *
 * Returns: %TRUE if @hint is a well-formed range hint
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_property_hint_parse_range (const gchar *hint,
                                        gdouble     *min,
                                        gdouble     *max,
                                        gdouble     *step);

/* ==========================================================================
 * Value helpers
 * ========================================================================== */

/**
 * lrg_property_values_equal:
 * @a: a #GValue
 * @b: a #GValue
 *
 * Compares two property values by content. Unlike
 * g_param_values_cmp(), which compares boxed values by pointer, this
 * knows #GrlColor, #GrlVector2, #GrlVector3, #GrlVector4,
 * #LrgGradientStops, #LrgKeyframeCurve, string arrays and #GVariant.
 * Values of different types are never equal.
 *
 * Returns: %TRUE if @a and @b hold the same value
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_property_values_equal (const GValue *a,
                                    const GValue *b);

/**
 * lrg_property_value_to_string:
 * @value: a #GValue
 *
 * Formats a property value as text the way the grid copies it to the
 * clipboard: numbers, "true"/"false", strings as is, enum and flags
 * nicks ("a|b"), colours as "#rrggbbaa", vectors as "x, y, z",
 * gradients as "offset #rrggbbaa; ...", string arrays one per line.
 *
 * Returns: (transfer full): the text
 */
LRG_AVAILABLE_IN_ALL
gchar * lrg_property_value_to_string (const GValue *value);

/**
 * lrg_property_value_from_string:
 * @value: an initialised #GValue whose type says what to parse
 * @text: text as written by lrg_property_value_to_string()
 *
 * Parses @text into @value (the reverse of
 * lrg_property_value_to_string()), used to paste a value copied as
 * text. On failure @value is left unchanged.
 *
 * Returns: %TRUE if @text was parsed
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_property_value_from_string (GValue      *value,
                                         const gchar *text);

/* ==========================================================================
 * LrgPropertyInfo
 * ========================================================================== */

#define LRG_TYPE_PROPERTY_INFO (lrg_property_info_get_type ())

/**
 * LrgPropertyInfo:
 *
 * An opaque, reference counted description of one property: its name,
 * label, group, #GParamSpec (value type, range and default), editor
 * hint, read-only flag and tooltip.
 */
typedef struct _LrgPropertyInfo LrgPropertyInfo;

/**
 * lrg_property_info_get_type:
 *
 * Returns: the #GType of #LrgPropertyInfo
 */
LRG_AVAILABLE_IN_ALL
GType lrg_property_info_get_type (void) G_GNUC_CONST;

/**
 * lrg_property_info_new:
 * @pspec: the property's #GParamSpec (value type, range, default)
 *
 * Describes a property from its #GParamSpec. The name is the pspec's
 * name; the label is its nick when it has one, otherwise the name with
 * dashes turned into spaces and the first letter upper-cased ("max-hp"
 * becomes "Max hp"). The hint and group come from the pspec's qdata
 * (lrg_property_hint_quark(), lrg_property_group_quark()) or from
 * "[hint:...]" / "[group:...]" prefixes of its blurb; the rest of the
 * blurb is the tooltip. Properties that are not writable, or are
 * construct-only, are read-only.
 *
 * Returns: (transfer full): a new #LrgPropertyInfo
 */
LRG_AVAILABLE_IN_ALL
LrgPropertyInfo * lrg_property_info_new (GParamSpec *pspec);

/**
 * lrg_property_info_ref:
 * @self: an #LrgPropertyInfo
 *
 * Returns: (transfer full): @self with one more reference
 */
LRG_AVAILABLE_IN_ALL
LrgPropertyInfo * lrg_property_info_ref (LrgPropertyInfo *self);

/**
 * lrg_property_info_unref:
 * @self: an #LrgPropertyInfo
 *
 * Drops a reference; the info is freed with the last one.
 */
LRG_AVAILABLE_IN_ALL
void lrg_property_info_unref (LrgPropertyInfo *self);

/**
 * lrg_property_info_get_name:
 * @self: an #LrgPropertyInfo
 *
 * Returns: (transfer none): the property name, the key passed to
 *   lrg_property_source_get_value()
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_property_info_get_name (const LrgPropertyInfo *self);

/**
 * lrg_property_info_get_label:
 * @self: an #LrgPropertyInfo
 *
 * Returns: (transfer none): the label shown in the grid
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_property_info_get_label (const LrgPropertyInfo *self);

/**
 * lrg_property_info_set_label:
 * @self: an #LrgPropertyInfo
 * @label: the label shown in the grid
 *
 * Replaces the label.
 */
LRG_AVAILABLE_IN_ALL
void lrg_property_info_set_label (LrgPropertyInfo *self,
                                  const gchar     *label);

/**
 * lrg_property_info_get_group:
 * @self: an #LrgPropertyInfo
 *
 * Returns: (transfer none) (nullable): the group (grid section), or
 *   %NULL for ungrouped properties shown above the sections
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_property_info_get_group (const LrgPropertyInfo *self);

/**
 * lrg_property_info_set_group:
 * @self: an #LrgPropertyInfo
 * @group: (nullable): the group, %NULL for none
 *
 * Moves the property into a section of the grid.
 */
LRG_AVAILABLE_IN_ALL
void lrg_property_info_set_group (LrgPropertyInfo *self,
                                  const gchar     *group);

/**
 * lrg_property_info_get_pspec:
 * @self: an #LrgPropertyInfo
 *
 * Returns: (transfer none): the #GParamSpec giving the value type,
 *   range and default
 */
LRG_AVAILABLE_IN_ALL
GParamSpec * lrg_property_info_get_pspec (const LrgPropertyInfo *self);

/**
 * lrg_property_info_get_value_type:
 * @self: an #LrgPropertyInfo
 *
 * Returns: the value type (the pspec's value type)
 */
LRG_AVAILABLE_IN_ALL
GType lrg_property_info_get_value_type (const LrgPropertyInfo *self);

/**
 * lrg_property_info_get_hint:
 * @self: an #LrgPropertyInfo
 *
 * Returns: (transfer none) (nullable): the editor hint, see
 *   lrg_property_hint_quark()
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_property_info_get_hint (const LrgPropertyInfo *self);

/**
 * lrg_property_info_set_hint:
 * @self: an #LrgPropertyInfo
 * @hint: (nullable): the editor hint, %NULL for none
 *
 * Replaces the editor hint.
 */
LRG_AVAILABLE_IN_ALL
void lrg_property_info_set_hint (LrgPropertyInfo *self,
                                 const gchar     *hint);

/**
 * lrg_property_info_get_read_only:
 * @self: an #LrgPropertyInfo
 *
 * Returns: %TRUE if the grid shows the value without letting it change
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_property_info_get_read_only (const LrgPropertyInfo *self);

/**
 * lrg_property_info_set_read_only:
 * @self: an #LrgPropertyInfo
 * @read_only: whether the value is read-only
 *
 * Marks the property read-only (or editable).
 */
LRG_AVAILABLE_IN_ALL
void lrg_property_info_set_read_only (LrgPropertyInfo *self,
                                      gboolean         read_only);

/**
 * lrg_property_info_get_tooltip:
 * @self: an #LrgPropertyInfo
 *
 * Returns: (transfer none) (nullable): the tooltip, without hint and
 *   group prefixes
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_property_info_get_tooltip (const LrgPropertyInfo *self);

/**
 * lrg_property_info_set_tooltip:
 * @self: an #LrgPropertyInfo
 * @tooltip: (nullable): the tooltip
 *
 * Replaces the tooltip.
 */
LRG_AVAILABLE_IN_ALL
void lrg_property_info_set_tooltip (LrgPropertyInfo *self,
                                    const gchar     *tooltip);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (LrgPropertyInfo, lrg_property_info_unref)

/* ==========================================================================
 * LrgPropertySource interface
 * ========================================================================== */

#define LRG_TYPE_PROPERTY_SOURCE (lrg_property_source_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_INTERFACE (LrgPropertySource, lrg_property_source, LRG, PROPERTY_SOURCE, GObject)

/**
 * LrgPropertySourceInterface:
 * @parent_iface: the parent interface
 * @list_properties: returns the properties as a #GPtrArray of
 *   #LrgPropertyInfo, in display order (required)
 * @get_value: initialises @value to the property's type and stores
 *   the current value (required)
 * @set_value: stores a new value (required); emit
 *   #LrgPropertySource::property-changed when it changed
 * @get_state: returns #LrgPropertyState flags (optional; the default
 *   reports %LRG_PROPERTY_STATE_DEFAULT when the value equals the
 *   pspec default)
 * @reset: puts the property back to its default (optional; the default
 *   sets the pspec's default value)
 *
 * The interface an #LrgPropertyGrid edits. The editor's node source,
 * theme editors and data editors implement it; #LrgObjectPropertySource
 * implements it over any #GObject.
 */
struct _LrgPropertySourceInterface
{
	GTypeInterface parent_iface;

	GPtrArray *      (*list_properties) (LrgPropertySource *self);
	gboolean         (*get_value)       (LrgPropertySource *self,
	                                     const gchar       *name,
	                                     GValue            *value);
	gboolean         (*set_value)       (LrgPropertySource *self,
	                                     const gchar       *name,
	                                     const GValue      *value);
	LrgPropertyState (*get_state)       (LrgPropertySource *self,
	                                     const gchar       *name);
	gboolean         (*reset)           (LrgPropertySource *self,
	                                     const gchar       *name);

	/*< private >*/
	gpointer _reserved[6];
};

/**
 * lrg_property_source_list_properties:
 * @self: an #LrgPropertySource
 *
 * Returns: (transfer container) (element-type LrgPropertyInfo): the
 *   properties in display order; the array frees its infos
 */
LRG_AVAILABLE_IN_ALL
GPtrArray * lrg_property_source_list_properties (LrgPropertySource *self);

/**
 * lrg_property_source_find_property:
 * @self: an #LrgPropertySource
 * @name: a property name
 *
 * Returns: (transfer full) (nullable): the info of @name, or %NULL
 */
LRG_AVAILABLE_IN_ALL
LrgPropertyInfo * lrg_property_source_find_property (LrgPropertySource *self,
                                                     const gchar       *name);

/**
 * lrg_property_source_get_value:
 * @self: an #LrgPropertySource
 * @name: a property name
 * @value: (out caller-allocates): an uninitialised #GValue
 *   (G_VALUE_INIT); it is initialised to the property's type
 *
 * Reads the current value. Unset @value with g_value_unset().
 *
 * Returns: %TRUE if @name exists and @value was filled
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_property_source_get_value (LrgPropertySource *self,
                                        const gchar       *name,
                                        GValue            *value);

/**
 * lrg_property_source_set_value:
 * @self: an #LrgPropertySource
 * @name: a property name
 * @value: the new value; transformed to the property's type when it
 *   is of another, transformable type
 *
 * Stores a new value.
 *
 * Returns: %TRUE if the value was stored
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_property_source_set_value (LrgPropertySource *self,
                                        const gchar       *name,
                                        const GValue      *value);

/**
 * lrg_property_source_get_state:
 * @self: an #LrgPropertySource
 * @name: a property name
 *
 * Returns: the state flags of @name
 */
LRG_AVAILABLE_IN_ALL
LrgPropertyState lrg_property_source_get_state (LrgPropertySource *self,
                                                const gchar       *name);

/**
 * lrg_property_source_reset:
 * @self: an #LrgPropertySource
 * @name: a property name
 *
 * Puts @name back to its default value.
 *
 * Returns: %TRUE if the property was reset
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_property_source_reset (LrgPropertySource *self,
                                    const gchar       *name);

/**
 * lrg_property_source_emit_changed:
 * @self: an #LrgPropertySource
 * @name: (nullable): the property that changed, or %NULL when the
 *   whole set may have changed
 *
 * Emits #LrgPropertySource::property-changed with @name as the detail.
 * Implementations call this when a value changes behind the grid's
 * back (undo, scripts, animation).
 */
LRG_AVAILABLE_IN_ALL
void lrg_property_source_emit_changed (LrgPropertySource *self,
                                       const gchar       *name);

/* ==========================================================================
 * LrgObjectPropertySource
 * ========================================================================== */

#define LRG_TYPE_OBJECT_PROPERTY_SOURCE (lrg_object_property_source_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgObjectPropertySource, lrg_object_property_source, LRG, OBJECT_PROPERTY_SOURCE, GObject)

/**
 * lrg_object_property_source_new:
 * @object: the object to expose
 *
 * Wraps @object as an #LrgPropertySource. Every readable property is
 * listed, except construct-only ones and those hidden with
 * lrg_object_property_source_hide_property(); properties that are not
 * writable are read-only. "notify" on @object becomes
 * #LrgPropertySource::property-changed. The source keeps a reference
 * to @object.
 *
 * Returns: (transfer full): a new #LrgObjectPropertySource
 */
LRG_AVAILABLE_IN_ALL
LrgObjectPropertySource * lrg_object_property_source_new (GObject *object);

/**
 * lrg_object_property_source_get_object:
 * @self: an #LrgObjectPropertySource
 *
 * Returns: (transfer none): the wrapped object
 */
LRG_AVAILABLE_IN_ALL
GObject * lrg_object_property_source_get_object (LrgObjectPropertySource *self);

/**
 * lrg_object_property_source_hide_property:
 * @self: an #LrgObjectPropertySource
 * @name: a property name of the object
 *
 * Leaves @name out of the listed properties (internal or noisy
 * properties an editor should not show).
 */
LRG_AVAILABLE_IN_ALL
void lrg_object_property_source_hide_property (LrgObjectPropertySource *self,
                                               const gchar             *name);

/**
 * lrg_object_property_source_set_owner_type:
 * @self: an #LrgObjectPropertySource
 * @owner_type: a #GType, or %G_TYPE_INVALID for all
 *
 * Lists only properties declared by @owner_type or its subclasses (for
 * a widget subclass: hide the #LrgWidget base properties by passing the
 * subclass type).
 */
LRG_AVAILABLE_IN_ALL
void lrg_object_property_source_set_owner_type (LrgObjectPropertySource *self,
                                                GType                    owner_type);

G_END_DECLS
