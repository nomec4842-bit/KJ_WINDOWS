#pragma once
#include "core/tracks.h"
#include <atomic>
#include <memory>
namespace meter {
struct Level { std::atomic<float> left{0},right{0};
    std::atomic<bool> clipped{false};void publish(double l,double r,double seconds); };
std::vector<std::shared_ptr<Level>> synchronize(const std::vector<Track>& tracks);
std::shared_ptr<Level> track(int id);
Level& master();
}
