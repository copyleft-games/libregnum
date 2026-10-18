/* test-sandbox.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Unit tests for LrgSandbox.  The tests that apply the sandbox run in a
 * forked child process, because applying it is
 * irreversible: the child applies the policy, then checks that execve
 * fails, writes outside the allowed directories fail with EACCES, writes
 * to the allowed ones (including an atomic rename) succeed, reads of the
 * read-only data still work, IP sockets, ptrace and namespaces are
 * refused, and threads still start.  The filesystem and port checks need
 * Landlock; on a kernel without it they are skipped (the seccomp checks
 * still run).
 */

#include <glib.h>
#include <glib/gstdio.h>
#include <libregnum.h>

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <sched.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/ptrace.h>
#include <sys/socket.h>
#include <sys/wait.h>

/* ==========================================================================
 * Helpers
 * ========================================================================== */

/* the system directories a GLib process reads while running */
static void
allow_system (LrgSandbox *sandbox)
{
    static const gchar * const system_paths[] = {
        "/usr", "/lib", "/lib64", "/etc", "/proc", "/sys", NULL
    };
    guint i;

    for (i = 0; system_paths[i] != NULL; i++)
        lrg_sandbox_allow_path (sandbox, system_paths[i], LRG_SANDBOX_ACCESS_READ);
    lrg_sandbox_allow_path (sandbox, "/dev", LRG_SANDBOX_ACCESS_DEVICE);
}

