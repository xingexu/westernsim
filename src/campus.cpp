#include "campus.hpp"

#include <glm/geometric.hpp>
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace wsim {
namespace {

constexpr int waterLevel = 4;
constexpr glm::ivec2 middlesexTower{119,85};
constexpr int middlesexTowerHeight = 22;

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
    if (distanceTo(p,bridge.start,bridge.end) < 6.5f && riverDistance >= riverWidth * .60f)
        height = 8.f;
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
    const bool stone = gothic(l.type);
    const bool residential = l.type == Building::residence;
    const bool brick = residential || l.type == Building::gallery;
    const auto walls = stone ? Block::sandstone : brick ? Block::brick : Block::concrete;
    const auto trim = stone || brick ? Block::limestone : Block::sandstone;
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
                const bool window = !floorBand && y > 2 && y < l.height && !corner &&
                                    (stone ? across%5 == 2 || across%5 == 3 : across%4 >= 1);
                auto block = walls;
                if (corner || y == l.height || (floorBand && !residential)) block = trim;
                if (window) block = Block::glass;
                world.set({x,y0+y,z},block);
            }
        }
    }
    const int roof = y0 + l.height + 1;
    if (stone || brick) {
        for (int z = z0 - 1; z <= z1 + 1; ++z) {
            const int rise = std::min(z-(z0-1),(z1+1)-z) / 2;
            box(world,x0,roof,z,x0,roof+rise-1,z,walls);
            box(world,x1,roof,z,x1,roof+rise-1,z,walls);
            box(world,x0-1,roof+rise,z,x1+1,roof+rise,z,Block::slate);
        }
    } else {
        box(world,x0-1,roof,z0-1,x1+1,roof,z1+1,trim);
        box(world,x0+2,roof+1,z0+2,x1-2,roof+1,z1-2,Block::slate);
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
                if (clock && std::abs(center) <= 1 && std::abs(dialY) <= 1) block = Block::clock;
                if (z == 4 && std::abs(x) <= 1 && y <= 4) block = Block::air;
                world.set({cx+x,y0+y,cz+z},block);
                if (!edge && std::abs(center) <= 1 && y == height-2 && !clock)
                    world.set({cx+x,y0+y,cz+z},Block::lancet);
            }
        }
    }
    box(world,cx-5,y0+height,cz-5,cx+5,y0+height,cz+5,Block::limestone);
    if (clock) {
        const int top = y0+height;
        for (int x : {-4,4}) {
            for (int z : {-4,4}) {
                box(world,cx+x-1,top+1,cz+z-1,cx+x+1,top+2,cz+z+1,Block::limestone);
                box(world,cx+x,top+3,cz+z,cx+x,top+4,cz+z,Block::sandstone);
            }
        }
        for (int i = 0; i <= 3; ++i)
            box(world,cx-3+i,top+1+i*3,cz-3+i,cx+3-i,top+3+i*3,cz+3-i,Block::copper);
        box(world,cx,top+13,cz,cx,top+15,cz,Block::iron);
        world.set({cx,top+16,cz},Block::lantern);
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
        box(world,cx,y0+height+1,cz,cx,y0+height+7,cz,Block::iron);
        for (int x = 1; x <= 5; ++x)
            box(world,cx+x,y0+height+5,cz,cx+x,y0+height+6,cz,
                x == 1 || x == 5 || x == 3 ? Block::flagRed : Block::flagWhite);
    }
}

void universityCollege(World& world, const Landmark& l) {
    wing(world,l);
    const int y = base(l), front = l.high.y;
    // Conron Hall's south elevation has a row of tall, pointed stained-glass bays.
    for (int x = l.low.x + 2; x <= 69; x += 4) {
        for (int pane = x; pane <= x+2; ++pane) {
            box(world,pane,y+5,front,pane,y+11,front,Block::stainedGlass);
            box(world,pane,y+4,front+1,pane,y+4,front+1,Block::limestone);
        }
        world.set({x+1,y+12,front},Block::lancet);
        box(world,x-1,y+2,front+1,x-1,y+13,front+1,Block::sandstone);
        world.set({x-1,y+14,front+1},Block::limestone);
    }
    for (const int end : {l.low.x, l.high.x-3}) {
        box(world,end,y+1,front+1,end+3,y+13,front+2,Block::sandstone);
        box(world,end,y+13,front,end+3,y+13,front+3,Block::limestone);
        for (int x : {end,end+3}) world.set({x,y+14,front+3},Block::limestone);
        for (int storey : {3,7,10})
            box(world,end+1,y+storey,front+2,end+2,y+storey+1,front+2,Block::glass);
    }
    tower(world,76,88,y,25,false);
    // Extend the entry stairs and keep the doorway itself traversable.
    for (int x : {74,78}) box(world,x,y+1,93,x,y+4,93,Block::limestone);
    box(world,74,y+5,93,78,y+5,93,Block::limestone);
    world.set({76,y+6,93},Block::stainedGlass);
    box(world,72,y,92,80,y,94,Block::limestone);
    box(world,73,y-1,95,79,y-1,95,Block::limestone);
    box(world,74,y-2,96,78,y-2,96,Block::limestone);
}

