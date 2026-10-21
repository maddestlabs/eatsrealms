#ifndef EATSREALMS_TILE_CELL_HPP
#define EATSREALMS_TILE_CELL_HPP

#include "eatsrealms/hd2d_types.hpp"
#include <cstdint>
#include <algorithm>

namespace eatsrealms {

enum class SlopeDir : uint8_t {
    None     = 0,
    PlusX    = 1,
    MinusX   = 2,
    PlusZ    = 3,
    MinusZ   = 4,
    CornerNW = 5,
    CornerNE = 6,
    CornerSE = 7,
    CornerSW = 8,
    Custom   = 9
};

enum class SlopeStyle : uint8_t {
    SmoothRamp = 0,
    Stairs     = 1
};

struct TileCell {
    int16_t elevation{2}; // Base height in 0.6-unit steps
    int8_t cornerOffset[4]{0, 0, 0, 0}; // Corner offsets: [0]=NW(x0,z0), [1]=NE(x1,z0), [2]=SE(x1,z1), [3]=SW(x0,z1)
    uint16_t topLayer{TexLayer::GrassMeadow};
    uint16_t cliffLayer{TexLayer::CliffStone};
    SlopeDir slope{SlopeDir::None};
    bool isStairs{false};
    bool hasWater{false};
    float waterY{0.65f};

    [[nodiscard]] int16_t getCornerElevation(int corner) const noexcept {
        return elevation + static_cast<int16_t>(cornerOffset[corner & 3]);
    }

    void setCornerElevations(int16_t nw, int16_t ne, int16_t se, int16_t sw) noexcept {
        int16_t minE = std::min({nw, ne, se, sw});
        elevation = minE;
        cornerOffset[0] = static_cast<int8_t>(std::clamp<int>(nw - minE, -128, 127));
        cornerOffset[1] = static_cast<int8_t>(std::clamp<int>(ne - minE, -128, 127));
        cornerOffset[2] = static_cast<int8_t>(std::clamp<int>(se - minE, -128, 127));
        cornerOffset[3] = static_cast<int8_t>(std::clamp<int>(sw - minE, -128, 127));
        if (nw == ne && ne == se && se == sw) {
            slope = SlopeDir::None;
        } else {
            slope = SlopeDir::Custom;
        }
    }

    [[nodiscard]] bool isSloped() const noexcept {
        return slope != SlopeDir::None || cornerOffset[0] != 0 || cornerOffset[1] != 0 || cornerOffset[2] != 0 || cornerOffset[3] != 0;
    }
};

} // namespace eatsrealms

#endif // EATSREALMS_TILE_CELL_HPP
