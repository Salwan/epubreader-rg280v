#include "app/controller.hpp"
#include "library/identity.hpp"
#include "ui/books_list.hpp"
#include "platform/mixer.hpp"
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <time.h>
#include <unistd.h>

namespace {
volatile sig_atomic_t interrupted = 0;
void signalHandler(int) { interrupted = 1; }
uint64_t suspendClock() {
    struct timespec time{};
    if (clock_gettime(CLOCK_BOOTTIME, &time) != 0) return SDL_GetTicks();
    return static_cast<uint64_t>(time.tv_sec) * 1000 + static_cast<uint64_t>(time.tv_nsec) / 1000000;
}
std::string executableDirectory() {
    char path[4096]; const ssize_t n = readlink("/proc/self/exe", path, sizeof(path) - 1);
    if (n <= 0) return ".";
    return epub::parentPath(std::string(path, static_cast<size_t>(n)));
}
struct SDLGuard { ~SDLGuard() { SDL_Quit(); } };
void present(epub::Canvas& canvas, SDL_Surface* display, bool native) {
    epub::Canvas preview(640, 480);
    if (!native) for (int y = 0; y < 480; ++y) for (int x = 0; x < 640; ++x)
        preview.pixel(x, y, canvas.pixels[static_cast<size_t>(y * 320 + x / 2)]);
    SDL_Surface* surface = native ? canvas.surface() : preview.surface();
    if (!surface) throw std::runtime_error(SDL_GetError());
    SDL_BlitSurface(surface, nullptr, display, nullptr); SDL_FreeSurface(surface);
    if (SDL_Flip(display) < 0) throw std::runtime_error(SDL_GetError());
}
}
int main(int argc, char** argv) {
    try {
        if (argc == 6 && std::string(argv[1]) == "--worker")
            return epub::readerWorkerMain(std::stoi(argv[2]), argv[3], argv[4], std::stoi(argv[5]));
#ifdef EPUBREADER_DEVICE
        bool native = true, restoreMixer = true;
        setenv("SDL_VIDEO_KMSDRM_SCALING_MODE", "0", 1);
#else
        bool native = false, restoreMixer = false;
#endif
        const char* home = std::getenv("HOME");
        const std::string homePath = epub::absolutePath(home ? home : ".");
        std::string assets = executableDirectory() + "/assets", stateDirectory = homePath + "/.epubreader";
        std::string book, smoke, booksFolder, musicFolder, mixerCard = "default";
        for (int i = 1; i < argc; ++i) {
            const std::string arg = argv[i];
            auto value = [&]() -> std::string { if (++i >= argc) throw std::runtime_error("Missing value for " + arg); return argv[i]; };
            if (arg == "--assets") assets = epub::absolutePath(value());
            else if (arg == "--state-dir") stateDirectory = epub::absolutePath(value());
            else if (arg == "--book") book = epub::absolutePath(value());
            else if (arg == "--books-dir") booksFolder = epub::absolutePath(value());
            else if (arg == "--music-dir") musicFolder = epub::absolutePath(value());
            else if (arg == "--mixer-card") mixerCard = value();
            else if (arg == "--restore-mixer") restoreMixer = true;
            else if (arg == "--observe-only") restoreMixer = false;
            else if (arg == "--smoke-test") smoke = epub::absolutePath(value());
            else if (arg == "--native-window") native = true;
            else if (arg == "--help") {
                std::cout << "EPUB Reader " EPUBREADER_VERSION " / Milestone 5\n"
                    << "--assets DIR --state-dir DIR --book FILE --books-dir DIR --music-dir DIR --native-window --smoke-test DIR\n"
                    << "--mixer-card CARD --restore-mixer --observe-only (host defaults to observation only)\n"
                    << "Default: books list, or the last successfully opened reader session.\n"
                    << "List: arrows select/page; A opens; X settings/folders; E/R themes.\n"
                    << "Reader: left/right or Q/W pages; X menu; E/R themes; Escape jump slider.\n"
                    << "Reader A: brief press starts/pauses/resumes music; hold 600ms skips once.\n"
                    << "Menu: up/down selects; left/right adjusts Font size; A activates; B/X closes.\n"
                    << "Jump: left/right -/+1%; up/down +/-10%; brief A jumps; B/Escape cancels; X menu.\n"
                    << "Escape+Enter: save and exit everywhere. Music scans once at startup; folder changes apply next launch.\n";
                return 0;
            } else if (!arg.empty() && arg[0] != '-' && book.empty()) book = epub::absolutePath(arg);
            else throw std::runtime_error("Unknown argument: " + arg);
        }
        epub::ensureDirectory(stateDirectory);
        std::cout.setf(std::ios::unitbuf);
        if (!std::freopen((stateDirectory + "/reader.log").c_str(), "w", stdout) ||
            !std::freopen((stateDirectory + "/stderr.log").c_str(), "w", stderr)) throw std::runtime_error("Cannot open reader logs");
        std::cout << "EPUBREADER " EPUBREADER_VERSION " Milestone 5\nassets=" << assets << "\nstate=" << stateDirectory << '\n';
        epub::Store store(stateDirectory, smoke.empty() ? homePath : stateDirectory + "/home");
        if (!booksFolder.empty()) store.config.books = booksFolder;
        if (!musicFolder.empty()) store.config.music = musicFolder;
        if (!smoke.empty()) store.config.preferences = {};
        epub::ReaderWorker worker(assets, stateDirectory);
        std::signal(SIGINT, signalHandler); std::signal(SIGTERM, signalHandler);
        SDLGuard guard;
        if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) throw std::runtime_error(SDL_GetError());
        SDL_WM_SetCaption("EPUB Reader - RG280V Milestone 5", nullptr);
        SDL_ShowCursor(SDL_DISABLE); SDL_EnableKeyRepeat(0, 0);
        SDL_Surface* display = SDL_SetVideoMode(native ? 320 : 640, 480, 16,
            native && smoke.empty() ? SDL_HWSURFACE | SDL_DOUBLEBUF : SDL_SWSURFACE);
        if (!display || display->w != (native ? 320 : 640) || display->h != 480) throw std::runtime_error("SDL display initialization failed");
        std::unique_ptr<epub::Mixer> mixer;
        if (smoke.empty()) {
            mixer = std::make_unique<epub::Mixer>(mixerCard, store.config.preferences, restoreMixer);
            std::cout << "MIXER START " << mixer->restoreResult() << " error=" << mixer->error() << " setters=" << mixer->writes() << '\n';
            for (const auto& l : mixer->levels()) std::cout << "CONTROL " << l.id << " range=" << l.minimum << ':' << l.maximum << '\n';
            if (restoreMixer && mixer->checkpoint(store.config.preferences)) store.saveConfig();
        }
        epub::Audio audio;
        epub::Music music(audio, store.config.music);
        epub::Controller app(store, worker, book, &music);
        auto shutdown = [&] {
            music.endBook();
            if (mixer) {
                mixer->poll(); if (restoreMixer) mixer->checkpoint(store.config.preferences);
                std::cout << "MIXER EXIT setters=" << mixer->writes() << " changes=" << mixer->changes() << '\n';
            }
            app.shutdown();
            const auto s = audio.status();
            std::cout << "AUDIO EXIT " << s.frequency << "Hz channels=" << s.channels << " underruns=" << s.underruns << '\n';
        };
        epub::Canvas canvas;
        epub::Input input;
        auto consumeTransition = [&] { app.consumeTransition(input); };
        auto dispatch = [&](epub::Action a) { app.action(a); consumeTransition(); };
        if (!smoke.empty()) {
            if (book.empty()) throw std::runtime_error("Reader smoke requires --book FILE");
            epub::ensureDirectory(smoke);
            auto wait = [&] {
                const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
                do { app.tick(SDL_GetTicks()); consumeTransition(); SDL_Delay(1); }
                while (app.busy() && std::chrono::steady_clock::now() < deadline);
                if (app.busy() || app.screen() != epub::Screen::Reader || app.page.pixels.empty() || !app.notice.empty())
                    throw std::runtime_error("Reader smoke failed: " + app.notice + ' ' + app.status);
            };
            auto shot = [&](const std::string& name) { epub::renderApplication(canvas, app); canvas.saveBmp(smoke + '/' + name + ".bmp"); };
            wait();
            const uint32_t scanSince = SDL_GetTicks();
            while (music.snapshot().scan == epub::MusicScan::Scanning && SDL_GetTicks() - scanSince < 5000) { app.tick(SDL_GetTicks()); SDL_Delay(1); }
            if (music.snapshot().scan == epub::MusicScan::Ready) {
                dispatch(epub::Action::AShort); app.tick(SDL_GetTicks()); SDL_Delay(100);
                if (!audio.status().playedFrames) throw std::runtime_error("Production music smoke did not play PCM");
                dispatch(epub::Action::AShort); dispatch(epub::Action::ALong); app.tick(SDL_GetTicks());
                if (audio.status().playback != epub::Playback::Paused) throw std::runtime_error("Production paused skip failed");
                dispatch(epub::Action::X); shot("music-paused"); dispatch(epub::Action::B); dispatch(epub::Action::AShort);
            }
            dispatch(epub::Action::X); dispatch(epub::Action::Down);
            for (int size = 0; size < 3; ++size) {
                while (store.config.preferences.font != size) { dispatch(store.config.preferences.font < size ? epub::Action::Right : epub::Action::Left); wait(); }
                if (app.readerControls.selected() != epub::ReaderMenuItem::FontSize) throw std::runtime_error("Font menu selection drifted");
                shot("font-menu-" + std::to_string(size));
                dispatch(epub::Action::B); shot("font-" + std::to_string(size));
                dispatch(epub::Action::X); dispatch(epub::Action::Down);
            }
            dispatch(epub::Action::B);
            for (size_t theme = 0; theme < epub::palettes.size(); ++theme) {
                while (store.config.preferences.theme != static_cast<int>(theme)) { dispatch(epub::Action::R2); wait(); }
                shot(epub::palettes[theme].name);
                dispatch(epub::Action::X); shot(std::string(epub::palettes[theme].name) + "-menu"); dispatch(epub::Action::B);
                dispatch(epub::Action::Select); shot(std::string(epub::palettes[theme].name) + "-jump-origin");
                for (int n = 0; n < 20; ++n) dispatch(epub::Action::Down);
                shot(std::string(epub::palettes[theme].name) + "-jump-zero");
                for (int n = 0; n < 5; ++n) dispatch(epub::Action::Up);
                shot(std::string(epub::palettes[theme].name) + "-jump-middle");
                for (int n = 0; n < 5; ++n) dispatch(epub::Action::Up);
                shot(std::string(epub::palettes[theme].name) + "-jump-hundred");
                dispatch(epub::Action::B);
            }
            dispatch(epub::Action::X); dispatch(epub::Action::Down); shot("info");
            present(canvas, display, native); shutdown();
            std::cout << "SMOKE PASS: production music, popup font controls, six-theme menus/slider previews, native composition, JSON state\n"; return 0;
        }
        uint64_t lastLoop = suspendClock();
        bool running = true;
        epub::VolumeCheckpoint volume;
        uint32_t volumePoll = 0;
        while (running && !app.quit() && !interrupted) {
            const uint32_t now = SDL_GetTicks(); const uint64_t clock = suspendClock();
            if (clock - lastLoop > 1500) input.reset();
            lastLoop = clock;
            SDL_Event event;
            while (SDL_PollEvent(&event)) {
                if (event.type == SDL_QUIT) running = false;
                else if (event.type == SDL_ACTIVEEVENT && !event.active.gain) input.reset();
                else if (event.type == SDL_KEYDOWN || event.type == SDL_KEYUP) dispatch(input.key(event.key.keysym.sym, event.type == SDL_KEYDOWN, now));
                if (app.quit()) break;
            }
            if (!running || app.quit()) break;
            dispatch(input.tick(now)); app.tick(now); consumeTransition();
            if (mixer && now - volumePoll >= 100) {
                mixer->poll(); volumePoll = now;
                if (restoreMixer) volume.observe(mixer->selected(), store.config.preferences, now);
            }
            if (volume.due(now)) store.saveConfig();
            if (app.changed()) { epub::renderApplication(canvas, app); present(canvas, display, native); }
            SDL_Delay(8);
        }
        shutdown(); std::cout << "Clean exit: music stopped, volume/session/last completed text anchor saved\n"; return 0;
    } catch (const std::exception& error) { std::cerr << "FATAL: " << error.what() << '\n'; return 1; }
}
