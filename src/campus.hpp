#pragma once

#include "campus_landmarks.hpp"
#include "world.hpp"

namespace wsim {

World makeCampus();
glm::vec3 campusClockCenter(const World& world);
bool landmarkContainsColumn(const Landmark& landmark, glm::ivec2 column);

} // namespace wsim
