/* test-ecs.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Unit tests for ECS module (Component, GameObject, World).
 */

#include <glib.h>
#include <libregnum.h>

/* ==========================================================================
 * Mock Component for Testing
 *
 * A concrete component implementation for testing the abstract base class.
 * ========================================================================== */

#define MOCK_TYPE_COMPONENT (mock_component_get_type ())
G_DECLARE_FINAL_TYPE (MockComponent, mock_component, MOCK, COMPONENT, LrgComponent)

struct _MockComponent
{
    LrgComponent parent_instance;

    guint    update_count;
    gboolean attached_called;
    gboolean detached_called;
    gfloat   last_delta;
};

G_DEFINE_TYPE (MockComponent, mock_component, LRG_TYPE_COMPONENT)

static void
mock_component_attached (LrgComponent  *component,
                         LrgGameObject *owner)
{
    MockComponent *self = MOCK_COMPONENT (component);
    self->attached_called = TRUE;
}

static void
mock_component_detached (LrgComponent *component)
{
    MockComponent *self = MOCK_COMPONENT (component);
    self->detached_called = TRUE;
}

static void
mock_component_update (LrgComponent *component,
                       gfloat        delta)
{
    MockComponent *self = MOCK_COMPONENT (component);
    self->update_count++;
    self->last_delta = delta;
}

static void
mock_component_class_init (MockComponentClass *klass)
{
    LrgComponentClass *component_class = LRG_COMPONENT_CLASS (klass);

    component_class->attached = mock_component_attached;
    component_class->detached = mock_component_detached;
    component_class->update = mock_component_update;
}

static void
mock_component_init (MockComponent *self)
{
    self->update_count = 0;
    self->attached_called = FALSE;
    self->detached_called = FALSE;
    self->last_delta = 0.0f;
}

static MockComponent *
mock_component_new (void)
{
    return g_object_new (MOCK_TYPE_COMPONENT, NULL);
}

/* ==========================================================================
 * Second Mock Component (for testing multiple component types)
 * ========================================================================== */

#define MOCK_TYPE_COMPONENT2 (mock_component2_get_type ())
G_DECLARE_FINAL_TYPE (MockComponent2, mock_component2, MOCK, COMPONENT2, LrgComponent)

struct _MockComponent2
{
    LrgComponent parent_instance;
    gint         value;
};

G_DEFINE_TYPE (MockComponent2, mock_component2, LRG_TYPE_COMPONENT)

static void
mock_component2_class_init (MockComponent2Class *klass)
{
    /* No overrides needed */
}

static void
mock_component2_init (MockComponent2 *self)
{
    self->value = 0;
}

static MockComponent2 *
mock_component2_new (void)
{
    return g_object_new (MOCK_TYPE_COMPONENT2, NULL);
}

/* ==========================================================================
 * Test Fixtures - Component
 * ========================================================================== */

typedef struct
{
    MockComponent *component;
} ComponentFixture;

static void
component_fixture_set_up (ComponentFixture *fixture,
                          gconstpointer     user_data)
{
    fixture->component = mock_component_new ();
    g_assert_nonnull (fixture->component);
}

static void
component_fixture_tear_down (ComponentFixture *fixture,
                             gconstpointer     user_data)
{
    g_clear_object (&fixture->component);
}

/* ==========================================================================
 * Test Fixtures - GameObject
 * ========================================================================== */

typedef struct
{
    LrgGameObject *object;
} GameObjectFixture;

static void
game_object_fixture_set_up (GameObjectFixture *fixture,
                            gconstpointer      user_data)
{
    fixture->object = lrg_game_object_new ();
    g_assert_nonnull (fixture->object);
}

static void
game_object_fixture_tear_down (GameObjectFixture *fixture,
                               gconstpointer      user_data)
{
    g_clear_object (&fixture->object);
}

/* ==========================================================================
 * Test Fixtures - World
 * ========================================================================== */

typedef struct
{
    LrgWorld *world;
} WorldFixture;

static void
world_fixture_set_up (WorldFixture  *fixture,
                      gconstpointer  user_data)
{
    fixture->world = lrg_world_new ();
    g_assert_nonnull (fixture->world);
}

static void
world_fixture_tear_down (WorldFixture  *fixture,
                         gconstpointer  user_data)
{
    g_clear_object (&fixture->world);
}

/* ==========================================================================
 * Test Cases - Component
 * ========================================================================== */

static void
test_component_new (void)
{
    g_autoptr(MockComponent) component = NULL;

    component = mock_component_new ();

    g_assert_nonnull (component);
    g_assert_true (LRG_IS_COMPONENT (component));
    g_assert_null (lrg_component_get_owner (LRG_COMPONENT (component)));
    g_assert_true (lrg_component_get_enabled (LRG_COMPONENT (component)));
}

static void
test_component_enabled (ComponentFixture *fixture,
                        gconstpointer     user_data)
{
    LrgComponent *component = LRG_COMPONENT (fixture->component);

    /* Default is enabled */
    g_assert_true (lrg_component_get_enabled (component));

    /* Disable */
    lrg_component_set_enabled (component, FALSE);
    g_assert_false (lrg_component_get_enabled (component));

    /* Re-enable */
    lrg_component_set_enabled (component, TRUE);
    g_assert_true (lrg_component_get_enabled (component));
}

