#include "campus.hpp"
#include "campus_survey.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace wsim {
namespace {

constexpr std::array groundMaterials{
    Block::grass, Block::water, Block::sand, Block::pavement, Block::limestone,
    Block::concrete, Block::parking, Block::limestone, Block::playingField, Block::pavement, Block::grass,
    Block::track, Block::turf, Block::courtSand, Block::pavers
};
constexpr std::array facadeMaterials{
    Block::air, Block::houseWall, Block::academicWall, Block::modernWall, Block::heritageWall,
    Block::heritageWall, Block::libraryWall, Block::unionWall, Block::residenceWall,
    Block::galleryWall, Block::saugeenWall, Block::delawareWall, Block::iveyWall,
    Block::thamesWall, Block::medwayWall, Block::recreationWall, Block::hospitalWall,
    Block::socialWall, Block::naturalWall, Block::physicsWall, Block::westernScienceWall,
    Block::alumniWall, Block::huronWall, Block::elginWall,
    Block::fimsWall, Block::musicWall, Block::engineeringWall
};
constexpr std::array roofMaterials{
    Block::air, Block::slate, Block::gravelRoof, Block::membraneRoof, Block::slate, Block::slate,
    Block::gravelRoof, Block::membraneRoof, Block::membraneRoof, Block::slate, Block::gravelRoof,
    Block::gravelRoof, Block::metalRoof, Block::gravelRoof, Block::slate, Block::membraneRoof,
    Block::membraneRoof, Block::gravelRoof, Block::membraneRoof, Block::gravelRoof, Block::membraneRoof,
    Block::metalRoof, Block::gravelRoof, Block::metalRoof, Block::membraneRoof, Block::metalRoof,
    Block::membraneRoof
};

std::uint32_t hash(int x, int z) {
    std::uint32_t n = static_cast<std::uint32_t>(x)*374761393u +
                      static_cast<std::uint32_t>(z)*668265263u;
    n = (n ^ (n >> 13)) * 1274126177u;
    return n ^ (n >> 16);
}

int roofHeight(const World& world, glm::ivec2 p) {
    for (int y = worldHeight-1; y > 0; --y)
        if (solid(world.get({p.x,y,p.y}))) return y;
    return 0;
}

void box(World& world, glm::ivec3 low, glm::ivec3 high, Block material) {
    for (int y = low.y; y <= high.y; ++y)
        for (int z = low.z; z <= high.z; ++z)
            for (int x = low.x; x <= high.x; ++x)
                world.set({x,y,z},material);
}

void buildTower(World& world, glm::ivec2 at, int crown, bool clock) {
    const int x = at.x, z = at.y;
    const int top = crown - (clock ? 5 : 1);
    constexpr int radius = 2;
    for (int dz = -radius; dz <= radius; ++dz) {
        for (int dx = -radius; dx <= radius; ++dx) {
            const bool edge = std::abs(dx) == radius || std::abs(dz) == radius;
            const bool corner = std::abs(dx) == radius && std::abs(dz) == radius;
            const int across = std::abs(dx) == radius ? dz : dx;
            int foundation = roofHeight(world,{x+dx,z+dz});
            if (world.get({x+dx,foundation,z+dz}) == Block::slate) --foundation;
            while (world.get({x+dx,foundation,z+dz}) == Block::heritageWall) --foundation;
            for (int y = foundation+1; y <= top; ++y) {
                auto block = Block::sandstone;
                if (corner || y == top-4 || y == top) block = Block::limestone;
                if (edge && across == 0 && y >= top-3 && y <= top-1) block = Block::lancet;
                if (clock && edge && std::abs(across) <= 1 && std::abs(y-(top-3)) <= 1)
                    block = Block::clock;
                if (dx == radius && dz == 0 && y <= foundation+2) block = Block::cedar;
                world.set({x+dx,y,z+dz},block);
            }
        }
    }
    if (clock) {
        box(world,{x-1,top+1,z-1},{x+1,top+2,z+1},Block::copper);
        box(world,{x,top+3,z},{x,crown,z},Block::copper);
        for (const int dx : {-radius,radius})
            for (const int dz : {-radius,radius})
                box(world,{x+dx,top+1,z+dz},{x+dx,top+2,z+dz},Block::limestone);
        world.set({x,crown+1,z},Block::iron);
        return;
    }
    for (const int offset : {-radius,0,radius}) {
        world.set({x+offset,top+1,z-radius},Block::limestone);
        world.set({x+offset,top+1,z+radius},Block::limestone);
        world.set({x-radius,top+1,z+offset},Block::limestone);
        world.set({x+radius,top+1,z+offset},Block::limestone);
    }
    box(world,{x,top+1,z},{x,top+3,z},Block::iron);
    box(world,{x+1,top+3,z},{x+2,top+3,z},Block::flagWhite);
    world.set({x+1,top+3,z},Block::flagRed);
}

void finishExterior(World& world) {
    // Conron Hall's tall windows and the McIntosh Gallery's red entry.
    for (const auto landmark : {landmarks[0],landmarks[5]}) {
        const bool hall = landmark.position == landmarks[0].position;
        const auto p = landmark.position;
        const auto material = hall ? Block::heritageWall : Block::galleryWall;
        for (int x = p.x-(hall ? 15 : 3); x <= p.x+(hall ? 8 : 3); ++x) {
            for (int z = p.y-4; z <= p.y+18; ++z) {
                for (int y = 2; y < worldHeight-1; ++y) {
                    if (world.get({x,y,z}) != material || world.get({x,y,z+1}) != Block::air ||
                        world.get({x,y+1,z}) != material) continue;
                    const auto below = world.get({x,y-1,z});
                    if (hall && x%4 != 1 && x%4 != 2) continue;
                    if (!hall && (std::abs(x-p.x) > 1 || below == material)) continue;
                    if (hall && below == material && world.get({x,y-2,z}) == material)
                        world.set({x,y,z},Block::stainedGlass);
                    if (!hall && below != material) {
                        world.set({x,y,z},Block::flagRed);
                        world.set({x,y+1,z},Block::flagRed);
                    }
                }
            }
        }
    }

    const auto beach = landmarks[4].position;
    for (const auto offset : {glm::ivec2{-3,1},glm::ivec2{3,2},glm::ivec2{7,0}}) {
        const auto p = beach + offset;
        const int y = roofHeight(world,p);
        if (world.get({p.x,y,p.y}) == Block::concrete)
            world.set({p.x,y+1,p.y},Block::bench);
    }
}

} // namespace

