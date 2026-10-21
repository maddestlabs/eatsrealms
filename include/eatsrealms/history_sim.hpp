#ifndef EATSREALMS_HISTORY_SIM_HPP
#define EATSREALMS_HISTORY_SIM_HPP

#include "eatsrealms/realm_world.hpp"
#include <string>
#include <vector>
#include <cstdint>

namespace eatsrealms {

/**
 * Historical Eras modeled during the narrative simulation of a settlement.
 */
enum class HistoricalEra {
    Founding,      // Year 1-20: Pioneers settle, river mill, stone weir, cottages, desire paths
    Prosperity,    // Year 21-45: Expansion, arched bridge, cobblestone plaza, tavern, alchemist buys hill manor
    Obsession,     // Year 46-58: Subterranean crypt excavated, rune circle, beast containment cages
    Calamity,      // Year 59: Chimera breaks out, fires, collapsed roofs, road barricades, defenders fall
    Reclamation    // Present: Overgrowth, weeds, eerie twilight, hero party arrives to uncover the chronicle
};

struct NarrativeActor {
    std::string id;
    std::string name;
    std::string title;
    Vec3f homePos;
    Vec3f lastPos;
    bool survived{false};
    std::string fate;
};

struct ChronicleEvent {
    HistoricalEra era{HistoricalEra::Founding};
    int year{0};
    std::string actorId;
    std::string headline;
    std::string details;
    Vec3f location;
};

struct ChronicleNote {
    std::string title;
    std::string author;
    Vec3f location;
    std::string excerpt;
};

struct ChronicleRecord {
    uint32_t seed{999u};
    std::string settlementName{"Blackhollow Vale"};
    std::vector<NarrativeActor> actors;
    std::vector<ChronicleEvent> events;
    std::vector<ChronicleNote> notes;

    [[nodiscard]] std::string generateSummaryMarkdown() const;
};

/**
 * Macro-Region Points of Interest (POIs) and Historical Sites in Open World.
 * Partitioned into 192m x 192m (4x4 chunks) macro-cells generated from world seed.
 */
enum class MacroSiteType : uint8_t {
    Wilderness = 0,
    WizardSanctum = 1,      // Arcane observatory, rune circles, astronomical spire
    CalamityRupture = 2,    // Ruptured leyline / failed transmutation lab (crater, crystals, ash, anomalies)
    SunkenRuins = 3,        // Overgrown stone keeps, fallen pillars, flooded crypts
    AlpineMonastery = 4,    // Mountain redoubt, stone chapel, hermit ascetic
    BanditRedoubt = 5,      // Fortified outpost, palisades, campfires, sentries
    SettlementHamlet = 6    // Riverside cottages, watermill, villagers, trade post
};

struct MacroSiteInfo {
    MacroSiteType type{MacroSiteType::Wilderness};
    int rx{0};
    int rz{0};
    float worldX{0.0f};
    float worldZ{0.0f};
    float influenceRadius{32.0f};
    uint32_t seed{0};
    std::string name;
    std::string notableFigure;
    std::string loreHeadline;
    std::string chronicleExcerpt;
    int yearOccurred{0};
};

class MacroHistoryGrid {
public:
    static constexpr float kRegionSpan = 192.0f; // 4 chunks x 48m = 192m

    static MacroSiteInfo evaluateRegion(int rx, int rz, uint32_t worldSeed);
    static MacroSiteInfo getNearestSite(float worldX, float worldZ, uint32_t worldSeed);
};

/**
 * HistorySimulator:
 * Simulates settlement history through sequential actor decisions and world mutations
 * ("The Archaeology of Intent"):
 *
 *  1. Foundation: The Miller harnesses the river; settlers build cottages and farm terraces.
 *  2. Prosperity: Crossroads tavern, market plaza, stone bridge, and the Scholar's Manor.
 *  3. Obsession: The Scholar carves a subterranean cellar/crypt with cages and runic altar.
 *  4. Calamity: Containment breach! Beast rampages, makeshift barricades, tavern fire, collapsed roofs.
 *  5. Reclamation: Decades of overgrowth, eerie twilight lighting, scattered journals, and player entry.
 */
class HistorySimulator {
public:
    static ChronicleRecord simulate(RealmWorld& world, uint32_t seed = 999u);
};

} // namespace eatsrealms

#endif // EATSREALMS_HISTORY_SIM_HPP
