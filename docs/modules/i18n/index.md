# I18N (Internationalization) Module

The I18N module provides a complete localization system for supporting multiple languages in games. It handles string translation, pluralization with language-specific rules, and dynamic locale switching.

## Overview

The I18N module consists of two main components:

1. **LrgLocale** - Represents a single language/locale with translated strings
2. **LrgLocalization** - Singleton manager handling multiple locales and string lookup

## Key Features

- **Multiple Locales**: Store and manage multiple language translations
- **Pluralization Support**: Handle language-specific plural rules (CLDR-based)
- **String Formatting**: Format strings with printf-style arguments
- **Fallback Locales**: Fall back to another language for missing translations
- **YAML-Based**: Load translations from YAML files
- **Dynamic Switching**: Change language at runtime
- **Extendable**: Create custom locale subclasses with different pluralization rules

## Components

### [LrgLocale - Language Translations](locale.md)

A locale represents a single language with its strings and pluralization rules. Locales are derivable, allowing custom pluralization implementations for languages with complex plural rules.

**Key Concepts:**
- Unique locale code (e.g., "en", "de_DE")
- String lookup by key
- Plural form handling (one, few, many, other, etc.)
- Custom properties via subclassing

### [LrgLocalization - Localization Manager](localization.md)

A singleton manager that handles multiple locales. Provides convenient string lookup with automatic fallback and formatting capabilities.

**Key Concepts:**
- Singleton access pattern
- Register and manage multiple locales
- Set current and fallback locales
- Direct string lookup and formatting
- Signals for locale changes

## Usage Example

```c
/* Create locales */
g_autoptr(LrgLocale) en = lrg_locale_new("en", "English");
lrg_locale_set_string(en, "greeting", "Hello");
lrg_locale_set_plural(en, "items", LRG_PLURAL_ONE, "1 item");
lrg_locale_set_plural(en, "items", LRG_PLURAL_OTHER, "%d items");

g_autoptr(LrgLocale) de = lrg_locale_new("de", "Deutsch");
lrg_locale_set_string(de, "greeting", "Hallo");
lrg_locale_set_plural(de, "items", LRG_PLURAL_ONE, "1 Artikel");
lrg_locale_set_plural(de, "items", LRG_PLURAL_OTHER, "%d Artikel");

/* Register locales */
LrgLocalization *loc = lrg_localization_get_default();
lrg_localization_add_locale(loc, g_steal_pointer(&en));
lrg_localization_add_locale(loc, g_steal_pointer(&de));

/* Use localization */
lrg_localization_set_current(loc, "en");
const gchar *greeting = lrg_localization_get(loc, "greeting");  /* "Hello" */

/* Format strings */
g_autofree gchar *msg = lrg_localization_format(loc, "greeting");  /* "Hello" */

/* Pluralization */
const gchar *items_str = lrg_localization_get_plural(loc, "items", 5);  /* "%d items" */
g_autofree gchar *formatted = lrg_localization_format_plural(loc, "items", 5, 5);
/* "5 items" */
```

## Plural Rules

Different languages have different plural rules. The system supports CLDR (Common Locale Data Repository) plural forms:

- `LRG_PLURAL_ZERO` - For zero (e.g., "0 items" in some languages)
- `LRG_PLURAL_ONE` - For singular (e.g., "1 item")
- `LRG_PLURAL_TWO` - For dual (rare, some Slavic languages)
- `LRG_PLURAL_FEW` - For few (e.g., "2-4" in Czech)
- `LRG_PLURAL_MANY` - For many (e.g., "5-21" in Polish)
- `LRG_PLURAL_OTHER` - Default/fallback form

English uses simple rules:
- `count == 1` → ONE form
- Everything else → OTHER form

Polish uses complex rules:
- `count == 1` → ONE
- `count % 10 in 2..4 && count % 100 not in 12..14` → FEW
- `count != 1 && count % 10 in 0..1 || count % 10 in 5..9 || count % 100 in 12..14` → MANY
- Everything else → OTHER

## YAML Format

Locales can be loaded from YAML files:

```yaml
# locales/en.yaml
code: en
name: English
strings:
  greeting: Hello
  farewell: Goodbye
  welcome: Welcome, %s!
  items:
    one: "%d item"
    other: "%d items"
  files:
    one: "1 file"
    other: "%d files"
```

