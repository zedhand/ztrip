#include "ztrip/io/audio_capture.hpp"

#include "ztrip/io/wav.hpp"

#include <miniaudio.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <stdexcept>
#include <string_view>

namespace ztrip::io {
namespace {

bool is_monitor(std::string_view name) {
    return name.find("Monitor of") != std::string_view::npos;
}

bool matches(std::string_view name, std::string_view wanted) {
    return wanted.empty() || name == wanted || name.find(wanted) != std::string_view::npos;
}

struct Context {
    ma_context ctx{};
    bool ok = false;

    explicit Context(bool null_backend) {
        ma_backend null_only[] = {ma_backend_null};
        ok = ma_context_init(null_backend ? null_only : nullptr, null_backend ? 1u : 0u, nullptr, &ctx) == MA_SUCCESS;
        // miniaudio falls back to its null backend when no real one loads;
        // that would record silence without saying so.
        if (ok && !null_backend && ctx.backend == ma_backend_null) {
            ma_context_uninit(&ctx);
            ok = false;
        }
    }
    ~Context() {
        if (ok) ma_context_uninit(&ctx);
    }
    Context(const Context&) = delete;
    Context& operator=(const Context&) = delete;

    struct Devices {
        ma_device_info* playback = nullptr;
        ma_uint32 playback_count = 0;
        ma_device_info* capture = nullptr;
        ma_uint32 capture_count = 0;
    };
    Devices devices() {
        Devices d;
        if (ok) ma_context_get_devices(&ctx, &d.playback, &d.playback_count, &d.capture, &d.capture_count);
        return d;
    }
    // WASAPI can record any output directly; elsewhere loopback means a
    // monitor source.
    bool native_loopback() const { return ok && ctx.backend == ma_backend_wasapi; }
};

} // namespace

std::vector<AudioDeviceInfo> list_capture_devices() {
    std::vector<AudioDeviceInfo> out;
    Context context(false);
    const auto d = context.devices();
    for (ma_uint32 i = 0; i < d.capture_count; ++i) {
        out.push_back({d.capture[i].name, d.capture[i].isDefault != 0, is_monitor(d.capture[i].name)});
    }
    if (context.native_loopback()) {
        for (ma_uint32 i = 0; i < d.playback_count; ++i) {
            out.push_back({d.playback[i].name, d.playback[i].isDefault != 0, true});
        }
    }
    return out;
}

struct AudioCapture::Impl {
    Context context;
    ma_device device{};
    bool device_ok = false;
    int channel = -1;
    std::uint32_t channels = 1;
    std::string name;
    SpscRing<float> ring;
    std::vector<float> mono; // callback scratch, preallocated

    Impl(const CaptureSettings& s, std::size_t ring_size)
        : context(s.null_backend), channel(s.channel), ring(ring_size), mono(16384) {}
    ~Impl() {
        if (device_ok) ma_device_uninit(&device);
    }

