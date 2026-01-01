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

/* LrgAssetPack is a final type - no Class forward declaration needed */
typedef struct _LrgAssetPack       LrgAssetPack;

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
typedef struct _LrgWaveData        LrgWaveData;

/* LrgProceduralAudio is a derivable type */
typedef struct _LrgProceduralAudio       LrgProceduralAudio;
typedef struct _LrgProceduralAudioClass  LrgProceduralAudioClass;

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

/* LrgRectangle2D is a final type - no Class forward declaration needed */
typedef struct _LrgRectangle2D  LrgRectangle2D;

/* LrgCircle2D is a final type - no Class forward declaration needed */
typedef struct _LrgCircle2D  LrgCircle2D;

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

/* LrgMod is a derivable type */
typedef struct _LrgMod          LrgMod;
typedef struct _LrgModClass     LrgModClass;

typedef struct _LrgModLoader    LrgModLoader;
typedef struct _LrgModManager   LrgModManager;

/* DLC types - LrgDlc is derivable */
typedef struct _LrgDlc                    LrgDlc;
typedef struct _LrgDlcClass               LrgDlcClass;
typedef struct _LrgDlcOwnership           LrgDlcOwnership;
typedef struct _LrgDlcOwnershipInterface  LrgDlcOwnershipInterface;
typedef struct _LrgDlcOwnershipSteam      LrgDlcOwnershipSteam;
typedef struct _LrgDlcOwnershipLicense    LrgDlcOwnershipLicense;
typedef struct _LrgDlcOwnershipManifest   LrgDlcOwnershipManifest;
typedef struct _LrgExpansionPack          LrgExpansionPack;
typedef struct _LrgCosmeticPack           LrgCosmeticPack;
typedef struct _LrgQuestPack              LrgQuestPack;
typedef struct _LrgItemPack               LrgItemPack;
typedef struct _LrgCharacterPack          LrgCharacterPack;
typedef struct _LrgMapPack                LrgMapPack;

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

/* ==========================================================================
 * Scripting Module
 * ========================================================================== */

/* LrgScripting is a derivable type (abstract base class) */
typedef struct _LrgScripting       LrgScripting;
typedef struct _LrgScriptingClass  LrgScriptingClass;

/* LrgScriptingLua is a final type - no Class forward declaration needed */
typedef struct _LrgScriptingLua  LrgScriptingLua;

/* LrgScriptingGI is a derivable type (intermediate base for GI-based languages) */
typedef struct _LrgScriptingGI       LrgScriptingGI;
typedef struct _LrgScriptingGIClass  LrgScriptingGIClass;

/* LrgScriptingPython is a final type - no Class forward declaration needed */
typedef struct _LrgScriptingPython  LrgScriptingPython;

/* LrgScriptingPyGObject is a final type - no Class forward declaration needed */
typedef struct _LrgScriptingPyGObject  LrgScriptingPyGObject;

/* LrgScriptable is an interface for custom script exposure */
typedef struct _LrgScriptable           LrgScriptable;
typedef struct _LrgScriptableInterface  LrgScriptableInterface;

/* LrgScriptMethod is a boxed type for method descriptors */
typedef struct _LrgScriptMethod  LrgScriptMethod;

/* ==========================================================================
 * Economy Module (Phase 2)
 * ========================================================================== */

/* LrgResource is a derivable type */
typedef struct _LrgResource       LrgResource;
typedef struct _LrgResourceClass  LrgResourceClass;

/* LrgResourcePool is a final type - no Class forward declaration needed */
typedef struct _LrgResourcePool  LrgResourcePool;

/* LrgProductionRecipe is a final type - no Class forward declaration needed */
typedef struct _LrgProductionRecipe  LrgProductionRecipe;

/* LrgProducer is a derivable component type */
typedef struct _LrgProducer       LrgProducer;
typedef struct _LrgProducerClass  LrgProducerClass;

/* LrgConsumer is a derivable component type */
typedef struct _LrgConsumer       LrgConsumer;
typedef struct _LrgConsumerClass  LrgConsumerClass;

