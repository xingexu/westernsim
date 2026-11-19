#pragma once

#include "campus_landmarks.hpp"
#include "world.hpp"

namespace wsim {

World makeCampus();
glm::vec3 campusClockCenter(const World& world);

} // namespace wsim
