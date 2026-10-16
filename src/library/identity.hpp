#pragma once
#include <string>

namespace epub {
std::string contentIdentity(const std::string& path);
std::string fileSignature(const std::string& path);
std::string absolutePath(const std::string& path);
std::string baseName(const std::string& path);
std::string parentPath(const std::string& path);
std::string foldAscii(std::string text);
std::string clipUtf8(std::string text, size_t bytes);
}
