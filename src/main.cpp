#include "eatsrealms/realm_world.hpp"
#include "eatsrealms/script_bindings.hpp"
#include "eatsrealms/audio_foley.hpp"
#include "eatsrealms/hd2d_renderer.hpp"
#include <iostream>
#include <filesystem>
#include <string>

int main(int argc, char** argv) {
    std::string scriptPath = "scenes/starfall_vale.eats";
    std::string outputPngPath;
    uint32_t width = 1280;
    uint32_t height = 720;
    float specifiedTime = -1.0f;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if ((arg == "--render-png" || arg == "--snapshot") && i + 1 < argc) {
            outputPngPath = argv[++i];
        } else if (arg == "--script" && i + 1 < argc) {
            scriptPath = argv[++i];
        } else if ((arg == "--time" || arg == "--time-of-day") && i + 1 < argc) {
            specifiedTime = std::stof(argv[++i]);
            if (specifiedTime > 1.0f && specifiedTime <= 24.0f) {
                specifiedTime /= 24.0f;
            }
        } else if (arg == "--width" && i + 1 < argc) {
            width = static_cast<uint32_t>(std::stoul(argv[++i]));
        } else if (arg == "--height" && i + 1 < argc) {
            height = static_cast<uint32_t>(std::stoul(argv[++i]));
        }
    }

#if defined(__EMSCRIPTEN__)
    static eatsrealms::RealmWorld world;
    static eatsrealms::RealmScriptEngine scriptEngine(world);
#else
    eatsrealms::RealmWorld world;
    eatsrealms::RealmScriptEngine scriptEngine(world);
#endif

    if (std::filesystem::exists(scriptPath)) {
        std::cout << "[Eatsrealms] Executing Eatscript HD-2D realm recipe: " << scriptPath << "\n";
        if (!scriptEngine.executeFile(scriptPath)) {
            std::cerr << "[Eatsrealms] Eatscript error: " << scriptEngine.lastError() << "\n";
            world.buildStarfallValeRealm();
        }
    } else {
        std::cout << "[Eatsrealms] Building default Starfall Vale HD-2D RPG Village & Highland Ruins...\n";
        world.buildStarfallValeRealm();
    }

    if (specifiedTime >= 0.0f) {
        world.setTimeOfDay(specifiedTime);
    }

    eatsrealms::RealmStats stats = world.rebuildWorldGeometry();
    uint32_t totalTris = stats.terrainTriangles + stats.propTriangles + stats.spriteCount * 2u;
    std::cout << "[Eatsrealms] HD-2D Realm compiled:\n"
              << "  - Procedural 24-Layer Pixel+Normal Atlas : " << stats.atlasGenTimeMs << " ms\n"
              << "  - 3D Multi-Height Terrain Triangles      : " << stats.terrainTriangles << "\n"
              << "  - 3D Gabled Village & Bridge Triangles   : " << stats.propTriangles << "\n"
              << "  - 2.5D Shadow-Casting Sprite3D Entities  : " << stats.spriteCount << "\n"
              << "  - Dynamic Point Lights                   : " << stats.pointLightCount << "\n"
              << "  - Total Scene Triangles                  : " << totalTris << " (6 WebGPU draw calls)\n";

    if (!outputPngPath.empty()) {
        std::cout << "[Eatsrealms] Rendering WebGPU HD-2D Diorama frame (" << width << "x" << height << ") -> " << outputPngPath << " ...\n";
        if (eatsrealms::Hd2dRenderer::renderToPng(world, width, height, outputPngPath)) {
            std::cout << "[Eatsrealms] Successfully wrote " << outputPngPath << "\n";
            return 0;
        } else {
            std::cerr << "[Eatsrealms] Failed to render PNG.\n";
            return 1;
        }
    }

#if defined(__EMSCRIPTEN__)
    static eatsrealms::AudioFoleyEngine foley;
#else
    eatsrealms::AudioFoleyEngine foley;
#endif
    foley.init(true);
    return eatsrealms::Hd2dRenderer::runInteractiveWindow(world, scriptEngine, scriptPath, foley, width, height);
}
