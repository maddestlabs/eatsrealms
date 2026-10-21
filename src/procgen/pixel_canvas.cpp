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
    RGBA8 deepMoss{
        static_cast<uint8_t>(darkCol.r * 3 / 4),
        static_cast<uint8_t>(darkCol.g * 3 / 4),
        static_cast<uint8_t>(darkCol.b * 3 / 4),
        255
    };
    RGBA8 goldenMoss{
        static_cast<uint8_t>(std::min(255, lightCol.r + 28)),
        static_cast<uint8_t>(std::min(255, lightCol.g + 22)),
        static_cast<uint8_t>(lightCol.b * 2 / 3),
        255
    };

    for (int y = 0; y < s; ++y) {
        for (int x = 0; x < s; ++x) {
            float n = PixelCanvas::fbm2D(x * 0.14f, y * 0.14f, 3, seed) + PixelCanvas::bayer4x4(x, y) * 0.12f;
            RGBA8 col = (n < 0.34f) ? deepMoss : ((n < 0.54f) ? darkCol : ((n < 0.74f) ? midCol : ((n < 0.88f) ? lightCol : goldenMoss)));
            float h = 0.28f + n * 0.44f;
            c.setPixel(x, y, col, h, 0);
        }
    }
    // Crisp pixel-art grass blade clusters & moss sprigs
    for (int i = 0; i < 110; ++i) {
        int bx = static_cast<int>(PixelCanvas::hash2D(i, 1, seed) * s);
        int by = static_cast<int>(PixelCanvas::hash2D(i, 2, seed) * s);
        c.setPixelWrapped(bx, by, deepMoss, 0.22f);
        c.setPixelWrapped(bx, by - 1, midCol, 0.65f);
        c.setPixelWrapped(bx + ((i & 1) ? 1 : -1), by - 2, lightCol, 0.88f);
        if (i % 6 == 0) {
            c.setPixelWrapped(bx, by - 2, goldenMoss, 0.94f);
        }
        if (withFlowers && (i % 14 == 0)) {
            c.setPixelWrapped(bx, by - 2, flowerCol, 0.95f);
            c.setPixelWrapped(bx + 1, by - 2, flowerCol, 0.90f);
        }
    }
}

