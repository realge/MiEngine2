# Milestone 6: Actor Spawning UI

## Goal
Allow users to spawn MiActors with 3D models directly from the editor UI, enabling testing of the Actor System without code.

## Overview
Add UI panels that let users:
1. Browse available actor types
2. Spawn actors into the world
3. Assign meshes from the Asset Browser
4. Edit actor properties (transform, material)

---

## Phase 1: Actor Spawner Panel (Core)

### New Files
```
include/debug/ActorSpawnerPanel.h
src/debug/ActorSpawnerPanel.cpp
```

### Features
- **Actor Type List**: Show all registered MiActor types from MiTypeRegistry
- **Spawn Button**: Create actor at origin or camera position
- **Mesh Assignment**: Dropdown/browser to select mesh from AssetRegistry
- **Quick Spawn**: Predefined actors (Empty, StaticMesh, Light, Camera)

### UI Layout
```
+----------------------------------+
| Actor Spawner                    |
+----------------------------------+
| [Quick Spawn]                    |
|   [Empty Actor] [Static Mesh]    |
|   [Point Light] [Camera]         |
+----------------------------------+
| Actor Type: [MiStaticMeshActor v]|
| Mesh:       [cube.fbx        v]  |
| Position:   [0.0] [0.0] [0.0]    |
|                                  |
| [Spawn at Origin] [Spawn at Cam] |
+----------------------------------+
```

---

## Phase 2: World Outliner Panel

### New Files
```
include/debug/WorldOutlinerPanel.h
src/debug/WorldOutlinerPanel.cpp
```

### Features
- **Actor Tree View**: List all actors in MiWorld
- **Selection**: Click to select actor
- **Context Menu**: Delete, Duplicate, Rename
- **Drag & Drop**: Reparent actors (future)
- **Search/Filter**: Find actors by name/tag

### UI Layout
```
+----------------------------------+
| World Outliner                   |
+----------------------------------+
| [Search...                     ] |
+----------------------------------+
| > MainWorld                      |
|   - Floor (MiStaticMeshActor)    |
|   - Player (MiStaticMeshActor)   |
|   - SpawnPoint_0 (MiEmptyActor)  |
|   - SpawnPoint_1 (MiEmptyActor)  |
|   - PointLight_0 (MiLightActor)  |
+----------------------------------+
| Actors: 5 | Selected: Player     |
+----------------------------------+
```

---

## Phase 3: Actor Details Panel

### New Files
```
include/debug/ActorDetailsPanel.h
src/debug/ActorDetailsPanel.cpp
```

### Features
- **Transform Editor**: Position, Rotation (euler), Scale with drag
- **Component List**: Show all components on selected actor
- **Material Editor**: For MiStaticMeshComponent (color, metallic, roughness)
- **Mesh Selector**: Change mesh on MiStaticMeshComponent
- **Flags/Tags Editor**: Edit actor flags and tags

### UI Layout
```
+----------------------------------+
| Actor Details                    |
+----------------------------------+
| Name: [Player                  ] |
| Type: MiStaticMeshActor          |
+----------------------------------+
| Transform                        |
|   Position: [X] [Y] [Z]          |
|   Rotation: [X] [Y] [Z] (deg)    |
|   Scale:    [X] [Y] [Z]          |
+----------------------------------+
| Components                       |
|   > StaticMeshComponent          |
|       Mesh: [character.fbx   v]  |
|       Cast Shadows: [x]          |
|       Material:                  |
|         Base Color: [###]        |
|         Metallic:   [====]  0.5  |
|         Roughness:  [====]  0.5  |
+----------------------------------+
| Tags: [enemy] [+]                |
| Layer: [0    v]                  |
+----------------------------------+
```

---

## Phase 4: Integration

### Modifications
- **DebugUIManager**: Add new panels to menu
- **VulkanRenderer**: Connect panels to MiWorld
- **Input System**: Handle actor selection via mouse picking

### Workflow
1. User opens Actor Spawner panel
2. Selects "Static Mesh" quick spawn
3. Picks mesh from dropdown (populated from AssetRegistry)
4. Clicks "Spawn at Camera"
5. Actor appears in World Outliner
6. User selects actor, edits in Details Panel
7. Changes reflect in 3D view immediately

---

## Implementation Order

### Step 1: ActorSpawnerPanel (simplest, enables testing)
- Register panel in DebugUIManager
- List actor types from MiTypeRegistry
- Spawn basic actors with default mesh

### Step 2: WorldOutlinerPanel (see what's spawned)
- List actors from MiWorld
- Basic selection (stored in panel state)
- Delete selected actor

### Step 3: ActorDetailsPanel (edit selected)
- Transform editing with immediate feedback
- Material property sliders
- Mesh dropdown from AssetRegistry

### Step 4: Polish
- Mouse picking in viewport
- Gizmos for transform (future milestone)
- Undo/Redo (future milestone)

---

## Dependencies
- MiWorld must be initialized in VulkanRenderer ✓
- MeshLibrary for mesh loading ✓
- AssetRegistry for mesh list ✓
- MiTypeRegistry for actor type list ✓

---

## Estimated Scope
- Phase 1: ~200 lines
- Phase 2: ~250 lines
- Phase 3: ~350 lines
- Phase 4: ~100 lines modifications

Total: ~900 lines of new code

---

## Future Enhancements (Not in this milestone)
- Transform gizmos (translate/rotate/scale handles)
- Multi-selection
- Copy/Paste actors
- Prefab system
- Undo/Redo stack
