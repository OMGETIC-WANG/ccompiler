#pragma once

#include <format>
#include <string>
#include <string_view>
#include <variant>

class StringVariant {
public:
  constexpr StringVariant() noexcept : data_(std::string_view{}) {}

  constexpr StringVariant(std::string_view sv) noexcept : data_(sv) {}

  constexpr StringVariant(const char *s) noexcept
      : data_(std::string_view(s)) {}

  StringVariant(std::string &&s) noexcept : data_(std::move(s)) {}

  StringVariant(const std::string &s) : data_(s) {}

  operator std::string_view() const noexcept {
    return std::visit(
        [](const auto &s) noexcept -> std::string_view { return s; }, data_);
  }

  [[nodiscard]] std::string_view view() const noexcept {
    return static_cast<std::string_view>(*this);
  }

  [[nodiscard]] const char *data() const noexcept { return view().data(); }

  [[nodiscard]] size_t size() const noexcept { return view().size(); }

  [[nodiscard]] bool empty() const noexcept { return view().empty(); }

  [[nodiscard]] bool owns_data() const noexcept {
    return std::holds_alternative<std::string>(data_);
  }

  [[nodiscard]] const std::variant<std::string_view, std::string> &
  raw() const noexcept {
    return data_;
  }

  bool operator==(const StringVariant &other) const noexcept {
    return view() == other.view();
  }
  bool operator==(std::string_view other) const noexcept {
    return view() == other;
  }

private:
  std::variant<std::string_view, std::string> data_;
};

inline std::ostream &operator<<(std::ostream &os, const StringVariant &sv) {
  return os << sv.view();
}

template <>
struct std::formatter<StringVariant> : std::formatter<std::string_view> {
  static auto format(const StringVariant &sv, std::format_context &ctx) {
    return std::format_to(ctx.out(), "{}", sv.view());
  }
};
