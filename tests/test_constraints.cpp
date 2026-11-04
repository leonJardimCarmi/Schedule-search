#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "solver/Constraint.h"
#include "solver/Solver.h"

namespace {

const Date kSunday = Date::fromString("2026-11-08");
const Date kMonday = Date::fromString("2026-11-09");

Shift makeShift(const std::string& id, const Date& date, const std::string& time) {
    return Shift{id, date, TimePeriod::fromString(time), 1, 1, ""};
}

Employee freeOn(int id, std::initializer_list<Date> days) {
    Employee e;
    e.id = id;
    e.name = "E" + std::to_string(id);
    for (const Date& d : days) e.availability.push_back({d, std::nullopt, AvailabilityType::Available});
    return e;
}

}  // namespace

// ---- The rule on its own -----------------------------------------------------
// A constraint only needs a Problem and a State, so it can be tested
// without running any search.

TEST_CASE("no-overlap: shifts at the same time are rejected", "[Constraint][no-overlap]") {
    Problem p{kSunday, kMonday, {freeOn(1, {kSunday, kMonday}), freeOn(2, {kSunday})},
              {makeShift("A", kSunday, "08:00-16:00"),     // 0
               makeShift("B", kSunday, "12:00-20:00"),     // 1  overlaps A
               makeShift("C", kSunday, "16:00-23:00"),     // 2  starts when A ends
               makeShift("N", kSunday, "22:00-06:00"),     // 3  overnight
               makeShift("E", kMonday, "05:00-09:00")},    // 4  overlaps N after midnight
              Rules{}};
    NoOverlapConstraint rule;
    State state(2, 5);

    state.assign(0, 0);   // employee 0 works A
    CHECK_FALSE(rule.allows(p, state, 0, 1));   // B overlaps A
    CHECK(rule.allows(p, state, 0, 2));         // C only touches A
    CHECK(rule.allows(p, state, 1, 1));         // another employee is free

    state.assign(0, 3);   // employee 0 also works N (Sun 22:00 - Mon 06:00)
    CHECK_FALSE(rule.allows(p, state, 0, 4));   // Mon 05:00 is still inside N
}

TEST_CASE("the factory builds the rules", "[Constraint]") {
    Problem p{kSunday, kSunday, {}, {}, Rules{}};
    ConstraintList list = makeConstraints(p);
    REQUIRE(list.size() == 2);
    CHECK(std::string(list[0]->name()) == "no-overlap");
    CHECK(std::string(list[1]->name()) == "max-shifts-per-day");
}

// ---- The rule inside the search ----------------------------------------------

TEST_CASE("the solver respects no-overlap", "[Solver][no-overlap]") {
    const std::vector<Shift> overlapping = {makeShift("A", kSunday, "08:00-16:00"),
                                            makeShift("B", kSunday, "12:00-20:00")};

    SECTION("one person cannot cover two overlapping shifts") {
        Problem p{kSunday, kSunday, {freeOn(1, {kSunday})}, overlapping, Rules{}};
        Solver solver(p);
        CHECK(solver.candidatesOf(0).size() == 1);   // available for both...
        CHECK(solver.candidatesOf(1).size() == 1);
        CHECK_FALSE(solver.findFirst().has_value());  // ...but not at once
    }

    SECTION("two people split them") {
        Problem p{kSunday, kSunday, {freeOn(1, {kSunday}), freeOn(2, {kSunday})}, overlapping, Rules{}};
        Solver solver(p);
        auto result = solver.findFirst();
        REQUIRE(result.has_value());
        CHECK(result->staffOf(0) == std::vector<int>{0});
        CHECK(result->staffOf(1) == std::vector<int>{1});
    }
}

// ---- Which days a shift touches (DECISIONS D13) ------------------------------

TEST_CASE("lastDay: night shifts reach the next day, midnight endings do not", "[Shift][D13]") {
    CHECK(makeShift("M", kSunday, "08:00-16:00").lastDay() == kSunday);
    CHECK(makeShift("E", kSunday, "16:00-00:00").lastDay() == kSunday);   // ends exactly at 00:00
    CHECK(makeShift("N", kSunday, "22:00-06:00").lastDay() == kMonday);
}

// ---- max-shifts-per-day ------------------------------------------------------

TEST_CASE("max-shifts-per-day", "[Constraint][max-shifts-per-day]") {
    Problem p{kSunday, kMonday, {freeOn(1, {kSunday, kMonday})},
              {makeShift("SUN-M", kSunday, "08:00-16:00"),    // 0
               makeShift("SUN-E", kSunday, "16:00-00:00"),    // 1
               makeShift("SUN-N", kSunday, "22:00-06:00"),    // 2  Sunday + Monday
               makeShift("MON-M", kMonday, "08:00-16:00"),    // 3
               makeShift("MON-E", kMonday, "16:00-00:00")},   // 4
              Rules{}};
    State state(1, 5);

    SECTION("with max 1, a second shift on the same day is rejected") {
        MaxShiftsPerDayConstraint rule(1);
        state.assign(0, 0);                          // SUN-M
        CHECK_FALSE(rule.allows(p, state, 0, 1));    // SUN-E: also Sunday
        CHECK(rule.allows(p, state, 0, 3));          // MON-M: another day
    }

    SECTION("an evening shift ending at midnight does not use up the next day") {
        MaxShiftsPerDayConstraint rule(1);
        state.assign(0, 1);                          // SUN-E (ends 00:00)
        CHECK(rule.allows(p, state, 0, 3));          // MON-M is fine
    }

    SECTION("a night shift counts on both days") {
        MaxShiftsPerDayConstraint rule(1);
        state.assign(0, 2);                          // SUN-N
        CHECK_FALSE(rule.allows(p, state, 0, 4));    // MON-E: Monday is taken by SUN-N
    }

    SECTION("a night shift is rejected if either of its days is full") {
        MaxShiftsPerDayConstraint rule(1);
        state.assign(0, 3);                          // MON-M
        CHECK_FALSE(rule.allows(p, state, 0, 2));    // SUN-N would touch Monday too
    }

    SECTION("with max 2, two shifts a day are fine, three are not") {
        MaxShiftsPerDayConstraint rule(2);
        state.assign(0, 0);                          // SUN-M
        CHECK(rule.allows(p, state, 0, 1));          // SUN-E
        state.assign(0, 1);
        CHECK_FALSE(rule.allows(p, state, 0, 2));    // SUN-N would be the third on Sunday
    }
}
