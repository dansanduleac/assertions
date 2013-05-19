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

#ifndef	ANNOTATEVARIABLES_CALLEE_INSTRUMENTATION_H
#define	ANNOTATEVARIABLES_CALLEE_INSTRUMENTATION_H

#include "Common.h"
 // From the clang tool.
#include "Assertion.h"

#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/DIBuilder.h"
#include "llvm/DebugInfo.h"
#include "llvm/IR/Constant.h"
#include "llvm/Pass.h"

namespace llvm {
  class Function;
  class Instruction;
  class LLVMContext;
  class Module;
  class Value;
  class StructType;
}

namespace assertions {

class AssertionManager;

/// Instruments function calls in the callee context.
/// Adds 
class CalleeInstrumenter : public llvm::ModulePass {
  Common &Co;

  struct AnnotationT {
    llvm::StringRef annotation;
    Constant *FName;
    Constant *LineNo;
  };
  
  typedef llvm::SmallVector<AnnotationT, 1> AnnotationsT;
  llvm::DenseMap<llvm::Function*, AnnotationsT> GlobalAnno;

  // Map each LLVM function to corresponding metadata with debug info. If
  // the function is replaced with another one, we should patch the pointer
  // to LLVM function in metadata.
  // As the code generation for module is finished (and DIBuilder is
  // finalized) we assume that subprogram descriptors won't be changed, and
  // they are stored in map for short duration anyway.
  typedef llvm::DenseMap<llvm::Function*, llvm::DISubprogram> FunctionDIMap;
  FunctionDIMap FunctionDIs;

  AssertionManager AM; // To parse assertion strings.
public:
  static char ID;
  CalleeInstrumenter(Common &C) : ModulePass(ID), Co(C) {}
  ~CalleeInstrumenter();

  const char* getPassName() const {
    return "Assertion function instrumenter (callee-side)";
  }

  virtual bool doInitialization(llvm::Module &M);
  virtual bool runOnModule(llvm::Module &M);
private:
  // Copied from DeadArgumentElimination.cpp
  void CollectFunctionDIs(llvm::Module &M);
  llvm::Function *ReplaceFunction(llvm::Function *F,
                                  llvm::ArrayRef<llvm::Type*> Params);
  void ExtractGlobalAnnotations(llvm::Module &M);

  bool runOnFunction(llvm::Function &Fn);
};

}

#endif

