// analyze-cli: offline analysis and plot dump for tuning.
//
// Analyses a WAV file, stores the result in a .sigcache next to the CSVs
// (reused on the next run if audio and settings are unchanged), and writes
// signals.csv, events.csv, beats.csv plus plot.py, which renders PNGs with
// matplotlib. It also drafts musical time: a tempo map fitted to the
// detected beats and a Note Sequence with an auto-drafted Kick lane, and can
// render that lane as clicks over the track for checking by ear.
#include "ztrip/analysis/offline.hpp"
#include "ztrip/io/wav.hpp"
#include "ztrip/serialize/nseq.hpp"
#include "ztrip/serialize/sigcache.hpp"
#include "ztrip/serialize/tempo.hpp"
#include "ztrip/time/autodraft.hpp"
#include "ztrip/time/tempo_map.hpp"
#include "ztrip/version.hpp"

#include "plot_script.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <string>
#include <string_view>

namespace fs = std::filesystem;
using namespace ztrip;

namespace {

constexpr std::string_view kUsage = R"(usage: analyze-cli <input.wav> [options]

Runs offline analysis (bands, envelopes, onsets, beats) and writes:
  <out>/<name>.sigcache   binary analysis cache, reused when unchanged
  <out>/signals.csv       120 Hz signals (band.*, env.*, rms, centroid)
  <out>/events.csv        onsets per band, beats and downbeats
  <out>/<name>.tempo.json tempo map fitted to the detected beats
  <out>/<name>.nseq.json  Note Sequence with an auto-drafted Kick lane
  <out>/beats.csv         beat grid with bar/beat numbers
  <out>/plot.py           renders PNG plots: python3 plot.py [--start S --end S]

options:
  -o, --out DIR         output directory (default: ./<name>-analysis)
  --fft N               FFT size, power of two 512..8192 (default 2048)
  --hop N               hop size in samples (default fft/4)
  --window W            hann | hamming | blackman-harris (default hann)
  --channel C           mix (default) or a 0-based channel index
  --attack MS           envelope attack in ms (default 5)
  --release MS          envelope release in ms (default 150)
  --hold MS             envelope hold in ms (default 0)
  --sensitivity X       onset threshold multiplier, lower = more onsets (default 1.5)
  --min-gap MS          minimum time between onsets in ms (default 50)
  --bpm-prior BPM       centre of the tempo prior (default 120)
  --beats-per-bar N     beats per bar for downbeats (default 4)
  --grid G              auto-draft grid, 1/1..1/64, T for triplets (default 1/16)
  --kick-events NAME    event list the Kick lane is drafted from (default onset.Sub)
  --nudge MS            shift the tempo map (and drafted notes) by MS milliseconds
  --clicks              also write <out>/kick_clicks.wav: the track with a click
                        on every drafted kick, for checking the draft by ear
  --no-cache            ignore any existing .sigcache
  -v, --version         print the version
  -h, --help            print this help
)";

struct Options {
    fs::path input;
    fs::path out;
    analysis::AnalysisSettings settings;
    bool use_cache = true;
    time::DraftSettings draft;
    double nudge_ms = 0.0;
    bool clicks = false;
};

template <typename T>
bool parse_number(std::string_view s, T& out) {
    const auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), out);
    return ec == std::errc() && ptr == s.data() + s.size();
}

