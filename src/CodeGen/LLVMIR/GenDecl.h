#pragma once

#include "../../AST/AST.h"
#include "GenContext.h"

std::string GenVariableDecl(VarDecl *decl, GenContext &context);

std::string GenStructDecl(StructDecl *decl, GenContext &context);

std::string GenEnumDecl(EnumDecl *decl, GenContext &context);

std::string GenGlobalVarInit(TypeBase *type, Initializer *initializer,
                             GenContext &context);
