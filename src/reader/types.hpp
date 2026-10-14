#pragma once
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace epub {
enum class ReaderCommand { Open, Turn, Font, Theme, Page, Restore };
struct ReaderRequest {
    ReaderCommand command = ReaderCommand::Open;
    int value = 0, font = 1, theme = 0;
    std::string path, anchor;
    ReaderRequest(ReaderCommand c = ReaderCommand::Open, int v = 0, int f = 1,
                  int t = 0, std::string p = {}, std::string a = {})
        : command(c), value(v), font(f), theme(t), path(std::move(p)), anchor(std::move(a)) {}
};
struct ReaderResult {
    bool complete = true, pageCacheHit = false, documentCacheHit = false, anchorOnPage = false;
    int page = 0, pageCount = 0, progress = 0, font = 1, theme = 0, images = 0;
    uint64_t elapsedMs = 0;
    std::string error, message, identity, path, title, author, anchor, pageAnchor, text, layoutKey;
    std::vector<uint32_t> pixels; // 320x480 native RGB888, with no UI overlays
};
}
