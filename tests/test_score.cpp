#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <optional>
#include <random>
#include <string>
#include <vector>

#include "io/JsonLoader.h"
#include "solver/Score.h"
#include "solver/Solver.h"

// The score is computed on a finished schedule, so these tests build a
// State by hand. No search is involved (stage 5.1a does not change it).

namespace {

const Date kSunday = Date::fromString("2026-11-08");

Shift makeShift(const std::string& id, const std::string& time, int min, int wanted) {
    return Shift{id, kSunday, TimePeriod::fromString(time), min, wanted, ""};
}

Employee withSlot(int id, AvailabilityType type, bool allowsDouble = false) {
    Employee e;
    e.id = id;
    e.name = "E" + std::to_string(id);
    e.availability.push_back({kSunday, std::nullopt, type, allowsDouble});
    return e;
}

long long violationsOf(const ScoreModel& model, const Problem& p, const State& s, const std::string& rule) {
    for (const ScoreLine& line : model.breakdown(p, s))
        if (line.rule == rule) return line.violations;
    FAIL("no rule named " << rule);
    return -1;
}

}  // namespace

TEST_CASE("scores compare Medium before Soft", "[Score]") {
    Score fewHoles, manyHoles;
    fewHoles.total = {1, 500};    // 1 hole, many small issues
    manyHoles.total = {2, 0};     // 2 holes, otherwise perfect
    CHECK(fewHoles < manyHoles);  // D29: the Medium level decides first

    Score a, b;
    a.total = {0, 30};
    b.total = {0, 40};
    CHECK(a < b);
    CHECK_FALSE(b < a);
}

TEST_CASE("each soft rule counts its own violations", "[Score]") {
    Problem p{kSunday, kSunday,
              {withSlot(1, AvailabilityType::Preferred), withSlot(2, AvailabilityType::Available, true)},
              {makeShift("A", "06:00-14:00", 1, 3),     // 0
               makeShift("B", "14:00-20:00", 2, 2)},    // 1  back to back with A
              Rules{}};
    p.employees[0].minShifts = 2;
    State s(2, 2);
    s.assign(0, 0);   // E1 (preferred) works A
    s.assign(1, 0);   // E2 (only available) works A ...
    s.assign(1, 1);   // ... and B: 06:00-20:00 = 14 hours, a double
    const ScoreModel model = makeScoreModel(p);

    CHECK(violationsOf(model, p, s, "below-min") == 1);         // B has 1 of min 2
    CHECK(violationsOf(model, p, s, "below-wanted") == 2);      // A 2 of 3, B 1 of 2
    CHECK(violationsOf(model, p, s, "not-preferred") == 2);     // both of E2's shifts
    CHECK(violationsOf(model, p, s, "double") == 1);            // E2's block
    CHECK(violationsOf(model, p, s, "below-min-shifts") == 1);  // E1 has 1 of 2

    // Default weights (D28): below-min 1 (Medium); below-wanted 10, double 5,
    // below-min-shifts 5, not-preferred 1 (Soft).
    const Score score = model.score(p, s);
    CHECK(score.at(Level::Medium) == 1);
    CHECK(score.at(Level::Soft) == 2 * 10 + 1 * 5 + 1 * 5 + 2 * 1);
}

TEST_CASE("weights from the input change the score", "[Score]") {
    Problem p{kSunday, kSunday, {withSlot(1, AvailabilityType::Available)},
              {makeShift("A", "08:00-16:00", 1, 1)}, Rules{}};
    State s(1, 1);
    s.assign(0, 0);   // one assignment that was not preferred

    SECTION("default weight") {
        CHECK(makeScoreModel(p).score(p, s).at(Level::Soft) == 1);
    }
    SECTION("a custom weight multiplies") {
        p.rules.weights["not-preferred"] = 7;
        CHECK(makeScoreModel(p).score(p, s).at(Level::Soft) == 7);
    }
    SECTION("weight 0 turns the rule off") {
        p.rules.weights["not-preferred"] = 0;
        const ScoreModel model = makeScoreModel(p);
        CHECK(model.score(p, s).at(Level::Soft) == 0);
        CHECK(violationsOf(model, p, s, "not-preferred") == 0);   // not even computed
    }
    SECTION("an unknown name is reported") {
        p.rules.weights["dubble"] = 3;
        const ScoreModel model = makeScoreModel(p);
        REQUIRE(model.unknownWeights().size() == 1);
        CHECK(model.unknownWeights()[0] == "dubble");
    }
}

