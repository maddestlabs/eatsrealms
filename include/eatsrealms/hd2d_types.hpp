#ifndef EATSREALMS_HD2D_TYPES_HPP
#define EATSREALMS_HD2D_TYPES_HPP

#include <cstdint>
#include <cmath>
#include <algorithm>

namespace eatsrealms {

struct Vec2f {
    float x{0.0f};
    float y{0.0f};
};

struct Vec3f {
    float x{0.0f};
    float y{0.0f};
    float z{0.0f};
};

struct Vec4f {
    float x{0.0f};
    float y{0.0f};
    float z{0.0f};
    float w{0.0f};
};

struct RGBA8 {
    uint8_t r{0};
    uint8_t g{0};
    uint8_t b{0};
    uint8_t a{255};

    [[nodiscard]] constexpr uint32_t pack() const noexcept {
        return static_cast<uint32_t>(r) |
               (static_cast<uint32_t>(g) << 8u) |
               (static_cast<uint32_t>(b) << 16u) |
               (static_cast<uint32_t>(a) << 24u);
    }

    static constexpr RGBA8 unpack(uint32_t v) noexcept {
        return RGBA8{
            static_cast<uint8_t>(v & 0xFFu),
            static_cast<uint8_t>((v >> 8u) & 0xFFu),
            static_cast<uint8_t>((v >> 16u) & 0xFFu),
            static_cast<uint8_t>((v >> 24u) & 0xFFu)
        };
    }
};

/**
 * Procedural Texture2DArray Atlas Layer IDs (64x64 pixel-art tiles & sprite sheets).
 * Each layer has a matching Albedo+Alpha slice and Normal+Emissive slice.
 */
namespace TexLayer {
    constexpr uint16_t GrassMeadow      = 0;
    constexpr uint16_t GrassHighland    = 1;
    constexpr uint16_t CliffStone       = 2;
    constexpr uint16_t Cobblestone      = 3;
    constexpr uint16_t DirtPath         = 4;
    constexpr uint16_t WoodPlanks       = 5;
    constexpr uint16_t TimberWall       = 6;
    constexpr uint16_t StuccoPlaster    = 7;
    constexpr uint16_t TerracottaRoof   = 8;
    constexpr uint16_t SlateRoof        = 9;
    constexpr uint16_t StoneBrick       = 10;
    constexpr uint16_t WindowWarmGlow   = 11;
    constexpr uint16_t WaterSurface     = 12;
    constexpr uint16_t SandShore        = 13;

    // 2.5D Billboard Sprite Sheets (Alpha-tested & Normal-mapped)
    constexpr uint16_t SpriteHero       = 14;
    constexpr uint16_t SpriteVillager   = 15;
    constexpr uint16_t SpriteMerchant   = 16;
    constexpr uint16_t SpriteGuard      = 17;
    constexpr uint16_t SpriteSlime      = 18;
    constexpr uint16_t SpriteWolf       = 19;
    constexpr uint16_t SpritePineTree   = 20;
    constexpr uint16_t PineFoliage      = 20; // 3D Procedural Conifer Pine Foliage Skirts
    constexpr uint16_t SpriteOakTree    = 21;
    constexpr uint16_t SpriteWildflower = 22;
    constexpr uint16_t SpriteTorchFlame = 23;

    // Coastal & Tropical Additions (Matching Octopath coastal diorama aesthetic)
    constexpr uint16_t PalmFronds       = 24; // 3D Procedural Tropical Palm Leaf Fronds
    constexpr uint16_t PalmBark         = 25; // 3D Palm Trunk Ringed Fibrous Bark
    constexpr uint16_t CoastalCliff     = 26; // Stratified Sedimentary Limestone Rock
    constexpr uint16_t CoastalScree     = 27; // Rocky Plateau Scree & Wild Sprouts
    constexpr uint16_t SpritePalmTree   = 28; // 2.5D Palm Tree Billboard Sprite
    constexpr uint16_t SpriteCleric     = 29; // White mantle cleric party member
    constexpr uint16_t SpriteScholar    = 30; // Scholar robes party member
    constexpr uint16_t SpriteDancer     = 31; // Dancer costume party member