static void
test_component_update_when_enabled (ComponentFixture *fixture,
                                    gconstpointer     user_data)
{
    LrgComponent *component = LRG_COMPONENT (fixture->component);

    g_assert_cmpuint (fixture->component->update_count, ==, 0);

    lrg_component_update (component, 0.016f);
    g_assert_cmpuint (fixture->component->update_count, ==, 1);
    g_assert_cmpfloat_with_epsilon (fixture->component->last_delta, 0.016f, 0.0001f);

    lrg_component_update (component, 0.033f);
    g_assert_cmpuint (fixture->component->update_count, ==, 2);
}

static void
test_component_update_when_disabled (ComponentFixture *fixture,
                                     gconstpointer     user_data)
{
    LrgComponent *component = LRG_COMPONENT (fixture->component);

    lrg_component_set_enabled (component, FALSE);

    lrg_component_update (component, 0.016f);
    g_assert_cmpuint (fixture->component->update_count, ==, 0);
}

/* ==========================================================================
 * Test Cases - GameObject
 * ========================================================================== */

static void
test_game_object_new (void)
{
    g_autoptr(LrgGameObject) object = NULL;

    object = lrg_game_object_new ();

    g_assert_nonnull (object);
    g_assert_true (LRG_IS_GAME_OBJECT (object));
    g_assert_true (GRL_IS_ENTITY (object));
    g_assert_cmpuint (lrg_game_object_get_component_count (object), ==, 0);
}

static void
test_game_object_new_at (void)
{
    g_autoptr(LrgGameObject) object = NULL;

    object = lrg_game_object_new_at (100.0f, 200.0f);

    g_assert_nonnull (object);
    g_assert_cmpfloat_with_epsilon (grl_entity_get_x (GRL_ENTITY (object)),
                                    100.0f, 0.0001f);
    g_assert_cmpfloat_with_epsilon (grl_entity_get_y (GRL_ENTITY (object)),
                                    200.0f, 0.0001f);
}

static void
test_game_object_add_component (GameObjectFixture *fixture,
                                gconstpointer      user_data)
{
    g_autoptr(MockComponent) component = NULL;

    component = mock_component_new ();

    lrg_game_object_add_component (fixture->object, LRG_COMPONENT (component));

    g_assert_cmpuint (lrg_game_object_get_component_count (fixture->object), ==, 1);
    g_assert_true (lrg_game_object_has_component (fixture->object, MOCK_TYPE_COMPONENT));
    g_assert_true (component->attached_called);
    g_assert_true (lrg_component_get_owner (LRG_COMPONENT (component)) == fixture->object);
}

static void
test_game_object_remove_component (GameObjectFixture *fixture,
                                   gconstpointer      user_data)
{
    g_autoptr(MockComponent) component = NULL;

    component = mock_component_new ();
    g_object_ref (component); /* Keep our own reference */

    lrg_game_object_add_component (fixture->object, LRG_COMPONENT (component));
    g_assert_cmpuint (lrg_game_object_get_component_count (fixture->object), ==, 1);

    lrg_game_object_remove_component (fixture->object, LRG_COMPONENT (component));

    g_assert_cmpuint (lrg_game_object_get_component_count (fixture->object), ==, 0);
    g_assert_true (component->detached_called);
    g_assert_null (lrg_component_get_owner (LRG_COMPONENT (component)));
}

static void
test_game_object_get_component (GameObjectFixture *fixture,
                                gconstpointer      user_data)
{
    g_autoptr(MockComponent) component = NULL;
    LrgComponent            *found;

    component = mock_component_new ();
    lrg_game_object_add_component (fixture->object, LRG_COMPONENT (component));

    found = lrg_game_object_get_component (fixture->object, MOCK_TYPE_COMPONENT);
    g_assert_nonnull (found);
    g_assert_true (found == LRG_COMPONENT (component));

    /* Not found case */
    found = lrg_game_object_get_component (fixture->object, MOCK_TYPE_COMPONENT2);
    g_assert_null (found);
}

static void
test_game_object_get_components (GameObjectFixture *fixture,
                                 gconstpointer      user_data)
{
    g_autoptr(MockComponent) comp1 = NULL;
    g_autoptr(MockComponent2) comp2 = NULL;
    GList                   *components;

    comp1 = mock_component_new ();
    comp2 = mock_component2_new ();

    lrg_game_object_add_component (fixture->object, LRG_COMPONENT (comp1));
    lrg_game_object_add_component (fixture->object, LRG_COMPONENT (comp2));

    components = lrg_game_object_get_components (fixture->object);
    g_assert_nonnull (components);
    g_assert_cmpuint (g_list_length (components), ==, 2);

    g_list_free (components);
}

static void
test_game_object_has_component (GameObjectFixture *fixture,
                                gconstpointer      user_data)
{
    g_autoptr(MockComponent) component = NULL;

    component = mock_component_new ();

    g_assert_false (lrg_game_object_has_component (fixture->object, MOCK_TYPE_COMPONENT));

    lrg_game_object_add_component (fixture->object, LRG_COMPONENT (component));

    g_assert_true (lrg_game_object_has_component (fixture->object, MOCK_TYPE_COMPONENT));
    g_assert_false (lrg_game_object_has_component (fixture->object, MOCK_TYPE_COMPONENT2));
}

