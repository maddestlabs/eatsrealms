#include "eatsrealms/world_generator.hpp"
#include "eatsrealms/history_sim.hpp"
#include "eatsrealms/realm_chunk.hpp"
#include "eatsrealms/realm_world.hpp"
#include <cmath>
#include <algorithm>
#include <vector>

namespace eatsrealms {

namespace {

// Deterministic 1D/2D hash functions
inline float hash1D(uint32_t x, uint32_t seed) noexcept {
    uint32_t h = x * 374761393u + seed * 1442695041u;
    h = (h ^ (h >> 13u)) * 1274126177u;
    return static_cast<float>((h ^ (h >> 16u)) & 0x00FFFFFFu) / static_cast<float>(0x01000000u);
}

inline float hash2D(int x, int y, uint32_t seed) noexcept {
    return PixelCanvas::hash2D(x, y, seed);
}

// 2D fractal Brownian motion for macro terrain & biome masks
inline float fbm(float x, float y, int octaves, uint32_t seed) noexcept {
    return PixelCanvas::fbm2D(x, y, octaves, seed);
}

// Stepped quantizer to produce crisp HD-2D diorama terraces rather than noisy slopes
inline int16_t quantizeElevation(float continuousHeight) noexcept {
    if (continuousHeight < 2.5f) return 2;
    if (continuousHeight < 5.0f) return 4;
    if (continuousHeight < 7.0f) return 6;
    if (continuousHeight < 9.0f) return 8;
    if (continuousHeight < 11.5f) return 10;
    if (continuousHeight < 14.0f) return 12;
    return 16;
}

struct Point2D {
    float x;
    float z;
};

// Check if a point is within camera sightline cone of a key interactive landmark
// Camera is at (-4.5, 24.5, -39.0) looking north towards target (-2.5, 4.8, -4.5).
// Trees south of entities (smaller Z) within a narrow angle directly block the player view.
inline bool blocksCameraSightline(float treeX, float treeZ, const std::vector<Point2D>& landmarks) noexcept {
    constexpr float kMaxBlockingDist = 6.8f;
    for (const auto& lm : landmarks) {
        float dz = lm.z - treeZ;
        // Tree is south of the landmark (in front of it from camera perspective)
        if (dz > 0.4f && dz < kMaxBlockingDist) {
            float dx = std::abs(treeX - lm.x);
            // Check cone of occlusion
            if (dx < dz * 0.45f + 0.8f) {
                return true;
            }
        }
    }
    return false;
}

inline void emitChunkBox(
    std::vector<WorldVertex>& dst,
    Vec3f bMin, Vec3f bMax,
    uint32_t texLayer, float uvScale = 0.5f, float roughness = 0.78f, uint32_t flags = 0u
) {
    auto emitQ = [&](Vec3f p0, Vec3f p1, Vec3f p2, Vec3f p3, Vec3f n, Vec3f t, float uSpan, float vSpan, float ao) {
        WorldVertex v[4]{};
        for (int i = 0; i < 4; ++i) {
            v[i].normal[0] = n.x; v[i].normal[1] = n.y; v[i].normal[2] = n.z;
            v[i].tangent[0] = t.x; v[i].tangent[1] = t.y; v[i].tangent[2] = t.z;
            v[i].texLayer = texLayer;
            v[i].roughness = roughness;
            v[i].flags = flags;
            v[i].ao = ao;
        }
        v[0].pos[0] = p0.x; v[0].pos[1] = p0.y; v[0].pos[2] = p0.z; v[0].uv[0] = 0.0f;  v[0].uv[1] = vSpan;
        v[1].pos[0] = p1.x; v[1].pos[1] = p1.y; v[1].pos[2] = p1.z; v[1].uv[0] = uSpan; v[1].uv[1] = vSpan;
        v[2].pos[0] = p2.x; v[2].pos[1] = p2.y; v[2].pos[2] = p2.z; v[2].uv[0] = uSpan; v[2].uv[1] = 0.0f;
        v[3].pos[0] = p3.x; v[3].pos[1] = p3.y; v[3].pos[2] = p3.z; v[3].uv[0] = 0.0f;  v[3].uv[1] = 0.0f;

        dst.push_back(v[0]); dst.push_back(v[1]); dst.push_back(v[2]);
        dst.push_back(v[0]); dst.push_back(v[2]); dst.push_back(v[3]);
    };

    float dx = (bMax.x - bMin.x) * uvScale;
    float dy = (bMax.y - bMin.y) * uvScale;
    float dz = (bMax.z - bMin.z) * uvScale;

    emitQ({bMin.x, bMin.y, bMin.z}, {bMax.x, bMin.y, bMin.z}, {bMax.x, bMax.y, bMin.z}, {bMin.x, bMax.y, bMin.z}, {0.0f, 0.0f, -1.0f}, {1.0f, 0.0f, 0.0f}, dx, dy, 0.85f);
    emitQ({bMax.x, bMin.y, bMax.z}, {bMin.x, bMin.y, bMax.z}, {bMin.x, bMax.y, bMax.z}, {bMax.x, bMax.y, bMax.z}, {0.0f, 0.0f, 1.0f}, {-1.0f, 0.0f, 0.0f}, dx, dy, 0.85f);
    emitQ({bMin.x, bMin.y, bMax.z}, {bMin.x, bMin.y, bMin.z}, {bMin.x, bMax.y, bMin.z}, {bMin.x, bMax.y, bMax.z}, {-1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, -1.0f}, dz, dy, 0.85f);
    emitQ({bMax.x, bMin.y, bMin.z}, {bMax.x, bMin.y, bMax.z}, {bMax.x, bMax.y, bMax.z}, {bMax.x, bMax.y, bMin.z}, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, dz, dy, 0.85f);
    emitQ({bMin.x, bMax.y, bMin.z}, {bMax.x, bMax.y, bMin.z}, {bMax.x, bMax.y, bMax.z}, {bMin.x, bMax.y, bMax.z}, {0.0f, 1.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, dx, dz, 0.95f);
}

inline void emitChunkCrystalSpire(
    std::vector<WorldVertex>& dst,
    Vec3f basePos, float width, float height, float tiltAngleX, float tiltAngleZ,
    uint32_t texLayer
) {
    float sinX = std::sin(tiltAngleX), cosX = std::cos(tiltAngleX);
    float sinZ = std::sin(tiltAngleZ), cosZ = std::cos(tiltAngleZ);

    Vec3f apex = {
        basePos.x + height * sinZ,
        basePos.y + height * (cosX * cosZ),
        basePos.z + height * sinX
    };

    float hw = width * 0.5f;
    Vec3f b0 = {basePos.x - hw, basePos.y, basePos.z - hw};
    Vec3f b1 = {basePos.x + hw, basePos.y, basePos.z - hw};
    Vec3f b2 = {basePos.x + hw, basePos.y, basePos.z + hw};
    Vec3f b3 = {basePos.x - hw, basePos.y, basePos.z + hw};

    auto emitTri = [&](Vec3f p0, Vec3f p1, Vec3f p2, Vec3f n) {
        WorldVertex v[3]{};
        for (int i = 0; i < 3; ++i) {
            v[i].normal[0] = n.x; v[i].normal[1] = n.y; v[i].normal[2] = n.z;
            v[i].tangent[0] = 1.0f; v[i].tangent[1] = 0.0f; v[i].tangent[2] = 0.0f;
            v[i].texLayer = texLayer;
            v[i].roughness = 0.20f;
            v[i].flags = 2u; // Emissive flag
            v[i].ao = 0.95f;
        }
        v[0].pos[0] = p0.x; v[0].pos[1] = p0.y; v[0].pos[2] = p0.z; v[0].uv[0] = 0.0f; v[0].uv[1] = 1.0f;
        v[1].pos[0] = p1.x; v[1].pos[1] = p1.y; v[1].pos[2] = p1.z; v[1].uv[0] = 1.0f; v[1].uv[1] = 1.0f;
        v[2].pos[0] = p2.x; v[2].pos[1] = p2.y; v[2].pos[2] = p2.z; v[2].uv[0] = 0.5f; v[2].uv[1] = 0.0f;
        dst.push_back(v[0]); dst.push_back(v[1]); dst.push_back(v[2]);
    };

    emitTri(b0, b1, apex, {0.0f, 0.4f, -0.9f});
    emitTri(b1, b2, apex, {0.9f, 0.4f, 0.0f});
    emitTri(b2, b3, apex, {0.0f, 0.4f, 0.9f});
    emitTri(b3, b0, apex, {-0.9f, 0.4f, 0.0f});
}

inline PointLight makeChunkPointLight(Vec3f pos, Vec3f color, float radius, float intensity) {
    PointLight pl{};
    pl.position[0] = pos.x;
    pl.position[1] = pos.y;
    pl.position[2] = pos.z;
    pl.radius = radius;
    pl.color[0] = color.x;
    pl.color[1] = color.y;
    pl.color[2] = color.z;
    pl.intensity = intensity;
    return pl;
}

} // namespace

void ProceduralWorldGenerator::generate(RealmWorld& world, const ProceduralRealmConfig& config) {
    world.clear();

    const uint32_t seed = config.seed;
    const int gridW = RealmWorld::kGridWidth;
    const int gridD = RealmWorld::kGridDepth;

    // Track landmark locations (doors, NPCs, bridge entrances) for camera sightline culling
    std::vector<Point2D> sightlineLandmarks;

    // =========================================================================
    // PASS 1: Multi-Tier Topography & Relief Pass
    // =========================================================================
    // Natural rolling terrain with:
    //  - Northern mountain ridge backdrop rising up to elevation 16
    //  - Western Village Terrace at elevation 6
    //  - Eastern Sanctuary Acropolis Plateau at elevation 7..10
    //  - Central River Basin at elevation 4
    for (int gz = 0; gz < gridD; ++gz) {
        for (int gx = 0; gx < gridW; ++gx) {
            float nx = static_cast<float>(gx) / static_cast<float>(gridW);
            float nz = static_cast<float>(gz) / static_cast<float>(gridD);

            // Base gentle meadow rolling noise
            float baseNoise = fbm(nx * 3.2f, nz * 3.2f, 3, seed) * 2.8f + 3.2f;

            // Northern mountain ridge backdrop (gz > 40)
            float northFactor = std::clamp((static_cast<float>(gz) - 38.0f) / 22.0f, 0.0f, 1.0f);
            float mountainBoost = northFactor * northFactor * 13.5f + fbm(nx * 6.0f, nz * 6.0f, 4, seed + 101u) * 3.5f * northFactor;

            // Western Village Terrace leveling (gx: 4..24, gz: 14..40)
            float distToPlaza = std::sqrt((gx - 14.0f) * (gx - 14.0f) + (gz - 27.0f) * (gz - 27.0f));
            float villagePlazaMask = 1.0f - std::clamp(distToPlaza / 16.0f, 0.0f, 1.0f);

            // Eastern Highland Sanctuary leveling (gx: 36..56, gz: 16..42)
            float distToSanctuary = std::sqrt((gx - 46.0f) * (gx - 46.0f) + (gz - 29.0f) * (gz - 29.0f));
            float sanctuaryMask = 1.0f - std::clamp(distToSanctuary / 14.0f, 0.0f, 1.0f);

            float rawHeight = baseNoise + mountainBoost;
            if (villagePlazaMask > 0.1f && gz < 44) {
                rawHeight = rawHeight * (1.0f - villagePlazaMask) + 6.2f * villagePlazaMask;
            }
            if (sanctuaryMask > 0.1f && gz < 44) {
                float plateauH = (distToSanctuary < 7.0f) ? 10.2f : 7.4f;
                rawHeight = rawHeight * (1.0f - sanctuaryMask) + plateauH * sanctuaryMask;
            }

            int16_t elev = quantizeElevation(rawHeight);

            // Assign natural biome top layers
            uint16_t topLayer = TexLayer::GrassMeadow;
            if (elev >= 12) {
                topLayer = TexLayer::CliffStone;
            } else if (elev >= 7) {
                topLayer = TexLayer::GrassHighland;
            }

            world.setTile(gx, gz, elev, topLayer, TexLayer::CliffStone);
        }
    }

    // Ensure dedicated flat village terrace & eastern sanctuary plateaus for stable placement
    world.fillPlateau(4, 14, 24, 40, config.villageElevation, TexLayer::GrassMeadow, TexLayer::CliffStone);
    world.fillPlateau(36, 16, 58, 42, 7, TexLayer::GrassHighland, TexLayer::CliffStone);
    world.fillPlateau(40, 22, 55, 38, config.sanctuaryElevation, TexLayer::GrassHighland, TexLayer::CliffStone);

    // Northern mountain tiers
    world.fillPlateau(0, 44, 63, 63, 8, TexLayer::GrassHighland, TexLayer::CliffStone);
    world.fillPlateau(4, 49, 59, 63, 12, TexLayer::GrassHighland, TexLayer::CliffStone);
    world.fillPlateau(12, 54, 52, 63, config.mountainMaxElevation, TexLayer::CliffStone, TexLayer::CliffStone);

    // =========================================================================
    // PASS 2: Hydrology & River Meander Pass
    // =========================================================================
    // Winding river flowing south from the mountains, widening into a millpond
    if (config.enableRiver) {
        float riverSeedOffset = hash1D(seed, 42u) * 6.28f;
        for (int gz = 0; gz < gridD; ++gz) {
            // Meander formula: sine curve + noise perturbation
            float meander = std::sin(static_cast<float>(gz) * 0.11f + riverSeedOffset) * config.riverWiggle +
                            (fbm(static_cast<float>(gz) * 0.08f, 0.0f, 2, seed + 202u) - 0.5f) * 2.8f;
            int centerX = 29 + static_cast<int>(std::round(meander));

            // River half-width (widens in southern foreground pond)
            int halfW = 3;
            if (gz >= config.pondStartZ && gz <= config.pondEndZ) {
                halfW = 5 + static_cast<int>(std::sin(static_cast<float>(gz - config.pondStartZ) / static_cast<float>(config.pondEndZ - config.pondStartZ) * 3.14159f) * 2.5f);
            }

            int x0 = std::clamp(centerX - halfW, 1, gridW - 2);
            int x1 = std::clamp(centerX + halfW, 1, gridW - 2);

            for (int gx = x0; gx <= x1; ++gx) {
                world.setTile(gx, gz, 1, TexLayer::SandShore, TexLayer::CliffStone);
                // Mark water cell
                TileCell cell = world.getTile(gx, gz);
                cell.elevation = 1;
                cell.hasWater = true;
                cell.waterY = 1.35f;
                cell.topLayer = TexLayer::SandShore;
                // Update directly in world
                world.fillRiver(gx, gz, gx, gz, 1, 1.35f);
            }

            // Shoreline sand borders
            if (x0 - 1 >= 0) {
                const auto& c = world.getTile(x0 - 1, gz);
                if (c.elevation <= 4 && !c.hasWater) {
                    world.setTile(x0 - 1, gz, c.elevation, TexLayer::SandShore, c.cliffLayer);
                }
            }
            if (x1 + 1 < gridW) {
                const auto& c = world.getTile(x1 + 1, gz);
                if (c.elevation <= 4 && !c.hasWater) {
                    world.setTile(x1 + 1, gz, c.elevation, TexLayer::SandShore, c.cliffLayer);
                }
            }
        }
    }

    // =========================================================================
    // PASS 3: Road & Path Network Pass
    // =========================================================================
    // Cobblestone Village Plaza & thoroughfares on western terrace
    world.fillPlateau(10, 18, 23, 32, config.villageElevation, TexLayer::Cobblestone, TexLayer::StoneBrick);
    world.fillPlateau(24, 23, 25, 26, 4, TexLayer::Cobblestone, TexLayer::StoneBrick);
    world.fillPlateau(33, 23, 35, 26, 4, TexLayer::Cobblestone, TexLayer::StoneBrick);

    // Eastern highland trail winding through sanctuary ruins
    world.fillPlateau(41, 27, 49, 30, config.sanctuaryElevation, TexLayer::DirtPath, TexLayer::CliffStone);

    // =========================================================================
    // PASS 4: Automated Cliff Staircases
    // =========================================================================
    // Stepped stone staircases connecting river crossing to West Village & East Highland
    world.addStairs(22, 23, 24, 26, 6, 4, SlopeDir::PlusX, TexLayer::Cobblestone);
    world.addStairs(35, 23, 38, 26, 4, 7, SlopeDir::PlusX, TexLayer::Cobblestone);
    world.addStairs(38, 27, 40, 30, 7, 10, SlopeDir::PlusX, TexLayer::Cobblestone);
    world.addStairs(15, 13, 18, 17, 4, 6, SlopeDir::PlusZ, TexLayer::Cobblestone);

    // =========================================================================
    // PASS 5: Village Architecture & Compound Perimeter Dressing
    // =========================================================================
    const float elevStep = RealmWorld::kElevStep;
    const float villageY = static_cast<float>(config.villageElevation) * elevStep;

    // Grand Wayfarer's Tavern overlooking the Western Plaza
    Vec3f tavernPos{-20.5f, villageY, -1.5f};
    world.addGabledHouse(tavernPos, {8.6f, 4.4f, 6.4f}, 0, TexLayer::TimberWall, TexLayer::TerracottaRoof, true, true);
    sightlineLandmarks.push_back({tavernPos.x, tavernPos.z - 3.4f}); // Tavern door
    if (config.dressPerimeters) {
        world.addCrateStack({tavernPos.x - 4.6f, villageY, tavernPos.z - 1.2f}, 3);
        world.addBarrel({tavernPos.x - 4.5f, villageY, tavernPos.z + 1.4f});
        world.addBarrel({tavernPos.x - 4.5f, villageY, tavernPos.z + 2.2f});
        world.addFlowerBox({tavernPos.x + 2.8f, villageY + 1.2f, tavernPos.z - 3.3f}, 1.8f, 0);
    }

    // Cozy Apothecary Cottage (North-West plaza)
    Vec3f apothecaryPos{-22.0f, villageY, 8.5f};
    world.addGabledHouse(apothecaryPos, {6.2f, 3.8f, 5.4f}, 1, TexLayer::StuccoPlaster, TexLayer::SlateRoof, true, true);
    sightlineLandmarks.push_back({apothecaryPos.x, apothecaryPos.z - 2.8f});
    if (config.dressPerimeters) {
        world.addFlowerBox({apothecaryPos.x - 1.8f, villageY + 1.1f, apothecaryPos.z - 2.85f}, 1.6f, 0);
        world.addFlowerBox({apothecaryPos.x + 1.8f, villageY + 1.1f, apothecaryPos.z - 2.85f}, 1.6f, 0);
        world.addCrateStack({apothecaryPos.x + 3.4f, villageY, apothecaryPos.z + 0.5f}, 2);
    }

    // Blacksmith & Guildhouse (South-West terrace)
    Vec3f smithPos{-11.5f, villageY, -9.5f};
    world.addGabledHouse(smithPos, {6.6f, 3.9f, 5.2f}, 0, TexLayer::TimberWall, TexLayer::TerracottaRoof, true, true);
    sightlineLandmarks.push_back({smithPos.x, smithPos.z - 2.8f});
    if (config.dressPerimeters) {
        world.addCrateStack({smithPos.x + 3.6f, villageY, smithPos.z - 1.0f}, 3);
        world.addBarrel({smithPos.x + 3.6f, villageY, smithPos.z + 1.0f});
    }

    // Riverside Watermill Watch-Cottage (East bank, elev 7)
    float eastBankY = 7.0f * elevStep;
    Vec3f millPos{11.0f, eastBankY, -12.0f};
    world.addGabledHouse(millPos, {6.0f, 3.8f, 5.2f}, 0, TexLayer::TimberWall, TexLayer::SlateRoof, false, true);
    sightlineLandmarks.push_back({millPos.x, millPos.z - 2.8f});
    if (config.dressPerimeters) {
        world.addCrateStack({millPos.x - 3.4f, eastBankY, millPos.z}, 2);
        world.addBarrel({millPos.x - 3.4f, eastBankY, millPos.z + 1.2f});
    }

    // Market Stall in Western Plaza
    if (config.addMarketStalls) {
        Vec3f stallPos{-15.5f, villageY, 3.5f};
        world.addMarketStall(stallPos, 3.8f, 2.6f, TexLayer::TerracottaRoof);
        sightlineLandmarks.push_back({stallPos.x, stallPos.z});
        world.addCrateStack({stallPos.x + 2.4f, villageY, stallPos.z - 0.2f}, 2);
        world.addBarrel({stallPos.x - 2.4f, villageY, stallPos.z - 0.2f});
    }

    // =========================================================================
    // PASS 6: Wooden River Bridge & Street Lantern Network
    // =========================================================================
    float bridgeY = 4.0f * elevStep;
    Vec3f bridgeStart{-9.5f, bridgeY, -10.5f};
    Vec3f bridgeEnd{ 3.5f, bridgeY, -10.5f};
    world.addWoodenBridge(bridgeStart, bridgeEnd, 3.6f);

    sightlineLandmarks.push_back({bridgeStart.x, bridgeStart.z});
    sightlineLandmarks.push_back({bridgeEnd.x, bridgeEnd.z});
    sightlineLandmarks.push_back({(bridgeStart.x + bridgeEnd.x) * 0.5f, bridgeStart.z});

    // Street Lanterns along bridge, stairs, and plaza
    world.addStreetLantern({-10.2f, bridgeY, -12.8f});
    world.addStreetLantern({  3.8f, bridgeY, -12.8f});
    world.addStreetLantern({-15.5f, villageY,  -5.5f});
    world.addStreetLantern({-14.0f, villageY,   3.5f});
    world.addStreetLantern({  9.5f, eastBankY, -6.5f});

    float highY = static_cast<float>(config.sanctuaryElevation) * elevStep;
    world.addStreetLantern({ 16.5f, highY, -6.8f});

    // =========================================================================
    // PASS 7: Highland Ancient Sanctuary Ruins
    // =========================================================================
    if (config.addHighlandRuins) {
        world.addStoneArchway({19.5f, highY, -4.5f}, 5.0f, 5.4f, 1);
        world.addStoneArchway({25.5f, highY, -4.5f}, 5.0f, 5.4f, 1);
        world.addStoneArchway({22.5f, highY,  2.5f}, 5.6f, 6.0f, 0);

        // Glowing Torch Braziers with dynamic point lights
        world.spawnSprite({17.2f, highY, -2.2f}, 1.4f, 1.9f, TexLayer::SpriteTorchFlame, SpriteFlags::Emissive | SpriteFlags::WindSway, 0.3f);
        world.spawnSprite({24.8f, highY, -2.2f}, 1.4f, 1.9f, TexLayer::SpriteTorchFlame, SpriteFlags::Emissive | SpriteFlags::WindSway, 1.7f);
        world.addPointLight({17.2f, highY + 1.4f, -2.2f}, {1.0f, 0.52f, 0.18f}, 14.0f, 2.1f);
        world.addPointLight({24.8f, highY + 1.4f, -2.2f}, {1.0f, 0.52f, 0.18f}, 14.0f, 2.1f);
    }

    // =========================================================================
    // PASS 8: Biome-Aware Flora with Camera Sightline Culling
    // =========================================================================
    // Lumina-inspired camera clearance pass:
    // Any tree whose crown would block the player's view of an NPC, doorway,
    // or bridge entrance from the south-facing camera is pruned or converted to flowers.
    int placedTrees = 0;
    for (int i = 0; i < config.treeCount * 2 && placedTrees < config.treeCount; ++i) {
        float rx = (hash2D(i, 11, seed + 500u) - 0.5f) * 86.0f;
        float rz = (hash2D(i, 23, seed + 500u) - 0.42f) * 80.0f;

        // Keep river channel, bridge, and village plaza center clear
        if (std::abs(rx + 3.0f) < 6.5f) continue;
        if (rx > -26.0f && rx < -8.0f && rz > -13.0f && rz < 12.0f) continue;
        if (rx > 8.0f && rx < 28.0f && rz > -15.0f && rz < 5.0f) continue;

        // Camera sightline culling
        if (config.enableSightlineCulling && blocksCameraSightline(rx, rz, sightlineLandmarks)) {
            // Replace with low wildflower cluster so sightline remains open
            world.spawnSpriteGrounded(rx, rz, 1.35f, 1.25f, TexLayer::SpriteWildflower, SpriteFlags::WindSway, static_cast<float>(i) * 0.45f);
            continue;
        }

        // Biome tree selection: 3D Conifer Pines on northern ridges/elevations; 3D Oaks & Autumn trees on lowland meadows
        float scale = 0.88f + hash2D(i, 37, seed + 500u) * 0.42f;
        if (rz > 4.0f || (i % 3 == 0)) {
            world.addPineTree3DGrounded(rx, rz, scale, static_cast<float>(i) * 1.25f);
        } else if (i % 5 == 0) {
            world.addAutumnTree3DGrounded(rx, rz, scale, static_cast<float>(i) * 0.73f);
        } else {
            world.addOakTree3DGrounded(rx, rz, scale, static_cast<float>(i) * 0.73f);
        }
        placedTrees++;
    }

    // Wildflower clusters in meadows and riverbanks
    for (int i = 0; i < config.wildflowerCount; ++i) {
        float fx = (hash2D(i, 51, seed + 777u) - 0.5f) * 72.0f;
        float fz = (hash2D(i, 67, seed + 777u) - 0.5f) * 62.0f;
        if (std::abs(fx + 3.0f) < 4.8f) continue;
        world.spawnSpriteGrounded(fx, fz, 1.35f, 1.25f, TexLayer::SpriteWildflower, SpriteFlags::WindSway, static_cast<float>(i) * 0.41f);
    }

    // Lush Octopath / Lumina style procedural grass tufts, tall grass, and flower bouquets
    world.scatterMeadowVegetation(1.0f, 0.32f, seed + 888u);

    // =========================================================================
    // PASS 9: Living Entities, NPCs & Dynamic Lighting
    // =========================================================================
    // Hero party crossing bridge & exploring village
    if (config.spawnHeroParty) {
        world.spawnSprite({-3.2f, bridgeY + 0.65f, -10.5f}, 1.95f, 2.45f, TexLayer::SpriteHero, SpriteFlags::IdleBob, 0.0f);
        world.spawnSpriteGrounded(-14.5f, -3.2f, 1.90f, 2.40f, TexLayer::SpriteHero, SpriteFlags::IdleBob | SpriteFlags::FlipX, 1.2f);
    }

    // Villagers & Merchant in town square
    if (config.spawnVillagers) {
        world.spawnSpriteGrounded(-17.8f, -4.5f, 1.80f, 2.25f, TexLayer::SpriteVillager, SpriteFlags::IdleBob, 0.7f);
        world.spawnSpriteGrounded(-12.2f, -1.8f, 1.80f, 2.25f, TexLayer::SpriteVillager, SpriteFlags::IdleBob | SpriteFlags::FlipX, 2.1f);
        world.spawnSpriteGrounded(-18.5f,  3.2f, 1.95f, 2.35f, TexLayer::SpriteMerchant, SpriteFlags::IdleBob, 3.4f);

        // Town guards watching bridge and sanctuary stairs
        world.spawnSpriteGrounded(-10.8f, -8.2f, 1.95f, 2.45f, TexLayer::SpriteGuard, SpriteFlags::IdleBob, 0.4f);
        world.spawnSpriteGrounded(  7.2f, -8.5f, 1.95f, 2.45f, TexLayer::SpriteGuard, SpriteFlags::IdleBob | SpriteFlags::FlipX, 1.9f);
    }

    // Monsters lurking in Eastern Highland Ruins
    if (config.spawnMonsters) {
        world.spawnSpriteGrounded(18.5f, -3.5f, 1.75f, 1.85f, TexLayer::SpriteSlime, SpriteFlags::IdleBob, 0.5f);
        world.spawnSpriteGrounded(21.2f, -1.2f, 1.75f, 1.85f, TexLayer::SpriteSlime, SpriteFlags::IdleBob | SpriteFlags::FlipX, 2.3f);
        world.spawnSpriteGrounded(23.5f, -5.2f, 2.35f, 2.20f, TexLayer::SpriteWolf,  SpriteFlags::IdleBob | SpriteFlags::FlipX, 1.1f);
    }

    // =========================================================================
    // PASS 10: HD-2D Tilt-Shift Diorama Camera & Golden-Hour Dusk Atmosphere
    // =========================================================================
    auto& cam = world.camera();
    cam.position      = {-4.5f, 24.5f, -39.0f};
    cam.target        = {-2.5f,  4.8f,  -4.5f};
    cam.fovDegrees    = 28.0f;
    cam.focusDistance = 39.5f;
    cam.focusRange    = 15.5f;
    cam.bokehRadius   = 8.0f;

    auto& atmos = world.atmosphere();
    atmos.sunDirection = {-0.56f, 0.45f, -0.69f};
    atmos.sunColor     = {1.58f, 1.08f, 0.70f};
    atmos.fogDensity   = 0.0080f;
    atmos.exposure     = 1.16f;
}

void ProceduralWorldGenerator::generateWoodlandTrail(RealmWorld& world, uint32_t seed) {
    world.clear();

    const int gridW = RealmWorld::kGridWidth;
    const int gridD = RealmWorld::kGridDepth;

    // 1. Organic Stepped Contour Topography & Sunken Forest Brook
    // Natural valley trail curving diagonally across the diorama, flanked by stepped contour knolls
    for (int gz = 0; gz < gridD; ++gz) {
        // Diagonal meandering path matching the screenshot's composition
        float trailCenter = 26.5f + (static_cast<float>(gz) * 0.20f) + std::sin(static_cast<float>(gz) * 0.14f) * 3.2f;
        float trailHalfW = 1.75f;

        for (int gx = 0; gx < gridW; ++gx) {
            float distFromTrail = static_cast<float>(gx) - trailCenter;
            float absDist = std::abs(distFromTrail);

            // Natural organic terrace knoll calculation
            float noiseFactor = fbm(static_cast<float>(gx) * 0.09f, static_cast<float>(gz) * 0.09f, 3, seed + 77u) * 2.6f;
            float contourDist = absDist + noiseFactor;

            int16_t elev = 2;
            if (contourDist < trailHalfW + 0.8f) {
                elev = 2; // winding trail bed
            } else if (contourDist < 6.8f) {
                elev = 4; // first stepped mossy contour terrace
            } else if (contourDist < 13.5f) {
                elev = 6; // second mossy contour terrace
            } else if (contourDist < 20.5f) {
                elev = 8; // high conifer ridge
            } else {
                elev = 10; // upper mountain plateau
            }

            // Distant soft-rolling background hills
            if (gz >= 44) {
                float northT = static_cast<float>(gz - 44) / 19.0f;
                elev = std::max(elev, static_cast<int16_t>(8 + static_cast<int>(northT * 5.0f)));
            }

            // Sunken Forest Brook Cutting Across Midground (gz 34..37)
            bool isBrookZone = (gz >= 34 && gz <= 37);
            bool isBridgeZone = isBrookZone && (absDist <= trailHalfW + 0.55f);
            bool isWaterStream = isBrookZone && !isBridgeZone && (gx >= 16 && gx <= 44);

            uint16_t topLayer = TexLayer::GrassMeadow;

            if (isBridgeZone) {
                // Wide flat stone-paved bridge culvert over the brook (seamless crossing)
                elev = 2;
                topLayer = TexLayer::Cobblestone;
            } else if (isWaterStream) {
                // Sunken stream channel bed with reflective water
                elev = 1;
                topLayer = TexLayer::SandShore;
            } else {
                // Organic feathered trail margin with noise dithering
                float edgeNoise = (fbm(static_cast<float>(gx) * 0.40f, static_cast<float>(gz) * 0.40f, 2, seed + 109u) - 0.5f) * 0.95f;
                float ditherHash = hash2D(gx, gz, seed + 303u);

                if (absDist <= trailHalfW + edgeNoise) {
                    topLayer = TexLayer::DirtPath; // Main pebbled trail
                } else if (absDist <= trailHalfW + edgeNoise + 0.55f && ditherHash > 0.45f) {
                    topLayer = TexLayer::DirtPath; // Feathered gravel boundary
                } else if (elev >= 8) {
                    topLayer = TexLayer::GrassHighland;
                } else {
                    topLayer = TexLayer::GrassMeadow;
                }
            }

            world.setTile(gx, gz, elev, topLayer, TexLayer::CliffStone);

            if (isWaterStream) {
                world.fillRiver(gx, gz, gx, gz, 1, 1.35f);
            }
        }
    }

    // 3. Traveler on the Woodland Trail
    float beamWorldX = -1.2f;
    float beamWorldZ = 3.5f;
    float beamGroundY = world.getTerrainHeightAt(beamWorldX, beamWorldZ);

    // Subtle personal traveler lantern (dim local radius, illuminated by the true sunbeams)
    world.addPointLight({beamWorldX, beamGroundY + 1.25f, beamWorldZ}, {1.10f, 0.90f, 0.65f}, 5.2f, 0.65f);

    // 4. The Lone Traveler on the Path
    // Positioned right in the illuminated pool of light
    world.spawnSpriteGrounded(beamWorldX, beamWorldZ, 2.05f, 2.55f, TexLayer::SpriteHero, SpriteFlags::IdleBob, 0.0f);

    // 5. Clustered Backlit Conifer Pines matching the screenshot
    // Hand-crafted clusters: foreground-left knoll, midground-right ridge, distant backdrop
    const Point2D pineClusters[] = {
        // Foreground left knoll (blurs into creamy foreground bokeh)
        {-16.0f, -12.0f}, {-18.5f, -8.0f}, {-14.0f, -6.5f}, {-19.0f, -3.0f}, {-15.5f, 0.0f},
        // Midground left terrace
        {-12.5f, 8.0f}, {-14.0f, 14.0f}, {-16.5f, 18.0f}, {-11.0f, 22.0f},
        // Midground right terrace (beside the path)
        { 9.5f, -4.0f}, { 12.0f, 0.5f}, { 8.5f, 5.0f}, { 11.5f, 9.5f}, { 14.0f, 13.0f},
        { 7.5f, 16.0f}, { 10.5f, 21.0f}, { 13.5f, 25.0f},
        // Background upper knolls and ridges
        {-8.0f, 28.0f}, {-4.0f, 32.0f}, { 2.0f, 34.0f}, { 8.0f, 31.0f}, { 14.0f, 33.0f}
    };

    for (size_t i = 0; i < sizeof(pineClusters) / sizeof(pineClusters[0]); ++i) {
        const auto& pt = pineClusters[i];
        float scale = 0.95f + hash2D(static_cast<int>(i), 31, seed) * 0.45f;
        float rot = static_cast<float>(i) * 1.37f;
        world.addPineTree3DGrounded(pt.x, pt.z, scale, rot);
    }

    // Additional scattered pines on distant high ridges (gz > 38)
    for (int i = 0; i < 65; ++i) {
        float px = (hash2D(i, 11, seed + 800u) - 0.5f) * 90.0f;
        float pz = 18.0f + hash2D(i, 23, seed + 800u) * 44.0f;
        if (std::abs(px - (pz * 0.2f)) < 6.0f) continue; // Keep trail corridor open

        float scale = 0.85f + hash2D(i, 43, seed) * 0.45f;
        float rot = static_cast<float>(i) * 1.57f;
        world.addPineTree3DGrounded(px, pz, scale, rot);
    }

    // 6. Woodland Rounded Moss Boulders along trail margins & cliff footings (matching Octopath diorama)
    const Point2D mossBoulders[] = {
        {-2.8f,  2.4f}, { 1.5f,  4.2f}, {-3.6f,  6.8f}, { 2.8f,  8.5f},
        {-5.5f, -2.5f}, { 4.5f, -1.0f}, {-6.8f, 12.0f}, { 5.2f, 15.5f},
        {-1.5f, 18.0f}, {-8.5f,  4.5f}, { 7.5f,  2.5f}, {-10.2f, 16.0f},
        {-6.8f,  5.8f}, {-10.2f, 7.2f}, { 6.5f,  7.2f}
    };
    for (size_t i = 0; i < sizeof(mossBoulders) / sizeof(mossBoulders[0]); ++i) {
        const auto& pt = mossBoulders[i];
        float rx = 0.85f + hash2D(static_cast<int>(i), 57, seed) * 0.45f;
        float ry = 0.55f + hash2D(static_cast<int>(i), 61, seed) * 0.30f;
        float rz = 0.85f + hash2D(static_cast<int>(i), 73, seed) * 0.45f;
        float rot = static_cast<float>(i) * 1.15f;
        world.addMossBoulder3DGrounded(pt.x, pt.z, rx, ry, rz, rot);
    }

    // 7. Rustic Timber Post Fence along the Left Trail Ridge (matching reference diorama)
    world.addRusticFenceGrounded(-6.2f, -4.5f, -5.5f, 4.0f, 5);

    // 8. Procedural 3D Star Ferns nestled along cliff skirts
    const Point2D starFernClusters[] = {
        {-3.5f,  1.2f}, { 2.4f,  2.8f}, {-4.2f,  5.5f}, { 3.2f,  7.0f},
        {-2.0f,  9.5f}, { 1.8f, 11.2f}, {-5.0f, 14.5f}, { 4.2f, 16.5f},
        {-7.5f,  8.0f}, { 6.8f,  9.5f}, {-8.5f, 12.5f}, { 8.5f, 14.0f},
        {-9.2f,  7.8f}
    };
    for (size_t i = 0; i < sizeof(starFernClusters) / sizeof(starFernClusters[0]); ++i) {
        const auto& pt = starFernClusters[i];
        float scale = 0.90f + hash2D(static_cast<int>(i), 89, seed) * 0.40f;
        world.addStarFern3DGrounded(pt.x, pt.z, scale, static_cast<float>(i) * 0.95f);
    }

    // 9. Trail Boundary Grass Fringe Tufts & River Pebble Clusters (dissolves grid into soft organic edge)
    for (int gz = 15; gz <= 45; gz += 2) {
        float trailCenterX = 26.5f + (static_cast<float>(gz) * 0.20f) + std::sin(static_cast<float>(gz) * 0.14f) * 3.2f;
        float worldZ = (static_cast<float>(gz) - 32.0f) * RealmWorld::kTileSize;
        float leftX = (trailCenterX - 1.85f - 32.0f) * RealmWorld::kTileSize + (hash2D(gz, 17, seed) - 0.5f) * 0.35f;
        float rightX = (trailCenterX + 1.85f - 32.0f) * RealmWorld::kTileSize + (hash2D(gz, 29, seed) - 0.5f) * 0.35f;

        world.addGrassTuft3DGrounded(leftX, worldZ, 0.55f + hash2D(gz, 31, seed) * 0.25f, static_cast<float>(gz) * 0.85f);
        world.addGrassTuft3DGrounded(rightX, worldZ, 0.55f + hash2D(gz, 43, seed) * 0.25f, static_cast<float>(gz) * 1.25f);

        if (gz % 3 == 0) {
            world.addRiverPebbleClusterGrounded(leftX + 0.25f, worldZ, 0.45f, 4, seed + static_cast<uint32_t>(gz * 7));
        }
        if (gz % 4 == 0) {
            world.addRiverPebbleClusterGrounded(rightX - 0.25f, worldZ, 0.45f, 4, seed + static_cast<uint32_t>(gz * 13));
        }
    }

    // Sunken Forest Brook River Pebble Clusters
    world.addRiverPebbleClusterGrounded(-5.5f, 6.2f, 0.95f, 7, seed + 101u);
    world.addRiverPebbleClusterGrounded(-8.5f, 6.8f, 1.25f, 9, seed + 102u);
    world.addRiverPebbleClusterGrounded(-11.0f, 7.5f, 1.10f, 6, seed + 103u);
    world.addRiverPebbleClusterGrounded(5.8f, 6.5f, 0.85f, 6, seed + 104u);

    // 10. Woodland Star Fern Sprites & Wildflowers at Cliff Skirts and Trail Margins
    for (int i = 0; i < 120; ++i) {
        float fx = (hash2D(i, 53, seed + 900u) - 0.5f) * 82.0f;
        float fz = (hash2D(i, 67, seed + 900u) - 0.40f) * 78.0f;

        // Keep path center walkable, but allow ferns along trail margins
        float trailCenterAtZ = 26.5f + (fz / RealmWorld::kTileSize + 32.0f) * 0.20f;
        float trailWorldX = (trailCenterAtZ - 32.0f) * RealmWorld::kTileSize;
        if (std::abs(fx - trailWorldX) < 1.4f) continue;

        float scale = 0.95f + hash2D(i, 79, seed) * 0.45f;
        world.spawnSpriteGrounded(fx, fz, 1.45f * scale, 1.35f * scale, TexLayer::SpriteWildflower, SpriteFlags::WindSway, static_cast<float>(i) * 0.42f);
    }

    // Lush Octopath / Lumina style procedural grass tufts, tall grass, and flower bouquets
    world.scatterMeadowVegetation(1.1f, 0.35f, seed + 1200u);

    // 9. Signature HD-2D Tilt-Shift Lens & Backlit Forest Atmosphere
    // 3/4 elevated perspective matching the reference screenshot
    auto& cam = world.camera();
    cam.position      = {-11.5f, 21.0f, -27.5f};
    cam.target        = {-1.5f,   3.8f,   5.5f};
    cam.fovDegrees    = 26.0f;
    cam.focusDistance = 35.5f; // Locked directly on the traveler
    cam.focusRange    = 7.5f;  // Tight focal band for creamy top/bottom blur
    cam.bokehRadius   = 26.0f; // Lush cinematic tilt-shift bokeh aperture discs (Octopath look)

    world.setTimeOfDay(0.38f); // Morning reference light angle & golden sunbeam
    auto& atmos = world.atmosphere();
    atmos.fogDensity   = 0.0075f;
    atmos.exposure     = 1.02f;
}

void ProceduralWorldGenerator::generateCoastalCove(RealmWorld& world, uint32_t seed) {
    world.clear();

    const int gridW = RealmWorld::kGridWidth;
    const int gridD = RealmWorld::kGridDepth;
    const float waterY = 1.45f;

    // 1. Base Ocean Inlet & Stepped Stratified Limestone Headland
    // The ocean surrounds the scene, and a prominent rocky promontory juts out into the water
    for (int gz = 0; gz < gridD; ++gz) {
        float worldZ = (static_cast<float>(gz) - 32.0f) * RealmWorld::kTileSize;

        for (int gx = 0; gx < gridW; ++gx) {
            float worldX = (static_cast<float>(gx) - 32.0f) * RealmWorld::kTileSize;

            // Distance to main promontory center
            float dxProm = worldX - 3.5f;
            float dzProm = worldZ - 8.5f;

            // Distance to southern jutting overlook (where party & chest stand)
            float dxOverlook = worldX - (-1.5f);
            float dzOverlook = worldZ - 3.5f;
            float distOverlook = std::sqrt(dxOverlook * dxOverlook * 0.85f + dzOverlook * dzOverlook * 1.25f);

            // Distance to back island across the channel
            float dxBack = worldX - 6.5f;
            float dzBack = worldZ - 26.5f;
            float distBack = std::sqrt(dxBack * dxBack * 0.65f + dzBack * dzBack * 1.15f);

            // Channel cutting between main headland and back island (gz ~ 38..42)
            bool isChannel = (worldZ >= 13.5f && worldZ <= 17.5f && worldX < 14.0f);

            float nMacro = fbm(static_cast<float>(gx) * 0.12f, static_cast<float>(gz) * 0.12f, 3, seed + 101u) * 2.2f;

            int16_t elev = 0;
            bool isWater = true;

            if (distOverlook < 7.8f + nMacro && !isChannel) {
                // Main southern rocky plateau where the party, chest, and palm planter stand
                if (distOverlook < 5.2f) {
                    elev = 6; // Main overlook plateau (worldY = 3.6m)
                } else if (distOverlook < 6.8f) {
                    elev = 4; // First stepped limestone shelf (worldY = 2.4m)
                } else {
                    elev = 2; // Water-line limestone shelf (worldY = 1.2m)
                }
                isWater = false;
            } else if (distBack < 14.5f + nMacro && !isChannel && worldZ > 16.5f) {
                // Back island across the channel
                if (distBack < 7.5f) {
                    elev = 8;
                } else if (distBack < 11.5f) {
                    elev = 6;
                } else {
                    elev = 4;
                }
                isWater = false;
            } else if (std::abs(dxProm) < 14.0f + nMacro && worldZ > 0.0f && worldZ < 14.5f && !isChannel) {
                // Neck connecting to mainland
                elev = 6;
                isWater = false;
            }

            // Distant backdrop headlands on far north / east
            if (gz >= 54) {
                float northT = static_cast<float>(gz - 54) / 9.0f;
                elev = std::max(elev, static_cast<int16_t>(6 + static_cast<int>(northT * 6.0f)));
                isWater = false;
            }

            uint16_t topLayer = TexLayer::CoastalScree;
            if (elev <= 2) {
                topLayer = TexLayer::SandShore;
            } else if (elev >= 8) {
                topLayer = TexLayer::GrassHighland;
            }

            world.setTile(gx, gz, elev, topLayer, TexLayer::CoastalCliff);

            if (isWater || elev <= 2) {
                world.fillRiver(gx, gz, gx, gz, std::min(elev, static_cast<int16_t>(1)), waterY);
            }
        }
    }

    // 2. Stepped Offshore Sea Stacks (Rising dramatically out of the ocean with foaming bases)
    // Left prominent multi-tier sea stack (matching reference screenshot)
    world.addSeaStack3D({-17.5f, waterY - 0.2f, -2.5f}, 2.65f, 9.2f, seed + 11u);
    // Secondary sea stacks offshore
    world.addSeaStack3D({-24.5f, waterY - 0.2f,  8.5f}, 1.95f, 7.5f, seed + 22u);
    world.addSeaStack3D({-12.5f, waterY - 0.2f, 21.5f}, 2.25f, 8.4f, seed + 33u);
    world.addSeaStack3D({ 18.5f, waterY - 0.2f, 24.5f}, 2.10f, 7.8f, seed + 44u);

    // 3. Wooden Rope Suspension Bridge across the Ocean Channel
    // Spanning from the main rocky promontory to the back island
    world.addCoastalBridge({-0.8f, 3.8f, 13.2f}, {2.8f, 4.2f, 21.0f}, 2.9f);

    // 4. Prominent Tropical Palm Tree in Raised Stone Planter
    // Positioned beside the cliff trail right where the reference shows the planter drum!
    float planterGroundY = world.getTerrainHeightAt(-7.2f, 3.8f);
    world.addPalmTree3D({-7.2f, planterGroundY, 3.8f}, 1.15f, 0.45f, true /* withStonePlanter */);

    // Additional Coastal & Island Palm Trees
    world.addPalmTree3DGrounded(-11.5f, 8.5f, 1.05f, 1.25f, false);
    world.addPalmTree3DGrounded( 10.5f, 4.5f, 1.00f, 2.15f, false);
    world.addPalmTree3DGrounded( 13.8f, 11.2f, 0.95f, 0.85f, false);
    // Palms on the back island across the bridge
    world.addPalmTree3DGrounded(  5.5f, 24.5f, 1.12f, 1.65f, false);
    world.addPalmTree3DGrounded( 11.5f, 26.8f, 0.92f, 2.75f, false);
    world.addPalmTree3DGrounded( -4.2f, 25.5f, 0.88f, 0.35f, false);

    // 5. The Iconic RPG Treasure Chest
    // Sitting on the southern cliff edge facing the camera ("Open Chest")
    float chestX = -1.8f;
    float chestZ = 0.5f;
    world.addTreasureChestGrounded(chestX, chestZ, 0.0f);

    // 6. Glowing Green Quest / Save Beacon Spark
    // Emitting gentle radiant emerald point light on the rock ledge
    float sparkX = -5.4f;
    float sparkZ = 2.4f;
    float sparkY = world.getTerrainHeightAt(sparkX, sparkZ) + 0.65f;
    world.addPointLight({sparkX, sparkY, sparkZ}, {0.25f, 1.95f, 0.55f}, 6.2f, 1.85f);
    world.spawnSprite({sparkX, sparkY, sparkZ}, 0.85f, 0.85f, TexLayer::SpriteTorchFlame, SpriteFlags::Emissive, 0.0f);

    // 7. Full Octopath Traveler Party Assembled on the Cliff Plateau (matching reference screenshot)
    // 1. Warrior / Sword Hero (interacting with the chest)
    world.spawnSpriteGrounded(-1.0f, -0.4f, 1.95f, 2.45f, TexLayer::SpriteHero, SpriteFlags::IdleBob, 0.0f);
    // 2. Scholar (silver hair & grimoire)
    world.spawnSpriteGrounded(-2.2f,  1.2f, 1.85f, 2.35f, TexLayer::SpriteScholar, SpriteFlags::IdleBob, 0.35f);
    // 3. Cleric (white & gold mantle, prayer staff)
    world.spawnSpriteGrounded(-0.8f,  1.4f, 1.85f, 2.35f, TexLayer::SpriteCleric, SpriteFlags::IdleBob, 0.70f);
    // 4. Guard / Knight (steel armor & blade)
    world.spawnSpriteGrounded( 0.7f,  1.5f, 1.95f, 2.45f, TexLayer::SpriteGuard, SpriteFlags::IdleBob, 1.05f);
    // 5. Dancer (ruby attire & lilac sash)
    world.spawnSpriteGrounded( 2.2f,  1.6f, 1.85f, 2.35f, TexLayer::SpriteDancer, SpriteFlags::IdleBob, 1.40f);
    // 6. Merchant (green tunic & traveler pack)
    world.spawnSpriteGrounded( 3.6f,  1.7f, 1.85f, 2.35f, TexLayer::SpriteMerchant, SpriteFlags::IdleBob, 1.75f);
    // 7. Companion / Villager
    world.spawnSpriteGrounded( 4.9f,  1.8f, 1.85f, 2.35f, TexLayer::SpriteVillager, SpriteFlags::IdleBob, 2.10f);

    // 8. Coastal Flora & Wild Rock Sprouts
    // Scatter green sprouts across the cliff plateau (matching reference sprigs)
    for (int i = 0; i < 75; ++i) {
        float sx = -10.0f + hash2D(i, 41, seed + 500u) * 22.0f;
        float sz = -2.0f + hash2D(i, 61, seed + 500u) * 20.0f;
        float sh = world.getTerrainHeightAt(sx, sz);
        if (sh > waterY + 0.5f) {
            float scale = 0.40f + hash2D(i, 83, seed) * 0.30f;
            world.addGrassTuft3D({sx, sh, sz}, scale, static_cast<float>(i) * 1.15f);
        }
    }

    // Coastal pebble clusters along shoreline margins
    world.addRiverPebbleClusterGrounded(-8.5f, 0.5f, 0.85f, 6, seed + 301u);
    world.addRiverPebbleClusterGrounded( 8.2f, 1.5f, 0.95f, 7, seed + 302u);
    world.addRiverPebbleClusterGrounded(-3.5f, 14.5f, 0.75f, 5, seed + 303u);

    // 9. Camera & Coastal Morning Atmosphere
    // Elevated perspective matching the attached reference screenshot
    auto& cam = world.camera();
    cam.position      = {-3.2f, 19.5f, -27.5f};
    cam.target        = {-0.8f,  3.8f,   2.5f};
    cam.fovDegrees    = 27.5f;
    cam.focusDistance = 33.5f; // Locked on the hero party and treasure chest
    cam.focusRange    = 15.5f; // Crisp focus across headland, bridge, and sea stack
    cam.bokehRadius   = 14.0f; // Subtle creamy bokeh on distant ocean & foreground shelf

    world.setTimeOfDay(0.38f); // Warm coastal morning sun angle (~09:07 AM)
    auto& atmos = world.atmosphere();
    atmos.fogDensity   = 0.0024f; // Clean, radiant coastal sea-air
    atmos.exposure     = 1.02f;
}

void ProceduralWorldGenerator::generateChronicleHamlet(RealmWorld& world, uint32_t seed) {
    HistorySimulator::simulate(world, seed);
}

void ProceduralWorldGenerator::generateHighlandBridge(RealmWorld& world, uint32_t seed) {
    world.clear();

    const int gridW = RealmWorld::kGridWidth;
    const int gridD = RealmWorld::kGridDepth;
    const float waterY = 1.35f;

    // =========================================================================
    // PASS 1: Multi-Level Topography & River Gorge
    // =========================================================================
    // The river flows through the center (gx ~ 27..35 in the south, widening north).
    // Flanked by multi-tiered terraces: shore (elev 2), abutment (elev 4), middle (elev 6), high (elev 8).
    for (int gz = 0; gz < gridD; ++gz) {
        float zFrac = static_cast<float>(gz) / static_cast<float>(gridD);

        // River gorge center meander
        float riverCenter = 31.5f + std::sin(static_cast<float>(gz) * 0.12f) * 2.5f;
        float riverHalfW = 4.2f + (gz > 36 ? static_cast<float>(gz - 36) * 0.35f : 0.0f);

        // Mid-river background island (gz 42..54)
        bool isMidIsland = (gz >= 42 && gz <= 54);

        for (int gx = 0; gx < gridW; ++gx) {
            float distToRiver = static_cast<float>(gx) - riverCenter;
            float absDist = std::abs(distToRiver);

            // Natural organic contour noise
            float nContour = fbm(static_cast<float>(gx) * 0.09f, static_cast<float>(gz) * 0.09f, 3, seed + 101u) * 2.4f;
            float effDist = absDist + nContour;

            int16_t elev = 2;
            bool isWater = false;
            uint16_t topLayer = TexLayer::GrassMeadow;
            uint16_t cliffLayer = TexLayer::CliffStone;

            // Check for mid-river island in the background
            if (isMidIsland && std::abs(distToRiver) < 5.5f) {
                float distToIslandCenter = std::sqrt(distToRiver * distToRiver + (static_cast<float>(gz) - 48.0f) * (static_cast<float>(gz) - 48.0f) * 0.5f);
                if (distToIslandCenter < 4.2f) {
                    elev = 4;
                    topLayer = TexLayer::GrassHighland;
                    isWater = false;
                } else {
                    elev = 0;
                    isWater = true;
                    topLayer = TexLayer::SandShore;
                }
            } else if (absDist < riverHalfW) {
                // Riverbed
                elev = 0;
                isWater = true;
                topLayer = TexLayer::SandShore;
            } else {
                // Terraced river banks
                float bankDist = absDist - riverHalfW;
                if (bankDist < 1.4f) {
                    elev = 2; // Low shoreline rock shelf
                    topLayer = (hash2D(gx, gz, seed + 41u) > 0.45f) ? TexLayer::GrassMeadow : TexLayer::Cobblestone;
                } else if (bankDist < 5.8f) {
                    elev = 4; // Bridge abutment terrace
                    topLayer = TexLayer::GrassMeadow;
                } else if (bankDist < 11.5f) {
                    elev = 6; // Middle terrace
                    topLayer = TexLayer::GrassMeadow;
                } else {
                    elev = 8; // High woodland ridge
                    topLayer = TexLayer::GrassHighland;
                }
            }

            world.setTile(gx, gz, elev, topLayer, cliffLayer);

            if (isWater) {
                world.fillRiver(gx, gz, gx, gz, elev, waterY);
            }
        }
    }

    // =========================================================================
    // PASS 2: Sloping Roads & Mountain Highway Incline
    // =========================================================================
    // Left Bank Winding Cobblestone Road:
    // Starts in foreground-left at elevation 6 (gx: 16..19, gz: 18..22),
    // then slopes down smoothly to elevation 4 at gz: 23..27 to meet the bridge abutment!
    world.fillPlateau(17, 18, 20, 22, 6, TexLayer::Cobblestone, TexLayer::CliffStone);
    world.addSlope(18, 23, 21, 27, 6, 4, SlopeDir::PlusZ, TexLayer::Cobblestone, TexLayer::CliffStone, false);
    // Road connecting to bridge entrance at elev 4
    world.fillPlateau(21, 26, 26, 28, 4, TexLayer::Cobblestone, TexLayer::CliffStone);

    // Right Bank Winding Cobblestone Road:
    // Leaves bridge abutment at elevation 4 (gx: 37..39, gz: 26..28),
    // then slopes up smoothly to elevation 6 as it curves into the conifer forest!
    world.fillPlateau(37, 26, 39, 28, 4, TexLayer::Cobblestone, TexLayer::CliffStone);
    world.addSlope(40, 26, 44, 29, 4, 6, SlopeDir::PlusX, TexLayer::Cobblestone, TexLayer::CliffStone, false);
    // Continuing into the forest at elev 6
    world.fillPlateau(44, 27, 49, 32, 6, TexLayer::Cobblestone, TexLayer::CliffStone);
    world.addSlope(47, 33, 50, 38, 6, 8, SlopeDir::PlusZ, TexLayer::Cobblestone, TexLayer::CliffStone, false);

    // =========================================================================
    // PASS 3: The Authentic Octopath Rustic Timber Suspension Footbridge
    // =========================================================================
    // Spans the gorge from left abutment to right abutment
    Vec3f bridgeStart{-6.5f, 2.45f, -4.5f};
    Vec3f bridgeEnd  { 6.8f, 2.45f, -4.5f};
    world.addSuspensionBridge(bridgeStart, bridgeEnd, 2.0f, 4.0f);

    // Background Secondary Trestle Bridge (crossing upstream river inlet on the right)
    Vec3f bgBridgeStart{10.5f, 2.45f, 15.0f};
    Vec3f bgBridgeEnd  {19.5f, 2.45f, 15.0f};
    world.addWoodenBridge(bgBridgeStart, bgBridgeEnd, 2.8f);

    // =========================================================================
    // PASS 4: Hero Party Assembled Crossing the Bridge (Facing Right)
    // =========================================================================
    // 1. Warrior / Sword Hero (Hikari - leading the party, facing right)
    world.spawnSprite({-1.4f, 2.38f, -4.5f}, 1.55f, 2.05f, TexLayer::SpriteHero, SpriteFlags::IdleBob | SpriteFlags::FlipX, 0.0f);
    // 2. Dancer (Agnea - green dress & ribbon, facing right)
    world.spawnSprite({ 0.4f, 2.38f, -4.5f}, 1.45f, 1.95f, TexLayer::SpriteDancer, SpriteFlags::IdleBob | SpriteFlags::FlipX, 0.35f);
    // 3. Traveler / Merchant (Partitio - yellow hair & traveler pack, facing right)
    world.spawnSprite({ 2.1f, 2.38f, -4.5f}, 1.45f, 1.95f, TexLayer::SpriteMerchant, SpriteFlags::IdleBob | SpriteFlags::FlipX, 0.70f);
    // 4. Cleric / Companion (Castti - white mantle, facing right)
    world.spawnSprite({ 3.8f, 2.38f, -4.5f}, 1.45f, 1.95f, TexLayer::SpriteCleric, SpriteFlags::IdleBob | SpriteFlags::FlipX, 1.05f);

    // =========================================================================
    // PASS 5: Roadside Emerald Beacon / Signpost & Shrubbery
    // =========================================================================
    // Green glowing beacon near right bridge abutment (matching reference screenshot)
    float beaconX = 7.8f;
    float beaconZ = -4.0f;
    float beaconY = world.getTerrainHeightAt(beaconX, beaconZ);
    world.addRoadsideBeacon({beaconX, beaconY, beaconZ}, {0.20f, 1.95f, 0.55f});

    // Lush rounded green bush shrubbery framing the roadside beacon
    world.addMossBoulder3DGrounded(7.2f, -3.8f, 0.85f, 0.65f, 0.85f, 0.25f);
    world.addStarFern3DGrounded(8.0f, -3.2f, 1.15f, 0.45f);

    // =========================================================================
    // PASS 6: Flora, Pines, Boulders & River Shore Dressing
    // =========================================================================
    // Mid-River Background Island Pines (Matching dense backlit backdrop)
    for (int i = 0; i < 28; ++i) {
        float ix = -4.5f + hash2D(i, 31, seed + 701u) * 11.0f;
        float iz = 17.5f + hash2D(i, 43, seed + 701u) * 16.0f;
        float iy = world.getTerrainHeightAt(ix, iz);
        if (iy > waterY + 0.3f) {
            float scale = 0.95f + hash2D(i, 57, seed) * 0.45f;
            float rot = static_cast<float>(i) * 1.25f;
            world.addPineTree3D({ix, iy, iz}, scale, rot);
        }
    }

    // Distant Ridge & Shoreline Pines
    for (int i = 0; i < 45; ++i) {
        float px = (hash2D(i, 71, seed + 801u) - 0.5f) * 85.0f;
        float pz = 12.0f + hash2D(i, 83, seed + 801u) * 34.0f;
        // Keep bridge corridors clear
        if (std::abs(px) < 7.5f && pz < 5.0f) continue;
        float py = world.getTerrainHeightAt(px, pz);
        if (py > waterY + 0.4f) {
            float scale = 0.85f + hash2D(i, 97, seed) * 0.50f;
            float rot = static_cast<float>(i) * 1.57f;
            world.addPineTree3D({px, py, pz}, scale, rot);
        }
    }

    // Right Bank Forest Framing Pines
    for (int i = 0; i < 18; ++i) {
        float px = 18.5f + hash2D(i, 13, seed + 901u) * 22.0f;
        float pz = -12.0f + hash2D(i, 29, seed + 901u) * 32.0f;
        float py = world.getTerrainHeightAt(px, pz);
        if (py > waterY + 0.5f) {
            float scale = 1.05f + hash2D(i, 37, seed) * 0.45f;
            world.addPineTree3D({px, py, pz}, scale, static_cast<float>(i) * 1.15f);
        }
    }

    // Rounded Moss Boulders along river gorge margins
    const Point2D mossBoulders[] = {
        {-6.5f,  -1.2f}, {-7.8f,  -6.5f}, {-7.5f,   3.5f},
        { 6.8f,  -1.5f}, { 7.5f,  -6.2f}, { 7.8f,   4.2f},
        {-6.2f,  -8.5f}, { 6.6f,  -8.2f}, {-5.5f,   7.5f},
        { 5.5f,   7.2f}, {11.5f,   2.5f}, {-11.5f,  1.5f}
    };
    for (size_t i = 0; i < sizeof(mossBoulders) / sizeof(mossBoulders[0]); ++i) {
        const auto& pt = mossBoulders[i];
        float rx = 0.85f + hash2D(static_cast<int>(i), 57, seed) * 0.45f;
        float ry = 0.55f + hash2D(static_cast<int>(i), 61, seed) * 0.30f;
        float rz = 0.85f + hash2D(static_cast<int>(i), 73, seed) * 0.45f;
        float rot = static_cast<float>(i) * 1.15f;
        world.addMossBoulder3DGrounded(pt.x, pt.z, rx, ry, rz, rot);
    }

    // River Pebble Clusters in the shallows and banks
    world.addRiverPebbleClusterGrounded(-5.5f, -2.5f, 0.95f, 7, seed + 101u);
    world.addRiverPebbleClusterGrounded( 6.2f, -2.2f, 0.95f, 6, seed + 102u);
    world.addRiverPebbleClusterGrounded(-5.5f,  3.5f, 0.85f, 6, seed + 103u);
    world.addRiverPebbleClusterGrounded( 5.8f,  4.2f, 0.85f, 5, seed + 104u);
    world.addRiverPebbleClusterGrounded( 8.5f, -4.5f, 0.75f, 5, seed + 105u);

    // Procedural Meadow Vegetation along the terraces
    world.scatterMeadowVegetation(1.15f, 0.35f, seed + 1337u);

    // =========================================================================
    // PASS 7: Camera & Cinematic Tilt-Shift Miniature Atmosphere
    // =========================================================================
    auto& cam = world.camera();
    cam.position      = {0.0f, 15.0f, -38.0f};
    cam.target        = {0.2f,  2.5f,   2.0f};
    cam.fovDegrees    = 26.5f;
    cam.focusDistance = 35.0f; // Locked directly on the suspension bridge & hero party
    cam.focusRange    = 9.0f;  // Crisp focal band on the bridge and party
    cam.bokehRadius   = 16.0f; // Soft cinematic tilt-shift blur

    world.setTimeOfDay(0.38f); // Warm coastal morning light angle (~09:10 AM)
    auto& atmos = world.atmosphere();
    atmos.fogDensity = 0.0042f; // Soft river mist
    atmos.exposure   = 1.05f;
}

void ProceduralWorldGenerator::generateProceduralChunk(RealmChunk& chunk, uint32_t seed) {
    chunk.clearPropsAndSprites();
    const ChunkCoord coord = chunk.coord();
    constexpr float kSeaLevel = 3.0f; // Global sea level in elevation steps

    float chunkOriginX = static_cast<float>(coord.cx) * RealmChunk::kChunkSize;
    float chunkOriginZ = static_cast<float>(coord.cz) * RealmChunk::kChunkSize;
    float chunkCenterX = chunkOriginX + RealmChunk::kChunkSize * 0.5f;
    float chunkCenterZ = chunkOriginZ + RealmChunk::kChunkSize * 0.5f;
    MacroSiteInfo nearestSite = MacroHistoryGrid::getNearestSite(chunkCenterX, chunkCenterZ, seed);

    for (int lz = 0; lz < RealmChunk::kTilesPerChunk; ++lz) {
        for (int lx = 0; lx < RealmChunk::kTilesPerChunk; ++lx) {
            int gx = coord.cx * RealmChunk::kTilesPerChunk + lx;
            int gz = coord.cz * RealmChunk::kTilesPerChunk + lz;

            float wx = static_cast<float>(gx) * RealmChunk::kTileSize;
            float wz = static_cast<float>(gz) * RealmChunk::kTileSize;

            // 1. Continental & Mountain Elevation
            float continental = fbm(wx * 0.003f, wz * 0.003f, 3, seed) * 10.0f + 2.0f;
            float hillNoise = fbm(wx * 0.015f, wz * 0.015f, 3, seed + 101u) * 3.5f;

            float ridgeRaw = std::abs(fbm(wx * 0.006f + 40.0f, wz * 0.006f + 40.0f, 4, seed + 202u) * 2.0f - 1.0f);
            float ridgeNoise = std::pow(1.0f - ridgeRaw, 3.2f) * 24.0f;

            // 2. Continuous Winding River Network
            float riverCenter = std::sin(wz * 0.010f + fbm(wz * 0.005f, 0.0f, 2, seed + 42u) * 3.0f) * 45.0f;
            float distToRiver = std::abs(wx - riverCenter);

            float riverCarve = 0.0f;
            if (distToRiver < 10.0f) {
                riverCarve = (1.0f - distToRiver / 10.0f) * 7.0f;
            }

            float continuousH = continental + hillNoise + ridgeNoise - riverCarve;

            // 3. Macro Climate Fields (Temperature & Moisture)
            float tempMacro = fbm(wx * 0.0045f + 140.0f, wz * 0.0045f - 60.0f, 3, seed + 500u);
            float moistMacro = fbm(wx * 0.0045f - 180.0f, wz * 0.0045f + 250.0f, 3, seed + 600u);

            if (distToRiver < 14.0f) {
                moistMacro = std::min(1.0f, moistMacro + (1.0f - distToRiver / 14.0f) * 0.35f);
            }

            float altitudeCooling = std::clamp((continuousH - 8.0f) / 16.0f, 0.0f, 1.0f) * 0.50f;
            float tempEffective = std::clamp(tempMacro - altitudeCooling, 0.0f, 1.0f);

            // 4. Macro-Region Historical Site Influence & Environmental Scars
            float ddxSite = wx - nearestSite.worldX;
            float ddzSite = wz - nearestSite.worldZ;
            float distToSite = std::sqrt(ddxSite * ddxSite + ddzSite * ddzSite);
            bool inSiteInfluence = (nearestSite.type != MacroSiteType::Wilderness && distToSite < nearestSite.influenceRadius);

            if (inSiteInfluence && nearestSite.type == MacroSiteType::CalamityRupture) {
                if (distToSite < 14.0f) {
                    continuousH = std::max(2.0f, continuousH - (1.0f - distToSite / 14.0f) * 4.5f);
                }
            } else if (inSiteInfluence && (nearestSite.type == MacroSiteType::WizardSanctum || nearestSite.type == MacroSiteType::SettlementHamlet)) {
                if (distToSite < 16.0f) {
                    continuousH = std::round(continuousH / 2.0f) * 2.0f; // Terrace leveling
                }
            }

            int16_t elev = std::max<int16_t>(1, static_cast<int16_t>(std::round(continuousH / 2.0f) * 2.0f));

            TileCell cell{};
            cell.elevation = elev;
            cell.cliffLayer = TexLayer::CliffStone;

            // 5. Biome, River & Historical Surface Assignment
            bool isRiverWater = (distToRiver < 7.0f);
            bool isSubmerged = (continuousH <= kSeaLevel) || isRiverWater;

            if (inSiteInfluence && nearestSite.type == MacroSiteType::CalamityRupture && !isSubmerged) {
                // Calamity Scorched Earth & Basalt
                cell.topLayer = TexLayer::ScorchedAsh;
                cell.cliffLayer = TexLayer::CliffStone;
            } else if (inSiteInfluence && nearestSite.type == MacroSiteType::WizardSanctum && distToSite < 15.0f && !isSubmerged) {
                // Wizard Spire Paved Courtyard
                cell.topLayer = TexLayer::StoneBrick;
                cell.cliffLayer = TexLayer::CliffStone;
            } else if (inSiteInfluence && nearestSite.type == MacroSiteType::SunkenRuins && distToSite < 16.0f && !isSubmerged) {
                // Ruined Citadel Masonry
                cell.topLayer = ((gx + gz) % 3 == 0) ? TexLayer::Cobblestone : TexLayer::StoneBrick;
                cell.cliffLayer = TexLayer::CliffStone;
            } else if (inSiteInfluence && nearestSite.type == MacroSiteType::SettlementHamlet && !isSubmerged) {
                // Village Streets & Plazas
                cell.topLayer = (distToSite < 10.0f) ? TexLayer::Cobblestone : TexLayer::DirtPath;
                cell.cliffLayer = TexLayer::CliffStone;
            } else if (inSiteInfluence && nearestSite.type == MacroSiteType::AlpineMonastery && distToSite < 14.0f && !isSubmerged) {
                // Mountain Chapel Terrace
                cell.topLayer = TexLayer::StoneBrick;
                cell.cliffLayer = TexLayer::CliffStone;
            } else if (isSubmerged) {
                // Water surface (sea or river channel)
                cell.elevation = 1;
                cell.topLayer = (distToRiver < 4.0f) ? TexLayer::Cobblestone : TexLayer::SandShore;
                cell.cliffLayer = TexLayer::CoastalCliff;
                cell.hasWater = true;
                cell.waterY = kSeaLevel * RealmChunk::kElevStep;
            } else if (distToRiver < 10.5f || continuousH <= kSeaLevel + 1.2f) {
                cell.topLayer = TexLayer::SandShore;
                cell.cliffLayer = TexLayer::CoastalCliff;
            } else if (elev >= 16 || tempEffective < 0.28f) {
                cell.topLayer = TexLayer::SnowFrost;
                cell.cliffLayer = TexLayer::CliffStone;
            } else if (tempEffective > 0.58f && moistMacro < 0.42f) {
                cell.topLayer = TexLayer::DesertDune;
                cell.cliffLayer = TexLayer::CoastalCliff;
            } else if (moistMacro > 0.65f && elev <= 6) {
                cell.topLayer = TexLayer::SwampBog;
                cell.cliffLayer = TexLayer::CliffStone;
            } else if (elev >= 10) {
                cell.topLayer = TexLayer::GrassHighland;
                cell.cliffLayer = TexLayer::CliffStone;
            } else if (distToRiver >= 10.5f && distToRiver < 16.0f && fbm(wx * 0.05f, wz * 0.05f, 2, seed + 88u) > 0.45f) {
                cell.topLayer = TexLayer::DirtPath;
                cell.cliffLayer = TexLayer::CliffStone;
            } else {
                cell.topLayer = TexLayer::GrassMeadow;
                cell.cliffLayer = TexLayer::CliffStone;
            }

            chunk.setTile(lx, lz, cell);

            // 6. Flora & 3D Tree Placement (Superseded within site epicenters)
            if (!cell.hasWater && (!inSiteInfluence || distToSite > 14.0f)) {
                float posX = wx + 1.0f;
                float posZ = wz + 1.0f;
                float posY = static_cast<float>(elev) * RealmChunk::kElevStep;
                float rnd = hash2D(gx, gz, seed + 777u);
                float rot = hash2D(gx + 9, gz + 19, seed + 101u) * 6.28f;
                float treeScale = 0.85f + hash2D(gx + 1, gz + 3, seed + 202u) * 0.35f;

                if (cell.topLayer == TexLayer::SnowFrost || (elev >= 16 && tempEffective < 0.35f)) {
                    // Cold Mountain Peaks: 3D Snow-Draped Pines
                    if (rnd > 0.988f) {
                        RealmWorld::emitSnowPineTree3D(chunk.propVertices(), {posX, posY, posZ}, treeScale, rot);
                    }
                } else if (cell.topLayer == TexLayer::DesertDune) {
                    if (rnd > 0.986f) {
                        chunk.addSprite(Sprite3DInstance{
                            {posX, posY, posZ},
                            1.6f * treeScale, 3.2f * treeScale,
                            TexLayer::SpriteCactus,
                            SpriteFlags::None,
                            rot
                        });
                    } else if (rnd < 0.045f) {
                        chunk.addSprite(Sprite3DInstance{
                            {posX, posY, posZ},
                            1.1f * treeScale, 1.1f * treeScale,
                            TexLayer::SpriteDeadShrub,
                            SpriteFlags::WindSway,
                            rot
                        });
                    }
                } else if (cell.topLayer == TexLayer::SwampBog) {
                    // Swamps: 3D Dead Trees with Barren Twigs or Weeping Canopy
                    if (rnd > 0.988f) {
                        if (rnd > 0.994f) {
                            RealmWorld::emitDeadTree3D(chunk.propVertices(), {posX, posY, posZ}, treeScale, rot);
                        } else {
                            RealmWorld::emitOakTree3D(chunk.propVertices(), {posX, posY, posZ}, treeScale, rot, TexLayer::PineFoliage);
                        }
                    } else if (rnd < 0.065f) {
                        chunk.addSprite(Sprite3DInstance{
                            {posX, posY, posZ},
                            1.1f, 1.35f,
                            TexLayer::SpriteTallGrass,
                            SpriteFlags::WindSway,
                            rot
                        });
                    }
                } else if (cell.topLayer == TexLayer::SandShore) {
                    // Coastal Beaches: 3D Slanted Feather Palm Trees
                    if (rnd > 0.987f && distToRiver > 8.0f) {
                        RealmWorld::emitPalmTree3D(chunk.propVertices(), {posX, posY, posZ}, treeScale, rot, false);
                    }
                } else if (cell.topLayer == TexLayer::GrassHighland || elev >= 10) {
                    // Alpine Conifer Highlands: 3D Pine Trees, Moss Boulders, Star Ferns, and Highland Grass
                    if (rnd > 0.988f) {
                        RealmWorld::emitPineTree3D(chunk.propVertices(), {posX, posY, posZ}, treeScale, rot);
                        if (rnd > 0.994f) {
                            RealmWorld::emitStarFern3D(chunk.propVertices(), {posX + 0.9f, posY, posZ - 0.7f}, 0.8f * treeScale, rot + 1.4f);
                        }
                    } else if (rnd > 0.980f && rnd <= 0.988f) {
                        RealmWorld::emitMossBoulder3D(chunk.propVertices(), {posX, posY, posZ}, 1.1f * treeScale, 0.65f * treeScale, 1.0f * treeScale, rot);
                    } else if (rnd > 0.972f && rnd <= 0.980f) {
                        RealmWorld::emitStarFern3D(chunk.propVertices(), {posX, posY, posZ}, 0.85f * treeScale, rot);
                    } else {
                        // Alpine Conifer Highlands: Wind-swaying grass tufts and alpine blue wildflowers
                        int numTufts = (hash2D(gx, gz, seed + 29u) < 0.65f) ? 1 : 0;
                        for (int t = 0; t < numTufts; ++t) {
                            float jx = (hash2D(gx * 19 + t, gz * 29, seed + 131u) - 0.5f) * (RealmChunk::kTileSize * 0.75f);
                            float jz = (hash2D(gx * 31 + t, gz * 37, seed + 241u) - 0.5f) * (RealmChunk::kTileSize * 0.75f);
                            float phase = hash2D(gx + 7 + t, gz + 11, seed + 555u) * 6.28f;
                            float fScale = 0.82f + hash2D(gx + 13 + t, gz + 17, seed + 666u) * 0.35f;
                            float roll = hash2D(gx * 41 + t, gz * 47, seed + 711u);
                            if (roll < 0.22f) {
                                chunk.addSprite(Sprite3DInstance{{posX + jx, posY, posZ + jz}, 0.9f * fScale, 0.9f * fScale, TexLayer::SpriteWildflowerBlue, SpriteFlags::WindSway, phase});
                            } else {
                                chunk.addSprite(Sprite3DInstance{{posX + jx, posY, posZ + jz}, 0.95f * fScale, 0.85f * fScale, TexLayer::SpriteGrassTuft, SpriteFlags::WindSway, phase});
                            }
                        }
                    }
                } else if (elev >= 4 && elev < 10) {
                    // Lush Meadows: 3D Emerald Oaks, Golden Autumn Groves, 3D Pines, Moss Boulders, Diverse Grass & Multi-color Wildflowers
                    if (rnd > 0.988f) {
                        float autumnNoise = fbm(wx * 0.008f + 50.0f, wz * 0.008f + 50.0f, 2, seed + 919u);
                        if (autumnNoise > 0.62f) {
                            RealmWorld::emitAutumnTree3D(chunk.propVertices(), {posX, posY, posZ}, treeScale, rot);
                        } else if (rnd > 0.994f) {
                            RealmWorld::emitPineTree3D(chunk.propVertices(), {posX, posY, posZ}, treeScale, rot);
                        } else {
                            RealmWorld::emitOakTree3D(chunk.propVertices(), {posX, posY, posZ}, treeScale, rot, TexLayer::OakFoliage);
                        }
                    } else if (rnd > 0.984f && rnd <= 0.988f && distToRiver < 14.0f) {
                        RealmWorld::emitMossBoulder3D(chunk.propVertices(), {posX, posY, posZ}, 1.0f * treeScale, 0.60f * treeScale, 0.95f * treeScale, rot);
                    } else {
                        // Sprawling Octopath-style wind-swaying meadow vegetation (grass tufts, tall grass, clovers, wildflowers)
                        float baseTufts = 1.75f;
                        int numTufts = static_cast<int>(baseTufts) + ((hash2D(gx, gz, seed + 11u) < (baseTufts - 1.0f)) ? 1 : 0);
                        for (int t = 0; t < numTufts; ++t) {
                            float jx = (hash2D(gx * 17 + t * 7, gz * 23 + t * 13, seed + 101u) - 0.5f) * (RealmChunk::kTileSize * 0.78f);
                            float jz = (hash2D(gx * 29 + t * 11, gz * 31 + t * 17, seed + 202u) - 0.5f) * (RealmChunk::kTileSize * 0.78f);
                            float tuftX = posX + jx;
                            float tuftZ = posZ + jz;
                            float flowerNoise = fbm(wx * 0.08f + jx, wz * 0.08f + jz, 2, seed + 333u);
                            float cloverNoise = fbm(wx * 0.14f, wz * 0.14f, 2, seed + 444u);
                            float typeRoll = hash2D(gx * 37 + t * 9, gz * 43 + t * 5, seed + 555u);
                            float phase = hash2D(gx * 53 + t * 3, gz * 59 + t * 7, seed + 777u) * 6.28f;
                            float fScale = 0.85f + hash2D(gx + 13 + t, gz + 17, seed + 666u) * 0.35f;

                            if (typeRoll < 0.18f) {
                                uint16_t fl = (flowerNoise < 0.35f) ? TexLayer::SpriteWildflowerRed :
                                              ((flowerNoise < 0.68f) ? TexLayer::SpriteWildflowerYellow : TexLayer::SpriteWildflowerBlue);
                                chunk.addSprite(Sprite3DInstance{{tuftX, posY, tuftZ}, 0.95f * fScale, 0.95f * fScale, fl, SpriteFlags::WindSway, phase});
                            } else if (cloverNoise > 0.65f) {
                                chunk.addSprite(Sprite3DInstance{{tuftX, posY, tuftZ}, 0.92f * fScale, 0.70f * fScale, TexLayer::SpriteCloverPatch, SpriteFlags::WindSway, phase});
                            } else if (typeRoll < 0.45f || distToRiver < 13.0f) {
                                chunk.addSprite(Sprite3DInstance{{tuftX, posY, tuftZ}, 1.05f * fScale, 1.35f * fScale, TexLayer::SpriteTallGrass, SpriteFlags::WindSway, phase});
                            } else {
                                chunk.addSprite(Sprite3DInstance{{tuftX, posY, tuftZ}, 0.95f * fScale, 0.85f * fScale, TexLayer::SpriteGrassTuft, SpriteFlags::WindSway, phase});
                            }
                        }
                    }
                }
            }
        }
    }

    // 7. Manifest Historical POI 3D Props, Inhabitants & Dynamic Lights if Epicenter falls in this chunk
    if (nearestSite.type != MacroSiteType::Wilderness &&
        nearestSite.worldX >= chunkOriginX && nearestSite.worldX < chunkOriginX + RealmChunk::kChunkSize &&
        nearestSite.worldZ >= chunkOriginZ && nearestSite.worldZ < chunkOriginZ + RealmChunk::kChunkSize) {

        int slx = std::clamp(static_cast<int>((nearestSite.worldX - chunkOriginX) / RealmChunk::kTileSize), 0, RealmChunk::kTilesPerChunk - 1);
        int slz = std::clamp(static_cast<int>((nearestSite.worldZ - chunkOriginZ) / RealmChunk::kTileSize), 0, RealmChunk::kTilesPerChunk - 1);
        float siteY = static_cast<float>(chunk.getTile(slx, slz).elevation) * RealmChunk::kElevStep;
        float sx = nearestSite.worldX;
        float sz = nearestSite.worldZ;

        switch (nearestSite.type) {
            case MacroSiteType::CalamityRupture: {
                // Prismatic Arcane Crystal Spires bursting from shattered crater
                emitChunkCrystalSpire(chunk.propVertices(), {sx, siteY, sz}, 1.8f, 6.8f, 0.08f, -0.12f, TexLayer::WindowWarmGlow);
                emitChunkCrystalSpire(chunk.propVertices(), {sx - 3.5f, siteY, sz + 2.0f}, 1.4f, 5.2f, -0.15f, 0.10f, TexLayer::WindowWarmGlow);
                emitChunkCrystalSpire(chunk.propVertices(), {sx + 3.0f, siteY, sz - 2.5f}, 1.5f, 5.6f, 0.18f, 0.05f, TexLayer::WindowWarmGlow);
                emitChunkCrystalSpire(chunk.propVertices(), {sx + 2.2f, siteY, sz + 3.5f}, 1.2f, 4.2f, -0.05f, -0.18f, TexLayer::WindowWarmGlow);
                emitChunkCrystalSpire(chunk.propVertices(), {sx - 2.8f, siteY, sz - 3.0f}, 1.3f, 4.8f, 0.12f, 0.14f, TexLayer::WindowWarmGlow);

                // Shattered stone containment circle
                emitChunkBox(chunk.propVertices(), {sx - 5.5f, siteY, sz - 5.5f}, {sx + 5.5f, siteY + 0.6f, sz + 5.5f}, TexLayer::StoneBrick, 0.5f);

                // Corrupted Wandering Phantom of the Transmuter
                chunk.addSprite(Sprite3DInstance{{sx, siteY + 0.6f, sz}, 1.9f, 2.5f, TexLayer::SpriteScholar, SpriteFlags::IdleBob | SpriteFlags::Emissive, 0.0f});

                // Vibrant Violet/Magenta Arcane Rift Point Light
                chunk.addPointLight(makeChunkPointLight({sx, siteY + 2.8f, sz}, {0.85f, 0.22f, 1.0f}, 25.0f, 3.5f));
                break;
            }
            case MacroSiteType::WizardSanctum: {
                // 3D Stepped Wizard Spire Tower (12m tall)
                emitChunkBox(chunk.propVertices(), {sx - 3.0f, siteY, sz - 3.0f}, {sx + 3.0f, siteY + 4.5f, sz + 3.0f}, TexLayer::StoneBrick);
                emitChunkBox(chunk.propVertices(), {sx - 2.4f, siteY + 4.5f, sz - 2.4f}, {sx + 2.4f, siteY + 8.0f, sz + 2.4f}, TexLayer::WindowWarmGlow, 0.5f, 0.2f, 2u);
                emitChunkBox(chunk.propVertices(), {sx - 1.8f, siteY + 8.0f, sz - 1.8f}, {sx + 1.8f, siteY + 12.0f, sz + 1.8f}, TexLayer::SlateRoof);

                // Resident Archmage in Celestial Robes
                chunk.addSprite(Sprite3DInstance{{sx + 3.8f, siteY, sz + 1.0f}, 1.9f, 2.5f, TexLayer::SpriteScholar, SpriteFlags::IdleBob, 0.4f});

                // Golden Starlight Observatory Point Light
                chunk.addPointLight(makeChunkPointLight({sx, siteY + 8.5f, sz}, {1.0f, 0.85f, 0.45f}, 24.0f, 3.0f));
                break;
            }
            case MacroSiteType::SunkenRuins: {
                // Weathered pillars & fallen archway blocks
                for (int p = 0; p < 4; ++p) {
                    float px = sx + ((p % 2 == 0) ? -4.5f : 4.5f);
                    float pz = sz + ((p / 2 == 0) ? -4.5f : 4.5f);
                    float ph = 3.0f + static_cast<float>(p % 3) * 1.5f;
                    emitChunkBox(chunk.propVertices(), {px - 0.7f, siteY, pz - 0.7f}, {px + 0.7f, siteY + ph, pz + 0.7f}, TexLayer::StoneBrick);
                }
                emitChunkBox(chunk.propVertices(), {sx - 5.0f, siteY + 4.2f, sz - 0.7f}, {sx + 5.0f, siteY + 5.2f, sz + 0.7f}, TexLayer::StoneBrick);

                // Resident Spectral Wolf
                chunk.addSprite(Sprite3DInstance{{sx, siteY, sz}, 2.2f, 2.0f, TexLayer::SpriteWolf, SpriteFlags::IdleBob, 1.2f});
                break;
            }
            case MacroSiteType::SettlementHamlet: {
                // Gabled Timber Cottage
                emitChunkBox(chunk.propVertices(), {sx - 3.2f, siteY, sz - 2.5f}, {sx + 3.2f, siteY + 3.5f, sz + 2.5f}, TexLayer::TimberWall);
                emitChunkBox(chunk.propVertices(), {sx - 3.4f, siteY + 3.5f, sz - 2.7f}, {sx + 3.4f, siteY + 5.2f, sz + 2.7f}, TexLayer::TerracottaRoof);

                // Resident Villagers & Merchants
                chunk.addSprite(Sprite3DInstance{{sx + 3.5f, siteY, sz + 1.2f}, 1.8f, 2.3f, TexLayer::SpriteVillager, SpriteFlags::IdleBob, 0.8f});
                chunk.addSprite(Sprite3DInstance{{sx - 1.5f, siteY, sz + 3.5f}, 1.9f, 2.4f, TexLayer::SpriteMerchant, SpriteFlags::IdleBob, 1.6f});

                // Warm Village Hearthfire Point Light
                chunk.addPointLight(makeChunkPointLight({sx, siteY + 1.2f, sz}, {1.0f, 0.65f, 0.25f}, 18.0f, 2.2f));
                break;
            }
            case MacroSiteType::AlpineMonastery: {
                // Mountain Stone Chapel
                emitChunkBox(chunk.propVertices(), {sx - 2.8f, siteY, sz - 2.2f}, {sx + 2.8f, siteY + 3.8f, sz + 2.2f}, TexLayer::StoneBrick);
                emitChunkBox(chunk.propVertices(), {sx - 3.0f, siteY + 3.8f, sz - 2.4f}, {sx + 3.0f, siteY + 5.5f, sz + 2.4f}, TexLayer::SlateRoof);

                // Resident Monk / Ascetic Cleric
                chunk.addSprite(Sprite3DInstance{{sx + 1.5f, siteY, sz + 2.5f}, 1.8f, 2.4f, TexLayer::SpriteCleric, SpriteFlags::IdleBob, 0.5f});

                // Quiet Sanctuary Candle Lantern
                chunk.addPointLight(makeChunkPointLight({sx, siteY + 1.5f, sz}, {1.0f, 0.78f, 0.45f}, 18.0f, 2.0f));
                break;
            }
            default:
                break;
        }
    }

    // Update downsampled pyramids
    chunk.computeElevationPyramid();
}

} // namespace eatsrealms

