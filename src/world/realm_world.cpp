#include "eatsrealms/realm_world.hpp"
#include "eatsrealms/world_generator.hpp"
#include "eatsrealms/history_sim.hpp"
#include <cmath>
#include <algorithm>

namespace eatsrealms {

RealmWorld::RealmWorld()
    : tiles_(static_cast<size_t>(kGridWidth) * kGridDepth)
    , atlas_(ProceduralAtlasGenerator::buildAllLayers()) {
    clear();
}

void RealmWorld::clear() {
    for (auto& t : tiles_) {
        t = TileCell{};
    }
    propVertices_.clear();
    worldVertices_.clear();
    sprites_.clear();
    pointLights_.clear();
    terrainTriCount_ = 0;
    propTriCount_ = 0;
    chunks_.clear();
    lod0VertexCount_ = 0;
    lod0SpriteCount_ = 0;
    camera_ = CameraConfig{};
    atmosphere_ = AtmosphereConfig{};
}

RealmChunk* RealmWorld::getOrCreateChunk(int cx, int cz) {
    ChunkCoord coord{cx, cz};
    auto it = chunks_.find(coord);
    if (it != chunks_.end()) {
        return it->second.get();
    }
    auto chunk = std::make_unique<RealmChunk>(coord);
    RealmChunk* ptr = chunk.get();
    chunks_[coord] = std::move(chunk);
    return ptr;
}

RealmChunk* RealmWorld::getChunk(int cx, int cz) noexcept {
    auto it = chunks_.find(ChunkCoord{cx, cz});
    return (it != chunks_.end()) ? it->second.get() : nullptr;
}

const RealmChunk* RealmWorld::getChunk(int cx, int cz) const noexcept {
    auto it = chunks_.find(ChunkCoord{cx, cz});
    return (it != chunks_.end()) ? it->second.get() : nullptr;
}

void RealmWorld::generateOpenWorld(uint32_t seed, int chunkRadius, float centerX, float centerZ) {
    clear();
    openWorldSeed_ = seed;
    camera_.target = {centerX, 0.0f, centerZ};
    float streamDist = static_cast<float>(std::clamp(chunkRadius, 2, 10)) * RealmChunk::kChunkSize;
    streamChunksAround(camera_.target, streamDist, streamDist + 180.0f);
    float groundH = getTerrainHeightAt(centerX, centerZ);
    camera_.target.y = groundH + 1.8f;
    camera_.position = {centerX, camera_.target.y + 18.0f, centerZ - 24.0f};
    updateStreamingClipmap(camera_.target, 70.0f, 200.0f, 600.0f);
}

bool RealmWorld::streamChunksAround(Vec3f cameraPos, float viewDistance, float unloadDistance) {
    bool dirty = false;
    const float chunkSize = RealmChunk::kChunkSize;
    const int centerCx = static_cast<int>(std::floor(cameraPos.x / chunkSize));
    const int centerCz = static_cast<int>(std::floor(cameraPos.z / chunkSize));
    const int chunkRadius = static_cast<int>(std::ceil(viewDistance / chunkSize));

    // 1. Generate chunks on-demand within viewDistance (No Man's Sky continuous synthesis)
    for (int dcz = -chunkRadius; dcz <= chunkRadius; ++dcz) {
        for (int dcx = -chunkRadius; dcx <= chunkRadius; ++dcx) {
            int cx = centerCx + dcx;
            int cz = centerCz + dcz;
            ChunkCoord coord{cx, cz};

            float chunkMidX = (static_cast<float>(cx) + 0.5f) * chunkSize;
            float chunkMidZ = (static_cast<float>(cz) + 0.5f) * chunkSize;
            float dx = cameraPos.x - chunkMidX;
            float dz = cameraPos.z - chunkMidZ;
            float dist = std::sqrt(dx * dx + dz * dz);

            if (dist <= viewDistance + chunkSize * 0.5f) {
                if (chunks_.find(coord) == chunks_.end()) {
                    RealmChunk* ch = getOrCreateChunk(cx, cz);
                    ProceduralWorldGenerator::generateProceduralChunk(*ch, openWorldSeed_);
                    dirty = true;
                }
            }
        }
    }

    // 2. Prune distant chunks beyond unloadDistance to bound memory
    if (chunks_.size() > 320) {
        for (auto it = chunks_.begin(); it != chunks_.end(); ) {
            float dist = it->second->distanceTo(cameraPos);
            if (dist > unloadDistance) {
                it = chunks_.erase(it);
                dirty = true;
            } else {
                ++it;
            }
        }
    }

    // 3. Update continuous hierarchical LODs relative to moving camera
    for (auto& [coord, chunk] : chunks_) {
        float dist = chunk->distanceTo(cameraPos);
        ChunkLOD targetLOD;
        if (dist <= 70.0f) {
            targetLOD = ChunkLOD::LOD0_MicroDiorama;
        } else if (dist <= 180.0f) {
            targetLOD = ChunkLOD::LOD1_Simplified;
        } else if (dist <= 350.0f) {
            targetLOD = ChunkLOD::LOD2_MacroBlocks;
        } else {
            targetLOD = ChunkLOD::LOD3_HorizonRidge;
        }

        if (chunk->currentLOD() != targetLOD) {
            chunk->setLOD(targetLOD);
            dirty = true;
        }
    }

    return dirty;
}

void RealmWorld::updateStreamingClipmap(
    Vec3f cameraFocusPos,
    float lod0Radius,
    float lod1Radius,
    float lod2Radius,
    float lod3Radius
) {
    (void)lod3Radius;
    for (auto& [coord, chunk] : chunks_) {
        float d = chunk->distanceTo(cameraFocusPos);
        if (d <= lod0Radius) {
            chunk->setLOD(ChunkLOD::LOD0_MicroDiorama);
        } else if (d <= lod1Radius) {
            chunk->setLOD(ChunkLOD::LOD1_Simplified);
        } else if (d <= lod2Radius) {
            chunk->setLOD(ChunkLOD::LOD2_MacroBlocks);
        } else {
            chunk->setLOD(ChunkLOD::LOD3_HorizonRidge);
        }
    }
}

Vec3f RealmWorld::gridToWorld(int gx, int gz) const noexcept {
    float wx = (static_cast<float>(gx) - static_cast<float>(kGridWidth) * 0.5f) * kTileSize;
    float wz = (static_cast<float>(gz) - static_cast<float>(kGridDepth) * 0.5f) * kTileSize;
    float wy = static_cast<float>(getTile(gx, gz).elevation) * kElevStep;
    return Vec3f{wx, wy, wz};
}

const TileCell& RealmWorld::getTile(int gx, int gz) const noexcept {
    int cx = std::clamp(gx, 0, kGridWidth - 1);
    int cz = std::clamp(gz, 0, kGridDepth - 1);
    return tiles_[static_cast<size_t>(cz) * kGridWidth + static_cast<size_t>(cx)];
}

float RealmWorld::getTerrainHeightAt(float worldX, float worldZ) const noexcept {
    if (!chunks_.empty()) {
        float chunkSpan = RealmChunk::kChunkSize;
        int cx = static_cast<int>(std::floor(worldX / chunkSpan));
        int cz = static_cast<int>(std::floor(worldZ / chunkSpan));
        const RealmChunk* ch = getChunk(cx, cz);
        if (ch) {
            float lxWorld = worldX - static_cast<float>(cx) * chunkSpan;
            float lzWorld = worldZ - static_cast<float>(cz) * chunkSpan;
            float fx = lxWorld / kTileSize;
            float fz = lzWorld / kTileSize;
            int lx = std::clamp(static_cast<int>(std::floor(fx)), 0, RealmChunk::kTilesPerChunk - 1);
            int lz = std::clamp(static_cast<int>(std::floor(fz)), 0, RealmChunk::kTilesPerChunk - 1);
            const TileCell& cell = ch->getTile(lx, lz);
            float u = std::clamp(fx - std::floor(fx), 0.0f, 1.0f);
            float v = std::clamp(fz - std::floor(fz), 0.0f, 1.0f);
            float y00 = static_cast<float>(cell.getCornerElevation(0)) * kElevStep;
            float y10 = static_cast<float>(cell.getCornerElevation(1)) * kElevStep;
            float y11 = static_cast<float>(cell.getCornerElevation(2)) * kElevStep;
            float y01 = static_cast<float>(cell.getCornerElevation(3)) * kElevStep;
            if (u + v <= 1.0f) {
                return y00 + u * (y10 - y00) + v * (y01 - y00);
            } else {
                return y11 + (1.0f - u) * (y01 - y11) + (1.0f - v) * (y10 - y11);
            }
        }
    }

    float fx = (worldX / kTileSize) + static_cast<float>(kGridWidth) * 0.5f;
    float fz = (worldZ / kTileSize) + static_cast<float>(kGridDepth) * 0.5f;
    int gx = std::clamp(static_cast<int>(std::floor(fx)), 0, kGridWidth - 1);
    int gz = std::clamp(static_cast<int>(std::floor(fz)), 0, kGridDepth - 1);
    const TileCell& cell = getTile(gx, gz);

    float u = std::clamp(fx - std::floor(fx), 0.0f, 1.0f);
    float v = std::clamp(fz - std::floor(fz), 0.0f, 1.0f);

    float y00 = static_cast<float>(cell.getCornerElevation(0)) * kElevStep; // NW
    float y10 = static_cast<float>(cell.getCornerElevation(1)) * kElevStep; // NE
    float y11 = static_cast<float>(cell.getCornerElevation(2)) * kElevStep; // SE
    float y01 = static_cast<float>(cell.getCornerElevation(3)) * kElevStep; // SW

    float groundY = 0.0f;
    if (cell.isStairs) {
        constexpr int kSteps = 3;
        float progress = 0.0f;
        if (cell.slope == SlopeDir::PlusX) progress = u;
        else if (cell.slope == SlopeDir::MinusX) progress = 1.0f - u;
        else if (cell.slope == SlopeDir::PlusZ) progress = v;
        else if (cell.slope == SlopeDir::MinusZ) progress = 1.0f - v;

        int stepIdx = std::clamp(static_cast<int>(progress * static_cast<float>(kSteps)), 0, kSteps - 1);
        float stepFrac = static_cast<float>(stepIdx + 1) / static_cast<float>(kSteps);
        float minH = std::min({y00, y10, y11, y01});
        float maxH = std::max({y00, y10, y11, y01});
        groundY = minH + (maxH - minH) * stepFrac;
    } else {
        // Continuous bilinear interpolation across the 4 corners
        float yNorth = y00 + (y10 - y00) * u;
        float ySouth = y01 + (y11 - y01) * u;
        groundY = yNorth + (ySouth - yNorth) * v;
    }

    if (cell.hasWater && cell.waterY > groundY) {
        return cell.waterY;
    }
    return groundY;
}

void RealmWorld::setTile(int gx, int gz, int16_t elevation, uint16_t topLayer, uint16_t cliffLayer) {
    if (gx < 0 || gz < 0 || gx >= kGridWidth || gz >= kGridDepth) return;
    TileCell& t = tiles_[static_cast<size_t>(gz) * kGridWidth + static_cast<size_t>(gx)];
    t.elevation = elevation;
    t.cornerOffset[0] = 0;
    t.cornerOffset[1] = 0;
    t.cornerOffset[2] = 0;
    t.cornerOffset[3] = 0;
    t.topLayer = topLayer;
    t.cliffLayer = cliffLayer;
    t.slope = SlopeDir::None;
    t.isStairs = false;
}

void RealmWorld::setTileSlope(int gx, int gz, SlopeDir dir, int16_t heightDelta, bool isStairs) {
    if (gx < 0 || gz < 0 || gx >= kGridWidth || gz >= kGridDepth) return;
    TileCell& t = tiles_[static_cast<size_t>(gz) * kGridWidth + static_cast<size_t>(gx)];
    t.slope = dir;
    t.isStairs = isStairs;
    int8_t d = static_cast<int8_t>(std::clamp<int>(heightDelta, -128, 127));
    t.cornerOffset[0] = 0;
    t.cornerOffset[1] = 0;
    t.cornerOffset[2] = 0;
    t.cornerOffset[3] = 0;
    if (dir == SlopeDir::PlusX) {
        t.cornerOffset[1] = d;
        t.cornerOffset[2] = d;
    } else if (dir == SlopeDir::MinusX) {
        t.cornerOffset[0] = d;
        t.cornerOffset[3] = d;
    } else if (dir == SlopeDir::PlusZ) {
        t.cornerOffset[2] = d;
        t.cornerOffset[3] = d;
    } else if (dir == SlopeDir::MinusZ) {
        t.cornerOffset[0] = d;
        t.cornerOffset[1] = d;
    } else if (dir == SlopeDir::CornerNW) {
        t.cornerOffset[0] = d;
    } else if (dir == SlopeDir::CornerNE) {
        t.cornerOffset[1] = d;
    } else if (dir == SlopeDir::CornerSE) {
        t.cornerOffset[2] = d;
    } else if (dir == SlopeDir::CornerSW) {
        t.cornerOffset[3] = d;
    }
}

void RealmWorld::setTileCorners(int gx, int gz, int16_t nw, int16_t ne, int16_t se, int16_t sw, uint16_t topLayer, uint16_t cliffLayer) {
    if (gx < 0 || gz < 0 || gx >= kGridWidth || gz >= kGridDepth) return;
    TileCell& t = tiles_[static_cast<size_t>(gz) * kGridWidth + static_cast<size_t>(gx)];
    t.setCornerElevations(nw, ne, se, sw);
    t.topLayer = topLayer;
    t.cliffLayer = cliffLayer;
    t.isStairs = false;
}

void RealmWorld::fillPlateau(int gx0, int gz0, int gx1, int gz1, int16_t elevation, uint16_t topLayer, uint16_t cliffLayer) {
    int xMin = std::max(0, std::min(gx0, gx1));
    int xMax = std::min(kGridWidth - 1, std::max(gx0, gx1));
    int zMin = std::max(0, std::min(gz0, gz1));
    int zMax = std::min(kGridDepth - 1, std::max(gz0, gz1));
    for (int gz = zMin; gz <= zMax; ++gz) {
        for (int gx = xMin; gx <= xMax; ++gx) {
            setTile(gx, gz, elevation, topLayer, cliffLayer);
        }
    }
}

void RealmWorld::addSlope(int gx0, int gz0, int gx1, int gz1, int16_t startElev, int16_t endElev, SlopeDir dir, uint16_t topLayer, uint16_t cliffLayer, bool isStairs) {
    int xMin = std::max(0, std::min(gx0, gx1));
    int xMax = std::min(kGridWidth - 1, std::max(gx0, gx1));
    int zMin = std::max(0, std::min(gz0, gz1));
    int zMax = std::min(kGridDepth - 1, std::max(gz0, gz1));

    int spanX = xMax - xMin + 1;
    int spanZ = zMax - zMin + 1;

    for (int gz = zMin; gz <= zMax; ++gz) {
        for (int gx = xMin; gx <= xMax; ++gx) {
            float t0 = 0.0f;
            float t1 = 1.0f;
            if (dir == SlopeDir::PlusX || dir == SlopeDir::MinusX) {
                t0 = static_cast<float>(gx - xMin) / static_cast<float>(spanX);
                t1 = static_cast<float>(gx - xMin + 1) / static_cast<float>(spanX);
                if (dir == SlopeDir::MinusX) {
                    float origT0 = t0;
                    t0 = 1.0f - t1;
                    t1 = 1.0f - origT0;
                }
            } else {
                t0 = static_cast<float>(gz - zMin) / static_cast<float>(spanZ);
                t1 = static_cast<float>(gz - zMin + 1) / static_cast<float>(spanZ);
                if (dir == SlopeDir::MinusZ) {
                    float origT0 = t0;
                    t0 = 1.0f - t1;
                    t1 = 1.0f - origT0;
                }
            }

            int16_t e0 = static_cast<int16_t>(std::round(startElev + (endElev - startElev) * t0));
            int16_t e1 = static_cast<int16_t>(std::round(startElev + (endElev - startElev) * t1));

            int16_t nw = e0, ne = e0, se = e0, sw = e0;
            if (dir == SlopeDir::PlusX) {
                nw = e0; sw = e0; ne = e1; se = e1;
            } else if (dir == SlopeDir::MinusX) {
                nw = e1; sw = e1; ne = e0; se = e0;
            } else if (dir == SlopeDir::PlusZ) {
                nw = e0; ne = e0; se = e1; sw = e1;
            } else if (dir == SlopeDir::MinusZ) {
                nw = e1; ne = e1; se = e0; sw = e0;
            }

            TileCell& cell = tiles_[static_cast<size_t>(gz) * kGridWidth + static_cast<size_t>(gx)];
            cell.setCornerElevations(nw, ne, se, sw);
            cell.slope = dir;
            cell.isStairs = isStairs;
            cell.topLayer = topLayer;
            cell.cliffLayer = cliffLayer;
        }
    }
}

void RealmWorld::addTerracedSlope(int gx0, int gz0, int gx1, int gz1, int16_t lowElev, int16_t highElev, SlopeDir dir, uint16_t pathLayer) {
    addSlope(gx0, gz0, gx1, gz1, lowElev, highElev, dir, pathLayer, TexLayer::CliffStone, false);
}

void RealmWorld::addStairs(int gx0, int gz0, int gx1, int gz1, int16_t lowElev, int16_t highElev, SlopeDir dir, uint16_t stairLayer) {
    addSlope(gx0, gz0, gx1, gz1, lowElev, highElev, dir, stairLayer, TexLayer::StoneBrick, true);
}

void RealmWorld::fillRiver(int gx0, int gz0, int gx1, int gz1, int16_t bedElev, float waterWorldY) {
    int xMin = std::max(0, std::min(gx0, gx1));
    int xMax = std::min(kGridWidth - 1, std::max(gx0, gx1));
    int zMin = std::max(0, std::min(gz0, gz1));
    int zMax = std::min(kGridDepth - 1, std::max(gz0, gz1));
    for (int gz = zMin; gz <= zMax; ++gz) {
        for (int gx = xMin; gx <= xMax; ++gx) {
            TileCell& t = tiles_[static_cast<size_t>(gz) * kGridWidth + static_cast<size_t>(gx)];
            t.elevation = bedElev;
            t.topLayer = TexLayer::SandShore;
            t.cliffLayer = TexLayer::CliffStone;
            t.hasWater = true;
            t.waterY = waterWorldY;
        }
    }
}

void RealmWorld::emitQuad(
    std::vector<WorldVertex>& dst,
    Vec3f p0, Vec3f p1, Vec3f p2, Vec3f p3,
    Vec3f normal, Vec3f tangent,
    Vec2f uv0, Vec2f uv1, Vec2f uv2, Vec2f uv3,
    float ao0, float ao1, float ao2, float ao3,
    uint32_t texLayer, float roughness, uint32_t flags,
    float extra0, float extra1, float extra2, float extra3
) {
    auto makeV = [&](Vec3f p, Vec2f uv, float ao, float extra) -> WorldVertex {
        WorldVertex v{};
        v.pos[0] = p.x; v.pos[1] = p.y; v.pos[2] = p.z;
        v.ao = ao;
        v.normal[0] = normal.x; v.normal[1] = normal.y; v.normal[2] = normal.z;
        v.roughness = roughness;
        v.tangent[0] = tangent.x; v.tangent[1] = tangent.y; v.tangent[2] = tangent.z;
        v.texLayer = texLayer;
        v.uv[0] = uv.x; v.uv[1] = uv.y;
        v.flags = flags;
        v.pad = extra;
        return v;
    };

    WorldVertex v0 = makeV(p0, uv0, ao0, extra0);
    WorldVertex v1 = makeV(p1, uv1, ao1, extra1);
    WorldVertex v2 = makeV(p2, uv2, ao2, extra2);
    WorldVertex v3 = makeV(p3, uv3, ao3, extra3);

    dst.push_back(v0);
    dst.push_back(v1);
    dst.push_back(v2);
    dst.push_back(v0);
    dst.push_back(v2);
    dst.push_back(v3);
}

void RealmWorld::emitTriangle(
    std::vector<WorldVertex>& dst,
    Vec3f p0, Vec3f p1, Vec3f p2,
    Vec3f normal, Vec3f tangent,
    Vec2f uv0, Vec2f uv1, Vec2f uv2,
    float ao, uint32_t texLayer, float roughness, uint32_t flags
) {
    auto makeV = [&](Vec3f p, Vec2f uv) -> WorldVertex {
        WorldVertex v{};
        v.pos[0] = p.x; v.pos[1] = p.y; v.pos[2] = p.z;
        v.ao = ao;
        v.normal[0] = normal.x; v.normal[1] = normal.y; v.normal[2] = normal.z;
        v.roughness = roughness;
        v.tangent[0] = tangent.x; v.tangent[1] = tangent.y; v.tangent[2] = tangent.z;
        v.texLayer = texLayer;
        v.uv[0] = uv.x; v.uv[1] = uv.y;
        v.flags = flags;
        return v;
    };
    dst.push_back(makeV(p0, uv0));
    dst.push_back(makeV(p1, uv1));
    dst.push_back(makeV(p2, uv2));
}

void RealmWorld::emitBox(
    std::vector<WorldVertex>& dst,
    Vec3f bMin, Vec3f bMax,
    uint32_t texLayer, float uvScale, float roughness, uint32_t flags
) {
    float dx = (bMax.x - bMin.x) * uvScale;
    float dy = (bMax.y - bMin.y) * uvScale;
    float dz = (bMax.z - bMin.z) * uvScale;

    // Front (-Z)
    emitQuad(dst,
        {bMin.x, bMin.y, bMin.z}, {bMax.x, bMin.y, bMin.z}, {bMax.x, bMax.y, bMin.z}, {bMin.x, bMax.y, bMin.z},
        {0.0f, 0.0f, -1.0f}, {1.0f, 0.0f, 0.0f},
        {0.0f, dy}, {dx, dy}, {dx, 0.0f}, {0.0f, 0.0f},
        0.72f, 0.72f, 0.96f, 0.96f, texLayer, roughness, flags);
    // Back (+Z)
    emitQuad(dst,
        {bMax.x, bMin.y, bMax.z}, {bMin.x, bMin.y, bMax.z}, {bMin.x, bMax.y, bMax.z}, {bMax.x, bMax.y, bMax.z},
        {0.0f, 0.0f, 1.0f}, {-1.0f, 0.0f, 0.0f},
        {0.0f, dy}, {dx, dy}, {dx, 0.0f}, {0.0f, 0.0f},
        0.72f, 0.72f, 0.96f, 0.96f, texLayer, roughness, flags);
    // Left (-X)
    emitQuad(dst,
        {bMin.x, bMin.y, bMax.z}, {bMin.x, bMin.y, bMin.z}, {bMin.x, bMax.y, bMin.z}, {bMin.x, bMax.y, bMax.z},
        {-1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, -1.0f},
        {0.0f, dy}, {dz, dy}, {dz, 0.0f}, {0.0f, 0.0f},
        0.72f, 0.72f, 0.96f, 0.96f, texLayer, roughness, flags);
    // Right (+X)
    emitQuad(dst,
        {bMax.x, bMin.y, bMin.z}, {bMax.x, bMin.y, bMax.z}, {bMax.x, bMax.y, bMax.z}, {bMax.x, bMax.y, bMin.z},
        {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f},
        {0.0f, dy}, {dz, dy}, {dz, 0.0f}, {0.0f, 0.0f},
        0.72f, 0.72f, 0.96f, 0.96f, texLayer, roughness, flags);
    // Top (+Y)
    emitQuad(dst,
        {bMin.x, bMax.y, bMin.z}, {bMax.x, bMax.y, bMin.z}, {bMax.x, bMax.y, bMax.z}, {bMin.x, bMax.y, bMax.z},
        {0.0f, 1.0f, 0.0f}, {1.0f, 0.0f, 0.0f},
        {0.0f, 0.0f}, {dx, 0.0f}, {dx, dz}, {0.0f, dz},
        0.95f, 0.95f, 0.95f, 0.95f, texLayer, roughness, flags);
}

void RealmWorld::emitOrientedBeam(
    std::vector<WorldVertex>& dst,
    Vec3f p0, Vec3f p1,
    float thickness, float depth,
    uint32_t texLayer, float uvScale, float roughness, uint32_t flags
) {
    Vec3f d{p1.x - p0.x, p1.y - p0.y, p1.z - p0.z};
    float len = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
    if (len < 0.001f) return;

    Vec3f fwd{d.x / len, d.y / len, d.z / len};
    Vec3f up{0.0f, 1.0f, 0.0f};
    if (std::abs(fwd.y) > 0.95f) {
        up = {1.0f, 0.0f, 0.0f};
    }

    Vec3f right{
        fwd.y * up.z - fwd.z * up.y,
        fwd.z * up.x - fwd.x * up.z,
        fwd.x * up.y - fwd.y * up.x
    };
    float rLen = std::sqrt(right.x * right.x + right.y * right.y + right.z * right.z);
    if (rLen < 0.001f) return;
    right.x /= rLen; right.y /= rLen; right.z /= rLen;

    up = {
        right.y * fwd.z - right.z * fwd.y,
        right.z * fwd.x - right.x * fwd.z,
        right.x * fwd.y - right.y * fwd.x
    };

    float halfT = thickness * 0.5f;
    float halfD = depth * 0.5f;

    Vec3f rOffset{right.x * halfT, right.y * halfT, right.z * halfT};
    Vec3f uOffset{up.x * halfD, up.y * halfD, up.z * halfD};

    Vec3f v0 = {p0.x - rOffset.x - uOffset.x, p0.y - rOffset.y - uOffset.y, p0.z - rOffset.z - uOffset.z};
    Vec3f v1 = {p0.x + rOffset.x - uOffset.x, p0.y + rOffset.y - uOffset.y, p0.z + rOffset.z - uOffset.z};
    Vec3f v2 = {p0.x + rOffset.x + uOffset.x, p0.y + rOffset.y + uOffset.y, p0.z + rOffset.z + uOffset.z};
    Vec3f v3 = {p0.x - rOffset.x + uOffset.x, p0.y - rOffset.y + uOffset.y, p0.z - rOffset.z + uOffset.z};

    Vec3f v4 = {p1.x - rOffset.x - uOffset.x, p1.y - rOffset.y - uOffset.y, p1.z - rOffset.z - uOffset.z};
    Vec3f v5 = {p1.x + rOffset.x - uOffset.x, p1.y + rOffset.y - uOffset.y, p1.z + rOffset.z - uOffset.z};
    Vec3f v6 = {p1.x + rOffset.x + uOffset.x, p1.y + rOffset.y + uOffset.y, p1.z + rOffset.z + uOffset.z};
    Vec3f v7 = {p1.x - rOffset.x + uOffset.x, p1.y - rOffset.y + uOffset.y, p1.z - rOffset.z + uOffset.z};

    float uLen = len * uvScale;
    float uThick = thickness * uvScale;
    float uDepth = depth * uvScale;

    // Top face (+up)
    emitQuad(dst, v3, v2, v6, v7, up, right,
        {0.0f, 0.0f}, {uThick, 0.0f}, {uThick, uLen}, {0.0f, uLen},
        0.88f, 0.88f, 0.88f, 0.88f, texLayer, roughness, flags);

    // Bottom face (-up)
    Vec3f negUp{-up.x, -up.y, -up.z};
    emitQuad(dst, v0, v4, v5, v1, negUp, right,
        {0.0f, 0.0f}, {0.0f, uLen}, {uThick, uLen}, {uThick, 0.0f},
        0.55f, 0.55f, 0.55f, 0.55f, texLayer, roughness, flags);

    // Right face (+right)
    emitQuad(dst, v1, v5, v6, v2, right, up,
        {0.0f, 0.0f}, {uLen, 0.0f}, {uLen, uDepth}, {0.0f, uDepth},
        0.75f, 0.75f, 0.75f, 0.75f, texLayer, roughness, flags);

    // Left face (-right)
    Vec3f negRight{-right.x, -right.y, -right.z};
    emitQuad(dst, v0, v3, v7, v4, negRight, up,
        {0.0f, 0.0f}, {0.0f, uDepth}, {uLen, uDepth}, {uLen, 0.0f},
        0.75f, 0.75f, 0.75f, 0.75f, texLayer, roughness, flags);

    // Start cap (-fwd)
    Vec3f negFwd{-fwd.x, -fwd.y, -fwd.z};
    emitQuad(dst, v0, v1, v2, v3, negFwd, right,
        {0.0f, 0.0f}, {uThick, 0.0f}, {uThick, uDepth}, {0.0f, uDepth},
        0.70f, 0.70f, 0.70f, 0.70f, texLayer, roughness, flags);

    // End cap (+fwd)
    emitQuad(dst, v4, v7, v6, v5, fwd, right,
        {0.0f, 0.0f}, {0.0f, uDepth}, {uThick, uDepth}, {uThick, 0.0f},
        0.70f, 0.70f, 0.70f, 0.70f, texLayer, roughness, flags);
}

void RealmWorld::addGabledHouse(
    Vec3f centerPos,
    Vec3f size,
    int roofAxis,
    uint16_t wallLayer,
    uint16_t roofLayer,
    bool hasChimney,
    bool hasLantern
) {
    float hx = size.x * 0.5f;
    float hz = size.z * 0.5f;
    float baseY = centerPos.y;
    float plinthH = 0.65f;
    float wallTopY = baseY + plinthH + size.y;
    float roofPeakY = wallTopY + size.y * 0.62f;
    float overhang = 0.45f;

    // 1. Stone foundation plinth
    emitBox(propVertices_,
        {centerPos.x - hx - 0.15f, baseY - 0.4f, centerPos.z - hz - 0.15f},
        {centerPos.x + hx + 0.15f, baseY + plinthH, centerPos.z + hz + 0.15f},
        TexLayer::StoneBrick, 0.45f, 0.82f);

    // 2. Main half-timbered / plaster cottage walls
    emitBox(propVertices_,
        {centerPos.x - hx, baseY + plinthH, centerPos.z - hz},
        {centerPos.x + hx, wallTopY, centerPos.z + hz},
        wallLayer, 0.35f, 0.75f);

    // 3. Heavy timber corner posts & horizontal lintel beam
    emitBox(propVertices_,
        {centerPos.x - hx - 0.08f, baseY + wallTopY - baseY - 0.32f, centerPos.z - hz - 0.08f},
        {centerPos.x + hx + 0.08f, wallTopY + 0.05f, centerPos.z + hz + 0.08f},
        TexLayer::WoodPlanks, 0.5f, 0.70f);

    // 4. True 3D Pitched Gabled Roof + Gable End Triangles
    if (roofAxis == 0) {
        // Ridge runs along X axis; slopes face -Z and +Z
        float rx0 = centerPos.x - hx - overhang;
        float rx1 = centerPos.x + hx + overhang;
        float rz0 = centerPos.z - hz - overhang;
        float rz1 = centerPos.z + hz + overhang;
        float eaveY = wallTopY - 0.18f;
        float uSpan = (rx1 - rx0) * 0.45f;
        float vSpan = std::sqrt((hz + overhang) * (hz + overhang) + (roofPeakY - eaveY) * (roofPeakY - eaveY)) * 0.45f;

        // Front roof slope (-Z)
        Vec3f nFront{0.0f, 0.78f, -0.62f};
        emitQuad(propVertices_,
            {rx0, eaveY, rz0}, {rx1, eaveY, rz0}, {rx1, roofPeakY, centerPos.z}, {rx0, roofPeakY, centerPos.z},
            nFront, {1.0f, 0.0f, 0.0f},
            {0.0f, vSpan}, {uSpan, vSpan}, {uSpan, 0.0f}, {0.0f, 0.0f},
            0.85f, 0.85f, 1.0f, 1.0f, roofLayer, 0.65f);

        // Back roof slope (+Z)
        Vec3f nBack{0.0f, 0.78f, 0.62f};
        emitQuad(propVertices_,
            {rx1, eaveY, rz1}, {rx0, eaveY, rz1}, {rx0, roofPeakY, centerPos.z}, {rx1, roofPeakY, centerPos.z},
            nBack, {-1.0f, 0.0f, 0.0f},
            {0.0f, vSpan}, {uSpan, vSpan}, {uSpan, 0.0f}, {0.0f, 0.0f},
            0.85f, 0.85f, 1.0f, 1.0f, roofLayer, 0.65f);

        // Gable triangular walls (-X and +X)
        emitTriangle(propVertices_,
            {centerPos.x - hx, wallTopY, centerPos.z + hz},
            {centerPos.x - hx, wallTopY, centerPos.z - hz},
            {centerPos.x - hx, roofPeakY - 0.05f, centerPos.z},
            {-1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, -1.0f},
            {0.0f, 1.0f}, {1.0f, 1.0f}, {0.5f, 0.0f}, 0.88f, wallLayer);

        emitTriangle(propVertices_,
            {centerPos.x + hx, wallTopY, centerPos.z - hz},
            {centerPos.x + hx, wallTopY, centerPos.z + hz},
            {centerPos.x + hx, roofPeakY - 0.05f, centerPos.z},
            {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f},
            {0.0f, 1.0f}, {1.0f, 1.0f}, {0.5f, 0.0f}, 0.88f, wallLayer);

        // Wooden ridge crest beam
        emitBox(propVertices_,
            {rx0 - 0.05f, roofPeakY - 0.14f, centerPos.z - 0.14f},
            {rx1 + 0.05f, roofPeakY + 0.12f, centerPos.z + 0.14f},
            TexLayer::WoodPlanks, 0.5f, 0.72f);
    } else {
        // Ridge runs along Z axis; slopes face -X and +X
        float rx0 = centerPos.x - hx - overhang;
        float rx1 = centerPos.x + hx + overhang;
        float rz0 = centerPos.z - hz - overhang;
        float rz1 = centerPos.z + hz + overhang;
        float eaveY = wallTopY - 0.18f;
        float uSpan = (rz1 - rz0) * 0.45f;
        float vSpan = std::sqrt((hx + overhang) * (hx + overhang) + (roofPeakY - eaveY) * (roofPeakY - eaveY)) * 0.45f;

        Vec3f nLeft{-0.62f, 0.78f, 0.0f};
        emitQuad(propVertices_,
            {rx0, eaveY, rz1}, {rx0, eaveY, rz0}, {centerPos.x, roofPeakY, rz0}, {centerPos.x, roofPeakY, rz1},
            nLeft, {0.0f, 0.0f, -1.0f},
            {0.0f, vSpan}, {uSpan, vSpan}, {uSpan, 0.0f}, {0.0f, 0.0f},
            0.85f, 0.85f, 1.0f, 1.0f, roofLayer, 0.65f);

        Vec3f nRight{0.62f, 0.78f, 0.0f};
        emitQuad(propVertices_,
            {rx1, eaveY, rz0}, {rx1, eaveY, rz1}, {centerPos.x, roofPeakY, rz1}, {centerPos.x, roofPeakY, rz0},
            nRight, {0.0f, 0.0f, 1.0f},
            {0.0f, vSpan}, {uSpan, vSpan}, {uSpan, 0.0f}, {0.0f, 0.0f},
            0.85f, 0.85f, 1.0f, 1.0f, roofLayer, 0.65f);

        emitTriangle(propVertices_,
            {centerPos.x - hx, wallTopY, centerPos.z - hz},
            {centerPos.x + hx, wallTopY, centerPos.z - hz},
            {centerPos.x, roofPeakY - 0.05f, centerPos.z - hz},
            {0.0f, 0.0f, -1.0f}, {1.0f, 0.0f, 0.0f},
            {0.0f, 1.0f}, {1.0f, 1.0f}, {0.5f, 0.0f}, 0.88f, wallLayer);

        emitTriangle(propVertices_,
            {centerPos.x + hx, wallTopY, centerPos.z + hz},
            {centerPos.x - hx, wallTopY, centerPos.z + hz},
            {centerPos.x, roofPeakY - 0.05f, centerPos.z + hz},
            {0.0f, 0.0f, 1.0f}, {-1.0f, 0.0f, 0.0f},
            {0.0f, 1.0f}, {1.0f, 1.0f}, {0.5f, 0.0f}, 0.88f, wallLayer);

        emitBox(propVertices_,
            {centerPos.x - 0.14f, roofPeakY - 0.14f, rz0 - 0.05f},
            {centerPos.x + 0.14f, roofPeakY + 0.12f, rz1 + 0.05f},
            TexLayer::WoodPlanks, 0.5f, 0.72f);
    }

    // 5. Stone Chimney Stack
    if (hasChimney) {
        emitBox(propVertices_,
            {centerPos.x + hx * 0.45f, wallTopY - 0.6f, centerPos.z - hz * 0.35f},
            {centerPos.x + hx * 0.45f + 0.95f, roofPeakY + 0.95f, centerPos.z - hz * 0.35f + 0.95f},
            TexLayer::StoneBrick, 0.55f, 0.85f);
    }

    // 6. Timber Door & Glowing Leaded Tavern Windows on Front (-Z) and Side Walls
    float doorW = 1.15f;
    float doorH = 2.05f;
    emitBox(propVertices_,
        {centerPos.x - doorW * 0.5f, baseY + plinthH, centerPos.z - hz - 0.08f},
        {centerPos.x + doorW * 0.5f, baseY + plinthH + doorH, centerPos.z - hz + 0.02f},
        TexLayer::WoodPlanks, 0.5f, 0.70f);

    // Warm emissive windows on front & sides
    float winY0 = baseY + plinthH + size.y * 0.36f;
    float winY1 = winY0 + 1.25f;
    if (hx >= 2.4f) {
        emitBox(propVertices_,
            {centerPos.x - hx * 0.68f, winY0, centerPos.z - hz - 0.07f},
            {centerPos.x - hx * 0.28f, winY1, centerPos.z - hz + 0.02f},
            TexLayer::WindowWarmGlow, 1.0f, 0.30f, 2u);
        emitBox(propVertices_,
            {centerPos.x + hx * 0.28f, winY0, centerPos.z - hz - 0.07f},
            {centerPos.x + hx * 0.68f, winY1, centerPos.z - hz + 0.02f},
            TexLayer::WindowWarmGlow, 1.0f, 0.30f, 2u);
    }
    emitBox(propVertices_,
        {centerPos.x - hx - 0.07f, winY0, centerPos.z - 0.6f},
        {centerPos.x - hx + 0.02f, winY1, centerPos.z + 0.6f},
        TexLayer::WindowWarmGlow, 1.0f, 0.30f, 2u);
    emitBox(propVertices_,
        {centerPos.x + hx - 0.02f, winY0, centerPos.z - 0.6f},
        {centerPos.x + hx + 0.07f, winY1, centerPos.z + 0.6f},
        TexLayer::WindowWarmGlow, 1.0f, 0.30f, 2u);

    // Register warm point light outside the cottage doorway/windows
    if (hasLantern) {
        addPointLight(
            {centerPos.x, winY0 + 0.4f, centerPos.z - hz - 1.4f},
            {1.0f, 0.66f, 0.26f},
            14.5f,
            1.85f
        );
    }
}

void RealmWorld::addWoodenBridge(Vec3f startPos, Vec3f endPos, float width) {
    constexpr int kSegments = 8;
    float halfW = width * 0.5f;
    for (int i = 0; i < kSegments; ++i) {
        float t0 = static_cast<float>(i) / static_cast<float>(kSegments);
        float t1 = static_cast<float>(i + 1) / static_cast<float>(kSegments);
        float arch0 = std::sin(t0 * 3.14159265f) * 0.65f;
        float arch1 = std::sin(t1 * 3.14159265f) * 0.65f;

        Vec3f a{
            startPos.x + (endPos.x - startPos.x) * t0,
            startPos.y + (endPos.y - startPos.y) * t0 + arch0,
            startPos.z + (endPos.z - startPos.z) * t0
        };
        Vec3f b{
            startPos.x + (endPos.x - startPos.x) * t1,
            startPos.y + (endPos.y - startPos.y) * t1 + arch1,
            startPos.z + (endPos.z - startPos.z) * t1
        };

        float minX = std::min(a.x, b.x);
        float maxX = std::max(a.x, b.x);
        float avgY = (a.y + b.y) * 0.5f;
        emitBox(propVertices_,
            {minX - 0.05f, avgY - 0.18f, a.z - halfW},
            {maxX + 0.05f, avgY + 0.08f, a.z + halfW},
            TexLayer::WoodPlanks, 0.65f, 0.72f);

        // Timber posts & handrails on both sides
        if (i % 2 == 0) {
            emitBox(propVertices_,
                {a.x - 0.12f, avgY - 0.8f, a.z - halfW - 0.12f},
                {a.x + 0.12f, avgY + 1.05f, a.z - halfW + 0.12f},
                TexLayer::WoodPlanks, 0.5f, 0.75f);
            emitBox(propVertices_,
                {a.x - 0.12f, avgY - 0.8f, a.z + halfW - 0.12f},
                {a.x + 0.12f, avgY + 1.05f, a.z + halfW + 0.12f},
                TexLayer::WoodPlanks, 0.5f, 0.75f);
        }
        // Top handrail beams
        emitBox(propVertices_,
            {minX, avgY + 0.82f, a.z - halfW - 0.08f},
            {maxX, avgY + 0.98f, a.z - halfW + 0.08f},
            TexLayer::WoodPlanks, 0.5f, 0.75f);
        emitBox(propVertices_,
            {minX, avgY + 0.82f, a.z + halfW - 0.08f},
            {maxX, avgY + 0.98f, a.z + halfW + 0.08f},
            TexLayer::WoodPlanks, 0.5f, 0.75f);
    }
}

void RealmWorld::addSuspensionBridge(Vec3f startPos, Vec3f endPos, float width, float mastHeight) {
    Vec3f span{endPos.x - startPos.x, endPos.y - startPos.y, endPos.z - startPos.z};
    float spanLen = std::sqrt(span.x * span.x + span.y * span.y + span.z * span.z);
    if (spanLen < 1.0f) return;

    Vec3f fwd{span.x / spanLen, span.y / spanLen, span.z / spanLen};
    Vec3f up{0.0f, 1.0f, 0.0f};
    Vec3f right{
        fwd.y * up.z - fwd.z * up.y,
        fwd.z * up.x - fwd.x * up.z,
        fwd.x * up.y - fwd.y * up.x
    };
    float rLen = std::sqrt(right.x * right.x + right.y * right.y + right.z * right.z);
    if (rLen > 0.001f) {
        right.x /= rLen; right.y /= rLen; right.z /= rLen;
    }

    float halfW = width * 0.5f;
    const int numPlanks = std::max(16, static_cast<int>(spanLen / 0.55f));

    // Longitudinal timber stringer beams under both edges of the deck
    float stringerThickness = 0.28f;
    float stringerDepth = 0.32f;
    Vec3f strLeft0{startPos.x - right.x * (halfW - 0.22f), startPos.y - 0.18f, startPos.z - right.z * (halfW - 0.22f)};
    Vec3f strLeft1{endPos.x - right.x * (halfW - 0.22f), endPos.y - 0.18f, endPos.z - right.z * (halfW - 0.22f)};
    emitOrientedBeam(propVertices_, strLeft0, strLeft1, stringerThickness, stringerDepth, TexLayer::WoodPlanks, 0.5f, 0.72f);

    Vec3f strRight0{startPos.x + right.x * (halfW - 0.22f), startPos.y - 0.18f, startPos.z + right.z * (halfW - 0.22f)};
    Vec3f strRight1{endPos.x + right.x * (halfW - 0.22f), endPos.y - 0.18f, endPos.z + right.z * (halfW - 0.22f)};
    emitOrientedBeam(propVertices_, strRight0, strRight1, stringerThickness, stringerDepth, TexLayer::WoodPlanks, 0.5f, 0.72f);

    // Center keel stringer
    Vec3f strMid0{startPos.x, startPos.y - 0.20f, startPos.z};
    Vec3f strMid1{endPos.x, endPos.y - 0.20f, endPos.z};
    emitOrientedBeam(propVertices_, strMid0, strMid1, 0.24f, stringerDepth, TexLayer::WoodPlanks, 0.5f, 0.72f);

    // 1. Deck Planks with realistic thickness and slight spacing
    for (int i = 0; i < numPlanks; ++i) {
        float t = (static_cast<float>(i) + 0.5f) / static_cast<float>(numPlanks);
        float plankCenterY = startPos.y + span.y * t;

        // Slight catenary sag
        float sag = 0.08f * 4.0f * t * (1.0f - t);
        plankCenterY -= sag;

        Vec3f plankCenter{
            startPos.x + span.x * t,
            plankCenterY,
            startPos.z + span.z * t
        };

        Vec3f pL{plankCenter.x - right.x * halfW, plankCenter.y, plankCenter.z - right.z * halfW};
        Vec3f pR{plankCenter.x + right.x * halfW, plankCenter.y, plankCenter.z + right.z * halfW};
        float plankThick = 0.45f;
        float plankDepth = 0.14f;
        emitOrientedBeam(propVertices_, pL, pR, plankThick, plankDepth, TexLayer::WoodPlanks, 0.65f, 0.74f);

        // Transverse cross-bearers under deck every 3rd plank extending slightly past stringers
        if (i % 3 == 0) {
            Vec3f cbL{plankCenter.x - right.x * (halfW + 0.35f), plankCenter.y - 0.24f, plankCenter.z - right.z * (halfW + 0.35f)};
            Vec3f cbR{plankCenter.x + right.x * (halfW + 0.35f), plankCenter.y - 0.24f, plankCenter.z + right.z * (halfW + 0.35f)};
            emitOrientedBeam(propVertices_, cbL, cbR, 0.32f, 0.26f, TexLayer::WoodPlanks, 0.5f, 0.72f);
        }
    }

    // 2. Abutment Towers / A-Frame Inclined Masts (Left Bank & Right Bank)
    auto emitMastTower = [&](float tBase, float leanSign) {
        float mastY = startPos.y + span.y * tBase;
        Vec3f baseCenter{startPos.x + span.x * tBase, mastY, startPos.z + span.z * tBase};

        float leanDist = 0.95f * leanSign;
        Vec3f leanOffset{fwd.x * leanDist, 0.0f, fwd.z * leanDist};

        // Left post (inclined backwards and slightly outwards)
        Vec3f postL0{baseCenter.x - right.x * (halfW + 0.15f), mastY - 0.45f, baseCenter.z - right.z * (halfW + 0.15f)};
        Vec3f postL1{baseCenter.x - right.x * (halfW + 0.45f) + leanOffset.x, mastY + mastHeight, baseCenter.z - right.z * (halfW + 0.45f) + leanOffset.z};
        emitOrientedBeam(propVertices_, postL0, postL1, 0.35f, 0.35f, TexLayer::WoodPlanks, 0.5f, 0.70f);

        // Right post
        Vec3f postR0{baseCenter.x + right.x * (halfW + 0.15f), mastY - 0.45f, baseCenter.z + right.z * (halfW + 0.15f)};
        Vec3f postR1{baseCenter.x + right.x * (halfW + 0.45f) + leanOffset.x, mastY + mastHeight, baseCenter.z + right.z * (halfW + 0.45f) + leanOffset.z};
        emitOrientedBeam(propVertices_, postR0, postR1, 0.35f, 0.35f, TexLayer::WoodPlanks, 0.5f, 0.70f);

        // Top cross beam connecting the mast heads
        emitOrientedBeam(propVertices_, postL1, postR1, 0.30f, 0.30f, TexLayer::WoodPlanks, 0.5f, 0.72f);

        // Mid cross beam
        Vec3f midL{postL0.x * 0.45f + postL1.x * 0.55f, postL0.y * 0.45f + postL1.y * 0.55f, postL0.z * 0.45f + postL1.z * 0.55f};
        Vec3f midR{postR0.x * 0.45f + postR1.x * 0.55f, postR0.y * 0.45f + postR1.y * 0.55f, postR0.z * 0.45f + postR1.z * 0.55f};
        emitOrientedBeam(propVertices_, midL, midR, 0.24f, 0.24f, TexLayer::WoodPlanks, 0.5f, 0.72f);

        // Cross 'X' brace between posts
        emitOrientedBeam(propVertices_, postL0, midR, 0.18f, 0.18f, TexLayer::WoodPlanks, 0.5f, 0.75f);
        emitOrientedBeam(propVertices_, postR0, midL, 0.18f, 0.18f, TexLayer::WoodPlanks, 0.5f, 0.75f);

        // Diagonal Suspension Timber Stays from mast heads down to mid-deck
        float stayTargetT = (leanSign < 0.0f) ? 0.38f : 0.62f;
        Vec3f stayDeckL{startPos.x + span.x * stayTargetT - right.x * halfW, startPos.y + span.y * stayTargetT + 0.1f, startPos.z + span.z * stayTargetT - right.z * halfW};
        Vec3f stayDeckR{startPos.x + span.x * stayTargetT + right.x * halfW, startPos.y + span.y * stayTargetT + 0.1f, startPos.z + span.z * stayTargetT + right.z * halfW};
        emitOrientedBeam(propVertices_, postL1, stayDeckL, 0.22f, 0.22f, TexLayer::WoodPlanks, 0.5f, 0.70f);
        emitOrientedBeam(propVertices_, postR1, stayDeckR, 0.22f, 0.22f, TexLayer::WoodPlanks, 0.5f, 0.70f);

        // Backstay anchor beam anchoring into bank rock behind tower
        float anchorT = (leanSign < 0.0f) ? -0.12f : 1.12f;
        Vec3f anchorL{startPos.x + span.x * anchorT - right.x * (halfW + 0.75f), mastY - 0.6f, startPos.z + span.z * anchorT - right.z * (halfW + 0.75f)};
        Vec3f anchorR{startPos.x + span.x * anchorT + right.x * (halfW + 0.75f), mastY - 0.6f, startPos.z + span.z * anchorT + right.z * (halfW + 0.75f)};
        emitOrientedBeam(propVertices_, postL1, anchorL, 0.24f, 0.24f, TexLayer::WoodPlanks, 0.5f, 0.72f);
        emitOrientedBeam(propVertices_, postR1, anchorR, 0.24f, 0.24f, TexLayer::WoodPlanks, 0.5f, 0.72f);
    };

    // Emit the two signature slanted mast towers
    emitMastTower(0.06f, -1.0f); // Left bank tower, leaning backwards (-fwd)
    emitMastTower(0.94f,  1.0f); // Right bank tower, leaning backwards (+fwd)

    // 3. Railings: Handrails, Mid-Rails, Uprights, and 'X'-Cross Braces along both sides
    const int numBays = 7;
    for (int b = 0; b < numBays; ++b) {
        float t0 = static_cast<float>(b) / static_cast<float>(numBays);
        float t1 = static_cast<float>(b + 1) / static_cast<float>(numBays);

        float y0 = startPos.y + span.y * t0 - 0.08f * 4.0f * t0 * (1.0f - t0);
        float y1 = startPos.y + span.y * t1 - 0.08f * 4.0f * t1 * (1.0f - t1);

        Vec3f pL0{startPos.x + span.x * t0 - right.x * halfW, y0, startPos.z + span.z * t0 - right.z * halfW};
        Vec3f pL1{startPos.x + span.x * t1 - right.x * halfW, y1, startPos.z + span.z * t1 - right.z * halfW};
        Vec3f pR0{startPos.x + span.x * t0 + right.x * halfW, y0, startPos.z + span.z * t0 + right.z * halfW};
        Vec3f pR1{startPos.x + span.x * t1 + right.x * halfW, y1, startPos.z + span.z * t1 + right.z * halfW};

        float railH = 0.58f;
        float midH = 0.29f;

        // Vertical picket posts at bay boundaries
        emitOrientedBeam(propVertices_, pL0, {pL0.x, pL0.y + railH + 0.08f, pL0.z}, 0.16f, 0.16f, TexLayer::WoodPlanks, 0.5f, 0.75f);
        emitOrientedBeam(propVertices_, pR0, {pR0.x, pR0.y + railH + 0.08f, pR0.z}, 0.16f, 0.16f, TexLayer::WoodPlanks, 0.5f, 0.75f);
        if (b == numBays - 1) {
            emitOrientedBeam(propVertices_, pL1, {pL1.x, pL1.y + railH + 0.08f, pL1.z}, 0.16f, 0.16f, TexLayer::WoodPlanks, 0.5f, 0.75f);
            emitOrientedBeam(propVertices_, pR1, {pR1.x, pR1.y + railH + 0.08f, pR1.z}, 0.16f, 0.16f, TexLayer::WoodPlanks, 0.5f, 0.75f);
        }

        // Top handrails
        Vec3f topL0{pL0.x, pL0.y + railH, pL0.z};
        Vec3f topL1{pL1.x, pL1.y + railH, pL1.z};
        Vec3f topR0{pR0.x, pR0.y + railH, pR0.z};
        Vec3f topR1{pR1.x, pR1.y + railH, pR1.z};
        emitOrientedBeam(propVertices_, topL0, topL1, 0.16f, 0.12f, TexLayer::WoodPlanks, 0.5f, 0.72f);
        emitOrientedBeam(propVertices_, topR0, topR1, 0.16f, 0.12f, TexLayer::WoodPlanks, 0.5f, 0.72f);

        // Mid safety rails
        Vec3f midL0{pL0.x, pL0.y + midH, pL0.z};
        Vec3f midL1{pL1.x, pL1.y + midH, pL1.z};
        Vec3f midR0{pR0.x, pR0.y + midH, pR0.z};
        Vec3f midR1{pR1.x, pR1.y + midH, pR1.z};
        emitOrientedBeam(propVertices_, midL0, midL1, 0.10f, 0.09f, TexLayer::WoodPlanks, 0.5f, 0.72f);
        emitOrientedBeam(propVertices_, midR0, midR1, 0.12f, 0.10f, TexLayer::WoodPlanks, 0.5f, 0.72f);

        // Bracing: Front railing uses alternating open diagonal truss to preserve sprite visibility; back railing uses full X braces
        if (b % 2 == 0) {
            emitOrientedBeam(propVertices_, pL0, topL1, 0.10f, 0.10f, TexLayer::WoodPlanks, 0.5f, 0.78f);
        } else {
            emitOrientedBeam(propVertices_, pL1, topL0, 0.10f, 0.10f, TexLayer::WoodPlanks, 0.5f, 0.78f);
        }
        emitOrientedBeam(propVertices_, pR0, topR1, 0.11f, 0.11f, TexLayer::WoodPlanks, 0.5f, 0.78f);
        emitOrientedBeam(propVertices_, pR1, topR0, 0.11f, 0.11f, TexLayer::WoodPlanks, 0.5f, 0.78f);
    }

    // 4. Under-Deck Trestle Struts Anchoring into Cliff Footing
    Vec3f abutFootL0{startPos.x - fwd.x * 0.8f, startPos.y - 1.8f, startPos.z - fwd.z * 0.8f};
    Vec3f deckSupport0{startPos.x + fwd.x * (spanLen * 0.22f), startPos.y - 0.25f, startPos.z + fwd.z * (spanLen * 0.22f)};
    emitOrientedBeam(propVertices_, abutFootL0, deckSupport0, 0.30f, 0.30f, TexLayer::WoodPlanks, 0.5f, 0.70f);

    Vec3f abutFootR0{endPos.x + fwd.x * 0.8f, endPos.y - 1.8f, endPos.z + fwd.z * 0.8f};
    Vec3f deckSupport1{endPos.x - fwd.x * (spanLen * 0.22f), endPos.y - 0.25f, endPos.z - fwd.z * (spanLen * 0.22f)};
    emitOrientedBeam(propVertices_, abutFootR0, deckSupport1, 0.30f, 0.30f, TexLayer::WoodPlanks, 0.5f, 0.70f);
}

void RealmWorld::addStreetLantern(Vec3f basePos) {
    // Stone plinth
    emitBox(propVertices_,
        {basePos.x - 0.32f, basePos.y, basePos.z - 0.32f},
        {basePos.x + 0.32f, basePos.y + 0.55f, basePos.z + 0.32f},
        TexLayer::StoneBrick, 0.6f, 0.80f);
    // Timber post
    emitBox(propVertices_,
        {basePos.x - 0.14f, basePos.y + 0.55f, basePos.z - 0.14f},
        {basePos.x + 0.14f, basePos.y + 3.15f, basePos.z + 0.14f},
        TexLayer::WoodPlanks, 0.6f, 0.75f);
    // Cross-arm
    emitBox(propVertices_,
        {basePos.x - 0.14f, basePos.y + 2.95f, basePos.z - 0.55f},
        {basePos.x + 0.14f, basePos.y + 3.18f, basePos.z + 0.25f},
        TexLayer::WoodPlanks, 0.6f, 0.75f);
    // Hanging glowing lantern housing
    emitBox(propVertices_,
        {basePos.x - 0.22f, basePos.y + 2.35f, basePos.z - 0.62f},
        {basePos.x + 0.22f, basePos.y + 2.88f, basePos.z - 0.18f},
        TexLayer::WindowWarmGlow, 1.0f, 0.25f, 2u);

    addPointLight(
        {basePos.x, basePos.y + 2.6f, basePos.z - 0.45f},
        {1.0f, 0.68f, 0.24f},
        13.5f,
        1.75f
    );
}

void RealmWorld::addRoadsideBeacon(Vec3f pos, Vec3f glowColor) {
    // Weathered timber post
    emitOrientedBeam(propVertices_, pos, {pos.x, pos.y + 2.5f, pos.z}, 0.22f, 0.22f, TexLayer::WoodPlanks, 0.6f, 0.75f);
    // Angled cantilever arm
    Vec3f armEnd{pos.x - 0.65f, pos.y + 2.45f, pos.z + 0.15f};
    emitOrientedBeam(propVertices_, {pos.x, pos.y + 2.3f, pos.z}, armEnd, 0.16f, 0.16f, TexLayer::WoodPlanks, 0.6f, 0.75f);
    // Diagonal brace
    emitOrientedBeam(propVertices_, {pos.x, pos.y + 1.8f, pos.z}, {pos.x - 0.35f, pos.y + 2.35f, pos.z + 0.08f}, 0.12f, 0.12f, TexLayer::WoodPlanks, 0.6f, 0.78f);
    // Hanging lantern housing
    emitBox(propVertices_,
        {armEnd.x - 0.18f, armEnd.y - 0.65f, armEnd.z - 0.18f},
        {armEnd.x + 0.18f, armEnd.y - 0.15f, armEnd.z + 0.18f},
        TexLayer::WindowWarmGlow, 1.0f, 0.20f, 2u /* emissive */);

    // Radiant emerald point light
    addPointLight({armEnd.x, armEnd.y - 0.40f, armEnd.z}, glowColor, 9.5f, 1.95f);
    // Billboard emissive sparkle
    spawnSprite({armEnd.x, armEnd.y - 0.40f, armEnd.z}, 0.65f, 0.65f, TexLayer::SpriteTorchFlame, SpriteFlags::Emissive, 0.0f);
}

void RealmWorld::addStoneArchway(Vec3f basePos, float width, float height, int axis) {
    float halfW = width * 0.5f;
    float pillarW = 0.95f;
    if (axis == 0) {
        emitBox(propVertices_,
            {basePos.x - halfW, basePos.y, basePos.z - 0.55f},
            {basePos.x - halfW + pillarW, basePos.y + height, basePos.z + 0.55f},
            TexLayer::StoneBrick, 0.45f, 0.82f);
        emitBox(propVertices_,
            {basePos.x + halfW - pillarW, basePos.y, basePos.z - 0.55f},
            {basePos.x + halfW, basePos.y + height, basePos.z + 0.55f},
            TexLayer::StoneBrick, 0.45f, 0.82f);
        emitBox(propVertices_,
            {basePos.x - halfW - 0.35f, basePos.y + height - 0.95f, basePos.z - 0.70f},
            {basePos.x + halfW + 0.35f, basePos.y + height + 0.25f, basePos.z + 0.70f},
            TexLayer::CliffStone, 0.45f, 0.85f);
    } else {
        emitBox(propVertices_,
            {basePos.x - 0.55f, basePos.y, basePos.z - halfW},
            {basePos.x + 0.55f, basePos.y + height, basePos.z - halfW + pillarW},
            TexLayer::StoneBrick, 0.45f, 0.82f);
        emitBox(propVertices_,
            {basePos.x - 0.55f, basePos.y, basePos.z + halfW - pillarW},
            {basePos.x + 0.55f, basePos.y + height, basePos.z + halfW},
            TexLayer::StoneBrick, 0.45f, 0.82f);
        emitBox(propVertices_,
            {basePos.x - 0.70f, basePos.y + height - 0.95f, basePos.z - halfW - 0.35f},
            {basePos.x + 0.70f, basePos.y + height + 0.25f, basePos.z + halfW + 0.35f},
            TexLayer::CliffStone, 0.45f, 0.85f);
    }
}

void RealmWorld::addCrateStack(Vec3f pos, int count) {
    count = std::clamp(count, 1, 4);
    // Base crate
    emitBox(propVertices_,
        {pos.x - 0.45f, pos.y, pos.z - 0.45f},
        {pos.x + 0.45f, pos.y + 0.85f, pos.z + 0.45f},
        TexLayer::WoodPlanks, 0.75f, 0.72f);
    if (count >= 2) {
        emitBox(propVertices_,
            {pos.x + 0.48f, pos.y, pos.z - 0.38f},
            {pos.x + 1.28f, pos.y + 0.75f, pos.z + 0.42f},
            TexLayer::WoodPlanks, 0.75f, 0.72f);
    }
    if (count >= 3) {
        emitBox(propVertices_,
            {pos.x - 0.28f, pos.y + 0.85f, pos.z - 0.35f},
            {pos.x + 0.52f, pos.y + 1.62f, pos.z + 0.45f},
            TexLayer::WoodPlanks, 0.75f, 0.72f);
    }
    if (count >= 4) {
        emitBox(propVertices_,
            {pos.x + 0.42f, pos.y + 0.75f, pos.z - 0.25f},
            {pos.x + 1.12f, pos.y + 1.45f, pos.z + 0.45f},
            TexLayer::WoodPlanks, 0.75f, 0.72f);
    }
}

void RealmWorld::addBarrel(Vec3f pos) {
    // Barrel body
    emitBox(propVertices_,
        {pos.x - 0.36f, pos.y, pos.z - 0.36f},
        {pos.x + 0.36f, pos.y + 0.95f, pos.z + 0.36f},
        TexLayer::WoodPlanks, 0.75f, 0.70f);
    // Upper iron hoop
    emitBox(propVertices_,
        {pos.x - 0.39f, pos.y + 0.68f, pos.z - 0.39f},
        {pos.x + 0.39f, pos.y + 0.76f, pos.z + 0.39f},
        TexLayer::CliffStone, 0.35f, 0.45f);
    // Lower iron hoop
    emitBox(propVertices_,
        {pos.x - 0.39f, pos.y + 0.18f, pos.z - 0.39f},
        {pos.x + 0.39f, pos.y + 0.26f, pos.z + 0.39f},
        TexLayer::CliffStone, 0.35f, 0.45f);
}

void RealmWorld::addFlowerBox(Vec3f pos, float length, int axis) {
    float halfL = length * 0.5f;
    if (axis == 0) {
        // Planter box along X
        emitBox(propVertices_,
            {pos.x - halfL, pos.y - 0.18f, pos.z - 0.22f},
            {pos.x + halfL, pos.y + 0.18f, pos.z + 0.22f},
            TexLayer::WoodPlanks, 0.65f, 0.75f);
        // Soil
        emitBox(propVertices_,
            {pos.x - halfL + 0.05f, pos.y + 0.14f, pos.z - 0.18f},
            {pos.x + halfL - 0.05f, pos.y + 0.22f, pos.z + 0.18f},
            TexLayer::DirtPath, 0.55f, 0.85f);
    } else {
        // Planter box along Z
        emitBox(propVertices_,
            {pos.x - 0.22f, pos.y - 0.18f, pos.z - halfL},
            {pos.x + 0.22f, pos.y + 0.18f, pos.z + halfL},
            TexLayer::WoodPlanks, 0.65f, 0.75f);
        // Soil
        emitBox(propVertices_,
            {pos.x - 0.18f, pos.y + 0.14f, pos.z - halfL + 0.05f},
            {pos.x + 0.18f, pos.y + 0.22f, pos.z + halfL - 0.05f},
            TexLayer::DirtPath, 0.55f, 0.85f);
    }
    // Colorful wildflower flora in planter
    spawnSprite({pos.x, pos.y + 0.42f, pos.z}, length * 0.9f, 0.65f, TexLayer::SpriteWildflower, SpriteFlags::WindSway, 0.5f);
}

void RealmWorld::addMarketStall(Vec3f pos, float width, float depth, uint16_t canopyLayer) {
    float halfW = width * 0.5f;
    float halfD = depth * 0.5f;
    // 4 timber corner posts
    float postH = 2.85f;
    float r = 0.09f;
    emitBox(propVertices_, {pos.x - halfW, pos.y, pos.z - halfD}, {pos.x - halfW + 2*r, pos.y + postH, pos.z - halfD + 2*r}, TexLayer::WoodPlanks, 0.5f, 0.75f);
    emitBox(propVertices_, {pos.x + halfW - 2*r, pos.y, pos.z - halfD}, {pos.x + halfW, pos.y + postH, pos.z - halfD + 2*r}, TexLayer::WoodPlanks, 0.5f, 0.75f);
    emitBox(propVertices_, {pos.x - halfW, pos.y, pos.z + halfD - 2*r}, {pos.x - halfW + 2*r, pos.y + postH, pos.z + halfD}, TexLayer::WoodPlanks, 0.5f, 0.75f);
    emitBox(propVertices_, {pos.x + halfW - 2*r, pos.y, pos.z + halfD - 2*r}, {pos.x + halfW, pos.y + postH, pos.z + halfD}, TexLayer::WoodPlanks, 0.5f, 0.75f);

    // Wooden display table counter
    float tableH = 0.95f;
    emitBox(propVertices_,
        {pos.x - halfW + 0.12f, pos.y, pos.z - halfD + 0.18f},
        {pos.x + halfW - 0.12f, pos.y + tableH, pos.z + halfD - 0.18f},
        TexLayer::WoodPlanks, 0.65f, 0.72f);

    // Slanted fabric canopy
    emitBox(propVertices_,
        {pos.x - halfW - 0.22f, pos.y + postH - 0.25f, pos.z - halfD - 0.22f},
        {pos.x + halfW + 0.22f, pos.y + postH + 0.15f, pos.z + halfD + 0.22f},
        canopyLayer, 0.5f, 0.65f);
}

void RealmWorld::emitPineTree3D(std::vector<WorldVertex>& dst, Vec3f basePos, float scale, float rotAngle) {
    constexpr float kPi = 3.14159265358979323846f;
    const float s = std::max(0.2f, scale);

    // 1. Tapered Octagonal Wooden Trunk with Root Buttresses
    const int trunkSides = 8;
    const float rBase = 0.30f * s;
    const float rTop  = 0.14f * s;
    const float yBot  = basePos.y - 0.40f * s;
    const float yTop  = basePos.y + 5.20f * s;

    for (int i = 0; i < trunkSides; ++i) {
        float a0 = rotAngle + static_cast<float>(i) * (2.0f * kPi / static_cast<float>(trunkSides));
        float a1 = rotAngle + static_cast<float>(i + 1) * (2.0f * kPi / static_cast<float>(trunkSides));
        float midA = (a0 + a1) * 0.5f;

        Vec3f p00{basePos.x + std::cos(a0) * rBase, yBot, basePos.z + std::sin(a0) * rBase};
        Vec3f p10{basePos.x + std::cos(a1) * rBase, yBot, basePos.z + std::sin(a1) * rBase};
        Vec3f p11{basePos.x + std::cos(a1) * rTop,  yTop, basePos.z + std::sin(a1) * rTop};
        Vec3f p01{basePos.x + std::cos(a0) * rTop,  yTop, basePos.z + std::sin(a0) * rTop};

        Vec3f norm{std::cos(midA), 0.05f, std::sin(midA)};
        float nLen = std::sqrt(norm.x * norm.x + norm.y * norm.y + norm.z * norm.z);
        norm = {norm.x / nLen, norm.y / nLen, norm.z / nLen};
        Vec3f tang{-std::sin(midA), 0.0f, std::cos(midA)};

        float u0 = static_cast<float>(i) * 0.5f;
        float u1 = static_cast<float>(i + 1) * 0.5f;
        float v0 = 0.0f;
        float v1 = (yTop - yBot) * 0.45f;

        emitQuad(dst,
            p00, p10, p11, p01,
            norm, tang,
            {u0, v0}, {u1, v0}, {u1, v1}, {u0, v1},
            0.55f, 0.55f, 0.88f, 0.88f,
            TexLayer::WoodPlanks, 0.85f, 0u);
    }

    // 4 Buttress Roots flaring into the ground
    for (int r = 0; r < 4; ++r) {
        float ra = rotAngle + static_cast<float>(r) * (kPi * 0.5f) + 0.2f;
        float rNext = ra + 0.35f;
        Vec3f rTip{basePos.x + std::cos(ra) * (rBase * 1.85f), yBot, basePos.z + std::sin(ra) * (rBase * 1.85f)};
        Vec3f rSide{basePos.x + std::cos(rNext) * rBase, yBot, basePos.z + std::sin(rNext) * rBase};
        Vec3f rPeak{basePos.x + std::cos(ra) * (rBase * 0.9f), basePos.y + 0.55f * s, basePos.z + std::sin(ra) * (rBase * 0.9f)};
        Vec3f rNorm{std::cos(ra), 0.45f, std::sin(ra)};
        float rnLen = std::sqrt(rNorm.x * rNorm.x + rNorm.y * rNorm.y + rNorm.z * rNorm.z);
        rNorm = {rNorm.x / rnLen, rNorm.y / rnLen, rNorm.z / rnLen};
        Vec3f rTang{-std::sin(ra), 0.0f, std::cos(ra)};
        emitTriangle(dst, rTip, rSide, rPeak, rNorm, rTang,
            {0.0f, 0.0f}, {0.5f, 0.0f}, {0.25f, 0.5f}, 0.50f, TexLayer::WoodPlanks, 0.85f, 0u);
    }

    // 2. Multi-Tiered Conical Foliage Skirts (Stacked with Jagged Needle Points)
    struct FoliageTier {
        float relBotY;
        float relTopY;
        float radius;
    };
    const FoliageTier tiers[5] = {
        { 1.45f * s, 2.80f * s, 2.10f * s },
        { 2.45f * s, 3.80f * s, 1.72f * s },
        { 3.40f * s, 4.65f * s, 1.35f * s },
        { 4.25f * s, 5.40f * s, 0.98f * s },
        { 5.00f * s, 6.15f * s, 0.62f * s }
    };

    const int kSegments = 12; // 12-segment faceted star cone with 6 jagged needle clusters
    for (int t = 0; t < 5; ++t) {
        const auto& tier = tiers[t];
        float tierRot = rotAngle + static_cast<float>(t) * 0.45f;
        float tierBotY = basePos.y + tier.relBotY;
        float tierTopY = basePos.y + tier.relTopY;

        for (int i = 0; i < kSegments; ++i) {
            float a0 = tierRot + static_cast<float>(i) * (2.0f * kPi / static_cast<float>(kSegments));
            float a1 = tierRot + static_cast<float>(i + 1) * (2.0f * kPi / static_cast<float>(kSegments));
            float midA = (a0 + a1) * 0.5f;

            // Alternating radius for serrated bough tips
            float r0 = ((i % 2) == 0) ? tier.radius * 1.15f : tier.radius * 0.85f;
            float r1 = (((i + 1) % 2) == 0) ? tier.radius * 1.15f : tier.radius * 0.85f;
            float rInner0 = tier.radius * 0.26f;
            float rInner1 = tier.radius * 0.26f;

            // Outer bottom skirt vertices
            Vec3f b0{basePos.x + std::cos(a0) * r0, tierBotY, basePos.z + std::sin(a0) * r0};
            Vec3f b1{basePos.x + std::cos(a1) * r1, tierBotY, basePos.z + std::sin(a1) * r1};

            // Inner top trunk attachment vertices
            Vec3f t1{basePos.x + std::cos(a1) * rInner1, tierTopY, basePos.z + std::sin(a1) * rInner1};
            Vec3f t0{basePos.x + std::cos(a0) * rInner0, tierTopY, basePos.z + std::sin(a0) * rInner0};

            // Spherical/Canopy Vertex Normals: Pointing outward and slightly upward
            Vec3f nMid{std::cos(midA), 0.48f, std::sin(midA)};
            float nLen = std::sqrt(nMid.x * nMid.x + nMid.y * nMid.y + nMid.z * nMid.z);
            nMid = {nMid.x / nLen, nMid.y / nLen, nMid.z / nLen};
            Vec3f tang{-std::sin(midA), 0.0f, std::cos(midA)};

            float u0 = static_cast<float>(i) * 0.5f;
            float u1 = static_cast<float>(i + 1) * 0.5f;

            // Sloping upper needle bough quad
            emitQuad(dst,
                b0, b1, t1, t0,
                nMid, tang,
                {u0, 1.0f}, {u1, 1.0f}, {u1, 0.0f}, {u0, 0.0f},
                0.82f, 0.82f, 0.96f, 0.96f,
                TexLayer::PineFoliage, 0.80f, 0u);

            // Underside shadow cap: Facing downward into ambient occlusion shadow
            Vec3f centerUnder{basePos.x, tierBotY + 0.10f * s, basePos.z};
            Vec3f underNorm{0.0f, -0.92f, 0.0f};
            Vec3f underTang{1.0f, 0.0f, 0.0f};
            emitTriangle(dst,
                centerUnder, b1, b0,
                underNorm, underTang,
                {0.5f, 0.5f}, {u1, 1.0f}, {u0, 1.0f},
                0.32f,
                TexLayer::PineFoliage, 0.85f, 0u);
        }
    }

    // 3. Sharp Crown Spire Needle (Apex Peak)
    float spireBotY = basePos.y + 5.75f * s;
    float peakY     = basePos.y + 7.15f * s;
    float spireR    = 0.42f * s;
    Vec3f apexPoint{basePos.x, peakY, basePos.z};

    for (int i = 0; i < kSegments; ++i) {
        float a0 = rotAngle + static_cast<float>(i) * (2.0f * kPi / static_cast<float>(kSegments));
        float a1 = rotAngle + static_cast<float>(i + 1) * (2.0f * kPi / static_cast<float>(kSegments));
        float midA = (a0 + a1) * 0.5f;

        float r0 = ((i % 2) == 0) ? spireR * 1.15f : spireR * 0.85f;
        float r1 = (((i + 1) % 2) == 0) ? spireR * 1.15f : spireR * 0.85f;

        Vec3f p0{basePos.x + std::cos(a0) * r0, spireBotY, basePos.z + std::sin(a0) * r0};
        Vec3f p1{basePos.x + std::cos(a1) * r1, spireBotY, basePos.z + std::sin(a1) * r1};

        Vec3f nApex{std::cos(midA) * 0.65f, 0.76f, std::sin(midA) * 0.65f};
        float naLen = std::sqrt(nApex.x * nApex.x + nApex.y * nApex.y + nApex.z * nApex.z);
        nApex = {nApex.x / naLen, nApex.y / naLen, nApex.z / naLen};
        Vec3f tang{-std::sin(midA), 0.0f, std::cos(midA)};

        float u0 = static_cast<float>(i) * 0.5f;
        float u1 = static_cast<float>(i + 1) * 0.5f;

        emitTriangle(dst,
            p0, p1, apexPoint,
            nApex, tang,
            {u0, 1.0f}, {u1, 1.0f}, {(u0 + u1) * 0.5f, 0.0f},
            0.94f, TexLayer::PineFoliage, 0.78f, 0u);
    }
}

void RealmWorld::addPineTree3D(Vec3f basePos, float scale, float rotAngle) {
    emitPineTree3D(propVertices_, basePos, scale, rotAngle);
}

void RealmWorld::addPineTree3DGrounded(float worldX, float worldZ, float scale, float rotAngle) {
    float groundY = getTerrainHeightAt(worldX, worldZ);
    addPineTree3D({worldX, groundY, worldZ}, scale, rotAngle);
}

void RealmWorld::emitOakTree3D(std::vector<WorldVertex>& dst, Vec3f basePos, float scale, float rotAngle, uint16_t foliageLayer) {
    constexpr float kPi = 3.14159265358979323846f;
    const float s = std::max(0.2f, scale);

    // 1. Sturdy, Gnarled Octagonal Trunk with Root Buttresses
    const int trunkSides = 8;
    const float rBase = 0.38f * s;
    const float rTop  = 0.22f * s;
    const float yBot  = basePos.y - 0.40f * s;
    const float yTop  = basePos.y + 3.80f * s;

    for (int i = 0; i < trunkSides; ++i) {
        float a0 = rotAngle + static_cast<float>(i) * (2.0f * kPi / static_cast<float>(trunkSides));
        float a1 = rotAngle + static_cast<float>(i + 1) * (2.0f * kPi / static_cast<float>(trunkSides));
        float midA = (a0 + a1) * 0.5f;

        Vec3f p00{basePos.x + std::cos(a0) * rBase, yBot, basePos.z + std::sin(a0) * rBase};
        Vec3f p10{basePos.x + std::cos(a1) * rBase, yBot, basePos.z + std::sin(a1) * rBase};
        Vec3f p11{basePos.x + std::cos(a1) * rTop,  yTop, basePos.z + std::sin(a1) * rTop};
        Vec3f p01{basePos.x + std::cos(a0) * rTop,  yTop, basePos.z + std::sin(a0) * rTop};

        Vec3f norm{std::cos(midA), 0.05f, std::sin(midA)};
        float nLen = std::sqrt(norm.x * norm.x + norm.y * norm.y + norm.z * norm.z);
        norm = {norm.x / nLen, norm.y / nLen, norm.z / nLen};
        Vec3f tang{-std::sin(midA), 0.0f, std::cos(midA)};

        float u0 = static_cast<float>(i) * 0.5f;
        float u1 = static_cast<float>(i + 1) * 0.5f;
        float v0 = 0.0f;
        float v1 = (yTop - yBot) * 0.45f;

        emitQuad(dst,
            p00, p10, p11, p01,
            norm, tang,
            {u0, v0}, {u1, v0}, {u1, v1}, {u0, v1},
            0.55f, 0.55f, 0.88f, 0.88f,
            TexLayer::WoodPlanks, 0.85f, 0u);
    }

    // 5 Buttress Roots flaring into the uneven ground
    for (int r = 0; r < 5; ++r) {
        float ra = rotAngle + static_cast<float>(r) * (2.0f * kPi / 5.0f) + 0.15f;
        float rNext = ra + 0.32f;
        Vec3f rTip{basePos.x + std::cos(ra) * (rBase * 1.95f), yBot, basePos.z + std::sin(ra) * (rBase * 1.95f)};
        Vec3f rSide{basePos.x + std::cos(rNext) * rBase, yBot, basePos.z + std::sin(rNext) * rBase};
        Vec3f rPeak{basePos.x + std::cos(ra) * (rBase * 0.95f), basePos.y + 0.65f * s, basePos.z + std::sin(ra) * (rBase * 0.95f)};
        Vec3f rNorm{std::cos(ra), 0.45f, std::sin(ra)};
        float rnLen = std::sqrt(rNorm.x * rNorm.x + rNorm.y * rNorm.y + rNorm.z * rNorm.z);
        rNorm = {rNorm.x / rnLen, rNorm.y / rnLen, rNorm.z / rnLen};
        Vec3f rTang{-std::sin(ra), 0.0f, std::cos(ra)};
        emitTriangle(dst, rTip, rSide, rPeak, rNorm, rTang,
            {0.0f, 0.0f}, {0.5f, 0.0f}, {0.25f, 0.5f}, 0.50f, TexLayer::WoodPlanks, 0.85f, 0u);
    }

    // Diagonal Spreading Boughs / Timber Limbs
    const Vec3f boughDests[4] = {
        {basePos.x + std::cos(rotAngle + 0.35f) * 1.50f * s, basePos.y + 4.10f * s, basePos.z + std::sin(rotAngle + 0.35f) * 1.50f * s},
        {basePos.x + std::cos(rotAngle + 1.90f) * 1.45f * s, basePos.y + 4.30f * s, basePos.z + std::sin(rotAngle + 1.90f) * 1.45f * s},
        {basePos.x + std::cos(rotAngle + 3.45f) * 1.55f * s, basePos.y + 3.95f * s, basePos.z + std::sin(rotAngle + 3.45f) * 1.55f * s},
        {basePos.x + std::cos(rotAngle + 5.00f) * 1.40f * s, basePos.y + 4.25f * s, basePos.z + std::sin(rotAngle + 5.00f) * 1.40f * s}
    };
    for (int b = 0; b < 4; ++b) {
        emitOrientedBeam(dst,
            {basePos.x, yTop - 0.25f * s, basePos.z}, boughDests[b],
            0.22f * s, 0.22f * s, TexLayer::WoodPlanks, 0.52f, 0.80f);
    }

    // 2. Multi-Lobed Billowing Deciduous Foliage Canopy (Cloud-like Diorama Clusters)
    struct FoliageLobe {
        Vec3f center;
        float rx;
        float ry;
        float rz;
    };
    const FoliageLobe lobes[5] = {
        // High central crown dome
        { {basePos.x, basePos.y + 5.25f * s, basePos.z}, 2.35f * s, 1.95f * s, 2.35f * s },
        // 4 billowing lateral canopy lobes
        { {basePos.x + std::cos(rotAngle + 0.35f) * 1.55f * s, basePos.y + 3.95f * s, basePos.z + std::sin(rotAngle + 0.35f) * 1.55f * s}, 1.75f * s, 1.45f * s, 1.75f * s },
        { {basePos.x + std::cos(rotAngle + 1.90f) * 1.50f * s, basePos.y + 4.20f * s, basePos.z + std::sin(rotAngle + 1.90f) * 1.50f * s}, 1.65f * s, 1.40f * s, 1.65f * s },
        { {basePos.x + std::cos(rotAngle + 3.45f) * 1.65f * s, basePos.y + 3.85f * s, basePos.z + std::sin(rotAngle + 3.45f) * 1.65f * s}, 1.80f * s, 1.50f * s, 1.80f * s },
        { {basePos.x + std::cos(rotAngle + 5.00f) * 1.45f * s, basePos.y + 4.30f * s, basePos.z + std::sin(rotAngle + 5.00f) * 1.45f * s}, 1.60f * s, 1.35f * s, 1.60f * s }
    };

    const int kLobeSegments = 10;
    for (int l = 0; l < 5; ++l) {
        const auto& lobe = lobes[l];
        float lobeRot = rotAngle + static_cast<float>(l) * 0.62f;

        // Two vertical rings + top apex cap
        // Ring 0: Bottom equator / skirt
        // Ring 1: Mid dome ring
        // Ring 2: Apex cap
        float ySkirt = lobe.center.y - lobe.ry * 0.30f;
        float yMid   = lobe.center.y + lobe.ry * 0.40f;
        float yApex  = lobe.center.y + lobe.ry * 1.05f;

        Vec3f apexPoint{lobe.center.x, yApex, lobe.center.z};
        Vec3f bottomCenter{lobe.center.x, ySkirt, lobe.center.z};

        for (int i = 0; i < kLobeSegments; ++i) {
            float a0 = lobeRot + static_cast<float>(i) * (2.0f * kPi / static_cast<float>(kLobeSegments));
            float a1 = lobeRot + static_cast<float>(i + 1) * (2.0f * kPi / static_cast<float>(kLobeSegments));
            float midA = (a0 + a1) * 0.5f;

            // Serrated broadleaf lobe contour
            float mod0 = ((i % 2) == 0) ? 1.08f : 0.92f;
            float mod1 = (((i + 1) % 2) == 0) ? 1.08f : 0.92f;

            float rSkirt0 = lobe.rx * 0.95f * mod0;
            float rSkirt1 = lobe.rx * 0.95f * mod1;
            float rMid0   = lobe.rx * 0.78f * mod0;
            float rMid1   = lobe.rx * 0.78f * mod1;

            Vec3f b0{lobe.center.x + std::cos(a0) * rSkirt0, ySkirt, lobe.center.z + std::sin(a0) * (rSkirt0 * (lobe.rz / lobe.rx))};
            Vec3f b1{lobe.center.x + std::cos(a1) * rSkirt1, ySkirt, lobe.center.z + std::sin(a1) * (rSkirt1 * (lobe.rz / lobe.rx))};

            Vec3f m0{lobe.center.x + std::cos(a0) * rMid0, yMid, lobe.center.z + std::sin(a0) * (rMid0 * (lobe.rz / lobe.rx))};
            Vec3f m1{lobe.center.x + std::cos(a1) * rMid1, yMid, lobe.center.z + std::sin(a1) * (rMid1 * (lobe.rz / lobe.rx))};

            // Spherical vertex normals for soft, puffy diorama lighting
            auto calcSphereNorm = [&](Vec3f pt) -> Vec3f {
                Vec3f d{pt.x - lobe.center.x, (pt.y - lobe.center.y) * 1.25f, pt.z - lobe.center.z};
                float len = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
                return (len > 0.0001f) ? Vec3f{d.x / len, d.y / len, d.z / len} : Vec3f{0.0f, 1.0f, 0.0f};
            };

            Vec3f nMidQuad = calcSphereNorm({(b0.x + b1.x + m0.x + m1.x) * 0.25f, (b0.y + b1.y + m0.y + m1.y) * 0.25f, (b0.z + b1.z + m0.z + m1.z) * 0.25f});
            Vec3f tangQuad{-std::sin(midA), 0.0f, std::cos(midA)};

            float u0 = static_cast<float>(i) * 0.5f;
            float u1 = static_cast<float>(i + 1) * 0.5f;

            // Lower dome quad
            emitQuad(dst,
                b0, b1, m1, m0,
                nMidQuad, tangQuad,
                {u0, 1.0f}, {u1, 1.0f}, {u1, 0.5f}, {u0, 0.5f},
                0.80f, 0.80f, 0.95f, 0.95f,
                foliageLayer, 0.82f, 0u);

            // Upper crown triangle to apex
            Vec3f nApexTri = calcSphereNorm({(m0.x + m1.x + apexPoint.x) / 3.0f, (m0.y + m1.y + apexPoint.y) / 3.0f, (m0.z + m1.z + apexPoint.z) / 3.0f});
            emitTriangle(dst,
                m0, m1, apexPoint,
                nApexTri, tangQuad,
                {u0, 0.5f}, {u1, 0.5f}, {(u0 + u1) * 0.5f, 0.0f},
                0.96f, foliageLayer, 0.78f, 0u);

            // Underside shadow cap: Facing downward into ambient occlusion shadow
            emitTriangle(dst,
                bottomCenter, b1, b0,
                {0.0f, -0.92f, 0.0f}, {1.0f, 0.0f, 0.0f},
                {0.5f, 0.5f}, {u1, 1.0f}, {u0, 1.0f},
                0.34f, foliageLayer, 0.85f, 0u);
        }
    }
}

void RealmWorld::addOakTree3D(Vec3f basePos, float scale, float rotAngle, uint16_t foliageLayer) {
    emitOakTree3D(propVertices_, basePos, scale, rotAngle, foliageLayer);
}

void RealmWorld::addOakTree3DGrounded(float worldX, float worldZ, float scale, float rotAngle, uint16_t foliageLayer) {
    float groundY = getTerrainHeightAt(worldX, worldZ);
    addOakTree3D({worldX, groundY, worldZ}, scale, rotAngle, foliageLayer);
}

void RealmWorld::emitSnowPineTree3D(std::vector<WorldVertex>& dst, Vec3f basePos, float scale, float rotAngle) {
    constexpr float kPi = 3.14159265358979323846f;
    const float s = std::max(0.2f, scale);

    // 1. Tapered Octagonal Wooden Trunk with Root Buttresses
    const int trunkSides = 8;
    const float rBase = 0.30f * s;
    const float rTop  = 0.14f * s;
    const float yBot  = basePos.y - 0.40f * s;
    const float yTop  = basePos.y + 5.20f * s;

    for (int i = 0; i < trunkSides; ++i) {
        float a0 = rotAngle + static_cast<float>(i) * (2.0f * kPi / static_cast<float>(trunkSides));
        float a1 = rotAngle + static_cast<float>(i + 1) * (2.0f * kPi / static_cast<float>(trunkSides));
        float midA = (a0 + a1) * 0.5f;

        Vec3f p00{basePos.x + std::cos(a0) * rBase, yBot, basePos.z + std::sin(a0) * rBase};
        Vec3f p10{basePos.x + std::cos(a1) * rBase, yBot, basePos.z + std::sin(a1) * rBase};
        Vec3f p11{basePos.x + std::cos(a1) * rTop,  yTop, basePos.z + std::sin(a1) * rTop};
        Vec3f p01{basePos.x + std::cos(a0) * rTop,  yTop, basePos.z + std::sin(a0) * rTop};

        Vec3f norm{std::cos(midA), 0.05f, std::sin(midA)};
        float nLen = std::sqrt(norm.x * norm.x + norm.y * norm.y + norm.z * norm.z);
        norm = {norm.x / nLen, norm.y / nLen, norm.z / nLen};
        Vec3f tang{-std::sin(midA), 0.0f, std::cos(midA)};

        float u0 = static_cast<float>(i) * 0.5f;
        float u1 = static_cast<float>(i + 1) * 0.5f;
        float v0 = 0.0f;
        float v1 = (yTop - yBot) * 0.45f;

        emitQuad(dst,
            p00, p10, p11, p01,
            norm, tang,
            {u0, v0}, {u1, v0}, {u1, v1}, {u0, v1},
            0.55f, 0.55f, 0.88f, 0.88f,
            TexLayer::WoodPlanks, 0.85f, 0u);
    }

    // 4 Buttress Roots flaring into the ground
    for (int r = 0; r < 4; ++r) {
        float ra = rotAngle + static_cast<float>(r) * (kPi * 0.5f) + 0.2f;
        float rNext = ra + 0.35f;
        Vec3f rTip{basePos.x + std::cos(ra) * (rBase * 1.85f), yBot, basePos.z + std::sin(ra) * (rBase * 1.85f)};
        Vec3f rSide{basePos.x + std::cos(rNext) * rBase, yBot, basePos.z + std::sin(rNext) * rBase};
        Vec3f rPeak{basePos.x + std::cos(ra) * (rBase * 0.9f), basePos.y + 0.55f * s, basePos.z + std::sin(ra) * (rBase * 0.9f)};
        Vec3f rNorm{std::cos(ra), 0.45f, std::sin(ra)};
        float rnLen = std::sqrt(rNorm.x * rNorm.x + rNorm.y * rNorm.y + rNorm.z * rNorm.z);
        rNorm = {rNorm.x / rnLen, rNorm.y / rnLen, rNorm.z / rnLen};
        Vec3f rTang{-std::sin(ra), 0.0f, std::cos(ra)};
        emitTriangle(dst, rTip, rSide, rPeak, rNorm, rTang,
            {0.0f, 0.0f}, {0.5f, 0.0f}, {0.25f, 0.5f}, 0.50f, TexLayer::WoodPlanks, 0.85f, 0u);
    }

    // 2. Multi-Tiered Conical Foliage Skirts Draped with Crisp Snow
    struct FoliageTier {
        float relBotY;
        float relTopY;
        float radius;
    };
    const FoliageTier tiers[5] = {
        { 1.45f * s, 2.80f * s, 2.10f * s },
        { 2.45f * s, 3.80f * s, 1.72f * s },
        { 3.40f * s, 4.65f * s, 1.35f * s },
        { 4.25f * s, 5.40f * s, 0.98f * s },
        { 5.00f * s, 6.15f * s, 0.62f * s }
    };

    const int kSegments = 12;
    for (int t = 0; t < 5; ++t) {
        const auto& tier = tiers[t];
        float tierRot = rotAngle + static_cast<float>(t) * 0.45f;
        float tierBotY = basePos.y + tier.relBotY;
        float tierTopY = basePos.y + tier.relTopY;

        for (int i = 0; i < kSegments; ++i) {
            float a0 = tierRot + static_cast<float>(i) * (2.0f * kPi / static_cast<float>(kSegments));
            float a1 = tierRot + static_cast<float>(i + 1) * (2.0f * kPi / static_cast<float>(kSegments));
            float midA = (a0 + a1) * 0.5f;

            float r0 = ((i % 2) == 0) ? tier.radius * 1.15f : tier.radius * 0.85f;
            float r1 = (((i + 1) % 2) == 0) ? tier.radius * 1.15f : tier.radius * 0.85f;
            float rInner0 = tier.radius * 0.26f;
            float rInner1 = tier.radius * 0.26f;

            Vec3f b0{basePos.x + std::cos(a0) * r0, tierBotY, basePos.z + std::sin(a0) * r0};
            Vec3f b1{basePos.x + std::cos(a1) * r1, tierBotY, basePos.z + std::sin(a1) * r1};
            Vec3f t1{basePos.x + std::cos(a1) * rInner1, tierTopY, basePos.z + std::sin(a1) * rInner1};
            Vec3f t0{basePos.x + std::cos(a0) * rInner0, tierTopY, basePos.z + std::sin(a0) * rInner0};

            Vec3f nMid{std::cos(midA), 0.48f, std::sin(midA)};
            float nLen = std::sqrt(nMid.x * nMid.x + nMid.y * nMid.y + nMid.z * nMid.z);
            nMid = {nMid.x / nLen, nMid.y / nLen, nMid.z / nLen};
            Vec3f tang{-std::sin(midA), 0.0f, std::cos(midA)};

            float u0 = static_cast<float>(i) * 0.5f;
            float u1 = static_cast<float>(i + 1) * 0.5f;

            // Sloping upper snow-draped needle bough quad
            emitQuad(dst,
                b0, b1, t1, t0,
                nMid, tang,
                {u0, 1.0f}, {u1, 1.0f}, {u1, 0.0f}, {u0, 0.0f},
                0.86f, 0.86f, 0.98f, 0.98f,
                TexLayer::SnowPineFoliage, 0.80f, 0u);

            // Underside shadow cap: Facing downward into ambient occlusion shadow
            Vec3f centerUnder{basePos.x, tierBotY + 0.10f * s, basePos.z};
            Vec3f underNorm{0.0f, -0.92f, 0.0f};
            Vec3f underTang{1.0f, 0.0f, 0.0f};
            emitTriangle(dst,
                centerUnder, b1, b0,
                underNorm, underTang,
                {0.5f, 0.5f}, {u1, 1.0f}, {u0, 1.0f},
                0.30f,
                TexLayer::PineFoliage, 0.85f, 0u);
        }
    }

    // 3. Sharp Crown Spire Needle (Apex Peak) with Snow Cap
    float spireBotY = basePos.y + 5.75f * s;
    float peakY     = basePos.y + 7.15f * s;
    float spireR    = 0.42f * s;
    Vec3f apexPoint{basePos.x, peakY, basePos.z};

    for (int i = 0; i < kSegments; ++i) {
        float a0 = rotAngle + static_cast<float>(i) * (2.0f * kPi / static_cast<float>(kSegments));
        float a1 = rotAngle + static_cast<float>(i + 1) * (2.0f * kPi / static_cast<float>(kSegments));
        float midA = (a0 + a1) * 0.5f;

        float r0 = ((i % 2) == 0) ? spireR * 1.15f : spireR * 0.85f;
        float r1 = (((i + 1) % 2) == 0) ? spireR * 1.15f : spireR * 0.85f;

        Vec3f p0{basePos.x + std::cos(a0) * r0, spireBotY, basePos.z + std::sin(a0) * r0};
        Vec3f p1{basePos.x + std::cos(a1) * r1, spireBotY, basePos.z + std::sin(a1) * r1};

        Vec3f nApex{std::cos(midA) * 0.65f, 0.76f, std::sin(midA) * 0.65f};
        float naLen = std::sqrt(nApex.x * nApex.x + nApex.y * nApex.y + nApex.z * nApex.z);
        nApex = {nApex.x / naLen, nApex.y / naLen, nApex.z / naLen};
        Vec3f tang{-std::sin(midA), 0.0f, std::cos(midA)};

        float u0 = static_cast<float>(i) * 0.5f;
        float u1 = static_cast<float>(i + 1) * 0.5f;

        emitTriangle(dst,
            p0, p1, apexPoint,
            nApex, tang,
            {u0, 1.0f}, {u1, 1.0f}, {(u0 + u1) * 0.5f, 0.0f},
            0.98f, TexLayer::SnowPineFoliage, 0.75f, 0u);
    }
}

void RealmWorld::addSnowPineTree3D(Vec3f basePos, float scale, float rotAngle) {
    emitSnowPineTree3D(propVertices_, basePos, scale, rotAngle);
}

void RealmWorld::addSnowPineTree3DGrounded(float worldX, float worldZ, float scale, float rotAngle) {
    float groundY = getTerrainHeightAt(worldX, worldZ);
    addSnowPineTree3D({worldX, groundY, worldZ}, scale, rotAngle);
}

void RealmWorld::emitAutumnTree3D(std::vector<WorldVertex>& dst, Vec3f basePos, float scale, float rotAngle) {
    emitOakTree3D(dst, basePos, scale, rotAngle, TexLayer::AutumnFoliage);
}

void RealmWorld::addAutumnTree3D(Vec3f basePos, float scale, float rotAngle) {
    emitAutumnTree3D(propVertices_, basePos, scale, rotAngle);
}

void RealmWorld::addAutumnTree3DGrounded(float worldX, float worldZ, float scale, float rotAngle) {
    float groundY = getTerrainHeightAt(worldX, worldZ);
    addAutumnTree3D({worldX, groundY, worldZ}, scale, rotAngle);
}

void RealmWorld::emitDeadTree3D(std::vector<WorldVertex>& dst, Vec3f basePos, float scale, float rotAngle) {
    constexpr float kPi = 3.14159265358979323846f;
    const float s = std::max(0.2f, scale);

    // Sturdy, Weathered Octagonal Trunk with Root Buttresses
    const int trunkSides = 8;
    const float rBase = 0.35f * s;
    const float rTop  = 0.18f * s;
    const float yBot  = basePos.y - 0.35f * s;
    const float yTop  = basePos.y + 3.40f * s;

    for (int i = 0; i < trunkSides; ++i) {
        float a0 = rotAngle + static_cast<float>(i) * (2.0f * kPi / static_cast<float>(trunkSides));
        float a1 = rotAngle + static_cast<float>(i + 1) * (2.0f * kPi / static_cast<float>(trunkSides));
        float midA = (a0 + a1) * 0.5f;

        Vec3f p00{basePos.x + std::cos(a0) * rBase, yBot, basePos.z + std::sin(a0) * rBase};
        Vec3f p10{basePos.x + std::cos(a1) * rBase, yBot, basePos.z + std::sin(a1) * rBase};
        Vec3f p11{basePos.x + std::cos(a1) * rTop,  yTop, basePos.z + std::sin(a1) * rTop};
        Vec3f p01{basePos.x + std::cos(a0) * rTop,  yTop, basePos.z + std::sin(a0) * rTop};

        Vec3f norm{std::cos(midA), 0.05f, std::sin(midA)};
        float nLen = std::sqrt(norm.x * norm.x + norm.y * norm.y + norm.z * norm.z);
        norm = {norm.x / nLen, norm.y / nLen, norm.z / nLen};
        Vec3f tang{-std::sin(midA), 0.0f, std::cos(midA)};

        emitQuad(dst,
            p00, p10, p11, p01,
            norm, tang,
            {0.0f, 0.0f}, {0.5f, 0.0f}, {0.5f, 1.0f}, {0.0f, 1.0f},
            0.48f, 0.48f, 0.72f, 0.72f,
            TexLayer::WoodPlanks, 0.90f, 0u);
    }

    // Buttress Roots
    for (int r = 0; r < 4; ++r) {
        float ra = rotAngle + static_cast<float>(r) * (kPi * 0.5f) + 0.1f;
        Vec3f rTip{basePos.x + std::cos(ra) * (rBase * 1.8f), yBot, basePos.z + std::sin(ra) * (rBase * 1.8f)};
        Vec3f rSide{basePos.x + std::cos(ra + 0.35f) * rBase, yBot, basePos.z + std::sin(ra + 0.35f) * rBase};
        Vec3f rPeak{basePos.x + std::cos(ra) * (rBase * 0.9f), basePos.y + 0.50f * s, basePos.z + std::sin(ra) * (rBase * 0.9f)};
        Vec3f rNorm{std::cos(ra), 0.45f, std::sin(ra)};
        float rnLen = std::sqrt(rNorm.x * rNorm.x + rNorm.y * rNorm.y + rNorm.z * rNorm.z);
        rNorm = {rNorm.x / rnLen, rNorm.y / rnLen, rNorm.z / rnLen};
        Vec3f rTang{-std::sin(ra), 0.0f, std::cos(ra)};
        emitTriangle(dst, rTip, rSide, rPeak, rNorm, rTang,
            {0.0f, 0.0f}, {0.5f, 0.0f}, {0.25f, 0.5f}, 0.42f, TexLayer::WoodPlanks, 0.90f, 0u);
    }

    // Gnarled twisting barren branch forks
    const Vec3f branchForks[4] = {
        {basePos.x + std::cos(rotAngle + 0.4f) * 1.85f * s, basePos.y + 4.90f * s, basePos.z + std::sin(rotAngle + 0.4f) * 1.85f * s},
        {basePos.x + std::cos(rotAngle + 2.1f) * 1.65f * s, basePos.y + 5.25f * s, basePos.z + std::sin(rotAngle + 2.1f) * 1.65f * s},
        {basePos.x + std::cos(rotAngle + 3.7f) * 1.95f * s, basePos.y + 4.70f * s, basePos.z + std::sin(rotAngle + 3.7f) * 1.95f * s},
        {basePos.x + std::cos(rotAngle + 5.2f) * 1.55f * s, basePos.y + 5.10f * s, basePos.z + std::sin(rotAngle + 5.2f) * 1.55f * s}
    };
    for (int b = 0; b < 4; ++b) {
        Vec3f start{basePos.x, yTop - 0.2f * s, basePos.z};
        Vec3f end = branchForks[b];
        emitOrientedBeam(dst, start, end, 0.20f * s, 0.16f * s, TexLayer::WoodPlanks, 0.45f, 0.82f);

        // Sub-branch twig
        float subA = rotAngle + static_cast<float>(b) * 1.57f + 0.6f;
        Vec3f subEnd{end.x + std::cos(subA) * 0.95f * s, end.y + 0.65f * s, end.z + std::sin(subA) * 0.95f * s};
        emitOrientedBeam(dst, end, subEnd, 0.12f * s, 0.08f * s, TexLayer::WoodPlanks, 0.42f, 0.82f);
    }
}

void RealmWorld::addDeadTree3D(Vec3f basePos, float scale, float rotAngle) {
    emitDeadTree3D(propVertices_, basePos, scale, rotAngle);
}

void RealmWorld::addDeadTree3DGrounded(float worldX, float worldZ, float scale, float rotAngle) {
    float groundY = getTerrainHeightAt(worldX, worldZ);
    addDeadTree3D({worldX, groundY, worldZ}, scale, rotAngle);
}

void RealmWorld::setTimeOfDay(float t) noexcept {
    timeOfDay_ = t;
    if (useDayNightCycle_) {
        atmosphere_ = AtmosphereConfig::evaluateDayNight(timeOfDay_);
    }
}

void RealmWorld::emitMossBoulder3D(std::vector<WorldVertex>& dst, Vec3f pos, float radiusX, float radiusY, float radiusZ, float rotAngle) {
    constexpr int kSlices = 10;
    constexpr int kRings  = 4;
    const float kPi = 3.1415926535f;

    for (int r = 0; r < kRings; ++r) {
        float phi0 = (static_cast<float>(r) / static_cast<float>(kRings)) * (kPi * 0.5f);
        float phi1 = (static_cast<float>(r + 1) / static_cast<float>(kRings)) * (kPi * 0.5f);

        float y0 = pos.y + std::sin(phi0) * radiusY;
        float y1 = pos.y + std::sin(phi1) * radiusY;
        float rad0 = std::cos(phi0);
        float rad1 = std::cos(phi1);

        float ao0 = (r == 0) ? 0.22f : (0.50f + 0.45f * (static_cast<float>(r) / static_cast<float>(kRings)));
        float ao1 = (0.50f + 0.45f * (static_cast<float>(r + 1) / static_cast<float>(kRings)));

        for (int i = 0; i < kSlices; ++i) {
            float theta0 = rotAngle + static_cast<float>(i) * (2.0f * kPi / static_cast<float>(kSlices));
            float theta1 = rotAngle + static_cast<float>(i + 1) * (2.0f * kPi / static_cast<float>(kSlices));

            Vec3f p00{pos.x + std::cos(theta0) * rad0 * radiusX, y0, pos.z + std::sin(theta0) * rad0 * radiusZ};
            Vec3f p10{pos.x + std::cos(theta1) * rad0 * radiusX, y0, pos.z + std::sin(theta1) * rad0 * radiusZ};
            Vec3f p11{pos.x + std::cos(theta1) * rad1 * radiusX, y1, pos.z + std::sin(theta1) * rad1 * radiusZ};
            Vec3f p01{pos.x + std::cos(theta0) * rad1 * radiusX, y1, pos.z + std::sin(theta0) * rad1 * radiusZ};

            float midTheta = (theta0 + theta1) * 0.5f;
            float midPhi   = (phi0 + phi1) * 0.5f;
            Vec3f norm{
                std::cos(midTheta) * std::cos(midPhi),
                std::sin(midPhi) * 1.25f,
                std::sin(midTheta) * std::cos(midPhi)
            };
            float nL = std::sqrt(norm.x * norm.x + norm.y * norm.y + norm.z * norm.z);
            if (nL > 1e-5f) { norm.x /= nL; norm.y /= nL; norm.z /= nL; }
            Vec3f tang{-std::sin(midTheta), 0.0f, std::cos(midTheta)};

            float u0 = static_cast<float>(i) / static_cast<float>(kSlices) * 2.0f;
            float u1 = static_cast<float>(i + 1) / static_cast<float>(kSlices) * 2.0f;
            float v0 = static_cast<float>(r) / static_cast<float>(kRings);
            float v1 = static_cast<float>(r + 1) / static_cast<float>(kRings);

            if (r == kRings - 1) {
                Vec3f apexPoint{pos.x, pos.y + radiusY, pos.z};
                emitTriangle(dst,
                    p00, p10, apexPoint,
                    {0.0f, 1.0f, 0.0f}, tang,
                    {u0, v0}, {u1, v0}, {(u0 + u1) * 0.5f, 1.0f},
                    0.96f, TexLayer::GrassMeadow, 0.85f, 0u);
            } else {
                emitQuad(dst,
                    p00, p10, p11, p01,
                    norm, tang,
                    {u0, v0}, {u1, v0}, {u1, v1}, {u0, v1},
                    ao0, ao0, ao1, ao1,
                    TexLayer::GrassMeadow, 0.85f, 0u);
            }
        }
    }
}

void RealmWorld::addMossBoulder3D(Vec3f pos, float radiusX, float radiusY, float radiusZ, float rotAngle) {
    emitMossBoulder3D(propVertices_, pos, radiusX, radiusY, radiusZ, rotAngle);
}

void RealmWorld::addMossBoulder3DGrounded(float worldX, float worldZ, float radiusX, float radiusY, float radiusZ, float rotAngle) {
    float groundY = getTerrainHeightAt(worldX, worldZ);
    addMossBoulder3D({worldX, groundY, worldZ}, radiusX, radiusY, radiusZ, rotAngle);
}

void RealmWorld::emitStarFern3D(std::vector<WorldVertex>& dst, Vec3f pos, float scale, float rotAngle) {
    constexpr int kFronds = 6;
    const float kPi = 3.1415926535f;

    for (int i = 0; i < kFronds; ++i) {
        float angle = rotAngle + static_cast<float>(i) * (2.0f * kPi / static_cast<float>(kFronds));
        float frondLen = (1.25f + ((i % 2 == 0) ? 0.35f : 0.0f)) * scale;
        float halfW    = 0.32f * scale;

        Vec3f fwd{std::cos(angle), 0.0f, std::sin(angle)};
        Vec3f right{-std::sin(angle) * halfW, 0.0f, std::cos(angle) * halfW};

        Vec3f b0{pos.x - right.x * 0.4f, pos.y, pos.z - right.z * 0.4f};
        Vec3f b1{pos.x + right.x * 0.4f, pos.y, pos.z + right.z * 0.4f};

        Vec3f m0{pos.x + fwd.x * frondLen * 0.55f - right.x, pos.y + 0.45f * scale, pos.z + fwd.z * frondLen * 0.55f - right.z};
        Vec3f m1{pos.x + fwd.x * frondLen * 0.55f + right.x, pos.y + 0.45f * scale, pos.z + fwd.z * frondLen * 0.55f + right.z};

        Vec3f tip{pos.x + fwd.x * frondLen, pos.y + 0.18f * scale, pos.z + fwd.z * frondLen};

        Vec3f norm{0.0f, 0.88f, 0.0f};
        Vec3f tang{fwd.x, 0.0f, fwd.z};

        emitQuad(dst,
            b0, b1, m1, m0,
            norm, tang,
            {0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 0.6f}, {0.0f, 0.6f},
            0.55f, 0.55f, 0.85f, 0.85f,
            TexLayer::PineFoliage, 0.80f, 0u);

        emitTriangle(dst,
            m0, m1, tip,
            norm, tang,
            {0.0f, 0.6f}, {1.0f, 0.6f}, {0.5f, 1.0f},
            0.92f,
            TexLayer::PineFoliage, 0.80f, 0u);
    }
}

void RealmWorld::addStarFern3D(Vec3f pos, float scale, float rotAngle) {
    emitStarFern3D(propVertices_, pos, scale, rotAngle);
}

void RealmWorld::addStarFern3DGrounded(float worldX, float worldZ, float scale, float rotAngle) {
    float groundY = getTerrainHeightAt(worldX, worldZ);
    addStarFern3D({worldX, groundY, worldZ}, scale, rotAngle);
}

void RealmWorld::addRusticFence(Vec3f startPos, Vec3f endPos, int postCount) {
    postCount = std::max(2, postCount);
    Vec3f delta{endPos.x - startPos.x, endPos.y - startPos.y, endPos.z - startPos.z};
    float len = std::sqrt(delta.x * delta.x + delta.z * delta.z);
    if (len < 1e-4f) return;

    // Posts
    for (int i = 0; i < postCount; ++i) {
        float t = static_cast<float>(i) / static_cast<float>(postCount - 1);
        Vec3f pt{startPos.x + delta.x * t, startPos.y + delta.y * t, startPos.z + delta.z * t};
        emitBox(propVertices_,
            {pt.x - 0.11f, pt.y, pt.z - 0.11f},
            {pt.x + 0.11f, pt.y + 1.25f, pt.z + 0.11f},
            TexLayer::TimberWall, 0.45f, 0.72f);
    }

    // Horizontal rails connecting adjacent posts
    for (int i = 0; i < postCount - 1; ++i) {
        float t0 = static_cast<float>(i) / static_cast<float>(postCount - 1);
        float t1 = static_cast<float>(i + 1) / static_cast<float>(postCount - 1);
        Vec3f pA{startPos.x + delta.x * t0, startPos.y + delta.y * t0, startPos.z + delta.z * t0};
        Vec3f pB{startPos.x + delta.x * t1, startPos.y + delta.y * t1, startPos.z + delta.z * t1};

        Vec3f mid{ (pA.x + pB.x) * 0.5f, (pA.y + pB.y) * 0.5f, (pA.z + pB.z) * 0.5f };
        float minX = std::min(pA.x, pB.x) - 0.06f;
        float maxX = std::max(pA.x, pB.x) + 0.06f;
        float minZ = std::min(pA.z, pB.z) - 0.06f;
        float maxZ = std::max(pA.z, pB.z) + 0.06f;

        // Upper rail
        emitBox(propVertices_,
            {minX, mid.y + 0.90f, minZ},
            {maxX, mid.y + 1.02f, maxZ},
            TexLayer::WoodPlanks, 0.55f, 0.70f);

        // Lower rail
        emitBox(propVertices_,
            {minX, mid.y + 0.45f, minZ},
            {maxX, mid.y + 0.57f, maxZ},
            TexLayer::WoodPlanks, 0.55f, 0.70f);
    }
}

void RealmWorld::addRusticFenceGrounded(float x0, float z0, float x1, float z1, int postCount) {
    float y0 = getTerrainHeightAt(x0, z0);
    float y1 = getTerrainHeightAt(x1, z1);
    addRusticFence({x0, y0, z0}, {x1, y1, z1}, postCount);
}

void RealmWorld::addRiverPebbleCluster(Vec3f pos, float radius, int count, uint32_t seed) {
    count = std::clamp(count, 1, 12);
    const float kPi = 3.1415926535f;
    auto localHash = [](uint32_t x) -> float {
        x = ((x >> 16) ^ x) * 0x45d9f3b;
        x = ((x >> 16) ^ x) * 0x45d9f3b;
        x = (x >> 16) ^ x;
        return static_cast<float>(x & 0x00FFFFFFu) / 16777216.0f;
    };

    for (int i = 0; i < count; ++i) {
        float a = static_cast<float>(i) * 2.39996f + localHash(seed + static_cast<uint32_t>(i * 11 + 1)) * 0.6f;
        float dist = (0.25f + 0.75f * localHash(seed + static_cast<uint32_t>(i * 23 + 3))) * radius;
        float px = pos.x + std::cos(a) * dist;
        float pz = pos.z + std::sin(a) * dist;
        float py = pos.y;
        float rx = 0.20f + 0.22f * localHash(seed + static_cast<uint32_t>(i * 37 + 5));
        float ry = 0.10f + 0.12f * localHash(seed + static_cast<uint32_t>(i * 49 + 7));
        float rz = 0.20f + 0.22f * localHash(seed + static_cast<uint32_t>(i * 61 + 9));
        float rot = localHash(seed + static_cast<uint32_t>(i * 73 + 11)) * 3.14f;

        constexpr int kSlices = 6;
        for (int s = 0; s < kSlices; ++s) {
            float t0 = rot + static_cast<float>(s) * (2.0f * kPi / static_cast<float>(kSlices));
            float t1 = rot + static_cast<float>(s + 1) * (2.0f * kPi / static_cast<float>(kSlices));
            Vec3f p0{px + std::cos(t0) * rx, py, pz + std::sin(t0) * rz};
            Vec3f p1{px + std::cos(t1) * rx, py, pz + std::sin(t1) * rz};
            Vec3f top{px, py + ry, pz};

            Vec3f norm{ (std::cos(t0) + std::cos(t1)) * 0.5f, 1.4f, (std::sin(t0) + std::sin(t1)) * 0.5f };
            float nL = std::sqrt(norm.x * norm.x + norm.y * norm.y + norm.z * norm.z);
            if (nL > 1e-4f) norm = {norm.x / nL, norm.y / nL, norm.z / nL};
            Vec3f tang{-std::sin(t0), 0.0f, std::cos(t0)};

            float u0 = static_cast<float>(s) / static_cast<float>(kSlices);
            float u1 = static_cast<float>(s + 1) / static_cast<float>(kSlices);
            emitTriangle(propVertices_,
                p0, p1, top,
                norm, tang,
                {u0, 1.0f}, {u1, 1.0f}, {(u0 + u1) * 0.5f, 0.0f},
                0.35f, TexLayer::CliffStone, 0.65f, 0u);
        }
    }
}

void RealmWorld::addRiverPebbleClusterGrounded(float worldX, float worldZ, float radius, int count, uint32_t seed) {
    float groundY = getTerrainHeightAt(worldX, worldZ);
    addRiverPebbleCluster({worldX, groundY, worldZ}, radius, count, seed);
}

void RealmWorld::addGrassTuft3D(Vec3f pos, float scale, float phaseOffset) {
    float s = std::max(0.2f, scale);
    spawnSprite(pos, 0.95f * s, 0.85f * s, TexLayer::SpriteGrassTuft, SpriteFlags::WindSway, phaseOffset);
}

void RealmWorld::addGrassTuft3DGrounded(float worldX, float worldZ, float scale, float phaseOffset) {
    float groundY = getTerrainHeightAt(worldX, worldZ);
    addGrassTuft3D({worldX, groundY, worldZ}, scale, phaseOffset);
}

void RealmWorld::addTallGrass3D(Vec3f pos, float scale, float phaseOffset) {
    float s = std::max(0.2f, scale);
    spawnSprite(pos, 1.05f * s, 1.35f * s, TexLayer::SpriteTallGrass, SpriteFlags::WindSway, phaseOffset);
}

void RealmWorld::addTallGrass3DGrounded(float worldX, float worldZ, float scale, float phaseOffset) {
    float groundY = getTerrainHeightAt(worldX, worldZ);
    addTallGrass3D({worldX, groundY, worldZ}, scale, phaseOffset);
}

void RealmWorld::addWildflower3D(Vec3f pos, uint16_t flowerLayer, float scale, float phaseOffset) {
    float s = std::max(0.2f, scale);
    spawnSprite(pos, 1.10f * s, 1.15f * s, flowerLayer, SpriteFlags::WindSway, phaseOffset);
}

void RealmWorld::addWildflower3DGrounded(float worldX, float worldZ, uint16_t flowerLayer, float scale, float phaseOffset) {
    float groundY = getTerrainHeightAt(worldX, worldZ);
    addWildflower3D({worldX, groundY, worldZ}, flowerLayer, scale, phaseOffset);
}

void RealmWorld::addCloverPatch3D(Vec3f pos, float scale, float phaseOffset) {
    float s = std::max(0.2f, scale);
    spawnSprite(pos, 1.15f * s, 0.65f * s, TexLayer::SpriteCloverPatch, SpriteFlags::WindSway, phaseOffset);
}

void RealmWorld::addCloverPatch3DGrounded(float worldX, float worldZ, float scale, float phaseOffset) {
    float groundY = getTerrainHeightAt(worldX, worldZ);
    addCloverPatch3D({worldX, groundY, worldZ}, scale, phaseOffset);
}

void RealmWorld::scatterMeadowVegetation(float density, float flowerRatio, uint32_t seed) {
    if (density <= 0.001f) return;

    for (int gz = 0; gz < kGridDepth; ++gz) {
        for (int gx = 0; gx < kGridWidth; ++gx) {
            const TileCell& cell = getTile(gx, gz);
            if (cell.hasWater) continue;

            bool isMeadow = (cell.topLayer == TexLayer::GrassMeadow);
            bool isHighland = (cell.topLayer == TexLayer::GrassHighland);
            if (!isMeadow && !isHighland) continue;

            float baseCount = (isMeadow ? 3.5f : 2.5f) * density;
            int tuftCount = static_cast<int>(baseCount);
            if (PixelCanvas::hash2D(gx, gz, seed + 11u) < (baseCount - static_cast<float>(tuftCount))) {
                tuftCount += 1;
            }

            float cellBaseX = (static_cast<float>(gx) - 32.0f) * kTileSize;
            float cellBaseZ = (static_cast<float>(gz) - 32.0f) * kTileSize;

            for (int t = 0; t < tuftCount; ++t) {
                float jx = (PixelCanvas::hash2D(gx * 17 + t, gz * 23, seed + 101u) - 0.5f) * (kTileSize * 0.88f);
                float jz = (PixelCanvas::hash2D(gx * 29 + t, gz * 31, seed + 202u) - 0.5f) * (kTileSize * 0.88f);

                float worldX = cellBaseX + jx;
                float worldZ = cellBaseZ + jz;
                float groundY = getTerrainHeightAt(worldX, worldZ);

                if (groundY < 1.38f) continue;

                float flowerNoise = PixelCanvas::fbm2D(worldX * 0.08f, worldZ * 0.08f, 2, seed + 333u);
                float cloverNoise = PixelCanvas::fbm2D(worldX * 0.14f, worldZ * 0.14f, 2, seed + 444u);
                float typeRoll = PixelCanvas::hash2D(gx * 47 + t, gz * 53, seed + 555u);
                float scale = 0.82f + PixelCanvas::hash2D(gx * 59 + t, gz * 61, seed + 666u) * 0.38f;
                float phase = PixelCanvas::hash2D(gx * 71 + t, gz * 73, seed + 777u) * 6.28f;

                if (typeRoll < flowerRatio) {
                    uint16_t flowerLayer = TexLayer::SpriteWildflowerRed;
                    if (flowerNoise < 0.35f) {
                        flowerLayer = TexLayer::SpriteWildflowerRed;
                    } else if (flowerNoise < 0.68f) {
                        flowerLayer = TexLayer::SpriteWildflowerYellow;
                    } else {
                        flowerLayer = TexLayer::SpriteWildflowerBlue;
                    }
                    addWildflower3D({worldX, groundY, worldZ}, flowerLayer, scale * 1.05f, phase);
                } else if (cloverNoise > 0.65f) {
                    addCloverPatch3D({worldX, groundY, worldZ}, scale * 0.95f, phase);
                } else if (typeRoll < flowerRatio + 0.24f) {
                    addTallGrass3D({worldX, groundY, worldZ}, scale * 1.15f, phase);
                } else {
                    addGrassTuft3D({worldX, groundY, worldZ}, scale, phase);
                }
            }
        }
    }
}

void RealmWorld::spawnSprite(Vec3f worldPos, float width, float height, uint16_t spriteLayer, uint32_t flags, float phaseOffset) {
    Sprite3DInstance inst{};
    inst.worldPos[0] = worldPos.x;
    inst.worldPos[1] = worldPos.y;
    inst.worldPos[2] = worldPos.z;
    inst.width = width;
    inst.height = height;
    inst.texLayer = spriteLayer;
    inst.flags = flags;
    inst.phaseOffset = phaseOffset;
    sprites_.push_back(inst);
}

void RealmWorld::spawnSpriteGrounded(float worldX, float worldZ, float width, float height, uint16_t spriteLayer, uint32_t flags, float phaseOffset) {
    float groundY = getTerrainHeightAt(worldX, worldZ);
    spawnSprite({worldX, groundY, worldZ}, width, height, spriteLayer, flags, phaseOffset);
}

void RealmWorld::addPointLight(Vec3f position, Vec3f color, float radius, float intensity) {
    if (pointLights_.size() >= kMaxPointLights) return;
    PointLight pl{};
    pl.position[0] = position.x;
    pl.position[1] = position.y;
    pl.position[2] = position.z;
    pl.radius = radius;
    pl.color[0] = color.x;
    pl.color[1] = color.y;
    pl.color[2] = color.z;
    pl.intensity = intensity;
    pointLights_.push_back(pl);
}

RealmStats RealmWorld::rebuildWorldGeometry() {
    worldVertices_.clear();
    worldVertices_.reserve(65536);

    if (!chunks_.empty()) {
        std::vector<Sprite3DInstance> activeSprites;
        uint32_t lod0Tris = 0;
        uint32_t lod1Tris = 0;
        uint32_t lod2Tris = 0;
        uint32_t lod3Tris = 0;
        uint32_t chunkPropTris = 0;
        uint32_t culledFlora = 0;

        auto isMicroFlora = [](uint32_t layer) noexcept {
            return (layer == TexLayer::SpriteGrassTuft ||
                    layer == TexLayer::SpriteTallGrass ||
                    layer == TexLayer::SpriteWildflowerRed ||
                    layer == TexLayer::SpriteWildflowerBlue ||
                    layer == TexLayer::SpriteWildflowerYellow ||
                    layer == TexLayer::SpriteCloverPatch ||
                    layer == TexLayer::SpriteWildflower);
        };

        // -------------------------------------------------------------
        // Pass A: LOD 0 Chunks (Full Micro-Detail Diorama + Casts Shadows)
        // -------------------------------------------------------------
        for (const auto& [coord, chunk] : chunks_) {
            if (chunk->currentLOD() != ChunkLOD::LOD0_MicroDiorama) continue;
            const RealmChunk* nN = getChunk(coord.cx, coord.cz - 1);
            const RealmChunk* nS = getChunk(coord.cx, coord.cz + 1);
            const RealmChunk* nW = getChunk(coord.cx - 1, coord.cz);
            const RealmChunk* nE = getChunk(coord.cx + 1, coord.cz);

            size_t vBefore = worldVertices_.size();
            chunk->buildLOD0Mesh(worldVertices_, nN, nS, nW, nE);
            lod0Tris += static_cast<uint32_t>((worldVertices_.size() - vBefore) / 3u);

            // LOD 0 Props
            worldVertices_.insert(worldVertices_.end(), chunk->propVertices().begin(), chunk->propVertices().end());
            chunkPropTris += static_cast<uint32_t>(chunk->propVertices().size() / 3u);

            // LOD 0 Sprites (Micro-flora within 70m of camera target, all macro sprites)
            for (const auto& sp : chunk->sprites()) {
                if (isMicroFlora(sp.texLayer)) {
                    float dx = sp.worldPos[0] - camera_.target.x;
                    float dz = sp.worldPos[2] - camera_.target.z;
                    if (dx * dx + dz * dz <= 4900.0f) {
                        activeSprites.push_back(sp);
                    } else {
                        culledFlora++;
                    }
                } else {
                    activeSprites.push_back(sp);
                }
            }
        }

        lod0VertexCount_ = static_cast<uint32_t>(worldVertices_.size());
        lod0SpriteCount_ = static_cast<uint32_t>(activeSprites.size());

        // -------------------------------------------------------------
        // Pass B: LOD 1 Chunks (Simplified 3.0m Greedy Meshed + Macro Props)
        // -------------------------------------------------------------
        for (const auto& [coord, chunk] : chunks_) {
            if (chunk->currentLOD() != ChunkLOD::LOD1_Simplified) continue;
            const RealmChunk* nN = getChunk(coord.cx, coord.cz - 1);
            const RealmChunk* nS = getChunk(coord.cx, coord.cz + 1);
            const RealmChunk* nW = getChunk(coord.cx - 1, coord.cz);
            const RealmChunk* nE = getChunk(coord.cx + 1, coord.cz);

            size_t vBefore = worldVertices_.size();
            chunk->buildGreedyMesh(ChunkLOD::LOD1_Simplified, worldVertices_, nN, nS, nW, nE);
            lod1Tris += static_cast<uint32_t>((worldVertices_.size() - vBefore) / 3u);

            worldVertices_.insert(worldVertices_.end(), chunk->propVertices().begin(), chunk->propVertices().end());
            chunkPropTris += static_cast<uint32_t>(chunk->propVertices().size() / 3u);

            // Strips micro-flora, keeps macro sprites
            for (const auto& sp : chunk->sprites()) {
                if (!isMicroFlora(sp.texLayer)) {
                    activeSprites.push_back(sp);
                } else {
                    culledFlora++;
                }
            }
        }

        // -------------------------------------------------------------
        // Pass C: LOD 2 Chunks (Macro 6.0m Greedy Meshed, No Props)
        // -------------------------------------------------------------
        for (const auto& [coord, chunk] : chunks_) {
            if (chunk->currentLOD() != ChunkLOD::LOD2_MacroBlocks) continue;
            const RealmChunk* nN = getChunk(coord.cx, coord.cz - 1);
            const RealmChunk* nS = getChunk(coord.cx, coord.cz + 1);
            const RealmChunk* nW = getChunk(coord.cx - 1, coord.cz);
            const RealmChunk* nE = getChunk(coord.cx + 1, coord.cz);

            size_t vBefore = worldVertices_.size();
            chunk->buildGreedyMesh(ChunkLOD::LOD2_MacroBlocks, worldVertices_, nN, nS, nW, nE);
            lod2Tris += static_cast<uint32_t>((worldVertices_.size() - vBefore) / 3u);
        }

        // -------------------------------------------------------------
        // Pass D: LOD 3 Chunks (Horizon 12.0m Greedy Meshed + Sawtooth Canopy)
        // -------------------------------------------------------------
        for (const auto& [coord, chunk] : chunks_) {
            if (chunk->currentLOD() != ChunkLOD::LOD3_HorizonRidge) continue;
            const RealmChunk* nN = getChunk(coord.cx, coord.cz - 1);
            const RealmChunk* nS = getChunk(coord.cx, coord.cz + 1);
            const RealmChunk* nW = getChunk(coord.cx - 1, coord.cz);
            const RealmChunk* nE = getChunk(coord.cx + 1, coord.cz);

            size_t vBefore = worldVertices_.size();
            chunk->buildGreedyMesh(ChunkLOD::LOD3_HorizonRidge, worldVertices_, nN, nS, nW, nE);
            lod3Tris += static_cast<uint32_t>((worldVertices_.size() - vBefore) / 3u);

            // Procedural low-poly sawtooth canopy ribbons for distant ridges
            const Vec3f cOrig = chunk->worldOrigin();
            const auto& l3 = chunk->lod3();
            for (int z = 0; z < RealmChunk::kLOD3Dim; ++z) {
                for (int x = 0; x < RealmChunk::kLOD3Dim; ++x) {
                    const auto& cell = l3[z * RealmChunk::kLOD3Dim + x];
                    if (cell.elevation >= 8 && cell.topLayer != TexLayer::CliffStone && !cell.hasWater) {
                        float bx0 = cOrig.x + static_cast<float>(x) * 12.0f;
                        float bz = cOrig.z + static_cast<float>(z) * 12.0f;
                        float by = static_cast<float>(cell.elevation) * RealmChunk::kElevStep;
                        for (int tooth = 0; tooth < 3; ++tooth) {
                            float tx0 = bx0 + static_cast<float>(tooth) * 4.0f;
                            float tx1 = tx0 + 4.0f;
                            float tMid = (tx0 + tx1) * 0.5f;
                            float tPeakY = by + 2.8f;

                            WorldVertex v0{}, v1{}, v2{};
                            v0.pos[0] = tx0; v0.pos[1] = by; v0.pos[2] = bz; v0.ao = 0.85f;
                            v0.normal[0] = 0.0f; v0.normal[1] = 0.0f; v0.normal[2] = -1.0f;
                            v0.texLayer = TexLayer::GrassHighland;

                            v1.pos[0] = tx1; v1.pos[1] = by; v1.pos[2] = bz; v1.ao = 0.85f;
                            v1.normal[0] = 0.0f; v1.normal[1] = 0.0f; v1.normal[2] = -1.0f;
                            v1.texLayer = TexLayer::GrassHighland;

                            v2.pos[0] = tMid; v2.pos[1] = tPeakY; v2.pos[2] = bz; v2.ao = 0.95f;
                            v2.normal[0] = 0.0f; v2.normal[1] = 0.0f; v2.normal[2] = -1.0f;
                            v2.texLayer = TexLayer::GrassHighland;

                            worldVertices_.push_back(v0);
                            worldVertices_.push_back(v1);
                            worldVertices_.push_back(v2);
                            lod3Tris += 1;
                        }
                    }
                }
            }
        }

        // Add any global scene props
        worldVertices_.insert(worldVertices_.end(), propVertices_.begin(), propVertices_.end());
        sprites_ = std::move(activeSprites);

        // Gather chunk point lights (up to kMaxPointLights)
        pointLights_.clear();
        for (const auto& [coord, chunk] : chunks_) {
            for (const auto& pl : chunk->pointLights()) {
                if (pointLights_.size() < kMaxPointLights) {
                    pointLights_.push_back(pl);
                }
            }
        }

        terrainTriCount_ = lod0Tris + lod1Tris + lod2Tris + lod3Tris;
        propTriCount_ = static_cast<uint32_t>(propVertices_.size() / 3u) + chunkPropTris;

        RealmStats stats{};
        stats.terrainTriangles = terrainTriCount_;
        stats.propTriangles = propTriCount_;
        stats.spriteCount = static_cast<uint32_t>(sprites_.size());
        stats.pointLightCount = static_cast<uint32_t>(pointLights_.size());
        stats.atlasGenTimeMs = atlas_.generationTimeMs;
        stats.activeChunks = static_cast<uint32_t>(chunks_.size());
        stats.lod0Triangles = lod0Tris;
        stats.lod1Triangles = lod1Tris;
        stats.lod2Triangles = lod2Tris;
        stats.lod3Triangles = lod3Tris;
        stats.culledSprites = culledFlora;
        return stats;
    }

    const float halfGridW = static_cast<float>(kGridWidth) * 0.5f;
    const float halfGridD = static_cast<float>(kGridDepth) * 0.5f;

    for (int gz = 0; gz < kGridDepth; ++gz) {
        for (int gx = 0; gx < kGridWidth; ++gx) {
            const TileCell& cell = getTile(gx, gz);
            float x0 = (static_cast<float>(gx) - halfGridW) * kTileSize;
            float x1 = x0 + kTileSize;
            float z0 = (static_cast<float>(gz) - halfGridD) * kTileSize;
            float z1 = z0 + kTileSize;

            float y00 = static_cast<float>(cell.getCornerElevation(0)) * kElevStep; // NW: (x0, z0)
            float y10 = static_cast<float>(cell.getCornerElevation(1)) * kElevStep; // NE: (x1, z0)
            float y11 = static_cast<float>(cell.getCornerElevation(2)) * kElevStep; // SE: (x1, z1)
            float y01 = static_cast<float>(cell.getCornerElevation(3)) * kElevStep; // SW: (x0, z1)
            bool isFlat = (y00 == y10 && y10 == y11 && y11 == y01);
            float yTop = y00;

            // Compute corner AO from higher neighbor elevations (gives rich HD-2D cliff-base contact shadows)
            auto neighborHigherAtCorner = [&](int dx, int dz, float cornerY) -> float {
                int nx = gx + dx;
                int nz = gz + dz;
                if (nx < 0 || nx >= kGridWidth || nz < 0 || nz >= kGridDepth) return 0.0f;
                float neY = static_cast<float>(getTile(nx, nz).elevation) * kElevStep;
                return (neY > cornerY) ? std::min(0.42f, (neY - cornerY) * 0.14f / kElevStep) : 0.0f;
            };
            float ao00 = 1.0f - std::max({neighborHigherAtCorner(-1, 0, y00), neighborHigherAtCorner(0, -1, y00), neighborHigherAtCorner(-1, -1, y00) * 0.7f});
            float ao10 = 1.0f - std::max({neighborHigherAtCorner( 1, 0, y10), neighborHigherAtCorner(0, -1, y10), neighborHigherAtCorner( 1, -1, y10) * 0.7f});
            float ao11 = 1.0f - std::max({neighborHigherAtCorner( 1, 0, y11), neighborHigherAtCorner(0,  1, y11), neighborHigherAtCorner( 1,  1, y11) * 0.7f});
            float ao01 = 1.0f - std::max({neighborHigherAtCorner(-1, 0, y01), neighborHigherAtCorner(0,  1, y01), neighborHigherAtCorner(-1,  1, y01) * 0.7f});

            // Watertight cliff wall & moss lip generator supporting smooth slopes and height transitions
            auto emitWallAndLip = [&](Vec3f pA_top, Vec3f pB_top, float yA_bot, float yB_bot, Vec3f norm, Vec3f tang) {
                float diffA = pA_top.y - yA_bot;
                float diffB = pB_top.y - yB_bot;
                if (diffA <= 0.005f && diffB <= 0.005f) return;

                Vec3f pA_bot{pA_top.x, yA_bot, pA_top.z};
                Vec3f pB_bot{pB_top.x, yB_bot, pB_top.z};

                float vSpanA = std::max(0.1f, diffA / kTileSize);
                float vSpanB = std::max(0.1f, diffB / kTileSize);

                if (diffA > 0.005f && diffB > 0.005f) {
                    emitQuad(worldVertices_,
                        pA_bot, pB_bot, pB_top, pA_top,
                        norm, tang,
                        {0.0f, vSpanA}, {1.0f, vSpanB}, {1.0f, 0.0f}, {0.0f, 0.0f},
                        0.48f, 0.48f, 0.95f, 0.95f, cell.cliffLayer, 0.85f, 0u);
                } else if (diffA > 0.005f) {
                    emitTriangle(worldVertices_,
                        pA_bot, pB_top, pA_top,
                        norm, tang,
                        {0.0f, vSpanA}, {1.0f, 0.0f}, {0.0f, 0.0f},
                        0.75f, cell.cliffLayer, 0.85f, 0u);
                } else {
                    emitTriangle(worldVertices_,
                        pA_top, pB_bot, pB_top,
                        norm, tang,
                        {0.0f, 0.0f}, {1.0f, vSpanB}, {1.0f, 0.0f},
                        0.75f, cell.cliffLayer, 0.85f, 0u);
                }

                if (cell.topLayer == TexLayer::GrassMeadow || cell.topLayer == TexLayer::GrassHighland) {
                    const float overhang = 0.16f;
                    const float droop    = 0.12f;
                    Vec3f pOut0{pA_top.x + norm.x * overhang, pA_top.y - droop, pA_top.z + norm.z * overhang};
                    Vec3f pOut1{pB_top.x + norm.x * overhang, pA_top.y - droop, pB_top.z + norm.z * overhang};

                    Vec3f lipNorm{norm.x * 0.707f, 0.707f, norm.z * 0.707f};
                    emitQuad(worldVertices_,
                        pA_top, pB_top, pOut1, pOut0,
                        lipNorm, tang,
                        {0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 0.35f}, {0.0f, 0.35f},
                        0.92f, 0.92f, 0.82f, 0.82f,
                        cell.topLayer, 0.82f, 0u);

                    Vec3f pCliff0{pA_top.x, pA_top.y - droop * 1.35f, pA_top.z};
                    Vec3f pCliff1{pB_top.x, pB_top.y - droop * 1.35f, pB_top.z};
                    Vec3f underNorm{0.0f, -0.92f, 0.0f};
                    emitQuad(worldVertices_,
                        pOut0, pOut1, pCliff1, pCliff0,
                        underNorm, tang,
                        {0.0f, 0.35f}, {1.0f, 0.35f}, {1.0f, 0.50f}, {0.0f, 0.50f},
                        0.22f, 0.22f, 0.16f, 0.16f,
                        cell.topLayer, 0.88f, 0u);
                }
            };

            // Neighbor bottom elevations at boundaries
            float botN_A = (gz > 0) ? static_cast<float>(getTile(gx, gz - 1).getCornerElevation(3)) * kElevStep : 0.0f; // SW of N
            float botN_B = (gz > 0) ? static_cast<float>(getTile(gx, gz - 1).getCornerElevation(2)) * kElevStep : 0.0f; // SE of N

            float botE_A = (gx < kGridWidth - 1) ? static_cast<float>(getTile(gx + 1, gz).getCornerElevation(0)) * kElevStep : 0.0f; // NW of E
            float botE_B = (gx < kGridWidth - 1) ? static_cast<float>(getTile(gx + 1, gz).getCornerElevation(3)) * kElevStep : 0.0f; // SW of E

            float botS_A = (gz < kGridDepth - 1) ? static_cast<float>(getTile(gx, gz + 1).getCornerElevation(1)) * kElevStep : 0.0f; // NE of S
            float botS_B = (gz < kGridDepth - 1) ? static_cast<float>(getTile(gx, gz + 1).getCornerElevation(0)) * kElevStep : 0.0f; // NW of S

            float botW_A = (gx > 0) ? static_cast<float>(getTile(gx - 1, gz).getCornerElevation(2)) * kElevStep : 0.0f; // SE of W
            float botW_B = (gx > 0) ? static_cast<float>(getTile(gx - 1, gz).getCornerElevation(1)) * kElevStep : 0.0f; // NE of W

            if (cell.isStairs) {
                // Stepped stone stairs
                constexpr int kSteps = 3;
                float minH = std::min({y00, y10, y11, y01});
                float maxH = std::max({y00, y10, y11, y01});
                for (int s = 0; s < kSteps; ++s) {
                    float f0 = static_cast<float>(s) / static_cast<float>(kSteps);
                    float f1 = static_cast<float>(s + 1) / static_cast<float>(kSteps);
                    float stepY = minH + (maxH - minH) * f1;
                    if (cell.slope == SlopeDir::PlusX || cell.slope == SlopeDir::MinusX) {
                        float sx0 = (cell.slope == SlopeDir::PlusX) ? (x0 + (x1 - x0) * f0) : (x1 - (x1 - x0) * f1);
                        float sx1 = (cell.slope == SlopeDir::PlusX) ? (x0 + (x1 - x0) * f1) : (x1 - (x1 - x0) * f0);
                        emitBox(worldVertices_, {sx0, minH - kElevStep * 0.5f, z0}, {sx1, stepY, z1}, cell.topLayer, 0.65f, 0.75f);
                    } else {
                        float sz0 = (cell.slope == SlopeDir::PlusZ) ? (z0 + (z1 - z0) * f0) : (z1 - (z1 - z0) * f1);
                        float sz1 = (cell.slope == SlopeDir::PlusZ) ? (z0 + (z1 - z0) * f1) : (z1 - (z1 - z0) * f0);
                        emitBox(worldVertices_, {x0, minH - kElevStep * 0.5f, sz0}, {x1, stepY, sz1}, cell.topLayer, 0.65f, 0.75f);
                    }
                }
                // Cardinal walls around stairs
                emitWallAndLip({x0, y00, z0}, {x1, y10, z0}, botN_A, botN_B, { 0.0f, 0.0f, -1.0f}, { 1.0f, 0.0f,  0.0f});
                emitWallAndLip({x1, y10, z0}, {x1, y11, z1}, botE_A, botE_B, { 1.0f, 0.0f,  0.0f}, { 0.0f, 0.0f,  1.0f});
                emitWallAndLip({x1, y11, z1}, {x0, y01, z1}, botS_A, botS_B, { 0.0f, 0.0f,  1.0f}, {-1.0f, 0.0f,  0.0f});
                emitWallAndLip({x0, y01, z1}, {x0, y00, z0}, botW_A, botW_B, {-1.0f, 0.0f,  0.0f}, { 0.0f, 0.0f, -1.0f});
            } else if (!isFlat) {
                // Smooth sloping terrain / ramp
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
                float tlen = std::sqrt(slopeTang.x * slopeTang.x + slopeTang.y * slopeTang.y + slopeTang.z * slopeTang.z);
                if (tlen > 1e-4f) {
                    slopeTang = {slopeTang.x / tlen, slopeTang.y / tlen, slopeTang.z / tlen};
                }

                emitTriangle(worldVertices_, p00, p10, p11, slopeNorm, slopeTang, {0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}, ao10, cell.topLayer, 0.80f, 0u);
                emitTriangle(worldVertices_, p00, p11, p01, slopeNorm, slopeTang, {0.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f}, ao01, cell.topLayer, 0.80f, 0u);

                // Cardinal cliff walls around the sloped tile
                emitWallAndLip(p00, p10, botN_A, botN_B, { 0.0f, 0.0f, -1.0f}, { 1.0f, 0.0f,  0.0f});
                emitWallAndLip(p10, p11, botE_A, botE_B, { 1.0f, 0.0f,  0.0f}, { 0.0f, 0.0f,  1.0f});
                emitWallAndLip(p11, p01, botS_A, botS_B, { 0.0f, 0.0f,  1.0f}, {-1.0f, 0.0f,  0.0f});
                emitWallAndLip(p01, p00, botW_A, botW_B, {-1.0f, 0.0f,  0.0f}, { 0.0f, 0.0f, -1.0f});
            } else {
                // Flat tile (with convex terrace corner chamfering)
                int16_t nN = (gz > 0) ? getTile(gx, gz - 1).elevation : int16_t(0);
                int16_t nS = (gz < kGridDepth - 1) ? getTile(gx, gz + 1).elevation : int16_t(0);
                int16_t nW = (gx > 0) ? getTile(gx - 1, gz).elevation : int16_t(0);
                int16_t nE = (gx < kGridWidth - 1) ? getTile(gx + 1, gz).elevation : int16_t(0);

                bool bevelNW = (nW < cell.elevation) && (nN < cell.elevation);
                bool bevelNE = (nE < cell.elevation) && (nN < cell.elevation);
                bool bevelSE = (nE < cell.elevation) && (nS < cell.elevation);
                bool bevelSW = (nW < cell.elevation) && (nS < cell.elevation);

                const float c = 0.36f; // Chamfer distance

                Vec3f pNW_N{bevelNW ? x0 + c : x0, yTop, z0};
                Vec3f pNE_N{bevelNE ? x1 - c : x1, yTop, z0};

                Vec3f pNE_E{x1, yTop, bevelNE ? z0 + c : z0};
                Vec3f pSE_E{x1, yTop, bevelSE ? z1 - c : z1};

                Vec3f pSE_S{bevelSE ? x1 - c : x1, yTop, z1};
                Vec3f pSW_S{bevelSW ? x0 + c : x0, yTop, z1};

                Vec3f pSW_W{x0, yTop, bevelSW ? z1 - c : z1};
                Vec3f pNW_W{x0, yTop, bevelNW ? z0 + c : z0};

                if (!bevelNW && !bevelNE && !bevelSE && !bevelSW) {
                    emitQuad(worldVertices_,
                        {x0, yTop, z0}, {x1, yTop, z0}, {x1, yTop, z1}, {x0, yTop, z1},
                        {0.0f, 1.0f, 0.0f}, {1.0f, 0.0f, 0.0f},
                        {0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f},
                        ao00, ao10, ao11, ao01, cell.topLayer, 0.80f, 0u);
                } else {
                    Vec3f center{(x0 + x1) * 0.5f, yTop, (z0 + z1) * 0.5f};
                    std::vector<Vec3f> poly;
                    poly.push_back(pNW_N);
                    poly.push_back(pNE_N);
                    if (bevelNE) poly.push_back(pNE_E);
                    poly.push_back(pSE_E);
                    if (bevelSE) poly.push_back(pSE_S);
                    poly.push_back(pSW_S);
                    if (bevelSW) poly.push_back(pSW_W);
                    if (bevelNW) poly.push_back(pNW_W);

                    float aoMid = (ao00 + ao10 + ao11 + ao01) * 0.25f;
                    for (size_t k = 0; k < poly.size(); ++k) {
                        Vec3f v0 = poly[k];
                        Vec3f v1 = poly[(k + 1) % poly.size()];
                        Vec2f uv0{(v0.x - x0) / kTileSize, (v0.z - z0) / kTileSize};
                        Vec2f uv1{(v1.x - x0) / kTileSize, (v1.z - z0) / kTileSize};
                        Vec2f uvMid{0.5f, 0.5f};

                        emitTriangle(worldVertices_,
                            center, v0, v1,
                            {0.0f, 1.0f, 0.0f}, {1.0f, 0.0f, 0.0f},
                            uvMid, uv0, uv1,
                            aoMid, cell.topLayer, 0.80f, 0u);
                    }
                }

                // Cardinal cliff walls
                emitWallAndLip(pNW_N, pNE_N, botN_A, botN_B, { 0.0f, 0.0f, -1.0f}, { 1.0f, 0.0f,  0.0f});
                emitWallAndLip(pNE_E, pSE_E, botE_A, botE_B, { 1.0f, 0.0f,  0.0f}, { 0.0f, 0.0f,  1.0f});
                emitWallAndLip(pSE_S, pSW_S, botS_A, botS_B, { 0.0f, 0.0f,  1.0f}, {-1.0f, 0.0f,  0.0f});
                emitWallAndLip(pSW_W, pNW_W, botW_A, botW_B, {-1.0f, 0.0f,  0.0f}, { 0.0f, 0.0f, -1.0f});

                // Diagonal beveled corners
                const float invSqrt2 = 0.70710678f;
                if (bevelNW) {
                    float nDiag = std::max(botW_A, botN_A);
                    emitWallAndLip(pNW_W, pNW_N, nDiag, nDiag, {-invSqrt2, 0.0f, -invSqrt2}, {invSqrt2, 0.0f, -invSqrt2});
                }
                if (bevelNE) {
                    float nDiag = std::max(botE_A, botN_B);
                    emitWallAndLip(pNE_N, pNE_E, nDiag, nDiag, {invSqrt2, 0.0f, -invSqrt2}, {invSqrt2, 0.0f, invSqrt2});
                }
                if (bevelSE) {
                    float nDiag = std::max(botE_B, botS_A);
                    emitWallAndLip(pSE_E, pSE_S, nDiag, nDiag, {invSqrt2, 0.0f, invSqrt2}, {-invSqrt2, 0.0f, invSqrt2});
                }
                if (bevelSW) {
                    float nDiag = std::max(botW_B, botS_B);
                    emitWallAndLip(pSW_S, pSW_W, nDiag, nDiag, {-invSqrt2, 0.0f, invSqrt2}, {-invSqrt2, 0.0f, -invSqrt2});
                }
            }

            // 3. Animated Water Surface Plane with Shoreline Proximity & Surf Detection
            if (cell.hasWater) {
                float wy = cell.waterY;
                auto evalShoreProximity = [&](int cx, int cz) -> float {
                    for (int dz = -1; dz <= 0; ++dz) {
                        for (int dx = -1; dx <= 0; ++dx) {
                            int nx = cx + dx;
                            int nz = cz + dz;
                            if (nx >= 0 && nx < kGridWidth && nz >= 0 && nz < kGridDepth) {
                                const auto& neighbor = getTile(nx, nz);
                                if (!neighbor.hasWater || (neighbor.elevation * kElevStep) > (wy - 0.05f)) {
                                    return 1.0f; // Directly touching solid rock / coast
                                }
                            }
                        }
                    }
                    for (int dz = -2; dz <= 1; ++dz) {
                        for (int dx = -2; dx <= 1; ++dx) {
                            int nx = cx + dx;
                            int nz = cz + dz;
                            if (nx >= 0 && nx < kGridWidth && nz >= 0 && nz < kGridDepth) {
                                const auto& neighbor = getTile(nx, nz);
                                if (!neighbor.hasWater || (neighbor.elevation * kElevStep) > (wy - 0.05f)) {
                                    return 0.45f; // Coastal shallows
                                }
                            }
                        }
                    }
                    return 0.0f; // Open deep ocean
                };

                float shore0 = evalShoreProximity(gx, gz);
                float shore1 = evalShoreProximity(gx + 1, gz);
                float shore2 = evalShoreProximity(gx + 1, gz + 1);
                float shore3 = evalShoreProximity(gx, gz + 1);

                emitQuad(worldVertices_,
                    {x0, wy, z0}, {x1, wy, z0}, {x1, wy, z1}, {x0, wy, z1},
                    {0.0f, 1.0f, 0.0f}, {1.0f, 0.0f, 0.0f},
                    {x0, z0}, {x1, z0}, {x1, z1}, {x0, z1},
                    0.95f, 0.95f, 0.95f, 0.95f, TexLayer::WaterSurface, 0.05f, 1u,
                    shore0, shore1, shore2, shore3);
            }
        }
    }

    terrainTriCount_ = static_cast<uint32_t>(worldVertices_.size() / 3u);
    propTriCount_ = static_cast<uint32_t>(propVertices_.size() / 3u);

    worldVertices_.insert(worldVertices_.end(), propVertices_.begin(), propVertices_.end());

    lod0VertexCount_ = static_cast<uint32_t>(worldVertices_.size());
    lod0SpriteCount_ = static_cast<uint32_t>(sprites_.size());

    RealmStats stats{};
    stats.terrainTriangles = terrainTriCount_;
    stats.propTriangles = propTriCount_;
    stats.spriteCount = static_cast<uint32_t>(sprites_.size());
    stats.pointLightCount = static_cast<uint32_t>(pointLights_.size());
    stats.atlasGenTimeMs = atlas_.generationTimeMs;
    stats.activeChunks = 1;
    stats.lod0Triangles = terrainTriCount_;
    stats.lod1Triangles = 0;
    stats.lod2Triangles = 0;
    stats.lod3Triangles = 0;
    stats.culledSprites = 0;
    return stats;
}

RaycastHit RealmWorld::raycast(Vec3f origin, Vec3f dir, float maxDist) const noexcept {
    RaycastHit result{};
    float dirLenSq = dir.x * dir.x + dir.y * dir.y + dir.z * dir.z;
    if (dirLenSq < 1e-6f) return result;
    Vec3f nDir = dir;
    if (std::abs(dirLenSq - 1.0f) > 1e-4f) {
        float invL = 1.0f / std::sqrt(dirLenSq);
        nDir.x *= invL; nDir.y *= invL; nDir.z *= invL;
    }

    float step = 0.40f;
    for (float d = 0.8f; d < maxDist; d += step) {
        Vec3f p{origin.x + nDir.x * d, origin.y + nDir.y * d, origin.z + nDir.z * d};
        float h = getTerrainHeightAt(p.x, p.z);
        if (p.y <= h) {
            result.hit = true;
            result.distance = d;
            result.position = p;

            if (!chunks_.empty()) {
                float chunkSpan = RealmChunk::kChunkSize;
                int cx = static_cast<int>(std::floor(p.x / chunkSpan));
                int cz = static_cast<int>(std::floor(p.z / chunkSpan));
                const RealmChunk* ch = getChunk(cx, cz);
                if (ch) {
                    float lxWorld = p.x - static_cast<float>(cx) * chunkSpan;
                    float lzWorld = p.z - static_cast<float>(cz) * chunkSpan;
                    int lx = std::clamp(static_cast<int>(std::floor(lxWorld / kTileSize)), 0, RealmChunk::kTilesPerChunk - 1);
                    int lz = std::clamp(static_cast<int>(std::floor(lzWorld / kTileSize)), 0, RealmChunk::kTilesPerChunk - 1);
                    const TileCell& cell = ch->getTile(lx, lz);
                    result.layerId = cell.hasWater ? TexLayer::WaterSurface : cell.topLayer;
                } else {
                    result.layerId = TexLayer::GrassMeadow;
                }
            } else {
                float fx = (p.x / kTileSize) + static_cast<float>(kGridWidth) * 0.5f;
                float fz = (p.z / kTileSize) + static_cast<float>(kGridDepth) * 0.5f;
                if (fx >= 0.0f && fz >= 0.0f && fx < static_cast<float>(kGridWidth) && fz < static_cast<float>(kGridDepth)) {
                    int gx = static_cast<int>(fx);
                    int gz = static_cast<int>(fz);
                    const TileCell& cell = getTile(gx, gz);
                    result.layerId = cell.hasWater ? TexLayer::WaterSurface : cell.topLayer;
                } else {
                    result.layerId = TexLayer::GrassMeadow;
                }
            }
            return result;
        }
    }
    return result;
}

void RealmWorld::generateProceduralRealm(uint32_t seed) {
    ProceduralRealmConfig config{};
    config.seed = seed;
    ProceduralWorldGenerator::generate(*this, config);
}

void RealmWorld::buildStarfallValeRealm() {
    generateProceduralRealm(1337u);
}

void RealmWorld::buildWoodlandTrailRealm(uint32_t seed) {
    ProceduralWorldGenerator::generateWoodlandTrail(*this, seed);
}

void RealmWorld::buildCoastalCoveRealm(uint32_t seed) {
    ProceduralWorldGenerator::generateCoastalCove(*this, seed);
}

void RealmWorld::buildChronicleHamletRealm(uint32_t seed) {
    HistorySimulator::simulate(*this, seed);
}

void RealmWorld::buildHighlandBridgeRealm(uint32_t seed) {
    ProceduralWorldGenerator::generateHighlandBridge(*this, seed);
}

void RealmWorld::emitPalmTree3D(std::vector<WorldVertex>& dst, Vec3f basePos, float scale, float rotAngle, bool withStonePlanter) {
    constexpr float kPi = 3.14159265358979323846f;
    const float s = std::max(0.25f, scale);
    float trunkBaseY = basePos.y;

    // 1. Optional Cylindrical Stone Planter / Plinth (matching reference screenshot)
    if (withStonePlanter) {
        const int planterSides = 12;
        const float rPlanter = 1.05f * s;
        const float hPlanter = 1.45f * s;
        for (int i = 0; i < planterSides; ++i) {
            float a0 = static_cast<float>(i) * (2.0f * kPi / static_cast<float>(planterSides));
            float a1 = static_cast<float>(i + 1) * (2.0f * kPi / static_cast<float>(planterSides));
            float midA = (a0 + a1) * 0.5f;

            Vec3f p0{basePos.x + std::cos(a0) * rPlanter, basePos.y, basePos.z + std::sin(a0) * rPlanter};
            Vec3f p1{basePos.x + std::cos(a1) * rPlanter, basePos.y, basePos.z + std::sin(a1) * rPlanter};
            Vec3f p2{basePos.x + std::cos(a1) * rPlanter, basePos.y + hPlanter, basePos.z + std::sin(a1) * rPlanter};
            Vec3f p3{basePos.x + std::cos(a0) * rPlanter, basePos.y + hPlanter, basePos.z + std::sin(a0) * rPlanter};

            Vec3f n{std::cos(midA), 0.0f, std::sin(midA)};
            Vec3f t{-std::sin(midA), 0.0f, std::cos(midA)};
            emitQuad(dst, p0, p1, p2, p3, n, t,
                {0.0f, 0.0f}, {0.5f, 0.0f}, {0.5f, 1.0f}, {0.0f, 1.0f},
                0.75f, 0.75f, 0.95f, 0.95f, TexLayer::CoastalCliff, 0.82f, 0u);
        }
        // Top soil cap
        Vec3f topCenter{basePos.x, basePos.y + hPlanter - 0.04f, basePos.z};
        for (int i = 0; i < planterSides; ++i) {
            float a0 = static_cast<float>(i) * (2.0f * kPi / static_cast<float>(planterSides));
            float a1 = static_cast<float>(i + 1) * (2.0f * kPi / static_cast<float>(planterSides));
            Vec3f p0{basePos.x + std::cos(a0) * (rPlanter - 0.08f), basePos.y + hPlanter - 0.04f, basePos.z + std::sin(a0) * (rPlanter - 0.08f)};
            Vec3f p1{basePos.x + std::cos(a1) * (rPlanter - 0.08f), basePos.y + hPlanter - 0.04f, basePos.z + std::sin(a1) * (rPlanter - 0.08f)};
            emitTriangle(dst, topCenter, p0, p1, {0.0f, 1.0f, 0.0f}, {1.0f, 0.0f, 0.0f},
                {0.5f, 0.5f}, {0.0f, 0.0f}, {1.0f, 0.0f}, 0.70f, TexLayer::DirtPath, 0.85f, 0u);
        }
        trunkBaseY += hPlanter - 0.10f;
    }

    // 2. Segmented Curved Palm Trunk
    const int trunkSides = 8;
    const int heightRings = 9;
    const float trunkH = 6.4f * s;
    const float leanAngle = rotAngle + 0.35f;
    const float maxLeanOffset = 1.35f * s;

    struct TrunkRing {
        Vec3f center;
        float radius;
    };
    std::vector<TrunkRing> rings(heightRings + 1);
    for (int r = 0; r <= heightRings; ++r) {
        float t = static_cast<float>(r) / static_cast<float>(heightRings);
        float curveT = std::pow(t, 1.55f);
        float lx = std::cos(leanAngle) * (maxLeanOffset * curveT);
        float lz = std::sin(leanAngle) * (maxLeanOffset * curveT);
        float ry = trunkBaseY + t * trunkH;
        float rad = (0.32f * (1.0f - t * 0.55f)) * s;
        rings[r] = {{basePos.x + lx, ry, basePos.z + lz}, rad};
    }

    for (int r = 0; r < heightRings; ++r) {
        const auto& r0 = rings[r];
        const auto& r1 = rings[r + 1];
        float v0 = static_cast<float>(r) * 0.45f;
        float v1 = static_cast<float>(r + 1) * 0.45f;

        for (int i = 0; i < trunkSides; ++i) {
            float a0 = rotAngle + static_cast<float>(i) * (2.0f * kPi / static_cast<float>(trunkSides));
            float a1 = rotAngle + static_cast<float>(i + 1) * (2.0f * kPi / static_cast<float>(trunkSides));
            float midA = (a0 + a1) * 0.5f;

            Vec3f p0{r0.center.x + std::cos(a0) * r0.radius, r0.center.y, r0.center.z + std::sin(a0) * r0.radius};
            Vec3f p1{r0.center.x + std::cos(a1) * r0.radius, r0.center.y, r0.center.z + std::sin(a1) * r0.radius};
            Vec3f p2{r1.center.x + std::cos(a1) * r1.radius, r1.center.y, r1.center.z + std::sin(a1) * r1.radius};
            Vec3f p3{r1.center.x + std::cos(a0) * r1.radius, r1.center.y, r1.center.z + std::sin(a0) * r1.radius};

            Vec3f n{std::cos(midA), 0.10f, std::sin(midA)};
            float nLen = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
            n = {n.x / nLen, n.y / nLen, n.z / nLen};
            Vec3f tang{-std::sin(midA), 0.0f, std::cos(midA)};

            float u0 = static_cast<float>(i) * 0.25f;
            float u1 = static_cast<float>(i + 1) * 0.25f;

            emitQuad(dst, p0, p1, p2, p3, n, tang,
                {u0, v0}, {u1, v0}, {u1, v1}, {u0, v1},
                0.78f, 0.78f, 0.92f, 0.92f, TexLayer::PalmBark, 0.85f, 0u);
        }
    }

    Vec3f crownPos = rings.back().center;

    // 3. Cluster of Coconuts at crown
    const int coconutCount = 5;
    for (int k = 0; k < coconutCount; ++k) {
        float ca = rotAngle + static_cast<float>(k) * (2.0f * kPi / static_cast<float>(coconutCount)) + 0.3f;
        float cRad = 0.28f * s;
        Vec3f cPos{crownPos.x + std::cos(ca) * cRad, crownPos.y - 0.22f * s, crownPos.z + std::sin(ca) * cRad};
        float cr = 0.16f * s;
        emitBox(dst,
            {cPos.x - cr, cPos.y - cr, cPos.z - cr},
            {cPos.x + cr, cPos.y + cr, cPos.z + cr},
            TexLayer::PalmBark, 0.5f, 0.80f, 0u);
    }

    // 4. Radiating 3D Arched Feather Palm Fronds (Canopy)
    const int frondCount = 9;
    const int frondSegs = 6;
    const float frondLen = 2.95f * s;

    for (int f = 0; f < frondCount; ++f) {
        float fAzimuth = rotAngle + static_cast<float>(f) * (2.0f * kPi / static_cast<float>(frondCount));
        float cosAz = std::cos(fAzimuth);
        float sinAz = std::sin(fAzimuth);
        Vec3f frondDir{cosAz, 0.0f, sinAz};
        Vec3f frondPerp{-sinAz, 0.0f, cosAz};

        for (int step = 0; step < frondSegs; ++step) {
            float u0 = static_cast<float>(step) / static_cast<float>(frondSegs);
            float u1 = static_cast<float>(step + 1) / static_cast<float>(frondSegs);

            auto evalFrondPoint = [&](float u) -> Vec3f {
                float r = frondLen * u;
                float dy = std::sin(u * 2.55f) * (0.62f * s) - (u * u) * (1.35f * s);
                return {
                    crownPos.x + frondDir.x * r,
                    crownPos.y + dy,
                    crownPos.z + frondDir.z * r
                };
            };

            auto evalFrondHalfWidth = [&](float u) -> float {
                return (std::sin(u * 3.14159f) * 0.44f + 0.05f) * s;
            };

            Vec3f c0 = evalFrondPoint(u0);
            Vec3f c1 = evalFrondPoint(u1);
            float w0 = evalFrondHalfWidth(u0);
            float w1 = evalFrondHalfWidth(u1);
            float droop0 = w0 * 0.35f;
            float droop1 = w1 * 0.35f;

            Vec3f l0{c0.x - frondPerp.x * w0, c0.y - droop0, c0.z - frondPerp.z * w0};
            Vec3f r0{c0.x + frondPerp.x * w0, c0.y - droop0, c0.z + frondPerp.z * w0};
            Vec3f l1{c1.x - frondPerp.x * w1, c1.y - droop1, c1.z - frondPerp.z * w1};
            Vec3f r1{c1.x + frondPerp.x * w1, c1.y - droop1, c1.z + frondPerp.z * w1};

            Vec3f normLeft{-frondPerp.x * 0.45f, 0.85f, -frondPerp.z * 0.45f};
            Vec3f normRight{frondPerp.x * 0.45f, 0.85f, frondPerp.z * 0.45f};

            emitQuad(dst,
                l0, c0, c1, l1,
                normLeft, frondDir,
                {0.0f, u0}, {0.5f, u0}, {0.5f, u1}, {0.0f, u1},
                0.90f, 0.95f, 0.95f, 0.90f, TexLayer::PalmFronds, 0.48f, 0u);
            emitQuad(dst,
                l1, c1, c0, l0,
                {-normLeft.x, -normLeft.y, -normLeft.z}, frondDir,
                {0.0f, u1}, {0.5f, u1}, {0.5f, u0}, {0.0f, u0},
                0.60f, 0.65f, 0.65f, 0.60f, TexLayer::PalmFronds, 0.55f, 0u);

            emitQuad(dst,
                c0, r0, r1, c1,
                normRight, frondDir,
                {0.5f, u0}, {1.0f, u0}, {1.0f, u1}, {0.5f, u1},
                0.95f, 0.90f, 0.90f, 0.95f, TexLayer::PalmFronds, 0.48f, 0u);
            emitQuad(dst,
                c1, r1, r0, c0,
                {-normRight.x, -normRight.y, -normRight.z}, frondDir,
                {0.5f, u1}, {1.0f, u1}, {1.0f, u0}, {0.5f, u0},
                0.65f, 0.60f, 0.60f, 0.65f, TexLayer::PalmFronds, 0.55f, 0u);
        }
    }
}

void RealmWorld::addPalmTree3D(Vec3f basePos, float scale, float rotAngle, bool withStonePlanter) {
    emitPalmTree3D(propVertices_, basePos, scale, rotAngle, withStonePlanter);
}

void RealmWorld::addPalmTree3DGrounded(float worldX, float worldZ, float scale, float rotAngle, bool withStonePlanter) {
    float groundY = getTerrainHeightAt(worldX, worldZ);
    addPalmTree3D({worldX, groundY, worldZ}, scale, rotAngle, withStonePlanter);
}

void RealmWorld::addTreasureChest(Vec3f pos, float rotAngle) {
    constexpr float kPi = 3.14159265358979323846f;
    const float w = 1.15f;
    const float d = 0.78f;
    const float baseH = 0.46f;
    const float lidArchH = 0.32f;

    float cosA = std::cos(rotAngle);
    float sinA = std::sin(rotAngle);

    auto xform = [&](float lx, float ly, float lz) -> Vec3f {
        return {
            pos.x + (lx * cosA - lz * sinA),
            pos.y + ly,
            pos.z + (lx * sinA + lz * cosA)
        };
    };

    float hw = w * 0.5f;
    float hd = d * 0.5f;
    emitBox(propVertices_,
        {pos.x - hw, pos.y, pos.z - hd},
        {pos.x + hw, pos.y + baseH, pos.z + hd},
        TexLayer::WoodPlanks, 0.6f, 0.72f);

    const int lidSegs = 4;
    for (int i = 0; i < lidSegs; ++i) {
        float a0 = static_cast<float>(i) * (kPi / static_cast<float>(lidSegs));
        float a1 = static_cast<float>(i + 1) * (kPi / static_cast<float>(lidSegs));
        float z0 = -hd * std::cos(a0);
        float y0 = baseH + std::sin(a0) * lidArchH;
        float z1 = -hd * std::cos(a1);
        float y1 = baseH + std::sin(a1) * lidArchH;

        Vec3f p0 = xform(-hw, y0, z0);
        Vec3f p1 = xform( hw, y0, z0);
        Vec3f p2 = xform( hw, y1, z1);
        Vec3f p3 = xform(-hw, y1, z1);

        float midA = (a0 + a1) * 0.5f;
        Vec3f nLocal{0.0f, std::sin(midA), -std::cos(midA)};
        Vec3f nWorld{nLocal.x * cosA - nLocal.z * sinA, nLocal.y, nLocal.x * sinA + nLocal.z * cosA};
        Vec3f tWorld{cosA, 0.0f, sinA};

        emitQuad(propVertices_, p0, p1, p2, p3, nWorld, tWorld,
            {0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 0.5f}, {0.0f, 0.5f},
            0.88f, 0.88f, 0.95f, 0.95f, TexLayer::WoodPlanks, 0.68f, 0u);
    }

    Vec3f apexL = xform(-hw, baseH + lidArchH, 0.0f);
    Vec3f apexR = xform( hw, baseH + lidArchH, 0.0f);
    emitTriangle(propVertices_,
        xform(-hw, baseH, -hd), xform(-hw, baseH, hd), apexL,
        {-cosA, 0.0f, -sinA}, {0.0f, 1.0f, 0.0f},
        {0.0f, 0.0f}, {1.0f, 0.0f}, {0.5f, 1.0f}, 0.85f, TexLayer::WoodPlanks, 0.70f);
    emitTriangle(propVertices_,
        xform(hw, baseH, hd), xform(hw, baseH, -hd), apexR,
        {cosA, 0.0f, sinA}, {0.0f, 1.0f, 0.0f},
        {0.0f, 0.0f}, {1.0f, 0.0f}, {0.5f, 1.0f}, 0.85f, TexLayer::WoodPlanks, 0.70f);

    float bandW = 0.08f;
    emitBox(propVertices_,
        {pos.x - hw - 0.01f, pos.y, pos.z - hd - 0.01f},
        {pos.x - hw + bandW, pos.y + baseH + 0.02f, pos.z + hd + 0.01f},
        TexLayer::Cobblestone, 0.4f, 0.60f);
    emitBox(propVertices_,
        {pos.x + hw - bandW, pos.y, pos.z - hd - 0.01f},
        {pos.x + hw + 0.01f, pos.y + baseH + 0.02f, pos.z + hd + 0.01f},
        TexLayer::Cobblestone, 0.4f, 0.60f);

    Vec3f lockPos = xform(0.0f, baseH - 0.04f, -hd - 0.035f);
    emitBox(propVertices_,
        {lockPos.x - 0.10f, lockPos.y - 0.08f, lockPos.z - 0.04f},
        {lockPos.x + 0.10f, lockPos.y + 0.08f, lockPos.z + 0.04f},
        TexLayer::WindowWarmGlow, 1.0f, 0.25f, 2u);
}

void RealmWorld::addTreasureChestGrounded(float worldX, float worldZ, float rotAngle) {
    float groundY = getTerrainHeightAt(worldX, worldZ);
    addTreasureChest({worldX, groundY, worldZ}, rotAngle);
}

void RealmWorld::addSeaStack3D(Vec3f basePos, float baseRadius, float height, uint32_t seed) {
    constexpr float kPi = 3.14159265358979323846f;
    const int kSides = 10;
    const int kLayers = 6;
    const float layerH = height / static_cast<float>(kLayers);

    struct StackTier {
        float r;
        float y0;
        float y1;
    };
    std::vector<StackTier> tiers(kLayers);
    for (int l = 0; l < kLayers; ++l) {
        float t = static_cast<float>(l) / static_cast<float>(kLayers);
        float taper = 1.0f - t * 0.40f;
        if (l == 2 || l == 4) taper *= 1.15f;
        tiers[l] = {
            baseRadius * taper,
            basePos.y + static_cast<float>(l) * layerH,
            basePos.y + static_cast<float>(l + 1) * layerH
        };
    }

    for (int l = 0; l < kLayers; ++l) {
        const auto& tier = tiers[l];
        float rotTier = static_cast<float>(l) * 0.22f;

        for (int i = 0; i < kSides; ++i) {
            float a0 = rotTier + static_cast<float>(i) * (2.0f * kPi / static_cast<float>(kSides));
            float a1 = rotTier + static_cast<float>(i + 1) * (2.0f * kPi / static_cast<float>(kSides));
            float midA = (a0 + a1) * 0.5f;

            float r0 = tier.r * (1.0f + PixelCanvas::hash2D(l * 10 + i, 3, seed) * 0.14f);
            float r1 = tier.r * (1.0f + PixelCanvas::hash2D(l * 10 + i + 1, 3, seed) * 0.14f);

            Vec3f p0{basePos.x + std::cos(a0) * r0, tier.y0, basePos.z + std::sin(a0) * r0};
            Vec3f p1{basePos.x + std::cos(a1) * r1, tier.y0, basePos.z + std::sin(a1) * r1};
            Vec3f p2{basePos.x + std::cos(a1) * r1, tier.y1, basePos.z + std::sin(a1) * r1};
            Vec3f p3{basePos.x + std::cos(a0) * r0, tier.y1, basePos.z + std::sin(a0) * r0};

            Vec3f n{std::cos(midA), 0.08f, std::sin(midA)};
            Vec3f tang{-std::sin(midA), 0.0f, std::cos(midA)};

            float u0 = static_cast<float>(i) * 0.5f;
            float u1 = static_cast<float>(i + 1) * 0.5f;

            emitQuad(propVertices_, p0, p1, p2, p3, n, tang,
                {u0, 0.0f}, {u1, 0.0f}, {u1, 1.0f}, {u0, 1.0f},
                0.75f, 0.75f, 0.92f, 0.92f, TexLayer::CoastalCliff, 0.85f, 0u);
        }

        Vec3f topCenter{basePos.x, tier.y1, basePos.z};
        for (int i = 0; i < kSides; ++i) {
            float a0 = rotTier + static_cast<float>(i) * (2.0f * kPi / static_cast<float>(kSides));
            float a1 = rotTier + static_cast<float>(i + 1) * (2.0f * kPi / static_cast<float>(kSides));
            float r0 = tier.r * (1.0f + PixelCanvas::hash2D(l * 10 + i, 3, seed) * 0.14f);
            float r1 = tier.r * (1.0f + PixelCanvas::hash2D(l * 10 + i + 1, 3, seed) * 0.14f);

            Vec3f p0{basePos.x + std::cos(a0) * r0, tier.y1, basePos.z + std::sin(a0) * r0};
            Vec3f p1{basePos.x + std::cos(a1) * r1, tier.y1, basePos.z + std::sin(a1) * r1};

            emitTriangle(propVertices_, topCenter, p0, p1,
                {0.0f, 1.0f, 0.0f}, {1.0f, 0.0f, 0.0f},
                {0.5f, 0.5f}, {0.0f, 0.0f}, {1.0f, 0.0f},
                0.88f, TexLayer::CoastalScree, 0.80f, 0u);
        }
    }
}

void RealmWorld::addCoastalBridge(Vec3f startPos, Vec3f endPos, float width) {
    Vec3f delta{endPos.x - startPos.x, endPos.y - startPos.y, endPos.z - startPos.z};
    float len = std::sqrt(delta.x * delta.x + delta.y * delta.y + delta.z * delta.z);
    if (len < 0.1f) return;

    Vec3f fwd{delta.x / len, delta.y / len, delta.z / len};
    Vec3f up{0.0f, 1.0f, 0.0f};
    Vec3f perp{-fwd.z, 0.0f, fwd.x};
    float perpLen = std::sqrt(perp.x * perp.x + perp.z * perp.z);
    if (perpLen > 1e-5f) {
        perp = {perp.x / perpLen, 0.0f, perp.z / perpLen};
    } else {
        perp = {1.0f, 0.0f, 0.0f};
    }

    const int kSegs = 14;
    const float halfW = width * 0.5f;

    for (int i = 0; i < kSegs; ++i) {
        float t0 = static_cast<float>(i) / static_cast<float>(kSegs);
        float t1 = static_cast<float>(i + 1) / static_cast<float>(kSegs);

        float sag0 = std::sin(t0 * 3.14159f) * 0.32f;
        float sag1 = std::sin(t1 * 3.14159f) * 0.32f;

        Vec3f c0{startPos.x + delta.x * t0, startPos.y + delta.y * t0 + sag0, startPos.z + delta.z * t0};
        Vec3f c1{startPos.x + delta.x * t1, startPos.y + delta.y * t1 + sag1, startPos.z + delta.z * t1};

        Vec3f p00{c0.x - perp.x * halfW, c0.y, c0.z - perp.z * halfW};
        Vec3f p10{c0.x + perp.x * halfW, c0.y, c0.z + perp.z * halfW};
        Vec3f p11{c1.x + perp.x * halfW, c1.y, c1.z + perp.z * halfW};
        Vec3f p01{c1.x - perp.x * halfW, c1.y, c1.z - perp.z * halfW};

        emitQuad(propVertices_, p00, p10, p11, p01, up, fwd,
            {0.0f, t0 * 5.0f}, {width * 0.5f, t0 * 5.0f}, {width * 0.5f, t1 * 5.0f}, {0.0f, t1 * 5.0f},
            0.88f, 0.88f, 0.95f, 0.95f, TexLayer::WoodPlanks, 0.72f, 0u);

        emitQuad(propVertices_, p01, p11, p10, p00, {-up.x, -up.y, -up.z}, fwd,
            {0.0f, t1 * 5.0f}, {width * 0.5f, t1 * 5.0f}, {width * 0.5f, t0 * 5.0f}, {0.0f, t0 * 5.0f},
            0.55f, 0.55f, 0.60f, 0.60f, TexLayer::WoodPlanks, 0.75f, 0u);

        emitQuad(propVertices_,
            {p00.x, p00.y - 0.22f, p00.z}, p00, p01, {p01.x, p01.y - 0.22f, p01.z},
            {-perp.x, 0.0f, -perp.z}, fwd,
            {0.0f, 0.0f}, {0.0f, 0.3f}, {1.0f, 0.3f}, {1.0f, 0.0f},
            0.70f, 0.85f, 0.85f, 0.70f, TexLayer::WoodPlanks, 0.75f, 0u);
        emitQuad(propVertices_,
            p10, {p10.x, p10.y - 0.22f, p10.z}, {p11.x, p11.y - 0.22f, p11.z}, p11,
            {perp.x, 0.0f, perp.z}, fwd,
            {0.0f, 0.3f}, {0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 0.3f},
            0.85f, 0.70f, 0.70f, 0.85f, TexLayer::WoodPlanks, 0.75f, 0u);

        if (i % 2 == 0) {
            float postH = 1.15f;
            emitBox(propVertices_,
                {p00.x - 0.12f, p00.y - 0.45f, p00.z - 0.12f},
                {p00.x + 0.12f, p00.y + postH, p00.z + 0.12f},
                TexLayer::WoodPlanks, 0.5f, 0.75f);
            emitBox(propVertices_,
                {p10.x - 0.12f, p10.y - 0.45f, p10.z - 0.12f},
                {p10.x + 0.12f, p10.y + postH, p10.z + 0.12f},
                TexLayer::WoodPlanks, 0.5f, 0.75f);
        }

        float railH = 1.05f;
        Vec3f rL0{p00.x, p00.y + railH, p00.z};
        Vec3f rL1{p01.x, p01.y + railH, p01.z};
        Vec3f rR0{p10.x, p10.y + railH, p10.z};
        Vec3f rR1{p11.x, p11.y + railH, p11.z};

        emitQuad(propVertices_,
            {rL0.x - perp.x * 0.06f, rL0.y - 0.06f, rL0.z - perp.z * 0.06f},
            {rL0.x + perp.x * 0.06f, rL0.y + 0.06f, rL0.z + perp.z * 0.06f},
            {rL1.x + perp.x * 0.06f, rL1.y + 0.06f, rL1.z + perp.z * 0.06f},
            {rL1.x - perp.x * 0.06f, rL1.y - 0.06f, rL1.z - perp.z * 0.06f},
            up, fwd, {0.0f, 0.0f}, {0.2f, 0.0f}, {0.2f, 1.0f}, {0.0f, 1.0f},
            0.92f, 0.92f, 0.92f, 0.92f, TexLayer::WoodPlanks, 0.70f, 0u);

        emitQuad(propVertices_,
            {rR0.x - perp.x * 0.06f, rR0.y - 0.06f, rR0.z - perp.z * 0.06f},
            {rR0.x + perp.x * 0.06f, rR0.y + 0.06f, rR0.z + perp.z * 0.06f},
            {rR1.x + perp.x * 0.06f, rR1.y + 0.06f, rR1.z + perp.z * 0.06f},
            {rR1.x - perp.x * 0.06f, rR1.y - 0.06f, rR1.z - perp.z * 0.06f},
            up, fwd, {0.0f, 0.0f}, {0.2f, 0.0f}, {0.2f, 1.0f}, {0.0f, 1.0f},
            0.92f, 0.92f, 0.92f, 0.92f, TexLayer::WoodPlanks, 0.70f, 0u);
    }
}

void RealmWorld::addRuinedHouse(Vec3f centerPos, Vec3f size, int roofAxis, float collapseRatio) {
    (void)roofAxis;
    float hw = size.x * 0.5f;
    float hd = size.z * 0.5f;
    float wallH = size.y * 0.58f;
    float roofH = size.y * 0.42f;

    // Stone foundation base
    emitBox(propVertices_,
        {centerPos.x - hw - 0.12f, centerPos.y, centerPos.z - hd - 0.12f},
        {centerPos.x + hw + 0.12f, centerPos.y + 0.55f, centerPos.z + hd + 0.12f},
        TexLayer::StoneBrick, 0.45f, 0.85f);

    // Intact half (left side)
    float splitX = centerPos.x - hw + size.x * (1.0f - collapseRatio);

    // Standing stone/timber walls on intact side
    emitBox(propVertices_,
        {centerPos.x - hw, centerPos.y + 0.55f, centerPos.z - hd},
        {splitX, centerPos.y + wallH, centerPos.z - hd + 0.35f},
        TexLayer::StoneBrick, 0.55f, 0.80f);
    emitBox(propVertices_,
        {centerPos.x - hw, centerPos.y + 0.55f, centerPos.z + hd - 0.35f},
        {splitX, centerPos.y + wallH, centerPos.z + hd},
        TexLayer::StoneBrick, 0.55f, 0.80f);
    emitBox(propVertices_,
        {centerPos.x - hw, centerPos.y + 0.55f, centerPos.z - hd},
        {centerPos.x - hw + 0.35f, centerPos.y + wallH, centerPos.z + hd},
        TexLayer::StoneBrick, 0.55f, 0.80f);

    // Intact roof portion
    Vec3f r0{centerPos.x - hw - 0.25f, centerPos.y + wallH, centerPos.z - hd - 0.25f};
    Vec3f r1{splitX + 0.15f, centerPos.y + wallH + roofH, centerPos.z};
    emitBox(propVertices_,
        {r0.x, r0.y, r0.z},
        {r1.x, r1.y, centerPos.z + hd + 0.25f},
        TexLayer::SlateRoof, 0.55f, 0.70f);

    // Standing stone chimney with scorched soot
    emitBox(propVertices_,
        {centerPos.x - hw + 0.35f, centerPos.y + 0.55f, centerPos.z - 0.65f},
        {centerPos.x - hw + 1.65f, centerPos.y + wallH + roofH + 0.95f, centerPos.z + 0.65f},
        TexLayer::StoneBrick, 0.35f, 0.90f);

    // Collapsed side: Jagged low broken wall stumps
    emitBox(propVertices_,
        {splitX, centerPos.y + 0.55f, centerPos.z - hd},
        {centerPos.x + hw, centerPos.y + 1.25f, centerPos.z - hd + 0.35f},
        TexLayer::StoneBrick, 0.40f, 0.88f);
    emitBox(propVertices_,
        {splitX, centerPos.y + 0.55f, centerPos.z + hd - 0.35f},
        {centerPos.x + hw, centerPos.y + 0.95f, centerPos.z + hd},
        TexLayer::StoneBrick, 0.40f, 0.88f);
    emitBox(propVertices_,
        {centerPos.x + hw - 0.35f, centerPos.y + 0.55f, centerPos.z - hd},
        {centerPos.x + hw, centerPos.y + 1.45f, centerPos.z + hd},
        TexLayer::StoneBrick, 0.40f, 0.88f);

    // Fallen charred wooden rafters & rubble inside the collapsed area
    emitBox(propVertices_,
        {splitX - 0.2f, centerPos.y + 0.6f, centerPos.z - hd + 0.6f},
        {centerPos.x + hw - 0.5f, centerPos.y + 1.1f, centerPos.z - hd + 0.95f},
        TexLayer::WoodPlanks, 0.35f, 0.85f);
    emitBox(propVertices_,
        {splitX + 0.4f, centerPos.y + 0.6f, centerPos.z + 0.2f},
        {centerPos.x + hw - 0.2f, centerPos.y + 1.0f, centerPos.z + 0.55f},
        TexLayer::WoodPlanks, 0.35f, 0.85f);
    emitBox(propVertices_,
        {splitX + 0.8f, centerPos.y + 0.55f, centerPos.z - 1.2f},
        {centerPos.x + hw - 0.8f, centerPos.y + 0.85f, centerPos.z + 1.2f},
        TexLayer::SlateRoof, 0.45f, 0.80f);
}

void RealmWorld::addBarricade(Vec3f pos, float width, float rotAngle) {
    (void)rotAngle;
    float hw = width * 0.5f;

    // Overturned timber wagon chassis propped up as main shield wall
    emitBox(propVertices_,
        {pos.x - hw, pos.y, pos.z - 0.35f},
        {pos.x + hw, pos.y + 1.55f, pos.z + 0.25f},
        TexLayer::WoodPlanks, 0.55f, 0.78f);

    // Diagonal support timbers / cross-bracing behind
    emitBox(propVertices_,
        {pos.x - hw + 0.4f, pos.y, pos.z + 0.25f},
        {pos.x - hw + 0.7f, pos.y + 1.45f, pos.z + 0.95f},
        TexLayer::WoodPlanks, 0.50f, 0.80f);
    emitBox(propVertices_,
        {pos.x + hw - 0.7f, pos.y, pos.z + 0.25f},
        {pos.x + hw - 0.4f, pos.y + 1.45f, pos.z + 0.95f},
        TexLayer::WoodPlanks, 0.50f, 0.80f);

    // Sharpened wooden defensive stakes pointing outward
    for (int i = 0; i < 4; ++i) {
        float sx = pos.x - hw + 0.6f + static_cast<float>(i) * (width - 1.2f) / 3.0f;
        emitBox(propVertices_,
            {sx - 0.08f, pos.y + 0.4f, pos.z - 1.15f},
            {sx + 0.08f, pos.y + 1.35f, pos.z - 0.15f},
            TexLayer::WoodPlanks, 0.45f, 0.82f);
    }

    // Stacked reinforcing crates and barrels wedged into the defense line
    addCrateStack({pos.x - hw + 0.8f, pos.y, pos.z + 0.65f}, 2);
    addBarrel({pos.x + hw - 0.9f, pos.y, pos.z + 0.65f});
}

void RealmWorld::addRunicAltar(Vec3f pos, float radius, bool activeGlow) {
    constexpr float kPi = 3.14159265358979323846f;
    const int sides = 8;

    // 1. Stepped octagonal stone plinth
    for (int i = 0; i < sides; ++i) {
        float a0 = static_cast<float>(i) * (2.0f * kPi / static_cast<float>(sides));
        float a1 = static_cast<float>(i + 1) * (2.0f * kPi / static_cast<float>(sides));

        Vec3f p0{pos.x + std::cos(a0) * radius, pos.y, pos.z + std::sin(a0) * radius};
        Vec3f p1{pos.x + std::cos(a1) * radius, pos.y, pos.z + std::sin(a1) * radius};
        Vec3f p0Top{pos.x + std::cos(a0) * (radius * 0.95f), pos.y + 0.45f, pos.z + std::sin(a0) * (radius * 0.95f)};
        Vec3f p1Top{pos.x + std::cos(a1) * (radius * 0.95f), pos.y + 0.45f, pos.z + std::sin(a1) * (radius * 0.95f)};

        Vec3f n{std::cos((a0 + a1) * 0.5f), 0.2f, std::sin((a0 + a1) * 0.5f)};
        float nL = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
        n = {n.x / nL, n.y / nL, n.z / nL};
        Vec3f t{-std::sin((a0 + a1) * 0.5f), 0.0f, std::cos((a0 + a1) * 0.5f)};

        emitQuad(propVertices_, p0, p1, p1Top, p0Top, n, t,
            {0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 0.5f}, {0.0f, 0.5f},
            0.65f, 0.65f, 0.85f, 0.85f, TexLayer::StoneBrick, 0.82f, 0u);

        emitTriangle(propVertices_, {pos.x, pos.y + 0.45f, pos.z}, p0Top, p1Top,
            {0.0f, 1.0f, 0.0f}, {1.0f, 0.0f, 0.0f},
            {0.5f, 0.5f}, {0.0f, 0.0f}, {1.0f, 0.0f},
            0.88f, TexLayer::StoneBrick, 0.80f, 0u);
    }

    // 2. Four ritual stone obelisks at cardinal points
    const float pillarR = radius * 0.82f;
    for (int i = 0; i < 4; ++i) {
        float angle = static_cast<float>(i) * (kPi * 0.5f);
        float px = pos.x + std::cos(angle) * pillarR;
        float pz = pos.z + std::sin(angle) * pillarR;
        emitBox(propVertices_,
            {px - 0.28f, pos.y + 0.45f, pz - 0.28f},
            {px + 0.28f, pos.y + 2.85f, pz + 0.28f},
            TexLayer::StoneBrick, 0.55f, 0.82f);
        // Emissive crystal tip on each obelisk
        if (activeGlow) {
            emitBox(propVertices_,
                {px - 0.14f, pos.y + 2.85f, pz - 0.14f},
                {px + 0.14f, pos.y + 3.25f, pz + 0.14f},
                TexLayer::WindowWarmGlow, 1.0f, 0.20f, 2u);
        }
    }

    // 3. Central transmutation altar table
    emitBox(propVertices_,
        {pos.x - 1.25f, pos.y + 0.45f, pos.z - 0.75f},
        {pos.x + 1.25f, pos.y + 1.35f, pos.z + 0.75f},
        TexLayer::StoneBrick, 0.60f, 0.85f);

    // Glowing runic channel in center of slab
    if (activeGlow) {
        emitBox(propVertices_,
            {pos.x - 0.95f, pos.y + 1.36f, pos.z - 0.15f},
            {pos.x + 0.95f, pos.y + 1.38f, pos.z + 0.15f},
            TexLayer::WindowWarmGlow, 1.0f, 0.15f, 2u);

        // Mystical violet / arcane point light
        addPointLight(
            {pos.x, pos.y + 1.85f, pos.z},
            {0.75f, 0.22f, 1.0f}, // Deep Arcane Violet
            16.0f,
            2.4f
        );
    }
}

void RealmWorld::addBrokenBridge(Vec3f startPos, Vec3f endPos, float breakRatio, float width) {
    constexpr int kSegments = 10;
    float halfW = width * 0.5f;
    int breakStart = static_cast<int>(std::round(kSegments * (0.5f - breakRatio * 0.5f)));
    int breakEnd   = static_cast<int>(std::round(kSegments * (0.5f + breakRatio * 0.5f)));

    for (int i = 0; i < kSegments; ++i) {
        float t0 = static_cast<float>(i) / static_cast<float>(kSegments);
        float t1 = static_cast<float>(i + 1) / static_cast<float>(kSegments);
        float arch0 = std::sin(t0 * 3.14159265f) * 0.65f;
        float arch1 = std::sin(t1 * 3.14159265f) * 0.65f;

        Vec3f a{
            startPos.x + (endPos.x - startPos.x) * t0,
            startPos.y + (endPos.y - startPos.y) * t0 + arch0,
            startPos.z + (endPos.z - startPos.z) * t0
        };
        Vec3f b{
            startPos.x + (endPos.x - startPos.x) * t1,
            startPos.y + (endPos.y - startPos.y) * t1 + arch1,
            startPos.z + (endPos.z - startPos.z) * t1
        };

        // If in the broken center gap, drop collapsed splintered planks into the waterbed below!
        if (i >= breakStart && i <= breakEnd) {
            float dropY = std::min(startPos.y, endPos.y) - 2.1f;
            if (i % 2 == 0) {
                emitBox(propVertices_,
                    {a.x - 0.4f, dropY + 0.1f, a.z - halfW * 0.7f},
                    {b.x + 0.4f, dropY + 0.35f, a.z + halfW * 0.7f},
                    TexLayer::WoodPlanks, 0.45f, 0.80f);
            }
            continue;
        }

        float minX = std::min(a.x, b.x);
        float maxX = std::max(a.x, b.x);
        float avgY = (a.y + b.y) * 0.5f;

        // Deck planking
        emitBox(propVertices_,
            {minX - 0.05f, avgY - 0.18f, a.z - halfW},
            {maxX + 0.05f, avgY + 0.08f, a.z + halfW},
            TexLayer::WoodPlanks, 0.65f, 0.72f);

        // Handrail posts on intact ends
        emitBox(propVertices_,
            {a.x - 0.12f, avgY - 0.8f, a.z - halfW - 0.12f},
            {a.x + 0.12f, avgY + 1.05f, a.z - halfW + 0.12f},
            TexLayer::WoodPlanks, 0.5f, 0.75f);
        emitBox(propVertices_,
            {a.x - 0.12f, avgY - 0.8f, a.z + halfW - 0.12f},
            {a.x + 0.12f, avgY + 1.05f, a.z + halfW + 0.12f},
            TexLayer::WoodPlanks, 0.5f, 0.75f);

        // Top handrail beam
        emitBox(propVertices_,
            {minX, avgY + 0.82f, a.z - halfW - 0.08f},
            {maxX, avgY + 0.98f, a.z - halfW + 0.08f},
            TexLayer::WoodPlanks, 0.5f, 0.75f);
        emitBox(propVertices_,
            {minX, avgY + 0.82f, a.z + halfW - 0.08f},
            {maxX, avgY + 0.98f, a.z + halfW + 0.08f},
            TexLayer::WoodPlanks, 0.5f, 0.75f);
    }
}

} // namespace eatsrealms

