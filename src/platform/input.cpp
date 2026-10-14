#include "platform/input.hpp"

namespace epub {
Button mapKey(SDLKey key) {
    switch (key) {
    case SDLK_UP: return Button::Up; case SDLK_DOWN: return Button::Down;
    case SDLK_LEFT: return Button::Left; case SDLK_RIGHT: return Button::Right;
    case SDLK_LCTRL: case SDLK_a: return Button::A;
    case SDLK_LALT: case SDLK_b: return Button::B;
    case SDLK_SPACE: case SDLK_x: return Button::X;
    case SDLK_LSHIFT: case SDLK_y: return Button::Y;
    case SDLK_TAB: case SDLK_q: return Button::L1;
    case SDLK_BACKSPACE: case SDLK_w: return Button::R1;
    case SDLK_PAGEUP: case SDLK_e: return Button::L2;
    case SDLK_PAGEDOWN: case SDLK_r: return Button::R2;
    case SDLK_ESCAPE: return Button::Select;
    case SDLK_RETURN: return Button::Start;
    default: return Button::Unknown;
    }
}
const char* buttonName(Button b) {
    static const char* names[] = {"UP","DOWN","LEFT","RIGHT","A","B","X","Y","L1","R1","L2","R2","SELECT","START","UNKNOWN"};
    return names[static_cast<int>(b)];
}
bool Input::held(Button b) const { return b != Button::Unknown && held_[static_cast<size_t>(b)]; }
void Input::reset() { held_.fill(false); repeating_ = Button::Unknown; longFired_ = suppressA_ = false; }
void Input::consumeA() { suppressA_ = true; }
std::string Input::heldNames() const {
    std::string s;
    for (size_t i = 0; i < held_.size(); ++i) if (held_[i]) { if (!s.empty()) s += ' '; s += buttonName(static_cast<Button>(i)); }
    return s.empty() ? "NONE" : s;
}
Action Input::key(SDLKey k, bool down, uint32_t now) {
    const Button b = mapKey(k);
    if (b == Button::Unknown) return Action::None;
    bool& wasDown = held_[static_cast<size_t>(b)];
    if (wasDown == down) return Action::None; // Firmware repeats cannot duplicate actions.
    wasDown = down;
    if (held(Button::Select) && held(Button::Start)) { suppressA_ = true; return Action::Quit; }
    if (b == Button::A) {
        if (down) { aSince_ = now; longFired_ = suppressA_ = false; }
        else if (!longFired_ && !suppressA_) {
            // Release can arrive before tick(), including after a suspension.
            return now - aSince_ >= 600 ? Action::ALong : Action::AShort;
        }
        return Action::None;
    }
    if (!down) { if (repeating_ == b) repeating_ = Button::Unknown; return Action::None; }
    if (b == Button::Up || b == Button::Down) { repeating_ = b; nextRepeat_ = now + 350; }
    switch (b) {
    case Button::Up: return Action::Up; case Button::Down: return Action::Down;
    case Button::Left: return Action::Left; case Button::Right: return Action::Right;
    case Button::B: return Action::B; case Button::X: return Action::X; case Button::Y: return Action::Y;
    case Button::L1: return Action::L1; case Button::R1: return Action::R1;
    case Button::L2: return Action::L2; case Button::R2: return Action::R2;
    case Button::Select: return Action::Select; case Button::Start: return Action::Start;
    default: return Action::None;
    }
}
Action Input::tick(uint32_t now) {
    if (held(Button::Select) && held(Button::Start)) return Action::Quit;
    if (held(Button::A) && !longFired_ && !suppressA_ && now - aSince_ >= 600) {
        longFired_ = true; return Action::ALong;
    }
    if (repeating_ != Button::Unknown && static_cast<int32_t>(now - nextRepeat_) >= 0) {
        nextRepeat_ = now + 80; return repeating_ == Button::Up ? Action::Up : Action::Down;
    }
    return Action::None;
}
}
