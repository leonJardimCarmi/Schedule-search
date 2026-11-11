#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "solver/Solver.h"

namespace {

const Date kSunday = Date::fromString("2026-11-08");

// An employee available all day Sunday.
Employee freeOnSunday(int id) {
    Employee e;
    e.id = id;
    e.name = "E" + std::to_string(id);
    e.availability.push_back({kSunday, std::nullopt, AvailabilityType::Available});
    return e;
}

Employee neverAvailable(int id) {
    Employee e;
    e.id = id;
    e.name = "N" + std::to_string(id);
    return e;
}

Shift sundayShift(const std::string& id, const std::string& time, int minStaff) {
    return Shift{id, kSunday, TimePeriod::fromString(time), minStaff, minStaff, ""};
}

Shift sundayShift(const std::string& id, const std::string& time, int minStaff, int wantedStaff) {
    return Shift{id, kSunday, TimePeriod::fromString(time), minStaff, wantedStaff, ""};
}

Problem makeProblem(std::vector<Employee> employees, std::vector<Shift> shifts) {
    return Problem{kSunday, kSunday, std::move(employees), std::move(shifts), Rules{}};
}

}  // namespace

TEST_CASE("one shift, one available employee", "[Solver]") {
    Problem p = makeProblem({freeOnSunday(1)}, {sundayShift("S", "08:00-16:00", 1)});
    Solver solver(p);

    auto result = solver.findFirst();
    REQUIRE(result.has_value());
    CHECK(result->staffOf(0) == std::vector<int>{0});
}

TEST_CASE("only available employees are candidates", "[Solver]") {
    Problem p = makeProblem({neverAvailable(1), freeOnSunday(2), neverAvailable(3)},
                            {sundayShift("S", "08:00-16:00", 1)});
    Solver solver(p);
    CHECK(solver.candidatesOf(0) == std::vector<int>{1});

    auto result = solver.findFirst();
    REQUIRE(result.has_value());
    CHECK(result->staffOf(0) == std::vector<int>{1});
}

TEST_CASE("too few candidates: the shift gets what it can and is reported (D25)", "[Solver][D25]") {
    Problem p = makeProblem({freeOnSunday(1), neverAvailable(2)},
                            {sundayShift("S", "08:00-16:00", 2)});
    Solver solver(p);

    REQUIRE(solver.shortages().size() == 1);       // found before any search
    CHECK(solver.shortages()[0].shift == 0);
    CHECK(solver.shortages()[0].candidates == 1);
    CHECK(solver.shortages()[0].needed == 2);

    auto result = solver.findFirst();
    REQUIRE(result.has_value());                    // a schedule, not "impossible"
    CHECK(result->staffOf(0) == std::vector<int>{0}); // the one person who can
}

TEST_CASE("a shift nobody can work does not block the others (D25)", "[Solver][D25]") {
    Shift guardNight = sundayShift("N", "22:00-06:00", 1);
    guardNight.role = "guard";                       // nobody has it
    Problem p = makeProblem({freeOnSunday(1)}, {sundayShift("M", "08:00-16:00", 1), guardNight});
    Solver solver(p);

    auto result = solver.findFirst();
    REQUIRE(result.has_value());
    CHECK(result->assignedCount(0) == 1);            // the morning is staffed
    CHECK(result->assignedCount(1) == 0);            // the night is the reported hole
    REQUIRE(solver.shortages().size() == 1);
    CHECK(solver.shortages()[0].candidates == 0);
}

TEST_CASE("a shift that needs nobody is fine", "[Solver]") {
    Problem p = makeProblem({neverAvailable(1)}, {sundayShift("S", "08:00-16:00", 0)});
    Solver solver(p);
    auto result = solver.findFirst();
    REQUIRE(result.has_value());
    CHECK(result->assignedCount(0) == 0);
}

TEST_CASE("the search counts combinations, not permutations", "[Solver]") {
    // Shift A needs 2 of 4 people: C(4,2) = 6 ways.
    // Shift B needs 1 of the same 4:          4 ways.
    // Total 6 * 4 = 24. With permutations it would be 12 * 4 = 48.
    // A and B touch and together last 8 hours: one short work block,
    // so no rule (rest, doubles) limits who can take both.
    Problem p = makeProblem({freeOnSunday(1), freeOnSunday(2), freeOnSunday(3), freeOnSunday(4)},
                            {sundayShift("A", "08:00-12:00", 2), sundayShift("B", "12:00-16:00", 1)});
    Solver solver(p);
    CHECK(solver.countAll() == 24);
}

