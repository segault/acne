#include "audio.h"

#include <AudioToolbox/AudioToolbox.h>

#include <algorithm>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

float interpolate_output_voltage(const CircuitGraph &graph, const Sample &a, const Sample &b,
                                 double time_s) {
    if (time_s <= a.time_s) {
        return static_cast<float>(a.node_voltages[graph.output]);
    }
    if (time_s >= b.time_s) {
        return static_cast<float>(b.node_voltages[graph.output]);
    }

    const double span = b.time_s - a.time_s;
    if (span <= 0.0) {
        return static_cast<float>(a.node_voltages[graph.output]);
    }

    const double alpha = (time_s - a.time_s) / span;
    const double va = a.node_voltages[graph.output];
    const double vb = b.node_voltages[graph.output];
    return static_cast<float>(va + alpha * (vb - va));
}

float peak_output_voltage(const CircuitGraph &graph, const SimulationTrace &trace) {
    float peak = 0.0f;
    for (const Sample &sample : trace.samples) {
        peak = std::max(peak, static_cast<float>(std::abs(sample.node_voltages[graph.output])));
    }
    return peak < 1e-12f ? 1.0f : peak;
}

void write_u16(std::ofstream &out, std::uint16_t value) {
    out.write(reinterpret_cast<const char *>(&value), sizeof(value));
}

void write_u32(std::ofstream &out, std::uint32_t value) {
    out.write(reinterpret_cast<const char *>(&value), sizeof(value));
}

void check_status(OSStatus status, const char *what) {
    if (status == noErr) {
        return;
    }
    throw std::runtime_error(std::string(what) + " failed with status " +
                             std::to_string(static_cast<int>(status)));
}

}  // namespace

struct SystemAudioOutput::Impl {
    AudioQueueRef queue = nullptr;
    std::vector<AudioQueueBufferRef> free_buffers;
    std::mutex mutex;
    std::condition_variable cv;
    int sample_rate = 44100;
    bool started = false;
    bool ended = false;
    static constexpr int kBufferCount = 4;
    static constexpr std::size_t kBufferSamples = 4096;

    static void handle_buffer(void *user_data, AudioQueueRef, AudioQueueBufferRef buffer) {
        Impl *impl = static_cast<Impl *>(user_data);
        std::lock_guard<std::mutex> lock(impl->mutex);
        impl->free_buffers.push_back(buffer);
        impl->cv.notify_one();
    }
};

WavFileOutput::WavFileOutput(std::string path) : path_(std::move(path)) {}

void WavFileOutput::begin(int sample_rate) {
    if (sample_rate <= 0) {
        throw std::runtime_error("audio sample rate must be positive");
    }
    sample_rate_ = sample_rate;
    samples_.clear();
}

void WavFileOutput::write_samples(const std::vector<float> &samples) {
    samples_.insert(samples_.end(), samples.begin(), samples.end());
}

void WavFileOutput::end() {
    std::ofstream out(path_, std::ios::binary);
    if (!out) {
        throw std::runtime_error("failed to open WAV output file: " + path_);
    }

    std::vector<std::int16_t> pcm;
    pcm.reserve(samples_.size());
    for (float sample : samples_) {
        pcm.push_back(static_cast<std::int16_t>(std::clamp(sample, -1.0f, 1.0f) * 32767.0f));
    }

    const std::uint16_t num_channels = 1;
    const std::uint16_t bits_per_sample = 16;
    const std::uint32_t byte_rate =
        static_cast<std::uint32_t>(sample_rate_ * num_channels * bits_per_sample / 8);
    const std::uint16_t block_align =
        static_cast<std::uint16_t>(num_channels * bits_per_sample / 8);
    const std::uint32_t data_size =
        static_cast<std::uint32_t>(pcm.size() * sizeof(std::int16_t));
    const std::uint32_t riff_size = 36 + data_size;

    out.write("RIFF", 4);
    write_u32(out, riff_size);
    out.write("WAVE", 4);
    out.write("fmt ", 4);
    write_u32(out, 16);
    write_u16(out, 1);
    write_u16(out, num_channels);
    write_u32(out, static_cast<std::uint32_t>(sample_rate_));
    write_u32(out, byte_rate);
    write_u16(out, block_align);
    write_u16(out, bits_per_sample);
    out.write("data", 4);
    write_u32(out, data_size);
    out.write(reinterpret_cast<const char *>(pcm.data()),
              static_cast<std::streamsize>(data_size));
}

SystemAudioOutput::SystemAudioOutput() : impl_(std::make_unique<Impl>()) {}

SystemAudioOutput::~SystemAudioOutput() {
    try {
        end();
    } catch (...) {
    }
}

void SystemAudioOutput::begin(int sample_rate) {
    if (sample_rate <= 0) {
        throw std::runtime_error("audio sample rate must be positive");
    }
    if (impl_->queue != nullptr) {
        end();
    }

    impl_->sample_rate = sample_rate;
    impl_->started = false;
    impl_->ended = false;
    impl_->free_buffers.clear();

    AudioStreamBasicDescription format{};
    format.mSampleRate = static_cast<Float64>(sample_rate);
    format.mFormatID = kAudioFormatLinearPCM;
    format.mFormatFlags = kLinearPCMFormatFlagIsSignedInteger | kLinearPCMFormatFlagIsPacked;
    format.mFramesPerPacket = 1;
    format.mChannelsPerFrame = 1;
    format.mBitsPerChannel = 16;
    format.mBytesPerPacket = 2;
    format.mBytesPerFrame = 2;

    check_status(AudioQueueNewOutput(&format, Impl::handle_buffer, impl_.get(), nullptr, nullptr,
                                     0, &impl_->queue),
                 "AudioQueueNewOutput");

    for (int i = 0; i < Impl::kBufferCount; ++i) {
        AudioQueueBufferRef buffer = nullptr;
        check_status(AudioQueueAllocateBuffer(
                         impl_->queue,
                         static_cast<UInt32>(Impl::kBufferSamples * sizeof(std::int16_t)),
                         &buffer),
                     "AudioQueueAllocateBuffer");
        impl_->free_buffers.push_back(buffer);
    }
}

