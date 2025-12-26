# LrgLocale - Language Translations

`LrgLocale` is a derivable GObject class representing a single language or locale with translated strings and pluralization rules. It stores string tables keyed by string identifiers and supports language-specific plural forms.

## Type Information

- **Type Name**: `LrgLocale`
- **Base Class**: `GObject`
- **Type Class**: Derivable (can be subclassed for custom plural rules)
- **Transfer**: Full ownership when returned

## Overview

A locale encapsulates:
- **Identification**: Locale code and display name
- **Strings**: Translated strings organized by key
- **Plurals**: Language-specific plural forms for quantities
- **Plural Rules**: Virtual function determining plural form from count

Each locale is independent and can be registered with the localization manager for use in the application.

## Construction

### Creating Locales

```c
/* Create a new empty locale */
g_autoptr(LrgLocale) en = lrg_locale_new("en", "English");
g_autoptr(LrgLocale) de = lrg_locale_new("de", "Deutsch");
g_autoptr(LrgLocale) fr = lrg_locale_new("fr", "Français");
```

Parameters:
- `code`: Unique locale code (e.g., "en", "de_DE", "pt_BR")
- `name`: Human-readable name for UI display

### Loading from File

```c
/* Load locale from YAML file (transfer full) */
g_autoptr(LrgLocale) locale = NULL;
GError *error = NULL;

locale = lrg_locale_new_from_file("data/locales/en.yaml", &error);

if (error != NULL) {
    g_warning("Failed to load locale: %s", error->message);
    g_clear_error(&error);
}
```

Expected YAML format:

```yaml
code: en
name: English
strings:
  greeting: Hello
  farewell: Goodbye
  welcome: Welcome, %s!
  inventory:
    open: Open Inventory
    close: Close Inventory
  items:
    one: "1 item"
    other: "%d items"
```

## Properties

### Identification

```c
const gchar *code = lrg_locale_get_code(locale);      /* "en" */
const gchar *name = lrg_locale_get_name(locale);      /* "English" */
```

These are set at construction time and cannot be changed.

## String Management

### Basic String Operations

#### Set String
```c
lrg_locale_set_string(locale, "greeting", "Hello");
lrg_locale_set_string(locale, "menu.file.open", "Open File");
lrg_locale_set_string(locale, "error.not_found", "File not found");
```

#### Get String
```c
/* Returns pointer to string, or NULL if not found */
const gchar *greeting = lrg_locale_get_string(locale, "greeting");
if (greeting == NULL) {
    g_message("String not found");
}
```

#### Check String Exists
```c
if (lrg_locale_has_string(locale, "greeting")) {
    /* String is available */
}
```

### String Hierarchies

Use dot-separated keys for organized strings:

```c
/* UI strings */
lrg_locale_set_string(locale, "ui.window.title", "Game");
lrg_locale_set_string(locale, "ui.button.ok", "OK");
lrg_locale_set_string(locale, "ui.button.cancel", "Cancel");

/* Dialog strings */
lrg_locale_set_string(locale, "dialog.confirm.title", "Confirm Action");
lrg_locale_set_string(locale, "dialog.confirm.message", "Are you sure?");

/* Lookup full path */
const gchar *title = lrg_locale_get_string(locale, "dialog.confirm.title");
```

### Get All Keys

```c
/* Get array of all string keys (transfer container) */
g_autoptr(GPtrArray) keys = lrg_locale_get_keys(locale);

for (guint i = 0; i < keys->len; i++) {
    const gchar *key = g_ptr_array_index(keys, i);
    const gchar *str = lrg_locale_get_string(locale, key);
    g_message("%s: %s", key, str);
}

/* Get count of strings */
guint count = lrg_locale_get_string_count(locale);
```

## Pluralization

Different languages have different plural rules. The system supports CLDR (Common Locale Data Repository) plural forms.

### Setting Plural Forms

```c
/* Set singular and plural forms */
lrg_locale_set_plural(locale, "items", LRG_PLURAL_ONE, "%d item");
lrg_locale_set_plural(locale, "items", LRG_PLURAL_OTHER, "%d items");

/* More complex example (Polish) */
lrg_locale_set_plural(locale, "files", LRG_PLURAL_ONE, "1 plik");
lrg_locale_set_plural(locale, "files", LRG_PLURAL_FEW, "%d pliki");
lrg_locale_set_plural(locale, "files", LRG_PLURAL_MANY, "%d plików");
lrg_locale_set_plural(locale, "files", LRG_PLURAL_OTHER, "%d plików");
```

### Getting Plural Forms

```c
/* Get appropriate plural form for a count */
const gchar *form_one = lrg_locale_get_plural(locale, "items", 1);    /* "%d item" */
const gchar *form_many = lrg_locale_get_plural(locale, "items", 5);   /* "%d items" */
const gchar *form_zero = lrg_locale_get_plural(locale, "items", 0);   /* "%d items" */
```

The system automatically determines which plural form to use based on:
1. The count
2. The locale's plural rules (via `get_plural_form`)

### Plural Form Rules

Default plural rules follow English pattern:
- Count == 1 → `LRG_PLURAL_ONE`
- Everything else → `LRG_PLURAL_OTHER`

Available plural forms (CLDR standard):

