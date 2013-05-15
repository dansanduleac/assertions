//===- instrument.cpp - Driver for TESLA instrumentation passes -----------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file is derived from LLVM's 'tools/opt/opt.cpp'. It has been cut down
// to only invokes TESLA instrumentation passes, but many useful arguments to
// 'opt' still work, e.g. -p, -S and -verify-each.
//
//===----------------------------------------------------------------------===//

//#include "Assertion.h"
#include "Callee.h"
#include "Caller.h"
#include "Common.h"

#include "llvm/IR/DataLayout.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/Linker.h"
#include "llvm/IRReader/IRReader.h"
#include "llvm/CodeGen/CommandFlags.h"
#include "llvm/Bitcode/ReaderWriter.h"
#include "llvm/Assembly/PrintModulePass.h"
#include "llvm/Analysis/Verifier.h"
#include "llvm/Analysis/CallGraph.h"
#include "llvm/Target/TargetLibraryInfo.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/ADT/Triple.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Signals.h"
#include "llvm/Support/ManagedStatic.h"
#include "llvm/Support/PrettyStackTrace.h"
#include "llvm/Support/SourceMgr.h"          // SMDiagnostic
#include "llvm/Support/SystemUtils.h"
#include "llvm/Support/TargetRegistry.h"
#include "llvm/Support/ToolOutputFile.h"
#include "llvm/PassManager.h"

using namespace llvm;
using namespace assertions;

// Command line options.
//
static cl::opt<std::string>
InputFilename(cl::Positional, cl::desc("<input bitcode file>"),
    cl::init("-"), cl::value_desc("filename"));

static cl::opt<std::string>
OutputFilename("o", cl::desc("Override output filename"),
               cl::value_desc("filename"));

static cl::opt<bool>
Force("f", cl::desc("Enable binary output on terminals"));

static cl::opt<bool>
PrintEachXForm("p", cl::desc("Print module after each transformation"));

static cl::opt<bool>
OutputAssembly("S", cl::desc("Write output as LLVM assembly"));

static cl::opt<bool>
VerifyEach("verify-each", cl::desc("Verify after each transform"));

static cl::opt<bool>
Verbose("v", cl::desc("Print information about actions taken"));

// Filename of compiled bc Assertions module provided by CMake.
#ifdef ASSERTIONS_MODULE_PATH
#define STR2(X) #X
#define STR(X) STR2(X)
#define ASSERTIONS_FNAME STR(ASSERTIONS_MODULE_PATH)
#else
#error "Need to define ASSERTIONS_MODULE_PATH"
#endif


static inline void addPass(PassManagerBase &PM, Pass *P) {
  // Add the pass to the pass manager...
  PM.add(P);

  // If we are verifying all of the intermediate steps, add the verifier...
  if (VerifyEach) PM.add(createVerifierPass());

  // Optionally print the result of each pass...
  if (PrintEachXForm) PM.add(createPrintModulePass(&errs()));
}

const char *Argv0 = nullptr;

Module *LoadModule(StringRef Filename, LLVMContext& Context) {
  SMDiagnostic Err;
  Module *M = ParseIRFile(Filename, Err, Context);

  if (M == nullptr) {
    Err.print(Argv0, errs());
  }
  return M;
}

//===----------------------------------------------------------------------===//
//
int main(int argc, char **argv) {
  sys::PrintStackTraceOnErrorSignal();
  llvm::PrettyStackTraceProgram X(argc, argv);

  llvm_shutdown_obj Y;  // Call llvm_shutdown() on exit.
  LLVMContext &Context = getGlobalContext();

  cl::ParseCommandLineOptions(argc, argv, "Assertions bitcode instrumenter\n");

  Argv0 = argv[0];
  // Load the input module...
  llvm::OwningPtr<Module> M;
  M.reset(LoadModule(InputFilename, Context));
  // Load the assertions module...
  llvm::OwningPtr<Module> Assertions;
  Assertions.reset(LoadModule(ASSERTIONS_FNAME, Context));
  if (!M.get() || !Assertions.get()) {
    return 1;
  }


  // Create a PassManager to hold and optimize the collection of passes we are
  // about to build.
  //
  PassManager Passes;

  // Add an appropriate TargetLibraryInfo pass for the module's triple.
  TargetLibraryInfo *TLI = new TargetLibraryInfo(Triple(M->getTargetTriple()));
  Passes.add(TLI);

  // Add an appropriate DataLayout instance for this module.
  DataLayout *TD = 0;
  const std::string &ModuleDataLayout = M.get()->getDataLayout();
  if (!ModuleDataLayout.empty())
    TD = new DataLayout(ModuleDataLayout);

  if (TD)
    Passes.add(TD);

  // Just add TESLA instrumentation passes.
  // addPass(Passes, new tesla::AssertionSiteInstrumenter);
  OwningPtr<Common> Co(new Common(*Assertions.get()));
  addPass(Passes, new assertions::CalleeInstrumenter(*Co.get()));
  addPass(Passes, new assertions::CallerInstrumenter(*Co.get()));

  // Write bitcode or assembly to the output as the last step...
  // if (!NoOutput) {
  //   if (OutputAssembly)
  //     Passes.add(createPrintModulePass(&Out->os()));
  //   else
  //     Passes.add(createBitcodeWriterPass(Out->os()));
  // }

  // Before executing passes, print the final values of the LLVM options.
  cl::PrintOptionValues();


  // Link the module with the Assertions module.
  Linker L(M.get());
  if (Verbose) errs() << "Linking in the Assertions module\n";

  std::string ErrorMessage;
  if (L.linkInModule(Assertions.get(), &ErrorMessage)) {
    errs() << argv[0] << ": link error: " << ErrorMessage << "\n";
    return 1;
  }

  if (Verbose) errs() << "Running instrumentation passes\n";

  // Now that we have all of the passes ready, run them.
  Passes.run(*M.get());

  // Output stream...
  if (OutputFilename.empty())
    OutputFilename = "-";

  std::string ErrorInfo;
  tool_output_file Out(OutputFilename.c_str(), ErrorInfo,
                       raw_fd_ostream::F_Binary);
  if (!ErrorInfo.empty()) {
    errs() << ErrorInfo << '\n';
    return 1;
  }

  if (verifyModule(*M)) {
    errs() << argv[0] << ": linked module is broken!\n";
    return 1;
  }


  if (Verbose) errs() << "Writing bitcode...\n";

  // If the output is set to be emitted to standard out, and standard out is a
  // console, print out a warning message and refuse to do it.  We don't
  // impress anyone by spewing tons of binary goo to a terminal.
  bool NoOutput = false;
  if (!Force && !OutputAssembly)
    if (CheckBitcodeOutputToConsole(Out.os(), true))
      NoOutput = true;

  if (!NoOutput) {
    if (OutputAssembly) {
      Out.os() << *M;
    } else
      WriteBitcodeToFile(M.get(), Out.os());
  }

  // Declare success.
  if (!NoOutput)
    Out.keep();

  return 0;
}

