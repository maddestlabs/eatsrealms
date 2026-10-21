#ifndef EATSREALMS_REALM_WORLD_HPP
#define EATSREALMS_REALM_WORLD_HPP

#include "eatsrealms/hd2d_types.hpp"
#include "eatsrealms/pixel_canvas.hpp"
#include <vector>
#include <cstdint>

#include "eatsrealms/tile_cell.hpp"
#include "eatsrealms/realm_chunk.hpp"
#include <unordered_map>
#include <memory>

namespace eatsrealms {

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
    void setTileSlope(int gx, int gz, SlopeDir dir, int16_t heightDelta = 2, bool isStairs = false);
    void setTileCorners(int gx, int gz, int16_t nw, int16_t ne, int16_t se, int16_t sw, uint16_t topLayer, uint16_t cliffLayer = TexLayer::CliffStone);
    void fillPlateau(int gx0, int gz0, int gx1, int gz1, int16_t elevation, uint16_t topLayer, uint16_t cliffLayer = TexLayer::CliffStone);
    void addSlope(int gx0, int gz0, int gx1, int gz1, int16_t startElev, int16_t endElev, SlopeDir dir, uint16_t topLayer = TexLayer::DirtPath, uint16_t cliffLayer = TexLayer::CliffStone, bool isStairs = false);
    void addTerracedSlope(int gx0, int gz0, int gx1, int gz1, int16_t lowElev, int16_t highElev, SlopeDir dir, uint16_t pathLayer = TexLayer::Cobblestone);
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
    void addSuspensionBridge(Vec3f startPos, Vec3f endPos, float width = 2.2f, float mastHeight = 4.0f);
    void addStreetLantern(Vec3f basePos);
    void addRoadsideBeacon(Vec3f pos, Vec3f glowColor = {0.25f, 1.85f, 0.65f});
    void addStoneArchway(Vec3f basePos, float width = 4.6f, float height = 5.2f, int axis = 0);
    void addCrateStack(Vec3f pos, int count = 3);
    void addBarrel(Vec3f pos);
    void addFlowerBox(Vec3f pos, float length = 1.6f, int axis = 0);
    void addMarketStall(Vec3f pos, float width = 3.8f, float depth = 2.6f, uint16_t canopyLayer = TexLayer::TerracottaRoof);
    void addPineTree3D(Vec3f basePos, float scale = 1.0f, float rotAngle = 0.0f);
    void addPineTree3DGrounded(float worldX, float worldZ, float scale = 1.0f, float rotAngle = 0.0f);
    void addOakTree3D(Vec3f basePos, float scale = 1.0f, float rotAngle = 0.0f, uint16_t foliageLayer = TexLayer::OakFoliage);
    void addOakTree3DGrounded(float worldX, float worldZ, float scale = 1.0f, float rotAngle = 0.0f, uint16_t foliageLayer = TexLayer::OakFoliage);
    void addSnowPineTree3D(Vec3f basePos, float scale = 1.0f, float rotAngle = 0.0f);
    void addSnowPineTree3DGrounded(float worldX, float worldZ, float scale = 1.0f, float rotAngle = 0.0f);
    void addAutumnTree3D(Vec3f basePos, float scale = 1.0f, float rotAngle = 0.0f);
    void addAutumnTree3DGrounded(float worldX, float worldZ, float scale = 1.0f, float rotAngle = 0.0f);
    void addDeadTree3D(Vec3f basePos, float scale = 1.0f, float rotAngle = 0.0f);
    void addDeadTree3DGrounded(float worldX, float worldZ, float scale = 1.0f, float rotAngle = 0.0f);
    void addMossBoulder3D(Vec3f pos, float radiusX = 1.0f, float radiusY = 0.65f, float radiusZ = 1.0f, float rotAngle = 0.0f);
    void addMossBoulder3DGrounded(float worldX, float worldZ, float radiusX = 1.0f, float radiusY = 0.65f, float radiusZ = 1.0f, float rotAngle = 0.0f);
    void addStarFern3D(Vec3f pos, float scale = 1.0f, float rotAngle = 0.0f);
    void addStarFern3DGrounded(float worldX, float worldZ, float scale = 1.0f, float rotAngle = 0.0f);
    void addRusticFence(Vec3f startPos, Vec3f endPos, int postCount = 3);
    void addRusticFenceGrounded(float x0, float z0, float x1, float z1, int postCount = 3);
    void addRiverPebbleCluster(Vec3f pos, float radius = 0.6f, int count = 5, uint32_t seed = 0);
    void addRiverPebbleClusterGrounded(float worldX, float worldZ, float radius = 0.6f, int count = 5, uint32_t seed = 0);
    void addGrassTuft3D(Vec3f pos, float scale = 0.85f, float phaseOffset = 0.0f);
    void addGrassTuft3DGrounded(float worldX, float worldZ, float scale = 0.85f, float phaseOffset = 0.0f);
    void addTallGrass3D(Vec3f pos, float scale = 1.0f, float phaseOffset = 0.0f);
    void addTallGrass3DGrounded(float worldX, float worldZ, float scale = 1.0f, float phaseOffset = 0.0f);
    void addWildflower3D(Vec3f pos, uint16_t flowerLayer = TexLayer::SpriteWildflowerRed, float scale = 1.0f, float phaseOffset = 0.0f);
    void addWildflower3DGrounded(float worldX, float worldZ, uint16_t flowerLayer = TexLayer::SpriteWildflowerRed, float scale = 1.0f, float phaseOffset = 0.0f);
    void addCloverPatch3D(Vec3f pos, float scale = 1.0f, float phaseOffset = 0.0f);
    void addCloverPatch3DGrounded(float worldX, float worldZ, float scale = 1.0f, float phaseOffset = 0.0f);

