// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#pragma once
#include "CodeGenTypeCache.h"
#include "soll/AST/Decl.h"
#include "soll/AST/DeclYul.h"
#include "soll/Basic/TargetOptions.h"
#include <llvm/ADT/APInt.h>
#include <llvm/IR/ConstantFolder.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Type.h>

namespace soll {
class ASTContext;
class DiagnosticsEngine;

namespace CodeGen {

class CodeGenModule : public CodeGenTypeCache {
  ASTContext &Context;
  llvm::Module &TheModule;
  DiagnosticsEngine &Diags;
  const TargetOptions &TargetOpts;
  llvm::LLVMContext &VMContext;
  llvm::IRBuilder<llvm::ConstantFolder> Builder;
  llvm::DenseMap<const VarDecl *, llvm::GlobalVariable *> StateVarDeclMap;
  std::size_t StateVarAddrCursor;

  llvm::Function *Func_callDataCopy = nullptr;
  llvm::Function *Func_callStatic = nullptr;
  llvm::Function *Func_finish = nullptr;
  llvm::Function *Func_getCallDataSize = nullptr;
  llvm::Function *Func_getCallValue = nullptr;
  llvm::Function *Func_getCaller = nullptr;
  llvm::Function *Func_getGasLeft = nullptr;
  llvm::Function *Func_log = nullptr;
  llvm::Function *Func_log0 = nullptr;
  llvm::Function *Func_log1 = nullptr;
  llvm::Function *Func_log2 = nullptr;
  llvm::Function *Func_log3 = nullptr;
  llvm::Function *Func_log4 = nullptr;
  llvm::Function *Func_returnDataCopy = nullptr;
  llvm::Function *Func_revert = nullptr;
  llvm::Function *Func_storageLoad = nullptr;
  llvm::Function *Func_storageStore = nullptr;
  llvm::Function *Func_getTxGasPrice = nullptr;
  llvm::Function *Func_getTxOrigin = nullptr;
  llvm::Function *Func_getBlockCoinbase = nullptr;
  llvm::Function *Func_getBlockDifficulty = nullptr;
  llvm::Function *Func_getBlockGasLimit = nullptr;
  llvm::Function *Func_getBlockNumber = nullptr;
  llvm::Function *Func_getBlockTimestamp = nullptr;
  llvm::Function *Func_getBlockHash = nullptr;

  llvm::Function *Func_print32 = nullptr;

  llvm::Function *Func_keccak256 = nullptr;
  llvm::Function *Func_sha256 = nullptr;
  llvm::Function *Func_sha3 = nullptr;

  llvm::Function *Func_bswap256 = nullptr;
  llvm::Function *Func_memcpy = nullptr;

  void initTypes();

  void initEVMOpcodeDeclaration();
  void initEEIDeclaration();

  void initHelperDeclaration();
  void initBswapI256();
  void initMemcpy();

  void initPrebuiltContract();
  void initKeccak256();
  void initSha256();

public:
  CodeGenModule(const CodeGenModule &) = delete;
  void operator=(const CodeGenModule &) = delete;

  CodeGenModule(ASTContext &C, llvm::Module &module, DiagnosticsEngine &Diags,
                const TargetOptions &TargetOpts);
  llvm::Function *getIntrinsic(unsigned IID,
                               llvm::ArrayRef<llvm::Type *> Typs = llvm::None);
  llvm::Module &getModule() const { return TheModule; }
  llvm::LLVMContext &getLLVMContext() const { return VMContext; }
  llvm::IRBuilder<llvm::ConstantFolder> &getBuilder() { return Builder; }

  bool isEVM() const noexcept { return TargetOpts.BackendTarget == EVM; }
  bool isEWASM() const noexcept { return TargetOpts.BackendTarget == EWASM; }

  void emitContractDecl(const ContractDecl *CD);
  void emitYulObject(const YulObject *YO);
  llvm::Value *emitEndianConvert(llvm::Value *Val);
  llvm::Value *getEndianlessValue(llvm::Value *Val);

  llvm::Value *emitGetGasLeft();
  llvm::Value *emitGetCallValue();
  llvm::Value *emitGetCaller();
  void emitFinish(llvm::Value *DataOffset, llvm::Value *Length);
  void emitLog(llvm::Value *DataOffset, llvm::Value *DataLength,
               std::vector<llvm::Value *> &Topics);
  void emitRevert(llvm::Value *DataOffset, llvm::Value *Length);
  void emitCallDataCopy(llvm::Value *ResultOffset, llvm::Value *DataOffset,
                        llvm::Value *Length);
  llvm::Value *emitStorageLoad(llvm::Value *Key);
  void emitStorageStore(llvm::Value *Key, llvm::Value *Value);

private:
  void emitContractConstructorDecl(const ContractDecl *CD);
  void emitContractDispatcherDecl(const ContractDecl *CD);
  void emitEventDecl(const EventDecl *ED);
  void emitFunctionDecl(const FunctionDecl *FD);
  void emitVarDecl(const VarDecl *VD);

  void emitYulCode(const YulCode *YC);
  void emitYulData(const YulData *YD);
  void emitYulVarDecl(const YulVarDecl *VD);

  void emitABILoad(const FunctionDecl *FD, llvm::BasicBlock *Loader,
                   llvm::BasicBlock *Error, llvm::Value *callDataSize);
  llvm::Value *emitABILoadParamStatic(const Type *Ty, llvm::StringRef Name,
                                      llvm::Value *Buffer,
                                      std::uint32_t Offset);
  std::pair<llvm::Value *, llvm::Value *>
  emitABILoadParamDynamic(const Type *Ty, llvm::Value *Size,
                          llvm::StringRef Name, llvm::Value *Buffer,
                          llvm::Value *Offset);
  void emitABIStore(const Type *Ty, llvm::StringRef Name, llvm::Value *Result);

public:
  std::string getMangledName(const CallableVarDecl *CVD);
  llvm::Type *getLLVMType(const Type *Ty);
  llvm::Type *getStaticLLVMType(const Type *Ty);
  llvm::FunctionType *getFunctionType(const CallableVarDecl *CVD);
  llvm::Function *createLLVMFunction(const CallableVarDecl *CVD);
  llvm::GlobalVariable *getStateVarAddr(const VarDecl *VD) {
    return StateVarDeclMap.lookup(VD);
  }
};

} // namespace CodeGen
} // namespace soll
