#include <queue>          //bfs
#include <unordered_set>  //mark visited BBs

#include "llvm/IR/CFG.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Module.h"
#include "llvm/Pass.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/IPO/PassManagerBuilder.h"

#include "SimpleDataDependenceGraph.h"

using namespace llvm;

namespace {

class MyPass : public ModulePass {
public:
    static char ID;
    std::unordered_set<void *> st;
    MyPass() : ModulePass(ID) {}

    void getFunctionGraph(Function &F) {
        errs() << " + Function:" << F.getName() << '\n';
        if(F.empty()) {
            errs() << "   - Empty Function\n";
            return;
        }
        miner::SDDG sddg(&F);
        sddg.buildSDDG();
        sddg.flattenSDDG();
        sddg.dotify();
    }

    virtual bool runOnModule(Module &M) {
        errs() << "Module:" << M.getName() << '\n';
        for (auto iter = M.begin(); iter != M.end(); iter++) {
            Function &F = *iter;
            getFunctionGraph(F);    
        }
        return false;
    }
};
}  // namespace

char MyPass::ID = 0;

static void registerMyPass(const PassManagerBuilder &PMB, legacy::PassManagerBase &PM) {
    PM.add(new MyPass());
}

// works with "-O0" or no optimization options
static RegisterStandardPasses RegisterMyPass_OPT0(PassManagerBuilder::EP_EnabledOnOptLevel0, registerMyPass);

// works with "-O1", "-O2", ...
static RegisterStandardPasses RegisterMyPass_OPT(PassManagerBuilder::EP_ModuleOptimizerEarly, registerMyPass);
