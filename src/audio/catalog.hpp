#pragma once
#include <atomic>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace epub {
enum class MusicScan { Scanning, Ready, Empty, Failed };
struct MusicCatalog {
    MusicScan state = MusicScan::Scanning;
    std::vector<std::string> tracks;
    std::string error;
    unsigned elapsedMs = 0;
};
MusicCatalog scanMusic(const std::string& directory, const std::atomic<bool>* cancel = nullptr);
class MusicScanner {
    std::atomic<bool> cancel_{false};
    std::mutex mutex_;
    std::optional<MusicCatalog> result_;
    std::thread thread_;
public:
    explicit MusicScanner(const std::string& directory);
    ~MusicScanner();
    void stop();
    std::optional<MusicCatalog> poll();
};
}
