#include "reader/worker.hpp"
#include "reader/engine.hpp"
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
    out << static_cast<int>(r.command) << ' ' << r.value << ' ' << r.font << ' ' << r.theme << ' ' << std::quoted(r.path) << ' ' << std::quoted(r.anchor);
    return frame(out.str());
}
ReaderRequest decodeRequest(const std::string& data) {
    ReaderRequest r; int command = 0;
    std::istringstream in(data);
    if (!(in >> command >> r.value >> r.font >> r.theme >> std::quoted(r.path) >> std::quoted(r.anchor)) || command < 0 || command > 5)
        throw std::runtime_error("Invalid reader request");
    r.command = static_cast<ReaderCommand>(command); return r;
}
std::string encode(const ReaderResult& r) {
    std::ostringstream out;
    out << r.complete << ' ' << r.pageCacheHit << ' ' << r.documentCacheHit << ' ' << r.anchorOnPage << ' '
        << r.page << ' ' << r.pageCount << ' ' << r.progress << ' ' << r.font << ' ' << r.theme << ' ' << r.images << ' ' << r.elapsedMs;
    for (const auto* s : {&r.error, &r.message, &r.identity, &r.path, &r.title, &r.author, &r.anchor, &r.pageAnchor, &r.text, &r.layoutKey})
        out << ' ' << std::quoted(*s);
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
    uint32_t length;
    while (readAll(fd, reinterpret_cast<char*>(&length), sizeof(length)) && length <= maxFrame) {
        std::string data(length, '\0');
        if (!readAll(fd, data.data(), length)) break;
        ReaderResult result;
        try {
            if (!engine) engine = std::make_unique<ReaderEngine>(assets, directory);
            result = engine->execute(decodeRequest(data), [&](const std::string& stage, int percent) {
                ReaderResult update; update.complete = false; update.message = stage; update.progress = percent;
                if (!sendAll(fd, encode(update))) throw std::runtime_error("Reader UI disconnected");
            });
        } catch (const std::exception& error) { result.error = error.what(); }
        if (!sendAll(fd, encode(result))) break;
        if (result.error.empty()) {
            try { engine->prefetch(); } catch (const std::exception&) { /* Derived cache is optional. */ }
        }
    }
}
}
ReaderWorker::ReaderWorker(const std::string& assets, const std::string& directory) {
    int sockets[2];
    if (::socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, sockets)) throw std::runtime_error("Cannot create reader socket");
    const pid_t parent = ::getpid();
    const pid_t child = ::fork();
    if (child < 0) { ::close(sockets[0]); ::close(sockets[1]); throw std::runtime_error("Cannot start reader worker"); }
    if (!child) {
        ::close(sockets[0]);
        std::signal(SIGINT, SIG_DFL); std::signal(SIGTERM, SIG_DFL);
        ::prctl(PR_SET_PDEATHSIG, SIGTERM);
        if (::getppid() != parent) _exit(0);
        try { run(sockets[1], assets, directory); } catch (...) { ::close(sockets[1]); _exit(1); }
        ::close(sockets[1]); _exit(0);
    }
    ::close(sockets[1]); fd_ = sockets[0]; pid_ = child;
    if (::fcntl(fd_, F_SETFL, O_NONBLOCK) < 0) { stop(); throw std::runtime_error("Cannot make reader socket nonblocking"); }
}
ReaderWorker::~ReaderWorker() { stop(); }
bool ReaderWorker::submit(const ReaderRequest& r) {
    if (busy_ || fd_ < 0) return false;
    outgoing_ = encode(r); sent_ = 0; busy_ = true; return true;
}
std::optional<ReaderResult> ReaderWorker::poll() {
    if (fd_ < 0) return std::nullopt;
    auto failure = [&](const char* message) -> std::optional<ReaderResult> {
        ReaderResult r; r.error = message; busy_ = false;
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
    busy_ = false;
}
}
