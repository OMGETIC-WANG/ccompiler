#include "Intrinsics.h"

#include "Sizeof.h"
#include "Va.h"

void RegisterBuiltinIntrinsics() {
  static const bool registered = [] {
    IntrinsicManager::RegisterIntrinsic(va_start_desc);
    IntrinsicManager::RegisterIntrinsic(va_arg_desc);
    IntrinsicManager::RegisterIntrinsic(va_end_desc);
    IntrinsicManager::RegisterIntrinsic(sizeof_desc);
    return true;
  }();
  (void)registered;
}
