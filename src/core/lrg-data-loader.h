/* lrg-data-loader.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Data loader for loading GObjects from YAML files.
 *
 * The data loader integrates with the type registry to enable
 * fully data-driven object creation. YAML files can specify a
 * "type" field that maps to a registered GType.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <gio/gio.h>
#ifdef LRG_HAS_LIBDEX
#include <libdex.h>
#endif
#include "../lrg-version.h"
#include "../lrg-types.h"
#include "../lrg-enums.h"

G_BEGIN_DECLS

#define LRG_TYPE_DATA_LOADER (lrg_data_loader_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgDataLoader, lrg_data_loader, LRG, DATA_LOADER, GObject)

/* ==========================================================================
 * Construction
 * ========================================================================== */

/**
 * lrg_data_loader_new:
 *
 * Creates a new data loader.
 *
 * Note: The loader needs a registry to resolve type names. Call
 * lrg_data_loader_set_registry() before loading typed objects.
 *
 * Returns: (transfer full): A new #LrgDataLoader
 */
LRG_AVAILABLE_IN_ALL
LrgDataLoader * lrg_data_loader_new (void);

/* ==========================================================================
 * Registry
 * ========================================================================== */

/**
 * lrg_data_loader_set_registry:
 * @self: an #LrgDataLoader
 * @registry: (nullable): the #LrgRegistry to use for type lookups
 *
 * Sets the registry used to resolve type names in YAML files.
 *
 * When loading a YAML file that contains a "type" field at the root
 * level, the loader uses the registry to look up the corresponding
 * GType and deserialize the object.
 */
LRG_AVAILABLE_IN_ALL
void lrg_data_loader_set_registry (LrgDataLoader *self,
                                   LrgRegistry   *registry);

/**
 * lrg_data_loader_get_registry:
 * @self: an #LrgDataLoader
 *
 * Gets the registry used for type lookups.
 *
 * Returns: (transfer none) (nullable): The #LrgRegistry, or %NULL
 */
LRG_AVAILABLE_IN_ALL
LrgRegistry * lrg_data_loader_get_registry (LrgDataLoader *self);

/* ==========================================================================
 * Synchronous Loading
 * ========================================================================== */

/**
 * lrg_data_loader_load_file:
 * @self: an #LrgDataLoader
 * @path: path to the YAML file
 * @error: (nullable): return location for error
 *
 * Loads a GObject from a YAML file.
 *
 * The YAML file must have a "type" field at the root level that
 * corresponds to a registered type name. The remaining fields
 * are used to set object properties.
 *
 * Example YAML (a player with a name and health):
 * |[<!-- language="YAML" -->
 *   type: player
 *   name: "Hero"
 *   health: 100
 * ]|
 *
 * Returns: (transfer full) (nullable): The loaded #GObject, or %NULL on error
 */
LRG_AVAILABLE_IN_ALL
GObject * lrg_data_loader_load_file (LrgDataLoader  *self,
                                     const gchar    *path,
                                     GError        **error);

/**
 * lrg_data_loader_load_file_validated:
 * @self: an #LrgDataLoader
 * @path: path to the YAML file
 * @error: (nullable): return location for error
 *
 * Loads a definition with strict property validation. Rejects unknown or
 * unwritable fields, malformed scalar values, invalid enums/flags and values
 * outside GParamSpec bounds. Null numeric, boolean, enum and flags values
 * are rejected. Nullable strings, string arrays and raw YAML nodes are
 * supported. Custom non-construct properties must implement
 * YamlSerializable and accept their nodes. Each custom decoder runs once;
 * its output is validated before setting the property. Application-specific invariants
 * remain the responsibility of those custom deserializers. Integer values
 * must fit signed 64-bit, matching yaml-glib's numeric representation.
 * Existing permissive loading APIs are unchanged.
 *
 * Returns: (transfer full) (nullable): the validated object, or %NULL on error
 */
LRG_AVAILABLE_IN_ALL
GObject * lrg_data_loader_load_file_validated (LrgDataLoader  *self,
                                               const gchar    *path,
                                               GError        **error);

