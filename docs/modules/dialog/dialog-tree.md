# LrgDialogTree - Dialog Tree Container

A complete dialogue tree containing interconnected dialog nodes. Manages the structure of a conversation and provides node lookup and validation.

## Type

- **Final GObject Class** - Cannot be subclassed
- **Type ID** - `LRG_TYPE_DIALOG_TREE`
- **GIR Name** - `Libregnum.DialogTree`

## Construction

### lrg_dialog_tree_new()
```c
LrgDialogTree * lrg_dialog_tree_new (const gchar *id);
```

Creates a new dialog tree.

**Parameters:**
- `id` - Unique identifier for the tree

**Returns:** A new `LrgDialogTree` (transfer full)

## Identification

### lrg_dialog_tree_get_id()
```c
const gchar * lrg_dialog_tree_get_id (LrgDialogTree *self);
```

Gets the tree identifier.

**Parameters:**
- `self` - An `LrgDialogTree`

**Returns:** The tree ID (transfer none)

## Starting Point

### lrg_dialog_tree_get_start_node_id()
```c
const gchar * lrg_dialog_tree_get_start_node_id (LrgDialogTree *self);
```

Gets the starting node ID.

**Parameters:**
- `self` - An `LrgDialogTree`

**Returns:** The start node ID (transfer none, nullable)

### lrg_dialog_tree_set_start_node_id()
```c
void lrg_dialog_tree_set_start_node_id (LrgDialogTree *self,
                                        const gchar   *start_node_id);
```

Sets the starting node ID.

**Parameters:**
- `self` - An `LrgDialogTree`
- `start_node_id` - Start node ID (nullable)

## Node Management

### lrg_dialog_tree_add_node()
```c
void lrg_dialog_tree_add_node (LrgDialogTree *self,
                               LrgDialogNode *node);
```

Adds a node to the tree. If a node with the same ID already exists, it will be replaced.

**Parameters:**
- `self` - An `LrgDialogTree`
- `node` - Node to add (transfer full)

### lrg_dialog_tree_get_node()
```c
LrgDialogNode * lrg_dialog_tree_get_node (LrgDialogTree *self,
                                          const gchar   *node_id);
```

Gets a node by ID.

**Parameters:**
- `self` - An `LrgDialogTree`
- `node_id` - Node identifier

**Returns:** The node (transfer none, nullable) - `NULL` if not found

### lrg_dialog_tree_get_start_node()
```c
LrgDialogNode * lrg_dialog_tree_get_start_node (LrgDialogTree *self);
```

Gets the starting node.

**Parameters:**
- `self` - An `LrgDialogTree`

**Returns:** The start node (transfer none, nullable) - `NULL` if not set

### lrg_dialog_tree_remove_node()
```c
gboolean lrg_dialog_tree_remove_node (LrgDialogTree *self,
                                      const gchar   *node_id);
```

Removes a node from the tree.

**Parameters:**
- `self` - An `LrgDialogTree`
- `node_id` - Node identifier

**Returns:** `TRUE` if the node was removed

### lrg_dialog_tree_get_node_count()
```c
guint lrg_dialog_tree_get_node_count (LrgDialogTree *self);
```

Gets the number of nodes in the tree.

**Parameters:**
- `self` - An `LrgDialogTree`

**Returns:** Node count

### lrg_dialog_tree_get_node_ids()
```c
GList * lrg_dialog_tree_get_node_ids (LrgDialogTree *self);
```

Gets all node IDs in the tree.

**Parameters:**
- `self` - An `LrgDialogTree`

**Returns:** List of node IDs (element-type utf8, transfer container)

## Metadata

### lrg_dialog_tree_get_title()
```c
const gchar * lrg_dialog_tree_get_title (LrgDialogTree *self);
```

Gets the tree title.

**Parameters:**
- `self` - An `LrgDialogTree`

**Returns:** The title (transfer none, nullable)

### lrg_dialog_tree_set_title()
```c
void lrg_dialog_tree_set_title (LrgDialogTree *self,
                                const gchar   *title);
```

Sets the tree title.

**Parameters:**
- `self` - An `LrgDialogTree`
- `title` - Tree title (nullable)

### lrg_dialog_tree_get_description()
```c
const gchar * lrg_dialog_tree_get_description (LrgDialogTree *self);
```

Gets the tree description.

**Parameters:**
- `self` - An `LrgDialogTree`

**Returns:** The description (transfer none, nullable)

### lrg_dialog_tree_set_description()
```c
void lrg_dialog_tree_set_description (LrgDialogTree *self,
                                      const gchar   *description);
```

Sets the tree description.

**Parameters:**
- `self` - An `LrgDialogTree`
- `description` - Tree description (nullable)

## Validation

### lrg_dialog_tree_validate()
```c
gboolean lrg_dialog_tree_validate (LrgDialogTree  *self,
                                   GError        **error);
```

Validates the dialog tree structure. Checks that all node references are valid and there are no orphan nodes.

**Parameters:**
- `self` - An `LrgDialogTree`
- `error` - (optional) Return location for error

**Returns:** `TRUE` if the tree is valid

## Example

```c
/* Create a dialogue tree */
g_autoptr(LrgDialogTree) tree = lrg_dialog_tree_new("merchant_greeting");
lrg_dialog_tree_set_title(tree, "Merchant Greeting");
lrg_dialog_tree_set_description(tree, "Initial conversation with the town merchant");
lrg_dialog_tree_set_start_node_id(tree, "greeting");

/* Create and add nodes */
g_autoptr(LrgDialogNode) greeting = lrg_dialog_node_new("greeting");
lrg_dialog_node_set_speaker(greeting, "Merchant");
lrg_dialog_node_set_text(greeting, "Welcome to my shop!");
lrg_dialog_node_set_next_node_id(greeting, "browse");
lrg_dialog_tree_add_node(tree, greeting);

g_autoptr(LrgDialogNode) browse = lrg_dialog_node_new("browse");
lrg_dialog_node_set_speaker(browse, "Merchant");
lrg_dialog_node_set_text(browse, "Feel free to look around.");
g_autoptr(LrgDialogResponse) shop = lrg_dialog_response_new(
    "shop", "Show me your wares", "shopping"
);
lrg_dialog_node_add_response(browse, shop);
lrg_dialog_tree_add_node(tree, browse);

g_autoptr(LrgDialogNode) shopping = lrg_dialog_node_new("shopping");
lrg_dialog_node_set_speaker(shopping, "Merchant");
lrg_dialog_node_set_text(shopping, "Here's what I have...");
lrg_dialog_tree_add_node(tree, shopping);

/* Validate tree */
g_autoptr(GError) error = NULL;
if (!lrg_dialog_tree_validate(tree, &error)) {
    g_warning("Tree validation failed: %s\n", error->message);
}

/* Get node count */
g_print("Tree has %u nodes\n", lrg_dialog_tree_get_node_count(tree));

/* Get node IDs */
GList *ids = lrg_dialog_tree_get_node_ids(tree);
for (GList *l = ids; l != NULL; l = l->next) {
    g_print("Node: %s\n", (const gchar *)l->data);
}
g_list_free(ids);

/* Get start node */
LrgDialogNode *start = lrg_dialog_tree_get_start_node(tree);
if (start) {
    g_print("Start node: %s\n", lrg_dialog_node_get_text(start));
}

/* Retrieve node by ID */
LrgDialogNode *shopping_node = lrg_dialog_tree_get_node(tree, "shopping");
if (shopping_node) {
    g_print("Found shopping node\n");
}
```
