#include "LongDoubleLoweringTest.h"

#include "AST/Context.h"
#include "AST/TranslationUnit.h"
#include "AST/TypeContext.h"
#include "CodeGen/LLVMIR/Generator.h"
#include "CodeGen/LLVMIR/Intrinsics.h"
#include "Lex/Lexer.h"
#include "Lex/Macro.h"
#include "Sema/SemanticAnalysis.h"
#include "SourceManager.h"
#include "Util/Diagnostic.h"

#include <llvm/ADT/APFloat.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/GlobalVariable.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Verifier.h>
#include <llvm/Support/raw_ostream.h>

#include <iostream>
#include <memory>
#include <string>
#include <string_view>

namespace {

constexpr std::string_view kSource = R"(
long double precise_global = 1.0000000000000000001L;
long double folded_global = 1.0L + 0x1p-63L;
long double integer_global = 9007199254740993ULL;

long double add(long double left, long double right) {
  return left + right;
}

long double widen(double value) { return value; }
double narrow(long double value) { return (double)value; }
long double from_signed(int value) { return value; }
long double from_unsigned(unsigned int value) { return value; }
int to_signed(long double value) { return (int)value; }
unsigned int to_unsigned(long double value) { return (unsigned int)value; }

int main(void) {
  return 0;
}
)";

bool Check(bool condition, std::string_view message) {
  if (condition) {
    return true;
  }
  std::cerr << "FAIL long double lowering: " << message << '\n';
  return false;
}

template <typename Instruction>
bool HasInstruction(const llvm::Module &module) {
  for (const llvm::Function &function : module) {
    for (const llvm::BasicBlock &block : function) {
      for (const llvm::Instruction &instruction : block) {
        if (llvm::isa<Instruction>(&instruction)) {
          return true;
        }
      }
    }
  }
  return false;
}

bool HasFloatAdd(const llvm::Module &module) {
  for (const llvm::Function &function : module) {
    for (const llvm::BasicBlock &block : function) {
      for (const llvm::Instruction &instruction : block) {
        auto *binary = llvm::dyn_cast<llvm::BinaryOperator>(&instruction);
        if (binary && binary->getOpcode() == llvm::Instruction::FAdd) {
          return true;
        }
      }
    }
  }
  return false;
}

bool HasLongDoubleConstant(const llvm::GlobalVariable *global,
                           llvm::StringRef decimal_value) {
  if (!global || !global->hasInitializer() ||
      !global->getValueType()->isX86_FP80Ty()) {
    return false;
  }
  auto *constant = llvm::dyn_cast<llvm::ConstantFP>(global->getInitializer());
  if (!constant || &constant->getValueAPF().getSemantics() !=
                       &llvm::APFloatBase::x87DoubleExtended()) {
    return false;
  }
  llvm::APFloat expected(llvm::APFloatBase::x87DoubleExtended(), decimal_value);
  return constant->getValueAPF().bitwiseIsEqual(expected);
}

int Run() {
  SourceManager sources;
  SourceHandle source =
      sources.LoadSource(std::string(kSource), "<long-double-lowering>");

  MacroContext macros;
  TypeContext types;
  DiagnosticContext diagnostics(sources);
  CompileContext context(types, sources, diagnostics);
  RegisterBuiltinIntrinsics();
  RawLexer raw_lexer(source, macros, sources, context);
  Lexer lexer(raw_lexer);
  auto translation_unit =
      ParseTranslationUnit(lexer, types, sources, diagnostics);
  diagnostics.ThrowIfErrors();
  AnalyzeTranslationUnit(*translation_unit, types, diagnostics);
  diagnostics.ThrowIfErrors();

  auto llvm_context = std::make_unique<llvm::LLVMContext>();
  auto module = GenerateLLVMModule(translation_unit.get(), types, *llvm_context,
                                   diagnostics);
  std::string verification_error;
  llvm::raw_string_ostream error_stream(verification_error);
  if (llvm::verifyModule(*module, &error_stream)) {
    error_stream.flush();
    std::cerr << "FAIL long double lowering: invalid module\n"
              << verification_error;
    return 1;
  }

  auto *precise_global = module->getGlobalVariable("precise_global", true);
  auto *folded_global = module->getGlobalVariable("folded_global", true);
  auto *integer_global = module->getGlobalVariable("integer_global", true);
  auto *add = module->getFunction("add");
  auto *main_function = module->getFunction("main");

  if (!Check(precise_global && precise_global->getValueType()->isX86_FP80Ty() &&
                 precise_global->getAlign() == llvm::Align(16),
             "precise global must use aligned x86_fp80") ||
      !Check(HasLongDoubleConstant(precise_global, "1.0000000000000000001"),
             "literal constant lost long double precision") ||
      !Check(HasLongDoubleConstant(folded_global, "0x1.0000000000000002p+0"),
             "folded constant lost long double precision") ||
      !Check(HasLongDoubleConstant(integer_global, "9007199254740993"),
             "integer conversion lost long double precision") ||
      !Check(add && add->getReturnType()->isX86_FP80Ty() &&
                 add->getFunctionType()->getNumParams() == 2 &&
                 add->getFunctionType()->getParamType(0)->isX86_FP80Ty() &&
                 add->getFunctionType()->getParamType(1)->isX86_FP80Ty(),
             "long double function signature must use x86_fp80") ||
      !Check(main_function, "main function is missing") ||
      !Check(HasInstruction<llvm::FPExtInst>(*module),
             "missing fpext lowering") ||
      !Check(HasInstruction<llvm::FPTruncInst>(*module),
             "missing fptrunc lowering") ||
      !Check(HasInstruction<llvm::FPToSIInst>(*module),
             "missing fptosi lowering") ||
      !Check(HasInstruction<llvm::FPToUIInst>(*module),
             "missing fptoui lowering") ||
      !Check(HasInstruction<llvm::SIToFPInst>(*module),
             "missing sitofp lowering") ||
      !Check(HasInstruction<llvm::UIToFPInst>(*module),
             "missing uitofp lowering") ||
      !Check(HasFloatAdd(*module), "missing fadd lowering")) {
    return 1;
  }

  std::cout << "PASS long double lowering\n";
  return 0;
}

} // namespace

int RunLongDoubleLoweringSelfTest() {
  try {
    return Run();
  } catch (const std::exception &error) {
    std::cerr << "FAIL long double lowering: " << error.what() << '\n';
    return 1;
  }
}