/* LrgMarket is a final type - no Class forward declaration needed */
typedef struct _LrgMarket  LrgMarket;

/* LrgEconomyManager is a final type (singleton) - no Class forward declaration needed */
typedef struct _LrgEconomyManager  LrgEconomyManager;

/* LrgOfflineCalculator is a final type - no Class forward declaration needed */
typedef struct _LrgOfflineCalculator  LrgOfflineCalculator;

/* ==========================================================================
 * Idle Game Module (Phase 2)
 * ========================================================================== */

/* LrgBigNumber is a boxed type - no class typedef */
typedef struct _LrgBigNumber  LrgBigNumber;

/* LrgIdleCalculator is a final type - no Class forward declaration needed */
typedef struct _LrgIdleCalculator  LrgIdleCalculator;

/* LrgPrestige is a derivable type */
typedef struct _LrgPrestige       LrgPrestige;
typedef struct _LrgPrestigeClass  LrgPrestigeClass;

/* LrgUnlockTree is a final type - no Class forward declaration needed */
typedef struct _LrgUnlockTree  LrgUnlockTree;

/* LrgMilestone is a boxed type - no class typedef */
typedef struct _LrgMilestone  LrgMilestone;

/* LrgAutomation is a final type - no Class forward declaration needed */
typedef struct _LrgAutomation  LrgAutomation;

/* ==========================================================================
 * Building Module (Phase 2)
 * ========================================================================== */

/* LrgBuildingDef is a derivable type */
typedef struct _LrgBuildingDef       LrgBuildingDef;
typedef struct _LrgBuildingDefClass  LrgBuildingDefClass;

/* LrgBuildingInstance is a derivable type */
typedef struct _LrgBuildingInstance       LrgBuildingInstance;
typedef struct _LrgBuildingInstanceClass  LrgBuildingInstanceClass;

/* LrgBuildCell is a boxed type - no class typedef */
typedef struct _LrgBuildCell  LrgBuildCell;

/* LrgBuildGrid is a final type - no Class forward declaration needed */
typedef struct _LrgBuildGrid  LrgBuildGrid;

/* LrgPlacementSystem is a final type - no Class forward declaration needed */
typedef struct _LrgPlacementSystem  LrgPlacementSystem;

/* LrgPlacementGhost is a final type (implements LrgDrawable) - no Class forward declaration needed */
typedef struct _LrgPlacementGhost  LrgPlacementGhost;

/* LrgBuildingUI is a final type (extends LrgContainer) - no Class forward declaration needed */
typedef struct _LrgBuildingUI  LrgBuildingUI;

/* ==========================================================================
 * Vehicle Module (Phase 2)
 * ========================================================================== */

/* LrgVehicle is a derivable type */
typedef struct _LrgVehicle       LrgVehicle;
typedef struct _LrgVehicleClass  LrgVehicleClass;

/* LrgVehicleController is a final type - no Class forward declaration needed */
typedef struct _LrgVehicleController  LrgVehicleController;

/* LrgWheel is a boxed type - no class typedef */
typedef struct _LrgWheel  LrgWheel;

/* LrgVehicleCamera is a derivable type (extends LrgCamera3D) */
typedef struct _LrgVehicleCamera       LrgVehicleCamera;
typedef struct _LrgVehicleCameraClass  LrgVehicleCameraClass;

/* LrgVehicleAudio is a final type - no Class forward declaration needed */
typedef struct _LrgVehicleAudio  LrgVehicleAudio;

/* LrgTrafficAgent is a derivable type */
typedef struct _LrgTrafficAgent       LrgTrafficAgent;
typedef struct _LrgTrafficAgentClass  LrgTrafficAgentClass;

/* LrgRoad is a boxed type - no class typedef */
typedef struct _LrgRoad  LrgRoad;

/* LrgRoadNetwork is a final type - no Class forward declaration needed */
typedef struct _LrgRoadNetwork  LrgRoadNetwork;

/* ==========================================================================
 * Particles Module (Phase 3)
 * ========================================================================== */

