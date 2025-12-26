# LrgDebugConsole

An interactive command-line console for runtime debugging and game state inspection.

## Overview

`LrgDebugConsole` provides an in-game debug interface with command execution, history, and extensible command registration. It stores output and command history for later inspection.

## Singleton Access

```c
LrgDebugConsole *console = lrg_debug_console_get_default ();

/* Or create new instance */
LrgDebugConsole *console = lrg_debug_console_new ();
```

## Visibility Control

```c
/* Check if visible */
gboolean visible = lrg_debug_console_is_visible (console);

/* Show/hide */
lrg_debug_console_set_visible (console, TRUE);
lrg_debug_console_set_visible (console, FALSE);

/* Toggle */
lrg_debug_console_toggle (console);
```

## Command Registration

### Built-in Commands

The console includes these commands by default:
- `help` - Show available commands
- `clear` - Clear console output
- `echo <args>` - Echo arguments
- `history` - Show command history

### Registering Custom Commands

```c
static gchar *
my_command (LrgDebugConsole  *console,
            guint             argc,
            const gchar     **argv,
            gpointer          user_data)
{
    GameState *state = (GameState *)user_data;

    if (argc < 2)
    {
        return g_strdup ("Usage: mycommand <arg>");
    }

    const gchar *arg = argv[1];
    return g_strdup_printf ("Got: %s (score: %d)",
                            arg, state->score);
}

/* Register */
lrg_debug_console_register_command (console, "mycommand",
                                     "My command description",
                                     my_command,
                                     game_state,  /* user_data */
                                     NULL);       /* destroy notify */
```

### Unregistering Commands

```c
gboolean removed = lrg_debug_console_unregister_command (console, "mycommand");
if (removed)
{
    g_print ("Command removed\n");
}
```

### Getting Command Info

```c
GList *commands = lrg_debug_console_get_commands (console);
for (GList *l = commands; l != NULL; l = l->next)
{
    const gchar *name = (const gchar *)l->data;
    const gchar *desc = lrg_debug_console_get_command_description (console, name);
    g_print ("  %s: %s\n", name, desc);
}
g_list_free (commands);
```

## Command Execution

### Synchronous Execution

```c
g_autoptr(GError) error = NULL;
g_autofree gchar *result = lrg_debug_console_execute (console, "echo hello", &error);

if (error != NULL)
{
    g_print ("Error: %s\n", error->message);
}
else
{
    g_print ("Result: %s\n", result);
}
```

### Parsing Arguments

In your command handler:

```c
static gchar *
cmd_set_score (LrgDebugConsole  *console,
               guint             argc,
               const gchar     **argv,
               gpointer          user_data)
{
    GameState *state = (GameState *)user_data;

    if (argc < 2)
    {
        return g_strdup ("Usage: set-score <value>");
    }

    gint score = g_ascii_strtoll (argv[1], NULL, 10);
    state->score = score;

    return g_strdup_printf ("Score set to %d", score);
}
```

## Output Management

### Printing Text

```c
lrg_debug_console_print (console, "Normal message");
lrg_debug_console_print_error (console, "Error message");
lrg_debug_console_printf (console, "Formatted: %d", value);
```

### Accessing Output

```c
GQueue *output = lrg_debug_console_get_output (console);

g_print ("Console has %u entries\n", g_queue_get_length (output));

/* Iterate output */
for (GList *l = g_queue_peek_head_link (output); l != NULL; l = l->next)
{
    LrgConsoleOutput *entry = (LrgConsoleOutput *)l->data;
    const gchar *text = lrg_console_output_get_text (entry);
    gboolean is_error = lrg_console_output_is_error (entry);
    gint64 timestamp = lrg_console_output_get_timestamp (entry);

    g_print ("[%ld] %s: %s\n", timestamp, is_error ? "ERROR" : "INFO", text);
}
```

### Clearing Output

```c
lrg_debug_console_clear (console);
g_assert_cmpuint (g_queue_get_length (lrg_debug_console_get_output (console)), ==, 0);
```

## History Management

### Accessing History

```c
GQueue *history = lrg_debug_console_get_history (console);

for (GList *l = g_queue_peek_head_link (history); l != NULL; l = l->next)
{
    const gchar *cmd = (const gchar *)l->data;
    g_print ("  %s\n", cmd);
}
```

### Clearing History

```c
lrg_debug_console_clear_history (console);
```

### History Limit

```c
/* Default is 100 entries */
guint max = lrg_debug_console_get_max_history (console);

/* Change limit */
lrg_debug_console_set_max_history (console, 50);
```

## Complete Example

```c
#include <libregnum.h>

typedef struct
{
    gint score;
    gint level;
    gboolean paused;
} GameState;

static gchar *
cmd_status (LrgDebugConsole  *console,
            guint             argc,
            const gchar     **argv,
            gpointer          user_data)
{
    GameState *state = (GameState *)user_data;
    return g_strdup_printf ("Score: %d | Level: %d | Paused: %s",
                            state->score,
                            state->level,
                            state->paused ? "yes" : "no");
}

static gchar *
cmd_set_score (LrgDebugConsole  *console,
               guint             argc,
               const gchar     **argv,
               gpointer          user_data)
{
    GameState *state = (GameState *)user_data;

    if (argc < 2)
        return g_strdup ("Usage: score <value>");

    state->score = g_ascii_strtoll (argv[1], NULL, 10);
    return g_strdup_printf ("Score set to %d", state->score);
}

static gchar *
cmd_pause (LrgDebugConsole  *console,
           guint             argc,
           const gchar     **argv,
           gpointer          user_data)
{
    GameState *state = (GameState *)user_data;
    state->paused = !state->paused;
    return g_strdup (state->paused ? "Paused" : "Resumed");
}

int main (void)
{
    GameState state = { 0, 1, FALSE };

    LrgDebugConsole *console = lrg_debug_console_get_default ();

    /* Register commands */
    lrg_debug_console_register_command (console, "status",
        "Show game status", cmd_status, &state, NULL);
    lrg_debug_console_register_command (console, "score",
        "Set score", cmd_set_score, &state, NULL);
    lrg_debug_console_register_command (console, "pause",
        "Toggle pause", cmd_pause, &state, NULL);

    /* Show console */
    lrg_debug_console_set_visible (console, TRUE);

    /* Execute commands */
    g_autofree gchar *result = lrg_debug_console_execute (console, "status", NULL);
    g_print ("Command result: %s\n", result);

    result = lrg_debug_console_execute (console, "score 1000", NULL);
    g_print ("Command result: %s\n", result);

    result = lrg_debug_console_execute (console, "pause", NULL);
    g_print ("Command result: %s\n", result);

    return 0;
}
```

## Errors

The console uses `LRG_DEBUG_ERROR` domain:
- `LRG_DEBUG_ERROR_COMMAND_NOT_FOUND` - Unknown command
- `LRG_DEBUG_ERROR_INVALID_ARGS` - Invalid arguments
