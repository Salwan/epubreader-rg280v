#pragma once
#include "reader/types.hpp"
#include <optional>

namespace epub {
// Start before SDL/audio threads. Engine execution and file I/O are confined to
// the child; the main loop only pumps a bounded, nonblocking framed socket.
class ReaderWorker {
    int fd_ = -1, pid_ = -1;
    bool busy_ = false;
    std::string outgoing_, incoming_;
    size_t sent_ = 0;
public:
    ReaderWorker(const std::string& assets, const std::string& stateDirectory);
    ~ReaderWorker();
    ReaderWorker(const ReaderWorker&) = delete;
    ReaderWorker& operator=(const ReaderWorker&) = delete;
    bool submit(const ReaderRequest& request);
    std::optional<ReaderResult> poll();
    bool busy() const { return busy_; }
    void stop(); // SIGTERM, then bounded reap/SIGKILL; no engine join on UI thread.
};
}
