#pragma once

#include "world.hpp"
#include <glm/vec2.hpp>

namespace wsim {

class Water {
public:
    explicit Water(const World& world);

    bool splash(glm::vec3 feet, float strength);
    void step();
    int edge() const { return edge_; }
    const std::vector<float>& heights() const { return heights_; }
    std::uint64_t revision() const { return revision_; }

private:
    struct Cell { int index; std::array<int,4> neighbors; };
    const int edge_;
    std::vector<std::uint8_t> level_;
    std::vector<Cell> cells_;
    std::vector<float> heights_, previous_;
    int remainingSteps_ = 0;
    std::uint64_t revision_ = 0;
};

} // namespace wsim
