#include "library/identity.hpp"
#include <xxhash.h>
#include <array>
#include <cerrno>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace epub {
std::string contentIdentity(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("Cannot open EPUB/resource: " + path);
    std::unique_ptr<XXH3_state_t, decltype(&XXH3_freeState)> state(XXH3_createState(), XXH3_freeState);
    if (!state) throw std::bad_alloc();
    XXH3_128bits_reset(state.get());
    std::array<char, 65536> buffer{};
    while (in) {
        in.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        XXH3_128bits_update(state.get(), buffer.data(), static_cast<size_t>(in.gcount()));
    }
    if (!in.eof()) throw std::runtime_error("EPUB/resource read failed: " + path);
    const auto hash = XXH3_128bits_digest(state.get());
    std::ostringstream out;
    out << std::hex << std::setfill('0') << std::setw(16) << hash.high64 << std::setw(16) << hash.low64;
    return out.str();
}
std::string fileSignature(const std::string& path) {
    struct stat st{};
    if (::stat(path.c_str(), &st) || !S_ISREG(st.st_mode)) throw std::runtime_error("Not a readable regular file: " + path);
    std::ostringstream out;
    out << st.st_dev << ':' << st.st_ino << ':' << st.st_size << ':' << st.st_mtim.tv_sec << ':' << st.st_mtim.tv_nsec
        << ':' << st.st_ctim.tv_sec << ':' << st.st_ctim.tv_nsec;
    return out.str();
}
std::string absolutePath(const std::string& path) {
    if (path.empty() || path.size() > 4096 || path.find('\0') != std::string::npos) throw std::runtime_error("Invalid content path");
    std::string full = path;
    if (path.front() != '/') {
        char cwd[4096]; if (!::getcwd(cwd, sizeof(cwd))) throw std::runtime_error("Cannot resolve working directory");
        full = std::string(cwd) + '/' + path;
    }
    std::vector<std::string> parts;
    std::istringstream in(full); std::string part;
    while (std::getline(in, part, '/')) {
        if (part.empty() || part == ".") continue;
        if (part == "..") { if (!parts.empty()) parts.pop_back(); }
        else parts.push_back(part);
    }
    std::string result;
    for (const auto& p : parts) result += '/' + p;
    return result.empty() ? "/" : result;
}
std::string baseName(const std::string& path) { return path.substr(path.find_last_of('/') + 1); }
std::string parentPath(const std::string& path) { const auto p = path.find_last_of('/'); return p == 0 || p == std::string::npos ? "/" : path.substr(0, p); }
std::string foldAscii(std::string text) { for (char& c : text) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + ('a' - 'A')); return text; }
std::string clipUtf8(std::string text, size_t bytes) {
    if (text.size() <= bytes) return text;
    while (bytes && (static_cast<unsigned char>(text[bytes]) & 0xc0) == 0x80) --bytes;
    text.resize(bytes); return text;
}
}
