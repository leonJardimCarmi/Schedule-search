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

// ---- --explain (step 6.4, D47) -------------------------------------------------

TEST_CASE("explainAssignment checks availability, role and every rule", "[Report][D47]") {
    Problem p{kSunday, kSunday + 1, {availableOn(1, {0, 1}), availableOn(2, {0})},
              {shiftOn("E", 0, "16:00-23:00", 1, 1),             // 0
               shiftOn("M", 1, "06:00-14:00", 1, 1),             // 1  only 7h after E
               shiftOn("G", 1, "06:00-14:00", 1, 1, "guard")},   // 2  needs a role
              Rules{}};
    State s(2, 3);
    s.assign(0, 0);   // E1 works E
    const ConstraintList rules = makeConstraints(p);

    SECTION("blocked by a hard rule, with its reason") {
        const Explanation e = explainAssignment(p, s, rules, 0, 1);
        CHECK_FALSE(e.canWork());
        CHECK(e.availability == Fit::Yes);
        CHECK(e.roleOk);
        bool sawMinRest = false;
        for (const RuleCheck& c : e.rules) {
            if (c.rule == "min-rest") {
                sawMinRest = true;
                CHECK(c.whyNot == std::optional<std::string>("only 7h of rest between E and M, needs 10h"));
            } else {
                CHECK_FALSE(c.whyNot);
            }
        }
        CHECK(sawMinRest);
    }
    SECTION("not available, and no role") {
        const Explanation e = explainAssignment(p, s, rules, 1, 2);   // E2 is free only on Sunday
        CHECK(e.availability == Fit::No);
        CHECK_FALSE(e.roleOk);
        CHECK_FALSE(e.canWork());
    }
    SECTION("someone who already works the shift is checked as if they did not") {
        const Explanation e = explainAssignment(p, s, rules, 0, 0);
        CHECK(e.alreadyWorks);
        CHECK(e.canWork());   // without the shift itself, nothing overlaps it
        CHECK(e.staff == std::vector<int>{0});
    }
}

TEST_CASE("the command line finds employees by id or name and shifts by id", "[Problem][D47]") {
    Problem p{kSunday, kSunday, {availableOn(101, {0}), availableOn(102, {0})}, {shiftOn("SUN-M", 0, "08:00-16:00", 1, 1)},
              Rules{}};
    p.employees[1].name = "Dana Levi";
    CHECK(p.findEmployee(std::string("101")) == std::optional<std::size_t>(0));
    CHECK(p.findEmployee(std::string("dana LEVI")) == std::optional<std::size_t>(1));
    CHECK_FALSE(p.findEmployee(std::string("Dana")));       // the full name, not a part of it
    CHECK_FALSE(p.findEmployee(std::string("999")));
    CHECK(p.findShift("SUN-M") == std::optional<std::size_t>(0));
    CHECK_FALSE(p.findShift("sun-m"));                      // shift ids are exact
}

// ---- --check (stage 7.2, D51) ----------------------------------------------------

namespace {

std::vector<std::string> describe(const Problem& p, const CheckReport& c) {
    std::vector<std::string> out;
    for (const Violation& v : c.violations) {
        const std::string who = p.employees[v.employee].name + " " + p.shifts[v.shift].id + ": ";
        switch (v.kind) {
        case Violation::Kind::Availability: out.push_back(who + "availability"); break;
        case Violation::Kind::Role: out.push_back(who + "role"); break;
        case Violation::Kind::Rule: out.push_back(who + v.rule); break;
        }
    }
    return out;
}

}  // namespace

