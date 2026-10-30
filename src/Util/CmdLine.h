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

enum class CmdLineArgType { None, Single, Multiple };

struct CmdLineArgDesc {
public:
  std::string name;
  CmdLineArgType type;
  void *ptr;
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
  static constexpr CmdLineArgType value = CmdLineArgType::Multiple;
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
      throw CmdLineArgParseError("Failed to parse value: " + std::string(str));
    }
    if (ptr_end != str.data() + str.size()) {
      throw CmdLineArgParseError("Failed to parse rest of the value: " +
                                 std::string(str));
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
        throw CmdLineArgParseError("Failed to parse boolean value: " +
                                   std::string(str));
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

  static std::vector<std::string_view> Parse(int argc, char **argv) {
    std::vector<std::string_view> rest;
    for (int i = 1; i < argc;) {
      std::string_view arg(argv[i]);
      if (arg == "--") {
        for (int j = i + 1; j < argc; j++) {
          rest.push_back(argv[j]);
        }
        return rest;
      }
      if (arg.starts_with("-")) {
        std::string_view name_val = arg.substr(1);
        std::string_view name, value;
        if (auto eq_pos = name_val.find('='); eq_pos == name_val.npos) {
          name = name_val;
          auto desc = TryGetArg(name);
          if (!desc) {
            throw CmdLineArgParseError("Unknown argument: " +
                                       std::string(name));
          }

          if (desc->type == CmdLineArgType::None) {
            desc->parser(desc->ptr, {});
            i++;
          } else if (desc->type == CmdLineArgType::Single) {
            if (i + 1 < argc) {
              desc->parser(desc->ptr, argv[i + 1]);
              i += 2;
            } else {
              throw CmdLineArgParseError("Missing value for argument: " +
                                         std::string(name));
            }
          } else {
            int j = i + 1;
            while (j < argc) {
              std::string_view value(argv[j]);
              if (value == "--" || IsOption(value)) {
                break;
              }
              desc->parser(desc->ptr, value);
              j++;
            }
            i = j;
          }
        } else {
          name = name_val.substr(0, eq_pos);
          if (name.empty()) {
            throw CmdLineArgParseError("Empty argument name: " +
                                       std::string(name_val));
          }
          value = name_val.substr(eq_pos + 1);
          auto desc = TryGetArg(name);
          if (!desc) {
            throw CmdLineArgParseError("Unknown argument: " +
                                       std::string(name));
          }
          desc->parser(desc->ptr, value);
          i++;
        }
      } else {
        rest.push_back(arg);
        i++;
      }
    }
    return rest;
  }

private:
  static bool IsOption(std::string_view arg) {
    if (arg.empty())
      return false;
    if (arg[0] == '-') {
      for (size_t i = 1; i < arg.size(); i++) {
        if (!isalpha(arg[i]) && arg[i] != '_' && arg[i] != '-')
          return false;
      }
      return true;
    }
    return false;
  }

  static const CmdLineArgDesc *TryGetArg(std::string_view name) {
    for (const auto &arg : CmdLineArgs()) {
      if (CompArgName(arg.name, name)) {
        return &arg;
      }
    }
    return nullptr;
  }

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
};

#define CMDLINE_ARG_IMPL_4(type, name, flag_name, def_val)                     \
  inline type name = def_val;                                                  \
  inline auto __cmdline_arg_##name = []() {                                    \
    CmdLine::CmdLineArgs().push_back({flag_name,                               \
                                      CmdLineArgTypeOf<type>::value, &name,    \
                                      &CmdLineArgParser<type>::Parse});        \
    return 0;                                                                  \
  }();

#define CMDLINE_ARG_IMPL_3(type, name, def_val)                                \
  CMDLINE_ARG_IMPL_4(type, name, #name, def_val)

#define GET_CMDLINE_ARG_IMPL(_1, _2, _3, _4, macro, ...) macro

#define CMDLINE_ARG(...)                                                       \
  GET_CMDLINE_ARG_IMPL(__VA_ARGS__, CMDLINE_ARG_IMPL_4,                        \
                       CMDLINE_ARG_IMPL_3)(__VA_ARGS__)
