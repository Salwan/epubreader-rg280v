#include "storage/store.hpp"
#include "storage/position.hpp"
#include "platform/paths.hpp"
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
        {
            epub::Store s(root + "/volume", root);
            s.config.preferences.mixer = "PCM,0"; s.config.preferences.maximum = 31;
            s.config.preferences.rawLeft = 18; s.config.preferences.rawRight = 17; s.config.preferences.volume = 58;
            s.config.preferences.theme = 5; s.config.preferences.font = 2; s.config.music = root + "/Music with spaces";
            epub::ensureDirectory(root + "/volume/config.json.tmp"); s.saveConfig(); s.flush();
            check(!s.error().empty(), "Volume save failure was not reported");
            std::filesystem::remove_all(root + "/volume/config.json.tmp");
            s.config.preferences.rawLeft = 20; s.config.preferences.volume = 64; s.config.preferences.theme = 4;
            s.saveConfig(); s.retryFailed(); s.flush(); check(s.error().empty(), "Volume save retry failed");
        }
        {
            epub::Store s(root + "/volume", root); const auto& p = s.config.preferences;
            check(p.mixer == "PCM,0" && p.maximum == 31 && p.rawLeft == 20 && p.rawRight == 17 && p.volume == 64,
                "Newest raw volume snapshot was not durable");
            check(p.theme == 4 && p.font == 2 && s.config.music == root + "/Music with spaces", "Volume save lost unrelated preferences");
        }
        {
            const auto path = root + "/critical";
            epub::Store s(path, root); epub::BookState b; b.identity = id; b.anchor = "/completed-page"; b.path = root + "/book.epub";
            s.session.reader = false; s.session.selectedPath = b.path; s.config.musicRepeat = epub::RepeatMode::One;
            epub::ensureDirectory(path + "/books/" + id + ".json.tmp");
            const auto ticket = s.criticalCheckpoint(b); s.flush();
            check(s.idle() && s.criticalResult(ticket).state == epub::CheckpointState::Failed, "Drained failed queue claimed critical success");
            check(s.criticalResult(ticket).error.find("book:") == 0 && epub::readDocument(path + "/session.json").empty(), "Critical session advanced past failed book");
            std::filesystem::remove_all(path + "/books/" + id + ".json.tmp");
            check(s.retryCritical(ticket), "Failed critical data not retryable"); s.flush();
            check(s.criticalResult(ticket).state == epub::CheckpointState::Succeeded && s.criticalResult(ticket).attempts == 2 && !s.retryCritical(ticket), "Bounded critical retry/acknowledgement");
            check(epub::readBook(path, id).anchor == b.anchor, "Critical retry lost captured anchor");
            std::atomic<bool> started{false}, release{false}, priority{false};
            s.saveCatalog([&] { started = true; while (!release) std::this_thread::sleep_for(std::chrono::milliseconds(1)); return "{\"schema\":1}"; });
            while (!started) std::this_thread::sleep_for(std::chrono::milliseconds(1));
            b.anchor = "/new-completed-page";
            s.saveCatalog([&] { priority = epub::readBook(path, id).anchor == b.anchor; return "{\"schema\":1}"; });
            const auto next = s.criticalCheckpoint(b);
            check(s.criticalResult(next).state == epub::CheckpointState::Pending, "Active storage work falsely acknowledged");
            release = true; s.flush(); check(priority && s.criticalResult(next).state == epub::CheckpointState::Succeeded, "Critical work did not precede rebuildable catalog");
            epub::ensureDirectory(path + "/config.json.tmp"); const auto failedConfig = s.criticalCheckpoint(b); s.flush();
            check(s.criticalResult(failedConfig).state == epub::CheckpointState::Failed && s.criticalResult(failedConfig).error.find("config:") == 0, "Config-specific failure lost");
            std::filesystem::remove_all(path + "/config.json.tmp"); check(s.retryCritical(failedConfig), "Config retry rejected"); s.flush();
        }
        {
            epub::Store s(root + "/critical", root);
            check(s.config.musicRepeat == epub::RepeatMode::One && !s.session.reader && s.book(id).anchor == "/new-completed-page", "Fresh-store critical/repeat roundtrip");
            s.config.musicRepeat = epub::RepeatMode::Stop; s.saveConfig(); s.flush();
        }
        {
            epub::Store s(root + "/critical", root); check(s.config.musicRepeat == epub::RepeatMode::Stop, "STOP repeat roundtrip");
        }
        epub::atomicWrite(root + "/repeat-invalid", "config.json", "{\"schema\":1,\"books\":\"/books\",\"music\":\"/music\",\"theme\":\"warm2\",\"font\":\"large\",\"musicRepeat\":42}");
        {
            epub::Store s(root + "/repeat-invalid", root);
            check(s.config.musicRepeat == epub::RepeatMode::All && s.config.preferences.theme == 5 && s.config.preferences.font == 2 && !s.error().empty(), "Invalid repeat discarded valid settings or omitted notice");
        }
        const auto device = epub::defaultAppPaths(root + "/firmware-home", true);
        const auto host = epub::defaultAppPaths(root + "/preview-home", false);
        check(device.state == "/media/sdcard/epubreader" && device.contentRoot == "/media/sdcard", "Adam defaults do not use the user SD card");
        check(host.state == root + "/preview-home/.epubreader" && host.contentRoot == root + "/preview-home", "Host defaults escaped the preview home");
        // Exercise the device layout entirely under the build-local test root.
        const auto card = root + "/sdcard", state = card + "/epubreader";
        {
            epub::Store s(state, card);
            check(s.config.books == card + "/books" && s.config.music == card + "/music", "Fresh SD-card content defaults");
            s.config.preferences.theme = 4; s.config.musicRepeat = epub::RepeatMode::Stop;
            s.config.books = card + "/My Books"; s.config.music = card + "/My Music";
            epub::BookState b; b.identity = id; b.path = s.config.books + "/book.epub"; b.anchor = "/sdcard-page"; b.finished = true;
            s.session.selectedPath = b.path; s.session.selectedIdentity = id;
            const auto ticket = s.criticalCheckpoint(b); s.flush();
            check(s.criticalResult(ticket).state == epub::CheckpointState::Succeeded, "SD-card critical checkpoint failed");
            s.saveCatalog([] { return "{\"schema\":1}"; }); s.flush();
        }
        {
            epub::Store s(state, root + "/replacement-firmware-home");
            check(s.root() == state && s.config.books == card + "/My Books" && s.config.music == card + "/My Music", "Saved SD-card folder choices lost after HOME changes");
            check(s.config.preferences.theme == 4 && s.config.musicRepeat == epub::RepeatMode::Stop && s.book(id).anchor == "/sdcard-page" && s.book(id).finished && s.session.selectedIdentity == id, "SD-card settings/bookmarks/session did not survive firmware HOME replacement");
            check(std::filesystem::exists(state + "/cache/library.json") && !std::filesystem::exists(root + "/replacement-firmware-home"), "SD-card cache/state escaped to firmware HOME");
        }
        std::cout << "PASS: versioned production state, migration, save ordering, interrupted writes, schema recovery\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << "FAIL: " << e.what() << '\n'; return 1; }
}
