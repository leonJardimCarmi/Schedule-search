#include "solver/Score.h"

#include <algorithm>
#include <cmath>
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

// For each employee: how many empty shifts the hard rules still allow them
// in. Each is at most one more shift for them in this branch. Shared by the
// bounds of below-min-shifts and fairness.
void countStillPossible(const Problem& p, const SearchView& v, std::vector<int>& out) {
    out.assign(p.employees.size(), 0);
    for (int i = 0; i < static_cast<int>(p.shifts.size()); ++i) {
        if (v.decided[i]) continue;
        for (std::size_t pos = 0; pos < v.candidates[i].size(); ++pos)
            if (v.allowedNow[i][pos]) ++out[v.candidates[i][pos]];
    }
}

// The most shifts employee e can still end up with in this branch.
int mostShifts(const Problem& p, const State& s, int e, const std::vector<int>& stillPossible) {
    const int has = static_cast<int>(s.shiftsOf(e).size());
    int more = stillPossible[e];
    if (p.employees[e].maxShifts) more = std::min(more, *p.employees[e].maxShifts - has);
    return has + std::max(0, more);
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
        countStillPossible(p, v, stillPossible_);
        long long missing = 0;
        for (int e = 0; e < static_cast<int>(p.employees.size()); ++e)
            if (p.employees[e].minShifts)
                missing += std::max(0, *p.employees[e].minShifts - mostShifts(p, s, e, stillPossible_));
        return missing;
    }

private:
    // Scratch space, reused at every node instead of allocating a new vector.
    // "mutable" because lowerBound() is const: it does not change the rule.
    mutable std::vector<int> stillPossible_;
};

// D17: a penalty for each night shift that follows a night shift the day
// before (by start day, D13). Three nights in a row = 2.
class ConsecutiveNights : public SoftRule {
public:
    explicit ConsecutiveNights(const Problem& p) : night_(p.shifts.size()) {
        for (std::size_t i = 0; i < p.shifts.size(); ++i)
            night_[i] = isNight(p.shifts[i], p.rules);
    }
    const char* name() const override { return "consecutive-nights"; }
    const char* description() const override { return "night shift right after a night shift"; }
    long long violations(const Problem& p, const State& s) const override {
        long long pairs = 0;
        for (int e = 0; e < static_cast<int>(p.employees.size()); ++e) {
            std::vector<int> nightDays;
            for (int i : s.shiftsOf(e))
                if (night_[i]) nightDays.push_back(p.shifts[i].date.daysSinceEpoch());
            std::sort(nightDays.begin(), nightDays.end());
            for (std::size_t k = 1; k < nightDays.size(); ++k)
                if (nightDays[k] == nightDays[k - 1] + 1) ++pairs;
        }
        return pairs;
    }
    // Assigning more shifts can only add pairs, never remove one. So the
    // pairs already in a partial schedule are a correct lower bound.
    long long lowerBound(const Problem& p, const State& s, const SearchView&) const override {
        return violations(p, s);
    }

private:
    // Minutes of the shift inside the night window, counting the window that
    // starts the evening before (for early-morning shifts) and the one that
    // starts on the shift's own day. Absolute minutes, so midnight is no edge.
    static bool isNight(const Shift& shift, const Rules& rules) {
        const int start = shift.absoluteStart(), end = shift.absoluteEnd();
        const int day = shift.date.daysSinceEpoch() * TimePeriod::MINUTES_PER_DAY;
        int inside = 0;
        for (int d = -1; d <= 0; ++d) {
            const int wStart = day + d * TimePeriod::MINUTES_PER_DAY + rules.nightWindow.getStart();
            const int wEnd = day + d * TimePeriod::MINUTES_PER_DAY + rules.nightWindow.getEnd();
            inside += std::max(0, std::min(end, wEnd) - std::max(start, wStart));
        }
        return inside >= rules.nightMinHours * 60;
    }
    std::vector<char> night_;
};