| Form | Usage | Example |
|------|-------|---------|
| ZERO | Zero items | "0 items" (rare) |
| ONE | Singular | "1 item" |
| TWO | Dual | "2 items" (rare) |
| FEW | Few items | "2-4 items" |
| MANY | Many items | "5-21 items" |
| OTHER | Default | "0, 5+ items" |

### Plural Form Selection

The `get_plural_form` virtual function determines which form to use:

```c
/* Override for custom plural rules */
static LrgPluralForm
my_locale_get_plural_form(LrgLocale *self, gint count)
{
    /* Implement language-specific rules */
    if (count == 0)
        return LRG_PLURAL_ZERO;
    if (count == 1)
        return LRG_PLURAL_ONE;
    if (count % 10 == 2 || count % 10 == 3 || count % 10 == 4)
        return LRG_PLURAL_FEW;
    return LRG_PLURAL_OTHER;
}
```

## API Reference

### Construction
- `lrg_locale_new(const gchar *code, const gchar *name)` → `LrgLocale *` (transfer full)
- `lrg_locale_new_from_file(const gchar *path, GError **error)` → `LrgLocale *` (transfer full, nullable)

### Properties
- `lrg_locale_get_code(LrgLocale *self)` → `const gchar *` (transfer none)
- `lrg_locale_get_name(LrgLocale *self)` → `const gchar *` (transfer none)

### String Management
- `lrg_locale_set_string(LrgLocale *self, const gchar *key, const gchar *value)` → `void`
- `lrg_locale_get_string(LrgLocale *self, const gchar *key)` → `const gchar *` (transfer none, nullable)
- `lrg_locale_has_string(LrgLocale *self, const gchar *key)` → `gboolean`
- `lrg_locale_get_string_count(LrgLocale *self)` → `guint`
- `lrg_locale_get_keys(LrgLocale *self)` → `GPtrArray *` (transfer container)

### Pluralization
- `lrg_locale_set_plural(LrgLocale *self, const gchar *key, LrgPluralForm form, const gchar *value)` → `void`
- `lrg_locale_get_plural(LrgLocale *self, const gchar *key, gint count)` → `const gchar *` (transfer none, nullable)
- `lrg_locale_get_plural_form(LrgLocale *self, gint count)` → `LrgPluralForm`

## Example: Complete Locale Setup

```c
/* Create English locale */
g_autoptr(LrgLocale) en = lrg_locale_new("en", "English");

/* Basic strings */
lrg_locale_set_string(en, "game.title", "My Game");
lrg_locale_set_string(en, "game.version", "1.0");

/* UI strings with hierarchy */
lrg_locale_set_string(en, "ui.button.ok", "OK");
lrg_locale_set_string(en, "ui.button.cancel", "Cancel");
lrg_locale_set_string(en, "ui.button.save", "Save");

/* Inventory strings */
lrg_locale_set_string(en, "inventory.title", "Inventory");
lrg_locale_set_string(en, "inventory.weight", "Weight: %s");

/* Formatted strings (printf-style) */
lrg_locale_set_string(en, "greeting", "Hello, %s!");
lrg_locale_set_string(en, "level_up", "You reached level %d!");

/* Plural forms */
lrg_locale_set_plural(en, "items", LRG_PLURAL_ONE, "1 item");
lrg_locale_set_plural(en, "items", LRG_PLURAL_OTHER, "%d items");

lrg_locale_set_plural(en, "enemies", LRG_PLURAL_ONE, "1 enemy");
lrg_locale_set_plural(en, "enemies", LRG_PLURAL_OTHER, "%d enemies");

lrg_locale_set_plural(en, "days", LRG_PLURAL_ONE, "1 day");
lrg_locale_set_plural(en, "days", LRG_PLURAL_OTHER, "%d days");
```

## Example: Custom Plural Rules

```c
/* Russian has complex plural rules */
typedef struct _RussianLocale RussianLocale;
G_DECLARE_DERIVABLE_TYPE(RussianLocale, russian_locale, RUSSIAN, LOCALE, LrgLocale)

static LrgPluralForm
russian_locale_get_plural_form(LrgLocale *self, gint count)
{
    if (count % 10 == 1 && count % 100 != 11)
        return LRG_PLURAL_ONE;
    if (count % 10 >= 2 && count % 10 <= 4 && (count % 100 < 10 || count % 100 >= 20))
        return LRG_PLURAL_FEW;
    return LRG_PLURAL_OTHER;
}

/* Usage */
RussianLocale *ru = g_object_new(RUSSIAN_TYPE_LOCALE, NULL);
/* ... */
LrgPluralForm form = lrg_locale_get_plural_form(LRG_LOCALE(ru), 5);
```

## Loading Locales from Files

YAML file format:

```yaml
code: en_US
name: English (United States)

strings:
  # UI strings
  ui:
    button:
      ok: OK
      cancel: Cancel
      save: Save
      load: Load
    menu:
      file: File
      edit: Edit
      help: Help

  # Game strings
  game:
    title: My Awesome Game
    start: Start Game
    quit: Quit

  # Dialog strings
  dialog:
    confirm:
      title: Confirm Action
      message: Are you sure?
    error:
      title: Error
      notfound: File not found

  # Plural strings
  items:
    one: "%d item"
    other: "%d items"
  enemies:
    one: "%d enemy"
    other: "%d enemies"
  days:
    one: "%d day"
    other: "%d days"
```

## See Also

- [LrgLocalization](localization.md) - Locale management singleton
- [I18N Module Overview](index.md) - Overview of localization system
