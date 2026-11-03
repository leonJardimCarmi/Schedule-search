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

// ---- Factory -----------------------------------------------------------------

// Builds the list of rules that apply to this problem.
using ConstraintList = std::vector<std::unique_ptr<Constraint>>;
ConstraintList makeConstraints(const Problem& problem);
