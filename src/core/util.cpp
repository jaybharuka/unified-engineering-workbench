#include "util.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>

namespace wb {

std::string base64Encode(const void* data, std::size_t bytes) {
  static const char* tbl = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  const unsigned char* p = static_cast<const unsigned char*>(data);
  std::string out;
  out.reserve((bytes + 2) / 3 * 4);
  std::size_t i = 0;
  for (; i + 2 < bytes; i += 3) {
    unsigned v = (p[i] << 16) | (p[i + 1] << 8) | p[i + 2];
    out.push_back(tbl[(v >> 18) & 63]);
    out.push_back(tbl[(v >> 12) & 63]);
    out.push_back(tbl[(v >> 6) & 63]);
    out.push_back(tbl[v & 63]);
  }
  if (i + 1 == bytes) {
    unsigned v = p[i] << 16;
    out.push_back(tbl[(v >> 18) & 63]);
    out.push_back(tbl[(v >> 12) & 63]);
    out += "==";
  } else if (i + 2 == bytes) {
    unsigned v = (p[i] << 16) | (p[i + 1] << 8);
    out.push_back(tbl[(v >> 18) & 63]);
    out.push_back(tbl[(v >> 12) & 63]);
    out.push_back(tbl[(v >> 6) & 63]);
    out.push_back('=');
  }
  return out;
}

bool readTextFile(const std::string& path, std::size_t maxBytes, std::string& out, std::string& error) {
  std::ifstream f(path, std::ios::binary | std::ios::ate);
  if (!f) {
    error = "Cannot open file '" + path + "'. Check that it exists and is not locked by another program.";
    return false;
  }
  std::streamoff size = f.tellg();
  if (size < 0) {
    error = "Cannot determine size of '" + path + "'.";
    return false;
  }
  if (static_cast<unsigned long long>(size) > maxBytes) {
    error = "File '" + path + "' is " + std::to_string(size / (1024 * 1024)) + " MB, above the " +
            std::to_string(maxBytes / (1024 * 1024)) + " MB limit. Use a smaller file.";
    return false;
  }
  f.seekg(0);
  out.assign(static_cast<std::size_t>(size), '\0');
  if (size > 0 && !f.read(&out[0], size)) {
    error = "Read error while loading '" + path + "'.";
    return false;
  }
  return true;
}

bool writeTextFile(const std::string& path, const std::string& content, std::string& error) {
  {  // create the parent folder if needed (e.g. a fresh "results" directory)
    std::error_code ec;
    std::filesystem::path parent = std::filesystem::path(path).parent_path();
    if (!parent.empty()) std::filesystem::create_directories(parent, ec);
  }
  std::ofstream f(path, std::ios::binary | std::ios::trunc);
  if (!f) {
    error = "Cannot write '" + path + "'. Check that the folder exists and is writable.";
    return false;
  }
  f.write(content.data(), static_cast<std::streamsize>(content.size()));
  if (!f) {
    error = "Write error for '" + path + "'.";
    return false;
  }
  return true;
}

std::string formatDouble(double v, int significant) {
  char buf[64];
  std::snprintf(buf, sizeof buf, "%.*g", significant, v);
  return buf;
}

}  // namespace wb
