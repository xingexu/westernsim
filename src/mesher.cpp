#include "mesher.hpp"
#include "scenery.hpp"

#include <algorithm>
#include <array>

namespace wsim {
namespace {

struct Face {
    Block block = Block::air;
    int sign = 0;
    bool operator==(Face other) const { return block == other.block && sign == other.sign; }
};

bool exposed(Block block, Block neighbor) {
    return block != Block::air && !scenery(block) && (neighbor == Block::air || scenery(neighbor) ||
                                  (solid(block) && neighbor == Block::water));
}

void quad(Mesh& mesh, glm::vec3 p, glm::vec3 u, glm::vec3 v, glm::vec3 normal, Block block) {
    const auto base = static_cast<std::uint32_t>(mesh.vertices.size());
    const auto material = static_cast<float>(block);
    mesh.vertices.push_back({p, normal, material});
    mesh.vertices.push_back({p + u, normal, material});
    mesh.vertices.push_back({p + u + v, normal, material});
    mesh.vertices.push_back({p + v, normal, material});
    mesh.indices.insert(mesh.indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
}

} // namespace

Mesh meshChunk(const World& world, const Chunk& chunk, Meshing mode) {
    Mesh mesh;
    if (std::all_of(chunk.blocks.begin(), chunk.blocks.end(), [](Block b) { return b == Block::air; }))
        return mesh;

    constexpr int side = chunkSize;
    std::array<Face, side * side> mask{};
    for (int axis = 0; axis < 3; ++axis) {
        const int u = (axis + 1) % 3;
        const int v = (axis + 2) % 3;
        glm::ivec3 step{};
        step[axis] = 1;
        for (int slice = 0; slice <= side; ++slice) {
            for (int j = 0; j < side; ++j) {
                for (int i = 0; i < side; ++i) {
                    glm::ivec3 p = chunk.origin;
                    p[axis] += slice;
                    p[u] += i;
                    p[v] += j;
                    const Block back = world.get(p - step);
                    const Block front = world.get(p);
                    Face face;
                    if (slice > 0 && exposed(back, front)) face = {back, 1};
                    else if (slice < side && exposed(front, back)) face = {front, -1};
                    mask[j * side + i] = face;
                }
            }
            for (int j = 0; j < side; ++j) {
                for (int i = 0; i < side; ++i) {
                    const Face face = mask[j * side + i];
                    if (face.sign == 0) continue;
                    int width = 1, height = 1;
                    if (mode == Meshing::greedy) {
                        while (i + width < side && mask[j * side + i + width] == face) ++width;
                        while (j + height < side) {
                            const auto row = mask.begin() + (j + height) * side + i;
                            if (!std::all_of(row, row + width, [face](Face f) { return f == face; }))
                                break;
                            ++height;
                        }
                    }
                    glm::vec3 p = chunk.origin, du{}, dv{};
                    p[axis] += slice;
                    p[u] += i;
                    p[v] += j;
                    du[u] = width;
                    dv[v] = height;
                    if (face.sign < 0) std::swap(du, dv);
                    quad(mesh, p, du, dv, glm::vec3(step * face.sign), face.block);
                    for (int r = 0; r < height; ++r)
                        std::fill_n(mask.begin() + (j + r) * side + i, width, Face{});
                    i += width - 1;
                }
            }
        }
    }
    for (int z = 0; z < side; ++z)
        for (int y = 0; y < side; ++y)
            for (int x = 0; x < side; ++x) {
                const auto at = chunk.origin + glm::ivec3(x,y,z);
                const auto block = world.get(at);
                if (scenery(block)) appendScenery(mesh,at,block);
            }
    return mesh;
}

} // namespace wsim
