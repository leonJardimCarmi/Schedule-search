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

// ---- the weekly view (step 6.3, D42) ------------------------------------------

TEST_CASE("the report groups shifts into kinds by hours and role", "[Report][D42]") {
    Problem p{kSunday, kSunday + 1, {availableOn(1, {0, 1})},
              {shiftOn("N0", 0, "22:00-06:00", 0, 1, "guard"),   // 0
               shiftOn("M0", 0, "08:00-16:00", 0, 1),            // 1
               shiftOn("M1", 1, "08:00-16:00", 0, 1),            // 2  same kind as M0
               shiftOn("X1", 1, "08:00-16:00", 0, 1, "chef")},   // 3  same hours, other role
              Rules{}};
    const Report r = buildReport(p, State(1, 4), {}, makeScoreModel(p), {});

    REQUIRE(r.kinds.size() == 3);                      // sorted by start time, then role
    CHECK(r.kinds[0].time.toString() == "08:00-16:00");
    CHECK(r.kinds[0].role.empty());
    CHECK(r.kinds[1].role == "chef");
    CHECK(r.kinds[2].role == "guard");
    CHECK(r.kindOf == std::vector<int>{2, 0, 0, 1});
}

TEST_CASE("the report puts each shift in its week and weekday", "[Report][D42]") {
    // 2026-11-11 is a Wednesday: the period starts mid-week and ends in the next week.
    const Date wednesday = kSunday + 3;
    Problem p{wednesday, kSunday + 8, {availableOn(1, {3, 8})},
              {shiftOn("W", 3, "08:00-16:00", 1, 1),     // week 1, Wed
               shiftOn("M", 8, "08:00-16:00", 1, 2)},    // week 2, Mon
              Rules{}};
    State s(1, 2);
    s.assign(0, 0);
    s.assign(0, 1);
    const Report r = buildReport(p, s, {}, makeScoreModel(p), {});

    REQUIRE(r.weeks.size() == 2);
    CHECK(r.weeks[0].sunday == kSunday);             // the Sunday before the start
    CHECK(r.weeks[0].cells[0][3] == std::vector<int>{0});
    CHECK(r.weeks[1].cells[0][1] == std::vector<int>{1});
    CHECK(r.weeks[0].cells[0][0].empty());           // Sunday is outside the period
    CHECK(r.weeks[0].full == 1);
    CHECK(r.weeks[1].belowWanted == 1);
    CHECK(r.weeks[1].shiftsPerEmployee == std::vector<int>{1});
}

TEST_CASE("the report marks preferred assignments and doubles", "[Report][D42]") {
    Employee e = availableOn(1, {0}, AvailabilityType::Preferred);
    e.availability[0].allowsDouble = true;
    Problem p{kSunday, kSunday, {e, availableOn(2, {0})},
              {shiftOn("A", 0, "06:00-14:00", 1, 2), shiftOn("B", 0, "14:00-20:00", 1, 1)}, Rules{}};
    State s(2, 2);
    s.assign(0, 0);   // E1 works A and B: 14 hours in a row, a double
    s.assign(0, 1);
    s.assign(1, 0);   // E2 works only A
    const Report r = buildReport(p, s, {}, makeScoreModel(p), {});

    CHECK(r.shifts[0].preferred == std::vector<char>{1, 0});
    CHECK(r.shifts[0].inDouble == std::vector<char>{1, 0});
    CHECK(r.shifts[1].inDouble == std::vector<char>{1});
}
