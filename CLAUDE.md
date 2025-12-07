# Claude Code Context Management

## Project Overview
MiEngine2 is a Vulkan-based 3D rendering engine with PBR support, IBL system, and component-based physics.

## Key Components
- **VulkanRenderer**: Core rendering system
- **IBLSystem**: Image-based lighting implementation
- **Material System**: PBR material handling
- **Debug Panels**: ImGui-based debugging interface
- **Physics System**: Component-based rigid body physics (selective per-object)
- **Project System**: Project management with separate engine/project asset paths
- **Asset System**: Binary mesh caching, asset registry, and Asset Browser UI
- **Actor System**: UE5-inspired Actor/Component architecture with scene serialization
- **Ray Tracing System**: Hardware RT for reflections and soft shadows (VK_KHR_ray_tracing_pipeline)

## Build Commands
```bash
# Build project in Visual Studio
# Open MiEngine2.sln and build in Debug/Release configuration
```

## Testing Commands
```bash
# Run executable
./x64/Debug/MiEngine2.exe
```

## Code Conventions
- C++ with modern standards
- Vulkan API usage
- ImGui for debug UI
- PBR shader implementation

## Important Files
- `VulkanRenderer.cpp/h`: Main rendering logic
- `IBLSystem.cpp/h`: Image-based lighting
- `shaders/`: GLSL shaders and SPIR-V binaries
- `Material.cpp/h`: PBR material system
- `include/physics/`: Physics system headers
- `src/physics/`: Physics system implementation
- `include/project/`: Project system headers
- `src/project/`: Project system implementation
- `include/asset/`: Asset system headers (MeshCache, AssetRegistry, AssetImporter, MeshLibrary, AssetBrowserWindow)
- `src/asset/`: Asset system implementation
- `include/core/`: Core actor system (MiObject, MiActor, MiComponent, MiWorld, etc.)
- `src/core/`: Core actor system implementation
- `include/actor/`: Actor types (MiEmptyActor, MiStaticMeshActor, etc.)
- `src/actor/`: Actor implementations
- `include/component/`: Component types (MiStaticMeshComponent, etc.)
- `src/component/`: Component implementations
- `include/raytracing/`: Ray tracing system headers (RayTracingSystem, RayTracingTypes)
- `src/raytracing/`: Ray tracing system implementation
- `shaders/raytracing/`: RT shaders (raygen, closesthit, miss)
- `include/scene/SceneSerializer.h`: Scene save/load API

## Dependencies
- Vulkan SDK
- ImGui
- FBX SDK
- vcpkg managed libraries

---

## Milestones / Changelog

### Milestone 1: Physics Foundation (2025-11-29)
**Goal:** Component-based physics system foundation allowing selective physics on MeshInstances.

**New Files:**
```
include/physics/
├── Component.h           - Base component class with ComponentType enum
├── ColliderComponent.h   - AABB/Sphere collider shapes
├── RigidBodyComponent.h  - Rigid body dynamics (mass, velocity, forces)
└── PhysicsWorld.h        - Physics simulation manager

src/physics/
├── ColliderComponent.cpp - World-space bounds, layer filtering
├── RigidBodyComponent.cpp - Force/impulse application
└── PhysicsWorld.cpp      - Fixed timestep update loop
```

**Modified Files:**
- `include/scene/Scene.h` - Added component pointers to MeshInstance, PhysicsWorld member
- `src/scene/Scene.cpp` - Added `enablePhysics()`, physics update integration

**Features Implemented:**
- Optional physics via `shared_ptr` components (nullptr = no physics overhead)
- `Scene::enablePhysics(index, bodyType)` helper for easy setup
- RigidBodyType: Dynamic, Kinematic, Static
- Gravity application with per-object gravity scale
- Force and impulse API
- Position constraints (lock X/Y/Z axes)
- Collision layer bitmask filtering (structure only)
- Fixed timestep physics (60 Hz default)

**Usage:**
```cpp
// Enable physics on a mesh instance
m_Scene->enablePhysics(0, RigidBodyType::Dynamic);

// Access and customize
auto* obj = m_Scene->getMeshInstance(0);
obj->rigidBody->mass = 2.0f;
obj->rigidBody->addImpulse({0, 10, 0});
```

