#pragma once
#include <dirent.h>
#include <string>
#include <vector>
#include <map>

namespace epub {
struct LibraryEntry {
    std::string path, signature, identity, title, author, error;
    bool inspected = false;
    int progress = -1;
    bool finished = false;
};
class DirectoryCursor {
    DIR* directory_ = nullptr;
    std::string path_;
    bool folders_ = false;
    bool retryErrors_ = false;
    std::string root_;
    std::map<std::string, LibraryEntry> cache_;
public:
    ~DirectoryCursor();
    void open(const std::string& path, bool folders, const std::string& root = {}, bool retryErrors = false);
    std::vector<LibraryEntry> next(bool& more);
};
std::string entriesJson(const std::vector<LibraryEntry>& entries);
std::vector<LibraryEntry> parseEntries(const std::string& data, size_t limit);
std::string catalogDocument(const std::string& folder, const std::vector<LibraryEntry>& entries);
void entryProgress(LibraryEntry& entry, const std::string& root);
}