    // Lush HD-2D Meadow Grass & Wildflower Flora Objects (Matching Lumina / Octopath core)
    constexpr uint16_t SpriteGrassTuft        = 32; // Multi-blade dense green meadow grass clump
    constexpr uint16_t SpriteTallGrass        = 33; // Tall wild meadow grass & reed stalks
    constexpr uint16_t SpriteWildflowerRed    = 34; // Vibrant crimson poppy / scarlet anemone cluster
    constexpr uint16_t SpriteWildflowerBlue   = 35; // Cerulean bluebell / forget-me-not blossom bouquet
    constexpr uint16_t SpriteWildflowerYellow = 36; // Sunny golden buttercup & dandelion cluster
    constexpr uint16_t SpriteCloverPatch      = 37; // Lush three-leaf clover & daisy rosette

    // Biome Terrain Additions (Snow, Desert Sand Dunes, Swamp Mud, Scorched Earth)
    constexpr uint16_t SnowFrost              = 38; // Crisp crystalline snow & ice crust
    constexpr uint16_t DesertDune             = 39; // Sun-bleached wind-rippled desert sand dunes
    constexpr uint16_t SwampBog               = 40; // Dark murky peat, moss & wetland mud
    constexpr uint16_t ScorchedAsh            = 41; // Volcanic basalt & burnt ash ground

    // Biome Flora Billboard Sprites
    constexpr uint16_t SpriteCactus           = 42; // Desert Saguaro cactus
    constexpr uint16_t SpriteDeadShrub        = 43; // Arid tumbleweed / dry scrub
    constexpr uint16_t SpriteSnowPine         = 44; // Snow-laden conifer pine tree

    // 3D Procedural Foliage Canopies
    constexpr uint16_t OakFoliage             = 45; // 3D Procedural Deciduous / Oak Leafy Canopy
    constexpr uint16_t SnowPineFoliage        = 46; // 3D Snow-Draped Conifer Pine Foliage
    constexpr uint16_t AutumnFoliage          = 47; // 3D Golden / Amber Autumn Foliage

    constexpr uint32_t kCount                 = 48;
    constexpr uint32_t kTileSize              = 64;
} // namespace TexLayer

/**
 * 3D World & Architectural Prop Vertex (48 bytes, 16-byte aligned for WGSL storage buffer pulling).
 */
struct alignas(16) WorldVertex {
    float pos[3]{0.0f, 0.0f, 0.0f};
    float ao{1.0f};
    float normal[3]{0.0f, 1.0f, 0.0f};
    float roughness{0.75f};
    float tangent[3]{1.0f, 0.0f, 0.0f};
    uint32_t texLayer{0};
    float uv[2]{0.0f, 0.0f};
    uint32_t flags{0}; // bit 0 = animated water, bit 1 = emissive window
    float pad{0.0f};
};
static_assert(sizeof(WorldVertex) == 64, "WorldVertex must be 64 bytes (4x vec4) for WGSL storage alignment");

/**
 * 2.5D Billboard Sprite Instance (32 bytes, 16-byte aligned for WGSL instanced rendering).
 */
namespace SpriteFlags {
    constexpr uint32_t None        = 0u;
    constexpr uint32_t FlipX       = 1u << 0u;
    constexpr uint32_t WindSway    = 1u << 1u; // Trees & wildflowers sway in the wind
    constexpr uint32_t IdleBob     = 1u << 2u; // NPCs, heroes & monsters breathe/bob subtly
    constexpr uint32_t Emissive    = 1u << 3u; // Torch flames / glowing magic sprites
} // namespace SpriteFlags

struct alignas(16) Sprite3DInstance {
    float worldPos[3]{0.0f, 0.0f, 0.0f}; // Ground foot anchor in 3D world space
    float width{1.6f};
    float height{2.2f};
    uint32_t texLayer{TexLayer::SpriteHero};
    uint32_t flags{SpriteFlags::IdleBob};
    float phaseOffset{0.0f};
};
static_assert(sizeof(Sprite3DInstance) == 32, "Sprite3DInstance must be 32 bytes (2x vec4) for WGSL alignment");

/**
 * Dynamic Point Light (32 bytes, 16-byte aligned for WGSL uniform/storage light pool).
 */
struct alignas(16) PointLight {
    float position[3]{0.0f, 0.0f, 0.0f};
    float radius{14.0f};
    float color[3]{1.0f, 0.65f, 0.25f};
    float intensity{1.5f};
};
static_assert(sizeof(PointLight) == 32, "PointLight must be 32 bytes (2x vec4) for WGSL alignment");

struct CameraConfig {
    Vec3f position{0.0f, 26.0f, -38.0f};
    Vec3f target{0.0f, 6.0f, 4.0f};
    float fovDegrees{30.0f};
    float focusDistance{44.0f};
    float focusRange{18.0f};
    float bokehRadius{7.5f};
};

struct AtmosphereConfig {
    Vec3f sunDirection{-0.58f, 0.46f, -0.67f};
    Vec3f sunColor{1.55f, 1.08f, 0.72f};
    Vec3f skyZenith{0.12f, 0.09f, 0.22f};
    Vec3f skyHorizon{0.82f, 0.44f, 0.36f};
    float fogDensity{0.0080f};
    float fogBaseHeight{1.8f};     // Base elevation where low-lying valley mist pools
    float fogHeightFalloff{0.055f}; // Vertical exponential decay factor with altitude
    float exposure{1.02f};
    float timeOfDay{0.38f}; // 0.0 to 1.0 (e.g. 0.38 = ~09:07 morning)

