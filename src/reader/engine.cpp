#include "reader/engine.hpp"
#include "storage/position.hpp"
#include "storage/state.hpp"
#include "ui/canvas.hpp"
#include "library/identity.hpp"
#include "storage/store.hpp"
#include <epubfmt.h>
#include <hyphman.h>
#include <lvdocview.h>
#include <lvfntman.h>
#include <xxhash.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <map>
#include <sstream>
#include <stdexcept>

namespace epub {
namespace {
std::string utf8(const lString32& value) { return UnicodeToUtf8(value).c_str(); }
lString32 unicode(const std::string& value) { return Utf8ToUnicode(lString8(value.c_str())); }
std::string readText(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("Cannot read resource: " + path);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}
std::string hex(XXH128_hash_t hash) {
    std::ostringstream out;
    out << std::hex << std::setfill('0') << std::setw(16) << hash.high64 << std::setw(16) << hash.low64;
    return out.str();
}
std::string hashText(const std::string& text) { return hex(XXH3_128bits(text.data(), text.size())); }
struct Callbacks : LVDocViewCallback {
    ReaderEngine::Progress progress;
    void update(const char* stage, int percent) { if (progress) progress(stage, percent); }
    void OnLoadFileProgress(int p) override { update("Loading EPUB", p); }
    void OnFormatStart() override { update("Paginating", 0); }
    void OnFormatProgress(int p) override { update("Paginating", p); }
    void OnNodeStylesUpdateProgress(int p) override { update("Applying styles", p); }
};
}
struct ReaderEngine::Impl {
    std::string stateDirectory, stylesheet, resourceKey, identity, path, anchor, warning;
    int font = 1, theme = 0;
    bool documentCacheHit = false;
    Callbacks callbacks;
    std::unique_ptr<LVDocView> view;
    struct Image { std::vector<uint32_t> pixels; std::string text, pageAnchor; int images = 0; };
    std::map<int, Image> pages;
    std::vector<int> percentPages;

