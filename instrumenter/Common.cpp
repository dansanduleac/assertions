#include "StringJoin.h" // from Clang

#include "Common.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/raw_ostream.h"
#include <utility>

using namespace llvm;

namespace assertions {

// Puts the UIDs parsed from anno in the specified SmallVector. If anno isn't
// a valid function call annotation string, does nothing and returns false.
bool ParseAssertionFuncall(StringRef anno, SmallVectorImpl<StringRef> &UIDs) {
  StringRef prefix0 = "assertion.funcall,";
  if (anno.startswith(prefix0)) {
    auto Rest = anno.slice(prefix0.size(), anno.size());
    Rest.split(UIDs, ",");
    return true;
  }
  return false;
}

bool ParseAssertionMeta(StringRef anno, UID_KindTy &UID_Kinds) {
  StringRef prefix0 = "assertion.meta,";
  if (anno.startswith(prefix0)) {
    auto Rest = anno.slice(prefix0.size(), anno.size());
    while (!Rest.empty()) {
      // They appear as Kind (p.first), then UID (p2.first)
      auto p = Rest.split(',');
      auto p2 = p.second.split(',');
      Rest = p2.second;
      // But here we're putting them in the opposite order. Oh well.
      UID_Kinds.push_back( std::make_pair(p2.first, p.first) );
    }
    // Rest.split(UIDs, ",");
    return true;
  }
  return false; 
}

std::string getStateName(int UID) {
  Concatenation StateName(".");
  StateName.append("assertions");
  StateName.append(UID);
  StateName.append("state");
  return StateName.str();
}

StructType *Common::getStructTypeFor(StringRef AssertionKind) {
  auto StructName = ("struct." + AssertionKind + "_state").str();
  StructType *ST = M.getTypeByName(StructName);
  if (!ST) {
    // Create an empty struct instead, and give it that name.
    ST = StructType::create(ArrayRef<Type*>(), StructName);
  }
  return ST;
}

}