    [[nodiscard]] static AtmosphereConfig evaluateDayNight(float timeSecFraction) noexcept {
        float t = timeSecFraction - std::floor(timeSecFraction);
        if (t < 0.0f) t += 1.0f;

        // Keyframe timeline across 24 hours:
        // 0.00 = Midnight (00:00)
        // 0.22 = Early Dawn (05:15)
        // 0.28 = Sunrise (06:45)
        // 0.38 = Morning Woodland (09:07, Octopath reference golden angle)
        // 0.50 = Noon (12:00)
        // 0.72 = Golden Hour (17:15)
        // 0.80 = Dusk / Sunset (19:15)
        // 0.88 = Blue Hour / Nightfall (21:00)
        struct TimeKey {
            float time;
            Vec3f sunDir;
            Vec3f sunCol;
            Vec3f zenith;
            Vec3f horizon;
            float fog;
            float exp;
        };

        const TimeKey keys[] = {
            { 0.00f, { 0.42f, 0.78f,  0.35f}, {0.28f, 0.36f, 0.60f}, {0.02f, 0.03f, 0.08f}, {0.05f, 0.07f, 0.14f}, 0.0090f, 0.94f },
            { 0.20f, { 0.55f, 0.42f,  0.55f}, {0.45f, 0.42f, 0.65f}, {0.04f, 0.04f, 0.12f}, {0.14f, 0.10f, 0.22f}, 0.0085f, 0.96f },
            { 0.25f, {-0.78f, 0.14f, -0.60f}, {1.75f, 1.05f, 0.65f}, {0.10f, 0.10f, 0.24f}, {0.85f, 0.42f, 0.35f}, 0.0082f, 1.00f },
            { 0.30f, {-0.72f, 0.32f, -0.62f}, {1.82f, 1.30f, 0.85f}, {0.14f, 0.18f, 0.36f}, {0.90f, 0.60f, 0.42f}, 0.0080f, 1.02f },
            { 0.38f, {-0.62f, 0.55f, -0.56f}, {1.68f, 1.48f, 1.15f}, {0.16f, 0.28f, 0.55f}, {0.76f, 0.76f, 0.72f}, 0.0078f, 1.02f },
            { 0.50f, {-0.18f, 0.92f, -0.32f}, {1.55f, 1.50f, 1.42f}, {0.12f, 0.35f, 0.75f}, {0.68f, 0.78f, 0.88f}, 0.0075f, 1.00f },
            { 0.72f, {-0.65f, 0.38f, -0.65f}, {1.95f, 1.42f, 0.82f}, {0.16f, 0.12f, 0.32f}, {0.95f, 0.52f, 0.26f}, 0.0082f, 1.04f },
            { 0.80f, {-0.76f, 0.14f, -0.62f}, {1.45f, 0.65f, 0.36f}, {0.08f, 0.06f, 0.18f}, {0.84f, 0.34f, 0.32f}, 0.0086f, 0.98f },
            { 0.88f, { 0.35f, 0.52f,  0.38f}, {0.35f, 0.44f, 0.68f}, {0.03f, 0.04f, 0.10f}, {0.08f, 0.10f, 0.20f}, 0.0090f, 0.94f },
            { 1.00f, { 0.42f, 0.78f,  0.35f}, {0.28f, 0.36f, 0.60f}, {0.02f, 0.03f, 0.08f}, {0.05f, 0.07f, 0.14f}, 0.0090f, 0.94f }
        };

        constexpr size_t numKeys = sizeof(keys) / sizeof(keys[0]);
        size_t idx = 0;
        for (size_t i = 0; i < numKeys - 1; ++i) {
            if (t >= keys[i].time && t <= keys[i + 1].time) {
                idx = i;
                break;
            }
        }

        const auto& k0 = keys[idx];
        const auto& k1 = keys[idx + 1];
        float span = k1.time - k0.time;
        float factor = (span > 1e-5f) ? (t - k0.time) / span : 0.0f;
        // Smooth Hermite blend
        float s = factor * factor * (3.0f - 2.0f * factor);

        auto lerp3 = [](Vec3f a, Vec3f b, float w) -> Vec3f {
            return { a.x + (b.x - a.x) * w, a.y + (b.y - a.y) * w, a.z + (b.z - a.z) * w };
        };

        AtmosphereConfig cfg{};
        cfg.sunDirection = lerp3(k0.sunDir, k1.sunDir, s);
        float sLen = std::sqrt(cfg.sunDirection.x * cfg.sunDirection.x +
                               cfg.sunDirection.y * cfg.sunDirection.y +
                               cfg.sunDirection.z * cfg.sunDirection.z);
        if (sLen > 1e-5f) {
            cfg.sunDirection.x /= sLen;
            cfg.sunDirection.y /= sLen;
            cfg.sunDirection.z /= sLen;
        }
        cfg.sunColor   = lerp3(k0.sunCol, k1.sunCol, s);
        cfg.skyZenith  = lerp3(k0.zenith, k1.zenith, s);
        cfg.skyHorizon = lerp3(k0.horizon, k1.horizon, s);
        cfg.fogDensity = k0.fog + (k1.fog - k0.fog) * s;
        cfg.exposure   = k0.exp + (k1.exp - k0.exp) * s;
        cfg.timeOfDay  = t;
        return cfg;
    }

