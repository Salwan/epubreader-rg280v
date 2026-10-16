#include "app/controller.hpp"
#include "library/identity.hpp"
#include "ui/books_list.hpp"
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <iostream>
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
        bool native = true;
        setenv("SDL_VIDEO_KMSDRM_SCALING_MODE", "0", 1);
#else
        bool native = false;
#endif
        const char* home = std::getenv("HOME");
        const std::string homePath = epub::absolutePath(home ? home : ".");
        std::string assets = executableDirectory() + "/assets", stateDirectory = homePath + "/.epubreader";
        std::string book, smoke, booksFolder, musicFolder;
        for (int i = 1; i < argc; ++i) {
            const std::string arg = argv[i];
            auto value = [&]() -> std::string { if (++i >= argc) throw std::runtime_error("Missing value for " + arg); return argv[i]; };
            if (arg == "--assets") assets = epub::absolutePath(value());
            else if (arg == "--state-dir") stateDirectory = epub::absolutePath(value());
            else if (arg == "--book") book = epub::absolutePath(value());
            else if (arg == "--books-dir") booksFolder = epub::absolutePath(value());
            else if (arg == "--music-dir") musicFolder = epub::absolutePath(value());
            else if (arg == "--smoke-test") smoke = epub::absolutePath(value());
            else if (arg == "--native-window") native = true;
            else if (arg == "--help") {
                std::cout << "EPUB Reader " EPUBREADER_VERSION " / Milestone 3\n"
                    << "--assets DIR --state-dir DIR --book FILE --books-dir DIR --music-dir DIR --native-window --smoke-test DIR\n"
                    << "Default: books list, or the last successfully opened reader session.\n"
                    << "List: arrows select/page; A opens; X settings/folders; E/R themes.\n"
                    << "Reader: left/right or Q/W pages; X menu; E/R themes.\n"
                    << "Menu: up/down selects; left/right adjusts Font size; A activates; B/X closes.\n"
                    << "Escape+Enter: save and exit everywhere. Folder overrides are saved.\n";
                return 0;
            } else if (!arg.empty() && arg[0] != '-' && book.empty()) book = epub::absolutePath(arg);
            else throw std::runtime_error("Unknown argument: " + arg);
        }
        epub::ensureDirectory(stateDirectory);
        std::cout.setf(std::ios::unitbuf);
        if (!std::freopen((stateDirectory + "/reader.log").c_str(), "w", stdout) ||
            !std::freopen((stateDirectory + "/stderr.log").c_str(), "w", stderr)) throw std::runtime_error("Cannot open reader logs");
        std::cout << "EPUBREADER " EPUBREADER_VERSION " Milestone 3\nassets=" << assets << "\nstate=" << stateDirectory << '\n';
        epub::Store store(stateDirectory, smoke.empty() ? homePath : stateDirectory + "/home");
        if (!booksFolder.empty()) store.config.books = booksFolder;
        if (!musicFolder.empty()) store.config.music = musicFolder;
        if (!smoke.empty()) store.config.preferences = {};
        epub::ReaderWorker worker(assets, stateDirectory);
        std::signal(SIGINT, signalHandler); std::signal(SIGTERM, signalHandler);
        SDLGuard guard;
        if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) throw std::runtime_error(SDL_GetError());
        SDL_WM_SetCaption("EPUB Reader - RG280V Milestone 3", nullptr);
        SDL_ShowCursor(SDL_DISABLE); SDL_EnableKeyRepeat(0, 0);
        SDL_Surface* display = SDL_SetVideoMode(native ? 320 : 640, 480, 16,
            native && smoke.empty() ? SDL_HWSURFACE | SDL_DOUBLEBUF : SDL_SWSURFACE);
        if (!display || display->w != (native ? 320 : 640) || display->h != 480) throw std::runtime_error("SDL display initialization failed");
        epub::Controller app(store, worker, book);
        epub::Canvas canvas;
        epub::Input input;
        auto consumeTransition = [&] { if (app.transition()) { input.consumeA(); input.consumeRepeat(); } };
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
            wait(); dispatch(epub::Action::X); dispatch(epub::Action::Down);
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
            }
            dispatch(epub::Action::X); dispatch(epub::Action::Down); shot("info");
            present(canvas, display, native); app.shutdown();
            std::cout << "SMOKE PASS: production controller, popup font controls, themes, native composition, JSON state\n"; return 0;
        }
        uint64_t lastLoop = suspendClock();
        bool running = true;
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
            if (app.changed()) { epub::renderApplication(canvas, app); present(canvas, display, native); }
            SDL_Delay(8);
        }
        app.shutdown(); std::cout << "Clean exit: production session and last completed text anchor saved\n"; return 0;
    } catch (const std::exception& error) { std::cerr << "FATAL: " << error.what() << '\n'; return 1; }
}
