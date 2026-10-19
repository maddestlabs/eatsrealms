#include "eatsrealms/pixel_canvas.hpp"
#include <chrono>
#include <cmath>
#include <algorithm>

namespace eatsrealms {

PixelCanvas::PixelCanvas(uint32_t width, uint32_t height)
    : width_(width)
    , height_(height)
    , albedo_(static_cast<size_t>(width) * height, 0u)
    , heightMap_(static_cast<size_t>(width) * height, 0.5f)
    , emissive_(static_cast<size_t>(width) * height, 0u) {}

void PixelCanvas::clear(RGBA8 color, float heightVal, uint8_t emissive) {
    std::fill(albedo_.begin(), albedo_.end(), color.pack());
    std::fill(heightMap_.begin(), heightMap_.end(), heightVal);
    std::fill(emissive_.begin(), emissive_.end(), emissive);
}

void PixelCanvas::setPixel(int x, int y, RGBA8 color, float heightVal, uint8_t emissive) noexcept {
    if (x < 0 || y < 0 || x >= static_cast<int>(width_) || y >= static_cast<int>(height_)) return;
    size_t idx = static_cast<size_t>(y) * width_ + static_cast<size_t>(x);
    albedo_[idx] = color.pack();
    heightMap_[idx] = heightVal;
    emissive_[idx] = emissive;
}

void PixelCanvas::setPixelWrapped(int x, int y, RGBA8 color, float heightVal, uint8_t emissive) noexcept {
    int wx = ((x % static_cast<int>(width_)) + static_cast<int>(width_)) % static_cast<int>(width_);
    int wy = ((y % static_cast<int>(height_)) + static_cast<int>(height_)) % static_cast<int>(height_);
    size_t idx = static_cast<size_t>(wy) * width_ + static_cast<size_t>(wx);
    albedo_[idx] = color.pack();
    heightMap_[idx] = heightVal;
    emissive_[idx] = emissive;
}

RGBA8 PixelCanvas::getPixelWrapped(int x, int y) const noexcept {
    int wx = ((x % static_cast<int>(width_)) + static_cast<int>(width_)) % static_cast<int>(width_);
    int wy = ((y % static_cast<int>(height_)) + static_cast<int>(height_)) % static_cast<int>(height_);
    return RGBA8::unpack(albedo_[static_cast<size_t>(wy) * width_ + static_cast<size_t>(wx)]);
}

float PixelCanvas::getHeightWrapped(int x, int y) const noexcept {
    int wx = ((x % static_cast<int>(width_)) + static_cast<int>(width_)) % static_cast<int>(width_);
    int wy = ((y % static_cast<int>(height_)) + static_cast<int>(height_)) % static_cast<int>(height_);
    return heightMap_[static_cast<size_t>(wy) * width_ + static_cast<size_t>(wx)];
}

uint8_t PixelCanvas::getEmissiveWrapped(int x, int y) const noexcept {
    int wx = ((x % static_cast<int>(width_)) + static_cast<int>(width_)) % static_cast<int>(width_);
    int wy = ((y % static_cast<int>(height_)) + static_cast<int>(height_)) % static_cast<int>(height_);
    return emissive_[static_cast<size_t>(wy) * width_ + static_cast<size_t>(wx)];
}

void PixelCanvas::fillRect(int x0, int y0, int w, int h, RGBA8 color, float heightVal, uint8_t emissive) noexcept {
    for (int dy = 0; dy < h; ++dy) {
        for (int dx = 0; dx < w; ++dx) {
            setPixel(x0 + dx, y0 + dy, color, heightVal, emissive);
        }
    }
}

void PixelCanvas::fillCircle(int cx, int cy, int radius, RGBA8 color, float centerHeight, float edgeHeight, uint8_t emissive) noexcept {
    int r2 = radius * radius;
    for (int dy = -radius; dy <= radius; ++dy) {
        for (int dx = -radius; dx <= radius; ++dx) {
            int d2 = dx * dx + dy * dy;
            if (d2 <= r2) {
                float t = std::sqrt(static_cast<float>(d2)) / std::max(1.0f, static_cast<float>(radius));
                float h = centerHeight * (1.0f - t * t) + edgeHeight * (t * t);
                setPixel(cx + dx, cy + dy, color, h, emissive);
            }
        }
    }
}

float PixelCanvas::hash2D(int x, int y, uint32_t seed) noexcept {
    uint32_t h = static_cast<uint32_t>(x) * 374761393u + static_cast<uint32_t>(y) * 668265263u + seed * 1442695041u;
    h = (h ^ (h >> 13u)) * 1274126177u;
    h = h ^ (h >> 16u);
    return static_cast<float>(h & 0x00FFFFFFu) / static_cast<float>(0x01000000u);
}

float PixelCanvas::valueNoise2D(float x, float y, uint32_t seed) noexcept {
    int ix = static_cast<int>(std::floor(x));
    int iy = static_cast<int>(std::floor(y));
    float fx = x - static_cast<float>(ix);
    float fy = y - static_cast<float>(iy);
    float u = fx * fx * (3.0f - 2.0f * fx);
    float v = fy * fy * (3.0f - 2.0f * fy);

    float a = hash2D(ix, iy, seed);
    float b = hash2D(ix + 1, iy, seed);
    float c = hash2D(ix, iy + 1, seed);
    float d = hash2D(ix + 1, iy + 1, seed);
    return (a * (1.0f - u) + b * u) * (1.0f - v) + (c * (1.0f - u) + d * u) * v;
}

float PixelCanvas::fbm2D(float x, float y, int octaves, uint32_t seed) noexcept {
    float sum = 0.0f;
    float amp = 0.5f;
    float freq = 1.0f;
    for (int i = 0; i < octaves; ++i) {
        sum += amp * valueNoise2D(x * freq, y * freq, seed + static_cast<uint32_t>(i) * 101u);
        freq *= 2.0f;
        amp *= 0.5f;
    }
    return sum;
}

float PixelCanvas::bayer4x4(int x, int y) noexcept {
    static constexpr float kMatrix[4][4] = {
        { 0.0f / 16.0f,  8.0f / 16.0f,  2.0f / 16.0f, 10.0f / 16.0f },
        {12.0f / 16.0f,  4.0f / 16.0f, 14.0f / 16.0f,  6.0f / 16.0f },
        { 3.0f / 16.0f, 11.0f / 16.0f,  1.0f / 16.0f,  9.0f / 16.0f },
        {15.0f / 16.0f,  7.0f / 16.0f, 13.0f / 16.0f,  5.0f / 16.0f }
    };
    return kMatrix[y & 3][x & 3] - 0.5f;
}

RGBA8 PixelCanvas::lerpColor(RGBA8 a, RGBA8 b, float t) noexcept {
    t = std::clamp(t, 0.0f, 1.0f);
    return RGBA8{
        static_cast<uint8_t>(a.r + (b.r - a.r) * t),
        static_cast<uint8_t>(a.g + (b.g - a.g) * t),
        static_cast<uint8_t>(a.b + (b.b - a.b) * t),
        static_cast<uint8_t>(a.a + (b.a - a.a) * t)
    };
}

std::vector<uint32_t> PixelCanvas::computeNormalEmissiveMap(float strength, bool wrapEdges) const {
    std::vector<uint32_t> out(static_cast<size_t>(width_) * height_);
    const int w = static_cast<int>(width_);
    const int h = static_cast<int>(height_);

    auto sampleH = [&](int x, int y) -> float {
        if (wrapEdges) {
            return getHeightWrapped(x, y);
        }
        int cx = std::clamp(x, 0, w - 1);
        int cy = std::clamp(y, 0, h - 1);
        return heightMap_[static_cast<size_t>(cy) * width_ + static_cast<size_t>(cx)];
    };

    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            float tl = sampleH(x - 1, y - 1);
            float tc = sampleH(x,     y - 1);
            float tr = sampleH(x + 1, y - 1);
            float ml = sampleH(x - 1, y);
            float mr = sampleH(x + 1, y);
            float bl = sampleH(x - 1, y + 1);
            float bc = sampleH(x,     y + 1);
            float br = sampleH(x + 1, y + 1);

            // 3x3 Sobel operator
            float dX = (tr + 2.0f * mr + br) - (tl + 2.0f * ml + bl);
            float dY = (bl + 2.0f * bc + br) - (tl + 2.0f * tc + tr);

            float nx = -dX * strength;
            float ny = -dY * strength;
            float nz = 1.0f;
            float invLen = 1.0f / std::sqrt(nx * nx + ny * ny + nz * nz);
            nx *= invLen;
            ny *= invLen;
            nz *= invLen;

            RGBA8 encoded{
                static_cast<uint8_t>(std::clamp((nx * 0.5f + 0.5f) * 255.0f, 0.0f, 255.0f)),
                static_cast<uint8_t>(std::clamp((ny * 0.5f + 0.5f) * 255.0f, 0.0f, 255.0f)),
                static_cast<uint8_t>(std::clamp((nz * 0.5f + 0.5f) * 255.0f, 0.0f, 255.0f)),
                getEmissiveWrapped(x, y)
            };
            out[static_cast<size_t>(y) * width_ + static_cast<size_t>(x)] = encoded.pack();
        }
    }
    return out;
}