static void
test_game_object_multiple_components (GameObjectFixture *fixture,
                                      gconstpointer      user_data)
{
    g_autoptr(MockComponent) comp1 = NULL;
    g_autoptr(MockComponent) comp2 = NULL;
    g_autoptr(MockComponent2) comp3 = NULL;
    GList                   *same_type;

    comp1 = mock_component_new ();
    comp2 = mock_component_new ();
    comp3 = mock_component2_new ();

    lrg_game_object_add_component (fixture->object, LRG_COMPONENT (comp1));
    lrg_game_object_add_component (fixture->object, LRG_COMPONENT (comp2));
    lrg_game_object_add_component (fixture->object, LRG_COMPONENT (comp3));

    g_assert_cmpuint (lrg_game_object_get_component_count (fixture->object), ==, 3);

    /* Get all of same type */
    same_type = lrg_game_object_get_components_of_type (fixture->object, MOCK_TYPE_COMPONENT);
    g_assert_cmpuint (g_list_length (same_type), ==, 2);
    g_list_free (same_type);

    same_type = lrg_game_object_get_components_of_type (fixture->object, MOCK_TYPE_COMPONENT2);
    g_assert_cmpuint (g_list_length (same_type), ==, 1);
    g_list_free (same_type);
}

static void
test_game_object_remove_all_components (GameObjectFixture *fixture,
                                        gconstpointer      user_data)
{
    g_autoptr(MockComponent) comp1 = NULL;
    g_autoptr(MockComponent2) comp2 = NULL;

    comp1 = mock_component_new ();
    comp2 = mock_component2_new ();
    g_object_ref (comp1);
    g_object_ref (comp2);

    lrg_game_object_add_component (fixture->object, LRG_COMPONENT (comp1));
    lrg_game_object_add_component (fixture->object, LRG_COMPONENT (comp2));

    lrg_game_object_remove_all_components (fixture->object);

    g_assert_cmpuint (lrg_game_object_get_component_count (fixture->object), ==, 0);
    g_assert_true (comp1->detached_called);
    g_assert_null (lrg_component_get_owner (LRG_COMPONENT (comp1)));
}

/* ==========================================================================
 * Test Cases - World
 * ========================================================================== */

static void
test_world_new (void)
{
    g_autoptr(LrgWorld) world = NULL;

    world = lrg_world_new ();

    g_assert_nonnull (world);
    g_assert_true (LRG_IS_WORLD (world));
    g_assert_cmpuint (lrg_world_get_object_count (world), ==, 0);
    g_assert_true (lrg_world_get_active (world));
    g_assert_false (lrg_world_get_paused (world));
}

static void
test_world_add_object (WorldFixture  *fixture,
                       gconstpointer  user_data)
{
    g_autoptr(LrgGameObject) object = NULL;

    object = lrg_game_object_new ();

    lrg_world_add_object (fixture->world, object);

    g_assert_cmpuint (lrg_world_get_object_count (fixture->world), ==, 1);
}

static void
test_world_remove_object (WorldFixture  *fixture,
                          gconstpointer  user_data)
{
    g_autoptr(LrgGameObject) object = NULL;

    object = lrg_game_object_new ();
    g_object_ref (object);

    lrg_world_add_object (fixture->world, object);
    g_assert_cmpuint (lrg_world_get_object_count (fixture->world), ==, 1);

    lrg_world_remove_object (fixture->world, object);
    g_assert_cmpuint (lrg_world_get_object_count (fixture->world), ==, 0);
}

static void
test_world_clear (WorldFixture  *fixture,
                  gconstpointer  user_data)
{
    g_autoptr(LrgGameObject) obj1 = NULL;
    g_autoptr(LrgGameObject) obj2 = NULL;

    obj1 = lrg_game_object_new ();
    obj2 = lrg_game_object_new ();

    lrg_world_add_object (fixture->world, obj1);
    lrg_world_add_object (fixture->world, obj2);
    g_assert_cmpuint (lrg_world_get_object_count (fixture->world), ==, 2);

    lrg_world_clear (fixture->world);
    g_assert_cmpuint (lrg_world_get_object_count (fixture->world), ==, 0);
}

static void
test_world_find_by_tag (WorldFixture  *fixture,
                        gconstpointer  user_data)
{
    g_autoptr(LrgGameObject) obj1 = NULL;
    g_autoptr(LrgGameObject) obj2 = NULL;
    LrgGameObject           *found;

    obj1 = lrg_game_object_new ();
    obj2 = lrg_game_object_new ();

    grl_entity_set_tag (GRL_ENTITY (obj1), "player");
    grl_entity_set_tag (GRL_ENTITY (obj2), "enemy");

    lrg_world_add_object (fixture->world, obj1);
    lrg_world_add_object (fixture->world, obj2);

    found = lrg_world_find_by_tag (fixture->world, "player");
    g_assert_true (found == obj1);

    found = lrg_world_find_by_tag (fixture->world, "enemy");
    g_assert_true (found == obj2);

    found = lrg_world_find_by_tag (fixture->world, "nonexistent");
    g_assert_null (found);
}

static void
test_world_find_all_by_tag (WorldFixture  *fixture,
                            gconstpointer  user_data)
{
    g_autoptr(LrgGameObject) obj1 = NULL;
    g_autoptr(LrgGameObject) obj2 = NULL;
    g_autoptr(LrgGameObject) obj3 = NULL;
    GList                   *found;

    obj1 = lrg_game_object_new ();
    obj2 = lrg_game_object_new ();
    obj3 = lrg_game_object_new ();

    grl_entity_set_tag (GRL_ENTITY (obj1), "enemy");
    grl_entity_set_tag (GRL_ENTITY (obj2), "enemy");
    grl_entity_set_tag (GRL_ENTITY (obj3), "player");

    lrg_world_add_object (fixture->world, obj1);
    lrg_world_add_object (fixture->world, obj2);
    lrg_world_add_object (fixture->world, obj3);

    found = lrg_world_find_all_by_tag (fixture->world, "enemy");
    g_assert_cmpuint (g_list_length (found), ==, 2);
    g_list_free (found);

    found = lrg_world_find_all_by_tag (fixture->world, "player");
    g_assert_cmpuint (g_list_length (found), ==, 1);
    g_list_free (found);
}

