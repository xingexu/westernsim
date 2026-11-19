#pragma once

#include "player.hpp"

#include <glm/mat4x4.hpp>
#include <array>
#include <vector>

namespace wsim {

enum AvatarPart { torso, head, leftLeg, rightLeg, leftArm, leftForearm, rightArm, rightForearm, avatarPartCount };

struct AvatarVertex { glm::vec3 position, color; };
struct AvatarMesh {
    std::vector<AvatarVertex> vertices;
    std::vector<std::uint32_t> indices;
};

AvatarMesh westernAvatarMesh(AvatarPart part);
std::array<glm::mat4, avatarPartCount> westernAvatarPose(const Player& player);

} // namespace wsim
