#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "solver/Constraint.h"
#include "solver/Solver.h"
#include "solver/WorkBlocks.h"

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

TEST_CASE("an optional rule is added only when the input sets it", "[Constraint]") {
    Problem p{kSunday, kSunday, {}, {}, Rules{}};
    CHECK(makeConstraints(p).size() == 4);          // maxConsecutiveDays not set

    p.rules.maxConsecutiveDays = 5;
    ConstraintList list = makeConstraints(p);
    REQUIRE(list.size() == 5);
    CHECK(std::string(list[4]->name()) == "max-consecutive-days");
}

TEST_CASE("the factory builds the rules", "[Constraint]") {
    Problem p{kSunday, kSunday, {}, {}, Rules{}};
    ConstraintList list = makeConstraints(p);
    REQUIRE(list.size() == 4);
    CHECK(std::string(list[0]->name()) == "no-overlap");
    CHECK(std::string(list[1]->name()) == "min-rest");
    CHECK(std::string(list[2]->name()) == "double-needs-approval");
    CHECK(std::string(list[3]->name()) == "max-shifts");
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

// ---- Work blocks (DECISIONS D14) --------------------------------------------

TEST_CASE("buildBlocks joins adjacent shifts and splits the rest", "[WorkBlocks][D14]") {
    Problem p{kSunday, kMonday, {},
              {makeShift("M", kSunday, "08:00-16:00"),    // 0
               makeShift("E", kSunday, "16:00-00:00"),    // 1  starts when M ends
               makeShift("L", kSunday, "17:00-23:00"),    // 2  1 hour after M
               makeShift("X", kMonday, "10:00-14:00")},   // 3
              Rules{}};

    SECTION("no gap allowed: only touching shifts join") {
        auto blocks = buildBlocks(p, {3, 1, 0}, 0);   // any input order
        REQUIRE(blocks.size() == 2);
        CHECK(blocks[0].shifts == std::vector<int>{0, 1});   // M + E = one block
        CHECK(blocks[0].minutes() == 16 * 60);
        CHECK(blocks[1].shifts == std::vector<int>{3});
    }

    SECTION("a one-hour gap splits blocks when blockGapMinutes is 0") {
        auto blocks = buildBlocks(p, {0, 2}, 0);
        CHECK(blocks.size() == 2);
    }

    SECTION("...and joins them when blockGapMinutes is 60") {
        auto blocks = buildBlocks(p, {0, 2}, 60);
        REQUIRE(blocks.size() == 1);
        CHECK(blocks[0].minutes() == 15 * 60);   // 08:00 to 23:00
    }
}

// ---- min-rest (DECISIONS D15) ------------------------------------------------

TEST_CASE("min-rest", "[Constraint][min-rest]") {
    Problem p{kSunday, kMonday, {freeOn(1, {kSunday, kMonday})},
              {makeShift("SUN-M", kSunday, "08:00-16:00"),    // 0
               makeShift("SUN-E", kSunday, "16:00-00:00"),    // 1
               makeShift("SUN-N", kSunday, "22:00-06:00"),    // 2  ends Mon 06:00
               makeShift("MON-M", kMonday, "08:00-16:00"),    // 3
               makeShift("MON-E", kMonday, "16:00-00:00"),    // 4
               makeShift("MON-L", kMonday, "10:00-14:00")},   // 5
              Rules{}};
    MinRestConstraint rule(10, 0);   // the defaults
    State state(1, 6);

    SECTION("evening then next morning is only 8 hours: rejected") {
        state.assign(0, 1);                          // SUN-E ends 00:00
        CHECK_FALSE(rule.allows(p, state, 0, 3));    // MON-M starts 08:00
    }

    SECTION("night then next morning is only 2 hours: rejected") {
        state.assign(0, 2);                          // SUN-N ends Mon 06:00
        CHECK_FALSE(rule.allows(p, state, 0, 3));    // MON-M 08:00
    }

    SECTION("night then next evening is 10 hours: allowed") {
        state.assign(0, 2);                          // SUN-N ends Mon 06:00
        CHECK(rule.allows(p, state, 0, 4));          // MON-E 16:00
    }

    SECTION("morning then evening of the same day is one block: no rest needed") {
        state.assign(0, 0);                          // SUN-M 08-16
        CHECK(rule.allows(p, state, 0, 1));          // SUN-E 16-00 (a double; step 2.3 decides)
    }

    SECTION("the new shift is checked against blocks on both sides") {
        state.assign(0, 0);                          // SUN-M ends Sun 16:00
        state.assign(0, 4);                          // MON-E starts Mon 16:00
        CHECK_FALSE(rule.allows(p, state, 0, 5));    // MON-L 10-14: only 2h before MON-E
    }

    SECTION("a smaller minRestHours allows more") {
        MinRestConstraint relaxed(8, 0);
        state.assign(0, 1);                          // SUN-E ends 00:00
        CHECK(relaxed.allows(p, state, 0, 3));       // exactly 8 hours: allowed
    }
}

// ---- double-needs-approval (DECISIONS D16) ----------------------------------

namespace {

// An employee available all of Sunday 08:00-00:00, optionally approving a double.
Employee sundayWorker(bool approvesDouble) {
    Employee e;
    e.id = 1;
    e.name = "W";
    e.availability.push_back({kSunday, TimePeriod::fromString("08:00-00:00"),
                              AvailabilityType::Available, approvesDouble});
    return e;
}

}  // namespace

TEST_CASE("double-needs-approval", "[Constraint][double]") {
    const std::vector<Shift> shifts = {makeShift("M", kSunday, "08:00-16:00"),    // 0
                                       makeShift("E", kSunday, "16:00-00:00"),    // 1
                                       makeShift("A", kSunday, "08:00-12:00"),    // 2
                                       makeShift("B", kSunday, "12:00-16:00")};   // 3
    DoubleShiftConstraint rule(10, 16, 0);   // the defaults

    SECTION("a short block is not a double: no approval needed") {
        Problem p{kSunday, kSunday, {sundayWorker(false)}, shifts, Rules{}};
        State state(1, 4);
        state.assign(0, 2);                        // A 08-12
        CHECK(rule.allows(p, state, 0, 3));        // + B 12-16 = 8 hours
    }

    SECTION("a 16-hour block without approval is rejected") {
        Problem p{kSunday, kSunday, {sundayWorker(false)}, shifts, Rules{}};
        State state(1, 4);
        state.assign(0, 0);                        // M 08-16
        CHECK_FALSE(rule.allows(p, state, 0, 1));  // + E 16-00
    }

    SECTION("...and allowed with approval") {
        Problem p{kSunday, kSunday, {sundayWorker(true)}, shifts, Rules{}};
        State state(1, 4);
        state.assign(0, 0);
        CHECK(rule.allows(p, state, 0, 1));
    }

    SECTION("approval must cover the whole block") {
        Employee e;
        e.id = 1;
        e.name = "W";
        e.availability.push_back({kSunday, TimePeriod::fromString("08:00-20:00"),
                                  AvailabilityType::Available, true});   // ends at 20:00
        Problem p{kSunday, kSunday, {e}, shifts, Rules{}};
        State state(1, 4);
        state.assign(0, 0);
        CHECK_FALSE(rule.allows(p, state, 0, 1));  // the block runs until 00:00
    }

    SECTION("never longer than maxDoubleHours, even with approval") {
        DoubleShiftConstraint strict(10, 12, 0);   // business allows at most 12 hours
        Problem p{kSunday, kSunday, {sundayWorker(true)}, shifts, Rules{}};
        State state(1, 4);
        state.assign(0, 0);
        CHECK_FALSE(strict.allows(p, state, 0, 1));   // 16 > 12
    }
}

// ---- max-shifts --------------------------------------------------------------

TEST_CASE("max-shifts", "[Constraint][max-shifts]") {
    Employee limited = freeOn(1, {kSunday, kMonday});
    limited.maxShifts = 1;
    Employee unlimited = freeOn(2, {kSunday, kMonday});   // no maxShifts

    Problem p{kSunday, kMonday, {limited, unlimited},
              {makeShift("SUN", kSunday, "08:00-16:00"), makeShift("MON", kMonday, "08:00-16:00")},
              Rules{}};
    MaxShiftsConstraint rule;
    State state(2, 2);

    CHECK(rule.allows(p, state, 0, 0));          // first shift: fine
    state.assign(0, 0);
    CHECK_FALSE(rule.allows(p, state, 0, 1));    // a second one: over the limit

    state.assign(1, 0);
    CHECK(rule.allows(p, state, 1, 1));          // no limit at all
}

// ---- max-consecutive-days (DECISIONS D13) ------------------------------------

TEST_CASE("max-consecutive-days", "[Constraint][consecutive]") {
    // One morning shift on each of 7 days, Sunday (0) to Saturday (6),
    // plus a Sunday night shift (7) that reaches into Monday.
    std::vector<Shift> shifts;
    for (int d = 0; d < 7; ++d)
        shifts.push_back(makeShift("D" + std::to_string(d), kSunday + d, "08:00-16:00"));
    shifts.push_back(makeShift("SUN-N", kSunday, "22:00-06:00"));

    Problem p{kSunday, kSunday + 6, {freeOn(1, {kSunday})}, shifts, Rules{}};
    State state(1, 8);

    SECTION("five days in a row are fine, a sixth is not") {
        MaxConsecutiveDaysConstraint rule(5);
        for (int d = 0; d < 5; ++d) {
            CHECK(rule.allows(p, state, 0, d));
            state.assign(0, d);
        }
        CHECK_FALSE(rule.allows(p, state, 0, 5));   // Friday would be day 6
    }

    SECTION("a free day breaks the run") {
        MaxConsecutiveDaysConstraint rule(3);
        state.assign(0, 0);                          // Sun
        state.assign(0, 1);                          // Mon
        state.assign(0, 3);                          // Wed (Tue is free)
        state.assign(0, 4);                          // Thu
        CHECK(rule.allows(p, state, 0, 5));          // Fri: Wed-Thu-Fri = 3
    }

    SECTION("filling a gap joins two runs") {
        MaxConsecutiveDaysConstraint rule(4);
        state.assign(0, 0);                          // Sun
        state.assign(0, 1);                          // Mon
        state.assign(0, 3);                          // Wed
        state.assign(0, 4);                          // Thu
        CHECK_FALSE(rule.allows(p, state, 0, 2));    // Tue would make Sun..Thu = 5
    }

    SECTION("a night shift counts on both days (D13)") {
        MaxConsecutiveDaysConstraint rule(2);
        state.assign(0, 7);                          // SUN-N: Sunday + Monday
        CHECK_FALSE(rule.allows(p, state, 0, 2));    // Tue morning: Sun..Tue = 3
    }
}
