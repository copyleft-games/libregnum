/* lrg-types.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Forward declarations for all Libregnum types.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>

G_BEGIN_DECLS

/* ==========================================================================
 * Core Module
 * ========================================================================== */

typedef struct _LrgEngine       LrgEngine;
typedef struct _LrgEngineClass  LrgEngineClass;

/* LrgRegistry is a final type - no Class forward declaration needed */
typedef struct _LrgRegistry       LrgRegistry;

/* LrgDataLoader is a final type - no Class forward declaration needed */
typedef struct _LrgDataLoader       LrgDataLoader;

typedef struct _LrgAssetManager       LrgAssetManager;
typedef struct _LrgAssetManagerClass  LrgAssetManagerClass;

/* ==========================================================================
 * ECS Module
 * ========================================================================== */

typedef struct _LrgComponent       LrgComponent;
typedef struct _LrgComponentClass  LrgComponentClass;

typedef struct _LrgGameObject       LrgGameObject;
typedef struct _LrgGameObjectClass  LrgGameObjectClass;

/* LrgWorld is a final type - no Class forward declaration needed */
typedef struct _LrgWorld       LrgWorld;

/* Components */
typedef struct _LrgTransformComponent       LrgTransformComponent;
typedef struct _LrgTransformComponentClass  LrgTransformComponentClass;

/* LrgSpriteComponent is a final type - no Class forward declaration needed */
typedef struct _LrgSpriteComponent       LrgSpriteComponent;

/* LrgColliderComponent is a final type - no Class forward declaration needed */
typedef struct _LrgColliderComponent       LrgColliderComponent;

typedef struct _LrgAnimatorComponent       LrgAnimatorComponent;
typedef struct _LrgAnimatorComponentClass  LrgAnimatorComponentClass;

/* ==========================================================================
 * Input Module
 * ========================================================================== */

typedef struct _LrgInputBinding  LrgInputBinding;

typedef struct _LrgInputAction       LrgInputAction;
typedef struct _LrgInputActionClass  LrgInputActionClass;

typedef struct _LrgInputMap       LrgInputMap;
typedef struct _LrgInputMapClass  LrgInputMapClass;

/* LrgInput is a derivable type */
typedef struct _LrgInput       LrgInput;
typedef struct _LrgInputClass  LrgInputClass;

/* LrgInputManager is a final type - no Class forward declaration needed */
typedef struct _LrgInputManager  LrgInputManager;

/* LrgInputKeyboard is a final type - no Class forward declaration needed */
typedef struct _LrgInputKeyboard  LrgInputKeyboard;

/* LrgInputMouse is a final type - no Class forward declaration needed */
typedef struct _LrgInputMouse  LrgInputMouse;

/* LrgInputGamepad is a final type - no Class forward declaration needed */
typedef struct _LrgInputGamepad  LrgInputGamepad;

/* LrgInputMock is a final type - no Class forward declaration needed */
typedef struct _LrgInputMock  LrgInputMock;

/* LrgInputSoftware is a final type - no Class forward declaration needed */
typedef struct _LrgInputSoftware  LrgInputSoftware;

/* ==========================================================================
 * UI Module
 * ========================================================================== */

typedef struct _LrgWidget       LrgWidget;
typedef struct _LrgWidgetClass  LrgWidgetClass;

typedef struct _LrgContainer       LrgContainer;
typedef struct _LrgContainerClass  LrgContainerClass;

typedef struct _LrgCanvas       LrgCanvas;

typedef struct _LrgTheme       LrgTheme;

/* Widgets (final types - no class typedef needed) */
typedef struct _LrgLabel       LrgLabel;
typedef struct _LrgButton      LrgButton;
typedef struct _LrgPanel       LrgPanel;
typedef struct _LrgCheckbox    LrgCheckbox;
typedef struct _LrgSlider      LrgSlider;
typedef struct _LrgProgressBar LrgProgressBar;
typedef struct _LrgTextInput   LrgTextInput;
typedef struct _LrgImage       LrgImage;

/* Layouts (final types - no class typedef needed) */
typedef struct _LrgVBox        LrgVBox;
typedef struct _LrgHBox        LrgHBox;
typedef struct _LrgGrid        LrgGrid;

