#pragma once

#include <filesystem>

int RunStructuredDiagnosticTest(const std::filesystem::path &source_path,
                                const std::filesystem::path &expected_path);