glm::vec3 campusClockCenter(const World& world) {
    const int x = middlesexTower.x, z = middlesexTower.y;
    for (int y = worldHeight-1; y >= 0; --y)
        if (world.get({x,y,z+2}) == Block::clock) return {x+.5f,y-.5f,z+.5f};
    return {};
}

bool landmarkContainsColumn(const Landmark& landmark, glm::ivec2 column) {
    const auto* end = survey::landmarkSpans + landmark.endSpan;
    const auto* row = std::lower_bound(survey::landmarkSpans+landmark.firstSpan,end,column.y,
        [](const survey::LandmarkSpan& span, int z) { return span.z < z; });
    for (; row != end && row->z == column.y; ++row) {
        if (column.x >= row->firstX && column.x < row->endX) return true;
    }
    return false;
}

World makeCampus() {
    static_assert(worldSize == surveyEdge);
    World world;
    int x = 0, z = 0;
    for (const auto run : survey::columns) {
        const int ground = static_cast<int>(run & 63);
        const auto surface = (run >> 6) & 15;
        const int roof = static_cast<int>((run >> 10) & 63);
        const auto facade = (run >> 16) & 31;
        const auto top = groundMaterials[surface];
        const int count = static_cast<int>(run >> 21) + 1;
        for (int i = 0; i < count; ++i) {
            // A closed terrain shell keeps the voxel surface and its collisions aligned.
            const auto earth = top == Block::water ? Block::sand : Block::earth;
            for (int y = std::max(0,ground-5); y < ground; ++y) world.set({x,y,z},earth);
            world.set({x,ground,z},top);
            if (facade) {
                const auto wall = facadeMaterials[facade];
                for (int y = ground+1; y < roof; ++y) world.set({x,y,z},wall);
                world.set({x,roof,z},roofMaterials[facade]);
            } else if (surface == 0 && hash(x,z)%1000 < 5) {
                world.set({x,ground+1,z},Block::wildflowers);
            }
            if (++x == worldSize) {
                x = 0;
                ++z;
            }
        }
    }

    for (const auto bridge : survey::bridges) {
        const int bx = static_cast<int>(bridge & 1023);
        const int bz = static_cast<int>((bridge >> 10) & 1023);
        const int y = static_cast<int>(bridge >> 20);
        world.set({bx,y,bz},Block::pavement);
    }

    for (const auto tree : survey::trees) {
        const int tx = static_cast<int>(tree & 1023);
        const int tz = static_cast<int>((tree >> 10) & 1023);
        const int y = static_cast<int>((tree >> 20) & 63);
        const auto variant = (tree >> 26);
        const auto foliage = variant == 4 ? Block::canopyGold :
                             variant == 5 ? Block::canopyCopper : Block::canopyGreen;
        world.set({tx,y+1,tz},foliage);
    }

    buildTower(world,memorialTower,memorialTowerTop,false);
    buildTower(world,middlesexTower,middlesexTowerTop,true);
    finishExterior(world);
    return world;
}

} // namespace wsim
