#ifndef ASSERTIONS_INSTRUMENTER_COMMON_H
#define ASSERTIONS_INSTRUMENTER_COMMON_H

#include "llvm/ADT/StringMap.h"
#include "llvm/IR/LLVMContext.h"

#include <utility>
#include <string>

namespace llvm {
  class StringRef;
  class StructType;
  class Module;
  class Constant;
  class Function;
  class Twine;
  class raw_ostream;
  template <typename T> class SmallVectorImpl;
}

namespace assertions {

using namespace llvm;

// === Debug methods ==========================================================

// Outputs a colored message to dbgs() mentioning the current pass and the
// message.
void status(StringRef pass, Twine message, int level = 0);

// First prints the subject in a colorful manner, then returns the stream.
raw_ostream &info(StringRef subject);

// === Assertion parsing methods ==============================================

struct Assertion;

typedef SmallVectorImpl<std::pair<StringRef, StringRef>> UID_KindTy;

// Puts the UIDs parsed from anno in the specified SmallVector. If anno isn't
// a valid function call annotation string, does nothing and returns false.
bool ParseAssertionFuncall(StringRef anno, SmallVectorImpl<StringRef> &UIDs);

bool ParseAssertionMeta(StringRef anno, UID_KindTy &UID_Kinds);

// === Instrumentation variables naming =======================================

std::string getStateName(int UID);
std::string getGlobalStateNameFor(Function *F, Assertion &As);


// === Instrumentation helpers ================================================

class Common {
public:
  typedef llvm::StringMap<llvm::Function *> FnMapTy;
  // Instrumentation function types.
  enum class FuncType { Init, Update, Alloc };

  // This one crashes if the function is not found, but may return nullptr if
  // strict is set to false.
  Function *GetFuncFor(StringRef assertionKind,
                       FuncType type, bool strict = true);
private:
  // Returns a reference to the desired cache based on the FuncType.
  FnMapTy &SwitchCache(FuncType type);

  // Caches for alloc, init and update functions declared in the module
  // we're processing.
  FnMapTy InitFuncs;
  FnMapTy UpdateFuncs;
  FnMapTy AllocFuncs;

public:
  // The Composite module we're working on.
  Module &M;
  // LLVM Context.
  LLVMContext &Context;

  Common(Module &Mod) : M(Mod), Context(getGlobalContext()) {}

  StructType *getStructTypeFor(StringRef AssertionKind);
  Constant *getStructValueFor(StringRef AssertionKind);

  // === Functions that add instrumentation ===================================

  Constant *GetPtrToGlobalString(StringRef str, StringRef name = "");

};

}

#endif