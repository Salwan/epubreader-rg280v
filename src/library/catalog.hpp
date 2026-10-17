#pragma once
#include "library/scanner.hpp"
#include "storage/store.hpp"
#include <set>

namespace epub {
constexpr int libraryRows = 17;
inline int progressGroupRank(int progress) { return progress > 0 && progress < 10000 ? 0 : progress == 10000 ? 2 : 1; }
inline std::string progressLabel(int progress) { return progress > 0 && progress < 100 ? "<1%" : std::to_string(progress < 0 ? 0 : progress / 100) + '%'; }
class Catalog {
    std::string folder_, selectedPath_, selectedIdentity_, topPath_;
    int selected_ = 0, top_ = 0;
    int fallbackIndex_ = 0;
    std::set<std::string> seen_;
    void sortAndRestore();
public:
    std::vector<LibraryEntry> entries;
    void begin(const std::string& folder, const Session& session);
    void observe(LibraryEntry entry);
    void observe(std::vector<LibraryEntry> batch);
    void finish(bool success);
    void update(const BookState& book);
    void move(int delta);
    void page(int direction);
    const LibraryEntry* current() const;
    std::string nextInspection() const;
    int selected() const { return selected_; }
    int top() const { return top_; }
    const std::string& folder() const { return folder_; }
    void remember(Session& session) const;
};
}
