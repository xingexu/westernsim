#pragma once

#include "world.hpp"

namespace wsim {

constexpr double physicsStep = 1.0 / 120.0;
constexpr float playerRadius = .30f;
constexpr float playerHeight = 1.66f;

struct Player {
    glm::vec3 feet{}, velocity{};
    float heading = 0.f;
    float stride = 0.f;
    float gait = 0.f;
    bool grounded = false;

    void step(const World& world, glm::vec3 wish, bool sprint, bool jump);
    bool overlaps(glm::ivec3 block) const;
};

} // namespace wsim
