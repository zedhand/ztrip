// live-cli: checks live audio input from the command line.
//
// Lists capture devices, or captures one (a microphone, what the computer
// plays, or a WAV file played in real time) and prints band levels and
// onsets as they arrive, optionally with the outputs of a Motion Graph.
#include "ztrip/graph/compiler.hpp"
#include "ztrip/graph/evaluator.hpp"
#include "ztrip/io/audio_capture.hpp"
#include "ztrip/serialize/mgraph.hpp"
#include "ztrip/version.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <exception>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>

using namespace ztrip;

namespace {

constexpr std::string_view kUsage = R"(usage: live-cli [options]

Captures live audio and prints band levels (envelopes, 0-9) and onsets ten
times a second. With no source option it records the default microphone.

options:
  --list              list capture devices and exit
  --device NAME       capture from this device (or the first whose name contains NAME)
  --loopback          record what the computer plays (WASAPI loopback on Windows,
                      a PulseAudio/PipeWire monitor source on Linux)
  --channel N         use one channel instead of the mono sum
  --file FILE.wav     play a WAV file into the analysis in real time instead
  --loop              with --file, start again at the end
  --graph FILE        also evaluate this .mgraph.json live and print its outputs
  --seconds S         stop after S seconds (default: run until killed)
  --gain DB           input gain in dB (default 0)
  --version           print the version
)";

int fail(const std::string& message) {
    std::cerr << "live-cli: " << message << "\n";
    return 1;
}

} // namespace

int main(int argc, char** argv) {
    io::CaptureSettings capture;
    analysis::LiveSettings live;
    std::string file, graph_file;
    bool loop = false;
    double seconds = -1.0;
    try {
        for (int i = 1; i < argc; ++i) {
            const std::string_view a = argv[i];
            auto value = [&]() -> std::string {
                if (i + 1 >= argc) throw std::runtime_error(std::string(a) + " needs a value");
                return argv[++i];
            };
            if (a == "--list") {
                for (const auto& d : io::list_capture_devices()) {
                    std::cout << (d.is_default ? "* " : "  ") << d.name << (d.loopback ? "  [loopback]" : "") << "\n";
                }
                return 0;
            }
            if (a == "--device") capture.device = value();
            else if (a == "--loopback") capture.loopback = true;
            else if (a == "--channel") capture.channel = std::stoi(value());
            else if (a == "--file") file = value();
            else if (a == "--loop") loop = true;
            else if (a == "--graph") graph_file = value();
            else if (a == "--seconds") seconds = std::stod(value());
            else if (a == "--gain") live.gain_db = std::stof(value());
            else if (a == "--version") {
                std::cout << "live-cli " << version() << "\n";
                return 0;
            } else if (a == "-h" || a == "--help") {
                std::cout << kUsage;
                return 0;
            } else {
                return fail("unknown option " + std::string(a) + "\n\n" + std::string(kUsage));
            }
        }

        std::unique_ptr<io::LiveAudioInput> input =
            file.empty() ? std::make_unique<io::LiveAudioInput>(capture, live)
                         : std::make_unique<io::LiveAudioInput>(std::filesystem::path(file), loop, live);

        std::unique_ptr<graph::Evaluator> evaluator;
        if (!graph_file.empty()) {
            auto compiled = graph::compile(serialize::load_mgraph(graph_file));
            if (!compiled.ok()) return fail(graph_file + ":\n" + compiled.error_text());
            graph::EvaluatorSettings settings;
            settings.live = true;
            evaluator = std::make_unique<graph::Evaluator>(std::move(compiled.graph), settings);
            evaluator->bind_source("main", &input->result());
        }

        const auto& result = input->result();
        const auto& bands = input->analyzer().settings().bands;
        std::cout << "listening to " << input->source_name() << " at " << input->analyzer().sample_rate()
                  << " Hz; columns:";
        for (const auto& b : bands) std::cout << " " << b.name;
        std::cout << "\n";

        std::vector<std::size_t> seen(result.events.size(), 0);
        const auto start = std::chrono::steady_clock::now();
        auto next_print = start;
        while (seconds < 0.0 || input->time() < seconds) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            input->update();
            if (evaluator) evaluator->evaluate(input->time());
            if (std::chrono::steady_clock::now() < next_print) continue;
            next_print += std::chrono::milliseconds(100);

            char line[32];
            std::snprintf(line, sizeof line, "%8.2f ", input->time());
            std::string out = line;
            for (const auto& b : bands) {
                const auto* env = result.find_signal("env." + b.name);
                const int level = static_cast<int>(result.sample(*env, input->time()) * 9.99f);
                out += static_cast<char>('0' + std::clamp(level, 0, 9));
            }
            std::string onsets;
            for (std::size_t e = 0; e < result.events.size(); ++e) {
                const auto& list = result.events[e];
                if (list.events.size() > seen[e] && list.name.rfind("onset.", 0) == 0) {
                    onsets += " " + list.name.substr(6);
                }
                seen[e] = list.events.size();
            }
            out += onsets.empty() ? "" : "  onsets:" + onsets;
            if (evaluator) {
                for (const auto& o : evaluator->outputs()) {
                    std::snprintf(line, sizeof line, " %.2f", o.value);
                    out += "  " + o.target + line;
                }
            }
            std::cout << out << "\n" << std::flush;
            if (input->dropped() > 0) std::cerr << "dropped " << input->dropped() << " samples so far\n";
        }
        return 0;
    } catch (const std::exception& e) {
        return fail(e.what());
    }
}
