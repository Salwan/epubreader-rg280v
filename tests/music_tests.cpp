#include "audio/music.hpp"
#include "platform/mixer.hpp"
#include "storage/state.hpp"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <stdexcept>
#include <sys/stat.h>
#include <unistd.h>

namespace {
void check(bool ok, const std::string& message) { if (!ok) throw std::runtime_error(message); }
template<class F> void wait(F done, unsigned timeout = 4000) {
    const auto start = SDL_GetTicks();
    while (!done()) { check(SDL_GetTicks() - start < timeout, "Music timeout"); SDL_Delay(2); }
}
void playlist() {
    epub::Playlist p(25); p.catalog(7);
    std::optional<size_t> last;
    for (int cycle = 0; cycle < 100; ++cycle) {
        std::set<size_t> seen;
        for (int n = 0; n < 7; ++n) {
            const auto v = p.next(); check(v && *v < 7 && seen.insert(*v).second, "Not a permutation");
            if (!n) check(v != last, "Shuffle boundary repeats");
            last = v;
        }
    }
    for (size_t n = 0; n < 7; ++n) p.fail(n);
    check(!p.next(), "Invalid playlist did not exhaust"); p.reset(); check(!p.next(), "Reset retried failed tracks");
    p.catalog(1); check(p.next() == 0 && p.next() == 0, "Single track must repeat");
    p.catalog(0); check(!p.next(), "Empty playlist");
}
void volume() {
    using namespace epub;
    MixerLevel pcm; pcm.id = "PCM,0"; pcm.minimum = 0; pcm.maximum = 31;
    State s; s.theme = 5; s.font = 2;
    const auto initial = pcm.restored(s); check(initial.left == 15 && initial.right == 15, "Initial raw-range percentage");
    check(initial.checkpoint(s) && !initial.checkpoint(s), "Snapshot change detection");
    s.rawLeft = 19; s.rawRight = 17;
    auto exact = pcm.restored(s); check(exact.left == 19 && exact.right == 17, "Raw stereo restoration");
    pcm.maximum = 100; const auto fallback = pcm.restored(s);
    check(fallback.left == s.volume && fallback.right == s.volume && fallback.switchLeft == -1, "Range fallback");
    pcm.maximum = 31; pcm.left = 20; pcm.right = 21;
    VolumeCheckpoint pending; pending.observe(nullptr, s, 100); check(!pending.due(1000), "Absent mixer dirtied config");
    pending.observe(&pcm, s, 2000); check(!pending.due(2249) && pending.due(2250) && !pending.due(2500), "Volume debounce");
    check(s.theme == 5 && s.font == 2 && s.rawRight == 21, "Volume overwrote theme/font");
    pcm.left = 22; pending.observe(&pcm, s, 0xffffff80U); check(!pending.due(120) && pending.due(122), "Volume debounce tick wrap");
}
void discovery(const std::string& root) {
    namespace fs = std::filesystem;
    fs::create_directories(root + "/nested.mp3");
    std::ofstream(root + "/Café song.MP3") << "candidate";
    std::ofstream(root + "/not.mp4") << "ignored";
    std::ofstream(root + "/nested.mp3/hidden.mp3") << "ignored";
    fs::create_symlink(root + "/Café song.MP3", root + "/alias.mp3");
    fs::create_symlink(root + "/missing", root + "/broken.mp3");
    check(::mkfifo((root + "/pipe.mp3").c_str(), 0600) == 0, "FIFO fixture");
    auto result = epub::scanMusic(root);
    check(result.state == epub::MusicScan::Ready && result.tracks.size() == 2 && std::is_sorted(result.tracks.begin(), result.tracks.end()), "Discovery matching/symlinks/order");
    check(epub::scanMusic(root + "/absent").state == epub::MusicScan::Failed, "Missing directory outcome");
    fs::create_directory(root + "/empty"); check(epub::scanMusic(root + "/empty").state == epub::MusicScan::Empty, "Empty outcome");
    fs::create_directory(root + "/denied");
    fs::permissions(root + "/denied", fs::perms::none);
    const auto denied = epub::scanMusic(root + "/denied");
    fs::permissions(root + "/denied", fs::perms::owner_all);
    if (::geteuid() != 0) check(denied.state == epub::MusicScan::Failed, "Unreadable folder outcome");
    std::atomic<bool> cancel{true}; check(epub::scanMusic(root, &cancel).state == epub::MusicScan::Scanning, "Cancellation published catalog");
    epub::MusicScanner scanner(root);
    wait([&] { auto r = scanner.poll(); if (!r) return false; check(r->tracks.size() == 2, "Async catalog"); return true; });
    check(!scanner.poll(), "Scan publishes more than once");
    const auto start = SDL_GetTicks(); { epub::MusicScanner canceled(root); canceled.stop(); }
    check(SDL_GetTicks() - start < 500, "Scanner shutdown bound");
    fs::create_directory(root + "/large");
    for (int n = 0; n < 1000; ++n) std::ofstream(root + "/large/track-" + std::to_string(n) + ".mp3");
    const auto large = epub::scanMusic(root + "/large");
    check(large.tracks.size() == 1000, "Large catalog lost entries");
    std::cout << "SCAN tracks=1000 ms=" << large.elapsedMs << '\n';
}
void backend(const std::string& fixtures) {
    using namespace epub;
    Audio a; check(a.available(), "Dummy audio unavailable");
    for (const auto& name : {"short-stereo.mp3", "short-mono.mp3", "short-48k.mp3"}) {
        const auto gen = a.start(fixtures + '/' + name, Playback::Paused);
        SDL_Delay(60); auto paused = a.status();
        check(paused.playback == Playback::Paused && paused.playedFrames == 0 && paused.generation == gen, "Start-paused leaked samples");
        a.pause(false);
        wait([&] { return a.status().playback == Playback::Stopped; });
        const auto s = a.status(); const auto event = a.terminal();
        check(event && event->generation == gen && event->reason == AudioEnd::Finished && !a.terminal(), "EOF mailbox/generation");
        check(s.decodedFrames == s.playedFrames && s.playedFrames >= 13000 && !s.queuedFrames, "EOF discarded tail or conversion failed");
        std::cout << "AUDIO " << name << " decoded=" << s.decodedFrames << " played=" << s.playedFrames << " underruns=" << s.underruns << '\n';
    }
    const auto playSince = SDL_GetTicks();
    a.start(fixtures + "/short-stereo.mp3"); wait([&] { return a.status().playedFrames > 0; });
    std::cout << "FIRST PLAY ms=" << SDL_GetTicks() - playSince << '\n';
    a.pause(true); const auto position = a.status().playedFrames; SDL_Delay(80);
    check(a.status().playedFrames == position, "Pause consumed music");
    a.chirp(); SDL_Delay(60); check(a.status().playedFrames == position, "Chirp resumed paused music");
    a.pause(false); wait([&] { return a.status().playedFrames > position; });
    for (int n = 0; n < 60; ++n) { a.start(fixtures + "/short-mono.mp3"); if (n % 2) SDL_Delay(1); a.stop(); }
    SDL_Delay(60); check(a.status().playback == Playback::Stopped && !a.status().queuedFrames && !a.terminal(), "Stale decoder resurrected stop");
    const auto gen = a.start(fixtures + "/absent.mp3");
    wait([&] { return a.status().playback == Playback::Stopped; });
    const auto bad = a.terminal(); check(bad && bad->generation == gen && bad->reason == AudioEnd::Failed, "Typed failure");
    a.start(fixtures + "/short-stereo.mp3"); wait([&] { return a.status().playedFrames > 0; });
    const auto stopSince = std::chrono::steady_clock::now(); a.stop();
    std::cout << "STOP us=" << std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - stopSince).count() << '\n';
    check(a.status().queuedFrames == 0, "Stop left buffered PCM");
}
void sessions(const std::string& root, const std::string& fixtures) {
    using namespace epub;
    namespace fs = std::filesystem;
    fs::create_directories(root);
    fs::copy_file(fixtures + "/short-stereo.mp3", root + "/one.mp3");
    fs::copy_file(fixtures + "/short-mono.mp3", root + "/two.mp3");
    Audio a; Music m(a, root, 12); m.beginBook(); m.toggle(); // scan not yet delivered
    wait([&] { m.tick(); return m.snapshot().scan != MusicScan::Scanning; });
    check(m.snapshot().playback == Playback::Stopped, "Late scan auto-started music");
    fs::copy_file(fixtures + "/short-48k.mp3", root + "/added.mp3");
    m.skip(); m.tick(); check(m.snapshot().playback == Playback::Stopped, "Stopped skip started");
    m.toggle(); m.tick(); const auto first = m.snapshot().track;
    wait([&] { m.tick(); return a.status().playedFrames > 0; });
    m.toggle(); const auto position = a.status().playedFrames; m.tick(); SDL_Delay(50);
    check(m.snapshot().playback == Playback::Paused && a.status().playedFrames == position, "Session pause");
    m.skip(); m.tick(); SDL_Delay(60);
    check(m.snapshot().track != first && m.snapshot().playback == Playback::Paused && a.status().playedFrames == 0, "Paused skip leaked/failed");
    m.toggle(); m.tick();
    auto previous = m.snapshot().track; unsigned changes = 0;
    wait([&] { m.tick(); if (previous != m.snapshot().track) { ++changes; previous = m.snapshot().track; } return changes >= 4; });
    check(m.snapshot().count == 2, "Catalog rescanned after startup");
    // EOF already published, then pause before the service receives it.
    wait([&] { return a.status().playback == Playback::Stopped; });
    m.toggle(); m.tick(); SDL_Delay(50);
    check(m.snapshot().playback == Playback::Paused && a.status().playedFrames == 0, "Pause/EOF race lost intent");
    m.toggle(); m.tick(); wait([&] { return a.status().playback == Playback::Stopped; });
    m.endBook(); m.tick(); check(m.snapshot().playback == Playback::Stopped && a.status().queuedFrames == 0, "Stop/EOF race restarted");
    m.beginBook(); m.tick(); check(m.snapshot().playback == Playback::Stopped, "New book auto-started");
    m.toggle(); m.tick(); m.endBook(); SDL_Delay(70); m.tick();
    check(a.status().playback == Playback::Stopped, "Old completion restarted another session");
    std::cout << "SESSION automatic transitions=" << changes << " catalog=" << m.snapshot().count << '\n';
}
void invalid(const std::string& root) {
    namespace fs = std::filesystem;
    fs::create_directories(root); std::ofstream(root + "/bad.mp3") << "corrupt"; std::ofstream(root + "/zero.mp3");
    std::ofstream(root + "/removed.mp3");
    epub::Audio a; epub::Music m(a, root, 8); m.beginBook();
    wait([&] { m.tick(); return m.snapshot().scan != epub::MusicScan::Scanning; });
    fs::remove(root + "/removed.mp3"); m.toggle();
    wait([&] { m.tick(); return m.snapshot().playback == epub::Playback::Stopped; });
    check(m.snapshot().detail.find("No playable") != std::string::npos, "All-invalid did not exhaust");
    const auto generation = a.status().generation;
    for (int n = 0; n < 20; ++n) { m.tick(); SDL_Delay(2); }
    check(a.status().generation == generation, "Invalid playlist spins");
    m.toggle(); m.tick(); check(m.snapshot().playback == epub::Playback::Stopped, "Explicit retry spins");
}
}
int main(int argc, char** argv) {
    try {
        check(argc == 3, "fixtures and isolated directory required");
        check(SDL_Init(SDL_INIT_TIMER) == 0, "SDL timer init");
        std::filesystem::remove_all(argv[2]); epub::ensureDirectory(argv[2]);
        playlist(); volume(); discovery(std::string(argv[2]) + "/scan"); backend(argv[1]);
        sessions(std::string(argv[2]) + "/sessions", argv[1]); invalid(std::string(argv[2]) + "/invalid");
        SDL_Quit(); std::cout << "PASS: startup discovery, shuffle sessions, real conversion/drain, pause/skip, EOF races, failure exhaustion\n"; return 0;
    } catch (const std::exception& e) { std::cerr << "FAIL: " << e.what() << '\n'; SDL_Quit(); return 1; }
}
