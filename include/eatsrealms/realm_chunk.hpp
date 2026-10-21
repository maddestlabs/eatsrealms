#ifndef EATSREALMS_REALM_CHUNK_HPP
#define EATSREALMS_REALM_CHUNK_HPP

#include "eatsrealms/hd2d_types.hpp"
#include "eatsrealms/tile_cell.hpp"
#include <array>
#include <vector>
#include <cstdint>
#include <cmath>
#include <algorithm>

namespace eatsrealms {

/**
 * 2D Integer Chunk Coordinate for open-world spatial hashing.
 * 1 Chunk = 32 x 32 tiles = 48.0m x 48.0m (each tile is 1.5m).
 */
struct ChunkCoord {
    int cx{0};
    int cz{0};

    [[nodiscard]] constexpr bool operator==(const ChunkCoord& o) const noexcept {
        return cx == o.cx && cz == o.cz;
    }

    [[nodiscard]] constexpr bool operator!=(const ChunkCoord& o) const noexcept {
        return !(*this == o);
    }
};

struct ChunkCoordHash {
    [[nodiscard]] size_t operator()(const ChunkCoord& c) const noexcept {
        // High-quality integer spatial hash
        uint32_t ux = static_cast<uint32_t>(c.cx);
        uint32_t uz = static_cast<uint32_t>(c.cz);
        return (static_cast<size_t>(ux * 0x1e35a7bd) ^ static_cast<size_t>(uz * 0x85157af5));
    }
};

enum class ChunkLOD : uint8_t {
    LOD0_MicroDiorama = 0, // 0m – 70m: 1.5m cells, sloped micro-terrain, dense flora, full shadows
    LOD1_Simplified   = 1, // 70m – 200m: 3.0m aggregated blocks (2x2 downsample), macro props, no micro-flora
    LOD2_MacroBlocks  = 2, // 200m – 600m: 6.0m greedy-meshed blocks (4x4 downsample), no props
    LOD3_HorizonRidge = 3  // 600m – 2000m: 12.0m macro steps (8x8 downsample), atmospheric haze
};

/**
 * Downsampled cell for hierarchical raw-block LOD pyramids.
 */
struct DownsampledCell {
    int16_t elevation{2};
    uint16_t topLayer{TexLayer::GrassMeadow};
    uint16_t cliffLayer{TexLayer::CliffStone};
    bool hasWater{false};
    float waterY{0.65f};
};

/**
 * Single 32x32 terrain chunk supporting hierarchical block LODs and 2D greedy meshing.
 */
class RealmChunk {
public:
    static constexpr int kTilesPerChunk = 32;
    static constexpr float kTileSize = 1.5f;
    static constexpr float kElevStep = 0.6f;
    static constexpr float kChunkSize = static_cast<float>(kTilesPerChunk) * kTileSize; // 48.0m

    // Pyramid dimensions
    static constexpr int kLOD1Dim = 16; // 16x16 blocks (2x2 downsampled, 3.0m each)
    static constexpr int kLOD2Dim = 8;  // 8x8 blocks (4x4 downsampled, 6.0m each)
    static constexpr int kLOD3Dim = 4;  // 4x4 blocks (8x8 downsampled, 12.0m each)

    explicit RealmChunk(ChunkCoord coord = {0, 0});

    [[nodiscard]] ChunkCoord coord() const noexcept { return coord_; }
    void setCoord(ChunkCoord coord) noexcept { coord_ = coord; }

    [[nodiscard]] ChunkLOD currentLOD() const noexcept { return currentLOD_; }
    void setLOD(ChunkLOD lod) noexcept { currentLOD_ = lod; }

    // World-space chunk bounds
    [[nodiscard]] Vec3f worldOrigin() const noexcept;
    [[nodiscard]] Vec3f worldCenter() const noexcept;
    [[nodiscard]] float distanceTo(Vec3f pos) const noexcept;

