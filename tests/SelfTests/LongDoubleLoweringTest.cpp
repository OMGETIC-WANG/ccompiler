#include "CodeGenTestSupport.h"

#include <gtest/gtest.h>

#include <llvm/ADT/APFloat.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/GlobalVariable.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Module.h>

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

} // namespace

TEST(LongDoubleLowering, PreservesGlobalConstants) {
  CodeGenTestModule generated(kSource, "<long-double-lowering>");
  ASSERT_FALSE(generated.HasError()) << generated.GetError();
  llvm::Module *module = generated.GetModule();
  ASSERT_NE(module, nullptr);

  auto *precise_global = module->getGlobalVariable("precise_global", true);
  auto *folded_global = module->getGlobalVariable("folded_global", true);
  auto *integer_global = module->getGlobalVariable("integer_global", true);

  EXPECT_TRUE(HasLongDoubleConstant(precise_global, "1.0000000000000000001"));
  EXPECT_TRUE(HasLongDoubleConstant(folded_global, "0x1.0000000000000002p+0"));
  EXPECT_TRUE(HasLongDoubleConstant(integer_global, "9007199254740993"));
}

TEST(LongDoubleLowering, UsesX86FP80ABI) {
  CodeGenTestModule generated(kSource, "<long-double-lowering>");
  ASSERT_FALSE(generated.HasError()) << generated.GetError();
  llvm::Module *module = generated.GetModule();
  ASSERT_NE(module, nullptr);

  auto *precise_global = module->getGlobalVariable("precise_global", true);
  ASSERT_NE(precise_global, nullptr);
  EXPECT_TRUE(precise_global->getValueType()->isX86_FP80Ty());
  EXPECT_EQ(precise_global->getAlign(), llvm::Align(16));

  auto *add = module->getFunction("add");
  ASSERT_NE(add, nullptr);
  EXPECT_TRUE(add->getReturnType()->isX86_FP80Ty());
  ASSERT_EQ(add->getFunctionType()->getNumParams(), 2);
  EXPECT_TRUE(add->getFunctionType()->getParamType(0)->isX86_FP80Ty());
  EXPECT_TRUE(add->getFunctionType()->getParamType(1)->isX86_FP80Ty());
}

TEST(LongDoubleLowering, EmitsConversionInstructions) {
  CodeGenTestModule generated(kSource, "<long-double-lowering>");
  ASSERT_FALSE(generated.HasError()) << generated.GetError();
  llvm::Module *module = generated.GetModule();
  ASSERT_NE(module, nullptr);

  EXPECT_TRUE(HasInstruction<llvm::FPExtInst>(*module));
  EXPECT_TRUE(HasInstruction<llvm::FPTruncInst>(*module));
  EXPECT_TRUE(HasInstruction<llvm::FPToSIInst>(*module));
  EXPECT_TRUE(HasInstruction<llvm::FPToUIInst>(*module));
  EXPECT_TRUE(HasInstruction<llvm::SIToFPInst>(*module));
  EXPECT_TRUE(HasInstruction<llvm::UIToFPInst>(*module));
}

TEST(LongDoubleLowering, EmitsFloatArithmetic) {
  CodeGenTestModule generated(kSource, "<long-double-lowering>");
  ASSERT_FALSE(generated.HasError()) << generated.GetError();
  llvm::Module *module = generated.GetModule();
  ASSERT_NE(module, nullptr);

  EXPECT_TRUE(HasFloatAdd(*module));
}

TEST(LongDoubleLowering, ProducesVerifiableModule) {
  CodeGenTestModule generated(kSource, "<long-double-lowering>");

  EXPECT_FALSE(generated.HasError()) << generated.GetError();
  EXPECT_NE(generated.GetModule(), nullptr);
}
