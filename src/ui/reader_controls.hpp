#pragma once
#include "platform/input.hpp"

namespace epub {
enum class ReaderMenuItem { Reopen, FontSize, Exit };
enum class ReaderEffect { None, Refresh, Quit, Turn, Font, Theme, Reopen, Specimen };
struct ReaderControl { ReaderEffect effect = ReaderEffect::None; int value = 0; };
class ReaderControls {
    bool menuOpen_ = false;
    ReaderMenuItem selected_ = ReaderMenuItem::Reopen;
public:
    bool menuOpen() const { return menuOpen_; }
    ReaderMenuItem selected() const { return selected_; }
    void showMenu() { menuOpen_ = true; selected_ = ReaderMenuItem::Reopen; }
    ReaderControl handle(Action action, int font, int theme, bool busy);
};
}
