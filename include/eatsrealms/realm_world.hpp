#ifndef EATSREALMS_REALM_WORLD_HPP
#define EATSREALMS_REALM_WORLD_HPP

#include "eatsrealms/hd2d_types.hpp"
#include "eatsrealms/pixel_canvas.hpp"
#include <vector>
#include <cstdint>

namespace eatsrealms {

enum class SlopeDir : uint8_t {
    None   = 0,
    PlusX  = 1,
    MinusX = 2,
    PlusZ  = 3,
    MinusZ = 4
};

struct TileCell {
    int16_t elevation{2}; // Height in 0.5-unit steps (e.g., 4 = 2.0 world Y)
    uint16_t topLayer{TexLayer::GrassMeadow};
    uint16_t cliffLayer{TexLayer::CliffStone};
    SlopeDir slope{SlopeDir::None};
    bool hasWater{false};
    float waterY{0.65f};
};

/**
 * Complete HD-2D Realm World containing:
 *  1. Multi-Height 3D Tile Terrain & Animated Water (`TileMap`)
 *  2. Procedural Low-Poly 3D Architectural Props (`PropFactory` - Gabled Houses, Bridges, Lanterns, Archways)
 *  3. 2.5D Shadow-Casting & Normal-Mapped Billboard Entities (`Sprite3DInstance` array)
 *  4. Dynamic Point-Light Pool (`PointLight` array, up to 64 lights)
 *  5. Procedural 24-Layer Pixel-Art & Sobel Normal Atlas (`ProceduralAtlasData`)
 */
class RealmWorld {
public:
    static constexpr int kGridWidth  = 64;
    static constexpr int kGridDepth  = 64;
    static constexpr float kTileSize = 1.5f;   // World units per tile X/Z
    static constexpr float kElevStep = 0.6f;   // World units per elevation step Y
    static constexpr size_t kMaxPointLights = 64;

    RealmWorld();

    void clear();

    // Builds the default "Starfall Vale" HD-2D RPG Village & Highland Ruins showcase scene
    void buildStarfallValeRealm();

    // TileMap Terrain API (Grid coordinates gx, gz in [0, 63])
    void setTile(int gx, int gz, int16_t elevation, uint16_t topLayer, uint16_t cliffLayer = TexLayer::CliffStone);
    void fillPlateau(int gx0, int gz0, int gx1, int gz1, int16_t elevation, uint16_t topLayer, uint16_t cliffLayer = TexLayer::CliffStone);
    void addStairs(int gx0, int gz0, int gx1, int gz1, int16_t lowElev, int16_t highElev, SlopeDir dir, uint16_t stairLayer = TexLayer::Cobblestone);
    void fillRiver(int gx0, int gz0, int gx1, int gz1, int16_t bedElev, float waterWorldY);

    [[nodiscard]] const TileCell& getTile(int gx, int gz) const noexcept;
    [[nodiscard]] float getTerrainHeightAt(float worldX, float worldZ) const noexcept;
    [[nodiscard]] Vec3f gridToWorld(int gx, int gz) const noexcept;

    // 3D Architectural Prop Factory API (World coordinates)
    void addGabledHouse(
        Vec3f centerPos,
        Vec3f size,
        int roofAxis,
        uint16_t wallLayer = TexLayer::TimberWall,
        uint16_t roofLayer = TexLayer::TerracottaRoof,
        bool hasChimney = true,
        bool hasLantern = true
    );

    void addWoodenBridge(Vec3f startPos, Vec3f endPos, float width = 3.2f);
    void addStreetLantern(Vec3f basePos);
    void addStoneArchway(Vec3f basePos, float width = 4.6f, float height = 5.2f, int axis = 0);

    // 2.5D Shadow-Casting Sprite3D & PointLight Spawners
    void spawnSprite(Vec3f worldPos, float width, float height, uint16_t spriteLayer, uint32_t flags = SpriteFlags::IdleBob, float phaseOffset = 0.0f);
    void spawnSpriteGrounded(float worldX, float worldZ, float width, float height, uint16_t spriteLayer, uint32_t flags = SpriteFlags::IdleBob, float phaseOffset = 0.0f);
    void addPointLight(Vec3f position, Vec3f color, float radius = 14.0f, float intensity = 1.6f);

    // Rebuilds merged 3D terrain + architectural prop vertex buffer in 1 compact array
    RealmStats rebuildWorldGeometry();

    // Raycast against terrain & props for camera auto-focus and interactive foley clicks
    [[nodiscard]] RaycastHit raycast(Vec3f origin, Vec3f dir, float maxDist = 250.0f) const noexcept;

    [[nodiscard]] const std::vector<WorldVertex>& worldVertices() const noexcept { return worldVertices_; }
    [[nodiscard]] const std::vector<Sprite3DInstance>& sprites() const noexcept { return sprites_; }
    [[nodiscard]] const std::vector<PointLight>& pointLights() const noexcept { return pointLights_; }
    [[nodiscard]] const ProceduralAtlasData& atlas() const noexcept { return atlas_; }

    [[nodiscard]] CameraConfig& camera() noexcept { return camera_; }
    [[nodiscard]] const CameraConfig& camera() const noexcept { return camera_; }
    [[nodiscard]] AtmosphereConfig& atmosphere() noexcept { return atmosphere_; }
    [[nodiscard]] const AtmosphereConfig& atmosphere() const noexcept { return atmosphere_; }

private:
    void emitQuad(
        std::vector<WorldVertex>& dst,
        Vec3f p0, Vec3f p1, Vec3f p2, Vec3f p3,
        Vec3f normal, Vec3f tangent,
        Vec2f uv0, Vec2f uv1, Vec2f uv2, Vec2f uv3,
        float ao0, float ao1, float ao2, float ao3,
        uint32_t texLayer, float roughness = 0.78f, uint32_t flags = 0u
    );

    void emitTriangle(
        std::vector<WorldVertex>& dst,
        Vec3f p0, Vec3f p1, Vec3f p2,
        Vec3f normal, Vec3f tangent,
        Vec2f uv0, Vec2f uv1, Vec2f uv2,
        float ao, uint32_t texLayer, float roughness = 0.78f, uint32_t flags = 0u
    );

    void emitBox(
        std::vector<WorldVertex>& dst,
        Vec3f minCorner, Vec3f maxCorner,
        uint32_t texLayer, float uvScale = 0.5f, float roughness = 0.78f, uint32_t flags = 0u
    );

    std::vector<TileCell> tiles_;
    std::vector<WorldVertex> propVertices_;
    std::vector<WorldVertex> worldVertices_;
    std::vector<Sprite3DInstance> sprites_;
    std::vector<PointLight> pointLights_;
    ProceduralAtlasData atlas_;
    CameraConfig camera_{};
    AtmosphereConfig atmosphere_{};
    uint32_t terrainTriCount_{0};
    uint32_t propTriCount_{0};
};

} // namespace eatsrealms

#endif // EATSREALMS_REALM_WORLD_HPP