/* LrgParticle is a boxed type - no class typedef */
typedef struct _LrgParticle  LrgParticle;

/* LrgParticlePool is a final type - no Class forward declaration needed */
typedef struct _LrgParticlePool  LrgParticlePool;

/* LrgParticleEmitter is a derivable type */
typedef struct _LrgParticleEmitter       LrgParticleEmitter;
typedef struct _LrgParticleEmitterClass  LrgParticleEmitterClass;

/* LrgParticleForce is a derivable type (abstract base) */
typedef struct _LrgParticleForce       LrgParticleForce;
typedef struct _LrgParticleForceClass  LrgParticleForceClass;

/* Force subclasses - all final types */
typedef struct _LrgParticleGravity       LrgParticleGravity;
typedef struct _LrgParticleWind          LrgParticleWind;
typedef struct _LrgParticleAttractor     LrgParticleAttractor;
typedef struct _LrgParticleTurbulence    LrgParticleTurbulence;

/* LrgParticleBackend is an interface */
typedef struct _LrgParticleBackend           LrgParticleBackend;
typedef struct _LrgParticleBackendInterface  LrgParticleBackendInterface;

/* Backend implementations - final types */
typedef struct _LrgParticleBackendCPU  LrgParticleBackendCPU;
typedef struct _LrgParticleBackendGPU  LrgParticleBackendGPU;

/* LrgParticleSystem is a derivable type */
typedef struct _LrgParticleSystem       LrgParticleSystem;
typedef struct _LrgParticleSystemClass  LrgParticleSystemClass;

/* ==========================================================================
 * Post-Processing Module (Phase 3)
 * ========================================================================== */

/* LrgPostEffect is a derivable type (abstract base) */
typedef struct _LrgPostEffect       LrgPostEffect;
typedef struct _LrgPostEffectClass  LrgPostEffectClass;

/* LrgPostProcessor is a derivable type */
typedef struct _LrgPostProcessor       LrgPostProcessor;
typedef struct _LrgPostProcessorClass  LrgPostProcessorClass;

/* Post-processing effects - all final types */
typedef struct _LrgBloom            LrgBloom;
typedef struct _LrgColorGrade       LrgColorGrade;
typedef struct _LrgVignette         LrgVignette;
typedef struct _LrgMotionBlur       LrgMotionBlur;
typedef struct _LrgDOF              LrgDOF;
typedef struct _LrgScreenShake      LrgScreenShake;
typedef struct _LrgColorblindFilter LrgColorblindFilter;
typedef struct _LrgFXAA             LrgFXAA;
typedef struct _LrgFilmGrain        LrgFilmGrain;

/* ==========================================================================
 * Animation Module (Phase 3)
 * ========================================================================== */

/* Boxed types - no class typedef */
typedef struct _LrgBonePose           LrgBonePose;
typedef struct _LrgAnimationKeyframe  LrgAnimationKeyframe;
typedef struct _LrgAnimationEvent     LrgAnimationEvent;

/* LrgBone is a final type - no Class forward declaration needed */
typedef struct _LrgBone  LrgBone;

/* LrgSkeleton is a derivable type */
typedef struct _LrgSkeleton       LrgSkeleton;
typedef struct _LrgSkeletonClass  LrgSkeletonClass;

/* LrgAnimationClip is a derivable type */
typedef struct _LrgAnimationClip       LrgAnimationClip;
typedef struct _LrgAnimationClipClass  LrgAnimationClipClass;

/* LrgAnimator is a derivable type */
typedef struct _LrgAnimator       LrgAnimator;
typedef struct _LrgAnimatorClass  LrgAnimatorClass;

/* LrgAnimationState is a derivable type */
typedef struct _LrgAnimationState       LrgAnimationState;
typedef struct _LrgAnimationStateClass  LrgAnimationStateClass;

/* LrgAnimationTransition is a final type - no Class forward declaration needed */
typedef struct _LrgAnimationTransition  LrgAnimationTransition;

/* LrgAnimationStateMachine is a derivable type */
typedef struct _LrgAnimationStateMachine       LrgAnimationStateMachine;
typedef struct _LrgAnimationStateMachineClass  LrgAnimationStateMachineClass;