    // Lush Octopath / Lumina style procedural meadow grass & wildflower scatter
    void scatterMeadowVegetation(float density = 1.0f, float flowerRatio = 0.32f, uint32_t seed = 42u);

    // Coastal & Tropical Diorama Props (Matching reference screenshot)
    void addPalmTree3D(Vec3f basePos, float scale = 1.0f, float rotAngle = 0.0f, bool withStonePlanter = false);
    void addPalmTree3DGrounded(float worldX, float worldZ, float scale = 1.0f, float rotAngle = 0.0f, bool withStonePlanter = false);

    // Static Prop & Tree Geometry Emitters (shared between diorama props and open-world chunks)
    static void emitPineTree3D(std::vector<WorldVertex>& dst, Vec3f basePos, float scale = 1.0f, float rotAngle = 0.0f);
    static void emitOakTree3D(std::vector<WorldVertex>& dst, Vec3f basePos, float scale = 1.0f, float rotAngle = 0.0f, uint16_t foliageLayer = TexLayer::OakFoliage);
    static void emitSnowPineTree3D(std::vector<WorldVertex>& dst, Vec3f basePos, float scale = 1.0f, float rotAngle = 0.0f);
    static void emitAutumnTree3D(std::vector<WorldVertex>& dst, Vec3f basePos, float scale = 1.0f, float rotAngle = 0.0f);
    static void emitDeadTree3D(std::vector<WorldVertex>& dst, Vec3f basePos, float scale = 1.0f, float rotAngle = 0.0f);
    static void emitPalmTree3D(std::vector<WorldVertex>& dst, Vec3f basePos, float scale = 1.0f, float rotAngle = 0.0f, bool withStonePlanter = false);
    static void emitMossBoulder3D(std::vector<WorldVertex>& dst, Vec3f pos, float radiusX = 1.0f, float radiusY = 0.65f, float radiusZ = 1.0f, float rotAngle = 0.0f);
    static void emitStarFern3D(std::vector<WorldVertex>& dst, Vec3f pos, float scale = 1.0f, float rotAngle = 0.0f);
    void addTreasureChest(Vec3f pos, float rotAngle = 0.0f);
    void addTreasureChestGrounded(float worldX, float worldZ, float rotAngle = 0.0f);
    void addSeaStack3D(Vec3f basePos, float baseRadius = 2.4f, float height = 8.5f, uint32_t seed = 42u);
    void addCoastalBridge(Vec3f startPos, Vec3f endPos, float width = 2.8f);

    // Narrative Ruin & Calamity Props (Environmental Storytelling)
    void addRuinedHouse(Vec3f centerPos, Vec3f size, int roofAxis, float collapseRatio = 0.5f);
    void addBarricade(Vec3f pos, float width = 3.8f, float rotAngle = 0.0f);
    void addRunicAltar(Vec3f pos, float radius = 2.4f, bool activeGlow = true);
    void addBrokenBridge(Vec3f startPos, Vec3f endPos, float breakRatio = 0.4f, float width = 3.2f);

    // Day/Night Cycle Control (0.0 to 1.0, 0.0=Midnight, 0.25=Dawn, 0.38=Morning, 0.5=Noon, 0.72=Golden Hour, 0.8=Dusk)
    [[nodiscard]] float timeOfDay() const noexcept { return timeOfDay_; }
    void setTimeOfDay(float t) noexcept;
    [[nodiscard]] bool useDayNightCycle() const noexcept { return useDayNightCycle_; }
    void setUseDayNightCycle(bool use) noexcept { useDayNightCycle_ = use; }

    // Procedural multi-pass realm generator with configurable seed
    void generateProceduralRealm(uint32_t seed = 1337u);
    void buildWoodlandTrailRealm(uint32_t seed = 42u);
    void buildCoastalCoveRealm(uint32_t seed = 777u);
    void buildChronicleHamletRealm(uint32_t seed = 999u);
    void buildHighlandBridgeRealm(uint32_t seed = 42u);

