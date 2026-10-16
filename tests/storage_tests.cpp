#include "storage/store.hpp"
#include "storage/position.hpp"
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <atomic>
#include <thread>
#include <chrono>

namespace {
void check(bool value, const std::string& message) { if (!value) throw std::runtime_error(message); }
const std::string id(32, 'a');
}
int main(int argc, char** argv) {
    try {
        check(argc == 2, "Expected isolated state directory");
        const std::string root = argv[1];
        std::filesystem::remove_all(root);
        epub::State legacy; legacy.theme = 5; legacy.font = 2;
        epub::saveState(root + "/slice", legacy);
        epub::savePosition(root + "/slice", {id, root + "/old name.epub", "/body/section.0"});
        {
            epub::Store store(root, root);
            check(store.config.preferences.theme == 5 && store.config.preferences.font == 2, "Legacy preferences migration");
            check(!store.session.reader && store.config.books == root + "/books", "Fresh list/default folders");
            auto b = store.book(id);
            check(b.anchor == "/body/section.0" && b.progress == -1 && !b.finished, "Legacy bookmark migration");
            b.anchor = "/body/section.23"; b.progress = 7000; b.finished = true;
            store.session.reader = true; store.session.path = b.path; store.session.identity = id;
            const auto saveStart = std::chrono::steady_clock::now();
            store.checkpoint(b); store.flush();
            std::cout << "CHECKPOINT book+session ms=" << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - saveStart).count() << '\n';
            check(store.error().empty(), "Unexpected save error");
            const std::string next(32, 'b');
            epub::ensureDirectory(root + "/books/" + next + ".json.tmp");
            epub::BookState other; other.identity = next; other.anchor = "/other.0"; other.path = root + "/new.epub";
            store.session.identity = next; store.session.path = other.path;
            store.checkpoint(other); store.flush();
            check(!store.error().empty(), "Write failure not reported");
            check(epub::readDocument(root + "/session.json").find(id) != std::string::npos, "Session advanced past failed checkpoint");
            std::filesystem::remove_all(root + "/books/" + next + ".json.tmp");
            store.session.reader = false; store.saveSession(); store.retryFailed(); store.flush();
            check(epub::readBook(root, next).anchor == other.anchor, "Retained failed checkpoint retry");
            check(epub::readDocument(root + "/session.json").find("\"mode\": \"list\"") != std::string::npos, "Retried old checkpoint replaced newer list session");
            store.checkpoint(other); store.flush();
            check(epub::readBook(root, next).anchor == other.anchor, "Checkpoint retry failed");
            store.session.reader = false; store.session.selectedPath = b.path; store.session.selectedIdentity = id;
            store.saveSession();
        }
        {
            epub::Store reopened(root, root);
            check(reopened.book(id).finished && reopened.book(id).progress == 7000, "Durable independent book record");
            check(reopened.book(id).anchor == "/body/section.23", "Migration overwrote newer progress");
            check(!reopened.session.reader && reopened.session.selectedIdentity == id, "List session roundtrip");
        }
        epub::atomicWrite(root, "config.json.tmp", "interrupted");
        {
            epub::Store s(root, root);
            check(s.config.preferences.theme == 5, "Temporary file replaced committed config");
        }
        epub::atomicWrite(root, "config.json", "{\"schema\":99,\"theme\":\"light\"}");
        {
            epub::Store s(root, root);
            check(!s.error().empty(), "Unknown schema not reported");
            s.saveConfig(); s.flush();
            check(!s.error().empty(), "Unknown schema overwritten");
            check(std::filesystem::exists(root + "/config.json"), "Future config was removed");
        }
        std::filesystem::remove(root + "/config.json.tmp");
        epub::atomicWrite(root, "config.json", "{broken");
        {
            epub::Store s(root, root);
            check(!s.error().empty() && std::filesystem::exists(root + "/config.json.invalid"), "Malformed file not preserved");
            check(s.book(id).finished, "Malformed config discarded book state");
            s.saveConfig(); s.flush(); check(s.error().empty(), "Config default recovery failed");
        }
        epub::atomicWrite(root + "/invalid-legacy/slice", "state.ini", "version 900\ntheme 5\n");
        {
            epub::Store s(root + "/invalid-legacy", root);
            check(!s.error().empty() && s.config.preferences.theme == 0, "Unsupported legacy state silently imported");
        }
        const std::string shortDocument = "{\"schema\":1,\"value\":\"short\"}",
            longDocument = "{\"schema\":1,\"value\":\"" + std::string(16000, 'x') + "\"}";
        epub::atomicWrite(root, "concurrent.json", shortDocument);
        std::atomic<bool> writing{true};
        std::thread replacement([&] {
            for (int n = 0; n < 100; ++n) epub::atomicWrite(root, "concurrent.json", n % 2 ? shortDocument : longDocument);
            writing = false;
        });
        bool complete = true;
        do {
            try {
                const auto data = epub::readDocument(root + "/concurrent.json");
                if (data != shortDocument && data != longDocument) complete = false;
            } catch (const std::exception&) { complete = false; }
        } while (writing);
        replacement.join(); check(complete, "Atomic reader mixed old/new file inodes");
        std::cout << "PASS: versioned production state, migration, save ordering, interrupted writes, schema recovery\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << "FAIL: " << e.what() << '\n'; return 1; }
}
