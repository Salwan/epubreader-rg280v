#include "app/controller.hpp"
#include "library/identity.hpp"
#include <algorithm>
#include <iostream>
#include <iomanip>

namespace epub {
Controller::Controller(Store& store, ReaderWorker& worker, const std::string& bookOverride) : store_(store), worker_(worker) {
    page.font = store.config.preferences.font; page.theme = store.config.preferences.theme;
    if (!bookOverride.empty()) open(absolutePath(bookOverride));
    else if (store.session.reader) {
        resumeIdentity_ = store.session.identity; resumePath_ = store.session.path;
        open(resumePath_, resumeIdentity_, true);
    } else scan(false);
}
bool Controller::submit(ReaderRequest r) {
    if (!worker_.submit(r)) return false;
    activeCommand_ = r.command; jobSince_ = now_; changed_ = true; return true;
}
void Controller::cache() {
    if (!enumerationDone_) return;
    store_.saveCatalog([folder = catalog.folder(), entries = catalog.entries] { return catalogDocument(folder, entries); });
    inspectedSinceSave_ = 0;
}
void Controller::checkpoint() {
    if (page.anchor.empty() || currentBook_.identity.empty()) return;
    store_.checkpoint(currentBook_); positionDirty_ = false;
}
void Controller::scan(bool retryErrors) {
    worker_.restart(); screen_ = Screen::Library; transition_ = changed_ = true;
    libraryMenu = false; readerControls = {}; openPending_ = false;
    page = {}; currentBook_ = {}; positionDirty_ = false;
    store_.session.reader = false; store_.saveSession();
    catalog.begin(store_.config.books, store_.session);
    scanPending_ = true; more_ = enumerationDone_ = indexDone_ = false; retryErrors_ = retryErrors;
    inspections_ = cacheHits_ = inspectedSinceSave_ = 0;
    status = "Scanning library";
}
void Controller::open(const std::string& path, const std::string& expected, bool resume) {
    if (screen_ == Screen::Library) { catalog.remember(store_.session); cache(); }
    checkpoint(); worker_.restart();
    screen_ = Screen::Opening; transition_ = changed_ = true;
    readerControls = {}; libraryMenu = false;
    pendingPath_ = path; expectedIdentity_ = expected; openPending_ = true; resumeSearch_ = resume;
    page = {}; page.path = path; page.font = store_.config.preferences.font; page.theme = store_.config.preferences.theme;
    currentBook_ = {}; positionDirty_ = false; status = "Opening " + baseName(path); notice.clear();
}
void Controller::failOpen(const std::string& error) {
    // Opening never committed a session or an anchor. Recover to the library.
    const auto failedPath = pendingPath_;
    if (!resumeSearch_) {
        for (auto& entry : catalog.entries) if (entry.path == failedPath) entry.error = error;
        cache();
    }
    scan(false); notice = error;
}
void Controller::receive(ReaderResult r) {
    if (r.generation != worker_.generation()) return;
    changed_ = true;
    if (!r.complete) { status = r.message + " " + std::to_string(r.progress) + "%"; return; }
    if (r.kind == ResultKind::Directory) {
        if (screen_ == Screen::FolderChooser) {
            if (!r.error.empty()) { notice = r.error; chooserMore_ = false; chooserAvailable_ = false; return; }
            chooserAvailable_ = true; chooserMore_ = r.more;
            folders.insert(folders.end(), r.entries.begin(), r.entries.end());
            const auto selected = folderRow >= 2 && static_cast<size_t>(folderRow - 2) < folders.size() ? folders[static_cast<size_t>(folderRow - 2)].path : "";
            std::sort(folders.begin(), folders.end(), [](const auto& a, const auto& b) { return std::make_pair(foldAscii(a.title), a.path) < std::make_pair(foldAscii(b.title), b.path); });
            if (!selected.empty()) for (size_t n = 0; n < folders.size(); ++n) if (folders[n].path == selected) folderRow = static_cast<int>(n) + 2;
            folderTop = std::clamp(folderTop, std::max(0, folderRow - libraryRows + 1), folderRow);
            status = chooserMore_ ? "Scanning folders" : "Choose a folder";
        } else if (screen_ == Screen::Library) {
            if (!r.error.empty()) {
                more_ = false; indexDone_ = true; catalog.finish(false); status = "Folder unavailable"; notice = r.error;
                if (resumeSearch_) { resumeSearch_ = false; store_.session.reader = false; store_.saveSession(); }
                return;
            }
            for (const auto& entry : r.entries) if (entry.inspected && !entry.identity.empty()) ++cacheHits_;
            more_ = r.more; catalog.observe(std::move(r.entries));
            if (!more_) { enumerationDone_ = true; catalog.finish(true); }
        }
        return;
    }
    if (r.kind == ResultKind::Metadata && screen_ == Screen::Library) {
        if (!r.error.empty() || r.entries.empty()) {
            LibraryEntry e;
            const auto i = std::find_if(catalog.entries.begin(), catalog.entries.end(), [&](const auto& entry) { return entry.path == inspecting_; });
            if (i != catalog.entries.end()) e = *i;
            e.path = inspecting_; e.inspected = true; e.error = r.error.empty() ? "Metadata worker failed" : r.error;
            catalog.observe(std::move(e)); worker_.restart();
        } else catalog.observe(std::move(r.entries));
        if (++inspectedSinceSave_ >= 16) cache();
        return;
    }
    if (screen_ != Screen::Opening && screen_ != Screen::Reader) return;
    if (!r.error.empty()) {
        std::cerr << "READER ERROR: " << r.error << '\n';
        if (r.command == ReaderCommand::Open) failOpen(r.error);
        else { notice = r.error; readerControls.showMenu(); transition_ = true; }
        return;
    }
    if (r.pixels.empty()) return;
    if (!expectedIdentity_.empty() && r.identity != expectedIdentity_) { failOpen("Resume book changed; searching library"); return; }
    expectedIdentity_.clear(); resumeSearch_ = false;
    const bool opened = screen_ == Screen::Opening;
    screen_ = Screen::Reader; if (opened) transition_ = true;
    page = std::move(r); status = page.message;
    auto& prefs = store_.config.preferences;
    if (prefs.font != page.font || prefs.theme != page.theme) {
        prefs.font = page.font; prefs.theme = page.theme; store_.saveConfig();
    }
    if (currentBook_.identity != page.identity) currentBook_ = store_.book(page.identity);
    currentBook_.path = page.path; currentBook_.anchor = page.anchor; currentBook_.title = page.title; currentBook_.author = page.author;
    currentBook_.progress = page.progress; currentBook_.page = page.page; currentBook_.pageCount = page.pageCount;
    currentBook_.font = page.font; currentBook_.layoutKey = page.layoutKey;
    const bool final = page.pageCount > 0 && page.page == page.pageCount - 1;
    if (final && ((page.command == ReaderCommand::Turn && page.commandValue > 0) || (page.command == ReaderCommand::Page && page.pageCount > 1))) currentBook_.finished = true;
    catalog.update(currentBook_);
    store_.session.reader = true; store_.session.path = page.path; store_.session.identity = page.identity;
    positionDirty_ = true; saveSince_ = now_;
    std::cout << "PAGE " << page.page + 1 << '/' << page.pageCount << " identity=" << page.identity << " path=" << std::quoted(page.path)
        << " progress=" << page.progress << " font=" << page.font << " theme=" << page.theme
        << " ms=" << page.elapsedMs << " page-cache=" << page.pageCacheHit << " document-cache=" << page.documentCacheHit << " anchor=" << page.anchor << '\n';
}
void Controller::tick(uint32_t now) {
    now_ = now;
    for (int n = 0; n < 16; ++n) { auto result = worker_.poll(); if (!result) break; receive(std::move(*result)); }
    if (const auto error = store_.error(); !error.empty()) { notice = error; changed_ = true; std::cerr << error << '\n'; }
    if (now_ - retrySince_ >= 1000) { store_.retryFailed(); retrySince_ = now_; }
    if (positionDirty_ && now_ - saveSince_ >= 250) checkpoint();
    if (worker_.busy()) {
        if (screen_ == Screen::Library && now_ - jobSince_ >= 30000) {
            ReaderResult timeout; timeout.generation = worker_.generation(); timeout.command = activeCommand_;
            timeout.kind = activeCommand_ == ReaderCommand::Inspect ? ResultKind::Metadata : ResultKind::Directory;
            timeout.error = "Index job timed out; rescan to retry"; receive(std::move(timeout));
            if (worker_.busy()) worker_.restart();
        }
        return;
    }
    if (screen_ == Screen::Opening && openPending_ && store_.idle()) {
        const auto& p = store_.config.preferences;
        if (submit({ReaderCommand::Open, 0, p.font, p.theme, pendingPath_})) openPending_ = false;
    } else if (screen_ == Screen::FolderChooser) {
        if (chooserPending_) { if (submit({ReaderCommand::List, 1, 1, 0, folderPath})) chooserPending_ = false; }
        else if (chooserMore_) submit({ReaderCommand::NextBatch});
    } else if (screen_ == Screen::Library && !indexDone_ && store_.idle()) {
        if (scanPending_) { if (submit({ReaderCommand::List, retryErrors_ ? 2 : 0, 1, 0, store_.config.books})) { scanPending_ = false; indexSince_ = now_; } }
        else if (more_) submit({ReaderCommand::NextBatch});
        else {
            inspecting_ = catalog.nextInspection();
            if (!inspecting_.empty()) { status = "Indexing " + baseName(inspecting_); if (submit({ReaderCommand::Inspect, 0, 1, 0, inspecting_})) ++inspections_; }
            else {
                indexDone_ = true; status = catalog.entries.empty() ? "No EPUB books in this folder" : "Library ready";
                std::cout << "INDEX folder=" << std::quoted(catalog.folder()) << " entries=" << catalog.entries.size() << " inspected=" << inspections_ << " cache-hits=" << cacheHits_ << " ms=" << now_ - indexSince_ << '\n';
                cache(); catalog.remember(store_.session); store_.saveSession(); changed_ = true;
                if (resumeSearch_) {
                    resumeSearch_ = false;
                    const auto i = std::find_if(catalog.entries.begin(), catalog.entries.end(), [&](const auto& e) { return e.identity == resumeIdentity_ && e.path != resumePath_; });
                    if (i != catalog.entries.end()) { const auto path = i->path; open(path, resumeIdentity_); }
                    else notice = "Resume book unavailable; choose a book";
                }
            }
        }
    }
}
void Controller::chooseFolder(bool music) { musicFolder_ = music; enterFolder(music ? store_.config.music : store_.config.books); }
void Controller::enterFolder(const std::string& path) {
    worker_.restart(); screen_ = Screen::FolderChooser; transition_ = changed_ = true; libraryMenu = false;
    folderPath = absolutePath(path); folders.clear(); folderRow = folderTop = 0;
    chooserPending_ = true; chooserMore_ = chooserAvailable_ = false; notice.clear(); status = "Scanning folders";
}
void Controller::folderAction(Action a) {
    if (a == Action::B) { chooserPending_ = chooserMore_ = false; scan(false); return; }
    const int count = static_cast<int>(folders.size()) + 2;
    if (a == Action::Up || a == Action::Down) folderRow = std::clamp(folderRow + (a == Action::Down ? 1 : -1), 0, count - 1);
    if (a == Action::Left || a == Action::Right) folderRow = std::clamp(folderRow + (a == Action::Right ? libraryRows : -libraryRows), 0, count - 1);
    folderTop = std::clamp(folderTop, std::max(0, folderRow - libraryRows + 1), folderRow);
    if (a != Action::AShort) return;
    if (folderRow == 0) {
        if (!chooserAvailable_) { notice = "Cannot select an unavailable folder"; return; }
        if (musicFolder_) store_.config.music = folderPath; else store_.config.books = folderPath;
        store_.saveConfig(); chooserPending_ = chooserMore_ = false; scan(false);
    } else if (folderRow == 1) enterFolder(parentPath(folderPath));
    else enterFolder(folders.at(static_cast<size_t>(folderRow - 2)).path);
}
void Controller::action(Action a) {
    if (a == Action::None) return;
    changed_ = true;
    if (a == Action::Quit) { quit_ = true; return; }
    if (screen_ == Screen::FolderChooser) { folderAction(a); return; }
    if (screen_ == Screen::Library) {
        resumeSearch_ = false; // Explicit library interaction supersedes automatic recovery.
        if (a == Action::X) { libraryMenu = !libraryMenu; menuRow = 0; transition_ = true; return; }
        if (libraryMenu) {
            if (a == Action::B) { libraryMenu = false; transition_ = true; }
            if (a == Action::Up || a == Action::Down) menuRow = std::clamp(menuRow + (a == Action::Down ? 1 : -1), 0, 3);
            if (a == Action::AShort) {
                if (menuRow == 0) { resumeSearch_ = false; scan(true); }
                else if (menuRow == 1 || menuRow == 2) chooseFolder(menuRow == 2);
                else quit_ = true;
            }
            return;
        }
        if (a == Action::Up || a == Action::Down) catalog.move(a == Action::Down ? 1 : -1);
        if (a == Action::Left || a == Action::Right) catalog.page(a == Action::Right ? 1 : -1);
        if (a == Action::L2 || a == Action::R2) {
            auto& theme = store_.config.preferences.theme; theme = (theme + (a == Action::R2 ? 1 : 5)) % 6; store_.saveConfig();
        }
        if (a == Action::AShort) if (const auto* e = catalog.current()) { const auto path = e->path; open(path); }
        return;
    }
    const bool wasOpen = readerControls.menuOpen();
    const auto control = readerControls.handle(a, store_.config.preferences.font, store_.config.preferences.theme, worker_.busy() || openPending_);
    if (wasOpen != readerControls.menuOpen()) transition_ = true;
    using E = ReaderEffect; using C = ReaderCommand;
    switch (control.effect) {
    case E::Quit: quit_ = true; break;
    case E::Back:
        catalog.remember(store_.session); store_.session.reader = false; checkpoint(); scan(false); break;
    case E::Turn: submit({C::Turn, control.value}); break;
    case E::Font: submit({C::Font, control.value}); break;
    case E::Theme: submit({C::Theme, control.value}); break;
    case E::Refresh: case E::None: break;
    }
}
void Controller::shutdown() {
    if (screen_ == Screen::Library || screen_ == Screen::FolderChooser) { catalog.remember(store_.session); store_.session.reader = false; }
    checkpoint();
    if (page.anchor.empty()) store_.saveSession();
    store_.saveConfig(); worker_.stop(); store_.retryFailed(); store_.flush();
    for (;;) { const auto error = store_.error(); if (error.empty()) break; std::cerr << error << '\n'; }
}
}
