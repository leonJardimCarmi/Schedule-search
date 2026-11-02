#include <catch2/catch_test_macros.hpp>

#include <vector>

#include "solver/State.h"

TEST_CASE("a new state is empty", "[State]") {
    State s(3, 2);   // 3 employees, 2 shifts
    CHECK(s.assignedCount(0) == 0);
    CHECK(s.assignedCount(1) == 0);
    CHECK(s.shiftsOf(2).empty());
}

TEST_CASE("assign is visible from both sides", "[State]") {
    State s(3, 2);
    s.assign(2, 0);   // employee 2 -> shift 0

    CHECK(s.isAssigned(2, 0));
    CHECK_FALSE(s.isAssigned(1, 0));
    CHECK(s.staffOf(0) == std::vector<int>{2});
    CHECK(s.shiftsOf(2) == std::vector<int>{0});
}

TEST_CASE("unassign restores the exact previous state", "[State]") {
    State s(3, 2);
    s.assign(0, 0);
    s.assign(1, 0);
    s.assign(1, 1);

    // Undo in reverse order, as the DFS does.
    s.unassign(1, 1);
    CHECK(s.shiftsOf(1) == std::vector<int>{0});
    CHECK(s.assignedCount(1) == 0);

    s.unassign(1, 0);
    s.unassign(0, 0);
    CHECK(s.assignedCount(0) == 0);
    CHECK(s.shiftsOf(0).empty());
    CHECK(s.shiftsOf(1).empty());
}
