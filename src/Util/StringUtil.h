#pragma once

#include <string>
#include <string_view>

inline bool EndsWithInsensitive(std::string_view str, std::string_view suffix) {
  if (str.size() < suffix.size()) {
    return false;
  }
  for (size_t i = 0; i < suffix.size(); i++) {
    if (tolower(str[str.size() - suffix.size() + i]) != tolower(suffix[i])) {
      return false;
    }
  }
  return true;
}

inline bool StartsWithInsensitive(std::string_view str,
                                  std::string_view prefix) {
  if (str.size() < prefix.size()) {
    return false;
  }
  for (size_t i = 0; i < prefix.size(); i++) {
    if (tolower(str[i]) != tolower(prefix[i])) {
      return false;
    }
  }
  return true;
}