/* open() for writing, returning errno (0 on success) */
static gint
try_create (const gchar *path)
{
    gint fd;

    fd = open (path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
    if (fd < 0)
        return errno;
    close (fd);
    return 0;
}

/* execve() in a forked child; returns the errno it failed with, or -1
 * when the program ran (the sandbox let it through) */
static gint
try_exec (void)
{
    pid_t pid;
    gint status = 0;

    pid = fork ();
    if (pid == 0)
    {
        char *args[] = { (char *)"/bin/true", NULL };
        char *env[] = { NULL };

        execve ("/bin/true", args, env);
        _exit (errno);
    }
    g_assert_cmpint (pid, >, 0);
    g_assert_cmpint (waitpid (pid, &status, 0), ==, pid);
    g_assert_true (WIFEXITED (status));
    return WEXITSTATUS (status) == 0 ? -1 : WEXITSTATUS (status);
}

/* applies @sandbox, failing the test with the error message */
static void
assert_apply (LrgSandbox *sandbox)
{
    g_autoptr(GError) error = NULL;
    gboolean ok;

    ok = lrg_sandbox_apply (sandbox, &error);
    g_assert_no_error (error);
    g_assert_true (ok);
}

/* the directory the filesystem case works in: made and removed by the
 * test process, used by the forked child */
static gchar *test_root = NULL;

/* removes @path and everything below it (the test's own directory) */
static void
remove_tree (const gchar *path)
{
    g_autoptr(GDir) dir = NULL;
    const gchar *name;

    dir = g_dir_open (path, 0, NULL);
    while (dir != NULL && (name = g_dir_read_name (dir)) != NULL)
    {
        g_autofree gchar *child = g_build_filename (path, name, NULL);

        if (g_file_test (child, G_FILE_TEST_IS_DIR) && !g_file_test (child, G_FILE_TEST_IS_SYMLINK))
            remove_tree (child);
        else
            g_unlink (child);
    }
    g_rmdir (path);
}

/*
 * run_forked:
 * Runs @body in a forked child and asserts it exited 0.  Applying the
 * sandbox cannot be undone, so it only happens in the child; the test
 * process stays free to clean up.
 */
static void
run_forked (void (*body) (void))
{
    pid_t pid;
    gint status = 0;

    pid = fork ();
    g_assert_cmpint (pid, >=, 0);
    if (pid == 0)
    {
        body ();
        _exit (0);
    }
    g_assert_cmpint (waitpid (pid, &status, 0), ==, pid);
    if (WIFSIGNALED (status))
        g_error ("the sandboxed child died of signal %d (an assertion failed: see above)",
                 WTERMSIG (status));
    g_assert_true (WIFEXITED (status));
    g_assert_cmpint (WEXITSTATUS (status), ==, 0);
}

static gpointer
thread_noop (gpointer data)
{
    return data;
}

/* a thread that existed before the sandbox: it waits for the go signal,
 * then tries to write outside the allowed directories */
typedef struct
{
    GMutex       lock;
    GCond        cond;
    gboolean     go;
    const gchar *path;
    gint         result;
} EarlyThread;

static gpointer
early_thread_run (gpointer data)
{
    EarlyThread *early = (EarlyThread *)data;

    g_mutex_lock (&early->lock);
    while (!early->go)
        g_cond_wait (&early->cond, &early->lock);
    g_mutex_unlock (&early->lock);
    early->result = try_create (early->path);
    return NULL;
}

/* ==========================================================================
 * Policy (no sandbox applied)
 * ========================================================================== */

static void
test_sandbox_abi (void)
{
    gint abi;

    abi = lrg_sandbox_get_landlock_abi ();
    g_assert_cmpint (abi, >=, 0);
    g_test_message ("kernel Landlock ABI: %d", abi);
}

static void
test_sandbox_policy (void)
{
    g_autoptr(LrgSandbox) sandbox = NULL;
    g_autoptr(GError) error = NULL;
    g_auto(GStrv) paths = NULL;
    g_autofree gchar *text = NULL;

    sandbox = lrg_sandbox_new ();
    g_assert_cmpint (lrg_sandbox_get_network (sandbox), ==, LRG_SANDBOX_NETWORK_NONE);
    g_assert_false (lrg_sandbox_get_allow_exec (sandbox));
    g_assert_true (lrg_sandbox_get_require_landlock (sandbox));

    lrg_sandbox_allow_path (sandbox, "/usr", LRG_SANDBOX_ACCESS_READ);
    lrg_sandbox_allow_path (sandbox, "/var/tmp", LRG_SANDBOX_ACCESS_READ);
    lrg_sandbox_allow_path (sandbox, "/usr", LRG_SANDBOX_ACCESS_EXECUTE);
    g_assert_cmpuint (lrg_sandbox_get_path_access (sandbox, "/usr"), ==,
                      LRG_SANDBOX_ACCESS_READ | LRG_SANDBOX_ACCESS_EXECUTE);
    g_assert_cmpuint (lrg_sandbox_get_path_access (sandbox, "/nowhere"), ==,
                      LRG_SANDBOX_ACCESS_NONE);
    paths = lrg_sandbox_list_paths (sandbox);
    g_assert_cmpuint (g_strv_length (paths), ==, 2);
    g_assert_cmpstr (paths[0], ==, "/usr");
    g_assert_cmpstr (paths[1], ==, "/var/tmp");

    lrg_sandbox_set_network (sandbox, LRG_SANDBOX_NETWORK_SERVER);
    lrg_sandbox_allow_port (sandbox, 7710);
    lrg_sandbox_allow_port (sandbox, 7710);
    text = lrg_sandbox_describe (sandbox);
    g_assert_nonnull (strstr (text, "network: server (ports 7710)"));
    g_assert_nonnull (strstr (text, "path: r---x /usr"));
    g_assert_nonnull (strstr (text, "not applied"));

    /* a relative path is refused before anything irreversible happens */
    lrg_sandbox_allow_path (sandbox, "relative/dir", LRG_SANDBOX_ACCESS_READ);
    g_assert_false (lrg_sandbox_apply (sandbox, &error));
    g_assert_error (error, LRG_SANDBOX_ERROR, LRG_SANDBOX_ERROR_INVALID_PATH);
    g_assert_false (lrg_sandbox_is_applied (sandbox));
}

static void
test_sandbox_enums (void)
{
    GFlagsClass *flags;
    GEnumClass *network;

    flags = (GFlagsClass *)g_type_class_ref (LRG_TYPE_SANDBOX_ACCESS);
    g_assert_nonnull (g_flags_get_value_by_nick (flags, "write"));
    g_type_class_unref (flags);
    network = (GEnumClass *)g_type_class_ref (LRG_TYPE_SANDBOX_NETWORK);
    g_assert_cmpint (g_enum_get_value_by_nick (network, "client")->value, ==,
                     LRG_SANDBOX_NETWORK_CLIENT);
    g_type_class_unref (network);
}

static void
test_sandbox_bwrap_argv (void)
{
    g_autoptr(LrgSandbox) sandbox = NULL;
    g_autofree gchar *dir = NULL;
    g_autofree gchar *joined = NULL;
    g_auto(GStrv) argv = NULL;
    const gchar *cmd[] = { "/usr/bin/game", "--flag", NULL };

    dir = g_dir_make_tmp ("lrg-sandbox-XXXXXX", NULL);
    g_assert_nonnull (dir);
    sandbox = lrg_sandbox_new ();
    lrg_sandbox_allow_path (sandbox, "/dev", LRG_SANDBOX_ACCESS_DEVICE);
    lrg_sandbox_allow_path (sandbox, dir, LRG_SANDBOX_ACCESS_READ | LRG_SANDBOX_ACCESS_WRITE);
    lrg_sandbox_allow_path (sandbox, "/does/not/exist", LRG_SANDBOX_ACCESS_WRITE);

    argv = lrg_sandbox_build_bwrap_argv (sandbox, "/usr/bin/bwrap", cmd);
    joined = g_strjoinv (" ", argv);
    g_assert_cmpstr (argv[0], ==, "/usr/bin/bwrap");
    g_assert_nonnull (strstr (joined, "--die-with-parent --new-session --unshare-all"));
    g_assert_null (strstr (joined, "--share-net"));
    g_assert_nonnull (strstr (joined, "--ro-bind / /"));
    g_assert_nonnull (strstr (joined, "--dev-bind /dev /dev"));
    g_assert_null (strstr (joined, "/does/not/exist"));
    g_assert_true (g_str_has_suffix (joined, "-- /usr/bin/game --flag"));
    {
        g_autofree gchar *bind = g_strdup_printf ("--bind %s %s", dir, dir);

        g_assert_nonnull (strstr (joined, bind));
    }
    g_clear_pointer (&argv, g_strfreev);
    g_clear_pointer (&joined, g_free);

    lrg_sandbox_set_network (sandbox, LRG_SANDBOX_NETWORK_CLIENT);
    argv = lrg_sandbox_build_bwrap_argv (sandbox, NULL, cmd);
    joined = g_strjoinv (" ", argv);
    g_assert_cmpstr (argv[0], ==, "bwrap");
    g_assert_nonnull (strstr (joined, "--share-net"));
    g_rmdir (dir);
}

/* ==========================================================================
 * Applied (child processes)
 * ========================================================================== */

static void
apply_filesystem_child (void)
{
    g_autoptr(LrgSandbox) sandbox = NULL;
    g_autoptr(GError) error = NULL;
    g_autofree gchar *root = NULL;
    g_autofree gchar *data = NULL;
    g_autofree gchar *saves = NULL;
    g_autofree gchar *vars = NULL;
    g_autofree gchar *outside = NULL;
    g_autofree gchar *path = NULL;
    g_autofree gchar *contents = NULL;
    g_autofree gchar *early_path = NULL;
    EarlyThread early;
    GThread *early_thread;
    GThread *thread;
    gint fd;

    /* --- the directory layout: data (read), saves and vars (write) --- */
    root = g_strdup (test_root);
    data = g_build_filename (root, "data", NULL);
    saves = g_build_filename (root, "saves", NULL);
    vars = g_build_filename (root, "saved-variables", NULL);
    outside = g_build_filename (root, "outside", NULL);
    g_assert_cmpint (g_mkdir (data, 0700), ==, 0);
    g_assert_cmpint (g_mkdir (saves, 0700), ==, 0);
    g_assert_cmpint (g_mkdir (vars, 0700), ==, 0);
    g_assert_cmpint (g_mkdir (outside, 0700), ==, 0);
    path = g_build_filename (data, "table.yaml", NULL);
    g_assert_true (g_file_set_contents (path, "answer: 42\n", -1, NULL));
    g_clear_pointer (&path, g_free);

    /* a thread that already runs when the sandbox is applied */
    memset (&early, 0, sizeof (early));
    g_mutex_init (&early.lock);
    g_cond_init (&early.cond);
    early_path = g_build_filename (outside, "from-early-thread", NULL);
    early.path = early_path;
    early_thread = g_thread_new ("early", early_thread_run, &early);

    sandbox = lrg_sandbox_new ();
    allow_system (sandbox);
    lrg_sandbox_allow_path (sandbox, data, LRG_SANDBOX_ACCESS_READ);
    lrg_sandbox_allow_path (sandbox, saves, LRG_SANDBOX_ACCESS_READ | LRG_SANDBOX_ACCESS_WRITE);
    lrg_sandbox_allow_path (sandbox, vars, LRG_SANDBOX_ACCESS_READ | LRG_SANDBOX_ACCESS_WRITE);
    assert_apply (sandbox);
    g_assert_true (lrg_sandbox_is_applied (sandbox));
    g_assert_cmpint (lrg_sandbox_get_applied_abi (sandbox), >=, 1);

    /* --- programs cannot be started --- */
    g_assert_cmpint (try_exec (), ==, EPERM);
    {
        g_autoptr(GError) spawn_error = NULL;
        const gchar *spawn_argv[] = { "/bin/true", NULL };

        g_assert_false (g_spawn_sync (NULL, (gchar **)spawn_argv, NULL, G_SPAWN_DEFAULT,
                                      NULL, NULL, NULL, NULL, NULL, &spawn_error));
        g_assert_nonnull (spawn_error);
    }

    /* --- writes outside the allowed directories fail with EACCES --- */
    path = g_build_filename (outside, "escape", NULL);
    g_assert_cmpint (try_create (path), ==, EACCES);
    g_clear_pointer (&path, g_free);
    path = g_build_filename (data, "table.yaml", NULL);
    fd = open (path, O_WRONLY | O_CLOEXEC);
    g_assert_cmpint (fd, ==, -1);
    g_assert_cmpint (errno, ==, EACCES);
    g_clear_pointer (&path, g_free);
    path = g_build_filename (root, "next-to-saves", NULL);
    g_assert_cmpint (try_create (path), ==, EACCES);
    g_clear_pointer (&path, g_free);
    g_assert_cmpint (try_create ("/var/tmp/lrg-sandbox-escape"), ==, EACCES);

    /* --- reads of the read-only data still work --- */
    path = g_build_filename (data, "table.yaml", NULL);
    g_assert_true (g_file_get_contents (path, &contents, NULL, NULL));
    g_assert_cmpstr (contents, ==, "answer: 42\n");
    g_clear_pointer (&path, g_free);
    g_clear_pointer (&contents, g_free);
    /* ... but nothing outside the rules is readable */
    path = g_build_filename (outside, "..", "outside", NULL);
    g_assert_null (g_dir_open (path, 0, NULL));
    g_clear_pointer (&path, g_free);

    /* --- writes to the saves and saved-variables directories work,
     * including the temporary file + rename of g_file_set_contents --- */
    path = g_build_filename (saves, "slot-1.yaml", NULL);
    g_assert_true (g_file_set_contents (path, "level: 3\n", -1, &error));
    g_assert_no_error (error);
    g_assert_true (g_file_set_contents (path, "level: 4\n", -1, &error));
    g_assert_no_error (error);
    g_clear_pointer (&path, g_free);
    path = g_build_filename (vars, "my-mod", NULL);
    g_assert_cmpint (g_mkdir (path, 0700), ==, 0);
    g_clear_pointer (&path, g_free);
    path = g_build_filename (vars, "my-mod", "state.yaml", NULL);
    g_assert_cmpint (try_create (path), ==, 0);
    g_assert_cmpint (g_unlink (path), ==, 0);
    g_clear_pointer (&path, g_free);
    /* ... but no symlink can be planted there */
    path = g_build_filename (saves, "link", NULL);
    g_assert_cmpint (symlink ("/etc/passwd", path), ==, -1);
    g_assert_cmpint (errno, ==, EACCES);
    g_clear_pointer (&path, g_free);

    /* --- the other system calls --- */
    g_assert_cmpint (socket (AF_INET, SOCK_STREAM, 0), ==, -1);
    g_assert_cmpint (errno, ==, EACCES);
    g_assert_cmpint (socket (AF_INET6, SOCK_DGRAM, 0), ==, -1);
    g_assert_cmpint (errno, ==, EACCES);
    fd = socket (AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    g_assert_cmpint (fd, >=, 0);
    close (fd);
    g_assert_cmpint (ptrace (PTRACE_TRACEME, 0, NULL, NULL), ==, -1);
    g_assert_cmpint (errno, ==, EPERM);
    g_assert_cmpint (unshare (CLONE_NEWUSER), ==, -1);
    g_assert_cmpint (errno, ==, EPERM);

    /* --- threads still start (clone3 falls back to clone) --- */
    thread = g_thread_new ("after", thread_noop, GINT_TO_POINTER (7));
    g_assert_cmpint (GPOINTER_TO_INT (g_thread_join (thread)), ==, 7);

    /* --- the thread that ran before: restricted too with TSYNC --- */
    g_mutex_lock (&early.lock);
    early.go = TRUE;
    g_cond_signal (&early.cond);
    g_mutex_unlock (&early.lock);
    g_thread_join (early_thread);
    if (lrg_sandbox_get_threads_synced (sandbox))
        g_assert_cmpint (early.result, ==, EACCES);
    else
        g_test_message ("kernel without Landlock TSYNC: the early thread wrote (%d)", early.result);

    /* --- applying twice is an error --- */
    g_assert_false (lrg_sandbox_apply (sandbox, &error));
    g_assert_error (error, LRG_SANDBOX_ERROR, LRG_SANDBOX_ERROR_ALREADY_APPLIED);
    g_clear_error (&error);
    g_mutex_clear (&early.lock);
    g_cond_clear (&early.cond);
}

static void
test_sandbox_apply_filesystem (void)
{
    if (lrg_sandbox_get_landlock_abi () == 0)
    {
        g_test_skip ("the kernel has no Landlock (Linux 5.13+, landlock in lsm=)");
        return;
    }
    test_root = g_dir_make_tmp ("lrg-sandbox-XXXXXX", NULL);
    g_assert_nonnull (test_root);
    run_forked (apply_filesystem_child);
    remove_tree (test_root);
    g_clear_pointer (&test_root, g_free);
}

static void
test_sandbox_apply_seccomp_only_child (void)
{
    g_autoptr(LrgSandbox) sandbox = NULL;
    g_autoptr(GError) error = NULL;

    /* without requiring Landlock: seccomp works on every kernel */
    sandbox = lrg_sandbox_new ();
    lrg_sandbox_set_require_landlock (sandbox, FALSE);
    lrg_sandbox_set_network (sandbox, LRG_SANDBOX_NETWORK_FULL);
    lrg_sandbox_allow_path (sandbox, "/", LRG_SANDBOX_ACCESS_READ | LRG_SANDBOX_ACCESS_WRITE |
                            LRG_SANDBOX_ACCESS_DEVICE | LRG_SANDBOX_ACCESS_CONNECT);
    assert_apply (sandbox);
    g_assert_cmpint (try_exec (), ==, EPERM);
    /* the full network allows IP sockets */
    {
        gint fd = socket (AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);

        g_assert_cmpint (fd, >=, 0);
        close (fd);
    }
}

static void
test_sandbox_apply_seccomp_only (void)
{
    run_forked (test_sandbox_apply_seccomp_only_child);
}

static void
test_sandbox_allow_exec_child (void)
{
    g_autoptr(LrgSandbox) sandbox = NULL;
    g_autoptr(GError) error = NULL;

    sandbox = lrg_sandbox_new ();
    lrg_sandbox_set_require_landlock (sandbox, FALSE);
    lrg_sandbox_set_allow_exec (sandbox, TRUE);
    allow_system (sandbox);
    lrg_sandbox_allow_path (sandbox, "/usr", LRG_SANDBOX_ACCESS_EXECUTE);
    lrg_sandbox_allow_path (sandbox, "/bin", LRG_SANDBOX_ACCESS_READ | LRG_SANDBOX_ACCESS_EXECUTE);
    g_assert_true (lrg_sandbox_apply (sandbox, &error));
    g_assert_cmpint (try_exec (), ==, -1);
}

static void
test_sandbox_allow_exec (void)
{
    run_forked (test_sandbox_allow_exec_child);
}

/* a loopback TCP socket bound to @port (0: any); returns its fd or -errno */
static gint
bind_loopback (guint16 port)
{
    struct sockaddr_in addr;
    gint fd;

    fd = socket (AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0)
        return -errno;
    memset (&addr, 0, sizeof (addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons (port);
    addr.sin_addr.s_addr = htonl (INADDR_LOOPBACK);
    if (bind (fd, (struct sockaddr *)&addr, sizeof (addr)) != 0)
    {
        gint saved = errno;

        close (fd);
        return -saved;
    }
    return fd;
}

static gint
connect_loopback (guint16 port)
{
    struct sockaddr_in addr;
    gint fd;
    gint result = 0;

    fd = socket (AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0)
        return errno;
    memset (&addr, 0, sizeof (addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons (port);
    addr.sin_addr.s_addr = htonl (INADDR_LOOPBACK);
    if (connect (fd, (struct sockaddr *)&addr, sizeof (addr)) != 0)
        result = errno;
    close (fd);
    return result;
}

/* a free loopback port, found before the sandbox */
static guint16
free_port (void)
{
    struct sockaddr_in addr;
    socklen_t len = sizeof (addr);
    gint fd;

    fd = bind_loopback (0);
    g_assert_cmpint (fd, >=, 0);
    g_assert_cmpint (getsockname (fd, (struct sockaddr *)&addr, &len), ==, 0);
    close (fd);
    return ntohs (addr.sin_port);
}

static void
test_sandbox_network_server_child (void)
{
    g_autoptr(LrgSandbox) sandbox = NULL;
    g_autoptr(GError) error = NULL;
    guint16 listen_port;
    guint16 other_port;
    gint fd;

    listen_port = free_port ();
    other_port = free_port ();
    if (other_port == listen_port)
        other_port = (guint16)(listen_port + 1);

    sandbox = lrg_sandbox_new ();
    allow_system (sandbox);
    lrg_sandbox_set_network (sandbox, LRG_SANDBOX_NETWORK_SERVER);
    lrg_sandbox_allow_port (sandbox, listen_port);
    assert_apply (sandbox);

    /* the listen port binds, another port does not, nothing connects out */
    fd = bind_loopback (listen_port);
    g_assert_cmpint (fd, >=, 0);
    g_assert_cmpint (listen (fd, 4), ==, 0);
    g_assert_cmpint (bind_loopback (other_port), ==, -EACCES);
    g_assert_cmpint (connect_loopback (listen_port), ==, EACCES);
    close (fd);
}

static void
test_sandbox_network_server (void)
{
    if (lrg_sandbox_get_landlock_abi () < 4)
    {
        g_test_skip ("Landlock TCP rules need ABI 4 (Linux 6.7)");
        return;
    }
    run_forked (test_sandbox_network_server_child);
}

static void
test_sandbox_network_client_child (void)
{
    g_autoptr(LrgSandbox) sandbox = NULL;
    g_autoptr(GError) error = NULL;
    guint16 server_port;
    guint16 other_port;
    gint server;
    gint other;

    /* two listening sockets made before the sandbox */
    server = bind_loopback (0);
    g_assert_cmpint (server, >=, 0);
    g_assert_cmpint (listen (server, 4), ==, 0);
    other = bind_loopback (0);
    g_assert_cmpint (other, >=, 0);
    g_assert_cmpint (listen (other, 4), ==, 0);
    {
        struct sockaddr_in addr;
        socklen_t len = sizeof (addr);

        g_assert_cmpint (getsockname (server, (struct sockaddr *)&addr, &len), ==, 0);
        server_port = ntohs (addr.sin_port);
        len = sizeof (addr);
        g_assert_cmpint (getsockname (other, (struct sockaddr *)&addr, &len), ==, 0);
        other_port = ntohs (addr.sin_port);
    }

    sandbox = lrg_sandbox_new ();
    allow_system (sandbox);
    lrg_sandbox_set_network (sandbox, LRG_SANDBOX_NETWORK_CLIENT);
    lrg_sandbox_allow_port (sandbox, server_port);
    assert_apply (sandbox);

    g_assert_cmpint (connect_loopback (server_port), ==, 0);
    g_assert_cmpint (connect_loopback (other_port), ==, EACCES);
    g_assert_cmpint (bind_loopback (0), ==, -EACCES);
    close (server);
    close (other);
}

static void
test_sandbox_network_client (void)
{
    if (lrg_sandbox_get_landlock_abi () < 4)
    {
        g_test_skip ("Landlock TCP rules need ABI 4 (Linux 6.7)");
        return;
    }
    run_forked (test_sandbox_network_client_child);
}

int
main (int   argc,
      char *argv[])
{
    g_test_init (&argc, &argv, NULL);

    g_test_add_func ("/sandbox/abi", test_sandbox_abi);
    g_test_add_func ("/sandbox/policy", test_sandbox_policy);
    g_test_add_func ("/sandbox/enums", test_sandbox_enums);
    g_test_add_func ("/sandbox/bwrap-argv", test_sandbox_bwrap_argv);
    g_test_add_func ("/sandbox/apply/filesystem", test_sandbox_apply_filesystem);
    g_test_add_func ("/sandbox/apply/seccomp-only", test_sandbox_apply_seccomp_only);
    g_test_add_func ("/sandbox/apply/allow-exec", test_sandbox_allow_exec);
    g_test_add_func ("/sandbox/apply/network-server", test_sandbox_network_server);
    g_test_add_func ("/sandbox/apply/network-client", test_sandbox_network_client);

    return g_test_run ();
}
