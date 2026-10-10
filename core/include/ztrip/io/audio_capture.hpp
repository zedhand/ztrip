#pragma once

#include "ztrip/analysis/live.hpp"
#include "ztrip/io/spsc_ring.hpp"
#include "ztrip/io/wav.hpp"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <thread>
#include <vector>

namespace ztrip::io {

struct AudioDeviceInfo {
    std::string name;
    bool is_default = false;
    // A device that records what the computer plays: a WASAPI loopback of an
    // output on Windows, a PulseAudio/PipeWire "Monitor of" source on Linux.
    bool loopback = false;
};

struct CaptureSettings {
    // Device name as listed by list_capture_devices; empty for the default.
    // A device whose name contains this text also matches.
    std::string device;
    // Record what the computer plays instead of a microphone.
    bool loopback = false;
    int channel = -1;            // -1 sums all channels to mono
    int sample_rate = 0;         // 0 for the device's own rate
    double ring_seconds = 2.0;   // audio held between the device thread and update()
    // Testing: use miniaudio's null backend, which delivers silence in real
    // time with no device.
    bool null_backend = false;
};

// Microphones and loopback sources this machine offers. Never throws; an
// empty list means no audio backend could start.
std::vector<AudioDeviceInfo> list_capture_devices();

// Captures mono audio from a device on miniaudio's device thread into a
// lock-free ring, to be read from another thread.
class AudioCapture {
public:
    // Opens and starts the device. Throws std::runtime_error naming the
    // device and the reason when it can't.
    explicit AudioCapture(const CaptureSettings& settings);
    ~AudioCapture();
    AudioCapture(const AudioCapture&) = delete;
    AudioCapture& operator=(const AudioCapture&) = delete;

    int sample_rate() const noexcept;
    const std::string& device_name() const noexcept;

    // Consumer side of the ring; see SpscRing.
    std::size_t read(std::span<float> out) noexcept;
    std::size_t available() const noexcept;
    std::uint64_t take_dropped() noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// A live audio source for a graph: a capture device (or a WAV file played in
// real time, to try a setup without a device) feeding a LiveAnalyzer. Call
// update() on the thread that evaluates the graph, then evaluate at time().
class LiveAudioInput {
public:
    LiveAudioInput(const CaptureSettings& capture, analysis::LiveSettings settings = {});
    // Plays `wav` into the analyser in real time from its own thread, looping
    // if asked, as a stand-in for a device.
    LiveAudioInput(const std::filesystem::path& wav, bool loop, analysis::LiveSettings settings = {});
    LiveAudioInput(const AudioBuffer& audio, std::string name, bool loop, analysis::LiveSettings settings = {});
    ~LiveAudioInput();
    LiveAudioInput(const LiveAudioInput&) = delete;
    LiveAudioInput& operator=(const LiveAudioInput&) = delete;

    // Analyses everything captured since the last call.
    void update();

    const analysis::LiveAnalyzer& analyzer() const noexcept { return *analyzer_; }
    const analysis::AnalysisResult& result() const noexcept { return analyzer_->result(); }
    double time() const noexcept { return analyzer_->time(); }
    const std::string& source_name() const noexcept { return name_; }
    // Samples lost because update() was not called often enough.
    std::uint64_t dropped() const noexcept { return dropped_; }

private:
    std::unique_ptr<AudioCapture> capture_;
    std::unique_ptr<SpscRing<float>> file_ring_;
    std::thread file_thread_;
    std::atomic<bool> stop_{false};
    std::unique_ptr<analysis::LiveAnalyzer> analyzer_;
    std::vector<float> scratch_;
    std::string name_;
    std::uint64_t dropped_ = 0;
};

} // namespace ztrip::io
