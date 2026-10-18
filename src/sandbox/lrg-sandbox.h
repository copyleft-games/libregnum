/* lrg-sandbox.h - In-process sandbox: Landlock, seccomp, no_new_privs
 *
 * Copyright 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * This file is part of Libregnum.
 */

#ifndef LRG_SANDBOX_H
#define LRG_SANDBOX_H

#include <glib-object.h>
#include "../lrg-version.h"

G_BEGIN_DECLS

/**
 * LrgSandboxAccess:
 * @LRG_SANDBOX_ACCESS_NONE: no access
 * @LRG_SANDBOX_ACCESS_READ: read files and list directories beneath the path
 * @LRG_SANDBOX_ACCESS_WRITE: write, create, truncate, rename and remove
 *   regular files and directories beneath the path (no symlinks, sockets,
 *   FIFOs or device nodes; does not imply %LRG_SANDBOX_ACCESS_READ)
 * @LRG_SANDBOX_ACCESS_DEVICE: open device nodes beneath the path for
 *   reading and writing and use their ioctls (GPU, sound, input)
 * @LRG_SANDBOX_ACCESS_CONNECT: connect to pathname UNIX sockets beneath
 *   the path (display server, sound server, session bus)
 * @LRG_SANDBOX_ACCESS_EXECUTE: execute files beneath the path (only
 *   meaningful when the sandbox allows execve at all)
 *
 * What a path rule grants.  Everything not granted by a rule is denied
 * once the sandbox is applied (on kernels with Landlock).
 */
typedef enum /*< flags >*/
{
    LRG_SANDBOX_ACCESS_NONE    = 0,
    LRG_SANDBOX_ACCESS_READ    = 1 << 0,
    LRG_SANDBOX_ACCESS_WRITE   = 1 << 1,
    LRG_SANDBOX_ACCESS_DEVICE  = 1 << 2,
    LRG_SANDBOX_ACCESS_CONNECT = 1 << 3,
    LRG_SANDBOX_ACCESS_EXECUTE = 1 << 4
} LrgSandboxAccess;

LRG_AVAILABLE_IN_ALL
GType lrg_sandbox_access_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_SANDBOX_ACCESS (lrg_sandbox_access_get_type ())

/**
 * LrgSandboxNetwork:
 * @LRG_SANDBOX_NETWORK_NONE: no IP sockets at all (UNIX and netlink only)
 * @LRG_SANDBOX_NETWORK_CLIENT: IP sockets; TCP connects only to the
 *   allowed ports (any port when none was allowed) and no TCP bind
 * @LRG_SANDBOX_NETWORK_SERVER: IP sockets; TCP binds only to the allowed
 *   ports and no outgoing TCP connects (accepted connections work)
 * @LRG_SANDBOX_NETWORK_FULL: the network is not restricted
 *
 * The network policy.  The IP socket check is done by seccomp and works
 * on every kernel; the TCP port rules need Landlock ABI 4 (Linux 6.7),
 * the UDP port rules ABI 10.
 */
typedef enum
{
    LRG_SANDBOX_NETWORK_NONE,
    LRG_SANDBOX_NETWORK_CLIENT,
    LRG_SANDBOX_NETWORK_SERVER,
    LRG_SANDBOX_NETWORK_FULL
} LrgSandboxNetwork;

LRG_AVAILABLE_IN_ALL
GType lrg_sandbox_network_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_SANDBOX_NETWORK (lrg_sandbox_network_get_type ())

/**
 * LrgSandboxError:
 * @LRG_SANDBOX_ERROR_UNSUPPORTED: the kernel or platform lacks a mechanism
 *   the policy requires (Landlock with #LrgSandbox:require-landlock,
 *   seccomp, or a non-Linux platform)
 * @LRG_SANDBOX_ERROR_FAILED: a kernel call failed while applying
 * @LRG_SANDBOX_ERROR_ALREADY_APPLIED: the sandbox was applied before
 * @LRG_SANDBOX_ERROR_INVALID_PATH: a rule names a relative path
 *
 * Error codes in the %LRG_SANDBOX_ERROR domain.
 */
typedef enum
{
    LRG_SANDBOX_ERROR_UNSUPPORTED,
    LRG_SANDBOX_ERROR_FAILED,
    LRG_SANDBOX_ERROR_ALREADY_APPLIED,
    LRG_SANDBOX_ERROR_INVALID_PATH
} LrgSandboxError;