/**
 * lrg_data_loader_load_gfile:
 * @self: an #LrgDataLoader
 * @file: a #GFile to load
 * @cancellable: (nullable): a #GCancellable
 * @error: (nullable): return location for error
 *
 * Loads a GObject from a #GFile.
 *
 * Returns: (transfer full) (nullable): The loaded #GObject, or %NULL on error
 */
LRG_AVAILABLE_IN_ALL
GObject * lrg_data_loader_load_gfile (LrgDataLoader  *self,
                                      GFile          *file,
                                      GCancellable   *cancellable,
                                      GError        **error);

/**
 * lrg_data_loader_load_data:
 * @self: an #LrgDataLoader
 * @data: the YAML string
 * @length: length of @data, or -1 if null-terminated
 * @error: (nullable): return location for error
 *
 * Loads a GObject from a YAML string.
 *
 * Returns: (transfer full) (nullable): The loaded #GObject, or %NULL on error
 */
LRG_AVAILABLE_IN_ALL
GObject * lrg_data_loader_load_data (LrgDataLoader  *self,
                                     const gchar    *data,
                                     gssize          length,
                                     GError        **error);

/**
 * lrg_data_loader_load_typed:
 * @self: an #LrgDataLoader
 * @type: the #GType to deserialize as
 * @path: path to the YAML file
 * @error: (nullable): return location for error
 *
 * Loads a GObject of a specific type from a YAML file.
 *
 * Unlike lrg_data_loader_load_file(), this does not require a
 * "type" field in the YAML. The entire file is deserialized
 * directly to the specified type.
 *
 * Returns: (transfer full) (nullable): The loaded #GObject, or %NULL on error
 */
LRG_AVAILABLE_IN_ALL
GObject * lrg_data_loader_load_typed (LrgDataLoader  *self,
                                      GType           type,
                                      const gchar    *path,
                                      GError        **error);

/* ==========================================================================
 * Batch Loading
 * ========================================================================== */

/**
 * lrg_data_loader_load_directory:
 * @self: an #LrgDataLoader
 * @directory: path to directory containing YAML files
 * @recursive: whether to recurse into subdirectories
 * @error: (nullable): return location for error
 *
 * Loads all YAML files from a directory.
 *
 * Files are loaded in alphabetical order. Files that fail to load
 * are skipped (with a warning logged), and loading continues.
 *
 * Returns: (transfer full) (element-type GObject): A #GList of loaded
 *          objects. Free with g_list_free_full(list, g_object_unref).
 */
LRG_AVAILABLE_IN_ALL
GList * lrg_data_loader_load_directory (LrgDataLoader  *self,
                                        const gchar    *directory,
                                        gboolean        recursive,
                                        GError        **error);

/**
 * lrg_data_loader_load_files:
 * @self: an #LrgDataLoader
 * @paths: (array zero-terminated=1): NULL-terminated array of file paths
 * @error: (nullable): return location for error
 *
 * Loads multiple YAML files.
 *
 * Returns: (transfer full) (element-type GObject): A #GList of loaded
 *          objects. Free with g_list_free_full(list, g_object_unref).
 */
LRG_AVAILABLE_IN_ALL
GList * lrg_data_loader_load_files (LrgDataLoader   *self,
                                    const gchar    **paths,
                                    GError         **error);

#ifdef LRG_HAS_LIBDEX
/* ==========================================================================
 * Asynchronous Loading (libdex futures)
 * ========================================================================== */

/**
 * lrg_data_loader_load_file_async:
 * @self: an #LrgDataLoader
 * @path: path to the YAML file
 *
 * Asynchronously loads a GObject from a YAML file.
 *
 * Returns: (transfer full): A #DexFuture that resolves to a #GObject
 */
LRG_AVAILABLE_IN_ALL
DexFuture * lrg_data_loader_load_file_async (LrgDataLoader *self,
                                             const gchar   *path);

/**
 * lrg_data_loader_load_gfile_async:
 * @self: an #LrgDataLoader
 * @file: a #GFile to load
 *
 * Asynchronously loads a GObject from a #GFile.
 *
 * Returns: (transfer full): A #DexFuture that resolves to a #GObject
 */
