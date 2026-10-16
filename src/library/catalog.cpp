#include "library/catalog.hpp"
#include "library/identity.hpp"
#include <algorithm>
#include <tuple>

namespace epub {
void Catalog::begin(const std::string& folder, const Session& s) {
    if (folder_ != folder) entries.clear();
    folder_ = folder; seen_.clear();
    selectedPath_ = s.selectedPath; selectedIdentity_ = s.selectedIdentity; topPath_ = s.topPath;
    selected_ = s.selectedIndex; fallbackIndex_ = s.selectedIndex;
    sortAndRestore();
}
void Catalog::sortAndRestore() {
    std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) {
        return std::make_tuple(a.finished, foldAscii(a.title), baseName(a.path), a.path)
            < std::make_tuple(b.finished, foldAscii(b.title), baseName(b.path), b.path);
    });
    auto i = std::find_if(entries.begin(), entries.end(), [&](const auto& e) { return e.path == selectedPath_; });
    if (i == entries.end() && !selectedIdentity_.empty())
        i = std::find_if(entries.begin(), entries.end(), [&](const auto& e) { return e.identity == selectedIdentity_; });
    selected_ = i == entries.end() ? std::clamp(fallbackIndex_, 0, std::max(0, static_cast<int>(entries.size()) - 1)) : static_cast<int>(i - entries.begin());
    auto t = std::find_if(entries.begin(), entries.end(), [&](const auto& e) { return e.path == topPath_; });
    if (t != entries.end()) top_ = static_cast<int>(t - entries.begin());
    top_ = std::clamp(top_, 0, std::max(0, static_cast<int>(entries.size()) - libraryRows));
    if (selected_ < top_) top_ = selected_;
    if (selected_ >= top_ + libraryRows) top_ = selected_ - libraryRows + 1;
}
void Catalog::observe(LibraryEntry entry) {
    std::vector<LibraryEntry> batch; batch.push_back(std::move(entry)); observe(std::move(batch));
}
void Catalog::observe(std::vector<LibraryEntry> batch) {
    for (auto& entry : batch) {
        seen_.insert(entry.path);
        auto i = std::find_if(entries.begin(), entries.end(), [&](const auto& e) { return e.path == entry.path; });
        if (i == entries.end()) entries.push_back(std::move(entry)); else *i = std::move(entry);
    }
    sortAndRestore();
}
void Catalog::finish(bool success) {
    if (success) entries.erase(std::remove_if(entries.begin(), entries.end(), [&](const auto& e) { return !seen_.count(e.path); }), entries.end());
    sortAndRestore();
    if (const auto* e = current()) { selectedPath_ = e->path; selectedIdentity_ = e->identity; }
    if (!entries.empty()) topPath_ = entries[static_cast<size_t>(top_)].path;
    fallbackIndex_ = selected_;
}
void Catalog::update(const BookState& b) {
    for (auto& entry : entries) if (entry.identity == b.identity || entry.path == b.path) {
        entry.identity = b.identity; entry.progress = b.progress; entry.finished = b.finished;
        if (!b.title.empty()) entry.title = b.title;
        entry.author = b.author;
    }
    sortAndRestore();
}
void Catalog::move(int delta) {
    selected_ = std::clamp(selected_ + delta, 0, std::max(0, static_cast<int>(entries.size()) - 1));
    fallbackIndex_ = selected_;
    if (const auto* e = current()) { selectedPath_ = e->path; selectedIdentity_ = e->identity; }
    sortAndRestore();
    if (!entries.empty()) topPath_ = entries[static_cast<size_t>(top_)].path;
}
void Catalog::page(int direction) {
    if (entries.size() <= static_cast<size_t>(libraryRows)) move(direction < 0 ? -static_cast<int>(entries.size()) : static_cast<int>(entries.size()));
    else move(direction * libraryRows);
}
const LibraryEntry* Catalog::current() const { return entries.empty() ? nullptr : &entries[static_cast<size_t>(selected_)]; }
std::string Catalog::nextInspection() const {
    const auto i = std::find_if(entries.begin(), entries.end(), [](const auto& e) { return !e.inspected; });
    return i == entries.end() ? "" : i->path;
}
void Catalog::remember(Session& s) const {
    if (const auto* e = current()) { s.selectedPath = e->path; s.selectedIdentity = e->identity; }
    else { s.selectedPath.clear(); s.selectedIdentity.clear(); }
    s.selectedIndex = selected_; s.topPath = entries.empty() ? "" : entries[static_cast<size_t>(top_)].path;
}
}
