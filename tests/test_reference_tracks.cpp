// Accuracy on the project's reference tracks. The audio isn't in the repo
// (tracks 2 and 3 are CC BY and need attribution if distributed), so these
// tests skip unless ZTRIP_REFERENCE_TRACKS points at a folder holding
// 01_four_on_the_floor_128bpm.wav and its .truth.json (and optionally 02/03).
#include "ztrip/analysis/offline.hpp"
#include "ztrip/io/wav.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace fs = std::filesystem;
using namespace ztrip;

namespace {

fs::path reference_dir() {
    const char* env = std::getenv("ZTRIP_REFERENCE_TRACKS");
    return env ? fs::path(env) : fs::path();
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

struct Score {
    double precision = 0.0;
    double recall = 0.0;
    double mean_abs_error = 0.0;
};

Score score(const std::vector<analysis::Trigger>& det, const std::vector<double>& ref, double tol) {
    std::vector<bool> used(det.size(), false);
    std::size_t hits = 0;
    double err = 0.0;
    for (double t : ref) {
        std::size_t best = det.size();
        double best_err = tol;
        for (std::size_t i = 0; i < det.size(); ++i) {
            const double e = std::fabs(det[i].time - t);
            if (!used[i] && e <= best_err) {
                best = i;
                best_err = e;
            }
        }
        if (best < det.size()) {
            used[best] = true;
            ++hits;
            err += best_err;
        }
    }
    Score s;
    s.precision = det.empty() ? 0.0 : static_cast<double>(hits) / static_cast<double>(det.size());
    s.recall = static_cast<double>(hits) / static_cast<double>(ref.size());
    s.mean_abs_error = hits ? err / static_cast<double>(hits) : 0.0;
    return s;
}

} // namespace

TEST_CASE("track 01: kicks, beats and downbeats match ground truth", "[reference]") {
    const fs::path dir = reference_dir();
    const fs::path wav = dir / "01_four_on_the_floor_128bpm.wav";
    const fs::path truth = dir / "01_four_on_the_floor_128bpm.truth.json";
    if (dir.empty() || !fs::exists(wav) || !fs::exists(truth)) {
        SKIP("set ZTRIP_REFERENCE_TRACKS to the reference-tracks folder to run");
    }
    const auto r = analysis::analyze(io::read_wav(wav), {});

    CHECK(r.beats.bpm == Catch::Approx(128.0).epsilon(0.005));

    const Score kick = score(r.find_events("onset.Sub")->events, truth_times(truth, "kick"), 0.020);
    INFO("Sub vs kick P=" << kick.precision << " R=" << kick.recall << " err=" << kick.mean_abs_error);
    CHECK(kick.recall >= 0.95);
    CHECK(kick.precision >= 0.80);
    CHECK(kick.mean_abs_error < 0.010);

    const Score hats = score(r.find_events("onset.Air")->events, truth_times(truth, "hat"), 0.030);
    INFO("Air vs hat P=" << hats.precision << " R=" << hats.recall);
    CHECK(hats.recall >= 0.95);
    CHECK(hats.precision >= 0.90);

    const Score beats = score(r.find_events("beat")->events, truth_times(truth, "beats"), 0.070);
    INFO("beats P=" << beats.precision << " R=" << beats.recall);
    CHECK(beats.recall >= 0.95);
    CHECK(beats.precision >= 0.95);

    const Score down = score(r.find_events("downbeat")->events, truth_times(truth, "downbeats"), 0.070);
    INFO("downbeats P=" << down.precision << " R=" << down.recall);
    CHECK(down.recall >= 0.9);
    CHECK(down.precision >= 0.9);
}

TEST_CASE("tracks 02 and 03 analyse to a steady, plausible grid", "[reference]") {
    const fs::path dir = reference_dir();
    if (dir.empty()) SKIP("set ZTRIP_REFERENCE_TRACKS to the reference-tracks folder to run");
    int ran = 0;
    for (const char* name : {"02_live_band_lets_go_fishin.wav", "03_syncopated_jazz_vibe_ace.wav"}) {
        const fs::path wav = dir / name;
        if (!fs::exists(wav)) continue;
        ++ran;
        const auto r = analysis::analyze(io::read_wav(wav), {});
        INFO(name << ": " << r.beats.bpm << " BPM, " << r.beats.beats.size() << " beats");
        CHECK(r.beats.bpm > 70.0);
        CHECK(r.beats.bpm < 180.0);
        // Beats should cover most of the track at the reported tempo.
        CHECK(static_cast<double>(r.beats.beats.size()) > 0.8 * r.duration_s * r.beats.bpm / 60.0);
    }
    if (ran == 0) SKIP("tracks 02/03 not present");
}
