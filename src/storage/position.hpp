#pragma once
#include <string>

namespace epub {
struct Position { std::string identity, path, anchor; };
Position loadPosition(const std::string& directory, const std::string& identity);
void savePosition(const std::string& directory, const Position& position);
}
