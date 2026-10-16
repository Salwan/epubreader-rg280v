#include "library/scanner.hpp"
#include "library/identity.hpp"
#include "storage/store.hpp"
#include <nlohmann/json.hpp>
#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <sys/stat.h>

namespace epub {
DirectoryCursor::~DirectoryCursor() { if (directory_) ::closedir(directory_); }
void DirectoryCursor::open(const std::string& path, bool folders, const std::string& root, bool retryErrors) {
    if (directory_) ::closedir(directory_);
    path_ = absolutePath(path); folders_ = folders; root_ = root; retryErrors_ = retryErrors; cache_.clear();
    directory_ = ::opendir(path_.c_str());
    if (!directory_) throw std::runtime_error("Cannot scan folder: " + path_ + ": " + std::strerror(errno));
    if (!folders && !root.empty()) {
        try {
            const auto data = readDocument(root + "/cache/library.json", 32 * 1024 * 1024);
            if (!data.empty()) {
                const auto j = nlohmann::json::parse(data);
                if (j.at("folder") == path_ && j.at("extractor") == 1)
                    for (auto& entry : parseEntries(j.at("entries").dump(), 100000)) cache_.emplace(entry.path, std::move(entry));
            }
        } catch (const std::exception&) { /* A derived index is rebuilt by this scan. */ }
    }
}
std::vector<LibraryEntry> DirectoryCursor::next(bool& more) {
    if (!directory_) throw std::runtime_error("No folder scan active");
    std::vector<LibraryEntry> result; more = true;
    for (int examined = 0; examined < 128 && result.size() < 64; ++examined) {
        errno = 0; const auto* e = ::readdir(directory_);
        if (!e) {
            const int error = errno; ::closedir(directory_); directory_ = nullptr; more = false;
            if (error) throw std::runtime_error("Folder scan interrupted: " + std::string(std::strerror(error)));
            break;
        }
        const std::string name = e->d_name;
        if (name == "." || name == "..") continue;
        if (!folders_ && (name.size() < 5 || foldAscii(name.substr(name.size() - 5)) != ".epub")) continue;
        LibraryEntry item; item.path = path_ + (path_ == "/" ? "" : "/") + name;
        struct stat st{};
        if (::stat(item.path.c_str(), &st)) continue;
        if (folders_ ? !S_ISDIR(st.st_mode) : !S_ISREG(st.st_mode)) continue;
        item.title = folders_ ? name : name.substr(0, name.size() - 5);
        if (!folders_) {
            try { item.signature = fileSignature(item.path); }
            catch (const std::exception& error) { item.error = error.what(); item.inspected = true; }
            const auto cached = cache_.find(item.path);
            if (cached != cache_.end() && cached->second.signature == item.signature && !(retryErrors_ && !cached->second.error.empty())) item = cached->second;
            entryProgress(item, root_);
        }
        result.push_back(std::move(item));
    }
    return result;
}
std::string entriesJson(const std::vector<LibraryEntry>& entries) {
    auto j = nlohmann::json::array();
    for (const auto& e : entries) j.push_back({{"path", e.path}, {"signature", e.signature}, {"identity", e.identity},
        {"title", e.title}, {"author", e.author}, {"error", e.error}, {"inspected", e.inspected}, {"progress", e.progress}, {"finished", e.finished}});
    return j.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
}
std::vector<LibraryEntry> parseEntries(const std::string& data, size_t limit) {
    const auto j = nlohmann::json::parse(data);
    if (!j.is_array() || j.size() > limit) throw std::runtime_error("Invalid library batch");
    std::vector<LibraryEntry> entries;
    for (const auto& v : j) {
        LibraryEntry e;
        e.path = v.at("path"); e.signature = v.at("signature"); e.identity = v.at("identity");
        e.title = v.at("title"); e.author = v.at("author"); e.error = v.at("error"); e.inspected = v.at("inspected");
        e.progress = v.value("progress", -1); e.finished = v.value("finished", false);
        if (e.progress < -1 || e.progress > 10000) throw std::runtime_error("Invalid cached progress");
        for (const auto* s : {&e.path, &e.signature, &e.identity, &e.title, &e.author, &e.error})
            if (s->size() > 4096 || s->find('\0') != std::string::npos) throw std::runtime_error("Library string exceeds bound");
        if (e.path.empty() || e.path.front() != '/' || (!e.identity.empty() && !validIdentity(e.identity))) throw std::runtime_error("Invalid library path/identity");
        entries.push_back(std::move(e));
    }
    return entries;
}
void entryProgress(LibraryEntry& entry, const std::string& root) {
    if (root.empty() || entry.identity.empty()) return;
    try { const auto b = readBook(root, entry.identity); entry.progress = b.progress; entry.finished = b.finished; }
    catch (const std::exception&) { entry.progress = -1; entry.finished = false; }
}
std::string catalogDocument(const std::string& folder, const std::vector<LibraryEntry>& entries) {
    return nlohmann::json{{"schema", 1}, {"extractor", 1}, {"folder", folder}, {"entries", nlohmann::json::parse(entriesJson(entries))}}.dump() + '\n';
}
}