**TODO (Future Milestones):**
- Collision detection (AABB-AABB, Sphere-Sphere, AABB-Sphere)
- Collision response (impulse-based resolution)
- Raycast and spatial queries
- Debug visualization panel
---

### Milestone 2: Skeletal Animation System (2025-11-30)
**Goal:** GPU-based skeletal animation with FBX skeleton/animation extraction.

**New Files:**
```
include/animation/
├── Skeleton.h              - Bone hierarchy, inverse bind poses
├── AnimationClip.h         - Keyframe data, sampling
└── SkeletalMeshComponent.h - Per-instance animation state

src/animation/
├── Skeleton.cpp            - Global pose computation
├── AnimationClip.cpp       - Keyframe interpolation (position, rotation, scale)
└── SkeletalMeshComponent.cpp - Animation playback, bone matrix update

include/Utils/
└── SkeletalVertex.h        - Extended vertex with bone indices/weights

include/mesh/
└── SkeletalMesh.h          - GPU mesh for skeletal vertices

src/mesh/
└── SkeletalMesh.cpp        - Buffer creation for SkeletalVertex

src/loader/
└── SkeletalModelLoader.cpp - FBX skeleton/skinning/animation extraction

src/scene/
└── SceneSkeletal.cpp       - Scene integration for skeletal models

shaders/
└── skeletal.vert           - GPU skinning vertex shader
```

**Modified Files:**
- `include/loader/ModelLoader.h` - Added SkeletalMeshData, SkeletalModelData structs
- `include/mesh/Mesh.h` - Added virtual methods for derived SkeletalMesh
- `include/scene/Scene.h` - Added skeletal mesh component, loadSkeletalModel()

**Architecture:**
- **Skeleton**: Stores bones in flat array, computes global poses from local poses
- **AnimationClip**: Separate position/rotation/scale tracks per bone
- **SkeletalMeshComponent**: Per-instance playback state, caches final bone matrices
- **SkeletalVertex**: 92 bytes (60 base + 16 boneIndices + 16 boneWeights)
- **GPU Skinning**: 4 bone influences per vertex, weighted matrix blending

**Usage:**
```cpp
// Load skeletal model with animations
m_Scene->loadSkeletalModel("character.fbx", transform);

// Play animation on a skeletal mesh instance
m_Scene->playAnimation(instanceIndex, animationIndex, loop);

// Or direct control
auto* instance = m_Scene->getMeshInstance(0);
if (instance->skeletalMesh) {
    instance->skeletalMesh->playAnimation(clip, true);
    instance->skeletalMesh->setPlaybackSpeed(1.5f);
}
```

**Shader Integration:**
- `skeletal.vert` extends `pbr.vert` with bone matrix skinning
- Bone matrices passed via UBO (set 1, binding 0)
- Max 256 bones supported
- Compiled to `skeletal.vert.spv`

**Rendering Integration (Complete):**
- Skeletal rendering pipeline in VulkanRenderer (createSkeletalPipeline)
- Bone matrix descriptor set layout (set 1) for per-instance bone data
- Per-instance bone matrix UBO management (createSkeletalInstanceResources)
- Scene::draw() detects skeletal instances and uses skeletal pipeline
- Scene::update() updates skeletal animation state each frame

**TODO (Future Enhancements):**
- Animation blending between clips
- Debug bone visualization
- Ragdoll physics integration

---

### Milestone 3: Project System (2025-12-01)
**Goal:** Project management system separating engine code from user project code.

**New Files:**
```
include/project/
├── Project.h           - Project metadata, paths, and state
├── ProjectManager.h    - Singleton for project operations (create/open/save)
├── ProjectSerializer.h - JSON serialization for project files
└── ProjectLauncher.h   - ImGui-based project launcher window

src/project/
├── Project.cpp         - Project path resolution
├── ProjectManager.cpp  - Project lifecycle, recent projects, asset resolution
├── ProjectSerializer.cpp - JSON read/write for .miproj files
└── ProjectLauncher.cpp - Vulkan/ImGui launcher window
```

