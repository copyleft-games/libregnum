/* lrg-sandbox.c - In-process sandbox: Landlock, seccomp, no_new_privs
 *
 * Copyright 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * This file is part of Libregnum.
 *
 * The sandbox talks to the kernel directly (raw system calls, a seccomp
 * filter assembled here) so it needs neither libseccomp nor new kernel
 * headers: the Landlock ABI structures and constants are copied below
 * from the stable kernel UAPI, and every right is masked by the ABI the
 * running kernel reports, so one binary built on an old distribution
 * uses whatever the kernel it runs on offers.
 */

#include "config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_SANDBOX
#include "../lrg-log.h"

#include "lrg-sandbox.h"

#include <errno.h>
#include <string.h>

#ifdef __linux__
#include <fcntl.h>
#include <stddef.h>
#include <unistd.h>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/ioctl.h>
#include <linux/audit.h>
#include <linux/filter.h>
#include <linux/seccomp.h>
#include <sched.h>
#endif

/**
 * SECTION:lrg-sandbox
 * @title: LrgSandbox
 * @short_description: Confine the running process (Landlock + seccomp)
 *
 * #LrgSandbox collects a policy (paths and what may be done beneath
 * them, a network mode, whether programs may be executed) and applies it
 * to the calling process with Linux mechanisms that need no daemon and no
 * privileges: `PR_SET_NO_NEW_PRIVS`, a Landlock ruleset for the
 * filesystem and TCP/UDP ports, and a seccomp filter for the system calls
 * a confined game never needs (execve, ptrace, mount, namespaces, ...).
 *
 * |[<!-- language="C" -->
 * g_autoptr(LrgSandbox) sandbox = lrg_sandbox_new ();
 *
 * lrg_sandbox_allow_path (sandbox, "/usr", LRG_SANDBOX_ACCESS_READ);
 * lrg_sandbox_allow_path (sandbox, saves_dir,
 *                         LRG_SANDBOX_ACCESS_READ | LRG_SANDBOX_ACCESS_WRITE);
 * if (!lrg_sandbox_apply (sandbox, &error))
 *     g_printerr ("sandbox: %s\n", error->message);
 * ]|
 */

/* ==========================================================================
 * Landlock UAPI (include/uapi/linux/landlock.h), copied so the code builds
 * against kernel headers that predate Landlock.  The ABI is stable.
 * ========================================================================== */

#define LRG_LL_CREATE_RULESET_VERSION  (1U << 0)
#define LRG_LL_RESTRICT_SELF_TSYNC     (1U << 3)
#define LRG_LL_RULE_PATH_BENEATH       (1)
#define LRG_LL_RULE_NET_PORT           (2)

#define LRG_LL_FS_EXECUTE       (1ULL << 0)
#define LRG_LL_FS_WRITE_FILE    (1ULL << 1)
#define LRG_LL_FS_READ_FILE     (1ULL << 2)
#define LRG_LL_FS_READ_DIR      (1ULL << 3)
#define LRG_LL_FS_REMOVE_DIR    (1ULL << 4)
#define LRG_LL_FS_REMOVE_FILE   (1ULL << 5)
#define LRG_LL_FS_MAKE_CHAR     (1ULL << 6)
#define LRG_LL_FS_MAKE_DIR      (1ULL << 7)
#define LRG_LL_FS_MAKE_REG      (1ULL << 8)
#define LRG_LL_FS_MAKE_SOCK     (1ULL << 9)
#define LRG_LL_FS_MAKE_FIFO     (1ULL << 10)
#define LRG_LL_FS_MAKE_BLOCK    (1ULL << 11)
#define LRG_LL_FS_MAKE_SYM      (1ULL << 12)
#define LRG_LL_FS_REFER         (1ULL << 13)  /* ABI 2 */
#define LRG_LL_FS_TRUNCATE      (1ULL << 14)  /* ABI 3 */
#define LRG_LL_FS_IOCTL_DEV     (1ULL << 15)  /* ABI 5 */
#define LRG_LL_FS_RESOLVE_UNIX  (1ULL << 16)  /* ABI 9 */

#define LRG_LL_NET_BIND_TCP          (1ULL << 0)  /* ABI 4 */
#define LRG_LL_NET_CONNECT_TCP       (1ULL << 1)  /* ABI 4 */
#define LRG_LL_NET_BIND_UDP          (1ULL << 2)  /* ABI 10 */
#define LRG_LL_NET_CONNECT_SEND_UDP  (1ULL << 3)  /* ABI 10 */

#define LRG_LL_SCOPE_ABSTRACT_UNIX   (1ULL << 0)  /* ABI 6 */
#define LRG_LL_SCOPE_SIGNAL          (1ULL << 1)  /* ABI 6 */

/* the rights that only make sense on files (a rule on a non-directory
 * may only carry these, or landlock_add_rule() fails with EINVAL) */
#define LRG_LL_FS_FILE_RIGHTS (LRG_LL_FS_EXECUTE | LRG_LL_FS_WRITE_FILE | \
                               LRG_LL_FS_READ_FILE | LRG_LL_FS_TRUNCATE | \
                               LRG_LL_FS_IOCTL_DEV)

/* struct landlock_ruleset_attr up to `scoped` (ABI 6); later fields
 * (the ABI 10 quiet masks) stay unused and are not passed */
typedef struct
{
    guint64 handled_access_fs;
    guint64 handled_access_net;
    guint64 scoped;
} LrgLlRulesetAttr;

typedef struct
{
    guint64 allowed_access;
    gint32  parent_fd;
} __attribute__((packed)) LrgLlPathBeneathAttr;

typedef struct
{
    guint64 allowed_access;
    guint64 port;
} LrgLlNetPortAttr;

/* the Landlock system call numbers are the same on every architecture */
#ifndef __NR_landlock_create_ruleset
#define __NR_landlock_create_ruleset 444
#endif
#ifndef __NR_landlock_add_rule
#define __NR_landlock_add_rule 445
#endif
#ifndef __NR_landlock_restrict_self
#define __NR_landlock_restrict_self 446
#endif

/* ==========================================================================
 * Types
 * ========================================================================== */

