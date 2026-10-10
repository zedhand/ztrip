#include "mre/analysis/offline.hpp"
#include "mre/serialize/sigcache.hpp"

#include "synth.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cstring>
#include <filesystem>

using namespace mre;

namespace {

io::AudioBuffer groove_audio() {
    io::AudioBuffer a;
    a.sample_rate = 44100;
    a.channels = 1;
    a.samples = test::groove(a.sample_rate, 120.0, 4, 0.2);
    return a;
}

} // namespace

TEST_CASE("analysis produces every named signal at 120 Hz", "[pipeline]") {
    const auto audio = groove_audio();
    const auto r = analysis::analyze(audio, {});
    CHECK(r.signal_rate_hz == 120.0);
    CHECK(r.duration_s == Catch::Approx(audio.duration_seconds()));
    const auto expected = static_cast<std::size_t>(r.duration_s * 120.0) + 1;
    for (const char* band : {"Sub", "Bass", "LowMid", "Mid", "HighMid", "Air"}) {
        for (const std::string prefix : {"band.", "env."}) {
            const auto* s = r.find_signal(prefix + band);
            REQUIRE(s != nullptr);
            CHECK(s->values.size() == expected);
            for (float v : s->values) REQUIRE((v >= 0.0f && v <= 1.0f));
        }
        CHECK(r.find_events(std::string("onset.") + band) != nullptr);
    }
    CHECK(r.find_signal("rms") != nullptr);
    CHECK(r.find_signal("centroid") != nullptr);
    CHECK(r.find_events("onset.full") != nullptr);
    CHECK(r.find_events("beat")->events.size() == r.beats.beats.size());
    CHECK(r.find_events("downbeat")->events.size() == r.beats.downbeats.size());
}

TEST_CASE("kick envelope rises on kicks and decays between them", "[pipeline]") {
    const auto r = analysis::analyze(groove_audio(), {});
    const auto& env = *r.find_signal("env.Sub");
    const float on_kick = r.sample(env, 0.2 + 0.5 * 4 + 0.03);  // just after a kick
    const float before = r.sample(env, 0.2 + 0.5 * 4 - 0.01);   // just before it
    CHECK(on_kick > before);
    CHECK(on_kick > 0.7f);
}

TEST_CASE("analysis is bit-for-bit deterministic", "[pipeline]") {
    const auto audio = groove_audio();
    const analysis::AnalysisSettings settings;
    const serialize::SigcacheKey key{analysis::audio_hash(audio), analysis::settings_hash(settings)};
    const auto a = serialize::encode_sigcache(key, analysis::analyze(audio, settings));
    const auto b = serialize::encode_sigcache(key, analysis::analyze(audio, settings));
    CHECK(a == b);
}

TEST_CASE("cache keys change with audio and settings", "[pipeline]") {
    auto audio = groove_audio();
    const analysis::AnalysisSettings base;
    const auto h0 = analysis::audio_hash(audio);
    audio.samples[1000] += 1e-6f;
    CHECK(analysis::audio_hash(audio) != h0);

    analysis::AnalysisSettings changed = base;
    changed.envelope.release_ms = 200.0f;
    CHECK(analysis::settings_hash(changed) != analysis::settings_hash(base));
    changed = base;
    changed.stft.window = analysis::Window::Hamming;
    CHECK(analysis::settings_hash(changed) != analysis::settings_hash(base));
    CHECK(analysis::settings_hash(base) == analysis::settings_hash(analysis::AnalysisSettings{}));
}

TEST_CASE("sigcache round-trips and rejects stale or corrupt data", "[sigcache]") {
    const auto audio = groove_audio();
    const analysis::AnalysisSettings settings;
    const serialize::SigcacheKey key{analysis::audio_hash(audio), analysis::settings_hash(settings)};
    const auto result = analysis::analyze(audio, settings);
    const auto bytes = serialize::encode_sigcache(key, result);

    const auto decoded = serialize::decode_sigcache(bytes, key);
    REQUIRE(decoded.has_value());
    CHECK(serialize::encode_sigcache(key, *decoded) == bytes);
    CHECK(decoded->beats.bpm == result.beats.bpm);
    CHECK(decoded->find_signal("env.Bass")->values == result.find_signal("env.Bass")->values);

    CHECK_FALSE(serialize::decode_sigcache(bytes, {key.audio_hash, key.settings_hash + 1}));
    auto truncated = bytes;
    truncated.resize(bytes.size() - 3);
    CHECK_FALSE(serialize::decode_sigcache(truncated, key));
    auto bad_count = bytes;
    std::memset(bad_count.data() + 64, 0xFF, 4); // first signal's name length
    CHECK_FALSE(serialize::decode_sigcache(bad_count, key));
    auto bad_magic = bytes;
    bad_magic[0] = 'X';
    CHECK_FALSE(serialize::decode_sigcache(bad_magic, key));

    const auto path = std::filesystem::temp_directory_path() / "mre_test.sigcache";
    serialize::write_sigcache(path, key, result);
    const auto from_file = serialize::read_sigcache(path, key);
    std::filesystem::remove(path);
    REQUIRE(from_file.has_value());
    CHECK(serialize::encode_sigcache(key, *from_file) == bytes);
    CHECK_FALSE(serialize::read_sigcache(path, key)); // missing file
}
