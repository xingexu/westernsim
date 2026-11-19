#pragma once

#include "player.hpp"
#include <glm/gtc/quaternion.hpp>

namespace wsim {

inline constexpr glm::vec3 boxHalfSize{.43f,.34f,.39f};

struct RigidBox {
    glm::vec3 position{};
    glm::vec3 velocity{};
    glm::quat rotation{1,0,0,0};
    glm::vec3 angularVelocity{};
    float mass = 2.f;
};

class RigidBoxes {
public:
    bool spawn(const World& world, glm::vec3 position);
    void step(const World& world, const Player& player, glm::vec3 previousFeet);
    const std::vector<RigidBox>& bodies() const { return bodies_; }

private:
    std::vector<RigidBox> bodies_;
};

} // namespace wsim
