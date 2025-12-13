# Milestone 3: Project System (2025-12-01)

**Goal:** Project management system separating engine code from user project code.

## New Files
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

## Modified Files
- `include/core/Application.h` - ProjectManager initialization with engine path
- `src/main.cpp` - Project launcher integration, command-line arguments

## Features Implemented

### Project Launcher Window
- ImGui-based GUI shown on startup
- Recent projects list with quick access
- Create new project dialog with folder browser
- Open existing project via file dialog
- Missing project detection and cleanup

### Project Structure
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

### Other Features
- **Asset Resolution**: Project assets override engine assets
- **Recent Projects**: Stored in `%LOCALAPPDATA%/MiEngine2/recent_projects.json`
- **Command-Line Support**:
  - `-s, --skip-launcher`: Skip project launcher
  - `-p, --project PATH`: Open project directly
  - `-m, --mode N`: Start in specific game mode

## Project File Format (.miproj)
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

## Usage
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

## Command-Line Examples
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

## Architecture Notes
- ProjectManager is a singleton for global project access
- ProjectLauncher creates its own minimal Vulkan context for ImGui
- Engine path set from working directory at startup
- Asset resolution prioritizes project assets over engine defaults

## TODO (Future Enhancements)
- Scene serialization and loading
- Project settings editor in debug UI
- Project templates
