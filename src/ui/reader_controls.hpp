#pragma once
#include "platform/input.hpp"

namespace epub {
enum class ReaderMenuItem { Back, FontSize, Exit };
enum class ReaderEffect { None, Refresh, Quit, Turn, Font, Theme, Back };
struct ReaderControl { ReaderEffect effect = ReaderEffect::None; int value = 0; };
class ReaderControls {
    bool menuOpen_ = false;
    ReaderMenuItem selected_ = ReaderMenuItem::Back;
public:
    bool menuOpen() const { return menuOpen_; }
    ReaderMenuItem selected() const { return selected_; }
    void showMenu() { menuOpen_ = true; selected_ = ReaderMenuItem::Back; }
    ReaderControl handle(Action action, int font, int theme, bool busy);
};
}
