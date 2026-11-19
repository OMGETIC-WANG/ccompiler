#pragma once

#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>

#include <memory>
#include <string>
#include <string_view>

class CodeGenTestModule {
public:
  CodeGenTestModule(std::string_view source_text, std::string_view source_name);

  llvm::Module *GetModule() const { return module_.get(); }
  const std::string &GetError() const { return error_; }
  bool HasError() const { return !error_.empty(); }

private:
  std::unique_ptr<llvm::LLVMContext> llvm_context_;
  std::unique_ptr<llvm::Module> module_;
  std::string error_;
};
