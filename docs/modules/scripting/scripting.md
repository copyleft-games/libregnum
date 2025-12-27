# LrgScripting

## Overview

`LrgScripting` is the abstract base class for all scripting backends. It defines the interface that implementations must provide, ensuring consistent behavior regardless of which scripting language is used.

This is a GObject derivable type - concrete implementations like `LrgScriptingLua` and `LrgScriptingPython` inherit from it.

## API Reference

### Loading Scripts

#### lrg_scripting_load_file

```c
gboolean
lrg_scripting_load_file (LrgScripting  *self,
                         const gchar   *path,
                         GError       **error);
```

Load and execute a script from a file.

**Parameters:**
- `self` - The scripting context
- `path` - Path to the script file
- `error` - Return location for error, or NULL

**Returns:** TRUE on success, FALSE on error

**Example:**
```c
g_autoptr(GError) error = NULL;

if (!lrg_scripting_load_file (scripting, "scripts/game.lua", &error))
{
    g_printerr ("Failed to load script: %s\n", error->message);
}
```

#### lrg_scripting_load_string

```c
gboolean
lrg_scripting_load_string (LrgScripting  *self,
                           const gchar   *name,
                           const gchar   *code,
                           GError       **error);
```

Load and execute a script from a string.

**Parameters:**
- `self` - The scripting context
- `name` - Name to identify the script (used in error messages)
- `code` - The script source code
- `error` - Return location for error, or NULL

**Returns:** TRUE on success, FALSE on error

**Example:**
```c
const gchar *code = "player_health = 100";

if (!lrg_scripting_load_string (scripting, "init", code, &error))
{
    g_printerr ("Script error: %s\n", error->message);
}
```

### Calling Script Functions

#### lrg_scripting_call_function

```c
gboolean
lrg_scripting_call_function (LrgScripting  *self,
                             const gchar   *func_name,
                             GValue        *return_value,
                             guint          n_args,
                             const GValue  *args,
                             GError       **error);
```

Call a function defined in the script.

**Parameters:**
- `self` - The scripting context
- `func_name` - Name of the function to call
- `return_value` - Location to store return value (uninitialized GValue), or NULL
- `n_args` - Number of arguments
- `args` - Array of argument GValues, or NULL if n_args is 0
- `error` - Return location for error, or NULL

**Returns:** TRUE on success, FALSE on error

**Example:**
```c
/* Define a function in script */
lrg_scripting_load_string (scripting, "funcs",
                           "function add(a, b) return a + b end",
                           NULL);

/* Call it from C */
GValue args[2] = { G_VALUE_INIT, G_VALUE_INIT };
GValue result = G_VALUE_INIT;

g_value_init (&args[0], G_TYPE_DOUBLE);
g_value_init (&args[1], G_TYPE_DOUBLE);
g_value_set_double (&args[0], 10.0);
g_value_set_double (&args[1], 32.0);

if (lrg_scripting_call_function (scripting, "add", &result, 2, args, &error))
{
    gdouble sum = g_value_get_double (&result);
    g_print ("Result: %f\n", sum);  /* 42.0 */
    g_value_unset (&result);
}

g_value_unset (&args[0]);
g_value_unset (&args[1]);
```

### Registering C Functions

#### LrgScriptingCFunction

```c
typedef gboolean (*LrgScriptingCFunction) (LrgScripting  *scripting,
                                           guint          n_args,
                                           const GValue  *args,
                                           GValue        *return_value,
                                           gpointer       user_data,
                                           GError       **error);
```

Callback signature for C functions exposed to scripts.

**Parameters:**
- `scripting` - The scripting context
- `n_args` - Number of arguments passed from script
- `args` - Array of argument values
- `return_value` - Location to store return value (uninitialized)
- `user_data` - User data passed to register_function
- `error` - Return location for error

**Returns:** TRUE on success, FALSE on error

#### lrg_scripting_register_function

