/* SPDX-License-Identifier: AGPL-3.0-or-later */
#pragma once
#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif
#include <gio/gio.h>
#include "../lrg-version.h"
G_BEGIN_DECLS
/**
 * lrg_mmo_content_verify:
 * @manifest: canonical little-endian (tsa(stay)) version, release, files(path,size,SHA256)
 * @signature: 64-byte Ed25519 signature over the exact manifest bytes
 * @public_key: trusted 32-byte raw Ed25519 public key, provisioned independently
 * @error: (nullable): return location for error
 *
 * Verifies signature and bounded version-one manifest, rejecting duplicate or
 * unsafe paths. Maximum 4096 files, 1 GiB each, 4 GiB total, 1 MiB manifest.
 * A valid signature does not prevent rollback: the host must pin allowed release IDs.
 * Returns: (transfer full) (nullable): verified manifest
 */
LRG_AVAILABLE_IN_ALL
GVariant *lrg_mmo_content_verify (GBytes *manifest, GBytes *signature, GBytes *public_key, GError **error);
/**
 * lrg_mmo_content_verify_directory:
 * @manifest: validated output of lrg_mmo_content_verify()
 * @directory: private staging directory, not writable by untrusted concurrent processes
 * @error: (nullable): return location for error
 *
 * Streams each listed regular file through SHA256 and checks exact size. Rejects
 * symlinks in listed paths. Only listed files are authenticated: hosts must load
 * only manifest entries and publish staging directories atomically after success.
 * Returns: whether all listed files match
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_mmo_content_verify_directory (GVariant *manifest, const gchar *directory, GError **error);
G_END_DECLS
