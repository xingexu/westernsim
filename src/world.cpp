#include "world.hpp"
#include "campus.hpp"

#include <glm/geometric.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <limits>
#include <random>
#include <stdexcept>

namespace ql {
namespace {

constexpr std::array<char, 8> saveHeader{'Q','L','W','O','R','L','D',1};
int blockIndex(glm::ivec3 p) {
    const auto local = p % chunkSize;
    return local.x + chunkSize * (local.z + chunkSize * local.y);
}

} // namespace

World::World(int edge) : edge_(edge) {
    if (edge < chunkSize || edge > 256 || edge % chunkSize != 0)
        throw std::invalid_argument("World edge must be a multiple of 16 from 16 to 256");
    for (int y = 0; y < worldHeight; y += chunkSize)
        for (int z = 0; z < edge; z += chunkSize)
            for (int x = 0; x < edge; x += chunkSize)
                chunks_.push_back(Chunk{{x, y, z}, {}});
}

bool World::contains(glm::ivec3 p) const {
    return p.x >= 0 && p.x < edge_ && p.y >= 0 && p.y < worldHeight && p.z >= 0 && p.z < edge_;
}

std::size_t World::chunkIndex(glm::ivec3 p) const {
    const auto c = p / chunkSize;
    const int span = edge_ / chunkSize;
    return static_cast<std::size_t>(c.x + span * (c.z + span * c.y));
}

Block World::get(glm::ivec3 p) const {
    return contains(p) ? chunks_[chunkIndex(p)].blocks[blockIndex(p)] : Block::air;
}

bool World::set(glm::ivec3 p, Block block) {
    if (!contains(p) || block >= Block::count) return false;
    auto& cell = chunks_[chunkIndex(p)].blocks[blockIndex(p)];
    if (cell == block) return false;
    cell = block;
    return true;
}

std::vector<std::size_t> World::affectedChunks(glm::ivec3 p) const {
    if (!contains(p)) return {};
    std::vector<std::size_t> result{chunkIndex(p)};
    for (int axis = 0; axis < 3; ++axis) {
        auto neighbor = p;
        const int local = p[axis] % chunkSize;
        if (local == 0) --neighbor[axis];
        else if (local == chunkSize - 1) ++neighbor[axis];
        else continue;
        if (contains(neighbor)) result.push_back(chunkIndex(neighbor));
    }
    return result;
}

glm::vec3 World::spawn() const {
    const int x = edge_ == worldSize ? campusSpawn.x : edge_ / 2;
    const int z = edge_ == worldSize ? campusSpawn.y : edge_ / 2;
    for (int y = worldHeight - 1; y >= 0; --y)
        if (solid(get({x,y,z}))) return {x + .5f, y + 1.f, z + .5f};
    return {x + .5f, 6.f, z + .5f};
}

bool World::save(const std::filesystem::path& path, std::string& error) const {
    auto temporary = path;
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    temporary += ".tmp-" + std::to_string(stamp) + "-" + std::to_string(std::random_device{}());
    std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
    out.write(saveHeader.data(), saveHeader.size());
    out.put(static_cast<char>(edge_ / chunkSize));
    for (const auto& chunk : chunks_)
        out.write(reinterpret_cast<const char*>(chunk.blocks.data()), chunk.blocks.size());
    out.close();
    if (!out) {
        error = "Could not write save data";
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        return false;
    }
    std::error_code ec;
    std::filesystem::rename(temporary, path, ec);
    if (ec) {
        error = "Could not replace the save file";
        std::filesystem::remove(temporary, ec);
        return false;
    }
    return true;
}

std::optional<World> World::load(const std::filesystem::path& path, std::string& error) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        error = "No readable save file";
        return std::nullopt;
    }
    std::array<char, saveHeader.size()> header{};
    in.read(header.data(), header.size());
    const int span = in.get();
    if (header != saveHeader || span < 1 || span > 16) {
        error = "Unrecognized world format";
        return std::nullopt;
    }
    World world(span * chunkSize);
    for (auto& chunk : world.chunks_) {
        in.read(reinterpret_cast<char*>(chunk.blocks.data()), chunk.blocks.size());
        if (!in || std::any_of(chunk.blocks.begin(), chunk.blocks.end(),
                              [](Block b) { return b >= Block::count; })) {
            error = "Damaged world data";
            return std::nullopt;
        }
    }
    if (in.peek() != std::char_traits<char>::eof() || in.bad()) {
        error = "Unexpected data after the world";
        return std::nullopt;
    }
    return world;
}

std::optional<Hit> raycast(const World& world, glm::vec3 start, glm::vec3 direction, float reach) {
    if (glm::dot(direction, direction) < 1e-12f || reach < 0) return std::nullopt;
    direction = glm::normalize(direction);
    glm::ivec3 cell{std::floor(start.x), std::floor(start.y), std::floor(start.z)};
    glm::ivec3 step{};
    glm::vec3 next{}, delta{};
    for (int axis = 0; axis < 3; ++axis) {
        if (std::abs(direction[axis]) < 1e-8f) {
            next[axis] = delta[axis] = std::numeric_limits<float>::infinity();
            continue;
        }
        step[axis] = direction[axis] > 0 ? 1 : -1;
        const float boundary = static_cast<float>(cell[axis] + (step[axis] > 0 ? 1 : 0));
        next[axis] = (boundary - start[axis]) / direction[axis];
        delta[axis] = std::abs(1.f / direction[axis]);
    }
    float distance = 0.f;
    glm::ivec3 normal{};
    while (distance <= reach) {
        if (solid(world.get(cell))) return Hit{cell, normal, distance};
        int axis = next.x < next.y ? 0 : 1;
        if (next.z < next[axis]) axis = 2;
        distance = next[axis];
        next[axis] += delta[axis];
        cell[axis] += step[axis];
        normal = {};
        normal[axis] = -step[axis];
    }
    return std::nullopt;
}

} // namespace ql
