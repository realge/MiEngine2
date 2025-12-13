# Milestone 4: Model Caching and Asset Browser (2025-12-01)

**Goal:** Binary mesh caching system and Asset Browser UI for importing/managing project assets.

## New Files
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

## Modified Files
- `VulkanRenderer.h/cpp` - Added AssetBrowserWindow member and initialization
- `src/debug/DebugUIManager.cpp` - Added Assets menu to main menu bar

## Binary Cache Format (.mimesh)
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

## Asset Registry (asset_registry.json)
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

## Features Implemented
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

## Asset Browser UI
- Accessible via Assets menu in main menu bar (Ctrl+Shift+A)
- Table view with Name, Type, Status, Path columns
- Search box for filtering by name
- Type filter dropdown (All/Static Mesh/Skeletal Mesh)
- Footer with selection details and action buttons
- Double-click to add asset to scene

## Usage
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

## Architecture Notes
- MeshCache handles binary serialization only (no caching logic)
- AssetRegistry is the source of truth for asset metadata
- MeshLibrary provides runtime deduplication (same path = same mesh)
- AssetBrowserWindow is standalone ImGui window (not a DebugPanel)
- Cache files stored in project's Cache/ directory

## TODO (Future Enhancements)
- Thumbnail generation and display in Asset Browser
- Drag-and-drop import
- Batch import/reimport
- Asset dependency tracking
- Texture asset support
