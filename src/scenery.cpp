#include "scenery.hpp"

#include <utility>

namespace wsim {
namespace {

void box(Mesh& mesh, glm::vec3 center, glm::vec3 size, Block block) {
    const auto material = static_cast<float>(block);
    for (int axis = 0; axis < 3; ++axis) {
        for (int sign : {-1,1}) {
            glm::vec3 p = center-size*.5f, u{}, v{}, n{};
            n[axis] = sign;
            if (sign > 0) p[axis] += size[axis];
            u[(axis+1)%3] = size[(axis+1)%3];
            v[(axis+2)%3] = size[(axis+2)%3];
            if (sign < 0) std::swap(u,v);
            const auto first = static_cast<std::uint32_t>(mesh.vertices.size());
            for (const auto at : {p,p+u,p+u+v,p+v}) mesh.vertices.push_back({at,n,material});
            mesh.indices.insert(mesh.indices.end(),{first,first+1,first+2,first,first+2,first+3});
        }
    }
}

} // namespace

void appendScenery(Mesh& mesh, glm::ivec3 anchor, Block block) {
    const auto at = glm::vec3(anchor) + glm::vec3(.5f,0,.5f);
    const auto piece = [&](glm::vec3 center, glm::vec3 size, Block material) {
        box(mesh,at+center*.5f,size*.5f,material);
    };
    if (canopy(block)) {
        const auto leaf = block == Block::canopyGold ? Block::goldLeaves :
                          block == Block::canopyCopper ? Block::redLeaves : Block::leaves;
        const int pattern = anchor.x*31+anchor.z*19;
        const float flip = pattern%2 == 0 ? 1.f : -1.f;
        const float span = .64f + .11f*(pattern%5);
        const float height = 1.65f + .31f*(pattern%5);
        const auto wood = pattern%7 == 1 ? Block::birch : Block::cedar;
        const float diameter = .19f + .045f*(pattern%5);
        box(mesh,at+glm::vec3(0,height*.5f,0),{diameter,height,diameter},wood);
        const auto crown = [&](glm::vec3 center, glm::vec3 size) {
            box(mesh,at+center*span+glm::vec3(0,height,0),size*span,leaf);
        };
        crown({0,-.38f,0},{1.42f,.90f,1.5f});
        crown({0,.29f,-.22f},{2.50f,1.20f,2.1f});
        crown({flip*.19f,1.12f,-.27f},{1.64f,.64f,1.66f});
        crown({flip*.40f,1.54f,-.47f},{.97f,.31f,1.07f});
        crown({flip*.99f,-.24f,.44f},{.93f,1.07f,1.12f});
        crown({-flip*.98f,.08f,.49f},{1.14f,1.03f,1.59f});
    } else if (block == Block::lampPost) {
        piece({0,.12f,0},{.52f,.24f,.52f},Block::limestone);
        piece({0,.30f,0},{.32f,.20f,.32f},Block::iron);
        piece({0,1.43f,0},{.15f,2.24f,.15f},Block::iron);
        piece({0,2.55f,0},{.48f,.13f,.48f},Block::iron);
        piece({0,2.79f,0},{.40f,.39f,.40f},Block::lantern);
        for (const float x : {-.19f,.19f})
            for (const float z : {-.19f,.19f})
                piece({x,2.80f,z},{.055f,.41f,.055f},Block::iron);
        piece({0,3.03f,0},{.49f,.14f,.49f},Block::iron);
        piece({0,3.14f,0},{.33f,.08f,.33f},Block::copper);
    } else if (block == Block::bench) {
        for (const float x : {-.79f,.79f}) {
            piece({x,.29f,0},{.135f,.58f,.73f},Block::iron);
            piece({x,.61f,-.31f},{.13f,1.22f,.13f},Block::iron);
            piece({x,.72f,0},{.13f,.08f,.71f},Block::iron);
        }
        for (const float z : {-.19f,0.f,.19f})
            piece({0,.49f,z},{2.24f,.10f,.16f},Block::planks);
        for (const float y : {.80f,.99f,1.18f})
            piece({0,y,-.32f},{2.24f,.15f,.095f},Block::planks);
    } else if (block == Block::planter) {
        piece({0,.19f,0},{1.56f,.38f,.90f},Block::sandstone);
        piece({0,.38f,0},{1.68f,.15f,1.02f},Block::limestone);
        for (const float x : {-.48f,0.f,.48f}) {
            piece({x,.53f,0},{.37f,.38f,.61f},Block::needles);
            piece({x-.11f,.72f,.11f},{.15f,.12f,.18f},Block::flowers);
        }
    } else if (block == Block::wildflowers) {
        for (const auto x : {-.54f,.16f,.61f}) {
            const float z = x*x-.35f;
            piece({x,.09f,z},{.028f,.18f,.033f},Block::needles);
            piece({x,.19f,z},{.11f,.047f,.10f},x > 0 ? Block::flowers : Block::flagWhite);
        }
    }
}

} // namespace wsim
