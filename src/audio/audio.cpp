#include "audio/audio.hpp"
#include "audio/catalog.hpp"
#include <mpg123.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace epub {
std::vector<std::string> discoverMp3(const std::string& directory) {
    return scanMusic(directory).tracks;
}
Audio::Audio() {
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) { message_ = SDL_GetError(); return; }
    if (mpg123_init() != MPG123_OK) { message_ = "mpg123 initialization failed"; return; }
    mpgInitialized_ = true;
    SDL_AudioSpec wanted{};
    wanted.freq = 44100; wanted.format = AUDIO_S16SYS; wanted.channels = 2; wanted.samples = 1024;
    wanted.callback = callback; wanted.userdata = this;
    if (SDL_OpenAudio(&wanted, &spec_) != 0) { message_ = SDL_GetError(); return; }
    if (spec_.format != AUDIO_S16SYS || (spec_.channels != 1 && spec_.channels != 2) || spec_.freq <= 0) {
        SDL_CloseAudio(); message_ = "Unsupported negotiated audio format"; return;
    }
    opened_ = true;
    const size_t frames = static_cast<size_t>(spec_.freq * 0.045);
    chirp_.resize(frames);
    constexpr double pi = 3.14159265358979323846;
    for (size_t i = 0; i < frames; ++i) {
        const double t = static_cast<double>(i) / spec_.freq;
        const double env = std::sin(pi * static_cast<double>(i) / static_cast<double>(frames));
        chirp_[i] = static_cast<int16_t>(1800.0 * env * env * std::sin(2 * pi * (660 * t - 1300 * t * t)));
    }
    chirpAt_ = chirp_.size();
    worker_ = std::thread(&Audio::decode, this);
    SDL_PauseAudio(0);
}
Audio::~Audio() {
    stop();
    exiting_ = true;
    if (worker_.joinable()) worker_.join();
    if (opened_) SDL_CloseAudio();
    if (mpgInitialized_) mpg123_exit();
}
// All command/status paths take infoMutex_ before the SDL lock. The callback
// takes only SDL's lock and never touches strings or the terminal mailbox.
void Audio::report(unsigned gen, const std::string& value, std::optional<AudioEnd> end) {
    std::lock_guard<std::mutex> lock(infoMutex_);
    SDL_LockAudio();
    if (generation_ == gen) {
        if (end) {
            mode_ = Playback::Stopped; queued_ = read_ = write_ = 0; primed_ = false;
            terminal_ = AudioTerminal{gen, *end, value};
        }
        message_ = end ? value : mode_ == Playback::Paused ? "Paused" : value;
    }
    SDL_UnlockAudio();
}
unsigned Audio::start(const std::string& path, Playback initial) {
    if (!opened_) return 0;
    std::lock_guard<std::mutex> lock(infoMutex_);
    SDL_LockAudio();
    path_ = path; message_ = initial == Playback::Paused ? "Paused" : "Loading MP3"; terminal_.reset();
    ++generation_; queued_ = read_ = write_ = 0; primed_ = draining_ = false; decodedFrames_ = 0; playedFrames_ = 0;
    mode_ = initial == Playback::Paused ? Playback::Paused : Playback::Playing;
    const unsigned gen = generation_;
    SDL_UnlockAudio();
    return gen;
}
void Audio::pause(bool paused) {
    if (!opened_) return;
    std::lock_guard<std::mutex> lock(infoMutex_);
    SDL_LockAudio();
    if (mode_ != Playback::Stopped) {
        mode_ = paused ? Playback::Paused : Playback::Playing;
        message_ = paused ? "Paused" : "Playing";
    }
    SDL_UnlockAudio();
}
void Audio::toggle(const std::string& path) {
    if (!opened_) return;
    if (mode_ == Playback::Stopped) start(path);
    else pause(mode_ == Playback::Playing);
}
void Audio::stop() {
    if (!opened_) return;
    std::lock_guard<std::mutex> lock(infoMutex_);
    SDL_LockAudio(); ++generation_; mode_ = Playback::Stopped; queued_ = read_ = write_ = 0; primed_ = draining_ = false;
    terminal_.reset(); path_.clear(); message_ = "Stopped"; SDL_UnlockAudio();
}
void Audio::chirp() { if (opened_) { SDL_LockAudio(); chirpAt_ = 0; SDL_UnlockAudio(); } }
AudioStatus Audio::status() {
    std::lock_guard<std::mutex> lock(infoMutex_);
    AudioStatus s{}; s.frequency = spec_.freq; s.channels = spec_.channels;
    if (opened_) SDL_LockAudio();
    s.playback = mode_; s.generation = generation_; s.track = path_; s.message = message_;
    s.underruns = underruns_; s.decodedFrames = decodedFrames_; s.playedFrames = playedFrames_;
    s.queuedFrames = opened_ ? queued_ / spec_.channels : 0;
    if (opened_) SDL_UnlockAudio();
    return s;
}
std::optional<AudioTerminal> Audio::terminal() {
    std::lock_guard<std::mutex> lock(infoMutex_);
    auto result = std::move(terminal_); terminal_.reset(); return result;
}
void Audio::callback(void* user, Uint8* stream, int length) {
    auto& a = *static_cast<Audio*>(user);
    std::memset(stream, 0, static_cast<size_t>(length));
    auto* output = reinterpret_cast<int16_t*>(stream);
    const size_t count = static_cast<size_t>(length) / sizeof(int16_t);
    const bool playing = a.mode_ == Playback::Playing && a.primed_;
    bool underrun = false;
    for (size_t i = 0; i + a.spec_.channels <= count; i += a.spec_.channels) {
        const int chirp = a.chirpAt_ < a.chirp_.size() ? a.chirp_[a.chirpAt_++] : 0;
        bool frame = playing && a.queued_ >= a.spec_.channels;
        for (size_t ch = 0; ch < a.spec_.channels; ++ch) {
            int music = 0;
            if (frame) { music = a.pcm_[a.read_]; a.read_ = (a.read_ + 1) % Capacity; --a.queued_; }
            output[i+ch] = static_cast<int16_t>(std::clamp(music + chirp, -32768, 32767));
        }
        if (frame) ++a.playedFrames_;
        else if (playing) underrun = true;
    }
    if (underrun && !a.draining_) ++a.underruns_;
}
void Audio::decode() {
    mpg123_handle* decoder = nullptr;
    int descriptor = -1;
    auto close = [&] { if (decoder) mpg123_delete(decoder); decoder = nullptr; if (descriptor >= 0) ::close(descriptor); descriptor = -1; };
    unsigned active = 0;
    bool eof = false;
    std::array<unsigned char, 8192> buffer{};
    size_t have = 0, offset = 0;
    auto nap = [] { std::this_thread::sleep_for(std::chrono::milliseconds(5)); };
    while (!exiting_) {
        unsigned gen; Playback mode; std::string path;
        { std::lock_guard<std::mutex> lock(infoMutex_); gen = generation_; mode = mode_; if (gen != active) path = path_; }
        if (gen != active) {
            active = gen; eof = false; have = offset = 0;
            close();
            if (mode != Playback::Stopped) {
                int error = 0; decoder = mpg123_new(nullptr, &error);
                int result = decoder ? MPG123_OK : MPG123_ERR;
                if (decoder) {
                    mpg123_param(decoder, MPG123_ADD_FLAGS, MPG123_QUIET, 0);
                    result = mpg123_param(decoder, MPG123_FORCE_RATE, spec_.freq, 0);
                    if (result == MPG123_OK) result = mpg123_param(decoder, MPG123_ADD_FLAGS, spec_.channels == 2 ? MPG123_FORCE_STEREO : MPG123_MONO_MIX, 0);
                    if (result == MPG123_OK) result = mpg123_format_none(decoder);
                    if (result == MPG123_OK) result = mpg123_format(decoder, spec_.freq, spec_.channels == 2 ? MPG123_STEREO : MPG123_MONO, MPG123_ENC_SIGNED_16);
                    if (result == MPG123_OK) {
                        descriptor = ::open(path.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
                        struct stat st{};
                        result = descriptor >= 0 && ::fstat(descriptor, &st) == 0 && S_ISREG(st.st_mode)
                            ? mpg123_open_fd(decoder, descriptor) : MPG123_ERR;
                    }
                }
                if (result != MPG123_OK) {
                    report(active,"MP3 open failed",AudioEnd::Failed); close();
                } else report(active,"Playing");
            }
        }
        if (!decoder || mode_ != Playback::Playing) { nap(); continue; }
        if (offset == have && !eof) {
            size_t bytes = 0;
            const int result = mpg123_read(decoder, buffer.data(), buffer.size(), &bytes);
            if (result == MPG123_NEW_FORMAT) {
                long rate = 0; int channels = 0, encoding = 0;
                mpg123_getformat(decoder, &rate, &channels, &encoding);
                if (rate != spec_.freq || channels != spec_.channels || encoding != MPG123_ENC_SIGNED_16) {
                    report(active,"MP3 format negotiation failed",AudioEnd::Failed); close();
                    continue;
                }
            } else if (result != MPG123_OK && result != MPG123_DONE) {
                report(active,"MP3 decode failed",AudioEnd::Failed); close();
                continue;
            }
            have = bytes / sizeof(int16_t); offset = 0;
            SDL_LockAudio();
            if (generation_ == active) decodedFrames_ += have / spec_.channels;
            SDL_UnlockAudio();
            eof = result == MPG123_DONE;
        }
        SDL_LockAudio();
        if (generation_ == active && mode_ == Playback::Playing) {
            const size_t count = std::min(have - offset, Capacity - queued_);
            for (size_t i = 0; i < count; ++i) {
                int16_t sample; std::memcpy(&sample, buffer.data() + (offset+i) * sizeof(sample), sizeof(sample));
                pcm_[write_] = sample; write_ = (write_ + 1) % Capacity;
            }
            queued_ += count; offset += count;
            draining_ = eof && offset == have;
            if (queued_ >= 4096 || eof) primed_ = true;
        }
        const bool finished = generation_ == active && eof && offset == have && queued_ == 0;
        const bool empty = decodedFrames_ == 0;
        SDL_UnlockAudio();
        if (finished) { report(active, empty ? "MP3 contains no audio" : "End of track", empty ? AudioEnd::Failed : AudioEnd::Finished); close(); }
        nap();
    }
    close();
}
}
