#pragma once

#include <filesystem>
#include <span>
#include <string_view>

int RunLexerTest(const std::filesystem::path &source_path,
                 const std::filesystem::path &expected_path,
                 const std::filesystem::path &aliases_path,
                 std::span<const std::string_view> args);

int RunLexerSelfTest();