// D49: fewer trips to a remote site. A trip is a run of days in a row on
// which the employee works (by start day, D13); a day off ends it. The ideal
// trip is tripMinDays..tripMaxDays long. Violations, in days:
//   - a trip shorter than the min: the days missing (1 day, min 3 -> 2),
//     except a trip that touches the first or last day of the period, which
//     may go on outside it, so its real length is not known;
//   - a trip longer than the max: the days above it.
// Default weight 0: only a business with a far site turns it on.
class FewerTrips : public SoftRule {
public:
    explicit FewerTrips(const Problem& p)
        : first_(p.periodStart.daysSinceEpoch()), last_(p.periodEnd.daysSinceEpoch()),
          min_(p.rules.tripMinDays), max_(p.rules.tripMaxDays) {}
    const char* name() const override { return "fewer-trips"; }
    const char* description() const override { return "day outside the ideal trip length (days in a row)"; }
    long long violations(const Problem& p, const State& s) const override { return count(p, s, true); }
    // Assigning more shifts only makes trips longer or joins two of them,
    // and the days above the max never shrink: max(0, a+b+1-max) is at least
    // max(0, a-max) + max(0, b-max). A short trip can still grow, so only the
    // "too long" part is a lower bound.
    long long lowerBound(const Problem& p, const State& s, const SearchView&) const override {
        return count(p, s, false);
    }

private:
    long long count(const Problem& p, const State& s, bool shortToo) const {
        long long days = 0;
        std::vector<int> workDays;
        for (int e = 0; e < static_cast<int>(p.employees.size()); ++e) {
            workDays.clear();
            for (int i : s.shiftsOf(e)) workDays.push_back(p.shifts[i].date.daysSinceEpoch());
            std::sort(workDays.begin(), workDays.end());
            workDays.erase(std::unique(workDays.begin(), workDays.end()), workDays.end());   // a double is one day
            for (std::size_t k = 0; k < workDays.size();) {
                std::size_t end = k + 1;   // the trip is workDays[k .. end)
                while (end < workDays.size() && workDays[end] == workDays[end - 1] + 1) ++end;
                const int length = static_cast<int>(end - k);
                if (length > max_) days += length - max_;
                const bool atEdge = workDays[k] == first_ || workDays[end - 1] == last_;
                if (shortToo && length < min_ && !atEdge) days += min_ - length;
                k = end;
            }
        }
        return days;
    }
    int first_, last_, min_, max_;
};

// Fairness: shifts should be spread evenly. Violations = sum of squares of
// each employee's shift count, minus the smallest possible sum for the same
// total. 0 means "as even as possible"; one big gap costs more than several
// small ones (counts 5,5 -> 0; 6,4 -> 2; 8,2 -> 18).
// Only employees who can work at least one shift take part: someone who
// is never available cannot be given more, so counting them is just noise.
class Fairness : public SoftRule {
public:
    explicit Fairness(const Problem& p) {
        for (std::size_t e = 0; e < p.employees.size(); ++e)
            for (const Shift& shift : p.shifts)
                if (availabilityFit(p.employees[e], shift) != Fit::No && roleFits(p.employees[e], shift)) {
                    members_.push_back(static_cast<int>(e));
                    break;
                }
    }
    const char* name() const override { return "fairness"; }
    const char* description() const override { return "unevenness of shifts per employee (squares)"; }
    long long violations(const Problem&, const State& s) const override {
        if (members_.empty()) return 0;
        long long total = 0, squares = 0;
        for (int e : members_) {
            const long long n = static_cast<long long>(s.shiftsOf(e).size());
            total += n;
            squares += n * n;
        }
        const long long count = static_cast<long long>(members_.size());
        const long long q = total / count, r = total % count;   // the most even split
        const long long best = (count - r) * q * q + r * (q + 1) * (q + 1);
        return squares - best;
    }

