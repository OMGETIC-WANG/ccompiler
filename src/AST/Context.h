#pragma once

#include "../Util/EnumRefl.h"
#include "../Util/Util.h"
#include "Type.h"
#include <string_view>
#include <unordered_map>

// clang-format off
#define CONTEXT_ITEM_TYPE_VALUES(x)\
  x(Unknown)\
  x(Function)\
  x(Typedef)\
  x(Struct)\
  x(GlobalVariable)
// clang-format on

MAKE_ENUM(ContextItemType, CONTEXT_ITEM_TYPE_VALUES)
REGISTER_ENUM(ContextItemType, CONTEXT_ITEM_TYPE_VALUES)

class CompileContext {
public:
  std::vector<std::string_view> structs;
  std::unordered_map<std::string_view, std::shared_ptr<TypeBase>> typedefs;
  std::unordered_map<std::string_view, std::shared_ptr<TypeBase>> functions;
  std::unordered_map<std::string_view, std::shared_ptr<TypeBase>>
      global_variables;

  ContextItemType GetTypeOf(std::string_view name) const {
    if (Contains(structs, name))
      return ContextItemType::Struct;
    if (typedefs.contains(name))
      return ContextItemType::Typedef;
    if (functions.contains(name))
      return ContextItemType::Function;
    if (global_variables.contains(name))
      return ContextItemType::GlobalVariable;
    return ContextItemType::Unknown;
  }

  bool Is(std::string_view name, ContextItemType type) const {
    return GetTypeOf(name) == type;
  }
};
