#include "Scanner.h"
#include "../Exception.h"
#include "../Util/Diagnostic.h"
#include "../Util/StringUtil.h"
#include "Diagnostics/Errors.generated.h"
#include <algorithm>
#include <ranges>
#include <span>

struct ScannedToken {
  TokenType type = TokenType::Unknown;
  size_t length = 0;
};

static bool HasLineContinuation(std::string_view text) {
  for (size_t pos = 0; pos < text.size(); ++pos) {
    if (text[pos] != '\\') {
      continue;
    }
    if (pos + 1 < text.size() && text[pos + 1] == '\n') {
      return true;
    }
    if (pos + 2 < text.size() && text[pos + 1] == '\r' &&
        text[pos + 2] == '\n') {
      return true;
    }
  }
  return false;
}

struct LocationMap {
  SourceId source_id;
  bool identity;
  std::span<const std::uint32_t> raw_begins;
  std::span<const std::uint32_t> raw_ends;

  SourceLocation Point(size_t logical_pos) const {
    if (identity) {
      return {
          .source_id = source_id,
          .begin = static_cast<std::uint32_t>(logical_pos),
          .end = static_cast<std::uint32_t>(logical_pos + 1),
      };
    }
    ASSERT(logical_pos < raw_begins.size(),
           "Logical position is outside of the source");
    return {
        .source_id = source_id,
        .begin = raw_begins[logical_pos],
        .end = raw_ends[logical_pos],
    };
  }
};

Scanner::Scanner(SourceHandle source, DiagnosticContext &diagnostics)
    : _source(source), _diagnostics(diagnostics) {
  _has_line_continuations = HasLineContinuation(_source.text);
  if (_has_line_continuations) {
    BuildLogicalText();
  }
}

void Scanner::BuildLogicalText() {
  _logical_text.reserve(_source.text.size());
  _logical_char_raw_begins.reserve(_source.text.size());
  _logical_char_raw_ends.reserve(_source.text.size());

  size_t raw_pos = 0;
  while (raw_pos < _source.text.size()) {
    size_t old_pos = raw_pos;
    char c = GetAndAdvanceChar(_source.text, raw_pos);
    if (c == '\0' && raw_pos == _source.text.size() &&
        _source.text[old_pos] == '\\') {
      break;
    }
    _logical_text.push_back(c);
    _logical_char_raw_begins.push_back(raw_pos - 1);
    _logical_char_raw_ends.push_back(raw_pos);
  }
}

std::string_view Scanner::Text() const {
  return _has_line_continuations ? std::string_view(_logical_text)
                                 : _source.text;
}

std::uint32_t Scanner::GetRawBegin(size_t logical_pos) const {
  if (!_has_line_continuations) {
    return static_cast<std::uint32_t>(logical_pos);
  }
  if (logical_pos == _logical_char_raw_begins.size()) {
    return static_cast<std::uint32_t>(_source.text.size());
  }
  ASSERT(logical_pos < _logical_char_raw_begins.size(),
         "Logical position is outside of the source");
  return _logical_char_raw_begins[logical_pos];
}

std::uint32_t Scanner::GetRawEnd(size_t logical_end) const {
  if (!_has_line_continuations) {
    return static_cast<std::uint32_t>(logical_end);
  }
  ASSERT(logical_end > 0 && logical_end <= _logical_char_raw_ends.size(),
         "Logical position is outside of the source");
  return _logical_char_raw_ends[logical_end - 1];
}

size_t Scanner::GetLogicalEnd(std::uint32_t raw_end) const {
  if (!_has_line_continuations) {
    return raw_end;
  }
  auto found = std::lower_bound(_logical_char_raw_ends.begin(),
                                _logical_char_raw_ends.end(), raw_end);
  ASSERT(found != _logical_char_raw_ends.end() && *found == raw_end,
         "Token end does not align with a logical source character");
  return static_cast<size_t>(found - _logical_char_raw_ends.begin()) + 1;
}

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

static std::optional<std::string_view>
TryPeekAsLiteralHex(std::string_view s, DiagnosticContext &diagnostics,
                    const LocationMap &location_map, size_t token_begin,
                    bool &failed) {
  if (s.starts_with("0x") || s.starts_with("0X")) {
    size_t pos = 2;
    pos = AdvanceHexFloatNum(s, pos);
    if (pos == 2) {
      return std::nullopt;
    }
    if (pos < s.size() && (s[pos] == 'p' || s[pos] == 'P')) {
      SourceLocation exponent_location = location_map.Point(token_begin + pos);
      pos++;
      if (pos < s.size() && (s[pos] == '+' || s[pos] == '-')) {
        pos++;
      }
      size_t advanced = AdvancePureDecNum(s, pos);
      if (advanced == pos) {
        diagnostics.Error(exponent_location, errs::ExpectedDecimalExponentP{});
        failed = true;
        return std::nullopt;
      }
      pos = advanced;
    }
    return s.substr(0, pos);
  }
  return std::nullopt;
}

