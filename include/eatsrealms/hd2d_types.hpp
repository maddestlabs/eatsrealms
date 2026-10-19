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
    constexpr uint16_t SpriteOakTree    = 21;
    constexpr uint16_t SpriteWildflower = 22;
    constexpr uint16_t SpriteTorchFlame = 23;

    constexpr uint32_t kCount           = 24;
    constexpr uint32_t kTileSize        = 64;
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
    float fogDensity{0.0085f};
    float exposure{1.15f};
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
};

} // namespace eatsrealms

#endif // EATSREALMS_HD2D_TYPES_HPP