/* LrgAnimationLayer is a final type - no Class forward declaration needed */
typedef struct _LrgAnimationLayer  LrgAnimationLayer;

/* LrgBlendTree is a derivable type */
typedef struct _LrgBlendTree       LrgBlendTree;
typedef struct _LrgBlendTreeClass  LrgBlendTreeClass;

/* LrgIKChain is a final type - no Class forward declaration needed */
typedef struct _LrgIKChain  LrgIKChain;

/* LrgIKSolver is a derivable type (abstract base) */
typedef struct _LrgIKSolver       LrgIKSolver;
typedef struct _LrgIKSolverClass  LrgIKSolverClass;

/* IK solver implementations - final types */
typedef struct _LrgIKSolverFABRIK   LrgIKSolverFABRIK;
typedef struct _LrgIKSolverCCD      LrgIKSolverCCD;
typedef struct _LrgIKSolverTwoBone  LrgIKSolverTwoBone;
typedef struct _LrgIKSolverLookAt   LrgIKSolverLookAt;

/* ==========================================================================
 * Rich Text Module (Phase 3)
 * ========================================================================== */

/* LrgFontManager is a final type (singleton) - no Class forward declaration needed */
typedef struct _LrgFontManager  LrgFontManager;

/* LrgSDFFont is a final type - no Class forward declaration needed */
typedef struct _LrgSDFFont  LrgSDFFont;

/* LrgFontStack is a final type - no Class forward declaration needed */
typedef struct _LrgFontStack  LrgFontStack;

/* LrgTextSpan is a boxed type - no class typedef */
typedef struct _LrgTextSpan  LrgTextSpan;

/* LrgRichText is a derivable type */
typedef struct _LrgRichText       LrgRichText;
typedef struct _LrgRichTextClass  LrgRichTextClass;

/* LrgTextEffect is a derivable type */
typedef struct _LrgTextEffect       LrgTextEffect;
typedef struct _LrgTextEffectClass  LrgTextEffectClass;

/* LrgTextLayout is a final type (internal) - no Class forward declaration needed */
typedef struct _LrgTextLayout  LrgTextLayout;

/* ==========================================================================
 * Video Playback Module (Phase 3)
 * ========================================================================== */

/* LrgVideoPlayer is a final type - no Class forward declaration needed */
typedef struct _LrgVideoPlayer  LrgVideoPlayer;

/* LrgVideoTexture is a final type - no Class forward declaration needed */
typedef struct _LrgVideoTexture  LrgVideoTexture;

/* LrgVideoSubtitles is a final type - no Class forward declaration needed */
typedef struct _LrgVideoSubtitles  LrgVideoSubtitles;

/* LrgVideoSubtitleTrack is a final type - no Class forward declaration needed */
typedef struct _LrgVideoSubtitleTrack  LrgVideoSubtitleTrack;

/* ==========================================================================
 * Analytics Module (Phase 5)
 * ========================================================================== */

/* LrgAnalyticsEvent is a final type - no Class forward declaration needed */
typedef struct _LrgAnalyticsEvent  LrgAnalyticsEvent;

/* LrgConsent is a final type - no Class forward declaration needed */
typedef struct _LrgConsent  LrgConsent;

/* LrgAnalyticsBackend is a derivable type */
typedef struct _LrgAnalyticsBackend       LrgAnalyticsBackend;
typedef struct _LrgAnalyticsBackendClass  LrgAnalyticsBackendClass;

/* LrgAnalyticsBackendHttp is a final type - no Class forward declaration needed */
typedef struct _LrgAnalyticsBackendHttp  LrgAnalyticsBackendHttp;

/* LrgAnalytics is a final type (singleton) - no Class forward declaration needed */
typedef struct _LrgAnalytics  LrgAnalytics;

/* ==========================================================================
 * Achievement Module (Phase 5)
 * ========================================================================== */

/* LrgAchievementProgress is a boxed type - no class typedef */
typedef struct _LrgAchievementProgress  LrgAchievementProgress;

