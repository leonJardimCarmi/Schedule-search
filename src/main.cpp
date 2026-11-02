#include <iostream>

#include "io/JsonLoader.h"
#include "io/Printer.h"

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

    printProblem(std::cout, *result.problem);
    return 0;
}
