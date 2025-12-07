#include "core/Application.h"
#include "project/ProjectLauncher.h"
#include "project/ProjectManager.h"
#include "Games/FlappyBird/FlappyBirdGame.h"
#include "Games/Editor/EditorGame.h"
#include "Games/Minecraft/MinecraftGame.h"
#include "Games/ShadowTest/ShadowTestGame.h"
#include "Games/PointLightTest/PointLightTestGame.h"
#include "Games/WaterTest/WaterTestGame.h"
#include "Games/DrawCallTest/DrawCallTestGame.h"
#include "Games/PhysicsTest/PhysicsTestGame.h"
#include "Games/SkeletalAnimationTest/SkeletalAnimationTestGame.h"
#include "Games/RayTracingTest/RayTracingTestGame.h"
#include "Games/VirtualGeoTest/VirtualGeoTestGame.h"
#include <iostream>
#include <limits>
#include <stdlib.h>
#include <crtdbg.h>

// Command line arguments
struct LaunchArgs {
    bool skipLauncher = false;
    std::string projectPath;
    int gameMode = 2; // Default to Editor
};

LaunchArgs parseArgs(int argc, char* argv[]) {
    LaunchArgs args;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--skip-launcher" || arg == "-s") {
            args.skipLauncher = true;
        } else if ((arg == "--project" || arg == "-p") && i + 1 < argc) {
            args.projectPath = argv[++i];
            args.skipLauncher = true; // Skip launcher if project specified
        } else if ((arg == "--mode" || arg == "-m") && i + 1 < argc) {
            args.gameMode = std::atoi(argv[++i]);
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "MiEngine2 Usage:\n";
            std::cout << "  -s, --skip-launcher  Skip project launcher\n";
            std::cout << "  -p, --project PATH   Open project at PATH\n";
            std::cout << "  -m, --mode N         Start in mode N (1-10)\n";
            std::cout << "  -h, --help           Show this help\n";
            std::cout << "\nGame Modes:\n";
            std::cout << "  1. Flappy Bird\n";
            std::cout << "  2. Editor Mode (default)\n";
            std::cout << "  3. Minecraft Mode\n";
            std::cout << "  4. Shadow Test\n";
            std::cout << "  5. Point Light Test\n";
            std::cout << "  6. Water Test\n";
            std::cout << "  7. Draw Call Test\n";
            std::cout << "  8. Physics Test\n";
            std::cout << "  9. Skeletal Animation Test\n";
            std::cout << "  10. Ray Tracing Test (RTX)\n";
            std::cout << "  11. Virtual Geo Test (Clustering)\n";
            exit(0);
        }
    }
    return args;
}

int selectGameMode() {
    std::cout << "\nSelect Mode:\n";
    std::cout << "1. Play Flappy Bird\n";
    std::cout << "2. Editor Mode (Material Tester)\n";
    std::cout << "3. Minecraft Mode\n";
    std::cout << "4. Shadow Test Mode\n";
    std::cout << "5. Point Light Test Mode\n";
    std::cout << "6. Water Test Mode\n";
    std::cout << "7. Draw Call Stress Test (10K+ draws)\n";
    std::cout << "8. Physics Test (Gravity & Impulse)\n";
    std::cout << "9. Skeletal Animation Test\n";
    std::cout << "10. Ray Tracing Test (RTX)\n";
    std::cout << "11. Virtual Geo Test (Clustering)\n";
    std::cout << "Enter choice (1-11): ";

    int choice;
    if (!(std::cin >> choice)) {
        std::cout << "Invalid input. Defaulting to Editor Mode.\n";
        choice = 2;
        std::cin.clear();
        std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
    }
    return choice;
}

std::unique_ptr<Game> createGame(int choice) {
    switch (choice) {
        case 1: return std::make_unique<FlappyBirdGame>();
        case 3: return std::make_unique<MinecraftGame>();
        case 4: return std::make_unique<ShadowTestGame>();
        case 5: return std::make_unique<PointLightTestGame>();
        case 6: return std::make_unique<WaterTestGame>();
        case 7: return std::make_unique<DrawCallTestGame>();
        case 8: return std::make_unique<PhysicsTestGame>();
        case 9: return std::make_unique<SkeletalAnimationTestGame>();
        case 10: return std::make_unique<RayTracingTestGame>();
        case 11: return std::make_unique<VirtualGeoTestGame>();
        default: return std::make_unique<EditorGame>();
    }
}

int main(int argc, char* argv[]) {
    _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
    LaunchArgs args = parseArgs(argc, argv);

    // Select game mode - will be set from launcher or command line
    int choice = args.gameMode;

    // Show project launcher unless skipped
    if (!args.skipLauncher) {
        ProjectLauncher launcher;
        LauncherResult result = launcher.run();

        if (result == LauncherResult::Cancelled) {
            std::cout << "Launcher cancelled.\n";
            return EXIT_SUCCESS;
        }

        if (result != LauncherResult::ProjectOpened) {
            std::cerr << "Failed to open project: " << launcher.getErrorMessage() << std::endl;
            return EXIT_FAILURE;
        }

        // Get the selected game mode from launcher
        choice = launcher.getSelectedGameMode();

        // Project opened successfully
        auto* project = ProjectManager::getInstance().getCurrentProject();
        if (project) {
            std::cout << "Opened project: " << project->getName() << std::endl;
            std::cout << "Project path: " << project->getProjectPath() << std::endl;
        }
    } else if (!args.projectPath.empty()) {
        // Open project from command line
        if (!ProjectManager::getInstance().openProject(args.projectPath)) {
            std::cerr << "Failed to open project: " << args.projectPath << std::endl;
            return EXIT_FAILURE;
        }
        // Use mode from command line args, or ask
        if (args.gameMode == 2) {
            choice = selectGameMode();
        }
    } else {
        // Skip launcher without project - ask for mode
        if (args.gameMode == 2) {
            choice = selectGameMode();
        }
    }

    std::unique_ptr<Game> game = createGame(choice);

    try {
        Application app(std::move(game));
        app.Run();
    }
    catch (const std::exception& e) {
        std::cerr << e.what() << std::endl;
        return EXIT_FAILURE;
    }

    // Save project on exit
    ProjectManager::getInstance().closeProject();

    return EXIT_SUCCESS;
}