void SystemAudioOutput::write_samples(const std::vector<float> &samples) {
    if (impl_->queue == nullptr) {
        throw std::runtime_error("system audio output not initialized");
    }

    std::size_t offset = 0;
    while (offset < samples.size()) {
        AudioQueueBufferRef buffer = nullptr;
        {
            std::unique_lock<std::mutex> lock(impl_->mutex);
            impl_->cv.wait(lock, [&] { return !impl_->free_buffers.empty(); });
            buffer = impl_->free_buffers.back();
            impl_->free_buffers.pop_back();
        }

        const std::size_t remaining = samples.size() - offset;
        const std::size_t chunk =
            std::min<std::size_t>(remaining, Impl::kBufferSamples);

        auto *pcm = static_cast<std::int16_t *>(buffer->mAudioData);
        for (std::size_t i = 0; i < chunk; ++i) {
            pcm[i] = static_cast<std::int16_t>(
                std::clamp(samples[offset + i], -1.0f, 1.0f) * 32767.0f);
        }
        buffer->mAudioDataByteSize = static_cast<UInt32>(chunk * sizeof(std::int16_t));

        check_status(AudioQueueEnqueueBuffer(impl_->queue, buffer, 0, nullptr),
                     "AudioQueueEnqueueBuffer");
        if (!impl_->started) {
            check_status(AudioQueuePrime(impl_->queue, 0, nullptr), "AudioQueuePrime");
            check_status(AudioQueueStart(impl_->queue, nullptr), "AudioQueueStart");
            impl_->started = true;
        }

        offset += chunk;
    }
}

void SystemAudioOutput::end() {
    if (impl_->ended) {
        return;
    }
    impl_->ended = true;

    if (impl_->queue != nullptr) {
        if (impl_->started) {
            check_status(AudioQueueFlush(impl_->queue), "AudioQueueFlush");
            check_status(AudioQueueStop(impl_->queue, false), "AudioQueueStop");
        }
        AudioQueueDispose(impl_->queue, true);
        impl_->queue = nullptr;
    }
    impl_->free_buffers.clear();
}

PcmBuffer render_trace_audio(const CircuitGraph &graph, const SimulationTrace &trace,
                             const AudioOptions &options) {
    if (trace.samples.empty()) {
        throw std::runtime_error("cannot render audio from an empty trace");
    }
    if (options.sample_rate <= 0) {
        throw std::runtime_error("audio sample rate must be positive");
    }

    PcmBuffer buffer;
    buffer.sample_rate = options.sample_rate;

    const double duration_s =
        std::max(trace.samples.back().time_s, 1.0 / static_cast<double>(options.sample_rate));
    const std::size_t num_samples =
        static_cast<std::size_t>(duration_s * options.sample_rate) + 1;
    const float peak = peak_output_voltage(graph, trace);

    buffer.samples.reserve(num_samples);
    Sample previous = trace.samples.front();
    Sample next = trace.samples.size() > 1 ? trace.samples[1] : trace.samples.front();
    std::size_t next_index = trace.samples.size() > 1 ? 1 : 0;

    for (std::size_t i = 0; i < num_samples; ++i) {
        const double t = static_cast<double>(i) / options.sample_rate;
        while (next_index + 1 < trace.samples.size() && trace.samples[next_index].time_s < t) {
            previous = trace.samples[next_index];
            ++next_index;
            next = trace.samples[next_index];
        }
        const float voltage = interpolate_output_voltage(graph, previous, next, t);
        buffer.samples.push_back(std::clamp(voltage / peak, -1.0f, 1.0f));
    }

    return buffer;
}

void stream_audio(StreamingSimulator &simulator, double duration_s, const CircuitGraph &graph,
                  const AudioOptions &options, AudioOutput &output,
                  const std::atomic_bool *stop_flag) {
    if (options.sample_rate <= 0) {
        throw std::runtime_error("audio sample rate must be positive");
    }
    if (options.chunk_size == 0) {
        throw std::runtime_error("audio chunk size must be positive");
    }

    const bool infinite = duration_s < 0.0;
    const std::size_t total_samples =
        infinite ? 0 : static_cast<std::size_t>(duration_s * options.sample_rate) + 1;

    output.begin(options.sample_rate);

    simulator.reset();
    Sample previous = simulator.step();
    Sample next = previous;
    if (infinite || simulator.state().time_s <= duration_s + 1e-12) {
        next = simulator.step();
    }

    const float peak_guess =
        std::max(1.0f, static_cast<float>(std::abs(graph.source.amplitude)));

    std::vector<float> chunk;
    chunk.reserve(options.chunk_size);

    for (std::size_t i = 0; infinite || i < total_samples; ++i) {
        if (stop_flag != nullptr && stop_flag->load()) {
            break;
        }

        const double t = static_cast<double>(i) / options.sample_rate;
        while (next.time_s < t &&
               (infinite || simulator.state().time_s <= duration_s + 1e-12)) {
            previous = next;
            next = simulator.step();
        }

        const float voltage = interpolate_output_voltage(graph, previous, next, t);
        chunk.push_back(std::clamp(voltage / peak_guess, -1.0f, 1.0f));

        if (chunk.size() >= options.chunk_size) {
            output.write_samples(chunk);
            chunk.clear();
        }
    }

    if (!chunk.empty()) {
        output.write_samples(chunk);
    }
    output.end();
}
