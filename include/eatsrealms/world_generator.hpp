#ifndef EATSREALMS_WORLD_GENERATOR_HPP
#define EATSREALMS_WORLD_GENERATOR_HPP

#include "eatsrealms/realm_world.hpp"
#include <cstdint>

namespace eatsrealms {

/**
 * Procedural generation configuration for HD-2D diorama realms.
 * Inspired by Octopath Traveler and Lumina's multi-pass deterministic world generation.
 */
struct ProceduralRealmConfig {
    uint32_t seed{1337u};

    // Topography & Elevation
    int16_t baseElevation{3};
    int16_t villageElevation{6};
    int16_t sanctuaryElevation{10};
    int16_t mountainMaxElevation{16};
    float terrainRoughness{0.75f};

    // Hydrology & River
    bool enableRiver{true};
    float riverWiggle{4.8f};
    int pondStartZ{4};
    int pondEndZ{15};

    // Village Settlements & Architecture
    int villageHouseCount{5};
    bool dressPerimeters{true};
    bool addMarketStalls{true};
    bool addFarmland{true};
    bool addHighlandRuins{true};

    // Flora, Vegetation & Sightlines
    int treeCount{110};
    int wildflowerCount{80};
    bool enableSightlineCulling{true};

    // NPCs & Entities
    bool spawnHeroParty{true};
    bool spawnVillagers{true};
    bool spawnMonsters{true};
};

/**
 * Procedural World Generator:
 * Generates rich, coherent 64x64 HD-2D realms using deterministic multi-pass synthesis:
 *  1. Multi-Tier Relief (rolling meadows, village terrace, sanctuary acropolis, northern mountain ridge)
 *  2. Hydrology & River Meander (winding riverbed, sand shorelines, southern millpond)
 *  3. Road & Path Networks (cobblestone town square, connecting thoroughfares, dirt wilderness trails)
 *  4. Automated Cliff Staircases (detects road-to-cliff crossings and synthesizes directional stone stairs)
 *  5. Clustered Architecture & Perimeter Dressing (cottages, tavern, watermill, stone chimneys, flowerboxes, crates, barrels)
 *  6. River Bridges & Street Lantern Networks (arched timber bridge, perimeter lanterns with point lights)
 *  7. Farmland & Crop Plots (fenced furrowed soil with rural props)
 *  8. Highland Ancient Sanctuary (stone archways, ruined pillars, glowing torch braziers)
 *  9. Biome-Aware Flora with Camera Sightline Culling (pines on peaks, oaks in meadows, flowers by rivers,
 *     with camera occlusion avoidance so trees never obscure interactive NPCs, doors, or bridge walkways)
 * 10. Living Entities & Golden-Hour Atmosphere (hero party, villagers, merchant, guards, wildlife, dynamic lights)
 */
class ProceduralWorldGenerator {
public:
    static void generate(RealmWorld& world, const ProceduralRealmConfig& config = ProceduralRealmConfig{});

    // Recreates the signature HD-2D woodland forest trail aesthetic (from the reference screenshot):
    // Stepped mossy contour terraces, overhanging grass lips, winding dirt trail, backlit pines, ferns, and radiant sunbeam pool
    static void generateWoodlandTrail(RealmWorld& world, uint32_t seed = 42u);

    // Recreates the signature Octopath Traveler coastal cove & rocky sea stacks aesthetic:
    // Stratified coastal limestone cliffs, offshore sea stack pillars with crashing surf,
    // crystal turquoise to deep azure ocean with animated wave harmonics and diamond sparkles,
    // tropical curved palm trees with fronds, rope suspension bridge, party members, and treasure chest
    static void generateCoastalCove(RealmWorld& world, uint32_t seed = 777u);

    // Simulates an entire human settlement through simulated characters and historical eras
    // ("The Archaeology of Intent"): Pioneers settle the river mill -> Crossroads prosperity ->
    // The Alchemist's secret subterranean crypt -> Containment breach & calamity -> Overgrown ruin diorama.
    static void generateChronicleHamlet(RealmWorld& world, uint32_t seed = 999u);

    // Recreates the iconic Octopath Traveler mountain river gorge crossing:
    // Multi-tiered stepped grass contour terraces, smooth sloping cobblestone roads winding down the hillsides,
    // rustic timber suspension footbridge with tall slanted A-frame masts, diagonal tension stays,
    // hero party crossing the river, glowing emerald roadside lantern beacon, background trestle bridge,
    // distant pine covered islands, and cinematic tilt-shift miniature depth of field.
    static void generateHighlandBridge(RealmWorld& world, uint32_t seed = 42u);

    // Procedurally generates an open-world chunk at (cx, cz) with continuous deterministic
    // multi-frequency terrain, biome synthesis (snow peaks, highland terraces, valleys, rivers),
    // macro landmarks, and elevation pyramid updates.
    static void generateProceduralChunk(class RealmChunk& chunk, uint32_t seed = 1337u);
};

} // namespace eatsrealms

#endif // EATSREALMS_WORLD_GENERATOR_HPP