namespace {

// ----------------------------------------------------------------------------
// World Material Tile Generators (64x64 Seamless Pixel Art + Height Maps)
// ----------------------------------------------------------------------------

void generateGrassTile(PixelCanvas& c, RGBA8 darkCol, RGBA8 midCol, RGBA8 lightCol, RGBA8 flowerCol, uint32_t seed, bool withFlowers) {
    const int s = static_cast<int>(c.width());
    for (int y = 0; y < s; ++y) {
        for (int x = 0; x < s; ++x) {
            float n = PixelCanvas::fbm2D(x * 0.14f, y * 0.14f, 3, seed) + PixelCanvas::bayer4x4(x, y) * 0.12f;
            RGBA8 col = (n < 0.42f) ? darkCol : ((n < 0.64f) ? midCol : lightCol);
            float h = 0.35f + n * 0.35f;
            c.setPixel(x, y, col, h, 0);
        }
    }
    // Crisp pixel-art grass blade clusters
    for (int i = 0; i < 95; ++i) {
        int bx = static_cast<int>(PixelCanvas::hash2D(i, 1, seed) * s);
        int by = static_cast<int>(PixelCanvas::hash2D(i, 2, seed) * s);
        c.setPixelWrapped(bx, by, darkCol, 0.25f);
        c.setPixelWrapped(bx, by - 1, midCol, 0.65f);
        c.setPixelWrapped(bx + ((i & 1) ? 1 : -1), by - 2, lightCol, 0.88f);
        if (withFlowers && (i % 14 == 0)) {
            c.setPixelWrapped(bx, by - 2, flowerCol, 0.95f);
            c.setPixelWrapped(bx + 1, by - 2, flowerCol, 0.90f);
        }
    }
}

void generateCliffStoneTile(PixelCanvas& c, uint32_t seed) {
    const int s = static_cast<int>(c.width());
    RGBA8 crevice{42, 39, 48, 255};
    RGBA8 darkRock{76, 72, 82, 255};
    RGBA8 midRock{108, 102, 112, 255};
    RGBA8 warmHigh{142, 132, 136, 255};

    for (int y = 0; y < s; ++y) {
        for (int x = 0; x < s; ++x) {
            // Stratified horizontal ledges + vertical fractures
            int strataY = y / 8;
            int offset = (strataY & 1) * 7;
            float cellN = PixelCanvas::fbm2D((x + offset) * 0.16f, y * 0.22f, 3, seed);
            bool isHorizontalSeam = (y % 8 == 0) || (y % 8 == 7 && cellN < 0.48f);
            bool isVerticalCrack  = ((x + offset + static_cast<int>(cellN * 6.0f)) % 16 == 0);

            if (isHorizontalSeam || isVerticalCrack) {
                c.setPixel(x, y, crevice, 0.10f);
            } else {
                float dither = PixelCanvas::bayer4x4(x, y) * 0.10f;
                float val = cellN + ((y % 8 == 1) ? 0.18f : 0.0f) + dither;
                RGBA8 col = (val < 0.42f) ? darkRock : ((val < 0.68f) ? midRock : warmHigh);
                c.setPixel(x, y, col, std::clamp(0.35f + val * 0.55f, 0.2f, 0.95f));
            }
        }
    }
}

void generateCobblestoneTile(PixelCanvas& c, uint32_t seed) {
    const int s = static_cast<int>(c.width());
    RGBA8 mortar{54, 48, 46, 255};
    // Generate 32 deterministic stone centers on a torus
    struct StoneCenter { int x; int y; uint32_t id; };
    std::vector<StoneCenter> centers;
    for (int gy = 0; gy < 6; ++gy) {
        for (int gx = 0; gx < 6; ++gx) {
            uint32_t id = static_cast<uint32_t>(gy * 6 + gx);
            int cx = gx * 11 + static_cast<int>(PixelCanvas::hash2D(gx, gy, seed) * 6.0f);
            int cy = gy * 11 + static_cast<int>(PixelCanvas::hash2D(gx + 19, gy + 7, seed) * 6.0f);
            centers.push_back({cx % s, cy % s, id});
        }
    }

    for (int y = 0; y < s; ++y) {
        for (int x = 0; x < s; ++x) {
            float d1 = 9999.0f;
            float d2 = 9999.0f;
            uint32_t bestId = 0;
            int bestDx = 0, bestDy = 0;

            for (const auto& sc : centers) {
                int dx = x - sc.x;
                int dy = y - sc.y;
                if (dx > s / 2) dx -= s;
                if (dx < -s / 2) dx += s;
                if (dy > s / 2) dy -= s;
                if (dy < -s / 2) dy += s;
                float dist = std::sqrt(static_cast<float>(dx * dx + dy * dy));
                if (dist < d1) {
                    d2 = d1;
                    d1 = dist;
                    bestId = sc.id;
                    bestDx = dx;
                    bestDy = dy;
                } else if (dist < d2) {
                    d2 = dist;
                }
            }

            float edgeGap = d2 - d1;
            if (edgeGap < 1.35f) {
                c.setPixel(x, y, mortar, 0.12f);
            } else {
                float stoneTint = PixelCanvas::hash2D(static_cast<int>(bestId), 3, seed);
                RGBA8 base = PixelCanvas::lerpColor(RGBA8{116, 108, 102, 255}, RGBA8{158, 146, 134, 255}, stoneTint);
                // Top-left bevel highlight on each cobble
                float highlight = (-bestDx - bestDy) * 0.045f + PixelCanvas::bayer4x4(x, y) * 0.08f;
                RGBA8 shaded = (highlight > 0.08f)
                    ? PixelCanvas::lerpColor(base, RGBA8{192, 180, 168, 255}, 0.45f)
                    : ((highlight < -0.08f) ? PixelCanvas::lerpColor(base, RGBA8{78, 72, 68, 255}, 0.45f) : base);
                float h = std::clamp(0.45f + (edgeGap * 0.12f), 0.30f, 0.92f);
                c.setPixel(x, y, shaded, h);
            }
        }
    }
}

void generateDirtPathTile(PixelCanvas& c, uint32_t seed) {
    const int s = static_cast<int>(c.width());
    RGBA8 darkEarth{102, 74, 52, 255};
    RGBA8 midEarth{134, 98, 68, 255};
    RGBA8 warmSand{164, 124, 88, 255};
    for (int y = 0; y < s; ++y) {
        for (int x = 0; x < s; ++x) {
            float n = PixelCanvas::fbm2D(x * 0.15f, y * 0.15f, 3, seed) + PixelCanvas::bayer4x4(x, y) * 0.10f;
            RGBA8 col = (n < 0.40f) ? darkEarth : ((n < 0.68f) ? midEarth : warmSand);
            c.setPixel(x, y, col, 0.35f + n * 0.35f);
        }
    }
    // Small pebbles
    for (int i = 0; i < 24; ++i) {
        int px = static_cast<int>(PixelCanvas::hash2D(i, 10, seed) * s);
        int py = static_cast<int>(PixelCanvas::hash2D(i, 20, seed) * s);
        c.fillRect(px, py, 2, 2, RGBA8{152, 144, 136, 255}, 0.78f);
    }
}

void generateWoodPlanksTile(PixelCanvas& c, uint32_t seed) {
    const int s = static_cast<int>(c.width());
    RGBA8 seam{48, 30, 18, 255};
    RGBA8 woodDark{104, 68, 40, 255};
    RGBA8 woodMid{136, 92, 54, 255};
    RGBA8 woodLight{162, 114, 68, 255};

    for (int y = 0; y < s; ++y) {
        int plankRow = y / 8;
        bool isRowSeam = (y % 8 == 0);
        int jointX = ((plankRow * 23) + 11) % s;
        for (int x = 0; x < s; ++x) {
            if (isRowSeam || x == jointX) {
                c.setPixel(x, y, seam, 0.15f);
            } else {
                float grain = PixelCanvas::fbm2D(x * 0.08f, y * 0.45f + plankRow * 3.1f, 2, seed);
                RGBA8 col = (grain < 0.42f) ? woodDark : ((grain < 0.66f) ? woodMid : woodLight);
                c.setPixel(x, y, col, 0.55f + grain * 0.28f);
            }
        }
    }
}

void generateTimberWallTile(PixelCanvas& c, uint32_t seed) {
    const int s = static_cast<int>(c.width());
    RGBA8 plasterBase{228, 214, 192, 255};
    RGBA8 plasterWarm{214, 196, 172, 255};
    RGBA8 beamDark{68, 44, 28, 255};
    RGBA8 beamMid{92, 60, 38, 255};
    RGBA8 beamHigh{116, 78, 50, 255};

    for (int y = 0; y < s; ++y) {
        for (int x = 0; x < s; ++x) {
            float n = PixelCanvas::fbm2D(x * 0.18f, y * 0.18f, 2, seed) + PixelCanvas::bayer4x4(x, y) * 0.08f;
            RGBA8 plaster = (n < 0.5f) ? plasterWarm : plasterBase;

            // Half-timbered border & diagonal Tudor brace
            bool isOuterBeam = (x < 5 || x >= s - 5 || y < 5 || y >= s - 5 || (y >= 29 && y <= 34));
            bool isDiagBrace = (std::abs(x - y) <= 3) || (std::abs((s - 1 - x) - y) <= 3 && y > 32);

            if (isOuterBeam || isDiagBrace) {
                float g = PixelCanvas::valueNoise2D(x * 0.25f, y * 0.25f, seed + 7u);
                RGBA8 bCol = (g < 0.35f) ? beamDark : ((g < 0.72f) ? beamMid : beamHigh);
                c.setPixel(x, y, bCol, 0.85f);
            } else {
                c.setPixel(x, y, plaster, 0.42f + n * 0.12f);
            }
        }
    }
}

void generateStuccoPlasterTile(PixelCanvas& c, uint32_t seed) {
    const int s = static_cast<int>(c.width());
    RGBA8 warmA{234, 220, 198, 255};
    RGBA8 warmB{218, 200, 176, 255};
    for (int y = 0; y < s; ++y) {
        for (int x = 0; x < s; ++x) {
            float n = PixelCanvas::fbm2D(x * 0.16f, y * 0.16f, 3, seed) + PixelCanvas::bayer4x4(x, y) * 0.08f;
            c.setPixel(x, y, PixelCanvas::lerpColor(warmB, warmA, n), 0.45f + n * 0.20f);
        }
    }
}

void generateRoofShingleTile(PixelCanvas& c, RGBA8 shadowCol, RGBA8 midCol, RGBA8 highCol, uint32_t seed) {
    const int s = static_cast<int>(c.width());
    for (int y = 0; y < s; ++y) {
        int row = y / 8;
        int rowY = y % 8;
        int xOffset = (row & 1) * 4;
        for (int x = 0; x < s; ++x) {
            int tileX = (x + xOffset) % 8;
            // Curved scallop bottom edge
            bool isUnderLap = (rowY == 7) || (rowY == 6 && (tileX == 0 || tileX == 7));
            bool isSideSeam = (tileX == 0 && rowY > 1);
            if (isUnderLap || isSideSeam) {
                c.setPixel(x, y, shadowCol, 0.15f);
            } else {
                float tileRand = PixelCanvas::hash2D((x + xOffset) / 8, row, seed);
                float ramp = static_cast<float>(rowY) / 7.0f;
                RGBA8 col = (ramp + tileRand * 0.25f > 0.65f) ? highCol : midCol;
                c.setPixel(x, y, col, 0.35f + ramp * 0.55f);
            }
        }
    }
}

void generateStoneBrickTile(PixelCanvas& c, uint32_t seed) {
    const int s = static_cast<int>(c.width());
    RGBA8 mortar{52, 48, 54, 255};
    for (int y = 0; y < s; ++y) {
        int row = y / 8;
        int xOff = (row & 1) * 8;
        for (int x = 0; x < s; ++x) {
            if (y % 8 == 0 || ((x + xOff) % 16 == 0)) {
                c.setPixel(x, y, mortar, 0.12f);
            } else {
                float bHash = PixelCanvas::hash2D((x + xOff) / 16, row, seed);
                RGBA8 base = PixelCanvas::lerpColor(RGBA8{104, 100, 108, 255}, RGBA8{148, 142, 148, 255}, bHash);
                c.setPixel(x, y, base, 0.58f + bHash * 0.25f);
            }
        }
    }
}

void generateWindowWarmGlowTile(PixelCanvas& c) {
    const int s = static_cast<int>(c.width());
    RGBA8 timberFrame{58, 38, 24, 255};
    RGBA8 leadMullion{42, 32, 28, 255};
    for (int y = 0; y < s; ++y) {
        for (int x = 0; x < s; ++x) {
            bool isOuterFrame = (x < 6 || x >= s - 6 || y < 6 || y >= s - 6);
            bool isCrossBar   = (std::abs(x - s / 2) <= 2) || (std::abs(y - s / 2) <= 2);
            bool isDiamondLead = ((x + y) % 12 == 0) || ((x - y + 64) % 12 == 0);
            if (isOuterFrame) {
                c.setPixel(x, y, timberFrame, 0.90f, 0);
            } else if (isCrossBar || isDiamondLead) {
                c.setPixel(x, y, leadMullion, 0.72f, 25);
            } else {
                float distCenter = std::sqrt(static_cast<float>((x - 32) * (x - 32) + (y - 32) * (y - 32))) / 32.0f;
                RGBA8 glow = PixelCanvas::lerpColor(RGBA8{255, 232, 148, 255}, RGBA8{242, 148, 54, 255}, distCenter);
                c.setPixel(x, y, glow, 0.30f, 245);
            }
        }
    }
}

void generateWaterSurfaceTile(PixelCanvas& c, uint32_t seed) {
    const int s = static_cast<int>(c.width());
    RGBA8 deepTeal{28, 86, 114, 225};
    RGBA8 midAqua{48, 132, 158, 225};
    RGBA8 causticFoam{176, 234, 246, 245};

    for (int y = 0; y < s; ++y) {
        for (int x = 0; x < s; ++x) {
            float w1 = PixelCanvas::fbm2D(x * 0.14f, y * 0.14f, 3, seed);
            float w2 = PixelCanvas::fbm2D((x + 17) * 0.18f, (y - 11) * 0.18f, 2, seed + 55u);
            float ridge = 1.0f - std::abs(w1 - w2) * 3.2f;
            if (ridge > 0.78f) {
                c.setPixel(x, y, causticFoam, 0.85f, 40);
            } else {
                c.setPixel(x, y, PixelCanvas::lerpColor(deepTeal, midAqua, w1), 0.35f + w1 * 0.40f, 0);
            }
        }
    }
}

// ----------------------------------------------------------------------------
// 2.5D Pixel-Art Sprite Sheet Generators (64x64 with Alpha + 3D Dome Normal)
// ----------------------------------------------------------------------------

// Post-processes any sprite canvas so every non-transparent pixel gets a crisp dark
// pixel-art outline and a 3D spherical/cylindrical bevel height for normal-mapped lighting!
void finalizeSpriteBevelAndOutline(PixelCanvas& c, RGBA8 outlineColor = RGBA8{24, 18, 26, 255}) {
    const int w = static_cast<int>(c.width());
    const int h = static_cast<int>(c.height());
    std::vector<RGBA8> orig(static_cast<size_t>(w) * h);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            orig[static_cast<size_t>(y) * w + x] = c.getPixelWrapped(x, y);
        }
    }

    for (int y = 1; y < h - 1; ++y) {
        for (int x = 1; x < w - 1; ++x) {
            RGBA8 cur = orig[static_cast<size_t>(y) * w + x];
            if (cur.a < 128) {
                // Check 4-neighbors to add 1px dark HD-2D sprite outline
                bool touchOpaque =
                    orig[static_cast<size_t>(y) * w + (x - 1)].a >= 128 ||
                    orig[static_cast<size_t>(y) * w + (x + 1)].a >= 128 ||
                    orig[static_cast<size_t>(y - 1) * w + x].a >= 128 ||
                    orig[static_cast<size_t>(y + 1) * w + x].a >= 128;
                if (touchOpaque) {
                    c.setPixel(x, y, outlineColor, 0.15f, 0);
                } else {
                    c.setPixel(x, y, RGBA8{0, 0, 0, 0}, 0.0f, 0);
                }
            }
        }
    }
}

