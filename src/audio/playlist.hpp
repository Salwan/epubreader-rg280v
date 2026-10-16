#pragma once
#include <optional>
#include <random>
#include <vector>

namespace epub {
class Playlist {
    std::mt19937 random_;
    std::vector<bool> failed_;
    std::vector<size_t> order_;
    size_t cursor_ = 0;
    std::optional<size_t> last_;
public:
    explicit Playlist(unsigned seed) : random_(seed) {}
    void catalog(size_t count);
    void reset();
    void fail(size_t index);
    std::optional<size_t> next();
};
}
