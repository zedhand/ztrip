// Accuracy on the project's reference tracks. The audio isn't in the repo
// (tracks 2 and 3 are CC BY and need attribution if distributed), so these
// tests skip unless ZTRIP_REFERENCE_TRACKS points at a folder holding
// 01_four_on_the_floor_128bpm.wav and its .truth.json (and optionally 02/03).
#include "ztrip/analysis/offline.hpp"
#include "ztrip/io/wav.hpp"
#include "ztrip/time/autodraft.hpp"
#include "ztrip/time/tempo_map.hpp"

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

// Reads the number stored under "key": in the truth file.
double truth_number(const fs::path& file, const std::string& key) {
    std::ifstream in(file);
    std::stringstream ss;
    ss << in.rdbuf();
    const std::string text = ss.str();
    const auto at = text.find("\"" + key + "\"");
    REQUIRE(at != std::string::npos);
    return std::stod(text.substr(text.find(':', at) + 1));
}

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

    // Phase 4 exit: the kick lane lands within 20 ms of every true hit.
    const Score kicks = score(r.find_events("kick")->events, truth_times(truth, "kick"), 0.020);
    INFO("kick detector P=" << kicks.precision << " R=" << kicks.recall << " err=" << kicks.mean_abs_error);
    CHECK(kicks.recall >= 0.99);
    CHECK(kicks.precision >= 0.99);
    CHECK(kicks.mean_abs_error < 0.003);

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

TEST_CASE("tracks 02 and 03 find the tempo a listener claps to", "[reference]") {
    const fs::path dir = reference_dir();
    if (dir.empty()) SKIP("set ZTRIP_REFERENCE_TRACKS to the reference-tracks folder to run");
    // No ground truth for these. The tempos are where ztrip and librosa's
    // beat tracker agree, and track 02's 89 BPM was confirmed by clapping
    // along to a click track (librosa and the old ztrip read 89 and 118).
    struct Expect {
        const char* name;
        double bpm;
    };
    int ran = 0;
    for (const Expect e : {Expect{"02_live_band_lets_go_fishin.wav", 89.0},
                           Expect{"03_syncopated_jazz_vibe_ace.wav", 129.5}}) {
        const fs::path wav = dir / e.name;
        if (!fs::exists(wav)) continue;
        ++ran;
        const auto r = analysis::analyze(io::read_wav(wav), {});
        INFO(e.name << ": " << r.beats.bpm << " BPM, " << r.beats.beats.size() << " beats");
        CHECK(r.beats.bpm == Catch::Approx(e.bpm).epsilon(0.03));
        // Beats should cover most of the track at the reported tempo.
        CHECK(static_cast<double>(r.beats.beats.size()) > 0.8 * r.duration_s * r.beats.bpm / 60.0);
    }
    if (ran == 0) SKIP("tracks 02/03 not present");
}

TEST_CASE("track 01: the auto-drafted kick lane lands within 20 ms of true kicks", "[reference][time]") {
    const fs::path dir = reference_dir();
    const fs::path wav = dir / "01_four_on_the_floor_128bpm.wav";
    const fs::path truth = dir / "01_four_on_the_floor_128bpm.truth.json";
    if (dir.empty() || !fs::exists(wav) || !fs::exists(truth)) {
        SKIP("set ZTRIP_REFERENCE_TRACKS to the reference-tracks folder to run");
    }
    const auto r = analysis::analyze(io::read_wav(wav), {});
    const auto tempo = time::tempo_map_from_beats(r.beats);
    const auto seq = time::auto_draft(r, tempo);
    const auto* lane = seq.find_lane("Kick");
    REQUIRE(lane);

    std::vector<analysis::Trigger> drafted;
    for (const auto& n : lane->notes) drafted.push_back({tempo.seconds_at(n.start_beat), n.velocity});
    const Score kick = score(drafted, truth_times(truth, "kick"), 0.020);
    INFO("drafted kick lane: " << drafted.size() << " notes, P=" << kick.precision << " R=" << kick.recall
                               << " err=" << kick.mean_abs_error);
    // Drafted from the kick detector, so it should be as exact as the detector.
    CHECK(kick.recall >= 0.99);
    CHECK(kick.precision >= 0.99);
    CHECK(kick.mean_abs_error < 0.003);
}

