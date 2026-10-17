/* lrg-save-migrator.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgSaveMigration / LrgSaveMigrator - chained conversion of saved data
 * written by older versions of a game (or of a server's stored state)
 * to the current layout, one version step at a time, so a player who
 * skips several releases still keeps every character.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-types.h"
#include "../lrg-enums.h"
#include "lrg-save-document.h"

G_BEGIN_DECLS

/**
 * LrgSaveDocumentStepFunc:
 * @document: the document to convert, in place (a working copy: a
 *   failure anywhere in the chain discards it)
 * @user_data: the data given with the step
 * @error: return location for a #GError
 *
 * Converts a whole document from the step's format version to the
 * next. The migrator sets the format version itself after a successful
 * step. A step that creates or splits sections sets their versions.
 *
 * Returns: %TRUE on success, %FALSE with @error set
 */
typedef gboolean (*LrgSaveDocumentStepFunc) (LrgSaveDocument  *document,
                                             gpointer          user_data,
                                             GError          **error);

/**
 * LrgSaveSectionStepFunc:
 * @state: the section's state at the step's version
 * @user_data: the data given with the step
 * @error: return location for a #GError
 *
 * Converts one section's state from the step's version to the next.
 *
 * Returns: (transfer full) (nullable): the converted state (a floating
 *   reference is sunk), or %NULL with @error set
 */
typedef GVariant *(*LrgSaveSectionStepFunc) (GVariant  *state,
                                             gpointer   user_data,
                                             GError   **error);

/* ==========================================================================
 * LrgSaveMigration
 * ========================================================================== */

/**
 * LrgSaveMigration:
 *
 * One conversion step: from version N (the "from" version) to N + 1,
 * of the whole document (%LRG_SAVE_MIGRATION_SCOPE_DOCUMENT) or of one
 * named section (%LRG_SAVE_MIGRATION_SCOPE_SECTION). Immutable once
 * added to a migrator, except for the output type, which is set before.
 * Reference counted.
 */

#define LRG_TYPE_SAVE_MIGRATION (lrg_save_migration_get_type ())

LRG_AVAILABLE_IN_ALL
GType lrg_save_migration_get_type (void) G_GNUC_CONST;

/**
 * lrg_save_migration_new_document:
 * @from_version: the format version the step reads (at least 1)
 * @description: (nullable): what the step changes, for logs and errors
 * @func: (scope notified) (closure user_data) (destroy destroy): the conversion
 * @user_data: data for @func
 * @destroy: (nullable): frees @user_data with the step
 *
 * Returns: (transfer full): a document step from @from_version to
 *   @from_version + 1
 */
LRG_AVAILABLE_IN_ALL
LrgSaveMigration *
lrg_save_migration_new_document (guint                   from_version,
                                 const gchar            *description,
                                 LrgSaveDocumentStepFunc func,
                                 gpointer                user_data,
                                 GDestroyNotify          destroy);

/**
 * lrg_save_migration_new_section:
 * @section: the section's name
 * @from_version: the section version the step reads (at least 1)
 * @description: (nullable): what the step changes, for logs and errors
 * @func: (scope notified) (closure user_data) (destroy destroy): the conversion
 * @user_data: data for @func
 * @destroy: (nullable): frees @user_data with the step
 *
 * Returns: (transfer full): a section step from @from_version to
 *   @from_version + 1
 */
LRG_AVAILABLE_IN_ALL
LrgSaveMigration *
lrg_save_migration_new_section (const gchar            *section,
                                guint                   from_version,
                                const gchar            *description,
                                LrgSaveSectionStepFunc  func,
                                gpointer                user_data,
                                GDestroyNotify          destroy);

/**
 * lrg_save_migration_ref:
 * @self: an #LrgSaveMigration
 *
 * Returns: (transfer full): @self
 */
LRG_AVAILABLE_IN_ALL
LrgSaveMigration *
lrg_save_migration_ref (LrgSaveMigration *self);

/**
 * lrg_save_migration_unref:
 * @self: (transfer full): an #LrgSaveMigration
 *
 * Drops a reference; the last one calls the destroy notify.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_save_migration_unref (LrgSaveMigration *self);

/**
 * lrg_save_migration_get_scope:
 * @self: an #LrgSaveMigration
 *
 * Returns: what the step converts
 */
