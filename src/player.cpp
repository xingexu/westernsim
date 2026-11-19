#include "player.hpp"
#include "scenery.hpp"

#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <algorithm>
#include <cmath>

namespace wsim {
namespace {

const glm::vec3 low{-playerRadius, 0.f, -playerRadius};
const glm::vec3 high{playerRadius, playerHeight, playerRadius};
constexpr float epsilon = .0001f;

bool move(const World& world, glm::vec3& feet, int axis, float delta) {
    if (delta == 0) return false;
    auto next = feet;
    next[axis] += delta;
    const auto playerMin = next + low + epsilon;
    const auto playerMax = next + high - epsilon;
    const glm::ivec3 begin = glm::floor(playerMin);
    const glm::ivec3 end = glm::floor(playerMax);
    const auto clip = [&](glm::vec3 min, glm::vec3 max) {
        if (delta > 0) next[axis] = std::min(next[axis], min[axis] - high[axis]);
        else next[axis] = std::max(next[axis], max[axis] - low[axis]);
    };
    // A trunk can overlap the player while its anchor is below the player's feet.
    for (int y = begin.y - treeTrunkCells + 1; y <= end.y; ++y) {
        for (int z = begin.z; z <= end.z; ++z) {
            for (int x = begin.x; x <= end.x; ++x) {
                const glm::ivec3 cell{x,y,z};
                const auto block = world.get(cell);
                if (y >= begin.y && solid(block)) {
                    clip(glm::vec3(cell),glm::vec3(cell)+1.f);
                } else if (canopy(block)) {
                    const auto size = treeTrunkSize(cell);
                    const auto min = glm::vec3(cell) + glm::vec3(1.f-size.x,0,1.f-size.z)*.5f;
                    const auto max = min+size;
                    if (glm::all(glm::lessThanEqual(playerMin,max)) &&
                        glm::all(glm::greaterThanEqual(playerMax,min)))
                        clip(min,max);
                }
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
    const bool swimming = world.get(glm::ivec3(glm::floor(feet + glm::vec3(0,.25f,0)))) == Block::water;
    const float speed = swimming ? 6.25f : sprint ? 17.5f : 12.f;
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
    if (grounded && jump) velocity.y = 4.8f;
    if (swimming) velocity.y = jump ? 2.5f : std::max(velocity.y - 3.5f * dt, -1.f);
    else velocity.y = std::max(velocity.y - 11.f * dt, -15.f);
    const bool verticalContact = move(world, feet, 1, velocity.y * dt);
    grounded = verticalContact && velocity.y < 0;
    if (verticalContact) velocity.y = 0;
    const bool moving = amount > .001f;
    gait += ((moving && grounded ? 1.f : 0.f) - gait) * 12.f * dt;
    if (moving) {
        const float target = std::atan2(-wish.x, -wish.z);
        heading += std::remainder(target - heading, 6.2831853f) * 14.f * dt;
        stride += speed * dt;
    }
}

bool Player::overlaps(glm::ivec3 block) const {
    for (int axis = 0; axis < 3; ++axis)
        if (feet[axis] + high[axis] <= block[axis] + epsilon ||
            feet[axis] + low[axis] >= block[axis] + 1.f - epsilon) return false;
    return true;
}

} // namespace wsim
