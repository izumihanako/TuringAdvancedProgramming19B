#include <fstream>
#include <list>
#include <stack>
#include <queue>
#include <utility>
#include <vector>
#include "llvm/IR/CFG.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/ModuleSlotTracker.h"
#include "llvm/IR/ValueSymbolTable.h"
#include "llvm/IR/Instructions.h"
#include "SimpleDataDependenceGraph.h"

namespace miner
{

using std::endl;
using std::list;
using std::ofstream;
using std::pair;
using std::stack;
using std::queue;

// TRUE for successful insertion; FALSE indicates an existing pair.
bool SDDG::share(Instruction *fst, Instruction *snd)
{
    pair<Instruction *, Instruction *> p1(fst, snd);
    pair<Instruction *, Instruction *> p2(snd, fst);
    if (mShares.find(p1) == mShares.end() && mShares.find(p2) == mShares.end())
    {
        mShares.insert(p1);
        return true;
    }
    return false;
}


bool SDDGNode::addSuccessor(SDDGNode *dst) {
    mSuccessors.push_back(dst);
}
bool SDDGNode::addPredecessor(SDDGNode *dst) {
    mPredecessors.push_back(dst);
}

inline Instruction* SDDGNode::getInst() {
    return mInst;
}
vector<SDDGNode *>& SDDGNode::getSuccessors() {
    return mSuccessors;
}



namespace
{
void dotifyToFile(map<Instruction *, SDDGNode *> &nodes, set<pair<Instruction *, Instruction *>> &shares, string &file, bool showShareRelations)
{
    ofstream fos;
    fos.open(file);
    fos << "digraph {\n"
        << endl;
    for (auto iter = nodes.begin(); iter != nodes.end(); iter++)
    {
        Instruction *inst = iter->first;
        fos << "Inst" << (void *)inst << "[align = left, shape = box, label = \"";
        string label;
        raw_string_ostream rso(label);
        inst->print(rso);
        string::size_type pos(0);
        while ((pos = label.find('"', pos)) != string::npos)
        {
            label.replace(pos, 1, "'");
        }
        fos << label << "\"];" << endl;
    }
    fos << endl;
    for (auto iter = nodes.begin(); iter != nodes.end(); iter++)
    {
        Instruction *src = iter->first;
        SDDGNode *node = iter->second;
        vector<SDDGNode *> successors = node->getSuccessors();
        for (SDDGNode *succ : successors)
        {
            Instruction *dst = succ->getInst();
            fos << "Inst" << (void *)src << " -> Inst" << (void *)dst << " [dir=back];" << endl;
        }
    }
    fos << endl;
    if (showShareRelations || nodes.empty())
    {
        if (nodes.empty())
        {
            for (pair<Instruction *, Instruction *> pp : shares)
            {
                Instruction *inst1[2] = {pp.first, pp.second};
                Instruction *inst;
                for (int idx = 0; idx < 2; idx++)
                {
                    inst = inst1[idx];
                    fos << "Inst" << (void *)inst << "[align = left, shape = box, label = \"";
                    string label;
                    raw_string_ostream rso(label);
                    inst->print(rso);
                    string::size_type pos(0);
                    while ((pos = label.find('"', pos)) != string::npos)
                    {
                        label.replace(pos, 1, "'");
                    }
                    fos << label << "\"];" << endl;
                }
            }
            fos << endl;
        }
        for (pair<Instruction *, Instruction *> pp : shares)
        {
            Instruction *inst1 = pp.first;
            Instruction *inst2 = pp.second;
            fos << "Inst" << (void *)inst1 << " -> Inst" << (void *)inst2 << " [dir=none, color=red, style=dashed];" << endl;
        }
    }
    fos << "}" << endl;
    fos.close();
}
} // namespace

void SDDG::dotify(bool showShareRelations)
{
    std::string file = mFunc->getName().str() + ".dot";
    dotifyToFile(mNodes, mShares, file, showShareRelations);
    if (!mInterestingNodes.empty())
    {
        file = mFunc->getName().str() + ".flat.dot";
        dotifyToFile(mInterestingNodes, mShares, file, showShareRelations);
    }
}

namespace dfa
{
class Definition
{
    map<Value *, Instruction *> mDef;

public:
    Definition() = default;
    ~Definition()
    {
        mDef.clear();
    }
    void define(Value *var, Instruction *inst)
    {
        mDef[var] = inst;
    }
    map<Value *, Instruction *> &getDef()
    {
        return mDef;
    }
    Instruction *getDef(Value *var)
    {
        auto iter = mDef.find(var);
        if (iter != mDef.end())
        {
            return iter->second;
        }
        return nullptr;
    }
    // 输出当前基本块的Definition信息，主要用于测试、检查代码正确性
    void dump()
    {
        errs() << "Definitions: \n";
        for (auto iter = mDef.begin(); iter != mDef.end(); iter++)
        {
            errs() << *(iter->first) << "  <-  " << *(iter->second) << "\n";
        }
        errs() << "\n";
    }
};

///////////////////////////
// Use的数据结构(类)应该如何定义，请自行设计完成
///////////////////////////
class Use{
    map<Value *, set<Instruction *> *> mUse;

public:
    Use() = default;
    ~Use() {
        for(auto it = mUse.begin(); it != mUse.end(); ++it) {
            set<Instruction *> *i = it->second;
            if(i != nullptr) 
                delete i;
        }
        mUse.clear();
    }
    map<Value *, set<Instruction *> *>& getmUse() {
        return mUse;
    }
    void createUse(Value *val, Instruction *inst) {
        if(!mUse.count(val)) {
            mUse[val] = new set<Instruction*>;
        }
        mUse[val] -> insert(inst);
    }
    void deleteUse(Value *val, Instruction *inst) {
        if(!mUse.count(val)) {
            return;
        }
        if(mUse[val] -> count(inst))
            mUse[val] -> erase(inst);
    }
};

// 以下两个以“Share”开始的类仅用于计算数据共享关系
class ShareDefinition
{
    map<Value *, set<Value *> *> mShareDef;

public:
    ShareDefinition() = default;
    ~ShareDefinition()
    {
        for (auto iter = mShareDef.begin(); iter != mShareDef.end(); iter++)
        {
            set<Value *> *sdefs = iter->second;
            if (sdefs)
                delete sdefs;
        }
        mShareDef.clear();
    }
    map<Value *, set<Value *> *> &getShareDefs()
    {
        return mShareDef;
    }
    set<Value *> *getShareDef(Value *var)
    {
        auto iter = mShareDef.find(var);
        if (iter != mShareDef.end())
        {
            return iter->second;
        }
        return nullptr;
    }
    void shareDefine(Value *var, Value *def)
    {
        auto iter = mShareDef.find(var);
        set<Value *> *vSet;
        set<Value *> *dSet = getShareDef(def);
        if (iter != mShareDef.end())
        {
            vSet = iter->second;
            vSet->clear(); // re-definition overrides all old ones.
        }
        else
        {
            vSet = new set<Value *>;
            mShareDef[var] = vSet;
        }
        if (dSet)
        {
            for (Value *ds : *dSet)
            {
                vSet->insert(ds);
            }
        }
        else
        {
            vSet->insert(def);
        }
    }
    void dump()
    {
        errs() << "ShareDefinition:\n";
        for (auto iter = mShareDef.begin(); iter != mShareDef.end(); iter++)
        {
            Value *key = iter->first;
            set<Value *> *val = iter->second;
            errs() << " Define: " << *key << "  as \n";
            for (Value *vv : *val)
            {
                errs() << "  ++ " << *vv << "\n";
            }
        }
    }
};
class ShareUse
{
    map<Value *, set<Instruction *> *> mShareUse;
    set<Instruction *> *findOrCreateSharedUse(Value *var)
    {
        auto iter = mShareUse.find(var);
        if (iter != mShareUse.end())
            return iter->second;
        set<Instruction *> *sSet = new set<Instruction *>;
        mShareUse[var] = sSet;
        return sSet;
    }

public:
    ShareUse() = default;
    ~ShareUse()
    {
        for (auto iter = mShareUse.begin(); iter != mShareUse.end(); iter++)
        {
            set<Instruction *> *suses = iter->second;
            if (suses)
                delete suses;
        }
        mShareUse.clear();
    }
    map<Value *, set<Instruction *> *> &getShareUses()
    {
        return mShareUse;
    }
    set<Instruction *> *getShareUse(Value *var)
    {
        auto iter = mShareUse.find(var);
        if (iter != mShareUse.end())
            return iter->second;
        return nullptr;
    }
    void shareUse(Value *var, Instruction *inst, ShareDefinition *sdef)
    {
        set<Value *> *vDef = sdef->getShareDef(var);
        if (vDef)
        {
            for (Value *vd : *vDef)
            {
                set<Instruction *> *vdUse = findOrCreateSharedUse(vd);
                vdUse->insert(inst);
            }
        }
        // keep var:USE(inst) as well for a simple merge in DFA.
        set<Instruction *> *vUse = findOrCreateSharedUse(var);
        vUse->insert(inst);
    }
    void dump()
    {
        errs() << "ShareUse:\n";
        for (auto iter = mShareUse.begin(); iter != mShareUse.end(); iter++)
        {
            Value *key = iter->first;
            set<Instruction *> *val = iter->second;
            errs() << " Use: " << *key << "  at \n";
            for (Instruction *vv : *val)
            {
                errs() << "  ++ " << *vv << "\n";
            }
        }
    }
};

template <typename TK, typename TV>
TV *findOrCreate(map<TK *, TV *> &ian, TK *bb)
{
    auto iter = ian.find(bb);
    TV *agr = nullptr;
    if (iter == ian.end())
    {
        agr = new TV;
        ian[bb] = agr;
    }
    else
    {
        agr = iter->second;
    }
    return agr;
}

// TRUE for changes in 'to'; FALSE for no changes in 'to'.
template <typename TSE>
bool mergeTwoMaps(map<Value *, set<TSE *> *> &to, map<Value *, set<TSE *> *> &from)
{
    bool changed = false;
    for(auto it = from->begin(); it != from->end(); ++it) {
        Value *fVal = it->first;
        set<TSE *> *fSet = it->second;
        if(fSet->empty()) continue;
        if(!to.count(fVal)) { // new key value
            changed = true;
            to[fVal] = new set<TSE *>(fSet);
        }
        else { // key already exist
            for(TSE* fElem : (*fSet)) {
                if(!to[fVal]->count(fElem)) {
                    changed = true;
                    to[fVal]->insert(fElem);
                }
            }
        }
    }
    //////////////////////////////
    //  请在此实现map的合并操作，该函数可用于迭代数据流分析中的“并集”操作，
    //  在计算数据依赖关系、数据共享关系时均被大量使用。
    //  对模板尚不熟悉的同学，可以实现多个不同的函数，用于不同的目的。
    //  该函数的返回值为true时，表示合并操作之后，'to'相比合并之前发生了改变，
    //  false表示未发生改变。
    //////////////////////////////
    return changed;
}

template<class T>
bool mergeTwoSet(set<T> *from, set<T> *to) {
    bool changed = false;
    for(auto it : (*from)) {
        if(!to->count(it)) {
            changed = true;
            to->insert(it);
        }
    }
    return changed;
}

} // namespace dfa

void SDDG::buildSDDG()
{
    map<BasicBlock *, dfa::Definition *> sDfaDefs;
    map<BasicBlock *, dfa::Use *> sDfaUses;
    map<BasicBlock *, map<Value*, set<Instruction*>* >* > bbGen, bbIn, bbOut;
    ////////////////////////////
    // 在这里实现构建数据依赖关系的代码，可按照如下基本步骤进行：
    // 1. 初始化每个基本块的gen和kill、definition和use，并建立基本块内部的数据依赖关系
    // 2. 根据迭代数据流算法，计算IN和OUT
    // 3. 根据每个基本块的IN和use信息，更新数据依赖图
    ////////////////////////////

    //1. scan for initializing Def of each BB
    for(auto bbIter = mFunc -> begin(); bbIter != mFunc -> end(); bbIter++) {
        BasicBlock &bb = *bbIter;
        bbGen[&bb] = new map<Value*, set<Instruction*>* > ;
        bbIn[&bb] = new map<Value*, set<Instruction* >* >;
        bbOut[&bb] = new map<Value*, set<Instruction* >* > ;
        dfa::Definition *bbDef = dfa::findOrCreate(sDfaDefs, &bb) ; // bbDef = sDfaDefs[&bb]
        dfa::Use *bbUse = dfa::findOrCreate(sDfaUses, &bb) ;
        for(auto instIter = bb.begin() ; instIter != bb.end() ; instIter++ ) {
            //
            Instruction *curInst = dyn_cast<Instruction>(instIter);
            auto curInstOpcode = curInst->getOpcode();
            if(curInstOpcode == Instruction::Alloca) {
                continue ;
            }
            mNodes[curInst] = new SDDGNode( curInst ) ;
            if(curInstOpcode == Instruction::Store) { //store src dest
                Value *fstOp = curInst->getOperand( 0 ) ;
                Value *sndOp = curInst->getOperand( 1 ) ;
                if( !bbDef->getDef( fstOp ) ){
                    bbUse->createUse( fstOp , curInst ) ;
                } else { // 能够直接获取块内定义，加边
                    mNodes[curInst]->addPredecessor( mNodes[bbDef->getDef( fstOp )] ) ;
                    mNodes[ bbDef->getDef( fstOp ) ]->addSuccessor( mNodes[curInst] ) ;
                }
                bbDef->define( sndOp , curInst ) ;
            }
            else if(curInstOpcode == Instruction::Call ) {
                mInterestingNodes[ curInst ] = new SDDGNode( curInst ) ;
                if( !curInst->use_empty() ){ // 被用过，是定义，是call
                    Value *lvalue = dyn_cast<Value>(curInst) ;
                    bbDef->define( lvalue , curInst ) ;
                }
                unsigned int nOprands = curInst->getNumOperands() ;
                --nOprands ;//最后一个返回的operand是函数定义，不要
                for( int idx = 0 ; idx < nOprands ; idx ++ ){
                    Value *op = curInst->getOperand( idx ) ;
                    if( !bbDef->getDef( op ) ){
                        bbUse->createUse( op , curInst ) ;
                    } else { // 能够直接获取块内定义，加边
                        mNodes[curInst]->addPredecessor( mNodes[bbDef->getDef( op )] ) ;
                        mNodes[ bbDef->getDef( op ) ]->addSuccessor( mNodes[curInst] ) ;
                    }
                }
            } else if(curInstOpcode == Instruction::Br || curInstOpcode == Instruction::Ret ) {
                if( curInstOpcode == Instruction::Ret ){
                    mInterestingNodes[ curInst ] = new SDDGNode( curInst ) ;
                }
                int nOprands = curInst->getNumOperands() ;
                for( int idx = 0 ; idx < nOprands ; idx ++ ) {
                    Value *op = curInst->getOperand( idx ) ;
                    if( !bbDef->getDef( op ) ) {
                        bbUse->createUse( op , curInst ) ;
                    } else { // 能够直接获取块内定义，加边
                        mNodes[curInst]->addPredecessor( mNodes[bbDef->getDef( op )] ) ;
                        mNodes[ bbDef->getDef( op ) ]->addSuccessor( mNodes[curInst] ) ;
                    }
                }
            } else {
                if( !curInst->use_empty() ) {//被使用过，是一个定义
                    Value *lvalue = dyn_cast<Value>(curInst) ;
                    bbDef->define( lvalue , curInst ) ;
                } int nOprands = curInst->getNumOperands() ;
                for( int idx = 0 ; idx < nOprands ; idx ++ ) {
                    Value *op = curInst->getOperand( idx ) ;
                    if( !bbDef->getDef( op ) ) {
                        bbUse->createUse( op , curInst ) ;
                    } else { // 能够直接获取块内定义，加边
                        mNodes[curInst]->addPredecessor( mNodes[bbDef->getDef( op )] ) ;
                        mNodes[ bbDef->getDef( op ) ]->addSuccessor( mNodes[curInst] ) ;
                    }
                }
            }
        }
    }

    for(auto bbIter = mFunc -> begin(); bbIter != mFunc -> end(); bbIter++) {
        BasicBlock &bb = *bbIter;
        dfa::Definition *bbDef = dfa::findOrCreate(sDfaDefs, &bb) ; // bbDef = sDfaDefs[&bb]
        dfa::Use *bbUse = dfa::findOrCreate(sDfaUses, &bb) ;
        for( auto defIter : bbDef->getDef() ) (*bbGen[&bb])[defIter.first]-> insert( defIter.second ) ; // map<BasicBlock *, map<Value*, set<Instruction*>* >* >
        bbOut[&bb] = bbGen[&bb] ;
    }

    queue<BasicBlock *> bbQueue;
    set<BasicBlock *> bbQueueVisit;
    map< Value* , Instruction* > tmpIn ;
    bool changed = 1;
    while(changed == 1) {
        while(!bbQueue.empty()) bbQueue.pop();
        bbQueueVisit.clear();
        bbQueue.push(&(mFunc->getEntryBlock()));
        //start BFS
        while(!bbQueue.empty()) {
            BasicBlock *curBB = bbQueue.front();
            bbQueueVisit.insert(curBB);
            bbQueue.pop();
            
            for( auto preBBit = pred_begin(curBB) , endBBit = pred_end(curBB) ; preBBit != endBBit ; ++preBBit ){
                BasicBlock* preBB = *preBBit;
                dfa::mergeTwoMaps( *bbIn[curBB] , *bbOut[preBB]);// 合并前驱的Out到当前的In
                map< Value* , set<Instruction*>* > tmpOut( *(bbOut[preBB]) );
                //检查Out(pre)的所有 Instruction 是否被重定义
                for(auto tmpDef: *bbOut[preBB] ){ // 寻找是否被重定义，如果有，则被kill
                    Value *lvalue = tmpDef.first ;
                    if(sDfaDefs[curBB] -> getDef( lvalue ) != nullptr) {
                        tmpOut.erase( tmpDef.first );
                    }
                }
                changed |= dfa::mergeTwoMaps( *bbOut[curBB] , tmpOut);
            }                
            // wys code ^

            //bbIn[curBB];
            
            auto termInst = curBB->getTerminator();
            int numSuccessor = termInst->getNumSuccessors();
            for(int idxn = 0; idxn < numSuccessor; ++idxn) {
                BasicBlock *nextBB = termInst->getSuccessor(idxn);
                if(!bbQueueVisit.count(nextBB)) 
                    bbQueue.push(nextBB);
            }
        }
    }

    for( auto bbIter = mFunc -> begin() ; bbIter != mFunc->end() ; bbIter++ ){
        BasicBlock *bb = dyn_cast<BasicBlock>(bbIter);
        auto curUse = dfa::findOrCreate(sDfaUses, bb)->getmUse() ;// map& : value* to set<inst*>*
        auto curIn = bbIn[bb] ; // set of in Inst
        for( auto valUse : curUse ){ // get every  val-set pair
            
            for( auto useInst : ( *valUse.second ) ){ // get Inst
                
            }
            
        }
    }
    // 以下是创建数据共享关系的代码，其中有一处需要同学们自行处理，所依赖的mergeTwoMaps函数需要
    // 同学们去实现。
    /////////////////////////////////////////////////////////////////////////////////////////
    // Below we compute the DataShare relations using the following forwarding DFA algorithm:
    // IN(ShareDef : B) = \/[P of B's predecessor](OUT(ShareDef : P))
    // OUT(ShareDef : B) = (IN(ShareDef : B) - Def(B)) \/ ShareDef(B)
    // IN(ShareUse : B) = \/[P of B's predecessor](OUT(ShareUse : P))
    // OUT(ShareUse : B) =  (IN(ShareUse : B) - Def(B)) \/ ShareUse(B)
    // Be careful that "\/" may not a simple merge, but an update with particular operations.
    map<BasicBlock *, dfa::ShareDefinition *> sDefIN, sDefOUT;
    map<BasicBlock *, dfa::ShareUse *> sUseIN, sUseOUT;
    map<BasicBlock *, dfa::ShareDefinition *> sDfaShareDefs;
    map<BasicBlock *, dfa::ShareUse *> sDfaShareUses;
    // 1. scan for initializing ShareDef and ShareUse of each BB
    for (auto bbIter = mFunc->begin(); bbIter != mFunc->end(); bbIter++)
    {
        BasicBlock &bb = *bbIter;
        dfa::ShareDefinition *bbSDef = dfa::findOrCreate(sDfaShareDefs, &bb);
        dfa::ShareUse *bbSUse = dfa::findOrCreate(sDfaShareUses, &bb);
        for (auto instIter = bb.begin(); instIter != bb.end(); instIter++)
        {
            Instruction *inst = dyn_cast<Instruction>(instIter);
            // errs() << "Analyze Inst: " << *inst << "  **#OP: " << inst->getNumOperands() << "\n";
            if (Instruction::Alloca == inst->getOpcode())
            {
                continue;
            }
            else if (Instruction::Store == inst->getOpcode())
            {
                Value *fstOp = inst->getOperand(0);
                Value *sndOp = inst->getOperand(1);
                if (isa<Argument>(fstOp) || isa<Instruction>(fstOp)) // e.g., fstOp is "x = call f()"
                {
                    bbSDef->shareDefine(sndOp, fstOp);
                }
                else // should we consider usages of the same constant as a DataShare?
                {
                    bbSDef->shareDefine(sndOp, inst);
                }
                // no need to build ShareUse as we only care uses in interesting nodes (call, ret)
            }
            else if (Instruction::Call == inst->getOpcode() || Instruction::Ret == inst->getOpcode())
            {
                unsigned int nOprands = inst->getNumOperands();
                ////////////////////////////
                // 此处未考虑Intrinsic函数的处理，请自行添加。
                ////////////////////////////
                if (Instruction::Call == inst->getOpcode() && !inst->getType()->isVoidTy())
                {
                    bbSDef->shareDefine(inst, inst);
                    --nOprands;
                }
                // build DataShare relations for all incoming uses that can be seen within this BB
                for (unsigned int idxo = 0; idxo < nOprands; idxo++)
                {
                    Value *op = inst->getOperand(idxo);
                    set<Instruction *> *opSUses = bbSUse->getShareUse(op);
                    if (opSUses)
                    {
                        for (Instruction *opUse : *opSUses)
                        {
                            share(inst, opUse);
                        }
                    }
                    // as we do not obtain the ShareDef's uses in getShareUse(), we need a further step here.
                    set<Value *> *opSDefs = bbSDef->getShareDef(op);
                    if (opSDefs)
                    {
                        for (Value *opDef : *opSDefs)
                        {
                            opSUses = bbSUse->getShareUse(opDef);
                            if (opSUses)
                            {
                                for (Instruction *opUse : *opSUses)
                                {
                                    share(inst, opUse);
                                }
                            }
                        }
                    }
                }
                // update ShareUse
                for (unsigned int idxo = 0; idxo < nOprands; idxo++)
                {
                    Value *op = inst->getOperand(idxo);
                    if (isa<Argument>(op) || isa<Instruction>(op))
                    {
                        bbSUse->shareUse(op, inst, bbSDef);
                    }
                }
            }
            else if (Instruction::Br == inst->getOpcode())
            {
                // skip. do nothing for 'br'
            }
            else
            {
                // for other instructions, 'operand's exist only at RHS??
                // ShareUse does nothing, but ShareDef may be updated
                if (inst->use_empty()) // not a definition
                {
                    continue;
                }
                for (unsigned int idxo = 0; idxo < inst->getNumOperands(); idxo++)
                {
                    Value *op = inst->getOperand(idxo);
                    if (isa<Argument>(op) || isa<Instruction>(op))
                    {
                        bbSDef->shareDefine(inst, op);
                    }
                }
            }
        }
        // errs() << "\n#### BB" << &bb << "\n";
        // bbSDef->dump();
        // bbSUse->dump();
    }
    // 2. iteratively compute IN/OUT for ShareDef and ShareUse
    changed = true;
    int round = 1;
    while (changed)
    {
        round++;
        changed = false;
        for (auto bbIter = mFunc->begin(); bbIter != mFunc->end(); bbIter++)
        {
            BasicBlock &bb = *bbIter;
            dfa::ShareDefinition *bbSDefIN = dfa::findOrCreate(sDefIN, &bb);
            dfa::ShareDefinition *bbSDefOUT = dfa::findOrCreate(sDefOUT, &bb);
            dfa::ShareUse *bbSUseIN = dfa::findOrCreate(sUseIN, &bb);
            dfa::ShareUse *bbSUseOUT = dfa::findOrCreate(sUseOUT, &bb);
            dfa::ShareDefinition tmpSDefIN, tmpSDefOUT;
            dfa::ShareUse tmpSUseIN, tmpSUseOUT;
            // 2.1 merge OUT to IN
            for (auto predIter = bb.user_begin(); predIter != bb.user_end(); predIter++)
            {
                User *user = *predIter;
                Instruction *terminator = dyn_cast<Instruction>(user);
                BasicBlock *predBB = terminator->getParent();
                // 2.1.1 merge OUT(ShareDef) to tmpIN
                dfa::ShareDefinition *predSDef = dfa::findOrCreate(sDefOUT, predBB);
                dfa::mergeTwoMaps(tmpSDefIN.getShareDefs(), predSDef->getShareDefs());
                // 2.1.2 merge OUT(ShareUse) to tmpIN
                dfa::ShareUse *predSUse = dfa::findOrCreate(sUseOUT, predBB);
                dfa::mergeTwoMaps(tmpSUseIN.getShareUses(), predSUse->getShareUses());
            }
            // 2.1.3 merge OUTs (in tmpIN) to IN(ShareDef)
            changed = dfa::mergeTwoMaps(bbSDefIN->getShareDefs(), tmpSDefIN.getShareDefs()) | changed;
            // 2.1.4 merge OUTs (in tmpIN) to IN(ShareUse)
            changed = dfa::mergeTwoMaps(bbSUseIN->getShareUses(), tmpSUseIN.getShareUses()) | changed;
            // Below for debug output use.
            // errs() << " After merging OUT for BB" << &bb << "\n";
            // errs() << " ~~~~ IN : ShareDef ~~~~\n";
            // bbSDefIN->dump();
            // errs() << "\n ~~~~ IN : ShareUse ~~~~\n";
            // bbSUseIN->dump();
            // 2.2 compute OUT
            // 2.2.1 ShareDef: tmpOUT = IN(ShareDef : B) - Def(B) [rm all Defs in IN if re-defined]
            dfa::Definition *bbDef = sDfaDefs[&bb];
            map<Value *, Instruction *> &bbDefs = bbDef->getDef();
            map<Value *, set<Value *> *> &bbSDefIns = bbSDefIN->getShareDefs();
            map<Value *, set<Value *> *> &bbSDefTmpOUT = tmpSDefOUT.getShareDefs();
            for (auto iter = bbSDefIns.begin(); iter != bbSDefIns.end(); iter++)
            {
                Value *key = iter->first;
                auto miter = bbDefs.find(key);
                // copy from IN to tmpOUT only when not defined in current BB
                if (miter == bbDefs.end())
                {
                    set<Value *> *alreadyInIN = iter->second;
                    set<Value *> *newTmpSet = new set<Value *>;
                    for (Value *vv : *alreadyInIN)
                    {
                        newTmpSet->insert(vv);
                    }
                    bbSDefTmpOUT[key] = newTmpSet;
                }
            }
            // 2.2.2 ShareDef: tmpOUT = tmpOUT \/ ShareDef(B)
            dfa::ShareDefinition *bbShareDef = sDfaShareDefs[&bb];
            dfa::mergeTwoMaps(tmpSDefOUT.getShareDefs(), bbShareDef->getShareDefs());
            // 2.2.3 ShareDef: merge tmpOUT to OUT(ShareDef)
            changed = dfa::mergeTwoMaps(bbSDefOUT->getShareDefs(), tmpSDefOUT.getShareDefs()) | changed;
            // 2.2.4 ShareUse: tmpOUT = IN(ShareUse : B) - Def(B)
            map<Value *, set<Instruction *> *> &bbSUseIns = bbSUseIN->getShareUses();
            map<Value *, set<Instruction *> *> &bbSUseTmpOUT = tmpSUseOUT.getShareUses();
            for (auto iter = bbSUseIns.begin(); iter != bbSUseIns.end(); iter++)
            {
                Value *key = iter->first;
                auto miter = bbDefs.find(key);
                // copy from IN to tmpOUT only when not defined in current BB
                if (miter == bbDefs.end())
                {
                    set<Instruction *> *alreadyInIN = iter->second;
                    set<Instruction *> *newTmpSet = new set<Instruction *>;
                    for (Instruction *vv : *alreadyInIN)
                    {
                        newTmpSet->insert(vv);
                    }
                    bbSUseTmpOUT[key] = newTmpSet;
                }
            }
            // 2.2.5 ShareUse: tmpOUT = tmpOUT \/ ShareUse(B)
            // // if we allow a:USE and b:USE (a depends on b) in ShareUse, we can simply merge them.
            dfa::mergeTwoMaps(bbSUseTmpOUT, sDfaShareUses[&bb]->getShareUses());
            // 2.2.6 SahreUse: merge tmpOUT to OUT(ShareUse)
            changed = dfa::mergeTwoMaps(bbSUseOUT->getShareUses(), bbSUseTmpOUT) | changed;
            // errs() << "\n After computing OUT for BB" << &bb << "\n";
            // errs() << " ~~~~ OUT : ShareDef ~~~~\n";
            // bbSDefOUT->dump();
            // errs() << "\n ~~~~ OUT : ShareUse ~~~~\n";
            // bbSUseOUT->dump();
        }
    }
    // 3. update DataShare with IN(ShareUse : B) and ShareUse(B)
    for (auto bbIter = mFunc->begin(); bbIter != mFunc->end(); bbIter++)
    {
        BasicBlock &bb = *bbIter;
        dfa::ShareDefinition *bbSDefIN = dfa::findOrCreate(sDefIN, &bb);
        dfa::ShareUse *bbSUseIN = dfa::findOrCreate(sUseIN, &bb);
        dfa::ShareUse *bbSUse = sDfaShareUses[&bb];
        map<Value *, set<Instruction *> *> &bbSuses = bbSUse->getShareUses();
        for (auto iter = bbSuses.begin(); iter != bbSuses.end(); iter++)
        {
            Value *key = iter->first;
            set<Instruction *> *suseSet = iter->second;
            set<Value *> *sdefSet = bbSDefIN->getShareDef(key);
            if (sdefSet)
            {
                for (Value *dv : *sdefSet)
                {
                    set<Instruction *> *duseSet = bbSUseIN->getShareUse(dv);
                    if (duseSet)
                    {
                        for (Instruction *di : *duseSet)
                        {
                            for (Instruction *ds : *suseSet)
                            {
                                share(ds, di);
                            }
                        }
                    }
                }
            }
        }
    }
    ////////////////////////////
    // 清理数据结构，主要是本函数内的各种map之类的数据结构中保存的新分配的内存空间（通过new分配）
    // 需要通过delete释放掉。
    ////////////////////////////
    for(auto iter = mFunc->begin(); iter != mFunc->end(); ++iter) {
        BasicBlock *curBB = dyn_cast<BasicBlock>(iter);
        delete bbIn[curBB];
        delete bbOut[curBB];
        delete bbGen[curBB];
    }
}

void SDDG::flattenSDDG() {
    queue <SDDGNode*> Q;
    set <SDDGNode*> vis;
    for(auto tmp: mNodes) { // tmp: 遍历 mNodes 中的所有 ret 和 call
        if(tmp.first -> getOpcode() != Instruction::Call && tmp.first -> getOpcode() != Instruction::Ret) continue;
        while(!Q.empty()) Q.pop();
        vis.clear();
        Q.push(tmp.second);
        while(!Q.empty()) { // BFS
            SDDGNode* u = Q.front();
            Q.pop();
            vis.insert(u);
            if(u -> getInst() -> getOpcode() == Instruction::Call || u -> getInst() -> getOpcode() == Instruction::Ret) {
                // 如果可行，addedge(Source, u)
                mInterestingNodes[tmp.first] -> addSuccessor(mInterestingNodes[u -> getInst()]); 
                mInterestingNodes[u -> getInst()] -> addPredecessor(mInterestingNodes[tmp.first]); 
                continue;
            }
            vector <SDDGNode*> V = u -> getSuccessors();
            for(auto v: V) {
                if(vis.find(v) != vis.end()) Q.push(v);
            }
        }
    }
}

} // namespace miner