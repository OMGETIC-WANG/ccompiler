#include "Util/CmdLine.h"

#include <gtest/gtest.h>

#include <initializer_list>
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
    storage_.emplace_back("ccompiler_self_tests");
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

void ResetValues() {
  cmdline_test_value.clear();
  cmdline_test_paths.clear();
  cmdline_test_flag = false;
  cmdline_test_short.clear();
  cmdline_test_long.clear();
  cmdline_test_alias_one.clear();
  cmdline_test_alias_two.clear();
}

void ExpectParseError(std::initializer_list<std::string_view> arguments,
                      std::string_view expected) {
  bool raised = false;
  try {
    CmdLineInvocation invocation(arguments);
    invocation.Parse();
  } catch (const CmdLineArgParseError &error) {
    raised = true;
    EXPECT_EQ(error.what(), expected);
  }
  EXPECT_TRUE(raised);
}

} // namespace

TEST(CmdLine, ParsesRepeatableIncludeArguments) {
  ResetValues();
  CmdLineInvocation invocation({"-Ifirst", "-I", "second", "-I=third"});
  const auto rest = invocation.Parse();

  EXPECT_TRUE(rest.empty());
  ASSERT_EQ(cmdline_test_paths.size(), 3);
  EXPECT_EQ(cmdline_test_paths[0], "first");
  EXPECT_EQ(cmdline_test_paths[1], "second");
  EXPECT_EQ(cmdline_test_paths[2], "third");
}

TEST(CmdLine, UsesLongestOptionPrefix) {
  ResetValues();
  CmdLineInvocation invocation({"-xshort", "-xyzlong"});
  invocation.Parse();

  EXPECT_EQ(cmdline_test_short, "short");
  EXPECT_EQ(cmdline_test_long, "long");
}

TEST(CmdLine, RejectsDuplicateArguments) {
  ResetValues();
  ExpectParseError({"-vfirst", "-v", "second"},
                   "Argument specified multiple times: v");
  ExpectParseError({"--cmdline-test-flag", "--cmdline_test_flag"},
                   "Argument specified multiple times: cmdline_test_flag");
}

TEST(CmdLine, HandlesDoubleDashAndBooleanValues) {
  ResetValues();
  CmdLineInvocation invocation(
      {"--cmdline-test-flag=false", "--", "-v", "literal"});
  const auto rest = invocation.Parse();

  EXPECT_FALSE(cmdline_test_flag);
  ASSERT_EQ(rest.size(), 2);
  EXPECT_EQ(rest[0], "-v");
  EXPECT_EQ(rest[1], "literal");
}

TEST(CmdLine, AcceptsExplicitEmptyValue) {
  ResetValues();
  CmdLineInvocation invocation({"-v="});
  invocation.Parse();

  EXPECT_TRUE(cmdline_test_value.empty());
}

TEST(CmdLine, ReportsMalformedArguments) {
  ResetValues();
  ExpectParseError({"-ab_cvalue"}, "Ambiguous argument: ab_cvalue");
  ExpectParseError({"--unknown-option"}, "Unknown argument: unknown-option");
  ExpectParseError({"-v"}, "Missing value for argument: v");
  ExpectParseError({"--cmdline-test-flag=maybe"},
                   "Failed to parse boolean value: maybe");
}