struct _LrgSandbox
{
    GObject             parent_instance;

    GPtrArray          *paths;     /* gchar *, insertion order */
    GHashTable         *access;    /* path -> GUINT_TO_POINTER (LrgSandboxAccess) */
    GArray             *ports;     /* guint16 */
    LrgSandboxNetwork   network;
    gboolean            allow_exec;
    gboolean            require_landlock;

    /* filled by lrg_sandbox_apply() */
    gboolean            applied;
    gint                applied_abi;
    gboolean            threads_synced;
    gboolean            seccomp_applied;
    guint               rules_added;
    guint               rules_skipped;
};

G_DEFINE_TYPE (LrgSandbox, lrg_sandbox, G_TYPE_OBJECT)

G_DEFINE_QUARK (lrg-sandbox-error-quark, lrg_sandbox_error)

static const GFlagsValue sandbox_access_values[] = {
    { LRG_SANDBOX_ACCESS_NONE, "LRG_SANDBOX_ACCESS_NONE", "none" },
    { LRG_SANDBOX_ACCESS_READ, "LRG_SANDBOX_ACCESS_READ", "read" },
    { LRG_SANDBOX_ACCESS_WRITE, "LRG_SANDBOX_ACCESS_WRITE", "write" },
    { LRG_SANDBOX_ACCESS_DEVICE, "LRG_SANDBOX_ACCESS_DEVICE", "device" },
    { LRG_SANDBOX_ACCESS_CONNECT, "LRG_SANDBOX_ACCESS_CONNECT", "connect" },
    { LRG_SANDBOX_ACCESS_EXECUTE, "LRG_SANDBOX_ACCESS_EXECUTE", "execute" },
    { 0, NULL, NULL }
};

GType
lrg_sandbox_access_get_type (void)
{
    static gsize type_id = 0;

    if (g_once_init_enter (&type_id))
    {
        GType id;

        id = g_flags_register_static (g_intern_static_string ("LrgSandboxAccess"),
                                      sandbox_access_values);
        g_once_init_leave (&type_id, id);
    }
    return (GType)type_id;
}

static const GEnumValue sandbox_network_values[] = {
    { LRG_SANDBOX_NETWORK_NONE, "LRG_SANDBOX_NETWORK_NONE", "none" },
    { LRG_SANDBOX_NETWORK_CLIENT, "LRG_SANDBOX_NETWORK_CLIENT", "client" },
    { LRG_SANDBOX_NETWORK_SERVER, "LRG_SANDBOX_NETWORK_SERVER", "server" },
    { LRG_SANDBOX_NETWORK_FULL, "LRG_SANDBOX_NETWORK_FULL", "full" },
    { 0, NULL, NULL }
};

GType
lrg_sandbox_network_get_type (void)
{
    static gsize type_id = 0;

    if (g_once_init_enter (&type_id))
    {
        GType id;

        id = g_enum_register_static (g_intern_static_string ("LrgSandboxNetwork"),
                                     sandbox_network_values);
        g_once_init_leave (&type_id, id);
    }
    return (GType)type_id;
}

static void
lrg_sandbox_finalize (GObject *object)
{
    LrgSandbox *self = LRG_SANDBOX (object);

    g_clear_pointer (&self->paths, g_ptr_array_unref);
    g_clear_pointer (&self->access, g_hash_table_unref);
    g_clear_pointer (&self->ports, g_array_unref);

    G_OBJECT_CLASS (lrg_sandbox_parent_class)->finalize (object);
}

static void
lrg_sandbox_class_init (LrgSandboxClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = lrg_sandbox_finalize;
}

static void
lrg_sandbox_init (LrgSandbox *self)
{
    self->paths = g_ptr_array_new_with_free_func (g_free);
    self->access = g_hash_table_new (g_str_hash, g_str_equal);
    self->ports = g_array_new (FALSE, FALSE, sizeof (guint16));
    self->network = LRG_SANDBOX_NETWORK_NONE;
    self->allow_exec = FALSE;
    self->require_landlock = TRUE;
}

LrgSandbox *
lrg_sandbox_new (void)
{
    return (LrgSandbox *)g_object_new (LRG_TYPE_SANDBOX, NULL);
}

/* ==========================================================================
 * Policy
 * ========================================================================== */

void
lrg_sandbox_allow_path (LrgSandbox       *self,
                        const gchar      *path,
                        LrgSandboxAccess  access)
{
    gpointer key;
    gpointer old;
    gchar *copy;

    g_return_if_fail (LRG_IS_SANDBOX (self));
    g_return_if_fail (path != NULL);
    g_return_if_fail (!self->applied);

    /* the paths array owns the strings; the table borrows them, so a
     * second rule for a path keeps the stored key and ORs the access */
    if (g_hash_table_lookup_extended (self->access, path, &key, &old))
    {
        g_hash_table_insert (self->access, key,
                             GUINT_TO_POINTER (GPOINTER_TO_UINT (old) | (guint)access));
        return;
    }
    copy = g_strdup (path);
    g_ptr_array_add (self->paths, copy);
    g_hash_table_insert (self->access, copy, GUINT_TO_POINTER ((guint)access));
}

LrgSandboxAccess
lrg_sandbox_get_path_access (LrgSandbox  *self,
                             const gchar *path)
{
    g_return_val_if_fail (LRG_IS_SANDBOX (self), LRG_SANDBOX_ACCESS_NONE);
    g_return_val_if_fail (path != NULL, LRG_SANDBOX_ACCESS_NONE);

    return (LrgSandboxAccess)GPOINTER_TO_UINT (g_hash_table_lookup (self->access, path));
}

gchar **
lrg_sandbox_list_paths (LrgSandbox *self)
{
    GStrvBuilder *builder;
    gchar **result;
    guint i;

    g_return_val_if_fail (LRG_IS_SANDBOX (self), NULL);

    builder = g_strv_builder_new ();
    for (i = 0; i < self->paths->len; i++)
        g_strv_builder_add (builder, (const gchar *)g_ptr_array_index (self->paths, i));
    result = g_strv_builder_end (builder);
    g_strv_builder_unref (builder);
    return result;
}

