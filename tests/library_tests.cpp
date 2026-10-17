#include "library/catalog.hpp"
#include "library/identity.hpp"
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
void check(bool ok, const std::string& message) { if (!ok) throw std::runtime_error(message); }
std::vector<epub::LibraryEntry> scan(epub::DirectoryCursor& cursor) {
    std::vector<epub::LibraryEntry> all; bool more;
    do { auto batch = cursor.next(more); check(batch.size() <= 64, "Unbounded directory batch"); all.insert(all.end(), batch.begin(), batch.end()); } while (more);
    return all;
}
}
int main(int argc, char** argv) {
    try {
        check(argc == 3, "Expected isolated directory and EPUB fixture");
        const std::string root = argv[1], folder = root + "/books";
        std::filesystem::remove_all(root); epub::ensureDirectory(folder + "/nested.epub");
        std::filesystem::copy_file(argv[2], folder + "/Café story.EPuB");
        std::ofstream(folder + "/bad.epub") << "invalid EPUB";
        std::ofstream(folder + "/ignore.txt") << "ignored";
        std::filesystem::create_symlink(folder + "/Café story.EPuB", folder + "/linked.epub");
        std::filesystem::create_symlink(folder + "/gone.epub", folder + "/broken.epub");
        epub::DirectoryCursor cursor;
        cursor.open(folder, false); auto entries = scan(cursor);
        check(entries.size() == 3, "Nonrecursive extension/symlink discovery");
        const auto id = epub::contentIdentity(folder + "/Café story.EPuB");
        check(id == epub::contentIdentity(folder + "/linked.epub"), "Shared content identity");
        for (auto& e : entries) { e.inspected = true; e.identity = id; e.title = "Cached title"; }
        epub::writeDocument(root + "/cache", "library.json", epub::catalogDocument(folder, entries));
        cursor.open(folder, false, root); auto cached = scan(cursor);
        check(cached.size() == 3 && cached[0].inspected && cached[0].title == "Cached title", "Unchanged metadata cache miss");
        std::ofstream(folder + "/bad.epub", std::ios::app) << "changed";
        cursor.open(folder, false, root); auto changed = scan(cursor);
        const auto bad = std::find_if(changed.begin(), changed.end(), [&](const auto& e) { return e.path == folder + "/bad.epub"; });
        check(bad != changed.end() && !bad->inspected && bad->identity.empty(), "Changed content reused cached identity");
        cursor.open(folder, true); check(scan(cursor).size() == 1, "Directory-only chooser discovery");
        epub::Catalog catalog; epub::Session session;
        session.selectedPath = "/library/0500.epub"; session.selectedIndex = 500;
        catalog.begin("/library", session);
        std::vector<epub::LibraryEntry> large;
        for (int n = 999; n >= 0; --n) {
            char name[16]; std::snprintf(name, sizeof(name), "%04d", n);
            epub::LibraryEntry e; e.path = "/library/" + std::string(name) + ".epub"; e.title = name;
            e.identity = n == 500 ? id : std::string(32, 'b'); e.inspected = true; e.finished = n % 3 == 0;
            e.progress = n % 3 == 0 ? 10000 : n % 3 == 1 ? 1 : 0;
            large.push_back(e);
        }
        catalog.observe(large); catalog.finish(true);
        check(catalog.entries.size() == 1000 && catalog.current()->path == session.selectedPath, "Large-library selection restoration");
        check(epub::progressGroupRank(catalog.entries.front().progress) == 0 && epub::progressGroupRank(catalog.entries.back().progress) == 2, "Progress grouping");
        for (size_t n = 1; n < catalog.entries.size(); ++n) check(epub::progressGroupRank(catalog.entries[n - 1].progress) <= epub::progressGroupRank(catalog.entries[n].progress), "Noncontiguous groups");
        for (int progress : {-1, 0, 1, 99, 100, 9999, 10000}) check(epub::progressGroupRank(progress) == (progress > 0 && progress < 10000 ? 0 : progress == 10000 ? 2 : 1), "Raw group boundary");
        check(epub::progressLabel(1) == "<1%" && epub::progressLabel(99) == "<1%" && epub::progressLabel(100) == "1%" && epub::progressLabel(-1) == "0%", "Sub-one percent display");
        check(catalog.top() <= catalog.selected() && catalog.selected() < catalog.top() + epub::libraryRows, "Selected entry offscreen");
        epub::BookState book; book.identity = id; book.path = session.selectedPath; book.title = "Z story"; book.finished = true; book.progress = 10000;
        catalog.update(book); check(catalog.current()->identity == id && catalog.current()->finished, "Completion reorder lost selection");
        book.progress = 4000; catalog.update(book);
        check(catalog.current()->finished && epub::progressGroupRank(catalog.current()->progress) == 0, "Sticky completion incorrectly grouped as Read");
        catalog.remember(session); catalog.begin("/library", session);
        auto renamed = *catalog.current(); renamed.path = "/library/renamed.epub";
        catalog.observe(renamed); catalog.finish(true);
        check(catalog.entries.size() == 1 && catalog.current()->path == renamed.path, "Rename selection/removed-path reconciliation");
        catalog.begin("/library", session); catalog.finish(false);
        check(catalog.entries.size() == 1, "Failed enumeration removed unseen entries");
        catalog.begin("/empty", {}); catalog.finish(true); catalog.move(1); catalog.page(-1);
        check(!catalog.current() && catalog.selected() == 0, "Empty navigation");
        epub::LibraryEntry a; a.path = "/empty/a.epub"; a.title = "a"; auto z = a; z.path = "/empty/z.epub"; z.title = "z";
        catalog.observe(std::vector<epub::LibraryEntry>{z, a}); catalog.page(1);
        check(catalog.current()->title == "z", "Single-page right-to-last");
        catalog.page(-1); check(catalog.current()->title == "a", "Single-page left-to-first");
        bool failed = false;
        try { cursor.open(root + "/missing", false); } catch (const std::exception&) { failed = true; }
        check(failed, "Missing folder not reported");
        std::cout << "PASS: discovery, signatures/cache, copies, 1000-entry ordering, selection, rename/removal and list boundaries\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << "FAIL: " << e.what() << '\n'; return 1; }
}