    Impl(const std::string& assets, std::string directory) : stateDirectory(std::move(directory)) {
        stylesheet = readText(assets + "/styles/reader.css");
        resourceKey = std::string(EPUBREADER_ENGINE_REVISION) + ":layout-v1:dynamic-color-v1:portable-jpeg-v1:640x478:dpi192:margin16,12,16,12:line115:DejaVuSerif:builtin-break:embeddedstyles1:embeddedfonts0:" + hashText(stylesheet);
        for (const char* face : {"DejaVuSerif.ttf", "DejaVuSerif-Bold.ttf", "DejaVuSerif-Italic.ttf", "DejaVuSerif-BoldItalic.ttf"})
            resourceKey += ':' + contentIdentity(assets + "/fonts/" + face);
        ensureDirectory(stateDirectory + "/cache/documents");
        CRLog::setLogLevel(CRLog::LL_WARN);
        if (!InitFontManager(lString8((assets + "/fonts/").c_str()))) throw std::runtime_error("CRengine font initialization failed");
        for (const char* face : {"DejaVuSerif.ttf", "DejaVuSerif-Bold.ttf", "DejaVuSerif-Italic.ttf", "DejaVuSerif-BoldItalic.ttf"})
            if (!fontMan->RegisterFont(lString8((assets + "/fonts/" + face).c_str()))) throw std::runtime_error("CRengine font registration failed: " + std::string(face));
        HyphMan::initDictionaries(lString32());
        enableCacheFileContentsValidation(true);
        if (!ldomDocCache::init(unicode(stateDirectory + "/cache/documents/"), 64 * 1024 * 1024))
            warning = "Document cache unavailable";
    }
    ~Impl() {
        view.reset();
        ldomDocCache::close();
        HyphMan::uninit();
        ShutdownFontManager();
    }
    std::string layoutKey() const { return hashText(identity + ':' + resourceKey + ":font=" + std::to_string(font)); }
    void setTheme() {
        const auto& palette = palettes[static_cast<size_t>(theme)];
        view->setBackgroundColor(palette.background);
        view->setTextColor(palette.text);
        pages.clear(); // Colors change, pagination and anchor do not.
    }
    bool restore(const std::string& value) {
        auto pointer = view->getDocument()->createXPointer(unicode(value));
        if (pointer.isNull() || pointer.toPoint().y < 0) return false;
        view->goToBookmark(pointer);
        view->checkPos();
        anchor = value;
        return true;
    }
    void cacheDocument() {
        if (!ldomDocCache::enabled()) return;
        CRTimerUtil timeout(1000);
        // Direct DOM caching also handles small fixtures (LVDocView's convenience
        // method deliberately skips files smaller than 30K).
        view->getDocument()->swapToCache(timeout);
        view->getDocument()->updateMap(timeout);
    }
    void buildNavigation() {
        const int count = view->getPageCount();
        // Our single-visible-page mode has identical internal/external numbering.
        if (count < 1 || count != view->getPageCount(true)) throw std::runtime_error("Unsupported page numbering");
        percentPages.resize(101);
        const int height = view->GetFullHeight();
        for (int percent = 0; percent <= 100; ++percent) {
            const int y = static_cast<int>(static_cast<int64_t>(height) * percent / 100);
            percentPages[static_cast<size_t>(percent)] = std::clamp(view->getPageList()->FindNearestPage(y, 0), 0, count - 1);
        }
        percentPages.front() = 0; percentPages.back() = count - 1;
        if (!std::is_sorted(percentPages.begin(), percentPages.end())) throw std::runtime_error("Invalid page navigation layout");
    }
    void open(const ReaderRequest& r) {
        view.reset(); pages.clear(); anchor.clear(); identity.clear();
        warning = ldomDocCache::enabled() ? "" : "Document cache unavailable";
        path = r.path; font = std::clamp(r.font, 0, 2); theme = std::clamp(r.theme, 0, 5);
        const auto signature = fileSignature(path);
        identity = contentIdentity(path);
        auto stream = LVOpenFileStream(unicode(path).c_str(), LVOM_READ);
        if (stream.isNull() || !DetectEpubFormat(stream)) throw std::runtime_error("Not a readable, unprotected EPUB: " + path);
        view = std::make_unique<LVDocView>(32, true);
        view->setCallback(&callbacks);
        view->setViewMode(DVM_PAGES, 1);
        view->setVisiblePageCount(1, false);
        view->setPageHeaderInfo(0);
        view->setShowCover(false);
        view->setPageMargins(lvRect(16, 12, 16, 12));
        auto props = LVCreatePropsContainer();
        props->setInt(PROP_RENDER_DPI, 192); // CSS physical units follow the 2x square-pixel renderer.
        props->setInt(PROP_RENDER_SCALE_FONT_WITH_DPI, 0);
        props->setInt(PROP_EMBEDDED_STYLES, 1);
        props->setInt(PROP_EMBEDDED_FONTS, 0);
        props->setInt(PROP_TEXTLANG_HYPHENATION_ENABLED, 0);
        view->propsApply(props);
        view->setDefaultFontFace(lString8("DejaVu Serif"));
        view->setFontSize(std::array<int, 3>{{24, 28, 32}}[static_cast<size_t>(font)]);
        view->setDefaultInterlineSpace(115);
        view->setStyleSheet(lString8(stylesheet.c_str()), false);
        view->Resize(640, 478);
        setTheme();
        // Name the engine document cache by complete content/layout identity.
        // All variants share the single engine-managed 64 MiB LRU directory.
        auto documentProps = view->getDocProps();
        documentProps->setString(DOC_PROP_FILE_NAME, unicode(layoutKey() + ".epub"));
        documentProps->setString(DOC_PROP_FILE_PATH, unicode(path.substr(0, path.find_last_of('/') + 1)));
        documentProps->setInt(DOC_PROP_FILE_SIZE, static_cast<int>(stream->GetSize()));
        documentProps->setHex(DOC_PROP_FILE_CRC32, stream->getcrc32());
        if (!view->LoadDocument(stream) || view->getDocFormat() != doc_format_epub)
            throw std::runtime_error("EPUB load failed (malformed or encrypted): " + path);
        // isRendered() is set only after a successful cache load. A failed cache
        // attempt can leave a cache-file handle even when content was reparsed.
        documentCacheHit = view->getDocument()->isRendered();
        view->checkRender();
        if (view->getPageCount() < 1) throw std::runtime_error("EPUB contains no readable pages");
        if (fileSignature(path) != signature) throw std::runtime_error("Book changed while opening; retry from library");
        std::string saved = r.anchor;
        if (saved.empty()) {
            try { saved = readBook(stateDirectory, identity).anchor; }
            catch (const std::exception&) { warning = "Book state unreadable; opened at beginning"; }
            if (saved.empty()) saved = loadPosition(stateDirectory, identity).anchor;
        }
        if (!saved.empty() && !restore(saved)) warning = "Saved anchor invalid; opened at beginning";
        if (anchor.empty()) anchor = utf8(view->getBookmark().toString());
        buildNavigation();
        cacheDocument();
    }
    Image render(int page) {
        view->goToPage(page);
        LVColorDrawBuf buffer(640, 478, 32);
        view->Draw(buffer, false);
        Canvas square(640, 480), native;
        square.clear(palettes[static_cast<size_t>(theme)].background);
        for (int y = 0; y < 478; ++y) {
            const auto* row = reinterpret_cast<const lUInt32*>(buffer.GetScanLine(y));
            for (int x = 0; x < 640; ++x) square.pixel(x, y, row[x] & 0xffffff);
        }
        native.transformFrom(square, true);
        auto start = view->getBookmark();
        // CRengine's getPageText() uses an inclusive geometric end and may also
        // include the first line of the next page. Use half-open text anchors
        // for diagnostic continuity and bookmark comparisons.
        const auto end = page + 1 < view->getPageCount() ? view->getPageBookmark(page + 1)
            : ldomXPointer(view->getDocument()->getRootNode(), view->getDocument()->getRootNode()->getChildCount());
        ldomXRange range(start, end);
        return {std::move(native.pixels), utf8(range.getRangeText()), utf8(start.toString()), view->getCurrentPageImageCount()};
    }
    ReaderResult snapshot() {
        if (anchor.size() > 16384) throw std::runtime_error("EPUB text anchor exceeds supported bound");
        const int page = view->getCurPage();
        const bool hit = pages.count(page) != 0;
        if (!hit) pages.emplace(page, render(page));
        // A turn or jump may bypass prefetched neighbors. Evict before publishing.
        for (auto i = pages.begin(); i != pages.end();) {
            if (std::abs(i->first - page) > 1) i = pages.erase(i); else ++i;
        }
        const auto& image = pages.at(page);
        ReaderResult result;
        result.page = page; result.pageCount = view->getPageCount();
        result.percentPages = percentPages;
        // Upstream reports the last page's start (and can be negative for a
        // one-page document). Our progress represents the displayed page end.
        result.progress = page == result.pageCount - 1 ? 10000 : std::clamp(view->getPosEndPagePercent(), 0, 10000);
        result.font = font; result.theme = theme;
        result.pageCacheHit = hit; result.documentCacheHit = documentCacheHit;
        result.identity = identity; result.path = path; result.anchor = anchor; result.pageAnchor = image.pageAnchor;
        result.title = clipUtf8(utf8(view->getTitle()), 4096); result.author = clipUtf8(utf8(view->getAuthors()), 4096);
        result.text = image.text; result.images = image.images; result.pixels = image.pixels;
        result.layoutKey = layoutKey(); result.message = warning;
        const auto pointer = view->getDocument()->createXPointer(unicode(anchor));
        result.anchorOnPage = !pointer.isNull() && view->getBookmarkPage(pointer) == page;
        return result;
    }
};
ReaderEngine::ReaderEngine(const std::string& assets, const std::string& directory) : impl_(std::make_unique<Impl>(assets, directory)) {}
ReaderEngine::~ReaderEngine() = default;
ReaderResult ReaderEngine::execute(const ReaderRequest& r, Progress progress) {
    const auto start = std::chrono::steady_clock::now();
    auto& e = *impl_;
    e.callbacks.progress = std::move(progress);
    if (r.command == ReaderCommand::Open) {
        try { e.open(r); }
        catch (...) {
            // A parser may leave an engine-generated error document behind.
            // Never publish/navigate/save that as a successful opened book.
            e.view.reset(); e.pages.clear(); e.identity.clear(); e.anchor.clear();
            throw;
        }
    }
    else {
        if (!e.view || e.identity.empty()) throw std::runtime_error("No EPUB is open");
        if (r.command == ReaderCommand::Turn || r.command == ReaderCommand::Page) {
            const int page = std::clamp(r.command == ReaderCommand::Turn ? e.view->getCurPage() + r.value : r.value, 0, e.view->getPageCount() - 1);
            if (page != e.view->getCurPage()) {
                e.view->goToPage(page);
                e.anchor = utf8(e.view->getBookmark().toString());
            }
        } else if (r.command == ReaderCommand::Font) {
            e.font = std::clamp(r.value, 0, 2);
            e.pages.clear();
            e.view->setFontSize(std::array<int, 3>{{24, 28, 32}}[static_cast<size_t>(e.font)]);
            e.view->checkRender();
            if (!e.restore(e.anchor)) throw std::runtime_error("Text anchor lost during pagination");
            e.buildNavigation();
            // The next open builds this variant's keyed document cache lazily.
        } else if (r.command == ReaderCommand::Theme) {
            e.theme = std::clamp(r.value, 0, 5); e.setTheme();
        } else if (r.command == ReaderCommand::Restore && !e.restore(r.anchor)) {
            throw std::runtime_error("Invalid text bookmark");
        }
    }
    auto result = e.snapshot();
    result.elapsedMs = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count());
    e.callbacks.progress = {};
    return result;
}
void ReaderEngine::prefetch() {
    auto& e = *impl_;
    if (!e.view || e.identity.empty()) return;
    e.callbacks.progress = {};
    const int current = e.view->getCurPage();
    // A blank page's fallback text bookmark can resolve to a different page.
    // Cache work must restore this exact layout page and leave e.anchor alone.
    try {
        for (int page : {current + 1, current - 1}) {
            if (page >= 0 && page < e.view->getPageCount() && !e.pages.count(page)) e.pages.emplace(page, e.render(page));
        }
    } catch (...) {
        e.view->goToPage(current);
        throw;
    }
    e.view->goToPage(current);
}
}
