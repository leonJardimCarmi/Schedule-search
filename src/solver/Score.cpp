#include "solver/Score.h"

#include <algorithm>
#include <map>

#include "solver/Availability.h"
#include "solver/WorkBlocks.h"

const char* toString(Level level) {
    return level == Level::Medium ? "Medium" : "Soft";
}

// ---- The rules ---------------------------------------------------------------
// Each is small and independent. Their classes are local to this file: the
// rest of the program only sees them through SoftRule.

namespace {

// Shifts left below their min. This only happens to shifts nobody can fully
// staff (D25), so these are the holes the manager has to fill by hand.
class BelowMin : public SoftRule {
public:
    const char* name() const override { return "below-min"; }
    const char* description() const override { return "person missing below a shift's minimum"; }
    Level level() const override { return Level::Medium; }
    long long violations(const Problem& p, const State& s) const override {
        long long missing = 0;
        for (int i = 0; i < static_cast<int>(p.shifts.size()); ++i)
            missing += std::max(0, p.shifts[i].minStaff - s.assignedCount(i));
        return missing;
    }
};

class BelowWanted : public SoftRule {
public:
    const char* name() const override { return "below-wanted"; }
    const char* description() const override { return "person missing below a shift's wanted staff"; }
    long long violations(const Problem& p, const State& s) const override {
        long long missing = 0;
        for (int i = 0; i < static_cast<int>(p.shifts.size()); ++i)
            missing += std::max(0, p.shifts[i].wantedStaff - s.assignedCount(i));
        return missing;
    }
};

// D5: "preferred" availability should be used when possible.
class NotPreferred : public SoftRule {
public:
    const char* name() const override { return "not-preferred"; }
    const char* description() const override { return "assignment the employee did not mark as preferred"; }
    long long violations(const Problem& p, const State& s) const override {
        long long count = 0;
        for (int i = 0; i < static_cast<int>(p.shifts.size()); ++i)
            for (int e : s.staffOf(i))
                if (availabilityFit(p.employees[e], p.shifts[i]) != Fit::Preferred) ++count;
        return count;
    }
};

// D16: a double is allowed with approval, but is a last resort.
class DoubleShifts : public SoftRule {
public:
    const char* name() const override { return "double"; }
    const char* description() const override { return "double shift (approved, but best avoided)"; }
    long long violations(const Problem& p, const State& s) const override {
        const int limit = p.rules.doubleAboveHours * 60;
        long long doubles = 0;
        for (int e = 0; e < static_cast<int>(p.employees.size()); ++e)
            for (const WorkBlock& block : buildBlocks(p, s.shiftsOf(e), p.rules.blockGapMinutes))
                if (block.minutes() > limit) ++doubles;
        return doubles;
    }
};

// D22: minShifts is a wish, scored here.
class BelowMinShifts : public SoftRule {
public:
    const char* name() const override { return "below-min-shifts"; }
    const char* description() const override { return "shift missing below an employee's minShifts"; }
    long long violations(const Problem& p, const State& s) const override {
        long long missing = 0;
        for (int e = 0; e < static_cast<int>(p.employees.size()); ++e)
            if (p.employees[e].minShifts)
                missing += std::max(0, *p.employees[e].minShifts - static_cast<int>(s.shiftsOf(e).size()));
        return missing;
    }
};

}  // namespace

// ---- The model -------------------------------------------------------------

void ScoreModel::add(std::unique_ptr<SoftRule> rule, int weight) {
    entries_.push_back(Entry{std::move(rule), weight});
}

Score ScoreModel::score(const Problem& problem, const State& schedule) const {
    Score result;
    for (const Entry& entry : entries_) {
        if (entry.weight == 0) continue;   // turned off: not even computed
        result.total[static_cast<int>(entry.rule->level())] += entry.weight * entry.rule->violations(problem, schedule);
    }
    return result;
}

std::vector<ScoreLine> ScoreModel::breakdown(const Problem& problem, const State& schedule) const {
    std::vector<ScoreLine> lines;
    for (const Entry& entry : entries_) {
        const long long v = entry.weight == 0 ? 0 : entry.rule->violations(problem, schedule);
        lines.push_back(ScoreLine{entry.rule->name(), entry.rule->description(), entry.rule->level(),
                                  v, entry.weight, v * entry.weight});
    }
    return lines;
}

ScoreModel makeScoreModel(const Problem& problem) {
    // Default weights (D28). Below-wanted weighs most among the soft rules:
    // a short-staffed shift hurts the business more than a missed preference.
    ScoreModel model;
    const std::map<std::string, int>& custom = problem.rules.weights;
    auto weightOf = [&](const char* name, int defaultWeight) {
        auto it = custom.find(name);
        return it != custom.end() ? it->second : defaultWeight;
    };
    auto add = [&](std::unique_ptr<SoftRule> rule, int defaultWeight) {
        const int weight = weightOf(rule->name(), defaultWeight);
        model.add(std::move(rule), weight);
    };

    add(std::make_unique<BelowMin>(), 1);
    add(std::make_unique<BelowWanted>(), 10);
    add(std::make_unique<DoubleShifts>(), 5);
    add(std::make_unique<BelowMinShifts>(), 5);
    add(std::make_unique<NotPreferred>(), 1);
    // New soft rules are added here (D32).

    for (const auto& [name, weight] : custom) {
        const bool known = std::any_of(model.entries_.begin(), model.entries_.end(),
            [&](const ScoreModel::Entry& e) { return name == e.rule->name(); });
        if (!known) model.unknownWeights_.push_back(name);
    }
    return model;
}
