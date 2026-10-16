#pragma once
#include "storage/state.hpp"
#include <alsa/asoundlib.h>
#include <cstdint>
#include <string>
#include <vector>

namespace epub {
struct MixerLevel {
    std::string name, id;
    unsigned index = 0;
    long minimum = 0, maximum = 0, left = 0, right = 0;
    int switchLeft = -1, switchRight = -1;
    int percent() const;
    MixerLevel restored(const State& saved) const;
    bool checkpoint(State& state) const;
};
class VolumeCheckpoint {
    bool dirty_ = false;
    uint32_t since_ = 0;
public:
    void observe(const MixerLevel* level, State& preferences, uint32_t now);
    bool due(uint32_t now);
};
class Mixer {
    snd_mixer_t* handle_ = nullptr;
    std::vector<snd_mixer_elem_t*> elements_;
    std::vector<MixerLevel> levels_;
    int selected_ = 0;
    unsigned changes_ = 0, writes_ = 0;
    std::string error_, restoreResult_ = "Observation only";
public:
    Mixer(const std::string& card, const State& saved, bool restore);
    ~Mixer();
    Mixer(const Mixer&) = delete;
    Mixer& operator=(const Mixer&) = delete;
    bool poll();
    void select(int delta);
    bool checkpoint(State& state) const;
    const std::vector<MixerLevel>& levels() const { return levels_; }
    const MixerLevel* selected() const;
    unsigned changes() const { return changes_; }
    unsigned writes() const { return writes_; }
    int selection() const { return selected_; }
    const std::string& error() const { return error_; }
    const std::string& restoreResult() const { return restoreResult_; }
};
}