TEST_CASE("the state is clean after a full search", "[Solver]") {
    // countAll() explores and undoes everything; a following findFirst()
    // must still work from an empty state.
    Problem p = makeProblem({freeOnSunday(1), freeOnSunday(2)}, {sundayShift("S", "08:00-16:00", 1)});
    Solver solver(p);
    CHECK(solver.countAll() == 2);
    auto result = solver.findFirst();
    REQUIRE(result.has_value());
    CHECK(result->assignedCount(0) == 1);
}

TEST_CASE("wanted is tried first", "[Solver][wanted]") {
    Problem p = makeProblem({freeOnSunday(1), freeOnSunday(2), freeOnSunday(3)},
                            {sundayShift("S", "08:00-16:00", 1, 2)});
    Solver solver(p);
    auto result = solver.findFirst();
    REQUIRE(result.has_value());
    CHECK(result->staffOf(0) == std::vector<int>{0, 1});   // 2 people, not 1
}

TEST_CASE("every size between min and wanted is a different schedule", "[Solver][wanted]") {
    // min 1, wanted 2, 3 candidates: C(3,2) + C(3,1) = 3 + 3 = 6.
    Problem p = makeProblem({freeOnSunday(1), freeOnSunday(2), freeOnSunday(3)},
                            {sundayShift("S", "08:00-16:00", 1, 2)});
    Solver solver(p);
    CHECK(solver.countAll() == 6);
}

TEST_CASE("wanted larger than the candidates is capped", "[Solver][wanted]") {
    Problem p = makeProblem({freeOnSunday(1), freeOnSunday(2), neverAvailable(3)},
                            {sundayShift("S", "08:00-16:00", 1, 3)});
    Solver solver(p);
    auto result = solver.findFirst();
    REQUIRE(result.has_value());
    CHECK(result->assignedCount(0) == 2);
}

TEST_CASE("min stays hard wherever it can be met (D11 with D25)", "[Solver][wanted]") {
    // Each shift alone has enough candidates, so there is no shortage. But one
    // person cannot work two overlapping shifts: no schedule, as before.
    Problem p = makeProblem({freeOnSunday(1)},
                            {sundayShift("A", "08:00-16:00", 1, 2), sundayShift("B", "12:00-20:00", 1, 2)});
    Solver solver(p);
    CHECK(solver.shortages().empty());
    CHECK_FALSE(solver.findFirst().has_value());
    CHECK(solver.status() == SearchStatus::NoSchedule);
}

TEST_CASE("a double is the last resort", "[Solver][double]") {
    // Dana can work all day and approves a double. Noa can work only the evening.
    Employee dana;
    dana.id = 1;
    dana.name = "Dana";
    dana.availability.push_back({kSunday, TimePeriod::fromString("08:00-00:00"),
                                 AvailabilityType::Available, true});
    Employee noa;
    noa.id = 2;
    noa.name = "Noa";
    noa.availability.push_back({kSunday, TimePeriod::fromString("16:00-00:00"),
                                AvailabilityType::Available, false});

    const std::vector<Shift> shifts = {sundayShift("M", "08:00-16:00", 1),
                                       sundayShift("E", "16:00-00:00", 1)};

    SECTION("when someone else can take the evening, Dana does not double") {
        // Dana is first in the list, so without the ordering she would be tried first.
        Problem p = makeProblem({dana, noa}, shifts);
        auto result = Solver(p).findFirst();
        REQUIRE(result.has_value());
        CHECK(result->staffOf(0) == std::vector<int>{0});   // M: Dana
        CHECK(result->staffOf(1) == std::vector<int>{1});   // E: Noa
    }

    SECTION("when nobody else can, the approved double is used") {
        Problem p = makeProblem({dana}, shifts);
        auto result = Solver(p).findFirst();
        REQUIRE(result.has_value());
        CHECK(result->staffOf(1) == std::vector<int>{0});   // E: Dana, a double
    }
}

TEST_CASE("only employees with the shift's role are candidates", "[Solver][role]") {
    Employee guard = freeOnSunday(1);
    guard.roles = {"cashier", "guard"};
    Employee cashier = freeOnSunday(2);
    cashier.roles = {"cashier"};

    Shift night = sundayShift("N", "22:00-06:00", 1);
    night.role = "guard";
    Shift morning = sundayShift("M", "08:00-16:00", 1);   // no role: anyone

    Problem p = makeProblem({cashier, guard}, {morning, night});
    Solver solver(p);
    CHECK(solver.candidatesOf(0) == std::vector<int>{0, 1});   // both can do the morning
    CHECK(solver.candidatesOf(1) == std::vector<int>{1});      // only the guard at night
}

