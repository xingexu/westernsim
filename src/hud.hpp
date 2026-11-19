#pragma once

#include "renderer.hpp"

#include <glm/vec2.hpp>
#include <memory>
#include <string_view>

namespace wsim {

struct HudState {
    RenderStats stats;
    std::string_view toast;
    float fps = 0.f;
    float heading = 0.f;
    glm::vec2 position{};
    bool guide = false, map = false, diagnostics = false;
    Meshing meshing = Meshing::greedy;
};

class Hud {
public:
    Hud();
    ~Hud();
    Hud(const Hud&) = delete;
    Hud& operator=(const Hud&) = delete;
    void setCampusMap(const World& world);
    void draw(int width, int height, const HudState& state);

private:
    struct Data;
    std::unique_ptr<Data> data_;
};

} // namespace wsim