```c
gboolean
lrg_scripting_register_function (LrgScripting           *self,
                                 const gchar            *name,
                                 LrgScriptingCFunction   func,
                                 gpointer                user_data,
                                 GError                **error);
```

Register a C function that can be called from scripts.

**Example:**
```c
static gboolean
my_add (LrgScripting  *scripting,
        guint          n_args,
        const GValue  *args,
        GValue        *return_value,
        gpointer       user_data,
        GError       **error)
{
    gdouble sum = 0;

    for (guint i = 0; i < n_args; i++)
    {
        if (G_VALUE_HOLDS_DOUBLE (&args[i]))
            sum += g_value_get_double (&args[i]);
        else if (G_VALUE_HOLDS_INT64 (&args[i]))
            sum += (gdouble)g_value_get_int64 (&args[i]);
    }

    g_value_init (return_value, G_TYPE_DOUBLE);
    g_value_set_double (return_value, sum);

    return TRUE;
}

/* Register the function */
lrg_scripting_register_function (scripting, "my_add", my_add, NULL, NULL);

/* Now callable from scripts as my_add(1, 2, 3) */
```

### Global Variables

#### lrg_scripting_get_global

```c
gboolean
lrg_scripting_get_global (LrgScripting  *self,
                          const gchar   *name,
                          GValue        *value,
                          GError       **error);
```

Get a global variable from the script context.

**Parameters:**
- `self` - The scripting context
- `name` - Name of the global variable
- `value` - Uninitialized GValue to receive the value
- `error` - Return location for error, or NULL

**Returns:** TRUE on success, FALSE on error

#### lrg_scripting_set_global

```c
gboolean
lrg_scripting_set_global (LrgScripting  *self,
                          const gchar   *name,
                          const GValue  *value,
                          GError       **error);
```

Set a global variable in the script context.

**Example:**
```c
/* Set a global from C */
GValue player_name = G_VALUE_INIT;
g_value_init (&player_name, G_TYPE_STRING);
g_value_set_string (&player_name, "Hero");
lrg_scripting_set_global (scripting, "player_name", &player_name, NULL);
g_value_unset (&player_name);

/* Read a global set by script */
lrg_scripting_load_string (scripting, "vars", "score = 1000", NULL);

GValue score = G_VALUE_INIT;
if (lrg_scripting_get_global (scripting, "score", &score, NULL))
{
    g_print ("Score: %ld\n", g_value_get_int64 (&score));
    g_value_unset (&score);
}
```

### Context Management

#### lrg_scripting_reset

```c
void
lrg_scripting_reset (LrgScripting *self);
```

Reset the script context to a clean state.

This clears all loaded scripts, global variables, and registered functions, returning the scripting context to its initial state. Built-in API objects (Log, Registry, Engine) are preserved.

## Error Handling

Script errors use the `LRG_SCRIPTING_ERROR` domain with the following codes:

| Code | Description |
|------|-------------|
| `LRG_SCRIPTING_ERROR_SYNTAX` | Script has syntax errors |
| `LRG_SCRIPTING_ERROR_RUNTIME` | Runtime error during execution |
| `LRG_SCRIPTING_ERROR_NOT_FOUND` | Function or variable not found |
| `LRG_SCRIPTING_ERROR_TYPE` | Type conversion error |

**Example:**
```c
g_autoptr(GError) error = NULL;

if (!lrg_scripting_load_string (scripting, "bad", "invalid syntax!!!", &error))
{
    if (g_error_matches (error, LRG_SCRIPTING_ERROR, LRG_SCRIPTING_ERROR_SYNTAX))
    {
        g_printerr ("Syntax error: %s\n", error->message);
    }
}
```

## See Also

- [LrgScriptingLua](scripting-lua.md) - Lua implementation
- [LrgScriptingPython](scripting-python.md) - Python implementation
- [Scripting Examples](../../examples/scripting-basics.md) - Usage examples
