# DLC Store Demo

## Overview

The `game-dlc-store` example demonstrates a complete DLC management system including discovery, ownership verification, content gating, trial support, and store integration.

## Running the Example

```bash
cd examples
make game-dlc-store

# Run the demo
./build/release/examples/game-dlc-store
```

## Features Demonstrated

| Feature | Description |
|---------|-------------|
| DLC Discovery | Using `LrgModManager` to find DLCs via manifest files |
| Ownership Verification | `LrgDlcOwnershipManifest` for simulating purchase state |
| Content Gating | Checking if content is accessible (owned/trial/locked) |
| Trial Support | Partial content access for unowned DLCs |
| Store Integration | Opening store pages for purchasing DLCs |
| Type Filtering | Querying DLCs by type (expansion, cosmetic, character, etc.) |

## Controls

| Key | Action |
|-----|--------|
| UP/DOWN | Navigate DLC list |
| ENTER | Toggle ownership (simulate purchase/refund) |
| SPACE | Open store page (prints URL) |
| T | Toggle trial content view |
| V | Verify all DLC ownership |
| 0 | Show all DLCs |
| 1-6 | Filter by DLC type |
| ESC | Exit |

## Sample DLC Manifests

The example uses sample DLC manifests in `examples/data/dlcs/`:

### Expansion Pack (`frost-realm-expansion/mod.yaml`)

```yaml
id: frost-realm-expansion
name: "Frost Realm Expansion"
version: "1.0.0"
type: content
author: "Demo Games"
description: "Explore the frozen Frost Realm!"

dlc:
  type: expansion
  steam_app_id: 12346
  store_id: "frost-realm"
  price: "$14.99"
  currency: "USD"
  min_game_version: "1.0.0"
  release_date: "2025-01-15"

  ownership:
    method: manifest

  trial:
    enabled: true
    content_ids:
      - "frost-intro-quest"
      - "frost-preview-area"
```

### Character Pack (`character-rogue-class/mod.yaml`)

```yaml
id: character-rogue-class
name: "Rogue Class"
version: "1.0.0"
type: content
author: "Demo Games"
description: "Unlock the Rogue class!"

dlc:
  type: character
  steam_app_id: 12349
  store_id: "rogue-class"
  price: "$9.99"
  currency: "USD"
  min_game_version: "1.1.0"

  ownership:
    method: manifest

  trial:
    enabled: true
    content_ids:
      - "character-rogue-trial"

character:
  is_playable: true
  is_companion: false
  character_ids:
    - "character-rogue"
    - "character-rogue-trial"
```

## Key Code Patterns

### Discovering DLCs

```c
g_autoptr(LrgModManager) mod_manager = lrg_mod_manager_new ();

/* Add search path for DLC manifests */
lrg_mod_manager_add_search_path (mod_manager, "data/dlcs");

/* Discover all mods/DLCs */
g_autoptr(GError) error = NULL;
if (!lrg_mod_manager_discover (mod_manager, &error))
{
    g_warning ("Discovery failed: %s", error->message);
    return;
}

/* Get discovered DLCs */
GPtrArray *mods = lrg_mod_manager_get_mods (mod_manager);
for (guint i = 0; i < mods->len; i++)
{
    LrgMod *mod = g_ptr_array_index (mods, i);
    LrgDlc *dlc = lrg_mod_get_dlc (mod);

    if (dlc != NULL)
    {
        /* This mod is a DLC */
        g_print ("Found DLC: %s\n", lrg_dlc_get_name (dlc));
    }
}
```

### Checking Ownership

```c
/* Create ownership manifest (simulates Steam/platform ownership) */
g_autoptr(LrgDlcOwnershipManifest) ownership = lrg_dlc_ownership_manifest_new ();

/* Set ownership state for a DLC */
lrg_dlc_ownership_manifest_set_owned (ownership, "frost-realm-expansion", TRUE);

/* Check if content is accessible */
LrgDlcOwnershipState state = lrg_dlc_get_ownership_state (dlc);
switch (state)
{
case LRG_DLC_OWNERSHIP_OWNED:
    /* Full access */
    break;
case LRG_DLC_OWNERSHIP_TRIAL:
    /* Trial content only */
    break;
case LRG_DLC_OWNERSHIP_NOT_OWNED:
    /* Locked - show purchase prompt */
    break;
}
```

### Listening for Ownership Changes

```c
static void
on_ownership_changed (LrgDlc              *dlc,
                      LrgDlcOwnershipState state,
                      gpointer             user_data)
{
    g_print ("DLC '%s' ownership changed to: %s\n",
             lrg_dlc_get_name (dlc),
             state == LRG_DLC_OWNERSHIP_OWNED ? "OWNED" : "NOT OWNED");
}

/* Connect signal */
g_signal_connect (dlc, "ownership-changed",
                  G_CALLBACK (on_ownership_changed), NULL);
```

### Filtering by DLC Type

```c
/* Get only expansion packs */
for (guint i = 0; i < all_dlcs->len; i++)
{
    LrgDlc *dlc = g_ptr_array_index (all_dlcs, i);

    if (lrg_dlc_get_dlc_type (dlc) == LRG_DLC_TYPE_EXPANSION)
    {
        /* This is an expansion */
    }
}
```

### Opening Store Page

```c
/* Get store URL */
const gchar *store_url = lrg_dlc_get_store_url (dlc);
if (store_url != NULL)
{
    /* Open in browser or in-game overlay */
    g_print ("Store: %s\n", store_url);
}
```

## DLC Types

| Type | Enum Value | Description |
|------|------------|-------------|
| Expansion | `LRG_DLC_TYPE_EXPANSION` | Large content packs with new areas/storylines |
| Cosmetic | `LRG_DLC_TYPE_COSMETIC` | Visual customizations (skins, themes) |
| Quest | `LRG_DLC_TYPE_QUEST` | Additional missions/quests |
| Item | `LRG_DLC_TYPE_ITEM` | Weapons, armor, consumables |
| Character | `LRG_DLC_TYPE_CHARACTER` | Playable characters or classes |
| Map | `LRG_DLC_TYPE_MAP` | Additional levels/maps |

## Ownership States

| State | Enum Value | Description |
|-------|------------|-------------|
| Owned | `LRG_DLC_OWNERSHIP_OWNED` | Full access to all content |
| Not Owned | `LRG_DLC_OWNERSHIP_NOT_OWNED` | Content locked |
| Trial | `LRG_DLC_OWNERSHIP_TRIAL` | Access to trial content only |
| Unknown | `LRG_DLC_OWNERSHIP_UNKNOWN` | Ownership not yet verified |
| Error | `LRG_DLC_OWNERSHIP_ERROR` | Verification failed |

## File Structure

```
examples/
├── game-dlc-store.c              # Main example source
└── data/
    └── dlcs/
        ├── frost-realm-expansion/
        │   └── mod.yaml
        ├── shadow-skins-pack/
        │   └── mod.yaml
        ├── ancient-quests-pack/
        │   └── mod.yaml
        ├── legendary-weapons/
        │   └── mod.yaml
        ├── character-rogue-class/
        │   └── mod.yaml
        └── map-desert-arena/
            └── mod.yaml
```

## See Also

- [Native DLC Example](native-dlc.md) - Loading native GModule DLCs
- [Modding Guide](modding-guide.md) - Creating content mods
- [LrgModManager](../modules/mod/mod-manager.md) - Mod management API
- [LrgModManifest](../modules/mod/mod-manifest.md) - Manifest format reference
