#include "StringJoin.h" // from Clang
#include "Assertion.h"  // from Clang

#include "Common.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Constant.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/Debug.h"
#include <utility>

using namespace llvm;

namespace assertions {

void status(StringRef pass, Twine message, int level) {
  // dbgs() does not have colors :(
  raw_ostream& out = errs();
  // assert(out.has_colors());
  out << "[";
  out.changeColor(raw_ostream::MAGENTA) << pass;
  out.resetColor() << "] ";
  for (int i = 0; i < level-1; ++i) {
    out << "  ";
  }
  if (level > 0) {
    out.changeColor(raw_ostream::BLUE, true);
    out << "` ";
  } 
  out.changeColor(raw_ostream::YELLOW) << message << "\n";
  out.resetColor();
}

raw_ostream &info(StringRef subject) {
  raw_ostream &out = errs();
  out.changeColor(raw_ostream::BLUE, true) << subject;
  out.resetColor() << ": ";
  return out;
}


typedef Common::FnMapTy   FnMapTy;
typedef Common::FuncType  FuncType;

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

Constant *Common::getStructValueFor(StringRef AssertionKind) {
  auto StructName = (AssertionKind + "_state_default").str();
  GlobalVariable *Struct =
    cast_or_null<GlobalVariable>(M.getNamedGlobal(StructName));
    // DEBUG(dbgs() << "Struct Initializer: " << *Struct->getInitializer() << "\n");
  return Struct->getInitializer();
}

std::string getGlobalStateNameFor(Function *F, Assertion &As) {
  return (F->getName() + "." + getStateName(As.UID)).str();
}

Common::FnMapTy &Common::SwitchCache(FuncType type) {
  switch (type) {
    case FuncType::Init:  return InitFuncs;
    case FuncType::Update: return UpdateFuncs;
    case FuncType::Alloc: return AllocFuncs;
    default:
      llvm_unreachable("Unhandled FuncType in Caller.cpp");
  }
}

Function *Common::GetFuncFor(StringRef assertionKind,
                             FuncType type, bool strict) {
  FnMapTy &Map = SwitchCache(type);
  auto &Cached = Map[assertionKind];
  if (!Cached) {
    // Lookup @"__init_" + a.Kind in the Assertions module.
    StringRef prefix;
    switch (type) {
      case FuncType::Init:   prefix = "__init_"; break;
      case FuncType::Update: prefix = "__update_"; break;
      case FuncType::Alloc:  prefix = "__alloc_"; break;
    }
    std::string FnName = (prefix + assertionKind).str();
    //auto Fn = Co.Assertions.getFunction(FnName);
    auto Fn = M.getFunction(FnName);
    if (!Fn) {
      if (strict) {
        report_fatal_error("Instrumentation function '" + FnName + "' "
          + "does not exist in Assertions.c");
      }
      return nullptr;
    }
    // No need anymore, since we're linking "Assertions" in first.
    // Function *FDecl = cast<Function>(
    //   Mod->getOrInsertFunction(FnName, Fn->getFunctionType(),
    //                            Fn->getAttributes()));
    // InitFunc->setLinkage(GlobalValue::LinkageTypes::LinkerPrivateLinkage);
    // Cached = FDecl;
    Cached = Fn;
  }
  return Cached;
}

}