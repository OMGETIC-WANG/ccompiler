#pragma once

#include <string>
#include <string_view>
#include <variant>

class CowString {
public:
  CowString(std::string_view view = {}) : _data(view) {}

  CowString(std::string str) : _data(std::move(str)) {}

  std::string_view view() const {
    return std::visit([](const auto &s) { return std::string_view(s); }, _data);
  }

  operator std::string_view() const { return view(); }

  std::string &string() {
    if (std::holds_alternative<std::string>(_data)) {
      return std::get<std::string>(_data);
    } else {
      _data = std::string(std::get<std::string_view>(_data));
      return std::get<std::string>(_data);
    }
  }

  void into_own() {
    if (std::holds_alternative<std::string_view>(_data)) {
      _data = std::string(std::get<std::string_view>(_data));
    }
  }

  bool own() const { return std::holds_alternative<std::string>(_data); }

private:
  std::variant<std::string_view, std::string> _data;
};
