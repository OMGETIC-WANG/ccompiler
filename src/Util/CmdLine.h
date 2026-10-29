#pragma once

#include <charconv>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

class CmdLineArgParseError : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

struct CmdLineArgDesc {
public:
  std::string name;
  void *ptr;
  bool *declared;
  void (*parser)(void *, bool *, std::string_view);
};

template <typename T> struct CmdLineArgParser {};

template <typename T>
  requires std::integral<T> || std::floating_point<T>
struct CmdLineArgParser<T> {
public:
  static void Parse(void *ptr, bool *declared, std::string_view str) {
    T *pval = (T *)ptr;
    auto [ptr_end, ec] =
        std::from_chars(str.data(), str.data() + str.size(), *pval);
    if (ec != std::errc{}) {
      throw CmdLineArgParseError("Failed to parse value: " + std::string(str));
    }
    if (ptr_end != str.data() + str.size()) {
      throw CmdLineArgParseError("Failed to parse rest of the value: " +
                                 std::string(str));
    }
    *declared = true;
  }
};

template <> struct CmdLineArgParser<std::string> {
public:
  static void Parse(void *ptr, bool *declared, std::string_view str) {
    std::string *pval = (std::string *)ptr;
    *pval = std::string(str);
    *declared = true;
  }
};

template <> struct CmdLineArgParser<bool> {
public:
  static void Parse(void *ptr, bool *declared, std::string_view str) {
    bool *pval = (bool *)ptr;
    if (str == "true" || str == "1") {
      *pval = true;
    } else if (str == "false" || str == "0") {
      *pval = false;
    } else {
      *pval = true;
    }
    *declared = true;
  }
};

class CmdLine {
public:
  static std::vector<CmdLineArgDesc> &CmdLineArgs() {
    static std::vector<CmdLineArgDesc> args;
    return args;
  }

  static std::vector<std::string_view> Parse(int argc, char **argv) {
    std::vector<std::string_view> rest;
    for (int i = 1; i < argc; i++) {
      std::string_view arg(argv[i]);
      if (arg.starts_with("-")) {
        std::string_view name_val = arg.substr(1);
        std::string_view name, value;
        if (auto eq_pos = name_val.find('='); eq_pos == name_val.npos) {
          name = name_val;
          if (i + 1 < argc) {
            value = std::string_view(argv[i + 1]);
            i++;
          } else {
            value = {};
          }
        } else {
          name = name_val.substr(0, eq_pos);
          if (name.empty()) {
            throw CmdLineArgParseError("Empty argument name: " +
                                       std::string(name_val));
          }
          value = name_val.substr(eq_pos + 1);
        }
        HandleArg(name, value);
      } else {
        rest.push_back(arg);
      }
    }
    return rest;
  }

private:
  static bool CompArgName(std::string_view l, std::string_view r) {
    if (l.size() != r.size())
      return false;
    for (size_t i = 0; i < l.size(); i++) {
      if (l[i] != r[i]) {
        if ((l[i] == '_' || l[i] == '-') && (r[i] == '_' || r[i] == '-')) {
          continue;
        }
        return false;
      }
    }
    return true;
  }

  static void HandleArg(std::string_view name, std::string_view value) {
    for (const auto &arg : CmdLineArgs()) {
      if (CompArgName(arg.name, name)) {
        arg.parser(arg.ptr, arg.declared, value);
        return;
      }
    }
    throw CmdLineArgParseError("Unknown argument: " + std::string(name));
  }
};

#define __CMDLINE_ARG_IMPL_4(type, name, flag_name, def_val)                   \
  type name = def_val;                                                         \
  bool name##_declared = false;                                                \
  auto __cmdline_arg_##name = []() {                                           \
    CmdLine::CmdLineArgs().push_back(                                          \
        {flag_name, &name, &name##_declared, &CmdLineArgParser<type>::Parse}); \
    return 0;                                                                  \
  }();

#define __CMDLINE_ARG_IMPL_3(type, name, def_val)                              \
  __CMDLINE_ARG_IMPL_4(type, name, #name, def_val)

#define __GET_CMDLINE_ARG_IMPL(_1, _2, _3, _4, macro, ...) macro

#define CMDLINE_ARG(...)                                                       \
  __GET_CMDLINE_ARG_IMPL(__VA_ARGS__, __CMDLINE_ARG_IMPL_4,                    \
                         __CMDLINE_ARG_IMPL_3)(__VA_ARGS__)
