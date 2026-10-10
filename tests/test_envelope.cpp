#include "mre/analysis/envelope.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

using namespace mre::analysis;

TEST_CASE("attack and release reach 63% after one time constant", "[envelope]") {
    const double rate = 1000.0;
    std::vector<float> step(400, 0.0f);
    for (std::size_t i = 0; i < 200; ++i) step[i] = 1.0f;
    const auto env = follow_envelope(step, rate, {10.0f, 50.0f, 0.0f});
    CHECK(env[9] == Catch::Approx(1.0 - std::exp(-1.0)).epsilon(0.01));   // 10 samples = 10 ms
    CHECK(env[199] == Catch::Approx(1.0f).epsilon(1e-4));
    const float peak = env[199];
    CHECK(env[249] == Catch::Approx(peak * std::exp(-1.0)).epsilon(0.01)); // 50 ms release
}

TEST_CASE("hold keeps the peak before releasing", "[envelope]") {
    std::vector<float> pulse(100, 0.0f);
    pulse[0] = 1.0f;
    const auto env = follow_envelope(pulse, 1000.0, {0.0f, 10.0f, 20.0f});
    CHECK(env[0] == 1.0f);
    CHECK(env[20] == 1.0f);
    CHECK(env[30] < 0.5f);
}

TEST_CASE("linear resampling interpolates and holds the ends", "[envelope]") {
    const std::vector<float> v = {0.0f, 1.0f, 2.0f};
    const auto r = resample_linear(v, 1.0, 2.0, 7);
    CHECK(r[1] == Catch::Approx(0.5f));
    CHECK(r[4] == Catch::Approx(2.0f));
    CHECK(r[6] == Catch::Approx(2.0f));
}
