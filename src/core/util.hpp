// Small shared helpers: base64, file reading, string formatting.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace wb {

std::string base64Encode(const void* data, std::size_t bytes);

// Reads a whole file. On failure returns false and fills `error` with an actionable message.
// Refuses files larger than maxBytes (checked before reading).
bool readTextFile(const std::string& path, std::size_t maxBytes, std::string& out, std::string& error);

bool writeTextFile(const std::string& path, const std::string& content, std::string& error);

std::string formatDouble(double v, int significant = 10);  // "%.{n}g"

}  // namespace wb
