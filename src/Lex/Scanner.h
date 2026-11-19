#pragma once

#include "../SourceManager.h"
#include "../Util/EnumRefl.h"

#include <cassert>
#include <string>
#include <string_view>
#include <vector>

TokenType TryGetKeywordTokenType(std::string_view s);

class DiagnosticContext;

// clang-format off
#define SCANNER_TYPE_VALUES(x)\
  x(Scanner)\
  x(MacroScanner)
// clang-format on

MAKE_ENUM(ScannerType, SCANNER_TYPE_VALUES)
REGISTER_ENUM(ScannerType, SCANNER_TYPE_VALUES)

#define IScannerImpl(type)                                                     \
public:                                                                        \
  ScannerType GetType() const override { return ScannerType::type; }           \
  constexpr static ScannerType __type = ScannerType::type

struct IScanner {
  virtual ~IScanner() = default;

  virtual ScannerType GetType() const = 0;

  virtual Token PeekNextToken() = 0;

  virtual void ConsumeToken(Token token) = 0;

  template <std::derived_from<IScanner> T> bool Is() const {
    return GetType() == T::__type;
  }
};

// Pure token scanner, no preprocessor handling
class Scanner : public IScanner {
  IScannerImpl(Scanner);

public:
  explicit Scanner(SourceHandle source, DiagnosticContext &diagnostics);

  Token PeekNextToken() override;

  // Peek next token in the line, return TokenType::NextLine if reach the end of
  // line
  Token PeekNextTokenH();

  void AdvancePos(size_t count) { _pos += count; }
  void AdvancePosTo(size_t pos) { _pos = pos; }

  void ConsumeToken(Token tok) override;

private:
  void BuildLogicalText();

  std::string_view Text() const;
  std::uint32_t GetRawBegin(size_t logical_pos) const;
  std::uint32_t GetRawEnd(size_t logical_end) const;
  size_t GetLogicalEnd(std::uint32_t raw_end) const;

  std::pair<std::string_view, bool> TrimSourceBlank();

  std::pair<std::string_view, bool> TrimSourceBlankH();

  Token MakeToken(size_t begin, TokenType type, size_t length,
                  bool has_leading_space) const;

  SourceHandle _source;
  DiagnosticContext &_diagnostics;
  std::string _logical_text;
  std::vector<std::uint32_t> _logical_char_raw_begins;
  std::vector<std::uint32_t> _logical_char_raw_ends;
  bool _has_line_continuations = false;
  size_t _pos = 0;
};
