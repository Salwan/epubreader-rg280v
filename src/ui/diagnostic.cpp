#include "ui/diagnostic.hpp"
#include <algorithm>
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace epub {
namespace {
const char* screenNames[] = {"DISPLAY", "FONTS", "BUTTON EVENTS", "AUDIO", "ALSA MIXER"};
const char* fontFiles[] = {"DejaVuSerif.ttf", "DejaVuSerif-Bold.ttf", "DejaVuSerif-Italic.ttf", "DejaVuSerif-BoldItalic.ttf"};
const int fontSizes[] = {12, 14, 16};
std::string baseName(const std::string& path) { const auto pos = path.find_last_of('/'); return pos == std::string::npos ? path : path.substr(pos+1); }
std::string clipped(const std::string& s, size_t n = 50) { return s.size() > n ? s.substr(0,n-3) + "..." : s; }
void wrapped(Canvas& c, int x, int& y, int width, int bottom, const std::string& s, TTF_Font* font, uint32_t color) {
    std::istringstream words(s); std::string word, line;
    const int lineHeight = TTF_FontLineSkip(font);
    while (words >> word) {
        const std::string candidate = line.empty() ? word : line + ' ' + word;
        int w = 0; TTF_SizeUTF8(font, candidate.c_str(), &w, nullptr);
        if (w > width && !line.empty()) {
            if (y + lineHeight > bottom) return;
            c.ttf(x, y, line, font, color); y += lineHeight; line = word;
        } else line = candidate;
    }
    if (!line.empty() && y + lineHeight <= bottom) { c.ttf(x, y, line, font, color); y += lineHeight; }
}
}
Diagnostic::Diagnostic(State& saved, Audio& a, Mixer& m, const std::string& assets, const std::string& music) : state(saved), audio(a), mixer(m) {
    for (int face = 0; face < 4; ++face) for (int size = 0; size < 3; ++size) for (int high = 0; high < 2; ++high) {
        const auto index = static_cast<size_t>((face * 3 + size) * 2 + high);
        fonts_[index] = TTF_OpenFont((assets + "/fonts/" + fontFiles[face]).c_str(), fontSizes[size] * (high ? 2 : 1));
        if (!fonts_[index]) {
            for (auto* f : fonts_) if (f) TTF_CloseFont(f);
            throw std::runtime_error("Bundled font: " + std::string(TTF_GetError()));
        }
    }
    tracks.push_back(assets + "/audio/test-tone.mp3");
    auto user = discoverMp3(music); tracks.insert(tracks.end(), user.begin(), user.end());
}
Diagnostic::~Diagnostic() { for (auto* f : fonts_) if (f) TTF_CloseFont(f); }
TTF_Font* Diagnostic::font(int face, bool high) const { return fonts_[static_cast<size_t>((face * 3 + state.font) * 2 + (high ? 1 : 0))]; }
void Diagnostic::event(const std::string& line) {
    events.push_front(line); while (events.size() > 12) events.pop_back();
    std::cout << line << '\n';
}
bool Diagnostic::action(Action a) {
    if (a == Action::L1 || a == Action::R1) { screen = (screen + (a == Action::L1 ? 4 : 1)) % 5; return false; }
    if (a == Action::L2 || a == Action::R2) {
        const int count = static_cast<int>(palettes.size());
        state.theme = (state.theme + (a == Action::L2 ? count - 1 : 1)) % count;
        return true;
    }
    if (a == Action::Y) { state.font = (state.font + 1) % 3; return true; }
    if (screen == 0 && a == Action::X) pattern = (pattern + 1) % 4;
    if (screen == 1 && a == Action::X) highResolution = !highResolution;
    if (screen == 3) {
        if (a == Action::X) audio.chirp();
        if (a == Action::B) audio.stop();
        if (a == Action::AShort) audio.toggle(tracks[trackIndex]);
        if (a == Action::Up || a == Action::Down || a == Action::ALong) {
            const auto playing = audio.status().playback;
            if (a == Action::Up) trackIndex = (trackIndex + tracks.size() - 1) % tracks.size();
            else trackIndex = (trackIndex + 1) % tracks.size();
            if (playing != Playback::Stopped) {
                audio.start(tracks[trackIndex]);
                if (playing == Playback::Paused) audio.toggle(tracks[trackIndex]);
            }
        }
    }
    if (screen == 4 && (a == Action::Up || a == Action::Down)) { mixer.select(a == Action::Up ? -1 : 1); return true; }
    return false;
}
void Diagnostic::geometry(Canvas& c) {
    const auto& p = palettes[static_cast<size_t>(state.theme)];
    if (pattern == 0) {
        Canvas square(640,480); square.clear(p.background);
        square.circle(140,160,58,p.accent);
        c.transformFrom(square,true);
        c.circle(232,160,58,p.text);
        c.text(8,230,"640X480 -> 320X480    NATIVE SQUARE PIXELS",p.text);
        for (int y = 268; y < 332; ++y) for (int x = 8; x < 104; ++x) c.pixel(x,y,((x+y)&1) ? p.text : p.background);
        for (int y = 268; y < 332; ++y) for (int x = 112; x < 208; ++x) c.pixel(x,y,((x+y/2)&1) ? p.text : p.background);
        for (int x = 216; x < 304; x += 2) c.rect(x,268,1,64,p.text);
        c.text(8,342,"1X1 NATIVE       1X2 NATIVE    1-COL LINES",p.text);
        c.text(8,66,"ROUND ON DEVICE     SQUASHED REFERENCE",p.text);
        c.text(8,380,"BOTTOM: 2 ROWS Y=478..479, HALF PROGRESS",p.text);
        c.text(8,400,"RIGHT: 2 COLUMNS AT X=318..319",p.text);
    } else if (pattern == 1 || pattern == 2) {
        for (int y = 56; y < 434; ++y) for (int x = 0; x < 318; ++x)
            c.pixel(x,y,((x + (pattern == 1 ? y : y/2)) & 1) ? 0xffffff : 0x000000);
        c.rect(8,70,300,40,p.panel); c.text(14,82,pattern == 1 ? "1X1 NATIVE CHECKERBOARD" : "1X2 NATIVE CHECKERBOARD",p.text);
    } else {
        for (int x = 8; x < 312; ++x) { const auto g = static_cast<uint32_t>((x-8)*255/303); c.rect(x,100,1,180,g*0x010101); }
        c.text(8,304,"GREYSCALE RAMP: BLACK -> WHITE",p.text);
        const uint32_t colors[] = {0xff0000,0x00ff00,0x0000ff,0xffffff,0x000000};
        for (int i = 0; i < 5; ++i) c.rect(8+i*60,344,56,60,colors[i]);
    }
    c.outline(0,0,318,479,p.muted);
}
void Diagnostic::typography(Canvas& c) {
    const auto& p = palettes[static_cast<size_t>(state.theme)];
    const int scale = highResolution ? 2 : 1;
    Canvas page(highResolution ? 640 : 320, highResolution ? 480 : 240);
    page.clear(p.background);
    int y = 60*scale;
    for (int face = 0; face < 4; ++face) {
        wrapped(page,8*scale,y,304*scale,205*scale,
            face == 0 ? "The quiet page: regular prose." : face == 1 ? "Bold headings and emphasis." : face == 2 ? "Italic words drift softly." : "Bold italic: a gentle emphasis.",font(face,highResolution),p.text);
        y += 3*scale;
    }
    y += 4*scale;
    wrapped(page,8*scale,y,304*scale,202*scale,"“A reader lives a thousand lives.” UTF-8: café, naïve — an ellipsis… Curved letters, fine serifs, and a calm line of prose.",font(0,highResolution),p.text);
    c.transformFrom(page,highResolution);
    c.text(8,66,highResolution ? "640X480 / HORIZONTAL 2:1 BOX FILTER" : "320X240 / VERTICAL 2X NEAREST",p.accent);
    c.text(8,400,"NATIVE 5X7: ABC XYZ 0123456789 !?",p.text,1);
    c.text(8,419,"NATIVE 5X14: ABC XYZ 0123456789 !?",p.text,2);
}
void Diagnostic::render(Canvas& c) {
    const auto& p = palettes[static_cast<size_t>(state.theme)];
    c.clear(p.background);
    if (screen == 0) geometry(c);
    if (screen == 1) typography(c);
    if (screen == 2) {
        c.text(8,66,clipped(system),p.text);
        c.text(8,88,clipped(video),p.accent);
        c.text(8,110,clipped(evdev),p.text);
        c.text(8,140,"HELD: " + clipped(held,42),p.accent);
        c.text(8,166,"RAW SDL + LINUX EVENTS (NEWEST FIRST)",p.text);
        int y = 196;
        for (const auto& e : events) { c.text(8,y,clipped(e),p.text); y += 18; }
        c.text(8,420,"A: SHORT ON RELEASE / LONG AT 600MS",p.accent);
    }
    if (screen == 3) {
        const auto s = audio.status();
        c.text(8,66,"X CHIRP  A PLAY/PAUSE  B STOP",p.accent);
        c.text(8,92,"UP/DOWN TRACK  HOLD A NEXT (ONCE)",p.text);
        c.text(8,126,"TRACK " + std::to_string(trackIndex+1) + '/' + std::to_string(tracks.size()),p.text);
        c.text(8,150,clipped(baseName(tracks[trackIndex])),p.accent);
        c.text(8,184,"STATE: " + std::string(s.playback == Playback::Playing ? "PLAYING" : s.playback == Playback::Paused ? "PAUSED" : "STOPPED"),p.text);
        c.text(8,210,clipped(s.message),p.text);
        c.text(8,246,"OUTPUT " + std::to_string(s.frequency) + " HZ / " + std::to_string(s.channels) + " CH / S16",p.text);
        c.text(8,274,"DECODED FRAMES: " + std::to_string(s.decodedFrames),p.text);
        c.text(8,302,"PLAYED FRAMES:  " + std::to_string(s.playedFrames),p.text);
        c.text(8,330,"QUEUED FRAMES:  " + std::to_string(s.queuedFrames),p.text);
        c.text(8,358,"UNDERRUN CALLBACKS: " + std::to_string(s.underruns),p.accent);
        c.text(8,400,"MUSIC CONTINUES ACROSS TEST SCREENS",p.text);
        c.text(8,420,"FIRST TRACK IS BUNDLED QUIET STEREO TONE",p.text);
    }
    if (screen == 4) {
        c.text(8,66,"PHYSICAL VOLUME IS OWNED BY FIRMWARE",p.accent);
        c.text(8,92,"UP/DOWN SELECT OBSERVED CONTROL",p.text);
        if (const auto* l = mixer.selected()) {
            c.text(8,128,"CONTROL: " + clipped(l->id,40),p.accent);
            c.text(8,154,"RAW L/R: " + std::to_string(l->left) + " / " + std::to_string(l->right),p.text);
            c.text(8,180,"RANGE: " + std::to_string(l->minimum) + ".." + std::to_string(l->maximum) + "   LEVEL " + std::to_string(l->percent()) + '%',p.text);
            c.text(8,206,"SWITCH L/R: " + std::to_string(l->switchLeft) + " / " + std::to_string(l->switchRight),p.text);
            c.rect(8,236,304,8,p.panel); c.rect(8,236,304*l->percent()/100,8,p.accent);
            c.text(8,268,clipped(mixer.restoreResult()),p.text);
        } else c.text(8,130,"MIXER UNAVAILABLE: " + clipped(mixer.error(),30),p.text);
        c.text(8,298,"OBSERVED CHANGE POLLS: " + std::to_string(mixer.changes()),p.text);
        c.text(8,322,"VOLUME KEY-DOWN EVENTS: " + std::to_string(volumePresses),p.text);
        c.text(8,346,"APP SETTER CALLS: " + std::to_string(mixer.writes()) + " (STARTUP ONLY)",p.accent);
        c.text(8,378,"SAVED LEVEL: " + std::to_string(state.volume) + "% / " + clipped(state.mixer,28),p.text);
        c.text(8,404,clipped(storageStatus),p.text);
        c.text(8,424,"SWITCH: 0=MUTED 1=ON -1=NOT PRESENT",p.text);
    }
    c.rect(0,0,318,54,p.panel);
    c.text(8,8,"EPUB READER M1 " EPUBREADER_VERSION " / " + std::to_string(screen+1) + "/5",p.text);
    c.text(8,30,std::string(screenNames[screen]) + "  " + p.name + "  " + std::to_string(fontSizes[state.font]) + "PX",p.accent);
    c.rect(0,440,318,38,p.panel);
    c.text(8,446,"L1/R1 SCREEN  L2/R2 THEME  Y SIZE",p.text);
    c.text(8,464,"SELECT+START EXIT  X DISPLAY/FONT MODE",p.text);
    // All indicators are composed last in native coordinates, never scaled.
    c.rect(318,0,2,478,p.panel);
    c.rect(318,56+screen*70,2,60,p.muted);
    c.rect(0,478,320,2,p.panel); c.rect(0,478,160,2,p.muted);
}
}
