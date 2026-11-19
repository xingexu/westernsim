#include "campus.hpp"

#include <glm/geometric.hpp>
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace ql {
namespace {

constexpr int waterLevel = 4;

float distanceTo(glm::vec2 p, glm::vec2 a, glm::vec2 b) {
    const auto d = b - a;
    return glm::length(p - a - d * std::clamp(glm::dot(p - a,d) / glm::dot(d,d),0.f,1.f));
}

float distanceToRiver(glm::vec2 p) {
    float distance = 1000.f;
    for (std::size_t i = 1; i < river.size(); ++i)
        distance = std::min(distance,distanceTo(p,river[i-1],river[i]));
    return distance;
}

float relief(glm::vec2 p) {
    const auto hill = (p - glm::vec2(78,82)) / glm::vec2(37,36);
    const auto ridge = (p - glm::vec2(104,48)) / glm::vec2(79,51);
    return 6.5f + 8.1f * std::exp(-glm::dot(hill,hill)) + 2.7f * std::exp(-glm::dot(ridge,ridge))
           + .43f * std::sin(p.x * .17f + p.y * .14f);
}

int base(const Landmark& landmark) {
    return static_cast<int>(std::round(relief(glm::vec2(landmark.low + landmark.high) * .5f)));
}

int terrainHeight(glm::vec2 p, float riverDistance) {
    float height = relief(p);
    for (const auto& landmark : landmarks) {
        const auto d = glm::max(glm::max(glm::vec2(landmark.low) - p,
                                       p - glm::vec2(landmark.high)),glm::vec2(0));
        const float mix = std::clamp(1.f - (glm::length(d) - 1.f) / 6.f,0.f,1.f);
        height += (base(landmark) - height) * mix;
    }
    if (distanceTo(p,bridge.start,bridge.end) < 8.f && riverDistance >= riverWidth * .60f)
        height = 7.f;
    const float bank = std::clamp((riverDistance - riverWidth*.42f) / 5.f,0.f,1.f);
    return static_cast<int>(std::round(2.f + (height - 2.f) * bank));
}

void box(World& world, int x0, int y0, int z0, int x1, int y1, int z1, Block block) {
    for (int y = y0; y <= y1; ++y)
        for (int z = z0; z <= z1; ++z)
            for (int x = x0; x <= x1; ++x)
                world.set({x,y,z},block);
}

bool gothic(Building building) {
    return building == Building::university || building == Building::middlesex ||
           building == Building::gothic;
}

void wing(World& world, const Landmark& l) {
    const int y0 = base(l);
    const int x0 = l.low.x, x1 = l.high.x, z0 = l.low.y, z1 = l.high.y;
    const int entry = (x0 + x1) / 2;
    const bool stone = gothic(l.type), library = l.type == Building::library;
    const bool residential = l.type == Building::residence;
    const auto walls = stone ? Block::sandstone : residential ? Block::brick : Block::concrete;
    const auto trim = stone || library ? Block::limestone : Block::sandstone;
    for (int y = 1; y <= l.height; ++y) {
        for (int z = z0; z <= z1; ++z) {
            for (int x = x0; x <= x1; ++x) {
                if (x != x0 && x != x1 && z != z0 && z != z1) continue;
                const bool longWall = z == z0 || z == z1;
                const int across = longWall ? x-x0 : z-z0;
                const int span = longWall ? x1-x0 : z1-z0;
                const bool corner = across < 1 || across > span-1;
                const bool door = z == z1 && std::abs(x-entry) <= 1 && y <= 3;
                if (door) continue;
                const bool floorBand = (y-1) % 4 == 0;
                const bool window = !floorBand && y > 2 && y < l.height &&
                    !corner && (stone ? across%5 == 2 || across%5 == 3 :
                                library ? across%3 == 1 : across%4 >= 1);
                auto block = walls;
                if (corner || y == l.height || (floorBand && !residential)) block = trim;
                if (window) {
                    const bool conron = l.type == Building::university && z == z1 && x < 67;
                    block = conron ? Block::stainedGlass : Block::glass;
                }
                world.set({x,y0+y,z},block);
            }
        }
    }
    const int roof = y0 + l.height + 1;
    if (stone || residential) {
        for (int z = z0 - 1; z <= z1 + 1; ++z) {
            const int rise = std::min(z-(z0-1),(z1+1)-z) / 2;
            box(world,x0,roof,z,x0,roof+rise-1,z,walls);
            box(world,x1,roof,z,x1,roof+rise-1,z,walls);
            box(world,x0-1,roof+rise,z,x1+1,roof+rise,z,Block::slate);
        }
    } else {
        box(world,x0-1,roof,z0-1,x1+1,roof,z1+1,trim);
        box(world,x0+2,roof+1,z0+2,x1-2,roof+1,z1-2,Block::slate);
        if (library)
            for (int x = x0+1; x < x1; x += 3)
                box(world,x,y0+3,z1+1,x,roof-1,z1+1,Block::limestone);
    }
    box(world,entry-2,y0,z1+1,entry+2,y0,z1+2,Block::limestone);
}

void tower(World& world, int cx, int cz, int y0, int height, bool clock) {
    for (int y = 1; y <= height; ++y) {
        for (int z = -4; z <= 4; ++z) {
            for (int x = -4; x <= 4; ++x) {
                const int outer = std::max(std::abs(x),std::abs(z));
                if (outer != 4) continue;
                const bool edge = std::abs(x) == 4 && std::abs(z) == 4;
                const int center = std::abs(x) == 4 ? z : x;
                auto block = Block::sandstone;
                if (edge || y == 5 || y == 14 || y == height-1) block = Block::limestone;
                if (!edge && std::abs(center) <= 1 && ((y >= 8 && y <= 12) ||
                                                       (!clock && y >= height-7 && y <= height-3)))
                    block = Block::glass;
                const int dialY = y - (height-5);
                if (clock && std::abs(center) <= 2 && std::abs(dialY) <= 2 &&
                    std::abs(center) + std::abs(dialY) < 4) {
                    const bool hands = (center == 0 && dialY >= 0) || (dialY == 0 && center == 1);
                    block = hands ? Block::slate : Block::limestone;
                }
                if (z == 4 && std::abs(x) <= 1 && y <= 4) block = Block::air;
                world.set({cx+x,y0+y,cz+z},block);
            }
        }
    }
    box(world,cx-5,y0+height,cz-5,cx+5,y0+height,cz+5,Block::limestone);
    if (clock) {
        for (int i = 0; i <= 4; ++i)
            box(world,cx-4+i,y0+height+1+i*2,cz-4+i,cx+4-i,y0+height+2+i*2,cz+4-i,Block::copper);
        world.set({cx,y0+height+11,cz},Block::limestone);
    } else {
        for (int x = -5; x <= 5; ++x) {
            for (int z = -5; z <= 5; ++z) {
                if (std::max(std::abs(x),std::abs(z)) != 5) continue;
                const int across = std::abs(z) == 5 ? x : z;
                if (std::abs(across) % 3 != 0 || std::abs(across) == 5)
                    world.set({cx+x,y0+height+1,cz+z},Block::limestone);
                if (std::abs(x) == 5 && std::abs(z) == 5)
                    world.set({cx+x,y0+height+2,cz+z},Block::limestone);
            }
        }
        for (int x : {-3,3})
            box(world,cx+x,y0+7,cz+5,cx+x,y0+11,cz+5,Block::stainedGlass);
    }
}

void buildBridge(World& world) {
    const auto direction = glm::normalize(bridge.end - bridge.start);
    const glm::vec2 side{-direction.y,direction.x};
    const float length = glm::length(bridge.end - bridge.start);
    for (int z = 100; z <= 130; ++z) {
        for (int x = 130; x <= 178; ++x) {
            const auto p = glm::vec2(x,z) - bridge.start;
            const float t = glm::dot(p,direction), offset = std::abs(glm::dot(p,side));
            if (t < -2.f || t > length + 1.f || offset > bridge.width*.5f) continue;
            const bool rim = offset > bridge.width*.5f-1.0f;
            world.set({x,7,z},rim ? Block::limestone : Block::pavement);
            if (rim && t > 2 && t < length-2) world.set({x,8,z},Block::sandstone);
            if (std::abs(t-13.f) < 1 || std::abs(t-27.f) < 1)
                box(world,x,2,z,x,6,z,Block::sandstone);
        }
    }
}

std::uint32_t hash(int x, int z) {
    auto n = static_cast<std::uint32_t>(x)*1597334677u ^ static_cast<std::uint32_t>(z)*3812015801u;
    n ^= n >> 16;
    n *= 0x7feb352du;
    return n ^ (n >> 15);
}

void tree(World& world, int x, int z, int y, int height, Block leaf, bool birch) {
    for (int layer = height-3; layer <= height+1; ++layer) {
        const int radius = layer == height+1 ? 1 : layer == height-3 ? 1 : 2;
        for (int dz = -radius; dz <= radius; ++dz)
            for (int dx = -radius; dx <= radius; ++dx)
                if (std::abs(dx)+std::abs(dz) <= radius+1 &&
                    world.get({x+dx,y+layer,z+dz}) == Block::air)
                    world.set({x+dx,y+layer,z+dz},leaf);
    }
    box(world,x,y,z,x,y+height-1,z,birch ? Block::birch : Block::cedar);
}

} // namespace

World makeCampus() {
    World world;
    std::array<int,worldSize*worldSize> heights{};
    for (int z = 0; z < worldSize; ++z) {
        for (int x = 0; x < worldSize; ++x) {
            const glm::vec2 p{x,z};
            const float riverDistance = distanceToRiver(p);
            const int height = terrainHeight(p,riverDistance);
            heights[z*worldSize+x] = height;
            Block top = height <= waterLevel + 1 ? Block::sand : Block::grass;
            if (height > waterLevel) {
                for (const auto& path : campusPaths)
                    if (distanceTo(p,path.start,path.end) <= path.width/2) top = path.surface;
                for (const auto& path : hillPaths)
                    if (distanceTo(p,path.start,path.end) <= path.width/2) top = path.surface;
                if (x >= 46 && x <= 53 && z >= 58 && z <= 75) top = Block::concrete;
                if (x >= 69 && x <= 85 && z >= 89 && z <= 96) top = Block::limestone;
            }
            for (int y = 0; y <= std::max(height,waterLevel); ++y) {
                Block block = y > height ? Block::water : y == height ? top :
                              y >= height-2 ? Block::earth : Block::shale;
                world.set({x,y,z},block);
            }
        }
    }

    for (const auto& landmark : landmarks) wing(world,landmark);
    tower(world,76,88,base(landmarks[0]),25,false);
    tower(world,119,85,base(landmarks[1]),19,true);
    buildBridge(world);

    for (int z = 5; z < worldSize - 4; z += 6) {
        for (int x = 5; x < worldSize - 4; x += 6) {
            const auto random = hash(x,z);
            if (random % 7 < 2) continue;
            const int tx = x + int(random % 4), tz = z + int((random >> 8) % 4);
            const int y = heights[tz*worldSize+tx];
            if (world.get({tx,y,tz}) != Block::grass) continue;
            if (tx >= 55 && tx <= 123 && tz >= 83 && tz <= 134) continue;
            if (tx >= 103 && tx <= 155 && tz >= 78 && tz <= 114) continue;
            const auto treeNear = [&](const Landmark& l) {
                return tx >= l.low.x-3 && tx <= l.high.x+3 && tz >= l.low.y-3 && tz <= l.high.y+3;
            };
            if (std::any_of(landmarks.begin(),landmarks.end(),treeNear)) continue;
            const Block leaf = random%5 == 0 ? Block::goldLeaves :
                               random%5 == 1 ? Block::redLeaves : Block::leaves;
            tree(world,tx,tz,y+1,4+int((random>>4)%4),leaf,random%4 == 0);
        }
    }
    for (const glm::ivec2 p : {glm::ivec2(83,109), {116,109}, {64,95}, {98,93}}) {
        const int y = heights[p.y*worldSize+p.x];
        tree(world,p.x,p.y,y+1,5,p.x%2 ? Block::goldLeaves : Block::redLeaves,p.x%2 != 0);
    }
    for (const glm::ivec2 p : {glm::ivec2(100,113), {85,109}, {72,98}, {80,98}, {125,112},{141,111}}) {
        const int y = heights[p.y*worldSize+p.x];
        box(world,p.x,y+1,p.y,p.x,y+2,p.y,Block::cedar);
        world.set({p.x,y+3,p.y},Block::lantern);
    }
    return world;
}

} // namespace ql
