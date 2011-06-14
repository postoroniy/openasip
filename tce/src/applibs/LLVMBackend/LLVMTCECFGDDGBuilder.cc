/*
    Copyright (c) 2002-2010 Tampere University of Technology.

    This file is part of TTA-Based Codesign Environment (TCE).

    Permission is hereby granted, free of charge, to any person obtaining a
    copy of this software and associated documentation files (the "Software"),
    to deal in the Software without restriction, including without limitation
    the rights to use, copy, modify, merge, publish, distribute, sublicense,
    and/or sell copies of the Software, and to permit persons to whom the
    Software is furnished to do so, subject to the following conditions:

    The above copyright notice and this permission notice shall be included in
    all copies or substantial portions of the Software.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
    THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
    FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
    DEALINGS IN THE SOFTWARE.
 */
/**
 * @file LLVMTCECFGDDGBuilder.hh
 *
 * This builder builds a CFG and DDG from the new LLVM TTA backend format.
 *
 * @author Heikki Kultala 2011
 * @note reting: red
 */

#ifdef NDEBUG
#undef NDEBUG
#endif

#include "LLVMTCECFGDDGBuilder.hh"
#include "ControlFlowGraph.hh"
#include "Procedure.hh"
#include "AddressSpace.hh"
#include "ControlUnit.hh"
#include "DataDependenceGraph.hh"
#include "TerminalBasicBlockReference.hh"
#include "TerminalSymbolReference.hh"

#include "PreOptimizer.hh"
#include "BBSchedulerController.hh"
#include "CycleLookBackSoftwareBypasser.hh"
#include "CopyingDelaySlotFiller.hh"

#include "InstructionReferenceManager.hh"

#include "RegisterCopyAdder.hh"
#include "LLVMTCECmdLineOptions.hh"

#include "FunctionUnit.hh"
#include "HWOperation.hh"
#include "FUPort.hh"

#include <llvm/ADT/SmallString.h>
#include <llvm/MC/MCContext.h>

//#define WRITE_DDG_DOTS
//#define WRITE_CFG_DOTS

