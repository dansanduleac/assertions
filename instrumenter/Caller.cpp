#include "Caller.h"
#include "Common.h"

#include "llvm/IR/Function.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/IRBuilder.h" // maybe
#include "llvm/IR/Module.h"
#include "llvm/IR/ValueSymbolTable.h"
#include "llvm/Support/CallSite.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"

#include "StringJoin.h"

using namespace llvm;

namespace assertions {

char CallerInstrumenter::ID = 0;

CallerInstrumenter::~CallerInstrumenter() {}

typedef CallerInstrumenter::FnMapTy   FnMapTy;
typedef CallerInstrumenter::FuncType  FuncType;

bool CallerInstrumenter::doInitialization(Module &M) {
  Mod = &M;
  // TODO add global function decls
  // Prototype in all of the Assertions.c functions?
  return true;
}

bool CallerInstrumenter::runOnFunction(Function &F) {
  bool modifiedIR = false;

  for (auto &Block : F) {
    modifiedIR |= runOnBasicBlock(Block);
  }

  return modifiedIR;
}

bool CallerInstrumenter::runOnBasicBlock(BasicBlock &Block) {
  bool modifiedIR = false;

  //for (auto &Inst : Block) {
  for (auto I = Block.begin(), E = Block.end(); I != E; ) {
    auto &Inst = *I++;
    // Use CallSite to support invokes as well.
    CallSite CS(&Inst);
    if (!CS)
      continue;
    Function *Callee = CS.getCalledFunction();
    assert(Callee && "We don't have a called function?");
    // We're interested in calls to llvm.var/assign.annotation
    switch (__builtin_expect(Callee->getIntrinsicID(),
                             Intrinsic::not_intrinsic)) {
      case Intrinsic::var_annotation:
        modifiedIR |= InstrumentInit(Inst, CS);
        break;
      case Intrinsic::assign_annotation:
        modifiedIR |= InstrumentExpr(Inst, CS);
        break;
      case Intrinsic::not_intrinsic:
      default:
        continue;
    }
  }

  return modifiedIR;
}

FnMapTy &CallerInstrumenter::SwitchCache(FuncType type) {
  switch (type) {
    case FuncType::Init:  return InitFuncs;
    case FuncType::Update: return UpdateFuncs;
    case FuncType::Alloc: return AllocFuncs;
    default:
      llvm_unreachable("Unhandled FuncType in Caller.cpp");
  }
}

// May return nullptr if the function was not found.
Function *CallerInstrumenter::GetFuncFor(StringRef assertionKind,
                                         FuncType type,
                                         bool strict) {
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
    auto Fn = Mod->getFunction(FnName);
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

StringRef CallerInstrumenter::ParseAnnotationCall(CallSite &CS) {
  auto I = CS.arg_begin() + 1;
  // Second one is getelementptr to the string annotation.
  GlobalVariable *StrGV =
    cast<GlobalVariable>(cast<ConstantExpr>(*I)->getOperand(0));
  // Also drop the trailing '\0'.
  auto Str = cast<ConstantDataSequential>(
                StrGV->getInitializer())->getAsString().drop_back();
  // TODO is StrGV->getInitializer() a MDString? ->getString()
  return Str;
}

bool CallerInstrumenter::InstrumentInit(Instruction &Inst, CallSite &CS) {
  DEBUG(dbgs() << "Instrumenting assertion initialization\n");
  // IRBuilder::getInt8PtrTy()
  auto I = CS.arg_begin();
  // *I is the i8* bitcast of the new variable, save that.
  Value *Addr = (*I++);
  Assertion As = AM.getParsedAssertion(ParseAnnotationCall(CS));

  Function *F = GetFuncFor(As.Kind, FuncType::Init);
  IRBuilder<> Builder(Inst.getParent());
  // Pass props as NULL-terminated array of strings.
  // TODO Make a ConstantArray from As.Params
  // TODO maybe pass as varargs actually?
  auto Props = ConstantPointerNull::get(Builder.getInt8PtrTy()->getPointerTo());
  // Name of the state variable.
  auto StateName = getStateName(As.UID);
  // Make sure to insert after the initialisation (store), because in most
  // cases it follows the annotation, and we want to run _after_ that. The
  // exception is function parameters, the "store" happens right before. So
  // just decide based on whether next instruction is a store.
  Instruction *Here = Inst.getNextNode();
  // BitCast
  Value *DirectAddr = cast<CastInst>(Addr)->getOperand(0);
  if (isa<StoreInst>(Here) &&
      // Store location == address specified in annotation?
      cast<StoreInst>(Here)->getPointerOperand() == DirectAddr) {
    // It is, save it after that then.
    Here = Here->getNextNode();
  }
  Builder.SetInsertPoint(Here);

  // If the struct type is not declared even, this creates an empty StructType
  // and returns that instead.
  auto *Type = Co.getStructTypeFor(As.Kind);
  Value *StateVar = nullptr;
  // If we have an non-existent, not defined or empty struct, then consider
  // that the annotation doesn't use any state.
  if (Type->isOpaque() || Type->getNumElements() == 0) {
    StateVar = ConstantPointerNull::get(Type->getPointerTo());
  } else {
  // TODO add a special assertion flag (not parameter?) that says "i'm going
  // to allocate memory myself", this probably can be tested by checking for
  // the existence of a different function that allocates and returns a
  // pointer, and it would be retrieved by , then that pointer would get passed to the init
  // function.
    // USE THIS:
       //  GetFuncFor(As.Kind, FuncType::Alloc)
    auto *Alloca = Builder.CreateAlloca(Type, nullptr, StateName);
    // And save it for re-use.
    States[As.UID] = Alloca;
    DEBUG(dbgs() << "Alloca state: " << Alloca->getName() << "  isStatic=" 
      << Alloca->isStaticAlloca() << "\n");
    StateVar = Alloca;
  }
  //The last 2 parameters pass as in the annotation call (file name & line).
  Value *FNameExpr = *++I;
  auto *FName =
    cast<GlobalValue>(cast<ConstantExpr>(FNameExpr)->getOperand(0));
  // Notice:
  // We're using a string that's sitting in "llvm.metadata", which will
  // magically vanish upon CodeGen, so let's go ahead and remove that.
  FName->setSection("");
  Builder.CreateCall5(F, StateVar, Addr, Props, FNameExpr, *++I);
  // Builder.CreateStore(Call, Alloca);

  // auto FTy = FunctionType::get(
  //   RType, Params, FTy->isVarArg());
  // // PointerType::get(IntegerType::get(C, 8), 0);
  Inst.eraseFromParent();
  return true;
}

// TODO this crashes if Clang compiles with O2 at creating the Call4, why...
bool CallerInstrumenter::InstrumentExpr(Instruction &Inst, CallSite &CS) {
  DEBUG(dbgs() << "Instrumenting assertion Expr\n");
  // This should also be used for CallExpr (Clang).
  auto I = CS.arg_begin();
  // *I should the i8* bitcast of the modified variable, but it can also be
  // *null, specifically when we're annotating a clang CallExpr.
  Value *Addr = (*I++);
  StringRef anno = ParseAnnotationCall(CS);
  StringRef prefix1 = "assertion,";

  SmallVector<StringRef, 2> UIDs;
  if (ParseAssertionFuncall(anno, UIDs)) {
    // We're annotating a CallExpr, and we have to change to function call
    // in order to supply the states for the UIDs in extra arguments.
    // Callee pass should have already added null values for those arguments.

    // HACK: Taking a BIG risk here, but assume it's for the CallInst just
    // before this one (or InvokeInst...)
    Instruction *Call = Inst.getPrevNode();
    assert(Call && "No prev node before meta assertion");
    auto PrevCS = CallSite(Call);
    // However, here asserting it's a CallInst. Technically, sould be fine if
    // it's an InvokeInst too, but I don't think the Clang transform can ever
    // produce annotations for such case at the moment.
    Function *Callee = PrevCS.getCalledFunction();
    assert(Callee && "Not a direct call, but asserted.");
    auto lastArg = Callee->getFunctionType()->getNumParams() - 1;

    // Call->dump();
    // DEBUG(dbgs() << cast<CallInst>(Call)->getNumArgOperands() << "\n");

    // Find the states for those UIDs in the function.
    int UID;
    // Call->getNumArgOperands
    // Start from end of UIDs and end of function proper arguments.
    for (auto II = UIDs.rbegin(), EE = UIDs.rend(); II != EE; ++II, --lastArg) {
      auto UID_str = *II;
      if (UID_str.getAsInteger(10, UID))
        report_fatal_error("Can't parse UID");
      // Are we replacing the null value set up by Callee instrumentation?
      Value *Arg = PrevCS.getArgument(lastArg);
      if (!(isa<UndefValue>(Arg))) {
        Arg->dump();
        report_fatal_error("Replaced arg should be undef, as set by Callee");
      }
      // Change it to pass in the state instead, as described by the UID.
      Value *state = States[UID];
      PrevCS.setArgument(lastArg, state);
    }

    // TODO could use BB.getValueSymbolTable()

  } else if (anno.startswith(prefix1)) {
    Assertion As = AM.getParsedAssertion(anno);
    Function *F = GetFuncFor(As.Kind, FuncType::Update);

    IRBuilder<> Builder(Inst.getParent());
    Builder.SetInsertPoint(&Inst);

    Value *FNameExpr = *++I;
    auto *FName =
      cast<GlobalValue>(cast<ConstantExpr>(FNameExpr)->getOperand(0));
    // Notice:
    // We're using a string that's sitting in "llvm.metadata", which will
    // magically vanish upon CodeGen, so let's go ahead and remove that.
    FName->setSection("");

    auto *State = States[As.UID];
    if (!State) {
      // Haven't generated the alloca here, must be function parameter.
      Function *ThisF = Inst.getParent()->getParent();
      State = ThisF->getValueSymbolTable().lookup( getStateName(As.UID) );
    }
    Builder.CreateCall4(F, Addr, State, FNameExpr, *++I);
  }
  Inst.eraseFromParent();
  return true;
}

  /// getIntrinsicID - This method returns the ID number of the specified
  /// function, or Intrinsic::not_intrinsic if the function is not an
  /// intrinsic, or if the pointer is null.  This value is always defined to be
  /// zero to allow easy checking for whether a function is intrinsic or not.
  /// The particular intrinsic functions which correspond to this value are
  /// defined in llvm/Intrinsics.h.  Results are cached in the LLVM context,
  /// subsequent requests for the same ID return results much faster from the
  /// cache.
  ///
  // unsigned getIntrinsicID() const LLVM_READONLY;
  // bool isIntrinsic() const { return getName().startswith("llvm."); }

}