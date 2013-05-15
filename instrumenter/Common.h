#ifndef ASSERTIONS_INSTRUMENTER_COMMON_H
#define ASSERTIONS_INSTRUMENTER_COMMON_H

#include <utility>

namespace llvm {
  class StringRef;
  template <typename T> class SmallVectorImpl;
}

namespace assertions {

namespace {
  using namespace llvm;
}

typedef SmallVectorImpl<std::pair<StringRef, StringRef>> UID_KindTy;

// Puts the UIDs parsed from anno in the specified SmallVector. If anno isn't
// a valid function call annotation string, does nothing and returns false.
bool ParseAssertionFuncall(StringRef anno, SmallVectorImpl<StringRef> &UIDs);

bool ParseAssertionMeta(StringRef anno, UID_KindTy &UID_Kinds);

}

#endif