LRG_AVAILABLE_IN_ALL
DexFuture * lrg_data_loader_load_gfile_async (LrgDataLoader *self,
                                              GFile         *file);

/**
 * lrg_data_loader_load_directory_async:
 * @self: an #LrgDataLoader
 * @directory: path to directory containing YAML files
 * @recursive: whether to recurse into subdirectories
 *
 * Asynchronously loads all YAML files from a directory.
 *
 * Returns: (transfer full): A #DexFuture that resolves to a #GList
 *          of #GObject instances
 */
LRG_AVAILABLE_IN_ALL
DexFuture * lrg_data_loader_load_directory_async (LrgDataLoader *self,
                                                  const gchar   *directory,
                                                  gboolean       recursive);
#endif /* LRG_HAS_LIBDEX */

/* ==========================================================================
 * Utility
 * ========================================================================== */

/**
 * lrg_data_loader_get_type_field_name:
 * @self: an #LrgDataLoader
 *
 * Gets the field name used to identify object types in YAML files.
 *
 * Default is "type".
 *
 * Returns: (transfer none): The type field name
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_data_loader_get_type_field_name (LrgDataLoader *self);

/**
 * lrg_data_loader_set_type_field_name:
 * @self: an #LrgDataLoader
 * @field_name: the field name to use
 *
 * Sets the field name used to identify object types in YAML files.
 */
LRG_AVAILABLE_IN_ALL
void lrg_data_loader_set_type_field_name (LrgDataLoader *self,
                                          const gchar   *field_name);

/**
 * lrg_data_loader_get_file_extensions:
 * @self: an #LrgDataLoader
 *
 * Gets the file extensions recognized by directory loading.
 *
 * Default is ".yaml" and ".yml".
 *
 * Returns: (transfer none) (array zero-terminated=1): The extensions array
 */
LRG_AVAILABLE_IN_ALL
const gchar * const * lrg_data_loader_get_file_extensions (LrgDataLoader *self);

/**
 * lrg_data_loader_set_file_extensions:
 * @self: an #LrgDataLoader
 * @extensions: (array zero-terminated=1): NULL-terminated array of extensions
 *
 * Sets the file extensions recognized by directory loading.
 */
LRG_AVAILABLE_IN_ALL
void lrg_data_loader_set_file_extensions (LrgDataLoader   *self,
                                          const gchar    **extensions);

/* ==========================================================================
 * Composite documents: containers
 * ========================================================================== */

/**
 * LRG_DATA_LOADER_REF_KEY:
 *
 * The key of a reference mapping in a composite document. A mapping whose
 * only key is `ref` (for example `{ ref: hero }`) stands for an object
 * resolved after the whole document has been built: an object of the same
 * document (by its `guid` or `id`, or by a `#/pointer`), or one returned by
 * the loader's #LrgDataResolveFunc.
 */
#define LRG_DATA_LOADER_REF_KEY "ref"

#define LRG_TYPE_DATA_CONTAINER (lrg_data_container_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_INTERFACE (LrgDataContainer, lrg_data_container, LRG, DATA_CONTAINER, GObject)

/**
 * LrgDataContainerInterface:
 * @parent_iface: the parent interface
 * @add_child: appends a child built from the `children:` list (or resolved
 *   from a reference in it); returns %FALSE with @error set to reject it
 * @get_n_children: the number of children, for saving
 * @get_child: the child at an index (transfer none), for saving
 * @get_child_type: (nullable): the type a child mapping without `type:`
 *   is built as; %G_TYPE_INVALID (or a %NULL vfunc) makes `type:` required
 *
 * The interface of objects that own an ordered list of typed children in
 * composite documents. #LrgDataLoader builds each element of the
 * `children:` list (the loader's #LrgDataLoader:children-key) and passes it
 * to @add_child in document order; lrg_data_loader_save_to_data() writes
 * the children back through @get_n_children and @get_child.
 */
struct _LrgDataContainerInterface
{
    GTypeInterface parent_iface;

    /*< public >*/
    gboolean  (*add_child)      (LrgDataContainer  *self,
                                 GObject           *child,
                                 GError           **error);
    guint     (*get_n_children) (LrgDataContainer  *self);
    GObject * (*get_child)      (LrgDataContainer  *self,
                                 guint              index);
    GType     (*get_child_type) (LrgDataContainer  *self);

