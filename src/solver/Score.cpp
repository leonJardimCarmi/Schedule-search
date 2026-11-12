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

// Missing people below `target(shift)`, for both staffing rules.
// Decided shift: final, exact. Empty shift: it will get at most
// allowedCount people, so at least target - allowedCount are missing.
template <typename Target>
long long missingBound(const Problem& p, const State& s, const SearchView& v, Target target) {
    long long missing = 0;
    for (int i = 0; i < static_cast<int>(p.shifts.size()); ++i) {
        const int most = v.decided[i] ? s.assignedCount(i) : v.allowedCount[i];
        missing += std::max(0, target(p.shifts[i]) - most);
    }
    return missing;
}

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
    long long lowerBound(const Problem& p, const State& s, const SearchView& v) const override {
        return missingBound(p, s, v, [](const Shift& sh) { return sh.minStaff; });
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
    long long lowerBound(const Problem& p, const State& s, const SearchView& v) const override {
        return missingBound(p, s, v, [](const Shift& sh) { return sh.wantedStaff; });
    }
};

// D5: "preferred" availability should be used when possible.
class NotPreferred : public SoftRule {
public:
    // Availability never changes, so "preferred?" is computed once here
    // instead of at every node of the search.
    explicit NotPreferred(const Problem& p) : preferred_(p.shifts.size()) {
        for (std::size_t i = 0; i < p.shifts.size(); ++i)
            for (const Employee& e : p.employees)
                preferred_[i].push_back(availabilityFit(e, p.shifts[i]) == Fit::Preferred);
    }
    const char* name() const override { return "not-preferred"; }
    const char* description() const override { return "assignment the employee did not mark as preferred"; }
    long long violations(const Problem& p, const State& s) const override {
        long long count = 0;
        for (int i = 0; i < static_cast<int>(p.shifts.size()); ++i)
            for (int e : s.staffOf(i))
                if (!preferred_[i][e]) ++count;
        return count;
    }
    // Decided shift: its non-preferred people, exact. Empty shift: it will
    // get at least searchMin people; if fewer of the allowed ones prefer it,
    // the rest cannot be "preferred".
    long long lowerBound(const Problem& p, const State& s, const SearchView& v) const override {
        long long count = 0;
        for (int i = 0; i < static_cast<int>(p.shifts.size()); ++i) {
            if (v.decided[i]) {
                for (int e : s.staffOf(i))
                    if (!preferred_[i][e]) ++count;
                continue;
            }
            int preferredAllowed = 0;
            for (std::size_t pos = 0; pos < v.candidates[i].size(); ++pos)
                if (v.allowedNow[i][pos] && preferred_[i][v.candidates[i][pos]]) ++preferredAllowed;
            count += std::max(0, v.searchMin[i] - preferredAllowed);
        }
        return count;
    }

private:
    std::vector<std::vector<char>> preferred_;   // [shift][employee]
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

    // An employee can still get at most one more shift for each empty shift
    // the hard rules allow them in now (and never more than maxShifts).
    // If even that is not enough to reach minShifts, the rest is missing.
    long long lowerBound(const Problem& p, const State& s, const SearchView& v) const override {
        stillPossible_.assign(p.employees.size(), 0);
        for (int i = 0; i < static_cast<int>(p.shifts.size()); ++i) {
            if (v.decided[i]) continue;
            for (std::size_t pos = 0; pos < v.candidates[i].size(); ++pos)
                if (v.allowedNow[i][pos]) ++stillPossible_[v.candidates[i][pos]];
        }
        long long missing = 0;
        for (int e = 0; e < static_cast<int>(p.employees.size()); ++e) {
            const Employee& employee = p.employees[e];
            if (!employee.minShifts) continue;
            const int has = static_cast<int>(s.shiftsOf(e).size());
            int more = stillPossible_[e];
            if (employee.maxShifts) more = std::min(more, *employee.maxShifts - has);
            missing += std::max(0, *employee.minShifts - has - more);
        }
        return missing;
    }

private:
    // Scratch space, reused at every node instead of allocating a new vector.
    // "mutable" because lowerBound() is const: it does not change the rule.
    mutable std::vector<int> stillPossible_;
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

Score ScoreModel::lowerBound(const Problem& problem, const State& partial, const SearchView& view) const {
    // Each rule's bound is a minimum for that rule alone, so their weighted
    // sum is a minimum for the whole score.
    Score result;
    for (const Entry& entry : entries_) {
        if (entry.weight == 0) continue;
        result.total[static_cast<int>(entry.rule->level())] += entry.weight * entry.rule->lowerBound(problem, partial, view);
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
    add(std::make_unique<NotPreferred>(problem), 1);
    // New soft rules are added here (D32).

    for (const auto& [name, weight] : custom) {
        const bool known = std::any_of(model.entries_.begin(), model.entries_.end(),
            [&](const ScoreModel::Entry& e) { return name == e.rule->name(); });
        if (!known) model.unknownWeights_.push_back(name);
    }
    return model;
}