void generateCharacterSprite(
    PixelCanvas& c,
    RGBA8 cloakCol,
    RGBA8 tunicCol,
    RGBA8 hairCol,
    RGBA8 trimCol,
    bool hasSwordAndShield,
    bool hasMerchantPack
) {
    c.clear(RGBA8{0, 0, 0, 0}, 0.0f, 0);
    RGBA8 skinLight{246, 206, 174, 255};
    RGBA8 skinShade{214, 164, 132, 255};
    RGBA8 bootCol{64, 42, 30, 255};
    RGBA8 eyeCol{32, 26, 36, 255};

    // Boots & legs (y = 48..60, grounded near y = 61)
    c.fillRect(25, 49, 5, 11, bootCol, 0.65f);
    c.fillRect(34, 49, 5, 11, bootCol, 0.65f);

    // Cloak back cape
    c.fillCircle(32, 39, 13, cloakCol, 0.72f, 0.25f);

    if (hasMerchantPack) {
        // Large traveler backpack with rolled blanket
        c.fillRect(17, 24, 10, 18, RGBA8{148, 96, 52, 255}, 0.78f);
        c.fillRect(18, 20, 9, 5, RGBA8{196, 68, 58, 255}, 0.82f);
    }

    // Torso tunic & belt
    c.fillRect(24, 29, 16, 21, tunicCol, 0.85f);
    c.fillRect(24, 41, 16, 3, trimCol, 0.90f); // Belt & buckle
    c.fillRect(30, 40, 4, 4, RGBA8{238, 196, 84, 255}, 0.95f);

    // Shoulder mantle
    c.fillRect(22, 27, 20, 6, cloakCol, 0.88f);

    // Head & face (classic chibi 16-bit/HD-2D proportions)
    c.fillCircle(32, 18, 9, skinLight, 0.92f, 0.45f);
    c.fillRect(25, 21, 14, 4, skinShade, 0.78f);

    // Expressive pixel eyes
    c.fillRect(28, 18, 2, 4, eyeCol, 0.85f);
    c.fillRect(35, 18, 2, 4, eyeCol, 0.85f);
    c.setPixel(28, 18, RGBA8{255, 255, 255, 255}, 0.90f);
    c.setPixel(35, 18, RGBA8{255, 255, 255, 255}, 0.90f);

    // Hair & locks
    c.fillCircle(32, 13, 10, hairCol, 0.95f, 0.50f);
    c.fillRect(22, 13, 4, 10, hairCol, 0.82f);
    c.fillRect(38, 13, 4, 10, hairCol, 0.82f);

    if (hasSwordAndShield) {
        // Gleaming steel broadsword in right hand
        for (int i = 0; i < 18; ++i) {
            c.fillRect(44 + i / 3, 34 - i, 3, 2, RGBA8{220, 232, 245, 255}, 0.95f);
        }
        c.fillRect(41, 34, 8, 3, RGBA8{232, 186, 74, 255}, 0.92f); // Crossguard
        // Crest shield on left arm
        c.fillCircle(20, 37, 7, trimCol, 0.95f, 0.55f);
        c.fillCircle(20, 37, 5, RGBA8{62, 108, 168, 255}, 0.92f, 0.65f);
    }

    finalizeSpriteBevelAndOutline(c);
}

