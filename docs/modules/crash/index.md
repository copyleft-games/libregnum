# Crash Reporting

The Crash module provides signal handling and crash reporting functionality to capture crashes, generate reports, and optionally display crash dialogs.

## Overview

- **LrgCrashReporter**: Installs signal handlers and generates crash reports
- **LrgCrashDialog**: Abstract base for crash dialog implementations
- **LrgCrashDialogTerminal**: Default implementation that writes to stderr/file

## Basic Usage

```c
#include <libregnum.h>

int
main (int argc, char *argv[])
{
    g_autoptr(GError) error = NULL;
    LrgCrashReporter *reporter;

    /* Get the crash reporter singleton */
    reporter = lrg_crash_reporter_get_default ();

    /* Configure the reporter */
    lrg_crash_reporter_set_app_name (reporter, "My Game");
    lrg_crash_reporter_set_app_version (reporter, "1.0.0");
    lrg_crash_reporter_set_log_path (reporter, "crash.log");

    /* Add custom metadata */
    lrg_crash_reporter_add_metadata (reporter, "level", "5");
    lrg_crash_reporter_add_metadata (reporter, "save_slot", "1");

    /* Install signal handlers */
    if (!lrg_crash_reporter_install (reporter, &error))
    {
        g_warning ("Failed to install crash handler: %s", error->message);
    }

    /* ... game code ... */

    /* On shutdown, uninstall handlers */
    lrg_crash_reporter_uninstall (reporter);

    return 0;
}
```

## Handled Signals

The crash reporter handles the following signals:

| Signal | Description |
|--------|-------------|
| SIGSEGV | Segmentation fault (invalid memory access) |
| SIGABRT | Abort (assertion failure, g_error, etc.) |
| SIGFPE | Floating-point exception (division by zero) |
| SIGBUS | Bus error (invalid memory alignment) |
| SIGILL | Illegal instruction |

## Crash Report Contents

When a crash occurs, the reporter generates a report containing:

- Application name and version
- Signal type and address
- Stack trace (via `backtrace()`)
- Timestamp
- OS and platform information
- Custom metadata
- Last few log messages (if GLib logging is used)

Example crash report:

```
================================================================================
                           CRASH REPORT - My Game
================================================================================
Version: 1.0.0
Timestamp: 2025-01-15 14:32:45 UTC

Signal: SIGSEGV (Segmentation fault)
Address: 0x0000000000000000

Stack Trace:
  #0  0x00007f1234567890 in my_function () at game.c:123
  #1  0x00007f1234567abc in update_game () at game.c:456
  #2  0x00007f1234567def in main_loop () at main.c:89
  ...

Custom Metadata:
  level: 5
  save_slot: 1
  player_position: (123.4, 567.8)

System Information:
  OS: Linux 6.17.9-300.fc43.x86_64
  Platform: x86_64
================================================================================
```

## Custom Crash Dialogs

Create a custom crash dialog by subclassing `LrgCrashDialog`:

```c
#define MY_TYPE_CRASH_DIALOG (my_crash_dialog_get_type ())
G_DECLARE_FINAL_TYPE (MyCrashDialog, my_crash_dialog, MY, CRASH_DIALOG, LrgCrashDialog)

static void
my_crash_dialog_show (LrgCrashDialog *dialog,
                      const gchar    *crash_info)
{
    MyCrashDialog *self = MY_CRASH_DIALOG (dialog);

    /* Show your custom dialog (GTK, raylib GUI, etc.) */
    show_error_window ("Game Crashed", crash_info);
}

static void
my_crash_dialog_hide (LrgCrashDialog *dialog)
{
    MyCrashDialog *self = MY_CRASH_DIALOG (dialog);

    /* Hide/close the dialog */
    close_error_window ();
}

static void
my_crash_dialog_class_init (MyCrashDialogClass *klass)
{
    LrgCrashDialogClass *dialog_class = LRG_CRASH_DIALOG_CLASS (klass);

    dialog_class->show = my_crash_dialog_show;
    dialog_class->hide = my_crash_dialog_hide;
}

/* Use the custom dialog */
MyCrashDialog *dialog = my_crash_dialog_new ();
lrg_crash_reporter_set_dialog (reporter, LRG_CRASH_DIALOG (dialog));
```

## Dynamic Metadata

Update metadata as the game progresses:

```c
void
on_level_loaded (const gchar *level_name)
{
    LrgCrashReporter *reporter = lrg_crash_reporter_get_default ();

    lrg_crash_reporter_add_metadata (reporter, "current_level", level_name);
}

void
on_player_position_changed (gfloat x, gfloat y)
{
    LrgCrashReporter *reporter = lrg_crash_reporter_get_default ();
    g_autofree gchar *pos = g_strdup_printf ("(%.2f, %.2f)", x, y);

    lrg_crash_reporter_add_metadata (reporter, "player_position", pos);
}
```

## Manual Crash Reports

Trigger a crash report manually for error conditions:

```c
if (critical_error_occurred)
{
    LrgCrashReporter *reporter = lrg_crash_reporter_get_default ();

    /* Add error context */
    lrg_crash_reporter_add_metadata (reporter, "error_type", "resource_load_failed");
    lrg_crash_reporter_add_metadata (reporter, "resource", failed_resource_path);

    /* Generate report (without actually crashing) */
    lrg_crash_reporter_report_crash (reporter, 0, NULL, NULL);
}
```

## API Reference

### LrgCrashReporter

| Method | Description |
|--------|-------------|
| `lrg_crash_reporter_new()` | Create new reporter |
| `lrg_crash_reporter_get_default()` | Get singleton instance |
| `lrg_crash_reporter_install()` | Install signal handlers |
| `lrg_crash_reporter_uninstall()` | Remove signal handlers |
| `lrg_crash_reporter_is_installed()` | Check if handlers installed |
| `lrg_crash_reporter_set_dialog()` | Set crash dialog |
| `lrg_crash_reporter_get_dialog()` | Get crash dialog |
| `lrg_crash_reporter_set_app_name()` | Set application name |
| `lrg_crash_reporter_set_app_version()` | Set application version |
| `lrg_crash_reporter_set_log_path()` | Set crash log file path |
| `lrg_crash_reporter_add_metadata()` | Add custom metadata |
| `lrg_crash_reporter_remove_metadata()` | Remove metadata key |
| `lrg_crash_reporter_clear_metadata()` | Clear all metadata |
| `lrg_crash_reporter_report_crash()` | Manually trigger report |

### LrgCrashDialog (Virtual Methods)

| Method | Description |
|--------|-------------|
| `show(crash_info)` | Display crash dialog with info |
| `hide()` | Hide/close the dialog |

### Error Codes

| Error | Description |
|-------|-------------|
| `LRG_CRASH_REPORTER_ERROR_ALREADY_INSTALLED` | Handler already installed |
| `LRG_CRASH_REPORTER_ERROR_SIGNAL_FAILED` | Failed to install signal handler |

## Integration with GLib Logging

The crash reporter automatically integrates with GLib's logging system. Fatal log messages (`G_LOG_LEVEL_ERROR`) will trigger the crash handler.

```c
/* This will trigger a crash report */
g_error ("Something went terribly wrong!");
```

## Considerations

- **Signal Safety**: The crash handler uses only async-signal-safe functions
- **Stack Traces**: Requires debug symbols (`-g`) for meaningful traces
- **Thread Safety**: The reporter is thread-safe, but only one can be installed at a time
- **Recovery**: After a crash report, the default behavior is to exit; override this in your crash dialog if needed
