/*
 * Copyright (c) 2012-2013 Jonathan Anderson
 * All rights reserved.
 *
 * This software was developed by SRI International and the University of
 * Cambridge Computer Laboratory under DARPA/AFRL contract (FA8750-10-C-0237)
 * ("CTSRD"), as part of the DARPA CRASH research programme.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef	ANNOTATEVARIABLES_CALLER_INSTRUMENTATION_H
#define	ANNOTATEVARIABLES_CALLER_INSTRUMENTATION_H

#include "Common.h"
// From the clang tool.
#include "Assertion.h"

#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/IR/Value.h"
#include "llvm/Pass.h"


namespace llvm {
  class Function;
  class Instruction;
  class LLVMContext;
  class Module;
  class CallSite;
}

namespace assertions {

class CallerInstrumentation;

// TODO move to Common.h
/// A container for function arguments, which shouldn't be very numerous.
typedef llvm::SmallVector<llvm::Value*,3> ArgVector;

/// Instruments function calls in the caller context.
class CallerInstrumenter : public llvm::FunctionPass {
  llvm::Module *Mod;
  Common &Co;

  // Caches for alloc and update functions declared in the  module we're
  // processing.
public:
  typedef llvm::StringMap<llvm::Function *> FnMapTy;
  // Instrumentation function types.
  enum class FuncType { Init, Update };
private:
  FnMapTy InitFuncs;
  FnMapTy UpdateFuncs;

  llvm::DenseMap<int, llvm::Value *> States;

  AssertionManager AM; // To parse assertion strings.
public:

  static char ID;
  CallerInstrumenter(Common &C) : FunctionPass(ID), Co(C) {}
  ~CallerInstrumenter();

  const char* getPassName() const {
    return "Assertions function instrumenter (caller-side)";
  }

  virtual bool doInitialization(llvm::Module &M);
  virtual bool runOnFunction(llvm::Function &Fn);
  virtual bool runOnBasicBlock(llvm::BasicBlock &Block);

private:
  bool InstrumentInit(llvm::Instruction &Inst, llvm::CallSite &CS);
  bool InstrumentExpr(llvm::Instruction &Inst, llvm::CallSite &CS);

  Function *GetFuncFor(StringRef assertionKind, FuncType type);
  // Returns the cache for the requested instrumentation function type.
  FnMapTy &SwitchCache(FuncType type);
  // Gets the annotation string from call of the form void(i8*,i8*,i8*,i32).
  StringRef ParseAnnotationCall(llvm::CallSite &CS);
};

}

#endif	/* !TESLA_CALLER_INSTRUMENTATION_H */

