#include "storage/store.hpp"
#include "storage/position.hpp"
#include "ui/theme.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cerrno>
#include <cstring>
#include <dirent.h>
#include <fstream>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdexcept>
#include <limits>

namespace epub {
using Json = nlohmann::json;
namespace {
const char* fonts[] = {"small", "medium", "large"};
struct FutureSchema : std::runtime_error { using std::runtime_error::runtime_error; };
void schema(const Json& j) {
    if (!j.is_object() || !j.contains("schema") || !j.at("schema").is_number_integer())
        throw std::runtime_error("Missing state schema");
    if (j.at("schema") != 1) throw FutureSchema("Unsupported state schema");
}
void preserve(const std::string& path) {
    std::string target = path + ".invalid";
    for (unsigned n = 1; ::access(target.c_str(), F_OK) == 0; ++n) target = path + ".invalid." + std::to_string(n);
    if (::rename(path.c_str(), target.c_str())) throw std::runtime_error("Cannot preserve malformed state: " + path);
}
std::string text(const Json& j, const char* key, size_t max = 4096) {
    const std::string s = j.value(key, std::string{});
    if (s.size() > max || s.find('\0') != std::string::npos) throw std::runtime_error(std::string("Invalid state string: ") + key);
    return s;
}
int integer(const Json& j, const char* key, int fallback, int low, int high) {
    if (j.contains(key) && !j.at(key).is_number_integer()) throw std::runtime_error(std::string("Invalid integer: ") + key);
    if (j.contains(key) && j.at(key).is_number_unsigned() && j.at(key).get<uint64_t>() > static_cast<uint64_t>(std::numeric_limits<int64_t>::max()))
        throw std::runtime_error("State integer overflow");
    const auto value = j.value(key, static_cast<int64_t>(fallback));
    if (value < low || value > high) throw std::runtime_error(std::string("Invalid state range: ") + key);
    return static_cast<int>(value);
}
int named(const Json& j, const char* key, bool font) {
    const std::string value = j.value(key, std::string(font ? "medium" : "dark"));
    for (int i = 0; i < (font ? 3 : static_cast<int>(palettes.size())); ++i)
        if (value == (font ? fonts[i] : palettes[static_cast<size_t>(i)].name)) return i;
    throw std::runtime_error(std::string("Invalid preset: ") + key);
}
long mixerInteger(const Json& j, const char* key, long fallback) {
    if (!j.contains(key)) return fallback;
    if (!j.at(key).is_number_integer()) throw std::runtime_error("Invalid mixer integer");
    if (j.at(key).is_number_unsigned() && j.at(key).get<uint64_t>() > static_cast<uint64_t>(std::numeric_limits<int64_t>::max())) throw std::runtime_error("Mixer integer overflow");
    const auto value = j.at(key).get<int64_t>();
    if (value < std::numeric_limits<long>::min() || value > std::numeric_limits<long>::max()) throw std::runtime_error("Mixer value out of range");
    return static_cast<long>(value);
}
Json configJson(const Config& c) {
    const State& p = c.preferences;
    return {{"schema", 1}, {"theme", palettes.at(static_cast<size_t>(p.theme)).name}, {"font", fonts[p.font]},
        {"books", c.books}, {"music", c.music}, {"musicRepeat", repeatName(c.musicRepeat)}, {"volume", p.volume}, {"mixer", p.mixer},
        {"minimum", p.minimum}, {"maximum", p.maximum}, {"rawLeft", p.rawLeft}, {"rawRight", p.rawRight},
        {"switchLeft", p.switchLeft}, {"switchRight", p.switchRight}};
}
Json sessionJson(const Session& s) {
    return {{"schema", 1}, {"mode", s.reader ? "reader" : "list"}, {"identity", s.identity}, {"path", s.path},
        {"selectedPath", s.selectedPath}, {"selectedIdentity", s.selectedIdentity}, {"topPath", s.topPath}, {"selectedIndex", s.selectedIndex}};
}
Json bookJson(const BookState& b) {
    if (!validIdentity(b.identity) || b.anchor.size() > 16384) throw std::runtime_error("Invalid book checkpoint");
    return {{"schema", 1}, {"fingerprint", "xxh3-128-v1"}, {"identity", b.identity}, {"path", b.path},
        {"anchor", b.anchor}, {"title", b.title}, {"author", b.author}, {"layoutKey", b.layoutKey},
        {"progress", b.progress}, {"page", b.page}, {"pageCount", b.pageCount}, {"font", fonts[b.font]},
        {"metadataVersion", b.metadataVersion}, {"finished", b.finished}};
}
template<class F> void load(const std::string& path, F apply, std::deque<std::string>& errors) {
    try {
        const auto data = readDocument(path);
        if (!data.empty()) apply(Json::parse(data));
    } catch (const FutureSchema& e) { errors.push_back(path + ": " + e.what()); }
    catch (const std::exception& e) {
        errors.push_back(path + ": " + e.what());
        if (::access(path.c_str(), F_OK) == 0) {
            try { preserve(path); } catch (const std::exception& p) { errors.push_back(p.what()); }
        }
    }
}
}
bool validIdentity(const std::string& id) {
    return id.size() == 32 && std::all_of(id.begin(), id.end(), [](char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
    });
}
std::string readDocument(const std::string& path, size_t limit) {
    // Open first, then size/read that descriptor: a concurrent atomic rename
    // must not pair the old inode's size with the new inode's contents.
    const int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        if (errno == ENOENT) return {};
        throw std::runtime_error("Cannot open state: " + path + ": " + std::strerror(errno));
    }
    struct Descriptor { int fd; ~Descriptor() { ::close(fd); } } descriptor{fd};
    struct stat st{};
    if (::fstat(fd, &st)) throw std::runtime_error("Cannot stat open state: " + path);
    if (!S_ISREG(st.st_mode) || st.st_size <= 0 || static_cast<uint64_t>(st.st_size) > limit)
        throw std::runtime_error("Invalid state file size: " + path);
    std::string data(static_cast<size_t>(st.st_size), '\0');
    size_t offset = 0;
    while (offset < data.size()) {
        const auto n = ::read(fd, data.data() + offset, data.size() - offset);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) throw std::runtime_error("Short state read: " + path);
        offset += static_cast<size_t>(n);
    }
    schema(Json::parse(data));
    return data;
}
void writeDocument(const std::string& directory, const std::string& filename, const std::string& data) {
    // Check the previous file even if it was not loaded by this process.
    try { (void)readDocument(directory + '/' + filename, 32 * 1024 * 1024); }
    catch (const FutureSchema&) { throw; }
    catch (const std::exception&) { preserve(directory + '/' + filename); }
    schema(Json::parse(data));
    atomicWrite(directory, filename, data);
}
BookState readBook(const std::string& root, const std::string& identity) {
    if (!validIdentity(identity)) throw std::runtime_error("Invalid book identity");
    BookState b; b.identity = identity;
    const auto data = readDocument(root + "/books/" + identity + ".json");
    if (data.empty()) return b;
    const auto j = Json::parse(data);
    if (text(j, "identity") != identity || text(j, "fingerprint") != "xxh3-128-v1") throw std::runtime_error("Book identity/schema mismatch");
    b.path = text(j, "path"); b.anchor = text(j, "anchor", 16384); b.title = text(j, "title"); b.author = text(j, "author");
    b.layoutKey = text(j, "layoutKey"); b.font = named(j, "font", true);
    b.progress = integer(j, "progress", -1, -1, 10000); b.page = integer(j, "page", 0, 0, 10000000);
    b.pageCount = integer(j, "pageCount", 0, 0, 10000000); b.metadataVersion = integer(j, "metadataVersion", 1, 0, 1000000);
    b.finished = j.value("finished", false);
    return b;
}
Store::Store(std::string root, const std::string& contentRoot) : root_(std::move(root)) {
    ensureDirectory(root_);
    config.books = contentRoot + "/books"; config.music = contentRoot + "/music";
    migrate();
    load(root_ + "/config.json", [&](const Json& j) {
        Config c = config;
        c.preferences.theme = named(j, "theme", false); c.preferences.font = named(j, "font", true);
        c.books = text(j, "books"); c.music = text(j, "music");
        if (c.books.empty() || c.music.empty() || c.books.front() != '/' || c.music.front() != '/') throw std::runtime_error("Content folders must be absolute");
        auto& p = c.preferences;
        p.volume = integer(j, "volume", 50, 0, 100); p.mixer = text(j, "mixer");
        p.minimum = mixerInteger(j, "minimum", 0); p.maximum = mixerInteger(j, "maximum", 0);
        p.rawLeft = mixerInteger(j, "rawLeft", -1); p.rawRight = mixerInteger(j, "rawRight", -1);
        p.switchLeft = integer(j, "switchLeft", -1, -1, 1); p.switchRight = integer(j, "switchRight", -1, -1, 1);
        if (j.contains("musicRepeat")) {
            if (j["musicRepeat"] == "one") c.musicRepeat = RepeatMode::One;
            else if (j["musicRepeat"] == "stop") c.musicRepeat = RepeatMode::Stop;
            else if (j["musicRepeat"] != "all") errors_.push_back("Invalid Music Repeat; recovered to ALL");
        }
        config = std::move(c);
    }, errors_);
    load(root_ + "/session.json", [&](const Json& j) {
        Session s;
        const auto mode = text(j, "mode");
        if (mode != "reader" && mode != "list") throw std::runtime_error("Invalid session mode");
        s.reader = mode == "reader"; s.identity = text(j, "identity"); s.path = text(j, "path");
        s.selectedPath = text(j, "selectedPath"); s.selectedIdentity = text(j, "selectedIdentity"); s.topPath = text(j, "topPath");
        s.selectedIndex = integer(j, "selectedIndex", 0, 0, 10000000);
        if ((!s.identity.empty() && !validIdentity(s.identity)) || (!s.selectedIdentity.empty() && !validIdentity(s.selectedIdentity)) ||
            (s.reader && (s.identity.empty() || s.path.empty()))) throw std::runtime_error("Invalid session book");
        session = std::move(s);
    }, errors_);
    writer_ = std::thread(&Store::loop, this);
}
void Store::migrate() {
    try {
        if (::access((root_ + "/config.json").c_str(), F_OK) && ::access((root_ + "/slice/state.ini").c_str(), F_OK) == 0) {
            bool valid = false;
            const auto preferences = loadState(root_ + "/slice", &valid);
            if (valid) {
                config.preferences = preferences;
                writeDocument(root_, "config.json", configJson(config).dump(2) + '\n');
            } else errors_.push_back("Invalid legacy preferences; using defaults");
        }
        DIR* dir = ::opendir((root_ + "/slice/books").c_str());
        if (!dir) { if (errno != ENOENT) errors_.push_back("Legacy book directory unavailable"); return; }
        while (const auto* e = ::readdir(dir)) {
            const std::string name = e->d_name;
            if (name.size() != 41 || name.substr(32) != ".position" || !validIdentity(name.substr(0, 32))) continue;
            const auto id = name.substr(0, 32);
            if (::access((root_ + "/books/" + id + ".json").c_str(), F_OK) == 0) continue;
            const auto p = loadPosition(root_ + "/slice", id);
            if (p.anchor.empty()) { errors_.push_back("Invalid legacy position: " + name); continue; }
            BookState b; b.identity = id; b.path = p.path; b.anchor = p.anchor;
            try { writeDocument(root_ + "/books", id + ".json", bookJson(b).dump(2) + '\n'); }
            catch (const std::exception& error) { errors_.push_back(error.what()); }
        }
        ::closedir(dir);
    } catch (const std::exception& error) { errors_.push_back(error.what()); }
}
Store::~Store() {
    flush();
    { std::lock_guard<std::mutex> lock(mutex_); stopping_ = true; }
    wake_.notify_one();
    if (writer_.joinable()) writer_.join();
}
void Store::enqueue(std::string key, std::function<void()> run) {
    std::lock_guard<std::mutex> lock(mutex_);
    failed_.erase(std::remove_if(failed_.begin(), failed_.end(), [&](const auto& item) { return item.key == key; }), failed_.end());
    const auto revision = ++writeRevisions_[key];
    Write write{key, std::move(run), 0, revision};
    if (!pending_.empty() && pending_.back().key == key && !pending_.back().ticket) pending_.back() = std::move(write);
    else pending_.push_back(std::move(write));
    wake_.notify_one();
}
void Store::loop() {
    std::unique_lock<std::mutex> lock(mutex_);
    while (true) {
        wake_.wait(lock, [&] { return stopping_ || !pending_.empty(); });
        if (stopping_ && pending_.empty()) return;
        auto item = std::move(pending_.front()); pending_.pop_front(); active_ = true;
        lock.unlock();
        std::string error;
        try { item.run(); } catch (const std::exception& e) { error = e.what(); }
        lock.lock();
        if (item.ticket) {
            auto& result = critical_.at(item.ticket).result;
            result.state = error.empty() ? CheckpointState::Succeeded : CheckpointState::Failed;
            result.error = error;
        }
        if (!error.empty() && (!item.ticket || item.revision == sessionRevision_)) {
            errors_.push_back("SAVE ERROR: " + error);
            if (!item.ticket && item.revision == writeRevisions_[item.key] && std::none_of(pending_.begin(), pending_.end(), [&](const auto& next) { return next.key == item.key; })) failed_.push_back(std::move(item));
        }
        active_ = false; if (pending_.empty()) drained_.notify_all();
    }
}
BookState Store::book(const std::string& id) {
    auto i = books_.find(id);
    if (i != books_.end()) return i->second;
    BookState b; b.identity = id;
    try { b = readBook(root_, id); }
    catch (const FutureSchema& e) { std::lock_guard<std::mutex> lock(mutex_); errors_.push_back(e.what()); }
    catch (const std::exception& e) {
        std::lock_guard<std::mutex> lock(mutex_); errors_.push_back(e.what());
        try { preserve(root_ + "/books/" + id + ".json"); } catch (const std::exception& p) { errors_.push_back(p.what()); }
    }
    books_[id] = b; return b;
}
void Store::saveConfig() {
    const auto data = configJson(config).dump(2) + '\n';
    enqueue("config", [root = root_, data] { writeDocument(root, "config.json", data); });
}
void Store::saveSession() {
    const auto data = sessionJson(session).dump(2) + '\n';
    const auto revision = ++sessionRevision_;
    enqueue("session", [this, root = root_, data, revision] { if (revision == sessionRevision_) writeDocument(root, "session.json", data); });
}
void Store::checkpoint(const BookState& b) {
    books_[b.identity] = b;
    const auto data = bookJson(b).dump(2) + '\n', sessionData = sessionJson(session).dump(2) + '\n';
    const auto revision = ++sessionRevision_;
    enqueue("book:" + b.identity, [this, root = root_, id = b.identity, data, sessionData, revision] {
        writeDocument(root + "/books", id + ".json", data);
        if (revision == sessionRevision_) writeDocument(root, "session.json", sessionData);
    });
}
void Store::saveCatalog(std::function<std::string()> serialize) {
    enqueue("catalog", [root = root_, serialize = std::move(serialize)] { writeDocument(root + "/cache", "library.json", serialize()); });
}
CheckpointTicket Store::criticalCheckpoint(const std::optional<BookState>& b) {
    // Serialize on the owner thread; the worker never reads mutable app state.
    const auto bookData = b ? bookJson(*b).dump(2) + '\n' : std::string{};
    const auto sessionData = sessionJson(session).dump(2) + '\n', configData = configJson(config).dump(2) + '\n';
    if (b) books_[b->identity] = *b;
    const auto revision = ++sessionRevision_;
    std::lock_guard<std::mutex> lock(mutex_);
    const auto ticket = ++nextTicket_;
    ++writeRevisions_["config"]; ++writeRevisions_["session"];
    if (b) ++writeRevisions_["book:" + b->identity];
    const auto superseded = [&](const Write& w) { return !w.ticket && (w.key == "config" || w.key == "session" || (b && w.key == "book:" + b->identity)); };
    pending_.erase(std::remove_if(pending_.begin(), pending_.end(), superseded), pending_.end());
    failed_.erase(std::remove_if(failed_.begin(), failed_.end(), superseded), failed_.end());
    Write write{"critical", [this, root = root_, b, bookData, sessionData, configData, revision] {
        std::string stage = "book";
        try {
            if (revision != sessionRevision_) throw std::runtime_error("Checkpoint superseded");
            if (b) writeDocument(root + "/books", b->identity + ".json", bookData);
            stage = "session";
            if (revision != sessionRevision_) throw std::runtime_error("Checkpoint session superseded");
            writeDocument(root, "session.json", sessionData);
            stage = "config"; writeDocument(root, "config.json", configData);
        } catch (const std::exception& e) { throw std::runtime_error(stage + ": " + e.what()); }
    }, ticket, revision};
    critical_.emplace(ticket, Critical{write, {}}); pending_.push_front(std::move(write));
    wake_.notify_one(); return ticket;
}
CheckpointResult Store::criticalResult(CheckpointTicket ticket) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto i = critical_.find(ticket);
    return i == critical_.end() ? CheckpointResult{CheckpointState::Failed, 0, "Unknown checkpoint ticket"} : i->second.result;
}
bool Store::retryCritical(CheckpointTicket ticket) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto& c = critical_.at(ticket);
    if (c.result.state != CheckpointState::Failed || c.result.attempts >= 2) return false;
    c.result.state = CheckpointState::Pending; c.result.error.clear(); ++c.result.attempts;
    pending_.push_front(c.write); wake_.notify_one(); return true;
}
void Store::forgetCritical(CheckpointTicket ticket) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto i = critical_.find(ticket);
    if (i != critical_.end() && i->second.result.state != CheckpointState::Pending) critical_.erase(i);
}
bool Store::idle() { std::lock_guard<std::mutex> lock(mutex_); return pending_.empty() && !active_; }
void Store::flush() { std::unique_lock<std::mutex> lock(mutex_); drained_.wait(lock, [&] { return pending_.empty() && !active_; }); }
void Store::retryFailed() {
    std::lock_guard<std::mutex> lock(mutex_);
    while (!failed_.empty()) { pending_.push_back(std::move(failed_.front())); failed_.pop_front(); }
    wake_.notify_one();
}
std::string Store::error() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (errors_.empty()) return {};
    auto s = std::move(errors_.front()); errors_.pop_front(); return s;
}
}
