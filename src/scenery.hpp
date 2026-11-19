#pragma once

#include "mesher.hpp"

namespace wsim {

constexpr int treeTrunkCells = 3;
glm::vec3 treeTrunkSize(glm::ivec3 anchor);

// Scenery meshes are rendered at their actual size from their anchor cells.
void appendScenery(Mesh& mesh, glm::ivec3 anchor, Block block);

} // namespace wsim
