#include "eatsrealms/script_bindings.hpp"
#include <fstream>
#include <sstream>

namespace eatsrealms {

using eatsbits::eatscript::Value;
using eatsbits::eatscript::resolveArg;

RealmScriptEngine::RealmScriptEngine(RealmWorld& world)
    : world_(world) {
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
    texDict["SpriteOakTree"]    = Value(static_cast<int>(TexLayer::SpriteOakTree));
    texDict["SpriteWildflower"] = Value(static_cast<int>(TexLayer::SpriteWildflower));
    texDict["SpriteTorchFlame"] = Value(static_cast<int>(TexLayer::SpriteTorchFlame));
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
