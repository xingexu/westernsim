#include "remote_avatar.hpp"

#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <algorithm>
#include <cmath>

namespace wsim {

void RemoteAvatar::receive(Player pose,double now) {
    if (!samples_.empty() && now <= samples_.back().time) {
        samples_.back().pose = pose;
        return;
    }
    samples_.push_back({pose,now});
    if (samples_.size() > 16) samples_.pop_front();
}

std::optional<Player> RemoteAvatar::pose(double now) {
    constexpr double staleAfter = 2.5, interpolationDelay = .12;
    if (samples_.empty() || now-samples_.back().time > staleAfter) return std::nullopt;
    const double target = now-interpolationDelay;
    while (samples_.size() > 1 && samples_[1].time <= target) samples_.pop_front();
    const auto& first = samples_.front();
    if (samples_.size() == 1 || target <= first.time) return first.pose;
    const auto& next = samples_[1];
    // A respawn should appear as a cut, without gliding through buildings.
    if (next.time-first.time > .75 || glm::distance(next.pose.feet,first.pose.feet) > 8.f ||
        next.pose.stride < first.pose.stride) return first.pose;
    const auto blend = static_cast<float>((target-first.time)/(next.time-first.time));
    Player result;
    result.feet = glm::mix(first.pose.feet,next.pose.feet,blend);
    result.heading = first.pose.heading + blend*std::remainder(next.pose.heading-first.pose.heading,6.2831853f);
    result.stride = glm::mix(first.pose.stride,next.pose.stride,blend);
    result.gait = glm::mix(first.pose.gait,next.pose.gait,blend);
    return result;
}

} // namespace wsim
