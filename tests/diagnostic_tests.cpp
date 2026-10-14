#include "audio/audio.hpp"
#include "platform/input.hpp"
#include "storage/state.hpp"
#include "ui/canvas.hpp"
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool b, const char* message) { if (!b) throw std::runtime_error(message); }
void inputTests() {
    using namespace epub;
    Input i;
    require(i.key(SDLK_DOWN,true,100) == Action::Down,"initial down");
    require(i.key(SDLK_DOWN,true,120) == Action::None,"firmware repeat suppressed");
    require(i.tick(449) == Action::None,"repeat delay");
    require(i.tick(450) == Action::Down,"repeat starts at 350ms");
    require(i.tick(529) == Action::None,"repeat interval");
    require(i.tick(530) == Action::Down,"repeat 80ms");
    i.key(SDLK_DOWN,false,540);
    require(i.tick(900) == Action::None,"repeat released");
    require(i.key(SDLK_LCTRL,true,1000) == Action::None,"A press deferred");
    require(i.key(SDLK_LCTRL,false,1200) == Action::AShort,"short A release");
    i.key(SDLK_LCTRL,true,2000);
    require(i.tick(2599) == Action::None,"long threshold");
    require(i.tick(2600) == Action::ALong,"long A");
    require(i.tick(3000) == Action::None,"one long per hold");
    require(i.key(SDLK_LCTRL,false,3100) == Action::None,"long suppresses short");
    i.key(SDLK_LCTRL,true,4000); i.consumeA();
    require(i.key(SDLK_LCTRL,false,4200) == Action::None,"transition consumes A");
    i.key(SDLK_LCTRL,true,4300);
    require(i.key(SDLK_LCTRL,false,5000) == Action::ALong,"late release still long");
    i.key(SDLK_ESCAPE,true,5100);
    require(i.key(SDLK_RETURN,true,5200) == Action::Quit,"quit chord priority");
    i.reset(); require(i.heldNames() == "NONE","focus loss clears input");
    i.key(SDLK_RETURN,true,5300);
    require(i.key(SDLK_ESCAPE,true,5400) == Action::Quit,"quit chord either order");
    i.reset(); i.key(SDLK_UP,true,0xffffff00U);
    require(i.tick(94) == Action::Up,"SDL tick wraparound repeat");
}
void canvasTests() {
    using namespace epub;
    Canvas high(640,480), low(320,240), a, b;
    high.clear(0); low.clear(0);
    high.rect(0,479,640,1,0xffffff); a.transformFrom(high,true);
    require(a.pixels[478*320] == 0 && a.pixels[479*320] == 0xffffff,"high path retains individual native rows");
    low.rect(0,239,320,1,0xffffff); b.transformFrom(low,false);
    require(b.pixels[478*320] == 0xffffff && b.pixels[479*320] == 0xffffff,"logical path doubles rows");
    high.pixel(0,0,0xff0000); high.pixel(1,0,0x0000ff); a.transformFrom(high,true);
    require(a.pixels[0] == 0x7f007f,"box filter averages each color independently");
    a.clear(0); a.rect(318,0,2,478,0x123456); a.rect(0,478,320,2,0x112233);
    require(a.pixels[317] == 0 && a.pixels[318] == 0x123456 && a.pixels[319] == 0x123456,"two native columns");
    require(a.pixels[477*320] == 0 && a.pixels[478*320] == 0x112233 && a.pixels[479*320] == 0x112233,"two native bottom rows");
}
void stateTests(const std::string& dir) {
    using namespace epub;
    ensureDirectory(dir);
    State s; s.theme = 5; s.font = 2; s.volume = 63; s.mixer = "Headphone Playback,0";
    s.minimum = -31; s.maximum = 0; s.rawLeft = -12; s.rawRight = -11; s.switchLeft = 1; s.switchRight = 0;
    saveState(dir,s);
    const auto loaded = loadState(dir);
    require(loaded.mixer == s.mixer && loaded.rawLeft == -12 && loaded.rawRight == -11 && loaded.switchRight == 0,"exact mixer snapshot roundtrip");
    require(loaded.theme == 5 && loaded.font == 2 && loaded.volume == 63,"settings roundtrip");
    { std::ofstream interrupted(dir+"/state.ini.tmp"); interrupted << "version 1\ntheme 0\n"; }
    require(loadState(dir).theme == 5,"interrupted replacement retains last committed settings");
    { std::ofstream f(dir+"/state.ini"); f << "version 1\nvolume not-a-number\n"; }
    require(loadState(dir).volume == 50,"malformed state defaults");
    { std::ofstream f(dir+"/state.ini"); f << "version 900\ntheme 4\n"; }
    require(loadState(dir).theme == 0,"future schema defaults");
    { std::ofstream f(dir+"/state.ini"); f << "version 1\nvolume 1000\nfont -20\ntheme 100\n"; }
    require(loadState(dir).volume == 100 && loadState(dir).font == 0 && loadState(dir).theme == static_cast<int>(palettes.size()) - 1,"bounds validated");
    saveState(dir,s);
}
void audioTests(const std::string& assets) {
    using namespace epub;
    Audio audio;
    require(audio.available(),"SDL dummy audio available");
    const std::string track = assets+"/audio/test-tone.mp3";
    audio.start(track); audio.chirp();
    SDL_Delay(180);
    auto s = audio.status();
    require(s.decodedFrames > 0 && s.playedFrames > 0,"real MP3 decode reaches callback");
    require(s.queuedFrames <= 32768U/static_cast<unsigned>(s.channels),"PCM stays bounded");
    audio.toggle(track); SDL_Delay(50);
    s = audio.status(); const auto paused = s.playedFrames;
    SDL_Delay(100);
    require(audio.status().playedFrames == paused,"pause stops consumption");
    audio.toggle(track); SDL_Delay(80);
    require(audio.status().playedFrames > paused,"resume keeps playing");
    for (int n=0; n<12; ++n) { audio.start(track); SDL_Delay(2); audio.stop(); }
    SDL_Delay(80);
    require(audio.status().playback == Playback::Stopped && audio.status().queuedFrames == 0,"stale decodes cannot restart stopped playback");
    audio.start(assets+"/missing.mp3"); SDL_Delay(80);
    require(audio.status().playback == Playback::Stopped,"missing MP3 fails boundedly");
    audio.start(track); SDL_Delay(80);
    require(audio.status().decodedFrames > 0,"decoder recovers after invalid file");
    const uint32_t deadline = SDL_GetTicks() + 14000;
    while (audio.status().playback != Playback::Stopped && static_cast<int32_t>(SDL_GetTicks()-deadline) < 0) SDL_Delay(20);
    s = audio.status();
    require(s.playback == Playback::Stopped && s.message == "End of track" && s.queuedFrames == 0,"end of real MP3 drains PCM then stops");
    require(s.playedFrames > 44100*11U,"end of track does not discard buffered tail");
    audio.stop();
}
}
int main(int argc, char** argv) {
    try {
        require(argc == 3,"assets and isolated state dir required");
        require(SDL_Init(SDL_INIT_TIMER) == 0,"SDL init");
        inputTests(); canvasTests(); stateTests(argv[2]); audioTests(argv[1]);
        SDL_Quit(); std::cout << "PASS: input arbitration/timing, pixel transforms, atomic recovery, MP3 lifecycle\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << "FAIL: " << e.what() << '\n'; SDL_Quit(); return 1; }
}