    /*< private >*/
    gpointer _reserved[8];
};

/**
 * lrg_data_container_add_child:
 * @self: an #LrgDataContainer
 * @child: (transfer none): the child to append
 * @error: (nullable): return location for error
 *
 * Appends @child to @self's children. The container takes its own
 * reference when it keeps the child.
 *
 * Returns: %TRUE on success, %FALSE with @error set when @self rejects it
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_data_container_add_child (LrgDataContainer  *self,
                                       GObject           *child,
                                       GError           **error);

/**
 * lrg_data_container_get_n_children:
 * @self: an #LrgDataContainer
 *
 * Gets the number of children @self holds.
 *
 * Returns: the number of children
 */
LRG_AVAILABLE_IN_ALL
guint lrg_data_container_get_n_children (LrgDataContainer *self);

/**
 * lrg_data_container_get_child:
 * @self: an #LrgDataContainer
 * @index: the child index
 *
 * Gets the child at @index.
 *
 * Returns: (transfer none) (nullable): the child, or %NULL when @index is
 *   out of range
 */
LRG_AVAILABLE_IN_ALL
GObject * lrg_data_container_get_child (LrgDataContainer *self,
                                        guint             index);

/**
 * lrg_data_container_get_child_type:
 * @self: an #LrgDataContainer
 *
 * Gets the type that a child mapping without a `type:` key is built as.
 *
 * Returns: the default child type, or %G_TYPE_INVALID when every child
 *   must name its type
 */
LRG_AVAILABLE_IN_ALL
GType lrg_data_container_get_child_type (LrgDataContainer *self);

/* ==========================================================================
 * Composite documents: references
 * ========================================================================== */

/**
 * LrgDataResolveFunc:
 * @loader: the #LrgDataLoader resolving the reference
 * @ref: the reference text (the value of `ref:`), for example a GUID or
 *   an asset path such as `materials/stone.rmat`
 * @expected_type: the type the slot holding the reference accepts
 * @user_data: (closure): the data given to lrg_data_loader_set_resolver()
 * @error: (nullable): return location for error
 *
 * Resolves a reference that is not an object of the document being loaded.
 * It runs after the whole document has been built, so it may look things
 * up anywhere (an asset manager, a GUID table, another loaded document).
 * Returning %NULL without setting @error reports the reference as
 * unresolved.
 *
 * Returns: (transfer full) (nullable): the object, or %NULL
 */
typedef GObject * (*LrgDataResolveFunc) (LrgDataLoader  *loader,
                                         const gchar    *ref,
                                         GType           expected_type,
                                         gpointer        user_data,
                                         GError        **error);

/**
 * lrg_data_loader_set_resolver:
 * @self: an #LrgDataLoader
 * @func: (nullable) (scope notified) (closure user_data) (destroy destroy):
 *   the resolver, or %NULL to only resolve references within the document
 * @user_data: data for @func
 * @destroy: (nullable): frees @user_data when the resolver is replaced
 *
 * Sets the resolver used for `ref:` values that name no object of the
 * document itself. Objects returned by the resolver are tagged with their
 * reference (see lrg_data_loader_get_object_ref()), so saving writes them
 * back as `ref:` instead of inline.
 */
LRG_AVAILABLE_IN_ALL
void lrg_data_loader_set_resolver (LrgDataLoader      *self,
                                   LrgDataResolveFunc  func,
                                   gpointer            user_data,
                                   GDestroyNotify      destroy);

/**
 * lrg_data_loader_get_object_ref:
 * @object: a #GObject
 *
 * Gets the reference @object was resolved from, or was marked with by
 * lrg_data_loader_set_object_ref().
 *
 * Returns: (transfer none) (nullable): the reference, or %NULL
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_data_loader_get_object_ref (GObject *object);

/**
 * lrg_data_loader_set_object_ref:
 * @object: a #GObject
 * @ref: (nullable): the reference, or %NULL to clear it
 *
 * Marks @object as living outside the documents that use it: saving writes
 * `ref: <ref>` wherever @object appears instead of writing it inline. Use
 * it for shared assets (materials, textures, sounds) that a document points
 * at.
 */
