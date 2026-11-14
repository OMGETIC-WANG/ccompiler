#pragma once

#include <algorithm>
#include <cstdint>

using SourceId = uint32_t;

inline constexpr SourceId INVALID_SOURCE_ID = 0;

struct SourceLocation {
  SourceId source_id = INVALID_SOURCE_ID;
  uint32_t begin = 0;
  uint32_t end = 0;

  bool IsValid() const {
    return source_id != INVALID_SOURCE_ID && begin <= end;
  }

  uint32_t Size() const { return begin <= end ? end - begin : 0; }

  bool operator==(const SourceLocation &) const = default;
};

// Combine a node's first-token location with its final token. A single
// SourceLocation cannot represent macro expansions spanning multiple sources,
// so those nodes retain the first token as their diagnostic anchor.
inline SourceLocation CombineLocation(SourceLocation first,
                                      SourceLocation last) {
  if (!first.IsValid()) {
    return last;
  }
  if (!last.IsValid()) {
    return first;
  }
  if (first.source_id != last.source_id) {
    return first;
  }
  return SourceLocation{.source_id = first.source_id,
                        .begin = std::min(first.begin, last.begin),
                        .end = std::max(first.end, last.end)};
}
