#include "storage/state.hpp"
#include "ui/theme.hpp"
#include <algorithm>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <sys/stat.h>
#include <unistd.h>

namespace epub {
void ensureDirectory(const std::string& path) {
    if (path.empty()) throw std::runtime_error("Empty state directory");
    for (size_t end = 1; end <= path.size(); ++end) if (end == path.size() || path[end] == '/') {
        const std::string part = path.substr(0, end);
        if (::mkdir(part.c_str(), 0700) != 0 && errno != EEXIST)
            throw std::runtime_error("mkdir " + part + ": " + std::strerror(errno));
    }
}
State loadState(const std::string& directory) {
    State defaults, s;
    std::ifstream f(directory + "/state.ini");
    std::string key;
    int version = 0;
    while (f >> key) {
        if (key == "version") f >> version;
        else if (key == "theme") f >> s.theme;
        else if (key == "font") f >> s.font;
        else if (key == "volume") f >> s.volume;
        else if (key == "mixer") f >> std::quoted(s.mixer);
        else if (key == "range") f >> s.minimum >> s.maximum;
        else if (key == "raw") f >> s.rawLeft >> s.rawRight;
        else if (key == "switch") f >> s.switchLeft >> s.switchRight;
        else { std::string rest; std::getline(f, rest); }
        if (f.fail()) return defaults;
    }
    if (version != 1) return defaults;
    s.theme = std::clamp(s.theme, 0, static_cast<int>(palettes.size()) - 1); s.font = std::clamp(s.font, 0, 2);
    s.volume = std::clamp(s.volume, 0, 100);
    return s;
}
void saveState(const std::string& directory, const State& s) {
    ensureDirectory(directory);
    std::ostringstream out;
    out << "version 1\ntheme " << s.theme << "\nfont " << s.font << "\nvolume " << s.volume
        << "\nmixer " << std::quoted(s.mixer) << "\nrange " << s.minimum << ' ' << s.maximum
        << "\nraw " << s.rawLeft << ' ' << s.rawRight << "\nswitch " << s.switchLeft << ' ' << s.switchRight << '\n';
    const std::string data = out.str(), path = directory + "/state.ini", temp = path + ".tmp";
    int fd = ::open(temp.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) throw std::runtime_error("Cannot open state temporary file: " + std::string(std::strerror(errno)));
    size_t offset = 0;
    while (offset < data.size()) {
        const ssize_t n = ::write(fd, data.data() + offset, data.size() - offset);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) { ::close(fd); throw std::runtime_error("State write failed"); }
        offset += static_cast<size_t>(n);
    }
    const int syncResult = ::fsync(fd), closeResult = ::close(fd);
    if (syncResult || closeResult || ::rename(temp.c_str(), path.c_str())) throw std::runtime_error("State atomic replacement failed");
    fd = ::open(directory.c_str(), O_RDONLY | O_DIRECTORY);
    if (fd >= 0) { ::fsync(fd); ::close(fd); }
}
}
