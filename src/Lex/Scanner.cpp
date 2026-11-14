#include "Scanner.h"
#include "../Exception.h"
#include "../Util/Diagnostic.h"
#include "../Util/StringUtil.h"
#include <algorithm>
#include <ranges>

struct ScannedToken {
  TokenType type = TokenType::Unknown;
  size_t length = 0;
};

static bool IsBlank(char c) {
  return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

// skip blank and comments
static size_t GetTrimmedPos(std::string_view s, size_t start = 0) {
  size_t pos = start;
  while (pos < s.size()) {
    if (IsBlank(s[pos])) {
      pos++;
    } else if (s[pos] == '/') {
      if (pos + 1 < s.size()) {
        if (s[pos + 1] == '/') {
          pos += 2;
          while (pos < s.size() && s[pos] != '\n') {
            pos++;
          }
        } else if (s[pos + 1] == '*') {
          pos += 2;
          while (pos + 1 < s.size() && !(s[pos] == '*' && s[pos + 1] == '/')) {
            pos++;
          }
          if (pos + 1 < s.size()) {
            pos += 2;
          }
        } else {
          break;
        }
      }
    } else {
      break;
    }
  }
  return pos;
}

static bool IsHBlank(char c) { return c == ' ' || c == '\t'; }

// skip blank and comments, not including '\n'
static size_t GetHTrimmedPos(std::string_view s, size_t start = 0) {
  size_t pos = start;
  while (pos < s.size()) {
    if (IsHBlank(s[pos])) {
      pos++;
    } else if (s[pos] == '/') {
      if (pos + 1 < s.size()) {
        if (s[pos + 1] == '/') {
          pos += 2;
          while (pos < s.size() && s[pos] != '\n') {
            pos++;
          }
        } else if (s[pos + 1] == '*') {
          pos += 2;
          while (pos + 1 < s.size() && !(s[pos] == '*' && s[pos + 1] == '/')) {
            pos++;
          }
          if (pos + 1 < s.size()) {
            pos += 2;
          }
        } else {
          break;
        }
      }
    } else {
      break;
    }
  }
  return pos;
}

static std::array<TokenType, 256> BuildSingleCharTokens() {
  std::array<TokenType, 256> map;
  map.fill(TokenType::Unknown);
  map['#'] = TokenType::Sharp;
  map[';'] = TokenType::Semicolon;
  map[':'] = TokenType::Colon;
  map['('] = TokenType::LParen;
  map[')'] = TokenType::RParen;
  map['['] = TokenType::LBracket;
  map[']'] = TokenType::RBracket;
  map['{'] = TokenType::LBrace;
  map['}'] = TokenType::RBrace;
  map['*'] = TokenType::Star;
  map[','] = TokenType::Comma;
  map['='] = TokenType::Equal;
  map['&'] = TokenType::Et;
  // map['.'] = TokenType::Dot;
  // '.' may be the prefix of literal float

  map['+'] = TokenType::Add;
  map['-'] = TokenType::Sub;
  map['/'] = TokenType::Div;
  map['%'] = TokenType::Mod;

  map['|'] = TokenType::BitOr;
  map['^'] = TokenType::BitXor;
  map['~'] = TokenType::BitNot;

  map['<'] = TokenType::Lt;
  map['>'] = TokenType::Gt;

  map['!'] = TokenType::Not;

  map['?'] = TokenType::Question;

  return map;
}

static std::array<TokenType, 256> single_char_tokens = BuildSingleCharTokens();

#pragma region literal_num_detail

template <auto... valid_ranges> static bool IsInRange(char c) {
  return (... || (c >= valid_ranges.first && c <= valid_ranges.second));
}

template <auto... valid_ranges>
static size_t AdvancePureNum(std::string_view s, size_t start) {
  size_t pos = start;
  while (pos < s.size()) {
    char c = s[pos];
    if (pos > start && c == '\'') {
      pos++;
      continue;
    }
    if (IsInRange<valid_ranges...>(c)) {
      pos++;
    } else {
      break;
    }
  }
  return pos;
}

constexpr auto AdvancePureDecNum = AdvancePureNum<std::pair{'0', '9'}>;

constexpr auto AdvancePureBinNum = AdvancePureNum<std::pair{'0', '1'}>;

template <auto... valid_ranges>
static size_t AdvanceFloatNum(std::string_view s, size_t start) {
  size_t pos = AdvancePureNum<valid_ranges...>(s, start);
  if (pos < s.size() && s[pos] == '.') {
    if ((pos >= 1 && IsInRange<valid_ranges...>(s[pos - 1])) ||
        (pos + 1 < s.size() && IsInRange<valid_ranges...>(s[pos + 1]))) {
      pos++;
      pos = AdvancePureNum<valid_ranges...>(s, pos);
    }
  }
  return pos;
}

constexpr auto AdvanceHexFloatNum =
    AdvanceFloatNum<std::pair{'0', '9'}, std::pair{'a', 'f'},
                    std::pair{'A', 'F'}>;

constexpr auto AdvanceDecFloatNum = AdvanceFloatNum<std::pair{'0', '9'}>;

static SourceLocation PointLocation(SourceLocation token_location,
                                    size_t offset) {
  return {
      .source_id = token_location.source_id,
      .begin = token_location.begin + static_cast<std::uint32_t>(offset),
      .end = token_location.begin + static_cast<std::uint32_t>(offset + 1),
  };
}

static std::optional<std::string_view>
TryPeekAsLiteralHex(std::string_view s, DiagnosticContext &diagnostics,
                    SourceLocation token_location) {
  if (s.starts_with("0x") || s.starts_with("0X")) {
    size_t pos = 2;
    pos = AdvanceHexFloatNum(s, pos);
    if (pos == 2) {
      return std::nullopt;
    }
    if (pos < s.size() && (s[pos] == 'p' || s[pos] == 'P')) {
      SourceLocation exponent_location = PointLocation(token_location, pos);
      pos++;
      if (pos < s.size() && (s[pos] == '+' || s[pos] == '-')) {
        pos++;
      }
      size_t advanced = AdvancePureDecNum(s, pos);
      if (advanced == pos) {
        diagnostics.RaiseError(exponent_location,
                               "Expected decimal exponent after 'p' or 'P'");
      }
      pos = advanced;
    }
    return s.substr(0, pos);
  }
  return std::nullopt;
}

static std::optional<std::string_view>
TryPeekAsLiteralDec(std::string_view s, DiagnosticContext &diagnostics,
                    SourceLocation token_location) {
  size_t pos = AdvanceDecFloatNum(s, 0);
  if (pos == 0) {
    return std::nullopt;
  }
  if (pos < s.size() && (s[pos] == 'e' || s[pos] == 'E')) {
    SourceLocation exponent_location = PointLocation(token_location, pos);
    pos++;
    if (pos < s.size() && (s[pos] == '+' || s[pos] == '-')) {
      pos++;
    }
    size_t advanced = AdvancePureDecNum(s, pos);
    if (advanced == pos) {
      diagnostics.RaiseError(exponent_location,
                             "Expected decimal exponent after 'e' or 'E'");
    }
    pos = advanced;
  }
  return s.substr(0, pos);
}

static std::optional<std::string_view> TryPeekAsLiteralBin(std::string_view s) {
  if (s.starts_with("0b") || s.starts_with("0B")) {
    size_t pos = 2;
    pos = AdvancePureBinNum(s, pos);
    if (pos == 2) {
      return std::nullopt;
    }
    return s.substr(0, pos);
  }
  return std::nullopt;
}

static std::string_view TryAddPostfix(std::string_view s,
                                      std::string_view literal) {
  static std::string_view postfixes[] = {
      "ull", "ul", "ll", "l", "u", "f",
  };
  size_t pos = literal.size();
  std::string_view rest = s.substr(pos);
  for (const auto &postfix : postfixes) {
    if (StartsWithInsensitive(rest, postfix)) {
      return s.substr(0, pos + postfix.size());
    }
  }
  return literal;
}

#pragma endregion literal_num_detail

static ScannedToken TryPeekAsLiteralNum(std::string_view s,
                                        DiagnosticContext &diagnostics,
                                        SourceLocation token_location) {
  if (auto hex = TryPeekAsLiteralHex(s, diagnostics, token_location)) {
    return {TokenType::LiteralNum, TryAddPostfix(s, hex.value()).size()};
  }
  if (auto bin = TryPeekAsLiteralBin(s)) {
    return {TokenType::LiteralNum, TryAddPostfix(s, bin.value()).size()};
  }
  // Treat oct as dec in lexer
  if (auto dec = TryPeekAsLiteralDec(s, diagnostics, token_location)) {
    return {TokenType::LiteralNum, TryAddPostfix(s, dec.value()).size()};
  }
  return {};
}

static bool IsIdentifierStart(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

static bool IsIdentifierPart(char c) {
  return IsIdentifierStart(c) || (c >= '0' && c <= '9');
}

static std::string_view GetIdentifier(std::string_view s) {
  if (s.empty() || !IsIdentifierStart(s[0])) {
    return {};
  }
  size_t pos = 0;
  while (pos < s.size() && IsIdentifierPart(s[pos])) {
    pos++;
  }
  return s.substr(0, pos);
}

TokenType TryGetKeywordTokenType(std::string_view s) {
  static const std::pair<std::string_view, TokenType> keywords[] = {
      {"if", TokenType::KwIf},
      {"else", TokenType::KwElse},
      {"while", TokenType::KwWhile},
      {"do", TokenType::KwDo},
      {"for", TokenType::KwFor},
      {"return", TokenType::KwReturn},
      {"continue", TokenType::KwContinue},
      {"void", TokenType::KwVoid},
      {"static", TokenType::KwStatic},
      {"const", TokenType::KwConst},
      {"struct", TokenType::KwStruct},
      {"union", TokenType::KwUnion},
      {"typedef", TokenType::KwTypedef},
      {"enum", TokenType::KwEnum},
      {"switch", TokenType::KwSwitch},
      {"case", TokenType::KwCase},
      {"break", TokenType::KwBreak},
      {"default", TokenType::KwDefault},
      {"goto", TokenType::KwGoto},
      {"int", TokenType::KwInt},
      {"char", TokenType::KwChar},
      {"short", TokenType::KwShort},
      {"float", TokenType::KwFloat},
      {"double", TokenType::KwDouble},
      {"signed", TokenType::KwSigned},
      {"unsigned", TokenType::KwUnsigned},
      {"long", TokenType::KwLong},
      {"volatile", TokenType::KwVolatile},
      {"extern", TokenType::KwExtern},
      {"register", TokenType::KwRegister},
  };
  for (const auto &[keyword, type] : keywords) {
    if (s == keyword) {
      return type;
    }
  }
  return TokenType::Unknown;
}

static ScannedToken TryPeekAsKeyword(std::string_view s) {
  TokenType type = TryGetKeywordTokenType(s);
  if (type != TokenType::Unknown) {
    return {type, s.size()};
  }
  return {};
}

static ScannedToken ResolveIdentifier(std::string_view s) {
  ScannedToken kw = TryPeekAsKeyword(s);
  if (kw.type != TokenType::Unknown) {
    return kw;
  }
  return {TokenType::Identifier, s.size()};
}

static ScannedToken TryPeekAsOperator(std::string_view s) {
  static std::pair<std::string_view, TokenType> prefix_tokens[] = {
      {"==", TokenType::Eq},           {"!=", TokenType::Ne},
      {"<=", TokenType::Le},           {">=", TokenType::Ge},
      {"<<", TokenType::LShift},       {">>", TokenType::RShift},
      {"&&", TokenType::And},          {"||", TokenType::Or},
      {"++", TokenType::AddAdd},       {"--", TokenType::SubSub},
      {"...", TokenType::DotDotDot},   {"+=", TokenType::AddEqual},
      {"-=", TokenType::SubEqual},     {"*=", TokenType::MulEqual},
      {"/=", TokenType::DivEqual},     {"%=", TokenType::ModEqual},
      {"&=", TokenType::BitAndEqual},  {"|=", TokenType::BitOrEqual},
      {"^=", TokenType::BitXorEqual},  {"<<=", TokenType::LShiftEqual},
      {">>=", TokenType::RShiftEqual}, {"->", TokenType::RightArrow}};

  ScannedToken result;
  for (const auto &p : prefix_tokens) {
    if (s.starts_with(p.first) && p.first.size() > result.length) {
      result = {p.second, p.first.size()};
    }
  }
  return result;
}

static ScannedToken PeekAsLiterlChar(std::string_view s) {
  if (s.size() >= 3) {
    if (s[0] == '\'' && s[2] == '\'') {
      return {TokenType::LiteralChar, 3};
    }
  }
  return {};
}

#pragma region literal_string_detail

static bool IsSimpleEscape(char c) {
  switch (c) {
  case '0':
  case '\'':
  case '\"':
  case '?':
  case '\\':
  case 'a':
  case 'b':
  case 'f':
  case 'n':
  case 'r':
  case 't':
  case 'v':
    return true;
  default:
    return false;
  }
}

static size_t AdvanceEscapeSeq(std::string_view s, size_t start) {
  if (start < s.size()) {
    char next = s[start];
    if (IsSimpleEscape(next)) {
      return start + 1;
    } else if (next == 'x') {
      if (start + 2 < s.size()) { // x <num1> <num2>
        return start + 3;
      }
    } else if (next >= '0' && next <= '7') {
      if (start + 2 < s.size()) { // <oct0> <oct1> <oct2>
        return start + 3;
      }
    }
  }
  return (size_t)-1;
}

// Get the end of literal string, contains the quote, return -1 if quote not
// closed
static size_t GetLiteralStringEnd(std::string_view s, size_t start = 0) {
  ASSERT(s[start] == '\"');
  size_t pos = start + 1;
  while (pos < s.size()) {
    if (s[pos] == '"') {
      return pos + 1;
    }
    if (s[pos] == '\\') {
      pos = AdvanceEscapeSeq(s, pos);
      if (pos == (size_t)-1) {
        return (size_t)-1;
      }
    } else {
      pos++;
    }
  }
  return (size_t)-1;
}

#pragma endregion literal_string_detail

static ScannedToken PeekAsLiteralString(std::string_view s) {
  if (s.empty())
    return {};
  if (s[0] != '"')
    return {};
  size_t end = GetLiteralStringEnd(s);
  if (end == (size_t)-1)
    return {};
  return {TokenType::LiteralString, end};
}

std::pair<std::string_view, bool> Scanner::TrimSourceBlank() {
  size_t old_pos = _pos;
  size_t pos = GetTrimmedPos(_source.text, _pos);
  _pos = pos;
  return {_source.text.substr(_pos), pos == old_pos};
}

std::pair<std::string_view, bool> Scanner::TrimSourceBlankH() {
  size_t old_pos = _pos;
  size_t pos = GetHTrimmedPos(_source.text, _pos);
  _pos = pos;
  return {_source.text.substr(_pos), pos == old_pos};
}

static ScannedToken ScanToken(std::string_view rest,
                              DiagnosticContext &diagnostics,
                              SourceLocation token_location) {
  if (rest.starts_with("##")) {
    return {TokenType::SharpSharp, 2};
  }

  if (rest[0] == '.') {
    if (1 < rest.size() && isdigit(rest[1])) {
      return TryPeekAsLiteralNum(rest, diagnostics, token_location);
    }
    if (rest.starts_with("...")) {
      return {TokenType::DotDotDot, 3};
    } else {
      return {TokenType::Dot, 1};
    }
  }

  if (IsIdentifierStart(rest[0])) {
    std::string_view ident = GetIdentifier(rest);
    return ResolveIdentifier(ident);
  }

  if (rest[0] == '\'') {
    return PeekAsLiterlChar(rest);
  }

  if (rest[0] == '\"') {
    return PeekAsLiteralString(rest);
  }

  if (isdigit(rest[0])) {
    return TryPeekAsLiteralNum(rest, diagnostics, token_location);
  }

  ScannedToken op_tok = TryPeekAsOperator(rest);
  if (op_tok.type != TokenType::Unknown) {
    return op_tok;
  }

  if (single_char_tokens[rest[0]] != TokenType::Unknown) {
    return {single_char_tokens[rest[0]], 1};
  }

  diagnostics.RaiseError(PointLocation(token_location, 0),
                         "Unknown character: '", rest[0], "'");
}

static Token MakeToken(const SourceHandle &source, size_t begin,
                       ScannedToken scanned, bool has_leading_space) {
  if (scanned.type == TokenType::Unknown || scanned.length == 0) {
    return Token{scanned.type, {}, has_leading_space};
  }

  ASSERT(begin <= source.text.size() &&
             scanned.length <= source.text.size() - begin,
         "Invalid token range at offset ", begin);
  return Token{
      scanned.type,
      {
          .source_id = source.id,
          .begin = static_cast<std::uint32_t>(begin),
          .end = static_cast<std::uint32_t>(begin + scanned.length),
      },
      has_leading_space,
  };
}

Token Scanner::PeekNextToken() {
  auto [rest, has_leading_space] = TrimSourceBlank();
  if (rest.empty()) {
    return Token{
        TokenType::EndOfFile,
        {
            .source_id = _source.id,
            .begin = static_cast<std::uint32_t>(_pos),
            .end = static_cast<std::uint32_t>(_pos),
        },
    };
  }

  size_t token_begin = _pos;
  SourceLocation token_location{
      .source_id = _source.id,
      .begin = static_cast<std::uint32_t>(token_begin),
      .end = static_cast<std::uint32_t>(token_begin + 1),
  };
  return MakeToken(_source, token_begin,
                   ScanToken(rest, _diagnostics, token_location),
                   has_leading_space);
}

Token Scanner::PeekNextTokenH() {
  auto [rest, has_leading_space] = TrimSourceBlankH();
  if (rest.empty()) {
    return Token{
        TokenType::EndOfFile,
        {
            .source_id = _source.id,
            .begin = static_cast<std::uint32_t>(_pos),
            .end = static_cast<std::uint32_t>(_pos),
        },
    };
  }

  size_t token_begin = _pos;
  if (rest[0] == '\n') {
    return MakeToken(_source, token_begin, {TokenType::NextLine, 1},
                     has_leading_space);
  }
  if (rest.starts_with("\r\n")) {
    return MakeToken(_source, token_begin, {TokenType::NextLine, 2},
                     has_leading_space);
  }

  SourceLocation token_location{
      .source_id = _source.id,
      .begin = static_cast<std::uint32_t>(token_begin),
      .end = static_cast<std::uint32_t>(token_begin + 1),
  };
  return MakeToken(_source, token_begin,
                   ScanToken(rest, _diagnostics, token_location),
                   has_leading_space);
}

void Scanner::ConsumeToken(Token tok) {
  ASSERT(tok.location.IsValid() && tok.location.source_id == _source.id,
         "Cannot consume a token from a different source");
  ASSERT(tok.location.begin == _pos,
         "Cannot consume a token that is not at the current scanner position");
  ASSERT(tok.location.end <= _source.text.size(),
         "Token end is outside of the source buffer");
  _pos = tok.location.end;
}
