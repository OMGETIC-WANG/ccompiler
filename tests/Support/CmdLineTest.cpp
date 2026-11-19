#include "CmdLineTest.h"

#include "Util/CmdLine.h"

#include <initializer_list>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

CMDLINE_ARG(std::string, cmdline_test_value, "v", "");
CMDLINE_ARG(std::vector<std::string>, cmdline_test_paths, "I", {});
CMDLINE_ARG(bool, cmdline_test_flag, false);
CMDLINE_ARG(std::string, cmdline_test_short, "x", "");
CMDLINE_ARG(std::string, cmdline_test_long, "xyz", "");
CMDLINE_ARG(std::string, cmdline_test_alias_one, "ab_c", "");
CMDLINE_ARG(std::string, cmdline_test_alias_two, "ab-c", "");

namespace {

class CmdLineInvocation {
public:
  CmdLineInvocation(std::initializer_list<std::string_view> arguments) {
    storage_.reserve(arguments.size() + 1);
    pointers_.reserve(arguments.size() + 1);
    storage_.emplace_back("ccompiler_tests");
    for (std::string_view argument : arguments) {
      storage_.emplace_back(argument);
    }
    for (std::string &argument : storage_) {
      pointers_.push_back(argument.data());
    }
  }

  std::vector<std::string_view> Parse() {
    return CmdLine::Parse(static_cast<int>(pointers_.size()), pointers_.data());
  }

private:
  std::vector<std::string> storage_;
  std::vector<char *> pointers_;
};

bool Check(bool condition, std::string_view message) {
  if (condition) {
    return true;
  }
  std::cerr << "FAIL command line parsing: " << message << '\n';
  return false;
}

void ResetValues() {
  cmdline_test_value.clear();
  cmdline_test_paths.clear();
  cmdline_test_flag = false;
  cmdline_test_short.clear();
  cmdline_test_long.clear();
  cmdline_test_alias_one.clear();
  cmdline_test_alias_two.clear();
}

bool ExpectParseError(std::initializer_list<std::string_view> arguments,
                      std::string_view expected) {
  try {
    CmdLineInvocation invocation(arguments);
    invocation.Parse();
  } catch (const CmdLineArgParseError &error) {
    return Check(error.what() == expected, expected);
  }
  return Check(false, "expected a command line parse error");
}

} // namespace

int RunCmdLineSelfTest() {
  ResetValues();
  {
    CmdLineInvocation invocation({"-Ifirst", "-I", "second", "-I=third"});
    auto rest = invocation.Parse();
    if (!Check(rest.empty(), "attached include arguments leaked into rest") ||
        !Check(cmdline_test_paths.size() == 3,
               "repeatable option did not accept all values") ||
        !Check(cmdline_test_paths[0] == "first",
               "compact option fallback parsed the wrong value") ||
        !Check(cmdline_test_paths[1] == "second",
               "separated repeatable value was not parsed") ||
        !Check(cmdline_test_paths[2] == "third",
               "equals-separated repeatable value was not parsed")) {
      return 1;
    }
  }

  ResetValues();
  {
    CmdLineInvocation invocation({"-xshort", "-xyzlong"});
    invocation.Parse();
    if (!Check(cmdline_test_short == "short",
               "short prefix did not attach its value") ||
        !Check(cmdline_test_long == "long",
               "longest prefix did not win over a shorter prefix")) {
      return 1;
    }
  }

  ResetValues();
  if (!ExpectParseError({"-vfirst", "-v", "second"},
                        "Argument specified multiple times: v") ||
      !ExpectParseError({"--cmdline-test-flag", "--cmdline_test_flag"},
                        "Argument specified multiple times: "
                        "cmdline_test_flag") ||
      !ExpectParseError({"-ab_cvalue"}, "Ambiguous argument: ab_cvalue") ||
      !ExpectParseError({"--unknown-option"},
                        "Unknown argument: unknown-option") ||
      !ExpectParseError({"-v"}, "Missing value for argument: v") ||
      !ExpectParseError({"--cmdline-test-flag=maybe"},
                        "Failed to parse boolean value: maybe")) {
    return 1;
  }

  ResetValues();
  {
    CmdLineInvocation invocation(
        {"--cmdline-test-flag=false", "--", "-v", "literal"});
    auto rest = invocation.Parse();
    if (!Check(!cmdline_test_flag, "boolean value was not parsed") ||
        !Check(rest.size() == 2 && rest[0] == "-v" && rest[1] == "literal",
               "arguments after '--' were not preserved")) {
      return 1;
    }
  }

  ResetValues();
  {
    CmdLineInvocation invocation({"-v="});
    invocation.Parse();
    if (!Check(cmdline_test_value.empty(),
               "an explicit empty value was not accepted")) {
      return 1;
    }
  }

  std::cout << "PASS command line parsing\n";
  return 0;
}
