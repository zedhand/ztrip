// analyze-cli: offline analysis and plot dump for tuning.
// Phase 1 skeleton: only reports the version until the pipeline lands.
#include "ztrip/version.hpp"

#include <iostream>
#include <string_view>

int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (arg == "--version" || arg == "-v") {
            std::cout << "analyze-cli " << ztrip::version() << '\n';
            return 0;
        }
    }
    std::cout << "usage: analyze-cli [--version]\n"
                 "Offline analysis is not implemented yet (Phase 1).\n";
    return 0;
}
