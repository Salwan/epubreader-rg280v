#pragma once
namespace epub {
enum class RepeatMode { All, One, Stop };
inline const char* repeatName(RepeatMode mode) {
    switch (mode) { case RepeatMode::One: return "one"; case RepeatMode::Stop: return "stop"; default: return "all"; }
}
inline const char* repeatLabel(RepeatMode mode) {
    switch (mode) { case RepeatMode::One: return "ONE"; case RepeatMode::Stop: return "STOP"; default: return "ALL"; }
}
}
