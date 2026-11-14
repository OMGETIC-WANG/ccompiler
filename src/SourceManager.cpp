#include "SourceManager.h"
#include "Exception.h"
#include "Util/Diagnostic.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <limits>

static std::string MakeAbsolutePath(std::string_view path) {
  if (path == "<stdin>") {
    return "<stdin>";
  }
  return std::filesystem::absolute(std::filesystem::path(path))
      .lexically_normal()
      .string();
}

static std::string ReadFileContent(std::string_view path) {
  std::ifstream file(std::string(path), std::ios::binary | std::ios::ate);
  if (!file.is_open()) {
    throw DriverError("Failed to open file: " + std::string(path));
  }

  std::streamsize size = file.tellg();
  file.seekg(0, std::ios::beg);

  std::string content;
  if (size > 0) {
    content.resize(static_cast<std::size_t>(size));
    if (!file.read(content.data(), size)) {
      throw DriverError("Failed to read file: " + std::string(path));
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

SourceHandle SourceManager::AddSource(std::string name, std::string text) {
  ASSERT(text.size() <= std::numeric_limits<std::uint32_t>::max(),
         "Source file is too large for SourceLocation");
  ASSERT(_sources.size() < std::numeric_limits<SourceId>::max(),
         "Too many source files");

  SourceId source_id = static_cast<SourceId>(_sources.size() + 1);

  SourceEntry entry{
      .id = source_id,
      .name = MakeAbsolutePath(name),
      .text = std::move(text),
      .line_starts = {0},
  };
  for (std::size_t i = 0; i < entry.text.size(); ++i) {
    if (entry.text[i] == '\n') {
      entry.line_starts.push_back(static_cast<std::uint32_t>(i + 1));
    }
  }

  _sources.push_back(std::move(entry));
  return {.id = source_id, .text = _sources.back().text};
}

SourceHandle SourceManager::LoadSource(std::string source, std::string name) {
  return AddSource(std::move(name), RemoveLineContinuations(source));
}

SourceHandle SourceManager::LoadSourceFrom(std::string_view path) {
  std::string absolute_path = MakeAbsolutePath(path);
  auto found = _files.find(absolute_path);
  if (found != _files.end()) {
    const SourceEntry &entry = GetEntry(found->second);
    return {.id = entry.id, .text = entry.text};
  }

  SourceHandle source = AddSource(
      absolute_path, RemoveLineContinuations(ReadFileContent(absolute_path)));
  _files.emplace(GetEntry(source.id).name, source.id);
  return source;
}

SourceHandle SourceManager::LoadSourceFrom(std::string_view curr_file,
                                           std::string_view relative_path) {
  std::filesystem::path include_path =
      std::filesystem::path(curr_file).parent_path() /
      std::filesystem::path(relative_path);
  return LoadSourceFrom(include_path.string());
}

SourceLocation SourceManager::AddSynthetic(std::string text, std::string name) {
  SourceHandle source = AddSource(std::move(name), std::move(text));
  return {
      .source_id = source.id,
      .begin = 0,
      .end = static_cast<std::uint32_t>(source.text.size()),
  };
}

const SourceManager::SourceEntry &
SourceManager::GetEntry(SourceId source_id) const {
  ASSERT(source_id != INVALID_SOURCE_ID && source_id <= _sources.size(),
         "Invalid source id: ", source_id);
  return _sources[source_id - 1];
}

std::string_view SourceManager::GetText(SourceLocation location) const {
  if (!location.IsValid()) {
    return {};
  }

  const SourceEntry &entry = GetEntry(location.source_id);
  ASSERT(location.end <= entry.text.size(),
         "Source location is outside of its source: [", location.begin, ", ",
         location.end, ")");
  return std::string_view(entry.text)
      .substr(location.begin,
              static_cast<std::size_t>(location.end) - location.begin);
}

std::string_view SourceManager::GetFileName(SourceLocation location) const {
  if (!location.IsValid()) {
    return {};
  }
  return GetEntry(location.source_id).name;
}

SourcePosition SourceManager::GetLineColumn(SourceLocation location) const {
  if (!location.IsValid()) {
    return {};
  }

  const SourceEntry &entry = GetEntry(location.source_id);
  ASSERT(location.begin <= entry.text.size(),
         "Source location is outside of its source: offset ", location.begin);

  auto line_it = std::upper_bound(entry.line_starts.begin(),
                                  entry.line_starts.end(), location.begin);
  ASSERT(line_it != entry.line_starts.begin(),
         "Source line table must include offset 0");
  --line_it;

  return {
      .line =
          static_cast<std::uint32_t>(line_it - entry.line_starts.begin() + 1),
      .column = static_cast<std::uint32_t>(location.begin - *line_it + 1),
  };
}

std::string SourceManager::FormatLocation(SourceLocation location) const {
  if (!location.IsValid()) {
    return "<invalid location>";
  }

  std::string_view file_name = GetFileName(location);
  SourcePosition pos = GetLineColumn(location);
  return std::string(file_name) + ":" + std::to_string(pos.line) + ":" +
         std::to_string(pos.column);
}