    [[nodiscard]] static const char* timePeriodName(float t) noexcept {
        float frac = t - std::floor(t);
        if (frac < 0.0f) frac += 1.0f;
        if (frac < 0.20f) return "Night";
        if (frac < 0.26f) return "Dawn";
        if (frac < 0.34f) return "Sunrise";
        if (frac < 0.46f) return "Morning";
        if (frac < 0.62f) return "Noon";
        if (frac < 0.76f) return "Golden Hour";
        if (frac < 0.84f) return "Sunset";
        if (frac < 0.92f) return "Dusk";
        return "Night";
    }
};

struct RaycastHit {
    bool hit{false};
    float distance{0.0f};
    Vec3f position{};
    uint16_t layerId{0};
};

struct RealmStats {
    uint32_t terrainTriangles{0};
    uint32_t propTriangles{0};
    uint32_t spriteCount{0};
    uint32_t pointLightCount{0};
    double atlasGenTimeMs{0.0};
    uint32_t activeChunks{0};
    uint32_t lod0Triangles{0};
    uint32_t lod1Triangles{0};
    uint32_t lod2Triangles{0};
    uint32_t lod3Triangles{0};
    uint32_t culledSprites{0};
};

} // namespace eatsrealms

#endif // EATSREALMS_HD2D_TYPES_HPP
