#include "water.hpp"
#include "player.hpp"

#include <glm/common.hpp>
#include <algorithm>
#include <cmath>

namespace wsim {

Water::Water(const World& world) : edge_(world.edge()), level_(edge_*edge_),
    heights_(level_.size()), previous_(level_.size()) {
    for (const auto& chunk : world.chunks()) {
        for (std::size_t i = 0; i < chunk.blocks.size(); ++i) {
            if (chunk.blocks[i] != Block::water) continue;
            const auto p = chunk.origin + glm::ivec3(i%chunkSize,i/(chunkSize*chunkSize),i/chunkSize%chunkSize);
            auto& level = level_[p.z*edge_+p.x];
            level = std::max(level,static_cast<std::uint8_t>(p.y+1));
        }
    }
    for (int z = 0; z < edge_; ++z) {
        for (int x = 0; x < edge_; ++x) {
            const int index = z*edge_+x;
            if (!level_[index]) continue;
            const auto neighbor = [&](int nx, int nz) {
                if (nx < 0 || nz < 0 || nx >= edge_ || nz >= edge_) return index;
                const int other = nz*edge_+nx;
                // A solid bank or a different surface elevation has no normal flux.
                return level_[other] == level_[index] ? other : index;
            };
            cells_.push_back({index,{neighbor(x-1,z),neighbor(x+1,z),neighbor(x,z-1),neighbor(x,z+1)}});
        }
    }
}

bool Water::splash(glm::vec3 feet, float strength) {
    const glm::ivec2 center = glm::floor(glm::vec2(feet.x,feet.z));
    if (center.x < 0 || center.y < 0 || center.x >= edge_ || center.y >= edge_) return false;
    const auto level = level_[center.y*edge_+center.x];
    if (!level || feet.y < level-1.5f || feet.y > level+.15f) return false;
    for (int z = std::max(0,center.y-1); z <= std::min(edge_-1,center.y+1); ++z) {
        for (int x = std::max(0,center.x-1); x <= std::min(edge_-1,center.x+1); ++x) {
            const int index = z*edge_+x;
            if (level_[index] != level) continue;
            const auto delta = glm::vec2(x+.5f-feet.x,z+.5f-feet.z);
            const float weight = std::max(0.f,1.f-(delta.x*delta.x+delta.y*delta.y)*.64f);
            previous_[index] -= strength*weight;
        }
    }
    remainingSteps_ = 900;
    return true;
}

void Water::step() {
    if (!remainingSteps_) return;
    constexpr float waveSpeed = 16.f;
    constexpr float courant = waveSpeed*static_cast<float>(physicsStep);
    static_assert(courant*courant < .5f,"the 2D wave scheme must satisfy the CFL condition");
    for (const auto& cell : cells_) {
        const float height = heights_[cell.index];
        float laplacian = -4.f*height;
        for (const int neighbor : cell.neighbors) laplacian += heights_[neighbor];
        previous_[cell.index] = .998f*height + .982f*(height-previous_[cell.index]) +
                               courant*courant*laplacian;
    }
    heights_.swap(previous_);
    ++revision_;
    if (--remainingSteps_ == 0) {
        std::fill(heights_.begin(),heights_.end(),0.f);
        std::fill(previous_.begin(),previous_.end(),0.f);
    }
}

} // namespace wsim