TEST_CASE("genre tracks 04-12 match their ground truth", "[reference]") {
    const fs::path dir = reference_dir();
    if (dir.empty()) SKIP("set ZTRIP_REFERENCE_TRACKS to the reference-tracks folder to run");
    // Metal, EDM and rock with ground truth (see the folder's README). 04-08
    // and 12 are human drummers from E-GMD; their truth includes ghost
    // notes, which the detectors skip by design, so recall there is lower.
    // The floors sit a little under the results when the detectors were
    // tuned, to catch regressions. F is the harmonic mean of precision and
    // recall; tolerances are 70 ms for beats, 25 ms for kicks, 30 ms for snares.
    struct Expect {
        const char* name;
        double tempo_ratio; // reported BPM / true BPM
        double beat_f, kick_f, snare_f;
    };
    const auto f_of = [](const Score& s) {
        return s.precision + s.recall > 0.0 ? 2.0 * s.precision * s.recall / (s.precision + s.recall) : 0.0;
    };
    int ran = 0;
    for (const Expect e : {
             Expect{"04_egmd_speed_metal_180bpm", 1.0, 0.95, 0.80, 0.68},
             Expect{"05_egmd_hard_rock_135bpm", 1.0, 0.95, 0.75, 0.65},
             Expect{"06_egmd_rock_128bpm", 1.0, 0.95, 0.76, 0.40},
             Expect{"07_egmd_disco_house_120bpm", 1.0, 0.95, 0.78, 0.70},
             // Known gap: drum and bass reads at half tempo (85 BPM), so beats are every other one.
             Expect{"08_egmd_dnb_170bpm", 0.5, 0.0, 0.77, 0.33},
             Expect{"09_synth_metal_190bpm", 1.0, 0.70, 0.75, 0.90},
             Expect{"10_synth_edm_sidechain_128bpm", 1.0, 0.95, 0.66, 0.80},
             Expect{"11_synth_rock_140bpm", 1.0, 0.95, 0.71, 0.95},
             Expect{"12_egmd_fast_rock_200bpm", 1.0, 0.95, 0.73, 0.40},
         }) {
        const fs::path wav = dir / (std::string(e.name) + ".wav");
        const fs::path truth = dir / (std::string(e.name) + ".truth.json");
        if (!fs::exists(wav) || !fs::exists(truth)) continue;
        ++ran;
        const auto r = analysis::analyze(io::read_wav(wav), {});
        const double bpm = truth_number(truth, "bpm");
        const Score beats = score(r.find_events("beat")->events, truth_times(truth, "beats"), 0.070);
        const Score kicks = score(r.find_events("kick")->events, truth_times(truth, "kick"), 0.025);
        const Score snares = score(r.find_events("snare")->events, truth_times(truth, "snare"), 0.030);
        INFO(e.name << ": " << r.beats.bpm << " BPM (true " << bpm << "), beats F=" << f_of(beats) << ", kicks P="
                    << kicks.precision << " R=" << kicks.recall << ", snares P=" << snares.precision
                    << " R=" << snares.recall);
        CHECK(r.beats.bpm / bpm == Catch::Approx(e.tempo_ratio).epsilon(0.02));
        CHECK(f_of(beats) >= e.beat_f);
        CHECK(f_of(kicks) >= e.kick_f);
        CHECK(kicks.mean_abs_error < 0.015);
        CHECK(f_of(snares) >= e.snare_f);
    }
    if (ran == 0) SKIP("genre tracks 04-12 not present");
}