static void
test_world_active (WorldFixture  *fixture,
                   gconstpointer  user_data)
{
    g_assert_true (lrg_world_get_active (fixture->world));

    lrg_world_set_active (fixture->world, FALSE);
    g_assert_false (lrg_world_get_active (fixture->world));

    lrg_world_set_active (fixture->world, TRUE);
    g_assert_true (lrg_world_get_active (fixture->world));
}

static void
test_world_paused (WorldFixture  *fixture,
                   gconstpointer  user_data)
{
    g_assert_false (lrg_world_get_paused (fixture->world));

    lrg_world_set_paused (fixture->world, TRUE);
    g_assert_true (lrg_world_get_paused (fixture->world));

    lrg_world_set_paused (fixture->world, FALSE);
    g_assert_false (lrg_world_get_paused (fixture->world));
}

static void
test_world_get_scene (WorldFixture  *fixture,
                      gconstpointer  user_data)
{
    GrlScene *scene;

    scene = lrg_world_get_scene (fixture->world);
    g_assert_nonnull (scene);
    g_assert_true (GRL_IS_SCENE (scene));
}

/* ==========================================================================
 * Test Cases - Sprite Component
 * ========================================================================== */

static void
test_sprite_component_new (void)
{
    g_autoptr(LrgSpriteComponent) sprite = NULL;

    sprite = lrg_sprite_component_new ();

    g_assert_nonnull (sprite);
    g_assert_true (LRG_IS_SPRITE_COMPONENT (sprite));
    g_assert_true (LRG_IS_COMPONENT (sprite));
    g_assert_null (lrg_sprite_component_get_texture (sprite));
    g_assert_false (lrg_sprite_component_get_flip_h (sprite));
    g_assert_false (lrg_sprite_component_get_flip_v (sprite));
}

static void
test_sprite_component_flip (void)
{
    g_autoptr(LrgSpriteComponent) sprite = NULL;

    sprite = lrg_sprite_component_new ();

    lrg_sprite_component_set_flip_h (sprite, TRUE);
    g_assert_true (lrg_sprite_component_get_flip_h (sprite));

    lrg_sprite_component_set_flip_v (sprite, TRUE);
    g_assert_true (lrg_sprite_component_get_flip_v (sprite));

    lrg_sprite_component_set_flip_h (sprite, FALSE);
    g_assert_false (lrg_sprite_component_get_flip_h (sprite));
}

static void
test_sprite_component_source (void)
{
    g_autoptr(LrgSpriteComponent) sprite = NULL;
    g_autoptr(GrlRectangle)       source = NULL;

    sprite = lrg_sprite_component_new ();

    /* No source by default */
    source = lrg_sprite_component_get_source (sprite);
    g_assert_null (source);

    /* Set source */
    lrg_sprite_component_set_source (sprite, 10, 20, 32, 32);
    source = lrg_sprite_component_get_source (sprite);
    g_assert_nonnull (source);
    g_assert_cmpfloat_with_epsilon (source->x, 10.0f, 0.0001f);
    g_assert_cmpfloat_with_epsilon (source->y, 20.0f, 0.0001f);
    g_assert_cmpfloat_with_epsilon (source->width, 32.0f, 0.0001f);
    g_assert_cmpfloat_with_epsilon (source->height, 32.0f, 0.0001f);

    /* Clear source */
    g_clear_pointer (&source, grl_rectangle_free);
    lrg_sprite_component_clear_source (sprite);
    source = lrg_sprite_component_get_source (sprite);
    g_assert_null (source);
}

static void
test_sprite_component_tint (void)
{
    g_autoptr(LrgSpriteComponent) sprite = NULL;
    g_autoptr(GrlColor)           tint = NULL;
    g_autoptr(GrlColor)           red = NULL;

    sprite = lrg_sprite_component_new ();

    /* Default is white */
    tint = lrg_sprite_component_get_tint (sprite);
    g_assert_nonnull (tint);
    g_assert_cmpint (tint->r, ==, 255);
    g_assert_cmpint (tint->g, ==, 255);
    g_assert_cmpint (tint->b, ==, 255);
    g_assert_cmpint (tint->a, ==, 255);

    /* Set to red */
    red = grl_color_new (255, 0, 0, 255);
    lrg_sprite_component_set_tint (sprite, red);

    g_clear_pointer (&tint, grl_color_free);
    tint = lrg_sprite_component_get_tint (sprite);
    g_assert_cmpint (tint->r, ==, 255);
    g_assert_cmpint (tint->g, ==, 0);
    g_assert_cmpint (tint->b, ==, 0);
}

/* ==========================================================================
 * Test Cases - Collider Component
 * ========================================================================== */

static void
test_collider_component_new (void)
{
    g_autoptr(LrgColliderComponent) collider = NULL;

    collider = lrg_collider_component_new ();

    g_assert_nonnull (collider);
    g_assert_true (LRG_IS_COLLIDER_COMPONENT (collider));
    g_assert_true (LRG_IS_COMPONENT (collider));
    g_assert_true (lrg_collider_component_get_collision_enabled (collider));
}