void generateCliffStoneTile(PixelCanvas& c, uint32_t seed) {
    const int s = static_cast<int>(c.width());
    RGBA8 crevice{36, 32, 42, 255};
    RGBA8 darkRock{68, 64, 76, 255};
    RGBA8 midRock{102, 98, 110, 255};
    RGBA8 warmHigh{138, 132, 138, 255};

    // Deep forest moss colors for the top drooping grass-lip
    RGBA8 mossDark{34, 58, 36, 255};
    RGBA8 mossMid{56, 94, 46, 255};
    RGBA8 mossLight{86, 136, 62, 255};
    RGBA8 mossGold{132, 168, 74, 255};

    for (int y = 0; y < s; ++y) {
        for (int x = 0; x < s; ++x) {
            // Check for top overhanging grass lip (rows 0..12)
            // Creates the signature organic HD-2D moss/grass fringe hanging over cliff faces
            float fringeNoise = PixelCanvas::fbm2D(x * 0.18f, 0.0f, 2, seed + 888u);
            int fringeBottom = 5 + static_cast<int>(fringeNoise * 6.0f);

            if (y <= fringeBottom) {
                float n = PixelCanvas::fbm2D(x * 0.22f, y * 0.22f, 2, seed) + PixelCanvas::bayer4x4(x, y) * 0.15f;
                RGBA8 col = (y <= 2) ? mossMid : ((n < 0.38f) ? mossDark : ((n > 0.65f) ? mossGold : mossLight));
                float h = 0.85f - static_cast<float>(y) * 0.035f;
                c.setPixel(x, y, col, h);
            } else if (y == fringeBottom + 1) {
                // Dark contact shadow cast by the overhanging grass fringe onto the stone below
                c.setPixel(x, y, crevice, 0.15f);
            } else {
                // Stratified horizontal ledges + vertical fractures
                int strataY = (y - fringeBottom) / 8;
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
    RGBA8 deepLoam{44, 38, 32, 255};
    RGBA8 darkEarth{64, 56, 46, 255};
    RGBA8 midEarth{92, 82, 68, 255};
    RGBA8 warmGrit{126, 114, 96, 255};
    RGBA8 lightGravel{162, 150, 134, 255};
    RGBA8 mossFlecks{58, 82, 42, 255};

    for (int y = 0; y < s; ++y) {
        for (int x = 0; x < s; ++x) {
            float n = PixelCanvas::fbm2D(x * 0.16f, y * 0.16f, 3, seed) + PixelCanvas::bayer4x4(x, y) * 0.10f;
            RGBA8 col = (n < 0.28f) ? deepLoam : ((n < 0.55f) ? darkEarth : ((n < 0.80f) ? midEarth : warmGrit));
            // Moss encroaching on shaded soil patches
            float mossN = PixelCanvas::fbm2D(x * 0.22f, y * 0.22f, 2, seed + 404u);
            if (mossN > 0.68f) {
                col = mossFlecks;
            }
            c.setPixel(x, y, col, 0.32f + n * 0.38f);
        }
    }

    // Dense embedded river gravel & smooth rounded pebbles (Octopath woodland path look)
    for (int i = 0; i < 75; ++i) {
        int px = static_cast<int>(PixelCanvas::hash2D(i, 11, seed) * s);
        int py = static_cast<int>(PixelCanvas::hash2D(i, 23, seed) * s);
        int pSize = (i % 6 == 0) ? 3 : ((i % 2 == 0) ? 2 : 1);
        RGBA8 pBase = (i % 3 == 0) ? lightGravel : ((i % 3 == 1) ? warmGrit : RGBA8{142, 136, 126, 255});
        RGBA8 pShadow = deepLoam;
        RGBA8 pHigh = RGBA8{196, 188, 174, 255};

        for (int dy = 0; dy < pSize; ++dy) {
            for (int dx = 0; dx < pSize; ++dx) {
                RGBA8 pc = pBase;
                float ph = 0.72f;
                if (dx == 0 && dy == 0) {
                    pc = pHigh;
                    ph = 0.88f;
                } else if (dx == pSize - 1 && dy == pSize - 1) {
                    pc = pShadow;
                    ph = 0.40f;
                }
                c.setPixelWrapped(px + dx, py + dy, pc, ph);
            }
        }
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
    RGBA8 deepAzure{22, 74, 118, 235};
    RGBA8 midTurquoise{42, 138, 168, 235};
    RGBA8 lightAquamarine{86, 196, 218, 245};
    RGBA8 causticHighlight{210, 246, 255, 255};

    for (int y = 0; y < s; ++y) {
        for (int x = 0; x < s; ++x) {
            float w1 = PixelCanvas::fbm2D(x * 0.15f, y * 0.15f, 3, seed);
            float w2 = PixelCanvas::fbm2D((x + 23) * 0.20f, (y - 17) * 0.20f, 3, seed + 99u);
            float causticRidge = 1.0f - std::abs(w1 - w2) * 3.0f;
            float h = 0.40f + w1 * 0.35f;
            uint8_t emiss = 0;

            RGBA8 col = PixelCanvas::lerpColor(deepAzure, midTurquoise, w1);
            if (causticRidge > 0.72f) {
                float t = (causticRidge - 0.72f) / 0.28f;
                col = PixelCanvas::lerpColor(lightAquamarine, causticHighlight, t);
                h = 0.75f + t * 0.20f;
                emiss = static_cast<uint8_t>(t * 55.0f);
            }
            c.setPixel(x, y, col, h, emiss);
        }
    }
}

void generateSandShoreTile(PixelCanvas& c, uint32_t seed) {
    const int s = static_cast<int>(c.width());
    RGBA8 sandBase{226, 194, 144, 255};
    RGBA8 sandLight{246, 222, 176, 255};
    RGBA8 sandWet{192, 158, 116, 255};
    RGBA8 shellMica{255, 250, 236, 255};

    for (int y = 0; y < s; ++y) {
        for (int x = 0; x < s; ++x) {
            float n1 = PixelCanvas::fbm2D(x * 0.12f, y * 0.12f, 3, seed);
            // Wave wash ripple marks running diagonally
            float waveRipple = std::sin((x * 0.45f + y * 0.25f) + n1 * 4.2f) * 0.5f + 0.5f;
            RGBA8 col = PixelCanvas::lerpColor(sandWet, sandBase, n1);
            col = PixelCanvas::lerpColor(col, sandLight, waveRipple * 0.65f);

            // Sparkling shell / quartz grains
            float spark = PixelCanvas::hash2D(x, y, seed + 888u);
            float h = 0.45f + waveRipple * 0.25f;
            uint8_t emiss = 0;
            if (spark > 0.965f) {
                col = shellMica;
                h = 0.90f;
                emiss = 35;
            }
            c.setPixel(x, y, col, h, emiss);
        }
    }
}

void generateCoastalCliffStoneTile(PixelCanvas& c, uint32_t seed) {
    const int s = static_cast<int>(c.width());
    RGBA8 rockDeep{68, 60, 52, 255};
    RGBA8 rockMid{124, 114, 102, 255};
    RGBA8 rockLight{168, 156, 140, 255};
    RGBA8 calciumWhite{206, 198, 184, 255};

    for (int y = 0; y < s; ++y) {
        for (int x = 0; x < s; ++x) {
            // Horizontal sedimentary bedding planes
            float n = PixelCanvas::fbm2D(x * 0.08f, y * 0.32f, 3, seed);
            float strataPhase = (y * 0.55f) + n * 3.5f;
            float strataLedge = std::sin(strataPhase) * 0.5f + 0.5f;
            float fissure = std::pow(strataLedge, 6.0f);

            RGBA8 col = PixelCanvas::lerpColor(rockMid, rockLight, strataLedge);
            if (fissure > 0.75f) {
                col = rockDeep;
            } else if (strataLedge > 0.88f) {
                col = calciumWhite;
            }
            float h = 0.35f + strataLedge * 0.55f - (fissure > 0.75f ? 0.30f : 0.0f);
            c.setPixel(x, y, col, h, 0);
        }
    }
}

void generateCoastalScreeTile(PixelCanvas& c, uint32_t seed) {
    const int s = static_cast<int>(c.width());
    RGBA8 groundBase{134, 126, 114, 255};
    RGBA8 pebbleLight{172, 162, 148, 255};
    RGBA8 crackDark{78, 70, 62, 255};
    RGBA8 sproutGreen{78, 144, 52, 255};
    RGBA8 sproutTip{132, 202, 74, 255};

    for (int y = 0; y < s; ++y) {
        for (int x = 0; x < s; ++x) {
            float n = PixelCanvas::fbm2D(x * 0.20f, y * 0.20f, 3, seed);
            float pebbleNoise = PixelCanvas::valueNoise2D(x * 0.45f, y * 0.45f, seed + 123u);
            RGBA8 col = PixelCanvas::lerpColor(crackDark, groundBase, n);
            if (pebbleNoise > 0.65f) {
                col = pebbleLight;
            }
            float h = 0.45f + n * 0.35f;

            // Tiny wild coastal sprouts & grass sprigs poking out of crevices
            float sproutH = PixelCanvas::hash2D(x, y, seed + 505u);
            if (sproutH > 0.94f) {
                col = (sproutH > 0.975f) ? sproutTip : sproutGreen;
                h = 0.85f;
            }
            c.setPixel(x, y, col, h, 0);
        }
    }
}

void generatePalmFrondsTile(PixelCanvas& c, uint32_t seed) {
    (void)seed;
    const int s = static_cast<int>(c.width());
    RGBA8 emeraldDeep{34, 82, 38, 255};
    RGBA8 emeraldMid{64, 138, 52, 255};
    RGBA8 sunnyTip{112, 194, 68, 255};
    RGBA8 rachisGold{158, 184, 68, 255};

    for (int y = 0; y < s; ++y) {
        for (int x = 0; x < s; ++x) {
            int distRachis = std::abs(x - 32);
            float leaflet = std::sin((distRachis * 1.8f - y * 1.2f)) * 0.5f + 0.5f;

            RGBA8 col = PixelCanvas::lerpColor(emeraldDeep, emeraldMid, leaflet);
            if (leaflet > 0.75f) {
                col = PixelCanvas::lerpColor(col, sunnyTip, (leaflet - 0.75f) * 4.0f);
            }
            if (distRachis <= 2) {
                col = rachisGold;
            }
            float h = 0.50f + leaflet * 0.40f;
            c.setPixel(x, y, col, h, 0);
        }
    }
}

void generatePalmBarkTile(PixelCanvas& c, uint32_t seed) {
    (void)seed;
    const int s = static_cast<int>(c.width());
    RGBA8 barkBase{138, 98, 64, 255};
    RGBA8 ringRidge{174, 132, 88, 255};
    RGBA8 ringGroove{78, 52, 34, 255};

    for (int y = 0; y < s; ++y) {
        for (int x = 0; x < s; ++x) {
            float ringPhase = static_cast<float>(y % 8) / 8.0f;
            float ringShape = std::sin(ringPhase * 3.14159f);

            RGBA8 col = barkBase;
            float h = 0.50f;
            if (ringPhase < 0.20f) {
                col = ringGroove;
                h = 0.20f;
            } else if (ringShape > 0.65f) {
                col = PixelCanvas::lerpColor(barkBase, ringRidge, (ringShape - 0.65f) * 2.8f);
                h = 0.85f;
            }
            c.setPixel(x, y, col, h, 0);
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

void generatePineFoliageTile(PixelCanvas& c, uint32_t seed) {
    const int s = static_cast<int>(c.width());
    // Authentic Octopath-style rich mossy evergreen conifer palette
    RGBA8 deepCrevice{20, 36, 22, 255};
    RGBA8 needleShadow{30, 52, 28, 255};
    RGBA8 needleMid{46, 78, 40, 255};
    RGBA8 needleFresh{72, 114, 54, 255};
    RGBA8 needleTip{118, 154, 76, 255};
    RGBA8 barkTwig{58, 42, 28, 255};

    for (int y = 0; y < s; ++y) {
        for (int x = 0; x < s; ++x) {
            // Layered conifer bough texture with downward radiating needle tufts
            float n0 = PixelCanvas::fbm2D(x * 0.16f, y * 0.16f, 3, seed);
            float dither = PixelCanvas::bayer4x4(x, y) * 0.12f;
            float val = n0 + dither;

            // Tiered horizontal foliage clumps every 12 pixels
            float boughPhase = std::fmod(static_cast<float>(y) + std::sin(static_cast<float>(x) * 0.35f) * 2.5f, 12.0f);
            float boughT = boughPhase / 12.0f; // 0 at top of tier, 1 at bottom needle tips

            RGBA8 col = needleMid;
            float h = 0.55f;

            if (boughT < 0.18f) {
                // Deep shadowed crevice between foliage tiers
                col = (val < 0.45f) ? deepCrevice : needleShadow;
                h = 0.28f + val * 0.12f;
            } else if (boughT > 0.78f) {
                // Outward-reaching needle tips
                col = (val > 0.55f) ? needleTip : needleFresh;
                h = 0.72f + val * 0.18f;
            } else {
                // Dense mid-foliage needle body
                col = (val < 0.38f) ? needleShadow : ((val > 0.68f) ? needleFresh : needleMid);
                h = 0.46f + val * 0.30f;
            }

            // Fine wooden twig accents embedded in the branch interior
            if (boughT < 0.25f && std::abs(x % 16 - 8) <= 1 && val > 0.62f) {
                col = barkTwig;
                h = 0.32f;
            }

            c.setPixel(x, y, col, h, 0);
        }
    }

    // Crisp pixel-art needle strokes
    for (int i = 0; i < 120; ++i) {
        int nx = static_cast<int>(PixelCanvas::hash2D(i, 11, seed) * s);
        int ny = static_cast<int>(PixelCanvas::hash2D(i, 23, seed) * s);
        int len = 2 + (i % 2);
        RGBA8 tipCol = (i % 2 == 0) ? needleFresh : needleTip;
        for (int l = 0; l < len; ++l) {
            int py = (ny + l) % s;
            int px = (nx + (l % 2)) % s;
            c.setPixel(px, py, tipCol, 0.75f + static_cast<float>(l) * 0.05f, 0);
        }
    }
}

void generateOakFoliageTile(PixelCanvas& c, uint32_t seed) {
    const int s = static_cast<int>(c.width());
    // Authentic Octopath-style rich broadleaf oak / deciduous canopy palette
    RGBA8 deepCrevice{24, 40, 20, 255};
    RGBA8 leafShadow{36, 62, 30, 255};
    RGBA8 leafMid{56, 96, 44, 255};
    RGBA8 leafFresh{82, 138, 58, 255};
    RGBA8 leafSunlit{126, 172, 74, 255};
    RGBA8 leafHighlight{168, 198, 92, 255};
    RGBA8 twigBark{62, 44, 28, 255};

    for (int y = 0; y < s; ++y) {
        for (int x = 0; x < s; ++x) {
            float n0 = PixelCanvas::fbm2D(x * 0.12f, y * 0.12f, 3, seed);
            float dither = PixelCanvas::bayer4x4(x, y) * 0.12f;
            float val = n0 + dither;

            // Clustered scalloped leaf puffs
            float puffWave = std::sin(static_cast<float>(x) * 0.42f) * 1.8f +
                             std::cos(static_cast<float>(y) * 0.38f) * 1.8f;
            float puffPhase = std::fmod(static_cast<float>(x + y * 2) + puffWave + 64.0f, 14.0f) / 14.0f;

            RGBA8 col = leafMid;
            float h = 0.55f;

            if (val < 0.32f || puffPhase < 0.14f) {
                col = (val < 0.22f) ? deepCrevice : leafShadow;
                h = 0.26f + val * 0.15f;
            } else if (val > 0.72f && puffPhase > 0.65f) {
                col = (val > 0.82f) ? leafHighlight : leafSunlit;
                h = 0.78f + val * 0.16f;
            } else if (val > 0.52f) {
                col = (puffPhase > 0.50f) ? leafSunlit : leafFresh;
                h = 0.58f + val * 0.20f;
            } else {
                col = (puffPhase < 0.35f) ? leafShadow : leafMid;
                h = 0.42f + val * 0.25f;
            }

            if (puffPhase < 0.08f && (x % 16 == 0 || y % 16 == 0) && val < 0.28f) {
                col = twigBark;
                h = 0.22f;
            }

            c.setPixel(x, y, col, std::clamp(h, 0.15f, 0.98f), 0);
        }
    }

    // Leaf cluster micro-details (dappled sunlight spots)
    for (int i = 0; i < 90; ++i) {
        int nx = static_cast<int>(PixelCanvas::hash2D(i, 13, seed) * s);
        int ny = static_cast<int>(PixelCanvas::hash2D(i, 29, seed) * s);
        RGBA8 col = (i % 3 == 0) ? leafHighlight : ((i % 3 == 1) ? leafSunlit : deepCrevice);
        float h = (i % 3 == 2) ? 0.25f : 0.82f;
        c.setPixel(nx % s, ny % s, col, h, 0);
        c.setPixel((nx + 1) % s, ny % s, col, h, 0);
        c.setPixel(nx % s, (ny + 1) % s, col, h, 0);
    }
}

void generateSnowPineFoliageTile(PixelCanvas& c, uint32_t seed) {
    const int s = static_cast<int>(c.width());
    // Two-tone snow-draped conifer needle bough palette
    RGBA8 deepCrevice{22, 38, 26, 255};
    RGBA8 needleShadow{32, 56, 36, 255};
    RGBA8 needleMid{48, 80, 48, 255};
    RGBA8 snowIceBlue{195, 218, 238, 255};
    RGBA8 snowPureWhite{245, 250, 255, 255};
    RGBA8 snowCrust{218, 234, 248, 255};

    for (int y = 0; y < s; ++y) {
        for (int x = 0; x < s; ++x) {
            float n0 = PixelCanvas::fbm2D(x * 0.16f, y * 0.16f, 3, seed);
            float dither = PixelCanvas::bayer4x4(x, y) * 0.10f;
            float val = n0 + dither;

            float boughPhase = std::fmod(static_cast<float>(y) + std::sin(static_cast<float>(x) * 0.35f) * 2.5f, 12.0f);
            float boughT = boughPhase / 12.0f;

            RGBA8 col = needleMid;
            float h = 0.55f;

            if (boughT < 0.48f) {
                // Heavy snow blanket resting on top of the branch tier
                if (boughT < 0.15f) {
                    col = snowCrust;
                    h = 0.70f + val * 0.15f;
                } else if (val > 0.42f) {
                    col = snowPureWhite;
                    h = 0.85f + val * 0.12f;
                } else {
                    col = snowIceBlue;
                    h = 0.75f + val * 0.15f;
                }
            } else if (boughT > 0.82f) {
                // Needle tips peeking out below the snow bank
                col = (val > 0.50f) ? needleMid : needleShadow;
                h = 0.42f + val * 0.20f;
            } else {
                col = (val > 0.60f) ? snowIceBlue : deepCrevice;
                h = 0.50f + val * 0.20f;
            }

            c.setPixel(x, y, col, std::clamp(h, 0.20f, 0.98f), 0);
        }
    }
}

void generateAutumnFoliageTile(PixelCanvas& c, uint32_t seed) {
    const int s = static_cast<int>(c.width());
    // Warm golden hour autumn palette (amber, pumpkin gold, scarlet accents)
    RGBA8 deepCrevice{46, 26, 18, 255};
    RGBA8 amberShadow{92, 48, 22, 255};
    RGBA8 amberMid{168, 92, 32, 255};
    RGBA8 goldenOrange{218, 142, 42, 255};
    RGBA8 brightGold{248, 196, 68, 255};
    RGBA8 crimsonAccent{182, 54, 38, 255};

    for (int y = 0; y < s; ++y) {
        for (int x = 0; x < s; ++x) {
            float n0 = PixelCanvas::fbm2D(x * 0.12f, y * 0.12f, 3, seed);
            float dither = PixelCanvas::bayer4x4(x, y) * 0.12f;
            float val = n0 + dither;

            float puffWave = std::sin(static_cast<float>(x) * 0.42f) * 1.8f +
                             std::cos(static_cast<float>(y) * 0.38f) * 1.8f;
            float puffPhase = std::fmod(static_cast<float>(x + y * 2) + puffWave + 64.0f, 14.0f) / 14.0f;

            RGBA8 col = amberMid;
            float h = 0.55f;

            if (val < 0.28f || puffPhase < 0.12f) {
                col = (val < 0.18f) ? deepCrevice : amberShadow;
                h = 0.28f + val * 0.14f;
            } else if (val > 0.70f && puffPhase > 0.60f) {
                col = (val > 0.82f) ? brightGold : goldenOrange;
                h = 0.80f + val * 0.15f;
            } else if (val > 0.48f) {
                col = (puffPhase > 0.50f) ? goldenOrange : amberMid;
                h = 0.58f + val * 0.20f;
            } else {
                col = (puffPhase < 0.32f) ? amberShadow : amberMid;
                h = 0.40f + val * 0.22f;
            }

            if (val > 0.58f && puffPhase > 0.38f && puffPhase < 0.48f) {
                col = crimsonAccent;
            }

            c.setPixel(x, y, col, std::clamp(h, 0.18f, 0.98f), 0);
        }
    }
}

void generatePineTreeSprite(PixelCanvas& c, uint32_t seed) {
    c.clear(RGBA8{0, 0, 0, 0}, 0.0f, 0);
    RGBA8 trunkDark{58, 36, 22, 255};
    RGBA8 trunkLight{88, 56, 34, 255};
    RGBA8 needleShadow{20, 48, 34, 255};
    RGBA8 needleMid{38, 86, 52, 255};
    RGBA8 needleSun{72, 134, 74, 255};
    RGBA8 needleRimLight{156, 204, 102, 255}; // Warm golden backlight rim on conifer bough tips

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

                // Backlight rim highlight on upper-left edges (dx < 0, dy < -tr.h/3)
                bool isRimEdge = (dx < -span + 3 || (dy < -tr.h + 3 && dx < 0));
                RGBA8 col = isRimEdge ? needleRimLight :
                            ((dy > 1 || n < 0.32f) ? needleShadow :
                            ((dy < -tr.h / 2 && n > 0.55f) ? needleSun : needleMid));
                c.setPixel(px, py, col, std::clamp(0.35f + dome * 0.60f, 0.25f, 0.98f));
            }
        }
    }
    finalizeSpriteBevelAndOutline(c, RGBA8{14, 26, 18, 255});
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

void generateWildflowerSprite(PixelCanvas& c, uint32_t /*seed*/) {
    c.clear(RGBA8{0, 0, 0, 0}, 0.0f, 0);
    RGBA8 fernDark{24, 62, 32, 255};
    RGBA8 fernMid{44, 102, 48, 255};
    RGBA8 fernLight{76, 148, 68, 255};
    RGBA8 fernTip{128, 186, 88, 255};

    // Radiating woodland star-fern fronds
    const float angles[7] = {-1.35f, -0.92f, -0.45f, 0.0f, 0.45f, 0.92f, 1.35f};
    for (int i = 0; i < 7; ++i) {
        float a = angles[i] - 1.5708f; // Radiate upward from center base (32, 54)
        int len = 14 + (i % 2 == 0 ? 4 : 0);
        for (int step = 0; step < len; ++step) {
            float dist = static_cast<float>(step);
            float arch = std::sin(dist / static_cast<float>(len) * 3.14159f) * 2.5f * (i < 3 ? -1.0f : 1.0f);
            int px = 32 + static_cast<int>(std::cos(a) * dist + arch);
            int py = 54 + static_cast<int>(std::sin(a) * dist);

            float t = dist / static_cast<float>(len);
            RGBA8 col = (t > 0.75f) ? fernTip : ((t > 0.40f) ? fernLight : ((t > 0.15f) ? fernMid : fernDark));
            c.setPixel(px, py, col, 0.85f - t * 0.35f);
            // Leaflet side-serrations
            if (step > 4 && step % 2 == 0) {
                c.setPixel(px - 1, py, fernMid, 0.65f);
                c.setPixel(px + 1, py, fernLight, 0.75f);
            }
        }
    }
    // Small moss boulder beside the fern
    RGBA8 rockDark{54, 52, 60, 255};
    RGBA8 rockMoss{82, 126, 60, 255};
    c.fillCircle(18, 54, 6, rockDark, 0.75f, 0.35f);
    c.fillCircle(18, 51, 4, rockMoss, 0.88f, 0.55f);

    // Delicate woodland blossom stars blooming on the fern fronds
    RGBA8 petalWhite{252, 248, 240, 255};
    RGBA8 petalBlush{246, 178, 196, 255};
    RGBA8 flowerCenter{255, 218, 72, 255};

    struct BlossomDot { int x; int y; };
    const BlossomDot dots[4] = {{27, 43}, {36, 40}, {39, 48}, {24, 49}};
    for (const auto& b : dots) {
        c.setPixel(b.x, b.y, flowerCenter, 0.95f);
        c.setPixel(b.x - 1, b.y, petalWhite, 0.90f);
        c.setPixel(b.x + 1, b.y, petalWhite, 0.90f);
        c.setPixel(b.x, b.y - 1, petalBlush, 0.90f);
        c.setPixel(b.x, b.y + 1, petalBlush, 0.90f);
    }

    finalizeSpriteBevelAndOutline(c, RGBA8{16, 28, 18, 255});
}

void generateGrassTuftSprite(PixelCanvas& c, uint32_t seed) {
    (void)seed;
    c.clear(RGBA8{0, 0, 0, 0}, 0.0f, 0);

    RGBA8 bladeDeep{22, 46, 18, 255};
    RGBA8 bladeMid{48, 108, 38, 255};
    RGBA8 bladeLight{98, 178, 54, 255};
    RGBA8 bladeTip{188, 224, 78, 255};

    // Base root clump
    c.fillCircle(32, 58, 5, bladeDeep, 0.55f, 0.20f);

    // Dense fan of 11 grass blades with organic curves and varied heights
    struct BladeDef {
        float startAngle;
        int length;
        float curvature;
        int thickness;
    };

    const BladeDef blades[11] = {
        {-1.15f, 15, -1.8f, 1},
        {-0.85f, 21, -2.5f, 2},
        {-0.55f, 26, -1.6f, 2},
        {-0.28f, 31, -0.9f, 2},
        {-0.08f, 34,  0.4f, 2},
        { 0.12f, 32, -0.6f, 2},
        { 0.35f, 29,  1.2f, 2},
        { 0.62f, 24,  2.1f, 2},
        { 0.90f, 19,  2.8f, 2},
        { 1.18f, 14,  2.2f, 1},
        {-0.42f, 18, -3.2f, 1},
    };

    for (int b = 0; b < 11; ++b) {
        const auto& bl = blades[b];
        float baseAngle = bl.startAngle - 1.5707963f;
        int len = bl.length;

        for (int step = 0; step < len; ++step) {
            float t = static_cast<float>(step) / static_cast<float>(len);
            float bend = std::sin(t * 1.5707963f) * bl.curvature * 4.0f;
            float dist = static_cast<float>(step);

            int px = 32 + static_cast<int>(std::cos(baseAngle) * dist + bend);
            int py = 57 + static_cast<int>(std::sin(baseAngle) * dist);

            RGBA8 col = (t > 0.82f) ? bladeTip : ((t > 0.50f) ? bladeLight : ((t > 0.20f) ? bladeMid : bladeDeep));
            float h = 0.85f - t * 0.40f;

            c.setPixel(px, py, col, h);
            if (bl.thickness >= 2 && step < len - 3) {
                int offsetDir = (bl.curvature < 0.0f) ? -1 : 1;
                c.setPixel(px + offsetDir, py, (t > 0.6f) ? bladeLight : bladeMid, h * 0.9f);
            }
        }
    }

    // Small clover leaflet at the base
    c.setPixel(28, 56, bladeLight, 0.70f);
    c.setPixel(27, 56, bladeMid, 0.60f);
    c.setPixel(36, 56, bladeLight, 0.70f);
    c.setPixel(37, 56, bladeMid, 0.60f);

    finalizeSpriteBevelAndOutline(c, RGBA8{16, 28, 14, 255});
}

void generateTallGrassSprite(PixelCanvas& c, uint32_t seed) {
    (void)seed;
    c.clear(RGBA8{0, 0, 0, 0}, 0.0f, 0);

    RGBA8 stemDark{36, 68, 28, 255};
    RGBA8 stemMid{68, 126, 48, 255};
    RGBA8 stemLight{118, 182, 68, 255};
    RGBA8 huskGold{218, 192, 116, 255};
    RGBA8 huskAmber{182, 144, 72, 255};
    RGBA8 huskTip{245, 230, 168, 255};

    struct StemDef {
        int baseX;
        int height;
        float lean;
    };
    const StemDef stems[4] = {
        {26, 44, -3.8f},
        {31, 50,  2.2f},
        {35, 46,  4.5f},
        {29, 36, -2.0f},
    };

    for (int s = 0; s < 4; ++s) {
        const auto& st = stems[s];
        int startY = 58;
        for (int y = 0; y < st.height; ++y) {
            float t = static_cast<float>(y) / static_cast<float>(st.height);
            float sway = std::pow(t, 1.6f) * st.lean;
            int px = st.baseX + static_cast<int>(sway);
            int py = startY - y;

            RGBA8 col = (t > 0.65f) ? stemLight : ((t > 0.25f) ? stemMid : stemDark);
            c.setPixel(px, py, col, 0.80f - t * 0.30f);

            // Drooping leaves along stem joints
            if (y == st.height / 3 || y == (st.height * 2) / 3) {
                int leafDir = (s % 2 == 0) ? -1 : 1;
                for (int l = 1; l <= 5; ++l) {
                    c.setPixel(px + l * leafDir, py + l / 2, stemMid, 0.65f);
                }
            }

            // Feathery seed head at top (last 12 pixels)
            if (y >= st.height - 12) {
                float headT = static_cast<float>(y - (st.height - 12)) / 12.0f;
                RGBA8 seedCol = (headT > 0.7f) ? huskTip : ((headT > 0.3f) ? huskGold : huskAmber);
                c.setPixel(px - 1, py, seedCol, 0.90f);
                c.setPixel(px + 1, py, seedCol, 0.85f);
                if (y % 2 == 0) {
                    c.setPixel(px - 2, py, huskAmber, 0.70f);
                    c.setPixel(px + 2, py, huskGold, 0.75f);
                }
            }
        }
    }

    finalizeSpriteBevelAndOutline(c, RGBA8{20, 28, 16, 255});
}

void generateWildflowerBouquetSprite(PixelCanvas& c, uint32_t type, uint32_t seed) {
    (void)seed;
    c.clear(RGBA8{0, 0, 0, 0}, 0.0f, 0);

    RGBA8 stemDeep{28, 58, 26, 255};
    RGBA8 stemMid{52, 114, 44, 255};
    RGBA8 stemLight{96, 172, 60, 255};

    // Base foliage rosette
    for (int angleIdx = 0; angleIdx < 6; ++angleIdx) {
        float a = static_cast<float>(angleIdx) * 1.047f - 1.57f;
        for (int r = 0; r < 8; ++r) {
            int lx = 32 + static_cast<int>(std::cos(a) * r);
            int ly = 56 + static_cast<int>(std::sin(a) * r * 0.45f);
            RGBA8 col = (r > 4) ? stemLight : ((r > 1) ? stemMid : stemDeep);
            c.setPixel(lx, ly, col, 0.70f - static_cast<float>(r) * 0.04f);
        }
    }

    struct FlowerPos {
        int cx;
        int cy;
        int stemX0;
        int stemY0;
        int size;
    };

    if (type == 0) {
        // Red Poppy / Scarlet Anemone Bouquet
        const FlowerPos poppies[4] = {
            {23, 34, 30, 56, 6},
            {35, 27, 33, 56, 7},
            {43, 37, 34, 56, 5},
            {28, 44, 31, 56, 5},
        };

        RGBA8 petalDark{146, 20, 32, 255};
        RGBA8 petalMid{212, 38, 48, 255};
        RGBA8 petalBright{255, 68, 76, 255};
        RGBA8 petalGlow{255, 126, 118, 255};
        RGBA8 centerDark{32, 14, 22, 255};
        RGBA8 centerGold{255, 224, 88, 255};

        for (const auto& fl : poppies) {
            for (int y = fl.cy + fl.size - 1; y <= fl.stemY0; ++y) {
                float t = static_cast<float>(y - fl.cy) / static_cast<float>(fl.stemY0 - fl.cy);
                int sx = fl.cx + static_cast<int>((fl.stemX0 - fl.cx) * t + std::sin(t * 3.14f) * 1.5f);
                c.setPixel(sx, y, stemMid, 0.65f);
            }

            int r = fl.size;
            c.fillCircle(fl.cx, fl.cy, r, petalMid, 0.90f, 0.40f);
            c.fillCircle(fl.cx - 1, fl.cy - 1, r - 2, petalBright, 0.96f, 0.60f);
            c.setPixel(fl.cx - 2, fl.cy - 2, petalGlow, 0.98f);
            c.setPixel(fl.cx + 1, fl.cy - 2, petalGlow, 0.98f);
            c.setPixel(fl.cx, fl.cy, centerDark, 0.75f);
            c.setPixel(fl.cx + 1, fl.cy, centerDark, 0.75f);
            c.setPixel(fl.cx, fl.cy + 1, petalDark, 0.70f);
            c.setPixel(fl.cx - 1, fl.cy, centerGold, 0.85f);
        }
    } else if (type == 1) {
        // Cerulean Bluebell / Forget-Me-Not Bouquet
        const FlowerPos bells[5] = {
            {22, 32, 29, 56, 5},
            {32, 24, 32, 56, 6},
            {42, 30, 34, 56, 5},
            {26, 42, 30, 56, 4},
            {38, 40, 33, 56, 5},
        };

        RGBA8 blueDeep{34, 64, 168, 255};
        RGBA8 blueMid{64, 122, 235, 255};
        RGBA8 blueLight{126, 178, 255, 255};
        RGBA8 blueGlow{202, 224, 255, 255};
        RGBA8 eyeWhite{252, 255, 242, 255};

        for (const auto& fl : bells) {
            for (int y = fl.cy + fl.size - 1; y <= fl.stemY0; ++y) {
                float t = static_cast<float>(y - fl.cy) / static_cast<float>(fl.stemY0 - fl.cy);
                int sx = fl.cx + static_cast<int>((fl.stemX0 - fl.cx) * t);
                c.setPixel(sx, y, stemMid, 0.65f);
            }

            int r = fl.size;
            c.fillCircle(fl.cx, fl.cy, r, blueMid, 0.88f, 0.40f);
            c.fillCircle(fl.cx, fl.cy - 1, r - 2, blueLight, 0.95f, 0.60f);
            c.setPixel(fl.cx - 1, fl.cy - 1, blueGlow, 0.98f);
            c.setPixel(fl.cx, fl.cy, eyeWhite, 0.90f);
            c.setPixel(fl.cx, fl.cy + 1, blueDeep, 0.70f);
        }
    } else {
        // Sunny Golden Buttercup & Daisy Bouquet
        const FlowerPos buttercups[5] = {
            {24, 28, 30, 56, 6},
            {34, 23, 32, 56, 7},
            {43, 33, 35, 56, 5},
            {26, 41, 30, 56, 5},
            {37, 38, 33, 56, 6},
        };

        RGBA8 goldDark{196, 124, 18, 255};
        RGBA8 goldMid{255, 196, 32, 255};
        RGBA8 goldBright{255, 236, 78, 255};
        RGBA8 goldGlow{255, 250, 164, 255};
        RGBA8 daisyWhite{246, 248, 242, 255};

        for (int i = 0; i < 5; ++i) {
            const auto& fl = buttercups[i];
            for (int y = fl.cy + fl.size - 1; y <= fl.stemY0; ++y) {
                float t = static_cast<float>(y - fl.cy) / static_cast<float>(fl.stemY0 - fl.cy);
                int sx = fl.cx + static_cast<int>((fl.stemX0 - fl.cx) * t);
                c.setPixel(sx, y, stemMid, 0.65f);
            }

            int r = fl.size;
            if (i == 1) {
                c.fillCircle(fl.cx, fl.cy, r, daisyWhite, 0.90f, 0.45f);
                c.fillCircle(fl.cx, fl.cy, 2, goldMid, 0.98f, 0.80f);
                c.setPixel(fl.cx, fl.cy, goldDark, 0.95f);
            } else {
                c.fillCircle(fl.cx, fl.cy, r, goldMid, 0.90f, 0.45f);
                c.fillCircle(fl.cx - 1, fl.cy - 1, r - 2, goldBright, 0.96f, 0.65f);
                c.setPixel(fl.cx - 2, fl.cy - 2, goldGlow, 0.98f);
                c.setPixel(fl.cx, fl.cy, goldDark, 0.85f);
            }
        }
    }

    finalizeSpriteBevelAndOutline(c, RGBA8{20, 26, 16, 255});
}

void generateCloverPatchSprite(PixelCanvas& c, uint32_t seed) {
    (void)seed;
    c.clear(RGBA8{0, 0, 0, 0}, 0.0f, 0);

    RGBA8 leafDark{24, 64, 28, 255};
    RGBA8 leafMid{46, 122, 52, 255};
    RGBA8 leafLight{88, 186, 76, 255};
    RGBA8 leafMark{144, 218, 122, 255};
    RGBA8 flowerWhite{244, 246, 238, 255};
    RGBA8 flowerPink{242, 196, 214, 255};

    struct CloverCluster {
        int cx;
        int cy;
        int radius;
    };
    const CloverCluster clovers[7] = {
        {19, 52, 5},
        {27, 48, 6},
        {36, 47, 6},
        {45, 51, 5},
        {31, 54, 5},
        {22, 44, 4},
        {40, 43, 4},
    };

    for (const auto& cl : clovers) {
        const float angles[3] = {-2.35f, -1.57f, -0.78f};
        for (int i = 0; i < 3; ++i) {
            int lx = cl.cx + static_cast<int>(std::cos(angles[i]) * (static_cast<float>(cl.radius) * 0.85f));
            int ly = cl.cy + static_cast<int>(std::sin(angles[i]) * (static_cast<float>(cl.radius) * 0.85f));
            c.fillCircle(lx, ly, cl.radius - 2, leafMid, 0.80f, 0.40f);
            c.setPixel(lx, ly, leafLight, 0.85f);
            c.setPixel(lx, ly - 1, leafMark, 0.90f);
        }
        c.setPixel(cl.cx, cl.cy, leafDark, 0.60f);
    }

    // 2 small fluffy white/pink clover flowers on thin stalks
    c.setPixel(25, 43, leafMid, 0.65f);
    c.setPixel(25, 42, leafMid, 0.65f);
    c.fillCircle(25, 40, 2, flowerWhite, 0.92f, 0.60f);
    c.setPixel(25, 39, flowerPink, 0.95f);

    c.setPixel(38, 39, leafMid, 0.65f);
    c.setPixel(38, 38, leafMid, 0.65f);
    c.fillCircle(38, 36, 2, flowerWhite, 0.92f, 0.60f);
    c.setPixel(38, 35, flowerPink, 0.95f);

    finalizeSpriteBevelAndOutline(c, RGBA8{16, 28, 16, 255});
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

void generatePalmTreeSprite(PixelCanvas& c, uint32_t seed) {
    (void)seed;
    c.clear(RGBA8{0, 0, 0, 0}, 0.0f, 0);
    RGBA8 barkBase{138, 98, 64, 255};
    RGBA8 barkRidge{174, 132, 88, 255};
    RGBA8 barkDark{84, 56, 36, 255};
    RGBA8 frondDeep{34, 82, 38, 255};
    RGBA8 frondMid{64, 138, 52, 255};
    RGBA8 frondLight{112, 194, 68, 255};
    RGBA8 coconutCol{106, 74, 42, 255};

    // Curved slender palm trunk leaning towards the right
    // Ground at y = 58, Crown at (38, 22)
    for (int y = 22; y <= 58; ++y) {
        float t = static_cast<float>(58 - y) / 36.0f;
        float cx = 26.0f + std::pow(t, 1.4f) * 12.0f;
        int halfW = static_cast<int>(3.2f - t * 1.4f);
        if (halfW < 1) halfW = 1;

        bool isRing = (y % 4 == 0);
        RGBA8 col = isRing ? barkRidge : ((y % 4 == 1) ? barkDark : barkBase);
        c.fillRect(static_cast<int>(cx) - halfW, y, halfW * 2 + 1, 1, col, 0.75f - t * 0.15f);
    }

    // Coconut drupe cluster at crown
    c.fillCircle(37, 24, 2, coconutCol, 0.92f, 0.6f);
    c.fillCircle(40, 25, 2, coconutCol, 0.92f, 0.6f);
    c.fillCircle(38, 26, 2, coconutCol, 0.90f, 0.6f);

    // Radiating arched feather fronds
    struct FrondArch {
        float tipX;
        float tipY;
        float apexX;
        float apexY;
    };
    const FrondArch fronds[] = {
        { 10.0f, 32.0f, 22.0f, 16.0f },
        {  8.0f, 22.0f, 20.0f, 12.0f },
        { 16.0f, 12.0f, 28.0f,  8.0f },
        { 32.0f,  7.0f, 36.0f,  5.0f },
        { 48.0f,  9.0f, 44.0f,  6.0f },
        { 58.0f, 18.0f, 50.0f, 12.0f },
        { 60.0f, 28.0f, 52.0f, 18.0f },
        { 54.0f, 36.0f, 48.0f, 26.0f }
    };

    float crownX = 38.0f;
    float crownY = 22.0f;
    for (const auto& fa : fronds) {
        for (int step = 0; step <= 24; ++step) {
            float u = static_cast<float>(step) / 24.0f;
            float omt = 1.0f - u;
            float px = omt * omt * crownX + 2.0f * omt * u * fa.apexX + u * u * fa.tipX;
            float py = omt * omt * crownY + 2.0f * omt * u * fa.apexY + u * u * fa.tipY;

            int ix = static_cast<int>(std::round(px));
            int iy = static_cast<int>(std::round(py));
            float spread = std::sin(u * 3.14159f) * 2.5f + 0.5f;

            RGBA8 leafCol = (u > 0.7f) ? frondLight : ((u > 0.3f) ? frondMid : frondDeep);
            int ispr = static_cast<int>(spread);
            for (int dx = -ispr; dx <= ispr; ++dx) {
                c.setPixel(ix + dx, iy, leafCol, 0.85f - u * 0.3f);
            }
        }
    }

    finalizeSpriteBevelAndOutline(c, RGBA8{22, 34, 18, 255});
}

void generateClericSprite(PixelCanvas& c) {
    c.clear(RGBA8{0, 0, 0, 0}, 0.0f, 0);
    RGBA8 ivoryRobe{194, 188, 178, 255};
    RGBA8 goldTrim{214, 172, 64, 255};
    RGBA8 mantleWhite{212, 208, 202, 255};
    RGBA8 hairBlond{208, 176, 88, 255};
    RGBA8 silverStaff{202, 214, 226, 255};
    RGBA8 skinLight{238, 202, 174, 255};
    RGBA8 eyeCol{44, 40, 52, 255};

    // Boots / hem
    c.fillRect(26, 49, 12, 11, ivoryRobe, 0.70f);
    c.fillRect(26, 57, 12, 3, goldTrim, 0.75f);

    // Flowing white mantle / cloak
    c.fillCircle(32, 38, 14, mantleWhite, 0.75f, 0.30f);
    c.fillRect(23, 28, 18, 20, ivoryRobe, 0.85f);
    c.fillRect(23, 40, 18, 3, goldTrim, 0.90f);

    // Cleric shoulder stole
    c.fillRect(28, 27, 8, 15, goldTrim, 0.92f);
    c.fillRect(30, 27, 4, 15, RGBA8{214, 82, 72, 255}, 0.94f);

    // Head & face
    c.fillCircle(32, 18, 8, skinLight, 0.92f, 0.45f);
    c.fillRect(29, 18, 2, 4, eyeCol, 0.85f);
    c.fillRect(34, 18, 2, 4, eyeCol, 0.85f);
    c.setPixel(29, 18, RGBA8{255, 255, 255, 255}, 0.92f);
    c.setPixel(34, 18, RGBA8{255, 255, 255, 255}, 0.92f);

    // Hair & Holy Circlet
    c.fillCircle(32, 13, 9, hairBlond, 0.95f, 0.50f);
    c.fillRect(26, 14, 12, 2, goldTrim, 0.98f, 0);

    // Prayer staff in hand
    for (int y = 14; y <= 56; ++y) {
        c.setPixel(44, y, silverStaff, 0.90f);
    }
    c.fillCircle(44, 12, 4, goldTrim, 0.98f, 0.6f, 0);

    finalizeSpriteBevelAndOutline(c);
}

void generateScholarSprite(PixelCanvas& c) {
    c.clear(RGBA8{0, 0, 0, 0}, 0.0f, 0);
    RGBA8 coatIndigo{38, 48, 88, 255};
    RGBA8 mantleViolet{78, 44, 98, 255};
    RGBA8 bookLeather{152, 72, 46, 255};
    RGBA8 bookGold{232, 192, 78, 255};
    RGBA8 hairSilver{196, 204, 218, 255};
    RGBA8 skinLight{246, 208, 178, 255};
    RGBA8 eyeCol{34, 30, 42, 255};

    // Dark shoes & robe base
    c.fillRect(25, 49, 14, 11, coatIndigo, 0.70f);
    c.fillRect(24, 28, 16, 22, coatIndigo, 0.85f);
    c.fillRect(22, 27, 20, 7, mantleViolet, 0.88f);

    // Leather satchel strap
    for (int i = 0; i < 14; ++i) {
        c.setPixel(25 + i, 28 + i, bookLeather, 0.92f);
    }
    c.fillRect(36, 38, 7, 8, bookLeather, 0.90f);

    // Head & face
    c.fillCircle(32, 18, 8, skinLight, 0.92f, 0.45f);
    c.fillRect(28, 18, 2, 4, eyeCol, 0.85f);
    c.fillRect(34, 18, 2, 4, eyeCol, 0.85f);
    c.setPixel(27, 18, RGBA8{210, 225, 240, 255}, 0.95f);
    c.setPixel(30, 18, RGBA8{210, 225, 240, 255}, 0.95f);
    c.setPixel(33, 18, RGBA8{210, 225, 240, 255}, 0.95f);
    c.setPixel(36, 18, RGBA8{210, 225, 240, 255}, 0.95f);

    // Hair
    c.fillCircle(32, 13, 9, hairSilver, 0.95f, 0.50f);

    // Open grimoire in left hand
    c.fillRect(16, 33, 8, 9, bookLeather, 0.95f);
    c.fillRect(18, 34, 5, 7, RGBA8{248, 242, 224, 255}, 0.96f);
    c.setPixel(20, 37, bookGold, 0.98f);

    finalizeSpriteBevelAndOutline(c);
}

void generateDancerSprite(PixelCanvas& c) {
    c.clear(RGBA8{0, 0, 0, 0}, 0.0f, 0);
    RGBA8 rubyBodice{186, 42, 68, 255};
    RGBA8 lilacSash{172, 108, 198, 255};
    RGBA8 goldJewel{242, 196, 68, 255};
    RGBA8 hairEbony{32, 26, 34, 255};
    RGBA8 skinWarm{244, 198, 168, 255};
    RGBA8 eyeCol{36, 28, 38, 255};

    // Bare legs & dancer slippers with ribbon ties
    c.fillRect(27, 47, 4, 13, skinWarm, 0.70f);
    c.fillRect(34, 47, 4, 13, skinWarm, 0.70f);
    c.fillRect(26, 57, 5, 3, rubyBodice, 0.75f);
    c.fillRect(34, 57, 5, 3, rubyBodice, 0.75f);

    // Flared festive dancer skirt & sash
    c.fillCircle(32, 42, 11, lilacSash, 0.82f, 0.35f);
    c.fillRect(25, 39, 14, 4, goldJewel, 0.90f);

    // Bodice
    c.fillRect(26, 29, 12, 10, rubyBodice, 0.88f);
    c.fillRect(29, 30, 6, 4, goldJewel, 0.92f);

    // Head & face
    c.fillCircle(32, 18, 8, skinWarm, 0.92f, 0.45f);
    c.fillRect(28, 18, 2, 4, eyeCol, 0.85f);
    c.fillRect(34, 18, 2, 4, eyeCol, 0.85f);
    c.setPixel(28, 18, RGBA8{255, 255, 255, 255}, 0.90f);
    c.setPixel(34, 18, RGBA8{255, 255, 255, 255}, 0.90f);

    // Flowing dark raven hair & flower pin
    c.fillCircle(32, 13, 9, hairEbony, 0.95f, 0.50f);
    c.fillRect(22, 14, 4, 16, hairEbony, 0.85f);
    c.fillRect(38, 14, 5, 18, hairEbony, 0.85f);
    c.fillCircle(24, 12, 3, RGBA8{248, 84, 112, 255}, 0.98f);
    c.setPixel(24, 12, goldJewel, 0.99f);

    // Gold arm bangles
    c.fillRect(20, 33, 4, 2, goldJewel, 0.92f);
    c.fillRect(40, 33, 4, 2, goldJewel, 0.92f);

    finalizeSpriteBevelAndOutline(c);
}

void generateSnowFrostTile(PixelCanvas& c, uint32_t seed) {
    const int s = static_cast<int>(c.width());
    RGBA8 snowBase{228, 238, 248, 255};      // Soft alpine shadow blue-white
    RGBA8 snowHighlight{250, 253, 255, 255}; // Direct sunlit crystalline crust
    RGBA8 snowIceGlint{255, 255, 255, 255};   // Glinting ice faceted quartz sparkle

    for (int y = 0; y < s; ++y) {
        for (int x = 0; x < s; ++x) {
            float n0 = PixelCanvas::fbm2D(x * 0.14f, y * 0.14f, 3, seed);
            float drift = std::sin((x * 0.35f - y * 0.18f) + n0 * 3.6f) * 0.5f + 0.5f;
            float dither = PixelCanvas::bayer4x4(x, y) * 0.08f;
            float val = std::clamp(n0 * 0.6f + drift * 0.4f + dither, 0.0f, 1.0f);

            RGBA8 col = PixelCanvas::lerpColor(snowBase, snowHighlight, val);
            float h = 0.55f + val * 0.35f;
            uint8_t emiss = 0;

            float spark = PixelCanvas::hash2D(x, y, seed + 919u);
            if (spark > 0.968f) {
                col = snowIceGlint;
                h = 0.95f;
                emiss = 40;
            }
            c.setPixel(x, y, col, h, emiss);
        }
    }
}

void generateDesertDuneTile(PixelCanvas& c, uint32_t seed) {
    const int s = static_cast<int>(c.width());
    RGBA8 duneShadow{214, 152, 88, 255};    // Warm amber desert shadow
    RGBA8 duneMid{238, 186, 116, 255};     // Sun-warmed golden sand
    RGBA8 duneCrest{252, 218, 154, 255};   // Sun-bleached dune crest
    RGBA8 quartzGrain{255, 245, 220, 255};

    for (int y = 0; y < s; ++y) {
        for (int x = 0; x < s; ++x) {
            float n = PixelCanvas::fbm2D(x * 0.10f, y * 0.10f, 3, seed);
            float ripple = std::sin((x * 0.48f + y * 0.22f) + n * 3.8f);
            float rippleShape = (ripple > 0.0f) ? std::pow(ripple, 0.7f) : -std::pow(-ripple, 1.3f);
            float t = rippleShape * 0.5f + 0.5f;

            RGBA8 col = (t < 0.45f) ? PixelCanvas::lerpColor(duneShadow, duneMid, t / 0.45f)
                                    : PixelCanvas::lerpColor(duneMid, duneCrest, (t - 0.45f) / 0.55f);
            float h = 0.40f + t * 0.45f;
            uint8_t emiss = 0;

            float spark = PixelCanvas::hash2D(x, y, seed + 721u);
            if (spark > 0.97f) {
                col = quartzGrain;
                h = 0.92f;
                emiss = 25;
            }
            c.setPixel(x, y, col, h, emiss);
        }
    }
}

void generateSwampBogTile(PixelCanvas& c, uint32_t seed) {
    const int s = static_cast<int>(c.width());
    RGBA8 deepPeat{34, 40, 26, 255};      // Dark wet peat mud
    RGBA8 mossyMud{58, 70, 38, 255};      // Murky swamp moss
    RGBA8 stagnantWater{24, 42, 34, 255};  // Puddle sheen
    RGBA8 algaeGreen{78, 102, 44, 255};   // Brackish algae scum

    for (int y = 0; y < s; ++y) {
        for (int x = 0; x < s; ++x) {
            float n0 = PixelCanvas::fbm2D(x * 0.12f, y * 0.12f, 3, seed);
            float n1 = PixelCanvas::fbm2D(x * 0.25f, y * 0.25f, 2, seed + 333u);

            RGBA8 col = PixelCanvas::lerpColor(deepPeat, mossyMud, n0);
            float h = 0.30f + n0 * 0.35f;

            if (n1 > 0.65f) {
                col = PixelCanvas::lerpColor(col, algaeGreen, (n1 - 0.65f) / 0.35f);
                h += 0.10f;
            } else if (n0 < 0.25f) {
                col = stagnantWater;
                h = 0.15f;
            }
            c.setPixel(x, y, col, h, 0);
        }
    }
}

void generateScorchedAshTile(PixelCanvas& c, uint32_t seed) {
    const int s = static_cast<int>(c.width());
    RGBA8 basaltDark{26, 26, 30, 255};
    RGBA8 ashGrey{62, 60, 66, 255};
    RGBA8 charcoal{42, 40, 44, 255};
    RGBA8 glowingEmber{255, 98, 28, 255};

    for (int y = 0; y < s; ++y) {
        for (int x = 0; x < s; ++x) {
            float n0 = PixelCanvas::fbm2D(x * 0.15f, y * 0.15f, 3, seed);
            float n1 = PixelCanvas::fbm2D(x * 0.30f, y * 0.30f, 2, seed + 808u);

            RGBA8 col = PixelCanvas::lerpColor(basaltDark, ashGrey, n0);
            float h = 0.40f + n0 * 0.35f;
            uint8_t emiss = 0;

            float fissure = std::abs(n1 - 0.50f);
            if (fissure < 0.045f) {
                float intensity = 1.0f - fissure / 0.045f;
                col = PixelCanvas::lerpColor(charcoal, glowingEmber, intensity);
                emiss = static_cast<uint8_t>(intensity * 220.0f);
                h = 0.20f;
            }
            c.setPixel(x, y, col, h, emiss);
        }
    }
}

void generateCactusSprite(PixelCanvas& c, uint32_t seed) {
    c.clear(RGBA8{0, 0, 0, 0}, 0.0f, 0);
    RGBA8 cactusDark{42, 92, 52, 255};
    RGBA8 cactusMid{64, 134, 76, 255};
    RGBA8 cactusLight{96, 178, 108, 255};
    RGBA8 spineCol{244, 238, 172, 255};

    for (int y = 14; y <= 60; ++y) {
        for (int x = 28; x <= 36; ++x) {
            float rx = static_cast<float>(x - 32) / 4.0f;
            RGBA8 col = (rx < -0.2f) ? cactusLight : ((rx > 0.4f) ? cactusDark : cactusMid);
            c.setPixel(x, y, col, 0.85f - std::abs(rx) * 0.3f);
        }
    }
    c.fillCircle(32, 14, 4, cactusMid, 0.90f, 0.50f);

    c.fillRect(20, 38, 9, 5, cactusMid, 0.80f);
    c.fillRect(18, 24, 5, 18, cactusMid, 0.82f);
    c.fillCircle(20, 24, 2, cactusLight, 0.88f, 0.50f);

    c.fillRect(35, 30, 9, 5, cactusDark, 0.78f);
    c.fillRect(41, 18, 5, 16, cactusDark, 0.80f);
    c.fillCircle(43, 18, 2, cactusMid, 0.85f, 0.50f);

    for (int y = 16; y <= 58; y += 4) {
        c.setPixel(27, y, spineCol, 0.95f);
        c.setPixel(37, y, spineCol, 0.95f);
        if (y >= 26 && y <= 38) c.setPixel(17, y, spineCol, 0.95f);
        if (y >= 20 && y <= 32) c.setPixel(46, y, spineCol, 0.95f);
    }
    finalizeSpriteBevelAndOutline(c, RGBA8{18, 38, 22, 255});
}

void generateDeadShrubSprite(PixelCanvas& c, uint32_t seed) {
    c.clear(RGBA8{0, 0, 0, 0}, 0.0f, 0);
    RGBA8 woodDark{78, 56, 38, 255};
    RGBA8 woodMid{118, 88, 62, 255};
    RGBA8 woodLight{164, 130, 96, 255};

    struct BranchDef { int x0, y0, x1, y1, x2, y2; };
    const BranchDef branches[8] = {
        {32, 58, 26, 48, 16, 42},
        {32, 58, 24, 44, 20, 32},
        {32, 58, 30, 42, 28, 28},
        {32, 58, 34, 44, 38, 30},
        {32, 58, 38, 48, 46, 38},
        {32, 58, 40, 52, 48, 46},
        {26, 48, 22, 40, 14, 36},
        {38, 48, 42, 40, 46, 28},
    };

    for (const auto& b : branches) {
        for (int i = 0; i <= 20; ++i) {
            float t = static_cast<float>(i) / 20.0f;
            float omt = 1.0f - t;
            float px = omt * omt * b.x0 + 2.0f * omt * t * b.x1 + t * t * b.x2;
            float py = omt * omt * b.y0 + 2.0f * omt * t * b.y1 + t * t * b.y2;
            RGBA8 col = (t > 0.6f) ? woodLight : ((t > 0.3f) ? woodMid : woodDark);
            c.setPixel(static_cast<int>(px), static_cast<int>(py), col, 0.75f - t * 0.3f);
            if (t < 0.4f) {
                c.setPixel(static_cast<int>(px) + 1, static_cast<int>(py), woodDark, 0.70f);
            }
        }
    }
    finalizeSpriteBevelAndOutline(c, RGBA8{32, 22, 16, 255});
}

void generateSnowPineSprite(PixelCanvas& c, uint32_t seed) {
    generatePineTreeSprite(c, seed);

    RGBA8 snowCrust{250, 253, 255, 255};
    RGBA8 snowShadow{214, 228, 246, 255};

    struct Tier { int cy; int halfW; int h; };
    const Tier tiers[4] = { {42, 23, 14}, {31, 19, 13}, {21, 14, 12}, {12, 9, 10} };

    for (int t = 0; t < 4; ++t) {
        const auto& tr = tiers[t];
        for (int dy = -tr.h; dy <= -tr.h + 5; ++dy) {
            float progress = static_cast<float>(dy + tr.h) / 5.0f;
            int span = static_cast<int>(tr.halfW * (progress * 0.6f + 0.4f));
            for (int dx = -span; dx <= span; ++dx) {
                int px = 32 + dx;
                int py = tr.cy + dy;
                if (c.getPixelWrapped(px, py).a == 0) continue;

                float n = PixelCanvas::hash2D(px, py, seed + t * 47u);
                if (std::abs(dx) > span - 1 && n < 0.3f) continue;

                RGBA8 col = (dy == -tr.h + 5 || (dx > span / 2)) ? snowShadow : snowCrust;
                c.setPixel(px, py, col, 0.95f);
            }
        }
    }
    finalizeSpriteBevelAndOutline(c, RGBA8{18, 30, 42, 255});
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
        generateGrassTile(c, {30, 48, 24, 255}, {52, 78, 36, 255}, {82, 118, 50, 255}, {206, 192, 84, 255}, 101u, true);
        writeLayer(TexLayer::GrassMeadow, c, 2.8f, true);

        generateGrassTile(c, {40, 60, 28, 255}, {66, 92, 40, 255}, {96, 130, 56, 255}, {198, 176, 76, 255}, 202u, false);
        writeLayer(TexLayer::GrassHighland, c, 2.8f, true);

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

        generateSandShoreTile(c, 1313u);
        writeLayer(TexLayer::SandShore, c, 2.6f, true);
    }

    // 2. 2.5D Billboard Sprites & Custom Props (14..23)
    {
        PixelCanvas c;
        generateCharacterSprite(c, {186, 44, 48, 255}, {46, 72, 118, 255}, {232, 192, 96, 255}, {218, 174, 72, 255}, true, false);
        writeLayer(TexLayer::SpriteHero, c, 3.2f, false);

        generateCharacterSprite(c, {58, 112, 68, 255}, {216, 198, 168, 255}, {114, 68, 44, 255}, {142, 94, 54, 255}, false, false);
        writeLayer(TexLayer::SpriteVillager, c, 3.2f, false);

        generateCharacterSprite(c, {118, 56, 132, 255}, {196, 138, 64, 255}, {62, 48, 44, 255}, {228, 184, 78, 255}, false, true);
        writeLayer(TexLayer::SpriteMerchant, c, 3.2f, false);

        generateCharacterSprite(c, {44, 84, 156, 255}, {158, 168, 182, 255}, {188, 196, 208, 255}, {214, 172, 74, 255}, true, false);
        writeLayer(TexLayer::SpriteGuard, c, 3.2f, false);

        generateSlimeMonsterSprite(c);
        writeLayer(TexLayer::SpriteSlime, c, 3.5f, false);

        generateWolfMonsterSprite(c);
        writeLayer(TexLayer::SpriteWolf, c, 3.4f, false);

        generatePineFoliageTile(c, 1414u);
        writeLayer(TexLayer::PineFoliage, c, 2.5f, true);

        generateOakTreeSprite(c, 1515u);
        writeLayer(TexLayer::SpriteOakTree, c, 3.2f, false);

        generateWildflowerSprite(c, 1616u);
        writeLayer(TexLayer::SpriteWildflower, c, 2.5f, false);

        generateTorchFlameSprite(c);
        writeLayer(TexLayer::SpriteTorchFlame, c, 2.8f, false);
    }

    // 3. Coastal, Tropical Palm & Expanded Party Layers (24..31)
    {
        PixelCanvas c;
        generatePalmFrondsTile(c, 1717u);
        writeLayer(TexLayer::PalmFronds, c, 3.2f, true);

        generatePalmBarkTile(c, 1818u);
        writeLayer(TexLayer::PalmBark, c, 3.5f, true);

        generateCoastalCliffStoneTile(c, 1919u);
        writeLayer(TexLayer::CoastalCliff, c, 4.4f, true);

        generateCoastalScreeTile(c, 2020u);
        writeLayer(TexLayer::CoastalScree, c, 3.6f, true);

        generatePalmTreeSprite(c, 2121u);
        writeLayer(TexLayer::SpritePalmTree, c, 3.2f, false);

        generateClericSprite(c);
        writeLayer(TexLayer::SpriteCleric, c, 3.2f, false);

        generateScholarSprite(c);
        writeLayer(TexLayer::SpriteScholar, c, 3.2f, false);

        generateDancerSprite(c);
        writeLayer(TexLayer::SpriteDancer, c, 3.2f, false);
    }

    // 4. Lush HD-2D Meadow Grass & Wildflower Flora (32..37)
    {
        PixelCanvas c;
        generateGrassTuftSprite(c, 2222u);
        writeLayer(TexLayer::SpriteGrassTuft, c, 3.0f, false);

        generateTallGrassSprite(c, 2323u);
        writeLayer(TexLayer::SpriteTallGrass, c, 3.0f, false);

        generateWildflowerBouquetSprite(c, 0, 2424u);
        writeLayer(TexLayer::SpriteWildflowerRed, c, 3.2f, false);

        generateWildflowerBouquetSprite(c, 1, 2525u);
        writeLayer(TexLayer::SpriteWildflowerBlue, c, 3.2f, false);

        generateWildflowerBouquetSprite(c, 2, 2626u);
        writeLayer(TexLayer::SpriteWildflowerYellow, c, 3.2f, false);

        generateCloverPatchSprite(c, 2727u);
        writeLayer(TexLayer::SpriteCloverPatch, c, 2.8f, false);
    }

    // 5. Biome Material & Flora Billboard Layers (38..44)
    {
        PixelCanvas c;
        generateSnowFrostTile(c, 2828u);
        writeLayer(TexLayer::SnowFrost, c, 3.2f, true);

        generateDesertDuneTile(c, 2929u);
        writeLayer(TexLayer::DesertDune, c, 3.0f, true);

        generateSwampBogTile(c, 3030u);
        writeLayer(TexLayer::SwampBog, c, 2.8f, true);

        generateScorchedAshTile(c, 3131u);
        writeLayer(TexLayer::ScorchedAsh, c, 3.5f, true);

        generateCactusSprite(c, 3232u);
        writeLayer(TexLayer::SpriteCactus, c, 3.0f, false);

        generateDeadShrubSprite(c, 3333u);
        writeLayer(TexLayer::SpriteDeadShrub, c, 2.8f, false);

        generateSnowPineSprite(c, 3434u);
        writeLayer(TexLayer::SpriteSnowPine, c, 3.2f, false);

        generateOakFoliageTile(c, 3535u);
        writeLayer(TexLayer::OakFoliage, c, 2.8f, true);

        generateSnowPineFoliageTile(c, 3636u);
        writeLayer(TexLayer::SnowPineFoliage, c, 2.6f, true);

        generateAutumnFoliageTile(c, 3737u);
        writeLayer(TexLayer::AutumnFoliage, c, 2.8f, true);
    }

    const auto t1 = std::chrono::high_resolution_clock::now();
    atlas.generationTimeMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
    return atlas;
}

} // namespace eatsrealms
