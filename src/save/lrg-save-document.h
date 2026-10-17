/* lrg-save-document.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgSaveDocument - a save (or a server's stored state) as data: a
 * format version and named sections, each a #GVariant state with its
 * own version. It is what #LrgSaveMigrator converts: a game reads its
 * file into a document, the migrator brings the document to the
 * current versions, and the game restores from the result. Nothing in
 * it touches files or a running game.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-types.h"
#include "../lrg-enums.h"

G_BEGIN_DECLS

/**
 * LRG_SAVE_DOCUMENT_VARIANT_TYPE:
 *
 * The #GVariant type string of lrg_save_document_to_variant(): the
 * format version, the sections by name and their versions by name.
 */
#define LRG_SAVE_DOCUMENT_VARIANT_TYPE "(ua{sv}a{su})"

/**
 * LrgSaveDocument:
 *
 * A reference-counted, mutable save document. A section version of 0
 * means "unversioned" (a file written before its format recorded
 * section versions); #LrgSaveMigrator refuses to convert an unversioned
 * section, so a document step of the format that introduced section
 * versions has to set them.
 *
 * Section states are immutable #GVariant values; replacing a state
 * never changes a state another document (a copy) holds.
 */

#define LRG_TYPE_SAVE_DOCUMENT (lrg_save_document_get_type ())

LRG_AVAILABLE_IN_ALL
GType lrg_save_document_get_type (void) G_GNUC_CONST;

/**
 * lrg_save_document_new:
 * @format_version: the document's format version (at least 1)
 *
 * Creates an empty document.
 *
 * Returns: (transfer full): a new #LrgSaveDocument
 */
LRG_AVAILABLE_IN_ALL
LrgSaveDocument *
lrg_save_document_new (guint format_version);

/**
 * lrg_save_document_new_from_variant:
 * @variant: a #GVariant of type %LRG_SAVE_DOCUMENT_VARIANT_TYPE
 * @error: return location for a #GError
 *
 * Reads a document lrg_save_document_to_variant() wrote (a server's
 * stored state, a test fixture). A version for a section the variant
 * does not hold, an empty section name or a format version of 0 is
 * %LRG_SAVE_ERROR_CORRUPT.
 *
 * Returns: (transfer full) (nullable): the document, or %NULL with
 *   @error set
 */
LRG_AVAILABLE_IN_ALL
LrgSaveDocument *
lrg_save_document_new_from_variant (GVariant  *variant,
                                    GError   **error);

/**
 * lrg_save_document_ref:
 * @self: an #LrgSaveDocument
 *
 * Returns: (transfer full): @self
 */
LRG_AVAILABLE_IN_ALL
LrgSaveDocument *
lrg_save_document_ref (LrgSaveDocument *self);

/**
 * lrg_save_document_unref:
 * @self: (transfer full): an #LrgSaveDocument
 *
 * Drops a reference; the last one frees the document.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_save_document_unref (LrgSaveDocument *self);

/**
 * lrg_save_document_copy:
 * @self: an #LrgSaveDocument
 *
 * A new document with the same format version, sections and versions;
 * changing either afterwards does not change the other.
 *
 * Returns: (transfer full): the copy
 */
LRG_AVAILABLE_IN_ALL
LrgSaveDocument *
lrg_save_document_copy (LrgSaveDocument *self);

/**
 * lrg_save_document_get_format_version:
 * @self: an #LrgSaveDocument
 *
 * Returns: the format version
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_save_document_get_format_version (LrgSaveDocument *self);

/**
 * lrg_save_document_set_format_version:
 * @self: an #LrgSaveDocument
 * @format_version: the format version (at least 1)
 */
LRG_AVAILABLE_IN_ALL
void
lrg_save_document_set_format_version (LrgSaveDocument *self,
                                      guint            format_version);

