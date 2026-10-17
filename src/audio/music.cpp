#include "audio/music.hpp"
#include <iostream>
#include <iomanip>

namespace epub {
std::string MusicSnapshot::label() const {
    if (!available) return "MUSIC: UNAVAILABLE";
    if (scan == MusicScan::Scanning) return "MUSIC: SCANNING";
    if (scan == MusicScan::Failed) return "MUSIC: FOLDER UNAVAILABLE";
    if (scan == MusicScan::Empty) return "MUSIC: NO MP3 FILES";
    if (playback == Playback::Stopped) return "MUSIC: STOPPED / " + std::to_string(count) + " TRACKS";
    return std::string("MUSIC: ") + (playback == Playback::Playing ? "PLAYING " : "PAUSED ") + std::to_string(track) + '/' + std::to_string(count);
}
Music::Music(Audio& audio, const std::string& folder, unsigned seed) : audio_(audio), scanner_(folder), playlist_(seed) {
    snapshot_.available = audio.available();
    if (!snapshot_.available) snapshot_.detail = audio.status().message;
    std::cout << "MUSIC SCAN folder=" << std::quoted(folder) << '\n';
}
Music::~Music() { endBook(); scanner_.stop(); }
void Music::beginBook() { endBook(); active_ = true; }
void Music::endBook() {
    active_ = false; advance_ = false; audio_.stop(); generation_ = 0;
    playlist_.reset(); snapshot_.playback = Playback::Stopped; snapshot_.track = 0; dirty_ = true;
}
void Music::toggle() {
    if (!active_ || !snapshot_.available || snapshot_.scan != MusicScan::Ready) return;
    if (snapshot_.playback == Playback::Stopped) { playlist_.reset(); advance_ = true; snapshot_.playback = Playback::Playing; }
    else {
        snapshot_.playback = snapshot_.playback == Playback::Playing ? Playback::Paused : Playback::Playing;
        audio_.pause(snapshot_.playback == Playback::Paused);
    }
    dirty_ = true;
}
void Music::skip() {
    if (!active_ || snapshot_.playback == Playback::Stopped) return;
    audio_.stop(); generation_ = 0; advance_ = true; dirty_ = true;
}
bool Music::tick() {
    if (auto catalog = scanner_.poll()) {
        tracks_ = std::move(catalog->tracks); snapshot_.count = tracks_.size(); snapshot_.scan = catalog->state;
        if (snapshot_.available) snapshot_.detail = catalog->error;
        playlist_.catalog(tracks_.size()); dirty_ = true;
        std::cout << "MUSIC SCANNED tracks=" << tracks_.size() << " ms=" << catalog->elapsedMs << " error=" << catalog->error << '\n';
    }
    if (auto event = audio_.terminal(); event && active_ && generation_ && event->generation == generation_) {
        generation_ = 0;
        if (event->reason == AudioEnd::Failed) {
            playlist_.fail(snapshot_.track - 1); snapshot_.detail = event->message;
            std::cerr << "MUSIC ERROR track=" << snapshot_.track << " " << event->message << '\n';
            advance_ = true;
        } else if (snapshot_.repeat == RepeatMode::Stop) {
            audio_.stop(); playlist_.reset(); snapshot_.playback = Playback::Stopped; snapshot_.track = 0; advance_ = false;
        } else if (snapshot_.repeat == RepeatMode::One && snapshot_.track) {
            generation_ = audio_.start(tracks_[snapshot_.track - 1], snapshot_.playback);
        } else advance_ = true;
        dirty_ = true;
    }
    if (active_ && advance_) {
        advance_ = false;
        if (const auto next = playlist_.next()) {
            snapshot_.track = *next + 1;
            generation_ = audio_.start(tracks_[*next], snapshot_.playback);
            std::cout << "MUSIC TRACK " << snapshot_.track << '/' << snapshot_.count << " generation=" << generation_
                << " paused=" << (snapshot_.playback == Playback::Paused) << '\n';
        } else {
            snapshot_.playback = Playback::Stopped; snapshot_.track = 0;
            snapshot_.detail = "No playable tracks; restart to retry"; audio_.stop();
        }
        dirty_ = true;
    }
    const bool changed = dirty_; dirty_ = false; return changed;
}
}