#define LRG_SANDBOX_ERROR (lrg_sandbox_error_quark ())

LRG_AVAILABLE_IN_ALL
GQuark lrg_sandbox_error_quark (void);

#define LRG_TYPE_SANDBOX (lrg_sandbox_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgSandbox, lrg_sandbox, LRG, SANDBOX, GObject)

/**
 * lrg_sandbox_new:
 *
 * Creates an empty sandbox policy: no path is reachable, the network is
 * %LRG_SANDBOX_NETWORK_NONE, execve is denied and Landlock is required.
 * Add rules, then call lrg_sandbox_apply() once.
 *
 * Returns: (transfer full): a new #LrgSandbox
 */
LRG_AVAILABLE_IN_ALL
LrgSandbox *
lrg_sandbox_new (void);

/**
 * lrg_sandbox_get_landlock_abi:
 *
 * Asks the running kernel for its Landlock ABI version.
 *
 * Returns: the ABI version (1 or more), or 0 when Landlock is not built
 *   in, not enabled (the `lsm=` boot list) or the platform is not Linux
 */
LRG_AVAILABLE_IN_ALL
gint
lrg_sandbox_get_landlock_abi (void);

/**
 * lrg_sandbox_allow_path:
 * @self: an #LrgSandbox
 * @path: an absolute path to a directory or file
 * @access: what the rule grants beneath @path
 *
 * Grants @access on @path and everything beneath it.  Rules for the same
 * path add up.  A path that does not exist when the sandbox is applied is
 * skipped (nothing beneath it can be created later either), so a caller
 * creates its writable directories first.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_sandbox_allow_path (LrgSandbox       *self,
                        const gchar      *path,
                        LrgSandboxAccess  access);

/**
 * lrg_sandbox_get_path_access:
 * @self: an #LrgSandbox
 * @path: a path given to lrg_sandbox_allow_path()
 *
 * Returns: the access granted on exactly @path (not inherited from a
 *   parent), %LRG_SANDBOX_ACCESS_NONE when there is no such rule
 */
LRG_AVAILABLE_IN_ALL
LrgSandboxAccess
lrg_sandbox_get_path_access (LrgSandbox  *self,
                             const gchar *path);

/**
 * lrg_sandbox_list_paths:
 * @self: an #LrgSandbox
 *
 * Returns: (transfer full) (array zero-terminated=1): the paths with a
 *   rule, in the order they were first added
 */
LRG_AVAILABLE_IN_ALL
gchar **
lrg_sandbox_list_paths (LrgSandbox *self);

/**
 * lrg_sandbox_set_network:
 * @self: an #LrgSandbox
 * @network: the network policy
 */
LRG_AVAILABLE_IN_ALL
void
lrg_sandbox_set_network (LrgSandbox        *self,
                         LrgSandboxNetwork  network);

/**
 * lrg_sandbox_get_network:
 * @self: an #LrgSandbox
 *
 * Returns: the network policy
 */
LRG_AVAILABLE_IN_ALL
LrgSandboxNetwork
lrg_sandbox_get_network (LrgSandbox *self);