/* LrgAchievement is a derivable type */
typedef struct _LrgAchievement       LrgAchievement;
typedef struct _LrgAchievementClass  LrgAchievementClass;

/* LrgAchievementManager is a final type (singleton) - no Class forward declaration needed */
typedef struct _LrgAchievementManager  LrgAchievementManager;

/* LrgAchievementNotification is a final type - no Class forward declaration needed */
typedef struct _LrgAchievementNotification  LrgAchievementNotification;

/* ==========================================================================
 * Photo Mode Module (Phase 5)
 * ========================================================================== */

/* LrgScreenshot is a final type - no Class forward declaration needed */
typedef struct _LrgScreenshot  LrgScreenshot;

/* LrgPhotoCameraController is a final type - no Class forward declaration needed */
typedef struct _LrgPhotoCameraController  LrgPhotoCameraController;

/* LrgPhotoMode is a final type (singleton) - no Class forward declaration needed */
typedef struct _LrgPhotoMode  LrgPhotoMode;

/* ==========================================================================
 * Steam Module Types
 * ========================================================================== */

/* LrgSteamService is an interface */
typedef struct _LrgSteamService           LrgSteamService;
typedef struct _LrgSteamServiceInterface  LrgSteamServiceInterface;

/* LrgSteamClient is a final type - no Class forward declaration needed */
typedef struct _LrgSteamClient  LrgSteamClient;

/* ==========================================================================
 * Steam Workshop Module (Phase 5)
 * ========================================================================== */

/* LrgWorkshopItem is a final type - no Class forward declaration needed */
typedef struct _LrgWorkshopItem  LrgWorkshopItem;

/* LrgWorkshopQuery is a final type - no Class forward declaration needed */
typedef struct _LrgWorkshopQuery  LrgWorkshopQuery;

/* LrgWorkshopManager is a final type - no Class forward declaration needed */
typedef struct _LrgWorkshopManager  LrgWorkshopManager;

/* ============================================================================
 * Demo Module Types
 * ========================================================================== */

/* LrgDemoGatable is an interface */
typedef struct _LrgDemoGatable           LrgDemoGatable;
typedef struct _LrgDemoGatableInterface  LrgDemoGatableInterface;

/* LrgDemoManager is a final type - no Class forward declaration needed */
typedef struct _LrgDemoManager  LrgDemoManager;

/* ============================================================================
 * VR Module Types
 * ========================================================================== */

/* LrgVRService is an interface */
typedef struct _LrgVRService           LrgVRService;
typedef struct _LrgVRServiceInterface  LrgVRServiceInterface;

/* LrgVRStub is a final type - no Class forward declaration needed */
typedef struct _LrgVRStub  LrgVRStub;

/* LrgVRController is a final type - no Class forward declaration needed */
typedef struct _LrgVRController  LrgVRController;

/* LrgVRComfortSettings is a final type - no Class forward declaration needed */
typedef struct _LrgVRComfortSettings  LrgVRComfortSettings;

/* ==========================================================================
 * Deckbuilder Module
 * ========================================================================== */

/* LrgCardDef is a derivable type */
typedef struct _LrgCardDef       LrgCardDef;
typedef struct _LrgCardDefClass  LrgCardDefClass;

/* LrgCardInstance is a final type - no Class forward declaration needed */
typedef struct _LrgCardInstance  LrgCardInstance;

/* LrgCardPile is a final type - no Class forward declaration needed */
typedef struct _LrgCardPile  LrgCardPile;

/* LrgCardHand is a final type - no Class forward declaration needed */
typedef struct _LrgCardHand  LrgCardHand;

/* LrgZone is a final type - no Class forward declaration needed */
typedef struct _LrgZone  LrgZone;

/* LrgDeckDef is a derivable type */
typedef struct _LrgDeckDef       LrgDeckDef;
typedef struct _LrgDeckDefClass  LrgDeckDefClass;

/* LrgDeckCardEntry is a boxed type - no class typedef */
typedef struct _LrgDeckCardEntry  LrgDeckCardEntry;

