#pragma once
#include <memory>
#include <vector>

#include "Problem.h"
#include "solver/State.h"

// A hard rule (DECISIONS D6): if allows() returns false, the assignment is
// not made and that branch of the search is cut off.
//
// This is an abstract base class: it only says WHAT every rule can answer.
// Each concrete rule (a derived class) decides HOW.
class Constraint {
public:
    virtual ~Constraint() = default;

    // May employee `emp` be added to shift `shift`, given what is already
    // assigned in `state`? Called just before State::assign().
    virtual bool allows(const Problem& problem, const State& state, int emp, int shift) const = 0;

    // Short name for messages, e.g. "no-overlap".
    virtual const char* name() const = 0;
};

// ---- The rules ---------------------------------------------------------------

// Nobody works two shifts at the same time. Always on: it is a fact of
// physics, not a policy, so it is not in the "rules" section of the file.
class NoOverlapConstraint : public Constraint {
public:
    bool allows(const Problem& problem, const State& state, int emp, int shift) const override;
    const char* name() const override { return "no-overlap"; }
};

// At least `minRestHours` between any two work blocks of the same employee
// (DECISIONS D15). Shifts no more than `blockGapMinutes` apart are one block
// (D14), so a double (e.g. 08-16 + 16-00) needs no rest in the middle.
// Whether a double is allowed at all is a separate rule (step 2.3).
class MinRestConstraint : public Constraint {
public:
    MinRestConstraint(int minRestHours, int blockGapMinutes)
        : minRestMinutes_(minRestHours * 60), blockGapMinutes_(blockGapMinutes) {}
    bool allows(const Problem& problem, const State& state, int emp, int shift) const override;
    const char* name() const override { return "min-rest"; }

private:
    int minRestMinutes_;
    int blockGapMinutes_;
};

// A work block longer than `doubleAboveHours` is a double (DECISIONS D16).
// A double is allowed only if the employee approved it in their availability
// ("double": true on a slot that covers the whole block), and never longer
// than `maxDoubleHours`. Preferring non-doubles is the solver's job (step 2.3).
class DoubleShiftConstraint : public Constraint {
public:
    DoubleShiftConstraint(int doubleAboveHours, int maxDoubleHours, int blockGapMinutes)
        : doubleAboveMinutes_(doubleAboveHours * 60),
          maxDoubleMinutes_(maxDoubleHours * 60),
          blockGapMinutes_(blockGapMinutes) {}
    bool allows(const Problem& problem, const State& state, int emp, int shift) const override;
    const char* name() const override { return "double-needs-approval"; }

private:
    int doubleAboveMinutes_;
    int maxDoubleMinutes_;
    int blockGapMinutes_;
};

// ---- Factory -----------------------------------------------------------------

// Builds the list of rules that apply to this problem.
using ConstraintList = std::vector<std::unique_ptr<Constraint>>;
ConstraintList makeConstraints(const Problem& problem);
