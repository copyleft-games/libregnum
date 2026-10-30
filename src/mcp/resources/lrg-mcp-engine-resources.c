/* lrg-mcp-engine-resources.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * MCP resource group for engine state.
 */

#include "lrg-mcp-engine-resources.h"
#include "../../core/lrg-engine.h"
#include "../../core/lrg-registry.h"
#include "../../core/lrg-data-loader.h"
#include "../../core/lrg-asset-manager.h"
#include "../../graphics/lrg-window.h"
#include "../../ecs/lrg-game-object.h"
#include "../../ecs/lrg-component.h"
#include "../../settings/lrg-settings.h"
#include "../../settings/lrg-settings-group.h"
#include "../../lrg-log.h"
#include "../../lrg-version.h"
#include <gio/gio.h>
#include <json-glib/json-glib.h>
#include <mcp.h>

/**
 * SECTION:lrg-mcp-engine-resources
 * @title: LrgMcpEngineResources
 * @short_description: MCP resources for engine state
 *
 * #LrgMcpEngineResources provides MCP resources for read-only
 * access to engine state, configuration, and the type registry:
 *
 * - `libregnum://engine/info`: lifecycle state and version.
 * - `libregnum://engine/config`: version, owned subsystems (window,
 *   renderer, scripting), registered worlds, data loader and asset
 *   manager setup, and the default #LrgSettings groups.
 * - `libregnum://engine/registry`: every registered type name with its
 *   GType, parent, kind, abstract flag and properties.
 */

#define URI_PREFIX "libregnum://engine/"

struct _LrgMcpEngineResources
{
	LrgMcpResourceGroup parent_instance;
};

G_DEFINE_FINAL_TYPE (LrgMcpEngineResources, lrg_mcp_engine_resources, LRG_TYPE_MCP_RESOURCE_GROUP)

/* ==========================================================================
 * Helpers
 * ========================================================================== */

/*
 * engine_state_name:
 *
 * The lowercase name used for an #LrgEngineState in every resource.
 */
static const gchar *
engine_state_name (LrgEngineState state)
{
	switch (state)
	{
	case LRG_ENGINE_STATE_UNINITIALIZED:
		return "uninitialized";
	case LRG_ENGINE_STATE_INITIALIZING:
		return "initializing";
	case LRG_ENGINE_STATE_RUNNING:
		return "running";
	case LRG_ENGINE_STATE_PAUSED:
		return "paused";
	case LRG_ENGINE_STATE_SHUTTING_DOWN:
		return "shutting_down";
	case LRG_ENGINE_STATE_TERMINATED:
		return "terminated";
	default:
		return "unknown";
	}
}

static gint
compare_strings (gconstpointer a,
                         gconstpointer b)
{
	return g_strcmp0 (*(const gchar * const *)a, *(const gchar * const *)b);
}

/* ==========================================================================
 * Resource Handlers
 * ========================================================================== */

static GList *
read_engine_info (LrgMcpEngineResources  *self,
                  GError                **error)
{
	LrgEngine *engine;
	const gchar *state_str;
	g_autoptr(JsonBuilder) builder = NULL;
	g_autoptr(JsonGenerator) generator = NULL;
	g_autoptr(JsonNode) root = NULL;
	g_autofree gchar *json_str = NULL;
	McpResourceContents *contents;

	engine = lrg_engine_get_default ();
	if (engine == NULL)
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
		             "Engine not available");
		return NULL;
	}

	state_str = engine_state_name (lrg_engine_get_state (engine));

	builder = json_builder_new ();
	json_builder_begin_object (builder);

	json_builder_set_member_name (builder, "state");
	json_builder_add_string_value (builder, state_str);

	json_builder_set_member_name (builder, "running");
	json_builder_add_boolean_value (builder, lrg_engine_is_running (engine));

	json_builder_set_member_name (builder, "version");
	json_builder_add_string_value (builder, LRG_VERSION_STRING);

	json_builder_end_object (builder);

	root = json_builder_get_root (builder);
	generator = json_generator_new ();
	json_generator_set_root (generator, root);
	json_generator_set_pretty (generator, TRUE);
	json_str = json_generator_to_data (generator, NULL);

	contents = mcp_resource_contents_new_text (URI_PREFIX "info", json_str, "application/json");
	return g_list_append (NULL, contents);
}

/*
 * json_contents:
 *
 * Wraps @json (taking ownership) as one application/json resource
 * contents entry for @uri.
 */
static GList *
json_contents (const gchar *uri,
               JsonObject  *json)
{
	g_autoptr(JsonNode) root = json_node_new (JSON_NODE_OBJECT);
	g_autofree gchar *text = NULL;

	json_node_take_object (root, json);
	text = json_to_string (root, TRUE);
	return g_list_append (NULL, mcp_resource_contents_new_text (uri, text, "application/json"));
}