void
lrg_sandbox_set_network (LrgSandbox        *self,
                         LrgSandboxNetwork  network)
{
    g_return_if_fail (LRG_IS_SANDBOX (self));
    g_return_if_fail (!self->applied);
    self->network = network;
}

LrgSandboxNetwork
lrg_sandbox_get_network (LrgSandbox *self)
{
    g_return_val_if_fail (LRG_IS_SANDBOX (self), LRG_SANDBOX_NETWORK_NONE);
    return self->network;
}

void
lrg_sandbox_allow_port (LrgSandbox *self,
                        guint16     port)
{
    guint i;

    g_return_if_fail (LRG_IS_SANDBOX (self));
    g_return_if_fail (!self->applied);

    for (i = 0; i < self->ports->len; i++)
        if (g_array_index (self->ports, guint16, i) == port)
            return;
    g_array_append_val (self->ports, port);
}

void
lrg_sandbox_set_allow_exec (LrgSandbox *self,
                            gboolean    allow)
{
    g_return_if_fail (LRG_IS_SANDBOX (self));
    g_return_if_fail (!self->applied);
    self->allow_exec = allow;
}

gboolean
lrg_sandbox_get_allow_exec (LrgSandbox *self)
{
    g_return_val_if_fail (LRG_IS_SANDBOX (self), FALSE);
    return self->allow_exec;
}

void
lrg_sandbox_set_require_landlock (LrgSandbox *self,
                                  gboolean    require)
{
    g_return_if_fail (LRG_IS_SANDBOX (self));
    g_return_if_fail (!self->applied);
    self->require_landlock = require;
}

gboolean
lrg_sandbox_get_require_landlock (LrgSandbox *self)
{
    g_return_val_if_fail (LRG_IS_SANDBOX (self), TRUE);
    return self->require_landlock;
}

gboolean
lrg_sandbox_is_applied (LrgSandbox *self)
{
    g_return_val_if_fail (LRG_IS_SANDBOX (self), FALSE);
    return self->applied;
}

gint
lrg_sandbox_get_applied_abi (LrgSandbox *self)
{
    g_return_val_if_fail (LRG_IS_SANDBOX (self), 0);
    return self->applied_abi;
}

gboolean
lrg_sandbox_get_threads_synced (LrgSandbox *self)
{
    g_return_val_if_fail (LRG_IS_SANDBOX (self), FALSE);
    return self->threads_synced;
}

/* ==========================================================================
 * Landlock
 * ========================================================================== */

gint
lrg_sandbox_get_landlock_abi (void)
{
#ifdef __linux__
    long abi;

    abi = syscall (__NR_landlock_create_ruleset, NULL, (size_t)0,
                   (guint32)LRG_LL_CREATE_RULESET_VERSION);
    return abi < 0 ? 0 : (gint)abi;
#else
    return 0;
#endif
}

#ifdef __linux__

/* every filesystem right the kernel's ABI knows */
static guint64
landlock_fs_rights_for_abi (gint abi)
{
    guint64 rights;

    rights = (LRG_LL_FS_MAKE_SYM << 1) - 1;  /* ABI 1: bits 0 .. 12 */
    if (abi >= 2)
        rights |= LRG_LL_FS_REFER;
    if (abi >= 3)
        rights |= LRG_LL_FS_TRUNCATE;
    if (abi >= 5)
        rights |= LRG_LL_FS_IOCTL_DEV;
    if (abi >= 9)
        rights |= LRG_LL_FS_RESOLVE_UNIX;
    return rights;
}

/* the Landlock rights one LrgSandboxAccess grants */
static guint64
landlock_rights_for_access (LrgSandboxAccess access)
{
    guint64 rights = 0;

    if (access & LRG_SANDBOX_ACCESS_READ)
        rights |= LRG_LL_FS_READ_FILE | LRG_LL_FS_READ_DIR;
    if (access & LRG_SANDBOX_ACCESS_WRITE)
        rights |= LRG_LL_FS_WRITE_FILE | LRG_LL_FS_TRUNCATE |
                  LRG_LL_FS_REMOVE_DIR | LRG_LL_FS_REMOVE_FILE |
                  LRG_LL_FS_MAKE_DIR | LRG_LL_FS_MAKE_REG |
                  LRG_LL_FS_REFER;
    /* no symlinks, sockets, FIFOs or device nodes: a writable directory
     * holds plain files, so a link planted there cannot redirect a later
     * unsandboxed run's writes */
    if (access & LRG_SANDBOX_ACCESS_DEVICE)
        rights |= LRG_LL_FS_READ_FILE | LRG_LL_FS_READ_DIR |
                  LRG_LL_FS_WRITE_FILE | LRG_LL_FS_TRUNCATE |
                  LRG_LL_FS_IOCTL_DEV;
    if (access & LRG_SANDBOX_ACCESS_CONNECT)
        rights |= LRG_LL_FS_RESOLVE_UNIX;
    if (access & LRG_SANDBOX_ACCESS_EXECUTE)
        rights |= LRG_LL_FS_EXECUTE;
    return rights;
}

/* how many threads the process has now (/proc/self/task) */
static guint
count_threads (void)
{
    g_autoptr(GDir) dir = NULL;
    guint count = 0;

    dir = g_dir_open ("/proc/self/task", 0, NULL);
    if (dir == NULL)
        return 0;
    while (g_dir_read_name (dir) != NULL)
        count++;
    return count;
}

static gboolean
add_net_rule (gint      ruleset,
              guint64   rights,
              guint16   port,
              GError  **error)
{
    LrgLlNetPortAttr attr;

    memset (&attr, 0, sizeof (attr));
    attr.allowed_access = rights;
    attr.port = port;
    if (syscall (__NR_landlock_add_rule, ruleset, LRG_LL_RULE_NET_PORT, &attr, (guint32)0) != 0)
    {
        g_set_error (error, LRG_SANDBOX_ERROR, LRG_SANDBOX_ERROR_FAILED,
                     "landlock: cannot allow port %u: %s", (guint)port, g_strerror (errno));
        return FALSE;
    }
    return TRUE;
}

/*
 * apply_landlock:
 * Builds the ruleset for the kernel's ABI, adds the path and port rules
 * and restricts the process (every thread when TSYNC is supported).
 */
