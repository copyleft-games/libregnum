/* lrg-ui-loader.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgUILoader: Builds widget trees from YAML documents with components and hot reload.
 *
 * A UI document is YAML. Every widget node is a mapping with a `type:`
 * (a GType name such as LrgButton, a name registered in an #LrgRegistry,
 * a kebab-case short name such as `button` or `scroll-box`, or the name of
 * a component declared in the document) and its GObject properties by
 * name, plus a handful of special keys (`id`, `class`, `children`,
 * `child`, `tabs`, `items`, `on`, `look`, `appear`, `min-size`,
 * `max-size`, `if`). Components are reusable templates with parameters,
 * `${expression}` substitution and slots; `repeat`, `for-each`, `include`
 * and `if` generate nodes. A document loaded with
 * lrg_ui_loader_watch_file() follows its files: when one of them is saved,
 * the new tree is diffed against the live widgets, which keep their
 * identity and their state.
 *
 * See docs/modules/ui/declarative-ui.org for the full format reference.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../core/lrg-registry.h"
#include "lrg-widget.h"
#include "lrg-container.h"

G_BEGIN_DECLS

/* ==========================================================================
 * Errors
 * ========================================================================== */

/**
 * LRG_UI_LOADER_ERROR:
 *
 * Error domain for #LrgUILoader. Errors in this domain are
 * #LrgUILoaderError values.
 */
#define LRG_UI_LOADER_ERROR (lrg_ui_loader_error_quark ())

/**
 * LrgUILoaderError:
 * @LRG_UI_LOADER_ERROR_PARSE: the text is not valid YAML
 * @LRG_UI_LOADER_ERROR_IO: a file could not be read
 * @LRG_UI_LOADER_ERROR_UNKNOWN_TYPE: a `type:` names no widget type and
 *   no component
 * @LRG_UI_LOADER_ERROR_UNKNOWN_PROPERTY: a key is neither a special key
 *   nor a writable property of the widget
 * @LRG_UI_LOADER_ERROR_INVALID_VALUE: a value cannot be converted to the
 *   type the property or key expects
 * @LRG_UI_LOADER_ERROR_EXPRESSION: a `${...}` expression is malformed or
 *   failed to evaluate
 * @LRG_UI_LOADER_ERROR_HANDLER: an `on:` entry names an unknown signal or
 *   an unknown handler
 * @LRG_UI_LOADER_ERROR_COMPONENT: a component is malformed, uses itself or
 *   is given a slot it does not have
 * @LRG_UI_LOADER_ERROR_INCLUDE_CYCLE: a file includes itself, directly or
 *   through other files
 * @LRG_UI_LOADER_ERROR_STRUCTURE: the document's shape is wrong (no root,
 *   several roots, children on a widget that cannot hold them, ...)
 *
 * Error codes of the %LRG_UI_LOADER_ERROR domain. Every message starts
 * with the path of the offending node inside the document (for example
 * `children[2].children[0]: unknown property 'colour' on LrgLabel`) and
 * ends with the file, line and column when they are known.
 */
typedef enum
{
	LRG_UI_LOADER_ERROR_PARSE,
	LRG_UI_LOADER_ERROR_IO,
	LRG_UI_LOADER_ERROR_UNKNOWN_TYPE,
	LRG_UI_LOADER_ERROR_UNKNOWN_PROPERTY,
	LRG_UI_LOADER_ERROR_INVALID_VALUE,
	LRG_UI_LOADER_ERROR_EXPRESSION,
	LRG_UI_LOADER_ERROR_HANDLER,
	LRG_UI_LOADER_ERROR_COMPONENT,
	LRG_UI_LOADER_ERROR_INCLUDE_CYCLE,
	LRG_UI_LOADER_ERROR_STRUCTURE
} LrgUILoaderError;

/**
 * lrg_ui_loader_error_quark:
 *
 * Returns: the error quark of #LrgUILoader errors
 */
LRG_AVAILABLE_IN_ALL
GQuark lrg_ui_loader_error_quark (void);

/**
 * lrg_ui_loader_error_get_type:
 *
 * Returns: the #GType of #LrgUILoaderError
 */
LRG_AVAILABLE_IN_ALL
GType lrg_ui_loader_error_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_UI_LOADER_ERROR (lrg_ui_loader_error_get_type ())