TEST_CASE("checkSchedule finds what a hand-made schedule breaks, each once", "[Report][D51]") {
    // E1 and E2 free on days 0..1; E3 never available. G needs the role "guard".
    Problem p{kSunday, kSunday + 1, {availableOn(1, {0, 1}), availableOn(2, {0, 1}), availableOn(3, {})},
              {shiftOn("A", 0, "08:00-16:00", 1, 1),             // 0
               shiftOn("B", 0, "12:00-20:00", 1, 1),             // 1  overlaps A
               shiftOn("N", 0, "16:00-00:00", 1, 1),             // 2
               shiftOn("M", 1, "06:00-14:00", 1, 1),             // 3  6h after N: too little rest
               shiftOn("G", 1, "16:00-22:00", 0, 1, "guard")},   // 4
              Rules{}};
    const ConstraintList rules = makeConstraints(p);

    SECTION("a clean schedule") {
        State s(3, 5);
        s.assign(0, 0);
        s.assign(1, 1);
        const CheckReport c = checkSchedule(p, s, rules);
        CHECK(c.clean());
        CHECK(c.overStaffed.empty());
    }
    SECTION("each problem once, at the later shift, with its reason") {
        State s(3, 5);
        s.assign(0, 1);   // E1: B (12-20) ...
        s.assign(0, 0);   // ... and A (08-16): overlap, reported at B, which starts later.
                          // Together they are also 12h in a row: an unapproved double.
        s.assign(1, 2);   // E2: N, ends at midnight ...
        s.assign(1, 3);   // ... then M at 06:00: 6h of rest
        s.assign(2, 3);   // E3: never available
        s.assign(0, 4);   // E1 in G without the role "guard"
        const CheckReport c = checkSchedule(p, s, rules);
        CHECK(describe(p, c) == std::vector<std::string>{"E1 B: no-overlap", "E1 B: double-needs-approval",
                                                         "E2 M: min-rest", "E3 M: availability", "E1 G: role"});
        REQUIRE(c.violations.size() == 5);
        CHECK(c.violations[2].why.find("rest") != std::string::npos);
        CHECK(c.overStaffed.size() == 1);   // M: 2 people, wanted 1
        CHECK(c.overStaffed[0].shift == 3);
        CHECK(c.overStaffed[0].staff == 2);

        // The report marks exactly those assignments.
        const Report r = buildReport(p, s, {}, makeScoreModel(p), {}, &c);
        CHECK(r.shifts[0].breaksRule == std::vector<char>{0});      // A: E1 fine (it started first)
        CHECK(r.shifts[1].breaksRule == std::vector<char>{1});      // B: E1
        CHECK(r.shifts[3].breaksRule == std::vector<char>{1, 1});   // M: E2 (rest) and E3 (availability)
    }
}

TEST_CASE("an old violation is not reported again at every later shift", "[Report][D51]") {
    // E1: N (16-24), then M (06:00, 6h of rest: a violation), then two more
    // shifts with plenty of rest after M. Only M is reported.
    Problem p{kSunday, kSunday + 2, {availableOn(1, {0, 1, 2})},
              {shiftOn("N", 0, "16:00-00:00", 1, 1),    // 0
               shiftOn("M", 1, "06:00-14:00", 1, 1),    // 1
               shiftOn("L", 2, "06:00-14:00", 1, 1),    // 2
               shiftOn("E", 2, "14:00-18:00", 0, 1)},   // 3  right after L: one block of 12h, not approved
              Rules{}};
    State s(1, 4);
    for (int i = 0; i < 4; ++i) s.assign(0, i);
    const CheckReport c = checkSchedule(p, s, makeConstraints(p));
    std::vector<std::string> found;
    for (const Violation& v : c.violations) found.push_back(p.shifts[v.shift].id + ": " + v.rule);
    // L (16h after M) is fine. E is new: L + E is an unapproved double.
    CHECK(found == std::vector<std::string>{"M: min-rest", "E: double-needs-approval"});
}

TEST_CASE("an availability exception still counts for the other rules", "[Report][D51]") {
    // The manager put E1 in N although E1 is not available that day (D18).
    // M at 06:00 the next morning is still too little rest after N.
    Problem p{kSunday, kSunday + 1, {availableOn(1, {1})},
              {shiftOn("N", 0, "16:00-00:00", 1, 1), shiftOn("M", 1, "06:00-14:00", 1, 1)}, Rules{}};
    State s(1, 2);
    s.assign(0, 0);
    s.assign(0, 1);
    const CheckReport c = checkSchedule(p, s, makeConstraints(p));
    REQUIRE(c.violations.size() == 2);
    CHECK(c.violations[0].kind == Violation::Kind::Availability);
    CHECK(c.violations[1].rule == "min-rest");
}

TEST_CASE("every schedule the search finds passes the check", "[Report][Solver][D51]") {
    // The search only assigns what the rules allow; --check must agree.
    for (unsigned seed = 1; seed <= 30; ++seed) {
        CAPTURE(seed);
        Problem p{kSunday, kSunday + 2, {}, {}, Rules{}};
        unsigned x = seed * 2654435761u;
        auto pick = [&](int n) { x = x * 1103515245u + 12345u; return static_cast<int>((x >> 16) % static_cast<unsigned>(n)); };
        const char* times[] = {"06:00-14:00", "14:00-22:00", "22:00-06:00"};
        for (int d = 0; d < 3; ++d)
            for (int t = 0; t < 3; ++t) p.shifts.push_back(shiftOn("S" + std::to_string(3 * d + t), d, times[t], pick(2), 1 + pick(2)));
        for (int id = 1; id <= 4; ++id) {
            std::vector<int> days;
            for (int d = 0; d < 3; ++d)
                if (pick(3)) days.push_back(d);
            Employee e;
            e.id = id;
            e.name = "E" + std::to_string(id);
            for (int d : days) e.availability.push_back({kSunday + d, std::nullopt, AvailabilityType::Available, pick(2) == 1});
            p.employees.push_back(e);
        }
        Solver solver(p);
        solver.setLimits(SearchLimits{std::nullopt, 20000});
        const auto best = solver.findBest(makeScoreModel(p));
        if (!best) continue;
        CHECK(checkSchedule(p, *best, solver.constraints()).clean());
    }
}
