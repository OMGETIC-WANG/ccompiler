#pragma once

#include <string>
#include <string_view>

class IError {
public:
  virtual ~IError() = default;

  virtual std::string_view GetTypeName() const = 0;
  virtual std::string ToString() const = 0;
};
