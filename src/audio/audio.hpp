#pragma once
#include <SDL.h>
#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace epub {
enum class Playback { Stopped, Playing, Paused };
enum class AudioEnd { Finished, Failed };
struct AudioTerminal { unsigned generation = 0; AudioEnd reason = AudioEnd::Finished; std::string message; };
struct AudioStatus {
    Playback playback;
    std::string track, message;
    int frequency = 0, channels = 0;
    unsigned underruns = 0;
    unsigned generation = 0;
    uint64_t decodedFrames = 0, playedFrames = 0;
    size_t queuedFrames = 0;
};
// One producer, one SDL callback. The callback only consumes bounded PCM and
// a precomputed chirp; it performs no decoding, file access, or allocation.
class Audio {
    static constexpr size_t Capacity = 32768; // interleaved int16 samples
    std::array<int16_t, Capacity> pcm_{};
    size_t read_ = 0, write_ = 0, queued_ = 0, chirpAt_ = 0;
    std::vector<int16_t> chirp_;
    SDL_AudioSpec spec_{};
    bool opened_ = false, mpgInitialized_ = false, primed_ = false;
    bool draining_ = false;
    unsigned underruns_ = 0;
    uint64_t playedFrames_ = 0;
    uint64_t decodedFrames_ = 0; // Protected by SDL audio lock (MIPS32 has no lock-free 64-bit atomics).
    std::atomic<Playback> mode_{Playback::Stopped};
    std::atomic<unsigned> generation_{0};
    std::atomic<bool> exiting_{false};
    std::mutex infoMutex_;
    std::string path_, message_ = "Stopped";
    std::optional<AudioTerminal> terminal_;
    std::thread worker_;
    static void callback(void* user, Uint8* stream, int length);
    void decode();
    void report(unsigned generation, const std::string& value, std::optional<AudioEnd> end = {});
public:
    Audio();
    ~Audio();
    Audio(const Audio&) = delete;
    Audio& operator=(const Audio&) = delete;
    bool available() const { return opened_; }
    unsigned start(const std::string& path, Playback initial = Playback::Playing);
    void pause(bool paused);
    void toggle(const std::string& path);
    void stop();
    void chirp();
    AudioStatus status();
    std::optional<AudioTerminal> terminal();
};
std::vector<std::string> discoverMp3(const std::string& directory);
}
