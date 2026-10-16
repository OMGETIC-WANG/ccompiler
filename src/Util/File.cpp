#include "File.h"

#include <fstream>

std::string ReadFileContent(std::string_view path) {
  std::ifstream file(std::string(path), std::ios::binary | std::ios::ate);
  if (!file.is_open()) {
    throw std::runtime_error("Failed to open file: " + std::string(path));
  }

  std::streamsize size = file.tellg();
  file.seekg(0, std::ios::beg);

  std::string content;
  if (size > 0) {
    content.resize(static_cast<std::size_t>(size));
    if (!file.read(content.data(), size)) {
      throw std::runtime_error("Failed to read file: " + std::string(path));
    }
  }

  return content;
}
