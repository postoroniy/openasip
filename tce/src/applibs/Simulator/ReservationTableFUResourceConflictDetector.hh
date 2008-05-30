/**
 * @file ReservationTableFUResourceConflictDetector.hh
 *
 * Declaration of ReservationTableFUResourceConflictDetector class.
 *
 * @author Pekka Jääskeläinen 2007 (pjaaskel@cs.tut.fi)
 * @note rating: red
 */

#ifndef TTA_CRT_FU_RESOURCE_CONFLICT_DETECTOR_HH
#define TTA_CRT_FU_RESOURCE_CONFLICT_DETECTOR_HH

#include <map>
#include <string>

#include "Exception.hh"
#include "FunctionUnit.hh"
#include "FUResourceConflictDetector.hh"
#include "FUReservationTableIndex.hh"
#include "ReservationTable.hh"

class Operation;
class TCEString;

namespace TTAMachine {
    class FunctionUnit;
}

/**
 * A Conventional Reservation Table implementation of a FU resource conflict 
 * detector.
 */
class ReservationTableFUResourceConflictDetector : 
    public FUResourceConflictDetector {
public:

    ReservationTableFUResourceConflictDetector(
        const TTAMachine::FunctionUnit& fu)
        throw (InvalidData);
    virtual ~ReservationTableFUResourceConflictDetector();

    virtual bool issueOperation(OperationID id);
    virtual bool advanceCycle();
    virtual void reset();

    bool issueOperationInline(OperationID id);
    bool advanceCycleInline();

    virtual OperationID operationID(const TCEString& operationName) const;

private:
    /// The reservation tables of operations.
    FUReservationTableIndex reservationTables_;
    /// The global reservation table.
    ReservationTable globalReservationTable_;
    /// The modeled FU.
    const TTAMachine::FunctionUnit& fu_;
};

#include "ReservationTableFUResourceConflictDetector.icc"

#endif