/* ==========================================================================
 * Tilemap Module
 * ========================================================================== */

/* Final types - no class typedef needed */
typedef struct _LrgTileset       LrgTileset;
typedef struct _LrgTilemapLayer  LrgTilemapLayer;

/* Derivable types */
typedef struct _LrgTilemap       LrgTilemap;
typedef struct _LrgTilemapClass  LrgTilemapClass;

/* ==========================================================================
 * Dialog Module
 * ========================================================================== */

/* Derivable type */
typedef struct _LrgDialogNode       LrgDialogNode;
typedef struct _LrgDialogNodeClass  LrgDialogNodeClass;

/* Boxed type - no class typedef */
typedef struct _LrgDialogResponse  LrgDialogResponse;

/* Final types - no class typedef needed */
typedef struct _LrgDialogTree    LrgDialogTree;
typedef struct _LrgDialogRunner  LrgDialogRunner;

/* ==========================================================================
 * Inventory Module
 * ========================================================================== */

typedef struct _LrgItemDef       LrgItemDef;
typedef struct _LrgItemDefClass  LrgItemDefClass;

typedef struct _LrgItemStack       LrgItemStack;
typedef struct _LrgItemStackClass  LrgItemStackClass;

typedef struct _LrgInventory       LrgInventory;
typedef struct _LrgInventoryClass  LrgInventoryClass;

/* LrgEquipment is a final type - no Class forward declaration needed */
typedef struct _LrgEquipment       LrgEquipment;

/* ==========================================================================
 * Quest Module
 * ========================================================================== */

/* Boxed type - no class typedef */
typedef struct _LrgQuestObjective  LrgQuestObjective;

/* Derivable type */
typedef struct _LrgQuestDef       LrgQuestDef;
typedef struct _LrgQuestDefClass  LrgQuestDefClass;

/* Final types - no class typedef needed */
typedef struct _LrgQuestInstance  LrgQuestInstance;
typedef struct _LrgQuestLog       LrgQuestLog;

/* ==========================================================================
 * Save Module
 * ========================================================================== */

/* Final types - no class typedef needed */
typedef struct _LrgSaveContext       LrgSaveContext;
typedef struct _LrgSaveGame          LrgSaveGame;
typedef struct _LrgSaveManager       LrgSaveManager;

/* ==========================================================================
 * Audio Module
 * ========================================================================== */

/* Final types - no class typedef needed */
typedef struct _LrgSoundBank       LrgSoundBank;
typedef struct _LrgMusicTrack      LrgMusicTrack;
typedef struct _LrgAudioManager    LrgAudioManager;

/* ==========================================================================
 * AI Module
 * ========================================================================== */

/* Derivable types */
typedef struct _LrgBTNode       LrgBTNode;
typedef struct _LrgBTNodeClass  LrgBTNodeClass;

typedef struct _LrgBTComposite       LrgBTComposite;
typedef struct _LrgBTCompositeClass  LrgBTCompositeClass;

typedef struct _LrgBTDecorator       LrgBTDecorator;
typedef struct _LrgBTDecoratorClass  LrgBTDecoratorClass;

/* Final types - no class typedef needed */
typedef struct _LrgBlackboard     LrgBlackboard;
typedef struct _LrgBehaviorTree   LrgBehaviorTree;

/* ==========================================================================
 * Pathfinding Module
 * ========================================================================== */

typedef struct _LrgNavCell  LrgNavCell;  /* Boxed type */
typedef struct _LrgPath     LrgPath;     /* Boxed type */

typedef struct _LrgNavGrid       LrgNavGrid;
typedef struct _LrgNavGridClass  LrgNavGridClass;

typedef struct _LrgPathfinder       LrgPathfinder;
/* LrgPathfinderClass is not forward-declared (FINAL type) */

/* ==========================================================================
 * Physics Module
 * ========================================================================== */

typedef struct _LrgRigidBody       LrgRigidBody;
typedef struct _LrgRigidBodyClass  LrgRigidBodyClass;

typedef struct _LrgPhysicsWorld       LrgPhysicsWorld;
typedef struct _LrgPhysicsWorldClass  LrgPhysicsWorldClass;

