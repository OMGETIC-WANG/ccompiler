#include "Diagnostics/DiagnosticFixtureTest.h"

#include <exception>
#include <iostream>

int main(int argc, char **argv) {
  if (argc != 3) {
    std::cerr << "Usage: " << argv[0] << " SOURCE EXPECTED_ERROR_YAML\n";
    return 2;
  }

  try {
    return RunStructuredDiagnosticTest(argv[1], argv[2]);
  } catch (const std::exception &error) {
    std::cerr << "FAIL " << argv[1] << '\n' << error.what() << '\n';
    return 1;
  }
}