namespace llvm {

char LLVMTCECFGDDGBuilder::ID = -1;


LLVMTCECFGDDGBuilder::LLVMTCECFGDDGBuilder(
    const llvm::TargetMachine& tm, TTAMachine::Machine* mach, 
    InterPassData& ipd, bool functionAtATime, bool modifyMF) :
    LLVMTCEBuilder(tm, mach, ID), ipData_(&ipd), ddgBuilder_(ipd),
    functionAtATime_(functionAtATime), modifyMF_(modifyMF) {
    RegisterCopyAdder::findTempRegisters(*mach, ipd);

    if (functionAtATime_) {
        umach_ = new UniversalMachine();        
        const TTAMachine::Machine::FunctionUnitNavigator fuNav =
            mach->functionUnitNavigator();

        // the supported operation set
        for (int i = 0;i < fuNav.count(); i++) {
            const TTAMachine::FunctionUnit& fu = *fuNav.item(i);
            for (int o = 0; o < fu.operationCount(); o++) {
                opset_.insert(
                    StringTools::stringToLower(fu.operation(o)->name()));
            }
        }

    } // otherwise the umach_ will be used from the Program instance
}

bool
LLVMTCECFGDDGBuilder::writeMachineFunction(MachineFunction& mf) {

    if (tm_ == NULL)
        tm_ = &mf.getTarget();

    if (!functionAtATime_) {
        // ensure data sections have been initialized
        initDataSections();
    } else {
        MCContext* ctx = new MCContext(*tm_->getMCAsmInfo(), NULL);
        mang_ = new llvm::Mangler(*ctx, *tm_->getTargetData()); 
    }

    // omit empty functions..
    if (mf.begin() == mf.end()) return true;   

    SmallString<256> Buffer;
    mang_->getNameWithPrefix(Buffer, mf.getFunction(), false);
    TCEString fnName(Buffer.c_str());

    emitConstantPool(*mf.getConstantPool());

    TTAMachine::AddressSpace* as = mach_->controlUnit()->addressSpace();
    
    TTAProgram::Procedure* procedure = 
        new TTAProgram::Procedure(fnName, *as);

    if (!functionAtATime_) {
        prog_->addProcedure(procedure);
    } 
    ControlFlowGraph* cfg = new ControlFlowGraph(fnName, prog_);
    
/*
    // TODO: antidep level bigger on trunk where loop scheduling.
    DataDependenceGraph* ddg = new DataDependenceGraph(
        allParamRegs_, fnName, DataDependenceGraph::INTRA_BB_ANTIDEPS,
        NULL, true, false);
*/

    bbMapping_.clear();
    skippedBBs_.clear();

    std::set<const MachineBasicBlock*> endingCallBBs;
    std::set<const MachineBasicBlock*> endingCondJumpBBs;
    std::set<const MachineBasicBlock*> endingUncondJumpBBs;
    std::map<const BasicBlockNode*,BasicBlockNode*> callSuccs;
    std::map<const BasicBlockNode*, const MachineBasicBlock*> condJumpSucc;
    std::map<const BasicBlockNode*, BasicBlockNode*> ftSuccs;
    std::set<const MachineBasicBlock*> emptyMBBs;
    std::map<const BasicBlockNode*, bool> bbPredicates;

    BasicBlockNode* entry = new BasicBlockNode(0, 0, true);
    cfg->addNode(*entry);
    bool firstInsOfProc = true;

    // 1st loop create all BB's. do not fill them yet.
    for (MachineFunction::const_iterator i = mf.begin(); i != mf.end(); i++) {
        const MachineBasicBlock& mbb = *i;
        //TODO: what does the parameter do? start address?
        ::BasicBlock* bb = new ::BasicBlock(0);

        // first BB of the program
        if (!functionAtATime_ && prog_->procedureCount() == 1 && 
            cfg->nodeCount() == 1) {
            LLVMTCEBuilder::emitSPInitialization(*bb);
        }
        
        TCEString bbName = mbbName(mbb);
        BasicBlockNode* bbn = new BasicBlockNode(*bb);

        bool newMBB = true;
        bool newBB = true;

        // 1st loop: create all BB's. Do not fill them with instructions.
        for (MachineBasicBlock::const_iterator j = mbb.begin();
             j != mbb.end(); j++) {

            if (!isRealInstruction(*j)) {
                continue;
            }

            if (newBB) {
                newBB = false;
                cfg->addNode(*bbn);
                if (firstInsOfProc) {
                    ControlFlowEdge* edge = new ControlFlowEdge;
                    cfg->connectNodes(*entry, *bbn, *edge);
                    firstInsOfProc = false;
                }

                if (newMBB) {
                    newMBB = false;
                    bbMapping_[&(mbb)] = bbn;
                    for (std::set<const MachineBasicBlock*>::iterator k = 
                             emptyMBBs.begin(); k != emptyMBBs.end(); k++) {
                        skippedBBs_[*k] = bbn;
                    }
                    emptyMBBs.clear();
                }
            }

            if (j->getDesc().isCall()) {
                // if last ins of bb is call, no need to create new bb.
                if (&(*j) == &(mbb.back())) {
                    endingCallBBs.insert(&(*i));
                } else {
                    if (!hasRealInstructions(j, mbb)) {
                        endingCallBBs.insert(&(*i));
                    } else {
                        // create a new BB for code after the call
                        bb = new ::BasicBlock(0);
                        BasicBlockNode* succBBN = new BasicBlockNode(*bb);
                        callSuccs[bbn] = succBBN;
                        bbn = succBBN;
                        newBB = true;
                    }
                }
            } else {
                // also need to split BB on cond branch.
                // LLVM BB may contain 2 branches.
                if (j->getDesc().isBranch()) {
                    TCEString opName = operationName(*j);
                    bool pred = false;
                    // TODO: correctly detect conditional branches
                    // nasty hack to set pred true
                    if ((opName == "?jump" && (pred = true)) ||
                        (opName == "!jump")) {
                        
                        bbPredicates[bbn] = pred;
                        assert(j->getNumOperands() == 2);
                        const MachineOperand& mo = j->getOperand(1);
                        assert(mo.isMBB());
                        condJumpSucc[bbn] = mo.getMBB();

                        if (&(*j) == &(mbb.back())) {
                            endingCondJumpBBs.insert(&(*i));
                        } else {
                            if (!hasRealInstructions(j, mbb)) {
                                endingCondJumpBBs.insert(&(*i));
                            } else {
                                // create a new BB for code after the call.
                                // this should only contain one uncond jump.
                                bb = new ::BasicBlock(0);
                                BasicBlockNode* succBBN = 
                                    new BasicBlockNode(*bb);
                                ftSuccs[bbn] = succBBN;
                                bbn = succBBN;
                                newBB = true;
                            }
                        }
                    } else {
                        // has to be uncond jump, and last ins of bb.
                        assert(operationName(*j) == "jump");
                        assert(&(*j) == &(mbb.back()));
                        endingUncondJumpBBs.insert(&(*i));
                    }
                }
            }
        }
        if (newMBB == true) {
            assert (newBB == true);
            emptyMBBs.insert(&mbb);
        }
        if (newBB) {
            assert (bb->instructionCount() == 0);
            delete bb;
            delete bbn;
        }
    }

    // 2nd loop: create all instructions inside BB's.
    // this can only come after the first loop so that BB's have
    // already been generated.
    for (MachineFunction::const_iterator i = mf.begin(); i != mf.end(); i++) {
        const MachineBasicBlock& mbb = *i;
        
        BasicBlockNode* bbn = NULL;
        std::map<const MachineBasicBlock*,BasicBlockNode*>::iterator
            bbMapIter = bbMapping_.find(&mbb);
        if (bbMapIter == bbMapping_.end()) {
            continue;
        } else {
            bbn = bbMapIter->second;
        }
        ::BasicBlock* bb = &bbn->basicBlock();
        
        for (MachineBasicBlock::const_iterator j = mbb.begin();
             j != mbb.end(); j++) {
            
            TTAProgram::Instruction* instr = NULL;
            instr = emitInstruction(j, bb);
            
            if (instr == NULL) {
                continue;
            } 
            
            // if call, switch tto next bb(in callsucc chain)
            if (instr->hasCall() && &(*j) != &(mbb.back())) {
                bbn = callSuccs[bbn];
                bb = &bbn->basicBlock();
            }

            // conditional jump that is not last ins splits a bb.
            if (j->getDesc().isBranch() && 
                (operationName(*j) == "?jump" || operationName(*j) == "!jump")
                && &(*j) != &(mbb.back())) {
                bbn = ftSuccs[bbn];
                bb = &bbn->basicBlock();
            }
        }

//        ddgBuilder_->constructIndividualBB(REGISTERS_AND_PROGRAM_OPERATIONS);

    }

    // 3rd loop: create edges?
    for (MachineFunction::const_iterator i = mf.begin(); i != mf.end(); i++) {
        const MachineBasicBlock& mbb = *i;

        const BasicBlockNode* bbn = NULL;
        std::map<const MachineBasicBlock*,BasicBlockNode*>::iterator
            bbMapIter = bbMapping_.find(&mbb);
        if (bbMapIter == bbMapping_.end()) {
            continue;
        } else {
            bbn = bbMapIter->second;
        }
        
        // is last ins a call?
        bool callPass = AssocTools::containsKey(endingCallBBs, &mbb);
        bool ftPass = AssocTools::containsKey(endingCondJumpBBs, &mbb);
        bool hasUncondJump = 
            AssocTools::containsKey(endingUncondJumpBBs, &mbb);
        
        const MachineBasicBlock* jumpSucc = condJumpSucc[bbn];
        
        while(true) {
            std::map<const BasicBlockNode*, BasicBlockNode*>::iterator j =
                callSuccs.find(bbn);
            
            std::map<const BasicBlockNode*, BasicBlockNode*>::iterator k =
                ftSuccs.find(bbn);

            // same BB should not be in both.
            assert(j == callSuccs.end() || k == ftSuccs.end());

            if (j != callSuccs.end()) {
                const BasicBlockNode* callSucc = j->second;
                assert(callSucc != NULL);
                ControlFlowEdge* cfe = new ControlFlowEdge(
                    ControlFlowEdge::CFLOW_EDGE_NORMAL, 
                    ControlFlowEdge::CFLOW_EDGE_CALL);
                cfg->connectNodes(*bbn, *callSucc, *cfe);
                bbn = callSucc;
                jumpSucc = condJumpSucc[bbn];                
                continue;
            }
            
            // BB has conditional jump which is not last ins.
            if (k != ftSuccs.end()) {
                assert(jumpSucc != NULL);
                assert(MapTools::containsKey(bbPredicates,bbn));
                ControlFlowEdge* cfe = new ControlFlowEdge(
                    bbPredicates[bbn] == true ?
                    ControlFlowEdge::CFLOW_EDGE_TRUE:
                    ControlFlowEdge::CFLOW_EDGE_FALSE,
                    ControlFlowEdge::CFLOW_EDGE_JUMP);
                if (MapTools::containsKey(bbMapping_, jumpSucc)) {
                    cfg->connectNodes(*bbn, *bbMapping_[jumpSucc], *cfe);
                } else {
                    cfg->connectNodes(*bbn, *skippedBBs_[jumpSucc], *cfe);
                }
                
                const BasicBlockNode* ftSucc = k->second;
                assert(ftSucc != NULL);
                cfe = new ControlFlowEdge(
                    bbPredicates[bbn] == true ?
                    ControlFlowEdge::CFLOW_EDGE_FALSE:
                    ControlFlowEdge::CFLOW_EDGE_TRUE,
                    ControlFlowEdge::CFLOW_EDGE_FALLTHROUGH);
                cfg->connectNodes(*bbn, *ftSucc, *cfe);
                bbn = ftSucc;
                continue;
            }
            break;
        }

        for (MachineBasicBlock::const_succ_iterator si = mbb.succ_begin();
             si != mbb.succ_end(); si++) {
            const MachineBasicBlock* succ = *si;
            
            BasicBlockNode* succBBN = NULL;
            std::map<const MachineBasicBlock*,BasicBlockNode*>::iterator 
                bbMapIter = bbMapping_.find(succ);
            if (bbMapIter == bbMapping_.end()) {
                succBBN = skippedBBs_[succ];
            } else {
                succBBN = bbMapIter->second;
            }
            
            // TODO: type of the edge
            ControlFlowEdge* cfe = NULL;
            // if last ins of bb was call, cheyte call-pass edge.
            if (callPass) {
                cfe = new ControlFlowEdge(
                    ControlFlowEdge::CFLOW_EDGE_NORMAL, 
                    ControlFlowEdge::CFLOW_EDGE_CALL);
            } else {
                // do we have conditional jump?
                if (jumpSucc != NULL) {
                    // fall-through is a pass to next mbb.
                    if (ftPass) {
                        assert(MapTools::containsKey(bbPredicates,bbn));
                        if (succ == jumpSucc) {
                            cfe = new ControlFlowEdge(
                                bbPredicates[bbn] == true ?
                                ControlFlowEdge::CFLOW_EDGE_TRUE:
                                ControlFlowEdge::CFLOW_EDGE_FALSE,
                                ControlFlowEdge::CFLOW_EDGE_JUMP);
                        } else {
                            cfe = new ControlFlowEdge(
                                bbPredicates[bbn] == true ?
                                ControlFlowEdge::CFLOW_EDGE_FALSE:
                                ControlFlowEdge::CFLOW_EDGE_TRUE,
                                ControlFlowEdge::CFLOW_EDGE_FALLTHROUGH);
                        }
                    } else {
                        // split a bb. ft edges created earlier.
                        // cond.jump edge also created earlier. 
                        // just needs to add the uncond jump edge.
                        
                        if (succ == jumpSucc) {
                            continue;
                        }
                        
                        cfe = new ControlFlowEdge;
                    }
                } else { // no conditional jump. ft to next bb.
                    // unconditional jump to nxt bb
                    if (hasUncondJump) {
                        cfe = new ControlFlowEdge;
                    } else {
                        // no unconditional jump to next bb. limits bb
                        // reordering
                        cfe = new ControlFlowEdge(
                            ControlFlowEdge::CFLOW_EDGE_NORMAL,
                            ControlFlowEdge::CFLOW_EDGE_FALLTHROUGH);
                    }
                }
            }
            cfg->connectNodes(*bbn, *succBBN, *cfe);
        }
    }
    // create jumps to exit node
    BasicBlockNode* exit = new BasicBlockNode(0, 0, false, true);
    cfg->addNode(*exit);
    cfg->addExitFromSinkNodes(exit);

    // add back edge properties.
    cfg->detectBackEdges();

#ifdef WRITE_CFG_DOTS
    cfg->writeToDotFile(fnName + "_cfg1.dot");
#endif

    // TODO: on trunk single bb loop(swp), last param true(rr, threading)
    DataDependenceGraph* ddg = ddgBuilder_.build(
        *cfg, DataDependenceGraph::INTRA_BB_ANTIDEPS, NULL, true, false);

#ifdef WRITE_DDG_DOTS
    ddg->writeToDotFile(fnName + "_ddg1.dot");
#endif

    PreOptimizer preOpt(*ipData_);
    preOpt.handleCFGDDG(*cfg, *ddg);

#ifdef WRITE_DDG_DOTS
    ddg->writeToDotFile(fnName + "_ddg2.dot");
#endif

    CycleLookBackSoftwareBypasser bypasser;
    CopyingDelaySlotFiller dsf;
    BBSchedulerController bbsc(*ipData_, &bypasser, &dsf);
    bbsc.handleCFGDDG(*cfg, *ddg, *mach_ );

#ifdef WRITE_CFG_DOTS
    cfg->writeToDotFile(fnName + "_cfg2.dot");
#endif
#ifdef WRITE_DDG_DOTS
    ddg->writeToDotFile(fnName + "_ddg3.dot");
#endif

    TTAProgram::InstructionReferenceManager* irm = NULL;
    if (functionAtATime_) {
        irm = new TTAProgram::InstructionReferenceManager();
    } else {        
        irm = &prog_->instructionReferenceManager();
    }

    cfg->convertBBRefsToInstRefs(*irm);

    if (!functionAtATime_) {
        // TODO: make DS filler work with FAAT
        dsf.fillDelaySlots(*cfg, *ddg, *mach_, *umach_, true);
    }

#ifdef WRITE_DDG_DOTS
    ddg->writeToDotFile(fnName + "_ddg4.dot");
#endif

#ifdef WRITE_CFG_DOTS
    cfg->writeToDotFile(fnName + "_cfg3.dot");
#endif

    cfg->copyToProcedure(*procedure, irm);
#ifdef WRITE_CFG_DOTS
    cfg->writeToDotFile(fnName + "_cfg4.dot");
#endif
    if (procedure->instructionCount() > 0) {
        codeLabels_[fnName] = &procedure->firstInstruction();
    }

    delete ddg;
    delete cfg;

    if (modifyMF_) {
        //convertProcedureToMachineFunction(*procedure, mf);
        return true;
    }

    if (functionAtATime_) delete irm;
    return false;
}

TTAProgram::Terminal*
LLVMTCECFGDDGBuilder::createMBBReference(const MachineOperand& mo) {
    MachineBasicBlock* mbb = mo.getMBB();
    std::map<const MachineBasicBlock*,BasicBlockNode*>::iterator i = 
        bbMapping_.find(mbb);
    
    if (i == bbMapping_.end()) {
        std::map<const MachineBasicBlock*, BasicBlockNode*>::iterator j = 
            skippedBBs_.find(mbb);
        return new TTAProgram::TerminalBasicBlockReference(
            j->second->basicBlock());
    }

    return new TTAProgram::TerminalBasicBlockReference(
        i->second->basicBlock());
}

TTAProgram::Terminal*
LLVMTCECFGDDGBuilder::createSymbolReference(const TCEString& symbolName) {
    return new TTAProgram::TerminalSymbolReference(symbolName);
}

bool 
LLVMTCECFGDDGBuilder::isRealInstruction(const MachineInstr& instr) {
    
    const llvm::TargetInstrDesc* opDesc = &instr.getDesc();

    if (opDesc->isReturn()) {
        return true;
    }

    // when the -g option turn on, this will come up opc with this, therefore
    // add this to ignore however, it is uncertain whether the debug "-g" will
    // generate more opc, need to verify
    if (opDesc->getOpcode() == TargetOpcode::DBG_VALUE) {
        return false;
    }	

    std::string opName = operationName(instr);

    // Pseudo instructions don't require any actual instructions.
    if (opName == "PSEUDO") {
        return false;
    }
    return true;
}

bool 
LLVMTCECFGDDGBuilder::hasRealInstructions(
    MachineBasicBlock::const_iterator i, 
    const MachineBasicBlock& mbb) {
    for (; i != mbb.end(); i++) {
        if (isRealInstruction(*i)) {
            return true;
        }
    }
    return false;
}

bool
LLVMTCECFGDDGBuilder::doFinalization(Module& m) { 

    LLVMTCEBuilder::doFinalization(m);
    prog_->convertSymbolRefsToInsRefs();

    LLVMTCECmdLineOptions* options =
        dynamic_cast<LLVMTCECmdLineOptions*>(Application::cmdLineOptions());

    std::string outputFileName = "cfgddgbuilder.tpef";
    if (options->isOutputFileDefined()) {
        outputFileName = options->outputFile();
    }

    TTAProgram::Program::writeToTPEF(*prog_, outputFileName);
    exit(0);
    return false; 
}

TCEString 
LLVMTCECFGDDGBuilder::operationName(const MachineInstr& mi) const {
    if (dynamic_cast<const TCETargetMachine*>(&targetMachine()) 
        != NULL) {
        return dynamic_cast<const TCETargetMachine&>(targetMachine())
            .operationName(mi.getDesc().getOpcode());
    } else {
        return mi.getDesc().getName();
    }
}

TCEString
LLVMTCECFGDDGBuilder::registerFileName(unsigned llvmRegNum) const { 
    if (isTTATarget()) {
        return dynamic_cast<const TCETargetMachine&>(
            targetMachine()).rfName(llvmRegNum); 
    } else {
        // LLVM does not support explicit register file info
        // at the moment, so we assume there's only one reg file
        // in the machine. Pick the first one that is not
        // a 1-bit reg file.
        const TTAMachine::Machine::RegisterFileNavigator rfNav =
            mach_->registerFileNavigator();

        for (int i = 0; i < rfNav.count(); i++) {
            const TTAMachine::RegisterFile& rf = *rfNav.item(i);
            if (rf.width() > 1) 
                return rf.name();
        }
        abortWithError(
            TCEString("Unable to figure the RF for llvm reg num ") <<
            llvmRegNum);
        
    }
    TCEString justAnotherWarningFix;
    return justAnotherWarningFix;
}

int
LLVMTCECFGDDGBuilder::registerIndex(unsigned llvmRegNum) const {
    if (isTTATarget()) {
        return dynamic_cast<const TCETargetMachine&>(
            targetMachine()).registerIndex(llvmRegNum); 
    } else {
        /* Assume for non-TTA targets the register index
           is the final and correct one and that there's only
           one register file. With TTA we have to do conversion 
           due to the multiple register files option which LLVM 
           does not support. */
        return llvmRegNum;
    }
}

#define DEBUG_POM_TO_MI

void
LLVMTCECFGDDGBuilder::convertProcedureToMachineFunction(
    const TTAProgram::Procedure& proc,
    llvm::MachineFunction& mf) {

#ifdef DEBUG_POM_TO_MI
    Application::logStream()
        << "TTA instructions:" << std::endl 
        << proc.toString() << std::endl << std::endl
        << "OTA instructions:" << std::endl;
#endif

    const int operationSlots = mach_->functionUnitNavigator().count();
    // the order of function unit operations in the instruction bundle
    typedef std::vector<const TTAMachine::FunctionUnit*> BundleOrderIndex;
    BundleOrderIndex bundleOrder;

    // Currently the bundle order is hard coded to the order of appearance
    // in the ADF file.
    for (int fuc = 0; fuc < mach_->functionUnitNavigator().count(); ++fuc) {
        TTAMachine::FunctionUnit* fu = mach_->functionUnitNavigator().item(fuc);
        bundleOrder.push_back(fu);
    }

    for (int i = 0; i < proc.instructionCount(); ++i) {
        const TTAProgram::Instruction& instr = 
            proc.instructionAtIndex(i);
        // First collect all started operations at this cycle
        // on each FU. 
        typedef std::map<const TTAMachine::FunctionUnit*, 
                         const TTAMachine::HWOperation*> OpsMap;
        OpsMap startedOps;
        for (int m = 0; m < instr.moveCount(); ++m) {
            const TTAProgram::Move& move = instr.move(m);
            if (move.isTriggering()) {
                startedOps[&move.destination().functionUnit()] =
                    dynamic_cast<TTAProgram::TerminalFUPort&>(
                        move.destination()).hwOperation();
            }
        }

        // in OTAs with data hazard detection, we do not need to emit
        // completely empty instruction bundles at all
        if (startedOps.size() == 0)
            continue; 
        
        typedef std::map<const TTAMachine::HWOperation*,
                         std::vector<TTAProgram::Terminal*> > OperandMap;
        OperandMap operands;
        // On a second pass through the moves we now should know the operand 
        // numbers of all the moves. The result moves should be at an 
        // instruction at the operation latency.
        OperationPool operations;

        for (OpsMap::const_iterator opsi = startedOps.begin(); 
             opsi != startedOps.end(); ++opsi) {
            const TTAMachine::FunctionUnit* fu = (*opsi).first;
            const TTAMachine::HWOperation* hwOp = (*opsi).second;
            const Operation& operation = 
                operations.operation(hwOp->name().c_str());
            // first find the outputs
            for (int out = 0; out < operation.numberOfOutputs(); ++out) {
                const TTAProgram::Instruction& resultInstr = 
                    proc.instructionAtIndex(i + hwOp->latency());
                for (int m = 0; m < resultInstr.moveCount(); ++m) {
                    const TTAProgram::Move& move = resultInstr.move(m);
                    // assume it's a register write, the potential (pseudo) 
                    // bypass move is ignored
                    if (move.source().isFUPort() && 
                        &move.source().functionUnit() ==
                        hwOp->parentUnit() &&
                        (move.destination().isGPR() ||
                         move.destination().isRA())) {
                        operands[hwOp].push_back(&move.destination());
                    }
                }
            }
            if (operation.numberOfOutputs() != operands[hwOp].size()) {
                PRINT_VAR(operation.name());
                PRINT_VAR(operands[hwOp].size());
                PRINT_VAR(operation.numberOfOutputs());
                assert(operation.numberOfOutputs() == operands[hwOp].size());
                abort();
            }

            // then the inputs
            for (int input = 0; input < operation.numberOfInputs();
                 ++input) {
                for (int m = 0; m < instr.moveCount(); ++m) {
                    const TTAProgram::Move& move = instr.move(m);
                    if (move.destination().isFUPort() &&
                        &move.destination().functionUnit() ==
                        hwOp->parentUnit() &&
                        dynamic_cast<const TTAMachine::Port*>(
                            hwOp->port(input + 1)) == 
                        &move.destination().port()) {
                        // if the result is forwarded (bypass), find the
                        // result move 
                        if (move.source().isFUPort()) {
                            for (int mm = 0; mm < instr.moveCount(); ++mm) {
                                const TTAProgram::Move& move2 = 
                                    instr.move(mm);
                                if (move2.destination().isGPR() &&
                                    move2.source().isFUPort() && 
                                    &move2.source().port() ==
                                    &move.source().port()) {
                                    operands[hwOp].push_back(&move2.destination());
                                }
                            }
                        } else {
                            // otherwise assume it's not bypassed but
                            // read from the RF
                            operands[hwOp].push_back(&move.source());
                        }
                    }
                }
            }

            if (operation.numberOfInputs() + operation.numberOfOutputs() !=
                operands[hwOp].size()) {
                PRINT_VAR(operation.name());
                PRINT_VAR(operands[hwOp].size());
                PRINT_VAR(operation.numberOfInputs());
                PRINT_VAR(operation.numberOfOutputs());
                assert(
                    operation.numberOfInputs() + operation.numberOfOutputs() ==
                    operands[hwOp].size());
            }
        }

        for (BundleOrderIndex::const_iterator boi = bundleOrder.begin();
             boi != bundleOrder.end(); ++boi) {
            if (startedOps.find(*boi) == startedOps.end()) {
#ifdef DEBUG_POM_TO_MI
                Application::logStream() << "nop";
#endif
            } else {
                const TTAMachine::HWOperation* hwop = 
                    (*startedOps.find(*boi)).second;
#ifdef DEBUG_POM_TO_MI
                Application::logStream() << hwop->name() << " ";
#endif
                std::vector<TTAProgram::Terminal*>& opr = operands[hwop];
                
                int counter = 0;
                for (std::vector<TTAProgram::Terminal*>::const_iterator opri =
                         opr.begin(); opri != opr.end(); ++opri, ++counter) {
                    TTAProgram::Terminal* terminal = *opri;
#ifdef DEBUG_POM_TO_MI
                    if (counter > 0)
                        Application::logStream() << ", ";
                    Application::logStream() << terminal->toString();
#endif                    
                }
            }
#ifdef DEBUG_POM_TO_MI
            Application::logStream() << "\t# " << (*boi)->name() << std::endl;
#endif
        }
#ifdef DEBUG_POM_TO_MI
        Application::logStream() << std::endl;
#endif
    }

#ifdef DEBUG_POM_TO_MI
    Application::logStream() << std::endl << std::endl;
#endif
}
    

}