// D32: the model accepts any rule. This one exists only in the test, which
// shows that adding a rule needs no change anywhere else.
TEST_CASE("a new soft rule plugs in without other changes", "[Score]") {
    class CountAssignments : public SoftRule {
    public:
        const char* name() const override { return "test-count"; }
        const char* description() const override { return "assignment"; }
        long long violations(const Problem& p, const State& s) const override {
            long long n = 0;
            for (int i = 0; i < static_cast<int>(p.shifts.size()); ++i) n += s.assignedCount(i);
            return n;
        }
    };
    Problem p{kSunday, kSunday, {withSlot(1, AvailabilityType::Preferred)},
              {makeShift("A", "08:00-16:00", 1, 1)}, Rules{}};
    State s(1, 1);
    s.assign(0, 0);

    ScoreModel model;
    model.add(std::make_unique<CountAssignments>(), 4);
    CHECK(model.score(p, s).at(Level::Soft) == 4);
}

TEST_CASE("the loader reads weights", "[Score][JsonLoader]") {
    const std::string base = R"({
        "period": {"start": "2026-11-08", "end": "2026-11-08"},
        "employees": [], "shifts": [],)";

    SECTION("valid weights are stored") {
        LoadResult r = JsonLoader::loadString(base + R"("weights": {"double": 8, "not-preferred": 0}})", "t.json");
        REQUIRE(r.ok());
        CHECK(r.problem->rules.weights.at("double") == 8);
        CHECK(r.problem->rules.weights.at("not-preferred") == 0);
    }
    SECTION("a negative weight is an error") {
        CHECK_FALSE(JsonLoader::loadString(base + R"("weights": {"double": -1}})", "t.json").ok());
    }
    SECTION("a non-number is an error") {
        CHECK_FALSE(JsonLoader::loadString(base + R"("weights": {"double": "high"}})", "t.json").ok());
    }
}

// ---- findBest: the search uses the score (step 5.2) ---------------------------

TEST_CASE("findBest returns a better schedule than findFirst", "[Solver][Score]") {
    // The search tries E2 first, because E2 marked the day as preferred (D36).
    // But E1 has minShifts 1 and this is the only shift: without it, E1 misses
    // a shift (weight 5), which costs more than a non-preferred assignment (1).
    Problem p{kSunday, kSunday,
              {withSlot(1, AvailabilityType::Available), withSlot(2, AvailabilityType::Preferred)},
              {makeShift("A", "08:00-16:00", 1, 1)}, Rules{}};
    p.employees[0].minShifts = 1;
    const ScoreModel model = makeScoreModel(p);
    Solver solver(p);

    const std::optional<State> first = solver.findFirst();
    REQUIRE(first);
    CHECK(first->staffOf(0) == std::vector<int>{1});           // E2, the preferred one
    CHECK(model.score(p, *first).at(Level::Soft) == 5);        // E1 below minShifts

    const std::optional<State> best = solver.findBest(model);
    REQUIRE(best);
    CHECK(best->staffOf(0) == std::vector<int>{0});            // E1
    CHECK(solver.bestScore().at(Level::Soft) == 1);            // one non-preferred
    CHECK(solver.improvements() == 2);   // E2 first, then E1 was better
    CHECK(solver.complete());            // everything checked: proven best
}

