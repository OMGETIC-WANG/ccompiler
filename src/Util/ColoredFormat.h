#pragma once

#include "EnumRefl.h"

#include <format>

constexpr static int UNDECLARED_COLOR = -1;

// clang-format off
#define COLOR_TAG_VALUES(x)\
  x(NodeString, UNDECLARED_COLOR)\
  x(NodeAddress, 0x00994c)
// clang-format on

MAKE_ENUM(ColorTag, COLOR_TAG_VALUES)
REGISTER_ENUM(ColorTag, COLOR_TAG_VALUES)

template <typename T> struct ColorWrapper {
  using type = T;

  const T &ref;
};

// non-constexpr functions, called in constexpr functions to raise error
namespace color_parse_error {
inline void invalid_color_rgb_value() {}
} // namespace color_parse_error

template <typename T> struct std::formatter<ColorWrapper<T>> {
  std::formatter<T> _underlying;
  int _color_rgb = UNDECLARED_COLOR;

  template <typename ParseContext> constexpr auto parse(ParseContext &ctx) {
    auto it = ctx.begin();

    while (it != ctx.end() && *it != ';') {
      ++it;
    }

    bool parsed = false;
    if (it != ctx.end() && *it == ';') {
      std::string_view color_tag(ctx.begin(), it);
      if (EnumRefl<ColorTag>::IsValidEnumName(color_tag)) {
        ColorTag tag = EnumRefl<ColorTag>::GetValue(color_tag);
        if ((_color_rgb > 0xffffff || _color_rgb < 0) &&
            _color_rgb != UNDECLARED_COLOR) {
          color_parse_error::invalid_color_rgb_value();
        }
        _color_rgb = (int)tag;
        parsed = true;
      }
    }

    if (parsed) {
      ctx.advance_to(std::next(it));
    }
    return _underlying.parse(ctx);
  }

  template <typename FormatContext>
  auto format(const ColorWrapper<T> &wrapper, FormatContext &ctx) const {
    if (_color_rgb != UNDECLARED_COLOR) {
      auto out = std::format_to(ctx.out(), "\x1b[38;2;{};{};{}m",
                                (_color_rgb >> 16) & 0xFFu,
                                (_color_rgb >> 8) & 0xFFu, _color_rgb & 0xFFu);
      ctx.advance_to(out);
      out = _underlying.format(wrapper.ref, ctx);
      return std::format_to(out, "\x1b[0m");
    } else {
      return std::format_to(ctx.out(), "{}", wrapper.ref);
    }
  }
};

template <>
struct std::formatter<ColorWrapper<std::pair<std::string_view, int>>>
    : std::formatter<std::string_view> {
  template <typename FormatContext>
  auto format(const ColorWrapper<std::pair<std::string_view, int>> &wrapper,
              FormatContext &ctx) const {
    int rgb = wrapper.ref.second;
    if (rgb != UNDECLARED_COLOR) {
      auto out =
          std::format_to(ctx.out(), "\x1b[38;2;{};{};{}m", (rgb >> 16) & 0xFFu,
                         (rgb >> 8) & 0xFFu, rgb & 0xFFu);
      ctx.advance_to(out);
      out = std::formatter<std::string_view>::format(wrapper.ref.first, ctx);
      return std::format_to(out, "\x1b[0m");
    } else {
      return std::format_to(ctx.out(), "{}", wrapper.ref.first);
    }
  }
};

// arg format: {:[<color_tag>;][<arg_format_options>]}
template <typename... Args>
std::string
ColoredFormat(std::format_string<ColorWrapper<std::decay_t<Args>>...> fmt,
              Args &&...args) {
  return std::format(fmt, ColorWrapper<std::decay_t<Args>>{args}...);
}