static void
test_collider_component_bounds (void)
{
    g_autoptr(LrgColliderComponent) collider = NULL;
    g_autoptr(GrlRectangle)         bounds = NULL;

    collider = lrg_collider_component_new_with_bounds (10.0f, 20.0f, 32.0f, 32.0f);

    bounds = lrg_collider_component_get_bounds (collider);
    g_assert_nonnull (bounds);
    g_assert_cmpfloat_with_epsilon (bounds->x, 10.0f, 0.0001f);
    g_assert_cmpfloat_with_epsilon (bounds->y, 20.0f, 0.0001f);
    g_assert_cmpfloat_with_epsilon (bounds->width, 32.0f, 0.0001f);
    g_assert_cmpfloat_with_epsilon (bounds->height, 32.0f, 0.0001f);
}

static void
test_collider_component_world_bounds (void)
{
    g_autoptr(LrgGameObject)        object = NULL;
    g_autoptr(LrgColliderComponent) collider = NULL;
    g_autoptr(GrlRectangle)         world_bounds = NULL;

    object = lrg_game_object_new_at (100.0f, 200.0f);
    collider = lrg_collider_component_new_with_bounds (10.0f, 20.0f, 32.0f, 32.0f);

    /* No owner yet */
    world_bounds = lrg_collider_component_get_world_bounds (collider);
    g_assert_null (world_bounds);

    /* Add to game object */
    lrg_game_object_add_component (object, LRG_COMPONENT (collider));

    world_bounds = lrg_collider_component_get_world_bounds (collider);
    g_assert_nonnull (world_bounds);
    g_assert_cmpfloat_with_epsilon (world_bounds->x, 110.0f, 0.0001f);  /* 100 + 10 */
    g_assert_cmpfloat_with_epsilon (world_bounds->y, 220.0f, 0.0001f);  /* 200 + 20 */
}

static void
test_collider_component_layers (void)
{
    g_autoptr(LrgColliderComponent) collider = NULL;

    collider = lrg_collider_component_new ();

    /* Defaults */
    g_assert_cmpuint (lrg_collider_component_get_layer (collider), ==, 1);
    g_assert_cmpuint (lrg_collider_component_get_mask (collider), ==, G_MAXUINT32);

    /* Set layer/mask */
    lrg_collider_component_set_layer (collider, 4);
    lrg_collider_component_set_mask (collider, 6);

    g_assert_cmpuint (lrg_collider_component_get_layer (collider), ==, 4);
    g_assert_cmpuint (lrg_collider_component_get_mask (collider), ==, 6);
}

static void
test_collider_component_can_collide (void)
{
    g_autoptr(LrgColliderComponent) a = NULL;
    g_autoptr(LrgColliderComponent) b = NULL;

    a = lrg_collider_component_new ();
    b = lrg_collider_component_new ();

    /* Default: all layers match all masks */
    g_assert_true (lrg_collider_component_can_collide_with (a, b));

    /* Set up layer/mask that don't match */
    lrg_collider_component_set_layer (a, 1);  /* Layer 0 */
    lrg_collider_component_set_mask (a, 1);   /* Only collide with layer 0 */
    lrg_collider_component_set_layer (b, 2);  /* Layer 1 */
    lrg_collider_component_set_mask (b, 2);   /* Only collide with layer 1 */

    g_assert_false (lrg_collider_component_can_collide_with (a, b));

    /* Fix masks to allow collision */
    lrg_collider_component_set_mask (a, 3);  /* Collide with layers 0 and 1 */
    lrg_collider_component_set_mask (b, 3);

    g_assert_true (lrg_collider_component_can_collide_with (a, b));

    /* Disable collision on one */
    lrg_collider_component_set_collision_enabled (a, FALSE);
    g_assert_false (lrg_collider_component_can_collide_with (a, b));
}

/* ==========================================================================
 * Test Cases - Transform Component
 * ========================================================================== */

static void
test_transform_component_new (void)
{
    g_autoptr(LrgTransformComponent) transform = NULL;

    transform = lrg_transform_component_new ();

    g_assert_nonnull (transform);
    g_assert_true (LRG_IS_TRANSFORM_COMPONENT (transform));
    g_assert_true (LRG_IS_COMPONENT (transform));
    g_assert_cmpfloat_with_epsilon (lrg_transform_component_get_local_x (transform), 0.0f, 0.0001f);
    g_assert_cmpfloat_with_epsilon (lrg_transform_component_get_local_y (transform), 0.0f, 0.0001f);
    g_assert_cmpfloat_with_epsilon (lrg_transform_component_get_local_rotation (transform), 0.0f, 0.0001f);
}

static void
test_transform_component_new_at (void)
{
    g_autoptr(LrgTransformComponent) transform = NULL;

    transform = lrg_transform_component_new_at (100.0f, 200.0f);

    g_assert_cmpfloat_with_epsilon (lrg_transform_component_get_local_x (transform), 100.0f, 0.0001f);
    g_assert_cmpfloat_with_epsilon (lrg_transform_component_get_local_y (transform), 200.0f, 0.0001f);
}

static void
test_transform_component_local_position (void)
{
    g_autoptr(LrgTransformComponent) transform = NULL;
    g_autoptr(GrlVector2)            pos = NULL;

    transform = lrg_transform_component_new ();

    lrg_transform_component_set_local_position_xy (transform, 50.0f, 75.0f);

    pos = lrg_transform_component_get_local_position (transform);
    g_assert_cmpfloat_with_epsilon (pos->x, 50.0f, 0.0001f);
    g_assert_cmpfloat_with_epsilon (pos->y, 75.0f, 0.0001f);
}

static void
test_transform_component_local_rotation (void)
{
    g_autoptr(LrgTransformComponent) transform = NULL;

    transform = lrg_transform_component_new ();

    lrg_transform_component_set_local_rotation (transform, 45.0f);
    g_assert_cmpfloat_with_epsilon (lrg_transform_component_get_local_rotation (transform), 45.0f, 0.0001f);
}

