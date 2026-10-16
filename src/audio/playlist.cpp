#include "audio/playlist.hpp"
#include <algorithm>

namespace epub {
void Playlist::catalog(size_t count) { failed_.assign(count, false); reset(); }
void Playlist::reset() { order_.clear(); cursor_ = 0; last_.reset(); }
void Playlist::fail(size_t index) { failed_.at(index) = true; }
std::optional<size_t> Playlist::next() {
    for (;;) {
        if (cursor_ == order_.size()) {
            order_.clear(); cursor_ = 0;
            for (size_t n = 0; n < failed_.size(); ++n) if (!failed_[n]) order_.push_back(n);
            if (order_.empty()) return {};
            std::shuffle(order_.begin(), order_.end(), random_);
            if (order_.size() > 1 && last_ == order_.front()) std::swap(order_[0], order_[1]);
        }
        const size_t next = order_[cursor_++];
        if (!failed_[next]) { last_ = next; return next; }
    }
}
}
