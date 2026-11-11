#pragma once

#include <cstdint>
#include <deque>
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

struct SourcePosition {
  std::uint32_t line = 0;
  std::uint32_t column = 0;

  bool operator==(const SourcePosition &) const = default;
};

class SourceManager {
public:
  SourceHandle LoadSource(std::string source, std::string name = "<stdin>");
  SourceHandle LoadSourceFrom(std::string_view path);

  SourceLocation AddSynthetic(std::string text,
                              std::string name = "<macro expansion>");

  std::string_view GetText(SourceLocation location) const;
  std::string_view GetText(Token tok) const { return GetText(tok.location); }
  std::string_view GetFileName(SourceLocation location) const;
  SourcePosition GetLineColumn(SourceLocation location) const;

private:
  struct SourceEntry {
    SourceId id = INVALID_SOURCE_ID;
    std::string name;
    std::string text;
    std::vector<std::uint32_t> line_starts;
  };

  SourceHandle AddSource(std::string name, std::string text);

  const SourceEntry &GetEntry(SourceId source_id) const;

  std::deque<SourceEntry> _sources;
  std::unordered_map<std::string, SourceId> _files;
};
