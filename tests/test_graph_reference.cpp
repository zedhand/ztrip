// Graph determinism on real analysis of the reference tracks. Skips unless
// ZTRIP_REFERENCE_TRACKS points at the reference-tracks folder.
#include "ztrip/analysis/offline.hpp"
#include "ztrip/graph/evaluator.hpp"
#include "ztrip/io/wav.hpp"
#include "ztrip/serialize/mgraph.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>

namespace fs = std::filesystem;
using namespace ztrip;

namespace {

constexpr double kRate = 120.0;

fs::path reference_dir() {
    const char* env = std::getenv("ZTRIP_REFERENCE_TRACKS");
    return env ? fs::path(env) : fs::path();
}

graph::Evaluator load(const char* example, const analysis::AnalysisResult& song) {
    auto r = graph::compile(serialize::load_mgraph(fs::path(ZTRIP_SOURCE_DIR) / "examples" / example));
    INFO(r.error_text());
    REQUIRE(r.ok());
    graph::Evaluator ev(std::move(r.graph));
    ev.bind_source("main", &song);
    return ev;
}

std::vector<double> outputs_of(const graph::Evaluator& ev) {
    std::vector<double> v;
    for (const auto& o : ev.outputs()) v.push_back(o.value);
    return v;
}

// Reads the numeric array stored under "key": [...] in the truth file.
std::vector<double> truth_times(const fs::path& file, const std::string& key) {
    std::ifstream in(file);
    std::stringstream ss;
    ss << in.rdbuf();
    const std::string text = ss.str();
    const auto at = text.find("\"" + key + "\"");
    REQUIRE(at != std::string::npos);
    const auto open = text.find('[', at);
    const auto close = text.find(']', open);
    std::vector<double> out;
    std::stringstream list(text.substr(open + 1, close - open - 1));
    std::string item;
    while (std::getline(list, item, ',')) out.push_back(std::stod(item));
    return out;
}

} // namespace

TEST_CASE("graphs on the reference tracks: seek matches play", "[reference][graph]") {
    const fs::path dir = reference_dir();
    if (dir.empty()) SKIP("set ZTRIP_REFERENCE_TRACKS to the reference-tracks folder to run");
    int ran = 0;
    for (const char* name : {"01_four_on_the_floor_128bpm.wav", "02_live_band_lets_go_fishin.wav",
                             "03_syncopated_jazz_vibe_ace.wav"}) {
        if (!fs::exists(dir / name)) continue;
        ++ran;
        INFO(name);
        const auto song = analysis::analyze(io::read_wav(dir / name), {});
        auto player = load("kitchen_sink.mgraph.json", song);
        std::vector<std::vector<double>> ref;
        const auto ticks = static_cast<int>(song.duration_s * kRate);
        for (int i = 0; i <= ticks; ++i) {
            player.evaluate(i / kRate);
            ref.push_back(outputs_of(player));
        }
        auto seeker = load("kitchen_sink.mgraph.json", song);
        std::mt19937 rng(5);
        std::uniform_int_distribution<int> tick(0, ticks);
        double worst = 0.0;
        for (int k = 0; k < 200; ++k) {
            const int i = tick(rng);
            seeker.evaluate(i / kRate);
            const auto got = outputs_of(seeker);
            for (std::size_t j = 0; j < got.size(); ++j) {
                worst = std::max(worst, std::abs(got[j] - ref[static_cast<std::size_t>(i)][j]));
            }
        }
        CHECK(worst <= 1e-5);
    }
    if (ran == 0) SKIP("no reference tracks present");
}

TEST_CASE("track 01: the kick graph flashes on the true kicks", "[reference][graph]") {
    const fs::path dir = reference_dir();
    const fs::path wav = dir / "01_four_on_the_floor_128bpm.wav";
    const fs::path truth = dir / "01_four_on_the_floor_128bpm.truth.json";
    if (dir.empty() || !fs::exists(wav) || !fs::exists(truth)) {
        SKIP("set ZTRIP_REFERENCE_TRACKS to the reference-tracks folder to run");
    }
    const auto song = analysis::analyze(io::read_wav(wav), {});
    auto ev = load("kick_to_light.mgraph.json", song);
    std::vector<double> flashes; // tick times the flash was retriggered
    for (int i = 0; i <= static_cast<int>(song.duration_s * kRate); ++i) {
        ev.evaluate(i / kRate);
        if (ev.port_value("kick", "trigger")->fired) flashes.push_back(i / kRate);
    }
    const auto kicks = truth_times(truth, "kick");
    std::size_t hit = 0;
    for (double k : kicks) {
        // A tick reports events up to 1/120 s before it, plus 20 ms of detection error.
        const auto it = std::lower_bound(flashes.begin(), flashes.end(), k - 0.020);
        if (it != flashes.end() && *it <= k + 0.020 + 1.0 / kRate) ++hit;
    }
    const double recall = static_cast<double>(hit) / static_cast<double>(kicks.size());
    INFO("flashes " << flashes.size() << ", kicks " << kicks.size() << ", recall " << recall);
    CHECK(recall >= 0.95);
    CHECK(static_cast<double>(flashes.size()) <= 1.2 * static_cast<double>(kicks.size()));
}
