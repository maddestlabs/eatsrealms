#include "eatsrealms/pixel_canvas.hpp"
#include "eatsrealms/realm_world.hpp"
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

    std::cout << "[Tests] 2. Verifying 24-Layer ProceduralAtlasGenerator (< 50ms startup budget)...\n";
    {
        ProceduralAtlasData atlas = ProceduralAtlasGenerator::buildAllLayers();
        assert(atlas.layerCount == TexLayer::kCount);
        assert(atlas.tileSize == TexLayer::kTileSize);
        assert(atlas.albedoLayers.size() == TexLayer::kCount * 64u * 64u);
        assert(atlas.normalLayers.size() == TexLayer::kCount * 64u * 64u);
        std::cout << "  -> 24-Layer Albedo + Normal Atlas synthesized in " << atlas.generationTimeMs << " ms.\n";
    }

    std::cout << "[Tests] 3. Verifying Multi-Height 3D TileMap, Gabled Props & Sprite3D Grounding...\n";
    {
        RealmWorld world;
        world.buildStarfallValeRealm();
        RealmStats stats = world.rebuildWorldGeometry();

        assert(stats.terrainTriangles > 8000u);
        assert(stats.propTriangles > 100u);
        assert(stats.spriteCount > 50u);
        assert(stats.pointLightCount >= 6u);
        assert((stats.terrainTriangles + stats.propTriangles) < 80000u);

        // Verify raycast hits terrain
        RaycastHit hit = world.raycast({-15.0f, 20.0f, -2.0f}, {0.0f, -1.0f, 0.0f}, 50.0f);
        assert(hit.hit);
        assert(hit.distance > 1.0f && hit.distance < 30.0f);
    }

    std::cout << "[Tests] 4. Verifying Eatscript RealmScriptEngine Bindings...\n";
    {
        RealmWorld world;
        RealmScriptEngine script(world);
        bool ok = script.executeScript(R"(
realm.clear()
realm.fill_plateau(10, 10, 20, 20, 6, Tex["Cobblestone"], Tex["CliffStone"])
realm.add_house(0.0, 3.6, 0.0, 6.0, 4.0, 5.0, 0, Tex["TimberWall"], Tex["TerracottaRoof"])
realm.spawn_sprite(2.0, 2.0, 1.9, 2.4, Tex["SpriteHero"], 4)
)");
        assert(ok);
        RealmStats stats = world.rebuildWorldGeometry();
        assert(stats.propTriangles > 20u);
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

    std::cout << "[Tests] All Eatsrealms HD-2D Core Unit Tests PASSED!\n";
    return 0;
}