LRG_AVAILABLE_IN_ALL
void lrg_data_loader_set_object_ref (GObject     *object,
                                     const gchar *ref);

/* ==========================================================================
 * Composite documents: schema hints
 * ========================================================================== */

/**
 * lrg_data_loader_set_param_item_type:
 * @pspec: a #GParamSpec of a #GListModel or #GPtrArray property
 * @item_type: the #GObject type of the list's items
 *
 * Declares the item type of a list property, so a composite document may
 * give the property a sequence of mappings (each without `type:` when it is
 * exactly @item_type). Call it in class_init, right after creating @pspec.
 * A #GPtrArray property is only composite once it has an item type; a
 * #GListModel property defaults to the item type of the #GListStore it
 * holds.
 */
LRG_AVAILABLE_IN_ALL
void lrg_data_loader_set_param_item_type (GParamSpec *pspec,
                                          GType       item_type);

/**
 * lrg_data_loader_get_param_item_type:
 * @pspec: a #GParamSpec
 *
 * Gets the item type declared with lrg_data_loader_set_param_item_type().
 *
 * Returns: the item type, or %G_TYPE_INVALID
 */
LRG_AVAILABLE_IN_ALL
GType lrg_data_loader_get_param_item_type (GParamSpec *pspec);

/* ==========================================================================
 * Composite documents: loading and saving
 * ========================================================================== */

/**
 * lrg_data_loader_get_strict:
 * @self: an #LrgDataLoader
 *
 * Gets whether composite loading rejects unknown keys.
 *
 * Returns: %TRUE when unknown keys are errors (the default)
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_data_loader_get_strict (LrgDataLoader *self);

/**
 * lrg_data_loader_set_strict:
 * @self: an #LrgDataLoader
 * @strict: whether unknown keys are errors
 *
 * Sets whether composite loading rejects unknown keys. When %FALSE they are
 * logged as warnings and skipped; wrong values, unknown types and
 * unresolved references are errors either way. Only the composite APIs
 * use it.
 */
LRG_AVAILABLE_IN_ALL
void lrg_data_loader_set_strict (LrgDataLoader *self,
                                 gboolean       strict);

/**
 * lrg_data_loader_get_children_key:
 * @self: an #LrgDataLoader
 *
 * Gets the key holding an #LrgDataContainer's child list.
 *
 * Returns: (transfer none): the key, `children` by default
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_data_loader_get_children_key (LrgDataLoader *self);

/**
 * lrg_data_loader_set_children_key:
 * @self: an #LrgDataLoader
 * @key: the key holding an #LrgDataContainer's child list
 *
 * Sets the key holding an #LrgDataContainer's child list. It only has that
 * meaning on objects implementing #LrgDataContainer; on other objects it is
 * an ordinary property name.
 */
LRG_AVAILABLE_IN_ALL
void lrg_data_loader_set_children_key (LrgDataLoader *self,
                                       const gchar   *key);

/**
 * lrg_data_loader_load_composite_data:
 * @self: an #LrgDataLoader
 * @data: the YAML text
 * @length: length of @data, or -1 if nul-terminated
 * @root_type: the root object's type, or %G_TYPE_INVALID to take it from
 *   the root `type:` key
 * @source_name: (nullable): the name used in error messages (a path), or
 *   %NULL for `(data)`
 * @error: (nullable): return location for error
 *
 * Loads a composite document: a tree of typed objects with typed
 * sub-objects in object properties, lists of typed objects in #GListModel
 * and #GPtrArray properties, child lists on #LrgDataContainer objects and
 * `ref:` references resolved after the whole tree is built (so forward
 * references work). Validation is strict: unknown keys (see
 * lrg_data_loader_set_strict()), values of the wrong type or out of range,
 * unknown or mismatched types and unresolved references fail, and the
 * error message lists every problem as `source:line:column: /path: text`.
 * A flat document loads exactly as with lrg_data_loader_load_file_validated().
 *
 * Returns: (transfer full) (nullable): the root object, or %NULL on error
 */
