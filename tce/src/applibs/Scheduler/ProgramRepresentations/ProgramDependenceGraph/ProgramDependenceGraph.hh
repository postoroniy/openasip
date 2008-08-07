/**
 * @file ProgramDependenceGraph.hh
 *
 * Declaration of prototype of graph-based program representation:
 * declaration of the program dependence graph.
 *
 * @author Vladimir Guzma 2006 (vladimir.guzma@tut.fi)
 * @note rating: red
 */

#ifndef TTA_PROGRAM_DEPENDENCE_GRAPH_HH
#define TTA_PROGRAM_DEPENDENCE_GRAPH_HH

#include "BoostGraph.hh"
#include "ProgramDependenceEdge.hh"
#include "ProgramDependenceNode.hh"
#include "ControlFlowGraph.hh"
#include "ControlDependenceGraph.hh"
#include "DataDependenceGraph.hh"

class ProgramDependenceGraph :
    public BoostGraph<ProgramDependenceNode, ProgramDependenceEdge> {

public:
    ProgramDependenceGraph(
        ControlDependenceGraph& cdg,
        DataDependenceGraph& ddg);
    virtual ~ProgramDependenceGraph();
    
private:
    typedef std::map<ControlDependenceNode*, ProgramDependenceNode*,
                     ControlDependenceNode::Comparator> 
        ControlToProgram; 
        
    void removeGuardedJump(
        ControlToProgram&,
        ProgramDependenceNode&, 
        ControlDependenceNode&);
    
    ControlDependenceGraph* cdg_;
    DataDependenceGraph* ddg_;
    
};

#endif
