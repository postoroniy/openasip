/**
 * @file ResourceVectorFUResourceConflictDetector.hh
 *
 * Declaration of ResourceVectorFUResourceConflictDetector class.
 *
 * @author Pekka Jääskeläinen 2006 (pjaaskel@cs.tut.fi)
 * @note rating: red
 */

#ifndef TTA_RV_FU_RESOURCE_CONFLICT_DETECTOR_HH
#define TTA_RV_FU_RESOURCE_CONFLICT_DETECTOR_HH

#include <map>
#include <string>

#include "Exception.hh"
#include "FunctionUnit.hh"
#include "FUResourceConflictDetector.hh"
#include "FUFiniteStateAutomaton.hh"
#include "ResourceVectorSet.hh"

class Operation;

/**
 * An FSA implementation of a FU resource conflict detector.
 */
class ResourceVectorFUResourceConflictDetector : 
    public FUResourceConflictDetector {
public:

    ResourceVectorFUResourceConflictDetector(
        const TTAMachine::FunctionUnit& fu)
        throw (InvalidData);
    virtual ~ResourceVectorFUResourceConflictDetector();

    virtual bool issueOperation(OperationID id);

    virtual bool advanceCycle();

    virtual void reset();

    virtual OperationID operationID(const TCEString& operationName) const;

private:
    /// The resource vectors of operations.
    ResourceVectorSet vectors_;
    /// The composite resource vector.
    ResourceVector compositeVector_;
};

#endif