**Modified Files:**
- `include/core/Application.h` - ProjectManager initialization with engine path
- `src/main.cpp` - Project launcher integration, command-line arguments

**Features Implemented:**
- **Project Launcher Window**: ImGui-based GUI shown on startup
  - Recent projects list with quick access
  - Create new project dialog with folder browser
  - Open existing project via file dialog
  - Missing project detection and cleanup
- **Project Structure**: Standardized directory layout
  ```
  MyProject/
  ├── MyProject.miproj    - Project file (JSON)
  ├── Assets/
  │   ├── Models/
  │   ├── Textures/
  │   ├── Shaders/
  │   ├── HDR/
  │   └── Audio/
  ├── Scenes/
  ├── Scripts/
  ├── Config/
  └── Cache/
  ```
- **Asset Resolution**: Project assets override engine assets
- **Recent Projects**: Stored in `%LOCALAPPDATA%/MiEngine2/recent_projects.json`
- **Command-Line Support**:
  - `-s, --skip-launcher`: Skip project launcher
  - `-p, --project PATH`: Open project directly
  - `-m, --mode N`: Start in specific game mode

**Project File Format (.miproj):**
```json
{
  "name": "MyProject",
  "description": "Project description",
  "version": "1.0.0",
  "engineVersion": "2.0.0",
  "author": "Developer Name",
  "createdAt": "2025-12-01T10:30:00",
  "modifiedAt": "2025-12-01T10:30:00",
  "recentScenes": []
}
```

**Usage:**
```cpp
// Access current project
auto& pm = ProjectManager::getInstance();
if (pm.hasProject()) {
    auto* project = pm.getCurrentProject();
    std::cout << "Project: " << project->getName() << std::endl;
    std::cout << "Models path: " << project->getModelsPath() << std::endl;
}

// Resolve assets (checks project first, then engine)
auto modelPath = pm.resolveModelPath("character.fbx");
if (modelPath) {
    // Use *modelPath
}

// Create new project programmatically
pm.createProject("NewGame", "C:/Projects");
```

**Command-Line Examples:**
```bash
# Normal launch with project launcher
MiEngine2.exe

# Skip launcher and use console mode selection
MiEngine2.exe --skip-launcher

# Open specific project
MiEngine2.exe --project "C:/Projects/MyGame/MyGame.miproj"

# Open project in specific mode
MiEngine2.exe -p "path/to/project.miproj" -m 2
```

**Architecture Notes:**
- ProjectManager is a singleton for global project access
- ProjectLauncher creates its own minimal Vulkan context for ImGui
- Engine path set from working directory at startup
- Asset resolution prioritizes project assets over engine defaults

**TODO (Future Enhancements):**
- Scene serialization and loading
- Project settings editor in debug UI
- Project templates

---

### Milestone 4: Model Caching and Asset Browser (2025-12-01)
**Goal:** Binary mesh caching system and Asset Browser UI for importing/managing project assets.

**New Files:**
```
include/asset/
├── AssetTypes.h          - AssetType enum, AssetEntry struct, common definitions
├── MeshCache.h           - Binary .mimesh format headers and API
├── AssetRegistry.h       - Asset database singleton with JSON persistence
├── AssetImporter.h       - Import workflow: copy, parse, cache, register
├── MeshLibrary.h         - Runtime mesh deduplication via weak_ptr cache
└── AssetBrowserWindow.h  - ImGui Asset Browser window

src/asset/
├── MeshCache.cpp         - Binary serialization for static/skeletal meshes
├── AssetRegistry.cpp     - UUID generation, JSON save/load, index management
├── AssetImporter.cpp     - File dialog, FBX parsing, cache generation
├── MeshLibrary.cpp       - Cache-aware mesh loading with fallback to FBX
└── AssetBrowserWindow.cpp - Full ImGui UI implementation
```

**Modified Files:**
- `VulkanRenderer.h/cpp` - Added AssetBrowserWindow member and initialization
- `src/debug/DebugUIManager.cpp` - Added Assets menu to main menu bar