TEST_CASE("candidates who prefer the shift are tried first", "[Solver]") {
    Problem p{kSunday, kSunday,
              {withSlot(1, AvailabilityType::Available), withSlot(2, AvailabilityType::Preferred)},
              {makeShift("A", "08:00-16:00", 1, 1)}, Rules{}};
    Solver solver(p);
    const std::optional<State> first = solver.findFirst();
    REQUIRE(first);
    CHECK(first->staffOf(0) == std::vector<int>{1});   // E2 before E1, despite the input order
}

TEST_CASE("findBest prefers a full shift over many small issues", "[Solver][Score]") {
    // Both available only, so each assignment costs 1 (not-preferred).
    // Two people cost 2, but one person leaves "wanted" short: 10.
    Problem p{kSunday, kSunday,
              {withSlot(1, AvailabilityType::Available), withSlot(2, AvailabilityType::Available)},
              {makeShift("A", "08:00-16:00", 1, 2)}, Rules{}};
    Solver solver(p);
    const std::optional<State> best = solver.findBest(makeScoreModel(p));
    REQUIRE(best);
    CHECK(best->assignedCount(0) == 2);
    CHECK(solver.bestScore().at(Level::Soft) == 2);
}

TEST_CASE("findBest keeps the best so far when a limit stops it (anytime)", "[Solver][Score]") {
    Problem p{kSunday, kSunday,
              {withSlot(1, AvailabilityType::Available), withSlot(2, AvailabilityType::Available),
               withSlot(3, AvailabilityType::Preferred)},
              {makeShift("A", "08:00-16:00", 1, 1), makeShift("B", "16:00-23:00", 1, 1)}, Rules{}};
    const ScoreModel model = makeScoreModel(p);

    Solver full(p);
    REQUIRE(full.findBest(model));
    REQUIRE(full.complete());
    const long long allNodes = full.nodesVisited();

    // The smallest node limit that still finds a schedule: it stops long
    // before the whole tree, so the answer is "best so far", not proven.
    Solver limited(p);
    std::optional<State> soFar;
    for (long long n = 1; n < allNodes && !soFar; ++n) {
        limited.setLimits(SearchLimits{std::nullopt, n});
        soFar = limited.findBest(model);
    }
    REQUIRE(soFar);
    CHECK(limited.status() == SearchStatus::Found);
    CHECK_FALSE(limited.complete());
    CHECK_FALSE(limited.bestScore() < full.bestScore());   // never better than the true best
}

// ---- Branch & Bound (step 5.3) ------------------------------------------------

namespace {

// A small random problem: 3 days, 2-3 shifts a day, 4 employees with mixed
// availability. Small enough for a full search without the bound to finish. std::mt19937 gives the same numbers on every compiler; the
// std:: distributions do not, so plain modulo is used instead.
Problem randomProblem(unsigned seed) {
    std::mt19937 rng(seed);
    auto pick = [&](int n) { return static_cast<int>(rng() % static_cast<unsigned>(n)); };
    const char* times[] = {"06:00-14:00", "14:00-22:00", "22:00-06:00"};

    Problem p{kSunday, kSunday + 2, {}, {}, Rules{}};
    for (int day = 0; day < 3; ++day)
        for (int t = 0; t < 2 + pick(2); ++t) {
            const int min = pick(2);   // 0 or 1
            p.shifts.push_back(Shift{"D" + std::to_string(day) + "-" + std::to_string(t), kSunday + day,
                                     TimePeriod::fromString(times[t]), min, min + pick(2), ""});
        }
    for (int id = 1; id <= 4; ++id) {
        Employee e;
        e.id = id;
        e.name = "E" + std::to_string(id);
        if (pick(2)) e.minShifts = 1 + pick(3);
        for (int day = 0; day < 3; ++day) {
            const int kind = pick(4);   // 0 off, 1 available, 2 preferred, 3 available + double ok
            if (kind == 0) continue;
            e.availability.push_back({kSunday + day, std::nullopt,
                                      kind == 2 ? AvailabilityType::Preferred : AvailabilityType::Available,
                                      kind == 3});
        }
        p.employees.push_back(e);
    }
    return p;
}

}  // namespace