    // 2.5D Shadow-Casting Sprite3D & PointLight Spawners
    void spawnSprite(Vec3f worldPos, float width, float height, uint16_t spriteLayer, uint32_t flags = SpriteFlags::IdleBob, float phaseOffset = 0.0f);
    void spawnSpriteGrounded(float worldX, float worldZ, float width, float height, uint16_t spriteLayer, uint32_t flags = SpriteFlags::IdleBob, float phaseOffset = 0.0f);
    void addPointLight(Vec3f position, Vec3f color, float radius = 14.0f, float intensity = 1.6f);

    // Rebuilds merged 3D terrain + architectural prop vertex buffer in 1 compact array
    RealmStats rebuildWorldGeometry();

    // Open-world Chunk Management & Clipmap Streaming (Eatsrealms Open)
    RealmChunk* getOrCreateChunk(int cx, int cz);
    [[nodiscard]] RealmChunk* getChunk(int cx, int cz) noexcept;
    [[nodiscard]] const RealmChunk* getChunk(int cx, int cz) const noexcept;
    [[nodiscard]] const std::unordered_map<ChunkCoord, std::unique_ptr<RealmChunk>, ChunkCoordHash>& chunks() const noexcept { return chunks_; }

    void updateStreamingClipmap(
        Vec3f cameraFocusPos,
        float lod0Radius = 70.0f,
        float lod1Radius = 200.0f,
        float lod2Radius = 600.0f,
        float lod3Radius = 2000.0f
    );

    void generateOpenWorld(uint32_t seed = 1337u, int chunkRadius = 6, float centerX = 0.0f, float centerZ = 0.0f);

    // Dynamic on-demand infinite procedural chunk streaming (No Man's Sky architecture)
    // Generates chunks deterministically as user travels, updates LODs, and unloads distant chunks
    bool streamChunksAround(
        Vec3f cameraPos,
        float viewDistance = 420.0f,
        float unloadDistance = 620.0f
    );

    [[nodiscard]] uint32_t lod0VertexCount() const noexcept { return lod0VertexCount_; }
    [[nodiscard]] uint32_t lod0SpriteCount() const noexcept { return lod0SpriteCount_; }
    [[nodiscard]] bool isOpenWorld() const noexcept { return !chunks_.empty(); }
    [[nodiscard]] uint32_t openWorldSeed() const noexcept { return openWorldSeed_; }

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
    static void emitQuad(
        std::vector<WorldVertex>& dst,
        Vec3f p0, Vec3f p1, Vec3f p2, Vec3f p3,
        Vec3f normal, Vec3f tangent,
        Vec2f uv0, Vec2f uv1, Vec2f uv2, Vec2f uv3,
        float ao0, float ao1, float ao2, float ao3,
        uint32_t texLayer, float roughness = 0.78f, uint32_t flags = 0u,
        float extra0 = 0.0f, float extra1 = 0.0f, float extra2 = 0.0f, float extra3 = 0.0f
    );

    static void emitTriangle(
        std::vector<WorldVertex>& dst,
        Vec3f p0, Vec3f p1, Vec3f p2,
        Vec3f normal, Vec3f tangent,
        Vec2f uv0, Vec2f uv1, Vec2f uv2,
        float ao, uint32_t texLayer, float roughness = 0.78f, uint32_t flags = 0u
    );

    static void emitBox(
        std::vector<WorldVertex>& dst,
        Vec3f minCorner, Vec3f maxCorner,
        uint32_t texLayer, float uvScale = 0.5f, float roughness = 0.78f, uint32_t flags = 0u
    );

    static void emitOrientedBeam(
        std::vector<WorldVertex>& dst,
        Vec3f p0, Vec3f p1,
        float thickness, float depth,
        uint32_t texLayer, float uvScale = 0.5f, float roughness = 0.75f, uint32_t flags = 0u
    );

    std::vector<TileCell> tiles_;
    std::vector<WorldVertex> propVertices_;
    std::vector<WorldVertex> worldVertices_;
    std::vector<Sprite3DInstance> sprites_;
    std::vector<PointLight> pointLights_;
    ProceduralAtlasData atlas_;
    CameraConfig camera_{};
    AtmosphereConfig atmosphere_{};
    float timeOfDay_{0.38f};
    bool useDayNightCycle_{true};
    uint32_t terrainTriCount_{0};
    uint32_t propTriCount_{0};
    std::unordered_map<ChunkCoord, std::unique_ptr<RealmChunk>, ChunkCoordHash> chunks_;
    uint32_t lod0VertexCount_{0};
    uint32_t lod0SpriteCount_{0};
    uint32_t openWorldSeed_{1337u};
};

} // namespace eatsrealms

#endif // EATSREALMS_REALM_WORLD_HPP
