#include "eatsrealms/realm_chunk.hpp"
#include <map>

namespace eatsrealms {

RealmChunk::RealmChunk(ChunkCoord coord)
    : coord_(coord) {
    // Initialize default flat diorama tiles
    for (auto& t : tiles_) {
        t.elevation = 2;
        t.topLayer = TexLayer::GrassMeadow;
        t.cliffLayer = TexLayer::CliffStone;
        t.slope = SlopeDir::None;
        t.isStairs = false;
        t.hasWater = false;
        t.waterY = 0.65f;
    }
    computeElevationPyramid();
}

Vec3f RealmChunk::worldOrigin() const noexcept {
    return {
        static_cast<float>(coord_.cx) * kChunkSize,
        0.0f,
        static_cast<float>(coord_.cz) * kChunkSize
    };
}

Vec3f RealmChunk::worldCenter() const noexcept {
    return {
        (static_cast<float>(coord_.cx) + 0.5f) * kChunkSize,
        0.0f,
        (static_cast<float>(coord_.cz) + 0.5f) * kChunkSize
    };
}

float RealmChunk::distanceTo(Vec3f pos) const noexcept {
    Vec3f c = worldCenter();
    float dx = pos.x - c.x;
    float dz = pos.z - c.z;
    return std::sqrt(dx * dx + dz * dz);
}

TileCell& RealmChunk::getTile(int lx, int lz) noexcept {
    int cx = std::clamp(lx, 0, kTilesPerChunk - 1);
    int cz = std::clamp(lz, 0, kTilesPerChunk - 1);
    return tiles_[cz * kTilesPerChunk + cx];
}

const TileCell& RealmChunk::getTile(int lx, int lz) const noexcept {
    int cx = std::clamp(lx, 0, kTilesPerChunk - 1);
    int cz = std::clamp(lz, 0, kTilesPerChunk - 1);
    return tiles_[cz * kTilesPerChunk + cx];
}

void RealmChunk::setTile(int lx, int lz, const TileCell& cell) noexcept {
    if (lx >= 0 && lx < kTilesPerChunk && lz >= 0 && lz < kTilesPerChunk) {
        tiles_[lz * kTilesPerChunk + lx] = cell;
    }
}

void RealmChunk::computeElevationPyramid() {
    auto findDominant = [](const uint16_t items[4]) noexcept -> uint16_t {
        int bestC = 0;
        uint16_t bestL = items[0];
        for (int i = 0; i < 4; ++i) {
            int c = 1;
            for (int j = i + 1; j < 4; ++j) {
                if (items[j] == items[i]) ++c;
            }
            if (c > bestC) {
                bestC = c;
                bestL = items[i];
            }
        }
        return bestL;
    };

    // 1. Compute LOD 1: 16x16 blocks (2x2 downsample from 32x32 tiles)
    for (int lz = 0; lz < kLOD1Dim; ++lz) {
        for (int lx = 0; lx < kLOD1Dim; ++lx) {
            int tx0 = lx * 2;
            int tz0 = lz * 2;
            int elevSum = 0;
            uint16_t topSamples[4]{};
            uint16_t cliffSamples[4]{};
            bool water = false;
            float waterYSum = 0.0f;
            int waterCount = 0;

            int idx = 0;
            for (int dz = 0; dz < 2; ++dz) {
                for (int dx = 0; dx < 2; ++dx) {
                    const auto& t = tiles_[(tz0 + dz) * kTilesPerChunk + (tx0 + dx)];
                    elevSum += t.elevation;
                    topSamples[idx] = t.topLayer;
                    cliffSamples[idx] = t.cliffLayer;
                    ++idx;
                    if (t.hasWater) {
                        water = true;
                        waterYSum += t.waterY;
                        waterCount++;
                    }
                }
            }

            DownsampledCell cell{};
            cell.elevation = static_cast<int16_t>(std::round(static_cast<float>(elevSum) / 4.0f));
            cell.topLayer = findDominant(topSamples);
            cell.cliffLayer = findDominant(cliffSamples);
            cell.hasWater = water;
            cell.waterY = (waterCount > 0) ? (waterYSum / static_cast<float>(waterCount)) : 0.65f;

            lod1_[lz * kLOD1Dim + lx] = cell;
        }
    }

    // 2. Compute LOD 2: 8x8 blocks (2x2 downsample from LOD 1)
    for (int lz = 0; lz < kLOD2Dim; ++lz) {
        for (int lx = 0; lx < kLOD2Dim; ++lx) {
            int tx0 = lx * 2;
            int tz0 = lz * 2;
            int elevSum = 0;
            uint16_t topSamples[4]{};
            uint16_t cliffSamples[4]{};
            bool water = false;
            float waterYSum = 0.0f;
            int waterCount = 0;

            int idx = 0;
            for (int dz = 0; dz < 2; ++dz) {
                for (int dx = 0; dx < 2; ++dx) {
                    const auto& c = lod1_[(tz0 + dz) * kLOD1Dim + (tx0 + dx)];
                    elevSum += c.elevation;
                    topSamples[idx] = c.topLayer;
                    cliffSamples[idx] = c.cliffLayer;
                    ++idx;
                    if (c.hasWater) {
                        water = true;
                        waterYSum += c.waterY;
                        waterCount++;
                    }
                }
            }

            DownsampledCell cell{};
            cell.elevation = static_cast<int16_t>(std::round(static_cast<float>(elevSum) / 4.0f));
            cell.topLayer = findDominant(topSamples);
            cell.cliffLayer = findDominant(cliffSamples);
            cell.hasWater = water;
            cell.waterY = (waterCount > 0) ? (waterYSum / static_cast<float>(waterCount)) : 0.65f;

            lod2_[lz * kLOD2Dim + lx] = cell;
        }
    }

    // 3. Compute LOD 3: 4x4 blocks (2x2 downsample from LOD 2)
    for (int lz = 0; lz < kLOD3Dim; ++lz) {
        for (int lx = 0; lx < kLOD3Dim; ++lx) {
            int tx0 = lx * 2;
            int tz0 = lz * 2;
            int elevSum = 0;
            uint16_t topSamples[4]{};
            uint16_t cliffSamples[4]{};
            bool water = false;
            float waterYSum = 0.0f;
            int waterCount = 0;

            int idx = 0;
            for (int dz = 0; dz < 2; ++dz) {
                for (int dx = 0; dx < 2; ++dx) {
                    const auto& c = lod2_[(tz0 + dz) * kLOD2Dim + (tx0 + dx)];
                    elevSum += c.elevation;
                    topSamples[idx] = c.topLayer;
                    cliffSamples[idx] = c.cliffLayer;
                    ++idx;
                    if (c.hasWater) {
                        water = true;
                        waterYSum += c.waterY;
                        waterCount++;
                    }
                }
            }

            DownsampledCell cell{};
            cell.elevation = static_cast<int16_t>(std::round(static_cast<float>(elevSum) / 4.0f));
            cell.topLayer = findDominant(topSamples);
            cell.cliffLayer = findDominant(cliffSamples);
            cell.hasWater = water;
            cell.waterY = (waterCount > 0) ? (waterYSum / static_cast<float>(waterCount)) : 0.65f;

            lod3_[lz * kLOD3Dim + lx] = cell;
        }
    }
}

void RealmChunk::emitGreedyQuad(
    std::vector<WorldVertex>& dst,
    Vec3f p0, Vec3f p1, Vec3f p2, Vec3f p3,
    Vec3f normal, Vec3f tangent,
    Vec2f uv0, Vec2f uv1, Vec2f uv2, Vec2f uv3,
    float ao0, float ao1, float ao2, float ao3,
    uint32_t texLayer, float roughness, uint32_t flags
) {
    // Triangle 1: p0 -> p1 -> p2
    WorldVertex v0{}, v1{}, v2{}, v3{};

    v0.pos[0] = p0.x; v0.pos[1] = p0.y; v0.pos[2] = p0.z; v0.ao = ao0;
    v0.normal[0] = normal.x; v0.normal[1] = normal.y; v0.normal[2] = normal.z; v0.roughness = roughness;
    v0.tangent[0] = tangent.x; v0.tangent[1] = tangent.y; v0.tangent[2] = tangent.z; v0.texLayer = texLayer;
    v0.uv[0] = uv0.x; v0.uv[1] = uv0.y; v0.flags = flags;

    v1.pos[0] = p1.x; v1.pos[1] = p1.y; v1.pos[2] = p1.z; v1.ao = ao1;
    v1.normal[0] = normal.x; v1.normal[1] = normal.y; v1.normal[2] = normal.z; v1.roughness = roughness;
    v1.tangent[0] = tangent.x; v1.tangent[1] = tangent.y; v1.tangent[2] = tangent.z; v1.texLayer = texLayer;
    v1.uv[0] = uv1.x; v1.uv[1] = uv1.y; v1.flags = flags;

    v2.pos[0] = p2.x; v2.pos[1] = p2.y; v2.pos[2] = p2.z; v2.ao = ao2;
    v2.normal[0] = normal.x; v2.normal[1] = normal.y; v2.normal[2] = normal.z; v2.roughness = roughness;
    v2.tangent[0] = tangent.x; v2.tangent[1] = tangent.y; v2.tangent[2] = tangent.z; v2.texLayer = texLayer;
    v2.uv[0] = uv2.x; v2.uv[1] = uv2.y; v2.flags = flags;

    v3.pos[0] = p3.x; v3.pos[1] = p3.y; v3.pos[2] = p3.z; v3.ao = ao3;
    v3.normal[0] = normal.x; v3.normal[1] = normal.y; v3.normal[2] = normal.z; v3.roughness = roughness;
    v3.tangent[0] = tangent.x; v3.tangent[1] = tangent.y; v3.tangent[2] = tangent.z; v3.texLayer = texLayer;
    v3.uv[0] = uv3.x; v3.uv[1] = uv3.y; v3.flags = flags;

    dst.push_back(v0);
    dst.push_back(v1);
    dst.push_back(v2);

    // Triangle 2: p0 -> p2 -> p3
    dst.push_back(v0);
    dst.push_back(v2);
    dst.push_back(v3);
}

void RealmChunk::buildGreedyMesh(
    ChunkLOD lod,
    std::vector<WorldVertex>& outVertices,
    const RealmChunk* neighborN,
    const RealmChunk* neighborS,
    const RealmChunk* neighborW,
    const RealmChunk* neighborE
) const {
    const int N = (lod == ChunkLOD::LOD1_Simplified) ? kLOD1Dim :
                  (lod == ChunkLOD::LOD2_MacroBlocks) ? kLOD2Dim : kLOD3Dim;

    const float blockSize = kChunkSize / static_cast<float>(N);
    const Vec3f origin = worldOrigin();

    auto getDownsampledCell = [&](int x, int z) -> DownsampledCell {
        if (x >= 0 && x < N && z >= 0 && z < N) {
            if (lod == ChunkLOD::LOD1_Simplified) return lod1_[z * kLOD1Dim + x];
            if (lod == ChunkLOD::LOD2_MacroBlocks) return lod2_[z * kLOD2Dim + x];
            return lod3_[z * kLOD3Dim + x];
        }
        // Sample neighbor chunk if outside [0, N-1]
        if (z < 0 && neighborN) {
            int nx = std::clamp(x, 0, N - 1);
            if (lod == ChunkLOD::LOD1_Simplified) return neighborN->lod1()[(N - 1) * kLOD1Dim + nx];
            if (lod == ChunkLOD::LOD2_MacroBlocks) return neighborN->lod2()[(N - 1) * kLOD2Dim + nx];
            return neighborN->lod3()[(N - 1) * kLOD3Dim + nx];
        }
        if (z >= N && neighborS) {
            int nx = std::clamp(x, 0, N - 1);
            if (lod == ChunkLOD::LOD1_Simplified) return neighborS->lod1()[0 * kLOD1Dim + nx];
            if (lod == ChunkLOD::LOD2_MacroBlocks) return neighborS->lod2()[0 * kLOD2Dim + nx];
            return neighborS->lod3()[0 * kLOD3Dim + nx];
        }
        if (x < 0 && neighborW) {
            int nz = std::clamp(z, 0, N - 1);
            if (lod == ChunkLOD::LOD1_Simplified) return neighborW->lod1()[nz * kLOD1Dim + (N - 1)];
            if (lod == ChunkLOD::LOD2_MacroBlocks) return neighborW->lod2()[nz * kLOD2Dim + (N - 1)];
            return neighborW->lod3()[nz * kLOD3Dim + (N - 1)];
        }
        if (x >= N && neighborE) {
            int nz = std::clamp(z, 0, N - 1);
            if (lod == ChunkLOD::LOD1_Simplified) return neighborE->lod1()[nz * kLOD1Dim + 0];
            if (lod == ChunkLOD::LOD2_MacroBlocks) return neighborE->lod2()[nz * kLOD2Dim + 0];
            return neighborE->lod3()[nz * kLOD3Dim + 0];
        }
        // Default border cell
        DownsampledCell def{};
        def.elevation = 0;
        def.topLayer = TexLayer::GrassMeadow;
        def.cliffLayer = TexLayer::CliffStone;
        return def;
    };

    // =========================================================================
    // 1. GREEDY MESH TOP FACES (Y = elevation * kElevStep)
    // =========================================================================
    std::vector<bool> visited(N * N, false);

    for (int z = 0; z < N; ++z) {
        for (int x = 0; x < N; ++x) {
            if (visited[z * N + x]) continue;

            const DownsampledCell start = getDownsampledCell(x, z);
            int16_t targetElev = start.elevation;
            uint16_t targetLayer = start.topLayer;

            // Expand horizontally along X
            int width = 1;
            while (x + width < N) {
                if (visited[z * N + (x + width)]) break;
                const auto next = getDownsampledCell(x + width, z);
                if (next.elevation != targetElev || next.topLayer != targetLayer) break;
                ++width;
            }

            // Expand vertically along Z
            int height = 1;
            bool canExpandZ = true;
            while (z + height < N && canExpandZ) {
                for (int w = 0; w < width; ++w) {
                    if (visited[(z + height) * N + (x + w)]) {
                        canExpandZ = false;
                        break;
                    }
                    const auto next = getDownsampledCell(x + w, z + height);
                    if (next.elevation != targetElev || next.topLayer != targetLayer) {
                        canExpandZ = false;
                        break;
                    }
                }
                if (canExpandZ) {
                    ++height;
                }
            }

            // Mark cells visited
            for (int dz = 0; dz < height; ++dz) {
                for (int dx = 0; dx < width; ++dx) {
                    visited[(z + dz) * N + (x + dx)] = true;
                }
            }

            // Emit merged quad
            float x0 = origin.x + static_cast<float>(x) * blockSize;
            float x1 = x0 + static_cast<float>(width) * blockSize;
            float z0 = origin.z + static_cast<float>(z) * blockSize;
            float z1 = z0 + static_cast<float>(height) * blockSize;
            float yTop = static_cast<float>(targetElev) * kElevStep;

            // Non-stretching repeating UV coordinates
            const float uvScale = 1.0f / (kTileSize * 2.0f);
            Vec2f uv0{x0 * uvScale, z0 * uvScale};
            Vec2f uv1{x1 * uvScale, z0 * uvScale};
            Vec2f uv2{x1 * uvScale, z1 * uvScale};
            Vec2f uv3{x0 * uvScale, z1 * uvScale};

            emitGreedyQuad(outVertices,
                {x0, yTop, z0}, {x1, yTop, z0}, {x1, yTop, z1}, {x0, yTop, z1},
                {0.0f, 1.0f, 0.0f}, {1.0f, 0.0f, 0.0f},
                uv0, uv1, uv2, uv3,
                0.95f, 0.95f, 0.95f, 0.95f,
                targetLayer, 0.80f, 0u
            );
        }
    }

    // =========================================================================
    // 2. GREEDY MESH CARDINAL CLIFF FACES (North, South, West, East)
    // =========================================================================
    // 2a. North walls (facing -Z): check if cell(x, z) > cell(x, z - 1)
    for (int z = 0; z < N; ++z) {
        int x = 0;
        while (x < N) {
            const auto cur = getDownsampledCell(x, z);
            const auto nbr = getDownsampledCell(x, z - 1);
            if (cur.elevation > nbr.elevation) {
                int16_t curElev = cur.elevation;
                int16_t nbrElev = nbr.elevation;
                uint16_t cliffLayer = cur.cliffLayer;

                int width = 1;
                while (x + width < N) {
                    const auto cNext = getDownsampledCell(x + width, z);
                    const auto nNext = getDownsampledCell(x + width, z - 1);
                    if (cNext.elevation != curElev || nNext.elevation != nbrElev || cNext.cliffLayer != cliffLayer) break;
                    ++width;
                }

                float x0 = origin.x + static_cast<float>(x) * blockSize;
                float x1 = x0 + static_cast<float>(width) * blockSize;
                float zEdge = origin.z + static_cast<float>(z) * blockSize;
                float yBot = static_cast<float>(nbrElev) * kElevStep;
                float yTop = static_cast<float>(curElev) * kElevStep;

                float u0 = x0 * (1.0f / kTileSize);
                float u1 = x1 * (1.0f / kTileSize);
                float vBot = (yTop - yBot) / kTileSize;

                emitGreedyQuad(outVertices,
                    {x0, yBot, zEdge}, {x1, yBot, zEdge}, {x1, yTop, zEdge}, {x0, yTop, zEdge},
                    {0.0f, 0.0f, -1.0f}, {1.0f, 0.0f, 0.0f},
                    {u0, vBot}, {u1, vBot}, {u1, 0.0f}, {u0, 0.0f},
                    0.55f, 0.55f, 0.95f, 0.95f,
                    cliffLayer, 0.85f, 0u
                );

                x += width;
            } else {
                ++x;
            }
        }
    }

    // 2b. South walls (facing +Z): check if cell(x, z) > cell(x, z + 1)
    for (int z = 0; z < N; ++z) {
        int x = 0;
        while (x < N) {
            const auto cur = getDownsampledCell(x, z);
            const auto nbr = getDownsampledCell(x, z + 1);
            if (cur.elevation > nbr.elevation) {
                int16_t curElev = cur.elevation;
                int16_t nbrElev = nbr.elevation;
                uint16_t cliffLayer = cur.cliffLayer;

                int width = 1;
                while (x + width < N) {
                    const auto cNext = getDownsampledCell(x + width, z);
                    const auto nNext = getDownsampledCell(x + width, z + 1);
                    if (cNext.elevation != curElev || nNext.elevation != nbrElev || cNext.cliffLayer != cliffLayer) break;
                    ++width;
                }

                float x0 = origin.x + static_cast<float>(x) * blockSize;
                float x1 = x0 + static_cast<float>(width) * blockSize;
                float zEdge = origin.z + static_cast<float>(z + 1) * blockSize;
                float yBot = static_cast<float>(nbrElev) * kElevStep;
                float yTop = static_cast<float>(curElev) * kElevStep;

                float u0 = x0 * (1.0f / kTileSize);
                float u1 = x1 * (1.0f / kTileSize);
                float vBot = (yTop - yBot) / kTileSize;

                emitGreedyQuad(outVertices,
                    {x1, yBot, zEdge}, {x0, yBot, zEdge}, {x0, yTop, zEdge}, {x1, yTop, zEdge},
                    {0.0f, 0.0f, 1.0f}, {-1.0f, 0.0f, 0.0f},
                    {u1, vBot}, {u0, vBot}, {u0, 0.0f}, {u1, 0.0f},
                    0.55f, 0.55f, 0.95f, 0.95f,
                    cliffLayer, 0.85f, 0u
                );

                x += width;
            } else {
                ++x;
            }
        }
    }

    // 2c. West walls (facing -X): check if cell(x, z) > cell(x - 1, z)
    for (int x = 0; x < N; ++x) {
        int z = 0;
        while (z < N) {
            const auto cur = getDownsampledCell(x, z);
            const auto nbr = getDownsampledCell(x - 1, z);
            if (cur.elevation > nbr.elevation) {
                int16_t curElev = cur.elevation;
                int16_t nbrElev = nbr.elevation;
                uint16_t cliffLayer = cur.cliffLayer;

                int height = 1;
                while (z + height < N) {
                    const auto cNext = getDownsampledCell(x, z + height);
                    const auto nNext = getDownsampledCell(x - 1, z + height);
                    if (cNext.elevation != curElev || nNext.elevation != nbrElev || cNext.cliffLayer != cliffLayer) break;
                    ++height;
                }

                float xEdge = origin.x + static_cast<float>(x) * blockSize;
                float z0 = origin.z + static_cast<float>(z) * blockSize;
                float z1 = z0 + static_cast<float>(height) * blockSize;
                float yBot = static_cast<float>(nbrElev) * kElevStep;
                float yTop = static_cast<float>(curElev) * kElevStep;

                float u0 = z0 * (1.0f / kTileSize);
                float u1 = z1 * (1.0f / kTileSize);
                float vBot = (yTop - yBot) / kTileSize;

                emitGreedyQuad(outVertices,
                    {xEdge, yBot, z1}, {xEdge, yBot, z0}, {xEdge, yTop, z0}, {xEdge, yTop, z1},
                    {-1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, -1.0f},
                    {u1, vBot}, {u0, vBot}, {u0, 0.0f}, {u1, 0.0f},
                    0.55f, 0.55f, 0.95f, 0.95f,
                    cliffLayer, 0.85f, 0u
                );

                z += height;
            } else {
                ++z;
            }
        }
    }

    // 2d. East walls (facing +X): check if cell(x, z) > cell(x + 1, z)
    for (int x = 0; x < N; ++x) {
        int z = 0;
        while (z < N) {
            const auto cur = getDownsampledCell(x, z);
            const auto nbr = getDownsampledCell(x + 1, z);
            if (cur.elevation > nbr.elevation) {
                int16_t curElev = cur.elevation;
                int16_t nbrElev = nbr.elevation;
                uint16_t cliffLayer = cur.cliffLayer;

                int height = 1;
                while (z + height < N) {
                    const auto cNext = getDownsampledCell(x, z + height);
                    const auto nNext = getDownsampledCell(x + 1, z + height);
                    if (cNext.elevation != curElev || nNext.elevation != nbrElev || cNext.cliffLayer != cliffLayer) break;
                    ++height;
                }

                float xEdge = origin.x + static_cast<float>(x + 1) * blockSize;
                float z0 = origin.z + static_cast<float>(z) * blockSize;
                float z1 = z0 + static_cast<float>(height) * blockSize;
                float yBot = static_cast<float>(nbrElev) * kElevStep;
                float yTop = static_cast<float>(curElev) * kElevStep;

                float u0 = z0 * (1.0f / kTileSize);
                float u1 = z1 * (1.0f / kTileSize);
                float vBot = (yTop - yBot) / kTileSize;

                emitGreedyQuad(outVertices,
                    {xEdge, yBot, z0}, {xEdge, yBot, z1}, {xEdge, yTop, z1}, {xEdge, yTop, z0},
                    {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f},
                    {u0, vBot}, {u1, vBot}, {u1, 0.0f}, {u0, 0.0f},
                    0.55f, 0.55f, 0.95f, 0.95f,
                    cliffLayer, 0.85f, 0u
                );

                z += height;
            } else {
                ++z;
            }
        }
    }

    // =========================================================================
    // 3. GREEDY MESH WATER SURFACE
    // =========================================================================
    std::vector<bool> waterVisited(N * N, false);
    for (int z = 0; z < N; ++z) {
        for (int x = 0; x < N; ++x) {
            if (waterVisited[z * N + x]) continue;
            const auto cur = getDownsampledCell(x, z);
            if (!cur.hasWater) continue;

            float wy = cur.waterY;
            int width = 1;
            while (x + width < N) {
                if (waterVisited[z * N + (x + width)]) break;
                const auto next = getDownsampledCell(x + width, z);
                if (!next.hasWater || std::abs(next.waterY - wy) > 0.05f) break;
                ++width;
            }

            int height = 1;
            bool canExpandZ = true;
            while (z + height < N && canExpandZ) {
                for (int w = 0; w < width; ++w) {
                    if (waterVisited[(z + height) * N + (x + w)]) { canExpandZ = false; break; }
                    const auto next = getDownsampledCell(x + w, z + height);
                    if (!next.hasWater || std::abs(next.waterY - wy) > 0.05f) { canExpandZ = false; break; }
                }
                if (canExpandZ) ++height;
            }

            for (int dz = 0; dz < height; ++dz) {
                for (int dx = 0; dx < width; ++dx) {
                    waterVisited[(z + dz) * N + (x + dx)] = true;
                }
            }

            float x0 = origin.x + static_cast<float>(x) * blockSize;
            float x1 = x0 + static_cast<float>(width) * blockSize;
            float z0 = origin.z + static_cast<float>(z) * blockSize;
            float z1 = z0 + static_cast<float>(height) * blockSize;

            emitGreedyQuad(outVertices,
                {x0, wy, z0}, {x1, wy, z0}, {x1, wy, z1}, {x0, wy, z1},
                {0.0f, 1.0f, 0.0f}, {1.0f, 0.0f, 0.0f},
                {x0, z0}, {x1, z0}, {x1, z1}, {x0, z1},
                0.95f, 0.95f, 0.95f, 0.95f,
                TexLayer::WaterSurface, 0.05f, 1u
            );
        }
    }
}

void RealmChunk::buildLOD0Mesh(
    std::vector<WorldVertex>& outVertices,
    const RealmChunk* neighborN,
    const RealmChunk* neighborS,
    const RealmChunk* neighborW,
    const RealmChunk* neighborE
) const {
    const Vec3f origin = worldOrigin();

    auto sampleNeighborTile = [&](int lx, int lz) -> TileCell {
        if (lx >= 0 && lx < kTilesPerChunk && lz >= 0 && lz < kTilesPerChunk) {
            return getTile(lx, lz);
        }
        if (lz < 0 && neighborN) return neighborN->getTile(lx, kTilesPerChunk - 1);
        if (lz >= kTilesPerChunk && neighborS) return neighborS->getTile(lx, 0);
        if (lx < 0 && neighborW) return neighborW->getTile(kTilesPerChunk - 1, lz);
        if (lx >= kTilesPerChunk && neighborE) return neighborE->getTile(0, lz);
        TileCell fallback{};
        fallback.elevation = 0;
        return fallback;
    };

    for (int lz = 0; lz < kTilesPerChunk; ++lz) {
        for (int lx = 0; lx < kTilesPerChunk; ++lx) {
            const TileCell& cell = getTile(lx, lz);

            float x0 = origin.x + static_cast<float>(lx) * kTileSize;
            float x1 = x0 + kTileSize;
            float z0 = origin.z + static_cast<float>(lz) * kTileSize;
            float z1 = z0 + kTileSize;

            float y00 = static_cast<float>(cell.getCornerElevation(0)) * kElevStep; // NW
            float y10 = static_cast<float>(cell.getCornerElevation(1)) * kElevStep; // NE
            float y11 = static_cast<float>(cell.getCornerElevation(2)) * kElevStep; // SE
            float y01 = static_cast<float>(cell.getCornerElevation(3)) * kElevStep; // SW
            bool isFlat = (y00 == y10 && y10 == y11 && y11 == y01);
            float yTop = y00;

            // Cardinal cliff walls around the tile
            auto emitWall = [&](Vec3f pA_top, Vec3f pB_top, float yA_bot, float yB_bot, Vec3f norm, Vec3f tang) {
                float diffA = pA_top.y - yA_bot;
                float diffB = pB_top.y - yB_bot;
                if (diffA <= 0.005f && diffB <= 0.005f) return;

                Vec3f pA_bot{pA_top.x, yA_bot, pA_top.z};
                Vec3f pB_bot{pB_top.x, yB_bot, pB_top.z};

                float vSpanA = std::max(0.1f, diffA / kTileSize);
                float vSpanB = std::max(0.1f, diffB / kTileSize);

                if (diffA > 0.005f && diffB > 0.005f) {
                    emitGreedyQuad(outVertices,
                        pA_bot, pB_bot, pB_top, pA_top,
                        norm, tang,
                        {0.0f, vSpanA}, {1.0f, vSpanB}, {1.0f, 0.0f}, {0.0f, 0.0f},
                        0.48f, 0.48f, 0.95f, 0.95f, cell.cliffLayer, 0.85f, 0u);
                } else if (diffA > 0.005f) {
                    WorldVertex va{}, vb{}, vc{};
                    va.pos[0] = pA_bot.x; va.pos[1] = pA_bot.y; va.pos[2] = pA_bot.z; va.ao = 0.75f;
                    va.normal[0] = norm.x; va.normal[1] = norm.y; va.normal[2] = norm.z; va.roughness = 0.85f;
                    va.tangent[0] = tang.x; va.tangent[1] = tang.y; va.tangent[2] = tang.z; va.texLayer = cell.cliffLayer;
                    va.uv[0] = 0.0f; va.uv[1] = vSpanA;

                    vb.pos[0] = pB_top.x; vb.pos[1] = pB_top.y; vb.pos[2] = pB_top.z; vb.ao = 0.75f;
                    vb.normal[0] = norm.x; vb.normal[1] = norm.y; vb.normal[2] = norm.z; vb.roughness = 0.85f;
                    vb.tangent[0] = tang.x; vb.tangent[1] = tang.y; vb.tangent[2] = tang.z; vb.texLayer = cell.cliffLayer;
                    vb.uv[0] = 1.0f; vb.uv[1] = 0.0f;

                    vc.pos[0] = pA_top.x; vc.pos[1] = pA_top.y; vc.pos[2] = pA_top.z; vc.ao = 0.75f;
                    vc.normal[0] = norm.x; vc.normal[1] = norm.y; vc.normal[2] = norm.z; vc.roughness = 0.85f;
                    vc.tangent[0] = tang.x; vc.tangent[1] = tang.y; vc.tangent[2] = tang.z; vc.texLayer = cell.cliffLayer;
                    vc.uv[0] = 0.0f; vc.uv[1] = 0.0f;

                    outVertices.push_back(va); outVertices.push_back(vb); outVertices.push_back(vc);
                } else {
                    WorldVertex va{}, vb{}, vc{};
                    va.pos[0] = pA_top.x; va.pos[1] = pA_top.y; va.pos[2] = pA_top.z; va.ao = 0.75f;
                    va.normal[0] = norm.x; va.normal[1] = norm.y; va.normal[2] = norm.z; va.roughness = 0.85f;
                    va.tangent[0] = tang.x; va.tangent[1] = tang.y; va.tangent[2] = tang.z; va.texLayer = cell.cliffLayer;
                    va.uv[0] = 0.0f; va.uv[1] = 0.0f;

                    vb.pos[0] = pB_bot.x; vb.pos[1] = pB_bot.y; vb.pos[2] = pB_bot.z; vb.ao = 0.75f;
                    vb.normal[0] = norm.x; vb.normal[1] = norm.y; vb.normal[2] = norm.z; vb.roughness = 0.85f;
                    vb.tangent[0] = tang.x; vb.tangent[1] = tang.y; vb.tangent[2] = tang.z; vb.texLayer = cell.cliffLayer;
                    vb.uv[0] = 1.0f; vb.uv[1] = vSpanB;

                    vc.pos[0] = pB_top.x; vc.pos[1] = pB_top.y; vc.pos[2] = pB_top.z; vc.ao = 0.75f;
                    vc.normal[0] = norm.x; vc.normal[1] = norm.y; vc.normal[2] = norm.z; vc.roughness = 0.85f;
                    vc.tangent[0] = tang.x; vc.tangent[1] = tang.y; vc.tangent[2] = tang.z; vc.texLayer = cell.cliffLayer;
                    vc.uv[0] = 1.0f; vc.uv[1] = 0.0f;

                    outVertices.push_back(va); outVertices.push_back(vb); outVertices.push_back(vc);
                }
            };

            float botN_A = static_cast<float>(sampleNeighborTile(lx, lz - 1).getCornerElevation(3)) * kElevStep;
            float botN_B = static_cast<float>(sampleNeighborTile(lx, lz - 1).getCornerElevation(2)) * kElevStep;
            float botE_A = static_cast<float>(sampleNeighborTile(lx + 1, lz).getCornerElevation(0)) * kElevStep;
            float botE_B = static_cast<float>(sampleNeighborTile(lx + 1, lz).getCornerElevation(3)) * kElevStep;
            float botS_A = static_cast<float>(sampleNeighborTile(lx, lz + 1).getCornerElevation(1)) * kElevStep;
            float botS_B = static_cast<float>(sampleNeighborTile(lx, lz + 1).getCornerElevation(0)) * kElevStep;
            float botW_A = static_cast<float>(sampleNeighborTile(lx - 1, lz).getCornerElevation(2)) * kElevStep;
            float botW_B = static_cast<float>(sampleNeighborTile(lx - 1, lz).getCornerElevation(1)) * kElevStep;

            if (isFlat) {
                emitGreedyQuad(outVertices,
                    {x0, yTop, z0}, {x1, yTop, z0}, {x1, yTop, z1}, {x0, yTop, z1},
                    {0.0f, 1.0f, 0.0f}, {1.0f, 0.0f, 0.0f},
                    {0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f},
                    0.95f, 0.95f, 0.95f, 0.95f, cell.topLayer, 0.80f, 0u);

                emitWall({x0, yTop, z0}, {x1, yTop, z0}, botN_A, botN_B, { 0.0f, 0.0f, -1.0f}, { 1.0f, 0.0f,  0.0f});
                emitWall({x1, yTop, z0}, {x1, yTop, z1}, botE_A, botE_B, { 1.0f, 0.0f,  0.0f}, { 0.0f, 0.0f,  1.0f});
                emitWall({x1, yTop, z1}, {x0, yTop, z1}, botS_A, botS_B, { 0.0f, 0.0f,  1.0f}, {-1.0f, 0.0f,  0.0f});
                emitWall({x0, yTop, z1}, {x0, yTop, z0}, botW_A, botW_B, {-1.0f, 0.0f,  0.0f}, { 0.0f, 0.0f, -1.0f});
            } else {
                // Sloped terrain: emit 2 triangles
                Vec3f p00{x0, y00, z0};
                Vec3f p10{x1, y10, z0};
                Vec3f p11{x1, y11, z1};
                Vec3f p01{x0, y01, z1};

                float nx = -(y10 - y00) * kTileSize;
                float ny = kTileSize * kTileSize;
                float nz = -(y01 - y00) * kTileSize;
                float nlen = std::sqrt(nx * nx + ny * ny + nz * nz);
                Vec3f slopeNorm = (nlen > 1e-4f) ? Vec3f{nx / nlen, ny / nlen, nz / nlen} : Vec3f{0.0f, 1.0f, 0.0f};
                Vec3f slopeTang{1.0f, (y10 - y00) / kTileSize, 0.0f};

                WorldVertex va{}, vb{}, vc{};
                // T1: p00 -> p10 -> p11
                va.pos[0] = p00.x; va.pos[1] = p00.y; va.pos[2] = p00.z; va.ao = 0.95f;
                va.normal[0] = slopeNorm.x; va.normal[1] = slopeNorm.y; va.normal[2] = slopeNorm.z; va.roughness = 0.80f;
                va.tangent[0] = slopeTang.x; va.tangent[1] = slopeTang.y; va.tangent[2] = slopeTang.z; va.texLayer = cell.topLayer;
                va.uv[0] = 0.0f; va.uv[1] = 0.0f;

                vb.pos[0] = p10.x; vb.pos[1] = p10.y; vb.pos[2] = p10.z; vb.ao = 0.95f;
                vb.normal[0] = slopeNorm.x; vb.normal[1] = slopeNorm.y; vb.normal[2] = slopeNorm.z; vb.roughness = 0.80f;
                vb.tangent[0] = slopeTang.x; vb.tangent[1] = slopeTang.y; vb.tangent[2] = slopeTang.z; vb.texLayer = cell.topLayer;
                vb.uv[0] = 1.0f; vb.uv[1] = 0.0f;

                vc.pos[0] = p11.x; vc.pos[1] = p11.y; vc.pos[2] = p11.z; vc.ao = 0.95f;
                vc.normal[0] = slopeNorm.x; vc.normal[1] = slopeNorm.y; vc.normal[2] = slopeNorm.z; vc.roughness = 0.80f;
                vc.tangent[0] = slopeTang.x; vc.tangent[1] = slopeTang.y; vc.tangent[2] = slopeTang.z; vc.texLayer = cell.topLayer;
                vc.uv[0] = 1.0f; vc.uv[1] = 1.0f;

                outVertices.push_back(va); outVertices.push_back(vb); outVertices.push_back(vc);

                // T2: p00 -> p11 -> p01
                vb = vc;
                vc.pos[0] = p01.x; vc.pos[1] = p01.y; vc.pos[2] = p01.z; vc.ao = 0.95f;
                vc.uv[0] = 0.0f; vc.uv[1] = 1.0f;
                outVertices.push_back(va); outVertices.push_back(vb); outVertices.push_back(vc);

                emitWall(p00, p10, botN_A, botN_B, { 0.0f, 0.0f, -1.0f}, { 1.0f, 0.0f,  0.0f});
                emitWall(p10, p11, botE_A, botE_B, { 1.0f, 0.0f,  0.0f}, { 0.0f, 0.0f,  1.0f});
                emitWall(p11, p01, botS_A, botS_B, { 0.0f, 0.0f,  1.0f}, {-1.0f, 0.0f,  0.0f});
                emitWall(p01, p00, botW_A, botW_B, {-1.0f, 0.0f,  0.0f}, { 0.0f, 0.0f, -1.0f});
            }

            if (cell.hasWater) {
                emitGreedyQuad(outVertices,
                    {x0, cell.waterY, z0}, {x1, cell.waterY, z0}, {x1, cell.waterY, z1}, {x0, cell.waterY, z1},
                    {0.0f, 1.0f, 0.0f}, {1.0f, 0.0f, 0.0f},
                    {x0, z0}, {x1, z0}, {x1, z1}, {x0, z1},
                    0.95f, 0.95f, 0.95f, 0.95f,
                    TexLayer::WaterSurface, 0.05f, 1u);
            }
        }
    }
}

} // namespace eatsrealms