/* ==========================================================================
 * LrgUILoader
 * ========================================================================== */

#define LRG_TYPE_UI_LOADER (lrg_ui_loader_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgUILoader, lrg_ui_loader, LRG, UI_LOADER, GObject)

/**
 * lrg_ui_loader_new:
 *
 * Creates a loader with no handlers, no variables and no registry.
 *
 * Returns: (transfer full): a new #LrgUILoader
 */
LRG_AVAILABLE_IN_ALL
LrgUILoader * lrg_ui_loader_new (void);

/**
 * lrg_ui_loader_set_registry:
 * @self: an #LrgUILoader
 * @registry: (nullable): a registry of short type names, or %NULL
 *
 * Sets a registry consulted first when resolving `type:` names, so
 * `type: gauge` can name a type registered with
 * lrg_registry_register (registry, "gauge", MY_TYPE_GAUGE). The loader
 * keeps a reference.
 */
LRG_AVAILABLE_IN_ALL
void lrg_ui_loader_set_registry (LrgUILoader *self,
                                 LrgRegistry *registry);

/**
 * lrg_ui_loader_get_registry:
 * @self: an #LrgUILoader
 *
 * Returns: (transfer none) (nullable): the registry set with
 *   lrg_ui_loader_set_registry()
 */
LRG_AVAILABLE_IN_ALL
LrgRegistry * lrg_ui_loader_get_registry (LrgUILoader *self);

/**
 * lrg_ui_loader_add_handler:
 * @self: an #LrgUILoader
 * @name: the handler name used in documents (`on: {clicked: name}`)
 * @callback: (scope forever): the signal handler, with the signature the
 *   signal expects
 * @user_data: data passed to @callback as its last argument
 *
 * Registers a named signal handler. Documents connect it with an `on:`
 * mapping. Registering a name again replaces the previous handler for
 * documents loaded afterwards.
 */
LRG_AVAILABLE_IN_ALL
void lrg_ui_loader_add_handler (LrgUILoader *self,
                                const gchar *name,
                                GCallback    callback,
                                gpointer     user_data);

/**
 * lrg_ui_loader_set_use_symbols:
 * @self: an #LrgUILoader
 * @use_symbols: whether to look unknown handler names up as symbols
 *
 * When %TRUE, a handler name that was not registered with
 * lrg_ui_loader_add_handler() is looked up as a global symbol of the
 * program (with #GModule); it is connected with %NULL user data. The
 * program must export the symbol (link it with `-rdynamic` or
 * `-Wl,--export-dynamic`). Default: %FALSE.
 */
LRG_AVAILABLE_IN_ALL
void lrg_ui_loader_set_use_symbols (LrgUILoader *self,
                                    gboolean     use_symbols);

/**
 * lrg_ui_loader_get_use_symbols:
 * @self: an #LrgUILoader
 *
 * Returns: whether unknown handler names are looked up as symbols
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_ui_loader_get_use_symbols (LrgUILoader *self);

/**
 * lrg_ui_loader_set_variable:
 * @self: an #LrgUILoader
 * @name: the variable name used in `${...}` expressions
 * @value: (nullable): a boolean, numeric or string value (other types are
 *   converted to text), or %NULL to remove the variable
 *
 * Sets a global variable visible to every expression of the documents
 * loaded afterwards (and of watched documents when they reload). Call
 * lrg_ui_loader_reload() to apply a change to watched documents now.
 */
LRG_AVAILABLE_IN_ALL
void lrg_ui_loader_set_variable (LrgUILoader  *self,
                                 const gchar  *name,
                                 const GValue *value);

/**
 * lrg_ui_loader_load_from_data:
 * @self: an #LrgUILoader
 * @data: a YAML document
 * @length: length of @data in bytes, or -1 if it is nul-terminated
 * @error: return location for a #GError
 *
 * Builds the widget tree described by @data. `include:` paths are
 * relative to the current directory.
 *
 * Returns: (transfer full) (nullable): the root widget, or %NULL with
 *   @error set
 */
LRG_AVAILABLE_IN_ALL
LrgWidget * lrg_ui_loader_load_from_data (LrgUILoader  *self,
                                          const gchar  *data,
                                          gssize        length,
                                          GError      **error);