**Binary Cache Format (.mimesh):**
```cpp
struct MeshCacheHeader {
    char magic[8];              // "MIMESH01"
    uint32_t version;           // Format version
    uint32_t flags;             // Static/Skeletal mesh flags
    uint64_t sourceFileHash;    // For cache invalidation
    uint64_t sourceModTime;     // Source file modification time
    uint32_t meshCount;         // Number of mesh chunks
    uint32_t boneCount;         // Bones (skeletal only)
    uint32_t animationCount;    // Animations (skeletal only)
    uint32_t reserved[4];
};
// Followed by MeshChunkHeaders + vertex/index data
// For skeletal: BoneChunkHeaders + AnimationChunkHeaders
```

**Asset Registry (asset_registry.json):**
```json
{
  "version": 1,
  "assets": [
    {
      "uuid": "550e8400-e29b-41d4-a716-446655440000",
      "name": "character",
      "projectPath": "Models/character.fbx",
      "cachePath": "Cache/character_550e84.mimesh",
      "type": 2,
      "importTime": 1701432000,
      "sourceModTime": 1701431000,
      "cacheValid": true
    }
  ]
}
```

**Features Implemented:**
- **Binary Mesh Cache**: Serializes vertices, indices, bones, and animations
- **Cache Invalidation**: Validates via source file hash and modification time
- **Asset Registry**: JSON database tracking all imported assets
- **UUID Generation**: Unique identifiers using std::random_device
- **Asset Importer**: Windows file dialog, copies to project, generates cache
- **MeshLibrary**: Runtime weak_ptr cache for mesh deduplication
- **Asset Browser UI**: Main menu access, list view, search, filter by type
  - Import Model button with file dialog
  - Add to Scene functionality
  - Reimport and Delete actions
  - Context menu support

**Asset Browser UI:**
- Accessible via Assets menu in main menu bar (Ctrl+Shift+A)
- Table view with Name, Type, Status, Path columns
- Search box for filtering by name
- Type filter dropdown (All/Static Mesh/Skeletal Mesh)
- Footer with selection details and action buttons
- Double-click to add asset to scene

**Usage:**
```cpp
// Import via UI
// Assets menu -> Import Model... -> Select FBX file

// Programmatic import
std::string uuid = MiEngine::AssetImporter::importModel("path/to/model.fbx");

// Access mesh through MeshLibrary (cache-aware)
auto& meshLib = renderer->getMeshLibrary();
auto mesh = meshLib.getMesh("Models/character.fbx");  // Loads from .mimesh if valid

// Query asset registry
auto& registry = MiEngine::AssetRegistry::getInstance();
const auto* entry = registry.findByUuid(uuid);
if (entry && entry->cacheValid) {
    // Asset is cached and ready
}
```

**Architecture Notes:**
- MeshCache handles binary serialization only (no caching logic)
- AssetRegistry is the source of truth for asset metadata
- MeshLibrary provides runtime deduplication (same path = same mesh)
- AssetBrowserWindow is standalone ImGui window (not a DebugPanel)
- Cache files stored in project's Cache/ directory

**TODO (Future Enhancements):**
- Thumbnail generation and display in Asset Browser
- Drag-and-drop import
- Batch import/reimport
- Asset dependency tracking
- Texture asset support

---

### Milestone 5: Actor System and Scene Serialization (2025-12-02)
**Goal:** UE5-inspired Actor/Component architecture with scene save/load support.

**See:** `MILESTONE_5_ACTOR_SCENE_SYSTEM.md` for full design documentation.

