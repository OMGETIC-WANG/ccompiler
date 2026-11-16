#include "QualifierCodeGenTest.h"

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
extern int external_only;
int tentative;
extern int tentative;
extern int extern_initialized = 11;
int tentative_then_initialized;
int tentative_then_initialized = 9;
int initialized_then_tentative = 7;
int initialized_then_tentative;
static int internal_link;
extern int internal_link;
volatile int observed;

static int internal_function(void) {
  return 1;
}
extern int internal_function(void);

int use_local_extern(void) {
  extern int local_external;
  return local_external;
}

void restricted(int *__restrict__ first, volatile int *second) {
  *first = *second;
}

int *restrict restricted_return(void) {
  return &tentative;
}

struct Record {
  int value;
};

void write_record(volatile struct Record *record) {
  record->value = 4;
}

int main(void) {
  volatile int local = 3;
  local = local + 1;
  observed = local;
  return observed;
}
)";

bool Check(bool condition, std::string_view message) {
  if (condition) {
    return true;
  }
  std::cerr << "FAIL qualified IR: " << message << '\n';
  return false;
}

bool HasVolatileLoad(const llvm::Function &function) {
  for (const auto &block : function) {
    for (const auto &instruction : block) {
      if (const auto *load = llvm::dyn_cast<llvm::LoadInst>(&instruction);
          load && load->isVolatile()) {
        return true;
      }
    }
  }
  return false;
}

bool HasVolatileStore(const llvm::Function &function) {
  for (const auto &block : function) {
    for (const auto &instruction : block) {
      if (const auto *store = llvm::dyn_cast<llvm::StoreInst>(&instruction);
          store && store->isVolatile()) {
        return true;
      }
    }
  }
  return false;
}

int Run() {
  SourceManager sources;
  SourceHandle source =
      sources.LoadSource(std::string(kSource), "<qualifier-ir>");

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
    std::cerr << "FAIL qualified IR: invalid module\n" << verification_error;
    return 1;
  }

  auto *external_only = module->getGlobalVariable("external_only", true);
  auto *tentative = module->getGlobalVariable("tentative", true);
  auto *extern_initialized =
      module->getGlobalVariable("extern_initialized", true);
  auto *tentative_then_initialized =
      module->getGlobalVariable("tentative_then_initialized", true);
  auto *initialized_then_tentative =
      module->getGlobalVariable("initialized_then_tentative", true);
  auto *internal_link = module->getGlobalVariable("internal_link", true);
  auto *local_external = module->getGlobalVariable("local_external", true);
  auto *observed = module->getGlobalVariable("observed", true);
  auto *internal_function = module->getFunction("internal_function");
  auto *restricted = module->getFunction("restricted");
  auto *restricted_return = module->getFunction("restricted_return");
  auto *write_record = module->getFunction("write_record");
  auto *main_function = module->getFunction("main");

  if (!Check(external_only && external_only->isDeclaration() &&
                 external_only->hasExternalLinkage(),
             "extern-only object must be an external declaration") ||
      !Check(tentative && !tentative->isDeclaration() &&
                 tentative->hasExternalLinkage(),
             "tentative object must have one external definition") ||
      !Check(extern_initialized && !extern_initialized->isDeclaration() &&
                 llvm::cast<llvm::ConstantInt>(
                     extern_initialized->getInitializer())
                     ->equalsInt(11),
             "initialized extern object must be a definition") ||
      !Check(tentative_then_initialized &&
                 llvm::cast<llvm::ConstantInt>(
                     tentative_then_initialized->getInitializer())
                     ->equalsInt(9),
             "later initializer must replace a tentative definition") ||
      !Check(initialized_then_tentative &&
                 llvm::cast<llvm::ConstantInt>(
                     initialized_then_tentative->getInitializer())
                     ->equalsInt(7),
             "later tentative declaration must not overwrite an initializer") ||
      !Check(internal_link && !internal_link->isDeclaration() &&
                 internal_link->hasInternalLinkage(),
             "static-then-extern object must retain internal linkage") ||
      !Check(local_external && local_external->isDeclaration() &&
                 local_external->hasExternalLinkage(),
             "block-scope extern must create an external declaration") ||
      !Check(observed && !observed->isDeclaration(),
             "volatile global must still be defined")) {
    return 1;
  }

  if (!Check(internal_function && !internal_function->isDeclaration() &&
                 internal_function->hasInternalLinkage(),
             "static-then-extern function must retain internal linkage") ||
      !Check(restricted &&
                 restricted->hasParamAttribute(0, llvm::Attribute::NoAlias),
             "restrict parameter must have noalias") ||
      !Check(restricted_return &&
                 restricted_return->hasRetAttribute(llvm::Attribute::NoAlias),
             "restrict return must have noalias") ||
      !Check(write_record && HasVolatileStore(*write_record),
             "volatile aggregate members must use volatile stores") ||
      !Check(main_function && HasVolatileLoad(*main_function),
             "volatile loads must be emitted as LLVM volatile") ||
      !Check(main_function && HasVolatileStore(*main_function),
             "volatile stores must be emitted as LLVM volatile")) {
    return 1;
  }

  std::cout << "PASS qualified IR\n";
  return 0;
}

} // namespace

int RunQualifierCodeGenSelfTest() {
  try {
    return Run();
  } catch (const std::exception &error) {
    std::cerr << "FAIL qualified IR: " << error.what() << '\n';
    return 1;
  }
}
