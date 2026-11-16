#pragma once

#include "../../AST/AST.h"
#include "GenContext.h"

namespace llvm {
class Constant;
} // namespace llvm

class IRBuilder;

void GenFunction(FunctionDecl *decl, IRBuilder &builder, GenContext &context);

void GenStructDecl(StructDecl *decl, GenContext &context);

void GenUnionDecl(UnionDecl *decl, GenContext &context);

void GenEnumDecl(EnumDecl *decl, GenContext &context);

llvm::Constant *GenConstInitializer(TypeRef type, Initializer *initializer,
                                    GenContext &context);

LValue GenGlobalVarDecl(VarDecl *decl, GenContext &context);

LValue GenExternVarDecl(VarDecl *decl, GenContext &context);
