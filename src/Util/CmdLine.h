#pragma once

#include <charconv>
#include <format>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

class CmdLineArgParseError : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

enum class CmdLineArgType { None, Single, Repeatable };

struct CmdLineArgDesc {
public:
  std::string name;
  CmdLineArgType type;
  void *ptr;
  int *input_count;
  void (*parser)(void *, std::string_view);
};

template <typename T> struct CmdLineArgTypeOf {};

template <> struct CmdLineArgTypeOf<bool> {
  static constexpr CmdLineArgType value = CmdLineArgType::None;
};

template <typename T>
  requires std::integral<T> || std::floating_point<T> ||
           std::is_same_v<T, std::string>
struct CmdLineArgTypeOf<T> {
  static constexpr CmdLineArgType value = CmdLineArgType::Single;
};

template <typename T> struct IsVector {
  static constexpr bool value = false;
};

template <typename T> struct IsVector<std::vector<T>> {
  static constexpr bool value = true;
};

template <typename T>
  requires IsVector<T>::value
struct CmdLineArgTypeOf<T> {
  static constexpr CmdLineArgType value = CmdLineArgType::Repeatable;
};

template <typename T> struct CmdLineArgParser {};

template <typename T>
  requires std::integral<T> || std::floating_point<T>
struct CmdLineArgParser<T> {
  static void Parse(void *ptr, std::string_view str) {
    T *pval = (T *)ptr;
    auto [ptr_end, ec] =
        std::from_chars(str.data(), str.data() + str.size(), *pval);
    if (ec != std::errc{}) {
      throw CmdLineArgParseError(std::format("Failed to parse value: {}", str));
    }
    if (ptr_end != str.data() + str.size()) {
      throw CmdLineArgParseError(
          std::format("Failed to parse rest of the value: {}", str));
    }
  }
};

template <> struct CmdLineArgParser<std::string> {
  static void Parse(void *ptr, std::string_view str) {
    std::string *pval = (std::string *)ptr;
    *pval = std::string(str);
  }
};

template <> struct CmdLineArgParser<bool> {
  static void Parse(void *ptr, std::string_view str) {
    bool *pval = (bool *)ptr;
    if (str.empty()) {
      *pval = true;
    } else {
      if (str == "0" || str == "false") {
        *pval = false;
      } else if (str == "1" || str == "true") {
        *pval = true;
      } else {
        throw CmdLineArgParseError(
            std::format("Failed to parse boolean value: {}", str));
      }
    }
  }
};

template <typename Vec>
  requires IsVector<Vec>::value
struct CmdLineArgParser<Vec> {
  static void Parse(void *ptr, std::string_view str) {
    Vec *pval = (Vec *)ptr;
    pval->emplace_back();
    CmdLineArgParser<typename Vec::value_type>::Parse(&pval->back(), str);
  }
};

class CmdLine {
public:
  static std::vector<CmdLineArgDesc> &CmdLineArgs() {
    static std::vector<CmdLineArgDesc> args;
    return args;
  }

  static std::vector<std::string_view> Parse(int argc, char **argv);
};

#define CMDLINE_ARG_IMPL_4(TYPE, NAME, FLAG_NAME, DEFAULT_VALUE)               \
  inline TYPE NAME = DEFAULT_VALUE;                                            \
  inline int __cmdline_arg_##NAME##_input_count = 0;                           \
  inline auto __cmdline_arg_##NAME = []() {                                    \
    CmdLine::CmdLineArgs().push_back({FLAG_NAME,                               \
                                      CmdLineArgTypeOf<TYPE>::value, &NAME,    \
                                      &__cmdline_arg_##NAME##_input_count,     \
                                      &CmdLineArgParser<TYPE>::Parse});        \
    return 0;                                                                  \
  }();

#define CMDLINE_ARG_IMPL_3(TYPE, NAME, DEFAULT_VALUE)                          \
  CMDLINE_ARG_IMPL_4(TYPE, NAME, #NAME, DEFAULT_VALUE)

#define GET_CMDLINE_ARG_IMPL(_1, _2, _3, _4, macro, ...) macro

#define CMDLINE_ARG(...)                                                       \
  GET_CMDLINE_ARG_IMPL(__VA_ARGS__, CMDLINE_ARG_IMPL_4,                        \
                       CMDLINE_ARG_IMPL_3)(__VA_ARGS__)
