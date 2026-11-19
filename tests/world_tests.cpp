#include "mesher.hpp"
#include "player.hpp"
#include "campus.hpp"

#include <glm/geometric.hpp>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {

using namespace wsim;

void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void meshing() {
    World world(32);
    const auto counts = [&](glm::ivec3 p, std::size_t exposed, std::size_t greedy) {
        const auto& chunk = world.chunks()[world.chunkIndex(p)];
        check(meshChunk(world, chunk, Meshing::exposed).triangles() == exposed, "exposed triangles");
        check(meshChunk(world, chunk).triangles() == greedy, "greedy triangles");
    };
    counts({0,0,0}, 0, 0);
    world.set({4,4,4}, Block::grass);
    counts({0,0,0}, 12, 12);
    world.set({5,4,4}, Block::grass);
    counts({0,0,0}, 20, 12);
    world.set({5,4,4}, Block::limestone);
    counts({0,0,0}, 20, 20);
    world.set({15,2,2}, Block::shale);
    world.set({16,2,2}, Block::shale);
    counts({0,0,0}, 30, 30);
    counts({16,0,0}, 10, 10);
    check(world.affectedChunks({15,2,2}).size() == 2, "a boundary edit must remesh both sides");
    check(world.affectedChunks({15,15,15}).size() == 4, "only face neighbors need remeshing");
    world.set({16,2,2}, Block::air);
    counts({0,0,0}, 32, 32);
    counts({16,0,0}, 0, 0);

    const auto mesh = meshChunk(world, world.chunks().front());
    for (std::size_t i = 0; i < mesh.indices.size(); i += 3) {
        const auto& a = mesh.vertices[mesh.indices[i]];
        const auto& b = mesh.vertices[mesh.indices[i + 1]];
        const auto& c = mesh.vertices[mesh.indices[i + 2]];
        check(glm::dot(glm::cross(b.position - a.position, c.position - a.position), a.normal) > 0,
              "triangles must wind outward");
    }

    World shoreline(16);
    shoreline.set({4,4,4}, Block::water);
    shoreline.set({5,4,4}, Block::shale);
    check(meshChunk(shoreline, shoreline.chunks().front()).triangles() == 22, "water interfaces");

    World fixture(32);
    fixture.set({15,0,10},Block::limestone);
    fixture.set({15,1,10},Block::bench);
    const auto detail = meshChunk(fixture,fixture.chunks().front());
    check(detail.triangles() == 156, "a decorative anchor emits bench geometry and keeps the floor top");
    const auto hit = raycast(fixture,{15.5f,2.5f,10.5f},{0,-1,0},3.f);
    check(hit && hit->block.y == 0 && !solid(Block::bench), "fixtures stay out of voxel picking and collision");
}

void movementAndEditing() {
    World world(16);
    for (int z = 0; z < 16; ++z)
        for (int x = 0; x < 16; ++x)
            world.set({x,0,z}, Block::grass);
    world.set({5,1,4}, Block::shale);
    world.set({5,2,4}, Block::shale);
    Player player;
    player.feet = {3.5f, 4.f, 4.5f};
    for (int i = 0; i < 120; ++i) player.step(world, {}, false, false);
    check(player.grounded && std::abs(player.feet.y - 1.f) < .001f, "landing on the floor");
    for (int i = 0; i < 120; ++i) player.step(world, {1,0,0}, false, false);
    check(std::abs(player.feet.x - 4.7f) < .001f, "wall collision");
    player.step(world, {}, false, true);
    check(!player.grounded && player.feet.y > 1.f, "jump begins on the ground");
    for (int i = 0; i < 120; ++i) player.step(world, {}, false, false);
    check(player.grounded, "land after jumping");
    const auto hit = raycast(world, {3.5f,1.5f,4.5f}, {2,0,0}, 7);
    check(hit && hit->block == glm::ivec3(5,1,4) && hit->normal == glm::ivec3(-1,0,0),
          "ray finds the first block and the placement face");
    check(!raycast(world, {3.5f,1.5f,4.5f}, {1,0,0}, 1), "ray respects reach");
    check(player.overlaps({4,1,4}) && !player.overlaps({5,1,4}), "placement protects the avatar");
}

void persistence() {
    const auto path = std::filesystem::current_path() / "westernsim-test.qlw";
    World world(32);
    std::string error;
    world.set({31,63,31}, Block::brick);
    check(world.save(path, error), "world saves successfully");
    world.set({16,3,7}, Block::cedar);
    check(world.save(path, error), "saving again replaces the previous snapshot");
    const auto loaded = World::load(path, error);
    check(loaded && loaded->edge() == 32 && loaded->get({31,63,31}) == Block::brick &&
          loaded->get({16,3,7}) == Block::cedar, "save round-trip preserves edits");
    std::filesystem::resize_file(path, 10);
    check(!World::load(path, error), "truncated data is rejected");
    std::filesystem::remove(path);
}

void campus() {
    const auto world = makeCampus();
    const auto spawn = world.spawn();
    check(world.edge() == 192 && world.chunks().size() == 576, "fixed benchmark campus dimensions");
    check(spawn.x == campusSpawn.x + .5f && spawn.z == campusSpawn.y + .5f, "start at UC Hill");
    check(world.get({153,4,115}) == Block::water &&
          world.get({153,8,115}) == Block::pavement &&
          world.get({153,9,115}) == Block::air, "University Bridge has a walkable deck over the Thames");
    const auto tower = raycast(world,{76.5f,63.f,88.5f},{0,-1,0},63.f);
    const auto spire = raycast(world,{119.5f,63.f,85.5f},{0,-1,0},63.f);
    check(tower && tower->block.y > 37 && spire && spire->block.y > 37, "both tower silhouettes");
    check(world.get({60,22,89}) == Block::stainedGlass, "Conron Hall windows");
    const auto plaza = raycast(world,{49.5f,63.f,71.5f},{0,-1,0},63.f);
    check(plaza && world.get(plaza->block) == Block::concrete, "Concrete Beach plaza");
    const auto residence = raycast(world,{6.5f,63.f,170.5f},{0,-1,0},63.f);
    check(residence && world.get(residence->block) == Block::slate, "Ontario Hall has a flat residence wing");
}

} // namespace

int main() {
    try {
        meshing();
        movementAndEditing();
        persistence();
        campus();
        std::cout << "Meshing, movement, editing, and persistence checks passed.\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