void weldon(World& world, const Landmark& landmark) {
    const int floor = base(landmark);
    const int x0 = landmark.low.x, x1 = landmark.high.x;
    const int z0 = landmark.low.y, z1 = landmark.high.y;
    for (int storey = 0; storey < 5; ++storey) {
        const int setback = storey / 2;
        const int y = floor+1+storey*3;
        for (int z = z0+setback; z <= z1-setback; ++z) {
            for (int x = x0+setback; x <= x1-setback; ++x) {
                if (x != x0+setback && x != x1-setback && z != z0+setback && z != z1-setback)
                    continue;
                box(world,x,y,z,x,y+2,z,Block::concrete);
                world.set({x,y+1,z},Block::glass);
                const int outX = (x == x0+setback ? -1 : (x == x1-setback ? 1 : 0));
                const int outZ = (z == z0+setback ? -1 : (z == z1-setback ? 1 : 0));
                world.set({x+outX,y+2,z+outZ},Block::limestone);
                if (storey == 4) world.set({x,y+3,z},Block::concrete);
            }
        }
    }
    box(world,x0+2,floor+16,z0+2,x1-2,floor+16,z1-2,Block::slate);
    for (int x = x0+4; x <= x1-4; x += 4) {
        box(world,x,floor+17,z0+4,x+1,floor+17,z1-4,Block::glass);
        box(world,x,floor+18,z0+4,x,floor+18,z1-4,Block::concrete);
    }
    for (const auto x : {x0,x1-1})
        for (const auto z : {z0,z1-1})
            box(world,x,floor+1,z,x+1,floor+17,z+1,Block::concrete);
    const int entry = (x0+x1)/2;
    box(world,entry-2,floor+1,z0,entry+2,floor+3,z0,Block::air);
    box(world,entry-3,floor+4,z0-2,entry+3,floor+4,z0,Block::concrete);
}

void communityCentre(World& world, const Landmark& landmark) {
    wing(world,landmark);
    const int y = base(landmark), front = landmark.high.y;
    for (int x = 34; x <= 46; ++x) {
        const auto curtain = x%3 == 0 ? Block::iron : Block::glass;
        box(world,x,y+1,front,x,y+9,front,curtain);
    }
    box(world,39,y+1,front,41,y+3,front,Block::air);
    box(world,36,y+4,front+1,44,y+4,front+3,Block::slate);
    const int plaza = base(landmarks.back());
    // Concrete Beach pavilion, retaining-wall seating and planting beds.
    for (int x : {31,37}) for (int z : {71,75})
        box(world,x,plaza+1,z,x,plaza+3,z,Block::iron);
    box(world,30,plaza+4,70,38,plaza+4,76,Block::slate);
    box(world,31,plaza+5,71,37,plaza+5,75,Block::copper);
    box(world,42,plaza+1,75,49,plaza+1,75,Block::limestone);
    box(world,43,plaza+2,75,48,plaza+2,75,Block::planks);
    box(world,43,plaza+1,72,47,plaza+1,72,Block::flowers);
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
            world.set({x,8,z},rim ? Block::limestone : Block::pavement);
            if (rim && t > 2 && t < length-2) world.set({x,9,z},Block::iron);
            if (t > 10 && t < 31) {
                const float fromPier = std::min(std::abs(t-11.f),std::abs(t-29.f));
                const int rise = std::min(3,static_cast<int>(fromPier * .6f));
                box(world,x,4+rise,z,x,7,z,Block::concrete);
                if (fromPier < 1.4f) box(world,x,2,z,x,4,z,Block::brick);
            }
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

glm::vec3 campusClockCenter() {
    return {middlesexTower.x+.5f,base(landmarks[1])+middlesexTowerHeight-4.5f,middlesexTower.y+.5f};
}

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
                const auto& plaza = landmarks.back();
                if (x >= plaza.low.x && x <= plaza.high.x && z >= plaza.low.y && z <= plaza.high.y)
                    top = Block::concrete;
                if (x >= 69 && x <= 85 && z >= 89 && z <= 96) top = Block::limestone;
            }
            for (int y = 0; y <= std::max(height,waterLevel); ++y) {
                Block block = y > height ? Block::water : y == height ? top :
                              y >= height-2 ? Block::earth : Block::shale;
                world.set({x,y,z},block);
            }
        }
    }

    for (const auto& landmark : landmarks) {
        switch (landmark.type) {
            case Building::university: universityCollege(world,landmark); break;
            case Building::library: weldon(world,landmark); break;
            case Building::community: communityCentre(world,landmark); break;
            case Building::plaza: break;
            default: wing(world,landmark); break;
        }
    }
    tower(world,middlesexTower.x,middlesexTower.y,base(landmarks[1]),middlesexTowerHeight,true);
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
        box(world,p.x,y+1,p.y,p.x,y+2,p.y,Block::iron);
        world.set({p.x,y+3,p.y},Block::lantern);
    }
    for (const glm::ivec2 p : {glm::ivec2(72,95), {80,95}, {85,104}, {70,100}, {96,104}}) {
        const int y = heights[p.y*worldSize+p.x];
        box(world,p.x,y,p.y,p.x+2,y,p.y+1,Block::limestone);
        box(world,p.x,y+1,p.y,p.x+2,y+1,p.y,Block::flowers);
    }
    return world;
}

} // namespace wsim