    static void on_data(ma_device* dev, void*, const void* input, ma_uint32 frames) {
        auto* self = static_cast<Impl*>(dev->pUserData);
        const auto* in = static_cast<const float*>(input);
        if (in == nullptr) return;
        const std::uint32_t ch = self->channels;
        std::size_t done = 0;
        while (done < frames) {
            const std::size_t n = std::min<std::size_t>(frames - done, self->mono.size());
            for (std::size_t i = 0; i < n; ++i) {
                const float* frame = in + (done + i) * ch;
                if (self->channel >= 0) {
                    self->mono[i] = frame[std::min<std::uint32_t>(static_cast<std::uint32_t>(self->channel), ch - 1)];
                } else {
                    float sum = 0.0f;
                    for (std::uint32_t c = 0; c < ch; ++c) sum += frame[c];
                    self->mono[i] = sum / static_cast<float>(ch);
                }
            }
            self->ring.write(std::span<const float>(self->mono.data(), n));
            done += n;
        }
    }
};

AudioCapture::AudioCapture(const CaptureSettings& s) {
    const int rate_guess = s.sample_rate > 0 ? s.sample_rate : 48000;
    const auto ring_size = static_cast<std::size_t>(std::max(0.1, s.ring_seconds) * std::max(rate_guess, 96000));
    impl_ = std::make_unique<Impl>(s, ring_size);
    Context& context = impl_->context;
    if (!context.ok) {
        throw std::runtime_error(
            "audio capture: no audio system found (WASAPI on Windows; PulseAudio, PipeWire or ALSA on Linux)");
    }

    const auto d = context.devices();
    const ma_device_id* id = nullptr;
    std::string name = "default input";
    ma_device_type type = ma_device_type_capture;
    if (s.loopback && context.native_loopback()) {
        type = ma_device_type_loopback;
        name = "default output (loopback)";
        for (ma_uint32 i = 0; i < d.playback_count; ++i) {
            if (!s.device.empty() && matches(d.playback[i].name, s.device)) {
                id = &d.playback[i].id;
                name = std::string(d.playback[i].name) + " (loopback)";
                break;
            }
        }
        if (!s.device.empty() && id == nullptr) {
            throw std::runtime_error("audio capture: no output named \"" + s.device + "\" to record");
        }
    } else if (s.loopback && !s.null_backend) {
        // Prefer the monitor of the default output, then any monitor.
        std::string default_out;
        for (ma_uint32 i = 0; i < d.playback_count; ++i) {
            if (d.playback[i].isDefault) default_out = d.playback[i].name;
        }
        const ma_device_info* best = nullptr;
        for (ma_uint32 i = 0; i < d.capture_count; ++i) {
            const std::string_view n = d.capture[i].name;
            if (!is_monitor(n) || !matches(n, s.device)) continue;
            if (best == nullptr || (!default_out.empty() && n.find(default_out) != std::string_view::npos)) {
                best = &d.capture[i];
            }
        }
        if (best == nullptr) {
            throw std::runtime_error(
                "audio capture: no loopback source found (on Linux this needs PulseAudio or PipeWire, "
                "which list a \"Monitor of\" source for each output)");
        }
        id = &best->id;
        name = best->name;
    } else if (!s.device.empty()) {
        for (ma_uint32 i = 0; i < d.capture_count; ++i) {
            if (matches(d.capture[i].name, s.device)) {
                id = &d.capture[i].id;
                name = d.capture[i].name;
                break;
            }
        }
        if (id == nullptr) throw std::runtime_error("audio capture: no input named \"" + s.device + "\"");
    } else {
        for (ma_uint32 i = 0; i < d.capture_count; ++i) {
            if (d.capture[i].isDefault) name = d.capture[i].name;
        }
    }

    ma_device_config config = ma_device_config_init(type);
    config.capture.pDeviceID = id;
    config.capture.format = ma_format_f32;
    config.capture.channels = 0; // the device's own, so one channel can be picked
    config.sampleRate = s.sample_rate > 0 ? static_cast<ma_uint32>(s.sample_rate) : 0;
    config.periodSizeInMilliseconds = 10;
    config.dataCallback = &Impl::on_data;
    config.pUserData = impl_.get();
    if (ma_device_init(&context.ctx, &config, &impl_->device) != MA_SUCCESS) {
        throw std::runtime_error("audio capture: cannot open " + name);
    }
    impl_->device_ok = true;
    impl_->channels = std::max<ma_uint32>(1, impl_->device.capture.channels);
    impl_->name = name;
    if (ma_device_start(&impl_->device) != MA_SUCCESS) {
        throw std::runtime_error("audio capture: cannot start " + name);
    }
}

AudioCapture::~AudioCapture() = default;

int AudioCapture::sample_rate() const noexcept { return static_cast<int>(impl_->device.sampleRate); }
const std::string& AudioCapture::device_name() const noexcept { return impl_->name; }
std::size_t AudioCapture::read(std::span<float> out) noexcept { return impl_->ring.read(out); }
std::size_t AudioCapture::available() const noexcept { return impl_->ring.available(); }
std::uint64_t AudioCapture::take_dropped() noexcept { return impl_->ring.take_dropped(); }

LiveAudioInput::LiveAudioInput(const CaptureSettings& capture, analysis::LiveSettings settings)
    : capture_(std::make_unique<AudioCapture>(capture)),
      analyzer_(std::make_unique<analysis::LiveAnalyzer>(capture_->sample_rate(), std::move(settings))),
      scratch_(4096),
      name_(capture_->device_name()) {}

LiveAudioInput::LiveAudioInput(const std::filesystem::path& wav, bool loop, analysis::LiveSettings settings)
    : LiveAudioInput(read_wav(wav), wav.filename().string() + " (simulated input)", loop, std::move(settings)) {}

LiveAudioInput::LiveAudioInput(const AudioBuffer& audio, std::string name, bool loop, analysis::LiveSettings settings)
    : scratch_(4096), name_(std::move(name)) {
    analyzer_ = std::make_unique<analysis::LiveAnalyzer>(audio.sample_rate, std::move(settings));
    const int rate = audio.sample_rate;
    file_ring_ = std::make_unique<SpscRing<float>>(static_cast<std::size_t>(rate) * 2);
    file_thread_ = std::thread([this, mono = to_mono(audio), rate, loop] {
        // 10 ms blocks on a real-time schedule, like a device callback.
        const auto block = static_cast<std::size_t>(rate / 100);
        const auto start = std::chrono::steady_clock::now();
        std::size_t pos = 0;
        for (std::uint64_t sent = 0; !stop_.load(std::memory_order_relaxed);) {
            if (pos >= mono.size()) {
                if (!loop || mono.empty()) break;
                pos = 0;
            }
            const std::size_t n = std::min(block, mono.size() - pos);
            file_ring_->write(std::span<const float>(mono.data() + pos, n));
            pos += n;
            sent += n;
            std::this_thread::sleep_until(start + std::chrono::microseconds(sent * 1000000 / static_cast<std::uint64_t>(rate)));
        }
    });
}

LiveAudioInput::~LiveAudioInput() {
    stop_ = true;
    if (file_thread_.joinable()) file_thread_.join();
}

void LiveAudioInput::update() {
    auto read = [&](std::span<float> out) { return capture_ ? capture_->read(out) : file_ring_->read(out); };
    // Lost samples happened after what is in the ring, so fill them in after it.
    const std::uint64_t lost = capture_ ? capture_->take_dropped() : file_ring_->take_dropped();
    for (std::size_t n; (n = read(scratch_)) > 0;) analyzer_->push(std::span<const float>(scratch_.data(), n));
    if (lost > 0) {
        analyzer_->push_silence(static_cast<std::size_t>(lost));
        dropped_ += lost;
    }
}

} // namespace ztrip::io
