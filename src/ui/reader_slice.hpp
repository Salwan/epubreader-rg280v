#pragma once
#include "reader/types.hpp"
#include "ui/canvas.hpp"
#include "ui/reader_controls.hpp"
#include "audio/music.hpp"

namespace epub {
void renderReaderSlice(Canvas& canvas, const ReaderResult& page, int theme,
                       const ReaderControls& controls, bool busy, const std::string& status,
                       const MusicSnapshot& music = {});
}
