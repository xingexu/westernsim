#include "avatar.hpp"
#include "western_avatar_data.hpp"

#include <glm/ext/matrix_transform.hpp>
#include <algorithm>
#include <cmath>

namespace wsim {
namespace {

struct PartData {
    const westernAvatar::Quad* quads;
    std::size_t count;
    glm::vec3 pivot;
};

template<std::size_t N>
PartData part(const westernAvatar::Quad (&quads)[N], const float (&pivot)[3]) {
    return {quads,N,{pivot[0],pivot[1],pivot[2]}};
}

const std::array<PartData,avatarPartCount> parts{
    part(westernAvatar::torso,westernAvatar::torsoPivot),
    part(westernAvatar::head,westernAvatar::headPivot),
    part(westernAvatar::leftLeg,westernAvatar::leftLegPivot),
    part(westernAvatar::rightLeg,westernAvatar::rightLegPivot),
    part(westernAvatar::leftArm,westernAvatar::leftArmPivot),
    part(westernAvatar::leftForearm,westernAvatar::leftForearmPivot),
    part(westernAvatar::rightArm,westernAvatar::rightArmPivot),
    part(westernAvatar::rightForearm,westernAvatar::rightForearmPivot)
};

} // namespace

AvatarMesh westernAvatarMesh(AvatarPart part) {
    const auto& data = parts[part];
    AvatarMesh mesh;
    mesh.vertices.reserve(data.count*4);
    mesh.indices.reserve(data.count*6);
    for (std::size_t i = 0; i < data.count; ++i) {
        const auto& quad = data.quads[i];
        const auto first = static_cast<std::uint32_t>(mesh.vertices.size());
        const glm::vec3 base{quad.x,quad.y,quad.z};
        for (unsigned corner = 0; corner < 4; ++corner) {
            const auto bits = quad.corners >> (corner*3);
            const glm::vec3 offset{bits&1,(bits>>1)&1,(bits>>2)&1};
            const auto rgb = quad.colors[corner];
            const glm::vec3 color{(rgb>>16)&255,(rgb>>8)&255,rgb&255};
            mesh.vertices.push_back({(base+offset)*westernAvatar::voxelSize,color/255.f});
        }
        mesh.indices.insert(mesh.indices.end(),{first,first+1,first+2,first+2,first+1,first+3});
    }
    return mesh;
}

std::array<glm::mat4, avatarPartCount> westernAvatarPose(const Player& player) {
    constexpr float pi = 3.14159265f;
    constexpr float scale = playerHeight / westernAvatar::height;
    const float step = std::sin(player.stride) * player.gait;
    const float lift = std::abs(step) * .7f * scale;
    const float jump = player.grounded ? 0.f : std::clamp(player.velocity.y/6.f,0.f,1.f);
    // The portfolio model faces +Z. Western Sim's player heading faces -Z at zero.
    auto root = glm::translate(glm::mat4(1),player.feet + glm::vec3(0,lift,0));
    root = glm::scale(glm::rotate(root,player.heading+pi,{0,1,0}),glm::vec3(scale));
    const auto joint = [&](AvatarPart part, const glm::mat4& parent, float angle = 0.f) {
        return glm::rotate(glm::translate(parent,parts[part].pivot),angle,{1,0,0});
    };
    const auto lArm = joint(leftArm,root,-step*.60f-jump*.36f);
    const auto rArm = joint(rightArm,root,step*.60f-jump*.36f);
    return {joint(torso,root),joint(head,root),
            joint(leftLeg,root,step*.75f-jump*.58f),joint(rightLeg,root,-step*.75f+jump*.32f),
            lArm,joint(leftForearm,lArm,-jump*.12f),rArm,joint(rightForearm,rArm,-jump*.12f)};
}

} // namespace wsim
