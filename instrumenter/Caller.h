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

#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Pass.h"

namespace llvm {
  class Function;
  class Instruction;
  class LLVMContext;
  class Module;
}

namespace assertions {

class CallerInstrumentation;

/// Instruments function calls in the caller context.
class CallerInstrumenter : public llvm::FunctionPass {
public:
  static char ID;
  CallerInstrumenter() : FunctionPass(ID) {}
  ~CallerInstrumenter();

  const char* getPassName() const {
    return "Assertions function instrumenter (caller-side)";
  }

  virtual bool doInitialization(llvm::Module &M);
  virtual bool runOnFunction(llvm::Function &Fn);
};



// /// Function instrumentation (caller context).
// class CallerInstrumentation
//   : public FnInstrumentation, public InstInstrumentation
// {
// public:
//   /// Construct an object that can instrument calls to a given function.
//   static CallerInstrumentation* Build(llvm::Module&, llvm::Function *Target);

//   // InstInstrumentation implementation:
//   bool Instrument(llvm::Instruction&);

// private:
//   /// Private constructor: clients should use CalleeInstrumention::Build().
//   CallerInstrumentation(llvm::Module& M, llvm::Function *Target,
//                         llvm::Function *Instr, FunctionEvent::Direction Dir)
//     : FnInstrumentation(M, Target, Instr, Dir)
//   {
//   }
// };

}

#endif	/* !TESLA_CALLER_INSTRUMENTATION_H */