/*
 * string_list_json:
 *
 * Copies a GList of strings into a sorted JSON array (the list is freed,
 * not its strings, unless @free_data is set).
 */
static JsonArray *
string_list_json (GList    *list,
                  gboolean  free_data)
{
	JsonArray *array = json_array_new ();
	GList *iter;

	list = g_list_sort (list, (GCompareFunc)g_strcmp0);
	for (iter = list; iter != NULL; iter = iter->next)
		json_array_add_string_element (array, iter->data);
	if (free_data)
		g_list_free_full (list, g_free);
	else
		g_list_free (list);
	return array;
}

/*
 * settings_json:
 *
 * Every group of the default #LrgSettings, serialized through the
 * group's own GVariant serializer.
 */
static JsonObject *
settings_json (void)
{
	LrgSettings *settings = lrg_settings_get_default ();
	JsonObject *json = json_object_new ();
	g_autoptr(GPtrArray) groups = lrg_settings_list_groups (settings);
	guint i;

	g_ptr_array_sort (groups, (GCompareFunc)compare_strings);
	for (i = 0; i < groups->len; i++)
	{
		const gchar *name = g_ptr_array_index (groups, i);
		LrgSettingsGroup *group = lrg_settings_get_group (settings, name);
		g_autoptr(GVariant) variant = NULL;
		g_autoptr(GError) error = NULL;

		if (group == NULL)
			continue;
		variant = lrg_settings_group_serialize (group, &error);
		if (variant == NULL)
		{
			json_object_set_null_member (json, name);
			continue;
		}
		g_variant_ref_sink (variant);
		json_object_set_member (json, name, json_gvariant_serialize (variant));
	}
	return json;
}

/*
 * read_engine_config:
 *
 * The engine's live configuration: version, lifecycle, owned subsystems
 * (window, renderer, scripting), data loader and asset manager setup,
 * registered worlds and the default settings groups.
 */
static GList *
read_engine_config (LrgMcpEngineResources  *self,
                    GError                **error)
{
	LrgEngine *engine;
	LrgRegistry *registry;
	LrgDataLoader *loader;
	LrgAssetManager *assets;
	LrgWindow *window;
	LrgScripting *scripting;
	JsonObject *json;
	gboolean pin_cores = FALSE;

	engine = lrg_engine_get_default ();
	if (engine == NULL)
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
		             "Engine not available");
		return NULL;
	}

	json = json_object_new ();

	/* Version and lifecycle */
	json_object_set_string_member (json, "version", LRG_VERSION_STRING);
	json_object_set_int_member (json, "api_version", LRG_API_VERSION);
	json_object_set_string_member (json, "state", engine_state_name (lrg_engine_get_state (engine)));
	g_object_get (engine, "pin-performance-cores", &pin_cores, NULL);
	json_object_set_boolean_member (json, "pin_performance_cores", pin_cores);

	/* Owned subsystems */
	window = lrg_engine_get_window (engine);
	if (window != NULL)
	{
		JsonObject *win = json_object_new ();
		const gchar *title = lrg_window_get_title (window);

		json_object_set_string_member (win, "type", G_OBJECT_TYPE_NAME (window));
		json_object_set_string_member (win, "title", title != NULL ? title : "");
		json_object_set_int_member (win, "width", lrg_window_get_width (window));
		json_object_set_int_member (win, "height", lrg_window_get_height (window));
		json_object_set_int_member (win, "target_fps", lrg_window_get_target_fps (window));
		json_object_set_object_member (json, "window", win);
	}
	else
		json_object_set_null_member (json, "window");
	json_object_set_boolean_member (json, "renderer", lrg_engine_get_renderer (engine) != NULL);
	scripting = lrg_engine_get_scripting (engine);
	if (scripting != NULL)
		json_object_set_string_member (json, "scripting", G_OBJECT_TYPE_NAME (scripting));
	else
		json_object_set_null_member (json, "scripting");
	json_object_set_array_member (json, "worlds",
	                              string_list_json (lrg_engine_list_worlds (engine), TRUE));

	/* Data loading (only present between startup and shutdown) */
	registry = lrg_engine_get_registry (engine);
	json_object_set_int_member (json, "registry_count",
	                            registry != NULL ? lrg_registry_get_count (registry) : 0);
	loader = lrg_engine_get_data_loader (engine);
	if (loader != NULL)
	{
		JsonObject *data = json_object_new ();
		JsonArray *extensions = json_array_new ();
		const gchar * const *exts = lrg_data_loader_get_file_extensions (loader);
		guint i;

		for (i = 0; exts != NULL && exts[i] != NULL; i++)
			json_array_add_string_element (extensions, exts[i]);
		json_object_set_string_member (data, "type_field_name",
		                               lrg_data_loader_get_type_field_name (loader));
		json_object_set_array_member (data, "file_extensions", extensions);
		json_object_set_object_member (json, "data_loader", data);
	}
	else
		json_object_set_null_member (json, "data_loader");

	assets = lrg_engine_get_asset_manager (engine);
	if (assets != NULL)
	{
		JsonObject *asset = json_object_new ();
		JsonObject *caches = json_object_new ();
		JsonArray *paths = json_array_new ();
		const GPtrArray *search = lrg_asset_manager_get_search_paths (assets);
		guint i;

		for (i = 0; search != NULL && i < search->len; i++)
			json_array_add_string_element (paths, g_ptr_array_index (search, i));
		json_object_set_int_member (caches, "textures", lrg_asset_manager_get_texture_cache_size (assets));
		json_object_set_int_member (caches, "fonts", lrg_asset_manager_get_font_cache_size (assets));
		json_object_set_int_member (caches, "sounds", lrg_asset_manager_get_sound_cache_size (assets));
		json_object_set_int_member (caches, "music", lrg_asset_manager_get_music_cache_size (assets));
		json_object_set_int_member (caches, "models", lrg_asset_manager_get_model_cache_size (assets));
		json_object_set_int_member (caches, "animations", lrg_asset_manager_get_animation_cache_size (assets));
		json_object_set_array_member (asset, "search_paths", paths);
		json_object_set_object_member (asset, "cache_sizes", caches);
		json_object_set_object_member (json, "asset_manager", asset);
	}
	else
		json_object_set_null_member (json, "asset_manager");

	/* User-facing settings */
	json_object_set_object_member (json, "settings", settings_json ());

	return json_contents (URI_PREFIX "config", json);
}