**New Files:**
```
include/core/
├── MiObject.h              - Base class with UUID, name, type info
├── MiActor.h               - Base actor class with components
├── MiComponent.h           - Base component class
├── MiSceneComponent.h      - Transform-based component with hierarchy
├── MiWorld.h               - World container for actors
├── MiTransform.h           - Transform with quaternion rotation
├── MiDelegate.h            - Event/delegate system
├── MiTypeRegistry.h        - Runtime type registration
├── JsonIO.h                - JSON serialization utilities
└── MiCore.h                - Convenience header

include/actor/
├── MiEmptyActor.h          - Empty grouping actor
└── MiStaticMeshActor.h     - Static mesh actor

include/component/
└── MiStaticMeshComponent.h - Static mesh rendering component

include/scene/
└── SceneSerializer.h       - Scene save/load API

src/core/
├── MiObject.cpp
├── MiActor.cpp
├── MiComponent.cpp
├── MiSceneComponent.cpp
├── MiWorld.cpp
├── MiTransform.cpp
├── MiTypeRegistry.cpp
└── JsonIO.cpp

src/actor/
├── MiEmptyActor.cpp
└── MiStaticMeshActor.cpp

src/component/
└── MiStaticMeshComponent.cpp

src/scene/
└── SceneSerializer.cpp
```

**Features Implemented:**
- **MiObject**: Base class with UUID, name, dirty tracking, serialization
- **MiActor**: Component management, transform, tags, layers, flags
- **MiComponent**: Lifecycle callbacks (onAttached, beginPlay, tick, etc.)
- **MiSceneComponent**: Hierarchical transforms, parent/child relationships
- **MiWorld**: Actor spawning, queries, update loop, serialization
- **MiTransform**: Quaternion-based rotation, matrix operations
- **MiTypeRegistry**: Runtime type creation by name (for deserialization)
- **MiDelegate**: Single and multicast delegates for events
- **SceneSerializer**: .miscene JSON format save/load

**Usage:**
```cpp
// Create world and spawn actors
MiWorld world;
world.initialize(renderer);

auto cube = world.spawnActor<MiStaticMeshActor>();
cube->setName("Floor");
cube->setPosition({0, -1, 0});
cube->setScale({10, 0.5f, 10});
cube->setMesh("Models/cube.fbx");

auto empty = world.spawnActor<MiEmptyActor>();
empty->setName("Waypoint");
empty->addTag("spawn_point");

// Update loop
world.beginPlay();
while (running) {
    world.tick(deltaTime);
}
world.endPlay();

// Save scene
SceneSerializer::saveScene(world, "Scenes/Level1.miscene");

// Load scene
MiWorld newWorld;
newWorld.initialize(renderer);
SceneSerializer::loadScene(newWorld, "Scenes/Level1.miscene");
```

**Scene File Format (.miscene):**
```json
{
  "version": 1,
  "name": "Level1",
  "settings": {
    "gravity": [0, -9.81, 0],
    "ambientColor": [0.1, 0.1, 0.1]
  },
  "actors": [
    {
      "type": "MiStaticMeshActor",
      "id": "uuid-here",
      "name": "Floor",
      "transform": {
        "position": [0, -1, 0],
        "rotation": [1, 0, 0, 0],
        "scale": [10, 0.5, 10]
      },
      "components": [...]
    }
  ]
}
```

**Architecture Notes:**
- `MI_OBJECT_BODY(TypeName, TypeId)` macro for RTTI
- `MI_REGISTER_TYPE(TypeName)` macro for auto-registration
- Template-based component management (`addComponent<T>()`, `getComponent<T>()`)
- Deferred actor destruction (processed at end of frame)
- Type ID ranges: 100-199 actors, 200-299 components, 1000+ game-specific

**TODO (Future Phases):**
- MiSkeletalMeshActor and MiSkeletalMeshComponent
- MiLightActor (Point, Directional, Spot)
- MiCameraActor
- Physics component refactor (MiRigidBodyComponent, MiColliderComponent)
- World Outliner debug panel
- Actor Details debug panel
- Integration with existing Scene class

---

### Milestone 6: Hardware Ray Tracing System (2025-12-03 to 2025-12-04)
**Goal:** Real-time ray-traced reflections and soft shadows using VK_KHR_ray_tracing_pipeline.