LRG_AVAILABLE_IN_ALL
GObject * lrg_data_loader_load_composite_data (LrgDataLoader  *self,
                                               const gchar    *data,
                                               gssize          length,
                                               GType           root_type,
                                               const gchar    *source_name,
                                               GError        **error);

/**
 * lrg_data_loader_load_composite_file:
 * @self: an #LrgDataLoader
 * @path: (type filename): the YAML file
 * @root_type: the root object's type, or %G_TYPE_INVALID to take it from
 *   the root `type:` key
 * @error: (nullable): return location for error
 *
 * Loads a composite document from a file; see
 * lrg_data_loader_load_composite_data().
 *
 * Returns: (transfer full) (nullable): the root object, or %NULL on error
 */
LRG_AVAILABLE_IN_ALL
GObject * lrg_data_loader_load_composite_file (LrgDataLoader  *self,
                                               const gchar    *path,
                                               GType           root_type,
                                               GError        **error);

/**
 * lrg_data_loader_save_to_data:
 * @self: an #LrgDataLoader
 * @object: the root object
 * @length: (out) (optional): return location for the text length
 * @error: (nullable): return location for error
 *
 * Writes @object as a composite document that
 * lrg_data_loader_load_composite_data() reads back into an equal tree. The
 * root and every object whose type differs from what its slot implies get
 * a `type:` key (their registry name; an unregistered type is an error).
 * Properties are written when readable and writable and different from a
 * freshly constructed instance of the same type; object properties become
 * nested mappings, list properties sequences, #LrgDataContainer children a
 * `children:` list. An object met a second time becomes a `ref:` (by its
 * unique `guid` or `id`, else by `#/pointer`), and an object marked with
 * lrg_data_loader_set_object_ref() always does. The output is
 * deterministic: saving what was loaded from saved text gives the same
 * bytes.
 *
 * Returns: (transfer full) (nullable): the YAML text, or %NULL on error
 */
LRG_AVAILABLE_IN_ALL
gchar * lrg_data_loader_save_to_data (LrgDataLoader  *self,
                                      GObject        *object,
                                      gsize          *length,
                                      GError        **error);

/**
 * lrg_data_loader_save_to_file:
 * @self: an #LrgDataLoader
 * @object: the root object
 * @path: (type filename): the file to write
 * @error: (nullable): return location for error
 *
 * Writes @object to @path; see lrg_data_loader_save_to_data().
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_data_loader_save_to_file (LrgDataLoader  *self,
                                       GObject        *object,
                                       const gchar    *path,
                                       GError        **error);

#ifndef __GI_SCANNER__
#ifdef LIBREGNUM_COMPILATION
/*
 * In-tree helpers for format loaders and writers (not installed API: they
 * expose yaml-glib types, which the public GIR does not include). See
 * docs/guides/writing-a-data-format.org.
 */
#include <yaml-glib.h>

G_GNUC_INTERNAL
GObject * lrg_data_loader_load_composite_node (LrgDataLoader  *self,
                                               YamlNode       *root,
                                               GType           root_type,
                                               const gchar    *source_name,
                                               GError        **error);
G_GNUC_INTERNAL
YamlNode * lrg_data_loader_save_to_node (LrgDataLoader  *self,
                                         GObject        *object,
                                         GError        **error);
G_GNUC_INTERNAL
YamlNode * lrg_data_yaml_string (const gchar *text);
G_GNUC_INTERNAL
YamlNode * lrg_data_yaml_double (gdouble value);
G_GNUC_INTERNAL
YamlNode * lrg_data_yaml_float (gfloat value);
G_GNUC_INTERNAL
YamlNode * lrg_data_yaml_int (gint64 value);
G_GNUC_INTERNAL
YamlNode * lrg_data_yaml_boolean (gboolean value);
G_GNUC_INTERNAL
gchar * lrg_data_yaml_to_data (YamlNode  *root,
                               gsize     *length,
                               GError   **error);
G_GNUC_INTERNAL
gboolean lrg_data_yaml_to_file (YamlNode     *root,
                                const gchar  *path,
                                GError      **error);
#endif /* LIBREGNUM_COMPILATION */
#endif /* __GI_SCANNER__ */

G_END_DECLS
