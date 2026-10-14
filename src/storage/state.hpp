#pragma once
#include <string>

namespace epub {
struct State {
    int theme = 0, font = 1, volume = 50;
    std::string mixer;
    long minimum = 0, maximum = 0, rawLeft = -1, rawRight = -1;
    int switchLeft = -1, switchRight = -1;
};
State loadState(const std::string& directory);
void saveState(const std::string& directory, const State& state);
void ensureDirectory(const std::string& directory);
}