**New Files:**
```
include/raytracing/
├── RayTracingSystem.h      - Main RT system (BLAS/TLAS, pipeline, rendering)
├── RayTracingTypes.h       - Structs: RTSettings, BLASInfo, TLASInfo, RTMaterialData

src/raytracing/
└── RayTracingSystem.cpp    - Core implementation (~2700 lines)

shaders/raytracing/
├── raygen.rgen             - Ray generation (reflections + shadow rays)
├── closesthit.rchit        - Closest hit (material eval, geometric normals)
├── miss.rmiss              - Miss shader (IBL environment sampling)
├── miss_shadow.rmiss       - Shadow miss (point is lit)
├── rt_common.glsl          - Shared utilities (RNG, sampling, Fresnel)
├── denoise_temporal.comp   - Temporal accumulation
└── denoise_spatial.comp    - Spatial bilateral filter

include/debug/
└── RayTracingDebugPanel.h  - ImGui RT controls

src/debug/
└── RayTracingDebugPanel.cpp

src/Games/RayTracingTest/
└── RayTracingTestGame.h    - RT test scene with reflective materials
```

**Modified Files:**
- `VulkanRenderer.h/cpp` - RT extensions, features, hybrid integration
- `shaders/pbr.frag` - RT output sampling, fallback ambient lighting

**Features Implemented:**
- **Acceleration Structures**: BLAS per-mesh, TLAS per-frame rebuild
- **RT Pipeline**: raygen, closesthit, 2 miss shaders (reflection + shadow)
- **Shader Binding Table**: Proper alignment and region setup
- **Per-Instance Materials**: Material buffer (binding 5) with base color, metallic, roughness
- **Soft Shadows**: Cone sampling for penumbra, tMin-based bias to prevent self-shadowing
- **Denoising**: Temporal accumulation + spatial bilateral filter (optional)
- **Hybrid Rendering**: RT outputs blended with rasterized PBR
- **Debug Panel**: Enable/disable, SPP, bounces, bias controls, debug modes
- **Fallback Ambient**: Hemisphere lighting when IBL is disabled

**RT Settings (RTSettings struct):**
```cpp
bool enabled = false;
int samplesPerPixel = 1;        // 1-4 for real-time
int maxBounces = 2;
float reflectionBias = 0.001f;
float shadowBias = 0.05f;       // tMin for shadow rays
bool enableReflections = true;
bool enableSoftShadows = true;
float shadowSoftness = 0.02f;   // Cone angle for soft shadows
bool enableDenoising = true;
int debugMode = 0;              // 0=off, 1=normals, 2=depth, etc.
```

**Shadow Ray Implementation:**
- Uses `tMin` parameter as bias instead of position offset
- More robust for curved surfaces (spheres)
- shadowBias slider range: 0.001 - 0.5

**PBR Integration:**
- Set 5: RT outputs (rtReflections, rtShadows)
- Push constants: useRTReflections, useRTShadows (separate flags)
- Fallback ambient when IBL disabled (hemisphere lighting)
- Debug layer 15: RT Shadow visualization

**Debug Modes (raygen.rgen):**
- 0: Off (normal rendering)
- 1: Normals
- 2: Depth (hit distance)
- 3: Metallic
- 4: Roughness
- 5: Reflections only
- 6: Shadows only

**Known Limitations:**
- Geometric normals computed via heuristic (sphere detection by distance from center)
- No vertex buffer access in closesthit - proper normals would require bindless vertex data
- Denoiser output selection based on settings (raw vs denoised)

**Bug Fixes Applied:**
1. Fixed NdotL check that inverted shadows (removed - PBR handles lighting attenuation)
2. Fixed denoised output not being used (descriptor set now selects based on settings)
3. Added fallback ambient lighting when IBL is off
4. Fixed shadow bias - changed from position offset to tMin parameter
5. Fixed debug mode mismatch between panel and shader
6. Added separate useRTReflections/useRTShadows push constant flags

**Usage:**
```cpp
// Enable RT in test game
MiEngine::RayTracingSystem* rtSystem = m_Renderer->getRayTracingSystem();
rtSystem->getSettings().enabled = true;
rtSystem->getSettings().enableReflections = true;
rtSystem->getSettings().enableSoftShadows = true;

// Adjust shadow bias if seeing self-shadowing artifacts
rtSystem->getSettings().shadowBias = 0.1f;
```

**TODO (Future Enhancements):**
- Bindless vertex buffer access for proper interpolated normals
- Global illumination (multi-bounce indirect lighting)
- Area light sampling
- Performance profiling GPU timestamps

