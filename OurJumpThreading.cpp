#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/CFG.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"

#include <vector>

using namespace llvm;

namespace {

  const unsigned MaxWalkDepth = 4;

  struct OurJumpThreading : public PassInfoMixin<OurJumpThreading> {

    BranchInst *getConditionalBranch(BasicBlock *BB) {
      BranchInst *Branch = dyn_cast<BranchInst>(BB->getTerminator());
      if (Branch == nullptr || !Branch->isConditional()) {
        return nullptr;
      }
      return Branch;
    }

    bool getStrictForm(ICmpInst *Cmp, CmpInst::Predicate &Pred, APInt &C) {
      ConstantInt *RHS = dyn_cast<ConstantInt>(Cmp->getOperand(1));
      if (RHS == nullptr) {
        return false;
      }

      Pred = Cmp->getPredicate();
      C = RHS->getValue();

      switch (Pred) {
        case ICmpInst::ICMP_SGE:
          if (C.isMinSignedValue()) {
            return false;
          }
          Pred = ICmpInst::ICMP_SGT;
          C -= 1;
          break;
        case ICmpInst::ICMP_SLE:
          if (C.isMaxSignedValue()) {
            return false;
          }
          Pred = ICmpInst::ICMP_SLT;
          C += 1;
          break;
        case ICmpInst::ICMP_UGE:
          if (C.isMinValue()) {
            return false;
          }
          Pred = ICmpInst::ICMP_UGT;
          C -= 1;
          break;
        case ICmpInst::ICMP_ULE:
          if (C.isMaxValue()) {
            return false;
          }
          Pred = ICmpInst::ICMP_ULT;
          C += 1;
          break;
        default:
          break;
      }

      return true;
    }

    bool isEquivalentCondition(Value *Condition1, Value *Condition2) {
      if (Condition1 == Condition2) {
        return true;
      }

      ICmpInst *Cmp1 = dyn_cast<ICmpInst>(Condition1);
      ICmpInst *Cmp2 = dyn_cast<ICmpInst>(Condition2);
      if (Cmp1 == nullptr || Cmp2 == nullptr ||
          Cmp1->getOperand(0) != Cmp2->getOperand(0)) {
        return false;
      }

      if (Cmp1->getPredicate() == Cmp2->getPredicate() &&
          Cmp1->getOperand(1) == Cmp2->getOperand(1)) {
        return true;
      }

      CmpInst::Predicate Pred1, Pred2;
      APInt C1, C2;
      if (!getStrictForm(Cmp1, Pred1, C1) || !getStrictForm(Cmp2, Pred2, C2)) {
        return false;
      }

      return Pred1 == Pred2 && C1 == C2;
    }

    bool isDefinedInBlock(Value *V, BasicBlock *BB) {
      Instruction *I = dyn_cast<Instruction>(V);
      return I != nullptr && I->getParent() == BB;
    }

    bool isConditionStable(Value *Condition, BasicBlock *BB) {
      if (!isDefinedInBlock(Condition, BB)) {
        return true;
      }

      Instruction *I = dyn_cast<Instruction>(Condition);
      for (size_t Index = 0; Index < I->getNumOperands(); Index++) {
        if (isDefinedInBlock(I->getOperand(Index), BB)) {
          return false;
        }
      }

      return true;
    }

    bool isConditionKnown(Value *Condition, BasicBlock *Pred, BasicBlock *BB,
                          bool &KnownValue) {
      if (!isConditionStable(Condition, BB)) {
        return false;
      }

      BasicBlock *Child = BB;
      BasicBlock *Parent = Pred;

      for (unsigned Depth = 0; Depth < MaxWalkDepth; Depth++) {
        BranchInst *Branch = getConditionalBranch(Parent);

        if (Branch != nullptr &&
            isEquivalentCondition(Branch->getCondition(), Condition) &&
            isConditionStable(Branch->getCondition(), BB)) {
          bool OnTrueEdge = Branch->getSuccessor(0) == Child;
          bool OnFalseEdge = Branch->getSuccessor(1) == Child;

          if (OnTrueEdge != OnFalseEdge) {
            KnownValue = OnTrueEdge;
            return true;
          }
        }

        Child = Parent;
        Parent = Child->getUniquePredecessor();
        if (Parent == nullptr || Parent == BB) {
          return false;
        }
      }

      return false;
    }

    bool foldKnownBranch(BasicBlock *BB) {
      BranchInst *Branch = getConditionalBranch(BB);
      if (Branch == nullptr) {
        return false;
      }

      BasicBlock *TrueBB = Branch->getSuccessor(0);
      BasicBlock *FalseBB = Branch->getSuccessor(1);
      if (TrueBB == FalseBB) {
        return false;
      }

      bool SawPredecessor = false;
      bool AgreedValue = false;

      for (BasicBlock *Pred : predecessors(BB)) {
        bool KnownValue = false;
        if (!isConditionKnown(Branch->getCondition(), Pred, BB, KnownValue)) {
          return false;
        }

        if (SawPredecessor && KnownValue != AgreedValue) {
          return false;
        }

        AgreedValue = KnownValue;
        SawPredecessor = true;
      }

      if (!SawPredecessor) {
        return false;
      }

      BasicBlock *TakenBB = AgreedValue ? TrueBB : FalseBB;
      BasicBlock *NotTakenBB = AgreedValue ? FalseBB : TrueBB;

      NotTakenBB->removePredecessor(BB);
      Branch->eraseFromParent();
      BranchInst::Create(TakenBB, BB);

      return true;
    }

