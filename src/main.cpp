#include <chrono>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "io/CsvLoader.h"
#include "io/JsonLoader.h"
#include "io/Printer.h"
#include "io/Terminal.h"
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
                 "  --weight <name>=<value>    change the weight of a soft rule, e.g. --weight fairness=0\n"
                 "                             (may be repeated; overrides \"weights\" in the file)\n"
                 "  --view grid|list           a table per week (default), or the list by day\n"
                 "  --week <n>                 show only week n of the table\n"
                 "  --color auto|always|never  colors in the table (default auto: only on a terminal)\n"
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
    std::vector<WeightSetting> weights;   // D43: --weight, in the order given
    bool listView = false;                // D42: --view list (grid is the default)
    std::optional<int> week;              // D42: --week n, grid only
    enum class ColorMode { Auto, Always, Never } color = ColorMode::Auto;   // D45
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
                   arg == "--top" || arg == "--min-diff" || arg == "--weight" || arg == "--view" ||
                   arg == "--week" || arg == "--color") {
            if (i + 1 >= argc) {   // these options need a value after them
                std::cerr << arg << " needs a value\n";
                return std::nullopt;
            }
            const std::string value = argv[++i];
            if (arg == "--availability") {
                options.availabilityPath = value;
                continue;
            }
            if (arg == "--view") {
                if (value != "grid" && value != "list") {
                    std::cerr << "--view must be grid or list, got '" << value << "'\n";
                    return std::nullopt;
                }
                options.listView = value == "list";
                continue;
            }
            if (arg == "--color") {
                if (value == "auto") options.color = Options::ColorMode::Auto;
                else if (value == "always") options.color = Options::ColorMode::Always;
                else if (value == "never") options.color = Options::ColorMode::Never;
                else {
                    std::cerr << "--color must be auto, always or never, got '" << value << "'\n";
                    return std::nullopt;
                }
                continue;
            }
            if (arg == "--weight") {
                const std::optional<WeightSetting> w = parseWeightSetting(value);
                if (!w) {
                    std::cerr << "--weight needs <name>=<value> with a whole number from 0 to " << kMaxWeight
                              << ", got '" << value << "'\n";
                    return std::nullopt;
                }
                options.weights.push_back(*w);
                continue;
            }
            try {
                std::size_t used = 0;
                if (arg == "--time-limit") {
                    const double seconds = std::stod(value, &used);
                    if (used != value.size() || seconds <= 0) throw std::invalid_argument("");
                    options.limits.seconds = seconds;
                } else if (arg == "--top" || arg == "--min-diff" || arg == "--week") {
                    const int n = std::stoi(value, &used);
                    if (used != value.size() || n <= 0) throw std::invalid_argument("");
                    if (arg == "--week") options.week = n;
                    else (arg == "--top" ? options.top : options.minDifferent) = n;
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
    if (options.week && options.listView) {
        std::cerr << "--week works with the table (--view grid) only\n";
        return std::nullopt;
    }
    return options;
}

// D45: colors only where they can be seen. "auto" colors a terminal, but
// not a file or a pipe, and respects NO_COLOR. "always" still tries to turn
// colors on in the Windows console, but colors even if that fails.
bool useColors(Options::ColorMode mode) {
    switch (mode) {
    case Options::ColorMode::Never: return false;
    case Options::ColorMode::Always: enableAnsiColors(); return true;
    case Options::ColorMode::Auto: break;
    }
    return stdoutIsTerminal() && !noColorRequested() && enableAnsiColors();
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
    // --weight wins over "weights" in the file; a later --weight wins over
    // an earlier one for the same rule (D43).
    for (const WeightSetting& w : options->weights) result.problem->rules.weights[w.name] = w.value;
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
    // 4. Report: computed once (D41), then shown.
    const Report report = buildReport(problem, *schedule, solver.shortages(), scoreModel, solver.top());
    if (options->week && *options->week > static_cast<int>(report.weeks.size())) {
        std::cerr << "--week " << *options->week << ": the period has only " << report.weeks.size() << " weeks\n";
        return kBadInput;
    }
    if (options->listView) {
        printSchedule(std::cout, problem, report);
    } else {
        printWeeks(std::cout, problem, report, options->week, useColors(options->color));
        printPeriodSummary(std::cout, problem, report);
    }
    printShortages(std::cout, problem, report);
    printScore(std::cout, report);

    // Anytime (D31, D34): a usable schedule, but the search did not finish.
    if (!options->firstOnly) {
        if (solver.complete())
            std::cout << "This is the best schedule: the whole search space was checked.\n";
        else
            std::cout << "Search stopped by the time or node limit: this is the best schedule\n"
                         "found so far, and a better one may exist. Try a larger --time-limit.\n";
    }

    if (!options->firstOnly) printAlternatives(std::cout, problem, report, options->minDifferent);

    if (!solver.shortages().empty())
        std::cerr << "warning: " << solver.shortages().size()
                  << " shift(s) cannot be fully staffed, see the list at the end\n";
    return kOk;
}
