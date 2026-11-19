#pragma once

#include "renderer.hpp"

#include <glm/vec2.hpp>
#include <array>
#include <memory>
#include <string_view>

namespace wsim {

struct MaterialSlot { Block block; std::string_view label; };
inline constexpr std::array<MaterialSlot, 8> buildingPalette{{
    {Block::grass, "Turf"}, {Block::shale, "Shale"}, {Block::limestone, "Limestone"},
    {Block::brick, "Terracotta"}, {Block::planks, "Cedar plank"}, {Block::birch, "Birch"},
    {Block::stainedGlass, "Stained glass"}, {Block::lantern, "Lantern"}
}};

struct HudState {
    RenderStats stats;
    std::string_view toast;
    float fps = 0.f;
    float heading = 0.f;
    int slot = 0;
    glm::vec2 position{};
    bool editing = false, guide = false, map = false, diagnostics = false;
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