void generateSlimeMonsterSprite(PixelCanvas& c) {
    c.clear(RGBA8{0, 0, 0, 0}, 0.0f, 0);
    RGBA8 bodyDeep{36, 138, 196, 240};
    RGBA8 bodyMid{68, 186, 236, 245};
    RGBA8 bodySpec{210, 248, 255, 255};
    RGBA8 coreGlow{140, 250, 220, 255};

    c.fillCircle(32, 44, 15, bodyDeep, 0.92f, 0.25f, 25);
    c.fillCircle(32, 42, 12, bodyMid, 0.96f, 0.45f, 45);
    c.fillCircle(32, 30, 5, bodyMid, 0.88f, 0.40f, 45); // Teardrop tip
    c.fillCircle(27, 37, 3, bodySpec, 0.99f, 0.80f, 90); // Glossy specular catchlight

    // Glowing monster eyes & smile
    c.fillRect(27, 43, 3, 4, RGBA8{255, 236, 94, 255}, 0.95f, 200);
    c.fillRect(35, 43, 3, 4, RGBA8{255, 236, 94, 255}, 0.95f, 200);
    c.fillRect(30, 48, 5, 2, coreGlow, 0.90f, 150);

    finalizeSpriteBevelAndOutline(c);
}

void generateWolfMonsterSprite(PixelCanvas& c) {
    c.clear(RGBA8{0, 0, 0, 0}, 0.0f, 0);
    RGBA8 furDark{58, 52, 68, 255};
    RGBA8 furMid{88, 80, 102, 255};
    RGBA8 furLight{138, 128, 152, 255};
    RGBA8 eyeGlow{255, 76, 64, 255};

    // Legs
    c.fillRect(18, 45, 5, 15, furDark, 0.65f);
    c.fillRect(26, 46, 5, 14, furMid, 0.70f);
    c.fillRect(37, 45, 5, 15, furDark, 0.65f);
    c.fillRect(44, 46, 5, 14, furMid, 0.70f);
    // Torso & bushy tail
    c.fillCircle(32, 39, 13, furMid, 0.88f, 0.35f);
    c.fillCircle(15, 34, 7, furDark, 0.78f, 0.30f);
    // Mane & head
    c.fillCircle(42, 31, 9, furLight, 0.92f, 0.45f);
    c.fillCircle(48, 27, 7, furMid, 0.94f, 0.50f);
    // Pointed ears
    c.fillRect(43, 16, 4, 7, furDark, 0.85f);
    c.fillRect(49, 17, 4, 7, furDark, 0.85f);
    // Glowing crimson eye
    c.fillRect(49, 25, 3, 2, eyeGlow, 0.95f, 240);

    finalizeSpriteBevelAndOutline(c);
}