```yaml
# locales/de.yaml
code: de
name: Deutsch
strings:
  greeting: Hallo
  farewell: Auf Wiedersehen
  welcome: Willkommen, %s!
  items:
    one: "%d Artikel"
    other: "%d Artikel"
  files:
    one: "1 Datei"
    other: "%d Dateien"
```

## Usage Patterns

### Nested String Keys

Organize strings hierarchically:

```c
lrg_locale_set_string(locale, "menu.file.open", "Open");
lrg_locale_set_string(locale, "menu.file.save", "Save");
lrg_locale_set_string(locale, "menu.edit.cut", "Cut");

/* Lookup with full key path */
const gchar *open = lrg_locale_get_string(locale, "menu.file.open");
```

### Contextual Strings

Use prefixes for context:

```c
/* Different contexts for same word */
lrg_locale_set_string(locale, "btn.ok", "OK");
lrg_locale_set_string(locale, "dialog.ok", "OK");
lrg_locale_set_string(locale, "tooltip.ok", "Click to proceed");
```

### Dynamic Translation

Load translations at runtime:

```c
LrgLocalization *loc = lrg_localization_get_default();

/* Load from file */
GError *error = NULL;
gboolean success = lrg_localization_add_locale_from_file(loc,
                                                        "data/locales/en.yaml",
                                                        &error);
if (!success) {
    g_warning("Failed to load locale: %s", error->message);
}

/* Switch to new locale */
lrg_localization_set_current(loc, "en");
```

### Fallback Chain

Set up language fallbacks:

```c
LrgLocalization *loc = lrg_localization_get_default();

/* Add en_US (US English) and fallback to en */
lrg_localization_add_locale(loc, g_steal_pointer(&en_us));
lrg_localization_add_locale(loc, g_steal_pointer(&en));

/* Use en_US as current, en as fallback for missing strings */
lrg_localization_set_current(loc, "en_US");
lrg_localization_set_fallback(loc, "en");

/* If string not in en_US, falls back to en */
const gchar *str = lrg_localization_get(loc, "some_key");
```

## Design Patterns

### Per-Module Localization

Create locale files for each game system:

```
data/locales/
  en/
    menu.yaml
    dialog.yaml
    inventory.yaml
    combat.yaml
  de/
    menu.yaml
    dialog.yaml
    inventory.yaml
    combat.yaml
```

Load and merge at startup:

```c
void load_all_locales(const gchar *lang)
{
    LrgLocalization *loc = lrg_localization_get_default();
    const gchar *modules[] = { "menu", "dialog", "inventory", "combat" };

    for (int i = 0; i < G_N_ELEMENTS(modules); i++) {
        g_autofree gchar *path = g_strdup_printf("data/locales/%s/%s.yaml",
                                                 lang, modules[i]);
        lrg_localization_add_locale_from_file(loc, path, NULL);
    }
}
```

### Translatable Strings in Code

Use gettext-style conventions:

```c
#define _(string) lrg_localization_get(lrg_localization_get_default(), string)
#define _p(key, count) lrg_localization_get_plural(lrg_localization_get_default(), key, count)

/* Usage */
g_message("%s", _("greeting"));
g_autofree gchar *msg = g_strdup_printf(_("welcome"), "Player");
const gchar *items = _p("items", item_count);
```

### Language Selection Menu

```c
void show_language_menu(void)
{
    LrgLocalization *loc = lrg_localization_get_default();
    g_autoptr(GPtrArray) codes = lrg_localization_get_locale_codes(loc);

    for (guint i = 0; i < codes->len; i++) {
        const gchar *code = g_ptr_array_index(codes, i);
        LrgLocale *locale = lrg_localization_get_locale(loc, code);

        g_message("[ ] %s (%s)",
                  lrg_locale_get_name(locale),
                  code);
    }
}
```

## Integration with Other Modules

The I18N module integrates with:
- **UI Module**: Display translated strings in dialogs and menus
- **Data Loader**: Load locale files from YAML
- **Dialog System**: Localize dialog text
- **Quest System**: Translate quest descriptions
- **Inventory**: Localize item names and descriptions

## See Also

- [LrgLocale Documentation](locale.md)
- [LrgLocalization Documentation](localization.md)
- [Implementing Saveable for Locale Persistence](../../guides/implementing-saveable.md)
