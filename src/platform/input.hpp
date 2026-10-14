#pragma once
#include <SDL.h>
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace epub {
enum class Button { Up, Down, Left, Right, A, B, X, Y, L1, R1, L2, R2, Select, Start, Unknown };
enum class Action { None, Quit, Up, Down, Left, Right, AShort, ALong, B, X, Y, L1, R1, L2, R2, Select, Start };
Button mapKey(SDLKey key);
const char* buttonName(Button button);
class Input {
    std::array<bool, 14> held_{};
    uint32_t aSince_ = 0, nextRepeat_ = 0;
    Button repeating_ = Button::Unknown;
    bool longFired_ = false, suppressA_ = false;
public:
    Action key(SDLKey key, bool down, uint32_t now);
    Action tick(uint32_t now);
    void reset();
    void consumeA();
    bool held(Button b) const;
    std::string heldNames() const;
};
}