static gboolean
apply_landlock (LrgSandbox  *self,
                gint         abi,
                GError     **error)
{
    LrgLlRulesetAttr attr;
    gsize attr_size;
    guint64 fs_rights;
    guint64 net_handled = 0;
    gint ruleset;
    guint i;
    guint threads;

    /* --- the handled rights: everything the ABI knows --- */
    fs_rights = landlock_fs_rights_for_abi (abi);
    memset (&attr, 0, sizeof (attr));
    attr.handled_access_fs = fs_rights;
    attr_size = sizeof (guint64);

    if (abi >= 4 && self->network != LRG_SANDBOX_NETWORK_FULL)
    {
        switch (self->network)
        {
        case LRG_SANDBOX_NETWORK_NONE:
            net_handled = LRG_LL_NET_BIND_TCP | LRG_LL_NET_CONNECT_TCP;
            if (abi >= 10)
                net_handled |= LRG_LL_NET_BIND_UDP | LRG_LL_NET_CONNECT_SEND_UDP;
            break;
        case LRG_SANDBOX_NETWORK_CLIENT:
            /* no binding; connects only to the listed ports (all of them
             * when none were listed) */
            net_handled = LRG_LL_NET_BIND_TCP;
            if (self->ports->len > 0)
                net_handled |= LRG_LL_NET_CONNECT_TCP;
            if (abi >= 10 && self->ports->len > 0)
                net_handled |= LRG_LL_NET_BIND_UDP | LRG_LL_NET_CONNECT_SEND_UDP;
            break;
        case LRG_SANDBOX_NETWORK_SERVER:
            /* binds only the listed ports, never connects out; UDP
             * replies go to the clients' ports, so sends stay open */
            net_handled = LRG_LL_NET_BIND_TCP | LRG_LL_NET_CONNECT_TCP;
            if (abi >= 10)
                net_handled |= LRG_LL_NET_BIND_UDP;
            break;
        case LRG_SANDBOX_NETWORK_FULL:
        default:
            break;
        }
        attr.handled_access_net = net_handled;
        attr_size = 2 * sizeof (guint64);
    }
    if (abi >= 6)
    {
        attr.scoped = LRG_LL_SCOPE_ABSTRACT_UNIX | LRG_LL_SCOPE_SIGNAL;
        attr_size = 3 * sizeof (guint64);
    }

    ruleset = (gint)syscall (__NR_landlock_create_ruleset, &attr, attr_size, (guint32)0);
    if (ruleset < 0)
    {
        g_set_error (error, LRG_SANDBOX_ERROR, LRG_SANDBOX_ERROR_FAILED,
                     "landlock: cannot create the ruleset (ABI %d): %s", abi,
                     g_strerror (errno));
        return FALSE;
    }

    /* --- path rules --- */
    for (i = 0; i < self->paths->len; i++)
    {
        const gchar *path = (const gchar *)g_ptr_array_index (self->paths, i);
        LrgLlPathBeneathAttr rule;
        struct stat st;
        guint64 rights;
        gint fd;

        rights = landlock_rights_for_access (lrg_sandbox_get_path_access (self, path)) & fs_rights;
        if (rights == 0)
            continue;
        fd = open (path, O_PATH | O_CLOEXEC);
        if (fd < 0)
        {
            /* a missing path is not an error: nothing beneath it exists */
            lrg_debug (LRG_LOG_DOMAIN_SANDBOX, "skipping %s: %s", path, g_strerror (errno));
            self->rules_skipped++;
            continue;
        }
        if (fstat (fd, &st) == 0 && !S_ISDIR (st.st_mode))
            rights &= LRG_LL_FS_FILE_RIGHTS;
        if (rights == 0)
        {
            close (fd);
            self->rules_skipped++;
            continue;
        }
        memset (&rule, 0, sizeof (rule));
        rule.allowed_access = rights;
        rule.parent_fd = fd;
        if (syscall (__NR_landlock_add_rule, ruleset, LRG_LL_RULE_PATH_BENEATH, &rule,
                     (guint32)0) != 0)
        {
            g_set_error (error, LRG_SANDBOX_ERROR, LRG_SANDBOX_ERROR_FAILED,
                         "landlock: cannot add the rule for %s: %s", path, g_strerror (errno));
            close (fd);
            close (ruleset);
            return FALSE;
        }
        close (fd);
        self->rules_added++;
    }

    /* --- port rules --- */
    if (net_handled != 0)
    {
        for (i = 0; i < self->ports->len; i++)
        {
            guint16 port = g_array_index (self->ports, guint16, i);
            guint64 rights = 0;

            if (self->network == LRG_SANDBOX_NETWORK_CLIENT)
                rights = net_handled & (LRG_LL_NET_CONNECT_TCP | LRG_LL_NET_CONNECT_SEND_UDP);
            else if (self->network == LRG_SANDBOX_NETWORK_SERVER)
                rights = net_handled & (LRG_LL_NET_BIND_TCP | LRG_LL_NET_BIND_UDP);
            if (rights != 0 && !add_net_rule (ruleset, rights, port, error))
            {
                close (ruleset);
                return FALSE;
            }
        }
        /* a UDP client's first send binds an ephemeral port (port 0) */
        if (self->network == LRG_SANDBOX_NETWORK_CLIENT && (net_handled & LRG_LL_NET_BIND_UDP) &&
            !add_net_rule (ruleset, LRG_LL_NET_BIND_UDP, 0, error))
        {
            close (ruleset);
            return FALSE;
        }
    }

    /* --- restrict: every thread when the kernel can (TSYNC) --- */
    threads = count_threads ();
    if (syscall (__NR_landlock_restrict_self, ruleset, (guint32)LRG_LL_RESTRICT_SELF_TSYNC) == 0)
    {
        self->threads_synced = TRUE;
    }
    else if (errno == EINVAL)
    {
        if (syscall (__NR_landlock_restrict_self, ruleset, (guint32)0) != 0)
        {
            g_set_error (error, LRG_SANDBOX_ERROR, LRG_SANDBOX_ERROR_FAILED,
                         "landlock: cannot restrict the process: %s", g_strerror (errno));
            close (ruleset);
            return FALSE;
        }
        self->threads_synced = threads <= 1;
        if (!self->threads_synced)
            lrg_warning (LRG_LOG_DOMAIN_SANDBOX,
                         "Landlock ABI %d cannot restrict the %u other threads that already "
                         "run; only this thread and the threads it starts are restricted",
                         abi, threads - 1);
    }
    else
    {
        g_set_error (error, LRG_SANDBOX_ERROR, LRG_SANDBOX_ERROR_FAILED,
                     "landlock: cannot restrict the process: %s", g_strerror (errno));
        close (ruleset);
        return FALSE;
    }
    close (ruleset);
    self->applied_abi = abi;
    return TRUE;
}

