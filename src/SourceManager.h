#pragma once

#include <cstdint>
#include <deque>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "Lex/Token.h"

// A borrowed, stable view of a source buffer owned by SourceManager.
struct SourceHandle {
  SourceId id = INVALID_SOURCE_ID;
  std::string_view text;
};

// Return the next logical source character, skipping line splices before it.
char GetAndAdvanceChar(std::string_view str, size_t &pos);

struct SourcePosition {
  std::uint32_t line = 0;
  std::uint32_t column = 0;

  bool operator==(const SourcePosition &) const = default;
};

class SourceManager {
public:
  SourceHandle LoadSource(std::string source, std::string name = "<stdin>");
  SourceHandle LoadSourceFrom(std::string_view path);
  SourceHandle LoadSourceFrom(std::string_view curr_file,
                              std::string_view relative_path);

  SourceLocation AddSynthetic(std::string text,
                              std::string name = "<macro expansion>");

  std::string_view GetText(SourceLocation location) const;
  std::string_view GetText(Token tok) const { return GetText(tok.location); }
  std::string_view GetFileName(SourceLocation location) const;
  SourcePosition GetLineColumn(SourceLocation location) const;

  std::string FormatLocation(SourceLocation location) const;

  void AddIncludePath(std::filesystem::path path);

private:
  struct SourceEntry {
    struct CachedText {
      std::uint32_t begin;
      std::uint32_t end;
      std::string text;
    };

    SourceId id = INVALID_SOURCE_ID;
    std::string name;
    std::string text;
    std::vector<std::uint32_t> line_starts;
    mutable std::deque<CachedText> cached_texts;
  };

  SourceHandle AddSource(std::string name, std::string text);

  std::optional<std::filesystem::path>
  FindSourceFile(std::string_view curr_file,
                 std::string_view relative_path) const;

  const SourceEntry &GetEntry(SourceId source_id) const;

  std::deque<SourceEntry> _sources;
  std::unordered_map<std::string, SourceId> _files;

  std::vector<std::filesystem::path> _include_paths;
};
