#include "eatsrealms/realm_world.hpp"
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
        t = TileCell{2, TexLayer::GrassMeadow, TexLayer::CliffStone, SlopeDir::None, false, 0.65f};
    }
    propVertices_.clear();
    worldVertices_.clear();
    sprites_.clear();
    pointLights_.clear();
    terrainTriCount_ = 0;
    propTriCount_ = 0;
    camera_ = CameraConfig{};
    atmosphere_ = AtmosphereConfig{};
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
    float fx = (worldX / kTileSize) + static_cast<float>(kGridWidth) * 0.5f;
    float fz = (worldZ / kTileSize) + static_cast<float>(kGridDepth) * 0.5f;
    int gx = std::clamp(static_cast<int>(std::floor(fx)), 0, kGridWidth - 1);
    int gz = std::clamp(static_cast<int>(std::floor(fz)), 0, kGridDepth - 1);
    const TileCell& cell = getTile(gx, gz);
    float baseH = static_cast<float>(cell.elevation) * kElevStep;
    if (cell.hasWater && cell.waterY > baseH) {
        return cell.waterY;
    }
    return baseH;
}

void RealmWorld::setTile(int gx, int gz, int16_t elevation, uint16_t topLayer, uint16_t cliffLayer) {
    if (gx < 0 || gz < 0 || gx >= kGridWidth || gz >= kGridDepth) return;
    TileCell& t = tiles_[static_cast<size_t>(gz) * kGridWidth + static_cast<size_t>(gx)];
    t.elevation = elevation;
    t.topLayer = topLayer;
    t.cliffLayer = cliffLayer;
    t.slope = SlopeDir::None;
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

void RealmWorld::addStairs(int gx0, int gz0, int gx1, int gz1, int16_t lowElev, int16_t highElev, SlopeDir dir, uint16_t stairLayer) {
    int xMin = std::max(0, std::min(gx0, gx1));
    int xMax = std::min(kGridWidth - 1, std::max(gx0, gx1));
    int zMin = std::max(0, std::min(gz0, gz1));
    int zMax = std::min(kGridDepth - 1, std::max(gz0, gz1));
    for (int gz = zMin; gz <= zMax; ++gz) {
        for (int gx = xMin; gx <= xMax; ++gx) {
            float t = 0.0f;
            if (dir == SlopeDir::PlusX || dir == SlopeDir::MinusX) {
                t = static_cast<float>(gx - xMin) / std::max(1.0f, static_cast<float>(xMax - xMin));
                if (dir == SlopeDir::MinusX) t = 1.0f - t;
            } else {
                t = static_cast<float>(gz - zMin) / std::max(1.0f, static_cast<float>(zMax - zMin));
                if (dir == SlopeDir::MinusZ) t = 1.0f - t;
            }
            int16_t elev = static_cast<int16_t>(std::round(lowElev + (highElev - lowElev) * t));
            setTile(gx, gz, elev, stairLayer, TexLayer::StoneBrick);
            tiles_[static_cast<size_t>(gz) * kGridWidth + static_cast<size_t>(gx)].slope = dir;
        }
    }
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
    uint32_t texLayer, float roughness, uint32_t flags
) {
    auto makeV = [&](Vec3f p, Vec2f uv, float ao) -> WorldVertex {
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

    WorldVertex v0 = makeV(p0, uv0, ao0);
    WorldVertex v1 = makeV(p1, uv1, ao1);
    WorldVertex v2 = makeV(p2, uv2, ao2);
    WorldVertex v3 = makeV(p3, uv3, ao3);

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

    const float halfGridW = static_cast<float>(kGridWidth) * 0.5f;
    const float halfGridD = static_cast<float>(kGridDepth) * 0.5f;

    for (int gz = 0; gz < kGridDepth; ++gz) {
        for (int gx = 0; gx < kGridWidth; ++gx) {
            const TileCell& cell = getTile(gx, gz);
            float x0 = (static_cast<float>(gx) - halfGridW) * kTileSize;
            float x1 = x0 + kTileSize;
            float z0 = (static_cast<float>(gz) - halfGridD) * kTileSize;
            float z1 = z0 + kTileSize;
            float yTop = static_cast<float>(cell.elevation) * kElevStep;

            // Compute corner AO from higher neighbor elevations (gives rich HD-2D cliff-base contact shadows)
            auto neighborHigher = [&](int dx, int dz) -> float {
                int16_t ne = getTile(gx + dx, gz + dz).elevation;
                return (ne > cell.elevation) ? std::min(0.42f, static_cast<float>(ne - cell.elevation) * 0.14f) : 0.0f;
            };
            float ao00 = 1.0f - std::max({neighborHigher(-1, 0), neighborHigher(0, -1), neighborHigher(-1, -1) * 0.7f});
            float ao10 = 1.0f - std::max({neighborHigher( 1, 0), neighborHigher(0, -1), neighborHigher( 1, -1) * 0.7f});
            float ao11 = 1.0f - std::max({neighborHigher( 1, 0), neighborHigher(0,  1), neighborHigher( 1,  1) * 0.7f});
            float ao01 = 1.0f - std::max({neighborHigher(-1, 0), neighborHigher(0,  1), neighborHigher(-1,  1) * 0.7f});

            // 1. Top terrain quad (or stepped stone stairs when cell.slope != None)
            if (cell.slope != SlopeDir::None) {
                // Render 3 crisp stone stair steps per tile
                constexpr int kSteps = 3;
                for (int s = 0; s < kSteps; ++s) {
                    float f0 = static_cast<float>(s) / static_cast<float>(kSteps);
                    float f1 = static_cast<float>(s + 1) / static_cast<float>(kSteps);
                    float stepY = yTop - (1.0f - f1) * kElevStep;
                    if (cell.slope == SlopeDir::PlusX || cell.slope == SlopeDir::MinusX) {
                        float sx0 = x0 + (x1 - x0) * f0;
                        float sx1 = x0 + (x1 - x0) * f1;
                        emitBox(worldVertices_, {sx0, yTop - kElevStep, z0}, {sx1, stepY, z1}, cell.topLayer, 0.65f, 0.75f);
                    } else {
                        float sz0 = z0 + (z1 - z0) * f0;
                        float sz1 = z0 + (z1 - z0) * f1;
                        emitBox(worldVertices_, {x0, yTop - kElevStep, sz0}, {x1, stepY, sz1}, cell.topLayer, 0.65f, 0.75f);
                    }
                }
            } else {
                emitQuad(worldVertices_,
                    {x0, yTop, z0}, {x1, yTop, z0}, {x1, yTop, z1}, {x0, yTop, z1},
                    {0.0f, 1.0f, 0.0f}, {1.0f, 0.0f, 0.0f},
                    {0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f},
                    ao00, ao10, ao11, ao01, cell.topLayer, 0.80f, 0u);
            }

            // 2. Stepped 3D Cliff Skirt Walls wherever this tile is higher than its 4 neighbors
            auto emitCliffFace = [&](int nx, int nz, Vec3f c0, Vec3f c1, Vec3f norm, Vec3f tang) {
                int16_t nElev = (nx >= 0 && nz >= 0 && nx < kGridWidth && nz < kGridDepth)
                    ? getTile(nx, nz).elevation
                    : static_cast<int16_t>(0);
                if (cell.elevation > nElev) {
                    float yBot = static_cast<float>(nElev) * kElevStep;
                    float vSpan = (yTop - yBot) / kTileSize;
                    emitQuad(worldVertices_,
                        {c0.x, yBot, c0.z}, {c1.x, yBot, c1.z}, {c1.x, yTop, c1.z}, {c0.x, yTop, c0.z},
                        norm, tang,
                        {0.0f, vSpan}, {1.0f, vSpan}, {1.0f, 0.0f}, {0.0f, 0.0f},
                        0.52f, 0.52f, 0.96f, 0.96f, cell.cliffLayer, 0.85f, 0u);
                }
            };

            emitCliffFace(gx, gz - 1, {x0, 0.0f, z0}, {x1, 0.0f, z0}, { 0.0f, 0.0f, -1.0f}, { 1.0f, 0.0f,  0.0f});
            emitCliffFace(gx, gz + 1, {x1, 0.0f, z1}, {x0, 0.0f, z1}, { 0.0f, 0.0f,  1.0f}, {-1.0f, 0.0f,  0.0f});
            emitCliffFace(gx - 1, gz, {x0, 0.0f, z1}, {x0, 0.0f, z0}, {-1.0f, 0.0f,  0.0f}, { 0.0f, 0.0f, -1.0f});
            emitCliffFace(gx + 1, gz, {x1, 0.0f, z0}, {x1, 0.0f, z1}, { 1.0f, 0.0f,  0.0f}, { 0.0f, 0.0f,  1.0f});

            // 3. Animated Water Surface Plane
            if (cell.hasWater) {
                float wy = cell.waterY;
                emitQuad(worldVertices_,
                    {x0, wy, z0}, {x1, wy, z0}, {x1, wy, z1}, {x0, wy, z1},
                    {0.0f, 1.0f, 0.0f}, {1.0f, 0.0f, 0.0f},
                    {0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f},
                    0.95f, 0.95f, 0.95f, 0.95f, TexLayer::WaterSurface, 0.15f, 1u);
            }
        }
    }

    terrainTriCount_ = static_cast<uint32_t>(worldVertices_.size() / 3u);
    propTriCount_ = static_cast<uint32_t>(propVertices_.size() / 3u);

    worldVertices_.insert(worldVertices_.end(), propVertices_.begin(), propVertices_.end());

    RealmStats stats{};
    stats.terrainTriangles = terrainTriCount_;
    stats.propTriangles = propTriCount_;
    stats.spriteCount = static_cast<uint32_t>(sprites_.size());
    stats.pointLightCount = static_cast<uint32_t>(pointLights_.size());
    stats.atlasGenTimeMs = atlas_.generationTimeMs;
    return stats;
}

RaycastHit RealmWorld::raycast(Vec3f origin, Vec3f dir, float maxDist) const noexcept {
    RaycastHit result{};
    float step = 0.35f;
    for (float d = 1.0f; d < maxDist; d += step) {
        Vec3f p{origin.x + dir.x * d, origin.y + dir.y * d, origin.z + dir.z * d};
        float fx = (p.x / kTileSize) + static_cast<float>(kGridWidth) * 0.5f;
        float fz = (p.z / kTileSize) + static_cast<float>(kGridDepth) * 0.5f;
        if (fx >= 0.0f && fz >= 0.0f && fx < static_cast<float>(kGridWidth) && fz < static_cast<float>(kGridDepth)) {
            int gx = static_cast<int>(fx);
            int gz = static_cast<int>(fz);
            const TileCell& cell = getTile(gx, gz);
            float h = getTerrainHeightAt(p.x, p.z);
            if (p.y <= h) {
                result.hit = true;
                result.distance = d;
                result.position = p;
                result.layerId = cell.hasWater ? TexLayer::WaterSurface : cell.topLayer;
                return result;
            }
        }
    }
    return result;
}

void RealmWorld::buildStarfallValeRealm() {
    clear();

    // 1. Base Meadow & Multi-Tiered Highland Cliffs
    fillPlateau(0, 0, 63, 63, 4, TexLayer::GrassMeadow, TexLayer::CliffStone);

    // Northern mountain ridge backdrop (tiered cliffs from elev 8 up to 16)
    fillPlateau(0, 44, 63, 63, 8, TexLayer::GrassHighland, TexLayer::CliffStone);
    fillPlateau(4, 49, 59, 63, 12, TexLayer::GrassHighland, TexLayer::CliffStone);
    fillPlateau(12, 54, 52, 63, 16, TexLayer::CliffStone, TexLayer::CliffStone);

    // Western village terrace (elev 6) & Eastern sanctuary acropolis plateau (elev 8 -> 11)
    fillPlateau(4, 14, 24, 40, 6, TexLayer::GrassMeadow, TexLayer::CliffStone);
    fillPlateau(36, 16, 58, 42, 7, TexLayer::GrassHighland, TexLayer::CliffStone);
    fillPlateau(40, 22, 55, 38, 10, TexLayer::GrassHighland, TexLayer::CliffStone);

    // 2. Winding Crystalline River Canyon down the center (gx = 26..32)
    fillRiver(26, 0, 32, 63, 1, 1.35f);
    // Widen into a tranquil millpond at the southern foreground
    fillRiver(23, 4, 35, 14, 1, 1.35f);

    // 3. Cobblestone Village Plaza, Roads & Stepped Stone Staircases
    fillPlateau(10, 18, 23, 32, 6, TexLayer::Cobblestone, TexLayer::StoneBrick);
    fillPlateau(24, 23, 25, 26, 4, TexLayer::Cobblestone, TexLayer::StoneBrick);
    fillPlateau(33, 23, 35, 26, 4, TexLayer::Cobblestone, TexLayer::StoneBrick);

    // Stepped stone staircases connecting the river crossing to the Western Village & Eastern Highland
    addStairs(22, 23, 24, 26, 6, 4, SlopeDir::PlusX, TexLayer::Cobblestone);
    addStairs(35, 23, 38, 26, 4, 7, SlopeDir::PlusX, TexLayer::Cobblestone);
    addStairs(38, 27, 40, 30, 7, 10, SlopeDir::PlusX, TexLayer::Cobblestone);
    addStairs(15, 13, 18, 17, 4, 6, SlopeDir::PlusZ, TexLayer::Cobblestone);

    // Dirt woodland trail winding through the eastern ruins
    fillPlateau(41, 27, 49, 30, 10, TexLayer::DirtPath, TexLayer::CliffStone);

    // 4. 3D Architectural Props: Gabled Timber Tavern, Cottages, River Bridge, Archways & Lanterns
    // Grand Wayfarer's Tavern overlooking the Western Cobblestone Plaza
    addGabledHouse(
        {-20.5f, 6.0f * kElevStep, -1.5f},
        {8.6f, 4.4f, 6.4f},
        0,
        TexLayer::TimberWall,
        TexLayer::TerracottaRoof,
        true,
        true
    );

    // Cozy Apothecary Cottage (North-West plaza)
    addGabledHouse(
        {-22.0f, 6.0f * kElevStep, 8.5f},
        {6.2f, 3.8f, 5.4f},
        1,
        TexLayer::StuccoPlaster,
        TexLayer::SlateRoof,
        true,
        true
    );

    // Blacksmith & Guildhouse (South-West terrace)
    addGabledHouse(
        {-11.5f, 6.0f * kElevStep, -9.5f},
        {6.6f, 3.9f, 5.2f},
        0,
        TexLayer::TimberWall,
        TexLayer::TerracottaRoof,
        true,
        true
    );

    // Riverside Watermill Watch-Cottage (East bank)
    addGabledHouse(
        {11.0f, 7.0f * kElevStep, -12.0f},
        {6.0f, 3.8f, 5.2f},
        0,
        TexLayer::TimberWall,
        TexLayer::SlateRoof,
        false,
        true
    );

    // Arched Wooden Plank Bridge spanning the central river gorge
    addWoodenBridge(
        {-9.5f, 4.0f * kElevStep, -10.5f},
        { 3.5f, 4.0f * kElevStep, -10.5f},
        3.6f
    );

    // Highland Sanctuary Stone Archways & Ruined Pillars (East Plateau)
    float highY = 10.0f * kElevStep;
    addStoneArchway({19.5f, highY, -4.5f}, 5.0f, 5.4f, 1);
    addStoneArchway({25.5f, highY, -4.5f}, 5.0f, 5.4f, 1);
    addStoneArchway({22.5f, highY,  2.5f}, 5.6f, 6.0f, 0);

    // Warm Street Lanterns along the bridge, stairs, and plaza
    addStreetLantern({-10.2f, 4.0f * kElevStep, -12.8f});
    addStreetLantern({  3.8f, 4.0f * kElevStep, -12.8f});
    addStreetLantern({-15.5f, 6.0f * kElevStep,  -5.5f});
    addStreetLantern({-14.0f, 6.0f * kElevStep,   3.5f});
    addStreetLantern({  9.5f, 7.0f * kElevStep,  -6.5f});
    addStreetLantern({ 16.5f, highY,             -6.8f});

    // 5. 2.5D Shadow-Casting & Normal-Mapped Billboards (`Sprite3D`)
    // Hero Party crossing the wooden bridge & plaza
    spawnSprite({-3.2f, 4.0f * kElevStep + 0.65f, -10.5f}, 1.95f, 2.45f, TexLayer::SpriteHero, SpriteFlags::IdleBob, 0.0f);
    spawnSpriteGrounded(-14.5f, -3.2f, 1.90f, 2.40f, TexLayer::SpriteHero, SpriteFlags::IdleBob | SpriteFlags::FlipX, 1.2f);

    // Villagers & Traveling Merchant in the town square
    spawnSpriteGrounded(-17.8f, -4.5f, 1.80f, 2.25f, TexLayer::SpriteVillager, SpriteFlags::IdleBob, 0.7f);
    spawnSpriteGrounded(-12.2f, -1.8f, 1.80f, 2.25f, TexLayer::SpriteVillager, SpriteFlags::IdleBob | SpriteFlags::FlipX, 2.1f);
    spawnSpriteGrounded(-18.5f,  3.2f, 1.95f, 2.35f, TexLayer::SpriteMerchant, SpriteFlags::IdleBob, 3.4f);

    // Town Guards watching the bridge & eastern sanctuary stairs
    spawnSpriteGrounded(-10.8f, -8.2f, 1.95f, 2.45f, TexLayer::SpriteGuard, SpriteFlags::IdleBob, 0.4f);
    spawnSpriteGrounded(  7.2f, -8.5f, 1.95f, 2.45f, TexLayer::SpriteGuard, SpriteFlags::IdleBob | SpriteFlags::FlipX, 1.9f);

    // Monsters lurking in the Eastern Highland Ruins
    spawnSpriteGrounded(18.5f, -3.5f, 1.75f, 1.85f, TexLayer::SpriteSlime, SpriteFlags::IdleBob, 0.5f);
    spawnSpriteGrounded(21.2f, -1.2f, 1.75f, 1.85f, TexLayer::SpriteSlime, SpriteFlags::IdleBob | SpriteFlags::FlipX, 2.3f);
    spawnSpriteGrounded(23.5f, -5.2f, 2.35f, 2.20f, TexLayer::SpriteWolf,  SpriteFlags::IdleBob | SpriteFlags::FlipX, 1.1f);

    // Glowing Torch Braziers flanking the Highland Sanctuary Archway
    spawnSprite({17.2f, highY, -2.2f}, 1.4f, 1.9f, TexLayer::SpriteTorchFlame, SpriteFlags::Emissive | SpriteFlags::WindSway, 0.3f);
    spawnSprite({24.8f, highY, -2.2f}, 1.4f, 1.9f, TexLayer::SpriteTorchFlame, SpriteFlags::Emissive | SpriteFlags::WindSway, 1.7f);
    addPointLight({17.2f, highY + 1.4f, -2.2f}, {1.0f, 0.52f, 0.18f}, 14.0f, 2.1f);
    addPointLight({24.8f, highY + 1.4f, -2.2f}, {1.0f, 0.52f, 0.18f}, 14.0f, 2.1f);

    // 6. Lush 2.5D Swaying Pine Trees, Golden-Green Oak Trees & Wildflower Clusters
    for (int i = 0; i < 68; ++i) {
        float rx = (PixelCanvas::hash2D(i, 11, 777u) - 0.5f) * 82.0f;
        float rz = (PixelCanvas::hash2D(i, 23, 777u) - 0.42f) * 76.0f;

        // Keep the central river channel, bridge, and main plaza clear for clean diorama sightlines
        if (std::abs(rx + 3.0f) < 6.5f) continue;
        if (rx > -26.0f && rx < -8.0f && rz > -13.0f && rz < 12.0f) continue;
        if (rx > 8.0f && rx < 28.0f && rz > -15.0f && rz < 5.0f) continue;

        uint16_t treeLayer = (rz > 6.0f || (i % 3 == 0)) ? TexLayer::SpritePineTree : TexLayer::SpriteOakTree;
        float scale = 0.88f + PixelCanvas::hash2D(i, 37, 777u) * 0.42f;
        float w = (treeLayer == TexLayer::SpritePineTree ? 3.8f : 4.4f) * scale;
        float h = (treeLayer == TexLayer::SpritePineTree ? 5.4f : 5.0f) * scale;
        uint32_t flags = SpriteFlags::WindSway | ((i & 1) ? SpriteFlags::FlipX : 0u);
        spawnSpriteGrounded(rx, rz, w, h, treeLayer, flags, static_cast<float>(i) * 0.73f);
    }

    // Wildflower tufts along the meadows and riverbanks
    for (int i = 0; i < 45; ++i) {
        float fx = (PixelCanvas::hash2D(i, 51, 999u) - 0.5f) * 64.0f;
        float fz = (PixelCanvas::hash2D(i, 67, 999u) - 0.5f) * 54.0f;
        if (std::abs(fx + 3.0f) < 5.2f) continue;
        spawnSpriteGrounded(fx, fz, 1.35f, 1.25f, TexLayer::SpriteWildflower, SpriteFlags::WindSway, static_cast<float>(i) * 0.41f);
    }

    // 7. Signature Octopath / Lumina Tilt-Shift Diorama Camera & Golden-Hour Dusk Atmosphere
    camera_.position      = {-4.5f, 24.5f, -39.0f};
    camera_.target        = {-2.5f,  4.8f,  -4.5f};
    camera_.fovDegrees    = 28.0f;
    camera_.focusDistance = 39.5f;
    camera_.focusRange    = 15.5f;
    camera_.bokehRadius   = 8.0f;

    atmosphere_.sunDirection = {-0.56f, 0.45f, -0.69f};
    atmosphere_.sunColor     = {1.58f, 1.08f, 0.70f};
    atmosphere_.fogDensity   = 0.0080f;
    atmosphere_.exposure     = 1.16f;
}

} // namespace eatsrealms