/* ==========================================================================
 * seccomp
 * ========================================================================== */

#if defined(__x86_64__)
#define LRG_SECCOMP_ARCH AUDIT_ARCH_X86_64
#elif defined(__aarch64__)
#define LRG_SECCOMP_ARCH AUDIT_ARCH_AARCH64
#elif defined(__riscv) && __riscv_xlen == 64
#define LRG_SECCOMP_ARCH AUDIT_ARCH_RISCV64
#endif

#ifndef SECCOMP_RET_KILL_PROCESS
#define SECCOMP_RET_KILL_PROCESS 0x80000000U
#endif
#ifndef SECCOMP_FILTER_FLAG_TSYNC
#define SECCOMP_FILTER_FLAG_TSYNC (1UL << 0)
#endif
#ifndef SECCOMP_SET_MODE_FILTER
#define SECCOMP_SET_MODE_FILTER 1
#endif

/* the low 32 bits of a system call argument in struct seccomp_data */
#if G_BYTE_ORDER == G_LITTLE_ENDIAN
#define LRG_ARG_LO(n) ((guint32)(offsetof (struct seccomp_data, args) + 8 * (n)))
#else
#define LRG_ARG_LO(n) ((guint32)(offsetof (struct seccomp_data, args) + 8 * (n) + 4))
#endif

#ifdef LRG_SECCOMP_ARCH

/* the system calls the filter denies with EPERM; numbers that do not
 * exist on an architecture are left out at compile time */
static const gint denied_syscalls[] = {
#ifdef __NR_ptrace
    __NR_ptrace,
#endif
#ifdef __NR_process_vm_readv
    __NR_process_vm_readv,
#endif
#ifdef __NR_process_vm_writev
    __NR_process_vm_writev,
#endif
#ifdef __NR_mount
    __NR_mount,
#endif
#ifdef __NR_umount2
    __NR_umount2,
#endif
#ifdef __NR_pivot_root
    __NR_pivot_root,
#endif
#ifdef __NR_chroot
    __NR_chroot,
#endif
#ifdef __NR_unshare
    __NR_unshare,
#endif
#ifdef __NR_setns
    __NR_setns,
#endif
#ifdef __NR_open_tree
    __NR_open_tree,
#endif
#ifdef __NR_move_mount
    __NR_move_mount,
#endif
#ifdef __NR_fsopen
    __NR_fsopen,
#endif
#ifdef __NR_fsconfig
    __NR_fsconfig,
#endif
#ifdef __NR_fsmount
    __NR_fsmount,
#endif
#ifdef __NR_fspick
    __NR_fspick,
#endif
#ifdef __NR_mount_setattr
    __NR_mount_setattr,
#endif
#ifdef __NR_kexec_load
    __NR_kexec_load,
#endif
#ifdef __NR_kexec_file_load
    __NR_kexec_file_load,
#endif
#ifdef __NR_init_module
    __NR_init_module,
#endif
#ifdef __NR_finit_module
    __NR_finit_module,
#endif
#ifdef __NR_delete_module
    __NR_delete_module,
#endif
#ifdef __NR_bpf
    __NR_bpf,
#endif
#ifdef __NR_perf_event_open
    __NR_perf_event_open,
#endif
#ifdef __NR_userfaultfd
    __NR_userfaultfd,
#endif
#ifdef __NR_keyctl
    __NR_keyctl,
#endif
#ifdef __NR_add_key
    __NR_add_key,
#endif
#ifdef __NR_request_key
    __NR_request_key,
#endif
#ifdef __NR_reboot
    __NR_reboot,
#endif
#ifdef __NR_swapon
    __NR_swapon,
#endif
#ifdef __NR_swapoff
    __NR_swapoff,
#endif
#ifdef __NR_acct
    __NR_acct,
#endif
#ifdef __NR_settimeofday
    __NR_settimeofday,
#endif
#ifdef __NR_clock_settime
    __NR_clock_settime,
#endif
#ifdef __NR_clock_adjtime
    __NR_clock_adjtime,
#endif
#ifdef __NR_adjtimex
    __NR_adjtimex,
#endif
#ifdef __NR_syslog
    __NR_syslog,
#endif
#ifdef __NR_quotactl
    __NR_quotactl,
#endif
#ifdef __NR_open_by_handle_at
    __NR_open_by_handle_at,
#endif
#ifdef __NR_name_to_handle_at
    __NR_name_to_handle_at,
#endif
#ifdef __NR_uselib
    __NR_uselib,
#endif
#ifdef __NR_ioperm
    __NR_ioperm,
#endif
#ifdef __NR_iopl
    __NR_iopl,
#endif
#ifdef __NR_vhangup
    __NR_vhangup,
#endif
#ifdef __NR_lookup_dcookie
    __NR_lookup_dcookie,
#endif
#ifdef __NR_pidfd_getfd
    __NR_pidfd_getfd,
#endif
    -1
};

/* the clone flags that create namespaces (unshare is denied outright) */
#define LRG_CLONE_NAMESPACE_FLAGS (0x00020000U /* CLONE_NEWNS */ | \
                                   0x02000000U /* CLONE_NEWCGROUP */ | \
                                   0x04000000U /* CLONE_NEWUTS */ | \
                                   0x08000000U /* CLONE_NEWIPC */ | \
                                   0x10000000U /* CLONE_NEWUSER */ | \
                                   0x20000000U /* CLONE_NEWPID */ | \
                                   0x40000000U /* CLONE_NEWNET */)