void generatePineTreeSprite(PixelCanvas& c, uint32_t seed) {
    c.clear(RGBA8{0, 0, 0, 0}, 0.0f, 0);
    RGBA8 trunkDark{68, 42, 26, 255};
    RGBA8 trunkLight{98, 62, 38, 255};
    RGBA8 needleShadow{26, 58, 42, 255};
    RGBA8 needleMid{42, 92, 58, 255};
    RGBA8 needleSun{76, 136, 78, 255};

    // Sturdy trunk at bottom center
    c.fillRect(28, 46, 8, 17, trunkDark, 0.65f);
    c.fillRect(29, 46, 4, 17, trunkLight, 0.82f);

    // 4 tiered coniferous bough cones with pixel-dithered needles
    struct Tier { int cy; int halfW; int h; };
    const Tier tiers[4] = { {42, 23, 14}, {31, 19, 13}, {21, 14, 12}, {12, 9, 10} };
    for (int t = 0; t < 4; ++t) {
        const auto& tr = tiers[t];
        for (int dy = -tr.h; dy <= 4; ++dy) {
            float progress = static_cast<float>(dy + tr.h) / static_cast<float>(tr.h + 4);
            int span = static_cast<int>(tr.halfW * progress);
            for (int dx = -span; dx <= span; ++dx) {
                int px = 32 + dx;
                int py = tr.cy + dy;
                float n = PixelCanvas::hash2D(px, py, seed + t * 17u) + PixelCanvas::bayer4x4(px, py) * 0.20f;
                if (std::abs(dx) > span - 2 && n < 0.35f) continue; // Serrated bough silhouette
                float dome = 1.0f - (static_cast<float>(dx * dx) / std::max(1.0f, static_cast<float>(span * span))) * 0.65f;
                RGBA8 col = (dy > 1 || n < 0.32f) ? needleShadow : ((dy < -tr.h / 2 && n > 0.55f) ? needleSun : needleMid);
                c.setPixel(px, py, col, std::clamp(0.35f + dome * 0.60f, 0.25f, 0.98f));
            }
        }
    }
    finalizeSpriteBevelAndOutline(c, RGBA8{16, 28, 20, 255});
}

