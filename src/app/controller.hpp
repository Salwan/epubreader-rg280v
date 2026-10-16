#pragma once
#include "library/catalog.hpp"
#include "reader/worker.hpp"
#include "ui/reader_controls.hpp"
#include "audio/music.hpp"

namespace epub {
enum class Screen { Library, Opening, Reader, FolderChooser };
class Controller {
    Store& store_;
    ReaderWorker& worker_;
    MusicPort* music_;
    Screen screen_ = Screen::Library;
    bool changed_ = true, transition_ = false, quit_ = false;
    bool scanPending_ = false, more_ = false, enumerationDone_ = false, indexDone_ = false, retryErrors_ = false;
    bool openPending_ = false, positionDirty_ = false, resumeSearch_ = false;
    std::string pendingPath_, expectedIdentity_, resumeIdentity_, resumePath_, inspecting_;
    ReaderCommand activeCommand_ = ReaderCommand::Close;
    uint32_t now_ = 0, jobSince_ = 0, saveSince_ = 0, retrySince_ = 0;
    unsigned inspectedSinceSave_ = 0;
    unsigned inspections_ = 0, cacheHits_ = 0;
    uint32_t indexSince_ = 0;
    BookState currentBook_;
    bool chooserPending_ = false, chooserMore_ = false, chooserAvailable_ = false, musicFolder_ = false;
    void scan(bool retryErrors);
    void open(const std::string& path, const std::string& expected = {}, bool resume = false);
    void receive(ReaderResult result);
    void failOpen(const std::string& error);
    void checkpoint();
    void cache();
    bool submit(ReaderRequest request);
    void chooseFolder(bool music);
    void enterFolder(const std::string& path);
    void folderAction(Action action);
    void requestQuit();
public:
    Catalog catalog;
    ReaderResult page;
    ReaderControls readerControls;
    std::string status, notice, folderPath;
    std::vector<LibraryEntry> folders;
    bool libraryMenu = false;
    int menuRow = 0, folderRow = 0, folderTop = 0;
    Controller(Store& store, ReaderWorker& worker, const std::string& bookOverride = {}, MusicPort* music = nullptr);
    MusicSnapshot music() const { return music_ ? music_->snapshot() : MusicSnapshot{}; }
    void action(Action action);
    void tick(uint32_t now);
    void shutdown();
    void consumeTransition(Input& input);
    Screen screen() const { return screen_; }
    const Config& config() const { return store_.config; }
    bool busy() const { return worker_.busy() || openPending_ || (screen_ == Screen::Library && !indexDone_) || chooserPending_ || chooserMore_; }
    bool quit() const { return quit_; }
    unsigned inspections() const { return inspections_; }
    unsigned cacheHits() const { return cacheHits_; }
    bool changed() { const bool c = changed_; changed_ = false; return c; }
    bool transition() { const bool t = transition_; transition_ = false; return t; }
};
}
