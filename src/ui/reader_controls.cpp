#include "ui/reader_controls.hpp"
#include <algorithm>

namespace epub {
bool ReaderControls::jumpMatches(const ReaderResult& page) const {
    return jumpOpen() && page.validNavigation() && page.identity == identity_ && page.layoutKey == layout_
        && page.anchor == anchor_ && page.page == originPage_ && page.pageCount == pageCount_;
}
ReaderControl ReaderControls::handle(Action action, int font, int theme, bool busy, const ReaderResult* page, RepeatMode repeat) {
    if (action == Action::Quit) return {ReaderEffect::Quit};
    if (action == Action::X) {
        if (menuOpen()) closeOverlay(); else showMenu();
        return {ReaderEffect::Refresh};
    }
    if (menuOpen()) {
        if (action == Action::B) { closeOverlay(); return {ReaderEffect::Refresh}; }
        if (action == Action::Up || action == Action::Down) {
            selected_ = static_cast<ReaderMenuItem>(std::clamp(static_cast<int>(selected_) + (action == Action::Down ? 1 : -1), 0, 3));
            return {ReaderEffect::Refresh};
        }
        if (action == Action::AShort && selected_ == ReaderMenuItem::Exit) return {ReaderEffect::Quit};
        if (action == Action::AShort && selected_ == ReaderMenuItem::Back) {
            closeOverlay(); return {ReaderEffect::Back};
        }
        if (selected_ == ReaderMenuItem::MusicRepeat && (action == Action::Left || action == Action::Right)) {
            const int target = std::clamp(static_cast<int>(repeat) + (action == Action::Right ? 1 : -1), 0, 2);
            if (target != static_cast<int>(repeat)) return {ReaderEffect::Repeat, target};
        }
        if (busy) return {};
        if (selected_ == ReaderMenuItem::FontSize && (action == Action::Left || action == Action::Right)) {
            const int target = std::clamp(font + (action == Action::Right ? 1 : -1), 0, 2);
            if (target != font) return {ReaderEffect::Font, target};
        }
        return {}; // Overlay consumes page turns, specimen shortcuts, and A holds.
    }
    if (jumpOpen()) {
        if (action == Action::B || action == Action::Select) { closeOverlay(); return {ReaderEffect::Refresh}; }
        if (busy) return {};
        if (!page || !jumpMatches(*page)) { showMenu(); return {ReaderEffect::Refresh}; }
        int delta = 0;
        if (action == Action::Left) delta = -1;
        if (action == Action::Right) delta = 1;
        if (action == Action::Up) delta = 10;
        if (action == Action::Down) delta = -10;
        if (delta) { target_ = std::clamp(target_ + delta, 0, 100); return {ReaderEffect::Refresh}; }
        if (action == Action::AShort) {
            if (targetPage() == page->page) { closeOverlay(); return {ReaderEffect::Refresh}; }
            return {ReaderEffect::Jump, targetPage()}; // Controller closes only after successful submission.
        }
        return {};
    }
    if (page && !page->pixels.empty()) {
        if (action == Action::AShort) return {ReaderEffect::MusicToggle};
        if (action == Action::ALong) return {ReaderEffect::MusicSkip};
    }
    if (busy) return {};
    if (action == Action::Select && page && page->validNavigation() && !page->pixels.empty()) {
        originPage_ = page->page; pageCount_ = page->pageCount;
        originPercent_ = originPage_ == pageCount_ - 1 ? 100 : originPage_ == 0 ? 0 : std::clamp(page->progress / 100, 1, 99);
        target_ = originPercent_; targets_ = page->percentPages;
        identity_ = page->identity; layout_ = page->layoutKey; anchor_ = page->anchor;
        overlay_ = ReaderOverlay::Jump;
        return {ReaderEffect::Refresh};
    }
    switch (action) {
    case Action::Left: case Action::L1: return {ReaderEffect::Turn, -1};
    case Action::Right: case Action::R1: return {ReaderEffect::Turn, 1};
    case Action::L2: return {ReaderEffect::Theme, (theme + 5) % 6};
    case Action::R2: return {ReaderEffect::Theme, (theme + 1) % 6};
    default: return {}; // Font size is changed only by the popup row.
    }
}
}
