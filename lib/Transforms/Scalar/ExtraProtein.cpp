#include "llvm/ADT/SmallVector.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include "llvm/Transforms/Scalar/ExtraProtein.h"
#include "llvm/Support/Debug.h"
using namespace llvm;

#define DEBUG_TYPE "extra-protein"

namespace llvm {
void initializeExtraProteinLegacyPassPass(PassRegistry&);
} // end namespace llvm

namespace {
struct ExtraProteinLegacyPass : public FunctionPass {
  static char ID;
  ExtraProteinLegacyPass() : FunctionPass(ID) {
    initializeExtraProteinLegacyPassPass(*PassRegistry::getPassRegistry());
  }

  bool runOnFunction(Function &F) override;

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesCFG();
    AU.addRequired<LoopInfoWrapperPass>();
  }
};
} // end anonymous namespace

char ExtraProteinLegacyPass::ID = 0;
INITIALIZE_PASS_BEGIN(ExtraProteinLegacyPass, "extra-protein",
                      "Increase EVERY loops' trip counts! "
                      "(and break your program logic)",
                      false, false)
INITIALIZE_PASS_DEPENDENCY(LoopInfoWrapperPass)
INITIALIZE_PASS_END(ExtraProteinLegacyPass, "extra-protein",
                    "Increase EVERY loops' trip counts! "
                    "(and break your program logic)",
                    false, false)

bool ExtraProteinLegacyPass::runOnFunction(Function &F) {
  auto& LI = getAnalysis<LoopInfoWrapperPass>().getLoopInfo();
  if(LI.empty()) return false;

  SmallVector<Use*, 2> Worklist;
  for(Loop* L : LI) {
    // Can only handle single exit
    auto* ExitingBB = L->getExitingBlock();
    if(!ExitingBB) continue;
    auto* Br = dyn_cast<BranchInst>(ExitingBB->getTerminator());
    if(!Br || Br->getNumSuccessors() != 2) continue;
    BasicBlock *TrueBB = Br->getSuccessor(0);
    auto* Cmp = dyn_cast<CmpInst>(Br->getCondition());
    if(!Cmp) continue;
    LLVM_DEBUG(dbgs() << "Exit condition: " << *Cmp << "\n");

    // Just a simplified way to find indvar
    PHINode *IndVar = nullptr;
    // We always want to get form like 'i < C' or 'i > C'
    // instead of 'C < i' or 'C > i'
    CmpInst::Predicate Pred;
    if(auto* PN = dyn_cast<PHINode>(Cmp->getOperand(0))) {
      IndVar = PN;
      Pred = Cmp->getPredicate();
    } else if(auto* PN = dyn_cast<PHINode>(Cmp->getOperand(1))) {
      IndVar = PN;
      Pred = Cmp->getInversePredicate();
    }
    if(!IndVar) continue;

    auto processAscending = [Pred,&Worklist](CmpInst *Cmp) {
      if(Pred != Cmp->getPredicate())
        // IndVar is on RHS
        Worklist.push_back(&Cmp->getOperandUse(0));
      else
        Worklist.push_back(&Cmp->getOperandUse(1));
    };

    auto processDescending = [L,&Worklist](PHINode *IndVar) {
      for(Use& OpUse : IndVar->incoming_values()){
        if(L->contains(IndVar->getIncomingBlock(OpUse))) continue;
        // Get the initial value
        Worklist.push_back(&OpUse);
        break;
      }
    };

    // See whether it's ascending or descending
    switch(Pred){
    case CmpInst::ICMP_UGT:
    case CmpInst::ICMP_UGE:
    case CmpInst::ICMP_SGT:
    case CmpInst::ICMP_SGE: {
      if(L->contains(TrueBB))
        // descending
        processDescending(IndVar);
      else
        // ascending
        processAscending(Cmp);
      break;
    }
    case CmpInst::ICMP_ULT:
    case CmpInst::ICMP_ULE:
    case CmpInst::ICMP_SLT:
    case CmpInst::ICMP_SLE: {
      if(L->contains(TrueBB))
        // ascending
        processAscending(Cmp);
      else
        // descending
        processDescending(IndVar);
      break;
    }
    default:
      continue;
    }
  }

  bool Changed = false;
  for(Use* U : Worklist) {
    Value* Val = U->get();
    User* Usr = U->getUser();
    LLVM_DEBUG(dbgs() << "Working on: " << *Val << " -> " << *Usr << "\n");
    auto* Ty = Val->getType();
    if(!Ty->isIntegerTy()) continue;

    Value *NewVal;
    if(const auto* ConstInt = dyn_cast<ConstantInt>(Val)){
      NewVal = ConstantInt::get(Ty,
                                ConstInt->getValue() * 2);
    }else{
      // Non constant, insert multiplication
      auto* Two = ConstantInt::get(Ty, 2);
      auto* Mul = BinaryOperator::Create(BinaryOperator::Mul,
                                         const_cast<Value*>(Val), Two);
      NewVal = cast<Value>(Mul);
      // We can't insert instruction before a PHINode
      if(auto* PN = dyn_cast<PHINode>(Usr)) {
        auto* InBB = PN->getIncomingBlock(*U);
        Mul->insertBefore(cast<Instruction>(InBB->getTerminator()));
      }else if(auto* I = dyn_cast<Instruction>(Usr))
        Mul->insertBefore(I);
      else
        continue;
    }
    Changed = true;
    Usr->replaceUsesOfWith(Val, NewVal);
  }

  return Changed;
}

namespace llvm {
FunctionPass* createExtraProteinLegacyPass() {
  return new ExtraProteinLegacyPass();
}
} // end namespace llvm
