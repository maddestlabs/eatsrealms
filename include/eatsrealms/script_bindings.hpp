#ifndef EATSREALMS_SCRIPT_BINDINGS_HPP
#define EATSREALMS_SCRIPT_BINDINGS_HPP

#include "eatsrealms/realm_world.hpp"
#include "eatsbits/eatscript/evaluator.hpp"
#include <string>
#include <vector>

namespace eatsrealms {

/**
 * Binds Eatsrealms' declarative HD-2D TileMap, PropFactory, and Sprite3D API to Eatscript.
 * Supports executing in-memory scripts as well as live hot-reloading of .eats scene files.
 */
class RealmScriptEngine {
public:
    explicit RealmScriptEngine(RealmWorld& world);

    bool executeScript(const std::string& sourceCode);
    bool executeFile(const std::string& filePath);

    [[nodiscard]] const std::string& lastError() const noexcept { return lastError_; }
    [[nodiscard]] const std::vector<std::string>& logs() const noexcept { return evaluator_.getLogs(); }

private:
    void registerRealmBindings();

    RealmWorld& world_;
    eatsbits::eatscript::Evaluator evaluator_;
    std::string lastError_;
};

} // namespace eatsrealms

#endif // EATSREALMS_SCRIPT_BINDINGS_HPP
