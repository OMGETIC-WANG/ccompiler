#pragma once

#include <deque>
#include <string>
#include <string_view>
#include <unordered_map>

class SourceManager {
public:
  std::string_view LoadSource(std::string source);
  std::string_view LoadSourceFrom(std::string_view path);

  std::string_view AddString(std::string s);

private:
  std::unordered_map<std::string, std::string> _files;
  std::deque<std::string> _strings;
};
