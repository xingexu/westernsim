#pragma once

#include "player.hpp"
#include <deque>
#include <optional>

namespace wsim {

class RemoteAvatar {
public:
    void receive(Player pose, double now);
    void clear() { samples_.clear(); }
    std::optional<Player> pose(double now);

private:
    struct Sample { Player pose; double time; };
    std::deque<Sample> samples_;
};

} // namespace wsim