LRG_AVAILABLE_IN_ALL
LrgSaveMigrationScope
lrg_save_migration_get_scope (LrgSaveMigration *self);

/**
 * lrg_save_migration_get_section:
 * @self: an #LrgSaveMigration
 *
 * Returns: (transfer none) (nullable): the section of a section step,
 *   %NULL for a document step
 */
LRG_AVAILABLE_IN_ALL
const gchar *
lrg_save_migration_get_section (LrgSaveMigration *self);

/**
 * lrg_save_migration_get_from_version:
 * @self: an #LrgSaveMigration
 *
 * Returns: the version the step reads
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_save_migration_get_from_version (LrgSaveMigration *self);

/**
 * lrg_save_migration_get_to_version:
 * @self: an #LrgSaveMigration
 *
 * Returns: the version the step writes (from + 1)
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_save_migration_get_to_version (LrgSaveMigration *self);

/**
 * lrg_save_migration_get_description:
 * @self: an #LrgSaveMigration
 *
 * Returns: (transfer none): the description ("" without one)
 */
LRG_AVAILABLE_IN_ALL
const gchar *
lrg_save_migration_get_description (LrgSaveMigration *self);

/**
 * lrg_save_migration_set_output_type:
 * @self: a section step not yet added to a migrator
 * @type: (nullable): the type every state the step produces must have,
 *   %NULL for any
 *
 * A step whose result has another type fails with
 * %LRG_SAVE_ERROR_STEP_FAILED.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_save_migration_set_output_type (LrgSaveMigration   *self,
                                    const GVariantType *type);

/**
 * lrg_save_migration_get_output_type:
 * @self: an #LrgSaveMigration
 *
 * Returns: (transfer none) (nullable): the expected output type
 */
LRG_AVAILABLE_IN_ALL
const GVariantType *
lrg_save_migration_get_output_type (LrgSaveMigration *self);

/**
 * lrg_save_migration_to_string:
 * @self: an #LrgSaveMigration
 *
 * Returns: (transfer full): "format 1 -> 2: description" or
 *   "section \"name\" 1 -> 2: description"
 */
LRG_AVAILABLE_IN_ALL
gchar *
lrg_save_migration_to_string (LrgSaveMigration *self);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (LrgSaveMigration, lrg_save_migration_unref)

/* ==========================================================================
 * LrgSaveMigrator
 * ========================================================================== */

#define LRG_TYPE_SAVE_MIGRATOR (lrg_save_migrator_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgSaveMigrator, lrg_save_migrator, LRG, SAVE_MIGRATOR, GObject)

/**
 * LrgSaveMigrator:
 *
 * Holds the conversion steps of one kind of saved data and the current
 * versions: the format version (#LrgSaveMigrator:current-format) and
 * every known section's version (lrg_save_migrator_set_section_version()).
 *
 * lrg_save_migrator_migrate_document() brings a document to the current
 * versions: first the document steps, from the stored format version to
 * the current one, in order; then, for every section the migrator knows
 * (in name order), the section steps from its stored version to its
 * current one. Sections the migrator does not know (a mod that is not
 * installed) are kept unchanged. The conversion works on a copy and
 * changes the caller's document only when every step succeeded; a
 * missing step (%LRG_SAVE_ERROR_MISSING_STEP), a failing step
 * (%LRG_SAVE_ERROR_STEP_FAILED, the message names it), a stored version
 * newer than the current one (%LRG_SAVE_ERROR_VERSION_MISMATCH: data of
 * a later release is never downgraded) or an unversioned known section
 * (%LRG_SAVE_ERROR_CORRUPT) leaves it exactly as it was.
 *
 * Signals: #LrgSaveMigrator::step-applied, once for every step of a
 * successful conversion, in order, after the result is committed.
 */

/**
 * lrg_save_migrator_new:
 *
 * Returns: (transfer full): a migrator with current format 1 and no
 *   steps
 */
