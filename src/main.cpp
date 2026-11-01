#include <iostream>

#include "TimePeriod.h"

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "usage: schedule_search <problem.json>\n";
        return 1;
    }

    // Temporary smoke check until the JSON loader exists (stage 0.4).
    TimePeriod night = TimePeriod::fromString("22:00-06:00");
    std::cout << "input: " << argv[1] << '\n'
              << "night shift " << night.toString()
              << " lasts " << night.duration() / 60 << " hours\n";
    return 0;
}