/**
 * lrg_ui_loader_load_from_file:
 * @self: an #LrgUILoader
 * @path: (type filename): a YAML file
 * @error: return location for a #GError
 *
 * Builds the widget tree described by the file at @path. `include:`
 * paths are relative to the including file. The file is not watched; see
 * lrg_ui_loader_watch_file().
 *
 * Returns: (transfer full) (nullable): the root widget, or %NULL with
 *   @error set
 */
LRG_AVAILABLE_IN_ALL
LrgWidget * lrg_ui_loader_load_from_file (LrgUILoader  *self,
                                          const gchar  *path,
                                          GError      **error);

/**
 * lrg_ui_loader_get_widget:
 * @self: an #LrgUILoader
 * @id: an `id:` given in a document
 *
 * Finds a live widget built by this loader by its id. The most recently
 * loaded document wins when several define the same id; within one
 * document the first widget in depth-first order wins. The loader does
 * not keep the widget alive.
 *
 * Returns: (transfer none) (nullable): the widget, or %NULL
 */
LRG_AVAILABLE_IN_ALL
LrgWidget * lrg_ui_loader_get_widget (LrgUILoader *self,
                                      const gchar *id);

/**
 * lrg_ui_loader_watch_file:
 * @self: an #LrgUILoader
 * @path: (type filename): a YAML file
 * @host: the container (an #LrgCanvas, a panel, ...) the root widget is
 *   added to
 * @error: return location for a #GError
 *
 * Builds the document at @path, adds its root to @host and watches the
 * file and every file it includes. lrg_ui_loader_poll() (or a file
 * monitor, see lrg_ui_loader_set_use_monitors()) notices changes; the new
 * document is then diffed against the live tree: widgets are matched by
 * `id`, then by type and order, reused and given only the values that
 * changed, so they keep their identity and their state. A document with a
 * mistake changes nothing and emits #LrgUILoader::reload-failed.
 *
 * Returns: (transfer none) (nullable): the root widget (owned by @host),
 *   or %NULL with @error set
 */
LRG_AVAILABLE_IN_ALL
LrgWidget * lrg_ui_loader_watch_file (LrgUILoader   *self,
                                      const gchar   *path,
                                      LrgContainer  *host,
                                      GError       **error);

/**
 * lrg_ui_loader_unwatch_file:
 * @self: an #LrgUILoader
 * @path: (type filename): a path given to lrg_ui_loader_watch_file()
 *
 * Stops watching the document. Its widgets stay where they are.
 *
 * Returns: %TRUE if the document was watched
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_ui_loader_unwatch_file (LrgUILoader *self,
                                     const gchar *path);

/**
 * lrg_ui_loader_poll:
 * @self: an #LrgUILoader
 *
 * Checks the watched files and reloads the documents whose files
 * changed. Without monitors this is a stat() of each file (modification
 * time, size and inode), cheap enough to call every frame. With monitors
 * (lrg_ui_loader_set_use_monitors()) it only reloads the documents a
 * monitor flagged. Never blocks and starts no thread.
 *
 * Returns: %TRUE if at least one document was reloaded or failed to
 *   reload
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_ui_loader_poll (LrgUILoader *self);

/**
 * lrg_ui_loader_reload:
 * @self: an #LrgUILoader
 * @error: return location for the first #GError
 *
 * Reloads every watched document now, changed or not (for example after
 * lrg_ui_loader_set_variable()).
 *
 * Returns: %TRUE if every document reloaded
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_ui_loader_reload (LrgUILoader  *self,
                               GError      **error);

/**
 * lrg_ui_loader_set_use_monitors:
 * @self: an #LrgUILoader
 * @use_monitors: whether to watch files with #GFileMonitor
 *
 * When %TRUE, watched files get a #GFileMonitor that flags their document
 * as changed, and lrg_ui_loader_poll() stops calling stat(). Monitors
 * deliver their notifications through the thread-default #GMainContext,
 * so the application must iterate it (g_main_context_iteration()).
 * Default: %FALSE.
 */
LRG_AVAILABLE_IN_ALL
void lrg_ui_loader_set_use_monitors (LrgUILoader *self,
                                     gboolean     use_monitors);

/**
 * lrg_ui_loader_get_use_monitors:
 * @self: an #LrgUILoader
 *
 * Returns: whether watched files use #GFileMonitor
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_ui_loader_get_use_monitors (LrgUILoader *self);

G_END_DECLS
