#include "platform/events.hpp"
#include "ui/diagnostic.hpp"
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <sys/utsname.h>
#include <time.h>
#include <unistd.h>

namespace {
volatile sig_atomic_t interrupted = 0;
void signalHandler(int) { interrupted = 1; }
uint64_t suspendClock() {
    struct timespec time{};
    if (clock_gettime(CLOCK_BOOTTIME,&time) != 0) return SDL_GetTicks();
    return static_cast<uint64_t>(time.tv_sec)*1000 + static_cast<uint64_t>(time.tv_nsec)/1000000;
}
std::string executableDirectory() {
    char path[4096];
    const ssize_t n = readlink("/proc/self/exe",path,sizeof(path)-1);
    if (n <= 0) return ".";
    path[n] = 0; std::string s(path); return s.substr(0,s.find_last_of('/'));
}
struct SDLGuard { ~SDLGuard() { TTF_Quit(); SDL_Quit(); } };
void present(epub::Canvas& canvas, SDL_Surface* display, bool native) {
    SDL_Surface* image = canvas.surface();
    if (native) SDL_BlitSurface(image,nullptr,display,nullptr);
    else {
        // Desktop window emulates the panel's half-height native pixels at 2x
        // logical size: every native row is one desktop row, columns doubled.
        epub::Canvas preview(640,480);
        for (int y=0; y<480; ++y) for (int x=0; x<640; ++x) preview.pixel(x,y,canvas.pixels[static_cast<size_t>(y*320+x/2)]);
        SDL_Surface* scaled = preview.surface(); SDL_BlitSurface(scaled,nullptr,display,nullptr); SDL_FreeSurface(scaled);
    }
    SDL_FreeSurface(image);
    if (SDL_Flip(display) < 0) throw std::runtime_error(SDL_GetError());
}
}
int main(int argc, char** argv) {
    try {
#ifdef EPUBREADER_DEVICE
        bool native = true, restore = true, evdev = true;
        setenv("SDL_VIDEO_KMSDRM_SCALING_MODE","0",1);
#else
        bool native = false, restore = false, evdev = false;
#endif
        const char* home = std::getenv("HOME");
        const std::string homePath = home ? home : ".";
        std::string assets = executableDirectory() + "/assets", stateDir = homePath + "/.epubreader/diagnostic", music = homePath + "/music", card = "default", smoke;
        for (int i=1; i<argc; ++i) {
            const std::string arg = argv[i];
            auto value = [&]() -> std::string { if (++i >= argc) throw std::runtime_error("Missing value for " + arg); return argv[i]; };
            if (arg == "--assets") assets = value();
            else if (arg == "--state-dir") stateDir = value();
            else if (arg == "--music-dir") music = value();
            else if (arg == "--mixer-card") card = value();
            else if (arg == "--smoke-test") { smoke = value(); restore = evdev = false; }
            else if (arg == "--native-window") native = true;
            else if (arg == "--restore-mixer") restore = true;
            else if (arg == "--observe-only") restore = false;
            else if (arg == "--evdev") evdev = true;
            else if (arg == "--help") {
                std::cout << "EPUB Reader Milestone 1 diagnostic\n"
                    << "--assets DIR --state-dir DIR --music-dir DIR --mixer-card CARD\n"
                    << "--native-window --restore-mixer --observe-only --evdev --smoke-test DIR\n"
                    << "Host: arrows, a/b/x/y, q/w=L1/R1, e/r=L2/R2, Esc+Enter=exit.\n";
                return 0;
            } else throw std::runtime_error("Unknown argument " + arg);
        }
        epub::ensureDirectory(stateDir);
        // Flush logs so a device crash still leaves the last successful step.
        std::cout.setf(std::ios::unitbuf);
        std::freopen((stateDir+"/diagnostic.log").c_str(),"w",stdout);
        std::freopen((stateDir+"/stderr.log").c_str(),"w",stderr);
        std::cout << "EPUBREADER " EPUBREADER_VERSION " Milestone 1\nassets=" << assets << "\nstate=" << stateDir << "\nmusic=" << music << '\n';
        std::signal(SIGINT,signalHandler); std::signal(SIGTERM,signalHandler);
        SDLGuard guard;
        if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_JOYSTICK) != 0) throw std::runtime_error(SDL_GetError());
        if (TTF_Init() != 0) throw std::runtime_error(TTF_GetError());
        SDL_WM_SetCaption("EPUB Reader - RG280V diagnostic",nullptr); SDL_ShowCursor(SDL_DISABLE); SDL_EnableKeyRepeat(0,0);
        const Uint32 flags = native && smoke.empty() ? SDL_HWSURFACE | SDL_DOUBLEBUF : SDL_SWSURFACE;
        SDL_Surface* display = SDL_SetVideoMode(native ? 320 : 640,480,16,flags);
        if (!display) throw std::runtime_error(SDL_GetError());
        if (display->w != (native ? 320 : 640) || display->h != 480) throw std::runtime_error("SDL returned unexpected surface geometry");
        char driver[64]{}; SDL_VideoDriverName(driver,sizeof(driver));
        struct utsname os{}; uname(&os);
        auto state = epub::loadState(stateDir);
        epub::Mixer mixer(card,state,restore);
        epub::Audio audio;
        epub::EventObserver observer(evdev);
        epub::Diagnostic ui(state,audio,mixer,assets,music);
        ui.system = std::string(os.machine) + " " + os.release;
        ui.video = std::string(driver) + " " + std::to_string(display->w) + "X" + std::to_string(display->h) + " BPP=" + std::to_string(display->format->BitsPerPixel) + " PITCH=" + std::to_string(display->pitch);
        ui.evdev = observer.summary;
        std::cout << ui.system << '\n' << ui.video << '\n' << ui.evdev << "\nMixer: " << mixer.error() << ' ' << mixer.restoreResult() << '\n';
        for (const auto& l : mixer.levels()) std::cout << "CONTROL " << l.id << " RANGE=" << l.minimum << ':' << l.maximum << '\n';
        auto checkpoint = [&] {
            mixer.checkpoint(state);
            try { epub::saveState(stateDir,state); ui.storageStatus = "State saved atomically"; }
            catch (const std::exception& e) { ui.storageStatus = "STATE ERROR: " + std::string(e.what()); std::cerr << ui.storageStatus << '\n'; }
        };
        checkpoint();
        epub::Canvas canvas;
        if (!smoke.empty()) {
            epub::ensureDirectory(smoke);
            for (int theme=0; theme<static_cast<int>(epub::palettes.size()); ++theme) for (int screen=0; screen<5; ++screen) {
                state.theme = theme; ui.screen = screen; ui.render(canvas);
                canvas.saveBmp(smoke + '/' + epub::palettes[static_cast<size_t>(theme)].name + '-' + std::to_string(screen) + ".bmp");
            }
            for (int high=0; high<2; ++high) for (int size=0; size<3; ++size) {
                state.theme = 0; state.font = size; ui.screen = 1; ui.highResolution = high != 0;
                ui.render(canvas); canvas.saveBmp(smoke+"/font-"+std::to_string(high)+'-'+std::to_string(size)+".bmp");
            }
            audio.start(ui.tracks[0]); audio.chirp(); SDL_Delay(500);
            const auto s = audio.status();
            if (s.decodedFrames == 0 || s.playedFrames == 0) throw std::runtime_error("Smoke MP3 decoding/callback failed: " + s.message);
            audio.stop(); state.theme = 0; state.font = 1; ui.screen = 0; ui.render(canvas); present(canvas,display,native);
            checkpoint(); std::cout << "SMOKE PASS: screens, fonts, MP3, callback, state\n"; return 0;
        }
        epub::Input input;
        bool running = true, dirty = true, stateDirty = false;
        uint32_t lastFrame = 0, lastPoll = 0, saveSince = 0;
        uint64_t lastLoop = suspendClock();
        auto dispatch = [&](epub::Action action) {
            if (action == epub::Action::Quit) running = false;
            else if (action != epub::Action::None) {
                ui.event("ACTION " + std::to_string(static_cast<int>(action)));
                const int previousScreen = ui.screen;
                if (ui.action(action)) { stateDirty = true; saveSince = SDL_GetTicks(); }
                if (ui.screen != previousScreen) input.consumeA();
                dirty = true;
            }
        };
        while (running && !interrupted) {
            const uint32_t now = SDL_GetTicks();
            const uint64_t bootNow = suspendClock();
            if (bootNow - lastLoop > 1500) { input.reset(); ui.event("RESUME GAP: HELD INPUT CLEARED"); dirty = true; }
            lastLoop = bootNow;
            SDL_Event event;
            while (SDL_PollEvent(&event)) {
                if (event.type == SDL_QUIT) running = false;
                else if (event.type == SDL_ACTIVEEVENT && !event.active.gain) { input.reset(); dirty = true; }
                else if (event.type == SDL_KEYDOWN || event.type == SDL_KEYUP) {
                    const bool down = event.type == SDL_KEYDOWN;
                    ui.event(std::string(down ? "DOWN " : "UP ") + epub::buttonName(epub::mapKey(event.key.keysym.sym)) + " SYM=" + std::to_string(event.key.keysym.sym) + " SCAN=" + std::to_string(event.key.keysym.scancode));
                    dispatch(input.key(event.key.keysym.sym,down,now)); dirty = true;
                } else if (event.type >= SDL_JOYAXISMOTION && event.type <= SDL_JOYBUTTONUP) { ui.event("SDL JOYSTICK EVENT " + std::to_string(event.type)); dirty = true; }
            }
            if (!running) break;
            dispatch(input.tick(now)); ui.held = input.heldNames();
            for (const auto& line : observer.poll()) { ui.event(line); dirty = true; }
            ui.volumePresses = observer.volumePresses;
            if (now - lastPoll >= 100) {
                if (mixer.poll()) { stateDirty = dirty = true; saveSince = now; }
                lastPoll = now;
            }
            if (stateDirty && now-saveSince >= 250) { checkpoint(); stateDirty = false; dirty = true; }
            if (dirty || (now-lastFrame >= 100 && (ui.screen == 3 || ui.screen == 4))) {
                ui.render(canvas); present(canvas,display,native); dirty = false; lastFrame = now;
            }
            SDL_Delay(8);
        }
        mixer.poll(); checkpoint(); audio.stop();
        std::cout << "Clean exit. Mixer writes=" << mixer.writes() << " changes=" << mixer.changes() << " volume-down-events=" << observer.volumePresses << '\n';
        return 0;
    } catch (const std::exception& e) { std::cerr << "FATAL: " << e.what() << '\n'; return 1; }
}
