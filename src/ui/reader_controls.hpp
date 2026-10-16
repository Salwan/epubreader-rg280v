#pragma once
#include "platform/input.hpp"
#include "reader/types.hpp"

namespace epub {
enum class ReaderMenuItem { Back, FontSize, Exit };
enum class ReaderOverlay { None, Menu, Jump };
enum class ReaderEffect { None, Refresh, Quit, Turn, Font, Theme, Back, Jump, MusicToggle, MusicSkip };
struct ReaderControl { ReaderEffect effect = ReaderEffect::None; int value = 0; };
class ReaderControls {
    ReaderOverlay overlay_ = ReaderOverlay::None;
    ReaderMenuItem selected_ = ReaderMenuItem::Back;
    int target_ = 0, originPercent_ = 0, originPage_ = 0, pageCount_ = 0;
    std::string identity_, layout_, anchor_;
    std::vector<int> targets_;
public:
    ReaderOverlay overlay() const { return overlay_; }
    bool menuOpen() const { return overlay_ == ReaderOverlay::Menu; }
    bool jumpOpen() const { return overlay_ == ReaderOverlay::Jump; }
    ReaderMenuItem selected() const { return selected_; }
    int targetPercent() const { return target_; }
    int targetPage() const { return target_ == originPercent_ ? originPage_ : targets_.at(static_cast<size_t>(target_)); }
    int targetPageCount() const { return pageCount_; }
    bool jumpMatches(const ReaderResult& page) const;
    void closeOverlay() { overlay_ = ReaderOverlay::None; }
    void showMenu() { overlay_ = ReaderOverlay::Menu; selected_ = ReaderMenuItem::Back; }
    ReaderControl handle(Action action, int font, int theme, bool busy, const ReaderResult* page = nullptr);
};
}