/**
 * lrg_sandbox_allow_port:
 * @self: an #LrgSandbox
 * @port: a TCP/UDP port in host order
 *
 * Allows @port: the port a %LRG_SANDBOX_NETWORK_CLIENT may connect to, or
 * the port a %LRG_SANDBOX_NETWORK_SERVER may bind.  Ignored by the other
 * network policies.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_sandbox_allow_port (LrgSandbox *self,
                        guint16     port);

/**
 * lrg_sandbox_set_allow_exec:
 * @self: an #LrgSandbox
 * @allow: %TRUE to leave execve()/execveat() allowed
 *
 * By default the sandbox denies execve() and execveat() with %EPERM, so
 * nothing in the process can start another program.  Programs that must
 * run tools after the sandbox is applied allow it here; only files under
 * a %LRG_SANDBOX_ACCESS_EXECUTE rule can then be executed.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_sandbox_set_allow_exec (LrgSandbox *self,
                            gboolean    allow);

/**
 * lrg_sandbox_get_allow_exec:
 * @self: an #LrgSandbox
 *
 * Returns: whether execve() stays allowed
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_sandbox_get_allow_exec (LrgSandbox *self);

/**
 * lrg_sandbox_set_require_landlock:
 * @self: an #LrgSandbox
 * @require: %TRUE (the default) to fail when the kernel has no Landlock
 *
 * Without Landlock the filesystem and port rules cannot be enforced;
 * lrg_sandbox_apply() then fails with %LRG_SANDBOX_ERROR_UNSUPPORTED
 * unless this is %FALSE, in which case only no_new_privs and the seccomp
 * filter are applied.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_sandbox_set_require_landlock (LrgSandbox *self,
                                  gboolean    require);

/**
 * lrg_sandbox_get_require_landlock:
 * @self: an #LrgSandbox
 *
 * Returns: whether Landlock is required
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_sandbox_get_require_landlock (LrgSandbox *self);

/**
 * lrg_sandbox_apply:
 * @self: an #LrgSandbox
 * @error: (nullable): return location for an error
 *
 * Applies the policy to the whole process, irreversibly: sets
 * no_new_privs, restricts the filesystem and TCP/UDP ports with Landlock
 * (every access right the kernel's ABI knows), scopes abstract UNIX
 * sockets and signals to the sandbox (ABI 6), then installs a seccomp
 * filter on every thread that denies execve/execveat (unless allowed),
 * ptrace, process_vm_readv/writev, mount and the new mount API, chroot,
 * pivot_root, unshare, setns, namespace flags on clone, kernel module and
 * kexec loading, bpf, perf_event_open, userfaultfd, keyrings, reboot,
 * swap, clock setting, TIOCSTI/TIOCLINUX, and IP sockets when the
 * network is %LRG_SANDBOX_NETWORK_NONE.  Denied calls fail with %EPERM
 * (%EACCES for sockets); a system call of another architecture kills the
 * process.
 *
 * Landlock is applied to every thread at once when the kernel supports
 * %LANDLOCK_RESTRICT_SELF_TSYNC; on older kernels only the calling thread
 * and the threads it creates later are restricted, which
 * lrg_sandbox_get_threads_synced() reports.
 *
 * Returns: %TRUE when the sandbox is in place
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_sandbox_apply (LrgSandbox  *self,
                   GError     **error);

/**
 * lrg_sandbox_is_applied:
 * @self: an #LrgSandbox
 *
 * Returns: whether lrg_sandbox_apply() succeeded
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_sandbox_is_applied (LrgSandbox *self);

/**
 * lrg_sandbox_get_applied_abi:
 * @self: an #LrgSandbox
 *
 * Returns: the Landlock ABI the applied ruleset used, 0 when Landlock was
 *   not used (or the sandbox is not applied)
 */
LRG_AVAILABLE_IN_ALL
gint
lrg_sandbox_get_applied_abi (LrgSandbox *self);

/**
 * lrg_sandbox_get_threads_synced:
 * @self: an #LrgSandbox
 *
 * Returns: %TRUE when the Landlock domain covers every thread that
 *   existed when the sandbox was applied (TSYNC, or the caller was the
 *   only thread)
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_sandbox_get_threads_synced (LrgSandbox *self);

/**
 * lrg_sandbox_describe:
 * @self: an #LrgSandbox
 *
 * Returns: (transfer full): a multi-line, human readable summary of the
 *   policy and, once applied, what the kernel enforced
 */
LRG_AVAILABLE_IN_ALL
gchar *
lrg_sandbox_describe (LrgSandbox *self);

/**
 * lrg_sandbox_build_bwrap_argv:
 * @self: an #LrgSandbox
 * @bwrap: (nullable): the bubblewrap program (%NULL: `bwrap` from `PATH`)
 * @argv: (array zero-terminated=1): the command to run inside
 *
 * Turns the policy into a bubblewrap command line that runs @argv as an
 * outer sandbox layer: the root filesystem read-only, a fresh `/proc`
 * and `/tmp`, the device paths bound read-write, the writable paths bound
 * read-write, every namespace unshared (the network one only when the
 * network is %LRG_SANDBOX_NETWORK_NONE), `--new-session`,
 * `--die-with-parent` and the capabilities dropped.  Paths that do not
 * exist are left out.  The command still applies the in-process sandbox
 * itself: bubblewrap adds namespaces, it does not replace Landlock.
 *
 * Returns: (transfer full) (array zero-terminated=1): the command line
 */
LRG_AVAILABLE_IN_ALL
gchar **
lrg_sandbox_build_bwrap_argv (LrgSandbox         *self,
                              const gchar        *bwrap,
                              const gchar * const *argv);

G_END_DECLS

#endif /* LRG_SANDBOX_H */
