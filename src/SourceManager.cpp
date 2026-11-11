#include "SourceManager.h"

#include <fstream>

static std::string ReadFileContent(std::string_view path) {
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

static std::string RemoveLineContinuations(std::string_view src) {
  std::string result;
  result.reserve(src.size());

  for (size_t i = 0; i < src.size(); ++i) {
    if (src[i] == '\\') {
      if (i + 1 < src.size() && src[i + 1] == '\n') {
        i++;
        continue;
      }
      if (i + 2 < src.size() && src[i + 1] == '\r' && src[i + 2] == '\n') {
        i += 2;
        continue;
      }
    }
    result.push_back(src[i]);
  }
  return result;
}

std::string_view SourceManager::LoadSource(std::string source) {
  std::string content = RemoveLineContinuations(source);
  _strings.push_back(std::move(content));
  return _strings.back();
}

std::string_view SourceManager::LoadSourceFrom(std::string_view path) {
  std::string content = RemoveLineContinuations(ReadFileContent(path));
  // std::string content = ReadFileContent(path);
  auto [it, inserted] = _files.emplace(std::string(path), std::move(content));
  return it->second;
}

std::string_view SourceManager::AddString(std::string s) {
  _strings.push_back(std::move(s));
  return _strings.back();
}