void generateOakTreeSprite(PixelCanvas& c, uint32_t seed) {
    c.clear(RGBA8{0, 0, 0, 0}, 0.0f, 0);
    RGBA8 trunkDark{76, 48, 30, 255};
    RGBA8 trunkLight{112, 74, 46, 255};
    RGBA8 leafShadow{48, 76, 38, 255};
    RGBA8 leafMid{84, 126, 52, 255};
    RGBA8 leafGold{156, 178, 68, 255};

    // Gnarled trunk & roots
    c.fillRect(27, 40, 10, 22, trunkDark, 0.65f);
    c.fillRect(28, 40, 5, 22, trunkLight, 0.85f);
    c.fillRect(24, 57, 16, 5, trunkDark, 0.60f);

    // Multi-puff deciduous canopy puffs
    struct Puff { int cx; int cy; int r; };
    const Puff puffs[6] = {
        {20, 28, 12}, {44, 28, 12}, {32, 26, 15},
        {23, 18, 11}, {41, 18, 11}, {32, 14, 12}
    };
    for (int p = 0; p < 6; ++p) {
        const auto& pf = puffs[p];
        for (int dy = -pf.r; dy <= pf.r; ++dy) {
            for (int dx = -pf.r; dx <= pf.r; ++dx) {
                float d2 = static_cast<float>(dx * dx + dy * dy);
                if (d2 <= static_cast<float>(pf.r * pf.r)) {
                    int px = pf.cx + dx;
                    int py = pf.cy + dy;
                    float n = PixelCanvas::fbm2D(px * 0.25f, py * 0.25f, 2, seed + p * 31u);
                    float lightBias = (-dx * 0.35f - dy * 0.65f) / static_cast<float>(pf.r) + n * 0.45f;
                    RGBA8 col = (lightBias < 0.05f) ? leafShadow : ((lightBias > 0.42f) ? leafGold : leafMid);
                    float h = 0.45f + (1.0f - d2 / static_cast<float>(pf.r * pf.r)) * 0.50f;
                    c.setPixel(px, py, col, h);
                }
            }
        }
    }
    finalizeSpriteBevelAndOutline(c, RGBA8{22, 30, 18, 255});
}

