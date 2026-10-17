#pragma once
#include "storage/state.hpp"
#include "audio/repeat.hpp"
#include <condition_variable>
#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <atomic>
#include <optional>

namespace epub {
bool validIdentity(const std::string& id);
struct Config {
    State preferences;
    std::string books, music;
    RepeatMode musicRepeat = RepeatMode::All;
};
struct Session {
    bool reader = false;
    std::string identity, path, selectedPath, selectedIdentity, topPath;
    int selectedIndex = 0;
};
struct BookState {
    std::string identity, path, anchor, title, author, layoutKey;
    int progress = -1, page = 0, pageCount = 0, font = 1, metadataVersion = 1;
    bool finished = false;
};
// Readers ignore .tmp files. Malformed documents are preserved before recovery;
// an unsupported schema is never replaced by an older application.
std::string readDocument(const std::string& path, size_t limit = 65536);
void writeDocument(const std::string& directory, const std::string& filename, const std::string& data);
BookState readBook(const std::string& root, const std::string& identity);
using CheckpointTicket = uint32_t;
enum class CheckpointState { Pending, Succeeded, Failed };
struct CheckpointResult {
    CheckpointState state = CheckpointState::Pending;
    unsigned attempts = 1;
    std::string error;
};

class Store {
    struct Write { std::string key; std::function<void()> run; CheckpointTicket ticket = 0; uint32_t revision = 0; };
    struct Critical { Write write; CheckpointResult result; };
    std::string root_;
    std::mutex mutex_;
    std::condition_variable wake_, drained_;
    std::deque<Write> pending_;
    std::deque<Write> failed_;
    std::deque<std::string> errors_;
    bool active_ = false, stopping_ = false;
    std::thread writer_;
    std::atomic<uint32_t> sessionRevision_{0}; // Native-width atomics on MIPS32.
    std::map<std::string, BookState> books_;
    std::map<CheckpointTicket, Critical> critical_;
    std::map<std::string, uint32_t> writeRevisions_;
    CheckpointTicket nextTicket_ = 0;
    void enqueue(std::string key, std::function<void()> run);
    void loop();
    void migrate();
public:
    Config config;
    Session session;
    Store(std::string root, const std::string& contentRoot);
    ~Store();
    Store(const Store&) = delete;
    Store& operator=(const Store&) = delete;
    const std::string& root() const { return root_; }
    BookState book(const std::string& identity);
    void saveConfig();
    void saveSession();
    void checkpoint(const BookState& book);
    CheckpointTicket criticalCheckpoint(const std::optional<BookState>& book);
    CheckpointResult criticalResult(CheckpointTicket ticket);
    bool retryCritical(CheckpointTicket ticket); // At most one retry, captured data retained.
    void forgetCritical(CheckpointTicket ticket);
    void saveCatalog(std::function<std::string()> serialize);
    bool idle();
    void flush();
    void retryFailed();
    std::string error();
};
}
