#include "CodeGenTestSupport.h"

#include <gtest/gtest.h>

#include <llvm/IR/Constants.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/GlobalVariable.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Module.h>

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

} // namespace

TEST(QualifiedCodeGen, EmitsGlobalStorageAndLinkage) {
  CodeGenTestModule generated(kSource, "<qualifier-ir>");
  ASSERT_FALSE(generated.HasError()) << generated.GetError();
  llvm::Module *module = generated.GetModule();
  ASSERT_NE(module, nullptr);

  auto *external_only = module->getGlobalVariable("external_only", true);
  ASSERT_NE(external_only, nullptr);
  EXPECT_TRUE(external_only->isDeclaration());
  EXPECT_TRUE(external_only->hasExternalLinkage());

  auto *tentative = module->getGlobalVariable("tentative", true);
  ASSERT_NE(tentative, nullptr);
  EXPECT_FALSE(tentative->isDeclaration());
  EXPECT_TRUE(tentative->hasExternalLinkage());

  auto *extern_initialized =
      module->getGlobalVariable("extern_initialized", true);
  ASSERT_NE(extern_initialized, nullptr);
  ASSERT_FALSE(extern_initialized->isDeclaration());
  auto *extern_initializer =
      llvm::dyn_cast<llvm::ConstantInt>(extern_initialized->getInitializer());
  ASSERT_NE(extern_initializer, nullptr);
  EXPECT_TRUE(extern_initializer->equalsInt(11));

  auto *tentative_then_initialized =
      module->getGlobalVariable("tentative_then_initialized", true);
  ASSERT_NE(tentative_then_initialized, nullptr);
  auto *tentative_then_initializer = llvm::dyn_cast<llvm::ConstantInt>(
      tentative_then_initialized->getInitializer());
  ASSERT_NE(tentative_then_initializer, nullptr);
  EXPECT_TRUE(tentative_then_initializer->equalsInt(9));

  auto *initialized_then_tentative =
      module->getGlobalVariable("initialized_then_tentative", true);
  ASSERT_NE(initialized_then_tentative, nullptr);
  auto *initialized_then_tentative_value = llvm::dyn_cast<llvm::ConstantInt>(
      initialized_then_tentative->getInitializer());
  ASSERT_NE(initialized_then_tentative_value, nullptr);
  EXPECT_TRUE(initialized_then_tentative_value->equalsInt(7));

  auto *internal_link = module->getGlobalVariable("internal_link", true);
  ASSERT_NE(internal_link, nullptr);
  EXPECT_FALSE(internal_link->isDeclaration());
  EXPECT_TRUE(internal_link->hasInternalLinkage());

  auto *local_external = module->getGlobalVariable("local_external", true);
  ASSERT_NE(local_external, nullptr);
  EXPECT_TRUE(local_external->isDeclaration());
  EXPECT_TRUE(local_external->hasExternalLinkage());

  auto *observed = module->getGlobalVariable("observed", true);
  ASSERT_NE(observed, nullptr);
  EXPECT_FALSE(observed->isDeclaration());
}

TEST(QualifiedCodeGen, EmitsFunctionStorageAndRestrict) {
  CodeGenTestModule generated(kSource, "<qualifier-ir>");
  ASSERT_FALSE(generated.HasError()) << generated.GetError();
  llvm::Module *module = generated.GetModule();
  ASSERT_NE(module, nullptr);

  auto *internal_function = module->getFunction("internal_function");
  ASSERT_NE(internal_function, nullptr);
  EXPECT_FALSE(internal_function->isDeclaration());
  EXPECT_TRUE(internal_function->hasInternalLinkage());

  auto *restricted = module->getFunction("restricted");
  ASSERT_NE(restricted, nullptr);
  EXPECT_TRUE(restricted->hasParamAttribute(0, llvm::Attribute::NoAlias));

  auto *restricted_return = module->getFunction("restricted_return");
  ASSERT_NE(restricted_return, nullptr);
  EXPECT_TRUE(restricted_return->hasRetAttribute(llvm::Attribute::NoAlias));
}

TEST(QualifiedCodeGen, EmitsVolatileAccesses) {
  CodeGenTestModule generated(kSource, "<qualifier-ir>");
  ASSERT_FALSE(generated.HasError()) << generated.GetError();
  llvm::Module *module = generated.GetModule();
  ASSERT_NE(module, nullptr);

  auto *write_record = module->getFunction("write_record");
  ASSERT_NE(write_record, nullptr);
  EXPECT_TRUE(HasVolatileStore(*write_record));

  auto *main_function = module->getFunction("main");
  ASSERT_NE(main_function, nullptr);
  EXPECT_TRUE(HasVolatileLoad(*main_function));
  EXPECT_TRUE(HasVolatileStore(*main_function));
}

TEST(QualifiedCodeGen, ProducesVerifiableModule) {
  CodeGenTestModule generated(kSource, "<qualifier-ir>");

  EXPECT_FALSE(generated.HasError()) << generated.GetError();
  EXPECT_NE(generated.GetModule(), nullptr);
}