LRG_AVAILABLE_IN_ALL
LrgSaveMigrator *
lrg_save_migrator_new (void);

/**
 * lrg_save_migrator_get_current_format:
 * @self: an #LrgSaveMigrator
 *
 * Returns: the format version documents are converted to
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_save_migrator_get_current_format (LrgSaveMigrator *self);

/**
 * lrg_save_migrator_set_current_format:
 * @self: an #LrgSaveMigrator
 * @format_version: the current format version (at least 1)
 */
LRG_AVAILABLE_IN_ALL
void
lrg_save_migrator_set_current_format (LrgSaveMigrator *self,
                                      guint            format_version);

/**
 * lrg_save_migrator_set_section_version:
 * @self: an #LrgSaveMigrator
 * @section: a section name
 * @version: its current version (at least 1)
 *
 * Declares a known section and the version it is converted to.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_save_migrator_set_section_version (LrgSaveMigrator *self,
                                       const gchar     *section,
                                       guint            version);

/**
 * lrg_save_migrator_get_section_version:
 * @self: an #LrgSaveMigrator
 * @section: a section name
 *
 * Returns: the section's current version, 0 for an unknown section
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_save_migrator_get_section_version (LrgSaveMigrator *self,
                                       const gchar     *section);

/**
 * lrg_save_migrator_forget_section:
 * @self: an #LrgSaveMigrator
 * @section: a section name
 *
 * The section becomes unknown (documents keep it unchanged) and its
 * steps are removed: what a mod that registered it does when unloaded.
 *
 * Returns: %TRUE when the section was known or had steps
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_save_migrator_forget_section (LrgSaveMigrator *self,
                                  const gchar     *section);

/**
 * lrg_save_migrator_get_sections:
 * @self: an #LrgSaveMigrator
 *
 * Returns: (transfer full) (array zero-terminated=1): the known
 *   sections, in byte order
 */
LRG_AVAILABLE_IN_ALL
gchar **
lrg_save_migrator_get_sections (LrgSaveMigrator *self);

/**
 * lrg_save_migrator_add:
 * @self: an #LrgSaveMigrator
 * @migration: a step (a reference is taken)
 * @error: return location for a #GError
 *
 * Adds a step. Every step converts exactly one version, so two steps
 * overlap exactly when they have the same scope, section and from
 * version.
 *
 * Returns: %TRUE on success; %LRG_SAVE_ERROR_DUPLICATE_STEP when the
 *   migrator already has that step (the new one is not added)
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_save_migrator_add (LrgSaveMigrator   *self,
                       LrgSaveMigration  *migration,
                       GError           **error);

/**
 * lrg_save_migrator_add_document_step:
 * @self: an #LrgSaveMigrator
 * @from_version: the format version the step reads
 * @description: (nullable): what it changes
 * @func: (scope notified) (closure user_data) (destroy destroy): the conversion
 * @user_data: data for @func
 * @destroy: (nullable): frees @user_data with the step (also when it is
 *   refused)
 * @error: return location for a #GError
 *
 * lrg_save_migration_new_document() and lrg_save_migrator_add() in one.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_save_migrator_add_document_step (LrgSaveMigrator         *self,
                                     guint                    from_version,
                                     const gchar             *description,
                                     LrgSaveDocumentStepFunc  func,
                                     gpointer                 user_data,
                                     GDestroyNotify           destroy,
                                     GError                 **error);

/**
 * lrg_save_migrator_add_section_step:
 * @self: an #LrgSaveMigrator
 * @section: the section's name
 * @from_version: the version the step reads
 * @description: (nullable): what it changes
 * @func: (scope notified) (closure user_data) (destroy destroy): the conversion
 * @user_data: data for @func
 * @destroy: (nullable): frees @user_data with the step (also when it is
 *   refused)
 * @error: return location for a #GError
 *
 * lrg_save_migration_new_section() and lrg_save_migrator_add() in one.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_save_migrator_add_section_step (LrgSaveMigrator         *self,
                                    const gchar             *section,
                                    guint                    from_version,
                                    const gchar             *description,
                                    LrgSaveSectionStepFunc   func,
                                    gpointer                 user_data,
                                    GDestroyNotify           destroy,
                                    GError                 **error);

/**
 * lrg_save_migrator_remove:
 * @self: an #LrgSaveMigrator
 * @scope: the step's scope
 * @section: (nullable): its section (ignored for document steps)
 * @from_version: its from version
 *
 * Returns: %TRUE when the step was there
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_save_migrator_remove (LrgSaveMigrator       *self,
                          LrgSaveMigrationScope  scope,
                          const gchar           *section,
                          guint                  from_version);

/**
 * lrg_save_migrator_lookup:
 * @self: an #LrgSaveMigrator
 * @scope: the step's scope
 * @section: (nullable): its section (ignored for document steps)
 * @from_version: its from version
 *
 * Returns: (transfer none) (nullable): the step
 */
