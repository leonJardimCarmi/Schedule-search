#include <chrono>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>

#include "io/CsvLoader.h"
#include "io/JsonLoader.h"
#include "io/Printer.h"
#include "solver/Score.h"
#include "solver/Solver.h"

namespace {

// Exit codes, so scripts can tell what happened without reading the text.
constexpr int kOk = 0;           // a schedule was found (maybe not proven best: see D34)
constexpr int kBadInput = 1;     // wrong arguments or errors in the input file
constexpr int kNoSchedule = 2;   // the input is fine but no schedule exists
constexpr int kLimitReached = 3; // stopped by --time-limit / --max-nodes (D24)

constexpr double kDefaultTimeLimit = 60;   // seconds (D23)

void printUsage() {
    std::cerr << "usage: schedule_search <problem.json> [options]\n"
                 "  --availability <file.csv>  add availability lines from a CSV file\n"
                 "                             (employee_id,date,time,type[,double])\n"
                 "  --time-limit <seconds>     stop the search after this long (default 60)\n"
                 "  --max-nodes <n>            stop the search after n nodes (default: no limit)\n"
                 "  --first                    return the first legal schedule instead of the best one\n"
                 "  --top <n>                  also list the next best schedules, n in total (default 5)\n"
                 "  --min-diff <k>             schedules in that list differ in at least k assignments (default 3)\n"
                 "  --verbose                  also print the input, the candidates and the search size\n";
}

struct Options {
    std::string inputPath;
    std::string availabilityPath;   // empty = no CSV
    SearchLimits limits{kDefaultTimeLimit, std::nullopt};
    bool verbose = false;
    bool firstOnly = false;   // --first: skip the optimization (D33)
    int top = 5;              // D39: how many good schedules to keep
    int minDifferent = 3;     // D39: how different they must be
};

// Arguments may come in any order: "file --verbose" or "--verbose file".
std::optional<Options> parseArgs(int argc, char* argv[]) {
    Options options;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--verbose" || arg == "-v") {
            options.verbose = true;
        } else if (arg == "--first") {
            options.firstOnly = true;
        } else if (arg == "--availability" || arg == "--time-limit" || arg == "--max-nodes" ||
                   arg == "--top" || arg == "--min-diff") {
            if (i + 1 >= argc) {   // these options need a value after them
                std::cerr << arg << " needs a value\n";
                return std::nullopt;
            }
            const std::string value = argv[++i];
            if (arg == "--availability") {
                options.availabilityPath = value;
                continue;
            }
            try {
                std::size_t used = 0;
                if (arg == "--time-limit") {
                    const double seconds = std::stod(value, &used);
                    if (used != value.size() || seconds <= 0) throw std::invalid_argument("");
                    options.limits.seconds = seconds;
                } else if (arg == "--top" || arg == "--min-diff") {
                    const int n = std::stoi(value, &used);
                    if (used != value.size() || n <= 0) throw std::invalid_argument("");
                    (arg == "--top" ? options.top : options.minDifferent) = n;
                } else {
                    const long long nodes = std::stoll(value, &used);
                    if (used != value.size() || nodes <= 0) throw std::invalid_argument("");
                    options.limits.maxNodes = nodes;
                }
            } catch (const std::exception&) {
                std::cerr << arg << " needs a positive number, got '" << value << "'\n";
                return std::nullopt;
            }
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
    LoadResult result = JsonLoader::loadFile(options->inputPath);
    if (result.ok() && !options->availabilityPath.empty())
        CsvLoader::loadAvailability(options->availabilityPath, *result.problem, result.diagnostics);

    for (const Diagnostic& d : result.diagnostics.all())
        std::cerr << d << '\n';
    if (!result.ok()) {
        std::cerr << "input has errors, stopping.\n";
        return kBadInput;
    }
    const Problem& problem = *result.problem;

    // 2. Search.
    const ScoreModel scoreModel = makeScoreModel(problem);
    for (const std::string& name : scoreModel.unknownWeights())
        std::cerr << "warning: weights." << name << ": no soft rule with this name, ignored\n";
    Solver solver(problem);
    solver.setLimits(options->limits);
    solver.setTop(options->top, options->minDifferent);
    if (options->verbose) {
        printProblem(std::cout, problem);
        printCandidates(std::cout, problem, solver);
        std::cout << "\nHard rules:";
        for (const auto& constraint : solver.constraints()) std::cout << ' ' << constraint->name();
        std::cout << "\n\n";
    }
    const auto searchStart = std::chrono::steady_clock::now();
    // By default, the best schedule (D2, D33). --first stops at the first legal one.
    const std::optional<State> schedule =
        options->firstOnly ? solver.findFirst() : solver.findBest(scoreModel);
    const std::chrono::duration<double, std::milli> searchTime = std::chrono::steady_clock::now() - searchStart;
    if (options->verbose)
        std::cout << "Search visited " << solver.nodesVisited() << " nodes in "
                  << searchTime.count() << " ms"
                  << (options->firstOnly ? "" : ", the best schedule improved "
                                               + std::to_string(solver.improvements()) + " times, "
                                               + std::to_string(solver.pruned()) + " branches pruned")
                  << "\n";
    if (options->verbose && !options->firstOnly)
        std::cout << "No schedule can score better than: Medium " << solver.startBound().at(Level::Medium)
                  << ", Soft " << solver.startBound().at(Level::Soft) << "\n";
    if (options->verbose) std::cout << "\n";

    // 3. Report.
    if (solver.status() == SearchStatus::LimitReached) {
        std::cout << "Search stopped after " << solver.nodesVisited()
                  << " nodes (time or node limit). No schedule found yet;\n"
                     "a schedule may still exist. Try a larger --time-limit.\n";
        return kLimitReached;
    }
    if (!schedule) {
        std::cout << "No schedule satisfies the input.\n";
        return kNoSchedule;
    }
    printSchedule(std::cout, problem, *schedule);
    printShortages(std::cout, problem, solver);
    printScore(std::cout, problem, scoreModel, *schedule);

    // Anytime (D31, D34): a usable schedule, but the search did not finish.
    if (!options->firstOnly) {
        if (solver.complete())
            std::cout << "This is the best schedule: the whole search space was checked.\n";
        else
            std::cout << "Search stopped by the time or node limit: this is the best schedule\n"
                         "found so far, and a better one may exist. Try a larger --time-limit.\n";
    }

    if (!options->firstOnly) printAlternatives(std::cout, problem, solver.top(), options->minDifferent);

    if (!solver.shortages().empty())
        std::cerr << "warning: " << solver.shortages().size()
                  << " shift(s) cannot be fully staffed, see the list at the end\n";
    return kOk;
}
