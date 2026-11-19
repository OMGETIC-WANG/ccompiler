#include "CodeGenTestSupport.h"

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

#include <llvm/IR/Verifier.h>
#include <llvm/Support/raw_ostream.h>

#include <exception>
#include <memory>
#include <string>

CodeGenTestModule::CodeGenTestModule(std::string_view source_text,
                                     std::string_view source_name) {
  SourceManager sources;
  DiagnosticContext diagnostics(sources);
  try {
    SourceHandle source =
        sources.LoadSource(std::string(source_text), std::string(source_name));
    MacroContext macros;
    TypeContext types;
    CompileContext context(types, sources, diagnostics);

    RegisterBuiltinIntrinsics();
    RawLexer raw_lexer(source, macros, sources, context);
    Lexer lexer(raw_lexer);
    auto translation_unit =
        ParseTranslationUnit(lexer, types, sources, diagnostics);
    diagnostics.ThrowIfErrors();
    AnalyzeTranslationUnit(*translation_unit, types, diagnostics);
    diagnostics.ThrowIfErrors();

    llvm_context_ = std::make_unique<llvm::LLVMContext>();
    module_ = GenerateLLVMModule(translation_unit.get(), types, *llvm_context_,
                                 diagnostics);
    diagnostics.ThrowIfErrors();
    if (!module_) {
      error_ = "code generation returned no module";
      return;
    }

    std::string verification_error;
    llvm::raw_string_ostream error_stream(verification_error);
    if (llvm::verifyModule(*module_, &error_stream)) {
      error_stream.flush();
      error_ = "invalid LLVM module:\n" + verification_error;
      module_.reset();
    }
  } catch (const DiagnosticError &error) {
    error_ = diagnostics.Format(error.Messages());
  } catch (const std::exception &error) {
    error_ = error.what();
  }
}