    bool holdsOnlyPhisAndCondition(BasicBlock *BB, BranchInst *Branch) {
      for (Instruction &I : *BB) {
        if (isa<PHINode>(&I) || &I == Branch) {
          continue;
        }

        if (&I == Branch->getCondition() && I.hasOneUse()) {
          continue;
        }

        return false;
      }

      return true;
    }

    void replaceUsesInBlock(Value *From, BasicBlock *InBB, Value *To) {
      std::vector<Use *> UsesToReplace;

      for (Use &U : From->uses()) {
        Instruction *UserInstruction = dyn_cast<Instruction>(U.getUser());
        if (UserInstruction != nullptr &&
            UserInstruction->getParent() == InBB) {
          UsesToReplace.push_back(&U);
        }
      }

      for (Use *U : UsesToReplace) {
        U->set(To);
      }
    }

    void renameSinglePhiEntries(BasicBlock *BB, BasicBlock *OldPred,
                                BasicBlock *NewPred) {
      for (PHINode &PN : BB->phis()) {
        int Index = PN.getBasicBlockIndex(OldPred);
        if (Index >= 0) {
          PN.setIncomingBlock(Index, NewPred);
        }
      }
    }

    void redirectEdges(BasicBlock *Pred, BasicBlock *OldBB,
                       BasicBlock *NewBB) {
      Instruction *Terminator = Pred->getTerminator();

      for (unsigned Index = 0; Index < Terminator->getNumSuccessors();
           Index++) {
        if (Terminator->getSuccessor(Index) == OldBB) {
          Terminator->setSuccessor(Index, NewBB);
        }
      }
    }

    bool threadMergeBlock(BasicBlock *BB) {
      BranchInst *Branch = getConditionalBranch(BB);
      if (Branch == nullptr || !holdsOnlyPhisAndCondition(BB, Branch)) {
        return false;
      }

      BasicBlock *TrueBB = Branch->getSuccessor(0);
      BasicBlock *FalseBB = Branch->getSuccessor(1);
      if (TrueBB == FalseBB || TrueBB == BB || FalseBB == BB) {
        return false;
      }

      if (TrueBB->getUniquePredecessor() != BB ||
          FalseBB->getUniquePredecessor() != BB) {
        return false;
      }

      BasicBlock *TruePred = nullptr;
      BasicBlock *FalsePred = nullptr;

      for (BasicBlock *Pred : predecessors(BB)) {
        bool KnownValue = false;
        if (Pred == BB ||
            !isConditionKnown(Branch->getCondition(), Pred, BB, KnownValue)) {
          return false;
        }

        if (KnownValue) {
          if (TruePred != nullptr) {
            return false;
          }
          TruePred = Pred;
        }
        else {
          if (FalsePred != nullptr) {
            return false;
          }
          FalsePred = Pred;
        }
      }

      if (TruePred == nullptr || FalsePred == nullptr) {
        return false;
      }

      for (PHINode &PN : BB->phis()) {
        for (User *U : PN.users()) {
          Instruction *UserInstruction = dyn_cast<Instruction>(U);
          if (UserInstruction == nullptr) {
            return false;
          }

          BasicBlock *UserBB = UserInstruction->getParent();
          if (UserBB != BB && UserBB != TrueBB && UserBB != FalseBB) {
            return false;
          }
        }
      }

      for (PHINode &PN : BB->phis()) {
        replaceUsesInBlock(&PN, TrueBB, PN.getIncomingValueForBlock(TruePred));
        replaceUsesInBlock(&PN, FalseBB,
                           PN.getIncomingValueForBlock(FalsePred));
      }

      renameSinglePhiEntries(TrueBB, BB, TruePred);
      renameSinglePhiEntries(FalseBB, BB, FalsePred);

      redirectEdges(TruePred, BB, TrueBB);
      redirectEdges(FalsePred, BB, FalseBB);

      Branch->eraseFromParent();
      new UnreachableInst(BB->getContext(), BB);

      return true;
    }

    bool runOnFunction(Function &F) {
      bool Changed = false;
      bool TryAgain = true;

      while (TryAgain) {
        TryAgain = false;

        for (BasicBlock &BB : F) {
          if (foldKnownBranch(&BB) || threadMergeBlock(&BB)) {
            TryAgain = true;
            break;
          }
        }

        if (TryAgain) {
          Changed = true;
          EliminateUnreachableBlocks(F);
        }
      }

      return Changed;
    }

    PreservedAnalyses run(Function &F, FunctionAnalysisManager &) {
      if (!runOnFunction(F)) {
        return PreservedAnalyses::all();
      }
      return PreservedAnalyses::none();
    }
  };
}

extern "C" LLVM_ATTRIBUTE_WEAK ::llvm::PassPluginLibraryInfo
llvmGetPassPluginInfo() {
  return {LLVM_PLUGIN_API_VERSION, "OurJumpThreading", LLVM_VERSION_STRING,
          [](PassBuilder &PB) {
            PB.registerPipelineParsingCallback(
                [](StringRef Name, FunctionPassManager &FPM,
                   ArrayRef<PassBuilder::PipelineElement>) {
                  if (Name == "our-jump-threading") {
                    FPM.addPass(OurJumpThreading());
                    return true;
                  }
                  return false;
                });
          }};
}
