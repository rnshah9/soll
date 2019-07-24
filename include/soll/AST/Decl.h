#pragma once

#include "soll/AST/DeclVisitor.h"
#include "soll/AST/Expr.h"
#include "soll/AST/Type.h"
#include "soll/Basic/IdentifierTable.h"
#include "soll/Basic/SourceLocation.h"
#include <memory>
#include <vector>

namespace soll {

class ASTContext;
class Token;

class Decl {
public:
  enum class Visibility { Default, Private, Internal, Public, External };
  virtual ~Decl() noexcept {}

private:
  Visibility Vis;
  std::string Name;

protected:
  friend class ASTReader;

protected:
  Decl() {}
  Decl(llvm::StringRef Name,
       Visibility vis = Visibility::Default)
      : Name(Name.str()), Vis(vis) {}

public:
  virtual void accept(DeclVisitor &visitor) = 0;
  virtual void accept(ConstDeclVisitor &visitor) const = 0;
};

class SourceUnit : public Decl {
  std::vector<std::unique_ptr<Decl>> Nodes;

public:
  SourceUnit(std::vector<std::unique_ptr<Decl>> &&Nodes)
      : Nodes(std::move(Nodes)) {}

  void setNodes(std::vector<std::unique_ptr<Decl>> &&Nodes) {
    for (auto &Node : Nodes)
      this->Nodes.emplace_back(std::move(Node));
  }

  std::vector<Decl *> getNodes() {
    std::vector<Decl *> Nodes;
    for (auto &Node : this->Nodes)
      Nodes.push_back(Node.get());
    return Nodes;
  }
  std::vector<const Decl *> getNodes() const {
    std::vector<const Decl *> Nodes;
    for (auto &Node : this->Nodes)
      Nodes.push_back(Node.get());
    return Nodes;
  }

  void accept(DeclVisitor &visitor) override;
  void accept(ConstDeclVisitor &visitor) const override;
};

class PragmaDirective : public Decl {
public:
  void accept(DeclVisitor &visitor) override;
  void accept(ConstDeclVisitor &visitor) const override;
};

class InheritanceSpecifier;
class FunctionDecl;
class ContractDecl : public Decl {
public:
  enum class ContractKind { Interface, Contract, Library };

private:
  std::vector<std::unique_ptr<InheritanceSpecifier>> BaseContracts;
  std::vector<std::unique_ptr<FunctionDecl>> Functions;
  ContractKind Kind;

public:
  ContractDecl(llvm::StringRef name,
      std::vector<std::unique_ptr<InheritanceSpecifier>> &&baseContracts,
      ContractKind kind = ContractKind::Contract)
      : Decl(name), BaseContracts(std::move(baseContracts)),
        Kind(kind) {}

  std::vector<FunctionDecl *> getFuncs() {
    std::vector<FunctionDecl *> Funcs;
    for (auto &Func : this->Functions)
      Funcs.push_back(Func.get());
    return Funcs;
  }
  std::vector<const FunctionDecl *> getFuncs() const {
    std::vector<const FunctionDecl *> Funcs;
    for (auto &Func : this->Functions)
      Funcs.push_back(Func.get());
    return Funcs;
  }

  void accept(DeclVisitor &visitor) override;
  void accept(ConstDeclVisitor &visitor) const override;
};

class InheritanceSpecifier {
  std::string BaseName;
  std::vector<std::unique_ptr<Expr>> Arguments;

public:
  InheritanceSpecifier(llvm::StringRef baseName,
                       std::vector<std::unique_ptr<Expr>> &&arguments)
      : BaseName(baseName.str()),
        Arguments(std::move(arguments)) {}
};

class ParamList;
class CallableVarDecl : public Decl {
  std::unique_ptr<ParamList> Params;
  std::unique_ptr<ParamList> ReturnParams;

public:
  CallableVarDecl(llvm::StringRef name,
      Visibility visibility, std::unique_ptr<ParamList> &&Params,
      std::unique_ptr<ParamList> &&returnParams = nullptr)
      : Decl(name, visibility), Params(std::move(Params)),
        ReturnParams(std::move(returnParams)) {}
};

enum class StateMutability { Pure, View, NonPayable, Payable };
class ModifierInvocation;
class Block;

class FunctionDecl : public CallableVarDecl {
  StateMutability SM;
  bool IsConstructor;
  std::vector<std::unique_ptr<ModifierInvocation>> FunctionModifiers;
  std::unique_ptr<Block> Body;
  bool Implemented;

public:
  FunctionDecl(llvm::StringRef name,
      Visibility visibility, StateMutability sm, bool isConstructor,
      std::unique_ptr<ParamList> &&Params,
      std::vector<std::unique_ptr<ModifierInvocation>> &&modifiers,
      std::unique_ptr<ParamList> &&returnParams,
      std::unique_ptr<Block> &&body)
      : CallableVarDecl(name, visibility, std::move(Params),
                        std::move(returnParams)),
        SM(sm), IsConstructor(isConstructor),
        FunctionModifiers(std::move(modifiers)), Body(std::move(body)),
        Implemented(body != nullptr) {}

  Block *getBody() const { return Body.get(); }

  void accept(DeclVisitor &visitor) override;
  void accept(ConstDeclVisitor &visitor) const override;
};

class VarDecl;
class ParamList {
  std::vector<std::unique_ptr<VarDecl>> Params;

public:
  ParamList(std::vector<std::unique_ptr<VarDecl>> &&Params)
      : Params(std::move(Params)) {}
};

class VarDecl : public Decl {
public:
  enum class Location { Unspecified, Storage, Memory, CallData };

private:
  std::unique_ptr<Type> TypeName;
  std::unique_ptr<Expr> Value;
  bool IsStateVariable;
  bool IsIndexed;
  bool IsConstant;
  Location ReferenceLocation;

public:
  VarDecl(std::unique_ptr<Type> &&T, llvm::StringRef name,
          std::unique_ptr<Expr> &&value, Visibility visibility,
          bool isStateVar = false, bool isIndexed = false,
          bool isConstant = false,
          Location referenceLocation = Location::Unspecified)
      : Decl(name, visibility), TypeName(std::move(T)),
        Value(std::move(value)), IsStateVariable(isStateVar),
        IsIndexed(isIndexed), IsConstant(isConstant),
        ReferenceLocation(referenceLocation) {}

  void accept(DeclVisitor &visitor) override;
  void accept(ConstDeclVisitor &visitor) const override;
};

class ModifierInvocation {
  std::string ModifierName;
  std::vector<std::unique_ptr<Expr>> Arguments;

public:
  ModifierInvocation(llvm::StringRef name,
                     std::vector<std::unique_ptr<Expr>> arguments)
      : ModifierName(name), Arguments(std::move(arguments)) {}
};

} // namespace soll
