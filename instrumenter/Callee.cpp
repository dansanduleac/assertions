#include "Callee.h"

#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/CallSite.h"
#include "llvm/Support/Debug.h"
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
  auto *Annos = cast<GlobalVariable>(M.getNamedGlobal("llvm.global.annotations"));
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
    GlobalVariable *StrGV =
      cast<GlobalVariable>(cast<ConstantExpr>(*iter)->op_begin()->get());
    auto Str = cast<ConstantDataSequential>(
                  StrGV->getInitializer())->getAsString();
    // errs() << Str << "\n";
    GlobalAnno[F].push_back(Str);
  }
  // Don't do this, otherwise we end up with a dangling CallSite to 
  // the function.
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
    // It might not be a call instruction, could be e.g. bitcast in the
    // annotations ConstantArray.
    DEBUG(dbgs() << "Removing function use: ");
    DEBUG(F->use_back()->dump());
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

  DEBUG(dbgs() << "Assertions[Callee] - Updating function params\n");
  // Can't do foreach because we sometimes remove current function as we go
  // and we need iterators to maintain validity.
  for (Module::iterator I = M.begin(), E = M.end(); I != E; ) {
    runOnFunction(*I++);
  }
  return true;
}

bool CalleeInstrumenter::runOnFunction(Function &F) {
  DEBUG(dbgs() << "Running on: " << F.getName() << "\n");
  LLVMContext &C = F.getContext();
  FunctionType *FTy = F.getFunctionType();
  //unsigned NumArgs = F.arg_size();
  auto const it = GlobalAnno.find(&F);
  if (it != GlobalAnno.end()) {
    for (StringRef anno : it->second) {
      // Holds information about new parameters we should add.
      StringRef prefix0 = "assertion.meta,";
      // Holds assertions on the function's return value.
      StringRef prefix1 = "assertion,";
      if (anno.startswith(prefix0)) {
        // Recreate the function type.
        SmallVector<Type*, 10> Params(FTy->param_begin(), FTy->param_end());

        auto Rest = anno.slice(prefix0.size(), anno.size());
        SmallVector<StringRef, 2> UIDs;
        Rest.split(UIDs, ",");
        // auto &ArgList = F.getArgumentList();
        for (auto UID_cstr : UIDs) {
          // Arg is inserted at the function automatically.
          // TODO how do we "know" the actual type?
          // Maybe we can extract it from a compiled Assertions.h IR file
          auto UID = UID_cstr.slice(0, UID_cstr.size()-1);
          auto typ = PointerType::get(IntegerType::get(C, 8), 0);
          auto arg = new Argument(typ,
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
        // TODO
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