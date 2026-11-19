#include "Support/ASTVerifier.h"

#include <gtest/gtest.h>

#include <string>

TEST(VerifierRegistry, AllASTAndTypeVerifiersAreRegistered) {
  const std::vector<std::string> missing = FindMissingVerifierNames();

  ASSERT_TRUE(missing.empty()) << "Missing verifiers:\n"
                               << testing::PrintToString(missing);
}
