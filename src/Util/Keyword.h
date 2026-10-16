#pragma once

#include <algorithm>
#include <string_view>

// TODO: Add more keywords as needed

inline bool IsTypeKeyword(std::string_view str) {
  static std::string_view type_keywords[] = {"void",  "int",    "char",
                                             "float", "double", "long"};
  return std::ranges::contains(type_keywords, str);
}

inline bool IsStorageSpecifierKeyword(std::string_view str) {
  static std::string_view storage_specifiers[] = {"static", "extern",
                                                  "register"};
  return std::ranges::contains(storage_specifiers, str);
}

inline bool IsTypeQualifierKeyword(std::string_view str) {
  static std::string_view type_qualifiers[] = {"const", "volatile"};
  return std::ranges::contains(type_qualifiers, str);
}
