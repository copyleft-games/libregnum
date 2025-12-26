# LrgLocalization - Localization Manager

`LrgLocalization` is a final GObject singleton class that manages multiple locales and provides centralized string lookup, formatting, and language switching. It handles the application's localization needs.

## Type Information

- **Type Name**: `LrgLocalization`
- **Base Class**: `GObject`
- **Type Class**: Final (cannot be subclassed)
- **Pattern**: Singleton (single global instance)
- **Transfer**: Full ownership when returned (usually unnecessary due to singleton pattern)

## Overview

The localization manager:
- **Manages Locales**: Register and store multiple language translations
- **Current Locale**: Maintain the active language
- **Fallback**: Fall back to another language for missing strings
- **Formatting**: Format strings with arguments
- **Signal Support**: Notify about language changes
- **Convenience**: Direct string lookup without managing locales

## Singleton Access

```c
/* Get the global localization manager */
LrgLocalization *loc = lrg_localization_get_default();

/* Same instance everywhere */
LrgLocalization *loc2 = lrg_localization_get_default();
g_assert_true(loc == loc2);
```

## Locale Management

### Add Locales

```c
/* Create and add a locale */
g_autoptr(LrgLocale) en = lrg_locale_new("en", "English");
lrg_locale_set_string(en, "greeting", "Hello");

LrgLocalization *loc = lrg_localization_get_default();
lrg_localization_add_locale(loc, g_steal_pointer(&en));
```

### Add from File

```c
/* Load locale from YAML file and register */
LrgLocalization *loc = lrg_localization_get_default();
GError *error = NULL;

gboolean success = lrg_localization_add_locale_from_file(loc,
                                                         "data/locales/en.yaml",
                                                         &error);
if (!success) {
    g_warning("Failed to load locale: %s", error->message);
    g_clear_error(&error);
}
```

### Get Locale

```c
/* Retrieve registered locale (transfer none) */
LrgLocalization *loc = lrg_localization_get_default();
LrgLocale *en = lrg_localization_get_locale(loc, "en");

if (en != NULL) {
    const gchar *greeting = lrg_locale_get_string(en, "greeting");
}
```

### Check Locale Exists

```c
if (lrg_localization_has_locale(loc, "en")) {
    /* English locale is registered */
}
```

### Remove Locale

```c
/* Unregister a locale */
gboolean removed = lrg_localization_remove_locale(loc, "old_lang");
```

### List Locales

```c
/* Get all registered locale codes (transfer container) */
g_autoptr(GPtrArray) codes = lrg_localization_get_locale_codes(loc);

for (guint i = 0; i < codes->len; i++) {
    const gchar *code = g_ptr_array_index(codes, i);
    LrgLocale *locale = lrg_localization_get_locale(loc, code);
    g_message("Locale: %s (%s)",
              lrg_locale_get_name(locale),
              code);
}

/* Get count of locales */
guint count = lrg_localization_get_locale_count(loc);
```

## Current Locale

### Set Current Locale

```c
/* Switch to a registered locale */
LrgLocalization *loc = lrg_localization_get_default();

gboolean success = lrg_localization_set_current(loc, "en");
if (!success) {
    g_warning("Locale 'en' not registered");
}

/* Emits "locale-changed" signal */
```

### Get Current Locale

```c
/* Get current locale object (transfer none, nullable) */
LrgLocale *current = lrg_localization_get_current(loc);

if (current != NULL) {
    g_message("Current language: %s",
              lrg_locale_get_name(current));
}

/* Get current locale code */
const gchar *code = lrg_localization_get_current_code(loc);
```

## Fallback Locale

Set a fallback locale for missing strings:

```c
LrgLocalization *loc = lrg_localization_get_default();

/* Set fallback - used if string not found in current locale */
lrg_localization_set_fallback(loc, "en");

/* String lookup order:
 * 1. Look in current locale (e.g., "de")
 * 2. If not found, look in fallback locale (e.g., "en")
 * 3. If still not found, return NULL
 */
```

### Get Fallback Locale

```c
LrgLocale *fallback = lrg_localization_get_fallback(loc);

if (fallback != NULL) {
    g_message("Fallback language: %s",
              lrg_locale_get_name(fallback));
}
```

## String Lookup

### Basic Lookup

```c
LrgLocalization *loc = lrg_localization_get_default();

/* Get string from current locale, fall back if needed */
const gchar *str = lrg_localization_get(loc, "greeting");

if (str != NULL) {
    g_message("%s", str);
} else {
    g_message("String not found");
}
```

### Formatted Lookup

```c
/* Get string and format with arguments (printf-style) */
g_autofree gchar *msg = lrg_localization_format(loc, "welcome", "Player");
/* Looks up "welcome": "Welcome, %s!" → "Welcome, Player!" */

/* Multiple arguments */
g_autofree gchar *status = lrg_localization_format(loc, "level_up",
                                                   "Player", 5);
/* Looks up "level_up": "%s reached level %d!" → "Player reached level 5!" */
```

### Plural Lookup

```c
/* Get plural form appropriate for count */
const gchar *str = lrg_localization_get_plural(loc, "items", 5);
/* Returns: "%d items" */

/* Format plural with arguments */
g_autofree gchar *msg = lrg_localization_format_plural(loc, "items", 5, 5);
/* Returns: "5 items" */
```

## Signals

Listen for locale changes:

```c
static void
on_locale_changed(LrgLocalization *loc,
                  gpointer         user_data)
{
    g_message("Language changed to: %s",
              lrg_localization_get_current_code(loc));

    /* Update UI with new language */
}

LrgLocalization *loc = lrg_localization_get_default();
g_signal_connect(loc, "locale-changed",
                G_CALLBACK(on_locale_changed), NULL);

/* Changing locale emits signal */
lrg_localization_set_current(loc, "de");
```