#ifndef TIOCSTI
#define TIOCSTI 0x5412
#endif
#ifndef TIOCLINUX
#define TIOCLINUX 0x541C
#endif

/*
 * A tiny assembler: instructions are appended with symbolic jump
 * targets (labels), resolved once the whole program is known.  A target
 * of -1 means "fall through" (offset 0).
 */
enum
{
    LABEL_DENY = 0,
    LABEL_ALLOW,
    LABEL_SOCKET,
    LABEL_CLONE,
    LABEL_IOCTL,
    LABEL_ENOSYS,
    LABEL_EACCES,
    LABEL_COUNT
};

typedef struct
{
    struct sock_filter insn;
    gint               jt_label;
    gint               jf_label;
} BpfInsn;

static void
bpf_emit (GArray  *prog,
          guint16  code,
          guint32  k,
          gint     jt_label,
          gint     jf_label)
{
    BpfInsn insn;

    memset (&insn, 0, sizeof (insn));
    insn.insn.code = code;
    insn.insn.k = k;
    insn.jt_label = jt_label;
    insn.jf_label = jf_label;
    g_array_append_val (prog, insn);
}

/*
 * build_seccomp_program:
 * Assembles the filter: architecture check, the denied numbers, then the
 * argument checks for socket(), clone(), clone3() and ioctl().
 * Returns NULL (with @error) when a jump does not fit in 8 bits.
 */
static struct sock_filter *
build_seccomp_program (LrgSandbox  *self,
                       guint       *out_len,
                       GError     **error)
{
    g_autoptr(GArray) prog = NULL;
    gint labels[LABEL_COUNT];
    struct sock_filter *out;
    gboolean allow_inet;
    guint i;

    prog = g_array_new (FALSE, FALSE, sizeof (BpfInsn));
    for (i = 0; i < LABEL_COUNT; i++)
        labels[i] = -1;

    /* --- the architecture: anything else (i386 int 0x80, x32) dies --- */
    bpf_emit (prog, BPF_LD | BPF_W | BPF_ABS, (guint32)offsetof (struct seccomp_data, arch), -1, -1);
    bpf_emit (prog, BPF_JMP | BPF_JEQ | BPF_K, LRG_SECCOMP_ARCH, -2, -1);
    bpf_emit (prog, BPF_RET | BPF_K, SECCOMP_RET_KILL_PROCESS, -1, -1);
    /* -2 above: skip the kill (resolved below as a plain offset of 1) */
    g_array_index (prog, BpfInsn, 1).jt_label = -1;
    g_array_index (prog, BpfInsn, 1).insn.jt = 1;

    bpf_emit (prog, BPF_LD | BPF_W | BPF_ABS, (guint32)offsetof (struct seccomp_data, nr), -1, -1);
#if defined(__x86_64__)
    /* x32 system calls carry bit 30 */
    bpf_emit (prog, BPF_JMP | BPF_JGE | BPF_K, 0x40000000U, LABEL_DENY, -1);
#endif

    /* --- plain denials --- */
    for (i = 0; denied_syscalls[i] != -1; i++)
        bpf_emit (prog, BPF_JMP | BPF_JEQ | BPF_K, (guint32)denied_syscalls[i], LABEL_DENY, -1);
    if (!self->allow_exec)
    {
#ifdef __NR_execve
        bpf_emit (prog, BPF_JMP | BPF_JEQ | BPF_K, (guint32)__NR_execve, LABEL_DENY, -1);
#endif
#ifdef __NR_execveat
        bpf_emit (prog, BPF_JMP | BPF_JEQ | BPF_K, (guint32)__NR_execveat, LABEL_DENY, -1);
#endif
    }

    /* --- the calls whose arguments decide --- */
    if (self->network != LRG_SANDBOX_NETWORK_FULL)
        bpf_emit (prog, BPF_JMP | BPF_JEQ | BPF_K, (guint32)__NR_socket, LABEL_SOCKET, -1);
#ifdef __NR_clone
    bpf_emit (prog, BPF_JMP | BPF_JEQ | BPF_K, (guint32)__NR_clone, LABEL_CLONE, -1);
#endif
#ifdef __NR_clone3
    /* clone3 passes its flags in memory a filter cannot read: ENOSYS
     * makes the C library fall back to clone(), which is checked */
    bpf_emit (prog, BPF_JMP | BPF_JEQ | BPF_K, (guint32)__NR_clone3, LABEL_ENOSYS, -1);
#endif
    bpf_emit (prog, BPF_JMP | BPF_JEQ | BPF_K, (guint32)__NR_ioctl, LABEL_IOCTL, -1);
    /* everything else passes (BPF jumps only go forward, so the
     * argument checks below end in their own copy of this return) */
    bpf_emit (prog, BPF_RET | BPF_K, SECCOMP_RET_ALLOW, -1, -1);

    /* socket(domain, ...): UNIX and netlink always, IP only with a network */
    allow_inet = self->network != LRG_SANDBOX_NETWORK_NONE;
    labels[LABEL_SOCKET] = (gint)prog->len;
    bpf_emit (prog, BPF_LD | BPF_W | BPF_ABS, LRG_ARG_LO (0), -1, -1);
    bpf_emit (prog, BPF_JMP | BPF_JEQ | BPF_K, AF_UNIX, LABEL_ALLOW, -1);
    bpf_emit (prog, BPF_JMP | BPF_JEQ | BPF_K, AF_NETLINK, LABEL_ALLOW, -1);
    if (allow_inet)
    {
        bpf_emit (prog, BPF_JMP | BPF_JEQ | BPF_K, AF_INET, LABEL_ALLOW, -1);
        bpf_emit (prog, BPF_JMP | BPF_JEQ | BPF_K, AF_INET6, LABEL_ALLOW, -1);
    }
    bpf_emit (prog, BPF_JMP | BPF_JA | BPF_K, 0, -1, -1);
    g_array_index (prog, BpfInsn, prog->len - 1).jt_label = LABEL_EACCES;

    /* clone(flags, ...): no new namespaces */
    labels[LABEL_CLONE] = (gint)prog->len;
    bpf_emit (prog, BPF_LD | BPF_W | BPF_ABS, LRG_ARG_LO (0), -1, -1);
    bpf_emit (prog, BPF_JMP | BPF_JSET | BPF_K, LRG_CLONE_NAMESPACE_FLAGS, LABEL_DENY, LABEL_ALLOW);

    /* ioctl(fd, request, ...): no typing into the terminal */
    labels[LABEL_IOCTL] = (gint)prog->len;
    bpf_emit (prog, BPF_LD | BPF_W | BPF_ABS, LRG_ARG_LO (1), -1, -1);
    bpf_emit (prog, BPF_JMP | BPF_JEQ | BPF_K, TIOCSTI, LABEL_DENY, -1);
    bpf_emit (prog, BPF_JMP | BPF_JEQ | BPF_K, TIOCLINUX, LABEL_DENY, LABEL_ALLOW);

    labels[LABEL_ALLOW] = (gint)prog->len;
    bpf_emit (prog, BPF_RET | BPF_K, SECCOMP_RET_ALLOW, -1, -1);
    labels[LABEL_EACCES] = (gint)prog->len;
    bpf_emit (prog, BPF_RET | BPF_K, SECCOMP_RET_ERRNO | (EACCES & SECCOMP_RET_DATA), -1, -1);
    labels[LABEL_ENOSYS] = (gint)prog->len;
    bpf_emit (prog, BPF_RET | BPF_K, SECCOMP_RET_ERRNO | (ENOSYS & SECCOMP_RET_DATA), -1, -1);
    labels[LABEL_DENY] = (gint)prog->len;
    bpf_emit (prog, BPF_RET | BPF_K, SECCOMP_RET_ERRNO | (EPERM & SECCOMP_RET_DATA), -1, -1);

    /* --- resolve the labels into relative 8-bit offsets --- */
    out = g_new0 (struct sock_filter, prog->len);
    for (i = 0; i < prog->len; i++)
    {
        BpfInsn *insn = &g_array_index (prog, BpfInsn, i);
        gint targets[2];
        gint which;

        out[i] = insn->insn;
        targets[0] = insn->jt_label;
        targets[1] = insn->jf_label;
        for (which = 0; which < 2; which++)
        {
            gint offset;

            if (targets[which] < 0)
                continue;
            offset = labels[targets[which]] - (gint)i - 1;
            if (offset < 0 || offset > 255)
            {
                g_set_error (error, LRG_SANDBOX_ERROR, LRG_SANDBOX_ERROR_FAILED,
                             "seccomp: jump %d out of range at %u", offset, i);
                g_free (out);
                return NULL;
            }
            if (BPF_OP (out[i].code) == BPF_JA)
                out[i].k = (guint32)offset;
            else if (which == 0)
                out[i].jt = (guint8)offset;
            else
                out[i].jf = (guint8)offset;
        }
    }
    *out_len = prog->len;
    return out;
}

