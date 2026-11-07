#include "CmdLine.h"

static bool IsOption(std::string_view arg) {
  if (arg.empty())
    return false;
  if (arg[0] == '-') {
    for (size_t i = 1; i < arg.size(); i++) {
      if (i == 1) {
        if (!isalpha(arg[i]) && arg[i] != '_' && arg[i] != '-')
          return false;
      } else {
        if (!isalpha(arg[i]) && arg[i] != '_' && arg[i] != '-' &&
            !isdigit(arg[i]))
          return false;
      }
    }
    return true;
  }
  return false;
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

static const CmdLineArgDesc *TryGetArg(std::string_view name) {
  for (const auto &arg : CmdLine::CmdLineArgs()) {
    if (CompArgName(arg.name, name)) {
      return &arg;
    }
  }
  return nullptr;
}

std::vector<std::string_view> CmdLine::Parse(int argc, char **argv) {
  std::vector<std::string_view> rest;
  for (int i = 1; i < argc;) {
    std::string_view arg(argv[i]);
    if (arg == "--") {
      for (int j = i + 1; j < argc; j++) {
        rest.push_back(argv[j]);
      }
      return rest;
    }
    if (arg.starts_with("-") || arg.starts_with("--")) {
      std::string_view name_val =
          arg.starts_with("--") ? arg.substr(2) : arg.substr(1);
      std::string_view name, value;
      if (auto eq_pos = name_val.find('='); eq_pos == name_val.npos) {
        name = name_val;
        auto desc = TryGetArg(name);
        if (!desc) {
          throw CmdLineArgParseError("Unknown argument: " + std::string(name));
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
          throw CmdLineArgParseError("Unknown argument: " + std::string(name));
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
