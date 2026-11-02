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

TEST_CASE("not enough candidates means no solution", "[Solver]") {
    Problem p = makeProblem({freeOnSunday(1), neverAvailable(2)},
                            {sundayShift("S", "08:00-16:00", 2)});
    Solver solver(p);
    CHECK_FALSE(solver.findFirst().has_value());
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
    Problem p = makeProblem({freeOnSunday(1), freeOnSunday(2), freeOnSunday(3), freeOnSunday(4)},
                            {sundayShift("A", "08:00-16:00", 2), sundayShift("B", "16:00-23:00", 1)});
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

TEST_CASE("min stays a hard limit even when wanted is larger", "[Solver][wanted]") {
    Problem p = makeProblem({freeOnSunday(1), neverAvailable(2)},
                            {sundayShift("S", "08:00-16:00", 2, 3)});
    Solver solver(p);
    CHECK_FALSE(solver.findFirst().has_value());
}
