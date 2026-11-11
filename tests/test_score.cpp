#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <string>

#include "io/JsonLoader.h"
#include "solver/Score.h"

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
