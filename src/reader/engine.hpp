#pragma once
#include "reader/types.hpp"
#include <functional>
#include <memory>

namespace epub {
// Only the worker owns this adapter and CRengine's process-global state.
class ReaderEngine {
    struct Impl;
    std::unique_ptr<Impl> impl_;
public:
    using Progress = std::function<void(const std::string&, int)>;
    ReaderEngine(const std::string& assets, const std::string& stateDirectory);
    ~ReaderEngine();
    ReaderResult execute(const ReaderRequest& request, Progress progress = {});
    void prefetch(); // At most current +/-1, never changes the authoritative anchor.
};
}
