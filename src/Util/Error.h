#pragma once

#include <string>

class IError {
public:
  virtual ~IError() = default;

  virtual std::string ToString() const = 0;
};
