#include "eatsrealms/history_sim.hpp"
#include "eatsrealms/pixel_canvas.hpp"
#include <cmath>
#include <sstream>
#include <iomanip>
#include <algorithm>

namespace eatsrealms {

namespace {

inline float hash1D(uint32_t x, uint32_t seed) noexcept {
    uint32_t h = x * 374761393u + seed * 1442695041u;
    h = (h ^ (h >> 13u)) * 1274126177u;
    return static_cast<float>((h ^ (h >> 16u)) & 0x00FFFFFFu) / static_cast<float>(0x01000000u);
}

inline float fbm(float x, float y, int octaves, uint32_t seed) noexcept {
    return PixelCanvas::fbm2D(x, y, octaves, seed);
}

inline int16_t quantizeElevation(float h) noexcept {
    if (h < 2.0f) return 2;
    if (h < 4.5f) return 4;
    if (h < 7.0f) return 6;
    if (h < 9.5f) return 8;
    if (h < 12.0f) return 10;
    if (h < 14.5f) return 12;
    return 16;
}

} // namespace

std::string ChronicleRecord::generateSummaryMarkdown() const {
    std::ostringstream ss;
    ss << "# Chronicle of " << settlementName << "\n\n";
    ss << "*Simulated Seed: " << seed << "*\n\n";

    ss << "## Historical Dramatis Personae\n\n";
    for (const auto& a : actors) {
        ss << "- **" << a.name << "** (" << a.title << ")\n"
           << "  - *Status*: " << (a.survived ? "Survived" : "Fallen") << " — " << a.fate << "\n"
           << "  - *Location*: (" << std::fixed << std::setprecision(1) << a.lastPos.x << ", " << a.lastPos.z << ")\n";
    }

    ss << "\n## Historical Event Timeline (The Archaeology of Intent)\n\n";
    for (const auto& ev : events) {
        std::string eraName;
        switch (ev.era) {
            case HistoricalEra::Founding:   eraName = "I. Foundation"; break;
            case HistoricalEra::Prosperity: eraName = "II. Prosperity"; break;
            case HistoricalEra::Obsession:  eraName = "III. Obsession"; break;
            case HistoricalEra::Calamity:   eraName = "IV. Calamity"; break;
            case HistoricalEra::Reclamation:eraName = "V. Reclamation"; break;
        }
        ss << "### Year " << ev.year << " — [" << eraName << "] " << ev.headline << "\n";
        ss << ev.details << "\n\n";
    }

    ss << "## Discovered Field Notes & Lore Journals\n\n";
    for (const auto& n : notes) {
        ss << "> **\"" << n.title << "\"** — *Penned by " << n.author << "*\n>\n";
        ss << "> *" << n.excerpt << "*\n\n";
    }

    return ss.str();
}

ChronicleRecord HistorySimulator::simulate(RealmWorld& world, uint32_t seed) {
    world.clear();

    ChronicleRecord record{};
    record.seed = seed;
    record.settlementName = "Blackhollow Vale";

    const int gridW = RealmWorld::kGridWidth;
    const int gridD = RealmWorld::kGridDepth;
    const float elevStep = RealmWorld::kElevStep;

    // =========================================================================
    // BASE GEOGRAPHY PASS: River Valley & Limestone Bluffs
    // =========================================================================
    // Stepped terrain with:
    // - Northern mountainous ridge (gz: 46..63, elev 10..16)
    // - High Western Limestone Bluff for the Alchemist's Manor (gx: 4..24, gz: 28..46, elev 10)
    // - Central Crossroads Terrace (gx: 8..28, gz: 12..28, elev 6)
    // - Low Riverbed & Watermill Weir (gx: 29..36, gz: 0..63, elev 1, water 1.35)
    // - Eastern Farmland Terrace (gx: 37..58, gz: 4..30, elev 4)
    // - Sunken Subterranean Crypt Pit excavated into the Western Bluff (gx: 10..20, gz: 36..44, elev 2)
    for (int gz = 0; gz < gridD; ++gz) {
        for (int gx = 0; gx < gridW; ++gx) {
            float nx = static_cast<float>(gx) / static_cast<float>(gridW);
            float nz = static_cast<float>(gz) / static_cast<float>(gridD);

            float baseH = fbm(nx * 3.0f, nz * 3.0f, 3, seed) * 3.2f + 3.0f;

            // Mountain backdrop
            float northFactor = std::clamp((static_cast<float>(gz) - 40.0f) / 20.0f, 0.0f, 1.0f);
            baseH += northFactor * northFactor * 12.0f;

            // West bluff lift
            float westFactor = std::clamp((26.0f - static_cast<float>(gx)) / 20.0f, 0.0f, 1.0f);
            if (gz > 22 && gz < 50) {
                baseH += westFactor * 6.5f;
            }

            int16_t elev = quantizeElevation(baseH);
            uint16_t top = (elev >= 10) ? TexLayer::CliffStone : ((elev >= 6) ? TexLayer::GrassHighland : TexLayer::GrassMeadow);
            world.setTile(gx, gz, elev, top, TexLayer::CliffStone);
        }
    }

    // Terrace plateau flattening for clean HD-2D diorama layers
    world.fillPlateau(8, 12, 26, 26, 6, TexLayer::GrassMeadow, TexLayer::CliffStone);      // Crossroads Village Terrace
    world.fillPlateau(4, 28, 24, 46, 10, TexLayer::GrassHighland, TexLayer::CliffStone);    // High Manor Bluff
    world.fillPlateau(37, 4, 58, 28, 4, TexLayer::GrassMeadow, TexLayer::CliffStone);      // Eastern Farmland
    world.fillPlateau(20, 8, 30, 14, 8, TexLayer::GrassHighland, TexLayer::CliffStone);    // Southern Overlook Bluff
    world.addStairs(23, 13, 26, 15, 8, 6, SlopeDir::PlusZ, TexLayer::Cobblestone);

    // Winding Riverbed with Stone Weir
    float riverWiggleSeed = hash1D(seed, 77u) * 6.28f;
    for (int gz = 0; gz < gridD; ++gz) {
        float meander = std::sin(static_cast<float>(gz) * 0.12f + riverWiggleSeed) * 3.5f;
        int cx = 32 + static_cast<int>(std::round(meander));
        int hw = (gz > 6 && gz < 20) ? 4 : 3;

        int x0 = std::clamp(cx - hw, 2, gridW - 3);
        int x1 = std::clamp(cx + hw, 2, gridW - 3);

        for (int gx = x0; gx <= x1; ++gx) {
            world.setTile(gx, gz, 1, TexLayer::SandShore, TexLayer::CliffStone);
            world.fillRiver(gx, gz, gx, gz, 1, 1.35f);
        }
        world.setTile(x0 - 1, gz, 2, TexLayer::SandShore, TexLayer::CliffStone);
        world.setTile(x1 + 1, gz, 2, TexLayer::SandShore, TexLayer::CliffStone);
    }

    // =========================================================================
    // ERA 1: THE FOUNDING (Years 1 - 22)
    // =========================================================================
    // Alden the Miller establishes the watermill and weir on the eastern riverbank.
    // Settler families arrive, clearing paths and tilling initial crop plots.
    NarrativeActor alden{};
    alden.id = "alden";
    alden.name = "Alden the Miller";
    alden.title = "Founding Patriarch of Blackhollow";
    alden.homePos = {14.0f, 4.0f * elevStep, -8.0f};
    alden.lastPos = {2.0f, 4.0f * elevStep, 2.0f};
    alden.survived = false;
    alden.fate = "Killed at the bridge during the Calamity while holding the river line.";
    record.actors.push_back(alden);

    // Alden's Watermill cottage & timber wheel frame
    world.addGabledHouse(alden.homePos, {6.4f, 4.2f, 5.2f}, 0, TexLayer::TimberWall, TexLayer::SlateRoof, true, true);
    world.addCrateStack({alden.homePos.x - 4.2f, alden.homePos.y, alden.homePos.z + 0.8f}, 3);
    world.addBarrel({alden.homePos.x - 4.2f, alden.homePos.y, alden.homePos.z + 2.0f});

    // Pioneer farmer cottage on eastern terrace
    Vec3f farmCottagePos{22.0f, 4.0f * elevStep, -18.0f};
    world.addGabledHouse(farmCottagePos, {5.8f, 3.8f, 4.8f}, 1, TexLayer::TimberWall, TexLayer::TerracottaRoof, true, false);

    // Fenced farmland and crop furrows
    world.fillPlateau(42, 6, 54, 18, 4, TexLayer::DirtPath, TexLayer::CliffStone);
    world.addRusticFenceGrounded(18.0f, -22.0f, 26.0f, -22.0f, 4);
    world.addRusticFenceGrounded(26.0f, -22.0f, 26.0f, -14.0f, 4);

    // Desire path along the riverbank
    world.fillPlateau(35, 12, 38, 24, 4, TexLayer::DirtPath, TexLayer::CliffStone);

    record.events.push_back({
        HistoricalEra::Founding, 8, "alden",
        "Erection of the River Weir & Watermill",
        "Alden redirects the southern rapids, carving out a timber race and grindstone mill. "
        "The first barley harvest is gathered on the eastern terrace.",
        alden.homePos
    });

    // =========================================================================
    // ERA 2: PROSPERITY & THE CROSSROADS (Years 23 - 45)
    // =========================================================================
    // The village expands across the river onto the high western terrace.
    // The Wayfarer Tavern, central cobblestone plaza, and connecting staircases are built.
    // Master Raymond arrives with royal coin and constructs his imposing ridge manor.
    NarrativeActor marcus{};
    marcus.id = "marcus";
    marcus.name = "Captain Marcus Vane";
    marcus.title = "Commander of the Blackhollow Watch";
    marcus.homePos = {-8.0f, 6.0f * elevStep, 2.0f};
    marcus.lastPos = {0.5f, 4.0f * elevStep, 2.0f};
    marcus.survived = false;
    marcus.fate = "Fell at the improvised road barricade defending the fleeing villagers.";
    record.actors.push_back(marcus);

    NarrativeActor raymond{};
    raymond.id = "raymond";
    raymond.name = "Master Raymond";
    raymond.title = "High Alchemist & Transmuter";
    raymond.homePos = {-18.0f, 10.0f * elevStep, 24.0f};
    raymond.lastPos = {-16.0f, 2.0f * elevStep, 36.0f};
    raymond.survived = false;
    raymond.fate = "Slain in the subterranean crypt when Experiment IX breached containment.";
    record.actors.push_back(raymond);

    // Cobblestone town plaza on western terrace
    world.fillPlateau(10, 14, 24, 25, 6, TexLayer::Cobblestone, TexLayer::StoneBrick);

    // Stone staircases connecting the terrace to the bridge crossing
    world.addStairs(23, 19, 25, 21, 6, 4, SlopeDir::PlusX, TexLayer::Cobblestone);
    world.addStairs(33, 19, 35, 21, 4, 4, SlopeDir::PlusX, TexLayer::Cobblestone);

    // Stone stairs ascending from the village terrace to Raymond's high bluff
    world.addStairs(14, 25, 17, 27, 6, 10, SlopeDir::PlusZ, TexLayer::Cobblestone);

    // Street lanterns illuminating the plaza
    world.addStreetLantern({-6.0f, 6.0f * elevStep, -2.0f});
    world.addStreetLantern({-14.0f, 6.0f * elevStep, 8.0f});

    // Master Raymond's imposing stone manor on the bluff (elev 10)
    world.addGabledHouse(raymond.homePos, {9.2f, 5.2f, 7.2f}, 0, TexLayer::StoneBrick, TexLayer::SlateRoof, true, true);
    world.addStoneArchway({-18.0f, 10.0f * elevStep, 17.5f}, 5.0f, 5.5f, 0);

    record.events.push_back({
        HistoricalEra::Prosperity, 32, "marcus",
        "Chartering of the Blackhollow Crossway & Stone Bridge",
        "The merchant council funds the grand timber arched bridge and the Wayfarer Tavern. "
        "Caravans between the coast and the northern citadels establish Blackhollow as a thriving way-station.",
        {-6.0f, 6.0f * elevStep, 2.0f}
    });

    record.events.push_back({
        HistoricalEra::Prosperity, 41, "raymond",
        "Master Raymond's Seclusion on the Ridge",
        "The former court philosopher buys the western limestone bluff. "
        "High iron-studded gates and stone walls are erected, closing the manor to outsiders.",
        raymond.homePos
    });

    // =========================================================================
    // ERA 3: THE OBSESSION & THE SUBTERRANEAN CRYPT (Years 46 - 58)
    // =========================================================================
    // Raymond excavates a sunken crypt and laboratory into the limestone behind his manor.
    // In this deep sanctum, he constructs cages, blood drainage channels, and a runic transmutation altar.
    // Sunken crypt excavation (gx: 12..20, gz: 35..43, sunken to elev 2)
    world.fillPlateau(12, 35, 20, 43, 2, TexLayer::StoneBrick, TexLayer::CliffStone);

    // Steep descending stone stairway down into the sunken crypt
    world.addStairs(14, 32, 17, 34, 10, 2, SlopeDir::PlusZ, TexLayer::StoneBrick);

    // Arcane Runic Altar in the center of the sunken subterranean sanctum
    Vec3f cryptAltarPos{-16.0f, 2.0f * elevStep, 39.0f};
    world.addRunicAltar(cryptAltarPos, 2.6f, true);

    // Cages and iron storage crates in the crypt
    world.addCrateStack({cryptAltarPos.x - 3.8f, cryptAltarPos.y, cryptAltarPos.z - 2.5f}, 3);
    world.addCrateStack({cryptAltarPos.x + 3.8f, cryptAltarPos.y, cryptAltarPos.z - 2.5f}, 2);
    world.addBarrel({cryptAltarPos.x - 3.8f, cryptAltarPos.y, cryptAltarPos.z + 2.5f});
    world.addBarrel({cryptAltarPos.x + 3.8f, cryptAltarPos.y, cryptAltarPos.z + 2.5f});

    record.events.push_back({
        HistoricalEra::Obsession, 51, "raymond",
        "Excavation of the Subterranean Crypt & Experiment IX",
        "Miners secretly hollow out the limestone bedrock beneath the manor. "
        "Raymond begins binding the essence of mountain shadow-wolves into living human remains.",
        cryptAltarPos
    });

    record.notes.push_back({
        "Raymond's Ledger: The Sanguine Transmutation",
        "Master Raymond",
        {raymond.homePos.x + 2.0f, raymond.homePos.y + 0.9f, raymond.homePos.z - 1.5f},
        "Day 412: The marrow responds to the crimson distillation. The heartbeat continues long after the lung has ceased. "
        "The villagers whisper of screams echoing through the well shafts, but they are cattle blind to eternity."
    });

    // =========================================================================
    // ERA 4: THE CALAMITY & THE BREACH (Year 59)
    // =========================================================================
    // Experiment IX suffers necrotic escalation, shatters its cage, and slays Raymond.
    // The beast breaches into the village. Captain Marcus Vane builds an emergency barricade.
    // The tavern catches fire (collapsed roof, charred walls) and the bridge is smashed!
    NarrativeActor chimera{};
    chimera.id = "chimera";
    chimera.name = "Experiment IX (The Chimera)";
    chimera.title = "The Scourge of Blackhollow";
    chimera.homePos = cryptAltarPos;
    chimera.lastPos = {-16.0f, 2.0f * elevStep, 37.0f};
    chimera.survived = true;
    chimera.fate = "Prowling the subterranean crypt sanctum beneath the abandoned ruins.";
    record.actors.push_back(chimera);

    // 1. The Ruined Wayfarer Tavern (Replaces intact tavern with charred collapsed ruin)
    Vec3f tavernPos{-10.5f, 6.0f * elevStep, 1.5f};
    world.addRuinedHouse(tavernPos, {8.6f, 4.4f, 6.4f}, 0, 0.55f);

    // 2. Emergency Road Barricade at the Bridge Chokepoint
    Vec3f barricadePos{0.5f, 4.0f * elevStep, 1.8f};
    world.addBarricade(barricadePos, 4.2f, 0.15f);

    // 3. Smashed Wooden Bridge across the River
    Vec3f bridgeStart{-1.5f, 4.0f * elevStep, 1.8f};
    Vec3f bridgeEnd{10.5f, 4.0f * elevStep, 1.8f};
    world.addBrokenBridge(bridgeStart, bridgeEnd, 0.45f, 3.4f);

    // Splintered barrels and crates scattered around the barricade and tavern
    world.addCrateStack({barricadePos.x - 2.2f, barricadePos.y, barricadePos.z - 1.8f}, 2);
    world.addBarrel({barricadePos.x - 1.8f, barricadePos.y, barricadePos.z + 1.4f});
    world.addBarrel({tavernPos.x + 3.8f, tavernPos.y, tavernPos.z - 2.8f});

    record.events.push_back({
        HistoricalEra::Calamity, 59, "chimera",
        "The Containment Breach & Fall of Blackhollow",
        "Experiment IX breaks its iron bonds, tearing through the crypt and into the sleeping village. "
        "Captain Vane's militia holds the bridge barricade to allow fleeing families to cross, "
        "before fire engulfs the tavern and the river bridge collapses.",
        barricadePos
    });

    record.notes.push_back({
        "Captain Vane's Final Orders",
        "Captain Marcus Vane",
        {barricadePos.x, barricadePos.y + 0.8f, barricadePos.z},
        "To the militia: Hold the barricade! The creature cannot reach the eastern road. "
        "If the bridge gives way under its weight, let the river take us all."
    });

    // =========================================================================
    // ERA 5: PRESENT DAY — RECLAMATION & THE ADVENTURERS' ENTRY
    // =========================================================================
    // Decades of overgrowth: moss boulders, star ferns, and grass tufts crack through the stone.
    // A traveling hero party gazes upon the ruins from the cliff overlook at twilight.
    // The chimera stalks the glowing crypt below. A forgotten treasure chest rests in the manor.
    NarrativeActor party{};
    party.id = "party";
    party.name = "The Wayfarer Vanguard";
    party.title = "Investigating Adventurers";
    party.homePos = {-7.0f, 8.0f * elevStep, -21.0f};
    party.lastPos = party.homePos;
    party.survived = true;
    party.fate = "Arrived at the southern cliff overlook investigating the legend of the lost hamlet.";
    record.actors.push_back(party);

    // Overgrowth & Vegetation
    world.addMossBoulder3DGrounded(-8.0f, 6.0f, 1.2f, 0.8f, 1.1f, 0.4f);
    world.addMossBoulder3DGrounded(4.0f, -4.0f, 1.4f, 0.9f, 1.3f, 1.2f);
    world.addMossBoulder3DGrounded(-14.0f, 32.0f, 1.0f, 0.7f, 1.0f, 2.1f);

    for (int i = 0; i < 28; ++i) {
        float fx = hash1D(seed + i * 17u, 101u) * 56.0f - 28.0f;
        float fz = hash1D(seed + i * 31u, 102u) * 56.0f - 28.0f;
        float groundY = world.getTerrainHeightAt(fx, fz);
        if (groundY > 1.4f) {
            world.addGrassTuft3D({fx, groundY, fz}, 0.55f, hash1D(seed + i * 5u, 103u) * 6.28f);
            if (i % 3 == 0) {
                world.addStarFern3D({fx + 0.5f, groundY, fz + 0.5f}, 0.85f, hash1D(seed + i * 9u, 104u) * 6.28f);
            }
        }
    }

    // Lush Octopath / Lumina style overgrowth: wild grass blades, poppies, buttercups, and clover
    world.scatterMeadowVegetation(1.1f, 0.40f, seed + 999u);

    // Ancient pines scattered on the higher ridges
    world.addPineTree3DGrounded(-24.0f, 40.0f, 1.15f, 0.3f);
    world.addPineTree3DGrounded(-22.0f, 44.0f, 1.35f, 0.8f);
    world.addPineTree3DGrounded(24.0f, 36.0f, 1.20f, 1.4f);
    world.addPineTree3DGrounded(28.0f, 28.0f, 1.40f, 2.1f);
    world.addPineTree3DGrounded(20.0f, -24.0f, 1.25f, 0.5f);
    world.addPineTree3DGrounded(-24.0f, -18.0f, 1.30f, 1.8f);

    // Weathered 3D dead trees framing the ruined hamlet & calamity site
    world.addDeadTree3DGrounded(-16.0f, 14.0f, 1.20f, 0.4f);
    world.addDeadTree3DGrounded(14.0f, 16.0f, 1.15f, 1.1f);
    world.addDeadTree3DGrounded(-4.0f, 24.0f, 1.30f, 2.2f);

    // Forgotten Treasure Chest in the Alchemist's manor study
    world.addTreasureChest({raymond.homePos.x - 2.8f, raymond.homePos.y, raymond.homePos.z + 1.8f}, 0.45f);

    // Entity Spawns:
    // 1. The Adventurers standing on the southern overlook bluff
    world.spawnSpriteGrounded(-7.0f, -21.0f, 1.9f, 2.5f, TexLayer::SpriteHero, SpriteFlags::IdleBob);
    world.spawnSpriteGrounded(-9.2f, -20.2f, 1.8f, 2.4f, TexLayer::SpriteCleric, SpriteFlags::IdleBob);
    world.spawnSpriteGrounded(-4.8f, -20.4f, 1.8f, 2.4f, TexLayer::SpriteScholar, SpriteFlags::IdleBob);

    // 2. The Chimera Beast prowling near the subterranean crypt entrance
    world.spawnSpriteGrounded(cryptAltarPos.x, cryptAltarPos.z + 1.5f, 2.6f, 2.8f, TexLayer::SpriteWolf, SpriteFlags::IdleBob);

    // 3. Adventurer's warm lantern light on the overlook
    world.addPointLight(
        {-7.0f, 8.0f * elevStep + 1.8f, -20.0f},
        {1.0f, 0.72f, 0.35f},
        18.0f,
        2.5f
    );

    // 4. Lingering ember inside the ruined tavern
    world.addPointLight(
        {tavernPos.x, tavernPos.y + 0.8f, tavernPos.z},
        {1.0f, 0.38f, 0.12f},
        14.0f,
        1.6f
    );

    // Atmospheric Settings: Deep Twilight / Blue Hour with cinematic contrast
    world.setTimeOfDay(0.82f); // 19:40 dusk twilight

    // Camera Framing: Overlook perspective taking in the adventurers, broken bridge, ruined tavern, and glowing crypt
    auto& cam = world.camera();
    cam.position = {-6.5f, 24.0f, -32.5f};
    cam.target   = {-6.0f,  5.0f,   3.5f};
    cam.fovDegrees = 32.0f;
    cam.focusDistance = 35.0f;
    cam.focusRange = 22.0f;
    cam.bokehRadius = 6.5f;

    return record;
}

// =========================================================================
// MacroHistoryGrid: Deterministic Open-World Lore & POI Grid
// =========================================================================

MacroSiteInfo MacroHistoryGrid::evaluateRegion(int rx, int rz, uint32_t worldSeed) {
    MacroSiteInfo info{};
    info.rx = rx;
    info.rz = rz;

    // High quality deterministic spatial hash for region seed
    uint32_t h = static_cast<uint32_t>(rx) * 73856093u ^ static_cast<uint32_t>(rz) * 19349663u ^ worldSeed * 83492791u;
    h = (h ^ (h >> 13u)) * 1274126177u;
    h = h ^ (h >> 16u);
    info.seed = h;

    float r0 = static_cast<float>(h & 0xFFFFu) / 65536.0f;
    float r1 = static_cast<float>((h >> 16u) & 0xFFFFu) / 65536.0f;

    // 50% of macro regions are untouched natural wilderness
    if (r0 > 0.50f) {
        info.type = MacroSiteType::Wilderness;
        info.name = "Pristine Wilderness";
        info.loreHeadline = "Unbroken expanse of natural biomes, rivers, and alpine terrain.";
        return info;
    }

    // 50% contain historical sites with distinct physical mutations and lore!
    float siteTypePick = r1;
    if (siteTypePick < 0.28f) {
        info.type = MacroSiteType::CalamityRupture;
    } else if (siteTypePick < 0.52f) {
        info.type = MacroSiteType::WizardSanctum;
    } else if (siteTypePick < 0.74f) {
        info.type = MacroSiteType::SunkenRuins;
    } else if (siteTypePick < 0.88f) {
        info.type = MacroSiteType::SettlementHamlet;
    } else {
        info.type = MacroSiteType::AlpineMonastery;
    }

    // Interior placement offset within the 192m cell (36m..156m)
    uint32_t hPos = h * 1664525u + 1013904223u;
    float ox = 36.0f + (static_cast<float>(hPos & 0x7FFu) / 2048.0f) * 120.0f;
    float oz = 36.0f + (static_cast<float>((hPos >> 11u) & 0x7FFu) / 2048.0f) * 120.0f;
    info.worldX = static_cast<float>(rx) * kRegionSpan + ox;
    info.worldZ = static_cast<float>(rz) * kRegionSpan + oz;
    info.influenceRadius = 26.0f + static_cast<float>((hPos >> 22u) & 0x1Fu); // 26m - 57m

    info.yearOccurred = 35 + static_cast<int>((h ^ 0xA5A5A5A5u) % 210u);

    static const char* kMages[] = {"Archmage Vaelen", "Alchemist Orik", "Sorceress Lyra", "Sage Joshua", "Enchanter Morzan", "The Star-Weaver"};
    static const char* kHeroes[] = {"Commander Karlen", "Lady Rowan", "Sir Donald", "Captain Alyssa", "The Black Knight"};
    static const char* kHermits[] = {"Brother Aaron", "Elder Cynthia", "The Silent Monk", "Anchorite Tobias"};
    static const char* kHamlets[] = {"Millbrook Crossing", "Oakhaven Rest", "Riverford Way", "High-Hearth"};

    uint32_t nameIdx = (hPos >> 4u) % 6u;
    uint32_t nameIdx2 = (hPos >> 8u) % 5u;

    switch (info.type) {
        case MacroSiteType::CalamityRupture: {
            info.notableFigure = kMages[nameIdx];
            info.name = info.notableFigure + "'s Ruptured Crucible";
            info.loreHeadline = "A forbidden astral transmutation went catastrophic, shattering the leyline.";
            info.chronicleExcerpt = "Year " + std::to_string(info.yearOccurred) + ": " + info.notableFigure + 
                " attempted to harness an astral rift. The containment matrix ruptured, scorching the ground into black basalt and surging violet crystal spires.";
            break;
        }
        case MacroSiteType::WizardSanctum: {
            info.notableFigure = kMages[nameIdx];
            info.name = info.notableFigure + "'s High Observatory";
            info.loreHeadline = "A solitary spire constructed to chart falling stars and planar conduits.";
            info.chronicleExcerpt = "Year " + std::to_string(info.yearOccurred) + ": " + info.notableFigure + 
                " erected an astronomical tower overlooking the realm, illuminating the night sky with an unextinguishable starlight beacon.";
            break;
        }
        case MacroSiteType::SunkenRuins: {
            info.notableFigure = kHeroes[nameIdx2];
            info.name = "Citadel of " + info.notableFigure;
            info.loreHeadline = "The crumbling battlements of an ancient order that fell defending the valley.";
            info.chronicleExcerpt = "Year " + std::to_string(info.yearOccurred) + ": " + info.notableFigure + 
                "'s forces held the pass against a forgotten blight. Only weathered pillars and overgrown stone arches remain.";
            break;
        }
        case MacroSiteType::SettlementHamlet: {
            info.name = kHamlets[(hPos >> 12u) % 4u];
            info.notableFigure = "Mayor Raymond";
            info.loreHeadline = "A peaceful trading hamlet nestled along the natural waterway.";
            info.chronicleExcerpt = "Year " + std::to_string(info.yearOccurred) + 
                ": Settlers established a trading waypoint with timber cottages and stone hearths.";
            break;
        }
        case MacroSiteType::AlpineMonastery: {
            info.notableFigure = kHermits[(hPos >> 12u) % 4u];
            info.name = "The Sanctuary of " + info.notableFigure;
            info.loreHeadline = "A secluded shrine high in the rocky bluffs dedicated to contemplation.";
            info.chronicleExcerpt = "Year " + std::to_string(info.yearOccurred) + ": " + info.notableFigure + 
                " built a quiet stone chapel among the mountain ridges, welcoming weary pilgrims.";
            break;
        }
        default:
            break;
    }

    return info;
}

MacroSiteInfo MacroHistoryGrid::getNearestSite(float worldX, float worldZ, uint32_t worldSeed) {
    int rx = static_cast<int>(std::floor(worldX / kRegionSpan));
    int rz = static_cast<int>(std::floor(worldZ / kRegionSpan));

    MacroSiteInfo bestSite{};
    float bestDistSq = 1e12f;

    for (int dz = -1; dz <= 1; ++dz) {
        for (int dx = -1; dx <= 1; ++dx) {
            MacroSiteInfo candidate = evaluateRegion(rx + dx, rz + dz, worldSeed);
            if (candidate.type == MacroSiteType::Wilderness) continue;

            float ddx = worldX - candidate.worldX;
            float ddz = worldZ - candidate.worldZ;
            float distSq = ddx * ddx + ddz * ddz;
            if (distSq < bestDistSq) {
                bestDistSq = distSq;
                bestSite = candidate;
            }
        }
    }

    if (bestDistSq >= 1e11f) {
        // No POI in immediate 9 cells, return local wilderness info
        return evaluateRegion(rx, rz, worldSeed);
    }
    return bestSite;
}

} // namespace eatsrealms
