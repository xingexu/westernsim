#pragma once

#include "mesher.hpp"

namespace wsim {

// Fixtures occupy a saveable anchor cell and are rendered at their actual size.
void appendScenery(Mesh& mesh, glm::ivec3 anchor, Block block);

} // namespace wsim