/*
 * RegistryEntry:
 *
 * A registry row collected by lrg_registry_foreach() for sorting.
 */
typedef struct
{
	gchar *name;
	GType  type;
} RegistryEntry;

static void
registry_entry_free (gpointer data)
{
	RegistryEntry *entry = data;

	g_free (entry->name);
	g_free (entry);
}

static void
collect_registry_entry (const gchar *name,
                        GType        type,
                        gpointer     user_data)
{
	RegistryEntry *entry = g_new0 (RegistryEntry, 1);

	entry->name = g_strdup (name);
	entry->type = type;
	g_ptr_array_add (user_data, entry);
}

static gint
compare_registry_entries (gconstpointer a,
                          gconstpointer b)
{
	const RegistryEntry *ea = *(const RegistryEntry * const *)a;
	const RegistryEntry *eb = *(const RegistryEntry * const *)b;

	return g_strcmp0 (ea->name, eb->name);
}

/*
 * type_properties_json:
 *
 * The properties declared by @type (and its ancestors) with their value
 * type and readable/writable/construct-only flags. Only GObject types
 * have properties; anything else yields an empty array.
 */
static JsonArray *
type_properties_json (GType type)
{
	JsonArray *array = json_array_new ();
	GObjectClass *klass;
	g_autofree GParamSpec **specs = NULL;
	guint count;
	guint i;

	if (!G_TYPE_IS_OBJECT (type))
		return array;

	klass = g_type_class_ref (type);
	specs = g_object_class_list_properties (klass, &count);
	for (i = 0; i < count; i++)
	{
		JsonObject *property = json_object_new ();

		json_object_set_string_member (property, "name", specs[i]->name);
		json_object_set_string_member (property, "type", g_type_name (specs[i]->value_type));
		json_object_set_string_member (property, "owner", g_type_name (specs[i]->owner_type));
		json_object_set_boolean_member (property, "readable", (specs[i]->flags & G_PARAM_READABLE) != 0);
		json_object_set_boolean_member (property, "writable", (specs[i]->flags & G_PARAM_WRITABLE) != 0);
		json_object_set_boolean_member (property, "construct_only",
		                                (specs[i]->flags & G_PARAM_CONSTRUCT_ONLY) != 0);
		json_array_add_object_element (array, property);
	}
	g_type_class_unref (klass);
	return array;
}

/*
 * read_engine_registry:
 *
 * Every registered name (sorted) with its GType, parent type, kind
 * (game object, component or other), abstract flag and properties.
 */