/* LrgDeckInstance is a final type - no Class forward declaration needed */
typedef struct _LrgDeckInstance  LrgDeckInstance;

/* LrgDeckBuilder is a final type - no Class forward declaration needed */
typedef struct _LrgDeckBuilder  LrgDeckBuilder;

/* LrgCardEffect is a boxed type - no class typedef */
typedef struct _LrgCardEffect  LrgCardEffect;

/* LrgCardEffectExecutor is an interface */
typedef struct _LrgCardEffectExecutor           LrgCardEffectExecutor;
typedef struct _LrgCardEffectExecutorInterface  LrgCardEffectExecutorInterface;

/* LrgCardEffectRegistry is a final type (singleton) - no Class forward declaration needed */
typedef struct _LrgCardEffectRegistry  LrgCardEffectRegistry;

/* LrgEffectStack is a final type - no Class forward declaration needed */
typedef struct _LrgEffectStack  LrgEffectStack;

/* LrgCardEvent is a boxed type - no class typedef */
typedef struct _LrgCardEvent  LrgCardEvent;

/* LrgTriggerListener is an interface */
typedef struct _LrgTriggerListener           LrgTriggerListener;
typedef struct _LrgTriggerListenerInterface  LrgTriggerListenerInterface;

/* LrgCardEventBus is a final type (singleton) - no Class forward declaration needed */
typedef struct _LrgCardEventBus  LrgCardEventBus;

/* LrgCardKeywordDef is a derivable type */
typedef struct _LrgCardKeywordDef       LrgCardKeywordDef;
typedef struct _LrgCardKeywordDefClass  LrgCardKeywordDefClass;

/* LrgCardKeywordRegistry is a final type (singleton) - no Class forward declaration needed */
typedef struct _LrgCardKeywordRegistry  LrgCardKeywordRegistry;

/* LrgSynergy is a derivable type */
typedef struct _LrgSynergy       LrgSynergy;
typedef struct _LrgSynergyClass  LrgSynergyClass;

/* LrgStatusEffectDef is a derivable type */
typedef struct _LrgStatusEffectDef       LrgStatusEffectDef;
typedef struct _LrgStatusEffectDefClass  LrgStatusEffectDefClass;

/* LrgStatusEffectInstance is a boxed type - no class typedef */
typedef struct _LrgStatusEffectInstance  LrgStatusEffectInstance;

/* LrgStatusEffectRegistry is a final type (singleton) - no Class forward declaration needed */
typedef struct _LrgStatusEffectRegistry  LrgStatusEffectRegistry;

/* LrgRelicDef is a derivable type */
typedef struct _LrgRelicDef       LrgRelicDef;
typedef struct _LrgRelicDefClass  LrgRelicDefClass;

/* LrgRelicInstance is a final type - no Class forward declaration needed */
typedef struct _LrgRelicInstance  LrgRelicInstance;

/* LrgRelicRegistry is a final type (singleton) - no Class forward declaration needed */
typedef struct _LrgRelicRegistry  LrgRelicRegistry;

/* LrgPotionDef is a derivable type */
typedef struct _LrgPotionDef       LrgPotionDef;
typedef struct _LrgPotionDefClass  LrgPotionDefClass;

/* LrgPotionInstance is a final type - no Class forward declaration needed */
typedef struct _LrgPotionInstance  LrgPotionInstance;

/* LrgCombatant is an interface */
typedef struct _LrgCombatant           LrgCombatant;
typedef struct _LrgCombatantInterface  LrgCombatantInterface;

/* LrgCombatRules is an interface */
typedef struct _LrgCombatRules           LrgCombatRules;
typedef struct _LrgCombatRulesInterface  LrgCombatRulesInterface;

/* LrgPlayerCombatant is a final type - no Class forward declaration needed */
typedef struct _LrgPlayerCombatant  LrgPlayerCombatant;

/* LrgEnemyDef is a derivable type */
typedef struct _LrgEnemyDef       LrgEnemyDef;
typedef struct _LrgEnemyDefClass  LrgEnemyDefClass;