// Returns an exit code if the program should stop, nullopt to continue.
std::optional<int> parse_args(int argc, char** argv, Options& opt) {
    auto& s = opt.settings;
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        auto value = [&]() -> std::optional<std::string_view> {
            if (i + 1 >= argc) {
                std::cerr << "analyze-cli: " << arg << " needs a value\n";
                return std::nullopt;
            }
            return std::string_view(argv[++i]);
        };
        auto number = [&](auto& out) {
            const auto v = value();
            if (!v) return false;
            if (!parse_number(*v, out)) {
                std::cerr << "analyze-cli: bad value for " << arg << ": " << *v << '\n';
                return false;
            }
            return true;
        };

        if (arg == "-h" || arg == "--help") {
            std::cout << kUsage;
            return 0;
        } else if (arg == "-v" || arg == "--version") {
            std::cout << "analyze-cli " << version() << '\n';
            return 0;
        } else if (arg == "-o" || arg == "--out") {
            const auto v = value();
            if (!v) return 2;
            opt.out = fs::path(std::string(*v));
        } else if (arg == "--fft") {
            if (!number(s.stft.fft_size)) return 2;
        } else if (arg == "--hop") {
            if (!number(s.stft.hop)) return 2;
        } else if (arg == "--window") {
            const auto v = value();
            if (!v) return 2;
            if (*v == "hann") s.stft.window = analysis::Window::Hann;
            else if (*v == "hamming") s.stft.window = analysis::Window::Hamming;
            else if (*v == "blackman-harris") s.stft.window = analysis::Window::BlackmanHarris;
            else {
                std::cerr << "analyze-cli: unknown window " << *v << '\n';
                return 2;
            }
        } else if (arg == "--channel") {
            const auto v = value();
            if (!v) return 2;
            if (*v == "mix") s.channel = -1;
            else if (!parse_number(*v, s.channel) || s.channel < 0) {
                std::cerr << "analyze-cli: bad channel " << *v << '\n';
                return 2;
            }
        } else if (arg == "--attack") {
            if (!number(s.envelope.attack_ms)) return 2;
        } else if (arg == "--release") {
            if (!number(s.envelope.release_ms)) return 2;
        } else if (arg == "--hold") {
            if (!number(s.envelope.hold_ms)) return 2;
        } else if (arg == "--sensitivity") {
            if (!number(s.onsets.threshold)) return 2;
        } else if (arg == "--min-gap") {
            float ms = 0.0f;
            if (!number(ms)) return 2;
            s.onsets.min_gap_s = ms / 1000.0f;
        } else if (arg == "--bpm-prior") {
            if (!number(s.beats.prior_bpm) || s.beats.prior_bpm <= 0.0f) return 2;
        } else if (arg == "--beats-per-bar") {
            if (!number(s.beats.beats_per_bar) || s.beats.beats_per_bar < 1) return 2;
        } else if (arg == "--grid") {
            const auto v = value();
            if (!v) return 2;
            const auto g = time::parse_grid(*v);
            if (!g) {
                std::cerr << "analyze-cli: --grid must look like 1/16 or 1/8T\n";
                return 2;
            }
            opt.draft.grid = *g;
        } else if (arg == "--kick-events") {
            const auto v = value();
            if (!v) return 2;
            opt.draft.lanes = {{"Kick", std::string(*v), 0.0f}};
        } else if (arg == "--nudge") {
            if (!number(opt.nudge_ms)) return 2;
        } else if (arg == "--clicks") {
            opt.clicks = true;
        } else if (arg == "--no-cache") {
            opt.use_cache = false;
        } else if (!arg.empty() && arg[0] == '-') {
            std::cerr << "analyze-cli: unknown option " << arg << "\n\n" << kUsage;
            return 2;
        } else if (opt.input.empty()) {
            opt.input = fs::path(std::string(arg));
        } else {
            std::cerr << "analyze-cli: more than one input given\n";
            return 2;
        }
    }
    if (opt.input.empty()) {
        std::cerr << kUsage;
        return 2;
    }
    if (opt.out.empty()) opt.out = fs::path(opt.input.stem().string() + "-analysis");
    return std::nullopt;
}

std::ofstream open_out(const fs::path& path) {
    std::ofstream out(path, std::ios::trunc);
    if (!out) throw std::runtime_error("cannot write " + path.string());
    out.setf(std::ios::fixed);
    return out;
}

void write_signals_csv(const fs::path& path, const analysis::AnalysisResult& r) {
    auto out = open_out(path);
    out.precision(5);
    out << "time";
    std::size_t rows = 0;
    for (const auto& s : r.signals) {
        out << ',' << s.name;
        rows = std::max(rows, s.values.size());
    }
    out << '\n';
    for (std::size_t i = 0; i < rows; ++i) {
        out << static_cast<double>(i) / r.signal_rate_hz;
        for (const auto& s : r.signals) out << ',' << (i < s.values.size() ? s.values[i] : 0.0f);
        out << '\n';
    }
}

void write_events_csv(const fs::path& path, const analysis::AnalysisResult& r) {
    auto out = open_out(path);
    out.precision(5);
    out << "signal,time,velocity\n";
    for (const auto& list : r.events) {
        for (const auto& e : list.events) out << list.name << ',' << e.time << ',' << e.velocity << '\n';
    }
}

void write_beats_csv(const fs::path& path, const analysis::AnalysisResult& r) {
    auto out = open_out(path);
    out.precision(5);
    out << "time,bar,beat,downbeat\n";
    const auto& g = r.beats;
    const int bpb = std::max(1, g.beats_per_bar);
    for (std::size_t i = 0; i < g.beats.size(); ++i) {
        // Bars count from the first downbeat; pickup beats belong to bar 0.
        const int k = static_cast<int>(i) - g.downbeat_phase;
        const int bar = k >= 0 ? k / bpb + 1 : 0;
        const int beat = ((k % bpb) + bpb) % bpb + 1;
        out << g.beats[i] << ',' << bar << ',' << beat << ',' << (beat == 1 && bar > 0 ? 1 : 0) << '\n';
    }
}

