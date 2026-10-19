#ifndef EATSREALMS_PIXEL_CANVAS_HPP
#define EATSREALMS_PIXEL_CANVAS_HPP

#include "eatsrealms/hd2d_types.hpp"
#include <vector>
#include <cstdint>

namespace eatsrealms {

/**
 * Native C++20 Procedural Pixel-Art Canvas & Sobel Normal-Map Generator.
 * Synthesizes crisp 64x64 RGBA8 pixel-art tiles and multi-character/flora sprites
 * along with tangent-space normal + emissive maps in < 5 milliseconds at startup.
 */
class PixelCanvas {
public:
    PixelCanvas(uint32_t width = TexLayer::kTileSize, uint32_t height = TexLayer::kTileSize);

    void clear(RGBA8 color, float heightVal = 0.5f, uint8_t emissive = 0);

    void setPixel(int x, int y, RGBA8 color, float heightVal = 0.5f, uint8_t emissive = 0) noexcept;
    void setPixelWrapped(int x, int y, RGBA8 color, float heightVal = 0.5f, uint8_t emissive = 0) noexcept;

    [[nodiscard]] RGBA8 getPixelWrapped(int x, int y) const noexcept;
    [[nodiscard]] float getHeightWrapped(int x, int y) const noexcept;
    [[nodiscard]] uint8_t getEmissiveWrapped(int x, int y) const noexcept;

    void fillRect(int x0, int y0, int w, int h, RGBA8 color, float heightVal = 0.5f, uint8_t emissive = 0) noexcept;
    void fillCircle(int cx, int cy, int radius, RGBA8 color, float centerHeight = 0.85f, float edgeHeight = 0.35f, uint8_t emissive = 0) noexcept;

    // Generates a tangent-space normal map (RGB = Nx, Ny, Nz in [0..255]) + emissive in Alpha (A = emissive)
    // using a 3x3 Sobel filter over the height buffer. For sprites (alpha = 0), automatically generates a 3D bevel dome.
    [[nodiscard]] std::vector<uint32_t> computeNormalEmissiveMap(float strength = 3.0f, bool wrapEdges = true) const;

    // Deterministic procedural noise & dithering helpers
    static float hash2D(int x, int y, uint32_t seed) noexcept;
    static float valueNoise2D(float x, float y, uint32_t seed) noexcept;
    static float fbm2D(float x, float y, int octaves, uint32_t seed) noexcept;
    static float bayer4x4(int x, int y) noexcept;
    static RGBA8 lerpColor(RGBA8 a, RGBA8 b, float t) noexcept;

    [[nodiscard]] uint32_t width() const noexcept { return width_; }
    [[nodiscard]] uint32_t height() const noexcept { return height_; }
    [[nodiscard]] const std::vector<uint32_t>& albedoPixels() const noexcept { return albedo_; }

private:
    uint32_t width_;
    uint32_t height_;
    std::vector<uint32_t> albedo_;
    std::vector<float> heightMap_;
    std::vector<uint8_t> emissive_;
};

/**
 * Holds the complete 24-layer Texture2DArray data for GPU upload:
 *  - albedoAtlasData:  24 layers * 64 * 64 RGBA8 texels (RGB = Albedo, A = Opacity)
 *  - normalAtlasData:  24 layers * 64 * 64 RGBA8 texels (RGB = Tangent Normal, A = Emissive)
 */
struct ProceduralAtlasData {
    uint32_t tileSize{TexLayer::kTileSize};
    uint32_t layerCount{TexLayer::kCount};
    std::vector<uint32_t> albedoLayers; // size = layerCount * tileSize * tileSize
    std::vector<uint32_t> normalLayers; // size = layerCount * tileSize * tileSize
    double generationTimeMs{0.0};
};

class ProceduralAtlasGenerator {
public:
    static ProceduralAtlasData buildAllLayers();
};

} // namespace eatsrealms

#endif // EATSREALMS_PIXEL_CANVAS_HPP
