#include "reader/metadata.hpp"
#include "library/identity.hpp"
#include <epubfmt.h>
#include <lvdocview.h>
#include <lvfntman.h>
#include <stdexcept>

namespace epub {
LibraryEntry inspectEpub(const std::string& path) {
    LibraryEntry e; e.path = absolutePath(path); e.inspected = true;
    const auto name = baseName(path);
    e.title = name.substr(0, name.size() >= 5 ? name.size() - 5 : name.size());
    struct Fonts { bool owned = false; ~Fonts() { if (owned) ShutdownFontManager(); } } fonts;
    try {
        e.signature = fileSignature(path);
        e.identity = contentIdentity(path);
        auto stream = LVOpenFileStream(Utf8ToUnicode(lString8(path.c_str())).c_str(), LVOM_READ);
        if (stream.isNull() || !DetectEpubFormat(stream)) throw std::runtime_error("Not a readable EPUB");
        // Even metadata-only temporary DOM destruction accesses fontMan.
        // A minimal manager suffices: no faces, layout, or document cache.
        fonts.owned = fontMan == nullptr;
        if (!InitFontManager(lString8())) throw std::runtime_error("Metadata font runtime unavailable");
        ldomDocument document;
        if (!ImportEpubDocument(stream, &document, nullptr, nullptr, true)) throw std::runtime_error("EPUB metadata unavailable");
        const auto props = document.getProps();
        const std::string title = UnicodeToUtf8(props->getStringDef(DOC_PROP_TITLE)).c_str();
        if (!title.empty()) e.title = clipUtf8(title, 4096);
        e.author = clipUtf8(UnicodeToUtf8(props->getStringDef(DOC_PROP_AUTHORS)).c_str(), 4096);
        if (fileSignature(path) != e.signature) {
            e.identity.clear(); e.signature.clear();
            throw std::runtime_error("File changed during inspection; rescan to retry");
        }
    } catch (const std::exception& error) { e.error = error.what(); }
    return e;
}
}