static gboolean
apply_seccomp (LrgSandbox  *self,
               GError     **error)
{
    g_autofree struct sock_filter *filter = NULL;
    struct sock_fprog fprog;
    guint len = 0;

    filter = build_seccomp_program (self, &len, error);
    if (filter == NULL)
        return FALSE;
    memset (&fprog, 0, sizeof (fprog));
    fprog.len = (unsigned short)len;
    fprog.filter = filter;

    /* TSYNC puts every thread under the filter (and no_new_privs) */
    if (syscall (__NR_seccomp, SECCOMP_SET_MODE_FILTER, SECCOMP_FILTER_FLAG_TSYNC, &fprog) != 0)
    {
        g_set_error (error, LRG_SANDBOX_ERROR,
                     errno == EINVAL || errno == ENOSYS ? LRG_SANDBOX_ERROR_UNSUPPORTED
                                                         : LRG_SANDBOX_ERROR_FAILED,
                     "seccomp: cannot install the filter: %s", g_strerror (errno));
        return FALSE;
    }
    self->seccomp_applied = TRUE;
    return TRUE;
}

#endif /* LRG_SECCOMP_ARCH */
#endif /* __linux__ */

/* ==========================================================================
 * Apply
 * ========================================================================== */

gboolean
lrg_sandbox_apply (LrgSandbox  *self,
                   GError     **error)
{
#if defined(__linux__) && defined(LRG_SECCOMP_ARCH)
    gint abi;

    g_return_val_if_fail (LRG_IS_SANDBOX (self), FALSE);
    g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

    if (self->applied)
    {
        g_set_error_literal (error, LRG_SANDBOX_ERROR, LRG_SANDBOX_ERROR_ALREADY_APPLIED,
                             "the sandbox is already applied");
        return FALSE;
    }

    /* --- validate before anything irreversible happens --- */
    {
        guint i;

        for (i = 0; i < self->paths->len; i++)
        {
            const gchar *path = (const gchar *)g_ptr_array_index (self->paths, i);

            if (!g_path_is_absolute (path))
            {
                g_set_error (error, LRG_SANDBOX_ERROR, LRG_SANDBOX_ERROR_INVALID_PATH,
                             "sandbox rule for a relative path: %s", path);
                return FALSE;
            }
        }
    }
    abi = lrg_sandbox_get_landlock_abi ();
    if (abi == 0 && self->require_landlock)
    {
        g_set_error_literal (error, LRG_SANDBOX_ERROR, LRG_SANDBOX_ERROR_UNSUPPORTED,
                             "the kernel has no Landlock (Linux 5.13 or newer with "
                             "landlock in the lsm= list): the filesystem cannot be restricted");
        return FALSE;
    }

    /* --- 1. no_new_privs: setuid binaries and file caps stop working --- */
    if (prctl (PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) != 0)
    {
        g_set_error (error, LRG_SANDBOX_ERROR, LRG_SANDBOX_ERROR_FAILED,
                     "prctl(PR_SET_NO_NEW_PRIVS): %s", g_strerror (errno));
        return FALSE;
    }

    /* --- 2. Landlock: filesystem, ports, IPC scopes --- */
    if (abi > 0)
    {
        if (!apply_landlock (self, abi, error))
            return FALSE;
    }
    else
    {
        lrg_warning (LRG_LOG_DOMAIN_SANDBOX,
                     "the kernel has no Landlock: only no_new_privs and seccomp are applied");
        self->threads_synced = TRUE;
    }

    /* --- 3. seccomp: last, so the calls above were not filtered --- */
    if (!apply_seccomp (self, error))
        return FALSE;

    self->applied = TRUE;
    lrg_debug (LRG_LOG_DOMAIN_SANDBOX, "sandbox applied: Landlock ABI %d, %u rules (%u skipped)",
               abi, self->rules_added, self->rules_skipped);
    return TRUE;
#else
    g_return_val_if_fail (LRG_IS_SANDBOX (self), FALSE);
    g_set_error_literal (error, LRG_SANDBOX_ERROR, LRG_SANDBOX_ERROR_UNSUPPORTED,
                         "the sandbox needs Linux on x86_64, aarch64 or riscv64");
    return FALSE;
#endif
}

