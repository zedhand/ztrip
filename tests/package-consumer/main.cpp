// Links ztrip::core from an installed package, checks the version and
// compiles an example graph, so a broken export or missing library fails here.
#include "ztrip/graph/compiler.hpp"
#include "ztrip/serialize/mgraph.hpp"
#include "ztrip/version.hpp"

#include <iostream>

int main(int argc, char** argv) {
    std::cout << "ztrip " << ztrip::version() << '\n';
    if (ztrip::version() != EXPECTED_VERSION) {
        std::cerr << "expected version " << EXPECTED_VERSION << '\n';
        return 1;
    }
    if (argc < 2) {
        std::cerr << "usage: consumer <file.mgraph.json>\n";
        return 2;
    }
    const auto doc = ztrip::serialize::load_mgraph(argv[1]);
    const auto compiled = ztrip::graph::compile(doc);
    if (!compiled.ok()) {
        std::cerr << compiled.error_text();
        return 1;
    }
    std::cout << "compiled " << argv[1] << '\n';
    return 0;
}
