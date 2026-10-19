#ifndef EATSREALMS_HD2D_RENDERER_HPP
#define EATSREALMS_HD2D_RENDERER_HPP

#include "eatsrealms/realm_world.hpp"
#include "eatsrealms/script_bindings.hpp"
#include "eatsrealms/audio_foley.hpp"
#include <string>
#include <cstdint>

namespace eatsrealms {

/**
 * Hardware-Accelerated WebGPU / Google Dawn HD-2D Diorama Renderer (6 Draw Calls Total).
 * Implements:
 *   1. Single WebGPU Texture2DArray upload for all 24 procedural pixel-art Albedo & Sobel Normal layers.
 *   2. Pass 1: 2048x2048 Directional Sun Shadow Map (3D World Geometry + Alpha-Tested 2.5D Sprite Billboards).
 *   3. Pass 2: HD-2D Lit Pass (RGBA16Float) with Sub-Pixel AA Pixel-Art Sampling, Tangent Normal Mapping,
 *      Animated Water Caustics, 64-Light Dynamic Point-Light Pool, and Normal-Lit 2.5D Billboards.
 *   4. Pass 3: 24-Tap Golden-Angle Spiral Tilt-Shift Bokeh Depth of Field + HDR Bloom + ACES Filmic Grade.
 */
class Hd2dRenderer {
public:
    Hd2dRenderer() = default;
    ~Hd2dRenderer() = default;

    // Renders the RealmWorld offscreen via WebGPU and writes a 32-bit RGBA PNG file.
    static bool renderToPng(
        RealmWorld& world,
        uint32_t width,
        uint32_t height,
        const std::string& outputPngPath
    );

    // Launches the interactive desktop GLFW + WebGPU HD-2D Diorama Viewer with live .eats hot-reloading.
    static int runInteractiveWindow(
        RealmWorld& world,
        RealmScriptEngine& scriptEngine,
        const std::string& watchedScriptPath,
        AudioFoleyEngine& foleyEngine,
        uint32_t width = 1280,
        uint32_t height = 720
    );

    // Zero-dependency RGBA8 PNG encoder
    static bool writePngFile(
        const std::string& filePath,
        uint32_t width,
        uint32_t height,
        const uint8_t* rgbaPixels
    );
};

} // namespace eatsrealms

#endif // EATSREALMS_HD2D_RENDERER_HPP
