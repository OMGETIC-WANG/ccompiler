#include "Support/ASTFixtureTest.h"

#include <exception>
#include <filesystem>
#include <iostream>
#include <string_view>

int main(int argc, char **argv) {
  if (argc < 2 || argc > 3) {
    std::cerr << "Usage: " << argv[0] << " SOURCE [EXPECTED_AST_YAML]\n";
    return 2;
  }

  const std::filesystem::path source_path = argv[1];
  const std::filesystem::path expected_path =
      argc == 3 ? std::filesystem::path(argv[2]) : ExpectedASTPath(source_path);
  try {
    return RunASTFixtureTest(source_path, expected_path,
                             ASTFixtureMode::ParseOnly);
  } catch (const std::exception &error) {
    std::cerr << "FAIL " << source_path << '\n' << error.what() << '\n';
    return 1;
  }
}
