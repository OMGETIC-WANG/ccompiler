#include "SourceManager.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <string_view>

TEST(SourceManager, SkipsCrLfLineContinuation) {
  std::string_view text = "A\\\r\nB";
  size_t pos = 0;

  EXPECT_EQ(GetAndAdvanceChar(text, pos), 'A');
  EXPECT_EQ(GetAndAdvanceChar(text, pos), 'B');
  EXPECT_EQ(pos, text.size());
}

TEST(SourceManager, SkipsConsecutiveLineContinuations) {
  std::string_view text = "A\\\n\\\nB";
  size_t pos = 0;

  EXPECT_EQ(GetAndAdvanceChar(text, pos), 'A');
  EXPECT_EQ(GetAndAdvanceChar(text, pos), 'B');
  EXPECT_EQ(pos, text.size());
}

TEST(SourceManager, PreservesNonContinuationBackslash) {
  std::string_view text = "A\\B";
  size_t pos = 0;

  EXPECT_EQ(GetAndAdvanceChar(text, pos), 'A');
  EXPECT_EQ(GetAndAdvanceChar(text, pos), '\\');
  EXPECT_EQ(GetAndAdvanceChar(text, pos), 'B');
}

TEST(SourceManager, HandlesTrailingLineContinuation) {
  std::string_view text = "A\\\n";
  size_t pos = 0;

  EXPECT_EQ(GetAndAdvanceChar(text, pos), 'A');
  EXPECT_EQ(GetAndAdvanceChar(text, pos), '\0');
  EXPECT_EQ(pos, text.size());
}

TEST(SourceManager, PreservesRawSource) {
  SourceManager sources;
  const std::string raw = "a\\\nb\r\nc";
  SourceHandle source = sources.LoadSource(raw, "<line-continuations>");

  EXPECT_EQ(source.text, raw);
}

TEST(SourceManager, NormalizesAndCachesText) {
  SourceManager sources;
  const std::string raw = "a\\\nb\r\nc";
  SourceHandle source = sources.LoadSource(raw, "<line-continuations>");
  SourceLocation whole_location{
      .source_id = source.id,
      .begin = 0,
      .end = static_cast<std::uint32_t>(raw.size()),
  };

  const std::string_view whole = sources.GetText(whole_location);
  EXPECT_EQ(whole, "ab\r\nc");
  EXPECT_EQ(sources.GetText(whole_location).data(), whole.data());
}

TEST(SourceManager, UsesRawLineBoundaries) {
  SourceManager sources;
  const std::string raw = "a\\\nb\r\nc";
  SourceHandle source = sources.LoadSource(raw, "<line-continuations>");
  SourcePosition position = sources.GetLineColumn({
      .source_id = source.id,
      .begin = 3,
      .end = 4,
  });

  EXPECT_EQ(position.line, 2);
  EXPECT_EQ(position.column, 1);
}

TEST(SourceManager, ReturnsDirectSourceViews) {
  SourceManager sources;
  SourceHandle direct = sources.LoadSource("direct", "<direct>");
  SourceLocation direct_location{
      .source_id = direct.id,
      .begin = 1,
      .end = 4,
  };

  const std::string_view direct_text = sources.GetText(direct_location);
  EXPECT_EQ(direct_text, "ire");
  EXPECT_EQ(direct_text.data(), direct.text.data() + 1);
}