/* ==========================================================================
 * Localization Module
 * ========================================================================== */

typedef struct _LrgLocale       LrgLocale;
typedef struct _LrgLocaleClass  LrgLocaleClass;

typedef struct _LrgLocalization       LrgLocalization;
/* LrgLocalizationClass is not forward-declared (FINAL type) */

/* ==========================================================================
 * Networking Module
 * ========================================================================== */

/* Boxed type - no class typedef */
typedef struct _LrgNetMessage  LrgNetMessage;

/* Final types - no class typedef needed */
typedef struct _LrgNetPeer    LrgNetPeer;
typedef struct _LrgNetServer  LrgNetServer;
typedef struct _LrgNetClient  LrgNetClient;

/* ==========================================================================
 * Graphics Module
 * ========================================================================== */

/* LrgDrawable is an interface - only interface typedef needed */
typedef struct _LrgDrawable           LrgDrawable;
typedef struct _LrgDrawableInterface  LrgDrawableInterface;

/* LrgWindow is a derivable type */
typedef struct _LrgWindow       LrgWindow;
typedef struct _LrgWindowClass  LrgWindowClass;

/* LrgGrlWindow is a final type - no Class forward declaration needed */
typedef struct _LrgGrlWindow       LrgGrlWindow;

/* LrgCamera is a derivable type */
typedef struct _LrgCamera       LrgCamera;
typedef struct _LrgCameraClass  LrgCameraClass;

/* LrgCamera2D is a derivable type */
typedef struct _LrgCamera2D       LrgCamera2D;
typedef struct _LrgCamera2DClass  LrgCamera2DClass;

/* LrgCamera3D is a derivable type */
typedef struct _LrgCamera3D       LrgCamera3D;
typedef struct _LrgCamera3DClass  LrgCamera3DClass;

/* LrgCameraIsometric is a derivable type (inherits from LrgCamera3D) */
typedef struct _LrgCameraIsometric       LrgCameraIsometric;
typedef struct _LrgCameraIsometricClass  LrgCameraIsometricClass;

/* LrgCameraTopDown is a derivable type (inherits from LrgCamera2D) */
typedef struct _LrgCameraTopDown       LrgCameraTopDown;
typedef struct _LrgCameraTopDownClass  LrgCameraTopDownClass;

/* LrgCameraSideOn is a derivable type (inherits from LrgCamera2D) */
typedef struct _LrgCameraSideOn       LrgCameraSideOn;
typedef struct _LrgCameraSideOnClass  LrgCameraSideOnClass;

/* LrgCameraFirstPerson is a derivable type (inherits from LrgCamera3D) */
typedef struct _LrgCameraFirstPerson       LrgCameraFirstPerson;
typedef struct _LrgCameraFirstPersonClass  LrgCameraFirstPersonClass;

/* LrgCameraThirdPerson is a derivable type (inherits from LrgCamera3D) */
typedef struct _LrgCameraThirdPerson       LrgCameraThirdPerson;
typedef struct _LrgCameraThirdPersonClass  LrgCameraThirdPersonClass;

/* LrgRenderer is a derivable type */
typedef struct _LrgRenderer       LrgRenderer;
typedef struct _LrgRendererClass  LrgRendererClass;

/* ==========================================================================
 * Shapes Module
 * ========================================================================== */

/* LrgShape is a derivable type (abstract base) */
typedef struct _LrgShape       LrgShape;
typedef struct _LrgShapeClass  LrgShapeClass;

/* LrgShape2D is a derivable type (abstract, for 2D shapes) */
typedef struct _LrgShape2D       LrgShape2D;
typedef struct _LrgShape2DClass  LrgShape2DClass;

/* LrgShape3D is a derivable type (abstract, for 3D shapes) */
typedef struct _LrgShape3D       LrgShape3D;
typedef struct _LrgShape3DClass  LrgShape3DClass;

/* LrgSphere3D is a final type - no Class forward declaration needed */
typedef struct _LrgSphere3D  LrgSphere3D;

/* LrgCube3D is a final type - no Class forward declaration needed */
typedef struct _LrgCube3D  LrgCube3D;

