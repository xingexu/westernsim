#include "player.hpp"

#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <algorithm>
#include <cmath>

namespace ql {
namespace {

const glm::vec3 low{-playerRadius, 0.f, -playerRadius};
const glm::vec3 high{playerRadius, playerHeight, playerRadius};
constexpr float epsilon = .0001f;

bool move(const World& world, glm::vec3& feet, int axis, float delta) {
    if (delta == 0) return false;
    auto next = feet;
    next[axis] += delta;
    const glm::ivec3 begin = glm::floor(next + low + epsilon);
    const glm::ivec3 end = glm::floor(next + high - epsilon);
    for (int y = begin.y; y <= end.y; ++y) {
        for (int z = begin.z; z <= end.z; ++z) {
            for (int x = begin.x; x <= end.x; ++x) {
                const glm::ivec3 cell{x,y,z};
                if (!solid(world.get(cell))) continue;
                if (delta > 0) next[axis] = std::min(next[axis], cell[axis] - high[axis]);
                else next[axis] = std::max(next[axis], cell[axis] + 1.f - low[axis]);
            }
        }
    }
    const bool collided = std::abs(next[axis] - feet[axis] - delta) > epsilon;
    feet = next;
    return collided;
}

} // namespace

void Player::step(const World& world, glm::vec3 wish, bool sprint, bool jump) {
    constexpr auto dt = static_cast<float>(physicsStep);
    const bool swimming = world.get(glm::ivec3(glm::floor(feet + glm::vec3(0,.5f,0)))) == Block::water;
    const float speed = swimming ? 2.5f : sprint ? 7.f : 4.8f;
    const float amount = glm::dot(wish, wish);
    if (amount > 1.f) wish /= std::sqrt(amount);
    velocity.x = wish.x * speed;
    velocity.z = wish.z * speed;
    for (const int axis : {0, 2}) {
        const auto before = feet;
        if (move(world, feet, axis, velocity[axis] * dt) && grounded && !jump) {
            auto raised = before;
            if (!move(world, raised, 1, 1.01f) &&
                !move(world, raised, axis, velocity[axis] * dt)) {
                move(world, raised, 1, -.08f);
                feet = raised;
            }
        }
    }
    if (grounded && jump) velocity.y = 7.6f;
    if (swimming) velocity.y = jump ? 3.f : std::max(velocity.y - 7.f * dt, -2.f);
    else velocity.y = std::max(velocity.y - 22.f * dt, -30.f);
    const bool verticalContact = move(world, feet, 1, velocity.y * dt);
    grounded = verticalContact && velocity.y < 0;
    if (verticalContact) velocity.y = 0;
    const bool moving = amount > .001f;
    gait += ((moving && grounded ? 1.f : 0.f) - gait) * 12.f * dt;
    if (moving) {
        const float target = std::atan2(-wish.x, -wish.z);
        heading += std::remainder(target - heading, 6.2831853f) * 14.f * dt;
        stride += speed * 2.5f * dt;
    }
}

bool Player::overlaps(glm::ivec3 block) const {
    for (int axis = 0; axis < 3; ++axis)
        if (feet[axis] + high[axis] <= block[axis] + epsilon ||
            feet[axis] + low[axis] >= block[axis] + 1.f - epsilon) return false;
    return true;
}

} // namespace ql