/* LrgEnemyInstance is a final type - no Class forward declaration needed */
typedef struct _LrgEnemyInstance  LrgEnemyInstance;

/* LrgEnemyIntent is a final type - no Class forward declaration needed */
typedef struct _LrgEnemyIntent  LrgEnemyIntent;

/* LrgCombatContext is a final type - no Class forward declaration needed */
typedef struct _LrgCombatContext  LrgCombatContext;

/* LrgCombatManager is a derivable type */
typedef struct _LrgCombatManager       LrgCombatManager;
typedef struct _LrgCombatManagerClass  LrgCombatManagerClass;

/* LrgRun is a final type - no Class forward declaration needed */
typedef struct _LrgRun  LrgRun;

/* LrgRunConfig is a derivable type */
typedef struct _LrgRunConfig       LrgRunConfig;
typedef struct _LrgRunConfigClass  LrgRunConfigClass;

/* LrgMapNode is a final type - no Class forward declaration needed */
typedef struct _LrgMapNode  LrgMapNode;

/* LrgRunMap is a final type - no Class forward declaration needed */
typedef struct _LrgRunMap  LrgRunMap;

/* LrgRunManager is a final type - no Class forward declaration needed */
typedef struct _LrgRunManager  LrgRunManager;

/* LrgMapGenerator is a derivable type */
typedef struct _LrgMapGenerator       LrgMapGenerator;
typedef struct _LrgMapGeneratorClass  LrgMapGeneratorClass;

/* LrgEncounterPool is a final type - no Class forward declaration needed */
typedef struct _LrgEncounterPool  LrgEncounterPool;

/* LrgCardPool is a final type - no Class forward declaration needed */
typedef struct _LrgCardPool  LrgCardPool;

/* LrgRewardScreen is a final type - no Class forward declaration needed */
typedef struct _LrgRewardScreen  LrgRewardScreen;

/* LrgShop is a final type - no Class forward declaration needed */
typedef struct _LrgShop  LrgShop;

/* LrgEventDef is a derivable type */
typedef struct _LrgEventDef       LrgEventDef;
typedef struct _LrgEventDefClass  LrgEventDefClass;

/* Scoring System (Balatro-style) */

/* LrgScoringHand is a derivable type */
typedef struct _LrgScoringHand       LrgScoringHand;
typedef struct _LrgScoringHandClass  LrgScoringHandClass;

/* LrgScoringContext is a final type - no Class forward declaration needed */
typedef struct _LrgScoringContext  LrgScoringContext;

/* LrgScoringRules is an interface */
typedef struct _LrgScoringRules           LrgScoringRules;
typedef struct _LrgScoringRulesInterface  LrgScoringRulesInterface;

/* LrgJokerDef is a derivable type */
typedef struct _LrgJokerDef       LrgJokerDef;
typedef struct _LrgJokerDefClass  LrgJokerDefClass;

/* LrgJokerInstance is a final type - no Class forward declaration needed */
typedef struct _LrgJokerInstance  LrgJokerInstance;

/* LrgScoringManager is a derivable type */
typedef struct _LrgScoringManager       LrgScoringManager;
typedef struct _LrgScoringManagerClass  LrgScoringManagerClass;

/* Meta-Progression */

/* LrgCharacterDef is a derivable type */
typedef struct _LrgCharacterDef       LrgCharacterDef;
typedef struct _LrgCharacterDefClass  LrgCharacterDefClass;

/* LrgPlayerProfile is a final type - no Class forward declaration needed */
typedef struct _LrgPlayerProfile  LrgPlayerProfile;

/* LrgUnlockDef is a derivable type */
typedef struct _LrgUnlockDef       LrgUnlockDef;
typedef struct _LrgUnlockDefClass  LrgUnlockDefClass;

/* LrgAscension is a final type - no Class forward declaration needed */
typedef struct _LrgAscension  LrgAscension;

/* LrgDeckbuilderManager is a final type (singleton) - no Class forward declaration needed */
typedef struct _LrgDeckbuilderManager  LrgDeckbuilderManager;

G_END_DECLS
