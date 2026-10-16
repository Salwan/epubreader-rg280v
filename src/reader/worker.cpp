#include "reader/worker.hpp"
#include "reader/engine.hpp"
#include "reader/metadata.hpp"
#include <array>
#include <spawn.h>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <iomanip>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <sys/socket.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

extern char** environ;
namespace epub {
namespace {
constexpr size_t maxFrame = 2 * 1024 * 1024;
constexpr size_t pixelCount = 320 * 480;
void appendNumber(std::string& out, uint32_t n) { out.append(reinterpret_cast<const char*>(&n), sizeof(n)); }
uint32_t number(const char* p) { uint32_t n; std::memcpy(&n, p, sizeof(n)); return n; }
std::string frame(const std::string& data) {
    if (data.size() > maxFrame) throw std::runtime_error("Reader message exceeds bound");
    std::string out; appendNumber(out, static_cast<uint32_t>(data.size())); out += data; return out;
}
std::string encode(const ReaderRequest& r) {
    std::ostringstream out;
    out << static_cast<int>(r.command) << ' ' << r.value << ' ' << r.font << ' ' << r.theme << ' ' << std::quoted(r.path) << ' ' << std::quoted(r.anchor)
        << ' ' << r.requestId << ' ' << r.generation;
    return frame(out.str());
}
ReaderRequest decodeRequest(const std::string& data) {
    ReaderRequest r; int command = 0;
    std::istringstream in(data);
    if (!(in >> command >> r.value >> r.font >> r.theme >> std::quoted(r.path) >> std::quoted(r.anchor) >> r.requestId >> r.generation) || command < 0 || command > 9 ||
        r.path.size() > 4096 || r.anchor.size() > 16384)
        throw std::runtime_error("Invalid reader request");
    r.command = static_cast<ReaderCommand>(command); return r;
}
std::string encode(const ReaderResult& r) {
    std::ostringstream out;
    out << r.complete << ' ' << r.pageCacheHit << ' ' << r.documentCacheHit << ' ' << r.anchorOnPage << ' '
        << r.page << ' ' << r.pageCount << ' ' << r.progress << ' ' << r.font << ' ' << r.theme << ' ' << r.images << ' ' << r.elapsedMs;
    for (const auto* s : {&r.error, &r.message, &r.identity, &r.path, &r.title, &r.author, &r.anchor, &r.pageAnchor, &r.text, &r.layoutKey})
        out << ' ' << std::quoted(*s);
    out << ' ' << static_cast<int>(r.kind) << ' ' << static_cast<int>(r.command) << ' ' << r.commandValue << ' ' << r.requestId << ' ' << r.generation
        << ' ' << r.more << ' ' << std::quoted(entriesJson(r.entries));
    std::string data; appendNumber(data, static_cast<uint32_t>(out.str().size())); data += out.str();
    if (!r.pixels.empty()) data.append(reinterpret_cast<const char*>(r.pixels.data()), r.pixels.size() * sizeof(uint32_t));
    return frame(data);
}
ReaderResult decodeResult(const std::string& data) {
    if (data.size() < 4 || number(data.data()) > data.size() - 4) throw std::runtime_error("Invalid reader result length");
    const size_t metadata = number(data.data());
    std::istringstream in(data.substr(4, metadata));
    ReaderResult r;
    in >> r.complete >> r.pageCacheHit >> r.documentCacheHit >> r.anchorOnPage
       >> r.page >> r.pageCount >> r.progress >> r.font >> r.theme >> r.images >> r.elapsedMs;
    for (auto* s : {&r.error, &r.message, &r.identity, &r.path, &r.title, &r.author, &r.anchor, &r.pageAnchor, &r.text, &r.layoutKey}) in >> std::quoted(*s);
    int kind = 0, command = 0; std::string entries;
    in >> kind >> command >> r.commandValue >> r.requestId >> r.generation >> r.more >> std::quoted(entries);
    if (kind < 0 || kind > 2 || command < 0 || command > 9) throw std::runtime_error("Invalid result kind/command");
    r.kind = static_cast<ResultKind>(kind); r.command = static_cast<ReaderCommand>(command);
    r.entries = parseEntries(entries, 64);
    const size_t bytes = data.size() - 4 - metadata;
    if (!in || (bytes && bytes != pixelCount * sizeof(uint32_t))) throw std::runtime_error("Invalid reader result");
    if (bytes) { r.pixels.resize(pixelCount); std::memcpy(r.pixels.data(), data.data() + 4 + metadata, bytes); }
    return r;
}
bool readAll(int fd, char* buffer, size_t count) {
    while (count) {
        const ssize_t n = ::read(fd, buffer, count);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return false;
        buffer += n; count -= static_cast<size_t>(n);
    }
    return true;
}
bool sendAll(int fd, const std::string& data) {
    size_t offset = 0;
    while (offset < data.size()) {
        const ssize_t n = ::send(fd, data.data() + offset, data.size() - offset, MSG_NOSIGNAL);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return false;
        offset += static_cast<size_t>(n);
    }
    return true;
}
void run(int fd, const std::string& assets, const std::string& directory) {
    std::unique_ptr<ReaderEngine> engine;
    DirectoryCursor cursor;
    uint32_t length;
    while (readAll(fd, reinterpret_cast<char*>(&length), sizeof(length)) && length <= maxFrame) {
        std::string data(length, '\0');
        if (!readAll(fd, data.data(), length)) break;
        ReaderResult result;
        ReaderRequest request;
        auto tag = [&](ReaderResult& r) {
            r.requestId = request.requestId; r.generation = request.generation; r.command = request.command; r.commandValue = request.value;
            if (request.command == ReaderCommand::List || request.command == ReaderCommand::NextBatch) r.kind = ResultKind::Directory;
            else if (request.command == ReaderCommand::Inspect) r.kind = ResultKind::Metadata;
        };
        try {
            request = decodeRequest(data);
            if (request.command == ReaderCommand::List || request.command == ReaderCommand::NextBatch) {
                if (request.command == ReaderCommand::List) cursor.open(request.path, request.value == 1, directory, request.value == 2);
                result.entries = cursor.next(result.more);
            } else if (request.command == ReaderCommand::Inspect) {
                auto entry = inspectEpub(request.path); entryProgress(entry, directory); result.entries.push_back(std::move(entry));
            }
            else if (request.command == ReaderCommand::Close) engine.reset();
            else {
                if (!engine) engine = std::make_unique<ReaderEngine>(assets, directory);
                result = engine->execute(request, [&](const std::string& stage, int percent) {
                    ReaderResult update; tag(update); update.complete = false; update.message = stage; update.progress = percent;
                    if (!sendAll(fd, encode(update))) throw std::runtime_error("Reader UI disconnected");
                });
            }
        } catch (const std::exception& error) { result.error = error.what(); }
        tag(result);
        if (!sendAll(fd, encode(result))) break;
        if (engine && result.kind == ResultKind::Page && result.error.empty()) {
            try { engine->prefetch(); } catch (const std::exception&) { /* Derived cache is optional. */ }
        }
    }
}
}
int readerWorkerMain(int fd, const std::string& assets, const std::string& directory, int parent) {
    std::signal(SIGINT, SIG_DFL); std::signal(SIGTERM, SIG_DFL);
    ::prctl(PR_SET_PDEATHSIG, SIGTERM);
    if (::getppid() != parent) return 0;
    try { run(fd, assets, directory); } catch (...) { ::close(fd); return 1; }
    ::close(fd); return 0;
}
ReaderWorker::ReaderWorker(const std::string& assets, const std::string& directory, std::string executable)
    : assets_(assets), directory_(directory), executable_(std::move(executable)) {
    if (executable_.empty()) {
        char path[4096]; const auto n = ::readlink("/proc/self/exe", path, sizeof(path) - 1);
        if (n <= 0) throw std::runtime_error("Cannot resolve worker executable");
        executable_.assign(path, static_cast<size_t>(n));
    }
    start();
}
void ReaderWorker::start() {
    int sockets[2];
    if (::socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, sockets)) throw std::runtime_error("Cannot create reader socket");
    posix_spawn_file_actions_t actions;
    int error = ::posix_spawn_file_actions_init(&actions);
    if (error) { ::close(sockets[0]); ::close(sockets[1]); throw std::runtime_error("Cannot initialize worker spawn"); }
    error = ::posix_spawn_file_actions_addclose(&actions, sockets[0]);
    if (!error) error = ::posix_spawn_file_actions_adddup2(&actions, sockets[1], 3);
    if (!error && sockets[1] != 3) error = ::posix_spawn_file_actions_addclose(&actions, sockets[1]);
    std::array<std::string, 6> values{{executable_, "--worker", "3", assets_, directory_, std::to_string(::getpid())}};
    std::array<char*, 7> args{};
    for (size_t i = 0; i < values.size(); ++i) args[i] = values[i].data();
    pid_t child = -1;
    if (!error) error = ::posix_spawn(&child, executable_.c_str(), &actions, nullptr, args.data(), environ);
    ::posix_spawn_file_actions_destroy(&actions);
    if (error) { ::close(sockets[0]); ::close(sockets[1]); throw std::runtime_error("Cannot start reader worker: " + std::string(std::strerror(error))); }
    ::close(sockets[1]); fd_ = sockets[0]; pid_ = child;
    if (::fcntl(fd_, F_SETFL, O_NONBLOCK) < 0) { stop(); throw std::runtime_error("Cannot make reader socket nonblocking"); }
}
ReaderWorker::~ReaderWorker() { stop(); }
bool ReaderWorker::submit(const ReaderRequest& r) {
    if (busy_ || fd_ < 0) return false;
    current_ = r; current_.requestId = ++sequence_; current_.generation = generation_;
    outgoing_ = encode(current_); sent_ = 0; busy_ = true; return true;
}
std::optional<ReaderResult> ReaderWorker::poll() {
    if (fd_ < 0) return std::nullopt;
    auto failure = [&](const char* message) -> std::optional<ReaderResult> {
        ReaderResult r; r.error = message; r.command = current_.command; r.commandValue = current_.value;
        r.requestId = current_.requestId; r.generation = current_.generation;
        if (r.command == ReaderCommand::List || r.command == ReaderCommand::NextBatch) r.kind = ResultKind::Directory;
        else if (r.command == ReaderCommand::Inspect) r.kind = ResultKind::Metadata;
        busy_ = false;
        ::close(fd_); fd_ = -1; return r;
    };
    if (sent_ < outgoing_.size()) {
        const ssize_t n = ::send(fd_, outgoing_.data() + sent_, outgoing_.size() - sent_, MSG_NOSIGNAL);
        if (n > 0) sent_ += static_cast<size_t>(n);
        else if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) return failure("Reader worker disconnected");
    }
    // Keep individual UI iterations bounded even while receiving a full image.
    char buffer[65536];
    const ssize_t n = ::read(fd_, buffer, sizeof(buffer));
    if (n > 0) incoming_.append(buffer, static_cast<size_t>(n));
    else if (n == 0 || (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)) return failure("Reader worker stopped unexpectedly");
    if (incoming_.size() >= 4) {
        const size_t length = number(incoming_.data());
        if (length > maxFrame) return failure("Reader result exceeds bound");
        if (incoming_.size() >= 4 + length) {
            ReaderResult result;
            try { result = decodeResult(incoming_.substr(4, length)); }
            catch (const std::exception&) { return failure("Invalid reader result"); }
            incoming_.erase(0, 4 + length);
            if (result.generation != generation_ || result.requestId != current_.requestId) return failure("Mismatched reader response");
            if (result.complete) { busy_ = false; outgoing_.clear(); }
            return result;
        }
    }
    return std::nullopt;
}
void ReaderWorker::stop() {
    if (fd_ >= 0) { ::close(fd_); fd_ = -1; }
    if (pid_ > 0) {
        ::kill(pid_, SIGTERM);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(200);
        bool reaped = false;
        do {
            const pid_t result = ::waitpid(pid_, nullptr, WNOHANG);
            if (result == pid_ || (result < 0 && errno == ECHILD)) { reaped = true; break; }
            struct timespec delay{0, 5000000}; ::nanosleep(&delay, nullptr);
        } while (std::chrono::steady_clock::now() < deadline);
        if (!reaped) {
            ::kill(pid_, SIGKILL);
            while (::waitpid(pid_, nullptr, 0) < 0 && errno == EINTR) {}
        }
        pid_ = -1;
    }
    busy_ = false; outgoing_.clear(); incoming_.clear(); sent_ = 0;
}
void ReaderWorker::restart() { stop(); ++generation_; start(); }
}