TEST_CASE("the bound never loses the best schedule", "[Solver][Score][BranchAndBound]") {
    // The bound is only allowed to skip branches that cannot win. So with and
    // without it, the best score must be exactly the same, every time.
    for (unsigned seed = 1; seed <= 60; ++seed) {
        CAPTURE(seed);
        const Problem p = randomProblem(seed);
        const ScoreModel model = makeScoreModel(p);

        Solver plain(p);
        plain.setUseBound(false);
        const std::optional<State> slow = plain.findBest(model);

        Solver pruning(p);
        const std::optional<State> fast = pruning.findBest(model);

        REQUIRE(slow.has_value() == fast.has_value());
        if (!slow) continue;
        CHECK(pruning.bestScore() == plain.bestScore());
        CHECK(model.score(p, *fast) == pruning.bestScore());
        CHECK(pruning.nodesVisited() <= plain.nodesVisited());
    }
}

// ---- consecutive-nights and fairness (step 5.1b) ------------------------------

namespace {

Employee freeAllWeek(int id) {
    Employee e;
    e.id = id;
    e.name = "E" + std::to_string(id);
    for (int d = 0; d < 7; ++d) e.availability.push_back({kSunday + d, std::nullopt, AvailabilityType::Available});
    return e;
}

Shift shiftOn(const std::string& id, int day, const std::string& time) {
    return Shift{id, kSunday + day, TimePeriod::fromString(time), 0, 1, ""};
}

}  // namespace

TEST_CASE("consecutive-nights counts each night right after a night", "[Score]") {
    Problem p{kSunday, kSunday + 6, {freeAllWeek(1)},
              {shiftOn("N0", 0, "22:00-06:00"),     // 0  night
               shiftOn("N1", 1, "22:00-06:00"),     // 1  night
               shiftOn("N2", 2, "22:00-06:00"),     // 2  night
               shiftOn("E3", 3, "16:00-23:00"),     // 3  only 1 h in 22-06: not a night
               shiftOn("L4", 4, "20:00-02:00"),     // 4  4 h in 22-06: a night
               shiftOn("M5", 5, "02:00-10:00")},    // 5  4 h in 22-06 (the window of the evening before)
              Rules{}};
    State s(1, 6);
    for (int i = 0; i < 6; ++i) s.assign(0, i);
    const ScoreModel model = makeScoreModel(p);
    // Nights on days 0,1,2,4,5: pairs (0,1) (1,2) (4,5) = 3. Day 3 breaks the run.
    CHECK(violationsOf(model, p, s, "consecutive-nights") == 3);

    SECTION("nightMinHours decides what is a night") {
        p.rules.nightMinHours = 5;   // now only the full 22-06 shifts are nights
        const ScoreModel strict = makeScoreModel(p);
        CHECK(violationsOf(strict, p, s, "consecutive-nights") == 2);   // (0,1) (1,2)
    }
}

TEST_CASE("fairness is 0 for the most even split and grows with the square", "[Score]") {
    // 10 shifts at different times of one week, two employees free all week.
    Problem p{kSunday, kSunday + 6, {freeAllWeek(1), freeAllWeek(2)}, {}, Rules{}};
    for (int i = 0; i < 10; ++i)
        p.shifts.push_back(shiftOn("S" + std::to_string(i), i % 7, i < 7 ? "08:00-12:00" : "18:00-22:00"));
    const ScoreModel model = makeScoreModel(p);

    auto split = [&](int forFirst) {
        State s(2, 10);
        for (int i = 0; i < 10; ++i) s.assign(i < forFirst ? 0 : 1, i);
        return violationsOf(model, p, s, "fairness");
    };
    CHECK(split(5) == 0);    // 5 + 5
    CHECK(split(6) == 2);    // 6 + 4: 36 + 16 - 50
    CHECK(split(8) == 18);   // 8 + 2: 64 + 4 - 50
    CHECK(split(10) == 50);  // 10 + 0
}

