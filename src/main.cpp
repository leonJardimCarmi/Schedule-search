#include <iostream>

#include "io/JsonLoader.h"
#include "io/Printer.h"
#include "solver/Availability.h"
#include "solver/Solver.h"

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "usage: schedule_search <problem.json>\n";
        return 1;
    }

    const LoadResult result = JsonLoader::loadFile(argv[1]);

    // Warnings and errors go to stderr, so they stay separate from the output.
    for (const Diagnostic& d : result.diagnostics.all())
        std::cerr << d << '\n';

    if (!result.ok()) {
        std::cerr << "input has errors, stopping.\n";
        return 1;
    }

    const Problem& problem = *result.problem;
    printProblem(std::cout, problem);

    // Temporary (stage 1.2): show who could work each shift.
    std::cout << "\nWho can work each shift\n";
    for (const Shift& shift : problem.shifts) {
        std::cout << "  " << shift.id << ":";
        for (const Employee& e : problem.employees) {
            const Fit fit = availabilityFit(e, shift);
            if (fit == Fit::No) continue;
            std::cout << "  " << e.name;
            if (fit == Fit::Preferred) std::cout << " (preferred)";
        }
        std::cout << '\n';
    }

    // Temporary (stage 1.3): run the search and show the result.
    Solver solver(problem);
    const std::optional<State> schedule = solver.findFirst();
    std::cout << "\nSearch visited " << solver.nodesVisited() << " nodes\n";
    if (!schedule) {
        std::cout << "No schedule found.\n";
        return 2;
    }
    std::cout << "Schedule (each shift filled to min)\n";
    for (int s = 0; s < static_cast<int>(problem.shifts.size()); ++s) {
        std::cout << "  " << problem.shifts[s].id << ":";
        for (int e : schedule->staffOf(s)) std::cout << "  " << problem.employees[e].name;
        std::cout << '\n';
    }
    std::cout << "Different schedules possible: " << solver.countAll() << '\n';
    return 0;
}
