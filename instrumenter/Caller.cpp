#include "Caller.h"
#include "Common.h"

#include "llvm/IR/Function.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/ValueSymbolTable.h"
#include "llvm/Support/CallSite.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"

#include "StringJoin.h"

using namespace llvm;

namespace assertions {

char CallerInstrumenter::ID = 0;

typedef Common::FnMapTy   FnMapTy;
typedef Common::FuncType  FuncType;

CallerInstrumenter::~CallerInstrumenter() {}

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
    // Externally defined function, ignore.
    if (!Callee)
      continue;
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
  DEBUG(status("Caller", "Instrumenting assertion initialization"));
  // IRBuilder::getInt8PtrTy()
  auto I = CS.arg_begin();
  // *I is the i8* bitcast of the new variable, save that.
  Value *Addr = (*I++);
  Assertion As = AM.getParsedAssertion(ParseAnnotationCall(CS));

  Function *F = Co.GetFuncFor(As.Kind, FuncType::Init);
  IRBuilder<> Builder(Inst.getParent());

  // === Converting Props =================================================

  // Pass props as NULL-terminated array of strings.
  auto *PropsTy = cast<PointerType>(F->getFunctionType()->getParamType(2));
  // Or could get it out of ParamTy.
  auto *ElemTy  = Builder.getInt8PtrTy();
  // The array type should be compatible with ParamTy(now i8**).
  //auto Props = ConstantPointerNull::get(Builder.getInt8PtrTy()->getPointerTo());
  static_assert(sizeof(int) <= sizeof(char *),
    "sizeof(int) must fit into char*");
  // Make As.Params nicer: parse ints directly to int (fits in i8*)
  SmallVector<Constant*, 3> ParamsArr;
  for (StringRef str : As.Params) {
    DEBUG(info("Param") << str << "\n");
    int Int; // TODO Could make it size_t? always the size of a pointer,
    // and modify Assertions.c accordingly to cast to size_t
    // TODO If converted to int, will have to bitcast it to ElemTy.
    if (!str.getAsInteger(0, Int)) {
      // TODO assuming sizeof(int) == 4
      Constant *IntC = ConstantInt::get(Builder.getInt32Ty(), Int);
      ParamsArr.push_back(
        ConstantExpr::getIntToPtr(IntC, ElemTy));
      continue;
    }

    // Default case: create a constant string.
    // TODO move ALL these into a CreateGlobalString method
    // This is a [x * i8] constant, do a const GEP on it
    auto *ConstStr = ConstantDataArray::getString(getGlobalContext(), str);
    // TODO make it unnamed_addr
    auto *ConstStrGV = new GlobalVariable(*Mod, ConstStr->getType(), true,
      GlobalValue::PrivateLinkage, ConstStr, "assertions.prop");
    DEBUG(info("ConstStrGV") << *ConstStrGV << "\n");

    Constant *Idx = ConstantInt::get(Builder.getInt32Ty(), 0);
    Constant *Indices[] = { Idx, Idx };
    ParamsArr.push_back(
      ConstantExpr::getGetElementPtr(ConstStrGV, Indices, true));
    DEBUG(info("Which GEPped") << *ParamsArr.back() << "\n");
  }
  // End the list with a NULL ptr.
  ParamsArr.push_back(ConstantPointerNull::get(ElemTy));
  // TODO move all these in a CreateGlobalArray method
  auto *ArrayTy = ArrayType::get(ElemTy, ParamsArr.size());

  auto *Array = ConstantArray::get(ArrayTy, ParamsArr);
  auto *ArrayGV = 
          new GlobalVariable(*Mod, Array->getType(), true,
            GlobalValue::PrivateLinkage, Array,
            "assertions.props");

  DEBUG(info("Params array") << *ArrayGV << "\n");
  // We can't just do a bitcast to ParamTy, we need a GEP.
  // TODO ocnsider ConstantExpr::getGetElementPtr(ArrayGV, )
  auto *Props = Builder.CreateConstInBoundsGEP2_32(ArrayGV, 0, 0);
  assert(Props->getType() == PropsTy && "Props argument type mismatch");
  DEBUG(info("Props arg") << *Props << "\n");


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
    DEBUG(info("Alloca state") << Alloca->getName() << "  isStatic=" 
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

bool CallerInstrumenter::InstrumentExpr(Instruction &Inst, CallSite &CS) {
  DEBUG(status("Caller", "Instrumenting assertion Expr"));
  // This should also be used for CallExpr (Clang).
  auto I = CS.arg_begin();
  // Value *Addr = (*I++);
  // Addr should the i8* bitcast of the modified variable, but it can also be
  // *null, specifically when we're annotating a clang CallExpr.
  I++;
  StringRef anno = ParseAnnotationCall(CS);
  StringRef prefix1 = "assertion,";
  LLVMContext &Context = Inst.getParent()->getParent()->getContext();

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
    Function *F = Co.GetFuncFor(As.Kind, FuncType::Update);

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
    Function *ThisF = Inst.getParent()->getParent();
    if (!State) {
      // Haven't generated the alloca here, must be function parameter.
      State = ThisF->getValueSymbolTable().lookup( getStateName(As.UID) );
    }
    DEBUG(info("Annotated Expr") << *CS.getInstruction() << "\n");
    DEBUG(info("State") << *State << "\n");
    if (!State) {
      Concatenation Err;
      Err << "Couldn't find state for UID " << As.UID;
      Err << " in function '" << ThisF->getName() << "'. ";
      Err << "Only pass unoptimised modules to this program.";
      Context.emitError(&Inst, Err.str());
      return true;
    }
    // Instead of passing Addr (the updated variable's address), look 2
    // instructions behind for the store (because one instruction behind is
    // the addr bitcast), and pass the value.
    auto *store = dyn_cast<StoreInst>(Inst.getPrevNode()->getPrevNode());
    assert(store && "Variable update annotation, but no store beforehand");
    auto *NewVal = store->getValueOperand();
    Builder.CreateCall4(F, NewVal, State, FNameExpr, *++I);
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