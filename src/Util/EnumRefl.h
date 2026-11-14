#pragma once

#include <format>
#include <span>
#include <stdexcept>
#include <string_view>

class EnumReflException : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

template <typename E> class EnumRefl final {
public:
  constexpr static bool registered = false;
};

template <typename E>
concept EnumReflRegistered = EnumRefl<E>::registered;

// MAKE_ENUM
#define __EXTRACT_ENUM_VALUES_1(item) item,
#define __EXTRACT_ENUM_VALUES_2(item, value) item = value,

#define __GET_EXTRACT_ENUM_VALUES(_1, _2, macro, ...) macro
#define __EXTRACT_ENUM_VALUES(...)                                             \
  __GET_EXTRACT_ENUM_VALUES(__VA_ARGS__, __EXTRACT_ENUM_VALUES_2,              \
                            __EXTRACT_ENUM_VALUES_1)(__VA_ARGS__)

#define MAKE_ENUM(enum_name, values)                                           \
  enum class enum_name { values(__EXTRACT_ENUM_VALUES) };

// MAKE_ENUM_COUNT
#define __EXTRACT_ENUM_AS_ADD_ONE_1(item) +1
#define __EXTRACT_ENUM_AS_ADD_ONE_2(item, value) +1

#define __GET_EXTRACT_ENUM_AS_ADD_ONE(_1, _2, macro, ...) macro
#define __EXTRACT_ENUM_AS_ADD_ONE(...)                                         \
  __GET_EXTRACT_ENUM_AS_ADD_ONE(__VA_ARGS__, __EXTRACT_ENUM_AS_ADD_ONE_2,      \
                                __EXTRACT_ENUM_AS_ADD_ONE_1)(__VA_ARGS__)

#define MAKE_ENUM_COUNT(values) (values(__EXTRACT_ENUM_AS_ADD_ONE))

// REGISTER_ENUM
#define __EXTRACT_ENUM_NAME_VALUE_1(item) {_EnumAlias::item, #item},
#define __EXTRACT_ENUM_NAME_VALUE_2(item, value) {_EnumAlias::item, #item},

#define __GET_EXTRACT_NUM_NAME_VALUE(_1, _2, macro, ...) macro
#define __EXTRACT_ENUM_NAME_VALUE(...)                                         \
  __GET_EXTRACT_NUM_NAME_VALUE(__VA_ARGS__, __EXTRACT_ENUM_NAME_VALUE_2,       \
                               __EXTRACT_ENUM_NAME_VALUE_1)(__VA_ARGS__)

#define REGISTER_ENUM(enum_name, values)                                       \
  template <> class EnumRefl<enum_name> final {                                \
    using _EnumAlias = enum_name;                                              \
    static constexpr std::pair<enum_name, std::string_view> _values[] = {      \
        values(__EXTRACT_ENUM_NAME_VALUE)};                                    \
                                                                               \
  public:                                                                      \
    constexpr static bool registered = true;                                   \
    static constexpr std::string_view GetName(enum_name _Val) {                \
      for (const auto &[_Enum_val, _Enum_str] : GetValues()) {                 \
        if (_Val == _Enum_val) {                                               \
          return _Enum_str;                                                    \
        }                                                                      \
      }                                                                        \
      throw EnumReflException("Value not found");                              \
      return {};                                                               \
    }                                                                          \
    static constexpr enum_name GetValue(std::string_view _Name) {              \
      for (const auto &[_Enum_val, _Enum_str] : GetValues()) {                 \
        if (_Name == _Enum_str) {                                              \
          return _Enum_val;                                                    \
        }                                                                      \
      }                                                                        \
      throw EnumReflException("Name not found");                               \
      return {};                                                               \
    }                                                                          \
    static constexpr std::span<const std::pair<enum_name, std::string_view>>   \
    GetValues() {                                                              \
      return _values;                                                          \
    }                                                                          \
    static constexpr size_t EnumCount() { return GetValues().size(); }         \
    static constexpr bool IsValidEnumName(std::string_view _Name) {            \
      for (const auto &[_Enum_val, _Enum_str] : GetValues()) {                 \
        if (_Name == _Enum_str) {                                              \
          return true;                                                         \
        }                                                                      \
      }                                                                        \
      return false;                                                            \
    }                                                                          \
  };                                                                           \
  template <> struct std::formatter<enum_name> : std::formatter<char> {        \
    static auto format(enum_name e, std::format_context &ctx) {                \
      return std::format_to(ctx.out(), "{}", EnumRefl<enum_name>::GetName(e)); \
    }                                                                          \
  };