void generateWildflowerSprite(PixelCanvas& c, uint32_t seed) {
    c.clear(RGBA8{0, 0, 0, 0}, 0.0f, 0);
    RGBA8 stemDark{38, 86, 44, 255};
    RGBA8 stemLight{68, 134, 62, 255};
    for (int i = 0; i < 14; ++i) {
        int bx = 14 + i * 3;
        int h = 14 + static_cast<int>(PixelCanvas::hash2D(i, 1, seed) * 16.0f);
        for (int dy = 0; dy < h; ++dy) {
            int sway = (dy > h / 2) ? ((i & 1) ? 1 : -1) : 0;
            c.setPixel(bx + sway, 61 - dy, (dy > h / 2) ? stemLight : stemDark, 0.70f);
        }
        if (i % 2 == 0) {
            RGBA8 petal = (i % 4 == 0) ? RGBA8{246, 198, 68, 255} : RGBA8{238, 104, 136, 255};
            c.fillCircle(bx, 61 - h, 3, petal, 0.92f, 0.55f);
            c.setPixel(bx, 61 - h, RGBA8{255, 248, 210, 255}, 0.98f);
        }
    }
    finalizeSpriteBevelAndOutline(c, RGBA8{18, 34, 20, 255});
}

void generateTorchFlameSprite(PixelCanvas& c) {
    c.clear(RGBA8{0, 0, 0, 0}, 0.0f, 0);
    // Iron brazier / sconce base
    c.fillRect(28, 42, 8, 18, RGBA8{54, 48, 56, 255}, 0.75f, 0);
    c.fillRect(25, 39, 14, 4, RGBA8{78, 70, 82, 255}, 0.85f, 0);
    // Outer warm orange flame aura
    c.fillCircle(32, 28, 11, RGBA8{255, 116, 36, 245}, 0.90f, 0.40f, 245);
    c.fillCircle(32, 20, 7, RGBA8{255, 148, 42, 245}, 0.92f, 0.45f, 245);
    // Inner incandescent golden-white core
    c.fillCircle(32, 30, 6, RGBA8{255, 242, 156, 255}, 0.98f, 0.70f, 255);
    finalizeSpriteBevelAndOutline(c, RGBA8{120, 38, 14, 220});
}

} // namespace

