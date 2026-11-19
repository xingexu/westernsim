#include "rigid_boxes.hpp"

#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <algorithm>
#include <cmath>
#include <limits>

namespace wsim {
namespace {

struct Collider {
    glm::vec3 center, half;
    glm::mat3 axes{1.f};
};

struct Contact {
    glm::vec3 normal;
    float depth;
};

Collider shape(const RigidBox& body) {
    return {body.position,boxHalfSize,glm::mat3_cast(body.rotation)};
}

glm::vec3 extent(const Collider& box) {
    return glm::abs(box.axes[0])*box.half.x + glm::abs(box.axes[1])*box.half.y +
           glm::abs(box.axes[2])*box.half.z;
}

std::optional<Contact> intersect(const Collider& a, const Collider& b) {
    const auto offset = a.center-b.center;
    Contact result{{},std::numeric_limits<float>::max()};
    const auto test = [&](glm::vec3 axis) {
        const float length2 = glm::dot(axis,axis);
        if (length2 < .000001f) return true;
        axis /= std::sqrt(length2);
        const float distance = glm::dot(offset,axis);
        const float penetration = glm::dot(a.half,glm::abs(axis*a.axes)) +
                                  glm::dot(b.half,glm::abs(axis*b.axes)) - std::abs(distance);
        if (penetration <= 0.f) return false;
        if (penetration < result.depth)
            result = {distance >= 0.f ? axis : -axis,penetration};
        return true;
    };
    // Separating axes are the six face normals and the nine edge cross products.
    for (int i = 0; i < 3; ++i)
        if (!test(a.axes[i]) || !test(b.axes[i])) return std::nullopt;
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            if (!test(glm::cross(a.axes[i],b.axes[j]))) return std::nullopt;
    return result;
}

glm::vec3 supportPoint(const Collider& box, glm::vec3 normal, glm::vec3 otherCenter) {
    auto point = box.center;
    for (int i = 0; i < 3; ++i) {
        const float alignment = glm::dot(box.axes[i],normal);
        const float coordinate = std::abs(alignment) > .001f
            ? std::copysign(box.half[i],alignment)
            : std::clamp(glm::dot(otherCenter-box.center,box.axes[i]),-box.half[i],box.half[i]);
        point += box.axes[i]*coordinate;
    }
    return point;
}

glm::mat3 inverseInertia(const RigidBox& body, const glm::mat3& axes) {
    const auto squared = boxHalfSize*boxHalfSize;
    // A uniform cuboid's diagonal inertia expressed in world coordinates.
    glm::mat3 diagonal{0.f};
    for (int i = 0; i < 3; ++i)
        diagonal[i][i] = 3.f/(body.mass*(squared[(i+1)%3]+squared[(i+2)%3]));
    return axes*diagonal*glm::transpose(axes);
}

void resolve(RigidBox& a, const Collider& other, RigidBox* b = nullptr, glm::vec3 motion = {}) {
    const auto box = shape(a);
    const auto contact = intersect(box,other);
    if (!contact) return;
    const auto normal = contact->normal;
    const auto onBox = supportPoint(box,-normal,other.center);
    const auto point = b ? .5f*(onBox+supportPoint(other,normal,box.center)) : onBox;
    const auto ra = point-a.position, rb = point-other.center;
    const float invA = 1.f/a.mass, invB = b ? 1.f/b->mass : 0.f;
    const auto inertiaA = inverseInertia(a,box.axes);
    const auto inertiaB = b ? inverseInertia(*b,other.axes) : glm::mat3(0.f);
    const auto relative = [&]() {
        return a.velocity+glm::cross(a.angularVelocity,ra) -
            (b ? b->velocity+glm::cross(b->angularVelocity,rb) : motion);
    };
    const auto effectiveMass = [&](glm::vec3 direction) {
        const auto angularA = glm::cross(ra,direction), angularB = glm::cross(rb,direction);
        return invA+invB + glm::dot(inertiaA*angularA,angularA) +
               glm::dot(inertiaB*angularB,angularB);
    };
    const auto apply = [&](glm::vec3 impulse) {
        a.velocity += impulse*invA;
        a.angularVelocity += inertiaA*glm::cross(ra,impulse);
        if (b) {
            b->velocity -= impulse*invB;
            b->angularVelocity -= inertiaB*glm::cross(rb,impulse);
        }
    };
    const float correction = .8f*std::max(contact->depth-.001f,0.f)/(invA+invB);
    a.position += normal*(correction*invA);
    if (b) b->position -= normal*(correction*invB);

    const float approach = glm::dot(relative(),normal);
    if (approach >= 0.f) return;
    const float bounce = approach < -1.2f ? .31f : 0.f;
    const float impulse = -(1.f+bounce)*approach/effectiveMass(normal);
    apply(normal*impulse);
    const auto tangent = relative()-normal*glm::dot(relative(),normal);
    const float slip = glm::length(tangent);
    if (slip > .0001f) {
        const auto direction = tangent/slip;
        apply(-direction*std::min(slip/effectiveMass(direction),.68f*impulse));
    }
}

template<typename Function>
bool solidCells(const World& world, const Collider& box, Function visit) {
    const auto half = extent(box);
    const glm::ivec3 low = glm::floor(box.center-half), high = glm::floor(box.center+half);
    for (int y = low.y; y <= high.y; ++y)
        for (int z = low.z; z <= high.z; ++z)
            for (int x = low.x; x <= high.x; ++x)
                if (solid(world.get({x,y,z})) &&
                    visit(Collider{glm::vec3(x,y,z)+.5f,glm::vec3(.5f)})) return true;
    return false;
}

glm::vec3 limit(glm::vec3 value, float maxSpeed) {
    const float speed = glm::length(value);
    return speed > maxSpeed ? value*(maxSpeed/speed) : value;
}

} // namespace

bool RigidBoxes::spawn(const World& world, glm::vec3 position) {
    RigidBox body{position};
    const auto box = shape(body);
    if (!world.contains(glm::ivec3(glm::floor(position))) ||
        solidCells(world,box,[&](const Collider& voxel) { return intersect(box,voxel).has_value(); }) ||
        std::any_of(bodies_.begin(),bodies_.end(),[&](const auto& other) { return intersect(box,shape(other)).has_value(); }))
        return false;
    if (bodies_.size() == 32) bodies_.erase(bodies_.begin());
    bodies_.push_back(body);
    return true;
}

void RigidBoxes::step(const World& world, const Player& player, glm::vec3 previousFeet) {
    constexpr float dt = static_cast<float>(physicsStep);
    const Collider avatar{player.feet+glm::vec3(0,playerHeight*.5f,0),
                          {playerRadius,playerHeight*.5f,playerRadius}};
    const auto avatarVelocity = (player.feet-previousFeet)/dt;
    for (auto& body : bodies_) {
        body.velocity.y -= 11.f*dt;
        body.velocity = limit(body.velocity,24.f);
        body.position += dt*body.velocity;
        body.angularVelocity = .997f*limit(body.angularVelocity,16.f);
        body.rotation = glm::normalize(body.rotation +
            (.5f*dt)*(glm::quat(0.f,body.angularVelocity)*body.rotation));
    }
    for (int iteration = 0; iteration < 4; ++iteration) {
        for (std::size_t i = 0; i < bodies_.size(); ++i) {
            auto& body = bodies_[i];
            if (glm::distance(body.position,avatar.center) < 1.5f)
                resolve(body,avatar,nullptr,avatarVelocity);
            for (std::size_t j = i+1; j < bodies_.size(); ++j) {
                auto& other = bodies_[j];
                if (glm::distance(body.position,other.position) < 1.4f)
                    resolve(body,shape(other),&other);
            }
            solidCells(world,shape(body),[&](const Collider& voxel) {
                resolve(body,voxel);
                return false;
            });
        }
    }
    bodies_.erase(std::remove_if(bodies_.begin(),bodies_.end(),[&](const RigidBox& body) {
        const auto p = body.position;
        return p.y < -4.f || p.x < -2.f || p.z < -2.f || p.x > world.edge()+2.f || p.z > world.edge()+2.f;
    }),bodies_.end());
}

} // namespace wsim
