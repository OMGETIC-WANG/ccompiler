#pragma once

#include <filesystem>

enum class ASTFixtureMode {
  ParseOnly,
  ParseAndAnalyze,
};

std::filesystem::path ExpectedASTPath(const std::filesystem::path &source_path);

int RunASTFixtureTest(const std::filesystem::path &source_path,
                      const std::filesystem::path &expected_path,
                      ASTFixtureMode mode);