ProceduralAtlasData ProceduralAtlasGenerator::buildAllLayers() {
    const auto t0 = std::chrono::high_resolution_clock::now();

    ProceduralAtlasData atlas;
    atlas.tileSize = TexLayer::kTileSize;
    atlas.layerCount = TexLayer::kCount;
    const size_t pixelsPerLayer = static_cast<size_t>(atlas.tileSize) * atlas.tileSize;
    atlas.albedoLayers.resize(atlas.layerCount * pixelsPerLayer);
    atlas.normalLayers.resize(atlas.layerCount * pixelsPerLayer);

    auto writeLayer = [&](uint16_t layerId, const PixelCanvas& canvas, float normalStrength, bool wrapEdges) {
        const auto& alb = canvas.albedoPixels();
        auto norm = canvas.computeNormalEmissiveMap(normalStrength, wrapEdges);
        std::copy(alb.begin(), alb.end(), atlas.albedoLayers.begin() + static_cast<size_t>(layerId) * pixelsPerLayer);
        std::copy(norm.begin(), norm.end(), atlas.normalLayers.begin() + static_cast<size_t>(layerId) * pixelsPerLayer);
    };

    // 1. World Terrain & Architectural Material Layers (0..13)
    {
        PixelCanvas c;
        generateGrassTile(c, {48, 88, 44, 255}, {72, 122, 54, 255}, {106, 156, 68, 255}, {244, 206, 78, 255}, 101u, true);
        writeLayer(TexLayer::GrassMeadow, c, 2.6f, true);

        generateGrassTile(c, {62, 82, 42, 255}, {92, 114, 52, 255}, {128, 144, 68, 255}, {228, 124, 152, 255}, 202u, false);
        writeLayer(TexLayer::GrassHighland, c, 2.6f, true);

        generateCliffStoneTile(c, 303u);
        writeLayer(TexLayer::CliffStone, c, 4.2f, true);

        generateCobblestoneTile(c, 404u);
        writeLayer(TexLayer::Cobblestone, c, 4.0f, true);

        generateDirtPathTile(c, 505u);
        writeLayer(TexLayer::DirtPath, c, 2.5f, true);

        generateWoodPlanksTile(c, 606u);
        writeLayer(TexLayer::WoodPlanks, c, 3.2f, true);

        generateTimberWallTile(c, 707u);
        writeLayer(TexLayer::TimberWall, c, 3.5f, true);

        generateStuccoPlasterTile(c, 808u);
        writeLayer(TexLayer::StuccoPlaster, c, 2.0f, true);

        generateRoofShingleTile(c, {104, 42, 32, 255}, {168, 74, 52, 255}, {206, 104, 72, 255}, 909u);
        writeLayer(TexLayer::TerracottaRoof, c, 3.8f, true);

        generateRoofShingleTile(c, {38, 46, 62, 255}, {64, 78, 102, 255}, {94, 112, 138, 255}, 1010u);
        writeLayer(TexLayer::SlateRoof, c, 3.8f, true);

        generateStoneBrickTile(c, 1111u);
        writeLayer(TexLayer::StoneBrick, c, 3.6f, true);

        generateWindowWarmGlowTile(c);
        writeLayer(TexLayer::WindowWarmGlow, c, 3.0f, false);

        generateWaterSurfaceTile(c, 1212u);
        writeLayer(TexLayer::WaterSurface, c, 2.8f, true);

        generateDirtPathTile(c, 1313u);
        writeLayer(TexLayer::SandShore, c, 2.0f, true);
    }

    // 2. 2.5D Shadow-Casting & Normal-Mapped Billboard Sprite Layers (14..23)
    {
        PixelCanvas c;
        // Hero: Crimson traveler cloak, navy tunic, golden blond hair, broadsword & crest shield
        generateCharacterSprite(c, {186, 44, 48, 255}, {46, 72, 118, 255}, {232, 192, 96, 255}, {218, 174, 72, 255}, true, false);
        writeLayer(TexLayer::SpriteHero, c, 3.2f, false);

        // Villager: Forest green shawl, warm linen tunic, chestnut hair
        generateCharacterSprite(c, {58, 112, 68, 255}, {216, 198, 168, 255}, {114, 68, 44, 255}, {142, 94, 54, 255}, false, false);
        writeLayer(TexLayer::SpriteVillager, c, 3.2f, false);

        // Merchant: Royal purple mantle, ochre coat, dark beard/hair, heavy trade backpack
        generateCharacterSprite(c, {118, 56, 132, 255}, {196, 138, 64, 255}, {62, 48, 44, 255}, {228, 184, 78, 255}, false, true);
        writeLayer(TexLayer::SpriteMerchant, c, 3.2f, false);

        // Town Guard: Cobalt surcoat, steel armor tunic, silver helm/hair, sword & shield
        generateCharacterSprite(c, {44, 84, 156, 255}, {158, 168, 182, 255}, {188, 196, 208, 255}, {214, 172, 74, 255}, true, false);
        writeLayer(TexLayer::SpriteGuard, c, 3.2f, false);

        // Monsters
        generateSlimeMonsterSprite(c);
        writeLayer(TexLayer::SpriteSlime, c, 3.5f, false);

        generateWolfMonsterSprite(c);
        writeLayer(TexLayer::SpriteWolf, c, 3.4f, false);

        // Swaying Flora & Torch Sprites
        generatePineTreeSprite(c, 1414u);
        writeLayer(TexLayer::SpritePineTree, c, 3.2f, false);

        generateOakTreeSprite(c, 1515u);
        writeLayer(TexLayer::SpriteOakTree, c, 3.2f, false);

        generateWildflowerSprite(c, 1616u);
        writeLayer(TexLayer::SpriteWildflower, c, 2.5f, false);

        generateTorchFlameSprite(c);
        writeLayer(TexLayer::SpriteTorchFlame, c, 2.8f, false);
    }

    const auto t1 = std::chrono::high_resolution_clock::now();
    atlas.generationTimeMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
    return atlas;
}

} // namespace eatsrealms
