#include <catch2/catch_test_macros.hpp>

#include <vector>

#include "report/ReportModel.h"

// The report model holds numbers and indices, not text (D41), so these
// tests check facts directly, without parsing any output.

namespace {

const Date kSunday = Date::fromString("2026-11-08");

Employee availableOn(int id, std::initializer_list<int> days, AvailabilityType type = AvailabilityType::Available) {
    Employee e;
    e.id = id;
    e.name = "E" + std::to_string(id);
    for (int d : days) e.availability.push_back({kSunday + d, std::nullopt, type});
    return e;
}

Shift shiftOn(const std::string& id, int day, const std::string& time, int min, int wanted,
              const std::string& role = "") {
    return Shift{id, kSunday + day, TimePeriod::fromString(time), min, wanted, role};
}

}  // namespace

TEST_CASE("the report gives each shift its staff and status", "[Report]") {
    Problem p{kSunday, kSunday, {availableOn(1, {0}), availableOn(2, {0})},
              {shiftOn("A", 0, "06:00-14:00", 1, 2),     // 0
               shiftOn("B", 0, "14:00-22:00", 1, 1),     // 1
               shiftOn("C", 0, "22:00-06:00", 1, 1)},    // 2
              Rules{}};
    State s(2, 3);
    s.assign(0, 0);   // A: 1 of 2 -> below wanted
    s.assign(1, 1);   // B: full
                      // C: nobody -> below min
    const Report r = buildReport(p, s, {}, makeScoreModel(p), {});

    REQUIRE(r.shifts.size() == 3);
    CHECK(r.shifts[0].status == ShiftReport::Status::BelowWanted);
    CHECK(r.shifts[1].status == ShiftReport::Status::Full);
    CHECK(r.shifts[2].status == ShiftReport::Status::BelowMin);
    CHECK(r.shifts[0].staff == std::vector<int>{0});
    CHECK(r.shifts[2].staff.empty());
}

TEST_CASE("the report sums each employee's shifts and hours in calendar order", "[Report]") {
    Problem p{kSunday, kSunday + 1, {availableOn(1, {0, 1}), availableOn(2, {0, 1})},
              {shiftOn("A", 0, "08:00-16:00", 1, 1),     // 0: 8 h
               shiftOn("B", 1, "08:00-12:00", 1, 1)},    // 1: 4 h
              Rules{}};
    p.employees[0].minShifts = 2;
    p.employees[1].minShifts = 1;
    State s(2, 2);
    s.assign(0, 1);   // assigned out of calendar order, as MRV may do
    s.assign(0, 0);
    const Report r = buildReport(p, s, {}, makeScoreModel(p), {});

    REQUIRE(r.employees.size() == 2);
    CHECK(r.employees[0].shifts == std::vector<int>{0, 1});   // calendar order
    CHECK(r.employees[0].minutes == 12 * 60);
    CHECK_FALSE(r.employees[0].belowMinShifts);               // has 2 of 2
    CHECK(r.employees[1].shifts.empty());
    CHECK(r.employees[1].belowMinShifts == 1);                // wanted 1, got 0
}

TEST_CASE("the report gives each shortage its reason", "[Report][D25]") {
    Problem p{kSunday, kSunday + 1, {availableOn(1, {0}), Employee{}}, {}, Rules{}};
    p.employees[1].id = 2;
    p.employees[1].name = "never";
    p.shifts = {shiftOn("BIG", 0, "08:00-16:00", 2, 2),             // 1 available, needs 2
                shiftOn("N", 0, "22:00-06:00", 1, 1, "guard"),      // available, but no guard
                shiftOn("X", 1, "08:00-16:00", 1, 1)};              // nobody on Monday
    Solver solver(p);
    const Report r = buildReport(p, State(2, 3), solver.shortages(), makeScoreModel(p), {});

    REQUIRE(r.shortages.size() == 3);
    CHECK(r.shortages[0].reason == ShortageReport::Reason::TooFewAvailable);
    CHECK(r.shortages[0].available == 1);
    CHECK(r.shortages[0].needed == 2);
    CHECK(r.shortages[1].reason == ShortageReport::Reason::MissingRole);
    CHECK(r.shortages[1].candidates == 0);
    CHECK(r.shortages[2].reason == ShortageReport::Reason::NobodyAvailable);
}

TEST_CASE("the report keeps the score and its breakdown", "[Report]") {
    Problem p{kSunday, kSunday, {availableOn(1, {0})}, {shiftOn("A", 0, "08:00-16:00", 1, 2)}, Rules{}};
    State s(1, 1);
    s.assign(0, 0);
    const ScoreModel model = makeScoreModel(p);
    const Report r = buildReport(p, s, {}, model, {});

    CHECK(r.score == model.score(p, s));
    CHECK(r.scoreLines.size() == model.breakdown(p, s).size());
}

TEST_CASE("the report says who leaves and who joins in each alternative", "[Report][D39]") {
    Problem p{kSunday, kSunday, {availableOn(1, {0}), availableOn(2, {0}), availableOn(3, {0})},
              {shiftOn("A", 0, "08:00-16:00", 1, 1), shiftOn("B", 0, "16:00-23:00", 1, 1)}, Rules{}};
    State best(3, 2), other(3, 2);
    best.assign(0, 0);  best.assign(1, 1);     // A: E1, B: E2
    other.assign(2, 0); other.assign(1, 1);    // A: E3, B: E2 (same)
    const ScoreModel model = makeScoreModel(p);
    const std::vector<RankedSchedule> top{{best, model.score(p, best)}, {other, model.score(p, other)}};
    const Report r = buildReport(p, best, {}, model, top);

    REQUIRE(r.alternatives.size() == 1);   // top[0] is the best itself
    const AlternativeReport& alt = r.alternatives[0];
    CHECK(alt.differences == 1);
    REQUIRE(alt.changes.size() == 1);      // B did not change
    CHECK(alt.changes[0].shift == 0);
    CHECK(alt.changes[0].out == std::vector<int>{0});
    CHECK(alt.changes[0].in == std::vector<int>{2});
}