/* LrgLine3D is a final type - no Class forward declaration needed */
typedef struct _LrgLine3D  LrgLine3D;

/* LrgText2D is a final type - no Class forward declaration needed */
typedef struct _LrgText2D  LrgText2D;

/* ==========================================================================
 * 3D World Module
 * ========================================================================== */

/* Boxed types - no class typedef */
typedef struct _LrgBoundingBox3D  LrgBoundingBox3D;
typedef struct _LrgSpawnPoint3D   LrgSpawnPoint3D;
typedef struct _LrgTrigger3D      LrgTrigger3D;
typedef struct _LrgPortal         LrgPortal;
typedef struct _LrgSector         LrgSector;

/* Final types - no class typedef needed */
typedef struct _LrgOctree        LrgOctree;
typedef struct _LrgLevel3D       LrgLevel3D;
typedef struct _LrgPortalSystem  LrgPortalSystem;

/* ==========================================================================
 * Scene Module
 * ========================================================================== */

/* Boxed type - no class typedef */
typedef struct _LrgMeshData  LrgMeshData;

/* Final types - no class typedef needed */
typedef struct _LrgSceneObject    LrgSceneObject;
typedef struct _LrgSceneEntity    LrgSceneEntity;
typedef struct _LrgScene          LrgScene;
typedef struct _LrgMaterial3D     LrgMaterial3D;

/* Derivable types */
typedef struct _LrgSceneSerializer       LrgSceneSerializer;
typedef struct _LrgSceneSerializerClass  LrgSceneSerializerClass;

typedef struct _LrgSceneSerializerYaml       LrgSceneSerializerYaml;
typedef struct _LrgSceneSerializerYamlClass  LrgSceneSerializerYamlClass;

/* ==========================================================================
 * Debug Module
 * ========================================================================== */

/* Final types - no class typedef needed */
typedef struct _LrgDebugConsole   LrgDebugConsole;
typedef struct _LrgDebugOverlay   LrgDebugOverlay;
typedef struct _LrgProfiler       LrgProfiler;
typedef struct _LrgInspector      LrgInspector;

/* ==========================================================================
 * Mod Module
 * ========================================================================== */

/* Final types - no class typedef needed */
typedef struct _LrgModManifest  LrgModManifest;
typedef struct _LrgMod          LrgMod;
typedef struct _LrgModLoader    LrgModLoader;
typedef struct _LrgModManager   LrgModManager;

/* Boxed types */
typedef struct _LrgSemver          LrgSemver;
typedef struct _LrgConsoleCommand  LrgConsoleCommand;

/* ==========================================================================
 * Interfaces
 * ========================================================================== */

typedef struct _LrgSaveable           LrgSaveable;
typedef struct _LrgSaveableInterface  LrgSaveableInterface;

typedef struct _LrgModable           LrgModable;
typedef struct _LrgModableInterface  LrgModableInterface;

typedef struct _LrgEntityProvider           LrgEntityProvider;
typedef struct _LrgEntityProviderInterface  LrgEntityProviderInterface;

typedef struct _LrgItemProvider           LrgItemProvider;
typedef struct _LrgItemProviderInterface  LrgItemProviderInterface;

typedef struct _LrgSceneProvider           LrgSceneProvider;
typedef struct _LrgSceneProviderInterface  LrgSceneProviderInterface;

typedef struct _LrgDialogProvider           LrgDialogProvider;
typedef struct _LrgDialogProviderInterface  LrgDialogProviderInterface;

typedef struct _LrgQuestProvider           LrgQuestProvider;
typedef struct _LrgQuestProviderInterface  LrgQuestProviderInterface;

typedef struct _LrgAIProvider           LrgAIProvider;
typedef struct _LrgAIProviderInterface  LrgAIProviderInterface;

typedef struct _LrgCommandProvider           LrgCommandProvider;
typedef struct _LrgCommandProviderInterface  LrgCommandProviderInterface;

typedef struct _LrgLocaleProvider           LrgLocaleProvider;
typedef struct _LrgLocaleProviderInterface  LrgLocaleProviderInterface;

G_END_DECLS