static std::optional<std::string_view>
TryPeekAsLiteralDec(std::string_view s, DiagnosticContext &diagnostics,
                    const LocationMap &location_map, size_t token_begin,
                    bool &failed) {
  size_t pos = AdvanceDecFloatNum(s, 0);
  if (pos == 0) {
    return std::nullopt;
  }
  if (pos < s.size() && (s[pos] == 'e' || s[pos] == 'E')) {
    SourceLocation exponent_location = location_map.Point(token_begin + pos);
    pos++;
    if (pos < s.size() && (s[pos] == '+' || s[pos] == '-')) {
      pos++;
    }
    size_t advanced = AdvancePureDecNum(s, pos);
    if (advanced == pos) {
      diagnostics.Error(exponent_location, errs::ExpectedDecimalExponentE{});
      failed = true;
      return std::nullopt;
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

static std::optional<ScannedToken>
TryPeekAsLiteralNum(std::string_view s, DiagnosticContext &diagnostics,
                    const LocationMap &location_map, size_t token_begin) {
  bool failed = false;
  if (auto hex = TryPeekAsLiteralHex(s, diagnostics, location_map, token_begin,
                                     failed)) {
    return ScannedToken{TokenType::LiteralNum,
                        TryAddPostfix(s, hex.value()).size()};
  }
  if (failed) {
    return std::nullopt;
  }
  if (auto bin = TryPeekAsLiteralBin(s)) {
    return ScannedToken{TokenType::LiteralNum,
                        TryAddPostfix(s, bin.value()).size()};
  }
  // Treat oct as dec in lexer
  if (auto dec = TryPeekAsLiteralDec(s, diagnostics, location_map, token_begin,
                                     failed)) {
    return ScannedToken{TokenType::LiteralNum,
                        TryAddPostfix(s, dec.value()).size()};
  }
  if (failed) {
    return std::nullopt;
  }
  return ScannedToken{};
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
      {"restrict", TokenType::KwRestrict},
      {"__restrict", TokenType::KwRestrict},
      {"__restrict__", TokenType::KwRestrict},
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

static std::optional<ScannedToken>
PeekAsLiteralChar(std::string_view s, DiagnosticContext &diagnostics,
                  const LocationMap &location_map, size_t token_begin) {
  ASSERT(s.starts_with('\''));

  size_t pos = 1;
  while (pos < s.size()) {
    if (s[pos] == '\n' || s[pos] == '\r') {
      diagnostics.Error(location_map.Point(token_begin),
                        errs::UnterminatedCharacterLiteral{});
      return std::nullopt;
    }
    if (s[pos] == '\\') {
      ++pos;
      if (pos == s.size() || s[pos] == '\n' || s[pos] == '\r') {
        diagnostics.Error(location_map.Point(token_begin),
                          errs::UnterminatedCharacterLiteral{});
        return std::nullopt;
      }
      ++pos;
      continue;
    }
    if (s[pos] == '\'') {
      return ScannedToken{TokenType::LiteralChar, pos + 1};
    }
    ++pos;
  }

  diagnostics.Error(location_map.Point(token_begin),
                    errs::UnterminatedCharacterLiteral{});
  return std::nullopt;
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
      return start + 2;
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
  size_t pos = GetTrimmedPos(Text(), _pos);
  _pos = pos;
  return {Text().substr(_pos), pos != old_pos};
}

std::pair<std::string_view, bool> Scanner::TrimSourceBlankH() {
  size_t old_pos = _pos;
  size_t pos = GetHTrimmedPos(Text(), _pos);
  _pos = pos;
  return {Text().substr(_pos), pos != old_pos};
}

static std::optional<ScannedToken> ScanToken(std::string_view rest,
                                             DiagnosticContext &diagnostics,
                                             const LocationMap &location_map,
                                             size_t token_begin) {
  if (rest.starts_with("##")) {
    return ScannedToken{TokenType::SharpSharp, 2};
  }

  if (rest[0] == '.') {
    if (1 < rest.size() && isdigit(rest[1])) {
      return TryPeekAsLiteralNum(rest, diagnostics, location_map, token_begin);
    }
    if (rest.starts_with("...")) {
      return ScannedToken{TokenType::DotDotDot, 3};
    } else {
      return ScannedToken{TokenType::Dot, 1};
    }
  }

  if (rest.starts_with("u8'") || rest.starts_with("L'") ||
      rest.starts_with("u'") || rest.starts_with("U'")) {
    diagnostics.Error(location_map.Point(token_begin),
                      errs::UnsupportedCharacterLiteralPrefix{});
    return std::nullopt;
  }

  if (IsIdentifierStart(rest[0])) {
    std::string_view ident = GetIdentifier(rest);
    return ResolveIdentifier(ident);
  }

  if (rest[0] == '\'') {
    return PeekAsLiteralChar(rest, diagnostics, location_map, token_begin);
  }

  if (rest[0] == '\"') {
    return PeekAsLiteralString(rest);
  }

  if (isdigit(rest[0])) {
    return TryPeekAsLiteralNum(rest, diagnostics, location_map, token_begin);
  }

  ScannedToken op_tok = TryPeekAsOperator(rest);
  if (op_tok.type != TokenType::Unknown) {
    return op_tok;
  }

  if (single_char_tokens[rest[0]] != TokenType::Unknown) {
    return ScannedToken{single_char_tokens[rest[0]], 1};
  }

  diagnostics.Error(location_map.Point(token_begin),
                    errs::UnknownCharacter{rest[0]});
  return std::nullopt;
}

Token Scanner::MakeToken(size_t begin, TokenType type, size_t length,
                         bool has_leading_space) const {
  if (type == TokenType::Unknown || length == 0) {
    return Token{type, {}, has_leading_space};
  }

  ASSERT(begin <= Text().size() && length <= Text().size() - begin,
         "Invalid token range at offset ", begin);
  return Token{
      type,
      {
          .source_id = _source.id,
          .begin = GetRawBegin(begin),
          .end = GetRawEnd(begin + length),
      },
      has_leading_space,
  };
}

std::optional<Token> Scanner::PeekNextToken() {
  auto [rest, has_leading_space] = TrimSourceBlank();
  if (rest.empty()) {
    return Token{
        TokenType::EndOfFile,
        {
            .source_id = _source.id,
            .begin = static_cast<std::uint32_t>(_source.text.size()),
            .end = static_cast<std::uint32_t>(_source.text.size()),
        },
    };
  }

  LocationMap location_map{
      .source_id = _source.id,
      .identity = !_has_line_continuations,
      .raw_begins = _logical_char_raw_begins,
      .raw_ends = _logical_char_raw_ends,
  };
  size_t token_begin = _pos;
  TRY_ASSIGN_NEW(scanned,
                 ScanToken(rest, _diagnostics, location_map, token_begin));
  return MakeToken(token_begin, scanned.type, scanned.length,
                   has_leading_space);
}

std::optional<Token> Scanner::PeekNextTokenH() {
  auto [rest, has_leading_space] = TrimSourceBlankH();
  if (rest.empty()) {
    return Token{
        TokenType::EndOfFile,
        {
            .source_id = _source.id,
            .begin = static_cast<std::uint32_t>(_source.text.size()),
            .end = static_cast<std::uint32_t>(_source.text.size()),
        },
    };
  }

  LocationMap location_map{
      .source_id = _source.id,
      .identity = !_has_line_continuations,
      .raw_begins = _logical_char_raw_begins,
      .raw_ends = _logical_char_raw_ends,
  };
  size_t token_begin = _pos;
  if (rest[0] == '\n') {
    return MakeToken(token_begin, TokenType::NextLine, 1, has_leading_space);
  }
  if (rest.starts_with("\r\n")) {
    return MakeToken(token_begin, TokenType::NextLine, 2, has_leading_space);
  }

  TRY_ASSIGN_NEW(scanned,
                 ScanToken(rest, _diagnostics, location_map, token_begin));
  return MakeToken(token_begin, scanned.type, scanned.length,
                   has_leading_space);
}

void Scanner::ConsumeToken(Token tok) {
  ASSERT(tok.location.IsValid() && tok.location.source_id == _source.id,
         "Cannot consume a token from a different source");
  const std::string_view text = Text();
  const std::uint32_t expected_begin =
      _pos < text.size() ? GetRawBegin(_pos)
                         : static_cast<std::uint32_t>(_source.text.size());
  ASSERT(tok.location.begin == expected_begin,
         "Cannot consume a token that is not at the current scanner position");
  ASSERT(tok.location.end <= _source.text.size(),
         "Token end is outside of the source buffer");
  if (_pos == text.size()) {
    ASSERT(tok.location.end == tok.location.begin,
           "EOF token must have an empty location");
    return;
  }

  size_t logical_end = GetLogicalEnd(tok.location.end);
  ASSERT(logical_end > _pos,
         "Cannot consume a token that does not advance the scanner");
  _pos = logical_end;
}
