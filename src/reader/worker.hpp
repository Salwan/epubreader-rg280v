#pragma once
#include "reader/types.hpp"
#include <optional>

namespace epub {
// Encoder returns a length-prefixed frame; decoder receives the frame body.
std::string encodeReaderResult(const ReaderResult& result);
ReaderResult decodeReaderResult(const std::string& body);
int readerWorkerMain(int fd, const std::string& assets, const std::string& stateDirectory, int parent);
// Spawn/exec permits replacement after SDL/storage threads start. The UI only
// pumps bounded, nonblocking frames; document/directory inspection stays here.
class ReaderWorker {
    int fd_ = -1, pid_ = -1;
    bool busy_ = false;
    std::string outgoing_, incoming_;
    size_t sent_ = 0;
    std::string assets_, directory_, executable_;
    uint64_t generation_ = 1, sequence_ = 0;
    ReaderRequest current_;
    void start();
public:
    ReaderWorker(const std::string& assets, const std::string& stateDirectory, std::string executable = {});
    ~ReaderWorker();
    ReaderWorker(const ReaderWorker&) = delete;
    ReaderWorker& operator=(const ReaderWorker&) = delete;
    bool submit(const ReaderRequest& request);
    std::optional<ReaderResult> poll();
    bool busy() const { return busy_; }
    uint64_t generation() const { return generation_; }
    void restart();
    void stop(); // SIGTERM, then bounded reap/SIGKILL; no engine join on UI thread.
};
}
