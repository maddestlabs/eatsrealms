#include "eatsrealms/pixel_canvas.hpp"
#include "eatsrealms/realm_world.hpp"
#include "eatsrealms/world_generator.hpp"
#include "eatsrealms/history_sim.hpp"
#include "eatsrealms/script_bindings.hpp"
#include "eatsrealms/audio_foley.hpp"
#include <iostream>
#include <cassert>
#include <cmath>
#include <vector>

using namespace eatsrealms;

int main() {
    std::cout << "[Tests] 1. Verifying C++20 PixelCanvas & Sobel Normal-Map Generator...\n";
    {
        PixelCanvas canvas(64, 64);
        canvas.clear(RGBA8{100, 120, 140, 255}, 0.5f, 0);
        canvas.fillCircle(32, 32, 12, RGBA8{220, 180, 90, 255}, 0.95f, 0.25f, 180);

        auto normalMap = canvas.computeNormalEmissiveMap(3.0f, true);
        assert(normalMap.size() == 64u * 64u);

        // Center pixel of dome should have high emissive (180) and near-vertical Z normal
        RGBA8 centerN = RGBA8::unpack(normalMap[32 * 64 + 32]);
        assert(centerN.a == 180);
        assert(centerN.b > 200);
    }

    std::cout << "[Tests] 2. Verifying " << TexLayer::kCount << "-Layer ProceduralAtlasGenerator (< 50ms startup budget)...\n";
    {
        ProceduralAtlasData atlas = ProceduralAtlasGenerator::buildAllLayers();
        assert(atlas.layerCount == TexLayer::kCount);
        assert(atlas.tileSize == TexLayer::kTileSize);
        assert(atlas.albedoLayers.size() == TexLayer::kCount * 64u * 64u);
        assert(atlas.normalLayers.size() == TexLayer::kCount * 64u * 64u);
        std::cout << "  -> " << TexLayer::kCount << "-Layer Albedo + Normal Atlas synthesized in " << atlas.generationTimeMs << " ms.\n";
    }

    std::cout << "[Tests] 3. Verifying Multi-Height 3D TileMap, Gabled Props & Sprite3D Grounding...\n";
    {
        RealmWorld world;
        world.buildStarfallValeRealm();
        RealmStats stats = world.rebuildWorldGeometry();

        assert(stats.terrainTriangles > 8000u);
        assert(stats.propTriangles > 100u);
        assert(stats.spriteCount > 20u);
        assert(stats.pointLightCount >= 6u);
        assert((stats.terrainTriangles + stats.propTriangles) < 90000u);

        // Verify raycast hits terrain
        RaycastHit hit = world.raycast({-15.0f, 20.0f, -2.0f}, {0.0f, -1.0f, 0.0f}, 50.0f);
        assert(hit.hit);
        assert(hit.distance > 1.0f && hit.distance < 30.0f);
    }

    std::cout << "[Tests] 4. Verifying Eatscript RealmScriptEngine Bindings & 3D Trees...\n";
    {
        RealmWorld world;
        RealmScriptEngine script(world);
        bool ok = script.executeScript(R"(
realm.clear()
realm.fill_plateau(10, 10, 20, 20, 6, Tex["Cobblestone"], Tex["CliffStone"])
realm.add_house(0.0, 3.6, 0.0, 6.0, 4.0, 5.0, 0, Tex["TimberWall"], Tex["TerracottaRoof"])
realm.spawn_sprite(2.0, 2.0, 1.9, 2.4, Tex["SpriteHero"], 4)
realm.add_oak_tree(-6.0, 3.6, 4.0, 1.1, 0.5)
realm.add_snow_pine_tree(6.0, 3.6, 4.0, 1.2, 1.0)
realm.add_autumn_tree(-6.0, 3.6, -4.0, 1.0, 1.5)
realm.add_dead_tree(6.0, 3.6, -4.0, 0.9, 2.0)
)");
        assert(ok);
        (void)ok;
        RealmStats stats = world.rebuildWorldGeometry();
        assert(stats.propTriangles > 500u);
        assert(stats.spriteCount == 1u);
    }

    std::cout << "[Tests] 5. Verifying Headless AudioFoleyEngine Synthesis...\n";
    {
        AudioFoleyEngine foley;
        foley.init(false);
        foley.triggerMaterialImpact(TexLayer::Cobblestone, 0.9f);
        foley.triggerMaterialImpact(TexLayer::WaterSurface, 0.8f);

        std::vector<float> stereoBuf(512 * 2, 0.0f);
        foley.mixSamples(stereoBuf.data(), 512, 48000);

        float maxAmp = 0.0f;
        for (float s : stereoBuf) {
            maxAmp = std::max(maxAmp, std::abs(s));
        }
        assert(maxAmp > 0.01f && maxAmp <= 0.95f);
    }

    std::cout << "[Tests] 6. Verifying Multi-Pass Procedural World Generator & Compound Props...\n";
    {
        RealmWorld world;
        ProceduralRealmConfig config{};
        config.seed = 2026u;
        config.treeCount = 90;
        config.wildflowerCount = 60;
        ProceduralWorldGenerator::generate(world, config);
        RealmStats stats = world.rebuildWorldGeometry();

        assert(stats.terrainTriangles > 8000u);
        assert(stats.propTriangles > 150u);
        assert(stats.spriteCount > 40u);
        assert(stats.pointLightCount >= 6u);

        // Test Eatscript realm.generate() and compound props
        RealmScriptEngine script(world);
        bool ok = script.executeScript(R"(
realm.generate(seed=777)
realm.add_crates(-10.0, 3.6, -2.0, 3)
realm.add_barrel(-8.0, 3.6, -2.0)
realm.add_flowerbox(-6.0, 3.6, -2.0, 1.8, 0)
realm.add_market_stall(-4.0, 3.6, -2.0, 3.8, 2.6, Tex["TerracottaRoof"])
)");
        assert(ok);
        (void)ok;
        RealmStats scriptStats = world.rebuildWorldGeometry();
        assert(scriptStats.propTriangles > 150u);
        std::cout << "  -> Procedural Realm synthesized: " << scriptStats.terrainTriangles
                  << " terrain tris, " << scriptStats.propTriangles
                  << " prop tris, " << scriptStats.spriteCount
                  << " sprites, " << scriptStats.pointLightCount << " lights.\n";
    }

    std::cout << "[Tests] 7. Verifying Narrative History Simulator & Calamity Diorama...\n";
    {
        RealmWorld world;
        ChronicleRecord chronicle = HistorySimulator::simulate(world, 999u);
        assert(!chronicle.actors.empty());
        assert(!chronicle.events.empty());
        assert(!chronicle.notes.empty());
        assert(chronicle.settlementName == "Blackhollow Vale");

        std::string md = chronicle.generateSummaryMarkdown();
        assert(md.find("Archaeology of Intent") != std::string::npos);
        assert(md.find("Alden the Miller") != std::string::npos);
        assert(md.find("Experiment IX") != std::string::npos);

        RealmStats stats = world.rebuildWorldGeometry();
        assert(stats.terrainTriangles > 8000u);
        assert(stats.propTriangles > 150u);
        assert(stats.spriteCount >= 4u);
        assert(stats.pointLightCount >= 3u);

        // Test Eatscript realm.build_chronicle_hamlet()
        RealmScriptEngine script(world);
        bool ok = script.executeScript(R"(
realm.build_chronicle_hamlet(seed=123)
)");
        assert(ok);
        (void)ok;
        RealmStats scriptStats = world.rebuildWorldGeometry();
        assert(scriptStats.propTriangles > 150u);
        std::cout << "  -> Chronicle Realm synthesized: " << stats.terrainTriangles
                  << " terrain tris, " << stats.propTriangles
                  << " prop tris, " << chronicle.events.size() << " narrative events logged.\n";
    }

    std::cout << "[Tests] 8. Verifying Sloping Terrain, Continuous Height Sampling & Highland Bridge Scene...\n";
    {
        RealmWorld world;
        world.clear();

        // 1. Sloping terrain corner elevations
        world.setTile(10, 10, 4, TexLayer::Cobblestone, TexLayer::CliffStone);
        world.setTileSlope(10, 10, SlopeDir::PlusX, 2, false);
        const auto& cell = world.getTile(10, 10);
        assert(cell.isSloped());
        assert(cell.elevation == 4);
        assert(cell.getCornerElevation(0) == 4); // NW
        assert(cell.getCornerElevation(1) == 6); // NE (+X is raised)
        assert(cell.getCornerElevation(2) == 6); // SE (+X is raised)
        assert(cell.getCornerElevation(3) == 4); // SW
        (void)cell;

        // 2. Analytical continuous bilinear height query
        // Center of cell (10.5, 10.5) should have elevation 5.0 -> world height = 5.0 * 0.6 = 3.0f
        float midWorldX = (10.5f - 32.0f) * 1.6f;
        float midWorldZ = (10.5f - 32.0f) * 1.6f;
        float hSample = world.getTerrainHeightAt(midWorldX, midWorldZ);
        assert(std::abs(hSample - 3.0f) < 0.05f);
        (void)hSample;

        // 3. Multi-tile slope generation and Eatscript execution
        world.addSlope(12, 10, 16, 14, 2, 6, SlopeDir::PlusZ, TexLayer::Cobblestone, TexLayer::CliffStone, false);
        RealmStats slopeStats = world.rebuildWorldGeometry();
        assert(slopeStats.terrainTriangles > 0u);
        (void)slopeStats;

        // 4. Procedural Highland Bridge scene verification
        world.buildHighlandBridgeRealm(42u);
        RealmStats bridgeStats = world.rebuildWorldGeometry();
        assert(bridgeStats.terrainTriangles > 6000u);
        assert(bridgeStats.propTriangles > 200u);
        assert(bridgeStats.spriteCount >= 4u);
        assert(bridgeStats.pointLightCount >= 1u);

        // 5. Eatscript realm.build_highland_bridge binding
        RealmScriptEngine script(world);
        bool ok = script.executeScript(R"(
realm.build_highland_bridge(seed=99)
realm.add_roadside_beacon(8.0, 2.4, -4.0, 0.2, 1.8, 0.6)
)");
        assert(ok);
        (void)ok;
        RealmStats scriptStats = world.rebuildWorldGeometry();
        assert(scriptStats.propTriangles > 200u);
        std::cout << "  -> Highland Bridge Realm synthesized: " << bridgeStats.terrainTriangles
                  << " terrain tris, " << bridgeStats.propTriangles
                  << " prop tris, " << bridgeStats.spriteCount
                  << " sprites, " << bridgeStats.pointLightCount << " lights.\n";
    }

    std::cout << "[Tests] 9. Verifying Open World Hierarchical Block LODs, Greedy Meshing & Clipmap Streaming...\n";
    {
        // 1. Chunk Downsampled Pyramid Verification
        RealmChunk chunk({0, 0});
        for (int lz = 0; lz < RealmChunk::kTilesPerChunk; ++lz) {
            for (int lx = 0; lx < RealmChunk::kTilesPerChunk; ++lx) {
                TileCell t{};
                t.elevation = (lx < 16) ? 4 : 8;
                t.topLayer = (lx < 16) ? TexLayer::GrassMeadow : TexLayer::GrassHighland;
                t.cliffLayer = TexLayer::CliffStone;
                chunk.setTile(lx, lz, t);
            }
        }
        chunk.computeElevationPyramid();
        assert(chunk.lod1()[0].elevation == 4);
        assert(chunk.lod1()[15].elevation == 8);
        assert(chunk.lod2()[0].elevation == 4);
        assert(chunk.lod2()[7].elevation == 8);
        assert(chunk.lod3()[0].elevation == 4);
        assert(chunk.lod3()[3].elevation == 8);

        // 2. 2D Greedy Meshing Compression Verification
        // An unmerged 16x16 LOD 1 grid would emit 16 * 16 * 2 = 512 top triangles.
        // Our 2-plateau chunk (left half elevation 4, right half elevation 8) merges into minimal quads!
        std::vector<WorldVertex> greedyVerts;
        chunk.buildGreedyMesh(ChunkLOD::LOD1_Simplified, greedyVerts);
        uint32_t greedyTriangles = static_cast<uint32_t>(greedyVerts.size() / 3u);
        assert(greedyTriangles < 30u); // Tremendous compression compared to 512+ triangles!
        std::cout << "  -> 2D Greedy Mesher compressed 512+ unmerged raw blocks into "
                  << greedyTriangles << " triangles.\n";

        // 3. Open World Multi-Chunk Generation & Clipmap Streaming Verification
        RealmWorld world;
        world.generateOpenWorld(1337u, 2); // 5x5 = 25 chunks covering 240m x 240m
        assert(world.isOpenWorld());
        assert(world.chunks().size() == 25u);

        // Center camera at world origin (0, 0, 0)
        world.camera().target = {0.0f, 2.0f, 0.0f};
        world.updateStreamingClipmap(world.camera().target, 70.0f, 200.0f, 600.0f);

        // Verify chunk LOD assignment:
        // Chunk (0, 0) is at distance ~0m -> LOD 0
        const RealmChunk* ch0 = world.getChunk(0, 0);
        assert(ch0 != nullptr);
        assert(ch0->currentLOD() == ChunkLOD::LOD0_MicroDiorama);
        (void)ch0;

        // Chunks on outer ring (e.g. (+2, +2) -> distance ~135m) -> LOD 1
        const RealmChunk* ch2 = world.getChunk(2, 2);
        assert(ch2 != nullptr);
        assert(ch2->currentLOD() == ChunkLOD::LOD1_Simplified);
        (void)ch2;

        // 4. Rebuild Geometry & Check Shadow Pass Exclusion + Flora Culling
        RealmStats openStats = world.rebuildWorldGeometry();
        assert(openStats.activeChunks == 25u);
        assert(openStats.terrainTriangles > 0u);
        assert(openStats.lod0Triangles > 0u);
        assert(openStats.lod1Triangles > 0u);
        assert(world.lod0VertexCount() > 0u);
        assert(world.lod0VertexCount() < world.worldVertices().size()); // Distant LOD 1-3 chunks excluded from shadow pass!

        std::cout << "  -> Open World (25 Chunks, 240m x 240m) synthesized: "
                  << openStats.terrainTriangles << " terrain tris (LOD0: " << openStats.lod0Triangles
                  << ", LOD1: " << openStats.lod1Triangles << "), "
                  << openStats.spriteCount << " active sprites ("
                  << openStats.culledSprites << " distant micro-flora culled).\n";

        // 5. Verify Eatscript Open World Bindings
        RealmScriptEngine script(world);
        bool ok = script.executeScript(R"(
realm.generate_open_world(seed=42, radius=2)
assert(realm.is_open_world())
assert(realm.chunk_count() == 25)
realm.update_clipmap(48.0, 48.0)
)");
        assert(ok);
        (void)ok;
    }

    std::cout << "[Tests] 10. Verifying Macro-History Grid, Regional POIs & Narrative Lore Integration...\n";
    {
        uint32_t worldSeed = 7331u;
        MacroSiteInfo originSite = MacroHistoryGrid::evaluateRegion(0, 0, worldSeed);
        // Discover nearest macro POI around origin
        MacroSiteInfo nearestPOI = MacroHistoryGrid::getNearestSite(0.0f, 0.0f, worldSeed);
        assert(nearestPOI.type != MacroSiteType::Wilderness);
        assert(!nearestPOI.name.empty());
        assert(!nearestPOI.notableFigure.empty());
        assert(!nearestPOI.loreHeadline.empty());
        assert(!nearestPOI.chronicleExcerpt.empty());
        assert(nearestPOI.yearOccurred > 0);

        std::cout << "  -> Discovered Macro Site: [" << nearestPOI.name << "] ("
                  << nearestPOI.notableFigure << ", Year " << nearestPOI.yearOccurred << ")\n"
                  << "     Lore: \"" << nearestPOI.loreHeadline << "\"\n"
                  << "     Location: (" << nearestPOI.worldX << ", " << nearestPOI.worldZ << ")\n";

        // Generate open world around the nearest POI
        RealmWorld world;
        world.generateOpenWorld(worldSeed, 2, nearestPOI.worldX, nearestPOI.worldZ);

        assert(world.isOpenWorld());
        assert(world.chunks().size() == 25u);

        // Rebuild geometry and verify POI architectural props and dynamic point lights were harvested
        RealmStats stats = world.rebuildWorldGeometry();
        assert(stats.propTriangles > 0u);
        assert(stats.pointLightCount >= 1u);
        std::cout << "  -> Streamed POI Realm (" << nearestPOI.name << "): "
                  << stats.propTriangles << " prop tris, "
                  << stats.pointLightCount << " dynamic point lights active.\n";

        // Verify Eatscript realm.get_macro_site, get_terrain_height, and stdlib print()
        RealmScriptEngine script(world);
        bool ok = script.executeScript(R"(
let site = realm.get_macro_site(0.0, 0.0)
assert(site.name != "")
assert(site.dist >= 0.0)
let h = realm.get_terrain_height(site.x, site.z)
print("  -> Eatscript POI Query: " + site.name + " (" + site.figure + ") at dist " + str(site.dist) + "m, ground h=" + str(h))
)");
        assert(ok);
        (void)ok;
    }

    std::cout << "[Tests] All Eatsrealms HD-2D Core Unit Tests PASSED!\n";
    return 0;
}