/**
 * lrg_save_document_has_section:
 * @self: an #LrgSaveDocument
 * @name: a section name
 *
 * Returns: %TRUE when the document holds the section
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_save_document_has_section (LrgSaveDocument *self,
                               const gchar     *name);

/**
 * lrg_save_document_get_section:
 * @self: an #LrgSaveDocument
 * @name: a section name
 *
 * Returns: (transfer none) (nullable): the section's state, %NULL when
 *   the document does not hold it
 */
LRG_AVAILABLE_IN_ALL
GVariant *
lrg_save_document_get_section (LrgSaveDocument *self,
                               const gchar     *name);

/**
 * lrg_save_document_get_section_version:
 * @self: an #LrgSaveDocument
 * @name: a section name
 *
 * Returns: the section's version; 0 when it is unversioned or absent
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_save_document_get_section_version (LrgSaveDocument *self,
                                       const gchar     *name);

/**
 * lrg_save_document_set_section:
 * @self: an #LrgSaveDocument
 * @name: a non-empty section name
 * @state: the section's state (a floating reference is sunk)
 * @version: its version, 0 for unversioned
 *
 * Adds the section or replaces its state and version.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_save_document_set_section (LrgSaveDocument *self,
                               const gchar     *name,
                               GVariant        *state,
                               guint            version);

/**
 * lrg_save_document_set_section_version:
 * @self: an #LrgSaveDocument
 * @name: a section name
 * @version: its version, 0 for unversioned
 *
 * Returns: %FALSE when the document does not hold the section
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_save_document_set_section_version (LrgSaveDocument *self,
                                       const gchar     *name,
                                       guint            version);

/**
 * lrg_save_document_remove_section:
 * @self: an #LrgSaveDocument
 * @name: a section name
 *
 * Returns: %TRUE when the section was there
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_save_document_remove_section (LrgSaveDocument *self,
                                  const gchar     *name);

/**
 * lrg_save_document_rename_section:
 * @self: an #LrgSaveDocument
 * @old_name: the section's name
 * @new_name: its new, non-empty name
 * @error: return location for a #GError
 *
 * Renames a section, keeping its state and version.
 *
 * Returns: %TRUE on success; %LRG_SAVE_ERROR_NOT_FOUND without
 *   @old_name, %LRG_SAVE_ERROR_FAILED when @new_name is taken
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_save_document_rename_section (LrgSaveDocument  *self,
                                  const gchar      *old_name,
                                  const gchar      *new_name,
                                  GError          **error);

/**
 * lrg_save_document_get_n_sections:
 * @self: an #LrgSaveDocument
 *
 * Returns: the number of sections
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_save_document_get_n_sections (LrgSaveDocument *self);

/**
 * lrg_save_document_get_section_names:
 * @self: an #LrgSaveDocument
 *
 * Returns: (transfer full) (array zero-terminated=1): the section names
 *   in byte order
 */
LRG_AVAILABLE_IN_ALL
gchar **
lrg_save_document_get_section_names (LrgSaveDocument *self);

/**
 * lrg_save_document_get_versions:
 * @self: an #LrgSaveDocument
 *
 * Returns: (transfer full): an `a{su}` of every section's version
 *   (unversioned sections included, as 0), in name order
 */
LRG_AVAILABLE_IN_ALL
GVariant *
lrg_save_document_get_versions (LrgSaveDocument *self);

/**
 * lrg_save_document_to_variant:
 * @self: an #LrgSaveDocument
 *
 * Returns: (transfer full): the document as a
 *   %LRG_SAVE_DOCUMENT_VARIANT_TYPE value, sections in name order
 */
LRG_AVAILABLE_IN_ALL
GVariant *
lrg_save_document_to_variant (LrgSaveDocument *self);

/**
 * lrg_save_document_equal:
 * @a: an #LrgSaveDocument
 * @b: an #LrgSaveDocument
 *
 * Returns: %TRUE when both hold the same format version, the same
 *   sections with equal states and the same versions
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_save_document_equal (LrgSaveDocument *a,
                         LrgSaveDocument *b);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (LrgSaveDocument, lrg_save_document_unref)

G_END_DECLS
