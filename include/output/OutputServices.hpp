#pragma once

#include "output/OutputTypes.hpp"
#include <atomic>

namespace V2::Output {
// Application callbacks are installed at composition time. Device workers
// evaluate the gate again immediately before sending a normal action.
struct OutputServiceCallbacks {
    bool (*suppressed)() = nullptr;
    bool (*debugLog)() = nullptr;
    bool (*verboseLog)() = nullptr;
};

inline std::atomic<bool (*)()> outputSuppressionSource{nullptr};
inline std::atomic<bool (*)()> outputDebugSource{nullptr};
inline std::atomic<bool (*)()> outputVerboseSource{nullptr};

inline void SetOutputServices(OutputServiceCallbacks callbacks) {
    outputSuppressionSource.store(callbacks.suppressed);
    outputDebugSource.store(callbacks.debugLog);
    outputVerboseSource.store(callbacks.verboseLog);
}
inline bool OutputSuppressed() {
    auto callback = outputSuppressionSource.load();
    return callback && callback();
}
inline bool OutputDebugLog() {
    auto callback = outputDebugSource.load();
    return callback && callback();
}
inline bool OutputVerboseLog() {
    auto callback = outputVerboseSource.load();
    return callback && callback();
}
inline bool SuppressOutput(OutputIntent intent) {
    return ShouldSuppressOutputForMenu(OutputSuppressed(), intent);
}
}
