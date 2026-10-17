#pragma once
#include <string>

namespace epub {
struct AppPaths { std::string state, contentRoot; };
inline AppPaths defaultAppPaths(const std::string& home, bool device) {
    return device ? AppPaths{"/media/sdcard/epubreader", "/media/sdcard"} : AppPaths{home + "/.epubreader", home};
}
}
