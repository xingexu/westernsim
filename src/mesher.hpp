#pragma once

#include "world.hpp"
#include <vector>

namespace ql {

struct Vertex {
    glm::vec3 position;
    glm::vec3 normal;
    float material;
};

struct Mesh {
    std::vector<Vertex> vertices;
    std::vector<std::uint32_t> indices;
    std::size_t triangles() const { return indices.size() / 3; }
};

enum class Meshing { exposed, greedy };
Mesh meshChunk(const World& world, const Chunk& chunk, Meshing mode = Meshing::greedy);

} // namespace ql