## API Reference

### Singleton
- `lrg_localization_get_default(void)` → `LrgLocalization *` (transfer none)

### Locale Management
- `lrg_localization_add_locale(LrgLocalization *self, LrgLocale *locale)` → `void`
- `lrg_localization_add_locale_from_file(LrgLocalization *self, const gchar *path, GError **error)` → `gboolean`
- `lrg_localization_remove_locale(LrgLocalization *self, const gchar *code)` → `gboolean`
- `lrg_localization_get_locale(LrgLocalization *self, const gchar *code)` → `LrgLocale *` (transfer none, nullable)
- `lrg_localization_has_locale(LrgLocalization *self, const gchar *code)` → `gboolean`
- `lrg_localization_get_locale_codes(LrgLocalization *self)` → `GPtrArray *` (transfer container)
- `lrg_localization_get_locale_count(LrgLocalization *self)` → `guint`

### Current Locale
- `lrg_localization_get_current(LrgLocalization *self)` → `LrgLocale *` (transfer none, nullable)
- `lrg_localization_get_current_code(LrgLocalization *self)` → `const gchar *` (transfer none, nullable)
- `lrg_localization_set_current(LrgLocalization *self, const gchar *code)` → `gboolean`

### Fallback Locale
- `lrg_localization_get_fallback(LrgLocalization *self)` → `LrgLocale *` (transfer none, nullable)
- `lrg_localization_set_fallback(LrgLocalization *self, const gchar *code)` → `gboolean`

### String Lookup
- `lrg_localization_get(LrgLocalization *self, const gchar *key)` → `const gchar *` (transfer none, nullable)
- `lrg_localization_format(LrgLocalization *self, const gchar *key, ...)` → `gchar *` (transfer full, nullable)
- `lrg_localization_get_plural(LrgLocalization *self, const gchar *key, gint count)` → `const gchar *` (transfer none, nullable)
- `lrg_localization_format_plural(LrgLocalization *self, const gchar *key, gint count, ...)` → `gchar *` (transfer full, nullable)

## Example: Complete Setup

```c
/* Initialize localization */
LrgLocalization *loc = lrg_localization_get_default();

/* Load locales from files */
GError *error = NULL;

if (!lrg_localization_add_locale_from_file(loc, "data/locales/en.yaml", &error)) {
    g_warning("Failed to load English: %s", error->message);
    g_clear_error(&error);
}

if (!lrg_localization_add_locale_from_file(loc, "data/locales/de.yaml", &error)) {
    g_warning("Failed to load German: %s", error->message);
    g_clear_error(&error);
}

if (!lrg_localization_add_locale_from_file(loc, "data/locales/fr.yaml", &error)) {
    g_warning("Failed to load French: %s", error->message);
    g_clear_error(&error);
}

/* Set up fallback chain */
lrg_localization_set_fallback(loc, "en");

/* Set current language (from config, environment, or user choice) */
const gchar *user_lang = "de";
if (lrg_localization_has_locale(loc, user_lang)) {
    lrg_localization_set_current(loc, user_lang);
} else {
    lrg_localization_set_current(loc, "en");
}

/* Listen for language changes */
g_signal_connect(loc, "locale-changed",
                G_CALLBACK(on_language_changed), ui_context);
```

## Example: Using Localization in Code

```c
#define _(key) lrg_localization_get(lrg_localization_get_default(), key)
#define _f(key, ...) lrg_localization_format(lrg_localization_get_default(), key, __VA_ARGS__)
#define _p(key, count) lrg_localization_get_plural(lrg_localization_get_default(), key, count)
#define _pf(key, count, ...) lrg_localization_format_plural(lrg_localization_get_default(), key, count, __VA_ARGS__)

/* Usage in code */
g_message("%s", _("greeting"));  /* "Hello" / "Hallo" / "Bonjour" */

g_autofree gchar *welcome = _f("welcome", "Player");
/* "Welcome, Player!" / "Willkommen, Spieler!" / "Bienvenue, Joueur!" */

const gchar *items = _p("items", 5);  /* "%d items" / "%d Artikel" / "%d articles" */

g_autofree gchar *inventory = _pf("items", 5, 5);
/* "5 items" / "5 Artikel" / "5 articles" */
```

## Example: Language Selection

```c
void show_language_menu(void)
{
    LrgLocalization *loc = lrg_localization_get_default();
    g_autoptr(GPtrArray) codes = lrg_localization_get_locale_codes(loc);

    g_message("Available Languages:");

    for (guint i = 0; i < codes->len; i++) {
        const gchar *code = g_ptr_array_index(codes, i);
        LrgLocale *locale = lrg_localization_get_locale(loc, code);

        gboolean is_current = g_str_equal(code,
                                          lrg_localization_get_current_code(loc));

        g_message("%s %s (%s)",
                  is_current ? "[X]" : "[ ]",
                  lrg_locale_get_name(locale),
                  code);
    }
}

void select_language(const gchar *code)
{
    LrgLocalization *loc = lrg_localization_get_default();

    if (lrg_localization_set_current(loc, code)) {
        g_message("Language changed to: %s", code);
        /* UI is automatically updated via "locale-changed" signal */
    } else {
        g_warning("Language '%s' not available", code);
    }
}
```

## See Also

- [LrgLocale](locale.md) - Individual language translations
- [I18N Module Overview](index.md) - Overview of localization system