TEST_CASE("fairness ignores employees who can work no shift", "[Score]") {
    Employee never;
    never.id = 9;
    never.name = "Never";
    Problem p{kSunday, kSunday, {freeAllWeek(1), never}, {shiftOn("A", 0, "08:00-16:00")}, Rules{}};
    State s(2, 1);
    s.assign(0, 0);
    CHECK(violationsOf(makeScoreModel(p), p, s, "fairness") == 0);   // E1 alone is "even"
}

TEST_CASE("the loader reads the night rules", "[Score][JsonLoader]") {
    const std::string base = R"({
        "period": {"start": "2026-11-08", "end": "2026-11-08"},
        "employees": [], "shifts": [],)";
    SECTION("custom values") {
        LoadResult r = JsonLoader::loadString(base + R"("rules": {"nightWindow": "23:00-07:00", "nightMinHours": 3}})", "t.json");
        REQUIRE(r.ok());
        CHECK(r.problem->rules.nightWindow.toString() == "23:00-07:00");
        CHECK(r.problem->rules.nightMinHours == 3);
    }
    SECTION("nightMinHours must fit inside the window") {
        CHECK_FALSE(JsonLoader::loadString(base + R"("rules": {"nightWindow": "23:00-01:00", "nightMinHours": 3}})", "t.json").ok());
    }
}

TEST_CASE("the fairness bound is exact when every shift is decided", "[Score][BranchAndBound]") {
    Problem p{kSunday, kSunday + 6, {freeAllWeek(1), freeAllWeek(2)}, {}, Rules{}};
    for (int i = 0; i < 10; ++i)
        p.shifts.push_back(shiftOn("S" + std::to_string(i), i % 7, i < 7 ? "08:00-12:00" : "18:00-22:00"));
    // Only fairness counts here: every other rule gets weight 0 (D28).
    for (const char* other : {"below-min", "below-wanted", "double", "below-min-shifts",
                              "not-preferred", "consecutive-nights"})
        p.rules.weights[other] = 0;
    const ScoreModel model = makeScoreModel(p);

    // Everything decided: nobody can get another shift, so lo == hi.
    const std::vector<char> decided(10, 1);
    std::vector<std::vector<int>> candidates(10, std::vector<int>{0, 1});
    std::vector<std::vector<char>> allowedNow(10, std::vector<char>{0, 0});
    const std::vector<int> allowedCount(10, 0), searchMin(10, 0);
    const SearchView view{decided, candidates, allowedNow, allowedCount, searchMin};

    for (int forFirst : {5, 6, 8, 10}) {
        CAPTURE(forFirst);
        State s(2, 10);
        for (int i = 0; i < 10; ++i) s.assign(i < forFirst ? 0 : 1, i);
        const Score bound = model.lowerBound(p, s, view);
        const Score real = model.score(p, s);
        CHECK(bound.at(Level::Soft) == real.at(Level::Soft));
    }

    SECTION("with shifts still open the bound stays at or below the real value") {
        // 6 decided for E1, 4 still open and allowed for both.
        std::vector<char> someOpen(10, 1);
        std::vector<std::vector<char>> open(10, std::vector<char>{0, 0});
        for (int i = 6; i < 10; ++i) { someOpen[i] = 0; open[i] = {1, 1}; }
        const std::vector<int> two(10, 2), one(10, 1);
        const SearchView partial{someOpen, candidates, open, two, one};
        State s(2, 10);
        for (int i = 0; i < 6; ++i) s.assign(0, i);
        // Best completion: all 4 open shifts to E2 -> 6 + 4 -> fairness 2.
        // The bound may be lower, never higher.
        CHECK(model.lowerBound(p, s, partial).at(Level::Soft) <= 2);
    }
}
