#include <iostream>
#include <optional>
#include <string>

#include "io/JsonLoader.h"
#include "io/Printer.h"
#include "solver/Solver.h"

namespace {

// Exit codes, so scripts can tell what happened without reading the text.
constexpr int kOk = 0;           // a schedule was found
constexpr int kBadInput = 1;     // wrong arguments or errors in the input file
constexpr int kNoSchedule = 2;   // the input is fine but no schedule exists

void printUsage() {
    std::cerr << "usage: schedule_search <problem.json> [--verbose]\n"
                 "  --verbose  also print the loaded input and who can work each shift\n";
}

struct Options {
    std::string inputPath;
    bool verbose = false;
};

// Arguments may come in any order: "file --verbose" or "--verbose file".
std::optional<Options> parseArgs(int argc, char* argv[]) {
    Options options;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--verbose" || arg == "-v") {
            options.verbose = true;
        } else if (!arg.empty() && arg[0] == '-') {
            std::cerr << "unknown option: " << arg << '\n';
            return std::nullopt;
        } else if (options.inputPath.empty()) {
            options.inputPath = arg;
        } else {
            std::cerr << "only one input file is supported\n";
            return std::nullopt;
        }
    }
    if (options.inputPath.empty()) return std::nullopt;
    return options;
}

}  // namespace

int main(int argc, char* argv[]) {
    const std::optional<Options> options = parseArgs(argc, argv);
    if (!options) {
        printUsage();
        return kBadInput;
    }

    // 1. Load. Warnings and errors go to stderr, separate from the output.
    const LoadResult result = JsonLoader::loadFile(options->inputPath);
    for (const Diagnostic& d : result.diagnostics.all())
        std::cerr << d << '\n';
    if (!result.ok()) {
        std::cerr << "input has errors, stopping.\n";
        return kBadInput;
    }
    const Problem& problem = *result.problem;

    // 2. Search.
    Solver solver(problem);
    if (options->verbose) {
        printProblem(std::cout, problem);
        printCandidates(std::cout, problem, solver);
        std::cout << '\n';
    }
    const std::optional<State> schedule = solver.findFirst();
    if (options->verbose)
        std::cout << "Search visited " << solver.nodesVisited() << " nodes\n\n";

    // 3. Report.
    if (!schedule) {
        std::cout << "No schedule satisfies the input.\n";
        return kNoSchedule;
    }
    printSchedule(std::cout, problem, *schedule);
    return kOk;
}
