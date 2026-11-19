#include "CmdLine.h"

#include <format>

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

static const CmdLineArgDesc *TryGetPrefixArg(std::string_view name,
                                             std::string_view &value) {
  const CmdLineArgDesc *match = nullptr;
  size_t match_length = 0;
  bool ambiguous = false;

  for (const auto &arg : CmdLine::CmdLineArgs()) {
    if (arg.type == CmdLineArgType::None || arg.name.size() >= name.size()) {
      continue;
    }
    if (!CompArgName(arg.name, name.substr(0, arg.name.size()))) {
      continue;
    }
    if (arg.name.size() < match_length) {
      continue;
    }
    if (arg.name.size() > match_length) {
      match = &arg;
      match_length = arg.name.size();
      ambiguous = false;
    } else {
      ambiguous = true;
    }
  }

  if (ambiguous) {
    throw CmdLineArgParseError(std::format("Ambiguous argument: {}", name));
  }
  if (!match) {
    return nullptr;
  }
  value = name.substr(match_length);
  return match;
}

static void ParseArg(const CmdLineArgDesc &arg, std::string_view value) {
  if (arg.type != CmdLineArgType::Repeatable && *arg.input_count != 0) {
    throw CmdLineArgParseError(
        std::format("Argument specified multiple times: {}", arg.name));
  }
  arg.parser(arg.ptr, value);
  ++*arg.input_count;
}

std::vector<std::string_view> CmdLine::Parse(int argc, char **argv) {
  for (auto &arg : CmdLineArgs()) {
    *arg.input_count = 0;
  }

  std::vector<std::string_view> rest;
  for (int i = 1; i < argc;) {
    std::string_view arg(argv[i]);
    if (arg == "--") {
      for (int j = i + 1; j < argc; j++) {
        rest.push_back(argv[j]);
      }
      return rest;
    }
    if (arg == "-") {
      rest.push_back(arg);
      i++;
      continue;
    }
    if (arg.starts_with("-") || arg.starts_with("--")) {
      std::string_view name_val =
          arg.starts_with("--") ? arg.substr(2) : arg.substr(1);
      std::string_view name, value;
      if (auto eq_pos = name_val.find('='); eq_pos == name_val.npos) {
        name = name_val;
        auto desc = TryGetArg(name);
        if (!desc) {
          desc = TryGetPrefixArg(name, value);
          if (desc) {
            ParseArg(*desc, value);
            i++;
            continue;
          }
        }
        if (!desc) {
          throw CmdLineArgParseError(std::format("Unknown argument: {}", name));
        }

        if (desc->type == CmdLineArgType::None) {
          ParseArg(*desc, {});
          i++;
        } else {
          if (i + 1 < argc) {
            ParseArg(*desc, argv[i + 1]);
            i += 2;
          } else {
            throw CmdLineArgParseError(
                std::format("Missing value for argument: {}", name));
          }
        }
      } else {
        name = name_val.substr(0, eq_pos);
        if (name.empty()) {
          throw CmdLineArgParseError(
              std::format("Empty argument name: {}", name_val));
        }
        value = name_val.substr(eq_pos + 1);
        auto desc = TryGetArg(name);
        if (!desc) {
          throw CmdLineArgParseError(std::format("Unknown argument: {}", name));
        }
        ParseArg(*desc, value);
        i++;
      }
    } else {
      rest.push_back(arg);
      i++;
    }
  }
  return rest;
}
