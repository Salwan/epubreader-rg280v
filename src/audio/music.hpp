#pragma once
#include "audio/audio.hpp"
#include "audio/catalog.hpp"
#include "audio/playlist.hpp"

namespace epub {
struct MusicSnapshot {
    MusicScan scan = MusicScan::Scanning;
    Playback playback = Playback::Stopped;
    bool available = false;
    size_t count = 0, track = 0; // track is a one-based stable catalog index; 0 means none.
    std::string detail;
    std::string label() const;
};
// Main-thread port: controller tests inject a counting fake without SDL audio.
class MusicPort {
public:
    virtual ~MusicPort() = default;
    virtual void beginBook() = 0;
    virtual void endBook() = 0;
    virtual void toggle() = 0;
    virtual void skip() = 0;
    virtual void chirp() = 0;
    virtual bool tick() = 0;
    virtual MusicSnapshot snapshot() const = 0;
};
class Music : public MusicPort {
    Audio& audio_;
    MusicScanner scanner_;
    Playlist playlist_;
    MusicSnapshot snapshot_;
    std::vector<std::string> tracks_;
    bool active_ = false, advance_ = false, dirty_ = true;
    unsigned generation_ = 0;
public:
    Music(Audio& audio, const std::string& folder, unsigned seed = std::random_device{}());
    ~Music() override;
    void beginBook() override;
    void endBook() override;
    void toggle() override;
    void skip() override;
    void chirp() override { audio_.chirp(); }
    bool tick() override;
    MusicSnapshot snapshot() const override { return snapshot_; }
};
}