static GList *
read_engine_registry (LrgMcpEngineResources  *self,
                      GError                **error)
{
	LrgEngine *engine;
	LrgRegistry *registry;
	g_autoptr(GPtrArray) entries = NULL;
	JsonObject *json;
	JsonArray *types;
	guint i;

	engine = lrg_engine_get_default ();
	if (engine == NULL)
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
		             "Engine not available");
		return NULL;
	}
	registry = lrg_engine_get_registry (engine);
	if (registry == NULL)
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_INITIALIZED,
		             "Engine registry is not initialized (engine not started)");
		return NULL;
	}

	entries = g_ptr_array_new_with_free_func (registry_entry_free);
	lrg_registry_foreach (registry, collect_registry_entry, entries);
	g_ptr_array_sort (entries, compare_registry_entries);

	types = json_array_new ();
	for (i = 0; i < entries->len; i++)
	{
		const RegistryEntry *entry = g_ptr_array_index (entries, i);
		JsonObject *row = json_object_new ();
		const gchar *kind;

		if (g_type_is_a (entry->type, LRG_TYPE_GAME_OBJECT))
			kind = "game-object";
		else if (g_type_is_a (entry->type, LRG_TYPE_COMPONENT))
			kind = "component";
		else
			kind = "object";

		json_object_set_string_member (row, "name", entry->name);
		json_object_set_string_member (row, "gtype", g_type_name (entry->type));
		json_object_set_string_member (row, "parent", g_type_name (g_type_parent (entry->type)));
		json_object_set_string_member (row, "kind", kind);
		json_object_set_boolean_member (row, "abstract", G_TYPE_IS_ABSTRACT (entry->type));
		json_object_set_array_member (row, "properties", type_properties_json (entry->type));
		json_array_add_object_element (types, row);
	}

	json = json_object_new ();
	json_object_set_int_member (json, "count", entries->len);
	json_object_set_array_member (json, "types", types);
	return json_contents (URI_PREFIX "registry", json);
}

/* ==========================================================================
 * LrgMcpResourceGroup Virtual Methods
 * ========================================================================== */

static const gchar *
lrg_mcp_engine_resources_get_group_name (LrgMcpResourceGroup *group)
{
	return "engine";
}

static void
lrg_mcp_engine_resources_register_resources (LrgMcpResourceGroup *group)
{
	McpResource *resource;

	lrg_mcp_resource_group_set_uri_prefix (group, URI_PREFIX);

	resource = mcp_resource_new (URI_PREFIX "info", "Engine state information");
	mcp_resource_set_mime_type (resource, "application/json");
	lrg_mcp_resource_group_add_resource (group, resource);

	resource = mcp_resource_new (URI_PREFIX "config", "Engine configuration, subsystems and settings");
	mcp_resource_set_mime_type (resource, "application/json");
	lrg_mcp_resource_group_add_resource (group, resource);

	resource = mcp_resource_new (URI_PREFIX "registry", "Registered type names, GTypes and properties");
	mcp_resource_set_mime_type (resource, "application/json");
	lrg_mcp_resource_group_add_resource (group, resource);
}

static GList *
lrg_mcp_engine_resources_read_resource (LrgMcpResourceGroup  *group,
                                        const gchar          *uri,
                                        GError              **error)
{
	LrgMcpEngineResources *self = LRG_MCP_ENGINE_RESOURCES (group);

	if (g_strcmp0 (uri, URI_PREFIX "info") == 0)
		return read_engine_info (self, error);
	if (g_strcmp0 (uri, URI_PREFIX "config") == 0)
		return read_engine_config (self, error);
	if (g_strcmp0 (uri, URI_PREFIX "registry") == 0)
		return read_engine_registry (self, error);

	g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
	             "Unknown resource: %s", uri);
	return NULL;
}

/* ==========================================================================
 * GObject Implementation
 * ========================================================================== */

static void
lrg_mcp_engine_resources_class_init (LrgMcpEngineResourcesClass *klass)
{
	LrgMcpResourceGroupClass *group_class = LRG_MCP_RESOURCE_GROUP_CLASS (klass);

	group_class->get_group_name = lrg_mcp_engine_resources_get_group_name;
	group_class->register_resources = lrg_mcp_engine_resources_register_resources;
	group_class->read_resource = lrg_mcp_engine_resources_read_resource;
}

static void
lrg_mcp_engine_resources_init (LrgMcpEngineResources *self)
{
	/* Nothing to initialize */
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

/*
 * lrg_mcp_engine_resources_new:
 *
 * Creates a new engine resources provider.
 *
 * Returns: (transfer full): A new #LrgMcpEngineResources
 */
LrgMcpEngineResources *
lrg_mcp_engine_resources_new (void)
{
	return g_object_new (LRG_TYPE_MCP_ENGINE_RESOURCES, NULL);
}
