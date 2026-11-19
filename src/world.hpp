#pragma once

#include <glm/vec3.hpp>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace wsim {

constexpr int chunkSize = 16;
constexpr int worldHeight = 64;
constexpr int worldSize = 896;

enum class Block : std::uint8_t {
    air, grass, earth, shale, sand, cedar, needles, birch, leaves,
    limestone, brick, planks, water, lantern, sandstone, slate, glass,
    pavement, concrete, copper, goldLeaves, redLeaves, stainedGlass,
    lancet, clock, iron, flagRed, flagWhite, flowers,
    lampPost, bench, planter, canopyGreen, canopyGold, canopyCopper,
    houseWall, academicWall, modernWall, heritageWall, libraryWall, unionWall, residenceWall,
    galleryWall, wildflowers,
    count
};

constexpr bool scenery(Block block) {
    return (block >= Block::lampPost && block <= Block::canopyCopper) || block == Block::wildflowers;
}
constexpr bool canopy(Block block) { return block >= Block::canopyGreen && block <= Block::canopyCopper; }

constexpr bool solid(Block block) {
    return block != Block::air && block != Block::water && !scenery(block);
}

struct Chunk {
    glm::ivec3 origin{};
    std::array<Block, chunkSize * chunkSize * chunkSize> blocks{};
};

class World {
public:
    explicit World(int edge = worldSize);

    int edge() const { return edge_; }
    bool contains(glm::ivec3 p) const;
    Block get(glm::ivec3 p) const;
    bool set(glm::ivec3 p, Block block);
    std::size_t chunkIndex(glm::ivec3 p) const;
    const std::vector<Chunk>& chunks() const { return chunks_; }
    std::vector<std::size_t> affectedChunks(glm::ivec3 p) const;
    glm::vec3 spawn() const;

    bool save(const std::filesystem::path& path, std::string& error) const;
    static std::optional<World> load(const std::filesystem::path& path, std::string& error);

private:
    int edge_;
    std::vector<Chunk> chunks_;
};

struct Hit {
    glm::ivec3 block;
    glm::ivec3 normal;
    float distance;
};

std::optional<Hit> raycast(const World& world, glm::vec3 start, glm::vec3 direction, float reach);

} // namespace wsim
