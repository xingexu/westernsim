#include "scenery.hpp"

#include <glm/geometric.hpp>
#include <array>
#include <cmath>

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

void foliage(Mesh& mesh, glm::vec3 center, glm::vec3 radius, Block leaf, float turn) {
    constexpr int sides = 9;
    constexpr float tau = 6.2831853f;
    const auto material = static_cast<float>(leaf);
    const auto triangle = [&](glm::vec3 a, glm::vec3 b, glm::vec3 c) {
        const auto first = static_cast<std::uint32_t>(mesh.vertices.size());
        // Smooth the individual crown lobes while retaining their low-poly silhouettes.
        for (const auto p : {a,b,c})
            mesh.vertices.push_back({center+p,glm::normalize(p/(radius*radius)),material});
        const bool outward = glm::dot(glm::cross(b-a,c-a),a+b+c) > 0;
        mesh.indices.insert(mesh.indices.end(),{first,first+(outward ? 1u : 2u),first+(outward ? 2u : 1u)});
    };
    const auto ring = [&](int i, bool upper) {
        const float a = turn + tau*(i+(upper ? .5f : 0.f))/sides;
        const float y = upper ? .44f : -.42f;
        return glm::vec3(std::cos(a)*.88f,y,std::sin(a)*.88f)*radius;
    };
    const glm::vec3 top(0,radius.y,0), bottom(0,-radius.y,0);
    for (int i = 0; i < sides; ++i) {
        const auto low = ring(i,false), nextLow = ring(i+1,false);
        const auto high = ring(i,true), nextHigh = ring(i+1,true);
        triangle(low,nextLow,bottom);
        triangle(low,high,nextLow);
        triangle(high,nextHigh,nextLow);
        triangle(high,top,nextHigh);
    }
}

} // namespace

void appendScenery(Mesh& mesh, glm::ivec3 anchor, Block block) {
    const auto at = glm::vec3(anchor) + glm::vec3(.5f,0,.5f);
    const auto piece = [&](glm::vec3 center, glm::vec3 size, Block material) {
        box(mesh,at+center,size,material);
    };
    if (canopy(block)) {
        const auto leaf = block == Block::canopyGold ? Block::goldLeaves :
                          block == Block::canopyCopper ? Block::redLeaves : Block::leaves;
        const float turn = ((anchor.x*31+anchor.z*19)%19)*.25f;
        foliage(mesh,at+glm::vec3(0,.24f,-.24f),{2.06f,1.88f,1.79f},leaf,turn);
        foliage(mesh,at+glm::vec3(-.95f,-.56f,.56f),{1.58f,1.34f,1.41f},leaf,turn+1.f);
        foliage(mesh,at+glm::vec3(1.06f,-.23f,.60f),{1.64f,1.49f,1.42f},leaf,turn+.5f);
        foliage(mesh,at+glm::vec3(-.38f,.76f,-.62f),{1.45f,1.44f,1.51f},leaf,turn+.8f);
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
            foliage(mesh,at+glm::vec3(x,.51f,0),{.32f,.38f,.38f},Block::needles,x);
            foliage(mesh,at+glm::vec3(x-.11f,.70f,.11f),{.13f,.16f,.14f},Block::flowers,x);
        }
    }
}

} // namespace wsim
