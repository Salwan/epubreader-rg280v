#pragma once
#include "audio/audio.hpp"
#include "platform/input.hpp"
#include "platform/mixer.hpp"
#include "ui/canvas.hpp"
#include <deque>

namespace epub {
class Diagnostic {
    std::array<TTF_Font*, 24> fonts_{};
    void geometry(Canvas& c);
    void typography(Canvas& c);
    TTF_Font* font(int face, bool high) const;
public:
    State& state;
    Audio& audio;
    Mixer& mixer;
    int screen = 0, pattern = 0;
    bool highResolution = true; // Device-selected 640x480 -> 320x480 box filter.
    size_t trackIndex = 0;
    std::vector<std::string> tracks;
    std::deque<std::string> events;
    std::string system, video, held = "NONE", storageStatus = "State saved", evdev;
    unsigned volumePresses = 0;
    Diagnostic(State& state, Audio& audio, Mixer& mixer, const std::string& assets, const std::string& music);
    ~Diagnostic();
    void event(const std::string& line);
    bool action(Action action); // True if settings/selected mixer changed.
    void render(Canvas& canvas);
};
}