static void
test_transform_component_local_scale (void)
{
    g_autoptr(LrgTransformComponent) transform = NULL;
    g_autoptr(GrlVector2)            scale = NULL;

    transform = lrg_transform_component_new ();

    /* Default scale is 1.0 */
    scale = lrg_transform_component_get_local_scale (transform);
    g_assert_cmpfloat_with_epsilon (scale->x, 1.0f, 0.0001f);
    g_assert_cmpfloat_with_epsilon (scale->y, 1.0f, 0.0001f);

    /* Set non-uniform scale */
    lrg_transform_component_set_local_scale_xy (transform, 2.0f, 0.5f);

    g_clear_pointer (&scale, grl_vector2_free);
    scale = lrg_transform_component_get_local_scale (transform);
    g_assert_cmpfloat_with_epsilon (scale->x, 2.0f, 0.0001f);
    g_assert_cmpfloat_with_epsilon (scale->y, 0.5f, 0.0001f);
}

static void
test_transform_component_hierarchy (void)
{
    g_autoptr(LrgTransformComponent) parent = NULL;
    g_autoptr(LrgTransformComponent) child = NULL;
    GList                           *children = NULL;

    parent = lrg_transform_component_new_at (100.0f, 100.0f);
    child = lrg_transform_component_new_at (10.0f, 10.0f);

    /* No parent initially */
    g_assert_null (lrg_transform_component_get_parent (child));
    g_assert_cmpuint (lrg_transform_component_get_child_count (parent), ==, 0);

    /* Set parent */
    lrg_transform_component_set_parent (child, parent);
    g_assert_true (lrg_transform_component_get_parent (child) == parent);
    g_assert_cmpuint (lrg_transform_component_get_child_count (parent), ==, 1);

    children = lrg_transform_component_get_children (parent);
    g_assert_cmpuint (g_list_length (children), ==, 1);
    g_assert_true (g_list_nth_data (children, 0) == child);
    g_list_free (children);

    /* Unparent */
    lrg_transform_component_set_parent (child, NULL);
    g_assert_null (lrg_transform_component_get_parent (child));
    g_assert_cmpuint (lrg_transform_component_get_child_count (parent), ==, 0);
}

static void
test_transform_component_world_position (void)
{
    g_autoptr(LrgTransformComponent) parent = NULL;
    g_autoptr(LrgTransformComponent) child = NULL;
    g_autoptr(GrlVector2)            world_pos = NULL;

    parent = lrg_transform_component_new_at (100.0f, 100.0f);
    child = lrg_transform_component_new_at (10.0f, 20.0f);

    /* Without parent, world = local */
    world_pos = lrg_transform_component_get_world_position (child);
    g_assert_cmpfloat_with_epsilon (world_pos->x, 10.0f, 0.0001f);
    g_assert_cmpfloat_with_epsilon (world_pos->y, 20.0f, 0.0001f);

    /* With parent, world = parent + local */
    lrg_transform_component_set_parent (child, parent);
    g_clear_pointer (&world_pos, grl_vector2_free);
    world_pos = lrg_transform_component_get_world_position (child);
    g_assert_cmpfloat_with_epsilon (world_pos->x, 110.0f, 0.0001f);
    g_assert_cmpfloat_with_epsilon (world_pos->y, 120.0f, 0.0001f);
}

static void
test_transform_component_translate (void)
{
    g_autoptr(LrgTransformComponent) transform = NULL;
    g_autoptr(GrlVector2)            offset = NULL;

    transform = lrg_transform_component_new_at (50.0f, 50.0f);
    offset = grl_vector2_new (10.0f, -5.0f);

    lrg_transform_component_translate (transform, offset);

    g_assert_cmpfloat_with_epsilon (lrg_transform_component_get_local_x (transform), 60.0f, 0.0001f);
    g_assert_cmpfloat_with_epsilon (lrg_transform_component_get_local_y (transform), 45.0f, 0.0001f);
}

static void
test_transform_component_rotate (void)
{
    g_autoptr(LrgTransformComponent) transform = NULL;

    transform = lrg_transform_component_new ();
    lrg_transform_component_set_local_rotation (transform, 45.0f);

    lrg_transform_component_rotate (transform, 15.0f);

    g_assert_cmpfloat_with_epsilon (lrg_transform_component_get_local_rotation (transform), 60.0f, 0.0001f);
}

/* ==========================================================================
 * Test Cases - Animator Component
 * ========================================================================== */

static void
test_animator_component_new (void)
{
    g_autoptr(LrgAnimatorComponent) animator = NULL;

    animator = lrg_animator_component_new ();

    g_assert_nonnull (animator);
    g_assert_true (LRG_IS_ANIMATOR_COMPONENT (animator));
    g_assert_true (LRG_IS_COMPONENT (animator));
    g_assert_null (lrg_animator_component_get_current_animation (animator));
    g_assert_false (lrg_animator_component_is_playing (animator));
    g_assert_cmpfloat_with_epsilon (lrg_animator_component_get_speed (animator), 1.0f, 0.0001f);
}

static void
test_animator_component_add_animation (void)
{
    g_autoptr(LrgAnimatorComponent) animator = NULL;
    gboolean                        result;

    animator = lrg_animator_component_new ();

    result = lrg_animator_component_add_animation (animator, "idle", 0, 4, 10.0f, TRUE);
    g_assert_true (result);
    g_assert_true (lrg_animator_component_has_animation (animator, "idle"));

    result = lrg_animator_component_add_animation (animator, "walk", 4, 8, 15.0f, TRUE);
    g_assert_true (result);
    g_assert_true (lrg_animator_component_has_animation (animator, "walk"));

    /* Duplicate name should fail */
    result = lrg_animator_component_add_animation (animator, "idle", 0, 4, 10.0f, TRUE);
    g_assert_false (result);
}