    // Each member will end with between lo (what they have now) and hi
    // (mostShifts) shifts. With m = total / count, the violations are exactly
    //     sum (x - m)^2  -  r(count - r)/count        (r = total % count)
    // and |x - m| is at least the distance from m to [lo, hi]. We do not know
    // the total yet, so take the m that makes those distances smallest:
    //     violations >= min over m of sum dist(m, [lo,hi])^2  -  count/4
    // (r(count-r)/count is never more than count/4). Near the end of a branch
    // lo and hi meet, and the bound gets close to the real value.
    long long lowerBound(const Problem& p, const State& s, const SearchView& v) const override {
        if (members_.empty()) return 0;
        countStillPossible(p, v, stillPossible_);
        lo_.clear();
        hi_.clear();
        for (int e : members_) {
            lo_.push_back(static_cast<double>(s.shiftsOf(e).size()));
            hi_.push_back(static_cast<double>(mostShifts(p, s, e, stillPossible_)));
        }
        const double spread = smallestSpread();
        const double count = static_cast<double>(members_.size());
        const double slack = std::floor(count / 2) * std::ceil(count / 2) / count;   // max of r(count-r)/count
        // Violations are whole numbers: round up, minus a tiny margin so a
        // rounding error in the doubles can never push the bound too high.
        return std::max(0LL, static_cast<long long>(std::ceil(spread - slack - 1e-6)));
    }

private:
    // min over m of sum dist(m, [lo_i, hi_i])^2, exactly.
    // The function is a sum of parabolas that change only at the interval
    // ends, so between two neighbouring ends it is one parabola: find its
    // lowest point, keep it inside that piece, and take the smallest result.
    double smallestSpread() const {
        std::vector<double> ends(lo_);
        ends.insert(ends.end(), hi_.begin(), hi_.end());
        std::sort(ends.begin(), ends.end());
        auto value = [&](double m) {
            double sum = 0;
            for (std::size_t i = 0; i < lo_.size(); ++i) {
                const double d = m < lo_[i] ? lo_[i] - m : (m > hi_[i] ? m - hi_[i] : 0.0);
                sum += d * d;
            }
            return sum;
        };
        double best = value(ends.front());
        for (std::size_t k = 0; k + 1 < ends.size(); ++k) {
            const double a = ends[k], b = ends[k + 1];
            const double mid = (a + b) / 2;
            // Inside (a, b) the same intervals lie left/right of m: the
            // parabola's lowest point is the mean of their near ends.
            double sum = 0;
            int active = 0;
            for (std::size_t i = 0; i < lo_.size(); ++i) {
                if (hi_[i] <= a) { sum += hi_[i]; ++active; }        // interval left of m
                else if (lo_[i] >= b) { sum += lo_[i]; ++active; }   // interval right of m
            }
            const double m = active == 0 ? mid : std::clamp(sum / active, a, b);
            best = std::min(best, value(m));
        }
        return best;
    }

    std::vector<int> members_;
    mutable std::vector<int> stillPossible_;    // scratch, as in BelowMinShifts
    mutable std::vector<double> lo_, hi_;
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
    add(std::make_unique<ConsecutiveNights>(problem), 3);
    add(std::make_unique<Fairness>(problem), 1);
    add(std::make_unique<FewerTrips>(problem), 0);   // off unless a business turns it on (D49)
    // New soft rules are added here (D32).

    for (const auto& [name, weight] : custom) {
        const bool known = std::any_of(model.entries_.begin(), model.entries_.end(),
            [&](const ScoreModel::Entry& e) { return name == e.rule->name(); });
        if (!known) model.unknownWeights_.push_back(name);
    }
    return model;
}

std::optional<WeightSetting> parseWeightSetting(const std::string& text) {
    const std::size_t eq = text.find('=');
    if (eq == std::string::npos || eq == 0 || eq + 1 == text.size()) return std::nullopt;
    const std::string name = text.substr(0, eq);
    const std::string number = text.substr(eq + 1);
    // Digits only: stoi alone would also accept "8x", " 8" or "-3".
    if (number.size() > 7 || !std::all_of(number.begin(), number.end(), [](char c) { return c >= '0' && c <= '9'; }))
        return std::nullopt;
    const int value = std::stoi(number);
    if (value > kMaxWeight) return std::nullopt;
    return WeightSetting{name, value};
}
