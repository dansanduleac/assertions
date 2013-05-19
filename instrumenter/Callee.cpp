#include "Callee.h"
#include "Common.h"

#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/CallSite.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/InstIterator.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

namespace assertions {

char CalleeInstrumenter::ID = 0;

CalleeInstrumenter::~CalleeInstrumenter() {}

class ValueOpRange {
  User &U;
  typedef User::value_op_iterator iter;
public:
  ValueOpRange(User &U) : U(U) {}
  iter begin() { return U.value_op_begin(); }
  iter end() { return U.value_op_end(); }
};

void CalleeInstrumenter::ExtractGlobalAnnotations(llvm::Module &M) {
  auto *Annos = cast_or_null<GlobalVariable>(M.getNamedGlobal("llvm.global.annotations"));
  if (!Annos)
    return;
  assert(Annos->getSection() == "llvm.metadata");
  // This should be a constant array.
  auto *Array = cast<ConstantArray>(Annos->getInitializer());
  for (Value *Op : ValueOpRange(*Array)) {
    // Key them by first arg (function).
    auto CS = cast<ConstantStruct>(Op);
    auto iter = CS->value_op_begin();
    // This is a ConstantExpr: rather than getting the instruction via
    // getInstruction(), then cast to CastInst etc., we just get the first op
    // which is the function.
    Function *F = cast<Function>(*cast<ConstantExpr>(*iter)->op_begin());
    iter++;
    // Second one is getelementptr to the string annotation.
    // Drop the last character as that is '\0'.
    GlobalVariable *StrGV =
      cast<GlobalVariable>(cast<ConstantExpr>(*iter)->op_begin()->get());
    auto Str = cast<ConstantDataSequential>(
                  StrGV->getInitializer())->getAsString().drop_back();
    auto *FName = cast<Constant>(*++iter);  //     =>  i8* file name
    auto *LineNo = cast<Constant>(*++iter); //     =>  i32 line number
    GlobalAnno[F].push_back( (AnnotationT) { Str, FName, LineNo } );
  }
  // Delete it, don't need this structure anymore.
  // (even if we don't, it's going to be eliminated in CodeGen)
  Annos->eraseFromParent();
}

bool CalleeInstrumenter::doInitialization(llvm::Module &M) {
  ExtractGlobalAnnotations(M);
  return true;
}

// Code largely copied from DeadArgumentElimina´tion.cpp:RemoveDeadStuffFromFunction
// Assumes that F's Attributes have already been modified to accommodate for extra Params,
// we just need to reuse them but create a new type.
Function *CalleeInstrumenter::ReplaceFunction(Function *F, ArrayRef<Type*> Params) {
  FunctionType *FTy = F->getFunctionType();
  FunctionType *NFTy = FunctionType::get(
    F->getReturnType(), Params, FTy->isVarArg());
  // No change?
  if (NFTy == FTy) {
    return F;
  }
  // Create the new function body and insert it into the module...
  Function *NF = Function::Create(NFTy, F->getLinkage());
  NF->copyAttributesFrom(F);
  // We already updated the attributes on F so use that.
  NF->setAttributes(F->getAttributes());
  F->getParent()->getFunctionList().insert(F, NF);
  NF->takeName(F);

  // Loop over all of the callers of the function, transforming the call sites
  // to pass in a smaller number of arguments into the new function.
  //
  std::vector<Value*> Args;
  SmallVector<AttributeSet, 8> AttributesVec;
  // Manual RAUW that also adapts Call instructions to new arguments.
  while (!F->use_empty()) {
    auto &Use = F->use_begin().getUse();
    // Must handle Constants specially, we cannot call replaceUsesOfWith on a
    // constant because they are uniqued.
    if (Constant *C = dyn_cast<Constant>(Use.getUser())) {
      if (!isa<GlobalValue>(C)) {
        C->replaceUsesOfWithOnConstant(F, NF, &Use);
        continue;
      }
    }
    CallSite CS(F->use_back());
    assert(CS);
    Instruction *Call = CS.getInstruction();

    AttributesVec.clear();
    const AttributeSet &CallPAL = CS.getAttributes();

    // The call return attributes.
    // AttributeSet RAttrs = CallPAL.getRetAttributes();

    // Adjust in case the function was changed to return void.
    // RAttrs =
    //   AttributeSet::get(NF->getContext(), AttributeSet::ReturnIndex,
    //                     AttrBuilder(RAttrs, AttributeSet::ReturnIndex).
    //     removeAttributes(AttributeFuncs::
    //                      typeIncompatible(NF->getReturnType(),
    //                                       AttributeSet::ReturnIndex),
    //                      AttributeSet::ReturnIndex));
    // if (RAttrs.hasAttributes(AttributeSet::ReturnIndex))
    //   AttributesVec.push_back(AttributeSet::get(NF->getContext(), RAttrs));

    // TODO Insert Undef argument (for now) in position at FTy->getNumParams()
    // (after last arg, before potential varargs).
    // auto Pos = CS.arg_begin() + FTy->getNumParams();

    // Declare these outside of the loops, so we can reuse them for the second
    // loop, which loops the varargs.
    CallSite::arg_iterator I = CS.arg_begin();
    unsigned i = 0;
    // Loop over those operands, corresponding to the normal arguments to the
    // original function, and add those that are still alive.
    // TODO loop really needed? can just set those first getNumParams() 
    // in Args() definition
    for (unsigned e = FTy->getNumParams(); i != e; ++I, ++i) {
      Args.push_back(*I);
      // Get original parameter attributes, but skip return attributes.
      if (CallPAL.hasAttributes(i + 1)) {
        AttrBuilder B(CallPAL, i + 1);
        AttributesVec.
            push_back(AttributeSet::get(F->getContext(), Args.size(), B));
      }
    }

    // For now, inject additional undefined params here.
    for (unsigned e = NFTy->getNumParams(); i != e; ++i) {
      Args.push_back(UndefValue::get(Params[i]));
    }

    // Push any varargs arguments on the list. Don't forget their attributes.
    for (CallSite::arg_iterator E = CS.arg_end(); I != E; ++I, ++i) {
      Args.push_back(*I);
      if (CallPAL.hasAttributes(i + 1)) {
        AttrBuilder B(CallPAL, i + 1);
        AttributesVec.
          push_back(AttributeSet::get(F->getContext(), Args.size(), B));
      }
    }
    
    if (CallPAL.hasAttributes(AttributeSet::FunctionIndex))
      AttributesVec.push_back(AttributeSet::get(Call->getContext(),
                                                CallPAL.getFnAttributes()));

    // Reconstruct the AttributesList based on the vector we constructed.
    AttributeSet NewCallPAL = AttributeSet::get(F->getContext(), AttributesVec);

    Instruction *New;
    if (InvokeInst *II = dyn_cast<InvokeInst>(Call)) {
      New = InvokeInst::Create(NF, II->getNormalDest(), II->getUnwindDest(),
                               Args, "", Call);
      cast<InvokeInst>(New)->setCallingConv(CS.getCallingConv());
      cast<InvokeInst>(New)->setAttributes(NewCallPAL);
    } else {
      New = CallInst::Create(NF, Args, "", Call);
      cast<CallInst>(New)->setCallingConv(CS.getCallingConv());
      cast<CallInst>(New)->setAttributes(NewCallPAL);
      if (cast<CallInst>(Call)->isTailCall())
        cast<CallInst>(New)->setTailCall();
    }
    New->setDebugLoc(Call->getDebugLoc());

    Args.clear();

    // Further go into the call and RAUW.
    if (!Call->use_empty()) {
      // Return type not changed? Just replace users then.
      assert(New->getType() == Call->getType());
      Call->replaceAllUsesWith(New);
      New->takeName(Call);
    }

    // Finally, remove the old call from the program, reducing the use-count of
    // F.
    Call->eraseFromParent();
  }

  // Since we have now created the new function, splice the body of the old
  // function right into the new function, leaving the old rotting hulk of the
  // function empty.
  NF->getBasicBlockList().splice(NF->begin(), F->getBasicBlockList());

  // Loop over the argument list, transferring uses of the old arguments over to
  // the new arguments, also transferring over the names as well.
  for (Function::arg_iterator I = F->arg_begin(), E = F->arg_end(),
       I2 = NF->arg_begin(); I != E; ++I) {
    I->replaceAllUsesWith(I2);
    I2->takeName(I);
    ++I2;
  }

  // Patch the pointer to LLVM function in debug info descriptor.
  FunctionDIMap::iterator DI = FunctionDIs.find(F);
  if (DI != FunctionDIs.end())
    DI->second.replaceFunction(NF);

  // Now that the old function is dead, delete it.
  F->eraseFromParent();
  return NF;
}

bool CalleeInstrumenter::runOnModule(Module &M) {
  // Collect debug info descriptors for functions.
  CollectFunctionDIs(M);
  // Can't do foreach because we sometimes remove current function as we go
  // and we need iterators to maintain validity.
  for (Module::iterator I = M.begin(), E = M.end(); I != E; ) {
    runOnFunction(*I++);
  }
  return true;
}

bool CalleeInstrumenter::runOnFunction(Function &F) {
  if (F.getName().startswith("__update_") ||
      F.getName().startswith("__init_")   ||
      F.getName().startswith("__alloc_")) {
    F.setLinkage(GlobalValue::LinkageTypes::LinkOnceODRLinkage);
  return true;
  }
  if (F.isIntrinsic()) {
    return true;
  }
  DEBUG(dbgs() << "[Callee] Running on: " << F.getName() << "\n");
  // LLVMContext &C = F.getContext();
  FunctionType *FTy = F.getFunctionType();
  //unsigned NumArgs = F.arg_size();
  auto const it = GlobalAnno.find(&F);
  if (it != GlobalAnno.end()) {
    for (AnnotationT annoInfo : it->second) {
      StringRef anno = annoInfo.annotation;
      // Holds assertions on the function's return value.
      StringRef prefix1 = "assertion,";
      SmallVector<std::pair<StringRef, StringRef>, 2> UID_Kinds;
      if (ParseAssertionMeta(anno, UID_Kinds)) {
        DEBUG(dbgs() << "[Callee] ` Updating function params\n");
        // Recreate the function type.
        SmallVector<Type*, 10> Params(FTy->param_begin(), FTy->param_end());
  
        // auto &ArgList = F.getArgumentList(); // like F.arg_begin() ..
        for (auto &pair : UID_Kinds) {
          auto &UID = pair.first;
          auto &AsKind = pair.second;
          // Extract the control structure type from the compiled Assertions.c
          // IR file.
          //auto typ = PointerType::get(IntegerType::get(C, 8), 0);
          auto *Type = Co.getStructTypeFor(AsKind)->getPointerTo();
          // Arg is inserted at the function automatically.
          auto arg = new Argument(Type,
            "assertions." + UID + ".state", &F);
          F.setDoesNotCapture(arg->getArgNo()+1);
          Params.push_back(arg->getType());
          // arg->addAttr(AttributeSet::get(C,
          //   Attribute::get(C, Attribute::NoCapture)));
          //errs() << "Adding " << arg << "\n";
        }
        if (!ReplaceFunction(&F, Params)) {
          return false;
        }

        // TODO Also put into a map FD -> UID_String -> parameter?
           // 

        // For each UID, add a slot in a structure.
        // Values: ArrayRef<Constant *>, each of which will be a i8*
        // named after the UID: "assertions.<UID>.state"
        // ConstantStruct, StructType
        /*
        llvm::Constant *Array = llvm::ConstantStruct::get(llvm::StructType::create(
        Annotations->getType(), Annotations.size()), Annotations);
        llvm::GlobalValue *gv = new llvm::GlobalVariable(getModule(),
          Array->getType(), false, llvm::GlobalValue::AppendingLinkage, Array,
          "llvm.global.annotations");
        gv->setSection(AnnotationSection);
        */
      } else if (anno.startswith(prefix1)) {
        DEBUG(dbgs() << "[Callee] ` Instrumenting asserted return value\n");
        Assertion As = AM.getParsedAssertion(anno);
        // To keep state for the function's return type,
        // 1) Introduce "static" variable that keeps the state (internal global)
        //    called `<function>.getStateName(As.UID)`
        auto GlobalStateName = getGlobalStateNameFor(&F, As);
        StructType *ST = Co.getStructTypeFor(As.Kind);
        Module *M = F.getParent();
        // Initialise it to the "default" state.
        Constant *Init = Co.getStructValueFor(As.Kind);
        GlobalVariable *StateVar = 
          new GlobalVariable(*M, ST, false,
            GlobalValue::LinkageTypes::InternalLinkage, Init,
            GlobalStateName);
        // 4) Instrument the function's return points so that it can
        //    always runs the Update function for the assertion As.
        for (auto I = inst_begin(F), E = inst_end(F); I != E; I++) {
          // Is this an actual return instruction?
          if (auto *Return = dyn_cast<ReturnInst>(&*I)) {
            DEBUG(dbgs() << "Return value: " << *Return << "\n");
            IRBuilder<> Builder(Return->getParent());
            Builder.SetInsertPoint(Return);
            if (F.getReturnType()->isVoidTy()) {
              getGlobalContext().emitError("Asserted return type can't be void");
            }
            auto *InstrFn = Co.GetFuncFor(As.Kind, Common::FuncType::Update);
            // Store Return->getReturnValue() to an alloca, so that we can
            // pass the address.
            auto *RV = Return->getReturnValue();
            Value *Args[] = {
              RV,
              StateVar, // state
              annoInfo.FName,
              annoInfo.LineNo
            };
            Builder.CreateCall(InstrFn, Args);
          }
        }
        break;
      }
    }
  }
  return true;
}


/// CollectFunctionDIs - Map each function in the module to its debug info
/// descriptor.
void CalleeInstrumenter::CollectFunctionDIs(Module &M) {
  FunctionDIs.clear();

  for (Module::named_metadata_iterator I = M.named_metadata_begin(),
       E = M.named_metadata_end(); I != E; ++I) {
    NamedMDNode &NMD = *I;
    for (unsigned MDIndex = 0, MDNum = NMD.getNumOperands();
         MDIndex < MDNum; ++MDIndex) {
      MDNode *Node = NMD.getOperand(MDIndex);
      if (!DIDescriptor(Node).isCompileUnit())
        continue;
      DICompileUnit CU(Node);
      const DIArray &SPs = CU.getSubprograms();
      for (unsigned SPIndex = 0, SPNum = SPs.getNumElements();
           SPIndex < SPNum; ++SPIndex) {
        DISubprogram SP(SPs.getElement(SPIndex));
        if (!SP.Verify())
          continue;
        if (Function *F = SP.getFunction())
          FunctionDIs[F] = SP;
      }
    }
  }
}

}