static void
test_animator_component_play (void)
{
    g_autoptr(LrgAnimatorComponent) animator = NULL;
    gboolean                        result;

    animator = lrg_animator_component_new ();

    lrg_animator_component_add_animation (animator, "idle", 0, 4, 10.0f, TRUE);
    lrg_animator_component_add_animation (animator, "walk", 4, 8, 15.0f, TRUE);

    /* Play animation */
    result = lrg_animator_component_play (animator, "idle");
    g_assert_true (result);
    g_assert_cmpstr (lrg_animator_component_get_current_animation (animator), ==, "idle");
    g_assert_true (lrg_animator_component_is_playing (animator));
    g_assert_cmpint (lrg_animator_component_get_current_frame (animator), ==, 0);

    /* Play different animation */
    result = lrg_animator_component_play (animator, "walk");
    g_assert_true (result);
    g_assert_cmpstr (lrg_animator_component_get_current_animation (animator), ==, "walk");
    g_assert_cmpint (lrg_animator_component_get_current_frame (animator), ==, 4);

    /* Play non-existent animation */
    result = lrg_animator_component_play (animator, "jump");
    g_assert_false (result);
}

static void
test_animator_component_stop (void)
{
    g_autoptr(LrgAnimatorComponent) animator = NULL;

    animator = lrg_animator_component_new ();
    lrg_animator_component_add_animation (animator, "idle", 0, 4, 10.0f, TRUE);

    lrg_animator_component_play (animator, "idle");
    g_assert_true (lrg_animator_component_is_playing (animator));

    lrg_animator_component_stop (animator);
    g_assert_false (lrg_animator_component_is_playing (animator));
    g_assert_cmpint (lrg_animator_component_get_current_frame (animator), ==, 0);
}

static void
test_animator_component_pause_resume (void)
{
    g_autoptr(LrgAnimatorComponent) animator = NULL;

    animator = lrg_animator_component_new ();
    lrg_animator_component_add_animation (animator, "idle", 0, 4, 10.0f, TRUE);

    lrg_animator_component_play (animator, "idle");
    g_assert_true (lrg_animator_component_is_playing (animator));

    lrg_animator_component_pause (animator);
    g_assert_false (lrg_animator_component_is_playing (animator));

    lrg_animator_component_resume (animator);
    g_assert_true (lrg_animator_component_is_playing (animator));
}

static void
test_animator_component_speed (void)
{
    g_autoptr(LrgAnimatorComponent) animator = NULL;

    animator = lrg_animator_component_new ();

    g_assert_cmpfloat_with_epsilon (lrg_animator_component_get_speed (animator), 1.0f, 0.0001f);

    lrg_animator_component_set_speed (animator, 2.0f);
    g_assert_cmpfloat_with_epsilon (lrg_animator_component_get_speed (animator), 2.0f, 0.0001f);

    lrg_animator_component_set_speed (animator, 0.5f);
    g_assert_cmpfloat_with_epsilon (lrg_animator_component_get_speed (animator), 0.5f, 0.0001f);
}

static void
test_animator_component_animation_names (void)
{
    g_autoptr(LrgAnimatorComponent) animator = NULL;
    GList                          *names = NULL;

    animator = lrg_animator_component_new ();

    lrg_animator_component_add_animation (animator, "idle", 0, 4, 10.0f, TRUE);
    lrg_animator_component_add_animation (animator, "walk", 4, 8, 15.0f, TRUE);
    lrg_animator_component_add_animation (animator, "run", 12, 6, 20.0f, TRUE);

    names = lrg_animator_component_get_animation_names (animator);
    g_assert_cmpuint (g_list_length (names), ==, 3);
    g_list_free (names);

    /* Remove one */
    lrg_animator_component_remove_animation (animator, "walk");
    g_assert_false (lrg_animator_component_has_animation (animator, "walk"));

    names = lrg_animator_component_get_animation_names (animator);
    g_assert_cmpuint (g_list_length (names), ==, 2);
    g_list_free (names);
}

static void
test_animator_component_default_animation (void)
{
    g_autoptr(LrgAnimatorComponent) animator = NULL;

    animator = lrg_animator_component_new ();

    g_assert_null (lrg_animator_component_get_default_animation (animator));

    lrg_animator_component_set_default_animation (animator, "idle");
    g_assert_cmpstr (lrg_animator_component_get_default_animation (animator), ==, "idle");

    lrg_animator_component_set_default_animation (animator, NULL);
    g_assert_null (lrg_animator_component_get_default_animation (animator));
}

/* ==========================================================================
 * Main
 * ========================================================================== */

