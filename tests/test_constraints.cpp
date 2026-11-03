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

TEST_CASE("the factory always includes no-overlap", "[Constraint]") {
    Problem p{kSunday, kSunday, {}, {}, Rules{}};
    ConstraintList list = makeConstraints(p);
    REQUIRE(list.size() == 1);
    CHECK(std::string(list[0]->name()) == "no-overlap");
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