LRG_AVAILABLE_IN_ALL
LrgSaveMigration *
lrg_save_migrator_lookup (LrgSaveMigrator       *self,
                          LrgSaveMigrationScope  scope,
                          const gchar           *section,
                          guint                  from_version);

/**
 * lrg_save_migrator_get_n_steps:
 * @self: an #LrgSaveMigrator
 *
 * Returns: the number of steps of every scope
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_save_migrator_get_n_steps (LrgSaveMigrator *self);

/**
 * lrg_save_migrator_plan:
 * @self: an #LrgSaveMigrator
 * @scope: document or section steps
 * @section: (nullable): the section, for section steps
 * @from_version: the stored version
 * @to_version: the version to reach
 * @error: return location for a #GError
 *
 * The ordered chain from @from_version to @to_version; empty when they
 * are equal.
 *
 * Returns: (transfer full) (element-type LrgSaveMigration) (nullable):
 *   the steps, or %NULL with %LRG_SAVE_ERROR_VERSION_MISMATCH (a
 *   downgrade), %LRG_SAVE_ERROR_CORRUPT (version 0) or
 *   %LRG_SAVE_ERROR_MISSING_STEP (naming the first missing step)
 */
LRG_AVAILABLE_IN_ALL
GPtrArray *
lrg_save_migrator_plan (LrgSaveMigrator        *self,
                        LrgSaveMigrationScope   scope,
                        const gchar            *section,
                        guint                   from_version,
                        guint                   to_version,
                        GError                **error);

/**
 * lrg_save_migrator_migrate_section:
 * @self: an #LrgSaveMigrator
 * @section: the section's name
 * @state: its state at @from_version
 * @from_version: the stored version
 * @to_version: the version to reach
 * @error: return location for a #GError
 *
 * Applies the chain of @section's steps to @state (@state itself is
 * immutable and never changes).
 *
 * Returns: (transfer full) (nullable): the converted state (@state
 *   itself, referenced, for an empty chain), or %NULL with @error set as
 *   for lrg_save_migrator_plan() or %LRG_SAVE_ERROR_STEP_FAILED
 */
LRG_AVAILABLE_IN_ALL
GVariant *
lrg_save_migrator_migrate_section (LrgSaveMigrator  *self,
                                   const gchar      *section,
                                   GVariant         *state,
                                   guint             from_version,
                                   guint             to_version,
                                   GError          **error);

/**
 * lrg_save_migrator_migrate_document:
 * @self: an #LrgSaveMigrator
 * @document: the document, converted in place on success
 * @applied: (out) (optional) (transfer full) (element-type LrgSaveMigration):
 *   the steps applied, in order (empty when nothing needed converting)
 * @error: return location for a #GError
 *
 * Brings @document to the current format and section versions; see
 * #LrgSaveMigrator. Atomic: on failure @document is unchanged and
 * *@applied is %NULL.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_save_migrator_migrate_document (LrgSaveMigrator  *self,
                                    LrgSaveDocument  *document,
                                    GPtrArray       **applied,
                                    GError          **error);

/**
 * lrg_save_migrator_needs_migration:
 * @self: an #LrgSaveMigrator
 * @document: a document
 *
 * Returns: %TRUE when the document's format or a known section's
 *   version differs from the current one
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_save_migrator_needs_migration (LrgSaveMigrator *self,
                                   LrgSaveDocument *document);

G_END_DECLS