int
main (int   argc,
      char *argv[])
{
    g_test_init (&argc, &argv, NULL);

    /* Component Tests */
    g_test_add_func ("/ecs/component/new", test_component_new);

    g_test_add ("/ecs/component/enabled",
                ComponentFixture, NULL,
                component_fixture_set_up,
                test_component_enabled,
                component_fixture_tear_down);

    g_test_add ("/ecs/component/update-when-enabled",
                ComponentFixture, NULL,
                component_fixture_set_up,
                test_component_update_when_enabled,
                component_fixture_tear_down);

    g_test_add ("/ecs/component/update-when-disabled",
                ComponentFixture, NULL,
                component_fixture_set_up,
                test_component_update_when_disabled,
                component_fixture_tear_down);

    /* GameObject Tests */
    g_test_add_func ("/ecs/game-object/new", test_game_object_new);
    g_test_add_func ("/ecs/game-object/new-at", test_game_object_new_at);

    g_test_add ("/ecs/game-object/add-component",
                GameObjectFixture, NULL,
                game_object_fixture_set_up,
                test_game_object_add_component,
                game_object_fixture_tear_down);

    g_test_add ("/ecs/game-object/remove-component",
                GameObjectFixture, NULL,
                game_object_fixture_set_up,
                test_game_object_remove_component,
                game_object_fixture_tear_down);

    g_test_add ("/ecs/game-object/get-component",
                GameObjectFixture, NULL,
                game_object_fixture_set_up,
                test_game_object_get_component,
                game_object_fixture_tear_down);

    g_test_add ("/ecs/game-object/get-components",
                GameObjectFixture, NULL,
                game_object_fixture_set_up,
                test_game_object_get_components,
                game_object_fixture_tear_down);

    g_test_add ("/ecs/game-object/has-component",
                GameObjectFixture, NULL,
                game_object_fixture_set_up,
                test_game_object_has_component,
                game_object_fixture_tear_down);

    g_test_add ("/ecs/game-object/multiple-components",
                GameObjectFixture, NULL,
                game_object_fixture_set_up,
                test_game_object_multiple_components,
                game_object_fixture_tear_down);

    g_test_add ("/ecs/game-object/remove-all-components",
                GameObjectFixture, NULL,
                game_object_fixture_set_up,
                test_game_object_remove_all_components,
                game_object_fixture_tear_down);

    /* World Tests */
    g_test_add_func ("/ecs/world/new", test_world_new);

    g_test_add ("/ecs/world/add-object",
                WorldFixture, NULL,
                world_fixture_set_up,
                test_world_add_object,
                world_fixture_tear_down);

    g_test_add ("/ecs/world/remove-object",
                WorldFixture, NULL,
                world_fixture_set_up,
                test_world_remove_object,
                world_fixture_tear_down);

    g_test_add ("/ecs/world/clear",
                WorldFixture, NULL,
                world_fixture_set_up,
                test_world_clear,
                world_fixture_tear_down);

    g_test_add ("/ecs/world/find-by-tag",
                WorldFixture, NULL,
                world_fixture_set_up,
                test_world_find_by_tag,
                world_fixture_tear_down);

    g_test_add ("/ecs/world/find-all-by-tag",
                WorldFixture, NULL,
                world_fixture_set_up,
                test_world_find_all_by_tag,
                world_fixture_tear_down);

    g_test_add ("/ecs/world/active",
                WorldFixture, NULL,
                world_fixture_set_up,
                test_world_active,
                world_fixture_tear_down);

    g_test_add ("/ecs/world/paused",
                WorldFixture, NULL,
                world_fixture_set_up,
                test_world_paused,
                world_fixture_tear_down);

    g_test_add ("/ecs/world/get-scene",
                WorldFixture, NULL,
                world_fixture_set_up,
                test_world_get_scene,
                world_fixture_tear_down);

    /* Sprite Component Tests */
    g_test_add_func ("/ecs/sprite-component/new", test_sprite_component_new);
    g_test_add_func ("/ecs/sprite-component/flip", test_sprite_component_flip);
    g_test_add_func ("/ecs/sprite-component/source", test_sprite_component_source);
    g_test_add_func ("/ecs/sprite-component/tint", test_sprite_component_tint);

    /* Collider Component Tests */
    g_test_add_func ("/ecs/collider-component/new", test_collider_component_new);
    g_test_add_func ("/ecs/collider-component/bounds", test_collider_component_bounds);
    g_test_add_func ("/ecs/collider-component/world-bounds", test_collider_component_world_bounds);
    g_test_add_func ("/ecs/collider-component/layers", test_collider_component_layers);
    g_test_add_func ("/ecs/collider-component/can-collide", test_collider_component_can_collide);

    /* Transform Component Tests */
    g_test_add_func ("/ecs/transform-component/new", test_transform_component_new);
    g_test_add_func ("/ecs/transform-component/new-at", test_transform_component_new_at);
    g_test_add_func ("/ecs/transform-component/local-position", test_transform_component_local_position);
    g_test_add_func ("/ecs/transform-component/local-rotation", test_transform_component_local_rotation);
    g_test_add_func ("/ecs/transform-component/local-scale", test_transform_component_local_scale);
    g_test_add_func ("/ecs/transform-component/hierarchy", test_transform_component_hierarchy);
    g_test_add_func ("/ecs/transform-component/world-position", test_transform_component_world_position);
    g_test_add_func ("/ecs/transform-component/translate", test_transform_component_translate);
    g_test_add_func ("/ecs/transform-component/rotate", test_transform_component_rotate);

    /* Animator Component Tests */
    g_test_add_func ("/ecs/animator-component/new", test_animator_component_new);
    g_test_add_func ("/ecs/animator-component/add-animation", test_animator_component_add_animation);
    g_test_add_func ("/ecs/animator-component/play", test_animator_component_play);
    g_test_add_func ("/ecs/animator-component/stop", test_animator_component_stop);
    g_test_add_func ("/ecs/animator-component/pause-resume", test_animator_component_pause_resume);
    g_test_add_func ("/ecs/animator-component/speed", test_animator_component_speed);
    g_test_add_func ("/ecs/animator-component/animation-names", test_animator_component_animation_names);
    g_test_add_func ("/ecs/animator-component/default-animation", test_animator_component_default_animation);

    return g_test_run ();
}
