#pragma once
#include <memory>
#include <optional>
#include <string>
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
    // assigned in `state`? Called just before State::assign(), millions of
    // times in a search, so it never builds any text.
    bool allows(const Problem& problem, const State& state, int emp, int shift) const {
        return check(problem, state, emp, shift, nullptr);
    }

    // The same question, for a person (--explain, step 6.4, D47): nothing if
    // the rule allows it, otherwise the reason in words, e.g.
    // "only 8h of rest between SUN-E and MON-M, needs 10h".
    std::optional<std::string> whyNot(const Problem& problem, const State& state, int emp, int shift) const {
        std::string why;
        if (check(problem, state, emp, shift, &why)) return std::nullopt;
        return why;
    }

    // Short name for messages, e.g. "no-overlap".
    virtual const char* name() const = 0;

protected:
    // The rule itself, written once for both questions above (D47).
    // Returns true if allowed. When it returns false and `why` is not null,
    // it writes the reason there; the search passes null, so no text is
    // built where speed matters.
    virtual bool check(const Problem& problem, const State& state, int emp, int shift, std::string* why) const = 0;
};

// ---- The rules ---------------------------------------------------------------

// Nobody works two shifts at the same time. Always on: it is a fact of
// physics, not a policy, so it is not in the "rules" section of the file.
class NoOverlapConstraint : public Constraint {
public:
    const char* name() const override { return "no-overlap"; }

protected:
    bool check(const Problem& problem, const State& state, int emp, int shift, std::string* why) const override;
};

// At least `minRestHours` between any two work blocks of the same employee
// (DECISIONS D15). Shifts no more than `blockGapMinutes` apart are one block
// (D14), so a double (e.g. 08-16 + 16-00) needs no rest in the middle.
// Whether a double is allowed at all is a separate rule (step 2.3).
class MinRestConstraint : public Constraint {
public:
    MinRestConstraint(int minRestHours, int blockGapMinutes)
        : minRestMinutes_(minRestHours * 60), blockGapMinutes_(blockGapMinutes) {}
    const char* name() const override { return "min-rest"; }

protected:
    bool check(const Problem& problem, const State& state, int emp, int shift, std::string* why) const override;

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
    const char* name() const override { return "double-needs-approval"; }

protected:
    bool check(const Problem& problem, const State& state, int emp, int shift, std::string* why) const override;

private:
    int doubleAboveMinutes_;
    int maxDoubleMinutes_;
    int blockGapMinutes_;
};

// An employee with "maxShifts" in the input never gets more shifts than that.
// Employees without it have no personal limit. A double counts as two shifts.
class MaxShiftsConstraint : public Constraint {
public:
    const char* name() const override { return "max-shifts"; }

protected:
    bool check(const Problem& problem, const State& state, int emp, int shift, std::string* why) const override;
};

// No more than `maxDays` calendar days in a row with any work.
// A shift counts on the day it starts (DECISIONS D13): five night shifts
// Sunday to Thursday are five work days, not six. Short rest after a night
// shift is the job of min-rest, not of this rule.
class MaxConsecutiveDaysConstraint : public Constraint {
public:
    explicit MaxConsecutiveDaysConstraint(int maxDays) : maxDays_(maxDays) {}
    const char* name() const override { return "max-consecutive-days"; }

protected:
    bool check(const Problem& problem, const State& state, int emp, int shift, std::string* why) const override;

private:
    int maxDays_;
};

// ---- Factory -----------------------------------------------------------------

// Builds the list of rules that apply to this problem.
using ConstraintList = std::vector<std::unique_ptr<Constraint>>;
ConstraintList makeConstraints(const Problem& problem);
