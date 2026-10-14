#include "storage/position.hpp"
#include "storage/state.hpp"
#include <algorithm>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace epub {
namespace {
bool validIdentity(const std::string& id) {
    return id.size() == 32 && std::all_of(id.begin(), id.end(), [](char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
    });
}
}
Position loadPosition(const std::string& directory, const std::string& identity) {
    if (!validIdentity(identity)) return {};
    std::ifstream in(directory + "/books/" + identity + ".position");
    Position p;
    int version = 0;
    if (!(in >> version >> std::quoted(p.identity) >> std::quoted(p.path) >> std::quoted(p.anchor)) ||
        version != 1 || p.identity != identity || p.anchor.size() > 16384) return {};
    return p;
}
void savePosition(const std::string& directory, const Position& p) {
    if (!validIdentity(p.identity) || p.anchor.empty() || p.anchor.size() > 16384)
        throw std::runtime_error("Invalid reading position");
    std::ostringstream out;
    out << "1\n" << std::quoted(p.identity) << '\n' << std::quoted(p.path) << '\n' << std::quoted(p.anchor) << '\n';
    atomicWrite(directory + "/books", p.identity + ".position", out.str());
}
}
