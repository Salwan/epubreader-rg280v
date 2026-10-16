#include "audio/catalog.hpp"
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <dirent.h>
#include <sys/stat.h>

namespace epub {
MusicCatalog scanMusic(const std::string& directory, const std::atomic<bool>* cancel) {
    MusicCatalog result;
    const auto start = std::chrono::steady_clock::now();
    DIR* dir = ::opendir(directory.c_str());
    if (!dir) { result.state = MusicScan::Failed; result.error = "Music folder: " + std::string(std::strerror(errno)); return result; }
    struct Guard { DIR* dir; ~Guard() { ::closedir(dir); } } guard{dir};
    for (;;) {
        if (cancel && *cancel) return {};
        errno = 0; const auto* entry = ::readdir(dir);
        if (!entry) {
            if (errno) { result.tracks.clear(); result.state = MusicScan::Failed; result.error = "Music scan: " + std::string(std::strerror(errno)); return result; }
            break;
        }
        std::string name = entry->d_name, lower = name;
        for (char& c : lower) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + ('a' - 'A'));
        if (lower.size() < 4 || lower.substr(lower.size() - 4) != ".mp3") continue;
        const auto path = directory + '/' + name;
        struct stat st{};
        if (::stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode)) result.tracks.push_back(path);
    }
    if (cancel && *cancel) return {};
    std::sort(result.tracks.begin(), result.tracks.end());
    result.state = result.tracks.empty() ? MusicScan::Empty : MusicScan::Ready;
    result.elapsedMs = static_cast<unsigned>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count());
    return result;
}
MusicScanner::MusicScanner(const std::string& directory) : thread_([this, directory] {
    auto result = scanMusic(directory, &cancel_);
    std::lock_guard<std::mutex> lock(mutex_);
    if (!cancel_) result_ = std::move(result);
}) {}
MusicScanner::~MusicScanner() { stop(); }
void MusicScanner::stop() { cancel_ = true; if (thread_.joinable()) thread_.join(); }
std::optional<MusicCatalog> MusicScanner::poll() {
    std::lock_guard<std::mutex> lock(mutex_);
    auto result = std::move(result_); result_.reset(); return result;
}
}
