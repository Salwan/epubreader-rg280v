#pragma once
#include <string>
#include <vector>

namespace epub {
// Passive observation only: never EVIOCGRAB, never writes input events.
class EventObserver {
    std::vector<int> files_;
public:
    unsigned volumePresses = 0;
    std::string summary = "Evdev disabled on host";
    explicit EventObserver(bool enabled);
    ~EventObserver();
    std::vector<std::string> poll();
};
}
