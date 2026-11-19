#pragma once

#include "mesher.hpp"
#include "player.hpp"

#include <glm/mat4x4.hpp>
#include <memory>

namespace wsim {

struct Camera {
    glm::vec3 eye, forward;
    glm::mat4 view, projection;
};

struct RenderStats {
    std::size_t chunks = 0, totalChunks = 0, triangles = 0, totalTriangles = 0;
};

class Renderer {
public:
    Renderer();
    ~Renderer();
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    void rebuild(const World& world, Meshing mode);
    RenderStats draw(const Camera& camera, const Player& player, int width, int height, float time);

private:
    struct Data;
    std::unique_ptr<Data> data_;
};

unsigned shaderProgram(const char* vertex, const char* fragment);

} // namespace wsim
