#include "eatsrealms/script_bindings.hpp"
#include "eatsrealms/history_sim.hpp"
#include "eatsbits/eatscript/stdlib.hpp"
#include <fstream>
#include <sstream>

namespace eatsrealms {

using eatsbits::eatscript::Value;
using eatsbits::eatscript::resolveArg;

RealmScriptEngine::RealmScriptEngine(RealmWorld& world)
    : world_(world) {
    eatsbits::eatscript::registerStandardLibrary(evaluator_, nullptr);
    registerRealmBindings();
}

void RealmScriptEngine::registerRealmBindings() {
    std::map<std::string, Value> texDict;
    texDict["GrassMeadow"]      = Value(static_cast<int>(TexLayer::GrassMeadow));
    texDict["GrassHighland"]    = Value(static_cast<int>(TexLayer::GrassHighland));
    texDict["CliffStone"]       = Value(static_cast<int>(TexLayer::CliffStone));
    texDict["Cobblestone"]      = Value(static_cast<int>(TexLayer::Cobblestone));
    texDict["DirtPath"]         = Value(static_cast<int>(TexLayer::DirtPath));
    texDict["WoodPlanks"]       = Value(static_cast<int>(TexLayer::WoodPlanks));
    texDict["TimberWall"]       = Value(static_cast<int>(TexLayer::TimberWall));
    texDict["StuccoPlaster"]    = Value(static_cast<int>(TexLayer::StuccoPlaster));
    texDict["TerracottaRoof"]   = Value(static_cast<int>(TexLayer::TerracottaRoof));
    texDict["SlateRoof"]        = Value(static_cast<int>(TexLayer::SlateRoof));
    texDict["StoneBrick"]       = Value(static_cast<int>(TexLayer::StoneBrick));
    texDict["WindowWarmGlow"]   = Value(static_cast<int>(TexLayer::WindowWarmGlow));
    texDict["WaterSurface"]     = Value(static_cast<int>(TexLayer::WaterSurface));
    texDict["SandShore"]        = Value(static_cast<int>(TexLayer::SandShore));
    texDict["SpriteHero"]       = Value(static_cast<int>(TexLayer::SpriteHero));
    texDict["SpriteVillager"]   = Value(static_cast<int>(TexLayer::SpriteVillager));
    texDict["SpriteMerchant"]   = Value(static_cast<int>(TexLayer::SpriteMerchant));
    texDict["SpriteGuard"]      = Value(static_cast<int>(TexLayer::SpriteGuard));
    texDict["SpriteSlime"]      = Value(static_cast<int>(TexLayer::SpriteSlime));
    texDict["SpriteWolf"]       = Value(static_cast<int>(TexLayer::SpriteWolf));
    texDict["SpritePineTree"]   = Value(static_cast<int>(TexLayer::SpritePineTree));
    texDict["PineFoliage"]      = Value(static_cast<int>(TexLayer::PineFoliage));
    texDict["SpriteOakTree"]    = Value(static_cast<int>(TexLayer::SpriteOakTree));
    texDict["SpriteWildflower"] = Value(static_cast<int>(TexLayer::SpriteWildflower));
    texDict["SpriteTorchFlame"] = Value(static_cast<int>(TexLayer::SpriteTorchFlame));

    // Coastal & Tropical Layers
    texDict["PalmFronds"]       = Value(static_cast<int>(TexLayer::PalmFronds));
    texDict["PalmBark"]         = Value(static_cast<int>(TexLayer::PalmBark));
    texDict["CoastalCliff"]     = Value(static_cast<int>(TexLayer::CoastalCliff));
    texDict["CoastalScree"]     = Value(static_cast<int>(TexLayer::CoastalScree));
    texDict["SpritePalmTree"]   = Value(static_cast<int>(TexLayer::SpritePalmTree));
    texDict["SpriteCleric"]     = Value(static_cast<int>(TexLayer::SpriteCleric));
    texDict["SpriteScholar"]    = Value(static_cast<int>(TexLayer::SpriteScholar));
    texDict["SpriteDancer"]     = Value(static_cast<int>(TexLayer::SpriteDancer));

    // Lush Meadow Flora & Grass
    texDict["SpriteGrassTuft"]        = Value(static_cast<int>(TexLayer::SpriteGrassTuft));
    texDict["SpriteTallGrass"]        = Value(static_cast<int>(TexLayer::SpriteTallGrass));
    texDict["SpriteWildflowerRed"]    = Value(static_cast<int>(TexLayer::SpriteWildflowerRed));
    texDict["SpriteWildflowerBlue"]   = Value(static_cast<int>(TexLayer::SpriteWildflowerBlue));
    texDict["SpriteCloverPatch"]      = Value(static_cast<int>(TexLayer::SpriteCloverPatch));

    // Biome Terrain Additions
    texDict["SnowFrost"]              = Value(static_cast<int>(TexLayer::SnowFrost));
    texDict["DesertDune"]             = Value(static_cast<int>(TexLayer::DesertDune));
    texDict["SwampBog"]               = Value(static_cast<int>(TexLayer::SwampBog));
    texDict["ScorchedAsh"]            = Value(static_cast<int>(TexLayer::ScorchedAsh));

    // Biome Flora Billboard Sprites
    texDict["SpriteCactus"]           = Value(static_cast<int>(TexLayer::SpriteCactus));
    texDict["SpriteDeadShrub"]        = Value(static_cast<int>(TexLayer::SpriteDeadShrub));
    texDict["SpriteSnowPine"]         = Value(static_cast<int>(TexLayer::SpriteSnowPine));
    texDict["OakFoliage"]             = Value(static_cast<int>(TexLayer::OakFoliage));
    texDict["SnowPineFoliage"]        = Value(static_cast<int>(TexLayer::SnowPineFoliage));
    texDict["AutumnFoliage"]          = Value(static_cast<int>(TexLayer::AutumnFoliage));

    evaluator_.setVariable("Tex", Value(texDict));

    std::map<std::string, Value> realmObj;
    realmObj["name"] = Value("EatsrealmsHD2DCore");
    evaluator_.setVariable("realm", Value(realmObj));

    evaluator_.registerMemberFunction("realm", "clear", [this](const auto&, const auto&) -> Value {
        world_.clear();
        return Value(true);
    });

    evaluator_.registerMemberFunction("realm", "build_starfall_vale", [this](const auto&, const auto&) -> Value {
        world_.buildStarfallValeRealm();
        return Value(true);
    });

    evaluator_.registerMemberFunction("realm", "generate", [this](const auto& args, const auto& kwargs) -> Value {
        uint32_t seed = static_cast<uint32_t>(resolveArg(args, kwargs, 0, "seed", Value(1337)).asInt());
        world_.generateProceduralRealm(seed);
        return Value(true);
    });

    evaluator_.registerMemberFunction("realm", "build_woodland_trail", [this](const auto& args, const auto& kwargs) -> Value {
        uint32_t seed = static_cast<uint32_t>(resolveArg(args, kwargs, 0, "seed", Value(42)).asInt());
        world_.buildWoodlandTrailRealm(seed);
        return Value(true);
    });

    evaluator_.registerMemberFunction("realm", "build_coastal_cove", [this](const auto& args, const auto& kwargs) -> Value {
        uint32_t seed = static_cast<uint32_t>(resolveArg(args, kwargs, 0, "seed", Value(777)).asInt());
        world_.buildCoastalCoveRealm(seed);
        return Value(true);
    });

    evaluator_.registerMemberFunction("realm", "build_chronicle_hamlet", [this](const auto& args, const auto& kwargs) -> Value {
        uint32_t seed = static_cast<uint32_t>(resolveArg(args, kwargs, 0, "seed", Value(999)).asInt());
        world_.buildChronicleHamletRealm(seed);
        return Value(true);
    });

    evaluator_.registerMemberFunction("realm", "build_highland_bridge", [this](const auto& args, const auto& kwargs) -> Value {
        uint32_t seed = static_cast<uint32_t>(resolveArg(args, kwargs, 0, "seed", Value(42)).asInt());
        world_.buildHighlandBridgeRealm(seed);
        return Value(true);
    });

    evaluator_.registerMemberFunction("realm", "add_slope", [this](const auto& args, const auto& kwargs) -> Value {
        int gx0 = resolveArg(args, kwargs, 0, "x0", Value(0)).asInt();
        int gz0 = resolveArg(args, kwargs, 1, "z0", Value(0)).asInt();
        int gx1 = resolveArg(args, kwargs, 2, "x1", Value(10)).asInt();
        int gz1 = resolveArg(args, kwargs, 3, "z1", Value(10)).asInt();
        int16_t startElev = static_cast<int16_t>(resolveArg(args, kwargs, 4, "start_elev", Value(4)).asInt());
        int16_t endElev = static_cast<int16_t>(resolveArg(args, kwargs, 5, "end_elev", Value(6)).asInt());
        int dirInt = resolveArg(args, kwargs, 6, "dir", Value(3)).asInt();
        SlopeDir dir = static_cast<SlopeDir>(dirInt);
        uint16_t top = static_cast<uint16_t>(resolveArg(args, kwargs, 7, "top", Value(static_cast<int>(TexLayer::Cobblestone))).asInt());
        uint16_t cliff = static_cast<uint16_t>(resolveArg(args, kwargs, 8, "cliff", Value(static_cast<int>(TexLayer::CliffStone))).asInt());
        bool isStairs = resolveArg(args, kwargs, 9, "is_stairs", Value(false)).asBoolean();
        world_.addSlope(gx0, gz0, gx1, gz1, startElev, endElev, dir, top, cliff, isStairs);
        return Value(true);
    });

    evaluator_.registerMemberFunction("realm", "add_suspension_bridge", [this](const auto& args, const auto& kwargs) -> Value {
        float x0 = resolveArg(args, kwargs, 0, "x0", Value(-6.2)).asFloat();
        float y0 = resolveArg(args, kwargs, 1, "y0", Value(2.45)).asFloat();
        float z0 = resolveArg(args, kwargs, 2, "z0", Value(-4.5)).asFloat();
        float x1 = resolveArg(args, kwargs, 3, "x1", Value(6.8)).asFloat();
        float y1 = resolveArg(args, kwargs, 4, "y1", Value(2.45)).asFloat();
        float z1 = resolveArg(args, kwargs, 5, "z1", Value(-4.5)).asFloat();
        float w  = resolveArg(args, kwargs, 6, "width", Value(3.4)).asFloat();
        float mh = resolveArg(args, kwargs, 7, "mast_h", Value(4.8)).asFloat();
        world_.addSuspensionBridge({x0, y0, z0}, {x1, y1, z1}, w, mh);
        return Value(true);
    });

    evaluator_.registerMemberFunction("realm", "add_roadside_beacon", [this](const auto& args, const auto& kwargs) -> Value {
        float x = resolveArg(args, kwargs, 0, "x", Value(0.0)).asFloat();
        float y = resolveArg(args, kwargs, 1, "y", Value(2.4)).asFloat();
        float z = resolveArg(args, kwargs, 2, "z", Value(0.0)).asFloat();
        float r = resolveArg(args, kwargs, 3, "r", Value(0.20)).asFloat();
        float g = resolveArg(args, kwargs, 4, "g", Value(1.95)).asFloat();
        float b = resolveArg(args, kwargs, 5, "b", Value(0.55)).asFloat();
        world_.addRoadsideBeacon({x, y, z}, {r, g, b});
        return Value(true);
    });

    evaluator_.registerMemberFunction("realm", "fill_plateau", [this](const auto& args, const auto& kwargs) -> Value {
        int gx0 = resolveArg(args, kwargs, 0, "x0", Value(0)).asInt();
        int gz0 = resolveArg(args, kwargs, 1, "z0", Value(0)).asInt();
        int gx1 = resolveArg(args, kwargs, 2, "x1", Value(10)).asInt();
        int gz1 = resolveArg(args, kwargs, 3, "z1", Value(10)).asInt();
        int16_t elev = static_cast<int16_t>(resolveArg(args, kwargs, 4, "elev", Value(4)).asInt());
        uint16_t top = static_cast<uint16_t>(resolveArg(args, kwargs, 5, "top", Value(0)).asInt());
        uint16_t cliff = static_cast<uint16_t>(resolveArg(args, kwargs, 6, "cliff", Value(static_cast<int>(TexLayer::CliffStone))).asInt());
        world_.fillPlateau(gx0, gz0, gx1, gz1, elev, top, cliff);
        return Value(true);
    });

    evaluator_.registerMemberFunction("realm", "fill_river", [this](const auto& args, const auto& kwargs) -> Value {
        int gx0 = resolveArg(args, kwargs, 0, "x0", Value(26)).asInt();
        int gz0 = resolveArg(args, kwargs, 1, "z0", Value(0)).asInt();
        int gx1 = resolveArg(args, kwargs, 2, "x1", Value(32)).asInt();
        int gz1 = resolveArg(args, kwargs, 3, "z1", Value(63)).asInt();
        int16_t bedElev = static_cast<int16_t>(resolveArg(args, kwargs, 4, "bed_elev", Value(1)).asInt());
        float waterY = resolveArg(args, kwargs, 5, "water_y", Value(1.35)).asFloat();
        world_.fillRiver(gx0, gz0, gx1, gz1, bedElev, waterY);
        return Value(true);
    });

    evaluator_.registerMemberFunction("realm", "add_house", [this](const auto& args, const auto& kwargs) -> Value {
        float x = resolveArg(args, kwargs, 0, "x", Value(0.0)).asFloat();
        float y = resolveArg(args, kwargs, 1, "y", Value(3.6)).asFloat();
        float z = resolveArg(args, kwargs, 2, "z", Value(0.0)).asFloat();
        float w = resolveArg(args, kwargs, 3, "w", Value(6.5)).asFloat();
        float h = resolveArg(args, kwargs, 4, "h", Value(4.0)).asFloat();
        float d = resolveArg(args, kwargs, 5, "d", Value(5.5)).asFloat();
        int axis = resolveArg(args, kwargs, 6, "roof_axis", Value(0)).asInt();
        uint16_t wall = static_cast<uint16_t>(resolveArg(args, kwargs, 7, "wall", Value(static_cast<int>(TexLayer::TimberWall))).asInt());
        uint16_t roof = static_cast<uint16_t>(resolveArg(args, kwargs, 8, "roof", Value(static_cast<int>(TexLayer::TerracottaRoof))).asInt());
        world_.addGabledHouse({x, y, z}, {w, h, d}, axis, wall, roof, true, true);
        return Value(true);
    });

    evaluator_.registerMemberFunction("realm", "add_bridge", [this](const auto& args, const auto& kwargs) -> Value {
        float x0 = resolveArg(args, kwargs, 0, "x0", Value(-9.5)).asFloat();
        float y0 = resolveArg(args, kwargs, 1, "y0", Value(2.4)).asFloat();
        float z0 = resolveArg(args, kwargs, 2, "z0", Value(-10.5)).asFloat();
        float x1 = resolveArg(args, kwargs, 3, "x1", Value(3.5)).asFloat();
        float y1 = resolveArg(args, kwargs, 4, "y1", Value(2.4)).asFloat();
        float z1 = resolveArg(args, kwargs, 5, "z1", Value(-10.5)).asFloat();
        float w  = resolveArg(args, kwargs, 6, "width", Value(3.6)).asFloat();
        world_.addWoodenBridge({x0, y0, z0}, {x1, y1, z1}, w);
        return Value(true);
    });

    evaluator_.registerMemberFunction("realm", "add_lantern", [this](const auto& args, const auto& kwargs) -> Value {
        float x = resolveArg(args, kwargs, 0, "x", Value(0.0)).asFloat();
        float y = resolveArg(args, kwargs, 1, "y", Value(2.4)).asFloat();
        float z = resolveArg(args, kwargs, 2, "z", Value(0.0)).asFloat();
        world_.addStreetLantern({x, y, z});
        return Value(true);
    });

    evaluator_.registerMemberFunction("realm", "add_crates", [this](const auto& args, const auto& kwargs) -> Value {
        float x = resolveArg(args, kwargs, 0, "x", Value(0.0)).asFloat();
        float y = resolveArg(args, kwargs, 1, "y", Value(2.4)).asFloat();
        float z = resolveArg(args, kwargs, 2, "z", Value(0.0)).asFloat();
        int count = resolveArg(args, kwargs, 3, "count", Value(3)).asInt();
        world_.addCrateStack({x, y, z}, count);
        return Value(true);
    });

    evaluator_.registerMemberFunction("realm", "add_barrel", [this](const auto& args, const auto& kwargs) -> Value {
        float x = resolveArg(args, kwargs, 0, "x", Value(0.0)).asFloat();
        float y = resolveArg(args, kwargs, 1, "y", Value(2.4)).asFloat();
        float z = resolveArg(args, kwargs, 2, "z", Value(0.0)).asFloat();
        world_.addBarrel({x, y, z});
        return Value(true);
    });

    evaluator_.registerMemberFunction("realm", "add_flowerbox", [this](const auto& args, const auto& kwargs) -> Value {
        float x = resolveArg(args, kwargs, 0, "x", Value(0.0)).asFloat();
        float y = resolveArg(args, kwargs, 1, "y", Value(2.4)).asFloat();
        float z = resolveArg(args, kwargs, 2, "z", Value(0.0)).asFloat();
        float length = resolveArg(args, kwargs, 3, "length", Value(1.6)).asFloat();
        int axis = resolveArg(args, kwargs, 4, "axis", Value(0)).asInt();
        world_.addFlowerBox({x, y, z}, length, axis);
        return Value(true);
    });

    evaluator_.registerMemberFunction("realm", "add_market_stall", [this](const auto& args, const auto& kwargs) -> Value {
        float x = resolveArg(args, kwargs, 0, "x", Value(0.0)).asFloat();
        float y = resolveArg(args, kwargs, 1, "y", Value(2.4)).asFloat();
        float z = resolveArg(args, kwargs, 2, "z", Value(0.0)).asFloat();
        float width = resolveArg(args, kwargs, 3, "width", Value(3.8)).asFloat();
        float depth = resolveArg(args, kwargs, 4, "depth", Value(2.6)).asFloat();
        uint16_t canopy = static_cast<uint16_t>(resolveArg(args, kwargs, 5, "canopy", Value(static_cast<int>(TexLayer::TerracottaRoof))).asInt());
        world_.addMarketStall({x, y, z}, width, depth, canopy);
        return Value(true);
    });

    evaluator_.registerMemberFunction("realm", "add_pine_tree", [this](const auto& args, const auto& kwargs) -> Value {
        float x = resolveArg(args, kwargs, 0, "x", Value(0.0)).asFloat();
        float y = resolveArg(args, kwargs, 1, "y", Value(2.4)).asFloat();
        float z = resolveArg(args, kwargs, 2, "z", Value(0.0)).asFloat();
        float scale = resolveArg(args, kwargs, 3, "scale", Value(1.0)).asFloat();
        float rot = resolveArg(args, kwargs, 4, "rot", Value(0.0)).asFloat();
        world_.addPineTree3D({x, y, z}, scale, rot);
        return Value(true);
    });

    evaluator_.registerMemberFunction("realm", "add_pine_tree_grounded", [this](const auto& args, const auto& kwargs) -> Value {
        float x = resolveArg(args, kwargs, 0, "x", Value(0.0)).asFloat();
        float z = resolveArg(args, kwargs, 1, "z", Value(0.0)).asFloat();
        float scale = resolveArg(args, kwargs, 2, "scale", Value(1.0)).asFloat();
        float rot = resolveArg(args, kwargs, 3, "rot", Value(0.0)).asFloat();
        world_.addPineTree3DGrounded(x, z, scale, rot);
        return Value(true);
    });

    evaluator_.registerMemberFunction("realm", "add_oak_tree", [this](const auto& args, const auto& kwargs) -> Value {
        float x = resolveArg(args, kwargs, 0, "x", Value(0.0)).asFloat();
        float y = resolveArg(args, kwargs, 1, "y", Value(2.4)).asFloat();
        float z = resolveArg(args, kwargs, 2, "z", Value(0.0)).asFloat();
        float scale = resolveArg(args, kwargs, 3, "scale", Value(1.0)).asFloat();
        float rot = resolveArg(args, kwargs, 4, "rot", Value(0.0)).asFloat();
        world_.addOakTree3D({x, y, z}, scale, rot);
        return Value(true);
    });

    evaluator_.registerMemberFunction("realm", "add_oak_tree_grounded", [this](const auto& args, const auto& kwargs) -> Value {
        float x = resolveArg(args, kwargs, 0, "x", Value(0.0)).asFloat();
        float z = resolveArg(args, kwargs, 1, "z", Value(0.0)).asFloat();
        float scale = resolveArg(args, kwargs, 2, "scale", Value(1.0)).asFloat();
        float rot = resolveArg(args, kwargs, 3, "rot", Value(0.0)).asFloat();
        world_.addOakTree3DGrounded(x, z, scale, rot);
        return Value(true);
    });

    evaluator_.registerMemberFunction("realm", "add_snow_pine_tree", [this](const auto& args, const auto& kwargs) -> Value {
        float x = resolveArg(args, kwargs, 0, "x", Value(0.0)).asFloat();
        float y = resolveArg(args, kwargs, 1, "y", Value(2.4)).asFloat();
        float z = resolveArg(args, kwargs, 2, "z", Value(0.0)).asFloat();
        float scale = resolveArg(args, kwargs, 3, "scale", Value(1.0)).asFloat();
        float rot = resolveArg(args, kwargs, 4, "rot", Value(0.0)).asFloat();
        world_.addSnowPineTree3D({x, y, z}, scale, rot);
        return Value(true);
    });

    evaluator_.registerMemberFunction("realm", "add_snow_pine_tree_grounded", [this](const auto& args, const auto& kwargs) -> Value {
        float x = resolveArg(args, kwargs, 0, "x", Value(0.0)).asFloat();
        float z = resolveArg(args, kwargs, 1, "z", Value(0.0)).asFloat();
        float scale = resolveArg(args, kwargs, 2, "scale", Value(1.0)).asFloat();
        float rot = resolveArg(args, kwargs, 3, "rot", Value(0.0)).asFloat();
        world_.addSnowPineTree3DGrounded(x, z, scale, rot);
        return Value(true);
    });

    evaluator_.registerMemberFunction("realm", "add_autumn_tree", [this](const auto& args, const auto& kwargs) -> Value {
        float x = resolveArg(args, kwargs, 0, "x", Value(0.0)).asFloat();
        float y = resolveArg(args, kwargs, 1, "y", Value(2.4)).asFloat();
        float z = resolveArg(args, kwargs, 2, "z", Value(0.0)).asFloat();
        float scale = resolveArg(args, kwargs, 3, "scale", Value(1.0)).asFloat();
        float rot = resolveArg(args, kwargs, 4, "rot", Value(0.0)).asFloat();
        world_.addAutumnTree3D({x, y, z}, scale, rot);
        return Value(true);
    });

    evaluator_.registerMemberFunction("realm", "add_autumn_tree_grounded", [this](const auto& args, const auto& kwargs) -> Value {
        float x = resolveArg(args, kwargs, 0, "x", Value(0.0)).asFloat();
        float z = resolveArg(args, kwargs, 1, "z", Value(0.0)).asFloat();
        float scale = resolveArg(args, kwargs, 2, "scale", Value(1.0)).asFloat();
        float rot = resolveArg(args, kwargs, 3, "rot", Value(0.0)).asFloat();
        world_.addAutumnTree3DGrounded(x, z, scale, rot);
        return Value(true);
    });

    evaluator_.registerMemberFunction("realm", "add_dead_tree", [this](const auto& args, const auto& kwargs) -> Value {
        float x = resolveArg(args, kwargs, 0, "x", Value(0.0)).asFloat();
        float y = resolveArg(args, kwargs, 1, "y", Value(2.4)).asFloat();
        float z = resolveArg(args, kwargs, 2, "z", Value(0.0)).asFloat();
        float scale = resolveArg(args, kwargs, 3, "scale", Value(1.0)).asFloat();
        float rot = resolveArg(args, kwargs, 4, "rot", Value(0.0)).asFloat();
        world_.addDeadTree3D({x, y, z}, scale, rot);
        return Value(true);
    });

    evaluator_.registerMemberFunction("realm", "add_dead_tree_grounded", [this](const auto& args, const auto& kwargs) -> Value {
        float x = resolveArg(args, kwargs, 0, "x", Value(0.0)).asFloat();
        float z = resolveArg(args, kwargs, 1, "z", Value(0.0)).asFloat();
        float scale = resolveArg(args, kwargs, 2, "scale", Value(1.0)).asFloat();
        float rot = resolveArg(args, kwargs, 3, "rot", Value(0.0)).asFloat();
        world_.addDeadTree3DGrounded(x, z, scale, rot);
        return Value(true);
    });

    evaluator_.registerMemberFunction("realm", "add_moss_boulder", [this](const auto& args, const auto& kwargs) -> Value {
        float x = resolveArg(args, kwargs, 0, "x", Value(0.0)).asFloat();
        float y = resolveArg(args, kwargs, 1, "y", Value(2.4)).asFloat();
        float z = resolveArg(args, kwargs, 2, "z", Value(0.0)).asFloat();
        float rx = resolveArg(args, kwargs, 3, "rx", Value(1.0)).asFloat();
        float ry = resolveArg(args, kwargs, 4, "ry", Value(0.65)).asFloat();
        float rz = resolveArg(args, kwargs, 5, "rz", Value(1.0)).asFloat();
        float rot = resolveArg(args, kwargs, 6, "rot", Value(0.0)).asFloat();
        world_.addMossBoulder3D({x, y, z}, rx, ry, rz, rot);
        return Value(true);
    });

    evaluator_.registerMemberFunction("realm", "add_moss_boulder_grounded", [this](const auto& args, const auto& kwargs) -> Value {
        float x = resolveArg(args, kwargs, 0, "x", Value(0.0)).asFloat();
        float z = resolveArg(args, kwargs, 1, "z", Value(0.0)).asFloat();
        float rx = resolveArg(args, kwargs, 2, "rx", Value(1.0)).asFloat();
        float ry = resolveArg(args, kwargs, 3, "ry", Value(0.65)).asFloat();
        float rz = resolveArg(args, kwargs, 4, "rz", Value(1.0)).asFloat();
        float rot = resolveArg(args, kwargs, 5, "rot", Value(0.0)).asFloat();
        world_.addMossBoulder3DGrounded(x, z, rx, ry, rz, rot);
        return Value(true);
    });

    evaluator_.registerMemberFunction("realm", "add_star_fern", [this](const auto& args, const auto& kwargs) -> Value {
        float x = resolveArg(args, kwargs, 0, "x", Value(0.0)).asFloat();
        float y = resolveArg(args, kwargs, 1, "y", Value(2.4)).asFloat();
        float z = resolveArg(args, kwargs, 2, "z", Value(0.0)).asFloat();
        float scale = resolveArg(args, kwargs, 3, "scale", Value(1.0)).asFloat();
        float rot = resolveArg(args, kwargs, 4, "rot", Value(0.0)).asFloat();
        world_.addStarFern3D({x, y, z}, scale, rot);
        return Value(true);
    });

    evaluator_.registerMemberFunction("realm", "add_star_fern_grounded", [this](const auto& args, const auto& kwargs) -> Value {
        float x = resolveArg(args, kwargs, 0, "x", Value(0.0)).asFloat();
        float z = resolveArg(args, kwargs, 1, "z", Value(0.0)).asFloat();
        float scale = resolveArg(args, kwargs, 2, "scale", Value(1.0)).asFloat();
        float rot = resolveArg(args, kwargs, 3, "rot", Value(0.0)).asFloat();
        world_.addStarFern3DGrounded(x, z, scale, rot);
        return Value(true);
    });

    evaluator_.registerMemberFunction("realm", "add_rustic_fence", [this](const auto& args, const auto& kwargs) -> Value {
        float x0 = resolveArg(args, kwargs, 0, "x0", Value(0.0)).asFloat();
        float z0 = resolveArg(args, kwargs, 1, "z0", Value(0.0)).asFloat();
        float x1 = resolveArg(args, kwargs, 2, "x1", Value(0.0)).asFloat();
        float z1 = resolveArg(args, kwargs, 3, "z1", Value(5.0)).asFloat();
        int posts = resolveArg(args, kwargs, 4, "posts", Value(3)).asInt();
        world_.addRusticFenceGrounded(x0, z0, x1, z1, posts);
        return Value(true);
    });

    evaluator_.registerMemberFunction("realm", "add_river_pebbles", [this](const auto& args, const auto& kwargs) -> Value {
        float x = resolveArg(args, kwargs, 0, "x", Value(0.0)).asFloat();
        float z = resolveArg(args, kwargs, 1, "z", Value(0.0)).asFloat();
        float radius = resolveArg(args, kwargs, 2, "radius", Value(0.6)).asFloat();
        int count = resolveArg(args, kwargs, 3, "count", Value(5)).asInt();
        uint32_t seed = static_cast<uint32_t>(resolveArg(args, kwargs, 4, "seed", Value(0)).asInt());
        world_.addRiverPebbleClusterGrounded(x, z, radius, count, seed);
        return Value(true);
    });

    evaluator_.registerMemberFunction("realm", "add_grass_tuft", [this](const auto& args, const auto& kwargs) -> Value {
        float x = resolveArg(args, kwargs, 0, "x", Value(0.0)).asFloat();
        float z = resolveArg(args, kwargs, 1, "z", Value(0.0)).asFloat();
        float scale = resolveArg(args, kwargs, 2, "scale", Value(0.85)).asFloat();
        float rot = resolveArg(args, kwargs, 3, "rot", Value(0.0)).asFloat();
        world_.addGrassTuft3DGrounded(x, z, scale, rot);
        return Value(true);
    });

    evaluator_.registerMemberFunction("realm", "add_tall_grass", [this](const auto& args, const auto& kwargs) -> Value {
        float x = resolveArg(args, kwargs, 0, "x", Value(0.0)).asFloat();
        float z = resolveArg(args, kwargs, 1, "z", Value(0.0)).asFloat();
        float scale = resolveArg(args, kwargs, 2, "scale", Value(1.0)).asFloat();
        float phase = resolveArg(args, kwargs, 3, "phase", Value(0.0)).asFloat();
        world_.addTallGrass3DGrounded(x, z, scale, phase);
        return Value(true);
    });

    evaluator_.registerMemberFunction("realm", "add_wildflower", [this](const auto& args, const auto& kwargs) -> Value {
        float x = resolveArg(args, kwargs, 0, "x", Value(0.0)).asFloat();
        float z = resolveArg(args, kwargs, 1, "z", Value(0.0)).asFloat();
        int layer = resolveArg(args, kwargs, 2, "layer", Value(static_cast<int>(TexLayer::SpriteWildflowerRed))).asInt();
        float scale = resolveArg(args, kwargs, 3, "scale", Value(1.0)).asFloat();
        float phase = resolveArg(args, kwargs, 4, "phase", Value(0.0)).asFloat();
        world_.addWildflower3DGrounded(x, z, static_cast<uint16_t>(layer), scale, phase);
        return Value(true);
    });

    evaluator_.registerMemberFunction("realm", "add_clover_patch", [this](const auto& args, const auto& kwargs) -> Value {
        float x = resolveArg(args, kwargs, 0, "x", Value(0.0)).asFloat();
        float z = resolveArg(args, kwargs, 1, "z", Value(0.0)).asFloat();
        float scale = resolveArg(args, kwargs, 2, "scale", Value(1.0)).asFloat();
        float phase = resolveArg(args, kwargs, 3, "phase", Value(0.0)).asFloat();
        world_.addCloverPatch3DGrounded(x, z, scale, phase);
        return Value(true);
    });

    evaluator_.registerMemberFunction("realm", "scatter_meadow_vegetation", [this](const auto& args, const auto& kwargs) -> Value {
        float density = resolveArg(args, kwargs, 0, "density", Value(1.0)).asFloat();
        float flowerRatio = resolveArg(args, kwargs, 1, "flower_ratio", Value(0.32)).asFloat();
        uint32_t seed = static_cast<uint32_t>(resolveArg(args, kwargs, 2, "seed", Value(42)).asInt());
        world_.scatterMeadowVegetation(density, flowerRatio, seed);
        return Value(true);
    });

    evaluator_.registerMemberFunction("realm", "add_palm_tree", [this](const auto& args, const auto& kwargs) -> Value {
        float x = resolveArg(args, kwargs, 0, "x", Value(0.0)).asFloat();
        float y = resolveArg(args, kwargs, 1, "y", Value(3.6)).asFloat();
        float z = resolveArg(args, kwargs, 2, "z", Value(0.0)).asFloat();
        float scale = resolveArg(args, kwargs, 3, "scale", Value(1.0)).asFloat();
        float rot = resolveArg(args, kwargs, 4, "rot", Value(0.0)).asFloat();
        bool withPlanter = resolveArg(args, kwargs, 5, "with_planter", Value(false)).asBoolean();
        world_.addPalmTree3D({x, y, z}, scale, rot, withPlanter);
        return Value(true);
    });

    evaluator_.registerMemberFunction("realm", "add_palm_tree_grounded", [this](const auto& args, const auto& kwargs) -> Value {
        float x = resolveArg(args, kwargs, 0, "x", Value(0.0)).asFloat();
        float z = resolveArg(args, kwargs, 1, "z", Value(0.0)).asFloat();
        float scale = resolveArg(args, kwargs, 2, "scale", Value(1.0)).asFloat();
        float rot = resolveArg(args, kwargs, 3, "rot", Value(0.0)).asFloat();
        bool withPlanter = resolveArg(args, kwargs, 4, "with_planter", Value(false)).asBoolean();
        world_.addPalmTree3DGrounded(x, z, scale, rot, withPlanter);
        return Value(true);
    });

    evaluator_.registerMemberFunction("realm", "add_treasure_chest", [this](const auto& args, const auto& kwargs) -> Value {
        float x = resolveArg(args, kwargs, 0, "x", Value(0.0)).asFloat();
        float y = resolveArg(args, kwargs, 1, "y", Value(3.6)).asFloat();
        float z = resolveArg(args, kwargs, 2, "z", Value(0.0)).asFloat();
        float rot = resolveArg(args, kwargs, 3, "rot", Value(0.0)).asFloat();
        world_.addTreasureChest({x, y, z}, rot);
        return Value(true);
    });

    evaluator_.registerMemberFunction("realm", "add_treasure_chest_grounded", [this](const auto& args, const auto& kwargs) -> Value {
        float x = resolveArg(args, kwargs, 0, "x", Value(0.0)).asFloat();
        float z = resolveArg(args, kwargs, 1, "z", Value(0.0)).asFloat();
        float rot = resolveArg(args, kwargs, 2, "rot", Value(0.0)).asFloat();
        world_.addTreasureChestGrounded(x, z, rot);
        return Value(true);
    });

    evaluator_.registerMemberFunction("realm", "add_sea_stack", [this](const auto& args, const auto& kwargs) -> Value {
        float x = resolveArg(args, kwargs, 0, "x", Value(0.0)).asFloat();
        float y = resolveArg(args, kwargs, 1, "y", Value(1.4)).asFloat();
        float z = resolveArg(args, kwargs, 2, "z", Value(0.0)).asFloat();
        float rad = resolveArg(args, kwargs, 3, "radius", Value(2.4)).asFloat();
        float h = resolveArg(args, kwargs, 4, "height", Value(8.5)).asFloat();
        uint32_t seed = static_cast<uint32_t>(resolveArg(args, kwargs, 5, "seed", Value(42)).asInt());
        world_.addSeaStack3D({x, y, z}, rad, h, seed);
        return Value(true);
    });

    evaluator_.registerMemberFunction("realm", "add_coastal_bridge", [this](const auto& args, const auto& kwargs) -> Value {
        float x0 = resolveArg(args, kwargs, 0, "x0", Value(0.0)).asFloat();
        float y0 = resolveArg(args, kwargs, 1, "y0", Value(3.6)).asFloat();
        float z0 = resolveArg(args, kwargs, 2, "z0", Value(0.0)).asFloat();
        float x1 = resolveArg(args, kwargs, 3, "x1", Value(0.0)).asFloat();
        float y1 = resolveArg(args, kwargs, 4, "y1", Value(3.6)).asFloat();
        float z1 = resolveArg(args, kwargs, 5, "z1", Value(10.0)).asFloat();
        float width = resolveArg(args, kwargs, 6, "width", Value(2.8)).asFloat();
        world_.addCoastalBridge({x0, y0, z0}, {x1, y1, z1}, width);
        return Value(true);
    });

    evaluator_.registerMemberFunction("realm", "add_ruined_house", [this](const auto& args, const auto& kwargs) -> Value {
        float x = resolveArg(args, kwargs, 0, "x", Value(0.0)).asFloat();
        float y = resolveArg(args, kwargs, 1, "y", Value(3.6)).asFloat();
        float z = resolveArg(args, kwargs, 2, "z", Value(0.0)).asFloat();
        float w = resolveArg(args, kwargs, 3, "w", Value(8.0)).asFloat();
        float h = resolveArg(args, kwargs, 4, "h", Value(4.2)).asFloat();
        float d = resolveArg(args, kwargs, 5, "d", Value(6.0)).asFloat();
        float collapse = resolveArg(args, kwargs, 6, "collapse", Value(0.55)).asFloat();
        world_.addRuinedHouse({x, y, z}, {w, h, d}, 0, collapse);
        return Value(true);
    });

    evaluator_.registerMemberFunction("realm", "add_barricade", [this](const auto& args, const auto& kwargs) -> Value {
        float x = resolveArg(args, kwargs, 0, "x", Value(0.0)).asFloat();
        float y = resolveArg(args, kwargs, 1, "y", Value(3.6)).asFloat();
        float z = resolveArg(args, kwargs, 2, "z", Value(0.0)).asFloat();
        float width = resolveArg(args, kwargs, 3, "width", Value(3.8)).asFloat();
        float rot = resolveArg(args, kwargs, 4, "rot", Value(0.0)).asFloat();
        world_.addBarricade({x, y, z}, width, rot);
        return Value(true);
    });

    evaluator_.registerMemberFunction("realm", "add_runic_altar", [this](const auto& args, const auto& kwargs) -> Value {
        float x = resolveArg(args, kwargs, 0, "x", Value(0.0)).asFloat();
        float y = resolveArg(args, kwargs, 1, "y", Value(3.6)).asFloat();
        float z = resolveArg(args, kwargs, 2, "z", Value(0.0)).asFloat();
        float rad = resolveArg(args, kwargs, 3, "radius", Value(2.4)).asFloat();
        bool glow = resolveArg(args, kwargs, 4, "glow", Value(true)).asBoolean();
        world_.addRunicAltar({x, y, z}, rad, glow);
        return Value(true);
    });

    evaluator_.registerMemberFunction("realm", "add_broken_bridge", [this](const auto& args, const auto& kwargs) -> Value {
        float x0 = resolveArg(args, kwargs, 0, "x0", Value(0.0)).asFloat();
        float y0 = resolveArg(args, kwargs, 1, "y0", Value(3.6)).asFloat();
        float z0 = resolveArg(args, kwargs, 2, "z0", Value(0.0)).asFloat();
        float x1 = resolveArg(args, kwargs, 3, "x1", Value(0.0)).asFloat();
        float y1 = resolveArg(args, kwargs, 4, "y1", Value(3.6)).asFloat();
        float z1 = resolveArg(args, kwargs, 5, "z1", Value(10.0)).asFloat();
        float breakRatio = resolveArg(args, kwargs, 6, "break_ratio", Value(0.4)).asFloat();
        float width = resolveArg(args, kwargs, 7, "width", Value(3.2)).asFloat();
        world_.addBrokenBridge({x0, y0, z0}, {x1, y1, z1}, breakRatio, width);
        return Value(true);
    });

    evaluator_.registerMemberFunction("realm", "set_time_of_day", [this](const auto& args, const auto& kwargs) -> Value {
        float timeVal = resolveArg(args, kwargs, 0, "time", Value(0.38)).asFloat();
        world_.setTimeOfDay(timeVal);
        return Value(true);
    });

    evaluator_.registerMemberFunction("realm", "get_time_of_day", [this](const auto&, const auto&) -> Value {
        return Value(static_cast<double>(world_.timeOfDay()));
    });

    evaluator_.registerMemberFunction("realm", "spawn_sprite", [this](const auto& args, const auto& kwargs) -> Value {
        float x = resolveArg(args, kwargs, 0, "x", Value(0.0)).asFloat();
        float z = resolveArg(args, kwargs, 1, "z", Value(0.0)).asFloat();
        float w = resolveArg(args, kwargs, 2, "w", Value(1.9)).asFloat();
        float h = resolveArg(args, kwargs, 3, "h", Value(2.4)).asFloat();
        uint16_t layer = static_cast<uint16_t>(resolveArg(args, kwargs, 4, "sprite", Value(static_cast<int>(TexLayer::SpriteHero))).asInt());
        uint32_t flags = static_cast<uint32_t>(resolveArg(args, kwargs, 5, "flags", Value(static_cast<int>(SpriteFlags::IdleBob))).asInt());
        world_.spawnSpriteGrounded(x, z, w, h, layer, flags, 0.5f);
        return Value(true);
    });

    evaluator_.registerMemberFunction("realm", "set_camera", [this](const auto& args, const auto& kwargs) -> Value {
        auto& cam = world_.camera();
        cam.position.x    = resolveArg(args, kwargs, 0, "px", Value(cam.position.x)).asFloat();
        cam.position.y    = resolveArg(args, kwargs, 1, "py", Value(cam.position.y)).asFloat();
        cam.position.z    = resolveArg(args, kwargs, 2, "pz", Value(cam.position.z)).asFloat();
        cam.target.x      = resolveArg(args, kwargs, 3, "tx", Value(cam.target.x)).asFloat();
        cam.target.y      = resolveArg(args, kwargs, 4, "ty", Value(cam.target.y)).asFloat();
        cam.target.z      = resolveArg(args, kwargs, 5, "tz", Value(cam.target.z)).asFloat();
        cam.fovDegrees    = resolveArg(args, kwargs, 6, "fov", Value(cam.fovDegrees)).asFloat();
        cam.focusDistance = resolveArg(args, kwargs, 7, "focus_dist", Value(cam.focusDistance)).asFloat();
        cam.focusRange    = resolveArg(args, kwargs, 8, "focus_range", Value(cam.focusRange)).asFloat();
        cam.bokehRadius   = resolveArg(args, kwargs, 9, "bokeh", Value(cam.bokehRadius)).asFloat();
        return Value(true);
    });

    evaluator_.registerMemberFunction("realm", "generate_open_world", [this](const auto& args, const auto& kwargs) -> Value {
        uint32_t seed = static_cast<uint32_t>(resolveArg(args, kwargs, 0, "seed", Value(1337)).asInt());
        int radius = resolveArg(args, kwargs, 1, "radius", Value(3)).asInt();
        float cx = resolveArg(args, kwargs, 2, "center_x", Value(0.0)).asFloat();
        float cz = resolveArg(args, kwargs, 3, "center_z", Value(0.0)).asFloat();
        world_.generateOpenWorld(seed, radius, cx, cz);
        return Value(true);
    });

    evaluator_.registerMemberFunction("realm", "update_clipmap", [this](const auto& args, const auto& kwargs) -> Value {
        float tx = resolveArg(args, kwargs, 0, "tx", Value(world_.camera().target.x)).asFloat();
        float tz = resolveArg(args, kwargs, 1, "tz", Value(world_.camera().target.z)).asFloat();
        world_.updateStreamingClipmap({tx, 0.0f, tz});
        return Value(true);
    });

    evaluator_.registerMemberFunction("realm", "is_open_world", [this](const auto&, const auto&) -> Value {
        return Value(world_.isOpenWorld());
    });

    evaluator_.registerMemberFunction("realm", "chunk_count", [this](const auto&, const auto&) -> Value {
        return Value(static_cast<int>(world_.chunks().size()));
    });

    evaluator_.registerMemberFunction("realm", "get_terrain_height", [this](const auto& args, const auto& kwargs) -> Value {
        float x = resolveArg(args, kwargs, 0, "x", Value(0.0)).asFloat();
        float z = resolveArg(args, kwargs, 1, "z", Value(0.0)).asFloat();
        return Value(world_.getTerrainHeightAt(x, z));
    });

    evaluator_.registerMemberFunction("realm", "get_macro_site", [this](const auto& args, const auto& kwargs) -> Value {
        float x = resolveArg(args, kwargs, 0, "x", Value(0.0)).asFloat();
        float z = resolveArg(args, kwargs, 1, "z", Value(0.0)).asFloat();
        uint32_t seed = static_cast<uint32_t>(resolveArg(args, kwargs, 2, "seed", Value(static_cast<int>(world_.openWorldSeed()))).asInt());
        MacroSiteInfo site = MacroHistoryGrid::getNearestSite(x, z, seed);
        std::map<std::string, Value> siteDict;
        siteDict["has_site"]  = Value(site.type != MacroSiteType::Wilderness);
        siteDict["type"]      = Value(static_cast<int>(site.type));
        siteDict["name"]      = Value(site.name);
        siteDict["figure"]    = Value(site.notableFigure);
        siteDict["headline"]  = Value(site.loreHeadline);
        siteDict["chronicle"] = Value(site.chronicleExcerpt);
        siteDict["year"]      = Value(site.yearOccurred);
        siteDict["x"]         = Value(site.worldX);
        siteDict["z"]         = Value(site.worldZ);
        float ddx = x - site.worldX;
        float ddz = z - site.worldZ;
        siteDict["dist"]      = Value(std::sqrt(ddx * ddx + ddz * ddz));
        return Value(siteDict);
    });
}

bool RealmScriptEngine::executeScript(const std::string& sourceCode) {
    lastError_.clear();
    evaluator_.evaluateSource(sourceCode);
    if (!evaluator_.getLastError().empty()) {
        lastError_ = evaluator_.getLastError();
        return false;
    }
    return true;
}

bool RealmScriptEngine::executeFile(const std::string& filePath) {
    std::ifstream ifs(filePath);
    if (!ifs.is_open()) {
        lastError_ = "Could not open Eatscript file: " + filePath;
        return false;
    }
    std::stringstream ss;
    ss << ifs.rdbuf();
    return executeScript(ss.str());
}

} // namespace eatsrealms
