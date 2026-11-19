#include "Support/LexerTest.h"

#include <exception>
#include <iostream>
#include <string_view>
#include <vector>

int main(int argc, char **argv) {
  if (argc < 4) {
    std::cerr << "Usage: " << argv[0]
              << " SOURCE EXPECTED_TOKENS TOKEN_ALIASES [-I DIR]...\n";
    return 2;
  }

  std::vector<std::string_view> args;
  for (int index = 4; index < argc; ++index) {
    args.emplace_back(argv[index]);
  }

  try {
    return RunLexerTest(argv[1], argv[2], argv[3], args);
  } catch (const std::exception &error) {
    std::cerr << "FAIL " << argv[1] << '\n' << error.what() << '\n';
    return 1;
  }
}