// 16-bit stereo WAV of a mono signal.
void write_wav16(const fs::path& path, const std::vector<float>& mono, int sample_rate) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) throw std::runtime_error("cannot write " + path.string());
    auto u32 = [&](std::uint32_t v) { out.write(reinterpret_cast<const char*>(&v), 4); };
    auto u16 = [&](std::uint16_t v) { out.write(reinterpret_cast<const char*>(&v), 2); };
    const auto data_bytes = static_cast<std::uint32_t>(mono.size() * 4);
    out.write("RIFF", 4);
    u32(36 + data_bytes);
    out.write("WAVEfmt ", 8);
    u32(16);
    u16(1);
    u16(2);
    u32(static_cast<std::uint32_t>(sample_rate));
    u32(static_cast<std::uint32_t>(sample_rate) * 4);
    u16(4);
    u16(16);
    out.write("data", 4);
    u32(data_bytes);
    for (float x : mono) {
        const auto s = static_cast<std::int16_t>(std::lround(std::clamp(x, -1.0f, 1.0f) * 32767.0f));
        u16(static_cast<std::uint16_t>(s));
        u16(static_cast<std::uint16_t>(s));
    }
}

// The track at half level with a short 1.5 kHz click on every note of a lane.
std::vector<float> click_mix(const io::AudioBuffer& audio, const time::Lane& lane, const time::TempoMap& tempo) {
    std::vector<float> mix = io::to_mono(audio);
    for (float& x : mix) x *= 0.5f;
    const int sr = audio.sample_rate;
    const auto click_len = static_cast<std::size_t>(0.03 * sr);
    for (const auto& n : lane.notes) {
        const double t = tempo.seconds_at(n.start_beat);
        if (t < 0.0) continue;
        const auto start = static_cast<std::size_t>(std::lround(t * sr));
        for (std::size_t i = 0; i < click_len && start + i < mix.size(); ++i) {
            const double s = static_cast<double>(i) / sr;
            mix[start + i] += static_cast<float>(0.6 * std::sin(2.0 * 3.141592653589793 * 1500.0 * s) *
                                                 std::exp(-s / 0.006));
        }
    }
    return mix;
}

} // namespace

int main(int argc, char** argv) {
    Options opt;
    if (const auto code = parse_args(argc, argv, opt)) return *code;

    try {
        const io::AudioBuffer audio = io::read_wav(opt.input);
        if (opt.settings.channel >= audio.channels) {
            std::cerr << "analyze-cli: channel " << opt.settings.channel << " not in a "
                      << audio.channels << "-channel file\n";
            return 2;
        }
        fs::create_directories(opt.out);

        const serialize::SigcacheKey key{analysis::audio_hash(audio),
                                         analysis::settings_hash(opt.settings)};
        const fs::path cache = opt.out / (opt.input.stem().string() + ".sigcache");
        std::optional<analysis::AnalysisResult> result;
        if (opt.use_cache) result = serialize::read_sigcache(cache, key);
        const bool cached = result.has_value();
        if (!result) {
            result = analysis::analyze(audio, opt.settings);
            serialize::write_sigcache(cache, key, *result);
        }
        const auto& r = *result;

        write_signals_csv(opt.out / "signals.csv", r);
        write_events_csv(opt.out / "events.csv", r);
        write_beats_csv(opt.out / "beats.csv", r);
        open_out(opt.out / "plot.py") << tools::kPlotScript;

        time::TempoMap tempo = time::tempo_map_from_beats(r.beats);
        tempo.set_nudge(opt.nudge_ms / 1000.0);
        const time::NoteSequence draft = time::auto_draft(r, tempo, opt.draft);
        const fs::path tempo_path = opt.out / (opt.input.stem().string() + ".tempo.json");
        const fs::path nseq_path = opt.out / (opt.input.stem().string() + ".nseq.json");
        serialize::save_tempo_map(tempo_path, tempo);
        serialize::save_nseq(nseq_path, draft);

        std::printf("input     %s\n", opt.input.string().c_str());
        std::printf("audio     %.2f s, %d Hz, %d ch\n", audio.duration_seconds(), audio.sample_rate,
                    audio.channels);
        std::printf("analysis  %s (%s)\n", cache.string().c_str(), cached ? "cached" : "computed");
        std::printf("tempo     %.2f BPM, %zu beats, %zu downbeats\n", r.beats.bpm,
                    r.beats.beats.size(), r.beats.downbeats.size());
        for (const auto& list : r.events) {
            if (list.name.rfind("onset.", 0) == 0) {
                std::printf("%-16s %zu\n", list.name.c_str(), list.events.size());
            }
        }
        std::printf("tempo map %s (%zu segments)\n", tempo_path.string().c_str(), tempo.segments().size());
        for (const auto& lane : draft.lanes) {
            std::printf("lane      %s: %zu notes -> %s\n", lane.name.c_str(), lane.notes.size(),
                        nseq_path.string().c_str());
        }
        if (opt.clicks && !draft.lanes.empty()) {
            const fs::path clicks = opt.out / "kick_clicks.wav";
            write_wav16(clicks, click_mix(audio, draft.lanes.front(), tempo), audio.sample_rate);
            std::printf("clicks    %s\n", clicks.string().c_str());
        }
        std::printf("plots     python3 %s\n", (opt.out / "plot.py").string().c_str());
    } catch (const std::exception& e) {
        std::cerr << "analyze-cli: " << e.what() << '\n';
        return 1;
    }
    return 0;
}