// ---- search limits (DECISIONS D23, D24) --------------------------------------
// Tested with a node limit, not a time limit: the number of nodes is the same
// on every computer and every run, while time depends on how fast and how
// busy the machine is. A test that depends on time would sometimes fail.

namespace {

// Four early shifts, then a late shift that needs ALL five people.
// Each shift alone has enough candidates (no shortage, D25), but whoever
// works early has only 5 hours of rest before the late shift (10 needed).
// The search only finds out at the last shift, after trying every
// combination of the first four: a small version of the month benchmark.
Problem hopelessAtTheEnd() {
    std::vector<Employee> people;
    for (int id = 1; id <= 5; ++id) people.push_back(freeOnSunday(id));
    // Back-to-back one-hour shifts: together a short work block, so no rule
    // limits who takes them, and the search tree is as wide as possible.
    std::vector<Shift> shifts = {sundayShift("S0", "01:00-02:00", 1, 2),
                                 sundayShift("S1", "02:00-03:00", 1, 2),
                                 sundayShift("S2", "03:00-04:00", 1, 2),
                                 sundayShift("S3", "04:00-05:00", 1, 2)};
    shifts.push_back(sundayShift("X", "10:00-11:00", 5));
    return makeProblem(people, shifts);
}

// The pigeonhole problem: 8 shifts at the same time, only 7 people.
// Every shift alone has 7 candidates, and every partial assignment still
// looks possible until the very last shift, so even MRV (D26) has to try
// all the ways to place 7 people in 8 shifts before it can say "no".
// A classic worst case for backtracking search.
Problem pigeonhole() {
    std::vector<Employee> people;
    for (int id = 1; id <= 7; ++id) people.push_back(freeOnSunday(id));
    std::vector<Shift> shifts;
    for (int i = 0; i < 8; ++i) shifts.push_back(sundayShift("P" + std::to_string(i), "08:00-16:00", 1));
    return makeProblem(people, shifts);
}

}  // namespace

TEST_CASE("MRV finds a late conflict without trying every earlier combination (D26)", "[Solver][mrv]") {
    // In calendar order this took thousands of nodes: every combination of
    // S0..S3 was tried and failed at X. MRV looks at X first: it needs all 5
    // people, so it is the tightest shift, and the conflict shows at once.
    Problem p = hopelessAtTheEnd();
    Solver solver(p);
    CHECK_FALSE(solver.findFirst().has_value());
    CHECK(solver.status() == SearchStatus::NoSchedule);
    CHECK(solver.nodesVisited() < 50);
}

TEST_CASE("without a limit the search proves there is no schedule", "[Solver][limits]") {
    Problem p = pigeonhole();
    Solver solver(p);
    CHECK_FALSE(solver.findFirst().has_value());
    CHECK(solver.status() == SearchStatus::NoSchedule);
    CHECK(solver.nodesVisited() > 1000);   // it really had to search
}

TEST_CASE("a node limit stops the search and says so", "[Solver][limits]") {
    Problem p = pigeonhole();
    Solver solver(p);
    solver.setLimits({std::nullopt, 100});

    CHECK_FALSE(solver.findFirst().has_value());
    CHECK(solver.status() == SearchStatus::LimitReached);   // not "no schedule"
    CHECK(solver.nodesVisited() == 101);                     // stopped right away
}

TEST_CASE("a limit that is not reached changes nothing", "[Solver][limits]") {
    Problem p = makeProblem({freeOnSunday(1)}, {sundayShift("S", "08:00-16:00", 1)});
    Solver solver(p);
    solver.setLimits({60.0, 1000000});
    REQUIRE(solver.findFirst().has_value());
    CHECK(solver.status() == SearchStatus::Found);
}

TEST_CASE("the next search starts with a fresh limit", "[Solver][limits]") {
    Problem p = pigeonhole();
    Solver solver(p);
    solver.setLimits({std::nullopt, 100});
    solver.findFirst();
    REQUIRE(solver.status() == SearchStatus::LimitReached);

    solver.setLimits({});                    // no limits now
    solver.findFirst();
    CHECK(solver.status() == SearchStatus::NoSchedule);
}
