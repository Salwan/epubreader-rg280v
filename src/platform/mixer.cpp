#include "platform/mixer.hpp"
#include <algorithm>
#include <iostream>

namespace epub {
int MixerLevel::percent() const {
    return maximum <= minimum ? 0 : static_cast<int>((left - minimum) * 100 / (maximum - minimum));
}
Mixer::Mixer(const std::string& card, const State& saved, bool restore) {
    int result = snd_mixer_open(&handle_, 0);
    if (result >= 0) result = snd_mixer_attach(handle_, card.c_str());
    if (result >= 0) result = snd_mixer_selem_register(handle_, nullptr, nullptr);
    if (result >= 0) result = snd_mixer_load(handle_);
    if (result < 0) { error_ = snd_strerror(result); return; }
    for (auto* e = snd_mixer_first_elem(handle_); e; e = snd_mixer_elem_next(e)) {
        if (!snd_mixer_selem_is_active(e) || !snd_mixer_selem_has_playback_volume(e)) continue;
        MixerLevel level;
        level.name = snd_mixer_selem_get_name(e); level.index = snd_mixer_selem_get_index(e);
        level.id = level.name + "," + std::to_string(level.index);
        snd_mixer_selem_get_playback_volume_range(e, &level.minimum, &level.maximum);
        elements_.push_back(e); levels_.push_back(level);
    }
    if (levels_.empty()) { error_ = "No playback-volume controls"; return; }
    bool found = false;
    for (size_t i = 0; i < levels_.size(); ++i) if (levels_[i].id == saved.mixer) { selected_ = static_cast<int>(i); found = true; }
    if (!found) for (const char* preferred : {"Master", "PCM", "Headphone", "Speaker"}) {
        for (size_t i = 0; i < levels_.size(); ++i) if (levels_[i].name == preferred) { selected_ = static_cast<int>(i); found = true; break; }
        if (found) break;
    }
    poll(); changes_ = 0;
    if (restore) {
        auto& l = levels_[static_cast<size_t>(selected_)];
        auto* e = elements_[static_cast<size_t>(selected_)];
        const bool exact = l.id == saved.mixer && l.minimum == saved.minimum && l.maximum == saved.maximum && saved.rawLeft >= l.minimum && saved.rawLeft <= l.maximum;
        const long left = exact ? saved.rawLeft : l.minimum + (l.maximum - l.minimum) * saved.volume / 100;
        const long right = exact && saved.rawRight >= l.minimum && saved.rawRight <= l.maximum ? saved.rawRight : left;
        // Startup restoration is the only mixer write path. Volume keys are
        // handled by pwswd; neither key handling nor polling calls a setter.
        result = snd_mixer_selem_set_playback_volume(e, SND_MIXER_SCHN_FRONT_LEFT, left); ++writes_;
        if (snd_mixer_selem_has_playback_channel(e, SND_MIXER_SCHN_FRONT_RIGHT)) {
            const int r = snd_mixer_selem_set_playback_volume(e, SND_MIXER_SCHN_FRONT_RIGHT, right); ++writes_;
            if (r < 0) result = r;
        }
        if (exact && snd_mixer_selem_has_playback_switch(e)) {
            if (saved.switchLeft >= 0) {
                const int r = snd_mixer_selem_set_playback_switch(e, SND_MIXER_SCHN_FRONT_LEFT, saved.switchLeft); ++writes_;
                if (r < 0) result = r;
            }
            if (saved.switchRight >= 0 && snd_mixer_selem_has_playback_channel(e, SND_MIXER_SCHN_FRONT_RIGHT)) {
                const int r = snd_mixer_selem_set_playback_switch(e, SND_MIXER_SCHN_FRONT_RIGHT, saved.switchRight); ++writes_;
                if (r < 0) result = r;
            }
        }
        restoreResult_ = result < 0 ? "Restore failed: " + std::string(snd_strerror(result)) : (exact ? "Saved raw level restored" : "Initial percent restored");
        poll(); changes_ = 0;
    }
}
Mixer::~Mixer() { if (handle_) snd_mixer_close(handle_); }
const MixerLevel* Mixer::selected() const { return levels_.empty() ? nullptr : &levels_[static_cast<size_t>(selected_)]; }
void Mixer::select(int delta) {
    if (!levels_.empty()) selected_ = (selected_ + delta + static_cast<int>(levels_.size())) % static_cast<int>(levels_.size());
}
bool Mixer::poll() {
    if (!handle_ || levels_.empty()) return false;
    const int events = snd_mixer_handle_events(handle_);
    if (events < 0 && events != -EAGAIN) { error_ = snd_strerror(events); return false; }
    bool changed = false;
    for (size_t i = 0; i < levels_.size(); ++i) {
        auto& l = levels_[i]; auto* e = elements_[i];
        long left = l.left, right = l.right;
        int sl = -1, sr = -1;
        const int read = snd_mixer_selem_get_playback_volume(e, SND_MIXER_SCHN_FRONT_LEFT, &left);
        if (read < 0) { error_ = snd_strerror(read); continue; }
        right = left;
        if (snd_mixer_selem_has_playback_channel(e, SND_MIXER_SCHN_FRONT_RIGHT)) snd_mixer_selem_get_playback_volume(e, SND_MIXER_SCHN_FRONT_RIGHT, &right);
        if (snd_mixer_selem_has_playback_switch(e)) {
            snd_mixer_selem_get_playback_switch(e, SND_MIXER_SCHN_FRONT_LEFT, &sl);
            sr = sl;
            if (snd_mixer_selem_has_playback_channel(e, SND_MIXER_SCHN_FRONT_RIGHT)) snd_mixer_selem_get_playback_switch(e, SND_MIXER_SCHN_FRONT_RIGHT, &sr);
        }
        if (left != l.left || right != l.right || sl != l.switchLeft || sr != l.switchRight) {
            std::cout << "MIXER " << l.id << " raw=" << left << ',' << right << " switch=" << sl << ',' << sr << '\n';
            changed = true;
        }
        l.left = left; l.right = right; l.switchLeft = sl; l.switchRight = sr;
    }
    if (changed) ++changes_;
    return changed;
}
void Mixer::checkpoint(State& s) const {
    if (const auto* l = selected()) {
        s.mixer = l->id; s.volume = l->percent(); s.minimum = l->minimum; s.maximum = l->maximum;
        s.rawLeft = l->left; s.rawRight = l->right; s.switchLeft = l->switchLeft; s.switchRight = l->switchRight;
    }
}
}
