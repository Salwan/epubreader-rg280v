#include "ui/reader_controls.hpp"
#include <algorithm>

namespace epub {
ReaderControl ReaderControls::handle(Action action, int font, int theme, bool busy) {
    if (action == Action::Quit) return {ReaderEffect::Quit};
    if (action == Action::X) {
        if (menuOpen_) menuOpen_ = false; else showMenu();
        return {ReaderEffect::Refresh};
    }
    if (menuOpen_) {
        if (action == Action::B) { menuOpen_ = false; return {ReaderEffect::Refresh}; }
        if (action == Action::Up || action == Action::Down) {
            selected_ = static_cast<ReaderMenuItem>(std::clamp(static_cast<int>(selected_) + (action == Action::Down ? 1 : -1), 0, 2));
            return {ReaderEffect::Refresh};
        }
        if (action == Action::AShort && selected_ == ReaderMenuItem::Exit) return {ReaderEffect::Quit};
        if (action == Action::AShort && selected_ == ReaderMenuItem::Back) {
            menuOpen_ = false; return {ReaderEffect::Back};
        }
        if (busy) return {};
        if (selected_ == ReaderMenuItem::FontSize && (action == Action::Left || action == Action::Right)) {
            const int target = std::clamp(font + (action == Action::Right ? 1 : -1), 0, 2);
            if (target != font) return {ReaderEffect::Font, target};
        }
        return {}; // Overlay consumes page turns, specimen shortcuts, and A holds.
    }
    if (busy) return {};
    switch (action) {
    case Action::Left: case Action::L1: return {ReaderEffect::Turn, -1};
    case Action::Right: case Action::R1: return {ReaderEffect::Turn, 1};
    case Action::L2: return {ReaderEffect::Theme, (theme + 5) % 6};
    case Action::R2: return {ReaderEffect::Theme, (theme + 1) % 6};
    default: return {}; // Font size is changed only by the popup row.
    }
}
}