/* ==========================================================================
 * Description and bubblewrap
 * ========================================================================== */

gchar *
lrg_sandbox_describe (LrgSandbox *self)
{
    GString *out;
    GEnumClass *network_class;
    GEnumValue *network_value;
    guint i;

    g_return_val_if_fail (LRG_IS_SANDBOX (self), NULL);

    out = g_string_new (NULL);
    network_class = (GEnumClass *)g_type_class_ref (LRG_TYPE_SANDBOX_NETWORK);
    network_value = g_enum_get_value (network_class, (gint)self->network);
    g_string_append_printf (out, "network: %s", network_value != NULL ? network_value->value_nick : "?");
    for (i = 0; i < self->ports->len; i++)
        g_string_append_printf (out, "%s%u", i == 0 ? " (ports " : ", ",
                                (guint)g_array_index (self->ports, guint16, i));
    g_string_append_printf (out, "%s\nexec: %s\n", self->ports->len > 0 ? ")" : "",
                            self->allow_exec ? "allowed" : "denied");
    g_type_class_unref (network_class);

    for (i = 0; i < self->paths->len; i++)
    {
        const gchar *path = (const gchar *)g_ptr_array_index (self->paths, i);
        LrgSandboxAccess access = lrg_sandbox_get_path_access (self, path);

        g_string_append_printf (out, "path: %s%s%s%s%s %s\n",
                                (access & LRG_SANDBOX_ACCESS_READ) ? "r" : "-",
                                (access & LRG_SANDBOX_ACCESS_WRITE) ? "w" : "-",
                                (access & LRG_SANDBOX_ACCESS_DEVICE) ? "d" : "-",
                                (access & LRG_SANDBOX_ACCESS_CONNECT) ? "c" : "-",
                                (access & LRG_SANDBOX_ACCESS_EXECUTE) ? "x" : "-",
                                path);
    }
    if (self->applied)
        g_string_append_printf (out,
                                "applied: landlock ABI %d (kernel ABI %d), %u rules, %u skipped, "
                                "threads %s, seccomp %s\n",
                                self->applied_abi, lrg_sandbox_get_landlock_abi (),
                                self->rules_added, self->rules_skipped,
                                self->threads_synced ? "all restricted" : "partly restricted",
                                self->seccomp_applied ? "on" : "off");
    else
        g_string_append_printf (out, "not applied (kernel Landlock ABI %d)\n",
                                lrg_sandbox_get_landlock_abi ());
    return g_string_free (out, FALSE);
}

gchar **
lrg_sandbox_build_bwrap_argv (LrgSandbox          *self,
                              const gchar         *bwrap,
                              const gchar * const *argv)
{
    GStrvBuilder *builder;
    gchar **result;
    gboolean dev_bound = FALSE;
    guint i;

    g_return_val_if_fail (LRG_IS_SANDBOX (self), NULL);
    g_return_val_if_fail (argv != NULL && argv[0] != NULL, NULL);

    builder = g_strv_builder_new ();
    g_strv_builder_add (builder, bwrap != NULL ? bwrap : "bwrap");
    g_strv_builder_add_many (builder, "--die-with-parent", "--new-session",
                             "--unshare-all", NULL);
    if (self->network != LRG_SANDBOX_NETWORK_NONE)
        g_strv_builder_add (builder, "--share-net");
    g_strv_builder_add_many (builder, "--cap-drop", "ALL",
                             "--ro-bind", "/", "/",
                             "--proc", "/proc", NULL);

    /* /dev: the real one when a rule grants device access to it, else a
     * minimal one with the granted device subtrees bound in */
    if (lrg_sandbox_get_path_access (self, "/dev") & LRG_SANDBOX_ACCESS_DEVICE)
    {
        g_strv_builder_add_many (builder, "--dev-bind", "/dev", "/dev", NULL);
        dev_bound = TRUE;
    }
    else
        g_strv_builder_add_many (builder, "--dev", "/dev", NULL);
    g_strv_builder_add_many (builder, "--tmpfs", "/tmp", NULL);

    for (i = 0; i < self->paths->len; i++)
    {
        const gchar *path = (const gchar *)g_ptr_array_index (self->paths, i);
        LrgSandboxAccess access = lrg_sandbox_get_path_access (self, path);

        if (!g_file_test (path, G_FILE_TEST_EXISTS))
            continue;
        if (access & LRG_SANDBOX_ACCESS_WRITE)
            g_strv_builder_add_many (builder, "--bind", path, path, NULL);
        else if ((access & LRG_SANDBOX_ACCESS_DEVICE) && !dev_bound &&
                 g_str_has_prefix (path, "/dev/"))
            g_strv_builder_add_many (builder, "--dev-bind", path, path, NULL);
        else if (g_str_has_prefix (path, "/tmp/") || strcmp (path, "/tmp") == 0)
            /* the fresh /tmp hides it; bring the readable path back */
            g_strv_builder_add_many (builder, "--ro-bind", path, path, NULL);
    }
    g_strv_builder_add (builder, "--");
    for (i = 0; argv[i] != NULL; i++)
        g_strv_builder_add (builder, argv[i]);
    result = g_strv_builder_end (builder);
    g_strv_builder_unref (builder);
    return result;
}