    // Tile access (local coordinates lx, lz in [0, 31])
    [[nodiscard]] TileCell& getTile(int lx, int lz) noexcept;
    [[nodiscard]] const TileCell& getTile(int lx, int lz) const noexcept;
    void setTile(int lx, int lz, const TileCell& cell) noexcept;

    // Elevation pyramid construction
    void computeElevationPyramid();

    // 2D Greedy Meshing for LOD 1, 2, 3 raw blocks
    // Merges contiguous coplanar top blocks and vertical cliff walls into minimal quads
    void buildGreedyMesh(
        ChunkLOD lod,
        std::vector<WorldVertex>& outVertices,
        const RealmChunk* neighborN = nullptr,
        const RealmChunk* neighborS = nullptr,
        const RealmChunk* neighborW = nullptr,
        const RealmChunk* neighborE = nullptr
    ) const;

    // Micro-diorama (LOD 0) full mesh generation
    void buildLOD0Mesh(
        std::vector<WorldVertex>& outVertices,
        const RealmChunk* neighborN = nullptr,
        const RealmChunk* neighborS = nullptr,
        const RealmChunk* neighborW = nullptr,
        const RealmChunk* neighborE = nullptr
    ) const;

    // Chunk architectural props and entities
    [[nodiscard]] const std::vector<WorldVertex>& propVertices() const noexcept { return propVertices_; }
    [[nodiscard]] std::vector<WorldVertex>& propVertices() noexcept { return propVertices_; }

    [[nodiscard]] const std::vector<Sprite3DInstance>& sprites() const noexcept { return sprites_; }
    [[nodiscard]] std::vector<Sprite3DInstance>& sprites() noexcept { return sprites_; }

    [[nodiscard]] const std::vector<PointLight>& pointLights() const noexcept { return pointLights_; }
    [[nodiscard]] std::vector<PointLight>& pointLights() noexcept { return pointLights_; }

    void addPropVertex(const WorldVertex& v) { propVertices_.push_back(v); }
    void addSprite(const Sprite3DInstance& sp) { sprites_.push_back(sp); }
    void addPointLight(const PointLight& pl) { pointLights_.push_back(pl); }
    void clearPropsAndSprites() noexcept { propVertices_.clear(); sprites_.clear(); pointLights_.clear(); }

    [[nodiscard]] const std::array<DownsampledCell, kLOD1Dim * kLOD1Dim>& lod1() const noexcept { return lod1_; }
    [[nodiscard]] const std::array<DownsampledCell, kLOD2Dim * kLOD2Dim>& lod2() const noexcept { return lod2_; }
    [[nodiscard]] const std::array<DownsampledCell, kLOD3Dim * kLOD3Dim>& lod3() const noexcept { return lod3_; }

private:
    static void emitGreedyQuad(
        std::vector<WorldVertex>& dst,
        Vec3f p0, Vec3f p1, Vec3f p2, Vec3f p3,
        Vec3f normal, Vec3f tangent,
        Vec2f uv0, Vec2f uv1, Vec2f uv2, Vec2f uv3,
        float ao0, float ao1, float ao2, float ao3,
        uint32_t texLayer, float roughness = 0.78f, uint32_t flags = 0u
    );

    ChunkCoord coord_{0, 0};
    ChunkLOD currentLOD_{ChunkLOD::LOD0_MicroDiorama};
    std::array<TileCell, kTilesPerChunk * kTilesPerChunk> tiles_;
    std::array<DownsampledCell, kLOD1Dim * kLOD1Dim> lod1_{};
    std::array<DownsampledCell, kLOD2Dim * kLOD2Dim> lod2_{};
    std::array<DownsampledCell, kLOD3Dim * kLOD3Dim> lod3_{};

    std::vector<WorldVertex> propVertices_;
    std::vector<Sprite3DInstance> sprites_;
    std::vector<PointLight> pointLights_;
};

} // namespace eatsrealms

#endif // EATSREALMS_REALM_CHUNK_HPP
