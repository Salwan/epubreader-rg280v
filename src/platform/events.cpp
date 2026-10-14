#include "platform/events.hpp"
#include <array>
#include <fcntl.h>
#include <linux/input.h>
#include <sys/ioctl.h>
#include <unistd.h>

namespace epub {
EventObserver::EventObserver(bool enabled) {
    if (!enabled) return;
    constexpr size_t bits = sizeof(unsigned long) * 8;
    for (int n = 0; n < 32; ++n) {
        const std::string path = "/dev/input/event" + std::to_string(n);
        const int fd = open(path.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0) continue;
        std::array<unsigned long, (KEY_MAX + bits) / bits> keys{};
        if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(keys)), keys.data()) < 0) { close(fd); continue; }
        auto has = [&](int k) { return (keys[static_cast<size_t>(k) / bits] >> (static_cast<size_t>(k) % bits)) & 1; };
        if (!has(KEY_VOLUMEUP) && !has(KEY_VOLUMEDOWN) && !(has(KEY_LEFTCTRL) && has(KEY_ENTER))) { close(fd); continue; }
        files_.push_back(fd);
    }
    summary = files_.empty() ? "No readable evdev controls" : std::to_string(files_.size()) + " passive evdev source(s)";
}
EventObserver::~EventObserver() { for (int fd : files_) close(fd); }
std::vector<std::string> EventObserver::poll() {
    std::vector<std::string> result;
    std::array<input_event, 32> events{};
    for (size_t source = 0; source < files_.size(); ++source) {
        // Bounded work each frame even while a key repeats continuously.
        const ssize_t bytes = read(files_[source], events.data(), sizeof(events));
        if (bytes <= 0) continue;
        const size_t count = static_cast<size_t>(bytes) / sizeof(input_event);
        for (size_t i = 0; i < count; ++i) if (events[i].type == EV_KEY) {
            const auto& e = events[i];
            if ((e.code == KEY_VOLUMEUP || e.code == KEY_VOLUMEDOWN) && e.value == 1) ++volumePresses;
            result.push_back("EVDEV " + std::to_string(source) + " CODE=" + std::to_string(e.code) + " VALUE=" + std::to_string(e.value));
        }
    }
    return result;
}
}
